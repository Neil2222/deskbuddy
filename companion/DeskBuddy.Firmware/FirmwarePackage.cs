using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

namespace DeskBuddy.Firmware;

public enum PackageError
{
    NotAPackage,       // not a zip, too large, odd entries
    Unsigned,          // no manifest.sig
    SignatureInvalid,  // not signed by a trusted key, or changed after signing
    ManifestInvalid,   // manifest.json unreadable or not what this Companion understands
    UnsupportedBoard,  // for a board this Companion does not know
    FileMismatch,      // a file missing, extra, or not the size and SHA-256 the manifest names
    ImageInvalid,      // a file is not the ESP image it should be, or not for this chip and flash
    LayoutMismatch,    // would change the partition table or write outside the fixed regions
    BuildMismatch,     // the application is not the version / build the manifest names
}

public sealed class FirmwarePackageException(PackageError error, string message) : Exception(message)
{
    public PackageError Error { get; } = error;
}

public sealed record PackageImage(ImageSlot Slot, byte[] Data);

/// <summary>
/// A firmware package (docs/firmware-packages.md): a zip with manifest.json, manifest.sig and the images it names.
/// <see cref="Open"/> returns one only when every check has passed: the signature first, then every byte. Nothing
/// else in the Companion may treat a file as firmware.
/// </summary>
public sealed class FirmwarePackage
{
    public const string ManifestFile = "manifest.json";
    public const string SignatureFile = "manifest.sig";
    public const long MaxPackageBytes = 16 * 1024 * 1024;
    private const int MaxEntries = 8;

    public FirmwareManifest Manifest { get; }
    public BoardProfile Profile { get; }
    /// <summary>The key that signed it.</summary>
    public TrustedKey SignedBy { get; }
    /// <summary>The images in flash order, each exactly as it is to be written.</summary>
    public IReadOnlyList<PackageImage> Images { get; }

    private FirmwarePackage(FirmwareManifest manifest, BoardProfile profile, TrustedKey signedBy,
        IReadOnlyList<PackageImage> images)
    {
        Manifest = manifest;
        Profile = profile;
        SignedBy = signedBy;
        Images = images;
    }

    public static string FileName(FirmwareManifest m) => $"{m.Product}-{m.Version}-{m.Board}.zip";

    public static FirmwarePackage Open(string path, IReadOnlyList<TrustedKey> trusted)
    {
        using var stream = File.OpenRead(path);
        return Open(stream, trusted);
    }

    public static FirmwarePackage Open(Stream stream, IReadOnlyList<TrustedKey> trusted)
    {
        var files = ReadZip(stream);
        if (!files.TryGetValue(ManifestFile, out var manifestBytes))
        {
            throw new FirmwarePackageException(PackageError.NotAPackage, $"no {ManifestFile}: not a DeskBuddy firmware package");
        }
        if (!files.TryGetValue(SignatureFile, out var signature))
        {
            throw new FirmwarePackageException(PackageError.Unsigned, "the package is not signed");
        }
        var signedBy = PackageSignature.Verify(manifestBytes, signature, trusted) ??
            throw new FirmwarePackageException(PackageError.SignatureInvalid,
                "the package is not signed by a key this Companion trusts, or was changed after signing");

        FirmwareManifest manifest;
        try
        {
            manifest = FirmwareManifest.Parse(manifestBytes);
        }
        catch (FormatException e)
        {
            throw new FirmwarePackageException(PackageError.ManifestInvalid, e.Message);
        }
        var profile = Check(manifest, files, extraEntries: [ManifestFile, SignatureFile]);
        var images = profile.Images.Select(s => new PackageImage(s, files[s.File])).ToList();
        return new FirmwarePackage(manifest, profile, signedBy, images);
    }

