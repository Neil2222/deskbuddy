using System.Text;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Companion.Schedule;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// P7.2: how the History page tells a day (rows, numbers, details), from the real ledger's records.
public sealed class HistoryJournalTests : IDisposable
{
    private const string Dev = "441bf68d1178";
    private const string B1 = "00000000000000b1";
    private static readonly TimeZoneInfo Amsterdam = TimeZoneInfo.FindSystemTimeZoneById("W. Europe Standard Time");
    private static readonly DateOnly Fri = new(2026, 10, 16);
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-journal-" + Guid.NewGuid().ToString("N"));
    private readonly HistoryLedger _l;
    private uint _seq;

    public HistoryJournalTests()
    {
        _l = new HistoryLedger(_dir, HistorySource.Board, "test-installation", Amsterdam);
        _l.Load();
        _l.Connected(Dev, B1, "29f5c80", At(8, 0));
        _l.ReplayAnswered(_l.TakeFetch(At(8, 0))!, F($"<DB1 101 ok boot={B1} last=0 lost=0"), At(8, 0));
    }

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    private static DateTimeOffset At(int h, int m, int s = 0) => new(2026, 10, 16, h - 2, m, s, TimeSpan.Zero); // CEST

    private static Frame F(string content) =>
        Frame.TryParse($"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}", out _) ?? throw new InvalidOperationException(content);

    private void Did(string item, DateTimeOffset at) =>
        _l.Received(F($"<DB1 0 evt did item=myday.{item} seq={++_seq} t={at.ToUnixTimeMilliseconds()}"), at);

    private void Act(uint iid, string name, string? result, DateTimeOffset at)
    {
        _l.Received(F($"<DB1 0 evt act iid={iid} name={name} state=shown src=touch seq={++_seq} t={at.ToUnixTimeMilliseconds()}"), at);
        if (result is not null)
        {
            var end = at.AddSeconds(20);
            _l.Received(F($"<DB1 0 evt act iid={iid} state=done result={result} seq={++_seq} t={end.ToUnixTimeMilliseconds()}"), end);
        }
    }

    private HistoryDay Day()
    {
        _l.Commit();
        return HistoryView.Day(HistoryView.Items(_l.Snapshot, _l.Entries, HistoryAnnotations.None), _l.Snapshot, Fri, Amsterdam);
    }

    private static IReadOnlyList<JournalRow> Rows(HistoryDay day, IReadOnlyList<PlanItem>? plan = null, bool today = true,
                                                  ISet<string>? open = null, bool technical = false, int hour = 16, int minute = 40) =>
        HistoryJournal.Rows(day, plan ?? [], today, (IReadOnlySet<string>)(open ?? new HashSet<string>()), technical,
                            Fri.ToDateTime(new TimeOnly(hour, minute)));

    private static IReadOnlyList<PlanItem> Plan() => HistoryView.TodayPlan(WeekSchedule.Empty.WithDay(DayOfWeek.Friday,
    [
        new ScheduleBlock { Name = "Afternoon work", Start = 12 * 60 + 45, End = 17 * 60 + 30, Mood = "working" },
        new ScheduleBlock { Name = "Smoke break", Start = 16 * 60 + 30, End = 16 * 60 + 45, Plans = "smoke", Action = "ask_peukie" },
        new ScheduleBlock { Name = "Wind down", Start = 17 * 60 + 30, End = 18 * 60, Mood = "relaxed" },
    ]), Fri, Fri);

    [Fact]
    public void Todays_plan_is_one_line_naming_the_block_in_force_and_the_next_until_opened()
    {
        var day = Day();
        var rows = Rows(day, Plan());
        var summary = rows.Single(r => r.Kind == JournalRowKind.PlanSummary);
        // Two blocks cover 16:40: the one that started last is in force.
        Assert.Equal("3 blocks · now Smoke break · next Wind down at 17:30", summary.Text);
        Assert.DoesNotContain(rows, r => r.Kind == JournalRowKind.Plan);

        var open = Rows(day, Plan(), open: new HashSet<string> { HistoryJournal.PlanGroup });
        var blocks = open.Where(r => r.Kind == JournalRowKind.Plan).ToList();
        Assert.Equal(["Afternoon work", "Smoke break", "Wind down"], blocks.Select(r => r.Text));
        Assert.Equal([false, true, false], blocks.Select(r => r.Current));
        Assert.All(blocks, r => Assert.True(r.Selectable));

        Assert.Equal("3 blocks · nothing more planned", Rows(day, Plan(), hour: 19, minute: 0).Single(r => r.Kind == JournalRowKind.PlanSummary).Text);
    }

