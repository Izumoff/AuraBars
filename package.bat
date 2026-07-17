@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "PKGDIR=%ROOT%package"
set "STAGE=%PKGDIR%\AuraBars"

echo === Running full build (x64 + x86) to ensure fresh DLLs ===
call "%ROOT%build.bat"
if errorlevel 1 exit /b 1

echo.
echo === Reading version from src\Version.h ===
set "VERSION="
for /f "tokens=3" %%A in ('findstr /C:"#define AURABARS_VERSION_STRING" "%ROOT%src\Version.h"') do set "VERSION_RAW=%%A"
if not defined VERSION_RAW (
    echo Could not find AURABARS_VERSION_STRING in src\Version.h
    exit /b 1
)
set "VERSION=%VERSION_RAW:"=%"
echo Version: %VERSION%

echo.
echo === Staging package layout ===
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
mkdir "%STAGE%\x64"
if errorlevel 1 exit /b 1

copy /Y "%BUILD%\x86\AuraBars.dll" "%STAGE%\AuraBars.dll" >nul
if errorlevel 1 exit /b 1

copy /Y "%BUILD%\x64\AuraBars.dll" "%STAGE%\x64\AuraBars.dll" >nul
if errorlevel 1 exit /b 1

if exist "%STAGE%\ReadMe.txt" del /f /q "%STAGE%\ReadMe.txt"
> "%STAGE%\ReadMe.txt" echo AuraBars
>> "%STAGE%\ReadMe.txt" echo Version: %VERSION%
>> "%STAGE%\ReadMe.txt" echo.
>> "%STAGE%\ReadMe.txt" echo LED/Smooth spectrum bar visualization plugin for AIMP.
>> "%STAGE%\ReadMe.txt" echo.
>> "%STAGE%\ReadMe.txt" echo Installation:
>> "%STAGE%\ReadMe.txt" echo   Copy the contents of this folder into your AIMP Plugins directory,
>> "%STAGE%\ReadMe.txt" echo   preserving the x64 subfolder. AIMP automatically loads the DLL
>> "%STAGE%\ReadMe.txt" echo   matching its own architecture.
>> "%STAGE%\ReadMe.txt" echo.
>> "%STAGE%\ReadMe.txt" echo Configure via AIMP Preferences, Plugins, AuraBars.
if errorlevel 1 exit /b 1

echo.
echo === Zipping package ===
if exist "%PKGDIR%\AuraBars.zip" del /f /q "%PKGDIR%\AuraBars.zip"
if exist "%PKGDIR%\AuraBars.aimppack" del /f /q "%PKGDIR%\AuraBars.aimppack"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGE%' -DestinationPath '%PKGDIR%\AuraBars.zip' -Force"
if errorlevel 1 exit /b 1

move /Y "%PKGDIR%\AuraBars.zip" "%PKGDIR%\AuraBars.aimppack" >nul
if errorlevel 1 exit /b 1

echo.
echo Package created: %PKGDIR%\AuraBars.aimppack
