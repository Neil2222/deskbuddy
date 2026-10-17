using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Schedule;

namespace DeskBuddy.Companion.UI;

/// <summary>One line of an <see cref="EntryList"/>: a time column, a title and a detail line; amber when hidden.</summary>
public sealed record EntryLine(string Time, string Title, string Detail, bool Hidden = false, object? Tag = null);

/// <summary>A dark list of entries, two lines each (the period preview, the hidden entries).</summary>
public sealed class EntryList : ListBox
{
    private readonly Theme _theme;

    public EntryList(Theme theme)
    {
        _theme = theme;
        DrawMode = DrawMode.OwnerDrawFixed;
        BorderStyle = BorderStyle.None;
        BackColor = Theme.Input;
        ForeColor = Theme.Text;
        IntegralHeight = false;
        ItemHeight = Row;
        NativeLook.DarkScrollBars(this);
    }

    private const int Row = 42; // logical pixels: two lines

    // A fixed-height owner-drawn list keeps its row height in device pixels: set it for the DPI it is shown at.
    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        ItemHeight = LogicalToDeviceUnits(Row);
    }

    protected override void OnDpiChangedAfterParent(EventArgs e)
    {
        base.OnDpiChangedAfterParent(e);
        ItemHeight = LogicalToDeviceUnits(Row);
    }

    public void Show(IEnumerable<EntryLine> lines)
    {
        BeginUpdate();
        Items.Clear();
        foreach (var line in lines)
        {
            Items.Add(line);
        }
        EndUpdate();
    }

    protected override void OnDrawItem(DrawItemEventArgs e)
    {
        if (e.Index < 0 || e.Index >= Items.Count || Items[e.Index] is not EntryLine line)
        {
            return;
        }
        var selected = (e.State & DrawItemState.Selected) != 0 && SelectionMode != SelectionMode.None;
        using (var back = new SolidBrush(selected ? Theme.Tile : Theme.Input))
        {
            e.Graphics.FillRectangle(back, e.Bounds);
        }
        var x = e.Bounds.X + LogicalToDeviceUnits(8);
        var timeWidth = LogicalToDeviceUnits(52);
        var flags = TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix | TextFormatFlags.VerticalCenter;
        TextRenderer.DrawText(e.Graphics, line.Time, _theme.At(DeviceDpi).Strong, new Rectangle(x, e.Bounds.Y, timeWidth, e.Bounds.Height / 2 + LogicalToDeviceUnits(4)),
                              line.Time == "?" ? Theme.Faint : Theme.Text, flags);
        var textX = x + timeWidth;
        var w = e.Bounds.Right - textX - LogicalToDeviceUnits(8);
        TextRenderer.DrawText(e.Graphics, line.Title, _theme.At(DeviceDpi).Text10, new Rectangle(textX, e.Bounds.Y + LogicalToDeviceUnits(3), w, LogicalToDeviceUnits(20)),
                              line.Hidden ? Theme.Amber : Theme.Text, flags);
        TextRenderer.DrawText(e.Graphics, line.Detail, _theme.At(DeviceDpi).Small, new Rectangle(textX, e.Bounds.Y + LogicalToDeviceUnits(22), w, LogicalToDeviceUnits(18)),
                              Theme.Muted, flags);
        using var pen = new Pen(Theme.CardBorder);
        e.Graphics.DrawLine(pen, e.Bounds.X, e.Bounds.Bottom - 1, e.Bounds.Right, e.Bounds.Bottom - 1);
    }
}

/// <summary>The window's dark look for the History page's dialogs: a text, a body, buttons at the bottom right.</summary>
public abstract class DarkDialog : Form
{
    protected readonly Theme Theme;
    protected readonly FlowLayoutPanel Buttons = new()
    {
        Dock = DockStyle.Bottom, Height = 52, FlowDirection = FlowDirection.RightToLeft, Padding = new Padding(0, 12, 0, 0),
    };

    protected DarkDialog(Theme theme, string title, Size size)
    {
        Theme = theme;
        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        Text = title;
        Icon = BuddyArt.AppIcon();
        BackColor = UI.Theme.Window;
        ForeColor = UI.Theme.Text;
        Font = theme.Text10;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.CenterParent;
        ClientSize = size;
        Padding = new Padding(20, 16, 20, 14);
        Buttons.TabIndex = 100; // after the dialog's own fields (added later, but before it in the tab order)
        Controls.Add(Buttons);
    }

