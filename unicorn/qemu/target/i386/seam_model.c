/*
 * SEAM, Stage A (NoVmp, ledgers U1331-U1349, plan 1.J.13; design, fault tables and the Stage B plan:
 * docs/tdx.md).
 *
 * Sources: SDM 325462-093 Vol3C chapter 35 (Secure Arbitration Mode), Vol3D Appendix C (exit reasons
 * 76 / 77, p. C-4), Vol4 Table 2-2 (FEH, 87H, 1400H, 1401H); Intel Trust Domain CPU Architectural
 * Extensions 343754-003 (August 2026) chapters 5 (instructions), 8 (MSRs) and 9 (enumeration).
 * Everything here is specification-validated only: the i5-13600K has no SEAM (the four instructions
 * are #UD there, as in the default model).
 *
 * Opt-in: UC_CTL_X86_FEATURES / UC_X86_FEATURE_SEAM together with UC_X86_FEATURE_VMX (default off).
 * Stage A models a processor that enumerates SEAM on a platform where SEAM is not ready: no SEAM
 * range is ever activated (IA32_SEAMRR_MASK.VALID stays 0 - activation is a microcode / platform
 * operation, 343754 A.2), no SEAM loader or module exists, and the logical processor never enters
 * SEAM (x86_in_seam is always false). Stage B (SEAM root / non-root operation) is design only.
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U1331): whole file */
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "uc_priv.h"
#include <unicorn/unicorn.h>

#define EXIT_REASON_SEAMCALL        76      /* Vol3D Table C-1 p. C-4 */
#define EXIT_REASON_TDCALL          77

#define SEAMRR_BASE_CONFIGURED      (1ull << 3)
#define SEAMRR_MASK_LOCK            (1ull << 10)
#define SEAMRR_MASK_VALID           (1ull << 11)

/* helper_seam_insn operations: ModRM - CCH of 66 0F 01 CC / CD / CE / CF */
enum { SEAM_OP_TDCALL, SEAM_OP_SEAMRET, SEAM_OP_SEAMOPS, SEAM_OP_SEAMCALL };

/*
 * The CPU enumerates SEAM: IA32_VMX_PROCBASED_CTLS3[5] reads 1 (343754 ch. 9 p. 9-1: bit 5
 * "enumerates both the ability to set the control bit and the availability of SEAM ISA"). The
 * capability MSRs exist only with VMX as the CPU reports it (a strict UC_CTL_X86_CPUID profile that
 * hides CPUID.01H:ECX.VMX hides SEAM too); the TERTIARY controls are always offered (vmx_helper.c).
 */
bool x86_seam_enumerated(CPUX86State *env)
{
    return env->uc && (env->uc->x86_features & UC_X86_FEATURE_SEAM) && x86_vmx_cpuid(env);
}

/* ---------------------------------------------------------------------------------------------
 * MSRs (U1332): IA32_SEAMRR_BASE (1400H), IA32_SEAMRR_MASK (1401H)
 * ------------------------------------------------------------------------------------------- */

/*
 * Vol4 Table 2-2 1400H / 1401H (093 p. 2-78 / 2-79): "IA32_MTRRCAP.SEAMRR[15] = 1"; 343754 ch. 9:
 * "enumerated by bit 15 of IA32_MTRRCAP". IA32_MTRRCAP exists with CPUID.01H:EDX.MTRR (as the CPU
 * reports it), so the SEAM range registers need it too. The SEAM-only MSRs of 343754 ch. 8
 * (MSR_SEAMEXTEND 1402H, MSR_WBINVDP 98H, MSR_WBNOINVDP 99H, MSR_INTR_PENDING 9AH, MSR_SEAM_SAI_MODE
 * 550H) are #GP(0) outside SEAM - Stage A is always outside SEAM, so they stay unimplemented MSRs.
 */
int x86_seam_msr_present(CPUX86State *env, uint32_t msr)
{
    if (msr != MSR_IA32_SEAMRR_BASE && msr != MSR_IA32_SEAMRR_MASK) {
        return -1;
    }
    return x86_seam_enumerated(env) && (env->features[FEAT_1_EDX] & CPUID_MTRR) &&
           (x86_cpuid_profile_mask(env, 1, 0, 3) & CPUID_MTRR);
}

