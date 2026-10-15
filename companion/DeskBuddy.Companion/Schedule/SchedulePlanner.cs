using System.Collections.Immutable;

namespace DeskBuddy.Companion.Schedule;

/// <summary>What the schedule says at one moment.</summary>
/// <param name="MoodBlock">The block whose mood applies; null: the schedule asks for no mood.</param>
/// <param name="PlanBlock">The block whose planned activity applies; null: nothing planned.</param>
/// <param name="Covering">Every enabled block that covers the moment, lowest first (the top one last).</param>
public sealed record ScheduleSnapshot(ScheduleBlock? MoodBlock, ScheduleBlock? PlanBlock, ImmutableArray<ScheduleBlock> Covering)
{
    public static readonly ScheduleSnapshot None = new(null, null, []);

    /// <summary>The mood the schedule asks for; null: none (Buddy keeps whatever mood he has).</summary>
    public string? Mood => MoodBlock?.Mood;
}

/// <summary>
/// Which block applies, in local wall-clock time. Blocks may overlap (layers): of the enabled blocks covering the
/// moment, the one that started last is on top; equal starts: the shorter one; still equal: the one later in the
/// day's list. The mood comes from the top block that has a mood (a block without one does not hide the block
/// below), the planned activity likewise. When the top block ends, the one below applies again.
/// Pure functions: no clock, no I/O.
/// </summary>
public static class SchedulePlanner
{
    public static ScheduleSnapshot Resolve(WeekSchedule schedule, DateTime local)
    {
        if (!schedule.Enabled)
        {
            return ScheduleSnapshot.None;
        }
        var minute = local.Hour * 60 + local.Minute;
        var covering = Layers(schedule[local.DayOfWeek]).Select(l => l.Block).Where(b => b.Enabled && b.Covers(minute)).ToImmutableArray();
        return new ScheduleSnapshot(covering.LastOrDefault(b => b.Mood is not null), covering.LastOrDefault(b => b.Plans is not null), covering);
    }

    /// <summary>
    /// The next moment (wall clock, on a whole minute) at which the scheduled mood changes, including to "no mood";
    /// block boundaries where it stays the same are skipped. Null if it does not change within a week.
    /// </summary>
    public static DateTime? NextMoodChange(WeekSchedule schedule, DateTime local)
    {
        if (!schedule.Enabled)
        {
            return null;
        }
        var current = Resolve(schedule, local).Mood;
        var today = local.Date;
        var candidates = new SortedSet<DateTime>();
        for (var d = 0; d <= 7; d++)
        {
            var day = today.AddDays(d);
            foreach (var b in schedule[day.DayOfWeek].Where(b => b.Enabled && b.Mood is not null))
            {
                candidates.Add(day.AddMinutes(b.Start));
                candidates.Add(day.AddMinutes(b.End)); // 24:00 is the next day's 00:00
            }
        }
        foreach (var at in candidates.Where(t => t > local))
        {
            if (Resolve(schedule, at).Mood != current)
            {
                return at;
            }
        }
        return null;
    }

    /// <summary>
    /// The day's blocks from lowest to highest layer, each with its depth: how many enabled blocks it lies on top of.
    /// Disabled blocks get the depth they would have.
    /// </summary>
    public static IReadOnlyList<(ScheduleBlock Block, int Depth)> Layers(ImmutableArray<ScheduleBlock> day)
    {
        var order = day.Select((b, i) => (Block: b, Index: i))
                       .OrderBy(x => x.Block.Start)
                       .ThenByDescending(x => x.Block.Duration)
                       .ThenBy(x => x.Index)
                       .Select(x => x.Block)
                       .ToList();
        var result = new List<(ScheduleBlock, int)>(order.Count);
        for (var i = 0; i < order.Count; i++)
        {
            var depth = 0;
            for (var j = 0; j < i; j++)
            {
                if (order[j].Enabled && order[j].Overlaps(order[i]))
                {
                    depth++;
                }
            }
            result.Add((order[i], depth));
        }
        return result;
    }
}

/// <summary>Edits the editor offers, as pure functions.</summary>
public static class ScheduleEdits
{
    public const int DefaultMinutes = 60;

    /// <summary>A new block of an hour at <paramref name="start"/> (cut at 24:00; at least a minute).</summary>
    public static ScheduleBlock NewAt(int start)
    {
        start = Math.Clamp(start, 0, DayTime.Minutes - 1);
        return new ScheduleBlock { Name = "New block", Start = start, End = Math.Min(start + DefaultMinutes, DayTime.Minutes) };
    }

    /// <summary>A copy right after the original if it fits before 24:00, else at the same time (on top of it).</summary>
    public static ScheduleBlock Duplicate(ScheduleBlock block)
    {
        var copy = block with { Id = Guid.NewGuid() };
        return block.End + block.Duration <= DayTime.Minutes ? copy with { Start = block.End, End = block.End + block.Duration } : copy;
    }

    /// <summary>
    /// A block that runs past midnight, as two blocks: <paramref name="start"/>–24:00 on its day and 00:00–
    /// <paramref name="endNextDay"/> on the next day, both like <paramref name="template"/>.
    /// </summary>
    public static (ScheduleBlock Today, ScheduleBlock Tomorrow) SplitAtMidnight(ScheduleBlock template, int start, int endNextDay) =>
        (template with { Start = start, End = DayTime.Minutes },
         template with { Id = Guid.NewGuid(), Start = 0, End = endNextDay });

    public static DayOfWeek Next(DayOfWeek day) => (DayOfWeek)(((int)day + 1) % 7);
}
