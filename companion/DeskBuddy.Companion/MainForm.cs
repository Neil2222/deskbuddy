using DeskBuddy.Companion.Device;
using DeskBuddy.Companion.Protocol;

namespace DeskBuddy.Companion;

/// <summary>One window: connection, device state, action buttons, and a log of traffic and device output.</summary>
public sealed class MainForm : Form
{
    private const int MaxLogLines = 3000;
    private const int MaxHistory = 10;

    private readonly CompanionService _service; // created in the constructor: needs the form's UI context
    private readonly ComboBox _ports = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 280 };
    private readonly Button _connect = new() { Text = "Connect", AutoSize = true };
    private readonly Button _disconnect = new() { Text = "Disconnect", AutoSize = true };
    private readonly Label _connection = new() { AutoSize = true };
    private readonly Label _device = new() { AutoSize = true };
    private readonly Label _mode = new() { AutoSize = true, Font = new Font(SystemFonts.MessageBoxFont!, FontStyle.Bold) };
    private readonly Label _clock = new() { AutoSize = true };
    private readonly List<(Button Button, string Cap)> _actions = [];
    private readonly CheckBox _showDevice = new() { Text = "Show device log", Checked = true, AutoSize = true };
    private readonly TextBox _log = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, WordWrap = false, Dock = DockStyle.Fill,
        Font = new Font(FontFamily.GenericMonospace, 9f), BackColor = SystemColors.Window,
    };
    private readonly TextBox _message = new() { Width = 520, MaxLength = 400 };
    private readonly Label _counter = new() { AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(6, 6, 6, 3) };
    private readonly Button _send = new()
    {
        Text = "Send to Buddy", AutoSize = true, MinimumSize = new Size(140, 34),
        Font = new Font(SystemFonts.MessageBoxFont!, FontStyle.Bold),
    };
    private readonly ListBox _history = new() { Width = 700, Height = 92, IntegralHeight = false };

    public MainForm()
    {
        _service = new CompanionService();
        Text = "DeskBuddy Companion";
        ClientSize = new Size(900, 720);
        MinimumSize = new Size(640, 400);
        Font = SystemFonts.MessageBoxFont!;

        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, Padding = new Padding(8) };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        var refresh = new Button { Text = "Refresh", AutoSize = true };
        layout.Controls.Add(Row(new Label { Text = "Port:", AutoSize = true, Anchor = AnchorStyles.Left }, _ports,
                                refresh, _connect, _disconnect));

        var status = new TableLayoutPanel { AutoSize = true, ColumnCount = 2, Dock = DockStyle.Top };
        AddStatus(status, "Connection:", _connection);
        AddStatus(status, "Device:", _device);
        AddStatus(status, "Mode:", _mode);
        AddStatus(status, "Clock:", _clock);
        layout.Controls.Add(status);

        var actions = Row();
        AddAction(actions, "Surprise!", "poke", () => _service.RunAsync("Surprise", "poke"));
        AddAction(actions, "Monster", "monster", () => _service.RunAsync("Monster", "monster"));
        AddAction(actions, "Office Mode", "mode", () => _service.RunAsync("Office Mode", "mode.set mode=office"));
        AddAction(actions, "Buddy Mode", "mode", () => _service.RunAsync("Buddy Mode", "mode.set mode=normal"));
        AddAction(actions, "Sync clock", "time", _service.SyncClockAsync);
        layout.Controls.Add(actions);

        var message = new GroupBox { Text = "Message on Buddy's LED sign", AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(8) };
        var messageRows = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false };
        messageRows.Controls.Add(Row(_message, _counter, _send));
        messageRows.Controls.Add(new Label { Text = "Recent (double-click to reuse):", AutoSize = true });
        messageRows.Controls.Add(_history);
        message.Controls.Add(messageRows);
        layout.Controls.Add(message);

        var clear = new Button { Text = "Clear log", AutoSize = true };
        layout.Controls.Add(Row(_showDevice, clear));
        layout.Controls.Add(_log);
        Controls.Add(layout);

        refresh.Click += (_, _) => RefreshPorts();
        _connect.Click += (_, _) =>
        {
            _service.SelectedPort = (_ports.SelectedItem as PortInfo)?.Name;
            _service.Connect();
        };
        _disconnect.Click += (_, _) => _service.Disconnect();
        clear.Click += (_, _) => _log.Clear();
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
        _history.DoubleClick += (_, _) =>
        {
            if (_history.SelectedItem is string text)
            {
                _message.Text = text;
                _message.Focus();
                _message.SelectAll();
            }
        };
        _service.Changed += UpdateState;
        _service.Log += AppendLog;

        RefreshPorts();
        UpdateState();
        Shown += (_, _) => _service.Start();
    }

    private static FlowLayoutPanel Row(params Control[] controls)
    {
        var row = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, WrapContents = true };
        row.Controls.AddRange(controls);
        return row;
    }

    private static void AddStatus(TableLayoutPanel table, string caption, Label value)
    {
        table.Controls.Add(new Label { Text = caption, AutoSize = true, Margin = new Padding(3, 3, 12, 3) });
        table.Controls.Add(value);
    }

    private void AddAction(FlowLayoutPanel row, string text, string cap, Func<Task> run)
    {
        var button = new Button { Text = text, AutoSize = true, MinimumSize = new Size(110, 36) };
        button.Click += async (_, _) =>
        {
            button.Enabled = false;
            try
            {
                await run();
            }
            finally
            {
                UpdateState();
            }
        };
        _actions.Add((button, cap));
        row.Controls.Add(button);
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

    private void UpdateState()
    {
        _connection.Text = _service.ConnectionText;
        _connection.ForeColor = _service.IsConnected ? Color.DarkGreen : SystemColors.ControlText;
        _device.Text = _service.IsConnected ? $"firmware {_service.Firmware}" : "-";
        _mode.Text = _service.IsConnected ? _service.Mode switch
        {
            "normal" => "Buddy",
            "office" => "Office Mode (clock)",
            "monster" => "Monster time!",
            var m => m,
        } : "-";
        _clock.Text = _service.ClockText;
        foreach (var (button, cap) in _actions)
        {
            button.Enabled = _service.IsConnected && _service.Caps.Contains(cap);
        }
        _connect.Enabled = !_service.IsConnected;
        _disconnect.Enabled = _service.IsConnected || _service.AutoConnect;

        var text = MessageText.Clean(_message.Text);
        var problem = MessageText.Problem(text);
        _counter.Text = $"{MessageText.CountChars(text)} / {MessageText.MaxChars}";
        _counter.ForeColor = problem is not null && text.Length > 0 ? Color.Firebrick : SystemColors.GrayText;
        _send.Enabled = _service.IsConnected && _service.Caps.Contains("msg") && problem is null && !_sending;
    }

    private bool _sending;

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
                _message.Clear();
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

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _service.Dispose();
        base.OnFormClosed(e);
    }
}
