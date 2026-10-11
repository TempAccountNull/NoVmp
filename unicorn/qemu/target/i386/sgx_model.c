/*
 * Intel SGX (NoVmp, ledgers U1261-U1299, plan 1.J.11; design and requirement mapping: docs/sgx.md).
 *
 * Sources: SDM 092 Vol3D (332831-092) chapters 37-43 (compared with 093: identical for the parts
 * modelled here), Vol1 Table 21-47..21-49 (CPUID.12H), Vol3A 5.7 / 7.15 (PFEC.SGX), Vol4 Table 2-2
 * (3AH, 8CH-8FH, 500H). Everything here is specification-validated only: the i5-13600K has no SGX
 * and Intel SDE cannot run CPL0 enclave-management code.
 *
 * Opt-in: UC_CTL_X86_FEATURES / UC_X86_FEATURE_SGX (default off). The platform (EPC section, the
 * platform-specific keys and SVNs) comes from UC_CTL_X86_SGX_PLATFORM; every value the SDM leaves
 * to the platform is a documented placeholder (docs/sgx.md section 3), never an invented Intel
 * value.
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U1261): whole file */
#include "cpu.h"
#include "exec/exec-all.h"
#include "uc_priv.h"
#include <unicorn/unicorn.h>

/* ---------------------------------------------------------------------------------------------
 * Enumeration and platform (U1261)
 * ------------------------------------------------------------------------------------------- */

/*
 * The model enumerates SGX: the opt-in created the package state and the CPU reports
 * CPUID.(07H,0):EBX.SGX as seen by software (a strict UC_CTL_X86_CPUID profile that hides it hides
 * SGX, as the translator hides instructions). CPUID.12H.0:EAX.SGX1 follows it (Table 37-3).
 */
bool x86_sgx_enumerated(CPUX86State *env)
{
    return env->sgx && (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SGX) &&
           (x86_cpuid_profile_mask(env, 7, 0, 1) & CPUID_7_0_EBX_SGX);
}

/*
 * NoVmp (ledger U1261): create the package state from UC_CTL_X86_SGX_PLATFORM (called from the
 * CPU realize path after the feature words are final). An EPC section beyond MAXPHYADDR is not
 * reachable by any paging mode (Vol3A 5.5: those PTE bits are reserved): the model then has no
 * EPC section (epc_size 0 in the platform read back too; CPUID.12H.2 invalid).
 */
void x86_sgx_init(CPUX86State *env)
{
    struct uc_struct *uc = env->uc;
    uc_x86_sgx_platform *cfg = &uc->x86_sgx_platform;
    X86CPU *cpu = env_archcpu(env);
    X86SgxPkg *p;

    if (env->sgx || !(uc->x86_features & UC_X86_FEATURE_SGX)) {
        return;
    }
    if (cfg->epc_size && cfg->epc_base + cfg->epc_size > (1ULL << cpu->phys_bits)) {
        cfg->epc_base = 0;
        cfg->epc_size = 0;
    }
    p = g_new0(X86SgxPkg, 1);
    p->epc_base = cfg->epc_size ? cfg->epc_base : 0;
    p->epc_size = cfg->epc_size;
    p->npages = cfg->epc_size >> 12;
    p->epcm = p->npages ? g_new0(X86SgxEpcm, p->npages) : NULL;
    memcpy(p->root_key, cfg->root_key, 16);
    memcpy(p->cpusvn, cfg->cpusvn, 16);
    memcpy(p->owner_epoch, cfg->owner_epoch, 16);
    memcpy(p->seal_fuses, cfg->seal_fuses, 16);
    memcpy(p->base_pk, cfg->base_pk, 16);
    memcpy(p->report_keyid, cfg->report_keyid, 32);
    memcpy(p->le_hash_reset, cfg->le_pubkey_hash, 32);
    p->svn_sinit = cfg->svn_sinit;
    p->next_eid = 1;            /* CR_NEXT_EID: our choice of start value */
    env->sgx = p;
    x86_sgx_reset(env);
}

/*
 * NoVmp (ledger U1261): the per-logical-processor state after reset (the reset area is already
 * zero: not in enclave mode, 42.9). IA32_SGXLEPUBKEYHASH0-3: "On reset, the default value is the
 * digest of Intel's signing key" (Vol4 8CH-8FH) - the platform's value (host-provided).
 */
void x86_sgx_reset(CPUX86State *env)
{
    int i;

    if (!env->sgx) {
        return;
    }
    for (i = 0; i < 4; i++) {
        env->sgx_lehash[i] = ldq_le_p(env->sgx->le_hash_reset + 8 * i);
    }
}

/*
 * CPUID.12H (Vol1 Table 21-47..21-49), valid with CPUID.(07H,0):EBX.SGX (the caller checks
 * MAX_LEAF). Sub-leaf 0: SGX1, SGX2; MISCSELECT EXINFO; the model's maximum enclave sizes.
 * Sub-leaf 1: the SECS.ATTRIBUTES bits ECREATE may set - DEBUG, MODE64BIT, PROVISIONKEY,
 * EINITTOKEN_KEY, and XFRM = the XCR0 bits the model supports (XFRM[63] never). Sub-leaf 2: the
 * EPC section (type 1; property 0001b "confidentiality, integrity and replay protection" - our
 * choice: the model protects it by access control only, docs/sgx.md 3); 3 and up: type 0.
 * Table 37-3 marks the leaf "Valid*" ("dependent on enablement") without giving values for an
 * unlocked / disabled IA32_FEATURE_CONTROL: the model reports the same values in every state
 * (our choice, docs/sgx.md 4).
 */
