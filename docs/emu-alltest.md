# emu-alltest — harness options and machine state

`build\x64\Release\tests\emu-alltest.exe` compares Unicorn against the host CPU (hardware cases) or against values derived from the Intel SDM (expected-value cases). Source: `Emulator\tests\alltest\at_main.cpp` (options), `at_cases.hpp` (case-file format), `at_engine.hpp` (how options reach Unicorn).

## Modes

| option | what it runs |
|---|---|
| *(none)* | quick sweep: every 7th instruction form, 2 iterations (test.cmd smoke run) |
| `--full [--iters N]` | every form, N iterations (default 6), report in `--out DIR` |
| `--filter S` / `--sample N` / `--rebuild` | restrict the sweep / sample / re-sweep the opcode space |
| `--cases FILE` | hand-written snippets with a chosen input state; format at the top of `at_cases.hpp` |
| `--expect-only` | with `--cases`: only the expected-value (`=>`) lines — they never run natively |
| `--bench [--reps N] [--filter S] [--scale F] [--bench-cpu C] [--csv FILE]` | performance benchmark (U500, `at_bench.hpp`): 13 workloads of our own code, Unicorn only — integer/branch, AVX/FMA, SSE integer, x87, REP strings, TLB misses, self-modifying code, 4096 short TBs, code/count/memory-hook overhead, short `uc_emu_start` calls, `uc_open` churn. Cold and warm median of N (default 5) fresh engines, Minsn/s from a calibrated instruction count, and an FNV-1a hash of the complete guest end state that must not change between builds. Pinned to logical CPU C (default 4) at high priority |
| `--profile N` | with `--bench` (per workload) or a quick/`--full` run (whole run): in-process sampling profiler, the N hottest functions self and inclusive (PDBs next to the exe; `[jit]` = TCG-generated code) |

## Machine-state options (apply to `--cases` runs)

These set **real architectural state** defined by the SDM — they are not test tricks. They exist because the snippets run in user mode, where the OS normally owns these registers.

| option | what it sets | why |
|---|---|---|
| `--cpuid FILE` | a CPUID profile (e.g. `Emulator\data\cpuid_i5-13600k.txt`) | Unicorn reports exactly that CPU's CPUID leaves. Since U435 a profile is **strict by default**: features the profile hides raise #UD. |
| `--strict` / `--no-strict` | `UC_CTL_X86_CPUID_STRICT` = 1 / 0 | explicit override of the U435 default; `--no-strict` lets hidden features execute (only for testing the profile mechanism). |
| `--xcr0 V` | XCR0 (written with `uc_reg_write`) for both kinds of case | XCR0 is the OS-set register (XSETBV) that enables x87/SSE/AVX/AVX-512/AMX state; it decides whether AVX/AVX-512/AMX instructions run or #UD. Not given (or 0) = automatic (U540, below). Needed only to test the XCR0 gating itself (a component *disabled*, e.g. AVX state off → VEX #UD). |
| `--cr0 V` | CR0 for both kinds of case | Not given (or 0) = automatic (U540, below). PG must stay clear (Unicorn memory is flat). |
| `--avx512` | `UC_CTL_X86_AVX512` = F\|DQ\|BW\|VL\|CD\|IFMA\|VPOPCNTDQ\|BITALG\|VBMI\|FP16 before the engine starts | AVX-512 is opt-in (off in MAX and in the i5-13600K profile, which has none). |
| `--amx` | `UC_CTL_X86_AMX` = all AMX parts | AMX is opt-in. |
| `--avx10 N` | `UC_CTL_X86_AVX10` = N (1 or 2) | AVX10 is opt-in; the AVX512* CPUID bits stay off unless `--avx512` is given too. |
| `cpl=3` (a case's input token, ledger U452) | Unicorn runs that case at CPL3: a Windows x64 GDT (10h code64 / 18h data DPL0, 23h code32 DPL3 = compatibility mode, 2Bh data DPL3, 33h code64 DPL3) on a separate page (`SYS_PAGE`, never compared), entered by IRETQ from a CPL0 stub with CS = 33h, SS = 2Bh, RFLAGS = 202h; IA32_EFER.SCE = 1 | The host always runs the snippets at CPL3 under Windows; without `cpl=3` Unicorn runs at CPL0 with no GDT, so privilege-dependent results (RDPMC, IRET, MOV DR, STI, SYSRET, LSS/MOV SS selectors) cannot be compared. A snippet may switch to compatibility mode (far transfer to 23h) and back to 33h; a fault taken in compatibility mode resumes at the 64-bit epilogue in both engines (native: the VEH sets the context's CS to 33h; Unicorn: a 32-bit gate `push 33h; push epilogue; retf`). |

### Automatic machine state (U540, plan 1.H.2)

Without `--xcr0` / `--cr0`, emu-alltest derives the state per kind of case and prints the effective values first (`machine state: hardware cases XCR0=… CR0=…` / `machine state: expected-value cases …`, read back from an engine configured like the run's):

| kind of case | XCR0 | CR0 | why |
|---|---|---|---|
| hardware (no `=>`, compared with this CPU) | the host's XCR0, `_xgetbv(0)` (only when CPUID.1:ECX.OSXSAVE = 1; else Unicorn's reset value) — 7 on the i5-13600K under Windows (x87\|SSE\|AVX; the profile's leaf 0Dh also offers PKRU, bit 9, which Windows leaves off) | 33h = PE\|MP\|ET\|NE | the comparison is against this machine, so Unicorn gets this machine's OS-owned state, not a typed number |
| expected-value (`=>`, vs the SDM) | Unicorn's reset value: every state component the CPU model, the opt-ins and the CPUID profile support (`unicorn\qemu\target\i386\cpu.c`, x86_cpu_reset, U120/U370) — e.g. 21Fh (MAX, no opt-in), 2FFh (`--avx512` or `--avx10`), 6021Fh (`--amx`), 207h (with the i5-13600K profile) | Unicorn's reset value 11h (PE\|ET) | the SDM model of an OS that enabled everything the CPU has |
| the quick / `--full` sweep | Unicorn's reset value (UC_CPU_X86_MAX, printed with the host's XCR0) | 11h | unchanged (the `--full` baseline) |

