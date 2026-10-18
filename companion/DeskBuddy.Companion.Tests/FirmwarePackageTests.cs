using System.IO.Compression;
using System.Security.Cryptography;
using DeskBuddy.Firmware;
using Xunit;
using static DeskBuddy.Companion.Tests.FirmwareTestImages;

namespace DeskBuddy.Companion.Tests;

// A firmware package is accepted only if it is signed by a trusted key and every byte is what that signature
// vouches for and what the board's fixed layout allows. Each test below breaks one thing and expects that exact refusal.
public class FirmwarePackageTests
{
    private static readonly (string PrivatePem, string PublicPem) Trusted = PackageSignature.CreateKeyPair();
    private static readonly (string PrivatePem, string PublicPem) Other = PackageSignature.CreateKeyPair();
    private static readonly IReadOnlyList<TrustedKey> Keys = [TrustedKey.FromPem("test key", Trusted.PublicPem)];

    private static FirmwarePackage Open(byte[] zip) => FirmwarePackage.Open(new MemoryStream(zip), Keys);

    private static PackageError Refused(byte[] zip) => Assert.Throws<FirmwarePackageException>(() => Open(zip)).Error;

    private static byte[] Good(out FirmwareManifest manifest, out Dictionary<string, byte[]> images)
    {
        images = Images();
        manifest = Manifest(images);
        return Signed(manifest.ToJson(), images, Trusted.PrivatePem);
    }

    [Fact]
    public void A_signed_package_opens_with_its_images_in_flash_order_byte_for_byte()
    {
        var zip = Good(out var manifest, out var images);
        var package = Open(zip);
        Assert.Equal("1.0.0", package.Manifest.Version.ToString());
        Assert.Equal(Build, package.Manifest.Build);
        Assert.Equal("v5.5.5", package.Manifest.Idf);
        Assert.Equal([0x0u, 0x8000u, 0x10000u], package.Images.Select(i => i.Slot.Offset));
        foreach (var image in package.Images)
        {
            Assert.Equal(images[image.Slot.File], image.Data);
        }
        Assert.Equal(manifest.ToJson(), package.Manifest.ToJson());
    }

    [Fact]
    public void The_preserved_regions_are_nvs_and_phy_init_and_no_image_touches_them()
    {
        var package = Open(Good(out _, out _));
        Assert.Equal([(0x9000u, 0x6000u), (0xF000u, 0x1000u)], package.Manifest.Preserve.Select(p => (p.Offset, p.Size)));
        foreach (var image in package.Images)
        {
            var end = image.Slot.Offset + (uint)Math.Ceiling(image.Data.Length / 4096.0) * 4096;
            Assert.True(end <= 0x9000 || image.Slot.Offset >= 0x10000, $"{image.Slot.File} reaches into 0x9000-0xFFFF");
        }
    }

    [Fact]
    public void An_unsigned_package_is_refused()
    {
        var images = Images();
        var manifest = Manifest(images);
        var zip = FirmwarePackage.WriteZip(manifest.ToJson(), null, images.Select(f => (f.Key, f.Value)));
        Assert.Equal(PackageError.Unsigned, Refused(zip));
    }

    [Fact]
    public void A_package_signed_by_an_untrusted_key_is_refused()
    {
        var images = Images();
        Assert.Equal(PackageError.SignatureInvalid, Refused(Signed(Manifest(images).ToJson(), images, Other.PrivatePem)));
    }

    [Fact]
    public void A_manifest_changed_after_signing_is_refused()
    {
        var images = Images();
        var manifest = Manifest(images);
        var signature = PackageSignature.Sign(manifest.ToJson(), Trusted.PrivatePem);
        var changed = Edit(manifest, "\"minCompanion\": \"1.1.0\"", "\"minCompanion\": \"1.0.0\"");
        var zip = FirmwarePackage.WriteZip(changed, signature, images.Select(f => (f.Key, f.Value)));
        Assert.Equal(PackageError.SignatureInvalid, Refused(zip));
    }

    [Fact]
    public void A_damaged_signature_is_refused()
    {
        var images = Images();
        var manifest = Manifest(images).ToJson();
        var signature = PackageSignature.Sign(manifest, Trusted.PrivatePem);
        signature[^1] ^= 1;
        Assert.Equal(PackageError.SignatureInvalid,
            Refused(FirmwarePackage.WriteZip(manifest, signature, images.Select(f => (f.Key, f.Value)))));
        Assert.Equal(PackageError.SignatureInvalid,
            Refused(FirmwarePackage.WriteZip(manifest, [1, 2, 3], images.Select(f => (f.Key, f.Value)))));
    }