    protected FlatButton AddButton(string text, ButtonLook look, int width)
    {
        var button = new FlatButton(Theme, look) { Text = text, Width = width, Height = 34, Margin = new Padding(8, 0, 0, 0), BackColor = UI.Theme.Window };
        Buttons.Controls.Add(button);
        return button;
    }

    protected Label Text10(string text, int height, Color? color = null) => new()
    {
        Text = text, Dock = DockStyle.Top, Height = height, ForeColor = color ?? UI.Theme.Muted, UseMnemonic = false,
    };
}

/// <summary>A question with a clear answer: "Restore 12 entries" / "Cancel".</summary>
public sealed class ConfirmDialog : DarkDialog
{
    public ConfirmDialog(Theme theme, string title, string text, string ok) : base(theme, title, new Size(460, 190))
    {
        var okButton = AddButton(ok, ButtonLook.Accent, 190);
        var cancel = AddButton("Cancel", ButtonLook.Quiet, 96);
        okButton.Click += (_, _) => DialogResult = DialogResult.OK;
        cancel.Click += (_, _) => DialogResult = DialogResult.Cancel;
        AcceptButton = okButton;
        Load += (_, _) => ActiveControl = okButton;
        CancelButton = cancel;
        var label = Text10(text, 0, UI.Theme.Text);
        label.Dock = DockStyle.Fill;
        Controls.Add(label);
        label.BringToFront();
    }

    public static bool Ask(IWin32Window owner, Theme theme, string title, string text, string ok)
    {
        using var dialog = new ConfirmDialog(theme, title, text, ok);
        return dialog.ShowDialog(owner) == DialogResult.OK;
    }
}

/// <summary>
/// Hiding a period as test data: the day's from and to times, an optional note, and a live preview of exactly what
/// would be hidden (each entry, its linked smoking break, entries with an unknown time by when they arrived). Nothing
/// is written here: OK only returns the period and the entries the user saw.
/// </summary>
public sealed class HidePeriodDialog : DarkDialog
{
    private readonly DateOnly _date;
    private readonly TimeZoneInfo _zone;
    private readonly Func<DateTimeOffset> _nowUtc;
    private readonly Func<DateTimeOffset, DateTimeOffset, IReadOnlyList<HistoryItem>> _affected;
    private readonly TimeBox _from = new() { AccessibleName = "From" };
    private readonly TimeBox _to = new() { AccessibleName = "To" };
    private readonly TextBox _note = new() { BorderStyle = BorderStyle.None, MaxLength = 120, PlaceholderText = "Note (optional), e.g. P6 board test", AccessibleName = "Note" };
    private readonly Label _summary;
    private readonly EntryList _list;
    private readonly FlatButton _hide;

    public HidePeriodDialog(Theme theme, DateOnly date, TimeZoneInfo zone, Func<DateTimeOffset> nowUtc,
                            Func<DateTimeOffset, DateTimeOffset, IReadOnlyList<HistoryItem>> affected, int fromMinute, int toMinute)
        : base(theme, "Hide a period as test data", new Size(560, 560))
    {
        _date = date;
        _zone = zone;
        _nowUtc = nowUtc;
        _affected = affected;
        _hide = AddButton("Hide", ButtonLook.Accent, 200);
        var cancel = AddButton("Cancel", ButtonLook.Quiet, 96);
        _hide.Click += (_, _) => DialogResult = DialogResult.OK;
        cancel.Click += (_, _) => DialogResult = DialogResult.Cancel;
        CancelButton = cancel;

        var intro = Text10("Entries in this period are left out of the History and its numbers. Nothing is deleted: " +
                           "restore them any time under Test data → Hidden entries.", 44);
        var row = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 44, WrapContents = false, Padding = new Padding(0, 4, 0, 0) };
        row.Controls.Add(new Label { Text = $"On {date.ToString("dddd d MMMM", System.Globalization.CultureInfo.InvariantCulture)} from", AutoSize = true, Margin = new Padding(0, 9, 8, 0), ForeColor = UI.Theme.Text });
        foreach (var box in new[] { _from, _to })
        {
            box.Font = theme.Text10;
            box.ForeColor = UI.Theme.Text;
            box.BackColor = UI.Theme.Input;
        }
        row.Controls.Add(new InputFrame(_from) { Width = 74, Height = 34, Padding = new Padding(8, 8, 8, 4), Margin = new Padding(0, 0, 8, 0) });
        row.Controls.Add(new Label { Text = "to", AutoSize = true, Margin = new Padding(0, 9, 8, 0), ForeColor = UI.Theme.Text });
        row.Controls.Add(new InputFrame(_to) { Width = 74, Height = 34, Padding = new Padding(8, 8, 8, 4), Margin = new Padding(0) });
        _note.Font = theme.Text10;
        _note.ForeColor = UI.Theme.Text;
        _note.BackColor = UI.Theme.Input;
        var note = new InputFrame(_note) { Dock = DockStyle.Top, Height = 36, Padding = new Padding(10, 9, 8, 5) };
        _summary = Text10("", 62, UI.Theme.Text); // two lines: a long summary wraps
        _summary.Font = theme.Strong;
        _summary.Padding = new Padding(0, 12, 0, 0);
        _list = new EntryList(theme) { Dock = DockStyle.Fill, SelectionMode = SelectionMode.None, TabStop = false }; // a preview: nothing to pick
        row.TabIndex = 0; // the times, then the note, then the buttons
        note.TabIndex = 1;

