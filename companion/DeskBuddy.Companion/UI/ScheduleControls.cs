using System.Collections.Immutable;
using System.Drawing.Drawing2D;
using DeskBuddy.Companion.Schedule;

namespace DeskBuddy.Companion.UI;

/// <summary>
/// A scrolling panel that stays where it is when a child gets the focus (a plain one jumps to show the child's top
/// left corner: clicking the day's timeline would scroll it back to 00:00).
/// </summary>
public sealed class KeepScrollPanel : Panel
{
    protected override Point ScrollToControl(Control activeControl) => DisplayRectangle.Location;
}


/// <summary>
/// The seven days as chips, Monday first: each with a small bar of its blocks (an overview of the week), today marked
/// with a cyan dot, the chosen one outlined.
/// </summary>
public sealed class WeekStrip : Control
{
    private readonly Theme _theme;
    private WeekSchedule _schedule = WeekSchedule.Empty;
    private int _hover = -1;

    public DayOfWeek Selected { get; private set; } = DateTime.Today.DayOfWeek;
    public DayOfWeek Today { get; set; } = DateTime.Today.DayOfWeek;

    public event Action<DayOfWeek>? DaySelected;

    public WeekStrip(Theme theme)
    {
        _theme = theme;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw, true);
        BackColor = Theme.Window;
        Cursor = Cursors.Hand;
        AccessibleRole = AccessibleRole.PageTabList;
        AccessibleName = "Days";
    }

    public void Show(WeekSchedule schedule, DayOfWeek selected)
    {
        _schedule = schedule;
        Selected = selected;
        Invalidate();
    }

    private int Gap => LogicalToDeviceUnits(6);

    private Rectangle Chip(int i)
    {
        var w = (Width - 6 * Gap) / 7;
        return new Rectangle(i * (w + Gap), 0, w, Height - 1);
    }

    private int HitTest(Point p)
    {
        for (var i = 0; i < 7; i++)
        {
            if (Chip(i).Contains(p))
            {
                return i;
            }
        }
        return -1;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        var hit = HitTest(e.Location);
        if (hit != _hover)
        {
            _hover = hit;
            Invalidate();
        }
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        _hover = -1;
        Invalidate();
        base.OnMouseLeave(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left && HitTest(e.Location) is var i and >= 0)
        {
            Selected = WeekSchedule.Week[i];
            Invalidate();
            DaySelected?.Invoke(Selected);
        }
        base.OnMouseDown(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        for (var i = 0; i < 7; i++)
        {
            var day = WeekSchedule.Week[i];
            var r = Chip(i);
            var selected = day == Selected;
            using (var path = Theme.Rounded(new RectangleF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), LogicalToDeviceUnits(8)))
            {
                using var fill = new SolidBrush(selected ? Theme.Tile : i == _hover ? Theme.TileHover : Theme.Card);
                using var pen = new Pen(selected ? Theme.Cyan : Theme.CardBorder, selected ? LogicalToDeviceUnits(2) : 1);
                g.FillPath(fill, path);
                g.DrawPath(pen, path);
            }
            var name = day.ToString()[..3].ToUpperInvariant();
            var top = new Rectangle(r.X, r.Y + LogicalToDeviceUnits(6), r.Width, LogicalToDeviceUnits(18));
            TextRenderer.DrawText(g, name, _theme.Section, top, selected ? Theme.Text : Theme.Muted,
                                  TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            if (day == Today)
            {
                var size = TextRenderer.MeasureText(name, _theme.Section);
                var d = LogicalToDeviceUnits(5);
                using var dot = new SolidBrush(Theme.Cyan);
                g.FillEllipse(dot, r.X + (r.Width + size.Width) / 2f + LogicalToDeviceUnits(2), top.Y + (top.Height - d) / 2f, d, d);
            }

            // The day in a bar: 00:00 to 24:00, its enabled blocks in their colours.
            var pad = LogicalToDeviceUnits(10);
            var bar = new Rectangle(r.X + pad, r.Bottom - LogicalToDeviceUnits(16), r.Width - 2 * pad, LogicalToDeviceUnits(6));
            using (var track = new SolidBrush(Theme.Input))
            {
                g.FillRectangle(track, bar);
            }
            var blocks = _schedule[day];
            if (blocks.IsEmpty)
            {
                continue;
            }
            foreach (var (block, _) in SchedulePlanner.Layers(blocks).Where(l => l.Block.Enabled))
            {
                var x0 = bar.X + bar.Width * block.Start / (float)DayTime.Minutes;
                var x1 = bar.X + bar.Width * block.End / (float)DayTime.Minutes;
                using var brush = new SolidBrush(block.Mood is not null ? Theme.MoodColor(block.Mood) : block.Plans is not null ? Theme.AmberDim : Theme.Faint);
                g.FillRectangle(brush, x0, bar.Y, Math.Max(1, x1 - x0), bar.Height);
            }
        }
    }
}

