/* Unicorn Emulator Engine */
/* By Nguyen Anh Quynh <aquynh@gmail.com>, 2015 */
/* Modified for Unicorn Engine by Chen Huitao<chenhuitao@hfmrit.com>, 2020 */

#include "uc_priv.h"
#include "sysemu/cpus.h"
#include "cpu.h"
#include "unicorn_common.h"
#include <unicorn/x86.h> /* needed for uc_x86_mmr */
#include "unicorn.h"
#if __Use_Original_Qemu != 1 /* ours (U831) */
#include "exec/exec-all.h" /* tlb_flush */
#endif /* __Use_Original_Qemu (U831) */
#if __Use_Original_Qemu != 1 /* ours (U832) */
/*
 * NoVmp (ledger U832): env is the live CPU, not a uc_context image (uc_context_reg_read/write
 * run reg_read/reg_write on the image; uc_context_alloc zeroes it, a save copies env->uc)
 */
static bool x86_env_is_live(CPUX86State *env)
{
    return env->uc != NULL && env->uc->cpu != NULL && env->uc->cpu->env_ptr == env;
}
#endif /* __Use_Original_Qemu (U832) */

#define FPST(n) (env->fpregs[(env->fpstt + (n)) & 7].d)

#define X86_NON_CS_FLAGS (DESC_P_MASK | DESC_S_MASK | DESC_W_MASK | DESC_A_MASK)
static void load_seg_16_helper(CPUX86State *env, int seg, uint32_t selector)
{
    cpu_x86_load_seg_cache(env, seg, selector, (selector << 4), 0xffff,
                           X86_NON_CS_FLAGS);
}

void cpu_get_fp80(uint64_t *pmant, uint16_t *pexp, floatx80 f);
floatx80 cpu_set_fp80(uint64_t mant, uint16_t upper);

extern void helper_wrmsr(CPUX86State *env);
extern void helper_rdmsr(CPUX86State *env);
extern void x86_store_dr(CPUX86State *env, int reg, target_ulong t0);

static void x86_set_pc(struct uc_struct *uc, uint64_t address)
{
    if (uc->mode == UC_MODE_16) {
        int16_t cs = (uint16_t)X86_CPU(uc->cpu)->env.segs[R_CS].selector;
        ((CPUX86State *)uc->cpu->env_ptr)->eip = address - cs * 16;
    } else
        ((CPUX86State *)uc->cpu->env_ptr)->eip = address;
}

static uint64_t x86_get_pc(struct uc_struct *uc)
{
    if (uc->mode == UC_MODE_16) {
        return X86_CPU(uc->cpu)->env.segs[R_CS].selector * 16 +
               ((CPUX86State *)uc->cpu->env_ptr)->eip;
    } else {
        return ((CPUX86State *)uc->cpu->env_ptr)->eip;
    }
}

static void x86_release(void *ctx)
{
    int i;
    TCGContext *tcg_ctx = (TCGContext *)ctx;
    X86CPU *cpu = (X86CPU *)tcg_ctx->uc->cpu;
    CPUTLBDesc *d = cpu->neg.tlb.d;
    CPUTLBDescFast *f = cpu->neg.tlb.f;
    CPUTLBDesc *desc;
    CPUTLBDescFast *fast;
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);

    release_common(ctx);
    for (i = 0; i < NB_MMU_MODES; i++) {
        desc = &(d[i]);
        fast = &(f[i]);
        g_free(desc->fulltlb);
        g_free(fast->table);
    }

    free(xcc->model);
}

static void reg_reset(struct uc_struct *uc)
{
    CPUArchState *env = uc->cpu->env_ptr;

    memset(env->regs, 0, sizeof(env->regs));
    memset(env->segs, 0, sizeof(env->segs));
    memset(env->cr, 0, sizeof(env->cr));

    memset(&env->ldt, 0, sizeof(env->ldt));
    memset(&env->gdt, 0, sizeof(env->gdt));
    memset(&env->tr, 0, sizeof(env->tr));
    memset(&env->idt, 0, sizeof(env->idt));

    env->eip = 0;
    cpu_load_eflags(env, 0, -1);
    env->cc_op = CC_OP_EFLAGS;

#if __Use_Original_Qemu == 1 /* original QEMU (U48) */
    env->fpstt = 0; /* top of stack index */
    env->fpus = 0;
    env->fpuc = 0;
    memset(env->fptags, 0, sizeof(env->fptags)); /* 0 = valid, 1 = empty */

    env->mxcsr = 0;
#else /* ours (U48) */
    /*
     * x87/SSE state after RESET, SDM Vol3 Table 11-1: FCW 0040H, FSW 0000H,
     * FTW 5555H (all registers valid +0.0), MXCSR 1F80H. NoVmp (ledger U48):
     * FCW and MXCSR were 0 (reserved FCW bit 6 reads 1; MXCSR 0 unmasked
     * every SIMD exception and left sse_status out of sync).
     */
    env->fpstt = 0; /* top of stack index */
    env->fpus = 0;
    memset(env->fptags, 0, sizeof(env->fptags)); /* 0 = valid, 1 = empty */
    cpu_set_fpuc(env, 0x0040);

    cpu_set_mxcsr(env, 0x1f80);
#endif /* __Use_Original_Qemu (U48) */
    memset(env->xmm_regs, 0, sizeof(env->xmm_regs));
    memset(&env->xmm_t0, 0, sizeof(env->xmm_t0));
    memset(&env->mmx_t0, 0, sizeof(env->mmx_t0));

    memset(env->ymmh_regs, 0, sizeof(env->ymmh_regs));

    memset(env->opmask_regs, 0, sizeof(env->opmask_regs));
    memset(env->zmmh_regs, 0, sizeof(env->zmmh_regs));
    memset(env->dr, 0, sizeof(env->dr));
    env->dr[6] = DR6_FIXED_1;
    env->dr[7] = DR7_FIXED_1;

    /* sysenter registers */
    env->sysenter_cs = 0;
    env->sysenter_esp = 0;
    env->sysenter_eip = 0;
    env->efer = 0;
    env->star = 0;

    env->vm_hsave = 0;

    env->tsc = 0;
    env->tsc_adjust = 0;
    env->tsc_deadline = 0;

    env->mcg_status = 0;
    env->msr_ia32_misc_enable = 0;
    env->msr_ia32_feature_control = 0;

    env->msr_fixed_ctr_ctrl = 0;
    env->msr_global_ctrl = 0;
    env->msr_global_status = 0;
    env->msr_global_ovf_ctrl = 0;
    memset(env->msr_fixed_counters, 0, sizeof(env->msr_fixed_counters));
    memset(env->msr_gp_counters, 0, sizeof(env->msr_gp_counters));
    memset(env->msr_gp_evtsel, 0, sizeof(env->msr_gp_evtsel));

#ifdef TARGET_X86_64
    memset(env->hi16_zmm_regs, 0, sizeof(env->hi16_zmm_regs));
    env->lstar = 0;
    env->cstar = 0;
    env->fmask = 0;
    env->kernelgsbase = 0;
#endif

    // TODO: reset other registers in CPUX86State qemu/target-i386/cpu.h

    // properly initialize internal setup for each mode
    switch (uc->mode) {
    default:
        break;
    case UC_MODE_16:
        env->hflags = 0;
        env->cr[0] = 0;
        // undo the damage done by the memset of env->segs above
        // for R_CS, not quite the same as x86_cpu_reset
        cpu_x86_load_seg_cache(env, R_CS, 0, 0, 0xffff,
                               DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                                   DESC_R_MASK | DESC_A_MASK);
        // remainder yields same state as x86_cpu_reset
        load_seg_16_helper(env, R_DS, 0);
        load_seg_16_helper(env, R_ES, 0);
        load_seg_16_helper(env, R_SS, 0);
        load_seg_16_helper(env, R_FS, 0);
        load_seg_16_helper(env, R_GS, 0);

        break;
    case UC_MODE_32:
        env->hflags |= HF_CS32_MASK | HF_SS32_MASK | HF_OSFXSR_MASK;
#if __Use_Original_Qemu != 1 /* ours (U852) */
        /*
         * NoVmp (ledger U852): SYSENTER is #GP(0) with IA32_SYSENTER_CS[15:2] = 0 (SDM Vol2B; the
         * SDM leaves the MSR's reset value open); a 32-bit OS loads its ring-0 code selector
         * (Windows x86 KGDT_R0_CODE = 08h), and Unicorn's UC_X86_INS_SYSENTER hook relies on
         * SYSENTER running, so the 32-bit reset state has IA32_SYSENTER_CS = 08h (only with
         * CPUID.01H:EDX.SEP, as WRMSR would allow it).
         */
        if (env->features[FEAT_1_EDX] & CPUID_SEP) {
            env->sysenter_cs = 0x08;
        }
#endif /* __Use_Original_Qemu (U852) */
        break;
    case UC_MODE_64:
        env->hflags |= HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK |
                       HF_LMA_MASK | HF_OSFXSR_MASK;
        env->hflags &= ~(HF_ADDSEG_MASK);
        env->efer |= MSR_EFER_LMA | MSR_EFER_LME; // extended mode activated
#if __Use_Original_Qemu != 1 /* ours (U594) */
        /*
         * NoVmp (ledger U594): SYSCALL is #UD with IA32_EFER.SCE = 0 (SDM Vol2B); a 64-bit
         * OS (Windows, Linux) enables it, and Unicorn's UC_X86_INS_SYSCALL hook relies on
         * SYSCALL running, so the 64-bit reset state has SCE = 1 (as WRMSR would allow it:
         * only with CPUID.80000001H:EDX.SYSCALL).
         */
        if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_SYSCALL) {
            env->efer |= MSR_EFER_SCE;
        }
#endif /* __Use_Original_Qemu (U594) */
#if __Use_Original_Qemu != 1 /* ours (U852) */
        /*
         * NoVmp (ledger U852): the same for SYSENTER (#GP(0) with IA32_SYSENTER_CS[15:2] = 0, SDM
         * Vol2B): a 64-bit OS loads its ring-0 code selector (Windows x64 KGDT64_R0_CODE, Linux
         * __KERNEL_CS = 10h), so the 64-bit reset state has IA32_SYSENTER_CS = 10h (with
         * CPUID.01H:EDX.SEP).
         */
        if (env->features[FEAT_1_EDX] & CPUID_SEP) {
            env->sysenter_cs = 0x10;
        }
#endif /* __Use_Original_Qemu (U852) */

        /* If we are operating in 64bit mode then add the Long Mode flag
         * to the CPUID feature flag
         */
        env->features[FEAT_8000_0001_EDX] |= CPUID_EXT2_LM;
        break;
    }

    // CR initialization
    switch (uc->mode) {
    case UC_MODE_32:
    case UC_MODE_64: {
        uint32_t cr4 = 0;

        if (env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE) {
#if __Use_Original_Qemu == 1 /* original QEMU (U67) */
            cr4 |= CR4_OSFXSR_MASK | CR4_OSXSAVE_MASK;
#else /* ours (U67) */
            /* an OS that enables SSE also enables #XM (Windows, Linux) */
            cr4 |= CR4_OSFXSR_MASK | CR4_OSXMMEXCPT_MASK | CR4_OSXSAVE_MASK;
#endif /* __Use_Original_Qemu (U67) */
        }
        if (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_FSGSBASE) {
            cr4 |= CR4_FSGSBASE_MASK;
        }
#if __Use_Original_Qemu != 1 /* ours (U100) */
        /*
         * NoVmp (ledger U100): like OSFXSR above, the OS of a Key Locker
         * capable CPU has enabled it (CR4.KL, Key Locker spec 1.3); IWKey
         * stays all zero (reset) until LOADIWKEY.
         */
        if (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_KeyLocker) {
            cr4 |= CR4_KL_MASK;
        }
#endif /* __Use_Original_Qemu (U100) */
#if __Use_Original_Qemu != 1 /* ours (U104) */
        /* likewise CR4.UINTR on a CPU with user interrupts (SDM Vol3A 9.2) */
        if (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_UINTR) {
            cr4 |= CR4_UINTR_MASK;
        }
#endif /* __Use_Original_Qemu (U104) */

        cpu_x86_update_cr0(env, CR0_PE_MASK); // protected mode
        cpu_x86_update_cr4(env, cr4);
        break;
    }
    default:
        break;
    }
}

