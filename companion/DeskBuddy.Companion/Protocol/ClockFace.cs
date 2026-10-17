using System.Globalization;

namespace DeskBuddy.Companion.Protocol;

/// <summary>
/// Clock Mode faces as the protocol carries them (docs/protocol.md): <c>clock.face name=&lt;face&gt;</c>, the
/// <c>faces=</c> list in hello, <c>face=</c> in status and <c>evt face name= src=</c>. Needs the <c>face</c> cap.
/// </summary>
public static class ClockFace
{
    public const string Cap = "face";

    public static string Command(string face) => $"clock.face name={face}";

    /// <summary>The faces the firmware offers, in the device's display order, from hello's <c>faces=</c> (absent: none).</summary>
    public static IReadOnlyList<string> ParseList(string? faces) => DeviceMood.ParseList(faces);

    /// <summary>The face from a status reply (<c>face=</c>); null when it has none.</summary>
    public static string? FromStatus(Frame frame) => frame.Arg("face") is { Length: > 0 } face ? face : null;

    /// <summary>The face from an <c>evt face</c> (<c>name=</c>); null when it has none.</summary>
    public static string? FromEvent(Frame frame) => frame.Arg("name") is { Length: > 0 } face ? face : null;

    /// <summary>What the picker shows: a known face's name, else the wire name capitalised.</summary>
    public static string DisplayName(string face) => face switch
    {
        "classic" => "Classic",
        "skeleton" => "Mechanical Skeleton",
        "" => face,
        _ => CultureInfo.InvariantCulture.TextInfo.ToTitleCase(face.Replace('_', ' ').Replace('-', ' ')),
    };
}

/// <summary>The last face the device reported, and the device boot (hello's <c>dev</c> and <c>boot</c>) it was seen in.</summary>
public sealed record SavedClockFace(string Face, string? Dev, string? Boot);

/// <summary>
/// Decides when the Companion puts the remembered Clock face back. The device does not keep its face over a restart
/// (it starts as classic), so the face is re-sent once per device restart, never on an ordinary reconnect (that would
/// override a choice made on the touchscreen). No UI, no IO: the caller sends what is returned and stores
/// <see cref="Saved"/> whenever it changed.
/// </summary>
public sealed class ClockFacePolicy(SavedClockFace? saved)
{
    private string? _dev, _boot; // the connected device's current boot

    public SavedClockFace? Saved { get; private set; } = saved;

    /// <summary>
    /// Hello or <c>evt boot</c>: the device (offering <paramref name="offered"/>, with <paramref name="caps"/>) is at boot
    /// <paramref name="dev"/>/<paramref name="boot"/>. Returns the face to send now, or null. A face is sent when the
    /// device has the face cap, one is saved, the saved device boot is known and differs from this one (the device
    /// restarted since, so it lost the face), and the device offers it. Either way the current boot is recorded.
    /// </summary>
    public string? DeviceStarted(IReadOnlySet<string> caps, string? dev, string? boot, IReadOnlyList<string> offered)
    {
        if (!caps.Contains(ClockFace.Cap))
        {
            return null; // a device without faces: nothing to send, nothing to remember
        }
        _dev = string.IsNullOrEmpty(dev) || string.IsNullOrEmpty(boot) ? null : dev;
        _boot = _dev is null ? null : boot;
        if (Saved is not { } now || _dev is null)
        {
            return null;
        }
        var restarted = now.Dev is not null && now.Boot is not null && (now.Dev != _dev || now.Boot != _boot);
        Saved = now with { Dev = _dev, Boot = _boot };
        return restarted && offered.Contains(now.Face) ? now.Face : null;
    }

    /// <summary>The device reported its face (status <c>face=</c>, <c>evt face</c>, or our own accepted command): remember it for the current boot.</summary>
    public void FaceReported(string face)
    {
        if (face.Length > 0)
        {
            Saved = new SavedClockFace(face, _dev, _boot);
        }
    }

    /// <summary>The connection closed: a face reported before the next hello belongs to no known boot.</summary>
    public void Disconnected() => _dev = _boot = null;
}
