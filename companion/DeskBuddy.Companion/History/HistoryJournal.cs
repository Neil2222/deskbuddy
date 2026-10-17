using System.Globalization;
using DeskBuddy.Companion.Schedule;

namespace DeskBuddy.Companion.History;

/// <summary>The kinds of rows in a day's journal on the History page.</summary>
public enum JournalRowKind
{
    /// <summary>A section caption: today's plan, the day, time unknown.</summary>
    Header,
    /// <summary>Today's plan in one line (now / next); opens to list its blocks. Key "plan".</summary>
    PlanSummary,
    /// <summary>A block of today's plan: what the schedule says, never something that happened.</summary>
    Plan,
    /// <summary>One entry of the day (activity, question, show, skipped action, loss notice).</summary>
    Entry,
    /// <summary>Buddy's shows in a row, collapsed into one line until opened.</summary>
    Shows,
    /// <summary>Nothing to show for the day.</summary>
    Empty,
    /// <summary>Opens or closes the technical list.</summary>
    TechnicalToggle,
    /// <summary>A restart or schedule change: only while the technical list is open.</summary>
    Technical,
}

/// <summary>
/// How an entry reads at a glance (the page maps it to a colour). <see cref="Answer"/>: a valid answer other than yes
/// (a no): neutral, never an error colour.
/// </summary>
public enum JournalTone { Positive, Answer, Warning, Neutral, Muted }

/// <summary>
/// One row of the journal. <see cref="Key"/> is stable across refreshes (an entry's id, a group's first show), so the
/// selection and the open groups survive new events. <see cref="Nested"/>: a show listed under its opened group.
/// </summary>
public sealed record JournalRow(
    JournalRowKind Kind, string Key, string Text, HistoryItem? Item = null, PlanItem? Plan = null,
    IReadOnlyList<HistoryItem>? Shows = null, HistoryTechnical? Technical = null, bool Open = false,
    bool Current = false, bool Nested = false)
{
    public bool Selectable => Kind is JournalRowKind.Entry or JournalRowKind.Plan;
}

/// <summary>One of the four numbers above the journal.</summary>
public sealed record StatTile(string Label, int Value, string Detail, JournalTone Tone);

/// <summary>What the inspector shows for a selected row; <see cref="Technical"/> stays folded until asked for.</summary>
public sealed record InspectorInfo(
    string Title, string Kind, string Status, JournalTone Tone, string? Icon,
    IReadOnlyList<(string Label, string Value)> Fields, IReadOnlyList<string> Notes,
    IReadOnlyList<(string Label, string Value)> Technical);

/// <summary>
/// How the History page tells a day: its rows, numbers and details, as pure functions of the <see cref="HistoryView"/>
/// projection. Times are local; an entry without a device time is never given one (its arrival is named as such).
/// </summary>
public static class HistoryJournal
{
    private static readonly CultureInfo En = CultureInfo.InvariantCulture;

    /// <summary>The key of today's plan line: open it with the shows' groups (<c>openGroups</c>).</summary>
    public const string PlanGroup = "plan";

