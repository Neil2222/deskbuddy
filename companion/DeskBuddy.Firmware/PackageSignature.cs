using System.Security.Cryptography;

namespace DeskBuddy.Firmware;

/// <summary>
/// A public key the Companion accepts firmware packages from: ECDSA P-256, as a PEM "PUBLIC KEY" (SubjectPublicKeyInfo).
/// </summary>
public sealed class TrustedKey
{
    public string Name { get; }
    private readonly byte[] _spki;

    private TrustedKey(string name, byte[] spki)
    {
        Name = name;
        _spki = spki;
    }

    /// <summary>
    /// SHA-256 of the key's SubjectPublicKeyInfo (the DER inside the PEM), lower-case hex: what is recorded in
    /// docs/firmware-packages.md and checked against the file. The same as
    /// <c>openssl pkey -pubin -in key.pub.pem -outform DER | openssl dgst -sha256</c>.
    /// </summary>
    public string Fingerprint => Convert.ToHexStringLower(SHA256.HashData(_spki));

    public static TrustedKey FromPem(string name, string pem)
    {
        using var key = ECDsa.Create();
        try
        {
            key.ImportFromPem(pem);
        }
        catch (Exception e) when (e is ArgumentException or CryptographicException)
        {
            throw new FormatException($"{name}: not a PEM public key ({e.Message})");
        }
        var p = key.ExportParameters(false);
        if (p.Curve.Oid.Value != ECCurve.NamedCurves.nistP256.Oid.Value)
        {
            throw new FormatException($"{name}: not a P-256 key");
        }
        return new TrustedKey(name, key.ExportSubjectPublicKeyInfo());
    }

    internal bool Verifies(byte[] data, byte[] signature)
    {
        using var key = ECDsa.Create();
        key.ImportSubjectPublicKeyInfo(_spki, out _);
        return key.VerifyData(data, signature, HashAlgorithmName.SHA256, DSASignatureFormat.Rfc3279DerSequence);
    }
}

/// <summary>
/// The package signature: manifest.sig holds an ECDSA P-256 / SHA-256 signature over the exact bytes of manifest.json,
/// DER-encoded (what <c>openssl dgst -sha256 -sign key.pem</c> writes). The manifest holds every file's SHA-256, so
/// the signature covers the whole package.
/// </summary>
public static class PackageSignature
{
    /// <summary>Signs with a PEM private key ("PRIVATE KEY" or "EC PRIVATE KEY"). For the packaging tool and tests.</summary>
    public static byte[] Sign(byte[] manifest, string privateKeyPem)
    {
        using var key = ECDsa.Create();
        key.ImportFromPem(privateKeyPem);
        if (key.ExportParameters(false).Curve.Oid.Value != ECCurve.NamedCurves.nistP256.Oid.Value)
        {
            throw new ArgumentException("the signing key is not a P-256 key");
        }
        return key.SignData(manifest, HashAlgorithmName.SHA256, DSASignatureFormat.Rfc3279DerSequence);
    }

    /// <summary>The trusted key that made <paramref name="signature"/>, or null if none did.</summary>
    public static TrustedKey? Verify(byte[] manifest, byte[] signature, IReadOnlyList<TrustedKey> trusted)
    {
        foreach (var key in trusted)
        {
            try
            {
                if (key.Verifies(manifest, signature))
                {
                    return key;
                }
            }
            catch (CryptographicException)
            {
                // a malformed signature verifies with no key
            }
        }
        return null;
    }

    /// <summary>A new P-256 key pair as PEM (PKCS#8 private key, SubjectPublicKeyInfo public key).</summary>
    public static (string PrivatePem, string PublicPem) CreateKeyPair()
    {
        using var key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        return (key.ExportPkcs8PrivateKeyPem(), key.ExportSubjectPublicKeyInfoPem());
    }
}