#if __Use_Original_Qemu != 1 /* ours (U878) */
/*
 * NoVmp (ledger U878): DR0-DR3 / DR7 through the register API. Into a uc_context image the
 * value is only stored (x86_store_dr would insert the breakpoint into the CPUState the image
 * does not have); uc_context_restore re-inserts the live breakpoints from the restored DR7.
 */
static void x86_reg_store_dr(CPUX86State *env, int reg, target_ulong v)
{
    if (!x86_env_is_live(env)) {
        env->dr[reg] = reg == 7 ? (v | DR7_FIXED_1) : v;
        return;
    }
    x86_store_dr(env, reg, v);
}
#endif /* __Use_Original_Qemu (U878) */

static int x86_msr_read(CPUX86State *env, uc_x86_msr *msr)
{
    uint64_t ecx = env->regs[R_ECX];
    uint64_t eax = env->regs[R_EAX];
    uint64_t edx = env->regs[R_EDX];

    env->regs[R_ECX] = msr->rid;
#if __Use_Original_Qemu == 1 /* original QEMU (U111) */
    helper_rdmsr(env);
#else /* ours (U111) */
    env->msr_api = x86_env_is_live(env) ? 1 : 2;    /* 2: a uc_context image (U878) */
    env->msr_api_err = 0;           /* U905 */
    helper_rdmsr(env);
    env->msr_api = 0;
    if (env->msr_api_err) {         /* U905: #GP(0) for an instruction: no access */
        env->regs[R_EAX] = eax;
        env->regs[R_ECX] = ecx;
        env->regs[R_EDX] = edx;
        return -1;
    }
#endif /* __Use_Original_Qemu (U111) */

    msr->value = ((uint32_t)env->regs[R_EAX]) |
                 ((uint64_t)((uint32_t)env->regs[R_EDX]) << 32);

    env->regs[R_EAX] = eax;
    env->regs[R_ECX] = ecx;
    env->regs[R_EDX] = edx;

    /* The implementation doesn't throw exception or return an error if there is
     * one, so we will return 0.  */
    return 0;
}

static int x86_msr_write(CPUX86State *env, uc_x86_msr *msr)
{
    uint64_t ecx = env->regs[R_ECX];
    uint64_t eax = env->regs[R_EAX];
    uint64_t edx = env->regs[R_EDX];

    env->regs[R_ECX] = msr->rid;
    env->regs[R_EAX] = (unsigned int)msr->value;
    env->regs[R_EDX] = (unsigned int)(msr->value >> 32);
#if __Use_Original_Qemu == 1 /* original QEMU (U111) */
    helper_wrmsr(env);
#else /* ours (U111) */
    env->msr_api = x86_env_is_live(env) ? 1 : 2;    /* 2: a uc_context image (U878) */
    env->msr_api_err = 0;           /* U905 */
    helper_wrmsr(env);
    env->msr_api = 0;
    if (env->msr_api_err) {         /* U905: #GP(0) for an instruction: nothing written */
        env->regs[R_ECX] = ecx;
        env->regs[R_EAX] = eax;
        env->regs[R_EDX] = edx;
        return -1;
    }
#endif /* __Use_Original_Qemu (U111) */

    env->regs[R_ECX] = ecx;
    env->regs[R_EAX] = eax;
    env->regs[R_EDX] = edx;

    /* The implementation doesn't throw exception or return an error if there is
     * one, so we will return 0.  */
    return 0;
}

DEFAULT_VISIBILITY
uc_err reg_read(void *_env, int mode, unsigned int regid, void *value,
                size_t *size)
{
    CPUX86State *env = _env;
    uc_err ret = UC_ERR_ARG;

#if __Use_Original_Qemu != 1 /* ours (U611) */
    /*
     * NoVmp (ledger U611): Intel APX EGPRs R16-R31 and their 32/16/8-bit forms, any mode;
     * UC_ERR_ARG when the CPU model has no APX (UC_CTL_X86_APX).
     */
    if (regid >= UC_X86_REG_R16 && regid <= UC_X86_REG_R31B) {
        unsigned k = regid - UC_X86_REG_R16;
        target_ulong *r = &env->regs[16 + (k & 15)];

        if (!(env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_APX_F)) {
            return UC_ERR_ARG;
        }
        switch (k >> 4) {
        case 0:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(*r);
            break;
        case 1:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(*r);
            break;
        case 2:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(*r);
            break;
        default:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(*r);
            break;
        }
        return ret;
    }
#endif /* __Use_Original_Qemu (U611) */

    switch (regid) {
    default:
        break;
    case UC_X86_REG_FP0:
    case UC_X86_REG_FP1:
    case UC_X86_REG_FP2:
    case UC_X86_REG_FP3:
    case UC_X86_REG_FP4:
    case UC_X86_REG_FP5:
    case UC_X86_REG_FP6:
    case UC_X86_REG_FP7: {
        CHECK_REG_TYPE(char[10]);
        floatx80 reg = env->fpregs[regid - UC_X86_REG_FP0].d;
        cpu_get_fp80(value, (uint16_t *)((char *)value + sizeof(uint64_t)),
                     reg);
        return ret;
    }
    case UC_X86_REG_FPSW: {
        CHECK_REG_TYPE(uint16_t);
        uint16_t fpus = env->fpus;
        fpus = fpus & ~0x3800;
        fpus |= (env->fpstt & 0x7) << 11;
        *(uint16_t *)value = fpus;
        return ret;
    }
    case UC_X86_REG_FPCW:
        CHECK_REG_TYPE(uint16_t);
        *(uint16_t *)value = env->fpuc;
        return ret;
    case UC_X86_REG_FPTAG: {
        CHECK_REG_TYPE(uint16_t);
#define EXPD(fp) (fp.l.upper & 0x7fff)
#define MANTD(fp) (fp.l.lower)
#define MAXEXPD 0x7fff
        int fptag, exp, i;
        uint64_t mant;
        CPU_LDoubleU tmp;
        fptag = 0;
        for (i = 7; i >= 0; i--) {
            fptag <<= 2;
            if (env->fptags[i]) {
                fptag |= 3;
            } else {
                tmp.d = env->fpregs[i].d;
                exp = EXPD(tmp);
                mant = MANTD(tmp);
                if (exp == 0 && mant == 0) {
                    /* zero */
                    fptag |= 1;
                } else if (exp == 0 || exp == MAXEXPD ||
                           (mant & (1LL << 63)) == 0) {
                    /* NaNs, infinity, denormal */
                    fptag |= 2;
                }
            }
        }
        *(uint16_t *)value = fptag;
        return ret;
    }
    case UC_X86_REG_K0:
    case UC_X86_REG_K1:
    case UC_X86_REG_K2:
    case UC_X86_REG_K3:
    case UC_X86_REG_K4:
    case UC_X86_REG_K5:
    case UC_X86_REG_K6:
    case UC_X86_REG_K7:
        CHECK_REG_TYPE(uint64_t);
        *(uint64_t *)value = env->opmask_regs[regid - UC_X86_REG_K0];
        return ret;
    case UC_X86_REG_XMM0:
    case UC_X86_REG_XMM1:
    case UC_X86_REG_XMM2:
    case UC_X86_REG_XMM3:
    case UC_X86_REG_XMM4:
    case UC_X86_REG_XMM5:
    case UC_X86_REG_XMM6:
    case UC_X86_REG_XMM7: {
        CHECK_REG_TYPE(uint64_t[2]);
        uint64_t *dst = (uint64_t *)value;
        const ZMMReg *const reg = &env->xmm_regs[regid - UC_X86_REG_XMM0];
        dst[0] = reg->ZMM_Q(0);
        dst[1] = reg->ZMM_Q(1);
        return ret;
    }
    case UC_X86_REG_ST0:
    case UC_X86_REG_ST1:
    case UC_X86_REG_ST2:
    case UC_X86_REG_ST3:
    case UC_X86_REG_ST4:
    case UC_X86_REG_ST5:
    case UC_X86_REG_ST6:
    case UC_X86_REG_ST7: {
        CHECK_REG_TYPE(char[10]);
        memcpy(value, &FPST(regid - UC_X86_REG_ST0), 10);
        return ret;
    }
    case UC_X86_REG_YMM0:
    case UC_X86_REG_YMM1:
    case UC_X86_REG_YMM2:
    case UC_X86_REG_YMM3:
    case UC_X86_REG_YMM4:
    case UC_X86_REG_YMM5:
    case UC_X86_REG_YMM6:
    case UC_X86_REG_YMM7: {
        CHECK_REG_TYPE(uint64_t[4]);
        uint64_t *dst = (uint64_t *)value;
        const ZMMReg *const reg = &env->xmm_regs[regid - UC_X86_REG_YMM0];
        dst[0] = reg->ZMM_Q(0);
        dst[1] = reg->ZMM_Q(1);
        dst[2] = reg->ZMM_Q(2);
        dst[3] = reg->ZMM_Q(3);
        return ret;
    }
#if __Use_Original_Qemu != 1 /* ours (U123) */
    /* NoVmp (ledger U123): ZMM0-7 exist in every mode, like XMM0-7 and YMM0-7 */
    case UC_X86_REG_ZMM0:
    case UC_X86_REG_ZMM1:
    case UC_X86_REG_ZMM2:
    case UC_X86_REG_ZMM3:
    case UC_X86_REG_ZMM4:
    case UC_X86_REG_ZMM5:
    case UC_X86_REG_ZMM6:
    case UC_X86_REG_ZMM7: {
        CHECK_REG_TYPE(uint64_t[8]);
        uint64_t *dst = (uint64_t *)value;
        const ZMMReg *const reg = &env->xmm_regs[regid - UC_X86_REG_ZMM0];
        int i;
        for (i = 0; i < 8; i++) {
            dst[i] = reg->ZMM_Q(i);
        }
        return ret;
    }
#endif /* __Use_Original_Qemu (U123) */

    case UC_X86_REG_FIP:
        CHECK_REG_TYPE(uint64_t);
        *(uint64_t *)value = env->fpip;
        return ret;
    case UC_X86_REG_FCS:
        CHECK_REG_TYPE(uint16_t);
        *(uint16_t *)value = env->fpcs;
        return ret;
    case UC_X86_REG_FDP:
        CHECK_REG_TYPE(uint64_t);
        *(uint64_t *)value = env->fpdp;
        return ret;
    case UC_X86_REG_FDS:
        CHECK_REG_TYPE(uint16_t);
        *(uint16_t *)value = env->fpds;
        return ret;
    case UC_X86_REG_FOP:
        CHECK_REG_TYPE(uint16_t);
        *(uint16_t *)value = env->fpop;
        return ret;
#if __Use_Original_Qemu != 1 /* ours (U114) */
    case UC_X86_REG_SSP:
        CHECK_REG_TYPE(uint64_t);
        *(uint64_t *)value = env->ssp;
        return ret;
#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U174) */
    /* NoVmp (ledger U174): AMX TILECFG (64 bytes) and TMM0-7 (1 KB each), any mode */
    case UC_X86_REG_TILECFG:
        CHECK_REG_TYPE(uint8_t[64]);
        memcpy(value, env->xtilecfg, sizeof(env->xtilecfg));
        return ret;
    case UC_X86_REG_TMM0:
    case UC_X86_REG_TMM1:
    case UC_X86_REG_TMM2:
    case UC_X86_REG_TMM3:
    case UC_X86_REG_TMM4:
    case UC_X86_REG_TMM5:
    case UC_X86_REG_TMM6:
    case UC_X86_REG_TMM7:
        CHECK_REG_TYPE(uint8_t[1024]);
        memcpy(value, env->xtiledata + 1024 * (regid - UC_X86_REG_TMM0), 1024);
        return ret;
#endif /* __Use_Original_Qemu (U174) */
#if __Use_Original_Qemu != 1 /* ours (U830) */
    /* NoVmp (ledger U830): MMn = bits 63:0 of the physical register Rn (SDM Vol1 9.5) */
    case UC_X86_REG_MM0:
    case UC_X86_REG_MM1:
    case UC_X86_REG_MM2:
    case UC_X86_REG_MM3:
    case UC_X86_REG_MM4:
    case UC_X86_REG_MM5:
    case UC_X86_REG_MM6:
    case UC_X86_REG_MM7:
        CHECK_REG_TYPE(uint64_t);
        *(uint64_t *)value = env->fpregs[regid - UC_X86_REG_MM0].mmx.MMX_Q(0);
        return ret;
#endif /* __Use_Original_Qemu (U830) */
#if __Use_Original_Qemu != 1 /* ours (U831) */
    /* NoVmp (ledger U831): PKRU as RDPKRU returns it (EAX; EDX = 0), CPUs with PKU only */
    case UC_X86_REG_PKRU:
        if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKU)) {
            return UC_ERR_ARG;
        }
        CHECK_REG_TYPE(uint32_t);
        *(uint32_t *)value = env->pkru;
        return ret;
