using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.Schedule;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// A mood the schedule set itself must not look like the user's choice after a reconnect, a wake from sleep or a
// companion restart; a genuine manual choice (touchscreen, mood tile) must still survive all of those. The companion
// is modelled the way CompanionService wires it: MoodDirector for the rules, ScheduleMoodTracker (saved in a
// settings.json of its own, in a temporary folder) for "is the device's mood the schedule's own".
public sealed class ScheduleMoodTests : IDisposable
{
    private const string Dev = "441bf68d1178";
    private const string BootA = "00000000000000a1", BootB = "00000000000000b2";
    private static readonly DateTime Wed = new(2026, 10, 14); // a Wednesday
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-schedmood-" + Guid.NewGuid().ToString("N"));

    // Tue/Wed: working 08:30-12:00, smoke break 10:00-10:12, working 12:00-17:00, sleepy 20:30 until the end of the day.
    private static readonly WeekSchedule Week = WeekSchedule.Empty
        .WithDay(DayOfWeek.Tuesday, [B("08:30", "17:00", "working"), B("20:30", "24:00", "sleepy")])
        .WithDay(DayOfWeek.Wednesday, [B("08:30", "12:00", "working"), B("10:00", "10:12", "peukie"), B("12:00", "17:00", "working")]);

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    private static ScheduleBlock B(string start, string end, string mood) =>
        new() { Start = DayTime.Parse(start)!.Value, End = DayTime.Parse(end)!.Value, Mood = mood };

    private static DateTime At(string time, DateTime? date = null) => (date ?? Wed).AddMinutes(DayTime.Parse(time)!.Value);

    /// <summary>One run of the companion (a restart is a new instance on the same folder).</summary>
    private sealed class Companion
    {
        public readonly MoodDirector Director = new();
        public readonly ScheduleMoodTracker Tracker;
        public string? Sent; // the last scheduled mood sent

        public Companion(string dir, DateTime now)
        {
            var store = new PdcStore(dir);
            Tracker = new ScheduleMoodTracker(store.LoadScheduleMood(), store.SaveScheduleMood);
            Director.Tick(Week, now, null); // where the schedule stands at start: not a transition
        }

        /// <summary>hello + status reported <paramref name="mood"/>; a returned mood is sent and accepted.</summary>
        public string? Connect(string boot, string mood) => Accept(boot, Director.Connected(mood, Tracker.SetBySchedule(Dev, boot, mood)));

        /// <summary>A tick while connected to <paramref name="boot"/> in <paramref name="mood"/>.</summary>
        public string? Tick(DateTime local, string boot, string mood) => Accept(boot, Director.Tick(Week, local, mood));

        /// <summary>evt mood src=touch (the Moods page on the device) or src=cmd from a mood tile.</summary>
        public void ChosenByUser(string mood)
        {
            Tracker.MoodChanged(bySchedule: false);
            Director.Manual(mood);
        }

        private string? Accept(string boot, string? mood)
        {
            Sent = mood;
            if (mood is not null)
            {
                Tracker.Applied(Dev, boot, mood); // ok, then evt mood src=cmd (the echo)
                Tracker.MoodChanged(bySchedule: true);
            }
            return mood;
        }
    }

    [Fact]
    public void Last_nights_scheduled_mood_gives_way_to_the_morning_after_a_companion_restart()
    {
        var tuesday = new Companion(_dir, At("20:00", Wed.AddDays(-1)));
        Assert.Null(tuesday.Connect(BootA, "working"));
        Assert.Equal("sleepy", tuesday.Tick(At("20:30", Wed.AddDays(-1)), BootA, "working"));
        // The laptop shuts down; the board stays powered (same boot) and sleepy all night.

        var wednesday = new Companion(_dir, At("09:00"));
        Assert.Equal("working", wednesday.Connect(BootA, "sleepy"));
        Assert.Null(wednesday.Director.Override);
    }

    [Fact]
    public void A_transition_missed_while_asleep_or_disconnected_is_applied_on_reconnect()
    {
        var c = new Companion(_dir, At("08:20"));
        Assert.Null(c.Connect(BootA, "idle"));
        Assert.Equal("working", c.Tick(At("08:30"), BootA, "idle"));
        // Sleep at 09:55; the 10:00 transition is evaluated with nothing to send it to (dropped by the caller).
        Assert.Equal("peukie", c.Director.Tick(Week, At("10:05"), null));
        Assert.Equal("peukie", c.Connect(BootA, "working")); // the board still shows the schedule's own "working"
    }

