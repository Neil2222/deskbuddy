# Builds and runs the host-side unit tests (no ESP32 needed), using the MSVC toolchain from Visual Studio
# (Build Tools with the "Desktop development with C++" workload, which includes the Windows SDK).
#   tools/host-test.ps1              # build + run tests
#   tools/host-test.ps1 -Snapshot    # also render the character poses to PNG (firmware/tests/host/build/snapshots)
#   tools/host-test.ps1 -Icons       # also export Buddy's artwork and app icon to assets/buddy (committed)
#   tools/host-test.ps1 -Assets      # also regenerate the Mechanical Skeleton's packed artwork (committed:
#                                    # firmware/components/office/src/office_skeleton_assets.c), then test it
param([switch] $Snapshot, [switch] $Icons, [switch] $Assets)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$src = Join-Path $PSScriptRoot '..\firmware\tests\host'
$build = Join-Path $src 'build'

$steps = @(
    "cmake -S `"$src`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo"
)
if ($Assets) {
    # The drawing code makes the data the firmware (and the tests below) are built with.
    $out = Join-Path $PSScriptRoot '..\firmware\components\office\src\office_skeleton_assets.c'
    $steps += "cmake --build `"$build`" --target skeleton_assets", "`"$build\skeleton_assets.exe`" `"$out`""
}
$steps += "cmake --build `"$build`"", "ctest --test-dir `"$build`" --output-on-failure"
if ($Snapshot) {
    $out = Join-Path $build 'snapshots'
    New-Item -ItemType Directory -Force $out | Out-Null
    $steps += "`"$build\buddy_snapshot.exe`" `"$out`""
}
if ($Icons) {
    $assets = Join-Path $PSScriptRoot '..\assets\buddy'
    New-Item -ItemType Directory -Force $assets | Out-Null
    $steps += "`"$build\buddy_icons.exe`" `"$assets`""
}
Invoke-Msvc $steps
exit $LASTEXITCODE
