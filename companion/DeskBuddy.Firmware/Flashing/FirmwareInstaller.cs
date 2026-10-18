using System.Security.Cryptography;
using System.Text;

namespace DeskBuddy.Firmware.Flashing;

/// <summary>
/// Install: bootloader, partition table and app, for a board without DeskBuddy.
/// Update: the app only, for a DeskBuddy whose bootloader and partition table are verified compatible first.
/// Repair: all three again, for any DeskBuddy (or a board an Install or Update left without one), confirmed twice.
/// </summary>
public enum FirmwareOperation { Install, Update, Repair }

/// <summary>What a DeskBuddy said after connecting: hello fw= dev= boot=, info build= board= hw= (null: not reported).</summary>
public sealed record DeviceReport(string Firmware, string? Build, string? DeviceId, string? Boot, string? Board, string? Hardware);

/// <summary>The Companion's side of the port, so the installer can have it alone and get DeskBuddy back afterwards.</summary>
public interface IDevicePort
{
    /// <summary>
    /// Takes the port for flashing: the Companion closes its connection and does not reconnect until the returned
    /// hold is disposed. Throws <see cref="PortBusyException"/> if another tool (ESP-IDF's flash, a backup) has it.
    /// </summary>
    Task<IDisposable> AcquireAsync(string port, CancellationToken ct);

    /// <summary>After the hold is gone: waits until DeskBuddy answers on the port (hello, and info when offered).</summary>
    Task<DeviceReport?> WaitForDeskBuddyAsync(string port, TimeSpan timeout, CancellationToken ct);
}

public sealed class PortBusyException(string message) : Exception(message);

/// <summary>Where a full backup goes (a new, empty folder), or the user's explicit decision to go without one.</summary>
public sealed record BackupChoice(string? Folder, bool SkipConfirmed)
{
    public static BackupChoice To(string folder) => new(folder, false);
    public static BackupChoice SkipConfirmedByUser { get; } = new(null, true);
}

/// <param name="Before">The DeskBuddy being updated or repaired (its hello and info); null for an Install.</param>
/// <param name="ModelConfirmed">The user confirmed the exact board model (always needed for Install and Repair).</param>
/// <param name="RepairConfirmed">Repair only: the second, explicit confirmation that all three images are rewritten.</param>
public sealed record InstallRequest(
    FirmwareOperation Operation,
    FirmwarePackage Package,
    string Port,
    EntryMode Entry,
    DeviceReport? Before,
    bool ModelConfirmed,
    BackupChoice Backup,
    bool RepairConfirmed = false);

public enum InstallStep { Preparing, Identifying, BackingUp, CheckingBootloader, Writing, Verifying, Reconnecting, Done }

public sealed record InstallProgress(InstallStep Step, int Percent, string Text);

public enum InstallFailure
{
    None,
    NotAllowed,             // the request itself is not allowed (missing confirmation, wrong operation for the board)
    ToolMissing,
    PortBusy,
    PortUnavailable,
    NoSerialData,
    ConnectFailed,
    ConnectionLost,
    WrongChip,
    WrongMac,
    BackupFailed,
    BootloaderIncompatible,
    AlreadyInstalled,       // an Install on a board whose app is already DeskBuddy (the Companion was not connected)
    WriteFailed,            // nothing was written
    WriteInterrupted,       // writing had begun: the board may not start until it is installed again
    VerifyFailed,
    NoReconnect,
    WrongFirmwareAfter,
    HardwareCheckFailed,
    Cancelled,
}

/// <param name="FlashChanged">Whether anything on the board's flash may have changed.</param>
public sealed record InstallResult(
    InstallFailure Failure,
    string Message,
    string Recovery,
    bool FlashChanged,
    string? BackupFile,
    BoardIdentity? Identity,
    DeviceReport? After)
{
    public bool Succeeded => Failure == InstallFailure.None;
}

/// <summary>The outcome of an identification (read-only); <see cref="Failure"/> None when the board is what it should be.</summary>
public sealed record IdentifyResult(InstallFailure Failure, string Message, string Recovery, BoardIdentity? Identity, DeviceReport? After)
{
    public bool Succeeded => Failure == InstallFailure.None;
}

