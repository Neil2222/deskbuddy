using DeskBuddy.Companion.Device;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.Protocol;
using Microsoft.Win32;

namespace DeskBuddy.Companion;

public enum LogKind { Info, Warning, Sent, Received, Device }

/// <summary>
/// Keeps DeskBuddy connected and its clock right: finds the board, connects (and reconnects after unplugging),
/// checks it still answers, syncs the time on connect / restart / hourly / when Windows' time or zone changes, and
/// steps aside while an ESP-IDF tool (tools/idf.ps1 flash or monitor) needs the port.
/// All members are used on the UI thread; link callbacks are posted to it.
/// </summary>
public sealed class CompanionService : IDisposable
{
    private const string PortHoldMutex = @"Local\DeskBuddy.PortHold"; // held by tools/idf.ps1
    private static readonly TimeSpan ReplyTimeout = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan ReplayTimeout = TimeSpan.FromSeconds(8); // log.since: up to 64 events, two per frame
    private static readonly TimeSpan PingInterval = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan SyncInterval = TimeSpan.FromHours(1);
    private static readonly TimeSpan RetryDelay = TimeSpan.FromSeconds(5);

    private readonly SynchronizationContext _ui;
    private readonly System.Windows.Forms.Timer _tick = new() { Interval = 1000 };
    private DeviceLink? _link;
    private bool _connecting;
    private bool _paused; // an ESP-IDF tool holds the port
    private DateTime _nextAttempt = DateTime.MinValue;
    private DateTime _nextPing;
    private DateTime _nextSync;
    private int _missedPings;
    private readonly PdcLease _pdcLease = new();
    private readonly string? _pdcWarning;
    private readonly string _dataDirectory = PdcStore.DefaultDirectory;
    private HistoryRecorder? _boardHistory;
    private HistoryRecorder? _simulatorHistory;
    private HistoryRecorder? _history; // the current connection's (board or simulator)

    public bool AutoConnect { get; private set; } = true;
    /// <summary>Port chosen by the user (a COM port or <see cref="DeviceLink.SimulatorName"/>); null: the first DeskBuddy (CH343) port, else the simulator if it is running.</summary>
    public string? SelectedPort { get; set; }

    public bool IsConnected { get; private set; }
    public string ConnectionText { get; private set; } = "Starting";
    public string? Firmware { get; private set; }
    public string? Protocol { get; private set; }
    /// <summary>The port of the current connection; null while not connected.</summary>
    public string? PortName => IsConnected ? _link?.PortName : null;
    public string Mode { get; private set; } = "unknown";
    /// <summary>Buddy's mood (status / evt mood); independent from <see cref="Mode"/>.</summary>
    public string Mood { get; private set; } = DeviceMood.Idle;
    /// <summary>The moods the firmware supports (hello moods=), in display order; empty: none.</summary>
    public IReadOnlyList<string> Moods { get; private set; } = [];
    /// <summary>The activities the firmware can start with <c>act</c> (hello acts=), in display order; empty: none.</summary>
    public IReadOnlyList<string> Acts { get; private set; } = [];
    public string ClockText { get; private set; } = "not synced";
    public IReadOnlySet<string> Caps { get; private set; } = new HashSet<string>();
    /// <summary>The PDC Manager "Ze Doen Maar Wat" integration (off by default; see Pdc/).</summary>
    public PdcIntegration Pdc { get; }

    public event Action? Changed;
    public event Action<LogKind, string>? Log;

    public CompanionService()
    {
        _ui = SynchronizationContext.Current ?? throw new InvalidOperationException("create on the UI thread");
        _tick.Tick += (_, _) => Tick();
        Pdc = PdcIntegration.CreateDefault(out _pdcWarning);
        Pdc.Log += Write;
        Pdc.Changed += SendPdcLease;
        SystemEvents.TimeChanged += OnTimeChanged; // also raised when the time zone changes
        SystemEvents.PowerModeChanged += OnPowerModeChanged;
    }

