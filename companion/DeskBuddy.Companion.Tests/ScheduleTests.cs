using DeskBuddy.Companion.Schedule;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The schedule's rules: which block applies (layers), when the mood changes, and when a manual mood is left alone.
public class ScheduleTests
{
    private static readonly DateTime Wed = new(2026, 10, 14); // a Wednesday

    private static ScheduleBlock B(string start, string end, string? mood = null, string? plans = null, bool enabled = true) =>
        new() { Start = DayTime.Parse(start)!.Value, End = DayTime.Parse(end)!.Value, Mood = mood, Plans = plans, Enabled = enabled };

    private static WeekSchedule Day(DayOfWeek day, params ScheduleBlock[] blocks) => WeekSchedule.Empty.WithDay(day, blocks);

    private static DateTime At(string time, DateTime? date = null) => (date ?? Wed).AddMinutes(DayTime.Parse(time)!.Value);

    [Fact]
    public void Day_times_parse_strictly_and_round_trip()
    {
        Assert.Equal(510, DayTime.Parse("08:30"));
        Assert.Equal(510, DayTime.Parse("8:30"));
        Assert.Equal(510, DayTime.Parse("0830"));
        Assert.Equal(1440, DayTime.Parse("24:00"));
        Assert.Null(DayTime.Parse("24:01"));
        Assert.Null(DayTime.Parse("12:60"));
        Assert.Null(DayTime.Parse("12:5"));
        Assert.Null(DayTime.Parse(":30"));
        Assert.Null(DayTime.Parse("-1:00"));
        Assert.Equal("24:00", DayTime.Format(1440));
        Assert.Equal("1 h 5 min", DayTime.Duration(65));
    }

    [Fact]
    public void A_block_covers_its_start_up_to_but_not_its_end_and_24_00_covers_the_last_minute()
    {
        var s = Day(DayOfWeek.Wednesday, B("08:30", "12:00", "working"), B("22:00", "24:00", "sleepy"));
        Assert.Null(SchedulePlanner.Resolve(s, At("08:29")).Mood);
        Assert.Equal("working", SchedulePlanner.Resolve(s, At("08:30")).Mood);
        Assert.Equal("working", SchedulePlanner.Resolve(s, At("11:59")).Mood);
        Assert.Null(SchedulePlanner.Resolve(s, At("12:00")).Mood);
        Assert.Equal("sleepy", SchedulePlanner.Resolve(s, At("23:59")).Mood);
        Assert.Null(SchedulePlanner.Resolve(s, At("00:00", Wed.AddDays(1))).Mood); // Thursday has no blocks
    }

    [Fact]
    public void Layers_the_latest_start_is_on_top_and_the_block_below_resumes()
    {
        var s = Day(DayOfWeek.Wednesday, B("08:30", "12:00", "working"), B("10:00", "10:12", "peukie"));
        Assert.Equal("working", SchedulePlanner.Resolve(s, At("09:59")).Mood);
        Assert.Equal("peukie", SchedulePlanner.Resolve(s, At("10:00")).Mood);
        Assert.Equal("working", SchedulePlanner.Resolve(s, At("10:12")).Mood);
    }

    [Fact]
    public void Layers_equal_starts_the_shorter_wins_then_the_later_in_the_list()
    {
        var shorter = Day(DayOfWeek.Wednesday, B("09:00", "10:00", "happy"), B("09:00", "12:00", "working"));
        Assert.Equal("happy", SchedulePlanner.Resolve(shorter, At("09:30")).Mood);
        var tie = Day(DayOfWeek.Wednesday, B("09:00", "10:00", "happy"), B("09:00", "10:00", "angry"));
        Assert.Equal("angry", SchedulePlanner.Resolve(tie, At("09:30")).Mood);
    }

    [Fact]
    public void Blocks_without_a_mood_or_disabled_do_not_hide_the_mood_below()
    {
        var s = Day(DayOfWeek.Wednesday, B("08:30", "17:00", "working"), B("12:00", "12:45", plans: "lunch"),
                    B("15:00", "16:00", "happy", enabled: false));
        var lunch = SchedulePlanner.Resolve(s, At("12:10"));
        Assert.Equal("working", lunch.Mood);
        Assert.Equal("lunch", lunch.PlanBlock?.Plans);
        Assert.Equal("working", SchedulePlanner.Resolve(s, At("15:30")).Mood);
        Assert.Equal(2, lunch.Covering.Length);
    }

    [Fact]
    public void A_switched_off_schedule_asks_for_nothing()
    {
        var s = Day(DayOfWeek.Wednesday, B("08:30", "12:00", "working")).WithEnabled(false);
        Assert.Null(SchedulePlanner.Resolve(s, At("09:00")).Mood);
        Assert.Null(SchedulePlanner.NextMoodChange(s, At("07:00")));
    }