    /// <summary>
    /// Every check of a package's content against its manifest and the fixed board profile: what <see cref="Open"/>
    /// does after the signature, and what the packaging tool does before it writes a package.
    /// </summary>
    public static BoardProfile Check(FirmwareManifest m, IReadOnlyDictionary<string, byte[]> files,
        IReadOnlyCollection<string> extraEntries)
    {
        if (m.Product != FirmwareManifest.ProductName)
        {
            throw new FirmwarePackageException(PackageError.ManifestInvalid, $"product '{m.Product}' is not {FirmwareManifest.ProductName}");
        }
        var profile = BoardProfile.Find(m.Board) ??
            throw new FirmwarePackageException(PackageError.UnsupportedBoard, $"board '{m.Board}' is not supported");
        if (m.Chip != profile.Chip || m.FlashSize != profile.FlashSize || m.FlashMode != profile.FlashMode ||
            m.FlashFreq != profile.FlashFreq)
        {
            throw new FirmwarePackageException(PackageError.ManifestInvalid,
                $"chip and flash ({m.Chip}, {m.FlashSize} {m.FlashMode} {m.FlashFreq}) are not the board's " +
                $"({profile.Chip}, {profile.FlashSize} {profile.FlashMode} {profile.FlashFreq})");
        }

        // Exactly the board's regions, in flash order; nothing else may be written.
        if (m.Images.Count != profile.Images.Count ||
            m.Images.Zip(profile.Images).Any(p => p.First.Name != p.Second.Name || p.First.File != p.Second.File ||
                                                  p.First.Offset != p.Second.Offset))
        {
            throw new FirmwarePackageException(PackageError.LayoutMismatch,
                "the package writes other regions than " + string.Join(", ",
                    profile.Images.Select(s => $"{s.Name} at {FirmwareManifest.ToHex(s.Offset)}")));
        }
        if (m.Preserve.Count != profile.Preserved.Count ||
            m.Preserve.Zip(profile.Preserved).Any(p => p.First.Name != p.Second.Name ||
                                                      p.First.Offset != p.Second.Offset || p.First.Size != p.Second.Size))
        {
            throw new FirmwarePackageException(PackageError.LayoutMismatch, "the package does not keep the board's preserved regions");
        }

        var expected = profile.Images.Select(s => s.File).Concat(extraEntries).ToHashSet();
        foreach (var name in files.Keys.Where(n => !expected.Contains(n)))
        {
            throw new FirmwarePackageException(PackageError.FileMismatch, $"unexpected file '{name}' in the package");
        }
        foreach (var (slot, entry) in profile.Images.Zip(m.Images))
        {
            if (!files.TryGetValue(slot.File, out var data))
            {
                throw new FirmwarePackageException(PackageError.FileMismatch, $"{slot.File} is missing");
            }
            if (data.Length != entry.Size || Convert.ToHexStringLower(SHA256.HashData(data)) != entry.Sha256)
            {
                throw new FirmwarePackageException(PackageError.FileMismatch,
                    $"{slot.File} is not the file the manifest names (size or SHA-256 differs)");
            }
            if (data.Length == 0 || (uint)data.Length > slot.MaxSize)
            {
                throw new FirmwarePackageException(PackageError.LayoutMismatch,
                    $"{slot.File} ({data.Length} bytes) does not fit its region ({slot.MaxSize} bytes)");
            }
            // Flash is erased in whole sectors: what a write touches must stay clear of the preserved regions.
            var end = (ulong)slot.Offset + ((uint)data.Length + BoardProfile.SectorSize - 1) / BoardProfile.SectorSize * BoardProfile.SectorSize;
            if (slot.Offset % BoardProfile.SectorSize != 0 ||
                profile.Preserved.Any(p => slot.Offset < p.End && end > p.Offset))
            {
                throw new FirmwarePackageException(PackageError.LayoutMismatch, $"{slot.File} would overwrite a preserved region");
            }
        }

        foreach (var slot in profile.Images)
        {
            var data = files[slot.File];
            if (slot.Name == "partition-table")
            {
                CheckPartitionTable(data, profile);
                continue;
            }
            EspImage image;
            try
            {
                image = EspImage.Parse(data, slot.IsApp);
            }
            catch (FormatException e)
            {
                throw new FirmwarePackageException(PackageError.ImageInvalid, $"{slot.File}: {e.Message}");
            }
            if (image.ChipId != profile.ChipId || image.FlashModeName != profile.FlashMode ||
                image.FlashSizeName != profile.FlashSize || image.FlashFreqName != profile.FlashFreq)
            {
                throw new FirmwarePackageException(PackageError.ImageInvalid,
                    $"{slot.File} is built for chip {image.ChipId}, {image.FlashSizeName} {image.FlashModeName} " +
                    $"{image.FlashFreqName}, not for this board");
            }
            if (image.App is { } app)
            {
                if (app.ProjectName != FirmwareManifest.ProductName || app.Version != m.Version.ToString() ||
                    app.IdfVersion != m.Idf)
                {
                    throw new FirmwarePackageException(PackageError.BuildMismatch,
                        $"{slot.File} is {app.ProjectName} {app.Version} (ESP-IDF {app.IdfVersion}), " +
                        $"the manifest says {FirmwareManifest.ProductName} {m.Version} (ESP-IDF {m.Idf})");
                }
                // The commit as the C string the firmware compiled in (port.c: DESKBUDDY_BUILD_COMMIT), so a build
                // from a tree with changes ("<commit>-dirty") never matches.
                if (data.AsSpan().IndexOf(Encoding.ASCII.GetBytes(m.Build + "\0")) < 0)
                {
                    throw new FirmwarePackageException(PackageError.BuildMismatch,
                        $"{slot.File} was not built from commit {m.Build} alone (no uncommitted changes)");
                }
            }
        }
        return profile;
    }

