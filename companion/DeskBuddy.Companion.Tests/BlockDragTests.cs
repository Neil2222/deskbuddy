using DeskBuddy.Companion.Schedule;
using Xunit;

namespace DeskBuddy.Companion.Tests;

// Dragging blocks on the timeline: snapping, the length kept on a move, the edges, and the day's bounds.
public class BlockDragTests
{
    private static ScheduleBlock B(string start, string end) =>
        new() { Start = DayTime.Parse(start)!.Value, End = DayTime.Parse(end)!.Value };

    private static (string, string) Times((int Start, int End) t) => (DayTime.Format(t.Start), DayTime.Format(t.End));

    [Fact]
    public void Moving_keeps_the_length_and_snaps_the_start_to_five_minutes()
    {
        var b = B("10:00", "10:12");
        Assert.Equal(("10:20", "10:32"), Times(BlockDrag.Apply(b, DragKind.Move, 21.7, precise: false)));
        Assert.Equal(("09:50", "10:02"), Times(BlockDrag.Apply(b, DragKind.Move, -8.4, precise: false)));
        Assert.Equal(("10:00", "10:12"), Times(BlockDrag.Apply(b, DragKind.Move, 2.4, precise: false))); // a small wobble
    }

    [Fact]
    public void Shift_gives_minute_precision()
    {
        var b = B("10:00", "10:12");
        Assert.Equal(("10:22", "10:34"), Times(BlockDrag.Apply(b, DragKind.Move, 21.7, precise: true)));
        Assert.Equal(("10:00", "10:13"), Times(BlockDrag.Apply(b, DragKind.ResizeEnd, 1.2, precise: true)));
    }

    [Fact]
    public void An_off_grid_block_lands_on_the_grid_when_moved()
    {
        Assert.Equal(("10:05", "10:17"), Times(BlockDrag.Apply(B("10:03", "10:15"), DragKind.Move, 1.5, precise: false)));
    }

    [Fact]
    public void Resizing_moves_one_edge_and_keeps_the_other()
    {
        var b = B("08:30", "12:00");
        Assert.Equal(("08:00", "12:00"), Times(BlockDrag.Apply(b, DragKind.ResizeStart, -31, precise: false)));
        Assert.Equal(("08:30", "12:45"), Times(BlockDrag.Apply(b, DragKind.ResizeEnd, 44, precise: false)));
    }

    [Fact]
    public void A_block_keeps_at_least_a_minute()
    {
        var b = B("10:00", "10:12");
        Assert.Equal(("10:11", "10:12"), Times(BlockDrag.Apply(b, DragKind.ResizeStart, 30, precise: false)));
        Assert.Equal(("10:00", "10:01"), Times(BlockDrag.Apply(b, DragKind.ResizeEnd, -30, precise: false)));
        Assert.Equal(("10:00", "10:01"), Times(BlockDrag.Apply(B("10:00", "10:05"), DragKind.ResizeEnd, -5, precise: false)));
    }

    [Fact]
    public void Nothing_crosses_midnight()
    {
        Assert.Equal(("23:00", "24:00"), Times(BlockDrag.Apply(B("22:00", "23:00"), DragKind.Move, 200, precise: false)));
        Assert.Equal(("00:00", "00:45"), Times(BlockDrag.Apply(B("00:30", "01:15"), DragKind.Move, -90, precise: false)));
        Assert.Equal(("00:00", "01:15"), Times(BlockDrag.Apply(B("00:30", "01:15"), DragKind.ResizeStart, -90, precise: false)));
        Assert.Equal(("22:00", "24:00"), Times(BlockDrag.Apply(B("22:00", "23:00"), DragKind.ResizeEnd, 200, precise: false)));
    }

    [Fact]
    public void A_drag_back_to_the_start_changes_nothing()
    {
        var b = B("10:00", "10:12");
        Assert.Equal((b.Start, b.End), BlockDrag.Apply(b, DragKind.Move, 0, precise: false));
        Assert.Equal((b.Start, b.End), BlockDrag.Apply(b, DragKind.ResizeEnd, 0, precise: true));
    }

    [Fact]
    public void Edges_are_grabbable_and_even_short_blocks_keep_a_body()
    {
        // A tall block: 6-pixel edges.
        Assert.Equal(DragKind.ResizeStart, BlockDrag.Zone(100, 160, 103, 6));
        Assert.Equal(DragKind.Move, BlockDrag.Zone(100, 160, 180, 6));
        Assert.Equal(DragKind.ResizeEnd, BlockDrag.Zone(100, 160, 257, 6));
        // A 5-minute block drawn at its minimum height (22 px): edges shrink to a quarter, the middle still moves it.
        Assert.Equal(DragKind.ResizeStart, BlockDrag.Zone(100, 22, 101, 6));
        Assert.Equal(DragKind.Move, BlockDrag.Zone(100, 22, 110, 6));
        Assert.Equal(DragKind.ResizeEnd, BlockDrag.Zone(100, 22, 120, 6));
    }

    [Fact]
    public void Dragging_a_layer_on_top_leaves_the_block_below_alone()
    {
        // The timeline drags the block it hit (the top layer); the result only concerns that block, and the day's
        // layers follow from the times.
        var day = WeekSchedule.Empty.WithDay(DayOfWeek.Wednesday, [B("08:30", "12:00") with { Mood = "working" }, B("10:00", "10:12") with { Mood = "peukie" }]);
        var top = day[DayOfWeek.Wednesday][1];
        var (start, end) = BlockDrag.Apply(top, DragKind.Move, 25, precise: false);
        var moved = day.Put(DayOfWeek.Wednesday, top with { Start = start, End = end });
        Assert.Equal(("08:30", "12:00"), (DayTime.Format(moved[DayOfWeek.Wednesday][0].Start), DayTime.Format(moved[DayOfWeek.Wednesday][0].End)));
        Assert.Equal("peukie", SchedulePlanner.Resolve(moved, new DateTime(2026, 10, 14, 10, 25, 0)).Mood);
        Assert.Equal("working", SchedulePlanner.Resolve(moved, new DateTime(2026, 10, 14, 10, 5, 0)).Mood);
    }
}
