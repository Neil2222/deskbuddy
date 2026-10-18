using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text;

namespace DeskBuddy.Firmware.Flashing;

/// <summary>What to run: an executable, its arguments (one per element, never a shell string) and a working directory.</summary>
public sealed record ProcessSpec(string FileName, IReadOnlyList<string> Arguments, string WorkingDirectory);

/// <summary>A started process: its output arrives as lines; <see cref="Kill"/> ends it and everything it started.</summary>
public interface IRunningProcess : IDisposable
{
    /// <summary>The exit code once the process and its output have ended.</summary>
    Task<int> Exited { get; }
    void Kill();
}

/// <summary>Starts processes. The real one is <see cref="SystemProcessRunner"/>; tests script the output.</summary>
public interface IProcessRunner
{
    /// <param name="onLine">Each line of standard output and standard error, as it arrives (any thread). Lines end at
    /// \n, \r or \b: esptool overwrites progress in place with \r and \b.</param>
    IRunningProcess Start(ProcessSpec spec, Action<string> onLine);
}

/// <summary>
/// Runs a real process without a window, with its output redirected, inside a Windows job object that kills the whole
/// tree when the job is closed. esptool.exe is a PyInstaller program: the exe that is started unpacks and starts a
/// second process that does the work, so killing only the first would leave the second holding the COM port. The job
/// also ends the tree if the Companion itself exits.
/// </summary>
public sealed class SystemProcessRunner : IProcessRunner
{
    public IRunningProcess Start(ProcessSpec spec, Action<string> onLine)
    {
        var psi = new ProcessStartInfo(spec.FileName)
        {
            WorkingDirectory = spec.WorkingDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            RedirectStandardInput = true, // nothing is ever typed: a prompt would read end-of-file, not hang
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };
        foreach (var a in spec.Arguments)
        {
            psi.ArgumentList.Add(a);
        }
        // esptool reads settings from ESPTOOL_* variables and config files: neither may change what is run.
        foreach (var key in psi.Environment.Keys.Where(k => k.StartsWith("ESPTOOL", StringComparison.OrdinalIgnoreCase)).ToList())
        {
            psi.Environment.Remove(key);
        }
        psi.Environment["PYTHONUNBUFFERED"] = "1";
        psi.Environment["PYTHONIOENCODING"] = "utf-8";

        var job = OperatingSystem.IsWindows() ? KillOnCloseJob.Create() : null;
        var process = new Process { StartInfo = psi };
        try
        {
            process.Start();
        }
        catch
        {
            job?.Dispose();
            process.Dispose();
            throw;
        }
        if (job is not null && OperatingSystem.IsWindows())
        {
            job.Assign(process);
        }
        process.StandardInput.Close();
        return new Running(process, job, onLine);
    }

    private sealed class Running : IRunningProcess
    {
        private readonly Process _process;
        private readonly KillOnCloseJob? _job;

        public Task<int> Exited { get; }

        public Running(Process process, KillOnCloseJob? job, Action<string> onLine)
        {
            _process = process;
            _job = job;
            var stdout = Pump(process.StandardOutput, onLine);
            var stderr = Pump(process.StandardError, onLine);
            Exited = Task.Run(async () =>
            {
                await process.WaitForExitAsync().ConfigureAwait(false);
                await Task.WhenAll(stdout, stderr).ConfigureAwait(false);
                return process.ExitCode;
            });
        }

        private static Task Pump(StreamReader reader, Action<string> onLine) => Task.Run(async () =>
        {
            var line = new StringBuilder();
            var buffer = new char[4096];
            int n;
            while ((n = await reader.ReadAsync(buffer).ConfigureAwait(false)) > 0)
            {
                for (var i = 0; i < n; i++)
                {
                    var c = buffer[i];
                    if (c is '\n' or '\r' or '\b')
                    {
                        if (line.Length > 0)
                        {
                            onLine(line.ToString());
                            line.Clear();
                        }
                    }
                    else if (line.Length < 4096)
                    {
                        line.Append(c);
                    }
                }
            }
            if (line.Length > 0)
            {
                onLine(line.ToString());
            }
        });

        public void Kill()
        {
            if (_job is not null && OperatingSystem.IsWindows())
            {
                _job.Terminate();
            }
            try
            {
                _process.Kill(entireProcessTree: true);
            }
            catch (Exception e) when (e is InvalidOperationException or Win32Exception or NotSupportedException)
            {
                // already gone
            }
        }

        public void Dispose()
        {
            _job?.Dispose(); // closing the job ends whatever is still in it
            _process.Dispose();
        }
    }
}

/// <summary>A job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: everything in it ends when it is closed.</summary>
internal sealed class KillOnCloseJob : IDisposable
{
    private IntPtr _handle;

    private KillOnCloseJob(IntPtr handle) => _handle = handle;

    [SupportedOSPlatform("windows")]
    public static KillOnCloseJob? Create()
    {
        var handle = CreateJobObjectW(IntPtr.Zero, null);
        if (handle == IntPtr.Zero)
        {
            return null;
        }
        var info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        var size = Marshal.SizeOf<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>();
        var ptr = Marshal.AllocHGlobal(size);
        try
        {
            Marshal.StructureToPtr(info, ptr, false);
            if (!SetInformationJobObject(handle, JobObjectExtendedLimitInformation, ptr, (uint)size))
            {
                CloseHandle(handle);
                return null;
            }
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
        return new KillOnCloseJob(handle);
    }

    /// <summary>Puts a just-started process in the job; the processes it starts from then on are in it too.</summary>
    [SupportedOSPlatform("windows")]
    public void Assign(Process process)
    {
        if (!AssignProcessToJobObject(_handle, process.Handle))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "could not put the flashing tool in a job object");
        }
    }

    [SupportedOSPlatform("windows")]
    public void Terminate()
    {
        if (_handle != IntPtr.Zero)
        {
            TerminateJobObject(_handle, 1);
        }
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero)
        {
            CloseHandle(_handle);
            _handle = IntPtr.Zero;
        }
    }

    private const int JobObjectExtendedLimitInformation = 9;
    private const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000;

    [StructLayout(LayoutKind.Sequential)]
    private struct JOBOBJECT_BASIC_LIMIT_INFORMATION
    {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct IO_COUNTERS
    {
        public ulong ReadOperationCount, WriteOperationCount, OtherOperationCount;
        public ulong ReadTransferCount, WriteTransferCount, OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        public IO_COUNTERS IoInfo;
        public UIntPtr ProcessMemoryLimit;
        public UIntPtr JobMemoryLimit;
        public UIntPtr PeakProcessMemoryUsed;
        public UIntPtr PeakJobMemoryUsed;
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateJobObjectW(IntPtr attributes, string? name);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetInformationJobObject(IntPtr job, int infoClass, IntPtr info, uint length);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool TerminateJobObject(IntPtr job, uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);
}
