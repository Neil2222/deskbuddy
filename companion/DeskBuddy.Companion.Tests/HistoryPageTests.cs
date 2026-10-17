using System.Text;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// P7.2: the History page in a real window: live updates keep the reader's place, the plan is today's only, board and
// simulator stay apart.
public sealed class HistoryPageTests : IDisposable
{
    private const string Dev = "441bf68d1178";
    private static readonly TimeZoneInfo Amsterdam = TimeZoneInfo.FindSystemTimeZoneById("W. Europe Standard Time");
    private static readonly DateOnly Fri = new(2026, 10, 16);
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-historypage-" + Guid.NewGuid().ToString("N"));
    private uint _seq;

    public void Dispose()
    {
        if (Directory.Exists(_dir))
        {
            Directory.Delete(_dir, recursive: true);
        }
    }

    private static DateTimeOffset At(int h, int m) => new(2026, 10, 16, h - 2, m, 0, TimeSpan.Zero); // CEST

    private static Frame F(string content) =>
        Frame.TryParse($"{content} #{Frame.Crc16(Encoding.ASCII.GetBytes(content)):X4}", out _) ?? throw new InvalidOperationException(content);

    private HistoryLedger Ledger(HistorySource source, string boot)
    {
        var l = new HistoryLedger(_dir, source, "test-installation", Amsterdam);
        l.Load();
        l.Connected(Dev, boot, "29f5c80", At(8, 0));
        l.ReplayAnswered(l.TakeFetch(At(8, 0))!, F($"<DB1 101 ok boot={boot} last=0 lost=0"), At(8, 0));
        return l;
    }

    private void Did(HistoryLedger l, string item, DateTimeOffset at) =>
        l.Received(F($"<DB1 0 evt did item=myday.{item} seq={++_seq} t={at.ToUnixTimeMilliseconds()}"), at);

    private static HistoryState State(HistoryLedger l)
    {
        l.Commit();
        return new HistoryState(l.Snapshot, HistoryView.Items(l.Snapshot, l.Entries, HistoryAnnotations.None), true);
    }

    private static WeekSchedule Schedule() => WeekSchedule.Empty.WithDay(DayOfWeek.Friday,
        [new ScheduleBlock { Name = "Afternoon work", Start = 12 * 60, End = 17 * 60, Mood = "working" }]);

    // The page over fixed states (what the recorder would publish); body gets the page and a "let the timers run".
    private static void InWindow(Func<HistorySource, HistoryState> state, Action<HistoryPage, Action> body) =>
        TestWindow.Run(s => new FixedHistory(() => state(s)), new StillClock(At(16, 40)), Schedule(),
                       (page, pump) => body(page, () => pump.For(TimeSpan.FromSeconds(2))));

    private static KeepScrollPanel Scroller(Control root) =>
        root as KeepScrollPanel ?? root.Controls.Cast<Control>().Select(c => Find(c)).FirstOrDefault(c => c is not null) ?? throw new InvalidOperationException();

    private static KeepScrollPanel? Find(Control root) =>
        root as KeepScrollPanel ?? root.Controls.Cast<Control>().Select(Find).FirstOrDefault(c => c is not null);

    [Fact]
    public void New_events_appear_without_losing_the_selection_or_the_scroll_position()
    {
        var board = Ledger(HistorySource.Board, "00000000000000b1");
        for (var i = 0; i < 20; i++)
        {
            Did(board, i % 2 == 0 ? "coffee" : "water", At(8, 10 + i * 2));
        }
        // A question on screen, its answer not in yet.
        board.Received(F($"<DB1 0 evt act iid=2147483700 name=ask_peukie state=shown src=touch seq={++_seq} t={At(9, 0).ToUnixTimeMilliseconds()}"), At(9, 0));
        var current = State(board);
        InWindow(_ => current, (page, pump) =>
        {
            var question = page.Rows.Single(r => r.Item?.Kind == HistoryItemKind.Question);
            page.SelectEntry(question.Key);
            Assert.Equal("No outcome received", page.Inspected!.Status);
            var scroller = Scroller(page);
            scroller.AutoScrollPosition = new Point(0, 400);
            var before = scroller.AutoScrollPosition;
            Assert.NotEqual(Point.Empty, before);

            board.Received(F($"<DB1 0 evt act iid=2147483700 state=done result=yes seq={++_seq} t={At(9, 1).ToUnixTimeMilliseconds()}"), At(9, 1));
            Did(board, "lunch", At(12, 30));
            current = State(board);
            page.HistoryChanged(HistorySource.Simulator); // another source: nothing to redo
            page.HistoryChanged(HistorySource.Board);
            pump();

            Assert.Contains(page.Rows, r => r.Text == "Lunch");
            Assert.Equal(question.Key, page.Selected?.Key);
            Assert.Equal("Yes", page.Inspected!.Status); // the details follow the entry
            Assert.Contains(("Counted as", "Smoking break at 09:01:00"), page.Inspected.Fields);
            Assert.Equal(before, scroller.AutoScrollPosition);
        });
    }

    [Fact]
    public void Todays_plan_shows_on_today_only()
    {
        var board = Ledger(HistorySource.Board, "00000000000000b1");
        Did(board, "coffee", At(9, 0));
        var state = State(board);
        InWindow(_ => state, (page, _) =>
        {
            Assert.Equal("1 block · now Afternoon work · nothing more planned",
                         page.Rows.Single(r => r.Kind == JournalRowKind.PlanSummary).Text);
            page.ShowDate(Fri.AddDays(-7)); // also a Friday: the schedule then may have differed
            Assert.DoesNotContain(page.Rows, r => r.Kind is JournalRowKind.PlanSummary or JournalRowKind.Plan);
            Assert.Equal("Nothing was recorded on this day.", page.Rows.Single(r => r.Kind == JournalRowKind.Empty).Text);
            page.ShowDate(Fri.AddDays(3)); // the future is not offered: today instead
            Assert.Equal(Fri, page.Date);
        });
    }

    [Fact]
    public void The_simulators_history_is_shown_on_its_own()
    {
        var board = Ledger(HistorySource.Board, "00000000000000b1");
        Did(board, "coffee", At(9, 0));
        var boardState = State(board);
        _seq = 0;
        var sim = Ledger(HistorySource.Simulator, "00000000000000c1");
        Did(sim, "water", At(9, 5));
        var simState = State(sim);
        InWindow(s => s == HistorySource.Simulator ? simState : boardState, (page, _) =>
        {
            string[] Entries() => page.Rows.Where(r => r.Kind == JournalRowKind.Entry).Select(r => r.Text).ToArray();
            Assert.Equal(["Coffee"], Entries());
            page.SelectEntry(page.Rows.Single(r => r.Text == "Coffee").Key);
            page.ShowSource(HistorySource.Simulator);
            Assert.Equal(["Water"], Entries());
            Assert.Null(page.Selected); // a selection never carries over to the other history
            page.ShowSource(HistorySource.Board);
            Assert.Equal(["Coffee"], Entries());
        });
    }
}