    [Fact]
    public void An_image_changed_after_signing_is_refused()
    {
        var images = Images();
        var manifest = Manifest(images);
        images["deskbuddy.bin"][40] ^= 0x01;
        Assert.Equal(PackageError.FileMismatch, Refused(Signed(manifest.ToJson(), images, Trusted.PrivatePem)));
    }

    [Fact]
    public void Missing_and_extra_files_are_refused()
    {
        var images = Images();
        var manifest = Manifest(images).ToJson();
        var missing = images.Where(f => f.Key != "bootloader.bin").ToDictionary();
        Assert.Equal(PackageError.FileMismatch, Refused(Signed(manifest, missing, Trusted.PrivatePem)));
        var extra = new Dictionary<string, byte[]>(images) { ["otadata.bin"] = new byte[0x2000] };
        Assert.Equal(PackageError.FileMismatch, Refused(Signed(manifest, extra, Trusted.PrivatePem)));
    }

    [Fact]
    public void A_manifest_this_Companion_does_not_understand_is_refused_even_when_signed()
    {
        var images = Images();
        var manifest = Manifest(images);
        foreach (var (from, to) in new[]
        {
            ("\"schema\": 1", "\"schema\": 2"),
            ("\"schema\": 1", "\"schema\": 1,\n  \"eraseAll\": true"),
            ("\"version\": \"1.0.0\"", "\"version\": \"v1.0.0\""),
            ($"\"build\": \"{Build}\"", "\"build\": \"0123456\""),
            ("\"offset\": \"0x10000\"", "\"offset\": \"65536\""),
        })
        {
            var zip = Signed(Edit(manifest, from, to), images, Trusted.PrivatePem);
            Assert.Equal(PackageError.ManifestInvalid, Refused(zip));
        }
        Assert.Equal(PackageError.ManifestInvalid, Refused(Signed("not json"u8.ToArray(), images, Trusted.PrivatePem)));
    }

    [Fact]
    public void A_package_for_another_board_is_refused()
    {
        var images = Images();
        var zip = Signed(Edit(Manifest(images), "waveshare-esp32-s3-touch-lcd-7b", "waveshare-esp32-s3-touch-lcd-4.3b"),
            images, Trusted.PrivatePem);
        Assert.Equal(PackageError.UnsupportedBoard, Refused(zip));
    }

    [Fact]
    public void A_package_that_writes_elsewhere_is_refused()
    {
        var images = Images();
        var manifest = Manifest(images);
        // The app moved onto the NVS region, or the NVS region no longer listed as kept.
        Assert.Equal(PackageError.LayoutMismatch,
            Refused(Signed(Edit(manifest, "\"offset\": \"0x10000\"", "\"offset\": \"0x9000\""), images, Trusted.PrivatePem)));
        Assert.Equal(PackageError.LayoutMismatch,
            Refused(Signed(Edit(manifest, "\"offset\": \"0x9000\"", "\"offset\": \"0xa000\""), images, Trusted.PrivatePem)));
    }

    [Fact]
    public void A_different_partition_table_is_refused_even_when_consistently_hashed_and_signed()
    {
        var p = BoardProfile.WaveshareS3Lcd7B.Partitions;
        foreach (var layout in new[]
        {
            // OTA slots added (the planned layout needs its own approved migration)
            new[] { p[0], p[1], new Partition("otadata", 0x01, 0x00, 0x10000, 0x2000), new Partition("ota_0", 0x00, 0x10, 0x20000, 0x300000) },
            // a bigger app partition
            new[] { p[0], p[1], p[2] with { Size = 0x800000 } },
            // NVS moved
            new[] { p[0] with { Offset = 0xA000 }, p[1], p[2] },
        })
        {
            var images = Images();
            images["partition-table.bin"] = Table(layout);
            var manifest = new FirmwareManifest(1, "deskbuddy", FirmwareVersion.Parse("1.0.0"), Build,
                BoardProfile.WaveshareS3Lcd7B.Board, "esp32s3", "16MB", "dio", "80m", 1, FirmwareVersion.Parse("1.1.0"), "v5.5.5",
                BoardProfile.WaveshareS3Lcd7B.Images.Select(s => new ManifestImage(s.Name, s.Offset, s.File,
                    images[s.File].Length, Convert.ToHexStringLower(SHA256.HashData(images[s.File])))).ToList(),
                BoardProfile.WaveshareS3Lcd7B.Preserved, []);
            Assert.Equal(PackageError.LayoutMismatch, Refused(Signed(manifest.ToJson(), images, Trusted.PrivatePem)));
        }
    }

