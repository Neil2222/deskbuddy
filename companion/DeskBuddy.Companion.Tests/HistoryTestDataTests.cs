using System.Security.Cryptography;
using System.Text;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Protocol;
using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// P7.3: hiding test data from the History page, restoring it, hiding a period after its preview; through the real
// recorder and annotation files in a temporary folder (never the user's history).
public sealed class HistoryTestDataTests : IDisposable
{
    private const string Dev = "441bf68d1178";
    private const string B1 = "00000000000000b1";
    private static readonly TimeZoneInfo Amsterdam = StillClock.Amsterdam;
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "db-testdata-" + Guid.NewGuid().ToString("N"));
    private readonly StillClock _clock = new(At(16, 40));
    private readonly List<HistoryRecorder> _recorders = [];
    private bool _confirm = true;
    private readonly List<string> _asked = [];
    private uint _seq;

    private static string Question(uint iid) => $"{Dev}/{B1}/i{iid}";
    private string Annotations(HistorySource s = HistorySource.Board) =>
        Path.Combine(_dir, s == HistorySource.Simulator ? "history-annotations.sim.jsonl" : "history-annotations.jsonl");

    public HistoryTestDataTests()
    {
        Directory.CreateDirectory(_dir);
        var l = Ledger(HistorySource.Board, B1);
        Ask(l, 0x80000001, "yes", At(9, 0));                                                        // with a linked smoke break
        Did(l, "coffee", At(10, 0));
        l.Received(F($"<DB1 0 evt did item=myday.water seq={++_seq} up=1725000"), At(10, 30));        // time unknown, arrived 10:30
        Ask(l, 0x80000002, "no", At(11, 0));
        l.Commit();
    }

    public void Dispose()
    {
        foreach (var r in _recorders)
        {
            r.Dispose();
        }
        foreach (var file in Directory.Exists(_dir) ? Directory.GetFiles(_dir) : [])
        {
            File.SetAttributes(file, FileAttributes.Normal);
        }
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

    private void Ask(HistoryLedger l, uint iid, string result, DateTimeOffset at)
    {
        l.Received(F($"<DB1 0 evt act iid={iid} name=ask_peukie state=shown src=touch seq={++_seq} t={at.ToUnixTimeMilliseconds()}"), at);
        l.Received(F($"<DB1 0 evt act iid={iid} state=done result={result} seq={++_seq} t={at.AddSeconds(20).ToUnixTimeMilliseconds()}"), at.AddSeconds(20));
    }

    private HistoryRecorder Recorder(HistorySource source = HistorySource.Board)
    {
        var r = new HistoryRecorder(_dir, source, _clock);
        _recorders.Add(r);
        r.FlushAsync().Wait(TimeSpan.FromSeconds(10));
        Assert.True(r.State.Loaded);
        return r;
    }

    private void Restart(ref HistoryRecorder r)
    {
        r.Dispose();
        _recorders.Remove(r);
        r = Recorder(r.Source);
    }

    private void InWindow(Func<HistorySource, IHistoryAccess> access, Action<HistoryPage, TestWindow.Pump> body) =>
        TestWindow.Run(access, _clock, WeekSchedule.Empty, body, (title, text, ok) =>
        {
            _asked.Add($"{title}|{text}|{ok}");
            return _confirm;
        });

    private Dictionary<string, string> Canonical() =>
        new[] { "device-events.jsonl", "companion-events.jsonl", "device-checkpoints.json" }
            .ToDictionary(f => f, f => File.Exists(Path.Combine(_dir, f))
                ? Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(_dir, f)))) : "absent");

    private static (int Activities, int Smoke, int Questions) Numbers(HistoryPage page) =>
        (page.Tiles[0].Value, page.Tiles[1].Value, page.Tiles[2].Value);

    private static JournalRow Row(HistoryPage page, string id) => page.Rows.Single(r => r.Key == id);

    [Fact]
    public void Hiding_a_question_as_test_hides_its_smoking_break_updates_the_numbers_and_survives_a_restart()
    {
        var before = Canonical();
        var recorder = Recorder();
        InWindow(_ => recorder, (page, pump) =>
        {
            Assert.False(File.Exists(Annotations())); // nothing is hidden by itself
            Assert.Equal((3, 1, 2), Numbers(page));
            page.SelectEntry(Question(0x80000001));
            Assert.Equal("Hide as test", page.ActionText);

            Assert.True(pump.Wait(page.HideAsync(Question(0x80000001))));

            Assert.DoesNotContain(page.Rows, r => r.Key == Question(0x80000001));
            Assert.Equal((2, 0, 1), Numbers(page)); // the question and the smoking break it counted are both out
            Assert.False(page.NoticeIsError);
            Assert.StartsWith("Hidden as test data: Peukie? and its smoking break.", page.Notice);
            Assert.Null(page.Selected);
        });

        var line = Assert.Single(File.ReadAllLines(Annotations()));
        Assert.Contains($"\"item\":\"{Question(0x80000001)}\"", line);
        Assert.Contains("\"reason\":\"test\"", line);
        Assert.Equal(before, Canonical()); // the logs and the journal are untouched

        Restart(ref recorder);
        var item = recorder.State.Items.Single(i => i.Id == Question(0x80000001));
        Assert.True(item is { Hidden: true, LinkedHidden: true });
        Assert.Equal(2, recorder.State.Snapshot.Interactions.Count); // the derived history itself is unchanged
        Assert.Single(recorder.State.Snapshot.Activities, a => a.Type == "smoke");
    }

    [Fact]
    public void Show_hidden_marks_a_hidden_entry_and_restore_brings_it_back_into_the_numbers()
    {
        var recorder = Recorder();
        InWindow(_ => recorder, (page, pump) =>
        {
            var coffee = page.Rows.Single(r => r.Text == "Coffee").Key;
            Assert.True(pump.Wait(page.HideAsync(coffee)));
            page.ShowHidden = true;

            var row = Row(page, coffee);
            Assert.True(row.Item!.Hidden);
            page.SelectEntry(coffee);
            Assert.Equal("Restore", page.ActionText);
            Assert.Contains(page.Inspected!.Notes, n => n.StartsWith("Hidden as test data on Fri 16 Oct 16:40"));
            Assert.Equal((2, 1, 2), Numbers(page)); // shown, never counted

            Assert.True(pump.Wait(page.RestoreAsync(coffee)));
            Assert.False(Row(page, coffee).Item!.Hidden);
            Assert.Equal("Hide as test", page.ActionText); // the selection stays and follows the entry
            Assert.Equal((3, 1, 2), Numbers(page));
            Assert.Empty(_asked); // an entry hidden on its own is restored without a question
        });
        var lines = File.ReadAllLines(Annotations());
        Assert.Equal(["\"action\":\"hide\"", "\"action\":\"unhide\""], lines.Select(l => l.Contains("\"action\":\"hide\"") ? "\"action\":\"hide\"" : "\"action\":\"unhide\""));

        Restart(ref recorder);
        Assert.DoesNotContain(recorder.State.Items, i => i.Hidden);
    }

    [Fact]
    public void Undo_right_after_hiding_withdraws_the_hide()
    {
        var recorder = Recorder();
        InWindow(_ => recorder, (page, pump) =>
        {
            Assert.True(pump.Wait(page.HideAsync(Question(0x80000002))));
            Assert.True(pump.Wait(page.UndoAsync()));
            Assert.Contains(page.Rows, r => r.Key == Question(0x80000002));
            Assert.Equal((3, 1, 2), Numbers(page));
            Assert.False(pump.Wait(page.UndoAsync())); // once
        });
        Assert.Equal(2, File.ReadAllLines(Annotations()).Length);
    }

    [Fact]
    public void A_period_is_hidden_only_after_its_preview_and_comes_back_only_as_a_whole_after_asking()
    {
        var recorder = Recorder();
        InWindow(_ => recorder, (page, pump) =>
        {
            using var theme = new Theme();
            using var dialog = new HidePeriodDialog(theme, new DateOnly(2026, 10, 16), Amsterdam, _clock.GetUtcNow,
                                                    (f, t) => HistoryView.Affected(recorder.State.Items, f, t), 0, 0);
            Assert.False(dialog.CanHide); // no period yet
            dialog.SetPeriod(9 * 60, 10 * 60 + 31);

            Assert.True(dialog.CanHide);
            Assert.Equal("3 entries: 1 question (1 with its smoking break), 2 activities · 1 with an unknown time", dialog.Summary);
            var lines = dialog.Previewed.Select(i => HistoryJournal.PreviewLine(i, Amsterdam)).ToList();
            Assert.Equal([("09:00", "Peukie?", "Question · Yes · with its smoking break (linked)"),
                          ("10:00", "Coffee", "MY DAY"),
                          ("?", "Water", "MY DAY · time unknown, arrived Fri 10:30")], lines);
            Assert.False(File.Exists(Annotations())); // a preview writes nothing

            Assert.True(pump.Wait(page.HidePeriodAsync(dialog.FromUtc, dialog.ToUtc, "P6 board test", dialog.Previewed)));
            Assert.Equal((0, 0, 1), Numbers(page));
            Assert.Equal("Hidden as test data: 3 entries: 1 question (1 with its smoking break), 2 activities · 1 with an unknown time " +
                         "(Fri 16 Oct 09:00–10:31). Nothing was deleted.", page.Notice);

            page.ShowHidden = true;
            var coffee = page.Rows.Single(r => r.Text == "Coffee").Key;
            page.SelectEntry(coffee);
            Assert.Equal("Restore period…", page.ActionText);
            Assert.Contains(page.Inspected!.Notes, n => n.StartsWith("Hidden as test data with the period Fri 16 Oct 09:00–10:31 (P6 board test)"));

            _confirm = false;
            Assert.False(pump.Wait(page.RestoreAsync(coffee)));
            Assert.Equal("Restore a hidden period|Coffee was hidden with the period Fri 16 Oct 09:00–10:31. Restoring it brings back the whole period: " +
                         "3 entries: 1 question (1 with its smoking break), 2 activities · 1 with an unknown time.|Restore 3 entries", Assert.Single(_asked));
            Assert.True(Row(page, coffee).Item!.Hidden);

            _confirm = true;
            Assert.True(pump.Wait(page.RestoreAsync(coffee)));
            Assert.Equal((3, 1, 2), Numbers(page));
        });
    }

    [Fact]
    public void A_period_that_changed_after_its_preview_is_not_hidden()
    {
        var recorder = Recorder();
        InWindow(_ => recorder, (page, pump) =>
        {
            var from = At(9, 0);
            var to = At(12, 0);
            var previewed = HistoryView.Affected(recorder.State.Items, from, to);
            Assert.Equal(4, previewed.Count);
            // Meanwhile the coffee is hidden on its own (another window, an Undo): the period now covers other entries.
            Assert.True(recorder.HideItem(previewed.Single(i => i.Title == "Coffee").Id, null).Wait(TimeSpan.FromSeconds(10)));

            Assert.False(pump.Wait(page.HidePeriodAsync(from, to, null, previewed)));
            Assert.True(page.NoticeIsError);
            Assert.StartsWith("The period changed while you looked at it", page.Notice);
        });
        Assert.DoesNotContain(File.ReadAllLines(Annotations()), l => l.Contains("from_utc")); // no period was written
    }

    [Fact]
    public void A_hide_that_cannot_be_saved_is_not_shown_as_hidden()
    {
        File.WriteAllText(Annotations(), "");
        File.SetAttributes(Annotations(), FileAttributes.ReadOnly); // the write will fail
        var recorder = Recorder();
        InWindow(_ => recorder, (page, pump) =>
        {
            Assert.False(pump.Wait(page.HideAsync(Question(0x80000002))));
            Assert.True(page.NoticeIsError);
            Assert.StartsWith("Could not save the change (", page.Notice);
            Assert.EndsWith("Nothing was hidden.", page.Notice);
            Assert.False(Row(page, Question(0x80000002)).Item!.Hidden);
            Assert.Equal((3, 1, 2), Numbers(page));
        });
        File.SetAttributes(Annotations(), FileAttributes.Normal);
        Restart(ref recorder);
        Assert.DoesNotContain(recorder.State.Items, i => i.Hidden);
        Assert.Equal("", File.ReadAllText(Annotations()));
    }

    [Fact]
    public void Board_and_simulator_annotations_stay_in_their_own_files()
    {
        var sim = Ledger(HistorySource.Simulator, "00000000000000c1");
        _seq = 0;
        Did(sim, "coffee", At(10, 0));
        sim.Commit();
        var board = Recorder();
        var simulator = Recorder(HistorySource.Simulator);
        InWindow(s => s == HistorySource.Simulator ? simulator : board, (page, pump) =>
        {
            var boardCoffee = page.Rows.Single(r => r.Text == "Coffee").Key;
            Assert.True(pump.Wait(page.HideAsync(boardCoffee)));
            Assert.False(File.Exists(Annotations(HistorySource.Simulator)));

            page.ShowSource(HistorySource.Simulator);
            var simCoffee = page.Rows.Single(r => r.Text == "Coffee");
            Assert.False(simCoffee.Item!.Hidden); // the board's hide does not reach the simulator's coffee
            Assert.True(pump.Wait(page.HideAsync(simCoffee.Key)));
            Assert.Null(page.Notice is { } n && n.Contains("Could not") ? n : null);

            page.ShowSource(HistorySource.Board);
            Assert.DoesNotContain(page.Rows, r => r.Key == boardCoffee);
        });
        Assert.Single(File.ReadAllLines(Annotations()));
        var simLine = Assert.Single(File.ReadAllLines(Annotations(HistorySource.Simulator)));
        Assert.Contains("\"src\":\"simulator\"", simLine);
        Assert.DoesNotContain(board.State.Items, i => i.Id.Contains("c1") && i.Hidden);
    }

    [Fact]
    public void A_period_must_have_passed_and_its_times_must_exist()
    {
        var today = new DateOnly(2026, 10, 16);
        var now = At(16, 40);
        Assert.Equal("The period must have passed: it can end at 16:40 at the latest.",
                     HistoryJournal.LocalPeriod(today, 16 * 60, 17 * 60, Amsterdam, now).Error);
        Assert.Null(HistoryJournal.LocalPeriod(today, 16 * 60, 16 * 60 + 40, Amsterdam, now).Error);
        Assert.Equal("The end must be after the start.", HistoryJournal.LocalPeriod(today, 600, 600, Amsterdam, now).Error);
        var spring = new DateOnly(2026, 3, 29); // 02:00–03:00 does not exist in Amsterdam
        Assert.Equal("That time does not exist on this day (the clocks went forward).",
                     HistoryJournal.LocalPeriod(spring, 2 * 60 + 30, 4 * 60, Amsterdam, now).Error);
        var (from, to, error) = HistoryJournal.LocalPeriod(today.AddDays(-1), 22 * 60, 24 * 60, Amsterdam, now);
        Assert.Null(error);
        Assert.Equal((new DateTimeOffset(2026, 10, 15, 20, 0, 0, TimeSpan.Zero), new DateTimeOffset(2026, 10, 15, 22, 0, 0, TimeSpan.Zero)), (from, to));
    }
}
