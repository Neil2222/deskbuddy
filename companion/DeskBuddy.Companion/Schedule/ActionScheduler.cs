namespace DeskBuddy.Companion.Schedule;

/// <summary>
/// One block's action on one local calendar day. <see cref="Id"/> (<c>&lt;block id&gt;/&lt;yyyy-MM-dd&gt;</c>) is the
/// same however often the day is evaluated, so it runs at most once per block per day, the repeated DST hour included.
/// </summary>
public sealed record Occurrence(string Id, Guid BlockId, string Action, DateTime PlannedLocal, DateTimeOffset PlannedUtc)
{
    public TimeSpan Grace => ScheduledActions.Grace(Action);
    public DateTimeOffset Deadline => PlannedUtc + Grace;

    public static string IdFor(Guid block, DateTime localDate) => $"{block}/{localDate:yyyy-MM-dd}";
}

public enum ActionStep
{
    /// <summary>Record the request (durably), then send <c>act</c>; report back with Answered or Busy.</summary>
    Send,
    /// <summary>Record that it was skipped (<see cref="ActionDecision.Reason"/>); it never runs.</summary>
    Skip,
}

public sealed record ActionDecision(Occurrence Occurrence, ActionStep Step, string? Reason = null);

/// <summary>
/// When a scheduled action runs, as a pure state machine (docs/schedule.md §4): the caller ticks it with the time and
/// whether the device can be reached, and carries out its decisions.
///
/// - Due at the block's start (local wall clock; a start in the skipped DST hour runs at the first existing minute; in
///   the repeated hour the first one counts), for a grace period: 3 minutes for questions, 1 for shows.
/// - Within it: paused → skipped:paused; not connected → wait; busy → try again every 10 s. Past it: skipped, with why
///   (offline, busy:&lt;mode&gt;, companion_off when it passed before the Companion started, asleep when the clock
///   jumped over it). Never run late, never queued.
/// - Once requested or skipped, an occurrence is decided and never acted on again, whatever its outcome: an
///   uncertain one (no reply, link lost) is not sent again.
/// - Off (not live): nothing is tracked or recorded; switched on during the day, what was planned before is left alone.
/// </summary>
/// <param name="runningSince">When the Companion started (null: unknown). On the first tick, a missed occurrence planned
/// after it was missed because the device could not be reached ("offline"), one before it "companion_off".</param>
public sealed class ActionScheduler(TimeZoneInfo zone, IEnumerable<string> decided, DateTimeOffset? runningSince = null)
{
    public static readonly TimeSpan RetryInterval = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan SleepGap = TimeSpan.FromMinutes(2);

    private readonly HashSet<string> _decided = [.. decided];
    private readonly Dictionary<string, Waiting> _waiting = [];
    private DateTimeOffset? _lastTick;
    private DateTimeOffset? _liveSince; // switched on at runtime: occurrences planned before are not ours
    private bool _wasLive;

    public bool IsDecided(string occurrence) => _decided.Contains(occurrence);

    public IReadOnlyList<ActionDecision> Tick(WeekSchedule schedule, DateTimeOffset now, bool connected)
    {
        var decisions = new List<ActionDecision>();
        var first = _lastTick is null;
        var gap = _lastTick is { } last ? now - last : TimeSpan.Zero;
        _lastTick = now;
        if (!schedule.Live)
        {
            _wasLive = false;
            _waiting.Clear();
            return decisions;
        }
        if (!_wasLive)
        {
            _wasLive = true;
            _liveSince = first ? null : now; // live when the Companion started: earlier ones today were missed while it was off
        }
        var paused = schedule.IsPaused(now);
        foreach (var o in Occurrences(schedule, now))
        {
            if (_decided.Contains(o.Id) || _liveSince is { } since && o.PlannedUtc < since)
            {
                continue;
            }
            _waiting.TryGetValue(o.Id, out var waiting);
            if (waiting is { InFlight: true })
            {
                continue; // sent: its answer decides (Answered / Busy), never a skip on top of a request
            }
            if (now > o.Deadline)
            {
                var reason = waiting is { BusyMode: { } mode } ? $"busy:{mode}"
                    : waiting is not null ? "offline"
                    : first ? (runningSince is { } started && o.PlannedUtc >= started ? "offline" : "companion_off")
                    : gap > SleepGap ? "asleep"
                    : connected ? "missed" : "offline";
                Decide(o.Id);
                decisions.Add(new ActionDecision(o, ActionStep.Skip, reason));
                continue;
            }
            if (paused)
            {
                Decide(o.Id);
                decisions.Add(new ActionDecision(o, ActionStep.Skip, "paused"));
                continue;
            }
            if (!connected)
            {
                _waiting.TryAdd(o.Id, new Waiting(now, null));
                continue;
            }
            if (waiting is not null && now < waiting.NextTry)
            {
                continue;
            }
            // Not again before the answer (or the retry time) is known.
            _waiting[o.Id] = new Waiting(now + RetryInterval, waiting?.BusyMode, InFlight: true);
            decisions.Add(new ActionDecision(o, ActionStep.Send));
        }
        return decisions;
    }

