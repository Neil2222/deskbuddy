using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;

namespace DeskBuddy.Firmware;

/// <summary>
/// Makes a package from an ESP-IDF build (the packaging tool, tools/package-firmware.ps1). The build is taken as it
/// is; the manifest is derived from the images themselves (version and ESP-IDF from the app description) and then
/// put through every check <see cref="FirmwarePackage.Open"/> makes.
/// </summary>
public static class PackageBuilder
{
    /// <summary>
    /// The images an ESP-IDF build directory flashes (flasher_args.json), by package file name. The build must flash
    /// exactly the board's regions.
    /// </summary>
    public static Dictionary<string, byte[]> ReadBuild(string buildDir, BoardProfile profile)
    {
        var argsPath = Path.Combine(buildDir, "flasher_args.json");
        using var doc = JsonDocument.Parse(File.ReadAllBytes(argsPath));
        var flashFiles = doc.RootElement.GetProperty("flash_files");
        var byOffset = new Dictionary<uint, string>();
        foreach (var p in flashFiles.EnumerateObject())
        {
            if (!p.Name.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ||
                !uint.TryParse(p.Name.AsSpan(2), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var offset))
            {
                throw new FormatException($"{argsPath}: offset '{p.Name}' is not hex");
            }
            byOffset[offset] = p.Value.GetString() ?? throw new FormatException($"{argsPath}: no file at {p.Name}");
        }
        if (byOffset.Count != profile.Images.Count || profile.Images.Any(s => !byOffset.ContainsKey(s.Offset)))
        {
            throw new FirmwarePackageException(PackageError.LayoutMismatch,
                $"the build flashes {string.Join(", ", byOffset.Keys.Order().Select(FirmwareManifest.ToHex))}, " +
                $"the board takes {string.Join(", ", profile.Images.Select(s => FirmwareManifest.ToHex(s.Offset)))}");
        }
        return profile.Images.ToDictionary(s => s.File, s => File.ReadAllBytes(Path.Combine(buildDir, byOffset[s.Offset])));
    }

    public static FirmwareManifest CreateManifest(BoardProfile profile, IReadOnlyDictionary<string, byte[]> images,
        string build, int protocol, FirmwareVersion minCompanion)
    {
        var appSlot = profile.Images.Single(s => s.IsApp);
        AppDescription app;
        try
        {
            app = EspImage.Parse(images[appSlot.File], expectApp: true).App!;
        }
        catch (FormatException e)
        {
            throw new FirmwarePackageException(PackageError.ImageInvalid, $"{appSlot.File}: {e.Message}");
        }
        var version = FirmwareVersion.TryParse(app.Version) ??
            throw new FirmwarePackageException(PackageError.BuildMismatch,
                $"the application's version '{app.Version}' is not a release version (firmware/deskbuddy/version.txt)");
        var manifest = new FirmwareManifest(
            FirmwareManifest.CurrentSchema, FirmwareManifest.ProductName, version, build, profile.Board, profile.Chip,
            profile.FlashSize, profile.FlashMode, profile.FlashFreq, protocol, minCompanion, app.IdfVersion,
            profile.Images.Select(s => new ManifestImage(s.Name, s.Offset, s.File, images[s.File].Length,
                Convert.ToHexStringLower(SHA256.HashData(images[s.File])))).ToList(),
            profile.Preserved);
        // Through the parser too: what is written is what a Companion will read.
        var reread = FirmwareManifest.Parse(manifest.ToJson());
        FirmwarePackage.Check(reread, images, extraEntries: []);
        return reread;
    }
}
