param([string] $Shot)
# A PrintWindow capture of the Companion's visible main window (found through ui.ps1 -Hwnd: there is also a hidden
# window with the same title), so the user's screen is not touched.
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class S {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
$hwnd = & (Join-Path $PSScriptRoot 'ui.ps1') -Hwnd
if ($hwnd -notmatch '^\d+$' -or [int64]$hwnd -eq 0) { "shot failed: $hwnd"; exit 1 }
$h = [IntPtr][int64]$hwnd
$r = New-Object S+RECT
[void][S]::GetClientRect($h, [ref] $r)
if ($r.R -le $r.L -or $r.B -le $r.T) { 'shot failed: the window has no client area (minimised?)'; exit 1 }
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][S]::PrintWindow($h, $dc, 3)
$g.ReleaseHdc($dc)
$bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
"shot $Shot"
