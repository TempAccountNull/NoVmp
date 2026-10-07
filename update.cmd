@echo off
rem Reports upstream changes for every repo and checks the .vcxproj files against the disk.
rem Never overwrites our files. Pristine dependencies are fast-forwarded only with --apply.
rem   update.cmd [--no-fetch] [--apply] [--save-baseline] [--max-log N]
rem All paths are relative to this script's folder (%~dp0), so the tree can be moved.
setlocal EnableExtensions
set "ROOT=%~dp0"

where python >nul 2>&1
if errorlevel 1 goto :no_python

python "%ROOT%msvc\update.py" %*
exit /b %ERRORLEVEL%

:no_python
echo [update] python not found on PATH
exit /b 1