    public void Start()
    {
        _tick.Start();
        if (_pdcWarning is not null)
        {
            Write(LogKind.Warning, _pdcWarning);
        }
        else if (!Pdc.IsDefaultServer)
        {
            Write(LogKind.Warning, $"PDC Manager: using the test server {Pdc.BaseUri} ({PdcEndpoint.EnvironmentVariable})");
        }
        _ = Pdc.StartAsync();
        Tick();
    }

    /// <summary>Connect now (to <see cref="SelectedPort"/> or the auto-detected board) and keep reconnecting.</summary>
    public void Connect()
    {
        AutoConnect = true;
        _nextAttempt = DateTime.MinValue;
        Tick();
    }

    /// <summary>
    /// Switches to <paramref name="port"/> (a COM port, <see cref="DeviceLink.SimulatorName"/>, or null for
    /// auto-detect): a connection elsewhere is closed first. Then connects and keeps reconnecting, as <see cref="Connect"/>.
    /// </summary>
    public void ConnectTo(string? port)
    {
        SelectedPort = port;
        if (_link is { } link && (port is null || link.PortName != port))
        {
            CloseLink(port is null ? "Switching to auto-detect" : $"Switching to {port}");
        }
        Connect();
    }

    /// <summary>Disconnect and stay disconnected until <see cref="Connect"/>, e.g. to use the port elsewhere.</summary>
    public void Disconnect()
    {
        AutoConnect = false;
        CloseLink("Disconnected (auto-connect off)");
    }

    /// <summary>Sends an action command. True when DeskBuddy did it.</summary>
    public async Task<bool> RunAsync(string label, string body) => (await SendActionAsync(label, body)).Done;

    /// <summary>
    /// Starts one of Buddy's activities (<c>act</c>). With firmware that records events (caps track and log) the
    /// request gets an iid and goes into the history journal first, so what the device reports is linked to it.
    /// </summary>
    public async Task<bool> RunActAsync(string label, string act)
    {
        if (_history is not { } history || !Caps.Contains("track") || !Caps.Contains("log"))
        {
            return await RunAsync(label, $"act name={act}");
        }
        var iid = history.NextIid();
        history.Requested(iid, act, "companion");
        var (done, result) = await SendActionAsync(label, $"act name={act} iid={iid}");
        history.RequestAnswered(iid, result);
        return done;
    }

    // Result: ok, dup, busy, unsupported, err:<code>, no_reply, lost, not_connected.
    private async Task<(bool Done, string Result)> SendActionAsync(string label, string body)
    {
        if (_link is not { } link || !IsConnected)
        {
            return (false, "not_connected");
        }
        try
        {
            var reply = await link.RequestAsync(body, ReplyTimeout);
            if (reply.IsOk)
            {
                Write(LogKind.Info, $"{label}: done");
                return (true, reply.Arg("dup") == "1" ? "dup" : "ok");
            }
            Write(LogKind.Warning, $"{label}: refused ({reply.Name}{Describe(reply)})");
            return (false, reply.Name is "busy" or "unsupported" ? reply.Name : $"err:{reply.Name}");
        }
        catch (TimeoutException)
        {
            Write(LogKind.Warning, $"{label}: no answer from DeskBuddy");
            return (false, "no_reply");
        }
        catch (Exception e) when (e is IOException or OperationCanceledException)
        {
            return (false, "lost"); // reported by the link
        }
    }

