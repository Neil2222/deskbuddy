using System.Globalization;
using System.Text.RegularExpressions;

namespace DeskBuddy.Firmware.Flashing;

public enum EsptoolFailure
{
    None,
    ToolMissing,           // the bundled esptool.exe is not there or not the pinned file
    PortUnavailable,       // the COM port does not exist or another program has it open
    NoSerialData,          // the port opened but nothing answered (UART DIP switch #16, cable, wrong port)
    ConnectFailed,         // something answered, but not an ESP32 bootloader (needs BOOT + RESET)
    WrongBootMode,         // the chip did not enter download mode
    ChipStoppedResponding, // the connection broke off (cable, USB hub, power)
    WrongChip,             // not an ESP32-S3 with 8 MB embedded PSRAM
    WrongMac,              // another board than the one identified
    VerifyMismatch,        // flash differs from what was written
    ToolFailed,            // any other esptool error (its message is kept)
    Cancelled,
}

public sealed record EsptoolProgress(string Operation, uint Address, int Percent);

/// <summary>
/// What esptool v4.12.0 printed in one run, read line by line. The formats are esptool's own, from its source at
/// v4.12.0 (esptool/__init__.py, cmds.py, loader.py, targets/esp32s3.py); when output goes to a pipe it prints
/// progress as plain lines. Tests feed recorded output through here.
/// </summary>
public sealed partial class EsptoolOutput
{
    private readonly List<string> _lines = [];

    public IReadOnlyList<string> Lines => _lines;
    /// <summary>"ESP32-S3 (QFN56) (revision v0.2)".</summary>
    public string? Chip { get; private set; }
    /// <summary>"WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)".</summary>
    public string? Features { get; private set; }
    public string? CrystalMHz { get; private set; }
    /// <summary>Lower-case, colon-separated, as esptool prints it ("44:1b:f6:8d:11:78").</summary>
    public string? Mac { get; private set; }
    public string? FlashManufacturer { get; private set; }
    public string? FlashDevice { get; private set; }
    /// <summary>"16MB" (flash_id only).</summary>
    public string? DetectedFlashSize { get; private set; }
    public int HashesVerified { get; private set; }
    public int VerifiedOk { get; private set; }
    public int VerifyFailed { get; private set; }
    /// <summary>A write has begun (the first "Writing at" line): from here on the flash is being changed.</summary>
    public bool WriteStarted { get; private set; }
    public string? FatalError { get; private set; }

    /// <summary>Takes one line; returns the progress it reports, if it is a progress line.</summary>
    public EsptoolProgress? Feed(string line)
    {
        _lines.Add(line);
        var t = line.Trim();
        Match m;
        if ((m = WritingAt().Match(t)).Success)
        {
            WriteStarted = true;
            return new EsptoolProgress("write", uint.Parse(m.Groups[1].Value, NumberStyles.HexNumber, CultureInfo.InvariantCulture),
                int.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture));
        }
        if ((m = ReadProgress().Match(t)).Success)
        {
            return new EsptoolProgress("read", 0, (int)Math.Min(100, double.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture)));
        }
        if (t.StartsWith("Chip is ", StringComparison.Ordinal))
        {
            Chip = t["Chip is ".Length..];
        }
        else if (t.StartsWith("Features: ", StringComparison.Ordinal))
        {
            Features = t["Features: ".Length..];
        }
        else if ((m = Crystal().Match(t)).Success)
        {
            CrystalMHz = m.Groups[1].Value;
        }
        else if ((m = MacLine().Match(t)).Success)
        {
            Mac ??= m.Groups[1].Value.ToLowerInvariant(); // "MAC:" comes first; BASE MAC / MAC_EXT are other lines
        }
        else if (t.StartsWith("Manufacturer: ", StringComparison.Ordinal))
        {
            FlashManufacturer = t["Manufacturer: ".Length..];
        }
        else if (t.StartsWith("Device: ", StringComparison.Ordinal))
        {
            FlashDevice = t["Device: ".Length..];
        }
        else if (t.StartsWith("Detected flash size: ", StringComparison.Ordinal))
        {
            DetectedFlashSize = t["Detected flash size: ".Length..];
        }
        else if (t == "Hash of data verified.")
        {
            HashesVerified++;
        }
        else if (t == "-- verify OK (digest matched)")
        {
            VerifiedOk++;
        }
        else if (t.StartsWith("-- verify FAILED", StringComparison.Ordinal))
        {
            VerifyFailed++;
        }
        else if (t.StartsWith("A fatal error occurred: ", StringComparison.Ordinal))
        {
            FatalError = t["A fatal error occurred: ".Length..];
        }
        return null;
    }

    /// <summary>Why a run that exited with <paramref name="exitCode"/> failed (None when it did not).</summary>
    public EsptoolFailure Classify(int exitCode)
    {
        if (exitCode == 0 && VerifyFailed == 0)
        {
            return EsptoolFailure.None;
        }
        var all = string.Join("\n", _lines);
        bool Has(string s) => all.Contains(s, StringComparison.OrdinalIgnoreCase);
        if (VerifyFailed > 0)
        {
            return EsptoolFailure.VerifyMismatch;
        }
        if (Has("could not open port") || Has("PermissionError") || Has("Access is denied") || Has("FileNotFoundError"))
        {
            return EsptoolFailure.PortUnavailable;
        }
        if (Has("No serial data received"))
        {
            return EsptoolFailure.NoSerialData;
        }
        if (Has("Wrong boot mode detected"))
        {
            return EsptoolFailure.WrongBootMode;
        }
        if (Has("Failed to connect to"))
        {
            return EsptoolFailure.ConnectFailed;
        }
        if (Has("The chip stopped responding") || Has("Timed out waiting for packet") || Has("Serial data stream stopped") ||
            Has("Possible serial noise or corruption") || Has("ClearCommError failed") || Has("WriteFile failed"))
        {
            return EsptoolFailure.ChipStoppedResponding;
        }
        return EsptoolFailure.ToolFailed;
    }

    /// <summary>The last thing esptool said that explains a failure, for the log and the message.</summary>
    public string Explanation =>
        FatalError ?? _lines.LastOrDefault(l => l.Contains("error", StringComparison.OrdinalIgnoreCase)) ?? _lines.LastOrDefault() ?? "no output";

    [GeneratedRegex(@"^Writing at 0x([0-9a-fA-F]{8})\.\.\. \((\d{1,3}) %\)$")]
    private static partial Regex WritingAt();

    [GeneratedRegex(@"^(\d+) \((\d+(?:\.\d+)?) %\)$")]
    private static partial Regex ReadProgress();

    [GeneratedRegex(@"^Crystal is (\d+)MHz$")]
    private static partial Regex Crystal();

    [GeneratedRegex(@"^MAC: ((?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2})$")]
    private static partial Regex MacLine();
}
