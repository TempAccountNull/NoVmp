# AMD (and VIA) instruction sets

_Generated 2026-10-09 13:55 from `Emulator/tools/isa/gen_status_docs.py` (HEAD `0ec613b Ledger U750-U756, U758, U760 (CET on far transfers, CET_U/CET_S XSAVE components, REX2 ENDBR64, CPU-canonical CET MSRs; U757 retired); docs refresh`). Do not edit by hand._

These instruction sets are **not in the Intel manuals** and our CPU (Intel i5-13600K) **cannot run them**, so almost every row is ❌ NOT SUPPORTED on our CPU (listed under "Instructions that can't be supported for now:"). The few AMD-originated instructions Intel also implements (LZCNT, SYSCALL/SYSRET in 64-bit mode) run on the i5-13600K and are listed first.

- Decision (2026-10-07): implement them **after every Intel instruction is done** (plan 1.15f), from the vendor manuals (AMD APM Vol 2–5, AMD LWP spec, VIA PadLock guide → `emulator/Amd Handbooks/`).
- A switch `__use_AMD_instruction_set__` (default 0 = Intel instruction set) will gate them: with 0 they are #UD as on Intel.
- Some (3DNow!, SSE4a, FEMMS) already exist in QEMU and run under the non-strict MAX model; under the i5-13600K profile with `--strict` they are #UD like the CPU.

**Totals:** 229 forms, implemented per the manual 21, not implemented yet 205


## Instructions your i5-13600K can run (3 forms)

### By family

| family | forms | **Done** | **Implementing** |
|---|---|---|---|
| AMD | 3 | ✅ 1 | ⏳ 2 |

### Per instruction

<details><summary><b>AMD</b> (3 forms)</summary>

| instruction | encoding | vector bits | **Done** | **Implementing** |
|---|---|---|---|---|
| LZCNT | legacy | - | ✅ identical to the i5-13600K (4 forms) |  |
| SYSCALL | legacy | - |  | ⏳ open item — implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check … |
| SYSRET | legacy | - |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>


---

### Instructions that can't be supported for now:

**226 forms your i5-13600K cannot run** — not supported by your CPU; never compared against your hardware. The emulator implements them per the Intel manual (vendor manual for AMD/VIA) and checks them against SDM-pseudocode vectors.

#### By family

| family | forms | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|
| 3DNOW | 25 | ❌ **cannot run** (AMD 3DNow!) |  | ⏳ 1 · ⬜ 24 queued |
| ACE_1 | 16 | ❌ **cannot run** (VIA/Zhaoxin ACE; AMX_AVX512 not reported by this CPU) | ✅ 1 | ⬜ 15 queued |
| AMD_INVLPGB | 2 | ❌ **cannot run** (AMD INVLPGB) |  | ⏳ 2 |
| CLZERO | 1 | ❌ **cannot run** (AMD CLZERO) |  | ⬜ 1 queued |
| FMA4 | 20 | ❌ **cannot run** (AMD FMA4) |  | ⬜ 20 queued |
| LWP | 4 | ❌ **cannot run** (AMD LWP) |  | ⬜ 4 queued |
| MCOMMIT | 1 | ❌ **cannot run** (AMD MCOMMIT) |  | ⬜ 1 queued |
| MONITORX | 2 | ❌ **cannot run** (AMD MONITORX) |  | ⬜ 2 queued |
| RDPRU | 1 | ❌ **cannot run** (AMD RDPRU) |  | ⬜ 1 queued |
| SNP | 4 | ❌ **cannot run** (AMD SEV-SNP) |  | ⏳ 4 |
| SSE4a | 4 | ❌ **cannot run** (AMD SSE4a) |  | ⏳ 4 |
| SVM | 8 | ❌ **cannot run** (AMD SVM) |  | ⏳ 8 |
| TBM | 10 | ❌ **cannot run** (AMD TBM) | ✅ 1 | ⬜ 9 queued |
| VIA_PADLOCK_AES | 5 | ❌ **cannot run** (VIA PadLock) |  | ⬜ 5 queued |
| VIA_PADLOCK_MONTMUL | 1 | ❌ **cannot run** (VIA PadLock) |  | ⬜ 1 queued |
| VIA_PADLOCK_RNG | 1 | ❌ **cannot run** (VIA PadLock) |  | ⬜ 1 queued |
| VIA_PADLOCK_SHA | 2 | ❌ **cannot run** (VIA PadLock) |  | ⬜ 2 queued |
| XOP | 119 | ❌ **cannot run** (AMD XOP) |  | ⬜ 119 queued |

#### Per instruction

