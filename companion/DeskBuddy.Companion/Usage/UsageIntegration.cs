using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using DeskBuddy.Companion.Pdc;

namespace DeskBuddy.Companion.Usage;

/// <summary>
/// The Claude plan usage strip, built in and off by default: every <see cref="PollInterval"/> it reads Claude Code's
/// OAuth token from <c>%USERPROFILE%\.claude\.credentials.json</c> (again on every poll: Claude Code rewrites it) and
/// asks <c>GET https://api.anthropic.com/api/oauth/usage</c> how much of the 5-hour and 7-day windows is used. The
/// token is never logged, shown, refreshed or written: an expired login is Claude Code's to renew. While it is off it
/// makes no network traffic at all. Used on the UI thread.
/// </summary>
public sealed class UsageIntegration : IDisposable
{
    public static readonly Uri Endpoint = new("https://api.anthropic.com/api/oauth/usage");
    public static readonly TimeSpan PollInterval = TimeSpan.FromSeconds(60);
    public static readonly TimeSpan RateLimitedInterval = TimeSpan.FromMinutes(5);
    /// <summary>How long the last numbers are kept when polls fail for a passing reason.</summary>
    public static readonly TimeSpan StaleAfter = TimeSpan.FromMinutes(5);
    public static readonly TimeSpan HttpTimeout = TimeSpan.FromSeconds(15);

    public const string LoginExpiredText = "Claude Code login expired: run claude to renew it";
    public const string NotLoggedInText = "Claude Code is not logged in on this PC (no .claude\\.credentials.json): run claude and log in";

    private readonly HttpClient _http;
    private readonly string _credentialsPath;
    private readonly PdcStore _store;
    private readonly TimeProvider _clock;
    private int _epoch; // bumped by on/off: answers to older requests are dropped
    private bool _polling;
    private DateTimeOffset _nextPoll = DateTimeOffset.MinValue;
    private UsageSnapshot? _last;
    private DateTimeOffset _lastAt;

    public UsageIntegration(HttpMessageHandler handler, string credentialsPath, PdcStore store, TimeProvider clock)
    {
        _http = new HttpClient(handler) { Timeout = HttpTimeout };
        _credentialsPath = credentialsPath;
        _store = store;
        _clock = clock;
    }

    /// <summary>The real thing: the live endpoint, the current user's Claude Code credentials, settings.json in
    /// %APPDATA%\DeskBuddy.</summary>
    public static UsageIntegration CreateDefault() =>
        new(new SocketsHttpHandler(), DefaultCredentialsPath, new PdcStore(PdcStore.DefaultDirectory), TimeProvider.System);

