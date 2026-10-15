using System.Net;

namespace DeskBuddy.Companion.Pdc;

/// <summary>
/// Where PDC Manager is: always https://pdcmanager, except for a local fake server (tools/pdc-fake-server.py) named
/// in DESKBUDDY_PDC_URL. The override is taken only for a loopback host, so it can never point the companion (and the
/// user's session) at another machine.
/// </summary>
public static class PdcEndpoint
{
    public const string EnvironmentVariable = "DESKBUDDY_PDC_URL";
    public static readonly Uri Default = new("https://pdcmanager");

    /// <summary>The base URL to use; <paramref name="ignored"/> is set when an override was given but refused.</summary>
    public static Uri Resolve(string? overrideUrl, out string? ignored)
    {
        ignored = null;
        if (string.IsNullOrWhiteSpace(overrideUrl))
        {
            return Default;
        }
        if (Uri.TryCreate(overrideUrl.Trim(), UriKind.Absolute, out var uri) &&
            (uri.Scheme == Uri.UriSchemeHttp || uri.Scheme == Uri.UriSchemeHttps) && IsLoopback(uri) &&
            uri.UserInfo.Length == 0 && uri.Query.Length == 0 && uri.Fragment.Length == 0 && uri.AbsolutePath == "/")
        {
            return new Uri(uri.GetLeftPart(UriPartial.Authority));
        }
        ignored = $"{EnvironmentVariable} ignored: only http(s)://127.0.0.1, localhost or [::1] (with a port) is allowed";
        return Default;
    }

    public static bool IsLoopback(Uri uri) =>
        uri.IsLoopback && (uri.HostNameType != UriHostNameType.Dns || uri.Host.Equals("localhost", StringComparison.OrdinalIgnoreCase)) ||
        IPAddress.TryParse(uri.Host.Trim('[', ']'), out var ip) && IPAddress.IsLoopback(ip);
}
