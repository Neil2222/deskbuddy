using System.Net;
using System.Text;
using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.Usage;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The Claude usage strip: the API answer, the prediction, the usage.set line, and the org ID / session key handling against a fake
// handler (never the real endpoint).
public sealed class UsageTests : IDisposable
{
    private static readonly DateTimeOffset Now = new(2026, 10, 18, 19, 0, 0, TimeSpan.Zero);

    private readonly string _dir = Path.Combine(Path.GetTempPath(), "deskbuddy-usage-tests-" + Guid.NewGuid().ToString("N"));
    private readonly FakeClock _clock = new() { Now = Now };
    private readonly FakeUsageServer _server = new();
    private readonly UsageIntegration _usage;

    public UsageTests()
    {
        Directory.CreateDirectory(_dir);
        _usage = new UsageIntegration(_server, new PdcStore(_dir), _clock);
    }

    private const string OrgId = "0a1b2c3d-4e5f-6789-abcd-ef0123456789";

    public void Dispose()
    {
        _usage.Dispose();
        Directory.Delete(_dir, recursive: true);
    }

    // --- the API answer --------------------------------------------------------------------------------------------

    [Fact]
    public void Parse_reads_both_windows_and_ignores_other_keys()
    {
        var usage = UsageApi.Parse("""
            {"five_hour":{"utilization":20.0,"resets_at":"2026-10-18T21:50:00.143198+00:00"},
             "seven_day":{"utilization":39.4,"resets_at":"2026-10-20T05:00:00.143216+00:00"},
             "seven_day_opus":null,"extra_usage":{"is_enabled":false}}
            """, Now);
        Assert.NotNull(usage);
        Assert.Equal(20, usage.Five!.Pct);
        Assert.Equal(new DateTimeOffset(2026, 10, 18, 21, 50, 0, 143, TimeSpan.Zero).AddTicks(1980), usage.Five.ResetsAt);
        Assert.Equal(39, usage.Week!.Pct);
        Assert.Equal(new DateTimeOffset(2026, 10, 20, 5, 0, 0, TimeSpan.Zero).ToUnixTimeSeconds(), usage.Week.ResetsAt.ToUnixTimeSeconds());
    }

    [Theory]
    [InlineData("""{"five_hour":null,"seven_day":{"utilization":10,"resets_at":"2026-10-20T05:00:00+00:00"}}""", false, true)]
    [InlineData("""{"seven_day":{"utilization":10,"resets_at":"2026-10-20T05:00:00+00:00"}}""", false, true)]
    [InlineData("""{"five_hour":{"utilization":10,"resets_at":"2026-10-18T21:00:00+00:00"},"seven_day":{"utilization":null,"resets_at":"2026-10-20T05:00:00+00:00"}}""", true, false)]
    [InlineData("""{"five_hour":{"utilization":10},"seven_day":{"resets_at":"2026-10-20T05:00:00+00:00"}}""", false, false)]
    [InlineData("""{}""", false, false)]
    public void Parse_leaves_out_null_or_incomplete_windows(string body, bool five, bool week)
    {
        var usage = UsageApi.Parse(body, Now)!;
        Assert.Equal(five, usage.Five is not null);
        Assert.Equal(week, usage.Week is not null);
    }

    [Theory]
    [InlineData("not json")]
    [InlineData("[1,2]")]
    public void Parse_rejects_what_is_not_an_object(string body) => Assert.Null(UsageApi.Parse(body, Now));

    // --- the prediction ---------------------------------------------------------------------------------------------

    [Fact]
    public void Prediction_extrapolates_to_the_end_of_the_window()
    {
        // 2 h of 5 h passed at 40 %: 100 % at the reset.
        Assert.Equal(100, UsageApi.Predict(40, Now.AddHours(3), UsageApi.FiveHours, Now));
        // 1 day of 7 passed at 20 %: 140 %.
        Assert.Equal(140, UsageApi.Predict(20, Now.AddDays(6), UsageApi.SevenDays, Now));
    }