    public static string DefaultCredentialsPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".claude", ".credentials.json");

    public bool Enabled { get; private set; }
    /// <summary>What is going on, for the window; null while off.</summary>
    public string? Status { get; private set; }
    /// <summary>True while <see cref="Status"/> is a problem.</summary>
    public bool Problem { get; private set; }

    /// <summary>The numbers to show: on, fresh (at most <see cref="StaleAfter"/> old) and with at least one window;
    /// otherwise null. This is also what the device is told.</summary>
    public UsageSnapshot? Usage =>
        Enabled && _last is { IsEmpty: false } last && Now - _lastAt <= StaleAfter ? last : null;

    public event Action? Changed;
    public event Action<LogKind, string>? Log;

    private DateTimeOffset Now => _clock.GetUtcNow();

    /// <summary>Reads the setting; when on, polls at once.</summary>
    public Task StartAsync()
    {
        if (_store.LoadUsageEnabled())
        {
            return ActivateAsync();
        }
        Changed?.Invoke();
        return Task.CompletedTask;
    }

    public Task SetEnabledAsync(bool on)
    {
        if (on == Enabled)
        {
            return Task.CompletedTask;
        }
        _store.SaveUsageEnabled(on);
        if (on)
        {
            Write(LogKind.Info, "Claude usage on");
            return ActivateAsync();
        }
        _epoch++;
        Enabled = false;
        _last = null;
        Status = null;
        Problem = false;
        Write(LogKind.Info, "Claude usage off");
        Changed?.Invoke();
        return Task.CompletedTask;
    }

    private Task ActivateAsync()
    {
        _epoch++;
        Enabled = true;
        _last = null;
        Status = "checking…";
        Problem = false;
        _nextPoll = DateTimeOffset.MinValue;
        Changed?.Invoke();
        return PollAsync();
    }

    /// <summary>Called every second: polls every 60 s (5 min after a 429). Does nothing while off.</summary>
    public Task TickAsync() => Enabled && !_polling && Now >= _nextPoll ? PollAsync() : Task.CompletedTask;

    /// <summary>One poll: the token from the file, then the endpoint.</summary>
    public async Task PollAsync()
    {
        if (!Enabled || _polling)
        {
            return;
        }
        _polling = true;
        var epoch = _epoch;
        _nextPoll = Now + PollInterval;
        try
        {
            var token = ReadToken(out var problem);
            if (token is null)
            {
                Fail(problem!, drop: true);
                return;
            }
            using var request = new HttpRequestMessage(HttpMethod.Get, Endpoint);
            request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            request.Headers.Add("anthropic-beta", "oauth-2025-04-20");
            request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
            request.Headers.UserAgent.Add(new ProductInfoHeaderValue("DeskBuddy", Version));
            int status;
            string body;
            try
            {
                using var response = await _http.SendAsync(request);
                status = (int)response.StatusCode;
                body = await response.Content.ReadAsStringAsync();
            }
            catch (Exception e) when (e is HttpRequestException or TaskCanceledException or IOException)
            {
                if (epoch == _epoch)
                {
                    Fail(e is TaskCanceledException ? "no answer from api.anthropic.com in time" : "api.anthropic.com not reachable", drop: false);
                }
                return;
            }
            if (epoch != _epoch)
            {
                return;
            }
            if (status is (int)HttpStatusCode.Unauthorized or (int)HttpStatusCode.Forbidden)
            {
                Fail(LoginExpiredText, drop: true);
                return;
            }
            if (status == (int)HttpStatusCode.TooManyRequests)
            {
                _nextPoll = Now + RateLimitedInterval;
                Fail("rate limited by api.anthropic.com: trying again in 5 minutes", drop: false);
                return;
            }
            if (status is < 200 or >= 300 || UsageApi.Parse(body, Now) is not { } usage)
            {
                Fail(status is >= 200 and < 300 ? "unexpected answer from api.anthropic.com" : $"api.anthropic.com answered HTTP {status}", drop: false);
                return;
            }
            _last = usage;
            _lastAt = Now;
            Status = usage.IsEmpty ? "logged in; no usage windows reported" : $"updated {Now.ToLocalTime():HH:mm}";
            Problem = false;
        }
        finally
        {
            _polling = false;
            Changed?.Invoke();
        }
    }

    // The access token, or null with the reason. An expired token is not sent (and never refreshed here).
    private string? ReadToken(out string? problem)
    {
        problem = null;
        string text;
        try
        {
            text = File.ReadAllText(_credentialsPath);
        }
        catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException)
        {
            problem = NotLoggedInText;
            return null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            problem = "cannot read Claude Code's credentials file";
            return null;
        }
        try
        {
            using var doc = JsonDocument.Parse(text);
            if (doc.RootElement.ValueKind != JsonValueKind.Object ||
                !doc.RootElement.TryGetProperty("claudeAiOauth", out var oauth) || oauth.ValueKind != JsonValueKind.Object ||
                !oauth.TryGetProperty("accessToken", out var t) || t.ValueKind != JsonValueKind.String || t.GetString() is not { Length: > 0 } token)
            {
                problem = NotLoggedInText;
                return null;
            }
            if (oauth.TryGetProperty("expiresAt", out var exp) && exp.ValueKind == JsonValueKind.Number && exp.TryGetInt64(out var ms) &&
                DateTimeOffset.FromUnixTimeMilliseconds(ms) <= Now)
            {
                problem = LoginExpiredText;
                return null;
            }
            return token;
        }
        catch (JsonException)
        {
            problem = "Claude Code's credentials file is damaged";
            return null;
        }
    }

    // drop: the numbers are wrong now (login gone); otherwise they are kept until they are StaleAfter old.
    private void Fail(string problem, bool drop)
    {
        if (drop)
        {
            _last = null;
        }
        if (Status != problem)
        {
            Write(LogKind.Warning, $"Claude usage: {problem}");
        }
        Status = problem;
        Problem = true;
    }

    private static string Version =>
        typeof(UsageIntegration).Assembly.GetName().Version is { } v ? $"{v.Major}.{v.Minor}.{v.Build}" : "1.0";

    private void Write(LogKind kind, string text) => Log?.Invoke(kind, text);

    public void Dispose() => _http.Dispose();
}
