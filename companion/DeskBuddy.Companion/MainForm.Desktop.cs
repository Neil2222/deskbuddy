using DeskBuddy.Companion.Desktop;
using DeskBuddy.Companion.Pdc;
using DeskBuddy.Companion.UI;

namespace DeskBuddy.Companion;

// Windows integration, both optional and off by default: tray mode (closing the window keeps the companion, and so
// the schedule, running in the notification area; only Quit stops it) and Start with Windows (minimized: in the
// notification area in tray mode, else on the taskbar). Without them, closing the window quits, as it always did.
public sealed partial class MainForm
{
    private const int MaxTrayText = 127; // NotifyIcon.Text's limit
    private const int WindowsCardHeight = 112;

    private readonly PdcStore _desktopSettings = new(PdcStore.DefaultDirectory); // settings.json: trayEnabled, trayNoticeShown
    private readonly Autostart _autostart = Autostart.ForThisProcess();
    private readonly CheckBox _trayToggle = new()
    {
        Text = "Keep running in the notification area when I close the window (Quit from its icon's menu)",
        AutoSize = true, FlatStyle = FlatStyle.Flat, Dock = DockStyle.Top, Height = 30, AccessibleName = "Tray mode",
    };
    private readonly CheckBox _autostartToggle = new()
    {
        Text = "Start with Windows, minimized", AutoSize = true, FlatStyle = FlatStyle.Flat, Dock = DockStyle.Left,
        AccessibleName = "Start with Windows",
    };
    private readonly Label _autostartInfo = new()
    {
        Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true, UseMnemonic = false,
        Padding = new Padding(12, 0, 0, 0),
    };
    private readonly Label _autostartProblem = new()
    {
        Dock = DockStyle.Top, Height = 40, TextAlign = ContentAlignment.TopLeft, UseMnemonic = false, ForeColor = Theme.Amber,
        Visible = false, AccessibleName = "Start with Windows problem",
    };
    private Card _windowsCard = null!;
    private FlatButton _autostartFix = null!;
    private NotifyIcon? _trayIcon;
    private bool _trayEnabled, _trayNoticeShown, _quitting, _updatingDesktop;

    private void InitDesktop(SingleInstance instance, bool minimized)
    {
        (_trayEnabled, _trayNoticeShown) = _desktopSettings.LoadTray();
        instance.OnShowRequested(() =>
        {
            if (IsHandleCreated)
            {
                BeginInvoke(ShowWindow);
            }
        });
        if (minimized)
        {
            WindowState = FormWindowState.Minimized; // started by Windows at sign-in
            if (_trayEnabled)
            {
                Shown += (_, _) => Hide(); // in the notification area only
            }
        }
        Shown += (_, _) =>
        {
            if (_autostart.Status() is { State: not AutostartState.Off } status && _autostart.Problem(status) is { } problem)
            {
                AppendLog(LogKind.Warning, problem);
            }
        };
        Activated += (_, _) => UpdateDesktopControls(); // Task Manager or a new publish may have changed things meanwhile
        _service.Changed += UpdateTrayText;
        UpdateTrayIcon();
        UpdateDesktopControls();
    }

    private Card BuildWindowsCard()
    {
        var card = _windowsCard = new Card(_theme) { Title = "Windows", Dock = DockStyle.Top, Height = WindowsCardHeight };
        foreach (var toggle in new[] { _trayToggle, _autostartToggle })
        {
            toggle.ForeColor = Theme.Text;
        }
        _autostartFix = Quiet("Use this copy", null, 130);
        _autostartFix.Dock = DockStyle.Right;
        var row = new Panel { Dock = DockStyle.Top, Height = 34, Padding = new Padding(0, 2, 0, 2) };
        _autostartInfo.Font = _theme.Small;
        _autostartProblem.Font = _theme.Small;
        row.Controls.Add(_autostartInfo); // fills what the toggle and the button leave
        row.Controls.Add(_autostartToggle);
        row.Controls.Add(_autostartFix);
        Stack(card, _trayToggle, row, _autostartProblem);

        _trayToggle.CheckedChanged += (_, _) =>
        {
            if (_updatingDesktop)
            {
                return;
            }
            _trayEnabled = _trayToggle.Checked;
            _desktopSettings.SaveTray(_trayEnabled, _trayNoticeShown);
            AppendLog(LogKind.Info, _trayEnabled
                ? "Tray mode on: closing the window keeps DeskBuddy running in the notification area"
                : "Tray mode off: closing the window quits DeskBuddy");
            UpdateTrayIcon();
            UpdateDesktopControls();
        };
        _autostartToggle.CheckedChanged += (_, _) =>
        {
            if (!_updatingDesktop)
            {
                ChangeAutostart(_autostartToggle.Checked ? _autostart.Enable : _autostart.Disable);
            }
        };
        _autostartFix.Click += (_, _) => ChangeAutostart(_autostart.Enable);
        _tips.SetToolTip(_trayToggle, "Closing the window then hides it; DeskBuddy, its connection and the schedule keep running. Quit from the icon's menu stops them.");
        _tips.SetToolTip(_autostartToggle, $"Windows starts {_autostart.CurrentExe} at sign-in");
        return card;
    }

