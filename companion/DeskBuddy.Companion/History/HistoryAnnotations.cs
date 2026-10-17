using System.Globalization;
using System.Text.Json.Nodes;

namespace DeskBuddy.Companion.History;

/// <summary>
/// One line of history-annotations.jsonl: a user's note on the history that changes what the History view shows, never
/// what was recorded. <see cref="Action"/> "hide" hides <see cref="Item"/> (an interaction, activity or occurrence id) or
/// a period (<see cref="FromUtc"/> up to <see cref="ToUtc"/>); "unhide" withdraws the hide <see cref="Retracts"/>.
/// </summary>
public sealed record Annotation(
    string Id, string Action, string? Item, DateTimeOffset? FromUtc, DateTimeOffset? ToUtc, string? Retracts,
    string Reason, string? Note, DateTimeOffset AtUtc)
{
    public bool IsPeriod => FromUtc is not null && ToUtc is not null;
}

/// <summary>
/// The hides in force: every "hide" not withdrawn by an "unhide". Folded from the annotations in file order, so the
/// file is the whole truth and any hide can be undone at any time.
/// </summary>
public sealed class HistoryAnnotations
{
    public static readonly HistoryAnnotations None = new([]);

    private readonly HashSet<string> _items;

    private HistoryAnnotations(IReadOnlyList<Annotation> hides)
    {
        Hides = hides;
        _items = hides.Where(h => h.Item is not null).Select(h => h.Item!).ToHashSet(StringComparer.Ordinal);
        Periods = hides.Where(h => h.IsPeriod).ToList();
    }

    /// <summary>The hides in force, oldest first.</summary>
    public IReadOnlyList<Annotation> Hides { get; }

    public IReadOnlyList<Annotation> Periods { get; }

    public bool HidesItem(string id) => _items.Contains(id);

    /// <summary>The period hide that covers <paramref name="atUtc"/>, if any (start included, end excluded).</summary>
    public Annotation? PeriodCovering(DateTimeOffset atUtc) => Periods.FirstOrDefault(p => p.FromUtc <= atUtc && atUtc < p.ToUtc);

    public static HistoryAnnotations Fold(IEnumerable<Annotation> annotations)
    {
        var hides = new List<Annotation>();
        foreach (var a in annotations)
        {
            if (a.Action == "hide")
            {
                hides.Add(a);
            }
            else if (a.Action == "unhide" && a.Retracts is { } id)
            {
                hides.RemoveAll(h => h.Id == id);
            }
        }
        return new HistoryAnnotations(hides);
    }
}

/// <summary>
/// The annotations file of one source: history-annotations.jsonl (the simulator's: history-annotations.sim.jsonl), next
/// to the device event log, append-only and flushed like it. The canonical logs are never touched.
/// </summary>
public sealed class AnnotationStore
{
    private readonly JsonlFile _file;
    private readonly string _source;
    private readonly string _installation;
    private readonly List<Annotation> _all = [];

    public AnnotationStore(string directory, HistorySource source, string installation)
    {
        var sim = source == HistorySource.Simulator ? ".sim" : "";
        _file = new JsonlFile(System.IO.Path.Combine(directory, $"history-annotations{sim}.jsonl"));
        _source = source == HistorySource.Simulator ? "simulator" : "board";
        _installation = installation;
    }

    public string Path => _file.Path;

    public HistoryAnnotations Current { get; private set; } = HistoryAnnotations.None;

    /// <summary>Every annotation in the file, oldest first (hides, and the unhides that withdrew some).</summary>
    public IReadOnlyList<Annotation> All => _all;

    public void Load()
    {
        _all.Clear();
        foreach (var o in _file.Read(out _))
        {
            if (FromJson(o) is { } a)
            {
                _all.Add(a);
            }
        }
        Current = HistoryAnnotations.Fold(_all);
    }

    public Annotation HideItem(string id, string reason, string? note, DateTimeOffset at) =>
        Append(new Annotation(Guid.CreateVersion7(at).ToString(), "hide", id, null, null, null, reason, note, at));

    public Annotation HidePeriod(DateTimeOffset fromUtc, DateTimeOffset toUtc, string reason, string? note, DateTimeOffset at)
    {
        if (toUtc <= fromUtc)
        {
            throw new ArgumentException("the period must end after it starts");
        }
        return Append(new Annotation(Guid.CreateVersion7(at).ToString(), "hide", null, fromUtc.ToUniversalTime(), toUtc.ToUniversalTime(),
                                     null, reason, note, at));
    }

    /// <summary>Withdraws a hide (by its annotation id). Nothing happens to the data it hid: it shows again.</summary>
    public Annotation Unhide(string hideId, DateTimeOffset at) =>
        Append(new Annotation(Guid.CreateVersion7(at).ToString(), "unhide", null, null, null, hideId, "undo", null, at));

    private Annotation Append(Annotation a)
    {
        _file.Append([ToJson(a)]);
        _all.Add(a);
        Current = HistoryAnnotations.Fold(_all);
        return a;
    }

    private JsonObject ToJson(Annotation a)
    {
        var o = new JsonObject { ["v"] = HistoryCodec.Version, ["kind"] = "annotation", ["id"] = a.Id, ["action"] = a.Action };
        if (a.Item is { } item)
        {
            o["item"] = item;
        }
        if (a.IsPeriod)
        {
            o["from_utc"] = HistoryCodec.Iso(a.FromUtc!.Value);
            o["to_utc"] = HistoryCodec.Iso(a.ToUtc!.Value);
        }
        if (a.Retracts is { } r)
        {
            o["retracts"] = r;
        }
        o["reason"] = a.Reason;
        o["note"] = a.Note;
        o["src"] = _source;
        o["at_utc"] = HistoryCodec.Iso(a.AtUtc.ToUniversalTime());
        o["inst"] = _installation;
        return o;
    }

    private static Annotation? FromJson(JsonObject o)
    {
        string? S(string k) => o[k] is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;
        DateTimeOffset? T(string k) => S(k) is { } s && DateTimeOffset.TryParse(s, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var t)
            ? t.ToUniversalTime() : null;
        if (S("kind") != "annotation" || S("id") is not { } id || S("action") is not ("hide" or "unhide") || T("at_utc") is not { } at)
        {
            return null;
        }
        var a = new Annotation(id, S("action")!, S("item"), T("from_utc"), T("to_utc"), S("retracts"), S("reason") ?? "", S("note"), at);
        var valid = a.Action == "unhide" ? a.Retracts is not null : a.Item is not null ^ (a.IsPeriod && a.FromUtc < a.ToUtc);
        return valid ? a : null;
    }
}
