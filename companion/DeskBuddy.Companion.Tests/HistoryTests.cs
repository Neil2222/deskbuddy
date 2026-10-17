using System.Text;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Protocol;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The history (P5): the canonical device event log, replay and checkpoints, and what is derived from them. Each test
// works on files in a folder of its own, through the same ledger the Companion runs.
public sealed class HistoryTests : IDisposable
{
    private const string Dev = "441bf68d1178";
    private const string B1 = "00000000000000b1";
    private const string B2 = "00000000000000b2";
    private static readonly DateTimeOffset T0 = new(2026, 10, 16, 8, 0, 0, TimeSpan.Zero);
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-history-" + Guid.NewGuid().ToString("N"));
    private uint _replySeq = 100;

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    private HistoryLedger Open(HistorySource source = HistorySource.Board)
    {
        var ledger = new HistoryLedger(_dir, source, "test-installation", TimeZoneInfo.Utc);
        ledger.Load();
        return ledger;
    }

    private static Frame F(string content)
    {
        var line = $"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}";
        return Frame.TryParse(line, out _) ?? throw new InvalidOperationException(line);
    }

    private static Frame Evt(string payload, bool replay = false) => F($"<DB1 0 {payload}{(replay ? " replay=1" : "")}");

    private static long Ms(int seconds) => T0.AddSeconds(seconds).ToUnixTimeMilliseconds();

    private static string Shown(uint seq, uint iid, string name = "ask_peukie", string src = "cmd") =>
        $"evt act iid={iid} name={name} state=shown src={src} seq={seq} t={Ms((int)seq)}";

    private static string Done(uint seq, uint iid, string result) => $"evt act iid={iid} state=done result={result} seq={seq} t={Ms((int)seq)}";

    private static string Did(uint seq, string item) => $"evt did item=myday.{item} seq={seq} t={Ms((int)seq)}";

    private Frame Reply(string boot, uint last, uint lost) => F($"<DB1 {++_replySeq} ok boot={boot} last={last} lost={lost}");

    // Connects and answers the first log.since (the device has nothing kept beyond `last`, replayed by the caller).
    private static FetchRequest Connect(HistoryLedger l, string boot = B1)
    {
        l.Connected(Dev, boot, "29f5c80", T0);
        return l.TakeFetch(T0) ?? throw new InvalidOperationException("no log.since after hello");
    }

    private static void Live(HistoryLedger l, params string[] payloads)
    {
        foreach (var p in payloads)
        {
            l.Received(Evt(p), T0);
        }
    }

    private static void Replayed(HistoryLedger l, params string[] payloads)
    {
        foreach (var p in payloads)
        {
            l.Received(Evt(p, replay: true), T0);
        }
    }

    private string[] LogLines(string name = "device-events.jsonl") =>
        File.ReadAllLines(Path.Combine(_dir, name)).Where(l => l.Length > 0).ToArray();

    private static int Events(HistoryLedger l) => l.Entries.OfType<EventEntry>().Count();

    // ---- storage ----

    [Fact]
    public void Live_events_are_stored_with_their_identity_and_survive_a_restart()
    {
        var l = Open();
        var fetch = Connect(l);
        Assert.Equal($"log.since boot={B1} seq=0", fetch.Command);
        l.ReplayAnswered(fetch, Reply(B1, 0, 0), T0);
        Live(l, Shown(1, 7), Done(2, 7, "no"), Did(3, "coffee"));
        l.Commit();

        var again = Open();
        var e = again.Entries.OfType<EventEntry>().ToList();
        Assert.Equal([1u, 2u, 3u], e.Select(x => x.Seq));
        Assert.All(e, x => Assert.Equal((Dev, B1), (x.Dev, x.Boot)));
        Assert.Equal("evt did item=myday.coffee seq=3 t=" + Ms(3), e[2].Payload);
        Assert.Equal(3u, again.Checkpoint(Dev, B1));
        var line = LogLines().Last();
        Assert.Contains("\"src\":\"board\"", line);
        Assert.Contains("\"v\":1", line);
        Assert.Contains("\"recv_utc\":", line);
        Assert.Contains("\"inst\":\"test-installation\"", line);
    }

