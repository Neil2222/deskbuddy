using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace DeskBuddy.Companion.UI;

/// <summary>A rounded panel with a small capital title, optionally marked with LED dots in an accent colour.</summary>
public sealed class Card : Panel
{
    private readonly Theme _theme;

    public string Title { get; set; } = "";
    public Color? Accent { get; set; }

    public Card(Theme theme)
    {
        _theme = theme;
        DoubleBuffered = true;
        ResizeRedraw = true;
        BackColor = Theme.Card; // children inherit it; the corners are painted with the parent's colour
        Padding = new Padding(16, 38, 16, 14);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(Parent?.BackColor ?? Theme.Window);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
        using (var path = Theme.Rounded(r, LogicalToDeviceUnits(10)))
        using (var fill = new SolidBrush(Theme.Card))
        using (var pen = new Pen(Theme.CardBorder))
        {
            g.FillPath(fill, path);
            g.DrawPath(pen, path);
        }
        var x = LogicalToDeviceUnits(16);
        var top = LogicalToDeviceUnits(13);
        if (Accent is { } accent)
        {
            // Three LEDs, the middle one lit: the sign's colours as a section marker.
            var d = LogicalToDeviceUnits(5);
            var gap = LogicalToDeviceUnits(3);
            var y = top + LogicalToDeviceUnits(4);
            for (var i = 0; i < 3; i++)
            {
                using var dot = new SolidBrush(i == 1 ? accent : ControlPaint.Dark(accent, 0.55f));
                g.FillEllipse(dot, x + i * (d + gap), y, d, d);
            }
            x += 3 * (d + gap) + LogicalToDeviceUnits(4);
        }
        TextRenderer.DrawText(g, Title.ToUpperInvariant(), _theme.At(DeviceDpi).Section, new Point(x, top), Theme.Muted,
                              TextFormatFlags.NoPadding);
    }
}

public enum ButtonLook
{
    Tile,   // pixel-art icon over a label; activities
    Accent, // filled amber; the main action of a section
    Quiet,  // flat, text only; secondary commands
    Pill,   // status: a coloured dot and text
    Nav,    // a page in the sidebar: glyph and label, left-aligned; Selected marks the page that is open
}

/// <summary>
/// One owner-drawn button for the whole window. Keeps everything a Button has (focus, Enter/Space, accessibility
/// name); only the painting is DeskBuddy's.
/// </summary>
public sealed class FlatButton : Button
{
    private readonly Theme _theme;
    private bool _hover, _pressed, _selected;
    private Image? _icon;

    public ButtonLook Look { get; set; }
    /// <summary>Icons are scaled by whole numbers to about this height (at 96 DPI): faces, can and moon line up.</summary>
    public int IconHeight { get; set; } = 42;
    public string? Glyph { get; set; }
    public Color DotColor { get; set; } = Theme.Faint;
    /// <summary>Nav: a status dot at the right end (in <see cref="DotColor"/>).</summary>
    public bool ShowDot { get; set; }

    public Image? Icon
    {
        get => _icon;
        set
        {
            _icon = value;
            Invalidate();
        }
    }

    /// <summary>A mode that is on: cyan outline.</summary>
    public bool Selected
    {
        get => _selected;
        set
        {
            if (_selected != value)
            {
                _selected = value;
                AccessibleDescription = value ? "on" : null;
                Invalidate();
            }
        }
    }

