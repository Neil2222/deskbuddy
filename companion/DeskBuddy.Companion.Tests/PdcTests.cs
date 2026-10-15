using System.Net;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.Protocol;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The PDC Manager (ZDMW) integration against a fake server: never the real one. Every test checks that no request
// went to any host but the fake one.
public sealed class PdcTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "deskbuddy-pdc-tests-" + Guid.NewGuid().ToString("N"));
    private readonly FakeClock _clock = new();
    private readonly FakePdcServer _server = new();
    private readonly PdcStore _store;
    private readonly PdcIntegration _pdc;

    public PdcTests()
    {
        _store = new PdcStore(_dir);
        _pdc = new PdcIntegration(FakePdcServer.BaseUri, _server, _store, _clock, TimeSpan.FromMilliseconds(300));
    }

    public void Dispose()
    {
        _pdc.Dispose();
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
        Assert.All(_server.Hosts, h => Assert.Equal(FakePdcServer.Host, h));
    }

    private async Task LoggedInAsync()
    {
        _store.SaveSettings(true, "jdoe");
        _store.SaveSession(FakePdcServer.BaseUri, new Dictionary<string, string> { ["session"] = "abc" });
        await _pdc.StartAsync();
        Assert.Equal(PdcAuth.LoggedIn, _pdc.Auth);
        Assert.True(_pdc.CanClick);
    }

    // --- login and session ---------------------------------------------------------------------------------------

    [Fact]
    public async Task Login_success_stores_the_session_encrypted_and_reads_the_profile()
    {
        _store.SaveSettings(true, null);
        await _pdc.StartAsync();
        Assert.Equal(PdcAuth.LoginRequired, _pdc.Auth);
        Assert.Equal(["GET /health"], _server.Paths); // no session: only reachability

        Assert.True(await _pdc.LoginAsync(" jdoe ", "s3cret!"));
        Assert.Equal(PdcAuth.LoggedIn, _pdc.Auth);
        Assert.Equal("JDoe", _pdc.Username); // as the server spells it
        Assert.Equal(PdcAccess.Write, _pdc.Access);
        Assert.Equal(41, _pdc.Count);
        Assert.True(_pdc.CanClick);
        Assert.Null(_pdc.LoginProblem);

        var login = _server.Requests.Single(r => r.Path == "/auth/login");
        Assert.Equal("POST", login.Method);
        Assert.Equal("application/x-www-form-urlencoded", login.ContentType);
        Assert.Equal("username=jdoe&password=s3cret%21", login.Body);
        Assert.Null(login.Cookie);
        Assert.Equal("session=abc; theme=dark", _server.Requests.Single(r => r.Path == "/auth/api/me/profile").Cookie);
        Assert.All(_server.Requests, r => Assert.Equal("application/json", r.Accept));

        // Kept: the cookies (DPAPI, not readable as text) and the user name; never the password.
        Assert.Equal("abc", _store.LoadSession(FakePdcServer.BaseUri)?["session"]);
        Assert.DoesNotContain("session", Encoding.Latin1.GetString(File.ReadAllBytes(_store.SessionPath)));
        var settings = File.ReadAllText(_store.SettingsPath);
        Assert.Contains("\"pdcUsername\": \"jdoe\"", settings);
        Assert.DoesNotContain("s3cret", settings);
    }

    [Fact]
    public async Task Login_with_a_wrong_password_fails_and_keeps_nothing()
    {
        _store.SaveSettings(true, null);
        await _pdc.StartAsync();
        Assert.False(await _pdc.LoginAsync("jdoe", "wrong"));
        Assert.Equal(PdcAuth.LoginRequired, _pdc.Auth);
        Assert.Equal("Wrong username or password", _pdc.LoginProblem);
        Assert.False(File.Exists(_store.SessionPath));
        Assert.False(_pdc.CanClick);
        Assert.Equal(0, _server.Count("GET", "/auth/api/me/profile"));
    }

    [Fact]
    public async Task Login_to_an_account_without_a_password_says_to_set_one_first()
    {
        _server.Route("POST /auth/login", (_, _) => FakePdcServer.Html(200, "<form action=\"/auth/set_password\">"));
        _store.SaveSettings(true, null);
        await _pdc.StartAsync();
        Assert.False(await _pdc.LoginAsync("jdoe", "anything"));
        Assert.Contains("no password yet", _pdc.LoginProblem);
        Assert.False(File.Exists(_store.SessionPath));
    }

    [Fact]
    public void Session_round_trip_is_bound_to_its_server()
    {
        var cookies = new Dictionary<string, string> { ["session"] = "eyJhbGciOi.x-y_z", ["remember"] = "1" };
        _store.SaveSession(FakePdcServer.BaseUri, cookies);
        Assert.Equal(cookies, _store.LoadSession(FakePdcServer.BaseUri));
        Assert.Equal(cookies, new PdcStore(_dir).LoadSession(new Uri("https://PDC.test/")));

        // A session of another server (the local fake server, say) is never used for this one: it is deleted.
        Assert.Null(_store.LoadSession(new Uri("https://pdcmanager")));
        Assert.False(File.Exists(_store.SessionPath));

        File.WriteAllBytes(_store.SessionPath, [1, 2, 3]); // damaged
        Assert.Null(_store.LoadSession(FakePdcServer.BaseUri));
        Assert.False(File.Exists(_store.SessionPath));
    }

    [Fact]
    public void Settings_default_to_off_and_keep_other_keys()
    {
        Assert.Equal((false, (string?)null), _store.LoadSettings());
        Directory.CreateDirectory(_dir);
        File.WriteAllText(_store.SettingsPath, """{"other":42}""");
        _store.SaveSettings(true, "jdoe");
        Assert.Equal((true, "jdoe"), _store.LoadSettings());
        Assert.Contains("\"other\": 42", File.ReadAllText(_store.SettingsPath));
        File.WriteAllText(_store.SettingsPath, "{broken");
        Assert.Equal((false, (string?)null), _store.LoadSettings());
    }

    [Theory]
    [InlineData(401, null)]
    [InlineData(302, "/auth/login?next=/auth/api/me/profile")]
    public async Task An_expired_session_means_login_required_and_is_deleted(int status, string? location)
    {
        _server.Route("GET /auth/api/me/profile", (_, _) => FakePdcServer.Json(status, """{"success":false,"error":"Niet ingelogd"}""", location));
        _store.SaveSettings(true, "jdoe");
        _store.SaveSession(FakePdcServer.BaseUri, new Dictionary<string, string> { ["session"] = "old" });
        await _pdc.StartAsync();
        Assert.Equal(PdcAuth.LoginRequired, _pdc.Auth);
        Assert.Equal(PdcReach.Reachable, _pdc.Reach);
        Assert.Contains("expired", _pdc.LoginProblem);
        Assert.False(File.Exists(_store.SessionPath));
        Assert.False(_pdc.CanClick);
        Assert.Equal("pdc.set state=off", PdcWire.Set(_pdc.CanClick, _pdc.Count));
        Assert.Equal(PdcOutcome.Unavailable, (await _pdc.ClickAsync()).Outcome);
        Assert.Equal(0, _server.Count("POST", "/zdmw/api/click"));
    }

    [Fact]
    public async Task A_session_that_expires_later_is_noticed_by_the_count_refresh()
    {
        await LoggedInAsync();
        _server.Route("GET /zdmw/api/count", (_, _) => FakePdcServer.Json(401, """{"success":false}"""));
        _clock.Advance(PdcIntegration.CheckInterval);
        await _pdc.TickAsync();
        Assert.Equal(PdcAuth.LoginRequired, _pdc.Auth);
        Assert.False(File.Exists(_store.SessionPath));
    }

    [Fact]
    public async Task Renewed_cookies_are_stored_again()
    {
        await LoggedInAsync();
        _server.Route("GET /zdmw/api/count", (_, _) =>
            FakePdcServer.Json(200, """{"success":true,"count":43}""", cookie: "session=renewed; Path=/; HttpOnly"));
        _clock.Advance(PdcIntegration.CheckInterval);
        await _pdc.TickAsync();
        Assert.Equal(43, _pdc.Count);
        Assert.Equal("renewed", _store.LoadSession(FakePdcServer.BaseUri)?["session"]);
    }

    [Fact]
    public async Task Log_out_deletes_the_session_without_a_request()
    {
        await LoggedInAsync();
        var before = _server.Requests.Count;
        _pdc.Logout();
        Assert.Equal(PdcAuth.LoginRequired, _pdc.Auth);
        Assert.False(_pdc.CanClick);
        Assert.False(File.Exists(_store.SessionPath));
        Assert.Equal(before, _server.Requests.Count);
    }

    // --- permission and count ---------------------------------------------------------------------------------------

    [Theory]
    [InlineData("admin", null, PdcAccess.Write)]
    [InlineData("user", "admin", PdcAccess.Write)]
    [InlineData("user", "write", PdcAccess.Write)]
    [InlineData("user", "read", PdcAccess.Read)]
    [InlineData("user", null, PdcAccess.None)]
    [InlineData(null, "bogus", PdcAccess.None)]
    public void Permission_follows_role_and_blueprint_access(string? role, string? zdmw, PdcAccess expected) =>
        Assert.Equal(expected, PdcIntegration.AccessFrom(role, zdmw));

    [Fact]
    public async Task Read_access_shows_the_count_but_cannot_click()
    {
        _server.Route("GET /auth/api/me/profile", (_, _) => FakePdcServer.Json(200,
            """{"success":true,"profile":{"username":"JDoe","role":"user","blueprint_access":{"zdmw":"read","todo":"write"}}}"""));
        _store.SaveSettings(true, "jdoe");
        _store.SaveSession(FakePdcServer.BaseUri, new Dictionary<string, string> { ["session"] = "abc" });
        await _pdc.StartAsync();
        Assert.Equal(PdcAccess.Read, _pdc.Access);
        Assert.Equal(41, _pdc.Count);
        Assert.False(_pdc.CanClick);
        Assert.Equal(PdcOutcome.Unavailable, (await _pdc.DeviceClickAsync(5))?.Outcome);
        Assert.Equal(0, _server.Count("POST", "/zdmw/api/click"));
    }

    [Fact]
    public async Task No_access_fetches_no_count()
    {
        _server.Route("GET /auth/api/me/profile", (_, _) => FakePdcServer.Json(200,
            """{"success":true,"profile":{"username":"JDoe","role":"user","blueprint_access":{}}}"""));
        _store.SaveSettings(true, "jdoe");
        _store.SaveSession(FakePdcServer.BaseUri, new Dictionary<string, string> { ["session"] = "abc" });
        await _pdc.StartAsync();
        Assert.Equal(PdcAccess.None, _pdc.Access);
        Assert.Null(_pdc.Count);
        Assert.Equal(0, _server.Count("GET", "/zdmw/api/count"));
    }

    [Fact]
    public async Task The_count_is_refreshed_every_minute()
    {
        await LoggedInAsync();
        Assert.Equal(["GET /health", "GET /auth/api/me/profile", "GET /zdmw/api/count"], _server.Paths);
        _clock.Advance(TimeSpan.FromSeconds(59));
        await _pdc.TickAsync();
        Assert.Equal(1, _server.Count("GET", "/zdmw/api/count"));
        _clock.Advance(TimeSpan.FromSeconds(1));
        await _pdc.TickAsync();
        Assert.Equal(2, _server.Count("GET", "/zdmw/api/count"));
        Assert.Equal(1, _server.Count("GET", "/health")); // reachable and logged in: the count is enough
    }

    // --- clicks ------------------------------------------------------------------------------------------------------

    [Fact]
    public async Task A_click_sends_the_profile_username_and_reports_count_and_teams()
    {
        await LoggedInAsync();
        var result = await _pdc.ClickAsync();
        Assert.Equal(new PdcClickResult(PdcOutcome.Ok, 42, true), result);
        Assert.Equal(42, _pdc.Count);
        Assert.Same(result, _pdc.LastClick);
        var post = _server.Requests.Single(r => r.Method == "POST");
        Assert.Equal("/zdmw/api/click", post.Path);
        Assert.Equal("application/json", post.ContentType);
        Assert.Equal("""{"username":"JDoe"}""", post.Body);
        Assert.Equal("session=abc", post.Cookie);
        Assert.Equal("pdc.result id=7 result=ok count=42 teams=1", PdcWire.Result(7, result));
        Assert.Equal("Verstuurd: #42", PdcText.Describe(result));
        Assert.Equal(1, _server.Clicks);

        // The count is fetched again right after a click.
        await _pdc.TickAsync();
        Assert.Equal(2, _server.Count("GET", "/zdmw/api/count"));
    }

    [Fact]
    public async Task A_click_counted_without_a_Teams_message_says_so()
    {
        await LoggedInAsync();
        _server.Route("POST /zdmw/api/click", (_, _) =>
            FakePdcServer.Json(200, """{"success":true,"count":42,"webhook_sent":false,"webhook_error":"x","image_url":null}"""));
        var result = await _pdc.ClickAsync();
        Assert.Equal(new PdcClickResult(PdcOutcome.Ok, 42, false), result);
        Assert.Equal("pdc.result id=1 result=ok count=42 teams=0", PdcWire.Result(1, result));
        Assert.Contains("Teams-bericht lukte niet", PdcText.Describe(result));
    }

    [Fact]
    public async Task A_refused_click_is_denied_and_not_sent()
    {
        await LoggedInAsync();
        _server.Route("POST /zdmw/api/click", (_, _) => FakePdcServer.Json(403, """{"success":false,"error":"Onvoldoende rechten"}"""));
        var result = await _pdc.ClickAsync();
        Assert.Equal(PdcOutcome.Denied, result.Outcome);
        Assert.False(result.IsAmbiguous);
        Assert.Equal("pdc.result id=3 result=denied count=41", PdcWire.Result(3, result));
        Assert.StartsWith("Niet verstuurd", PdcText.Describe(result));
    }

    [Fact]
    public async Task A_click_answered_with_401_logs_out()
    {
        await LoggedInAsync();
        _server.Route("POST /zdmw/api/click", (_, _) => FakePdcServer.Json(401, """{"success":false,"error":"login"}"""));
        var result = await _pdc.ClickAsync();
        Assert.Equal(PdcOutcome.Auth, result.Outcome);
        Assert.Equal("pdc.result id=3 result=auth", PdcWire.Result(3, result));
        Assert.Equal(PdcAuth.LoginRequired, _pdc.Auth);
        Assert.False(File.Exists(_store.SessionPath));
        Assert.Equal("pdc.set state=off", PdcWire.Set(_pdc.CanClick, _pdc.Count));
    }

    [Fact]
    public async Task A_click_without_an_answer_is_a_timeout_and_never_retried()
    {
        await LoggedInAsync();
        _server.Route("POST /zdmw/api/click", async (_, ct) =>
        {
            _server.Clicks++; // the server got it, the answer never comes
            await Task.Delay(Timeout.Infinite, ct);
            return FakePdcServer.Json(200, "{}");
        });
        var result = await _pdc.ClickAsync();
        Assert.Equal(PdcOutcome.Timeout, result.Outcome);
        Assert.True(result.IsAmbiguous);
        Assert.Equal("Geen antwoord: misschien toch verstuurd", PdcText.Describe(result));
        Assert.Equal("pdc.result id=9 result=timeout count=41", PdcWire.Result(9, result));
        for (var i = 0; i < 5; i++)
        {
            _clock.Advance(TimeSpan.FromSeconds(30));
            await _pdc.TickAsync();
        }
        Assert.Equal(1, _server.Count("POST", "/zdmw/api/click"));
        Assert.Equal(PdcReach.Reachable, _pdc.Reach);
    }

    public static TheoryData<HttpRequestError, PdcOutcome> NetworkFailures => new()
    {
        { HttpRequestError.ConnectionError, PdcOutcome.Offline },
        { HttpRequestError.NameResolutionError, PdcOutcome.Offline },
        { HttpRequestError.SecureConnectionError, PdcOutcome.Tls },
        { HttpRequestError.ResponseEnded, PdcOutcome.Unknown },
        { HttpRequestError.InvalidResponse, PdcOutcome.Unknown },
    };

    [Theory]
    [MemberData(nameof(NetworkFailures))]
    public async Task Network_failures_tell_not_sent_from_maybe_sent(HttpRequestError error, PdcOutcome expected)
    {
        await LoggedInAsync();
        _server.Route("POST /zdmw/api/click", (_, _) => FakePdcServer.Fail(new HttpRequestException(
            error, "fake failure", error == HttpRequestError.SecureConnectionError ? new AuthenticationException("cert") : null)));
        var result = await _pdc.ClickAsync();
        Assert.Equal(expected, result.Outcome);
        Assert.Equal(expected == PdcOutcome.Unknown, result.IsAmbiguous);
        if (result.IsAmbiguous)
        {
            Assert.Equal("Onduidelijk antwoord: misschien toch verstuurd", PdcText.Describe(result));
        }
        else
        {
            Assert.StartsWith("Niet verstuurd", PdcText.Describe(result));
        }
        Assert.Equal(1, _server.Count("POST", "/zdmw/api/click"));
        Assert.Equal(0, _server.Clicks);
    }

    [Fact]
    public async Task Offline_turns_the_device_off_and_rechecks_only_health_every_minute()
    {
        await LoggedInAsync();
        _server.Route("POST /zdmw/api/click", (_, _) => FakePdcServer.Fail(new HttpRequestException(HttpRequestError.ConnectionError, "refused")));
        _server.Route("GET /health", (_, _) => FakePdcServer.Fail(new HttpRequestException(HttpRequestError.NameResolutionError, "no such host")));
        Assert.Equal(PdcOutcome.Offline, (await _pdc.ClickAsync()).Outcome);
        Assert.Equal(PdcReach.Unreachable, _pdc.Reach);
        Assert.False(_pdc.CanClick);
        Assert.Equal("pdc.set state=off", PdcWire.Set(_pdc.CanClick, _pdc.Count));
        Assert.Equal(0, _server.Clicks);

        var before = _server.Requests.Count;
        _clock.Advance(PdcIntegration.CheckInterval);
        await _pdc.TickAsync(); // the regular check: health only
        await _pdc.TickAsync();
        _clock.Advance(TimeSpan.FromSeconds(30));
        await _pdc.TickAsync();
        Assert.Equal(["GET /health"], _server.Paths.Skip(before));
        Assert.Equal(PdcAuth.LoggedIn, _pdc.Auth); // the session is kept for when the network is back

        _server.Route("GET /health", (_, _) => FakePdcServer.Json(200, """{"status":"ok"}"""));
        _clock.Advance(TimeSpan.FromSeconds(30));
        await _pdc.TickAsync();
        Assert.Equal(PdcReach.Reachable, _pdc.Reach);
        Assert.True(_pdc.CanClick);
    }

    // The same mapping with the real handler, on loopback only: refused (nothing sent), silent (maybe sent) and cut
    // off after the request (maybe sent).
    [Fact]
    public async Task The_real_handler_tells_refused_from_silent_and_cut_off()
    {
        using var pinned = PdcCertificate.LoadPinned();
        var probe = new System.Net.Sockets.TcpListener(IPAddress.Loopback, 0);
        probe.Start();
        var closedPort = ((IPEndPoint)probe.LocalEndpoint).Port;
        probe.Stop();
        using (var refused = new PdcClient(new Uri($"http://127.0.0.1:{closedPort}"), PdcCertificate.CreateHandler(pinned), TimeSpan.FromSeconds(5)))
        {
            Assert.Equal(PdcNetError.Offline, (await refused.PostJsonAsync("/zdmw/api/click", new { username = "x" })).Error);
        }

        var listener = new System.Net.Sockets.TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            var uri = new Uri($"http://127.0.0.1:{((IPEndPoint)listener.LocalEndpoint).Port}");
            using var client = new PdcClient(uri, PdcCertificate.CreateHandler(pinned), TimeSpan.FromMilliseconds(500));
            using var wait = new CancellationTokenSource(TimeSpan.FromSeconds(5));

            // Served, and the connection left open: the next request still gets a fresh connection (a reused one
            // could be resent by the handler on its own).
            var served = client.GetAsync("/health");
            using var kept = await listener.AcceptSocketAsync(wait.Token);
            await kept.ReceiveAsync(new byte[4096]);
            await kept.SendAsync(Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}"));
            Assert.Equal(200, (await served).Status);

            var silent = client.PostJsonAsync("/zdmw/api/click", new { username = "x" });
            using (var socket = await listener.AcceptSocketAsync(wait.Token))
            {
                Assert.Equal(PdcNetError.Timeout, (await silent).Error);
            }
            var cut = client.PostJsonAsync("/zdmw/api/click", new { username = "x" });
            using (var socket = await listener.AcceptSocketAsync(wait.Token))
            {
                var buffer = new byte[4096];
                await socket.ReceiveAsync(buffer); // the request arrives, then the connection drops
                socket.Close();
            }
            Assert.Equal(PdcNetError.Unknown, (await cut).Error);
        }
        finally
        {
            listener.Stop();
        }
    }

    [Fact]
    public async Task Unparsable_or_server_error_answers_are_unknown()
    {
        await LoggedInAsync();
        _server.Route("POST /zdmw/api/click", (_, _) => FakePdcServer.Html(502, "<html>Bad gateway</html>"));
        Assert.Equal(PdcOutcome.Unknown, (await _pdc.ClickAsync()).Outcome);
        _clock.Advance(PdcIntegration.Cooldown);
        _server.Route("POST /zdmw/api/click", (_, _) => FakePdcServer.Html(200, "<html>not json</html>"));
        Assert.Equal(PdcOutcome.Unknown, (await _pdc.ClickAsync()).Outcome);
    }

    [Fact]
    public async Task The_same_device_click_id_never_sends_twice()
    {
        await LoggedInAsync();
        var first = await _pdc.DeviceClickAsync(7);
        Assert.Equal(PdcOutcome.Ok, first?.Outcome);
        _clock.Advance(TimeSpan.FromMinutes(1)); // no cooldown in the way
        Assert.Same(first, await _pdc.DeviceClickAsync(7)); // the same result again, for pdc.result
        Assert.Equal(1, _server.Count("POST", "/zdmw/api/click"));

        // Still on its way: ignored.
        var gate = new TaskCompletionSource();
        _server.Route("POST /zdmw/api/click", async (_, _) =>
        {
            await gate.Task;
            return FakePdcServer.Json(200, """{"success":true,"count":50,"webhook_sent":true}""");
        });
        var pending = _pdc.DeviceClickAsync(8);
        Assert.Null(await _pdc.DeviceClickAsync(8));
        gate.SetResult();
        Assert.Equal(50, (await pending)?.Count);
        Assert.Equal(2, _server.Count("POST", "/zdmw/api/click"));

        // A restarted device starts its ids afresh.
        _pdc.ResetDeviceIds();
        _clock.Advance(TimeSpan.FromMinutes(1));
        Assert.Equal(PdcOutcome.Ok, (await _pdc.DeviceClickAsync(7))?.Outcome);
        Assert.Equal(3, _server.Count("POST", "/zdmw/api/click"));
    }

    [Fact]
    public async Task Clicks_from_the_window_and_the_device_share_a_5_second_cooldown()
    {
        await LoggedInAsync();
        Assert.Equal(PdcOutcome.Ok, (await _pdc.ClickAsync()).Outcome);
        Assert.Equal(_clock.Now + PdcIntegration.Cooldown, _pdc.CooldownUntil);
        _clock.Advance(TimeSpan.FromSeconds(4.9));
        var device = await _pdc.DeviceClickAsync(11);
        Assert.Equal(new PdcClickResult(PdcOutcome.Cooldown, 42), device);
        Assert.Equal("pdc.result id=11 result=cooldown count=42", PdcWire.Result(11, device!));
        Assert.Equal(PdcOutcome.Cooldown, (await _pdc.ClickAsync()).Outcome);
        Assert.Equal(1, _server.Count("POST", "/zdmw/api/click"));

        _clock.Advance(TimeSpan.FromSeconds(0.1));
        Assert.Null(_pdc.CooldownUntil);
        Assert.Equal(PdcOutcome.Ok, (await _pdc.DeviceClickAsync(12))?.Outcome);
        Assert.Equal(PdcOutcome.Cooldown, (await _pdc.ClickAsync()).Outcome);
        Assert.Equal(2, _server.Count("POST", "/zdmw/api/click"));
    }

    [Fact]
    public async Task Only_one_click_is_on_its_way_at_a_time()
    {
        await LoggedInAsync();
        var gate = new TaskCompletionSource();
        _server.Route("POST /zdmw/api/click", async (_, _) =>
        {
            await gate.Task;
            return FakePdcServer.Json(200, """{"success":true,"count":42,"webhook_sent":true}""");
        });
        var first = _pdc.ClickAsync();
        Assert.True(_pdc.Clicking);
        Assert.Equal(PdcOutcome.Busy, (await _pdc.ClickAsync()).Outcome);
        Assert.Equal(PdcOutcome.Busy, (await _pdc.DeviceClickAsync(4))?.Outcome);
        gate.SetResult();
        Assert.Equal(PdcOutcome.Ok, (await first).Outcome);
        Assert.False(_pdc.Clicking);
        Assert.Equal(1, _server.Count("POST", "/zdmw/api/click"));
    }

    // --- off means off --------------------------------------------------------------------------------------------

    [Fact]
    public async Task Disabled_makes_no_requests_at_all()
    {
        _store.SaveSession(FakePdcServer.BaseUri, new Dictionary<string, string> { ["session"] = "abc" });
        await _pdc.StartAsync(); // no settings file: off by default
        Assert.False(_pdc.Enabled);
        for (var i = 0; i < 10; i++)
        {
            _clock.Advance(TimeSpan.FromMinutes(5));
            await _pdc.TickAsync();
        }
        Assert.Equal(PdcOutcome.Unavailable, (await _pdc.ClickAsync()).Outcome);
        Assert.Equal(PdcOutcome.Unavailable, (await _pdc.DeviceClickAsync(1))?.Outcome);
        Assert.False(await _pdc.LoginAsync("jdoe", "s3cret!"));
        Assert.Empty(_server.Requests);
        Assert.Equal("pdc.set state=off", PdcWire.Set(_pdc.CanClick, _pdc.Count));

        // On, then off again: quiet from then on.
        await _pdc.SetEnabledAsync(true);
        Assert.True(_pdc.CanClick);
        await _pdc.SetEnabledAsync(false);
        Assert.False(_pdc.CanClick);
        var before = _server.Requests.Count;
        _clock.Advance(TimeSpan.FromMinutes(5));
        await _pdc.TickAsync();
        Assert.Equal(before, _server.Requests.Count);
        Assert.False(_store.LoadSettings().Enabled);
    }

    // --- the device protocol ------------------------------------------------------------------------------------

    [Fact]
    public void The_lease_is_sent_on_change_and_every_10_seconds()
    {
        var lease = new PdcLease();
        var t = new DateTimeOffset(2026, 10, 15, 9, 0, 0, TimeSpan.Zero);
        Assert.Equal("pdc.set state=off", lease.Next(t, false, null));
        Assert.Null(lease.Next(t.AddSeconds(9.9), false, 12)); // off: the count does not matter
        Assert.Equal("pdc.set state=off", lease.Next(t.AddSeconds(10), false, null));
        Assert.Equal("pdc.set state=on count=41", lease.Next(t.AddSeconds(11), true, 41));
        Assert.True(lease.IsOn);
        Assert.Null(lease.Next(t.AddSeconds(12), true, 41));
        Assert.Equal("pdc.set state=on count=42", lease.Next(t.AddSeconds(13), true, 42));
        Assert.Equal("pdc.set state=off", lease.Next(t.AddSeconds(14), false, 42));
        lease.Reset();
        Assert.Equal("pdc.set state=off", lease.Next(t.AddSeconds(15), false, 42));

        Assert.Equal("pdc.set state=on", PdcWire.Set(true, null));
        Assert.Equal("pdc.set state=on", PdcWire.Set(true, 3_000_000_000)); // out of range: left out
        Assert.StartsWith(">DB1 5 pdc.set state=on count=41 #", Frame.Command(5, PdcWire.Set(true, 41)));
    }

    [Fact]
    public void Results_use_the_protocol_names()
    {
        Assert.Equal(["ok", "auth", "denied", "offline", "tls", "timeout", "unknown", "cooldown", "busy", "unavailable"],
                     Enum.GetValues<PdcOutcome>().Select(PdcWire.Name));
        Assert.Equal("pdc.result id=65535 result=busy", PdcWire.Result(65535, new PdcClickResult(PdcOutcome.Busy)));
        Assert.Equal("pdc.result id=2 result=offline count=7", PdcWire.Result(2, new PdcClickResult(PdcOutcome.Offline, 7, true)));
    }

    [Theory]
    [InlineData("<DB1 0 evt pdc.click id=12", 12)]
    [InlineData("<DB1 0 evt pdc.click id=65535", 65535)]
    [InlineData("<DB1 0 evt pdc.click id=0", null)]
    [InlineData("<DB1 0 evt pdc.click id=65536", null)]
    [InlineData("<DB1 0 evt pdc.click id=-1", null)]
    [InlineData("<DB1 0 evt pdc.click", null)]
    [InlineData("<DB1 0 evt mood id=12", null)]
    public void Click_events_carry_an_id_from_1_to_65535(string content, int? expected)
    {
        var frame = Frame.TryParse($"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}", out var error);
        Assert.Null(error);
        Assert.Equal(expected, PdcWire.ClickId(frame!));
    }

    // --- certificate and server address --------------------------------------------------------------------------

    [Fact]
    public void The_pinned_certificate_is_accepted_only_when_the_chain_is_the_sole_problem()
    {
        using var pinned = PdcCertificate.LoadPinned();
        Assert.Equal("74AF4426EB46933FA4FC8EA73DF06B8932DB39B0", pinned.Thumbprint);
        var now = new DateTime(2026, 10, 15);
        using var presented = X509CertificateLoader.LoadCertificate(pinned.RawData);

        Assert.True(PdcCertificate.Validate(presented, SslPolicyErrors.None, pinned, now)); // trusted by Windows
        Assert.True(PdcCertificate.Validate(presented, SslPolicyErrors.RemoteCertificateChainErrors, pinned, now));
        Assert.False(PdcCertificate.Validate(presented, SslPolicyErrors.RemoteCertificateNameMismatch, pinned, now));
        Assert.False(PdcCertificate.Validate(presented,
            SslPolicyErrors.RemoteCertificateChainErrors | SslPolicyErrors.RemoteCertificateNameMismatch, pinned, now));
        Assert.False(PdcCertificate.Validate(null, SslPolicyErrors.RemoteCertificateNotAvailable, pinned, now));
        Assert.False(PdcCertificate.Validate(presented, SslPolicyErrors.RemoteCertificateChainErrors, pinned, new DateTime(2027, 4, 1)));

        // Another certificate with the same name is refused.
        using var key = RSA.Create(2048);
        var request = new CertificateRequest("CN=PDCManager", key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        using var other = request.CreateSelfSigned(DateTimeOffset.Now.AddDays(-1), DateTimeOffset.Now.AddYears(1));
        Assert.False(PdcCertificate.Validate(other, SslPolicyErrors.RemoteCertificateChainErrors, pinned, now));
    }

    [Theory]
    [InlineData(null, "https://pdcmanager/", false)]
    [InlineData("", "https://pdcmanager/", false)]
    [InlineData("http://127.0.0.1:8765", "http://127.0.0.1:8765/", false)]
    [InlineData("http://localhost:8765/", "http://localhost:8765/", false)]
    [InlineData("https://[::1]:8443", "https://[::1]:8443/", false)]
    [InlineData("http://pdcmanager", "https://pdcmanager/", true)]
    [InlineData("https://evil.example:8765", "https://pdcmanager/", true)]
    [InlineData("http://127.0.0.1.evil.example", "https://pdcmanager/", true)]
    [InlineData("http://10.1.2.3:8765", "https://pdcmanager/", true)]
    [InlineData("http://127.0.0.1:8765/zdmw", "https://pdcmanager/", true)]
    [InlineData("ftp://127.0.0.1", "https://pdcmanager/", true)]
    [InlineData("not a url", "https://pdcmanager/", true)]
    public void The_server_override_is_only_taken_for_loopback(string? value, string expected, bool ignored)
    {
        Assert.Equal(expected, PdcEndpoint.Resolve(value, out var warning).ToString());
        Assert.Equal(ignored, warning is not null);
    }
}