#endif /* __Use_Original_Qemu (U831) */
    }

    switch (mode) {
    default:
        break;
    case UC_MODE_16:
        switch (regid) {
        default:
            break;
        case UC_X86_REG_ES:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = env->segs[R_ES].selector;
            return ret;
        case UC_X86_REG_SS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = env->segs[R_SS].selector;
            return ret;
        case UC_X86_REG_DS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = env->segs[R_DS].selector;
            return ret;
        case UC_X86_REG_FS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = env->segs[R_FS].selector;
            return ret;
        case UC_X86_REG_GS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = env->segs[R_GS].selector;
            return ret;
        case UC_X86_REG_FS_BASE:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = (uint32_t)env->segs[R_FS].base;
            return ret;
        }
        // fall-thru
    case UC_MODE_32:
        switch (regid) {
        default:
            break;
        case UC_X86_REG_CR0:
        case UC_X86_REG_CR1:
        case UC_X86_REG_CR2:
        case UC_X86_REG_CR3:
        case UC_X86_REG_CR4:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->cr[regid - UC_X86_REG_CR0];
            break;
        case UC_X86_REG_DR0:
        case UC_X86_REG_DR1:
        case UC_X86_REG_DR2:
        case UC_X86_REG_DR3:
        case UC_X86_REG_DR4:
        case UC_X86_REG_DR5:
        case UC_X86_REG_DR6:
        case UC_X86_REG_DR7:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->dr[regid - UC_X86_REG_DR0];
            break;
        case UC_X86_REG_FLAGS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = cpu_compute_eflags(env);
            break;
        case UC_X86_REG_EFLAGS:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = cpu_compute_eflags(env);
            break;
        case UC_X86_REG_EAX:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->regs[R_EAX];
            break;
        case UC_X86_REG_AX:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EAX]);
            break;
        case UC_X86_REG_AH:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_H(env->regs[R_EAX]);
            break;
        case UC_X86_REG_AL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_EAX]);
            break;
        case UC_X86_REG_EBX:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->regs[R_EBX];
            break;
        case UC_X86_REG_BX:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EBX]);
            break;
        case UC_X86_REG_BH:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_H(env->regs[R_EBX]);
            break;
        case UC_X86_REG_BL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_EBX]);
            break;
        case UC_X86_REG_ECX:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->regs[R_ECX];
            break;
        case UC_X86_REG_CX:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_ECX]);
            break;
        case UC_X86_REG_CH:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_H(env->regs[R_ECX]);
            break;
        case UC_X86_REG_CL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_ECX]);
            break;
        case UC_X86_REG_EDX:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->regs[R_EDX];
            break;
        case UC_X86_REG_DX:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EDX]);
            break;
        case UC_X86_REG_DH:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_H(env->regs[R_EDX]);
            break;
        case UC_X86_REG_DL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_EDX]);
            break;
        case UC_X86_REG_ESP:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->regs[R_ESP];
            break;
        case UC_X86_REG_SP:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_ESP]);
            break;
        case UC_X86_REG_EBP:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->regs[R_EBP];
            break;
        case UC_X86_REG_BP:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EBP]);
            break;
        case UC_X86_REG_ESI:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->regs[R_ESI];
            break;
        case UC_X86_REG_SI:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_ESI]);
            break;
        case UC_X86_REG_EDI:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->regs[R_EDI];
            break;
        case UC_X86_REG_DI:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EDI]);
            break;
        case UC_X86_REG_EIP:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = env->eip;
            break;
        case UC_X86_REG_IP:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->eip);
            break;
        case UC_X86_REG_CS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_CS].selector;
            break;
        case UC_X86_REG_DS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_DS].selector;
            break;
        case UC_X86_REG_SS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_SS].selector;
            break;
        case UC_X86_REG_ES:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_ES].selector;
            break;
        case UC_X86_REG_FS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_FS].selector;
            break;
        case UC_X86_REG_GS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_GS].selector;
            break;
        case UC_X86_REG_IDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            ((uc_x86_mmr *)value)->limit = (uint16_t)env->idt.limit;
            ((uc_x86_mmr *)value)->base = (uint32_t)env->idt.base;
            break;
        case UC_X86_REG_GDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            ((uc_x86_mmr *)value)->limit = (uint16_t)env->gdt.limit;
            ((uc_x86_mmr *)value)->base = (uint32_t)env->gdt.base;
            break;
        case UC_X86_REG_LDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            ((uc_x86_mmr *)value)->limit = env->ldt.limit;
            ((uc_x86_mmr *)value)->base = (uint32_t)env->ldt.base;
            ((uc_x86_mmr *)value)->selector = (uint16_t)env->ldt.selector;
            ((uc_x86_mmr *)value)->flags = env->ldt.flags;
            break;
        case UC_X86_REG_TR:
            CHECK_REG_TYPE(uc_x86_mmr);
            ((uc_x86_mmr *)value)->limit = env->tr.limit;
            ((uc_x86_mmr *)value)->base = (uint32_t)env->tr.base;
            ((uc_x86_mmr *)value)->selector = (uint16_t)env->tr.selector;
            ((uc_x86_mmr *)value)->flags = env->tr.flags;
            break;
        case UC_X86_REG_MSR:
            CHECK_REG_TYPE(uc_x86_msr);
#if __Use_Original_Qemu == 1 /* original QEMU (U905) */
            x86_msr_read(env, (uc_x86_msr *)value);
#else /* ours (U905) */
            if (x86_msr_read(env, (uc_x86_msr *)value)) {
                /* the instruction would #GP(0): no such MSR or a refused value (U905);
                   not UC_ERR_ARG, which CHECK_RET_DEPRECATE turns into UC_ERR_OK */
                ret = UC_ERR_EXCEPTION;
            }
#endif /* __Use_Original_Qemu (U905) */
            break;
        case UC_X86_REG_MXCSR:
            CHECK_REG_TYPE(uint32_t);
#if __Use_Original_Qemu == 1 /* original QEMU (U447) */
            *(uint32_t *)value = env->mxcsr;
#else /* ours (U447) */
            /*
             * NoVmp (ledger U447): SSE/AVX instructions leave their new
             * exception flags in sse_status; fold them in as STMXCSR /
             * FXSAVE / XSAVE do, or the API shows stale MXCSR flags.
             */
            update_mxcsr_from_sse_status(env);
            *(uint32_t *)value = env->mxcsr;
#endif /* __Use_Original_Qemu (U447) */
            break;
        case UC_X86_REG_FS_BASE:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = (uint32_t)env->segs[R_FS].base;
            break;
        case UC_X86_REG_XCR0:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->xcr0;
            break;
        }
        break;

#ifdef TARGET_X86_64
    case UC_MODE_64:
        switch (regid) {
        default:
            break;
        case UC_X86_REG_CR0:
        case UC_X86_REG_CR1:
        case UC_X86_REG_CR2:
        case UC_X86_REG_CR3:
        case UC_X86_REG_CR4:
        case UC_X86_REG_CR8:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = env->cr[regid - UC_X86_REG_CR0];
            break;
        case UC_X86_REG_DR0:
        case UC_X86_REG_DR1:
        case UC_X86_REG_DR2:
        case UC_X86_REG_DR3:
        case UC_X86_REG_DR4:
        case UC_X86_REG_DR5:
        case UC_X86_REG_DR6:
        case UC_X86_REG_DR7:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = env->dr[regid - UC_X86_REG_DR0];
            break;
        case UC_X86_REG_FLAGS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = cpu_compute_eflags(env);
            break;
        case UC_X86_REG_EFLAGS:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = cpu_compute_eflags(env);
            break;
        case UC_X86_REG_RFLAGS:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = cpu_compute_eflags(env);
            break;
        case UC_X86_REG_RAX:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->regs[R_EAX];
            break;
        case UC_X86_REG_EAX:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[R_EAX]);
            break;
        case UC_X86_REG_AX:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EAX]);
            break;
        case UC_X86_REG_AH:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_H(env->regs[R_EAX]);
            break;
        case UC_X86_REG_AL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_EAX]);
            break;
        case UC_X86_REG_RBX:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->regs[R_EBX];
            break;
        case UC_X86_REG_EBX:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[R_EBX]);
            break;
        case UC_X86_REG_BX:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EBX]);
            break;
        case UC_X86_REG_BH:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_H(env->regs[R_EBX]);
            break;
        case UC_X86_REG_BL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_EBX]);
            break;
        case UC_X86_REG_RCX:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->regs[R_ECX];
            break;
        case UC_X86_REG_ECX:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[R_ECX]);
            break;
        case UC_X86_REG_CX:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_ECX]);
            break;
        case UC_X86_REG_CH:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_H(env->regs[R_ECX]);
            break;
        case UC_X86_REG_CL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_ECX]);
            break;
        case UC_X86_REG_RDX:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->regs[R_EDX];
            break;
        case UC_X86_REG_EDX:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[R_EDX]);
            break;
        case UC_X86_REG_DX:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EDX]);
            break;
        case UC_X86_REG_DH:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_H(env->regs[R_EDX]);
            break;
        case UC_X86_REG_DL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_EDX]);
            break;
        case UC_X86_REG_RSP:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->regs[R_ESP];
            break;
        case UC_X86_REG_ESP:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[R_ESP]);
            break;
        case UC_X86_REG_SP:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_ESP]);
            break;
        case UC_X86_REG_SPL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_ESP]);
            break;
        case UC_X86_REG_RBP:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->regs[R_EBP];
            break;
        case UC_X86_REG_EBP:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[R_EBP]);
            break;
        case UC_X86_REG_BP:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EBP]);
            break;
        case UC_X86_REG_BPL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_EBP]);
            break;
        case UC_X86_REG_RSI:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->regs[R_ESI];
            break;
        case UC_X86_REG_ESI:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[R_ESI]);
            break;
        case UC_X86_REG_SI:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_ESI]);
            break;
        case UC_X86_REG_SIL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_ESI]);
            break;
        case UC_X86_REG_RDI:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->regs[R_EDI];
            break;
        case UC_X86_REG_EDI:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[R_EDI]);
            break;
        case UC_X86_REG_DI:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[R_EDI]);
            break;
        case UC_X86_REG_DIL:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[R_EDI]);
            break;
        case UC_X86_REG_RIP:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->eip;
            break;
        case UC_X86_REG_EIP:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->eip);
            break;
        case UC_X86_REG_IP:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->eip);
            break;
        case UC_X86_REG_CS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_CS].selector;
            break;
        case UC_X86_REG_DS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_DS].selector;
            break;
        case UC_X86_REG_SS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_SS].selector;
            break;
        case UC_X86_REG_ES:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_ES].selector;
            break;
        case UC_X86_REG_FS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_FS].selector;
            break;
        case UC_X86_REG_GS:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = (uint16_t)env->segs[R_GS].selector;
            break;
        case UC_X86_REG_R8:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(env->regs[8]);
            break;
        case UC_X86_REG_R8D:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[8]);
            break;
        case UC_X86_REG_R8W:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[8]);
            break;
        case UC_X86_REG_R8B:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[8]);
            break;
        case UC_X86_REG_R9:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(env->regs[9]);
            break;
        case UC_X86_REG_R9D:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[9]);
            break;
        case UC_X86_REG_R9W:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[9]);
            break;
        case UC_X86_REG_R9B:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[9]);
            break;
        case UC_X86_REG_R10:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(env->regs[10]);
            break;
        case UC_X86_REG_R10D:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[10]);
            break;
        case UC_X86_REG_R10W:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[10]);
            break;
        case UC_X86_REG_R10B:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[10]);
            break;
        case UC_X86_REG_R11:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(env->regs[11]);
            break;
        case UC_X86_REG_R11D:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[11]);
            break;
        case UC_X86_REG_R11W:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[11]);
            break;
        case UC_X86_REG_R11B:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[11]);
            break;
        case UC_X86_REG_R12:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(env->regs[12]);
            break;
        case UC_X86_REG_R12D:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[12]);
            break;
        case UC_X86_REG_R12W:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[12]);
            break;
        case UC_X86_REG_R12B:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[12]);
            break;
        case UC_X86_REG_R13:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(env->regs[13]);
            break;
        case UC_X86_REG_R13D:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[13]);
            break;
        case UC_X86_REG_R13W:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[13]);
            break;
        case UC_X86_REG_R13B:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[13]);
            break;
        case UC_X86_REG_R14:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(env->regs[14]);
            break;
        case UC_X86_REG_R14D:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[14]);
            break;
        case UC_X86_REG_R14W:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[14]);
            break;
        case UC_X86_REG_R14B:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[14]);
            break;
        case UC_X86_REG_R15:
            CHECK_REG_TYPE(int64_t);
            *(int64_t *)value = READ_QWORD(env->regs[15]);
            break;
        case UC_X86_REG_R15D:
            CHECK_REG_TYPE(int32_t);
            *(int32_t *)value = READ_DWORD(env->regs[15]);
            break;
        case UC_X86_REG_R15W:
            CHECK_REG_TYPE(int16_t);
            *(int16_t *)value = READ_WORD(env->regs[15]);
            break;
        case UC_X86_REG_R15B:
            CHECK_REG_TYPE(int8_t);
            *(int8_t *)value = READ_BYTE_L(env->regs[15]);
            break;
        case UC_X86_REG_IDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            ((uc_x86_mmr *)value)->limit = (uint16_t)env->idt.limit;
            ((uc_x86_mmr *)value)->base = env->idt.base;
            break;
        case UC_X86_REG_GDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            ((uc_x86_mmr *)value)->limit = (uint16_t)env->gdt.limit;
            ((uc_x86_mmr *)value)->base = env->gdt.base;
            break;
        case UC_X86_REG_LDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            ((uc_x86_mmr *)value)->limit = env->ldt.limit;
            ((uc_x86_mmr *)value)->base = env->ldt.base;
            ((uc_x86_mmr *)value)->selector = (uint16_t)env->ldt.selector;
            ((uc_x86_mmr *)value)->flags = env->ldt.flags;
            break;
        case UC_X86_REG_TR:
            CHECK_REG_TYPE(uc_x86_mmr);
            ((uc_x86_mmr *)value)->limit = env->tr.limit;
            ((uc_x86_mmr *)value)->base = env->tr.base;
            ((uc_x86_mmr *)value)->selector = (uint16_t)env->tr.selector;
            ((uc_x86_mmr *)value)->flags = env->tr.flags;
            break;
        case UC_X86_REG_MSR:
            CHECK_REG_TYPE(uc_x86_msr);
