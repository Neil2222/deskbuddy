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