    [Fact]
    public void No_prediction_in_the_first_5_minutes_at_0_percent_or_after_the_reset()
    {
        Assert.Null(UsageApi.Predict(10, Now.AddMinutes(296), UsageApi.FiveHours, Now)); // 4 min elapsed
        Assert.Equal(300, UsageApi.Predict(5, Now.AddMinutes(295), UsageApi.FiveHours, Now)); // exactly 5: predicted
        Assert.Null(UsageApi.Predict(0, Now.AddHours(1), UsageApi.FiveHours, Now));
        Assert.Null(UsageApi.Predict(50, Now, UsageApi.FiveHours, Now));
        Assert.Null(UsageApi.Predict(50, Now.AddMinutes(-1), UsageApi.FiveHours, Now));
    }

    [Fact]
    public void Prediction_and_percentage_are_clamped_to_999()
    {
        Assert.Equal(999, UsageApi.Predict(90, Now.AddMinutes(290), UsageApi.FiveHours, Now)); // 90 · 300 / 10 = 2700
        var usage = UsageApi.Parse("""{"five_hour":{"utilization":1234.5,"resets_at":"2026-10-18T20:00:00+00:00"}}""", Now)!;
        Assert.Equal(999, usage.Five!.Pct);
        Assert.Equal(999, usage.Five.Pred);
    }

    // --- the wire ------------------------------------------------------------------------------------------------------

    [Fact]
    public void Set_lists_each_window_present_with_its_prediction()
    {
        var five = new UsageWindow(37, DateTimeOffset.FromUnixTimeSeconds(1792360200), 130);
        var week = new UsageWindow(20, DateTimeOffset.FromUnixTimeSeconds(1792472400), null);
        Assert.Equal("usage.set state=on five=37 five_end=1792360200 five_pred=130 week=20 week_end=1792472400",
                     UsageWire.Set(new UsageSnapshot(five, week)));
        Assert.Equal("usage.set state=on week=20 week_end=1792472400", UsageWire.Set(new UsageSnapshot(null, week)));
        Assert.Equal("usage.set state=on five=37 five_end=1792360200 five_pred=130", UsageWire.Set(new UsageSnapshot(five, null)));
        Assert.Equal("usage.set state=off", UsageWire.Set(null));
    }

    [Theory]
    [InlineData(3 * 24 * 60 + 2 * 60 + 59, "3d 2u")]
    [InlineData(24 * 60, "1d 0u")]
    [InlineData(150, "2:30")]
    [InlineData(60, "1:00")]
    [InlineData(45, "45m")]
    [InlineData(0, "nu")]
    [InlineData(-5, "nu")]
    public void Countdown_formats_as_on_the_device(int minutes, string text) =>
        Assert.Equal(text, UsageText.Countdown(TimeSpan.FromMinutes(minutes)));

    [Fact]
    public void Window_text_matches_the_device()
    {
        Assert.Equal("5U 37% > 130% 2:30", UsageText.Window("5U", new UsageWindow(37, Now.AddMinutes(150), 130), Now));
        Assert.Equal("7D 20% 3d 2u", UsageText.Window("7D", new UsageWindow(20, Now.AddHours(74), null), Now));
    }

    // --- org ID and session key -----------------------------------------------------------------------------------------

    [Fact]
    public async Task Without_org_id_or_key_it_asks_for_them_and_sends_nothing()
    {
        await _usage.SetEnabledAsync(true);
        Assert.Empty(_server.Requests);
        Assert.Equal(UsageIntegration.NotConfiguredText, _usage.Status);
        Assert.Null(_usage.Usage);

        Assert.Null(await _usage.SaveConfigAsync(OrgId, "")); // org ID only
        Assert.Empty(_server.Requests);
        Assert.Equal(UsageIntegration.NotConfiguredText, _usage.Status);
    }

