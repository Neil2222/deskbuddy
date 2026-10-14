using DeskBuddy.Companion.Device;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Companion.UI;

namespace DeskBuddy.Companion;

/// <summary>
/// The DeskBuddy Control Center: Buddy's face and the connection status on top, activities, the LED-sign message
/// box, and diagnostics (the protocol and device log) folded away at the bottom.
/// </summary>
public sealed class MainForm : Form
{
    private const int MaxLogLines = 3000;
    private const int MaxHistory = 10;
    private const int DiagnosticsHeight = 230; // logical pixels of log when unfolded
    private const int RecentRows = 3, HistoryRow = 24; // recent messages shown before the list scrolls; row height

    private readonly Theme _theme = new();
    private readonly BuddyArt _art = new();
    private readonly CompanionService _service; // created in the constructor: needs the form's UI context
    private readonly ToolTip _tips = new();
    private readonly System.Windows.Forms.Timer _reactionTimer = new() { Interval = 1800 };
    private string? _reaction; // a face shown for a moment after something happened

    // Header.
    private readonly Control _header;
    private readonly FaceView _face = new() { Width = 64, Dock = DockStyle.Left };
    private readonly FlatButton _status;
    private readonly FlatButton _settings;

    // Connection settings (folded away).
    private readonly Card _connectionCard;
    private readonly Panel _connectionGap = Gap();
    private readonly Picker _ports;
    private readonly FlatButton _refresh, _connect, _disconnect, _syncClock;
    private readonly Label _detailFirmware = new(), _detailClock = new();

    // Activities.
    private readonly Card _activitiesCard;
    private readonly Panel _activitiesGap = Gap();
    private readonly FlowLayoutPanel _activityFlow = new() { Dock = DockStyle.Fill, WrapContents = true };
    private readonly List<(Panel Group, FlowLayoutPanel Tiles, int Count)> _activityGroups = [];
    private readonly List<(FlatButton Tile, Activity Activity)> _tiles = [];

    // Messages. The recent list takes room only once something was sent.
    private readonly Card _messagesCard;
    private readonly TextBox _message = new() { BorderStyle = BorderStyle.None, MaxLength = 400 };
    private readonly Label _counter = new() { TextAlign = ContentAlignment.MiddleRight, Width = 64 };
    private readonly FlatButton _send;
    private readonly Panel _inputRow = new() { Dock = DockStyle.Top, Height = 46 };
    private readonly Label _hint = new()
    {
        Text = "Enter sends · up to 100 characters · accents show plain, emoji as a box",
        Dock = DockStyle.Top, Height = 26, ForeColor = Theme.Faint, TextAlign = ContentAlignment.MiddleLeft,
    };
    private readonly Label _recentCaption = new()
    {
        Text = "Recent · double-click to reuse", Dock = DockStyle.Top, Height = 24, ForeColor = Theme.Muted,
        TextAlign = ContentAlignment.MiddleLeft, Visible = false,
    };
    private readonly HistoryList _history;
    private readonly InputFrame _recent;
    private bool _sending;