    /// <summary>
    /// Sends a message for DeskBuddy's LED marquee. True when it was taken (showing now, or waiting for the sign).
    /// Never retried: a lost reply cannot make the message show twice (DeskBuddy also ignores a repeated id).
    /// </summary>
    public async Task<bool> SendMessageAsync(string text)
    {
        var cleaned = MessageText.Clean(text);
        if (MessageText.Problem(cleaned) is { } problem)
        {
            Write(LogKind.Warning, $"Message not sent: {problem}");
            return false;
        }
        if (_link is not { } link || !IsConnected)
        {
            return false;
        }
        var id = NextMessageId();
        try
        {
            var reply = await link.RequestAsync(
                $"msg.show id={id} style=marquee text={MessageText.Encode(cleaned)}", ReplyTimeout);
            if (reply.IsOk)
            {
                Write(LogKind.Info, reply.Arg("queued") == "1"
                    ? $"Message {id} waits for the sign (Monster time, or another message is up)"
                    : $"Message {id} sent: \"{cleaned}\"");
                return true;
            }
            Write(LogKind.Warning, $"Message refused ({reply.Name}{Describe(reply)})");
        }
        catch (TimeoutException)
        {
            Write(LogKind.Warning, "Message: no answer from DeskBuddy (not resent)");
        }
        catch (Exception e) when (e is IOException or OperationCanceledException)
        {
        }
        return false;
    }

    // Message ids start at a random point each run, so DeskBuddy does not take a new message for a repeat of one
    // from an earlier run.
    private uint _messageId = (uint)Random.Shared.Next(1, int.MaxValue);

    private uint NextMessageId()
    {
        if (++_messageId == 0)
        {
            _messageId = 1;
        }
        return _messageId;
    }

    /// <summary>Sets Buddy's mood (until changed again). True when DeskBuddy took it.</summary>
    public async Task<bool> SetMoodAsync(string label, string mood)
    {
        if (!await RunAsync(label, DeviceMood.Command(mood)))
        {
            return false;
        }
        if (IsConnected && Mood != mood)
        {
            Mood = mood; // evt mood follows; showing it now keeps the tile from flickering back
            Changed?.Invoke();
        }
        return true;
    }

    public Task SyncClockAsync() => SyncClockAsync("manual");

    private void Tick()
    {
        var now = DateTime.UtcNow;
        _ = Pdc.TickAsync(); // no network at all while the integration is off
        // The hold concerns serial ports only; a simulator connection (TCP) carries on while a tool flashes.
        var simulator = _link?.IsSimulator ?? SelectedPort == DeviceLink.SimulatorName;
        var held = Mutex.TryOpenExisting(PortHoldMutex, out var hold) && !simulator;
        hold?.Dispose();
        if (held)
        {
            if (!_paused)
            {
                _paused = true;
                CloseLink("Paused: an ESP-IDF tool is using the port (flash/monitor)");
            }
            return;
        }
        if (_paused)
        {
            _paused = false;
            _nextAttempt = now.AddSeconds(2); // let the board boot after flashing
            SetConnection("Resuming after the ESP-IDF tool");
        }

        if (_link is null)
        {
            if (AutoConnect && !_connecting && now >= _nextAttempt)
            {
                _ = ConnectAsync();
            }
            return;
        }
        if (!IsConnected)
        {
            return; // handshake running
        }
        if (now >= _nextSync)
        {
            _ = SyncClockAsync("hourly");
        }
        if (now >= _nextPing)
        {
            _ = PingAsync();
        }
        _history?.Tick();
        SendPdcLease();
    }

