# Instruction support — index

_Generated 2026-10-26 02:24 (HEAD `d191cb65 Ledger: U2050-U2061 (agent/p3sys, M5 part 2b - the --Sys loader to the CSR frontier, P39), merged; verify 418 of
419`) by `Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 926 rows; the 10 highest U numbers)

- **U2061** — Selftest nt_measured_loader (namespace, image directory, TLS, apphelp, hot patch, 32H, page-file section, the frontier stops) (file …
- **U2060** — The P39 frontier: the session's CSR shared section opens (measured), its view and NtConnectPort to \Sessions\1\Windows\ApiPort stop loudly with the …
- **U2059** — kernelbase's start as measured: NtSetInformationProcess 23H ProcessTlsInformation (replace vector / replace index), NtApphelpCacheControl 6 (no …
- **U2058** — The emulated file system's one directory (the image's = the CurrentDirectory): NtOpenFile (FILE_OPENED; every other path stops) and …
- **U2057** — The emulated object namespace: \KnownDlls (the measured 42 sections + KnownDllPath -> <SystemRoot>\System32), NtOpenDirectoryObject / …
- **U2056** — Selftest nt_measured_pool; suite win_env_sys_loader frontier B (files: Emulator/tests/selftest/st_main.cpp, Emulator/tools/win_env_sys_loader.py)
- **U2055** — NtTraceControl 1BH (the first ETW notification event succeeds - inferred from the measured STATUS_ALREADY_REGISTERED of a later one) / 0FH (the …
- **U2054** — The thread pool as measured: NtCreateWaitCompletionPacket, NtCreateIoCompletion, NtCreateWorkerFactory (no worker thread) …
- **U2053** — Selftest nt_measured_services (sat / qsi / qip answers and stops) + nt_kernel_sys_services updated (C0H, 37H, the cookie); suite win_env_sys_loader …
- **U2052** — Measured services of the process start: NtQuerySecurityAttributesToken (WIN://SYSAPPID / WIN://PKG -> STATUS_NOT_FOUND, ReturnLength 0) …
