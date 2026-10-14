using DeskBuddy.Companion.Protocol;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// Frames as the firmware writes them (lines taken from a session with the device).
public class FrameTests
{
    [Fact]
    public void Checksum_matches_the_firmware()
    {
        Assert.Equal(0x29B1, Frame.Crc16("123456789"u8));
        Assert.Equal(">DB1 6 monster #3F7C\n", Frame.Command(6, "monster"));
    }

    [Fact]
    public void Parses_replies_and_events()
    {
        var reply = Frame.TryParse("<DB1 7 err busy mode=monster #7C4C\r", out var error);
        Assert.Null(error);
        Assert.NotNull(reply);
        Assert.True(reply.IsError);
        Assert.Equal(7u, reply.Seq);
        Assert.Equal("busy", reply.Name);
        Assert.Equal("monster", reply.Arg("mode"));

        var evt = Frame.TryParse("<DB1 0 evt mode mode=monster #04EC", out error);
        Assert.True(evt is { IsEvent: true, Name: "mode" });
    }

    [Fact]
    public void Tells_log_lines_from_damaged_frames()
    {
        Assert.Null(Frame.TryParse("I (6188) deskbuddy: PERF 5.2s", out var error));
        Assert.Null(error); // just log output
        Assert.Null(Frame.TryParse("<DB1 7 err busy mode=monster #7C4D", out error));
        Assert.Equal("bad checksum", error);
    }
}
