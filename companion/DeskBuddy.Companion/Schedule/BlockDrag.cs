namespace DeskBuddy.Companion.Schedule;

/// <summary>What a drag on the timeline changes.</summary>
public enum DragKind
{
    Move,        // the body: start and end together, the length stays
    ResizeStart, // the top edge: the end stays
    ResizeEnd,   // the bottom edge: the start stays
}

/// <summary>
/// Dragging blocks on the day timeline, as pure functions: which part of a block is under the pointer, and where a
/// drag puts the block. Times snap to 5 minutes (1 with Shift); a block keeps at least a minute and stays inside its
/// day (00:00–24:00: a drag never carries it over midnight).
/// </summary>
public static class BlockDrag
{
    public const int Snap = 5;
    public const int MinMinutes = 1;

    /// <summary>
    /// The part of a drawn block at <paramref name="y"/> (device pixels, like the rest). The edges are
    /// <paramref name="edge"/> pixels tall but never more than a quarter of the block each, so even a short block
    /// keeps a body to move it by.
    /// </summary>
    public static DragKind Zone(int top, int height, int y, int edge)
    {
        var e = Math.Max(1, Math.Min(edge, height / 4));
        return y < top + e ? DragKind.ResizeStart : y >= top + height - e ? DragKind.ResizeEnd : DragKind.Move;
    }

    /// <summary>
    /// Where <paramref name="block"/> ends up after the pointer moved <paramref name="deltaMinutes"/> (any fraction;
    /// pixels times minutes per pixel). The moved edge (both, for a move: the start) lands on the 5-minute grid, or on
    /// the minute when <paramref name="precise"/>.
    /// </summary>
    public static (int Start, int End) Apply(ScheduleBlock block, DragKind kind, double deltaMinutes, bool precise)
    {
        var grid = precise ? 1 : Snap;
        switch (kind)
        {
        case DragKind.Move:
        {
            var start = Round(block.Start + deltaMinutes, grid);
            start = Math.Clamp(start, 0, DayTime.Minutes - block.Duration);
            return (start, start + block.Duration);
        }
        case DragKind.ResizeStart:
        {
            var start = Math.Clamp(Round(block.Start + deltaMinutes, grid), 0, block.End - MinMinutes);
            return (start, block.End);
        }
        default:
        {
            var end = Math.Clamp(Round(block.End + deltaMinutes, grid), block.Start + MinMinutes, DayTime.Minutes);
            return (block.Start, end);
        }
        }
    }

    private static int Round(double minute, int grid) => (int)Math.Round(minute / grid, MidpointRounding.AwayFromZero) * grid;
}
