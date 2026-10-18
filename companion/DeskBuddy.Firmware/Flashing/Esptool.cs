using System.Globalization;

namespace DeskBuddy.Firmware.Flashing;

/// <summary>How esptool gets the chip into download mode before a command, and what it does after.</summary>
public enum EntryMode
{
    /// <summary>DTR/RTS on the CH343 UART port reset the chip into its ROM bootloader (no buttons).</summary>
    AutoReset,
    /// <summary>The user holds BOOT, presses RESET and releases BOOT first; esptool does not touch DTR/RTS.</summary>
    Manual,
}

public sealed record FlashRegion(uint Offset, string File);

public sealed record EsptoolResult(EsptoolFailure Failure, int ExitCode, string Message, EsptoolOutput Output)
{
    public bool Ok => Failure == EsptoolFailure.None;
}

/// <summary>
/// esptool v4.12.0, run as a separate program (it is GPL-2.0-or-later and stays one; see
/// companion/third_party/esptool). One method per operation the Firmware Manager needs, with fixed arguments: no
/// erase command, no --erase-all, no eFuse tool, and images are written as they are (flash mode, frequency and size
/// "keep"). Every run is checked as it goes: if the chip or the MAC it reports is not the expected board, the run is
/// stopped at once, before esptool gets to its command.
/// </summary>
public sealed class Esptool(IProcessRunner runner, string exePath, string workingDirectory, BoardProfile profile)
{
    public const string Version = "4.12.0";
    public BoardProfile Profile => profile;
    public const int TransferBaud = 921600; // what tools/flash-backup.ps1 uses on the CH343
    public const int IdentifyBaud = 115200;

    /// <summary>The commands this class ever runs; everything else (erase_flash, erase_region, write_flash_status...) never.</summary>
    public static readonly IReadOnlySet<string> Commands = new HashSet<string> { "flash_id", "read_flash", "write_flash", "verify_flash", "read_mac" };

    /// <summary>
    /// flash_id: the chip, features, crystal, MAC and the flash chip's ID and size. Read-only, but the automatic
    /// entry resets the board (whatever it was doing stops; it restarts afterwards).
    /// </summary>
    public async Task<(EsptoolResult Result, BoardIdentity? Identity)> IdentifyAsync(string port, EntryMode entry, bool resetAfter,
        CancellationToken ct)
    {
        var result = await RunAsync(Arguments(port, IdentifyBaud, entry, resetAfter, "flash_id"), null, null, ct).ConfigureAwait(false);
        var identity = BoardIdentity.From(result.Output, profile);
        if (result.Ok && identity is null)
        {
            result = result with { Failure = EsptoolFailure.ToolFailed, Message = "esptool reported no MAC address" };
        }
        return (result, identity);
    }

    public Task<EsptoolResult> ReadFlashAsync(string port, EntryMode entry, uint offset, uint size, string file, string expectedMac,
        bool resetAfter, IProgress<EsptoolProgress>? progress, CancellationToken ct) =>
        RunAsync(Arguments(port, TransferBaud, entry, resetAfter, "read_flash", Hex(offset), Hex(size), file), expectedMac, progress, ct);

    /// <summary>
    /// write_flash of exactly these regions. esptool erases only the 4 KB sectors each file covers, writes it
    /// compressed, and checks every region's MD5 on the chip afterwards ("Hash of data verified.").
    /// </summary>
    public async Task<EsptoolResult> WriteFlashAsync(string port, EntryMode entry, IReadOnlyList<FlashRegion> regions, string expectedMac,
        bool resetAfter, IProgress<EsptoolProgress>? progress, CancellationToken ct)
    {
        var result = await RunAsync(Arguments(port, TransferBaud, entry, resetAfter,
            ["write_flash", .. KeepFlashParams, .. Pairs(regions)]), expectedMac, progress, ct).ConfigureAwait(false);
        if (result.Ok && result.Output.HashesVerified != regions.Count)
        {
            return result with
            {
                Failure = EsptoolFailure.VerifyMismatch,
                Message = $"esptool confirmed {result.Output.HashesVerified} of {regions.Count} written regions",
            };
        }
        return result;
    }

    /// <summary>verify_flash: compares the flash with the files by MD5 on the chip; reads only.</summary>
    public async Task<EsptoolResult> VerifyFlashAsync(string port, EntryMode entry, IReadOnlyList<FlashRegion> regions, string expectedMac,
        bool resetAfter, CancellationToken ct)
    {
        var result = await RunAsync(Arguments(port, TransferBaud, entry, resetAfter,
            ["verify_flash", .. KeepFlashParams, .. Pairs(regions)]), expectedMac, null, ct).ConfigureAwait(false);
        if (result.Ok && result.Output.VerifiedOk != regions.Count)
        {
            return result with
            {
                Failure = EsptoolFailure.VerifyMismatch,
                Message = $"{result.Output.VerifiedOk} of {regions.Count} regions verified",
            };
        }
        return result;
    }

