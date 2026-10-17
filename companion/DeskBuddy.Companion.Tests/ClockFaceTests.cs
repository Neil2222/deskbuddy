using System.Text;
using System.Text.Json.Nodes;
using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.Protocol;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// Clock Mode faces: the protocol bits, the settings.json round trip, and when the remembered face is sent again.
public sealed class ClockFaceTests : IDisposable
{
    private static readonly HashSet<string> Cap = ["time", "mode", "face"];
    private static readonly string[] Both = ["classic", "skeleton"];
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "deskbuddy-face-tests-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    private static Frame Parse(string content)
    {
        var frame = Frame.TryParse($"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}", out var error);
        Assert.Null(error);
        Assert.NotNull(frame);
        return frame;
    }

    // --- protocol ---------------------------------------------------------------------------------------------------

    [Fact]
    public void Reads_faces_from_hello_status_and_evt_face()
    {
        var hello = Parse("<DB1 1 ok fw=0.5.0 proto=1 caps=time,mode,face acts=water faces=classic,skeleton dev=ab12 boot=0123456789abcdef");
        Assert.Equal(["classic", "skeleton"], ClockFace.ParseList(hello.Arg("faces")));
        Assert.Empty(ClockFace.ParseList(Parse("<DB1 1 ok fw=0.4.0 proto=1 caps=time,mode").Arg("faces")));

        Assert.Equal("skeleton", ClockFace.FromStatus(Parse("<DB1 3 ok mode=office mood=idle time=set face=skeleton")));
        Assert.Null(ClockFace.FromStatus(Parse("<DB1 3 ok mode=office mood=idle time=set")));

        var evt = Parse("<DB1 0 evt face name=skeleton src=touch");
        Assert.True(evt is { IsEvent: true, Name: "face" });
        Assert.Equal("skeleton", ClockFace.FromEvent(evt));
    }

    [Fact]
    public void Sends_clock_face_with_the_name_and_names_faces_readably()
    {
        Assert.Equal("clock.face name=skeleton", ClockFace.Command("skeleton"));
        Assert.StartsWith(">DB1 5 clock.face name=classic #", Frame.Command(5, ClockFace.Command("classic")));
        Assert.Equal("Classic", ClockFace.DisplayName("classic"));
        Assert.Equal("Mechanical Skeleton", ClockFace.DisplayName("skeleton"));
        Assert.Equal("Neon Sign", ClockFace.DisplayName("neon_sign"));
        Assert.Equal("Nixie", ClockFace.DisplayName("nixie"));
    }

    [Fact]
    public void The_clock_tile_still_just_enters_clock_mode()
    {
        var clock = Activities.All.Single(a => a.Label == "Clock");
        Assert.Equal("mode.set mode=office", clock.Command);
    }

    // --- settings.json ----------------------------------------------------------------------------------------------

    [Fact]
    public void Face_settings_round_trip_and_keep_the_other_keys()
    {
        var store = new PdcStore(_dir);
        Assert.Null(store.LoadClockFace());

        store.SaveSettings(true, "jdoe");
        var saved = new SavedClockFace("skeleton", "ab12", "0123456789abcdef");
        store.SaveClockFace(saved);

        Assert.Equal(saved, new PdcStore(_dir).LoadClockFace());
        Assert.Equal((true, "jdoe"), store.LoadSettings());

        store.SaveSettings(false, "jdoe"); // the other writer keeps the face too
        Assert.Equal(saved, store.LoadClockFace());

        var root = JsonNode.Parse(File.ReadAllText(store.SettingsPath))!.AsObject();
        Assert.Equal("skeleton", (string?)root["clockFace"]);
        Assert.Equal("0123456789abcdef", (string?)root["clockFaceBoot"]);
    }

    [Fact]
    public void Face_settings_keep_unknown_keys_and_a_face_without_a_boot()
    {
        Directory.CreateDirectory(_dir);
        File.WriteAllText(Path.Combine(_dir, "settings.json"), """{"future": 7, "clockFace": "classic"}""");
        var store = new PdcStore(_dir);
        Assert.Equal(new SavedClockFace("classic", null, null), store.LoadClockFace());

        store.SaveClockFace(new SavedClockFace("skeleton", "d", "b"));
        Assert.Equal(7, (int?)JsonNode.Parse(File.ReadAllText(store.SettingsPath))!["future"]);
    }

    // --- when the remembered face is sent again ---------------------------------------------------------------------

