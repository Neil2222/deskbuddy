using System.Text.Json;

namespace DeskBuddy.Companion.Pdc;

public enum PdcReach { Unknown, Reachable, Unreachable, Certificate }

public enum PdcAuth
{
    /// <summary>Integration off.</summary>
    None,
    /// <summary>A stored session that has not been checked yet (the server was not reachable so far).</summary>
    Stored,
    LoggedIn,
    LoginRequired,
}

public enum PdcAccess { None, Read, Write }

/// <summary>
/// PDC Manager's "Ze Doen Maar Wat" (ZDMW) button, built in and off by default. Keeps the login (session cookies,
/// DPAPI-encrypted), whether the server is reachable, the user's ZDMW permission and the count, and sends clicks:
/// one at a time, at most one per 5 s, never retried, with the outcome told apart into "not sent" and "maybe sent".
/// While it is off it makes no network traffic at all. Used on the UI thread (the tests drive it directly).
/// </summary>
public sealed class PdcIntegration : IDisposable
{
    public static readonly TimeSpan Cooldown = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan CheckInterval = TimeSpan.FromSeconds(60);
    public static readonly TimeSpan HttpTimeout = TimeSpan.FromSeconds(10);

    private readonly PdcClient _client;
    private readonly PdcStore _store;
    private readonly TimeProvider _clock;
    private readonly Dictionary<int, PdcClickResult?> _deviceIds = []; // null: still on its way
    private Dictionary<string, string>? _savedCookies;
    private int _epoch; // bumped by enable/disable/login/logout: answers to older requests are dropped
    private bool _checking;
    private bool _countDue, _profileDue;
    private DateTimeOffset _nextCheck = DateTimeOffset.MinValue;
    private DateTimeOffset? _lastClickStart;

    public PdcIntegration(Uri baseUri, HttpMessageHandler handler, PdcStore store, TimeProvider clock, TimeSpan? httpTimeout = null)
    {
        _client = new PdcClient(baseUri, handler, httpTimeout ?? HttpTimeout);
        _store = store;
        _clock = clock;
    }

    /// <summary>The real thing: https://pdcmanager (or a loopback fake server from DESKBUDDY_PDC_URL), the pinned
    /// certificate, and %APPDATA%\DeskBuddy.</summary>
    public static PdcIntegration CreateDefault(out string? warning)
    {
        var baseUri = PdcEndpoint.Resolve(Environment.GetEnvironmentVariable(PdcEndpoint.EnvironmentVariable), out warning);
        var handler = PdcCertificate.CreateHandler(PdcCertificate.LoadPinned());
        return new PdcIntegration(baseUri, handler, new PdcStore(PdcStore.DefaultDirectory), TimeProvider.System);
    }

    public Uri BaseUri => _client.BaseUri;
    public bool IsDefaultServer => BaseUri == PdcEndpoint.Default;
    public bool Enabled { get; private set; }
    public PdcReach Reach { get; private set; }
    public PdcAuth Auth { get; private set; }
    /// <summary>Why the last login did not work, or why the session ended; null otherwise.</summary>
    public string? LoginProblem { get; private set; }
    /// <summary>The user name last logged in with (settings.json), to fill in the login form.</summary>
    public string? SavedUsername { get; private set; }
    /// <summary>The account's user name (profile.username): what a click is sent with.</summary>
    public string? Username { get; private set; }
    public string? DisplayName { get; private set; }
    public PdcAccess Access { get; private set; }
    public long? Count { get; private set; }
    public bool LoggingIn { get; private set; }
    public bool Clicking { get; private set; }
    public PdcClickResult? LastClick { get; private set; }

    /// <summary>When the next click is allowed; null: now.</summary>
    public DateTimeOffset? CooldownUntil =>
        _lastClickStart is { } t && _clock.GetUtcNow() < t + Cooldown ? t + Cooldown : null;

    /// <summary>Clicks can be sent: on, logged in, reachable, with write permission. This is also the device's state.</summary>
    public bool CanClick => Enabled && Auth == PdcAuth.LoggedIn && Reach == PdcReach.Reachable && Access == PdcAccess.Write;

    public event Action? Changed;
    public event Action<LogKind, string>? Log;

    private DateTimeOffset Now => _clock.GetUtcNow();

    /// <summary>Reads the settings; when the integration is on, checks the stored session.</summary>
    public async Task StartAsync()
    {
        var (enabled, user) = _store.LoadSettings();
        SavedUsername = user;
        if (enabled)
        {
            await ActivateAsync();
        }
        else
        {
            Changed?.Invoke();
        }
    }

