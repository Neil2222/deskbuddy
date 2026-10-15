using DeskBuddy.Companion.Schedule;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// When a scheduled action runs (P6): once per block per local day, within its grace period, never late, never twice.
public class ActionSchedulerTests
{
    private static readonly TimeZoneInfo Amsterdam = TimeZoneInfo.FindSystemTimeZoneById("W. Europe Standard Time");
    private static readonly DateTime Wed = new(2026, 10, 14); // a Wednesday, CEST (UTC+2)

    private static ScheduleBlock Block(string start, string action, string end = "") =>
        new() { Start = DayTime.Parse(start)!.Value, End = DayTime.Parse(end.Length > 0 ? end : "23:00")!.Value, Action = action };

    private static WeekSchedule Live(DayOfWeek day, params ScheduleBlock[] blocks) =>
        WeekSchedule.Empty.WithDay(day, blocks).WithLive(true);

    private static DateTimeOffset At(string time, DateTime? date = null) =>
        ActionScheduler.ToUtc((date ?? Wed).AddMinutes(DayTime.Parse(time)!.Value), Amsterdam);

    private static ActionScheduler Fresh(params string[] decided) => new(Amsterdam, decided);

    // Ticks once a second from `from` to `to`; returns every decision with the time it was made.
    private static List<(DateTimeOffset At, ActionDecision D)> Run(ActionScheduler s, WeekSchedule w, DateTimeOffset from, DateTimeOffset to,
                                                                   Func<DateTimeOffset, bool>? connected = null)
    {
        var all = new List<(DateTimeOffset, ActionDecision)>();
        for (var t = from; t <= to; t = t.AddSeconds(1))
        {
            all.AddRange(s.Tick(w, t, connected?.Invoke(t) ?? true).Select(d => (t, d)));
        }
        return all;
    }

    [Fact]
    public void Off_by_default_and_off_sends_and_records_nothing()
    {
        var w = WeekSchedule.Empty.WithDay(DayOfWeek.Wednesday, [Block("10:00", "sneeze")]);
        Assert.False(w.Live);
        var s = Fresh();
        Assert.Empty(Run(s, w, At("09:59"), At("10:05")));
    }

    [Fact]
    public void A_due_action_is_sent_once_at_its_start_and_not_again_that_day()
    {
        var w = Live(DayOfWeek.Wednesday, Block("10:00", "ask_peukie"));
        var s = Fresh();
        s.Tick(w, At("09:58"), true);
        var sent = Run(s, w, At("09:59"), At("10:00").AddSeconds(5));
        var (when, d) = Assert.Single(sent);
        Assert.Equal((At("10:00"), ActionStep.Send), (when, d.Step));
        Assert.Equal($"{w[DayOfWeek.Wednesday][0].Id}/2026-10-14", d.Occurrence.Id);
        s.Answered(d.Occurrence.Id);
        Assert.Empty(Run(s, w, At("10:00").AddSeconds(6), At("22:00").AddSeconds(-1)));
    }

    [Fact]
    public void Busy_is_retried_every_10_seconds_and_skipped_when_the_grace_period_ends()
    {
        var w = Live(DayOfWeek.Wednesday, Block("10:00", "sneeze")); // a show: 1 minute
        var s = Fresh();
        s.Tick(w, At("09:59"), true);
        var sends = new List<DateTimeOffset>();
        ActionDecision? skip = null;
        for (var t = At("10:00"); t <= At("10:02"); t = t.AddSeconds(1))
        {
            foreach (var d in s.Tick(w, t, true))
            {
                if (d.Step == ActionStep.Send)
                {
                    sends.Add(t);
                    s.Busy(d.Occurrence.Id, t, "office"); // Clock Mode: refused, never interrupted
                }
                else
                {
                    skip = d;
                }
            }
        }
        Assert.Equal(7, sends.Count); // 10:00:00, :10 … :60
        Assert.All(sends.Zip(sends.Skip(1)), p => Assert.Equal(TimeSpan.FromSeconds(10), p.Second - p.First));
        Assert.Equal("busy:office", skip?.Reason);
        Assert.True(s.IsDecided(skip!.Occurrence.Id));
    }