    [Fact]
    public void Replayed_and_repeated_events_are_stored_once()
    {
        var l = Open();
        var fetch = Connect(l);
        Live(l, Shown(1, 7), Done(2, 7, "yes"));
        Replayed(l, Shown(1, 7), Done(2, 7, "yes"));
        l.ReplayAnswered(fetch, Reply(B1, 2, 0), T0);
        Replayed(l, Shown(1, 7)); // a late or second replay
        Live(l, Done(2, 7, "yes"));
        l.Commit();
        Assert.Equal(2, Events(l));
        Assert.Equal(2, LogLines().Count(x => x.Contains("\"kind\":\"event\"")));
        Assert.Single(l.Snapshot.Interactions);
        Assert.Single(l.Snapshot.Activities); // the JA's smoke break, once
    }

    [Fact]
    public void Events_out_of_order_are_kept_and_the_checkpoint_waits_for_the_hole()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Did(1, "work"), Did(3, "water"));
        Assert.Equal(1u, l.Checkpoint(Dev, B1));
        var fetch = l.TakeFetch(T0); // the hole at 2 asks for it
        Assert.NotNull(fetch);
        Assert.Equal(1u, fetch.Since);
        Live(l, Did(4, "lunch")); // live events keep coming meanwhile
        Replayed(l, Did(2, "coffee"), Did(3, "water"));
        l.ReplayAnswered(fetch, Reply(B1, 3, 0), T0);
        Assert.Equal(4u, l.Checkpoint(Dev, B1));
        Assert.Null(l.TakeFetch(T0.AddMinutes(1)));
        l.Commit();
        Assert.Equal(["coffee", "lunch", "water", "work"], l.Snapshot.Activities.Select(a => a.Type).Order());
    }

    [Fact]
    public void Overflow_is_recorded_as_a_gap_of_known_size_and_the_checkpoint_moves_past_it()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Did(1, "work"), Did(2, "work"), Did(3, "work"));
        l.Disconnected();
        var fetch = Connect(l);
        Assert.Equal(3u, fetch.Since);
        Replayed(l, Did(10, "coffee"), Did(11, "water"), Did(12, "lunch")); // 4..9 dropped by the device
        l.ReplayAnswered(fetch, Reply(B1, 12, 6), T0);
        l.Commit();
        var gap = Assert.Single(l.Snapshot.Gaps);
        Assert.Equal((4u, 9u, 6u), (gap.From, gap.To, gap.Count));
        Assert.Equal(12u, l.Checkpoint(Dev, B1));
        Assert.Equal(6, l.Snapshot.Activities.Count); // nothing invented for the gap
        Assert.Single(LogLines(), x => x.Contains("\"kind\":\"gap\"") && x.Contains("\"count\":6"));
    }

    [Fact]
    public void Overflow_that_does_not_add_up_is_not_written_and_is_asked_again()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Did(1, "work"));
        l.Disconnected();
        var fetch = Connect(l);
        Replayed(l, Did(8, "coffee"), Did(9, "coffee"));
        l.ReplayAnswered(fetch, Reply(B1, 9, 7), T0); // 2..7 is 6 missing, not 7
        Assert.Empty(l.Snapshot.Gaps);
        Assert.Equal(1u, l.Checkpoint(Dev, B1));
        Assert.Null(l.TakeFetch(T0)); // not at once...
        var retry = l.TakeFetch(T0 + HistoryLedger.RetryDelay); // ...but after a pause
        Assert.NotNull(retry);
        Assert.Equal(1u, retry.Since);
    }

    [Fact]
    public void A_replayed_line_lost_on_the_way_leaves_the_hole_open_and_is_asked_again()
    {
        var l = Open();
        var fetch = Connect(l);
        Replayed(l, Did(1, "work"), Did(3, "water")); // 2 lost on the line
        l.ReplayAnswered(fetch, Reply(B1, 3, 0), T0);
        Assert.Equal(1u, l.Checkpoint(Dev, B1));
        var retry = l.TakeFetch(T0 + HistoryLedger.RetryDelay);
        Assert.NotNull(retry);
        Replayed(l, Did(2, "coffee"), Did(3, "water"));
        l.ReplayAnswered(retry, Reply(B1, 3, 0), T0);
        Assert.Equal(3u, l.Checkpoint(Dev, B1));
    }

    [Fact]
    public void A_restart_while_away_closes_the_previous_boot_as_possibly_incomplete()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Did(1, "work"), Did(2, "coffee"));
        l.Disconnected();
        var fetch = Connect(l, B2);
        Assert.Equal($"log.since boot={B2} seq=0", fetch.Command);
        l.Commit();
        var end = Assert.Single(l.Snapshot.BootEnds);
        Assert.Equal((B1, 2u, false), (end.Boot, end.After, end.SeenLive));

        // Seen again after a Companion restart: not closed twice.
        var again = Open();
        Connect(again, B2);
        again.Commit();
        Assert.Single(again.Snapshot.BootEnds);
    }

    [Fact]
    public void A_restart_seen_live_with_no_hole_is_marked_seen_live_not_complete()
    {
        var l = Open();
        var fetch = Connect(l);
        Live(l, Did(1, "work"));
        l.ReplayAnswered(fetch, Reply(B1, 1, 0), T0);
        Live(l, Did(2, "coffee"));
        l.Received(F($"<DB1 0 evt boot proto=1 fw=29f5c80 dev={Dev} boot={B2}"), T0);
        var next = l.TakeFetch(T0);
        Assert.NotNull(next);
        Assert.Equal($"log.since boot={B2} seq=0", next.Command);
        Live(l, Did(1, "water")); // the new boot counts from 1 again: a different key
        l.Commit();
        var end = Assert.Single(l.Snapshot.BootEnds);
        Assert.Equal((B1, 2u, true), (end.Boot, end.After, end.SeenLive));
        Assert.Equal(3, Events(l));
        Assert.DoesNotContain("complete", LogLines().Single(x => x.Contains("\"kind\":\"boot_end\"")));
    }

    private static Frame BootEvt(string boot) => F($"<DB1 0 evt boot proto=1 fw=29f5c80 dev={Dev} boot={boot}");

    [Fact]
    public void A_restart_into_the_hellos_boot_during_the_handshake_drops_what_came_before_it()
    {
        var l = Open();
        l.Received(Evt(Did(5, "work")), T0); // the old boot's, before it restarted: its boot cannot be named
        l.Received(BootEvt(B2), T0);
        l.Received(Evt(Did(1, "coffee")), T0); // the new boot's, before the hello reply was handled
        var fetch = Connect(l, B2);
        Assert.Equal($"log.since boot={B2} seq=1", fetch.Command); // its seq 1 is already stored
        l.Commit();
        var e = Assert.Single(l.Entries.OfType<EventEntry>());
        Assert.Equal((B2, 1u, "evt did item=myday.coffee seq=1 t=" + Ms(1)), (e.Boot, e.Seq, e.Payload));
    }

    [Fact]
    public void A_restart_after_the_hello_reply_but_before_it_was_handled_switches_to_the_new_boot()
    {
        var l = Open();
        l.Received(BootEvt(B2), T0); // read before the hello reply (from B1) reached the history
        l.Received(Evt(Did(1, "coffee")), T0);
        l.Connected(Dev, B1, "29f5c80", T0);
        var fetch = l.TakeFetch(T0);
        Assert.NotNull(fetch);
        Assert.Equal($"log.since boot={B2} seq=1", fetch.Command);
        l.Commit();
        Assert.Equal((B2, 1u), l.Entries.OfType<EventEntry>().Select(x => (x.Boot, x.Seq)).Single());
        var end = Assert.Single(l.Snapshot.BootEnds);
        Assert.Equal((B1, false), (end.Boot, end.SeenLive));
    }

    [Fact]
    public void A_reply_for_another_boot_is_retried_a_few_times_not_forever()
    {
        var l = Open();
        var fetch = Connect(l);
        var at = T0;
        for (var i = 0; i < HistoryLedger.MaxFetchAttempts; i++)
        {
            Assert.NotNull(fetch);
            l.ReplayAnswered(fetch, Reply(B2, 0, 0), at);
            at += HistoryLedger.RetryDelay;
            fetch = l.TakeFetch(at);
        }
        Assert.Null(fetch);
    }

    [Fact]
    public void After_a_crash_before_the_checkpoint_was_written_the_log_decides_and_nothing_is_stored_twice()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Did(1, "work"));
        l.Commit();
        var checkpoints = Path.Combine(_dir, "device-checkpoints.json");
        var before = File.ReadAllText(checkpoints); // checkpoint 1
        Live(l, Did(2, "coffee"), Did(3, "water"));
        l.Commit();
        File.WriteAllText(checkpoints, before); // as if the process died between the log append and the checkpoint

        var after = Open();
        Assert.Equal(3u, after.Checkpoint(Dev, B1)); // events 2 and 3 are in the log, so they count
        var fetch = Connect(after);
        Assert.Equal(3u, fetch.Since);
        Replayed(after, Did(2, "coffee"), Did(3, "water")); // a device answering an older checkpoint
        after.ReplayAnswered(fetch, Reply(B1, 3, 0), T0);
        after.Commit();
        Assert.Equal(3, LogLines().Count(x => x.Contains("\"kind\":\"event\"")));
        Assert.Contains("\"seq\":3", File.ReadAllText(checkpoints));
    }

    [Fact]
    public void A_checkpoint_ahead_of_the_log_is_not_trusted()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Did(1, "work"), Did(2, "coffee"));
        l.Commit();
        File.WriteAllText(Path.Combine(_dir, "device-checkpoints.json"),
            $"{{\"v\":1,\"checkpoints\":[{{\"dev\":\"{Dev}\",\"boot\":\"{B1}\",\"seq\":9}}]}}");
        var warnings = new List<string>();
        var again = new HistoryLedger(_dir, HistorySource.Board, "test-installation", TimeZoneInfo.Utc);
        again.Log += warnings.Add;
        again.Load();
        Assert.Equal(2u, again.Checkpoint(Dev, B1));
        Assert.Contains(warnings, w => w.Contains("only accounts for 2"));
        Assert.Equal(2u, Connect(again).Since);
    }

    [Fact]
    public void A_torn_last_line_is_skipped_and_never_swallows_the_next_one()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Did(1, "work"));
        l.Commit();
        File.AppendAllText(Path.Combine(_dir, "device-events.jsonl"), "{\"v\":1,\"kind\":\"event\",\"dev\":\"44"); // power cut mid-write

        var again = Open();
        Assert.Equal(1, Events(again));
        again.ReplayAnswered(Connect(again), Reply(B1, 1, 0), T0);
        Live(again, Did(2, "coffee"));
        again.Commit();
        Assert.Equal(2, Events(Open()));
    }

    // ---- time ----

    [Fact]
    public void An_uptime_only_event_has_no_date_and_a_t_event_keeps_its_utc_time()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, "evt did item=myday.work seq=1 up=1725", Did(2, "coffee"));
        l.Commit();
        var reloaded = Open().Snapshot.Activities;
        Assert.False(reloaded[0].StartedAt.Known);
        Assert.Null(reloaded[0].StartedAt.Utc);
        Assert.Equal(1725, reloaded[0].StartedAt.UptimeMs);
        Assert.Equal(T0.AddSeconds(2), reloaded[1].StartedAt.Utc);
        Assert.Contains("\"time_known\":false", LogLines().First(x => x.Contains("\"seq\":1")));
        Assert.Contains("\"t_utc\":null", LogLines().First(x => x.Contains("\"seq\":1")));
    }

    // ---- interactions ----

    [Fact]
    public void Outcomes_are_kept_as_reported_and_nothing_is_guessed()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Shown(1, 11), Done(2, 11, "yes"),
                Shown(3, 12), Done(4, 12, "no"),
                Shown(5, 13), Done(6, 13, "timeout"),
                Shown(7, 14), Done(8, 14, "interrupted"),
                Shown(9, 15), // no outcome (yet)
                Done(10, 16, "interrupted"), // cut off before it was shown
                "evt mood mood=peukie src=break"); // a mood is never an answer (and not a recorded event)
        l.Requested(17, "ask_peukie", "companion", T0);
        l.RequestAnswered(17, "ok", T0); // accepted, but never reported shown
        l.Requested(18, "ask_peukie", "companion", T0);
        l.RequestAnswered(18, "busy", T0);
        l.Requested(19, "ask_peukie", "companion", T0);
        l.RequestAnswered(19, "no_reply", T0);
        l.Commit();
        var i = l.Snapshot.Interactions.ToDictionary(x => x.Iid);
        Assert.Equal(InteractionOutcome.Yes, i[11].Outcome);
        Assert.Equal(InteractionOutcome.No, i[12].Outcome);
        Assert.Equal(InteractionOutcome.Timeout, i[13].Outcome);
        Assert.Equal(InteractionOutcome.Interrupted, i[14].Outcome);
        Assert.Equal((true, InteractionOutcome.Unknown), (i[15].Shown, i[15].Outcome));
        Assert.Equal((false, InteractionOutcome.Interrupted), (i[16].Shown, i[16].Outcome));
        Assert.Equal((false, InteractionOutcome.Unknown), (i[17].Shown, i[17].Outcome));
        Assert.Equal((false, InteractionOutcome.NotStarted), (i[18].Shown, i[18].Outcome));
        Assert.Equal((false, InteractionOutcome.Unconfirmed), (i[19].Shown, i[19].Outcome)); // sent, no reply: maybe, maybe not
        Assert.Equal(T0.AddSeconds(5), i[13].ShownAt?.Utc);
        Assert.Equal(T0.AddSeconds(6), i[13].EndedAt?.Utc);
        Assert.True(i[11].IsQuestion);
    }

    [Fact]
    public void The_origin_comes_from_the_device_and_the_companion_journal()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        l.Requested(21, "sneeze", "companion", T0);
        Live(l, Shown(1, 21, "sneeze"), Done(2, 21, "done"),
                Shown(3, 0x80000005, "ask_peukie", src: "touch"),
                Shown(4, 0x80000006, "sneeze")); // a command whose request is not in the journal
        l.Commit();
        var i = l.Snapshot.Interactions.ToDictionary(x => x.Iid);
        Assert.Equal(InteractionOrigin.Companion, i[21].Origin);
        Assert.NotNull(i[21].RequestedAtUtc);
        Assert.Equal(InteractionOrigin.Touchscreen, i[0x80000005].Origin);
        Assert.Equal(InteractionOrigin.Command, i[0x80000006].Origin);
        Assert.Equal($"{Dev}/{B1}/i21", i[21].Id);
    }

    // ---- activities ----

    [Fact]
    public void My_day_taps_are_activities_of_their_type()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        string[] items = ["work", "meeting", "coffee", "water", "smoke", "stretch", "lunch"];
        Live(l, items.Select((x, n) => Did((uint)n + 1, x)).ToArray());
        l.Commit();
        Assert.Equal(items, l.Snapshot.Activities.Select(a => a.Type));
        Assert.All(l.Snapshot.Activities, a => Assert.StartsWith("myday:myday.", a.Via));
        Assert.All(l.Snapshot.Activities, a => Assert.Null(a.EndedAt));
        Assert.Equal($"{Dev}/{B1}/3", l.Snapshot.Activities[2].Id);
    }

    [Fact]
    public void Ja_to_the_question_is_one_linked_smoke_break_and_a_manual_one_is_another()
    {
        var l = Open();
        var fetch = Connect(l);
        Live(l, Shown(1, 31), Done(2, 31, "yes"), Did(3, "smoke"));
        Replayed(l, Shown(1, 31), Done(2, 31, "yes"), Did(3, "smoke"));
        l.ReplayAnswered(fetch, Reply(B1, 3, 0), T0);
        Live(l, Shown(4, 32), Done(5, 32, "no"), Shown(6, 33), Done(7, 33, "timeout"));
        l.Commit();
        var smoke = l.Snapshot.Activities.Where(a => a.Type == "smoke").ToList();
        Assert.Equal(2, smoke.Count);
        Assert.Equal($"interaction:{Dev}/{B1}/i31", smoke[0].Via);
        Assert.Equal(T0.AddSeconds(2), smoke[0].StartedAt.Utc); // the answer time
        Assert.Equal("myday:myday.smoke", smoke[1].Via);
        Assert.Equal(smoke.Select(a => a.Id).Distinct().Count(), smoke.Count);
        Assert.Equal(l.Snapshot.Activities, Open().Snapshot.Activities); // rebuilt from the log: the same records
    }

    [Fact]
    public void Shows_and_moods_are_never_activities()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        Live(l, Shown(1, 41, "sneeze"), Done(2, 41, "done"), Shown(3, 42, "water"), Done(4, 42, "done"),
                "evt mood mood=working src=cmd", "evt mode mode=office");
        l.Commit();
        Assert.Equal(2, l.Snapshot.Interactions.Count);
        Assert.Empty(l.Snapshot.Activities);
    }

    // ---- scheduled actions (P6) ----

    private static readonly Guid Block = Guid.Parse("6f1c2c1e-0000-4000-8000-0000000000aa");

    private static ScheduleRef Occ(string date) =>
        new($"{Block}/{date}", Block, DateTime.Parse($"{date}T10:00"), DateTimeOffset.Parse($"{date}T08:00:00Z"));

    [Fact]
    public void A_scheduled_question_is_linked_to_its_occurrence_and_a_ja_is_one_smoke_break()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        l.Requested(51, "ask_peukie", "scheduled", T0, Occ("2026-10-16"));
        l.RequestAnswered(51, "ok", T0);
        Live(l, Shown(1, 51), Done(2, 51, "yes"));
        l.Commit();

        var again = Open(); // all of it from the files
        var o = Assert.Single(again.Snapshot.Occurrences);
        Assert.Equal(($"{Block}/2026-10-16", Block, 51u, "ok", true), (o.Id, o.BlockId, o.Iid, o.LastResult, o.Decided));
        Assert.Equal(new DateTime(2026, 10, 16, 10, 0, 0), o.PlannedLocal);
        var i = Assert.Single(again.Snapshot.Interactions);
        Assert.Equal((o.InteractionId, InteractionOrigin.Scheduled, true, InteractionOutcome.Yes), (i.Id, i.Origin, i.Shown, i.Outcome));
        var smoke = Assert.Single(again.Snapshot.Activities);
        Assert.Equal(("smoke", $"interaction:{i.Id}"), (smoke.Type, smoke.Via));
        var line = File.ReadAllLines(Path.Combine(_dir, "companion-events.jsonl")).First(x => x.Contains("\"kind\":\"request\""));
        Assert.Contains("\"origin\":\"scheduled\"", line);
        Assert.Contains($"\"occurrence\":\"{Block}/2026-10-16\"", line);
        Assert.Contains("\"planned\":\"2026-10-16T10:00\"", line);
        Assert.Contains("\"planned_utc\":\"2026-10-16T08:00:00.0000000+00:00\"", line);
    }

    [Fact]
    public void What_an_occurrence_is_after_a_restart_decides_whether_it_may_run_again()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        l.Requested(61, "sneeze", "scheduled", T0, Occ("2026-10-16")); // written, then the Companion died: no answer
        l.Requested(62, "sneeze", "scheduled", T0, Occ("2026-10-17"));
        l.RequestAnswered(62, "busy", T0); // Clock Mode: may be retried within the grace period
        l.Requested(63, "sneeze", "scheduled", T0, Occ("2026-10-18"));
        l.RequestAnswered(63, "busy", T0);
        l.RequestAnswered(63, "ok", T0); // a retry got through
        l.Requested(64, "sneeze", "scheduled", T0, Occ("2026-10-19"));
        l.RequestAnswered(64, "no_reply", T0); // uncertain: never again
        l.Disconnected();
        l.Skipped(Occ("2026-10-20"), "sneeze", "offline", T0); // recorded without a device
        l.Commit();

        var o = Open().Snapshot.Occurrences.ToDictionary(x => x.Id[^10..]);
        Assert.True(o["2026-10-16"].Decided);
        Assert.False(o["2026-10-17"].Decided);
        Assert.True(o["2026-10-18"].Decided);
        Assert.True(o["2026-10-19"].Decided);
        Assert.Equal(("offline", true, (string?)null), (o["2026-10-20"].Skipped, o["2026-10-20"].Decided, o["2026-10-20"].Dev));
        var i = Open().Snapshot.Interactions.ToDictionary(x => x.Iid);
        Assert.Equal(InteractionOutcome.NotStarted, i[62].Outcome);
        Assert.Equal(InteractionOutcome.Unknown, i[63].Outcome); // accepted, nothing heard yet: not shown
        Assert.False(i[63].Shown);
        Assert.Equal(InteractionOutcome.Unconfirmed, i[64].Outcome);
        Assert.Empty(Open().Snapshot.Activities); // skipped, refused or unconfirmed prompts say nothing about you
    }

    [Fact]
    public void An_answer_that_comes_after_the_link_went_is_still_recorded_for_its_device()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        l.Requested(71, "water", "scheduled", T0, Occ("2026-10-16"));
        l.Disconnected();
        l.RequestAnswered(71, "lost", T0);
        l.Commit();
        var o = Assert.Single(Open().Snapshot.Occurrences);
        Assert.Equal(("lost", Dev, B1), (o.LastResult, o.Dev, o.Boot));
    }

    [Fact]
    public void Busy_until_the_grace_period_ran_out_is_skipped_and_stays_decided_after_a_restart()
    {
        var l = Open();
        l.ReplayAnswered(Connect(l), Reply(B1, 0, 0), T0);
        l.Requested(81, "sneeze", "scheduled", T0, Occ("2026-10-16"));
        l.RequestAnswered(81, "busy", T0); // Clock Mode, retried...
        l.RequestAnswered(81, "busy", T0);
        l.Skipped(Occ("2026-10-16"), "sneeze", "busy:office", T0); // ...until the grace period ended
        l.Commit();
        var o = Assert.Single(Open().Snapshot.Occurrences);
        Assert.Equal(("busy:office", 81u, true), (o.Skipped, o.Iid, o.Decided));
        Assert.Equal(InteractionOutcome.NotStarted, Assert.Single(Open().Snapshot.Interactions).Outcome);
    }

    // ---- sources ----

    [Fact]
    public void Simulator_history_has_files_of_its_own()
    {
        var sim = Open(HistorySource.Simulator);
        sim.ReplayAnswered(Connect(sim), Reply(B1, 0, 0), T0);
        Live(sim, Did(1, "coffee"));
        sim.Requested(5, "sneeze", "companion", T0);
        sim.Commit();
        Assert.True(File.Exists(Path.Combine(_dir, "device-events.sim.jsonl")));
        Assert.True(File.Exists(Path.Combine(_dir, "companion-events.sim.jsonl")));
        Assert.True(File.Exists(Path.Combine(_dir, "device-checkpoints.sim.json")));
        Assert.False(File.Exists(Path.Combine(_dir, "device-events.jsonl")));
        Assert.Contains("\"src\":\"simulator\"", LogLines("device-events.sim.jsonl").Last());
        Assert.Empty(Open(HistorySource.Board).Snapshot.Activities);
    }
}
