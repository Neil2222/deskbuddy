# Shared helpers dot-sourced by the other scripts. Single source of truth for the pinned toolchain.

$script:IdfVersion = 'v5.5.5'

# ESP-IDF checkout location: $env:DESKBUDDY_IDF_PATH, else %USERPROFILE%\esp\<version>\esp-idf.
function Get-IdfPath {
    if ($env:DESKBUDDY_IDF_PATH) { return $env:DESKBUDDY_IDF_PATH }
    return Join-Path $env:USERPROFILE "esp\$script:IdfVersion\esp-idf"
}

function Enter-Idf {
    $idf = Get-IdfPath
    if (-not (Test-Path (Join-Path $idf 'export.ps1'))) {
        throw "ESP-IDF $script:IdfVersion not found at $idf. Run tools/setup-idf.ps1 (or set DESKBUDDY_IDF_PATH)."
    }
    $actual = (git -C $idf describe --tags --exact-match 2>$null)
    if ($actual -ne $script:IdfVersion) {
        throw "ESP-IDF at $idf is '$actual', expected $script:IdfVersion."
    }
    . (Join-Path $idf 'export.ps1') | Out-Null
}

# Serial port resolution: explicit value, else $env:ESPPORT, else the single connected board port.
# CH343 USB-UART (1A86:55D3) is preferred: it auto-resets into the bootloader on any firmware.
function Resolve-BoardPort([string] $Port) {
    if ($Port) { return $Port }
    if ($env:ESPPORT) { return $env:ESPPORT }
    $found = foreach ($vidpid in 'VID_1A86&PID_55D3', 'VID_303A&PID_1001') {
        Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -match [regex]::Escape($vidpid) } |
            ForEach-Object { if ($_.FriendlyName -match '\((COM\d+)\)') { $Matches[1] } }
    }
    $found = @($found | Select-Object -Unique)
    if ($found.Count -eq 0) { throw 'No ESP32-S3 board port found. Connect the UART Type-C port or pass -Port / set ESPPORT.' }
    return $found[0] # CH343 entries come first
}

# ---- Host (PC) build: firmware/host with MSVC from Visual Studio (Build Tools, "Desktop development with C++") ----
$script:HostSrc = Join-Path $PSScriptRoot '..\firmware\host'

# Runs commands (joined with &&) in an x64 Visual Studio developer environment. Returns the exit code.
# VsDevCmd normally finds the Windows SDK with reg.exe, which group policy blocks on managed machines
# ("Registry editing has been disabled"). The SDK is located here instead (PowerShell's registry provider is not
# affected) and passed in with -winsdk=none.
function Invoke-InMsvc([string[]] $Commands) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio Installer not found; install Visual Studio Build Tools with the C++ workload (or see README for MinGW).' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC not found; install Visual Studio Build Tools with the C++ workload.' }
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
    cmd /c "`"$devcmd`" -no_logo -arch=x64 -host_arch=x64 -winsdk=none && $($Commands -join ' && ')" | Out-Host
    return $LASTEXITCODE
}

# CMake configure + build of firmware/host into build\<Name>. Extra CMake arguments are passed through.
function Get-HostBuildCommands([string] $Name, [string] $BuildType, [string[]] $CMakeArgs, [string] $Target) {
    $build = Join-Path $script:HostSrc "build\$Name"
    $configure = "cmake -S `"$script:HostSrc`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$BuildType $($CMakeArgs -join ' ')"
    $compile = if ($Target) { "cmake --build `"$build`" --target $Target" } else { "cmake --build `"$build`"" }
    return @{ Dir = $build; Commands = @($configure, $compile) }
}
