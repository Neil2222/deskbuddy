using System.Net;
using System.Net.Http.Headers;
using System.Security.Authentication;
using System.Text;
using System.Text.Json;

namespace DeskBuddy.Companion.Pdc;

/// <summary>Why a request got no HTTP response.</summary>
public enum PdcNetError
{
    None,
    /// <summary>No connection at all (name not found, refused, connect timed out): the request was not sent.</summary>
    Offline,
    /// <summary>The server's certificate was refused: the request was not sent.</summary>
    Tls,
    /// <summary>No response in time: the request may have been processed.</summary>
    Timeout,
    /// <summary>The connection broke after the request went out, or the response was garbled: it may have been processed.</summary>
    Unknown,
}

/// <summary>An HTTP response (Error None) or why there was none.</summary>
public sealed record PdcResponse(PdcNetError Error, int Status, string? Location, string Body)
{
    public static PdcResponse Failed(PdcNetError error) => new(error, 0, null, "");

    public bool IsRedirect => Status is >= 300 and < 400;

    /// <summary>Not (or no longer) logged in: 401, or a redirect to the login page.</summary>
    public bool IsLoginRequired =>
        Error == PdcNetError.None &&
        (Status == 401 || IsRedirect && (Location ?? "").Contains("/auth/login", StringComparison.OrdinalIgnoreCase));

    public JsonElement? Json()
    {
        try
        {
            using var doc = JsonDocument.Parse(Body);
            return doc.RootElement.Clone();
        }
        catch (JsonException)
        {
            return null;
        }
    }
}

/// <summary>
/// PDC Manager over HTTP(S), as pdc.py talks to it: no redirects followed, the session kept as a cookie dictionary
/// (Set-Cookie from every response is taken in), JSON in and out. Never logs or exposes headers or bodies.
/// </summary>
public sealed class PdcClient : IDisposable
{
    private readonly HttpClient _http;
    private readonly TimeSpan _timeout;

    public Uri BaseUri { get; }
    /// <summary>The session cookies (name → value).</summary>
    public Dictionary<string, string> Cookies { get; private set; } = new(StringComparer.Ordinal);

    public PdcClient(Uri baseUri, HttpMessageHandler handler, TimeSpan timeout, bool disposeHandler = true)
    {
        BaseUri = baseUri;
        _timeout = timeout;
        _http = new HttpClient(handler, disposeHandler) { BaseAddress = baseUri, Timeout = Timeout.InfiniteTimeSpan };
    }

    public void SetCookies(IReadOnlyDictionary<string, string> cookies) => Cookies = new(cookies, StringComparer.Ordinal);

    public Task<PdcResponse> GetAsync(string path) => SendAsync(HttpMethod.Get, path, null);

    public Task<PdcResponse> PostJsonAsync(string path, object body) =>
        SendAsync(HttpMethod.Post, path, new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, "application/json"));

    public Task<PdcResponse> PostFormAsync(string path, IEnumerable<KeyValuePair<string, string>> fields) =>
        SendAsync(HttpMethod.Post, path, new FormUrlEncodedContent(fields));

    /// <summary>One request, never retried. Returns the response, or the reason there is none.</summary>
    public async Task<PdcResponse> SendAsync(HttpMethod method, string path, HttpContent? content)
    {
        using var request = new HttpRequestMessage(method, path) { Content = content };
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        if (Cookies.Count > 0)
        {
            request.Headers.TryAddWithoutValidation("Cookie", string.Join("; ", Cookies.Select(c => $"{c.Key}={c.Value}")));
        }
        using var cts = new CancellationTokenSource(_timeout);
        var sent = false;
        try
        {
            using var response = await _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cts.Token);
            sent = true;
            Absorb(response);
            var body = await response.Content.ReadAsStringAsync(cts.Token);
            return new PdcResponse(PdcNetError.None, (int)response.StatusCode, response.Headers.Location?.OriginalString, body);
        }
        catch (OperationCanceledException e) when (!sent && !cts.IsCancellationRequested && HasInner<TimeoutException>(e))
        {
            return PdcResponse.Failed(PdcNetError.Offline); // the handler's connect timeout: nothing was sent
        }
        catch (OperationCanceledException)
        {
            return PdcResponse.Failed(PdcNetError.Timeout);
        }
        catch (HttpRequestException e) when (!sent)
        {
            return PdcResponse.Failed(Classify(e));
        }
        catch (Exception e) when (e is HttpRequestException or IOException)
        {
            return PdcResponse.Failed(PdcNetError.Unknown); // the response broke off
        }
    }

    /// <summary>Which failures mean the request certainly did not reach the server.</summary>
    public static PdcNetError Classify(HttpRequestException e)
    {
        if (HasInner<AuthenticationException>(e) || e.HttpRequestError == HttpRequestError.SecureConnectionError)
        {
            return PdcNetError.Tls;
        }
        if (HasInner<TimeoutException>(e) && e.HttpRequestError != HttpRequestError.ConnectionError)
        {
            return PdcNetError.Timeout;
        }
        return e.HttpRequestError switch
        {
            HttpRequestError.NameResolutionError or HttpRequestError.ConnectionError or HttpRequestError.ProxyTunnelError
                => PdcNetError.Offline,
            _ => PdcNetError.Unknown,
        };
    }

    private static bool HasInner<T>(Exception e) where T : Exception
    {
        for (Exception? x = e; x is not null; x = x.InnerException)
        {
            if (x is T)
            {
                return true;
            }
        }
        return false;
    }

    // Takes in Set-Cookie like pdc.py: a cookie with Max-Age=0 or an empty value is removed.
    private void Absorb(HttpResponseMessage response)
    {
        if (!response.Headers.TryGetValues("Set-Cookie", out var values))
        {
            return;
        }
        foreach (var raw in values)
        {
            var parts = raw.Split(';');
            var eq = parts[0].IndexOf('=');
            if (eq <= 0)
            {
                continue;
            }
            var name = parts[0][..eq].Trim();
            var value = parts[0][(eq + 1)..].Trim();
            var expired = parts.Skip(1).Any(p =>
            {
                var a = p.Trim();
                if (a.StartsWith("max-age=", StringComparison.OrdinalIgnoreCase))
                {
                    return int.TryParse(a[8..], out var age) && age <= 0;
                }
                return a.StartsWith("expires=", StringComparison.OrdinalIgnoreCase) &&
                       DateTimeOffset.TryParse(a[8..], System.Globalization.CultureInfo.InvariantCulture,
                                               System.Globalization.DateTimeStyles.AssumeUniversal, out var when) &&
                       when < DateTimeOffset.UtcNow;
            });
            if (expired || value.Length == 0 || value == "\"\"")
            {
                Cookies.Remove(name);
            }
            else
            {
                Cookies[name] = value;
            }
        }
    }

    public void Dispose() => _http.Dispose();
}
