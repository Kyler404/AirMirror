# Move the cursor over the airplay-windows window (no click) so hover
# animations can be observed in a follow-up screenshot.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/hover.ps1 -X 400 -Y 300
param([int]$X = 400, [int]$Y = 300)

$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Hover {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[Win32Hover]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$proc = Get-Process airplay-windows -ErrorAction Stop
$h = $proc.MainWindowHandle
$r = New-Object Win32Hover+RECT
[Win32Hover]::GetWindowRect($h, [ref]$r) | Out-Null
[Win32Hover]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
[Win32Hover]::SetCursorPos($r.Left + $X, $r.Top + $Y) | Out-Null
Write-Host "cursor at +$X+$Y"