#if __Use_Original_Qemu == 1 /* original QEMU (U905) */
            x86_msr_read(env, (uc_x86_msr *)value);
#else /* ours (U905) */
            if (x86_msr_read(env, (uc_x86_msr *)value)) {
                /* the instruction would #GP(0): no such MSR or a refused value (U905);
                   not UC_ERR_ARG, which CHECK_RET_DEPRECATE turns into UC_ERR_OK */
                ret = UC_ERR_EXCEPTION;
            }
#endif /* __Use_Original_Qemu (U905) */
            break;
        case UC_X86_REG_MXCSR:
            CHECK_REG_TYPE(uint32_t);
#if __Use_Original_Qemu == 1 /* original QEMU (U447) */
            *(uint32_t *)value = env->mxcsr;
#else /* ours (U447) */
            /*
             * NoVmp (ledger U447): SSE/AVX instructions leave their new
             * exception flags in sse_status; fold them in as STMXCSR /
             * FXSAVE / XSAVE do, or the API shows stale MXCSR flags.
             */
            update_mxcsr_from_sse_status(env);
            *(uint32_t *)value = env->mxcsr;
#endif /* __Use_Original_Qemu (U447) */
            break;
        case UC_X86_REG_XMM8:
        case UC_X86_REG_XMM9:
        case UC_X86_REG_XMM10:
        case UC_X86_REG_XMM11:
        case UC_X86_REG_XMM12:
        case UC_X86_REG_XMM13:
        case UC_X86_REG_XMM14:
        case UC_X86_REG_XMM15:
        case UC_X86_REG_XMM16:
        case UC_X86_REG_XMM17:
        case UC_X86_REG_XMM18:
        case UC_X86_REG_XMM19:
        case UC_X86_REG_XMM20:
        case UC_X86_REG_XMM21:
        case UC_X86_REG_XMM22:
        case UC_X86_REG_XMM23:
        case UC_X86_REG_XMM24:
        case UC_X86_REG_XMM25:
        case UC_X86_REG_XMM26:
        case UC_X86_REG_XMM27:
        case UC_X86_REG_XMM28:
        case UC_X86_REG_XMM29:
        case UC_X86_REG_XMM30:
        case UC_X86_REG_XMM31: {
            CHECK_REG_TYPE(uint64_t[2]);
            uint64_t *dst = (uint64_t *)value;
            const ZMMReg *const reg = &env->xmm_regs[regid - UC_X86_REG_XMM0];
            dst[0] = reg->ZMM_Q(0);
            dst[1] = reg->ZMM_Q(1);
            break;
        }
        case UC_X86_REG_YMM8:
        case UC_X86_REG_YMM9:
        case UC_X86_REG_YMM10:
        case UC_X86_REG_YMM11:
        case UC_X86_REG_YMM12:
        case UC_X86_REG_YMM13:
        case UC_X86_REG_YMM14:
        case UC_X86_REG_YMM15:
        case UC_X86_REG_YMM16:
        case UC_X86_REG_YMM17:
        case UC_X86_REG_YMM18:
        case UC_X86_REG_YMM19:
        case UC_X86_REG_YMM20:
        case UC_X86_REG_YMM21:
        case UC_X86_REG_YMM22:
        case UC_X86_REG_YMM23:
        case UC_X86_REG_YMM24:
        case UC_X86_REG_YMM25:
        case UC_X86_REG_YMM26:
        case UC_X86_REG_YMM27:
        case UC_X86_REG_YMM28:
        case UC_X86_REG_YMM29:
        case UC_X86_REG_YMM30:
        case UC_X86_REG_YMM31: {
            CHECK_REG_TYPE(uint64_t[4]);
            uint64_t *dst = (uint64_t *)value;
            const ZMMReg *const reg = &env->xmm_regs[regid - UC_X86_REG_YMM0];
            dst[0] = reg->ZMM_Q(0);
            dst[1] = reg->ZMM_Q(1);
            dst[2] = reg->ZMM_Q(2);
            dst[3] = reg->ZMM_Q(3);
            break;
        }
        case UC_X86_REG_ZMM0:
        case UC_X86_REG_ZMM1:
        case UC_X86_REG_ZMM2:
        case UC_X86_REG_ZMM3:
        case UC_X86_REG_ZMM4:
        case UC_X86_REG_ZMM5:
        case UC_X86_REG_ZMM6:
        case UC_X86_REG_ZMM7:
        case UC_X86_REG_ZMM8:
        case UC_X86_REG_ZMM9:
        case UC_X86_REG_ZMM10:
        case UC_X86_REG_ZMM11:
        case UC_X86_REG_ZMM12:
        case UC_X86_REG_ZMM13:
        case UC_X86_REG_ZMM14:
        case UC_X86_REG_ZMM15:
        case UC_X86_REG_ZMM16:
        case UC_X86_REG_ZMM17:
        case UC_X86_REG_ZMM18:
        case UC_X86_REG_ZMM19:
        case UC_X86_REG_ZMM20:
        case UC_X86_REG_ZMM21:
        case UC_X86_REG_ZMM22:
        case UC_X86_REG_ZMM23:
        case UC_X86_REG_ZMM24:
        case UC_X86_REG_ZMM25:
        case UC_X86_REG_ZMM26:
        case UC_X86_REG_ZMM27:
        case UC_X86_REG_ZMM28:
        case UC_X86_REG_ZMM29:
        case UC_X86_REG_ZMM30:
        case UC_X86_REG_ZMM31: {
            CHECK_REG_TYPE(uint64_t[8]);
            uint64_t *dst = (uint64_t *)value;
            const ZMMReg *const reg = &env->xmm_regs[regid - UC_X86_REG_ZMM0];
            dst[0] = reg->ZMM_Q(0);
            dst[1] = reg->ZMM_Q(1);
            dst[2] = reg->ZMM_Q(2);
            dst[3] = reg->ZMM_Q(3);
            dst[4] = reg->ZMM_Q(4);
            dst[5] = reg->ZMM_Q(5);
            dst[6] = reg->ZMM_Q(6);
            dst[7] = reg->ZMM_Q(7);
            break;
        }
        case UC_X86_REG_FS_BASE:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = (uint64_t)env->segs[R_FS].base;
            break;
        case UC_X86_REG_GS_BASE:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = (uint64_t)env->segs[R_GS].base;
            break;
        case UC_X86_REG_XCR0:
            CHECK_REG_TYPE(uint64_t);
            *(uint64_t *)value = env->xcr0;
            break;
        }
        break;
#endif
    }

    CHECK_RET_DEPRECATE(ret, regid);
    return ret;
}

