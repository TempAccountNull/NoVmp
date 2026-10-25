# Instruction support — index

_Generated 2026-10-25 07:10 (HEAD `bce94a52 Ledger: U2000-U2016 (agent/p3m5, plan 1.K.8 + 3.M5 part 1), merged and verified`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 888 rows; the 10 highest U numbers)

- **U2016** — Suite win_env_m5 (Emulator/tools/win_env_m5.py): the guest capture's numbers by an independent parser == emu-oep, patched T3 / T4 copies; T4 2DH in …
- **U2015** — Test PEs (guest code, MASM, never sample-derived): T2 t2_import (kernel32 GetCurrentProcessId / GetModuleHandleW / ExitProcess, 2BH), T3 t3_tls (TLS …
- **U2014** — emu-selftest M5 cases (47 in all): nt_kernel_vm, nt_kernel_process_info, nt_kernel_close_terminate, win_env_syscall_layer_run, win_env_hle_imports …
- **U2013** — milestone M5; emu-oep exit 4 also for an unimplemented --HLE API; usage text (files: Emulator/src/options.cpp, Emulator/tools/oep/emu_oep.cpp)
- **U2012** — emu::win M5: the SYSCALL hook dispatches EAX AND FFFFH from any address to the kernel layer (status in RAX; OUT writes, API calls and allocations in …
- **U2011** — platform facts dwPageSize (4096), dwAllocationGranularity (a power of two >= the page), dwActiveProcessorMask from views\environment.txt (files …
- **U2010** — the --HLE TLS start: index 0 to AddressOfIndex, the thread's TLS block (template + SizeOfZeroFill zero bytes) and the slot array as private …
- **U2009** — emu::hle: --HLE binds every import to a 16-byte stub (RET) in the emulator page 7FFA00000000H; a code hook runs the API (Windows x64 convention) …
- **U2008** — emu::pe imports (import directory from the file: descriptors, ILT / IAT, IMAGE_IMPORT_BY_NAME, ordinals; bounded counts and names) and tls …
- **U2007** — emu::nt::kernel - the one kernel layer behind SYSCALL for --Sys and --HLE: NtTerminateProcess, NtClose, NtAllocateVirtualMemory …
