using System.Globalization;
using DeskBuddy.Companion.Schedule;

namespace DeskBuddy.Companion.History;

public enum HistoryItemKind
{
    /// <summary>Something you said you did (MY DAY).</summary>
    Activity,
    /// <summary>A question Buddy asked (or was asked to ask).</summary>
    Question,
    /// <summary>One of Buddy's shows: never an activity of yours.</summary>
    Show,
    /// <summary>A scheduled action that did not run.</summary>
    Skipped,
    /// <summary>Recorded events were lost (the device's buffer overflowed): explains missing entries.</summary>
    Notice,
}

/// <summary>
/// One entry of a day in the History view, projected from the P5 / P6 records without changing them.
/// <see cref="AtUtc"/> is null when only the device's uptime is known: such entries are listed apart, never placed at
/// an invented time. <see cref="Displayed"/> only after a confirmed <c>shown</c> event. A YES-derived smoke break is
/// <see cref="Linked"/> to its question, not an entry of its own (<see cref="LinkedHidden"/>: hidden with it, or alone).
/// </summary>
public sealed record HistoryItem(
    string Id, HistoryItemKind Kind, DateTimeOffset? AtUtc, DateTimeOffset? ReceivedUtc, long? UptimeMs, string Dev, string? Boot,
    uint Seq, string Title, string Status, bool Displayed, string? Origin, bool Hidden,
    Interaction? Interaction = null, ActivityRecord? Activity = null, ScheduledOccurrence? Occurrence = null,
    ActivityRecord? Linked = null, GapEntry? Gap = null, bool LinkedHidden = false);

/// <summary>A day's numbers, from the entries that are not hidden. Each activity counts once.</summary>
public sealed record HistoryStats(
    int Activities, IReadOnlyDictionary<string, int> ActivitiesByType, int SmokeBreaks, int SmokeFromQuestions,
    int QuestionsShown, int Yes, int No, int TimedOut, int Interrupted, int NoOutcomeYet,
    int NotStarted, int Unconfirmed, int Skipped, IReadOnlyDictionary<string, int> SkipReasons, int Shows);

/// <summary>A device state change of the day, kept out of the timeline (a restart, the schedule switched or paused).</summary>
public sealed record HistoryTechnical(DateTimeOffset AtUtc, string Text);

public sealed record HistoryDay(
    DateOnly Date, IReadOnlyList<HistoryItem> Timeline, IReadOnlyList<HistoryItem> UnknownTime,
    IReadOnlyList<HistoryTechnical> Technical, HistoryStats Stats, int HiddenCount);

/// <summary>Today's plan as the schedule holds it now: shown only for today, labelled as a plan, never as history.</summary>
public sealed record PlanItem(string Name, DateTime StartLocal, DateTime EndLocal, string? Mood, string? Plans, string? Action);

/// <summary>One source's history as the History page shows it, made in one piece on the recorder's worker.</summary>
public sealed record HistoryState(HistorySnapshot Snapshot, IReadOnlyList<HistoryItem> Items, bool Loaded)
{
    public static readonly HistoryState Empty = new(HistorySnapshot.Empty, [], false);
}

