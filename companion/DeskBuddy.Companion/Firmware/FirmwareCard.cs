using DeskBuddy.Companion.UI;
using DeskBuddy.Firmware;
using DeskBuddy.Firmware.Flashing;

namespace DeskBuddy.Companion.Firmware;

/// <summary>What the Firmware card needs from the Companion (<see cref="FirmwareManager"/>; tests use a fake).</summary>
public interface IFirmwareHost
{
    IReadOnlyList<TrustedKey> Trusted { get; }
    bool ToolBundled { get; }
    DeviceReport? Connected { get; }
    string? TargetPort();
    FirmwarePackage Open(string path);
    InstallDecision Decide(FirmwarePackage package);
    string NewBackupFolder(DeviceReport? before, FirmwarePackage package);
    Task<InstallResult> RunAsync(InstallRequest request, IProgress<InstallProgress> progress,
        Func<string, CancellationToken, Task<bool>> manualBootPrompt, CancellationToken ct);
    /// <summary>Read-only: chip, features, MAC and flash chip; the board restarts twice, nothing is written.</summary>
    Task<IdentifyResult> IdentifyAsync(string port, EntryMode entry, DeviceReport? before,
        Func<string, CancellationToken, Task<bool>> manualBootPrompt, CancellationToken ct);
}

/// <summary>The user's answers in the confirmation dialog; null: cancelled.</summary>
public sealed record FirmwareConfirmation(bool ModelConfirmed, bool RepairConfirmed, bool GoWithoutBackup);

/// <summary>The questions the card asks (dialogs in the Companion, scripted in tests).</summary>
public interface IFirmwareDialogs
{
    string? PickPackage();
    FirmwareConfirmation? Confirm(FirmwareConfirmRequest request);
    bool ConfirmStop(bool writing);
    bool ConfirmIdentify(string port, DeviceReport? before);
    Task<bool> ManualBootAsync(string instruction);
}

public sealed record FirmwareConfirmRequest(
    FirmwareOperation Operation,
    FirmwarePackage Package,
    DeviceReport? Before,
    string Port,
    InstallDecision Decision,
    bool Backup,
    bool NeedsModelConfirmation);

/// <summary>
/// The Device page's Firmware card: the package, the board, what can be done with them, and the run itself. Nothing
/// happens without a click and a confirmation; the card only offers what <see cref="UpdatePolicy"/> and
/// <see cref="FirmwareInstaller.Refusal"/> allow.
/// </summary>
public sealed class FirmwareCard : Panel
{
    public const string DataStatement =
        "DeskBuddy keeps no data on the board. Schedule, History, Companion settings and PDC stay on this PC and are " +
        "not changed. The board's calibration area (0x9000–0xFFFF) is never written.";

    private readonly IFirmwareHost _host;
    private readonly IFirmwareDialogs _dialogs;
    private readonly Action<string> _log;
    private readonly Label _package = Value(), _board = Value(), _offer = Value(), _step = Value(), _result = Value();
    private readonly FlatButton _open, _install, _update, _repair, _cancel, _identify;
    private readonly CheckBox _backup = Check("Full backup first (16 MB)"), _manual = Check("Manual download mode (BOOT + RESET)");
    private readonly ProgressBar _bar = new() { Dock = DockStyle.Fill, Height = 10, Style = ProgressBarStyle.Continuous };
    private FirmwarePackage? _loaded;
    private string? _loadError;
    private CancellationTokenSource? _cts;
    private InstallStep _currentStep;
    private bool _backupTouched;
    private readonly TableLayoutPanel _progressRow;

    public bool Running => _cts is not null;