void x86_sgx_cpuid(CPUX86State *env, uint32_t count, uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
    *eax = *ebx = *ecx = *edx = 0;
    if (!env->sgx || !(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SGX)) {
        return;
    }
    switch (count) {
    case 0:
        *eax = NOVMP_SGX_12_0_EAX;
        *ebx = NOVMP_SGX_12_0_EBX;
        *edx = NOVMP_SGX_12_0_EDX;
        break;
    case 1: {
        uint64_t xfrm = ((uint64_t)env->features[FEAT_XSAVE_XCR0_HI] << 32 |
                         env->features[FEAT_XSAVE_XCR0_LO]) & ~(1ULL << 63);

        *eax = (uint32_t)NOVMP_SGX_ATTR_LOW;
        *ebx = (uint32_t)(NOVMP_SGX_ATTR_LOW >> 32);
        *ecx = (uint32_t)(xfrm | 3);
        *edx = (uint32_t)(xfrm >> 32);
        break;
    }
    case 2:
        if (env->sgx->epc_size) {
            *eax = 1 | (uint32_t)(env->sgx->epc_base & 0xfffff000u);
            *ebx = (uint32_t)(env->sgx->epc_base >> 32) & 0xfffff;
            *ecx = 1 | (uint32_t)(env->sgx->epc_size & 0xfffff000u);
            *edx = (uint32_t)(env->sgx->epc_size >> 32) & 0xfffff;
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------------------------
 * MSRs (U1261): IA32_SGXLEPUBKEYHASH0-3 (8CH-8FH), IA32_SGX_SVN_STATUS (500H). IA32_FEATURE_CONTROL
 * bits 17 / 18 are in misc_helper.c (U906).
 * ------------------------------------------------------------------------------------------- */

static bool sgx_lc_enumerated(CPUX86State *env)
{
    return x86_sgx_enumerated(env) && (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_SGX_LC) &&
           (x86_cpuid_profile_mask(env, 7, 0, 2) & CPUID_7_0_ECX_SGX_LC);
}

/*
 * Vol4 Table 2-2: 8CH-8FH "Read permitted If CPUID.12H.00H:EAX[0] = 1 && CPUID.07H.00H:ECX[30] = 1";
 * 500H "If CPUID.07H.00H:EBX[2] = 1". 90H / 91H (IA32_SGXLEPUBKEYHASH4/5, "bits 383:256 of the
 * SHA256 digest") are not modelled (a SHA-256 digest has 256 bits; Vol3D uses 256 bits;
 * docs/sgx.md 4): they stay unimplemented MSRs (#GP(0)).
 */
int x86_sgx_msr_present(CPUX86State *env, uint32_t msr)
{
    if (msr >= MSR_IA32_SGXLEPUBKEYHASH0 && msr <= MSR_IA32_SGXLEPUBKEYHASH3) {
        return sgx_lc_enumerated(env);
    }
    if (msr == MSR_IA32_SGX_SVN_STATUS) {
        return x86_sgx_enumerated(env);
    }
    return -1;
}

/*
 * Write permission (Vol4 8CH: "Write permitted if CPUID.12H.00H:EAX[0] = 1 &&
 * IA32_FEATURE_CONTROL[17] = 1 && IA32_FEATURE_CONTROL[0] = 1"; Vol3D 39.1.4 / 42.2.2: "read
 * only" otherwise - #GP(0) for WRMSR). IA32_SGX_SVN_STATUS is R/O.
 */
int x86_sgx_msr_write_ok(CPUX86State *env, uint32_t msr, uint64_t val)
{
    (void)val;
    if (msr >= MSR_IA32_SGXLEPUBKEYHASH0 && msr <= MSR_IA32_SGXLEPUBKEYHASH3) {
        return (env->msr_ia32_feature_control & (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX_LC)) ==
               (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX_LC);
    }
    if (msr == MSR_IA32_SGX_SVN_STATUS) {
        return 0;
    }
    return -1;
}

bool x86_sgx_rdmsr(CPUX86State *env, uint32_t msr, uint64_t *val)
{
    if (msr >= MSR_IA32_SGXLEPUBKEYHASH0 && msr <= MSR_IA32_SGXLEPUBKEYHASH3) {
        *val = env->sgx_lehash[msr - MSR_IA32_SGXLEPUBKEYHASH0];
        return true;
    }
    if (msr == MSR_IA32_SGX_SVN_STATUS) {
        /* Table 42-2: Lock 0, SGX_SVN_SINIT 23:16 with CPUID.01H:ECX.SMX (else reserved 0) */
        *val = env->sgx_svn_lock |
               ((env->features[FEAT_1_ECX] & CPUID_EXT_SMX) ? (uint64_t)env->sgx->svn_sinit << 16 : 0);
        return true;
    }
    return false;
}

bool x86_sgx_wrmsr(CPUX86State *env, uint32_t msr, uint64_t val)
{
    if (msr >= MSR_IA32_SGXLEPUBKEYHASH0 && msr <= MSR_IA32_SGXLEPUBKEYHASH3) {
        env->sgx_lehash[msr - MSR_IA32_SGXLEPUBKEYHASH0] = val;
        return true;
    }
    return false;
}

/*
 * "Once processor reserved memory protections are activated (see Section 39.5), any execution of
 * INVD will result in a #GP(0)" (Vol3D 39.6.5); 39.5 does not say when they are activated. Our
 * choice (docs/sgx.md 3): the model has an EPC section and software opted in to SGX
 * (IA32_FEATURE_CONTROL.LOCK = SGX_ENABLE = 1, 37.7.1).
 */
bool x86_sgx_prm_active(CPUX86State *env)
{
    return x86_sgx_enumerated(env) && env->sgx->epc_size &&
           (env->msr_ia32_feature_control & (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX)) ==
               (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX);
}

#endif /* __Use_Original_Qemu (U1261) */
