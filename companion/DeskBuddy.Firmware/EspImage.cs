using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace DeskBuddy.Firmware;

/// <summary>
/// An ESP32 flash image (the second-stage bootloader or an application) as ESP-IDF writes it: the 24-byte header, the
/// segments, a one-byte checksum on a 16-byte boundary, and (when the header says so) a SHA-256 of everything before
/// it. Checked here byte for byte, so a damaged or truncated file is caught even before its package hash is compared.
/// </summary>
public sealed class EspImage
{
    public const byte Magic = 0xE9;
    public const ushort ChipIdEsp32S3 = 9;
    private const uint AppDescMagic = 0xABCD5432;
    private const int HeaderSize = 24;
    private const int SegmentHeaderSize = 8;

    /// <summary>SPI flash mode byte as in the header: 0 QIO, 1 QOUT, 2 DIO, 3 DOUT.</summary>
    public byte FlashMode { get; }
    /// <summary>High nibble of header byte 3: 0 1MB, 1 2MB, 2 4MB, 3 8MB, 4 16MB...</summary>
    public byte FlashSizeCode { get; }
    /// <summary>Low nibble of header byte 3: 0xF is 80 MHz.</summary>
    public byte FlashFreqCode { get; }
    public ushort ChipId { get; }
    public int SegmentCount { get; }
    /// <summary>The application description (null for a bootloader, which has none).</summary>
    public AppDescription? App { get; }

    private EspImage(byte mode, byte sizeFreq, ushort chip, int segments, AppDescription? app)
    {
        FlashMode = mode;
        FlashSizeCode = (byte)(sizeFreq >> 4);
        FlashFreqCode = (byte)(sizeFreq & 0x0F);
        ChipId = chip;
        SegmentCount = segments;
        App = app;
    }

    public string FlashModeName => FlashMode switch { 0 => "qio", 1 => "qout", 2 => "dio", 3 => "dout", _ => "unknown" };
    public string FlashSizeName => FlashSizeCode <= 6 ? $"{1 << FlashSizeCode}MB" : "unknown";
    public string FlashFreqName => FlashFreqCode switch { 0x0 => "40m", 0x1 => "26m", 0x2 => "20m", 0xF => "80m", _ => "unknown" };

    /// <summary>
    /// Parses and checks a whole image file. <paramref name="expectApp"/>: the first segment must start with the
    /// application description (an app image), otherwise it must not (a bootloader).
    /// </summary>
    public static EspImage Parse(ReadOnlySpan<byte> data, bool expectApp)
    {
        if (data.Length < HeaderSize + SegmentHeaderSize || data[0] != Magic)
        {
            throw new FormatException("not an ESP image (no 0xE9 header)");
        }
        int segments = data[1];
        if (segments is 0 or > 16)
        {
            throw new FormatException($"implausible segment count {segments}");
        }
        var chip = BinaryPrimitives.ReadUInt16LittleEndian(data[12..]);
        var hashAppended = data[23] == 1;

        var pos = HeaderSize;
        byte checksum = 0xEF;
        AppDescription? app = null;
        for (var i = 0; i < segments; i++)
        {
            if (pos + SegmentHeaderSize > data.Length)
            {
                throw new FormatException("truncated segment header");
            }
            var length = BinaryPrimitives.ReadUInt32LittleEndian(data[(pos + 4)..]);
            pos += SegmentHeaderSize;
            if (length > (uint)(data.Length - pos))
            {
                throw new FormatException("truncated segment");
            }
            var segment = data.Slice(pos, (int)length);
            if (i == 0 && segment.Length >= 256 && BinaryPrimitives.ReadUInt32LittleEndian(segment) == AppDescMagic)
            {
                app = AppDescription.Parse(segment);
            }
            foreach (var b in segment)
            {
                checksum ^= b;
            }
            pos += (int)length;
        }
        pos += 15 - pos % 16; // zero padding, then the checksum as the last byte of a 16-byte block
        if (pos >= data.Length)
        {
            throw new FormatException("truncated image (no checksum)");
        }
        if (data[pos] != checksum)
        {
            throw new FormatException("image checksum does not match");
        }
        pos++;
        if (hashAppended)
        {
            if (pos + 32 > data.Length || !SHA256.HashData(data[..pos]).AsSpan().SequenceEqual(data.Slice(pos, 32)))
            {
                throw new FormatException("image SHA-256 does not match");
            }
            pos += 32;
        }
        if (pos != data.Length)
        {
            throw new FormatException($"{data.Length - pos} unexpected bytes after the image");
        }
        if (expectApp != app is not null)
        {
            throw new FormatException(expectApp ? "no application description" : "unexpected application description");
        }
        return new EspImage(data[2], data[3], chip, segments, app);
    }
}

/// <summary>esp_app_desc_t: what ESP-IDF stores at the start of an application.</summary>
public sealed record AppDescription(string Version, string ProjectName, string Time, string Date, string IdfVersion)
{
    internal static AppDescription Parse(ReadOnlySpan<byte> d) => new(
        Text(d.Slice(16, 32)), Text(d.Slice(48, 32)), Text(d.Slice(80, 16)), Text(d.Slice(96, 16)), Text(d.Slice(112, 32)));

    private static string Text(ReadOnlySpan<byte> field)
    {
        var end = field.IndexOf((byte)0);
        return Encoding.ASCII.GetString(end < 0 ? field : field[..end]);
    }
}
