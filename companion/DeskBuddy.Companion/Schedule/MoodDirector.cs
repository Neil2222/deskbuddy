namespace DeskBuddy.Companion.Schedule;

/// <summary>
/// Decides when the schedule changes Buddy's mood, and when it leaves him alone. Pure state machine: the caller
/// feeds it the local wall-clock time and what happens on the device, and sends the mood it returns (mood.set).
///
/// Precedence, highest first: the device's own exclusive shows (Clock Mode, Monster, activities, a smoke break; they
/// stay on the device, mood.set is accepted during them and shows afterwards), then a manual mood (the user's own
/// choice), then the scheduled mood. There is no forced default: when no block asks for a mood, Buddy keeps the mood
/// he has.
///
/// A scheduled mood is sent only at an actual transition: the moment the schedule starts asking for a different mood
/// (from none, or from another one). Block boundaries where the mood stays the same are not transitions, and neither
/// is the end of the last block (the schedule then asks for nothing: Buddy keeps his mood).
/// A manual mood lasts until the next actual transition, or until <see cref="Resume"/>.
/// </summary>
public sealed class MoodDirector
{
    public const string DefaultMood = "idle"; // the device's mood after every start

    private bool _started;
    private DateTime _highWater;

    /// <summary>What the schedule asks for now; null: nothing (or the schedule is off).</summary>
    public string? Scheduled { get; private set; }

    /// <summary>The user's own choice, in force until the next actual transition or Resume; null: none.</summary>
    public string? Override { get; private set; }

    /// <summary>The mood the companion wants Buddy in; null: no opinion.</summary>
    public string? Desired => Override ?? Scheduled;

    /// <summary>
    /// Evaluates the schedule at <paramref name="local"/>. Returns the mood to send at an actual transition (and
    /// clears the override), or null. <paramref name="deviceMood"/>: the device's current mood (null while not
    /// connected: the transition still happens, and <see cref="Connected"/> applies it later).
    /// </summary>
    public string? Tick(WeekSchedule schedule, DateTime local, string? deviceMood)
    {
        // When the clock goes back (the repeated hour at the end of daylight saving time, or a manual correction),
        // the schedule stands still until the clock has caught up: no transition happens twice.
        if (_started && local < _highWater)
        {
            local = _highWater;
        }
        _highWater = local;

        var now = SchedulePlanner.Resolve(schedule, local).Mood;
        if (!_started)
        {
            _started = true; // the first evaluation is not a transition: Connected decides what to do at start
            Scheduled = now;
            return null;
        }
        var transition = now is not null && now != Scheduled;
        Scheduled = now;
        if (!transition)
        {
            return null;
        }
        Override = null;
        return now != deviceMood ? now : null;
    }

    /// <summary>
    /// The user chose a mood themselves (touchscreen, companion tile, an integration): it stays until the next actual
    /// transition. Choosing what the schedule asks for anyway is not an override.
    /// </summary>
    public void Manual(string mood) => Override = mood == Scheduled ? null : mood;

    /// <summary>Back to the schedule: the override ends; returns the scheduled mood to send, if any.</summary>
    public string? Resume(string? deviceMood)
    {
        Override = null;
        return Scheduled is { } mood && mood != deviceMood ? mood : null;
    }

    /// <summary>
    /// The device is connected (or the companion started) and reports <paramref name="deviceMood"/>. Returns the
    /// mood to send, or null. An override is reapplied (the device may have restarted). Otherwise, a device in its
    /// default mood gets the scheduled mood; a device in another mood is taken as the user's own choice, so starting
    /// the companion never undoes what was picked meanwhile.
    /// </summary>
    public string? Connected(string deviceMood)
    {
        if (Override is { } chosen)
        {
            return chosen != deviceMood ? chosen : null;
        }
        if (Scheduled is not { } scheduled || scheduled == deviceMood)
        {
            return null;
        }
        if (deviceMood == DefaultMood)
        {
            return scheduled;
        }
        Override = deviceMood;
        return null;
    }

    /// <summary>The device restarted (it is in its default mood again): what it should be in.</summary>
    public string? Booted() => Desired is { } mood && mood != DefaultMood ? mood : null;

    /// <summary>
    /// The device ended something of its own (a smoke break, a timed mood) and fell back to
    /// <paramref name="deviceMood"/>: the mood the companion wants, if that differs.
    /// </summary>
    public string? DeviceSettled(string deviceMood) => Desired is { } mood && mood != deviceMood ? mood : null;
}