    /// <summary>
    /// The day's rows, top to bottom: today's plan (only today; one line unless opened), the entries in time order
    /// (Buddy's shows in a row collapsed into one line unless opened), the entries without a known time, then the
    /// technical toggle (and its list when open).
    /// </summary>
    public static IReadOnlyList<JournalRow> Rows(HistoryDay day, IReadOnlyList<PlanItem> plan, bool isToday,
                                                 IReadOnlySet<string> openGroups, bool technicalOpen, DateTime nowLocal)
    {
        var rows = new List<JournalRow>();
        if (plan.Count > 0)
        {
            // The block in force is the one that started last (as the schedule applies them).
            var now = plan.Where(p => p.StartLocal <= nowLocal && nowLocal < p.EndLocal).OrderBy(p => p.StartLocal).LastOrDefault();
            var next = plan.Where(p => p.StartLocal > nowLocal).OrderBy(p => p.StartLocal).FirstOrDefault();
            var parts = new List<string> { $"{plan.Count} {(plan.Count == 1 ? "block" : "blocks")}" };
            if (now is not null)
            {
                parts.Add($"now {PlanText(now)}");
            }
            parts.Add(next is not null ? $"next {PlanText(next)} at {next.StartLocal.ToString("HH:mm", En)}" : "nothing more planned");
            var open = openGroups.Contains(PlanGroup);
            rows.Add(new JournalRow(JournalRowKind.Header, "h:plan", "Today's plan"));
            rows.Add(new JournalRow(JournalRowKind.PlanSummary, PlanGroup, string.Join(" · ", parts), Open: open, Plan: now ?? next));
            if (open)
            {
                rows.AddRange(plan.Select(p => new JournalRow(JournalRowKind.Plan, PlanKey(p), PlanText(p), Plan: p, Current: p == now)));
            }
        }

        rows.Add(new JournalRow(JournalRowKind.Header, "h:day", isToday ? "So far today" : "What happened"));
        if (day.Timeline.Count == 0 && day.UnknownTime.Count == 0)
        {
            rows.Add(new JournalRow(JournalRowKind.Empty, "empty", isToday
                ? "Nothing recorded yet today."
                : "Nothing was recorded on this day."));
        }
        for (var i = 0; i < day.Timeline.Count;)
        {
            var item = day.Timeline[i];
            if (item.Kind != HistoryItemKind.Show)
            {
                rows.Add(new JournalRow(JournalRowKind.Entry, item.Id, item.Title, Item: item));
                i++;
                continue;
            }
            var run = new List<HistoryItem>();
            while (i < day.Timeline.Count && day.Timeline[i].Kind == HistoryItemKind.Show)
            {
                run.Add(day.Timeline[i++]);
            }
            var key = "shows:" + run[0].Id;
            var open = openGroups.Contains(key);
            rows.Add(new JournalRow(JournalRowKind.Shows, key, ShowsText(run), Shows: run, Open: open));
            if (open)
            {
                rows.AddRange(run.Select(s => new JournalRow(JournalRowKind.Entry, s.Id, s.Title, Item: s, Nested: true)));
            }
        }

        if (day.UnknownTime.Count > 0)
        {
            rows.Add(new JournalRow(JournalRowKind.Header, "h:unknown", "Time unknown"));
            rows.AddRange(day.UnknownTime.Select(u => new JournalRow(JournalRowKind.Entry, u.Id, u.Title, Item: u)));
        }

        if (day.Technical.Count > 0)
        {
            rows.Add(new JournalRow(JournalRowKind.TechnicalToggle, "technical",
                                    $"Technical: {day.Technical.Count} {(day.Technical.Count == 1 ? "change" : "changes")}", Open: technicalOpen));
            if (technicalOpen)
            {
                rows.AddRange(day.Technical.Select((t, n) => new JournalRow(JournalRowKind.Technical, $"t:{n}", t.Text, Technical: t)));
            }
        }
        return rows;
    }

    public static string PlanKey(PlanItem p) => $"plan:{p.StartLocal:HHmm}:{p.Name}";

