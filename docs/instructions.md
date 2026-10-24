# Instruction support — index

_Generated 2026-10-24 15:34 (HEAD `f7dde46d Ledger: U1960-U1969 (agent/k9k15, plan 1.K.9 + 1.K.15), merged and verified`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 849 rows; the 10 highest U numbers)

- **U1969** — docs/quirks.md CR0 / CR4 subsection: the "open gap" bullet replaced by the shared-check description (what is new for the instruction …
- **U1968** — unit tests kk_movcr0_bits, _movcr_paging64, _movcr_compat, _movcr_mode32, _movcr0_activate, _movcr_cpl3_lmsw_clts: MOV CR0 / CR3 / CR4 at CPL0 per …
- **U1967** — MOV to CR3 outside 64-bit mode uses a 32-bit source: RAX[63:32] in compatibility mode no longer reaches the CR3[63:MAXPHYADDR] #GP (a 64-Bit Mode …
- **U1966** — MOV to CR0 / CR4 (helper_write_crN) raise #GP(0) via the shared checks: new (1) CR0[63:32], (2) ignored CR0[31:0] bits dropped, (3) PG without PE …
- **U1965** — ONE shared check function for MOV to CR0 / CR4: rules (1)-(15) moved from unicorn.c (x86_crw_check_cr0 / _cr4 removed) to misc_helper.c …
- **U1964** — callers that relied on the raw store, adapted to legal sequences (found with a temporary audit build over test.cmd, the hardware files and --full …
- **U1963** — unit tests kk_cr0_reserved, _cr0_combinations, _cr_paging_64, _cr_paging_compat, _cr0_activate, _cr_mode32, _cr4_reserved_pcide …
- **U1962** — uc_reg_write / uc_context_reg_write CR0 / CR4 apply the checks of MOV to CR (x86_crw_check_cr0 / _cr4, x86_crw_write): refused with UC_ERR_ARG …
- **U1961** — unit test test_x86_kk_max_hypervisor_bit (kk_ block): MAX 64- / 32-bit 0, Haswell 1, MAX + the --guest / --host rows = the captured bit, MAX + a …
- **U1960** — MAX model: CPUID.01H:ECX[31] (hypervisor-present) = 0 - x86_cpu_expand_features clears CPUID_EXT_HYPERVISOR when max_features (upstream sets it on …