        Controls.Add(_list);
        Controls.Add(_summary);
        Controls.Add(new Panel { Dock = DockStyle.Top, Height = 8 });
        Controls.Add(note);
        Controls.Add(row);
        Controls.Add(intro);
        _list.BringToFront();

        _from.Value = fromMinute;
        _to.Value = toMinute;
        _from.Committed += _ => UpdatePreview();
        _to.Committed += _ => UpdatePreview();
        _from.TextChanged += (_, _) => UpdatePreview();
        _to.TextChanged += (_, _) => UpdatePreview();
        UpdatePreview();
        Load += (_, _) => ActiveControl = _from; // the keyboard starts on the times (once the dialog has its window)
    }

    public DateTimeOffset FromUtc { get; private set; }
    public DateTimeOffset ToUtc { get; private set; }
    public string? Note => _note.Text.Trim() is { Length: > 0 } n ? n : null;
    /// <summary>The entries the preview showed: hiding goes ahead only if the period still covers exactly these.</summary>
    public IReadOnlyList<HistoryItem> Previewed { get; private set; } = [];
    public string Summary => _summary.Text;
    public bool CanHide => _hide.Enabled;

    /// <summary>Sets the times (as typing them does).</summary>
    public void SetPeriod(int fromMinute, int toMinute)
    {
        _from.Value = fromMinute;
        _to.Value = toMinute;
        UpdatePreview();
    }

    private void UpdatePreview()
    {
        var from = DayTime.Parse(_from.Text);
        var to = DayTime.Parse(_to.Text);
        string? error = from is null || to is null ? "Type the times as 8:30, 08:30 or 0830." : null;
        if (error is null)
        {
            (FromUtc, ToUtc, error) = HistoryJournal.LocalPeriod(_date, from!.Value, to!.Value, _zone, _nowUtc());
        }
        Previewed = error is null ? _affected(FromUtc, ToUtc) : [];
        _list.Show(Previewed.Select(i =>
        {
            var (time, title, detail) = HistoryJournal.PreviewLine(i, _zone);
            return new EntryLine(time, title, detail);
        }));
        _summary.Text = error ?? (Previewed.Count == 0 ? "Nothing to hide in this period." : HistoryJournal.Summary(Previewed));
        _summary.ForeColor = error is null ? UI.Theme.Text : UI.Theme.Amber;
        _hide.Enabled = error is null && Previewed.Count > 0;
        _hide.Text = Previewed.Count == 1 ? "Hide 1 entry" : $"Hide {Previewed.Count} entries";
        AcceptButton = _hide.Enabled ? _hide : null;
    }
}

/// <summary>
/// The hides in force for one source (the board's or the simulator's): each hidden entry or period with what it
/// covers, the note and when it was hidden. Restore withdraws the selected one (a period: after saying how many entries
/// come back).
/// </summary>
public sealed class HiddenEntriesDialog : DarkDialog
{
    private readonly Func<HistoryState> _state;
    private readonly TimeZoneInfo _zone;
    private readonly Func<Annotation, Task<string?>> _restore;
    private readonly EntryList _list;
    private readonly FlatButton _restoreButton;
    private readonly Label _status;

