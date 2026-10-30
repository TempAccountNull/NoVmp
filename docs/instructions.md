# Instruction support — index

_Generated 2026-10-30 14:35 (HEAD `05c681ed Ledger: U2383 (emu-oep --watch-write / --watch-value / --trace-from) and U2384 (tm_u32 with the host's user32
10.0.19041.6926) (P59)`) by `Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 1099 rows; the 10 highest U numbers)

- **U2384** — tm_u32 knows two user32 builds by public symbol: the VMware guest's 10.0.19044.7807 and the bare host's 10.0.19041.6926 (user32.pdb …
- **U2383** — emu-oep observation options: --watch-write ADDR:LEN (repeatable, at most 16: every guest write overlapping the range - instruction count, RIP, size …
- **U2382** — the --host variants: win_env_sys_loader.py / win_env_exceptions.py / nt_measured.py --env bare_host (the bare-host capture, the host statuses / …
- **U2381** — hostref_import.py: the bare-host native references of host_reg_ref_v2 as Emulator/data/nt_measured_bare_host.txt (1006 lines, 48 cases; nqit / nsit …
- **U2380** — KUSER_SHARED_DATA.XState: EnabledSupervisorFeatures = the supported ones of PT (bit 8) and CET_U (bit 11), EnabledUserVisibleSupervisorFeatures = …
- **U2377** — suite win_env_cpulp: the recorded T11 reference well-formed (8 distinct APIC IDs), T11 under --guest --Sys --loader = the recorded lines + exit …
- **U2376** — emu-selftest engine_cpuid_per_lp: every profile row on every LP of the guest (8) and host (20) tables = the profile + the LP's APIC ID / rows …
- **U2375** — test PE T11 t11_cpulp (asm, kernel32 only): pinned to each processor of the process mask, CPUID 01H / 04H.0-3 / 06H / 0BH.0-2 / 1AH / 1FH.0-2, the …
- **U2374** — per-logical-processor CPUID: Emulator/data/cpuid_lp_i5-13600k.txt (guest: 8 LPs, APIC 0 - 7, the profile = LP 5) and …
- **U2373** — emu-oep --stop-at-written-exec (stop BEFORE the first instruction on a page the guest's instructions wrote: a UC_HOOK_MEM_WRITE page record + the …