/// <summary>
/// One install, update or repair, step by step (docs/firmware-packages.md, "Installing"). Nothing is written before
/// every check has passed: the package (already verified when it was opened), the confirmations, the board's chip and
/// MAC, the backup, and for an Update the board's own bootloader and partition table. Each esptool run checks the chip
/// and MAC again. Afterwards the flash is verified, the board restarts, and DeskBuddy must come back with exactly the
/// package's version and build.
/// </summary>
public sealed class FirmwareInstaller(Esptool esptool, IDevicePort port, string workFolder,
    Func<string, CancellationToken, Task<bool>>? manualBootPrompt = null, Action<string>? log = null)
{
    public static readonly TimeSpan ReconnectTimeout = TimeSpan.FromSeconds(45);

    /// <summary>
    /// Identification only: the board restarts into download mode, esptool reads the chip, its features, the MAC and
    /// the flash chip (flash_id) and restarts it into its app. Nothing is read from the flash and nothing is written.
    /// With a DeskBuddy connected before, it must come back as the same device with the same firmware.
    /// </summary>
    public async Task<IdentifyResult> IdentifyAsync(string portName, EntryMode entry, DeviceReport? before, CancellationToken ct)
    {
        IDisposable hold;
        try
        {
            hold = await port.AcquireAsync(portName, ct).ConfigureAwait(false);
        }
        catch (PortBusyException e)
        {
            return new IdentifyResult(InstallFailure.PortBusy, e.Message, "Close the other tool and try again.", null, null);
        }
        catch (OperationCanceledException)
        {
            return new IdentifyResult(InstallFailure.Cancelled, "Cancelled.", "", null, null);
        }
        EsptoolResult result;
        BoardIdentity? identity;
        using (hold)
        {
            if (entry == EntryMode.Manual && (manualBootPrompt is null ||
                !await manualBootPrompt("Hold BOOT, press and release RESET, then release BOOT, to identify the board", ct).ConfigureAwait(false)))
            {
                return new IdentifyResult(InstallFailure.Cancelled, "Cancelled.", "Nothing was read.", null, null);
            }
            (result, identity) = await esptool.IdentifyAsync(portName, entry, resetAfter: entry == EntryMode.AutoReset, ct).ConfigureAwait(false);
            Log(result);
        }
        if (!result.Ok)
        {
            var failed = FromEsptool(result, false, null, identity);
            return new IdentifyResult(failed.Failure, failed.Message, failed.Recovery, identity, null);
        }
        var problems = new List<string>(identity!.Mismatches);
        var kind = problems.Count > 0 ? InstallFailure.WrongChip : InstallFailure.None;
        if (before?.DeviceId is { } dev && dev != identity.DeviceId)
        {
            problems.Add($"MAC {identity.Mac} is not the connected DeskBuddy's device {dev}");
            kind = InstallFailure.WrongMac;
        }
        if (entry == EntryMode.Manual && manualBootPrompt is not null)
        {
            await manualBootPrompt("press RESET (without BOOT) so the board starts again", ct).ConfigureAwait(false);
        }
        DeviceReport? after = null;
        if (before is not null)
        {
            after = await port.WaitForDeskBuddyAsync(portName, ReconnectTimeout, ct).ConfigureAwait(false);
            if (after is null)
            {
                return new IdentifyResult(InstallFailure.NoReconnect,
                    $"Identified ({identity.Summary}), but DeskBuddy did not answer again within {ReconnectTimeout.TotalSeconds:0} s.",
                    "Nothing was written. Press RESET on the board, or unplug and reconnect it.", identity, null);
            }
            if (after.DeviceId != before.DeviceId || after.Firmware != before.Firmware)
            {
                problems.Add($"DeskBuddy came back as {after.Firmware} on device {after.DeviceId}");
                kind = kind == InstallFailure.None ? InstallFailure.WrongFirmwareAfter : kind;
            }
        }
        var summary = $"{identity.Summary}. This does not prove the exact board model.";
        return kind == InstallFailure.None
            ? new IdentifyResult(kind, summary + (after is null ? "" : $" DeskBuddy {after.Firmware} reconnected (new boot {after.Boot})."), "", identity, after)
            : new IdentifyResult(kind, $"{string.Join("; ", problems)}. {summary}", "Nothing was written.", identity, after);
    }

    public async Task<InstallResult> RunAsync(InstallRequest request, IProgress<InstallProgress>? progress, CancellationToken ct)
    {
        var package = request.Package;
        var profile = package.Profile;
        if (Refusal(request) is { } refused)
        {
            return Fail(InstallFailure.NotAllowed, refused, "Nothing was changed.", false, null, null);
        }

        progress?.Report(new(InstallStep.Preparing, 0, "Taking the serial port"));
        IDisposable hold;
        try
        {
            hold = await port.AcquireAsync(request.Port, ct).ConfigureAwait(false);
        }
        catch (PortBusyException e)
        {
            return Fail(InstallFailure.PortBusy, e.Message,
                "Close the other tool (ESP-IDF flash or monitor, a backup, a serial terminal) and try again. Nothing was changed.",
                false, null, null);
        }
        catch (OperationCanceledException)
        {
            return Fail(InstallFailure.Cancelled, "Cancelled.", "Nothing was changed.", false, null, null);
        }

        var run = Path.Combine(workFolder, "run-" + Guid.NewGuid().ToString("N")[..8]);
        BoardIdentity? identity = null;
        string? backupFile = null;
        var flashChanged = false;
        var connected = false;
        try
        {
            Directory.CreateDirectory(run);

            // 1. Which board is this, really?
            if (!await EnterBootModeAsync(request, "identify the board", ct).ConfigureAwait(false))
            {
                return Fail(InstallFailure.Cancelled, "Cancelled before anything was read or written.", "Nothing was changed.", false, null, null);
            }
            progress?.Report(new(InstallStep.Identifying, 0, "Identifying the board"));
            var (idResult, id) = await esptool.IdentifyAsync(request.Port, request.Entry, resetAfter: false, ct).ConfigureAwait(false);
            Log(idResult);
            connected = idResult.Output.Mac is not null;
            if (!idResult.Ok)
            {
                return FromEsptool(idResult, flashChanged: false, backupFile, id);
            }
            identity = id!;
            if (!identity.ChipMatches)
            {
                return Fail(InstallFailure.WrongChip,
                    $"This board is not a {profile.DisplayName}: {string.Join("; ", identity.Mismatches)}.",
                    "Nothing was changed. DeskBuddy only runs on that board.", false, null, identity);
            }
            if (request.Before?.DeviceId is { } expected && expected != identity.DeviceId)
            {
                return Fail(InstallFailure.WrongMac,
                    $"The board in download mode (MAC {identity.Mac}) is not the DeskBuddy that was connected ({expected}).",
                    "Nothing was changed. Connect only the board you want to update, and try again.", false, null, identity);
            }
            var mac = identity.Mac;

            // An Install rewrites the bootloader too, with no backup by default: never on a board that already runs
            // DeskBuddy just because the Companion was not connected to it. The app's description says (read-only).
            if (request.Operation == FirmwareOperation.Install)
            {
                if (!await EnterBootModeAsync(request, "check what the board runs", ct).ConfigureAwait(false))
                {
                    return Fail(InstallFailure.Cancelled, "Cancelled before anything was written.", "Nothing was changed.", false, null, identity);
                }
                var appFile = Path.Combine(run, "app-head.bin");
                var head = await esptool.ReadFlashAsync(request.Port, request.Entry, 0x10000, 0x1000, appFile, mac, resetAfter: false, null, ct)
                    .ConfigureAwait(false);
                Log(head);
                if (!head.Ok)
                {
                    return FromEsptool(head, false, null, identity);
                }
                if (InstalledApp(await File.ReadAllBytesAsync(appFile, ct).ConfigureAwait(false)) is { } installed)
                {
                    return Fail(InstallFailure.AlreadyInstalled,
                        $"This board already runs DeskBuddy {installed}.",
                        "Nothing was changed. Let the Companion connect to it, then use Update (or Repair).", false, null, identity);
                }
            }

            // 2. The backup, before anything is written.
            if (request.Backup.Folder is { } folder)
            {
                progress?.Report(new(InstallStep.BackingUp, 0, "Backing up the whole flash (16 MB, a few minutes)"));
                if (!await EnterBootModeAsync(request, "back up the flash", ct).ConfigureAwait(false))
                {
                    return Fail(InstallFailure.Cancelled, "Cancelled before anything was written.", "Nothing was changed.", false, null, identity);
                }
                var (backup, error) = await BackupAsync(request, folder, identity, profile, progress, ct).ConfigureAwait(false);
                if (backup is null)
                {
                    return error!;
                }
                backupFile = backup;
            }

            // 3. An Update keeps the board's bootloader and partition table: only if they are known to fit.
            if (request.Operation == FirmwareOperation.Update)
            {
                progress?.Report(new(InstallStep.CheckingBootloader, 0, "Checking the board's bootloader and partition table"));
                if (!await EnterBootModeAsync(request, "read the bootloader", ct).ConfigureAwait(false))
                {
                    return Fail(InstallFailure.Cancelled, "Cancelled before anything was written.", "Nothing was changed.", false, backupFile, identity);
                }
                var regionFile = Path.Combine(run, "boot-region.bin");
                var read = await esptool.ReadFlashAsync(request.Port, request.Entry, 0, BootloaderCheck.ReadSize, regionFile, mac,
                    resetAfter: false, null, ct).ConfigureAwait(false);
                Log(read);
                if (!read.Ok)
                {
                    return FromEsptool(read, false, backupFile, identity);
                }
                var check = BootloaderCheck.ForAppOnlyUpdate(await File.ReadAllBytesAsync(regionFile, ct).ConfigureAwait(false), package);
                log?.Invoke($"Bootloader check: {check.Reason}");
                if (!check.Compatible)
                {
                    return Fail(InstallFailure.BootloaderIncompatible,
                        $"An app-only update is not safe here: {check.Reason}.",
                        "Nothing was changed. Use Repair (full reinstall), which writes the bootloader, the partition table " +
                        "and the app together" + (backupFile is null ? ", after a backup." : $"; the backup is in {backupFile}."),
                        false, backupFile, identity);
                }
            }

            // 4. Write. The files come from the verified package, written out just for this run and checked again.
            var regions = new List<FlashRegion>();
            foreach (var image in package.Images.Where(i => request.Operation != FirmwareOperation.Update || i.Slot.IsApp))
            {
                var file = Path.Combine(run, image.Slot.File);
                await File.WriteAllBytesAsync(file, image.Data, ct).ConfigureAwait(false);
                var sha = package.Manifest.Images.Single(i => i.File == image.Slot.File).Sha256;
                if (Convert.ToHexStringLower(SHA256.HashData(await File.ReadAllBytesAsync(file, ct).ConfigureAwait(false))) != sha)
                {
                    return Fail(InstallFailure.WriteFailed, $"{image.Slot.File} changed on disk before it could be written.",
                        "Nothing was changed on the board. Check the PC (antivirus, disk) and try again.", false, backupFile, identity);
                }
                regions.Add(new FlashRegion(image.Slot.Offset, file));
            }
            var what = string.Join(", ", package.Images.Where(i => regions.Any(r => r.Offset == i.Slot.Offset))
                .Select(i => $"{i.Slot.Name} at {FirmwareManifest.ToHex(i.Slot.Offset)}"));
            progress?.Report(new(InstallStep.Writing, 0, $"Writing {what}"));
            if (!await EnterBootModeAsync(request, "write the firmware", ct).ConfigureAwait(false))
            {
                return Fail(InstallFailure.Cancelled, "Cancelled before anything was written.", "Nothing was changed.", false, backupFile, identity);
            }
            var total = regions.Sum(r => new FileInfo(r.File).Length);
            var write = await esptool.WriteFlashAsync(request.Port, request.Entry, regions, mac, resetAfter: false,
                new InlineProgress<EsptoolProgress>(p => progress?.Report(new(InstallStep.Writing,
                    WritePercent(regions, p, total), $"Writing {what}"))), ct).ConfigureAwait(false);
            Log(write);
            flashChanged = write.Output.WriteStarted;
            if (!write.Ok)
            {
                return FromEsptool(write, flashChanged, backupFile, identity);
            }
            flashChanged = true;

            // 5. Read it back, then let the board start.
            progress?.Report(new(InstallStep.Verifying, 0, "Verifying what was written"));
            if (!await EnterBootModeAsync(request, "verify", ct).ConfigureAwait(false))
            {
                return Fail(InstallFailure.Cancelled, "Cancelled after writing, before the read-back check.",
                    "The firmware was written and esptool confirmed every region. Press RESET on the board to start it.", true, backupFile, identity);
            }
            var verify = await esptool.VerifyFlashAsync(request.Port, request.Entry, regions, mac,
                resetAfter: request.Entry == EntryMode.AutoReset, ct).ConfigureAwait(false);
            Log(verify);
            if (!verify.Ok)
            {
                return FromEsptool(verify, true, backupFile, identity);
            }
            connected = false; // verify_flash ended with a hard reset (automatic entry)
        }
        catch (OperationCanceledException)
        {
            return Fail(InstallFailure.Cancelled, "Cancelled.", flashChanged ? Recovery(InstallFailure.WriteInterrupted, backupFile) : "Nothing was changed.",
                flashChanged, backupFile, identity);
        }
        catch (IOException e)
        {
            return Fail(flashChanged ? InstallFailure.WriteInterrupted : InstallFailure.WriteFailed, $"A file could not be read or written: {e.Message}",
                flashChanged ? Recovery(InstallFailure.WriteInterrupted, backupFile) : "Nothing was changed on the board.", flashChanged, backupFile, identity);
        }
        finally
        {
            // Back into its app if a step left it in download mode (best effort, never a write).
            if (connected && request.Entry == EntryMode.AutoReset)
            {
                var reset = await esptool.ResetAsync(request.Port, identity?.Mac, CancellationToken.None).ConfigureAwait(false);
                Log(reset);
            }
            hold.Dispose();
            TryDelete(run);
        }

        // 6. DeskBuddy must come back as exactly what was installed.
        if (request.Entry == EntryMode.Manual && manualBootPrompt is not null)
        {
            await manualBootPrompt("press RESET (without BOOT) so the board starts DeskBuddy", ct).ConfigureAwait(false);
        }
        progress?.Report(new(InstallStep.Reconnecting, 0, "Waiting for DeskBuddy to start"));
        var after = await port.WaitForDeskBuddyAsync(request.Port, ReconnectTimeout, ct).ConfigureAwait(false);
        if (after is null)
        {
            return Fail(InstallFailure.NoReconnect,
                $"The firmware was written and verified, but DeskBuddy did not answer within {ReconnectTimeout.TotalSeconds:0} s.",
                "Press RESET on the board, or unplug and reconnect it. If DeskBuddy still does not answer, use Repair" +
                (backupFile is null ? "." : $", or restore the backup in {backupFile} with tools\\flash-backup.ps1 -Action restore."),
                true, backupFile, identity);
        }
        var wrong = new List<string>();
        if (after.DeviceId is { } dev && dev != identity!.DeviceId) wrong.Add($"device {dev}, not {identity.DeviceId}");
        if (after.Firmware != package.Manifest.Version.ToString()) wrong.Add($"firmware {after.Firmware}, not {package.Manifest.Version}");
        if (after.Build != package.Manifest.Build) wrong.Add($"build {after.Build ?? "not reported"}, not {package.Manifest.Build[..7]}");
        if (after.Board != profile.Board) wrong.Add($"board {after.Board ?? "not reported"}");
        if (request.Before?.Boot is { } oldBoot && after.Boot == oldBoot) wrong.Add("it did not restart");
        if (wrong.Count > 0)
        {
            return Fail(InstallFailure.WrongFirmwareAfter, $"DeskBuddy answered, but not as the installed firmware: {string.Join("; ", wrong)}.",
                "Try Repair. If that does not help, restore the backup and report this.", true, backupFile, identity, after);
        }
        if (after.Hardware != "ok")
        {
            return Fail(InstallFailure.HardwareCheckFailed,
                $"DeskBuddy {after.Firmware} runs, but its hardware check reports '{after.Hardware}'.",
                "The board is probably not a " + profile.DisplayName + ". Restore its backup" +
                (backupFile is null ? "" : $" ({backupFile})") + " with tools\\flash-backup.ps1 -Action restore.", true, backupFile, identity, after);
        }
        progress?.Report(new(InstallStep.Done, 100, $"DeskBuddy {after.Firmware} is running"));
        return new InstallResult(InstallFailure.None, $"DeskBuddy {after.Firmware} (build {after.Build![..7]}) is installed and running.",
            "", true, backupFile, identity, after);
    }

    /// <summary>Why the request may not run at all; null if it may.</summary>
    public static string? Refusal(InstallRequest r)
    {
        if (r.Backup.Folder is null && !r.Backup.SkipConfirmed)
        {
            return "Choose a backup folder, or confirm that you go on without a backup.";
        }
        if (r.Before?.Board is { } board && board != r.Package.Profile.Board)
        {
            return $"This DeskBuddy runs on '{board}'; the package is for '{r.Package.Profile.Board}'.";
        }
        if (r.Before?.Hardware is { } hw && hw != "ok")
        {
            return $"This DeskBuddy's own hardware check reports '{hw}': it is not a board the package is for.";
        }
        var reportedSelf = r.Before is { Hardware: "ok" } b && b.Board == r.Package.Profile.Board;
        return r.Operation switch
        {
            FirmwareOperation.Install when r.Before is not null =>
                "DeskBuddy is already installed on this board: use Update or Repair.",
            FirmwareOperation.Install or FirmwareOperation.Repair when !r.ModelConfirmed =>
                $"Confirm that this is a {r.Package.Profile.DisplayName}: the chip alone does not prove the model.",
            FirmwareOperation.Repair when !r.RepairConfirmed => "A Repair rewrites the bootloader and the partition table: confirm it.",
            FirmwareOperation.Update when r.Before is null => "An Update needs a connected DeskBuddy; for a board without one, use Install.",
            FirmwareOperation.Update when !r.ModelConfirmed && !reportedSelf =>
                $"This DeskBuddy does not report its board: confirm that it is a {r.Package.Profile.DisplayName}.",
            _ => null,
        };
    }

    private async Task<(string? File, InstallResult? Error)> BackupAsync(InstallRequest request, string folder, BoardIdentity identity,
        BoardProfile profile, IProgress<InstallProgress>? progress, CancellationToken ct)
    {
        InstallResult Failed(string message) => Fail(InstallFailure.BackupFailed, $"The backup failed: {message}",
            "Nothing was written. Try again (another USB cable or port helps if the connection broke), or choose to go on " +
            "without a backup: that is your decision, the Companion never makes it.", false, null, identity);

        try
        {
            if (Directory.Exists(folder) && Directory.EnumerateFileSystemEntries(folder).Any())
            {
                return (null, Failed($"{folder} is not empty; a backup never overwrites anything"));
            }
            Directory.CreateDirectory(folder);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return (null, Failed($"{folder} cannot be created ({e.Message})"));
        }
        var file = Path.Combine(folder, $"deskbuddy_{identity.DeviceId}_full_{profile.FlashSize}.bin");
        var read = await esptool.ReadFlashAsync(request.Port, request.Entry, 0, profile.FlashBytes, file, identity.Mac, resetAfter: false,
            new InlineProgress<EsptoolProgress>(p => progress?.Report(new(InstallStep.BackingUp, p.Percent * 9 / 10,
                "Backing up the whole flash (16 MB, a few minutes)"))), ct).ConfigureAwait(false);
        Log(read);
        if (!read.Ok)
        {
            return (null, read.Failure == EsptoolFailure.Cancelled
                ? Fail(InstallFailure.Cancelled, "Cancelled during the backup.", "Nothing was written. The partial backup file is not usable.", false, null, identity)
                : Failed(Describe(read)));
        }
        if (!File.Exists(file) || new FileInfo(file).Length != profile.FlashBytes)
        {
            return (null, Failed($"the backup file is not {profile.FlashSize}"));
        }
        progress?.Report(new(InstallStep.BackingUp, 92, "Checking the backup against the board"));
        if (!await EnterBootModeAsync(request, "check the backup", ct).ConfigureAwait(false))
        {
            return (null, Fail(InstallFailure.Cancelled, "Cancelled before anything was written.", "Nothing was changed.", false, null, identity));
        }
        var check = await esptool.VerifyFlashAsync(request.Port, request.Entry, [new FlashRegion(0, file)], identity.Mac, resetAfter: false, ct)
            .ConfigureAwait(false);
        Log(check);
        if (!check.Ok)
        {
            return (null, Failed($"the backup does not match the board ({Describe(check)})"));
        }
        await using (var s = File.OpenRead(file))
        {
            var sha = Convert.ToHexStringLower(await SHA256.HashDataAsync(s, ct).ConfigureAwait(false));
            await File.WriteAllTextAsync(Path.Combine(folder, "SHA256SUMS.txt"), $"{sha}  {Path.GetFileName(file)}\n", Encoding.ASCII, ct)
                .ConfigureAwait(false);
        }
        await File.WriteAllTextAsync(Path.Combine(folder, "board.txt"),
            $"{identity.Summary}\nbefore: {request.Before?.Firmware ?? "no DeskBuddy"} build {request.Before?.Build ?? "-"}\n" +
            $"restore: tools\\flash-backup.ps1 -Action restore -Image \"{file}\"\n", Encoding.UTF8, ct).ConfigureAwait(false);
        return (file, null);
    }

    private async Task<bool> EnterBootModeAsync(InstallRequest request, string step, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        if (request.Entry == EntryMode.AutoReset)
        {
            return true;
        }
        return manualBootPrompt is not null && await manualBootPrompt(
            $"Hold BOOT, press and release RESET, then release BOOT, to {step}", ct).ConfigureAwait(false);
    }

    /// <summary>The version of the DeskBuddy app whose start is <paramref name="head"/> (flash at 0x10000), or null.</summary>
    internal static string? InstalledApp(ReadOnlySpan<byte> head)
    {
        // The image header (24 bytes), the first segment's header (8), then esp_app_desc_t.
        if (head.Length < 32 + 144 || head[0] != EspImage.Magic ||
            System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(head[32..]) != 0xABCD5432)
        {
            return null;
        }
        static string Text(ReadOnlySpan<byte> f)
        {
            var end = f.IndexOf((byte)0);
            return Encoding.ASCII.GetString(end < 0 ? f : f[..end]);
        }
        return Text(head.Slice(32 + 48, 32)) == FirmwareManifest.ProductName ? Text(head.Slice(32 + 16, 32)) : null;
    }

    private static int WritePercent(IReadOnlyList<FlashRegion> regions, EsptoolProgress p, long total)
    {
        // esptool reports a percentage per region: weigh it by the regions' sizes.
        long before = 0, size = 0;
        foreach (var r in regions.OrderBy(r => r.Offset))
        {
            var len = new FileInfo(r.File).Length;
            if (p.Address >= r.Offset && p.Address < r.Offset + Math.Max(len, 1))
            {
                size = len;
                break;
            }
            before += len;
        }
        return total == 0 ? 0 : (int)Math.Clamp((before + size * p.Percent / 100) * 100 / total, 0, 100);
    }

    private InstallResult FromEsptool(EsptoolResult r, bool flashChanged, string? backupFile, BoardIdentity? identity)
    {
        var kind = r.Failure switch
        {
            EsptoolFailure.ToolMissing => InstallFailure.ToolMissing,
            EsptoolFailure.PortUnavailable => InstallFailure.PortUnavailable,
            EsptoolFailure.NoSerialData => InstallFailure.NoSerialData,
            EsptoolFailure.ConnectFailed or EsptoolFailure.WrongBootMode => InstallFailure.ConnectFailed,
            EsptoolFailure.WrongChip => InstallFailure.WrongChip,
            EsptoolFailure.WrongMac => InstallFailure.WrongMac,
            EsptoolFailure.VerifyMismatch => InstallFailure.VerifyFailed,
            EsptoolFailure.Cancelled => InstallFailure.Cancelled,
            _ when flashChanged => InstallFailure.WriteInterrupted,
            EsptoolFailure.ChipStoppedResponding => InstallFailure.ConnectionLost,
            _ => InstallFailure.WriteFailed,
        };
        if (kind == InstallFailure.Cancelled && flashChanged)
        {
            return Fail(kind, "Cancelled while writing.", Recovery(InstallFailure.WriteInterrupted, backupFile), true, backupFile, identity);
        }
        return Fail(kind, Describe(r), Recovery(kind, backupFile), flashChanged, backupFile, identity);
    }

    private static string Describe(EsptoolResult r) => r.Failure switch
    {
        EsptoolFailure.PortUnavailable => $"The port could not be opened ({r.Message}).",
        EsptoolFailure.NoSerialData => "The port opened, but the board did not answer.",
        EsptoolFailure.ConnectFailed or EsptoolFailure.WrongBootMode => "The board did not enter download mode.",
        EsptoolFailure.ChipStoppedResponding => "The connection to the board broke off.",
        _ => r.Message,
    };

    private static string Recovery(InstallFailure kind, string? backupFile) => kind switch
    {
        InstallFailure.ToolMissing => "This Companion build has no (or a damaged) flashing tool. Nothing was changed.",
        InstallFailure.PortUnavailable => "Close any other program that uses the port, check the cable, and try again. Nothing was changed.",
        InstallFailure.NoSerialData => "Use the USB-C port labelled UART, check that DIP switch #16 is on the ESP32 side, try another cable. Nothing was changed.",
        InstallFailure.ConnectFailed => "Try again with manual download mode: hold BOOT, press RESET, release BOOT. Nothing was changed.",
        InstallFailure.ConnectionLost => "Check the cable and avoid USB hubs, then try again. Nothing was written.",
        InstallFailure.WrongChip => "Nothing was changed: this is not a board DeskBuddy supports.",
        InstallFailure.WrongMac => "Nothing more was done: another board is on the port. Connect only the board you mean.",
        InstallFailure.WriteFailed => "Nothing was written. Try again.",
        InstallFailure.VerifyFailed => "The flash does not hold what was written. Run the same operation again; if it keeps failing, try another cable." +
            (backupFile is null ? "" : $" The backup is in {backupFile}."),
        InstallFailure.WriteInterrupted => "Writing had begun, so the board may not start DeskBuddy now. This is recoverable: its ROM " +
            "bootloader is never written. Connect the UART port and run Repair (or Install for a new board)" +
            (backupFile is null ? "." : $", or restore the backup in {backupFile} with tools\\flash-backup.ps1 -Action restore."),
        _ => "",
    };

    private InstallResult Fail(InstallFailure kind, string message, string recovery, bool flashChanged, string? backupFile,
        BoardIdentity? identity, DeviceReport? after = null)
    {
        log?.Invoke($"Firmware: {kind}: {message}");
        return new InstallResult(kind, message, recovery, flashChanged, backupFile, identity, after);
    }

    private void Log(EsptoolResult r)
    {
        if (log is null)
        {
            return;
        }
        foreach (var line in r.Output.Lines.Where(l => !l.StartsWith("Writing at", StringComparison.Ordinal) && !char.IsAsciiDigit(l.FirstOrDefault())))
        {
            log($"esptool: {line}");
        }
        if (!r.Ok)
        {
            log($"esptool: {r.Failure}: {r.Message}");
        }
    }

    private static void TryDelete(string folder)
    {
        try
        {
            if (Directory.Exists(folder))
            {
                Directory.Delete(folder, recursive: true);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}

/// <summary>An IProgress that runs its handler at once, on the reporting thread (no SynchronizationContext hop).</summary>
public sealed class InlineProgress<T>(Action<T> handler) : IProgress<T>
{
    public void Report(T value) => handler(value);
}