    [Fact]
    public void The_next_change_skips_boundaries_where_the_mood_stays_the_same()
    {
        var s = Day(DayOfWeek.Wednesday, B("08:30", "12:00", "working"), B("12:00", "17:00", "working"),
                    B("10:00", "10:12", "working"), B("17:00", "18:00", "relaxed"));
        Assert.Equal(At("08:30"), SchedulePlanner.NextMoodChange(s, At("07:00")));
        Assert.Equal(At("17:00"), SchedulePlanner.NextMoodChange(s, At("09:00")));
        Assert.Equal(At("18:00"), SchedulePlanner.NextMoodChange(s, At("17:00"))); // to "no mood"
        Assert.Equal(At("08:30", Wed.AddDays(7)), SchedulePlanner.NextMoodChange(s, At("18:00"))); // next Wednesday
    }

    [Fact]
    public void Depth_counts_the_enabled_blocks_a_block_lies_on()
    {
        var day = Day(DayOfWeek.Wednesday, B("10:00", "10:12", "peukie"), B("08:30", "12:00", "working"), B("13:00", "14:00", "happy"))[DayOfWeek.Wednesday];
        var layers = SchedulePlanner.Layers(day);
        Assert.Equal(["working", "peukie", "happy"], layers.Select(l => l.Block.Mood));
        Assert.Equal([0, 1, 0], layers.Select(l => l.Depth));
    }

    // ---- MoodDirector: transitions and overrides ----

    private static readonly WeekSchedule Workday = Day(DayOfWeek.Wednesday,
        B("08:30", "12:00", "working"), B("10:00", "10:12", "peukie"), B("12:00", "17:00", "working"));

    [Fact]
    public void Sends_a_scheduled_mood_once_at_a_transition_and_not_at_an_unchanged_boundary()
    {
        var d = new MoodDirector();
        Assert.Null(d.Tick(Workday, At("08:00"), "idle"));
        Assert.Equal("working", d.Tick(Workday, At("08:30"), "idle"));
        Assert.Null(d.Tick(Workday, At("08:31"), "working"));
        Assert.Equal("peukie", d.Tick(Workday, At("10:00"), "working"));
        Assert.Equal("working", d.Tick(Workday, At("10:12"), "peukie"));
        Assert.Null(d.Tick(Workday, At("12:00"), "working")); // Working → Working: no transition
    }

    [Fact]
    public void A_transition_to_a_mood_the_device_already_has_sends_nothing()
    {
        var d = new MoodDirector();
        d.Tick(Workday, At("08:00"), "idle");
        Assert.Null(d.Tick(Workday, At("08:30"), "working"));
        Assert.Equal("working", d.Scheduled);
    }

    [Fact]
    public void When_the_last_block_ends_Buddy_keeps_his_mood_until_the_next_transition()
    {
        var d = new MoodDirector();
        d.Tick(Workday, At("16:00"), "working");
        Assert.Null(d.Tick(Workday, At("17:00"), "working")); // no forced idle
        Assert.Null(d.Scheduled);
        Assert.Null(d.Desired);
        Assert.Null(d.Tick(Workday, At("23:00"), "working"));
        Assert.Null(d.Tick(Workday, At("08:29", Wed.AddDays(7)), "working"));
        Assert.Equal("working", d.Tick(Workday, At("08:30", Wed.AddDays(7)), "sleepy")); // next Wednesday
    }

    [Fact]
    public void A_manual_mood_holds_over_unchanged_boundaries_and_ends_at_the_next_actual_transition()
    {
        var d = new MoodDirector();
        d.Tick(Workday, At("09:00"), "working");
        d.Manual("happy");
        Assert.Equal("happy", d.Override);
        Assert.Equal("happy", d.Desired);
        Assert.Equal("peukie", d.Tick(Workday, At("10:00"), "happy")); // working → peukie: an actual transition
        Assert.Null(d.Override);

        d.Manual("relaxed");
        Assert.Null(d.Tick(Workday, At("10:05"), "relaxed"));
        Assert.Equal("working", d.Tick(Workday, At("10:12"), "relaxed"));

        d.Manual("happy");
        Assert.Null(d.Tick(Workday, At("12:00"), "happy")); // working → working (next block): override stays
        Assert.Equal("happy", d.Override);
        Assert.Null(d.Tick(Workday, At("17:00"), "happy")); // working → nothing: override stays, nothing sent
        Assert.Equal("happy", d.Override);
        Assert.Equal("working", d.Tick(Workday, At("08:30", Wed.AddDays(7)), "happy"));
        Assert.Null(d.Override);
    }

