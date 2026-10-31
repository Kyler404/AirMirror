@echo off
REM ============================================================
REM  Start the desktop client WITHOUT a console window.
REM  For debugging (console visible + hot reload): start-client-dev.ps1
REM ============================================================

REM NOTE: "%~dp0" ends with a backslash; inside quotes that backslash
REM escapes the closing quote and breaks the path. Use "%~dp0." instead.
cd /d "%~dp0."

set "PYW=%LOCALAPPDATA%\Python\pythoncore-3.14-64\pythonw.exe"
if not exist "%PYW%" set "PYW=pythonw.exe"

start "" "%PYW%" "%~dp0client.py"
