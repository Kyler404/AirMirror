# Capture the airplay-windows main window to a PNG.
# Uses PrintWindow first (works when occluded); falls back to
# topmost-raise + CopyFromScreen if the result is blank/black.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/screenshot.ps1 <out.png>
param([string]$Out = "shots\ui_current.png")

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Shot {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[Win32Shot]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null  # PER_MONITOR_AWARE_V2: physical-pixel rects

function Test-MostlyBlank([System.Drawing.Bitmap]$b) {
    # sample a grid of pixels; if nearly all are the same dark/black color, treat as blank
    $same = 0; $n = 0
    for ($x = 10; $x -lt $b.Width; $x += [Math]::Max(1, $b.Width / 24)) {
        for ($y = 10; $y -lt $b.Height; $y += [Math]::Max(1, $b.Height / 16)) {
            $c = $b.GetPixel([int]$x, [int]$y)
            if ($c.R -lt 8 -and $c.G -lt 8 -and $c.B -lt 8) { $same++ }
            $n++
        }
    }
    return ($n -gt 0 -and $same / $n -gt 0.95)
}

function Save-Capture([IntPtr]$h, [string]$path, [bool]$usePrintWindow) {
    $r = New-Object Win32Shot+RECT
    [Win32Shot]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { throw "bad window rect ${w}x${ht}" }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [Win32Shot]::PrintWindow($h, $hdc, 2)  # 2 = PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    if (-not $usePrintWindow) {
        [Win32Shot]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x3) | Out-Null  # topmost, nomove|nosize
        Start-Sleep -Milliseconds 400
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
        [Win32Shot]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x3) | Out-Null  # notopmost
    }
    $g.Dispose()
    if (-not (Test-MostlyBlank $bmp)) {
        $dir = Split-Path -Parent $path
        if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Host "saved $path (${w}x${ht})"
        $bmp.Dispose()
        return $true
    }
    $bmp.Dispose()
    return $false
}

$proc = Get-Process airplay-windows -ErrorAction Stop
$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw "no main window handle" }

if (Save-Capture $h $Out $true) { exit 0 }
Start-Sleep -Milliseconds 300
if (Save-Capture $h $Out $false) { exit 0 }
throw "capture came out blank both ways"
