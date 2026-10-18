using System.Globalization;
using System.Security.Cryptography;
using DeskBuddy.Firmware;
using DeskBuddy.Firmware.Flashing;
using Xunit;

namespace DeskBuddy.Companion.Tests;

/// <summary>
/// A board on the end of a USB cable, as esptool v4.12.0 would see it: 16 MB of flash, a chip, a MAC. The flash
/// starts as a factory board's (a factory app at 0x10000, calibration data in NVS), so tests can check byte for byte
/// what changed and what did not.
/// </summary>
internal sealed class FakeBoard
{
    public const int FlashSize = 16 * 1024 * 1024;
    public byte[] Flash { get; } = new byte[FlashSize];
    public string Mac { get; set; } = "44:1b:f6:8d:11:78";
    public string Chip { get; set; } = "ESP32-S3 (QFN56) (revision v0.2)";
    public string Features { get; set; } = "WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)";
    public string DetectedFlashSize { get; set; } = "16MB";
    public int HardResets { get; set; }

    public FakeBoard()
    {
        Array.Fill(Flash, (byte)0xFF);
        var rng = new Random(1);
        rng.NextBytes(Flash.AsSpan(0x9000, 7572)); // the factory NVS / PHY calibration
        rng.NextBytes(Flash.AsSpan(0xF000, 0x80));
        rng.NextBytes(Flash.AsSpan(0x10000, 0x200000)); // a 2 MB factory demo app
    }

    public string DeviceId => BoardIdentity.NormalizeMac(Mac);

    public byte[] Region(uint offset, int length) => Flash.AsSpan((int)offset, length).ToArray();

    public void Load(uint offset, byte[] data) => data.CopyTo(Flash, (int)offset);
}

/// <summary>
/// Stands in for esptool.exe (an <see cref="IProcessRunner"/>): it reads the arguments the Firmware Manager passes,
/// acts on the <see cref="FakeBoard"/> and prints what esptool v4.12.0 prints, line by line. Faults can be scripted.
/// </summary>
internal sealed class FakeEsptool : IProcessRunner
{
    public FakeBoard? Board { get; set; }          // null: nothing answers on the port
    public bool PortBusy { get; set; }
    public string? MacSwapOnCommand { get; set; }  // another board is on the port from this command on (e.g. "write_flash")
    public string? SwapMac { get; set; }
    public int? FailWriteAtBlock { get; set; }     // the cable is pulled after this many 4 KB blocks
    public int? FailReadAtPercent { get; set; }
    public bool CorruptAfterWrite { get; set; }
    public Func<string, Task>? BeforeBlock { get; set; } // runs before each written block (tests cancel from here)
    public List<IReadOnlyList<string>> Runs { get; } = [];
    public int WrittenBlocks { get; private set; }

    public IRunningProcess Start(ProcessSpec spec, Action<string> onLine)
    {
        Runs.Add(spec.Arguments);
        var run = new Run();
        run.Task = Task.Run(() => Execute(spec.Arguments, onLine, run));
        return run;
    }

    private sealed class Run : IRunningProcess
    {
        public volatile bool Killed;
        public Task<int> Task { get; set; } = null!;
        public Task<int> Exited => Task;
        public void Kill() => Killed = true;
        public void Dispose() { }
    }

    private sealed class KilledException : Exception;

