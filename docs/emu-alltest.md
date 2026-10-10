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
| `--shard K/N` | with `--cases`: only the K-th of N contiguous blocks of the case lines (U543); the N runs together are the whole file in order, the case numbers restart per block |
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
| `--apx` | `UC_CTL_X86_APX` = `UC_X86_APX_F` (ledger U610) | Intel APX is opt-in (the i5-13600K has none): CPUID.(7,1):EDX.APX_F, leaf 29H, XSAVE component 19 (R16-R31), REX2. The case keys `r16`..`r31` move R16-R31 like the ZMM/K state (Unicorn only). |
| `--seeded` (default) / `--rdrand-seed N` / `--HostSeed` | `UC_CTL_X86_RDRAND` (ledger U835, decision D8) = `UC_X86_RDRAND_SEEDED` with seed N (default 0) / `UC_X86_RDRAND_HOST`, for the case engines and the quick/`--full` sweep | RDRAND/RDSEED source. Seeded = a deterministic model (SplitMix64 of seed + n * 9E3779B97F4A7C15h, CF = 1), so runs repeat exactly; the seed and the draw count are part of a uc_context. HostSeed = the host CPU's RDRAND/RDSEED (value and CF as the host returns them). Either way a hardware case never sees the host's own draw, so `hwcheck_gate1`'s RDRAND/RDSEED cases stay tagged `# host state: RDRAND/RDSEED values`. The run prints `rdrand: seeded, seed 0x0 (--seeded, the default)` or `rdrand: host DRNG (--HostSeed)` after the machine-state lines. |
| `cpl=3` (a case's input token, ledger U452) | Unicorn runs that case at CPL3: a Windows x64 GDT (10h code64 / 18h data DPL0, 23h code32 DPL3 = compatibility mode, 2Bh data DPL3, 33h code64 DPL3) on a separate page (`SYS_PAGE`, never compared), entered by IRETQ from a CPL0 stub with CS = 33h, SS = 2Bh, RFLAGS = 202h; IA32_EFER.SCE = 1 | The host always runs the snippets at CPL3 under Windows; without `cpl=3` Unicorn runs at CPL0 with no GDT, so privilege-dependent results (RDPMC, IRET, MOV DR, STI, SYSRET, LSS/MOV SS selectors) cannot be compared. A snippet may switch to compatibility mode (far transfer to 23h) and back to 33h; a fault taken in compatibility mode resumes at the 64-bit epilogue in both engines (native: the VEH sets the context's CS to 33h; Unicorn: a 32-bit gate `push 33h; push epilogue; retf`). |

### Automatic machine state (U540, plan 1.H.2)

Without `--xcr0` / `--cr0`, emu-alltest derives the state per kind of case and prints the effective values first (`machine state: hardware cases XCR0=… CR0=…` / `machine state: expected-value cases …`, read back from an engine configured like the run's):

| kind of case | XCR0 | CR0 | why |
|---|---|---|---|
| hardware (no `=>`, compared with this CPU) | the host's XCR0, `_xgetbv(0)` (only when CPUID.1:ECX.OSXSAVE = 1; else Unicorn's reset value) — 7 on the i5-13600K under Windows (x87\|SSE\|AVX; the profile's leaf 0Dh also offers PKRU, bit 9, which Windows leaves off) | 40033h = PE\|MP\|ET\|NE\|AM (U834; 33h before) | the comparison is against this machine, so Unicorn gets this machine's OS-owned state, not a typed number |
| expected-value (`=>`, vs the SDM) | Unicorn's reset value: every state component the CPU model, the opt-ins and the CPUID profile support (`unicorn\qemu\target\i386\cpu.c`, x86_cpu_reset, U120/U370) — e.g. 21Fh (MAX, no opt-in), 2FFh (`--avx512` or `--avx10`), 6021Fh (`--amx`), 207h (with the i5-13600K profile) | Unicorn's reset value 11h (PE\|ET) | the SDM model of an OS that enabled everything the CPU has |
| the quick / `--full` sweep | Unicorn's reset value (UC_CPU_X86_MAX, printed with the host's XCR0) | 11h | unchanged (the `--full` baseline) |