    public HiddenEntriesDialog(Theme theme, string sourceName, Func<HistoryState> state, TimeZoneInfo zone, Func<Annotation, Task<string?>> restore)
        : base(theme, $"Hidden entries ({sourceName})", new Size(600, 520))
    {
        _state = state;
        _zone = zone;
        _restore = restore;
        var close = AddButton("Close", ButtonLook.Quiet, 96);
        _restoreButton = AddButton("Restore", ButtonLook.Accent, 200);
        close.Click += (_, _) => Close();
        CancelButton = close;
        _status = Text10("", 26);
        _list = new EntryList(theme) { Dock = DockStyle.Fill };
        Controls.Add(_list);
        Controls.Add(_status);
        Controls.Add(Text10("Hidden entries are kept, never deleted: left out of the History and its numbers until restored.", 44));
        _list.BringToFront();
        _list.SelectedIndexChanged += (_, _) => UpdateButton();
        _restoreButton.Click += async (_, _) => await RestoreSelected();
        Fill();
        Load += (_, _) => ActiveControl = _list; // Up / Down choose a hide, Tab reaches Restore
    }

    public IReadOnlyList<EntryLine> Lines => _list.Items.Cast<EntryLine>().ToList();

    public void Fill()
    {
        var state = _state();
        var keep = _list.SelectedIndex;
        _list.Show(state.Hides.Hides.Select(h => Line(h, state)).OrderBy(l => ((Annotation)l.Tag!).AtUtc));
        if (_list.Items.Count > 0)
        {
            _list.SelectedIndex = Math.Clamp(keep, 0, _list.Items.Count - 1);
        }
        _status.Text = _list.Items.Count == 0 ? "Nothing is hidden." : $"{_list.Items.Count} {(_list.Items.Count == 1 ? "hide" : "hides")} in force.";
        UpdateButton();
    }

    private EntryLine Line(Annotation h, HistoryState state)
    {
        var when = $"hidden {TimeZoneInfo.ConvertTime(h.AtUtc, _zone).ToString("ddd d MMM HH:mm", System.Globalization.CultureInfo.InvariantCulture)}" +
                   (h.Note is { Length: > 0 } n ? $" · {n}" : "");
        if (h.IsPeriod)
        {
            var covered = HistoryView.Covered(state.Items, h);
            return new EntryLine("—", $"Period {HistoryJournal.Period(h, _zone)}", $"{HistoryJournal.Summary(covered)} · {when}", true, h);
        }
        var id = h.Item!;
        var smoke = id.EndsWith("/smoke", StringComparison.Ordinal);
        var item = state.Items.FirstOrDefault(i => i.Id == (smoke ? id[..^"/smoke".Length] : id) || i.Interaction?.Id == id || i.Occurrence?.Id == id);
        if (item is null)
        {
            return new EntryLine("?", "An entry not in this history", $"{id} · {when}", true, h);
        }
        var (time, title, _) = HistoryJournal.PreviewLine(item, _zone);
        var day = (item.AtUtc ?? item.ReceivedUtc) is { } t
            ? TimeZoneInfo.ConvertTime(t, _zone).ToString("ddd d MMM", System.Globalization.CultureInfo.InvariantCulture) + " · " : "";
        return new EntryLine(time, smoke ? $"Smoking break of {title}" : title, $"{day}{when}", true, h);
    }

    private void UpdateButton()
    {
        var line = _list.SelectedItem as EntryLine;
        _restoreButton.Enabled = line is not null;
        _restoreButton.Text = line?.Tag is Annotation { IsPeriod: true } p
            ? $"Restore {HistoryView.Covered(_state().Items, p).Count} entries"
            : "Restore";
    }

    private async Task RestoreSelected()
    {
        if (_list.SelectedItem is not EntryLine { Tag: Annotation hide })
        {
            return;
        }
        if (hide.IsPeriod)
        {
            var covered = HistoryView.Covered(_state().Items, hide);
            if (!ConfirmDialog.Ask(this, Theme, "Restore a period",
                    $"Restore the period {HistoryJournal.Period(hide, _zone)}?\n\n{HistoryJournal.Summary(covered)} come back into the History and its numbers " +
                    "(those also hidden on their own stay hidden).", $"Restore {covered.Count} entries"))
            {
                return;
            }
        }
        _restoreButton.Enabled = false;
        var error = await _restore(hide);
        Fill(); // from the state as it is now: what the file holds, whether or not the write went through
        _status.Text = error ?? $"Restored. {_status.Text}";
        _status.ForeColor = error is null ? UI.Theme.Muted : UI.Theme.Amber;
    }
}
