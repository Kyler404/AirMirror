# Move the airplay-overlay window to a bright wallpaper area and
# screen-capture its region (post-DWM-composition, proves transparency).
param(
    [int]$X = 1450,
    [int]$Y = 250,
    [string]$Out = "shots\overlay_region.png"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class OV {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr a, int x, int y, int w, int h, uint f);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out R r);
    public struct R { public int L, T, Rt, B; }
}
"@
[OV]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$h = [OV]::FindWindowW($null, "airplay-overlay")
if ($h -eq [IntPtr]::Zero) { throw "overlay window not found" }
[OV]::SetWindowPos($h, [IntPtr]::Zero, $X, $Y, 0, 0, 0x0015) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object OV+R
[OV]::GetWindowRect($h, [ref]$r) | Out-Null
$wd = $r.Rt - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap($wd, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$g.Dispose()
$dir = "shots"
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host ("overlay rect {0},{1} {2}x{3} -> {4}" -f $r.L, $r.T, $wd, $ht, $Out)
