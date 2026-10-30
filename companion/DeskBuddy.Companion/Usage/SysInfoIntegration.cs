using DeskBuddy.Companion.Pdc;

namespace DeskBuddy.Companion.Usage;

/// <summary>
/// The PC load strip, built in and off by default: while on, every <see cref="SampleInterval"/> it reads this PC's
/// processor, memory and C: disk (<see cref="SystemMeter"/>; no network at all) and offers the numbers to Buddy
/// (<c>sys.set</c>). The processor's load is the average since the previous reading. Used on the UI thread.
/// </summary>
public sealed class SysInfoIntegration
{
    public static readonly TimeSpan SampleInterval = TimeSpan.FromSeconds(5);
    /// <summary>From switching on to the first numbers: the processor needs a reading to compare with.</summary>
    public static readonly TimeSpan FirstSample = TimeSpan.FromSeconds(1);

    private readonly Func<SysReading> _read;
    private readonly PdcStore _store;
    private readonly TimeProvider _clock;
    private SysReading? _previous;
    private DateTimeOffset _nextSample;

    public SysInfoIntegration(Func<SysReading> read, PdcStore store, TimeProvider clock)
    {
        _read = read;
        _store = store;
        _clock = clock;
    }

    /// <summary>The real thing: this PC's counters, settings in %APPDATA%\DeskBuddy.</summary>
    public static SysInfoIntegration CreateDefault() =>
        new(SystemMeter.Read, new PdcStore(PdcStore.DefaultDirectory), TimeProvider.System);

    public bool Enabled { get; private set; }
    /// <summary>The numbers to show; null while off or before the first sample. This is also what the device is told.</summary>
    public SysSample? Sample { get; private set; }

    public event Action? Changed;
    public event Action<LogKind, string>? Log;

    /// <summary>Reads the setting.</summary>
    public void Start()
    {
        if (_store.LoadSysEnabled())
        {
            Activate();
        }
        Changed?.Invoke();
    }

    public void SetEnabled(bool on)
    {
        if (on == Enabled)
        {
            return;
        }
        _store.SaveSysEnabled(on);
        if (on)
        {
            Activate();
        }
        else
        {
            Enabled = false;
            Sample = null;
            _previous = null;
        }
        Log?.Invoke(LogKind.Info, on ? "PC load on" : "PC load off");
        Changed?.Invoke();
    }

    /// <summary>Takes a sample when one is due (call it often: every second).</summary>
    public void Tick()
    {
        var now = _clock.GetUtcNow();
        if (!Enabled || now < _nextSample)
        {
            return;
        }
        var reading = _read();
        Sample = SysSample.From(_previous, reading);
        _previous = reading;
        _nextSample = now + SampleInterval;
        Changed?.Invoke();
    }

    private void Activate()
    {
        Enabled = true;
        Sample = null;
        _previous = _read();
        _nextSample = _clock.GetUtcNow() + FirstSample;
    }
}
