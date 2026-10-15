using System.Net.Security;
using System.Security.Cryptography.X509Certificates;

namespace DeskBuddy.Companion.Pdc;

/// <summary>
/// PDC Manager runs on its own self-signed certificate (CN=PDCManager, SAN PDCManager and beesel, until 2027-03-16).
/// A PC that trusts it (installed in Windows) validates normally; otherwise exactly that certificate is accepted, and
/// only when the chain is the sole problem: a wrong host name or a missing certificate is always refused. Used per
/// handler (the companion's own HttpClient), never globally.
/// </summary>
public static class PdcCertificate
{
    private const string Resource = "DeskBuddy.Pdc.PDCManager.cer";

    /// <summary>The certificate that ships with the companion (Pdc/PDCManager.cer).</summary>
    public static X509Certificate2 LoadPinned()
    {
        using var stream = typeof(PdcCertificate).Assembly.GetManifestResourceStream(Resource)
                           ?? throw new InvalidOperationException($"missing resource {Resource}");
        using var reader = new StreamReader(stream);
        return X509Certificate2.CreateFromPem(reader.ReadToEnd());
    }

    /// <summary>True when the server's certificate is acceptable.</summary>
    public static bool Validate(X509Certificate? presented, SslPolicyErrors errors, X509Certificate2 pinned, DateTime now)
    {
        if (errors == SslPolicyErrors.None)
        {
            return true;
        }
        if (errors != SslPolicyErrors.RemoteCertificateChainErrors || presented is null)
        {
            return false;
        }
        var raw = presented.GetRawCertData();
        return raw.AsSpan().SequenceEqual(pinned.RawData) && now >= pinned.NotBefore && now <= pinned.NotAfter;
    }

    /// <summary>The handler for PDC Manager: pinned certificate, no redirects, no automatic cookies (the session is
    /// kept by <see cref="PdcClient"/>), and a short connect timeout so "not reachable" is told apart from "no answer".</summary>
    public static SocketsHttpHandler CreateHandler(X509Certificate2 pinned) => new()
    {
        AllowAutoRedirect = false,
        UseCookies = false,
        ConnectTimeout = TimeSpan.FromSeconds(5),
        // Every request on a fresh connection: SocketsHttpHandler resends a request by itself when a reused
        // connection turns out to be closed, and a click must never go out twice. (A request a minute; no cost.)
        PooledConnectionLifetime = TimeSpan.Zero,
        SslOptions = new SslClientAuthenticationOptions
        {
            RemoteCertificateValidationCallback = (_, cert, _, errors) => Validate(cert, errors, pinned, DateTime.Now),
        },
    };
}
