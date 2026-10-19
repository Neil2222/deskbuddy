using System.Net;
using System.Net.Http.Headers;
using DeskBuddy.Companion.Pdc;

namespace DeskBuddy.Companion.Usage;

/// <summary>
/// The Claude plan usage strip, built in and off by default: every <see cref="PollInterval"/> it asks
/// <c>GET https://claude.ai/api/organizations/{orgId}/usage</c> how much of the 5-hour and 7-day windows is used, with
/// the org ID and claude.ai session key the user pasted into the window (see <see cref="PdcStore"/>). The key is never
/// logged or shown. An expired key stops polling until a new one is saved or the switch is toggled. While it is off
/// (or not filled in) it makes no network traffic at all. Used on the UI thread.
/// </summary>
public sealed class UsageIntegration : IDisposable
{
    public static readonly Uri BaseUri = new("https://claude.ai/");
    public static readonly TimeSpan PollInterval = TimeSpan.FromSeconds(60);
    /// <summary>Back-off after the first 429 without Retry-After; later ones wait <see cref="MaxRateLimitedInterval"/>.</summary>
    public static readonly TimeSpan RateLimitedInterval = TimeSpan.FromMinutes(5);
    public static readonly TimeSpan MaxRateLimitedInterval = TimeSpan.FromMinutes(10);
    /// <summary>How long the last numbers are kept when polls fail for a passing reason.</summary>
    public static readonly TimeSpan StaleAfter = TimeSpan.FromMinutes(5);
    public static readonly TimeSpan HttpTimeout = TimeSpan.FromSeconds(15);

    public const string KeyExpiredText = "Session key expired: paste a new one from claude.ai";
    public const string NotConfiguredText = "Fill in the org ID and session key from claude.ai, then Save";
    public const string BadOrgIdText = "The org ID must look like 01234567-89ab-cdef-0123-456789abcdef";

    private readonly HttpClient _http;
    private readonly PdcStore _store;
    private readonly TimeProvider _clock;
    private int _epoch; // bumped by on/off and saves: answers to older requests are dropped
    private bool _polling;
    private bool _halted; // the key was refused: no polls until it is saved again or the switch is toggled
    private int _rateLimits; // 429s in a row
    private DateTimeOffset _nextPoll = DateTimeOffset.MinValue;
    private UsageSnapshot? _last;
    private DateTimeOffset _lastAt;

    public UsageIntegration(HttpMessageHandler handler, PdcStore store, TimeProvider clock)
    {
        _http = new HttpClient(handler) { Timeout = HttpTimeout };
        _store = store;
        _clock = clock;
    }

    /// <summary>The real thing: the live endpoint through curl.exe (see <see cref="CurlHandler"/>), settings in %APPDATA%\DeskBuddy.</summary>
    public static UsageIntegration CreateDefault() =>
        new(new CurlHandler(), new PdcStore(PdcStore.DefaultDirectory), TimeProvider.System);

    public bool Enabled { get; private set; }
    /// <summary>What is going on, for the window; null while off.</summary>
    public string? Status { get; private set; }
    /// <summary>True while <see cref="Status"/> is a problem.</summary>
    public bool Problem { get; private set; }
    /// <summary>The saved org ID; null: none.</summary>
    public string? OrgId => _store.LoadUsageOrgId();
    /// <summary>Whether a session key is saved (the key itself is never handed out).</summary>
    public bool HasSessionKey => _store.LoadClaudeSessionKey() is not null;
    /// <summary>When the next poll is due (while on).</summary>
    public DateTimeOffset NextPoll => _nextPoll;

    /// <summary>The numbers to show: on, fresh (at most <see cref="StaleAfter"/> old) and with at least one window;
    /// otherwise null. This is also what the device is told.</summary>
    public UsageSnapshot? Usage =>
        Enabled && _last is { IsEmpty: false } last && Now - _lastAt <= StaleAfter ? last : null;

    public event Action? Changed;
    public event Action<LogKind, string>? Log;

    private DateTimeOffset Now => _clock.GetUtcNow();

    public static bool IsValidOrgId(string? orgId) => Guid.TryParseExact(orgId?.Trim(), "D", out _);

