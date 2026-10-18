namespace DeskBuddy.Firmware.Flashing;

/// <summary>
/// What esptool's flash_id said about a board in download mode, and how far that matches a board profile. This is
/// conservative on purpose: an ESP32-S3 with 8 MB embedded PSRAM and 16 MB flash is what the 7B has, but so do other
/// Waveshare boards with the same module. <see cref="ModelVerified"/> is therefore always false: the exact model
/// is confirmed by the user (or, for a DeskBuddy 1.0.0 or newer, reported by the firmware's own check).
/// </summary>
public sealed record BoardIdentity(
    string Mac,
    string? Chip,
    string? Features,
    string? CrystalMHz,
    string? FlashManufacturer,
    string? FlashDevice,
    string? FlashSize,
    IReadOnlyList<string> Mismatches)
{
    public bool ChipMatches => Mismatches.Count == 0;
    public bool ModelVerified => false;

    /// <summary>The colon-free lower-case MAC, as DeskBuddy reports it in hello dev=.</summary>
    public string DeviceId => NormalizeMac(Mac);

    public string Summary =>
        $"{Chip ?? "unknown chip"}; {Features ?? "no features"}; flash {FlashSize ?? "?"} " +
        $"(manufacturer {FlashManufacturer ?? "?"}, device {FlashDevice ?? "?"}); crystal {CrystalMHz ?? "?"} MHz; MAC {Mac}";

    public static string NormalizeMac(string mac) => mac.Replace(":", "", StringComparison.Ordinal).ToLowerInvariant();

    /// <summary>From one flash_id run; null when it printed no MAC (it did not identify a chip).</summary>
    public static BoardIdentity? From(EsptoolOutput o, BoardProfile profile)
    {
        if (o.Mac is null)
        {
            return null;
        }
        var mismatches = ChipMismatches(o, profile).ToList();
        if (o.DetectedFlashSize != profile.FlashSize)
        {
            mismatches.Add($"flash {o.DetectedFlashSize ?? "not detected"}, not {profile.FlashSize}");
        }
        return new BoardIdentity(o.Mac, o.Chip, o.Features, o.CrystalMHz, o.FlashManufacturer, o.FlashDevice,
            o.DetectedFlashSize, mismatches);
    }

    /// <summary>
    /// What esptool prints when it connects (any command): the chip, its features and crystal. Checked in every run,
    /// so a different board on the port is caught before anything is read or written.
    /// </summary>
    public static IEnumerable<string> ChipMismatches(EsptoolOutput o, BoardProfile profile)
    {
        if (o.Chip is not { } chip || !chip.StartsWith(profile.EsptoolChipPrefix, StringComparison.Ordinal))
        {
            yield return $"chip {o.Chip ?? "unknown"}, not an {profile.EsptoolChipPrefix.Trim()}";
        }
        if (o.Features is not { } f || !f.Split(", ").Any(x => x.StartsWith(profile.PsramFeature, StringComparison.Ordinal)))
        {
            yield return $"features '{o.Features ?? "unknown"}' do not include {profile.PsramFeature}";
        }
        if (o.CrystalMHz is { } x && x != profile.CrystalMHz)
        {
            yield return $"crystal {x} MHz, not {profile.CrystalMHz} MHz";
        }
    }
}
