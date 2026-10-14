# Builds and starts the Windows companion (companion/) with the per-user .NET SDK from tools/setup-dotnet.ps1.
#   tools/companion.ps1            # build (Release) and start it
#   tools/companion.ps1 -BuildOnly
param([switch] $BuildOnly)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Enter-Dotnet
$project = Join-Path $PSScriptRoot '..\companion\DeskBuddy.Companion\DeskBuddy.Companion.csproj'
dotnet build $project -c Release --nologo -v quiet
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $BuildOnly) {
    # DOTNET_ROOT (set by Enter-Dotnet) lets the app find the per-user .NET runtime.
    $exe = Join-Path $PSScriptRoot '..\companion\DeskBuddy.Companion\bin\Release\net10.0-windows\DeskBuddy.Companion.exe'
    Start-Process $exe
}
