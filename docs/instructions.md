# Instruction support — index

_Generated 2026-10-08 03:53 (HEAD `25fd878 U95: keep EVEX.V' in 64-bit mode (U93 cut VEX.vvvv to 4 bits in every mode)`); refreshed every 30 minutes while work is in progress._

- [Intel instruction sets supported](Intel_instruction_sets_supported.md)
- [AMD / VIA instruction sets](AMD_instruction_sets_supported.md)

**Legend:** ✅ runs on our i5-13600K and the emulator is identical to it · ⏳ runs on our CPU, the emulator has an open item · ⬜ not implemented / not verified yet · **❌ NOT SUPPORTED on our i5-13600K** (the CPU cannot execute it, so it can never be checked against our hardware; it is still implemented from the manual where possible — the row says "implemented per the manual" or "not implemented yet").

**All forms:** ✅ 1173 · ⏳ 132 · ⬜ 0 · ❌ 1606 (implemented per the manual 223, open item 76, not implemented yet 1307)

## Currently being added

    - ⏳ When the 1.15c branches land: flip their `verified_forms.tsv` rows from "ud" to "sdm" (done for SHA512/SM3/SM4) and run `cases_reach.txt` with `--strict` (done).
    - ⬜ WRUSSD/WRUSSQ: #GP at CPL3 (Windows enables CET) — Phase 2 CPL3 work (D6).
  - ⏳ 1.15c small Intel extensions — merged so far: SHA512/SM3/SM4 (U82–U84), AVX-VNNI-INT8/INT16, AVX-IFMA, AVX-NE-CONVERT (U85–U88), Key Locker, RAO-INT, MOVRS, USER_MSR, UINTR, LOCK 0F 18 (U100–U105), TSX, WAITPKG, ENQCMD, GETSEC/PCONFIG/SGX faults, CET shadow stack + IBT + shadow-stack paging (U110–U117); CPL0-only ones = exact CPL3 faults per D6.
  - ⏳ 1.15d EVEX / AVX-512 — design done (`emulator\EVEX_DESIGN.md`, 2026-10-08): 2537 EVEX forms (AVX512F 1071, BW 250, DQ 126, CD 18, FP16 359, AVX10.2 ~410, others ~130) + 51 opmask forms; state (ZMM 32x512, k0–7) already in CPUX86State.
      - ⬜ M0 leftovers: Haswell-class models without XSAVEC still report non-zero 0DH.1 EBX (SDM: 0) — kept for an existing test, revisit; profile 0DH.1 EBX stays as captured (capture machine IA32_XSS share unknown).
      - ⬜ K leftovers / M1 notes: (1) 32-bit VEX.B leak: disas_insn_new sets rex_b from VEX.B in every mode (SDM: ignored outside 64-bit) — fix globally in the prefix parser; (2) VEX in 16-bit protected mode is not recognised (upstream: protected, non-VM86); (3) vvvv is 4 bits outside 64-bit — validate_evex must decide per operand; (4) check bits: only 16384/32768 left in the 16-bit check field → separate EVEX check field; (5) hflags: bit 31 was the last free in the high range — 2 used now; (6) AVX10.1 also enables the opmask forms (M5); (7) CD/VL need new UC_CTL_X86_AVX512 bits (M3); (8) no #AC anywhere in the fork (KMOV at CPL3 unaligned); (9) U121 comment in helper.c still says E3h/E7h "left to K".
      - ⬜ Decision for you (found by M1): legacy SSE NaN propagation — when both sources are NaN the fork returns the QNaN / larger significand; the SDM (Vol1 4.8.3.5) and the i5-13600K return SRC1 quieted (56 of 12992 hardware cases, SNaN in SRC1 + QNaN in SRC2). Fixing changes legacy SSE results (QEMU 11.1: float_2nan_prop_ab). EVEX inherits it; its cases avoid both-NaN lanes.
      - ⬜ Unicorn plain stores to a partly unmapped range write the mapped part (only the exit is requested); EVEX masked stores now probe first, VEX/legacy stores do not.
      - ⬜ M2 engine work: scalar merge from SRC1 (E3/E10), "masked lanes take SRC1" (VPBLENDM/VBLENDMP), FMA with dest as source under masking, T2/T4/T8 and Half/Quarter/Eighth-Mem masked loads, E*NF no-fault-suppression, gathers/scatters (VSIB, k cleared per element, E12 overlap over 32 regs), narrowing stores (VPMOV*), {sae} on compares into k.
      - ⬜ M2 instructions (rest of AVX512F): scalar FP (VADD…VSQRT SS/SD, VMOVSS/SD, VCOMIS/VUCOMIS, VCMPSS/SD), VCMPPS/PD → k, all FMA, conversions (DQ/UDQ/QQ↔PS/PD, CVTT*, PS↔PD, PH↔PS, SI/USI scalar, SS↔SD), shifts by xmm count, VPROLV/VPRORV, unpack/shuffle/permute (VPUNPCK*, VPSHUFD, VSHUFP*, VUNPCK*, VPERM*, VPERMI2/T2, VALIGND/Q, VPERMILP*, VSHUFF/I32X4/64X2), VPMOVZX/SX, VPMOV* narrowing, VPBLENDM*/VBLENDMP*, compress/expand, gathers/scatters, VMOVNT*/NTDQA/DDUP/SHDUP/SLDUP/D/Q/HLPS/LHPS/H/LPS/PD, VPINSR/EXTR D/Q, VINSERTPS/VEXTRACTPS, VINSERT/EXTRACT/BROADCAST F/I 32X4/64X4, VRCP14/VRSQRT14, VGETEXP/VGETMANT/VSCALEF/VFIXUPIMM/VRNDSCALE.
      - ⬜ EVEX outside 64-bit: decided EVEX is taken in 16-bit protected mode too (SDM exception tables cover protected mode); R'/B ignored, V'=0 #UD outside 64-bit.
      - ⬜ M1 notes from M0: gate EVEX on env->features AVX512F (set by UC_CTL_X86_AVX512) masked by the strict profile; extend UC_CTL_X86_AVX512 to a bitmask for CD/BW/DQ/VL (M3); EVEX sets PREFIX_VEX so U126 zeroing covers EVEX.128/256 register destinations, masking must merge before writeback; emu-alltest `--avx512` + `xcr0=` key.
    - ⬜ M2: rest of AVX512F (FP, FMA, conversions, permutes, compress/expand, gather/scatter, ternlog, getexp/getmant/scalef/fixupimm/rndscale/rcp14/rsqrt14).
    - ⬜ M3: AVX512VL gate, BW, DQ, CD.
    - ⬜ M4: VBMI/VBMI2, VNNI, BITALG, VPOPCNTDQ, IFMA, VP2INTERSECT, EVEX GFNI/VAES/VPCLMUL, BF16, FP16.
    - ⬜ M5: AVX10 (CPUID leaf 0x24, AVX10.2 instructions).
  - ⏳ 1.15e AVX10.x, AMX, APX — AMX (VEX) done; AVX10 after M1–M4; APX after the EVEX decoder.
      - ⬜ AMX leftovers: EVEX AMX-AVX512 (TCVTROWD2PS, TCVTROWPS2BF16H/L, TCVTROWPS2PHH/L, TILEMOVROW) after M1; APX-promoted tile loads/stores; AMX-FP8 (TDPBF8PS/TDPBHF8PS/TDPHBF8PS/TDPHF8PS); AMX-TF32 (TMMULTF32PS); AMX-MOVRS (TILELOADDRS/TILELOADDRST1); XSAVES/XRSTORS (fork has none); x86_cpuid_leaf_has_subleaves not updated for 1EH.
  - ⬜ 1.15f AMD/VIA-only forms (XOP, FMA4, TBM, 3DNow!, SSE4A, LWP, CLZERO, MONITORX, RDPRU, MCOMMIT, INVLPGB/TLBSYNC, VIA PadLock/ACE) — your decision 2026-10-07: implement, but only AFTER every Intel instruction (1.15a–e) is done; the host is Intel so these are tested against the AMD/VIA manuals only.
    - ⬜ New switch `__use_AMD_instruction_set__` (default 0 = Intel instruction set, like `__Use_Original_Qemu`): AMD/VIA-only instructions decode only when it is 1; with 0 they are #UD as on Intel (also re-gates what QEMU already has, e.g. 3DNow!, SSE4A, FEMMS).
    - ⬜ You download the handbooks into `emulator\Amd Handbooks\` (list given 2026-10-07): AMD APM Vol 2 #24593, Vol 3 #24594, Vol 4 #26568, Vol 5 #26569, LWP spec #43724, VIA PadLock Programming Guide (+ ACE/RNG/PHE docs).
    - ⬜ Then agents implement them family by family (manual first, then QEMU/asmjit/XED), each with expected-value cases.
      - ⬜ Key Locker leftovers: KeySource 1 (random IWKey), IWKeyBackup MSRs, MSR_FEATURE_CONFIG gate, AESKLE = 0 in SMM.
      - ⬜ MOVRS leftovers: EVEX VMOVRSB/W/D/Q (AVX10) and AMX-MOVRS.
      - ⬜ UINTR leftovers (no local APIC in Unicorn): IPIs to other APIC IDs / other vectors dropped, notification with IF=0 dropped instead of pending, x2APIC, XSAVES user-interrupt state, CET effects, STI/MOV SS shadow distinction.
      - ⬜ Fixes leftovers: LOCK 0F 0D: CPU #UD for every /r except /1 (PREFETCHW runs with LOCK), Unicorn runs all (14 forms); MPX bound-directory base uses BNDCFG[63:20] (SDM: [63:12]); VEX in 16-bit protected-mode code segments not decoded (SDM: only real/V86 #UD); 32-bit-mode hardware cases impossible in emu-alltest (64-bit snippets only); VEX.W in 32-bit mode for GPR forms untested; U129 KMOV 32-bit GPR mask now redundant.
      - ⬜ CET leftovers: shadow stack/IBT on far CALL/RET, interrupts/exceptions, IRET, SYSCALL/SYSRET/SYSENTER/SYSEXIT, task switch; XSAVES CET_U/CET_S components; PKS ignored by the page walker.
      - ⬜ SGX model ("present but disabled" → ENCLU #GP at CPL3); PCONFIG needs CPUID leaf 1BH (raise MAX level — your decision); GETSEC leaves beyond CAPABILITIES need a TXT chipset model.
      - ⬜ Harness: hardware case files must run with `--strict` (non-strict MAX now runs TSX/WAITPKG/ENQCMD where the CPU #UDs); hwcheck_gate1 too (done 2026-10-08: 6 known diffs).

## Latest ledger entries (`CHANGES_LEDGER.md`, 153 rows)

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