    /// <summary>The four numbers: activities, smoke breaks, questions asked (with their answers) and skipped actions.</summary>
    public static IReadOnlyList<StatTile> Tiles(HistoryStats s)
    {
        var types = s.ActivitiesByType.OrderByDescending(t => t.Value).ThenBy(t => t.Key, StringComparer.Ordinal)
                     .Select(t => $"{t.Value} {ActivityTypes.Label(t.Key).ToLowerInvariant()}").Take(3).ToList();
        var fromMyDay = s.SmokeBreaks - s.SmokeFromQuestions;
        var answers = new List<string>();
        void Add(int n, string text)
        {
            if (n > 0)
            {
                answers.Add($"{n} {text}");
            }
        }
        Add(s.Yes, "yes");
        Add(s.No, "no");
        Add(s.TimedOut, "timed out");
        Add(s.Interrupted, "interrupted");
        Add(s.NoOutcomeYet, "no outcome");
        // Not shown (refused or never confirmed): not among the questions asked, so listed as extras.
        var notShown = new List<string>();
        if (s.NotStarted > 0)
        {
            notShown.Add($"+{s.NotStarted} not started");
        }
        if (s.Unconfirmed > 0)
        {
            notShown.Add($"+{s.Unconfirmed} unconfirmed");
        }
        var topSkip = s.SkipReasons.OrderByDescending(r => r.Value).ThenBy(r => r.Key, StringComparer.Ordinal).FirstOrDefault();
        return
        [
            new("Activities", s.Activities, types.Count > 0 ? string.Join(" · ", types) : "Nothing logged", JournalTone.Positive),
            new("Smoke breaks", s.SmokeBreaks, s.SmokeBreaks == 0 ? "None"
                : string.Join(" · ", new[] { s.SmokeFromQuestions > 0 ? $"{s.SmokeFromQuestions} after a yes" : null,
                                              fromMyDay > 0 ? $"{fromMyDay} via MY DAY" : null }.OfType<string>()), JournalTone.Warning),
            new("Questions asked", s.QuestionsShown, answers.Count > 0 || notShown.Count > 0
                ? string.Join(" · ", answers.Concat(notShown)) : "None asked", JournalTone.Neutral),
            new("Skipped", s.Skipped, s.Skipped == 0 ? "Nothing skipped"
                : s.SkipReasons.Count == 1 ? Capital(HistoryView.SkipReason(topSkip.Key)) : $"Mostly: {HistoryView.SkipReason(topSkip.Key)}", JournalTone.Muted),
        ];
    }

    /// <summary>The entry's colour at a glance: never green for an answer that was not a yes.</summary>
    public static JournalTone Tone(HistoryItem item) => item.Kind switch
    {
        HistoryItemKind.Activity => JournalTone.Positive,
        HistoryItemKind.Skipped => JournalTone.Muted,
        HistoryItemKind.Notice => JournalTone.Warning,
        _ => item.Interaction?.Outcome switch
        {
            InteractionOutcome.Yes => JournalTone.Positive,
            InteractionOutcome.No => JournalTone.Answer, // a valid answer, not an error
            InteractionOutcome.Timeout or InteractionOutcome.Unknown => JournalTone.Warning,
            InteractionOutcome.Done => JournalTone.Neutral,
            _ => JournalTone.Muted,
        },
    };

    /// <summary>Buddy's pixel art for an entry (assets/buddy), or null for a notice. The page falls back to his face.</summary>
    public static string? Icon(HistoryItem item) => item.Kind switch
    {
        HistoryItemKind.Activity => ActivityIcon(item.Activity!.Type),
        HistoryItemKind.Notice => null,
        _ => ActionIcon(item.Interaction?.Action ?? item.Occurrence?.Action),
    };

    public static string ActivityIcon(string type) => type switch
    {
        "work" => "face-working",
        "meeting" => "myday-meeting",
        "coffee" => "myday-coffee",
        "water" => "myday-water",
        "smoke" => "face-peukie",
        "stretch" => "myday-walk",
        "lunch" => "myday-lunch",
        _ => "myday-calendar",
    };

    public static string ActionIcon(string? action) => action switch
    {
        null => "face-neutral",
        "ask_peukie" => "face-peukie",
        "water" => "glass",
        "monster" => "can",
        "poke" => "face-surprised",
        _ => "face-" + action,
    };

    /// <summary>The time column: local HH:mm, or nothing when only the device's uptime is known.</summary>
    public static string Time(HistoryItem item, TimeZoneInfo zone) =>
        item.AtUtc is { } t ? Local(t, zone).ToString("HH:mm", En) : "";