DEFAULT_VISIBILITY
uc_err reg_write(void *_env, int mode, unsigned int regid, const void *value,
                 size_t *size, int *setpc)
{
    CPUX86State *env = _env;
    uc_err ret = UC_ERR_ARG;

#if __Use_Original_Qemu != 1 /* ours (U611) */
    /* NoVmp (ledger U611): Intel APX EGPRs R16-R31 (see reg_read) */
    if (regid >= UC_X86_REG_R16 && regid <= UC_X86_REG_R31B) {
        unsigned k = regid - UC_X86_REG_R16;
        target_ulong *r = &env->regs[16 + (k & 15)];

        if (!(env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_APX_F)) {
            return UC_ERR_ARG;
        }
        switch (k >> 4) {
        case 0:
            CHECK_REG_TYPE(uint64_t);
            *r = *(uint64_t *)value;
            break;
        case 1:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(*r, *(uint32_t *)value);
            break;
        case 2:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(*r, *(uint16_t *)value);
            break;
        default:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(*r, *(uint8_t *)value);
            break;
        }
        return ret;
    }
#endif /* __Use_Original_Qemu (U611) */

    switch (regid) {
    default:
        break;
    case UC_X86_REG_FP0:
    case UC_X86_REG_FP1:
    case UC_X86_REG_FP2:
    case UC_X86_REG_FP3:
    case UC_X86_REG_FP4:
    case UC_X86_REG_FP5:
    case UC_X86_REG_FP6:
    case UC_X86_REG_FP7: {
        CHECK_REG_TYPE(char[10]);
        uint64_t mant = *(uint64_t *)value;
        uint16_t upper = *(uint16_t *)((char *)value + sizeof(uint64_t));
        env->fpregs[regid - UC_X86_REG_FP0].d = cpu_set_fp80(mant, upper);
        return ret;
    }
    case UC_X86_REG_FPSW: {
        CHECK_REG_TYPE(uint16_t);
        uint16_t fpus = *(uint16_t *)value;
        env->fpus = fpus & ~0x3800;
        env->fpstt = (fpus >> 11) & 0x7;
        return ret;
    }
    case UC_X86_REG_FPCW:
        CHECK_REG_TYPE(uint16_t);
        cpu_set_fpuc(env, *(uint16_t *)value);
        return ret;
    case UC_X86_REG_FPTAG: {
        CHECK_REG_TYPE(uint16_t);
        int i;
        uint16_t fptag = *(uint16_t *)value;
        for (i = 0; i < 8; i++) {
            env->fptags[i] = ((fptag & 3) == 3);
            fptag >>= 2;
        }

        return ret;
    }
    case UC_X86_REG_K0:
    case UC_X86_REG_K1:
    case UC_X86_REG_K2:
    case UC_X86_REG_K3:
    case UC_X86_REG_K4:
    case UC_X86_REG_K5:
    case UC_X86_REG_K6:
    case UC_X86_REG_K7:
        CHECK_REG_TYPE(uint64_t);
        env->opmask_regs[regid - UC_X86_REG_K0] = *(uint64_t *)value;
        return ret;
    case UC_X86_REG_XMM0:
    case UC_X86_REG_XMM1:
    case UC_X86_REG_XMM2:
    case UC_X86_REG_XMM3:
    case UC_X86_REG_XMM4:
    case UC_X86_REG_XMM5:
    case UC_X86_REG_XMM6:
    case UC_X86_REG_XMM7: {
        CHECK_REG_TYPE(uint64_t[2]);
        const uint64_t *src = (const uint64_t *)value;
        ZMMReg *reg = &env->xmm_regs[regid - UC_X86_REG_XMM0];
        reg->ZMM_Q(0) = src[0];
        reg->ZMM_Q(1) = src[1];
        return ret;
    }
    case UC_X86_REG_ST0:
    case UC_X86_REG_ST1:
    case UC_X86_REG_ST2:
    case UC_X86_REG_ST3:
    case UC_X86_REG_ST4:
    case UC_X86_REG_ST5:
    case UC_X86_REG_ST6:
    case UC_X86_REG_ST7: {
        CHECK_REG_TYPE(char[10]);
        memcpy(&FPST(regid - UC_X86_REG_ST0), value, 10);
        return ret;
    }
    case UC_X86_REG_YMM0:
    case UC_X86_REG_YMM1:
    case UC_X86_REG_YMM2:
    case UC_X86_REG_YMM3:
    case UC_X86_REG_YMM4:
    case UC_X86_REG_YMM5:
    case UC_X86_REG_YMM6:
    case UC_X86_REG_YMM7: {
        CHECK_REG_TYPE(uint64_t[4]);
        const uint64_t *src = (const uint64_t *)value;
        ZMMReg *reg = &env->xmm_regs[regid - UC_X86_REG_YMM0];
        reg->ZMM_Q(0) = src[0];
        reg->ZMM_Q(1) = src[1];
        reg->ZMM_Q(2) = src[2];
        reg->ZMM_Q(3) = src[3];
        return ret;
    }
#if __Use_Original_Qemu != 1 /* ours (U123) */
    /* NoVmp (ledger U123): ZMM0-7 exist in every mode, like XMM0-7 and YMM0-7 */
    case UC_X86_REG_ZMM0:
    case UC_X86_REG_ZMM1:
    case UC_X86_REG_ZMM2:
    case UC_X86_REG_ZMM3:
    case UC_X86_REG_ZMM4:
    case UC_X86_REG_ZMM5:
    case UC_X86_REG_ZMM6:
    case UC_X86_REG_ZMM7: {
        CHECK_REG_TYPE(uint64_t[8]);
        const uint64_t *src = (const uint64_t *)value;
        ZMMReg *reg = &env->xmm_regs[regid - UC_X86_REG_ZMM0];
        int i;
        for (i = 0; i < 8; i++) {
            reg->ZMM_Q(i) = src[i];
        }
        return ret;
    }
#endif /* __Use_Original_Qemu (U123) */

    case UC_X86_REG_FIP:
        CHECK_REG_TYPE(uint64_t);
        env->fpip = *(uint64_t *)value;
        return ret;
    case UC_X86_REG_FCS:
        CHECK_REG_TYPE(uint16_t);
        env->fpcs = *(uint16_t *)value;
        return ret;
    case UC_X86_REG_FDP:
        CHECK_REG_TYPE(uint64_t);
        env->fpdp = *(uint64_t *)value;
        return ret;
    case UC_X86_REG_FDS:
        CHECK_REG_TYPE(uint16_t);
        env->fpds = *(uint16_t *)value;
        return ret;
    case UC_X86_REG_FOP:
        CHECK_REG_TYPE(uint16_t);
        env->fpop = *(uint16_t *)value;
        return ret;
#if __Use_Original_Qemu != 1 /* ours (U114) */
    case UC_X86_REG_SSP:
        CHECK_REG_TYPE(uint64_t);
        env->ssp = *(uint64_t *)value;
        return ret;
#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U174) */
    /*
     * NoVmp (ledger U174): TILECFG is loaded like XRSTOR loads it (SDM Vol1 13.5.14): an
     * image LDTILECFG would #GP on, or palette 0, leaves the INIT state; TMMn raw.
     */
    case UC_X86_REG_TILECFG: {
        const uint8_t *buf = (const uint8_t *)value;
        CHECK_REG_TYPE(uint8_t[64]);
        if (buf[0] != 0 && x86_amx_tilecfg_ok(buf, env->xcr0)) {
            memcpy(env->xtilecfg, buf, sizeof(env->xtilecfg));
        } else {
            memset(env->xtilecfg, 0, sizeof(env->xtilecfg));
        }
        return ret;
    }
    case UC_X86_REG_TMM0:
    case UC_X86_REG_TMM1:
    case UC_X86_REG_TMM2:
    case UC_X86_REG_TMM3:
    case UC_X86_REG_TMM4:
    case UC_X86_REG_TMM5:
    case UC_X86_REG_TMM6:
    case UC_X86_REG_TMM7:
        CHECK_REG_TYPE(uint8_t[1024]);
        memcpy(env->xtiledata + 1024 * (regid - UC_X86_REG_TMM0), value, 1024);
        return ret;
#endif /* __Use_Original_Qemu (U174) */
#if __Use_Original_Qemu != 1 /* ours (U830) */
    /*
     * NoVmp (ledger U830): MMn -> bits 63:0 of Rn; bits 79:64 become all 1s, as when an MMX
     * instruction writes MMn (SDM Vol1 9.6.2). TOP and the tag word are not touched: the SDM
     * sets them on the execution of an MMX instruction (9.5.1, 9.6.2), and none executes.
     */
    case UC_X86_REG_MM0:
    case UC_X86_REG_MM1:
    case UC_X86_REG_MM2:
    case UC_X86_REG_MM3:
    case UC_X86_REG_MM4:
    case UC_X86_REG_MM5:
    case UC_X86_REG_MM6:
    case UC_X86_REG_MM7: {
        FPReg *r = &env->fpregs[regid - UC_X86_REG_MM0];
        CHECK_REG_TYPE(uint64_t);
        r->mmx.MMX_Q(0) = *(uint64_t *)value;
        r->d.high = 0xffff;
        return ret;
    }
#endif /* __Use_Original_Qemu (U830) */
#if __Use_Original_Qemu != 1 /* ours (U831) */
    /*
     * NoVmp (ledger U831): PKRU <- the 32-bit value, as WRPKRU writes EAX (EDX must be 0
     * there: a 32-bit type has no upper half). Like WRPKRU (and XRSTOR of component 9) a
     * change flushes the TLB, whose entries carry the protection-key rights.
     */
    case UC_X86_REG_PKRU:
        if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKU)) {
            return UC_ERR_ARG;
        }
        CHECK_REG_TYPE(uint32_t);
        if (env->pkru != *(uint32_t *)value) {
            env->pkru = *(uint32_t *)value;
            if (x86_env_is_live(env)) { /* U832: a uc_context image has no TLB */
                tlb_flush(env_cpu(env));
            }
        }
        return ret;
