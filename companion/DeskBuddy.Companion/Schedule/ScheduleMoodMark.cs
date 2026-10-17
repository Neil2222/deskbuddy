namespace DeskBuddy.Companion.Schedule;

/// <summary>The mood the schedule put Buddy in, on which board (<see cref="Dev"/>) and in which boot.</summary>
public sealed record ScheduleMoodMark(string Dev, string Boot, string Mood);

/// <summary>
/// Remembers whether Buddy's current mood is the schedule's own doing, across reconnects and companion restarts (the
/// mark is saved). Without it, a mood the schedule set (last night's sleepy) looks like the user's choice after a
/// reconnect, and the morning's scheduled mood is never applied.
///
/// The mark is set when the device accepts a scheduled mood, and dropped by any other mood change the companion sees
/// (the touchscreen, a mood tile, another tool, a smoke break, a timed mood), so a genuine manual choice is never
/// mistaken for the schedule's. A device restart gives a new boot, which no mark matches. What the companion cannot
/// see (a choice on the touchscreen while it is closed or disconnected) still counts as the user's unless it is the
/// very mood the schedule had set: the device does not record mood changes for a later replay.
/// </summary>
public sealed class ScheduleMoodTracker(ScheduleMoodMark? saved, Action<ScheduleMoodMark?> save)
{
    public ScheduleMoodMark? Mark { get; private set; } = saved;

    /// <summary>The device accepted the scheduled <paramref name="mood"/> (without dev and boot nothing is marked).</summary>
    public void Applied(string? dev, string? boot, string mood)
    {
        var mark = dev is null || boot is null ? null : new ScheduleMoodMark(dev, boot, mood);
        Set(mark);
    }

    /// <summary>evt mood: <paramref name="bySchedule"/> when it is the echo of the schedule's own mood.set.</summary>
    public void MoodChanged(bool bySchedule)
    {
        if (!bySchedule)
        {
            Set(null);
        }
    }

    /// <summary>The device's current mood is the one the schedule set in this boot, unchanged since.</summary>
    public bool SetBySchedule(string? dev, string? boot, string deviceMood) =>
        Mark is { } m && m.Dev == dev && m.Boot == boot && m.Mood == deviceMood;

    private void Set(ScheduleMoodMark? mark)
    {
        if (mark != Mark)
        {
            Mark = mark;
            save(mark);
        }
    }
}
