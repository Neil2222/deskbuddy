using System.Globalization;

namespace DeskBuddy.Firmware;

/// <summary>
/// A firmware version: Semantic Versioning 2.0.0 without build metadata (the build is the Git commit, reported
/// separately as <c>info build=</c>). MAJOR.MINOR.PATCH with an optional pre-release ("1.1.0-rc.1"), compared by
/// SemVer precedence: a pre-release sorts before its release.
/// </summary>
public sealed class FirmwareVersion : IComparable<FirmwareVersion>, IEquatable<FirmwareVersion>
{
    public int Major { get; }
    public int Minor { get; }
    public int Patch { get; }
    /// <summary>The dot-separated pre-release identifiers; empty for a release.</summary>
    public IReadOnlyList<string> PreRelease { get; }

    private FirmwareVersion(int major, int minor, int patch, IReadOnlyList<string> preRelease)
    {
        Major = major;
        Minor = minor;
        Patch = patch;
        PreRelease = preRelease;
    }

    public bool IsPreRelease => PreRelease.Count > 0;

    /// <summary>Null unless <paramref name="text"/> is exactly a version (no "v", no spaces, no build metadata).</summary>
    public static FirmwareVersion? TryParse(string? text)
    {
        if (string.IsNullOrEmpty(text) || text.Length > 31) // the ESP-IDF app description holds 31 characters
        {
            return null;
        }
        var dash = text.IndexOf('-');
        var core = (dash < 0 ? text : text[..dash]).Split('.');
        if (core.Length != 3 || !TryNumber(core[0], out var major) || !TryNumber(core[1], out var minor) ||
            !TryNumber(core[2], out var patch))
        {
            return null;
        }
        string[] pre = [];
        if (dash >= 0)
        {
            pre = text[(dash + 1)..].Split('.');
            foreach (var id in pre)
            {
                if (id.Length == 0 || !id.All(c => char.IsAsciiLetterOrDigit(c) || c == '-') ||
                    (id.All(char.IsAsciiDigit) && !TryNumber(id, out _)))
                {
                    return null;
                }
            }
        }
        return new FirmwareVersion(major, minor, patch, pre);
    }

    public static FirmwareVersion Parse(string text) =>
        TryParse(text) ?? throw new FormatException($"'{text}' is not a version (MAJOR.MINOR.PATCH[-pre-release])");

    // Digits only, no leading zero (SemVer), and small enough for an int.
    private static bool TryNumber(string s, out int value)
    {
        value = 0;
        return s.Length > 0 && s.All(char.IsAsciiDigit) && (s.Length == 1 || s[0] != '0') &&
               int.TryParse(s, NumberStyles.None, CultureInfo.InvariantCulture, out value);
    }

    public int CompareTo(FirmwareVersion? other)
    {
        if (other is null)
        {
            return 1;
        }
        var c = Major.CompareTo(other.Major);
        if (c == 0) c = Minor.CompareTo(other.Minor);
        if (c == 0) c = Patch.CompareTo(other.Patch);
        if (c != 0)
        {
            return c;
        }
        if (PreRelease.Count == 0 || other.PreRelease.Count == 0)
        {
            return other.PreRelease.Count.CompareTo(PreRelease.Count); // the release is the higher one
        }
        for (var i = 0; i < Math.Min(PreRelease.Count, other.PreRelease.Count); i++)
        {
            var a = PreRelease[i];
            var b = other.PreRelease[i];
            var aNum = a.All(char.IsAsciiDigit);
            var bNum = b.All(char.IsAsciiDigit);
            c = aNum && bNum ? int.Parse(a, CultureInfo.InvariantCulture).CompareTo(int.Parse(b, CultureInfo.InvariantCulture))
                : aNum ? -1 // numeric identifiers sort before alphanumeric ones
                : bNum ? 1
                : string.CompareOrdinal(a, b);
            if (c != 0)
            {
                return Math.Sign(c);
            }
        }
        return PreRelease.Count.CompareTo(other.PreRelease.Count);
    }

    public bool Equals(FirmwareVersion? other) => other is not null && CompareTo(other) == 0;
    public override bool Equals(object? obj) => Equals(obj as FirmwareVersion);
    public override int GetHashCode() => HashCode.Combine(Major, Minor, Patch, string.Join('.', PreRelease));
    public override string ToString() =>
        PreRelease.Count == 0 ? $"{Major}.{Minor}.{Patch}" : $"{Major}.{Minor}.{Patch}-{string.Join('.', PreRelease)}";

    public static bool operator <(FirmwareVersion a, FirmwareVersion b) => a.CompareTo(b) < 0;
    public static bool operator >(FirmwareVersion a, FirmwareVersion b) => a.CompareTo(b) > 0;
    public static bool operator <=(FirmwareVersion a, FirmwareVersion b) => a.CompareTo(b) <= 0;
    public static bool operator >=(FirmwareVersion a, FirmwareVersion b) => a.CompareTo(b) >= 0;
}
