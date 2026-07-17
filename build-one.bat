@echo off
setlocal

set "ARCH=%~1"
if /I "%ARCH%"=="x64" (
    set "MACHINE=X64"
) else if /I "%ARCH%"=="x86" (
    set "MACHINE=X86"
) else (
    echo Usage: build-one.bat ^<x86^|x64^>
    exit /b 1
)

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "SDKINC=A:\_Data\Projects\AuraBars\aimp-sdk\Sources\Cpp"
set "SRC=%~dp0src"
set "OUT=%~dp0build\%ARCH%"

if not exist "%OUT%" mkdir "%OUT%"

call "%VCVARS%" %ARCH%
if errorlevel 1 exit /b 1

rc /nologo /fo "%OUT%\AuraBars.res" /i "%SRC%" "%SRC%\AuraBars.rc"
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /EHsc /W3 /O2 /MD /DUNICODE /D_UNICODE ^
   /I "%SDKINC%" /I "%SRC%" ^
   /Fo"%OUT%\\" ^
   /c ^
   "%SRC%\Common.cpp" "%SRC%\Settings.cpp" "%SRC%\Visualization.cpp" "%SRC%\OptionsFrame.cpp" "%SRC%\Plugin.cpp" "%SRC%\DllMain.cpp"
if errorlevel 1 exit /b 1

link /nologo /DLL /MACHINE:%MACHINE% /DEF:"%SRC%\AuraBars.def" /OUT:"%OUT%\AuraBars_%ARCH%.dll" ^
   "%OUT%\Common.obj" "%OUT%\Settings.obj" "%OUT%\Visualization.obj" "%OUT%\OptionsFrame.obj" "%OUT%\Plugin.obj" "%OUT%\DllMain.obj" "%OUT%\AuraBars.res" ^
   user32.lib gdi32.lib comdlg32.lib comctl32.lib msimg32.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1

echo Build succeeded: %OUT%\AuraBars_%ARCH%.dll
exit /b 0
