using DeskBuddy.Companion.Protocol;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// The companion's half of msg.show: what is sent for a typed text, and what is refused before sending.
// The firmware's parser is tested against the same rules in firmware/tests/host/test_link.c.
public class MessageTextTests
{
    [Fact]
    public void Encodes_spaces_punctuation_and_non_ascii_as_utf8_percent_escapes()
    {
        Assert.Equal("Hello%2C%20world%21", MessageText.Encode("Hello, world!"));
        Assert.Equal("a.b_c:d+e-f", MessageText.Encode("a.b_c:d+e-f")); // allowed as they are
        Assert.Equal("caf%C3%A9%20%E2%82%AC5%20%F0%9F%98%80", MessageText.Encode("café €5 😀"));
        Assert.Equal("100%25%20%23%3D", MessageText.Encode("100% #="));
    }

    [Fact]
    public void Clean_normalises_accents_and_whitespace()
    {
        Assert.Equal("caf\u00E9", MessageText.Clean("cafe\u0301")); // e + combining accent -> one character
        Assert.Equal("two  lines here", MessageText.Clean("  two\r\nlines\there \n")); // \r\n: two spaces
    }

    [Fact]
    public void Counts_characters_like_deskbuddy()
    {
        Assert.Equal(3, MessageText.CountChars("a😀b")); // the emoji is one character, two UTF-16 units
    }

    [Fact]
    public void Refuses_empty_too_long_and_too_many_bytes()
    {
        Assert.NotNull(MessageText.Problem(""));
        Assert.Null(MessageText.Problem(new string('x', MessageText.MaxChars)));
        Assert.NotNull(MessageText.Problem(new string('x', MessageText.MaxChars + 1)));
        Assert.Null(MessageText.Problem(string.Concat(Enumerable.Repeat("é", 100)))); // 200 bytes: the limit
        Assert.Null(MessageText.Problem(string.Concat(Enumerable.Repeat("😀", 50)))); // 200 bytes
        Assert.NotNull(MessageText.Problem(string.Concat(Enumerable.Repeat("😀", 50)) + "x")); // 201 bytes
    }

    [Fact]
    public void Command_line_fits_deskbuddys_receive_buffer()
    {
        // The worst case allowed: 200 bytes, every one percent-encoded (3 characters each).
        var text = string.Concat(Enumerable.Repeat("😀", 50));
        var line = Frame.Command(4294967295, $"msg.show id=4294967295 style=marquee text={MessageText.Encode(text)}");
        Assert.True(line.Length <= 768, $"{line.Length} characters"); // LINK_LINE_MAX in firmware/components/link
    }
}
