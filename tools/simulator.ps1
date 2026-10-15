# Builds and starts the DeskBuddy simulator: the board's application (firmware/components/app) in a window on this
# PC, the mouse as the touchscreen. The companion connects to it on 127.0.0.1:47801 (choose "Simulator" in its port
# list; auto-detect picks it when no board is plugged in). Needs the MSVC toolchain, as tools/host-test.ps1.
#   tools/simulator.ps1                     # build (Release, no sanitizer) and start it
#   tools/simulator.ps1 -BuildOnly
#   tools/simulator.ps1 -Scale 2            # every panel pixel as 2x2 screen pixels
#   tools/simulator.ps1 -Port 47802         # another TCP port (the companion only looks on 47801)
#   tools/simulator.ps1 -Stage 70 -PanelLeft -Verify   # menuconfig's layout options and GFX_VERIFY
param([switch] $BuildOnly, [int] $Scale = 1, [int] $Port = 0, [int] $Stage = 0, [switch] $PanelLeft,
      [switch] $Verify)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$src = Join-Path $PSScriptRoot '..\firmware\tests\host'
$build = Join-Path $src 'build-sim'
# Reconfigure each time: the version (git describe) is read at configure time, as ESP-IDF does for the firmware.
Invoke-Msvc @(
    "cmake -S `"$src`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DHOST_SANITIZE=OFF",
    "cmake --build `"$build`" --target deskbuddy_sim"
)
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($BuildOnly) { exit 0 }

$simArgs = @('--scale', $Scale)
if ($Port) { $simArgs += '--port', $Port }
if ($Stage) { $simArgs += '--stage', $Stage }
if ($PanelLeft) { $simArgs += '--panel-left' }
if ($Verify) { $simArgs += '--verify' }
& (Join-Path $build 'deskbuddy_sim.exe') @simArgs # the log (as from the board's UART) in this console
exit $LASTEXITCODE
