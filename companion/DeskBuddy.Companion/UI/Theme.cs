using System.Drawing.Drawing2D;

namespace DeskBuddy.Companion.UI;

/// <summary>
/// DeskBuddy's look: dark navy panels, cyan from Buddy's eyes, amber from the LED sign, his body blue for details.
/// Colours are taken from the firmware's palettes (buddy_art.c, marquee.c). Owns its fonts: dispose with the window.
/// </summary>
public sealed class Theme : IDisposable
{
    public static readonly Color Window = Color.FromArgb(14, 19, 32);
    public static readonly Color Sidebar = Color.FromArgb(10, 14, 24);
    public static readonly Color Card = Color.FromArgb(22, 29, 46);
    public static readonly Color CardBorder = Color.FromArgb(36, 47, 71);
    public static readonly Color Tile = Color.FromArgb(29, 37, 58);
    public static readonly Color TileHover = Color.FromArgb(37, 47, 73);
    public static readonly Color TilePressed = Color.FromArgb(24, 31, 49);
    public static readonly Color Input = Color.FromArgb(11, 15, 26);
    public static readonly Color Text = Color.FromArgb(231, 236, 246);
    public static readonly Color Muted = Color.FromArgb(139, 151, 179);
    public static readonly Color Faint = Color.FromArgb(84, 96, 124);
    public static readonly Color Cyan = Color.FromArgb(96, 232, 255);   // Buddy's eyes
    public static readonly Color Body = Color.FromArgb(158, 178, 208);  // Buddy's body
    public static readonly Color Amber = Color.FromArgb(255, 170, 40);  // the LED sign
    public static readonly Color AmberDim = Color.FromArgb(140, 82, 18);
    public static readonly Color AmberDark = Color.FromArgb(48, 27, 9);
    public static readonly Color Green = Color.FromArgb(88, 224, 128);  // Buddy's chest light
    public static readonly Color Red = Color.FromArgb(255, 84, 84);     // his antenna

    public Font Title { get; }
    public Font Subtitle { get; }
    public Font Text10 { get; }
    public Font Small { get; }
    public Font Section { get; }
    public Font Strong { get; }
    public Font Input10 { get; }
    public Font Mono { get; }
    public Font Glyphs { get; }

    public Theme()
    {
        var ui = Installed("Segoe UI Variable Text") ?? "Segoe UI";
        var display = Installed("Segoe UI Variable Display") ?? "Segoe UI";
        Title = new Font(display, 15f, FontStyle.Bold);
        Subtitle = new Font(ui, 9f);
        Text10 = new Font(ui, 10f);
        Small = new Font(ui, 8.5f);
        Section = new Font(ui, 8f, FontStyle.Bold);
        Strong = new Font(ui, 10f, FontStyle.Bold);
        Input10 = new Font(ui, 11f);
        Mono = new Font(Installed("Cascadia Mono") ?? "Consolas", 9f);
        Glyphs = new Font(Installed("Segoe Fluent Icons") ?? "Segoe MDL2 Assets", 10f);
    }

    // Segoe Fluent Icons / MDL2 Assets code points.
    public const string GlyphChevronDown = "\uE70D";
    public const string GlyphSend = "\uE724";
    public const string GlyphSync = "\uE895";
    public const string GlyphRefresh = "\uE72C";
    public const string GlyphBuddy = "\uE77B";      // Contact
    public const string GlyphActivities = "\uE945"; // LightningBolt
    public const string GlyphMessage = "\uE8BD";    // Message
    public const string GlyphPdc = "\uE716";        // People
    public const string GlyphDevice = "\uE88E";     // USB
    public const string GlyphPlay = "\uE768";
    public const string GlyphStop = "\uE71A";

    private static string? Installed(string family)
    {
        using var probe = new Font(family, 10f);
        return probe.Name == family ? family : null;
    }

    public static GraphicsPath Rounded(RectangleF r, float radius)
    {
        var path = new GraphicsPath();
        var d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
        if (d <= 0)
        {
            path.AddRectangle(r);
            return path;
        }
        path.AddArc(r.X, r.Y, d, d, 180, 90);
        path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        path.CloseFigure();
        return path;
    }

    public void Dispose()
    {
        Title.Dispose();
        Subtitle.Dispose();
        Text10.Dispose();
        Small.Dispose();
        Section.Dispose();
        Strong.Dispose();
        Input10.Dispose();
        Mono.Dispose();
        Glyphs.Dispose();
    }
}