    private static ClockFacePolicy Saved(string face, string dev = "d1", string boot = "b1") =>
        new(new SavedClockFace(face, dev, boot));

    [Fact]
    public void A_device_restart_sends_the_saved_face_again_once()
    {
        var policy = Saved("skeleton", boot: "b1");
        Assert.Equal("skeleton", policy.DeviceStarted(Cap, "d1", "b2", Both)); // hello after a restart
        Assert.Equal(new SavedClockFace("skeleton", "d1", "b2"), policy.Saved); // the new boot is recorded
        Assert.Null(policy.DeviceStarted(Cap, "d1", "b2", Both)); // an ordinary reconnect afterwards
    }

    [Fact]
    public void An_evt_boot_while_connected_sends_the_saved_face_again()
    {
        var policy = Saved("skeleton");
        Assert.Null(policy.DeviceStarted(Cap, "d1", "b1", Both));
        policy.FaceReported("skeleton");
        Assert.Equal("skeleton", policy.DeviceStarted(Cap, "d1", "b2", Both)); // evt boot dev= boot=
    }

    [Fact]
    public void An_ordinary_reconnect_or_companion_restart_sends_nothing_and_adopts_the_device_face()
    {
        var policy = Saved("skeleton");
        Assert.Null(policy.DeviceStarted(Cap, "d1", "b1", Both)); // same dev+boot: the user may have touched the screen
        policy.FaceReported("classic"); // status face= after the connect
        Assert.Equal(new SavedClockFace("classic", "d1", "b1"), policy.Saved);

        var later = new ClockFacePolicy(policy.Saved); // Companion restarted, device kept running
        Assert.Null(later.DeviceStarted(Cap, "d1", "b1", Both));
    }

    [Fact]
    public void A_touch_choice_is_adopted_and_survives_a_later_device_restart()
    {
        var policy = Saved("classic");
        Assert.Null(policy.DeviceStarted(Cap, "d1", "b1", Both));
        policy.FaceReported("skeleton"); // evt face src=touch
        Assert.Equal(new SavedClockFace("skeleton", "d1", "b1"), policy.Saved);

        Assert.Equal("skeleton", policy.DeviceStarted(Cap, "d1", "b2", Both)); // the device lost it: put back
    }

    [Fact]
    public void Another_device_counts_as_a_restart()
    {
        Assert.Equal("skeleton", Saved("skeleton").DeviceStarted(Cap, "d2", "b1", Both));
    }

    [Fact]
    public void A_face_the_device_does_not_offer_is_not_sent()
    {
        var policy = Saved("skeleton");
        Assert.Null(policy.DeviceStarted(Cap, "d1", "b2", ["classic"]));
        Assert.Equal(new SavedClockFace("skeleton", "d1", "b2"), policy.Saved);
    }

    [Fact]
    public void Nothing_saved_adopts_the_device_face()
    {
        var policy = new ClockFacePolicy(null);
        Assert.Null(policy.DeviceStarted(Cap, "d1", "b1", Both));
        Assert.Null(policy.Saved);
        policy.FaceReported("skeleton");
        Assert.Equal(new SavedClockFace("skeleton", "d1", "b1"), policy.Saved);
    }

    [Fact]
    public void Without_dev_and_boot_nothing_is_sent()
    {
        var policy = Saved("skeleton");
        Assert.Null(policy.DeviceStarted(Cap, null, null, Both));
        Assert.Null(policy.DeviceStarted(Cap, "d1", null, Both));
        Assert.Null(policy.DeviceStarted(Cap, null, "b2", Both));
        Assert.Equal(new SavedClockFace("skeleton", "d1", "b1"), policy.Saved); // untouched

        var unknownBoot = new ClockFacePolicy(new SavedClockFace("skeleton", null, null)); // saved without a boot
        Assert.Null(unknownBoot.DeviceStarted(Cap, "d1", "b2", Both));
        Assert.Equal(new SavedClockFace("skeleton", "d1", "b2"), unknownBoot.Saved);
    }

    [Fact]
    public void A_device_without_the_face_cap_does_nothing()
    {
        var policy = Saved("skeleton");
        Assert.Null(policy.DeviceStarted(new HashSet<string> { "time", "mode" }, "d1", "b2", Both));
        Assert.Equal(new SavedClockFace("skeleton", "d1", "b1"), policy.Saved); // not even the boot is recorded
    }
}
