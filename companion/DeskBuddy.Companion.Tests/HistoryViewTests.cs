using System.Security.Cryptography;
using System.Text;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Companion.Schedule;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// P7.1: what the History page will show, projected from the real ledger's files, and the reversible annotations.
public sealed class HistoryViewTests : IDisposable
{
    private const string Dev = "441bf68d1178";
    private const string B1 = "00000000000000b1";
    private static readonly TimeZoneInfo Amsterdam = TimeZoneInfo.FindSystemTimeZoneById("W. Europe Standard Time");
    private static readonly DateTimeOffset T0 = new(2026, 10, 16, 8, 0, 0, TimeSpan.Zero); // 10:00 in Amsterdam
    private static readonly DateOnly Fri = new(2026, 10, 16);
    private static readonly Guid Block = Guid.Parse("6f1c2c1e-0000-4000-8000-0000000000bb");
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-historyview-" + Guid.NewGuid().ToString("N"));
    private uint _reply = 100;

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    private HistoryLedger Ledger(HistorySource source = HistorySource.Board)
    {
        var l = new HistoryLedger(_dir, source, "test-installation", Amsterdam);
        l.Load();
        l.Connected(Dev, B1, "29f5c80", T0);
        var fetch = l.TakeFetch(T0)!;
        l.ReplayAnswered(fetch, F($"<DB1 {++_reply} ok boot={B1} last=0 lost=0"), T0);
        return l;
    }

    private static Frame F(string content) =>
        Frame.TryParse($"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}", out _) ?? throw new InvalidOperationException(content);

    private static long Ms(DateTimeOffset t) => t.ToUnixTimeMilliseconds();

    private static void Did(HistoryLedger l, uint seq, string item, DateTimeOffset at) =>
        l.Received(F($"<DB1 0 evt did item=myday.{item} seq={seq} t={Ms(at)}"), at);

    private static void Ask(HistoryLedger l, uint seq, uint iid, string result, DateTimeOffset at, string name = "ask_peukie", string src = "cmd")
    {
        l.Received(F($"<DB1 0 evt act iid={iid} name={name} state=shown src={src} seq={seq} t={Ms(at)}"), at);
        l.Received(F($"<DB1 0 evt act iid={iid} state=done result={result} seq={seq + 1} t={Ms(at.AddSeconds(20))}"), at.AddSeconds(20));
    }

    private static IReadOnlyList<HistoryItem> Items(HistoryLedger l, HistoryAnnotations? a = null)
    {
        l.Commit();
        return HistoryView.Items(l.Snapshot, l.Entries, a ?? HistoryAnnotations.None);
    }

    private static HistoryDay Day(HistoryLedger l, DateOnly date, HistoryAnnotations? a = null, bool hidden = false) =>
        HistoryView.Day(Items(l, a), l.Snapshot, date, Amsterdam, hidden);

