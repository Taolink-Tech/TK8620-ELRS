@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-flasher.ps1" %*
exit /b %ERRORLEVEL%
