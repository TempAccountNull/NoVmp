# Instruction support — index

_Generated 2026-10-23 07:50 (HEAD `2c67d396 Status docs: one instruction table for Intel + AMD/VIA, split by privilege (P26 generator)`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 816 rows; the 10 highest U numbers)

- **U1915** — SDM citations "325383-093 Vol2X" (the Vol. 2 set number with a single-volume label) -> 253666 / 253667 / 334569-093 Vol2A / 2B / 2D (AGENT_RULES …
- **U1914** — harness_selftest_tags lines 20-27: a value outside a mask (VAL/FFFFFFFFFFFFFFFEH) in one middle bit - bits 4, 12, 20, 28, 36, 44, 52, 60, one per …
- **U1913** — the discriminator is a second register: probe line 36 and cases_waitpkg_host.txt line 131 - 16 x (RDTSC; UMWAIT without UMONITOR, deadline T + 2^40 …
- **U1912** — cases_waitpkg_probe_hw line 35 (UMWAIT without UMONITOR) keeps Unicorn 1H..12H(timing): the guest's own no-wait samples reach 10H..12H (6 of 2620 …
- **U1911** — cases_waitpkg_host.txt: the wake W exactly - 16 x (RDTSC; TPAUSE C0.2 far; RDTSC), R12 = 1 if the smallest (elapsed - 200000H, unsigned) is below …
- **U1910** — cases_waitpkg_probe_hw far lines 30 / 31 / 33: Unicorn R11 = BSR(W + d) (U1620 / U1622: the TSC jumps by the 2^21-tick wake, d = the no-wait tail) = …
- **U1906** — cases_probe_p10_hw RDTSC / RDTSCP under TF: the rdx alternatives removed (EDX equal in every same-run pairing: Unicorn's TSC is the host's, read …
- **U1905** — harness_selftest_tags lines 17 / 18: a value outside a mask in bit 0 only (hw) / bit 63 only (Unicorn) is refused
- **U1904** — harness_selftest_tags lines 15 / 16 / 19: a value one above a range (hw, Unicorn) is refused, both range ends are inside; test_runner.ps1 …
- **U1903** — cases_probe_p1_hw LSL ecx, 53h = 3C00H / N << 14 per environment / core type (guest N 0-7, hostP 0-11, hostE 12-19; Vol2A LSL p. 3-573) …
