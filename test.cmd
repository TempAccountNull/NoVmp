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
rem plan 1.15a: expected-value ("SDM vector") cases - Unicorn only, compared to hand-derived SDM
rem results; any difference or unparsable line fails. The deliberately wrong file must be caught.
call :suite emu-alltest --cases "%ROOT%Emulator\data\expect_selftest.txt" --expect-only
call :expect_bad
rem plan 1.15d milestone K: VEX opmask instructions vs the SDM model (ref_opmask.py), Unicorn only
rem with the AVX-512 opt-in (the host has no AVX-512); every line is an expected-value case.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_opmask.txt" --avx512 --xcr0 0xE7
rem Intel AMX (ledger U170-U180) vs the SDM model (ref_amx.py), Unicorn only with the AMX opt-in
rem (the host has no AMX); tile programs store their results to memory.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_amx.txt" --amx --xcr0 0x60007
rem plan 1.15d milestone M1: EVEX instructions (moves, integer/logic, FP with {er}, compares
rem into k, broadcasts; masking, {1toN}, disp8*N, fault suppression, #UD) vs the SDM model
rem ref_evex_m1.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m1.txt" --avx512 --xcr0 0xE7
rem ledger U96/U97: NaN propagation (SDM Vol1 4.8.3.5 Table 4-8). EVEX VADD/VSUB/VMUL/VDIV/VMIN/
rem VMAX/VSQRT PS/PD with NaNs in both sources vs the SDM model (gen_cases_nan.py --evex).
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_nan_evex.txt" --avx512 --xcr0 0xE7 --expect-only
rem The same rules on the host CPU: legacy SSE, VEX AVX/FMA and x87 hardware cases (self-generated
rem snippets, gen_cases_nan.py --hw) must all match the i5-13600K.
call :hw_nan
rem plan 1.15d milestone M2 (ledger U250-U251): EVEX gathers/scatters (every form x VL, partial
rem completion at an unmapped element, overlapping scatter indices, E12 #UD) vs the SDM model
rem ref_evex_m2_gather.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m2_gather.txt" --avx512 --xcr0 0xE7 --expect-only
rem plan 1.15d milestone M2 engine (ledger U190-U201): EVEX scalar FP (SS/SD arithmetic, VMOVSS/SD,
rem (U)COMISS/SD), VCMPPS/PD/SS/SD into k, FMA (dest as source under masking), VPBLENDMx/VBLENDMPx
rem vs the SDM model ref_evex_m2_engine.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m2_engine.txt" --avx512 --xcr0 0xE7
rem EVEX milestone M2 permutes / moves (ledger U210-U215: unpack/shuffle/permute, VPMOVZX/SX and
rem narrowing VPMOV*, compress/expand, VMOVNT/DUP/MOVD/MOVQ/MOVHPS.., insert/extract/broadcast x2..x8)
rem vs the SDM model ref_evex_m2_perm.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m2_perm.txt" --avx512 --xcr0 0xE7
rem plan 1.15d milestone M3 BW (ledger U260-U269): AVX512BW byte/word instructions (64-bit
rem writemasks, masking / fault suppression per byte and word, narrowing stores, disp8*N of every
rem byte/word tuple, #UD) vs the SDM model ref_evex_m3_bw.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m3_bw.txt" --avx512 --xcr0 0xE7
rem plan 1.15d milestone M3 (AVX512DQ, ledger U290-U296): VPMULLQ, VANDPS..VXORPD, VFPCLASS,
rem VRANGE, VREDUCE vs the SDM model ref_evex_m3_dq.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m3_dq.txt" --avx512 --xcr0 0xE7
rem AVX512CD (ledger U320-U321): VPCONFLICTD/Q, VPLZCNTD/Q, VPBROADCASTMB2Q/MW2D vs the SDM
rem model ref_evex_m3_cd.py, Unicorn only with the AVX-512 opt-in (F|DQ|BW|VL|CD).
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m3_cd.txt" --avx512 --xcr0 0xE7
rem EVEX milestone M2 conversions / FP specials / shifts (ledger U230-U241): VCVT* (incl. the
rem AVX512DQ QQ forms), VRCP14/VRSQRT14, VGETEXP, VGETMANT, VSCALEF, VFIXUPIMM, VRNDSCALE,
rem VPSLL/VPSRL/VPSRA by xmm, VPROLV/VPRORV vs the SDM model ref_evex_m2_cvt.py, Unicorn only.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m2_cvt.txt" --avx512 --xcr0 0xE7
rem AVX512DQ forms that need the integrated engine (ledger U290-U296 with U192, U210/U215, U230/U233):
rem masked VRANGESS/SD, VREDUCESS/SD with DEST != SRC1, DQ insert/extract/broadcast, QQ conversions
rem vs the SDM model ref_evex_m3_dq.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m3_dq_post.txt" --avx512 --xcr0 0xE7 --expect-only

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
"%EXE%" %2 %3 %4 %5 %6 %7 %8 %9
if errorlevel 1 (
    echo [test] %~1 FAILED
    set /a FAILED+=1
) else (
    echo [test] %~1 passed
)
exit /b 0

:expect_bad
rem every line of expect_selftest_bad.txt has a wrong expectation: all 6 must be reported, exit 1
set "EXE=%TESTS%\emu-alltest.exe"
set "BADLOG=%TESTS%\expect_selftest_bad.log"
echo.
echo [test] ===== emu-alltest --cases expect_selftest_bad.txt --expect-only ^(must detect 6 of 6^)
if not exist "%EXE%" (
    echo [test] missing %EXE% - run build.cmd first
    set /a FAILED+=1
    exit /b 0
)
"%EXE%" --cases "%ROOT%Emulator\data\expect_selftest_bad.txt" --expect-only > "%BADLOG%"
set "RC=!errorlevel!"
type "%BADLOG%"
findstr /B /C:"cases: 6, differing: 6" "%BADLOG%" >nul
if errorlevel 1 (
    echo [test] expect_selftest_bad FAILED: wrong expectations not all detected
    set /a FAILED+=1
) else if not "!RC!"=="1" (
    echo [test] expect_selftest_bad FAILED: exit status !RC!, expected 1
    set /a FAILED+=1
) else (
    echo [test] expect_selftest_bad passed: every wrong expectation reported
)
exit /b 0

:hw_nan
rem hardware cases never fail emu-alltest itself: require "differing: 0" in its summary
set "EXE=%TESTS%\emu-alltest.exe"
set "NANLOG=%TESTS%\cases_nan.log"
echo.
echo [test] ===== emu-alltest --cases cases_nan.txt --strict --xcr0 7 ^(host CPU, must be 0 differing^)
if not exist "%EXE%" (
    echo [test] missing %EXE% - run build.cmd first
    set /a FAILED+=1
    exit /b 0
)
"%EXE%" --cases "%ROOT%Emulator\data\cases_nan.txt" --cpuid "%ROOT%Emulator\data\cpuid_i5-13600k.txt" --strict --xcr0 7 > "%NANLOG%"
set "RC=!errorlevel!"
findstr /R /B /C:"cases: [0-9]*, differing: 0$" "%NANLOG%"
if errorlevel 1 (
    findstr /B /C:"cases:" "%NANLOG%"
    echo [test] cases_nan FAILED: hardware and Unicorn differ, see %NANLOG%
    set /a FAILED+=1
) else if not "!RC!"=="0" (
    echo [test] cases_nan FAILED: exit status !RC!
    set /a FAILED+=1
) else (
    echo [test] cases_nan passed
)
exit /b 0

:no_tests
echo [test] no tests at "%TESTS%" - run build.cmd first
exit /b 1
