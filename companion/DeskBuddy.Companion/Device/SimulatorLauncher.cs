using System.Diagnostics;
using System.Net.Sockets;
using System.Text.RegularExpressions;

namespace DeskBuddy.Companion.Device;

public enum SimulatorState
{
    Unavailable, // no DeskBuddy repository around this exe
    Idle,
    Building,    // tools/simulator.ps1 is building it
    Running,     // started from here and drawn
    Failed,      // the build or the start failed (see the log)
}

/// <summary>
/// Builds the desktop simulator from the DeskBuddy repository this companion was built in and starts it, through
/// tools/simulator.ps1 (the latest code of the checkout). Only available when that script is found in a folder
/// above the exe (a development build or companion\publish); a companion copied elsewhere has no simulator.
/// Events are raised on the thread that created it (the UI thread).
/// </summary>
public sealed partial class SimulatorLauncher : IDisposable
{
    private const string Script = @"tools\simulator.ps1";
    private const string ReadyLine = "both buffers drawn"; // the simulator's log once its window is lit

    private readonly SynchronizationContext _ui;
    private readonly string? _script;
    private Process? _process;

    public SimulatorState State { get; private set; }
    /// <summary>What the simulator is doing, in a line for the window.</summary>
    public string StatusText { get; private set; } = "";

    public event Action? Changed;
    /// <summary>A line of the build's output worth showing (warnings, errors, the result), or a note of our own.</summary>
    public event Action<LogKind, string>? Output;
    /// <summary>The simulator is up and listens for the companion.</summary>
    public event Action? Ready;

    public SimulatorLauncher() : this(FindScript(AppContext.BaseDirectory))
    {
    }

    public SimulatorLauncher(string? script)
    {
        _ui = SynchronizationContext.Current ?? throw new InvalidOperationException("create on the UI thread");
        _script = script;
        SetState(script is null ? SimulatorState.Unavailable : SimulatorState.Idle,
                 script is null ? "Needs the DeskBuddy repository: start the companion from your checkout"
                                : "Not running");
    }

    public bool IsAvailable => _script is not null;
    public bool IsBusy => State is SimulatorState.Building or SimulatorState.Running;

    /// <summary>The repository's tools\simulator.ps1 in <paramref name="start"/> or a folder above it; null if none.</summary>
    public static string? FindScript(string start)
    {
        for (var dir = new DirectoryInfo(start); dir is not null; dir = dir.Parent)
        {
            var script = Path.Combine(dir.FullName, Script);
            if (File.Exists(script) && Directory.Exists(Path.Combine(dir.FullName, "firmware", "simulator")))
            {
                return script;
            }
        }
        return null;
    }

    /// <summary>
    /// Builds and starts the simulator; one started from here before is closed first (its exe would be locked). If
    /// a simulator started elsewhere already listens, it is used as it is.
    /// </summary>
    public async Task StartAsync()
    {
        if (_script is null)
        {
            return;
        }
        await StopAsync();
        if (await ListeningAsync())
        {
            SetState(SimulatorState.Idle, "Already running (started elsewhere)");
            Output?.Invoke(LogKind.Info, "Simulator: already running, not started from here: connecting to it");
            Ready?.Invoke();
            return;
        }
        var info = new ProcessStartInfo("powershell.exe")
        {
            UseShellExecute = false,
            CreateNoWindow = true, // the simulator's own window still shows; its log reaches us over the link
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            WorkingDirectory = Path.GetDirectoryName(Path.GetDirectoryName(_script))!,
        };
        foreach (var arg in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", _script })
        {
            info.ArgumentList.Add(arg);
        }
        var process = new Process { StartInfo = info, EnableRaisingEvents = true };
        process.OutputDataReceived += (_, e) => Post(() => OnLine(process, e.Data));
        process.ErrorDataReceived += (_, e) => Post(() => OnLine(process, e.Data));
        process.Exited += (_, _) => Post(() => OnExited(process));
        try
        {
            process.Start();
        }
        catch (Exception e) when (e is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            process.Dispose();
            SetState(SimulatorState.Failed, $"Cannot start PowerShell: {e.Message}");
            return;
        }
        _process = process;
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        SetState(SimulatorState.Building, "Building the latest code…");
        Output?.Invoke(LogKind.Info, $"Simulator: building and starting ({_script})");
    }

    /// <summary>Closes the simulator started from here (and its build), if any.</summary>
    public async Task StopAsync()
    {
        if (_process is not { } process)
        {
            return;
        }
        _process = null; // its Exited is ours no more
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
            }
        }
        catch (Exception e) when (e is InvalidOperationException or System.ComponentModel.Win32Exception or TimeoutException)
        {
            // already gone
        }
        process.Dispose();
        SetState(SimulatorState.Idle, "Closed");
    }

    private void OnLine(Process process, string? line)
    {
        if (process != _process || line is null)
        {
            return;
        }
        if (State == SimulatorState.Building)
        {
            if (line.Contains(ReadyLine, StringComparison.Ordinal))
            {
                SetState(SimulatorState.Running, "Running");
                Output?.Invoke(LogKind.Info, "Simulator: running");
                Ready?.Invoke();
            }
            else if (Noteworthy().IsMatch(line))
            {
                Output?.Invoke(LogKind.Warning, $"Simulator build: {line.Trim()}");
            }
        }
        // While running, its log and frames arrive over the link as well: not repeated here.
    }

    private void OnExited(Process process)
    {
        if (process != _process)
        {
            return;
        }
        _process = null;
        var code = process.ExitCode;
        process.Dispose();
        if (State == SimulatorState.Building || code != 0)
        {
            SetState(SimulatorState.Failed, $"Build or start failed (exit code {code}): see Diagnostics");
            Output?.Invoke(LogKind.Warning, $"Simulator: build or start failed (exit code {code})");
        }
        else
        {
            SetState(SimulatorState.Idle, "Closed");
            Output?.Invoke(LogKind.Info, "Simulator: closed");
        }
    }

    private static async Task<bool> ListeningAsync()
    {
        using var probe = new TcpClient();
        try
        {
            await probe.ConnectAsync("127.0.0.1", DeviceLink.SimulatorPort).WaitAsync(TimeSpan.FromSeconds(1));
            return true;
        }
        catch (Exception e) when (e is SocketException or TimeoutException)
        {
            return false;
        }
    }

    // Compiler and linker problems, CMake/script errors: what explains a failed build. Not the progress lines.
    [GeneratedRegex(@"error|warning|not found|not recognized|cannot|failed|exception", RegexOptions.IgnoreCase)]
    private static partial Regex Noteworthy();

    private void SetState(SimulatorState state, string text)
    {
        State = state;
        StatusText = text;
        Changed?.Invoke();
    }

    private void Post(Action action) => _ui.Post(_ => action(), null);

    /// <summary>Leaves a running simulator open (it is a window of its own); only stops listening to it.</summary>
    public void Dispose()
    {
        _process?.Dispose();
        _process = null;
    }
}