    private static void CheckPartitionTable(byte[] data, BoardProfile profile)
    {
        IReadOnlyList<Partition> parts;
        try
        {
            parts = PartitionTable.Parse(data);
        }
        catch (FormatException e)
        {
            throw new FirmwarePackageException(PackageError.ImageInvalid, $"partition table: {e.Message}");
        }
        if (!parts.SequenceEqual(profile.Partitions))
        {
            throw new FirmwarePackageException(PackageError.LayoutMismatch,
                "the partition table is not the board's (" +
                string.Join(", ", parts.Select(p => $"{p.Name} {FirmwareManifest.ToHex(p.Offset)}+{FirmwareManifest.ToHex(p.Size)}")) +
                "); changing the layout is not supported");
        }
    }

    // Every entry into memory (they are small), refusing anything that is not a flat list of a few files.
    private static Dictionary<string, byte[]> ReadZip(Stream stream)
    {
        if (stream.CanSeek && stream.Length > MaxPackageBytes)
        {
            throw new FirmwarePackageException(PackageError.NotAPackage, "the file is too large for a firmware package");
        }
        try
        {
            using var zip = new ZipArchive(stream, ZipArchiveMode.Read, leaveOpen: true);
            if (zip.Entries.Count > MaxEntries)
            {
                throw new FirmwarePackageException(PackageError.NotAPackage, "too many files for a firmware package");
            }
            var files = new Dictionary<string, byte[]>(StringComparer.Ordinal);
            long total = 0;
            foreach (var e in zip.Entries)
            {
                if (e.FullName.Length is 0 or > 64 || e.FullName.IndexOfAny(['/', '\\', ':']) >= 0 || e.FullName.StartsWith('.'))
                {
                    throw new FirmwarePackageException(PackageError.NotAPackage, $"unexpected entry '{e.FullName}'");
                }
                if (files.ContainsKey(e.FullName))
                {
                    throw new FirmwarePackageException(PackageError.NotAPackage, $"'{e.FullName}' is in the package twice");
                }
                total += e.Length;
                if (e.Length > MaxPackageBytes || total > MaxPackageBytes)
                {
                    throw new FirmwarePackageException(PackageError.NotAPackage, "the package unpacks to too much data");
                }
                var data = new byte[e.Length];
                using (var s = e.Open())
                {
                    s.ReadExactly(data);
                    if (s.ReadByte() >= 0)
                    {
                        throw new FirmwarePackageException(PackageError.NotAPackage, $"'{e.FullName}' is longer than its entry says");
                    }
                }
                files.Add(e.FullName, data);
            }
            return files;
        }
        catch (Exception e) when (e is InvalidDataException or EndOfStreamException)
        {
            throw new FirmwarePackageException(PackageError.NotAPackage, $"not a readable zip file ({e.Message})");
        }
    }

    /// <summary>
    /// The package as a zip: the manifest, the signature (null: unsigned, for signing elsewhere) and the images, in
    /// a fixed order with fixed timestamps, so the same content gives the same file.
    /// </summary>
    public static byte[] WriteZip(byte[] manifestJson, byte[]? signature, IEnumerable<(string Name, byte[] Data)> images)
    {
        var entries = new List<(string Name, byte[] Data)> { (ManifestFile, manifestJson) };
        if (signature is not null)
        {
            entries.Add((SignatureFile, signature));
        }
        entries.AddRange(images);
        using var buffer = new MemoryStream();
        using (var zip = new ZipArchive(buffer, ZipArchiveMode.Create, leaveOpen: true))
        {
            foreach (var (name, data) in entries)
            {
                var entry = zip.CreateEntry(name, CompressionLevel.Optimal);
                entry.LastWriteTime = new DateTimeOffset(1980, 1, 1, 0, 0, 0, TimeSpan.Zero);
                using var s = entry.Open();
                s.Write(data);
            }
        }
        return buffer.ToArray();
    }

    /// <summary>The entries of a package zip as they are, without any check (the packaging tool's sign step).</summary>
    public static Dictionary<string, byte[]> ReadEntriesUnchecked(Stream stream) => ReadZip(stream);
}
