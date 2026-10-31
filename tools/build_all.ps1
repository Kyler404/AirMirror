# -*- powershell -*-
# One-shot build pipeline: sync source tree -> msbuild -> deploy -> launch.
# Usage: powershell -File tools/build_all.ps1 [-Mode normal|demo]
param([string]$Mode = "normal")

$ErrorActionPreference = "Continue"
$root = "C:\Users\ABC\WorkBuddy\2026-10-29-19-00-28\mirror-launcher"
$tree = "C:\Users\ABC\.zcode\workspace\draft-061cf45f-6bb6-45e6-b074-9689fd7ea461\mirror-launcher\build\AirPlay-Windows"
$built = Join-Path $tree "build\out\Release\airplay-windows.exe"
$deployed = Join-Path $root "client\airplay-windows.exe"

Write-Output "== [1/5] stop running instance =="
Get-Process airplay-windows -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

Write-Output "== [2/5] sync source tree =="
robocopy (Join-Path $root "client-src\src") (Join-Path $tree "src") /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
Write-Output ("robocopy exit: " + $LASTEXITCODE)

Write-Output "== [3/5] msbuild =="
& "C:\Users\ABC\.workbuddy\binaries\python\versions\3.13.12\python.exe" (Join-Path $root "tools\build_msbuild_direct.py") 2>&1 |
    Select-String -Pattern "error C|error LNK|vcxproj ->|exit code" |
    ForEach-Object { $_.Line }

Write-Output "== [4/5] deploy =="
Copy-Item $built $deployed -Force
Write-Output ("deployed: " + (Get-Item $deployed).LastWriteTime)

Write-Output "== [5/5] launch ($Mode) =="
$argList = @()
if ($Mode -eq "demo") { $argList += "--demo" }
Start-Process -FilePath $deployed -ArgumentList $argList -WorkingDirectory (Split-Path $deployed)
Start-Sleep -Seconds 6
Get-Process airplay-windows -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Output ("running PID {0}" -f $_.Id) }
