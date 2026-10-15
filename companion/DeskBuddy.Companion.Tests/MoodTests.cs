using System.Text;
using DeskBuddy.Companion.Protocol;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// Buddy's mood as the companion reads it from hello / status / evt mood, and which tile shows it.
public class MoodTests
{
    private static Frame Parse(string content)
    {
        var frame = Frame.TryParse($"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}", out var error);
        Assert.Null(error);
        Assert.NotNull(frame);
        return frame;
    }

    private static Activity Tile(string label) => Activities.All.Single(a => a.Label == label);

    [Fact]
    public void Reads_the_mood_from_status_and_from_evt_mood()
    {
        var status = Parse("<DB1 3 ok mode=normal mood=working time=set msg=idle queued=0 uptime=12");
        Assert.Equal("working", DeviceMood.From(status, DeviceMood.Idle));

        var evt = Parse("<DB1 0 evt mood mood=sleepy");
        Assert.True(evt is { IsEvent: true, Name: "mood" });
        Assert.Equal("sleepy", DeviceMood.From(evt, "working"));

        // Firmware without moods: the mood stays as it was.
        Assert.Equal(DeviceMood.Idle, DeviceMood.From(Parse("<DB1 4 ok mode=office time=set"), DeviceMood.Idle));
    }

    [Fact]
    public void Reads_the_supported_moods_from_hello()
    {
        var hello = Parse("<DB1 1 ok fw=0.4.0 proto=1 caps=time,mode,poke,monster,msg,mood moods=idle,relaxed,peukie,angry,sleepy,working,happy");
        Assert.Equal(["idle", "relaxed", "peukie", "angry", "sleepy", "working", "happy"], DeviceMood.ParseList(hello.Arg("moods")));
        Assert.Empty(DeviceMood.ParseList(Parse("<DB1 1 ok fw=0.3.0 proto=1 caps=time,mode").Arg("moods")));
    }

    [Fact]
    public void Sends_mood_set_with_the_mood_name()
    {
        Assert.Equal("mood.set name=angry", Tile("Angry").Command);
        Assert.StartsWith(">DB1 9 mood.set name=idle #", Frame.Command(9, DeviceMood.Command("idle")));
    }

    [Fact]
    public void Only_the_current_moods_tile_is_on_and_modes_are_unaffected()
    {
        var on = Activities.All.Where(a => Activities.IsOn(a, "office", "working")).Select(a => a.Label);
        Assert.Equal(["Clock", "Working"], on);
        Assert.Equal(["Buddy", "Idle"], Activities.All.Where(a => Activities.IsOn(a, "monster", "idle")).Select(a => a.Label));
        Assert.DoesNotContain(Activities.All, a => a.Kind == ActivityKind.Mood && Activities.IsOn(a, "normal", "grumpy"));
    }

    [Fact]
    public void A_mood_tile_needs_the_mood_cap_and_the_mood_in_hello()
    {
        var caps = new HashSet<string> { "mode", "poke", "monster", "mood" };
        IReadOnlyList<string> moods = ["idle", "sleepy", "happy"];
        Assert.True(Activities.IsAvailable(Tile("Sleepy"), caps, moods, []));
        Assert.False(Activities.IsAvailable(Tile("Angry"), caps, moods, [])); // not in moods=
        Assert.False(Activities.IsAvailable(Tile("Sleepy"), new HashSet<string> { "mode" }, moods, [])); // no mood cap
        Assert.True(Activities.IsAvailable(Tile("Monster"), caps, [], [])); // actions do not need moods=
    }

    [Fact]
    public void Header_face_follows_the_mood()
    {
        Assert.Equal("face-working", Activities.FaceFor("working"));
        Assert.Equal("face-neutral", Activities.FaceFor(DeviceMood.Idle));
        Assert.Equal("face-neutral", Activities.FaceFor("grumpy")); // a mood this companion does not know
    }
}
