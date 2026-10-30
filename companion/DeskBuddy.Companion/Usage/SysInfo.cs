using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace DeskBuddy.Companion.Usage;

/// <summary>This PC's counters at one moment: the processor's times since boot over all cores (100 ns ticks; the total
/// includes idle), physical memory and the C: disk in bytes. A part that could not be read is 0.</summary>
public readonly record struct SysReading(ulong CpuIdle, ulong CpuTotal, ulong MemTotal, ulong MemAvail, ulong DiskTotal, ulong DiskFree);

/// <summary>The PC's load as Buddy shows it: whole percent (0..100), and what is free on C: in whole GB (1024³ bytes, as
/// Explorer counts them). Null: not known.</summary>
public sealed record SysSample(int? Cpu, int? Ram, int? Disk, int? DiskFreeGb)
{
    public const int DiskFreeMax = 999999; // GB, as the device takes it

    public bool IsEmpty => Cpu is null && Ram is null && Disk is null;

    /// <summary>The load at <paramref name="now"/>; the processor's over the time since <paramref name="before"/> (none
    /// without an earlier reading, or when no time passed).</summary>
    public static SysSample From(SysReading? before, SysReading now)
    {
        int? cpu = null;
        if (before is { } b && now.CpuTotal > b.CpuTotal && now.CpuIdle >= b.CpuIdle)
        {
            var total = now.CpuTotal - b.CpuTotal;
            cpu = Pct(total - Math.Min(total, now.CpuIdle - b.CpuIdle), total);
        }
        int? ram = now.MemTotal > 0 ? Pct(now.MemTotal - Math.Min(now.MemTotal, now.MemAvail), now.MemTotal) : null;
        int? disk = null, free = null;
        if (now.DiskTotal > 0)
        {
            var diskFree = Math.Min(now.DiskTotal, now.DiskFree);
            disk = Pct(now.DiskTotal - diskFree, now.DiskTotal);
            free = (int)Math.Min(DiskFreeMax, Math.Round(diskFree / (1024.0 * 1024 * 1024), MidpointRounding.AwayFromZero));
        }
        return new SysSample(cpu, ram, disk, free);
    }

    private static int Pct(ulong part, ulong whole) =>
        (int)Math.Clamp(Math.Round(100.0 * part / whole, MidpointRounding.AwayFromZero), 0, 100);
}

/// <summary>The PC load frame of serial protocol v1 (docs/protocol.md, "PC load").</summary>
public static class SysWire
{
    public const string Cap = "sys";

    /// <summary><c>sys.set state=on [cpu=] [ram=] [disk= [disk_free=]]</c>, or <c>sys.set state=off</c> when there is
    /// nothing (null or empty).</summary>
    public static string Set(SysSample? sample)
    {
        if (sample is null || sample.IsEmpty)
        {
            return "sys.set state=off";
        }
        var line = new StringBuilder("sys.set state=on");
        Append(line, "cpu", sample.Cpu);
        Append(line, "ram", sample.Ram);
        Append(line, "disk", sample.Disk);
        if (sample.Disk is not null)
        {
            Append(line, "disk_free", sample.DiskFreeGb);
        }
        return line.ToString();
    }

    private static void Append(StringBuilder line, string key, int? value)
    {
        if (value is { } v)
        {
            line.Append(CultureInfo.InvariantCulture, $" {key}={v}");
        }
    }

    /// <summary>The load as a line for the window: <c>CPU 12%  ·  RAM 45%  ·  C: 85%, 120 GB vrij</c>.</summary>
    public static string Text(SysSample sample)
    {
        var parts = new List<string>();
        if (sample.Cpu is { } cpu)
        {
            parts.Add($"CPU {cpu}%");
        }
        if (sample.Ram is { } ram)
        {
            parts.Add($"RAM {ram}%");
        }
        if (sample.Disk is { } disk)
        {
            parts.Add($"C: {disk}%" + (sample.DiskFreeGb is { } gb ? $", {gb} GB vrij" : ""));
        }
        return string.Join("  ·  ", parts);
    }
}

/// <summary>Reads this PC's counters, as PDC Manager's service window does: GetSystemTimes, GlobalMemoryStatusEx and
/// the C: drive. Nothing leaves the PC except through <c>sys.set</c>.</summary>
public static class SystemMeter
{
    public static SysReading Read()
    {
        ulong idle = 0, total = 0, memTotal = 0, memAvail = 0, diskTotal = 0, diskFree = 0;
        if (GetSystemTimes(out var i, out var k, out var u))
        {
            idle = i;
            total = k + u; // the kernel's time includes idle
        }
        var memory = new MemoryStatusEx { Length = (uint)Marshal.SizeOf<MemoryStatusEx>() };
        if (GlobalMemoryStatusEx(ref memory))
        {
            memTotal = memory.TotalPhys;
            memAvail = memory.AvailPhys;
        }
        try
        {
            var drive = new DriveInfo("C");
            diskTotal = (ulong)drive.TotalSize;
            diskFree = (ulong)drive.TotalFreeSpace;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
        return new SysReading(idle, total, memTotal, memAvail, diskTotal, diskFree);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MemoryStatusEx
    {
        public uint Length;
        public uint MemoryLoad;
        public ulong TotalPhys;
        public ulong AvailPhys;
        public ulong TotalPageFile;
        public ulong AvailPageFile;
        public ulong TotalVirtual;
        public ulong AvailVirtual;
        public ulong AvailExtendedVirtual;
    }

    [DllImport("kernel32.dll")]
    private static extern bool GetSystemTimes(out ulong idle, out ulong kernel, out ulong user);

    [DllImport("kernel32.dll")]
    private static extern bool GlobalMemoryStatusEx(ref MemoryStatusEx buffer);
}
