param([string] $Keys = 'esc')
# Keys to the Companion's top window without a title (an open drop-down menu): esc, down, enter.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class K { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); }
'@
$h = (Get-Process DeskBuddy.Companion | Select-Object -First 1).MainWindowHandle
$codes = @{ esc = 0x1B; down = 0x28; enter = 0x0D }
foreach ($k in $Keys.Split(',')) {
    [void][K]::PostMessage($h, 0x100, [IntPtr]$codes[$k], [IntPtr]0)
    [void][K]::PostMessage($h, 0x101, [IntPtr]$codes[$k], [IntPtr]0)
    Start-Sleep -Milliseconds 150
}
"keys $Keys"