/*
 * WRMSR (Vol4 Table 2-2; SDM Vol2D WRMSR: #GP(0) for reserved bits). M = CPUID.80000008H:EAX[7:0]
 * (not reduced: the Vol4 comment names CPUID.80000008H). BASE: 2:0, 24:4 and 63:M reserved; "Bits
 * M-1:M-k must be 0, where k = IA32_TME_ACTIVATE.MK_TME_KEYID_BITS" (the KeyID bits). MASK: 9:0,
 * 24:12 and 63:M reserved; VALID (11) "Read-only. Must be written with 0". "When the LOCK bit is set,
 * the IA32_SEAMRR_BASE/MASK registers are non-writable": #GP(0) (our reading - the SDM does not say
 * #GP or ignored; like IA32_FEATURE_CONTROL once locked, U906; docs/tdx.md gap G3). 343754 ch. 8:
 * "these MSRs can be written only outside SEAM" (always true in Stage A). Not checked (not stated):
 * mask contiguity, base alignment to the mask.
 */
int x86_seam_msr_write_ok(CPUX86State *env, uint32_t msr, uint64_t val)
{
    int m = env_archcpu(env)->phys_bits;
    unsigned k = (unsigned)(env->tme_activate >> 32) & 0xf;
    uint64_t hi = m < 64 ? ~0ull << m : 0;

    if (msr != MSR_IA32_SEAMRR_BASE && msr != MSR_IA32_SEAMRR_MASK) {
        return -1;
    }
    if (env->seamrr_mask & SEAMRR_MASK_LOCK) {
        return 0;
    }
    if (msr == MSR_IA32_SEAMRR_BASE) {
        uint64_t keyid = k ? MAKE_64BIT_MASK(m - k, k) : 0;
        return !(val & (MAKE_64BIT_MASK(0, 3) | MAKE_64BIT_MASK(4, 21) | hi | keyid));
    }
    return !(val & (MAKE_64BIT_MASK(0, 10) | SEAMRR_MASK_VALID | MAKE_64BIT_MASK(12, 13) | hi));
}

bool x86_seam_rdmsr(CPUX86State *env, uint32_t msr, uint64_t *val)
{
    switch (msr) {
    case MSR_IA32_SEAMRR_BASE:
        *val = env->seamrr_base;
        return true;
    case MSR_IA32_SEAMRR_MASK:
        *val = env->seamrr_mask;            /* VALID (11) = 0: the SEAM range is never activated */
        return true;
    default:
        return false;
    }
}

/* the value passed x86_seam_msr_write_ok (msr_write_ok) */
bool x86_seam_wrmsr(CPUX86State *env, uint32_t msr, uint64_t val)
{
    switch (msr) {
    case MSR_IA32_SEAMRR_BASE:
        env->seamrr_base = val;
        return true;
    case MSR_IA32_SEAMRR_MASK:
        env->seamrr_mask = val;
        return true;
    default:
        return false;
    }
}

/*
 * U1333: IA32_MKTME_KEYID_PARTITIONING.NUM_TDX_PRIV_KEYIDS (87H bits 63:32, Vol4 Table 2-2 093
 * p. 2-10): "supported on all parts that enumerate support for SEAM mode. If IA32_TME_ACTIVATE.LOCK is
 * 1, this field reports the number of SEAM-private KeyIDs supported by the processor (all KeyIDs,
 * except shared KeyIDs and KeyID 0); otherwise, it reports 0". With k = MK_TME_KEYID_BITS and p =
 * TDX_RESERVED_KEYID_BITS a k-bit KeyID is SEAM-private if it sets any of bits k-1:k-p (343754 ch. 2
 * p. 2-1, Vol3C 35.2): 2^k - 2^(k-p) KeyIDs, numbered after the 2^(k-p) - 1 shared ones that
 * NUM_MKTME_KEYIDS reports (U1020). Every k-bit value is a KeyID of the model (MK_TME_MAX_KEYS =
 * 2^MK_TME_MAX_KEYID_BITS - 1).
 */
uint64_t x86_seam_tdx_priv_keyids(CPUX86State *env)
{
    uint64_t act = env->tme_activate;
    unsigned k = (unsigned)(act >> 32) & 0xf, p = (unsigned)(act >> 36) & 0xf;

    if (!x86_seam_enumerated(env) || !(act & 1) || k == 0 || p > k) {
        return 0;
    }
    return (1ull << k) - (1ull << (k - p));
}

