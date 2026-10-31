# 开发模式启动：控制台保留（能看异常日志），前端文件改动自动刷新窗口
# 用法：右键「使用 PowerShell 运行」，或 .\start-client-dev.ps1
#
# 改 python 后端（server.py / hid_transport.py）需要重启本脚本；
# 改 static/ 下的 html/css/js 会自动刷新，不用重启。

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
chcp 65001 > $null

$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { Write-Host "找不到 python" -ForegroundColor Red; exit 1 }

Write-Host "开发模式：WebView2 窗口 + 热重载（Ctrl+C 退出）" -ForegroundColor Cyan
& $python client.py --dev
