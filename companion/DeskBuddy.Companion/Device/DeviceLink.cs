using System.Collections.Concurrent;
using System.IO.Ports;
using System.Net.Sockets;
using System.Text;
using DeskBuddy.Companion.Protocol;

namespace DeskBuddy.Companion.Device;

/// <summary>
/// One open connection to DeskBuddy: sends commands, matches replies, and passes on events and the device's
/// log output. The byte stream is the board's UART, either a real serial port or the desktop simulator's TCP socket
/// (identical frames and log lines). Events are raised on a background thread.
/// </summary>
public sealed class DeviceLink : IDisposable
{
    /// <summary>The name under which the desktop simulator is listed, selected and shown instead of a COM port.</summary>
    public const string SimulatorName = "Simulator";
    /// <summary>TCP port the desktop simulator listens on (127.0.0.1 only).</summary>
    public const int SimulatorPort = 47801;
    private const string SimulatorHost = "127.0.0.1";

    private readonly Stream _stream;
    private readonly IDisposable _owner; // the SerialPort or TcpClient behind the stream
    private readonly CancellationTokenSource _stop = new();
    private readonly ConcurrentDictionary<uint, TaskCompletionSource<Frame>> _pending = new();
    private readonly object _writeLock = new();
    private uint _seq;
    private int _closed;

    /// <summary>A line of the device's log (not a frame).</summary>
    public event Action<string>? LogLine;
    /// <summary>A frame sent (true) or received (false), as text.</summary>
    public event Action<string, bool>? Traffic;
    public event Action<Frame>? EventReceived;
    /// <summary>A damaged frame or one of another protocol version.</summary>
    public event Action<string>? BadFrame;
    /// <summary>The port went away (unplugged) or failed. Not raised by <see cref="Dispose"/>.</summary>
    public event Action<string>? Lost;

    /// <summary>The COM port name, or <see cref="SimulatorName"/>.</summary>
    public string PortName { get; }

    public bool IsSimulator => PortName == SimulatorName;

    private DeviceLink(string portName, Stream stream, IDisposable owner)
    {
        PortName = portName;
        _stream = stream;
        _owner = owner;
    }

    /// <summary>
    /// Opens a serial port, or the simulator's TCP socket for <see cref="SimulatorName"/>. For serial, DTR and RTS
    /// stay off: on the CH343 they reset the board or enter its bootloader.
    /// </summary>
    public static DeviceLink Open(string portName)
    {
        var link = portName == SimulatorName ? OpenSimulator() : OpenSerial(portName);
        _ = Task.Run(link.ReadLoop);
        return link;
    }

    private static DeviceLink OpenSerial(string portName)
    {
        var port = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One)
        {
            DtrEnable = false,
            RtsEnable = false,
            Handshake = Handshake.None,
            WriteTimeout = 1000,
            Encoding = Encoding.ASCII,
        };
        port.Open();
        return new DeviceLink(portName, port.BaseStream, port);
    }

    private static DeviceLink OpenSimulator()
    {
        var client = new TcpClient { NoDelay = true };
        try
        {
            client.Connect(SimulatorHost, SimulatorPort);
        }
        catch (SocketException e)
        {
            client.Dispose();
            throw new IOException($"the simulator is not running ({e.Message})", e);
        }
        var stream = client.GetStream();
        stream.WriteTimeout = 1000;
        return new DeviceLink(SimulatorName, stream, client);
    }

    /// <summary>Sends a command and waits for its reply (ok or err). Throws TimeoutException without a reply.</summary>
    public async Task<Frame> RequestAsync(string body, TimeSpan timeout)
    {
        var seq = Interlocked.Increment(ref _seq);
        var reply = new TaskCompletionSource<Frame>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[seq] = reply;
        try
        {
            var line = Frame.Command(seq, body);
            lock (_writeLock)
            {
                _stream.Write(Encoding.ASCII.GetBytes(line));
            }
            Traffic?.Invoke(line.TrimEnd(), true);
            return await reply.Task.WaitAsync(timeout, _stop.Token);
        }
        catch (Exception e) when (e is IOException or InvalidOperationException or UnauthorizedAccessException)
        {
            OnLost($"write failed: {e.Message}");
            throw new IOException("DeskBuddy connection lost", e);
        }
        finally
        {
            _pending.TryRemove(seq, out _);
        }
    }

    private async Task ReadLoop()
    {
        var buffer = new byte[1024];
        var line = new StringBuilder();
        try
        {
            var stream = _stream;
            while (!_stop.IsCancellationRequested)
            {
                var n = await stream.ReadAsync(buffer, _stop.Token);
                if (n == 0)
                {
                    throw new IOException("end of stream");
                }
                for (var i = 0; i < n; i++)
                {
                    var c = (char)buffer[i];
                    if (c == '\n')
                    {
                        HandleLine(line.ToString().TrimEnd('\r'));
                        line.Clear();
                    }
                    else if (line.Length < 4096)
                    {
                        line.Append(c);
                    }
                }
            }
        }
        catch (OperationCanceledException) when (_stop.IsCancellationRequested)
        {
        }
        catch (Exception e)
        {
            OnLost(e.Message);
        }
    }

    private void HandleLine(string line)
    {
        var frame = Frame.TryParse(line, out var error);
        if (frame is null)
        {
            if (error is not null)
            {
                BadFrame?.Invoke($"{error}: {line}");
            }
            else if (line.Length > 0)
            {
                LogLine?.Invoke(line);
            }
            return;
        }
        Traffic?.Invoke(line, false);
        if (frame.IsEvent)
        {
            EventReceived?.Invoke(frame);
        }
        else if (_pending.TryGetValue(frame.Seq, out var reply))
        {
            reply.TrySetResult(frame);
        }
        else if (frame.IsError)
        {
            BadFrame?.Invoke($"device rejected a frame: {line}"); // e.g. bad_crc, sent with sequence 0
        }
    }

    private void OnLost(string reason)
    {
        if (Interlocked.Exchange(ref _closed, 1) == 0)
        {
            Shutdown();
            Lost?.Invoke(reason);
        }
    }

    private void Shutdown()
    {
        _stop.Cancel();
        try
        {
            _owner.Dispose();
        }
        catch (IOException)
        {
            // already gone (unplugged)
        }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) == 0)
        {
            Shutdown();
        }
    }
}
