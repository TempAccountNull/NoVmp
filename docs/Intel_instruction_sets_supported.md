# Instruction sets supported (Intel + AMD/VIA)

_Generated 2026-10-28 13:12 from `Emulator/tools/isa/gen_status_docs.py` (HEAD `4c89d060 Ledger: U2165-U2172 (agent/p3m7e object W1 - \Device\KsecDD /
\Device\CNG as devices, test PE T9, --HLE BCryptGenRandom, run 12's services), merged and verified`). Do not edit by hand._

Every instruction form (mnemonic + encoding) of the Intel SDM / XED list `Emulator/data/isa_manual_forms.tsv`, AMD and VIA forms included: **2912 forms, each
listed exactly once**, in one of three tables by privilege class (`Emulator/data/isa_privilege.tsv`). Checked against an Intel i5-13600K (Raptor Lake) with
`emu-alltest` (hardware sweep and `--cases` files) and, for forms this CPU cannot run, against expected values derived from the SDM pseudocode.

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

Per profile (U1632): 19 of the ❌ forms are enumerated by the bare host (`--host`) and hidden only by the VMware guest (`--guest` / `--vm`); every other reason
holds on both.

AMD / VIA forms (Vendor column; "both" = AMD-originated forms the i5-13600K also runs): implemented after every Intel instruction (decision 2026-10-07, plan
1.15f) from the vendor manuals; a switch `__use_AMD_instruction_set__` (default 0 = #UD as on Intel) will gate them. 3DNow!, SSE4a and FEMMS already exist in
QEMU and run under the non-strict MAX model; under the i5-13600K profile with `--strict` they are #UD like the CPU.

## Currently being added

- (nothing in progress)

## Usermode (no emu driver required)

2813 forms whose privilege rules allow CPL3 under Windows: no CPL / IOPL / CR4 privilege condition on their SDM page, or one Windows does not trigger (CR4.TSD =
0, CR4.UMIP = 0, CR4.FSGSBASE = 1; IOPL only in virtual-8086 mode), as listed in `Emulator/data/isa_privilege.tsv`. Whether the i5-13600K itself can run a form
is the i5-13600K column.

### By family (usermode)

| family | vendor | forms | ✅ | ⏳ | ⬜ | ❌ | i5-13600K cannot run it because |
|---|---|---|---|---|---|---|---|
| 3DNOW | AMD | 25 |  |  |  | 25 | AMD 3DNow!, on --host and --guest |
| ACE_1 | Intel | 16 |  |  |  | 16 | CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest (+1 more) |
| ADOX_ADCX | Intel | 2 | 2 |  |  |  |  |
| AES | Intel | 6 | 6 |  |  |  |  |
| AMD | both | 2 | 2 |  |  |  |  |
| AVX | Intel | 381 | 381 |  |  |  |  |
| AVX2 | Intel | 20 | 20 |  |  |  |  |
| AVX2GATHER | Intel | 8 | 8 |  |  |  |  |
| AVXAES | Intel | 6 | 6 |  |  |  |  |
| AVX_GFNI | Intel | 3 | 3 |  |  |  |  |
| AVX_IFMA | Intel | 2 |  |  |  | 2 | CPUID.7H.1:EAX[23] = 0 on --host and --guest |
| AVX_NE_CONVERT | Intel | 7 |  |  |  | 7 | CPUID.7H.1:EDX[5] = 0 on --host and --guest |
| AVX_VNNI | Intel | 4 | 4 |  |  |  |  |
| AVX_VNNI_INT16 | Intel | 6 |  |  |  | 6 | CPUID.7H.1:EDX[10] = 0 on --host and --guest |
| AVX_VNNI_INT8 | Intel | 6 |  |  |  | 6 | CPUID.7H.1:EDX[4] = 0 on --host and --guest |
| BMI1 | Intel | 6 | 6 |  |  |  |  |
| BMI2 | Intel | 8 | 8 |  |  |  |  |
| CET | Intel | 10 | 4 |  |  | 6 | the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host … |
| CLDEMOTE | Intel | 1 |  |  |  | 1 | CPUID.7H:ECX[25] = 0 on --host and --guest |
| CLFLUSHOPT | Intel | 1 | 1 |  |  |  |  |
| CLFSH | Intel | 1 | 1 |  |  |  |  |
| CLWB | Intel | 1 | 1 |  |  |  |  |
| CLZERO | AMD | 1 |  |  |  | 1 | AMD CLZERO, on --host and --guest |
| CMOV | Intel | 16 | 16 |  |  |  |  |
| CMPCCXADD | Intel | 22 |  |  |  | 22 | CPUID.7H.1:EAX[7] = 0 on --host and --guest |
| CMPXCHG16B | Intel | 1 | 1 |  |  |  |  |
| ENQCMD | Intel | 1 |  |  |  | 1 | CPUID.7H:ECX[29] = 0 on --host and --guest |
| F16C | Intel | 2 | 2 |  |  |  |  |
| FAT_NOP | Intel | 8 | 8 |  |  |  |  |
| FCMOV | Intel | 9 | 9 |  |  |  |  |
| FCOMI | Intel | 6 | 6 |  |  |  |  |
| FMA | Intel | 60 | 60 |  |  |  |  |
| FMA4 | AMD | 20 |  |  |  | 20 | AMD FMA4, on --host and --guest |
| FXSAVE | Intel | 2 | 2 |  |  |  |  |
| FXSAVE64 | Intel | 2 | 2 |  |  |  |  |
| GFNI | Intel | 3 | 3 |  |  |  |  |
| HLE | Intel | 2 |  |  |  | 2 | CPUID.7H:EBX[4] = 0 on --host and --guest |
| I186 | Intel | 15 | 15 |  |  |  |  |
| I286PROTECTED | Intel | 7 | 5 | 2 |  |  |  |
| I286REAL | Intel | 3 |  | 3 |  |  |  |
| I386 | Intel | 45 | 44 | 1 |  |  |  |
| I486REAL | Intel | 4 | 4 |  |  |  |  |
| I86 | Intel | 83 | 80 | 3 |  |  |  |
| IBHF | Intel | 1 |  |  |  | 1 | not reported by this CPU, on --host and --guest |
| ICACHE_PREFETCH | Intel | 2 |  |  |  | 2 | CPUID.7H.1:EDX[14] = 0 on --host and --guest |
| KEYLOCKER | Intel | 6 |  |  |  | 6 | --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] … |
| KEYLOCKER_WIDE | Intel | 4 |  |  |  | 4 | --guest: CPUID.19H:EBX[2] = 0; --host: enumerated (CPUID.19H:EBX[2] = 1 on the bare host), CPUID.19H:EBX[0] … |
| LAHF | Intel | 2 | 2 |  |  |  |  |
| LONGMODE | Intel | 12 | 11 | 1 |  |  |  |
| LWP | AMD | 4 |  |  |  | 4 | AMD LWP, on --host and --guest |
| MCOMMIT | AMD | 1 |  |  |  | 1 | AMD MCOMMIT, on --host and --guest |
| MONITORX | AMD | 2 |  |  |  | 2 | AMD MONITORX, on --host and --guest |
| MOVBE | Intel | 1 | 1 |  |  |  |  |
| MOVDIR64B | Intel | 1 | 1 |  |  |  |  |
| MOVDIRI | Intel | 1 | 1 |  |  |  |  |
| MOVRS | Intel | 2 |  |  |  | 2 | CPUID.7H.1:EAX[31] = 0 on --host and --guest |
| MPX | Intel | 7 |  |  |  | 7 | CPUID.7H:EBX[14] = 0 on --host and --guest |
| PAUSE | Intel | 1 | 1 |  |  |  |  |
| PCLMULQDQ | Intel | 1 | 1 |  |  |  |  |
| PENTIUMMMX | Intel | 60 | 60 |  |  |  |  |
| PENTIUMREAL | Intel | 2 | 1 | 1 |  |  |  |
| PKU | Intel | 2 |  |  |  | 2 | the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode, on --host and --guest |
| POPCNT | Intel | 1 | 1 |  |  |  |  |
| PPRO | Intel | 2 | 2 |  |  |  |  |
| PPRO_UD0_LONG | Intel | 1 | 1 |  |  |  |  |
| PREFETCHWT1 | Intel | 1 |  |  |  | 1 | CPUID.7H:ECX[0] = 0 on --host and --guest |
| PREFETCH_NOP | Intel | 2 | 2 |  |  |  |  |
| PREFIX | Intel | 11 | 11 |  |  |  |  |
| PTWRITE | Intel | 1 | 1 |  |  |  |  |
| RAO_INT | Intel | 4 |  |  |  | 4 | CPUID.7H.1:EAX[3] = 0 on --host and --guest |
| RDPID | Intel | 1 |  | 1 |  |  |  |
| RDPRU | AMD | 1 |  |  |  | 1 | AMD RDPRU, on --host and --guest |
| RDRAND | Intel | 1 | 1 |  |  |  |  |
| RDSEED | Intel | 1 | 1 |  |  |  |  |
| RDTSCP | Intel | 1 |  | 1 |  |  |  |
| RDWRFSGS | Intel | 4 | 3 | 1 |  |  |  |
| RTM | Intel | 4 |  |  |  | 4 | CPUID.7H:EBX[11] = 0 on --host and --guest |
| SEP | Intel | 1 | 1 |  |  |  |  |
| SERIALIZE | Intel | 1 | 1 |  |  |  |  |
| SGX | Intel | 1 |  |  |  | 1 | CPUID.7H:EBX[2] = 0 on --host and --guest |
| SHA | Intel | 7 | 7 |  |  |  |  |
| SHA512 | Intel | 3 |  |  |  | 3 | CPUID.7H.1:EAX[0] = 0 on --host and --guest |
| SM3 | Intel | 3 |  |  |  | 3 | CPUID.7H.1:EAX[1] = 0 on --host and --guest |
| SM4 | Intel | 4 |  |  |  | 4 | CPUID.7H.1:EAX[2] = 0 on --host and --guest |
| SSE | Intel | 110 | 110 |  |  |  |  |
| SSE2 | Intel | 129 | 129 |  |  |  |  |
| SSE3 | Intel | 10 | 10 |  |  |  |  |
| SSE3X87 | Intel | 1 | 1 |  |  |  |  |
| SSE4 | Intel | 48 | 48 |  |  |  |  |
| SSE42 | Intel | 6 | 6 |  |  |  |  |
| SSE4a | AMD | 4 |  |  |  | 4 | AMD SSE4a, on --host and --guest |
| SSEMXCSR | Intel | 2 | 2 |  |  |  |  |
| SSE_PREFETCH | Intel | 4 | 4 |  |  |  |  |
| SSSE3 | Intel | 16 | 16 |  |  |  |  |
| TBM | AMD | 10 |  |  |  | 10 | AMD TBM, on --host and --guest |
| TSX_LDTRK | Intel | 2 |  |  |  | 2 | CPUID.7H:EDX[16] = 0 on --host and --guest |
| UINTR | Intel | 5 |  |  |  | 5 | CPUID.7H:EDX[5] = 0 on --host and --guest |
| USER_MSR | Intel | 4 |  |  |  | 4 | CPUID.7H.1:EDX[15] = 0 on --host and --guest |
| VIA_PADLOCK_AES | VIA | 5 |  |  |  | 5 | VIA PadLock, on --host and --guest |
| VIA_PADLOCK_MONTMUL | VIA | 1 |  |  |  | 1 | VIA PadLock, on --host and --guest |
| VIA_PADLOCK_RNG | VIA | 1 |  |  |  | 1 | VIA PadLock, on --host and --guest |
| VIA_PADLOCK_SHA | VIA | 2 |  |  |  | 2 | VIA PadLock, on --host and --guest |
| WAITPKG | Intel | 3 |  |  |  | 3 | --guest: CPUID.7H:ECX[5] = 0; --host: enumerated (CPUID.7H:ECX[5] = 1 on the bare host) |
| X87 | Intel | 85 | 85 |  |  |  |  |
| XOP | AMD | 119 |  |  |  | 119 | AMD XOP, on --host and --guest |
| XSAVE | Intel | 5 | 5 |  |  |  |  |
| XSAVEC | Intel | 2 | 2 |  |  |  |  |
| XSAVEOPT | Intel | 2 | 2 |  |  |  |  |
| AMX_AVX512 | Intel | 5 |  |  |  | 5 | AMX_AVX512 not reported by this CPU, on --host and --guest |
| AMX_BF16 | Intel | 1 |  |  |  | 1 | CPUID.7H:EDX[22] = 0 on --host and --guest |
| AMX_COMPLEX | Intel | 2 |  |  |  | 2 | CPUID.7H.1:EDX[8] = 0 on --host and --guest |
| AMX_FP16 | Intel | 1 |  |  |  | 1 | CPUID.7H.1:EAX[21] = 0 on --host and --guest |
| AMX_FP8 | Intel | 4 |  |  |  | 4 | AMX_FP8 not reported by this CPU, on --host and --guest |
| AMX_INT8 | Intel | 4 |  |  |  | 4 | CPUID.7H:EDX[25] = 0 on --host and --guest |
| AMX_MOVRS | Intel | 2 |  |  |  | 2 | AMX_MOVRS not reported by this CPU, on --host and --guest |
| AMX_TF32 | Intel | 1 |  |  |  | 1 | AMX_TF32 not reported by this CPU, on --host and --guest |
| AMX_TILE | Intel | 3 |  |  |  | 3 | CPUID.7H:EDX[24] = 0 on --host and --guest |
| AMX_TILE_BASE | Intel | 4 |  |  |  | 4 | CPUID.7H:EDX[24] = 0 on --host and --guest |
| APX_F | Intel | 33 |  |  |  | 33 | APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest (+1 more) |
| APX_F_ADX | Intel | 2 |  |  |  | 2 | APX_F_ADX not reported by this CPU; APX_F_ADX_N3 not reported by this CPU, on --host and --guest |
| APX_F_AMX | Intel | 3 |  |  |  | 3 | APX_F_AMX not reported by this CPU, on --host and --guest |
| APX_F_AMX_BASE | Intel | 2 |  |  |  | 2 | APX_F_AMX_BASE not reported by this CPU, on --host and --guest |
| APX_F_AMX_MOVRS | Intel | 2 |  |  |  | 2 | APX_F_AMX_MOVRS not reported by this CPU, on --host and --guest |
| APX_F_BMI1 | Intel | 6 |  |  |  | 6 | APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU, on --host and --guest |
| APX_F_BMI2 | Intel | 8 |  |  |  | 8 | APX_F_BMI2 not reported by this CPU; APX_F_BMI2_N3 not reported by this CPU, on --host and --guest (+1 more) |
| APX_F_CET | Intel | 2 |  |  |  | 2 | APX_F_CET not reported by this CPU, on --host and --guest |
| APX_F_CMPCCXADD | Intel | 22 |  |  |  | 22 | APX_F_CMPCCXADD not reported by this CPU, on --host and --guest |
| APX_F_ENQCMD | Intel | 1 |  |  |  | 1 | APX_F_ENQCMD not reported by this CPU, on --host and --guest |
| APX_F_LZCNT | Intel | 1 |  |  |  | 1 | APX_F_LZCNT not reported by this CPU; APX_F_LZCNT_N3 not reported by this CPU, on --host and --guest |
| APX_F_MOVBE | Intel | 1 |  |  |  | 1 | APX_F_MOVBE not reported by this CPU, on --host and --guest |
| APX_F_MOVDIR64B | Intel | 1 |  |  |  | 1 | APX_F_MOVDIR64B not reported by this CPU, on --host and --guest |
| APX_F_MOVDIRI | Intel | 1 |  |  |  | 1 | APX_F_MOVDIRI not reported by this CPU, on --host and --guest |
| APX_F_MOVRS | Intel | 1 |  |  |  | 1 | APX_F_MOVRS not reported by this CPU, on --host and --guest |
| APX_F_N3 | Intel | 84 |  |  |  | 84 | APX_F_N3 not reported by this CPU, on --host and --guest |
| APX_F_POPCNT | Intel | 1 |  |  |  | 1 | APX_F_POPCNT not reported by this CPU; APX_F_POPCNT_N3 not reported by this CPU, on --host and --guest |
| APX_F_RAO_INT | Intel | 4 |  |  |  | 4 | APX_F_RAO_INT not reported by this CPU, on --host and --guest |
| APX_F_USER_MSR | Intel | 2 |  |  |  | 2 | APX_F_USER_MSR not reported by this CPU, on --host and --guest |
| AVX10_2_BF16 | Intel | 29 |  |  |  | 29 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 |
| AVX10_MOVRS | Intel | 4 |  |  |  | 4 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND MOVRS |
| AVX10_V2_AUX | Intel | 21 |  |  |  | 21 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX |
| AVX512BW | Intel | 112 |  |  |  | 112 | CPUID.7H:EBX[30] = 0 on --host and --guest |
| AVX512CD | Intel | 6 |  |  |  | 6 | CPUID.7H:EBX[28] = 0 on --host and --guest |
| AVX512DQ | Intel | 69 |  |  |  | 69 | CPUID.7H:EBX[17] = 0 on --host and --guest |
| AVX512ER | Intel | 10 |  |  |  | 10 | CPUID.7H:EBX[27] = 0 on --host and --guest |
| AVX512F | Intel | 479 |  |  |  | 479 | CPUID.7H:EBX[16] = 0 on --host and --guest (+1 more) |
| AVX512PF | Intel | 16 |  |  |  | 16 | CPUID.7H:EBX[26] = 0 on --host and --guest |
| AVX512_4FMAPS | Intel | 4 |  |  |  | 4 | CPUID.7H:EDX[3] = 0 on --host and --guest |
| AVX512_4VNNIW | Intel | 2 |  |  |  | 2 | CPUID.7H:EDX[2] = 0 on --host and --guest |
| AVX512_BF16 | Intel | 3 |  |  |  | 3 | CPUID.7H.1:EAX[5] = 0 on --host and --guest |
| AVX512_BITALG | Intel | 3 |  |  |  | 3 | CPUID.7H:ECX[12] = 0 on --host and --guest |
| AVX512_COM_EF | Intel | 6 |  |  |  | 6 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 |
| AVX512_FP16 | Intel | 170 |  |  |  | 170 | CPUID.7H:EDX[23] = 0 on --host and --guest (+1 more) |
| AVX512_FP16_CONVERT | Intel | 1 |  |  |  | 1 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX |
| AVX512_FP8_CONVERT | Intel | 13 |  |  |  | 13 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX |
| AVX512_GFNI | Intel | 3 |  |  |  | 3 | AVX512_GFNI not reported by this CPU, on --host and --guest |
| AVX512_IFMA | Intel | 2 |  |  |  | 2 | CPUID.7H:EBX[21] = 0 on --host and --guest |
| AVX512_MEDIAX | Intel | 1 |  |  |  | 1 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 |
| AVX512_MINMAX | Intel | 7 |  |  |  | 7 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 |
| AVX512_SAT_CVT | Intel | 12 |  |  |  | 12 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 |
| AVX512_SAT_CVT_DS | Intel | 12 |  |  |  | 12 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 |
| AVX512_VAES | Intel | 4 |  |  |  | 4 | AVX512_VAES not reported by this CPU, on --host and --guest |
| AVX512_VBMI | Intel | 4 |  |  |  | 4 | CPUID.7H:ECX[1] = 0 on --host and --guest |
| AVX512_VBMI2 | Intel | 16 |  |  |  | 16 | CPUID.7H:ECX[6] = 0 on --host and --guest |
| AVX512_VNNI | Intel | 4 |  |  |  | 4 | CPUID.7H:ECX[11] = 0 on --host and --guest |
| AVX512_VNNI_FP16 | Intel | 1 |  |  |  | 1 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 |
| AVX512_VNNI_INT16 | Intel | 6 |  |  |  | 6 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX |
| AVX512_VNNI_INT8 | Intel | 6 |  |  |  | 6 | CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX |
| AVX512_VP2INTERSECT | Intel | 2 |  |  |  | 2 | CPUID.7H:EDX[8] = 0 on --host and --guest |
| AVX512_VPCLMULQDQ | Intel | 1 |  |  |  | 1 | AVX512_VPCLMULQDQ not reported by this CPU, on --host and --guest |
| AVX512_VPOPCNTDQ | Intel | 2 |  |  |  | 2 | CPUID.7H:ECX[14] = 0 on --host and --guest |

### Usermode forms (2813)

