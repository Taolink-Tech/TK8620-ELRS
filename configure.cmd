@echo off
setlocal

set "CONFIG_EXE=%~dp0tools\config\tk8620_configurator.exe"
if exist "%CONFIG_EXE%" (
    "%CONFIG_EXE%"
    exit /b %ERRORLEVEL%
)

where py.exe >nul 2>nul
if errorlevel 1 (
    echo Configuration tool is missing: %CONFIG_EXE%
    echo Run tools\package-configurator.cmd to build it.
    exit /b 2
)

py.exe -3 "%~dp0tools\config\tk8620_configurator.py"
exit /b %ERRORLEVEL%
