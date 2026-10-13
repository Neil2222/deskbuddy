# Full-flash backup / restore / verify for the ESP32-S3-Touch-LCD-7B (16 MB flash).
# Uses the esptool bundled with the pinned ESP-IDF so there is exactly one toolchain.
#
#   tools/flash-backup.ps1 -Action backup  -Image backup\my_dump.bin
#   tools/flash-backup.ps1 -Action verify  -Image <factory dump>      (read-only)
#   tools/flash-backup.ps1 -Action restore -Image <factory dump>
#
# Port: -Port, else $env:ESPPORT, else auto-detected. Use the Type-C port labelled UART (CH343)
# with DIP switch #16 on the ESP32 side: its auto-reset enters the bootloader without buttons.
param(
    [Parameter(Mandatory)] [ValidateSet('backup', 'verify', 'restore')] [string] $Action,
    [Parameter(Mandatory)] [string] $Image,
    [string] $Port,
    [int] $Baud = 921600
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Enter-Idf
$Port = Resolve-BoardPort $Port
Write-Host "Using port $Port"

$FlashSize = 16MB
$common = @('--chip', 'esp32s3', '--port', $Port, '--baud', $Baud)

switch ($Action) {
    'backup' {
        if (Test-Path $Image) { throw "$Image already exists; refusing to overwrite a backup." }
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
        if ((Get-Item $Image).Length -ne $FlashSize) { throw "$Image is not a full 16 MB dump." }
        # Raw image: write byte-for-byte, do not let esptool patch the bootloader header.
        esptool.py @common write_flash --flash_mode keep --flash_freq keep --flash_size keep 0 $Image
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        esptool.py @common verify_flash 0 $Image
    }
}
exit $LASTEXITCODE