| instruction | encoding | vector bits | vendor | family | i5-13600K | Status | verified by |
|---|---|---|---|---|---|---|---|
| FEMMS | legacy | - | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⏳ open item | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| PAVGUSB | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PF2ID | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PF2IW | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFACC | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFADD | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFCMPEQ | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFCMPGE | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFCMPGT | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFMAX | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFMIN | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFMUL | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFNACC | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFPNACC | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFRCP | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFRCPIT1 | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFRCPIT2 | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFRSQIT1 | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFRSQRT | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFSUB | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PFSUBR | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PI2FD | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PI2FW | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PMULHRW | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| PSWAPD | legacy | 64 | AMD | 3DNOW | cannot: AMD 3DNow!, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD 3DNow! |
| BSRINIT | vex | - | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (cases_reach): U1094 ACE 1.15 tile instruction (CPUID.(07H,1):ECX[11] ACE, chapters 10-14; XED ISA set ACE_1): not implemented … |
| BSRMOVF | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| BSRMOVH | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| BSRMOVL | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TILEMOVCOL | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TILEMOVROW | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest; AMX_AVX512 not reported by this CPU | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TOP2BF16PS | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4BSSD | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4BSUD | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4BUSD | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4BUUD | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXBF8PS | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXBHF8PS | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXBSSPS | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXHBF8PS | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXHF8PS | evex | 512 | Intel | ACE_1 | cannot: CPUID.7H.1:ECX[11] (ACE, AI Compute Extensions tile instructions) = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ADCX | legacy | - | Intel | ADOX_ADCX | runs | ✅ | hardware sweep (4 forms) |
| ADOX | legacy | - | Intel | ADOX_ADCX | runs | ✅ | hardware sweep (4 forms) |
| AESDEC | legacy | 128 | Intel | AES | runs | ✅ | hardware sweep (2 forms) |
| AESDECLAST | legacy | 128 | Intel | AES | runs | ✅ | hardware sweep (2 forms) |
| AESENC | legacy | 128 | Intel | AES | runs | ✅ | hardware sweep (2 forms) |
| AESENCLAST | legacy | 128 | Intel | AES | runs | ✅ | hardware sweep (2 forms) |
| AESIMC | legacy | 128 | Intel | AES | runs | ✅ | hardware sweep (2 forms) |
| AESKEYGENASSIST | legacy | 128 | Intel | AES | runs | ✅ | hardware sweep (2 forms) |
| LZCNT | legacy | - | both | AMD | runs | ✅ | hardware sweep (4 forms) |
| SYSCALL | legacy | - | both | AMD | runs | ✅ | SDM vectors: U1823 SYSCALL (0F 05) from 64-bit CPL3 to CPL0 (natively a real Windows kernel entry: emu-alltest sweep "not native-safe", U850) … |
| VADDPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VADDPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VADDSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VADDSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VADDSUBPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VADDSUBPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VANDNPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VANDNPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VANDPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VANDPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VBLENDPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VBLENDPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VBLENDVPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VBLENDVPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VBROADCASTF128 | vex | 256 | Intel | AVX | runs | ✅ | hardware sweep (1 forms) |
| VBROADCASTSD | vex | 256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VBROADCASTSS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCMPEQ_OSPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPEQ_OSPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPEQ_OSSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPEQ_OSSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPEQ_UQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPEQ_UQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPEQ_UQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPEQ_UQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPEQ_USPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPEQ_USPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPEQ_USSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPEQ_USSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPEQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPEQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPEQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPEQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPFALSE_OSPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPFALSE_OSPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPFALSE_OSSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPFALSE_OSSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPFALSEPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPFALSEPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPFALSESD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPFALSESS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPGE_OQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPGE_OQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPGE_OQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPGE_OQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPGEPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPGEPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPGESD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPGESS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPGT_OQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPGT_OQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPGT_OQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPGT_OQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPGTPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPGTPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPGTSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPGTSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPLE_OQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPLE_OQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPLE_OQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPLE_OQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPLEPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPLEPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPLESD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPLESS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPLT_OQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPLT_OQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPLT_OQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPLT_OQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPLTPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPLTPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPLTSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPLTSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNEQ_OQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNEQ_OQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNEQ_OQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNEQ_OQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNEQ_OSPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNEQ_OSPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNEQ_OSSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNEQ_OSSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNEQ_USPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNEQ_USPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNEQ_USSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNEQ_USSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNEQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNEQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNEQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNEQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNGE_UQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNGE_UQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNGE_UQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNGE_UQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNGEPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNGEPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNGESD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNGESS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNGT_UQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNGT_UQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNGT_UQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNGT_UQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNGTPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNGTPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNGTSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNGTSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNLE_UQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNLE_UQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNLE_UQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNLE_UQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNLEPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNLEPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNLESD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNLESS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNLT_UQPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNLT_UQPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNLT_UQSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNLT_UQSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPNLTPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPNLTPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPNLTSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPNLTSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPORD_SPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPORD_SPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPORD_SSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPORD_SSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPORDPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPORDPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPORDSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPORDSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCMPPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCMPSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VCMPSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VCMPTRUE_USPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPTRUE_USPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPTRUE_USSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPTRUE_USSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPTRUEPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPTRUEPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPTRUESD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPTRUESS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPUNORD_SPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPUNORD_SPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPUNORD_SSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPUNORD_SSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCMPUNORDPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPD imm8 predicate alias |
| VCMPUNORDPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) — VCMPPS imm8 predicate alias |
| VCMPUNORDSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSD imm8 predicate alias |
| VCMPUNORDSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) — VCMPSS imm8 predicate alias |
| VCOMISD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VCOMISS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VCVTDQ2PD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTDQ2PS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTPD2DQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTPD2PS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTPS2DQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTPS2PD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTSD2SI | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTSD2SS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VCVTSI2SD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTSI2SS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTSS2SD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VCVTSS2SI | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTTPD2DQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTTPS2DQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTTSD2SI | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VCVTTSS2SI | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VDIVPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VDIVPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VDIVSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VDIVSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VDPPD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VDPPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VEXTRACTF128 | vex | 256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VEXTRACTPS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VHADDPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VHADDPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VHSUBPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VHSUBPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VINSERTF128 | vex | 256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VINSERTPS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VLDDQU | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VLDMXCSR | vex | - | Intel | AVX | runs | ✅ | hardware sweep (1 forms) |
| VMASKMOVDQU | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (1 forms) |
| VMASKMOVPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMASKMOVPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMAXPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMAXPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMAXSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMAXSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMINPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMINPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMINSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMINSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVAPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VMOVAPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VMOVD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMOVDDUP | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMOVDQA | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VMOVDQU | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VMOVHLPS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (1 forms) |
| VMOVHPD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVHPS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVLHPS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (1 forms) |
| VMOVLPD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVLPS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVMSKPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVMSKPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVNTDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVNTDQA | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVNTPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVNTPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMOVQ | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (5 forms) |
| VMOVSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (3 forms) |
| VMOVSHDUP | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMOVSLDUP | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMOVSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (3 forms) |
| VMOVUPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VMOVUPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VMPSADBW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMULPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMULPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VMULSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VMULSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VORPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VORPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPABSB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPABSD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPABSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPACKSSDW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPACKSSWB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPACKUSDW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPACKUSWB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPADDB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPADDD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPADDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPADDSB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPADDSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPADDUSB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPADDUSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPADDW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPALIGNR | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPAND | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPANDN | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPAVGB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPAVGW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPBLENDVB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPBLENDW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCLMULQDQ | vex | 128/256 | Intel | AVX | runs | ✅ | U69 (VEX.128/256); EVEX U574 |
| VPCMPEQB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCMPEQD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCMPEQQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCMPEQW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCMPESTRI | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPCMPESTRM | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPCMPGTB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCMPGTD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCMPGTQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCMPGTW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPCMPISTRI | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPCMPISTRM | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPERM2F128 | vex | 256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPERMILPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (8 forms) |
| VPERMILPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (8 forms) |
| VPEXTRB | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPEXTRD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPEXTRQ | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPEXTRW | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPHADDD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPHADDSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPHADDW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPHMINPOSUW | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPHSUBD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPHSUBSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPHSUBW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPINSRB | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPINSRD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPINSRQ | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPINSRW | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPMADDUBSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMADDWD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMAXSB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMAXSD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMAXSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMAXUB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMAXUD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMAXUW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMINSB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMINSD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMINSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMINUB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMINUD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMINUW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVMSKB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPMOVSXBD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVSXBQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVSXBW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVSXDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVSXWD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVSXWQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVZXBD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVZXBQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVZXBW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVZXDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVZXWD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMOVZXWQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMULDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMULHRSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMULHUW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMULHW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMULLD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMULLW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPMULUDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPOR | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSADBW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSHUFB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSHUFD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSHUFHW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSHUFLW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSIGNB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSIGND | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSIGNW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSLLD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VPSLLDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPSLLQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VPSLLW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VPSRAD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VPSRAW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VPSRLD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VPSRLDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VPSRLQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VPSRLW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (6 forms) |
| VPSUBB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSUBD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSUBQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSUBSB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSUBSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSUBUSB | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSUBUSW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPSUBW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPTEST | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPUNPCKHBW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPUNPCKHDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPUNPCKHQDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPUNPCKHWD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPUNPCKLBW | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPUNPCKLDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPUNPCKLQDQ | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPUNPCKLWD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VPXOR | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VRCPPS | vex | 128/256 | Intel | AVX | runs | ✅ | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| VRCPSS | vex | 128 | Intel | AVX | runs | ✅ | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| VROUNDPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VROUNDPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VROUNDSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VROUNDSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VRSQRTPS | vex | 128/256 | Intel | AVX | runs | ✅ | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| VRSQRTSS | vex | 128 | Intel | AVX | runs | ✅ | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| VSHUFPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VSHUFPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VSQRTPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VSQRTPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VSQRTSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VSQRTSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VSTMXCSR | vex | - | Intel | AVX | runs | ✅ | hardware sweep (1 forms) |
| VSUBPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VSUBPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VSUBSD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VSUBSS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VTESTPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VTESTPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VUCOMISD | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VUCOMISS | vex | 128 | Intel | AVX | runs | ✅ | hardware sweep (2 forms) |
| VUNPCKHPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VUNPCKHPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VUNPCKLPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VUNPCKLPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VXORPD | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VXORPS | vex | 128/256 | Intel | AVX | runs | ✅ | hardware sweep (4 forms) |
| VZEROALL | vex | - | Intel | AVX | runs | ✅ | hardware sweep (1 forms) |
| VZEROUPPER | vex | - | Intel | AVX | runs | ✅ | hardware sweep (1 forms) |
| VBROADCASTI128 | vex | 256 | Intel | AVX2 | runs | ✅ | hardware sweep (1 forms) |
| VEXTRACTI128 | vex | 256 | Intel | AVX2 | runs | ✅ | hardware sweep (2 forms) |
| VINSERTI128 | vex | 256 | Intel | AVX2 | runs | ✅ | hardware sweep (2 forms) |
| VPBLENDD | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPBROADCASTB | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPBROADCASTD | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPBROADCASTQ | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPBROADCASTW | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPERM2I128 | vex | 256 | Intel | AVX2 | runs | ✅ | hardware sweep (2 forms) |
| VPERMD | vex | 256 | Intel | AVX2 | runs | ✅ | hardware sweep (2 forms) |
| VPERMPD | vex | 256 | Intel | AVX2 | runs | ✅ | hardware sweep (2 forms) |
| VPERMPS | vex | 256 | Intel | AVX2 | runs | ✅ | hardware sweep (2 forms) |
| VPERMQ | vex | 256 | Intel | AVX2 | runs | ✅ | hardware sweep (2 forms) |
| VPMASKMOVD | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPMASKMOVQ | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPSLLVD | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPSLLVQ | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPSRAVD | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPSRLVD | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VPSRLVQ | vex | 128/256 | Intel | AVX2 | runs | ✅ | hardware sweep (4 forms) |
| VGATHERDPD | vex | 128/256 | Intel | AVX2GATHER | runs | ✅ | hardware (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| VGATHERDPS | vex | 128/256 | Intel | AVX2GATHER | runs | ✅ | hardware (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| VGATHERQPD | vex | 128/256 | Intel | AVX2GATHER | runs | ✅ | hardware (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| VGATHERQPS | vex | 128/256 | Intel | AVX2GATHER | runs | ✅ | hardware (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| VPGATHERDD | vex | 128/256 | Intel | AVX2GATHER | runs | ✅ | hardware (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| VPGATHERDQ | vex | 128/256 | Intel | AVX2GATHER | runs | ✅ | hardware (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| VPGATHERQD | vex | 128/256 | Intel | AVX2GATHER | runs | ✅ | hardware (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| VPGATHERQQ | vex | 128/256 | Intel | AVX2GATHER | runs | ✅ | hardware (cases_reach): VEX 128/256, D/Q index x D/Q data, full and partial masks, mid-gather #PF, index/mask = dest #UD |
| VAESDEC | vex | 128/256 | Intel | AVXAES | runs | ✅ | hardware sweep (4 forms) |
| VAESDECLAST | vex | 128/256 | Intel | AVXAES | runs | ✅ | hardware sweep (4 forms) |
| VAESENC | vex | 128/256 | Intel | AVXAES | runs | ✅ | hardware sweep (4 forms) |
| VAESENCLAST | vex | 128/256 | Intel | AVXAES | runs | ✅ | hardware sweep (4 forms) |
| VAESIMC | vex | 128 | Intel | AVXAES | runs | ✅ | hardware sweep (2 forms) |
| VAESKEYGENASSIST | vex | 128 | Intel | AVXAES | runs | ✅ | hardware sweep (2 forms) |
| VGF2P8AFFINEINVQB | vex | 128/256 | Intel | AVX_GFNI | runs | ✅ | U70 (VEX); EVEX U572 |
| VGF2P8AFFINEQB | vex | 128/256 | Intel | AVX_GFNI | runs | ✅ | U70 (VEX); EVEX U572 |
| VGF2P8MULB | vex | 128/256 | Intel | AVX_GFNI | runs | ✅ | U70 (VEX); EVEX U572 |
| VPMADD52HUQ | vex | 128/256 | Intel | AVX_IFMA | cannot: CPUID.7H.1:EAX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U87 AVX-IFMA (VEX): independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPMADD52LUQ | vex | 128/256 | Intel | AVX_IFMA | cannot: CPUID.7H.1:EAX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U87 AVX-IFMA (VEX): independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VBCSTNEBF162PS | vex | 128/256 | Intel | AVX_NE_CONVERT | cannot: CPUID.7H.1:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U88 AVX-NE-CONVERT: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VBCSTNESH2PS | vex | 128/256 | Intel | AVX_NE_CONVERT | cannot: CPUID.7H.1:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U88 AVX-NE-CONVERT: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VCVTNEEBF162PS | vex | 128/256 | Intel | AVX_NE_CONVERT | cannot: CPUID.7H.1:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U88 AVX-NE-CONVERT: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VCVTNEEPH2PS | vex | 128/256 | Intel | AVX_NE_CONVERT | cannot: CPUID.7H.1:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U88 AVX-NE-CONVERT: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VCVTNEOBF162PS | vex | 128/256 | Intel | AVX_NE_CONVERT | cannot: CPUID.7H.1:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U88 AVX-NE-CONVERT: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VCVTNEOPH2PS | vex | 128/256 | Intel | AVX_NE_CONVERT | cannot: CPUID.7H.1:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U88 AVX-NE-CONVERT: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VCVTNEPS2BF16 | vex | 128/256 | Intel | AVX_NE_CONVERT | cannot: CPUID.7H.1:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U88 AVX-NE-CONVERT: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPBUSD | vex | 128/256 | Intel | AVX_VNNI | runs | ✅ | U71 AVX-VNNI (VEX) |
| VPDPBUSDS | vex | 128/256 | Intel | AVX_VNNI | runs | ✅ | U71 |
| VPDPWSSD | vex | 128/256 | Intel | AVX_VNNI | runs | ✅ | U71 |
| VPDPWSSDS | vex | 128/256 | Intel | AVX_VNNI | runs | ✅ | U71 |
| VPDPWSUD | vex | 128/256 | Intel | AVX_VNNI_INT16 | cannot: CPUID.7H.1:EDX[10] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U86 AVX-VNNI-INT16: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPWSUDS | vex | 128/256 | Intel | AVX_VNNI_INT16 | cannot: CPUID.7H.1:EDX[10] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U86 AVX-VNNI-INT16: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPWUSD | vex | 128/256 | Intel | AVX_VNNI_INT16 | cannot: CPUID.7H.1:EDX[10] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U86 AVX-VNNI-INT16: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPWUSDS | vex | 128/256 | Intel | AVX_VNNI_INT16 | cannot: CPUID.7H.1:EDX[10] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U86 AVX-VNNI-INT16: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPWUUD | vex | 128/256 | Intel | AVX_VNNI_INT16 | cannot: CPUID.7H.1:EDX[10] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U86 AVX-VNNI-INT16: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPWUUDS | vex | 128/256 | Intel | AVX_VNNI_INT16 | cannot: CPUID.7H.1:EDX[10] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U86 AVX-VNNI-INT16: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPBSSD | vex | 128/256 | Intel | AVX_VNNI_INT8 | cannot: CPUID.7H.1:EDX[4] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U85 AVX-VNNI-INT8: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPBSSDS | vex | 128/256 | Intel | AVX_VNNI_INT8 | cannot: CPUID.7H.1:EDX[4] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U85 AVX-VNNI-INT8: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPBSUD | vex | 128/256 | Intel | AVX_VNNI_INT8 | cannot: CPUID.7H.1:EDX[4] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U85 AVX-VNNI-INT8: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPBSUDS | vex | 128/256 | Intel | AVX_VNNI_INT8 | cannot: CPUID.7H.1:EDX[4] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U85 AVX-VNNI-INT8: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPBUUD | vex | 128/256 | Intel | AVX_VNNI_INT8 | cannot: CPUID.7H.1:EDX[4] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U85 AVX-VNNI-INT8: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| VPDPBUUDS | vex | 128/256 | Intel | AVX_VNNI_INT8 | cannot: CPUID.7H.1:EDX[4] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U85 AVX-VNNI-INT8: independent SDM-pseudocode model (ref_vnni_ifma_ne.py) + unit tests + cases_vnni_ifma.txt "=>"; strict i5-13600K … |
| ANDN | vex | - | Intel | BMI1 | runs | ✅ | hardware sweep (4 forms) |
| BEXTR | vex | - | Intel | BMI1 | runs | ✅ | hardware sweep (4 forms) |
| BLSI | vex | - | Intel | BMI1 | runs | ✅ | hardware sweep (4 forms) |
| BLSMSK | vex | - | Intel | BMI1 | runs | ✅ | hardware sweep (4 forms) |
| BLSR | vex | - | Intel | BMI1 | runs | ✅ | hardware sweep (4 forms) |
| TZCNT | legacy | - | Intel | BMI1 | runs | ✅ | hardware sweep (4 forms) |
| BZHI | vex | - | Intel | BMI2 | runs | ✅ | hardware sweep (4 forms) |
| MULX | vex | - | Intel | BMI2 | runs | ✅ | hardware sweep (4 forms) |
| PDEP | vex | - | Intel | BMI2 | runs | ✅ | hardware sweep (4 forms) |
| PEXT | vex | - | Intel | BMI2 | runs | ✅ | hardware sweep (4 forms) |
| RORX | vex | - | Intel | BMI2 | runs | ✅ | hardware sweep (4 forms) |
| SARX | vex | - | Intel | BMI2 | runs | ✅ | hardware sweep (4 forms) |
| SHLX | vex | - | Intel | BMI2 | runs | ✅ | hardware sweep (4 forms) |
| SHRX | vex | - | Intel | BMI2 | runs | ✅ | hardware sweep (4 forms) |
| ENDBR32 | legacy | - | Intel | CET | runs | ✅ | hardware sweep (1 forms) |
| ENDBR64 | legacy | - | Intel | CET | runs | ✅ | hardware sweep (1 forms) |
| INCSSPD | legacy | - | Intel | CET | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ✅ per manual | SDM vectors: U114 CET shadow stack: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| INCSSPQ | legacy | - | Intel | CET | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ✅ per manual | SDM vectors: U114 CET shadow stack: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| RDSSPD | legacy | - | Intel | CET | runs | ✅ | hardware sweep (1 forms) |
| RDSSPQ | legacy | - | Intel | CET | runs | ✅ | hardware sweep (1 forms) |
| RSTORSSP | legacy | - | Intel | CET | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ✅ per manual | SDM vectors: U114 CET shadow stack: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| SAVEPREVSSP | legacy | - | Intel | CET | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ✅ per manual | SDM vectors: U114 CET shadow stack: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| WRSSD | legacy | - | Intel | CET | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ✅ per manual | SDM vectors: U114 CET shadow stack: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| WRSSQ | legacy | - | Intel | CET | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ✅ per manual | SDM vectors: U114 CET shadow stack: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| CLDEMOTE | legacy | - | Intel | CLDEMOTE | cannot: CPUID.7H:ECX[25] = 0 on --host and --guest | ❌ · ✅ per manual | hardware sweep (1 forms) |
| CLFLUSHOPT | legacy | - | Intel | CLFLUSHOPT | runs | ✅ | hardware sweep (1 forms) |
| CLFLUSH | legacy | - | Intel | CLFSH | runs | ✅ | hardware sweep (1 forms) |
| CLWB | legacy | - | Intel | CLWB | runs | ✅ | hardware sweep (1 forms) |
| CLZERO | legacy | - | AMD | CLZERO | cannot: AMD CLZERO, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD CLZERO |
| CMOVA | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVAE | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVB | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVBE | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVE | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVG | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVGE | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVL | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVLE | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVNE | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVNO | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVNP | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVNS | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVO | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVP | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMOVS | legacy | - | Intel | CMOV | runs | ✅ | hardware sweep (6 forms) |
| CMPAEXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPAXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPBEXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPBXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPEXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPGEXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPGXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPLEXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPLXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNBEXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNBXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNEXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNLEXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNLXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNOXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNPXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNSXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPNZXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPOXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPPXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPSXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPZXADD | vex | - | Intel | CMPCCXADD | cannot: CPUID.7H.1:EAX[7] = 0 on --host and --guest | ❌ · ⏳ open item | implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5) |
| CMPXCHG16B | legacy | - | Intel | CMPXCHG16B | runs | ✅ | hardware sweep (1 forms) |
| ENQCMD | legacy | - | Intel | ENQCMD | cannot: CPUID.7H:ECX[29] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U112 ENQCMD: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU; SDE-validated (Intel … |
| VCVTPH2PS | vex | 128/256 | Intel | F16C | runs | ✅ | hardware sweep (4 forms) |
| VCVTPS2PH | vex | 128/256 | Intel | F16C | runs | ✅ | hardware sweep (4 forms) |
| NOP | legacy | - | Intel | FAT_NOP | runs | ✅ | hardware sweep (10 forms) |
| NOP3 | legacy | - | Intel | FAT_NOP | runs | ✅ | hardware sweep (10 forms) — same opcode as NOP (Capstone name) |
| NOP4 | legacy | - | Intel | FAT_NOP | runs | ✅ | hardware sweep (10 forms) — same opcode as NOP (Capstone name) |
| NOP5 | legacy | - | Intel | FAT_NOP | runs | ✅ | hardware sweep (10 forms) — same opcode as NOP (Capstone name) |
| NOP6 | legacy | - | Intel | FAT_NOP | runs | ✅ | hardware sweep (10 forms) — same opcode as NOP (Capstone name) |
| NOP7 | legacy | - | Intel | FAT_NOP | runs | ✅ | hardware sweep (10 forms) — same opcode as NOP (Capstone name) |
| NOP8 | legacy | - | Intel | FAT_NOP | runs | ✅ | hardware sweep (10 forms) — same opcode as NOP (Capstone name) |
| NOP9 | legacy | - | Intel | FAT_NOP | runs | ✅ | hardware sweep (10 forms) — same opcode as NOP (Capstone name) |
| FCMOVB | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) |
| FCMOVBE | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) |
| FCMOVE | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) |
| FCMOVNB | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) |
| FCMOVNBE | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) |
| FCMOVNE | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) |
| FCMOVNP | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) — same opcode as FCMOVNU (Capstone name) |
| FCMOVNU | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) |
| FCMOVU | legacy | - | Intel | FCMOV | runs | ✅ | hardware sweep (1 forms) |
| FCOMI | legacy | - | Intel | FCOMI | runs | ✅ | SDM C1 = 0 (U531; the CPU keeps C1: docs/quirks.md) |
| FCOMIP | legacy | - | Intel | FCOMI | runs | ✅ | SDM C1 = 0 (U531, docs/quirks.md) |
| FCOMPI | legacy | - | Intel | FCOMI | runs | ✅ | hardware sweep (1 forms) |
| FUCOMI | legacy | - | Intel | FCOMI | runs | ✅ | SDM C1 = 0 (U531, docs/quirks.md) |
| FUCOMIP | legacy | - | Intel | FCOMI | runs | ✅ | SDM C1 = 0 (U531, docs/quirks.md) |
| FUCOMPI | legacy | - | Intel | FCOMI | runs | ✅ | hardware sweep (1 forms) |
| VFMADD132PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADD132PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADD132SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMADD132SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMADD213PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADD213PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADD213SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMADD213SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMADD231PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADD231PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADD231SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMADD231SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMADDSUB132PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADDSUB132PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADDSUB213PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADDSUB213PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADDSUB231PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMADDSUB231PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUB132PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUB132PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUB132SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMSUB132SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMSUB213PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUB213PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUB213SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMSUB213SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMSUB231PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUB231PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUB231SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMSUB231SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMSUBADD132PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUBADD132PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUBADD213PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUBADD213PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUBADD231PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFMSUBADD231PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMADD132PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMADD132PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMADD132SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMADD132SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMADD213PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMADD213PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMADD213SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMADD213SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMADD231PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMADD231PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMADD231SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMADD231SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMSUB132PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMSUB132PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMSUB132SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMSUB132SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMSUB213PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMSUB213PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMSUB213SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMSUB213SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMSUB231PD | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMSUB231PS | vex | 128/256 | Intel | FMA | runs | ✅ | hardware sweep (4 forms) |
| VFNMSUB231SD | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFNMSUB231SS | vex | 128 | Intel | FMA | runs | ✅ | hardware sweep (2 forms) |
| VFMADDPD | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMADDPS | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMADDSD | vex | 128 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMADDSS | vex | 128 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMADDSUBPD | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMADDSUBPS | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMSUBADDPD | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMSUBADDPS | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMSUBPD | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMSUBPS | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMSUBSD | vex | 128 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFMSUBSS | vex | 128 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFNMADDPD | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFNMADDPS | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFNMADDSD | vex | 128 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFNMADDSS | vex | 128 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFNMSUBPD | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFNMSUBPS | vex | 128/256 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFNMSUBSD | vex | 128 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| VFNMSUBSS | vex | 128 | AMD | FMA4 | cannot: AMD FMA4, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD FMA4 |
| FXRSTOR | legacy | - | Intel | FXSAVE | runs | ✅ | U64 |
| FXSAVE | legacy | - | Intel | FXSAVE | runs | ✅ | U64 (FOP/FIP/FDP, REX.W layout) |
| FXRSTOR64 | legacy | - | Intel | FXSAVE64 | runs | ✅ | U64 |
| FXSAVE64 | legacy | - | Intel | FXSAVE64 | runs | ✅ | U64 |
| GF2P8AFFINEINVQB | legacy | 128 | Intel | GFNI | runs | ✅ | U70 |
| GF2P8AFFINEQB | legacy | 128 | Intel | GFNI | runs | ✅ | U70 |
| GF2P8MULB | legacy | 128 | Intel | GFNI | runs | ✅ | U70, identical to the CPU |
| XACQUIRE | legacy | - | Intel | HLE | cannot: CPUID.7H:EBX[4] = 0 on --host and --guest | ❌ · ✅ per manual | hardware (cases_reach): F2 on LOCK/XCHG: ignored, the CPU lacks HLE (locked op runs) |
| XRELEASE | legacy | - | Intel | HLE | cannot: CPUID.7H:EBX[4] = 0 on --host and --guest | ❌ · ✅ per manual | hardware (cases_reach): F3 on LOCK/XCHG/MOV m: ignored, the CPU lacks HLE |
| BOUND | legacy | - | Intel | I186 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| ENTER | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (2 forms) |
| IMUL | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (20 forms) |
| LEAVE | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (1 forms) |
| POPAW | legacy | - | Intel | I186 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| PUSH | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (7 forms) |
| PUSHAW | legacy | - | Intel | I186 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| RCL | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (23 forms) |
| RCR | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (22 forms) |
| ROL | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (22 forms) |
| ROR | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (22 forms) |
| SAL | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (22 forms) |
| SAR | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (22 forms) |
| SHL | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (22 forms) |
| SHR | legacy | - | Intel | I186 | runs | ✅ | hardware sweep (22 forms) |
| ARPL | legacy | - | Intel | I286PROTECTED | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| LAR | legacy | - | Intel | I286PROTECTED | runs | ✅ | hardware sweep (6 forms) |
| LSL | legacy | - | Intel | I286PROTECTED | runs | ✅ | hardware sweep (6 forms) |
| SLDT | legacy | - | Intel | I286PROTECTED | runs | ⏳ open item | values: Phase 3 environment |
| STR | legacy | - | Intel | I286PROTECTED | runs | ⏳ open item | values: Phase 3 environment |
| VERR | legacy | - | Intel | I286PROTECTED | runs | ✅ | hardware sweep (2 forms) |
| VERW | legacy | - | Intel | I286PROTECTED | runs | ✅ | hardware sweep (2 forms) |
| SGDT | legacy | - | Intel | I286REAL | runs | ⏳ open item | values: Phase 3 environment |
| SIDT | legacy | - | Intel | I286REAL | runs | ⏳ open item | values: Phase 3 environment |
| SMSW | legacy | - | Intel | I286REAL | runs | ⏳ open item | CR0 value: Phase 3 environment |
| BSF | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (8 forms) |
| BSR | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (8 forms) |
| BT | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (12 forms) |
| BTC | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (12 forms) |
| BTR | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (12 forms) |
| BTS | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (12 forms) |
| CDQ | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (1 forms) |
| CMPSD | legacy | 128 | Intel | I386 | runs | ✅ | hardware sweep (5 forms) |
| CWDE | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (1 forms) |
| IRETD | legacy | - | Intel | I386 | runs | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| JCXZ | legacy | - | Intel | I386 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| JECXZ | legacy | - | Intel | I386 | runs | ✅ | hardware (cases_reach): 67 E3 rel8 (ECX = 0 with RCX[63:32] != 0 taken) |
| LFS | legacy | - | Intel | I386 | runs | ✅ | SDM vectors: natively it would replace the host thread's FS selector and base (emu-alltest sweep: not native-safe, U850); independent SDM model … |
| LGS | legacy | - | Intel | I386 | runs | ✅ | SDM vectors: natively it would replace the host thread's GS selector and base (the TEB) (emu-alltest sweep: not native-safe, U850); independent SDM … |
| LODSD | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (3 forms) |
| LSS | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (3 forms) |
| MOVSD | legacy | 128 | Intel | I386 | runs | ✅ | hardware sweep (6 forms) |
| MOVSX | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (12 forms) |
| MOVZX | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (12 forms) |
| POPAL | legacy | - | Intel | I386 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| POPFD | legacy | - | Intel | I386 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| POPFL | legacy | - | Intel | I386 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| PUSHAL | legacy | - | Intel | I386 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| PUSHFD | legacy | - | Intel | I386 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| PUSHFL | legacy | - | Intel | I386 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| SCASD | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (3 forms) |
| SETA | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETAE | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETB | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETBE | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETE | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETG | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETGE | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETL | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETLE | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETNE | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETNO | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETNP | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETNS | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETO | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETP | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SETS | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (2 forms) |
| SHLD | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (12 forms) |
| SHRD | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (12 forms) |
| STOSD | legacy | - | Intel | I386 | runs | ✅ | hardware sweep (3 forms) |
| BSWAP | legacy | - | Intel | I486REAL | runs | ✅ | hardware sweep (3 forms) |
| CMPXCHG | legacy | - | Intel | I486REAL | runs | ✅ | hardware sweep (8 forms) |
| CPUID | legacy | - | Intel | I486REAL | runs | ✅ | U68 i5-13600K profile + UC_CTL_X86_CPUID(_STRICT); only per-core APIC IDs vary |
| XADD | legacy | - | Intel | I486REAL | runs | ✅ | hardware sweep (8 forms) |
| AAA | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| AAD | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| AAM | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| AAS | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| ADC | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| ADD | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| AND | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| CALL | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| CBW | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| CLC | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| CLD | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| CMC | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| CMP | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| CMPSB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (3 forms) |
| CMPSW | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| CWD | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| DAA | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| DAS | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| DEC | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| DIV | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| IDIV | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| INC | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| INT | legacy | - | Intel | I86 | runs | ✅ | SDM vectors: U1823 INT n (CD ib) from 64-bit CPL3 to CPL0 through the guest IDT (natively Windows' IDT decides: emu-alltest sweep "not native-safe" … |
| INT1 | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| INT3 | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| INTO | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| IRET | legacy | - | Intel | I86 | runs | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| JA | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JAE | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JBE | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JE | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JG | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JGE | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JL | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JLE | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JMP | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| JNE | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JNO | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JNP | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JNS | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JO | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JP | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| JS | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| LCALL | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| LDS | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| LEA | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (3 forms) |
| LES | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| LJMP | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| LODSB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (3 forms) |
| LODSW | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| LOOP | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (2 forms) |
| LOOPE | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (2 forms) |
| LOOPNE | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (2 forms) |
| MOV | legacy | - | Intel | I86 | runs | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| MOVABS | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (9 forms) |
| MOVSB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (3 forms) |
| MOVSW | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| MUL | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| NEG | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| NOP2 | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (10 forms) — same opcode as NOP (Capstone name) |
| NOT | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (8 forms) |
| OR | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| POP | legacy | - | Intel | I86 | runs | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| POPF | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| PUSHF | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| RET | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (9 forms) |
| RETF | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (5 forms) |
| RETFQ | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (4 forms) |
| SALC | legacy | - | Intel | I86 | runs | ✅ | hardware sweep: #UD in 64-bit mode; 16/32-bit modes out of scope (x64 sample) |
| SBB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| SCASB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (3 forms) |
| SCASW | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| STC | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| STD | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| STOSB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (3 forms) |
| STOSW | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| SUB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| TEST | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (16 forms) |
| UDB | legacy | - | Intel | I86 | runs | ✅ | hardware (cases_reach): D6 (UDB): #UD in 64-bit mode |
| XCHG | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| XLATB | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (1 forms) |
| XOR | legacy | - | Intel | I86 | runs | ✅ | hardware sweep (20 forms) |
| IBHF | legacy | - | Intel | IBHF | cannot: not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | hardware (cases_reach): F3 [REX.W] 0F 1E F8: hint NOP |
| PREFETCHIT0 | legacy | - | Intel | ICACHE_PREFETCH | cannot: CPUID.7H.1:EDX[14] = 0 on --host and --guest | ❌ · ✅ per manual | hardware (cases_reach): 0F 18 /7: NOP without PREFETCHI (RIP-relative and other memory forms) |
| PREFETCHIT1 | legacy | - | Intel | ICACHE_PREFETCH | cannot: CPUID.7H.1:EDX[14] = 0 on --host and --guest | ❌ · ✅ per manual | hardware (cases_reach): 0F 18 /6: NOP without PREFETCHI |
| AESDEC128KL | legacy | 128 | Intel | KEYLOCKER | cannot: --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| AESDEC256KL | legacy | 128 | Intel | KEYLOCKER | cannot: --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| AESENC128KL | legacy | 128 | Intel | KEYLOCKER | cannot: --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| AESENC256KL | legacy | 128 | Intel | KEYLOCKER | cannot: --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| ENCODEKEY128 | legacy | - | Intel | KEYLOCKER | cannot: --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| ENCODEKEY256 | legacy | - | Intel | KEYLOCKER | cannot: --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| AESDECWIDE128KL | legacy | - | Intel | KEYLOCKER_WIDE | cannot: --guest: CPUID.19H:EBX[2] = 0; --host: enumerated (CPUID.19H:EBX[2] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| AESDECWIDE256KL | legacy | - | Intel | KEYLOCKER_WIDE | cannot: --guest: CPUID.19H:EBX[2] = 0; --host: enumerated (CPUID.19H:EBX[2] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| AESENCWIDE128KL | legacy | - | Intel | KEYLOCKER_WIDE | cannot: --guest: CPUID.19H:EBX[2] = 0; --host: enumerated (CPUID.19H:EBX[2] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| AESENCWIDE256KL | legacy | - | Intel | KEYLOCKER_WIDE | cannot: --guest: CPUID.19H:EBX[2] = 0; --host: enumerated (CPUID.19H:EBX[2] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>" … |
| LAHF | legacy | - | Intel | LAHF | runs | ✅ | hardware sweep (1 forms) |
| SAHF | legacy | - | Intel | LAHF | runs | ✅ | hardware sweep (1 forms) |
| CDQE | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (1 forms) |
| CMPSQ | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (3 forms) |
| CQO | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (1 forms) |
| IRETQ | legacy | - | Intel | LONGMODE | runs | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| JRCXZ | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (4 forms) |
| LODSQ | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (3 forms) |
| MOVSQ | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (3 forms) |
| MOVSXD | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (6 forms) |
| POPFQ | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (1 forms) |
| PUSHFQ | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (1 forms) |
| SCASQ | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (3 forms) |
| STOSQ | legacy | - | Intel | LONGMODE | runs | ✅ | hardware sweep (3 forms) |
| LLWPCB | xop | - | AMD | LWP | cannot: AMD LWP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD LWP |
| LWPINS | xop | - | AMD | LWP | cannot: AMD LWP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD LWP |
| LWPVAL | xop | - | AMD | LWP | cannot: AMD LWP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD LWP |
| SLWPCB | xop | - | AMD | LWP | cannot: AMD LWP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD LWP |
| MCOMMIT | legacy | - | AMD | MCOMMIT | cannot: AMD MCOMMIT, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD MCOMMIT |
| MONITORX | legacy | - | AMD | MONITORX | cannot: AMD MONITORX, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD MONITORX |
| MWAITX | legacy | - | AMD | MONITORX | cannot: AMD MONITORX, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD MONITORX |
| MOVBE | legacy | - | Intel | MOVBE | runs | ✅ | hardware sweep (8 forms; 2 more encodings #UD on both) |
| MOVDIR64B | legacy | - | Intel | MOVDIR64B | runs | ✅ | U73, identical to the CPU |
| MOVDIRI | legacy | - | Intel | MOVDIRI | runs | ✅ | U72, identical to the CPU |
| MOVRS | legacy | - | Intel | MOVRS | cannot: CPUID.7H.1:EAX[31] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U102 MOVRS: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile #UD … |
| PREFETCHRST2 | legacy | - | Intel | MOVRS | cannot: CPUID.7H.1:EAX[31] = 0 on --host and --guest | ❌ · ✅ per manual | hardware (cases_reach): 0F 18 /4: NOP without MOVRS; SDE-validated (Intel SDE 10.13.1 -future, U1040 cross-check 2026-10-10): 2 cases agree |
| BNDCL | legacy | - | Intel | MPX | cannot: CPUID.7H:EBX[14] = 0 on --host and --guest | ❌ · ⏳ open item | hardware, absent-feature only; feature semantics open: U1094 MPX absent (CPUID.7H:EBX[14] = 0 on the i5-13600K and in the emulator under its … |
| BNDCN | legacy | - | Intel | MPX | cannot: CPUID.7H:EBX[14] = 0 on --host and --guest | ❌ · ⏳ open item | hardware, absent-feature only; feature semantics open: U1094 MPX absent (CPUID.7H:EBX[14] = 0 on the i5-13600K and in the emulator under its … |
| BNDCU | legacy | - | Intel | MPX | cannot: CPUID.7H:EBX[14] = 0 on --host and --guest | ❌ · ⏳ open item | hardware, absent-feature only; feature semantics open: U1094 MPX absent (CPUID.7H:EBX[14] = 0 on the i5-13600K and in the emulator under its … |
| BNDLDX | legacy | - | Intel | MPX | cannot: CPUID.7H:EBX[14] = 0 on --host and --guest | ❌ · ⏳ open item | hardware, absent-feature only; feature semantics open: U1094 MPX absent (CPUID.7H:EBX[14] = 0 on the i5-13600K and in the emulator under its … |
| BNDMK | legacy | - | Intel | MPX | cannot: CPUID.7H:EBX[14] = 0 on --host and --guest | ❌ · ⏳ open item | hardware, absent-feature only; feature semantics open: U1094 MPX absent (CPUID.7H:EBX[14] = 0 on the i5-13600K and in the emulator under its … |
| BNDMOV | legacy | - | Intel | MPX | cannot: CPUID.7H:EBX[14] = 0 on --host and --guest | ❌ · ⏳ open item | hardware, absent-feature only; feature semantics open: U1094 MPX absent (CPUID.7H:EBX[14] = 0 on the i5-13600K and in the emulator under its … |
| BNDSTX | legacy | - | Intel | MPX | cannot: CPUID.7H:EBX[14] = 0 on --host and --guest | ❌ · ⏳ open item | hardware, absent-feature only; feature semantics open: U1094 MPX absent (CPUID.7H:EBX[14] = 0 on the i5-13600K and in the emulator under its … |
| PAUSE | legacy | - | Intel | PAUSE | runs | ✅ | hardware sweep (1 forms) |
| PCLMULQDQ | legacy | 128 | Intel | PCLMULQDQ | runs | ✅ | hardware sweep (2 forms) |
| EMMS | legacy | - | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (1 forms) |
| MASKMOVQ | legacy | 64 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (1 forms) |
| MOVD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (8 forms) |
| MOVNTQ | legacy | 64 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (1 forms) |
| MOVQ | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (10 forms) |
| PACKSSDW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PACKSSWB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PACKUSWB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PADDB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PADDD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PADDSB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PADDSW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PADDUSB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PADDUSW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PADDW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PAND | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PANDN | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PAVGB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PAVGW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PCMPEQB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PCMPEQD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PCMPEQW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PCMPGTB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PCMPGTD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PCMPGTW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PEXTRW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (3 forms) |
| PINSRW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PMADDWD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PMAXSW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PMAXUB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PMINSW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PMINUB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PMULHUW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PMULHW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PMULLW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| POR | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PSADBW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PSHUFW | legacy | 64 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (2 forms) |
| PSLLD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (6 forms) |
| PSLLQ | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (6 forms) |
| PSLLW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (6 forms) |
| PSRAD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (6 forms) |
| PSRAW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (6 forms) |
| PSRLD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (6 forms) |
| PSRLQ | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (6 forms) |
| PSRLW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (6 forms) |
| PSUBB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PSUBD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PSUBSB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PSUBSW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PSUBUSB | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PSUBUSW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PSUBW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PUNPCKHBW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PUNPCKHDQ | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PUNPCKHWD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PUNPCKLBW | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PUNPCKLDQ | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PUNPCKLWD | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| PXOR | legacy | 64/128 | Intel | PENTIUMMMX | runs | ✅ | hardware sweep (4 forms) |
| CMPXCHG8B | legacy | - | Intel | PENTIUMREAL | runs | ✅ | hardware sweep (1 forms) |
| RDTSC | legacy | - | Intel | PENTIUMREAL | runs | ⏳ open item | TSC determinism hook: Phase 3 |
| RDPKRU | legacy | - | Intel | PKU | cannot: the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U1690-U1692 PKU (U1821 status): Windows leaves CR4.PKE = 0 (OSPKE off), so the i5-13600K and the strict profile give #UD (hardware … |
| WRPKRU | legacy | - | Intel | PKU | cannot: the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U1690-U1692 PKU (U1821 status): Windows leaves CR4.PKE = 0 (OSPKE off), so the i5-13600K and the strict profile give #UD (hardware … |
| POPCNT | legacy | - | Intel | POPCNT | runs | ✅ | hardware sweep (4 forms) |
| UD1 | legacy | - | Intel | PPRO | runs | ✅ | hardware (cases_reach): 0F B9 /r: #UD (its defined behaviour) |
| UD2 | legacy | - | Intel | PPRO | runs | ✅ | hardware (cases_reach): 0F 0B: #UD (its defined behaviour) |
| UD0 | legacy | - | Intel | PPRO_UD0_LONG | runs | ✅ | hardware (cases_reach): 0F FF /r: #UD (its defined behaviour) |
| PREFETCHWT1 | legacy | - | Intel | PREFETCHWT1 | cannot: CPUID.7H:ECX[0] = 0 on --host and --guest | ❌ · ✅ per manual | hardware, absent-feature behaviour (NOP / hint): U1094 PREFETCHWT1 is a hint with no architectural effect (SDM 092 Vol2D 8-2: "merely a hint and … |
| PREFETCH | legacy | - | Intel | PREFETCH_NOP | runs | ✅ | hardware sweep (1 forms) |
| PREFETCHW | legacy | - | Intel | PREFETCH_NOP | runs | ✅ | hardware sweep (1 forms) |
| ADDR32 | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): 67 prefix (LEA eax, [esi]) |
| BND | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): F2 on branches: plain branch without MPX |
| DATA16 | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): 66 prefix |
| LOCK | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): F0 prefix (memory: locked; register: #UD); SDE-validated (Intel SDE 10.13.1 -future/-gnr/-spr, U1040 cross-check … |
| NOTRACK | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): 3E on indirect JMP/CALL: ignored (IBT not enabled) |
| REP | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): F3 prefix (string ops) |
| REPE | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): F3 prefix (CMPS/SCAS) |
| REPNE | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): F2 prefix (CMPS/SCAS) |
| REPNZ | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): F2 prefix (CMPS/SCAS) |
| REPZ | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): F3 prefix (CMPS/SCAS) |
| REX64 | legacy | - | Intel | PREFIX | runs | ✅ | hardware (cases_reach): REX.W prefix |
| PTWRITE | legacy | - | Intel | PTWRITE | runs | ✅ | U1710: executes where CPUID.(14H,0):EBX[4] = 1 (--host, as the bare host enumerates it; Intel PT tracing not modelled = no-op), SDM #UD where hidden … |
| AADD | legacy | - | Intel | RAO_INT | cannot: CPUID.7H.1:EAX[3] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U101 RAO-INT: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile … |
| AAND | legacy | - | Intel | RAO_INT | cannot: CPUID.7H.1:EAX[3] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U101 RAO-INT: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile … |
| AOR | legacy | - | Intel | RAO_INT | cannot: CPUID.7H.1:EAX[3] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U101 RAO-INT: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile … |
| AXOR | legacy | - | Intel | RAO_INT | cannot: CPUID.7H.1:EAX[3] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U101 RAO-INT: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile … |
| RDPID | legacy | - | Intel | RDPID | runs | ⏳ open item | TSC_AUX value: Phase 3 environment |
| RDPRU | legacy | - | AMD | RDPRU | cannot: AMD RDPRU, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD RDPRU |
| RDRAND | legacy | - | Intel | RDRAND | runs | ✅ | U65 host entropy (RtlGenRandom), CF/flags per SDM |
| RDSEED | legacy | - | Intel | RDSEED | runs | ✅ | U65 |
| RDTSCP | legacy | - | Intel | RDTSCP | runs | ⏳ open item | TSC/TSC_AUX: Phase 3 |
| RDFSBASE | legacy | - | Intel | RDWRFSGS | runs | ✅ | hardware sweep (2 forms) |
| RDGSBASE | legacy | - | Intel | RDWRFSGS | runs | ⏳ open item | value = TEB base: Phase 3 environment |
| WRFSBASE | legacy | - | Intel | RDWRFSGS | runs | ✅ | SDM vectors: natively it would replace the host thread's FS base (emu-alltest sweep: not native-safe, U850); independent SDM model ref_sweep_sdm.py … |
| WRGSBASE | legacy | - | Intel | RDWRFSGS | runs | ✅ | SDM vectors: natively it would replace the host thread's GS base (the TEB) (emu-alltest sweep: not native-safe, U850); independent SDM model … |
| XABORT | legacy | - | Intel | RTM | cannot: CPUID.7H:EBX[11] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U110 RTM always-abort model (every XBEGIN aborts at once: EAX = 0, RIP = the fallback; CPUID RTM_ALWAYS_ABORT reported; no … |
| XBEGIN | legacy | - | Intel | RTM | cannot: CPUID.7H:EBX[11] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U110 RTM always-abort model (every XBEGIN aborts at once: EAX = 0, RIP = the fallback; CPUID RTM_ALWAYS_ABORT reported; no … |
| XEND | legacy | - | Intel | RTM | cannot: CPUID.7H:EBX[11] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U110 RTM always-abort model (every XBEGIN aborts at once: EAX = 0, RIP = the fallback; CPUID RTM_ALWAYS_ABORT reported; no … |
| XTEST | legacy | - | Intel | RTM | cannot: CPUID.7H:EBX[11] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U110 RTM always-abort model (every XBEGIN aborts at once: EAX = 0, RIP = the fallback; CPUID RTM_ALWAYS_ABORT reported; no … |
| SYSENTER | legacy | - | Intel | SEP | runs | ✅ | SDM vectors: U1823 SYSENTER (0F 34) from 64-bit CPL3 to CPL0 (natively a real kernel entry: emu-alltest sweep "not native-safe", U850): independent … |
| SERIALIZE | legacy | - | Intel | SERIALIZE | runs | ✅ | U74, identical to the CPU |
| ENCLU | legacy | - | Intel | SGX | cannot: CPUID.7H:EBX[2] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U1263-U1266 Intel SGX ENCLU (NP 0F 01 D7; EREPORT, EGETKEY, EENTER, ERESUME, EEXIT, EACCEPT, EMODPE, EACCEPTCOPY) with the AEX … |
| SHA1MSG1 | legacy | 128 | Intel | SHA | runs | ✅ | hardware sweep (2 forms) |
| SHA1MSG2 | legacy | 128 | Intel | SHA | runs | ✅ | hardware sweep (2 forms) |
| SHA1NEXTE | legacy | 128 | Intel | SHA | runs | ✅ | hardware sweep (2 forms) |
| SHA1RNDS4 | legacy | 128 | Intel | SHA | runs | ✅ | hardware sweep (2 forms) |
| SHA256MSG1 | legacy | 128 | Intel | SHA | runs | ✅ | hardware sweep (2 forms) |
| SHA256MSG2 | legacy | 128 | Intel | SHA | runs | ✅ | hardware sweep (2 forms) |
| SHA256RNDS2 | legacy | 128 | Intel | SHA | runs | ✅ | hardware sweep (2 forms) |
| VSHA512MSG1 | vex | 256 | Intel | SHA512 | cannot: CPUID.7H.1:EAX[0] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U82: SDM-pseudocode reference (standard test vectors) + cases_sha_sm.txt "=>" + unit tests; strict i5-13600K profile #UD == CPU |
| VSHA512MSG2 | vex | 256 | Intel | SHA512 | cannot: CPUID.7H.1:EAX[0] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U82: SDM-pseudocode reference (standard test vectors) + cases_sha_sm.txt "=>" + unit tests; strict i5-13600K profile #UD == CPU |
| VSHA512RNDS2 | vex | 256 | Intel | SHA512 | cannot: CPUID.7H.1:EAX[0] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U82: SDM-pseudocode reference (standard test vectors) + cases_sha_sm.txt "=>" + unit tests; strict i5-13600K profile #UD == CPU |
| VSM3MSG1 | vex | 128 | Intel | SM3 | cannot: CPUID.7H.1:EAX[1] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U83: SDM-pseudocode reference (standard test vectors) + cases_sha_sm.txt "=>" + unit tests; strict i5-13600K profile #UD == CPU |
| VSM3MSG2 | vex | 128 | Intel | SM3 | cannot: CPUID.7H.1:EAX[1] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U83: SDM-pseudocode reference (standard test vectors) + cases_sha_sm.txt "=>" + unit tests; strict i5-13600K profile #UD == CPU |
| VSM3RNDS2 | vex | 128 | Intel | SM3 | cannot: CPUID.7H.1:EAX[1] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U83: SDM-pseudocode reference (standard test vectors) + cases_sha_sm.txt "=>" + unit tests; strict i5-13600K profile #UD == CPU |
| VSM4KEY4 | vex | 128/256 | Intel | SM4 | cannot: CPUID.7H.1:EAX[2] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U84: SDM-pseudocode reference (standard test vectors) + cases_sha_sm.txt "=>" + unit tests; strict i5-13600K profile #UD == CPU |
| VSM4KEY4 | evex | 128/256/512 | Intel | SM4 | cannot: CPUID.7H.1:EAX[2] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U412 EVEX SM4 (AVX10 and SM4, VL 128/256/512): model ref_sha512_sm.py via ref_avx10_b.py (GB/T 32907 example) + unit tests + … |
| VSM4RNDS4 | vex | 128/256 | Intel | SM4 | cannot: CPUID.7H.1:EAX[2] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U84: SDM-pseudocode reference (standard test vectors) + cases_sha_sm.txt "=>" + unit tests; strict i5-13600K profile #UD == CPU |
| VSM4RNDS4 | evex | 128/256/512 | Intel | SM4 | cannot: CPUID.7H.1:EAX[2] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U412 EVEX SM4 (AVX10 and SM4, VL 128/256/512): model ref_sha512_sm.py via ref_avx10_b.py (GB/T 32907 example) + unit tests + … |
| ADDPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| ADDSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| ANDNPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| ANDPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| CMPEQ_OSPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPEQ_OSSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPEQ_UQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPEQ_UQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPEQ_USPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPEQ_USSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPEQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPEQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPFALSE_OSPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPFALSE_OSSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPFALSEPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPFALSESS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPGE_OQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPGE_OQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPGEPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPGESS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPGT_OQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPGT_OQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPGTPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPGTSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPLE_OQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPLE_OQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPLEPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPLESS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPLT_OQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPLT_OQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPLTPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPLTSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNEQ_OQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNEQ_OQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNEQ_OSPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNEQ_OSSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNEQ_USPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNEQ_USSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNEQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNEQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNGE_UQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNGE_UQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNGEPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNGESS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNGT_UQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNGT_UQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNGTPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNGTSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNLE_UQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNLE_UQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNLEPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNLESS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNLT_UQPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNLT_UQSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPNLTPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPNLTSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPORD_SPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPORD_SSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPORDPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPORDSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| CMPSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| CMPTRUE_USPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPTRUE_USSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPTRUEPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPTRUESS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPUNORD_SPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPUNORD_SSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| CMPUNORDPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPPS imm8 predicate alias |
| CMPUNORDSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) — CMPSS imm8 predicate alias |
| COMISS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| CVTPI2PS | legacy | 128 | Intel | SSE | runs | ✅ | m64 form: SDM x87 transition + #MF (U532; the CPU deviates, docs/quirks.md) |
| CVTPS2PI | legacy | 64/128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| CVTSI2SS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (4 forms) |
| CVTSS2SI | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (4 forms) |
| CVTTPS2PI | legacy | 64/128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| CVTTSS2SI | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (4 forms) |
| DIVPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| DIVSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| MAXPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| MAXSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| MINPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| MINSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| MOVAPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (3 forms) |
| MOVHLPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (1 forms) |
| MOVHPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| MOVLHPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (1 forms) |
| MOVLPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| MOVMSKPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (1 forms) |
| MOVNTPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (1 forms) |
| MOVSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (3 forms) |
| MOVUPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (3 forms) |
| MULPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| MULSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| ORPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| PMOVMSKB | legacy | 64/128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| RCPPS | legacy | 128 | Intel | SSE | runs | ✅ | U81 analytic Intel 12-bit model (RN of 1/midpoint); 2^32 inputs x 6 MXCSR == CPU |
| RCPSS | legacy | 128 | Intel | SSE | runs | ✅ | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| RSQRTPS | legacy | 128 | Intel | SSE | runs | ✅ | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| RSQRTSS | legacy | 128 | Intel | SSE | runs | ✅ | U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU |
| SFENCE | legacy | - | Intel | SSE | runs | ✅ | hardware sweep (1 forms) |
| SHUFPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| SQRTPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| SQRTSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| SUBPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| SUBSS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| UCOMISS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| UNPCKHPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| UNPCKLPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| XORPS | legacy | 128 | Intel | SSE | runs | ✅ | hardware sweep (2 forms) |
| ADDPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| ADDSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| ANDNPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| ANDPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CMPEQ_OSPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPEQ_OSSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPEQ_UQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPEQ_UQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPEQ_USPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPEQ_USSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPEQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPEQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPFALSE_OSPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPFALSE_OSSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPFALSEPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPFALSESD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPGE_OQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPGE_OQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPGEPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPGESD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPGT_OQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPGT_OQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPGTPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPGTSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPLE_OQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPLE_OQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPLEPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPLESD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPLT_OQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPLT_OQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPLTPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPLTSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNEQ_OQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNEQ_OQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNEQ_OSPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNEQ_OSSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNEQ_USPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNEQ_USSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNEQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNEQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNGE_UQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNGE_UQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNGEPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNGESD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNGT_UQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNGT_UQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNGTPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNGTSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNLE_UQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNLE_UQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNLEPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNLESD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNLT_UQPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNLT_UQSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPNLTPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPNLTSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPORD_SPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPORD_SSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPORDPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPORDSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CMPTRUE_USPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPTRUE_USSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPTRUEPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPTRUESD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPUNORD_SPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPUNORD_SSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| CMPUNORDPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) — CMPPD imm8 predicate alias |
| CMPUNORDSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (5 forms) — CMPSD imm8 predicate alias |
| COMISD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTDQ2PD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTDQ2PS | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTPD2DQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTPD2PI | legacy | 64/128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTPD2PS | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTPI2PD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTPS2DQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTPS2PD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTSD2SI | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (4 forms) |
| CVTSD2SS | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTSI2SD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (4 forms) |
| CVTSS2SD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTTPD2DQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTTPD2PI | legacy | 64/128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTTPS2DQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| CVTTSD2SI | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (4 forms) |
| DIVPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| DIVSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| LFENCE | legacy | - | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| MASKMOVDQU | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| MAXPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| MAXSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| MFENCE | legacy | - | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| MINPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| MINSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| MOVAPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (3 forms) |
| MOVDQ2Q | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| MOVDQA | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (3 forms) |
| MOVDQU | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (3 forms) |
| MOVHPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| MOVLPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| MOVMSKPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| MOVNTDQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| MOVNTI | legacy | - | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| MOVNTPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| MOVQ2DQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| MOVUPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (3 forms) |
| MULPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| MULSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| ORPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| PADDQ | legacy | 64/128 | Intel | SSE2 | runs | ✅ | hardware sweep (4 forms) |
| PMULUDQ | legacy | 64/128 | Intel | SSE2 | runs | ✅ | hardware sweep (4 forms) |
| PSHUFD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| PSHUFHW | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| PSHUFLW | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| PSLLDQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| PSRLDQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (1 forms) |
| PSUBQ | legacy | 64/128 | Intel | SSE2 | runs | ✅ | hardware sweep (4 forms) |
| PUNPCKHQDQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| PUNPCKLQDQ | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| SHUFPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| SQRTPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| SQRTSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| SUBPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| SUBSD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| UCOMISD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| UNPCKHPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| UNPCKLPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| XORPD | legacy | 128 | Intel | SSE2 | runs | ✅ | hardware sweep (2 forms) |
| ADDSUBPD | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| ADDSUBPS | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| HADDPD | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| HADDPS | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| HSUBPD | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| HSUBPS | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| LDDQU | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (1 forms) |
| MOVDDUP | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| MOVSHDUP | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| MOVSLDUP | legacy | 128 | Intel | SSE3 | runs | ✅ | hardware sweep (2 forms) |
| FISTTP | legacy | - | Intel | SSE3X87 | runs | ✅ | hardware sweep (3 forms) |
| BLENDPD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| BLENDPS | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| BLENDVPD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| BLENDVPS | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| DPPD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| DPPS | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| EXTRACTPS | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| INSERTPS | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| MOVNTDQA | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (1 forms) |
| MPSADBW | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PACKUSDW | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PBLENDVB | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PBLENDW | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PCMPEQQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PEXTRB | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PEXTRD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PEXTRQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PHMINPOSUW | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PINSRB | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PINSRD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PINSRQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMAXSB | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMAXSD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMAXUD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMAXUW | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMINSB | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMINSD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMINUD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMINUW | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVSXBD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVSXBQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVSXBW | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVSXDQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVSXWD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVSXWQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVZXBD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVZXBQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVZXBW | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVZXDQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVZXWD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMOVZXWQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMULDQ | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PMULLD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| PTEST | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| ROUNDPD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| ROUNDPS | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| ROUNDSD | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| ROUNDSS | legacy | 128 | Intel | SSE4 | runs | ✅ | hardware sweep (2 forms) |
| CRC32 | legacy | - | Intel | SSE42 | runs | ✅ | hardware sweep (8 forms) |
| PCMPESTRI | legacy | 128 | Intel | SSE42 | runs | ✅ | hardware sweep (2 forms) |
| PCMPESTRM | legacy | 128 | Intel | SSE42 | runs | ✅ | hardware sweep (2 forms) |
| PCMPGTQ | legacy | 128 | Intel | SSE42 | runs | ✅ | hardware sweep (2 forms) |
| PCMPISTRI | legacy | 128 | Intel | SSE42 | runs | ✅ | hardware sweep (2 forms) |
| PCMPISTRM | legacy | 128 | Intel | SSE42 | runs | ✅ | hardware sweep (2 forms) |
| EXTRQ | legacy | 128 | AMD | SSE4a | cannot: AMD SSE4a, on --host and --guest | ❌ · ⏳ open item | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| INSERTQ | legacy | 128 | AMD | SSE4a | cannot: AMD SSE4a, on --host and --guest | ❌ · ⏳ open item | implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| MOVNTSD | legacy | 128 | AMD | SSE4a | cannot: AMD SSE4a, on --host and --guest | ❌ · ⏳ open item | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| MOVNTSS | legacy | 128 | AMD | SSE4a | cannot: AMD SSE4a, on --host and --guest | ❌ · ⏳ open item | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| LDMXCSR | legacy | - | Intel | SSEMXCSR | runs | ✅ | hardware sweep (1 forms) |
| STMXCSR | legacy | - | Intel | SSEMXCSR | runs | ✅ | hardware sweep (1 forms) |
| PREFETCHNTA | legacy | - | Intel | SSE_PREFETCH | runs | ✅ | hardware sweep (1 forms) |
| PREFETCHT0 | legacy | - | Intel | SSE_PREFETCH | runs | ✅ | hardware sweep (1 forms) |
| PREFETCHT1 | legacy | - | Intel | SSE_PREFETCH | runs | ✅ | hardware sweep (1 forms) |
| PREFETCHT2 | legacy | - | Intel | SSE_PREFETCH | runs | ✅ | hardware sweep (1 forms) |
| PABSB | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PABSD | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PABSW | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PALIGNR | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PHADDD | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PHADDSW | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PHADDW | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PHSUBD | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PHSUBSW | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PHSUBW | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PMADDUBSW | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PMULHRSW | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PSHUFB | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PSIGNB | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PSIGND | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| PSIGNW | legacy | 64/128 | Intel | SSSE3 | runs | ✅ | hardware sweep (4 forms) |
| BEXTR | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ✅ per manual | hardware sweep (4 forms) |
| BLCFILL | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| BLCI | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| BLCIC | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| BLCMSK | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| BLCS | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| BLSFILL | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| BLSIC | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| T1MSKC | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| TZMSK | xop | - | AMD | TBM | cannot: AMD TBM, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD TBM |
| XRESLDTRK | legacy | - | Intel | TSX_LDTRK | cannot: CPUID.7H:EDX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U110 TSX: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| XSUSLDTRK | legacy | - | Intel | TSX_LDTRK | cannot: CPUID.7H:EDX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U110 TSX: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| CLUI | legacy | - | Intel | UINTR | cannot: CPUID.7H:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U104 UINTR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile #UD … |
| SENDUIPI | legacy | - | Intel | UINTR | cannot: CPUID.7H:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U104 UINTR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile #UD … |
| STUI | legacy | - | Intel | UINTR | cannot: CPUID.7H:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U104 UINTR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile #UD … |
| TESTUI | legacy | - | Intel | UINTR | cannot: CPUID.7H:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U104 UINTR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile #UD … |
| UIRET | legacy | - | Intel | UINTR | cannot: CPUID.7H:EDX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U104 UINTR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile #UD … |
| URDMSR | legacy | - | Intel | USER_MSR | cannot: CPUID.7H.1:EDX[15] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U103 USER_MSR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile … |
| URDMSR | vex | - | Intel | USER_MSR | cannot: CPUID.7H.1:EDX[15] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U103 USER_MSR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile … |
| UWRMSR | legacy | - | Intel | USER_MSR | cannot: CPUID.7H.1:EDX[15] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U103 USER_MSR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile … |
| UWRMSR | vex | - | Intel | USER_MSR | cannot: CPUID.7H.1:EDX[15] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U103 USER_MSR: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests + cases_keylocker.txt "=>"; strict i5-13600K profile … |
| XCRYPTCBC | legacy | - | VIA | VIA_PADLOCK_AES | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| XCRYPTCFB | legacy | - | VIA | VIA_PADLOCK_AES | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| XCRYPTCTR | legacy | - | VIA | VIA_PADLOCK_AES | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| XCRYPTECB | legacy | - | VIA | VIA_PADLOCK_AES | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| XCRYPTOFB | legacy | - | VIA | VIA_PADLOCK_AES | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| MONTMUL | legacy | - | VIA | VIA_PADLOCK_MONTMUL | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| XSTORE | legacy | - | VIA | VIA_PADLOCK_RNG | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| XSHA1 | legacy | - | VIA | VIA_PADLOCK_SHA | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| XSHA256 | legacy | - | VIA | VIA_PADLOCK_SHA | cannot: VIA PadLock, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): VIA PadLock |
| TPAUSE | legacy | - | Intel | WAITPKG | cannot: --guest: CPUID.7H:ECX[5] = 0; --host: enumerated (CPUID.7H:ECX[5] = 1 on the bare host) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U111 WAITPKG: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| UMONITOR | legacy | - | Intel | WAITPKG | cannot: --guest: CPUID.7H:ECX[5] = 0; --host: enumerated (CPUID.7H:ECX[5] = 1 on the bare host) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U111 WAITPKG: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| UMWAIT | legacy | - | Intel | WAITPKG | cannot: --guest: CPUID.7H:ECX[5] = 0; --host: enumerated (CPUID.7H:ECX[5] = 1 on the bare host) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U111 WAITPKG: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU |
| F2XM1 | legacy | - | Intel | X87 | runs | ✅ | U56 Goldmont-microcode model, 100% bit-exact (value + FSW) |
| FABS | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FADD | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (4 forms) |
| FADDP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FBLD | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FBSTP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FCHS | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FCLEX | legacy | - | Intel | X87 | runs | ✅ | hardware (cases_reach): 9B DB E2 (FWAIT + FNCLEX; pending #MF with CR0.NE) |
| FCOM | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (3 forms) |
| FCOMP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (3 forms) |
| FCOMPP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FCOS | legacy | - | Intel | X87 | runs | ✅ | U56 microcode model, 100% bit-exact |
| FDECSTP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FDISI8087_NOP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FDIV | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (4 forms) |
| FDIVP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FDIVR | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (4 forms) |
| FDIVRP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FENI8087_NOP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FFREE | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FFREEP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FIADD | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FICOM | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FICOMP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FIDIV | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FIDIVR | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FILD | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (3 forms) |
| FIMUL | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FINCSTP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FINIT | legacy | - | Intel | X87 | runs | ✅ | hardware (cases_reach): 9B DB E3 (FWAIT + FNINIT) |
| FIST | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FISTP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (3 forms) |
| FISUB | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FISUBR | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FLD | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (4 forms) |
| FLD1 | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FLDCW | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FLDENV | legacy | - | Intel | X87 | runs | ✅ | U64 |
| FLDL2E | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FLDL2T | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FLDLG2 | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FLDLN2 | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FLDPI | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FLDZ | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FMUL | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (4 forms) |
| FMULP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FNCLEX | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FNINIT | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FNOP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FNSAVE | legacy | - | Intel | X87 | runs | ✅ | U61-U64 |
| FNSTCW | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FNSTENV | legacy | - | Intel | X87 | runs | ✅ | U61/U62/U64 (FCW masked after, reserved FFFF, FIP/FOP/FDP model) |
| FNSTSW | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (2 forms) |
| FPATAN | legacy | - | Intel | X87 | runs | ✅ | U56 microcode model, 100% bit-exact |
| FPREM | legacy | - | Intel | X87 | runs | ✅ | ROM model == hw 148/148, fork == hw 388/388 |
| FPREM1 | legacy | - | Intel | X87 | runs | ✅ | ROM model == hw, fork == hw |
| FPTAN | legacy | - | Intel | X87 | runs | ✅ | U56 microcode model, 100% bit-exact |
| FRNDINT | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FRSTOR | legacy | - | Intel | X87 | runs | ✅ | U63/U64 |
| FSAVE | legacy | - | Intel | X87 | runs | ✅ | U61-U64 |
| FSCALE | legacy | - | Intel | X87 | runs | ✅ | ROM model == hw, fork == hw |
| FSETPM | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FSIN | legacy | - | Intel | X87 | runs | ✅ | U56 microcode model, 100% bit-exact |
| FSINCOS | legacy | - | Intel | X87 | runs | ✅ | U56 microcode model, 100% bit-exact |
| FSQRT | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FST | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (3 forms) |
| FSTCW | legacy | - | Intel | X87 | runs | ✅ | hardware (cases_reach): 9B D9 /7 (FWAIT + FNSTCW) |
| FSTENV | legacy | - | Intel | X87 | runs | ✅ | U61/U62/U64 |
| FSTP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (4 forms) |
| FSTPNCE | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FSTSW | legacy | - | Intel | X87 | runs | ✅ | hardware (cases_reach): 9B DD /7, 9B DF E0 (FWAIT + FNSTSW; FIP per U90) |
| FSUB | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (4 forms) |
| FSUBP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FSUBR | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (4 forms) |
| FSUBRP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FTST | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FUCOM | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FUCOMP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FUCOMPP | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FXAM | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FXCH | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FXTRACT | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| FYL2X | legacy | - | Intel | X87 | runs | ✅ | U56 microcode model, 100% bit-exact |
| FYL2XP1 | legacy | - | Intel | X87 | runs | ✅ | U56 microcode model, 100% bit-exact; x < -1: SDM #IA (U533; the CPU deviates, docs/quirks.md) |
| WAIT | legacy | - | Intel | X87 | runs | ✅ | hardware sweep (1 forms) |
| VFRCZPD | xop | 128/256 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VFRCZPS | xop | 128/256 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VFRCZSD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VFRCZSS | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCMOV | xop | 128/256 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMEQB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMEQD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMEQQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMEQUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMEQUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMEQUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMEQUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMEQW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMFALSEB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMFALSED | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMFALSEQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMFALSEUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMFALSEUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMFALSEUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMFALSEUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMFALSEW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGEB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGED | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGEQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGEUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGEUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGEUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGEUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGEW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGTB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGTD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGTQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGTUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGTUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGTUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGTUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMGTW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLEB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLED | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLEQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLEUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLEUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLEUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLEUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLEW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLTB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLTD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLTQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLTUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLTUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLTUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLTUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMLTW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMNEQB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMNEQD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMNEQQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMNEQUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMNEQUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMNEQUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMNEQUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMNEQW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMTRUEB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMTRUED | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMTRUEQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMTRUEUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMTRUEUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMTRUEUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMTRUEUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMTRUEW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMUB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMUD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMUQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMUW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPCOMW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPERMIL2PD | vex | 128/256 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPERMIL2PS | vex | 128/256 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDBD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDBQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDBW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDDQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDUBD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDUBQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDUBW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDUDQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDUWD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDUWQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDWD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHADDWQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHSUBBW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHSUBDQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPHSUBWD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSDD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSDQH | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSDQL | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSSDD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSSDQH | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSSDQL | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSSWD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSSWW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSWD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMACSWW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMADCSSWD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPMADCSWD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPPERM | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPROTB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPROTD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPROTQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPROTW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPSHAB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPSHAD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPSHAQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPSHAW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPSHLB | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPSHLD | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPSHLQ | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| VPSHLW | xop | 128 | AMD | XOP | cannot: AMD XOP, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (out of scope until plan 1.15f): AMD XOP |
| XGETBV | legacy | - | Intel | XSAVE | runs | ✅ | hardware sweep (1 forms) |
| XRSTOR | legacy | - | Intel | XSAVE | runs | ✅ | hardware sweep (1 forms) |
| XRSTOR64 | legacy | - | Intel | XSAVE | runs | ✅ | hardware sweep (1 forms) |
| XSAVE | legacy | - | Intel | XSAVE | runs | ✅ | hardware sweep (1 forms) |
| XSAVE64 | legacy | - | Intel | XSAVE | runs | ✅ | hardware sweep (1 forms) |
| XSAVEC | legacy | - | Intel | XSAVEC | runs | ✅ | U66 compacted format |
| XSAVEC64 | legacy | - | Intel | XSAVEC | runs | ✅ | U66 |
| XSAVEOPT | legacy | - | Intel | XSAVEOPT | runs | ✅ | hardware sweep (1 forms) |
| XSAVEOPT64 | legacy | - | Intel | XSAVEOPT | runs | ✅ | hardware sweep (1 forms) |
| TCVTROWD2PS | evex | 512 | Intel | AMX_AVX512 | cannot: AMX_AVX512 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TCVTROWPS2BF16H | evex | 512 | Intel | AMX_AVX512 | cannot: AMX_AVX512 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TCVTROWPS2BF16L | evex | 512 | Intel | AMX_AVX512 | cannot: AMX_AVX512 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TCVTROWPS2PHH | evex | 512 | Intel | AMX_AVX512 | cannot: AMX_AVX512 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TCVTROWPS2PHL | evex | 512 | Intel | AMX_AVX512 | cannot: AMX_AVX512 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TDPBF16PS | vex | - | Intel | AMX_BF16 | cannot: CPUID.7H:EDX[22] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TCMMIMFP16PS | vex | - | Intel | AMX_COMPLEX | cannot: CPUID.7H.1:EDX[8] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TCMMRLFP16PS | vex | - | Intel | AMX_COMPLEX | cannot: CPUID.7H.1:EDX[8] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TDPFP16PS | vex | - | Intel | AMX_FP16 | cannot: CPUID.7H.1:EAX[21] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TDPBF8PS | vex | - | Intel | AMX_FP8 | cannot: AMX_FP8 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TDPBHF8PS | vex | - | Intel | AMX_FP8 | cannot: AMX_FP8 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TDPHBF8PS | vex | - | Intel | AMX_FP8 | cannot: AMX_FP8 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TDPHF8PS | vex | - | Intel | AMX_FP8 | cannot: AMX_FP8 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TDPBSSD | vex | - | Intel | AMX_INT8 | cannot: CPUID.7H:EDX[25] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TDPBSUD | vex | - | Intel | AMX_INT8 | cannot: CPUID.7H:EDX[25] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TDPBUSD | vex | - | Intel | AMX_INT8 | cannot: CPUID.7H:EDX[25] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TDPBUUD | vex | - | Intel | AMX_INT8 | cannot: CPUID.7H:EDX[25] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TILELOADDRS | vex | - | Intel | AMX_MOVRS | cannot: AMX_MOVRS not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TILELOADDRST1 | vex | - | Intel | AMX_MOVRS | cannot: AMX_MOVRS not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TMMULTF32PS | vex | - | Intel | AMX_TF32 | cannot: AMX_TF32 not reported by this CPU, on --host and --guest | ❌ · ⏳ open item | not bit-exact / decided open item: U1150 AMX-TF32 (ISE 319433-060 3.7 pp. 3-32/3-33; withdrawn in ISE -062, no product, no SDM text) behind its own … |
| TILELOADD | vex | - | Intel | AMX_TILE | cannot: CPUID.7H:EDX[24] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TILELOADDT1 | vex | - | Intel | AMX_TILE | cannot: CPUID.7H:EDX[24] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TILESTORED | vex | - | Intel | AMX_TILE | cannot: CPUID.7H:EDX[24] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| LDTILECFG | vex | - | Intel | AMX_TILE_BASE | cannot: CPUID.7H:EDX[24] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| STTILECFG | vex | - | Intel | AMX_TILE_BASE | cannot: CPUID.7H:EDX[24] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TILERELEASE | vex | - | Intel | AMX_TILE_BASE | cannot: CPUID.7H:EDX[24] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| TILEZERO | vex | - | Intel | AMX_TILE_BASE | cannot: CPUID.7H:EDX[24] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U175-U180 Intel AMX (VEX, UC_CTL_X86_AMX opt-in): independent SDM-pseudocode model (ref_amx.py) + unit tests + cases_amx.txt "=>"; the … |
| ADC | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| ADD | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| AND | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| CRC32 | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| DEC | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| DIV | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| IDIV | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| IMUL | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| INC | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| JMPABS | legacy | - | Intel | APX_F | cannot: APX_F not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U644/U790 Intel APX JMPABS (REX2 M0 = 0 W = 0 A1 imm64): independent spec model (ref_apx_cases.py; #UD rules: ref_apx_map4.py) + … |
| KMOVB | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX-promoted KMOV (EVEX map 1 90-93, GPRs R16-R31 / EGPR addresses): independent spec model (ref_apx_map4.py --ext) + … |
| KMOVD | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX-promoted KMOV (EVEX map 1 90-93, GPRs R16-R31 / EGPR addresses): independent spec model (ref_apx_map4.py --ext) + … |
| KMOVQ | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX-promoted KMOV (EVEX map 1 90-93, GPRs R16-R31 / EGPR addresses): independent spec model (ref_apx_map4.py --ext) + … |
| KMOVW | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX-promoted KMOV (EVEX map 1 90-93, GPRs R16-R31 / EGPR addresses): independent spec model (ref_apx_map4.py --ext) + … |
| MUL | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| NEG | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| NOT | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| OR | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| POPP | legacy | - | Intel | APX_F | cannot: APX_F not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U614 Intel APX PUSHP/POPP (REX2 50+r / 58+r with REX2.W = 1 = PPX hint; R16-R31): independent spec model (ref_apx_core.py) + … |
| PUSHP | legacy | - | Intel | APX_F | cannot: APX_F not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U614 Intel APX PUSHP/POPP (REX2 50+r / 58+r with REX2.W = 1 = PPX hint; R16-R31): independent spec model (ref_apx_core.py) + … |
| RCL | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| RCR | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| ROL | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| ROR | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| SAL | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms; SAL = SHL: /4 and /6 both encode it (APX spec 3.1.2.3.1 … |
| SAR | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| SBB | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| SHL | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms; /4 counted here (/6 = SAL row)): independent spec model … |
| SHLD | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| SHR | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| SHRD | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| SUB | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| XOR | evex | - | Intel | APX_F | cannot: APX_F not reported by this CPU; APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| ADCX | evex | - | Intel | APX_F_ADX | cannot: APX_F_ADX not reported by this CPU; APX_F_ADX_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| ADOX | evex | - | Intel | APX_F_ADX | cannot: APX_F_ADX not reported by this CPU; APX_F_ADX_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| TILELOADD | evex | - | Intel | APX_F_AMX | cannot: APX_F_AMX not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX-promoted AMX memory form (EVEX map 2 49/4B, EGPR addresses): independent spec model (ref_apx_map4.py --ext) + … |
| TILELOADDT1 | evex | - | Intel | APX_F_AMX | cannot: APX_F_AMX not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646/U790 Intel APX-promoted AMX memory form (EVEX map 2 49/4B, EGPR addresses): independent spec model (ref_apx_cases.py; #UD rules … |
| TILESTORED | evex | - | Intel | APX_F_AMX | cannot: APX_F_AMX not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX-promoted AMX memory form (EVEX map 2 49/4B, EGPR addresses): independent spec model (ref_apx_map4.py --ext) + … |
| LDTILECFG | evex | - | Intel | APX_F_AMX_BASE | cannot: APX_F_AMX_BASE not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX-promoted AMX memory form (EVEX map 2 49/4B, EGPR addresses): independent spec model (ref_apx_map4.py --ext) + … |
| STTILECFG | evex | - | Intel | APX_F_AMX_BASE | cannot: APX_F_AMX_BASE not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX-promoted AMX memory form (EVEX map 2 49/4B, EGPR addresses): independent spec model (ref_apx_map4.py --ext) + … |
| TILELOADDRS | evex | - | Intel | APX_F_AMX_MOVRS | cannot: APX_F_AMX_MOVRS not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| TILELOADDRST1 | evex | - | Intel | APX_F_AMX_MOVRS | cannot: APX_F_AMX_MOVRS not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U720-U725 Intel AMX ISE 319433-062 (UC_CTL_X86_AMX FP8/AVX512/MOVRS opt-in): independent ISE-pseudocode model (ref_amx.py … |
| ANDN | evex | - | Intel | APX_F_BMI1 | cannot: APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs, NF): independent spec model (ref_apx_map4.py) + … |
| BEXTR | evex | - | Intel | APX_F_BMI1 | cannot: APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs, NF): independent spec model (ref_apx_map4.py) + … |
| BLSI | evex | - | Intel | APX_F_BMI1 | cannot: APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs, NF): independent spec model (ref_apx_map4.py) + … |
| BLSMSK | evex | - | Intel | APX_F_BMI1 | cannot: APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs, NF): independent spec model (ref_apx_map4.py) + … |
| BLSR | evex | - | Intel | APX_F_BMI1 | cannot: APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs, NF): independent spec model (ref_apx_map4.py) + … |
| TZCNT | evex | - | Intel | APX_F_BMI1 | cannot: APX_F_BMI1 not reported by this CPU; APX_F_BMI1_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| BZHI | evex | - | Intel | APX_F_BMI2 | cannot: APX_F_BMI2 not reported by this CPU; APX_F_BMI2_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs, NF): independent spec model (ref_apx_map4.py) + … |
| MULX | evex | - | Intel | APX_F_BMI2 | cannot: APX_F_BMI2 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| PDEP | evex | - | Intel | APX_F_BMI2 | cannot: APX_F_BMI2 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| PEXT | evex | - | Intel | APX_F_BMI2 | cannot: APX_F_BMI2 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| RORX | evex | - | Intel | APX_F_BMI2 | cannot: APX_F_BMI2 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| SARX | evex | - | Intel | APX_F_BMI2 | cannot: APX_F_BMI2 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| SHLX | evex | - | Intel | APX_F_BMI2 | cannot: APX_F_BMI2 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| SHRX | evex | - | Intel | APX_F_BMI2 | cannot: APX_F_BMI2 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| WRSSD | evex | - | Intel | APX_F_CET | cannot: APX_F_CET not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645/U790 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_cases.py; #UD rules … |
| WRSSQ | evex | - | Intel | APX_F_CET | cannot: APX_F_CET not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645/U790 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_cases.py; #UD rules … |
| CMPAEXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs; same opcode as CMPNBXADD (E3)): independent spec model … |
| CMPAXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs; same opcode as CMPNBEXADD (E7)): independent spec model … |
| CMPBEXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPBXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPEXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs; same opcode as CMPZXADD (E4)): independent spec model … |
| CMPGEXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs; same opcode as CMPNLXADD (ED)): independent spec model … |
| CMPGXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs; same opcode as CMPNLEXADD (EF)): independent spec model … |
| CMPLEXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPLXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPNBEXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPNBXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPNEXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs; same opcode as CMPNZXADD (E5)): independent spec model … |
| CMPNLEXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPNLXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPNOXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPNPXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPNSXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPNZXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPOXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPPXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPSXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| CMPZXADD | evex | - | Intel | APX_F_CMPCCXADD | cannot: APX_F_CMPCCXADD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U646 Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 … |
| ENQCMD | evex | - | Intel | APX_F_ENQCMD | cannot: APX_F_ENQCMD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645/U790 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_cases.py; #UD rules … |
| LZCNT | evex | - | Intel | APX_F_LZCNT | cannot: APX_F_LZCNT not reported by this CPU; APX_F_LZCNT_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| MOVBE | evex | - | Intel | APX_F_MOVBE | cannot: APX_F_MOVBE not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| MOVDIR64B | evex | - | Intel | APX_F_MOVDIR64B | cannot: APX_F_MOVDIR64B not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| MOVDIRI | evex | - | Intel | APX_F_MOVDIRI | cannot: APX_F_MOVDIRI not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| MOVRS | evex | - | Intel | APX_F_MOVRS | cannot: APX_F_MOVRS not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| CCMPB | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 8 … |
| CCMPBE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 17 … |
| CCMPF | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 46 … |
| CCMPL | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 18 … |
| CCMPLE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 11 … |
| CCMPNB | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 15 … |
| CCMPNBE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 12 … |
| CCMPNL | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 10 … |
| CCMPNLE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 13 … |
| CCMPNO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 14 … |
| CCMPNS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 9 … |
| CCMPNZ | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 13 … |
| CCMPO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 11 … |
| CCMPS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 11 … |
| CCMPT | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 38 … |
| CCMPZ | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CCMPscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 13 … |
| CFCMOVB | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVBE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVL | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVLE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVNB | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVNBE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVNL | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVNLE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVNO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVNP | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVNS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVNZ | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVP | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CFCMOVZ | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false): independent spec model (ref_apx_map4.py) + … |
| CMOVA | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" … |
| CMOVAE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" … |
| CMOVB | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 5 "=>" … |
| CMOVBE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 5 "=>" … |
| CMOVE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 1 "=>" … |
| CMOVG | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 3 "=>" … |
| CMOVGE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" … |
| CMOVL | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 5 "=>" … |
| CMOVLE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 "=>" … |
| CMOVNE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 1 "=>" … |
| CMOVNO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" … |
| CMOVNP | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 "=>" … |
| CMOVNS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 1 "=>" … |
| CMOVO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 2 "=>" … |
| CMOVP | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 3 "=>" … |
| CMOVS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U642 Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 3 "=>" … |
| CTESTB | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 10 … |
| CTESTBE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 8 … |
| CTESTF | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 35 … |
| CTESTL | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 7 … |
| CTESTLE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 9 … |
| CTESTNB | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 10 … |
| CTESTNBE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 11 … |
| CTESTNL | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 11 … |
| CTESTNLE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 10 … |
| CTESTNO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 5 … |
| CTESTNS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 10 … |
| CTESTNZ | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 11 … |
| CTESTO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 6 … |
| CTESTS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 10 … |
| CTESTT | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 34 … |
| CTESTZ | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U641 Intel APX CTESTscc (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0]): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 7 … |
| POP2 | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U643 Intel APX POP2 (EVEX map 4 8F /0, ND = 1, W = 0 PPX): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 14 "=>" … |
| POP2P | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U643 Intel APX POP2P (EVEX map 4 8F /0, ND = 1, W = 1 PPX): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 12 "=>" … |
| PUSH2 | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U643 Intel APX PUSH2 (EVEX map 4 FF /6, ND = 1, W = 0 PPX): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 17 "=>" … |
| PUSH2P | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U643 Intel APX PUSH2P (EVEX map 4 FF /6, ND = 1, W = 1 PPX): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 10 "=>" … |
| SETA | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETAE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETB | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETBE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 6 "=>" (--apx) … |
| SETG | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETGE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETL | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETLE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETNE | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 6 "=>" (--apx) … |
| SETNO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETNP | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETNS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETO | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETP | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| SETS | evex | - | Intel | APX_F_N3 | cannot: APX_F_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU): independent spec model (ref_apx_map4.py) + cases_apx_map4.txt 4 "=>" (--apx) … |
| POPCNT | evex | - | Intel | APX_F_POPCNT | cannot: APX_F_POPCNT not reported by this CPU; APX_F_POPCNT_N3 not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640 Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms): independent spec model (ref_apx_map4.py) + … |
| AADD | evex | - | Intel | APX_F_RAO_INT | cannot: APX_F_RAO_INT not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| AAND | evex | - | Intel | APX_F_RAO_INT | cannot: APX_F_RAO_INT not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| AOR | evex | - | Intel | APX_F_RAO_INT | cannot: APX_F_RAO_INT not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| AXOR | evex | - | Intel | APX_F_RAO_INT | cannot: APX_F_RAO_INT not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_map4.py) + cases_apx_map4.txt … |
| URDMSR | evex | - | Intel | APX_F_USER_MSR | cannot: APX_F_USER_MSR not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645/U790 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_cases.py; #UD rules … |
| UWRMSR | evex | - | Intel | APX_F_USER_MSR | cannot: APX_F_USER_MSR not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645/U790 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_cases.py; #UD rules … |
| VADDBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCMPBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCOMISBF16 | evex | 128 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VDIVBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFMADD132BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFMADD213BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFMADD231BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFMSUB132BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFMSUB213BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFMSUB231BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFNMADD132BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFNMADD213BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFNMADD231BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFNMSUB132BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFNMSUB213BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFNMSUB231BF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VFPCLASSBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VGETEXPBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VGETMANTBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMAXBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMINBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMULBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VRCPBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | documented bound, not bit-exact: U1243 AMB-2 (decisions A9 2026-10-09 / AMB-2 2026-10-10): documented compliance, NOT bit-exact - the analytic … |
| VREDUCEBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VRNDSCALEBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VRSQRTBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | documented bound, not bit-exact: U1243 AMB-2 (decisions A9 2026-10-09 / AMB-2 2026-10-10): documented compliance, NOT bit-exact - the analytic … |
| VSCALEFBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VSQRTBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VSUBBF16 | evex | 128/256/512 | Intel | AVX10_2_BF16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMOVRSB | evex | 128/256/512 | Intel | AVX10_MOVRS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND MOVRS | ❌ · ✅ per manual | SDM vectors: U409 EVEX VMOVRS* (AVX10 and MOVRS, VL 128/256/512): independent model ref_avx10_b.py + unit tests + cases_avx10_b.txt "=>" (--avx10) … |
| VMOVRSD | evex | 128/256/512 | Intel | AVX10_MOVRS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND MOVRS | ❌ · ✅ per manual | SDM vectors: U409 EVEX VMOVRS* (AVX10 and MOVRS, VL 128/256/512): independent model ref_avx10_b.py + unit tests + cases_avx10_b.txt "=>" (--avx10) … |
| VMOVRSQ | evex | 128/256/512 | Intel | AVX10_MOVRS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND MOVRS | ❌ · ✅ per manual | SDM vectors: U409 EVEX VMOVRS* (AVX10 and MOVRS, VL 128/256/512): independent model ref_avx10_b.py + unit tests + cases_avx10_b.txt "=>" (--avx10) … |
| VMOVRSW | evex | 128/256/512 | Intel | AVX10_MOVRS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND MOVRS | ❌ · ✅ per manual | SDM vectors: U409 EVEX VMOVRS* (AVX10 and MOVRS, VL 128/256/512): independent model ref_avx10_b.py + unit tests + cases_avx10_b.txt "=>" (--avx10) … |
| VCVTBF42HF8 | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP4 -> E4M3 exact (ACE 9.5, 16.4; E4NF, Half Mem), every nibble; VL 128/256/512. Specification-validated: independent … |
| VCVTBF62HF8 | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP6 -> E4M3 exact (ACE 9.7, 16.4; E7NM), every input; VL 128/256/512. Specification-validated: independent model … |
| VCVTBF82BF4S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP8 -> FP4 E2M1 (ACE 9.4, 16.3; E4NF, r/m destination G2, no writemask), every input; NaN -> +-6.0 as printed (SDE … |
| VCVTBF82BF6S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP8 -> FP6 (ACE 9.6, 16.3; E7NM register only, r/m destination G2), every input; NaN / e=15 -> +-max as printed (SDE … |
| VCVTBF82PS | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP8 -> FP32 exact (ACE 9.3, 16.3; E4, Quarter Mem), every input; VL 128/256/512. Specification-validated: independent … |
| VCVTBIASPS2BF8 | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 bias rounding (ACE 9.2, 16.1; E4); gaps G1 (bias = SRC1.dword[i][7:0] in the 8 bits below the LSB), G3 … |
| VCVTBIASPS2BF8S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 bias rounding (ACE 9.2, 16.1; E4); gaps G1 (bias = SRC1.dword[i][7:0] in the 8 bits below the LSB), G3 … |
| VCVTBIASPS2HF8 | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 bias rounding (ACE 9.2, 16.1; E4); gaps G1 (bias = SRC1.dword[i][7:0] in the 8 bits below the LSB), G3 … |
| VCVTBIASPS2HF8S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 bias rounding (ACE 9.2, 16.1; E4); gaps G1 (bias = SRC1.dword[i][7:0] in the 8 bits below the LSB), G3 … |
| VCVTHF62HF8 | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP6 -> E4M3 exact (ACE 9.7, 16.4; E7NM), every input; VL 128/256/512. Specification-validated: independent model … |
| VCVTHF82BF4S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP8 -> FP4 E2M1 (ACE 9.4, 16.3; E4NF, r/m destination G2, no writemask), every input; NaN -> +-6.0 as printed (SDE … |
| VCVTHF82HF6S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP8 -> FP6 (ACE 9.6, 16.3; E7NM register only, r/m destination G2), every input; NaN / e=15 -> +-max as printed (SDE … |
| VCVTHF82PS | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP8 -> FP32 exact (ACE 9.3, 16.3; E4, Quarter Mem), every input; VL 128/256/512. Specification-validated: independent … |
| VCVTPS2BF8 | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 RTNE / RTO (ACE 9.2, 16.1; E4, m32bcst, {k}{z}); RTNE / RTO equal to the exact-rational reading; VL … |
| VCVTPS2BF8S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 RTNE / RTO (ACE 9.2, 16.1; E4, m32bcst, {k}{z}); RTNE / RTO equal to the exact-rational reading; VL … |
| VCVTPS2HF8 | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 RTNE / RTO (ACE 9.2, 16.1; E4, m32bcst, {k}{z}); RTNE / RTO equal to the exact-rational reading; VL … |
| VCVTPS2HF8S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 RTNE / RTO (ACE 9.2, 16.1; E4, m32bcst, {k}{z}); RTNE / RTO equal to the exact-rational reading; VL … |
| VCVTROPS2HF8 | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 RTNE / RTO (ACE 9.2, 16.1; E4, m32bcst, {k}{z}); RTNE / RTO equal to the exact-rational reading; VL … |
| VCVTROPS2HF8S | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX FP32 -> FP8 RTNE / RTO (ACE 9.2, 16.1; E4, m32bcst, {k}{z}); RTNE / RTO equal to the exact-rational reading; VL … |
| VPMOVSSDB | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX dword -> byte symmetric saturation [-127, 127] (ACE 9.8; E6 masked store, r/m destination G2); VL 128/256/512. … |
| VUNPACKB | evex | 128/256/512 | Intel | AVX10_V2_AUX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10 AND AVX10_V2_AUX | ❌ · ✅ per manual | spec model: U1095 AVX10_V2_AUX sub-byte fields to bytes (ACE 9.9; E4NF), every size/start/sign imm8 and reserved imm8 (conditioned as printed; SDE … |
| KADDD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KADDQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KANDD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KANDND | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KANDNQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KANDQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KMOVD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KMOVQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KNOTD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KNOTQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KORD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KORQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KORTESTD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KORTESTQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KSHIFTLD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KSHIFTLQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KSHIFTRD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KSHIFTRQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KTESTD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KTESTQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KUNPCKDQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KUNPCKWD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KXNORD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KXNORQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KXORD | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KXORQ | vex | - | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| VDBPSADBW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VMOVDQU16 | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VMOVDQU8 | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPABSB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPABSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPACKSSDW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPACKSSWB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPACKUSDW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPACKUSWB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPADDB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPADDSB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPADDSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPADDUSB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPADDUSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPADDW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPALIGNR | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPAVGB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPAVGW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPBLENDMB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPBLENDMW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPBROADCASTB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPBROADCASTW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPCMPB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPCMPEQB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPCMPEQW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPCMPGTB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPCMPGTW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPCMPUB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPCMPUW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPCMPW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPERMI2W | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPERMT2W | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPERMW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPEXTRB | evex | 128 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPEXTRW | evex | 128 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPINSRB | evex | 128 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPINSRW | evex | 128 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMADDUBSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMADDWD | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMAXSB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMAXSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMAXUB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMAXUW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMINSB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMINSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMINUB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMINUW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVB2M | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVM2B | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVM2W | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVSWB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVSXBW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVUSWB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVW2M | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVWB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMOVZXBW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMULHRSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMULHUW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMULHW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPMULLW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSADBW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSHUFB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSHUFHW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSHUFLW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSLLDQ | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSLLVW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSLLW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSRAVW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSRAW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSRLDQ | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSRLVW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSRLW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSUBB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSUBSB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSUBSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSUBUSB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSUBUSW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPSUBW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPTESTMB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPTESTMW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPTESTNMB | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPTESTNMW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPUNPCKHBW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPUNPCKHWD | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPUNPCKLBW | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPUNPCKLWD | evex | 128/256/512 | Intel | AVX512BW | cannot: CPUID.7H:EBX[30] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U260-U269 EVEX (AVX512BW, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_bw.py; its byte/word arithmetic matches the … |
| VPBROADCASTMB2Q | evex | 128/256/512 | Intel | AVX512CD | cannot: CPUID.7H:EBX[28] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPBROADCASTMW2D | evex | 128/256/512 | Intel | AVX512CD | cannot: CPUID.7H:EBX[28] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPCONFLICTD | evex | 128/256/512 | Intel | AVX512CD | cannot: CPUID.7H:EBX[28] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPCONFLICTQ | evex | 128/256/512 | Intel | AVX512CD | cannot: CPUID.7H:EBX[28] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPLZCNTD | evex | 128/256/512 | Intel | AVX512CD | cannot: CPUID.7H:EBX[28] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPLZCNTQ | evex | 128/256/512 | Intel | AVX512CD | cannot: CPUID.7H:EBX[28] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| KADDB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KADDW | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KANDB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KANDNB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KMOVB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KNOTB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KORB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KORTESTB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KSHIFTLB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KSHIFTRB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KTESTB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KTESTW | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KXNORB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KXORB | vex | - | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| VANDNPD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VANDNPS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VANDPD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VANDPS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VBROADCASTF32X2 | evex | 256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTF32X8 | evex | 512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTF64X2 | evex | 256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTI32X2 | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTI32X8 | evex | 512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTI64X2 | evex | 256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VCVTPD2QQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTPD2UQQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTPS2QQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTPS2UQQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTQQ2PD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTQQ2PS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTTPD2QQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTTPD2UQQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTTPS2QQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTTPS2UQQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTUQQ2PD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VCVTUQQ2PS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U233 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + cases_evex_m2_cvt.txt "=>" … |
| VEXTRACTF32X8 | evex | 512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXTRACTF64X2 | evex | 256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXTRACTI32X8 | evex | 512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXTRACTI64X2 | evex | 256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VFPCLASSPD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VFPCLASSPS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VFPCLASSSD | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VFPCLASSSS | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VINSERTF32X8 | evex | 512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VINSERTF64X2 | evex | 256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VINSERTI32X8 | evex | 512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VINSERTI64X2 | evex | 256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VORPD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VORPS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VPEXTRD | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPEXTRQ | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPINSRD | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPINSRQ | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVD2M | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U800 EVEX AVX512DQ (VL 128/256/512, register forms): independent SDM-pseudocode model (ref_sysins.py) + unit tests … |
| VPMOVM2D | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U800 EVEX AVX512DQ (VL 128/256/512, register forms): independent SDM-pseudocode model (ref_sysins.py) + unit tests … |
| VPMOVM2Q | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U800 EVEX AVX512DQ (VL 128/256/512, register forms): independent SDM-pseudocode model (ref_sysins.py) + unit tests … |
| VPMOVQ2M | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U800 EVEX AVX512DQ (VL 128/256/512, register forms): independent SDM-pseudocode model (ref_sysins.py) + unit tests … |
| VPMULLQ | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VRANGEPD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VRANGEPS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VRANGESD | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U295-U296 EVEX (AVX512DQ, LLIG): independent SDM-pseudocode model (ref_evex_m3_dq.py) + cases_evex_m3_dq.txt and … |
| VRANGESS | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U295-U296 EVEX (AVX512DQ, LLIG): independent SDM-pseudocode model (ref_evex_m3_dq.py) + cases_evex_m3_dq.txt and … |
| VREDUCEPD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VREDUCEPS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VREDUCESD | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U295-U296 EVEX (AVX512DQ, LLIG): independent SDM-pseudocode model (ref_evex_m3_dq.py) + cases_evex_m3_dq.txt and … |
| VREDUCESS | evex | 128 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U295-U296 EVEX (AVX512DQ, LLIG): independent SDM-pseudocode model (ref_evex_m3_dq.py) + cases_evex_m3_dq.txt and … |
| VXORPD | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VXORPS | evex | 128/256/512 | Intel | AVX512DQ | cannot: CPUID.7H:EBX[17] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U290-U294 EVEX (AVX512DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m3_dq.py; its bitwise / int64<->FP rounding and … |
| VEXP2PD | evex | 512 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VEXP2PS | evex | 512 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VRCP28PD | evex | 512 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VRCP28PS | evex | 512 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VRCP28SD | evex | 128 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VRCP28SS | evex | 128 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VRSQRT28PD | evex | 512 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VRSQRT28PS | evex | 512 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VRSQRT28SD | evex | 128 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| VRSQRT28SS | evex | 128 | Intel | AVX512ER | cannot: CPUID.7H:EBX[27] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U994/U1247/U1248 AVX512ER (SDM 092 Vol2D 8-8..8-35; user decisions 2026-10-11): status documented compliance (bound … |
| KANDNW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KANDW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KMOVW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KNOTW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KORTESTW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KORW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KSHIFTLW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KSHIFTRW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KUNPCKBW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KXNORW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| KXORW | vex | - | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U129-U133 opmask (VEX, AVX512F/DQ/BW): independent SDM-pseudocode model (ref_opmask.py) + unit tests + cases_opmask.txt "=>" … |
| VADDPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VADDPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VADDSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VADDSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VALIGND | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VALIGNQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBLENDMPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VBLENDMPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VBROADCASTF32X4 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTF64X4 | evex | 512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTI32X4 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTI64X4 | evex | 512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VBROADCASTSD | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VBROADCASTSS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VCMPEQ_OSPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_OSPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_OSSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_OSSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_UQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_UQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_UQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_UQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_USPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_USPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_USSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQ_USSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPEQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPFALSE_OSPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPFALSE_OSPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPFALSE_OSSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPFALSE_OSSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPFALSEPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPFALSEPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPFALSESD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPFALSESS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGE_OQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGE_OQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGE_OQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGE_OQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGEPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGEPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGESD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGESS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGT_OQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGT_OQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGT_OQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGT_OQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGTPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGTPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGTSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPGTSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLE_OQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLE_OQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLE_OQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLE_OQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLEPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLEPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLESD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLESS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLT_OQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLT_OQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLT_OQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLT_OQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLTPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLTPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLTSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPLTSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_OQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_OQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_OQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_OQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_OSPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_OSPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_OSSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_OSSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_USPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_USPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_USSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQ_USSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNEQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGE_UQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGE_UQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGE_UQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGE_UQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGEPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGEPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGESD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGESS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGT_UQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGT_UQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGT_UQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGT_UQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGTPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGTPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGTSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNGTSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLE_UQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLE_UQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLE_UQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLE_UQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLEPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLEPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLESD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLESS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLT_UQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLT_UQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLT_UQSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLT_UQSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLTPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLTPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLTSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPNLTSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPORD_SPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPORD_SPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPORD_SSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPORD_SSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPORDPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPORDPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPORDSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPORDSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPTRUE_USPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPTRUE_USPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPTRUE_USSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPTRUE_USSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPTRUEPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPTRUEPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPTRUESD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPTRUESS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPUNORD_SPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPUNORD_SPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPUNORD_SSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPUNORD_SSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPUNORDPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPUNORDPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPUNORDSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCMPUNORDSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCOMISD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCOMISS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VCOMPRESSPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VCOMPRESSPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VCVTDQ2PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTDQ2PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTPD2DQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTPD2PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTPD2UDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTPH2PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTPS2DQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTPS2PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTPS2PH | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTPS2UDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTSD2SI | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTSD2SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTSD2USI | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTSI2SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTSI2SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTSS2SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTSS2SI | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTSS2USI | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTTPD2DQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTTPD2UDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTTPS2DQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTTPS2UDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTTSD2SI | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTTSD2USI | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTTSS2SI | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTTSS2USI | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTUDQ2PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTUDQ2PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTUSI2SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VCVTUSI2SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VDIVPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VDIVPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VDIVSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VDIVSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VEXPANDPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXPANDPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXTRACTF32X4 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXTRACTF64X4 | evex | 512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXTRACTI32X4 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXTRACTI64X4 | evex | 512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VEXTRACTPS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VFIXUPIMMPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VFIXUPIMMPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VFIXUPIMMSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VFIXUPIMMSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VFMADD132PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD132PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD132SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD132SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD213PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD213PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD213SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD213SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD231PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD231PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD231SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADD231SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADDSUB132PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADDSUB132PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADDSUB213PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADDSUB213PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADDSUB231PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMADDSUB231PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB132PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB132PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB132SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB132SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB213PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB213PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB213SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB213SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB231PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB231PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB231SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUB231SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUBADD132PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUBADD132PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUBADD213PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUBADD213PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUBADD231PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFMSUBADD231PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD132PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD132PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD132SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD132SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD213PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD213PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD213SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD213SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD231PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD231PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD231SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMADD231SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB132PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB132PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB132SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB132SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB213PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB213PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB213SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB213SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB231PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB231PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB231SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VFNMSUB231SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VGATHERDPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VGATHERDPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VGATHERQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VGATHERQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VGETEXPPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VGETEXPPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VGETEXPSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VGETEXPSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VGETMANTPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VGETMANTPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VGETMANTSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VGETMANTSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VINSERTF32X4 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VINSERTF64X4 | evex | 512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VINSERTI32X4 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VINSERTI64X4 | evex | 512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VINSERTPS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMAXPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMAXPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMAXSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VMAXSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VMINPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMINPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMINSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VMINSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VMOVAPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMOVAPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMOVD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest; CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ⬜ not implemented yet | not implemented (4 forms #UD; the i5-13600K lacks it) |
| VMOVDDUP | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVDQA32 | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMOVDQA64 | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMOVDQU32 | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMOVDQU64 | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMOVHLPS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVHPD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVHPS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVLHPS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVLPD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVLPS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVNTDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVNTDQA | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVNTPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVNTPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVQ | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (5 forms #UD; the i5-13600K lacks it) |
| VMOVSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VMOVSHDUP | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVSLDUP | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VMOVSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VMOVUPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMOVUPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMULPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMULPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VMULSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VMULSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VPABSD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPABSQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPADDD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPADDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPANDD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPANDND | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPANDNQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPANDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPBLENDMD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VPBLENDMQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VPBROADCASTD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPBROADCASTQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCMPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCMPEQD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCMPEQQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCMPGTD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCMPGTQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCMPQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCMPUD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCMPUQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPCOMPRESSD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPCOMPRESSQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMD | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMI2D | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMI2PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMI2PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMI2Q | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMILPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMILPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMPD | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMPS | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMQ | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMT2D | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMT2PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMT2PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPERMT2Q | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPEXPANDD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPEXPANDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPGATHERDD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VPGATHERDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VPGATHERQD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VPGATHERQQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VPMAXSD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMAXSQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMAXUD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMAXUQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMINSD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMINSQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMINUD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMINUQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMOVDB | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVDW | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVQB | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVQD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVQW | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSDB | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSDW | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSQB | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSQD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSQW | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSXBD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSXBQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSXDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSXWD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVSXWQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVUSDB | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVUSDW | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVUSQB | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVUSQD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVUSQW | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVZXBD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVZXBQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVZXDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVZXWD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMOVZXWQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPMULDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMULLD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPMULUDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPORD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPORQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPROLD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPROLQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPROLVD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPROLVQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPRORD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPRORQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPRORVD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPRORVQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPSCATTERDD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VPSCATTERDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VPSCATTERQD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VPSCATTERQQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VPSHUFD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPSLLD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPSLLQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPSLLVD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPSLLVQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPSRAD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPSRAQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPSRAVD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPSRAVQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPSRLD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPSRLQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VPSRLVD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPSRLVQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPSUBD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPSUBQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPTERNLOGD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPTERNLOGQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPTESTMD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPTESTMQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPTESTNMD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPTESTNMQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPUNPCKHDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPUNPCKHQDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPUNPCKLDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPUNPCKLQDQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VPXORD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VPXORQ | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VRCP14PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1245/U1246 (user decision 2026-10-11; AMB-2, A9): status documented compliance (Table 5-24, Vol2C 5-674: the bound … |
| VRCP14PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1245/U1246 (user decision 2026-10-11; AMB-2, A9): status documented compliance (Table 5-25, SDM 092 Vol2C 5-676 … |
| VRCP14SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1245/U1246 (user decision 2026-10-11; AMB-2, A9): status documented compliance (Table 5-24, Vol2C 5-678: the bound … |
| VRCP14SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1245/U1246 (user decision 2026-10-11; AMB-2, A9): status documented compliance (Table 5-25, Vol2C 5-680: the bound … |
| VRNDSCALEPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VRNDSCALEPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VRNDSCALESD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VRNDSCALESS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VRSQRT14PD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1245/U1246 (user decision 2026-10-11; AMB-2, A9): status documented compliance (Table 5-32, Vol2C 5-714: the bound … |
| VRSQRT14PS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1245/U1246 (user decision 2026-10-11; AMB-2, A9): status documented compliance (Table 5-33, Vol2C 5-716: the bound … |
| VRSQRT14SD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1245/U1246 (user decision 2026-10-11; AMB-2, A9): status documented compliance (Table 5-34, Vol2C 5-718: the bound … |
| VRSQRT14SS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1245/U1246 (user decision 2026-10-11; AMB-2, A9): status documented compliance (Table 5-35, Vol2C 5-720: the bound … |
| VSCALEFPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VSCALEFPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VSCALEFSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VSCALEFSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U230-U241 EVEX (AVX512F, VL 128/256/512; scalar LIG): independent SDM-pseudocode model (ref_evex_m2_cvt.py) + unit tests + … |
| VSCATTERDPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VSCATTERDPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VSCATTERQPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VSCATTERQPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U250-U251 EVEX (AVX512F, VL 128/256/512, class E12): independent SDM-pseudocode model (ref_evex_m2_gather.py) + unit tests (restart … |
| VSHUFF32X4 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VSHUFF64X2 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VSHUFI32X4 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VSHUFI64X2 | evex | 256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VSHUFPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VSHUFPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VSQRTPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VSQRTPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VSQRTSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VSQRTSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VSUBPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VSUBPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U141-U159 EVEX (AVX512F, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m1.py; its floating point matches the i5-13600K … |
| VSUBSD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VSUBSS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VUCOMISD | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VUCOMISS | evex | 128 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U190-U201 EVEX (AVX512F; scalar LIG / VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_engine.py; its floating point … |
| VUNPCKHPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VUNPCKHPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VUNPCKLPD | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VUNPCKLPS | evex | 128/256/512 | Intel | AVX512F | cannot: CPUID.7H:EBX[16] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U210-U215 EVEX (AVX512F/DQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m2_perm.py) + unit tests + … |
| VGATHERPF0DPD | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VGATHERPF0DPS | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VGATHERPF0QPD | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VGATHERPF0QPS | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VGATHERPF1DPD | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VGATHERPF1DPS | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VGATHERPF1QPD | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VGATHERPF1QPS | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VSCATTERPF0DPD | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VSCATTERPF0DPS | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VSCATTERPF0QPD | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VSCATTERPF0QPS | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VSCATTERPF1DPD | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VSCATTERPF1DPS | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VSCATTERPF1QPD | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| VSCATTERPF1QPS | evex | 512 | Intel | AVX512PF | cannot: CPUID.7H:EBX[26] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U993 AVX512PF (EVEX.512.66.0F38 C6/C7 /1 /2 /5 /6, SDM 092 Vol2D 8-12..8-15, 8-36..8-39, E12NP): prefetch hint without architectural … |
| V4FMADDPS | evex | 512 | Intel | AVX512_4FMAPS | cannot: CPUID.7H:EDX[3] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U992 AVX512_4FMAPS (EVEX.F2.0F38.W0 9A/AA 512, 9B/AB LLIG, SDM 092 Vol2D 8-4..8-7, Type E2): independent SDM-pseudocode model … |
| V4FMADDSS | evex | 128 | Intel | AVX512_4FMAPS | cannot: CPUID.7H:EDX[3] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U992 AVX512_4FMAPS (EVEX.F2.0F38.W0 9A/AA 512, 9B/AB LLIG, SDM 092 Vol2D 8-4..8-7, Type E2): independent SDM-pseudocode model … |
| V4FNMADDPS | evex | 512 | Intel | AVX512_4FMAPS | cannot: CPUID.7H:EDX[3] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U992 AVX512_4FMAPS (EVEX.F2.0F38.W0 9A/AA 512, 9B/AB LLIG, SDM 092 Vol2D 8-4..8-7, Type E2): independent SDM-pseudocode model … |
| V4FNMADDSS | evex | 128 | Intel | AVX512_4FMAPS | cannot: CPUID.7H:EDX[3] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U992 AVX512_4FMAPS (EVEX.F2.0F38.W0 9A/AA 512, 9B/AB LLIG, SDM 092 Vol2D 8-4..8-7, Type E2): independent SDM-pseudocode model … |
| VP4DPWSSD | evex | 512 | Intel | AVX512_4VNNIW | cannot: CPUID.7H:EDX[2] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U991 AVX512_4VNNIW (EVEX.512.F2.0F38.W0 52/53, SDM 092 Vol2D 8-16..8-19): independent SDM-pseudocode model (ref_xeonphi.py) + unit test … |
| VP4DPWSSDS | evex | 512 | Intel | AVX512_4VNNIW | cannot: CPUID.7H:EDX[2] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U991 AVX512_4VNNIW (EVEX.512.F2.0F38.W0 52/53, SDM 092 Vol2D 8-16..8-19): independent SDM-pseudocode model (ref_xeonphi.py) + unit test … |
| VCVTNE2PS2BF16 | evex | 128/256/512 | Intel | AVX512_BF16 | cannot: CPUID.7H.1:EAX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U556 EVEX AVX512_BF16 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VCVTNEPS2BF16 | evex | 128/256/512 | Intel | AVX512_BF16 | cannot: CPUID.7H.1:EAX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U556 EVEX AVX512_BF16 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VDPBF16PS | evex | 128/256/512 | Intel | AVX512_BF16 | cannot: CPUID.7H.1:EAX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U557 EVEX AVX512_BF16 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py, exact FMA steps with DAZ/FTZ/RNE) == … |
| VPOPCNTB | evex | 128/256/512 | Intel | AVX512_BITALG | cannot: CPUID.7H:ECX[12] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPOPCNTW | evex | 128/256/512 | Intel | AVX512_BITALG | cannot: CPUID.7H:ECX[12] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPSHUFBITQMB | evex | 128/256/512 | Intel | AVX512_BITALG | cannot: CPUID.7H:ECX[12] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VCOMXSD | evex | 128 | Intel | AVX512_COM_EF | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCOMXSH | evex | 128 | Intel | AVX512_COM_EF | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCOMXSS | evex | 128 | Intel | AVX512_COM_EF | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VUCOMXSD | evex | 128 | Intel | AVX512_COM_EF | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VUCOMXSH | evex | 128 | Intel | AVX512_COM_EF | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VUCOMXSS | evex | 128 | Intel | AVX512_COM_EF | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VADDPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VADDSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPEQ_OSPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPEQ_OSSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPEQ_UQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPEQ_UQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPEQ_USPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPEQ_USSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPEQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPEQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPFALSE_OSPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPFALSE_OSSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPFALSEPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPFALSESH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPGE_OQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPGE_OQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPGEPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPGESH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPGT_OQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPGT_OQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPGTPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPGTSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPLE_OQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPLE_OQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPLEPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPLESH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPLT_OQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPLT_OQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPLTPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPLTSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNEQ_OQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNEQ_OQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNEQ_OSPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNEQ_OSSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNEQ_USPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNEQ_USSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNEQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNEQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNGE_UQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNGE_UQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNGEPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNGESH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNGT_UQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNGT_UQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNGTPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNGTSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNLE_UQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNLE_UQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNLEPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNLESH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNLT_UQPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNLT_UQSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNLTPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPNLTSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPORD_SPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPORD_SSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPORDPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPORDSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPTRUE_USPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPTRUE_USSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPTRUEPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPTRUESH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPUNORD_SPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPUNORD_SSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPUNORDPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCMPUNORDSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCOMISH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTDQ2PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPD2PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPH2DQ | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPH2PD | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPH2PSX | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPH2QQ | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPH2UDQ | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPH2UQQ | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPH2UW | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPH2W | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTPS2PHX | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTQQ2PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTSD2SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTSH2SD | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTSH2SI | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTSH2SS | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTSH2USI | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTSI2SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTSS2SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTTPH2DQ | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTTPH2QQ | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTTPH2UDQ | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTTPH2UQQ | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTTPH2UW | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTTPH2W | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTTSH2SI | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTTSH2USI | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTUDQ2PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTUQQ2PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTUSI2SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTUW2PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVTW2PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VDIVPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VDIVSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFCMADDCPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFCMADDCSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFCMULCPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFCMULCSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADD132PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADD132SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADD213PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADD213SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADD231PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADD231SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADDCPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADDCSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADDSUB132PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADDSUB213PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMADDSUB231PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUB132PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUB132SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUB213PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUB213SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUB231PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUB231SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUBADD132PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUBADD213PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMSUBADD231PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMULCPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFMULCSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMADD132PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMADD132SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMADD213PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMADD213SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMADD231PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMADD231SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMSUB132PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMSUB132SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMSUB213PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMSUB213SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMSUB231PH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFNMSUB231SH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFPCLASSPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VFPCLASSSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VGETEXPPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VGETEXPSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VGETMANTPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VGETMANTSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VMAXPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VMAXSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VMINPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VMINSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VMOVSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VMOVW | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest; CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VMULPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VMULSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VRCPPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1243 AMB-2 (decisions A9 2026-10-09 / AMB-2 2026-10-10): documented compliance, NOT bit-exact - the analytic … |
| VRCPSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1243 AMB-2 (decisions A9 2026-10-09 / AMB-2 2026-10-10): documented compliance, NOT bit-exact - the analytic … |
| VREDUCEPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VREDUCESH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VRNDSCALEPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VRNDSCALESH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VRSQRTPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1243 AMB-2 (decisions A9 2026-10-09 / AMB-2 2026-10-10): documented compliance, NOT bit-exact - the analytic … |
| VRSQRTSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | documented bound, not bit-exact: U1243 AMB-2 (decisions A9 2026-10-09 / AMB-2 2026-10-10): documented compliance, NOT bit-exact - the analytic … |
| VSCALEFPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VSCALEFSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VSQRTPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VSQRTSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VSUBPH | evex | 128/256/512 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VSUBSH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VUCOMISH | evex | 128 | Intel | AVX512_FP16 | cannot: CPUID.7H:EDX[23] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U330-U339 AVX512-FP16 (maps 5/6 + NP/F3 0F3A, VL 128/256/512): independent SDM-pseudocode model (ref_evex_fp16.py; its FP32<->FP16 core … |
| VCVT2PS2PHX | evex | 128/256/512 | Intel | AVX512_FP16_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U404 AVX10.2 VCVT2PS2PHX (VL 128/256/512, {er}): independent model ref_avx10_b.py (IEEE rationals, MXCSR flags) + cases_avx10_b.txt … |
| VCVT2PH2BF8 | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVT2PH2BF8S | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVT2PH2HF8 | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVT2PH2HF8S | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVTBIASPH2BF8 | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVTBIASPH2BF8S | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVTBIASPH2HF8 | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVTBIASPH2HF8S | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVTHF82PH | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U403 AVX10.2 VCVTHF82PH (VL 128/256/512): independent model ref_avx10_b.py (all 256 E4M3 inputs) + unit tests + cases_avx10_b.txt "=>" … |
| VCVTPH2BF8 | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVTPH2BF8S | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVTPH2HF8 | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VCVTPH2HF8S | evex | 128/256/512 | Intel | AVX512_FP8_CONVERT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U402 AVX10.2 FP16->FP8 (VL 128/256/512): independent model ref_avx10_b.py (exact rationals; = the spec 5.1 pseudocode on all 65536 FP16 … |
| VGF2P8AFFINEINVQB | evex | 128/256/512 | Intel | AVX512_GFNI | cannot: AVX512_GFNI not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VGF2P8AFFINEQB | evex | 128/256/512 | Intel | AVX512_GFNI | cannot: AVX512_GFNI not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VGF2P8MULB | evex | 128/256/512 | Intel | AVX512_GFNI | cannot: AVX512_GFNI not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VPMADD52HUQ | evex | 128/256/512 | Intel | AVX512_IFMA | cannot: CPUID.7H:EBX[21] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPMADD52LUQ | evex | 128/256/512 | Intel | AVX512_IFMA | cannot: CPUID.7H:EBX[21] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VMPSADBW | evex | 128/256/512 | Intel | AVX512_MEDIAX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U408 AVX10.2 EVEX VMPSADBW (VL 128/256/512): independent model ref_avx10_b.py (spec 10.2 pseudocode) + cases_avx10_b.txt "=>" … |
| VMINMAXBF16 | evex | 128/256/512 | Intel | AVX512_MINMAX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMINMAXPD | evex | 128/256/512 | Intel | AVX512_MINMAX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMINMAXPH | evex | 128/256/512 | Intel | AVX512_MINMAX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMINMAXPS | evex | 128/256/512 | Intel | AVX512_MINMAX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMINMAXSD | evex | 128 | Intel | AVX512_MINMAX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMINMAXSH | evex | 128 | Intel | AVX512_MINMAX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VMINMAXSS | evex | 128 | Intel | AVX512_MINMAX | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTBF162IBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTBF162IUBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTPH2IBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTPH2IUBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTPS2IBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTPS2IUBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTBF162IBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTBF162IUBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPH2IBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPH2IUBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPS2IBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPS2IUBS | evex | 128/256/512 | Intel | AVX512_SAT_CVT | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPD2DQS | evex | 128/256/512 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPD2QQS | evex | 128/256/512 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPD2UDQS | evex | 128/256/512 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPD2UQQS | evex | 128/256/512 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPS2DQS | evex | 128/256/512 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPS2QQS | evex | 128/256/512 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPS2UDQS | evex | 128/256/512 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTPS2UQQS | evex | 128/256/512 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTSD2SIS | evex | 128 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTSD2USIS | evex | 128 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTSS2SIS | evex | 128 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VCVTTSS2USIS | evex | 128 | Intel | AVX512_SAT_CVT_DS | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U372-U376 AVX10.2 (maps 5/6 + 0F/0F3A, VL 128/256/512 or LLIG): independent AVX10.2-spec model (ref_avx10_a.py, spec 361050-007) + unit … |
| VAESDEC | evex | 128/256/512 | Intel | AVX512_VAES | cannot: AVX512_VAES not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VAESDECLAST | evex | 128/256/512 | Intel | AVX512_VAES | cannot: AVX512_VAES not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VAESENC | evex | 128/256/512 | Intel | AVX512_VAES | cannot: AVX512_VAES not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VAESENCLAST | evex | 128/256/512 | Intel | AVX512_VAES | cannot: AVX512_VAES not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VPERMB | evex | 128/256/512 | Intel | AVX512_VBMI | cannot: CPUID.7H:ECX[1] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPERMI2B | evex | 128/256/512 | Intel | AVX512_VBMI | cannot: CPUID.7H:ECX[1] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPERMT2B | evex | 128/256/512 | Intel | AVX512_VBMI | cannot: CPUID.7H:ECX[1] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPMULTISHIFTQB | evex | 128/256/512 | Intel | AVX512_VBMI | cannot: CPUID.7H:ECX[1] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U554 EVEX AVX512_VBMI (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPCOMPRESSB | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U551 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPCOMPRESSW | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U551 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPEXPANDB | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U551 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPEXPANDW | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U551 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHLDD | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHLDQ | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHLDVD | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHLDVQ | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHLDVW | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHLDW | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHRDD | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHRDQ | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHRDVD | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHRDVQ | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHRDVW | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPSHRDW | evex | 128/256/512 | Intel | AVX512_VBMI2 | cannot: CPUID.7H:ECX[6] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U550-U553 EVEX AVX512_VBMI2 (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) + unit tests (test_x86_m4a_*) + … |
| VPDPBUSD | evex | 128/256/512 | Intel | AVX512_VNNI | cannot: CPUID.7H:ECX[11] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U555 EVEX AVX512_VNNI (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) == i5-13600K VEX AVX-VNNI on 240 hardware … |
| VPDPBUSDS | evex | 128/256/512 | Intel | AVX512_VNNI | cannot: CPUID.7H:ECX[11] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U555 EVEX AVX512_VNNI (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) == i5-13600K VEX AVX-VNNI on 240 hardware … |
| VPDPWSSD | evex | 128/256/512 | Intel | AVX512_VNNI | cannot: CPUID.7H:ECX[11] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U555 EVEX AVX512_VNNI (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) == i5-13600K VEX AVX-VNNI on 240 hardware … |
| VPDPWSSDS | evex | 128/256/512 | Intel | AVX512_VNNI | cannot: CPUID.7H:ECX[11] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U555 EVEX AVX512_VNNI (VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4a.py) == i5-13600K VEX AVX-VNNI on 240 hardware … |
| VDPPHPS | evex | 128/256/512 | Intel | AVX512_VNNI_FP16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 | ❌ · ✅ per manual | SDM vectors: U407 AVX10.2 VDPPHPS (VL 128/256/512): independent model ref_avx10_b.py (fma32 of ref_amx.py) + cases_avx10_b.txt "=>" (--avx10) … |
| VPDPWSUD | evex | 128/256/512 | Intel | AVX512_VNNI_INT16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPWSUDS | evex | 128/256/512 | Intel | AVX512_VNNI_INT16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPWUSD | evex | 128/256/512 | Intel | AVX512_VNNI_INT16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPWUSDS | evex | 128/256/512 | Intel | AVX512_VNNI_INT16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPWUUD | evex | 128/256/512 | Intel | AVX512_VNNI_INT16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPWUUDS | evex | 128/256/512 | Intel | AVX512_VNNI_INT16 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPBSSD | evex | 128/256/512 | Intel | AVX512_VNNI_INT8 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPBSSDS | evex | 128/256/512 | Intel | AVX512_VNNI_INT8 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPBSUD | evex | 128/256/512 | Intel | AVX512_VNNI_INT8 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPBSUDS | evex | 128/256/512 | Intel | AVX512_VNNI_INT8 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPBUUD | evex | 128/256/512 | Intel | AVX512_VNNI_INT8 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VPDPBUUDS | evex | 128/256/512 | Intel | AVX512_VNNI_INT8 | cannot: CPUID.7H.1:EDX[19] (AVX10) = 0 on --host and --guest; the instruction needs AVX10.2 OR AVX10_V1_AUX | ❌ · ✅ per manual | SDM vectors: U405/U406 AVX10.2 EVEX VNNI (VL 128/256/512): model ref_vnni_ifma_ne.py via ref_avx10_b.py, equal to the i5-13600K VEX AVX-VNNI on 600 … |
| VP2INTERSECTD | evex | 128/256/512 | Intel | AVX512_VP2INTERSECT | cannot: CPUID.7H:EDX[8] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VP2INTERSECTQ | evex | 128/256/512 | Intel | AVX512_VP2INTERSECT | cannot: CPUID.7H:EDX[8] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VPCLMULQDQ | evex | 128/256/512 | Intel | AVX512_VPCLMULQDQ | cannot: AVX512_VPCLMULQDQ not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U570-U574 EVEX (AVX512_VP2INTERSECT / GFNI / VAES / VPCLMULQDQ, VL 128/256/512): independent SDM-pseudocode model (ref_evex_m4b.py) + … |
| VPOPCNTD | evex | 128/256/512 | Intel | AVX512_VPOPCNTDQ | cannot: CPUID.7H:ECX[14] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |
| VPOPCNTQ | evex | 128/256/512 | Intel | AVX512_VPOPCNTDQ | cannot: CPUID.7H:ECX[14] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U320-U325 EVEX (AVX512CD / AVX512_IFMA / AVX512_VPOPCNTDQ / AVX512_BITALG / AVX512_VBMI, VL 128/256/512): independent SDM-pseudocode … |

## Kernel mode required

93 forms that cannot execute at CPL3 under Windows (Windows runs user code at CPL3 with IOPL = 0 and CR4.PCE = 0); **Why kernel** gives the reason and its
evidence: the SDM exception list (093 single-volume PDFs, printed page), the hardware cases that fault at CPL3 on the i5-13600K
(`Emulator/data/cases_lockpriv_hw.txt`: 13 CPL0-only instructions -> #GP, G26) and the emulator's own check (`isa_privilege.tsv` columns 6-7).

### By family (kernel mode)

| family | vendor | forms | ✅ | ⏳ | ⬜ | ❌ | i5-13600K cannot run it because |
|---|---|---|---|---|---|---|---|
| AMD | both | 1 |  | 1 |  |  |  |
| AMD_INVLPGB | AMD | 2 |  |  |  | 2 | AMD INVLPGB, on --host and --guest |
| CET | Intel | 4 |  |  |  | 4 | the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host … |
| ENQCMD | Intel | 1 |  |  |  | 1 | CPUID.7H:ECX[29] = 0 on --host and --guest |
| FRED | Intel | 2 |  |  |  | 2 | CPUID.7H.1:EAX[17] = 0 on --host and --guest |
| HRESET | Intel | 1 |  |  |  | 1 | --guest: CPUID.7H.1:EAX[22] = 0; --host: enumerated (CPUID.7H.1:EAX[22] = 1 on the bare host) |
| I186 | Intel | 4 |  | 4 |  |  |  |
| I286PROTECTED | Intel | 2 |  | 2 |  |  |  |
| I286REAL | Intel | 4 |  | 4 |  |  |  |
| I386 | Intel | 2 |  | 2 |  |  |  |
| I486REAL | Intel | 3 |  | 3 |  |  |  |
| I86 | Intel | 5 |  | 5 |  |  |  |
| INVPCID | Intel | 1 | 1 |  |  |  |  |
| KEYLOCKER | Intel | 1 |  |  |  | 1 | --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] … |
| LKGS | Intel | 1 |  |  |  | 1 | CPUID.7H.1:EAX[18] = 0 on --host and --guest |
| LONGMODE | Intel | 2 |  | 2 |  |  |  |
| MONITOR | Intel | 2 |  |  |  | 2 | --guest: CPUID.1H:ECX[3] = 0; --host: enumerated (CPUID.1H:ECX[3] = 1 on the bare host) |
| MSRLIST | Intel | 2 |  |  |  | 2 | CPUID.7H.1:EAX[27] = 0 on --host and --guest |
| MSR_IMM | Intel | 2 |  |  |  | 2 | CPUID.7H.1:ECX[5] = 0 on --host and --guest |
| PBNDKB | Intel | 1 |  |  |  | 1 | CPUID.7H.1:EBX[1] = 0 on --host and --guest |
| PCONFIG | Intel | 1 |  |  |  | 1 | --guest: CPUID.7H:EDX[18] = 0; --host: enumerated (CPUID.7H:EDX[18] = 1 on the bare host) |
| PENTIUMREAL | Intel | 2 |  | 2 |  |  |  |
| RDPMC | Intel | 1 |  | 1 |  |  |  |
| SEP | Intel | 2 |  | 2 |  |  |  |
| SGX | Intel | 1 |  |  |  | 1 | CPUID.7H:EBX[2] = 0 on --host and --guest |
| SGX_ENCLV | Intel | 1 |  |  |  | 1 | CPUID.7H:EBX[2] = 0 on --host and --guest |
| SMAP | Intel | 2 |  | 2 |  |  |  |
| SNP | AMD | 4 |  |  |  | 4 | AMD SEV-SNP, on --host and --guest |
| SVM | AMD | 6 |  |  |  | 6 | AMD SVM, on --host and --guest |
| TDX | Intel | 3 |  |  |  | 3 | Intel TDX / SEAM (server CPUs); SEAMCALL: VMX root operation, 64-bit mode (VMX non-root: VM exit 76), on … (+2 more) |
| VTX | Intel | 12 | 12 |  |  |  |  |
| WBNOINVD | Intel | 1 |  |  |  | 1 | CPUID.80000008H:EBX[9] = 0 on --host and --guest |
| WRMSRNS | Intel | 1 |  |  |  | 1 | CPUID.7H.1:EAX[19] = 0 on --host and --guest |
| XSAVE | Intel | 1 |  | 1 |  |  |  |
| XSAVES | Intel | 4 |  | 4 |  |  |  |
| APX_F_CET | Intel | 2 |  |  |  | 2 | APX_F_CET not reported by this CPU, on --host and --guest |
| APX_F_ENQCMD | Intel | 1 |  |  |  | 1 | APX_F_ENQCMD not reported by this CPU, on --host and --guest |
| APX_F_INVPCID | Intel | 1 |  |  |  | 1 | APX_F_INVPCID not reported by this CPU, on --host and --guest |
| APX_F_MSR_IMM | Intel | 2 |  |  |  | 2 | APX_F_MSR_IMM not reported by this CPU, on --host and --guest |
| APX_F_VMX | Intel | 2 |  |  |  | 2 | APX_F_VMX not reported by this CPU, on --host and --guest |

### Kernel-mode forms (93)

| instruction | encoding | vector bits | vendor | family | Why kernel | i5-13600K | Status | verified by |
|---|---|---|---|---|---|---|---|---|
| SYSRET | legacy | - | both | AMD | CPL0 · SDM Vol2B 4-711 · hw: cases_backport_t1.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| INVLPGB | legacy | - | AMD | AMD_INVLPGB | CPL0 (AMD) · XED CPL 0 | cannot: AMD INVLPGB, on --host and --guest | ❌ · ⬜ not implemented yet | CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not verified yet (CPL3 fault: Phase 3, D6) |
| TLBSYNC | legacy | - | AMD | AMD_INVLPGB | CPL0 (AMD) · XED CPL 0 | cannot: AMD INVLPGB, on --host and --guest | ❌ · ⬜ not implemented yet | CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not verified yet (CPL3 fault: Phase 3, D6) |
| CLRSSBSY | legacy | - | Intel | CET | CPL0 · SDM Vol2A 3-151 | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ✅ per manual | SDM vectors: U114 CET shadow stack: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU; CPL3 fault … |
| SETSSBSY | legacy | - | Intel | CET | CPL0 · SDM Vol2B 4-626 | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ✅ per manual | SDM vectors: U114 CET shadow stack: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile == CPU; CPL3 fault … |
| WRUSSD | legacy | - | Intel | CET | CPL0 · SDM Vol2D 6-20 | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ⏳ open item | CPL0 instruction (cases_reach): CPL3 fault in Phase 3 (D6): 66 [REX.W] 0F 38 F5: CPU #GP(0) at CPL3 (CR4.CET = 1 under Windows), Unicorn #UD at the … |
| WRUSSQ | legacy | - | Intel | CET | CPL0 · SDM Vol2D 6-20 | cannot: the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode, on --host and --guest; CET_IBT CPUID.7H:EDX[20] = 1 on --host, 0 on --guest | ❌ · ⏳ open item | CPL0 instruction (cases_reach): CPL3 fault in Phase 3 (D6): 66 [REX.W] 0F 38 F5: CPU #GP(0) at CPL3 (CR4.CET = 1 under Windows), Unicorn #UD at the … |
| ENQCMDS | legacy | - | Intel | ENQCMD | CPL0 · SDM Vol2A 3-293 | cannot: CPUID.7H:ECX[29] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U112 ENQCMDS: SDM-pseudocode expectations + unit test test_x86_enqcmd (ZF = 1 retry, nothing written; #GP for source bits 30:20 and at … |
| ERETS | legacy | - | Intel | FRED | CPL0 (#UD at CPL > 0) · SDM Vol2A 3-300 | cannot: CPUID.7H.1:EAX[17] = 0 on --host and --guest | ❌ · ✅ gating only | gating (CPUID + #UD, --host and --guest): GATING ONLY, FRED (G10 section 3): FRED not enumerated on either profile (CPUID.(07H,1):EAX[17] = 0) … |
| ERETU | legacy | - | Intel | FRED | CPL0 (#UD at CPL > 0) · SDM Vol2A 3-304 | cannot: CPUID.7H.1:EAX[17] = 0 on --host and --guest | ❌ · ✅ gating only | gating (CPUID + #UD, --host and --guest): GATING ONLY, FRED (G10 section 3): FRED not enumerated on either profile (CPUID.(07H,1):EAX[17] = 0) … |
| HRESET | legacy | - | Intel | HRESET | CPL0 · SDM Vol2A 3-440 | cannot: --guest: CPUID.7H.1:EAX[22] = 0; --host: enumerated (CPUID.7H.1:EAX[22] = 1 on the bare host) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U804 HRESET (F3 0F 3A F0 C0 ib, CPUID.(7,1):EAX[22] + leaf 20H EBX[0] THREAD_DIRECTOR_HRESET on the MAX model … |
| INSB | legacy | - | Intel | I186 | IOPL + TSS I/O bitmap (Windows IOPL = 0) · SDM Vol2A 3-466 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| INSW | legacy | - | Intel | I186 | IOPL + TSS I/O bitmap (Windows IOPL = 0) · SDM Vol2A 3-466 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| OUTSB | legacy | - | Intel | I186 | IOPL + TSS I/O bitmap (Windows IOPL = 0) · SDM Vol2B 4-176 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| OUTSW | legacy | - | Intel | I186 | IOPL + TSS I/O bitmap (Windows IOPL = 0) · SDM Vol2B 4-176 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| LLDT | legacy | - | Intel | I286PROTECTED | CPL0 · SDM Vol2A 3-559 | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| LTR | legacy | - | Intel | I286PROTECTED | CPL0 · SDM Vol2A 3-577 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| CLTS | legacy | - | Intel | I286REAL | CPL0 · SDM Vol2A 3-152 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| LGDT | legacy | - | Intel | I286REAL | CPL0 · SDM Vol2A 3-555 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| LIDT | legacy | - | Intel | I286REAL | CPL0 · SDM Vol2A 3-555 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| LMSW | legacy | - | Intel | I286REAL | CPL0 · SDM Vol2A 3-561 | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| INSD | legacy | - | Intel | I386 | IOPL + TSS I/O bitmap (Windows IOPL = 0) · SDM Vol2A 3-466 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| OUTSD | legacy | - | Intel | I386 | IOPL + TSS I/O bitmap (Windows IOPL = 0) · SDM Vol2B 4-176 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| INVD | legacy | - | Intel | I486REAL | CPL0 · SDM Vol2A 3-484 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| INVLPG | legacy | - | Intel | I486REAL | CPL0 · SDM Vol2A 3-486 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| WBINVD | legacy | - | Intel | I486REAL | CPL0 · SDM Vol2D 6-2 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| CLI | legacy | - | Intel | I86 | IOPL (CPL > IOPL: Windows IOPL = 0) · SDM Vol2A 3-149 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| HLT | legacy | - | Intel | I86 | CPL0 · SDM Vol2A 3-439 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| IN | legacy | - | Intel | I86 | IOPL + TSS I/O bitmap (Windows IOPL = 0) · SDM Vol2A 3-456 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| OUT | legacy | - | Intel | I86 | IOPL + TSS I/O bitmap (Windows IOPL = 0) · SDM Vol2B 4-172 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| STI | legacy | - | Intel | I86 | IOPL (CPL > IOPL: Windows IOPL = 0) · SDM Vol2B 4-675 · hw: CLI cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| INVPCID | legacy | - | Intel | INVPCID | CPL0 · SDM Vol2A 3-489 | has it | ✅ | SDM vectors: U806 INVPCID r64, m128 (66 0F 38 82 /r, CPUID.(7,0):EBX[10] on the MAX model and in the i5-13600K profile): independent SDM-pseudocode … |
| LOADIWKEY | legacy | 128 | Intel | KEYLOCKER | CPL0 · SDM Vol2A 3-563 | cannot: --guest: CPUID.7H:ECX[23] = 0; --host: enumerated (CPUID.7H:ECX[23] = 1 on the bare host), CPUID.19H:EBX[0] AESKLE = 0 (Key Locker not enabled by Windows: user-mode #UD) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U100 Key Locker LOADIWKEY: SDM/spec-pseudocode reference (ref_keylocker_misc.py) + unit tests (keylocker_aes128 / … |
| LKGS | legacy | - | Intel | LKGS | CPL0 (#UD at CPL > 0) · SDM Vol2A 3-557 | cannot: CPUID.7H.1:EAX[18] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U805 LKGS r/m16 (F2 0F 00 /6, 64-bit mode, CPL0, CPUID.(7,1):EAX[18] on the MAX model): independent SDM-pseudocode model … |
| SWAPGS | legacy | - | Intel | LONGMODE | CPL0 · SDM Vol2B 4-698 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| SYSRETQ | legacy | - | Intel | LONGMODE | CPL0 · SDM Vol2B 4-711 · hw: cases_backport_t1.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| MONITOR | legacy | - | Intel | MONITOR | CPL0 (#UD at CPL > 0) · SDM Vol2B 4-27 | cannot: --guest: CPUID.1H:ECX[3] = 0; --host: enumerated (CPUID.1H:ECX[3] = 1 on the bare host) | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| MWAIT | legacy | - | Intel | MONITOR | CPL0 (#UD at CPL > 0) · SDM Vol2B 4-157 | cannot: --guest: CPUID.1H:ECX[3] = 0; --host: enumerated (CPUID.1H:ECX[3] = 1 on the bare host) | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| RDMSRLIST | legacy | - | Intel | MSRLIST | CPL0 · SDM Vol2B 3-547 | cannot: CPUID.7H.1:EAX[27] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U802 RDMSRLIST (F2 0F 01 C6, 64-bit mode, CPUID.(7,1):EAX[27] MSRLIST on the MAX model; IA32_BARRIER 2FH): independent SDM-pseudocode … |
| WRMSRLIST | legacy | - | Intel | MSRLIST | CPL0 · SDM Vol2D 6-11 | cannot: CPUID.7H.1:EAX[27] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U802 WRMSRLIST (F3 0F 01 C6, 64-bit mode, CPUID.(7,1):EAX[27] MSRLIST on the MAX model; IA32_BARRIER 2FH): independent SDM-pseudocode … |
| RDMSR | vex | - | Intel | MSR_IMM | CPL0 · SDM Vol2B 4-545 · hw: cases_lockpriv_hw.txt | cannot: CPUID.7H.1:ECX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U803 MSR-IMM RDMSR r64, imm32 (VEX.128.F2.MAP7.W0 F6 11:000:bbb id, CPUID.(7,1):ECX[5] MSR_IMM on the MAX model): independent … |
| WRMSRNS | vex | - | Intel | MSR_IMM | CPL0 · SDM Vol2D 6-13 | cannot: CPUID.7H.1:ECX[5] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U803 MSR-IMM WRMSRNS imm32, r64 (VEX.128.F3.MAP7.W0 F6 11:000:bbb id, CPUID.(7,1):ECX[5] MSR_IMM on the MAX model): independent … |
| PBNDKB | legacy | - | Intel | PBNDKB | CPL0 (#UD at CPL > 0) · SDM Vol2B 4-241 · hw: cases_sysins_hw.txt | cannot: CPUID.7H.1:EBX[1] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U807 PBNDKB (NP 0F 01 C7, 64-bit mode, CPL0, CPUID.(7,1):EBX[1] on the MAX model; IA32_TSE_CAPABILITY 9F1H = 0, R/O): SDM-pseudocode … |
| PCONFIG | legacy | - | Intel | PCONFIG | CPL0 · SDM Vol2B 4-281 | cannot: --guest: CPUID.7H:EDX[18] = 0; --host: enumerated (CPUID.7H:EDX[18] = 1 on the bare host) | ❌ · ✅ per manual | SDM vectors (the guest hides it): U1020-U1022 PCONFIG (NP 0F 01 C5, CPUID.(7,0):EDX[18] + leaf 1BH targets 1 TME-MK / 2 TSE on the MAX model, TME_EN … |
| RDMSR | legacy | - | Intel | PENTIUMREAL | CPL0 · SDM Vol2B 4-545 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| WRMSR | legacy | - | Intel | PENTIUMREAL | CPL0 · SDM Vol2D 6-9 · hw: cases_lockpriv_hw.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| RDPMC | legacy | - | Intel | RDPMC | CR4.PCE = 0 (Windows): #GP(0) at CPL > 0 · SDM Vol2B 4-553 · hw: cases_backport_t1.txt | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| SYSEXIT | legacy | - | Intel | SEP | CPL0 · SDM Vol2B 4-708 | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| SYSEXITQ | legacy | - | Intel | SEP | CPL0 · SDM Vol2B 4-708 | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| ENCLS | legacy | - | Intel | SGX | CPL0 (SGX, #UD at CPL > 0) · SDM Vol3D 41-9 | cannot: CPUID.7H:EBX[2] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U1261-U1266 Intel SGX ENCLS (NP 0F 01 CF; leaves 00H-0FH: SGX1 and SGX2) behind the opt-in UC_X86_FEATURE_SGX (default off: #UD, as on … |
| ENCLV | legacy | - | Intel | SGX_ENCLV | CPL0 (SGX, VMX root) · SDM Vol3D 39-15 (087) | cannot: CPUID.7H:EBX[2] = 0 on --host and --guest | ❌ · ✅ gating only | gating (CPUID + #UD, --host and --guest): ENCLV (NP 0F 01 C0) is unmodelled by decision: removed from the SDM in 325462-088 (last in -087 Vol3D … |
| CLAC | legacy | - | Intel | SMAP | CPL0 (#UD at CPL > 0) · SDM Vol2A 3-139 | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| STAC | legacy | - | Intel | SMAP | CPL0 (#UD at CPL > 0) · SDM Vol2B 4-671 | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| PSMASH | legacy | - | AMD | SNP | CPL0 (AMD) · XED CPL 0 | cannot: AMD SEV-SNP, on --host and --guest | ❌ · ⬜ not implemented yet | CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not verified yet (CPL3 fault: Phase 3, D6) |
| PVALIDATE | legacy | - | AMD | SNP | CPL0 (AMD) · XED CPL 0 | cannot: AMD SEV-SNP, on --host and --guest | ❌ · ⬜ not implemented yet | CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not verified yet (CPL3 fault: Phase 3, D6) |
| RMPADJUST | legacy | - | AMD | SNP | CPL0 (AMD) · XED CPL 0 | cannot: AMD SEV-SNP, on --host and --guest | ❌ · ⬜ not implemented yet | CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not verified yet (CPL3 fault: Phase 3, D6) |
| RMPUPDATE | legacy | - | AMD | SNP | CPL0 (AMD) · XED CPL 0 | cannot: AMD SEV-SNP, on --host and --guest | ❌ · ⬜ not implemented yet | CPL0 instruction, not reachable by the sweep and no verified_forms.tsv row: not implemented / not verified yet (CPL3 fault: Phase 3, D6) |
| CLGI | legacy | - | AMD | SVM | CPL0 (AMD SVM) · emulator SVM check | cannot: AMD SVM, on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| INVLPGA | legacy | - | AMD | SVM | CPL0 (AMD SVM) · emulator SVM check | cannot: AMD SVM, on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| STGI | legacy | - | AMD | SVM | CPL0 (AMD SVM) · emulator SVM check | cannot: AMD SVM, on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| VMLOAD | legacy | - | AMD | SVM | CPL0 (AMD SVM) · emulator SVM check | cannot: AMD SVM, on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| VMRUN | legacy | - | AMD | SVM | CPL0 (AMD SVM) · emulator SVM check | cannot: AMD SVM, on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| VMSAVE | legacy | - | AMD | SVM | CPL0 (AMD SVM) · emulator SVM check | cannot: AMD SVM, on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| SEAMCALL | legacy | - | Intel | TDX | CPL0 (SEAM, VMX root) · SDM Vol3C 35-8 | cannot: Intel TDX / SEAM (server CPUs); SEAMCALL: VMX root operation, 64-bit mode (VMX non-root: VM exit 76), on --host and --guest | ❌ · ✅ gating only | gating (CPUID + #UD, --host and --guest): GATING ONLY, 1.J.13: no SEAM on either profile (IA32_MTRRCAP FEH bit 15 SEAMRR = 0; no CPUID leaf 21H … |
| SEAMRET | legacy | - | Intel | TDX | SEAM root only (privileged instruction) · SDM Vol3C 35-6 | cannot: Intel TDX / SEAM (server CPUs); SEAMRET: SEAM root operation only, on --host and --guest | ❌ · ✅ gating only | gating (CPUID + #UD, --host and --guest): GATING ONLY, 1.J.13: no SEAM on either profile (IA32_MTRRCAP FEH bit 15 SEAMRR = 0; no CPUID leaf 21H … |
| TDCALL | legacy | - | Intel | TDX | CPL0 (SEAM / VMX non-root) · SDM Vol3C 35-9 | cannot: Intel TDX / SEAM (server CPUs); TDCALL: VMX non-root / SEAM non-root operation only (VM exit 77), on --host and --guest | ❌ · ✅ gating only | gating (CPUID + #UD, --host and --guest): GATING ONLY, 1.J.13: no SEAM on either profile (IA32_MTRRCAP FEH bit 15 SEAMRR = 0; no CPUID leaf 21H … |
| INVEPT | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-5 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| INVVPID | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-8 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMCALL | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-10 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMCLEAR | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-12 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMLAUNCH | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-16 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMPTRLD | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-18 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMPTRST | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-20 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMREAD | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-22 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMRESUME | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-16 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMWRITE | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-25 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMXOFF | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-27 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| VMXON | legacy | - | Intel | VTX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-30 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |
| WBNOINVD | legacy | - | Intel | WBNOINVD | CPL0 · SDM Vol2D 6-4 | cannot: CPUID.80000008H:EBX[9] = 0 on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| WRMSRNS | legacy | - | Intel | WRMSRNS | CPL0 · SDM Vol2D 6-13 | cannot: CPUID.7H.1:EAX[19] = 0 on --host and --guest | ❌ · ✅ per manual | SDM vectors: U801 WRMSRNS (NP 0F 01 C6, CPUID.(7,1):EAX[19] on the MAX model): independent SDM-pseudocode model (ref_sysins.py) + unit test … |
| XSETBV | legacy | - | Intel | XSAVE | CPL0 · SDM Vol2D 6-70 | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| XRSTORS | legacy | - | Intel | XSAVES | CPL0 · SDM Vol2D 6-57 | has it | ⏳ open item | CPL0: Phase 3 (D6) |
| XRSTORS64 | legacy | - | Intel | XSAVES | CPL0 · SDM Vol2D 6-57 | has it | ⏳ open item | CPL0: Phase 3 (D6) |
| XSAVES | legacy | - | Intel | XSAVES | CPL0 · SDM Vol2D 6-69 | has it | ⏳ open item | CPL0: #GP at CPL3 in Phase 3 (D6); compacted format shared with U66 |
| XSAVES64 | legacy | - | Intel | XSAVES | CPL0 · SDM Vol2D 6-69 | has it | ⏳ open item | CPL0: Phase 3 (D6) |
| WRUSSD | evex | - | Intel | APX_F_CET | CPL0 · SDM Vol2D 6-20 | cannot: APX_F_CET not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645/U790 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_cases.py; #UD rules … |
| WRUSSQ | evex | - | Intel | APX_F_CET | CPL0 · SDM Vol2D 6-20 | cannot: APX_F_CET not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645/U790 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_cases.py; #UD rules … |
| ENQCMDS | evex | - | Intel | APX_F_ENQCMD | CPL0 · SDM Vol2A 3-293 | cannot: APX_F_ENQCMD not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U640/U645/U790 Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction: independent spec model (ref_apx_cases.py; #UD rules … |
| INVPCID | evex | - | Intel | APX_F_INVPCID | CPL0 · SDM Vol2A 3-489 | cannot: APX_F_INVPCID not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U806 INVPCID r64, m128 (APX EVEX.LLZ.F3.MAP4 F2 !(11):rrr:bbb, APX_F + CPUID.(7,0):EBX[10] on the MAX model): independent spec model … |
| RDMSR | evex | - | Intel | APX_F_MSR_IMM | CPL0 · SDM Vol2B 4-545 · hw: cases_lockpriv_hw.txt | cannot: APX_F_MSR_IMM not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U803 MSR-IMM RDMSR r64, imm32 (APX EVEX.128.F2.MAP7 F6 11:000:bbb id, APX_F + CPUID.(7,1):ECX[5] MSR_IMM on the MAX model): independent … |
| WRMSRNS | evex | - | Intel | APX_F_MSR_IMM | CPL0 · SDM Vol2D 6-13 | cannot: APX_F_MSR_IMM not reported by this CPU, on --host and --guest | ❌ · ✅ per manual | SDM vectors: U803 MSR-IMM WRMSRNS imm32, r64 (APX EVEX.128.F3.MAP7 F6 11:000:bbb id, APX_F + CPUID.(7,1):ECX[5] MSR_IMM on the MAX model) … |
| INVEPT | evex | - | Intel | APX_F_VMX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-5 | cannot: APX_F_VMX not reported by this CPU, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (cases_reach): base instruction not implemented in this CPU model: VMX (INVEPT) is #UD with and without the APX opt-in (EVEX map 4 … |
| INVVPID | evex | - | Intel | APX_F_VMX | CPL0 (VMX root; #UD outside VMX operation) · SDM Vol3C 33-8 | cannot: APX_F_VMX not reported by this CPU, on --host and --guest | ❌ · ⬜ not implemented yet | not implemented (cases_reach): base instruction not implemented in this CPU model: VMX (INVVPID) is #UD with and without the APX opt-in (EVEX map 4 … |

### Kernel-only sub-forms of usermode rows

- MOV (legacy): row includes the CPL0 sub-forms MOV CR (0F 20/22) and MOV DR (0F 21/23) — SDM Vol2B 4-34, SDM Vol2B 4-36; MOV CR: hardware: #GP(0) at CPL3 on
  the i5-13600K (cases_lockpriv_hw.txt, G26, bare host P/E + guest); MOV DR: #GP(0) at CPL3 (cases_probe_p1_hw.txt …

## Needs review

6 forms without a privilege class backed by evidence (not guessed); they are not in the two tables above.

| instruction | encoding | vector bits | vendor | family | Why not classified | i5-13600K | Status | verified by |
|---|---|---|---|---|---|---|---|---|
| RSM | legacy | - | Intel | I486 | SMM only: #UD outside SMM at every CPL; #GP(0) at CPL > 0 only with IA32_CORE_CAPABILITIES.RSM_IN_CPL0_ONLY · SDM Vol2B 4-592 | has it | ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| GETSEC | legacy | - | Intel | SMX | leaf-dependent: ENTERACCS / EXITAC / SENTER / SEXIT / SMCTRL / WAKEUP CPL0, CAPABILITIES / PARAMETERS no CPL condition; every leaf needs CR4.SMXE = 1 · SDM Vol2D 7-30 | cannot: CPUID.01H:ECX[6] = 0: SMX hidden in the VMware guest; the bare host enumerates SMX (CPUID.01H:ECX[6] = 1, bare-host capture 2026-10-10 `emulator\capture\host\output\<bare-host>_20261010_133354`, again 2026-10-15 `emulator\capture\smmid\output\<bare-host>_20261015_002318_native-host`); GETSEC was not run on the bare host | ❌ · ✅ per manual | SDM vectors (the guest hides it): U113 GETSEC/PCONFIG: SDM-pseudocode expectations + unit tests + cases_tsx_cet(_max).txt; strict i5-13600K profile … |
| SKINIT | legacy | - | AMD | SVM | AMD SKINIT: XED lists CPL 3 and CPL 0 forms, no emulator CPL check; AMD APM not in the project extracts | cannot: AMD SVM, on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| VMMCALL | legacy | - | AMD | SVM | AMD SVM guest call: no CPL check in the emulator (SVME only); AMD APM not in the project extracts | cannot: AMD SVM, on --host and --guest | ❌ · ⏳ open item | open: CPL3 fault check, Phase 3 (D6) |
| SEAMOPS | legacy | - | Intel | TDX | SEAM root only (no CPL text in the SDM) · SDM Vol3C 35-6 | cannot: Intel TDX / SEAM (server CPUs); SEAMOPS: SEAM root operation only, on --host and --guest | ❌ · ✅ gating only | gating (CPUID + #UD, --host and --guest): GATING ONLY, 1.J.13: no SEAM on either profile (IA32_MTRRCAP FEH bit 15 SEAMRR = 0; no CPUID leaf 21H … |
| VMFUNC | legacy | - | Intel | VMFUNC | VMX non-root only, at any CPL (no CPL condition); #UD under Windows on the i5-13600K (no VMX guest) · SDM Vol3C 33-13 | has it | ✅ | SDM vectors (CPL0 only): U1301-U1330 VMX model (docs/vmx.md) behind the opt-in UC_X86_FEATURE_VMX or a strict CPUID profile that reports VMX (U1660 … |

Family fix (P26): the forms XED lists without an ISA set (family "?") are shown as PREFIX (11), X87 (6).

