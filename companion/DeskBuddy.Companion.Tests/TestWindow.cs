using System.Runtime.ExceptionServices;
using DeskBuddy.Companion.History;
using DeskBuddy.Companion.Schedule;
using DeskBuddy.Companion.UI;
using Xunit;

namespace DeskBuddy.Companion.Tests;

/// <summary>A source whose state the test sets; writing to it is not part of the test.</summary>
internal sealed class FixedHistory(Func<HistoryState> state) : IHistoryAccess
{
    public HistoryState State => state();
    public Task<Annotation> HideItem(string id, string? note) => throw new NotSupportedException();
    public Task<Annotation> HidePeriod(DateTimeOffset fromUtc, DateTimeOffset toUtc, string? note) => throw new NotSupportedException();
    public Task Unhide(IReadOnlyList<string> hideIds) => throw new NotSupportedException();
}

/// <summary>A clock standing still, in Amsterdam.</summary>
internal sealed class StillClock(DateTimeOffset now) : TimeProvider
{
    public static readonly TimeZoneInfo Amsterdam = TimeZoneInfo.FindSystemTimeZoneById("W. Europe Standard Time");
    public override DateTimeOffset GetUtcNow() => now.ToUniversalTime();
    public override TimeZoneInfo LocalTimeZone => Amsterdam;
}

/// <summary>Pages in a real (off-screen) window on an STA thread, as WinForms needs.</summary>
internal static class TestWindow
{
    /// <summary>The History page over the given sources.</summary>
    public static void Run(Func<HistorySource, IHistoryAccess> access, TimeProvider clock, WeekSchedule schedule,
                           Action<HistoryPage, Pump> body, Func<string, string, string, bool>? confirm = null) =>
        Sta((form, theme, art, tips) =>
        {
            var page = new HistoryPage(theme, art, tips, access, () => true, () => schedule, clock, confirm ?? ((_, _, _) => true)) { Dock = DockStyle.Fill };
            form.Controls.Add(page);
            return pump => body(page, pump);
        });

    /// <summary>
    /// A window with the Companion's theme, art and tooltips: <paramref name="build"/> fills it (before it is shown, as
    /// the Companion builds its pages) and returns the test, which runs once the window is shown.
    /// </summary>
    public static void Sta(Func<Form, Theme, BuddyArt, ToolTip, Action<Pump>> build)
    {
        ExceptionDispatchInfo? failure = null;
        var thread = new Thread(() =>
        {
            try
            {
                // A failure in a callback fails the test (instead of WinForms' modal error dialog, which would hang it).
                Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException, threadScope: true);
                using var theme = new Theme();
                using var art = new BuddyArt();
                using var tips = new ToolTip();
                using var form = new Form { ClientSize = new Size(1000, 700), StartPosition = FormStartPosition.Manual, Location = new Point(-3000, 0), ShowInTaskbar = false };
                var test = build(form, theme, art, tips);
                form.Show();
                Pump.Events();
                test(new Pump());
            }
            catch (Exception e)
            {
                failure = ExceptionDispatchInfo.Capture(e);
            }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        failure?.Throw();
    }

    /// <summary>
    /// Runs the window's messages: for a while, or until a task is done. Application.DoEvents outside Application.Run
    /// takes the WinForms synchronization context away when it returns; it is put back after every round, as
    /// Application.Run keeps it, so the page's awaits continue on its own thread.
    /// </summary>
    public sealed class Pump
    {
        public static void Events()
        {
            Application.DoEvents();
            if (SynchronizationContext.Current is not WindowsFormsSynchronizationContext)
            {
                SynchronizationContext.SetSynchronizationContext(new WindowsFormsSynchronizationContext());
            }
        }

        public void For(TimeSpan time)
        {
            var until = DateTime.UtcNow + time;
            while (DateTime.UtcNow < until)
            {
                Events();
                Thread.Sleep(10);
            }
        }

        public T Wait<T>(Task<T> task)
        {
            var until = DateTime.UtcNow.AddSeconds(10);
            while (!task.IsCompleted && DateTime.UtcNow < until)
            {
                Events();
                Thread.Sleep(5);
            }
            Assert.True(task.IsCompleted, "the page's task did not finish");
            return task.GetAwaiter().GetResult();
        }
    }
}
