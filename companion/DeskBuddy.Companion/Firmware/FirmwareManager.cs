using System.Reflection;
using DeskBuddy.Companion.Device;
using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Firmware;
using DeskBuddy.Firmware.Flashing;

namespace DeskBuddy.Companion.Firmware;

/// <summary>
/// The Companion's side of the Firmware Manager: which keys it trusts, where the flashing tool and backups go, which
/// port and DeskBuddy it is about, and the hand-over of the serial port (<see cref="IDevicePort"/>). The steps
/// themselves are <see cref="FirmwareInstaller"/>'s.
/// </summary>
public sealed class FirmwareManager : IDevicePort, IFirmwareHost
{
    private readonly CompanionService _service;
    private readonly IProcessRunner _runner;
    private readonly Func<Stream?> _esptool;
    private readonly Action<string> _log;
    private readonly SynchronizationContext _ui;

    public IReadOnlyList<TrustedKey> Trusted { get; }
    public string ToolsFolder { get; }
    public string BackupRoot { get; }

    public FirmwareManager(CompanionService service, IReadOnlyList<TrustedKey> trusted, string toolsFolder, string backupRoot,
        IProcessRunner runner, Func<Stream?> esptool, Action<string> log)
    {
        _service = service;
        Trusted = trusted;
        ToolsFolder = toolsFolder;
        BackupRoot = backupRoot;
        _runner = runner;
        _esptool = esptool;
        _log = log;
        _ui = SynchronizationContext.Current ?? new SynchronizationContext();
    }

