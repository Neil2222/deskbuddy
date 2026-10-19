using System.Globalization;
using System.Text;
using System.Text.Json;

namespace DeskBuddy.Companion.Usage;

/// <summary>One Claude plan window: utilization in whole percent (0..999), when it resets, and the predicted
/// utilization at the reset (null: no prediction).</summary>
public sealed record UsageWindow(int Pct, DateTimeOffset ResetsAt, int? Pred);

/// <summary>The two windows; either may be missing (null).</summary>
public sealed record UsageSnapshot(UsageWindow? Five, UsageWindow? Week)
{
    public bool IsEmpty => Five is null && Week is null;
}

/// <summary>The answer of <c>GET /api/oauth/usage</c>, and the prediction (a port of pdc-manager's).</summary>
public static class UsageApi
{
    public static readonly TimeSpan FiveHours = TimeSpan.FromHours(5);
    public static readonly TimeSpan SevenDays = TimeSpan.FromDays(7);

    /// <summary>The windows in the body (five_hour, seven_day); a window that is null, missing or without a valid
    /// utilization and resets_at is left out. Null when the body is not a JSON object.</summary>
    public static UsageSnapshot? Parse(string body, DateTimeOffset now)
    {
        try
        {
            using var doc = JsonDocument.Parse(body);
            if (doc.RootElement.ValueKind != JsonValueKind.Object)
            {
                return null;
            }
            return new UsageSnapshot(Window(doc.RootElement, "five_hour", FiveHours, now),
                                     Window(doc.RootElement, "seven_day", SevenDays, now));
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static UsageWindow? Window(JsonElement root, string key, TimeSpan length, DateTimeOffset now)
    {
        if (!root.TryGetProperty(key, out var w) || w.ValueKind != JsonValueKind.Object ||
            !w.TryGetProperty("utilization", out var u) || u.ValueKind != JsonValueKind.Number || !u.TryGetDouble(out var pct) ||
            !w.TryGetProperty("resets_at", out var r) || r.ValueKind != JsonValueKind.String ||
            !DateTimeOffset.TryParse(r.GetString(), CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal, out var resets))
        {
            return null;
        }
        return new UsageWindow(Clamp(pct), resets.ToUniversalTime(), Predict(pct, resets, length, now));
    }

    /// <summary>The utilization at the end of the window if it goes on at the same rate: pct · window / elapsed.
    /// Null when pct ≤ 0, the window has ended, or less than 5 minutes of it have passed.</summary>
    public static int? Predict(double pct, DateTimeOffset resetsAt, TimeSpan length, DateTimeOffset now)
    {
        var window = length.TotalMinutes;
        var remaining = (resetsAt - now).TotalMinutes;
        var elapsed = window - remaining;
        if (pct <= 0 || remaining <= 0 || elapsed < 5)
        {
            return null;
        }
        return Clamp(pct * window / elapsed);
    }

    private static int Clamp(double pct) => double.IsNaN(pct) ? 0 : (int)Math.Clamp(Math.Round(pct, MidpointRounding.AwayFromZero), 0, 999);
}

/// <summary>The usage frame of serial protocol v1 (docs/protocol.md, "Claude usage").</summary>
public static class UsageWire
{
    public const string Cap = "usage";

    /// <summary><c>usage.set state=on [five= five_end= [five_pred=]] [week= week_end= [week_pred=]]</c>, or
    /// <c>usage.set state=off</c> when there is nothing (null).</summary>
    public static string Set(UsageSnapshot? usage)
    {
        if (usage is null)
        {
            return "usage.set state=off";
        }
        var line = new StringBuilder("usage.set state=on");
        Append(line, "five", usage.Five);
        Append(line, "week", usage.Week);
        return line.ToString();
    }

    private static void Append(StringBuilder line, string key, UsageWindow? w)
    {
        if (w is null)
        {
            return;
        }
        line.Append(CultureInfo.InvariantCulture, $" {key}={w.Pct} {key}_end={w.ResetsAt.ToUnixTimeSeconds()}");
        if (w.Pred is { } pred)
        {
            line.Append(CultureInfo.InvariantCulture, $" {key}_pred={pred}");
        }
    }
}

/// <summary>
/// When to send <c>usage.set</c>: at once after hello and on every change, and every <see cref="Interval"/>
/// otherwise (the device drops the lease after 30 s without one).
/// </summary>
public sealed class UsageLease
{
    public static readonly TimeSpan Interval = TimeSpan.FromSeconds(10);

    private string? _sent;
    private DateTimeOffset _sentAt;

    /// <summary>The command to send now, or null.</summary>
    public string? Next(DateTimeOffset now, UsageSnapshot? usage)
    {
        var want = UsageWire.Set(usage);
        if (_sent == want && now - _sentAt < Interval)
        {
            return null;
        }
        _sent = want;
        _sentAt = now;
        return want;
    }

    /// <summary>Forget what was sent (new connection, device restarted): the next <see cref="Next"/> sends.</summary>
    public void Reset() => _sent = null;
}

/// <summary>A window as the device shows it: <c>5U 37% &gt; 130% 2:30</c>.</summary>
public static class UsageText
{
    public static string Window(string label, UsageWindow w, DateTimeOffset now) =>
        $"{label} {w.Pct}%" + (w.Pred is { } p ? $" > {p}%" : "") + " " + Countdown(w.ResetsAt - now);

    /// <summary>Time until the reset: ≥ 24 h <c>3d 2u</c>, ≥ 1 h <c>2:30</c>, &lt; 1 h <c>45m</c>, ≤ 0 <c>nu</c>.</summary>
    public static string Countdown(TimeSpan left)
    {
        var minutes = (long)Math.Floor(left.TotalMinutes);
        if (left <= TimeSpan.Zero || minutes <= 0)
        {
            return left <= TimeSpan.Zero ? "nu" : "0m";
        }
        return minutes >= 24 * 60 ? $"{minutes / (24 * 60)}d {minutes / 60 % 24}u"
             : minutes >= 60 ? $"{minutes / 60}:{minutes % 60:00}"
             : $"{minutes}m";
    }
}