    [Fact]
    public async Task A_poll_sends_the_org_id_and_key_and_yields_the_numbers()
    {
        Assert.Null(await _usage.SaveConfigAsync(OrgId, "sk-ant-sid-123"));
        Assert.Empty(_server.Requests); // off: nothing sent
        await _usage.SetEnabledAsync(true);
        var request = Assert.Single(_server.Requests);
        Assert.Contains($"/api/organizations/{OrgId}/usage", request.Url);
        Assert.StartsWith("https://claude.ai/", request.Url);
        Assert.Equal("sessionKey=sk-ant-sid-123", request.Cookie);
        Assert.StartsWith("DeskBuddy/", request.UserAgent);
        Assert.Equal(20, _usage.Usage?.Five?.Pct);
        Assert.False(_usage.Problem);
        Assert.DoesNotContain("sk-ant-sid-123", File.ReadAllText(Path.Combine(_dir, "settings.json")));
    }

    [Fact]
    public async Task A_401_drops_the_numbers_and_stops_polling_until_a_new_key_is_saved()
    {
        await _usage.SaveConfigAsync(OrgId, "old");
        await _usage.SetEnabledAsync(true);
        Assert.NotNull(_usage.Usage);

        _server.Status = HttpStatusCode.Unauthorized;
        _clock.Now = Now.AddMinutes(1);
        await _usage.TickAsync();
        Assert.Equal(2, _server.Requests.Count);
        Assert.Null(_usage.Usage);
        Assert.Equal(UsageIntegration.KeyExpiredText, _usage.Status);
        Assert.True(_usage.Problem);

        _clock.Now = Now.AddMinutes(10);
        await _usage.TickAsync();
        Assert.Equal(2, _server.Requests.Count); // halted

        _server.Status = HttpStatusCode.OK;
        Assert.Null(await _usage.SaveConfigAsync(OrgId, "new"));
        Assert.Equal(3, _server.Requests.Count);
        Assert.Equal("sessionKey=new", _server.Requests[^1].Cookie);
        Assert.NotNull(_usage.Usage);
    }

    [Fact]
    public async Task A_403_also_counts_as_an_expired_key_and_toggling_retries()
    {
        await _usage.SaveConfigAsync(OrgId, "k");
        _server.Status = HttpStatusCode.Forbidden;
        await _usage.SetEnabledAsync(true);
        Assert.Equal(UsageIntegration.KeyExpiredText, _usage.Status);
        await _usage.SetEnabledAsync(false);
        await _usage.SetEnabledAsync(true);
        Assert.Equal(2, _server.Requests.Count);
    }

    [Fact]
    public async Task A_Cloudflare_challenge_is_not_an_expired_key()
    {
        await _usage.SaveConfigAsync(OrgId, "k");
        _server.Status = HttpStatusCode.Forbidden;
        _server.Challenge = true;
        await _usage.SetEnabledAsync(true);
        Assert.NotEqual(UsageIntegration.KeyExpiredText, _usage.Status);
        Assert.True(_usage.Problem);
        _clock.Now = Now.AddMinutes(1);
        await _usage.TickAsync();
        Assert.Equal(2, _server.Requests.Count); // still polling
    }

    [Fact]
    public async Task A_429_honours_Retry_After()
    {
        await _usage.SaveConfigAsync(OrgId, "k");
        _server.Status = HttpStatusCode.TooManyRequests;
        _server.RetryAfter = TimeSpan.FromSeconds(90);
        await _usage.SetEnabledAsync(true);
        Assert.Equal(Now.AddSeconds(90), _usage.NextPoll);
        Assert.True(_usage.Problem);
    }

