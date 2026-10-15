using DeskBuddy.Companion.Device;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Companion.Schedule;

namespace DeskBuddy.Companion;

// The live schedule (P6): while the schedule is Live and not paused, its moods go to Buddy through MoodDirector and
// its actions through ActionScheduler, every request journaled (and flushed) before it is sent. Off or paused: nothing
// is sent. Runs on the UI thread from Tick.
public sealed partial class CompanionService
{
    private MoodDirector? _director; // set while the schedule is live and not paused
    private string? _scheduledEcho;  // the mood the schedule just sent: its evt mood src=cmd is ours, not a manual choice
    private HistoryRecorder? _lastHistory; // the history of the device connected last (board or simulator)
    private readonly DateTimeOffset _startedAt = DateTimeOffset.Now;
    private readonly Dictionary<HistoryRecorder, ActionScheduler> _schedulers = [];
    private readonly Dictionary<string, uint> _occurrenceIids = []; // occurrence → the iid of its request

    /// <summary>The schedule as the Schedule page holds it: blocks, the Live switch and the pause.</summary>
    public WeekSchedule ActiveSchedule { get; set; } = WeekSchedule.Empty;

    /// <summary>Back to the schedule (Resume): a manual mood choice ends and the scheduled mood is applied.</summary>
    public void ResumeSchedule()
    {
        if (_director is { } director)
        {
            Write(LogKind.Info, "Schedule resumed: Buddy follows it again");
            SendScheduledMood(director.Resume(IsConnected ? Mood : null));
        }
        // Paused: the next tick sees the pause gone and resumes (the same way).
    }

    private void ScheduleTick()
    {
        var now = DateTimeOffset.Now;
        var schedule = ActiveSchedule;
        var active = schedule.Live && !schedule.IsPaused(now);
        if (active && _director is null)
        {
            _director = new MoodDirector();
            _director.Tick(schedule, now.DateTime, null); // where the schedule stands now: not a transition
            Write(LogKind.Info, "Schedule live: Buddy follows its moods and actions");
            SendScheduledMood(_director.Resume(IsConnected ? Mood : null));
        }
        else if (!active && _director is not null)
        {
            _director = null;
            Write(LogKind.Info, schedule.Live ? "Schedule paused: no scheduled moods or actions" : "Schedule off: it asks Buddy for nothing");
        }
        else if (_director?.Tick(schedule, now.DateTime, IsConnected ? Mood : null) is { } mood)
        {
            SendScheduledMood(mood);
        }
        TickActions(schedule, now);
    }

    private void TickActions(WeekSchedule schedule, DateTimeOffset now)
    {
        if (!schedule.Live)
        {
            foreach (var s in _schedulers.Values)
            {
                s.Tick(schedule, now, false); // forgets what it waited for; records nothing
            }
            return;
        }
        // Records go to the history of the device in use: the connected one, else the last one connected. Before any
        // connection nothing is recorded (it is not known which device it is for); what was missed meanwhile is
        // recorded once one connects.
        if ((_history ?? _lastHistory) is not { IsLoaded: true } history)
        {
            return;
        }
        if (!_schedulers.TryGetValue(history, out var scheduler))
        {
            var occurrences = history.Snapshot.Occurrences;
            scheduler = new ActionScheduler(TimeZoneInfo.Local, occurrences.Where(o => o.Decided).Select(o => o.Id), _startedAt);
            foreach (var o in occurrences)
            {
                if (o.Iid is { } iid)
                {
                    _occurrenceIids[o.Id] = iid; // a busy one from before a restart keeps its request
                }
            }
            _schedulers[history] = scheduler;
        }
        foreach (var d in scheduler.Tick(schedule, now, IsConnected && _history == history))
        {
            var o = d.Occurrence;
            var reference = new ScheduleRef(o.Id, o.BlockId, o.PlannedLocal, o.PlannedUtc);
            if (d.Step == ActionStep.Skip)
            {
                history.Skipped(reference, o.Action, d.Reason!);
                Write(LogKind.Info, $"Scheduled {ScheduledActions.Label(o.Action)} ({o.PlannedLocal:HH:mm}) skipped: {d.Reason}");
            }
            else
            {
                _ = RunScheduledActionAsync(history, scheduler, o, reference);
            }
        }
    }