/// <summary>
/// What the History page shows, as pure functions of one source's snapshot, its canonical log and the annotations.
/// Nothing is derived beyond P5 / P6: moods, plans and Buddy's shows never become activities; a timeout is not a "no";
/// an unanswered or unconfirmed request is not an outcome; an uptime-only event gets no date of its own.
/// </summary>
public static class HistoryView
{
    /// <summary>Every entry of the source, with its hidden flag; linked smoke breaks inside their questions.</summary>
    public static IReadOnlyList<HistoryItem> Items(HistorySnapshot snapshot, IReadOnlyList<LogEntry> log, HistoryAnnotations annotations)
    {
        // Where each recorded event came from: receipt time and seq, for the uptime-only entries and their order.
        var actFirst = new Dictionary<(string, string, uint), EventEntry>();
        var bySeq = new Dictionary<(string, string, uint), EventEntry>();
        foreach (var e in log.OfType<EventEntry>())
        {
            bySeq[(e.Dev, e.Boot, e.Seq)] = e;
            if (e.Name == "act" && uint.TryParse(e.Arg("iid"), out var iid) && !actFirst.ContainsKey((e.Dev, e.Boot, iid)))
            {
                actFirst[(e.Dev, e.Boot, iid)] = e;
            }
        }
        var linked = snapshot.Activities.Where(a => a.Via.StartsWith("interaction:", StringComparison.Ordinal))
                                        .ToDictionary(a => a.Via["interaction:".Length..]);
        var occurrenceOf = snapshot.Occurrences.Where(o => o.InteractionId is not null).ToDictionary(o => o.InteractionId!);
        var items = new List<HistoryItem>();

        foreach (var i in snapshot.Interactions)
        {
            occurrenceOf.TryGetValue(i.Id, out var occ);
            if (occ?.Skipped is not null)
            {
                continue; // refused until its grace period ended: shown once, as the skipped occurrence
            }
            actFirst.TryGetValue((i.Dev, i.Boot, i.Iid), out var first);
            var at = i.ShownAt?.Utc ?? i.EndedAt?.Utc ?? i.RequestedAtUtc;
            var uptime = at is null ? (i.ShownAt ?? i.EndedAt)?.UptimeMs : null;
            var hidden = Hidden(annotations, at, first?.ReceivedUtc, i.Id, occ?.Id);
            linked.TryGetValue(i.Id, out var smoke);
            items.Add(new HistoryItem(i.Id, i.IsQuestion ? HistoryItemKind.Question : HistoryItemKind.Show, at, first?.ReceivedUtc, uptime,
                i.Dev, i.Boot, first?.Seq ?? 0, ActionTitle(i.Action), Status(i), i.Shown, Origin(i, occ), hidden,
                Interaction: i, Occurrence: occ, Linked: smoke,
                LinkedHidden: smoke is not null && (hidden || annotations.HidesItem(smoke.Id))));
        }

        foreach (var a in snapshot.Activities.Where(a => a.Via.StartsWith("myday:", StringComparison.Ordinal)))
        {
            var seq = uint.TryParse(a.Id[(a.Id.LastIndexOf('/') + 1)..], out var n) ? n : 0;
            bySeq.TryGetValue((a.Dev, a.Boot, seq), out var e);
            items.Add(new HistoryItem(a.Id, HistoryItemKind.Activity, a.StartedAt.Utc, e?.ReceivedUtc, a.StartedAt.Known ? null : a.StartedAt.UptimeMs,
                a.Dev, a.Boot, seq, ActivityTypes.Label(a.Type), "You did this (MY DAY)", false, "MY DAY on the touchscreen",
                Hidden(annotations, a.StartedAt.Utc, e?.ReceivedUtc, a.Id), Activity: a));
        }

        var interactions = snapshot.Interactions.ToDictionary(i => i.Id);
        foreach (var o in snapshot.Occurrences.Where(o => o.Skipped is not null))
        {
            var i = o.InteractionId is { } id && interactions.TryGetValue(id, out var found) ? found : null;
            items.Add(new HistoryItem(o.Id, HistoryItemKind.Skipped, o.PlannedUtc, null, null, o.Dev ?? "", o.Boot, 0,
                ActionTitle(o.Action), "Skipped: " + SkipReason(o.Skipped!), false, Scheduled(o),
                Hidden(annotations, o.PlannedUtc, null, o.Id, o.InteractionId), Interaction: i, Occurrence: o));
        }

        foreach (var g in snapshot.Gaps)
        {
            var id = $"gap:{g.Dev}/{g.Boot}/{g.From}";
            items.Add(new HistoryItem(id, HistoryItemKind.Notice, g.ReceivedUtc, g.ReceivedUtc, null, g.Dev, g.Boot, g.From,
                $"{g.Count} recorded {(g.Count == 1 ? "entry" : "entries")} lost", "DeskBuddy's buffer was full while the Companion was away",
                false, null, Hidden(annotations, g.ReceivedUtc, g.ReceivedUtc, id), Gap: g));
        }
        return items;
    }

