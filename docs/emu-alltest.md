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
| `--xcr0 V` | XCR0 (written with `uc_reg_write`) | XCR0 is the OS-set register (XSETBV) that enables x87/SSE/AVX/AVX-512/AMX state; it decides whether AVX/AVX-512/AMX instructions run or #UD. 0 / not given = keep Unicorn's reset value (below). |
| `--cr0 V` | CR0 | `0x33` = PE\|MP\|ET\|NE as Windows x64 sets it, so pending unmasked x87 exceptions raise #MF (not FERR#). PG must stay clear (Unicorn memory is flat). |
| `--avx512` | `UC_CTL_X86_AVX512` = F\|DQ\|BW\|VL\|CD\|IFMA\|VPOPCNTDQ\|BITALG\|VBMI\|FP16 before the engine starts | AVX-512 is opt-in (off in MAX and in the i5-13600K profile, which has none). |
| `--amx` | `UC_CTL_X86_AMX` = all AMX parts | AMX is opt-in. |
| `--avx10 N` | `UC_CTL_X86_AVX10` = N (1 or 2) | AVX10 is opt-in; the AVX512* CPUID bits stay off unless `--avx512` is given too. |
| `cpl=3` (a case's input token, ledger U452) | Unicorn runs that case at CPL3: a Windows x64 GDT (10h code64 / 18h data DPL0, 23h code32 DPL3 = compatibility mode, 2Bh data DPL3, 33h code64 DPL3) on a separate page (`SYS_PAGE`, never compared), entered by IRETQ from a CPL0 stub with CS = 33h, SS = 2Bh, RFLAGS = 202h; IA32_EFER.SCE = 1 | The host always runs the snippets at CPL3 under Windows; without `cpl=3` Unicorn runs at CPL0 with no GDT, so privilege-dependent results (RDPMC, IRET, MOV DR, STI, SYSRET, LSS/MOV SS selectors) cannot be compared. A snippet may switch to compatibility mode (far transfer to 23h) and back to 33h; a fault taken in compatibility mode resumes at the 64-bit epilogue in both engines (native: the VEH sets the context's CS to 33h; Unicorn: a 32-bit gate `push 33h; push epilogue; retf`). |

### XCR0: reset value and what test.cmd passes

- **Reset value (no `--xcr0`):** Unicorn sets XCR0 at reset to every state component the CPU model and the installed CPUID profile support — like an OS that enabled all of them (`unicorn\qemu\target\i386\cpu.c`, x86_cpu_reset, U120/U370). So `--avx512` alone already gives XCR0 bits 7:5 (E7h), `--amx` adds 18:17, `--avx10` adds 7:5.
- **test.cmd today:** `--xcr0 7` on the hardware files (2×: the value Windows uses on the i5-13600K, which has no AVX-512), `--xcr0 0xE7` on the AVX-512/AVX10 expected-value files (17×, redundant with the reset value), `--xcr0 0x60007` for AMX (1×).
- **When `--xcr0` is genuinely needed:** to run with a component *disabled* (e.g. AVX state off → VEX instructions #UD), i.e. testing the XCR0 gating itself, or to force the host's exact value in a hardware comparison.

## Planned (harness 1.H): derive XCR0 / CR0 automatically

- Hardware runs: read the host's real XCR0 with `_xgetbv(0)` and use Windows' CR0 value by default — the comparison is against this exact machine, so the state should come from it, not from a typed number.
- Expected-value runs: keep the reset value (derived from the opt-ins) — drop the redundant `--xcr0 0xE7` arguments.
- `--xcr0` / `--cr0` stay as explicit overrides for gating tests.
- Checking a harness-only change: run just the affected case files and confirm the same cases give the same counts (the harness cannot change the emulator, only what is checked).

## Quirks

`--quirks` and the UC_CTL_X86_HW_QUIRKS control are being removed (plan 1.G, pure SDM). Hardware cases where the i5-13600K deviates from the SDM are tagged `# known deviation: <name>` (documented in `docs\quirks.md`); host-state differences (RDRAND values, APIC ID, …) are tagged `# host state: <reason>`. Both are counted separately so every file reports 0 unexplained differences.

## Output

`[n] SAME|DIFF <line>` per case, then `cases: N, differing: M`; with expected-value cases a second line `expected-value cases: N, differing: M, errors: E, skipped: S`. Exit status 1 when an expected-value case differs or cannot be run; hardware differences are reported (test.cmd's `:hw_zero` requires `differing: 0`).
