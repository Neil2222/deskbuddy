param([string] $Shot)
# A PrintWindow capture of the Companion's main window (found by its title), so the user's screen is not touched.
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class S {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
$h = [S]::FindWindow($null, 'DeskBuddy')
$r = New-Object S+RECT
[void][S]::GetClientRect($h, [ref] $r)
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][S]::PrintWindow($h, $dc, 3)
$g.ReleaseHdc($dc)
$bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
"shot $Shot"
