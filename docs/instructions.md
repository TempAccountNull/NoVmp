# Instruction support — index

_Generated 2026-10-19 01:44 (HEAD `5b3ac17a Ledger: U1620-U1624 (agent/tpause), merged and verified`); refreshed every 30 minutes while work is in progress._

- [Intel instruction sets supported](Intel_instruction_sets_supported.md)
- [AMD / VIA instruction sets](AMD_instruction_sets_supported.md)

**How the page is split.** The first part lists only instructions **your i5-13600K can run** (columns **Done** / **Implementing**). Everything your CPU **cannot honestly run** (CPUID bit clear, AMD/VIA-only, or disabled by Windows) is listed separately below under **"Instructions that can't be supported for now:"**, with its own **CPU cannot support** column giving the reason — those rows are never marked as supported by your CPU; the emulator still implements them per the Intel manual and verifies them against SDM-pseudocode vectors. **Done** = ✅ identical to your i5-13600K (or, in the cannot-support part, ✅ per the manual). **Implementing** = ⏳ being implemented now (agent named) or implemented with an open item, ⬜ queued (not started). **Per profile** (U1632, plan 1.J.57): the split follows the VMware guest (`--guest` / `--vm`, where the hardware cases run); every **CPU cannot support** reason says whether it also holds on the bare host (`--host`) - the forms the bare host enumerates are labelled "--host: enumerated". **Gating only** (your decision 2026-10-17, G10) = not enumerated on either profile, CPUID + #UD verified on both (`Emulator/data/cases_gating*.txt`).

**Your i5-13600K:** runs 1305 forms · **cannot run 1607 forms** (each document lists them separately under "Instructions that can't be supported for now:").

**All forms:** ✅ 1252 · ⏳ 53 · ⬜ 0 · ❌ 1607 (implemented per the manual 1342, open item 48, not implemented yet 217)

## Currently being added

  - ⏳ Last Phase-1 wave (started 2026-10-09 after the audit, base ab67266; besides wt/fix4 U770–U789 and wt/apx_cases U790–U799):
      - ⬜ regs leftovers [remaining parts GATING ONLY (CPUID.07H:EBX[16] AVX512F = 0 and CPUID.07H:EBX[14] MPX = 0 on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest (VCOMPRESS/VPCOMPRESS #UD; MPX BND* forms NOP as on the CPU); no #AC cases needed; the other parts are done (U870-U878, A15)]: scalar FMA (VFMADD*SS/SD …) memory operand reads 16 bytes (decoded W,x: wrong #PF near a page end, missing #AC); an MMX memory operand that takes #PF still applies the x87→MMX transition (TOP=0, tags valid); CVTPD2PI/CVTTPD2PI m128 misaligned: CPU #GP, Unicorn none; #AC not covered for VCOMPRESS/VPCOMPRESS stores and MPX BND memory forms; uc_context_reg_write of CR0/CR3/CR4/EFER/MSR into a context calls live-CPU update functions; two CPU-vs-SDM deviations need your sign-off (MASKMOVDQU-AC, SxDT-STR-SMSW-NO-AC, quirks.md).
      - ⬜ VRCP14/VRSQRT14 (U236) [GATING ONLY (CPUID.07H:EBX[16] AVX512F = 0 on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest; Intel recip14.c adopted (U1245/U1246)] are a correctly-rounded stand-in, not bit-exact: the SDM gives only the 2^-14 bound and refers to RECIP14.c (not available) — derive Intel's table-free algorithm analytically like U81 (RCPPS/RSQRTPS) did, or keep as an open item; never a measured table.
      - ⬜ M3 leftovers [GATING ONLY (CPUID.07H:EBX[16] AVX512F = 0 on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest]: VPMOVD2M/VPMOVQ2M/VPMOVM2D/VPMOVM2Q done (U800); VREDUCE DAZ on C4 watch list; BW 32-bit → 1.I [audit 2026-10-09]
      - ⬜ M4 leftovers [GATING ONLY (CPUID.07H:EBX[16] AVX512F = 0 and CPUID.07H:EDX[2] AVX512_4VNNIW = 0 on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest; the EVEX_VAES readability item is dropped]: Xeon-Phi families (A3); EVEX_VAES macro readability; 32-bit → 1.I [audit 2026-10-09]
    - ⬜ APX leftovers [GATING ONLY (CPUID.(07H,1):EDX[21] APX_F = 0 on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest; the maps 1/2/3/7 sweep, APX INVEPT/INVVPID and the VMX/SMM/LBR/PT interactions are dropped]: INVPCID + MSR-IMM done (U803/U806); sweep for promoted maps 1/2/3/7; INVEPT/INVVPID (VMX decision); VMX/SMM/LBR/PT not modelled; CPUID.29H:EBX[0] done (U793) [audit 2026-10-09]
      - ⬜ AMX leftovers 2 [GATING ONLY (CPUID.07H:EDX[24] AMX_TILE = 0, CPUID.07H:EDX[5] UINTR = 0, PASID (10) / HDC (13) absent from IA32_XSS on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest; the PT / LBR / HWP components are in scope on --host -> 1.J.56]: AMX-TF32 (decision); UINTR XSAVES component; APX TILELOADDRS EVEX.R4 check; cases_amx2 run with --avx10 only; PT/PASID/HDC/LBR/HWP components not modelled [audit 2026-10-09]
      - ⬜ UINTR leftovers [GATING ONLY (CPUID.07H:EDX[5] UINTR = 0 on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest]: IF=0 pending notification, XSAVES component 14, CET effects, STI/MOV SS shadow; APIC/x2APIC parts need a decision (local-APIC model) [audit 2026-10-09]
      - ⬜ Fixes leftovers [remaining parts GATING ONLY (CPUID.07H:EBX[16] AVX512F = 0 on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest (KMOV GPR mask, cosmetic); 32-bit -> 1.I]: LOCK 0F 0D verified (U457) + MPX BNDCFG fixed (U782/U783); drop redundant U129 KMOV GPR mask; 32-bit hw + VEX.W 32-bit → 1.I. VEX 16-bit PM done (U484) [audit 2026-10-09]
      - ⬜ CET leftovers 2: SYSCALL/SYSENTER CET (A1); IDT-delivery mode → Phase 3; PKS on shadow-stack accesses: verify vs SDM + case (PKS walk is U479); compat/legacy → 1.I [audit 2026-10-09]
      - ⬜ [SGX part GATING ONLY (CPUID.07H:EBX[2] SGX = 0 on both profiles), your decision 2026-10-17 (G10): closes as "CPUID + #UD verified" once a test shows CPUID absent + #UD on --host and --guest (ENCLS/ENCLU; model done U1260-U1273); GETSEC TXT leaves out of scope (1.J.11.b); PCONFIG leaf 1BH done (U1020-U1023), host-only -> Phase 3 3.E.6.c] SGX "present but disabled" model and GETSEC TXT leaves (decision, new); PCONFIG leaf 1BH (A4) [audit 2026-10-09]

## Latest ledger entries (`CHANGES_LEDGER.md`, 690 rows)

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
