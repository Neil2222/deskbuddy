using System.Globalization;
using System.Text.Json.Nodes;
using DeskBuddy.Companion.Protocol;

namespace DeskBuddy.Companion.History;

/// <summary>A <c>log.since</c> to send: the events of <see cref="Boot"/> after <see cref="Since"/>.</summary>
public sealed record FetchRequest(long Session, string Boot, uint Since)
{
    public string Command => $"log.since boot={Boot} seq={Since}";
}

/// <summary>
/// The Companion's memory of one source (board or simulator): the canonical device event log, its checkpoints and
/// the Companion's own journal, and the rules that keep them complete and free of duplicates (docs/schedule.md §4b).
/// Single-threaded: <see cref="HistoryRecorder"/> runs it on its worker.
///
/// - Events are stored by (dev, boot, seq), in whatever order they arrive; a second copy (live and replayed, or two
///   replays) is not stored again.
/// - The checkpoint of a boot is the highest seq such that every seq up to it is stored or recorded as lost. It never
///   passes a seq that is neither; it is written (atomically) only after the log lines it covers are on disk.
/// - A hole (a seq above checkpoint + 1) asks for <c>log.since</c>; the reply's <c>lost=</c> is written as a gap only
///   when it accounts exactly for what is missing; anything it does not explain stays open and is asked again.
/// - A new boot closes the previous ones: "seen live" when the restart arrived on a live connection with no hole
///   detected (never a proof that its last event was received), otherwise "events after seq N may have been missed".
/// - A restart during the handshake (evt boot before the hello reply was handled) is kept in order with the events:
///   events before an evt boot naming the hello's boot belong to an earlier, unknown boot and are not stored; a later
///   evt boot switches to the new boot and asks it for everything.
/// Nothing is ever invented to fill a gap.
/// </summary>
public sealed class HistoryLedger
{
    public const int MaxFetchAttempts = 3;
    public static readonly TimeSpan RetryDelay = TimeSpan.FromSeconds(5);

    private readonly JsonlFile _log;
    private readonly JsonlFile _journal;
    private readonly CheckpointFile _checkpoints;
    private readonly string _source;
    private readonly string _installation;
    private readonly TimeZoneInfo _zone;

    private readonly List<LogEntry> _entries = [];
    private readonly List<object> _journalEntries = [];
    private readonly Dictionary<(string Dev, string Boot, uint Seq), EventEntry> _events = [];
    private readonly Dictionary<(string Dev, string Boot), BootState> _boots = [];
    private readonly Dictionary<string, List<string>> _bootOrder = []; // per device, in the order first seen
    private readonly List<JsonObject> _logOut = [];
    private readonly List<JsonObject> _journalOut = [];
    private bool _checkpointsDirty;
    private bool _changed = true;

    private long _session;
    private (string Dev, string Boot)? _current;
    private readonly List<(Frame Frame, DateTimeOffset At)> _pending = [];
    private bool _fetchWanted;
    private FetchRequest? _inFlight;
    private readonly HashSet<uint> _replayed = [];
    private int _attempts;
    private DateTimeOffset _nextFetch = DateTimeOffset.MinValue;

    public HistoryLedger(string directory, HistorySource source, string installation, TimeZoneInfo? zone = null)
    {
        var sim = source == HistorySource.Simulator ? ".sim" : "";
        _log = new JsonlFile(Path.Combine(directory, $"device-events{sim}.jsonl"));
        _journal = new JsonlFile(Path.Combine(directory, $"companion-events{sim}.jsonl"));
        _checkpoints = new CheckpointFile(Path.Combine(directory, $"device-checkpoints{sim}.json"));
        _source = source == HistorySource.Simulator ? "simulator" : "board";
        _installation = installation;
        _zone = zone ?? TimeZoneInfo.Local;
    }

    public string LogPath => _log.Path;
    public string JournalPath => _journal.Path;
    public string CheckpointPath => _checkpoints.Path;
    public HistorySnapshot Snapshot { get; private set; } = HistorySnapshot.Empty;
    /// <summary>The highest iid in this source's journal (0: none), so new requests can continue above it.</summary>
    public uint MaxRequestIid { get; private set; }
    public event Action<string>? Log;