**Windows' CR0.** The host's full CR0 is 80050033h (PG\|AM\|WP\|NE\|ET\|MP\|PE), read natively with `smsw rax` (Windows does not enable UMIP on this machine; checked 2026-10-09). PG cannot be set (Unicorn's memory is a flat map without page tables); WP acts only on supervisor writes through paging; AM acts only with RFLAGS.AC = 1 at CPL3, and QEMU raises no alignment-check #AC (EXCP11_ALGN is never raised). So 33h is the part that acts: NE = 1 makes a pending unmasked x87 exception raise #MF (not FERR#) like the host — `cases_reach.txt` needs it (it ran with `--cr0 0x33` before); MP matters only with CR0.TS = 1 (never set). Every other hardware file gives exactly the same results with 33h as with the reset 11h (U540 check: all 11 hardware case files of test.cmd, case by case).

**What test.cmd passed before U540, and why it could go:** `--xcr0 7` on every hardware file (= the host's XGETBV value; without it the profile's reset XCR0 would be 207h), `--cr0 0x33` on cases_reach (= the new default), `--xcr0 0xE7` on the 17 AVX-512/AVX10 expected-value files and `--xcr0 0x60007` on cases_amx (the reset values 2FFh / 6021Fh additionally enable MPX and PKRU state, which no case uses). All 20 are removed; every file gives exactly the same cases and results (U540 check: old serial test.cmd output vs the new run, case by case).

**When `--xcr0` / `--cr0` are still needed:** to test the XCR0 gating itself (a component disabled) or another CR0 (e.g. NE = 0 → FERR#). They apply to both kinds of case.

## test.cmd: selective and parallel runs (U541–U543, plan 1.H.3)

```
test.cmd [--release | --debug] [-j N | --jobs N | --serial] [-v | --verbose] [list | NAME ...]
```

- **NAME** = a group or one suite id; several at once (`test.cmd evex x87`); no NAME (or `all`) = every suite, the full gate. `test.cmd list` prints the groups, the suites and their commands. Unknown names/options: message + usage, exit 2.
- **Groups:** `unit` (Unicorn unit tests + emu-uc72-risk), `sweep` (emu-alltest quick run), `selftest`, `expect` (every expected-value file), `evex`, `amx`, `fp16`, `avx10`, `ext` (Key Locker / SHA-SM / VNNI-IFMA), `sse`, `nan`, `x87`, `hw` (everything compared with the host CPU), `quirks`, `backport`, `tools` (check_decode_dups.py). A suite can be in several groups.
- **Parallel:** the suites are independent processes; up to N run at once (default NUMBER_OF_PROCESSORS, `--serial` = 1), longest first (the previous run's times, `logs\times.tsv`). Each writes its own log `build\x64\<config>\tests\logs\<suite>.log`; after the run each suite's key lines (command, machine state, summary, tags) are printed in suite order, `-v` prints whole logs.
- **Verdicts** are the old test.cmd rules: exit status 0; hardware files (`hw`): `differing: 0` + exit 0 + a strict CPUID profile; `expect_selftest_bad`: `cases: 6, differing: 6` + exit 1. A crash (negative exit status) now fails a suite (the old `if errorlevel 1` let it pass).
- **Summary table:** suite, result, cases, differing, known deviations, host state, tagged-not-observed, time; a total row for a split suite; wall clock vs the sum of the suite times. Then `[test] OK: all suites passed` (exit 0) or `[test] FAILED: N suite(s) failed` (exit 1), as before.
- **Run alone:** `hwcheck_gate1` and `cases_quirks` run first, each with nothing beside it: they contain the tagged REP LODS (67h, ECX = 0) case, whose CPU result depends on what runs on the other logical processors (docs\quirks.md "REP 67h ECX=0 zero-extension"; measured with 200 runs each: SDM path taken 8/200 serially with the old binary, 7/200 serially, 26/200 with 8 copies side by side). A tagged case never fails a run, but alone it keeps the same rate as the serial test.cmd.
- **Results do not depend on the parallel load:** hardware suites compare architectural results, never timing. Checked case by case (every verdict and every hw/uc/exp line) for every suite: the serial test.cmd before U540 vs the parallel runs — identical except the documented RDRAND values of loose cases, host-state tags (APIC ID, RDRAND) and the REP LODS tag above.
- **Tiers** (plan 1.H.3): while working on an area run only its group(s); a harness-only change: only the affected files (same cases, same counts); before a push that changes emulator code: the full `test.cmd` at 100%.

## Quirks

`--quirks` and the UC_CTL_X86_HW_QUIRKS control are being removed (plan 1.G, pure SDM). Hardware cases where the i5-13600K deviates from the SDM are tagged `# known deviation: <name>` (documented in `docs\quirks.md`); host-state differences (RDRAND values, APIC ID, …) are tagged `# host state: <reason>`. Both are counted separately so every file reports 0 unexplained differences.

## Output

`[n] SAME|DIFF <line>` per case, then `cases: N, differing: M`; with expected-value cases a second line `expected-value cases: N, differing: M, errors: E, skipped: S`. Exit status 1 when an expected-value case differs or cannot be run; hardware differences are reported (test.cmd's `:hw_zero` requires `differing: 0`).
