using Microsoft.Win32;

namespace DeskBuddy.Companion.Desktop;

/// <summary>Where Windows keeps its per-user startup entries (behind an interface so the tests never touch the real one).</summary>
public interface IStartupRegistry
{
    /// <summary>The command of the startup entry <paramref name="name"/>; null: none.</summary>
    string? Read(string name);
    void Write(string name, string command);
    void Delete(string name);
    /// <summary>The entry exists but is switched off in Task Manager (Startup apps) or Settings.</summary>
    bool IsDisabledInWindows(string name);
}

public enum AutostartState
{
    /// <summary>No startup entry.</summary>
    Off,
    /// <summary>Windows starts this copy of the companion at sign-in.</summary>
    On,
    /// <summary>The entry is this copy's, but switched off in Task Manager or Settings.</summary>
    DisabledInWindows,
    /// <summary>The entry starts another copy (the companion was moved or published elsewhere).</summary>
    OtherCopy,
    /// <summary>The entry starts a file that no longer exists.</summary>
    Missing,
}

public sealed record AutostartStatus(AutostartState State, string? RegisteredExe);

/// <summary>
/// "Start with Windows": one value in HKCU\Software\Microsoft\Windows\CurrentVersion\Run that starts the companion
/// with <see cref="MinimizedArgument"/>. Off unless the user switches it on. Only a published companion can be
/// registered: the development build needs the per-user .NET that tools\companion.ps1 sets up, so Windows could not
/// start it. The entry is read back on every check, so a copy that moved, a file that is gone and an entry switched
/// off in Task Manager are reported instead of failing silently at the next sign-in.
/// </summary>
public sealed class Autostart(IStartupRegistry registry, string currentExe, bool canRegister, Func<string, bool> fileExists)
{
    public const string ValueName = "DeskBuddy Companion";
    public const string MinimizedArgument = "--minimized";

    /// <summary>This copy can be registered (a published, self-contained exe).</summary>
    public bool CanRegister => canRegister;

    public string CurrentExe => currentExe;

    /// <summary>The command written for this copy.</summary>
    public string Command => $"\"{currentExe}\" {MinimizedArgument}";

    public AutostartStatus Status()
    {
        if (registry.Read(ValueName) is not { } command)
        {
            return new(AutostartState.Off, null);
        }
        var exe = ExeOf(command);
        if (!fileExists(exe))
        {
            return new(AutostartState.Missing, exe);
        }
        if (!SamePath(exe, currentExe))
        {
            return new(AutostartState.OtherCopy, exe);
        }
        return new(registry.IsDisabledInWindows(ValueName) ? AutostartState.DisabledInWindows : AutostartState.On, exe);
    }

    /// <summary>Registers this copy (replacing an entry for another copy, and switching it on again in Windows).</summary>
    public void Enable()
    {
        if (!canRegister)
        {
            throw new InvalidOperationException("only a published DeskBuddy.Companion.exe can start with Windows");
        }
        registry.Write(ValueName, Command);
    }

    public void Disable() => registry.Delete(ValueName);

    /// <summary>What to tell the user about <paramref name="status"/>; null: nothing to report.</summary>
    public string? Problem(AutostartStatus status) => status.State switch
    {
        AutostartState.Missing => $"Start with Windows points to {status.RegisteredExe}, which no longer exists. Use this copy, or switch it off.",
        AutostartState.OtherCopy => $"Start with Windows starts another copy: {status.RegisteredExe}, not this one ({currentExe}). Use this copy, or switch it off.",
        AutostartState.DisabledInWindows => "Start with Windows is switched off in Task Manager (Startup apps). Use this copy to switch it on again.",
        AutostartState.Off when !canRegister => "Start with Windows needs the published DeskBuddy.Companion.exe (tools\\companion.ps1 -Publish); this is a development build.",
        _ => null,
    };

    /// <summary>The executable of a startup command: the quoted path, or everything up to the first argument.</summary>
    public static string ExeOf(string command)
    {
        var c = command.Trim();
        if (c.StartsWith('"'))
        {
            var end = c.IndexOf('"', 1);
            return end > 0 ? c[1..end] : c[1..];
        }
        var space = c.IndexOf(" -", StringComparison.Ordinal);
        return space > 0 ? c[..space] : c;
    }

    private static bool SamePath(string a, string b)
    {
        try
        {
            return string.Equals(Path.GetFullPath(a), Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return false;
        }
    }

    /// <summary>The real thing, for the running executable. A published companion is one self-contained file; the
    /// development build has its DeskBuddy.Companion.dll beside the exe.</summary>
    public static Autostart ForThisProcess() =>
        new(new WindowsStartupRegistry(), Environment.ProcessPath ?? Application.ExecutablePath,
            canRegister: !File.Exists(Path.Combine(AppContext.BaseDirectory, "DeskBuddy.Companion.dll")),
            File.Exists);
}

/// <summary>HKCU's Run key, and Task Manager's switch for it (StartupApproved\Run: odd first byte = switched off).</summary>
public sealed class WindowsStartupRegistry : IStartupRegistry
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ApprovedKey = @"Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run";

    public string? Read(string name)
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        return key?.GetValue(name) as string is { Length: > 0 } command ? command : null;
    }

    public void Write(string name, string command)
    {
        using (var key = Registry.CurrentUser.CreateSubKey(RunKey))
        {
            key.SetValue(name, command, RegistryValueKind.String);
        }
        DeleteApproval(name); // the user asked for it: no longer switched off in Task Manager
    }

    public void Delete(string name)
    {
        using (var key = Registry.CurrentUser.OpenSubKey(RunKey, writable: true))
        {
            key?.DeleteValue(name, throwOnMissingValue: false);
        }
        DeleteApproval(name);
    }

    public bool IsDisabledInWindows(string name)
    {
        using var key = Registry.CurrentUser.OpenSubKey(ApprovedKey);
        return key?.GetValue(name) is byte[] { Length: > 0 } state && (state[0] & 1) == 1;
    }

    private static void DeleteApproval(string name)
    {
        using var key = Registry.CurrentUser.OpenSubKey(ApprovedKey, writable: true);
        key?.DeleteValue(name, throwOnMissingValue: false);
    }
}
