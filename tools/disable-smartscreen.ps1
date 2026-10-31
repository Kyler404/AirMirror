# Toggle Explorer's "check apps and files" SmartScreen.
#   tools\disable-smartscreen.ps1           -> Off    (no more "Windows protected your PC"
#                                                      dialog on unsigned/unknown exes)
#   tools\disable-smartscreen.ps1 -Restore  -> Warn   (Windows default, prompts again)
# Scope: this affects the exe-launch SmartScreen check only (the blue
# "Windows protected your PC" dialog); Edge download protection and
# Defender antivirus are untouched. Asks for admin via UAC.
# Why it exists: SmartScreen blocks on CLOUD reputation, not local trust.
# A privately-distributed, self-signed exe can never earn reputation, so
# machines that want it prompt-free must turn the check off locally.
param([switch]$Restore)

$ErrorActionPreference = "Stop"
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $launchArgs = @("-NoProfile","-ExecutionPolicy","Bypass","-File",$PSCommandPath)
    if ($Restore) { $launchArgs += "-Restore" }
    Start-Process powershell -Verb RunAs -ArgumentList $launchArgs -Wait
    exit
}

$key   = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer"
$state = if ($Restore) { "Warn" } else { "Off" }
New-ItemProperty -Path $key -Name "SmartScreenEnabled" -Value $state -PropertyType String -Force | Out-Null
Write-Host ("SmartScreenEnabled = " + $state)
if (-not $Restore) {
    Write-Host "restore the Windows default later:  tools\disable-smartscreen.ps1 -Restore"
}
