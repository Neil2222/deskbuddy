using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;

namespace DeskBuddy.Companion;

/// <summary>
/// The History page: Buddy's day journal. A day at a time (arrows, Today, a month with the days that have entries), its
/// four numbers, the entries in time order with today's plan above them, and the selected entry in words on the right.
/// Board and simulator each have their own history; the page shows one of them. It reads the recorder's published
/// <see cref="HistoryState"/> only, follows new events as they are recorded and keeps the selection and the scroll
/// position while it does.
/// </summary>
public sealed class HistoryPage : Panel
{
    private const int InspectorMin = 250, InspectorMax = 340; // logical pixels; about a third of the page between them

    private readonly Theme _theme;
    private readonly ToolTip _tips;
    private readonly Func<HistorySource, HistoryState> _state;
    private readonly Func<bool> _simulatorAvailable;
    private readonly Func<WeekSchedule> _schedule;
    private readonly System.Windows.Forms.Timer _clock = new() { Interval = 30_000 };
    private readonly System.Windows.Forms.Timer _coalesce = new() { Interval = 150 };

    private HistorySource _source = HistorySource.Board;
    private readonly TimeProvider _time;
    private DateOnly _date;
    private DateOnly _shownToday; // to follow midnight while today is shown
    private readonly HashSet<string> _openGroups = [];
    private bool _technicalOpen;
    private bool _dirty = true;
    private HistoryState? _shownState;
    private string? _inspected; // the selected row's key, as last shown in the inspector
    private JournalRow? _inspectedRow;
    private WeekSchedule? _shownSchedule;

