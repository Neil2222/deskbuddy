namespace DeskBuddy.Companion.Schedule;

/// <summary>Something you do in real life (not one of Buddy's shows), as a schedule block can plan it.</summary>
/// <param name="Id">Stable id, stored in the schedule (and later in the activity history); never renamed.</param>
/// <param name="Label">How the window shows it.</param>
public sealed record ActivityType(string Id, string Label);

/// <summary>
/// The real-world activities DeskBuddy knows. One list: a new activity is one entry here. Stored data keeps only the
/// id, so an id this version does not know (written by a newer one) is shown as it is, never dropped.
/// </summary>
public static class ActivityTypes
{
    public static readonly IReadOnlyList<ActivityType> All =
    [
        new("work", "Working"),
        new("meeting", "Meeting"),
        new("coffee", "Coffee"),
        new("water", "Water"),
        new("smoke", "Smoking break"),
        new("stretch", "Stretch / walk"),
        new("lunch", "Lunch"),
    ];

    public static string Label(string id) => All.FirstOrDefault(a => a.Id == id)?.Label ?? id;
}