<details><summary><b>3DNOW</b> (25 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| FEMMS | legacy | - | ❌ **cannot run** (AMD 3DNow!) |  | ⏳ open item — implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check … |
| PAVGUSB | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PF2ID | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PF2IW | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFACC | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFADD | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFCMPEQ | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFCMPGE | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFCMPGT | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFMAX | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFMIN | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFMUL | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFNACC | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFPNACC | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFRCP | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFRCPIT1 | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFRCPIT2 | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFRSQIT1 | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFRSQRT | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFSUB | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PFSUBR | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PI2FD | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PI2FW | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PMULHRW | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |
| PSWAPD | legacy | 64 | ❌ **cannot run** (AMD 3DNow!) |  | ⬜ queued — out of scope: AMD 3DNow! |

</details>

<details><summary><b>ACE_1</b> (16 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| BSRINIT | vex | - | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — out of scope: not in the Intel SDM (XED ACE_1 extension) |
| BSRMOVF | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| BSRMOVH | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| BSRMOVL | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TILEMOVCOL | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TILEMOVROW | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE; AMX_AVX512 not reported by this CPU) | ✅ per manual (SDM vectors) — implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors: U720-U725 Intel AMX ISE 319433-… |  |
| TOP2BF16PS | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4BSSD | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4BSUD | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4BUSD | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4BUUD | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXBF8PS | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXBHF8PS | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXBSSPS | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXHBF8PS | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |
| TOP4MXHF8PS | evex | 512 | ❌ **cannot run** (VIA/Zhaoxin ACE) |  | ⬜ queued — not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b) |

</details>

<details><summary><b>AMD_INVLPGB</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| INVLPGB | legacy | - | ❌ **cannot run** (AMD INVLPGB) |  | ⏳ open item — CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |
| TLBSYNC | legacy | - | ❌ **cannot run** (AMD INVLPGB) |  | ⏳ open item — CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |

</details>

<details><summary><b>CLZERO</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| CLZERO | legacy | - | ❌ **cannot run** (AMD CLZERO) |  | ⬜ queued — out of scope: AMD CLZERO |

</details>

<details><summary><b>FMA4</b> (20 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VFMADDPD | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMADDPS | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMADDSD | vex | 128 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMADDSS | vex | 128 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMADDSUBPD | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMADDSUBPS | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMSUBADDPD | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMSUBADDPS | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMSUBPD | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMSUBPS | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMSUBSD | vex | 128 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFMSUBSS | vex | 128 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFNMADDPD | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFNMADDPS | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFNMADDSD | vex | 128 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFNMADDSS | vex | 128 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFNMSUBPD | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFNMSUBPS | vex | 128/256 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFNMSUBSD | vex | 128 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |
| VFNMSUBSS | vex | 128 | ❌ **cannot run** (AMD FMA4) |  | ⬜ queued — out of scope: AMD FMA4 |

</details>

<details><summary><b>LWP</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| LLWPCB | xop | - | ❌ **cannot run** (AMD LWP) |  | ⬜ queued — out of scope: AMD LWP |
| LWPINS | xop | - | ❌ **cannot run** (AMD LWP) |  | ⬜ queued — out of scope: AMD LWP |
| LWPVAL | xop | - | ❌ **cannot run** (AMD LWP) |  | ⬜ queued — out of scope: AMD LWP |
| SLWPCB | xop | - | ❌ **cannot run** (AMD LWP) |  | ⬜ queued — out of scope: AMD LWP |

</details>

<details><summary><b>MCOMMIT</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MCOMMIT | legacy | - | ❌ **cannot run** (AMD MCOMMIT) |  | ⬜ queued — out of scope: AMD MCOMMIT |

</details>

<details><summary><b>MONITORX</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MONITORX | legacy | - | ❌ **cannot run** (AMD MONITORX) |  | ⬜ queued — out of scope: AMD MONITORX |
| MWAITX | legacy | - | ❌ **cannot run** (AMD MONITORX) |  | ⬜ queued — out of scope: AMD MONITORX |

</details>

<details><summary><b>RDPRU</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| RDPRU | legacy | - | ❌ **cannot run** (AMD RDPRU) |  | ⬜ queued — out of scope: AMD RDPRU |

</details>

<details><summary><b>SNP</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| PSMASH | legacy | - | ❌ **cannot run** (AMD SEV-SNP) |  | ⏳ open item — CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |
| PVALIDATE | legacy | - | ❌ **cannot run** (AMD SEV-SNP) |  | ⏳ open item — CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |
| RMPADJUST | legacy | - | ❌ **cannot run** (AMD SEV-SNP) |  | ⏳ open item — CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |
| RMPUPDATE | legacy | - | ❌ **cannot run** (AMD SEV-SNP) |  | ⏳ open item — CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6) |

</details>

