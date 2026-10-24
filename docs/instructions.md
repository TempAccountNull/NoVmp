# Instruction support — index

_Generated 2026-10-24 03:23 (HEAD `d1672631 Ledger: U1930-U1935 (agent/c4tests, Gate 1 C4 G31 test gaps + F1), merged and verified`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 830 rows; the 10 highest U numbers)

- **U1935** — cases_waitpkg_host.txt cases [56]-[68], [71]-[78] (21 TPAUSE / UMWAIT wait lines: deadline, wake, OS limit, unarmed, monitor trigger / MONITOR / …
- **U1934** — cases_waitpkg_host.txt case [79] (IA32_TSC_ADJUST 2^62, TPAUSE to the past deadline 2^62): 16 x (RDTSC; TPAUSE; ADC R14, 0; RDTSC), R12 = 1 if the …
- **U1933** — test_x86.c test_x86_ct_rsm_dr6_fixed1: the SMI handler writes a map DR6 without the reserved set-to-1 bits (14802H) on Intel Table 34-3 / 34-1 and …
- **U1932** — test_x86.c test_x86_ct_vmx_tpause_delay: TPAUSE in VMX non-root ("enable user wait and pause" 1, "RDTSC exiting" 0), TSC scaling 2.0 / 0.5 and …
- **U1931** — test_x86.c tw_ block: tw_near's window = the host ticks since the base was taken (tw_open / new tw_base note __rdtsc(); E = __rdtsc() - mark, capped …
- **U1930** — test_x86.c ct_ block: test_x86_ct_moveout_freed (region mapped after snapshot A, unmapped = moved out, restore A frees it, uc_mem_map_ptr of new …
- **U1923** — cases_waitpkg_probe_hw line 35 (UMWAIT without UMONITOR): Unicorn R11 class 0x0..0x14(timing) / 0x3F on both core types, derived from the U1620 wait …
- **U1922** — Emulator/tools/check_sdm_citations.py + test.cmd suite sdm_citation_check (test_runner.ps1): refuses 325383 / 325384-093 followed by a single-volume …
- **U1921** — Emulator/data/isa_privilege.tsv: the 32 SDM citations with the Vol. 2 set number 325383-093 and a Vol2A label -> 253666-093 Vol2A (same length …
- **U1920** — CHANGES_LEDGER.md completeness (Gate 1 audit 2026-10-23 §5, C3): U546 row (Keystone source fix 9a9b425f) and the closing line corrected (keystone …