    public async Task SetEnabledAsync(bool on)
    {
        if (on == Enabled)
        {
            return;
        }
        _store.SaveSettings(on, SavedUsername);
        if (on)
        {
            Write(LogKind.Info, $"PDC Manager integration on ({BaseUri.Host})");
            await ActivateAsync();
        }
        else
        {
            _epoch++;
            Enabled = false;
            Reset(PdcAuth.None);
            _client.SetCookies(new Dictionary<string, string>());
            Write(LogKind.Info, "PDC Manager integration off");
            Changed?.Invoke();
        }
    }

    private async Task ActivateAsync()
    {
        _epoch++;
        Enabled = true;
        var cookies = _store.LoadSession(BaseUri);
        _client.SetCookies(cookies ?? new Dictionary<string, string>());
        _savedCookies = cookies;
        Reset(cookies is null ? PdcAuth.LoginRequired : PdcAuth.Stored);
        Changed?.Invoke();
        await CheckAsync();
    }

    private void Reset(PdcAuth auth)
    {
        Reach = PdcReach.Unknown;
        Auth = auth;
        LoginProblem = null;
        Username = DisplayName = null;
        Access = PdcAccess.None;
        Count = null;
        LastClick = null;
        _countDue = _profileDue = false;
        _nextCheck = DateTimeOffset.MinValue;
    }

    /// <summary>Called every second: rechecks reachability, the session and the count every 60 s, and the count
    /// right after a click. Does nothing while the integration is off.</summary>
    public Task TickAsync()
    {
        if (!Enabled || _checking || LoggingIn)
        {
            return Task.CompletedTask;
        }
        var soon = (_countDue || _profileDue) && Reach == PdcReach.Reachable && Auth == PdcAuth.LoggedIn;
        return Now >= _nextCheck || soon ? CheckAsync() : Task.CompletedTask;
    }

    // Not reachable or not logged in: GET /health first (all an unreachable server gets). With a session: the
    // profile (when not checked yet) and the count.
    private async Task CheckAsync()
    {
        if (!Enabled || _checking)
        {
            return;
        }
        _checking = true;
        var epoch = _epoch;
        _nextCheck = Now + CheckInterval;
        try
        {
            if (Reach != PdcReach.Reachable || Auth != PdcAuth.LoggedIn)
            {
                var health = await _client.GetAsync("/health");
                if (epoch != _epoch || !Reached(health))
                {
                    return;
                }
                if (_client.Cookies.Count == 0)
                {
                    Auth = PdcAuth.LoginRequired;
                    return;
                }
                _profileDue |= Auth != PdcAuth.LoggedIn;
            }
            if (_profileDue && !await LoadProfileAsync(epoch))
            {
                return;
            }
            if (Access != PdcAccess.None)
            {
                await LoadCountAsync(epoch);
            }
        }
        finally
        {
            _checking = false;
            Changed?.Invoke();
        }
    }

    private async Task<bool> LoadProfileAsync(int epoch)
    {
        var r = await _client.GetAsync("/auth/api/me/profile");
        if (epoch != _epoch || !Reached(r))
        {
            return false;
        }
        if (r.IsLoginRequired)
        {
            DropSession();
            return false;
        }
        if (r.Status != 200 || r.Json() is not { } json || !Profile(json))
        {
            Write(LogKind.Warning, $"PDC Manager profile: unexpected answer (HTTP {r.Status})");
            return false;
        }
        Auth = PdcAuth.LoggedIn;
        LoginProblem = null;
        _profileDue = false;
        Persist();
        return true;
    }

    // {"success":true,"profile":{"username":...,"display_name":...,"role":...,"blueprint_access":{"zdmw":...}}}
    private bool Profile(JsonElement json)
    {
        if (json.ValueKind != JsonValueKind.Object || !json.TryGetProperty("success", out var ok) || ok.ValueKind != JsonValueKind.True ||
            !json.TryGetProperty("profile", out var p) || p.ValueKind != JsonValueKind.Object ||
            Str(p, "username") is not { Length: > 0 } user)
        {
            return false;
        }
        Username = user;
        DisplayName = Str(p, "display_name");
        var zdmw = p.TryGetProperty("blueprint_access", out var access) && access.ValueKind == JsonValueKind.Object
            ? Str(access, "zdmw")
            : null;
        Access = AccessFrom(Str(p, "role"), zdmw);
        return true;
    }

    /// <summary>ZDMW permission as PDC Manager decides it: admins and write/admin access may click; read sees the count.</summary>
    public static PdcAccess AccessFrom(string? role, string? zdmw) =>
        role == "admin" || zdmw is "write" or "admin" ? PdcAccess.Write
        : zdmw == "read" ? PdcAccess.Read
        : PdcAccess.None;

