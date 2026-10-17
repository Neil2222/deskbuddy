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
/// add one there, Delete to remove the selected one. Drag a block's body to move it, its top or bottom edge to change
/// its start or end (5-minute steps, Shift: 1 minute); the change is committed on release, Escape cancels it.
/// Sized by its owner (see <see cref="PreferredHeight"/>) inside a scrolling panel, which it scrolls while dragging
/// near its edges.
/// </summary>
public sealed class DayTimeline : Control
{
    private const int HourHeight = 48, LabelWidth = 52, Indent = 24, MinBlockHeight = 24, Margin8 = 8;
    private const int EdgeHeight = 6;      // the grab zone of a block's top and bottom edges (logical pixels)
    private const int AutoScrollZone = 28; // dragging this close to the visible top or bottom scrolls

    private readonly Theme _theme;
    private readonly Func<string, string> _moodLabel;
    private readonly Func<string, Image?> _moodFace;
    private readonly System.Windows.Forms.Timer _autoScroll = new() { Interval = 30 };
    private IReadOnlyList<(ScheduleBlock Block, int Depth)> _layers = [];
    private ImmutableHashSet<Guid> _active = [];
    private int? _now;
    private Guid? _hover;
    private DragState? _drag;
    private int _scrollStep; // device pixels per auto-scroll tick, signed; 0: not scrolling

    private sealed class DragState(ScheduleBlock block, int depth, DragKind kind, Point down)
    {
        public ScheduleBlock Block { get; } = block;
        public int Depth { get; } = depth;
        public DragKind Kind { get; } = kind;
        public Point Down { get; } = down;
        public bool Started { get; set; } // past the threshold: an ordinary click never changes a block
        public int Y { get; set; } = down.Y;
        public int Start { get; set; } = block.Start;
        public int End { get; set; } = block.End;
    }

    public Guid? SelectedId { get; private set; }

