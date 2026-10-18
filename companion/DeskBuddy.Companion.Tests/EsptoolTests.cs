using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using DeskBuddy.Firmware;
using DeskBuddy.Firmware.Flashing;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// esptool v4.12.0's output as the Firmware Manager reads it, the arguments it is ever run with, the pinned copy of it,
// and the real process runner (line splitting, killing the whole tree).
public class EsptoolTests
{
    // What esptool v4.12.0 prints for flash_id on this board when its output goes to a pipe (formats from its source).
    private const string FlashIdOutput = """
        esptool.py v4.12.0
        Serial port COM6
        Connecting....
        Chip is ESP32-S3 (QFN56) (revision v0.2)
        Features: WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)
        Crystal is 40MHz
        USB mode: USB-Serial/JTAG
        MAC: 44:1b:f6:8d:11:78
        Uploading stub...
        Running stub...
        Stub running...
        Manufacturer: 46
        Device: 4018
        Detected flash size: 16MB
        Flash type set in eFuse: quad (4 data lines)
        Flash voltage set by eFuse to 3.3V
        Staying in bootloader.
        """;

    private static EsptoolOutput Feed(string text)
    {
        var o = new EsptoolOutput();
        foreach (var line in text.Split('\n'))
        {
            o.Feed(line.TrimEnd('\r'));
        }
        return o;
    }

    [Fact]
    public void Flash_id_output_identifies_the_7B_class_of_board_but_never_the_model()
    {
        var id = BoardIdentity.From(Feed(FlashIdOutput), BoardProfile.WaveshareS3Lcd7B)!;
        Assert.True(id.ChipMatches);
        Assert.False(id.ModelVerified);
        Assert.Equal("441bf68d1178", id.DeviceId); // as DeskBuddy reports it in hello dev=
        Assert.Equal(("46", "4018", "16MB"), (id.FlashManufacturer, id.FlashDevice, id.FlashSize));
    }

    [Fact]
    public void Write_and_read_progress_and_verification_are_read_from_plain_lines()
    {
        var o = new EsptoolOutput();
        Assert.Null(o.Feed("Compressed 554688 bytes to 312345..."));
        Assert.Equal(new EsptoolProgress("write", 0x10000, 3), o.Feed("Writing at 0x00010000... (3 %)"));
        Assert.True(o.WriteStarted);
        Assert.Equal(new EsptoolProgress("read", 0, 12), o.Feed("2097152 (12 %)"));
        o.Feed("Hash of data verified.");
        o.Feed("-- verify OK (digest matched)");
        Assert.Equal((1, 1, 0), (o.HashesVerified, o.VerifiedOk, o.VerifyFailed));
        Assert.Equal(EsptoolFailure.None, o.Classify(0));
    }

    [Theory]
    [InlineData("A fatal error occurred: Could not open COM6, the port is busy or doesn't exist.\n(could not open port 'COM6': PermissionError(13, 'Access is denied.', None, 5))", EsptoolFailure.PortUnavailable)]
    [InlineData("A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.", EsptoolFailure.NoSerialData)]
    [InlineData("A fatal error occurred: Failed to connect to ESP32-S3: Invalid head of packet (0x1B): Possible serial noise or corruption.", EsptoolFailure.ConnectFailed)]
    [InlineData("A fatal error occurred: Failed to connect to ESP32-S3: Wrong boot mode detected (0x2b)! The chip needs to be in download mode.", EsptoolFailure.WrongBootMode)]
    [InlineData("A fatal error occurred: The chip stopped responding.", EsptoolFailure.ChipStoppedResponding)]
    [InlineData("-- verify FAILED (digest mismatch)\nA fatal error occurred: Verify failed.", EsptoolFailure.VerifyMismatch)]
    [InlineData("A fatal error occurred: something else", EsptoolFailure.ToolFailed)]
    public void Failures_are_classified_by_what_esptool_says(string output, EsptoolFailure expected) =>
        Assert.Equal(expected, Feed(output).Classify(2));