    private async Task LoadCountAsync(int epoch)
    {
        var r = await _client.GetAsync("/zdmw/api/count");
        if (epoch != _epoch || !Reached(r))
        {
            return;
        }
        _countDue = false;
        if (r.IsLoginRequired)
        {
            DropSession();
            return;
        }
        if (r.Status == 403)
        {
            Access = PdcAccess.None; // taken away meanwhile
            _profileDue = true;
            return;
        }
        if (r.Status == 200 && r.Json() is { } json && Success(json) && Long(json, "count") is { } count)
        {
            Count = count;
            Persist();
        }
        else
        {
            Write(LogKind.Warning, $"ZDMW count: unexpected answer (HTTP {r.Status})");
        }
    }

    /// <summary>Logs in (POST /auth/login). The password is only sent, never kept or logged.</summary>
    public async Task<bool> LoginAsync(string username, string password)
    {
        username = username.Trim();
        if (!Enabled || LoggingIn || username.Length == 0 || password.Length == 0)
        {
            return false;
        }
        var epoch = ++_epoch;
        LoggingIn = true;
        _store.DeleteSession();
        _client.SetCookies(new Dictionary<string, string>());
        _savedCookies = null;
        var reach = Reach;
        Reset(PdcAuth.LoginRequired);
        Reach = reach;
        SavedUsername = username;
        _store.SaveSettings(true, username);
        Changed?.Invoke();
        try
        {
            var r = await _client.PostFormAsync("/auth/login", [new("username", username), new("password", password)]);
            if (epoch != _epoch)
            {
                return false;
            }
            if (!Reached(r))
            {
                LoginProblem = Reach == PdcReach.Certificate ? "Certificate not trusted" : "PDC Manager not reachable";
                return false;
            }
            if (r.IsRedirect && !r.IsLoginRequired)
            {
                if (_client.Cookies.Count == 0)
                {
                    LoginProblem = "Logged in, but no session came back";
                    return false;
                }
                Persist();
                Auth = PdcAuth.Stored;
                _profileDue = true;
                if (!await LoadProfileAsync(epoch))
                {
                    LoginProblem ??= "Logged in, but the profile could not be read";
                    return false;
                }
                Write(LogKind.Info, $"Logged in to PDC Manager as {Username}");
                if (Access != PdcAccess.None)
                {
                    await LoadCountAsync(epoch);
                }
                return true;
            }
            if (r.Status == 200 && r.Body.Contains("set_password", StringComparison.Ordinal))
            {
                LoginProblem = "This account has no password yet: set one in the browser first";
            }
            else if (r.Status is 200 or 401 || r.IsLoginRequired)
            {
                LoginProblem = "Wrong username or password";
            }
            else
            {
                LoginProblem = $"Login failed (HTTP {r.Status})";
            }
            Write(LogKind.Warning, $"PDC Manager login failed (HTTP {r.Status})");
            return false;
        }
        finally
        {
            LoggingIn = false;
            _nextCheck = Now + CheckInterval;
            Changed?.Invoke();
        }
    }

    /// <summary>Forgets the session (deletes the stored one).</summary>
    public void Logout()
    {
        _epoch++;
        ForgetSession();
        LoginProblem = null;
        Write(LogKind.Info, "Logged out of PDC Manager");
        Changed?.Invoke();
    }

    private void DropSession()
    {
        ForgetSession();
        LoginProblem = "Session expired: log in again";
        Write(LogKind.Warning, "PDC Manager session expired: login required");
    }

    private void ForgetSession()
    {
        _store.DeleteSession();
        _client.SetCookies(new Dictionary<string, string>());
        _savedCookies = null;
        Auth = Enabled ? PdcAuth.LoginRequired : PdcAuth.None;
        Username = DisplayName = null;
        Access = PdcAccess.None;
        Count = null;
        _countDue = _profileDue = false;
    }

    // Keeps the (sliding) session on disk whenever the server renewed it.
    private void Persist()
    {
        var cookies = _client.Cookies;
        if (cookies.Count > 0 && (_savedCookies is null || !_savedCookies.OrderBy(c => c.Key).SequenceEqual(cookies.OrderBy(c => c.Key))))
        {
            _store.SaveSession(BaseUri, cookies);
            _savedCookies = new Dictionary<string, string>(cookies);
        }
    }

