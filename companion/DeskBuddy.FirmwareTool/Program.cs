using DeskBuddy.Firmware;

// deskbuddy-firmware-tool: makes, signs and checks DeskBuddy firmware packages (docs/firmware-packages.md).
//   pack    --build <esp-idf build dir> --commit <40 hex> --protocol <n> --min-companion <version> --out <dir>
//           [--compatible-bootloader-from <bootloader.bin or full flash backup>]...
//                                                          (bootloaders an app-only update may keep, listed and signed)
//   sign    --package <zip> --key <private key PEM>        (adds manifest.sig; refuses an already signed package)
//   verify  --package <zip> --public-key <PEM> [...]       (every check the Companion makes)
//   keygen  --private <file> --public <file>               (a new P-256 key pair; never overwrites)
// Exit code 0: done; 1: the package or the input was refused (the reason is printed); 2: usage.
try
{
    if (args.Length == 0)
    {
        return Usage();
    }
    var options = Options(args[1..]);
    return args[0] switch
    {
        "pack" => Pack(options),
        "sign" => Sign(options),
        "verify" => Verify(options),
        "keygen" => KeyGen(options),
        _ => Usage(),
    };
}
catch (UsageException e)
{
    Console.Error.WriteLine(e.Message);
    return Usage();
}
catch (Exception e) when (e is FirmwarePackageException or FormatException or IOException or UnauthorizedAccessException
                              or System.Text.Json.JsonException or KeyNotFoundException or ArgumentException
                              or System.Security.Cryptography.CryptographicException)
{
    Console.Error.WriteLine($"refused: {e.Message}");
    return 1;
}

static int Pack(Dictionary<string, List<string>> o)
{
    var profile = BoardProfile.WaveshareS3Lcd7B;
    var images = PackageBuilder.ReadBuild(One(o, "build"), profile);
    var protocol = int.TryParse(One(o, "protocol"), out var p) ? p : throw new UsageException("--protocol is a number");
    var minCompanion = FirmwareVersion.TryParse(One(o, "min-companion")) ?? throw new UsageException("--min-companion is a version");
    // --compatible-bootloader-from <file>: a bootloader.bin, or a full flash backup of a board in the field, whose
    // bootloader an app-only update may keep (it is then listed in the signed manifest).
    var known = o.TryGetValue("compatible-bootloader-from", out var sources)
        ? sources.Select(f => PackageBuilder.KnownBootloaderFrom(File.ReadAllBytes(f), Path.GetFileName(f), profile)).DistinctBy(k => k.Sha256).ToList()
        : [];
    var manifest = PackageBuilder.CreateManifest(profile, images, One(o, "commit"), protocol, minCompanion, known);
    var outDir = One(o, "out");
    Directory.CreateDirectory(outDir);
    var path = Path.Combine(outDir, FirmwarePackage.FileName(manifest));
    if (File.Exists(path))
    {
        throw new IOException($"{path} exists; remove it first");
    }
    File.WriteAllBytes(path, FirmwarePackage.WriteZip(manifest.ToJson(), null,
        profile.Images.Select(s => (s.File, images[s.File]))));
    Console.WriteLine($"packed (unsigned): {path}");
    Describe(manifest);
    return 0;
}

static int Sign(Dictionary<string, List<string>> o)
{
    var path = One(o, "package");
    Dictionary<string, byte[]> files;
    using (var s = File.OpenRead(path))
    {
        files = FirmwarePackage.ReadEntriesUnchecked(s);
    }
    if (files.ContainsKey(FirmwarePackage.SignatureFile))
    {
        throw new IOException($"{path} is already signed");
    }
    var manifestBytes = files[FirmwarePackage.ManifestFile];
    var manifest = FirmwareManifest.Parse(manifestBytes);
    var profile = FirmwarePackage.Check(manifest, files, extraEntries: [FirmwarePackage.ManifestFile]);
    var keyPath = One(o, "key");
    RefuseInsideGitWorkTree(keyPath, "signing key");
    var keyPem = File.ReadAllText(keyPath);
    var signature = PackageSignature.Sign(manifestBytes, keyPem);
    var tmp = path + ".signing";
    File.WriteAllBytes(tmp, FirmwarePackage.WriteZip(manifestBytes, signature,
        profile.Images.Select(i => (i.File, files[i.File]))));
    // Read back as the Companion will, trusting only this key's public half, before it replaces the unsigned one.
    FirmwarePackage.Open(tmp, [TrustedKey.FromPem("this signing key", keyPem)]);
    File.Move(tmp, path, overwrite: true);
    Console.WriteLine($"signed and verified: {path}");
    return 0;
}

