@echo off
rem Runs the test suites built by build.cmd and reports pass/fail per suite.
rem   test.cmd [--release ^| --debug] [-j N ^| --jobs N ^| --serial] [-v ^| --verbose] [list ^| NAME ...]
rem   NAME     a suite or a group: unit, sweep, selftest, expect, evex, amx, fp16, avx10, ext, sse, nan,
rem            x87, hw, quirks, backport, tools, or one suite id ("test.cmd list" prints them all);
rem            several names at once; no NAME (or "all") = every suite, the full gate.
rem   -j N     run up to N suites at the same time (default NUMBER_OF_PROCESSORS); --serial = -j 1.
rem   -v       also print each suite's whole log.
rem Plan 1.H.3 (ledger U541-U543): the suites are independent processes and run in parallel, each into
rem its own log build\x64\<config>\tests\logs\<suite>.log, judged by the same rules as before; then a
rem summary table (suite, result, cases, differing, known deviations, host state, time) and the
rem final "[test] OK: all suites passed" (exit 0) or "[test] FAILED: N suite(s) failed" (exit 1).
rem The suite table, the comments per suite and the verdict rules: Emulator\tools\test_runner.ps1.
rem All paths are relative to this script's folder (%~dp0), so the tree can be moved.
rem Only self-contained test code runs here (Unicorn's own unit tests emulate their own snippets;
rem emu-alltest runs self-generated snippets natively); nothing from the sample is ever executed.
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Emulator\tools\test_runner.ps1" "%~dp0." %*
exit /b %errorlevel%