/// <summary>
/// One day from 00:00 to 24:00, top to bottom: hour lines, the blocks as layers (a block on top of others is drawn
/// indented), the current time as a cyan line (today only). Click a block to select it, double-click empty time to
/// add one there, Delete to remove the selected one. Sized by its owner (see <see cref="PreferredHeight"/>) inside a
/// scrolling panel.
/// </summary>
public sealed class DayTimeline : Control
{
    private const int HourHeight = 48, LabelWidth = 52, Indent = 24, MinBlockHeight = 24, Margin8 = 8;

    private readonly Theme _theme;
    private readonly Func<string, string> _moodLabel;
    private readonly Func<string, Image?> _moodFace;
    private IReadOnlyList<(ScheduleBlock Block, int Depth)> _layers = [];
    private ImmutableHashSet<Guid> _active = [];
    private int? _now;

    public Guid? SelectedId { get; private set; }

    public event Action<Guid?>? BlockSelected;
    public event Action<int>? CreateRequested; // minute of the day
    public event Action? DeleteRequested;

    public DayTimeline(Theme theme, Func<string, string> moodLabel, Func<string, Image?> moodFace)
    {
        _theme = theme;
        _moodLabel = moodLabel;
        _moodFace = moodFace;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        BackColor = Theme.Card;
        TabStop = true;
        AccessibleRole = AccessibleRole.List;
        AccessibleName = "Day timeline";
    }

    public int PreferredHeight => LogicalToDeviceUnits(HourHeight * 24 + 2 * Margin8);

    /// <param name="blocks">The day's blocks.</param>
    /// <param name="active">Blocks covering the current time (today only; empty otherwise).</param>
    /// <param name="nowMinute">The current minute of the day if the day is today; else null.</param>
    public void Show(ImmutableArray<ScheduleBlock> blocks, Guid? selected, ImmutableHashSet<Guid> active, int? nowMinute)
    {
        _layers = SchedulePlanner.Layers(blocks);
        SelectedId = selected;
        _active = active;
        _now = nowMinute;
        Invalidate();
    }

    /// <summary>Where a minute of the day is drawn (device pixels from the top of the control).</summary>
    public int Y(int minute) => LogicalToDeviceUnits(Margin8) + (int)Math.Round(minute * LogicalToDeviceUnits(HourHeight) / 60.0);

    private int MinuteAt(int y) =>
        Math.Clamp((int)((y - LogicalToDeviceUnits(Margin8)) * 60.0 / LogicalToDeviceUnits(HourHeight)), 0, DayTime.Minutes - 1);

    private Rectangle BlockRect(ScheduleBlock b, int depth)
    {
        var left = LogicalToDeviceUnits(LabelWidth + Margin8);
        var right = Width - LogicalToDeviceUnits(Margin8);
        var x = Math.Min(left + depth * LogicalToDeviceUnits(Indent), left + (right - left) / 2);
        var y0 = Y(b.Start);
        var h = Math.Max(Y(b.End) - y0, LogicalToDeviceUnits(MinBlockHeight));
        return new Rectangle(x, y0 + 1, right - x, h - 2);
    }

    private ScheduleBlock? HitTest(Point p)
    {
        for (var i = _layers.Count - 1; i >= 0; i--) // the top layer first
        {
            if (BlockRect(_layers[i].Block, _layers[i].Depth).Contains(p))
            {
                return _layers[i].Block;
            }
        }
        return null;
    }

    public void Select(Guid? id)
    {
        if (SelectedId != id)
        {
            SelectedId = id;
            Invalidate();
            BlockSelected?.Invoke(id);
        }
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        Focus();
        if (e.Button == MouseButtons.Left)
        {
            Select(HitTest(e.Location)?.Id);
        }
        base.OnMouseDown(e);
    }

