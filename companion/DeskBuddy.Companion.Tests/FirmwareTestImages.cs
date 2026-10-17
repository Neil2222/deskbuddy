using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using DeskBuddy.Firmware;
using Xunit;

namespace DeskBuddy.Companion.Tests;

/// <summary>
/// Small but well-formed ESP images and partition tables in the layout ESP-IDF writes, and packages made of them,
/// signed with keys created per test run (no key is stored anywhere).
/// </summary>
internal static class FirmwareTestImages
{
    public const string Build = "0123456789abcdef0123456789abcdef01234567";

    public static byte[] Image(ushort chipId = EspImage.ChipIdEsp32S3, string? appVersion = null,
        string project = "deskbuddy", string idf = "v5.5.5", string build = Build, byte mode = 2, byte sizeFreq = 0x4F)
    {
        var segments = new List<byte[]>();
        if (appVersion is not null)
        {
            var desc = new byte[256];
            BinaryPrimitives.WriteUInt32LittleEndian(desc, 0xABCD5432);
            Encoding.ASCII.GetBytes(appVersion).CopyTo(desc, 16);
            Encoding.ASCII.GetBytes(project).CopyTo(desc, 48);
            Encoding.ASCII.GetBytes(idf).CopyTo(desc, 112);
            segments.Add(desc);
            segments.Add(Encoding.ASCII.GetBytes($"build=%s board=%s\0{build}\0more rodata\0")); // as port.c's strings
        }
        else
        {
            segments.Add(Enumerable.Range(0, 100).Select(i => (byte)i).ToArray()); // bootloader code
        }
        using var s = new MemoryStream();
        var header = new byte[24];
        header[0] = EspImage.Magic;
        header[1] = (byte)segments.Count;
        header[2] = mode;
        header[3] = sizeFreq;
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(12), chipId);
        header[23] = 1; // SHA-256 appended
        s.Write(header);
        byte checksum = 0xEF;
        foreach (var seg in segments)
        {
            var sh = new byte[8];
            BinaryPrimitives.WriteUInt32LittleEndian(sh.AsSpan(4), (uint)seg.Length);
            s.Write(sh);
            s.Write(seg);
            foreach (var b in seg)
            {
                checksum ^= b;
            }
        }
        s.Write(new byte[15 - s.Length % 16]);
        s.WriteByte(checksum);
        s.Write(SHA256.HashData(s.ToArray()));
        return s.ToArray();
    }

    public static byte[] Table(IEnumerable<Partition> parts)
    {
        var data = Enumerable.Repeat((byte)0xFF, 0xC00).ToArray();
        var pos = 0;
        foreach (var p in parts)
        {
            data[pos] = 0xAA;
            data[pos + 1] = 0x50;
            data[pos + 2] = p.Type;
            data[pos + 3] = p.SubType;
            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(pos + 4), p.Offset);
            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(pos + 8), p.Size);
            Array.Clear(data, pos + 12, 20);
            Encoding.ASCII.GetBytes(p.Name).CopyTo(data, pos + 12);
            pos += 32;
        }
        data[pos] = 0xEB;
        data[pos + 1] = 0xEB;
        MD5.HashData(data.AsSpan(0, pos)).CopyTo(data, pos + 16);
        return data;
    }

    /// <summary>The images of a valid package for the 7B, by package file name.</summary>
    public static Dictionary<string, byte[]> Images(string version = "1.0.0") => new()
    {
        ["bootloader.bin"] = Image(),
        ["partition-table.bin"] = Table(BoardProfile.WaveshareS3Lcd7B.Partitions),
        ["deskbuddy.bin"] = Image(appVersion: version),
    };

    public static FirmwareManifest Manifest(Dictionary<string, byte[]> images) =>
        PackageBuilder.CreateManifest(BoardProfile.WaveshareS3Lcd7B, images, Build, 1, FirmwareVersion.Parse("1.1.0"));

    /// <summary>
    /// A package exactly as given, signed: manifest bytes and files are not checked here, so tests can sign packages
    /// that are wrong in one way and see that the check catches it.
    /// </summary>
    public static byte[] Signed(byte[] manifestJson, IReadOnlyDictionary<string, byte[]> files, string privateKeyPem) =>
        FirmwarePackage.WriteZip(manifestJson, PackageSignature.Sign(manifestJson, privateKeyPem),
            files.Select(f => (f.Key, f.Value)));

    /// <summary>A manifest with one change: the JSON is edited as text after it was made from good images.</summary>
    public static byte[] Edit(FirmwareManifest m, string from, string to)
    {
        var json = Encoding.UTF8.GetString(m.ToJson());
        Assert.Contains(from, json);
        return Encoding.UTF8.GetBytes(json.Replace(from, to));
    }
}
