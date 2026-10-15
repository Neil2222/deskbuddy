using System.Globalization;
using System.Text.Json.Nodes;

namespace DeskBuddy.Companion.History;

/// <summary>Where the history comes from. Each has its own files, so simulator runs never mix with the board's.</summary>
public enum HistorySource { Board, Simulator }

/// <summary>
/// When something happened on the device, as the device knew it: UTC once the Companion had set its clock
/// (<c>t=</c>), otherwise only the time since that boot started (<c>up=</c>). An uptime is never turned into a date.
/// </summary>
public readonly record struct DeviceTime(DateTimeOffset? Utc, long? UptimeMs)
{
    public bool Known => Utc is not null;
}

/// <summary>One line of the canonical device event log (device-events.jsonl).</summary>
public abstract record LogEntry(string Dev, string Boot, DateTimeOffset ReceivedUtc);

/// <summary>
/// A recorded device event (<c>evt act</c>, <c>evt did</c>), keyed by (dev, boot, seq). <see cref="Payload"/> is the
/// frame as the device sent it, without its framing, checksum and <c>replay=1</c>, so a live and a replayed copy are
/// the same.
/// </summary>
public sealed record EventEntry(string Dev, string Boot, uint Seq, string Name, string Payload, DeviceTime Time,
                                DateTimeOffset ReceivedUtc) : LogEntry(Dev, Boot, ReceivedUtc)
{
    public string? Arg(string key) => HistoryCodec.PayloadArg(Payload, key);
}

/// <summary>A boot of a device, the first time it was seen (hello or evt boot).</summary>
public sealed record BootEntry(string Dev, string Boot, string? Firmware, DateTimeOffset ReceivedUtc)
    : LogEntry(Dev, Boot, ReceivedUtc);

/// <summary>
/// Events the device no longer had when asked (its buffer overflowed): seqs <see cref="From"/>..<see cref="To"/>,
/// their number known, their content not.
/// </summary>
public sealed record GapEntry(string Dev, string Boot, uint From, uint To, DateTimeOffset ReceivedUtc)
    : LogEntry(Dev, Boot, ReceivedUtc)
{
    public uint Count => To - From + 1;
}

/// <summary>
/// A boot is over (a newer one was seen). Never a claim that every event of it was received: the device cannot say
/// which seq it ended at, so a final event lost on the way stays undetectable.
/// <see cref="SeenLive"/>: the restart was seen on a live connection with no hole detected up to <see cref="After"/>.
/// Otherwise it restarted while the Companion was away: events after <see cref="After"/> may have been missed, how many
/// is unknown.
/// </summary>
public sealed record BootEndEntry(string Dev, string Boot, uint After, bool SeenLive, DateTimeOffset ReceivedUtc)
    : LogEntry(Dev, Boot, ReceivedUtc);

/// <summary>An activity the Companion asked the device to start (companion-events.jsonl), written before sending.</summary>
public sealed record RequestEntry(string Id, string Dev, string Boot, uint Iid, string Action, string Origin,
                                  DateTimeOffset AtUtc);

/// <summary>How the device answered a request: ok, dup, busy, unsupported, err:&lt;code&gt;, no_reply, lost.</summary>
public sealed record RequestResultEntry(string Dev, string Boot, uint Iid, string Result, DateTimeOffset AtUtc);

/// <summary>JSON lines of the history files. Unknown fields are ignored; a line that does not parse is skipped.</summary>
public static class HistoryCodec
{
    public const int Version = 1;

    public static JsonObject ToJson(LogEntry e, string source, string installation, TimeZoneInfo zone)
    {
        var o = new JsonObject { ["v"] = Version };
        switch (e)
        {
        case EventEntry ev:
            o["kind"] = "event";
            Key(o, e);
            o["seq"] = ev.Seq;
            o["name"] = ev.Name;
            o["payload"] = ev.Payload;
            o["time_known"] = ev.Time.Known;
            o["t_utc"] = ev.Time.Utc is { } t ? Iso(t) : null;
            o["up_ms"] = ev.Time.UptimeMs;
            break;
        case BootEntry b:
            o["kind"] = "boot";
            Key(o, e);
            o["fw"] = b.Firmware;
            break;
        case GapEntry g:
            o["kind"] = "gap";
            Key(o, e);
            o["from"] = g.From;
            o["to"] = g.To;
            o["count"] = g.Count;
            o["reason"] = "overflow";
            break;
        case BootEndEntry end:
            o["kind"] = "boot_end";
            Key(o, e);
            o["after"] = end.After;
            o["seen_live"] = end.SeenLive;
            break;
        }
        o["src"] = source;
        Received(o, e.ReceivedUtc, zone);
        o["inst"] = installation;
        return o;
    }

