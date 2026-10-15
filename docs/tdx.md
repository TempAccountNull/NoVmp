# Intel TDX / SEAM model (plan 1.J.13, ledgers U1331-U1349)

User decision (2026-10-14): "implement Stage A for now. Document everything we will need for full SEAM
root support as a future Stage B, including its architectural requirements, dependencies, prerequisites,
relevant SDM sections, and planned implementation scope. Include SEAM-transfer VMCS handling, SEAMRET,
SEAMOPS leaves 0/7/8/9/15, shared/private memory behavior, 17-bit VPIDs, EPT-violation #VE, and
SMM-transfer mechanics. Do not implement Stage B yet. Keep the default behavior as no SEAM, with Stage A
remaining opt-in."

- **Stage A** (this document, section 2): implemented, opt-in, default off. Validation label:
  **specification-validated** (unit tests with expected values read from the documents below). The
  i5-13600K has no SEAM and Intel SDE has no SEAM mode for our snippets, so nothing here is SDE- or
  hardware-validated; the default-off behaviour (all four instructions #UD) is what the host CPU does.
- **Stage B** (section 4): **NOT IMPLEMENTED - design only.**

## 1. Sources

Page numbers are the printed page labels (verified against the original PDFs).

| Short | Document | Use |
|---|---|---|
| **3C** | SDM 325462-093 Vol3C (326019-093): ch. 35 "Secure Arbitration Mode (SEAM)" pp. 35-1 to 35-9; VMX chapters 27-34 where they mention SEAM | CPU behaviour (wins) |
| **3D** | SDM 093 Vol3D (332831-093): Appendix A (A.3.4 p. A-4, A.5 p. A-5), Appendix B (fields 203CH p. B-2..B-4, 4026H p. B-8), Appendix C Table C-1 p. C-4 (exit reasons 76 SEAMCALL, 77 TDCALL) | CPU behaviour |
| **4** | SDM 093 Vol4 (335592-093) Table 2-2: 87H p. 2-10, FEH p. 2-13, 1400H / 1401H pp. 2-78 / 2-79 | MSRs |
| **2D** | SDM 093 Vol2D (334569-093) Table A-6 p. A-19: 0F 01 /1 (11B) 66 row: TDCALL (100), SEAMRET (101), SEAMOPS (110), SEAMCALL o64 (111) | encodings |
| **TDCAE** | Intel Trust Domain CPU Architectural Extensions, 343754-003, August 2026 (`E:\pdfs\MK\343754-003-intel-trust-domain-cpu-architectural-extensions.pdf`): ch. 1-10, App. A, B | the CPU-side SEAM specification ("governs CPU behaviour"); the SDM wins a disagreement |
| **MKTME** | Intel Architecture Memory Encryption Technologies Specification 336907-007 (`intel_docs_text\mktme_336907_007.md`) | TME-MK side |
| **TDXM** | Intel TDX Module Base Architecture Specification 348549-008 (`E:\pdfs\MK\intel-tdx-module-base-spec-348549008.pdf`) | software flow of a TDX module only (Stage B context; out of scope as code; no figures are cited) |

Text: SDM Vol3C / 3D / 4 ch. 35 content was read from the project extracts
(`intel_docs_text\sdm_vol3c_092.md`, `sdm_vol3d_092.md`, `sdm_vol4_092.md`, `sdm_vol2d_093.md`) and every
decisive passage was checked against the 093 PDFs (rendered pages). **No project extract exists for
SDM 093 Vol3C / Vol3D / Vol4 or for 343754-003** (reported to the coordinator); TDCAE was read from the
PDF directly. Open-source code (Linux `arch/x86/virt/vmx/tdx`, QEMU `target/i386/kvm/tdx*`, XED
`datafiles/tdx`) was used as a cross-check of the encodings only; nothing in the model comes from it.

## 2. Stage A - "SEAM enumerated, not ready" (implemented, opt-in)

### 2.1 Opt-in

| Item | Value |
|---|---|
| API | `UC_CTL_X86_FEATURES` bit `UC_X86_FEATURE_SEAM` = `0x10000000` (bit 28; VMX took bit 30 from the top of the free range, the vmx_ tests use bit 29 as a private test flag) |
| Requires | `UC_X86_FEATURE_VMX` in the same mask (else `UC_ERR_ARG`): SEAM is an extension of VMX operation (3C 35.1 p. 35-1) and is enumerated by a VMX capability MSR (TDCAE ch. 9 p. 9-1). Like every opt-in, only before the engine is initialised. |
| Effective | when the CPU reports VMX (CPUID.01H:ECX.VMX of the model, hidden by a strict `UC_CTL_X86_CPUID` profile like every VMX capability MSR): `x86_seam_enumerated` in `seam_model.c` |
| emu-alltest | `--x86-features 0x50000000` (VMX + SEAM) with `--cases` (U1181 generic mask) |
| Default | off: no change at all (section 2.6) |

The modelled platform: a processor that supports SEAM on a platform where no SEAM range was ever
activated and no SEAM loader / module exists (TDCAE A.2 lists the platform steps: MCHECK of the range
registers by a microcode update, NP-SEAMLDR ACM launch through GETSEC[ENTERACCS], P-SEAMLDR install;
none of them exists in the emulator). Consequences: IA32_SEAMRR_MASK.VALID is always 0; the
logical processor is never in SEAM; `x86_in_seam(env)` (cpu.h, the name shared with the TME-MK
MAXPHYADDR work) is a stub that returns false.

### 2.2 Enumeration

| Item | Default | Opt-in | Source |
|---|---|---|---|
| IA32_VMX_PROCBASED_CTLS3 (492H) bit 5 "SEAM guest-physical address width" | 0 | 1 | 3C Table 27-8 p. 27-14; TDCAE ch. 9 p. 9-1 ("bit 5 enumerates both the ability to set the control bit and the availability of SEAM ISA") |
| IA32_VMX_ENTRY_CTLS / TRUE_ENTRY_CTLS (484H / 490H) bit 57 = entry control 25 "allow SEAM-guest telemetry" | 0 | 1 | 3C Table 27-15 p. 27-25; 3C 35.1.3 p. 35-2 / TDCAE 1.1.3 p. 1-3: "Any processor that supports SEAM also supports the SEAM-specific VMCS fields and controls" |
| VMCS fields SEAM-guest KeyID (4026H, 32 bits) and SEAM shared EPT pointer (203CH / 203DH) | absent (VMREAD / VMWRITE VMfailValid 12) | exist (U1301 table: "exists only on processors that support the 1-setting of SEAM guest-physical address width") | 3C 27.6.26 pp. 27-21 / 27-22; 3D Appendix B footnotes |
| The two controls and the two fields outside SEAM | - | accepted by VM entry, no effect ("ignored outside of SEAM", 3C 27.6.26 p. 27-22; the SEAM-only VM-entry checks of 3C 29.2.1.1 pp. 29-4 / 29-5 apply only in SEAM) | |
| IA32_MTRRCAP (FEH) bit 15 SEAMRR | 0 | 1 (with CPUID.01H:EDX.MTRR) | 4 p. 2-13; 3C 35.4.1 p. 35-5 ("Processors that support SEAM architecture return IA32_MTRRCAP bit 15 as 1"); TDCAE 4.1 p. 4-1 |
| CPUID | no SEAM / TDX bit exists | unchanged | (XED lists the TDX ISA set as not CPUID-enumerated) |

### 2.3 MSRs

| MSR | Opt-in behaviour | Source |
|---|---|---|
| IA32_SEAMRR_BASE (1400H) | exists with MTRRCAP[15] (else #GP(0) as before). Reset 0 (our choice: no reset value is given). RDMSR returns the stored value. WRMSR #GP(0) for reserved bits 2:0, 24:4, 63:M (M = CPUID.80000008H:EAX[7:0], not reduced), for the KeyID bits M-1:M-k with k = IA32_TME_ACTIVATE.MK_TME_KEYID_BITS ("Bits M-1:M-k must be 0"), and while IA32_SEAMRR_MASK.LOCK = 1. CONFIGURED (3) and BASE (M-1:25) are stored. | 4 p. 2-78; TDCAE ch. 8 p. 8-1 |
| IA32_SEAMRR_MASK (1401H) | as BASE; reserved 9:0, 24:12, 63:M; VALID (11) "Read-only. Must be written with 0" -> #GP(0) when written with 1, always reads 0 (Stage A: the range is never activated); LOCK (10) writable; MASK (M-1:25) stored. After LOCK = 1 both registers are not writable (#GP(0), gap G3). | 4 pp. 2-78 / 2-79 |
| IA32_MKTME_KEYID_PARTITIONING (87H) bits 63:32 NUM_TDX_PRIV_KEYIDS | with TME-MK locked (IA32_TME_ACTIVATE.LOCK = 1), k = MK_TME_KEYID_BITS, p = TDX_RESERVED_KEYID_BITS: 2^k - 2^(k-p) (the k-bit KeyIDs that set any of bits k-1:k-p, numbered after the 2^(k-p) - 1 shared ones of bits 31:0); 0 otherwise and without the opt-in (unchanged). Example: k = 6, p = 2 -> 87H = 0000_0030_0000_000FH. | 4 p. 2-10 ("supported on all parts that enumerate support for SEAM mode"); 3C 35.2 pp. 35-2 / 35-3; TDCAE ch. 2 p. 2-1 |
| MSR_SEAMEXTEND (1402H), MSR_WBINVDP (98H), MSR_WBNOINVDP (99H), MSR_INTR_PENDING (9AH), MSR_SEAM_SAI_MODE (550H) | SEAM-only: #GP(0) outside SEAM -> unchanged (unimplemented MSRs, #GP(0)); in a VM-exit MSR-store / VM-entry MSR-load area the existing failure paths apply | TDCAE ch. 8 pp. 8-1 to 8-5 |

uc_context: the two registers are in the reset area of `CPUX86State` (a context carries them; a CPU
reset clears them).

### 2.4 Instructions (fault order)

Encodings (2D Table A-6 p. A-19; 3C pp. 35-7 / 35-9; TDCAE ch. 5): `66 0F 01 CC` TDCALL, `CD` SEAMRET,
`CE` SEAMOPS, `CF` SEAMCALL. The translator decodes them only with the opt-in and CPUID.01H:ECX.VMX in
the model; 66 with F2 / F3 is not a SEAM encoding (#UD, as before); LOCK #UD (TDCAE exception lists);
REX, segment and address-size prefixes are ignored. Everything else is `helper_seam_insn`
(`seam_model.c`). The first row applies to all four: IA32_VMX_PROCBASED_CTLS3[5] = 0 -> #UD (TDCAE
5-1, 9-1; 3C 35.5 p. 35-6 "on a processor that does not support SEAM").

| Instruction | Order of checks (Stage A outcome in **bold**) | Source |
|---|---|---|
| SEAMCALL | 1. not in VMX operation, in SMM, in SEAM, IA32_EFER.LMA = 0 or CS.L = 0 -> **#UD** (outside VMX operation; compatibility-mode guests) 2. CPL > 0 -> **#GP(0)** 3. VMX non-root -> **VM exit 76, qualification 0** (instruction length incl. prefixes) 4. IA32_SEAMRR_MASK.VALID = 0 or MOV-SS blocking -> **#GP(0)** (always in VMX root: VALID is 0) 5. CR_GLOBAL_TEE_DISABLE / SEAM_READY / P_SEAMLDR_READY / CR_SEAMLDR_MUTEX -> VMFailInvalid (coded, not reachable in Stage A) 6. SEAM call (Stage B) | 3C pp. 35-7 / 35-8; TDCAE pp. 5-2 / 5-3 |
| TDCALL | 1. not in VMX non-root and not in SEAM non-root -> **#UD** 2. CPL > 0 -> **#GP(0)** 3. **VM exit 77, qualification 0** - in 64-bit, compatibility, protected, virtual-8086 (CPL 3: #GP(0)) and real mode (unrestricted guest) | 3C p. 35-9; TDCAE p. 5-6; conflict C1 |
| SEAMRET | not in SEAM root operation, LMA = 0, CS.L = 0 -> **#UD** (always) | TDCAE pp. 5-4 / 5-5; 3C 35.3.1 p. 35-3 |
| SEAMOPS | not in SEAM root operation, LMA = 0, CS.L = 0, LOCK -> **#UD** (always) | TDCAE p. 5-7 |

Exit reasons 76 / 77: 3D Table C-1 p. C-4 (verified against the PDF; the reasons are listed among the
unconditional VM exits in 3C 28.1.2 p. 28-2). TDCALL: "Flags Affected: None".

### 2.5 Tests (`unicorn/tests/unit/test_x86.c`, prefix `tx_`)

| Test | What |
|---|---|
| `test_x86_tx_optin` | SEAM without VMX -> UC_ERR_ARG; default (no VMX / VMX only): 66 0F 01 CC-CF #UD outside VMX, in VMX root, in VMX non-root (no VM exit); MTRRCAP[15], 492H[5], 484H / 490H[57] = 0; 1400H / 1401H #GP(0); fields 4026H / 203CH VMfailValid 12; 87H[63:32] = 0 with TME-MK active. Opt-in: the MSRs differ from the default in exactly those bits; the fields exist (32-bit KeyID, 64-bit pointer with high access); a VM entry with the SEAM controls set works outside SEAM |
| `test_x86_tx_seamrr` | SEAMRR reset 0, every reserved-bit class, VALID written 1, the KeyID bits with TME-MK (k = 6, p = 2), 87H = 15 + 48 << 32, LOCK, uc_context save / restore |
| `test_x86_tx_insn` | outside VMX (all #UD, LOCK #UD); VMX root (TDCALL / SEAMRET / SEAMOPS #UD, SEAMCALL #GP(0) also with RAX[63] = 1, with REX / segment prefixes and with a configured locked range; LOCK / F2 / F3 #UD); VMX non-root 64-bit (exits 77 / 76 with qualification 0, prefixed lengths, SEAMRET / SEAMOPS / LOCK #UD); compatibility mode (TDCALL exit 77, SEAMCALL #UD); CPL 3 guest (TDCALL / SEAMCALL #GP(0), SEAMRET #UD) |

Not tested (no harness path): SMM (the model never enters SMM with VMX), real-mode / virtual-8086
unrestricted guests for TDCALL, MOV-SS blocking in front of SEAMCALL (same #GP(0) as VALID = 0).

### 2.6 Default off

Without `UC_X86_FEATURE_SEAM`: the translator never reaches the new code (the four encodings take the
old `illegal_op` paths), `x86_seam_enumerated` is false (MTRRCAP, 492H, 484H / 490H, the VMCS field
table, 87H, the MSR list are unchanged), and `emu-alltest --full` is byte-identical to the baseline.

### 2.7 Gaps and conflicts found while reading (Stage A relevant)

| Id | Item | Decision |
|---|---|---|
| C1 | TDCALL opcode table: 3C p. 35-9 "Compat/Leg Mode Valid"; TDCAE p. 5-6 "Invalid", but TDCAE's own exception sections say "Compatibility Mode Exceptions: Same as protected mode exceptions" and both pseudocodes have no mode check | SDM (and both pseudocodes): valid in compatibility / legacy mode |
| G1 | Reset value of IA32_SEAMRR_BASE / MASK not given | 0 (our choice) |
| G2 | Mask contiguity / base-to-mask alignment of SEAMRR not specified | not checked |
| G3 | "When the LOCK bit is set, the IA32_SEAMRR_BASE/MASK registers are non-writable" (4 p. 2-78): #GP or ignored not stated | #GP(0), as IA32_FEATURE_CONTROL once locked (U906); documented choice |
| G4 | How VALID becomes 1 (MCHECK by a microcode update, TDCAE A.2 "Details are outside the scope of this document") | never in Stage A; Stage B needs a host API (4.4) |
| G5 | TDCAE ch. 9: "CPUs that support SEAM disable switching to the legacy xAPIC mode after ... x2APIC mode" (IA32_XAPIC_DISABLE_STATUS, a web document not on E:\) | not modelled (LEFT TO IMPLEMENT; needs that document and docs/apic.md) |
| G6 | 4.2 TDCAE / 35.4.2 3C: outside SEAM, loading CRs / MSRs with a SEAM-private KeyID physical address #GP(0), paging-structure entries with one #PF, EPT entries EPT misconfiguration, VM entry failure, IA32_APIC_BASE with any non-zero KeyID #GP(0), VMX instruction operands "as if a reserved bit were set" | not modelled: it is the MAXPHYADDR reduction by TDX_RESERVED_KEYID_BITS outside SEAM, pending the user decision on the `x86_maxphyaddr(env)` / `x86_in_seam(env)` accessor (notes\eread\T1_tmekeyid_review.md, agent/tme). Stage A does not change any MAXPHYADDR / TME width logic. |
| G7 | INVD #GP when the SEAM range is valid and protected (TDCAE 4.2 p. 4-2) | never valid in Stage A: no change |

## 4. Stage B - full SEAM root / non-root support: NOT IMPLEMENTED - design only

Goal: a logical processor that can enter and run in SEAM root operation (loader and module mode) and
SEAM non-root operation (TDs) with the CPU-side architecture of 3C ch. 35 and TDCAE, so that our own
test SEAM modules (never an Intel TDX module or P-SEAMLDR binary) can be executed and debugged.

### 4.1 Architectural requirements

1. **SEAM state per logical processor**: in SEAM (root / non-root), sub-mode (loader / module), the
   "in SEAM" bit of cached translations, NMI blocking on entry, SMI / MSMI masking (TDCAE 3.1 p. 3-1).
2. **Platform-scope SEAM variables** (TDCAE 6.2 pp. 6-5 / 6-6): CR_CPUSVN, CR_GLOBAL_TEE_DISABLE,
   CR_SEAMEXTEND (136-byte SEAMEXTEND, Table 6-1 p. 6-1: VALID, TEE_TCB_SVN, MRSEAM, MRSIGNER,
   ATTRIBUTES, SEAM_READY, SYSTEM_UNDER_DEBUG, P_SEAMLDR_READY), CR_REPORT_MAC_KEY,
   CR_TEE_OWNER_EPOCH, CR_PFK1, CR_SEAMDB / CR_SEAMDB_NEXT, CR_SEAMLDR_MUTEX,
   CR_PENDING_TEE_KEYS_REFRESH, CR_SVNMAC_LOCK; core scope CR_SEAMLDR_RR_BASE / _MASK (6.3 p. 6-6).
   Shared by all engines of one platform (the `UC_CTL_X86_APIC_BUS` group is the natural owner).
3. **SEAM range** (3C 35.4.1 p. 35-5; TDCAE 4.1 p. 4-1): VALID = 1 -> outside SEAM root every access
   to the range is aborted (writes ignored, reads all ones); module sub-range at the bottom
   (`IA32_SEAMRR_BASE & ~FFFH`), loader sub-range at the top (CR_SEAMLDR_RR_BASE / MASK) accessible only
   in loader mode; instruction fetches from outside the range prevented in SEAM root
   (implementation-specific response: needs a decision); memory type WB / UC by CR0.CD (3C 35.4.1;
   TDCAE 4.3 p. 4-2).
4. **SEAM-transfer VMCS handling** (TDCAE 1.1.1 / 1.1.2 p. 1-2, 4.1 p. 4-1, SEAMCALL pp. 5-2 / 5-3,
   SEAMRET pp. 5-4 / 5-5; 3C 35.1.1 / 35.1.2 p. 35-2):
   - module mode: transfer VMCS at `(IA32_SEAMRR_BASE & ~FFFH) + (x + 1) * 4096`, x = x2APIC ID
     (CPUID.1FH.0:EDX); loader mode: `CR_SEAMLDR_RR_BASE + 4096`;
   - SEAMCALL = an SMM VM exit invoked by VMCALL (3C 34.15.2 pp. 34-20 to 34-22) with: exit reason 76,
     qualification 0; current-VMCS pointer := transfer VMCS; the transfer VMCS's VMCS link pointer :=
     the caller's current-VMCS pointer; SMBASE not saved; RFLAGS CF / OF / SF / PF / AF / ZF := 0;
     "Block NMI and SMI signals";
   - SEAMRET = a VM entry that returns from SMM (3C 34.15.4 pp. 34-22 to 34-25, checks 34.15.4.1-4.10)
     with: starts from the transfer VMCS; SMBASE not loaded; on success current VMCS := the link pointer;
     from loader mode: invalidate all address translations, current VMCS := NULL (FFFFFFFF_FFFFFFFFH),
     close the loader sub-range, release CR_SEAMLDR_MUTEX; then leave SEAM (close the range); failures
     are VMFailValid / VMFailInvalid / "failed VM entry" VM exits as in 34.15.4.10;
   - prerequisite: the dual-monitor machinery (executive-VMCS pointer, SMM-transfer VMCS state save /
     load) that the VMX model does not have (IA32_VMX_BASIC[49] = 0, docs/vmx.md).
5. **SEAM host-guest transitions** (3C 35.1.3 p. 35-2; TDCAE 1.1.3 pp. 1-2 / 1-3): SEAM-guest VMCS;
   VM entry in SEAM fails unless "enable EPT" = 1; SEAM shared EPT pointer bits 11:0 and 63:M must be 0
   (3C 29.2.1.1 pp. 29-4 / 29-5); a VM exit from SEAM non-root returns to SEAM root (blocks NMIs);
   "allow SEAM-guest telemetry" (no telemetry model: accepted, no effect); unbreakable shutdown on VMX
   abort during these exits (TDCAE 10.4 p. 10-2).
6. **SEAMOPS** (TDCAE ch. 5 p. 5-7, ch. 7): #UD outside SEAM root / LMA = 0 / CS.L = 0 / LOCK, #GP(0) at
   CPL > 0 and for a leaf whose CAPABILITIES bit is 0; helper functions Success / Error (7-20). The
   user-named leaves (Table 7-1 p. 7-1):
   - **leaf 0 CAPABILITIES** (p. 7-2): RAX := bitmap of supported leaves (bit 0 always 1); no flags
     changed, no exceptions. The model would report exactly the leaves it implements.
   - **leaf 7 TDSTATE_ENUM** (p. 7-11): RAX[7:0] = TDSTATE version, [15:8] = size in quadwords,
     [63:16] = 0; no flags. TDSTATE version 0 = 12 quadwords (Table 6-8 p. 6-4: DR0-DR3, DR6, CR2,
     DS_AREA, STAR, LSTAR, KERNEL_GS_BASE, TSC_AUX, FMASK).
   - **leaf 8 TDSTATE_LOAD** (p. 7-12): #GP(0) if RCX[2:0] != 0; loads the 12 registers from [RCX] in
     the order given (DR6 through SetClearDr6ReservedBits, DS_AREA / LSTAR / KERNEL_GS_BASE through
     CpuCanonicalize, TSC_AUX / FMASK low 32 bits); "the order of memory reads and register loads is
     implementation-specific" (needs a decision for the #PF partial-state case).
   - **leaf 9 TDSTATE_SAVE_CLEAR** (p. 7-13): #GP(0) if RCX[2:0] != 0; stores each register then clears
     it (DR6 := FFFF0FF0H); writable / canonical faults.
   - **leaf 15 FEATURES** (p. 7-18): RAX[1] = no WBINVDP MSRs, [2] = 128-bit keys, [3] = 256-bit keys;
     RBX / RCX / RDX = 0; all six flags cleared.
   - Leaves 1-6, 10, 11 (SEAMREPORT, SEAMDB_*, SEAMVERIFYREPORT, UPDATESVNREPORTKEY, SEAMGETKEY) need the
     cryptographic platform values (CR_REPORT_MAC_KEY, CR_PFK1, GetBaseKey, the NIST SP 800-108
     derivation string layout - not specified) - out of the user's list; would follow the SGX pattern
     of documented placeholders (docs/sgx.md 3).
7. **SEAM-only MSRs** (TDCAE ch. 8): MSR_SEAMEXTEND (1402H, loader mode, PRMRR check, COPY_IN /
   COPY_OUT, RDMSR #GP / VMX abort in the MSR-store area), MSR_WBINVDP / MSR_WBNOINVDP (98H / 99H,
   NUM_CACHE_BLOCKS: implementation-dependent value), MSR_INTR_PENDING (9AH), MSR_SEAM_SAI_MODE (550H,
   TDX Connect only: not offered). Also VMXOFF and VMCALL #GP(0) in SEAM root (3C 33.3 VMCALL p. 33-9,
   VMXOFF p. 33-26; TDCAE 3.1).
8. **Shared vs private memory** (3C 35.3.2 pp. 35-3 / 35-4; TDCAE 3.2 pp. 3-1 / 3-2):
   - GPAW = 52 only with 5-level EPT and "SEAM guest-physical address width" = 1, else 48; SHARED bit =
     GPA bit 47 / 51; GPAW 48: GPA bits 51:48 set -> EPT violation that always exits (even with
     "EPT-violation #VE"); MAXPHYADDR < 48: bits 51:48 or 46:MAXPHYADDR -> #PF with RSVD;
   - private GPAs: EPT from EPTP; every EPT paging-structure address and the final address get bits
     M-1:M-k := SEAM-guest KeyID[k-1:0];
   - shared GPAs: EPT from the SEAM shared EPT pointer with EPTP[7:0] controlling the walk; a shared EPT
     structure with a SEAM-private KeyID -> EPT misconfiguration; a final address with a SEAM-private
     KeyID -> conflict C10;
   - instruction fetch from a shared GPA -> conflict C9; CR3 / PDPTE with a shared GPA -> #GP(0); a
     guest paging structure at a shared GPA -> #PF with error code bit 3 (RSVD);
   - memory type from the shared / secure EPT (TDCAE 4.3); SEAM-private KeyID accesses outside SEAM are
     aborted; a cache line written with one KeyID is unreadable with another (machine-dependent
     memory-protection mode: needs the "Memory Protections for Confidential Computing" document,
     869103, not on E:\).
9. **17-bit VPIDs** (3C 31.1 p. 31-1; 31.4.3 footnotes 3 / 4 p. 31-23 (VM entry / exit, INVVPID) and the INVEPT footnote p. 31-24): translations used in
   SEAM carry VPIDs 10000H-1FFFFH (SEAM root = 10000H); the VMCS VPID field stays 16 bits; VM entries /
   exits in SEAM invalidate VPID 10000H instead of 0000H; INVVPID outside SEAM affects 0000H-FFFFH only,
   inside SEAM 10000H-1FFFFH only; INVEPT affects both ranges. Model consequence: today every VM entry /
   exit / INVEPT / INVVPID flushes the whole QEMU TLB (docs/vmx.md Stage 6), which satisfies every rule;
   Stage B keeps that (no per-VPID tagging) and documents the over-invalidation as allowed.
10. **EPT-violation #VE for TDs** (3C 27.6.19 p. 27-19, 28.5.8 pp. 28-22 to 28-24; TDCAE 3.2): reuse
    U1324 (convertible EPT violations, VE information area, vector 20); SEAM differences: the GPAW-48
    bits-51:48 violation is never converted (TDCAE 3.2 p. 3-2: "always causes a VM exit, even if the
    EPT-violation #VE VM execution control bit is set to 1") - consistent with the VMX model outside
    SEAM, which already does not convert a bits-51:48 violation under 4-level EPT (vmx U1330 documented
    reading, docs/quirks.md, test_x86_vmx_ve_gpa48: no EPT entry decides); in SEAM the rule extends to
    5-level EPT while "SEAM guest-physical address width" is 0; the shared / private EPT selects which EPT entry's
    "suppress #VE" bit applies (needs reading: 3C and TDCAE are silent on which structure supplies it
    for shared GPAs - question Q3).
11. **SMM-transfer mechanics / SMI in SEAM** (3C 35.3.1 / 35.3.2 pp. 35-3 / 35-4; TDCAE 3.1 / 3.2
    p. 3-1, 10.3 / 10.4 pp. 10-1 / 10-2): SMIs, MSMIs and uncore machine checks are masked in SEAM root
    and delivered after leaving it; in SEAM non-root an SMI causes a VM exit with reason 5 (I/O SMI) or
    6 (other SMI), qualification 0 (MSMI: reason 6, qualification 1), and stays pending until after the
    next SEAM return; SEAM software cannot use VMCALL to invoke an SMM-transfer monitor; poison
    consumption / triple fault in SEAM root = unbreakable shutdown (clears SEAM_READY /
    P_SEAMLDR_READY). Prerequisite: an SMI source and SMM entry with VMX (the model has QEMU's
    `smm_helper.c` but no SMI injection with VMX and no dual-monitor treatment - 3C 34.14 / 34.15
    pp. 34-17 to 34-29).
12. **Virtual-interrupt delivery in SEAM** (3C 32.2.2 p. 32-6; TDCAE 3.2): vectors 0-30 in VIRR are
    ignored when RVI is computed.
13. **Machine checks / telemetry / debug** (TDCAE ch. 10): IA32_MCi_CTL rule -> CR_GLOBAL_TEE_DISABLE;
    IA32_MCG_STATUS.SEAM_NR (bit 12) with IA32_SEAM_CAP[12] (that MSR is not defined in 3C, Vol4 or
    TDCAE - gap Q4); probe mode / ITP not modelled.

### 4.2 Dependencies

| Dependency | State | Needed for |
|---|---|---|
| VMX model (agent/vmx, U1300-U1330) | finalized at ac04da82 (the base of agent/tdx); not yet in novmp-emu | everything |
| EPT-violation #VE (U1324; GPA bits 51:48 reading U1330) | done on agent/vmx | 4.1 item 10 |
| TME-MK KeyID bits in physical addresses + the MAXPHYADDR accessor (`x86_maxphyaddr(env)` / `x86_in_seam(env)`, notes\eread\T1_tmekeyid_review.md, agent/tme) | user decision pending | items 3, 8, G6 (MAXPHYADDR is not reduced in SEAM: 3C 27.1 p. 27-1 footnote, 28-25, 29-2 etc.) |
| Dual-monitor treatment (SMM VM exits, VM entries that return from SMM) or an equivalent transfer-VMCS mechanism | not modelled (IA32_VMX_BASIC[49] = 0) | item 4 |
| SMI / MSMI injection with VMX | not modelled | item 11 |
| x2APIC ID per logical processor (CPUID.1FH) | APIC model (`UC_CTL_X86_APIC_ID`, docs/apic.md) | transfer-VMCS address |
| Platform-scope state across engines | `UC_CTL_X86_APIC_BUS` group | item 2 |
| SGX abort-page pattern (`sgx_model.c` x86_sgx_access) | done (U1262) | SEAM range aborts |
| A host API to play the platform (MCHECK -> VALID, CR_SEAMEXTEND / SEAM_READY / P_SEAMLDR_READY, CR_SEAMLDR_RR_*, CR_CPUSVN, keys) | new (like `UC_CTL_X86_SGX_PLATFORM`) | items 2, 3 |

### 4.3 Prerequisites (before any Stage B code)

1. agent/vmx merged and the TME-MK accessor decision taken (G6).
2. User decisions on the open questions: Q1 = C9, Q2 = C10, Q3 (#VE suppress bit for shared GPAs),
   Q4 (IA32_SEAM_CAP), Q5 (response to an instruction fetch outside the SEAM range in SEAM root:
   "implementation-specific"), Q6 (NUM_CACHE_BLOCKS value, TDSTATE_LOAD fault ordering), Q7 (where the
   loader sub-range lies: CR_SEAMLDR_RR_* are "initialized at patch load time" - a host API value).
3. Missing documents (ask before any download): "Intel Architecture Memory Protections for Confidential
   Computing" 869103 (TDCAE 4.2 footnote); the IA32_XAPIC_DISABLE_STATUS article (G5); the SEAMLDR
   interface specification only if P-SEAMLDR behaviour is ever emulated (out of scope below); a project
   text extract of 343754-003 and of SDM 093 Vol3C / Vol3D / Vol4.

### 4.4 Planned implementation scope (in order)

1. B1 - platform API and state: `UC_CTL_X86_SEAM_PLATFORM` (range activation = VALID, CR_SEAMEXTEND,
   CR_SEAMLDR_RR_*, CR_CPUSVN, CR_GLOBAL_TEE_DISABLE, key placeholders), platform-scope storage,
   x86_in_seam becomes real state (shared definition with agent/tme).
2. B2 - SEAM range protection: aborts outside SEAM root / loader sub-range, fetch restriction, WB/UC
   type, INVD #GP(0).
3. B3 - SEAMCALL into SEAM root (both sub-modes) and SEAMRET: the transfer-VMCS save / load of 4.1
   item 4, CR_SEAMLDR_MUTEX across engines, VMFailInvalid paths, NMI / SMI blocking, VMXOFF / VMCALL
   #GP(0) in SEAM root, 17-bit VPID rules (whole-TLB flush), unbreakable shutdown.
4. B4 - SEAMOPS leaves 0, 7, 8, 9, 15 and the SEAM-only MSRs (1402H, 98H, 99H, 9AH).
5. B5 - SEAM non-root operation: SEAM-guest VMCS checks, GPAW / SHARED bit, private / shared EPT with
   KeyID substitution, the private-address restrictions, TDCALL VM exit 77 to SEAM root, EPT-violation
   #VE (U1324) with the SEAM exceptions, virtual-interrupt RVI rule, SMI VM exits (needs item 11's SMI
   source).
6. B6 (optional, separate decision) - SEAMOPS leaves 1-6, 10, 11 with documented key placeholders.

Out of scope: NP-SEAMLDR (an ACM launched by GETSEC[ENTERACCS]; needs the TXT chipset model, blocked
on documents, plan 1.J.11.b), P-SEAMLDR and TDX module images (Intel binaries; only our own test SEAM
modules run), TDX module ABI (TDH.* / TDG.* leaves, GHCI) - software, not CPU behaviour (TDXM).

### 4.5 Test plan (Stage B)

- Unit tests (`tx_` block): a minimal own SEAM module image placed into the range by the platform API;
  SEAMCALL module / loader mode success and every VMFailInvalid; transfer-VMCS contents after SEAMCALL
  (exit reason 76, link pointer, saved VMX-root state) and after SEAMRET (current VMCS, loader-mode NULL
  pointer, translations invalidated); range aborts from VMX root / non-root / module mode; SEAMOPS leaf
  0 / 7 / 8 / 9 / 15 values, flags and faults; a TD guest: shared vs private GPAs, KeyID substitution
  seen through the EPT walk, the GPAW-48 violation, #VE delivery, TDCALL exit 77 to SEAM root, SMI VM
  exit 5 / 6.
- An independent Python reference model of the SEAMCALL / SEAMRET pseudocode and the TDSTATE leaves
  (`Emulator\tools\isa\ref_seam.py`), expected-value case files via emu-alltest `--x86-features`.
- Labels: specification-validated only (no hardware or SDE has SEAM for our snippets).

### 4.6 Other inconsistencies in 343754-003 (for the Stage B reading)

| Id | Item |
|---|---|
| C9 | Instruction fetch from a SEAM-guest shared address: 3C 35.3.2 p. 35-4 "#GP(0)"; TDCAE 3.2 p. 3-2 "page fault exception (#PF), regardless of ... NXE ... XD" (TDCAE is the newer document - user decision Q1) |
| C10 | Final physical address of a shared GPA with a SEAM-private KeyID: 3C p. 35-4 "EPT misconfiguration"; TDCAE p. 3-2 "the access is aborted" (Q2) |
| C11 | GPAW: 3C "48 bits if the control is 0"; TDCAE "52 bits if the EPT page-walk length is 5 and the control is 1; otherwise 48" (TDCAE is more precise: 4-level EPT with the control 1 -> 48) |
| C12 | TDSTATE text (TDCAE p. 6-4): layout "specified by SEAMOPS leaf 6 (TDSTATE_ENUM)"; Table 7-1 p. 7-1 and p. 7-11: TDSTATE_ENUM is leaf 7, leaf 6 is SEAMVERIFYREPORT |
| C13 | Leaf 10: contents list "UPDATESVNMAC" (p. v), Table 7-1 / p. 7-15 "UPDATESVNREPORTKEY" |
| C14 | Leaf 11 SEAMGETKEY (p. 7-16): the second input operand is listed as RCX, the pseudocode uses RDX; the key-size check "or TMP_KEYREQUEST.KEYSIZE >= 1" rejects every 256-bit request (p. 7-17) |
| C15 | Leaf 9 exceptions table (pp. 7-13 / 7-14) places "RCX is not aligned" under #PF |
| C16 | Ch. 9 names "IA32_SEAMRR_PHYSBASE / PHYSMASK" and "MSR_WBINVD / MSR_WBNOINVD"; ch. 8 and Vol4 "IA32_SEAMRR_BASE / MASK", "MSR_WBINVDP / WBNOINVDP" (Vol4 093 model tables: MSR_SEAM_WBINVDP ...); ch. 5 p. 5-1 "IA32_VMX_PROCBASED_CTRLS3" |
| C17 | 3C 35.1.2: "there is no current VMCS after such a SEAM return" (loader mode) appears in TDCAE only in the SEAMRET pseudocode (current VMCS := NULL); consistent |
| C18 | TDCAE 6.3 "Core-scope variables" introduced as "platform-scope SEAM variables" |
