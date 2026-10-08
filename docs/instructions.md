# Instruction support — index

_Generated 2026-10-08 01:33 (HEAD `3c49cf2 EVEX form table data: evex_forms.tsv, conflicts, APX list, draft .inc, summary`); refreshed every 30 minutes while work is in progress._

- [Intel instruction sets supported](Intel_instruction_sets_supported.md)
- [AMD / VIA instruction sets](AMD_instruction_sets_supported.md)

**Legend:** ✅ runs on our i5-13600K and the emulator is identical to it · ⏳ runs on our CPU, the emulator has an open item · ⬜ not implemented / not verified yet · **❌ NOT SUPPORTED on our i5-13600K** (the CPU cannot execute it, so it can never be checked against our hardware; it is still implemented from the manual where possible — the row says "implemented per the manual" or "not implemented yet").

**All forms:** ✅ 1173 · ⏳ 132 · ⬜ 0 · ❌ 1606 (implemented per the manual 82, open item 76, not implemented yet 1448)

## Currently being added

    - ⏳ When the 1.15c branches land: flip their `verified_forms.tsv` rows from "ud" to "sdm" (done for SHA512/SM3/SM4) and run `cases_reach.txt` with `--strict` (done).
    - ⬜ WRUSSD/WRUSSQ: #GP at CPL3 (Windows enables CET) — Phase 2 CPL3 work (D6).
  - ⏳ 1.15c small Intel extensions — merged so far: SHA512/SM3/SM4 (U82–U84), AVX-VNNI-INT8/INT16, AVX-IFMA, AVX-NE-CONVERT (U85–U88), Key Locker, RAO-INT, MOVRS, USER_MSR, UINTR, LOCK 0F 18 (U100–U105), TSX, WAITPKG, ENQCMD, GETSEC/PCONFIG/SGX faults, CET shadow stack + IBT + shadow-stack paging (U110–U117); CPL0-only ones = exact CPL3 faults per D6.
  - ⏳ 1.15d EVEX / AVX-512 — design done (`emulator\EVEX_DESIGN.md`, 2026-10-08): 2537 EVEX forms (AVX512F 1071, BW 250, DQ 126, CD 18, FP16 359, AVX10.2 ~410, others ~130) + 51 opmask forms; state (ZMM 32x512, k0–7) already in CPUX86State.
      - ⬜ M0 leftovers: Haswell-class models without XSAVEC still report non-zero 0DH.1 EBX (SDM: 0) — kept for an existing test, revisit; profile 0DH.1 EBX stays as captured (capture machine IA32_XSS share unknown).
    - ⏳ K [agent, wt/evex_k, U127–U139]: 51 opmask instructions (KAND…KUNPCK, KMOV, KTEST/KORTEST, KSHIFT, KADD), X86_OP_KREG / X86_TYPE_K, opmask enable = CR4.OSXSAVE && XCR0 & E3h (separate hflag), K20/K21 #UD rules; XSAVE component 5 already done in M0.
    - ⬜ M1: EVEX prefix (62), 32 registers, disp8*N, masking/zeroing, masked memory (fault suppression), broadcast, {er}/{sae}, validate_evex, SHIFT 3 helpers, asmjit-driven EVEX table generator, first instructions (VMOVDQU/A32/64, VMOVUPS/APS, VPADDD/Q, VPANDD/Q, VADDPS/PD, VPCMPD→k, VPBROADCASTD).
      - ⬜ M1 notes from M0: gate EVEX on env->features AVX512F (set by UC_CTL_X86_AVX512) masked by the strict profile; extend UC_CTL_X86_AVX512 to a bitmask for CD/BW/DQ/VL (M3); EVEX sets PREFIX_VEX so U126 zeroing covers EVEX.128/256 register destinations, masking must merge before writeback; emu-alltest `--avx512` + `xcr0=` key.
    - ⬜ M2: rest of AVX512F (FP, FMA, conversions, permutes, compress/expand, gather/scatter, ternlog, getexp/getmant/scalef/fixupimm/rndscale/rcp14/rsqrt14).
    - ⬜ M3: AVX512VL gate, BW, DQ, CD.
    - ⬜ M4: VBMI/VBMI2, VNNI, BITALG, VPOPCNTDQ, IFMA, VP2INTERSECT, EVEX GFNI/VAES/VPCLMUL, BF16, FP16.
    - ⬜ M5: AVX10 (CPUID leaf 0x24, AVX10.2 instructions).
  - ⬜ 1.15e AVX10.x, AMX, APX.
  - ⏳ Agent wrap-up (your instruction 2026-10-07): when an agent finishes, it reports what it implemented and what is left to implement → merged here as plan steps; its worktree is then moved (`git worktree move`) to `NoVmp\pending-deletion\wt_<name>` for you to delete manually (then `git worktree prune`). Ready to delete now: wt_harness, wt_reach, wt_sha_sm, wt_vnni_ifma, wt_evex_m0, wt_keylocker, wt_tsx_cet, wt_evex_gen.
  - ⬜ 1.15f AMD/VIA-only forms (XOP, FMA4, TBM, 3DNow!, SSE4A, LWP, CLZERO, MONITORX, RDPRU, MCOMMIT, INVLPGB/TLBSYNC, VIA PadLock/ACE) — your decision 2026-10-07: implement, but only AFTER every Intel instruction (1.15a–e) is done; the host is Intel so these are tested against the AMD/VIA manuals only.
    - ⬜ New switch `__use_AMD_instruction_set__` (default 0 = Intel instruction set, like `__Use_Original_Qemu`): AMD/VIA-only instructions decode only when it is 1; with 0 they are #UD as on Intel (also re-gates what QEMU already has, e.g. 3DNow!, SSE4A, FEMMS).
    - ⬜ You download the handbooks into `emulator\Amd Handbooks\` (list given 2026-10-07): AMD APM Vol 2 #24593, Vol 3 #24594, Vol 4 #26568, Vol 5 #26569, LWP spec #43724, VIA PadLock Programming Guide (+ ACE/RNG/PHE docs).
    - ⬜ Then agents implement them family by family (manual first, then QEMU/asmjit/XED), each with expected-value cases.
      - ⬜ Key Locker leftovers: KeySource 1 (random IWKey), IWKeyBackup MSRs, MSR_FEATURE_CONFIG gate, AESKLE = 0 in SMM.
      - ⬜ MOVRS leftovers: EVEX VMOVRSB/W/D/Q (AVX10) and AMX-MOVRS.
      - ⬜ UINTR leftovers (no local APIC in Unicorn): IPIs to other APIC IDs / other vectors dropped, notification with IF=0 dropped instead of pending, x2APIC, XSAVES user-interrupt state, CET effects, STI/MOV SS shadow distinction.
      - ⬜ Found, not fixed: LOCK on the multi-byte NOP opcodes 0F 19 / 0F 1C–0F 1F runs in Unicorn but #UD on the i5-13600K (same fix as U105).
      - ⬜ Investigate: after some #UD stops, rewriting code at an address that already ran made the next uc_emu_start report the old #UD once (stale TB?).
      - ⬜ CET leftovers: shadow stack/IBT on far CALL/RET, interrupts/exceptions, IRET, SYSCALL/SYSRET/SYSENTER/SYSEXIT, task switch; XSAVES CET_U/CET_S components; PKS ignored by the page walker.
      - ⬜ SGX model ("present but disabled" → ENCLU #GP at CPL3); PCONFIG needs CPUID leaf 1BH (raise MAX level — your decision); GETSEC leaves beyond CAPABILITIES need a TXT chipset model.
      - ⬜ Harness: hardware case files must run with `--strict` (non-strict MAX now runs TSX/WAITPKG/ENQCMD where the CPU #UDs); hwcheck_gate1 too (done 2026-10-08: 6 known diffs).

## Latest ledger entries (`CHANGES_LEDGER.md`, 110 rows)

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
