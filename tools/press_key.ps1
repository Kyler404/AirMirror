# Send a keystroke to the airplay-windows window (no focus stealing issues).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/press_key.ps1 -Key "h"
param([string]$Key = "h")

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Key {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
}
"@

$proc = Get-Process airplay-windows -ErrorAction Stop
$h = $proc.MainWindowHandle
[Win32Key]::ShowWindow($h, 5) | Out-Null      # SW_SHOW
[Win32Key]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
[System.Windows.Forms.SendKeys]::SendWait($Key)
Write-Host "sent '$Key' to PID $($proc.Id)"
