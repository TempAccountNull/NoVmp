# Instruction support — index

_Generated 2026-10-27 07:29 (HEAD `11415de9 Ledger: U2104-U2113 (agent/p3m7a P43 objects A / B / C + agent/p3m7b STEP 2 object - the KnownDlls slots, the DLL
path search, nt_measured volatile fields by shape), merged and verified`) by `Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 978 rows; the 10 highest U numbers)

- **U2113** — Suite nt_measured (USER DECISION 2026-10-27 measured-service drift): VOLATILE fields compared by SHAPE, every other byte exactly - the list (field / …
- **U2112** — Selftest nt_measured_dllsearch: the image's-directory miss leaves the caller's buffer untouched (file: Emulator/tests/selftest/st_main.cpp)
- **U2111** — NtQueryAttributesFile: a miss in the image's directory writes NOTHING - the image's directory lies on the system volume (measured: tm_nt sp.N.app …
- **U2110** — emu-selftest nt_measured_dllsearch (a process whose facts hold two synthetic DLL images: the path search, the open / section / view, a second view …
- **U2109** — tm_nt case mit + NtQueryInformationProcess ProcessMitigationPolicy (34H) policy 0BH ProcessSystemCallFilterPolicy (user32's start): Length 8 = …
- **U2108** — tm_nt ahc.9 .. ahc.32 + NtApphelpCacheControl LookupCdb for the 24 names the loader asks after the sample's import processing (the loader's …
- **U2107** — The emulated file system answers the image's directory (only the image) and the system directory's CAPTURED files: NtQueryAttributesFile (new …
- **U2106** — tm_nt case sp: the loader's DLL path search for a system DLL that is not a KnownDll (d3d11, dxgi, dcomp, dwmapi, kernel.appcore) …
- **U2105** — --Sys: a fixed slot for every \KnownDlls section - emu::win::system_image_slot keeps ntdll / kernel32 / kernelbase (7FFB00000000H / 7FFB10000000H / …
- **U2104** — tm_nt wnf4: the native reference of WNF state 418A073AA3BC1C75H (WIL feature usage) as a boot-stable shape - status, ChangeStamp non-zero …
