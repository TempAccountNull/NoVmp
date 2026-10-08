# Intel instruction sets supported by the NoVmp emulator

_Generated 2026-10-08 03:53 from `Emulator/tools/isa/gen_status_docs.py` (HEAD `25fd878 U95: keep EVEX.V' in 64-bit mode (U93 cut VEX.vvvv to 4 bits in every mode)`). Do not edit by hand._

**Legend:** ✅ runs on our i5-13600K and the emulator is identical to it · ⏳ runs on our CPU, the emulator has an open item · ⬜ not implemented / not verified yet · **❌ NOT SUPPORTED on our i5-13600K** (the CPU cannot execute it, so it can never be checked against our hardware; it is still implemented from the manual where possible — the row says "implemented per the manual" or "not implemented yet").

Source of truth: every instruction form of the Intel SDM / XED list (`Emulator/data/isa_manual_forms.tsv`), checked against an Intel i5-13600K (Raptor Lake) with `emu-alltest` (hardware sweeps, `--cases` files) and, for instructions this CPU lacks, against expected values derived from the SDM pseudocode.

**Totals (Intel families):** ✅ 1172 · ⏳ 130 · ⬜ 0 · ❌ 1380 (of which implemented per the manual 222, open item 57, not implemented yet 1101) — 2682 forms

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

## By family

| family | forms | ✅ | ⏳ | ⬜ | ❌ implemented per manual | ❌ open item | ❌ not implemented yet |
|---|---|---|---|---|---|---|---|
| ? | 17 | 17 | 0 | 0 | 0 | 0 | 0 |
| ADOX_ADCX | 2 | 0 | 2 | 0 | 0 | 0 | 0 |
| AES | 6 | 0 | 6 | 0 | 0 | 0 | 0 |
| AVX | 381 | 381 | 0 | 0 | 0 | 0 | 0 |
| AVX2 | 20 | 20 | 0 | 0 | 0 | 0 | 0 |
| AVX2GATHER | 8 | 8 | 0 | 0 | 0 | 0 | 0 |
| AVXAES | 6 | 6 | 0 | 0 | 0 | 0 | 0 |
| AVX_GFNI | 3 | 3 | 0 | 0 | 0 | 0 | 0 |
| AVX_IFMA | 2 | 0 | 0 | 0 | 2 | 0 | 0 |
| AVX_NE_CONVERT | 7 | 0 | 0 | 0 | 7 | 0 | 0 |
| AVX_VNNI | 4 | 4 | 0 | 0 | 0 | 0 | 0 |
| AVX_VNNI_INT16 | 6 | 0 | 0 | 0 | 6 | 0 | 0 |
| AVX_VNNI_INT8 | 6 | 0 | 0 | 0 | 6 | 0 | 0 |
| BMI1 | 6 | 6 | 0 | 0 | 0 | 0 | 0 |
| BMI2 | 8 | 8 | 0 | 0 | 0 | 0 | 0 |
| CET | 14 | 4 | 0 | 0 | 8 | 2 | 0 |
| CLDEMOTE | 1 | 0 | 0 | 0 | 1 | 0 | 0 |
| CLFLUSHOPT | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| CLFSH | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| CLWB | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| CMOV | 16 | 16 | 0 | 0 | 0 | 0 | 0 |
| CMPCCXADD | 22 | 0 | 0 | 0 | 0 | 22 | 0 |
| CMPXCHG16B | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| ENQCMD | 2 | 0 | 0 | 0 | 1 | 1 | 0 |
| F16C | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| FAT_NOP | 8 | 8 | 0 | 0 | 0 | 0 | 0 |
| FCMOV | 9 | 9 | 0 | 0 | 0 | 0 | 0 |
| FCOMI | 6 | 6 | 0 | 0 | 0 | 0 | 0 |
| FMA | 60 | 60 | 0 | 0 | 0 | 0 | 0 |
| FRED | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| FXSAVE | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| FXSAVE64 | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| GFNI | 3 | 3 | 0 | 0 | 0 | 0 | 0 |
| HLE | 2 | 0 | 0 | 0 | 2 | 0 | 0 |
| HRESET | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| I186 | 19 | 12 | 7 | 0 | 0 | 0 | 0 |
| I286PROTECTED | 9 | 5 | 4 | 0 | 0 | 0 | 0 |
| I286REAL | 7 | 0 | 7 | 0 | 0 | 0 | 0 |
| I386 | 47 | 41 | 6 | 0 | 0 | 0 | 0 |
| I486 | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| I486REAL | 7 | 4 | 3 | 0 | 0 | 0 | 0 |
| I86 | 88 | 48 | 40 | 0 | 0 | 0 | 0 |
| IBHF | 1 | 0 | 0 | 0 | 1 | 0 | 0 |
| ICACHE_PREFETCH | 2 | 0 | 0 | 0 | 2 | 0 | 0 |
| INVPCID | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| KEYLOCKER | 7 | 0 | 0 | 0 | 6 | 1 | 0 |
| KEYLOCKER_WIDE | 4 | 0 | 0 | 0 | 4 | 0 | 0 |
| LAHF | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| LKGS | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| LONGMODE | 14 | 8 | 6 | 0 | 0 | 0 | 0 |
| MONITOR | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| MOVBE | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| MOVDIR64B | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| MOVDIRI | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| MOVRS | 2 | 0 | 0 | 0 | 2 | 0 | 0 |
| MPX | 7 | 0 | 0 | 0 | 7 | 0 | 0 |
| MSRLIST | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| MSR_IMM | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| PAUSE | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| PBNDKB | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| PCLMULQDQ | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| PCONFIG | 1 | 0 | 0 | 0 | 1 | 0 | 0 |
| PENTIUMMMX | 60 | 60 | 0 | 0 | 0 | 0 | 0 |
| PENTIUMREAL | 4 | 1 | 3 | 0 | 0 | 0 | 0 |
| PKU | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| POPCNT | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| PPRO | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| PPRO_UD0_LONG | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| PREFETCHWT1 | 1 | 0 | 0 | 0 | 1 | 0 | 0 |
| PREFETCH_NOP | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| PTWRITE | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| RAO_INT | 4 | 0 | 0 | 0 | 4 | 0 | 0 |
| RDPID | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| RDPMC | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| RDRAND | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| RDSEED | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| RDTSCP | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| RDWRFSGS | 4 | 1 | 3 | 0 | 0 | 0 | 0 |
| RTM | 4 | 0 | 0 | 0 | 0 | 4 | 0 |
| SEP | 3 | 0 | 3 | 0 | 0 | 0 | 0 |
| SERIALIZE | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| SGX | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| SGX_ENCLV | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| SHA | 7 | 0 | 7 | 0 | 0 | 0 | 0 |
| SHA512 | 3 | 0 | 0 | 0 | 3 | 0 | 0 |
| SM3 | 3 | 0 | 0 | 0 | 3 | 0 | 0 |
| SM4 | 4 | 0 | 0 | 0 | 2 | 0 | 2 |
| SMAP | 2 | 0 | 2 | 0 | 0 | 0 | 0 |
| SMX | 1 | 0 | 0 | 0 | 1 | 0 | 0 |
| SSE | 110 | 110 | 0 | 0 | 0 | 0 | 0 |
| SSE2 | 129 | 129 | 0 | 0 | 0 | 0 | 0 |
| SSE3 | 10 | 10 | 0 | 0 | 0 | 0 | 0 |
| SSE3X87 | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| SSE4 | 48 | 44 | 4 | 0 | 0 | 0 | 0 |
| SSE42 | 6 | 5 | 1 | 0 | 0 | 0 | 0 |
| SSEMXCSR | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| SSE_PREFETCH | 4 | 4 | 0 | 0 | 0 | 0 | 0 |
| SSSE3 | 16 | 14 | 2 | 0 | 0 | 0 | 0 |
| TDX | 4 | 0 | 0 | 0 | 0 | 4 | 0 |
| TSX_LDTRK | 2 | 0 | 0 | 0 | 2 | 0 | 0 |
| UINTR | 5 | 0 | 0 | 0 | 5 | 0 | 0 |
| USER_MSR | 4 | 0 | 0 | 0 | 4 | 0 | 0 |
| VMFUNC | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| VTX | 12 | 0 | 12 | 0 | 0 | 0 | 0 |
| WAITPKG | 3 | 0 | 0 | 0 | 0 | 3 | 0 |
| WBNOINVD | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| WRMSRNS | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| X87 | 79 | 79 | 0 | 0 | 0 | 0 | 0 |
| XSAVE | 6 | 5 | 1 | 0 | 0 | 0 | 0 |
| XSAVEC | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| XSAVEOPT | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| XSAVES | 4 | 0 | 4 | 0 | 0 | 0 | 0 |
| AMX_AVX512 | 5 | 0 | 0 | 0 | 0 | 0 | 5 |
| AMX_BF16 | 1 | 0 | 0 | 0 | 1 | 0 | 0 |
| AMX_COMPLEX | 2 | 0 | 0 | 0 | 2 | 0 | 0 |
| AMX_FP16 | 1 | 0 | 0 | 0 | 1 | 0 | 0 |
| AMX_FP8 | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| AMX_INT8 | 4 | 0 | 0 | 0 | 4 | 0 | 0 |
| AMX_MOVRS | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| AMX_TILE | 3 | 0 | 0 | 0 | 3 | 0 | 0 |
| AMX_TILE_BASE | 4 | 0 | 0 | 0 | 4 | 0 | 0 |
| APX_F | 33 | 0 | 0 | 0 | 0 | 0 | 33 |
| APX_F_ADX | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| APX_F_AMX | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| APX_F_AMX_BASE | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| APX_F_AMX_MOVRS | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| APX_F_BMI1 | 6 | 0 | 0 | 0 | 0 | 0 | 6 |
| APX_F_BMI2 | 8 | 0 | 0 | 0 | 0 | 0 | 8 |
| APX_F_CET | 4 | 0 | 0 | 0 | 0 | 2 | 2 |
| APX_F_CMPCCXADD | 22 | 0 | 0 | 0 | 0 | 0 | 22 |
| APX_F_ENQCMD | 2 | 0 | 0 | 0 | 0 | 1 | 1 |
| APX_F_INVPCID | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| APX_F_LZCNT | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| APX_F_MOVBE | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| APX_F_MOVDIR64B | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| APX_F_MOVDIRI | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| APX_F_MOVRS | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| APX_F_MSR_IMM | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| APX_F_N3 | 84 | 0 | 0 | 0 | 0 | 0 | 84 |
| APX_F_POPCNT | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| APX_F_RAO_INT | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| APX_F_USER_MSR | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| APX_F_VMX | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| AVX10_2_BF16 | 29 | 0 | 0 | 0 | 0 | 0 | 29 |
| AVX10_MOVRS | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| AVX10_V2_AUX | 21 | 0 | 0 | 0 | 0 | 0 | 21 |
| AVX512BW | 112 | 0 | 0 | 0 | 26 | 0 | 86 |
| AVX512CD | 6 | 0 | 0 | 0 | 0 | 0 | 6 |
| AVX512DQ | 69 | 0 | 0 | 0 | 14 | 0 | 55 |
| AVX512ER | 10 | 0 | 0 | 0 | 0 | 0 | 10 |
| AVX512F | 479 | 0 | 0 | 0 | 86 | 0 | 393 |
| AVX512PF | 16 | 0 | 0 | 0 | 0 | 0 | 16 |
| AVX512_4FMAPS | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| AVX512_4VNNIW | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| AVX512_BF16 | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| AVX512_BITALG | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| AVX512_COM_EF | 6 | 0 | 0 | 0 | 0 | 0 | 6 |
| AVX512_FP16 | 170 | 0 | 0 | 0 | 0 | 0 | 170 |
| AVX512_FP16_CONVERT | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| AVX512_FP8_CONVERT | 13 | 0 | 0 | 0 | 0 | 0 | 13 |
| AVX512_GFNI | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| AVX512_IFMA | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| AVX512_MEDIAX | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| AVX512_MINMAX | 7 | 0 | 0 | 0 | 0 | 0 | 7 |
| AVX512_SAT_CVT | 12 | 0 | 0 | 0 | 0 | 0 | 12 |
| AVX512_SAT_CVT_DS | 12 | 0 | 0 | 0 | 0 | 0 | 12 |
| AVX512_VAES | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| AVX512_VBMI | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| AVX512_VBMI2 | 16 | 0 | 0 | 0 | 0 | 0 | 16 |
| AVX512_VNNI | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| AVX512_VNNI_FP16 | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| AVX512_VNNI_INT16 | 6 | 0 | 0 | 0 | 0 | 0 | 6 |
| AVX512_VNNI_INT8 | 6 | 0 | 0 | 0 | 0 | 0 | 6 |
| AVX512_VP2INTERSECT | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| AVX512_VPCLMULQDQ | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| AVX512_VPOPCNTDQ | 2 | 0 | 0 | 0 | 0 | 0 | 2 |

