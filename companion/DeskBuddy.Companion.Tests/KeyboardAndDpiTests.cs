using System.Runtime.InteropServices;
using System.Text;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// P7.5: the History page by keyboard (the date picker, the lines that open, Technical details, the dialogs) and text
// drawn at the control's own DPI. Keys are real WM_KEYDOWN messages to the controls.
public sealed class KeyboardAndDpiTests : IDisposable
{
    private const string Dev = "441bf68d1178", B1 = "00000000000000b1";
    private static readonly TimeZoneInfo Amsterdam = StillClock.Amsterdam;
    private static readonly DateOnly Fri = new(2026, 10, 16);
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-keys-" + Guid.NewGuid().ToString("N"));
    private uint _seq;

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] private static extern uint GetDpiForSystem();

    private static void Press(Control c, Keys key) => SendMessage(c.Handle, 0x0100, (IntPtr)(int)key, IntPtr.Zero);

    private static DateTimeOffset At(int h, int m) => new(2026, 10, 16, h - 2, m, 0, TimeSpan.Zero);

    private static Frame F(string content) =>
        Frame.TryParse($"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}", out _) ?? throw new InvalidOperationException(content);

    private HistoryState Day()
    {
        var l = new HistoryLedger(_dir, HistorySource.Board, "test-installation", Amsterdam);
        l.Load();
        l.Connected(Dev, B1, "29f5c80", At(8, 0));
        l.ReplayAnswered(l.TakeFetch(At(8, 0))!, F($"<DB1 101 ok boot={B1} last=0 lost=0"), At(8, 0));
        void Act(uint iid, string name, string result, DateTimeOffset t)
        {
            l.Received(F($"<DB1 0 evt act iid={iid} name={name} state=shown src=touch seq={++_seq} t={t.ToUnixTimeMilliseconds()}"), t);
            l.Received(F($"<DB1 0 evt act iid={iid} state=done result={result} seq={++_seq} t={t.AddSeconds(20).ToUnixTimeMilliseconds()}"), t.AddSeconds(20));
        }
        Act(0x80000001, "ask_peukie", "yes", At(9, 0));
        Act(0x80000002, "sneeze", "done", At(9, 10));
        Act(0x80000003, "stare", "done", At(9, 12));
        l.ScheduleStateChanged(true, null, At(8, 1));
        l.Commit();
        return new HistoryState(l.Snapshot, HistoryView.Items(l.Snapshot, l.Entries, HistoryAnnotations.None), true);
    }

    private static T Find<T>(Control root) where T : Control =>
        root as T ?? root.Controls.Cast<Control>().Select(c => FindOrNull<T>(c)).FirstOrDefault(c => c is not null) ?? throw new InvalidOperationException(typeof(T).Name);

    private static T? FindOrNull<T>(Control root) where T : Control =>
        root as T ?? root.Controls.Cast<Control>().Select(FindOrNull<T>).FirstOrDefault(c => c is not null);

    [Fact]
    public void The_date_picker_moves_by_day_week_and_month_never_into_the_future_and_picks_with_enter()
    {
        TestWindow.Sta((form, theme, _, _) =>
        {
            var month = new MonthView(theme);
            form.Controls.Add(month);
            return _ =>
            {
                var picked = new List<DateOnly>();
                var cancelled = 0;
                month.Picked += picked.Add;
                month.Cancelled += () => cancelled++;
                month.Show(Fri, today: Fri.AddDays(1), new HashSet<DateOnly> { Fri }, dpi: 144);
                Assert.Equal(new Size((34 * 7 + 16) * 3 / 2, (34 + 22 + 34 * 6 + 12) * 3 / 2), month.Size); // the drop-down's size at its screen's scale
                Assert.Equal("Pick a day: Friday 16 October 2026, has entries", month.AccessibleName);

                Press(month, Keys.Right);
                Assert.Equal(Fri.AddDays(1), month.KeyDay);
                Press(month, Keys.Right); // tomorrow has no history yet
                Assert.Equal(Fri.AddDays(1), month.KeyDay);
                Press(month, Keys.Up);
                Assert.Equal(Fri.AddDays(-6), month.KeyDay);
                Press(month, Keys.PageUp);
                Assert.Equal(new DateOnly(2026, 9, 10), month.KeyDay);
                Press(month, Keys.Left);
                Press(month, Keys.Down);
                Assert.Equal(new DateOnly(2026, 9, 16), month.KeyDay);
                Press(month, Keys.Home);
                Assert.Equal(Fri.AddDays(1), month.KeyDay);
                Press(month, Keys.Left);
                Press(month, Keys.Enter);
                Assert.Equal([Fri], picked);
                Press(month, Keys.Escape);
                Assert.Equal(1, cancelled);
            };
        });
    }

    [Fact]
    public void Up_and_down_reach_the_lines_that_open_and_enter_opens_them()
    {
        var state = Day();
        TestWindow.Run(_ => new FixedHistory(() => state), new StillClock(At(16, 40)), WeekSchedule.Empty, (page, _) =>
        {
            var journal = Find<JournalView>(page);
            journal.Focus();
            Press(journal, Keys.Down);
            Assert.Equal("Peukie?", page.Selected?.Text); // an entry: selected as it is reached
            Press(journal, Keys.Down);
            var shows = page.Rows.Single(r => r.Kind == JournalRowKind.Shows);
            Assert.Equal(shows.Key, journal.FocusKey); // a line that opens: reached, the selection stays
            Assert.Equal("Peukie?", page.Selected?.Text);
            Press(journal, Keys.Enter);
            Assert.Contains(page.Rows, r => r.Kind == JournalRowKind.Entry && r.Nested && r.Text == "Sneeze");
            Press(journal, Keys.Down);
            Press(journal, Keys.Down);
            Press(journal, Keys.Down);
            Assert.Equal("technical", journal.FocusKey);
            Press(journal, Keys.Space);
            Assert.Equal("Schedule live", page.Rows[^1].Text);
            Press(journal, Keys.Space);
            Assert.Equal(JournalRowKind.TechnicalToggle, page.Rows[^1].Kind);
        });
    }

    [Fact]
    public void Technical_details_open_with_the_keyboard()
    {
        var state = Day();
        TestWindow.Run(_ => new FixedHistory(() => state), new StillClock(At(16, 40)), WeekSchedule.Empty, (page, _) =>
        {
            var inspector = Find<InspectorView>(page);
            Assert.False(inspector.TabStop); // nothing selected: nothing to open
            page.SelectEntry(state.Items.First(i => i.Kind == HistoryItemKind.Question).Id);
            Assert.True(inspector.TabStop);
            Assert.EndsWith("Technical details closed: Enter opens them.", inspector.AccessibleDescription);
            inspector.Focus();
            Press(inspector, Keys.Enter);
            Assert.True(inspector.TechnicalOpen);
            Assert.Contains("Interaction: iid 2147483649", inspector.AccessibleDescription);
            Press(inspector, Keys.Space);
            Assert.False(inspector.TechnicalOpen);
        });
    }

    [Fact]
    public void The_hide_dialogs_start_on_their_fields_and_tab_to_their_buttons_last()
    {
        var state = Day();
        TestWindow.Sta((form, theme, _, _) => _ =>
        {
            using var period = new HidePeriodDialog(theme, Fri, Amsterdam, () => At(16, 40),
                                                    (f, t) => HistoryView.Affected(state.Items, f, t), 9 * 60, 10 * 60)
                                                    { StartPosition = FormStartPosition.Manual, Location = new Point(-3000, 0) };
            period.Show(); // creating its window, as ShowDialog does
            TestWindow.Pump.Events();
            Assert.IsType<TimeBox>(period.ActiveControl);
            Assert.Equal("From", period.ActiveControl!.AccessibleName);
            var controls = period.Controls.Cast<Control>().Where(c => c.TabStop || c.Controls.Count > 0).OrderBy(c => c.TabIndex).ToList();
            Assert.IsType<FlowLayoutPanel>(controls.Last()); // the buttons come last

            using var hidden = new HiddenEntriesDialog(theme, "DeskBuddy",
                () => state with { Annotations = HistoryAnnotations.Fold([new Annotation("h1", "hide", state.Items[0].Id, null, null, null, "test", null, At(12, 0))]) },
                Amsterdam, _ => Task.FromResult<string?>(null)) { StartPosition = FormStartPosition.Manual, Location = new Point(-3000, 0) };
            hidden.Show();
            TestWindow.Pump.Events();
            Assert.IsType<EntryList>(hidden.ActiveControl);
            Assert.Equal(0, ((ListBox)hidden.ActiveControl!).SelectedIndex); // Restore applies to the first at once
        });
    }

    [Fact]
    public void Text_is_drawn_with_fonts_for_the_controls_own_dpi()
    {
        using var theme = new Theme();
        var system = (int)GetDpiForSystem();
        Assert.Same(theme, theme.At(system));
        var at150 = theme.At(system * 3 / 2);
        Assert.Same(at150, theme.At(system * 3 / 2)); // made once
        Assert.Equal(theme.Text10.Size * 1.5f, at150.Text10.Size, 2);
        Assert.Equal(theme.Small.Size * 1.5f, at150.Small.Size, 2);
        Assert.Equal(theme.Glyphs.Name, at150.Glyphs.Name);
    }
}
