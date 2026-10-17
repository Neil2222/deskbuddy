using System.Collections.Immutable;
using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;

namespace DeskBuddy.Companion;

/// <summary>
/// The Schedule page: the global switch and what the schedule says now, the week as day chips, the chosen day as a
/// timeline, and the editor for the selected block. Every edit is saved at once (schedule.json, see
/// <see cref="ScheduleStore"/>). It only edits the schedule: applying it to Buddy is a later step.
/// </summary>
public sealed class SchedulePage : Panel
{
    private const int InspectorWidth = 340, TileWidth = 71, TileHeight = 54, TileGap = 6;

    private readonly Theme _theme;
    private readonly BuddyArt _art;
    private readonly ToolTip _tips;
    private readonly Action<LogKind, string> _log;
    private readonly ScheduleStore _store;
    private readonly System.Windows.Forms.Timer _clock = new() { Interval = 15_000 };

    private WeekSchedule _schedule = WeekSchedule.Empty;
    private DayOfWeek _day = DateTime.Today.DayOfWeek;
    private Guid? _selected;
    private bool _binding; // filling the editor from the model: its change events are not edits
    private (DayOfWeek Day, int Index, ScheduleBlock Block)? _deleted; // for Undo
    private int? _pendingEnd; // an end before the start: the split over midnight on offer
    private int? _scrollTo; // a minute of the day to bring into view once the timeline is visible and laid out