    [Fact]
    public void Choosing_the_scheduled_mood_by_hand_is_not_an_override()
    {
        var d = new MoodDirector();
        d.Tick(Workday, At("09:00"), "working");
        d.Manual("working");
        Assert.Null(d.Override);
    }

    [Fact]
    public void Resume_ends_the_override_and_sends_the_scheduled_mood()
    {
        var d = new MoodDirector();
        d.Tick(Workday, At("09:00"), "working");
        d.Manual("happy");
        Assert.Equal("working", d.Resume("happy"));
        Assert.Null(d.Override);

        d.Tick(Workday, At("18:00"), "working");
        d.Manual("happy");
        Assert.Null(d.Resume("happy")); // nothing scheduled: Buddy stays as he is
        Assert.Null(d.Override);
    }

    [Fact]
    public void On_connect_a_default_device_gets_the_schedule_and_another_mood_counts_as_the_users_choice()
    {
        var fresh = new MoodDirector();
        fresh.Tick(Workday, At("09:00"), null);
        Assert.Equal("working", fresh.Connected("idle"));

        var picked = new MoodDirector();
        picked.Tick(Workday, At("09:00"), null);
        Assert.Null(picked.Connected("happy"));
        Assert.Equal("happy", picked.Override);
        Assert.Equal("peukie", picked.Tick(Workday, At("10:00"), "happy"));

        var nothing = new MoodDirector();
        nothing.Tick(Workday, At("18:00"), null);
        Assert.Null(nothing.Connected("idle"));
    }

    [Fact]
    public void After_a_device_restart_the_override_or_the_scheduled_mood_is_reapplied()
    {
        var d = new MoodDirector();
        d.Tick(Workday, At("09:00"), "working");
        Assert.Equal("working", d.Booted());
        d.Manual("happy");
        Assert.Equal("happy", d.Booted());
        Assert.Equal("happy", d.Connected("idle"));
        Assert.Equal("happy", d.DeviceSettled("working")); // e.g. a smoke break ended into another mood
    }

    [Fact]
    public void A_transition_while_disconnected_is_applied_on_connect()
    {
        var d = new MoodDirector();
        d.Tick(Workday, At("08:00"), null);
        Assert.Equal("working", d.Tick(Workday, At("08:30"), null)); // nothing to send it to: the caller drops it
        Assert.Equal("working", d.Connected("idle"));
    }

    [Fact]
    public void When_the_clock_goes_back_no_transition_happens_twice()
    {
        var night = Day(DayOfWeek.Sunday, B("02:30", "02:45", "sleepy"));
        var sunday = new DateTime(2026, 10, 25); // the end of daylight saving time in Europe: 03:00 → 02:00
        var d = new MoodDirector();
        d.Tick(night, At("02:00", sunday), "idle");
        Assert.Equal("sleepy", d.Tick(night, At("02:30", sunday), "idle"));
        d.Tick(night, At("02:59", sunday), "sleepy");
        Assert.Null(d.Tick(night, At("02:30", sunday), "sleepy")); // the repeated hour
        Assert.Null(d.Tick(night, At("02:59", sunday), "sleepy"));
    }

    [Fact]
    public void A_block_starting_in_the_skipped_hour_starts_when_the_clock_jumps()
    {
        var night = Day(DayOfWeek.Sunday, B("02:30", "04:00", "sleepy"));
        var sunday = new DateTime(2026, 3, 29); // the start of daylight saving time in Europe: 02:00 → 03:00
        var d = new MoodDirector();
        d.Tick(night, At("01:59", sunday), "idle");
        Assert.Equal("sleepy", d.Tick(night, At("03:00", sunday), "idle"));
    }

    [Fact]
    public void Switching_the_schedule_off_sends_nothing_and_on_again_applies_it()
    {
        var d = new MoodDirector();
        d.Tick(Workday, At("09:00"), "working");
        Assert.Null(d.Tick(Workday.WithEnabled(false), At("09:01"), "working"));
        Assert.Equal("working", d.Tick(Workday, At("09:02"), "happy"));
    }

    // ---- edits ----

    [Fact]
    public void Duplicate_goes_after_the_original_or_on_top_when_the_day_is_full()
    {
        var b = B("09:00", "10:30", "working");
        var after = ScheduleEdits.Duplicate(b);
        Assert.NotEqual(b.Id, after.Id);
        Assert.Equal((630, 720), (after.Start, after.End));
        var late = ScheduleEdits.Duplicate(B("23:00", "24:00"));
        Assert.Equal((1380, 1440), (late.Start, late.End));
    }

