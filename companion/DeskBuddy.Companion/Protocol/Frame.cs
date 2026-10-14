using System.Text;

namespace DeskBuddy.Companion.Protocol;

/// <summary>
/// A frame from DeskBuddy (protocol version 1, docs/protocol.md; firmware side: firmware/components/link).
///   &lt;DB1 &lt;seq&gt; ok [key=value ...] #crc        reply to command &lt;seq&gt;
///   &lt;DB1 &lt;seq&gt; err &lt;code&gt; [key=value ...] #crc
///   &lt;DB1 0 evt &lt;name&gt; [key=value ...] #crc     unsolicited event
/// </summary>
public sealed record Frame(uint Seq, string Kind, string? Name, IReadOnlyDictionary<string, string> Args, string Line)
{
    public const int Version = 1;

    public bool IsOk => Kind == "ok";
    public bool IsError => Kind == "err";
    public bool IsEvent => Kind == "evt";

    public string? Arg(string key) => Args.TryGetValue(key, out var v) ? v : null;

    /// <summary>CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF ("123456789" -> 0x29B1).</summary>
    public static ushort Crc16(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (var b in data)
        {
            crc ^= (ushort)(b << 8);
            for (var i = 0; i < 8; i++)
            {
                crc = (crc & 0x8000) != 0 ? (ushort)((crc << 1) ^ 0x1021) : (ushort)(crc << 1);
            }
        }
        return crc;
    }

    /// <summary>The command line sent to the device, including the checksum and newline.</summary>
    public static string Command(uint seq, string body)
    {
        var content = $">DB{Version} {seq} {body}";
        return $"{content} #{Crc16(Encoding.ASCII.GetBytes(content)):X4}\n";
    }

    /// <summary>
    /// Parses a line from the device. Returns null for lines that are not frames (the device's log output).
    /// A line that claims to be a frame but is damaged or of another version sets <paramref name="error"/>.
    /// </summary>
    public static Frame? TryParse(string line, out string? error)
    {
        error = null;
        line = line.TrimEnd('\r');
        if (!line.StartsWith("<DB", StringComparison.Ordinal))
        {
            return null;
        }
        var hash = line.LastIndexOf(" #", StringComparison.Ordinal);
        if (hash < 0 || line.Length - hash != 6 ||
            !ushort.TryParse(line.AsSpan(hash + 2), System.Globalization.NumberStyles.HexNumber, null, out var sent))
        {
            error = "no checksum";
            return null;
        }
        var content = line[..hash];
        if (Crc16(Encoding.ASCII.GetBytes(content)) != sent)
        {
            error = "bad checksum";
            return null;
        }
        var tokens = content.Split(' ');
        if (tokens[0] != $"<DB{Version}")
        {
            error = $"unsupported protocol version '{tokens[0][1..]}'";
            return null;
        }
        if (tokens.Length < 3 || !uint.TryParse(tokens[1], out var seq))
        {
            error = "malformed frame";
            return null;
        }
        var kind = tokens[2];
        string? name = null;
        var args = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var t in tokens.Skip(3))
        {
            var eq = t.IndexOf('=');
            if (eq > 0)
            {
                args[t[..eq]] = t[(eq + 1)..];
            }
            else
            {
                name ??= t; // error code or event name
            }
        }
        return new Frame(seq, kind, name, args, line);
    }
}
