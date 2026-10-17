using DeskBuddy.Firmware;
using Xunit;
using static DeskBuddy.Companion.Tests.FirmwareTestImages;

namespace DeskBuddy.Companion.Tests;

public class FirmwareVersionTests
{
    [Theory]
    [InlineData("1.0.0")]
    [InlineData("0.0.1")]
    [InlineData("10.20.30")]
    [InlineData("1.1.0-rc.1")]
    [InlineData("1.0.0-alpha-2.x")]
    public void Release_and_pre_release_versions_parse_and_print_unchanged(string text) =>
        Assert.Equal(text, FirmwareVersion.Parse(text).ToString());

    [Theory]
    [InlineData("")]
    [InlineData("v1.0.0")]
    [InlineData("1.0")]
    [InlineData("1.0.0.0")]
    [InlineData("01.0.0")]
    [InlineData("1.0.0-")]
    [InlineData("1.0.0-rc.01")]
    [InlineData("1.0.0+57eb475")] // build metadata: the commit is reported separately
    [InlineData("57eb475")]       // what firmware before versioning reports
    [InlineData("1.0.0-dirty_tree")]
    [InlineData("1.0.0-aaaaaaaaaaaaaaaaaaaaaaaaaaaa")] // longer than the app description holds
    public void Anything_else_is_not_a_version(string text) => Assert.Null(FirmwareVersion.TryParse(text));

    [Fact]
    public void Versions_order_by_SemVer_precedence()
    {
        string[] ordered =
        [
            "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11",
            "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "1.10.0", "2.0.0",
        ];
        var versions = ordered.Select(FirmwareVersion.Parse).ToList();
        for (var i = 0; i < versions.Count; i++)
        {
            for (var j = 0; j < versions.Count; j++)
            {
                Assert.Equal(Math.Sign(i.CompareTo(j)), Math.Sign(versions[i].CompareTo(versions[j])));
            }
        }
    }
}

// Which package a device may get. Nothing is ever offered for a board or hardware other than the package's.
public class UpdatePolicyTests
{
    private static readonly FirmwareVersion Companion = FirmwareVersion.Parse("1.1.0");
    private static readonly FirmwareManifest Package = Manifest(Images("1.2.0"));
    private const string Board = "waveshare-esp32-s3-touch-lcd-7b";

    private static InstallOffer Offer(InstalledFirmware? installed, FirmwareManifest? package = null,
        string companion = "1.1.0", int protocol = 1) =>
        UpdatePolicy.Decide(package ?? Package, installed, FirmwareVersion.Parse(companion), protocol).Offer;

    [Fact]
    public void Newer_same_and_older_versions()
    {
        Assert.Equal(InstallOffer.Update, Offer(new("1.1.9", Build, Board, "ok")));
        Assert.Equal(InstallOffer.Update, Offer(new("1.2.0-rc.1", Build, Board, "ok")));
        Assert.Equal(InstallOffer.Reinstall, Offer(new("1.2.0", Build, Board, "ok")));
        Assert.Equal(InstallOffer.Downgrade, Offer(new("1.10.0", Build, Board, "ok")));
    }

    [Fact]
    public void Firmware_from_before_versioning_is_updated()
    {
        // fw=57eb475 and no info command (no board or hw known): the only firmware that ever reported that is ours.
        var d = UpdatePolicy.Decide(Package, new("57eb475", null, null, null), Companion, 1);
        Assert.Equal(InstallOffer.Update, d.Offer);
        Assert.Contains("57eb475", d.Reason);
    }

    [Fact]
    public void A_board_without_DeskBuddy_gets_an_install_offer()
    {
        Assert.Equal(InstallOffer.Install, Offer(null));
    }

    [Fact]
    public void Another_board_or_a_failed_hardware_check_is_never_offered_anything()
    {
        Assert.Equal(InstallOffer.Incompatible, Offer(new("1.0.0", Build, "simulator", "ok")));
        Assert.Equal(InstallOffer.Incompatible, Offer(new("1.0.0", Build, Board, "mismatch")));
    }

    [Fact]
    public void A_package_needing_a_newer_Companion_or_another_protocol_is_not_offered()
    {
        var needsNewer = Package with { MinCompanion = FirmwareVersion.Parse("1.3.0") };
        Assert.Equal(InstallOffer.Incompatible, Offer(new("1.0.0", Build, Board, "ok"), needsNewer));
        Assert.Equal(InstallOffer.Incompatible, Offer(null, needsNewer));
        Assert.Equal(InstallOffer.Incompatible, Offer(new("1.0.0", Build, Board, "ok"), protocol: 2));
    }
}