    [Fact]
    public void Buddys_shows_in_a_row_fold_into_one_line_that_opens_in_place()
    {
        Did("coffee", At(9, 0));
        Act(0x80000001, "sneeze", "done", At(9, 10));
        Act(0x80000002, "stare", "done", At(9, 12));
        Act(0x80000003, "ask_peukie", "no", At(9, 20));
        Act(0x80000004, "meditate", "done", At(9, 30));
        var day = Day();
        var rows = Rows(day).Where(r => r.Kind is JournalRowKind.Entry or JournalRowKind.Shows).ToList();
        Assert.Equal([JournalRowKind.Entry, JournalRowKind.Shows, JournalRowKind.Entry, JournalRowKind.Shows], rows.Select(r => r.Kind));
        Assert.Equal(["Buddy played 2 shows", "Buddy played Meditate"], rows.Where(r => r.Kind == JournalRowKind.Shows).Select(r => r.Text));

        var key = rows[1].Key;
        var opened = Rows(day, open: new HashSet<string> { key }).Where(r => r.Kind is JournalRowKind.Entry or JournalRowKind.Shows).ToList();
        Assert.Equal(["Coffee", "Buddy played 2 shows", "Sneeze", "Stare", "Peukie?", "Buddy played Meditate"], opened.Select(r => r.Text));
        Assert.True(opened[2].Nested && opened[3].Nested);

        // A later event does not change the group's key: it stays open.
        Did("water", At(10, 0));
        Assert.Contains(Rows(Day(), open: new HashSet<string> { key }), r => r.Key == key && r.Open);
    }

    [Fact]
    public void Time_unknown_entries_follow_the_timeline_and_technical_changes_stay_folded()
    {
        Did("work", At(9, 0));
        _l.Received(F("<DB1 0 evt did item=myday.coffee seq=7 up=1725"), At(12, 0));
        _l.ScheduleStateChanged(true, null, At(8, 1));
        var day = Day();
        var rows = Rows(day);
        Assert.Equal(["h:day", Id(day.Timeline[0]), "h:unknown", Id(day.UnknownTime[0]), "technical"], rows.Select(r => r.Key));
        Assert.Equal("Technical: 1 change", rows[^1].Text);
        Assert.Equal("", HistoryJournal.Time(day.UnknownTime[0], Amsterdam));
        var open = Rows(day, technical: true);
        Assert.Equal("Schedule live", open[^1].Text);
        Assert.False(open[^1].Selectable);

        static string Id(HistoryItem i) => i.Id;
    }

    [Fact]
    public void An_empty_day_says_so_differently_today_and_on_a_past_day()
    {
        var day = Day();
        Assert.Equal("Nothing recorded yet today.", Rows(day).Single(r => r.Kind == JournalRowKind.Empty).Text);
        Assert.Equal("Nothing was recorded on this day.", Rows(day, today: false).Single(r => r.Kind == JournalRowKind.Empty).Text);
        Assert.Equal("What happened", Rows(day, today: false)[0].Text);
    }

    [Fact]
    public void The_question_numbers_count_what_was_shown_and_list_the_rest_apart()
    {
        Act(1, "ask_peukie", "yes", At(9, 0));
        Act(2, "ask_peukie", "timeout", At(10, 0));
        _l.Requested(3, "ask_peukie", "companion", At(11, 0));
        _l.RequestAnswered(3, "no_reply", At(11, 0));
        _l.Requested(4, "ask_peukie", "companion", At(11, 5));
        _l.RequestAnswered(4, "busy", At(11, 5));
        Did("smoke", At(12, 0));
        var tiles = HistoryJournal.Tiles(Day().Stats);
        Assert.Equal(("Questions asked", 2, "1 yes · 1 timed out · +1 not started · +1 unconfirmed"), (tiles[2].Label, tiles[2].Value, tiles[2].Detail));
        Assert.Equal(("Smoke breaks", 2, "1 after a yes · 1 via MY DAY"), (tiles[1].Label, tiles[1].Value, tiles[1].Detail));
        Assert.Equal(("Activities", 2, "2 smoking break"), (tiles[0].Label, tiles[0].Value, tiles[0].Detail));
        Assert.Equal(("Skipped", 0, "Nothing skipped"), (tiles[3].Label, tiles[3].Value, tiles[3].Detail));
    }