    /// <summary>Brings the board out of download mode into its app: read_mac (read-only) with a hard reset after.</summary>
    public Task<EsptoolResult> ResetAsync(string port, string? expectedMac, CancellationToken ct) =>
        RunAsync(Arguments(port, IdentifyBaud, EntryMode.AutoReset, resetAfter: true, "read_mac"), expectedMac, null, ct);

    private static readonly string[] KeepFlashParams = ["--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"];

    private static IEnumerable<string> Pairs(IReadOnlyList<FlashRegion> regions) => regions.SelectMany(r => new[] { Hex(r.Offset), r.File });

    private static string Hex(uint v) => "0x" + v.ToString("x", CultureInfo.InvariantCulture);

    /// <summary>The full argument list for one run (public for the tests, which check it never erases anything).</summary>
    public static IReadOnlyList<string> Arguments(string port, int baud, EntryMode entry, bool resetAfter, params string[] command)
    {
        if (command.Length == 0 || !Commands.Contains(command[0]))
        {
            throw new ArgumentException($"esptool command '{command.FirstOrDefault()}' is not one the Firmware Manager runs");
        }
        if (command.Any(a => a.StartsWith('-') && a.Contains("erase", StringComparison.OrdinalIgnoreCase)))
        {
            throw new ArgumentException("the Firmware Manager never erases");
        }
        return
        [
            "--chip", "esp32s3", "--port", port, "--baud", baud.ToString(CultureInfo.InvariantCulture),
            "--before", entry == EntryMode.AutoReset ? "default_reset" : "no_reset",
            "--after", resetAfter ? "hard_reset" : "no_reset",
            .. command,
        ];
    }

    private async Task<EsptoolResult> RunAsync(IReadOnlyList<string> args, string? expectedMac, IProgress<EsptoolProgress>? progress,
        CancellationToken ct)
    {
        var output = new EsptoolOutput();
        string? guardFailure = null;
        var guardKind = EsptoolFailure.None;
        IRunningProcess? process = null;
        var gate = new object();

        void Stop(EsptoolFailure kind, string why)
        {
            lock (gate)
            {
                if (guardKind != EsptoolFailure.None)
                {
                    return;
                }
                guardKind = kind;
                guardFailure = why;
                process?.Kill();
            }
        }

        void OnLine(string line)
        {
            lock (gate)
            {
                var p = output.Feed(line);
                if (p is not null)
                {
                    progress?.Report(p);
                }
                // The chip and the MAC are printed right after connecting, before esptool loads its stub and long
                // before it reads or writes flash: a different board is stopped here.
                if (line.StartsWith("Features: ", StringComparison.Ordinal) || line.StartsWith("Crystal is ", StringComparison.Ordinal))
                {
                    var wrong = BoardIdentity.ChipMismatches(output, profile).ToList();
                    if (wrong.Count > 0)
                    {
                        Stop(EsptoolFailure.WrongChip, string.Join("; ", wrong));
                    }
                }
                if (expectedMac is not null && output.Mac is { } mac &&
                    BoardIdentity.NormalizeMac(mac) != BoardIdentity.NormalizeMac(expectedMac))
                {
                    Stop(EsptoolFailure.WrongMac, $"the board on the port has MAC {mac}, not {expectedMac}");
                }
            }
        }

        if (!File.Exists(exePath))
        {
            return new EsptoolResult(EsptoolFailure.ToolMissing, -1, $"esptool not found at {exePath}", output);
        }
        Directory.CreateDirectory(workingDirectory); // an empty directory of its own: no esptool.cfg / setup.cfg is picked up
        try
        {
            lock (gate)
            {
                process = runner.Start(new ProcessSpec(exePath, args, workingDirectory), OnLine);
            }
        }
        catch (Exception e) when (e is System.ComponentModel.Win32Exception or IOException or UnauthorizedAccessException)
        {
            return new EsptoolResult(EsptoolFailure.ToolMissing, -1, $"esptool could not be started: {e.Message}", output);
        }
        using (process)
        {
            lock (gate)
            {
                if (guardKind != EsptoolFailure.None)
                {
                    process.Kill(); // a guard fired before Start returned
                }
            }
            int exit;
            using (ct.Register(() => Stop(EsptoolFailure.Cancelled, "cancelled")))
            {
                exit = await process.Exited.ConfigureAwait(false);
            }
            lock (gate)
            {
                if (guardKind != EsptoolFailure.None)
                {
                    return new EsptoolResult(guardKind, exit, guardFailure!, output);
                }
                var failure = output.Classify(exit);
                return new EsptoolResult(failure, exit, failure == EsptoolFailure.None ? "ok" : output.Explanation, output);
            }
        }
    }
}
