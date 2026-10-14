using System.Text;

namespace DeskBuddy.Companion.Protocol;

/// <summary>
/// Text for msg.show (docs/protocol.md): UTF-8, at most <see cref="MaxChars"/> characters (Unicode code points)
/// and <see cref="MaxBytes"/> bytes, percent-encoded on the wire. DeskBuddy draws what its LED font has and shows a
/// box for the rest (emoji, other scripts); accents are dropped there, not here.
/// </summary>
public static class MessageText
{
    public const int MaxChars = 100;
    public const int MaxBytes = 200;

    /// <summary>What will be sent: Unicode-normalised (é as one character), line breaks and tabs as spaces, trimmed.</summary>
    public static string Clean(string text)
    {
        var sb = new StringBuilder(text.Length);
        foreach (var c in text.Normalize(NormalizationForm.FormC))
        {
            sb.Append(c is '\r' or '\n' or '\t' ? ' ' : c);
        }
        return sb.ToString().Trim();
    }

    /// <summary>Characters as DeskBuddy counts them (code points; an emoji is one, not two UTF-16 units).</summary>
    public static int CountChars(string cleaned) => cleaned.EnumerateRunes().Count();

    /// <summary>Why the text cannot be sent, or null if it can.</summary>
    public static string? Problem(string cleaned)
    {
        if (cleaned.Length == 0)
        {
            return "empty";
        }
        if (CountChars(cleaned) > MaxChars)
        {
            return $"longer than {MaxChars} characters";
        }
        if (Encoding.UTF8.GetByteCount(cleaned) > MaxBytes)
        {
            return $"more than {MaxBytes} bytes (emoji and accents take several each)";
        }
        return null;
    }

    /// <summary>
    /// The text argument: UTF-8 bytes, letters, digits and . _ : + - as they are, everything else as %XX.
    /// Lone surrogates (broken UTF-16) become U+FFFD instead of invalid UTF-8.
    /// </summary>
    public static string Encode(string cleaned)
    {
        var sb = new StringBuilder();
        foreach (var b in new UTF8Encoding(false, false).GetBytes(cleaned))
        {
            var c = (char)b;
            if (char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or ':' or '+' or '-')
            {
                sb.Append(c);
            }
            else
            {
                sb.Append('%').Append(b.ToString("X2"));
            }
        }
        return sb.ToString();
    }
}
