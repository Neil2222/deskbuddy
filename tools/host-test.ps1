# Builds and runs the host-side unit tests (no ESP32 needed), using the MSVC toolchain from Visual Studio
# (Build Tools with the "Desktop development with C++" workload, which includes the Windows SDK).
#   tools/host-test.ps1              # build + run tests
#   tools/host-test.ps1 -Snapshot    # also render the character poses to PNG (firmware/tests/host/build/snapshots)
#   tools/host-test.ps1 -Icons       # also export Buddy's artwork and app icon to assets/buddy (committed)
param([switch] $Snapshot, [switch] $Icons)
$ErrorActionPreference = 'Stop'

$src = Join-Path $PSScriptRoot '..\firmware\tests\host'
$build = Join-Path $src 'build'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio Installer not found; install Visual Studio Build Tools with the C++ workload.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC not found; install Visual Studio Build Tools with the C++ workload.' }

# VsDevCmd normally finds the Windows SDK with reg.exe, which group policy blocks on managed machines
# ("Registry editing has been disabled"). Locate the SDK here instead (PowerShell's registry provider is not
# affected) and pass it in with -winsdk=none.
$sdkDir = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\Windows\v10.0' -ErrorAction SilentlyContinue).InstallationFolder
if (-not $sdkDir) { throw 'Windows SDK not found; install it with the Visual Studio C++ workload.' }
$sdkVer = Get-ChildItem (Join-Path $sdkDir 'Include') -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'um\winsdkver.h') } |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1 -ExpandProperty Name
if (-not $sdkVer) { throw "No complete Windows SDK under $sdkDir" }
$env:WindowsSdkDir = $sdkDir
$env:WindowsSDKVersion = "$sdkVer\"
$env:UniversalCRTSdkDir = $sdkDir
$env:UCRTVersion = $sdkVer
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'

$steps = @(
    "cmake -S `"$src`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "cmake --build `"$build`"",
    "ctest --test-dir `"$build`" --output-on-failure"
)
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
cmd /c "`"$devcmd`" -no_logo -arch=x64 -host_arch=x64 -winsdk=none && $($steps -join ' && ')"
exit $LASTEXITCODE