    private async Task<int> Execute(IReadOnlyList<string> args, Action<string> onLine, Run run)
    {
        void Say(string line)
        {
            if (run.Killed)
            {
                throw new KilledException();
            }
            onLine(line);
            if (run.Killed)
            {
                throw new KilledException();
            }
        }

        try
        {
            var a = args.ToList();
            string Opt(string name) => a[a.IndexOf(name) + 1];
            var port = Opt("--port");
            var after = Opt("--after");
            var cmd = a.First(Esptool.Commands.Contains);
            var rest = a.Skip(a.IndexOf(cmd) + 1).ToList();
            Assert.DoesNotContain(a, x => x.Contains("erase", StringComparison.OrdinalIgnoreCase)); // never, in any run

            Say("esptool.py v4.12.0");
            if (PortBusy)
            {
                Say($"A fatal error occurred: Could not open {port}, the port is busy or doesn't exist.");
                Say($"(could not open port '{port}': PermissionError(13, 'Access is denied.', None, 5))");
                return 2;
            }
            Say($"Serial port {port}");
            Say("Connecting...");
            if (Board is not { } board)
            {
                Say("A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.");
                return 2;
            }
            var mac = MacSwapOnCommand == cmd && SwapMac is not null ? SwapMac : board.Mac;
            Say($"Chip is {board.Chip}");
            Say($"Features: {board.Features}");
            Say("Crystal is 40MHz");
            Say("USB mode: USB-Serial/JTAG");
            Say($"MAC: {mac}");
            Say("Uploading stub...");
            Say("Running stub...");
            Say("Stub running...");
            if (cmd != "flash_id" && cmd != "read_mac")
            {
                Say("Changing baud rate to 921600");
                Say("Changed.");
            }

            switch (cmd)
            {
                case "flash_id":
                    Say("Manufacturer: 46");
                    Say("Device: 4018");
                    Say($"Detected flash size: {board.DetectedFlashSize}");
                    break;
                case "read_mac":
                    Say($"MAC: {mac}");
                    break;
                case "read_flash":
                {
                    var offset = Hex(rest[0]);
                    var size = Hex(rest[1]);
                    var file = rest[2];
                    for (var done = 0u; done < size; done += Math.Min(size - done, 0x100000))
                    {
                        var percent = (int)(done * 100L / size);
                        if (FailReadAtPercent is { } f && percent >= f)
                        {
                            Say("A fatal error occurred: The chip stopped responding.");
                            return 2;
                        }
                        Say($"{done} ({percent} %)");
                    }
                    Say($"{size} (100 %)");
                    await File.WriteAllBytesAsync(file, board.Region(offset, (int)size));
                    Say($"Read {size} bytes at 0x{offset:x8} in 2.0 seconds (1000.0 kbit/s)...");
                    break;
                }
                case "write_flash":
                {
                    Assert.Equal(["--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"], rest.Take(6));
                    var pairs = rest.Skip(6).ToList();
                    Say("Configuring flash size...");
                    for (var i = 0; i < pairs.Count; i += 2)
                    {
                        var offset = Hex(pairs[i]);
                        var data = await File.ReadAllBytesAsync(pairs[i + 1]);
                        var padded = (data.Length + 3) / 4 * 4;
                        var erased = (padded + 0xFFF) / 0x1000 * 0x1000;
                        Say($"Flash will be erased from 0x{offset:x8} to 0x{offset + erased - 1:x8}...");
                        var blocks = erased / 0x1000;
                        for (var b = 0; b < blocks; b++)
                        {
                            if (BeforeBlock is { } hook)
                            {
                                await hook(pairs[i + 1]);
                            }
                            Say($"Writing at 0x{offset + b * 0x1000:x8}... ({(b + 1) * 100 / blocks} %)");
                            // The sector is erased, then written: an interruption leaves it half done.
                            Array.Fill(board.Flash, (byte)0xFF, (int)offset + b * 0x1000, 0x1000);
                            if (FailWriteAtBlock is { } fail && WrittenBlocks >= fail)
                            {
                                Say("A fatal error occurred: The chip stopped responding.");
                                return 2;
                            }
                            var chunk = data.AsSpan(Math.Min(b * 0x1000, data.Length), Math.Max(0, Math.Min(0x1000, data.Length - b * 0x1000)));
                            chunk.CopyTo(board.Flash.AsSpan((int)offset + b * 0x1000));
                            WrittenBlocks++;
                        }
                        Say($"Wrote {data.Length} bytes ({data.Length / 2} compressed) at 0x{offset:x8} in 0.5 seconds (effective 900.0 kbit/s)...");
                        Say("Hash of data verified.");
                    }
                    if (CorruptAfterWrite)
                    {
                        board.Flash[Hex(pairs[^2]) + 100] ^= 0xFF;
                    }
                    Say("");
                    Say("Leaving...");
                    break;
                }
                case "verify_flash":
                {
                    Assert.Equal(["--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"], rest.Take(6));
                    var pairs = rest.Skip(6).ToList();
                    var failed = false;
                    for (var i = 0; i < pairs.Count; i += 2)
                    {
                        var offset = Hex(pairs[i]);
                        var data = await File.ReadAllBytesAsync(pairs[i + 1]);
                        Say($"Verifying 0x{data.Length:x} ({data.Length}) bytes @ 0x{offset:x8} in flash against {pairs[i + 1]}...");
                        var same = board.Region(offset, data.Length).AsSpan().SequenceEqual(data);
                        Say(same ? "-- verify OK (digest matched)" : "-- verify FAILED (digest mismatch)");
                        failed |= !same;
                    }
                    if (failed)
                    {
                        Say("A fatal error occurred: Verify failed.");
                        return 2;
                    }
                    break;
                }
            }
            if (after == "hard_reset")
            {
                Say("Hard resetting via RTS pin...");
                board.HardResets++;
            }
            else
            {
                Say("Staying in bootloader.");
            }
            return 0;
        }
        catch (KilledException)
        {
            return -1;
        }
    }

    private static uint Hex(string s) => uint.Parse(s.AsSpan(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
}

/// <summary>
/// The Companion's side of the port in the tests: the hold, and what DeskBuddy reports after a restart, worked out
/// from the board's flash the way a real board starts: a valid bootloader, this layout, and an app it knows.
/// </summary>
internal sealed class FakeDevicePort(FakeBoard board) : IDevicePort
{
    public bool Busy { get; set; }
    public int Held { get; private set; }
    public int Released { get; private set; }
    /// <summary>Apps the board can boot, by their exact image bytes, and what each reports.</summary>
    public List<(byte[] App, DeviceReport Report)> Apps { get; } = [];
    public bool NeverAnswers { get; set; }
    public DeviceReport? Override { get; set; }

    public Task<IDisposable> AcquireAsync(string port, CancellationToken ct)
    {
        if (Busy)
        {
            throw new PortBusyException("Another DeskBuddy tool (ESP-IDF flash or monitor) is using the board's port.");
        }
        Held++;
        return Task.FromResult<IDisposable>(new Hold(this));
    }

    private sealed class Hold(FakeDevicePort owner) : IDisposable
    {
        public void Dispose() => owner.Released++;
    }

    public Task<DeviceReport?> WaitForDeskBuddyAsync(string port, TimeSpan timeout, CancellationToken ct) =>
        Task.FromResult(NeverAnswers ? null : Override ?? Boot());

    public DeviceReport? Boot()
    {
        try
        {
            BootloaderCheck.Bootloader(board.Flash, BoardProfile.WaveshareS3Lcd7B);
            if (!PartitionTable.Parse(board.Region(0x8000, 0xC00)).SequenceEqual(BoardProfile.WaveshareS3Lcd7B.Partitions))
            {
                return null;
            }
        }
        catch (FormatException)
        {
            return null;
        }
        foreach (var (app, report) in Apps)
        {
            if (board.Region(0x10000, app.Length).AsSpan().SequenceEqual(app))
            {
                return report with { Boot = Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(8)) };
            }
        }
        return null;
    }
}
