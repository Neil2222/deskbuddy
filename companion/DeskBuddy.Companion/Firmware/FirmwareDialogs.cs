using DeskBuddy.Firmware;
using DeskBuddy.Firmware.Flashing;

namespace DeskBuddy.Companion.Firmware;

/// <summary>The Firmware card's questions as Windows dialogs.</summary>
public sealed class FirmwareDialogs(IWin32Window owner) : IFirmwareDialogs
{
    public string? PickPackage()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Open a DeskBuddy firmware package",
            Filter = "DeskBuddy firmware package (*.zip)|*.zip",
            CheckFileExists = true,
        };
        return dialog.ShowDialog(owner) == DialogResult.OK ? dialog.FileName : null;
    }

    public FirmwareConfirmation? Confirm(FirmwareConfirmRequest request)
    {
        using var form = new FirmwareConfirmForm(request);
        return form.ShowDialog(owner) == DialogResult.OK ? form.Answer : null;
    }

    public bool ConfirmIdentify(string port, DeviceReport? before) => MessageBox.Show(owner,
        $"Identify the board on {port}?\n\nThe board restarts into download mode; esptool reads its chip, PSRAM, flash chip " +
        "and MAC, and restarts it. Nothing is read from the flash and nothing is written or erased." +
        (before is null ? "" : $"\n\nDeskBuddy {before.Firmware} reconnects afterwards (Buddy's mood and Clock face start fresh)."),
        "DeskBuddy firmware: identify", MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) == DialogResult.Yes;

    public bool ConfirmStop(bool writing) => MessageBox.Show(owner,
        writing
            ? "The firmware is being written. If you stop now, the board will not start DeskBuddy until you install again " +
              "(Repair). This is recoverable: the board's ROM bootloader is never written.\n\nStop anyway?"
            : "Stop? Nothing has been written yet.",
        "DeskBuddy firmware", MessageBoxButtons.YesNo, writing ? MessageBoxIcon.Warning : MessageBoxIcon.Question,
        MessageBoxDefaultButton.Button2) == DialogResult.Yes;

    public Task<bool> ManualBootAsync(string instruction) => Task.FromResult(MessageBox.Show(owner,
        $"{instruction}.\n\nThen click OK.", "DeskBuddy firmware: download mode", MessageBoxButtons.OKCancel,
        MessageBoxIcon.Information) == DialogResult.OK);
}

/// <summary>
/// What is about to happen, in full, with the confirmations it needs: the exact model (the chip does not prove it),
/// going without a backup, an older version, and for a Repair the typed word.
/// </summary>
public sealed class FirmwareConfirmForm : Form
{
    private readonly CheckBox? _model, _noBackup, _older;
    private readonly TextBox? _repair;
    private readonly Button _ok;

    public FirmwareConfirmation Answer => new(_model?.Checked ?? true, _repair?.Text == "REPAIR", _noBackup?.Checked ?? false);

    public FirmwareConfirmForm(FirmwareConfirmRequest r)
    {
        var m = r.Package.Manifest;
        Text = r.Operation switch
        {
            FirmwareOperation.Install => $"Install DeskBuddy {m.Version}",
            FirmwareOperation.Update => $"Update DeskBuddy to {m.Version}",
            _ => $"Repair: reinstall DeskBuddy {m.Version}",
        };
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        StartPosition = FormStartPosition.CenterParent;
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Padding = new Padding(16);

        var flow = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, WrapContents = false, MaximumSize = new Size(640, 0) };
        flow.Controls.Add(new Label { AutoSize = true, MaximumSize = new Size(620, 0), Text = Summary(r), Margin = new Padding(0, 0, 0, 10) });
        if (r.NeedsModelConfirmation)
        {
            flow.Controls.Add(new Label
            {
                AutoSize = true, MaximumSize = new Size(620, 0), Margin = new Padding(0, 4, 0, 2),
                Text = "The chip check cannot tell this board apart from other boards with the same ESP32-S3 module: only you can confirm the model.",
            });
            _model = Box($"This board is a {r.Package.Profile.DisplayName}.");
            flow.Controls.Add(_model);
        }
        if (r.Decision.Offer == InstallOffer.Downgrade)
        {
            _older = Box($"Install the older version {m.Version}.");
            flow.Controls.Add(_older);
        }
        if (!r.Backup)
        {
            _noBackup = Box("Go on without a backup of the board's flash.");
            flow.Controls.Add(_noBackup);
        }
        if (r.Operation == FirmwareOperation.Repair)
        {
            flow.Controls.Add(new Label { AutoSize = true, Text = "A Repair rewrites the bootloader and the partition table too. Type REPAIR to confirm:", Margin = new Padding(0, 8, 0, 2) });
            _repair = new TextBox { Width = 160 };
            _repair.TextChanged += (_, _) => Update();
            flow.Controls.Add(_repair);
        }
        _ok = new Button { Text = r.Operation == FirmwareOperation.Update ? "Update" : r.Operation == FirmwareOperation.Install ? "Install" : "Repair", DialogResult = DialogResult.OK, AutoSize = true };
        var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, AutoSize = true };
        var buttons = new FlowLayoutPanel { AutoSize = true, Margin = new Padding(0, 12, 0, 0) };
        buttons.Controls.AddRange([_ok, cancel]);
        flow.Controls.Add(buttons);
        Controls.Add(flow);
        AcceptButton = _ok;
        CancelButton = cancel;
        Update();
    }

    private CheckBox Box(string text)
    {
        var box = new CheckBox { Text = text, AutoSize = true, MaximumSize = new Size(620, 0), Margin = new Padding(0, 4, 0, 4) };
        box.CheckedChanged += (_, _) => Update();
        return box;
    }

    private new void Update() =>
        _ok.Enabled = (_model?.Checked ?? true) && (_older?.Checked ?? true) && (_noBackup?.Checked ?? true) && (_repair is null || _repair.Text == "REPAIR");

    public static string Summary(FirmwareConfirmRequest r)
    {
        var m = r.Package.Manifest;
        var writes = r.Operation == FirmwareOperation.Update
            ? "the app at 0x10000 only, after the board's own bootloader and partition table have been read back and found compatible (otherwise nothing is written)"
            : "the bootloader at 0x0, the partition table at 0x8000 and the app at 0x10000";
        return string.Join("\n\n",
            $"DeskBuddy {m.Version} (build {m.Build[..7]}), signed by {r.Package.SignedBy.Name}. {char.ToUpperInvariant(r.Decision.Reason[0])}{r.Decision.Reason[1..]}.",
            $"Board on {r.Port}" + (r.Before is { } b ? $": DeskBuddy {b.Firmware}, device {b.DeviceId}." : ": no DeskBuddy answers there.") +
            " The board restarts into download mode and is identified again first; it must be an ESP32-S3 with 8 MB PSRAM and 16 MB flash" +
            (r.Before?.DeviceId is { } id ? $" with the MAC of device {id}." : "."),
            $"Written: {writes}. Never written: 0x9000–0xFFFF (the board's NVS and calibration) and everything after the app. Nothing is erased, no eFuse is touched.",
            r.Backup ? "First a full 16 MB backup of the board is read and checked (a few minutes); if that fails, nothing is written."
                     : "No backup is taken.",
            FirmwareCard.DataStatement,
            "Afterwards the flash is verified, the board restarts, and the Companion checks that DeskBuddy reports exactly this version and build. Do not unplug the board while it runs.");
    }
}
