@echo off
setlocal

rem Packaging is no longer a separate opt-in step - build.bat now packages
rem automatically at the end of every dual-architecture run (see
rem package-build.bat, which does the actual staging/zipping). This file
rem is kept only as a familiar/documented entry point that does the exact
rem same thing plain "build.bat" does; there is no packaging-only logic
rem left here to duplicate or drift from build.bat's.

call "%~dp0build.bat"
exit /b %errorlevel%