    [Fact]
    public void A_question_waits_up_to_three_minutes_a_show_one()
    {
        var w = Live(DayOfWeek.Wednesday, Block("10:00", "ask_peukie"), Block("10:00", "water"));
        var s = Fresh();
        s.Tick(w, At("09:59"), true);
        var decisions = Run(s, w, At("10:00"), At("10:04"), connected: _ => false); // offline throughout
        Assert.Equal(2, decisions.Count);
        Assert.All(decisions, x => Assert.Equal((ActionStep.Skip, "offline"), (x.D.Step, x.D.Reason)));
        Assert.Equal(At("10:01").AddSeconds(1), decisions.Single(x => x.D.Occurrence.Action == "water").At);
        Assert.Equal(At("10:03").AddSeconds(1), decisions.Single(x => x.D.Occurrence.Action == "ask_peukie").At);
    }

    [Fact]
    public void Connecting_within_the_grace_period_still_runs_it()
    {
        var w = Live(DayOfWeek.Wednesday, Block("10:00", "ask_peukie"));
        var s = Fresh();
        s.Tick(w, At("09:59"), false);
        var d = Assert.Single(Run(s, w, At("10:00"), At("10:02"), connected: t => t >= At("10:02")));
        Assert.Equal((At("10:02"), ActionStep.Send), (d.At, d.D.Step));
    }

    [Fact]
    public void Missed_while_the_Companion_was_off_or_asleep_is_skipped_never_run_late()
    {
        var w = Live(DayOfWeek.Wednesday, Block("08:00", "sneeze"), Block("10:00", "stare"), Block("10:30", "focus", "11:00"));
        var s = Fresh();
        var start = s.Tick(w, At("09:00"), true); // started at 09:00: 08:00 is long gone
        Assert.Equal((ActionStep.Skip, "companion_off"), (Assert.Single(start).Step, start[0].Reason));
        s.Tick(w, At("09:59"), true);
        var woke = s.Tick(w, At("10:40"), true); // asleep from 09:59 to 10:40
        Assert.Equal(2, woke.Count);
        Assert.All(woke, d => Assert.Equal((ActionStep.Skip, "asleep"), (d.Step, d.Reason)));
    }

    [Fact]
    public void After_a_restart_a_decided_occurrence_is_not_sent_again()
    {
        var w = Live(DayOfWeek.Wednesday, Block("10:00", "ask_peukie"));
        var id = $"{w[DayOfWeek.Wednesday][0].Id}/2026-10-14";
        var s = Fresh(id); // the journal says it was requested (whatever its outcome)
        Assert.Empty(Run(s, w, At("10:00").AddSeconds(30), At("10:05")));
    }

    [Fact]
    public void Paused_occurrences_are_skipped_and_resuming_does_not_run_them()
    {
        var w = Live(DayOfWeek.Wednesday, Block("10:00", "ask_peukie"), Block("11:00", "water"));
        var paused = w.WithPause(At("10:30"));
        var s = Fresh();
        s.Tick(paused, At("09:59"), true);
        var during = Run(s, paused, At("10:00"), At("10:29"));
        Assert.Equal((ActionStep.Skip, "paused"), (Assert.Single(during).D.Step, during[0].D.Reason));
        var after = Run(s, paused, At("10:30"), At("11:00")); // the pause ran out: the 10:00 one stays skipped
        Assert.Equal((At("11:00"), ActionStep.Send, "water"), (Assert.Single(after).At, after[0].D.Step, after[0].D.Occurrence.Action));
    }

    [Fact]
    public void Switched_on_during_the_day_leaves_earlier_occurrences_alone()
    {
        var off = WeekSchedule.Empty.WithDay(DayOfWeek.Wednesday, [Block("10:00", "sneeze"), Block("11:00", "water")]);
        var s = Fresh();
        s.Tick(off, At("10:00"), true);
        var later = Run(s, off.WithLive(true), At("10:00").AddSeconds(30), At("11:00"));
        Assert.Equal(("water", ActionStep.Send), (Assert.Single(later).D.Occurrence.Action, later[0].D.Step));
    }

    [Fact]
    public void Not_sent_after_all_stays_due_within_its_grace_period()
    {
        var w = Live(DayOfWeek.Wednesday, Block("10:00", "sneeze"));
        var s = Fresh();
        s.Tick(w, At("09:59"), true);
        var first = Assert.Single(s.Tick(w, At("10:00"), true));
        Assert.Empty(s.Tick(w, At("10:00").AddSeconds(1), true)); // in flight: nothing more
        s.NotSent(first.Occurrence.Id); // the link went between the journal and the send
        Assert.Equal(ActionStep.Send, Assert.Single(s.Tick(w, At("10:00").AddSeconds(2), true)).Step);
    }

