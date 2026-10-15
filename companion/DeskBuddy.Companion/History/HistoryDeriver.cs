namespace DeskBuddy.Companion.History;

/// <summary>Who started an interaction.</summary>
public enum InteractionOrigin
{
    /// <summary>On the touchscreen (evt act src=touch).</summary>
    Touchscreen,
    /// <summary>The Companion, at the user's request (an Activities tile); its request is in the journal.</summary>
    Companion,
    /// <summary>The schedule (P6); its request is in the journal.</summary>
    Scheduled,
    /// <summary>A command whose request was not recorded (another tool, or before P5).</summary>
    Command,
    /// <summary>Not known: the event that says it (shown) was never received.</summary>
    Unknown,
}

/// <summary>How an interaction ended, exactly as the device reported it.</summary>
public enum InteractionOutcome
{
    Yes,
    No,
    /// <summary>Asked, no answer within the window. Not a no.</summary>
    Timeout,
    /// <summary>Cut off (Clock Mode, a restart of his mood). Never an answer.</summary>
    Interrupted,
    /// <summary>A show played to its end.</summary>
    Done,
    /// <summary>The device refused the request (busy, unsupported): it never started.</summary>
    NotStarted,
    /// <summary>No outcome received (still running, a lost event, or the request got no reply).</summary>
    Unknown,
}

/// <summary>
/// One interaction: an activity Buddy played or a question he asked, folded from its events.
/// <see cref="Shown"/> only when <c>evt act … state=shown</c> was received: an accepted command is not "shown".
/// </summary>
public sealed record Interaction(
    string Id, string Dev, string Boot, uint Iid, string? Action, InteractionOrigin Origin, bool Shown,
    DeviceTime? ShownAt, InteractionOutcome Outcome, DeviceTime? EndedAt, DateTimeOffset? RequestedAtUtc,
    string? RequestResult)
{
    /// <summary>A question (it can be answered), as opposed to one of Buddy's shows.</summary>
    public bool IsQuestion => Action?.StartsWith("ask_", StringComparison.Ordinal) == true;
}

/// <summary>
/// Something you really did. <see cref="Via"/> says what it rests on: <c>myday:&lt;item&gt;</c> (a MY DAY tap) or
/// <c>interaction:&lt;id&gt;</c> (JA to the question). <see cref="EndedAt"/> only from an explicit end (none yet).
/// </summary>
public sealed record ActivityRecord(
    string Id, string Type, DeviceTime StartedAt, DeviceTime? EndedAt, string Via, string Dev, string Boot);

public sealed record HistorySnapshot(
    IReadOnlyList<Interaction> Interactions, IReadOnlyList<ActivityRecord> Activities,
    IReadOnlyList<GapEntry> Gaps, IReadOnlyList<BootEndEntry> BootEnds)
{
    public static readonly HistorySnapshot Empty = new([], [], [], []);
}

/// <summary>
/// Derives the history from the canonical logs, as a pure function: the same logs always give the same records with
/// the same ids, so replays, restarts and rebuilds never create a second interaction or activity.
/// What is not derived: a mood change is never an answer or an activity; a show (sneeze, water, …) is never an
/// activity; a missing outcome stays unknown.
/// </summary>
public static class HistoryDeriver
{
    /// <summary>The question whose JA starts a smoke break, and the activity that JA records.</summary>
    public const string SmokeQuestion = "ask_peukie";
    public const string SmokeActivity = "smoke";
    private const string MyDayPrefix = "myday.";