    // Diagnostics.
    private readonly Panel _diagnostics = new() { Dock = DockStyle.Bottom };
    private readonly Panel _diagnosticsBody = new() { Dock = DockStyle.Fill, Visible = false };
    private readonly FlatButton _diagnosticsToggle, _clearLog;
    private readonly CheckBox _showDevice = new() { Text = "Device log", Checked = true, AutoSize = true, FlatStyle = FlatStyle.Flat };
    private readonly TextBox _log = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, WordWrap = false, BorderStyle = BorderStyle.None,
    };

    public MainForm()
    {
        SuspendLayout();
        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        _service = new CompanionService();
        Text = "DeskBuddy";
        Icon = BuddyArt.AppIcon();
        BackColor = Theme.Window;
        ForeColor = Theme.Text;
        Font = _theme.Text10;
        Padding = new Padding(16, 12, 16, 12);
        ClientSize = new Size(760, 460); // the height is fitted to the content on load
        MinimumSize = new Size(620, 400);

        _status = new FlatButton(_theme, ButtonLook.Pill) { Size = new Size(220, 34), Anchor = AnchorStyles.Right };
        _settings = new FlatButton(_theme, ButtonLook.Quiet)
        {
            Glyph = Theme.GlyphSettings, Size = new Size(36, 34), Anchor = AnchorStyles.Right, AccessibleName = "Connection settings",
        };
        _ports = new Picker(_theme) { Width = 260, Height = 32, Margin = new Padding(0, 0, 10, 0), AccessibleName = "Port" };
        _refresh = Quiet("Refresh", Theme.GlyphRefresh, 100);
        _connect = Quiet("Connect", null, 92);
        _disconnect = Quiet("Disconnect", null, 104);
        _syncClock = Quiet("Sync clock", Theme.GlyphSync, 118);
        _send = new FlatButton(_theme, ButtonLook.Accent) { Text = "Send", Glyph = Theme.GlyphSend, Dock = DockStyle.Right, Width = 120 };
        _history = new HistoryList(_theme) { Dock = DockStyle.Fill };
        _recent = new InputFrame(_history) { Dock = DockStyle.Top, Padding = new Padding(4), Visible = false };
        _diagnosticsToggle = new FlatButton(_theme, ButtonLook.Quiet)
        {
            Text = "Diagnostics", Glyph = Theme.GlyphChevronRight, Dock = DockStyle.Left, Width = 140,
        };
        _clearLog = Quiet("Clear", null, 72);

        _connectionCard = new Card(_theme) { Title = "Connection", Dock = DockStyle.Top, Height = 156, Visible = false };
        _connectionGap.Visible = false;
        _activitiesCard = new Card(_theme) { Title = "Activities", Dock = DockStyle.Top, Height = 160 };
        _messagesCard = new Card(_theme) { Title = "Message on Buddy's LED sign", Accent = Theme.Amber, Dock = DockStyle.Top, Height = 124 };

        BuildConnection();
        BuildActivities();
        BuildMessages();
        BuildDiagnostics();

        // Top to bottom; Dock=Top stacks in reverse order of adding.
        var root = new Panel { Dock = DockStyle.Fill, AutoScroll = true };
        root.Controls.Add(_messagesCard);
        root.Controls.Add(_activitiesGap);
        root.Controls.Add(_activitiesCard);
        root.Controls.Add(_connectionGap);
        root.Controls.Add(_connectionCard);
        _header = BuildHeader();
        root.Controls.Add(_header);
        NativeLook.DarkScrollBars(root);
        Controls.Add(root);
        Controls.Add(_diagnostics);

        _service.Changed += UpdateState;
        _service.Log += AppendLog;
        _reactionTimer.Tick += (_, _) =>
        {
            _reactionTimer.Stop();
            _reaction = null;
            UpdateState();
        };
        RefreshPorts();
        ResumeLayout(true);
        UpdateState();
        Load += (_, _) =>
        {
            ReflowActivities();
            LayoutMessages();
            FitToContent();
        };
        Shown += (_, _) =>
        {
            _message.Focus();
            _service.Start();
        };
    }

    private FlatButton Quiet(string text, string? glyph, int width) =>
        new(_theme, ButtonLook.Quiet) { Text = text, Glyph = glyph, Width = width, Height = 32, Margin = new Padding(0, 0, 6, 0) };

    private static Panel Gap(int height = 12) => new() { Dock = DockStyle.Top, Height = height };

    private Control BuildHeader()
    {
        var header = new Panel { Dock = DockStyle.Top, Height = 72, Padding = new Padding(0, 4, 0, 12) };
        var title = new Label { Text = "DeskBuddy", Font = _theme.Title, ForeColor = Theme.Text, AutoSize = true, Margin = new Padding(0) };
        var subtitle = new Label { Text = "Control Center", Font = _theme.Subtitle, ForeColor = Theme.Muted, AutoSize = true, Margin = new Padding(1, 0, 0, 0) };
        var titles = new FlowLayoutPanel
        {
            FlowDirection = FlowDirection.TopDown, WrapContents = false, Dock = DockStyle.Fill, Padding = new Padding(10, 3, 0, 0),
        };
        titles.Controls.Add(title);
        titles.Controls.Add(subtitle);
        var right = new FlowLayoutPanel
        {
            FlowDirection = FlowDirection.RightToLeft, WrapContents = false, Dock = DockStyle.Right, Width = 280, Padding = new Padding(0, 10, 0, 0),
        };
        _settings.Margin = new Padding(6, 0, 0, 0);
        right.Controls.Add(_settings);
        right.Controls.Add(_status);
        header.Controls.Add(titles);
        header.Controls.Add(right);
        header.Controls.Add(_face);

        _status.Click += (_, _) => ToggleConnection();
        _settings.Click += (_, _) => ToggleConnection();
        _tips.SetToolTip(_settings, "Connection settings and device details");
        return header;
    }

    private void BuildConnection()
    {
        foreach (var label in new[] { _detailFirmware, _detailClock })
        {
            label.AutoSize = true;
            label.ForeColor = Theme.Text;
            label.Anchor = AnchorStyles.Left; // vertically centred in its row, level with the caption
        }

        var portRow = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 40, WrapContents = false };
        portRow.Controls.Add(Caption("Port", 64));
        portRow.Controls.AddRange([_ports, _refresh, _connect, _disconnect]);

        // The connection status itself is the pill in the header (its tooltip has the details).
        var details = new TableLayoutPanel { Dock = DockStyle.Top, Height = 64, ColumnCount = 3, RowCount = 2, Padding = new Padding(0, 4, 0, 0) };
        details.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 64));
        details.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        details.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        details.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        details.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        string[] captions = ["Device", "Clock"];
        Label[] values = [_detailFirmware, _detailClock];
        for (var i = 0; i < 2; i++)
        {
            var caption = Caption(captions[i], 64);
            caption.Anchor = AnchorStyles.Left;
            details.Controls.Add(caption, 0, i);
            details.Controls.Add(values[i], 1, i);
        }
        _syncClock.Anchor = AnchorStyles.Right;
        details.Controls.Add(_syncClock, 2, 1);
        _connectionCard.Controls.Add(details);
        _connectionCard.Controls.Add(portRow);

        _refresh.Click += (_, _) => RefreshPorts();
        _connect.Click += (_, _) =>
        {
            _service.SelectedPort = (_ports.SelectedItem as PortInfo)?.Name;
            _service.Connect();
        };
        _disconnect.Click += (_, _) => _service.Disconnect();
        _syncClock.Click += async (_, _) => await _service.SyncClockAsync();
        _tips.SetToolTip(_ports, "Auto-detect finds DeskBuddy's UART port (CH343) by itself");
    }

    private Label Caption(string text, int width) => new()
    {
        Text = text, Width = width, ForeColor = Theme.Muted, Font = _theme.Small, TextAlign = ContentAlignment.MiddleLeft,
        Margin = new Padding(0, 3, 0, 3), Height = 28,
    };

    private const int TileWidth = 96, TileHeight = 80, TileGap = 8, GroupGap = 16, GroupCaption = 22;

    // One group per kind (Mode, Actions), each a caption over its tiles. Groups sit side by side and wrap.
    private void BuildActivities()
    {
        foreach (var kind in new[] { ActivityKind.Mode, ActivityKind.Action })
        {
            var group = new Panel { Margin = new Padding(0, 0, GroupGap, 0) };
            var caption = Caption(kind == ActivityKind.Mode ? "Mode" : "Actions", 120);
            caption.Dock = DockStyle.Top;
            caption.Height = GroupCaption;
            caption.TextAlign = ContentAlignment.TopLeft;
            var tiles = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = true };
            var count = 0;
            foreach (var activity in Activities.All.Where(a => a.Kind == kind))
            {
                var tile = new FlatButton(_theme, ButtonLook.Tile)
                {
                    Text = activity.Label, Icon = _art[activity.Icon], Size = new Size(TileWidth, TileHeight),
                    Margin = new Padding(0, 0, TileGap, TileGap), AccessibleName = activity.Label,
                };
                tile.Click += async (_, _) => await RunActivityAsync(tile, activity);
                _tips.SetToolTip(tile, kind == ActivityKind.Mode ? $"Switch Buddy to {activity.Label} mode" : activity.Label);
                _tiles.Add((tile, activity));
                tiles.Controls.Add(tile);
                count++;
            }
            group.Controls.Add(tiles);
            group.Controls.Add(caption);
            _activityFlow.Controls.Add(group);
            _activityGroups.Add((group, tiles, count));
        }
        _activitiesCard.Controls.Add(_activityFlow);
        _activitiesCard.Resize += (_, _) => ReflowActivities();
    }

    // Each group is as wide as its tiles allow; groups and tiles wrap onto more lines as activities are added or the
    // window narrows, and the card grows to fit.
    private void ReflowActivities()
    {
        var inner = _activitiesCard.ClientSize.Width - _activitiesCard.Padding.Horizontal;
        int tileW = LogicalToDeviceUnits(TileWidth + TileGap), tileH = LogicalToDeviceUnits(TileHeight + TileGap);
        foreach (var (group, _, count) in _activityGroups)
        {
            var cols = Math.Clamp(inner / tileW, 1, Math.Max(1, count));
            var rows = (count + cols - 1) / cols;
            group.Size = new Size(cols * tileW, LogicalToDeviceUnits(GroupCaption) + rows * tileH);
        }
        var height = _activityFlow.GetPreferredSize(new Size(Math.Max(1, inner), 0)).Height;
        var total = _activitiesCard.Padding.Vertical + height - LogicalToDeviceUnits(TileGap);
        if (_activitiesCard.Height != total)
        {
            _activitiesCard.Height = total;
        }
    }

    private void BuildMessages()
    {
        _message.Font = _theme.Input10;
        _message.ForeColor = Theme.Text;
        _message.PlaceholderText = "Type a message for Buddy's LED sign…";
        _counter.Font = _theme.Small;
        _hint.Font = _theme.Small;
        _recentCaption.Font = _theme.Small;
        var input = new InputFrame(_message, _counter) { Dock = DockStyle.Fill };
        _inputRow.Controls.Add(input);
        _inputRow.Controls.Add(new Panel { Dock = DockStyle.Right, Width = 10 });
        _inputRow.Controls.Add(_send);

        _messagesCard.Controls.Add(_recent);
        _messagesCard.Controls.Add(_recentCaption);
        _messagesCard.Controls.Add(_hint);
        _messagesCard.Controls.Add(_inputRow);
        NativeLook.DarkScrollBars(_history);

        _message.TextChanged += (_, _) => UpdateState();
        _message.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Enter)
            {
                e.SuppressKeyPress = true; // no beep
                if (_send.Enabled)
                {
                    _ = SendMessageAsync();
                }
            }
        };
        _send.Click += (_, _) => _ = SendMessageAsync();
        _history.DoubleClick += (_, _) => ReuseSelected();
        _history.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Enter)
            {
                e.SuppressKeyPress = true;
                ReuseSelected();
            }
        };
    }

    private void ReuseSelected()
    {
        if (_history.SelectedItem is string text)
        {
            _message.Text = text;
            _message.Focus();
            _message.SelectAll();
        }
    }

    // Sizes the message card to its content: the recent list appears with the first message and shows up to
    // RecentRows lines, then scrolls. Returns how much the card grew.
    private int LayoutMessages()
    {
        var count = _history.Items.Count;
        _history.ItemHeight = LogicalToDeviceUnits(HistoryRow);
        _recent.Height = Math.Min(count, RecentRows) * _history.ItemHeight + _recent.Padding.Vertical;
        _recentCaption.Visible = _recent.Visible = count > 0;
        var height = _messagesCard.Padding.Vertical + _inputRow.Height + _hint.Height +
                     (count > 0 ? _recentCaption.Height + _recent.Height : 0);
        var grew = height - _messagesCard.Height;
        _messagesCard.Height = height;
        return grew;
    }

    // At start the window is exactly as tall as its sections (connection and diagnostics folded away).
    private void FitToContent()
    {
        var content = _header.Height + _activitiesCard.Height + _activitiesGap.Height + _messagesCard.Height;
        ClientSize = new Size(ClientSize.Width, Padding.Vertical + content + _diagnostics.Height);
    }

    // Opening a section or growing the recent list resizes the window, so the other sections keep their size.
    private void GrowWindow(int delta)
    {
        if (WindowState == FormWindowState.Normal && delta != 0)
        {
            Height += delta;
        }
    }

    private void BuildDiagnostics()
    {
        var bar = new Panel { Dock = DockStyle.Top, Height = 40, Padding = new Padding(0, 4, 0, 4) };
        _showDevice.ForeColor = Theme.Muted;
        _showDevice.Font = _theme.Small;
        _showDevice.Dock = DockStyle.Right;
        _showDevice.Padding = new Padding(0, 0, 10, 0);
        _showDevice.Visible = false;
        _clearLog.Dock = DockStyle.Right;
        _clearLog.Visible = false;
        bar.Controls.Add(_diagnosticsToggle);
        bar.Controls.Add(_showDevice);
        bar.Controls.Add(_clearLog);

        _log.Font = _theme.Mono;
        _log.ForeColor = Theme.Muted;
        var logFrame = new InputFrame(_log) { Dock = DockStyle.Fill, Padding = new Padding(10, 8, 4, 8) };
        _diagnosticsBody.Controls.Add(logFrame);
        NativeLook.DarkScrollBars(_log);
        _diagnostics.Controls.Add(_diagnosticsBody);
        _diagnostics.Controls.Add(bar);
        _diagnostics.Height = 40;

        _diagnosticsToggle.Click += (_, _) => ToggleDiagnostics();
        _clearLog.Click += (_, _) => _log.Clear();
        _tips.SetToolTip(_diagnosticsToggle, "Commands, replies and DeskBuddy's own log output");
    }

    private void ToggleDiagnostics()
    {
        var open = !_diagnosticsBody.Visible;
        var extra = LogicalToDeviceUnits(DiagnosticsHeight);
        SuspendLayout();
        _diagnosticsBody.Visible = open;
        _showDevice.Visible = _clearLog.Visible = open;
        _diagnosticsToggle.Glyph = open ? Theme.GlyphChevronDown : Theme.GlyphChevronRight;
        _diagnostics.Height = LogicalToDeviceUnits(40) + (open ? extra : 0);
        GrowWindow(open ? extra : -extra);
        ResumeLayout(true);
        _diagnosticsToggle.Invalidate();
        if (open)
        {
            _log.SelectionStart = _log.TextLength;
            _log.ScrollToCaret();
        }
    }

    private void ToggleConnection()
    {
        var open = !_connectionCard.Visible;
        var extra = _connectionCard.Height + _connectionGap.Height;
        SuspendLayout();
        _connectionCard.Visible = _connectionGap.Visible = open;
        GrowWindow(open ? extra : -extra);
        ResumeLayout(true);
        if (open)
        {
            RefreshPorts();
        }
    }

    private async Task RunActivityAsync(FlatButton tile, Activity activity)
    {
        tile.Enabled = false;
        try
        {
            if (await _service.RunAsync(activity.Label, activity.Command) && activity.Reaction is { } reaction)
            {
                React(reaction);
            }
        }
        finally
        {
            UpdateState();
        }
    }

    private void React(string face)
    {
        _reaction = face;
        _reactionTimer.Stop();
        _reactionTimer.Start();
        UpdateState();
    }

    private void RefreshPorts()
    {
        var current = (_ports.SelectedItem as PortInfo)?.Name ?? _service.SelectedPort;
        _ports.Items.Clear();
        _ports.Items.Add("Auto-detect DeskBuddy");
        foreach (var port in PortFinder.List())
        {
            _ports.Items.Add(port);
        }
        _ports.SelectedIndex = 0;
        for (var i = 1; i < _ports.Items.Count; i++)
        {
            if (_ports.Items[i] is PortInfo p && p.Name == current)
            {
                _ports.SelectedIndex = i;
            }
        }
    }

    private (string Text, Color Dot) StatusSummary()
    {
        if (_service.IsConnected)
        {
            return ($"Connected · {_service.PortName}", Theme.Green);
        }
        var text = _service.ConnectionText;
        if (text.StartsWith("Paused", StringComparison.Ordinal))
        {
            return ("Paused while flashing", Theme.Amber);
        }
        if (text.StartsWith("Connecting", StringComparison.Ordinal) || text.StartsWith("Resuming", StringComparison.Ordinal))
        {
            return ("Connecting…", Theme.Amber);
        }
        if (text.StartsWith("Waiting", StringComparison.Ordinal))
        {
            return ("Not plugged in", Theme.Faint);
        }
        return (_service.AutoConnect ? "Looking for DeskBuddy…" : "Disconnected", _service.AutoConnect ? Theme.Amber : Theme.Faint);
    }

    private void UpdateState()
    {
        var connected = _service.IsConnected;
        var (status, dot) = StatusSummary();
        _status.Text = status;
        _status.DotColor = dot;
        _status.Invalidate();
        _tips.SetToolTip(_status, _service.ConnectionText);
        _face.Face = _art[$"face-{(connected ? _reaction ?? "neutral" : "sleepy")}"];

        _detailFirmware.Text = connected ? $"firmware {_service.Firmware} · protocol {_service.Protocol}" : "–";
        _detailClock.Text = _service.ClockText;
        _connect.Enabled = !connected;
        _disconnect.Enabled = connected || _service.AutoConnect;
        _syncClock.Enabled = connected && _service.Caps.Contains("time");

        foreach (var (tile, activity) in _tiles)
        {
            tile.Enabled = connected && _service.Caps.Contains(activity.Cap);
            tile.Selected = connected && Activities.IsOn(activity, _service.Mode);
        }

        var text = MessageText.Clean(_message.Text);
        var problem = MessageText.Problem(text);
        _counter.Text = $"{MessageText.CountChars(text)} / {MessageText.MaxChars}";
        _counter.ForeColor = problem is not null && text.Length > 0 ? Theme.Red : Theme.Faint;
        _send.Enabled = connected && _service.Caps.Contains("msg") && problem is null && !_sending;
    }

    private async Task SendMessageAsync()
    {
        var text = MessageText.Clean(_message.Text);
        _sending = true;
        UpdateState();
        try
        {
            if (await _service.SendMessageAsync(text))
            {
                _history.Items.Remove(text);
                _history.Items.Insert(0, text);
                while (_history.Items.Count > MaxHistory)
                {
                    _history.Items.RemoveAt(_history.Items.Count - 1);
                }
                GrowWindow(LayoutMessages());
                _message.Clear();
                React("happy");
            }
        }
        finally
        {
            _sending = false;
            UpdateState();
            _message.Focus();
        }
    }

    private void AppendLog(LogKind kind, string text)
    {
        if (kind == LogKind.Device && !_showDevice.Checked)
        {
            return;
        }
        var prefix = kind switch
        {
            LogKind.Sent => "->",
            LogKind.Received => "<-",
            LogKind.Device => "  ",
            LogKind.Warning => "!!",
            _ => "--",
        };
        if (_log.Lines.Length > MaxLogLines)
        {
            _log.Lines = _log.Lines[^(MaxLogLines / 2)..];
        }
        _log.AppendText($"{DateTime.Now:HH:mm:ss} {prefix} {text}{Environment.NewLine}");
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        NativeLook.DarkTitleBar(this);
    }

    protected override void OnDpiChanged(DpiChangedEventArgs e)
    {
        base.OnDpiChanged(e);
        BeginInvoke(ReflowActivities);
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _service.Dispose();
        _reactionTimer.Dispose();
        _tips.Dispose();
        base.OnFormClosed(e);
    }

    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);
        if (disposing)
        {
            Icon?.Dispose();
            _art.Dispose();
            _theme.Dispose();
        }
    }
}