    [Fact]
    public void The_Firmware_Manager_never_erases_and_writes_images_unchanged()
    {
        Assert.DoesNotContain(Esptool.Commands, c => c.Contains("erase", StringComparison.Ordinal) || c.Contains("efuse", StringComparison.Ordinal));
        Assert.Throws<ArgumentException>(() => Esptool.Arguments("COM6", 921600, EntryMode.AutoReset, false, "erase_flash"));
        Assert.Throws<ArgumentException>(() => Esptool.Arguments("COM6", 921600, EntryMode.AutoReset, false, "erase_region", "0x9000", "0x6000"));
        Assert.Throws<ArgumentException>(() => Esptool.Arguments("COM6", 921600, EntryMode.AutoReset, false, "write_flash", "--erase-all", "0x0", "a.bin"));
        Assert.Equal(["--chip", "esp32s3", "--port", "COM6", "--baud", "921600", "--before", "default_reset", "--after", "hard_reset", "read_mac"],
            Esptool.Arguments("COM6", 921600, EntryMode.AutoReset, true, "read_mac"));
        Assert.Equal(["--before", "no_reset", "--after", "no_reset"],
            Esptool.Arguments("COM6", 115200, EntryMode.Manual, false, "flash_id").Skip(6).Take(4));
    }

    // ---- the pinned esptool.exe ----

    [Fact]
    public void The_bundled_tool_is_written_from_the_embedded_copy_and_replaced_when_it_does_not_match_the_pin()
    {
        var dir = Path.Combine(Path.GetTempPath(), "deskbuddy-esptool-" + Guid.NewGuid().ToString("N")[..8]);
        try
        {
            var real = Encoding.ASCII.GetBytes("pretend this is esptool.exe");
            var pin = Convert.ToHexStringLower(SHA256.HashData(real));
            using (var lease = EsptoolBundle.Prepare(dir, () => new MemoryStream(real), pin))
            {
                Assert.Equal(real, File.ReadAllBytes(lease.Path));
                Assert.ThrowsAny<IOException>(() => File.WriteAllText(lease.Path, "swapped while in use")); // held open
            }
            var exe = Path.Combine(dir, $"esptool-{EsptoolBundle.Version}", "esptool.exe");
            File.WriteAllText(exe, "tampered");
            using (var lease = EsptoolBundle.Prepare(dir, () => new MemoryStream(real), pin))
            {
                Assert.Equal(real, File.ReadAllBytes(lease.Path));
            }
            File.WriteAllText(exe, "tampered");
            Assert.Throws<InvalidDataException>(() => EsptoolBundle.Prepare(dir, () => new MemoryStream(Encoding.ASCII.GetBytes("other")), pin));
            Assert.Throws<FileNotFoundException>(() => EsptoolBundle.Prepare(dir, () => null, pin));
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    // ---- the real process runner ----

    private static string PowerShell => Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe");

    [Fact]
    public async Task Output_is_cut_into_lines_at_newlines_carriage_returns_and_backspaces()
    {
        var lines = new List<string>();
        using var p = new SystemProcessRunner().Start(new ProcessSpec(PowerShell,
            ["-NoProfile", "-Command", "[Console]::Out.Write(\"10 (1 %)`b`b`b`b`b`b`b`b20 (2 %)`rWriting at 0x00010000... (5 %)`nHash of data verified.`n\"); exit 3"],
            Path.GetTempPath()), l => { lock (lines) { lines.Add(l); } });
        Assert.Equal(3, await p.Exited.WaitAsync(TimeSpan.FromSeconds(30)));
        Assert.Equal(["10 (1 %)", "20 (2 %)", "Writing at 0x00010000... (5 %)", "Hash of data verified."], lines);
    }

    [Fact]
    public async Task Kill_ends_the_whole_process_tree()
    {
        // Like esptool.exe (PyInstaller), the started process starts the one that does the work.
        var pidFile = Path.Combine(Path.GetTempPath(), "deskbuddy-tree-" + Guid.NewGuid().ToString("N")[..8] + ".txt");
        var script = $"$c = Start-Process -PassThru -WindowStyle Hidden '{PowerShell}' '-NoProfile -Command Start-Sleep 120'; " +
                     $"Set-Content -Path '{pidFile}' -Value $c.Id; Write-Output started; Start-Sleep 120";
        var started = new TaskCompletionSource();
        using var p = new SystemProcessRunner().Start(new ProcessSpec(PowerShell, ["-NoProfile", "-Command", script], Path.GetTempPath()),
            l => { if (l == "started") started.TrySetResult(); });
        await started.Task.WaitAsync(TimeSpan.FromSeconds(60));
        var child = Process.GetProcessById(int.Parse(File.ReadAllText(pidFile).Trim()));
        try
        {
            p.Kill();
            await p.Exited.WaitAsync(TimeSpan.FromSeconds(30));
            Assert.True(child.WaitForExit(10_000), "the grandchild survived");
        }
        finally
        {
            if (!child.HasExited)
            {
                child.Kill();
            }
            File.Delete(pidFile);
        }
    }
}
