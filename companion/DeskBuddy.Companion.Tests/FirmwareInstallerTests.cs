using DeskBuddy.Firmware;
using DeskBuddy.Firmware.Flashing;
using Xunit;
using static DeskBuddy.Companion.Tests.FirmwareTestImages;

namespace DeskBuddy.Companion.Tests;

// Install, Update and Repair against a simulated board and esptool: what is written, what is never written, and how
// every failure ends. The board's flash is checked byte for byte.
public sealed class FirmwareInstallerTests : IDisposable
{
    private static readonly (string PrivatePem, string PublicPem) Key = PackageSignature.CreateKeyPair();
    private static readonly BoardProfile Profile = BoardProfile.WaveshareS3Lcd7B;
    private static readonly byte[] OldBootloader = Image(variant: 7); // built earlier, with a date in it: another SHA-256
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "deskbuddy-fw-tests-" + Guid.NewGuid().ToString("N")[..8]);
    private readonly List<string> _log = [];

    public FirmwareInstallerTests() => Directory.CreateDirectory(_dir);

    public void Dispose()
    {
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private static FirmwarePackage Package(string version = "1.1.0", IReadOnlyList<KnownBootloader>? compatible = null)
    {
        var images = new Dictionary<string, byte[]>
        {
            ["bootloader.bin"] = Image(),
            ["partition-table.bin"] = Table(Profile.Partitions),
            ["deskbuddy.bin"] = Image(appVersion: version, padding: 40_000),
        };
        var manifest = PackageBuilder.CreateManifest(Profile, images, Build, 1, FirmwareVersion.Parse("1.1.0"), compatible);
        return FirmwarePackage.Open(new MemoryStream(Signed(manifest.ToJson(), images, Key.PrivatePem)),
            [TrustedKey.FromPem("test key", Key.PublicPem)]);
    }

    private static byte[] Bytes(FirmwarePackage p, string slot) => p.Images.Single(i => i.Slot.Name == slot).Data;

    private static DeviceReport Reports(FirmwarePackage p, FakeBoard b) =>
        new(p.Manifest.Version.ToString(), p.Manifest.Build, b.DeviceId, null, Profile.Board, "ok");

    /// <summary>A board running an older DeskBuddy, with the given bootloader under it.</summary>
    private static (FakeBoard Board, FirmwarePackage Old) WithDeskBuddy(byte[]? bootloader = null, byte[]? table = null)
    {
        var old = Package("1.0.0");
        var board = new FakeBoard();
        board.Load(0, bootloader ?? Bytes(old, "bootloader"));
        board.Load(0x8000, table ?? Bytes(old, "partition-table"));
        board.Load(0x10000, Bytes(old, "app"));
        return (board, old);
    }

    private sealed record Rig(FakeBoard Board, FakeEsptool Tool, FakeDevicePort Port, FirmwareInstaller Installer);

    private Rig Make(FakeBoard board, FirmwarePackage target, Func<string, CancellationToken, Task<bool>>? manual = null, bool toolExists = true)
    {
        var tool = new FakeEsptool { Board = board };
        var port = new FakeDevicePort(board);
        port.Apps.Add((Bytes(target, "app"), Reports(target, board)));
        var exe = Path.Combine(_dir, "tool-" + Guid.NewGuid().ToString("N")[..8], "esptool.exe");
        if (toolExists)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(exe)!);
            File.WriteAllText(exe, "fake");
        }
        var esptool = new Esptool(tool, exe, Path.Combine(_dir, "work"), Profile);
        return new Rig(board, tool, port, new FirmwareInstaller(esptool, port, Path.Combine(_dir, "runs"), manual, _log.Add));
    }

    private static InstallRequest Request(FirmwareOperation op, FirmwarePackage p, DeviceReport? before, BackupChoice? backup = null,
        bool model = true, bool repair = false, EntryMode entry = EntryMode.AutoReset) =>
        new(op, p, "COM6", entry, before, model, backup ?? BackupChoice.SkipConfirmedByUser, repair);

    private static List<IReadOnlyList<string>> Writes(FakeEsptool t) => t.Runs.Where(r => r.Contains("write_flash")).ToList();

    private static void Unchanged(byte[] before, FakeBoard board, uint from, uint to) =>
        Assert.True(before.AsSpan((int)from, (int)(to - from)).SequenceEqual(board.Flash.AsSpan((int)from, (int)(to - from))),
            $"0x{from:x}-0x{to:x} changed");

    // ---- Install ----

    [Fact]
    public async Task Install_writes_the_three_images_and_leaves_calibration_and_the_rest_of_the_flash_untouched()
    {
        var p = Package();
        var rig = Make(new FakeBoard(), p);
        var before = (byte[])rig.Board.Flash.Clone();

        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);

        Assert.True(r.Succeeded, r.Message);
        Assert.Equal(Bytes(p, "bootloader"), rig.Board.Region(0, Bytes(p, "bootloader").Length));
        Assert.Equal(Bytes(p, "partition-table"), rig.Board.Region(0x8000, 0xC00));
        Assert.Equal(Bytes(p, "app"), rig.Board.Region(0x10000, Bytes(p, "app").Length));
        Unchanged(before, rig.Board, 0x9000, 0x10000); // NVS and PHY calibration
        var appEnd = 0x10000u + (uint)((Bytes(p, "app").Length + 0xFFF) / 0x1000 * 0x1000);
        Unchanged(before, rig.Board, appEnd, FakeBoard.FlashSize); // the rest of the factory app and beyond
        Assert.Equal("1.1.0", r.After!.Firmware);
        Assert.Equal((1, 1), (rig.Port.Held, rig.Port.Released));
        Assert.True(rig.Board.HardResets >= 1);
    }

    [Fact]
    public async Task Nothing_runs_without_the_confirmations_an_operation_needs()
    {
        var p = Package();
        var (board, old) = WithDeskBuddy();
        var running = Reports(old, board);
        foreach (var request in new[]
        {
            Request(FirmwareOperation.Install, p, null, model: false),                     // model not confirmed
            Request(FirmwareOperation.Install, p, null, backup: new BackupChoice(null, false)), // no backup decision
            Request(FirmwareOperation.Install, p, running),                                // DeskBuddy is already there
            Request(FirmwareOperation.Repair, p, running, repair: false),                  // Repair not confirmed twice
            Request(FirmwareOperation.Update, p, null),                                    // nothing to update
            Request(FirmwareOperation.Update, p, running with { Board = null }, model: false), // legacy: model unknown
            Request(FirmwareOperation.Update, p, running with { Board = "simulator" }),    // another board
            Request(FirmwareOperation.Update, p, running with { Hardware = "mismatch" }),  // its own check failed
        })
        {
            var rig = Make(board, p);
            var r = await rig.Installer.RunAsync(request, null, CancellationToken.None);
            Assert.Equal(InstallFailure.NotAllowed, r.Failure);
            Assert.Empty(rig.Tool.Runs);
            Assert.Equal(0, rig.Port.Held);
        }
    }

    [Fact]
    public async Task A_board_that_is_not_an_ESP32_S3_with_8_MB_PSRAM_and_16_MB_flash_is_refused_before_anything_is_read()
    {
        var p = Package();
        foreach (var change in new Action<FakeBoard>[]
        {
            b => b.Features = "WiFi, BLE",                                  // no embedded PSRAM (an N16 module)
            b => b.Features = "WiFi, BLE, Embedded PSRAM 2MB (AP_3v3)",
            b => b.Chip = "ESP32-D0WD-V3 (revision v3.0)",
            b => b.DetectedFlashSize = "8MB",
        })
        {
            var board = new FakeBoard();
            change(board);
            var before = (byte[])board.Flash.Clone();
            var rig = Make(board, p);
            var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
            Assert.Equal(InstallFailure.WrongChip, r.Failure);
            Assert.False(r.FlashChanged);
            Assert.Equal(before, board.Flash);
            Assert.DoesNotContain(rig.Tool.Runs, run => run.Contains("read_flash") || run.Contains("write_flash"));
        }
    }

    // ---- Update ----

    [Fact]
    public async Task Update_writes_only_the_app_when_the_board_has_this_packages_bootloader_and_table()
    {
        var (board, old) = WithDeskBuddy();
        var p = Package();
        var rig = Make(board, p);
        var before = (byte[])board.Flash.Clone();
        var backups = Path.Combine(_dir, "backup-1");

        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Update, p, Reports(old, board), BackupChoice.To(backups)), null,
            CancellationToken.None);

        Assert.True(r.Succeeded, r.Message);
        var write = Assert.Single(Writes(rig.Tool));
        Assert.Equal(["0x10000"], write.Where(a => a.StartsWith("0x", StringComparison.Ordinal)));
        Unchanged(before, board, 0, 0x10000); // bootloader, table, NVS, PHY
        Assert.Equal(Bytes(p, "app"), board.Region(0x10000, Bytes(p, "app").Length));
        // The backup is the flash as it was, checked against the board, with its checksum.
        Assert.Equal(before, await File.ReadAllBytesAsync(r.BackupFile!));
        var sums = await File.ReadAllTextAsync(Path.Combine(backups, "SHA256SUMS.txt"));
        Assert.StartsWith(Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(before)), sums);
    }

    [Fact]
    public async Task Update_keeps_an_older_bootloader_that_the_signed_package_lists_as_compatible()
    {
        var (board, old) = WithDeskBuddy(OldBootloader);
        var p = Package(compatible: [PackageBuilder.KnownBootloaderFrom(board.Region(0, 0x10000), "board backup", Profile)]);
        var rig = Make(board, p);

        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Update, p, Reports(old, board)), null, CancellationToken.None);

        Assert.True(r.Succeeded, r.Message);
        Assert.Equal(OldBootloader, board.Region(0, OldBootloader.Length));
        Assert.Contains(_log, l => l.Contains("listed as compatible", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Update_is_refused_and_nothing_written_when_the_bootloader_or_the_table_is_not_known_to_fit()
    {
        var p = Package();
        var bigger = Table([Profile.Partitions[0], Profile.Partitions[1], Profile.Partitions[2] with { Size = 0x800000 }]);
        foreach (var (board, old) in new[] { WithDeskBuddy(OldBootloader), WithDeskBuddy(table: bigger), WithDeskBuddy(new byte[0x200]) })
        {
            var rig = Make(board, p);
            var before = (byte[])board.Flash.Clone();
            var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Update, p, Reports(old, board)), null, CancellationToken.None);
            Assert.Equal(InstallFailure.BootloaderIncompatible, r.Failure);
            Assert.False(r.FlashChanged);
            Assert.Contains("Repair", r.Recovery);
            Assert.Empty(Writes(rig.Tool));
            Assert.Equal(before, board.Flash);
            Assert.Equal(rig.Port.Held, rig.Port.Released);
        }
    }

    [Fact]
    public async Task Repair_rewrites_all_three_after_its_confirmation()
    {
        var (board, old) = WithDeskBuddy(OldBootloader);
        var p = Package();
        var rig = Make(board, p);
        var before = (byte[])board.Flash.Clone();

        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Repair, p, Reports(old, board), repair: true), null,
            CancellationToken.None);

        Assert.True(r.Succeeded, r.Message);
        Assert.Equal(Bytes(p, "bootloader"), board.Region(0, Bytes(p, "bootloader").Length));
        Assert.Equal(["0x0", "0x8000", "0x10000"], Assert.Single(Writes(rig.Tool)).Where(a => a.StartsWith("0x", StringComparison.Ordinal)));
        Unchanged(before, board, 0x9000, 0x10000);
    }

    // ---- the wrong board ----

    [Fact]
    public async Task Another_board_in_download_mode_than_the_connected_DeskBuddy_is_refused()
    {
        var (board, old) = WithDeskBuddy();
        var p = Package();
        var rig = Make(board, p);
        var before = (byte[])board.Flash.Clone();
        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Update, p, Reports(old, board) with { DeviceId = "aabbccddeeff" }),
            null, CancellationToken.None);
        Assert.Equal(InstallFailure.WrongMac, r.Failure);
        Assert.Equal(before, board.Flash);
    }

    [Fact]
    public async Task A_board_swapped_after_identification_is_stopped_before_esptool_writes_a_byte()
    {
        var p = Package();
        var rig = Make(new FakeBoard(), p);
        rig.Tool.MacSwapOnCommand = "write_flash";
        rig.Tool.SwapMac = "aa:bb:cc:dd:ee:ff";
        var before = (byte[])rig.Board.Flash.Clone();

        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);

        Assert.Equal(InstallFailure.WrongMac, r.Failure);
        Assert.False(r.FlashChanged);
        Assert.Equal(0, rig.Tool.WrittenBlocks);
        Assert.Equal(before, rig.Board.Flash);
    }

    // ---- interruptions and recovery ----

    [Fact]
    public async Task A_cable_pulled_while_writing_is_reported_as_recoverable_and_Repair_recovers_the_board()
    {
        var p = Package();
        var board = new FakeBoard();
        var rig = Make(board, p);
        rig.Tool.FailWriteAtBlock = 3; // the bootloader and the table are written, the app is half done

        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);

        Assert.Equal(InstallFailure.WriteInterrupted, r.Failure);
        Assert.True(r.FlashChanged);
        Assert.Contains("Repair", r.Recovery);
        Assert.Null(rig.Port.Boot()); // the board does not start DeskBuddy now
        Assert.Equal(rig.Port.Held, rig.Port.Released);

        var again = Make(board, p); // the same board, the cable back in
        var repaired = await again.Installer.RunAsync(Request(FirmwareOperation.Repair, p, null, repair: true), null, CancellationToken.None);
        Assert.True(repaired.Succeeded, repaired.Message);
        Assert.Equal("1.1.0", again.Port.Boot()!.Firmware);
    }

    [Fact]
    public async Task Cancelling_while_writing_says_the_board_needs_a_Repair()
    {
        var p = Package();
        var rig = Make(new FakeBoard(), p);
        using var cts = new CancellationTokenSource();
        var blocks = 0;
        rig.Tool.BeforeBlock = _ =>
        {
            if (++blocks == 2)
            {
                cts.Cancel();
            }
            return Task.CompletedTask;
        };

        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, cts.Token);

        Assert.Equal(InstallFailure.Cancelled, r.Failure);
        Assert.True(r.FlashChanged);
        Assert.Contains("Repair", r.Recovery);
        Assert.Equal(rig.Port.Held, rig.Port.Released);
    }

    [Fact]
    public async Task Cancelling_before_the_write_changes_nothing()
    {
        var (board, old) = WithDeskBuddy();
        var p = Package();
        var rig = Make(board, p);
        var before = (byte[])board.Flash.Clone();
        using var cts = new CancellationTokenSource();
        var progress = new InlineProgress<InstallProgress>(x =>
        {
            if (x.Step == InstallStep.BackingUp)
            {
                cts.Cancel();
            }
        });

        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Update, p, Reports(old, board), BackupChoice.To(Path.Combine(_dir, "b"))),
            progress, cts.Token);

        Assert.Equal(InstallFailure.Cancelled, r.Failure);
        Assert.False(r.FlashChanged);
        Assert.Equal(before, board.Flash);
        Assert.Empty(Writes(rig.Tool));
    }

    [Fact]
    public async Task A_failed_backup_stops_everything_and_leaves_the_decision_to_the_user()
    {
        var (board, old) = WithDeskBuddy();
        var p = Package();
        var before = (byte[])board.Flash.Clone();

        var rig = Make(board, p);
        rig.Tool.FailReadAtPercent = 50;
        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Update, p, Reports(old, board), BackupChoice.To(Path.Combine(_dir, "b1"))),
            null, CancellationToken.None);
        Assert.Equal(InstallFailure.BackupFailed, r.Failure);
        Assert.Contains("without a backup", r.Recovery);
        Assert.Empty(Writes(rig.Tool));

        var used = Path.Combine(_dir, "b2");
        Directory.CreateDirectory(used);
        await File.WriteAllTextAsync(Path.Combine(used, "old.bin"), "an earlier backup");
        rig = Make(board, p);
        r = await rig.Installer.RunAsync(Request(FirmwareOperation.Update, p, Reports(old, board), BackupChoice.To(used)), null, CancellationToken.None);
        Assert.Equal(InstallFailure.BackupFailed, r.Failure);
        Assert.Equal("an earlier backup", await File.ReadAllTextAsync(Path.Combine(used, "old.bin")));
        Assert.Equal(before, board.Flash);
    }

    [Fact]
    public async Task Port_and_connection_problems_are_told_apart_and_change_nothing()
    {
        var p = Package();

        var rig = Make(new FakeBoard(), p);
        rig.Tool.Board = null; // DIP switch #16 on the wrong side, or the wrong port
        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
        Assert.Equal(InstallFailure.NoSerialData, r.Failure);
        Assert.Contains("DIP switch", r.Recovery);

        rig = Make(new FakeBoard(), p);
        rig.Tool.PortBusy = true; // a serial terminal has it
        r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
        Assert.Equal(InstallFailure.PortUnavailable, r.Failure);

        rig = Make(new FakeBoard(), p);
        rig.Port.Busy = true; // ESP-IDF's flash holds the DeskBuddy port mutex
        r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
        Assert.Equal(InstallFailure.PortBusy, r.Failure);
        Assert.Empty(rig.Tool.Runs);

        rig = Make(new FakeBoard(), p, toolExists: false);
        r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
        Assert.Equal(InstallFailure.ToolMissing, r.Failure);
        Assert.False(r.FlashChanged);
    }

    [Fact]
    public async Task What_is_on_the_flash_afterwards_is_verified_and_so_is_what_DeskBuddy_reports()
    {
        var p = Package();

        var rig = Make(new FakeBoard(), p);
        rig.Tool.CorruptAfterWrite = true;
        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
        Assert.Equal(InstallFailure.VerifyFailed, r.Failure);

        rig = Make(new FakeBoard(), p);
        rig.Port.NeverAnswers = true;
        r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
        Assert.Equal(InstallFailure.NoReconnect, r.Failure);
        Assert.Contains("RESET", r.Recovery);

        rig = Make(new FakeBoard(), p);
        rig.Port.Override = Reports(p, rig.Board) with { Firmware = "1.0.0" };
        r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
        Assert.Equal(InstallFailure.WrongFirmwareAfter, r.Failure);

        rig = Make(new FakeBoard(), p);
        rig.Port.Override = Reports(p, rig.Board) with { Hardware = "mismatch" };
        r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null), null, CancellationToken.None);
        Assert.Equal(InstallFailure.HardwareCheckFailed, r.Failure);
    }

    [Fact]
    public async Task Manual_download_mode_asks_before_every_step_and_never_resets_the_board()
    {
        var p = Package();
        var asked = new List<string>();
        var rig = Make(new FakeBoard(), p, (text, _) =>
        {
            asked.Add(text);
            return Task.FromResult(true);
        });
        var r = await rig.Installer.RunAsync(Request(FirmwareOperation.Install, p, null, entry: EntryMode.Manual), null, CancellationToken.None);
        Assert.True(r.Succeeded, r.Message);
        Assert.Equal(4, asked.Count); // identify, write, verify, then RESET to start it
        Assert.All(rig.Tool.Runs, run => Assert.Equal(["--before", "no_reset", "--after", "no_reset"], run.Skip(6).Take(4)));

        var declined = Make(new FakeBoard(), p, (text, _) => Task.FromResult(!text.Contains("write", StringComparison.Ordinal)));
        var before = (byte[])declined.Board.Flash.Clone();
        r = await declined.Installer.RunAsync(Request(FirmwareOperation.Install, p, null, entry: EntryMode.Manual), null, CancellationToken.None);
        Assert.Equal(InstallFailure.Cancelled, r.Failure);
        Assert.Equal(before, declined.Board.Flash);
    }
}
