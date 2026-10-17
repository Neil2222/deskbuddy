using System.Collections.Immutable;
using System.Globalization;

namespace DeskBuddy.Companion.Schedule;

/// <summary>
/// One block of a day: from <see cref="Start"/> to <see cref="End"/> (minutes since local midnight, end exclusive,
/// 24:00 allowed as the end). It may set Buddy's mood, name a planned activity (an intention, never a record that
/// it happened), start one of Buddy's actions at its start time, any of these, or none. Blocks do not cross midnight:
/// the editor splits such a block in two.
/// </summary>
public sealed record ScheduleBlock
{
    public Guid Id { get; init; } = Guid.NewGuid();
    public string Name { get; init; } = "";
    public int Start { get; init; }
    public int End { get; init; }
    /// <summary>A firmware mood name (hello moods=); null: this block does not set a mood.</summary>
    public string? Mood { get; init; }
    /// <summary>An activity type id (<see cref="ActivityTypes"/>): what you plan to do; null: nothing planned.</summary>
    public string? Plans { get; init; }
    /// <summary>One of <see cref="ScheduledActions"/>, started once at the block's start time; null: none.</summary>
    public string? Action { get; init; }
    /// <summary>Stored for the reminders step; not used yet.</summary>
    public bool Reminder { get; init; }
    public bool Enabled { get; init; } = true;

    public int Duration => End - Start;
    public bool Covers(int minute) => Start <= minute && minute < End;
    public bool Overlaps(ScheduleBlock other) => Start < other.End && other.Start < End;
}

/// <summary>Times of day as the schedule shows and stores them: "08:30", "24:00".</summary>
public static class DayTime
{
    public const int Minutes = 24 * 60;

    public static string Format(int minute) => $"{minute / 60:00}:{minute % 60:00}";

    /// <summary>"8:30", "08:30", "830", "0830" and "24:00" (only as 24:00); null for anything else.</summary>
    public static int? Parse(string? text)
    {
        var t = (text ?? "").Trim();
        int hours, minutes;
        var colon = t.IndexOf(':');
        if (colon >= 0)
        {
            if (colon == 0 || t.Length - colon - 1 != 2 ||
                !int.TryParse(t[..colon], NumberStyles.None, CultureInfo.InvariantCulture, out hours) ||
                !int.TryParse(t[(colon + 1)..], NumberStyles.None, CultureInfo.InvariantCulture, out minutes))
            {
                return null;
            }
        }
        else if (t.Length is 3 or 4 && int.TryParse(t, NumberStyles.None, CultureInfo.InvariantCulture, out var hhmm))
        {
            hours = hhmm / 100;
            minutes = hhmm % 100;
        }
        else
        {
            return null;
        }
        if (minutes > 59 || hours > 24 || (hours == 24 && minutes != 0))
        {
            return null;
        }
        return hours * 60 + minutes;
    }

    public static string Duration(int minutes) =>
        minutes < 60 ? $"{minutes} min" : minutes % 60 == 0 ? $"{minutes / 60} h" : $"{minutes / 60} h {minutes % 60} min";
}

/// <summary>
/// Buddy's week: the blocks of each day (in list order; see <see cref="SchedulePlanner"/> for which one applies), whether
/// the Companion applies it to Buddy (<see cref="Live"/>, off until the user switches it on) and a pause. Immutable:
/// every edit returns a new schedule.
/// </summary>
public sealed class WeekSchedule
{
    /// <summary>Monday first, as the schedule shows the week.</summary>
    public static readonly IReadOnlyList<DayOfWeek> Week =
    [
        DayOfWeek.Monday, DayOfWeek.Tuesday, DayOfWeek.Wednesday, DayOfWeek.Thursday, DayOfWeek.Friday,
        DayOfWeek.Saturday, DayOfWeek.Sunday,
    ];

    /// <summary><see cref="PausedUntil"/> for "until I resume".</summary>
    public static readonly DateTimeOffset UntilResumed = DateTimeOffset.MaxValue;

    public static readonly WeekSchedule Empty = new(false, null, Enumerable.Repeat(ImmutableArray<ScheduleBlock>.Empty, 7).ToArray());

    private readonly ImmutableArray<ScheduleBlock>[] _days; // indexed by DayOfWeek

    /// <summary>The Companion applies the schedule to Buddy (moods and actions). Off by default.</summary>
    public bool Live { get; }

    /// <summary>Paused until then (<see cref="UntilResumed"/>: until resumed); null: not paused.</summary>
    public DateTimeOffset? PausedUntil { get; }

    private WeekSchedule(bool live, DateTimeOffset? pausedUntil, ImmutableArray<ScheduleBlock>[] days)
    {
        Live = live;
        PausedUntil = pausedUntil;
        _days = days;
    }

    public ImmutableArray<ScheduleBlock> this[DayOfWeek day] => _days[(int)day];

    public bool IsEmpty => _days.All(d => d.IsEmpty);

    public bool IsPaused(DateTimeOffset now) => PausedUntil is { } until && now < until;

    public WeekSchedule WithLive(bool live) => new(live, PausedUntil, _days);

    public WeekSchedule WithPause(DateTimeOffset? until) => new(Live, until, _days);

    public WeekSchedule WithDay(DayOfWeek day, IEnumerable<ScheduleBlock> blocks)
    {
        var days = (ImmutableArray<ScheduleBlock>[])_days.Clone();
        days[(int)day] = [.. blocks];
        return new WeekSchedule(Live, PausedUntil, days);
    }

    /// <summary>Replaces the block with the same id on that day, or adds it at the end.</summary>
    public WeekSchedule Put(DayOfWeek day, ScheduleBlock block)
    {
        var blocks = this[day];
        for (var i = 0; i < blocks.Length; i++)
        {
            if (blocks[i].Id == block.Id)
            {
                return WithDay(day, blocks.SetItem(i, block));
            }
        }
        return WithDay(day, blocks.Add(block));
    }

    public WeekSchedule Remove(DayOfWeek day, Guid id) => WithDay(day, this[day].Where(b => b.Id != id));

    /// <summary>The day's blocks on each target day, replacing what was there (copies get new ids).</summary>
    public WeekSchedule CopyDay(DayOfWeek from, IEnumerable<DayOfWeek> to)
    {
        var result = this;
        foreach (var day in to.Where(d => d != from))
        {
            result = result.WithDay(day, this[from].Select(b => b with { Id = Guid.NewGuid() }));
        }
        return result;
    }

    /// <summary>Why a block cannot be stored; null if it can.</summary>
    public static string? Problem(ScheduleBlock block)
    {
        if (block.Start < 0 || block.Start >= DayTime.Minutes)
        {
            return "The start must be between 00:00 and 23:59.";
        }
        if (block.End <= block.Start)
        {
            return "The end must be after the start (24:00 is midnight).";
        }
        if (block.End > DayTime.Minutes)
        {
            return "The end can be at most 24:00.";
        }
        if (block.Mood is { } mood && !IsName(mood) || block.Plans is { } plans && !IsName(plans))
        {
            return "Mood and activity names are lower-case letters and '_'.";
        }
        if (block.Action is { } action && !ScheduledActions.IsAllowed(action))
        {
            return $"'{action}' cannot be scheduled.";
        }
        return null;
    }

    // As the firmware's names: lower-case letters and '_', at most 15 (docs/protocol.md).
    private static bool IsName(string s) => s.Length is > 0 and <= 15 && s.All(c => c is >= 'a' and <= 'z' or '_');
}