    private async Task RunScheduledActionAsync(HistoryRecorder history, ActionScheduler scheduler, Occurrence o, ScheduleRef reference)
    {
        var label = $"Scheduled {ScheduledActions.Label(o.Action)} ({o.PlannedLocal:HH:mm})";
        if (!Caps.Contains("track") || !Caps.Contains("log") || !Acts.Contains(o.Action))
        {
            history.Skipped(reference, o.Action, "unsupported");
            scheduler.Skipped(o.Id);
            Write(LogKind.Warning, $"{label}: this firmware cannot do it (caps track, log and acts={o.Action} needed); skipped");
            return;
        }
        if (!_occurrenceIids.TryGetValue(o.Id, out var iid))
        {
            iid = history.NextIid();
            _occurrenceIids[o.Id] = iid;
            history.Requested(iid, o.Action, "scheduled", reference);
            await history.FlushAsync(); // on disk before it is sent: after a crash it counts as requested, never sent twice
        }
        var (_, result, reply) = await SendActionAsync(label, $"act name={o.Action} iid={iid}");
        history.RequestAnswered(iid, result);
        switch (result)
        {
        case "busy":
            scheduler.Busy(o.Id, DateTimeOffset.Now, reply?.Arg("mode"));
            break;
        case "not_connected":
            scheduler.NotSent(o.Id); // certainly not sent: it may still run within its grace period
            break;
        default:
            scheduler.Answered(o.Id); // ok, dup, refused, or uncertain (no reply, link lost): never sent again
            break;
        }
    }

    // After hello and status: the schedule's view of the device's mood (an override is reapplied; a mood picked while
    // the Companion was away is respected; missed transitions are not replayed).
    private void ScheduleConnected()
    {
        if (_director is { } director)
        {
            SendScheduledMood(director.Connected(Mood));
        }
    }

    private void ScheduleBooted()
    {
        if (_director is { } director)
        {
            SendScheduledMood(director.Booted());
        }
    }

    // evt mood: who changed it decides what it means for the schedule.
    private void ScheduleMoodEvent(Frame frame)
    {
        if (_director is not { } director || frame.Arg("mood") is not { } mood)
        {
            return;
        }
        switch (frame.Arg("src"))
        {
        case "touch":
            director.Manual(mood);
            break;
        case "cmd" when mood == _scheduledEcho:
            _scheduledEcho = null; // the schedule's own
            break;
        case "cmd":
            director.Manual(mood); // a mood tile in the Companion, or another tool
            break;
        case "timer":
        case "break" when mood != "peukie": // a timed mood ran out, or the smoke break ended
            SendScheduledMood(director.DeviceSettled(mood));
            break;
        }
    }

    private void SendScheduledMood(string? mood)
    {
        if (mood is null || _link is not { } link || !IsConnected || !Caps.Contains("mood"))
        {
            return;
        }
        _scheduledEcho = mood;
        _ = SendScheduledMoodAsync(link, mood);
    }

    private async Task SendScheduledMoodAsync(DeviceLink link, string mood)
    {
        try
        {
            var reply = await link.RequestAsync(DeviceMood.Command(mood), ReplyTimeout);
            if (reply.IsOk)
            {
                Write(LogKind.Info, $"Schedule: mood {mood}");
            }
            else
            {
                _scheduledEcho = null;
                Write(LogKind.Warning, $"Schedule: mood {mood} refused ({reply.Name})");
            }
        }
        catch (Exception e) when (e is TimeoutException or IOException or OperationCanceledException)
        {
            _scheduledEcho = null;
        }
    }
}
