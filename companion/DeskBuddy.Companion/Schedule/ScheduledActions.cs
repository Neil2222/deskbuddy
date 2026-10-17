namespace DeskBuddy.Companion.Schedule;

/// <summary>One of Buddy's activities a schedule block may start at its start time.</summary>
/// <param name="Id">The firmware's activity name (<c>act name=</c>, hello <c>acts=</c>).</param>
/// <param name="Question">Whether it asks something (it can be answered): a longer grace period.</param>
public sealed record ScheduledAction(string Id, string Label, bool Question);

/// <summary>
/// The only actions a schedule may start: Buddy's own shows and the question, each through the firmware's existing
/// <c>act</c>. Never Monster, PDC Manager, Teams, messages or anything else with an effect outside Buddy.
/// </summary>
public static class ScheduledActions
{
    public static readonly IReadOnlyList<ScheduledAction> All =
    [
        new("ask_peukie", "Ask: Peukie?", true),
        new("sneeze", "Sneeze", false),
        new("stare", "Stare", false),
        new("meditate", "Meditate", false),
        new("focus", "Focus", false),
        new("water", "Water", false),
    ];

    /// <summary>How long after its start time an action may still run (approved: questions 3 min, shows 1 min).</summary>
    public static readonly TimeSpan QuestionGrace = TimeSpan.FromMinutes(3);
    public static readonly TimeSpan AnimationGrace = TimeSpan.FromMinutes(1);

    public static ScheduledAction? Find(string? id) => All.FirstOrDefault(a => a.Id == id);

    public static bool IsAllowed(string? id) => Find(id) is not null;

    public static TimeSpan Grace(string id) => Find(id)?.Question == true ? QuestionGrace : AnimationGrace;

    public static string Label(string id) => Find(id)?.Label ?? id;
}