    private string Hash(string file) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(_dir, file))));

    [Fact]
    public void Entries_fall_on_the_local_calendar_day_not_the_utc_one()
    {
        var l = Ledger();
        Did(l, 1, "work", new DateTimeOffset(2026, 10, 16, 21, 59, 0, TimeSpan.Zero)); // 23:59 Friday in Amsterdam
        Did(l, 2, "coffee", new DateTimeOffset(2026, 10, 16, 22, 1, 0, TimeSpan.Zero)); // 00:01 Saturday
        Assert.Equal(["Working"], Day(l, Fri).Timeline.Select(i => i.Title));
        Assert.Equal(["Coffee"], Day(l, Fri.AddDays(1)).Timeline.Select(i => i.Title));
    }

    [Fact]
    public void Both_halves_of_the_repeated_DST_hour_stay_on_their_day_in_real_order()
    {
        var l = Ledger();
        Did(l, 1, "coffee", new DateTimeOffset(2026, 10, 25, 1, 30, 0, TimeSpan.Zero)); // 02:30 CET (the second 02:30)
        Did(l, 2, "water", new DateTimeOffset(2026, 10, 25, 0, 30, 0, TimeSpan.Zero));  // 02:30 CEST (the first)
        Did(l, 3, "work", new DateTimeOffset(2026, 10, 24, 22, 30, 0, TimeSpan.Zero));  // 00:30 CEST on Sunday 25th
        var sunday = Day(l, new DateOnly(2026, 10, 25));
        Assert.Equal(["Working", "Water", "Coffee"], sunday.Timeline.Select(i => i.Title));
        Assert.Empty(Day(l, new DateOnly(2026, 10, 24)).Timeline);
    }

    [Fact]
    public void Question_outcomes_keep_their_own_words_and_only_a_confirmed_shown_counts_as_asked()
    {
        var l = Ledger();
        Ask(l, 1, 11, "yes", T0);
        Ask(l, 3, 12, "no", T0.AddMinutes(1));
        Ask(l, 5, 13, "timeout", T0.AddMinutes(2));
        Ask(l, 7, 14, "interrupted", T0.AddMinutes(3));
        l.Received(F($"<DB1 0 evt act iid=15 name=ask_peukie state=shown src=cmd seq=9 t={Ms(T0.AddMinutes(4))}"), T0.AddMinutes(4)); // no outcome yet
        l.Requested(16, "ask_peukie", "companion", T0.AddMinutes(5));
        l.RequestAnswered(16, "busy", T0.AddMinutes(5));
        l.Requested(17, "ask_peukie", "companion", T0.AddMinutes(6));
        l.RequestAnswered(17, "no_reply", T0.AddMinutes(6));
        l.Requested(18, "ask_peukie", "companion", T0.AddMinutes(7));
        l.RequestAnswered(18, "ok", T0.AddMinutes(7)); // accepted, never confirmed shown
        var day = Day(l, Fri);
        var status = day.Timeline.ToDictionary(i => i.Interaction!.Iid, i => (i.Status, i.Displayed));
        Assert.Equal(("Yes", true), status[11]);
        Assert.Equal(("No", true), status[12]);
        Assert.Equal(("Timed out", true), status[13]);
        Assert.Equal(("Interrupted", true), status[14]);
        Assert.Equal(("No outcome received", true), status[15]);
        Assert.Equal(("Not started", false), status[16]);
        Assert.Equal(("Unconfirmed", false), status[17]);
        Assert.Equal(("Not shown", false), status[18]);
        var s = day.Stats;
        Assert.Equal((5, 1, 1, 1, 1, 1), (s.QuestionsShown, s.Yes, s.No, s.TimedOut, s.Interrupted, s.NoOutcomeYet));
        Assert.Equal((1, 1), (s.NotStarted, s.Unconfirmed));
    }

    [Fact]
    public void A_yes_smoke_break_sits_inside_its_question_and_a_my_day_smoke_stays_separate()
    {
        var l = Ledger();
        Ask(l, 1, 21, "yes", T0);
        Did(l, 3, "smoke", T0.AddMinutes(2)); // a manual smoke break shortly after: its own record, not merged
        var day = Day(l, Fri);
        Assert.Equal(2, day.Timeline.Count);
        var question = day.Timeline[0];
        Assert.Equal((HistoryItemKind.Question, "smoke"), (question.Kind, question.Linked?.Type));
        Assert.Equal((HistoryItemKind.Activity, "Smoking break"), (day.Timeline[1].Kind, day.Timeline[1].Title));
        Assert.Equal((2, 2, 1, 2), (day.Stats.Activities, day.Stats.SmokeBreaks, day.Stats.SmokeFromQuestions, day.Stats.ActivitiesByType["smoke"]));
    }

    [Fact]
    public void Shows_and_moods_never_count_as_something_you_did()
    {
        var l = Ledger();
        Ask(l, 1, 31, "done", T0, name: "sneeze");
        l.Received(F("<DB1 0 evt mood mood=working src=cmd"), T0);
        var day = Day(l, Fri);
        Assert.Equal((HistoryItemKind.Show, "Played"), (Assert.Single(day.Timeline).Kind, day.Timeline[0].Status));
        Assert.Equal((0, 1), (day.Stats.Activities, day.Stats.Shows));
    }

    [Fact]
    public void Uptime_only_entries_are_listed_apart_on_the_day_they_arrived_and_get_no_time()
    {
        var l = Ledger();
        l.Received(F("<DB1 0 evt did item=myday.coffee seq=1 up=1725"), T0.AddHours(2)); // received Friday 12:00
        Did(l, 2, "work", T0);
        var day = Day(l, Fri);
        Assert.Equal(["Working"], day.Timeline.Select(i => i.Title));
        var unknown = Assert.Single(day.UnknownTime);
        Assert.Equal(("Coffee", (DateTimeOffset?)null, 1725L, 1u), (unknown.Title, unknown.AtUtc, unknown.UptimeMs!.Value, unknown.Seq));
        Assert.Equal(2, day.Stats.Activities); // it still happened that day
        Assert.Empty(Day(l, Fri.AddDays(1)).UnknownTime);
    }

    [Fact]
    public void A_scheduled_action_refused_until_its_grace_period_ended_appears_once_as_skipped()
    {
        var l = Ledger();
        var occ = new ScheduleRef($"{Block}/2026-10-16", Block, new DateTime(2026, 10, 16, 10, 0, 0), T0, "Morning sneeze");
        l.Requested(41, "sneeze", "scheduled", T0, occ);
        l.RequestAnswered(41, "busy", T0);
        l.RequestAnswered(41, "busy", T0.AddSeconds(10));
        l.Skipped(occ, "sneeze", "busy:office", T0.AddMinutes(1));
        var day = Day(l, Fri);
        var item = Assert.Single(day.Timeline);
        Assert.Equal((HistoryItemKind.Skipped, "Skipped: Buddy was in Clock Mode"), (item.Kind, item.Status));
        Assert.Equal("Scheduled: \"Morning sneeze\" at 10:00", item.Origin);
        Assert.Equal(("busy:office", 1), day.Stats.SkipReasons.Single() is var r ? (r.Key, r.Value) : default);
        Assert.Equal(0, day.Stats.NotStarted); // not counted twice
    }

    [Fact]
    public void Hiding_a_question_hides_its_smoke_break_too_and_unhiding_brings_both_back()
    {
        var l = Ledger();
        Ask(l, 1, 51, "yes", T0);
        Did(l, 3, "coffee", T0.AddMinutes(5));
        var store = new AnnotationStore(_dir, HistorySource.Board, "test-installation");
        store.Load();
        var hide = store.HideItem($"{Dev}/{B1}/i51", "test", "P6 board test", T0);

        var day = Day(l, Fri, store.Current);
        Assert.Equal(["Coffee"], day.Timeline.Select(i => i.Title));
        Assert.Equal((1, 0, 0, 1), (day.Stats.Activities, day.Stats.SmokeBreaks, day.Stats.QuestionsShown, day.HiddenCount));
        var withHidden = Day(l, Fri, store.Current, hidden: true);
        Assert.True(withHidden.Timeline.Single(i => i.Kind == HistoryItemKind.Question) is { Hidden: true, LinkedHidden: true });
        Assert.Equal(1, withHidden.Stats.Activities); // shown, never counted

        store.Unhide(hide.Id, T0);
        var reloaded = new AnnotationStore(_dir, HistorySource.Board, "test-installation");
        reloaded.Load();
        Assert.Equal(2, reloaded.All.Count);
        var back = Day(l, Fri, reloaded.Current);
        Assert.Equal((2, 1, 0), (back.Stats.Activities, back.Stats.SmokeBreaks, back.HiddenCount));
    }

    [Fact]
    public void Hiding_only_the_smoke_break_keeps_the_question()
    {
        var l = Ledger();
        Ask(l, 1, 61, "yes", T0);
        var store = new AnnotationStore(_dir, HistorySource.Board, "test-installation");
        store.HideItem($"{Dev}/{B1}/i61/smoke", "test", null, T0);
        var day = Day(l, Fri, store.Current);
        var q = Assert.Single(day.Timeline);
        Assert.Equal((false, true, "Yes"), (q.Hidden, q.LinkedHidden, q.Status));
        Assert.Equal((0, 1, 1), (day.Stats.SmokeBreaks, day.Stats.QuestionsShown, day.Stats.Yes));
    }

    [Fact]
    public void A_period_preview_lists_exactly_what_hiding_it_would_hide()
    {
        var l = Ledger();
        Did(l, 1, "work", T0.AddMinutes(-1));                      // before
        Ask(l, 2, 71, "yes", T0);                                  // in
        Did(l, 4, "water", T0.AddMinutes(30));                     // in
        l.Received(F("<DB1 0 evt did item=myday.lunch seq=5 up=99"), T0.AddMinutes(40)); // received in
        Did(l, 6, "coffee", T0.AddHours(1));                       // the end is not included
        var items = Items(l);
        var preview = HistoryView.Affected(items, T0, T0.AddHours(1));
        Assert.Equal(["Peukie?", "Water", "Lunch"], preview.Select(i => i.Title));
        Assert.Equal("smoke", preview[0].Linked?.Type);

        var store = new AnnotationStore(_dir, HistorySource.Board, "test-installation");
        store.HidePeriod(T0, T0.AddHours(1), "test", "P6 board test", T0);
        var day = Day(l, Fri, store.Current);
        Assert.Equal(["Working", "Coffee"], day.Timeline.Select(i => i.Title));
        Assert.Empty(day.UnknownTime);
        Assert.Equal((2, 3), (day.Stats.Activities, day.HiddenCount)); // three entries (the smoke break is part of its question)
        Assert.Throws<ArgumentException>(() => store.HidePeriod(T0, T0, "test", null, T0));
    }

    [Fact]
    public void Annotations_never_touch_the_canonical_logs()
    {
        var l = Ledger();
        Ask(l, 1, 81, "yes", T0);
        l.Requested(82, "sneeze", "companion", T0);
        l.Commit();
        var events = Hash("device-events.jsonl");
        var journal = Hash("companion-events.jsonl");
        var store = new AnnotationStore(_dir, HistorySource.Board, "test-installation");
        var h = store.HideItem($"{Dev}/{B1}/i81", "test", null, T0);
        store.HidePeriod(T0, T0.AddDays(1), "test", null, T0);
        store.Unhide(h.Id, T0);
        Assert.Equal(events, Hash("device-events.jsonl"));
        Assert.Equal(journal, Hash("companion-events.jsonl"));
        var reopened = new HistoryLedger(_dir, HistorySource.Board, "test-installation", Amsterdam);
        reopened.Load();
        Assert.Equal(l.Snapshot.Interactions, reopened.Snapshot.Interactions);
    }

    [Fact]
    public void Simulator_history_and_its_annotations_stay_apart_from_the_board()
    {
        var board = Ledger();
        Did(board, 1, "work", T0);
        var sim = Ledger(HistorySource.Simulator);
        Did(sim, 1, "coffee", T0);
        var simNotes = new AnnotationStore(_dir, HistorySource.Simulator, "test-installation");
        simNotes.HidePeriod(T0.AddHours(-1), T0.AddHours(1), "test", null, T0);
        Assert.True(File.Exists(Path.Combine(_dir, "history-annotations.sim.jsonl")));
        Assert.False(File.Exists(Path.Combine(_dir, "history-annotations.jsonl")));

        var boardNotes = new AnnotationStore(_dir, HistorySource.Board, "test-installation");
        boardNotes.Load();
        Assert.Equal(["Working"], Day(board, Fri, boardNotes.Current).Timeline.Select(i => i.Title));
        Assert.Empty(Day(sim, Fri, simNotes.Current).Timeline);
        Assert.Equal(["Coffee"], Day(sim, Fri).Timeline.Select(i => i.Title));
    }

    [Fact]
    public void Schedule_state_changes_are_journaled_once_and_kept_out_of_the_timeline()
    {
        var l = Ledger();
        l.ScheduleStateChanged(true, null, T0);
        l.ScheduleStateChanged(true, null, T0.AddSeconds(1)); // the same again: nothing
        l.ScheduleStateChanged(true, T0.AddHours(1), T0.AddMinutes(1));
        l.ScheduleStateChanged(true, DateTimeOffset.MaxValue, T0.AddMinutes(2));
        l.ScheduleStateChanged(false, null, T0.AddMinutes(3));
        l.Commit();
        var again = new HistoryLedger(_dir, HistorySource.Board, "test-installation", Amsterdam);
        again.Load();
        again.ScheduleStateChanged(false, null, T0.AddMinutes(4)); // after a restart: already recorded
        again.Commit();
        Assert.Equal(4, File.ReadAllLines(Path.Combine(_dir, "companion-events.jsonl")).Count(x => x.Contains("\"kind\":\"schedule_state\"")));
        var day = HistoryView.Day(HistoryView.Items(again.Snapshot, again.Entries, HistoryAnnotations.None), again.Snapshot, Fri, Amsterdam);
        Assert.Empty(day.Timeline);
        Assert.Equal(["Schedule live", "Schedule paused until Fri 11:00", "Schedule paused until resumed", "Schedule switched off"],
                     day.Technical.Select(t => t.Text));
    }

    [Fact]
    public void Block_names_are_kept_with_scheduled_requests_and_skips()
    {
        var l = Ledger();
        var asked = new ScheduleRef($"{Block}/2026-10-16", Block, new DateTime(2026, 10, 16, 10, 0, 0), T0, "Smoke break");
        l.Requested(91, "ask_peukie", "scheduled", T0, asked);
        l.RequestAnswered(91, "ok", T0);
        Ask(l, 1, 91, "no", T0);
        l.Commit();
        var again = new HistoryLedger(_dir, HistorySource.Board, "test-installation", Amsterdam);
        again.Load();
        Assert.Equal("Smoke break", Assert.Single(again.Snapshot.Occurrences).BlockName);
        var item = Assert.Single(HistoryView.Items(again.Snapshot, again.Entries, HistoryAnnotations.None));
        Assert.Equal(("Scheduled: \"Smoke break\" at 10:00", "No"), (item.Origin, item.Status));
    }

    [Fact]
    public void Gaps_explain_missing_entries_in_the_timeline_and_restarts_stay_technical()
    {
        var l = Ledger();
        Did(l, 1, "work", T0);
        l.Disconnected();
        l.Connected(Dev, B1, "29f5c80", T0.AddHours(1));
        var fetch = l.TakeFetch(T0.AddHours(1))!;
        l.Received(F($"<DB1 0 evt did item=myday.coffee seq=9 t={Ms(T0.AddHours(1))} replay=1"), T0.AddHours(1));
        l.ReplayAnswered(fetch, F($"<DB1 {++_reply} ok boot={B1} last=9 lost=7"), T0.AddHours(1));
        var day = Day(l, Fri);
        Assert.Equal([HistoryItemKind.Activity, HistoryItemKind.Notice, HistoryItemKind.Activity], day.Timeline.Select(i => i.Kind));
        Assert.Equal("7 recorded entries lost", day.Timeline[1].Title);
    }

    [Fact]
    public void Todays_plan_comes_only_for_today_and_only_from_the_current_schedule()
    {
        var schedule = WeekSchedule.Empty.WithDay(DayOfWeek.Friday,
        [
            new ScheduleBlock { Name = "Work", Start = 510, End = 720, Mood = "working" },
            new ScheduleBlock { Name = "Empty", Start = 800, End = 820 },
            new ScheduleBlock { Name = "Smoke break", Start = 600, End = 612, Plans = "smoke", Action = "ask_peukie" },
        ]);
        var plan = HistoryView.TodayPlan(schedule, Fri, today: Fri);
        Assert.Equal(["Work", "Smoke break"], plan.Select(p => p.Name));
        Assert.Equal(new DateTime(2026, 10, 16, 10, 12, 0), plan[1].EndLocal);
        Assert.Empty(HistoryView.TodayPlan(schedule, Fri, today: Fri.AddDays(7)));
    }
}