#endif /* __Use_Original_Qemu (U831) */
    }

    switch (mode) {
    default:
        break;

    case UC_MODE_16:
        switch (regid) {
        default:
            break;
        case UC_X86_REG_ES:
            CHECK_REG_TYPE(uint16_t);
            load_seg_16_helper(env, R_ES, *(uint16_t *)value);
            return ret;
        case UC_X86_REG_SS:
            CHECK_REG_TYPE(uint16_t);
            load_seg_16_helper(env, R_SS, *(uint16_t *)value);
            return ret;
        case UC_X86_REG_DS:
            CHECK_REG_TYPE(uint16_t);
            load_seg_16_helper(env, R_DS, *(uint16_t *)value);
            return ret;
        case UC_X86_REG_FS:
            CHECK_REG_TYPE(uint16_t);
            load_seg_16_helper(env, R_FS, *(uint16_t *)value);
            return ret;
        case UC_X86_REG_GS:
            CHECK_REG_TYPE(uint16_t);
            load_seg_16_helper(env, R_GS, *(uint16_t *)value);
            return ret;
        }
        // fall-thru
    case UC_MODE_32:
        switch (regid) {
        default:
            break;
        case UC_X86_REG_CR0:
            CHECK_REG_TYPE(uint32_t);
            cpu_x86_update_cr0(env, *(uint32_t *)value);
            goto write_cr;
        case UC_X86_REG_CR1:
        case UC_X86_REG_CR2:
            CHECK_REG_TYPE(uint32_t);
            goto write_cr;
        case UC_X86_REG_CR3:
            CHECK_REG_TYPE(uint32_t);
            cpu_x86_update_cr3(env, *(uint32_t *)value);
            goto write_cr;
        case UC_X86_REG_CR4:
            CHECK_REG_TYPE(uint32_t);
            cpu_x86_update_cr4(env, *(uint32_t *)value);
        write_cr:
            env->cr[regid - UC_X86_REG_CR0] = *(uint32_t *)value;
            break;
        case UC_X86_REG_DR0:
        case UC_X86_REG_DR1:
        case UC_X86_REG_DR2:
        case UC_X86_REG_DR3:
        case UC_X86_REG_DR7:
            CHECK_REG_TYPE(uint32_t);
#if __Use_Original_Qemu == 1 /* original QEMU (U878) */
            x86_store_dr(env, regid - UC_X86_REG_DR0,
                         *(uint32_t *)value);
#else /* ours (U878) */
            x86_reg_store_dr(env, regid - UC_X86_REG_DR0, *(uint32_t *)value);
#endif /* __Use_Original_Qemu (U878) */
            break;
        case UC_X86_REG_DR4:
        case UC_X86_REG_DR5:
        case UC_X86_REG_DR6:
            CHECK_REG_TYPE(uint32_t);
            env->dr[regid - UC_X86_REG_DR0] = *(uint32_t *)value;
            break;
        case UC_X86_REG_FLAGS:
            CHECK_REG_TYPE(uint16_t);
            cpu_load_eflags(env, *(uint16_t *)value, -1);
            break;
        case UC_X86_REG_EFLAGS:
            CHECK_REG_TYPE(uint32_t);
            cpu_load_eflags(env, *(uint32_t *)value, -1);
            break;
        case UC_X86_REG_EAX:
            CHECK_REG_TYPE(uint32_t);
            env->regs[R_EAX] = *(uint32_t *)value;
            break;
        case UC_X86_REG_AX:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EAX], *(uint16_t *)value);
            break;
        case UC_X86_REG_AH:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_H(env->regs[R_EAX], *(uint8_t *)value);
            break;
        case UC_X86_REG_AL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_EAX], *(uint8_t *)value);
            break;
        case UC_X86_REG_EBX:
            CHECK_REG_TYPE(uint32_t);
            env->regs[R_EBX] = *(uint32_t *)value;
            break;
        case UC_X86_REG_BX:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EBX], *(uint16_t *)value);
            break;
        case UC_X86_REG_BH:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_H(env->regs[R_EBX], *(uint8_t *)value);
            break;
        case UC_X86_REG_BL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_EBX], *(uint8_t *)value);
            break;
        case UC_X86_REG_ECX:
            CHECK_REG_TYPE(uint32_t);
            env->regs[R_ECX] = *(uint32_t *)value;
            break;
        case UC_X86_REG_CX:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_ECX], *(uint16_t *)value);
            break;
        case UC_X86_REG_CH:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_H(env->regs[R_ECX], *(uint8_t *)value);
            break;
        case UC_X86_REG_CL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_ECX], *(uint8_t *)value);
            break;
        case UC_X86_REG_EDX:
            CHECK_REG_TYPE(uint32_t);
            env->regs[R_EDX] = *(uint32_t *)value;
            break;
        case UC_X86_REG_DX:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EDX], *(uint16_t *)value);
            break;
        case UC_X86_REG_DH:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_H(env->regs[R_EDX], *(uint8_t *)value);
            break;
        case UC_X86_REG_DL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_EDX], *(uint8_t *)value);
            break;
        case UC_X86_REG_ESP:
            CHECK_REG_TYPE(uint32_t);
            env->regs[R_ESP] = *(uint32_t *)value;
            break;
        case UC_X86_REG_SP:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_ESP], *(uint16_t *)value);
            break;
        case UC_X86_REG_EBP:
            CHECK_REG_TYPE(uint32_t);
            env->regs[R_EBP] = *(uint32_t *)value;
            break;
        case UC_X86_REG_BP:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EBP], *(uint16_t *)value);
            break;
        case UC_X86_REG_ESI:
            CHECK_REG_TYPE(uint32_t);
            env->regs[R_ESI] = *(uint32_t *)value;
            break;
        case UC_X86_REG_SI:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_ESI], *(uint16_t *)value);
            break;
        case UC_X86_REG_EDI:
            CHECK_REG_TYPE(uint32_t);
            env->regs[R_EDI] = *(uint32_t *)value;
            break;
        case UC_X86_REG_DI:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EDI], *(uint16_t *)value);
            break;
        case UC_X86_REG_EIP:
            CHECK_REG_TYPE(uint32_t);
            env->eip = *(uint32_t *)value;
            *setpc = 1;
            break;
        case UC_X86_REG_IP:
            CHECK_REG_TYPE(uint16_t);
            env->eip = *(uint16_t *)value;
            *setpc = 1;
            break;
        case UC_X86_REG_CS:
            CHECK_REG_TYPE(uint16_t);
            ret = uc_check_cpu_x86_load_seg(env, R_CS, *(uint16_t *)value);
            if (ret) {
                return ret;
            }
            cpu_x86_load_seg(env, R_CS, *(uint16_t *)value);
            break;
        case UC_X86_REG_DS:
            CHECK_REG_TYPE(uint16_t);
            ret = uc_check_cpu_x86_load_seg(env, R_DS, *(uint16_t *)value);
            if (ret) {
                return ret;
            }
            cpu_x86_load_seg(env, R_DS, *(uint16_t *)value);
            break;
        case UC_X86_REG_SS:
            CHECK_REG_TYPE(uint16_t);
            ret = uc_check_cpu_x86_load_seg(env, R_SS, *(uint16_t *)value);
            if (ret) {
                return ret;
            }
            cpu_x86_load_seg(env, R_SS, *(uint16_t *)value);
            break;
        case UC_X86_REG_ES:
            CHECK_REG_TYPE(uint16_t);
            ret = uc_check_cpu_x86_load_seg(env, R_ES, *(uint16_t *)value);
            if (ret) {
                return ret;
            }
            cpu_x86_load_seg(env, R_ES, *(uint16_t *)value);
            break;
        case UC_X86_REG_FS:
            CHECK_REG_TYPE(uint16_t);
            ret = uc_check_cpu_x86_load_seg(env, R_FS, *(uint16_t *)value);
            if (ret) {
                return ret;
            }
            cpu_x86_load_seg(env, R_FS, *(uint16_t *)value);
            break;
        case UC_X86_REG_GS:
            CHECK_REG_TYPE(uint16_t);
            ret = uc_check_cpu_x86_load_seg(env, R_GS, *(uint16_t *)value);
            if (ret) {
                return ret;
            }
            cpu_x86_load_seg(env, R_GS, *(uint16_t *)value);
            break;
        case UC_X86_REG_IDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            env->idt.limit = (uint16_t)((uc_x86_mmr *)value)->limit;
            env->idt.base = (uint32_t)((uc_x86_mmr *)value)->base;
            break;
        case UC_X86_REG_GDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            env->gdt.limit = (uint16_t)((uc_x86_mmr *)value)->limit;
            env->gdt.base = (uint32_t)((uc_x86_mmr *)value)->base;
            break;
        case UC_X86_REG_LDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            env->ldt.limit = ((uc_x86_mmr *)value)->limit;
            env->ldt.base = (uint32_t)((uc_x86_mmr *)value)->base;
            env->ldt.selector = (uint16_t)((uc_x86_mmr *)value)->selector;
            env->ldt.flags = ((uc_x86_mmr *)value)->flags;
            break;
        case UC_X86_REG_TR:
            CHECK_REG_TYPE(uc_x86_mmr);
            env->tr.limit = ((uc_x86_mmr *)value)->limit;
            env->tr.base = (uint32_t)((uc_x86_mmr *)value)->base;
            env->tr.selector = (uint16_t)((uc_x86_mmr *)value)->selector;
            env->tr.flags = ((uc_x86_mmr *)value)->flags;
            break;
        case UC_X86_REG_MSR:
            CHECK_REG_TYPE(uc_x86_msr);
#if __Use_Original_Qemu == 1 /* original QEMU (U905) */
            x86_msr_write(env, (uc_x86_msr *)value);
#else /* ours (U905) */
            if (x86_msr_write(env, (uc_x86_msr *)value)) {
                /* the instruction would #GP(0): no such MSR or a refused value (U905);
                   not UC_ERR_ARG, which CHECK_RET_DEPRECATE turns into UC_ERR_OK */
                ret = UC_ERR_EXCEPTION;
            }
#endif /* __Use_Original_Qemu (U905) */
            break;
        case UC_X86_REG_MXCSR:
            CHECK_REG_TYPE(uint32_t);
            cpu_set_mxcsr(env, *(uint32_t *)value);
            break;
            /*
        // Don't think base registers are a "thing" on x86
        case UC_X86_REG_FS_BASE:
            CHECK_REG_TYPE(uint32_t);
            env->segs[R_FS].base = *(uint32_t *)value;
            continue;
        case UC_X86_REG_GS_BASE:
            CHECK_REG_TYPE(uint32_t);
            env->segs[R_GS].base = *(uint32_t *)value;
            continue;
            */
        case UC_X86_REG_XCR0:
            CHECK_REG_TYPE(uint64_t);
            env->xcr0 = *(uint64_t *)value;
            cpu_sync_bndcs_hflags(env);
            cpu_sync_avx_hflag(env);
            break;
        }
        break;

