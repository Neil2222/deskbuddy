using System.Threading.Channels;
using DeskBuddy.Companion.Protocol;

namespace DeskBuddy.Companion.History;

/// <summary>
/// Runs a <see cref="HistoryLedger"/> on a worker of its own, so files are read and written off the UI thread and
/// events are handled in the order they arrived. Every call only queues work; after each batch the ledger commits
/// (log lines on disk, then checkpoints) and, when it needs one, <see cref="FetchNeeded"/> asks for a
/// <c>log.since</c>. Events, logs and changes are raised on the worker.
/// </summary>
public sealed class HistoryRecorder : IDisposable
{
    private readonly HistoryLedger _ledger;
    private readonly Channel<(Action? Run, TaskCompletionSource? Done)> _queue =
        Channel.CreateUnbounded<(Action?, TaskCompletionSource?)>(new UnboundedChannelOptions { SingleReader = true });
    private readonly Task _worker;
    private readonly TimeProvider _time;
    private bool _usable;
    private int _lastIid; // the Companion's request ids: 1..2^31-1 (the device's own have the top bit set)

    public HistoryRecorder(string directory, HistorySource source, TimeProvider? time = null)
    {
        _time = time ?? TimeProvider.System;
        Source = source;
        _ledger = new HistoryLedger(directory, source, Installation.Id(directory));
        _ledger.Log += text => Log?.Invoke(text);
        _lastIid = Random.Shared.Next(1, 1 << 30); // somewhere new each run, in case the journal cannot be read
        _worker = Task.Run(RunAsync);
        Post(() =>
        {
            try
            {
                _ledger.Load();
                _usable = true;
                if (_ledger.MaxRequestIid is > 0 and < int.MaxValue)
                {
                    Interlocked.Exchange(ref _lastIid, (int)_ledger.MaxRequestIid); // continue above the last one used
                }
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                Log?.Invoke($"History ({source}) cannot be read, so nothing is recorded this run: {e.Message}");
            }
        });
    }

    public HistorySource Source { get; }
    public HistorySnapshot Snapshot => _ledger.Snapshot;

    /// <summary>A <c>log.since</c> should be sent; answer with <see cref="ReplayAnswered"/> or <see cref="ReplayFailed"/>.</summary>
    public event Action<FetchRequest>? FetchNeeded;
    public event Action<string>? Log;
    public event Action? Changed;

    /// <summary>A fresh id for an <c>act</c> the Companion sends.</summary>
    public uint NextIid()
    {
        var id = Interlocked.Increment(ref _lastIid);
        if (id <= 0)
        {
            Interlocked.Exchange(ref _lastIid, 1);
            id = 1;
        }
        return (uint)id;
    }

    public void Connected(string dev, string boot, string? firmware) => Post(now => _ledger.Connected(dev, boot, firmware, now));
    public void Disconnected() => Post(_ => _ledger.Disconnected());

    /// <summary>An event frame, straight from the link's reader (so the order is the device's).</summary>
    public void Received(Frame frame)
    {
        var at = _time.GetUtcNow();
        Post(() => _ledger.Received(frame, at));
    }

    public void ReplayAnswered(FetchRequest request, Frame reply) => Post(now => _ledger.ReplayAnswered(request, reply, now));
    public void ReplayFailed(FetchRequest request, string reason) => Post(now => _ledger.ReplayFailed(request, reason, now));
    public void Requested(uint iid, string action, string origin) => Post(now => _ledger.Requested(iid, action, origin, now));
    public void RequestAnswered(uint iid, string result) => Post(now => _ledger.RequestAnswered(iid, result, now));

    /// <summary>Re-checks whether a postponed <c>log.since</c> is due (call every second or so).</summary>
    public void Tick() => Post(() => { });

    /// <summary>Completes when everything queued before it is handled and on disk.</summary>
    public Task FlushAsync()
    {
        var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!_queue.Writer.TryWrite((null, done)))
        {
            done.TrySetResult();
        }
        return done.Task;
    }

    private void Post(Action<DateTimeOffset> action) => Post(() => action(_time.GetUtcNow()));

    private void Post(Action action) => _queue.Writer.TryWrite((action, null));

    private async Task RunAsync()
    {
        var reader = _queue.Reader;
        while (await reader.WaitToReadAsync())
        {
            var done = new List<TaskCompletionSource>();
            while (reader.TryRead(out var item))
            {
                if (item.Done is { } d)
                {
                    done.Add(d);
                    continue;
                }
                try
                {
                    item.Run!();
                }
                catch (Exception e)
                {
                    Log?.Invoke($"History: {e.Message}");
                }
            }
            if (_usable)
            {
                try
                {
                    if (_ledger.Commit())
                    {
                        Changed?.Invoke();
                    }
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException)
                {
                    Log?.Invoke($"History: cannot write ({e.Message}); kept in memory and written later");
                }
                if (_ledger.TakeFetch(_time.GetUtcNow()) is { } fetch)
                {
                    FetchNeeded?.Invoke(fetch);
                }
            }
            foreach (var d in done)
            {
                d.TrySetResult();
            }
        }
    }

    public void Dispose()
    {
        _queue.Writer.TryComplete();
        _worker.Wait(TimeSpan.FromSeconds(3)); // the last lines reach the disk
    }
}
