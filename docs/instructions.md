# Instruction support — index

_Generated 2026-10-27 11:34 (HEAD `5cd5f408 Ledger: U2114-U2124 (agent/p3m7b object S3 - user32's start in an ordinary GUI program measured, test PE T6), merged
and verified`) by `Emulator/tools/isa/gen_status_docs.py`._

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

## Latest ledger entries (`CHANGES_LEDGER.md`, 989 rows; the 10 highest U numbers)

- **U2124** — Suite win_env_sys_loader with T6 (--Sys --loader: 369 services in order, exit 36H; --HLE 36H; the native reference started NORMALLY by tm_nt …
- **U2123** — --HLE user32.dll!CharUpperW (a character in the low word or a NUL-terminated string converted in place; ASCII letters - non-ASCII stops) (files …
- **U2122** — The measured-region watch: the views R1-R4 and the TEB's CLIENTINFO after the conversion are watched (a read hook over the win32k view window and …
- **U2121** — The kernel services of that start, each from tm_nt u32k / etw / etwt / ahc / mit: NtUnmapViewOfSection, the hive root \REGISTRY\MACHINE, NtQueryKey …
- **U2120** — The GUI conversion at the thread's first win32k service (NtGdiInit): GDI shared memory R3 (181000H; +180094H 1, the stock objects' handle shapes) …
- **U2119** — The model of user32's USERSRV connection: NtAlpcSendWaitReceivePort on the CSR API port with user32's CsrpClientConnect (ApiNumber 0, ServerId 3 …
- **U2118** — Kernel -> user callbacks: reply_kind callback / callback_return; the process layer saves the SYSCALL's user state, writes the input and a …
- **U2117** — The second system-service table: win32k's services as the capture's win32u.dll stubs load them (4C 8B D1 B8 imm32, bits 15:12 = 1; 1318 stubs) …
- **U2116** — tm_nt case u32k (sat on NtCurrentThreadEffectiveToken(), kernel32 BasepIsServiceSidBlocked's NtOpenProcessToken 0AH / DuplicateToken / NtAccessCheck …
- **U2115** — Test PE tm_u32 (GUI subsystem, links user32 / gdi32; started NORMALLY by tm_nt case u32 - CreateProcessW without creation flags - writing its lines …
