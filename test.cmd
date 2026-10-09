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
rem U539: the emulator implements the Intel SDM only (no quirk switch). Hardware files tag the cases
rem where the i5-13600K deviates from the SDM ("# known deviation: NAME", docs\quirks.md).
call :suite emu-alltest --cases "%ROOT%Emulator\data\expect_selftest.txt" --expect-only
call :expect_bad
rem plan 1.15d milestone K: VEX opmask instructions vs the SDM model (ref_opmask.py), Unicorn only
rem with the AVX-512 opt-in (the host has no AVX-512); every line is an expected-value case.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_opmask.txt" --avx512 --xcr0 0xE7 --expect-only
rem Intel AMX (ledger U170-U180) vs the SDM model (ref_amx.py), Unicorn only with the AMX opt-in
rem (the host has no AMX); tile programs store their results to memory.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_amx.txt" --amx --xcr0 0x60007 --expect-only
rem plan 1.15d milestone M1: EVEX instructions (moves, integer/logic, FP with {er}, compares
rem into k, broadcasts; masking, {1toN}, disp8*N, fault suppression, #UD) vs the SDM model
rem ref_evex_m1.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m1.txt" --avx512 --xcr0 0xE7 --expect-only
rem ledger U96/U97: NaN propagation (SDM Vol1 4.8.3.5 Table 4-8). EVEX VADD/VSUB/VMUL/VDIV/VMIN/
rem VMAX/VSQRT PS/PD with NaNs in both sources vs the SDM model (gen_cases_nan.py --evex).
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_nan_evex.txt" --avx512 --xcr0 0xE7 --expect-only
rem SDM-model expected-value lines of the Key Locker/RAO/UINTR, SHA512/SM3/SM4 and VNNI/IFMA files
rem (ledger U82-U105).
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_keylocker.txt" --expect-only
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_sha_sm.txt" --expect-only
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_vnni_ifma.txt" --expect-only
rem ledger U98: DPPS/DPPD with two or more NaN products vs the SDM pseudocode (gen_cases_nan.py --dp-sdm),
rem the first NaN product in the order p0..p3 lands in every selected element.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_dp_nan_sdm.txt" --expect-only
rem The same rules on the host CPU: legacy SSE, VEX AVX/FMA and x87 hardware cases (self-generated
rem snippets, gen_cases_nan.py --hw) must all match the i5-13600K.
call :hw_zero cases_nan
rem plan 1.15d milestone M2 (ledger U250-U251): EVEX gathers/scatters (every form x VL, partial
rem completion at an unmapped element, overlapping scatter indices, E12 #UD) vs the SDM model
rem ref_evex_m2_gather.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m2_gather.txt" --avx512 --xcr0 0xE7 --expect-only
rem plan 1.15d milestone M2 engine (ledger U190-U201): EVEX scalar FP (SS/SD arithmetic, VMOVSS/SD,
rem (U)COMISS/SD), VCMPPS/PD/SS/SD into k, FMA (dest as source under masking), VPBLENDMx/VBLENDMPx
rem vs the SDM model ref_evex_m2_engine.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m2_engine.txt" --avx512 --xcr0 0xE7 --expect-only
rem EVEX milestone M2 permutes / moves (ledger U210-U215: unpack/shuffle/permute, VPMOVZX/SX and
rem narrowing VPMOV*, compress/expand, VMOVNT/DUP/MOVD/MOVQ/MOVHPS.., insert/extract/broadcast x2..x8)
rem vs the SDM model ref_evex_m2_perm.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m2_perm.txt" --avx512 --xcr0 0xE7 --expect-only
rem plan 1.15d milestone M3 BW (ledger U260-U269): AVX512BW byte/word instructions (64-bit
rem writemasks, masking / fault suppression per byte and word, narrowing stores, disp8*N of every
rem byte/word tuple, #UD) vs the SDM model ref_evex_m3_bw.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m3_bw.txt" --avx512 --xcr0 0xE7 --expect-only
rem plan 1.15d milestone M3 (AVX512DQ, ledger U290-U296): VPMULLQ, VANDPS..VXORPD, VFPCLASS,
rem VRANGE, VREDUCE vs the SDM model ref_evex_m3_dq.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m3_dq.txt" --avx512 --xcr0 0xE7 --expect-only
rem AVX512CD (ledger U320-U321): VPCONFLICTD/Q, VPLZCNTD/Q, VPBROADCASTMB2Q/MW2D vs the SDM
rem model ref_evex_m3_cd.py, Unicorn only with the AVX-512 opt-in (F|DQ|BW|VL|CD).
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m3_cd.txt" --avx512 --xcr0 0xE7 --expect-only
rem EVEX milestone M2 conversions / FP specials / shifts (ledger U230-U241): VCVT* (incl. the
rem AVX512DQ QQ forms), VRCP14/VRSQRT14, VGETEXP, VGETMANT, VSCALEF, VFIXUPIMM, VRNDSCALE,
rem VPSLL/VPSRL/VPSRA by xmm, VPROLV/VPRORV vs the SDM model ref_evex_m2_cvt.py, Unicorn only.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m2_cvt.txt" --avx512 --xcr0 0xE7 --expect-only
rem AVX512DQ forms that need the integrated engine (ledger U290-U296 with U192, U210/U215, U230/U233):
rem masked VRANGESS/SD, VREDUCESS/SD with DEST != SRC1, DQ insert/extract/broadcast, QQ conversions
rem vs the SDM model ref_evex_m3_dq.py, Unicorn only with the AVX-512 opt-in.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m3_dq_post.txt" --avx512 --xcr0 0xE7 --expect-only
rem AVX512-FP16 (ledger U330-U339): EVEX maps 5/6 and the FP16 forms of map 3 vs the independent
rem SDM model ref_evex_fp16.py, Unicorn only with the AVX-512 opt-in (incl. UC_X86_AVX512_FP16).
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_fp16.txt" --avx512 --xcr0 0xE7 --expect-only
rem Intel AVX10 (ledger U370-U376): AVX10.2 alone (no AVX512* CPUID bits) runs the AVX-512 forms
rem and the AVX10.2 BF16 / MINMAX / VCOMX / saturating-conversion instructions vs the spec model
rem ref_avx10_a.py (AVX10.2 spec 361050-007), Unicorn only. AVX10.1 alone (U371) runs the M1 EVEX
rem and VEX opmask files at every vector length.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_avx10_a.txt" --avx10 2 --xcr0 0xE7 --expect-only
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_m1.txt" --avx10 1 --xcr0 0xE7 --expect-only
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_opmask.txt" --avx10 1 --xcr0 0xE7 --expect-only
rem AVX10.2 (avx10_b, ledger U400-U412): FP8 conversions, VCVT2PS2PHX, EVEX VNNI-INT8/INT16,
rem VDPPHPS, VMPSADBW, VMOVRS*, zero-extending VMOVD/VMOVW, EVEX SM4 vs the independent model
rem ref_avx10_b.py, Unicorn only with the AVX10.2 opt-in (UC_CTL_X86_AVX10 = 2).
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_avx10_b.txt" --avx10 2 --xcr0 0xE7 --expect-only
rem U445 x U147 / U236 / U373 / U404: MXCSR.UM / OM = 0 must not reach the forms that behave as if
rem every MXCSR exception were masked ({er} / {sae}, VRCP14, BF16, VCVT2PS2PHX); hand-derived cases.
call :suite emu-alltest --cases "%ROOT%Emulator\data\cases_evex_sae_unmasked.txt" --avx10 2 --xcr0 0xE7 --expect-only

rem ledger U440-U442: F16C VCVTPS2PH/VCVTPH2PS hardware cases (gen_cases_f16c.py): every rounding
rem source (imm8 / MXCSR.RC), FTZ/DAZ, denormal/tiny/overflow/NaN/inf, both VL, register and memory,
rem unmasked exceptions; must be 0 differing against the i5-13600K.
call :hw_zero cases_f16c
rem ledger U98/U535: DPPD with two NaN products on the host CPU (gen_cases_nan.py --dp-hw): the
rem emulator implements the SDM; the cases where the i5-13600K's element order gives another NaN are
rem tagged "# known deviation: DPPD two NaN products" (docs\quirks.md), all others must match.
call :hw_zero cases_dp_nan
rem ledger U434/U539: one or more hardware cases per documented i5-13600K deviation from the SDM
rem (cases_quirks.txt, docs\quirks.md): every case is tagged "# known deviation: NAME".
call :hw_zero cases_quirks
rem Decoder tables: no X86OpEntry table of decode-new.c.inc (with its included decode*.c.inc)
rem names an element twice ([0x42] = A, ..., [0x42] = B compiles silently, the later one wins)
rem in either build (__Use_Original_Qemu = 0 and = 1): Emulator\tools\check_decode_dups.py.
call :py_check "%ROOT%Emulator\tools\check_decode_dups.py" "%ROOT%unicorn\qemu\target\i386"

rem ledger U445-U447: SSE/AVX/FMA post-computation exceptions (gen_cases_sse_exc.py): ADD/SUB/MUL/DIV/
rem SQRT, HADD/HSUB/ADDSUB, DPPS/DPPD, all 60 FMA3 forms, CVT*, ROUND, RCP/RSQRT, MIN/MAX/CMP x {masked,
rem OM=0, UM=0, PM=0, DM=0, all unmasked} x RC x FTZ/DAZ: exact/inexact tiny, rounds-to-normal,
rem overflow per RC, exact overflow, denormal sources; must be 0 differing against the i5-13600K.
rem U538: the emulator uses the SDM DPPS step order; the cases where the i5-13600K's grouping gives
rem another outcome are tagged "# known deviation: DPPS exception step grouping" (dpps_steps.py).
call :hw_zero cases_sse_exc

echo.
if !FAILED! NEQ 0 (
    echo [test] FAILED: !FAILED! suite^(s^) failed
    exit /b 1
)
echo [test] OK: all suites passed
exit /b 0

:suite
rem %1 = test executable, up to 9 arguments after it (shifted below)
set "SUITE=%~1"
set "EXE=%TESTS%\%~1.exe"
echo.
echo [test] ===== %~1
if not exist "%EXE%" (
    echo [test] missing %EXE% - run build.cmd first
    set /a FAILED+=1
    exit /b 0
)
shift
"%EXE%" %1 %2 %3 %4 %5 %6 %7 %8 %9
if errorlevel 1 (
    echo [test] !SUITE! FAILED
    set /a FAILED+=1
) else (
    echo [test] !SUITE! passed
)
exit /b 0

:py_check
rem %1 = python script, %2 = its argument; python must be on PATH
echo.
echo [test] ===== python %~nx1
where python >nul 2>nul
if errorlevel 1 (
    echo [test] python not found - %~nx1 cannot run
    set /a FAILED+=1
    exit /b 0
)
python -I %1 %2
if errorlevel 1 (
    echo [test] %~nx1 FAILED
    set /a FAILED+=1
) else (
    echo [test] %~nx1 passed
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

:hw_zero
rem hardware cases never fail emu-alltest itself: require "differing: 0" in its summary (U530: cases
rem tagged "# known deviation: NAME" (docs\quirks.md) or "# host state: REASON" are counted apart).
rem %1 = case file name without .txt.
rem ledger U435: no --strict: a CPUID profile is strict by default (features it hides #UD);
rem the summary must say so.
set "EXE=%TESTS%\emu-alltest.exe"
set "NANLOG=%TESTS%\%~1.log"
echo.
echo [test] ===== emu-alltest --cases %~1.txt --cpuid i5-13600k --xcr0 7 ^(host CPU, must be 0 differing^)
if not exist "%EXE%" (
    echo [test] missing %EXE% - run build.cmd first
    set /a FAILED+=1
    exit /b 0
)
"%EXE%" --cases "%ROOT%Emulator\data\%~1.txt" --cpuid "%ROOT%Emulator\data\cpuid_i5-13600k.txt" --xcr0 7 > "%NANLOG%"
set "RC=!errorlevel!"
findstr /R /B /C:"cases: [0-9]*, differing: 0, known deviations: [0-9]*, host state: [0-9]*" "%NANLOG%"
if errorlevel 1 (
    findstr /B /C:"cases:" "%NANLOG%"
    echo [test] %~1 FAILED: hardware and Unicorn differ, see %NANLOG%
    set /a FAILED+=1
) else if not "!RC!"=="0" (
    echo [test] %~1 FAILED: exit status !RC!
    set /a FAILED+=1
) else (
    rem U530: tagged cases per docs\quirks.md deviation / host-state reason, and stale tags
    findstr /B /C:"known deviation: " /C:"host state: " /C:"tagged but not observed" /C:"  not observed " "%NANLOG%"
    findstr /B /C:"cpuid: profile" "%NANLOG%" | findstr /C:"strict on (default with a profile)" >nul
    if errorlevel 1 (
        echo [test] %~1 FAILED: the CPUID profile was not strict by default ^(U435^)
        set /a FAILED+=1
    ) else (
        echo [test] %~1 passed
    )
)
exit /b 0

:no_tests
echo [test] no tests at "%TESTS%" - run build.cmd first
exit /b 1