    protected override void OnMouseDoubleClick(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left && HitTest(e.Location) is null)
        {
            CreateRequested?.Invoke(MinuteAt(e.Y) / 15 * 15); // on the quarter: a starting point, edited to the minute
        }
        base.OnMouseDoubleClick(e);
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        Cursor = HitTest(e.Location) is null ? Cursors.Default : Cursors.Hand;
        base.OnMouseMove(e);
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Delete || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Delete && SelectedId is not null)
        {
            DeleteRequested?.Invoke();
            e.Handled = true;
        }
        base.OnKeyDown(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        var labelWidth = LogicalToDeviceUnits(LabelWidth);
        var right = Width - LogicalToDeviceUnits(Margin8);

        using (var line = new Pen(Theme.CardBorder))
        using (var half = new Pen(Color.FromArgb(28, 37, 57)) { DashStyle = DashStyle.Dot })
        {
            for (var h = 0; h <= 24; h++)
            {
                var y = Y(h * 60);
                g.DrawLine(line, labelWidth, y, right, y);
                if (h < 24)
                {
                    g.DrawLine(half, labelWidth, Y(h * 60 + 30), right, Y(h * 60 + 30));
                }
                TextRenderer.DrawText(g, DayTime.Format(h * 60), _theme.Small, new Rectangle(0, y - LogicalToDeviceUnits(9), labelWidth - LogicalToDeviceUnits(6), LogicalToDeviceUnits(18)),
                                      Theme.Muted, TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
            }
        }

        g.SmoothingMode = SmoothingMode.AntiAlias;
        foreach (var (block, depth) in _layers)
        {
            DrawBlock(g, block, BlockRect(block, depth));
        }

        if (_layers.Count == 0)
        {
            var hint = new Rectangle(labelWidth, Y(8 * 60), right - labelWidth, LogicalToDeviceUnits(HourHeight));
            TextRenderer.DrawText(g, "No blocks on this day. Double-click a time, or use + New block.", _theme.Text10, hint,
                                  Theme.Faint, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.WordBreak);
        }

        if (_now is { } now)
        {
            var y = Y(now);
            var d = LogicalToDeviceUnits(8);
            using var pen = new Pen(Theme.Cyan, LogicalToDeviceUnits(2));
            using var dot = new SolidBrush(Theme.Cyan);
            g.DrawLine(pen, labelWidth, y, right, y);
            g.FillEllipse(dot, labelWidth - d / 2f, y - d / 2f, d, d);
        }
    }

    private void DrawBlock(Graphics g, ScheduleBlock b, Rectangle r)
    {
        var selected = b.Id == SelectedId;
        // Moods in their colours; amber means a planned activity; a block with neither is neutral.
        var color = !b.Enabled ? Theme.Faint : b.Mood is not null ? Theme.MoodColor(b.Mood) : b.Plans is not null ? Theme.Amber : Theme.Body;
        using (var path = Theme.Rounded(new RectangleF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), LogicalToDeviceUnits(6)))
        {
            // Opaque, so a layer hides the lines and the block below it.
            var fill = b.Enabled && b.Mood is not null ? Blend(Theme.Card, color, 0.22f) : Theme.Card;
            using var brush = new SolidBrush(fill);
            g.FillPath(brush, path);
            using var pen = new Pen(selected ? Theme.Cyan : color, selected ? LogicalToDeviceUnits(2) : 1);
            if (!selected && (!b.Enabled || b.Mood is null))
            {
                pen.DashStyle = DashStyle.Dash; // no mood, or switched off: outlined only
            }
            g.DrawPath(pen, path);
        }

        var pad = LogicalToDeviceUnits(8);
        var x = r.X + pad;
        var face = b.Mood is { } mood && r.Height >= LogicalToDeviceUnits(36) ? _moodFace(mood) : null;
        if (face is not null)
        {
            var box = new Rectangle(x, r.Y + pad / 2, LogicalToDeviceUnits(30), Math.Min(r.Height - pad, LogicalToDeviceUnits(30)));
            BuddyArt.Draw(g, face, box, BuddyArt.Scale(1, DeviceDpi), b.Enabled ? 1f : 0.35f);
            x += box.Width + pad;
        }

        var title = b.Name.Length > 0 ? b.Name : b.Mood is { } m ? _moodLabel(m) : b.Plans is { } p ? ActivityTypes.Label(p) : "Block";
        var detail = $"{DayTime.Format(b.Start)}–{DayTime.Format(b.End)}";
        if (b.Mood is { } moodName && b.Name.Length > 0)
        {
            detail += $" · {_moodLabel(moodName)}";
        }
        if (b.Plans is { } plans)
        {
            detail += $" · plans: {ActivityTypes.Label(plans)}";
        }
        if (!b.Enabled)
        {
            detail += " · off";
        }
        var text = b.Enabled ? Theme.Text : Theme.Faint;
        var flags = TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix | TextFormatFlags.VerticalCenter;
        var line = LogicalToDeviceUnits(18);
        var width = r.Right - x - LogicalToDeviceUnits(20);
        if (r.Height >= LogicalToDeviceUnits(40))
        {
            TextRenderer.DrawText(g, title, _theme.Strong, new Rectangle(x, r.Y + pad / 2, width, line), text, flags);
            TextRenderer.DrawText(g, detail, _theme.Small, new Rectangle(x, r.Y + pad / 2 + line, width, line),
                                  b.Enabled ? Theme.Muted : Theme.Faint, flags);
        }
        else
        {
            TextRenderer.DrawText(g, $"{title} · {detail}", _theme.Small, new Rectangle(x, r.Y, width, r.Height), text, flags);
        }

        if (_active.Contains(b.Id))
        {
            // Lit LED: this block applies right now.
            var d = LogicalToDeviceUnits(6);
            using var dot = new SolidBrush(Theme.Cyan);
            g.FillEllipse(dot, r.Right - pad - d, r.Y + Math.Min(pad, (r.Height - d) / 2), d, d);
        }
    }

    private static Color Blend(Color a, Color b, float t) =>
        Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
}

