# Instruction support — index

_Generated 2026-10-30 08:21 (HEAD `5cbdcfac Ledger: U2372 (the u32.psi shape rule) and U2360-U2371 (NtQueryInformationThread 0, NtSetInformationThread 4 / 11H
measured and modelled), merged and verified (P57)`) by `Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 1089 rows; the 10 highest U numbers)

- **U2372** — nt_measured: SERVERINFO +0 bit 40H of case u32 (tm_nt u32.psi) compared by SHAPE - VOLATILE[2]: the bit = "the last input event of the session was a …
- **U2371** — emu-selftest nt_measured_nqit + the ThreadHideFromDebugger items (Length 1 / 4 mismatch, the sample's NULL / 0, a buffer with 0, another handle …
- **U2370** — NtSetInformationThread class 11H ThreadHideFromDebugger in the kernel layer (nt_kernel.cpp svc_set_thread_hide; phnt ntpsapi.h / THREADINFOCLASS …
- **U2369** — tm_nt case nhfd (appended after nsit): NtSetInformationThread( NtCurrentThread(), 11H ThreadHideFromDebugger ) with NULL / a buffer and Length 0 / 1 …
- **U2368** — emu-selftest nt_measured_nqit + the ThreadAffinityMask items (all / low / high / zero / outside / mixed / lengths / NULL / another handle), read …
- **U2367** — --HLE: ntdll.dll!NtSetInformationThread = the kernel layer's service (hle.cpp ntdll list) (files: Emulator/src/hle.cpp)
- **U2366** — NtSetInformationThread class 4 ThreadAffinityMask in the kernel layer (nt_kernel.cpp svc_set_thread_affinity; phnt ntpsapi.h / THREADINFOCLASS 4) …
- **U2365** — tm_nt case nsit (appended after nqit): NtSetInformationThread( NtCurrentThread(), 4 ThreadAffinityMask, &mask, Length ) with the process mask, its …
- **U2364** — emu-selftest nt_measured_nqit: --Sys and --HLE processes - nqit.0 (all 30H bytes, ReturnLength 30H), nqit.0n (ReturnLength NULL), Length 0 / 2CH / …
- **U2363** — --HLE: ntdll.dll!NtQueryInformationThread = the kernel layer's service (hle.cpp ntdll list, as NtQueryInformationProcess) (files …
