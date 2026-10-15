using DeskBuddy.Companion.Protocol;

namespace DeskBuddy.Companion.Pdc;

/// <summary>How a ZDMW click ended; the names are the <c>result=</c> values of <c>pdc.result</c> (docs/protocol.md).</summary>
public enum PdcOutcome
{
    /// <summary>The server confirmed it (counted; Teams message per <see cref="PdcClickResult.Teams"/>).</summary>
    Ok,
    /// <summary>Not (or no longer) logged in: not sent.</summary>
    Auth,
    /// <summary>No write permission (403): not sent.</summary>
    Denied,
    /// <summary>No connection at all: not sent.</summary>
    Offline,
    /// <summary>Certificate refused: not sent.</summary>
    Tls,
    /// <summary>No answer in time: maybe processed.</summary>
    Timeout,
    /// <summary>Connection lost after sending, a server error or an unexpected answer: maybe processed.</summary>
    Unknown,
    /// <summary>Within 5 s of the previous click: not sent.</summary>
    Cooldown,
    /// <summary>Another click is on its way: not sent.</summary>
    Busy,
    /// <summary>Integration off, not logged in, not reachable or no permission: not sent.</summary>
    Unavailable,
}

public sealed record PdcClickResult(PdcOutcome Outcome, long? Count = null, bool? Teams = null)
{
    /// <summary>The server may have counted it (and posted to Teams) although no confirmation came back.</summary>
    public bool IsAmbiguous => Outcome is PdcOutcome.Timeout or PdcOutcome.Unknown;
}

/// <summary>The PDC frames of serial protocol v1 (docs/protocol.md, "PDC Manager (ZDMW)").</summary>
public static class PdcWire
{
    public const string Cap = "pdc";
    public const string ClickEvent = "pdc.click";

    /// <summary><c>pdc.set state=on|off [count=n]</c>: the lease; the count only with state=on.</summary>
    public static string Set(bool on, long? count) =>
        on && Valid(count) ? $"pdc.set state=on count={count}" : $"pdc.set state={(on ? "on" : "off")}";

    /// <summary><c>pdc.result id= result= [count=] [teams=]</c>; teams only with ok.</summary>
    public static string Result(int id, PdcClickResult r)
    {
        var line = $"pdc.result id={id} result={Name(r.Outcome)}";
        if (Valid(r.Count))
        {
            line += $" count={r.Count}";
        }
        if (r.Outcome == PdcOutcome.Ok && r.Teams is { } teams)
        {
            line += $" teams={(teams ? 1 : 0)}";
        }
        return line;
    }

    public static string Name(PdcOutcome outcome) => outcome.ToString().ToLowerInvariant();

    /// <summary>The id of an <c>evt pdc.click</c>, or null when it is missing or out of range (1..65535).</summary>
    public static int? ClickId(Frame frame) =>
        frame is { IsEvent: true, Name: ClickEvent } && int.TryParse(frame.Arg("id"), System.Globalization.NumberStyles.None,
                                                                    null, out var id) && id is >= 1 and <= 65535
            ? id
            : null;

    private static bool Valid(long? count) => count is >= 0 and <= int.MaxValue;
}

/// <summary>
/// When to send <c>pdc.set</c>: at once after hello and on every change of state or count, and every
/// <see cref="Interval"/> otherwise (the device drops the lease after 30 s without one).
/// </summary>
public sealed class PdcLease
{
    public static readonly TimeSpan Interval = TimeSpan.FromSeconds(10);

    private (bool On, long? Count)? _sent;
    private DateTimeOffset _sentAt;

    /// <summary>The command to send now, or null.</summary>
    public string? Next(DateTimeOffset now, bool on, long? count)
    {
        var want = (on, on ? count : null);
        if (_sent == want && now - _sentAt < Interval)
        {
            return null;
        }
        _sent = want;
        _sentAt = now;
        return PdcWire.Set(on, count);
    }

    /// <summary>Forget what was sent (new connection, device restarted): the next <see cref="Next"/> sends.</summary>
    public void Reset() => _sent = null;

    /// <summary>True when the device was last told state=on.</summary>
    public bool IsOn => _sent is { On: true };
}

/// <summary>The words for a result, in Dutch as on the device.</summary>
public static class PdcText
{
    public static string Describe(PdcClickResult r) => r.Outcome switch
    {
        PdcOutcome.Ok when r.Teams == false => r.Count is { } n ? $"Geteld: #{n}, maar het Teams-bericht lukte niet" : "Geteld, maar het Teams-bericht lukte niet",
        PdcOutcome.Ok => r.Count is { } n ? $"Verstuurd: #{n}" : "Verstuurd",
        PdcOutcome.Auth => "Niet verstuurd: opnieuw inloggen",
        PdcOutcome.Denied => "Niet verstuurd: geen schrijfrechten voor ZDMW",
        PdcOutcome.Offline => "Niet verstuurd: PDC Manager niet bereikbaar",
        PdcOutcome.Tls => "Niet verstuurd: certificaat niet vertrouwd",
        PdcOutcome.Timeout => "Geen antwoord: misschien toch verstuurd",
        PdcOutcome.Unknown => "Onduidelijk antwoord: misschien toch verstuurd",
        PdcOutcome.Cooldown => "Niet verstuurd: even wachten (5 s)",
        PdcOutcome.Busy => "Niet verstuurd: vorige klik nog onderweg",
        _ => "Niet verstuurd: niet beschikbaar",
    };
}
