using System.Globalization;
using System.Text;
using System.Text.Json;

namespace DeskBuddy.Firmware;

public sealed record ManifestImage(string Name, uint Offset, string File, int Size, string Sha256);

/// <summary>
/// manifest.json of a firmware package (docs/firmware-packages.md). Parsed strictly: every field is required, unknown
/// fields are refused (the manifest is signed; a field this Companion does not understand could change what the
/// package means), numbers and hashes have one spelling.
/// </summary>
public sealed record FirmwareManifest(
    int Schema,
    string Product,
    FirmwareVersion Version,
    string Build,
    string Board,
    string Chip,
    string FlashSize,
    string FlashMode,
    string FlashFreq,
    int Protocol,
    FirmwareVersion MinCompanion,
    string Idf,
    IReadOnlyList<ManifestImage> Images,
    IReadOnlyList<Partition> Preserve)
{
    public const int CurrentSchema = 1;
    public const string ProductName = "deskbuddy";

    public static FirmwareManifest Parse(ReadOnlySpan<byte> json)
    {
        JsonDocument doc;
        try
        {
            doc = JsonDocument.Parse(json.ToArray(), new JsonDocumentOptions { MaxDepth = 8 });
        }
        catch (JsonException e)
        {
            throw new FormatException($"manifest is not JSON: {e.Message}");
        }
        using (doc)
        {
            var root = Object(doc.RootElement, "manifest",
                "schema", "product", "version", "build", "board", "chip", "flash", "protocol", "minCompanion", "idf",
                "images", "preserve");
            var schema = Int(root, "schema");
            if (schema != CurrentSchema)
            {
                throw new FormatException($"manifest schema {schema} is not supported (this Companion reads {CurrentSchema})");
            }
            var flash = Object(root.GetProperty("flash"), "flash", "size", "mode", "freq");
            var build = Str(root, "build");
            if (build.Length != 40 || !build.All(c => char.IsAsciiDigit(c) || c is >= 'a' and <= 'f'))
            {
                throw new FormatException("build is not a full lower-case Git commit (40 hex digits)");
            }
            var images = new List<ManifestImage>();
            foreach (var e in Array(root, "images"))
            {
                var o = Object(e, "image", "name", "offset", "file", "size", "sha256");
                var sha = Str(o, "sha256");
                if (sha.Length != 64 || !sha.All(c => char.IsAsciiDigit(c) || c is >= 'a' and <= 'f'))
                {
                    throw new FormatException("sha256 is not 64 lower-case hex digits");
                }
                images.Add(new ManifestImage(Str(o, "name"), Hex(o, "offset"), Str(o, "file"), Int(o, "size"), sha));
            }
            var preserve = new List<Partition>();
            foreach (var e in Array(root, "preserve"))
            {
                var o = Object(e, "preserve", "name", "offset", "size");
                preserve.Add(new Partition(Str(o, "name"), 0, 0, Hex(o, "offset"), Hex(o, "size")));
            }
            return new FirmwareManifest(
                schema,
                Str(root, "product"),
                FirmwareVersion.TryParse(Str(root, "version")) ?? throw new FormatException("version is not a version"),
                build,
                Str(root, "board"),
                Str(root, "chip"),
                Str(flash, "size"),
                Str(flash, "mode"),
                Str(flash, "freq"),
                Int(root, "protocol"),
                FirmwareVersion.TryParse(Str(root, "minCompanion")) ?? throw new FormatException("minCompanion is not a version"),
                Str(root, "idf"),
                images,
                preserve);
        }
    }

    /// <summary>The manifest as the package stores it (and as it is signed): indented, LF line ends, UTF-8.</summary>
    public byte[] ToJson()
    {
        using var buffer = new MemoryStream();
        using (var w = new Utf8JsonWriter(buffer, new JsonWriterOptions { Indented = true, NewLine = "\n" }))
        {
            w.WriteStartObject();
            w.WriteNumber("schema", Schema);
            w.WriteString("product", Product);
            w.WriteString("version", Version.ToString());
            w.WriteString("build", Build);
            w.WriteString("board", Board);
            w.WriteString("chip", Chip);
            w.WriteStartObject("flash");
            w.WriteString("size", FlashSize);
            w.WriteString("mode", FlashMode);
            w.WriteString("freq", FlashFreq);
            w.WriteEndObject();
            w.WriteNumber("protocol", Protocol);
            w.WriteString("minCompanion", MinCompanion.ToString());
            w.WriteString("idf", Idf);
            w.WriteStartArray("images");
            foreach (var i in Images)
            {
                w.WriteStartObject();
                w.WriteString("name", i.Name);
                w.WriteString("offset", ToHex(i.Offset));
                w.WriteString("file", i.File);
                w.WriteNumber("size", i.Size);
                w.WriteString("sha256", i.Sha256);
                w.WriteEndObject();
            }
            w.WriteEndArray();
            w.WriteStartArray("preserve");
            foreach (var p in Preserve)
            {
                w.WriteStartObject();
                w.WriteString("name", p.Name);
                w.WriteString("offset", ToHex(p.Offset));
                w.WriteString("size", ToHex(p.Size));
                w.WriteEndObject();
            }
            w.WriteEndArray();
            w.WriteEndObject();
        }
        buffer.WriteByte((byte)'\n');
        return buffer.ToArray();
    }

    public static string ToHex(uint value) => "0x" + value.ToString("x", CultureInfo.InvariantCulture);

    private static JsonElement Object(JsonElement e, string what, params string[] keys)
    {
        if (e.ValueKind != JsonValueKind.Object)
        {
            throw new FormatException($"{what} is not an object");
        }
        var seen = new HashSet<string>();
        foreach (var p in e.EnumerateObject())
        {
            if (!keys.Contains(p.Name))
            {
                throw new FormatException($"{what} has an unknown field '{p.Name}'");
            }
            if (!seen.Add(p.Name))
            {
                throw new FormatException($"{what} has '{p.Name}' twice");
            }
        }
        foreach (var k in keys)
        {
            if (!seen.Contains(k))
            {
                throw new FormatException($"{what} has no '{k}'");
            }
        }
        return e;
    }

    private static string Str(JsonElement o, string key)
    {
        var v = o.GetProperty(key);
        if (v.ValueKind != JsonValueKind.String || v.GetString() is not { Length: > 0 and <= 128 } s ||
            s.Any(c => c < 0x20 || c > 0x7E))
        {
            throw new FormatException($"'{key}' is not a short printable ASCII string");
        }
        return s;
    }

    private static int Int(JsonElement o, string key)
    {
        var v = o.GetProperty(key);
        if (v.ValueKind != JsonValueKind.Number || !v.TryGetInt32(out var n) || n < 0)
        {
            throw new FormatException($"'{key}' is not a non-negative integer");
        }
        return n;
    }

    private static uint Hex(JsonElement o, string key)
    {
        var s = Str(o, key);
        if (!s.StartsWith("0x", StringComparison.Ordinal) || s.Length is < 3 or > 10 ||
            !s[2..].All(c => char.IsAsciiDigit(c) || c is >= 'a' and <= 'f') ||
            (s.Length > 3 && s[2] == '0') ||
            !uint.TryParse(s.AsSpan(2), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var value))
        {
            throw new FormatException($"'{key}' is not a lower-case hex number like 0x10000");
        }
        return value;
    }

    private static JsonElement.ArrayEnumerator Array(JsonElement o, string key)
    {
        var v = o.GetProperty(key);
        if (v.ValueKind != JsonValueKind.Array || v.GetArrayLength() > 16)
        {
            throw new FormatException($"'{key}' is not a short array");
        }
        return v.EnumerateArray();
    }
}