    /// <summary>Saves the org ID and, when <paramref name="sessionKey"/> is not empty, the session key (empty keeps the
    /// stored one); when on, polls at once. Returns the problem (nothing saved) or null.</summary>
    public async Task<string?> SaveConfigAsync(string orgId, string? sessionKey)
    {
        if (!IsValidOrgId(orgId))
        {
            return BadOrgIdText;
        }
        _store.SaveUsageOrgId(orgId.Trim().ToLowerInvariant());
        if (sessionKey?.Trim() is { Length: > 0 } key)
        {
            _store.SaveClaudeSessionKey(key);
        }
        Write(LogKind.Info, "Claude usage: org ID and session key saved");
        if (Enabled)
        {
            await ActivateAsync();
        }
        else
        {
            Changed?.Invoke();
        }
        return null;
    }

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
        _halted = false;
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
        _halted = false;
        _rateLimits = 0;
        _last = null;
        Status = "checking…";
        Problem = false;
        _nextPoll = DateTimeOffset.MinValue;
        Changed?.Invoke();
        return PollAsync();
    }

    /// <summary>Called every second: polls every 60 s (longer after a 429). Does nothing while off or after the key was refused.</summary>
    public Task TickAsync() => Enabled && !_halted && !_polling && Now >= _nextPoll ? PollAsync() : Task.CompletedTask;

    /// <summary>One poll of the endpoint with the saved org ID and key.</summary>
    public async Task PollAsync()
    {
        if (!Enabled || _halted || _polling)
        {
            return;
        }
        _polling = true;
        var epoch = _epoch;
        _nextPoll = Now + PollInterval;
        try
        {
            var orgId = _store.LoadUsageOrgId();
            var key = _store.LoadClaudeSessionKey();
            if (!IsValidOrgId(orgId) || key is null)
            {
                _last = null;
                Status = NotConfiguredText;
                Problem = true;
                return;
            }
            using var request = new HttpRequestMessage(HttpMethod.Get, new Uri(BaseUri, $"api/organizations/{Uri.EscapeDataString(orgId!)}/usage"));
            request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("*/*"));
            request.Headers.TryAddWithoutValidation("cookie", $"sessionKey={key}");
            request.Headers.Add("anthropic-client-platform", "web_claude_ai");
            request.Headers.UserAgent.Add(new ProductInfoHeaderValue("DeskBuddy", Version));
            request.Content = new ByteArrayContent([]);
            request.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            TimeSpan? retryAfter = null;
            var challenged = false;
            int status;
            string body;
            try
            {
                using var response = await _http.SendAsync(request);
                status = (int)response.StatusCode;
                retryAfter = response.Headers.RetryAfter is { } ra
                    ? ra.Delta ?? (ra.Date is { } d ? d - Now : null)
                    : null;
                challenged = response.Headers.TryGetValues("cf-mitigated", out var mitigated) && mitigated.Contains("challenge");
                body = await response.Content.ReadAsStringAsync();
            }
            catch (Exception e) when (e is HttpRequestException or TaskCanceledException or IOException)
            {
                if (epoch == _epoch)
                {
                    Fail(e is TaskCanceledException ? "no answer from claude.ai in time" : "claude.ai not reachable", drop: false);
                }
                return;
            }
            if (epoch != _epoch)
            {
                return;
            }
            if (challenged)
            {
                Fail("claude.ai's Cloudflare check refused the request", drop: false); // not the key: no halt
                return;
            }
            if (status is (int)HttpStatusCode.Unauthorized or (int)HttpStatusCode.Forbidden)
            {
                _halted = true;
                Fail(KeyExpiredText, drop: true);
                return;
            }
            if (status == (int)HttpStatusCode.TooManyRequests)
            {
                _rateLimits++;
                var wait = retryAfter is { } r && r > TimeSpan.Zero
                    ? r
                    : _rateLimits == 1 ? RateLimitedInterval : MaxRateLimitedInterval;
                _nextPoll = Now + wait;
                Fail($"rate limited by claude.ai: trying again at {_nextPoll.ToLocalTime():HH:mm}", drop: false);
                return;
            }
            if (status is < 200 or >= 300 || UsageApi.Parse(body, Now) is not { } usage)
            {
                Fail(status is >= 200 and < 300 ? "unexpected answer from claude.ai" : $"claude.ai answered HTTP {status}", drop: false);
                return;
            }
            _rateLimits = 0;
            _last = usage;
            _lastAt = Now;
            Status = usage.IsEmpty ? "connected; no usage windows reported" : $"updated {Now.ToLocalTime():HH:mm}";
            Problem = false;
        }
        finally
        {
            _polling = false;
            Changed?.Invoke();
        }
    }

    // drop: the numbers are wrong now (key refused); otherwise they are kept until they are StaleAfter old.
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