    public FlatButton(Theme theme, ButtonLook look)
    {
        _theme = theme;
        Look = look;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw, true);
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        Cursor = Cursors.Hand;
        UseVisualStyleBackColor = false;
        BackColor = Theme.Card;
    }

    protected override void OnMouseEnter(EventArgs e)
    {
        _hover = true;
        Invalidate();
        base.OnMouseEnter(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        _hover = _pressed = false;
        Invalidate();
        base.OnMouseLeave(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        _pressed = e.Button == MouseButtons.Left;
        Invalidate();
        base.OnMouseDown(e);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        _pressed = false;
        Invalidate();
        base.OnMouseUp(e);
    }

    protected override void OnEnabledChanged(EventArgs e)
    {
        Cursor = Enabled ? Cursors.Hand : Cursors.Default;
        base.OnEnabledChanged(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(Parent?.BackColor ?? Theme.Card);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
        var radius = LogicalToDeviceUnits(Look == ButtonLook.Pill ? Height : 8);

        Color fill, text = Enabled ? Theme.Text : Theme.Faint, border = Color.Empty;
        switch (Look)
        {
        case ButtonLook.Accent:
            fill = !Enabled ? Theme.AmberDark : _pressed ? Theme.AmberDim : _hover ? Color.FromArgb(255, 190, 80) : Theme.Amber;
            text = Enabled ? Color.FromArgb(30, 18, 4) : Theme.AmberDim;
            break;
        case ButtonLook.Quiet:
        case ButtonLook.Pill:
            fill = _pressed ? Theme.TilePressed : _hover ? Theme.TileHover : Look == ButtonLook.Pill ? Theme.Tile : Theme.Card;
            border = Look == ButtonLook.Pill ? Theme.CardBorder : Color.Empty;
            text = Enabled ? (Look == ButtonLook.Pill ? Theme.Text : Theme.Muted) : Theme.Faint;
            break;
        case ButtonLook.Nav:
            fill = _selected ? Theme.Tile : _pressed ? Theme.TilePressed : _hover ? Theme.Card : Parent?.BackColor ?? Theme.Window;
            text = _selected ? Theme.Text : Theme.Muted;
            break;
        default:
            fill = !Enabled ? Theme.Card : _pressed ? Theme.TilePressed : _hover ? Theme.TileHover : Theme.Tile;
            border = _selected ? Theme.Cyan : Enabled ? Theme.CardBorder : Theme.Tile;
            break;
        }
        using (var path = Theme.Rounded(r, radius))
        {
            using (var brush = new SolidBrush(fill))
            {
                g.FillPath(brush, path);
            }
            if (border != Color.Empty)
            {
                using var pen = new Pen(border, _selected && Look == ButtonLook.Tile ? LogicalToDeviceUnits(2) : 1);
                g.DrawPath(pen, path);
            }
        }
        if (Focused && ShowFocusCues)
        {
            var inset = LogicalToDeviceUnits(3);
            using var focus = Theme.Rounded(RectangleF.Inflate(r, -inset, -inset), Math.Max(1, radius - inset));
            using var pen = new Pen(Look == ButtonLook.Accent ? Color.FromArgb(30, 18, 4) : Theme.Body) { DashStyle = DashStyle.Dot };
            g.DrawPath(pen, focus);
        }

        var flags = TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis |
                    TextFormatFlags.SingleLine;
        switch (Look)
        {
        case ButtonLook.Tile:
        {
            var labelHeight = LogicalToDeviceUnits(26);
            var iconArea = new Rectangle(0, LogicalToDeviceUnits(8), Width, Height - labelHeight - LogicalToDeviceUnits(8));
            if (_icon is not null)
            {
                var scale = Math.Max(1, IconHeight / _icon.Height);
                BuddyArt.Draw(g, _icon, iconArea, BuddyArt.Scale(scale, DeviceDpi), Enabled ? 1f : 0.35f);
            }
            var label = new Rectangle(0, Height - labelHeight - LogicalToDeviceUnits(4), Width, labelHeight);
            TextRenderer.DrawText(g, Text, _theme.At(DeviceDpi).Strong, label, _selected ? Theme.Cyan : text, flags);
            if (_selected)
            {
                // A small lit LED in the corner: this mode is on.
                var d = LogicalToDeviceUnits(6);
                var m = LogicalToDeviceUnits(8);
                using var dot = new SolidBrush(Theme.Cyan);
                g.FillEllipse(dot, Width - m - d, m, d, d);
            }
            break;
        }
        case ButtonLook.Nav:
        {
            if (_selected)
            {
                // Buddy's eye colour on the left edge: this page is open.
                using var bar = new SolidBrush(Theme.Cyan);
                var h = Height / 2f;
                g.FillRectangle(bar, LogicalToDeviceUnits(1), (Height - h) / 2, LogicalToDeviceUnits(3), h);
            }
            var x = LogicalToDeviceUnits(16);
            var glyphWidth = LogicalToDeviceUnits(22);
            if (Glyph is not null)
            {
                TextRenderer.DrawText(g, Glyph, _theme.At(DeviceDpi).Glyphs, new Rectangle(x, 0, glyphWidth, Height),
                                      _selected ? Theme.Cyan : text, TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
            }
            var dot = LogicalToDeviceUnits(8);
            var right = ShowDot ? dot + LogicalToDeviceUnits(14) : LogicalToDeviceUnits(8);
            var labelX = x + glyphWidth + LogicalToDeviceUnits(8);
            TextRenderer.DrawText(g, Text, _selected ? _theme.At(DeviceDpi).Strong : _theme.At(DeviceDpi).Text10,
                                  new Rectangle(labelX, 0, Width - labelX - right, Height), text,
                                  flags & ~TextFormatFlags.HorizontalCenter);
            if (ShowDot)
            {
                using var brush = new SolidBrush(DotColor);
                g.FillEllipse(brush, Width - LogicalToDeviceUnits(12) - dot, (Height - dot) / 2f, dot, dot);
            }
            break;
        }
        case ButtonLook.Pill:
        {
            var d = LogicalToDeviceUnits(8);
            var x = LogicalToDeviceUnits(12);
            using (var dot = new SolidBrush(DotColor))
            {
                g.FillEllipse(dot, x, (Height - d) / 2f, d, d);
            }
            var textRect = new Rectangle(x + d + LogicalToDeviceUnits(8), 0, Width - x - d - LogicalToDeviceUnits(20), Height);
            TextRenderer.DrawText(g, Text, _theme.At(DeviceDpi).Small, textRect, text, flags & ~TextFormatFlags.HorizontalCenter);
            break;
        }
        default:
        {
            var font = Look == ButtonLook.Accent ? _theme.At(DeviceDpi).Strong : _theme.At(DeviceDpi).Text10;
            var label = Text;
            if (Glyph is not null && label.Length == 0)
            {
                TextRenderer.DrawText(g, Glyph, _theme.At(DeviceDpi).Glyphs, ClientRectangle, text, flags);
                break;
            }
            if (Glyph is not null)
            {
                var glyphSize = TextRenderer.MeasureText(g, Glyph, _theme.At(DeviceDpi).Glyphs, Size.Empty, TextFormatFlags.NoPadding);
                var textSize = TextRenderer.MeasureText(g, label, font, Size.Empty, TextFormatFlags.NoPadding);
                var gap = LogicalToDeviceUnits(8);
                var x = (Width - glyphSize.Width - gap - textSize.Width) / 2;
                TextRenderer.DrawText(g, label, font, new Rectangle(x, 0, textSize.Width + 2, Height), text,
                                      TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.SingleLine);
                TextRenderer.DrawText(g, Glyph, _theme.At(DeviceDpi).Glyphs,
                                      new Rectangle(x + textSize.Width + gap, 0, glyphSize.Width + 2, Height), text,
                                      TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
            }
            else
            {
                TextRenderer.DrawText(g, label, font, ClientRectangle, text, flags);
            }
            break;
        }
        }
    }
}

/// <summary>A dark, rounded text field: the TextBox inside has no border; the frame lights up amber with focus.</summary>
public sealed class InputFrame : Panel
{
    private readonly Control _inner;

    public InputFrame(Control inner, Control? right = null)
    {
        _inner = inner;
        DoubleBuffered = true;
        ResizeRedraw = true;
        BackColor = Theme.Card;
        Padding = new Padding(12, 10, 10, 8);
        inner.BackColor = Theme.Input;
        inner.Dock = DockStyle.Fill;
        Controls.Add(inner);
        if (right is not null)
        {
            right.BackColor = Theme.Input;
            right.Dock = DockStyle.Right;
            Controls.Add(right);
        }
        inner.GotFocus += (_, _) => Invalidate();
        inner.LostFocus += (_, _) => Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
        using var path = Theme.Rounded(r, LogicalToDeviceUnits(8));
        using var fill = new SolidBrush(Theme.Input);
        using var pen = new Pen(_inner.Focused ? Theme.AmberDim : Theme.CardBorder, _inner.Focused ? 1.5f : 1f);
        g.FillPath(fill, path);
        g.DrawPath(pen, path);
    }
}

/// <summary>Buddy's face, pixel for pixel, at a whole-number scale for the current DPI.</summary>
public sealed class FaceView : Control
{
    private Image? _face;

    public int BaseScale { get; set; } = 2;

    public Image? Face
    {
        get => _face;
        set
        {
            if (!ReferenceEquals(_face, value))
            {
                _face = value;
                Invalidate();
            }
        }
    }

    public FaceView()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Theme.Window;
        AccessibleName = "Buddy";
        AccessibleRole = AccessibleRole.Graphic;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        if (_face is not null)
        {
            BuddyArt.Draw(e.Graphics, _face, ClientRectangle, BuddyArt.Scale(BaseScale, DeviceDpi));
        }
    }
}

/// <summary>A dark list for recent messages: one line each, amber marker, ellipsis for long ones.</summary>
public sealed class HistoryList : ListBox
{
    private readonly Theme _theme;

    public HistoryList(Theme theme)
    {
        _theme = theme;
        DrawMode = DrawMode.OwnerDrawFixed;
        BorderStyle = BorderStyle.None;
        BackColor = Theme.Input;
        ForeColor = Theme.Text;
        IntegralHeight = false;
        Font = theme.Text10;
    }

    protected override void OnDrawItem(DrawItemEventArgs e)
    {
        if (e.Index < 0)
        {
            return;
        }
        var selected = (e.State & DrawItemState.Selected) != 0;
        using (var back = new SolidBrush(selected ? Theme.TileHover : Theme.Input))
        {
            e.Graphics.FillRectangle(back, e.Bounds);
        }
        var d = LogicalToDeviceUnits(5);
        using (var dot = new SolidBrush(selected ? Theme.Amber : Theme.AmberDim))
        {
            e.Graphics.FillEllipse(dot, e.Bounds.X + LogicalToDeviceUnits(10), e.Bounds.Y + (e.Bounds.Height - d) / 2f, d, d);
        }
        var text = new Rectangle(e.Bounds.X + LogicalToDeviceUnits(24), e.Bounds.Y, e.Bounds.Width - LogicalToDeviceUnits(30), e.Bounds.Height);
        TextRenderer.DrawText(e.Graphics, Items[e.Index]?.ToString(), _theme.At(DeviceDpi).Text10, text, selected ? Theme.Text : Theme.Muted,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine |
                              TextFormatFlags.NoPrefix);
    }
}

/// <summary>
/// A dark drop-down list: a field showing the chosen item that opens a menu of all items. Replaces ComboBox, whose
/// flat painter hard-codes light system colours for the face and the arrow button.
/// </summary>
public sealed class Picker : Control
{
    private readonly Theme _theme;
    private readonly ContextMenuStrip _menu;
    private int _selectedIndex = -1;
    private bool _hover;

    public List<object> Items { get; } = [];

    public int SelectedIndex
    {
        get => _selectedIndex;
        set
        {
            _selectedIndex = value >= 0 && value < Items.Count ? value : -1;
            Invalidate();
        }
    }

    public object? SelectedItem => _selectedIndex >= 0 ? Items[_selectedIndex] : null;

    /// <summary>The user picked another item (from the list or with the arrow keys); not raised for code changes.</summary>
    public event EventHandler? Picked;

    public Picker(Theme theme)
    {
        _theme = theme;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        TabStop = true;
        Cursor = Cursors.Hand;
        BackColor = Theme.Card;
        AccessibleRole = AccessibleRole.ComboBox;
        _menu = new ContextMenuStrip
        {
            Renderer = new DarkMenuRenderer(), ShowImageMargin = false, ShowCheckMargin = true, Font = theme.Text10,
            BackColor = Theme.Input, ForeColor = Theme.Text,
        };
        _menu.Closed += (_, _) => Invalidate();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _menu.Dispose(); // and its items
        }
        base.Dispose(disposing);
    }

    private void Open()
    {
        while (_menu.Items.Count > 0)
        {
            _menu.Items[0].Dispose(); // removes it from the menu
        }
        for (var i = 0; i < Items.Count; i++)
        {
            var index = i;
            var item = new ToolStripMenuItem(Items[i].ToString()) { Checked = i == _selectedIndex };
            item.Click += (_, _) => Pick(index);
            _menu.Items.Add(item);
        }
        _menu.MinimumSize = new Size(Width, 0);
        _menu.Show(this, new Point(0, Height));
        Invalidate();
    }

    protected override bool IsInputKey(Keys keyData) => keyData is Keys.Up or Keys.Down || base.IsInputKey(keyData);

    protected override void OnKeyDown(KeyEventArgs e)
    {
        switch (e.KeyCode)
        {
        case Keys.Enter or Keys.Space or Keys.F4:
        case Keys.Down when e.Alt:
            Open();
            e.Handled = true;
            break;
        case Keys.Up when _selectedIndex > 0:
            Pick(_selectedIndex - 1);
            e.Handled = true;
            break;
        case Keys.Down when _selectedIndex < Items.Count - 1:
            Pick(_selectedIndex + 1);
            e.Handled = true;
            break;
        }
        base.OnKeyDown(e);
    }

    private void Pick(int index)
    {
        var changed = index != _selectedIndex;
        SelectedIndex = index;
        if (changed)
        {
            Picked?.Invoke(this, EventArgs.Empty);
        }
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left)
        {
            Focus();
            Open();
        }
        base.OnMouseDown(e);
    }

    protected override void OnMouseEnter(EventArgs e)
    {
        _hover = true;
        Invalidate();
        base.OnMouseEnter(e);
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        _hover = false;
        Invalidate();
        base.OnMouseLeave(e);
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
        g.Clear(Parent?.BackColor ?? Theme.Card);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
        var active = Focused || _menu.Visible;
        using (var path = Theme.Rounded(r, LogicalToDeviceUnits(8)))
        using (var fill = new SolidBrush(_hover && !active ? Theme.Tile : Theme.Input))
        using (var pen = new Pen(active ? Theme.Body : Theme.CardBorder))
        {
            g.FillPath(fill, path);
            g.DrawPath(pen, path);
        }
        var pad = LogicalToDeviceUnits(10);
        var chevron = LogicalToDeviceUnits(18);
        TextRenderer.DrawText(g, SelectedItem?.ToString(), _theme.At(DeviceDpi).Text10, new Rectangle(pad, 0, Width - 2 * pad - chevron, Height),
                              Enabled ? Theme.Text : Theme.Faint,
                              TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine |
                              TextFormatFlags.NoPrefix);
        TextRenderer.DrawText(g, Theme.GlyphChevronDown, _theme.At(DeviceDpi).Glyphs, new Rectangle(Width - pad - chevron, 0, chevron, Height),
                              Theme.Muted, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
    }
}

/// <summary>
/// Menus in the window's colours (the Picker's list, the schedule's copy menu): ticked items are marked with a cyan
/// LED, like an active mode. An item's own ForeColor is kept.
/// </summary>
internal sealed class DarkMenuRenderer() : ToolStripProfessionalRenderer(new Colors())
{
    protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
    {
        e.TextColor = e.Item?.ForeColor ?? Theme.Text;
        base.OnRenderItemText(e);
    }

    protected override void OnRenderItemCheck(ToolStripItemImageRenderEventArgs e)
    {
        var d = e.ToolStrip?.LogicalToDeviceUnits(6) ?? 6;
        var r = e.ImageRectangle;
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        using var dot = new SolidBrush(Theme.Cyan);
        e.Graphics.FillEllipse(dot, r.X + (r.Width - d) / 2f, r.Y + (r.Height - d) / 2f, d, d);
    }

    private sealed class Colors : ProfessionalColorTable
    {
        public override Color ToolStripDropDownBackground => Theme.Input;
        public override Color MenuBorder => Theme.CardBorder;
        public override Color MenuItemBorder => Theme.TileHover;
        public override Color MenuItemSelected => Theme.TileHover;
        public override Color ImageMarginGradientBegin => Theme.Input;
        public override Color ImageMarginGradientMiddle => Theme.Input;
        public override Color ImageMarginGradientEnd => Theme.Input;
        public override Color CheckBackground => Theme.Input;
        public override Color CheckSelectedBackground => Theme.TileHover;
        public override Color CheckPressedBackground => Theme.TilePressed;
        public override Color SeparatorDark => Theme.CardBorder;
        public override Color SeparatorLight => Theme.Input;
        public override Color MenuItemSelectedGradientBegin => Theme.TileHover;
        public override Color MenuItemSelectedGradientEnd => Theme.TileHover;
        public override Color MenuItemPressedGradientBegin => Theme.TilePressed;
        public override Color MenuItemPressedGradientEnd => Theme.TilePressed;
    }
}

/// <summary>The window's menus opened from code (a button, a key): ready for the keyboard on their first item.</summary>
internal static class Menus
{
    public static void SelectFirst(ToolStripDropDown menu) =>
        menu.Items.OfType<ToolStripMenuItem>().FirstOrDefault(i => i.Enabled && i.Available)?.Select();
}

internal static class NativeLook
{
    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
    private static extern int SetWindowTheme(IntPtr hwnd, string? app, string? idList);

    /// <summary>Dark title bar (Windows 10 20H1 and later; ignored elsewhere).</summary>
    public static void DarkTitleBar(Form form)
    {
        var on = 1;
        DwmSetWindowAttribute(form.Handle, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, ref on, sizeof(int));
    }

    /// <summary>Dark scroll bars on a text box or list.</summary>
    public static void DarkScrollBars(Control control)
    {
        control.HandleCreated += (_, _) => SetWindowTheme(control.Handle, "DarkMode_Explorer", null);
        if (control.IsHandleCreated)
        {
            SetWindowTheme(control.Handle, "DarkMode_Explorer", null);
        }
    }
}