    public event Action<Guid?>? BlockSelected;
    public event Action<int>? CreateRequested; // minute of the day
    public event Action? DeleteRequested;
    /// <summary>While dragging: the block's times as they would be (not stored).</summary>
    public event Action<Guid, int, int>? DragPreview;
    /// <summary>The drag ended with new times: store them.</summary>
    public event Action<Guid, int, int>? DragCommitted;
    /// <summary>The drag was cancelled (Escape, or the pointer was taken away): show the stored times again.</summary>
    public event Action? DragCancelled;

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
        _autoScroll.Tick += (_, _) => AutoScrollTick();
    }

    public int PreferredHeight => LogicalToDeviceUnits(HourHeight * 24 + 2 * Margin8);

    public bool Dragging => _drag is { Started: true };

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

    private double MinutesPerPixel => 60.0 / LogicalToDeviceUnits(HourHeight);

    private int MinuteAt(int y) =>
        Math.Clamp((int)((y - LogicalToDeviceUnits(Margin8)) * MinutesPerPixel), 0, DayTime.Minutes - 1);

    private Rectangle BlockRect(int start, int end, int depth)
    {
        var left = LogicalToDeviceUnits(LabelWidth + Margin8);
        var right = Width - LogicalToDeviceUnits(Margin8);
        var x = Math.Min(left + depth * LogicalToDeviceUnits(Indent), left + (right - left) / 2);
        var y0 = Y(start);
        var h = Math.Max(Y(end) - y0, LogicalToDeviceUnits(MinBlockHeight));
        return new Rectangle(x, y0 + 1, right - x, h - 2);
    }

    // The top layer under the pointer (a block on top is found before the one it covers).
    private (ScheduleBlock Block, int Depth, Rectangle Rect)? HitTest(Point p)
    {
        for (var i = _layers.Count - 1; i >= 0; i--)
        {
            var (block, depth) = _layers[i];
            var rect = BlockRect(block.Start, block.End, depth);
            if (rect.Contains(p))
            {
                return (block, depth, rect);
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
            var hit = HitTest(e.Location);
            Select(hit?.Block.Id);
            _drag = hit is { } h
                ? new DragState(h.Block, h.Depth, BlockDrag.Zone(h.Rect.Y, h.Rect.Height, e.Y, LogicalToDeviceUnits(EdgeHeight)), e.Location)
                : null;
        }
        base.OnMouseDown(e);
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        if (_drag is { } drag) // from the left button going down on a block until it comes up (the control has the capture)
        {
            var threshold = SystemInformation.DragSize;
            if (!drag.Started && (Math.Abs(e.Y - drag.Down.Y) >= threshold.Height || Math.Abs(e.X - drag.Down.X) >= threshold.Width))
            {
                drag.Started = true;
                Cursor = drag.Kind == DragKind.Move ? Cursors.SizeAll : Cursors.SizeNS;
            }
            if (drag.Started)
            {
                drag.Y = e.Y;
                UpdateDrag();
                UpdateAutoScroll(e.Y);
            }
        }
        else
        {
            var hit = HitTest(e.Location);
            Cursor = hit is not { } h ? Cursors.Default
                     : BlockDrag.Zone(h.Rect.Y, h.Rect.Height, e.Y, LogicalToDeviceUnits(EdgeHeight)) == DragKind.Move ? Cursors.Hand
                     : Cursors.SizeNS;
            SetHover(hit?.Block.Id);
        }
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        if (!Dragging)
        {
            SetHover(null);
        }
        base.OnMouseLeave(e);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left && _drag is { } drag)
        {
            _drag = null;
            StopAutoScroll();
            if (drag.Started && (drag.Start, drag.End) != (drag.Block.Start, drag.Block.End))
            {
                DragCommitted?.Invoke(drag.Block.Id, drag.Start, drag.End);
            }
            else if (drag.Started)
            {
                DragCancelled?.Invoke(); // back where it was: nothing to store
            }
            Invalidate();
        }
        base.OnMouseUp(e);
    }

    // The pointer was taken away mid-drag (another window, a dialog): nothing is changed.
    protected override void OnMouseCaptureChanged(EventArgs e)
    {
        if (Dragging && !Capture && (MouseButtons & MouseButtons.Left) != 0) // still pressed: not an ordinary release
        {
            CancelDrag();
        }
        base.OnMouseCaptureChanged(e);
    }

    protected override void OnMouseDoubleClick(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left && HitTest(e.Location) is null)
        {
            CreateRequested?.Invoke(MinuteAt(e.Y) / 15 * 15); // on the quarter: a starting point, edited to the minute
        }
        base.OnMouseDoubleClick(e);
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Delete or Keys.Escape || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Escape && _drag is not null)
        {
            CancelDrag();
            e.Handled = true;
        }
        else if (e.KeyCode == Keys.ShiftKey && Dragging)
        {
            UpdateDrag(); // to the minute from now on
        }
        else if (e.KeyCode == Keys.Delete && SelectedId is not null && !Dragging)
        {
            DeleteRequested?.Invoke();
            e.Handled = true;
        }
        base.OnKeyDown(e);
    }

    protected override void OnKeyUp(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.ShiftKey && Dragging)
        {
            UpdateDrag(); // back to 5-minute steps
        }
        base.OnKeyUp(e);
    }

    private void CancelDrag()
    {
        var started = Dragging;
        _drag = null;
        StopAutoScroll();
        Cursor = Cursors.Default;
        Invalidate();
        if (started)
        {
            DragCancelled?.Invoke();
        }
    }

    private void UpdateDrag()
    {
        if (_drag is not { Started: true } drag)
        {
            return;
        }
        var precise = (ModifierKeys & Keys.Shift) != 0;
        var (start, end) = BlockDrag.Apply(drag.Block, drag.Kind, (drag.Y - drag.Down.Y) * MinutesPerPixel, precise);
        if ((start, end) != (drag.Start, drag.End))
        {
            drag.Start = start;
            drag.End = end;
            Invalidate();
            DragPreview?.Invoke(drag.Block.Id, start, end);
        }
    }

    // Near the top or bottom of the visible part, the scrolling parent moves along, faster the closer the pointer.
    private void UpdateAutoScroll(int y)
    {
        if (Parent is not ScrollableControl viewport)
        {
            return;
        }
        var visibleY = y + Top; // Top is negative while the parent is scrolled down
        var zone = LogicalToDeviceUnits(AutoScrollZone);
        var speed = LogicalToDeviceUnits(14);
        _scrollStep = visibleY < zone ? -Math.Max(1, speed * (zone - visibleY) / zone)
                      : visibleY > viewport.ClientSize.Height - zone ? Math.Max(1, speed * (visibleY - viewport.ClientSize.Height + zone) / zone)
                      : 0;
        if (_scrollStep != 0)
        {
            _autoScroll.Start();
        }
        else
        {
            _autoScroll.Stop();
        }
    }

    private void AutoScrollTick()
    {
        if (!Dragging || Parent is not ScrollableControl viewport)
        {
            StopAutoScroll();
            return;
        }
        var before = viewport.AutoScrollPosition.Y;
        viewport.AutoScrollPosition = new Point(0, -before + _scrollStep);
        if (viewport.AutoScrollPosition.Y == before)
        {
            return; // at the top or the bottom
        }
        _drag!.Y = PointToClient(Cursor.Position).Y; // the content moved under the pointer
        UpdateDrag();
    }

    private void StopAutoScroll()
    {
        _autoScroll.Stop();
        _scrollStep = 0;
    }

    private void SetHover(Guid? id)
    {
        if (_hover != id)
        {
            _hover = id;
            Invalidate();
        }
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
        var dragging = _drag is { Started: true } d ? d : null;
        foreach (var (block, depth) in _layers)
        {
            if (block.Id != dragging?.Block.Id)
            {
                DrawBlock(g, block, BlockRect(block.Start, block.End, depth));
            }
        }
        if (dragging is not null)
        {
            // Where it was: a dotted outline over the blocks around it, until the drag is committed or cancelled.
            var was = BlockRect(dragging.Block.Start, dragging.Block.End, dragging.Depth);
            using (var path = Theme.Rounded(new RectangleF(was.X + 0.5f, was.Y + 0.5f, was.Width - 1, was.Height - 1), LogicalToDeviceUnits(6)))
            using (var pen = new Pen(Theme.Muted) { DashStyle = DashStyle.Dot })
            {
                g.DrawPath(pen, path);
            }
            // The dragged block on top of everything, at its layer's indent, with its new times beside it.
            var preview = dragging.Block with { Start = dragging.Start, End = dragging.End };
            var rect = BlockRect(preview.Start, preview.End, dragging.Depth);
            DrawBlock(g, preview, rect);
            DrawTimes(g, preview, rect);
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
            var dot = LogicalToDeviceUnits(8);
            using var pen = new Pen(Theme.Cyan, LogicalToDeviceUnits(2));
            using var brush = new SolidBrush(Theme.Cyan);
            g.DrawLine(pen, labelWidth, y, right, y);
            g.FillEllipse(brush, labelWidth - dot / 2f, y - dot / 2f, dot, dot);
        }
    }

    // A small label with the times being dragged to: above the block, or below it at the top of the day.
    private void DrawTimes(Graphics g, ScheduleBlock b, Rectangle r)
    {
        var text = $"{DayTime.Format(b.Start)}–{DayTime.Format(b.End)} · {DayTime.Duration(b.Duration)}";
        var size = TextRenderer.MeasureText(g, text, _theme.Small, Size.Empty, TextFormatFlags.NoPadding);
        var pad = LogicalToDeviceUnits(6);
        var w = size.Width + 2 * pad;
        var h = size.Height + pad;
        var y = r.Y - h - LogicalToDeviceUnits(3);
        if (y < 0)
        {
            y = r.Bottom + LogicalToDeviceUnits(3);
        }
        var box = new Rectangle(r.Right - w - LogicalToDeviceUnits(8), y, w, h);
        using var path = Theme.Rounded(new RectangleF(box.X + 0.5f, box.Y + 0.5f, box.Width - 1, box.Height - 1), h / 2f);
        using var fill = new SolidBrush(Theme.Input);
        using var pen = new Pen(Theme.Cyan);
        g.FillPath(fill, path);
        g.DrawPath(pen, path);
        TextRenderer.DrawText(g, text, _theme.Small, box, Theme.Text,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
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
            var border = selected ? Theme.Cyan : b.Id == _hover ? Blend(color, Theme.Text, 0.45f) : color; // hover: a little brighter
            using var pen = new Pen(border, selected ? LogicalToDeviceUnits(2) : 1);
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
        if (b.Action is { } action)
        {
            detail += $" · at start: {ScheduledActions.Label(action)}";
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

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _autoScroll.Dispose();
        }
        base.Dispose(disposing);
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