    public FirmwareCard(Theme theme, IFirmwareHost host, IFirmwareDialogs dialogs, Action<string> log)
    {
        _host = host;
        _dialogs = dialogs;
        _log = log;
        BackColor = Theme.Card;
        _open = new FlatButton(theme, ButtonLook.Quiet) { Text = "Open package…", Width = 150, Height = 32 };
        _install = new FlatButton(theme, ButtonLook.Accent) { Text = "Install DeskBuddy", Width = 170, Height = 34 };
        _update = new FlatButton(theme, ButtonLook.Accent) { Text = "Update firmware", Width = 160, Height = 34 };
        _repair = new FlatButton(theme, ButtonLook.Quiet) { Text = "Repair (full reinstall)…", Width = 200, Height = 34 };
        _identify = new FlatButton(theme, ButtonLook.Quiet) { Text = "Identify board…", Width = 150, Height = 34 };
        _cancel = new FlatButton(theme, ButtonLook.Quiet) { Text = "Cancel", Width = 100, Height = 34 };
        foreach (var l in new[] { _offer, _step, _result })
        {
            l.Font = theme.Small;
        }

        var grid = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 3 };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 72));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.Controls.Add(Caption("Package"), 0, 0);
        grid.Controls.Add(_package, 1, 0);
        grid.Controls.Add(_open, 2, 0);
        grid.Controls.Add(Caption("Board"), 0, 1);
        grid.Controls.Add(_board, 1, 1);
        grid.SetColumnSpan(_board, 2);
        grid.Controls.Add(_offer, 1, 2);
        grid.SetColumnSpan(_offer, 2);

        var actions = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 44, WrapContents = false, Padding = new Padding(68, 6, 0, 0) };
        actions.Controls.AddRange([_identify, _install, _update, _repair, _cancel]);
        var options = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 30, WrapContents = false, Padding = new Padding(58, 0, 0, 0) };
        options.Controls.AddRange([_backup, _manual]);
        _progressRow = new TableLayoutPanel { Dock = DockStyle.Top, Height = 30, ColumnCount = 2, Padding = new Padding(72, 6, 0, 0), Visible = false };
        _progressRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 260));
        _progressRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        _progressRow.Controls.Add(_bar, 0, 0);
        _progressRow.Controls.Add(_step, 1, 0);
        var result = new Panel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(72, 4, 0, 0) };
        result.Controls.Add(_result);
        Controls.Add(result);
        Controls.Add(_progressRow);
        Controls.Add(options);
        Controls.Add(actions);
        Controls.Add(grid);

        _open.Click += (_, _) => OpenPackage();
        _install.Click += async (_, _) => await StartAsync(FirmwareOperation.Install);
        _update.Click += async (_, _) => await StartAsync(FirmwareOperation.Update);
        _repair.Click += async (_, _) => await StartAsync(FirmwareOperation.Repair);
        _identify.Click += async (_, _) => await IdentifyAsync();
        _cancel.Click += (_, _) => Cancel();
        _backup.CheckedChanged += (_, _) => _backupTouched = true;
        RefreshState();
    }

    public FirmwarePackage? Package => _loaded;

    /// <summary>Opens a package file (it is verified completely, or refused with the reason).</summary>
    public void Load(string path)
    {
        try
        {
            _loaded = _host.Open(path);
            _loadError = null;
            _log($"Firmware package {Path.GetFileName(path)}: DeskBuddy {_loaded.Manifest.Version}, build {_loaded.Manifest.Build}, signed by {_loaded.SignedBy.Name}");
        }
        catch (Exception e) when (e is FirmwarePackageException or IOException or UnauthorizedAccessException)
        {
            _loaded = null;
            _loadError = $"{Path.GetFileName(path)} refused: {e.Message}";
            _log($"Firmware package {_loadError}");
        }
        RefreshState();
    }

    private void OpenPackage()
    {
        if (_dialogs.PickPackage() is { } path)
        {
            Load(path);
        }
    }

    /// <summary>Re-reads the device and package state (the Companion calls this whenever the connection changes).</summary>
    public void RefreshState()
    {
        var running = Running;
        var connected = _host.Connected;
        var port = _host.TargetPort();

        _package.ForeColor = _loadError is null ? Theme.Text : Theme.Red;
        _package.Text = _loadError ?? (_loaded is { } p
            ? $"DeskBuddy {p.Manifest.Version} (build {p.Manifest.Build[..7]}) for {p.Profile.DisplayName} · signed by {p.SignedBy.Name}"
            : _host.Trusted.Count == 0
                ? "None. This Companion trusts no signing key yet, so every package is refused."
                : "None opened.");

        _board.ForeColor = Theme.Text;
        _board.Text = connected is { } c
            ? $"DeskBuddy {c.Firmware}{(c.Build is { Length: >= 7 } b ? $" (build {b[..7]})" : "")} on {port} · device {c.DeviceId ?? "?"}" +
              (c.Board is null ? " · reports no board: you confirm the model" : c.Hardware == "ok" ? $" · reports {c.Board}, hardware check ok" : $" · hardware check: {c.Hardware}")
            : port is not null
                ? $"No DeskBuddy answers on {port}: Install identifies the board first (that resets it)."
                : "No board. Connect the board's UART USB-C port (one board only).";

        InstallDecision? decision = _loaded is { } pkg ? _host.Decide(pkg) : null;
        _offer.ForeColor = decision?.Offer == InstallOffer.Incompatible ? Theme.Red : Theme.Muted;
        _offer.Text = (decision is null ? "" : Sentence(decision.Reason) + ". ") + DataStatement;

        var ok = _loaded is not null && decision is not null && decision.Offer != InstallOffer.Incompatible && _host.ToolBundled && port is not null && !running;
        _install.Enabled = ok && connected is null;
        _update.Enabled = ok && connected is not null && decision!.Offer is InstallOffer.Update or InstallOffer.Reinstall or InstallOffer.Downgrade;
        _repair.Enabled = ok;
        _identify.Enabled = _host.ToolBundled && port is not null && !running;
        _open.Enabled = !running;
        _cancel.Enabled = running;
        _cancel.Visible = running;
        _backup.Enabled = _manual.Enabled = !running;
        if (!_backupTouched)
        {
            _backup.Checked = connected is not null; // on by default for an existing DeskBuddy, optional for a new board
        }
        if (!_host.ToolBundled)
        {
            _offer.Text = "This Companion build does not include the flashing tool (esptool). " + _offer.Text;
        }
        foreach (var button in new[] { _identify, _install, _update, _repair, _cancel, _open })
        {
            button.Invalidate();
        }
    }

    private async Task StartAsync(FirmwareOperation operation)
    {
        if (_loaded is not { } package || _host.TargetPort() is not { } port || Running)
        {
            return;
        }
        var before = _host.Connected;
        var decision = _host.Decide(package);
        var needsModel = operation != FirmwareOperation.Update || before is not { Hardware: "ok" } || before.Board != package.Profile.Board;
        var answer = _dialogs.Confirm(new FirmwareConfirmRequest(operation, package, before, port, decision, _backup.Checked, needsModel));
        if (answer is null)
        {
            return;
        }
        var backup = _backup.Checked ? BackupChoice.To(_host.NewBackupFolder(before, package))
            : answer.GoWithoutBackup ? BackupChoice.SkipConfirmedByUser : null;
        if (backup is null)
        {
            return;
        }
        var request = new InstallRequest(operation, package, port, _manual.Checked ? EntryMode.Manual : EntryMode.AutoReset, before,
            answer.ModelConfirmed, backup, answer.RepairConfirmed);

        _cts = new CancellationTokenSource();
        _result.Text = "";
        _bar.Value = 0;
        _progressRow.Visible = true;
        RefreshState();
        _log($"Firmware: {operation} of DeskBuddy {package.Manifest.Version} on {port} started");
        InstallResult result;
        try
        {
            result = await _host.RunAsync(request, new Progress<InstallProgress>(OnProgress),
                async (text, _) => await _dialogs.ManualBootAsync(text), _cts.Token);
        }
        finally
        {
            _cts.Dispose();
            _cts = null;
        }
        ShowResult(result);
        RefreshState();
    }

    /// <summary>Identification only (esptool flash_id): never writes; the board restarts and DeskBuddy reconnects.</summary>
    private async Task IdentifyAsync()
    {
        if (_host.TargetPort() is not { } port || Running)
        {
            return;
        }
        var before = _host.Connected;
        if (!_dialogs.ConfirmIdentify(port, before))
        {
            return;
        }
        _cts = new CancellationTokenSource();
        _result.Text = "";
        _bar.Value = 0;
        _progressRow.Visible = true;
        _step.Text = "Identifying the board (read-only)";
        RefreshState();
        _log($"Firmware: identifying the board on {port} (read-only)");
        IdentifyResult result;
        try
        {
            result = await _host.IdentifyAsync(port, _manual.Checked ? EntryMode.Manual : EntryMode.AutoReset, before,
                async (text, _) => await _dialogs.ManualBootAsync(text), _cts.Token);
        }
        finally
        {
            _cts.Dispose();
            _cts = null;
        }
        _bar.Value = 100;
        _step.Text = result.Succeeded ? "Identified" : "Stopped";
        _result.ForeColor = result.Succeeded ? Theme.Green : Theme.Amber;
        _result.Text = result.Succeeded ? result.Message : $"{result.Message}\n{result.Recovery}";
        _log($"Firmware: identify {(result.Succeeded ? "ok" : result.Failure.ToString())}: {result.Message} {result.Recovery}".Trim());
        RefreshState();
    }

    private void OnProgress(InstallProgress p)
    {
        _currentStep = p.Step;
        _bar.Value = Math.Clamp(p.Percent, 0, 100);
        _step.Text = $"{Step(p.Step)}: {p.Text}";
    }

    private static string Step(InstallStep s) => s switch
    {
        InstallStep.Preparing => "1/7 Preparing",
        InstallStep.Identifying => "2/7 Identifying",
        InstallStep.BackingUp => "3/7 Backup",
        InstallStep.CheckingBootloader => "4/7 Bootloader check",
        InstallStep.Writing => "5/7 Writing",
        InstallStep.Verifying => "6/7 Verifying",
        InstallStep.Reconnecting => "7/7 Reconnecting",
        _ => "Done",
    };

    public void ShowResult(InstallResult r)
    {
        _result.ForeColor = r.Succeeded ? Theme.Green : r.FlashChanged ? Theme.Red : Theme.Amber;
        _result.Text = r.Succeeded
            ? r.Message + (r.BackupFile is null ? "" : $" Backup: {r.BackupFile}")
            : $"{r.Message}\n{r.Recovery}" + (r.BackupFile is null || r.Recovery.Contains(r.BackupFile, StringComparison.Ordinal) ? "" : $"\nBackup: {r.BackupFile}");
        _step.Text = r.Succeeded ? "Done" : r.Failure == InstallFailure.Cancelled ? "Cancelled" : "Stopped";
        if (r.Succeeded)
        {
            _bar.Value = 100;
        }
        _log($"Firmware: {(r.Succeeded ? "done" : r.Failure.ToString())}: {r.Message} {r.Recovery}".Trim());
    }

    private void Cancel()
    {
        if (_cts is { } cts && _dialogs.ConfirmStop(_currentStep == InstallStep.Writing))
        {
            cts.Cancel();
        }
    }

    private static Label Value() => new() { AutoSize = true, MaximumSize = new Size(900, 0), ForeColor = Theme.Text, Margin = new Padding(0, 6, 0, 4) };

    private static Label Caption(string text) => new() { Text = text, AutoSize = true, ForeColor = Theme.Muted, Margin = new Padding(0, 6, 0, 4) };

    private static CheckBox Check(string text) => new()
    {
        Text = text, AutoSize = true, ForeColor = Theme.Text, BackColor = Theme.Card, Margin = new Padding(14, 8, 0, 0),
    };

    private static string Sentence(string s) => s.Length == 0 ? s : char.ToUpperInvariant(s[0]) + s[1..];
}