#ifdef TARGET_X86_64
    case UC_MODE_64:
        switch (regid) {
        default:
            break;
        case UC_X86_REG_CR0:
            CHECK_REG_TYPE(uint64_t);
            cpu_x86_update_cr0(env, (*(uint64_t *)value) & 0xFFFFFFFF);
            goto write_cr64;
        case UC_X86_REG_CR1:
        case UC_X86_REG_CR2:
            CHECK_REG_TYPE(uint64_t);
            goto write_cr64;
        case UC_X86_REG_CR3:
            CHECK_REG_TYPE(uint64_t);
            cpu_x86_update_cr3(env, (*(uint64_t *)value) & 0xFFFFFFFF);
            goto write_cr64;
        case UC_X86_REG_CR4:
            CHECK_REG_TYPE(uint64_t);
            cpu_x86_update_cr4(env, (*(uint64_t *)value) & 0xFFFFFFFF);
            goto write_cr64;
        case UC_X86_REG_CR8:
            CHECK_REG_TYPE(uint64_t);
        write_cr64:
            env->cr[regid - UC_X86_REG_CR0] = *(uint64_t *)value;
            break;
        case UC_X86_REG_DR0:
        case UC_X86_REG_DR1:
        case UC_X86_REG_DR2:
        case UC_X86_REG_DR3:
        case UC_X86_REG_DR7:
            CHECK_REG_TYPE(uint64_t);
#if __Use_Original_Qemu == 1 /* original QEMU (U878) */
            x86_store_dr(env, regid - UC_X86_REG_DR0,
                         *(uint64_t *)value);
#else /* ours (U878) */
            x86_reg_store_dr(env, regid - UC_X86_REG_DR0, *(uint64_t *)value);
#endif /* __Use_Original_Qemu (U878) */
            break;
        case UC_X86_REG_DR4:
        case UC_X86_REG_DR5:
        case UC_X86_REG_DR6:
            CHECK_REG_TYPE(uint64_t);
            env->dr[regid - UC_X86_REG_DR0] = *(uint64_t *)value;
            break;
        case UC_X86_REG_FLAGS:
            CHECK_REG_TYPE(uint16_t);
            cpu_load_eflags(env, *(uint16_t *)value, -1);
            break;
        case UC_X86_REG_EFLAGS:
            CHECK_REG_TYPE(uint32_t);
            cpu_load_eflags(env, *(uint32_t *)value, -1);
            break;
        case UC_X86_REG_RFLAGS:
            CHECK_REG_TYPE(uint64_t);
            cpu_load_eflags(env, *(uint64_t *)value, -1);
            break;
        case UC_X86_REG_RAX:
            CHECK_REG_TYPE(uint64_t);
            env->regs[R_EAX] = *(uint64_t *)value;
            break;
        case UC_X86_REG_EAX:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[R_EAX], *(uint32_t *)value);
            break;
        case UC_X86_REG_AX:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EAX], *(uint16_t *)value);
            break;
        case UC_X86_REG_AH:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_H(env->regs[R_EAX], *(uint8_t *)value);
            break;
        case UC_X86_REG_AL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_EAX], *(uint8_t *)value);
            break;
        case UC_X86_REG_RBX:
            CHECK_REG_TYPE(uint64_t);
            env->regs[R_EBX] = *(uint64_t *)value;
            break;
        case UC_X86_REG_EBX:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[R_EBX], *(uint32_t *)value);
            break;
        case UC_X86_REG_BX:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EBX], *(uint16_t *)value);
            break;
        case UC_X86_REG_BH:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_H(env->regs[R_EBX], *(uint8_t *)value);
            break;
        case UC_X86_REG_BL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_EBX], *(uint8_t *)value);
            break;
        case UC_X86_REG_RCX:
            CHECK_REG_TYPE(uint64_t);
            env->regs[R_ECX] = *(uint64_t *)value;
            break;
        case UC_X86_REG_ECX:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[R_ECX], *(uint32_t *)value);
            break;
        case UC_X86_REG_CX:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_ECX], *(uint16_t *)value);
            break;
        case UC_X86_REG_CH:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_H(env->regs[R_ECX], *(uint8_t *)value);
            break;
        case UC_X86_REG_CL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_ECX], *(uint8_t *)value);
            break;
        case UC_X86_REG_RDX:
            CHECK_REG_TYPE(uint64_t);
            env->regs[R_EDX] = *(uint64_t *)value;
            break;
        case UC_X86_REG_EDX:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[R_EDX], *(uint32_t *)value);
            break;
        case UC_X86_REG_DX:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EDX], *(uint16_t *)value);
            break;
        case UC_X86_REG_DH:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_H(env->regs[R_EDX], *(uint8_t *)value);
            break;
        case UC_X86_REG_DL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_EDX], *(uint8_t *)value);
            break;
        case UC_X86_REG_RSP:
            CHECK_REG_TYPE(uint64_t);
            env->regs[R_ESP] = *(uint64_t *)value;
            break;
        case UC_X86_REG_ESP:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[R_ESP], *(uint32_t *)value);
            break;
        case UC_X86_REG_SP:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_ESP], *(uint16_t *)value);
            break;
        case UC_X86_REG_SPL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_ESP], *(uint8_t *)value);
            break;
        case UC_X86_REG_RBP:
            CHECK_REG_TYPE(uint64_t);
            env->regs[R_EBP] = *(uint64_t *)value;
            break;
        case UC_X86_REG_EBP:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[R_EBP], *(uint32_t *)value);
            break;
        case UC_X86_REG_BP:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EBP], *(uint16_t *)value);
            break;
        case UC_X86_REG_BPL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_EBP], *(uint8_t *)value);
            break;
        case UC_X86_REG_RSI:
            CHECK_REG_TYPE(uint64_t);
            env->regs[R_ESI] = *(uint64_t *)value;
            break;
        case UC_X86_REG_ESI:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[R_ESI], *(uint32_t *)value);
            break;
        case UC_X86_REG_SI:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_ESI], *(uint16_t *)value);
            break;
        case UC_X86_REG_SIL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_ESI], *(uint8_t *)value);
            break;
        case UC_X86_REG_RDI:
            CHECK_REG_TYPE(uint64_t);
            env->regs[R_EDI] = *(uint64_t *)value;
            break;
        case UC_X86_REG_EDI:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[R_EDI], *(uint32_t *)value);
            break;
        case UC_X86_REG_DI:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[R_EDI], *(uint16_t *)value);
            break;
        case UC_X86_REG_DIL:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[R_EDI], *(uint8_t *)value);
            break;
        case UC_X86_REG_RIP:
            CHECK_REG_TYPE(uint64_t);
            env->eip = *(uint64_t *)value;
            *setpc = 1;
            break;
        case UC_X86_REG_EIP:
            CHECK_REG_TYPE(uint32_t);
            env->eip = *(uint32_t *)value;
            *setpc = 1;
            break;
        case UC_X86_REG_IP:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->eip, *(uint16_t *)value);
            *setpc = 1;
            break;
        case UC_X86_REG_CS:
            CHECK_REG_TYPE(uint16_t);
            env->segs[R_CS].selector = *(uint16_t *)value;
            break;
        case UC_X86_REG_DS:
            CHECK_REG_TYPE(uint16_t);
            env->segs[R_DS].selector = *(uint16_t *)value;
            break;
        case UC_X86_REG_SS:
            CHECK_REG_TYPE(uint16_t);
            env->segs[R_SS].selector = *(uint16_t *)value;
            break;
        case UC_X86_REG_ES:
            CHECK_REG_TYPE(uint16_t);
            env->segs[R_ES].selector = *(uint16_t *)value;
            break;
        case UC_X86_REG_FS:
            CHECK_REG_TYPE(uint16_t);
            ret = uc_check_cpu_x86_load_seg(env, R_FS, *(uint16_t *)value);
            if (ret) {
                return ret;
            }
            cpu_x86_load_seg(env, R_FS, *(uint16_t *)value);
            break;
        case UC_X86_REG_GS:
            CHECK_REG_TYPE(uint16_t);
            ret = uc_check_cpu_x86_load_seg(env, R_GS, *(uint16_t *)value);
            if (ret) {
                return ret;
            }
            cpu_x86_load_seg(env, R_GS, *(uint16_t *)value);
            break;
        case UC_X86_REG_R8:
            CHECK_REG_TYPE(uint64_t);
            env->regs[8] = *(uint64_t *)value;
            break;
        case UC_X86_REG_R8D:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[8], *(uint32_t *)value);
            break;
        case UC_X86_REG_R8W:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[8], *(uint16_t *)value);
            break;
        case UC_X86_REG_R8B:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[8], *(uint8_t *)value);
            break;
        case UC_X86_REG_R9:
            CHECK_REG_TYPE(uint64_t);
            env->regs[9] = *(uint64_t *)value;
            break;
        case UC_X86_REG_R9D:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[9], *(uint32_t *)value);
            break;
        case UC_X86_REG_R9W:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[9], *(uint16_t *)value);
            break;
        case UC_X86_REG_R9B:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[9], *(uint8_t *)value);
            break;
        case UC_X86_REG_R10:
            CHECK_REG_TYPE(uint64_t);
            env->regs[10] = *(uint64_t *)value;
            break;
        case UC_X86_REG_R10D:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[10], *(uint32_t *)value);
            break;
        case UC_X86_REG_R10W:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[10], *(uint16_t *)value);
            break;
        case UC_X86_REG_R10B:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[10], *(uint8_t *)value);
            break;
        case UC_X86_REG_R11:
            CHECK_REG_TYPE(uint64_t);
            env->regs[11] = *(uint64_t *)value;
            break;
        case UC_X86_REG_R11D:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[11], *(uint32_t *)value);
            break;
        case UC_X86_REG_R11W:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[11], *(uint16_t *)value);
            break;
        case UC_X86_REG_R11B:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[11], *(uint8_t *)value);
            break;
        case UC_X86_REG_R12:
            CHECK_REG_TYPE(uint64_t);
            env->regs[12] = *(uint64_t *)value;
            break;
        case UC_X86_REG_R12D:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[12], *(uint32_t *)value);
            break;
        case UC_X86_REG_R12W:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[12], *(uint16_t *)value);
            break;
        case UC_X86_REG_R12B:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[12], *(uint8_t *)value);
            break;
        case UC_X86_REG_R13:
            CHECK_REG_TYPE(uint64_t);
            env->regs[13] = *(uint64_t *)value;
            break;
        case UC_X86_REG_R13D:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[13], *(uint32_t *)value);
            break;
        case UC_X86_REG_R13W:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[13], *(uint16_t *)value);
            break;
        case UC_X86_REG_R13B:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[13], *(uint8_t *)value);
            break;
        case UC_X86_REG_R14:
            CHECK_REG_TYPE(uint64_t);
            env->regs[14] = *(uint64_t *)value;
            break;
        case UC_X86_REG_R14D:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[14], *(uint32_t *)value);
            break;
        case UC_X86_REG_R14W:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[14], *(uint16_t *)value);
            break;
        case UC_X86_REG_R14B:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[14], *(uint8_t *)value);
            break;
        case UC_X86_REG_R15:
            CHECK_REG_TYPE(uint64_t);
            env->regs[15] = *(uint64_t *)value;
            break;
        case UC_X86_REG_R15D:
            CHECK_REG_TYPE(uint32_t);
            WRITE_DWORD(env->regs[15], *(uint32_t *)value);
            break;
        case UC_X86_REG_R15W:
            CHECK_REG_TYPE(uint16_t);
            WRITE_WORD(env->regs[15], *(uint16_t *)value);
            break;
        case UC_X86_REG_R15B:
            CHECK_REG_TYPE(uint8_t);
            WRITE_BYTE_L(env->regs[15], *(uint8_t *)value);
            break;
        case UC_X86_REG_IDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            env->idt.limit = (uint16_t)((uc_x86_mmr *)value)->limit;
            env->idt.base = ((uc_x86_mmr *)value)->base;
            break;
        case UC_X86_REG_GDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            env->gdt.limit = (uint16_t)((uc_x86_mmr *)value)->limit;
            env->gdt.base = ((uc_x86_mmr *)value)->base;
            break;
        case UC_X86_REG_LDTR:
            CHECK_REG_TYPE(uc_x86_mmr);
            env->ldt.limit = ((uc_x86_mmr *)value)->limit;
            env->ldt.base = ((uc_x86_mmr *)value)->base;
            env->ldt.selector = (uint16_t)((uc_x86_mmr *)value)->selector;
            env->ldt.flags = ((uc_x86_mmr *)value)->flags;
            break;
        case UC_X86_REG_TR:
            CHECK_REG_TYPE(uc_x86_mmr);
            env->tr.limit = ((uc_x86_mmr *)value)->limit;
            env->tr.base = ((uc_x86_mmr *)value)->base;
            env->tr.selector = (uint16_t)((uc_x86_mmr *)value)->selector;
            env->tr.flags = ((uc_x86_mmr *)value)->flags;
            break;
        case UC_X86_REG_MSR:
            CHECK_REG_TYPE(uc_x86_msr);
#if __Use_Original_Qemu == 1 /* original QEMU (U905) */
            x86_msr_write(env, (uc_x86_msr *)value);
#else /* ours (U905) */
            if (x86_msr_write(env, (uc_x86_msr *)value)) {
                /* the instruction would #GP(0): no such MSR or a refused value (U905);
                   not UC_ERR_ARG, which CHECK_RET_DEPRECATE turns into UC_ERR_OK */
                ret = UC_ERR_EXCEPTION;
            }
#endif /* __Use_Original_Qemu (U905) */
            break;
        case UC_X86_REG_MXCSR:
            CHECK_REG_TYPE(uint32_t);
            cpu_set_mxcsr(env, *(uint32_t *)value);
            break;
        case UC_X86_REG_XMM8:
        case UC_X86_REG_XMM9:
        case UC_X86_REG_XMM10:
        case UC_X86_REG_XMM11:
        case UC_X86_REG_XMM12:
        case UC_X86_REG_XMM13:
        case UC_X86_REG_XMM14:
        case UC_X86_REG_XMM15:
        case UC_X86_REG_XMM16:
        case UC_X86_REG_XMM17:
        case UC_X86_REG_XMM18:
        case UC_X86_REG_XMM19:
        case UC_X86_REG_XMM20:
        case UC_X86_REG_XMM21:
        case UC_X86_REG_XMM22:
        case UC_X86_REG_XMM23:
        case UC_X86_REG_XMM24:
        case UC_X86_REG_XMM25:
        case UC_X86_REG_XMM26:
        case UC_X86_REG_XMM27:
        case UC_X86_REG_XMM28:
        case UC_X86_REG_XMM29:
        case UC_X86_REG_XMM30:
        case UC_X86_REG_XMM31: {
            CHECK_REG_TYPE(uint64_t[2]);
            const uint64_t *src = (const uint64_t *)value;
            ZMMReg *reg = &env->xmm_regs[regid - UC_X86_REG_XMM0];
            reg->ZMM_Q(0) = src[0];
            reg->ZMM_Q(1) = src[1];
            break;
        }
        case UC_X86_REG_YMM8:
        case UC_X86_REG_YMM9:
        case UC_X86_REG_YMM10:
        case UC_X86_REG_YMM11:
        case UC_X86_REG_YMM12:
        case UC_X86_REG_YMM13:
        case UC_X86_REG_YMM14:
        case UC_X86_REG_YMM15:
        case UC_X86_REG_YMM16:
        case UC_X86_REG_YMM17:
        case UC_X86_REG_YMM18:
        case UC_X86_REG_YMM19:
        case UC_X86_REG_YMM20:
        case UC_X86_REG_YMM21:
        case UC_X86_REG_YMM22:
        case UC_X86_REG_YMM23:
        case UC_X86_REG_YMM24:
        case UC_X86_REG_YMM25:
        case UC_X86_REG_YMM26:
        case UC_X86_REG_YMM27:
        case UC_X86_REG_YMM28:
        case UC_X86_REG_YMM29:
        case UC_X86_REG_YMM30:
        case UC_X86_REG_YMM31: {
            CHECK_REG_TYPE(uint64_t[4]);
            const uint64_t *src = (const uint64_t *)value;
            ZMMReg *reg = &env->xmm_regs[regid - UC_X86_REG_YMM0];
            reg->ZMM_Q(0) = src[0];
            reg->ZMM_Q(1) = src[1];
            reg->ZMM_Q(2) = src[2];
            reg->ZMM_Q(3) = src[3];
            break;
        }
        case UC_X86_REG_ZMM0:
        case UC_X86_REG_ZMM1:
        case UC_X86_REG_ZMM2:
        case UC_X86_REG_ZMM3:
        case UC_X86_REG_ZMM4:
        case UC_X86_REG_ZMM5:
        case UC_X86_REG_ZMM6:
        case UC_X86_REG_ZMM7:
        case UC_X86_REG_ZMM8:
        case UC_X86_REG_ZMM9:
        case UC_X86_REG_ZMM10:
        case UC_X86_REG_ZMM11:
        case UC_X86_REG_ZMM12:
        case UC_X86_REG_ZMM13:
        case UC_X86_REG_ZMM14:
        case UC_X86_REG_ZMM15:
        case UC_X86_REG_ZMM16:
        case UC_X86_REG_ZMM17:
        case UC_X86_REG_ZMM18:
        case UC_X86_REG_ZMM19:
        case UC_X86_REG_ZMM20:
        case UC_X86_REG_ZMM21:
        case UC_X86_REG_ZMM22:
        case UC_X86_REG_ZMM23:
        case UC_X86_REG_ZMM24:
        case UC_X86_REG_ZMM25:
        case UC_X86_REG_ZMM26:
        case UC_X86_REG_ZMM27:
        case UC_X86_REG_ZMM28:
        case UC_X86_REG_ZMM29:
        case UC_X86_REG_ZMM30:
        case UC_X86_REG_ZMM31: {
            CHECK_REG_TYPE(uint64_t[8]);
            const uint64_t *src = (const uint64_t *)value;
            ZMMReg *reg = &env->xmm_regs[regid - UC_X86_REG_ZMM0];
            reg->ZMM_Q(0) = src[0];
            reg->ZMM_Q(1) = src[1];
            reg->ZMM_Q(2) = src[2];
            reg->ZMM_Q(3) = src[3];
            reg->ZMM_Q(4) = src[4];
            reg->ZMM_Q(5) = src[5];
            reg->ZMM_Q(6) = src[6];
            reg->ZMM_Q(7) = src[7];
            break;
        }
        case UC_X86_REG_FS_BASE:
            CHECK_REG_TYPE(uint64_t);
            env->segs[R_FS].base = *(uint64_t *)value;
            return 0;
        case UC_X86_REG_GS_BASE:
            CHECK_REG_TYPE(uint64_t);
            env->segs[R_GS].base = *(uint64_t *)value;
            return 0;
        case UC_X86_REG_XCR0:
            CHECK_REG_TYPE(uint64_t);
            env->xcr0 = *(uint64_t *)value;
            cpu_sync_bndcs_hflags(env);
            cpu_sync_avx_hflag(env);
            break;
        }
        break;
