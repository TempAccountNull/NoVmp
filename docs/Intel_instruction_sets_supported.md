# Intel instruction sets supported by the NoVmp emulator

_Generated 2026-10-10 06:42 from `Emulator/tools/isa/gen_status_docs.py` (HEAD `07a44976 U1064: alltest baseline: syscall, wrmsr, rdmsr, sysretq rows after the sysmsr merge (U901, U903, U905)`). Do not edit by hand._

**How the page is split.** The first part lists only instructions **your i5-13600K can run** (columns **Done** / **Implementing**). Everything your CPU **cannot honestly run** (CPUID bit clear, AMD/VIA-only, or disabled by Windows) is listed separately below under **"Instructions that can't be supported for now:"**, with its own **CPU cannot support** column giving the reason — those rows are never marked as supported by your CPU; the emulator still implements them per the Intel manual and verifies them against SDM-pseudocode vectors. **Done** = ✅ identical to your i5-13600K (or, in the cannot-support part, ✅ per the manual). **Implementing** = ⏳ being implemented now (agent named) or implemented with an open item, ⬜ queued (not started).

Source of truth: every instruction form of the Intel SDM / XED list (`Emulator/data/isa_manual_forms.tsv`), checked against an Intel i5-13600K (Raptor Lake) with `emu-alltest` (hardware sweeps, `--cases` files) and, for instructions this CPU lacks, against expected values derived from the SDM pseudocode.

**Totals (Intel families):** ✅ 1238 · ⏳ 64 · ⬜ 0 · ❌ 1380 (of which implemented per the manual 1293, open item 51, not implemented yet 36) — 2682 forms

