using DeskBuddy.Companion.Protocol;

namespace DeskBuddy.Companion;

public enum ActivityKind
{
    Mode,   // stays on until another mode is chosen; the tile shows which one is on
    Action, // plays once
    Food,   // plays once, like an action; its own group, as on the device's menu (Food & Drink)
    Mood,   // Buddy's mood, independent from the mode; the tile shows which one is on
}

/// <summary>
/// Something Buddy can do, as a tile in the Activities section. Add an entry here when the firmware learns a new
/// command or activity; the window builds its tiles from this list and enables each one when the firmware's hello
/// lists <see cref="Cap"/> (and, for moods and activities, lists the name in <c>moods=</c> / <c>acts=</c>).
/// </summary>
/// <param name="Label">Tile text.</param>
/// <param name="Icon">Embedded pixel art (assets/buddy), without extension.</param>
/// <param name="Cap">Capability from hello that must be present.</param>
/// <param name="Command">What is sent (see docs/protocol.md).</param>
/// <param name="Mode">For modes: the device mode (status / evt mode) in which this tile shows as on.</param>
/// <param name="Reaction">Buddy's face in the window once the device has done it (face-&lt;reaction&gt;).</param>
/// <param name="Mood">For moods: the device mood (hello moods= / status / evt mood) this tile sets and shows as on.</param>
/// <param name="Act">For activities started with <c>act</c>: the name (hello acts=).</param>
public sealed record Activity(string Label, ActivityKind Kind, string Icon, string Cap, string Command,
                              string? Mode = null, string? Reaction = null, string? Mood = null, string? Act = null);

public static class Activities
{
    public const string DefaultFace = "face-neutral";

    public static readonly IReadOnlyList<Activity> All =
    [
        new("Buddy", ActivityKind.Mode, "face-neutral", "mode", "mode.set mode=normal", Mode: "normal"),
        new("Clock", ActivityKind.Mode, "moon", "mode", "mode.set mode=office", Mode: "office"), // the wire name stays
        new("Surprise!", ActivityKind.Action, "face-surprised", "poke", "poke", Reaction: "surprised"),
        ActTile("Sneeze", ActivityKind.Action, "face-sneeze", "sneeze"),
        ActTile("Stare", ActivityKind.Action, "face-stare", "stare"),
        ActTile("Meditate", ActivityKind.Action, "face-meditate", "meditate"),
        ActTile("Focus", ActivityKind.Action, "face-focus", "focus"),
        new("Monster", ActivityKind.Food, "can", "monster", "monster", Reaction: "happy"),
        ActTile("Water", ActivityKind.Food, "glass", "water"),
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

    // An activity: `act name=<act>`; afterwards the window shows the activity's own face (face-<act>).
    private static Activity ActTile(string label, ActivityKind kind, string icon, string act) =>
        new(label, kind, icon, "act", $"act name={act}", Reaction: act, Act: act);

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

    /// <summary>
    /// Whether the connected firmware can do it: hello lists its cap (and, for a mood or an activity, its name).
    /// </summary>
    public static bool IsAvailable(Activity a, IReadOnlySet<string> caps, IReadOnlyList<string> moods,
                                   IReadOnlyList<string> acts) =>
        caps.Contains(a.Cap) && (a.Mood is not { } mood || moods.Contains(mood)) && (a.Act is not { } act || acts.Contains(act));

    /// <summary>Buddy's face in the window for a device mood (neutral for a mood the companion does not know).</summary>
    public static string FaceFor(string deviceMood) =>
        All.FirstOrDefault(a => a.Kind == ActivityKind.Mood && a.Mood == deviceMood)?.Icon ?? DefaultFace;
}
