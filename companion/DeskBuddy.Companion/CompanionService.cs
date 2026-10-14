using DeskBuddy.Companion.Device;
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

    public bool AutoConnect { get; private set; } = true;
    /// <summary>Port chosen by the user; null: the first DeskBuddy (CH343) port found.</summary>
    public string? SelectedPort { get; set; }

    public bool IsConnected { get; private set; }
    public string ConnectionText { get; private set; } = "Starting";
    public string? Firmware { get; private set; }
    public string Mode { get; private set; } = "unknown";
    public string ClockText { get; private set; } = "not synced";
    public IReadOnlySet<string> Caps { get; private set; } = new HashSet<string>();

    public event Action? Changed;
    public event Action<LogKind, string>? Log;

    public CompanionService()
    {
        _ui = SynchronizationContext.Current ?? throw new InvalidOperationException("create on the UI thread");
        _tick.Tick += (_, _) => Tick();
        SystemEvents.TimeChanged += OnTimeChanged; // also raised when the time zone changes
        SystemEvents.PowerModeChanged += OnPowerModeChanged;
    }

    public void Start()
    {
        _tick.Start();
        Tick();
    }

    /// <summary>Connect now (to <see cref="SelectedPort"/> or the auto-detected board) and keep reconnecting.</summary>
    public void Connect()
    {
        AutoConnect = true;
        _nextAttempt = DateTime.MinValue;
        Tick();
    }

    /// <summary>Disconnect and stay disconnected until <see cref="Connect"/>, e.g. to use the port elsewhere.</summary>
    public void Disconnect()
    {
        AutoConnect = false;
        CloseLink("Disconnected (auto-connect off)");
    }

    public async Task RunAsync(string label, string body)
    {
        if (_link is not { } link || !IsConnected)
        {
            return;
        }
        try
        {
            var reply = await link.RequestAsync(body, ReplyTimeout);
            if (reply.IsOk)
            {
                Write(LogKind.Info, $"{label}: done");
            }
            else
            {
                Write(LogKind.Warning, $"{label}: refused ({reply.Name}{Describe(reply)})");
            }
        }
        catch (TimeoutException)
        {
            Write(LogKind.Warning, $"{label}: no answer from DeskBuddy");
        }
        catch (Exception e) when (e is IOException or OperationCanceledException)
        {
            // Lost: reported by the link
        }
    }

    public Task SyncClockAsync() => SyncClockAsync("manual");

    private void Tick()
    {
        var now = DateTime.UtcNow;
        var held = Mutex.TryOpenExisting(PortHoldMutex, out var hold);
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
    }

    private async Task ConnectAsync()
    {
        var port = SelectedPort ?? PortFinder.List().FirstOrDefault(p => p.IsDeskBuddy)?.Name;
        if (port is null)
        {
            SetConnection("Waiting for DeskBuddy (not plugged in?)");
            _nextAttempt = DateTime.UtcNow.AddSeconds(2);
            return;
        }
        _connecting = true;
        try
        {
            DeviceLink link;
            try
            {
                link = DeviceLink.Open(port);
            }
            catch (UnauthorizedAccessException)
            {
                SetConnection($"{port} is in use by another program (serial monitor?)");
                _nextAttempt = DateTime.UtcNow + RetryDelay;
                return;
            }
            catch (Exception e) when (e is IOException or ArgumentException or InvalidOperationException)
            {
                SetConnection($"Cannot open {port}: {e.Message}");
                _nextAttempt = DateTime.UtcNow + RetryDelay;
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
            Caps = (hello.Arg("caps") ?? "").Split(',', StringSplitOptions.RemoveEmptyEntries).ToHashSet();
            IsConnected = true;
            _missedPings = 0;
            _nextPing = DateTime.UtcNow + PingInterval;
            SetConnection($"Connected on {port}");
            Write(LogKind.Info, $"Connected to DeskBuddy on {port} (firmware {Firmware}, protocol {hello.Arg("proto")})");
            await SyncClockAsync("connected");
            await RefreshStatusAsync();
        }
        finally
        {
            _connecting = false;
        }
    }

    private void Attach(DeviceLink link)
    {
        _link = link;
        link.LogLine += line => Post(() => Write(LogKind.Device, line));
        link.Traffic += (line, sent) => Post(() => Write(sent ? LogKind.Sent : LogKind.Received, line));
        link.BadFrame += text => Post(() => Write(LogKind.Warning, text));
        link.EventReceived += frame => Post(() => OnDeviceEvent(link, frame));
        link.Lost += reason => Post(() =>
        {
            if (_link == link)
            {
                CloseLink($"Connection lost ({reason}). Unplugged? Reconnecting...");
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
        case "boot":
            Write(LogKind.Info, "DeskBuddy restarted");
            if (IsConnected)
            {
                _ = ResyncAfterBootAsync();
            }
            break;
        }
    }

    private async Task ResyncAfterBootAsync()
    {
        await SyncClockAsync("device restarted");
        await RefreshStatusAsync();
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
        link?.Dispose();
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
        _link?.Dispose();
    }
}