    /// <summary>Reads the logs. Throws IOException / UnauthorizedAccessException when they cannot be read at all.</summary>
    public void Load()
    {
        var lines = _log.Read(out var skipped);
        foreach (var o in lines)
        {
            if (HistoryCodec.LogFromJson(o) is { } e)
            {
                Apply(e);
            }
            else
            {
                skipped++;
            }
        }
        if (skipped > 0)
        {
            Log?.Invoke($"History: {skipped} unreadable line(s) in {Path.GetFileName(_log.Path)} skipped (a write cut off by a crash?)");
        }
        foreach (var o in _journal.Read(out _))
        {
            if (HistoryCodec.CompanionFromJson(o) is { } j)
            {
                _journalEntries.Add(j);
                if (j is RequestEntry r && r.Iid > MaxRequestIid && r.Iid < 0x80000000)
                {
                    MaxRequestIid = r.Iid;
                }
            }
        }
        var stored = _checkpoints.Read(out var problem);
        if (problem is not null)
        {
            Log?.Invoke($"History: {problem}");
        }
        foreach (var ((dev, boot), seq) in stored)
        {
            var derived = _boots.TryGetValue((dev, boot), out var b) ? b.Checkpoint : 0;
            if (seq > derived)
            {
                Log?.Invoke($"History: checkpoint {dev}/{boot} was {seq} but the log only accounts for {derived}; using {derived} (the missing events are asked again)");
            }
        }
        _checkpointsDirty = stored.Count != _boots.Count || _boots.Any(b => !stored.TryGetValue(b.Key, out var s) || s != b.Value.Checkpoint);
        Snapshot = HistoryDeriver.Derive(_entries, _journalEntries);
        _changed = false;
    }

    /// <summary>The checkpoint of a boot: every seq up to it is stored or recorded as lost.</summary>
    public uint Checkpoint(string dev, string boot) => _boots.TryGetValue((dev, boot), out var b) ? b.Checkpoint : 0;

    public IReadOnlyList<LogEntry> Entries => _entries;

    /// <summary>Connected (hello) to a device that keeps recorded events (cap log).</summary>
    public void Connected(string dev, string boot, string? firmware, DateTimeOffset at)
    {
        _session++;
        _inFlight = null;
        _replayed.Clear();
        _attempts = 0;
        _nextFetch = DateTimeOffset.MinValue;
        BootSeen(dev, boot, firmware, at, restartSeenLive: false);
        _current = (dev, boot);
        _fetchWanted = true; // whatever happened while we were away
        var pending = _pending.ToList();
        _pending.Clear();
        // The hello reply came from `boot`. An evt boot for it among the frames that arrived first means the device
        // restarted into it during the handshake: whatever came before belongs to an earlier boot we cannot name.
        var start = pending.FindLastIndex(p => p.Frame.Name == "boot" && p.Frame.Arg("boot") == boot) + 1;
        if (pending.Take(start).Count(p => p.Frame.Name != "boot") is var dropped and > 0)
        {
            Log?.Invoke($"History: {dropped} event(s) from before a restart during the handshake are not stored (their boot is unknown)");
        }
        foreach (var (frame, when) in pending.Skip(start))
        {
            Received(frame, when); // an evt boot for a newer boot switches to it here
        }
    }

    /// <summary>The connection is gone: nothing is "seen live" any more until the next hello.</summary>
    public void Disconnected()
    {
        _session++;
        _current = null;
        _pending.Clear();
        _inFlight = null;
        _fetchWanted = false;
        _replayed.Clear();
        foreach (var b in _boots.Values)
        {
            b.Drained = false;
        }
    }

    /// <summary>An event frame from the device (live or replayed), in the order received.</summary>
    public void Received(Frame frame, DateTimeOffset at)
    {
        if (frame.Name == "boot")
        {
            OnBootEvent(frame, at);
            return;
        }
        if (frame.Name is not ("act" or "did") || !uint.TryParse(frame.Arg("seq"), out var seq) || seq == 0)
        {
            return; // not a recorded event (or firmware without cap log: nothing to key it on)
        }
        if (_current is not { } cur)
        {
            _pending.Add((frame, at)); // before the hello reply was handled: the boot follows
            return;
        }
        if (HistoryCodec.Payload(frame.Line) is not { } payload)
        {
            return;
        }
        var replay = frame.Arg("replay") == "1";
        if (replay && _inFlight is not null)
        {
            _replayed.Add(seq);
        }
        var time = new DeviceTime(
            long.TryParse(frame.Arg("t"), NumberStyles.None, CultureInfo.InvariantCulture, out var t) ? DateTimeOffset.FromUnixTimeMilliseconds(t) : null,
            long.TryParse(frame.Arg("up"), NumberStyles.None, CultureInfo.InvariantCulture, out var up) ? up : null);
        var entry = new EventEntry(cur.Dev, cur.Boot, seq, frame.Name, payload, time, at);
        if (_events.TryGetValue((cur.Dev, cur.Boot, seq), out var existing))
        {
            if (existing.Payload != payload)
            {
                Log?.Invoke($"History: {cur.Dev}/{cur.Boot} seq {seq} arrived again with different content (kept the first): {payload}");
            }
            return;
        }
        Append(entry);
        var state = State(cur.Dev, cur.Boot);
        if (state.Ahead.Count > 0)
        {
            state.Drained = false;
            if (!replay)
            {
                _fetchWanted = true; // a hole: a line was lost on the way
            }
        }
    }

