@echo off
rem Runs every test executable built by build.cmd and reports pass/fail per suite.
rem   test.cmd [--release ^| --debug]      default: --release
rem All paths are relative to this script's folder (%~dp0), so the tree can be moved.
rem Only self-contained test code runs here (Unicorn's own unit tests emulate their own
rem snippets); nothing from the sample is ever executed.
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "CONFIG=Release"

:args
if "%~1"=="" goto :run
if /I "%~1"=="--release" (set "CONFIG=Release" & shift & goto :args)
if /I "%~1"=="--debug"   (set "CONFIG=Debug"   & shift & goto :args)
echo [test] unknown option "%~1"
echo usage: test.cmd [--release ^| --debug]
exit /b 2

:run
set "TESTS=%ROOT%build\x64\%CONFIG%\tests"
if not exist "%TESTS%\" goto :no_tests
set /a FAILED=0
echo [test] configuration : %CONFIG% ^| x64
echo [test] tests         : %TESTS%

call :suite unicorn-test_x86
call :suite unicorn-test_ctl
rem test_mem_read_and_write_large_memory_block opens UC_ARCH_ARM64; our unicorn.lib is built
rem for the x86_64 target only, so that one upstream test cannot apply (skipped by name).
echo [test] skip unicorn-test_mem:test_mem_read_and_write_large_memory_block (needs UC_ARCH_ARM64; x86_64-only build)
call :suite unicorn-test_mem --skip test_mem_read_and_write_large_memory_block
rem plan 1.5: QEMU 7.2 branch risks (long AVX2/FMA/VSIB blocks vs hardware, uc_context round
rem trip, old_exception / spurious #DF). Hardware reference runs self-generated code only.
call :suite emu-uc72-risk
rem plan 1.9: emu-alltest quick run (every 7th form of the full x86-64 universe, host CPU vs Unicorn
rem UC_CPU_X86_MAX). Differences are the Phase 4/5 work list, reported in build\x64\Release\tests\alltest;
rem only harness errors fail. Full run: emu-alltest --full (baseline in Emulator\data\alltest_baseline).
call :suite emu-alltest

echo.
if !FAILED! NEQ 0 (
    echo [test] FAILED: !FAILED! suite^(s^) failed
    exit /b 1
)
echo [test] OK: all suites passed
exit /b 0

:suite
set "EXE=%TESTS%\%~1.exe"
echo.
echo [test] ===== %~1
if not exist "%EXE%" (
    echo [test] missing %EXE% - run build.cmd first
    set /a FAILED+=1
    exit /b 0
)
"%EXE%" %2 %3 %4 %5 %6
if errorlevel 1 (
    echo [test] %~1 FAILED
    set /a FAILED+=1
) else (
    echo [test] %~1 passed
)
exit /b 0

:no_tests
echo [test] no tests at "%TESTS%" - run build.cmd first
exit /b 1
