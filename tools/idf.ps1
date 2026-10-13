# Runs idf.py inside the pinned ESP-IDF environment without polluting the caller's shell.
# The serial port is auto-detected (or taken from -p / $env:ESPPORT) for commands that need it.
#
#   tools/idf.ps1 -C firmware/bringup build
#   tools/idf.ps1 -C firmware/bringup flash monitor
#   tools/idf.ps1 -C firmware/bringup -p COM7 flash
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Enter-Idf

$needsPort = $args | Where-Object { $_ -in 'flash', 'app-flash', 'monitor', 'erase-flash', 'erase_flash' }
$hasPort = $args | Where-Object { $_ -in '-p', '--port' }
if ($needsPort -and -not $hasPort) {
    $env:ESPPORT = Resolve-BoardPort
    Write-Host "Using port $env:ESPPORT"
}

idf.py @args
exit $LASTEXITCODE