    [Fact]
    public void A_block_past_midnight_becomes_two_blocks()
    {
        var (today, tomorrow) = ScheduleEdits.SplitAtMidnight(B("00:00", "01:00", "sleepy"), DayTime.Parse("22:00")!.Value, DayTime.Parse("02:00")!.Value);
        Assert.Equal((1320, 1440, "sleepy"), (today.Start, today.End, today.Mood));
        Assert.Equal((0, 120, "sleepy"), (tomorrow.Start, tomorrow.End, tomorrow.Mood));
        Assert.NotEqual(today.Id, tomorrow.Id);
        Assert.Equal(DayOfWeek.Monday, ScheduleEdits.Next(DayOfWeek.Sunday));
    }

    [Fact]
    public void Copying_a_day_replaces_the_targets_with_new_ids_and_leaves_the_source()
    {
        var s = Day(DayOfWeek.Monday, B("08:30", "12:00", "working")).WithDay(DayOfWeek.Tuesday, [B("06:00", "07:00", "sleepy")]);
        var copied = s.CopyDay(DayOfWeek.Monday, [DayOfWeek.Monday, DayOfWeek.Tuesday, DayOfWeek.Friday]);
        Assert.Single(copied[DayOfWeek.Monday]);
        Assert.Equal("working", Assert.Single(copied[DayOfWeek.Tuesday]).Mood);
        Assert.NotEqual(copied[DayOfWeek.Monday][0].Id, copied[DayOfWeek.Friday][0].Id);
    }

    [Fact]
    public void Validation_refuses_empty_reversed_and_overlong_blocks()
    {
        Assert.Null(WeekSchedule.Problem(B("08:00", "24:00")));
        Assert.NotNull(WeekSchedule.Problem(B("08:00", "08:00")));
        Assert.NotNull(WeekSchedule.Problem(new ScheduleBlock { Start = 600, End = 500 }));
        Assert.NotNull(WeekSchedule.Problem(new ScheduleBlock { Start = 1440, End = 1440 }));
        Assert.NotNull(WeekSchedule.Problem(B("08:00", "09:00", "Not A Mood")));
    }

    // ---- storage ----

    [Fact]
    public void The_schedule_survives_a_save_and_load_and_keeps_the_previous_version()
    {
        var dir = Directory.CreateTempSubdirectory("deskbuddy-schedule").FullName;
        try
        {
            var store = new ScheduleStore(dir);
            Assert.True(store.Load().Schedule.IsEmpty);
            var first = Day(DayOfWeek.Monday, B("08:30", "24:00", "working", "work") with { Name = "Werk", Reminder = true });
            store.Save(first);
            store.Save(first.WithEnabled(false).Put(DayOfWeek.Sunday, B("10:00", "11:00", enabled: false)));

            var (loaded, warning) = store.Load();
            Assert.Null(warning);
            Assert.False(loaded.Enabled);
            var block = Assert.Single(loaded[DayOfWeek.Monday]);
            Assert.Equal(first[DayOfWeek.Monday][0], block);
            Assert.False(Assert.Single(loaded[DayOfWeek.Sunday]).Enabled);
            Assert.True(File.Exists(store.BackupPath));
            Assert.True(ScheduleStore.FromJson(File.ReadAllText(store.BackupPath)).Schedule!.Enabled);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    [Fact]
    public void A_damaged_file_is_set_aside_and_the_previous_version_used()
    {
        var dir = Directory.CreateTempSubdirectory("deskbuddy-schedule").FullName;
        try
        {
            var store = new ScheduleStore(dir);
            store.Save(Day(DayOfWeek.Monday, B("08:30", "12:00", "working")));
            store.Save(Day(DayOfWeek.Monday, B("09:00", "12:00", "happy")));
            File.WriteAllText(store.Path, "{ this is not json");

            var (loaded, warning) = store.Load();
            Assert.Equal("working", Assert.Single(loaded[DayOfWeek.Monday]).Mood);
            Assert.Contains("previous version", warning);
            Assert.Single(Directory.GetFiles(dir, "schedule.json.damaged-*"));
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    [Fact]
    public void Invalid_blocks_are_left_out_and_reported()
    {
        var json = """
            {"version": 1, "enabled": true, "days": {"wed": [
              {"id": "6f1c2c1e-0000-4000-8000-000000000001", "start": "08:30", "end": "12:00", "mood": "working", "future": 1},
              {"id": "6f1c2c1e-0000-4000-8000-000000000002", "start": "13:00", "end": "12:00"},
              {"id": "not-a-guid", "start": "13:00", "end": "14:00"}]}}
            """;
        var (schedule, problem) = ScheduleStore.FromJson(json);
        Assert.Equal("working", Assert.Single(schedule![DayOfWeek.Wednesday]).Mood);
        Assert.Equal("2 invalid block(s) left out", problem);
    }
}