    [Fact]
    public async Task A_429_without_Retry_After_backs_off_5_then_10_minutes_and_resets_after_a_success()
    {
        await _usage.SaveConfigAsync(OrgId, "k");
        await _usage.SetEnabledAsync(true);
        Assert.NotNull(_usage.Usage);

        _server.Status = HttpStatusCode.TooManyRequests;
        _clock.Now = Now.AddMinutes(1);
        await _usage.TickAsync();
        Assert.Equal(_clock.Now.AddMinutes(5), _usage.NextPoll);
        Assert.NotNull(_usage.Usage); // kept while fresh

        _clock.Now = _usage.NextPoll;
        await _usage.TickAsync();
        Assert.Equal(_clock.Now.AddMinutes(10), _usage.NextPoll);
        Assert.Null(_usage.Usage); // older than StaleAfter now

        _clock.Now = _usage.NextPoll;
        await _usage.TickAsync();
        Assert.Equal(_clock.Now.AddMinutes(10), _usage.NextPoll); // capped

        _server.Status = HttpStatusCode.OK;
        _clock.Now = _usage.NextPoll;
        await _usage.TickAsync();
        Assert.Equal(_clock.Now + UsageIntegration.PollInterval, _usage.NextPoll);

        _server.Status = HttpStatusCode.TooManyRequests;
        _clock.Now = _usage.NextPoll;
        await _usage.TickAsync();
        Assert.Equal(_clock.Now.AddMinutes(5), _usage.NextPoll); // back to the first step
    }

    [Theory]
    [InlineData(OrgId, true)]
    [InlineData(" 0A1B2C3D-4E5F-6789-ABCD-EF0123456789 ", true)]
    [InlineData("", false)]
    [InlineData("not-a-guid", false)]
    [InlineData("0a1b2c3d4e5f6789abcdef0123456789", false)]
    public void Org_id_must_be_a_guid(string orgId, bool valid) => Assert.Equal(valid, UsageIntegration.IsValidOrgId(orgId));

    [Fact]
    public async Task An_invalid_org_id_is_not_saved()
    {
        Assert.Equal(UsageIntegration.BadOrgIdText, await _usage.SaveConfigAsync("nope", "k"));
        Assert.Null(_usage.OrgId);
        Assert.False(_usage.HasSessionKey);
    }

    [Fact]
    public async Task The_key_is_stored_encrypted_and_an_empty_field_keeps_it()
    {
        var store = new PdcStore(_dir);
        await _usage.SaveConfigAsync(OrgId, "secret-key");
        Assert.Equal("secret-key", store.LoadClaudeSessionKey());
        Assert.DoesNotContain("secret-key", Encoding.UTF8.GetString(File.ReadAllBytes(store.ClaudeSessionPath)));

        await _usage.SaveConfigAsync(OrgId, "");
        Assert.Equal("secret-key", store.LoadClaudeSessionKey());
        Assert.Equal(OrgId, store.LoadUsageOrgId());
        Assert.True(_usage.HasSessionKey);
    }
}

/// <summary>The usage endpoint in memory: answers <see cref="Status"/> with a fixed body.</summary>
internal sealed class FakeUsageServer : HttpMessageHandler
{
    public sealed record Request(string Url, string? Cookie, string UserAgent);

    public List<Request> Requests { get; } = [];
    public HttpStatusCode Status { get; set; } = HttpStatusCode.OK;
    public TimeSpan? RetryAfter { get; set; }
    public bool Challenge { get; set; }

    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        Requests.Add(new Request(request.RequestUri!.ToString(),
                                 request.Headers.TryGetValues("cookie", out var cookie) ? string.Join("; ", cookie) : null,
                                 request.Headers.UserAgent.ToString()));
        const string body = """{"five_hour":{"utilization":20.0,"resets_at":"2026-10-18T21:50:00+00:00"},"seven_day":null}""";
        var response = new HttpResponseMessage(Status) { Content = new StringContent(body, Encoding.UTF8, "application/json") };
        if (Challenge)
        {
            response.Headers.Add("cf-mitigated", "challenge");
        }
        if (RetryAfter is { } wait)
        {
            response.Headers.RetryAfter = new System.Net.Http.Headers.RetryConditionHeaderValue(wait);
        }
        return Task.FromResult(response);
    }
}
