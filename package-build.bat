@echo off
setlocal

rem Stages and zips AuraBars.aimppack from already-built DLLs. Does NOT
rem build anything itself - called automatically by build.bat at the end of
rem a successful dual-architecture run, which is the only place that's
rem supposed to invoke this. Don't call it directly unless build\x86\ and
rem build\x64\ already hold fresh DLLs and src\BuildNumber.h is current;
rem nothing here regenerates either.
rem
rem Usage: package-build.bat [PREVBUILDNUM]
rem   PREVBUILDNUM (optional): the build number in effect *before* this
rem   run's BuildNumber.h was generated, so a same-clock-minute collision
rem   (Code Contract Sec6) can be caught. build.bat captures and passes
rem   this; a direct/manual invocation with no argument just skips that check.

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "PKGDIR=%ROOT%package"
set "STAGE=%PKGDIR%\AuraBars"
set "PREVBUILDNUM=%~1"

echo === Verifying build outputs ===
if not exist "%BUILD%\x86\AuraBars.dll" (
    echo.
    echo *** PACKAGING FAILED ***
    echo %BUILD%\x86\AuraBars.dll not found - run build.bat first.
    exit /b 1
)
if not exist "%BUILD%\x64\AuraBars.dll" (
    echo.
    echo *** PACKAGING FAILED ***
    echo %BUILD%\x64\AuraBars.dll not found - run build.bat first.
    exit /b 1
)
if not exist "%ROOT%ReadMe.txt" (
    echo.
    echo *** PACKAGING FAILED ***
    echo %ROOT%ReadMe.txt not found - this is the hand-maintained source
    echo copied into every package; it must exist and be tracked in git,
    echo never generated. Restore it before packaging.
    exit /b 1
)

echo.
echo === Reading version from src\Version.h ===
set "VERSION_RAW="
for /f "tokens=3" %%A in ('findstr /C:"#define AURABARS_VERSION_STRING" "%ROOT%src\Version.h"') do set "VERSION_RAW=%%A"
if not defined VERSION_RAW (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not find AURABARS_VERSION_STRING in src\Version.h
    exit /b 1
)
set "VERSION=%VERSION_RAW:"=%"

rem BuildNumber.h is read back here (not a fresh timestamp of our own) so
rem AuraBars.txt always names the exact build that's inside this package.
set "BUILDNUM_RAW="
for /f "tokens=3" %%A in ('findstr /C:"#define AURABARS_BUILD_NUMBER" "%ROOT%src\BuildNumber.h"') do set "BUILDNUM_RAW=%%A"
if not defined BUILDNUM_RAW (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not find AURABARS_BUILD_NUMBER in src\BuildNumber.h
    exit /b 1
)
set "BUILDNUM=%BUILDNUM_RAW:"=%"

rem Code Contract Sec6: "fail the build loudly if the computed <build>
rem equals the one recorded from the previous package - a packaging step
rem that silently reuses a number is worse than one that errors." <build>
rem is HHmm, so in practice this only fires when two packages are built
rem within the same clock-minute - the fix is to wait a minute and rebuild,
rem not to override this check.
if defined PREVBUILDNUM if "%BUILDNUM%"=="%PREVBUILDNUM%" (
    echo.
    echo *** PACKAGING FAILED ***
    echo Build number %BUILDNUM% is identical to the previous build's number.
    echo Two builds within the same clock-minute produce the same HHmm value.
    echo Wait at least a minute, then rebuild - do not override this check.
    exit /b 1
)

set "FULLVERSION=%VERSION%.%BUILDNUM%"
echo.
echo === Package version: %FULLVERSION% ===

rem Name/Author/Topic/Description are read from the same Version.h that the
rem About tab's C++ code reads, so the packaged metadata and the in-app
rem display can never drift apart. NAME and TOPIC are single-token values
rem (no embedded spaces), AUTHOR and DESCRIPTION are not, hence "tokens=3*"
rem (token 3 to end of line) for those two, preserving their internal spacing.
set "NAME_RAW="
for /f "tokens=3" %%A in ('findstr /C:"#define AURABARS_NAME_STRING" "%ROOT%src\Version.h"') do set "NAME_RAW=%%A"
set "NAME=%NAME_RAW:"=%"

