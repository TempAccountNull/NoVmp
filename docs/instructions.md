# Instruction support — index

_Generated 2026-10-25 17:29 (HEAD `839f7f43 Ledger: U2040-U2044 (agent/gpfix, the sample's #GP, P37), merged and verified`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 912 rows; the 10 highest U numbers)

- **U2044** — Hardware cases Emulator/data/cases_xpage_hw.txt (own routine written to CODE + 0FF7H and called; the instruction after XOR crosses 30001000H; 7 …
- **U2043** — Lazy flags survive the cross-page instruction restart: the instruction-start state (cc_op, cc_op_dirty, pc_save) restored by disas_insn case 2 …
- **U2042** — emu::win::locate_capture honours NOVMP_CAPTURE_ROOT (explicit --sysdlls, then <root>\sysdlls\<env>, then the data folder's ancestors) - worktrees …
- **U2041** — Suite win_env_probe register-trace checks (--trace-regs, --trace-last 6, usage errors) + selftest options_oep_probe extension (files …
- **U2040** — emu-oep --trace-regs (RAX..R15 / RFLAGS before each instruction + its memory reads / writes on every trace line) and --trace-last N (ring of the …
- **U2039** — Suite win_env_sys_loader: the --Sys --loader start through the real LdrInitializeThunk, the 18 services before the frontier answered in order, the …
- **U2037** — capture_sysdlls selftest 2b: ComputerName / ProductId values and an HKU key refused, nothing written (file: Emulator/tools/capture_sysdlls.py)
- **U2036** — Selftests nt_kernel_sys_services, win_env_nt_continue; nt_kernel_vm updated (partial release, MEM_IMAGE query) (file …
- **U2035** — NtProtectVirtualMemory, NtAllocateVirtualMemoryEx (address requirements), reservations as address space only (committed runs mapped), MEM_RELEASE of …
- **U2034** — The captured registry subset: capture_sysdlls.py --reg / --reg-value (registry.txt; HKLM only; identifying names refused), load_registry …
