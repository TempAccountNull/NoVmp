# Instruction support — index

_Generated 2026-10-29 07:52 (HEAD `d3704250 Ledger: U2240-U2247, U2250-U2260 (agent/p3m7 objects A / B - the exception path, plan 3.M7) and U2280-U2291
(agent/p3cm2 object X1' - the CMApi minimal device, T10 as ahc.45), merged and verified`) by `Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 1074 rows; the 10 highest U numbers)

- **U2291** — emu-selftest nt_measured_run14: the unnamed mutant (two objects, wait / release / not-owned, other access / InitialOwner stop), kd.abs open (+ …
- **U2290** — the second view of ntdll: NtMapViewOfSection( that section, NtCurrentProcess(), &base 0, 0, 0, &SectionOffset 0, &ViewSize 0, ViewShare, 0 …
- **U2289** — the sample's OWN open (its section .xditszm +124E99H): NtOpenSection( &s, 5, {NULL root, "\KnownDlls\ntdll.dll", Attributes 0} ) = STATUS_SUCCESS, a …
- **U2288** — the unnamed mutant: NtCreateMutant( &h, 1F0001H, ObjectAttributes NULL, InitialOwner FALSE ) = STATUS_SUCCESS, a new handle per call, free; wait / …
- **U2287** — setupapi.dll's ETW providers of the DllMain phase: etw.reg58 (45B576A6..., RegType 2, LoggerId 53), etw.reg59 (50A0F496..., ENABLED by a trace …
- **U2286** — suite win_env_sys_loader + T10: --Sys --loader 247 services in the recorded order, exit 3A000100H; fs_access.txt = the loader's directory open + …
- **U2285** — emu-selftest nt_measured_cmapi: the measured open (+ a second handle), other access / share / OBJ_CASE_INSENSITIVE / spelling / CMNotify stop, every …
- **U2284** — --HLE cfgmgr32.dll!CM_Locate_DevNodeW( pdn, NULL, CM_LOCATE_DEVNODE_NORMAL ): CR_SUCCESS, *pdn = 1 - cfgmgr32's own result under --Sys (the root …
- **U2283** — NtQueryLicenseValue: tm_nt case lic (+ probe mode lic.probe NAME SIZE) and kernel svc_query_license_value - only the measured value name …
- **U2282** — test PE T10 (t10_cm: imports cfgmgr32.dll, CM_Locate_DevNodeW( &dn, NULL, 0 ); exit 3A000000H OR the CONFIGRET OR (DEVINST written << 8); first …