    /// <summary>The reply to <paramref name="request"/> (sent after all the events it replayed).</summary>
    public void ReplayAnswered(FetchRequest request, Frame reply, DateTimeOffset at)
    {
        if (request.Session != _session || !ReferenceEquals(request, _inFlight))
        {
            return; // an earlier connection's
        }
        _inFlight = null;
        var replayed = _replayed.ToList();
        _replayed.Clear();
        if (!reply.IsOk)
        {
            RetryLater(at, $"log.since refused ({reply.Name})");
            return;
        }
        if (_current is not { } cur || reply.Arg("boot") is not { } boot || boot != cur.Boot ||
            !uint.TryParse(reply.Arg("last"), out var last) || !uint.TryParse(reply.Arg("lost"), out var lost))
        {
            // It restarted meanwhile (its evt boot is handled before the next request), or a reply we do not understand.
            RetryLater(at, $"log.since answered for boot {reply.Arg("boot") ?? "?"} instead of {request.Boot}");
            return;
        }
        var since = request.Boot == boot ? request.Since : 0;
        var state = State(cur.Dev, cur.Boot);
        var first = replayed.Count > 0 ? replayed.Min() : last + 1;
        if (lost > 0)
        {
            // The device lost the oldest ones: seqs since+1 .. first-1 (it sends what it still has in order). Only when
            // that range has exactly the reported size is it written as lost; otherwise ask again.
            var span = first > since + 1 ? first - since - 1 : 0;
            if (span != lost)
            {
                RetryLater(at, $"log.since reported {lost} lost but {span} are missing before the first replayed event");
                return;
            }
            WriteGaps(cur.Dev, cur.Boot, since + 1, first - 1, at);
        }
        if (state.Checkpoint < last)
        {
            RetryLater(at, $"events {state.Checkpoint + 1}..{last} of {cur.Dev}/{cur.Boot} still missing after a replay (lost on the line?)");
            return;
        }
        _attempts = 0;
        state.Drained = state.Ahead.Count == 0;
        _fetchWanted = state.Ahead.Count > 0;
    }

    /// <summary>The request got no reply (timeout, err busy, the link failed).</summary>
    public void ReplayFailed(FetchRequest request, string reason, DateTimeOffset at)
    {
        if (request.Session != _session || !ReferenceEquals(request, _inFlight))
        {
            return;
        }
        _inFlight = null;
        _replayed.Clear();
        RetryLater(at, $"log.since failed ({reason})");
    }

    /// <summary>The <c>log.since</c> to send now, if one is needed and none is under way.</summary>
    public FetchRequest? TakeFetch(DateTimeOffset now)
    {
        if (_current is not { } cur || _inFlight is not null || !_fetchWanted || now < _nextFetch)
        {
            return null;
        }
        _fetchWanted = false;
        _replayed.Clear();
        _inFlight = new FetchRequest(_session, cur.Boot, State(cur.Dev, cur.Boot).Checkpoint);
        return _inFlight;
    }

    /// <summary>The Companion is about to send <c>act name=<paramref name="action"/> iid=<paramref name="iid"/></c>.</summary>
    public void Requested(uint iid, string action, string origin, DateTimeOffset at)
    {
        if (_current is { } cur)
        {
            var r = new RequestEntry(Guid.CreateVersion7(at).ToString(), cur.Dev, cur.Boot, iid, action, origin, at);
            _journalEntries.Add(r);
            _journalOut.Add(HistoryCodec.ToJson(r, _source, _installation, _zone));
            _changed = true;
        }
    }

    public void RequestAnswered(uint iid, string result, DateTimeOffset at)
    {
        if (_current is { } cur)
        {
            var r = new RequestResultEntry(cur.Dev, cur.Boot, iid, result, at);
            _journalEntries.Add(r);
            _journalOut.Add(HistoryCodec.ToJson(r, _source, _installation, _zone));
            _changed = true;
        }
    }

    /// <summary>
    /// Makes everything received so far durable: the log lines and journal lines (flushed to disk), then the
    /// checkpoints. Returns true when the history changed. On an IO error nothing is lost from memory and the lines
    /// are written on the next call; the checkpoints wait for them.
    /// </summary>
    public bool Commit()
    {
        if (_logOut.Count > 0)
        {
            _log.Append(_logOut);
            _logOut.Clear();
        }
        if (_journalOut.Count > 0)
        {
            _journal.Append(_journalOut);
            _journalOut.Clear();
        }
        if (_checkpointsDirty)
        {
            _checkpoints.Write(_boots.Select(b => KeyValuePair.Create(b.Key, b.Value.Checkpoint)));
            _checkpointsDirty = false;
        }
        if (!_changed)
        {
            return false;
        }
        Snapshot = HistoryDeriver.Derive(_entries, _journalEntries);
        _changed = false;
        return true;
    }