internal sealed class FakeClock : TimeProvider
{
    public DateTimeOffset Now { get; set; } = new(2026, 10, 15, 9, 0, 0, TimeSpan.Zero);
    public override DateTimeOffset GetUtcNow() => Now;
    public void Advance(TimeSpan by) => Now += by;
}

/// <summary>PDC Manager as far as the companion uses it, in memory. Password "s3cret!" logs in; session=abc.</summary>
internal sealed class FakePdcServer : HttpMessageHandler
{
    public const string Host = "pdc.test";
    public static readonly Uri BaseUri = new($"https://{Host}");

    public sealed record Request(string Method, string Path, string? Cookie, string? Accept, string? ContentType, string Body);

    private readonly Dictionary<string, Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>>> _routes = [];
    private readonly object _lock = new();

    public List<Request> Requests { get; } = [];
    public HashSet<string> Hosts { get; } = [];
    /// <summary>Clicks the server processed (the counter on its side).</summary>
    public int Clicks { get; set; }

    public IEnumerable<string> Paths
    {
        get
        {
            lock (_lock)
            {
                return Requests.Select(r => $"{r.Method} {r.Path}").ToList();
            }
        }
    }

    public FakePdcServer()
    {
        Route("GET /health", (_, _) => Json(200, """{"status":"ok"}"""));
        Route("POST /auth/login", async (req, ct) =>
            (await req.Content!.ReadAsStringAsync(ct)).Contains("password=s3cret%21", StringComparison.Ordinal)
                ? Respond(302, "", "text/html", "/", "session=abc; Path=/; HttpOnly", "theme=dark; Path=/")
                : Html(401, "<p>Ongeldige gebruikersnaam of wachtwoord</p>"));
        Route("GET /auth/api/me/profile", (req, _) =>
            (req.Headers.TryGetValues("Cookie", out var c) ? string.Join("; ", c) : "").Contains("session=")
                ? Json(200, """{"success":true,"profile":{"username":"JDoe","display_name":"Jan Doe","role":"user","blueprint_access":{"zdmw":"write"}}}""")
                : Json(401, """{"success":false,"error":"Niet ingelogd"}"""));
        Route("GET /zdmw/api/count", (_, _) => Json(200, """{"success":true,"count":41}"""));
        Route("POST /zdmw/api/click", (_, _) =>
        {
            Clicks++;
            return Json(200, $$"""{"success":true,"count":{{41 + Clicks}},"webhook_sent":true,"webhook_error":null,"image_url":"/x.png"}""");
        });
    }

