using System.Drawing.Drawing2D;
using DeskBuddy.Companion.History;

namespace DeskBuddy.Companion.UI;

/// <summary>Colours of the History page's tones, from Buddy's palette.</summary>
internal static class Tones
{
    public static Color Of(JournalTone tone) => tone switch
    {
        JournalTone.Positive => Theme.Green,
        JournalTone.Negative => Theme.Red,
        JournalTone.Warning => Theme.Amber,
        JournalTone.Neutral => Theme.Cyan,
        _ => Theme.Muted,
    };

    public static Color Blend(Color a, Color b, float t) => Color.FromArgb(
        (int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));

    public static GraphicsPath Box(Rectangle r, float radius) =>
        Theme.Rounded(new RectangleF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), radius);

    /// <summary>A small rounded label in a tone: the outcome of a question, "Plan", "Now".</summary>
    public static Rectangle Pill(Graphics g, Font font, string text, Color color, int right, int centerY, int pad, bool dashed = false)
    {
        var size = TextRenderer.MeasureText(g, text, font, Size.Empty, TextFormatFlags.NoPadding);
        var h = size.Height + pad;
        var r = new Rectangle(right - size.Width - 2 * pad, centerY - h / 2, size.Width + 2 * pad, h);
        using (var path = Box(r, h / 2f))
        using (var fill = new SolidBrush(Blend(Theme.Card, color, 0.16f)))
        using (var pen = new Pen(Blend(Theme.Card, color, 0.7f)) { DashStyle = dashed ? DashStyle.Dash : DashStyle.Solid })
        {
            g.FillPath(fill, path);
            g.DrawPath(pen, path);
        }
        TextRenderer.DrawText(g, text, font, r, color, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
        return r;
    }
}

/// <summary>
/// A day of Buddy's journal: the entries one under another in time order (no proportional hours, so a quiet afternoon
/// takes no room), each a card with his pixel art, what it was and how it ended. Click an entry to inspect it, a group
/// of shows or the technical line to open it; Up and Down move the selection. Sized by its owner
/// (<see cref="PreferredHeight"/>) inside a scrolling panel, which keeps its position when rows change.
/// </summary>
public sealed class JournalView : Control
{
    private const int TimeWidth = 54, Rail = 18, Pad = 8;
    private readonly Theme _theme;
    private readonly BuddyArt _art;
    private IReadOnlyList<JournalRow> _rows = [];
    private int[] _tops = [0];
    private string? _hover;

    public JournalView(Theme theme, BuddyArt art)
    {
        _theme = theme;
        _art = art;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        BackColor = Theme.Card;
        TabStop = true;
        AccessibleRole = AccessibleRole.List;
        AccessibleName = "Day journal";
    }

    public TimeZoneInfo Zone { get; set; } = TimeZoneInfo.Local;
    public string? SelectedKey { get; private set; }
    public JournalRow? SelectedRow => _rows.FirstOrDefault(r => r.Key == SelectedKey);
    public IReadOnlyList<JournalRow> Rows => _rows;

    /// <summary>An entry or a plan block was selected (null: none).</summary>
    public event Action<JournalRow?>? SelectionChanged;
    /// <summary>A group of shows or the technical list was clicked: open or close it (by its key).</summary>
    public event Action<string>? Toggled;

    public int PreferredHeight => _tops[^1] + LogicalToDeviceUnits(Pad);

    /// <summary>Shows new rows; the selection is kept while its row is still there.</summary>
    public void Show(IReadOnlyList<JournalRow> rows)
    {
        _rows = rows;
        var tops = new int[rows.Count + 1];
        var y = LogicalToDeviceUnits(4);
        for (var i = 0; i < rows.Count; i++)
        {
            tops[i] = y;
            y += LogicalToDeviceUnits(HeightOf(rows[i]));
        }
        tops[rows.Count] = y;
        _tops = tops;
        if (SelectedKey is not null && !rows.Any(r => r.Key == SelectedKey && r.Selectable))
        {
            SelectedKey = null;
        }
        AccessibilityNotifyClients(AccessibleEvents.Reorder, -1);
        Invalidate();
    }

    public void Select(string? key, bool raise = true)
    {
        if (SelectedKey == key)
        {
            return;
        }
        SelectedKey = key;
        Invalidate();
        if (raise)
        {
            SelectionChanged?.Invoke(SelectedRow);
        }
    }

    private static int HeightOf(JournalRow r) => r.Kind switch
    {
        JournalRowKind.Header => 34,
        JournalRowKind.Plan => 44,
        JournalRowKind.Entry when r.Nested => 46,
        JournalRowKind.Entry => r.Item!.Linked is not null ? 88 : 66,
        JournalRowKind.Shows or JournalRowKind.PlanSummary => 44,
        JournalRowKind.Empty => 190,
        JournalRowKind.TechnicalToggle => 40,
        _ => 26,
    };

    private Rectangle RowRect(int i) => new(0, _tops[i], Width, _tops[i + 1] - _tops[i]);