    private void ChangeAutostart(Action change)
    {
        try
        {
            change();
        }
        catch (Exception e) when (e is UnauthorizedAccessException or IOException or System.Security.SecurityException or InvalidOperationException)
        {
            AppendLog(LogKind.Warning, $"Start with Windows: {e.Message}");
        }
        var status = _autostart.Status();
        AppendLog(LogKind.Info, status.State == AutostartState.Off ? "Start with Windows: off" : $"Start with Windows: {status.State} ({status.RegisteredExe})");
        UpdateDesktopControls();
    }

    private void UpdateDesktopControls()
    {
        if (_autostartFix is null)
        {
            return; // before the Device page is built
        }
        var status = _autostart.Status();
        _updatingDesktop = true;
        _trayToggle.Checked = _trayEnabled;
        _autostartToggle.Checked = status.State != AutostartState.Off;
        _autostartToggle.Enabled = _autostart.CanRegister || status.State != AutostartState.Off; // a dev build may still remove it
        _updatingDesktop = false;
        _autostartFix.Visible = _autostart.CanRegister && status.State is AutostartState.OtherCopy or AutostartState.Missing or AutostartState.DisabledInWindows;
        // A problem with an entry that exists is shown in full, on a line of its own (and logged at start).
        var problem = status.State == AutostartState.Off ? null : _autostart.Problem(status);
        _autostartProblem.Text = problem ?? "";
        _autostartProblem.Visible = problem is not null;
        _windowsCard.Height = WindowsCardHeight + (problem is null ? 0 : _autostartProblem.Height);
        _autostartInfo.Text = status.State switch
        {
            AutostartState.On => $"Windows starts this copy at sign-in, {(_trayEnabled ? "in the notification area" : "minimized on the taskbar")}.",
            AutostartState.Off => _autostart.Problem(status) ?? "Off: start DeskBuddy yourself.", // a development build says why
            _ => "Not starting this copy: see below.",
        };
        _autostartInfo.ForeColor = problem is null ? Theme.Faint : Theme.Amber;
        _tips.SetToolTip(_autostartInfo, _autostartInfo.Text);
    }

    private void UpdateTrayIcon()
    {
        if (_trayEnabled && _trayIcon is null)
        {
            var menu = new ContextMenuStrip();
            menu.Items.Add("Open DeskBuddy", null, (_, _) => ShowWindow());
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Quit DeskBuddy", null, (_, _) => Quit());
            _trayIcon = new NotifyIcon { Icon = Icon, ContextMenuStrip = menu, Visible = true };
            _trayIcon.MouseClick += (_, e) =>
            {
                if (e.Button == MouseButtons.Left)
                {
                    ShowWindow();
                }
            };
            UpdateTrayText();
        }
        else if (!_trayEnabled && _trayIcon is not null)
        {
            DisposeTrayIcon();
        }
    }

    private void UpdateTrayText()
    {
        if (_trayIcon is { } icon)
        {
            var text = $"DeskBuddy · {_service.ConnectionText}";
            icon.Text = text.Length > MaxTrayText ? text[..MaxTrayText] : text;
        }
    }

    private void ShowWindow()
    {
        Show();
        if (WindowState == FormWindowState.Minimized)
        {
            WindowState = FormWindowState.Normal;
        }
        Activate();
        UpdateDesktopControls(); // Activated is not raised when the window already was the active one
    }

    /// <summary>Stops the companion (and with it the schedule), also in tray mode.</summary>
    private void Quit()
    {
        _quitting = true;
        Close();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (WindowPolicy.HideOnClose(e.CloseReason, _trayEnabled, _quitting))
        {
            e.Cancel = true;
            Hide();
            if (!_trayNoticeShown && _trayIcon is { } icon)
            {
                icon.ShowBalloonTip(8000, "DeskBuddy is still running",
                    "It stays connected and keeps following the schedule. To stop it, right-click this icon and choose Quit DeskBuddy.",
                    ToolTipIcon.Info);
                _trayNoticeShown = true;
                _desktopSettings.SaveTray(_trayEnabled, _trayNoticeShown);
            }
            return;
        }
        if (_firmware.Running && e.CloseReason == CloseReason.UserClosing)
        {
            // Quitting now would stop the flashing tool halfway (its process tree ends with the Companion).
            e.Cancel = true;
            _quitting = false;
            MessageBox.Show(this, "DeskBuddy's firmware is being installed. Wait until it has finished, or cancel it on the Firmware page first.",
                "DeskBuddy", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        base.OnFormClosing(e);
    }

    private void DisposeTrayIcon()
    {
        if (_trayIcon is { } icon)
        {
            _trayIcon = null;
            icon.Visible = false;
            icon.ContextMenuStrip?.Dispose();
            icon.Dispose();
        }
    }
}
