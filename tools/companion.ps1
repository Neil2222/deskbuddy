# Builds and starts the Windows companion (companion/) with the per-user .NET SDK from tools/setup-dotnet.ps1.
#   tools/companion.ps1            # build (Release) and start it
#   tools/companion.ps1 -BuildOnly
#   tools/companion.ps1 -Test      # run the companion's unit tests
param([switch] $BuildOnly, [switch] $Test)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Enter-Dotnet
$root = Join-Path $PSScriptRoot '..\companion'
if ($Test) {
    dotnet test (Join-Path $root 'DeskBuddy.Companion.Tests\DeskBuddy.Companion.Tests.csproj') -c Release --nologo
    exit $LASTEXITCODE
}
dotnet build (Join-Path $root 'DeskBuddy.Companion\DeskBuddy.Companion.csproj') -c Release --nologo -v quiet
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $BuildOnly) {
    # DOTNET_ROOT (set by Enter-Dotnet) lets the app find the per-user .NET runtime.
    Start-Process (Join-Path $root 'DeskBuddy.Companion\bin\Release\net10.0-windows\DeskBuddy.Companion.exe')
}
