# -*- powershell -*-
# Post WM_CLOSE to a window by process name (no keyboard simulation).
param([string]$Name = "airplay-windows")

$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32Close {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
"@

$proc = Get-Process $Name -ErrorAction Stop
$h = $proc.MainWindowHandle
Write-Output ("PID {0}  hwnd 0x{1:X}" -f $proc.Id, $h.ToInt64())
[W32Close]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_CLOSE
Write-Output "WM_CLOSE posted"