    public static LogEntry? LogFromJson(JsonObject o)
    {
        var dev = Str(o["dev"]);
        var boot = Str(o["boot"]);
        var recv = Time(o["recv_utc"]);
        if (dev is null || boot is null || recv is null)
        {
            return null;
        }
        switch (Str(o["kind"]))
        {
        case "event":
            var seq = UInt(o["seq"]);
            var name = Str(o["name"]);
            var payload = Str(o["payload"]);
            if (seq is null or 0 || name is null || payload is null)
            {
                return null;
            }
            return new EventEntry(dev, boot, seq.Value, name, payload, new DeviceTime(Time(o["t_utc"]), Long(o["up_ms"])), recv.Value);
        case "boot":
            return new BootEntry(dev, boot, Str(o["fw"]), recv.Value);
        case "gap":
            var from = UInt(o["from"]);
            var to = UInt(o["to"]);
            return from is null or 0 || to is null || to < from ? null : new GapEntry(dev, boot, from.Value, to.Value, recv.Value);
        case "boot_end":
            var after = UInt(o["after"]);
            var seenLive = o["seen_live"] is JsonValue c && c.TryGetValue<bool>(out var cv) ? cv : (bool?)null;
            return after is null || seenLive is null ? null : new BootEndEntry(dev, boot, after.Value, seenLive.Value, recv.Value);
        default:
            return null;
        }
    }

    public static JsonObject ToJson(RequestEntry r, string source, string installation, TimeZoneInfo zone) => new()
    {
        ["v"] = Version, ["kind"] = "request", ["id"] = r.Id, ["dev"] = r.Dev, ["boot"] = r.Boot, ["iid"] = r.Iid,
        ["action"] = r.Action, ["origin"] = r.Origin, ["src"] = source, ["at_utc"] = Iso(r.AtUtc),
        ["at"] = Iso(TimeZoneInfo.ConvertTime(r.AtUtc, zone)), ["tz"] = zone.Id, ["inst"] = installation,
    };

    public static JsonObject ToJson(RequestResultEntry r, string source, string installation, TimeZoneInfo zone) => new()
    {
        ["v"] = Version, ["kind"] = "request_result", ["dev"] = r.Dev, ["boot"] = r.Boot, ["iid"] = r.Iid,
        ["result"] = r.Result, ["src"] = source, ["at_utc"] = Iso(r.AtUtc),
        ["at"] = Iso(TimeZoneInfo.ConvertTime(r.AtUtc, zone)), ["tz"] = zone.Id, ["inst"] = installation,
    };

    /// <summary>A companion journal line: a <see cref="RequestEntry"/>, a <see cref="RequestResultEntry"/> or null.</summary>
    public static object? CompanionFromJson(JsonObject o)
    {
        var dev = Str(o["dev"]);
        var boot = Str(o["boot"]);
        var iid = UInt(o["iid"]);
        var at = Time(o["at_utc"]);
        if (dev is null || boot is null || iid is null or 0 || at is null)
        {
            return null;
        }
        return Str(o["kind"]) switch
        {
            "request" when Str(o["id"]) is { } id && Str(o["action"]) is { } action && Str(o["origin"]) is { } origin =>
                new RequestEntry(id, dev, boot, iid.Value, action, origin, at.Value),
            "request_result" when Str(o["result"]) is { } result => new RequestResultEntry(dev, boot, iid.Value, result, at.Value),
            _ => null,
        };
    }

    /// <summary>
    /// The payload of an event frame: the frame content after "&lt;DB1 0 " without the checksum and without
    /// <c>replay=1</c>. Null when the line is not a frame.
    /// </summary>
    public static string? Payload(string line)
    {
        var hash = line.LastIndexOf(" #", StringComparison.Ordinal);
        var tokens = (hash > 0 ? line[..hash] : line).Split(' ');
        if (tokens.Length < 4 || !tokens[0].StartsWith("<DB", StringComparison.Ordinal))
        {
            return null;
        }
        return string.Join(' ', tokens.Skip(2).Where(t => t != "replay=1"));
    }

    public static string? PayloadArg(string payload, string key)
    {
        foreach (var t in payload.Split(' '))
        {
            if (t.Length > key.Length && t[key.Length] == '=' && t.StartsWith(key, StringComparison.Ordinal))
            {
                return t[(key.Length + 1)..];
            }
        }
        return null;
    }

    public static string Iso(DateTimeOffset t) => t.ToString("O", CultureInfo.InvariantCulture);

    private static void Key(JsonObject o, LogEntry e)
    {
        o["dev"] = e.Dev;
        o["boot"] = e.Boot;
    }

    private static void Received(JsonObject o, DateTimeOffset utc, TimeZoneInfo zone)
    {
        o["recv_utc"] = Iso(utc.ToUniversalTime());
        o["recv"] = Iso(TimeZoneInfo.ConvertTime(utc, zone));
        o["tz"] = zone.Id;
    }

    private static string? Str(JsonNode? n) => n is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;

    private static long? Long(JsonNode? n) => n is JsonValue v && v.TryGetValue<long>(out var l) ? l : null;

    private static uint? UInt(JsonNode? n) => Long(n) is { } l && l is >= 0 and <= uint.MaxValue ? (uint)l : null;

    private static DateTimeOffset? Time(JsonNode? n) =>
        Str(n) is { } s && DateTimeOffset.TryParse(s, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var t)
            ? t.ToUniversalTime() : null;
}
