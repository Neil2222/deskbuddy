namespace DeskBuddy.Companion;

public enum ActivityKind
{
    Mode,   // stays on until another mode is chosen; the tile shows which one is on
    Action, // plays once
}

/// <summary>
/// Something Buddy can do, as a tile in the Activities section. Add an entry here when the firmware learns a new
/// command; the window builds its tiles from this list and enables each one when the firmware's hello lists
/// <see cref="Cap"/>.
/// </summary>
/// <param name="Label">Tile text.</param>
/// <param name="Icon">Embedded pixel art (assets/buddy), without extension.</param>
/// <param name="Cap">Capability from hello that must be present.</param>
/// <param name="Command">What is sent (see docs/protocol.md).</param>
/// <param name="Mode">For modes: the device mode (status / evt mode) in which this tile shows as on.</param>
/// <param name="Reaction">Buddy's face in the window once the device has done it.</param>
public sealed record Activity(string Label, ActivityKind Kind, string Icon, string Cap, string Command,
                              string? Mode = null, string? Reaction = null);

public static class Activities
{
    public static readonly IReadOnlyList<Activity> All =
    [
        new("Buddy", ActivityKind.Mode, "face-neutral", "mode", "mode.set mode=normal", Mode: "normal"),
        new("Office", ActivityKind.Mode, "moon", "mode", "mode.set mode=office", Mode: "office"),
        new("Surprise!", ActivityKind.Action, "face-surprised", "poke", "poke", Reaction: "surprised"),
        new("Monster", ActivityKind.Action, "can", "monster", "monster", Reaction: "happy"),
    ];

    /// <summary>Whether a mode tile is on for the device's current mode (a Monster sequence happens in Buddy mode).</summary>
    public static bool IsOn(Activity a, string deviceMode) =>
        a.Kind == ActivityKind.Mode && (a.Mode == deviceMode || (a.Mode == "normal" && deviceMode == "monster"));
}
