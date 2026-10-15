using System.Text;
using DeskBuddy.Companion.Protocol;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The activities started with `act name=` (water, sneeze, stare, meditate, focus): which firmware has them, what is
// sent, and where their tiles go.
public class ActivityTests
{
    private static Activity Tile(string label) => Activities.All.Single(a => a.Label == label);

    private static Frame Parse(string content)
    {
        var frame = Frame.TryParse($"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}", out var error);
        Assert.Null(error);
        Assert.NotNull(frame);
        return frame;
    }

    [Fact]
    public void Sends_act_with_the_activity_name()
    {
        Assert.Equal("act name=water", Tile("Water").Command);
        Assert.Equal("act name=sneeze", Tile("Sneeze").Command);
        Assert.Equal("act name=stare", Tile("Stare").Command);
        Assert.Equal("act name=meditate", Tile("Meditate").Command);
        Assert.Equal("act name=focus", Tile("Focus").Command);
        Assert.StartsWith(">DB1 4 act name=stare #", Frame.Command(4, Tile("Stare").Command));
    }

    [Fact]
    public void An_activity_tile_needs_the_act_cap_and_its_name_in_hello()
    {
        var hello = Parse("<DB1 1 ok proto=1 fw=4de3017 caps=poke,monster,mode,time,status,msg,mood,act styles=marquee " +
                          "moods=idle acts=water,sneeze,stare,meditate");
        var caps = (hello.Arg("caps") ?? "").Split(',').ToHashSet();
        var acts = DeviceMood.ParseList(hello.Arg("acts"));
        Assert.Equal(["water", "sneeze", "stare", "meditate"], acts);
        foreach (var label in new[] { "Water", "Sneeze", "Stare", "Meditate" })
        {
            Assert.True(Activities.IsAvailable(Tile(label), caps, [], acts));
        }
        // A firmware that has only some of them, or none (no act cap: older firmware): only those tiles work.
        Assert.False(Activities.IsAvailable(Tile("Meditate"), caps, [], ["water", "sneeze"]));
        Assert.False(Activities.IsAvailable(Tile("Focus"), caps, [], acts)); // firmware from before FOCUS
        Assert.True(Activities.IsAvailable(Tile("Focus"), caps, [], [.. acts, "focus"]));
        Assert.False(Activities.IsAvailable(Tile("Water"), new HashSet<string> { "poke", "monster" }, [], acts));
        Assert.Empty(DeviceMood.ParseList(Parse("<DB1 1 ok proto=1 caps=poke,monster moods=idle").Arg("acts")));
        Assert.True(Activities.IsAvailable(Tile("Monster"), new HashSet<string> { "monster" }, [], []));
    }

    [Fact]
    public void Tiles_are_grouped_as_on_the_device_and_show_the_activitys_face_afterwards()
    {
        Assert.Equal(["Surprise!", "Sneeze", "Stare", "Meditate", "Focus"],
                     Activities.All.Where(a => a.Kind == ActivityKind.Action).Select(a => a.Label));
        Assert.Equal(["Monster", "Water"], Activities.All.Where(a => a.Kind == ActivityKind.Food).Select(a => a.Label));
        Assert.Equal("sneeze", Tile("Sneeze").Reaction); // face-sneeze in the header
        Assert.Equal("glass", Tile("Water").Icon);
        // Activities never show as "on": they play once.
        Assert.DoesNotContain(Activities.All, a => a.Act is not null && Activities.IsOn(a, "normal", "idle"));
    }
}
