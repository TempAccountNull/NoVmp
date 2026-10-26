# Instruction support — index

_Generated 2026-10-26 11:34 (HEAD `398d1a64 Ledger: U2068-U2089 (agent/p3con, P41 objects A + B - the model of a process started without a console, the console
capture), merged and verified`) by `Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 954 rows; the 10 highest U numbers)

- **U2089** — T5 in the suite and the measurement: win_env_sys_loader runs T5 under --Sys --loader (211 services, exit 2FH) and --HLE, checks its two output …
- **U2088** — no_host_passthrough exact exemptions: nt_kernel_sys.cpp pt_live_system_dir x1 (the measured user-hive key name compared with a guest name) …
- **U2087** — Selftest nt_measured_console (the detached parameters, 31H, NtWriteFile 0, ETW 4 / 5 / 1EH, the token, NtAccessCheck, BaseNamedObjects, the mutant …
- **U2086** — Suite win_env_sys_loader GREEN: T2 / T3 run to their end under --Sys --loader (210 / 202 answered services in the recorded order, exit 2BH / 2EH), = …
- **U2085** — Test PE T5 (t5_stdout, self-written guest code): GetStdHandle( STD_OUTPUT_HANDLE ), WriteFile and WriteConsoleW of a known text, GetLastError; exit …
- **U2084** — The CONSOLE CAPTURE: every console / standard-handle output attempt of the emulated process recorded (observation only) - --Sys: entry hooks on the …
- **U2083** — --HLE APIs of a process without a console: GetStdHandle (the parameters' standard handles: 0), WriteFile( 0 ) (written 0, the kernel layer's …
- **U2082** — NtTerminateProcess( NULL, status ): STATUS_SUCCESS, the caller continues, the process' exit status fixed by this first call (the final …
- **U2081** — NtTestAlert: STATUS_SUCCESS (no APC queued, not alerted - one thread, no APC service) (file: Emulator/src/nt_kernel_measured.cpp)
- **U2080** — The loader's second worker factory: NtSetInformationWorkerFactory rows 13 = 1, 2 = FFFFFFFFEE1E5D00H (8 bytes), 5 = 3; NtApphelpCacheControl …