static int Verify(Dictionary<string, List<string>> o)
{
    var keys = All(o, "public-key").Select(k => TrustedKey.FromPem(Path.GetFileName(k), File.ReadAllText(k))).ToList();
    var package = FirmwarePackage.Open(One(o, "package"), keys);
    Console.WriteLine($"verified: signed by {package.SignedBy.Name} (SHA-256 {package.SignedBy.Fingerprint})");
    Describe(package.Manifest);
    return 0;
}

static int KeyGen(Dictionary<string, List<string>> o)
{
    var privatePath = One(o, "private");
    var publicPath = One(o, "public");
    RefuseInsideGitWorkTree(privatePath, "private key");
    foreach (var p in new[] { privatePath, publicPath })
    {
        if (File.Exists(p))
        {
            throw new IOException($"{p} exists; a key is never overwritten");
        }
    }
    var (privatePem, publicPem) = PackageSignature.CreateKeyPair();
    File.WriteAllText(publicPath, publicPem);
    using (var f = new FileStream(privatePath, FileMode.CreateNew, FileAccess.Write))
    using (var w = new StreamWriter(f))
    {
        w.Write(privatePem);
    }
    Console.WriteLine($"private key: {privatePath} (keep it secret, never commit it)");
    Console.WriteLine($"public key:  {publicPath}");
    Console.WriteLine($"fingerprint: SHA-256 {TrustedKey.FromPem("new key", publicPem).Fingerprint}");
    return 0;
}

// A private key never lives in a Git working tree (this repository or any other): a careless "git add -A" could
// commit it, and a build could pick it up.
static void RefuseInsideGitWorkTree(string path, string what)
{
    for (var dir = Path.GetDirectoryName(Path.GetFullPath(path)); dir is not null; dir = Path.GetDirectoryName(dir))
    {
        if (Directory.Exists(Path.Combine(dir, ".git")) || File.Exists(Path.Combine(dir, ".git")))
        {
            throw new IOException($"the {what} {path} is inside the Git working tree {dir}; keep it outside any repository");
        }
    }
}

static void Describe(FirmwareManifest m)
{
    Console.WriteLine($"  {m.Product} {m.Version} build {m.Build} for {m.Board} (protocol {m.Protocol}, ESP-IDF {m.Idf}, Companion {m.MinCompanion}+)");
    foreach (var i in m.Images)
    {
        Console.WriteLine($"  {FirmwareManifest.ToHex(i.Offset),-8} {i.File,-20} {i.Size,8} B  sha256 {i.Sha256}");
    }
    foreach (var p in m.Preserve)
    {
        Console.WriteLine($"  kept     {p.Name} {FirmwareManifest.ToHex(p.Offset)}-{FirmwareManifest.ToHex(p.End - 1)}");
    }
    foreach (var b in m.CompatibleBootloaders)
    {
        Console.WriteLine($"  compatible bootloader {b.Sha256} ({b.Note})");
    }
}

static Dictionary<string, List<string>> Options(string[] args)
{
    var o = new Dictionary<string, List<string>>();
    for (var i = 0; i < args.Length; i += 2)
    {
        if (!args[i].StartsWith("--", StringComparison.Ordinal) || i + 1 >= args.Length)
        {
            throw new UsageException($"expected --option value, got '{args[i]}'");
        }
        var key = args[i][2..];
        if (!o.TryGetValue(key, out var list))
        {
            o[key] = list = [];
        }
        list.Add(args[i + 1]);
    }
    return o;
}

static string One(Dictionary<string, List<string>> o, string key) =>
    o.TryGetValue(key, out var v) && v.Count == 1 ? v[0] : throw new UsageException($"--{key} is required once");

static List<string> All(Dictionary<string, List<string>> o, string key) =>
    o.TryGetValue(key, out var v) ? v : throw new UsageException($"--{key} is required");

static int Usage()
{
    Console.Error.WriteLine("""
        usage: deskbuddy-firmware-tool pack   --build <dir> --commit <sha> --protocol <n> --min-companion <version> --out <dir> [--compatible-bootloader-from <file>]...
               deskbuddy-firmware-tool sign   --package <zip> --key <private.pem>
               deskbuddy-firmware-tool verify --package <zip> --public-key <public.pem> [--public-key ...]
               deskbuddy-firmware-tool keygen --private <file> --public <file>
        """);
    return 2;
}

sealed class UsageException(string message) : Exception(message);
