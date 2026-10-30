using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.Usage;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The PC load strip: the numbers from two readings, the sys.set line, when a strip's line goes out, and the switch
// (sampling only while on, the processor's first number after a baseline) against fake readings.
public sealed class SysInfoTests : IDisposable
{
    private const ulong GB = 1024UL * 1024 * 1024;
    private static readonly DateTimeOffset Now = new(2026, 10, 30, 9, 0, 0, TimeSpan.Zero);

    private readonly string _dir = Path.Combine(Path.GetTempPath(), "deskbuddy-sys-tests-" + Guid.NewGuid().ToString("N"));
    private readonly FakeClock _clock = new() { Now = Now };

    public SysInfoTests() => Directory.CreateDirectory(_dir);

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    private static SysReading Reading(ulong idle, ulong total) => new(idle, total, 32 * GB, 18 * GB, 476 * GB, 120 * GB + GB / 3);

    [Fact]
    public void The_load_comes_from_two_readings()
    {
        // 1000 ticks of processor time passed, 880 of them idle: 12 %. Memory 14 of 32 GB in use: 44 % (43.75 rounded).
        var sample = SysSample.From(Reading(10_000, 50_000), Reading(10_880, 51_000));
        Assert.Equal(new SysSample(12, 44, 75, 120), sample);
    }

    [Fact]
    public void The_processor_needs_an_earlier_reading_and_time_in_between()
    {
        Assert.Null(SysSample.From(null, Reading(10_000, 50_000)).Cpu);
        Assert.Null(SysSample.From(Reading(10_000, 50_000), Reading(10_000, 50_000)).Cpu);
        Assert.Null(SysSample.From(Reading(10_000, 50_000), Reading(9_000, 51_000)).Cpu); // counters went back
        Assert.Equal(0, SysSample.From(Reading(0, 0), Reading(1_200, 1_000)).Cpu); // more idle than time: idle
        Assert.Equal(100, SysSample.From(Reading(0, 0), Reading(0, 1_000)).Cpu);
    }

    [Fact]
    public void What_could_not_be_read_is_left_out()
    {
        var sample = SysSample.From(null, new SysReading(0, 0, 0, 0, 0, 0));
        Assert.True(sample.IsEmpty);
        Assert.Equal("sys.set state=off", SysWire.Set(sample));
        Assert.Equal("sys.set state=off", SysWire.Set(null));
    }

    [Fact]
    public void Free_space_is_whole_gb_and_capped_for_the_device()
    {
        Assert.Equal(1, SysSample.From(null, new SysReading(0, 0, 0, 0, 10 * GB, GB / 2)).DiskFreeGb); // rounded
        Assert.Equal(SysSample.DiskFreeMax,
                     SysSample.From(null, new SysReading(0, 0, 0, 0, 2_000_000 * GB, 1_500_000 * GB)).DiskFreeGb);
    }

    [Fact]
    public void The_line_and_the_window_text()
    {
        Assert.Equal("sys.set state=on cpu=12 ram=44 disk=75 disk_free=120", SysWire.Set(new SysSample(12, 44, 75, 120)));
        Assert.Equal("sys.set state=on ram=44 disk=75 disk_free=120", SysWire.Set(new SysSample(null, 44, 75, 120)));
        Assert.Equal("sys.set state=on cpu=3", SysWire.Set(new SysSample(3, null, null, 120))); // free space only with its disk
        Assert.Equal("CPU 12%  ·  RAM 44%  ·  C: 75%, 120 GB vrij", SysWire.Text(new SysSample(12, 44, 75, 120)));
    }

    [Fact]
    public void A_strip_line_goes_out_at_once_on_a_change_and_every_10_s_otherwise()
    {
        var lease = new StripLease();
        Assert.Equal("a", lease.Next(Now, "a"));
        Assert.Null(lease.Next(Now.AddSeconds(9.9), "a"));
        var changed = Now.AddSeconds(9.9);
        Assert.Equal("b", lease.Next(changed, "b"));
        Assert.Null(lease.Next(changed + StripLease.Interval - TimeSpan.FromMilliseconds(1), "b"));
        Assert.Equal("b", lease.Next(changed + StripLease.Interval, "b")); // 10 s after it went out
        lease.Reset(); // a new connection
        Assert.Equal("b", lease.Next(changed + StripLease.Interval, "b"));
    }

    [Fact]
    public void Samples_only_while_on_with_the_processor_from_the_first_one()
    {
        var reads = 0;
        var idle = 0UL;
        SysReading Read()
        {
            reads++;
            idle += 500;
            return Reading(idle, (ulong)reads * 1_000);
        }
        var sys = new SysInfoIntegration(Read, new PdcStore(_dir), _clock);
        sys.Start();
        sys.Tick();
        Assert.False(sys.Enabled);
        Assert.Equal(0, reads); // off: nothing read

        sys.SetEnabled(true);
        Assert.Null(sys.Sample); // the baseline only
        _clock.Now = Now + SysInfoIntegration.FirstSample;
        sys.Tick();
        Assert.Equal(50, sys.Sample?.Cpu); // 500 of 1000 ticks idle
        Assert.Equal(2, reads);
        _clock.Now += SysInfoIntegration.SampleInterval - TimeSpan.FromMilliseconds(1);
        sys.Tick();
        Assert.Equal(2, reads);
        _clock.Now += TimeSpan.FromMilliseconds(1);
        sys.Tick();
        Assert.Equal(3, reads);

        var again = new SysInfoIntegration(Read, new PdcStore(_dir), _clock); // the switch is kept
        again.Start();
        Assert.True(again.Enabled);
        sys.SetEnabled(false);
        Assert.Null(sys.Sample);
        _clock.Now += TimeSpan.FromMinutes(1);
        sys.Tick();
        Assert.Equal(4, reads); // again's baseline; nothing since the switch went off
    }
}
