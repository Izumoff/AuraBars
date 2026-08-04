@echo off
setlocal

rem Usage: deploy.bat [x86|x64]
rem   Defaults to x64, since that's the architecture actually installed.
rem
rem Copies build\<arch>\AuraBars.dll into AIMP's live Plugins folder for
rem manual testing - closing AIMP first if it's running, and relaunching it
rem after. See AuraBars-Code-Contract.md Sec5.
rem
rem REQUIRES AN ELEVATED PROMPT: C:\Program Files\AIMP\Plugins is not
rem writable otherwise. Right-click this file and choose "Run as
rem administrator", or run it from an already-elevated cmd/PowerShell.
rem The underlying script checks for elevation explicitly and fails loudly
rem if it's missing, rather than silently reporting success.

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" -Arch %ARCH%
exit /b %errorlevel%