    /// <summary>The second line of an entry's card.</summary>
    public static string Line(HistoryItem item) => item.Kind switch
    {
        HistoryItemKind.Activity => item.AtUtc is null ? "MY DAY · time unknown" : "MY DAY",
        HistoryItemKind.Question => $"{item.Status} · {item.Origin ?? "Asked"}",
        HistoryItemKind.Show => $"{item.Status} · {item.Origin ?? "Buddy's show"}",
        HistoryItemKind.Skipped => $"{item.Status} · {item.Origin}",
        _ => item.Status,
    };

    /// <summary>"Today", "Yesterday" or "Fri 16 Oct" (with the year when it is not this year's).</summary>
    public static string DateLabel(DateOnly date, DateOnly today) =>
        date == today ? "Today" : date == today.AddDays(-1) ? "Yesterday"
        : date.ToString(date.Year == today.Year ? "ddd d MMM" : "ddd d MMM yyyy", En);

    /// <summary>The selected row in words, for the inspector.</summary>
    public static InspectorInfo Describe(JournalRow row, TimeZoneInfo zone, HistoryAnnotations? annotations = null) => row.Kind switch
    {
        JournalRowKind.Plan => DescribePlan(row.Plan!),
        _ => Describe(row.Item!, zone, annotations),
    };

