# Instruction support — index

_Generated 2026-10-09 15:53 (HEAD `ca3dd6c Ledger U790-U793 (APX cases for the 10 uncovered forms, EVEX.R4 with k, APX_NCI_NDD_NF gating, instruction-table generator); docs refresh + Phase-1 audit plan state`); refreshed every 30 minutes while work is in progress._

- [Intel instruction sets supported](Intel_instruction_sets_supported.md)
- [AMD / VIA instruction sets](AMD_instruction_sets_supported.md)

**How the page is split.** The first part lists only instructions **your i5-13600K can run** (columns **Done** / **Implementing**). Everything your CPU **cannot honestly run** (CPUID bit clear, AMD/VIA-only, or disabled by Windows) is listed separately below under **"Instructions that can't be supported for now:"**, with its own **CPU cannot support** column giving the reason — those rows are never marked as supported by your CPU; the emulator still implements them per the Intel manual and verifies them against SDM-pseudocode vectors. **Done** = ✅ identical to your i5-13600K (or, in the cannot-support part, ✅ per the manual). **Implementing** = ⏳ being implemented now (agent named) or implemented with an open item, ⬜ queued (not started).

**Your i5-13600K:** runs 1305 forms · **cannot run 1606 forms** (each document lists them separately under "Instructions that can't be supported for now:").

**All forms:** ✅ 1173 · ⏳ 131 · ⬜ 1 · ❌ 1606 (implemented per the manual 1258, open item 54, not implemented yet 294)

## Currently being added

  - ⏳ Last Phase-1 wave (started 2026-10-09 after the audit, base ab67266; besides wt/fix4 U770–U789 and wt/apx_cases U790–U799):
    - ⏳ [agent, wt/sysins, U800–U829] AVX512DQ VPMOVD2M/Q2M/M2D/M2Q; CPL0 WRMSRNS, RDMSRLIST/WRMSRLIST, MSR-IMM, PBNDKB, HRESET, LKGS, INVPCID.
    - ⏳ [agent, wt/regs, U830–U849] uc_reg MM0–MM7 + PKRU (4.2); uc_context round-trip tests (4.3/4.7); CPUID 0DH.1 EBX without XSAVEC (M0 leftover); #AC alignment check (5.4, 1.H.5); RDRAND/RDSEED seeded vs host source (D8, 5.2.9).
    - ⏳ [agent, wt/sweepmem, U850–U859] the 205 "not native-safe" sweep forms run against the CPU with a valid memory base / in-buffer targets (≈67 ⏳ legacy rows); new --full baseline.
    - ⏳ [agent, wt/x87misc, U860–U889] x87 FDP/FCS/FDS per effective CPUID + unmasked #IS/#D compares; ref_evex_m1 two-NaN lanes; EVEX #O/#U cases; M2 leftovers (scatter probe hook, disp8 note, ev_dsrc); fp16 L'L=11b.
  - ⏳ 1.15d EVEX / AVX-512: M0–M5 merged; open = VPMOVD2M/Q2M/M2D/M2Q (not implemented), Xeon-Phi families (A3) [audit 2026-10-09]
      - ⬜ M0 leftover: 0DH.1 EBX = 0 on models without XSAVEC (SDM 13.2) + update the test that pins it; profile 0DH.1 EBX kept as captured (documented) [audit 2026-10-09]
        - ⬜ quirk leftovers: FDP/FCS/FDS behaviour follow the effective CPUID.7:EBX[6]/[13] (custom profiles); x87 compares with unmasked #IS/#D (SDM review + cases). REP LODS flake done (U544) [audit 2026-10-09]
        - ⬜ drop no_nan_pairs() in ref_evex_m1.py, regenerate cases_evex_m1 with two-NaN lanes (3DNow! rule → Phase 2) [audit 2026-10-09]
      - ⬜ M2 leftovers: scatter hook sees single-byte probe stores; fix evex_forms.tsv disp8 note (VPGATHERDQ/QD); use ev_dsrc for VPERMI2x/VPERMT2x/VPTERNLOG; E4NF/E6NF unconfirmable (no AVX-512 host); 32-bit → 1.I [audit 2026-10-09]
      - ⬜ VRCP14/VRSQRT14 (U236) are a correctly-rounded stand-in, not bit-exact: the SDM gives only the 2^-14 bound and refers to RECIP14.c (not available) — derive Intel's table-free algorithm analytically like U81 (RCPPS/RSQRTPS) did, or keep as an open item; never a measured table.
      - ⬜ SSE exc leftovers: EVEX PS/PD unmasked #O/#U expected-value cases; fix stale hc_run comment (U447); trim U441/U442 per-lane code [audit 2026-10-09]
      - ⬜ M3 leftovers: implement VPMOVD2M/VPMOVQ2M/VPMOVM2D/VPMOVM2Q (AVX512DQ, EVEX.F3.0F38 39/38); VREDUCE DAZ on C4 watch list; BW 32-bit → 1.I [audit 2026-10-09]
      - ⬜ M4 leftovers: Xeon-Phi families (A3); EVEX_VAES macro readability; 32-bit → 1.I [audit 2026-10-09]
      - ⬜ fp16 leftovers: scalar FP16 L'L=11b without EVEX.b (decide #UD vs LIG per Table 2-38 + cases); VRCP/VRSQRT PH → line 60; ambiguities C4 [audit 2026-10-09]
    - ⬜ APX leftovers: INVPCID + MSR-IMM base instructions (→ wt/sysins); sweep for promoted maps 1/2/3/7; INVEPT/INVVPID (VMX decision); VMX/SMM/LBR/PT not modelled; CPUID.29H:EBX[0] done (U793) [audit 2026-10-09]
      - ⬜ AMX leftovers 2: AMX-TF32 (decision); UINTR XSAVES component; APX TILELOADDRS EVEX.R4 check; cases_amx2 run with --avx10 only; PT/PASID/HDC/LBR/HWP components not modelled [audit 2026-10-09]
      - ⬜ Key Locker leftovers: KeySource 1 (via the D8 RNG source), IWKeyBackup MSRs, MSR_FEATURE_CONFIG gate; AESKLE in SMM deferred [audit 2026-10-09]
      - ⬜ UINTR leftovers: IF=0 pending notification, XSAVES component 14, CET effects, STI/MOV SS shadow; APIC/x2APIC parts need a decision (local-APIC model) [audit 2026-10-09]
      - ⬜ Fixes leftovers: LOCK 0F 0D + MPX BNDCFG → wt/fix4; drop redundant U129 KMOV GPR mask; 32-bit hw + VEX.W 32-bit → 1.I. VEX 16-bit PM done (U484) [audit 2026-10-09]
      - ⬜ CET leftovers 2: SYSCALL/SYSENTER CET (A1); IDT-delivery mode → Phase 3; PKS on shadow-stack accesses: verify vs SDM + case (PKS walk is U479); compat/legacy → 1.I [audit 2026-10-09]
      - ⬜ SGX "present but disabled" model and GETSEC TXT leaves (decision, new); PCONFIG leaf 1BH (A4) [audit 2026-10-09]

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
