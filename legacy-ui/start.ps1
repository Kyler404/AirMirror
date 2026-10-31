# 启动投屏启动台
# 用法：右键 → 使用 PowerShell 运行；或 .\start.ps1 -Port 9000

param(
    [int]$Port = 8770
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# 控制台切 UTF-8，否则中文日志在 GBK 终端里是乱码
chcp 65001 > $null

$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) {
    Write-Host "找不到 python，请先安装 Python 3.9+" -ForegroundColor Red
    exit 1
}

Write-Host "启动台 → http://127.0.0.1:$Port" -ForegroundColor Cyan
& $python server.py --port $Port
