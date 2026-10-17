# The Companion through UI Automation, for the P6 board test (docs/schedule-board-test.md).
param([string] $Invoke, [int] $Nth = 1, [string] $Texts, [switch] $Start, [switch] $Close, [switch] $Backup, [switch] $Hwnd)
[Console]::OutputEncoding = [Text.Encoding]::UTF8
if ($Backup) {
    $src = Join-Path $env:APPDATA 'DeskBuddy'
    $dst = Join-Path $env:USERPROFILE ('Documents\DeskBuddy-backups\appdata-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force $dst | Out-Null
    Copy-Item (Join-Path $src '*') $dst -Recurse
    "backup: $dst"
    Get-ChildItem $dst | ForEach-Object { '{0,-28} {1,8}' -f $_.Name, $_.Length }
    exit 0
}
if ($Close) {
    Get-Process DeskBuddy.Companion -ErrorAction SilentlyContinue | ForEach-Object { [void]$_.CloseMainWindow() }
    Start-Sleep 3
    "companions running: " + @(Get-Process DeskBuddy.Companion -ErrorAction SilentlyContinue).Count
    exit 0
}
if ($Start) {
    Start-Process (Join-Path $PSScriptRoot '..\..\companion\publish\DeskBuddy.Companion.exe')
    Start-Sleep 6
    "companions running: " + @(Get-Process DeskBuddy.Companion -ErrorAction SilentlyContinue).Count
    exit 0
}
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$p = Get-Process DeskBuddy.Companion -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { 'no window'; exit 1 }
$byPid = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
# The main window: the process also has a hidden window titled "DeskBuddy"; take the visible one, largest first.
$root = [Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children, $byPid) |
    Where-Object { $_.Current.Name -eq 'DeskBuddy' -and -not $_.Current.IsOffscreen } |
    Sort-Object { $_.Current.BoundingRectangle.Width * $_.Current.BoundingRectangle.Height } -Descending | Select-Object -First 1
if (-not $root) { 'no main window'; exit 1 }
if ($Hwnd) { $root.Current.NativeWindowHandle; exit 0 }
$all = $root.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)
if ($Invoke) {
    $seen = 0
    foreach ($x in $all) {
        if ($x.Current.Name -eq $Invoke -and $x.Current.IsEnabled) {
            $o = $null
            if ($x.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref] $o)) {
                $seen++
                if ($seen -eq $Nth) { $o.Invoke(); "invoked $Invoke ($Nth)"; exit 0 }
            }
        }
    }
    "missing $Invoke ($Nth)"; exit 1
}
if ($Texts) { foreach ($x in $all) { $n = $x.Current.Name; if ($n -match $Texts) { $n } } }
