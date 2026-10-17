using System.Drawing.Drawing2D;

namespace DeskBuddy.Companion.UI;

/// <summary>
/// Buddy's own pixels: PNGs exported from the firmware's sprite definitions (assets/buddy, made by
/// tools/host-test.ps1 -Icons) and embedded in the app. Always drawn at whole-number scales without smoothing.
/// </summary>
public sealed class BuddyArt : IDisposable
{
    private readonly Dictionary<string, Bitmap> _images = new(StringComparer.Ordinal);

    public Bitmap this[string name]
    {
        get
        {
            if (!_images.TryGetValue(name, out var bitmap))
            {
                using var stream = typeof(BuddyArt).Assembly.GetManifestResourceStream($"DeskBuddy.Assets.{name}.png")
                                   ?? throw new ArgumentException($"no embedded art '{name}'");
                using var decoded = new Bitmap(stream);
                bitmap = new Bitmap(decoded); // independent of the stream
                _images[name] = bitmap;
            }
            return bitmap;
        }
    }

    /// <summary>The embedded art of that name, or null when there is none (an action this version has no face for).</summary>
    public Bitmap? Find(string name)
    {
        if (_images.TryGetValue(name, out var bitmap))
        {
            return bitmap;
        }
        return typeof(BuddyArt).Assembly.GetManifestResourceInfo($"DeskBuddy.Assets.{name}.png") is null ? null : this[name];
    }

    public static Icon AppIcon()
    {
        using var stream = typeof(BuddyArt).Assembly.GetManifestResourceStream("DeskBuddy.Assets.buddy.ico")!;
        return new Icon(stream);
    }

    /// <summary>The scale for art meant to be shown at <paramref name="baseScale"/> at 96 DPI.</summary>
    public static int Scale(int baseScale, int dpi) => Math.Max(1, baseScale * dpi / 96);

    /// <summary>Draws the image centred in bounds at an integer scale, pixel for pixel.</summary>
    public static void Draw(Graphics g, Image image, Rectangle bounds, int scale, float opacity = 1f)
    {
        var w = image.Width * scale;
        var h = image.Height * scale;
        var dest = new Rectangle(bounds.X + (bounds.Width - w) / 2, bounds.Y + (bounds.Height - h) / 2, w, h);
        var mode = g.InterpolationMode;
        var offset = g.PixelOffsetMode;
        g.InterpolationMode = InterpolationMode.NearestNeighbor;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        if (opacity >= 1f)
        {
            g.DrawImage(image, dest);
        }
        else
        {
            using var attributes = new System.Drawing.Imaging.ImageAttributes();
            attributes.SetColorMatrix(new System.Drawing.Imaging.ColorMatrix { Matrix33 = opacity });
            g.DrawImage(image, dest, 0, 0, image.Width, image.Height, GraphicsUnit.Pixel, attributes);
        }
        g.InterpolationMode = mode;
        g.PixelOffsetMode = offset;
    }

    public void Dispose()
    {
        foreach (var bitmap in _images.Values)
        {
            bitmap.Dispose();
        }
        _images.Clear();
    }
}
