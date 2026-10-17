using System.Globalization;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;

namespace DeskBuddy.Companion;

/// <summary>
/// The History page: Buddy's day journal. A day at a time (arrows, Today, a month with the days that have entries), its
/// four numbers, the entries in time order with today's plan above them, and the selected entry in words on the right.
/// Board and simulator each have their own history; the page shows one of them. It reads the source's published
/// <see cref="HistoryState"/> only, follows new events as they are recorded and keeps the selection and the scroll
/// position while it does.
/// <para>
/// Test data (P7.3): an entry can be hidden as test data and restored; a period can be hidden after a preview of exactly
/// what it covers and an explicit confirmation. Hides are annotations of the source (never its logs), written before
/// the page says anything happened; "Show hidden entries" shows them marked.
/// </para>
/// </summary>
public sealed class HistoryPage : Panel
{
    private const int InspectorMin = 250, InspectorMax = 340; // logical pixels; about a third of the page between them

    private readonly Theme _theme;
    private readonly ToolTip _tips;
    private readonly Func<HistorySource, IHistoryAccess> _access;
    private readonly Func<bool> _simulatorAvailable;
    private readonly Func<WeekSchedule> _schedule;
    private readonly Func<string, string, string, bool> _confirm;
    private readonly System.Windows.Forms.Timer _clock = new() { Interval = 30_000 };
    private readonly System.Windows.Forms.Timer _coalesce = new() { Interval = 150 };
    private readonly System.Windows.Forms.Timer _noticeTimer = new() { Interval = 20_000 };

    private HistorySource _source = HistorySource.Board;
    private readonly TimeProvider _time;
    private DateOnly _date;
    private DateOnly _shownToday; // to follow midnight while today is shown
    private readonly HashSet<string> _openGroups = [];
    private bool _technicalOpen;
    private bool _showHidden;
    private bool _busy; // an annotation is being written
    private bool _dirty = true;
    private HistoryState? _shownState;
    private string? _inspected; // the selected row's key, as last shown in the inspector
    private JournalRow? _inspectedRow;
    private WeekSchedule? _shownSchedule;
    private (HistorySource Source, IReadOnlyList<string> HideIds)? _undo; // what the notice's Undo withdraws