**Windows' CR0.** The host's full CR0 is 80050033h (PG\|AM\|WP\|NE\|ET\|MP\|PE), read natively with `smsw rax` (Windows does not enable UMIP on this machine; checked 2026-10-09). PG cannot be set (Unicorn's memory is a flat map without page tables); WP acts only on supervisor writes through paging; AM acts only with RFLAGS.AC = 1 at CPL3: until U834 QEMU raised no alignment-check #AC (EXCP11_ALGN was never raised) and 33h was used; since U834 Unicorn implements #AC and hardware cases get 40033h, so a `cpl=3` case with `rflags=0x40202` faults on a misaligned access on both engines (`cases_ac_hw.txt`). Windows clears RFLAGS.AC while it dispatches a user-mode exception, so when both engines fault emu-alltest takes Unicorn's AC bit for the host too (the other flags and the vector are compared as before). So 33h (+ AM) is the part that acts: NE = 1 makes a pending unmasked x87 exception raise #MF (not FERR#) like the host — `cases_reach.txt` needs it (it ran with `--cr0 0x33` before); MP matters only with CR0.TS = 1 (never set). Every other hardware file gives exactly the same results with 33h as with the reset 11h (U540 check: all 11 hardware case files of test.cmd, case by case).

**What test.cmd passed before U540, and why it could go:** `--xcr0 7` on every hardware file (= the host's XGETBV value; without it the profile's reset XCR0 would be 207h), `--cr0 0x33` on cases_reach (= the new default), `--xcr0 0xE7` on the 17 AVX-512/AVX10 expected-value files and `--xcr0 0x60007` on cases_amx (the reset values 2FFh / 6021Fh additionally enable MPX and PKRU state, which no case uses). All 20 are removed; every file gives exactly the same cases and results (U540 check: old serial test.cmd output vs the new run, case by case).

**When `--xcr0` / `--cr0` are still needed:** to test the XCR0 gating itself (a component disabled) or another CR0 (e.g. NE = 0 → FERR#). They apply to both kinds of case.

## test.cmd: selective and parallel runs (U541–U543, plan 1.H.3)

```
test.cmd [--release | --debug] [-j N | --jobs N | --serial] [-v | --verbose] [list | NAME ...]
```

- **NAME** = a group or one suite id; several at once (`test.cmd evex x87`); no NAME (or `all`) = every suite, the full gate. `test.cmd list` prints the groups, the suites and their commands. Unknown names/options: message + usage, exit 2.
- **Groups:** `unit` (Unicorn unit tests + emu-uc72-risk), `sweep` (emu-alltest quick run), `selftest`, `expect` (every expected-value file), `evex`, `amx`, `fp16`, `avx10`, `ext` (Key Locker / SHA-SM / VNNI-IFMA), `sse`, `nan`, `x87`, `hw` (everything compared with the host CPU), `quirks`, `backport`, `tools` (check_decode_dups.py). A suite can be in several groups.
- **Parallel:** the suites are independent processes; up to N run at once (default NUMBER_OF_PROCESSORS, `--serial` = 1), longest first (the previous run's times, `logs\times.tsv`). Each writes its own log `build\x64\<config>\tests\logs\<suite>.log`; after the run each suite's key lines (command, machine state, summary, tags) are printed in suite order, `-v` prints whole logs.
- **Verdicts** are the old test.cmd rules: exit status 0; hardware files (`hw`): `differing: 0` + exit 0 + a strict CPUID profile; `expect_selftest_bad`: `cases: 7, differing: 7` + exit 1 (U772: 7 with the wrong error code). A crash (negative exit status) now fails a suite (the old `if errorlevel 1` let it pass).
- **Summary table:** suite, result, cases, differing, known deviations, host state, tagged-not-observed, time; a total row for a split suite; wall clock vs the sum of the suite times. Then `[test] OK: all suites passed` (exit 0) or `[test] FAILED: N suite(s) failed` (exit 1), as before.
- **Split suites:** `cases_sse_exc` runs as 8 shards (`emu-alltest --shard K/8`, contiguous blocks of its 174028 case lines; `test.cmd cases_sse_exc` selects all 8); `emu-uc72-risk` runs as three parts (`emu-uc72-risk --only 1-16`, `--only 17`, `--only 18`; R18 alone is the longest suite; `test.cmd emu-uc72-risk` selects all three). The summary adds a total row per split suite.
- **Run alone:** `hwcheck_gate1` and `cases_quirks` run first, each with nothing beside it: they contain the tagged REP LODS (67h, ECX = 0) case, whose CPU result depends on what runs on the other logical processors (docs\quirks.md "REP 67h ECX=0 zero-extension"; measured with 200 runs each: SDM path taken 8/200 serially with the old binary, 7/200 serially, 26/200 with 8 copies side by side). A tagged case never fails a run, but alone it keeps the same rate as the serial test.cmd.
- **Results do not depend on the parallel load:** hardware suites compare architectural results, never timing. Checked case by case (every verdict and every hw/uc/exp line) for every suite: the serial test.cmd before U540 vs the parallel runs — identical except the documented RDRAND values of loose cases, host-state tags (APIC ID, RDRAND) and the REP LODS tag above.
- **Tiers** (plan 1.H.3): while working on an area run only its group(s); a harness-only change: only the affected files (same cases, same counts); before a push that changes emulator code: the full `test.cmd` at 100%.

## Random input state (quick / `--full`, U931)

Each iteration of a sweep form starts from a random input state (`at_main.cpp` `rnd_state`): GPRs from a mix of small values, edge values (0, 1, 7Fh, 80h, FFFFh, 7FFFFFFFh, 8000000000000000h, ~0, ...) and full 64-bit values that never alias our regions; RSP / RSI / R14 / RDI the harness pointers; RCX bounded to 0–127; random CF PF AF ZF SF OF, DF one time in 8; MXCSR RC / FTZ|DAZ every 4th iteration; XMM/YMM lanes as integer bytes, floats or doubles (finite values, ±0, ±∞, NaN, denormals, raw bits); x87 registers as finite doubles converted to 80 bits or raw bytes; the 64 KiB operand memory and the test stack random, with 256 bytes of floats/doubles at MEM + 8000h and MEM + 9000h. The generator is xoshiro256** seeded with SplitMix64 from the form's encoding (FNV-1a 64) and the iteration number, so a form's inputs depend only on (form, iteration): `--filter`, `--sample` and the quick run reproduce the `--full` inputs of that form. Before U931 one `std::mt19937_64` (seed 5EED1234h) ran through the whole sweep, one call per random byte (35% of a `--full` run); the shaping rules are unchanged, the random values (and so the per-row CSV details) changed once with U931. The `--cases` files never use this generator (unset state is 0, see `at_cases.hpp`), and neither does `--bench`; RDRAND/RDSEED values come from the separate seeded model (U835, `--seeded`).

## Sweep buckets (quick / `--full`)

Every form of the sweep lands in exactly one bucket (`at_main.cpp` `k_bucket_name`; the CSV `bucket` column, the report's bucket table and per-group columns; `Emulator\tools\isa\gen_instruction_table.py` reads the exact strings):

| bucket | meaning |
|---|---|
| `match` | the host CPU and Unicorn give the same architectural result in every iteration |
| `differs` | at least one iteration differs (the CSV `detail` column has the first one) |
| `unicorn-#UD (hw runs it)` | the host runs it, Unicorn raises #UD in every iteration |
| `host lacks + unicorn #UD` | #UD on both in every iteration and the host CPU does not have the instruction (e.g. every EVEX form on the i5-13600K, AMD FMA4/XOP, AVX-512 opmask VEX forms) |
| `invalid encoding, #UD on both` | (U930) #UD on both in every iteration on an instruction the host CPU has: an encoding the SDM makes invalid (F3 0F 38 F0/F1, which Capstone names MOVBE; VEX VSIB gathers whose destination, index and mask overlap) or #UD by definition (UD0/UD1/UD2; VMREAD outside VMX operation). "The host has it": the host's runtime CPUID for the form's Capstone ISA group (an EVEX form needs AVX512F or AVX10; MOVBE, VMREAD/VMWRITE and UD0/1/2, which Capstone tags only `base`, by mnemonic), or another form of the same mnemonic and class ran natively in the same run (with `--filter` / `--sample` only the forms run count). Before U930 these were part of `host lacks + unicorn #UD` |
| `host lacks, unicorn runs (needs SDM check)` | the host #UDs, Unicorn runs it: checked against SDM vectors instead (verified_forms.tsv) |
| `not native-safe, unicorn runs (needs SDM check)` / `not native-safe, unicorn #UD` | the form cannot run natively (reason in `detail`); Unicorn's outcome |
| `privileged (CPL0; CPL3 check in Phase 3)` | a CPL0 instruction: Unicorn runs it at CPL0, never natively; the CPL3 fault (vs the host) is a Phase 3 item (D6). Was `privileged (CPL0 in raw unicorn; Phase 2 CPL3)` before U930 |
| `harness error` | the snippet could not be built or run (fails the run) |
| `known deviation (docs/quirks.md)` / `known deviation not observed (matches)` | a form listed in `Emulator\data\alltest_known_deviations.tsv` that differs / matches |

## Prepared native runs (U850)

The sweep runs a form on the host CPU only when nothing it does can leave our regions (CODE / DATA / MEM at 0x30000000) or touch host state. Most forms get there with the random state alone (memory operands on RSI / R14 = MEM + 8000h, RDI = MEM + 9000h). The rest get a prepared state on top of the random one (`at_universe.hpp` `prepare_native`, `pin_base`, `patch_moffs`; applied by `at_main.cpp` `apply_setup`, identically for both engines), and Unicorn runs them at CPL3 with the Windows GDT (`cpl=3`, as the host):

| forms | prepared |
|---|---|
| memory operand on another GPR base (the map-0F 38h/3Ah forms the sweep finds with the tail byte 5Bh as ModRM = `[rbx+0]`: AES*, SHA*, ADCX/ADOX, CRC32, MOVBE, MOVDIRI, MOVDIR64B, GF2P8*, PBLENDW, PEXTRD/Q, PMAXUW, PABSD, PHSUBD, WRSS) | the base register := MEM + 8000h (an index other than RCX := 0) |
| `MOV moffs` (A0–A3) | the 8-byte absolute address (0x5BC0 from the tail bytes, never mapped) is rewritten to MEM + 8000h; the form text shows the new address |
| Jcc, JRCXZ, LOOP/LOOPE/LOOPNE, JMP/CALL rel32, XBEGIN, INT3, INT1 | branch layout (`program::ctl`): the snippet sits between two 100h-byte pads, the rest of CODE is HLT (F4h). A taken branch stops at its target: CPL3 HLT is #GP(0) with RIP = the HLT, in both engines |
| JMP/CALL r64 / m64, RET [imm16], RETF/RETFQ [imm16], far JMP/CALL m16:xx | the register, the memory operand or the stack slot at RSP holds LAND = CODE + 8000h (HLT); far forms the selector 33h (the host's CS); RSP := STACK_TOP − 100h for the forms that pop. A 16-bit RETF / m16:16 target (8000h) is below 64 KiB, never mapped under Windows: #PF in both |
| PUSH, PUSHF, POP, POPF, ENTER, LEAVE | RSP := STACK_TOP − 100h for the forms that pop; POPF: TF cleared in the popped image (a trap in the epilogue), every other RFLAGS bit but RF compared; LEAVE: RBP := STACK_TOP − 100h; ENTER with a nesting level > 0: RBP inside the test stack; PUSH Sreg: outcome only (the selector is the environment's) |
| LSS | the selector after the offset := 2Bh (the host's own SS, as in `cases_fixes2.txt`) |

For a branch-layout form a fault anywhere but the prologue and epilogue belongs to the snippet (native: the vectored handler, only on the running thread while the snippet runs; Unicorn: the INTR / unmapped hooks), and the comparison adds the fault RIP (not for INT3 / INT1: UC_HOOK_INTR reports the next RIP, Windows the INT3) and the whole state at that fault. The test stack is compared from the lower of input and output RSP when nothing faulted (an RSP below the stack, ENTER 5BC0h, compares all of it).

Forms that stay out, with the reason in the CSV `detail` column: SYSCALL / SYSENTER (a real kernel entry), INT n (Windows' IDT decides natively; Unicorn delivers INT n to UC_HOOK_INTR without an IDT), WRFSBASE / WRGSBASE (the host thread's FS/GS base; GS base = the TEB), LFS / LGS (the host thread's FS/GS selector and base), XBEGIN when the host has RTM. WRUSSD/WRUSSQ are CPL0-only (SDM: #GP(0) at CPL > 0) and counted as privileged.

## Quirks

`--quirks` and the UC_CTL_X86_HW_QUIRKS control are being removed (plan 1.G, pure SDM). Hardware cases where the i5-13600K deviates from the SDM are tagged `# known deviation: <name>` (documented in `docs\quirks.md`); host-state differences (RDRAND values, APIC ID, …) are tagged `# host state: <reason>`. Both are counted separately so every file reports 0 unexplained differences.

## Paired hardware cases (U614)

A hardware case `<host asm> ~~ <unicorn asm> | <inputs>` runs the first snippet on the host CPU and the second on Unicorn and compares the two results like any hardware case. `Emulator\data\cases_apx_core_hw.txt` uses it to run each instruction's legacy encoding on the i5-13600K against its REX2 encoding on Unicorn (`--apx`, no CPUID profile, which would hide APX); test.cmd's `:hw_apx` requires `differing: 0`.

## Exception error codes (U772)

In an expected-value case the fault token may carry the error code in parentheses: `#GP(0)`, `#SS(0)`, `#GP(0x30)`, `#CP(2)`, `#PF(0x6)` (a C literal). Unicorn's error code is read in the `UC_HOOK_INTR` callback with `UC_CTL_X86_EXCEPTION` (U770, `uc_ctl_get_x86_exception`: vector, error code, #PF linear address) and must then equal it; the `uc:` line prints it as `fault #13(0x0)`. Without parentheses only the vector is compared, as before. Exceptions without an error code (INT n, #UD, #DE, ...) never match a token with one, and neither does a #PF that Unicorn reports for memory it has not mapped (a Unicorn memory error, not an x86 exception: only a #PF from the page walk with CR0.PG = 1 has an error code). Hardware cases compare the vector only: the Windows exception record does not carry the CPU's error code. Example (`Emulator\data\cases_fix4.txt`):

```
mov rax, qword ptr [rsp+rcx] | rcx=0x00007FFFCFFEC0FC => #SS(0)
```

## Native exception vectors (U773)

The native engine maps the Windows exception record to a vector (`vector_of`, `at_engine.hpp`). An access violation is #GP when its address (ExceptionInformation[1]) is -1, #SS when ExceptionInformation[0] = 3 (Windows reports a stack fault, e.g. a non-canonical `[rsp+rcx]`, with access type 3 and address 0), else #PF (access type 0 read, 1 write, 8 execute, and the address). Checked on this machine (Windows 10 19044, i5-13600K) with our own snippets; `cases_fix4.txt` has the hardware #SS / #GP / #PF lines. Before U773 every #SS was reported as #PF (14).

## Output

`[n] SAME|DIFF <line>` per case, then `cases: N, differing: M`; with expected-value cases a second line `expected-value cases: N, differing: M, errors: E, skipped: S`. Exit status 1 when an expected-value case differs or cannot be run; hardware differences are reported (test.cmd's `:hw_zero` requires `differing: 0`).
