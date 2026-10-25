# Instruction support — index

_Generated 2026-10-24 18:14 (HEAD `b2d81aca Ledger: U1970-U1979 (agent/p3m3, plan 3.M3 + 1.K.9 extended), merged and verified`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 859 rows; the 10 highest U numbers)

- **U1979** — no_native_check: the M1-only "opens" rule group retired - group "fileio": file access only in the audited reader Emulator/src/input_file.cpp …
- **U1978** — test.cmd: one "<suite>-snip" replay per expected-value suite (157: Add-Exp + cases_backport_t2_sdm + expect_selftest_bad), kinds snip (all lines …
- **U1977** — emu-alltest --snippet-export FILE: every case line Unicorn ran as an emu-snippet spec - engine settings, regions and registers at the snippet's …
- **U1976** — emu-selftest M3 cases snippet_expect, snippet_faults, snippet_syscall, snippet_stops, snippet_trace, snippet_state, snippet_json …
- **U1975** — emu-snippet v1 CLI: --hex / --asm / --state FILE.json / --batch FILE.jsonl, --set / --map / --mem, --stop-at / --stop-ret-depth / --max-insns …
- **U1974** — emu::snippet executor + emu::assemble (Keystone with the U545 decimal-radix check, Capstone): regions, code, automatic stack with the return …
- **U1973** — emu::engine config: built-in CPU model, explicit CPUID profile file (overrides the choice's), strict, AVX-512 / AMX / AVX10 / APX / platform-feature …
- **U1972** — emu::json (strict bounded RFC 8259 reader: integers only, duplicate keys / trailing data / bad escapes refused, depth / item / size limits, byte …
- **U1971** — Unit tests test_x86_hb_<MODEL> - one per built-in model, 39 (QEMU64 .. MAX): 64- and 32-bit mode bit 31 = 0, the --guest / --host rows = the …
- **U1970** — Every built-in CPU model reports CPUID.01H:ECX[31] (hypervisor-present) = 0: x86_cpu_load_model no longer ORs CPUID_EXT_HYPERVISOR into the model's …
