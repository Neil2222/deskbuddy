using System.Net;
using System.Text;
using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.Usage;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The Claude usage strip: the API answer, the prediction, the usage.set line, and the token handling against a fake
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
        _usage = new UsageIntegration(_server, CredentialsPath, new PdcStore(_dir), _clock);
    }

    private string CredentialsPath => Path.Combine(_dir, ".credentials.json");

    public void Dispose()
    {
        _usage.Dispose();
        Directory.Delete(_dir, recursive: true);
    }

    private void Credentials(DateTimeOffset expires) => File.WriteAllText(CredentialsPath,
        $$$"""{"claudeAiOauth":{"accessToken":"tok-123","refreshToken":"r","expiresAt":{{{expires.ToUnixTimeMilliseconds()}}}}}""");

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

    // --- the token --------------------------------------------------------------------------------------------------------

    [Fact]
    public async Task A_poll_sends_the_token_and_yields_the_numbers()
    {
        Credentials(Now.AddHours(1));
        await _usage.SetEnabledAsync(true);
        var request = Assert.Single(_server.Requests);
        Assert.Equal("Bearer tok-123", request.Authorization);
        Assert.Equal("oauth-2025-04-20", request.Beta);
        Assert.StartsWith("DeskBuddy/", request.UserAgent);
        Assert.Equal(20, _usage.Usage?.Five?.Pct);
        Assert.False(_usage.Problem);
    }

    [Fact]
    public async Task An_expired_token_is_not_sent_and_turns_the_strip_off()
    {
        Credentials(Now.AddHours(1));
        await _usage.SetEnabledAsync(true);
        Assert.NotNull(_usage.Usage);

        Credentials(Now.AddSeconds(-1));
        _clock.Now = Now.AddMinutes(1);
        await _usage.TickAsync();
        Assert.Single(_server.Requests); // not sent again
        Assert.Null(_usage.Usage);
        Assert.Equal(UsageIntegration.LoginExpiredText, _usage.Status);
        Assert.True(_usage.Problem);
        Assert.DoesNotContain("tok-123", File.ReadAllText(Path.Combine(_dir, "settings.json")));
    }

    [Fact]
    public async Task A_401_counts_as_an_expired_login()
    {
        Credentials(Now.AddHours(1));
        _server.Status = HttpStatusCode.Unauthorized;
        await _usage.SetEnabledAsync(true);
        Assert.Null(_usage.Usage);
        Assert.Equal(UsageIntegration.LoginExpiredText, _usage.Status);
    }

    [Fact]
    public async Task Without_a_credentials_file_it_says_claude_code_is_not_logged_in()
    {
        await _usage.SetEnabledAsync(true);
        Assert.Empty(_server.Requests);
        Assert.Equal(UsageIntegration.NotLoggedInText, _usage.Status);
        Assert.Null(_usage.Usage);
    }
}

/// <summary>The usage endpoint in memory: answers <see cref="Status"/> with a fixed body.</summary>
internal sealed class FakeUsageServer : HttpMessageHandler
{
    public sealed record Request(string? Authorization, string? Beta, string UserAgent);

    public List<Request> Requests { get; } = [];
    public HttpStatusCode Status { get; set; } = HttpStatusCode.OK;

    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        Requests.Add(new Request(request.Headers.Authorization?.ToString(),
                                 request.Headers.TryGetValues("anthropic-beta", out var beta) ? beta.Single() : null,
                                 request.Headers.UserAgent.ToString()));
        const string body = """{"five_hour":{"utilization":20.0,"resets_at":"2026-10-18T21:50:00+00:00"},"seven_day":null}""";
        return Task.FromResult(new HttpResponseMessage(Status) { Content = new StringContent(body, Encoding.UTF8, "application/json") });
    }
}
