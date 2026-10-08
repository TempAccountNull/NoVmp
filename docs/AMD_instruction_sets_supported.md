# AMD (and VIA) instruction sets

_Generated 2026-10-08 02:29 from `Emulator/tools/isa/gen_status_docs.py` (HEAD `60cee24 U94: uc_mem_write invalidates translated code it overwrites (stale #UD after rewrite)`). Do not edit by hand._

These instruction sets are **not in the Intel manuals** and our CPU (Intel i5-13600K) **cannot run them**, so every row is ❌ NOT SUPPORTED on our CPU.

- Decision (2026-10-07): implement them **after every Intel instruction is done** (plan 1.15f), from the vendor manuals (AMD APM Vol 2–5, AMD LWP spec, VIA PadLock guide → `emulator/Amd Handbooks/`).
- A switch `__use_AMD_instruction_set__` (default 0 = Intel instruction set) will gate them: with 0 they are #UD as on Intel.
- Some (3DNow!, SSE4a, FEMMS) already exist in QEMU and run under the non-strict MAX model; under the i5-13600K profile with `--strict` they are #UD like the CPU.

**Totals:** 229 forms, implemented per the manual 20, not implemented yet 206

## By family

| family | forms | ✅ | ⏳ | ⬜ | ❌ implemented per manual | ❌ open item | ❌ not implemented yet |
|---|---|---|---|---|---|---|---|
| 3DNOW | 25 | 0 | 0 | 0 | 0 | 1 | 24 |
| ACE_1 | 16 | 0 | 0 | 0 | 0 | 0 | 16 |
| AMD | 3 | 1 | 2 | 0 | 0 | 0 | 0 |
| AMD_INVLPGB | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| CLZERO | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| FMA4 | 20 | 0 | 0 | 0 | 0 | 0 | 20 |
| LWP | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| MCOMMIT | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| MONITORX | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| RDPRU | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| SNP | 4 | 0 | 0 | 0 | 0 | 4 | 0 |
| SSE4a | 4 | 0 | 0 | 0 | 0 | 4 | 0 |
| SVM | 8 | 0 | 0 | 0 | 0 | 8 | 0 |
| TBM | 10 | 0 | 0 | 0 | 1 | 0 | 9 |
| VIA_PADLOCK_AES | 5 | 0 | 0 | 0 | 0 | 0 | 5 |
| VIA_PADLOCK_MONTMUL | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| VIA_PADLOCK_RNG | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| VIA_PADLOCK_SHA | 2 | 0 | 0 | 0 | 0 | 0 | 2 |
| XOP | 119 | 0 | 0 | 0 | 0 | 0 | 119 |

## Per instruction