/* ---------------------------------------------------------------------------------------------
 * Instructions (U1334): 66 0F 01 CC TDCALL, CD SEAMRET, CE SEAMOPS, CF SEAMCALL
 * ------------------------------------------------------------------------------------------- */

/*
 * The translator calls this for 66 0F 01 CC-CF (no F2 / F3, no LOCK - LOCK is #UD there, 343754 ch. 5
 * "If preceded by LOCK prefix") with the opt-in and CPUID.01H:ECX.VMX in the model; without the opt-in
 * the encodings stay #UD in the translator, as before.
 *
 * Enumeration (343754 ch. 5 p. 5-1, ch. 9 p. 9-1; Vol3C 35.5 p. 35-6): IA32_VMX_PROCBASED_CTLS3[5] = 0
 * -> #UD for all four.
 * TDCALL (Vol3C p. 35-9; 343754 p. 5-6): #UD outside VMX non-root and SEAM non-root operation;
 * #GP(0) if CPL > 0; else VM exit 77, qualification 0. No mode check in the pseudocode: the SDM opcode
 * table says Compat/Leg Mode "Valid", 343754 "Invalid" while its exception lists say "Same as
 * protected mode exceptions" - the SDM (and both pseudocodes) win (docs/tdx.md conflict C1).
 * SEAMRET (343754 p. 5-4) / SEAMOPS (p. 5-7): #UD outside SEAM root operation - always in Stage A.
 * SEAMCALL (Vol3C pp. 35-7 / 35-8; 343754 pp. 5-2 / 5-3): #UD outside VMX operation, in SMM, in SEAM,
 * with IA32_EFER.LMA = 0 or CS.L = 0; #GP(0) if CPL > 0; VMX non-root: VM exit 76, qualification 0;
 * #GP(0) if IA32_SEAMRR_MASK.VALID = 0 or events are blocked by MOV SS - always the former in
 * Stage A. The VMFailInvalid checks after it (CR_GLOBAL_TEE_DISABLE, CR_SEAMEXTEND.SEAM_READY /
 * P_SEAMLDR_READY, CR_SEAMLDR_MUTEX) and the SEAM call are Stage B (docs/tdx.md).
 */
void helper_seam_insn(CPUX86State *env, uint32_t op)
{
    uintptr_t ra = GETPC();
    bool movss = (env->hflags & HF_INHIBIT_IRQ_MASK) && !env->fred_sti;

    if (!x86_seam_enumerated(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    switch (op) {
    case SEAM_OP_TDCALL:
        if (!env->vmx.nonroot) {            /* SEAM non-root operation: never in Stage A */
            raise_exception_ra(env, EXCP06_ILLOP, ra);
        }
        if (env->hflags & HF_CPL_MASK) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        x86_vmx_insn_exit(env, EXIT_REASON_TDCALL, 0, 0, false, ra);
    case SEAM_OP_SEAMCALL:
        if (!env->vmx.on || (env->hflags & HF_SMM_MASK) || x86_in_seam(env) ||
            !(env->hflags & HF_LMA_MASK) || !(env->hflags & HF_CS64_MASK)) {
            raise_exception_ra(env, EXCP06_ILLOP, ra);
        }
        if (env->hflags & HF_CPL_MASK) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        if (env->vmx.nonroot) {
            x86_vmx_insn_exit(env, EXIT_REASON_SEAMCALL, 0, 0, false, ra);
        }
        if (!(env->seamrr_mask & SEAMRR_MASK_VALID) || movss) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        /*
         * Not reachable in Stage A (VALID is never 1). SEAM is not ready for execution in either
         * sub-mode (CR_SEAMEXTEND.SEAM_READY = P_SEAMLDR_READY = 0: no NP-SEAMLDR ever ran):
         * VMFailInvalid (Vol3C 33.2: CF = 1, PF / AF / ZF / SF / OF = 0).
         */
        CC_SRC = CC_C;
        return;
    default:                                /* SEAMRET, SEAMOPS: not in SEAM root operation */
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
}

#endif /* __Use_Original_Qemu (U1331) */
