using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace DeskBuddy.Companion.History;

/// <summary>
/// An append-only JSON-lines file. Appends are flushed to disk before returning. Reading skips lines that do not
/// parse (a line torn by a crash or power loss is at most the last one); the next append starts on a fresh line, so
/// a torn line never swallows a good one.
/// </summary>
public sealed class JsonlFile(string path)
{
    public string Path { get; } = path;

    // Times keep their '+' (the default encoder writes \u002B): the files stay readable by hand.
    internal static readonly JsonSerializerOptions Options =
        new() { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    public List<JsonObject> Read(out int skipped)
    {
        skipped = 0;
        var result = new List<JsonObject>();
        if (!File.Exists(Path))
        {
            return result;
        }
        using var reader = new StreamReader(new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite), Encoding.UTF8);
        while (reader.ReadLine() is { } line)
        {
            if (line.Length == 0)
            {
                continue;
            }
            try
            {
                if (JsonNode.Parse(line) is JsonObject o)
                {
                    result.Add(o);
                    continue;
                }
            }
            catch (JsonException)
            {
            }
            skipped++;
        }
        return result;
    }

    public void Append(IReadOnlyList<JsonObject> lines)
    {
        if (lines.Count == 0)
        {
            return;
        }
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(Path)!);
        var text = new StringBuilder();
        foreach (var o in lines)
        {
            text.Append(o.ToJsonString(Options)).Append('\n');
        }
        using var stream = new FileStream(Path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.Read);
        if (stream.Length > 0)
        {
            stream.Seek(-1, SeekOrigin.End);
            if (stream.ReadByte() != '\n')
            {
                text.Insert(0, '\n'); // the previous write was cut off: leave its fragment on a line of its own
            }
        }
        stream.Seek(0, SeekOrigin.End);
        stream.Write(Encoding.UTF8.GetBytes(text.ToString()));
        stream.Flush(flushToDisk: true);
    }
}

/// <summary>
/// The replay checkpoints, one per (dev, boot): device-checkpoints.json, replaced atomically (written to a temporary
/// file, flushed, then swapped in). Written only after the log lines it covers are on disk. On loading, the log
/// decides: a checkpoint is never trusted beyond what the log holds.
/// </summary>
public sealed class CheckpointFile(string path)
{
    public string Path { get; } = path;

    public Dictionary<(string Dev, string Boot), uint> Read(out string? problem)
    {
        problem = null;
        var result = new Dictionary<(string, string), uint>();
        if (!File.Exists(Path))
        {
            return result;
        }
        try
        {
            var root = JsonNode.Parse(File.ReadAllText(Path, Encoding.UTF8)) as JsonObject;
            foreach (var n in root?["checkpoints"] as JsonArray ?? [])
            {
                if (n is JsonObject o && o["dev"]?.GetValue<string>() is { } dev && o["boot"]?.GetValue<string>() is { } boot &&
                    o["seq"]?.GetValue<long>() is { } seq and >= 0 and <= uint.MaxValue)
                {
                    result[(dev, boot)] = (uint)seq;
                }
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException or FormatException)
        {
            problem = $"{System.IO.Path.GetFileName(Path)} unreadable ({e.Message}); the event log is used instead";
            result.Clear();
        }
        return result;
    }

    public void Write(IEnumerable<KeyValuePair<(string Dev, string Boot), uint>> checkpoints)
    {
        var array = new JsonArray();
        foreach (var ((dev, boot), seq) in checkpoints.OrderBy(c => c.Key.Dev).ThenBy(c => c.Key.Boot))
        {
            array.Add(new JsonObject { ["dev"] = dev, ["boot"] = boot, ["seq"] = seq });
        }
        var root = new JsonObject { ["v"] = HistoryCodec.Version, ["checkpoints"] = array };
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(Path)!);
        var temp = Path + ".tmp";
        using (var stream = new FileStream(temp, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            stream.Write(Encoding.UTF8.GetBytes(root.ToJsonString(JsonlFile.Options)));
            stream.Flush(flushToDisk: true);
        }
        if (File.Exists(Path))
        {
            File.Replace(temp, Path, null);
        }
        else
        {
            File.Move(temp, Path);
        }
    }
}

/// <summary>This Companion installation's stable id (installation.json), created on first use; every record carries it.</summary>
public static class Installation
{
    public static string Id(string directory)
    {
        var path = System.IO.Path.Combine(directory, "installation.json");
        try
        {
            if (File.Exists(path) && JsonNode.Parse(File.ReadAllText(path, Encoding.UTF8))?["id"]?.GetValue<string>() is { Length: > 0 } id)
            {
                return id;
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException)
        {
        }
        var created = Guid.CreateVersion7().ToString();
        Directory.CreateDirectory(directory);
        var temp = path + ".tmp";
        File.WriteAllText(temp, new JsonObject { ["v"] = 1, ["id"] = created }.ToJsonString(), Encoding.UTF8);
        File.Move(temp, path, overwrite: true);
        return created;
    }
}
