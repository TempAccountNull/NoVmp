# Instruction support — index

_Generated 2026-10-27 05:39 (HEAD `86bd118c Ledger: U2090-U2103 (agent/p3m6, P42 objects A / B / C - milestone 3.M6, the one virtual time base + RNG), merged
and verified`) by `Emulator/tools/isa/gen_status_docs.py`._

Status of every instruction form the NoVmp emulator knows (Intel SDM / XED list, AMD and VIA forms included), checked against an Intel i5-13600K or, where the
CPU cannot run a form, against the manual.

- [Instruction sets supported (Intel + AMD/VIA)](Intel_instruction_sets_supported.md) — every form once, split into usermode / kernel mode / needs review

## Legend

| Mark | Meaning |
|---|---|
| ✅ **Done** | Implemented and identical to your i5-13600K (hardware sweep / hardware cases); for a form the CPU cannot run: implemented per the manual and verified against SDM-pseudocode vectors (or the independent model named in "verified by"). |
| ⏳ **Implementing** | Implemented with an open item (named in "verified by"), or being implemented now (agent named). |
| ⬜ **Queued** | Not implemented yet (not started). |
| ❌ **Cannot run** | Your i5-13600K cannot run the form: CPUID bit clear, AMD/VIA-only, or disabled by Windows; the reason is in the **i5-13600K** column. Never compared with your hardware. The Status cell adds the emulator state: `❌ · ✅ per manual`, `❌ · ✅ gating only`, `❌ · ⏳ open item`, `❌ · ⬜ not implemented yet`. |
| **Per profile** `--guest` / `--host` | The i5-13600K column follows the VMware guest (`--guest` / `--vm`, where the hardware cases run); each reason says whether it also holds on the bare host (`--host`); forms the bare host enumerates say "--host: enumerated" (U1632, plan 1.J.57). |
| **Gating only** | Not enumerated on either profile; CPUID + #UD verified on both (`Emulator/data/cases_gating*.txt`; your decision 2026-10-17, G10). |
| **Usermode** / **Kernel mode** | Privilege class (`Emulator/data/isa_privilege.tsv`): kernel mode = the form cannot execute at CPL3 under Windows (CPL0, VMX / SEAM, IOPL, CR4.PCE) and needs the emulator driver; the reason is in the **Why kernel** column. |

## Totals

| privilege class | forms | ✅ | ⏳ | ⬜ | ❌ | ❌ split: per manual / gating only / open item / not implemented yet |
|---|---|---|---|---|---|---|
| Usermode (no emu driver required) | 2813 | 1241 | 14 | 0 | 1558 | 1316 / 0 / 35 / 207 |
| Kernel mode required | 93 | 13 | 35 | 0 | 45 | 20 / 6 / 11 / 8 |
| Needs review | 6 | 1 | 1 | 0 | 4 | 1 / 1 / 2 / 0 |
| **Total** | **2912** | **1255** | **50** | **0** | **1607** | 1337 / 7 / 48 / 215 |

Your i5-13600K runs 1305 forms and cannot run 1607 (❌; the reason is in the i5-13600K column of the page above).

## Currently being added

- (nothing in progress)

## Latest ledger entries (`CHANGES_LEDGER.md`, 968 rows; the 10 highest U numbers)

- **U2103** — docs/emu-alltest.md: the seeded CTR_DRBG and the -snip replay on the virtual time base
- **U2102** — tm_nt case time - the time sources of a process measured from inside (21 lines) + nt_measured CASES (files: Emulator/tests/pe/tm_nt/tm_nt.c …
- **U2101** — The seeded RDRAND expected values (cases_rdrand.txt / cases_rdrand_seed.txt), hwcheck_gate1's RDRAND host-state tags (Unicorn side), engine_rng …
- **U2100** — Unit tests vc_ (test_x86.c): the virtual counter across runs / advances / a count bound / a restart, invalid writes, RDMSR 10H + IA32_TSC_ADJUST, a …
- **U2099** — NtCreateWorkerFactory with StackReserve 800000H (the sample's SizeOfStackReserve) accepted as measured; tm_nt wkf.800 + its data line (files …
- **U2098** — no_host_passthrough: rule pt_host_clock (host time / sleep APIs, RDTSC intrinsics, C / C++ clocks; GetTickCount moved from pt_host_query) + 12 …
- **U2097** — Self-tests vclock_consistency (RED / GREEN), ctr_drbg_kat, nt_time_services; win_env_layout and nt_kernel_sys_services on the clock (file …
- **U2096** — emu-alltest: expected-value lines on the virtual time base (hardware lines and --full keep the host TSC), --snippet-export records config.tsc_start …
- **U2095** — win_env: KUSER_SHARED_DATA in an emulator-owned page; TickCountMultiplier 0FA00000H, QpcFrequency 10 MHz, TimeZoneBias 0 (UTC); InterruptTime / …
- **U2094** — The kernel's time services on the clock: NtQueryPerformanceCounter, NtQuerySystemTime, NtDelayExecution (relative / absolute / 0 = …