    /// <summary>
    /// The real one: the production keys built into this Companion, the embedded esptool, and folders that follow
    /// DESKBUDDY_DATA_DIR (a test setup never touches the real tools or backup folders).
    /// </summary>
    public static FirmwareManager ForCompanion(CompanionService service, Action<string> log)
    {
        var data = PdcStore.DataDirectoryOverride;
        var tools = data is not null ? Path.Combine(data, "firmware-tools")
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskBuddy", "Tools");
        var backups = data is not null ? Path.Combine(data, "firmware-backups")
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "DeskBuddy-backups");
        return new FirmwareManager(service, FirmwareTrust.Production, tools, backups, new SystemProcessRunner(),
            () => typeof(FirmwareManager).Assembly.GetManifestResourceStream(EsptoolBundle.ResourceName), log);
    }

    public bool ToolBundled
    {
        get
        {
            using var s = _esptool();
            return s is not null;
        }
    }

    /// <summary>This Companion's semantic version (the csproj Version, without the +commit).</summary>
    public static FirmwareVersion CompanionVersion { get; } =
        FirmwareVersion.TryParse(typeof(FirmwareManager).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?
            .InformationalVersion.Split('+')[0]) ?? FirmwareVersion.Parse("0.0.0");

    public FirmwarePackage Open(string path) => FirmwarePackage.Open(path, Trusted);

    /// <summary>The connected DeskBuddy on a serial port, once hello and info are in; null otherwise.</summary>
    public DeviceReport? Connected =>
        _service is { IsConnected: true, IdentityKnown: true, PortName: { } port } && port != DeviceLink.SimulatorName && _service.Firmware is { } fw
            ? new DeviceReport(fw, _service.Build, _service.DeviceId, _service.BootId, _service.Board, _service.Hardware)
            : null;

    public InstallDecision Decide(FirmwarePackage package)
    {
        var c = Connected;
        return UpdatePolicy.Decide(package.Manifest, c is null ? null : new InstalledFirmware(c.Firmware, c.Build, c.Board, c.Hardware),
            CompanionVersion, Frame.Version);
    }

    /// <summary>
    /// The port to flash: the connected DeskBuddy's, else the only CH343 UART port; null when there is none or
    /// more than one (then the user connects to the one meant first).
    /// </summary>
    public string? TargetPort()
    {
        if (_service.PortName is { } p && p != DeviceLink.SimulatorName)
        {
            return p;
        }
        var boards = PortFinder.List().Where(x => x.IsDeskBuddy).ToList();
        return boards.Count == 1 ? boards[0].Name : null;
    }

    /// <summary>A new folder for a full backup before this operation.</summary>
    public string NewBackupFolder(DeviceReport? before, FirmwarePackage package) =>
        Path.Combine(BackupRoot, $"fw-{Safe(before?.Firmware ?? "none")}-before-{package.Manifest.Version}-{DateTime.Now:yyyyMMdd-HHmmss}");

    private static string Safe(string s) => new(s.Select(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '-' ? c : '_').ToArray());

    public async Task<InstallResult> RunAsync(InstallRequest request, IProgress<InstallProgress> progress,
        Func<string, CancellationToken, Task<bool>> manualBootPrompt, CancellationToken ct)
    {
        EsptoolLease lease;
        try
        {
            lease = await Task.Run(() => EsptoolBundle.Prepare(ToolsFolder, _esptool), ct);
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            return new InstallResult(InstallFailure.ToolMissing, $"The flashing tool is not available: {e.Message}",
                "Nothing was changed. Use a Companion build that includes esptool (tools\\fetch-esptool.ps1, then publish).",
                false, null, null, null);
        }
        using (lease)
        {
            var esptool = new Esptool(_runner, lease.Path, Path.Combine(ToolsFolder, "work"), request.Package.Profile);
            var installer = new FirmwareInstaller(esptool, this, Path.Combine(ToolsFolder, "runs"), manualBootPrompt, _log);
            return await installer.RunAsync(request, progress, ct);
        }
    }

    // ---- IDevicePort ----

    public async Task<IDisposable> AcquireAsync(string port, CancellationToken ct)
    {
        if (port == DeviceLink.SimulatorName)
        {
            throw new PortBusyException("The simulator cannot be flashed.");
        }
        var hold = await OnUi(() => _service.HoldPortForFirmware(port));
        try
        {
            await Task.Delay(TimeSpan.FromSeconds(1), ct); // Windows releases the port a moment after it is closed
        }
        catch (OperationCanceledException)
        {
            hold.Dispose();
            throw;
        }
        return hold;
    }

    public async Task<DeviceReport?> WaitForDeskBuddyAsync(string port, TimeSpan timeout, CancellationToken ct)
    {
        var until = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < until)
        {
            var report = await OnUi(() => _service.PortName == port ? Connected : null);
            if (report is not null)
            {
                return report;
            }
            await Task.Delay(250, ct);
        }
        return null;
    }

    private Task<T> OnUi<T>(Func<T> f)
    {
        var done = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ui.Post(_ =>
        {
            try
            {
                done.SetResult(f());
            }
            catch (Exception e)
            {
                done.SetException(e);
            }
        }, null);
        return done.Task;
    }
}

/// <summary>
/// The public keys this Companion accepts firmware packages from: PEM files embedded from Firmware/TrustedKeys/ at
/// build time. There are none yet; a production key is added there (only the public half) when it is created
/// (docs/firmware-packages.md, "Signing"). A key is never trusted any other way: not from a file next to the exe, not
/// from an environment variable, not because it signed the package being opened.
/// </summary>
public static class FirmwareTrust
{
    private const string Prefix = "DeskBuddy.Firmware.TrustedKeys.";

    public static IReadOnlyList<TrustedKey> Production { get; } = Load();

    private static IReadOnlyList<TrustedKey> Load()
    {
        var assembly = typeof(FirmwareTrust).Assembly;
        var keys = new List<TrustedKey>();
        foreach (var name in assembly.GetManifestResourceNames().Where(n => n.StartsWith(Prefix, StringComparison.Ordinal)).Order())
        {
            using var s = assembly.GetManifestResourceStream(name)!;
            using var r = new StreamReader(s);
            var pem = r.ReadToEnd();
            if (pem.Contains("PRIVATE KEY", StringComparison.Ordinal) || !pem.Contains("-----BEGIN PUBLIC KEY-----", StringComparison.Ordinal))
            {
                throw new InvalidDataException($"{name} is not a public key: a private key is never built into the Companion");
            }
            keys.Add(TrustedKey.FromPem(name[Prefix.Length..], pem));
        }
        return keys;
    }
}
