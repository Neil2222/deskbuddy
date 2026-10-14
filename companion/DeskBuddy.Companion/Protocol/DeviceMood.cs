namespace DeskBuddy.Companion.Protocol;

/// <summary>
/// Buddy's mood as the protocol carries it (docs/protocol.md): <c>mood.set name=&lt;id&gt;</c>, the <c>moods=</c> list
/// in hello, <c>mood=</c> in status and in <c>evt mood</c>. Independent from the device mode.
/// </summary>
public static class DeviceMood
{
    /// <summary>The mood DeskBuddy starts in, and the one assumed until status says otherwise.</summary>
    public const string Idle = "idle";

    public static string Command(string mood) => $"mood.set name={mood}";

    /// <summary>The moods the firmware supports, in display order, from hello's <c>moods=</c> (absent: none).</summary>
    public static IReadOnlyList<string> ParseList(string? moods) =>
        (moods ?? "").Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    /// <summary>The mood from a status reply or an <c>evt mood</c>; <paramref name="current"/> when it has none.</summary>
    public static string From(Frame frame, string current) => frame.Arg("mood") is { Length: > 0 } mood ? mood : current;
}