    [Fact]
    public void Only_a_yes_reads_green_and_a_timeout_never_reads_as_a_no()
    {
        Act(1, "ask_peukie", "yes", At(9, 0));
        Act(2, "ask_peukie", "no", At(9, 5));
        Act(3, "ask_peukie", "timeout", At(9, 10));
        Act(4, "ask_peukie", "interrupted", At(9, 15));
        _l.Requested(5, "ask_peukie", "companion", At(9, 20));
        _l.RequestAnswered(5, "no_reply", At(9, 20));
        var tones = Day().Timeline.ToDictionary(i => i.Interaction!.Iid, HistoryJournal.Tone);
        Assert.Equal(JournalTone.Positive, tones[1]);
        Assert.Equal(JournalTone.Negative, tones[2]);
        Assert.Equal(JournalTone.Warning, tones[3]);
        Assert.Equal(JournalTone.Muted, tones[4]);
        Assert.Equal(JournalTone.Muted, tones[5]);
    }

    [Fact]
    public void The_details_explain_an_unconfirmed_question_without_calling_it_an_answer()
    {
        _l.Requested(5, "ask_peukie", "companion", At(14, 30));
        _l.RequestAnswered(5, "no_reply", At(14, 30, 5));
        var info = HistoryJournal.Describe(Day().Timeline.Single(), Amsterdam);
        Assert.Equal(("Peukie?", "Buddy's question", "Unconfirmed"), (info.Title, info.Kind, info.Status));
        Assert.Contains(("Requested", "14:30:00"), info.Fields);
        Assert.DoesNotContain(info.Fields, f => f.Label == "Answered");
        Assert.Equal("Sent, but DeskBuddy never confirmed it. It may or may not have appeared.", Assert.Single(info.Notes));
        Assert.Contains(("Request result", "no_reply"), info.Technical);
    }

    [Fact]
    public void A_yes_shows_the_smoke_break_it_counted_and_a_timeout_says_it_was_not_a_no()
    {
        Act(1, "ask_peukie", "yes", At(9, 30));
        Act(2, "ask_peukie", "timeout", At(13, 30));
        var day = Day();
        var yes = HistoryJournal.Describe(day.Timeline[0], Amsterdam);
        Assert.Contains(("Counted as", "Smoking break at 09:30:20"), yes.Fields);
        Assert.Contains(("Answered", "09:30:20"), yes.Fields);
        var timeout = HistoryJournal.Describe(day.Timeline[1], Amsterdam);
        Assert.Contains(("Ended", "13:30:20"), timeout.Fields);
        Assert.Equal("Shown, but nobody answered before it ended. That is not a no.", Assert.Single(timeout.Notes));
    }

    [Fact]
    public void An_uptime_only_entry_names_its_arrival_as_an_arrival_never_as_its_time()
    {
        _l.Received(F("<DB1 0 evt did item=myday.coffee seq=7 up=1725"), At(12, 4));
        var item = Day().UnknownTime.Single();
        var info = HistoryJournal.Describe(item, Amsterdam);
        Assert.Contains(("When", "Time unknown"), info.Fields);
        Assert.DoesNotContain(info.Fields, f => f.Value.Contains("12:04"));
        Assert.Contains(info.Notes, n => n.Contains("Fri 16 Oct 12:04: that is when it arrived, not when it happened"));
        Assert.Equal("MY DAY · time unknown", HistoryJournal.Line(item));
    }

    [Fact]
    public void Day_labels_name_today_and_yesterday_and_add_the_year_only_when_it_differs()
    {
        Assert.Equal("Today", HistoryJournal.DateLabel(Fri, Fri));
        Assert.Equal("Yesterday", HistoryJournal.DateLabel(Fri.AddDays(-1), Fri));
        Assert.Equal("Tue 13 Oct", HistoryJournal.DateLabel(Fri.AddDays(-3), Fri));
        Assert.Equal("Thu 16 Oct 2025", HistoryJournal.DateLabel(new DateOnly(2025, 10, 16), Fri));
    }
}
