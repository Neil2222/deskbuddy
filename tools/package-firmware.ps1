# Builds the DeskBuddy firmware from the committed source into a fresh directory and makes a firmware package of it
# (docs/firmware-packages.md). Nothing is flashed and no serial port is opened.
#
#   tools/package-firmware.ps1                                # unsigned package in dist\firmware
#   tools/package-firmware.ps1 -Key <private key PEM>         # signed (and read back with that key)
#   tools/package-firmware.ps1 -CheckReproducible             # builds twice in two directories, compares the images
#
# Refuses a working tree with uncommitted changes: a package is always one exact commit. The build uses its own
# sdkconfig, made fresh from sdkconfig.defaults, so local menuconfig changes never reach a package.
param(
    [string] $Key,
    [string] $OutDir = (Join-Path $PSScriptRoot '..\dist\firmware'),
    [string] $MinCompanion = '1.1.0', # the first Companion with the Firmware Manager
    [switch] $CheckReproducible
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project = Join-Path $root 'firmware\deskbuddy'

# The firmware records whether any tracked file had changes (firmware/build_commit.cmake), and a package must be one
# exact commit: the whole working tree must be clean, untracked files included.
function Assert-Clean([string] $When) {
    $changes = git -C $root status --porcelain
    if ($LASTEXITCODE) { throw 'git status failed' }
    if ($changes) { throw "Uncommitted or untracked changes $When; a package is built from one exact commit:`n$($changes -join "`n")" }
}

Assert-Clean 'before the build'
$commit = (git -C $root rev-parse --verify HEAD).Trim()
$version = (Get-Content (Join-Path $project 'version.txt') -TotalCount 1).Trim()
$linkHeader = Get-Content (Join-Path $root 'firmware\components\link\include\link.h') -Raw
if ($linkHeader -notmatch '#define LINK_PROTO (\d+)') { throw 'LINK_PROTO not found in link.h' }
$protocol = $Matches[1]
Write-Host "DeskBuddy $version, commit $commit, protocol $protocol"

# A fresh build directory and sdkconfig per build: nothing from an earlier or a local build is reused.
function Build-Firmware([string] $Name) {
    $work = Join-Path ([IO.Path]::GetTempPath()) "deskbuddy-fw-$($commit.Substring(0, 7))-$Name-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
    New-Item -ItemType Directory $work | Out-Null
    $build = Join-Path $work 'build'
    Write-Host "Building in $build"
    idf.py -C $project -B $build -D "SDKCONFIG=$(Join-Path $work 'sdkconfig')" build | Out-Host
    if ($LASTEXITCODE) { throw "firmware build failed ($LASTEXITCODE)" }
    return $build
}

Enter-Idf
$build = Build-Firmware 'a'
if ($CheckReproducible) {
    $second = Build-Firmware 'b'
    foreach ($file in 'bootloader\bootloader.bin', 'partition_table\partition-table.bin', 'deskbuddy.bin') {
        $a = (Get-FileHash -Algorithm SHA256 (Join-Path $build $file)).Hash
        $b = (Get-FileHash -Algorithm SHA256 (Join-Path $second $file)).Hash
        if ($a -ne $b) { throw "Not reproducible: $file differs between $build and $second" }
        Write-Host "reproducible: $file $($a.ToLower())"
    }
}
Assert-Clean 'after the build (the build changed tracked files)'

Enter-Dotnet
$toolProject = Join-Path $root 'companion\DeskBuddy.FirmwareTool\DeskBuddy.FirmwareTool.csproj'
dotnet build $toolProject -c Release --nologo -v quiet | Out-Host
if ($LASTEXITCODE) { throw 'building the packaging tool failed' }
$tool = Join-Path $root 'companion\DeskBuddy.FirmwareTool\bin\Release\net10.0\deskbuddy-firmware-tool.exe'

$packOutput = & $tool pack --build $build --commit $commit --protocol $protocol --min-companion $MinCompanion --out $OutDir
$packOutput | Out-Host
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$package = ($packOutput | Select-Object -First 1) -replace '^packed \(unsigned\): ', ''
if ($Key) {
    & $tool sign --package $package --key $Key
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
Write-Host ("Package: {0} (SHA-256 {1})" -f $package, (Get-FileHash -Algorithm SHA256 $package).Hash.ToLower())