    private async Task ConnectAsync()
    {
        var auto = SelectedPort is null;
        var port = SelectedPort ?? PortFinder.List().FirstOrDefault(p => p.IsDeskBuddy)?.Name ?? DeviceLink.SimulatorName;
        _connecting = true;
        try
        {
            DeviceLink link;
            try
            {
                link = await Task.Run(() => DeviceLink.Open(port)); // a refused or slow TCP connect must not stall the UI
            }
            catch (UnauthorizedAccessException)
            {
                SetConnection($"{port} is in use by another program (serial monitor?)");
                _nextAttempt = DateTime.UtcNow + RetryDelay;
                return;
            }
            catch (IOException) when (auto && port == DeviceLink.SimulatorName)
            {
                // No board and no simulator: keep looking
                SetConnection("Waiting for DeskBuddy (not plugged in? or start the simulator)");
                _nextAttempt = DateTime.UtcNow.AddSeconds(2);
                return;
            }
            catch (Exception e) when (e is IOException or ArgumentException or InvalidOperationException)
            {
                SetConnection($"Cannot open {port}: {e.Message}");
                _nextAttempt = DateTime.UtcNow + RetryDelay;
                return;
            }
            if (!AutoConnect)
            {
                link.Dispose(); // disconnected while opening
                return;
            }
            Attach(link);
            SetConnection($"Connecting on {port}...");

            Frame? hello = null;
            for (var attempt = 0; attempt < 2 && hello is null && _link == link; attempt++)
            {
                try
                {
                    hello = await link.RequestAsync("hello", ReplyTimeout); // the board may still be booting
                }
                catch (TimeoutException)
                {
                }
                catch (Exception e) when (e is IOException or OperationCanceledException)
                {
                    return; // lost: reported by the link
                }
            }
            if (_link != link)
            {
                return;
            }
            if (hello is not { IsOk: true })
            {
                CloseLink($"No DeskBuddy answer on {port} (other firmware, or not a DeskBuddy?)");
                _nextAttempt = DateTime.UtcNow + RetryDelay;
                return;
            }
            Firmware = hello.Arg("fw");
            Protocol = hello.Arg("proto");
            Caps = (hello.Arg("caps") ?? "").Split(',', StringSplitOptions.RemoveEmptyEntries).ToHashSet();
            Moods = DeviceMood.ParseList(hello.Arg("moods"));
            Acts = DeviceMood.ParseList(hello.Arg("acts")); // the same comma-separated list
            Mood = DeviceMood.Idle; // until status says otherwise
            IsConnected = true;
            if (Caps.Contains("log") && hello.Arg("dev") is { } dev && hello.Arg("boot") is { } boot)
            {
                _history?.Connected(dev, boot, Firmware); // then it asks for what happened meanwhile (log.since)
            }
            _missedPings = 0;
            _nextPing = DateTime.UtcNow + PingInterval;
            SetConnection($"Connected on {port}");
            Write(LogKind.Info, $"Connected to DeskBuddy on {port} (firmware {Firmware}, protocol {hello.Arg("proto")})");
            await SyncClockAsync("connected");
            await RefreshStatusAsync();
            Pdc.ResetDeviceIds(); // the board may have restarted while disconnected
            _pdcLease.Reset();
            SendPdcLease();
        }
        finally
        {
            _connecting = false;
        }
    }

    private void Attach(DeviceLink link)
    {
        _link = link;
        var history = _history = HistoryFor(link.IsSimulator);
        link.LogLine += line => Post(() => Write(LogKind.Device, line));
        link.Traffic += (line, sent) => Post(() => Write(sent ? LogKind.Sent : LogKind.Received, line));
        link.BadFrame += text => Post(() => Write(LogKind.Warning, text));
        link.EventReceived += history.Received; // on the reader: in the device's order, before any later reply
        link.EventReceived += frame => Post(() => OnDeviceEvent(link, frame));
        link.Lost += reason => Post(() =>
        {
            if (_link == link)
            {
                CloseLink(link.IsSimulator
                    ? $"Connection lost ({reason}). Simulator closed? Reconnecting..."
                    : $"Connection lost ({reason}). Unplugged? Reconnecting...");
                _nextAttempt = DateTime.UtcNow.AddSeconds(2);
            }
        });
    }

    private void OnDeviceEvent(DeviceLink link, Frame frame)
    {
        if (_link != link)
        {
            return;
        }
        switch (frame.Name)
        {
        case "mode":
            Mode = frame.Arg("mode") ?? Mode;
            Changed?.Invoke();
            break;
        case "mood":
            Mood = DeviceMood.From(frame, Mood);
            Changed?.Invoke();
            break;
        case "msg":
            Write(LogKind.Info, frame.Arg("state") == "done"
                ? $"Message {frame.Arg("id")} finished on the marquee"
                : $"Message {frame.Arg("id")} is on the marquee");
            break;
        case PdcWire.ClickEvent:
            _ = HandlePdcClickAsync(frame);
            break;
        case "boot":
            Write(LogKind.Info, "DeskBuddy restarted");
            Pdc.ResetDeviceIds();
            _pdcLease.Reset(); // it lost the lease: resent right away
            SendPdcLease();
            if (IsConnected)
            {
                _ = ResyncAfterBootAsync();
            }
            break;
        }
    }