    /// <summary>An entry in words; with the annotations, a hidden entry says what hides it (an item hide or a period).</summary>
    public static InspectorInfo Describe(HistoryItem item, TimeZoneInfo zone, HistoryAnnotations? annotations = null)
    {
        var fields = new List<(string, string)>();
        var notes = new List<string>();
        var tech = new List<(string, string)> { ("Entry id", item.Id) };
        string? T(DateTimeOffset? utc) => utc is { } t ? Local(t, zone).ToString("HH:mm:ss", En) : null;
        string Day(DateTimeOffset utc) => Local(utc, zone).ToString("ddd d MMM HH:mm", En);
        string kind;

        switch (item.Kind)
        {
        case HistoryItemKind.Activity:
        {
            var a = item.Activity!;
            kind = "Something you did";
            fields.Add(("Activity", ActivityTypes.Label(a.Type)));
            fields.Add(("When", T(a.StartedAt.Utc) ?? "Time unknown"));
            fields.Add(("Logged with", "MY DAY on the touchscreen"));
            tech.Add(("Device time", a.StartedAt.Utc is { } u ? HistoryCodec.Iso(u) : $"uptime {a.StartedAt.UptimeMs} ms (clock not set)"));
            break;
        }
        case HistoryItemKind.Question:
        case HistoryItemKind.Show:
        {
            var i = item.Interaction!;
            kind = item.Kind == HistoryItemKind.Question ? "Buddy's question" : "Buddy's show";
            if (item.Kind == HistoryItemKind.Question)
            {
                fields.Add(("Answer", item.Status));
            }
            else
            {
                fields.Add(("Result", item.Status));
            }
            fields.Add((i.Shown ? "On screen" : "Requested", (i.Shown ? T(i.ShownAt?.Utc) : T(i.RequestedAtUtc)) ?? "Time unknown"));
            if (i.EndedAt is { } end && (i.Outcome is not (InteractionOutcome.NotStarted or InteractionOutcome.Unconfirmed)))
            {
                fields.Add((i.Outcome == InteractionOutcome.Timeout ? "Ended" : "Answered", T(end.Utc) ?? "Time unknown"));
            }
            fields.Add(("Started by", item.Origin ?? "Not known"));
            if (item.Linked is { } smoke)
            {
                fields.Add(("Counted as", item.LinkedHidden ? "Smoking break (hidden)" : $"Smoking break at {T(smoke.StartedAt.Utc) ?? "an unknown time"}"));
            }
            notes.Add(OutcomeNote(i));
            tech.Add(("Interaction", $"iid {i.Iid}{(i.Iid >= 0x80000000 ? " (device's own)" : "")}"));
            tech.Add(("Action", i.Action ?? "not known"));
            tech.Add(("Outcome", i.Outcome.ToString()));
            if (i.RequestResult is { } result)
            {
                tech.Add(("Request result", result));
            }
            if (item.Occurrence is { } occ)
            {
                tech.Add(("Occurrence", occ.Id));
            }
            if (i.ShownAt is { Known: false } up)
            {
                tech.Add(("Device time", $"uptime {up.UptimeMs} ms (clock not set)"));
            }
            break;
        }
        case HistoryItemKind.Skipped:
        {
            var o = item.Occurrence!;
            kind = "Scheduled action, skipped";
            fields.Add(("Action", item.Title));
            fields.Add(("Planned for", o.PlannedLocal.ToString("HH:mm", En)));
            fields.Add(("Why", Capital(HistoryView.SkipReason(o.Skipped!))));
            if (o.BlockName is { Length: > 0 } name)
            {
                fields.Add(("Block", name));
            }
            notes.Add(o.Skipped!.StartsWith("busy", StringComparison.Ordinal)
                ? "Buddy was asked during the grace period but stayed busy, so it was skipped. It is not done afterwards."
                : "Nothing missed is done afterwards.");
            tech.Add(("Occurrence", o.Id));
            tech.Add(("Skip reason", o.Skipped!));
            if (o.Iid is { } iid)
            {
                tech.Add(("Last request", $"iid {iid}: {o.LastResult ?? "no answer"}"));
            }
            break;
        }
        default:
        {
            var g = item.Gap!;
            kind = "Notice";
            fields.Add(("Lost", $"{g.Count} recorded {(g.Count == 1 ? "entry" : "entries")}"));
            fields.Add(("Noticed", Day(g.ReceivedUtc)));
            notes.Add("DeskBuddy keeps a limited number of entries while the Companion is away. These were overwritten before " +
                      "they could be collected, so this day may be missing things you did or answers you gave.");
            tech.Add(("Sequence", $"#{g.From}–#{g.To}"));
            break;
        }
        }

        if (item.AtUtc is null && item.ReceivedUtc is { } received)
        {
            notes.Add($"DeskBuddy's clock was not set when this happened, so its time is unknown. It reached the Companion on " +
                      $"{Day(received)}: that is when it arrived, not when it happened.");
        }
        if (item.Hidden)
        {
            var by = annotations is null ? [] : HistoryView.HiddenBy(item, annotations);
            if (by.Count == 0)
            {
                notes.Add("Hidden as test data: not counted in the day's numbers.");
            }
            foreach (var a in by)
            {
                var what = a.IsPeriod ? $"with the period {Period(a, zone)}" : $"on {Day(a.AtUtc)}";
                notes.Add($"Hidden as test data {what}{(a.Note is { Length: > 0 } n ? $" ({n})" : "")}: not counted in the day's numbers. " +
                          "Nothing was deleted; restoring brings it back.");
            }
        }
        else if (item.LinkedHidden)
        {
            notes.Add("Its smoking break is hidden as test data: not counted.");
        }
        tech.Add(("Device", item.Dev));
        if (item.Boot is { } boot)
        {
            tech.Add(("Boot", boot));
        }
        if (item.Seq > 0)
        {
            tech.Add(("Event", $"#{item.Seq}"));
        }
        if (item.ReceivedUtc is { } r)
        {
            tech.Add(("Received", HistoryCodec.Iso(r)));
        }
        return new InspectorInfo(item.Title, kind, item.Status, Tone(item), Icon(item), fields, notes, tech);
    }

    /// <summary>
    /// A period on one local day, from <paramref name="fromMinute"/> to <paramref name="toMinute"/> (24:00 = the end of
    /// the day), in UTC; or why it cannot be hidden: it must end after it starts, must have passed (no future entries
    /// are hidden in advance), and its times must exist (not in the hour skipped by the spring clock change).
    /// </summary>
    public static (DateTimeOffset FromUtc, DateTimeOffset ToUtc, string? Error) LocalPeriod(
        DateOnly date, int fromMinute, int toMinute, TimeZoneInfo zone, DateTimeOffset nowUtc)
    {
        if (toMinute <= fromMinute)
        {
            return (default, default, "The end must be after the start.");
        }
        var start = date.ToDateTime(TimeOnly.MinValue);
        DateTime from = start.AddMinutes(fromMinute), to = start.AddMinutes(toMinute);
        if (zone.IsInvalidTime(from) || zone.IsInvalidTime(to))
        {
            return (default, default, "That time does not exist on this day (the clocks went forward).");
        }
        var fromUtc = new DateTimeOffset(TimeZoneInfo.ConvertTimeToUtc(from, zone));
        var toUtc = new DateTimeOffset(TimeZoneInfo.ConvertTimeToUtc(to, zone));
        if (toUtc > nowUtc)
        {
            return (fromUtc, toUtc, $"The period must have passed: it can end at {Local(nowUtc, zone).ToString("HH:mm", En)} at the latest.");
        }
        return (fromUtc, toUtc, null);
    }

    /// <summary>A period hide's span in local time: "Sat 17 Oct 01:52–02:38" (the end's date too when it differs).</summary>
    public static string Period(Annotation period, TimeZoneInfo zone)
    {
        var from = Local(period.FromUtc!.Value, zone);
        var to = Local(period.ToUtc!.Value, zone);
        return $"{from.ToString("ddd d MMM HH:mm", En)}–{to.ToString(to.Date == from.Date ? "HH:mm" : to.TimeOfDay == TimeSpan.Zero && to.Date == from.Date.AddDays(1) ? "24:00" : "ddd d MMM HH:mm", En)}";
    }

    /// <summary>
    /// What a set of entries is, in one sentence, for the period preview and the restore list: "5 entries: 2 questions
    /// (1 with its smoking break), 2 activities, 1 skipped action · 1 with an unknown time".
    /// </summary>
    public static string Summary(IReadOnlyList<HistoryItem> items)
    {
        if (items.Count == 0)
        {
            return "No entries";
        }
        string N(int n, string one, string many) => $"{n} {(n == 1 ? one : many)}";
        var parts = new List<string>();
        var questions = items.Where(i => i.Kind == HistoryItemKind.Question).ToList();
        if (questions.Count > 0)
        {
            var linked = questions.Count(q => q.Linked is not null);
            parts.Add(N(questions.Count, "question", "questions") + (linked > 0 ? $" ({linked} with {(linked == 1 ? "its" : "their")} smoking break)" : ""));
        }
        void Add(HistoryItemKind kind, string one, string many)
        {
            var n = items.Count(i => i.Kind == kind);
            if (n > 0)
            {
                parts.Add(N(n, one, many));
            }
        }
        Add(HistoryItemKind.Activity, "activity", "activities");
        Add(HistoryItemKind.Show, "show", "shows");
        Add(HistoryItemKind.Skipped, "skipped action", "skipped actions");
        Add(HistoryItemKind.Notice, "loss notice", "loss notices");
        var unknown = items.Count(i => i.AtUtc is null);
        return $"{N(items.Count, "entry", "entries")}: {string.Join(", ", parts)}" + (unknown > 0 ? $" · {unknown} with an unknown time" : "");
    }

    /// <summary>
    /// One entry of a preview: its time (or "?"), what it is, and a detail line naming what goes with it: the linked
    /// smoking break, or for an uptime-only entry that its time is unknown and when it arrived.
    /// </summary>
    public static (string Time, string Title, string Detail) PreviewLine(HistoryItem item, TimeZoneInfo zone)
    {
        var detail = new List<string> { item.Kind switch
        {
            HistoryItemKind.Activity => "MY DAY",
            HistoryItemKind.Notice => "Loss notice",
            HistoryItemKind.Skipped => item.Status,
            HistoryItemKind.Show => $"Buddy's show · {item.Status}",
            _ => $"Question · {item.Status}",
        } };
        if (item.Linked is not null && !item.LinkedHidden)
        {
            detail.Add("with its smoking break (linked)");
        }
        if (item.AtUtc is null)
        {
            detail.Add(item.ReceivedUtc is { } r
                ? $"time unknown, arrived {Local(r, zone).ToString("ddd HH:mm", En)}"
                : "time unknown");
        }
        var date = item.AtUtc is { } t ? Local(t, zone).ToString("HH:mm", En) : "?";
        return (date, item.Title, string.Join(" · ", detail));
    }

    private static InspectorInfo DescribePlan(PlanItem p)
    {
        var fields = new List<(string, string)>
        {
            ("Time", $"{p.StartLocal:HH:mm}–{(p.EndLocal.Date > p.StartLocal.Date ? "24:00" : p.EndLocal.ToString("HH:mm", En))}"),
        };
        if (p.Mood is { } mood)
        {
            fields.Add(("Buddy's mood", Capital(mood)));
        }
        if (p.Plans is { } plans)
        {
            fields.Add(("You plan", ActivityTypes.Label(plans)));
        }
        if (p.Action is { } action)
        {
            fields.Add(("At its start", ScheduledActions.Label(action)));
        }
        return new InspectorInfo(p.Name.Length > 0 ? p.Name : "Schedule block", "Planned", "Plan", JournalTone.Muted, PlanIcon(p),
            fields, ["From today's schedule as it is now. A plan, not something that happened: what happened is listed below it."], []);
    }

    /// <summary>A plan block's art: what you plan to do, else Buddy's mood, else the calendar.</summary>
    public static string PlanIcon(PlanItem p) =>
        p.Plans is { } t ? ActivityIcon(t) : p.Mood is { } m ? "face-" + (m == "idle" ? "neutral" : m) : "myday-calendar";

    private static string OutcomeNote(Interaction i) => i.Outcome switch
    {
        InteractionOutcome.Yes => "Answered yes on the touchscreen.",
        InteractionOutcome.No => "Answered no on the touchscreen.",
        InteractionOutcome.Timeout => "Shown, but nobody answered before it ended. That is not a no.",
        InteractionOutcome.Interrupted => "Cut off before it ended (Clock Mode or a restart of his mood). Not an answer.",
        InteractionOutcome.Done => "Played to its end.",
        InteractionOutcome.NotStarted => "DeskBuddy refused the request (busy or not supported): it never appeared.",
        InteractionOutcome.Unconfirmed => "Sent, but DeskBuddy never confirmed it. It may or may not have appeared.",
        _ when i.Shown => "Shown, but how it ended was never received (a restart, or the entry was lost).",
        _ => "DeskBuddy accepted it, but never confirmed it on screen.",
    };

    private static string PlanText(PlanItem p)
    {
        var parts = new List<string>();
        if (p.Plans is { } plans)
        {
            parts.Add(ActivityTypes.Label(plans));
        }
        if (p.Action is { } action)
        {
            parts.Add(ScheduledActions.Label(action));
        }
        if (p.Mood is { } mood && parts.Count == 0)
        {
            parts.Add(Capital(mood));
        }
        return p.Name.Length > 0 ? p.Name : string.Join(" · ", parts);
    }

    private static string ShowsText(IReadOnlyList<HistoryItem> run) =>
        (run.Count == 1 ? $"Buddy played {run[0].Title}" : $"Buddy played {run.Count} shows") +
        (run.All(s => s.Hidden) ? " · hidden as test" : run.Any(s => s.Hidden) ? " · some hidden as test" : "");

    private static DateTime Local(DateTimeOffset utc, TimeZoneInfo zone) => TimeZoneInfo.ConvertTime(utc, zone).DateTime;

    private static string Capital(string s) => s.Length == 0 ? s : char.ToUpperInvariant(s[0]) + s[1..];
}
