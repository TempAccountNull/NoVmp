# Instruction support — index

_Generated 2026-10-25 03:24 (HEAD `47685d58 Ledger: U1980-U1991 (agent/p3m4, plan 3.M4), merged and verified; U1978 text 157 -> 156`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 871 rows; the 10 highest U numbers)

- **U1991** — Suite capture_sysdlls_selftest (capture_sysdlls.py --selftest --alltest <tests>\emu-alltest.exe): 13 decisions on synthetic "tag environment:" …
- **U1990** — capture_sysdlls.py decides the environment ONLY by the project's evidence-based detection (U1893), user decision 2026-10-25: it runs emu-alltest …
- **U1989** — no_native_check S5: test-PE projects (Emulator/tests/pe/*) are guest code - no project references, output in tests\test_pe\, referenced by no …
- **U1988** — Suite no_host_passthrough (Emulator/tools/check_no_passthrough.py), the NEW "no host pass-through" scan: static over the emulator code (Nt* / Zw* …
- **U1987** — Suite win_env_layout (Emulator/tools/check_win_layout.py): 49 PEB / TEB / KUSER offsets, GDT selectors and the KUSER address of win_env.hpp against …
- **U1986** — Suite win_env_t1 (Emulator/tools/win_env_t1.py): the guest capture's size / SHA-256, NtTerminateProcess's number by an independent export parser == …
- **U1985** — emu-selftest M4 cases pe_parse_refusals, pe_fuzz, pe_map_relocate, sysdll_syscall_table, win_env_layout, win_env_terminate, win_env_loud_stops …
- **U1984** — emu-oep M4: reads INPUT as data and runs it in emu::win (environment of --guest / --host, facts logged), --dry-run (the M1 behaviour), --image-base …
- **U1983** — emu::win process: the minimum Windows x64 user environment for one PE - KUSER_SHARED_DATA synthesized at 7FFE0000H (no host page), PEB / TEB (GS …
- **U1982** — emu::pe: PE32+ parsed with every field bounds-checked in 64-bit arithmetic (winnt.h IMAGE_* structures, sizes static_asserted) and mapped AS DATA …
