# Builds and starts the Windows companion (companion/) with the per-user .NET SDK from tools/setup-dotnet.ps1.
#   tools/companion.ps1            # build (Release) and start it
#   tools/companion.ps1 -BuildOnly
#   tools/companion.ps1 -Test      # run the companion's unit tests
#   tools/companion.ps1 -Publish   # one self-contained DeskBuddy.Companion.exe in companion\publish: .NET is built in,
#                                  # so it starts by double-click on any 64-bit Windows PC, no runtime or settings needed
param([switch] $BuildOnly, [switch] $Test, [switch] $Publish)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Enter-Dotnet
$root = Join-Path $PSScriptRoot '..\companion'
$project = Join-Path $root 'DeskBuddy.Companion\DeskBuddy.Companion.csproj'
if ($Test) {
    dotnet test (Join-Path $root 'DeskBuddy.Companion.Tests\DeskBuddy.Companion.Tests.csproj') -c Release --nologo
    exit $LASTEXITCODE
}
if ($Publish) {
    $out = [IO.Path]::GetFullPath((Join-Path $root 'publish'))
    $exe = Join-Path $out 'DeskBuddy.Companion.exe'
    if (Get-Process DeskBuddy.Companion -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
        throw "DeskBuddy Companion is running from ${out}: close it first."
    }
    # Single file with the runtime inside, compressed; English-only framework resources keep it smaller.
    dotnet publish $project -c Release -r win-x64 --self-contained true -o $out --nologo -v quiet `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true `
        -p:DebugType=none -p:SatelliteResourceLanguages=en
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    Write-Host ("Published: {0} ({1:N0} MB)" -f $exe, ((Get-Item $exe).Length / 1MB))
    exit 0
}
dotnet build $project -c Release --nologo -v quiet
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $BuildOnly) {
    # DOTNET_ROOT (set by Enter-Dotnet) lets the app find the per-user .NET runtime.
    Start-Process (Join-Path $root 'DeskBuddy.Companion\bin\Release\net10.0-windows\DeskBuddy.Companion.exe')
}