    /// <summary>The request was sent and answered (ok, dup, or an uncertain / refused outcome): never again.</summary>
    public void Answered(string occurrence) => Decide(occurrence);

    /// <summary>The device was busy (Clock Mode, a show, the question): try again in 10 s, within the grace period.</summary>
    public void Busy(string occurrence, DateTimeOffset now, string? mode) =>
        _waiting[occurrence] = new Waiting(now + RetryInterval, mode ?? "busy", InFlight: false);

    /// <summary>Certainly not sent (the link was gone after the request was journaled): due again while its grace lasts.</summary>
    public void NotSent(string occurrence)
    {
        _waiting.TryGetValue(occurrence, out var waiting);
        _waiting[occurrence] = new Waiting(DateTimeOffset.MinValue, waiting?.BusyMode, InFlight: false);
    }

    /// <summary>Recorded as skipped by the caller for a reason of its own (the firmware cannot do it).</summary>
    public void Skipped(string occurrence) => Decide(occurrence);

    /// <summary>
    /// Today's occurrences (and yesterday's whose grace period reaches past midnight) that have started by
    /// <paramref name="now"/>, earliest first.
    /// </summary>
    public IEnumerable<Occurrence> Occurrences(WeekSchedule schedule, DateTimeOffset now)
    {
        var today = TimeZoneInfo.ConvertTime(now, zone).Date;
        var midnight = ToUtc(today, zone);
        var list = new List<Occurrence>();
        foreach (var date in new[] { today.AddDays(-1), today })
        {
            foreach (var b in schedule[date.DayOfWeek].Where(b => b.Enabled && b.Action is not null && ScheduledActions.IsAllowed(b.Action)))
            {
                var local = date.AddMinutes(b.Start);
                var o = new Occurrence(Occurrence.IdFor(b.Id, date), b.Id, b.Action!, local, ToUtc(local, zone));
                if (o.PlannedUtc <= now && (date == today || o.Deadline >= midnight))
                {
                    list.Add(o);
                }
            }
        }
        return list.OrderBy(o => o.PlannedUtc);
    }

    /// <summary>
    /// A local wall-clock time as an instant: in the hour skipped when daylight saving time starts, the first minute
    /// that exists; in the hour repeated when it ends, the first of the two.
    /// </summary>
    public static DateTimeOffset ToUtc(DateTime local, TimeZoneInfo zone)
    {
        local = DateTime.SpecifyKind(local, DateTimeKind.Unspecified);
        while (zone.IsInvalidTime(local))
        {
            local = local.AddMinutes(1);
        }
        var offset = zone.IsAmbiguousTime(local) ? zone.GetAmbiguousTimeOffsets(local).Max() : zone.GetUtcOffset(local);
        return new DateTimeOffset(local, offset);
    }

    private void Decide(string occurrence)
    {
        _decided.Add(occurrence);
        _waiting.Remove(occurrence);
    }

    private sealed record Waiting(DateTimeOffset NextTry, string? BusyMode, bool InFlight = false);
}