    /// <summary>
    /// One local calendar day in <paramref name="zone"/>: timed entries in order, uptime-only entries apart (on the day
    /// they were received, by boot and seq), state changes apart, and the day's numbers. Hidden entries are left out
    /// unless <paramref name="includeHidden"/>; they never count.
    /// </summary>
    public static HistoryDay Day(IReadOnlyList<HistoryItem> items, HistorySnapshot snapshot, DateOnly date, TimeZoneInfo zone, bool includeHidden = false)
    {
        bool OnDay(DateTimeOffset? utc) => utc is { } t && DateOnly.FromDateTime(TimeZoneInfo.ConvertTime(t, zone).DateTime) == date;
        var ofDay = items.Where(i => i.AtUtc is not null ? OnDay(i.AtUtc) : OnDay(i.ReceivedUtc)).ToList();
        var shown = ofDay.Where(i => includeHidden || !i.Hidden).ToList();
        var timeline = shown.Where(i => i.AtUtc is not null).OrderBy(i => i.AtUtc).ThenBy(i => i.Seq).ToList();
        var unknown = shown.Where(i => i.AtUtc is null).OrderBy(i => i.ReceivedUtc).ThenBy(i => i.Boot).ThenBy(i => i.Seq).ToList();

        var technical = new List<HistoryTechnical>();
        foreach (var end in snapshot.BootEnds.Where(e => OnDay(e.ReceivedUtc)))
        {
            technical.Add(new HistoryTechnical(end.ReceivedUtc, end.SeenLive
                ? "Buddy restarted"
                : $"Buddy restarted while the Companion was away: entries after #{end.After} of that run may be missing"));
        }
        foreach (var st in snapshot.ScheduleStates.Where(s => OnDay(s.AtUtc)))
        {
            technical.Add(new HistoryTechnical(st.AtUtc, !st.Live ? "Schedule switched off"
                : st.PausedUntil is not { } p ? "Schedule live"
                : p == DateTimeOffset.MaxValue ? "Schedule paused until resumed"
                : $"Schedule paused until {TimeZoneInfo.ConvertTime(p, zone).ToString("ddd HH:mm", CultureInfo.InvariantCulture)}"));
        }
        return new HistoryDay(date, timeline, unknown, technical.OrderBy(t => t.AtUtc).ToList(),
                              Stats(ofDay.Where(i => !i.Hidden)), ofDay.Count(i => i.Hidden));
    }

    /// <summary>
    /// What hiding a period would hide: the visible entries whose time lies in it (uptime-only ones by when they were
    /// received), each with its linked smoke break. Shown to the user for confirmation before anything is written.
    /// </summary>
    public static IReadOnlyList<HistoryItem> Affected(IReadOnlyList<HistoryItem> items, DateTimeOffset fromUtc, DateTimeOffset toUtc) =>
        items.Where(i => (i.AtUtc ?? i.ReceivedUtc) is { } t && fromUtc <= t && t < toUtc && !i.Hidden)
             .OrderBy(i => i.AtUtc ?? i.ReceivedUtc).ToList();

    /// <summary>The local dates that have visible entries (for the date picker).</summary>
    public static IReadOnlySet<DateOnly> Days(IReadOnlyList<HistoryItem> items, TimeZoneInfo zone) =>
        items.Where(i => !i.Hidden && (i.AtUtc ?? i.ReceivedUtc) is not null)
             .Select(i => DateOnly.FromDateTime(TimeZoneInfo.ConvertTime((i.AtUtc ?? i.ReceivedUtc)!.Value, zone).DateTime))
             .ToHashSet();

    /// <summary>Today's plan from the current schedule: nothing for any other day (the schedule then may have differed).</summary>
    public static IReadOnlyList<PlanItem> TodayPlan(WeekSchedule schedule, DateOnly date, DateOnly today)
    {
        if (date != today)
        {
            return [];
        }
        var day = date.ToDateTime(TimeOnly.MinValue);
        return schedule[day.DayOfWeek]
            .Where(b => b.Enabled && (b.Mood is not null || b.Plans is not null || b.Action is not null))
            .OrderBy(b => b.Start)
            .Select(b => new PlanItem(b.Name, day.AddMinutes(b.Start), day.AddMinutes(b.End), b.Mood, b.Plans, b.Action))
            .ToList();
    }