    private void OnBootEvent(Frame frame, DateTimeOffset at)
    {
        if (frame.Arg("dev") is not { } dev || frame.Arg("boot") is not { } boot)
        {
            return; // firmware without cap log
        }
        if (_current is not { } cur)
        {
            _pending.Add((frame, at)); // during the handshake: sorted out when the hello reply is handled
            return;
        }
        BootSeen(dev, boot, frame.Arg("fw"), at, restartSeenLive: cur.Dev == dev);
        if (cur.Boot != boot)
        {
            _current = (dev, boot);
            _attempts = 0;
            _fetchWanted = true;
        }
    }

    private void BootSeen(string dev, string boot, string? firmware, DateTimeOffset at, bool restartSeenLive)
    {
        if (!_bootOrder.TryGetValue(dev, out var boots) || !boots.Contains(boot))
        {
            Append(new BootEntry(dev, boot, firmware, at));
        }
        foreach (var other in _bootOrder[dev].Where(b => b != boot).ToList())
        {
            var state = State(dev, other);
            if (state.Closed)
            {
                continue;
            }
            var seenLive = restartSeenLive && state.Drained && state.Ahead.Count == 0;
            Append(new BootEndEntry(dev, other, state.Checkpoint, seenLive, at));
            Log?.Invoke(seenLive
                ? $"History: {dev} restarted (boot {other} ended after seq {state.Checkpoint}, seen live; a last event lost on the way could not be detected)"
                : $"History: {dev} restarted (boot {other} ended); events after seq {state.Checkpoint} of that boot may have been missed, how many is unknown");
        }
    }

    private void WriteGaps(string dev, string boot, uint from, uint to, DateTimeOffset at)
    {
        var state = State(dev, boot);
        uint? run = null;
        for (var s = from; s <= to + 1 && s != 0; s++)
        {
            var missing = s <= to && s > state.Checkpoint && !state.Ahead.Contains(s);
            if (missing && run is null)
            {
                run = s;
            }
            else if (!missing && run is { } start)
            {
                Append(new GapEntry(dev, boot, start, s - 1, at));
                Log?.Invoke($"History: {dev}/{boot} events {start}..{s - 1} were lost on the device (its buffer overflowed); recorded as a gap");
                run = null;
            }
        }
    }

    private void RetryLater(DateTimeOffset at, string why)
    {
        if (++_attempts >= MaxFetchAttempts)
        {
            Log?.Invoke($"History: {why}; left open until the next connection or event");
            _fetchWanted = false;
            _attempts = 0;
            return;
        }
        Log?.Invoke($"History: {why}; asking again");
        _fetchWanted = true;
        _nextFetch = at + RetryDelay;
    }

    private void Append(LogEntry entry)
    {
        if (Apply(entry))
        {
            _logOut.Add(HistoryCodec.ToJson(entry, _source, _installation, _zone));
            _checkpointsDirty = true;
            _changed = true;
        }
    }

    // Adds an entry to the in-memory state. False when it is already there (the same key).
    private bool Apply(LogEntry entry)
    {
        switch (entry)
        {
        case EventEntry e:
            if (!_events.TryAdd((e.Dev, e.Boot, e.Seq), e))
            {
                return false;
            }
            State(e.Dev, e.Boot).Add(e.Seq, e.Seq);
            break;
        case BootEntry b:
            if (_bootOrder.TryGetValue(b.Dev, out var list) && list.Contains(b.Boot))
            {
                return false;
            }
            State(b.Dev, b.Boot);
            break;
        case GapEntry g:
            State(g.Dev, g.Boot).Add(g.From, g.To);
            break;
        case BootEndEntry end:
            var state = State(end.Dev, end.Boot);
            if (state.Closed)
            {
                return false;
            }
            state.Closed = true;
            break;
        }
        _entries.Add(entry);
        return true;
    }

    private BootState State(string dev, string boot)
    {
        if (!_boots.TryGetValue((dev, boot), out var state))
        {
            state = new BootState();
            _boots[(dev, boot)] = state;
            if (!_bootOrder.TryGetValue(dev, out var list))
            {
                _bootOrder[dev] = list = [];
            }
            list.Add(boot);
        }
        return state;
    }

    private sealed class BootState
    {
        public uint Checkpoint;
        public readonly HashSet<uint> Ahead = []; // stored or lost, above checkpoint + 1
        public bool Closed;
        public bool Drained; // this connection has every event of it up to now

        public void Add(uint from, uint to)
        {
            for (var s = from; s <= to && s != 0; s++)
            {
                if (s > Checkpoint)
                {
                    Ahead.Add(s);
                }
            }
            while (Ahead.Remove(Checkpoint + 1))
            {
                Checkpoint++;
            }
        }
    }
}