#endif
    }

    CHECK_RET_DEPRECATE(ret, regid);
    return ret;
}

static bool x86_stop_interrupt(struct uc_struct *uc, int intno)
{
    switch (intno) {
    default:
        return false;
    case EXCP06_ILLOP:
        return true;
    }
}

static bool x86_insn_hook_validate(uint32_t insn_enum)
{
    // for x86 we can only hook IN, OUT, SYSCALL, SYSENTER, CPUID, RDTSC, RDTSCP, RDMSR and WRMSR
    if (insn_enum != UC_X86_INS_IN && insn_enum != UC_X86_INS_OUT &&
        insn_enum != UC_X86_INS_SYSCALL && insn_enum != UC_X86_INS_SYSENTER &&
        insn_enum != UC_X86_INS_CPUID && insn_enum != UC_X86_INS_RDTSC &&
        insn_enum != UC_X86_INS_RDTSCP && insn_enum != UC_X86_INS_RDMSR &&
        insn_enum != UC_X86_INS_WRMSR) {
        return false;
    }
    return true;
}

static bool x86_opcode_hook_invalidate(uint32_t op, uint32_t flags)
{
    if (op != UC_TCG_OP_SUB) {
        return false;
    }

    switch (op) {
    case UC_TCG_OP_SUB:

        if ((flags & UC_TCG_OP_FLAG_CMP) && (flags & UC_TCG_OP_FLAG_DIRECT)) {
            return false;
        }

        break;

    default:
        return false;
    }

    return true;
}

#if __Use_Original_Qemu != 1 /* ours (U120) */
/* NoVmp (ledger U120): a CPUID profile set after init narrows XCR0 to its leaf 0DH */
static void x86_cpuid_changed(struct uc_struct *uc)
{
    CPUX86State *env = &X86_CPU(uc->cpu)->env;

    env->xcr0 = x86_cpu_xcr0_in_profile(env, env->xcr0);
    cpu_sync_bndcs_hflags(env);
    cpu_sync_avx_hflag(env);
#if __Use_Original_Qemu != 1 /* ours (U593) */
    /* MAXPHYADDR follows the new profile; cached translations used the old reserved bits */
    x86_cpu_update_phys_bits(X86_CPU(uc->cpu));
    tlb_flush(uc->cpu);
#endif /* __Use_Original_Qemu (U593) */
}

#endif /* __Use_Original_Qemu (U120) */
static int x86_cpus_init(struct uc_struct *uc, const char *cpu_model)
{

    X86CPU *cpu;

    cpu = cpu_x86_init(uc);
    if (cpu == NULL) {
        return -1;
    }
#if __Use_Original_Qemu != 1 /* ours (U835) */
    /* NoVmp (ledger U835): UC_CTL_X86_RDRAND written before init (default: seeded, seed 0) */
    cpu->env.rdrand_host = uc->x86_rdrand_mode == UC_X86_RDRAND_HOST;
    cpu->env.rdrand_seed = uc->x86_rdrand_seed;
    cpu->env.rdrand_count = 0;
#endif /* __Use_Original_Qemu (U835) */

    return 0;
}

#if __Use_Original_Qemu != 1 /* ours (U835) */
/* NoVmp (ledger U835): UC_CTL_X86_RDRAND after init (uc.c) */
static void x86_rdrand_sync(struct uc_struct *uc, int to_cpu)
{
    CPUX86State *env = &X86_CPU(uc->cpu)->env;

    if (to_cpu) {
        env->rdrand_host = uc->x86_rdrand_mode == UC_X86_RDRAND_HOST;
        env->rdrand_seed = uc->x86_rdrand_seed;
        env->rdrand_count = 0;
    } else {
        uc->x86_rdrand_mode = env->rdrand_host ? UC_X86_RDRAND_HOST : UC_X86_RDRAND_SEEDED;
        uc->x86_rdrand_seed = env->rdrand_seed;
    }
}
#endif /* __Use_Original_Qemu (U835) */

#if __Use_Original_Qemu != 1 /* ours (U832) */
/*
 * NoVmp (ledger U832): an x86 uc_context holds the whole CPUX86State. Unicorn's default
 * (the bytes up to end_reset_fields) lost architectural state that QEMU keeps after that
 * marker - IA32_XSS, IA32_UMWAIT_CONTROL, IA32_PASID, the MTRRs, IA32_MCG_CTL and the MCi
 * banks - and uc_context_reg_read/write read env->features (APX/PKU gating, RDMSR) and wrote
 * env->msr_api beyond the saved bytes. A restore copies the reset area plus that state, never
 * the CPU model (features, CPUID data, caches) or the uc pointer, and flushes the TLB when
 * state its translations depend on changed (CR0, CR3, CR4, EFER, PKRU, IA32_PKRS), as the
 * instructions that write those registers do.
 */
static size_t x86_context_size(struct uc_struct *uc)
{
    (void)uc;
    return sizeof(CPUX86State);
}

#define X86_CTX_COPY(f)                                                        \
    memcpy((char *)env + offsetof(CPUX86State, f),                             \
           context->data + offsetof(CPUX86State, f), sizeof(env->f))

static uc_err x86_context_restore(struct uc_struct *uc, uc_context *context)
{
    CPUX86State *env = uc->cpu->env_ptr;
    target_ulong cr0 = env->cr[0], cr3 = env->cr[3], cr4 = env->cr[4];
    uint64_t efer = env->efer;
    uint32_t pkru = env->pkru, pkrs = env->pkrs;

    if (context->context_size < sizeof(CPUX86State)) {
        return UC_ERR_ARG;
    }
#if __Use_Original_Qemu != 1 /* ours (U878) */
    /*
     * NoVmp (ledger U878): the debug-register breakpoints are objects of the live CPUState;
     * env->cpu_breakpoint[] holds pointers to them. Remove the live ones (DR7 = 400h) before
     * the copy, never adopt the image's pointers, and insert the ones of the restored DR7
     * after it (cpu_x86_update_dr7 also recomputes HF_IOBPT). Before, the old breakpoints
     * stayed in the CPU and the image's (possibly freed) pointers were taken over.
     */
    cpu_x86_update_dr7(env, 0);
#endif /* __Use_Original_Qemu (U878) */
    memcpy(env, context->data, offsetof(CPUX86State, end_reset_fields));
#if __Use_Original_Qemu != 1 /* ours (U878) */
    {
        target_ulong dr7 = env->dr[7];

        memset(env->cpu_breakpoint, 0, sizeof(env->cpu_breakpoint));
        env->dr[7] = DR7_FIXED_1;
        cpu_x86_update_dr7(env, (uint32_t)dr7);
    }
#endif /* __Use_Original_Qemu (U878) */
    X86_CTX_COPY(mtrr_fixed);
    X86_CTX_COPY(mtrr_deftype);
    X86_CTX_COPY(mtrr_var);
    X86_CTX_COPY(mcg_ctl);
    X86_CTX_COPY(mce_banks);
    X86_CTX_COPY(xss);
    X86_CTX_COPY(umwait);
    X86_CTX_COPY(pasid);
#if __Use_Original_Qemu != 1 /* ours (U835) */
    X86_CTX_COPY(rdrand_seed);  /* a restore replays the same RDRAND/RDSEED values */
    X86_CTX_COPY(rdrand_count);
    X86_CTX_COPY(rdrand_host);
#endif /* __Use_Original_Qemu (U835) */
    if (env->cr[0] != cr0 || env->cr[3] != cr3 || env->cr[4] != cr4 || env->efer != efer ||
        env->pkru != pkru || env->pkrs != pkrs) {
        tlb_flush(uc->cpu);
    }
#if __Use_Original_Qemu != 1 /* ours (U878) */
    x86_uintr_update_request(env);  /* the restored UIRR (U878; an image write does not) */
#endif /* __Use_Original_Qemu (U878) */
    return UC_ERR_OK;
}
#undef X86_CTX_COPY
#endif /* __Use_Original_Qemu (U832) */
#if __Use_Original_Qemu != 1 /* ours (U1021) */
/* NoVmp (ledger U1021): UC_CTL_X86_MKTME_KEY */
static uc_err x86_mktme_key(struct uc_struct *uc, int keyid, struct uc_x86_mktme_key *key)
{
    CPUX86State *env = &X86_CPU(uc->cpu)->env;
    const X86MktmeKey *e;

    if (keyid < 0 || keyid > NOVMP_MKTME_MAX_KEYS) {
        return UC_ERR_ARG;
    }
    e = &env->mktme_keys[keyid];
    key->mode = e->mode;
    key->random = e->random;
    key->enc_alg = e->enc_alg;
    memcpy(key->data_key, e->data_key, sizeof(key->data_key));
    memcpy(key->tweak_key, e->tweak_key, sizeof(key->tweak_key));
    return UC_ERR_OK;
}
#endif /* __Use_Original_Qemu (U1021) */

DEFAULT_VISIBILITY
void uc_init(struct uc_struct *uc)
{
    uc->reg_read = reg_read;
    uc->reg_write = reg_write;
    uc->reg_reset = reg_reset;
    uc->release = x86_release;
    uc->set_pc = x86_set_pc;
    uc->get_pc = x86_get_pc;
    uc->stop_interrupt = x86_stop_interrupt;
    uc->insn_hook_validate = x86_insn_hook_validate;
    uc->opcode_hook_invalidate = x86_opcode_hook_invalidate;
    uc->cpus_init = x86_cpus_init;
#if __Use_Original_Qemu != 1 /* ours (U120) */
    uc->x86_cpuid_changed = x86_cpuid_changed;
#endif /* __Use_Original_Qemu (U120) */
    uc->cpu_context_size = offsetof(CPUX86State, end_reset_fields);
#if __Use_Original_Qemu != 1 /* ours (U832) */
    uc->context_size = x86_context_size;
    uc->context_restore = x86_context_restore;
#endif /* __Use_Original_Qemu (U832) */
#if __Use_Original_Qemu != 1 /* ours (U835) */
    uc->x86_rdrand_sync = x86_rdrand_sync;
#endif /* __Use_Original_Qemu (U835) */
#if __Use_Original_Qemu != 1 /* ours (U1021) */
    uc->x86_mktme_key = x86_mktme_key;
#endif /* __Use_Original_Qemu (U1021) */
    uc_common_init(uc);
}

/* vim: set ts=4 sts=4 sw=4 et:  */