    [Fact]
    public void An_answer_after_the_grace_period_is_never_also_a_skip()
    {
        var w = Live(DayOfWeek.Wednesday, Block("10:00", "sneeze"));
        var s = Fresh();
        s.Tick(w, At("09:59"), true);
        var sent = Assert.Single(s.Tick(w, At("10:00").AddSeconds(59), true));
        Assert.Empty(s.Tick(w, At("10:01").AddSeconds(5), true)); // past the deadline, still waiting for the reply
        s.Answered(sent.Occurrence.Id);
        Assert.Empty(s.Tick(w, At("10:01").AddSeconds(6), true));
    }

    [Fact]
    public void In_the_repeated_DST_hour_it_runs_once_and_in_the_skipped_hour_at_the_first_minute()
    {
        // Sunday 25 Oct 2026: 03:00 CEST becomes 02:00 CET, so 02:30 happens twice (00:30 and 01:30 UTC).
        var w = Live(DayOfWeek.Sunday, Block("02:30", "ask_peukie", "04:00"));
        var s = Fresh();
        var start = new DateTimeOffset(2026, 10, 25, 0, 0, 0, TimeSpan.Zero); // 02:00 CEST
        var first = Assert.Single(Run(s, w, start, start.AddMinutes(31)));
        Assert.Equal(new DateTimeOffset(2026, 10, 25, 2, 30, 0, TimeSpan.FromHours(2)), first.At); // the first 02:30
        Assert.EndsWith("/2026-10-25", first.D.Occurrence.Id);
        s.Answered(first.D.Occurrence.Id);
        Assert.Empty(Run(s, w, start.AddMinutes(31).AddSeconds(1), start.AddHours(3))); // the second 02:30 CET: nothing

        // Sunday 29 Mar 2026: 02:00 CET becomes 03:00 CEST, so 02:30 does not exist: it runs at 03:00.
        Assert.Equal(new DateTimeOffset(2026, 3, 29, 3, 0, 0, TimeSpan.FromHours(2)),
                     ActionScheduler.ToUtc(new DateTime(2026, 3, 29, 2, 30, 0), Amsterdam));
    }

    [Fact]
    public void A_late_block_reaches_past_midnight_with_yesterdays_date()
    {
        var w = Live(DayOfWeek.Wednesday, Block("23:59", "ask_peukie", "24:00"));
        var s = Fresh();
        s.Tick(w, At("23:58"), false);
        var thursday = Wed.AddDays(1);
        var d = Assert.Single(Run(s, w, At("23:59"), At("00:01", thursday).AddSeconds(30), connected: t => t >= At("00:01", thursday)));
        Assert.Equal((At("00:01", thursday), ActionStep.Send), (d.At, d.D.Step));
        Assert.EndsWith("/2026-10-14", d.D.Occurrence.Id);
    }

    [Fact]
    public void Before_the_first_connection_a_miss_is_offline_if_the_Companion_was_already_running()
    {
        var w = Live(DayOfWeek.Wednesday, Block("08:00", "sneeze"), Block("09:30", "water"));
        var s = new ActionScheduler(Amsterdam, [], runningSince: At("09:00")); // started 09:00, first device at 10:00
        var first = s.Tick(w, At("10:00"), true).ToDictionary(d => d.Occurrence.Action, d => d.Reason);
        Assert.Equal("companion_off", first["sneeze"]);
        Assert.Equal("offline", first["water"]);
    }

    [Fact]
    public void Only_the_allowed_actions_can_be_scheduled()
    {
        Assert.Equal(["ask_peukie", "sneeze", "stare", "meditate", "focus", "water"], ScheduledActions.All.Select(a => a.Id));
        Assert.NotNull(WeekSchedule.Problem(new ScheduleBlock { Start = 600, End = 660, Action = "monster" }));
        Assert.Null(WeekSchedule.Problem(new ScheduleBlock { Start = 600, End = 660, Action = "ask_peukie" }));
        Assert.Equal(TimeSpan.FromMinutes(3), ScheduledActions.Grace("ask_peukie"));
        Assert.Equal(TimeSpan.FromMinutes(1), ScheduledActions.Grace("water"));
    }
}
