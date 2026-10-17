using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// P7.4: the schedule's effective state and the sidebar's quick controls, on the real Schedule page and store in a
// temporary folder (never the user's schedule.json).
public sealed class ScheduleStatusTests : IDisposable
{
    private static readonly TimeZoneInfo Amsterdam = StillClock.Amsterdam;
    private static readonly DateTimeOffset Now = new(2026, 10, 16, 14, 40, 0, TimeSpan.Zero); // Fri 16:40 in Amsterdam
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-schedstatus-" + Guid.NewGuid().ToString("N"));

    public ScheduleStatusTests() => Directory.CreateDirectory(_dir);

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    private sealed class MovingClock(DateTimeOffset now) : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = now;
        public override DateTimeOffset GetUtcNow() => Now.ToUniversalTime();
        public override TimeZoneInfo LocalTimeZone => Amsterdam;
    }

    private static readonly WeekSchedule Week = WeekSchedule.Empty.WithDay(DayOfWeek.Friday,
        [new ScheduleBlock { Name = "Work", Start = 9 * 60, End = 17 * 60, Mood = "working" }]);

    private static ScheduleStatusInfo Of(WeekSchedule s, bool connected = true) => ScheduleStatus.Of(s, Now, Amsterdam, connected);

    [Fact]
    public void Every_effective_state_has_its_own_words_and_off_wins_over_a_pause()
    {
        Assert.Equal((ScheduleState.Off, "Schedule Off"), Pick(Of(Week)));
        Assert.Equal((ScheduleState.Off, "Schedule Off"), Pick(Of(Week.WithPause(Now.AddHours(1))))); // a pause of a schedule that is off
        Assert.Equal((ScheduleState.Live, "Schedule Live"), Pick(Of(Week.WithLive(true))));
        Assert.Equal((ScheduleState.Paused, "Paused until 17:40"), Pick(Of(Week.WithLive(true).WithPause(Now.AddHours(1)))));
        Assert.Equal((ScheduleState.PausedUntilResumed, "Paused until resumed"), Pick(Of(Week.WithLive(true).WithPause(WeekSchedule.UntilResumed))));
        Assert.Equal((ScheduleState.Live, "Schedule Live"), Pick(Of(Week.WithLive(true).WithPause(Now.AddMinutes(-1))))); // ran out

        static (ScheduleState, string) Pick(ScheduleStatusInfo i) => (i.State, i.Text);
    }

    [Fact]
    public void A_pause_names_its_end_briefly_in_the_pill_and_fully_in_the_detail()
    {
        string Until(DateTimeOffset until) => Of(Week.WithLive(true).WithPause(until)).Text;
        Assert.Equal("Paused until midnight", Until(ScheduleStatus.LocalMidnight(Now, Amsterdam, 1)));
        Assert.Equal("Paused until Mon 19 Oct 00:00", Until(ScheduleStatus.LocalMidnight(Now, Amsterdam, 3)));
        Assert.Equal("Paused until Sat 17 Oct 09:00", Until(new DateTimeOffset(2026, 10, 17, 7, 0, 0, TimeSpan.Zero)));
        Assert.Equal("Paused until Monday 19 October 00:00: no scheduled moods or actions; nothing missed is done afterwards.",
                     Of(Week.WithLive(true).WithPause(ScheduleStatus.LocalMidnight(Now, Amsterdam, 3))).Detail);
    }

    [Fact]
    public void Off_and_paused_never_read_as_running_and_live_says_when_actions_are_skipped()
    {
        Assert.DoesNotContain("follows", Of(Week).Detail);
        Assert.Contains("no scheduled moods or actions", Of(Week.WithLive(true).WithPause(WeekSchedule.UntilResumed)).Detail);
        Assert.Equal("Live: Buddy follows the schedule's moods and actions.", Of(Week.WithLive(true)).Detail);
        Assert.EndsWith("DeskBuddy is not connected: scheduled actions are skipped until it is.", Of(Week.WithLive(true), connected: false).Detail);
        Assert.StartsWith("Live, but the week has no blocks", Of(WeekSchedule.Empty.WithLive(true)).Detail);
    }

    [Fact]
    public void The_quick_controls_offer_what_applies_in_each_state()
    {
        string[] Labels(ScheduleState s) => ScheduleStatus.Commands(s).Select(ScheduleStatus.Label).ToArray();
        Assert.Equal(["Turn Live on", "Open Schedule"], Labels(ScheduleState.Off));
        Assert.Equal(["Pause for 1 hour", "Pause for the rest of today", "2 days (today and the next 1)", "3 days (today and the next 2)",
                      "5 days (today and the next 4)", "7 days (today and the next 6)", "14 days (today and the next 13)",
                      "Pause until I resume", "Turn Off", "Open Schedule"], Labels(ScheduleState.Live));
        Assert.Equal(["Resume", "Turn Off", "Open Schedule"], Labels(ScheduleState.Paused));
        Assert.Equal(["Resume", "Turn Off", "Open Schedule"], Labels(ScheduleState.PausedUntilResumed));
    }

    [Fact]
    public void Away_ends_at_a_local_midnight_across_the_clock_change()
    {
        var saturday = new DateTimeOffset(2026, 10, 24, 10, 0, 0, TimeSpan.Zero); // the clocks go back on Sunday 25th
        var until = ScheduleStatus.PauseUntil(new(ScheduleCommandKind.Away, 3), saturday, Amsterdam)!.Value;
        Assert.Equal(new DateTimeOffset(2026, 10, 27, 0, 0, 0, TimeSpan.FromHours(1)), until);
        Assert.Equal(new DateTimeOffset(2026, 10, 25, 0, 0, 0, TimeSpan.FromHours(2)),
                     ScheduleStatus.PauseUntil(new(ScheduleCommandKind.PauseToday), saturday, Amsterdam));
        Assert.Equal(Now.AddHours(1), ScheduleStatus.PauseUntil(new(ScheduleCommandKind.PauseHour), Now, Amsterdam));
        Assert.Equal(WeekSchedule.UntilResumed, ScheduleStatus.PauseUntil(new(ScheduleCommandKind.PauseUntilResumed), Now, Amsterdam));
    }

    // The Schedule page on this test's folder and the sidebar's quick controls on it, wired as the window wires them;
    // the body runs once the window is shown.
    private void WithPage(MovingClock clock, Action<SchedulePage, ScheduleQuickControls> body, Action? opened = null, Action? resumed = null) =>
        TestWindow.Sta((form, theme, art, tips) =>
        {
            var page = new SchedulePage(theme, art, tips, (_, _) => { }, _dir) { Dock = DockStyle.Fill };
            form.Controls.Add(page);
            var quick = new ScheduleQuickControls(theme, tips, page, () => true, opened ?? (() => { }), clock);
            page.Changed += quick.UpdateStatus;
            page.ResumeRequested += resumed ?? (() => { });
            return _ =>
            {
                using (quick)
                {
                    page.LoadSchedule(); // as at the Companion's start: the stored switch and pause
                    quick.UpdateStatus();
                    body(page, quick);
                }
            };
        });

    [Fact]
    public void The_sidebar_and_the_schedule_page_change_one_stored_schedule_and_show_the_same()
    {
        var store = new ScheduleStore(_dir);
        store.Save(Week);
        var opened = 0;
        var resumed = 0;
        WithPage(new MovingClock(Now), (page, quick) =>
        {
            Assert.Equal("Schedule Off", quick.Status.Text);
            Assert.Equal(Theme.Faint, quick.Pill.DotColor);

            // Sidebar → page and file.
            quick.Run(new(ScheduleCommandKind.TurnLiveOn));
            Assert.True(page.Schedule.Live);
            Assert.True(store.Load().Schedule.Live);
            Assert.Equal((ScheduleState.Live, Theme.Green), (quick.Status.State, quick.Pill.DotColor));

            quick.Run(new(ScheduleCommandKind.PauseHour));
            Assert.Equal(Now.AddHours(1), page.Schedule.PausedUntil);
            Assert.Equal(Now.AddHours(1), store.Load().Schedule.PausedUntil);
            Assert.Equal(("Paused until 17:40", Theme.Amber), (quick.Status.Text, quick.Pill.DotColor));

            quick.Run(new(ScheduleCommandKind.Resume));
            Assert.Null(store.Load().Schedule.PausedUntil);
            Assert.Equal(1, resumed); // the Companion ends a manual mood, as the page's Resume does

            // Page → sidebar.
            page.Pause(WeekSchedule.UntilResumed);
            Assert.Equal("Paused until resumed", quick.Status.Text);
            page.SetLive(false);
            Assert.Equal("Schedule Off", quick.Status.Text);
            page.SetLive(true); // the switch keeps the pause (P6): still paused, and shown so
            Assert.Equal("Paused until resumed", quick.Status.Text);
            Assert.Equal(["Resume", "Turn Off", "Open Schedule"], quick.Commands.Select(ScheduleStatus.Label));
            quick.Run(new(ScheduleCommandKind.Resume));
            Assert.Equal("Schedule Live", quick.Status.Text);

            quick.Run(new(ScheduleCommandKind.Away, 5));
            Assert.Equal(ScheduleStatus.LocalMidnight(Now, Amsterdam, 5), store.Load().Schedule.PausedUntil);
            quick.Run(new(ScheduleCommandKind.TurnOff));
            Assert.False(store.Load().Schedule.Live);
            quick.Run(new(ScheduleCommandKind.OpenSchedule));
            Assert.Equal(1, opened);
            Assert.Equal(Week[DayOfWeek.Friday].ToArray(), store.Load().Schedule[DayOfWeek.Friday].ToArray()); // only the switch and the pause changed
        }, () => opened++, () => resumed++);
    }

    [Fact]
    public void A_pause_shows_as_live_once_it_ran_out_and_the_state_survives_a_restart()
    {
        new ScheduleStore(_dir).Save(Week.WithLive(true).WithPause(Now.AddHours(1)));
        var clock = new MovingClock(Now);
        WithPage(clock, (page, quick) =>
        {
            Assert.Equal("Paused until 17:40", quick.Status.Text);

            clock.Now = Now.AddHours(1); // the pause ends (the page's clock tick or a reconnect updates the pill)
            quick.UpdateStatus();
            Assert.Equal((ScheduleState.Live, "Schedule Live"), (quick.Status.State, quick.Status.Text));
            Assert.Equal(Now.AddHours(1), page.Schedule.PausedUntil); // nothing is written when a pause runs out
        });
    }

    [Fact]
    public void A_demo_installation_stays_off()
    {
        var demo = FindUp(Path.Combine("docs", "examples", "schedule.demo.json"));
        File.Copy(demo, Path.Combine(_dir, "schedule.json"));
        WithPage(new MovingClock(Now), (page, quick) =>
        {
            Assert.False(page.Schedule.IsEmpty);
            Assert.Equal(("Schedule Off", ScheduleState.Off), (quick.Status.Text, quick.Status.State));
        });

        static string FindUp(string relative)
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
            {
                if (File.Exists(Path.Combine(dir.FullName, relative)))
                {
                    return Path.Combine(dir.FullName, relative);
                }
            }
            throw new FileNotFoundException(relative);
        }
    }
}
