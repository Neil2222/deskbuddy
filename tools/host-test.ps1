# Builds and runs the host-side unit tests and simulator scenarios (no ESP32 needed), with AddressSanitizer.
#   tools/host-test.ps1              # build + run tests
#   tools/host-test.ps1 -Snapshot    # also render the character poses to PNG (firmware/host/build/test/snapshots)
param([switch] $Snapshot)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$b = Get-HostBuildCommands -Name 'test' -BuildType 'RelWithDebInfo' -CMakeArgs @('-DHOST_SANITIZE=ON')
$steps = $b.Commands + "ctest --test-dir `"$($b.Dir)`" --output-on-failure"
if ($Snapshot) {
    $out = Join-Path $b.Dir 'snapshots'
    New-Item -ItemType Directory -Force $out | Out-Null
    $steps += "`"$($b.Dir)\buddy_snapshot.exe`" `"$out`""
}
exit (Invoke-InMsvc $steps)
