using System.Globalization;

namespace DeskBuddy.Companion.Schedule;

/// <summary>What the schedule does right now, as the Live switch and the pause make it (P6).</summary>
public enum ScheduleState
{
    /// <summary>Off: asks Buddy for nothing.</summary>
    Off,
    /// <summary>Live: scheduled moods and actions are applied.</summary>
    Live,
    /// <summary>Live but paused until a time (that has not passed).</summary>
    Paused,
    /// <summary>Live but paused until resumed.</summary>
    PausedUntilResumed,
}

/// <summary>The effective state in words: a short <see cref="Text"/> for the sidebar pill and a full <see cref="Detail"/>.</summary>
public sealed record ScheduleStatusInfo(ScheduleState State, string Text, string Detail);

/// <summary>One of the quick controls: what it does and, for Away, for how many days (today counted).</summary>
public sealed record ScheduleCommand(ScheduleCommandKind Kind, int Days = 0);

public enum ScheduleCommandKind { TurnLiveOn, PauseHour, PauseToday, Away, PauseUntilResumed, Resume, TurnOff, OpenSchedule }

/// <summary>
/// The schedule's effective state and the quick controls on it, from the stored schedule alone (the P6 switch and
/// pause; nothing of its own). A pause that has run out is Live again; Off wins over a pause.
/// </summary>
public static class ScheduleStatus
{
    private static readonly CultureInfo En = CultureInfo.InvariantCulture;

    /// <summary>The days the Away presets offer (today counts as the first), as the Schedule page's Pause menu.</summary>
    public static readonly IReadOnlyList<int> AwayDays = [2, 3, 5, 7, 14];

    public static ScheduleState StateOf(WeekSchedule schedule, DateTimeOffset now) =>
        !schedule.Live ? ScheduleState.Off
        : !schedule.IsPaused(now) ? ScheduleState.Live
        : schedule.PausedUntil == WeekSchedule.UntilResumed ? ScheduleState.PausedUntilResumed
        : ScheduleState.Paused;

    /// <param name="connected">DeskBuddy is connected: without it a live schedule's actions are skipped (offline).</param>
    public static ScheduleStatusInfo Of(WeekSchedule schedule, DateTimeOffset now, TimeZoneInfo zone, bool connected)
    {
        var state = StateOf(schedule, now);
        switch (state)
        {
        case ScheduleState.Off:
            return new(state, "Schedule Off", "Off: the schedule asks Buddy for nothing (no scheduled moods or actions).");
        case ScheduleState.PausedUntilResumed:
            return new(state, "Paused until resumed", "Paused until you resume: no scheduled moods or actions; nothing missed is done afterwards.");
        case ScheduleState.Paused:
        {
            var until = TimeZoneInfo.ConvertTime(schedule.PausedUntil!.Value, zone);
            return new(state, $"Paused until {Short(until, TimeZoneInfo.ConvertTime(now, zone))}",
                       $"Paused until {until.ToString("dddd d MMMM HH:mm", En)}: no scheduled moods or actions; nothing missed is done afterwards.");
        }
        default:
            var detail = schedule.IsEmpty ? "Live, but the week has no blocks: nothing is scheduled."
                : "Live: Buddy follows the schedule's moods and actions.";
            if (!connected)
            {
                detail += " DeskBuddy is not connected: scheduled actions are skipped until it is.";
            }
            return new(state, "Schedule Live", detail);
        }
    }

    // "17:30" today, "midnight" at the end of today, else "Mon 20 Oct 00:00" (the time always: a date alone would read
    // as "through that day").
    private static string Short(DateTimeOffset until, DateTimeOffset now) =>
        until.Date == now.Date ? until.ToString("HH:mm", En)
        : until.TimeOfDay == TimeSpan.Zero && until.Date == now.Date.AddDays(1) ? "midnight"
        : until.ToString("ddd d MMM HH:mm", En);

    /// <summary>The quick controls that apply in a state (as the Schedule page offers them: pausing only while Live).</summary>
    public static IReadOnlyList<ScheduleCommand> Commands(ScheduleState state) => state switch
    {
        ScheduleState.Off => [new(ScheduleCommandKind.TurnLiveOn), new(ScheduleCommandKind.OpenSchedule)],
        ScheduleState.Live =>
        [
            new(ScheduleCommandKind.PauseHour), new(ScheduleCommandKind.PauseToday),
            .. AwayDays.Select(d => new ScheduleCommand(ScheduleCommandKind.Away, d)),
            new(ScheduleCommandKind.PauseUntilResumed), new(ScheduleCommandKind.TurnOff), new(ScheduleCommandKind.OpenSchedule),
        ],
        _ => [new(ScheduleCommandKind.Resume), new(ScheduleCommandKind.TurnOff), new(ScheduleCommandKind.OpenSchedule)],
    };

    public static string Label(ScheduleCommand c) => c.Kind switch
    {
        ScheduleCommandKind.TurnLiveOn => "Turn Live on",
        ScheduleCommandKind.PauseHour => "Pause for 1 hour",
        ScheduleCommandKind.PauseToday => "Pause for the rest of today",
        ScheduleCommandKind.Away => $"{c.Days} days (today and the next {c.Days - 1})",
        ScheduleCommandKind.PauseUntilResumed => "Pause until I resume",
        ScheduleCommandKind.Resume => "Resume",
        ScheduleCommandKind.TurnOff => "Turn Off",
        _ => "Open Schedule",
    };

    /// <summary>When a pause command ends (null: not a pause).</summary>
    public static DateTimeOffset? PauseUntil(ScheduleCommand c, DateTimeOffset now, TimeZoneInfo zone) => c.Kind switch
    {
        ScheduleCommandKind.PauseHour => now.AddHours(1),
        ScheduleCommandKind.PauseToday => LocalMidnight(now, zone, 1),
        ScheduleCommandKind.Away => LocalMidnight(now, zone, c.Days),
        ScheduleCommandKind.PauseUntilResumed => WeekSchedule.UntilResumed,
        _ => null,
    };

    /// <summary>The start of the local day <paramref name="days"/> after today's ("rest of today" is 1).</summary>
    public static DateTimeOffset LocalMidnight(DateTimeOffset now, TimeZoneInfo zone, int days)
    {
        var local = TimeZoneInfo.ConvertTime(now, zone).Date.AddDays(days);
        return new DateTimeOffset(local, zone.GetUtcOffset(local));
    }
}