    [Fact]
    public void Images_for_another_chip_or_flash_or_damaged_ones_are_refused()
    {
        foreach (var bootloader in new[]
        {
            Image(chipId: 0),           // ESP32
            Image(sizeFreq: 0x3F),      // 8 MB flash
            Image(mode: 0),             // QIO
            Corrupt(Image()),           // checksum and hash no longer match
            Image()[..^10],             // truncated
        })
        {
            var images = Images();
            images["bootloader.bin"] = bootloader;
            Assert.Equal(PackageError.ImageInvalid, Refused(Consistent(images)));
        }
    }

    [Fact]
    public void The_app_must_be_the_version_and_build_the_manifest_names()
    {
        var images = Images();
        images["deskbuddy.bin"] = Image(appVersion: "1.0.1");
        Assert.Equal(PackageError.BuildMismatch, Refused(Consistent(images))); // the manifest says 1.0.0

        images["deskbuddy.bin"] = Image(appVersion: "1.0.0", build: "fedcba9876543210fedcba9876543210fedcba98");
        Assert.Equal(PackageError.BuildMismatch, Refused(Consistent(images)));

        images["deskbuddy.bin"] = Image(appVersion: "1.0.0", project: "rtc_pcf85063"); // the factory demo
        Assert.Equal(PackageError.BuildMismatch, Refused(Consistent(images)));
    }

    [Fact]
    public void A_development_build_without_a_release_version_cannot_be_packaged()
    {
        var images = Images();
        images["deskbuddy.bin"] = Image(appVersion: "57eb475");
        Assert.Equal(PackageError.BuildMismatch, Assert.Throws<FirmwarePackageException>(() => Manifest(images)).Error);
        images["deskbuddy.bin"] = Image(appVersion: "1.0.0", build: "unknown");
        Assert.Equal(PackageError.BuildMismatch, Assert.Throws<FirmwarePackageException>(() => Manifest(images)).Error);
        images["deskbuddy.bin"] = Image(appVersion: "1.0.0", build: Build + "-dirty"); // built with uncommitted changes
        Assert.Equal(PackageError.BuildMismatch, Assert.Throws<FirmwarePackageException>(() => Manifest(images)).Error);
    }

    [Fact]
    public void Files_that_are_not_flat_small_zips_are_refused()
    {
        Assert.Equal(PackageError.NotAPackage, Refused("not a zip at all"u8.ToArray()));
        Assert.Equal(PackageError.NotAPackage, Refused(Zip(("../manifest.json", [1]))));
        Assert.Equal(PackageError.NotAPackage, Refused(Zip(("manifest.json", [1]), ("manifest.json", [2]))));
        Assert.Equal(PackageError.NotAPackage, Refused(Zip(("readme.txt", [1]))));
    }

    [Fact]
    public void Packing_the_same_content_twice_gives_the_same_file()
    {
        var images = Images();
        var manifest = Manifest(images).ToJson();
        var a = FirmwarePackage.WriteZip(manifest, null, images.Select(f => (f.Key, f.Value)));
        var b = FirmwarePackage.WriteZip(manifest, null, images.Select(f => (f.Key, f.Value)));
        Assert.Equal(a, b);
    }

    [Fact]
    public void A_public_key_must_be_P256()
    {
        using var p384 = ECDsa.Create(ECCurve.NamedCurves.nistP384);
        Assert.Throws<FormatException>(() => TrustedKey.FromPem("p384", p384.ExportSubjectPublicKeyInfoPem()));
        Assert.Throws<FormatException>(() => TrustedKey.FromPem("junk", "-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----"));
    }

    // A manifest that matches the given (possibly wrong) images byte for byte, signed: only the content check can refuse it.
    private static byte[] Consistent(Dictionary<string, byte[]> images)
    {
        var profile = BoardProfile.WaveshareS3Lcd7B;
        var manifest = new FirmwareManifest(1, "deskbuddy", FirmwareVersion.Parse("1.0.0"), Build, profile.Board, "esp32s3",
            "16MB", "dio", "80m", 1, FirmwareVersion.Parse("1.1.0"), "v5.5.5",
            profile.Images.Select(s => new ManifestImage(s.Name, s.Offset, s.File, images[s.File].Length,
                Convert.ToHexStringLower(SHA256.HashData(images[s.File])))).ToList(),
            profile.Preserved, []);
        return Signed(manifest.ToJson(), images, Trusted.PrivatePem);
    }

    private static byte[] Corrupt(byte[] image)
    {
        image[40] ^= 0xFF; // a byte of the first segment: the checksum and the SHA-256 no longer match
        return image;
    }

    private static byte[] Zip(params (string Name, byte[] Data)[] entries)
    {
        using var buffer = new MemoryStream();
        using (var zip = new ZipArchive(buffer, ZipArchiveMode.Create, leaveOpen: true))
        {
            foreach (var (name, data) in entries)
            {
                using var s = zip.CreateEntry(name).Open();
                s.Write(data);
            }
        }
        return buffer.ToArray();
    }
}
