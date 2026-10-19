using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Companion.Schedule;

namespace DeskBuddy.Companion.Pdc;

/// <summary>
/// What the PDC Manager integration keeps on disk, in its own folder (%APPDATA%\DeskBuddy by default):
///   settings.json     {"pdcEnabled": bool, "pdcUsername": "...", "clockFace": "...", "clockFaceDev": "...", "clockFaceBoot": "...",
///                      "scheduleMood": "...", "scheduleMoodDev": "...", "scheduleMoodBoot": "...",
///                      "trayEnabled": bool, "trayNoticeShown": bool, "usageEnabled": bool}
///                     (other keys in the file are kept; the clock face keys are the last face DeskBuddy reported and its boot;
///                     the schedule mood keys the mood the schedule last put Buddy in, see ScheduleMoodTracker)
///   pdc-session.bin   the session cookies for one server, encrypted with DPAPI for the current Windows user
/// The password is never stored.
/// </summary>
public sealed class PdcStore
{
    private static readonly byte[] Entropy = Encoding.UTF8.GetBytes("DeskBuddy.PdcSession.v1");

    /// <summary>Points the companion at another data folder (development and tests), instead of %APPDATA%\DeskBuddy.</summary>
    public const string DataDirectoryVariable = "DESKBUDDY_DATA_DIR";

    public string Directory { get; }
    public string SettingsPath => Path.Combine(Directory, "settings.json");
    public string SessionPath => Path.Combine(Directory, "pdc-session.bin");

    public PdcStore(string directory) => Directory = directory;

