# Full-flash backup / restore / verify for the ESP32-S3-Touch-LCD-7B (16 MB flash).
# Uses the esptool bundled with the pinned ESP-IDF so there is exactly one toolchain.
#
#   tools/flash-backup.ps1 -Port COM6 -Action backup  -Image backup/my_dump.bin
#   tools/flash-backup.ps1 -Port COM6 -Action verify  -Image backup/factory_full_16MB.bin   (read-only)
#   tools/flash-backup.ps1 -Port COM6 -Action restore -Image backup/factory_full_16MB.bin
#
# Use the Type-C port labelled UART (CH343) with DIP switch #16 set to the ESP32 position:
# its DTR/RTS auto-reset circuit enters the ROM bootloader without pressing any buttons.
param(
    [Parameter(Mandatory)] [string] $Port,
    [Parameter(Mandatory)] [ValidateSet('backup', 'verify', 'restore')] [string] $Action,
    [Parameter(Mandatory)] [string] $Image,
    [int] $Baud = 921600
)
$ErrorActionPreference = 'Stop'

$IdfVersion = 'v5.5.5'
$IdfPath = if ($env:DESKBUDDY_IDF_PATH) { $env:DESKBUDDY_IDF_PATH } else { "C:\esp\$IdfVersion\esp-idf" }
. "$IdfPath\export.ps1" | Out-Null

$FlashSize = 16MB
$common = @('--chip', 'esp32s3', '--port', $Port, '--baud', $Baud)

switch ($Action) {
    'backup' {
        if (Test-Path $Image) { Write-Error "$Image already exists; refusing to overwrite a backup." }
        esptool.py @common read_flash 0 $FlashSize $Image
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        esptool.py @common verify_flash 0 $Image
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        (Get-FileHash -Algorithm SHA256 $Image).Hash.ToLower()
    }
    'verify' {
        esptool.py @common verify_flash 0 $Image
    }
    'restore' {
        if ((Get-Item $Image).Length -ne $FlashSize) { Write-Error "$Image is not a full 16 MB dump." }
        # Raw image: write byte-for-byte, do not let esptool patch the bootloader header.
        esptool.py @common write_flash --flash_mode keep --flash_freq keep --flash_size keep 0 $Image
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        esptool.py @common verify_flash 0 $Image
    }
}
exit $LASTEXITCODE