    // Header.
    private readonly FlatButton _toggle, _pause, _newBlock, _copy, _undo;
    private readonly Label _status = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true, UseMnemonic = false };
    private readonly ContextMenuStrip _copyMenu;
    private readonly ContextMenuStrip _pauseMenu = new();

    // Week and day.
    private readonly WeekStrip _week;
    private readonly Card _dayCard;
    private readonly KeepScrollPanel _dayScroll = new() { Dock = DockStyle.Fill, AutoScroll = true };
    private readonly DayTimeline _timeline;

    // Editor.
    private readonly Card _inspector;
    private readonly Label _empty = new()
    {
        Dock = DockStyle.Fill, ForeColor = Theme.Muted, UseMnemonic = false,
        Text = "Select a block on the timeline to edit it.\n\nAdd one with + New block, or double-click an empty time on the timeline.\n\n" +
               "Blocks may overlap: the one that started last is on top, and the one below takes over again when it ends.",
    };
    private readonly Panel _editor = new() { Dock = DockStyle.Fill, AutoScroll = true, Visible = false };
    private readonly TextBox _name = new() { BorderStyle = BorderStyle.None, MaxLength = 40, PlaceholderText = "Name (optional)", AccessibleName = "Block name" };
    private readonly TimeBox _start = new() { AccessibleName = "Start time" };
    private readonly TimeBox _end = new() { AccessibleName = "End time" };
    private readonly InputFrame _startFrame, _endFrame;
    private readonly Label _duration = new() { AutoSize = false, Width = 90, TextAlign = ContentAlignment.MiddleLeft, ForeColor = Theme.Muted };
    private readonly Panel _problemRow = new() { Dock = DockStyle.Top, Height = 30, Visible = false };
    private readonly Label _problem = new() { Dock = DockStyle.Fill, ForeColor = Theme.Red, TextAlign = ContentAlignment.MiddleLeft, UseMnemonic = false };
    private readonly FlatButton _split;
    private readonly List<(FlatButton Tile, string? Mood)> _moodTiles = [];
    private readonly Picker _plans;
    private readonly Picker _action;
    private readonly FlatButton _enabled, _duplicate, _delete;

    public event Action? Changed; // the schedule or the status line changed (the sidebar may show it)
    /// <summary>The user asked to follow the schedule again (Resume): the Companion ends a manual mood choice too.</summary>
    public event Action? ResumeRequested;

    public SchedulePage(Theme theme, BuddyArt art, ToolTip tips, Action<LogKind, string> log, string directory)
    {
        _theme = theme;
        _art = art;
        _tips = tips;
        _log = log;
        _store = new ScheduleStore(directory);

        _toggle = new FlatButton(theme, ButtonLook.Pill) { Dock = DockStyle.Left, Width = 104, AccessibleName = "Live schedule on or off" };
        _pause = new FlatButton(theme, ButtonLook.Quiet) { Dock = DockStyle.Left, Width = 96, AccessibleName = "Pause or resume the schedule" };
        _newBlock = new FlatButton(theme, ButtonLook.Accent) { Text = "New block", Glyph = Theme.GlyphAdd, Dock = DockStyle.Right, Width = 136 };
        _copy = new FlatButton(theme, ButtonLook.Quiet) { Dock = DockStyle.Right, Width = 150, Glyph = Theme.GlyphCopy };
        _undo = new FlatButton(theme, ButtonLook.Quiet) { Text = "Undo delete", Dock = DockStyle.Right, Width = 116, Visible = false };
        _copyMenu = new ContextMenuStrip { ShowCheckMargin = true, ShowImageMargin = false, Font = theme.Text10 };
        _week = new WeekStrip(theme) { Dock = DockStyle.Top, Height = 56 };
        _timeline = new DayTimeline(theme, MoodLabel, mood => FaceFor(mood) is { } face ? art[face] : null) { Dock = DockStyle.Top };
        _dayCard = new Card(theme) { Dock = DockStyle.Fill, Padding = new Padding(8, 38, 4, 8) };
        _inspector = new Card(theme) { Title = "Block", Dock = DockStyle.Right, Width = InspectorWidth };
        _startFrame = new InputFrame(_start) { Width = 74, Height = 34, Padding = new Padding(8, 8, 8, 4), Margin = new Padding(0, 0, 8, 0) };
        _endFrame = new InputFrame(_end) { Width = 74, Height = 34, Padding = new Padding(8, 8, 8, 4), Margin = new Padding(8, 0, 8, 0) };
        _split = new FlatButton(theme, ButtonLook.Quiet) { Text = "Split over two days", Dock = DockStyle.Right, Width = 150 };
        _plans = new Picker(theme) { Dock = DockStyle.Top, Height = 32, AccessibleName = "Planned activity" };
        _action = new Picker(theme) { Dock = DockStyle.Top, Height = 32, AccessibleName = "Scheduled action" };
        _enabled = new FlatButton(theme, ButtonLook.Pill) { Width = 70, Height = 30, Margin = new Padding(0, 0, 10, 0), AccessibleName = "Block on or off" };
        _duplicate = new FlatButton(theme, ButtonLook.Quiet) { Text = "Duplicate", Width = 92, Height = 30, Margin = new Padding(0, 0, 4, 0) };
        _delete = new FlatButton(theme, ButtonLook.Quiet) { Text = "Delete", Width = 74, Height = 30, Margin = new Padding(0) };

        BuildHeader();
        BuildEditor();
        _dayScroll.Controls.Add(_timeline);
        NativeLook.DarkScrollBars(_dayScroll);
        _dayCard.Controls.Add(_dayScroll);
        var body = new Panel { Dock = DockStyle.Fill };
        body.Controls.Add(_dayCard); // fills what the inspector leaves
        body.Controls.Add(new Panel { Dock = DockStyle.Right, Width = 14 });
        body.Controls.Add(_inspector);

        Controls.Add(body);
        Controls.Add(new Panel { Dock = DockStyle.Top, Height = 12 });
        Controls.Add(_week);
        Controls.Add(new Panel { Dock = DockStyle.Top, Height = 6 });
        Controls.Add(BuildStatusRow());
        Controls.Add(BuildHeaderRow());

        _week.DaySelected += day => ShowDay(day, scrollToNow: true);
        _timeline.BlockSelected += id =>
        {
            _selected = id;
            _pendingEnd = null;
            BindEditor();
        };
        _timeline.CreateRequested += minute => AddBlock(ScheduleEdits.NewAt(minute));
        _timeline.DeleteRequested += DeleteSelected;
        _timeline.DragPreview += (_, start, end) =>
        {
            // The editor follows the drag; nothing is stored until the block is let go.
            _binding = true;
            _start.Value = start;
            _end.Value = end;
            _duration.Text = DayTime.Duration(end - start);
            _binding = false;
        };
        _timeline.DragCommitted += (id, start, end) =>
        {
            _pendingEnd = null;
            if (_selected == id)
            {
                Edit(b => b with { Start = start, End = end });
            }
        };
        _timeline.DragCancelled += BindEditor;
        _dayScroll.Resize += (_, _) => _timeline.Height = _timeline.PreferredHeight;
        _clock.Tick += (_, _) => RefreshNow();
        // The clock runs whether or not the page is shown: the header, the status line and the sidebar dot follow a pause
        // that runs out or a block that starts while another page is open.
        _clock.Start();
        VisibleChanged += (_, _) =>
        {
            if (Visible)
            {
                RefreshNow();
                BeginInvoke(ApplyScroll); // after the layout that showing the page causes
            }
        };
    }

    public WeekSchedule Schedule => _schedule;

    /// <summary>
    /// Switches the schedule Live or Off (saved at once), as the header's switch does. Either way a stored pause is
    /// cleared: Live on means effectively Live, and a pause from before Off never comes back later. A manual mood
    /// choice is left alone (unlike <see cref="Resume"/>).
    /// </summary>
    public void SetLive(bool live)
    {
        if (_schedule.Live != live || _schedule.PausedUntil is not null)
        {
            Save(_schedule.WithLive(live).WithPause(null));
        }
    }

    /// <summary>Pauses the schedule until then (<see cref="WeekSchedule.UntilResumed"/>: until resumed), saved at once.</summary>
    public void Pause(DateTimeOffset until) => Save(_schedule.WithPause(until));

    /// <summary>Ends a pause (saved at once); the Companion then follows the schedule again, ending a manual mood choice.</summary>
    public void Resume()
    {
        Save(_schedule.WithPause(null));
        ResumeRequested?.Invoke();
    }

    /// <summary>Reads the stored schedule (warnings go to the log) and shows today.</summary>
    public void LoadSchedule()
    {
        var (schedule, warning) = _store.Load();
        _schedule = schedule;
        if (warning is not null)
        {
            _log(LogKind.Warning, warning);
        }
        ShowDay(DateTime.Today.DayOfWeek, scrollToNow: true);
    }

    // ---- building ----

    private Panel BuildHeaderRow()
    {
        var row = new Panel { Dock = DockStyle.Top, Height = 36 };
        row.Controls.Add(new Panel { Dock = DockStyle.Fill }); // the room between the switches and the edit buttons
        row.Controls.Add(_pause);
        row.Controls.Add(new Panel { Dock = DockStyle.Left, Width = 6 });
        row.Controls.Add(_toggle);
        row.Controls.Add(_undo);
        row.Controls.Add(new Panel { Dock = DockStyle.Right, Width = 6 });
        row.Controls.Add(_copy);
        row.Controls.Add(new Panel { Dock = DockStyle.Right, Width = 8 });
        row.Controls.Add(_newBlock);
        return row;
    }

    // The status line, on a row of its own under the header buttons: the whole width, so it is read without a tooltip.
    private Panel BuildStatusRow()
    {
        var row = new Panel { Dock = DockStyle.Top, Height = 26 };
        _status.Font = _theme.Small;
        _status.ForeColor = Theme.Muted;
        _status.Padding = new Padding(4, 0, 0, 0);
        row.Controls.Add(_status);
        return row;
    }

    private void BuildHeader()
    {
        _toggle.Click += (_, _) => SetLive(!_schedule.Live);
        _pause.Click += (_, _) =>
        {
            if (_schedule.IsPaused(DateTimeOffset.Now))
            {
                Resume();
            }
            else
            {
                _pauseMenu.Show(_pause, new Point(0, _pause.Height));
            }
        };
        _newBlock.Click += (_, _) => AddBlock(ScheduleEdits.NewAt(NewBlockStart()));
        _undo.Click += (_, _) => UndoDelete();
        _copy.Click += (_, _) => OpenCopyMenu();
        _tips.SetToolTip(_toggle, "Live: Buddy follows the schedule (its moods and actions). Off: the schedule is kept, but asks Buddy for nothing");
        _tips.SetToolTip(_pause, "Pause: no scheduled moods or actions until then; nothing missed is done afterwards");
        _pauseMenu.Renderer = new DarkMenuRenderer();
        _pauseMenu.BackColor = Theme.Input;
        _pauseMenu.ForeColor = Theme.Text;
        // The same presets as the sidebar's quick controls (ScheduleStatus): one definition of every pause.
        void Preset(ToolStripItemCollection items, string label, ScheduleCommand command) =>
            items.Add(label, null, (_, _) => Pause(ScheduleStatus.PauseUntil(command, DateTimeOffset.Now, TimeZoneInfo.Local)!.Value));
        Preset(_pauseMenu.Items, "For 1 hour", new(ScheduleCommandKind.PauseHour));
        Preset(_pauseMenu.Items, "For the rest of today", new(ScheduleCommandKind.PauseToday));
        Preset(_pauseMenu.Items, "Until I resume", new(ScheduleCommandKind.PauseUntilResumed));
        var days = new ToolStripMenuItem("For days (holiday)") { ForeColor = Theme.Text };
        foreach (var n in ScheduleStatus.AwayDays)
        {
            var away = new ScheduleCommand(ScheduleCommandKind.Away, n);
            Preset(days.DropDownItems, ScheduleStatus.Label(away), away);
        }
        days.DropDown.BackColor = Theme.Input;
        days.DropDown.ForeColor = Theme.Text;
        _pauseMenu.Items.Add(days);
        _tips.SetToolTip(_newBlock, "A new hour-long block after the selected one (or at the next quarter)");

        _copyMenu.Renderer = new DarkMenuRenderer();
        _copyMenu.BackColor = Theme.Input;
        _copyMenu.ForeColor = Theme.Text;
        // Ticking a day keeps the menu open; only Copy (or clicking elsewhere) closes it.
        _copyMenu.Closing += (_, e) =>
        {
            if (e.CloseReason == ToolStripDropDownCloseReason.ItemClicked && _copyMenu.Items.OfType<ToolStripMenuItem>().Any(i => i.Selected && i.CheckOnClick))
            {
                e.Cancel = true;
            }
        };
    }

    private void BuildEditor()
    {
        _name.Font = _theme.Text10;
        _name.ForeColor = Theme.Text;
        var nameFrame = new InputFrame(_name) { Dock = DockStyle.Top, Height = 36, Padding = new Padding(10, 9, 8, 5) };
        _name.Leave += (_, _) => CommitName();
        _name.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Enter)
            {
                e.SuppressKeyPress = true;
                CommitName();
            }
        };

        foreach (var box in new[] { _start, _end })
        {
            box.Font = _theme.Text10;
            box.ForeColor = Theme.Text;
        }
        _duration.Font = _theme.Small;
        var timeRow = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 42, WrapContents = false, Padding = new Padding(0, 4, 0, 0) };
        var to = new Label { Text = "to", AutoSize = false, Width = 30, Height = 34, TextAlign = ContentAlignment.MiddleCenter, ForeColor = Theme.Muted, Margin = new Padding(0) };
        _duration.Height = 34;
        timeRow.Controls.AddRange([_startFrame, to, _endFrame, _duration]);
        _start.Committed += CommitStart;
        _end.Committed += CommitEnd;
        _start.ValidityChanged += ok => ShowTimeProblem(ok ? null : "Type a time like 08:30.");
        _end.ValidityChanged += ok => ShowTimeProblem(ok ? null : "Type a time like 17:00 (24:00 is midnight).");
        _tips.SetToolTip(_start, "Type a time, or use ↑/↓ (1 minute) and Shift+↑/↓ (15 minutes)");
        _tips.SetToolTip(_end, "Type a time, or use ↑/↓ (1 minute) and Shift+↑/↓ (15 minutes); 24:00 is midnight");

        _problem.Font = _theme.Small;
        _problemRow.Controls.Add(_problem);
        _problemRow.Controls.Add(_split);
        _split.Click += (_, _) => SplitOverMidnight();

        var moods = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 2 * (TileHeight + TileGap), WrapContents = true };
        foreach (var (label, mood, icon) in MoodChoices())
        {
            var tile = new FlatButton(_theme, ButtonLook.Tile)
            {
                Text = label, Icon = icon is null ? null : _art[icon], IconHeight = 21, Size = new Size(TileWidth, TileHeight),
                Margin = new Padding(0, 0, TileGap, TileGap), AccessibleName = $"Mood {label}",
            };
            tile.Click += (_, _) => Edit(b => b with { Mood = mood });
            _tips.SetToolTip(tile, mood is null ? "This block does not change Buddy's mood" : $"Buddy is {label.ToLowerInvariant()} during this block");
            _moodTiles.Add((tile, mood));
            moods.Controls.Add(tile);
        }

        _plans.Items.Add(new PlanChoice(null, "Nothing planned"));
        foreach (var type in ActivityTypes.All)
        {
            _plans.Items.Add(new PlanChoice(type.Id, type.Label));
        }
        _plans.Picked += (_, _) =>
        {
            if (!_binding && _plans.SelectedItem is PlanChoice choice)
            {
                Edit(b => b with { Plans = choice.Id });
            }
        };
        _tips.SetToolTip(_plans, "What you intend to do in this block. A plan, not a record: nothing is logged because of it");

        _action.Items.Add(new PlanChoice(null, "No action"));
        foreach (var action in ScheduledActions.All)
        {
            _action.Items.Add(new PlanChoice(action.Id, action.Label));
        }
        _action.Picked += (_, _) =>
        {
            if (!_binding && _action.SelectedItem is PlanChoice choice)
            {
                Edit(b => b with { Action = choice.Id });
            }
        };
        _tips.SetToolTip(_action, "Buddy does this once, at the block's start time (when the schedule is live). Questions wait up to 3 minutes for him to be free, shows 1 minute; otherwise it is skipped");

        var actions = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 38, WrapContents = false, Padding = new Padding(0, 6, 0, 0) };
        actions.Controls.AddRange([_enabled, _duplicate, _delete]);
        _enabled.Click += (_, _) => Edit(b => b with { Enabled = !b.Enabled });
        _duplicate.Click += (_, _) =>
        {
            if (SelectedBlock() is { } block)
            {
                AddBlock(ScheduleEdits.Duplicate(block));
            }
        };
        _delete.Click += (_, _) => DeleteSelected();
        _tips.SetToolTip(_enabled, "Off: the block stays in the schedule but does nothing");

        Stack(_editor, Caption("Name"), nameFrame, Caption("Time"), timeRow, _problemRow, Caption("Mood"), moods,
              Caption("Plans"), _plans, Caption("Action at the start"), _action, actions);
        NativeLook.DarkScrollBars(_editor);
        _inspector.Controls.Add(_editor);
        _inspector.Controls.Add(_empty);
        _empty.Font = _theme.Small;
    }

    private Label Caption(string text) => new()
    {
        Text = text, Dock = DockStyle.Top, Height = 20, ForeColor = Theme.Muted, Font = _theme.Small,
        TextAlign = ContentAlignment.BottomLeft, UseMnemonic = false, Padding = new Padding(0, 0, 0, 3),
    };

    // Adds controls to a docking parent top to bottom (Dock=Top stacks in reverse order of adding).
    private static void Stack(Control parent, params Control[] topToBottom)
    {
        for (var i = topToBottom.Length - 1; i >= 0; i--)
        {
            parent.Controls.Add(topToBottom[i]);
        }
    }

    private static IEnumerable<(string Label, string? Mood, string? Icon)> MoodChoices() =>
        new[] { ("None", (string?)null, (string?)null) }
            .Concat(Activities.All.Where(a => a.Kind == ActivityKind.Mood).Select(a => (a.Label, a.Mood, (string?)a.Icon)));

    private static string MoodLabel(string mood) =>
        Activities.All.FirstOrDefault(a => a.Kind == ActivityKind.Mood && a.Mood == mood)?.Label ?? mood;

    private static string? FaceFor(string mood) =>
        Activities.All.FirstOrDefault(a => a.Kind == ActivityKind.Mood && a.Mood == mood)?.Icon;

    private sealed record PlanChoice(string? Id, string Label)
    {
        public override string ToString() => Label;
    }

    // ---- showing ----

    private void ShowDay(DayOfWeek day, bool scrollToNow)
    {
        _day = day;
        _selected = null;
        _pendingEnd = null;
        RefreshAll();
        if (scrollToNow)
        {
            var blocks = _schedule[day];
            _scrollTo = day == DateTime.Today.DayOfWeek ? DateTime.Now.Hour * 60 + DateTime.Now.Minute
                        : blocks.IsEmpty ? 8 * 60 : blocks.Min(b => b.Start);
            ApplyScroll();
        }
    }

    // Scrolling a hidden panel does not stick: the position is kept until the page is on screen.
    private void ApplyScroll()
    {
        if (_scrollTo is not { } minute || !_dayScroll.Visible || !IsHandleCreated)
        {
            return;
        }
        _scrollTo = null;
        _timeline.Height = _timeline.PreferredHeight;
        _dayScroll.AutoScrollPosition = new Point(0, Math.Max(0, _timeline.Y(minute) - LogicalToDeviceUnits(60)));
    }

    private void RefreshAll()
    {
        _week.Today = DateTime.Today.DayOfWeek;
        _week.Show(_schedule, _day);
        _dayCard.Title = DayName(_day) + (_day == DateTime.Today.DayOfWeek ? " · today" : "");
        _dayCard.Invalidate();
        _copy.Text = $"Copy {DayName(_day)[..3]} to…";
        _copy.Invalidate();
        RefreshNow();
        BindEditor();
    }

    /// <summary>The parts that move with the clock: the status line, the now line and the lit blocks.</summary>
    private void RefreshNow()
    {
        var now = DateTime.Now;
        if (_week.Today != now.DayOfWeek)
        {
            _week.Today = now.DayOfWeek;
            _week.Invalidate();
        }
        var today = _day == now.DayOfWeek;
        var snapshot = SchedulePlanner.Resolve(_schedule, now);
        var active = today ? snapshot.Covering.Select(b => b.Id).ToImmutableHashSet() : [];
        _timeline.Show(_schedule[_day], _selected, active, today ? now.Hour * 60 + now.Minute : null);
        _status.Text = StatusText(now, snapshot);
        var paused = _schedule.IsPaused(DateTimeOffset.Now);
        _status.ForeColor = _schedule.Live && !paused ? Theme.Muted : Theme.Faint;
        _toggle.Text = _schedule.Live ? "Live" : "Off";
        _toggle.DotColor = !_schedule.Live ? Theme.Faint : paused ? Theme.Amber : Theme.Green;
        _toggle.Invalidate();
        _pause.Visible = _schedule.Live;
        _pause.Text = paused ? "Resume" : "Pause…";
        _pause.Invalidate();
        Changed?.Invoke();
    }

    private string StatusText(DateTime now, ScheduleSnapshot snapshot)
    {
        if (!_schedule.Live)
        {
            return "Off: asks Buddy for nothing. Switch to Live to apply it.";
        }
        if (_schedule.PausedUntil is { } until && DateTimeOffset.Now < until)
        {
            return until == WeekSchedule.UntilResumed ? "Paused until you resume: no scheduled moods or actions."
                : $"Paused until {until.ToLocalTime():ddd HH:mm}: no scheduled moods or actions.";
        }
        var text = $"Now {now:HH:mm} · " + (snapshot.MoodBlock is { } block
            ? $"{MoodLabel(block.Mood!)} ({Describe(block)})"
            : "no mood scheduled: Buddy keeps his mood");
        if (snapshot.PlanBlock is { Plans: { } plans })
        {
            text += $" · planned: {ActivityTypes.Label(plans)}";
        }
        if (SchedulePlanner.NextMoodChange(_schedule, now) is { } next)
        {
            var nextMood = SchedulePlanner.Resolve(_schedule, next).Mood;
            var when = next.Date == now.Date ? $"{next:HH:mm}" : $"{next:ddd HH:mm}";
            text += $" · next {when}: {(nextMood is null ? "keeps his mood" : MoodLabel(nextMood))}";
        }
        return text;
    }

    private static string Describe(ScheduleBlock b) =>
        (b.Name.Length > 0 ? b.Name + " " : "") + $"{DayTime.Format(b.Start)}–{DayTime.Format(b.End)}";

    private static string DayName(DayOfWeek day) => day.ToString();

    private ScheduleBlock? SelectedBlock() => _selected is { } id ? _schedule[_day].FirstOrDefault(b => b.Id == id) : null;

    private void BindEditor()
    {
        var block = SelectedBlock();
        _editor.Visible = block is not null;
        _empty.Visible = block is null;
        _inspector.Title = block is null ? "Block" : $"Block · {DayName(_day)}";
        _inspector.Invalidate();
        if (block is null)
        {
            return;
        }
        _binding = true;
        try
        {
            if (!_name.Focused)
            {
                _name.Text = block.Name;
            }
            _start.Value = block.Start;
            _end.Value = block.End;
            _duration.Text = DayTime.Duration(block.Duration);
            foreach (var (tile, mood) in _moodTiles)
            {
                tile.Selected = mood == block.Mood;
            }
            _plans.SelectedIndex = Math.Max(0, _plans.Items.FindIndex(i => i is PlanChoice c && c.Id == block.Plans));
            if (block.Plans is { } unknown && _plans.SelectedIndex == 0)
            {
                _plans.Items.Add(new PlanChoice(unknown, unknown)); // written by a newer version: shown as it is
                _plans.SelectedIndex = _plans.Items.Count - 1;
            }
            _action.SelectedIndex = Math.Max(0, _action.Items.FindIndex(i => i is PlanChoice c && c.Id == block.Action));
            _enabled.Text = block.Enabled ? "On" : "Off";
            _enabled.DotColor = block.Enabled ? Theme.Green : Theme.Faint;
            _enabled.Invalidate();
            if (_pendingEnd is null)
            {
                ShowTimeProblem(null);
            }
        }
        finally
        {
            _binding = false;
        }
    }

    private void ShowTimeProblem(string? text, bool offerSplit = false)
    {
        _problem.Text = text ?? "";
        _split.Visible = offerSplit;
        _problemRow.Visible = text is not null;
        _startFrame.Invalidate();
        _endFrame.Invalidate();
    }

    // ---- editing ----

    private void Save(WeekSchedule next)
    {
        _schedule = next;
        _undo.Visible = _deleted is not null;
        try
        {
            _store.Save(next);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            _log(LogKind.Warning, $"Schedule: could not be saved ({e.Message})");
        }
        RefreshAll();
    }

    private void Edit(Func<ScheduleBlock, ScheduleBlock> change)
    {
        if (_binding || SelectedBlock() is not { } block)
        {
            return;
        }
        var edited = change(block);
        if (edited == block)
        {
            return;
        }
        if (WeekSchedule.Problem(edited) is { } problem)
        {
            BindEditor(); // back to what is stored...
            ShowTimeProblem(problem); // ...and why the change was not taken
            return;
        }
        _deleted = null;
        Save(_schedule.Put(_day, edited));
    }

    private void CommitName()
    {
        if (!_binding)
        {
            Edit(b => b with { Name = _name.Text.Trim() });
        }
    }

    // Moving the start past the end keeps the block's length (as far as 24:00 allows).
    private void CommitStart(int start)
    {
        if (_binding || SelectedBlock() is not { } block)
        {
            return;
        }
        if (start >= DayTime.Minutes)
        {
            BindEditor();
            ShowTimeProblem("A block starts at 23:59 at the latest.");
            return;
        }
        _pendingEnd = null;
        var end = start < block.End ? block.End : Math.Min(start + block.Duration, DayTime.Minutes);
        Edit(b => b with { Start = start, End = end });
    }

    // An end at or before the start: past midnight (offer to split it over two days), or a block of no length.
    private void CommitEnd(int end)
    {
        if (_binding || SelectedBlock() is not { } block)
        {
            return;
        }
        if (end > block.Start)
        {
            _pendingEnd = null;
            Edit(b => b with { End = end });
            return;
        }
        if (end == block.Start)
        {
            BindEditor();
            ShowTimeProblem("A block needs at least a minute.");
            return;
        }
        _pendingEnd = end;
        ShowTimeProblem($"Past midnight ({DayTime.Format(end)})", offerSplit: true);
    }

    private void SplitOverMidnight()
    {
        if (SelectedBlock() is not { } block || _pendingEnd is not { } end)
        {
            return;
        }
        var (today, tomorrow) = ScheduleEdits.SplitAtMidnight(block, block.Start, end);
        _pendingEnd = null;
        _deleted = null;
        Save(_schedule.Put(_day, today).Put(ScheduleEdits.Next(_day), tomorrow));
        _log(LogKind.Info, $"Schedule: {Describe(today)} on {DayName(_day)} continues on {DayName(ScheduleEdits.Next(_day))} until {DayTime.Format(end)}");
    }

    private int NewBlockStart()
    {
        if (SelectedBlock() is { } block && block.End < DayTime.Minutes)
        {
            return block.End;
        }
        if (_day == DateTime.Today.DayOfWeek)
        {
            var now = DateTime.Now.Hour * 60 + DateTime.Now.Minute;
            return Math.Min((now + 14) / 15 * 15, DayTime.Minutes - 60);
        }
        return 9 * 60;
    }

    private void AddBlock(ScheduleBlock block)
    {
        _deleted = null;
        _pendingEnd = null;
        _selected = block.Id;
        Save(_schedule.Put(_day, block));
        _timeline.Focus();
    }

    private void DeleteSelected()
    {
        var blocks = _schedule[_day];
        var index = _selected is { } id ? blocks.Select(b => b.Id).ToList().IndexOf(id) : -1;
        if (index < 0)
        {
            return;
        }
        _deleted = (_day, index, blocks[index]);
        _selected = null;
        Save(_schedule.Remove(_day, blocks[index].Id));
    }

    private void UndoDelete()
    {
        if (_deleted is not { } d)
        {
            return;
        }
        _deleted = null;
        var blocks = _schedule[d.Day];
        _day = d.Day;
        _selected = d.Block.Id;
        Save(_schedule.WithDay(d.Day, blocks.Insert(Math.Min(d.Index, blocks.Length), d.Block)));
    }

    private void OpenCopyMenu()
    {
        while (_copyMenu.Items.Count > 0)
        {
            _copyMenu.Items[0].Dispose();
        }
        _copyMenu.Items.Add(new ToolStripLabel($"Copy {DayName(_day)} to:") { ForeColor = Theme.Muted });
        var days = new List<(ToolStripMenuItem Item, DayOfWeek Day)>();
        foreach (var day in WeekSchedule.Week.Where(d => d != _day))
        {
            var item = new ToolStripMenuItem(DayName(day) + (_schedule[day].IsEmpty ? "" : $"  ({_schedule[day].Length} blocks)"))
            {
                CheckOnClick = true, ForeColor = Theme.Text,
            };
            days.Add((item, day));
            _copyMenu.Items.Add(item);
        }
        _copyMenu.Items.Add(new ToolStripSeparator());
        var copy = new ToolStripMenuItem("Copy (replaces those days)") { ForeColor = Theme.Amber };
        copy.Click += (_, _) => CopyTo(days.Where(d => d.Item.Checked).Select(d => d.Day).ToList());
        _copyMenu.Items.Add(copy);
        _copyMenu.Show(_copy, new Point(0, _copy.Height));
    }

    private void CopyTo(List<DayOfWeek> targets)
    {
        if (targets.Count == 0)
        {
            return;
        }
        var replaced = targets.Where(d => !_schedule[d].IsEmpty).Select(DayName).ToList();
        if (replaced.Count > 0 &&
            MessageBox.Show(FindForm(), $"This replaces the blocks on {string.Join(", ", replaced)} with {DayName(_day)}'s.",
                            "Copy day", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK)
        {
            return;
        }
        _deleted = null;
        Save(_schedule.CopyDay(_day, targets));
        _log(LogKind.Info, $"Schedule: {DayName(_day)} copied to {string.Join(", ", targets.Select(DayName))}");
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _clock.Dispose();
            _copyMenu.Dispose();
            _pauseMenu.Dispose();
        }
        base.Dispose(disposing);
    }
}