    /// <summary>How an interaction ended, in the History's words: a timeout is never "No", no outcome is never "Timed out".</summary>
    public static string Status(Interaction i) => i.Outcome switch
    {
        InteractionOutcome.Yes => "Yes",
        InteractionOutcome.No => "No",
        InteractionOutcome.Timeout => "Timed out",
        InteractionOutcome.Interrupted => "Interrupted",
        InteractionOutcome.Done => "Played",
        InteractionOutcome.NotStarted => "Not started",
        InteractionOutcome.Unconfirmed => "Unconfirmed",
        _ when i.Shown => "No outcome received",
        _ => "Not shown",
    };

    public static string SkipReason(string reason) => reason switch
    {
        "paused" => "the schedule was paused",
        "offline" => "DeskBuddy was not connected",
        "companion_off" => "the Companion was not running",
        "asleep" => "the PC was asleep",
        "missed" => "missed",
        "unsupported" => "this firmware cannot do it",
        "busy:office" => "Buddy was in Clock Mode",
        _ when reason.StartsWith("busy", StringComparison.Ordinal) => "Buddy was busy",
        _ => reason,
    };

    private static HistoryStats Stats(IEnumerable<HistoryItem> visible)
    {
        var list = visible.ToList();
        var activities = list.Where(i => i.Activity is not null).Select(i => i.Activity!)
                             .Concat(list.Where(i => i.Linked is not null && !i.LinkedHidden).Select(i => i.Linked!)).ToList();
        var shown = list.Where(i => i.Kind == HistoryItemKind.Question && i.Displayed).Select(i => i.Interaction!).ToList();
        var questions = list.Where(i => i.Kind == HistoryItemKind.Question).Select(i => i.Interaction!).ToList();
        var skipped = list.Where(i => i.Kind == HistoryItemKind.Skipped).ToList();
        return new HistoryStats(
            activities.Count,
            activities.GroupBy(a => a.Type).ToDictionary(g => g.Key, g => g.Count()),
            activities.Count(a => a.Type == HistoryDeriver.SmokeActivity),
            activities.Count(a => a.Type == HistoryDeriver.SmokeActivity && a.Via.StartsWith("interaction:", StringComparison.Ordinal)),
            shown.Count,
            shown.Count(i => i.Outcome == InteractionOutcome.Yes),
            shown.Count(i => i.Outcome == InteractionOutcome.No),
            shown.Count(i => i.Outcome == InteractionOutcome.Timeout),
            shown.Count(i => i.Outcome == InteractionOutcome.Interrupted),
            shown.Count(i => i.Outcome == InteractionOutcome.Unknown),
            questions.Count(i => i.Outcome == InteractionOutcome.NotStarted),
            questions.Count(i => i.Outcome == InteractionOutcome.Unconfirmed),
            skipped.Count,
            skipped.GroupBy(i => i.Occurrence!.Skipped!).ToDictionary(g => g.Key, g => g.Count()),
            list.Count(i => i.Kind == HistoryItemKind.Show));
    }

    // Hidden by one of its ids (the entry, its interaction or occurrence) or by a hidden period covering its time
    // (uptime-only entries: when they were received).
    private static bool Hidden(HistoryAnnotations a, DateTimeOffset? at, DateTimeOffset? received, params string?[] ids) =>
        ids.Any(id => id is not null && a.HidesItem(id)) || (at ?? received) is { } t && a.PeriodCovering(t) is not null;

    private static string ActionTitle(string? action) => action switch
    {
        null => "Something Buddy did",
        "ask_peukie" => "Peukie?",
        _ => ScheduledActions.Find(action)?.Label ?? action,
    };

    private static string? Origin(Interaction i, ScheduledOccurrence? occ) => i.Origin switch
    {
        InteractionOrigin.Scheduled => occ is not null ? Scheduled(occ) : "Scheduled",
        InteractionOrigin.Touchscreen => "On the touchscreen",
        InteractionOrigin.Companion => "From the Companion",
        InteractionOrigin.Command => "By command",
        _ => null,
    };

    private static string Scheduled(ScheduledOccurrence o) =>
        o.BlockName is { Length: > 0 } name ? $"Scheduled: \"{name}\" at {o.PlannedLocal:HH:mm}" : $"Scheduled at {o.PlannedLocal:HH:mm}";
}