    // The lease: pdc.set right after hello, on every change of state or count, and every 10 s (only to firmware
    // with the pdc capability).
    private void SendPdcLease()
    {
        if (_link is not { } link || !IsConnected || !Caps.Contains(PdcWire.Cap))
        {
            return;
        }
        if (_pdcLease.Next(DateTimeOffset.UtcNow, Pdc.CanClick, Pdc.Count) is { } body)
        {
            _ = SendPdcAsync(link, body);
        }
    }

    private async Task HandlePdcClickAsync(Frame frame)
    {
        if (!Caps.Contains(PdcWire.Cap))
        {
            return;
        }
        if (PdcWire.ClickId(frame) is not { } id)
        {
            Write(LogKind.Warning, "ZDMW click from DeskBuddy without a valid id: ignored");
            return;
        }
        Write(LogKind.Info, $"ZDMW click {id} confirmed on DeskBuddy");
        if (await Pdc.DeviceClickAsync(id) is { } result && _link is { } link && IsConnected)
        {
            await SendPdcAsync(link, PdcWire.Result(id, result));
        }
    }

    private async Task SendPdcAsync(DeviceLink link, string body)
    {
        try
        {
            var reply = await link.RequestAsync(body, ReplyTimeout);
            if (!reply.IsOk)
            {
                Write(LogKind.Warning, $"DeskBuddy refused {body.Split(' ')[0]} ({reply.Name})");
            }
        }
        catch (TimeoutException)
        {
            if (_link == link && body.StartsWith("pdc.set", StringComparison.Ordinal))
            {
                _pdcLease.Reset(); // sent again on the next tick
            }
        }
        catch (Exception e) when (e is IOException or OperationCanceledException)
        {
        }
    }

    private async Task ResyncAfterBootAsync()
    {
        await SyncClockAsync("device restarted");
        await RefreshStatusAsync();
    }

    // The board's and the simulator's histories, each in its own files; created on first use.
    private HistoryRecorder HistoryFor(bool simulator)
    {
        ref var slot = ref simulator ? ref _simulatorHistory : ref _boardHistory;
        if (slot is null)
        {
            var recorder = new HistoryRecorder(_dataDirectory, simulator ? HistorySource.Simulator : HistorySource.Board);
            recorder.Log += text => Post(() => Write(LogKind.Info, text));
            recorder.FetchNeeded += request => Post(() => _ = FetchHistoryAsync(recorder, request));
            slot = recorder;
        }
        return slot;
    }

    // log.since for the history: the replayed events arrive (and are queued) before this reply.
    private async Task FetchHistoryAsync(HistoryRecorder recorder, FetchRequest request)
    {
        if (_history != recorder || _link is not { } link || !IsConnected)
        {
            recorder.ReplayFailed(request, "not connected");
            return;
        }
        try
        {
            recorder.ReplayAnswered(request, await link.RequestAsync(request.Command, ReplayTimeout));
        }
        catch (TimeoutException)
        {
            recorder.ReplayFailed(request, "no answer");
        }
        catch (Exception e) when (e is IOException or OperationCanceledException)
        {
            recorder.ReplayFailed(request, "connection lost");
        }
    }