## Per instruction

<details><summary><b>?</b> (17 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | ADDR32 | legacy | - | identical to the i5-13600K (cases_reach): 67 prefix (LEA eax, [esi]) |
| ✅ | BND | legacy | - | identical to the i5-13600K (cases_reach): F2 on branches: plain branch without MPX |
| ✅ | DATA16 | legacy | - | identical to the i5-13600K (cases_reach): 66 prefix |
| ✅ | FCLEX | legacy | - | identical to the i5-13600K (cases_reach): 9B DB E2 (FWAIT + FNCLEX; pending #MF with CR0.NE) |
| ✅ | FINIT | legacy | - | identical to the i5-13600K (cases_reach): 9B DB E3 (FWAIT + FNINIT) |
| ✅ | FSAVE | legacy | - | U61-U64 |
| ✅ | FSTCW | legacy | - | identical to the i5-13600K (cases_reach): 9B D9 /7 (FWAIT + FNSTCW) |
| ✅ | FSTENV | legacy | - | U61/U62/U64 |
| ✅ | FSTSW | legacy | - | identical to the i5-13600K (cases_reach): 9B DD /7, 9B DF E0 (FWAIT + FNSTSW; FIP per U90) |
| ✅ | LOCK | legacy | - | identical to the i5-13600K (cases_reach): F0 prefix (memory: locked; register: #UD) |
| ✅ | NOTRACK | legacy | - | identical to the i5-13600K (cases_reach): 3E on indirect JMP/CALL: ignored (IBT not enabled) |
| ✅ | REP | legacy | - | identical to the i5-13600K (cases_reach): F3 prefix (string ops) |
| ✅ | REPE | legacy | - | identical to the i5-13600K (cases_reach): F3 prefix (CMPS/SCAS) |
| ✅ | REPNE | legacy | - | identical to the i5-13600K (cases_reach): F2 prefix (CMPS/SCAS) |
| ✅ | REPNZ | legacy | - | identical to the i5-13600K (cases_reach): F2 prefix (CMPS/SCAS) |
| ✅ | REPZ | legacy | - | identical to the i5-13600K (cases_reach): F3 prefix (CMPS/SCAS) |
| ✅ | REX64 | legacy | - | identical to the i5-13600K (cases_reach): REX.W prefix |

</details>

<details><summary><b>ADOX_ADCX</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | ADCX | legacy | - | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 2 identical |
| ⏳ | ADOX | legacy | - | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 2 identical |

</details>

<details><summary><b>AES</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | AESDEC | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | AESDECLAST | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | AESENC | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | AESENCLAST | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | AESIMC | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | AESKEYGENASSIST | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |

</details>

<details><summary><b>AVX</b> (381 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | VADDPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VADDPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VADDSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VADDSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VADDSUBPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VADDSUBPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VANDNPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VANDNPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VANDPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VANDPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VBLENDPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VBLENDPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VBLENDVPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VBLENDVPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VBROADCASTF128 | vex | 256 | identical to the i5-13600K (1 forms) |
| ✅ | VBROADCASTSD | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VBROADCASTSS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCMPEQ_OSPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPEQ_OSPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPEQ_OSSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPEQ_OSSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPEQ_UQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPEQ_UQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPEQ_UQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPEQ_UQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPEQ_USPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPEQ_USPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPEQ_USSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPEQ_USSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPEQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPEQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPEQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPEQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPFALSE_OSPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPFALSE_OSPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPFALSE_OSSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPFALSE_OSSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPFALSEPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPFALSEPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPFALSESD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPFALSESS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPGE_OQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPGE_OQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPGE_OQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPGE_OQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPGEPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPGEPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPGESD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPGESS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPGT_OQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPGT_OQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPGT_OQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPGT_OQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPGTPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPGTPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPGTSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPGTSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPLE_OQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPLE_OQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPLE_OQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPLE_OQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPLEPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPLEPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPLESD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPLESS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPLT_OQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPLT_OQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPLT_OQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPLT_OQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPLTPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPLTPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPLTSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPLTSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNEQ_OQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNEQ_OQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNEQ_OQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNEQ_OQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNEQ_OSPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNEQ_OSPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNEQ_OSSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNEQ_OSSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNEQ_USPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNEQ_USPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNEQ_USSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNEQ_USSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNEQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNEQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNEQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNEQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNGE_UQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNGE_UQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNGE_UQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNGE_UQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNGEPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNGEPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNGESD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNGESS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNGT_UQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNGT_UQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNGT_UQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNGT_UQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNGTPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNGTPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNGTSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNGTSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNLE_UQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNLE_UQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNLE_UQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNLE_UQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNLEPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNLEPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNLESD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNLESS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNLT_UQPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNLT_UQPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNLT_UQSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNLT_UQSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPNLTPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPNLTPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPNLTSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPNLTSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPORD_SPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPORD_SPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPORD_SSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPORD_SSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPORDPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPORDPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPORDSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPORDSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCMPPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCMPSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VCMPSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VCMPTRUE_USPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPTRUE_USPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPTRUE_USSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPTRUE_USSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPTRUEPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPTRUEPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPTRUESD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPTRUESS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPUNORD_SPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPUNORD_SPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPUNORD_SSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPUNORD_SSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCMPUNORDPD | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |
| ✅ | VCMPUNORDPS | vex | 128/256 | identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |
| ✅ | VCMPUNORDSD | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |
| ✅ | VCMPUNORDSS | vex | 128 | identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |
| ✅ | VCOMISD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VCOMISS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VCVTDQ2PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTDQ2PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTPD2DQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTPD2PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTPS2DQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTPS2PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTSD2SI | vex | 128 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTSD2SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VCVTSI2SD | vex | 128 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTSI2SS | vex | 128 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTSS2SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VCVTSS2SI | vex | 128 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTTPD2DQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTTPS2DQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTTSD2SI | vex | 128 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTTSS2SI | vex | 128 | identical to the i5-13600K (4 forms) |
| ✅ | VDIVPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VDIVPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VDIVSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VDIVSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VDPPD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VDPPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VEXTRACTF128 | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VEXTRACTPS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VHADDPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VHADDPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VHSUBPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VHSUBPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VINSERTF128 | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VINSERTPS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VLDDQU | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VLDMXCSR | vex | - | identical to the i5-13600K (1 forms) |
| ✅ | VMASKMOVDQU | vex | 128 | identical to the i5-13600K (1 forms) |
| ✅ | VMASKMOVPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMASKMOVPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMAXPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMAXPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMAXSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMAXSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMINPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMINPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMINSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMINSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVAPD | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VMOVAPS | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VMOVD | vex | 128 | identical to the i5-13600K (4 forms) |
| ✅ | VMOVDDUP | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMOVDQA | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VMOVDQU | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VMOVHLPS | vex | 128 | identical to the i5-13600K (1 forms) |
| ✅ | VMOVHPD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVHPS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVLHPS | vex | 128 | identical to the i5-13600K (1 forms) |
| ✅ | VMOVLPD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVLPS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVMSKPD | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVMSKPS | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVNTDQ | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVNTDQA | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVNTPD | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVNTPS | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VMOVQ | vex | 128 | identical to the i5-13600K (5 forms) |
| ✅ | VMOVSD | vex | 128 | identical to the i5-13600K (3 forms) |
| ✅ | VMOVSHDUP | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMOVSLDUP | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMOVSS | vex | 128 | identical to the i5-13600K (3 forms) |
| ✅ | VMOVUPD | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VMOVUPS | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VMPSADBW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMULPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMULPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VMULSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VMULSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VORPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VORPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPABSB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPABSD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPABSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPACKSSDW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPACKSSWB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPACKUSDW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPACKUSWB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPADDB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPADDD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPADDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPADDSB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPADDSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPADDUSB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPADDUSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPADDW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPALIGNR | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPAND | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPANDN | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPAVGB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPAVGW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPBLENDVB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPBLENDW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCLMULQDQ | vex | 128/256 | U69 (VEX.128/256) |
| ✅ | VPCMPEQB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCMPEQD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCMPEQQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCMPEQW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCMPESTRI | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPCMPESTRM | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPCMPGTB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCMPGTD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCMPGTQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCMPGTW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPCMPISTRI | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPCMPISTRM | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPERM2F128 | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VPERMILPD | vex | 128/256 | identical to the i5-13600K (8 forms) |
| ✅ | VPERMILPS | vex | 128/256 | identical to the i5-13600K (8 forms) |
| ✅ | VPEXTRB | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPEXTRD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPEXTRQ | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPEXTRW | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPHADDD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPHADDSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPHADDW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPHMINPOSUW | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPHSUBD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPHSUBSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPHSUBW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPINSRB | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPINSRD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPINSRQ | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPINSRW | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VPMADDUBSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMADDWD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMAXSB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMAXSD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMAXSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMAXUB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMAXUD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMAXUW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMINSB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMINSD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMINSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMINUB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMINUD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMINUW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVMSKB | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VPMOVSXBD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVSXBQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVSXBW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVSXDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVSXWD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVSXWQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVZXBD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVZXBQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVZXBW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVZXDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVZXWD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMOVZXWQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMULDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMULHRSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMULHUW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMULHW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMULLD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMULLW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMULUDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPOR | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSADBW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSHUFB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSHUFD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSHUFHW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSHUFLW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSIGNB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSIGND | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSIGNW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSLLD | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VPSLLDQ | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VPSLLQ | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VPSLLW | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VPSRAD | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VPSRAW | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VPSRLD | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VPSRLDQ | vex | 128/256 | identical to the i5-13600K (2 forms) |
| ✅ | VPSRLQ | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VPSRLW | vex | 128/256 | identical to the i5-13600K (6 forms) |
| ✅ | VPSUBB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSUBD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSUBQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSUBSB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSUBSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSUBUSB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSUBUSW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSUBW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPTEST | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPUNPCKHBW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPUNPCKHDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPUNPCKHQDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPUNPCKHWD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPUNPCKLBW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPUNPCKLDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPUNPCKLQDQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPUNPCKLWD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPXOR | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VRCPPS | vex | 128/256 | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| ✅ | VRCPSS | vex | 128 | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| ✅ | VROUNDPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VROUNDPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VROUNDSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VROUNDSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VRSQRTPS | vex | 128/256 | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| ✅ | VRSQRTSS | vex | 128 | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| ✅ | VSHUFPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VSHUFPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VSQRTPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VSQRTPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VSQRTSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VSQRTSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VSTMXCSR | vex | - | identical to the i5-13600K (1 forms) |
| ✅ | VSUBPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VSUBPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VSUBSD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VSUBSS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VTESTPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VTESTPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VUCOMISD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VUCOMISS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VUNPCKHPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VUNPCKHPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VUNPCKLPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VUNPCKLPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VXORPD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VXORPS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VZEROALL | vex | - | identical to the i5-13600K (1 forms) |
| ✅ | VZEROUPPER | vex | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>AVX2</b> (20 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | VBROADCASTI128 | vex | 256 | identical to the i5-13600K (1 forms) |
| ✅ | VEXTRACTI128 | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VINSERTI128 | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VPBLENDD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPBROADCASTB | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPBROADCASTD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPBROADCASTQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPBROADCASTW | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPERM2I128 | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VPERMD | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VPERMPD | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VPERMPS | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VPERMQ | vex | 256 | identical to the i5-13600K (2 forms) |
| ✅ | VPMASKMOVD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPMASKMOVQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSLLVD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSLLVQ | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSRAVD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSRLVD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VPSRLVQ | vex | 128/256 | identical to the i5-13600K (4 forms) |

</details>

<details><summary><b>AVX2GATHER</b> (8 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | VGATHERDPD | vex | 128/256 | identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| ✅ | VGATHERDPS | vex | 128/256 | identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| ✅ | VGATHERQPD | vex | 128/256 | identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| ✅ | VGATHERQPS | vex | 128/256 | identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| ✅ | VPGATHERDD | vex | 128/256 | identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| ✅ | VPGATHERDQ | vex | 128/256 | identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| ✅ | VPGATHERQD | vex | 128/256 | identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| ✅ | VPGATHERQQ | vex | 128/256 | identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |

</details>

<details><summary><b>AVXAES</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | VAESDEC | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VAESDECLAST | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VAESENC | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VAESENCLAST | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VAESIMC | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VAESKEYGENASSIST | vex | 128 | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>AVX_GFNI</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | VGF2P8AFFINEINVQB | vex | 128/256 | U70 (VEX); EVEX not implemented |
| ✅ | VGF2P8AFFINEQB | vex | 128/256 | U70 (VEX); EVEX not implemented |
| ✅ | VGF2P8MULB | vex | 128/256 | U70 (VEX); EVEX not implemented |

</details>

<details><summary><b>AVX_IFMA</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPMADD52HUQ | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[23] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | VPMADD52LUQ | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[23] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |

</details>

<details><summary><b>AVX_NE_CONVERT</b> (7 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VBCSTNEBF162PS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VBCSTNESH2PS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VCVTNEEBF162PS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VCVTNEEPH2PS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VCVTNEOBF162PS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VCVTNEOPH2PS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VCVTNEPS2BF16 | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |

</details>

<details><summary><b>AVX_VNNI</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | VPDPBUSD | vex | 128/256 | U71 AVX-VNNI (VEX) |
| ✅ | VPDPBUSDS | vex | 128/256 | U71 |
| ✅ | VPDPWSSD | vex | 128/256 | U71 |
| ✅ | VPDPWSSDS | vex | 128/256 | U71 |

</details>

<details><summary><b>AVX_VNNI_INT16</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPDPWSUD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[10] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | VPDPWSUDS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[10] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | VPDPWUSD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[10] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | VPDPWUSDS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[10] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | VPDPWUUD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[10] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | VPDPWUUDS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[10] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |

</details>

<details><summary><b>AVX_VNNI_INT8</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPDPBSSD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[4] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VPDPBSSDS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[4] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VPDPBSUD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[4] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VPDPBSUDS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[4] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VPDPBUUD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[4] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VPDPBUUDS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[4] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |

</details>

<details><summary><b>BMI1</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | ANDN | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | BEXTR | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | BLSI | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | BLSMSK | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | BLSR | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | TZCNT | legacy | - | identical to the i5-13600K (4 forms) |

</details>

<details><summary><b>BMI2</b> (8 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | BZHI | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | MULX | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | PDEP | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | PEXT | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | RORX | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | SARX | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | SHLX | vex | - | identical to the i5-13600K (4 forms) |
| ✅ | SHRX | vex | - | identical to the i5-13600K (4 forms) |

</details>

<details><summary><b>CET</b> (14 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | CLRSSBSY | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual (SDM-vector… |
| ✅ | ENDBR32 | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | ENDBR64 | legacy | - | identical to the i5-13600K (1 forms) |
| ❌ | INCSSPD | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual (SDM-vector… |
| ❌ | INCSSPQ | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual (SDM-vector… |
| ✅ | RDSSPD | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | RDSSPQ | legacy | - | identical to the i5-13600K (1 forms) |
| ❌ | RSTORSSP | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual (SDM-vector… |
| ❌ | SAVEPREVSSP | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual (SDM-vector… |
| ❌ | SETSSBSY | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual (SDM-vector… |
| ❌ | WRSSD | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual (SDM-vector… |
| ❌ | WRSSQ | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual (SDM-vector… |
| ❌ | WRUSSD | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual, open item:… |
| ❌ | WRUSSQ | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) — implemented per the manual, open item:… |

</details>

<details><summary><b>CLDEMOTE</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | CLDEMOTE | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[25] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>CLFLUSHOPT</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | CLFLUSHOPT | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>CLFSH</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | CLFLUSH | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>CLWB</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | CLWB | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>CMOV</b> (16 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | CMOVA | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVAE | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVB | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVBE | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVE | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVG | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVGE | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVL | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVLE | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVNE | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVNO | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVNP | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVNS | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVO | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVP | legacy | - | identical to the i5-13600K (6 forms) |
| ✅ | CMOVS | legacy | - | identical to the i5-13600K (6 forms) |

</details>

<details><summary><b>CMPCCXADD</b> (22 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | CMPAEXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPAXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPBEXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPBXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPEXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPGEXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPGXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPLEXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPLXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNBEXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNBXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNEXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNLEXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNLXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNOXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNPXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNSXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPNZXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPOXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPPXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPSXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |
| ❌ | CMPZXADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[7] = 0 on this CPU) — implemented per the manual, open item: implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the… |

</details>

<details><summary><b>CMPXCHG16B</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | CMPXCHG16B | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>ENQCMD</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | ENQCMD | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[29] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | ENQCMDS | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[29] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in Ph… |

</details>

<details><summary><b>F16C</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | VCVTPH2PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VCVTPS2PH | vex | 128/256 | identical to the i5-13600K (4 forms) |

</details>

<details><summary><b>FAT_NOP</b> (8 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | NOP | legacy | - | identical to the i5-13600K (10 forms) |
| ✅ | NOP3 | legacy | - | identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |
| ✅ | NOP4 | legacy | - | identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |
| ✅ | NOP5 | legacy | - | identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |
| ✅ | NOP6 | legacy | - | identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |
| ✅ | NOP7 | legacy | - | identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |
| ✅ | NOP8 | legacy | - | identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |
| ✅ | NOP9 | legacy | - | identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |

</details>

<details><summary><b>FCMOV</b> (9 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | FCMOVB | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCMOVBE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCMOVE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCMOVNB | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCMOVNBE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCMOVNE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCMOVNP | legacy | - | identical to the i5-13600K (1 forms) — same opcode as FCMOVNU (Capstone name) |
| ✅ | FCMOVNU | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCMOVU | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>FCOMI</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | FCOMI | legacy | - | manual C1=0 default, quirk bit 0 = hardware (U38) |
| ✅ | FCOMIP | legacy | - | quirk bit 0 (U38) |
| ✅ | FCOMPI | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FUCOMI | legacy | - | quirk bit 0 (U38) |
| ✅ | FUCOMIP | legacy | - | quirk bit 0 (U38) |
| ✅ | FUCOMPI | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>FMA</b> (60 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | VFMADD132PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADD132PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADD132SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMADD132SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMADD213PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADD213PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADD213SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMADD213SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMADD231PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADD231PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADD231SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMADD231SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMADDSUB132PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADDSUB132PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADDSUB213PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADDSUB213PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADDSUB231PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMADDSUB231PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUB132PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUB132PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUB132SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMSUB132SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMSUB213PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUB213PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUB213SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMSUB213SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMSUB231PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUB231PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUB231SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMSUB231SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFMSUBADD132PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUBADD132PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUBADD213PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUBADD213PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUBADD231PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFMSUBADD231PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMADD132PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMADD132PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMADD132SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMADD132SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMADD213PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMADD213PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMADD213SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMADD213SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMADD231PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMADD231PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMADD231SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMADD231SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMSUB132PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMSUB132PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMSUB132SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMSUB132SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMSUB213PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMSUB213PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMSUB213SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMSUB213SS | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMSUB231PD | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMSUB231PS | vex | 128/256 | identical to the i5-13600K (4 forms) |
| ✅ | VFNMSUB231SD | vex | 128 | identical to the i5-13600K (2 forms) |
| ✅ | VFNMSUB231SS | vex | 128 | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>FRED</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | ERETS | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[17] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |
| ❌ | ERETU | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[17] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |

</details>

<details><summary><b>FXSAVE</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | FXRSTOR | legacy | - | U64 |
| ✅ | FXSAVE | legacy | - | U64 (FOP/FIP/FDP, REX.W layout) |

</details>

<details><summary><b>FXSAVE64</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | FXRSTOR64 | legacy | - | U64 |
| ✅ | FXSAVE64 | legacy | - | U64 |

</details>

<details><summary><b>GFNI</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | GF2P8AFFINEINVQB | legacy | 128 | U70 |
| ✅ | GF2P8AFFINEQB | legacy | 128 | U70 |
| ✅ | GF2P8MULB | legacy | 128 | U70, identical to the CPU |

</details>

<details><summary><b>HLE</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | XACQUIRE | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[4] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (cases_reach): F2 on LOCK… |
| ❌ | XRELEASE | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[4] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (cases_reach): F3 on LOCK… |

</details>

<details><summary><b>HRESET</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | HRESET | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[22] = 0 on this CPU) — implemented per the manual, open item: CPU: #GP at CPL3 even with CPUID bit 0 (Phase 2, D6) |

</details>

<details><summary><b>I186</b> (19 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | BOUND | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ⏳ | ENTER | legacy | - | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | IMUL | legacy | - | identical to the i5-13600K (20 forms) |
| ⏳ | INSB | legacy | - | CPL0 instruction (3 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | INSW | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | LEAVE | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | OUTSB | legacy | - | CPL0 instruction (3 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | OUTSW | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | POPAW | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ⏳ | PUSH | legacy | - | implemented; 7 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | PUSHAW | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | RCL | legacy | - | identical to the i5-13600K (23 forms) |
| ✅ | RCR | legacy | - | identical to the i5-13600K (22 forms) |
| ✅ | ROL | legacy | - | identical to the i5-13600K (22 forms) |
| ✅ | ROR | legacy | - | identical to the i5-13600K (22 forms) |
| ✅ | SAL | legacy | - | identical to the i5-13600K (22 forms) |
| ✅ | SAR | legacy | - | identical to the i5-13600K (22 forms) |
| ✅ | SHL | legacy | - | identical to the i5-13600K (22 forms) |
| ✅ | SHR | legacy | - | identical to the i5-13600K (22 forms) |

</details>

<details><summary><b>I286PROTECTED</b> (9 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | ARPL | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | LAR | legacy | - | identical to the i5-13600K (6 forms) |
| ⏳ | LLDT | legacy | - | CPL0 instruction (2 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | LSL | legacy | - | identical to the i5-13600K (6 forms) |
| ⏳ | LTR | legacy | - | CPL0 instruction (2 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | SLDT | legacy | - | values: Phase 2 environment |
| ⏳ | STR | legacy | - | values: Phase 2 environment |
| ✅ | VERR | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | VERW | legacy | - | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>I286REAL</b> (7 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | CLTS | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | LGDT | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | LIDT | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | LMSW | legacy | - | CPL0 instruction (2 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | SGDT | legacy | - | values: Phase 2 environment |
| ⏳ | SIDT | legacy | - | values: Phase 2 environment |
| ⏳ | SMSW | legacy | - | CR0 value: Phase 2 environment |

</details>

<details><summary><b>I386</b> (47 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | BSF | legacy | - | identical to the i5-13600K (8 forms) |
| ✅ | BSR | legacy | - | identical to the i5-13600K (8 forms) |
| ✅ | BT | legacy | - | identical to the i5-13600K (12 forms) |
| ✅ | BTC | legacy | - | identical to the i5-13600K (12 forms) |
| ✅ | BTR | legacy | - | identical to the i5-13600K (12 forms) |
| ✅ | BTS | legacy | - | identical to the i5-13600K (12 forms) |
| ✅ | CDQ | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | CMPSD | legacy | 128 | identical to the i5-13600K (5 forms) |
| ✅ | CWDE | legacy | - | identical to the i5-13600K (1 forms) |
| ⏳ | INSD | legacy | - | CPL0 instruction (3 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | IRETD | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | JCXZ | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | JECXZ | legacy | - | identical to the i5-13600K (cases_reach): 67 E3 rel8 (ECX = 0 with RCX[63:32] != 0 taken) |
| ⏳ | LFS | legacy | - | implemented; 3 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | LGS | legacy | - | implemented; 3 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | LODSD | legacy | - | identical to the i5-13600K (3 forms) |
| ⏳ | LSS | legacy | - | implemented; 3 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | MOVSD | legacy | 128 | identical to the i5-13600K (6 forms) |
| ✅ | MOVSX | legacy | - | identical to the i5-13600K (12 forms) |
| ✅ | MOVZX | legacy | - | identical to the i5-13600K (12 forms) |
| ⏳ | OUTSD | legacy | - | CPL0 instruction (3 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | POPAL | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | POPFD | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | POPFL | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | PUSHAL | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | PUSHFD | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | PUSHFL | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | SCASD | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | SETA | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETAE | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETB | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETBE | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETE | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETG | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETGE | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETL | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETLE | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETNE | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETNO | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETNP | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETNS | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETO | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETP | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SETS | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | SHLD | legacy | - | identical to the i5-13600K (12 forms) |
| ✅ | SHRD | legacy | - | identical to the i5-13600K (12 forms) |
| ✅ | STOSD | legacy | - | identical to the i5-13600K (3 forms) |

</details>

<details><summary><b>I486</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | RSM | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>I486REAL</b> (7 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | BSWAP | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | CMPXCHG | legacy | - | identical to the i5-13600K (8 forms) |
| ✅ | CPUID | legacy | - | U68 i5-13600K profile + UC_CTL_X86_CPUID(_STRICT); only per-core APIC IDs vary |
| ⏳ | INVD | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | INVLPG | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | WBINVD | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | XADD | legacy | - | identical to the i5-13600K (8 forms) |

</details>

<details><summary><b>I86</b> (88 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | AAA | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | AAD | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | AAM | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | AAS | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | ADC | legacy | - | identical to the i5-13600K (20 forms) |
| ✅ | ADD | legacy | - | identical to the i5-13600K (20 forms) |
| ✅ | AND | legacy | - | identical to the i5-13600K (20 forms) |
| ⏳ | CALL | legacy | - | implemented; 8 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | CBW | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | CLC | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | CLD | legacy | - | identical to the i5-13600K (1 forms) |
| ⏳ | CLI | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | CMC | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | CMP | legacy | - | identical to the i5-13600K (20 forms) |
| ✅ | CMPSB | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | CMPSW | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | CWD | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | DAA | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | DAS | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | DEC | legacy | - | identical to the i5-13600K (8 forms) |
| ✅ | DIV | legacy | - | identical to the i5-13600K (8 forms) |
| ⏳ | HLT | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | IDIV | legacy | - | identical to the i5-13600K (8 forms) |
| ⏳ | IN | legacy | - | CPL0 instruction (6 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | INC | legacy | - | identical to the i5-13600K (8 forms) |
| ⏳ | INT | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | INT1 | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | INT3 | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | INTO | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ⏳ | IRET | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | JA | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JAE | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JB | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JBE | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JE | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JG | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JGE | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JL | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JLE | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JMP | legacy | - | implemented; 8 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JNE | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JNO | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JNP | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JNS | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JO | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JP | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | JS | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | LCALL | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | LDS | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | LEA | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | LES | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ⏳ | LJMP | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | LODSB | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | LODSW | legacy | - | identical to the i5-13600K (1 forms) |
| ⏳ | LOOP | legacy | - | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | LOOPE | legacy | - | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | LOOPNE | legacy | - | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | MOV | legacy | - | CPL0 instruction (8 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | MOVABS | legacy | - | implemented; 8 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ✅ | MOVSB | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | MOVSW | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | MUL | legacy | - | identical to the i5-13600K (8 forms) |
| ✅ | NEG | legacy | - | identical to the i5-13600K (8 forms) |
| ✅ | NOP2 | legacy | - | identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |
| ✅ | NOT | legacy | - | identical to the i5-13600K (8 forms) |
| ✅ | OR | legacy | - | identical to the i5-13600K (20 forms) |
| ⏳ | OUT | legacy | - | CPL0 instruction (6 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | POP | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | POPF | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | PUSHF | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | RET | legacy | - | implemented; 9 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | RETF | legacy | - | implemented; 5 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | RETFQ | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | SALC | legacy | - | #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |
| ✅ | SBB | legacy | - | identical to the i5-13600K (20 forms) |
| ✅ | SCASB | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | SCASW | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | STC | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | STD | legacy | - | identical to the i5-13600K (1 forms) |
| ⏳ | STI | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ✅ | STOSB | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | STOSW | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | SUB | legacy | - | identical to the i5-13600K (20 forms) |
| ✅ | TEST | legacy | - | identical to the i5-13600K (16 forms) |
| ✅ | UDB | legacy | - | identical to the i5-13600K (cases_reach): D6 (UDB): #UD in 64-bit mode |
| ✅ | XCHG | legacy | - | identical to the i5-13600K (20 forms) |
| ✅ | XLATB | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | XOR | legacy | - | identical to the i5-13600K (20 forms) |

</details>

<details><summary><b>IBHF</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | IBHF | legacy | - | NOT SUPPORTED on our i5-13600K (not reported by this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (cases_reach): F3 [REX.W] 0F 1E … |

</details>

<details><summary><b>ICACHE_PREFETCH</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | PREFETCHIT0 | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (cases_reach): 0F 18 /… |
| ❌ | PREFETCHIT1 | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (cases_reach): 0F 18 /… |

</details>

<details><summary><b>INVPCID</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | INVPCID | legacy | - | CPL0: #GP at CPL3 in Phase 2 (D6); #UD today |

</details>

<details><summary><b>KEYLOCKER</b> (7 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | AESDEC128KL | legacy | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[23] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | AESDEC256KL | legacy | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[23] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | AESENC128KL | legacy | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[23] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | AESENC256KL | legacy | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[23] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | ENCODEKEY128 | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[23] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | ENCODEKEY256 | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[23] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | LOADIWKEY | legacy | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[23] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in Ph… |

</details>

<details><summary><b>KEYLOCKER_WIDE</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | AESDECWIDE128KL | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.19H:EBX[2] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | AESDECWIDE256KL | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.19H:EBX[2] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | AESENCWIDE128KL | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.19H:EBX[2] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | AESENCWIDE256KL | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.19H:EBX[2] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |

</details>

<details><summary><b>LAHF</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | LAHF | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | SAHF | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>LKGS</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | LKGS | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[18] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |

</details>

<details><summary><b>LONGMODE</b> (14 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | CDQE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | CMPSQ | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | CQO | legacy | - | identical to the i5-13600K (1 forms) |
| ⏳ | IRETQ | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | JRCXZ | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | LODSQ | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | MOVSQ | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | MOVSXD | legacy | - | identical to the i5-13600K (6 forms) |
| ⏳ | POPFQ | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | PUSHFQ | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ✅ | SCASQ | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | STOSQ | legacy | - | identical to the i5-13600K (3 forms) |
| ⏳ | SWAPGS | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | SYSRETQ | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>MONITOR</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MONITOR | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.1H:ECX[3] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ❌ | MWAIT | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.1H:ECX[3] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>MOVBE</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | MOVBE | legacy | - | partial: 8 SDM-vector check pending, 2 not implemented |

</details>

<details><summary><b>MOVDIR64B</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | MOVDIR64B | legacy | - | U73, identical to the CPU |

</details>

<details><summary><b>MOVDIRI</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | MOVDIRI | legacy | - | U72, identical to the CPU |

</details>

<details><summary><b>MOVRS</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MOVRS | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[31] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | PREFETCHRST2 | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[31] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (cases_reach): 0F 18 /… |

</details>

<details><summary><b>MPX</b> (7 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | BNDCL | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (2 forms) |
| ❌ | BNDCN | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (2 forms) |
| ❌ | BNDCU | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (2 forms) |
| ❌ | BNDLDX | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (1 forms) |
| ❌ | BNDMK | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (1 forms) |
| ❌ | BNDMOV | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (3 forms) |
| ❌ | BNDSTX | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[14] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>MSRLIST</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | RDMSRLIST | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[27] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |
| ❌ | WRMSRLIST | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[27] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |

</details>

<details><summary><b>MSR_IMM</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | RDMSR | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:ECX[5] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in P… |
| ❌ | WRMSRNS | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:ECX[5] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in P… |

</details>

<details><summary><b>PAUSE</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | PAUSE | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>PBNDKB</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | PBNDKB | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EBX[1] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in P… |

</details>

<details><summary><b>PCLMULQDQ</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | PCLMULQDQ | legacy | 128 | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>PCONFIG</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | PCONFIG | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[18] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |

</details>

<details><summary><b>PENTIUMMMX</b> (60 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | EMMS | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | MASKMOVQ | legacy | 64 | identical to the i5-13600K (1 forms) |
| ✅ | MOVD | legacy | 64/128 | identical to the i5-13600K (8 forms) |
| ✅ | MOVNTQ | legacy | 64 | identical to the i5-13600K (1 forms) |
| ✅ | MOVQ | legacy | 64/128 | identical to the i5-13600K (10 forms) |
| ✅ | PACKSSDW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PACKSSWB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PACKUSWB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PADDB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PADDD | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PADDSB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PADDSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PADDUSB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PADDUSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PADDW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PAND | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PANDN | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PAVGB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PAVGW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PCMPEQB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PCMPEQD | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PCMPEQW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PCMPGTB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PCMPGTD | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PCMPGTW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PEXTRW | legacy | 64/128 | identical to the i5-13600K (3 forms) |
| ✅ | PINSRW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMADDWD | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMAXSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMAXUB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMINSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMINUB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMULHUW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMULHW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMULLW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | POR | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSADBW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSHUFW | legacy | 64 | identical to the i5-13600K (2 forms) |
| ✅ | PSLLD | legacy | 64/128 | identical to the i5-13600K (6 forms) |
| ✅ | PSLLQ | legacy | 64/128 | identical to the i5-13600K (6 forms) |
| ✅ | PSLLW | legacy | 64/128 | identical to the i5-13600K (6 forms) |
| ✅ | PSRAD | legacy | 64/128 | identical to the i5-13600K (6 forms) |
| ✅ | PSRAW | legacy | 64/128 | identical to the i5-13600K (6 forms) |
| ✅ | PSRLD | legacy | 64/128 | identical to the i5-13600K (6 forms) |
| ✅ | PSRLQ | legacy | 64/128 | identical to the i5-13600K (6 forms) |
| ✅ | PSRLW | legacy | 64/128 | identical to the i5-13600K (6 forms) |
| ✅ | PSUBB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSUBD | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSUBSB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSUBSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSUBUSB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSUBUSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSUBW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PUNPCKHBW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PUNPCKHDQ | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PUNPCKHWD | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PUNPCKLBW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PUNPCKLDQ | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PUNPCKLWD | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PXOR | legacy | 64/128 | identical to the i5-13600K (4 forms) |

</details>

<details><summary><b>PENTIUMREAL</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | CMPXCHG8B | legacy | - | identical to the i5-13600K (1 forms) |
| ⏳ | RDMSR | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | RDTSC | legacy | - | TSC determinism hook: Phase 2 |
| ⏳ | WRMSR | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>PKU</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | RDPKRU | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode) — not implemented yet: not implemented (cases_reach): PKU… |
| ❌ | WRPKRU | legacy | - | NOT SUPPORTED on our i5-13600K (the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode) — not implemented yet: not implemented (cases_reach): PKU… |

</details>

<details><summary><b>POPCNT</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | POPCNT | legacy | - | identical to the i5-13600K (4 forms) |

</details>

<details><summary><b>PPRO</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | UD1 | legacy | - | identical to the i5-13600K (cases_reach): 0F B9 /r: #UD (its defined behaviour) |
| ✅ | UD2 | legacy | - | identical to the i5-13600K (cases_reach): 0F 0B: #UD (its defined behaviour) |

</details>

<details><summary><b>PPRO_UD0_LONG</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | UD0 | legacy | - | identical to the i5-13600K (cases_reach): 0F FF /r: #UD (its defined behaviour) |

</details>

<details><summary><b>PREFETCHWT1</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | PREFETCHWT1 | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[0] = 0 on this CPU) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>PREFETCH_NOP</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | PREFETCH | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | PREFETCHW | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>PTWRITE</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | PTWRITE | legacy | - | U80: SDM #UD default (CPUID.14 = 0), quirk bit 3 = hardware (operand read) |

</details>

<details><summary><b>RAO_INT</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | AADD | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[3] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | AAND | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[3] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | AOR | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[3] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | AXOR | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[3] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |

</details>

<details><summary><b>RDPID</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | RDPID | legacy | - | TSC_AUX value: Phase 2 environment |

</details>

<details><summary><b>RDPMC</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | RDPMC | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>RDRAND</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | RDRAND | legacy | - | U65 host entropy (RtlGenRandom), CF/flags per SDM |

</details>

<details><summary><b>RDSEED</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | RDSEED | legacy | - | U65 |

</details>

<details><summary><b>RDTSCP</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | RDTSCP | legacy | - | TSC/TSC_AUX: Phase 2 |

</details>

<details><summary><b>RDWRFSGS</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | RDFSBASE | legacy | - | identical to the i5-13600K (2 forms) |
| ⏳ | RDGSBASE | legacy | - | value = TEB base: Phase 2 environment |
| ⏳ | WRFSBASE | legacy | - | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | WRGSBASE | legacy | - | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |

</details>

<details><summary><b>RTM</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | XABORT | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[11] = 0 on this CPU) — implemented per the manual, open item: implemented; 4 forms not runnable on the i5-13600K (CPU lacks … |
| ❌ | XBEGIN | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[11] = 0 on this CPU) — implemented per the manual, open item: implemented; 4 forms not runnable on the i5-13600K (CPU lacks … |
| ❌ | XEND | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[11] = 0 on this CPU) — implemented per the manual, open item: implemented; 1 forms not runnable on the i5-13600K (CPU lacks … |
| ❌ | XTEST | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[11] = 0 on this CPU) — implemented per the manual, open item: implemented; 1 forms not runnable on the i5-13600K (CPU lacks … |

</details>

<details><summary><b>SEP</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | SYSENTER | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | SYSEXIT | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | SYSEXITQ | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>SERIALIZE</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | SERIALIZE | legacy | - | U74, identical to the CPU |

</details>

<details><summary><b>SGX</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | ENCLS | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[2] = 0 on this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks SGX: #UD in both |
| ❌ | ENCLU | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[2] = 0 on this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks SGX: #UD in both |

</details>

<details><summary><b>SGX_ENCLV</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | ENCLV | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[2] = 0 on this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks SGX: #UD in both |

</details>

<details><summary><b>SHA</b> (7 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | SHA1MSG1 | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | SHA1MSG2 | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | SHA1NEXTE | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | SHA1RNDS4 | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | SHA256MSG1 | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | SHA256MSG2 | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | SHA256RNDS2 | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |

</details>

<details><summary><b>SHA512</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VSHA512MSG1 | vex | 256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[0] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VSHA512MSG2 | vex | 256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[0] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VSHA512RNDS2 | vex | 256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[0] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |

</details>

<details><summary><b>SM3</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VSM3MSG1 | vex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[1] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VSM3MSG2 | vex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[1] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VSM3RNDS2 | vex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[1] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |

</details>

<details><summary><b>SM4</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VSM4KEY4 | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[2] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VSM4KEY4 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[2] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSM4RNDS4 | vex | 128/256 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[2] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | VSM4RNDS4 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[2] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>SMAP</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | CLAC | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | STAC | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>SMX</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | GETSEC | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.1H:ECX[6] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agains… |

</details>

<details><summary><b>SSE</b> (110 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | ADDPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ADDSS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ANDNPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ANDPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CMPEQ_OSPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPEQ_OSSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPEQ_UQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPEQ_UQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPEQ_USPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPEQ_USSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPEQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPEQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPFALSE_OSPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPFALSE_OSSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPFALSEPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPFALSESS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPGE_OQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPGE_OQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPGEPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPGESS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPGT_OQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPGT_OQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPGTPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPGTSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPLE_OQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPLE_OQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPLEPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPLESS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPLT_OQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPLT_OQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPLTPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPLTSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNEQ_OQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNEQ_OQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNEQ_OSPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNEQ_OSSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNEQ_USPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNEQ_USSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNEQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNEQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNGE_UQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNGE_UQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNGEPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNGESS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNGT_UQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNGT_UQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNGTPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNGTSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNLE_UQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNLE_UQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNLEPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNLESS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNLT_UQPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNLT_UQSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPNLTPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPNLTSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPORD_SPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPORD_SSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPORDPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPORDSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CMPSS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CMPTRUE_USPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPTRUE_USSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPTRUEPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPTRUESS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPUNORD_SPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPUNORD_SSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | CMPUNORDPS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |
| ✅ | CMPUNORDSS | legacy | 128 | identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |
| ✅ | COMISS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTPI2PS | legacy | 128 | m64 form: manual transition default, quirk bit 1 = hardware (U44/U50) |
| ✅ | CVTPS2PI | legacy | 64/128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTSI2SS | legacy | 128 | identical to the i5-13600K (4 forms) |
| ✅ | CVTSS2SI | legacy | 128 | identical to the i5-13600K (4 forms) |
| ✅ | CVTTPS2PI | legacy | 64/128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTTSS2SI | legacy | 128 | identical to the i5-13600K (4 forms) |
| ✅ | DIVPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | DIVSS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MAXPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MAXSS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MINPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MINSS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVAPS | legacy | 128 | identical to the i5-13600K (3 forms) |
| ✅ | MOVHLPS | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVHPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVLHPS | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVLPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVMSKPS | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVNTPS | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVSS | legacy | 128 | identical to the i5-13600K (3 forms) |
| ✅ | MOVUPS | legacy | 128 | identical to the i5-13600K (3 forms) |
| ✅ | MULPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MULSS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ORPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVMSKB | legacy | 64/128 | identical to the i5-13600K (2 forms) |
| ✅ | RCPPS | legacy | 128 | U81 analytic Intel 12-bit model (RN of 1/midpoint); 2^32 inputs x 6 MXCSR == CPU |
| ✅ | RCPSS | legacy | 128 | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| ✅ | RSQRTPS | legacy | 128 | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| ✅ | RSQRTSS | legacy | 128 | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| ✅ | SFENCE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | SHUFPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SQRTPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SQRTSS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SUBPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SUBSS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | UCOMISS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | UNPCKHPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | UNPCKLPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | XORPS | legacy | 128 | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>SSE2</b> (129 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | ADDPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ADDSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ANDNPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ANDPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CMPEQ_OSPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPEQ_OSSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPEQ_UQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPEQ_UQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPEQ_USPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPEQ_USSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPEQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPEQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPFALSE_OSPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPFALSE_OSSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPFALSEPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPFALSESD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPGE_OQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPGE_OQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPGEPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPGESD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPGT_OQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPGT_OQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPGTPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPGTSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPLE_OQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPLE_OQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPLEPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPLESD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPLT_OQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPLT_OQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPLTPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPLTSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNEQ_OQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNEQ_OQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNEQ_OSPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNEQ_OSSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNEQ_USPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNEQ_USSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNEQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNEQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNGE_UQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNGE_UQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNGEPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNGESD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNGT_UQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNGT_UQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNGTPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNGTSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNLE_UQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNLE_UQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNLEPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNLESD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNLT_UQPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNLT_UQSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPNLTPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPNLTSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPORD_SPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPORD_SSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPORDPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPORDSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CMPTRUE_USPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPTRUE_USSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPTRUEPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPTRUESD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPUNORD_SPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPUNORD_SSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | CMPUNORDPD | legacy | 128 | identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |
| ✅ | CMPUNORDSD | legacy | 128 | identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |
| ✅ | COMISD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTDQ2PD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTDQ2PS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTPD2DQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTPD2PI | legacy | 64/128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTPD2PS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTPI2PD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTPS2DQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTPS2PD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTSD2SI | legacy | 128 | identical to the i5-13600K (4 forms) |
| ✅ | CVTSD2SS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTSI2SD | legacy | 128 | identical to the i5-13600K (4 forms) |
| ✅ | CVTSS2SD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTTPD2DQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTTPD2PI | legacy | 64/128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTTPS2DQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | CVTTSD2SI | legacy | 128 | identical to the i5-13600K (4 forms) |
| ✅ | DIVPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | DIVSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | LFENCE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | MASKMOVDQU | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MAXPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MAXSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MFENCE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | MINPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MINSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVAPD | legacy | 128 | identical to the i5-13600K (3 forms) |
| ✅ | MOVDQ2Q | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVDQA | legacy | 128 | identical to the i5-13600K (3 forms) |
| ✅ | MOVDQU | legacy | 128 | identical to the i5-13600K (3 forms) |
| ✅ | MOVHPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVLPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVMSKPD | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVNTDQ | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVNTI | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | MOVNTPD | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVQ2DQ | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVUPD | legacy | 128 | identical to the i5-13600K (3 forms) |
| ✅ | MULPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MULSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ORPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PADDQ | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMULUDQ | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSHUFD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PSHUFHW | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PSHUFLW | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PSLLDQ | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | PSRLDQ | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | PSUBQ | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PUNPCKHQDQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PUNPCKLQDQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SHUFPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SQRTPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SQRTSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SUBPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | SUBSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | UCOMISD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | UNPCKHPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | UNPCKLPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | XORPD | legacy | 128 | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>SSE3</b> (10 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | ADDSUBPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ADDSUBPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | HADDPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | HADDPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | HSUBPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | HSUBPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | LDDQU | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MOVDDUP | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVSHDUP | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVSLDUP | legacy | 128 | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>SSE3X87</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | FISTTP | legacy | - | identical to the i5-13600K (3 forms) |

</details>

<details><summary><b>SSE4</b> (48 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | BLENDPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | BLENDPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | BLENDVPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | BLENDVPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | DPPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | DPPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | EXTRACTPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | INSERTPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | MOVNTDQA | legacy | 128 | identical to the i5-13600K (1 forms) |
| ✅ | MPSADBW | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PACKUSDW | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PBLENDVB | legacy | 128 | identical to the i5-13600K (2 forms) |
| ⏳ | PBLENDW | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ✅ | PCMPEQQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PEXTRB | legacy | 128 | identical to the i5-13600K (2 forms) |
| ⏳ | PEXTRD | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ⏳ | PEXTRQ | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ✅ | PHMINPOSUW | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PINSRB | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PINSRD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PINSRQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMAXSB | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMAXSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMAXUD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ⏳ | PMAXUW | legacy | 128 | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 1 identical |
| ✅ | PMINSB | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMINSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMINUD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMINUW | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVSXBD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVSXBQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVSXBW | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVSXDQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVSXWD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVSXWQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVZXBD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVZXBQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVZXBW | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVZXDQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVZXWD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMOVZXWQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMULDQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PMULLD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PTEST | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ROUNDPD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ROUNDPS | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ROUNDSD | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | ROUNDSS | legacy | 128 | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>SSE42</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | CRC32 | legacy | - | implemented; 4 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 4 identical |
| ✅ | PCMPESTRI | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PCMPESTRM | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PCMPGTQ | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PCMPISTRI | legacy | 128 | identical to the i5-13600K (2 forms) |
| ✅ | PCMPISTRM | legacy | 128 | identical to the i5-13600K (2 forms) |

</details>

<details><summary><b>SSEMXCSR</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | LDMXCSR | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | STMXCSR | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>SSE_PREFETCH</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | PREFETCHNTA | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | PREFETCHT0 | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | PREFETCHT1 | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | PREFETCHT2 | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>SSSE3</b> (16 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | PABSB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ⏳ | PABSD | legacy | 64/128 | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 2 identical |
| ✅ | PABSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PALIGNR | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PHADDD | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PHADDSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PHADDW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ⏳ | PHSUBD | legacy | 64/128 | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending, 2 identical |
| ✅ | PHSUBSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PHSUBW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMADDUBSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PMULHRSW | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSHUFB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSIGNB | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSIGND | legacy | 64/128 | identical to the i5-13600K (4 forms) |
| ✅ | PSIGNW | legacy | 64/128 | identical to the i5-13600K (4 forms) |

</details>

<details><summary><b>TDX</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | SEAMCALL | legacy | - | NOT SUPPORTED on our i5-13600K (Intel TDX (server, VMX root only)) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in P… |
| ❌ | SEAMOPS | legacy | - | NOT SUPPORTED on our i5-13600K (Intel TDX (server, VMX root only)) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in P… |
| ❌ | SEAMRET | legacy | - | NOT SUPPORTED on our i5-13600K (Intel TDX (server, VMX root only)) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in P… |
| ❌ | TDCALL | legacy | - | NOT SUPPORTED on our i5-13600K (Intel TDX (server, VMX root only)) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in P… |

</details>

<details><summary><b>TSX_LDTRK</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | XRESLDTRK | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | XSUSLDTRK | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |

</details>

<details><summary><b>UINTR</b> (5 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | CLUI | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agains… |
| ❌ | SENDUIPI | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agains… |
| ❌ | STUI | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agains… |
| ❌ | TESTUI | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agains… |
| ❌ | UIRET | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[5] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agains… |

</details>

<details><summary><b>USER_MSR</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | URDMSR | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[15] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | URDMSR | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[15] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | UWRMSR | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[15] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |
| ❌ | UWRMSR | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[15] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |

</details>

<details><summary><b>VMFUNC</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | VMFUNC | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>VTX</b> (12 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | INVEPT | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | INVVPID | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMCALL | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMCLEAR | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMLAUNCH | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMPTRLD | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMPTRST | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMREAD | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMRESUME | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMWRITE | legacy | - | CPL0 instruction (2 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMXOFF | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ⏳ | VMXON | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>WAITPKG</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TPAUSE | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[5] = 0 on this CPU) — implemented per the manual, open item: implemented; 1 forms not runnable on the i5-13600K (CPU lacks i… |
| ❌ | UMONITOR | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[5] = 0 on this CPU) — implemented per the manual, open item: implemented; 1 forms not runnable on the i5-13600K (CPU lacks i… |
| ❌ | UMWAIT | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[5] = 0 on this CPU) — implemented per the manual, open item: implemented; 1 forms not runnable on the i5-13600K (CPU lacks i… |

</details>

<details><summary><b>WBNOINVD</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | WBNOINVD | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.80000008H:EBX[9] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 … |

</details>

<details><summary><b>WRMSRNS</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | WRMSRNS | legacy | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[19] = 0 on this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |

</details>

<details><summary><b>X87</b> (79 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | F2XM1 | legacy | - | U56 Goldmont-microcode model, 100% bit-exact (value + FSW) |
| ✅ | FABS | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FADD | legacy | - | identical to the i5-13600K (4 forms) |
| ✅ | FADDP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FBLD | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FBSTP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCHS | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCOM | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | FCOMP | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | FCOMPP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FCOS | legacy | - | U56 microcode model, 100% bit-exact |
| ✅ | FDECSTP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FDISI8087_NOP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FDIV | legacy | - | identical to the i5-13600K (4 forms) |
| ✅ | FDIVP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FDIVR | legacy | - | identical to the i5-13600K (4 forms) |
| ✅ | FDIVRP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FENI8087_NOP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FFREE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FFREEP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FIADD | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FICOM | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FICOMP | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FIDIV | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FIDIVR | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FILD | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | FIMUL | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FINCSTP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FIST | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FISTP | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | FISUB | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FISUBR | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FLD | legacy | - | identical to the i5-13600K (4 forms) |
| ✅ | FLD1 | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FLDCW | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FLDENV | legacy | - | U64 |
| ✅ | FLDL2E | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FLDL2T | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FLDLG2 | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FLDLN2 | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FLDPI | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FLDZ | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FMUL | legacy | - | identical to the i5-13600K (4 forms) |
| ✅ | FMULP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FNCLEX | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FNINIT | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FNOP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FNSAVE | legacy | - | U61-U64 |
| ✅ | FNSTCW | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FNSTENV | legacy | - | U61/U62/U64 (FCW masked after, reserved FFFF, FIP/FOP/FDP model) |
| ✅ | FNSTSW | legacy | - | identical to the i5-13600K (2 forms) |
| ✅ | FPATAN | legacy | - | U56 microcode model, 100% bit-exact |
| ✅ | FPREM | legacy | - | ROM model == hw 148/148, fork == hw 388/388 |
| ✅ | FPREM1 | legacy | - | ROM model == hw, fork == hw |
| ✅ | FPTAN | legacy | - | U56 microcode model, 100% bit-exact |
| ✅ | FRNDINT | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FRSTOR | legacy | - | U63/U64 |
| ✅ | FSCALE | legacy | - | ROM model == hw, fork == hw |
| ✅ | FSETPM | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FSIN | legacy | - | U56 microcode model, 100% bit-exact |
| ✅ | FSINCOS | legacy | - | U56 microcode model, 100% bit-exact |
| ✅ | FSQRT | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FST | legacy | - | identical to the i5-13600K (3 forms) |
| ✅ | FSTP | legacy | - | identical to the i5-13600K (4 forms) |
| ✅ | FSTPNCE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FSUB | legacy | - | identical to the i5-13600K (4 forms) |
| ✅ | FSUBP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FSUBR | legacy | - | identical to the i5-13600K (4 forms) |
| ✅ | FSUBRP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FTST | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FUCOM | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FUCOMP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FUCOMPP | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FXAM | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FXCH | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FXTRACT | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | FYL2X | legacy | - | U56 microcode model, 100% bit-exact |
| ✅ | FYL2XP1 | legacy | - | U56 microcode model, 100% bit-exact; x < -1: manual #IA, quirk bit 2 = hardware |
| ✅ | WAIT | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>XSAVE</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | XGETBV | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | XRSTOR | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | XRSTOR64 | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | XSAVE | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | XSAVE64 | legacy | - | identical to the i5-13600K (1 forms) |
| ⏳ | XSETBV | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>XSAVEC</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | XSAVEC | legacy | - | U66 compacted format |
| ✅ | XSAVEC64 | legacy | - | U66 |

</details>

<details><summary><b>XSAVEOPT</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | XSAVEOPT | legacy | - | identical to the i5-13600K (1 forms) |
| ✅ | XSAVEOPT64 | legacy | - | identical to the i5-13600K (1 forms) |

</details>

<details><summary><b>XSAVES</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ⏳ | XRSTORS | legacy | - | CPL0: Phase 2 (D6) |
| ⏳ | XRSTORS64 | legacy | - | CPL0: Phase 2 (D6) |
| ⏳ | XSAVES | legacy | - | CPL0: #GP at CPL3 in Phase 2 (D6); compacted format shared with U66 |
| ⏳ | XSAVES64 | legacy | - | CPL0: Phase 2 (D6) |

</details>

<details><summary><b>AMX_AVX512</b> (5 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TCVTROWD2PS | evex | 512 | NOT SUPPORTED on our i5-13600K (AMX_AVX512 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TCVTROWPS2BF16H | evex | 512 | NOT SUPPORTED on our i5-13600K (AMX_AVX512 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TCVTROWPS2BF16L | evex | 512 | NOT SUPPORTED on our i5-13600K (AMX_AVX512 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TCVTROWPS2PHH | evex | 512 | NOT SUPPORTED on our i5-13600K (AMX_AVX512 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TCVTROWPS2PHL | evex | 512 | NOT SUPPORTED on our i5-13600K (AMX_AVX512 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AMX_BF16</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TDPBF16PS | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[22] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |

</details>

<details><summary><b>AMX_COMPLEX</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TCMMIMFP16PS | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[8] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |
| ❌ | TCMMRLFP16PS | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EDX[8] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified agai… |

</details>

<details><summary><b>AMX_FP16</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TDPFP16PS | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[21] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified aga… |

</details>

<details><summary><b>AMX_FP8</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TDPBF8PS | vex | - | NOT SUPPORTED on our i5-13600K (AMX_FP8 not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks AMX: #UD in both |
| ❌ | TDPBHF8PS | vex | - | NOT SUPPORTED on our i5-13600K (AMX_FP8 not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks AMX: #UD in both |
| ❌ | TDPHBF8PS | vex | - | NOT SUPPORTED on our i5-13600K (AMX_FP8 not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks AMX: #UD in both |
| ❌ | TDPHF8PS | vex | - | NOT SUPPORTED on our i5-13600K (AMX_FP8 not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks AMX: #UD in both |

</details>

<details><summary><b>AMX_INT8</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TDPBSSD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[25] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | TDPBSUD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[25] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | TDPBUSD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[25] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | TDPBUUD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[25] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |

</details>

<details><summary><b>AMX_MOVRS</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TILELOADDRS | vex | - | NOT SUPPORTED on our i5-13600K (AMX_MOVRS not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks AMX: #UD in both |
| ❌ | TILELOADDRST1 | vex | - | NOT SUPPORTED on our i5-13600K (AMX_MOVRS not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks AMX: #UD in both |

</details>

<details><summary><b>AMX_TILE</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TILELOADD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[24] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | TILELOADDT1 | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[24] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | TILESTORED | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[24] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |

</details>

<details><summary><b>AMX_TILE_BASE</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | LDTILECFG | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[24] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | STTILECFG | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[24] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | TILERELEASE | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[24] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | TILEZERO | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[24] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |

</details>

<details><summary><b>APX_F</b> (33 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | ADC | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | ADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | AND | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | CRC32 | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | DEC | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | DIV | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | IDIV | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | IMUL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | INC | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | JMPABS | legacy | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks APX (REX2 prefix D5 = #UD in 64… |
| ❌ | KMOVB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | KMOVD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | KMOVQ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | KMOVW | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | MUL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | NEG | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | NOT | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | OR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | POPP | legacy | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks APX (REX2 prefix D5 = #UD in 64… |
| ❌ | PUSHP | legacy | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU) — not implemented yet: not implemented (cases_reach): the i5-13600K lacks APX (REX2 prefix D5 = #UD in 64… |
| ❌ | RCL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | RCR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | ROL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | ROR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | SAL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | SAR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | SBB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | SHL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | SHLD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | SHR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | SHRD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | SUB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |
| ❌ | XOR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone for… |

</details>

<details><summary><b>APX_F_ADX</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | ADCX | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_ADX not reported by this CPU; APX_F_ADX_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Caps… |
| ❌ | ADOX | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_ADX not reported by this CPU; APX_F_ADX_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Caps… |

</details>

<details><summary><b>APX_F_AMX</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TILELOADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_AMX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TILELOADDT1 | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_AMX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TILESTORED | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_AMX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_AMX_BASE</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | LDTILECFG | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_AMX_BASE not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | STTILECFG | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_AMX_BASE not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_AMX_MOVRS</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | TILELOADDRS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_AMX_MOVRS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TILELOADDRST1 | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_AMX_MOVRS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_BMI1</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | ANDN | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Ca… |
| ❌ | BEXTR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Ca… |
| ❌ | BLSI | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Ca… |
| ❌ | BLSMSK | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Ca… |
| ❌ | BLSR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Ca… |
| ❌ | TZCNT | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Ca… |

</details>

<details><summary><b>APX_F_BMI2</b> (8 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | BZHI | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI2 not reported by this CPU; APX_F_BMI2_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Ca… |
| ❌ | MULX | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI2 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | PDEP | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI2 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | PEXT | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI2 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | RORX | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI2 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SARX | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI2 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SHLX | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI2 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SHRX | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_BMI2 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_CET</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | WRSSD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CET not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | WRSSQ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CET not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | WRUSSD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CET not reported by this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |
| ❌ | WRUSSQ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CET not reported by this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |

</details>

<details><summary><b>APX_F_CMPCCXADD</b> (22 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | CMPAEXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPAXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPBEXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPBXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPEXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPGEXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPGXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPLEXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPLXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNBEXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNBXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNEXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNLEXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNLXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNOXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNPXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNSXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPNZXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPOXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPPXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPSXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMPZXADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_CMPCCXADD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_ENQCMD</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | ENQCMD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_ENQCMD not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | ENQCMDS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_ENQCMD not reported by this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault … |

</details>

<details><summary><b>APX_F_INVPCID</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | INVPCID | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_INVPCID not reported by this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |

</details>

<details><summary><b>APX_F_LZCNT</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | LZCNT | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_LZCNT not reported by this CPU; APX_F_LZCNT_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no … |

</details>

<details><summary><b>APX_F_MOVBE</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MOVBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_MOVBE not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_MOVDIR64B</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MOVDIR64B | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_MOVDIR64B not reported by this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |

</details>

<details><summary><b>APX_F_MOVDIRI</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MOVDIRI | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_MOVDIRI not reported by this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |

</details>

<details><summary><b>APX_F_MOVRS</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MOVRS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_MOVRS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_MSR_IMM</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | RDMSR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_MSR_IMM not reported by this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault… |
| ❌ | WRMSRNS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_MSR_IMM not reported by this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault… |

</details>

<details><summary><b>APX_F_N3</b> (84 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | CCMPB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPF | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPLE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPNB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPNBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPNL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPNLE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPNO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPNS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPNZ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPT | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CCMPZ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVLE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVNB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVNBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVNL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVNLE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVNO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVNP | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVNS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVNZ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVP | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CFCMOVZ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVA | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVAE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVG | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVGE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVLE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVNE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVNO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVNP | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVNS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVP | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CMOVS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTF | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTLE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTNB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTNBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTNL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTNLE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTNO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTNS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTNZ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTT | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | CTESTZ | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | POP2 | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | POP2P | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | PUSH2 | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | PUSH2P | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETA | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETAE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETB | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETBE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETG | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETGE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETL | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETLE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETNE | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETNO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETNP | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETNS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETO | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETP | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | SETS | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_POPCNT</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | POPCNT | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_POPCNT not reported by this CPU; APX_F_POPCNT_N3 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (n… |

</details>

<details><summary><b>APX_F_RAO_INT</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | AADD | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_RAO_INT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | AAND | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_RAO_INT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | AOR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_RAO_INT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | AXOR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_RAO_INT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_USER_MSR</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | URDMSR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_USER_MSR not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | UWRMSR | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_USER_MSR not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>APX_F_VMX</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | INVEPT | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_VMX not reported by this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |
| ❌ | INVVPID | evex | - | NOT SUPPORTED on our i5-13600K (APX_F_VMX not reported by this CPU) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in … |

</details>

<details><summary><b>AVX10_2_BF16</b> (29 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VADDBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCOMISBF16 | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VDIVBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD132BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD213BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD231BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB132BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB213BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB231BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD132BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD213BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD231BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB132BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB213BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB231BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFPCLASSBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VGETEXPBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VGETMANTBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMAXBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMULBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRCPBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VREDUCEBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRNDSCALEBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRSQRTBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSCALEFBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSQRTBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSUBBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_2_BF16 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX10_MOVRS</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VMOVRSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_MOVRS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMOVRSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_MOVRS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMOVRSQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_MOVRS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMOVRSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_MOVRS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX10_V2_AUX</b> (21 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VCVTBF42HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBF62HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBF82BF4S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBF82BF6S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBF82PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBIASPS2BF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBIASPS2BF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBIASPS2HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBIASPS2HF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTHF62HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTHF82BF4S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTHF82HF6S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTHF82PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPS2BF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPS2BF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPS2HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPS2HF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTROPS2HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTROPS2HF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPMOVSSDB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VUNPACKB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX10_V2_AUX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512BW</b> (112 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | KADDD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KADDQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KANDD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KANDND | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KANDNQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KANDQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KMOVD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KMOVQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KNOTD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KNOTQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KORD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KORQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KORTESTD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KORTESTQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KSHIFTLD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KSHIFTLQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KSHIFTRD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KSHIFTRQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KTESTD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KTESTQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KUNPCKDQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KUNPCKWD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KXNORD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KXNORQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KXORD | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KXORQ | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VDBPSADBW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVDQU16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (24 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVDQU8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (24 forms #UD; the i5-13600K lacks it) |
| ❌ | VPABSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPABSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPACKSSDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPACKSSWB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPACKUSDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPACKUSWB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPADDB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPADDSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPADDSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPADDUSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPADDUSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPADDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPALIGNR | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPAVGB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPAVGW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPBLENDMB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPBLENDMW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPBROADCASTB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPBROADCASTW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCMPB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCMPEQB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCMPEQW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCMPGTB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCMPGTW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCMPUB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCMPUW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCMPW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMI2W | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMT2W | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPEXTRB | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VPEXTRW | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VPINSRB | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VPINSRW | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMADDUBSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMADDWD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMAXSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMAXSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMAXUB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMAXUW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMINSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMINSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMINUB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMINUW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVB2M | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVM2B | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVM2W | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSWB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSXBW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVUSWB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVW2M | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVWB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVZXBW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMULHRSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMULHUW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMULHW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMULLW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSADBW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHUFB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHUFHW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHUFLW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSLLDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSLLVW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSLLW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (36 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRAVW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRAW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (36 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRLDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRLVW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRLW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (36 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSUBB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSUBSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSUBSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSUBUSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSUBUSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSUBW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPTESTMB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPTESTMW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPTESTNMB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPTESTNMW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VPUNPCKHBW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPUNPCKHWD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPUNPCKLBW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPUNPCKLWD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[30] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512CD</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPBROADCASTMB2Q | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[28] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPBROADCASTMW2D | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[28] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCONFLICTD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[28] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCONFLICTQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[28] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPLZCNTD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[28] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPLZCNTQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[28] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512DQ</b> (69 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | KADDB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KADDW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KANDB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KANDNB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KMOVB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KNOTB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KORB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KORTESTB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KSHIFTLB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KSHIFTRB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KTESTB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KTESTW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KXNORB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KXORB | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VANDNPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VANDNPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VANDPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VANDPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTF32X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTF32X8 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTF64X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTI32X2 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTI32X8 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTI64X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPD2QQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPD2UQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPS2QQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPS2UQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTQQ2PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTQQ2PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTPD2QQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTPD2UQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTPS2QQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTPS2UQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTUQQ2PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTUQQ2PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTF32X8 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (5 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTF64X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (10 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTI32X8 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (5 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTI64X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (10 forms #UD; the i5-13600K lacks it) |
| ❌ | VFPCLASSPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (14 forms #UD; the i5-13600K lacks it) |
| ❌ | VFPCLASSPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (14 forms #UD; the i5-13600K lacks it) |
| ❌ | VFPCLASSSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (4 forms #UD; the i5-13600K lacks it) |
| ❌ | VFPCLASSSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (4 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTF32X8 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTF64X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTI32X8 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTI64X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VORPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VORPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPEXTRD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VPEXTRQ | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VPINSRD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VPINSRQ | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVD2M | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVM2D | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVM2Q | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVQ2M | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMULLQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VRANGEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VRANGEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VRANGESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VRANGESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VREDUCEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VREDUCEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VREDUCESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VREDUCESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VXORPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VXORPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[17] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512ER</b> (10 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VEXP2PD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXP2PS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VRCP28PD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VRCP28PS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VRCP28SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VRCP28SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VRSQRT28PD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VRSQRT28PS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VRSQRT28SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VRSQRT28SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[27] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512F</b> (479 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | KANDNW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KANDW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KMOVW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KNOTW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KORTESTW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KORW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KSHIFTLW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KSHIFTRW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KUNPCKBW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KXNORW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | KXORW | vex | - | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VADDPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VADDPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VADDSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VADDSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VALIGND | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VALIGNQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VBLENDMPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VBLENDMPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTF32X4 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTF64X4 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTI32X4 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTI64X4 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VBROADCASTSD | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VBROADCASTSS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VCMPEQ_OSPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_OSPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_OSSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_OSSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_UQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_UQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_UQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_UQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_USPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_USPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_USSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQ_USSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPEQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPFALSE_OSPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (13 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPFALSE_OSPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (13 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPFALSE_OSSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPFALSE_OSSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPFALSEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPFALSEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPFALSESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPFALSESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGE_OQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGE_OQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGE_OQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGE_OQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGT_OQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGT_OQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGT_OQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGT_OQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGTPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGTPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGTSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPGTSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLE_OQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLE_OQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLE_OQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLE_OQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLT_OQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLT_OQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLT_OQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLT_OQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLTPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLTPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLTSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPLTSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_OQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_OQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_OQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_OQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_OSPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_OSPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_OSSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_OSSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_USPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_USPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_USSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQ_USSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNEQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGE_UQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGE_UQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGE_UQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGE_UQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGT_UQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGT_UQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGT_UQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGT_UQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGTPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGTPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGTSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNGTSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLE_UQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLE_UQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLE_UQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLE_UQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLT_UQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLT_UQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLT_UQSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLT_UQSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLTPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLTPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLTSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPNLTSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPORD_SPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPORD_SPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPORD_SSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPORD_SSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPORDPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPORDPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPORDSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPORDSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPTRUE_USPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPTRUE_USPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPTRUE_USSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPTRUE_USSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPTRUEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPTRUEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPTRUESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPTRUESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPUNORD_SPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPUNORD_SPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPUNORD_SSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPUNORD_SSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPUNORDPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPUNORDPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (7 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPUNORDSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCMPUNORDSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCOMISD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCOMISS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VCOMPRESSPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VCOMPRESSPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTDQ2PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTDQ2PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPD2DQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPD2PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPD2UDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPH2PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (21 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPS2DQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPS2PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPS2PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTPS2UDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTSD2SI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTSD2SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTSD2USI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTSI2SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTSI2SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTSS2SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTSS2SI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTSS2USI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTPD2DQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTPD2UDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTPS2DQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTPS2UDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTSD2SI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTSD2USI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTSS2SI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTTSS2USI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTUDQ2PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTUDQ2PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTUSI2SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (5 forms #UD; the i5-13600K lacks it) |
| ❌ | VCVTUSI2SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VDIVPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VDIVPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VDIVSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VDIVSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXPANDPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXPANDPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTF32X4 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (10 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTF64X4 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (5 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTI32X4 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (10 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTI64X4 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (5 forms #UD; the i5-13600K lacks it) |
| ❌ | VEXTRACTPS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VFIXUPIMMPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFIXUPIMMPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFIXUPIMMSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFIXUPIMMSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD132PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD132PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD132SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD132SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD213PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD213PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD213SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD213SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD231PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD231PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD231SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADD231SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADDSUB132PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADDSUB132PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADDSUB213PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADDSUB213PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADDSUB231PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMADDSUB231PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB132PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB132PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB132SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB132SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB213PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB213PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB213SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB213SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB231PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB231PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB231SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUB231SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUBADD132PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUBADD132PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUBADD213PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUBADD213PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUBADD231PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFMSUBADD231PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD132PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD132PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD132SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD132SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD213PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD213PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD213SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD213SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD231PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD231PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD231SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMADD231SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB132PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB132PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB132SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB132SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB213PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB213PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB213SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB213SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB231PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB231PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB231SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VFNMSUB231SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERDPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERDPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VGETEXPPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VGETEXPPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VGETEXPSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VGETEXPSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VGETMANTPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VGETMANTPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VGETMANTSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VGETMANTSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTF32X4 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTF64X4 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTI32X4 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTI64X4 | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VINSERTPS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VMAXPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMAXPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMAXSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VMAXSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VMINPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMINPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMINSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VMINSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVAPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMOVAPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMOVD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU; AVX512_MOVZXC not reported by this CPU) — not implemented yet: not implemented (4 forms #UD; the i5-136… |
| ❌ | VMOVDDUP | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVDQA32 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMOVDQA64 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMOVDQU32 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMOVDQU64 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMOVHLPS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVHPD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVHPS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVLHPS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVLPD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVLPS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (2 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVNTDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVNTDQA | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVNTPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVNTPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVQ | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (5 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (8 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVSHDUP | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVSLDUP | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (8 forms #UD; the i5-13600K lacks it) |
| ❌ | VMOVUPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMOVUPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMULPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMULPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VMULSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VMULSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VPABSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPABSQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPADDD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPADDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPANDD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPANDND | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPANDNQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPANDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPBLENDMD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPBLENDMQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPBROADCASTD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPBROADCASTQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCMPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCMPEQD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCMPEQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCMPGTD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCMPGTQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCMPQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCMPUD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCMPUQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPCOMPRESSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCOMPRESSQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMD | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMI2D | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMI2PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMI2PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMI2Q | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMILPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (54 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMILPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (54 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMPD | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (36 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMPS | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMQ | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (36 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMT2D | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMT2PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMT2PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMT2Q | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPEXPANDD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPEXPANDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPGATHERDD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPGATHERDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPGATHERQD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPGATHERQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMAXSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMAXSQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMAXUD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMAXUQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMINSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMINSQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMINUD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMINUQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMOVDB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVQB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVQD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVQW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSDB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSQB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSQD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSQW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSXBD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSXBQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSXDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSXWD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVSXWQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVUSDB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVUSDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVUSQB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVUSQD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVUSQW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVZXBD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVZXBQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVZXDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVZXWD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMOVZXWQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMULDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMULLD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPMULUDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPORD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPORQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPROLD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPROLQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPROLVD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPROLVQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPRORD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPRORQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPRORVD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPRORVQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSCATTERDD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSCATTERDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSCATTERQD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSCATTERQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHUFD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSLLD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (45 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSLLQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (45 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSLLVD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPSLLVQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPSRAD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (45 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRAQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (45 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRAVD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPSRAVQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPSRLD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (45 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRLQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (45 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSRLVD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPSRLVQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPSUBD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPSUBQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPTERNLOGD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPTERNLOGQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPTESTMD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPTESTMQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPTESTNMD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPTESTNMQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPUNPCKHDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPUNPCKHQDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPUNPCKLDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPUNPCKLQDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPXORD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VPXORQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VRCP14PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VRCP14PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VRCP14SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VRCP14SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VRNDSCALEPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VRNDSCALEPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VRNDSCALESD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VRNDSCALESS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VRSQRT14PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VRSQRT14PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VRSQRT14SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VRSQRT14SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCALEFPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCALEFPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (30 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCALEFSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCALEFSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERDPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERDPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERQPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERQPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VSHUFF32X4 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VSHUFF64X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VSHUFI32X4 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VSHUFI64X2 | evex | 256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VSHUFPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VSHUFPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VSQRTPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VSQRTPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VSQRTSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VSQRTSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VSUBPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VSUBPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — implemented per the manual (SDM-vector verified): implemented; the i5-13600K lacks it: verified again… |
| ❌ | VSUBSD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VSUBSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (9 forms #UD; the i5-13600K lacks it) |
| ❌ | VUCOMISD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VUCOMISS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VUNPCKHPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VUNPCKHPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VUNPCKLPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VUNPCKLPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[16] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512PF</b> (16 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VGATHERPF0DPD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERPF0DPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERPF0QPD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERPF0QPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERPF1DPD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERPF1DPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERPF1QPD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VGATHERPF1QPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERPF0DPD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERPF0DPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERPF0QPD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERPF0QPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERPF1DPD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERPF1DPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERPF1QPD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |
| ❌ | VSCATTERPF1QPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[26] = 0 on this CPU) — not implemented yet: not implemented (1 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512_4FMAPS</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | V4FMADDPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[3] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | V4FMADDSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[3] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | V4FNMADDPS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[3] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | V4FNMADDSS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[3] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512_4VNNIW</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VP4DPWSSD | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[2] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |
| ❌ | VP4DPWSSDS | evex | 512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[2] = 0 on this CPU) — not implemented yet: not implemented (3 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512_BF16</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VCVTNE2PS2BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[5] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTNEPS2BF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[5] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VDPBF16PS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H.1:EAX[5] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_BITALG</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPOPCNTB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[12] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPOPCNTW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[12] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHUFBITQMB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[12] = 0 on this CPU) — not implemented yet: not implemented (12 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512_COM_EF</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VCOMXSD | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_COM_EF not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCOMXSH | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_COM_EF not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCOMXSS | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_COM_EF not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VUCOMXSD | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_COM_EF not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VUCOMXSH | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_COM_EF not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VUCOMXSS | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_COM_EF not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_FP16</b> (170 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VADDPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VADDSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPEQ_OSPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPEQ_OSSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPEQ_UQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPEQ_UQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPEQ_USPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPEQ_USSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPEQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPEQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPFALSE_OSPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPFALSE_OSSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPFALSEPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPFALSESH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPGE_OQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPGE_OQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPGEPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPGESH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPGT_OQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPGT_OQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPGTPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPGTSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPLE_OQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPLE_OQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPLEPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPLESH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPLT_OQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPLT_OQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPLTPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPLTSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNEQ_OQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNEQ_OQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNEQ_OSPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNEQ_OSSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNEQ_USPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNEQ_USSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNEQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNEQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNGE_UQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNGE_UQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNGEPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNGESH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNGT_UQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNGT_UQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNGTPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNGTSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNLE_UQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNLE_UQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNLEPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNLESH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNLT_UQPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNLT_UQSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNLTPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPNLTSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPORD_SPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPORD_SSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPORDPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPORDSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPTRUE_USPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPTRUE_USSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPTRUEPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPTRUESH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPUNORD_SPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPUNORD_SSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPUNORDPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCMPUNORDSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCOMISH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTDQ2PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPD2PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2DQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2PD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2PSX | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2QQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2UDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2UQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2UW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2W | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPS2PHX | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTQQ2PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTSD2SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTSH2SD | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTSH2SI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTSH2SS | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTSH2USI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTSI2SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTSS2SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPH2DQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPH2QQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPH2UDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPH2UQQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPH2UW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPH2W | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTSH2SI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTSH2USI | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTUDQ2PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTUQQ2PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTUSI2SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTUW2PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTW2PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VDIVPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VDIVSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFCMADDCPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFCMADDCSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFCMULCPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFCMULCSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD132PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD132SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD213PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD213SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD231PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADD231SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADDCPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADDCSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADDSUB132PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADDSUB213PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMADDSUB231PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB132PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB132SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB213PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB213SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB231PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUB231SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUBADD132PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUBADD213PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMSUBADD231PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMULCPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFMULCSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD132PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD132SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD213PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD213SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD231PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMADD231SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB132PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB132SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB213PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB213SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB231PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFNMSUB231SH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFPCLASSPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VFPCLASSSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VGETEXPPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VGETEXPSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VGETMANTPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VGETMANTSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMAXPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMAXSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMOVSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMOVW | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU; AVX512_MOVZXC not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capst… |
| ❌ | VMULPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMULSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRCPPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRCPSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VREDUCEPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VREDUCESH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRNDSCALEPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRNDSCALESH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRSQRTPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VRSQRTSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSCALEFPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSCALEFSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSQRTPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSQRTSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSUBPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VSUBSH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VUCOMISH | evex | 128 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_FP16_CONVERT</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VCVT2PS2PHX | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[23] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_FP8_CONVERT</b> (13 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VCVT2PH2BF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVT2PH2BF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVT2PH2HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVT2PH2HF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTBIASPH2BF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTBIASPH2BF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTBIASPH2HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTBIASPH2HF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTHF82PH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTPH2BF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTPH2BF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTPH2HF8 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |
| ❌ | VCVTPH2HF8S | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_FP8_CONVERT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.… |

</details>

<details><summary><b>AVX512_GFNI</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VGF2P8AFFINEINVQB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_GFNI not reported by this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |
| ❌ | VGF2P8AFFINEQB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_GFNI not reported by this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |
| ❌ | VGF2P8MULB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_GFNI not reported by this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |

</details>

<details><summary><b>AVX512_IFMA</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPMADD52HUQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[21] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMADD52LUQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EBX[21] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512_MEDIAX</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VMPSADBW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_MEDIAX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_MINMAX</b> (7 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VMINMAXBF16 | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_MINMAX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINMAXPD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_MINMAX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINMAXPH | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_MINMAX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINMAXPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_MINMAX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINMAXSD | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_MINMAX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINMAXSH | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_MINMAX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VMINMAXSS | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_MINMAX not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_SAT_CVT</b> (12 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VCVTBF162IBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTBF162IUBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2IBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPH2IUBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPS2IBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTPS2IUBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTBF162IBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTBF162IUBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPH2IBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPH2IUBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPS2IBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VCVTTPS2IUBS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_SAT_CVT_DS</b> (12 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VCVTTPD2DQS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTPD2QQS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTPD2UDQS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTPD2UQQS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTPS2DQS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTPS2QQS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTPS2UDQS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTPS2UQQS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTSD2SIS | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTSD2USIS | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTSS2SIS | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |
| ❌ | VCVTTSS2USIS | evex | 128 | NOT SUPPORTED on our i5-13600K (AVX512_SAT_CVT_DS not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3… |

</details>

<details><summary><b>AVX512_VAES</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VAESDEC | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_VAES not reported by this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VAESDECLAST | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_VAES not reported by this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VAESENC | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_VAES not reported by this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |
| ❌ | VAESENCLAST | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_VAES not reported by this CPU) — not implemented yet: not implemented (6 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512_VBMI</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPERMB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[1] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMI2B | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[1] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPERMT2B | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[1] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPMULTISHIFTQB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[1] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512_VBMI2</b> (16 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPCOMPRESSB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPCOMPRESSW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (15 forms #UD; the i5-13600K lacks it) |
| ❌ | VPEXPANDB | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPEXPANDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHLDD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHLDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHLDVD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHLDVQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHLDVW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHLDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHRDD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHRDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHRDVD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHRDVQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHRDVW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |
| ❌ | VPSHRDW | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[6] = 0 on this CPU) — not implemented yet: not implemented (18 forms #UD; the i5-13600K lacks it) |

</details>

<details><summary><b>AVX512_VNNI</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPDPBUSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |
| ❌ | VPDPBUSDS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |
| ❌ | VPDPWSSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |
| ❌ | VPDPWSSDS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |

</details>

<details><summary><b>AVX512_VNNI_FP16</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VDPPHPS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_VNNI_INT16</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPDPWSUD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPWSUDS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPWUSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPWUSDS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPWUUD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPWUUDS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_VNNI_INT8</b> (6 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPDPBSSD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPBSSDS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPBSUD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPBSUDS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPBUUD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VPDPBUUDS | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[11] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_VP2INTERSECT</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VP2INTERSECTD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[8] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | VP2INTERSECTQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:EDX[8] = 0 on this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AVX512_VPCLMULQDQ</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPCLMULQDQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (AVX512_VPCLMULQDQ not reported by this CPU) — not implemented yet: EVEX form not implemented (the i5-13600K lacks AVX-512) |

</details>

<details><summary><b>AVX512_VPOPCNTDQ</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VPOPCNTD | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[14] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |
| ❌ | VPOPCNTQ | evex | 128/256/512 | NOT SUPPORTED on our i5-13600K (CPUID.7H:ECX[14] = 0 on this CPU) — not implemented yet: not implemented (27 forms #UD; the i5-13600K lacks it) |

</details>