set "AUTHOR_RAW="
for /f "tokens=3*" %%A in ('findstr /C:"#define AURABARS_AUTHOR_STRING" "%ROOT%src\Version.h"') do set "AUTHOR_RAW=%%A %%B"
set "AUTHOR=%AUTHOR_RAW:"=%"

set "TOPIC_RAW="
for /f "tokens=3" %%A in ('findstr /C:"#define AURABARS_TOPIC_STRING" "%ROOT%src\Version.h"') do set "TOPIC_RAW=%%A"
set "TOPIC=%TOPIC_RAW:"=%"

set "DESC_RAW="
for /f "tokens=3*" %%A in ('findstr /C:"#define AURABARS_DESCRIPTION_STRING" "%ROOT%src\Version.h"') do set "DESC_RAW=%%A %%B"
set "DESCRIPTION=%DESC_RAW:"=%"

echo.
echo === Staging package layout ===
rem Rebuilt from scratch every time now that every staged file has a real
rem source (the two DLLs, ReadMe.txt from the project root, AuraBars.txt
rem generated fresh below) - nothing here is meant to persist unmanaged
rem inside package\, unlike the old manual workflow.
if exist "%STAGE%" rd /s /q "%STAGE%"
if errorlevel 1 (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not clear %STAGE% - is a file inside it open elsewhere?
    exit /b 1
)
mkdir "%STAGE%"
mkdir "%STAGE%\x64"
if errorlevel 1 (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not create %STAGE%
    exit /b 1
)

copy /Y "%BUILD%\x86\AuraBars.dll" "%STAGE%\AuraBars.dll" >nul
if errorlevel 1 (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not copy the x86 DLL into the package.
    exit /b 1
)

copy /Y "%BUILD%\x64\AuraBars.dll" "%STAGE%\x64\AuraBars.dll" >nul
if errorlevel 1 (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not copy the x64 DLL into the package.
    exit /b 1
)

rem Hand-maintained, never generated or templated - copied verbatim from
rem the tracked project-root copy on every build, so the package always
rem carries whatever's currently checked in, and package\ (gitignored) can
rem be deleted and rebuilt from a clean checkout at any time.
copy /Y "%ROOT%ReadMe.txt" "%STAGE%\ReadMe.txt" >nul
if errorlevel 1 (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not copy ReadMe.txt into the package.
    exit /b 1
)

rem AuraBars.txt (unlike ReadMe.txt) is always regenerated fresh - it's
rem never hand-edited. Version line sourced from the same Version.h /
rem BuildNumber.h read above, so it can never drift from the About tab.
> "%STAGE%\AuraBars.txt" echo Name: %NAME%
>> "%STAGE%\AuraBars.txt" echo Version: %FULLVERSION%
>> "%STAGE%\AuraBars.txt" echo Author: %AUTHOR%
>> "%STAGE%\AuraBars.txt" echo Topic: %TOPIC%
>> "%STAGE%\AuraBars.txt" echo Description: %DESCRIPTION%
if errorlevel 1 (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not write %STAGE%\AuraBars.txt
    exit /b 1
)

echo.
echo === Zipping package ===
if exist "%PKGDIR%\AuraBars.zip" del /f /q "%PKGDIR%\AuraBars.zip"
if exist "%PKGDIR%\AuraBars.aimppack" del /f /q "%PKGDIR%\AuraBars.aimppack"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGE%' -DestinationPath '%PKGDIR%\AuraBars.zip' -Force"
if errorlevel 1 (
    echo.
    echo *** PACKAGING FAILED ***
    echo Compress-Archive failed.
    exit /b 1
)
if not exist "%PKGDIR%\AuraBars.zip" (
    echo.
    echo *** PACKAGING FAILED ***
    echo Compress-Archive reported success but %PKGDIR%\AuraBars.zip does not exist.
    exit /b 1
)

move /Y "%PKGDIR%\AuraBars.zip" "%PKGDIR%\AuraBars.aimppack" >nul
if errorlevel 1 (
    echo.
    echo *** PACKAGING FAILED ***
    echo Could not rename the zip to AuraBars.aimppack.
    exit /b 1
)
if not exist "%PKGDIR%\AuraBars.aimppack" (
    echo.
    echo *** PACKAGING FAILED ***
    echo AuraBars.aimppack does not exist after the rename - packaging did not complete.
    exit /b 1
)

echo.
echo Package created: %PKGDIR%\AuraBars.aimppack
echo === Package version: %FULLVERSION% ===
