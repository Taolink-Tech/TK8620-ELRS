@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\burn.ps1" %*
exit /b %ERRORLEVEL%