<details><summary><b>3DNOW</b> (25 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | FEMMS | legacy | - | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — implemented per the manual, open item: implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form o… |
| ❌ | PAVGUSB | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PF2ID | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PF2IW | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFACC | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFADD | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFCMPEQ | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFCMPGE | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFCMPGT | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFMAX | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFMIN | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFMUL | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFNACC | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFPNACC | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFRCP | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFRCPIT1 | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFRCPIT2 | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFRSQIT1 | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFRSQRT | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFSUB | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PFSUBR | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PI2FD | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PI2FW | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PMULHRW | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |
| ❌ | PSWAPD | legacy | 64 | NOT SUPPORTED on our i5-13600K (AMD 3DNow!) — not implemented yet: out of scope: AMD 3DNow! |

</details>

<details><summary><b>ACE_1</b> (16 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | BSRINIT | vex | - | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: out of scope: not in the Intel SDM (XED ACE_1 extension) |
| ❌ | BSRMOVF | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | BSRMOVH | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | BSRMOVL | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TILEMOVCOL | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TILEMOVROW | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE; AMX_AVX512 not reported by this CPU) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder pl… |
| ❌ | TOP2BF16PS | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4BSSD | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4BSUD | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4BUSD | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4BUUD | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4MXBF8PS | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4MXBHF8PS | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4MXBSSPS | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4MXHBF8PS | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| ❌ | TOP4MXHF8PS | evex | 512 | NOT SUPPORTED on our i5-13600K (VIA/Zhaoxin ACE) — not implemented yet: not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AMD</b> (3 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ✅ | LZCNT | legacy | - | identical to the i5-13600K (4 forms) |
| ⏳ | SYSCALL | legacy | - | implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending |
| ⏳ | SYSRET | legacy | - | CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>AMD_INVLPGB</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | INVLPGB | legacy | - | NOT SUPPORTED on our i5-13600K (AMD INVLPGB) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |
| ❌ | TLBSYNC | legacy | - | NOT SUPPORTED on our i5-13600K (AMD INVLPGB) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |

</details>

<details><summary><b>CLZERO</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | CLZERO | legacy | - | NOT SUPPORTED on our i5-13600K (AMD CLZERO) — not implemented yet: out of scope: AMD CLZERO |

</details>

<details><summary><b>FMA4</b> (20 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VFMADDPD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMADDPS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMADDSD | vex | 128 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMADDSS | vex | 128 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMADDSUBPD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMADDSUBPS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMSUBADDPD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMSUBADDPS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMSUBPD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMSUBPS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMSUBSD | vex | 128 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFMSUBSS | vex | 128 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFNMADDPD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFNMADDPS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFNMADDSD | vex | 128 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFNMADDSS | vex | 128 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFNMSUBPD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFNMSUBPS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFNMSUBSD | vex | 128 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |
| ❌ | VFNMSUBSS | vex | 128 | NOT SUPPORTED on our i5-13600K (AMD FMA4) — not implemented yet: out of scope: AMD FMA4 |

</details>

<details><summary><b>LWP</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | LLWPCB | xop | - | NOT SUPPORTED on our i5-13600K (AMD LWP) — not implemented yet: out of scope: AMD LWP |
| ❌ | LWPINS | xop | - | NOT SUPPORTED on our i5-13600K (AMD LWP) — not implemented yet: out of scope: AMD LWP |
| ❌ | LWPVAL | xop | - | NOT SUPPORTED on our i5-13600K (AMD LWP) — not implemented yet: out of scope: AMD LWP |
| ❌ | SLWPCB | xop | - | NOT SUPPORTED on our i5-13600K (AMD LWP) — not implemented yet: out of scope: AMD LWP |

</details>

<details><summary><b>MCOMMIT</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MCOMMIT | legacy | - | NOT SUPPORTED on our i5-13600K (AMD MCOMMIT) — not implemented yet: out of scope: AMD MCOMMIT |

</details>

<details><summary><b>MONITORX</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MONITORX | legacy | - | NOT SUPPORTED on our i5-13600K (AMD MONITORX) — not implemented yet: out of scope: AMD MONITORX |
| ❌ | MWAITX | legacy | - | NOT SUPPORTED on our i5-13600K (AMD MONITORX) — not implemented yet: out of scope: AMD MONITORX |

</details>

<details><summary><b>RDPRU</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | RDPRU | legacy | - | NOT SUPPORTED on our i5-13600K (AMD RDPRU) — not implemented yet: out of scope: AMD RDPRU |

</details>

<details><summary><b>SNP</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | PSMASH | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SEV-SNP) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |
| ❌ | PVALIDATE | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SEV-SNP) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |
| ❌ | RMPADJUST | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SEV-SNP) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |
| ❌ | RMPUPDATE | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SEV-SNP) — implemented per the manual, open item: CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |

</details>

