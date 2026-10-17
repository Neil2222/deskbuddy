namespace DeskBuddy.Firmware;

/// <summary>A region a package writes: one image at a fixed offset.</summary>
public sealed record ImageSlot(string Name, string File, uint Offset, uint MaxSize, bool IsApp);

/// <summary>
/// What a package for a board must look like. The layout is fixed here, in the Companion, not taken from the package:
/// a package that would change the partition table, or write anywhere else, is refused (changing the layout needs its
/// own, approved, migration; docs/firmware-packages.md).
/// </summary>
public sealed record BoardProfile(
    string Board,
    string Chip,
    ushort ChipId,
    string FlashSize,
    string FlashMode,
    string FlashFreq,
    IReadOnlyList<ImageSlot> Images,
    IReadOnlyList<Partition> Partitions,
    IReadOnlyList<Partition> Preserved)
{
    public const uint SectorSize = 0x1000;

    /// <summary>
    /// The Waveshare ESP32-S3-Touch-LCD-7B (docs/hardware.md) with firmware/deskbuddy/partitions.csv. The factory
    /// firmware uses the same nvs and phy_init offsets, so 0x9000-0xFFFF (its calibration data) is never written.
    /// </summary>
    public static BoardProfile WaveshareS3Lcd7B { get; } = new(
        Board: "waveshare-esp32-s3-touch-lcd-7b",
        Chip: "esp32s3",
        ChipId: EspImage.ChipIdEsp32S3,
        FlashSize: "16MB",
        FlashMode: "dio",
        FlashFreq: "80m",
        Images:
        [
            new("bootloader", "bootloader.bin", 0x0, 0x8000, IsApp: false),
            new("partition-table", "partition-table.bin", 0x8000, PartitionTable.MaxSize, IsApp: false),
            new("app", "deskbuddy.bin", 0x10000, 0x300000, IsApp: true),
        ],
        Partitions:
        [
            new("nvs", 0x01, 0x02, 0x9000, 0x6000),
            new("phy_init", 0x01, 0x01, 0xF000, 0x1000),
            new("factory", 0x00, 0x00, 0x10000, 0x300000),
        ],
        Preserved:
        [
            new("nvs", 0x01, 0x02, 0x9000, 0x6000),
            new("phy_init", 0x01, 0x01, 0xF000, 0x1000),
        ]);

    public static IReadOnlyList<BoardProfile> All { get; } = [WaveshareS3Lcd7B];

    public static BoardProfile? Find(string board) => All.FirstOrDefault(p => p.Board == board);
}
