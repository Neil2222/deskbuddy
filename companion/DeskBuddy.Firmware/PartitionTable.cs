using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace DeskBuddy.Firmware;

public sealed record Partition(string Name, byte Type, byte SubType, uint Offset, uint Size)
{
    public uint End => Offset + Size;
}

/// <summary>
/// The binary partition table ESP-IDF writes at 0x8000: 32-byte entries (magic 0xAA50), then an MD5 entry (magic
/// 0xEBEB) over the entries before it, then 0xFF up to the end of the sector.
/// </summary>
public static class PartitionTable
{
    public const int MaxSize = 0xC00;
    private const int EntrySize = 32;

    public static IReadOnlyList<Partition> Parse(ReadOnlySpan<byte> data)
    {
        if (data.Length is 0 or > MaxSize || data.Length % EntrySize != 0)
        {
            throw new FormatException($"a partition table is a multiple of 32 bytes up to {MaxSize}, not {data.Length}");
        }
        var parts = new List<Partition>();
        var md5Checked = false;
        var pos = 0;
        for (; pos < data.Length; pos += EntrySize)
        {
            var e = data.Slice(pos, EntrySize);
            if (e[0] == 0xAA && e[1] == 0x50)
            {
                if (md5Checked)
                {
                    throw new FormatException("partition entry after the MD5 entry");
                }
                var label = e.Slice(12, 16);
                var end = label.IndexOf((byte)0);
                parts.Add(new Partition(Encoding.ASCII.GetString(end < 0 ? label : label[..end]), e[2], e[3],
                    BinaryPrimitives.ReadUInt32LittleEndian(e[4..]), BinaryPrimitives.ReadUInt32LittleEndian(e[8..])));
            }
            else if (e[0] == 0xEB && e[1] == 0xEB)
            {
                if (md5Checked || !MD5.HashData(data[..pos]).AsSpan().SequenceEqual(e[16..]))
                {
                    throw new FormatException("partition table MD5 does not match");
                }
                md5Checked = true;
            }
            else if (e.IndexOfAnyExcept((byte)0xFF) < 0)
            {
                break;
            }
            else
            {
                throw new FormatException($"unknown partition table entry at 0x{pos:x}");
            }
        }
        if (data[pos..].IndexOfAnyExcept((byte)0xFF) >= 0)
        {
            throw new FormatException("data after the end of the partition table");
        }
        if (!md5Checked)
        {
            throw new FormatException("partition table has no MD5 entry");
        }
        return parts;
    }
}
