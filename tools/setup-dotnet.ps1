# One-time install of the .NET SDK for the Windows companion (companion/). Per user, no administrator rights.
#   tools/setup-dotnet.ps1                    # installs to %LOCALAPPDATA%\Microsoft\dotnet
#   $env:DESKBUDDY_DOTNET_PATH='D:\dotnet'; tools/setup-dotnet.ps1
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-DotnetPath
$installer = Join-Path $env:TEMP 'dotnet-install.ps1'
Invoke-WebRequest -UseBasicParsing https://dot.net/v1/dotnet-install.ps1 -OutFile $installer
& $installer -Channel $DotnetChannel -InstallDir $root -NoPath
Enter-Dotnet
dotnet --version
