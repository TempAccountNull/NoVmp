@echo off
rem Builds NoVmp.sln with Visual Studio 2022 (x64, static).
rem   build.cmd [--release ^| --debug]      default: --release
rem All paths are relative to this script's folder (%~dp0), so the tree can be moved.
setlocal EnableExtensions

set "ROOT=%~dp0"
set "CONFIG=Release"

:args
if "%~1"=="" goto :find_msbuild
if /I "%~1"=="--release" (set "CONFIG=Release" & shift & goto :args)
if /I "%~1"=="--debug"   (set "CONFIG=Debug"   & shift & goto :args)
echo [build] unknown option "%~1"
echo usage: build.cmd [--release ^| --debug]
exit /b 2

:find_msbuild
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build] vswhere.exe not found: "%VSWHERE%" - is Visual Studio 2022 installed?
    exit /b 1
)
rem vswhere runs at top level (not inside a ( ) block): %ProgramFiles(x86)% contains a ')'
set "MSBUILD="
set "VSOUT=%TEMP%\novmp_vswhere_%RANDOM%.txt"
"%VSWHERE%" -version "[17.0,18.0)" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\amd64\MSBuild.exe > "%VSOUT%" 2>nul
if exist "%VSOUT%" set /p MSBUILD=<"%VSOUT%"
if exist "%VSOUT%" del "%VSOUT%" >nul 2>&1
if not defined MSBUILD (
    echo [build] MSBuild for Visual Studio 2022 not found via vswhere.
    exit /b 1
)

if not exist "%ROOT%build" mkdir "%ROOT%build"
set "LOG=%ROOT%build\msbuild_%CONFIG%.log"

echo [build] configuration : %CONFIG% ^| x64
echo [build] solution      : %ROOT%NoVmp.sln
echo [build] msbuild       : %MSBUILD%
echo [build] log           : %LOG%

"%MSBUILD%" "%ROOT%NoVmp.sln" /m /nologo /v:minimal /p:Configuration=%CONFIG% /p:Platform=x64 /fl "/flp:LogFile=%LOG%;Verbosity=normal"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo [build] FAILED ^(msbuild exit code %RC%^) - see "%LOG%"
    exit /b %RC%
)

echo [build] OK: %ROOT%build\x64\%CONFIG%\NoVmp.exe
exit /b 0
