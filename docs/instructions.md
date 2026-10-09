# Instruction support — index

_Generated 2026-10-09 15:51 (HEAD `6f5f6c0 U793: CPUID.29H:EBX.APX_NCI_NDD_NF gates the forms whose APX CPUID column names it`); refreshed every 30 minutes while work is in progress._

- [Intel instruction sets supported](Intel_instruction_sets_supported.md)
- [AMD / VIA instruction sets](AMD_instruction_sets_supported.md)

**How the page is split.** The first part lists only instructions **your i5-13600K can run** (columns **Done** / **Implementing**). Everything your CPU **cannot honestly run** (CPUID bit clear, AMD/VIA-only, or disabled by Windows) is listed separately below under **"Instructions that can't be supported for now:"**, with its own **CPU cannot support** column giving the reason — those rows are never marked as supported by your CPU; the emulator still implements them per the Intel manual and verifies them against SDM-pseudocode vectors. **Done** = ✅ identical to your i5-13600K (or, in the cannot-support part, ✅ per the manual). **Implementing** = ⏳ being implemented now (agent named) or implemented with an open item, ⬜ queued (not started).

**Your i5-13600K:** runs 1305 forms · **cannot run 1606 forms** (each document lists them separately under "Instructions that can't be supported for now:").

**All forms:** ✅ 1173 · ⏳ 131 · ⬜ 1 · ❌ 1606 (implemented per the manual 1258, open item 54, not implemented yet 294)

## Currently being added

- (nothing in progress)

## Latest ledger entries (`CHANGES_LEDGER.md`, 394 rows)

- U103 — URDMSR/UWRMSR (F2/F3 0F38 F8 11; VEX.128.F2/F3.MAP7.W0 F8 /0 id, new VEX map 7): ENABLE=0 #UD, address/bitmap/allow-list #GP, via helper_rd…
- U104 — UINTR: CLUI/STUI/TESTUI/UIRET (F3 0F01 EC-EF), SENDUIPI (F3 0F C7 /6 reg), 64-bit only; CR4.UINTR; UIRR/UIF/UIHANDLER/UISTACKADJUST/MISC/PD…
- U105 — LOCK on any 0F 18 form (PREFETCHh, PREFETCHRST2, hint NOPs) #UD (QEMU ran it as a NOP)
- U110 — TSX: RTM with every transaction aborting at once (RTM_ALWAYS_ABORT reported): XBEGIN #GP(0) for a bad fallback, else EAX=0, RIP=fallback; X…
- U111 — WAITPKG: UMONITOR/UMWAIT/TPAUSE (F3/F2/66 0F AE /6 reg, U75 prefix rule), #GP(0) src[31:1] or CR4.TSD at CPL>0, immediate-wake timing with …
- U112 — ENQCMD/ENQCMDS (F2/F3 0F38 F8 memory): #GP(0) on IA32_PASID[31]=0 / CPL / alignment / reserved source bits; no enqueue register -> retry st…
- U113 — GETSEC (CAPABILITIES -> EAX=0, other leaves #UD, CR4.SMXE), PCONFIG (#UD/#GP rules, not reported by any model), ENCLS/ENCLU/ENCLV stay #UD
- U114 — CET state + shadow-stack instructions: SSP (UC_X86_REG_SSP), IA32_U_CET/S_CET/PL0-3_SSP/INT_SSP_TAB, CR4.CET rules, #CP vector 21; RDSSP, I…
- U115 — Near CALL/RET shadow-stack push/pop, #CP(NEAR-RET), non-canonical SS address #GP(0)
- U116 — IBT: tracker on near/far indirect CALL/JMP, NOTRACK, ENDBR32/64, legacy code-page bitmap, #CP(ENDBRANCH)
- U117 — Shadow-stack accesses use own MMU modes (NB_MMU_MODES 3->5): shadow-stack page types under paging, #PF error code bit 6
- U3 — (placeholder for the 1.6 backports - all now listed individually as U5-U36)
