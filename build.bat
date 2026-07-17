@echo off
setlocal

rem Usage: build.bat [x86|x64]
rem   No argument builds both architectures.
rem Each architecture builds in its own "cmd /c" subshell so vcvarsall.bat's
rem PATH/LIB/INCLUDE changes for one arch can never leak into the other.

set "TARGETS=%~1"
if "%TARGETS%"=="" set "TARGETS=x64 x86"

for %%A in (%TARGETS%) do (
    echo.
    echo === Building %%A ===
    cmd /c call "%~dp0build-one.bat" %%A
    if errorlevel 1 (
        echo Build failed for %%A
        exit /b 1
    )
)

echo.
echo All builds succeeded.