    /// <summary>The companion's data folder: %DESKBUDDY_DATA_DIR% when set (development and tests), otherwise
    /// %APPDATA%\DeskBuddy.</summary>
    public static string DefaultDirectory => DataDirectoryOverride
        ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "DeskBuddy");

    /// <summary>The full path in DESKBUDDY_DATA_DIR; null when it is not set.</summary>
    public static string? DataDirectoryOverride =>
        Environment.GetEnvironmentVariable(DataDirectoryVariable) is { Length: > 0 } dir ? Path.GetFullPath(dir) : null;

    public (bool Enabled, string? Username) LoadSettings()
    {
        var root = ReadSettings();
        var enabled = root["pdcEnabled"] is JsonValue e && e.TryGetValue<bool>(out var on) && on;
        var user = root["pdcUsername"] is JsonValue u && u.TryGetValue<string>(out var name) ? name : null;
        return (enabled, string.IsNullOrWhiteSpace(user) ? null : user);
    }

    public void SaveSettings(bool enabled, string? username)
    {
        var root = ReadSettings();
        root["pdcEnabled"] = enabled;
        root["pdcUsername"] = username;
        WriteSettings(root);
    }

    /// <summary>The Clock face DeskBuddy last reported and the device boot it was seen in (keys clockFace, clockFaceDev, clockFaceBoot); null: none saved.</summary>
    public SavedClockFace? LoadClockFace()
    {
        var root = ReadSettings();
        return Text(root["clockFace"]) is { } face ? new SavedClockFace(face, Text(root["clockFaceDev"]), Text(root["clockFaceBoot"])) : null;
    }

    public void SaveClockFace(SavedClockFace saved)
    {
        var root = ReadSettings();
        root["clockFace"] = saved.Face;
        root["clockFaceDev"] = saved.Dev;
        root["clockFaceBoot"] = saved.Boot;
        WriteSettings(root);
    }

    /// <summary>The mood the schedule last put Buddy in (keys scheduleMood, scheduleMoodDev, scheduleMoodBoot); null: none.</summary>
    public ScheduleMoodMark? LoadScheduleMood()
    {
        var root = ReadSettings();
        return Text(root["scheduleMood"]) is { } mood && Text(root["scheduleMoodDev"]) is { } dev && Text(root["scheduleMoodBoot"]) is { } boot
            ? new ScheduleMoodMark(dev, boot, mood) : null;
    }

    public void SaveScheduleMood(ScheduleMoodMark? mark)
    {
        var root = ReadSettings();
        root["scheduleMood"] = mark?.Mood;
        root["scheduleMoodDev"] = mark?.Dev;
        root["scheduleMoodBoot"] = mark?.Boot;
        WriteSettings(root);
    }

    /// <summary>Tray mode (closing the window keeps the companion running in the notification area) and whether its
    /// one-time notice was shown (keys trayEnabled, trayNoticeShown); both off by default.</summary>
    public (bool Enabled, bool NoticeShown) LoadTray()
    {
        var root = ReadSettings();
        return (Flag(root["trayEnabled"]), Flag(root["trayNoticeShown"]));
    }

    public void SaveTray(bool enabled, bool noticeShown)
    {
        var root = ReadSettings();
        root["trayEnabled"] = enabled;
        root["trayNoticeShown"] = noticeShown;
        WriteSettings(root);
    }

    /// <summary>The Claude usage strip (key usageEnabled); off by default.</summary>
    public bool LoadUsageEnabled() => Flag(ReadSettings()["usageEnabled"]);

    public void SaveUsageEnabled(bool enabled)
    {
        var root = ReadSettings();
        root["usageEnabled"] = enabled;
        WriteSettings(root);
    }

    private static string? Text(JsonNode? n) => n is JsonValue v && v.TryGetValue<string>(out var s) && s.Length > 0 ? s : null;

    private static bool Flag(JsonNode? n) => n is JsonValue v && v.TryGetValue<bool>(out var on) && on;

    private void WriteSettings(JsonObject root)
    {
        System.IO.Directory.CreateDirectory(Directory);
        WriteAtomically(SettingsPath, Encoding.UTF8.GetBytes(root.ToJsonString(new JsonSerializerOptions { WriteIndented = true })));
    }

    private JsonObject ReadSettings()
    {
        try
        {
            return JsonNode.Parse(File.ReadAllText(SettingsPath)) as JsonObject ?? [];
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return []; // missing or damaged: defaults (integration off)
        }
    }

    /// <summary>The stored cookies for <paramref name="baseUri"/>; null when there are none. A session stored for
    /// another server (e.g. the local fake server) is deleted, never sent elsewhere.</summary>
    public Dictionary<string, string>? LoadSession(Uri baseUri)
    {
        byte[] sealedBytes;
        try
        {
            sealedBytes = File.ReadAllBytes(SessionPath);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return null;
        }
        try
        {
            var plain = ProtectedData.Unprotect(sealedBytes, Entropy, DataProtectionScope.CurrentUser);
            var stored = JsonSerializer.Deserialize<StoredSession>(plain);
            if (stored?.BaseUrl == Key(baseUri) && stored.Cookies is { Count: > 0 } cookies)
            {
                return new Dictionary<string, string>(cookies, StringComparer.Ordinal);
            }
        }
        catch (Exception e) when (e is CryptographicException or JsonException)
        {
        }
        DeleteSession(); // another server's, another user's, or damaged
        return null;
    }

    public void SaveSession(Uri baseUri, IReadOnlyDictionary<string, string> cookies)
    {
        var plain = JsonSerializer.SerializeToUtf8Bytes(new StoredSession(Key(baseUri), new Dictionary<string, string>(cookies)));
        System.IO.Directory.CreateDirectory(Directory);
        WriteAtomically(SessionPath, ProtectedData.Protect(plain, Entropy, DataProtectionScope.CurrentUser));
    }

    public void DeleteSession()
    {
        try
        {
            File.Delete(SessionPath);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }

    private static string Key(Uri baseUri) => baseUri.GetLeftPart(UriPartial.Authority).ToLowerInvariant();

    private static void WriteAtomically(string path, byte[] data)
    {
        var temp = path + ".tmp";
        File.WriteAllBytes(temp, data);
        File.Move(temp, path, overwrite: true);
    }

    private sealed record StoredSession(string BaseUrl, Dictionary<string, string> Cookies);
}
