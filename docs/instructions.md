# Instruction support — index

_Generated 2026-10-28 09:07 (HEAD `9c4619c9 Ledger: U2142-U2164 (agent/p3m7d object V2 - ProcessImageInformation for both image states, test PE T8, run 11's
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 1029 rows; the 10 highest U numbers)

- **U2164** — tm_nt probe mode "of.probe ACCESS SHARE OPTIONS ATTRIBUTES PATH" (one NtOpenFile from inside, exploration of a run's stop: \Device\KsecDD) (files …
- **U2163** — ProcessBasicInformation as measured: 30H {ExitStatus 103H, PebBaseAddress, AffinityMask = the process affinity mask, BasePriority 8 …
- **U2162** — ProcessInformationClass 56H (phnt ProcessEnclaveInformation, obsolete): STATUS_INVALID_INFO_CLASS for Length 0 / 8 / B0H / 100H, nothing written …
- **U2161** — An unnamed semaphore: NtCreateSemaphore( &h, 100003H, NULL, 0 <= Initial <= Maximum > 0 ) = STATUS_SUCCESS + a handle with the counts (files …
- **U2160** — NtUserRegisterWindowMessage( PUNICODE_STRING ): a placeholder message atom from C200H per name (the same name = the same atom; a table of its own …
- **U2159** — tm_u32 u32.rwm: RegisterWindowMessageW of the OLE name {FB8F0821-0164-101B-84ED-08002B2EC713} from our own GUI test PE (an atom in C000H..FFFFH, the …
- **U2158** — NtOpenEvent( 100000H, {the session's BaseNamedObjects directory, "HookSwitchHookEnabledEvent", 0} ): no such event in the measured session …
- **U2157** — The handle tags are a property of every key handle of the process (measured on the HKLM root, the user's hive root and a key below it) …
- **U2156** — Keys of the user's hive measured from inside with the process' own SID: the hive root (20119H) present, Software\Classes\Local Settings (20119H) and …
- **U2155** — NtOpenThreadToken( NtCurrentThread(), 4 / 8, OpenAsSelf 0 / 1 ): the thread does not impersonate - STATUS_NO_TOKEN (C000007CH), the handle not …
