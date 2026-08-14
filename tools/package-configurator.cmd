@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-configurator.ps1" %*
exit /b %ERRORLEVEL%
