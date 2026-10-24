# Instruction support — index

_Generated 2026-10-24 13:39 (HEAD `81c5e102 Ledger: U1950-U1954 (agent/p3m2, Phase 3 milestone M2), merged and verified`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 839 rows; the 10 highest U numbers)

- **U1954** — emu-selftest M2 cases engine_profile_parse / _at_open / _host / _guest, engine_strict_after_first_insn, engine_rng, engine_memory_map …
- **U1953** — emu::reg: minimal register / flag dump (90 fields: GPRs, RIP, RFLAGS decoded, selectors, FS / GS base, GDTR / IDTR / LDTR / TR, CR0 / CR4 / XCR0 …
- **U1952** — emu::engine: uc_open(X86, 64) + UC_CPU_X86_MAX, CPUID profile installed at uc_open before any map / register access (strict = fork default …
- **U1951** — emu::profile: the captured CPUID profiles of decision 3.0.3 (--host default = cpuid_i5-13600k_bare_host.txt, --guest = cpuid_i5-13600k.txt, --max …
- **U1950** — test.cmd list: an argument equal to the repository root (suite no_native_check) is shown as '.' instead of Substring($Root.Length + 1), which threw …
- **U1943** — Emulator/data/no_native_allowlist.tsv: the 5 NoVmp findings of today's tree (emulator.cpp + rwx_allocator.cpp ClCompile in NoVmp.vcxproj x2 …
- **U1942** — Emulator/tools/check_no_native.py + suite no_native_check: S1 emulator source scan (exec / fnptr / loader / process / kernel / hosttime / hostcpu / …
- **U1941** — emu::host_guard (VirtualQuery walk, SEH-guarded read-only pattern scan) + emu-selftest cases host_guard_regions and no_native_runtime: no executable …
- **U1940** — Phase 3 Emulator projects (plan 3.M1 / 3.A 2.2): Emulator.vcxproj static lib (refs unicorn / capstone / keystone, linux-pe include) with …
- **U1935** — cases_waitpkg_host.txt cases [56]-[68], [71]-[78] (21 TPAUSE / UMWAIT wait lines: deadline, wake, OS limit, unarmed, monitor trigger / MONITOR / …
