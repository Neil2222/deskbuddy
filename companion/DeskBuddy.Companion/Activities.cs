using DeskBuddy.Companion.Protocol;

namespace DeskBuddy.Companion;

public enum ActivityKind
{
    Mode,   // stays on until another mode is chosen; the tile shows which one is on
    Action, // plays once
    Mood,   // Buddy's mood, independent from the mode; the tile shows which one is on
}

/// <summary>
/// Something Buddy can do, as a tile in the Activities section. Add an entry here when the firmware learns a new
/// command; the window builds its tiles from this list and enables each one when the firmware's hello lists
/// <see cref="Cap"/> (and, for moods, lists the mood in <c>moods=</c>).
/// </summary>
/// <param name="Label">Tile text.</param>
/// <param name="Icon">Embedded pixel art (assets/buddy), without extension.</param>
/// <param name="Cap">Capability from hello that must be present.</param>
/// <param name="Command">What is sent (see docs/protocol.md).</param>
/// <param name="Mode">For modes: the device mode (status / evt mode) in which this tile shows as on.</param>
/// <param name="Reaction">Buddy's face in the window once the device has done it.</param>
/// <param name="Mood">For moods: the device mood (hello moods= / status / evt mood) this tile sets and shows as on.</param>
public sealed record Activity(string Label, ActivityKind Kind, string Icon, string Cap, string Command,
                              string? Mode = null, string? Reaction = null, string? Mood = null);

public static class Activities
{
    public const string DefaultFace = "face-neutral";

    public static readonly IReadOnlyList<Activity> All =
    [
        new("Buddy", ActivityKind.Mode, "face-neutral", "mode", "mode.set mode=normal", Mode: "normal"),
        new("Clock", ActivityKind.Mode, "moon", "mode", "mode.set mode=office", Mode: "office"), // the wire name stays
        new("Surprise!", ActivityKind.Action, "face-surprised", "poke", "poke", Reaction: "surprised"),
        new("Monster", ActivityKind.Action, "can", "monster", "monster", Reaction: "happy"),
        MoodTile("Idle", "face-neutral", DeviceMood.Idle),
        MoodTile("Relaxed", "face-relaxed", "relaxed"),
        MoodTile("Peukie", "face-peukie", "peukie"),
        MoodTile("Angry", "face-angry", "angry"),
        MoodTile("Sleepy", "face-sleepy", "sleepy"),
        MoodTile("Working", "face-working", "working"),
        MoodTile("Happy", "face-happy", "happy"),
    ];

    private static Activity MoodTile(string label, string icon, string mood) =>
        new(label, ActivityKind.Mood, icon, "mood", DeviceMood.Command(mood), Mood: mood);

    /// <summary>
    /// Whether a mode or mood tile is on for the device's current mode and mood (a Monster sequence happens in Buddy
    /// mode).
    /// </summary>
    public static bool IsOn(Activity a, string deviceMode, string deviceMood) => a.Kind switch
    {
        ActivityKind.Mode => a.Mode == deviceMode || (a.Mode == "normal" && deviceMode == "monster"),
        ActivityKind.Mood => a.Mood == deviceMood,
        _ => false,
    };

    /// <summary>Whether the connected firmware can do it: hello lists its cap (and, for a mood, the mood).</summary>
    public static bool IsAvailable(Activity a, IReadOnlySet<string> caps, IReadOnlyList<string> moods) =>
        caps.Contains(a.Cap) && (a.Kind != ActivityKind.Mood || (a.Mood is { } mood && moods.Contains(mood)));

    /// <summary>Buddy's face in the window for a device mood (neutral for a mood the companion does not know).</summary>
    public static string FaceFor(string deviceMood) =>
        All.FirstOrDefault(a => a.Kind == ActivityKind.Mood && a.Mood == deviceMood)?.Icon ?? DefaultFace;
}
