# Click inside the airplay-windows window at window-relative physical coords.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/click.ps1 -X 60 -Y 167
param([int]$X = 0, [int]$Y = 0)

$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Click {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[Win32Click]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$proc = Get-Process airplay-windows -ErrorAction Stop
$h = $proc.MainWindowHandle
$r = New-Object Win32Click+RECT
[Win32Click]::GetWindowRect($h, [ref]$r) | Out-Null
[Win32Click]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500

[Win32Click]::SetCursorPos($r.Left + $X, $r.Top + $Y) | Out-Null
Start-Sleep -Milliseconds 120
[Win32Click]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)  # LEFTDOWN
Start-Sleep -Milliseconds 60
[Win32Click]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)  # LEFTUP
Write-Host "clicked +$X+$Y (screen $($r.Left + $X),$($r.Top + $Y))"