    private async Task SyncClockAsync(string reason)
    {
        if (_link is not { } link || !IsConnected || !Caps.Contains("time"))
        {
            return;
        }
        _nextSync = DateTime.UtcNow + SyncInterval;
        TimeZoneInfo.ClearCachedData();
        var zone = TimeZoneInfo.Local;
        try
        {
            var reply = await link.RequestAsync(TimeSync.Command(DateTimeOffset.UtcNow, zone), ReplyTimeout);
            if (reply.IsOk)
            {
                var local = DateTimeOffset.Now;
                ClockText = $"synced {local:HH:mm:ss} ({reason}), UTC{local:zzz}";
                var name = zone.IsDaylightSavingTime(local) ? zone.DaylightName : zone.StandardName;
                Write(LogKind.Info, $"Clock synced ({reason}): {local:yyyy-MM-dd HH:mm:ss} {name}");
                Changed?.Invoke();
            }
            else
            {
                Write(LogKind.Warning, $"Clock sync refused: {reply.Name}");
            }
        }
        catch (TimeoutException)
        {
            Write(LogKind.Warning, "Clock sync: no answer");
            _nextSync = DateTime.UtcNow.AddMinutes(1);
        }
        catch (Exception e) when (e is IOException or OperationCanceledException)
        {
        }
    }

    private async Task RefreshStatusAsync()
    {
        if (_link is not { } link || !IsConnected)
        {
            return;
        }
        try
        {
            var reply = await link.RequestAsync("status", ReplyTimeout);
            if (reply.IsOk)
            {
                Mode = reply.Arg("mode") ?? Mode;
                Mood = DeviceMood.From(reply, Mood);
                Changed?.Invoke();
            }
        }
        catch (Exception e) when (e is TimeoutException or IOException or OperationCanceledException)
        {
        }
    }

    private async Task PingAsync()
    {
        if (_link is not { } link)
        {
            return;
        }
        _nextPing = DateTime.UtcNow + PingInterval;
        try
        {
            await link.RequestAsync("ping", ReplyTimeout);
            _missedPings = 0;
        }
        catch (TimeoutException)
        {
            if (++_missedPings >= 2 && _link == link)
            {
                CloseLink("DeskBuddy stopped answering. Reconnecting...");
                _nextAttempt = DateTime.UtcNow.AddSeconds(2);
            }
        }
        catch (Exception e) when (e is IOException or OperationCanceledException)
        {
        }
    }

    private void CloseLink(string text)
    {
        var link = _link;
        _link = null;
        IsConnected = false;
        Mode = "unknown";
        Mood = DeviceMood.Idle;
        link?.Dispose();
        _history?.Disconnected();
        _history = null;
        _pdcLease.Reset();
        if (link is not null)
        {
            Write(LogKind.Info, text);
        }
        SetConnection(text);
    }

    private void SetConnection(string text)
    {
        if (ConnectionText != text)
        {
            ConnectionText = text;
            Changed?.Invoke();
        }
    }

    private void Write(LogKind kind, string text) => Log?.Invoke(kind, text);

    private void Post(Action action) => _ui.Post(_ => action(), null);

    private static string Describe(Frame reply) =>
        reply.Args.Count == 0 ? "" : ", " + string.Join(" ", reply.Args.Select(a => $"{a.Key}={a.Value}"));

    private void OnTimeChanged(object? sender, EventArgs e) => Post(() =>
    {
        if (IsConnected)
        {
            _ = SyncClockAsync("Windows time or time zone changed");
        }
    });

    private void OnPowerModeChanged(object? sender, PowerModeChangedEventArgs e)
    {
        if (e.Mode == PowerModes.Resume)
        {
            Post(() =>
            {
                _nextAttempt = DateTime.MinValue;
                if (IsConnected)
                {
                    _ = SyncClockAsync("laptop woke up");
                }
            });
        }
    }

    public void Dispose()
    {
        SystemEvents.TimeChanged -= OnTimeChanged;
        SystemEvents.PowerModeChanged -= OnPowerModeChanged;
        _tick.Dispose();
        if (_link is { } link && IsConnected && Caps.Contains(PdcWire.Cap))
        {
            _ = SendPdcAsync(link, PdcWire.Set(false, null)); // best effort: written before the port closes
        }
        _link?.Dispose();
        _boardHistory?.Dispose(); // the last lines reach the disk
        _simulatorHistory?.Dispose();
        Pdc.Dispose();
    }
}