/// <summary>
/// A time of day, typed: "8:30", "08:30", "0830", "24:00". Up/Down change it by a minute (Shift: 15 minutes);
/// Enter or leaving the field commits it, Escape restores the last committed value.
/// </summary>
public sealed class TimeBox : TextBox
{
    private int _value;

    public int Value
    {
        get => _value;
        set
        {
            _value = value;
            Text = DayTime.Format(value);
        }
    }

    /// <summary>The typed time (a valid one), when it is committed.</summary>
    public event Action<int>? Committed;
    /// <summary>The text is not a time: shown in the field's frame by the owner.</summary>
    public event Action<bool>? ValidityChanged;

    public TimeBox()
    {
        BorderStyle = BorderStyle.None;
        MaxLength = 5;
        TextAlign = HorizontalAlignment.Center;
    }

    protected override bool IsInputKey(Keys keyData) => (keyData & Keys.KeyCode) is Keys.Up or Keys.Down || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        switch (e.KeyCode)
        {
        case Keys.Up or Keys.Down:
        {
            var step = (e.Shift ? 15 : 1) * (e.KeyCode == Keys.Up ? 1 : -1);
            var current = DayTime.Parse(Text) ?? _value;
            Commit(Math.Clamp(current + step, 0, DayTime.Minutes));
            SelectAll();
            e.SuppressKeyPress = true;
            break;
        }
        case Keys.Enter:
            CommitText();
            SelectAll();
            e.SuppressKeyPress = true;
            break;
        case Keys.Escape:
            Value = _value;
            ValidityChanged?.Invoke(true);
            SelectAll();
            e.SuppressKeyPress = true;
            break;
        }
        base.OnKeyDown(e);
    }

    protected override void OnTextChanged(EventArgs e)
    {
        ValidityChanged?.Invoke(DayTime.Parse(Text) is not null);
        base.OnTextChanged(e);
    }

    protected override void OnLeave(EventArgs e)
    {
        CommitText();
        base.OnLeave(e);
    }

    protected override void OnEnter(EventArgs e)
    {
        BeginInvoke(SelectAll);
        base.OnEnter(e);
    }

    private void CommitText()
    {
        if (DayTime.Parse(Text) is { } minute)
        {
            Commit(minute);
        }
    }

    private void Commit(int minute)
    {
        Value = minute;
        Committed?.Invoke(minute);
    }
}
