namespace DeskBuddy.Companion.Desktop;

/// <summary>
/// One companion per Windows session (a second one could not open the port anyway). The first holds a named mutex
/// and waits on a named event; a second launch sets that event, so the first shows its window (also from the
/// notification area), and exits.
/// </summary>
public sealed class SingleInstance : IDisposable
{
    public const string DefaultName = @"Local\DeskBuddy.Companion";

    private readonly Mutex _mutex;
    private readonly EventWaitHandle _show;
    private RegisteredWaitHandle? _wait;

    private SingleInstance(Mutex mutex, EventWaitHandle show)
    {
        _mutex = mutex;
        _show = show;
    }

    /// <summary>This process is the first: it owns <paramref name="name"/>. Null: another one already runs.</summary>
    public static SingleInstance? TryAcquire(string name = DefaultName)
    {
        var mutex = new Mutex(true, name, out var first);
        if (!first)
        {
            mutex.Dispose();
            return null;
        }
        return new SingleInstance(mutex, new EventWaitHandle(false, EventResetMode.AutoReset, ShowEventName(name)));
    }

    /// <summary>Asks the first companion to show its window; false when it cannot be reached (an older version).</summary>
    public static bool SignalFirst(string name = DefaultName)
    {
        if (!EventWaitHandle.TryOpenExisting(ShowEventName(name), out var show))
        {
            return false;
        }
        using (show)
        {
            return show.Set();
        }
    }

    /// <summary>Calls <paramref name="onShow"/> (on a pool thread) whenever another launch asks for the window.</summary>
    public void OnShowRequested(Action onShow)
    {
        _wait?.Unregister(null);
        _wait = ThreadPool.RegisterWaitForSingleObject(_show, (_, _) => onShow(), null, Timeout.Infinite, executeOnlyOnce: false);
    }

    public void Dispose()
    {
        _wait?.Unregister(null);
        _show.Dispose();
        _mutex.ReleaseMutex();
        _mutex.Dispose();
    }

    private static string ShowEventName(string name) => name + ".Show";
}

/// <summary>What closing the window and starting minimized mean, with and without tray mode.</summary>
public static class WindowPolicy
{
    /// <summary>
    /// Closing the window hides it (the companion keeps running in the notification area) only in tray mode, only
    /// when the user closed it, and not when Quit was chosen. Signing out, shutting down, Task Manager: it quits.
    /// </summary>
    public static bool HideOnClose(CloseReason reason, bool trayEnabled, bool quitting) =>
        trayEnabled && !quitting && reason == CloseReason.UserClosing;

    /// <summary>Started by Windows at sign-in (<see cref="Autostart.MinimizedArgument"/>).</summary>
    public static bool StartMinimized(IEnumerable<string> args) =>
        args.Any(a => string.Equals(a, Autostart.MinimizedArgument, StringComparison.OrdinalIgnoreCase));
}