    private int RowAt(int y)
    {
        for (var i = 0; i < _rows.Count; i++)
        {
            if (y >= _tops[i] && y < _tops[i + 1])
            {
                return i;
            }
        }
        return -1;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        var i = RowAt(e.Y);
        var key = i >= 0 && _rows[i].Kind is not (JournalRowKind.Header or JournalRowKind.Empty or JournalRowKind.Technical) ? _rows[i].Key : null;
        Cursor = key is not null ? Cursors.Hand : Cursors.Default;
        if (key != _hover)
        {
            _hover = key;
            Invalidate();
        }
        base.OnMouseMove(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        _hover = null;
        Invalidate();
        base.OnMouseLeave(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        Focus();
        var i = RowAt(e.Y);
        if (e.Button == MouseButtons.Left && i >= 0)
        {
            var row = _rows[i];
            if (row.Selectable)
            {
                Select(row.Key);
            }
            else if (row.Kind is JournalRowKind.Shows or JournalRowKind.PlanSummary or JournalRowKind.TechnicalToggle)
            {
                Toggled?.Invoke(row.Key);
            }
        }
        base.OnMouseDown(e);
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Up or Keys.Down || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode is Keys.Up or Keys.Down)
        {
            var selectable = Enumerable.Range(0, _rows.Count).Where(i => _rows[i].Selectable).ToList();
            if (selectable.Count > 0)
            {
                var at = selectable.FindIndex(i => _rows[i].Key == SelectedKey);
                var next = at < 0 ? (e.KeyCode == Keys.Down ? 0 : selectable.Count - 1)
                                  : Math.Clamp(at + (e.KeyCode == Keys.Down ? 1 : -1), 0, selectable.Count - 1);
                Select(_rows[selectable[next]].Key);
                ScrollIntoView(RowRect(selectable[next]));
            }
            e.Handled = true;
        }
        base.OnKeyDown(e);
    }

    private void ScrollIntoView(Rectangle r)
    {
        if (Parent is not ScrollableControl panel)
        {
            return;
        }
        var top = -panel.AutoScrollPosition.Y;
        var view = panel.ClientSize.Height;
        if (r.Top < top)
        {
            panel.AutoScrollPosition = new Point(0, r.Top);
        }
        else if (r.Bottom > top + view)
        {
            panel.AutoScrollPosition = new Point(0, r.Bottom - view);
        }
    }

    protected override void OnGotFocus(EventArgs e)
    {
        Invalidate();
        base.OnGotFocus(e);
    }

    protected override void OnLostFocus(EventArgs e)
    {
        Invalidate();
        base.OnLostFocus(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        for (var i = 0; i < _rows.Count; i++)
        {
            var r = RowRect(i);
            if (r.Bottom < e.ClipRectangle.Top || r.Top > e.ClipRectangle.Bottom)
            {
                continue;
            }
            var row = _rows[i];
            switch (row.Kind)
            {
            case JournalRowKind.Header:
                DrawHeader(g, row, r);
                break;
            case JournalRowKind.Plan:
                DrawPlan(g, row, r);
                break;
            case JournalRowKind.Entry:
                DrawEntry(g, row, r, Connected(i - 1), Connected(i + 1));
                break;
            case JournalRowKind.PlanSummary:
                DrawPlanSummary(g, row, r);
                break;
            case JournalRowKind.Shows:
                DrawShows(g, row, r, Connected(i - 1), Connected(i + 1));
                break;
            case JournalRowKind.Empty:
                DrawEmpty(g, row, r);
                break;
            case JournalRowKind.TechnicalToggle:
                DrawToggle(g, row, r);
                break;
            default:
                DrawTechnical(g, row, r);
                break;
            }
        }
    }

    // Whether the rail of the timeline continues into the row (entries of the same section join up).
    private bool Connected(int i) => i >= 0 && i < _rows.Count && _rows[i].Kind is JournalRowKind.Entry or JournalRowKind.Shows;

    private int X(int logical) => LogicalToDeviceUnits(logical);

    private void DrawHeader(Graphics g, JournalRow row, Rectangle r)
    {
        var text = row.Text.ToUpperInvariant();
        var bounds = new Rectangle(X(Pad), r.Y + X(12), r.Width - X(2 * Pad), r.Height - X(12));
        TextRenderer.DrawText(g, text, _theme.Section, bounds, Theme.Muted, TextFormatFlags.NoPadding);
        var w = TextRenderer.MeasureText(g, text, _theme.Section, Size.Empty, TextFormatFlags.NoPadding).Width;
        using var pen = new Pen(Theme.CardBorder);
        var y = bounds.Y + X(7);
        g.DrawLine(pen, bounds.X + w + X(10), y, r.Right - X(Pad), y);
    }

    private Rectangle CardRect(Rectangle r, bool nested = false)
    {
        var left = X(TimeWidth + Rail + 10 + (nested ? 26 : 0));
        return new Rectangle(left, r.Y + X(3), r.Width - left - X(Pad), r.Height - X(6));
    }

    private void DrawPlan(Graphics g, JournalRow row, Rectangle r)
    {
        var p = row.Plan!;
        var card = CardRect(r);
        var selected = row.Key == SelectedKey;
        using (var path = Tones.Box(card, X(6)))
        using (var pen = new Pen(selected ? Theme.Cyan : row.Key == _hover ? Theme.Muted : Theme.Faint, selected ? X(2) : 1)
               { DashStyle = selected ? DashStyle.Solid : DashStyle.Dash })
        {
            g.DrawPath(pen, path);
        }
        var time = $"{p.StartLocal:HH:mm}";
        TextRenderer.DrawText(g, time, _theme.Small, new Rectangle(0, r.Y, X(TimeWidth), r.Height), Theme.Faint,
                              TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
        var art = _art.Find(HistoryJournal.PlanIcon(p));
        var x = card.X + X(10);
        if (art is not null)
        {
            BuddyArt.Draw(g, art, new Rectangle(x, card.Y, X(28), card.Height), BuddyArt.Scale(1, DeviceDpi), 0.55f);
            x += X(36);
        }
        var end = p.EndLocal.Date > p.StartLocal.Date ? "24:00" : p.EndLocal.ToString("HH:mm");
        var pill = Tones.Pill(g, _theme.Small, row.Current ? "Now · planned" : "Planned", row.Current ? Theme.Cyan : Theme.Muted,
                              card.Right - X(10), card.Y + card.Height / 2, X(6), dashed: !row.Current);
        TextRenderer.DrawText(g, $"{row.Text}  ·  {p.StartLocal:HH:mm}–{end}", _theme.Text10,
                              new Rectangle(x, card.Y, pill.X - x - X(8), card.Height), Theme.Muted,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
    }

    private void DrawRail(Graphics g, Rectangle r, bool up, bool down, Color dot, bool hollow)
    {
        var cx = X(TimeWidth + Rail / 2 + 4);
        var cy = r.Y + X(26);
        using (var pen = new Pen(Theme.CardBorder, X(2)))
        {
            if (up)
            {
                g.DrawLine(pen, cx, r.Y, cx, cy);
            }
            if (down)
            {
                g.DrawLine(pen, cx, cy, cx, r.Bottom);
            }
        }
        var d = X(10);
        var dotRect = new RectangleF(cx - d / 2f, cy - d / 2f, d, d);
        using (var back = new SolidBrush(Theme.Card))
        {
            g.FillEllipse(back, dotRect);
        }
        if (hollow)
        {
            using var pen = new Pen(dot, X(2));
            g.DrawEllipse(pen, dotRect);
        }
        else
        {
            using var brush = new SolidBrush(dot);
            g.FillEllipse(brush, dotRect);
        }
    }

    private void DrawEntry(Graphics g, JournalRow row, Rectangle r, bool up, bool down)
    {
        var item = row.Item!;
        var tone = Tones.Of(HistoryJournal.Tone(item));
        var selected = row.Key == SelectedKey;
        var muted = item.Kind is HistoryItemKind.Skipped || item.Hidden;
        var nested = row.Nested;

        if (!nested)
        {
            var time = HistoryJournal.Time(item, Zone);
            TextRenderer.DrawText(g, time.Length > 0 ? time : "?", time.Length > 0 ? _theme.Strong : _theme.Text10,
                                  new Rectangle(0, r.Y + X(14), X(TimeWidth), X(24)), time.Length > 0 ? Theme.Text : Theme.Faint,
                                  TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
            DrawRail(g, r, up, down, tone, hollow: muted || item.Kind == HistoryItemKind.Notice);
        }
        else
        {
            var cx = X(TimeWidth + Rail / 2 + 4);
            using var pen = new Pen(Theme.CardBorder, X(2));
            g.DrawLine(pen, cx, r.Y, cx, r.Bottom);
            TextRenderer.DrawText(g, HistoryJournal.Time(item, Zone), _theme.Small, new Rectangle(0, r.Y, X(TimeWidth), r.Height),
                                  Theme.Muted, TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
        }

        var card = CardRect(r, nested);
        using (var path = Tones.Box(card, X(8)))
        {
            var fill = selected ? Theme.Tile : row.Key == _hover ? Tones.Blend(Theme.Card, Theme.Tile, 0.6f) : Tones.Blend(Theme.Card, Theme.Tile, 0.35f);
            using var brush = new SolidBrush(fill);
            g.FillPath(brush, path);
            using var pen = new Pen(selected ? Theme.Cyan : muted ? Theme.CardBorder : Tones.Blend(Theme.Card, tone, 0.45f), selected ? X(2) : 1);
            if (!selected && (item.Kind is HistoryItemKind.Skipped or HistoryItemKind.Notice))
            {
                pen.DashStyle = DashStyle.Dash;
            }
            g.DrawPath(pen, path);
        }
        if (!nested)
        {
            // An accent on the card's left edge, in the entry's tone.
            using var accent = new SolidBrush(muted ? Theme.Faint : tone);
            g.FillRectangle(accent, card.X + X(1), card.Y + X(10), X(3), X(26));
        }

        var iconBox = new Rectangle(card.X + X(10), card.Y + X(nested ? 4 : 6), X(nested ? 30 : 52), X(nested ? 32 : 48));
        if (HistoryJournal.Icon(item) is { } name)
        {
            var art = _art.Find(name) ?? _art["face-neutral"];
            var fit = nested ? 30 : 48; // faces and MY DAY icons at 2x, like the Activities tiles
            var scale = Math.Max(1, fit / Math.Max(art.Width, art.Height));
            BuddyArt.Draw(g, art, iconBox, BuddyArt.Scale(scale, DeviceDpi), muted ? 0.45f : 1f);
        }
        else
        {
            TextRenderer.DrawText(g, Theme.GlyphWarning, _theme.Glyphs, iconBox, Theme.Amber, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        }

        var textX = iconBox.Right + X(12);
        var pillRight = card.Right - X(12);
        var hasPill = item.Kind is HistoryItemKind.Question or HistoryItemKind.Show;
        var lineCenter = card.Y + X(nested ? 20 : 22);
        var pill = hasPill
            ? Tones.Pill(g, _theme.Small, item.Status, muted ? Theme.Muted : tone, pillRight, lineCenter, X(6),
                         dashed: item.Interaction?.Outcome is InteractionOutcome.Unconfirmed or InteractionOutcome.Unknown or InteractionOutcome.NotStarted)
            : new Rectangle(pillRight, 0, 0, 0);
        var flags = TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix | TextFormatFlags.VerticalCenter;
        if (nested)
        {
            TextRenderer.DrawText(g, item.Title, _theme.Text10, new Rectangle(textX, card.Y, pill.X - textX - X(8), card.Height),
                                  Theme.Text, flags);
            return;
        }
        TextRenderer.DrawText(g, item.Title, _theme.Strong, new Rectangle(textX, card.Y + X(10), pill.X - textX - X(8), X(22)),
                              muted ? Theme.Muted : Theme.Text, flags);
        TextRenderer.DrawText(g, HistoryJournal.Line(item), _theme.Small, new Rectangle(textX, card.Y + X(32), card.Right - textX - X(12), X(20)),
                              Theme.Muted, flags);
        if (item.Linked is { } smoke)
        {
            // The smoke break this yes counted: inside the question's card, not a second entry.
            var y = card.Y + X(58);
            using (var pen = new Pen(Theme.CardBorder))
            {
                g.DrawLine(pen, textX, y - X(4), card.Right - X(12), y - X(4));
            }
            var face = _art.Find("face-peukie");
            if (face is not null)
            {
                BuddyArt.Draw(g, face, new Rectangle(textX, y, X(24), X(22)), BuddyArt.Scale(1, DeviceDpi), item.LinkedHidden ? 0.4f : 1f);
            }
            var at = smoke.StartedAt.Utc is { } t ? TimeZoneInfo.ConvertTime(t, Zone).ToString("HH:mm") : "time unknown";
            TextRenderer.DrawText(g, item.LinkedHidden ? "Smoking break (hidden)" : $"Counted as a smoking break · {at}", _theme.Small,
                                  new Rectangle(textX + X(30), y, card.Right - textX - X(42), X(22)),
                                  item.LinkedHidden ? Theme.Faint : Theme.Green, flags);
        }
    }

    private void DrawPlanSummary(Graphics g, JournalRow row, Rectangle r)
    {
        var card = CardRect(r);
        var hover = row.Key == _hover;
        using (var path = Tones.Box(card, X(6)))
        using (var pen = new Pen(hover ? Theme.Muted : Theme.Faint) { DashStyle = DashStyle.Dash })
        {
            g.DrawPath(pen, path);
        }
        TextRenderer.DrawText(g, row.Open ? Theme.GlyphChevronDown : Theme.GlyphChevronRight, _theme.Glyphs,
                              new Rectangle(card.X + X(10), card.Y, X(16), card.Height), Theme.Muted, TextFormatFlags.VerticalCenter);
        var x = card.X + X(30);
        if (_art.Find("myday-calendar") is { } calendar)
        {
            BuddyArt.Draw(g, calendar, new Rectangle(x, card.Y, X(24), card.Height), BuddyArt.Scale(1, DeviceDpi), 0.7f);
            x += X(32);
        }
        var pill = Tones.Pill(g, _theme.Small, "Planned", Theme.Muted, card.Right - X(10), card.Y + card.Height / 2, X(6), dashed: true);
        TextRenderer.DrawText(g, row.Text, _theme.Text10, new Rectangle(x, card.Y, pill.X - x - X(8), card.Height), Theme.Muted,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
    }

    private void DrawShows(Graphics g, JournalRow row, Rectangle r, bool up, bool down)
    {
        var shows = row.Shows!;
        var first = shows[0];
        TextRenderer.DrawText(g, HistoryJournal.Time(first, Zone), _theme.Text10, new Rectangle(0, r.Y + X(14), X(TimeWidth), X(24)),
                              Theme.Muted, TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
        // The rail's dot sits where an entry's would; a show's row is lower, so it is drawn a little higher.
        var cx = X(TimeWidth + Rail / 2 + 4);
        using (var pen = new Pen(Theme.CardBorder, X(2)))
        {
            g.DrawLine(pen, cx, up ? r.Y : r.Y + X(22), cx, down || row.Open ? r.Bottom : r.Y + X(22));
        }
        var d = X(8);
        using (var dot = new SolidBrush(Theme.Body))
        {
            g.FillEllipse(dot, cx - d / 2f, r.Y + X(22) - d / 2f, d, d);
        }

        var card = CardRect(r);
        var hover = row.Key == _hover;
        using (var path = Tones.Box(card, X(8)))
        using (var brush = new SolidBrush(hover ? Tones.Blend(Theme.Card, Theme.Tile, 0.6f) : Theme.Card))
        using (var pen = new Pen(Theme.CardBorder))
        {
            g.FillPath(brush, path);
            g.DrawPath(pen, path);
        }
        TextRenderer.DrawText(g, row.Open ? Theme.GlyphChevronDown : Theme.GlyphChevronRight, _theme.Glyphs,
                              new Rectangle(card.X + X(10), card.Y, X(16), card.Height), Theme.Muted, TextFormatFlags.VerticalCenter);
        var x = card.X + X(32);
        foreach (var show in shows.Take(6))
        {
            var art = _art.Find(HistoryJournal.Icon(show) ?? "") ?? _art["face-neutral"];
            BuddyArt.Draw(g, art, new Rectangle(x, card.Y, X(26), card.Height), BuddyArt.Scale(1, DeviceDpi), 0.85f);
            x += X(28);
        }
        var last = shows[^1];
        var range = shows.Count > 1 && HistoryJournal.Time(last, Zone) is { Length: > 0 } t ? $" · until {t}" : "";
        TextRenderer.DrawText(g, row.Text + range, _theme.Text10, new Rectangle(x + X(6), card.Y, card.Right - x - X(16), card.Height),
                              Theme.Muted, TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
    }

    private void DrawEmpty(Graphics g, JournalRow row, Rectangle r)
    {
        var face = _art.Find(row.Text.StartsWith("Nothing recorded yet", StringComparison.Ordinal) ? "face-relaxed" : "face-sleepy");
        var art = new Rectangle(r.X, r.Y + X(16), r.Width, X(96));
        if (face is not null)
        {
            BuddyArt.Draw(g, face, art, BuddyArt.Scale(4, DeviceDpi));
        }
        TextRenderer.DrawText(g, row.Text, _theme.Strong, new Rectangle(r.X, art.Bottom + X(10), r.Width, X(24)), Theme.Text,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.NoPrefix);
        TextRenderer.DrawText(g, row.Text.StartsWith("Nothing recorded yet", StringComparison.Ordinal)
                                  ? "Tap MY DAY on Buddy's touchscreen when you do something; his questions and your answers appear here too."
                                  : "Buddy may have been off, unplugged or away from the Companion.",
                              _theme.Small, new Rectangle(r.X + X(40), art.Bottom + X(36), r.Width - X(80), X(40)), Theme.Muted,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.WordBreak | TextFormatFlags.NoPrefix);
    }

    private void DrawToggle(Graphics g, JournalRow row, Rectangle r)
    {
        var color = row.Key == _hover ? Theme.Muted : Theme.Faint;
        var y = r.Y + X(8);
        TextRenderer.DrawText(g, row.Open ? Theme.GlyphChevronDown : Theme.GlyphChevronRight, _theme.Glyphs,
                              new Rectangle(X(Pad), y, X(16), X(24)), color, TextFormatFlags.VerticalCenter);
        TextRenderer.DrawText(g, row.Text, _theme.Small, new Rectangle(X(Pad + 20), y, r.Width - X(Pad + 28), X(24)), color,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
    }

    private void DrawTechnical(Graphics g, JournalRow row, Rectangle r)
    {
        var t = row.Technical!;
        TextRenderer.DrawText(g, TimeZoneInfo.ConvertTime(t.AtUtc, Zone).ToString("HH:mm"), _theme.Small,
                              new Rectangle(0, r.Y, X(TimeWidth), r.Height), Theme.Faint, TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
        TextRenderer.DrawText(g, row.Text, _theme.Small, new Rectangle(X(TimeWidth + Rail + 10), r.Y, r.Width - X(TimeWidth + Rail + 18), r.Height),
                              Theme.Faint, TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
    }

    protected override AccessibleObject CreateAccessibilityInstance() => new JournalAccessible(this);

    private sealed class JournalAccessible(JournalView owner) : ControlAccessibleObject(owner)
    {
        public override int GetChildCount() => owner._rows.Count;

        public override AccessibleObject? GetChild(int index) =>
            index >= 0 && index < owner._rows.Count ? new RowAccessible(owner, index, this) : null;
    }

    private sealed class RowAccessible(JournalView owner, int index, AccessibleObject parent) : AccessibleObject
    {
        private JournalRow Row => owner._rows[index];
        public override AccessibleObject Parent => parent;
        public override AccessibleRole Role => Row.Selectable ? AccessibleRole.ListItem : AccessibleRole.StaticText;
        public override string? Name => Row.Item is { } i
            ? $"{HistoryJournal.Time(i, owner.Zone)} {i.Title}, {HistoryJournal.Line(i)}" : Row.Text;
        public override Rectangle Bounds => owner.RectangleToScreen(owner.RowRect(index));
        public override AccessibleStates State => Row.Key == owner.SelectedKey ? AccessibleStates.Selected | AccessibleStates.Focused : AccessibleStates.Selectable;
    }
}

/// <summary>One of the day's numbers: a caption, the number in its tone and a short breakdown.</summary>
public sealed class StatView : Control
{
    private readonly Theme _theme;
    private StatTile _tile = new("", 0, "", JournalTone.Muted);

    public StatView(Theme theme)
    {
        _theme = theme;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        BackColor = Theme.Card;
        AccessibleRole = AccessibleRole.StaticText;
    }

    public void Show(StatTile tile)
    {
        if (tile == _tile)
        {
            return;
        }
        _tile = tile;
        AccessibleName = $"{tile.Label}: {tile.Value}. {tile.Detail}";
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(Parent?.BackColor ?? Theme.Window);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using (var path = Tones.Box(ClientRectangle, LogicalToDeviceUnits(10)))
        using (var fill = new SolidBrush(Theme.Card))
        using (var pen = new Pen(Theme.CardBorder))
        {
            g.FillPath(fill, path);
            g.DrawPath(pen, path);
        }
        var pad = LogicalToDeviceUnits(14);
        var color = _tile.Value == 0 ? Theme.Faint : Tones.Of(_tile.Tone);
        TextRenderer.DrawText(g, _tile.Label.ToUpperInvariant(), _theme.Section, new Point(pad, LogicalToDeviceUnits(11)), Theme.Muted,
                              TextFormatFlags.NoPadding);
        TextRenderer.DrawText(g, _tile.Value.ToString(), _theme.Title, new Rectangle(pad, LogicalToDeviceUnits(26), Width - 2 * pad, LogicalToDeviceUnits(30)),
                              color, TextFormatFlags.NoPadding | TextFormatFlags.VerticalCenter);
        TextRenderer.DrawText(g, _tile.Detail, _theme.Small, new Rectangle(pad, LogicalToDeviceUnits(56), Width - 2 * pad, LogicalToDeviceUnits(18)),
                              Theme.Muted, TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
    }
}

/// <summary>
/// A month to pick a day from: days with entries carry a cyan dot, today is outlined, the chosen day is filled. Days
/// after today cannot be picked (there is no history yet).
/// </summary>
public sealed class MonthView : Control
{
    private const int Cell = 34, Head = 34, Names = 22;
    private readonly Theme _theme;
    private DateOnly _month;
    private IReadOnlySet<DateOnly> _available = new HashSet<DateOnly>();
    private DateOnly _selected, _today;
    private Point _hover = new(-1, -1);

    public MonthView(Theme theme)
    {
        _theme = theme;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        BackColor = Theme.Input;
        Size = new Size(Cell * 7 + 16, Head + Names + Cell * 6 + 12);
    }

    public event Action<DateOnly>? Picked;

    public void Show(DateOnly selected, DateOnly today, IReadOnlySet<DateOnly> available)
    {
        _selected = selected;
        _today = today;
        _available = available;
        _month = new DateOnly(selected.Year, selected.Month, 1);
        Size = new Size(LogicalToDeviceUnits(Cell * 7 + 16), LogicalToDeviceUnits(Head + Names + Cell * 6 + 12));
        Invalidate();
    }

    private DateOnly First => _month.AddDays(-(((int)_month.DayOfWeek + 6) % 7)); // Monday on or before the 1st

    private Rectangle CellRect(int col, int row) => new(LogicalToDeviceUnits(8 + col * Cell), LogicalToDeviceUnits(Head + Names + row * Cell),
                                                        LogicalToDeviceUnits(Cell), LogicalToDeviceUnits(Cell));

    private Rectangle PrevRect => new(LogicalToDeviceUnits(8), 0, LogicalToDeviceUnits(Head), LogicalToDeviceUnits(Head));
    private Rectangle NextRect => new(Width - LogicalToDeviceUnits(8 + Head), 0, LogicalToDeviceUnits(Head), LogicalToDeviceUnits(Head));

    protected override void OnMouseMove(MouseEventArgs e)
    {
        var col = (e.X - LogicalToDeviceUnits(8)) / LogicalToDeviceUnits(Cell);
        var row = (e.Y - LogicalToDeviceUnits(Head + Names)) / LogicalToDeviceUnits(Cell);
        var hover = e.Y >= LogicalToDeviceUnits(Head + Names) && col is >= 0 and < 7 && row is >= 0 and < 6 ? new Point(col, row) : new Point(-1, -1);
        if (hover != _hover)
        {
            _hover = hover;
            Invalidate();
        }
        base.OnMouseMove(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (PrevRect.Contains(e.Location))
        {
            _month = _month.AddMonths(-1);
            Invalidate();
        }
        else if (NextRect.Contains(e.Location) && _month.AddMonths(1) <= _today)
        {
            _month = _month.AddMonths(1);
            Invalidate();
        }
        else if (_hover.X >= 0)
        {
            var day = First.AddDays(_hover.Y * 7 + _hover.X);
            if (day <= _today)
            {
                Picked?.Invoke(day);
            }
        }
        base.OnMouseDown(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var center = TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding;
        TextRenderer.DrawText(g, _month.ToString("MMMM yyyy", System.Globalization.CultureInfo.InvariantCulture), _theme.Strong,
                              new Rectangle(0, 0, Width, LogicalToDeviceUnits(Head)), Theme.Text, center);
        TextRenderer.DrawText(g, Theme.GlyphChevronLeft, _theme.Glyphs, PrevRect, Theme.Muted, center);
        TextRenderer.DrawText(g, Theme.GlyphChevronRight, _theme.Glyphs, NextRect, _month.AddMonths(1) <= _today ? Theme.Muted : Theme.Tile, center);
        string[] names = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];
        for (var c = 0; c < 7; c++)
        {
            TextRenderer.DrawText(g, names[c], _theme.Small,
                                  new Rectangle(LogicalToDeviceUnits(8 + c * Cell), LogicalToDeviceUnits(Head), LogicalToDeviceUnits(Cell), LogicalToDeviceUnits(Names)),
                                  Theme.Faint, center);
        }
        for (var row = 0; row < 6; row++)
        {
            for (var col = 0; col < 7; col++)
            {
                var day = First.AddDays(row * 7 + col);
                var r = Rectangle.Inflate(CellRect(col, row), -LogicalToDeviceUnits(2), -LogicalToDeviceUnits(2));
                var inMonth = day.Month == _month.Month;
                var future = day > _today;
                if (day == _selected || (_hover == new Point(col, row) && !future))
                {
                    using var fill = new SolidBrush(day == _selected ? Theme.Tile : Theme.TileHover);
                    using var path = Tones.Box(r, LogicalToDeviceUnits(6));
                    g.FillPath(fill, path);
                }
                if (day == _today)
                {
                    using var pen = new Pen(Theme.Cyan);
                    using var path = Tones.Box(r, LogicalToDeviceUnits(6));
                    g.DrawPath(pen, path);
                }
                var color = future ? Theme.Tile : !inMonth ? Theme.Faint : Theme.Text;
                TextRenderer.DrawText(g, day.Day.ToString(), _theme.Text10, new Rectangle(r.X, r.Y, r.Width, r.Height - LogicalToDeviceUnits(6)), color, center);
                if (_available.Contains(day))
                {
                    var d = LogicalToDeviceUnits(4);
                    using var dot = new SolidBrush(inMonth ? Theme.Cyan : Tones.Blend(Theme.Input, Theme.Cyan, 0.5f));
                    g.FillEllipse(dot, r.X + (r.Width - d) / 2f, r.Bottom - LogicalToDeviceUnits(8), d, d);
                }
            }
        }
    }
}

/// <summary>
/// The selected entry in words: its art, what it was, how it ended and why that matters; the ids and device details
/// folded away under "Technical details".
/// </summary>
public sealed class InspectorView : Control
{
    private readonly Theme _theme;
    private readonly BuddyArt _art;
    private InspectorInfo? _info;
    private bool _technicalOpen;
    private Rectangle _toggle;

    public InspectorView(Theme theme, BuddyArt art)
    {
        _theme = theme;
        _art = art;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        BackColor = Theme.Card;
        AccessibleRole = AccessibleRole.Pane;
        AccessibleName = "Details";
    }

    public bool TechnicalOpen => _technicalOpen;

    public void Show(InspectorInfo? info, bool keepTechnical = false)
    {
        _info = info;
        if (!keepTechnical)
        {
            _technicalOpen = false;
        }
        AccessibleDescription = info is null ? null
            : $"{info.Title}. {info.Kind}. {string.Join(". ", info.Fields.Select(f => $"{f.Label}: {f.Value}"))}. {string.Join(" ", info.Notes)}";
        Height = Measure();
        Invalidate();
    }

    private int X(int logical) => LogicalToDeviceUnits(logical);

    private int Measure()
    {
        using var g = CreateGraphics();
        return Arrange(g, paint: false);
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        var h = Measure();
        if (h != Height)
        {
            Height = h;
        }
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        Cursor = _toggle.Contains(e.Location) ? Cursors.Hand : Cursors.Default;
        base.OnMouseMove(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (_toggle.Contains(e.Location))
        {
            _technicalOpen = !_technicalOpen;
            Height = Measure();
            Invalidate();
        }
        base.OnMouseDown(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.Clear(BackColor);
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        Arrange(e.Graphics, paint: true);
    }

    // Lays out (and paints) the details; returns the height they take.
    private int Arrange(Graphics g, bool paint)
    {
        var w = Math.Max(1, Width);
        var wrap = TextFormatFlags.WordBreak | TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding;
        if (_info is not { } info)
        {
            const string text = "Select an entry to see what happened, how it ended and where it came from.";
            var h = TextRenderer.MeasureText(g, text, _theme.Text10, new Size(w, 0), wrap).Height;
            if (paint)
            {
                TextRenderer.DrawText(g, text, _theme.Text10, new Rectangle(0, X(4), w, h), Theme.Muted, wrap);
            }
            _toggle = Rectangle.Empty;
            return h + X(8);
        }

        var y = 0;
        var icon = info.Icon is { } name ? _art.Find(name) : null;
        var iconBox = new Rectangle(0, y, X(56), X(52));
        if (paint)
        {
            if (icon is not null)
            {
                BuddyArt.Draw(g, icon, iconBox, BuddyArt.Scale(Math.Max(1, 48 / Math.Max(icon.Width, icon.Height)), DeviceDpi));
            }
            else
            {
                TextRenderer.DrawText(g, Theme.GlyphWarning, _theme.Glyphs, iconBox, Theme.Amber, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }
        }
        var tx = iconBox.Right + X(10);
        if (paint)
        {
            TextRenderer.DrawText(g, info.Title, _theme.Strong, new Rectangle(tx, y + X(6), w - tx, X(22)), Theme.Text,
                                  TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
            TextRenderer.DrawText(g, info.Kind, _theme.Small, new Rectangle(tx, y + X(28), w - tx, X(20)), Theme.Muted,
                                  TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
        }
        y = iconBox.Bottom + X(10);
        if (paint)
        {
            var width = TextRenderer.MeasureText(g, info.Status, _theme.Small, Size.Empty, TextFormatFlags.NoPadding).Width + 2 * X(6);
            Tones.Pill(g, _theme.Small, info.Status, Tones.Of(info.Tone), width, y + X(11), X(6));
        }
        y += X(30);

        var labelW = X(96);
        foreach (var (label, value) in info.Fields)
        {
            var h = Math.Max(X(20), TextRenderer.MeasureText(g, value, _theme.Text10, new Size(w - labelW, 0), wrap).Height);
            if (paint)
            {
                TextRenderer.DrawText(g, label, _theme.Small, new Rectangle(0, y + X(2), labelW, X(20)), Theme.Muted, TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding);
                TextRenderer.DrawText(g, value, _theme.Text10, new Rectangle(labelW, y, w - labelW, h), Theme.Text, wrap);
            }
            y += h + X(8);
        }

        foreach (var note in info.Notes)
        {
            y += X(4);
            var h = TextRenderer.MeasureText(g, note, _theme.Small, new Size(w - X(12), 0), wrap).Height;
            if (paint)
            {
                using var bar = new SolidBrush(Theme.CardBorder);
                g.FillRectangle(bar, 0, y, X(3), h);
                TextRenderer.DrawText(g, note, _theme.Small, new Rectangle(X(12), y, w - X(12), h), Theme.Muted, wrap);
            }
            y += h + X(6);
        }

        if (info.Technical.Count == 0)
        {
            _toggle = Rectangle.Empty;
            return y + X(4);
        }
        y += X(10);
        _toggle = new Rectangle(0, y, w, X(26));
        if (paint)
        {
            using var pen = new Pen(Theme.CardBorder);
            g.DrawLine(pen, 0, y, w, y);
            TextRenderer.DrawText(g, _technicalOpen ? Theme.GlyphChevronDown : Theme.GlyphChevronRight, _theme.Glyphs,
                                  new Rectangle(0, y + X(4), X(16), X(22)), Theme.Faint, TextFormatFlags.VerticalCenter);
            TextRenderer.DrawText(g, "Technical details", _theme.Small, new Rectangle(X(20), y + X(4), w - X(20), X(22)), Theme.Faint,
                                  TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
        }
        y += X(30);
        if (_technicalOpen)
        {
            foreach (var (label, value) in info.Technical)
            {
                var h = Math.Max(X(18), TextRenderer.MeasureText(g, value, _theme.Mono, new Size(w - labelW, 0), wrap).Height);
                if (paint)
                {
                    TextRenderer.DrawText(g, label, _theme.Small, new Rectangle(0, y, labelW, X(18)), Theme.Faint, TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding);
                    TextRenderer.DrawText(g, value, _theme.Mono, new Rectangle(labelW, y, w - labelW, h), Theme.Muted,
                                          wrap | TextFormatFlags.WordEllipsis);
                }
                y += h + X(4);
            }
        }
        return y + X(4);
    }
}
