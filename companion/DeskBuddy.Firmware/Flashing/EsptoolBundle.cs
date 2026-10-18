using System.Security.Cryptography;

namespace DeskBuddy.Firmware.Flashing;

/// <summary>
/// The esptool.exe the Companion ships: Espressif's own Windows build of esptool v4.12.0
/// (esptool-v4.12.0-windows-amd64.zip from github.com/espressif/esptool/releases), pinned by SHA-256. It is embedded
/// in the Companion, written to a tools folder on first use, and checked against the pin before every run; while it
/// runs, the file is held open so nothing can replace it. Licence and sources: companion/third_party/esptool.
/// </summary>
public static class EsptoolBundle
{
    public const string Version = Esptool.Version;
    /// <summary>SHA-256 of esptool-v4.12.0-windows-amd64.zip as GitHub publishes it (the release asset's digest).</summary>
    public const string ReleaseZipSha256 = "42fddc5e6a05716868ad77fb43acbf53be041f97abed87ff850df1dc88140889";
    /// <summary>SHA-256 of esptool.exe inside that zip.</summary>
    public const string ExeSha256 = "1cd8386a7934862eb6ce1e14af3cc2309b0a743de1171fd09fab2f2ef2f5d1a3";
    public const string ResourceName = "DeskBuddy.Tools.esptool.exe";

    /// <summary>
    /// The pinned esptool.exe in <paramref name="toolsFolder"/>, written from <paramref name="embedded"/> when it is
    /// missing or not the pinned file, and opened so it cannot change while it is used (dispose to release it).
    /// Throws <see cref="InvalidDataException"/> when even the embedded copy is not the pinned file.
    /// </summary>
    public static EsptoolLease Prepare(string toolsFolder, Func<Stream?> embedded, string expectedSha256 = ExeSha256)
    {
        var folder = Path.Combine(toolsFolder, $"esptool-{Version}");
        var exe = Path.Combine(folder, "esptool.exe");
        if (TryLease(exe, expectedSha256) is { } lease)
        {
            return lease;
        }
        using (var source = embedded() ?? throw new FileNotFoundException("this Companion build does not include esptool.exe (tools\\fetch-esptool.ps1)"))
        {
            using var memory = new MemoryStream();
            source.CopyTo(memory);
            if (Sha256(memory.ToArray()) != expectedSha256)
            {
                throw new InvalidDataException("the esptool.exe inside this Companion is not the pinned v4.12.0 file");
            }
            Directory.CreateDirectory(folder);
            var tmp = exe + ".tmp";
            File.WriteAllBytes(tmp, memory.ToArray());
            File.Move(tmp, exe, overwrite: true);
        }
        return TryLease(exe, expectedSha256) ?? throw new InvalidDataException($"{exe} changed while it was written");
    }

    /// <summary>The file at <paramref name="exe"/>, locked against writes, if it is the pinned one; else null.</summary>
    public static EsptoolLease? TryLease(string exe, string expectedSha256 = ExeSha256)
    {
        if (!File.Exists(exe))
        {
            return null;
        }
        FileStream? stream = null;
        try
        {
            // Others may read and run it, nobody may write, rename or delete it while this is open.
            stream = new FileStream(exe, FileMode.Open, FileAccess.Read, FileShare.Read);
            var ok = Convert.ToHexStringLower(SHA256.HashData(stream)) == expectedSha256;
            if (!ok)
            {
                stream.Dispose();
                return null;
            }
            return new EsptoolLease(exe, stream);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            stream?.Dispose();
            return null;
        }
    }

    private static string Sha256(byte[] data) => Convert.ToHexStringLower(SHA256.HashData(data));
}

/// <summary>A checked esptool.exe, held open (read-only, no writers) until disposed.</summary>
public sealed class EsptoolLease(string path, FileStream hold) : IDisposable
{
    public string Path { get; } = path;
    public void Dispose() => hold.Dispose();
}