    private readonly FlatButton _prev, _next, _today, _dateButton, _testData;
    private readonly Picker _sourcePicker;
    private readonly ContextMenuStrip _testMenu = new();
    private readonly ToolStripMenuItem _showHiddenItem, _hidePeriodItem, _hiddenListItem;
    private readonly ContextMenuStrip _entryMenu = new();
    private readonly Label _status = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleRight, AutoEllipsis = true, UseMnemonic = false };
    private readonly Panel _noticeBar = new() { Dock = DockStyle.Top, Height = 38, Visible = false, Padding = new Padding(0, 6, 0, 0) };
    private readonly Label _noticeText = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true, UseMnemonic = false };
    private readonly FlatButton _noticeUndo, _noticeClose;
    private readonly StatView[] _stats;
    private readonly Card _dayCard;
    private readonly KeepScrollPanel _dayScroll = new() { Dock = DockStyle.Fill, AutoScroll = true };
    private readonly JournalView _journal;
    private readonly Card _inspectorCard;
    private readonly Panel _inspectorScroll = new() { Dock = DockStyle.Fill, AutoScroll = true };
    private readonly InspectorView _inspector;
    private readonly Panel _actionRow = new() { Dock = DockStyle.Bottom, Height = 44, Padding = new Padding(0, 8, 0, 0), Visible = false };
    private readonly FlatButton _action;
    private readonly ToolStripDropDown _calendarDrop = new() { Padding = Padding.Empty, BackColor = Theme.Input };
    private readonly MonthView _calendar;

    /// <param name="confirm">Asks (title, text, OK button) and returns the answer; the window's dialog by default.</param>
    public HistoryPage(Theme theme, BuddyArt art, ToolTip tips, Func<HistorySource, IHistoryAccess> access, Func<bool> simulatorAvailable,
                       Func<WeekSchedule> schedule, TimeProvider? time = null, Func<string, string, string, bool>? confirm = null)
    {
        _theme = theme;
        _tips = tips;
        _access = access;
        _simulatorAvailable = simulatorAvailable;
        _schedule = schedule;
        _time = time ?? TimeProvider.System;
        _confirm = confirm ?? ((title, text, ok) => ConfirmDialog.Ask(FindForm()!, _theme, title, text, ok));
        _date = _shownToday = Today;

        _prev = new FlatButton(theme, ButtonLook.Quiet) { Glyph = Theme.GlyphChevronLeft, Dock = DockStyle.Left, Width = 36, AccessibleName = "Previous day" };
        _dateButton = new FlatButton(theme, ButtonLook.Pill) { Dock = DockStyle.Left, Width = 196, AccessibleName = "Choose a day", DotColor = Theme.Cyan };
        _next = new FlatButton(theme, ButtonLook.Quiet) { Glyph = Theme.GlyphChevronRight, Dock = DockStyle.Left, Width = 36, AccessibleName = "Next day" };
        _today = new FlatButton(theme, ButtonLook.Quiet) { Text = "Today", Dock = DockStyle.Left, Width = 76 };
        _testData = new FlatButton(theme, ButtonLook.Quiet) { Text = "Test data", Glyph = Theme.GlyphChevronDown, Dock = DockStyle.Right, Width = 118, AccessibleName = "Test data" };
        _sourcePicker = new Picker(theme) { Dock = DockStyle.Right, Width = 150, AccessibleName = "Whose history", Visible = false };
        _sourcePicker.Items.Add("DeskBuddy");
        _sourcePicker.Items.Add("Simulator");
        _sourcePicker.SelectedIndex = 0;
        _noticeUndo = new FlatButton(theme, ButtonLook.Quiet) { Text = "Undo", Dock = DockStyle.Right, Width = 76, BackColor = Theme.Window };
        _noticeClose = new FlatButton(theme, ButtonLook.Quiet) { Text = "×", Dock = DockStyle.Right, Width = 36, AccessibleName = "Close notice", BackColor = Theme.Window };
        _stats = Enumerable.Range(0, 4).Select(_ => new StatView(theme) { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 10, 0) }).ToArray();
        _journal = new JournalView(theme, art) { Dock = DockStyle.Top };
        _dayCard = new Card(theme) { Dock = DockStyle.Fill, Padding = new Padding(8, 38, 4, 8) };
        _inspector = new InspectorView(theme, art) { Dock = DockStyle.Top };
        _inspectorCard = new Card(theme) { Title = "Details", Dock = DockStyle.Right, Width = InspectorMax, Padding = new Padding(16, 40, 8, 12) };
        _action = new FlatButton(theme, ButtonLook.Pill) { Dock = DockStyle.Left, Width = 170, DotColor = Theme.Amber };
        _calendar = new MonthView(theme);
        _calendarDrop.Items.Add(new ToolStripControlHost(_calendar) { Padding = Padding.Empty, Margin = Padding.Empty, AutoSize = false, Size = _calendar.Size });

        foreach (var menu in new[] { _testMenu, _entryMenu })
        {
            menu.Renderer = new DarkMenuRenderer();
            menu.BackColor = Theme.Input;
            menu.ForeColor = Theme.Text;
            menu.Font = theme.Text10;
        }
        _testMenu.ShowCheckMargin = true;
        _testMenu.ShowImageMargin = false;
        _showHiddenItem = new ToolStripMenuItem("Show hidden entries") { CheckOnClick = true, ForeColor = Theme.Text };
        _hidePeriodItem = new ToolStripMenuItem("Hide a period as test…") { ForeColor = Theme.Text };
        _hiddenListItem = new ToolStripMenuItem("Hidden entries…") { ForeColor = Theme.Text };
        _testMenu.Items.AddRange([_showHiddenItem, _hidePeriodItem, new ToolStripSeparator(), _hiddenListItem]);

        _dayScroll.Controls.Add(_journal);
        NativeLook.DarkScrollBars(_dayScroll);
        _dayCard.Controls.Add(_dayScroll);
        _inspectorScroll.Controls.Add(_inspector);
        NativeLook.DarkScrollBars(_inspectorScroll);
        _actionRow.Controls.Add(_action);
        _inspectorCard.Controls.Add(_inspectorScroll);
        _inspectorCard.Controls.Add(_actionRow);

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

        _noticeText.Font = theme.Small;
        _noticeBar.Controls.Add(_noticeText);
        _noticeBar.Controls.Add(_noticeUndo);
        _noticeBar.Controls.Add(_noticeClose);

        Controls.Add(body);
        Controls.Add(new Panel { Dock = DockStyle.Top, Height = 12 });
        Controls.Add(BuildStats());
        Controls.Add(new Panel { Dock = DockStyle.Top, Height = 12 });
        Controls.Add(_noticeBar);
        Controls.Add(BuildHeader());

        tips.SetToolTip(_today, "Back to today");
        tips.SetToolTip(_dateButton, "Pick a day: days with entries have a dot");
        tips.SetToolTip(_sourcePicker, "The board's history and the simulator's are kept apart");
        tips.SetToolTip(_testData, "Hide test sessions from the History (nothing is deleted), show or restore them");

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
        _testData.Click += (_, _) =>
        {
            _hiddenListItem.Text = $"Hidden entries ({State.Hides.Hides.Count})…";
            _showHiddenItem.Checked = _showHidden;
            _testMenu.Show(_testData, new Point(0, _testData.Height));
        };
        _showHiddenItem.Click += (_, _) => ShowHidden = _showHiddenItem.Checked;
        _hidePeriodItem.Click += (_, _) => OpenHidePeriod();
        _hiddenListItem.Click += (_, _) => OpenHiddenList();
        _journal.SelectionChanged += row => ShowInspector(row);
        _journal.Toggled += Toggle;
        _journal.ContextRequested += (row, at) =>
        {
            if (row.Item is not { } item || _busy)
            {
                return;
            }
            _entryMenu.Items.Clear();
            _entryMenu.Items.Add(new ToolStripMenuItem(item.Hidden ? RestoreText(item) : "Hide as test", null,
                                                       async (_, _) => await (item.Hidden ? RestoreAsync(row.Key) : HideAsync(row.Key))) { ForeColor = Theme.Text });
            _entryMenu.Show(_journal, at);
        };
        _action.Click += async (_, _) =>
        {
            if (_journal.SelectedRow is { Item: { } item } row)
            {
                await (item.Hidden ? RestoreAsync(row.Key) : HideAsync(row.Key));
            }
        };
        _noticeUndo.Click += async (_, _) => await UndoAsync();
        _noticeClose.Click += (_, _) => HideNotice();
        _noticeTimer.Tick += (_, _) => HideNotice();
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

    private TimeZoneInfo Zone => _time.LocalTimeZone;
    private DateTime Now => _time.GetLocalNow().DateTime;
    private DateOnly Today => DateOnly.FromDateTime(Now);
    private HistoryState State => _access(_source).State;

    /// <summary>The selected entry's row (null: none); for tests and accessibility.</summary>
    public JournalRow? Selected => _journal.SelectedRow;
    public IReadOnlyList<JournalRow> Rows => _journal.Rows;
    public DateOnly Date => _date;
    public HistorySource Source => _source;
    public InspectorInfo? Inspected { get; private set; }
    /// <summary>The day's numbers as shown (hidden entries never count).</summary>
    public IReadOnlyList<StatTile> Tiles { get; private set; } = [];
    /// <summary>The notice under the header (null: none), and whether it reports a failure.</summary>
    public string? Notice => _noticeBar.Visible ? _noticeText.Text : null;
    public bool NoticeIsError { get; private set; }
    /// <summary>The selected entry's action as offered: "Hide as test", "Restore", "Restore period…" (null: none).</summary>
    public string? ActionText => _actionRow.Visible ? _action.Text : null;

    /// <summary>Hidden entries shown too, marked as hidden test data (they never count).</summary>
    public bool ShowHidden
    {
        get => _showHidden;
        set
        {
            if (_showHidden != value)
            {
                _showHidden = value;
                Refresh(force: true);
            }
        }
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
        HideNotice();
        Refresh(force: true, scrollTop: true);
    }

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

    // ---- test data ----

    /// <summary>
    /// Hides the entry of that row as test data (a question with its smoking break and its scheduled occurrence).
    /// True once it is saved; on a failure the notice says so and nothing is claimed.
    /// </summary>
    public async Task<bool> HideAsync(string key)
    {
        if (_busy || Rows.FirstOrDefault(r => r.Key == key)?.Item is not { Hidden: false } item)
        {
            return false;
        }
        var source = _source;
        var what = item.Title + (item.Linked is not null && !item.LinkedHidden ? " and its smoking break" : "");
        return await Write(source, async access =>
        {
            var hide = await access.HideItem(item.Id, null);
            ShowNotice($"Hidden as test data: {what}. It is not counted; nothing was deleted.", error: false, undo: (source, [hide.Id]));
        }, e => $"Could not save the change ({e.Message}). {What(source, item.Id, hidden: true)}");
    }

    /// <summary>
    /// Restores a hidden entry: withdraws every hide that keeps it hidden. When one of them is a period, the user is
    /// asked first (it brings back the period's other entries too).
    /// </summary>
    public async Task<bool> RestoreAsync(string key)
    {
        if (_busy || Rows.FirstOrDefault(r => r.Key == key)?.Item is not { Hidden: true } item)
        {
            return false;
        }
        var source = _source;
        var state = State;
        var by = HistoryView.HiddenBy(item, state.Hides);
        if (by.Count == 0)
        {
            return false;
        }
        var periods = by.Where(h => h.IsPeriod).ToList();
        if (periods.Count > 0)
        {
            var covered = periods.SelectMany(p => HistoryView.Covered(state.Items, p)).DistinctBy(i => i.Id).ToList();
            var span = string.Join(", ", periods.Select(p => HistoryJournal.Period(p, Zone)));
            if (!_confirm("Restore a hidden period",
                          $"{item.Title} was hidden with the period {span}. Restoring it brings back the whole period: " +
                          $"{HistoryJournal.Summary(covered)}.", $"Restore {covered.Count} {(covered.Count == 1 ? "entry" : "entries")}"))
            {
                return false;
            }
        }
        return await Write(source, async access =>
        {
            await access.Unhide(by.Select(h => h.Id).ToList());
            ShowNotice($"Restored: {item.Title}" + (periods.Count > 0 ? " and the rest of its period." : "."), error: false, undo: null);
        }, e => $"Could not save the change ({e.Message}). {What(source, item.Id, hidden: false)}");
    }

    /// <summary>
    /// Hides a period as test data once the user confirmed its preview. Goes ahead only when the period still covers
    /// exactly the previewed entries (new ones may have arrived meanwhile); otherwise nothing is written.
    /// </summary>
    public async Task<bool> HidePeriodAsync(DateTimeOffset fromUtc, DateTimeOffset toUtc, string? note, IReadOnlyList<HistoryItem> previewed)
    {
        if (_busy)
        {
            return false;
        }
        var source = _source;
        var now = HistoryView.Affected(State.Items, fromUtc, toUtc);
        if (previewed.Count == 0 || !now.Select(i => i.Id).SequenceEqual(previewed.Select(i => i.Id)))
        {
            ShowNotice("The period changed while you looked at it (entries arrived or were hidden). Nothing was hidden: check the preview again.",
                       error: true, undo: null);
            return false;
        }
        var span = HistoryJournal.Period(new Annotation("", "hide", null, fromUtc, toUtc, null, "", null, default), Zone);
        return await Write(source, async access =>
        {
            var hide = await access.HidePeriod(fromUtc, toUtc, note);
            ShowNotice($"Hidden as test data: {HistoryJournal.Summary(previewed)} ({span}). Nothing was deleted.", error: false, undo: (source, [hide.Id]));
        }, e => $"Could not save the change ({e.Message}). " +
                (HistoryView.Affected(_access(source).State.Items, fromUtc, toUtc).Count == 0
                    ? "The history shows the period as saved anyway: check it under Test data → Hidden entries."
                    : "Nothing was hidden."));
    }

    /// <summary>Withdraws the hide the notice reported (its Undo).</summary>
    public async Task<bool> UndoAsync()
    {
        if (_busy || _undo is not { } undo)
        {
            return false;
        }
        _undo = null;
        return await Write(undo.Source, async access =>
        {
            await access.Unhide(undo.HideIds);
            ShowNotice("Undone: the entries are back.", error: false, undo: null);
        }, e => $"Could not undo ({e.Message}). The history shows what is saved.");
    }

    // Writes through the source, then shows the day again from its state (what the file holds, whatever happened).
    private async Task<bool> Write(HistorySource source, Func<IHistoryAccess, Task> write, Func<Exception, string> failure)
    {
        _busy = true;
        UpdateAction();
        try
        {
            await write(_access(source));
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidOperationException or ObjectDisposedException or ArgumentException)
        {
            ShowNotice(failure(e), error: true, undo: null);
            return false;
        }
        finally
        {
            _busy = false;
            if (source == _source)
            {
                Refresh(force: true);
            }
            UpdateAction();
        }
    }

    // After a failed write: what the source's state says about the entry now (the file is the truth).
    private string What(HistorySource source, string id, bool hidden)
    {
        var now = _access(source).State.Items.FirstOrDefault(i => i.Id == id);
        return now is null ? "The history shows what is saved."
            : now.Hidden == hidden ? "The history shows it as changed anyway: the line reached the file."
            : hidden ? "Nothing was hidden." : "Nothing was restored.";
    }

    private void ShowNotice(string text, bool error, (HistorySource, IReadOnlyList<string>)? undo)
    {
        _noticeText.Text = text;
        _noticeText.ForeColor = error ? Theme.Amber : Theme.Text;
        NoticeIsError = error;
        _undo = undo;
        _noticeUndo.Visible = undo is not null;
        _noticeBar.Visible = true;
        _noticeTimer.Stop();
        _noticeTimer.Start();
    }

    private void HideNotice()
    {
        _noticeTimer.Stop();
        _noticeBar.Visible = false;
        _undo = null;
    }

    private string RestoreText(HistoryItem item) =>
        HistoryView.HiddenBy(item, State.Hides).Any(h => h.IsPeriod) ? "Restore period…" : "Restore";

    private void UpdateAction()
    {
        var item = _journal.SelectedRow?.Item;
        _actionRow.Visible = item is not null;
        if (item is null)
        {
            return;
        }
        _action.Text = item.Hidden ? RestoreText(item) : "Hide as test";
        _action.DotColor = item.Hidden ? Theme.Green : Theme.Amber;
        _action.Enabled = !_busy;
        _tips.SetToolTip(_action, item.Hidden
            ? "Bring it back into the History and its numbers"
            : "Leave it out of the History and its numbers (nothing is deleted; Undo or restore any time)");
        _action.Invalidate();
    }

    private void OpenHidePeriod()
    {
        // From the selected entry's minute (it is then included); otherwise the times are to be typed.
        var selected = _journal.SelectedRow?.Item?.AtUtc is { } at ? TimeZoneInfo.ConvertTime(at, Zone).DateTime : (DateTime?)null;
        var from = selected is { } s && DateOnly.FromDateTime(s) == _date ? s.Hour * 60 + s.Minute : 0;
        var to = selected is not null ? Math.Min(from + 1, DayTime.Minutes) : 0;
        var source = _source;
        using var dialog = new HidePeriodDialog(_theme, _date, Zone, _time.GetUtcNow,
                                                (f, t) => HistoryView.Affected(_access(source).State.Items, f, t), from, to);
        if (dialog.ShowDialog(FindForm()) == DialogResult.OK && source == _source)
        {
            _ = HidePeriodAsync(dialog.FromUtc, dialog.ToUtc, dialog.Note, dialog.Previewed);
        }
    }

    private void OpenHiddenList()
    {
        var source = _source;
        using var dialog = new HiddenEntriesDialog(_theme, source == HistorySource.Simulator ? "simulator" : "DeskBuddy",
                                                   () => _access(source).State, Zone, async hide =>
        {
            try
            {
                await _access(source).Unhide([hide.Id]);
                return null;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidOperationException or ObjectDisposedException)
            {
                return $"Could not save the change ({e.Message}). The list shows what is saved.";
            }
            finally
            {
                if (source == _source)
                {
                    Refresh(force: true);
                }
            }
        });
        dialog.ShowDialog(FindForm());
    }

    // ---- building and showing ----

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
        row.Controls.Add(_testData);
        row.Controls.Add(new Panel { Dock = DockStyle.Right, Width = 8 });
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
        _calendar.Show(_date, Today, HistoryView.Days(State.Items, Zone));
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
        var state = State;
        if (!force && ReferenceEquals(state, _shownState))
        {
            return;
        }
        _shownState = state;
        _dirty = false;
        var today = Today;
        _shownToday = today;
        var zone = Zone;

        var day = HistoryView.Day(state.Items, state.Snapshot, _date, zone, includeHidden: _showHidden);
        _shownSchedule = _schedule();
        var plan = HistoryView.TodayPlan(_shownSchedule, _date, today);
        var rows = HistoryJournal.Rows(day, plan, _date == today, _openGroups, _technicalOpen, Now);
        if (!state.Loaded)
        {
            rows = [new JournalRow(JournalRowKind.Header, "h:day", "Reading the history…")];
        }

        _dateButton.Text = _date == today ? $"Today · {_date.ToString("ddd d MMM", CultureInfo.InvariantCulture)}"
                                          : HistoryJournal.DateLabel(_date, today);
        _dateButton.DotColor = _date == today ? Theme.Cyan : Theme.Faint;
        _dateButton.Invalidate();
        _next.Enabled = _date < today;
        _today.Enabled = _date != today;
        _hidePeriodItem.Enabled = state.Loaded;
        _dayCard.Title = _date.ToString("dddd d MMMM yyyy", CultureInfo.InvariantCulture) +
                         (_source == HistorySource.Simulator ? " · simulator" : "") +
                         (_showHidden ? " · showing hidden" : day.HiddenCount > 0 ? $" · {day.HiddenCount} hidden" : "");
        _dayCard.Accent = _showHidden ? Theme.Amber : null;
        _dayCard.Invalidate();
        _status.Text = state.Loaded ? "" : "Reading…";

        Tiles = HistoryJournal.Tiles(day.Stats);
        for (var i = 0; i < 4; i++)
        {
            _stats[i].Show(Tiles[i]);
            _tips.SetToolTip(_stats[i], Tiles[i].Detail); // the whole breakdown where the tile cuts it short
        }

        var scroll = _dayScroll.AutoScrollPosition;
        _dayScroll.SuspendLayout();
        _journal.Zone = zone;
        _journal.Show(rows);
        FitJournal();
        _dayScroll.ResumeLayout();
        _dayScroll.AutoScrollPosition = scrollTop ? Point.Empty : new Point(-scroll.X, -scroll.Y);

        // The inspector follows its entry (a question that just got its answer, an entry hidden or restored), and
        // empties when the entry is gone.
        var selected = _journal.SelectedRow;
        if (selected?.Key != _inspected || selected != _inspectedRow)
        {
            ShowInspector(selected, keepTechnical: selected?.Key == _inspected);
        }
        UpdateAction();
    }

    private void ShowInspector(JournalRow? row, bool keepTechnical = false)
    {
        _inspected = row?.Key;
        _inspectedRow = row;
        _inspector.Width = _inspectorScroll.ClientSize.Width - 4;
        Inspected = row is null ? null : HistoryJournal.Describe(row, Zone, State.Hides);
        _inspector.Show(Inspected, keepTechnical);
        UpdateAction();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _clock.Dispose();
            _coalesce.Dispose();
            _noticeTimer.Dispose();
            _calendarDrop.Dispose();
            _testMenu.Dispose();
            _entryMenu.Dispose();
        }
        base.Dispose(disposing);
    }
}
