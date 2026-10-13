# Runs idf.py inside the pinned ESP-IDF environment without polluting the caller's shell.
# Usage: tools/idf.ps1 [-C <project dir>] <idf.py args...>
#   tools/idf.ps1 -C _ref/ESP32-S3-Touch-LCD-7B/examples/ESP-IDF/13_lvgl_v8_demo build
#   tools/idf.ps1 -C firmware -p COM6 flash monitor
$ErrorActionPreference = 'Stop'

$IdfVersion = 'v5.5.5'
$IdfPath = if ($env:DESKBUDDY_IDF_PATH) { $env:DESKBUDDY_IDF_PATH } else { "C:\esp\$IdfVersion\esp-idf" }

if (-not (Test-Path "$IdfPath\export.ps1")) {
    Write-Error "ESP-IDF $IdfVersion not found at $IdfPath. See README.md > Toolchain setup."
}

# export.ps1 prints a long banner; keep only errors.
. "$IdfPath\export.ps1" | Out-Null

idf.py @args
exit $LASTEXITCODE