## Currently being added

  - ⏳ Last Phase-1 wave (started 2026-10-09 after the audit, base ab67266; besides wt/fix4 U770–U789 and wt/apx_cases U790–U799):
      - ⬜ regs leftovers: scalar FMA (VFMADD*SS/SD …) memory operand reads 16 bytes (decoded W,x: wrong #PF near a page end, missing #AC); an MMX memory operand that takes #PF still applies the x87→MMX transition (TOP=0, tags valid); CVTPD2PI/CVTTPD2PI m128 misaligned: CPU #GP, Unicorn none; #AC not covered for VCOMPRESS/VPCOMPRESS stores and MPX BND memory forms; uc_context_reg_write of CR0/CR3/CR4/EFER/MSR into a context calls live-CPU update functions; two CPU-vs-SDM deviations need your sign-off (MASKMOVDQU-AC, SxDT-STR-SMSW-NO-AC, quirks.md).
      - ⬜ sweepmem leftovers: SYSENTER without a hook advances RIP from a stale value when it is not the first instruction of its TB (with A1); INT n needs an IDT-delivery mode (U755 leftover); LFS/LGS SDM text "and" vs pseudocode "or" for RPL/CPL > DPL (model follows the pseudocode); rename the "host lacks + unicorn #UD" bucket (also covers encodings both reject).
  - ⏳ 1.15d EVEX / AVX-512: M0–M5 merged; open = VPMOVD2M/Q2M/M2D/M2Q (not implemented), Xeon-Phi families (A3) [audit 2026-10-09]
      - ⬜ M2 leftovers: scatter hook (U866), disp8 note (U867), ev_dsrc (U868) done; open: helper_evex_mstore pre-pass stores an unmapped element twice for the write hook, some store probes report READ_UNMAPPED instead of WRITE_UNMAPPED (code reading, add a test); E4NF/E6NF unconfirmable without an AVX-512 host.
      - ⬜ VRCP14/VRSQRT14 (U236) are a correctly-rounded stand-in, not bit-exact: the SDM gives only the 2^-14 bound and refers to RECIP14.c (not available) — derive Intel's table-free algorithm analytically like U81 (RCPPS/RSQRTPS) did, or keep as an open item; never a measured table.
      - ⬜ M3 leftovers: VPMOVD2M/VPMOVQ2M/VPMOVM2D/VPMOVM2Q done (U800); VREDUCE DAZ on C4 watch list; BW 32-bit → 1.I [audit 2026-10-09]
      - ⬜ M4 leftovers: Xeon-Phi families (A3); EVEX_VAES macro readability; 32-bit → 1.I [audit 2026-10-09]
      - ⬜ fp16 leftovers: scalar FP16 L'L=11b = LIG per SDM (U869 cases; XED rejects it — if hardware ever shows #UD, quirks.md for SS/SD/SH); VRCP/VRSQRT PH → A9; ambiguities C4.
    - ⬜ APX leftovers: INVPCID + MSR-IMM done (U803/U806); sweep for promoted maps 1/2/3/7; INVEPT/INVVPID (VMX decision); VMX/SMM/LBR/PT not modelled; CPUID.29H:EBX[0] done (U793) [audit 2026-10-09]
      - ⬜ AMX leftovers 2: AMX-TF32 (decision); UINTR XSAVES component; APX TILELOADDRS EVEX.R4 check; cases_amx2 run with --avx10 only; PT/PASID/HDC/LBR/HWP components not modelled [audit 2026-10-09]
      - ⬜ Key Locker leftovers: KeySource 1 (via the D8 RNG source), IWKeyBackup MSRs, MSR_FEATURE_CONFIG gate; AESKLE in SMM deferred [audit 2026-10-09]
      - ⬜ UINTR leftovers: IF=0 pending notification, XSAVES component 14, CET effects, STI/MOV SS shadow; APIC/x2APIC parts need a decision (local-APIC model) [audit 2026-10-09]
      - ⬜ Fixes leftovers: LOCK 0F 0D verified (U457) + MPX BNDCFG fixed (U782/U783); drop redundant U129 KMOV GPR mask; 32-bit hw + VEX.W 32-bit → 1.I. VEX 16-bit PM done (U484) [audit 2026-10-09]
      - ⬜ CET leftovers 2: SYSCALL/SYSENTER CET (A1); IDT-delivery mode → Phase 3; PKS on shadow-stack accesses: verify vs SDM + case (PKS walk is U479); compat/legacy → 1.I [audit 2026-10-09]
      - ⬜ SGX "present but disabled" model and GETSEC TXT leaves (decision, new); PCONFIG leaf 1BH (A4) [audit 2026-10-09]

## Instructions your i5-13600K can run (1302 forms)

### By family

| family | forms | **Done** | **Implementing** |
|---|---|---|---|
| ? | 17 | ✅ 17 |  |
| ADOX_ADCX | 2 | ✅ 2 |  |
| AES | 6 | ✅ 6 |  |
| AVX | 381 | ✅ 381 |  |
| AVX2 | 20 | ✅ 20 |  |
| AVX2GATHER | 8 | ✅ 8 |  |
| AVXAES | 6 | ✅ 6 |  |
| AVX_GFNI | 3 | ✅ 3 |  |
| AVX_VNNI | 4 | ✅ 4 |  |
| BMI1 | 6 | ✅ 6 |  |
| BMI2 | 8 | ✅ 8 |  |
| CET | 4 | ✅ 4 |  |
| CLFLUSHOPT | 1 | ✅ 1 |  |
| CLFSH | 1 | ✅ 1 |  |
| CLWB | 1 | ✅ 1 |  |
| CMOV | 16 | ✅ 16 |  |
| CMPXCHG16B | 1 | ✅ 1 |  |
| F16C | 2 | ✅ 2 |  |
| FAT_NOP | 8 | ✅ 8 |  |
| FCMOV | 9 | ✅ 9 |  |
| FCOMI | 6 | ✅ 6 |  |
| FMA | 60 | ✅ 60 |  |
| FXSAVE | 2 | ✅ 2 |  |
| FXSAVE64 | 2 | ✅ 2 |  |
| GFNI | 3 | ✅ 3 |  |
| I186 | 19 | ✅ 15 | ⏳ 4 |
| I286PROTECTED | 9 | ✅ 5 | ⏳ 4 |
| I286REAL | 7 |  | ⏳ 7 |
| I386 | 47 | ✅ 44 | ⏳ 3 |
| I486 | 1 |  | ⏳ 1 |
| I486REAL | 7 | ✅ 4 | ⏳ 3 |
| I86 | 88 | ✅ 79 | ⏳ 9 |
| INVPCID | 1 | ✅ 1 |  |
| LAHF | 2 | ✅ 2 |  |
| LONGMODE | 14 | ✅ 11 | ⏳ 3 |
| MOVBE | 1 | ✅ 1 |  |
| MOVDIR64B | 1 | ✅ 1 |  |
| MOVDIRI | 1 | ✅ 1 |  |
| PAUSE | 1 | ✅ 1 |  |
| PCLMULQDQ | 1 | ✅ 1 |  |
| PENTIUMMMX | 60 | ✅ 60 |  |
| PENTIUMREAL | 4 | ✅ 1 | ⏳ 3 |
| POPCNT | 1 | ✅ 1 |  |
| PPRO | 2 | ✅ 2 |  |
| PPRO_UD0_LONG | 1 | ✅ 1 |  |
| PREFETCH_NOP | 2 | ✅ 2 |  |
| PTWRITE | 1 | ✅ 1 |  |
| RDPID | 1 |  | ⏳ 1 |
| RDPMC | 1 |  | ⏳ 1 |
| RDRAND | 1 | ✅ 1 |  |
| RDSEED | 1 | ✅ 1 |  |
| RDTSCP | 1 |  | ⏳ 1 |
| RDWRFSGS | 4 | ✅ 3 | ⏳ 1 |
| SEP | 3 |  | ⏳ 3 |
| SERIALIZE | 1 | ✅ 1 |  |
| SHA | 7 | ✅ 7 |  |
| SMAP | 2 |  | ⏳ 2 |
| SSE | 110 | ✅ 110 |  |
| SSE2 | 129 | ✅ 129 |  |
| SSE3 | 10 | ✅ 10 |  |
| SSE3X87 | 1 | ✅ 1 |  |
| SSE4 | 48 | ✅ 48 |  |
| SSE42 | 6 | ✅ 6 |  |
| SSEMXCSR | 2 | ✅ 2 |  |
| SSE_PREFETCH | 4 | ✅ 4 |  |
| SSSE3 | 16 | ✅ 16 |  |
| VMFUNC | 1 |  | ⏳ 1 |
| VTX | 12 |  | ⏳ 12 |
| X87 | 79 | ✅ 79 |  |
| XSAVE | 6 | ✅ 5 | ⏳ 1 |
| XSAVEC | 2 | ✅ 2 |  |
| XSAVEOPT | 2 | ✅ 2 |  |
| XSAVES | 4 |  | ⏳ 4 |

### Per instruction

<details><summary><b>?</b> (17 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| ADDR32 | legacy | - | ✅ identical to the i5-13600K (cases_reach): 67 prefix (LEA eax, [esi]) |  |
| BND | legacy | - | ✅ identical to the i5-13600K (cases_reach): F2 on branches: plain branch without MPX |  |
| DATA16 | legacy | - | ✅ identical to the i5-13600K (cases_reach): 66 prefix |  |
| FCLEX | legacy | - | ✅ identical to the i5-13600K (cases_reach): 9B DB E2 (FWAIT + FNCLEX; pending #MF with CR0.NE) |  |
| FINIT | legacy | - | ✅ identical to the i5-13600K (cases_reach): 9B DB E3 (FWAIT + FNINIT) |  |
| FSAVE | legacy | - | ✅ U61-U64 |  |
| FSTCW | legacy | - | ✅ identical to the i5-13600K (cases_reach): 9B D9 /7 (FWAIT + FNSTCW) |  |
| FSTENV | legacy | - | ✅ U61/U62/U64 |  |
| FSTSW | legacy | - | ✅ identical to the i5-13600K (cases_reach): 9B DD /7, 9B DF E0 (FWAIT + FNSTSW; FIP per U90) |  |
| LOCK | legacy | - | ✅ identical to the i5-13600K (cases_reach): F0 prefix (memory: locked; register: #UD) |  |
| NOTRACK | legacy | - | ✅ identical to the i5-13600K (cases_reach): 3E on indirect JMP/CALL: ignored (IBT not enabled) |  |
| REP | legacy | - | ✅ identical to the i5-13600K (cases_reach): F3 prefix (string ops) |  |
| REPE | legacy | - | ✅ identical to the i5-13600K (cases_reach): F3 prefix (CMPS/SCAS) |  |
| REPNE | legacy | - | ✅ identical to the i5-13600K (cases_reach): F2 prefix (CMPS/SCAS) |  |
| REPNZ | legacy | - | ✅ identical to the i5-13600K (cases_reach): F2 prefix (CMPS/SCAS) |  |
| REPZ | legacy | - | ✅ identical to the i5-13600K (cases_reach): F3 prefix (CMPS/SCAS) |  |
| REX64 | legacy | - | ✅ identical to the i5-13600K (cases_reach): REX.W prefix |  |

</details>

<details><summary><b>ADOX_ADCX</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| ADCX | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| ADOX | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |

</details>

<details><summary><b>AES</b> (6 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| AESDEC | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| AESDECLAST | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| AESENC | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| AESENCLAST | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| AESIMC | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| AESKEYGENASSIST | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>AVX</b> (381 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VADDPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VADDPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VADDSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VADDSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VADDSUBPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VADDSUBPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VANDNPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VANDNPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VANDPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VANDPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VBLENDPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VBLENDPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VBLENDVPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VBLENDVPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VBROADCASTF128 | vex | 256 | ✅ identical to the i5-13600K (1 forms) |  |
| VBROADCASTSD | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VBROADCASTSS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCMPEQ_OSPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPEQ_OSPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPEQ_OSSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPEQ_OSSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPEQ_UQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPEQ_UQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPEQ_UQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPEQ_UQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPEQ_USPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPEQ_USPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPEQ_USSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPEQ_USSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPEQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPEQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPEQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPEQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPFALSE_OSPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPFALSE_OSPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPFALSE_OSSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPFALSE_OSSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPFALSEPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPFALSEPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPFALSESD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPFALSESS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPGE_OQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPGE_OQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPGE_OQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPGE_OQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPGEPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPGEPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPGESD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPGESS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPGT_OQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPGT_OQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPGT_OQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPGT_OQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPGTPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPGTPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPGTSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPGTSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPLE_OQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPLE_OQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPLE_OQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPLE_OQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPLEPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPLEPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPLESD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPLESS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPLT_OQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPLT_OQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPLT_OQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPLT_OQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPLTPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPLTPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPLTSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPLTSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNEQ_OQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNEQ_OQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNEQ_OQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNEQ_OQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNEQ_OSPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNEQ_OSPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNEQ_OSSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNEQ_OSSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNEQ_USPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNEQ_USPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNEQ_USSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNEQ_USSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNEQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNEQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNEQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNEQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNGE_UQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNGE_UQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNGE_UQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNGE_UQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNGEPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNGEPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNGESD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNGESS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNGT_UQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNGT_UQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNGT_UQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNGT_UQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNGTPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNGTPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNGTSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNGTSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNLE_UQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNLE_UQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNLE_UQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNLE_UQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNLEPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNLEPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNLESD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNLESS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNLT_UQPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNLT_UQPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNLT_UQSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNLT_UQSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPNLTPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPNLTPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPNLTSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPNLTSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPORD_SPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPORD_SPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPORD_SSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPORD_SSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPORDPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPORDPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPORDSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPORDSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCMPPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCMPSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VCMPSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VCMPTRUE_USPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPTRUE_USPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPTRUE_USSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPTRUE_USSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPTRUEPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPTRUEPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPTRUESD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPTRUESS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPUNORD_SPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPUNORD_SPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPUNORD_SSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPUNORD_SSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCMPUNORDPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPD imm8 predicate alias |  |
| VCMPUNORDPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) — VCMPPS imm8 predicate alias |  |
| VCMPUNORDSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSD imm8 predicate alias |  |
| VCMPUNORDSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) — VCMPSS imm8 predicate alias |  |
| VCOMISD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VCOMISS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VCVTDQ2PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTDQ2PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTPD2DQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTPD2PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTPS2DQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTPS2PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTSD2SI | vex | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTSD2SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VCVTSI2SD | vex | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTSI2SS | vex | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTSS2SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VCVTSS2SI | vex | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTTPD2DQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTTPS2DQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTTSD2SI | vex | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTTSS2SI | vex | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| VDIVPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VDIVPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VDIVSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VDIVSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VDPPD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VDPPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VEXTRACTF128 | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VEXTRACTPS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VHADDPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VHADDPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VHSUBPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VHSUBPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VINSERTF128 | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VINSERTPS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VLDDQU | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VLDMXCSR | vex | - | ✅ identical to the i5-13600K (1 forms) |  |
| VMASKMOVDQU | vex | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| VMASKMOVPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMASKMOVPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMAXPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMAXPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMAXSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMAXSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMINPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMINPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMINSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMINSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVAPD | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VMOVAPS | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VMOVD | vex | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| VMOVDDUP | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMOVDQA | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VMOVDQU | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VMOVHLPS | vex | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| VMOVHPD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVHPS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVLHPS | vex | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| VMOVLPD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVLPS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVMSKPD | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVMSKPS | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVNTDQ | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVNTDQA | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVNTPD | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVNTPS | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VMOVQ | vex | 128 | ✅ identical to the i5-13600K (5 forms) |  |
| VMOVSD | vex | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| VMOVSHDUP | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMOVSLDUP | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMOVSS | vex | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| VMOVUPD | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VMOVUPS | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VMPSADBW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMULPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMULPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VMULSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VMULSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VORPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VORPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPABSB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPABSD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPABSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPACKSSDW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPACKSSWB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPACKUSDW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPACKUSWB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPADDB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPADDD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPADDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPADDSB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPADDSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPADDUSB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPADDUSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPADDW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPALIGNR | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPAND | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPANDN | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPAVGB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPAVGW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPBLENDVB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPBLENDW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCLMULQDQ | vex | 128/256 | ✅ U69 (VEX.128/256); EVEX U574 |  |
| VPCMPEQB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCMPEQD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCMPEQQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCMPEQW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCMPESTRI | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPCMPESTRM | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPCMPGTB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCMPGTD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCMPGTQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCMPGTW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPCMPISTRI | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPCMPISTRM | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPERM2F128 | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPERMILPD | vex | 128/256 | ✅ identical to the i5-13600K (8 forms) |  |
| VPERMILPS | vex | 128/256 | ✅ identical to the i5-13600K (8 forms) |  |
| VPEXTRB | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPEXTRD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPEXTRQ | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPEXTRW | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPHADDD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPHADDSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPHADDW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPHMINPOSUW | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPHSUBD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPHSUBSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPHSUBW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPINSRB | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPINSRD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPINSRQ | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPINSRW | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VPMADDUBSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMADDWD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMAXSB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMAXSD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMAXSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMAXUB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMAXUD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMAXUW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMINSB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMINSD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMINSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMINUB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMINUD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMINUW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVMSKB | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPMOVSXBD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVSXBQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVSXBW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVSXDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVSXWD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVSXWQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVZXBD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVZXBQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVZXBW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVZXDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVZXWD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMOVZXWQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMULDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMULHRSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMULHUW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMULHW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMULLD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMULLW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMULUDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPOR | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSADBW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSHUFB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSHUFD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSHUFHW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSHUFLW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSIGNB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSIGND | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSIGNW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSLLD | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VPSLLDQ | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPSLLQ | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VPSLLW | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VPSRAD | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VPSRAW | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VPSRLD | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VPSRLDQ | vex | 128/256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPSRLQ | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VPSRLW | vex | 128/256 | ✅ identical to the i5-13600K (6 forms) |  |
| VPSUBB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSUBD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSUBQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSUBSB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSUBSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSUBUSB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSUBUSW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSUBW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPTEST | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPUNPCKHBW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPUNPCKHDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPUNPCKHQDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPUNPCKHWD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPUNPCKLBW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPUNPCKLDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPUNPCKLQDQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPUNPCKLWD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPXOR | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VRCPPS | vex | 128/256 | ✅ U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |  |
| VRCPSS | vex | 128 | ✅ U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |  |
| VROUNDPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VROUNDPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VROUNDSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VROUNDSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VRSQRTPS | vex | 128/256 | ✅ U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |  |
| VRSQRTSS | vex | 128 | ✅ U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |  |
| VSHUFPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VSHUFPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VSQRTPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VSQRTPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VSQRTSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VSQRTSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VSTMXCSR | vex | - | ✅ identical to the i5-13600K (1 forms) |  |
| VSUBPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VSUBPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VSUBSD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VSUBSS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VTESTPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VTESTPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VUCOMISD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VUCOMISS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VUNPCKHPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VUNPCKHPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VUNPCKLPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VUNPCKLPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VXORPD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VXORPS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VZEROALL | vex | - | ✅ identical to the i5-13600K (1 forms) |  |
| VZEROUPPER | vex | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>AVX2</b> (20 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VBROADCASTI128 | vex | 256 | ✅ identical to the i5-13600K (1 forms) |  |
| VEXTRACTI128 | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VINSERTI128 | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPBLENDD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPBROADCASTB | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPBROADCASTD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPBROADCASTQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPBROADCASTW | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPERM2I128 | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPERMD | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPERMPD | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPERMPS | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPERMQ | vex | 256 | ✅ identical to the i5-13600K (2 forms) |  |
| VPMASKMOVD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPMASKMOVQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSLLVD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSLLVQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSRAVD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSRLVD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VPSRLVQ | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |

</details>

<details><summary><b>AVX2GATHER</b> (8 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VGATHERDPD | vex | 128/256 | ✅ identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask =… |  |
| VGATHERDPS | vex | 128/256 | ✅ identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask =… |  |
| VGATHERQPD | vex | 128/256 | ✅ identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask =… |  |
| VGATHERQPS | vex | 128/256 | ✅ identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask =… |  |
| VPGATHERDD | vex | 128/256 | ✅ identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask =… |  |
| VPGATHERDQ | vex | 128/256 | ✅ identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask =… |  |
| VPGATHERQD | vex | 128/256 | ✅ identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask =… |  |
| VPGATHERQQ | vex | 128/256 | ✅ identical to the i5-13600K (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask =… |  |

</details>

<details><summary><b>AVXAES</b> (6 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VAESDEC | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VAESDECLAST | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VAESENC | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VAESENCLAST | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VAESIMC | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VAESKEYGENASSIST | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>AVX_GFNI</b> (3 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VGF2P8AFFINEINVQB | vex | 128/256 | ✅ U70 (VEX); EVEX U572 |  |
| VGF2P8AFFINEQB | vex | 128/256 | ✅ U70 (VEX); EVEX U572 |  |
| VGF2P8MULB | vex | 128/256 | ✅ U70 (VEX); EVEX U572 |  |

</details>

<details><summary><b>AVX_VNNI</b> (4 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VPDPBUSD | vex | 128/256 | ✅ U71 AVX-VNNI (VEX) |  |
| VPDPBUSDS | vex | 128/256 | ✅ U71 |  |
| VPDPWSSD | vex | 128/256 | ✅ U71 |  |
| VPDPWSSDS | vex | 128/256 | ✅ U71 |  |

</details>

<details><summary><b>BMI1</b> (6 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| ANDN | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| BEXTR | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| BLSI | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| BLSMSK | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| BLSR | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| TZCNT | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |

</details>

<details><summary><b>BMI2</b> (8 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| BZHI | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| MULX | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| PDEP | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| PEXT | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| RORX | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| SARX | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| SHLX | vex | - | ✅ identical to the i5-13600K (4 forms) |  |
| SHRX | vex | - | ✅ identical to the i5-13600K (4 forms) |  |

</details>

<details><summary><b>CET</b> (4 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| ENDBR32 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| ENDBR64 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| RDSSPD | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| RDSSPQ | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>CLFLUSHOPT</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CLFLUSHOPT | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>CLFSH</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CLFLUSH | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>CLWB</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CLWB | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>CMOV</b> (16 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CMOVA | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVAE | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVB | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVBE | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVE | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVG | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVGE | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVL | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVLE | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVNE | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVNO | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVNP | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVNS | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVO | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVP | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| CMOVS | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |

</details>

<details><summary><b>CMPXCHG16B</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CMPXCHG16B | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>F16C</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VCVTPH2PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VCVTPS2PH | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |

</details>

<details><summary><b>FAT_NOP</b> (8 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| NOP | legacy | - | ✅ identical to the i5-13600K (10 forms) |  |
| NOP3 | legacy | - | ✅ identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |  |
| NOP4 | legacy | - | ✅ identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |  |
| NOP5 | legacy | - | ✅ identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |  |
| NOP6 | legacy | - | ✅ identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |  |
| NOP7 | legacy | - | ✅ identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |  |
| NOP8 | legacy | - | ✅ identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |  |
| NOP9 | legacy | - | ✅ identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |  |

</details>

<details><summary><b>FCMOV</b> (9 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| FCMOVB | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCMOVBE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCMOVE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCMOVNB | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCMOVNBE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCMOVNE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCMOVNP | legacy | - | ✅ identical to the i5-13600K (1 forms) — same opcode as FCMOVNU (Capstone name) |  |
| FCMOVNU | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCMOVU | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>FCOMI</b> (6 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| FCOMI | legacy | - | ✅ SDM C1 = 0 (U531; the CPU keeps C1: docs/quirks.md) |  |
| FCOMIP | legacy | - | ✅ SDM C1 = 0 (U531, docs/quirks.md) |  |
| FCOMPI | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FUCOMI | legacy | - | ✅ SDM C1 = 0 (U531, docs/quirks.md) |  |
| FUCOMIP | legacy | - | ✅ SDM C1 = 0 (U531, docs/quirks.md) |  |
| FUCOMPI | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>FMA</b> (60 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VFMADD132PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADD132PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADD132SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMADD132SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMADD213PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADD213PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADD213SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMADD213SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMADD231PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADD231PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADD231SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMADD231SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMADDSUB132PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADDSUB132PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADDSUB213PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADDSUB213PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADDSUB231PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMADDSUB231PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUB132PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUB132PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUB132SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMSUB132SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMSUB213PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUB213PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUB213SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMSUB213SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMSUB231PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUB231PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUB231SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMSUB231SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFMSUBADD132PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUBADD132PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUBADD213PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUBADD213PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUBADD231PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFMSUBADD231PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMADD132PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMADD132PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMADD132SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMADD132SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMADD213PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMADD213PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMADD213SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMADD213SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMADD231PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMADD231PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMADD231SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMADD231SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMSUB132PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMSUB132PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMSUB132SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMSUB132SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMSUB213PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMSUB213PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMSUB213SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMSUB213SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMSUB231PD | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMSUB231PS | vex | 128/256 | ✅ identical to the i5-13600K (4 forms) |  |
| VFNMSUB231SD | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| VFNMSUB231SS | vex | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>FXSAVE</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| FXRSTOR | legacy | - | ✅ U64 |  |
| FXSAVE | legacy | - | ✅ U64 (FOP/FIP/FDP, REX.W layout) |  |

</details>

<details><summary><b>FXSAVE64</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| FXRSTOR64 | legacy | - | ✅ U64 |  |
| FXSAVE64 | legacy | - | ✅ U64 |  |

</details>

<details><summary><b>GFNI</b> (3 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| GF2P8AFFINEINVQB | legacy | 128 | ✅ U70 |  |
| GF2P8AFFINEQB | legacy | 128 | ✅ U70 |  |
| GF2P8MULB | legacy | 128 | ✅ U70, identical to the CPU |  |

</details>

<details><summary><b>I186</b> (19 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| BOUND | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| ENTER | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| IMUL | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| INSB | legacy | - |  | ⏳ open item — CPL0 instruction (3 forms): CPL3 fault check in Phase 3 (D6) |
| INSW | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| LEAVE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| OUTSB | legacy | - |  | ⏳ open item — CPL0 instruction (3 forms): CPL3 fault check in Phase 3 (D6) |
| OUTSW | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| POPAW | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| PUSH | legacy | - | ✅ identical to the i5-13600K (7 forms) |  |
| PUSHAW | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| RCL | legacy | - | ✅ identical to the i5-13600K (23 forms) |  |
| RCR | legacy | - | ✅ identical to the i5-13600K (22 forms) |  |
| ROL | legacy | - | ✅ identical to the i5-13600K (22 forms) |  |
| ROR | legacy | - | ✅ identical to the i5-13600K (22 forms) |  |
| SAL | legacy | - | ✅ identical to the i5-13600K (22 forms) |  |
| SAR | legacy | - | ✅ identical to the i5-13600K (22 forms) |  |
| SHL | legacy | - | ✅ identical to the i5-13600K (22 forms) |  |
| SHR | legacy | - | ✅ identical to the i5-13600K (22 forms) |  |

</details>

<details><summary><b>I286PROTECTED</b> (9 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| ARPL | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| LAR | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| LLDT | legacy | - |  | ⏳ open item — CPL0 instruction (2 forms): CPL3 fault check in Phase 3 (D6) |
| LSL | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| LTR | legacy | - |  | ⏳ open item — CPL0 instruction (2 forms): CPL3 fault check in Phase 3 (D6) |
| SLDT | legacy | - |  | ⏳ open item — values: Phase 3 environment |
| STR | legacy | - |  | ⏳ open item — values: Phase 3 environment |
| VERR | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| VERW | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>I286REAL</b> (7 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CLTS | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| LGDT | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| LIDT | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| LMSW | legacy | - |  | ⏳ open item — CPL0 instruction (2 forms): CPL3 fault check in Phase 3 (D6) |
| SGDT | legacy | - |  | ⏳ open item — values: Phase 3 environment |
| SIDT | legacy | - |  | ⏳ open item — values: Phase 3 environment |
| SMSW | legacy | - |  | ⏳ open item — CR0 value: Phase 3 environment |

</details>

<details><summary><b>I386</b> (47 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| BSF | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| BSR | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| BT | legacy | - | ✅ identical to the i5-13600K (12 forms) |  |
| BTC | legacy | - | ✅ identical to the i5-13600K (12 forms) |  |
| BTR | legacy | - | ✅ identical to the i5-13600K (12 forms) |  |
| BTS | legacy | - | ✅ identical to the i5-13600K (12 forms) |  |
| CDQ | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| CMPSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) |  |
| CWDE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| INSD | legacy | - |  | ⏳ open item — CPL0 instruction (3 forms): CPL3 fault check in Phase 3 (D6) |
| IRETD | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| JCXZ | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| JECXZ | legacy | - | ✅ identical to the i5-13600K (cases_reach): 67 E3 rel8 (ECX = 0 with RCX[63:32] != 0 taken) |  |
| LFS | legacy | - | ✅ implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: natively it would replace the host thread's FS sele… |  |
| LGS | legacy | - | ✅ implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: natively it would replace the host thread's GS sele… |  |
| LODSD | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| LSS | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| MOVSD | legacy | 128 | ✅ identical to the i5-13600K (6 forms) |  |
| MOVSX | legacy | - | ✅ identical to the i5-13600K (12 forms) |  |
| MOVZX | legacy | - | ✅ identical to the i5-13600K (12 forms) |  |
| OUTSD | legacy | - |  | ⏳ open item — CPL0 instruction (3 forms): CPL3 fault check in Phase 3 (D6) |
| POPAL | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| POPFD | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| POPFL | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| PUSHAL | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| PUSHFD | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| PUSHFL | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| SCASD | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| SETA | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETAE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETB | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETBE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETG | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETGE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETL | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETLE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETNE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETNO | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETNP | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETNS | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETO | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETP | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SETS | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| SHLD | legacy | - | ✅ identical to the i5-13600K (12 forms) |  |
| SHRD | legacy | - | ✅ identical to the i5-13600K (12 forms) |  |
| STOSD | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |

</details>

<details><summary><b>I486</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| RSM | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>I486REAL</b> (7 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| BSWAP | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| CMPXCHG | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| CPUID | legacy | - | ✅ U68 i5-13600K profile + UC_CTL_X86_CPUID(_STRICT); only per-core APIC IDs vary |  |
| INVD | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| INVLPG | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| WBINVD | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| XADD | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |

</details>

<details><summary><b>I86</b> (88 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| AAA | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| AAD | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| AAM | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| AAS | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| ADC | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| ADD | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| AND | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| CALL | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| CBW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| CLC | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| CLD | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| CLI | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| CMC | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| CMP | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| CMPSB | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| CMPSW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| CWD | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| DAA | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| DAS | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| DEC | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| DIV | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| HLT | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| IDIV | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| IN | legacy | - |  | ⏳ open item — CPL0 instruction (6 forms): CPL3 fault check in Phase 3 (D6) |
| INC | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| INT | legacy | - |  | ⏳ open item — implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check … |
| INT1 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| INT3 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| INTO | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| IRET | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| JA | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JAE | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JB | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JBE | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JE | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JG | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JGE | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JL | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JLE | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JMP | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| JNE | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JNO | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JNP | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JNS | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JO | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JP | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| JS | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| LCALL | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| LDS | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| LEA | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| LES | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| LJMP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| LODSB | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| LODSW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| LOOP | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| LOOPE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| LOOPNE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| MOV | legacy | - |  | ⏳ open item — CPL0 instruction (8 forms): CPL3 fault check in Phase 3 (D6) |
| MOVABS | legacy | - | ✅ identical to the i5-13600K (9 forms) |  |
| MOVSB | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| MOVSW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| MUL | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| NEG | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| NOP2 | legacy | - | ✅ identical to the i5-13600K (10 forms) — same opcode as NOP (Capstone name) |  |
| NOT | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| OR | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| OUT | legacy | - |  | ⏳ open item — CPL0 instruction (6 forms): CPL3 fault check in Phase 3 (D6) |
| POP | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| POPF | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| PUSHF | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| RET | legacy | - | ✅ identical to the i5-13600K (9 forms) |  |
| RETF | legacy | - | ✅ identical to the i5-13600K (5 forms) |  |
| RETFQ | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| SALC | legacy | - | ✅ #UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample) |  |
| SBB | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| SCASB | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| SCASW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| STC | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| STD | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| STI | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| STOSB | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| STOSW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| SUB | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| TEST | legacy | - | ✅ identical to the i5-13600K (16 forms) |  |
| UDB | legacy | - | ✅ identical to the i5-13600K (cases_reach): D6 (UDB): #UD in 64-bit mode |  |
| XCHG | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |
| XLATB | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| XOR | legacy | - | ✅ identical to the i5-13600K (20 forms) |  |

</details>

<details><summary><b>INVPCID</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| INVPCID | legacy | - | ✅ implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U806 INVPCID r64, m128 (66 0F 38 82 /r, CPUID.(7,0)… |  |

</details>

<details><summary><b>LAHF</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| LAHF | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| SAHF | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>LONGMODE</b> (14 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CDQE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| CMPSQ | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| CQO | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| IRETQ | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| JRCXZ | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| LODSQ | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| MOVSQ | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| MOVSXD | legacy | - | ✅ identical to the i5-13600K (6 forms) |  |
| POPFQ | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| PUSHFQ | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| SCASQ | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| STOSQ | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| SWAPGS | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| SYSRETQ | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>MOVBE</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| MOVBE | legacy | - | ✅ identical to the i5-13600K (8 forms; 2 more encodings #UD on both) |  |

</details>

<details><summary><b>MOVDIR64B</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| MOVDIR64B | legacy | - | ✅ U73, identical to the CPU |  |

</details>

<details><summary><b>MOVDIRI</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| MOVDIRI | legacy | - | ✅ U72, identical to the CPU |  |

</details>

<details><summary><b>PAUSE</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| PAUSE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>PCLMULQDQ</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| PCLMULQDQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>PENTIUMMMX</b> (60 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| EMMS | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| MASKMOVQ | legacy | 64 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVD | legacy | 64/128 | ✅ identical to the i5-13600K (8 forms) |  |
| MOVNTQ | legacy | 64 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVQ | legacy | 64/128 | ✅ identical to the i5-13600K (10 forms) |  |
| PACKSSDW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PACKSSWB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PACKUSWB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PADDB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PADDD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PADDSB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PADDSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PADDUSB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PADDUSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PADDW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PAND | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PANDN | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PAVGB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PAVGW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PCMPEQB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PCMPEQD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PCMPEQW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PCMPGTB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PCMPGTD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PCMPGTW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PEXTRW | legacy | 64/128 | ✅ identical to the i5-13600K (3 forms) |  |
| PINSRW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMADDWD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMAXSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMAXUB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMINSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMINUB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMULHUW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMULHW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMULLW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| POR | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSADBW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSHUFW | legacy | 64 | ✅ identical to the i5-13600K (2 forms) |  |
| PSLLD | legacy | 64/128 | ✅ identical to the i5-13600K (6 forms) |  |
| PSLLQ | legacy | 64/128 | ✅ identical to the i5-13600K (6 forms) |  |
| PSLLW | legacy | 64/128 | ✅ identical to the i5-13600K (6 forms) |  |
| PSRAD | legacy | 64/128 | ✅ identical to the i5-13600K (6 forms) |  |
| PSRAW | legacy | 64/128 | ✅ identical to the i5-13600K (6 forms) |  |
| PSRLD | legacy | 64/128 | ✅ identical to the i5-13600K (6 forms) |  |
| PSRLQ | legacy | 64/128 | ✅ identical to the i5-13600K (6 forms) |  |
| PSRLW | legacy | 64/128 | ✅ identical to the i5-13600K (6 forms) |  |
| PSUBB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSUBD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSUBSB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSUBSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSUBUSB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSUBUSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSUBW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PUNPCKHBW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PUNPCKHDQ | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PUNPCKHWD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PUNPCKLBW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PUNPCKLDQ | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PUNPCKLWD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PXOR | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |

</details>

<details><summary><b>PENTIUMREAL</b> (4 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CMPXCHG8B | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| RDMSR | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| RDTSC | legacy | - |  | ⏳ open item — TSC determinism hook: Phase 3 |
| WRMSR | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>POPCNT</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| POPCNT | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |

</details>

<details><summary><b>PPRO</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| UD1 | legacy | - | ✅ identical to the i5-13600K (cases_reach): 0F B9 /r: #UD (its defined behaviour) |  |
| UD2 | legacy | - | ✅ identical to the i5-13600K (cases_reach): 0F 0B: #UD (its defined behaviour) |  |

</details>

<details><summary><b>PPRO_UD0_LONG</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| UD0 | legacy | - | ✅ identical to the i5-13600K (cases_reach): 0F FF /r: #UD (its defined behaviour) |  |

</details>

<details><summary><b>PREFETCH_NOP</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| PREFETCH | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| PREFETCHW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>PTWRITE</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| PTWRITE | legacy | - | ✅ SDM #UD (CPUID.14 = 0, U534; the CPU executes it: docs/quirks.md) |  |

</details>

<details><summary><b>RDPID</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| RDPID | legacy | - |  | ⏳ open item — TSC_AUX value: Phase 3 environment |

</details>

<details><summary><b>RDPMC</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| RDPMC | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>RDRAND</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| RDRAND | legacy | - | ✅ U65 host entropy (RtlGenRandom), CF/flags per SDM |  |

</details>

<details><summary><b>RDSEED</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| RDSEED | legacy | - | ✅ U65 |  |

</details>

<details><summary><b>RDTSCP</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| RDTSCP | legacy | - |  | ⏳ open item — TSC/TSC_AUX: Phase 3 |

</details>

<details><summary><b>RDWRFSGS</b> (4 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| RDFSBASE | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| RDGSBASE | legacy | - |  | ⏳ open item — value = TEB base: Phase 3 environment |
| WRFSBASE | legacy | - | ✅ implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: natively it would replace the host thread's FS base… |  |
| WRGSBASE | legacy | - | ✅ implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: natively it would replace the host thread's GS base… |  |

</details>

<details><summary><b>SEP</b> (3 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| SYSENTER | legacy | - |  | ⏳ open item — implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check … |
| SYSEXIT | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| SYSEXITQ | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>SERIALIZE</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| SERIALIZE | legacy | - | ✅ U74, identical to the CPU |  |

</details>

<details><summary><b>SHA</b> (7 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| SHA1MSG1 | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SHA1MSG2 | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SHA1NEXTE | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SHA1RNDS4 | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SHA256MSG1 | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SHA256MSG2 | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SHA256RNDS2 | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>SMAP</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CLAC | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| STAC | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>SSE</b> (110 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| ADDPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ADDSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ANDNPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ANDPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CMPEQ_OSPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPEQ_OSSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPEQ_UQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPEQ_UQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPEQ_USPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPEQ_USSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPEQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPEQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPFALSE_OSPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPFALSE_OSSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPFALSEPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPFALSESS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPGE_OQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPGE_OQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPGEPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPGESS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPGT_OQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPGT_OQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPGTPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPGTSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPLE_OQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPLE_OQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPLEPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPLESS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPLT_OQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPLT_OQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPLTPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPLTSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNEQ_OQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNEQ_OQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNEQ_OSPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNEQ_OSSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNEQ_USPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNEQ_USSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNEQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNEQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNGE_UQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNGE_UQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNGEPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNGESS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNGT_UQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNGT_UQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNGTPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNGTSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNLE_UQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNLE_UQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNLEPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNLESS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNLT_UQPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNLT_UQSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPNLTPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPNLTSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPORD_SPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPORD_SSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPORDPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPORDSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CMPSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CMPTRUE_USPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPTRUE_USSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPTRUEPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPTRUESS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPUNORD_SPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPUNORD_SSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| CMPUNORDPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPS imm8 predicate alias |  |
| CMPUNORDSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPSS imm8 predicate alias |  |
| COMISS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTPI2PS | legacy | 128 | ✅ m64 form: SDM x87 transition + #MF (U532; the CPU deviates, docs/quirks.md) |  |
| CVTPS2PI | legacy | 64/128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTSI2SS | legacy | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| CVTSS2SI | legacy | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| CVTTPS2PI | legacy | 64/128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTTSS2SI | legacy | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| DIVPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| DIVSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MAXPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MAXSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MINPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MINSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVAPS | legacy | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| MOVHLPS | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVHPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVLHPS | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVLPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVMSKPS | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVNTPS | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVSS | legacy | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| MOVUPS | legacy | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| MULPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MULSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ORPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVMSKB | legacy | 64/128 | ✅ identical to the i5-13600K (2 forms) |  |
| RCPPS | legacy | 128 | ✅ U81 analytic Intel 12-bit model (RN of 1/midpoint); 2^32 inputs x 6 MXCSR == CPU |  |
| RCPSS | legacy | 128 | ✅ U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |  |
| RSQRTPS | legacy | 128 | ✅ U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |  |
| RSQRTSS | legacy | 128 | ✅ U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |  |
| SFENCE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| SHUFPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SQRTPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SQRTSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SUBPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SUBSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| UCOMISS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| UNPCKHPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| UNPCKLPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| XORPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>SSE2</b> (129 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| ADDPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ADDSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ANDNPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ANDPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CMPEQ_OSPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPEQ_OSSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPEQ_UQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPEQ_UQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPEQ_USPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPEQ_USSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPEQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPEQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPFALSE_OSPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPFALSE_OSSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPFALSEPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPFALSESD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPGE_OQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPGE_OQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPGEPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPGESD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPGT_OQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPGT_OQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPGTPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPGTSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPLE_OQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPLE_OQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPLEPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPLESD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPLT_OQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPLT_OQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPLTPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPLTSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNEQ_OQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNEQ_OQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNEQ_OSPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNEQ_OSSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNEQ_USPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNEQ_USSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNEQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNEQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNGE_UQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNGE_UQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNGEPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNGESD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNGT_UQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNGT_UQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNGTPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNGTSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNLE_UQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNLE_UQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNLEPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNLESD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNLT_UQPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNLT_UQSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPNLTPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPNLTSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPORD_SPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPORD_SSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPORDPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPORDSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CMPTRUE_USPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPTRUE_USSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPTRUEPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPTRUESD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPUNORD_SPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPUNORD_SSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| CMPUNORDPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) — CMPPD imm8 predicate alias |  |
| CMPUNORDSD | legacy | 128 | ✅ identical to the i5-13600K (5 forms) — CMPSD imm8 predicate alias |  |
| COMISD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTDQ2PD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTDQ2PS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTPD2DQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTPD2PI | legacy | 64/128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTPD2PS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTPI2PD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTPS2DQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTPS2PD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTSD2SI | legacy | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| CVTSD2SS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTSI2SD | legacy | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| CVTSS2SD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTTPD2DQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTTPD2PI | legacy | 64/128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTTPS2DQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| CVTTSD2SI | legacy | 128 | ✅ identical to the i5-13600K (4 forms) |  |
| DIVPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| DIVSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| LFENCE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| MASKMOVDQU | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MAXPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MAXSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MFENCE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| MINPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MINSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVAPD | legacy | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| MOVDQ2Q | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVDQA | legacy | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| MOVDQU | legacy | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| MOVHPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVLPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVMSKPD | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVNTDQ | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVNTI | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| MOVNTPD | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVQ2DQ | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVUPD | legacy | 128 | ✅ identical to the i5-13600K (3 forms) |  |
| MULPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MULSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ORPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PADDQ | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMULUDQ | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSHUFD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PSHUFHW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PSHUFLW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PSLLDQ | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| PSRLDQ | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| PSUBQ | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PUNPCKHQDQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PUNPCKLQDQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SHUFPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SQRTPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SQRTSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SUBPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| SUBSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| UCOMISD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| UNPCKHPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| UNPCKLPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| XORPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>SSE3</b> (10 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| ADDSUBPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ADDSUBPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| HADDPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| HADDPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| HSUBPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| HSUBPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| LDDQU | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MOVDDUP | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVSHDUP | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVSLDUP | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>SSE3X87</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| FISTTP | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |

</details>

<details><summary><b>SSE4</b> (48 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| BLENDPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| BLENDPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| BLENDVPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| BLENDVPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| DPPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| DPPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| EXTRACTPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| INSERTPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| MOVNTDQA | legacy | 128 | ✅ identical to the i5-13600K (1 forms) |  |
| MPSADBW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PACKUSDW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PBLENDVB | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PBLENDW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PCMPEQQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PEXTRB | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PEXTRD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PEXTRQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PHMINPOSUW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PINSRB | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PINSRD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PINSRQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMAXSB | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMAXSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMAXUD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMAXUW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMINSB | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMINSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMINUD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMINUW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVSXBD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVSXBQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVSXBW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVSXDQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVSXWD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVSXWQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVZXBD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVZXBQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVZXBW | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVZXDQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVZXWD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMOVZXWQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMULDQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PMULLD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PTEST | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ROUNDPD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ROUNDPS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ROUNDSD | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| ROUNDSS | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>SSE42</b> (6 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| CRC32 | legacy | - | ✅ identical to the i5-13600K (8 forms) |  |
| PCMPESTRI | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PCMPESTRM | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PCMPGTQ | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PCMPISTRI | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |
| PCMPISTRM | legacy | 128 | ✅ identical to the i5-13600K (2 forms) |  |

</details>

<details><summary><b>SSEMXCSR</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| LDMXCSR | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| STMXCSR | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>SSE_PREFETCH</b> (4 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| PREFETCHNTA | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| PREFETCHT0 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| PREFETCHT1 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| PREFETCHT2 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>SSSE3</b> (16 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| PABSB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PABSD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PABSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PALIGNR | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PHADDD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PHADDSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PHADDW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PHSUBD | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PHSUBSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PHSUBW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMADDUBSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PMULHRSW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSHUFB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSIGNB | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSIGND | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |
| PSIGNW | legacy | 64/128 | ✅ identical to the i5-13600K (4 forms) |  |

</details>

<details><summary><b>VMFUNC</b> (1 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| VMFUNC | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>VTX</b> (12 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| INVEPT | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| INVVPID | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMCALL | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMCLEAR | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMLAUNCH | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMPTRLD | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMPTRST | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMREAD | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMRESUME | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMWRITE | legacy | - |  | ⏳ open item — CPL0 instruction (2 forms): CPL3 fault check in Phase 3 (D6) |
| VMXOFF | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| VMXON | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>X87</b> (79 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| F2XM1 | legacy | - | ✅ U56 Goldmont-microcode model, 100% bit-exact (value + FSW) |  |
| FABS | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FADD | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| FADDP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FBLD | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FBSTP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCHS | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCOM | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| FCOMP | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| FCOMPP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FCOS | legacy | - | ✅ U56 microcode model, 100% bit-exact |  |
| FDECSTP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FDISI8087_NOP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FDIV | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| FDIVP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FDIVR | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| FDIVRP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FENI8087_NOP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FFREE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FFREEP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FIADD | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FICOM | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FICOMP | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FIDIV | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FIDIVR | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FILD | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| FIMUL | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FINCSTP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FIST | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FISTP | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| FISUB | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FISUBR | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FLD | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| FLD1 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FLDCW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FLDENV | legacy | - | ✅ U64 |  |
| FLDL2E | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FLDL2T | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FLDLG2 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FLDLN2 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FLDPI | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FLDZ | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FMUL | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| FMULP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FNCLEX | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FNINIT | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FNOP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FNSAVE | legacy | - | ✅ U61-U64 |  |
| FNSTCW | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FNSTENV | legacy | - | ✅ U61/U62/U64 (FCW masked after, reserved FFFF, FIP/FOP/FDP model) |  |
| FNSTSW | legacy | - | ✅ identical to the i5-13600K (2 forms) |  |
| FPATAN | legacy | - | ✅ U56 microcode model, 100% bit-exact |  |
| FPREM | legacy | - | ✅ ROM model == hw 148/148, fork == hw 388/388 |  |
| FPREM1 | legacy | - | ✅ ROM model == hw, fork == hw |  |
| FPTAN | legacy | - | ✅ U56 microcode model, 100% bit-exact |  |
| FRNDINT | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FRSTOR | legacy | - | ✅ U63/U64 |  |
| FSCALE | legacy | - | ✅ ROM model == hw, fork == hw |  |
| FSETPM | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FSIN | legacy | - | ✅ U56 microcode model, 100% bit-exact |  |
| FSINCOS | legacy | - | ✅ U56 microcode model, 100% bit-exact |  |
| FSQRT | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FST | legacy | - | ✅ identical to the i5-13600K (3 forms) |  |
| FSTP | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| FSTPNCE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FSUB | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| FSUBP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FSUBR | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| FSUBRP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FTST | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FUCOM | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FUCOMP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FUCOMPP | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FXAM | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FXCH | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FXTRACT | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| FYL2X | legacy | - | ✅ U56 microcode model, 100% bit-exact |  |
| FYL2XP1 | legacy | - | ✅ U56 microcode model, 100% bit-exact; x < -1: SDM #IA (U533; the CPU deviates, docs/quirks.md) |  |
| WAIT | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>XSAVE</b> (6 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| XGETBV | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| XRSTOR | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| XRSTOR64 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| XSAVE | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| XSAVE64 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| XSETBV | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>XSAVEC</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| XSAVEC | legacy | - | ✅ U66 compacted format |  |
| XSAVEC64 | legacy | - | ✅ U66 |  |

</details>

<details><summary><b>XSAVEOPT</b> (2 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| XSAVEOPT | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |
| XSAVEOPT64 | legacy | - | ✅ identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>XSAVES</b> (4 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| XRSTORS | legacy | - |  | ⏳ open item — CPL0: Phase 3 (D6) |
| XRSTORS64 | legacy | - |  | ⏳ open item — CPL0: Phase 3 (D6) |
| XSAVES | legacy | - |  | ⏳ open item — CPL0: #GP at CPL3 in Phase 3 (D6); compacted format shared with U66 |
| XSAVES64 | legacy | - |  | ⏳ open item — CPL0: Phase 3 (D6) |

</details>


---

### Instructions that can't be supported for now:

**1380 forms your i5-13600K cannot run** — not supported by your CPU; never compared against your hardware. The emulator implements them per the Intel manual (vendor manual for AMD/VIA) and checks them against SDM-pseudocode vectors.

#### By family

| family | forms | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|
| AVX_IFMA | 2 | ❌ **cannot run** (CPUID.7H.1:EAX[23] = 0 on this CPU) | ✅ 2 |  |
| AVX_NE_CONVERT | 7 | ❌ **cannot run** (CPUID.7H.1:EDX[5] = 0 on this CPU) | ✅ 7 |  |
| AVX_VNNI_INT16 | 6 | ❌ **cannot run** (CPUID.7H.1:EDX[10] = 0 on this CPU) | ✅ 6 |  |
| AVX_VNNI_INT8 | 6 | ❌ **cannot run** (CPUID.7H.1:EDX[4] = 0 on this CPU) | ✅ 6 |  |
| CET | 10 | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ 8 | ⏳ 2 |
| CLDEMOTE | 1 | ❌ **cannot run** (CPUID.7H:ECX[25] = 0 on this CPU) | ✅ 1 |  |
| CMPCCXADD | 22 | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ 22 |
| ENQCMD | 2 | ❌ **cannot run** (CPUID.7H:ECX[29] = 0 on this CPU) | ✅ 2 |  |
| FRED | 2 | ❌ **cannot run** (CPUID.7H.1:EAX[17] = 0 on this CPU) |  | ⬜ 2 queued |
| HLE | 2 | ❌ **cannot run** (CPUID.7H:EBX[4] = 0 on this CPU) | ✅ 2 |  |
| HRESET | 1 | ❌ **cannot run** (CPUID.7H.1:EAX[22] = 0 on this CPU) | ✅ 1 |  |
| IBHF | 1 | ❌ **cannot run** (not reported by this CPU) | ✅ 1 |  |
| ICACHE_PREFETCH | 2 | ❌ **cannot run** (CPUID.7H.1:EDX[14] = 0 on this CPU) | ✅ 2 |  |
| KEYLOCKER | 7 | ❌ **cannot run** (CPUID.7H:ECX[23] = 0 on this CPU) | ✅ 7 |  |
| KEYLOCKER_WIDE | 4 | ❌ **cannot run** (CPUID.19H:EBX[2] = 0 on this CPU) | ✅ 4 |  |
| LKGS | 1 | ❌ **cannot run** (CPUID.7H.1:EAX[18] = 0 on this CPU) | ✅ 1 |  |
| MONITOR | 2 | ❌ **cannot run** (CPUID.1H:ECX[3] = 0 on this CPU) |  | ⏳ 2 |
| MOVRS | 2 | ❌ **cannot run** (CPUID.7H.1:EAX[31] = 0 on this CPU) | ✅ 2 |  |
| MPX | 7 | ❌ **cannot run** (CPUID.7H:EBX[14] = 0 on this CPU) | ✅ 7 |  |
| MSRLIST | 2 | ❌ **cannot run** (CPUID.7H.1:EAX[27] = 0 on this CPU) | ✅ 2 |  |
| MSR_IMM | 2 | ❌ **cannot run** (CPUID.7H.1:ECX[5] = 0 on this CPU) | ✅ 2 |  |
| PBNDKB | 1 | ❌ **cannot run** (CPUID.7H.1:EBX[1] = 0 on this CPU) | ✅ 1 |  |
| PCONFIG | 1 | ❌ **cannot run** (CPUID.7H:EDX[18] = 0 on this CPU) | ✅ 1 |  |
| PKU | 2 | ❌ **cannot run** (the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode) |  | ⬜ 2 queued |
| PREFETCHWT1 | 1 | ❌ **cannot run** (CPUID.7H:ECX[0] = 0 on this CPU) | ✅ 1 |  |
| RAO_INT | 4 | ❌ **cannot run** (CPUID.7H.1:EAX[3] = 0 on this CPU) | ✅ 4 |  |
| RTM | 4 | ❌ **cannot run** (CPUID.7H:EBX[11] = 0 on this CPU) | ✅ 4 |  |
| SGX | 2 | ❌ **cannot run** (CPUID.7H:EBX[2] = 0 on this CPU) |  | ⬜ 2 queued |
| SGX_ENCLV | 1 | ❌ **cannot run** (CPUID.7H:EBX[2] = 0 on this CPU) |  | ⬜ 1 queued |
| SHA512 | 3 | ❌ **cannot run** (CPUID.7H.1:EAX[0] = 0 on this CPU) | ✅ 3 |  |
| SM3 | 3 | ❌ **cannot run** (CPUID.7H.1:EAX[1] = 0 on this CPU) | ✅ 3 |  |
| SM4 | 4 | ❌ **cannot run** (CPUID.7H.1:EAX[2] = 0 on this CPU) | ✅ 4 |  |
| SMX | 1 | ❌ **cannot run** (CPUID.1H:ECX[6] = 0 on this CPU) | ✅ 1 |  |
| TDX | 4 | ❌ **cannot run** (Intel TDX (server, VMX root only)) |  | ⬜ 4 queued |
| TSX_LDTRK | 2 | ❌ **cannot run** (CPUID.7H:EDX[16] = 0 on this CPU) | ✅ 2 |  |
| UINTR | 5 | ❌ **cannot run** (CPUID.7H:EDX[5] = 0 on this CPU) | ✅ 5 |  |
| USER_MSR | 4 | ❌ **cannot run** (CPUID.7H.1:EDX[15] = 0 on this CPU) | ✅ 4 |  |
| WAITPKG | 3 | ❌ **cannot run** (CPUID.7H:ECX[5] = 0 on this CPU) | ✅ 3 |  |
| WBNOINVD | 1 | ❌ **cannot run** (CPUID.80000008H:EBX[9] = 0 on this CPU) |  | ⏳ 1 |
| WRMSRNS | 1 | ❌ **cannot run** (CPUID.7H.1:EAX[19] = 0 on this CPU) | ✅ 1 |  |
| AMX_AVX512 | 5 | ❌ **cannot run** (AMX_AVX512 not reported by this CPU) | ✅ 5 |  |
| AMX_BF16 | 1 | ❌ **cannot run** (CPUID.7H:EDX[22] = 0 on this CPU) | ✅ 1 |  |
| AMX_COMPLEX | 2 | ❌ **cannot run** (CPUID.7H.1:EDX[8] = 0 on this CPU) | ✅ 2 |  |
| AMX_FP16 | 1 | ❌ **cannot run** (CPUID.7H.1:EAX[21] = 0 on this CPU) | ✅ 1 |  |
| AMX_FP8 | 4 | ❌ **cannot run** (AMX_FP8 not reported by this CPU) | ✅ 4 |  |
| AMX_INT8 | 4 | ❌ **cannot run** (CPUID.7H:EDX[25] = 0 on this CPU) | ✅ 4 |  |
| AMX_MOVRS | 2 | ❌ **cannot run** (AMX_MOVRS not reported by this CPU) | ✅ 2 |  |
| AMX_TILE | 3 | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ 3 |  |
| AMX_TILE_BASE | 4 | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ 4 |  |
| APX_F | 33 | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ 33 |  |
| APX_F_ADX | 2 | ❌ **cannot run** (APX_F_ADX not reported by this CPU; APX_F_ADX_N3 not reported by this CPU) | ✅ 2 |  |
| APX_F_AMX | 3 | ❌ **cannot run** (APX_F_AMX not reported by this CPU) | ✅ 3 |  |
| APX_F_AMX_BASE | 2 | ❌ **cannot run** (APX_F_AMX_BASE not reported by this CPU) | ✅ 2 |  |
| APX_F_AMX_MOVRS | 2 | ❌ **cannot run** (APX_F_AMX_MOVRS not reported by this CPU) | ✅ 2 |  |
| APX_F_BMI1 | 6 | ❌ **cannot run** (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) | ✅ 6 |  |
| APX_F_BMI2 | 8 | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU; APX_F_BMI2_N3 not reported by this CPU) | ✅ 8 |  |
| APX_F_CET | 4 | ❌ **cannot run** (APX_F_CET not reported by this CPU) | ✅ 4 |  |
| APX_F_CMPCCXADD | 22 | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ 22 |  |
| APX_F_ENQCMD | 2 | ❌ **cannot run** (APX_F_ENQCMD not reported by this CPU) | ✅ 2 |  |
| APX_F_INVPCID | 1 | ❌ **cannot run** (APX_F_INVPCID not reported by this CPU) | ✅ 1 |  |
| APX_F_LZCNT | 1 | ❌ **cannot run** (APX_F_LZCNT not reported by this CPU; APX_F_LZCNT_N3 not reported by this CPU) | ✅ 1 |  |
| APX_F_MOVBE | 1 | ❌ **cannot run** (APX_F_MOVBE not reported by this CPU) | ✅ 1 |  |
| APX_F_MOVDIR64B | 1 | ❌ **cannot run** (APX_F_MOVDIR64B not reported by this CPU) | ✅ 1 |  |
| APX_F_MOVDIRI | 1 | ❌ **cannot run** (APX_F_MOVDIRI not reported by this CPU) | ✅ 1 |  |
| APX_F_MOVRS | 1 | ❌ **cannot run** (APX_F_MOVRS not reported by this CPU) | ✅ 1 |  |
| APX_F_MSR_IMM | 2 | ❌ **cannot run** (APX_F_MSR_IMM not reported by this CPU) | ✅ 2 |  |
| APX_F_N3 | 84 | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ 84 |  |
| APX_F_POPCNT | 1 | ❌ **cannot run** (APX_F_POPCNT not reported by this CPU; APX_F_POPCNT_N3 not reported by this CPU) | ✅ 1 |  |
| APX_F_RAO_INT | 4 | ❌ **cannot run** (APX_F_RAO_INT not reported by this CPU) | ✅ 4 |  |
| APX_F_USER_MSR | 2 | ❌ **cannot run** (APX_F_USER_MSR not reported by this CPU) | ✅ 2 |  |
| APX_F_VMX | 2 | ❌ **cannot run** (APX_F_VMX not reported by this CPU) |  | ⬜ 2 queued |
| AVX10_2_BF16 | 29 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ 27 | ⏳ 2 (wt/avx10_a, wt/avx10_b) |
| AVX10_MOVRS | 4 | ❌ **cannot run** (AVX10_MOVRS not reported by this CPU) | ✅ 4 |  |
| AVX10_V2_AUX | 21 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ 21 (wt/avx10_a, wt/avx10_b) |
| AVX512BW | 112 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ 112 |  |
| AVX512CD | 6 | ❌ **cannot run** (CPUID.7H:EBX[28] = 0 on this CPU) | ✅ 6 |  |
| AVX512DQ | 69 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ 69 |  |
| AVX512ER | 10 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ 10 |
| AVX512F | 479 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU; AVX512_MOVZXC not reported by this CPU) | ✅ 469 | ⏳ 10 (M2 agents: wt/m2_engine, m2_perm, m2_cvt, m2_gather) |
| AVX512PF | 16 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ 16 |  |
| AVX512_4FMAPS | 4 | ❌ **cannot run** (CPUID.7H:EDX[3] = 0 on this CPU) | ✅ 4 |  |
| AVX512_4VNNIW | 2 | ❌ **cannot run** (CPUID.7H:EDX[2] = 0 on this CPU) | ✅ 2 |  |
| AVX512_BF16 | 3 | ❌ **cannot run** (CPUID.7H.1:EAX[5] = 0 on this CPU) | ✅ 3 |  |
| AVX512_BITALG | 3 | ❌ **cannot run** (CPUID.7H:ECX[12] = 0 on this CPU) | ✅ 3 |  |
| AVX512_COM_EF | 6 | ❌ **cannot run** (AVX512_COM_EF not reported by this CPU) | ✅ 6 |  |
| AVX512_FP16 | 170 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU; AVX512_MOVZXC not reported by this CPU) | ✅ 166 | ⏳ 4 (wt/fp16) |
| AVX512_FP16_CONVERT | 1 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ 1 |  |
| AVX512_FP8_CONVERT | 13 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ 13 |  |
| AVX512_GFNI | 3 | ❌ **cannot run** (AVX512_GFNI not reported by this CPU) | ✅ 3 |  |
| AVX512_IFMA | 2 | ❌ **cannot run** (CPUID.7H:EBX[21] = 0 on this CPU) | ✅ 2 |  |
| AVX512_MEDIAX | 1 | ❌ **cannot run** (AVX512_MEDIAX not reported by this CPU) | ✅ 1 |  |
| AVX512_MINMAX | 7 | ❌ **cannot run** (AVX512_MINMAX not reported by this CPU) | ✅ 7 |  |
| AVX512_SAT_CVT | 12 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ 12 |  |
| AVX512_SAT_CVT_DS | 12 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ 12 |  |
| AVX512_VAES | 4 | ❌ **cannot run** (AVX512_VAES not reported by this CPU) | ✅ 4 |  |
| AVX512_VBMI | 4 | ❌ **cannot run** (CPUID.7H:ECX[1] = 0 on this CPU) | ✅ 4 |  |
| AVX512_VBMI2 | 16 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ 16 |  |
| AVX512_VNNI | 4 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ 4 |  |
| AVX512_VNNI_FP16 | 1 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ 1 |  |
| AVX512_VNNI_INT16 | 6 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ 6 |  |
| AVX512_VNNI_INT8 | 6 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ 6 |  |
| AVX512_VP2INTERSECT | 2 | ❌ **cannot run** (CPUID.7H:EDX[8] = 0 on this CPU) | ✅ 2 |  |
| AVX512_VPCLMULQDQ | 1 | ❌ **cannot run** (AVX512_VPCLMULQDQ not reported by this CPU) | ✅ 1 |  |
| AVX512_VPOPCNTDQ | 2 | ❌ **cannot run** (CPUID.7H:ECX[14] = 0 on this CPU) | ✅ 2 |  |

#### Per instruction

<details><summary><b>AVX_IFMA</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPMADD52HUQ | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EAX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U87 AVX-IFMA (VEX): independent… |  |
| VPMADD52LUQ | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EAX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U87 AVX-IFMA (VEX): independent… |  |

</details>

<details><summary><b>AVX_NE_CONVERT</b> (7 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VBCSTNEBF162PS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U88 AVX-NE-CONVERT: independent… |  |
| VBCSTNESH2PS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U88 AVX-NE-CONVERT: independent… |  |
| VCVTNEEBF162PS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U88 AVX-NE-CONVERT: independent… |  |
| VCVTNEEPH2PS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U88 AVX-NE-CONVERT: independent… |  |
| VCVTNEOBF162PS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U88 AVX-NE-CONVERT: independent… |  |
| VCVTNEOPH2PS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U88 AVX-NE-CONVERT: independent… |  |
| VCVTNEPS2BF16 | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U88 AVX-NE-CONVERT: independent… |  |

</details>

<details><summary><b>AVX_VNNI_INT16</b> (6 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPDPWSUD | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[10] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U86 AVX-VNNI-INT16: independent… |  |
| VPDPWSUDS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[10] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U86 AVX-VNNI-INT16: independent… |  |
| VPDPWUSD | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[10] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U86 AVX-VNNI-INT16: independent… |  |
| VPDPWUSDS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[10] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U86 AVX-VNNI-INT16: independent… |  |
| VPDPWUUD | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[10] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U86 AVX-VNNI-INT16: independent… |  |
| VPDPWUUDS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[10] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U86 AVX-VNNI-INT16: independent… |  |

</details>

<details><summary><b>AVX_VNNI_INT8</b> (6 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPDPBSSD | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[4] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U85 AVX-VNNI-INT8: independent … |  |
| VPDPBSSDS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[4] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U85 AVX-VNNI-INT8: independent … |  |
| VPDPBSUD | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[4] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U85 AVX-VNNI-INT8: independent … |  |
| VPDPBSUDS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[4] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U85 AVX-VNNI-INT8: independent … |  |
| VPDPBUUD | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[4] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U85 AVX-VNNI-INT8: independent … |  |
| VPDPBUUDS | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EDX[4] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U85 AVX-VNNI-INT8: independent … |  |

</details>

<details><summary><b>CET</b> (10 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| CLRSSBSY | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U114 CET shadow stack: SDM-pseu… |  |
| INCSSPD | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U114 CET shadow stack: SDM-pseu… |  |
| INCSSPQ | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U114 CET shadow stack: SDM-pseu… |  |
| RSTORSSP | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U114 CET shadow stack: SDM-pseu… |  |
| SAVEPREVSSP | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U114 CET shadow stack: SDM-pseu… |  |
| SETSSBSY | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U114 CET shadow stack: SDM-pseu… |  |
| WRSSD | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U114 CET shadow stack: SDM-pseu… |  |
| WRSSQ | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U114 CET shadow stack: SDM-pseu… |  |
| WRUSSD | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) |  | ⏳ open item — CPL0 instruction (cases_reach): CPL3 fault in Phase 3 (D6): 66 [REX.W] 0F 38 F5: CPU #GP(0) at CPL3 (CR4.CET = 1 under … |
| WRUSSQ | legacy | - | ❌ **cannot run** (the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode) |  | ⏳ open item — CPL0 instruction (cases_reach): CPL3 fault in Phase 3 (D6): 66 [REX.W] 0F 38 F5: CPU #GP(0) at CPL3 (CR4.CET = 1 under … |

</details>

<details><summary><b>CLDEMOTE</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| CLDEMOTE | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[25] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>CMPCCXADD</b> (22 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| CMPAEXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPAXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPBEXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPBXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPEXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPGEXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPGXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPLEXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPLXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNBEXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNBXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNEXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNLEXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNLXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNOXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNPXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNSXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPNZXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPOXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPPXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPSXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |
| CMPZXADD | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[7] = 0 on this CPU) |  | ⏳ open item — implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (… |

</details>

<details><summary><b>ENQCMD</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| ENQCMD | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[29] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U112 ENQCMD: SDM-pseudocode exp… |  |
| ENQCMDS | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[29] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U112 ENQCMDS: SDM-pseudocode ex… |  |

</details>

<details><summary><b>FRED</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| ERETS | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[17] = 0 on this CPU) |  | ⬜ queued — CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not v… |
| ERETU | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[17] = 0 on this CPU) |  | ⬜ queued — CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not v… |

</details>

<details><summary><b>HLE</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| XACQUIRE | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[4] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (cases_reach): F2 on LOCK/XCHG: ignored, the CPU lacks HLE (locked op runs) |  |
| XRELEASE | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[4] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (cases_reach): F3 on LOCK/XCHG/MOV m: ignored, the CPU lacks HLE |  |

</details>

<details><summary><b>HRESET</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| HRESET | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[22] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U804 HRESET (F3 0F 3A F0 C0 ib,… |  |

</details>

<details><summary><b>IBHF</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| IBHF | legacy | - | ❌ **cannot run** (not reported by this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (cases_reach): F3 [REX.W] 0F 1E F8: hint NOP |  |

</details>

<details><summary><b>ICACHE_PREFETCH</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| PREFETCHIT0 | legacy | - | ❌ **cannot run** (CPUID.7H.1:EDX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (cases_reach): 0F 18 /7: NOP without PREFETCHI (RIP-relative and other memory form… |  |
| PREFETCHIT1 | legacy | - | ❌ **cannot run** (CPUID.7H.1:EDX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (cases_reach): 0F 18 /6: NOP without PREFETCHI |  |

</details>

<details><summary><b>KEYLOCKER</b> (7 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| AESDEC128KL | legacy | 128 | ❌ **cannot run** (CPUID.7H:ECX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| AESDEC256KL | legacy | 128 | ❌ **cannot run** (CPUID.7H:ECX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| AESENC128KL | legacy | 128 | ❌ **cannot run** (CPUID.7H:ECX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| AESENC256KL | legacy | 128 | ❌ **cannot run** (CPUID.7H:ECX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| ENCODEKEY128 | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| ENCODEKEY256 | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| LOADIWKEY | legacy | 128 | ❌ **cannot run** (CPUID.7H:ECX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker LOADIWKEY: SDM/… |  |

</details>

<details><summary><b>KEYLOCKER_WIDE</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| AESDECWIDE128KL | legacy | - | ❌ **cannot run** (CPUID.19H:EBX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| AESDECWIDE256KL | legacy | - | ❌ **cannot run** (CPUID.19H:EBX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| AESENCWIDE128KL | legacy | - | ❌ **cannot run** (CPUID.19H:EBX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |
| AESENCWIDE256KL | legacy | - | ❌ **cannot run** (CPUID.19H:EBX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U100 Key Locker: SDM/spec-pseud… |  |

</details>

<details><summary><b>LKGS</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| LKGS | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[18] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U805 LKGS r/m16 (F2 0F 00 /6, 6… |  |

</details>

<details><summary><b>MONITOR</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MONITOR | legacy | - | ❌ **cannot run** (CPUID.1H:ECX[3] = 0 on this CPU) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |
| MWAIT | legacy | - | ❌ **cannot run** (CPUID.1H:ECX[3] = 0 on this CPU) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>MOVRS</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MOVRS | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[31] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U102 MOVRS: SDM/spec-pseudocode… |  |
| PREFETCHRST2 | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[31] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (cases_reach): 0F 18 /4: NOP without MOVRS |  |

</details>

<details><summary><b>MPX</b> (7 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| BNDCL | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (2 forms) |  |
| BNDCN | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (2 forms) |  |
| BNDCU | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (2 forms) |  |
| BNDLDX | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (1 forms) |  |
| BNDMK | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (1 forms) |  |
| BNDMOV | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (3 forms) |  |
| BNDSTX | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>MSRLIST</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| RDMSRLIST | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[27] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U802 RDMSRLIST (F2 0F 01 C6, 64… |  |
| WRMSRLIST | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[27] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U802 WRMSRLIST (F3 0F 01 C6, 64… |  |

</details>

<details><summary><b>MSR_IMM</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| RDMSR | vex | - | ❌ **cannot run** (CPUID.7H.1:ECX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U803 MSR-IMM RDMSR r64, imm32 (… |  |
| WRMSRNS | vex | - | ❌ **cannot run** (CPUID.7H.1:ECX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U803 MSR-IMM WRMSRNS imm32, r64… |  |

</details>

<details><summary><b>PBNDKB</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| PBNDKB | legacy | - | ❌ **cannot run** (CPUID.7H.1:EBX[1] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U807 PBNDKB (NP 0F 01 C7, 64-bi… |  |

</details>

<details><summary><b>PCONFIG</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| PCONFIG | legacy | - | ❌ **cannot run** (CPUID.7H:EDX[18] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U1020-U1022 PCONFIG (NP 0F 01 C… |  |

</details>

<details><summary><b>PKU</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| RDPKRU | legacy | - | ❌ **cannot run** (the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode) |  | ⬜ queued — not implemented (cases_reach): PKU (CR4.PKE = 0 under Windows: OSPKE off): #UD in both |
| WRPKRU | legacy | - | ❌ **cannot run** (the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode) |  | ⬜ queued — not implemented (cases_reach): PKU (CR4.PKE = 0 under Windows: OSPKE off): #UD in both |

</details>

<details><summary><b>PREFETCHWT1</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| PREFETCHWT1 | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[0] = 0 on this CPU) | ✅ per manual (SDM vectors) — identical to the i5-13600K (1 forms) |  |

</details>

<details><summary><b>RAO_INT</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| AADD | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[3] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U101 RAO-INT: SDM/spec-pseudoco… |  |
| AAND | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[3] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U101 RAO-INT: SDM/spec-pseudoco… |  |
| AOR | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[3] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U101 RAO-INT: SDM/spec-pseudoco… |  |
| AXOR | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[3] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U101 RAO-INT: SDM/spec-pseudoco… |  |

</details>

<details><summary><b>RTM</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| XABORT | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U110 TSX: SDM-pseudocode expect… |  |
| XBEGIN | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U110 TSX: SDM-pseudocode expect… |  |
| XEND | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U110 TSX: SDM-pseudocode expect… |  |
| XTEST | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U110 TSX: SDM-pseudocode expect… |  |

</details>

<details><summary><b>SGX</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| ENCLS | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[2] = 0 on this CPU) |  | ⬜ queued — not implemented (cases_reach): the i5-13600K lacks SGX: #UD in both |
| ENCLU | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[2] = 0 on this CPU) |  | ⬜ queued — not implemented (cases_reach): the i5-13600K lacks SGX: #UD in both |

</details>

<details><summary><b>SGX_ENCLV</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| ENCLV | legacy | - | ❌ **cannot run** (CPUID.7H:EBX[2] = 0 on this CPU) |  | ⬜ queued — not implemented (cases_reach): the i5-13600K lacks SGX: #UD in both |

</details>

<details><summary><b>SHA512</b> (3 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VSHA512MSG1 | vex | 256 | ❌ **cannot run** (CPUID.7H.1:EAX[0] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U82: SDM-pseudocode reference (… |  |
| VSHA512MSG2 | vex | 256 | ❌ **cannot run** (CPUID.7H.1:EAX[0] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U82: SDM-pseudocode reference (… |  |
| VSHA512RNDS2 | vex | 256 | ❌ **cannot run** (CPUID.7H.1:EAX[0] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U82: SDM-pseudocode reference (… |  |

</details>

<details><summary><b>SM3</b> (3 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VSM3MSG1 | vex | 128 | ❌ **cannot run** (CPUID.7H.1:EAX[1] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U83: SDM-pseudocode reference (… |  |
| VSM3MSG2 | vex | 128 | ❌ **cannot run** (CPUID.7H.1:EAX[1] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U83: SDM-pseudocode reference (… |  |
| VSM3RNDS2 | vex | 128 | ❌ **cannot run** (CPUID.7H.1:EAX[1] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U83: SDM-pseudocode reference (… |  |

</details>

<details><summary><b>SM4</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VSM4KEY4 | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EAX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U84: SDM-pseudocode reference (… |  |
| VSM4KEY4 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H.1:EAX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U412 EVEX SM4 (AVX10 and SM4, V… |  |
| VSM4RNDS4 | vex | 128/256 | ❌ **cannot run** (CPUID.7H.1:EAX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U84: SDM-pseudocode reference (… |  |
| VSM4RNDS4 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H.1:EAX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U412 EVEX SM4 (AVX10 and SM4, V… |  |

</details>

<details><summary><b>SMX</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| GETSEC | legacy | - | ❌ **cannot run** (CPUID.1H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U113 GETSEC/PCONFIG: SDM-pseudo… |  |

</details>

<details><summary><b>TDX</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| SEAMCALL | legacy | - | ❌ **cannot run** (Intel TDX (server, VMX root only)) |  | ⬜ queued — CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not v… |
| SEAMOPS | legacy | - | ❌ **cannot run** (Intel TDX (server, VMX root only)) |  | ⬜ queued — CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not v… |
| SEAMRET | legacy | - | ❌ **cannot run** (Intel TDX (server, VMX root only)) |  | ⬜ queued — CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not v… |
| TDCALL | legacy | - | ❌ **cannot run** (Intel TDX (server, VMX root only)) |  | ⬜ queued — CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not v… |

</details>

<details><summary><b>TSX_LDTRK</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| XRESLDTRK | legacy | - | ❌ **cannot run** (CPUID.7H:EDX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U110 TSX: SDM-pseudocode expect… |  |
| XSUSLDTRK | legacy | - | ❌ **cannot run** (CPUID.7H:EDX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U110 TSX: SDM-pseudocode expect… |  |

</details>

<details><summary><b>UINTR</b> (5 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| CLUI | legacy | - | ❌ **cannot run** (CPUID.7H:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U104 UINTR: SDM/spec-pseudocode… |  |
| SENDUIPI | legacy | - | ❌ **cannot run** (CPUID.7H:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U104 UINTR: SDM/spec-pseudocode… |  |
| STUI | legacy | - | ❌ **cannot run** (CPUID.7H:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U104 UINTR: SDM/spec-pseudocode… |  |
| TESTUI | legacy | - | ❌ **cannot run** (CPUID.7H:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U104 UINTR: SDM/spec-pseudocode… |  |
| UIRET | legacy | - | ❌ **cannot run** (CPUID.7H:EDX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U104 UINTR: SDM/spec-pseudocode… |  |

</details>

<details><summary><b>USER_MSR</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| URDMSR | legacy | - | ❌ **cannot run** (CPUID.7H.1:EDX[15] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U103 USER_MSR: SDM/spec-pseudoc… |  |
| URDMSR | vex | - | ❌ **cannot run** (CPUID.7H.1:EDX[15] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U103 USER_MSR: SDM/spec-pseudoc… |  |
| UWRMSR | legacy | - | ❌ **cannot run** (CPUID.7H.1:EDX[15] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U103 USER_MSR: SDM/spec-pseudoc… |  |
| UWRMSR | vex | - | ❌ **cannot run** (CPUID.7H.1:EDX[15] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U103 USER_MSR: SDM/spec-pseudoc… |  |

</details>

<details><summary><b>WAITPKG</b> (3 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TPAUSE | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U111 WAITPKG: SDM-pseudocode ex… |  |
| UMONITOR | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U111 WAITPKG: SDM-pseudocode ex… |  |
| UMWAIT | legacy | - | ❌ **cannot run** (CPUID.7H:ECX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U111 WAITPKG: SDM-pseudocode ex… |  |

</details>

<details><summary><b>WBNOINVD</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| WBNOINVD | legacy | - | ❌ **cannot run** (CPUID.80000008H:EBX[9] = 0 on this CPU) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 3 (D6) |

</details>

<details><summary><b>WRMSRNS</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| WRMSRNS | legacy | - | ❌ **cannot run** (CPUID.7H.1:EAX[19] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U801 WRMSRNS (NP 0F 01 C6, CPUI… |  |

</details>

<details><summary><b>AMX_AVX512</b> (5 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TCVTROWD2PS | evex | 512 | ❌ **cannot run** (AMX_AVX512 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TCVTROWPS2BF16H | evex | 512 | ❌ **cannot run** (AMX_AVX512 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TCVTROWPS2BF16L | evex | 512 | ❌ **cannot run** (AMX_AVX512 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TCVTROWPS2PHH | evex | 512 | ❌ **cannot run** (AMX_AVX512 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TCVTROWPS2PHL | evex | 512 | ❌ **cannot run** (AMX_AVX512 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |

</details>

<details><summary><b>AMX_BF16</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TDPBF16PS | vex | - | ❌ **cannot run** (CPUID.7H:EDX[22] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |

</details>

<details><summary><b>AMX_COMPLEX</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TCMMIMFP16PS | vex | - | ❌ **cannot run** (CPUID.7H.1:EDX[8] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| TCMMRLFP16PS | vex | - | ❌ **cannot run** (CPUID.7H.1:EDX[8] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |

</details>

<details><summary><b>AMX_FP16</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TDPFP16PS | vex | - | ❌ **cannot run** (CPUID.7H.1:EAX[21] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |

</details>

<details><summary><b>AMX_FP8</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TDPBF8PS | vex | - | ❌ **cannot run** (AMX_FP8 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TDPBHF8PS | vex | - | ❌ **cannot run** (AMX_FP8 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TDPHBF8PS | vex | - | ❌ **cannot run** (AMX_FP8 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TDPHF8PS | vex | - | ❌ **cannot run** (AMX_FP8 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |

</details>

<details><summary><b>AMX_INT8</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TDPBSSD | vex | - | ❌ **cannot run** (CPUID.7H:EDX[25] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| TDPBSUD | vex | - | ❌ **cannot run** (CPUID.7H:EDX[25] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| TDPBUSD | vex | - | ❌ **cannot run** (CPUID.7H:EDX[25] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| TDPBUUD | vex | - | ❌ **cannot run** (CPUID.7H:EDX[25] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |

</details>

<details><summary><b>AMX_MOVRS</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TILELOADDRS | vex | - | ❌ **cannot run** (AMX_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TILELOADDRST1 | vex | - | ❌ **cannot run** (AMX_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |

</details>

<details><summary><b>AMX_TILE</b> (3 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TILELOADD | vex | - | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| TILELOADDT1 | vex | - | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| TILESTORED | vex | - | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |

</details>

<details><summary><b>AMX_TILE_BASE</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| LDTILECFG | vex | - | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| STTILECFG | vex | - | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| TILERELEASE | vex | - | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |
| TILEZERO | vex | - | ❌ **cannot run** (CPUID.7H:EDX[24] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U175-U180 Intel AMX (VEX, UC_CT… |  |

</details>

<details><summary><b>APX_F</b> (33 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| ADC | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| ADD | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| AND | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| CRC32 | evex | - | ❌ **cannot run** (APX_F not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |
| DEC | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| DIV | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| IDIV | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| IMUL | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| INC | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| JMPABS | legacy | - | ❌ **cannot run** (APX_F not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U644/U790 Intel APX JMPABS (REX… |  |
| KMOVB | evex | - | ❌ **cannot run** (APX_F not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX-promoted KMOV (E… |  |
| KMOVD | evex | - | ❌ **cannot run** (APX_F not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX-promoted KMOV (E… |  |
| KMOVQ | evex | - | ❌ **cannot run** (APX_F not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX-promoted KMOV (E… |  |
| KMOVW | evex | - | ❌ **cannot run** (APX_F not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX-promoted KMOV (E… |  |
| MUL | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| NEG | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| NOT | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| OR | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| POPP | legacy | - | ❌ **cannot run** (APX_F not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U614 Intel APX PUSHP/POPP (REX2… |  |
| PUSHP | legacy | - | ❌ **cannot run** (APX_F not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U614 Intel APX PUSHP/POPP (REX2… |  |
| RCL | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| RCR | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| ROL | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| ROR | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| SAL | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| SAR | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| SBB | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| SHL | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| SHLD | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| SHR | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| SHRD | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| SUB | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |
| XOR | evex | - | ❌ **cannot run** (APX_F not reported by this CPU; APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |

</details>

<details><summary><b>APX_F_ADX</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| ADCX | evex | - | ❌ **cannot run** (APX_F_ADX not reported by this CPU; APX_F_ADX_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |
| ADOX | evex | - | ❌ **cannot run** (APX_F_ADX not reported by this CPU; APX_F_ADX_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |

</details>

<details><summary><b>APX_F_AMX</b> (3 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TILELOADD | evex | - | ❌ **cannot run** (APX_F_AMX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX-promoted AMX mem… |  |
| TILELOADDT1 | evex | - | ❌ **cannot run** (APX_F_AMX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646/U790 Intel APX-promoted AM… |  |
| TILESTORED | evex | - | ❌ **cannot run** (APX_F_AMX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX-promoted AMX mem… |  |

</details>

<details><summary><b>APX_F_AMX_BASE</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| LDTILECFG | evex | - | ❌ **cannot run** (APX_F_AMX_BASE not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX-promoted AMX mem… |  |
| STTILECFG | evex | - | ❌ **cannot run** (APX_F_AMX_BASE not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX-promoted AMX mem… |  |

</details>

<details><summary><b>APX_F_AMX_MOVRS</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| TILELOADDRS | evex | - | ❌ **cannot run** (APX_F_AMX_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TILELOADDRST1 | evex | - | ❌ **cannot run** (APX_F_AMX_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |

</details>

<details><summary><b>APX_F_BMI1</b> (6 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| ANDN | evex | - | ❌ **cannot run** (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| BEXTR | evex | - | ❌ **cannot run** (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| BLSI | evex | - | ❌ **cannot run** (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| BLSMSK | evex | - | ❌ **cannot run** (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| BLSR | evex | - | ❌ **cannot run** (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| TZCNT | evex | - | ❌ **cannot run** (APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |

</details>

<details><summary><b>APX_F_BMI2</b> (8 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| BZHI | evex | - | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU; APX_F_BMI2_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| MULX | evex | - | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| PDEP | evex | - | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| PEXT | evex | - | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| RORX | evex | - | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| SARX | evex | - | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| SHLX | evex | - | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |
| SHRX | evex | - | ❌ **cannot run** (APX_F_BMI2 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted BMI… |  |

</details>

<details><summary><b>APX_F_CET</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| WRSSD | evex | - | ❌ **cannot run** (APX_F_CET not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645/U790 Intel APX EVEX m… |  |
| WRSSQ | evex | - | ❌ **cannot run** (APX_F_CET not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645/U790 Intel APX EVEX m… |  |
| WRUSSD | evex | - | ❌ **cannot run** (APX_F_CET not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645/U790 Intel APX EVEX m… |  |
| WRUSSQ | evex | - | ❌ **cannot run** (APX_F_CET not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645/U790 Intel APX EVEX m… |  |

</details>

<details><summary><b>APX_F_CMPCCXADD</b> (22 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| CMPAEXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPAXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPBEXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPBXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPEXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPGEXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPGXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPLEXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPLXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNBEXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNBXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNEXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNLEXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNLXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNOXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNPXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNSXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPNZXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPOXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPPXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPSXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |
| CMPZXADD | evex | - | ❌ **cannot run** (APX_F_CMPCCXADD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U646 Intel APX VEX-promoted CMP… |  |

</details>

<details><summary><b>APX_F_ENQCMD</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| ENQCMD | evex | - | ❌ **cannot run** (APX_F_ENQCMD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645/U790 Intel APX EVEX m… |  |
| ENQCMDS | evex | - | ❌ **cannot run** (APX_F_ENQCMD not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645/U790 Intel APX EVEX m… |  |

</details>

<details><summary><b>APX_F_INVPCID</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| INVPCID | evex | - | ❌ **cannot run** (APX_F_INVPCID not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U806 INVPCID r64, m128 (APX EVE… |  |

</details>

<details><summary><b>APX_F_LZCNT</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| LZCNT | evex | - | ❌ **cannot run** (APX_F_LZCNT not reported by this CPU; APX_F_LZCNT_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |

</details>

<details><summary><b>APX_F_MOVBE</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MOVBE | evex | - | ❌ **cannot run** (APX_F_MOVBE not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |

</details>

<details><summary><b>APX_F_MOVDIR64B</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MOVDIR64B | evex | - | ❌ **cannot run** (APX_F_MOVDIR64B not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |

</details>

<details><summary><b>APX_F_MOVDIRI</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MOVDIRI | evex | - | ❌ **cannot run** (APX_F_MOVDIRI not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |

</details>

<details><summary><b>APX_F_MOVRS</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MOVRS | evex | - | ❌ **cannot run** (APX_F_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |

</details>

<details><summary><b>APX_F_MSR_IMM</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| RDMSR | evex | - | ❌ **cannot run** (APX_F_MSR_IMM not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U803 MSR-IMM RDMSR r64, imm32 (… |  |
| WRMSRNS | evex | - | ❌ **cannot run** (APX_F_MSR_IMM not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U803 MSR-IMM WRMSRNS imm32, r64… |  |

</details>

<details><summary><b>APX_F_N3</b> (84 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| CCMPB | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPBE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPF | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPL | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPLE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPNB | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPNBE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPNL | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPNLE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPNO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPNS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPNZ | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPT | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CCMPZ | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CCMPscc (EVEX ma… |  |
| CFCMOVB | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVBE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVL | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVLE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVNB | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVNBE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVNL | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVNLE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVNO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVNP | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVNS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVNZ | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVP | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CFCMOVZ | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CFCMOVcc (EVEX m… |  |
| CMOVA | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVAE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVB | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVBE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVG | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVGE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVL | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVLE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVNE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVNO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVNP | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVNS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVP | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CMOVS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U642 Intel APX CMOVcc NDD (EVEX… |  |
| CTESTB | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTBE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTF | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTL | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTLE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTNB | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTNBE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTNL | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTNLE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTNO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTNS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTNZ | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTT | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| CTESTZ | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U641 Intel APX CTESTscc (EVEX m… |  |
| POP2 | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U643 Intel APX POP2 (EVEX map 4… |  |
| POP2P | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U643 Intel APX POP2P (EVEX map … |  |
| PUSH2 | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U643 Intel APX PUSH2 (EVEX map … |  |
| PUSH2P | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U643 Intel APX PUSH2P (EVEX map… |  |
| SETA | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETAE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETB | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETBE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETG | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETGE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETL | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETLE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETNE | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETNO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETNP | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETNS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETO | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETP | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |
| SETS | evex | - | ❌ **cannot run** (APX_F_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 SETcc… |  |

</details>

<details><summary><b>APX_F_POPCNT</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| POPCNT | evex | - | ❌ **cannot run** (APX_F_POPCNT not reported by this CPU; APX_F_POPCNT_N3 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640 Intel APX EVEX map 4 promo… |  |

</details>

<details><summary><b>APX_F_RAO_INT</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| AADD | evex | - | ❌ **cannot run** (APX_F_RAO_INT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |
| AAND | evex | - | ❌ **cannot run** (APX_F_RAO_INT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |
| AOR | evex | - | ❌ **cannot run** (APX_F_RAO_INT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |
| AXOR | evex | - | ❌ **cannot run** (APX_F_RAO_INT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645 Intel APX EVEX map 4 … |  |

</details>

<details><summary><b>APX_F_USER_MSR</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| URDMSR | evex | - | ❌ **cannot run** (APX_F_USER_MSR not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645/U790 Intel APX EVEX m… |  |
| UWRMSR | evex | - | ❌ **cannot run** (APX_F_USER_MSR not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U640/U645/U790 Intel APX EVEX m… |  |

</details>

<details><summary><b>APX_F_VMX</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| INVEPT | evex | - | ❌ **cannot run** (APX_F_VMX not reported by this CPU) |  | ⬜ queued — not implemented (cases_reach): base instruction not implemented in this CPU model: VMX (INVEPT) is … |
| INVVPID | evex | - | ❌ **cannot run** (APX_F_VMX not reported by this CPU) |  | ⬜ queued — not implemented (cases_reach): base instruction not implemented in this CPU model: VMX (INVVPID) is… |

</details>

<details><summary><b>AVX10_2_BF16</b> (29 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VADDBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCMPBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCOMISBF16 | evex | 128 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VDIVBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFMADD132BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFMADD213BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFMADD231BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFMSUB132BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFMSUB213BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFMSUB231BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFNMADD132BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFNMADD213BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFNMADD231BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFNMSUB132BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFNMSUB213BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFNMSUB231BF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VFPCLASSBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VGETEXPBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VGETMANTBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMAXBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMINBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMULBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VRCPBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) |  | ⏳ open item — not bit-exact / decided open item: U373 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VREDUCEBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VRNDSCALEBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VRSQRTBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) |  | ⏳ open item — not bit-exact / decided open item: U373 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VSCALEFBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VSQRTBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VSUBBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX10_2_BF16 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |

</details>

<details><summary><b>AVX10_MOVRS</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VMOVRSB | evex | 128/256/512 | ❌ **cannot run** (AVX10_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U409 EVEX VMOVRS* (AVX10 and MO… |  |
| VMOVRSD | evex | 128/256/512 | ❌ **cannot run** (AVX10_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U409 EVEX VMOVRS* (AVX10 and MO… |  |
| VMOVRSQ | evex | 128/256/512 | ❌ **cannot run** (AVX10_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U409 EVEX VMOVRS* (AVX10 and MO… |  |
| VMOVRSW | evex | 128/256/512 | ❌ **cannot run** (AVX10_MOVRS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U409 EVEX VMOVRS* (AVX10 and MO… |  |

</details>

<details><summary><b>AVX10_V2_AUX</b> (21 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VCVTBF42HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTBF62HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTBF82BF4S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTBF82BF6S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTBF82PS | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTBIASPS2BF8 | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTBIASPS2BF8S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTBIASPS2HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTBIASPS2HF8S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTHF62HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTHF82BF4S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTHF82HF6S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTHF82PS | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTPS2BF8 | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTPS2BF8S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTPS2HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTPS2HF8S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTROPS2HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VCVTROPS2HF8S | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VPMOVSSDB | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |
| VUNPACKB | evex | 128/256/512 | ❌ **cannot run** (AVX10_V2_AUX not reported by this CPU) |  | ⏳ being implemented (wt/avx10_a, wt/avx10_b) |

</details>

<details><summary><b>AVX512BW</b> (112 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| KADDD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KADDQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KANDD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KANDND | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KANDNQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KANDQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KMOVD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KMOVQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KNOTD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KNOTQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KORD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KORQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KORTESTD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KORTESTQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KSHIFTLD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KSHIFTLQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KSHIFTRD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KSHIFTRQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KTESTD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KTESTQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KUNPCKDQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KUNPCKWD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KXNORD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KXNORQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KXORD | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KXORQ | vex | - | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| VDBPSADBW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VMOVDQU16 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VMOVDQU8 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPABSB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPABSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPACKSSDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPACKSSWB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPACKUSDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPACKUSWB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPADDB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPADDSB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPADDSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPADDUSB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPADDUSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPADDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPALIGNR | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPAVGB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPAVGW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPBLENDMB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPBLENDMW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPBROADCASTB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPBROADCASTW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPCMPB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPCMPEQB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPCMPEQW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPCMPGTB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPCMPGTW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPCMPUB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPCMPUW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPCMPW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPERMI2W | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPERMT2W | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPERMW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPEXTRB | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPEXTRW | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPINSRB | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPINSRW | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMADDUBSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMADDWD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMAXSB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMAXSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMAXUB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMAXUW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMINSB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMINSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMINUB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMINUW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVB2M | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVM2B | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVM2W | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVSWB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVSXBW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVUSWB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVW2M | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVWB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMOVZXBW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMULHRSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMULHUW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMULHW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPMULLW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSADBW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSHUFB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSHUFHW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSHUFLW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSLLDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSLLVW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSLLW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSRAVW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSRAW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSRLDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSRLVW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSRLW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSUBB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSUBSB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSUBSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSUBUSB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSUBUSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPSUBW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPTESTMB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPTESTMW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPTESTNMB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPTESTNMW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPUNPCKHBW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPUNPCKHWD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPUNPCKLBW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |
| VPUNPCKLWD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[30] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U260-U269 EVEX (AVX512BW, VL 12… |  |

</details>

<details><summary><b>AVX512CD</b> (6 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPBROADCASTMB2Q | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[28] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPBROADCASTMW2D | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[28] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPCONFLICTD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[28] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPCONFLICTQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[28] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPLZCNTD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[28] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPLZCNTQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[28] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |

</details>

<details><summary><b>AVX512DQ</b> (69 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| KADDB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KADDW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KANDB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KANDNB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KMOVB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KNOTB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KORB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KORTESTB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KSHIFTLB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KSHIFTRB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KTESTB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KTESTW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KXNORB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KXORB | vex | - | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| VANDNPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VANDNPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VANDPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VANDPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VBROADCASTF32X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTF32X8 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTF64X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTI32X2 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTI32X8 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTI64X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VCVTPD2QQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTPD2UQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTPS2QQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTPS2UQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTQQ2PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTQQ2PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTTPD2QQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTTPD2UQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTTPS2QQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTTPS2UQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTUQQ2PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VCVTUQQ2PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U233 EVEX (AVX512DQ, VL 128/256… |  |
| VEXTRACTF32X8 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXTRACTF64X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXTRACTI32X8 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXTRACTI64X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VFPCLASSPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VFPCLASSPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VFPCLASSSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VFPCLASSSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VINSERTF32X8 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VINSERTF64X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VINSERTI32X8 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VINSERTI64X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VORPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VORPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VPEXTRD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPEXTRQ | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPINSRD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPINSRQ | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVD2M | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U800 EVEX AVX512DQ (VL 128/256/… |  |
| VPMOVM2D | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U800 EVEX AVX512DQ (VL 128/256/… |  |
| VPMOVM2Q | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U800 EVEX AVX512DQ (VL 128/256/… |  |
| VPMOVQ2M | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U800 EVEX AVX512DQ (VL 128/256/… |  |
| VPMULLQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VRANGEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VRANGEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VRANGESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U295-U296 EVEX (AVX512DQ, LLIG)… |  |
| VRANGESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U295-U296 EVEX (AVX512DQ, LLIG)… |  |
| VREDUCEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VREDUCEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VREDUCESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U295-U296 EVEX (AVX512DQ, LLIG)… |  |
| VREDUCESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U295-U296 EVEX (AVX512DQ, LLIG)… |  |
| VXORPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |
| VXORPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[17] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U290-U294 EVEX (AVX512DQ, VL 12… |  |

</details>

<details><summary><b>AVX512ER</b> (10 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VEXP2PD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VEXP2PS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VRCP28PD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VRCP28PS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VRCP28SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VRCP28SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VRSQRT28PD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VRSQRT28PS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VRSQRT28SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |
| VRSQRT28SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[27] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U994 AVX512ER (SDM 092 Vol2D 8-8..8-35): documented architectural compliance, NOT bi… |

</details>

<details><summary><b>AVX512F</b> (479 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| KANDNW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KANDW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KMOVW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KNOTW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KORTESTW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KORW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KSHIFTLW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KSHIFTRW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KUNPCKBW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KXNORW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| KXORW | vex | - | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U129-U133 opmask (VEX, AVX512F/… |  |
| VADDPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VADDPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VADDSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VADDSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VALIGND | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VALIGNQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBLENDMPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VBLENDMPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VBROADCASTF32X4 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTF64X4 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTI32X4 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTI64X4 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VBROADCASTSD | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VBROADCASTSS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VCMPEQ_OSPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_OSPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_OSSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_OSSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_UQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_UQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_UQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_UQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_USPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_USPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_USSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQ_USSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPEQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPFALSE_OSPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPFALSE_OSPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPFALSE_OSSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPFALSE_OSSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPFALSEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPFALSEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPFALSESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPFALSESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGE_OQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGE_OQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGE_OQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGE_OQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGT_OQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGT_OQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGT_OQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGT_OQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGTPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGTPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGTSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPGTSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLE_OQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLE_OQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLE_OQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLE_OQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLT_OQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLT_OQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLT_OQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLT_OQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLTPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLTPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLTSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPLTSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_OQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_OQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_OQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_OQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_OSPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_OSPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_OSSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_OSSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_USPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_USPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_USSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQ_USSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNEQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGE_UQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGE_UQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGE_UQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGE_UQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGT_UQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGT_UQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGT_UQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGT_UQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGTPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGTPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGTSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNGTSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLE_UQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLE_UQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLE_UQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLE_UQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLT_UQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLT_UQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLT_UQSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLT_UQSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLTPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLTPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLTSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPNLTSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPORD_SPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPORD_SPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPORD_SSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPORD_SSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPORDPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPORDPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPORDSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPORDSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPTRUE_USPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPTRUE_USPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPTRUE_USSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPTRUE_USSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPTRUEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPTRUEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPTRUESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPTRUESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPUNORD_SPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPUNORD_SPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPUNORD_SSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPUNORD_SSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPUNORDPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPUNORDPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPUNORDSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCMPUNORDSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCOMISD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCOMISS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VCOMPRESSPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VCOMPRESSPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VCVTDQ2PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTDQ2PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTPD2DQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTPD2PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTPD2UDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTPH2PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTPS2DQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTPS2PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTPS2PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTPS2UDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTSD2SI | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTSD2SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTSD2USI | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTSI2SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTSI2SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTSS2SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTSS2SI | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTSS2USI | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTTPD2DQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTTPD2UDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTTPS2DQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTTPS2UDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTTSD2SI | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTTSD2USI | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTTSS2SI | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTTSS2USI | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTUDQ2PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTUDQ2PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTUSI2SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VCVTUSI2SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VDIVPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VDIVPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VDIVSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VDIVSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VEXPANDPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXPANDPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXTRACTF32X4 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXTRACTF64X4 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXTRACTI32X4 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXTRACTI64X4 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VEXTRACTPS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VFIXUPIMMPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VFIXUPIMMPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VFIXUPIMMSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VFIXUPIMMSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VFMADD132PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD132PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD132SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD132SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD213PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD213PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD213SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD213SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD231PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD231PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD231SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADD231SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADDSUB132PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADDSUB132PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADDSUB213PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADDSUB213PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADDSUB231PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMADDSUB231PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB132PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB132PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB132SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB132SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB213PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB213PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB213SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB213SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB231PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB231PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB231SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUB231SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUBADD132PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUBADD132PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUBADD213PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUBADD213PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUBADD231PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFMSUBADD231PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD132PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD132PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD132SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD132SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD213PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD213PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD213SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD213SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD231PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD231PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD231SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMADD231SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB132PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB132PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB132SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB132SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB213PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB213PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB213SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB213SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB231PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB231PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB231SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VFNMSUB231SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VGATHERDPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VGATHERDPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VGATHERQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VGATHERQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VGETEXPPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VGETEXPPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VGETEXPSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VGETEXPSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VGETMANTPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VGETMANTPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VGETMANTSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VGETMANTSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VINSERTF32X4 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VINSERTF64X4 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VINSERTI32X4 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VINSERTI64X4 | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VINSERTPS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMAXPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMAXPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMAXSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VMAXSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VMINPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMINPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMINSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VMINSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VMOVAPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMOVAPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMOVD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU; AVX512_MOVZXC not reported by this CPU) |  | ⏳ being implemented (M2 agents: wt/m2_engine, m2_perm, m2_cvt, m2_gather) |
| VMOVDDUP | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVDQA32 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMOVDQA64 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMOVDQU32 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMOVDQU64 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMOVHLPS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVHPD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVHPS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVLHPS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVLPD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVLPS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVNTDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVNTDQA | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVNTPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVNTPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVQ | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ being implemented (M2 agents: wt/m2_engine, m2_perm, m2_cvt, m2_gather) |
| VMOVSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VMOVSHDUP | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVSLDUP | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VMOVSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VMOVUPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMOVUPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMULPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMULPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VMULSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VMULSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VPABSD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPABSQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPADDD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPADDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPANDD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPANDND | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPANDNQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPANDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPBLENDMD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VPBLENDMQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VPBROADCASTD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPBROADCASTQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCMPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCMPEQD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCMPEQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCMPGTD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCMPGTQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCMPQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCMPUD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCMPUQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPCOMPRESSD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPCOMPRESSQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMD | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMI2D | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMI2PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMI2PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMI2Q | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMILPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMILPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMPD | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMPS | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMQ | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMT2D | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMT2PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMT2PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPERMT2Q | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPEXPANDD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPEXPANDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPGATHERDD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VPGATHERDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VPGATHERQD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VPGATHERQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VPMAXSD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMAXSQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMAXUD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMAXUQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMINSD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMINSQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMINUD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMINUQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMOVDB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVQB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVQD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVQW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSDB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSQB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSQD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSQW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSXBD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSXBQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSXDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSXWD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVSXWQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVUSDB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVUSDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVUSQB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVUSQD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVUSQW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVZXBD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVZXBQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVZXDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVZXWD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMOVZXWQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPMULDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMULLD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPMULUDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPORD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPORQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPROLD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPROLQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPROLVD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPROLVQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPRORD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPRORQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPRORVD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPRORVQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPSCATTERDD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VPSCATTERDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VPSCATTERQD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VPSCATTERQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VPSHUFD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPSLLD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPSLLQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPSLLVD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPSLLVQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPSRAD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPSRAQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPSRAVD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPSRAVQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPSRLD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPSRLQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VPSRLVD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPSRLVQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPSUBD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPSUBQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPTERNLOGD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPTERNLOGQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPTESTMD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPTESTMQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPTESTNMD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPTESTNMQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPUNPCKHDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPUNPCKHQDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPUNPCKLDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPUNPCKLQDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VPXORD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VPXORQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VRCP14PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U236/U940 documented architectural compliance (decision A9, U940-U943, docs/reciproc… |
| VRCP14PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U236/U940 documented architectural compliance (decision A9, U940-U943, docs/reciproc… |
| VRCP14SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U236/U940 documented architectural compliance (decision A9, U940-U943, docs/reciproc… |
| VRCP14SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U236/U940 documented architectural compliance (decision A9, U940-U943, docs/reciproc… |
| VRNDSCALEPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VRNDSCALEPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VRNDSCALESD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VRNDSCALESS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VRSQRT14PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U236 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VRSQRT14PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U236 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VRSQRT14SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U236 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VRSQRT14SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U236 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VSCALEFPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VSCALEFPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VSCALEFSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VSCALEFSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U230-U241 EVEX (AVX512F, VL 128… |  |
| VSCATTERDPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VSCATTERDPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VSCATTERQPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VSCATTERQPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U250-U251 EVEX (AVX512F, VL 128… |  |
| VSHUFF32X4 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VSHUFF64X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VSHUFI32X4 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VSHUFI64X2 | evex | 256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VSHUFPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VSHUFPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VSQRTPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VSQRTPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VSQRTSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VSQRTSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VSUBPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VSUBPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U141-U159 EVEX (AVX512F, VL 128… |  |
| VSUBSD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VSUBSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VUCOMISD | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VUCOMISS | evex | 128 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U190-U201 EVEX (AVX512F; scalar… |  |
| VUNPCKHPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VUNPCKHPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VUNPCKLPD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |
| VUNPCKLPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[16] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U210-U215 EVEX (AVX512F/DQ, VL … |  |

</details>

<details><summary><b>AVX512PF</b> (16 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VGATHERPF0DPD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VGATHERPF0DPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VGATHERPF0QPD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VGATHERPF0QPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VGATHERPF1DPD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VGATHERPF1DPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VGATHERPF1QPD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VGATHERPF1QPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VSCATTERPF0DPD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VSCATTERPF0DPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VSCATTERPF0QPD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VSCATTERPF0QPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VSCATTERPF1DPD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VSCATTERPF1DPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VSCATTERPF1QPD | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |
| VSCATTERPF1QPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EBX[26] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U993 AVX512PF (EVEX.512.66.0F38… |  |

</details>

<details><summary><b>AVX512_4FMAPS</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| V4FMADDPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EDX[3] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U992 AVX512_4FMAPS (EVEX.F2.0F3… |  |
| V4FMADDSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[3] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U992 AVX512_4FMAPS (EVEX.F2.0F3… |  |
| V4FNMADDPS | evex | 512 | ❌ **cannot run** (CPUID.7H:EDX[3] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U992 AVX512_4FMAPS (EVEX.F2.0F3… |  |
| V4FNMADDSS | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[3] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U992 AVX512_4FMAPS (EVEX.F2.0F3… |  |

</details>

<details><summary><b>AVX512_4VNNIW</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VP4DPWSSD | evex | 512 | ❌ **cannot run** (CPUID.7H:EDX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U991 AVX512_4VNNIW (EVEX.512.F2… |  |
| VP4DPWSSDS | evex | 512 | ❌ **cannot run** (CPUID.7H:EDX[2] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U991 AVX512_4VNNIW (EVEX.512.F2… |  |

</details>

<details><summary><b>AVX512_BF16</b> (3 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VCVTNE2PS2BF16 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H.1:EAX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U556 EVEX AVX512_BF16 (VL 128/2… |  |
| VCVTNEPS2BF16 | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H.1:EAX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U556 EVEX AVX512_BF16 (VL 128/2… |  |
| VDPBF16PS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H.1:EAX[5] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U557 EVEX AVX512_BF16 (VL 128/2… |  |

</details>

<details><summary><b>AVX512_BITALG</b> (3 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPOPCNTB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[12] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPOPCNTW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[12] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPSHUFBITQMB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[12] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |

</details>

<details><summary><b>AVX512_COM_EF</b> (6 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VCOMXSD | evex | 128 | ❌ **cannot run** (AVX512_COM_EF not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCOMXSH | evex | 128 | ❌ **cannot run** (AVX512_COM_EF not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCOMXSS | evex | 128 | ❌ **cannot run** (AVX512_COM_EF not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VUCOMXSD | evex | 128 | ❌ **cannot run** (AVX512_COM_EF not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VUCOMXSH | evex | 128 | ❌ **cannot run** (AVX512_COM_EF not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VUCOMXSS | evex | 128 | ❌ **cannot run** (AVX512_COM_EF not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |

</details>

<details><summary><b>AVX512_FP16</b> (170 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VADDPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VADDSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPEQ_OSPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPEQ_OSSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPEQ_UQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPEQ_UQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPEQ_USPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPEQ_USSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPEQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPEQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPFALSE_OSPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPFALSE_OSSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPFALSEPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPFALSESH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPGE_OQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPGE_OQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPGEPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPGESH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPGT_OQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPGT_OQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPGTPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPGTSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPLE_OQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPLE_OQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPLEPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPLESH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPLT_OQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPLT_OQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPLTPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPLTSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNEQ_OQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNEQ_OQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNEQ_OSPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNEQ_OSSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNEQ_USPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNEQ_USSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNEQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNEQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNGE_UQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNGE_UQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNGEPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNGESH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNGT_UQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNGT_UQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNGTPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNGTSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNLE_UQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNLE_UQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNLEPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNLESH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNLT_UQPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNLT_UQSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNLTPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPNLTSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPORD_SPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPORD_SSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPORDPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPORDSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPTRUE_USPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPTRUE_USSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPTRUEPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPTRUESH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPUNORD_SPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPUNORD_SSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPUNORDPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCMPUNORDSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCOMISH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTDQ2PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPD2PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPH2DQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPH2PD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPH2PSX | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPH2QQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPH2UDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPH2UQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPH2UW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPH2W | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTPS2PHX | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTQQ2PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTSD2SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTSH2SD | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTSH2SI | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTSH2SS | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTSH2USI | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTSI2SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTSS2SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTTPH2DQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTTPH2QQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTTPH2UDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTTPH2UQQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTTPH2UW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTTPH2W | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTTSH2SI | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTTSH2USI | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTUDQ2PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTUQQ2PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTUSI2SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTUW2PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VCVTW2PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VDIVPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VDIVSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFCMADDCPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFCMADDCSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFCMULCPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFCMULCSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADD132PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADD132SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADD213PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADD213SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADD231PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADD231SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADDCPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADDCSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADDSUB132PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADDSUB213PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMADDSUB231PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUB132PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUB132SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUB213PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUB213SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUB231PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUB231SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUBADD132PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUBADD213PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMSUBADD231PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMULCPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFMULCSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMADD132PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMADD132SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMADD213PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMADD213SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMADD231PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMADD231SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMSUB132PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMSUB132SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMSUB213PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMSUB213SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMSUB231PH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFNMSUB231SH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFPCLASSPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VFPCLASSSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VGETEXPPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VGETEXPSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VGETMANTPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VGETMANTSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VMAXPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VMAXSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VMINPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VMINSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VMOVSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VMOVW | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU; AVX512_MOVZXC not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VMULPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VMULSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VRCPPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U337 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VRCPSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U337 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VREDUCEPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VREDUCESH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VRNDSCALEPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VRNDSCALESH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VRSQRTPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U337 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VRSQRTSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) |  | ⏳ open item — not bit-exact / decided open item: U337 documented architectural compliance (decision A9, U940-U943, docs/reciprocal.md… |
| VSCALEFPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VSCALEFSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VSQRTPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VSQRTSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VSUBPH | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VSUBSH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |
| VUCOMISH | evex | 128 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U330-U339 AVX512-FP16 (maps 5/6… |  |

</details>

<details><summary><b>AVX512_FP16_CONVERT</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VCVT2PS2PHX | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[23] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U404 AVX10.2 VCVT2PS2PHX (VL 12… |  |

</details>

<details><summary><b>AVX512_FP8_CONVERT</b> (13 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VCVT2PH2BF8 | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVT2PH2BF8S | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVT2PH2HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVT2PH2HF8S | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVTBIASPH2BF8 | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVTBIASPH2BF8S | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVTBIASPH2HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVTBIASPH2HF8S | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVTHF82PH | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U403 AVX10.2 VCVTHF82PH (VL 128… |  |
| VCVTPH2BF8 | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVTPH2BF8S | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVTPH2HF8 | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |
| VCVTPH2HF8S | evex | 128/256/512 | ❌ **cannot run** (AVX512_FP8_CONVERT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U402 AVX10.2 FP16->FP8 (VL 128/… |  |

</details>

<details><summary><b>AVX512_GFNI</b> (3 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VGF2P8AFFINEINVQB | evex | 128/256/512 | ❌ **cannot run** (AVX512_GFNI not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |
| VGF2P8AFFINEQB | evex | 128/256/512 | ❌ **cannot run** (AVX512_GFNI not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |
| VGF2P8MULB | evex | 128/256/512 | ❌ **cannot run** (AVX512_GFNI not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |

</details>

<details><summary><b>AVX512_IFMA</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPMADD52HUQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[21] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPMADD52LUQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EBX[21] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |

</details>

<details><summary><b>AVX512_MEDIAX</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VMPSADBW | evex | 128/256/512 | ❌ **cannot run** (AVX512_MEDIAX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U408 AVX10.2 EVEX VMPSADBW (VL … |  |

</details>

<details><summary><b>AVX512_MINMAX</b> (7 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VMINMAXBF16 | evex | 128/256/512 | ❌ **cannot run** (AVX512_MINMAX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMINMAXPD | evex | 128/256/512 | ❌ **cannot run** (AVX512_MINMAX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMINMAXPH | evex | 128/256/512 | ❌ **cannot run** (AVX512_MINMAX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMINMAXPS | evex | 128/256/512 | ❌ **cannot run** (AVX512_MINMAX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMINMAXSD | evex | 128 | ❌ **cannot run** (AVX512_MINMAX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMINMAXSH | evex | 128 | ❌ **cannot run** (AVX512_MINMAX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VMINMAXSS | evex | 128 | ❌ **cannot run** (AVX512_MINMAX not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |

</details>

<details><summary><b>AVX512_SAT_CVT</b> (12 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VCVTBF162IBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTBF162IUBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTPH2IBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTPH2IUBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTPS2IBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTPS2IUBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTBF162IBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTBF162IUBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPH2IBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPH2IUBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPS2IBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPS2IUBS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |

</details>

<details><summary><b>AVX512_SAT_CVT_DS</b> (12 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VCVTTPD2DQS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPD2QQS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPD2UDQS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPD2UQQS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPS2DQS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPS2QQS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPS2UDQS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTPS2UQQS | evex | 128/256/512 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTSD2SIS | evex | 128 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTSD2USIS | evex | 128 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTSS2SIS | evex | 128 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |
| VCVTTSS2USIS | evex | 128 | ❌ **cannot run** (AVX512_SAT_CVT_DS not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U372-U376 AVX10.2 (maps 5/6 + 0… |  |

</details>

<details><summary><b>AVX512_VAES</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VAESDEC | evex | 128/256/512 | ❌ **cannot run** (AVX512_VAES not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |
| VAESDECLAST | evex | 128/256/512 | ❌ **cannot run** (AVX512_VAES not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |
| VAESENC | evex | 128/256/512 | ❌ **cannot run** (AVX512_VAES not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |
| VAESENCLAST | evex | 128/256/512 | ❌ **cannot run** (AVX512_VAES not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |

</details>

<details><summary><b>AVX512_VBMI</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPERMB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[1] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPERMI2B | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[1] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPERMT2B | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[1] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPMULTISHIFTQB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[1] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U554 EVEX AVX512_VBMI (VL 128/2… |  |

</details>

<details><summary><b>AVX512_VBMI2</b> (16 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPCOMPRESSB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U551 EVEX AVX512_VBMI2 (VL… |  |
| VPCOMPRESSW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U551 EVEX AVX512_VBMI2 (VL… |  |
| VPEXPANDB | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U551 EVEX AVX512_VBMI2 (VL… |  |
| VPEXPANDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U551 EVEX AVX512_VBMI2 (VL… |  |
| VPSHLDD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHLDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHLDVD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHLDVQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHLDVW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHLDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHRDD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHRDQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHRDVD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHRDVQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHRDVW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |
| VPSHRDW | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[6] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U550-U553 EVEX AVX512_VBMI2 (VL… |  |

</details>

<details><summary><b>AVX512_VNNI</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPDPBUSD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U555 EVEX AVX512_VNNI (VL 128/2… |  |
| VPDPBUSDS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U555 EVEX AVX512_VNNI (VL 128/2… |  |
| VPDPWSSD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U555 EVEX AVX512_VNNI (VL 128/2… |  |
| VPDPWSSDS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U555 EVEX AVX512_VNNI (VL 128/2… |  |

</details>

<details><summary><b>AVX512_VNNI_FP16</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VDPPHPS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U407 AVX10.2 VDPPHPS (VL 128/25… |  |

</details>

<details><summary><b>AVX512_VNNI_INT16</b> (6 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPDPWSUD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPWSUDS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPWUSD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPWUSDS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPWUUD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPWUUDS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |

</details>

<details><summary><b>AVX512_VNNI_INT8</b> (6 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPDPBSSD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPBSSDS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPBSUD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPBSUDS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPBUUD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |
| VPDPBUUDS | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[11] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U405/U406 AVX10.2 EVEX VNNI (VL… |  |

</details>

<details><summary><b>AVX512_VP2INTERSECT</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VP2INTERSECTD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[8] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |
| VP2INTERSECTQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:EDX[8] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |

</details>

<details><summary><b>AVX512_VPCLMULQDQ</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPCLMULQDQ | evex | 128/256/512 | ❌ **cannot run** (AVX512_VPCLMULQDQ not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U570-U574 EVEX (AVX512_VP2INTER… |  |

</details>

<details><summary><b>AVX512_VPOPCNTDQ</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VPOPCNTD | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |
| VPOPCNTQ | evex | 128/256/512 | ❌ **cannot run** (CPUID.7H:ECX[14] = 0 on this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U320-U325 EVEX (AVX512CD / AVX5… |  |

</details>

