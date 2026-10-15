# Shared helpers dot-sourced by the other scripts. Single source of truth for the pinned toolchain.

$script:IdfVersion = 'v5.5.5'
$script:DotnetChannel = '10.0' # Windows companion (companion/): latest .NET 10 SDK patch

# Per-user .NET SDK (no admin): $env:DESKBUDDY_DOTNET_PATH, else %LOCALAPPDATA%\Microsoft\dotnet.
function Get-DotnetPath {
    if ($env:DESKBUDDY_DOTNET_PATH) { return $env:DESKBUDDY_DOTNET_PATH }
    return Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet'
}

function Enter-Dotnet {
    $root = Get-DotnetPath
    if (-not (Test-Path (Join-Path $root 'dotnet.exe'))) {
        throw ".NET SDK not found at $root. Run tools/setup-dotnet.ps1 (or set DESKBUDDY_DOTNET_PATH)."
    }
    $env:DOTNET_ROOT = $root
    $env:PATH = "$root;$env:PATH"
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
    $env:DOTNET_NOLOGO = '1'
}

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

# Runs commands (cmd syntax) in the MSVC x64 environment from Visual Studio (Build Tools with the "Desktop
# development with C++" workload, which includes the Windows SDK); used for the host tests and the simulator.
function Invoke-Msvc([string[]] $Steps) {
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

    # CMake and Ninja come with Visual Studio's "C++ CMake tools" component; without it, use ESP-IDF's (the same
    # tools the firmware builds with).
    if (-not (Test-Path (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'))) {
        $tools = if ($env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH } else { Join-Path $env:USERPROFILE '.espressif' }
        $cmake = Get-ChildItem (Join-Path $tools 'tools\cmake\*\bin\cmake.exe') -ErrorAction SilentlyContinue |
            Sort-Object FullName | Select-Object -Last 1
        $ninja = Get-ChildItem (Join-Path $tools 'tools\ninja\*\ninja.exe') -ErrorAction SilentlyContinue |
            Sort-Object FullName | Select-Object -Last 1
        if (-not $cmake -or -not $ninja) {
            throw 'CMake/Ninja not found: add "C++ CMake tools for Windows" to Visual Studio, or run tools/setup-idf.ps1.'
        }
        $env:PATH = "$($cmake.DirectoryName);$($ninja.DirectoryName);$env:PATH"
    }
    cmd /c "`"$devcmd`" -no_logo -arch=x64 -host_arch=x64 -winsdk=none && $($Steps -join ' && ')"
}