<details><summary><b>SSE4a</b> (4 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | EXTRQ | legacy | 128 | NOT SUPPORTED on our i5-13600K (AMD SSE4a) — implemented per the manual, open item: implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on… |
| ❌ | INSERTQ | legacy | 128 | NOT SUPPORTED on our i5-13600K (AMD SSE4a) — implemented per the manual, open item: implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on… |
| ❌ | MOVNTSD | legacy | 128 | NOT SUPPORTED on our i5-13600K (AMD SSE4a) — implemented per the manual, open item: implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on… |
| ❌ | MOVNTSS | legacy | 128 | NOT SUPPORTED on our i5-13600K (AMD SSE4a) — implemented per the manual, open item: implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on… |

</details>

<details><summary><b>SVM</b> (8 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | CLGI | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SVM) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ❌ | INVLPGA | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SVM) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ❌ | SKINIT | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SVM) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ❌ | STGI | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SVM) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ❌ | VMLOAD | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SVM) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ❌ | VMMCALL | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SVM) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ❌ | VMRUN | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SVM) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| ❌ | VMSAVE | legacy | - | NOT SUPPORTED on our i5-13600K (AMD SVM) — implemented per the manual, open item: CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>TBM</b> (10 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | BEXTR | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — implemented per the manual (SDM-vector verified): identical to the i5-13600K (4 forms) |
| ❌ | BLCFILL | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |
| ❌ | BLCI | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |
| ❌ | BLCIC | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |
| ❌ | BLCMSK | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |
| ❌ | BLCS | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |
| ❌ | BLSFILL | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |
| ❌ | BLSIC | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |
| ❌ | T1MSKC | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |
| ❌ | TZMSK | xop | - | NOT SUPPORTED on our i5-13600K (AMD TBM) — not implemented yet: out of scope: AMD TBM |

</details>

<details><summary><b>VIA_PADLOCK_AES</b> (5 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | XCRYPTCBC | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |
| ❌ | XCRYPTCFB | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |
| ❌ | XCRYPTCTR | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |
| ❌ | XCRYPTECB | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |
| ❌ | XCRYPTOFB | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |

</details>

<details><summary><b>VIA_PADLOCK_MONTMUL</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | MONTMUL | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |

</details>

<details><summary><b>VIA_PADLOCK_RNG</b> (1 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | XSTORE | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |

</details>

<details><summary><b>VIA_PADLOCK_SHA</b> (2 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | XSHA1 | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |
| ❌ | XSHA256 | legacy | - | NOT SUPPORTED on our i5-13600K (VIA PadLock) — not implemented yet: out of scope: VIA PadLock |

</details>

<details><summary><b>XOP</b> (119 forms)</summary>

| | instruction | encoding | vector bits | status |
|---|---|---|---|---|
| ❌ | VFRCZPD | xop | 128/256 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VFRCZPS | xop | 128/256 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VFRCZSD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VFRCZSS | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCMOV | xop | 128/256 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMEQB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMEQD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMEQQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMEQUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMEQUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMEQUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMEQUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMEQW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMFALSEB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMFALSED | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMFALSEQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMFALSEUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMFALSEUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMFALSEUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMFALSEUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMFALSEW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGEB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGED | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGEQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGEUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGEUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGEUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGEUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGEW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGTB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGTD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGTQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGTUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGTUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGTUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGTUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMGTW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLEB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLED | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLEQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLEUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLEUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLEUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLEUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLEW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLTB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLTD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLTQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLTUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLTUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLTUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLTUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMLTW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMNEQB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMNEQD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMNEQQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMNEQUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMNEQUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMNEQUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMNEQUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMNEQW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMTRUEB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMTRUED | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMTRUEQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMTRUEUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMTRUEUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMTRUEUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMTRUEUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMTRUEW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMUB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMUD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMUQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMUW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPCOMW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPERMIL2PD | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPERMIL2PS | vex | 128/256 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDBD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDBQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDBW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDDQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDUBD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDUBQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDUBW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDUDQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDUWD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDUWQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDWD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHADDWQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHSUBBW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHSUBDQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPHSUBWD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSDD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSDQH | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSDQL | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSSDD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSSDQH | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSSDQL | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSSWD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSSWW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSWD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMACSWW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMADCSSWD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPMADCSWD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPPERM | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPROTB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPROTD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPROTQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPROTW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPSHAB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPSHAD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPSHAQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPSHAW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPSHLB | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPSHLD | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPSHLQ | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |
| ❌ | VPSHLW | xop | 128 | NOT SUPPORTED on our i5-13600K (AMD XOP) — not implemented yet: out of scope: AMD XOP |

</details>