    private readonly FlatButton _prev, _next, _today, _dateButton;
    private readonly Picker _sourcePicker;
    private readonly Label _status = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleRight, AutoEllipsis = true, UseMnemonic = false };
    private readonly StatView[] _stats;
    private readonly Card _dayCard;
    private readonly KeepScrollPanel _dayScroll = new() { Dock = DockStyle.Fill, AutoScroll = true };
    private readonly JournalView _journal;
    private readonly Card _inspectorCard;
    private readonly Panel _inspectorScroll = new() { Dock = DockStyle.Fill, AutoScroll = true };
    private readonly InspectorView _inspector;
    private readonly ToolStripDropDown _calendarDrop = new() { Padding = Padding.Empty, BackColor = Theme.Input };
    private readonly MonthView _calendar;

    public HistoryPage(Theme theme, BuddyArt art, ToolTip tips, Func<HistorySource, HistoryState> state, Func<bool> simulatorAvailable,
                       Func<WeekSchedule> schedule, TimeProvider? time = null)
    {
        _theme = theme;
        _tips = tips;
        _state = state;
        _simulatorAvailable = simulatorAvailable;
        _schedule = schedule;
        _time = time ?? TimeProvider.System;
        _date = _shownToday = Today;

        _prev = new FlatButton(theme, ButtonLook.Quiet) { Glyph = Theme.GlyphChevronLeft, Dock = DockStyle.Left, Width = 36, AccessibleName = "Previous day" };
        _dateButton = new FlatButton(theme, ButtonLook.Pill) { Dock = DockStyle.Left, Width = 196, AccessibleName = "Choose a day", DotColor = Theme.Cyan };
        _next = new FlatButton(theme, ButtonLook.Quiet) { Glyph = Theme.GlyphChevronRight, Dock = DockStyle.Left, Width = 36, AccessibleName = "Next day" };
        _today = new FlatButton(theme, ButtonLook.Quiet) { Text = "Today", Dock = DockStyle.Left, Width = 76 };
        _sourcePicker = new Picker(theme) { Dock = DockStyle.Right, Width = 150, AccessibleName = "Whose history", Visible = false };
        _sourcePicker.Items.Add("DeskBuddy");
        _sourcePicker.Items.Add("Simulator");
        _sourcePicker.SelectedIndex = 0;
        _stats = Enumerable.Range(0, 4).Select(_ => new StatView(theme) { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 10, 0) }).ToArray();
        _journal = new JournalView(theme, art) { Dock = DockStyle.Top };
        _dayCard = new Card(theme) { Dock = DockStyle.Fill, Padding = new Padding(8, 38, 4, 8) };
        _inspector = new InspectorView(theme, art) { Dock = DockStyle.Top };
        _inspectorCard = new Card(theme) { Title = "Details", Dock = DockStyle.Right, Width = InspectorMax, Padding = new Padding(16, 40, 8, 12) };
        _calendar = new MonthView(theme);
        _calendarDrop.Items.Add(new ToolStripControlHost(_calendar) { Padding = Padding.Empty, Margin = Padding.Empty, AutoSize = false, Size = _calendar.Size });

        _dayScroll.Controls.Add(_journal);
        NativeLook.DarkScrollBars(_dayScroll);
        _dayCard.Controls.Add(_dayScroll);
        _inspectorScroll.Controls.Add(_inspector);
        NativeLook.DarkScrollBars(_inspectorScroll);
        _inspectorCard.Controls.Add(_inspectorScroll);

        var body = new Panel { Dock = DockStyle.Fill };
        body.Controls.Add(_dayCard);
        body.Controls.Add(new Panel { Dock = DockStyle.Right, Width = 14 });
        body.Controls.Add(_inspectorCard);
        // The journal keeps most of a narrow window; the details take about a third.
        body.Resize += (_, _) =>
        {
            var width = Math.Clamp(body.Width * 36 / 100, LogicalToDeviceUnits(InspectorMin), LogicalToDeviceUnits(InspectorMax));
            if (_inspectorCard.Width != width)
            {
                _inspectorCard.Width = width;
            }
        };

        Controls.Add(body);
        Controls.Add(new Panel { Dock = DockStyle.Top, Height = 12 });
        Controls.Add(BuildStats());
        Controls.Add(new Panel { Dock = DockStyle.Top, Height = 12 });
        Controls.Add(BuildHeader());

        tips.SetToolTip(_today, "Back to today");
        tips.SetToolTip(_dateButton, "Pick a day: days with entries have a dot");
        tips.SetToolTip(_sourcePicker, "The board's history and the simulator's are kept apart");

        _prev.Click += (_, _) => ShowDate(_date.AddDays(-1));
        _next.Click += (_, _) => ShowDate(_date.AddDays(1));
        _today.Click += (_, _) => ShowDate(Today);
        _dateButton.Click += (_, _) => OpenCalendar();
        _calendar.Picked += day =>
        {
            _calendarDrop.Close();
            ShowDate(day);
        };
        _sourcePicker.Picked += (_, _) => ShowSource(_sourcePicker.SelectedIndex == 1 ? HistorySource.Simulator : HistorySource.Board);
        _journal.SelectionChanged += row => ShowInspector(row);
        _journal.Toggled += Toggle;
        _dayScroll.Resize += (_, _) => FitJournal();
        _inspectorScroll.Resize += (_, _) => _inspector.Width = _inspectorScroll.ClientSize.Width - 4;
        _coalesce.Tick += (_, _) =>
        {
            _coalesce.Stop();
            if (Visible)
            {
                Refresh();
            }
        };
        _clock.Tick += (_, _) =>
        {
            if (Today != _shownToday && _date == _shownToday)
            {
                _date = Today; // midnight passed while today was shown: today is the new day
            }
            if (Visible)
            {
                Refresh(force: true);
            }
        };
        _clock.Start();
        VisibleChanged += (_, _) =>
        {
            if (Visible)
            {
                Refresh(force: _dirty);
            }
        };
    }

    /// <summary>Shows the board's or the simulator's history (each its own; never mixed).</summary>
    public void ShowSource(HistorySource source)
    {
        if (source == _source)
        {
            return;
        }
        _source = source;
        _sourcePicker.SelectedIndex = source == HistorySource.Simulator ? 1 : 0;
        _openGroups.Clear();
        _technicalOpen = false;
        _journal.Select(null, raise: false);
        ShowInspector(null);
        Refresh(force: true, scrollTop: true);
    }

    private TimeZoneInfo Zone => _time.LocalTimeZone;
    private DateTime Now => _time.GetLocalNow().DateTime;
    private DateOnly Today => DateOnly.FromDateTime(Now);

    /// <summary>The selected entry's row (null: none); for tests and accessibility.</summary>
    public JournalRow? Selected => _journal.SelectedRow;
    public IReadOnlyList<JournalRow> Rows => _journal.Rows;
    public DateOnly Date => _date;
    public HistorySource Source => _source;
    public InspectorInfo? Inspected { get; private set; }

    /// <summary>Selects an entry or plan block by its row key, as a click does.</summary>
    public void SelectEntry(string? key) => _journal.Select(key);

    /// <summary>Opens or closes a group of shows (by its key) or the technical list ("technical"), as a click does.</summary>
    public void Toggle(string key)
    {
        if (key == "technical")
        {
            _technicalOpen = !_technicalOpen;
        }
        else if (!_openGroups.Remove(key))
        {
            _openGroups.Add(key);
        }
        Refresh(force: true);
    }

    /// <summary>The source's history changed (on the UI thread): shown soon if it is the one on the page.</summary>
    public void HistoryChanged(HistorySource source)
    {
        if (source != _source)
        {
            return;
        }
        _dirty = true;
        if (Visible)
        {
            _coalesce.Stop();
            _coalesce.Start(); // several events in a burst (a replay) are shown once
        }
    }

    /// <summary>The schedule changed: today's plan follows it.</summary>
    public void ScheduleChanged()
    {
        if (ReferenceEquals(_schedule(), _shownSchedule))
        {
            return; // only the Schedule page's clock ticked
        }
        if (Visible && _date == Today)
        {
            Refresh(force: true);
        }
        else
        {
            _dirty = true;
        }
    }

    public void ShowDate(DateOnly date)
    {
        date = date > Today ? Today : date;
        if (date == _date)
        {
            return;
        }
        _date = date;
        _openGroups.Clear();
        _technicalOpen = false;
        _journal.Select(null, raise: false);
        ShowInspector(null);
        Refresh(force: true, scrollTop: true);
    }

    private Panel BuildHeader()
    {
        var row = new Panel { Dock = DockStyle.Top, Height = 36 };
        _status.Font = _theme.Small;
        _status.ForeColor = Theme.Faint;
        _status.Padding = new Padding(0, 0, 12, 0);
        row.Controls.Add(_status);
        row.Controls.Add(_today);
        row.Controls.Add(new Panel { Dock = DockStyle.Left, Width = 8 });
        row.Controls.Add(_next);
        row.Controls.Add(new Panel { Dock = DockStyle.Left, Width = 4 });
        row.Controls.Add(_dateButton);
        row.Controls.Add(new Panel { Dock = DockStyle.Left, Width = 4 });
        row.Controls.Add(_prev);
        row.Controls.Add(_sourcePicker);
        return row;
    }

    private TableLayoutPanel BuildStats()
    {
        var table = new TableLayoutPanel { Dock = DockStyle.Top, Height = 82, ColumnCount = 4, RowCount = 1, Margin = Padding.Empty, Padding = Padding.Empty };
        for (var i = 0; i < 4; i++)
        {
            table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
            _stats[i].Margin = new Padding(0, 0, i < 3 ? 10 : 0, 0);
            table.Controls.Add(_stats[i], i, 0);
        }
        table.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        return table;
    }

    private void OpenCalendar()
    {
        var state = _state(_source);
        _calendar.Show(_date, Today, HistoryView.Days(state.Items, Zone));
        if (_calendarDrop.Items[0] is ToolStripControlHost host)
        {
            host.Size = _calendar.Size;
        }
        _calendarDrop.Show(_dateButton, new Point(0, _dateButton.Height + 2));
    }

    private void FitJournal()
    {
        _journal.Width = _dayScroll.ClientSize.Width;
        var h = Math.Max(_journal.PreferredHeight, _dayScroll.ClientSize.Height);
        if (_journal.Height != h)
        {
            _journal.Height = h;
        }
    }

    /// <summary>
    /// Shows the day again from the source's current state. Without <paramref name="force"/> nothing is redone when
    /// the state is the one already shown. The selection and the scroll position stay unless <paramref name="scrollTop"/>.
    /// </summary>
    private void Refresh(bool force = false, bool scrollTop = false)
    {
        _sourcePicker.Visible = _simulatorAvailable() || _source == HistorySource.Simulator;
        var state = _state(_source);
        if (!force && ReferenceEquals(state, _shownState))
        {
            return;
        }
        _shownState = state;
        _dirty = false;
        var today = Today;
        _shownToday = today;
        var zone = Zone;

        var day = HistoryView.Day(state.Items, state.Snapshot, _date, zone);
        _shownSchedule = _schedule();
        var plan = HistoryView.TodayPlan(_shownSchedule, _date, today);
        var rows = HistoryJournal.Rows(day, plan, _date == today, _openGroups, _technicalOpen, Now);
        if (!state.Loaded)
        {
            rows = [new JournalRow(JournalRowKind.Header, "h:day", "Reading the history…")];
        }

        _dateButton.Text = _date == today ? $"Today · {_date.ToString("ddd d MMM", System.Globalization.CultureInfo.InvariantCulture)}"
                                          : HistoryJournal.DateLabel(_date, today);
        _dateButton.DotColor = _date == today ? Theme.Cyan : Theme.Faint;
        _dateButton.Invalidate();
        _next.Enabled = _date < today;
        _today.Enabled = _date != today;
        _dayCard.Title = _date.ToString("dddd d MMMM yyyy", System.Globalization.CultureInfo.InvariantCulture) +
                         (_source == HistorySource.Simulator ? " · simulator" : "");
        _dayCard.Invalidate();
        _status.Text = state.Loaded ? "" : "Reading…";

        var tiles = HistoryJournal.Tiles(day.Stats);
        for (var i = 0; i < 4; i++)
        {
            _stats[i].Show(tiles[i]);
            _tips.SetToolTip(_stats[i], tiles[i].Detail); // the whole breakdown where the tile cuts it short
        }

        var scroll = _dayScroll.AutoScrollPosition;
        _dayScroll.SuspendLayout();
        _journal.Zone = zone;
        _journal.Show(rows);
        FitJournal();
        _dayScroll.ResumeLayout();
        _dayScroll.AutoScrollPosition = scrollTop ? Point.Empty : new Point(-scroll.X, -scroll.Y);

        // The inspector follows its entry (a question that just got its answer), and empties when the entry is gone.
        var selected = _journal.SelectedRow;
        if (selected?.Key != _inspected || selected != _inspectedRow)
        {
            ShowInspector(selected, keepTechnical: selected?.Key == _inspected);
        }
    }

    private void ShowInspector(JournalRow? row, bool keepTechnical = false)
    {
        _inspected = row?.Key;
        _inspectedRow = row;
        _inspector.Width = _inspectorScroll.ClientSize.Width - 4;
        Inspected = row is null ? null : HistoryJournal.Describe(row, Zone);
        _inspector.Show(Inspected, keepTechnical);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _clock.Dispose();
            _coalesce.Dispose();
            _calendarDrop.Dispose();
        }
        base.Dispose(disposing);
    }
}