    public static HistorySnapshot Derive(IEnumerable<LogEntry> log, IEnumerable<object> journal)
    {
        var builders = new Dictionary<(string, string, uint), Builder>();
        var order = new List<Builder>();
        var activities = new List<(ActivityRecord Activity, int Order)>();
        var gaps = new List<GapEntry>();
        var ends = new List<BootEndEntry>();
        var position = 0;

        Builder For(string dev, string boot, uint iid)
        {
            if (!builders.TryGetValue((dev, boot, iid), out var b))
            {
                b = new Builder(dev, boot, iid, position);
                builders[(dev, boot, iid)] = b;
                order.Add(b);
            }
            return b;
        }

        foreach (var entry in log)
        {
            position++;
            switch (entry)
            {
            case EventEntry { Name: "act" } e when uint.TryParse(e.Arg("iid"), out var iid) && iid != 0:
                var b = For(e.Dev, e.Boot, iid);
                switch (e.Arg("state"))
                {
                case "shown":
                    b.Shown = true;
                    b.ShownAt ??= e.Time;
                    b.Action ??= e.Arg("name");
                    b.Source ??= e.Arg("src");
                    break;
                case "done":
                    b.Result ??= e.Arg("result");
                    b.EndedAt ??= e.Time;
                    break;
                }
                break;
            case EventEntry { Name: "did" } e when e.Arg("item") is { Length: > 0 } item:
                var type = item.StartsWith(MyDayPrefix, StringComparison.Ordinal) ? item[MyDayPrefix.Length..] : item;
                activities.Add((new ActivityRecord($"{e.Dev}/{e.Boot}/{e.Seq}", type, e.Time, null, $"myday:{item}", e.Dev, e.Boot), position));
                break;
            case GapEntry g:
                gaps.Add(g);
                break;
            case BootEndEntry end:
                ends.Add(end);
                break;
            }
        }

        foreach (var j in journal)
        {
            switch (j)
            {
            case RequestEntry r:
                var b = For(r.Dev, r.Boot, r.Iid);
                b.RequestedAt ??= r.AtUtc;
                b.Origin ??= r.Origin;
                b.RequestedAction ??= r.Action;
                break;
            case RequestResultEntry r:
                For(r.Dev, r.Boot, r.Iid).RequestResult ??= r.Result;
                break;
            }
        }

        var interactions = order.Select(b => b.Build()).ToList();
        foreach (var i in interactions)
        {
            if (i.Action == SmokeQuestion && i.Outcome == InteractionOutcome.Yes)
            {
                // One per interaction, however often its events arrive: the id is the interaction's.
                var at = i.EndedAt ?? i.ShownAt ?? new DeviceTime(null, null);
                activities.Add((new ActivityRecord($"{i.Id}/{SmokeActivity}", SmokeActivity, at, null, $"interaction:{i.Id}", i.Dev, i.Boot),
                                builders[(i.Dev, i.Boot, i.Iid)].Position));
            }
        }
        return new HistorySnapshot(interactions, activities.OrderBy(a => a.Order).Select(a => a.Activity).ToList(), gaps, ends);
    }

    private sealed class Builder(string dev, string boot, uint iid, int position)
    {
        public int Position { get; } = position;
        public bool Shown;
        public DeviceTime? ShownAt, EndedAt;
        public string? Action, RequestedAction, Source, Result, Origin, RequestResult;
        public DateTimeOffset? RequestedAt;

        public Interaction Build()
        {
            var origin = Source == "touch" ? InteractionOrigin.Touchscreen
                : Origin == "companion" ? InteractionOrigin.Companion
                : Origin == "scheduled" ? InteractionOrigin.Scheduled
                : Source == "cmd" ? InteractionOrigin.Command
                : InteractionOrigin.Unknown;
            var outcome = Result switch
            {
                "yes" => InteractionOutcome.Yes,
                "no" => InteractionOutcome.No,
                "timeout" => InteractionOutcome.Timeout,
                "interrupted" => InteractionOutcome.Interrupted,
                "done" => InteractionOutcome.Done,
                _ when !Shown && (RequestResult is "busy" or "unsupported" ||
                                  RequestResult?.StartsWith("err:", StringComparison.Ordinal) == true)
                    => InteractionOutcome.NotStarted,
                _ => InteractionOutcome.Unknown,
            };
            return new Interaction($"{dev}/{boot}/i{iid}", dev, boot, iid, Action ?? RequestedAction, origin, Shown, ShownAt,
                                   outcome, EndedAt, RequestedAt, RequestResult);
        }
    }
}