<details><summary><b>SSE4a</b> (4 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| EXTRQ | legacy | 128 | ❌ **cannot run** (AMD SSE4a) |  | ⏳ open item — implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check … |
| INSERTQ | legacy | 128 | ❌ **cannot run** (AMD SSE4a) |  | ⏳ open item — implemented; 2 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check … |
| MOVNTSD | legacy | 128 | ❌ **cannot run** (AMD SSE4a) |  | ⏳ open item — implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check … |
| MOVNTSS | legacy | 128 | ❌ **cannot run** (AMD SSE4a) |  | ⏳ open item — implemented; 1 forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check … |

</details>

<details><summary><b>SVM</b> (8 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| CLGI | legacy | - | ❌ **cannot run** (AMD SVM) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| INVLPGA | legacy | - | ❌ **cannot run** (AMD SVM) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| SKINIT | legacy | - | ❌ **cannot run** (AMD SVM) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| STGI | legacy | - | ❌ **cannot run** (AMD SVM) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| VMLOAD | legacy | - | ❌ **cannot run** (AMD SVM) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| VMMCALL | legacy | - | ❌ **cannot run** (AMD SVM) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| VMRUN | legacy | - | ❌ **cannot run** (AMD SVM) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |
| VMSAVE | legacy | - | ❌ **cannot run** (AMD SVM) |  | ⏳ open item — CPL0 instruction (1 forms): CPL3 fault check in Phase 2 (D6) |

</details>

<details><summary><b>TBM</b> (10 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| BEXTR | xop | - | ❌ **cannot run** (AMD TBM) | ✅ per manual (SDM vectors) — identical to the i5-13600K (4 forms) |  |
| BLCFILL | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |
| BLCI | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |
| BLCIC | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |
| BLCMSK | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |
| BLCS | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |
| BLSFILL | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |
| BLSIC | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |
| T1MSKC | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |
| TZMSK | xop | - | ❌ **cannot run** (AMD TBM) |  | ⬜ queued — out of scope: AMD TBM |

</details>

<details><summary><b>VIA_PADLOCK_AES</b> (5 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| XCRYPTCBC | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |
| XCRYPTCFB | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |
| XCRYPTCTR | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |
| XCRYPTECB | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |
| XCRYPTOFB | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |

</details>

<details><summary><b>VIA_PADLOCK_MONTMUL</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| MONTMUL | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |

</details>

<details><summary><b>VIA_PADLOCK_RNG</b> (1 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| XSTORE | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |

</details>

<details><summary><b>VIA_PADLOCK_SHA</b> (2 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| XSHA1 | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |
| XSHA256 | legacy | - | ❌ **cannot run** (VIA PadLock) |  | ⬜ queued — out of scope: VIA PadLock |

</details>

<details><summary><b>XOP</b> (119 forms)</summary>

| instruction | encoding | vector bits | **CPU cannot support** (why) | **Done** (per manual) | **Implementing** |
|---|---|---|---|---|---|
| VFRCZPD | xop | 128/256 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VFRCZPS | xop | 128/256 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VFRCZSD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VFRCZSS | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCMOV | xop | 128/256 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMEQB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMEQD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMEQQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMEQUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMEQUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMEQUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMEQUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMEQW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMFALSEB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMFALSED | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMFALSEQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMFALSEUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMFALSEUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMFALSEUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMFALSEUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMFALSEW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGEB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGED | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGEQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGEUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGEUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGEUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGEUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGEW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGTB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGTD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGTQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGTUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGTUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGTUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGTUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMGTW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLEB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLED | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLEQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLEUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLEUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLEUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLEUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLEW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLTB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLTD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLTQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLTUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLTUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLTUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLTUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMLTW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMNEQB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMNEQD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMNEQQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMNEQUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMNEQUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMNEQUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMNEQUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMNEQW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMTRUEB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMTRUED | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMTRUEQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMTRUEUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMTRUEUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMTRUEUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMTRUEUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMTRUEW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMUB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMUD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMUQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMUW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPCOMW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPERMIL2PD | vex | 128/256 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPERMIL2PS | vex | 128/256 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDBD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDBQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDBW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDDQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDUBD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDUBQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDUBW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDUDQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDUWD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDUWQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDWD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHADDWQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHSUBBW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHSUBDQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPHSUBWD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSDD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSDQH | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSDQL | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSSDD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSSDQH | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSSDQL | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSSWD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSSWW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSWD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMACSWW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMADCSSWD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPMADCSWD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPPERM | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPROTB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPROTD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPROTQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPROTW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPSHAB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPSHAD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPSHAQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPSHAW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPSHLB | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPSHLD | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPSHLQ | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |
| VPSHLW | xop | 128 | ❌ **cannot run** (AMD XOP) |  | ⬜ queued — out of scope: AMD XOP |

</details>

