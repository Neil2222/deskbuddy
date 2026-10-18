using System.Security.Cryptography;

namespace DeskBuddy.Firmware;

public sealed record BootCheckResult(bool Compatible, string Reason, string? BootloaderSha256, BootloaderDescription? Description);

/// <summary>
/// Whether a board's own bootloader and partition table may stay when only the app is written (an Update). A
/// DeskBuddy that answers on the serial link only proves the app runs; it says nothing about the bootloader under it.
/// So the Companion reads the first 0x9000 bytes back from the board (read-only) and requires all of:
/// <list type="number">
/// <item>the partition table sector holds exactly this package's partition table;</item>
/// <item>a valid ESP image at 0x0 (checksum, SHA-256), for the board's chip and flash settings;</item>
/// <item>that image is this package's bootloader, or one the package's signed manifest names as compatible
/// (<see cref="FirmwareManifest.CompatibleBootloaders"/>), byte for byte by SHA-256.</item>
/// </list>
/// Anything else: no app-only update. The way on is a Repair (all three images), with its own confirmation.
/// </summary>
public static class BootloaderCheck
{
    public const uint BootloaderRegion = 0x8000;
    public const uint PartitionTableOffset = 0x8000;
    /// <summary>How much to read back: the bootloader region and the partition table sector (never the NVS after it).</summary>
    public const int ReadSize = 0x9000;

    /// <summary>The bootloader image at the start of <paramref name="flash"/> and its SHA-256; FormatException if invalid.</summary>
    public static (string Sha256, EspImage Image) Bootloader(ReadOnlySpan<byte> flash, BoardProfile profile)
    {
        var region = flash.Length > (int)BootloaderRegion ? flash[..(int)BootloaderRegion] : flash;
        var (image, length) = EspImage.ParsePrefix(region, expectApp: false);
        if (image.ChipId != profile.ChipId || image.FlashModeName != profile.FlashMode ||
            image.FlashSizeName != profile.FlashSize || image.FlashFreqName != profile.FlashFreq)
        {
            throw new FormatException(
                $"the bootloader is for chip {image.ChipId}, {image.FlashSizeName} {image.FlashModeName} {image.FlashFreqName}");
        }
        return (Convert.ToHexStringLower(SHA256.HashData(region[..length])), image);
    }

    public static BootCheckResult ForAppOnlyUpdate(ReadOnlySpan<byte> region, FirmwarePackage package)
    {
        if (region.Length < ReadSize)
        {
            return new(false, $"only {region.Length} bytes were read from the board", null, null);
        }
        var table = package.Images.Single(i => i.Slot.Name == "partition-table").Data;
        if (!region.Slice((int)PartitionTableOffset, table.Length).SequenceEqual(table))
        {
            return new(false, "the board's partition table is not this package's", null, null);
        }
        string sha;
        EspImage image;
        try
        {
            (sha, image) = Bootloader(region, package.Profile);
        }
        catch (FormatException e)
        {
            return new(false, $"the board's bootloader is not valid: {e.Message}", null, null);
        }
        var own = package.Manifest.Images.Single(i => i.Name == "bootloader").Sha256;
        if (sha == own)
        {
            return new(true, "the board has this package's bootloader", sha, image.Bootloader);
        }
        if (package.Manifest.CompatibleBootloaders.FirstOrDefault(b => b.Sha256 == sha) is { } known)
        {
            return new(true, $"the board's bootloader is listed as compatible: {known.Note}", sha, image.Bootloader);
        }
        var desc = image.Bootloader is { } d ? $"ESP-IDF {d.IdfVersion}{(d.DateTime.Length > 0 ? $", built {d.DateTime}" : "")}" : "no description";
        return new(false, $"the board's bootloader ({desc}, SHA-256 {sha[..12]}…) is not one this package lists as compatible",
            sha, image.Bootloader);
    }
}