    // True for any HTTP answer; otherwise notes that the server is not reachable (or its certificate is refused).
    private bool Reached(PdcResponse r)
    {
        var reach = r.Error switch
        {
            PdcNetError.None => PdcReach.Reachable,
            PdcNetError.Tls => PdcReach.Certificate,
            _ => PdcReach.Unreachable,
        };
        if (reach != Reach)
        {
            Write(reach == PdcReach.Reachable ? LogKind.Info : LogKind.Warning, reach switch
            {
                PdcReach.Reachable => $"PDC Manager reachable ({BaseUri.Host})",
                PdcReach.Certificate => "PDC Manager: certificate not trusted",
                _ => $"PDC Manager not reachable ({r.Error.ToString().ToLowerInvariant()}); next check in 60 s",
            });
            Reach = reach;
        }
        return r.Error == PdcNetError.None;
    }

    /// <summary>Sends one ZDMW click (from the window or the device). Never retried.</summary>
    public async Task<PdcClickResult> ClickAsync()
    {
        if (!CanClick || Username is not { } user)
        {
            return new PdcClickResult(PdcOutcome.Unavailable, Count);
        }
        if (Clicking)
        {
            return new PdcClickResult(PdcOutcome.Busy, Count);
        }
        if (CooldownUntil is not null)
        {
            return new PdcClickResult(PdcOutcome.Cooldown, Count);
        }
        Clicking = true;
        _lastClickStart = Now;
        Changed?.Invoke();
        PdcClickResult result;
        try
        {
            var r = await _client.PostJsonAsync("/zdmw/api/click", new Dictionary<string, string> { ["username"] = user });
            result = ClickResult(r);
        }
        finally
        {
            Clicking = false;
        }
        LastClick = result;
        _countDue = Auth == PdcAuth.LoggedIn;
        Write(result.Outcome == PdcOutcome.Ok ? LogKind.Info : LogKind.Warning, $"ZDMW click: {PdcWire.Name(result.Outcome)}" +
              (result.Outcome == PdcOutcome.Ok ? $", count {result.Count}, Teams {(result.Teams == true ? "sent" : "not sent")}" : ""));
        Changed?.Invoke();
        return result;
    }

    private PdcClickResult ClickResult(PdcResponse r)
    {
        switch (r.Error)
        {
        case PdcNetError.Offline:
        case PdcNetError.Tls:
            Reached(r);
            return new PdcClickResult(r.Error == PdcNetError.Tls ? PdcOutcome.Tls : PdcOutcome.Offline, Count);
        case PdcNetError.Timeout:
            return new PdcClickResult(PdcOutcome.Timeout, Count);
        case PdcNetError.Unknown:
            return new PdcClickResult(PdcOutcome.Unknown, Count);
        }
        if (r.IsLoginRequired)
        {
            DropSession();
            return new PdcClickResult(PdcOutcome.Auth);
        }
        if (r.Status == 403)
        {
            _profileDue = true;
            return new PdcClickResult(PdcOutcome.Denied, Count);
        }
        if (r.Status == 200 && r.Json() is { } json && Success(json))
        {
            Persist();
            var count = Long(json, "count");
            Count = count ?? Count;
            var teams = json.TryGetProperty("webhook_sent", out var w) && w.ValueKind is JsonValueKind.True or JsonValueKind.False
                ? w.ValueKind == JsonValueKind.True
                : (bool?)null;
            return new PdcClickResult(PdcOutcome.Ok, count, teams);
        }
        Write(LogKind.Warning, $"ZDMW click: unexpected answer (HTTP {r.Status})");
        return new PdcClickResult(PdcOutcome.Unknown, Count);
    }

    /// <summary>
    /// A click confirmed on the device (<c>evt pdc.click id=</c>). Returns the result to send back, or null to stay
    /// quiet: an id seen before never sends a second click (its known result is returned again; still on its way:
    /// null).
    /// </summary>
    public async Task<PdcClickResult?> DeviceClickAsync(int id)
    {
        if (_deviceIds.TryGetValue(id, out var known))
        {
            Write(LogKind.Info, known is null ? $"ZDMW click {id} from DeskBuddy again: still on its way"
                                              : $"ZDMW click {id} from DeskBuddy again: not sent twice");
            return known;
        }
        _deviceIds[id] = null;
        var result = await ClickAsync();
        if (_deviceIds.ContainsKey(id))
        {
            _deviceIds[id] = result;
        }
        return result;
    }

    /// <summary>The device restarted: its click ids start afresh.</summary>
    public void ResetDeviceIds() => _deviceIds.Clear();

    private static bool Success(JsonElement json) =>
        json.ValueKind == JsonValueKind.Object && json.TryGetProperty("success", out var ok) && ok.ValueKind == JsonValueKind.True;

    private static string? Str(JsonElement o, string name) =>
        o.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

    private static long? Long(JsonElement o, string name) =>
        o.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number && v.TryGetInt64(out var n) && n >= 0 ? n : null;

    private void Write(LogKind kind, string text) => Log?.Invoke(kind, text);

    public void Dispose() => _client.Dispose();
}
