# Instruction support — index

_Generated 2026-10-25 11:45 (HEAD `c1715ac4 Ledger: U2020-U2037, U2039 (agent/p3m5b, plan 3.M5 part 2a), merged and verified`) by
`Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 907 rows; the 10 highest U numbers)

- **U2039** — Suite win_env_sys_loader: the --Sys --loader start through the real LdrInitializeThunk, the 18 services before the frontier answered in order, the …
- **U2037** — capture_sysdlls selftest 2b: ComputerName / ProductId values and an HKU key refused, nothing written (file: Emulator/tools/capture_sysdlls.py)
- **U2036** — Selftests nt_kernel_sys_services, win_env_nt_continue; nt_kernel_vm updated (partial release, MEM_IMAGE query) (file …
- **U2035** — NtProtectVirtualMemory, NtAllocateVirtualMemoryEx (address requirements), reservations as address space only (committed runs mapped), MEM_RELEASE of …
- **U2034** — The captured registry subset: capture_sysdlls.py --reg / --reg-value (registry.txt; HKLM only; identifying names refused), load_registry …
- **U2033** — NtQuerySystemInformation 0 / 3EH / 72H (C0H NOT PROVIDED: STATUS_INVALID_INFO_CLASS), NtQueryInformationProcess 22H / 24H, NtQueryVirtualMemory …
- **U2032** — Handle table; events (create / set / reset / clear / wait, one thread); NtQueryPerformanceCounter = the virtual clock (one tick per instruction, 10 …
- **U2031** — emu-oep --Sys --loader (opt-in): ntdll mapped by the kernel, NLS view, API set schema view, image views, the CONTEXT, IRETQ to the real …
- **U2030** — NtContinue: CONTEXT (CONTROL / INTEGER / FLOATING_POINT) loaded, return through IRETQ from an emulator kernel stack page; debug registers = stop …
- **U2029** — Suite win_env_process; no_host_passthrough exemptions (exact counts) for the guest-visible System32 paths written into guest memory (files …