    [Fact]
    public void A_mood_chosen_on_the_touchscreen_survives_reconnects_and_companion_restarts()
    {
        var c = new Companion(_dir, At("09:00"));
        Assert.Equal("working", c.Connect(BootA, "idle"));
        c.ChosenByUser("happy"); // evt mood mood=happy src=touch
        Assert.Null(c.Connect(BootA, "happy")); // a reconnect (USB, wake from sleep)
        Assert.Equal("happy", c.Director.Override);

        var restarted = new Companion(_dir, At("09:30"));
        Assert.Null(restarted.Connect(BootA, "happy"));
        Assert.Equal("happy", restarted.Director.Override);
        Assert.Equal("peukie", restarted.Tick(At("10:00"), BootA, "happy")); // until the next actual transition
    }

    [Fact]
    public void A_mood_tile_choice_survives_a_reconnect_even_when_it_was_the_schedules_earlier_mood()
    {
        var c = new Companion(_dir, At("09:50"));
        Assert.Equal("working", c.Connect(BootA, "idle"));
        Assert.Equal("peukie", c.Tick(At("10:00"), BootA, "working"));
        c.ChosenByUser("working"); // the user wants to keep working during the break (src=cmd, not the schedule's echo)
        Assert.Null(c.Connect(BootA, "working"));
        Assert.Equal("working", c.Director.Override);
        Assert.Null(new Companion(_dir, At("10:05")).Connect(BootA, "working")); // and after a restart
    }

    [Fact]
    public void A_choice_made_while_the_companion_was_closed_is_kept()
    {
        var c = new Companion(_dir, At("09:00"));
        Assert.Equal("working", c.Connect(BootA, "idle"));
        // Closed; on the touchscreen the user picks relaxed (the companion never sees the event).
        var restarted = new Companion(_dir, At("09:30"));
        Assert.Null(restarted.Connect(BootA, "relaxed"));
        Assert.Equal("relaxed", restarted.Director.Override);
    }

    [Fact]
    public void A_board_restart_is_never_mistaken_for_the_marked_boot()
    {
        var c = new Companion(_dir, At("09:00"));
        Assert.Equal("working", c.Connect(BootA, "idle"));
        Assert.False(c.Tracker.SetBySchedule(Dev, BootB, "working"));
        Assert.Equal("working", c.Director.Booted()); // evt boot: the device is idle again
        Assert.Equal("working", c.Connect(BootB, "idle")); // or a reconnect after a power cycle
        Assert.False(c.Tracker.SetBySchedule("sim-0123456789ab", BootB, "working")); // another device
    }

    [Fact]
    public void An_override_in_force_is_reapplied_over_the_schedules_own_mood()
    {
        var c = new Companion(_dir, At("09:00"));
        Assert.Equal("working", c.Connect(BootA, "idle"));
        c.ChosenByUser("happy");
        Assert.Equal("happy", c.Director.Connected("idle", c.Tracker.SetBySchedule(Dev, BootB, "idle"))); // board restarted
    }

    [Fact]
    public void Nothing_scheduled_now_leaves_the_schedules_earlier_mood_alone()
    {
        var c = new Companion(_dir, At("16:50"));
        Assert.Null(c.Connect(BootA, "working"));
        Assert.Null(c.Tick(At("17:00"), BootA, "working")); // the last block ended: no forced idle
        Assert.Null(new Companion(_dir, At("18:00")).Connect(BootA, "working"));
    }

    [Fact]
    public void The_mark_is_kept_in_settings_json_next_to_the_other_settings()
    {
        Directory.CreateDirectory(_dir);
        File.WriteAllText(Path.Combine(_dir, "settings.json"), """{"pdcEnabled": false, "clockFace": "skeleton", "custom": 7}""");
        var store = new PdcStore(_dir);
        Assert.Null(store.LoadScheduleMood());

        store.SaveScheduleMood(new ScheduleMoodMark(Dev, BootA, "sleepy"));
        Assert.Equal(new ScheduleMoodMark(Dev, BootA, "sleepy"), new PdcStore(_dir).LoadScheduleMood());
        Assert.Equal("skeleton", store.LoadClockFace()?.Face);
        Assert.Contains("\"custom\": 7", File.ReadAllText(store.SettingsPath));

        store.SaveScheduleMood(null);
        Assert.Null(store.LoadScheduleMood());
        Assert.False(store.LoadSettings().Enabled);

        File.WriteAllText(store.SettingsPath, "{ damaged");
        Assert.Null(store.LoadScheduleMood()); // a damaged file: no mark, so every other mood counts as the user's
    }

    [Fact]
    public void The_tracker_writes_only_when_the_mark_changes()
    {
        var saves = new List<ScheduleMoodMark?>();
        var t = new ScheduleMoodTracker(null, saves.Add);
        t.MoodChanged(bySchedule: false); // nothing to clear
        t.Applied(Dev, BootA, "working");
        t.MoodChanged(bySchedule: true);
        t.Applied(Dev, BootA, "working");
        t.Applied(null, null, "working"); // firmware without dev/boot (no log cap): nothing marked
        Assert.Equal([new ScheduleMoodMark(Dev, BootA, "working"), null], saves);
    }
}
