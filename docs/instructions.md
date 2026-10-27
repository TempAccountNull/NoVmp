# Instruction support — index

_Generated 2026-10-27 13:38 (HEAD `adda1030 Ledger: U2125-U2141 (agent/p3m7c object V1 - the virtual file system with measured misses, test PE T7, run 10's
services), merged and verified`) by `Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 1006 rows; the 10 highest U numbers)

- **U2141** — tm_nt case pii: ProcessImageInformation (25H, SECTION_IMAGE_INFORMATION 40H; d3d11's DllMain, exploration x8 system call 633) of tm_nt - every field …
- **U2140** — NtQuerySystemInformation class 1 SystemProcessorInformation (d3d11's DllMain, exploration x7 system call 632): tm_nt qsi.1* (Length CH / 10H: 09 00 …
- **U2139** — dxgi's ETW providers: 03BBE5B8-... (LoggerId 11) with its traits (35H bytes "Microsoft.Windows.Graphics.DXGI", the capture's dxgi.dll bytes) and …
- **U2138** — emu-selftest nt_measured_atom (the access-denied add before user32's start and the unmeasured shapes; the adds after the connection run in the …
- **U2137** — The atom table (sample exploration x3 system call 599: dxgi's DllMain -> GlobalAddAtomW -> NtAddAtomEx( a 27-character name, Flags 0 )): tm_nt case …
- **U2136** — emu-selftest nt_measured_rvm (the measured shapes + the stops) (files: Emulator/tests/selftest/st_main.cpp)
- **U2135** — NtReadVirtualMemory of the own process: STATUS_SUCCESS + the copy + the count; 0 bytes: count 0; an unreadable (in whole or part) source or a …
- **U2134** — tm_nt case rvm (sample exploration x1 system call 573: NtReadVirtualMemory( NtCurrentProcess(), PEB + 18H, buf, 8, &read ) after …
- **U2133** — Test PE T7 (t7_vfs, self-written console program, started detached): NtOpenFile of a missing file next to itself and of …
- **U2132** — no_host_passthrough rule pt_host_file: no host file API (Win32 CreateFile* / ReadFile / FindFirstFile* / GetFileAttributes* / LoadLibrary* ..., the …