    /// <summary>A transport failure, as the real handler throws it.</summary>
    public static HttpResponseMessage Fail(Exception e) => throw e;

    public void Route(string key, Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> handler) => _routes[key] = handler;

    public void Route(string key, Func<HttpRequestMessage, CancellationToken, HttpResponseMessage> handler) =>
        _routes[key] = (req, ct) => Task.FromResult(handler(req, ct));

    public int Count(string method, string path)
    {
        lock (_lock)
        {
            return Requests.Count(r => r.Method == method && r.Path == path);
        }
    }

    protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        var uri = request.RequestUri!;
        var body = request.Content is null ? "" : await request.Content.ReadAsStringAsync(cancellationToken);
        lock (_lock)
        {
            Hosts.Add(uri.Host);
            Requests.Add(new Request(request.Method.Method, uri.AbsolutePath,
                request.Headers.TryGetValues("Cookie", out var c) ? string.Join("; ", c) : null,
                request.Headers.Accept.ToString(), request.Content?.Headers.ContentType?.MediaType, body));
        }
        if (uri.Host != Host)
        {
            throw new InvalidOperationException($"request to a foreign host: {uri.Host}");
        }
        return _routes.TryGetValue($"{request.Method.Method} {uri.AbsolutePath}", out var route)
            ? await route(request, cancellationToken)
            : Json(404, """{"success":false}""");
    }

    public static HttpResponseMessage Json(int status, string body, string? location = null, string? cookie = null) =>
        cookie is null ? Respond(status, body, "application/json", location) : Respond(status, body, "application/json", location, cookie);

    public static HttpResponseMessage Html(int status, string body) => Respond(status, body, "text/html");

    public static HttpResponseMessage Respond(int status, string body, string mediaType, string? location = null, params string[] cookies)
    {
        var response = new HttpResponseMessage((HttpStatusCode)status) { Content = new StringContent(body, Encoding.UTF8, mediaType) };
        if (location is not null)
        {
            response.Headers.Location = new Uri(location, UriKind.RelativeOrAbsolute);
        }
        foreach (var cookie in cookies)
        {
            response.Headers.Add("Set-Cookie", cookie);
        }
        return response;
    }
}
