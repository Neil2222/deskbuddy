using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace DeskBuddy.Companion.Schedule;

/// <summary>
/// The schedule on disk: schedule.json in the companion's folder (%APPDATA%\DeskBuddy by default), readable and
/// editable by hand:
///   {"version": 2, "live": false, "paused_until": null,
///    "days": {"mon": [{"id": "…", "name": "Smoke break", "start": "10:00", "end": "10:12", "mood": "peukie",
///                      "plans": "smoke", "action": "ask_peukie", "reminder": false, "enabled": true}], "tue": [], …}}
/// <c>live</c>: the Companion applies it to Buddy (absent, as in version 1 files: off; version 1's "enabled" is not
/// taken over, so nothing goes live without the user switching it on). <c>paused_until</c>: an ISO time, "resume"
/// (until resumed) or null.
/// Written atomically; the previous version is kept as schedule.json.bak. A damaged file is set aside
/// (schedule.json.damaged-…) and the backup is used instead, so nothing is lost silently.
/// </summary>
public sealed class ScheduleStore
{
    public const int Version = 2;
    private const string UntilResumed = "resume";
    private static readonly string[] DayKeys = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]; // by DayOfWeek

    public string Path { get; }
    public string BackupPath => Path + ".bak";

    public ScheduleStore(string directory) => Path = System.IO.Path.Combine(directory, "schedule.json");

    /// <summary>The stored schedule (empty, switched on, when there is none) and what went wrong reading it, if anything.</summary>
    public (WeekSchedule Schedule, string? Warning) Load()
    {
        if (!File.Exists(Path))
        {
            return (WeekSchedule.Empty, null);
        }
        var (schedule, problem) = Read(Path);
        if (schedule is not null)
        {
            return (schedule, problem is null ? null : $"Schedule: {problem}");
        }
        var damaged = $"{Path}.damaged-{DateTime.Now:yyyyMMdd-HHmmss}";
        TryCopy(Path, damaged);
        var (backup, backupProblem) = File.Exists(BackupPath) ? Read(BackupPath) : (null, null);
        if (backup is not null)
        {
            return (backup, $"Schedule: {problem}; using the previous version (the damaged file is kept as {damaged}){Also(backupProblem)}");
        }
        return (WeekSchedule.Empty, $"Schedule: {problem}; no usable backup, starting empty (the damaged file is kept as {damaged})");
    }

    public void Save(WeekSchedule schedule)
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(Path)!);
        var temp = Path + ".tmp";
        File.WriteAllBytes(temp, Encoding.UTF8.GetBytes(ToJson(schedule)));
        if (File.Exists(Path))
        {
            File.Replace(temp, Path, BackupPath); // atomic, and the old version becomes the backup
        }
        else
        {
            File.Move(temp, Path);
        }
    }

    public static string ToJson(WeekSchedule schedule)
    {
        var days = new JsonObject();
        foreach (var day in WeekSchedule.Week)
        {
            var blocks = new JsonArray();
            foreach (var b in schedule[day])
            {
                blocks.Add(new JsonObject
                {
                    ["id"] = b.Id.ToString(),
                    ["name"] = b.Name,
                    ["start"] = DayTime.Format(b.Start),
                    ["end"] = DayTime.Format(b.End),
                    ["mood"] = b.Mood,
                    ["plans"] = b.Plans,
                    ["action"] = b.Action,
                    ["reminder"] = b.Reminder,
                    ["enabled"] = b.Enabled,
                });
            }
            days[DayKeys[(int)day]] = blocks;
        }
        var paused = schedule.PausedUntil is not { } until ? null
            : until == WeekSchedule.UntilResumed ? UntilResumed : until.ToString("O", System.Globalization.CultureInfo.InvariantCulture);
        var root = new JsonObject { ["version"] = Version, ["live"] = schedule.Live, ["paused_until"] = paused, ["days"] = days };
        // Readable by hand: times keep their '+' (the default encoder writes \u002B).
        return root.ToJsonString(new JsonSerializerOptions
        {
            WriteIndented = true, Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        });
    }

    /// <summary>
    /// Parses a schedule. Unreadable JSON gives (null, why). Blocks that are invalid are left out and reported; unknown
    /// fields are ignored.
    /// </summary>
    public static (WeekSchedule? Schedule, string? Problem) FromJson(string json)
    {
        JsonObject root;
        try
        {
            root = JsonNode.Parse(json) as JsonObject ?? throw new JsonException("not a JSON object");
        }
        catch (JsonException e)
        {
            return (null, $"unreadable ({e.Message})");
        }
        var schedule = WeekSchedule.Empty.WithLive(Bool(root["live"]) ?? false).WithPause(Pause(Text(root["paused_until"])));
        var dropped = 0;
        var days = root["days"] as JsonObject;
        foreach (var day in WeekSchedule.Week)
        {
            if (days?[DayKeys[(int)day]] is not JsonArray list)
            {
                continue;
            }
            var blocks = new List<ScheduleBlock>();
            foreach (var node in list)
            {
                if (ParseBlock(node as JsonObject) is { } block && WeekSchedule.Problem(block) is null && blocks.All(b => b.Id != block.Id))
                {
                    blocks.Add(block);
                }
                else
                {
                    dropped++;
                }
            }
            schedule = schedule.WithDay(day, blocks);
        }
        return (schedule, dropped > 0 ? $"{dropped} invalid block(s) left out" : null);
    }

    private static ScheduleBlock? ParseBlock(JsonObject? o)
    {
        if (o is null || !Guid.TryParse(Text(o["id"]), out var id) ||
            DayTime.Parse(Text(o["start"])) is not { } start || DayTime.Parse(Text(o["end"])) is not { } end)
        {
            return null;
        }
        return new ScheduleBlock
        {
            Id = id,
            Name = Text(o["name"]) ?? "",
            Start = start,
            End = end,
            Mood = NullIfEmpty(Text(o["mood"])),
            Plans = NullIfEmpty(Text(o["plans"])),
            Action = NullIfEmpty(Text(o["action"])),
            Reminder = Bool(o["reminder"]) ?? false,
            Enabled = Bool(o["enabled"]) ?? true,
        };
    }

    private static DateTimeOffset? Pause(string? text) =>
        text == UntilResumed ? WeekSchedule.UntilResumed
        : DateTimeOffset.TryParse(text, System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.RoundtripKind, out var t) ? t
        : null;

    private static (WeekSchedule?, string?) Read(string path)
    {
        try
        {
            return FromJson(File.ReadAllText(path, Encoding.UTF8));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return (null, $"cannot be read ({e.Message})");
        }
    }

    private static string? Text(JsonNode? n) => n is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;
    private static bool? Bool(JsonNode? n) => n is JsonValue v && v.TryGetValue<bool>(out var b) ? b : null;
    private static string? NullIfEmpty(string? s) => string.IsNullOrWhiteSpace(s) ? null : s;
    private static string Also(string? problem) => problem is null ? "" : $"; in it: {problem}";

    private static void TryCopy(string from, string to)
    {
        try
        {
            File.Copy(from, to, overwrite: true);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }
}
