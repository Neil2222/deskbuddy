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

# While this mutex exists, the Windows companion lets go of the board's port and does not reconnect. If it already
# exists, another DeskBuddy tool (the Companion's Firmware Manager, a backup) has the port: refuse.
$portHold = $null
if ($needsPort) {
    $created = $false
    $portHold = New-Object System.Threading.Mutex($true, 'Local\DeskBuddy.PortHold', [ref] $created)
    if (-not $created) {
        $portHold.Dispose()
        throw 'Another DeskBuddy tool (the Companion''s Firmware Manager, a flash backup) is using the board''s port.'
    }
}
try {
    idf.py @args
} finally {
    if ($portHold) { $portHold.ReleaseMutex(); $portHold.Dispose() }
}
exit $LASTEXITCODE
