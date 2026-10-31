# Send a virtual key to the airplay-windows window via PostMessage
# (works without focus, unlike keybd_event).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/key.ps1 -Vk 0x48
param([int]$Vk = 0x48)

$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Key {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint msg, UIntPtr wp, IntPtr lp);
}
"@
[Win32Key]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$proc = Get-Process airplay-windows -ErrorAction Stop
$h = $proc.MainWindowHandle
$scancode = 0x23  # KEY_H
$WM_KEYDOWN = 0x0100; $WM_KEYUP = 0x0101
[Win32Key]::PostMessage($h, $WM_KEYDOWN, [UIntPtr]::new([uint64]$Vk), [IntPtr](($scancode -shl 16) -bor 1)) | Out-Null
Start-Sleep -Milliseconds 60
[Win32Key]::PostMessage($h, $WM_KEYUP, [UIntPtr]::new([uint64]$Vk), [IntPtr](0xC0000000 -bor ($scancode -shl 16) -bor 1)) | Out-Null
Write-Host ("posted VK 0x{0:X2}" -f $Vk)
