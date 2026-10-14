namespace DeskBuddy.Companion.Protocol;

/// <summary>
/// Builds the time.set command: UTC now, the current local offset, and when it changes next (daylight saving), so
/// the device's clock stays right across the switch even while the companion is not connected.
/// </summary>
public static class TimeSync
{
    public static string Command(DateTimeOffset utcNow, TimeZoneInfo zone)
    {
        var now = utcNow.ToUniversalTime();
        var offset = zone.GetUtcOffset(now);
        var body = $"time.set utc={now.ToUnixTimeSeconds()} ms={now.Millisecond} off={(int)offset.TotalMinutes}";
        if (NextChange(now, zone) is { } next)
        {
            body += $" next={next.ToUnixTimeSeconds()} next_off={(int)zone.GetUtcOffset(next).TotalMinutes}";
        }
        return body;
    }

    /// <summary>The first moment within a year after <paramref name="from"/> with a different offset, to the second.</summary>
    public static DateTimeOffset? NextChange(DateTimeOffset from, TimeZoneInfo zone)
    {
        var offset = zone.GetUtcOffset(from);
        var start = DateTimeOffset.FromUnixTimeSeconds(from.ToUnixTimeSeconds());
        for (var day = 1; day <= 366; day++)
        {
            var probe = start.AddDays(day);
            if (zone.GetUtcOffset(probe) == offset)
            {
                continue;
            }
            // The change lies within the last day: bisect to the second.
            long lo = probe.AddDays(-1).ToUnixTimeSeconds(), hi = probe.ToUnixTimeSeconds();
            while (hi - lo > 1)
            {
                var mid = lo + (hi - lo) / 2;
                if (zone.GetUtcOffset(DateTimeOffset.FromUnixTimeSeconds(mid)) == offset)
                {
                    lo = mid;
                }
                else
                {
                    hi = mid;
                }
            }
            return DateTimeOffset.FromUnixTimeSeconds(hi);
        }
        return null;
    }
}
