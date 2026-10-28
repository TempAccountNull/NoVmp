# Instruction support — index

_Generated 2026-10-28 12:59 (HEAD `a22e6ec2 Ledger: U2173-U2178 (agent/k30 - the nt_measured native-reference flake 1.K.30), merged and verified`) by
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

## Latest ledger entries (`CHANGES_LEDGER.md`, 1035 rows; the 10 highest U numbers)

- **U2178** — tm_nt case time, the read races: time.tscaux (the processor number read before AND after RDTSCP must agree, up to 100 tries), time.tsc (each QPC …
- **U2177** — nt_measured.py VOLATILE rule 2: tm_nt u32.teb.ci token "+900H" (TEB CLIENTINFO +100H - not modelled, the emulator writes 0 and WATCHES it) compared …
- **U2176** — tm_nt case qip: "written" = the highest byte written over three identical queries with the fills CCH / 33H / 5AH (the printed values from the first) …
- **U2175** — tm_nt time.qpc.it: QPC - KUSER InterruptTime read AT an InterruptTime update (spin until the clock interrupt writes a new value, then QPC; the least …
- **U2174** — nt_measured.py: on a native difference the first differing native line is printed beside the recorded one (tm_nt prints shapes only; the identifier …
- **U2173** — 1.K.30 cause + fix (case sp): tm_nt counts the bytes NtQueryAttributesFile writes with THREE identical queries and three fills (CCH / 33H / 5AH) - a …
- **U2164** — tm_nt probe mode "of.probe ACCESS SHARE OPTIONS ATTRIBUTES PATH" (one NtOpenFile from inside, exploration of a run's stop: \Device\KsecDD) (files …
- **U2163** — ProcessBasicInformation as measured: 30H {ExitStatus 103H, PebBaseAddress, AffinityMask = the process affinity mask, BasePriority 8 …
- **U2162** — ProcessInformationClass 56H (phnt ProcessEnclaveInformation, obsolete): STATUS_INVALID_INFO_CLASS for Length 0 / 8 / B0H / 100H, nothing written …
- **U2161** — An unnamed semaphore: NtCreateSemaphore( &h, 100003H, NULL, 0 <= Initial <= Maximum > 0 ) = STATUS_SUCCESS + a handle with the counts (files …
