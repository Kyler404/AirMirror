# Assemble a ready-to-send distribution package under dist\:
#   dist\AirPlay投屏\            <- folder to copy onto a target PC
#   dist\AirPlay投屏-分发包.zip  <- or just send this file
# Run build-client.ps1 first. Everything inside is path-relative, so the
# receiver only has to run the SmartScreen .bat once and start the app.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root "dist"
$pkg  = Join-Path $dist "AirPlay投屏"
$cli  = Join-Path $root "client"

if (-not (Test-Path (Join-Path $cli "airplay-windows.exe"))) {
    throw "client\airplay-windows.exe missing - run build-client.ps1 first"
}

Remove-Item $dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $pkg "client"), (Join-Path $pkg "tools") | Out-Null

Copy-Item (Join-Path $cli "*") (Join-Path $pkg "client") -Recurse -Force
Copy-Item (Join-Path $root "start-client.bat")   (Join-Path $pkg "AirPlay投屏.bat")
Copy-Item (Join-Path $root "tools\disable-smartscreen.ps1") (Join-Path $pkg "tools")

# .bat files stay ASCII-only inside (cmd parses them in the OEM code page);
# Chinese lives in the file names, which are NTFS Unicode and safe.
Set-Content -Path (Join-Path $pkg "关闭SmartScreen提示.bat") -Encoding Ascii -Value @'
@echo off
title AirPlay - SmartScreen setup
REM Turns off Explorer's "check apps and files" SmartScreen check.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\disable-smartscreen.ps1"
echo.
pause
'@

$readme = @'
AirPlay 投屏接收端 — 使用说明
================================

【首次安装】（每台电脑只需一次，弹 UAC 时点「是」）
  双击「关闭SmartScreen提示.bat」
  本程序没有商业证书签名，Windows 会反复弹蓝色警告框；这一步只关闭
  「检查应用和文件」提示，不影响系统其他安全防护。

  防火墙无需额外操作：首次启动时 Windows 会弹窗询问，点「允许访问」即可；
  如果错过了，直接在程序内黄色横幅里点「一键放行」。

【日常使用】
  - 双击「AirPlay投屏.bat」启动接收端。
  - iPhone 投屏：控制中心 → 屏幕镜像 → 选择「Windows 投屏」。
  - iPhone 音乐：音乐 App 播放页点 AirPlay 图标 → 选择本机。
  - 手机和电脑必须连同一个 Wi-Fi。

【常见问题】
  - 手机搜不到电脑：确认连的是同一个 Wi-Fi；确认防火墙已放行；
    接收端窗口要保持打开（可以按 H 隐藏界面，但别关窗口）。
  - 恢复系统的 SmartScreen 提示：以管理员身份运行
      powershell -NoProfile -ExecutionPolicy Bypass -File tools\disable-smartscreen.ps1 -Restore

【快捷键】F / F11 / 双击画面 = 全屏    H = 隐藏/显示界面
          R = 录制                     Esc = 退出全屏/恢复界面

【卸载】直接删除整个文件夹即可。
'@
Set-Content -Path (Join-Path $pkg "使用说明.txt") -Encoding UTF8 -Value $readme

Compress-Archive -Path $pkg -DestinationPath (Join-Path $dist "AirPlay投屏-分发包.zip") -Force

Write-Host "package ready:"
Get-ChildItem $dist | ForEach-Object { Write-Host ("  " + $_.FullName) }
$sum = Get-ChildItem $pkg -Recurse -File | Measure-Object Length -Sum
Write-Host ("  {0} files, {1} MB" -f $sum.Count, [math]::Round($sum.Sum / 1MB, 1))
