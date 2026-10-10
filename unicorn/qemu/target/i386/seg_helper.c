/*
 *  x86 segmentation related helpers:
 *  TSS, interrupts, system calls, jumps and call/task gates, descriptors
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/log.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

#include "uc_priv.h"
#include <unicorn/unicorn.h>

//#define DEBUG_PCALL

#ifdef DEBUG_PCALL
# define LOG_PCALL(...) qemu_log_mask(CPU_LOG_PCALL, ## __VA_ARGS__)
# define LOG_PCALL_STATE(cpu)                                  \
    log_cpu_state_mask(CPU_LOG_PCALL, (cpu), CPU_DUMP_CCOP)
#else
# define LOG_PCALL(...) do { } while (0)
# define LOG_PCALL_STATE(cpu) do { } while (0)
#endif

/*
 * TODO: Convert callers to compute cpu_mmu_index_kernel once
 * and use *_mmuidx_ra directly.
 */
#define cpu_ldub_kernel_ra(e, p, r) \
    cpu_ldub_mmuidx_ra(e, p, cpu_mmu_index_kernel(e), r)
#define cpu_lduw_kernel_ra(e, p, r) \
    cpu_lduw_mmuidx_ra(e, p, cpu_mmu_index_kernel(e), r)
#define cpu_ldl_kernel_ra(e, p, r) \
    cpu_ldl_mmuidx_ra(e, p, cpu_mmu_index_kernel(e), r)
#define cpu_ldq_kernel_ra(e, p, r) \
    cpu_ldq_mmuidx_ra(e, p, cpu_mmu_index_kernel(e), r)

#define cpu_stb_kernel_ra(e, p, v, r) \
    cpu_stb_mmuidx_ra(e, p, v, cpu_mmu_index_kernel(e), r)
#define cpu_stw_kernel_ra(e, p, v, r) \
    cpu_stw_mmuidx_ra(e, p, v, cpu_mmu_index_kernel(e), r)
#define cpu_stl_kernel_ra(e, p, v, r) \
    cpu_stl_mmuidx_ra(e, p, v, cpu_mmu_index_kernel(e), r)
#define cpu_stq_kernel_ra(e, p, v, r) \
    cpu_stq_mmuidx_ra(e, p, v, cpu_mmu_index_kernel(e), r)

#define cpu_ldub_kernel(e, p)    cpu_ldub_kernel_ra(e, p, 0)
#define cpu_lduw_kernel(e, p)    cpu_lduw_kernel_ra(e, p, 0)
#define cpu_ldl_kernel(e, p)     cpu_ldl_kernel_ra(e, p, 0)
#define cpu_ldq_kernel(e, p)     cpu_ldq_kernel_ra(e, p, 0)

#define cpu_stb_kernel(e, p, v)  cpu_stb_kernel_ra(e, p, v, 0)
#define cpu_stw_kernel(e, p, v)  cpu_stw_kernel_ra(e, p, v, 0)
#define cpu_stl_kernel(e, p, v)  cpu_stl_kernel_ra(e, p, v, 0)
#define cpu_stq_kernel(e, p, v)  cpu_stq_kernel_ra(e, p, v, 0)

/* return non zero if error */
static inline int load_segment_ra(CPUX86State *env, uint32_t *e1_ptr,
                               uint32_t *e2_ptr, int selector,
                               uintptr_t retaddr)
{
    SegmentCache *dt;
    int index;
    target_ulong ptr;

    if (selector & 0x4) {
        dt = &env->ldt;
    } else {
        dt = &env->gdt;
    }
    index = selector & ~7;
    if ((index + 7) > dt->limit) {
        return -1;
    }
    ptr = dt->base + index;
    *e1_ptr = cpu_ldl_kernel_ra(env, ptr, retaddr);
    *e2_ptr = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);
    return 0;
}

static inline int load_segment(CPUX86State *env, uint32_t *e1_ptr,
                               uint32_t *e2_ptr, int selector)
{
    return load_segment_ra(env, e1_ptr, e2_ptr, selector, 0);
}

static inline unsigned int get_seg_limit(uint32_t e1, uint32_t e2)
{
    unsigned int limit;

    limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & DESC_G_MASK) {
        limit = (limit << 12) | 0xfff;
    }
    return limit;
}

static inline uint32_t get_seg_base(uint32_t e1, uint32_t e2)
{
    return (e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000);
}

static inline void load_seg_cache_raw_dt(SegmentCache *sc, uint32_t e1,
                                         uint32_t e2)
{
    sc->base = get_seg_base(e1, e2);
    sc->limit = get_seg_limit(e1, e2);
    sc->flags = e2;
}

/* init the segment cache in vm86 mode. */
static inline void load_seg_vm(CPUX86State *env, int seg, int selector)
{
    selector &= 0xffff;

    cpu_x86_load_seg_cache(env, seg, selector, (selector << 4), 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK | (3 << DESC_DPL_SHIFT));
}

#if __Use_Original_Qemu != 1 /* ours (U750) */
/*
 * NoVmp (ledger U750): CET shadow stacks and indirect branch tracking on far transfers
 * (SDM Vol1 18.2.2, 18.2.3, 18.3.3; Vol2 CALL, RET, IRET, INT n, SYSRET, SYSEXIT;
 * Vol3A 7.12.1.1, 10.3). Common pieces used by far CALL (U750), RET far / IRET (U751,
 * U752), SYSRET / SYSEXIT (U753), task switches (U754) and IDT event delivery (U755):
 *  - cet2_ss_en / cet2_ibt_en: ShadowStackEnabled(CPL) / EndbranchEnabled(CPL) for a
 *    given CPL and EFLAGS.VM (a transfer evaluates them at the old and the new CPL):
 *    CR4.CET = 1, CR0.PE = 1, VM = 0 and SH_STK_EN / ENDBR_EN of IA32_U_CET (CPL 3) or
 *    IA32_S_CET (CPL < 3); like U115/U116 also the CPUID feature as a strict profile
 *    shows it;
 *  - shadow-stack loads and stores with an explicit privilege (user: the stack belongs to
 *    CPL 3) and address size (Vol1 18.2.1: 32-bit in 32-bit/compatibility mode, 64-bit in
 *    64-bit mode). Pops and the token release on the old shadow stack use the mode the
 *    transfer starts in, pushes and the token acquisition on the new shadow stack the
 *    target mode. A non-canonical 64-bit address is #GP(0) (as U115). They use the
 *    shadow-stack MMU modes (U117);
 *  - shadow_stack_lock_cmpxchg8b (Vol1 18.2.2): the value read is written back when the
 *    compare fails; the supervisor shadow-stack token checks (18.2.3);
 *  - LA_adjust (CALL, INT n, SYSCALL): bits 63:N get bit N-1, N = the maximum
 *    linear-address width (57 with LA57 enumerated, else 48).
 * Every check that faults runs before the transfer commits, so a fault leaves the
 * registers (SSP and the CET MSRs included) unchanged; stores already made stay (the
 * "prematurely busy" token of 18.2.3).
 */
static bool cet2_ss_cpuid(CPUX86State *env)
{
    return (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK) &&
           (x86_cpuid_profile_mask(env, 7, 0, 2) & CPUID_7_0_ECX_CET_SHSTK);
}

static bool cet2_ibt_cpuid(CPUX86State *env)
{
    return (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_CET_IBT) &&
           (x86_cpuid_profile_mask(env, 7, 0, 3) & CPUID_7_0_EDX_CET_IBT);
}

static uint64_t cet2_msr(CPUX86State *env, int cpl)
{
    return cpl == 3 ? env->u_cet : env->s_cet;
}

/* ShadowStackEnabled(cpl) with EFLAGS.VM = vm (SDM Vol1 18.2.2) */
static bool cet2_ss_en(CPUX86State *env, int cpl, bool vm)
{
    return cet2_ss_cpuid(env) && (env->cr[4] & CR4_CET_MASK) &&
           (env->cr[0] & CR0_PE_MASK) && !vm && (cet2_msr(env, cpl) & CET_SH_STK_EN);
}

/* EndbranchEnabled(cpl) with EFLAGS.VM = vm (SDM Vol1 18.3.2) */
static bool cet2_ibt_en(CPUX86State *env, int cpl, bool vm)
{
    return cet2_ibt_cpuid(env) && (env->cr[4] & CR4_CET_MASK) &&
           (env->cr[0] & CR0_PE_MASK) && !vm && (cet2_msr(env, cpl) & CET_ENDBR_EN);
}

/* IA32_x_CET.TRACKER = WAIT_FOR_ENDBRANCH, SUPPRESS = 0 for the tracker of cpl */
static void cet2_ibt_wait(CPUX86State *env, int cpl)
{
    uint64_t *cet = cpl == 3 ? &env->u_cet : &env->s_cet;

    *cet = (*cet & ~CET_SUPPRESS) | CET_TRACKER;
}

/* canonical relative to the current paging mode (CR4.LA57) */
static bool cet2_canonical(CPUX86State *env, uint64_t v)
{
    int64_t sext = (int64_t)v >> ((env->cr[4] & CR4_LA57_MASK) ? 56 : 47);

    return sext == 0 || sext == -1;
}

/* LA_adjust: bits 63:N := bit N-1, N = maximum linear-address width */
static uint64_t cet2_la_adjust(CPUX86State *env, uint64_t v)
{
    int shift = (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57) ? 64 - 57 : 64 - 48;

    return (uint64_t)((int64_t)(v << shift) >> shift);
}

static target_ulong cet2_addr(CPUX86State *env, uint64_t a, bool lm, uintptr_t ra)
{
    if (lm) {
        if (!cet2_canonical(env, a)) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, ra);
        }
        return a;
    }
    return (uint32_t)a;
}

static uint64_t cet2_ld8(CPUX86State *env, uint64_t a, bool lm, bool user, uintptr_t ra)
{
    return cpu_ldq_mmuidx_ra(env, cet2_addr(env, a, lm, ra),
                             user ? MMU_SS_USER_IDX : MMU_SS_KSMAP_IDX, ra);
}

static void cet2_st8(CPUX86State *env, uint64_t a, uint64_t v, bool lm, bool user,
                     uintptr_t ra)
{
    cpu_stq_mmuidx_ra(env, cet2_addr(env, a, lm, ra), v,
                      user ? MMU_SS_USER_IDX : MMU_SS_KSMAP_IDX, ra);
}

static void cet2_st4(CPUX86State *env, uint64_t a, uint32_t v, bool lm, bool user,
                     uintptr_t ra)
{
    cpu_stl_mmuidx_ra(env, cet2_addr(env, a, lm, ra), v,
                      user ? MMU_SS_USER_IDX : MMU_SS_KSMAP_IDX, ra);
}

/* shadow_stack_lock_cmpxchg8b(a, new, expected): returns the value read */
static uint64_t cet2_cmpxchg8(CPUX86State *env, uint64_t a, uint64_t nv, uint64_t expect,
                              bool lm, bool user, uintptr_t ra)
{
    uint64_t old = cet2_ld8(env, a, lm, user, ra);

    cet2_st8(env, a, old == expect ? nv : old, lm, user, ra);
    return old;
}

static uint64_t cet2_wrap(uint64_t v, bool lm)
{
    return lm ? v : (uint32_t)v;
}

/* ShadowStackPush8B(cs); ShadowStackPush8B(lip); ShadowStackPush8B(ssp) below top */
static uint64_t cet2_push_frame(CPUX86State *env, uint64_t top, uint64_t cs, uint64_t lip,
                                uint64_t ssp, bool lm, bool user, uintptr_t ra)
{
    cet2_st8(env, top - 8, cs, lm, user, ra);
    cet2_st8(env, top - 16, lip, lm, user, ra);
    cet2_st8(env, top - 24, ssp, lm, user, ra);
    return cet2_wrap(top - 24, lm);
}

/*
 * A far CALL's or an event delivery's shadow-stack work is done in two steps, so that a fault
 * leaves memory and registers as they were (SDM Vol3A 6.15; fix3's U708 probes the data-stack
 * pushes the same way). cet2_plan_*: every check of the pseudocode (#GP(0)), the supervisor
 * token read and compare, then the shadow-stack slots to be written are probed
 * (x86_probe_write_la in the shadow-stack MMU modes: #PF; a non-canonical 64-bit address
 * #GP(0)) - nothing is written. The caller checks and probes its data-stack pushes first
 * (they come first in the pseudocode), plans, writes the data stack, then cet2_commit sets
 * the token's busy bit, writes the 4 zero bytes and the CS / LIP / SSP frame and loads SSP /
 * IA32_PL3_SSP.
 */
typedef struct Cet2Xfer {
    bool en;            /* shadow stacks at the target CPL: SSP is loaded */
    bool lm, user;      /* target mode 64-bit; user shadow stack */
    bool zero4, token, frame, pl3;
    uint64_t base, tok, top, cs, lip, saved, new_ssp, pl3_val;
} Cet2Xfer;

static void cet2_probe(CPUX86State *env, uint64_t a, uint32_t len, bool lm, bool user,
                       uintptr_t ra)
{
    x86_probe_write_la(env, cet2_addr(env, a, lm, ra), len,
                       user ? MMU_SS_USER_IDX : MMU_SS_KSMAP_IDX, ra);
}

/* the CS / LIP / saved-SSP frame below top (x->cs, x->lip, x->saved set by the caller) */
static void cet2_plan_frame(CPUX86State *env, Cet2Xfer *x, uint64_t top, uintptr_t ra)
{
    x->frame = true;
    x->top = top;
    cet2_probe(env, top - 8, 8, x->lm, x->user, ra);
    cet2_probe(env, top - 16, 8, x->lm, x->user, ra);
    cet2_probe(env, top - 24, 8, x->lm, x->user, ra);
    x->new_ssp = cet2_wrap(top - 24, x->lm);
}

/*
 * "Shadow_stack_store 4 bytes of 0 to (base - 4); SSP = base & ~7; push CS, LIP, saved"
 * (far CALL / event delivery without a shadow-stack switch)
 */
static void cet2_plan_align(CPUX86State *env, Cet2Xfer *x, uint64_t base, uintptr_t ra)
{
    x->zero4 = true;
    x->base = base;
    cet2_probe(env, base - 4, 4, x->lm, x->user, ra);
    cet2_plan_frame(env, x, base & ~7ull, ra);
}

/*
 * Supervisor shadow-stack token at ssp (IA32_PLi_SSP or an interrupt SSP table entry)
 * before the new shadow stack is used by a far CALL or event delivery (Vol1 18.2.3): 8-byte
 * aligned, token and the 24-byte frame within one naturally aligned 32-byte region, below
 * 4 GB unless the target is 64-bit mode, token == ssp (busy clear, reserved bits 0). The
 * locked compare-exchange: on a mismatch the value read is written back and #GP(0); on a
 * match the busy bit is set by cet2_commit.
 */
static void cet2_plan_token(CPUX86State *env, Cet2Xfer *x, uint64_t ssp, uintptr_t ra)
{
    uint64_t v;

    if ((ssp & 7) || (ssp & ~0x1full) != ((ssp - 24) & ~0x1full) ||
        (!x->lm && (ssp >> 32))) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, ra);
    }
    v = cet2_ld8(env, ssp, x->lm, false, ra);
    cet2_probe(env, ssp, 8, x->lm, false, ra);
    if (v != ssp) {
        cet2_st8(env, ssp, v, x->lm, false, ra);
        raise_exception_err_ra(env, EXCP0D_GPF, 0, ra);
    }
    x->token = true;
    x->tok = ssp;
    x->new_ssp = ssp;
}

static void cet2_commit(CPUX86State *env, Cet2Xfer *x, uintptr_t ra)
{
    if (x->token) {
        cet2_st8(env, x->tok, x->tok | 1, x->lm, false, ra);
    }
    if (x->zero4) {
        cet2_st4(env, x->base - 4, 0, x->lm, x->user, ra);
    }
    if (x->frame) {
        cet2_push_frame(env, x->top, x->cs, x->lip, x->saved, x->lm, x->user, ra);
    }
    if (x->pl3) {
        env->pl_ssp[3] = x->pl3_val;
    }
    if (x->en) {
        env->ssp = x->new_ssp;
    }
}


/* token release (far RET / IRET): busy token == ssp | 1 becomes ssp, else unchanged */
static void cet2_token_release(CPUX86State *env, uint64_t ssp, bool lm, bool user,
                               uintptr_t ra)
{
    cet2_cmpxchg8(env, ssp, ssp, ssp | 1, lm, user, ra);
}

/* the SSP a far RET / IRET (or task-switch IRET) returns to: #GP(0) unless usable */
static void cet2_check_ret_ssp(CPUX86State *env, uint64_t v, bool lm_new, uintptr_t ra)
{
    if (lm_new ? !cet2_canonical(env, v) : (v >> 32) != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, ra);
    }
}

/*
 * Pop and check the CS / LIP / SSP frame at *ssp (far RET / IRET): CS and LIP must equal
 * the return CS (zero-extended selector) and CS.base + EIP (32 bits outside 64-bit mode),
 * the popped SSP must be 4-byte aligned, else #CP(FAR-RET/IRET). Returns the popped SSP.
 */
static uint64_t cet2_pop_frame(CPUX86State *env, uint64_t *ssp, uint64_t cs, uint64_t lip,
                               bool lm, bool user, uintptr_t ra)
{
    uint64_t scs = cet2_ld8(env, *ssp + 16, lm, user, ra);
    uint64_t slip = cet2_ld8(env, *ssp + 8, lm, user, ra);
    uint64_t prev = cet2_ld8(env, *ssp, lm, user, ra);

    *ssp = cet2_wrap(*ssp + 24, lm);
    if (scs != cs || slip != lip || (prev & 3)) {
        raise_exception_err_ra(env, EXCP15_CP, CP_FAR_RET_IRET, ra);
    }
    return prev;
}

/* CS.base + EIP of the current code (RIP in 64-bit mode), 32 bits outside 64-bit mode */
static uint64_t cet2_cur_lip(CPUX86State *env, target_ulong eip)
{
    if (env->hflags & HF_CS64_MASK) {
        return eip;
    }
    return (uint32_t)(env->segs[R_CS].base + eip);
}

/*
 * Far CALL to a code segment or through a call gate without a privilege change (SDM Vol2
 * CALL, CONFORMING/NONCONFORMING-CODE-SEGMENT and SAME-PRIVILEGE): planned after the
 * data-stack pushes are checked, before any of them is written. check4g: the code-segment
 * forms' "SSP must be in low 4GB" check for a legacy/compatibility-mode target (#GP(0)); the
 * call-gate form has none.
 */
static void cet2_plan_same(CPUX86State *env, Cet2Xfer *x, uint32_t new_e2, uint64_t lip,
                           bool check4g, uintptr_t ra)
{
    int cpl = env->hflags & HF_CPL_MASK;
    uint64_t ssp = env->ssp;

    memset(x, 0, sizeof(*x));
    if (!cet2_ss_en(env, cpl, env->eflags & VM_MASK)) {
        return;
    }
    x->en = true;
    x->lm = (env->hflags & HF_LMA_MASK) && (new_e2 & DESC_L_MASK);
    x->user = cpl == 3;
    if (check4g && !x->lm && (ssp >> 32)) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, ra);
    }
    x->cs = env->segs[R_CS].selector;
    x->lip = lip;
    x->saved = ssp;
    cet2_plan_align(env, x, ssp, ra);
}

/*
 * The LIP a far CALL to a code segment pushes (SDM Vol2 CALL): conforming target - RIP for
 * a 64-bit operand, CS.base + EIP / CS.base + IP otherwise; non-conforming target - RIP
 * in 64-bit mode, else CS.base + EIP. 32-bit values are zero-extended; CS.base is 0 in
 * 64-bit mode.
 */
static uint64_t cet2_call_lip(CPUX86State *env, uint32_t new_e2, int shift,
                              target_ulong next_eip)
{
    bool cs64 = env->hflags & HF_CS64_MASK;
    uint64_t base = cs64 ? 0 : env->segs[R_CS].base;

    if (new_e2 & DESC_C_MASK) {
        if (shift == 2) {
            return next_eip;
        }
        return (uint32_t)(base + (shift ? (uint32_t)next_eip : (next_eip & 0xffff)));
    }
    return cs64 ? next_eip : (uint32_t)(base + next_eip);
}

/*
 * Far CALL through a call gate to a more privileged level (SDM Vol2 CALL, MORE-PRIVILEGE):
 * planned after the new stack's slots are checked, before anything is pushed. From CPL 3
 * with user shadow stacks IA32_PL3_SSP gets SSP (LA_adjust in IA-32e mode); with shadow
 * stacks at the new CPL dpl the token at IA32_PLdpl_SSP is checked (#GP(0)) and, unless the
 * old SS.DPL is 3, CS / LIP / SSP of the caller are pushed on the new shadow stack.
 */
static void cet2_plan_inner(CPUX86State *env, Cet2Xfer *x, int dpl, uint32_t new_e2,
                            uint64_t lip, uintptr_t ra)
{
    int cpl = env->hflags & HF_CPL_MASK;
    bool lma = env->hflags & HF_LMA_MASK;
    uint64_t ssp = env->ssp;

    memset(x, 0, sizeof(*x));
    if (cpl == 3 && cet2_ss_en(env, cpl, env->eflags & VM_MASK)) {
        x->pl3 = true;
        x->pl3_val = lma ? cet2_la_adjust(env, ssp) : ssp;
    }
    if (!cet2_ss_en(env, dpl, false)) {
        return;
    }
    x->en = true;
    x->lm = lma && (new_e2 & DESC_L_MASK);
    cet2_plan_token(env, x, env->pl_ssp[dpl], ra);
    if (((env->segs[R_SS].flags >> DESC_DPL_SHIFT) & 3) != 3) {
        x->cs = env->segs[R_CS].selector;
        x->lip = lip;
        x->saved = ssp;
        cet2_plan_frame(env, x, x->tok, ra);
    }
}

/*
 * RET far / IRET in protected or IA-32e mode, not to virtual-8086 mode (SDM Vol2 RET and
 * IRET, RETURN-TO-SAME/OUTER-PRIVILEGE-LEVEL): called after every other check of the
 * instruction and before the transfer commits; commits SSP itself. rpl: the return CS
 * RPL (the new CPL); e1/e2: the return code-segment descriptor.
 *  - same privilege, shadow stacks at the CPL: SSP 8-byte aligned (#CP), frame popped and
 *    checked, the popped SSP usable in the target mode (#GP(0)); IRET in IA-32e mode then
 *    frees a busy token at the SSP after the frame when the popped SSP is elsewhere (an
 *    IST stack switch);
 *  - outer privilege: with shadow stacks at the old CPL SSP 8-byte aligned (#CP) and,
 *    unless returning to CPL 3, the frame popped and checked; with shadow stacks at the
 *    new CPL SSP becomes the popped SSP or IA32_PL3_SSP (#GP(0) unless usable); the
 *    supervisor token at the old SSP is freed (supervisor access).
 */
static void cet2_ret(CPUX86State *env, bool is_iret, int rpl, uint32_t new_cs,
                     uint32_t e1, uint32_t e2, target_ulong new_eip, uintptr_t ra)
{
    int cpl = env->hflags & HF_CPL_MASK;
    bool lm_old = env->hflags & HF_CS64_MASK;
    bool lm_new = (env->hflags & HF_LMA_MASK) && (e2 & DESC_L_MASK);
    uint64_t base = lm_new ? 0 : (uint32_t)((e1 >> 16) | ((e2 & 0xff) << 16) |
                                            (e2 & 0xff000000));
    uint64_t lip = lm_new ? (uint64_t)new_eip : (uint32_t)(base + new_eip);
    uint64_t ssp = env->ssp, tmp = 0;
    bool old_en = cet2_ss_en(env, cpl, false), new_en;

    if (rpl == cpl) {
        if (!old_en) {
            return;
        }
        if (ssp & 7) {
            raise_exception_err_ra(env, EXCP15_CP, CP_FAR_RET_IRET, ra);
        }
        tmp = cet2_pop_frame(env, &ssp, new_cs, lip, lm_old, cpl == 3, ra);
        cet2_check_ret_ssp(env, tmp, lm_new, ra);
        if (is_iret && (env->hflags & HF_LMA_MASK) && tmp != ssp) {
            cet2_token_release(env, ssp, lm_old, cpl == 3, ra);
        }
        env->ssp = tmp;
        return;
    }
    if (old_en) {
        if (ssp & 7) {
            raise_exception_err_ra(env, EXCP15_CP, CP_FAR_RET_IRET, ra);
        }
        if (rpl != 3) {
            tmp = cet2_pop_frame(env, &ssp, new_cs, lip, lm_old, false, ra);
        }
    }
    new_en = cet2_ss_en(env, rpl, false);
    if (new_en) {
        if (rpl == 3) {
            tmp = env->pl_ssp[3];
        }
        cet2_check_ret_ssp(env, tmp, lm_new, ra);
    }
    if (old_en) {
        cet2_token_release(env, ssp, lm_old, false, ra);
    }
    if (new_en) {
        env->ssp = tmp;
    }
}

#endif /* __Use_Original_Qemu (U750) */
static inline void get_ss_esp_from_tss(CPUX86State *env, uint32_t *ss_ptr,
                                       uint32_t *esp_ptr, int dpl,
                                       uintptr_t retaddr)
{
    X86CPU *cpu = env_archcpu(env);
    int type, index, shift;

#if 0
    {
        int i;
        printf("TR: base=%p limit=%x\n", env->tr.base, env->tr.limit);
        for (i = 0; i < env->tr.limit; i++) {
            printf("%02x ", env->tr.base[i]);
            if ((i & 7) == 7) {
                printf("\n");
            }
        }
        printf("\n");
    }
#endif

    if (!(env->tr.flags & DESC_P_MASK)) {
        cpu_abort(CPU(cpu), "invalid tss");
    }
    type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    if ((type & 7) != 1) {
        cpu_abort(CPU(cpu), "invalid tss type");
    }
    shift = type >> 3;
    index = (dpl * 4 + 2) << shift;
    if (index + (4 << shift) - 1 > env->tr.limit) {
        raise_exception_err_ra(env, EXCP0A_TSS, env->tr.selector & 0xfffc, retaddr);
    }
    if (shift == 0) {
        *esp_ptr = cpu_lduw_kernel_ra(env, env->tr.base + index, retaddr);
        *ss_ptr = cpu_lduw_kernel_ra(env, env->tr.base + index + 2, retaddr);
    } else {
        *esp_ptr = cpu_ldl_kernel_ra(env, env->tr.base + index, retaddr);
        *ss_ptr = cpu_lduw_kernel_ra(env, env->tr.base + index + 4, retaddr);
    }
}

static void tss_load_seg(CPUX86State *env, int seg_reg, int selector, int cpl,
                         uintptr_t retaddr)
{
    uint32_t e1, e2;
    int rpl, dpl;

    if ((selector & 0xfffc) != 0) {
        if (load_segment_ra(env, &e1, &e2, selector, retaddr) != 0) {
            raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
        }
        if (!(e2 & DESC_S_MASK)) {
            raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
        }
        rpl = selector & 3;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (seg_reg == R_CS) {
            if (!(e2 & DESC_CS_MASK)) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
            if (dpl != rpl) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
        } else if (seg_reg == R_SS) {
            /* SS must be writable data */
            if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
            if (dpl != cpl || dpl != rpl) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
        } else {
            /* not readable code */
            if ((e2 & DESC_CS_MASK) && !(e2 & DESC_R_MASK)) {
                raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
            }
            /* if data or non conforming code, checks the rights */
            if (((e2 >> DESC_TYPE_SHIFT) & 0xf) < 12) {
                if (dpl < cpl || dpl < rpl) {
                    raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
                }
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, retaddr);
        }
        cpu_x86_load_seg_cache(env, seg_reg, selector,
                               get_seg_base(e1, e2),
                               get_seg_limit(e1, e2),
                               e2);
    } else {
        if (seg_reg == R_SS || seg_reg == R_CS) {
            raise_exception_err_ra(env, EXCP0A_TSS, selector & 0xfffc, retaddr);
        }
    }
}

#define SWITCH_TSS_JMP  0
#define SWITCH_TSS_IRET 1
#define SWITCH_TSS_CALL 2

/* XXX: restore CPU state in registers (PowerPC case) */
static void switch_tss_ra(CPUX86State *env, int tss_selector,
                          uint32_t e1, uint32_t e2, int source,
                          uint32_t next_eip, uintptr_t retaddr)
{
    int tss_limit, tss_limit_max, type, old_tss_limit_max, old_type, v1, v2, i;
    target_ulong tss_base;
    uint32_t new_regs[8], new_segs[6];
    uint32_t new_eflags, new_eip, new_cr3, new_ldt, new_trap;
    uint32_t old_eflags, eflags_mask;
    SegmentCache *dt;
    int index;
    target_ulong ptr;
#if __Use_Original_Qemu != 1 /* ours (U754) */
    bool cet_push = false, cet_verify = false;
    uint64_t cet_cs = 0, cet_lip = 0, cet_ssp = 0;
#endif /* __Use_Original_Qemu (U754) */

    type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
    LOG_PCALL("switch_tss: sel=0x%04x type=%d src=%d\n", tss_selector, type,
              source);

    /* if task gate, we read the TSS segment and we load it */
    if (type == 5) {
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, tss_selector & 0xfffc, retaddr);
        }
        tss_selector = e1 >> 16;
        if (tss_selector & 4) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, retaddr);
        }
        if (load_segment_ra(env, &e1, &e2, tss_selector, retaddr) != 0) {
            raise_exception_err_ra(env, EXCP0D_GPF, tss_selector & 0xfffc, retaddr);
        }
        if (e2 & DESC_S_MASK) {
            raise_exception_err_ra(env, EXCP0D_GPF, tss_selector & 0xfffc, retaddr);
        }
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((type & 7) != 1) {
            raise_exception_err_ra(env, EXCP0D_GPF, tss_selector & 0xfffc, retaddr);
        }
    }

    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err_ra(env, EXCP0B_NOSEG, tss_selector & 0xfffc, retaddr);
    }

    if (type & 8) {
        tss_limit_max = 103;
    } else {
        tss_limit_max = 43;
    }
    tss_limit = get_seg_limit(e1, e2);
    tss_base = get_seg_base(e1, e2);
    if ((tss_selector & 4) != 0 ||
        tss_limit < tss_limit_max) {
        raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, retaddr);
    }
#if __Use_Original_Qemu != 1 /* ours (U754) */
    /*
     * NoVmp (ledger U754): SDM Vol3A 10.3 step 3 - a task switch by IRET with shadow
     * stacks at the current CPL needs SSP 8-byte aligned, else #TS(current task TSS); with
     * CR4.CET = 1 the new TSS must be a 32-bit TSS with a limit >= 107 (the task's SSP is
     * at offset 104), else #TS(new task TSS).
     */
    if (source == SWITCH_TSS_IRET &&
        cet2_ss_en(env, env->hflags & HF_CPL_MASK, env->eflags & VM_MASK) &&
        (env->ssp & 7)) {
        raise_exception_err_ra(env, EXCP0A_TSS, env->tr.selector & 0xfffc, retaddr);
    }
    if ((env->cr[4] & CR4_CET_MASK) && (!(type & 8) || tss_limit < 107)) {
        raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, retaddr);
    }
#endif /* __Use_Original_Qemu (U754) */
    old_type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    if (old_type & 8) {
        old_tss_limit_max = 103;
    } else {
        old_tss_limit_max = 43;
    }

    /* read all the registers from the new TSS */
    if (type & 8) {
        /* 32 bit */
        new_cr3 = cpu_ldl_kernel_ra(env, tss_base + 0x1c, retaddr);
        new_eip = cpu_ldl_kernel_ra(env, tss_base + 0x20, retaddr);
        new_eflags = cpu_ldl_kernel_ra(env, tss_base + 0x24, retaddr);
        for (i = 0; i < 8; i++) {
            new_regs[i] = cpu_ldl_kernel_ra(env, tss_base + (0x28 + i * 4),
                                            retaddr);
        }
        for (i = 0; i < 6; i++) {
            new_segs[i] = cpu_lduw_kernel_ra(env, tss_base + (0x48 + i * 4),
                                             retaddr);
        }
        new_ldt = cpu_lduw_kernel_ra(env, tss_base + 0x60, retaddr);
        new_trap = cpu_ldl_kernel_ra(env, tss_base + 0x64, retaddr);
    } else {
        /* 16 bit */
        new_cr3 = 0;
        new_eip = cpu_lduw_kernel_ra(env, tss_base + 0x0e, retaddr);
        new_eflags = cpu_lduw_kernel_ra(env, tss_base + 0x10, retaddr);
        for (i = 0; i < 8; i++) {
#if __Use_Original_Qemu == 1 /* original QEMU (U464) */
            /* QEMU a5505f6b5b: bits 31:16 are merged from the old value below */
            new_regs[i] = cpu_lduw_kernel_ra(env, tss_base + (0x12 + i * 2), retaddr);
#else /* ours (U464) */
            /*
             * NoVmp (ledger U464): SDM Vol3A 10.6 "When the general-purpose registers
             * are loaded or saved from a 16-bit TSS, the upper 16 bits of the registers
             * are modified and not maintained": not kept (upstream keeps them); the
             * value is not specified, FFFFh as before (docs/quirks.md, SDM undefined).
             */
            new_regs[i] = cpu_lduw_kernel_ra(env, tss_base + (0x12 + i * 2),
                                             retaddr) | 0xffff0000;
#endif /* __Use_Original_Qemu (U464) */
        }
        for (i = 0; i < 4; i++) {
            /* 2-byte slots (QEMU 28f6aa1178; SDM Vol3A Figure 10-10) */
            new_segs[i] = cpu_lduw_kernel_ra(env, tss_base + (0x22 + i * 2),
                                             retaddr);
        }
        new_ldt = cpu_lduw_kernel_ra(env, tss_base + 0x2a, retaddr);
        new_segs[R_FS] = 0;
        new_segs[R_GS] = 0;
        new_trap = 0;
    }
    /* XXX: avoid a compiler warning, see
     http://support.amd.com/us/Processor_TechDocs/24593.pdf
     chapters 12.2.5 and 13.2.4 on how to implement TSS Trap bit */
    (void)new_trap;

    /* NOTE: we must avoid memory exceptions during the task switch,
       so we make dummy accesses before */
    /* XXX: it can still fail in some cases, so a bigger hack is
       necessary to valid the TLB after having done the accesses */

    v1 = cpu_ldub_kernel_ra(env, env->tr.base, retaddr);
    v2 = cpu_ldub_kernel_ra(env, env->tr.base + old_tss_limit_max, retaddr);
    cpu_stb_kernel_ra(env, env->tr.base, v1, retaddr);
    cpu_stb_kernel_ra(env, env->tr.base + old_tss_limit_max, v2, retaddr);

    /* clear busy bit (it is restartable) */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_IRET) {
        target_ulong ptr;
        uint32_t e2;

        ptr = env->gdt.base + (env->tr.selector & ~7);
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);
        e2 &= ~DESC_TSS_BUSY_MASK;
        cpu_stl_kernel_ra(env, ptr + 4, e2, retaddr);
    }
    old_eflags = cpu_compute_eflags(env);
    if (source == SWITCH_TSS_IRET) {
        old_eflags &= ~NT_MASK;
    }

    /* save the current state in the old TSS, in its own format (QEMU 1b627f389f) */
    if (old_type & 8) {
        /* 32 bit */
        cpu_stl_kernel_ra(env, env->tr.base + 0x20, next_eip, retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + 0x24, old_eflags, retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 0 * 4), env->regs[R_EAX], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 1 * 4), env->regs[R_ECX], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 2 * 4), env->regs[R_EDX], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 3 * 4), env->regs[R_EBX], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 4 * 4), env->regs[R_ESP], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 5 * 4), env->regs[R_EBP], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 6 * 4), env->regs[R_ESI], retaddr);
        cpu_stl_kernel_ra(env, env->tr.base + (0x28 + 7 * 4), env->regs[R_EDI], retaddr);
        for (i = 0; i < 6; i++) {
            cpu_stw_kernel_ra(env, env->tr.base + (0x48 + i * 4),
                              env->segs[i].selector, retaddr);
        }
    } else {
        /* 16 bit */
        cpu_stw_kernel_ra(env, env->tr.base + 0x0e, next_eip, retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + 0x10, old_eflags, retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 0 * 2), env->regs[R_EAX], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 1 * 2), env->regs[R_ECX], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 2 * 2), env->regs[R_EDX], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 3 * 2), env->regs[R_EBX], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 4 * 2), env->regs[R_ESP], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 5 * 2), env->regs[R_EBP], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 6 * 2), env->regs[R_ESI], retaddr);
        cpu_stw_kernel_ra(env, env->tr.base + (0x12 + 7 * 2), env->regs[R_EDI], retaddr);
        for (i = 0; i < 4; i++) {
            /* 2-byte slots (QEMU 28f6aa1178) */
            cpu_stw_kernel_ra(env, env->tr.base + (0x22 + i * 2),
                              env->segs[i].selector, retaddr);
        }
    }

#if __Use_Original_Qemu != 1 /* ours (U754) */
    /*
     * NoVmp (ledger U754): SDM Vol3A 10.3 step 8 (before the new task's state is loaded).
     * The new task's CPL is 3 with EFLAGS.VM = 1 in its TSS, else its CS.RPL. With shadow
     * stacks at the current CPL:
     *  - CALL / exception / interrupt: from CPL 3 to a lower CPL IA32_PL3_SSP := SSP;
     *    otherwise CS, LIP (CS.base + EIP) and SSP of the old task are pushed on the new
     *    task's shadow stack in step 15;
     *  - IRET: when the new CPL equals the current one or is below 3, CS / LIP / SSP are
     *    popped from the current shadow stack (checked in step 15); then the busy token at
     *    the (8-byte aligned) SSP is freed and SSP := 0.
     */
    {
        int old_cpl = env->hflags & HF_CPL_MASK;
        int new_cpl = (new_eflags & VM_MASK) ? 3 : (new_segs[R_CS] & 3);

        if (cet2_ss_en(env, old_cpl, env->eflags & VM_MASK)) {
            if (source == SWITCH_TSS_CALL) {
                if (new_cpl < old_cpl && old_cpl == 3) {
                    env->pl_ssp[3] = env->ssp;
                } else {
                    cet_push = true;
                    cet_ssp = env->ssp;
                    cet_lip = (uint32_t)(env->segs[R_CS].base + next_eip);
                    cet_cs = env->segs[R_CS].selector;
                }
            } else if (source == SWITCH_TSS_IRET) {
                uint64_t ssp = env->ssp;

                if (new_cpl == old_cpl || new_cpl < 3) {
                    cet_cs = cet2_ld8(env, ssp + 16, false, old_cpl == 3, retaddr);
                    cet_lip = cet2_ld8(env, ssp + 8, false, old_cpl == 3, retaddr);
                    cet_ssp = cet2_ld8(env, ssp, false, old_cpl == 3, retaddr);
                    ssp = (uint32_t)(ssp + 24);
                    cet_verify = true;
                }
                if (!(ssp & 7)) {
                    cet2_cmpxchg8(env, ssp, ssp, (ssp & ~7ull) | 1, false, old_cpl == 3,
                                  retaddr);
                }
                env->ssp = 0;
            }
        }
    }
#endif /* __Use_Original_Qemu (U754) */
    /* now if an exception occurs, it will occurs in the next task
       context */

    if (source == SWITCH_TSS_CALL) {
        cpu_stw_kernel_ra(env, tss_base, env->tr.selector, retaddr);
        new_eflags |= NT_MASK;
    }

    /* set busy bit */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_CALL) {
        target_ulong ptr;
        uint32_t e2;

        ptr = env->gdt.base + (tss_selector & ~7);
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);
        e2 |= DESC_TSS_BUSY_MASK;
        cpu_stl_kernel_ra(env, ptr + 4, e2, retaddr);
    }

    /* set the new CPU state */
    /* from this point, any exception which occurs can give problems */
    env->cr[0] |= CR0_TS_MASK;
    env->hflags |= HF_TS_MASK;
    env->tr.selector = tss_selector;
    env->tr.base = tss_base;
    env->tr.limit = tss_limit;
    env->tr.flags = e2 & ~DESC_TSS_BUSY_MASK;

    if ((type & 8) && (env->cr[0] & CR0_PG_MASK)) {
        cpu_x86_update_cr3(env, new_cr3);
    }

    /* load all registers without an exception, then reload them with
       possible exception */
    env->eip = new_eip;
    eflags_mask = TF_MASK | AC_MASK | ID_MASK |
        IF_MASK | IOPL_MASK | VM_MASK | RF_MASK | NT_MASK;
#if __Use_Original_Qemu == 1 /* original QEMU (U464) */
    /* QEMU a5505f6b5b: a 16-bit TSS loads only bits 15:0 of the GPRs */
    if (type & 8) {
        cpu_load_eflags(env, new_eflags, eflags_mask);
        for (i = 0; i < 8; i++) {
            env->regs[i] = new_regs[i];
        }
    } else {
        cpu_load_eflags(env, new_eflags, eflags_mask & 0xffff);
        for (i = 0; i < 8; i++) {
            env->regs[i] = (env->regs[i] & 0xffff0000) | new_regs[i];
        }
    }
#else /* ours (U464) */
    if (!(type & 8)) {
        eflags_mask &= 0xffff;
    }
    cpu_load_eflags(env, new_eflags, eflags_mask);
    /* 16-bit TSS: bits 31:16 FFFFh from new_regs (U464, SDM Vol3A 10.6) */
    env->regs[R_EAX] = new_regs[0];
    env->regs[R_ECX] = new_regs[1];
    env->regs[R_EDX] = new_regs[2];
    env->regs[R_EBX] = new_regs[3];
    env->regs[R_ESP] = new_regs[4];
    env->regs[R_EBP] = new_regs[5];
    env->regs[R_ESI] = new_regs[6];
    env->regs[R_EDI] = new_regs[7];
#endif /* __Use_Original_Qemu (U464) */
    if (new_eflags & VM_MASK) {
        for (i = 0; i < 6; i++) {
            load_seg_vm(env, i, new_segs[i]);
        }
    } else {
        /* first just selectors as the rest may trigger exceptions */
        for (i = 0; i < 6; i++) {
            cpu_x86_load_seg_cache(env, i, new_segs[i], 0, 0, 0);
        }
    }

    env->ldt.selector = new_ldt & ~4;
    env->ldt.base = 0;
    env->ldt.limit = 0;
    env->ldt.flags = 0;

    /* load the LDT */
    if (new_ldt & 4) {
        raise_exception_err_ra(env, EXCP0A_TSS, new_ldt & 0xfffc, retaddr);
    }

    if ((new_ldt & 0xfffc) != 0) {
        dt = &env->gdt;
        index = new_ldt & ~7;
        if ((index + 7) > dt->limit) {
            raise_exception_err_ra(env, EXCP0A_TSS, new_ldt & 0xfffc, retaddr);
        }
        ptr = dt->base + index;
        e1 = cpu_ldl_kernel_ra(env, ptr, retaddr);
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, retaddr);
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2) {
            raise_exception_err_ra(env, EXCP0A_TSS, new_ldt & 0xfffc, retaddr);
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0A_TSS, new_ldt & 0xfffc, retaddr);
        }
        load_seg_cache_raw_dt(&env->ldt, e1, e2);
    }

    /* load the segments */
    if (!(new_eflags & VM_MASK)) {
        int cpl = new_segs[R_CS] & 3;
        tss_load_seg(env, R_CS, new_segs[R_CS], cpl, retaddr);
        tss_load_seg(env, R_SS, new_segs[R_SS], cpl, retaddr);
        tss_load_seg(env, R_ES, new_segs[R_ES], cpl, retaddr);
        tss_load_seg(env, R_DS, new_segs[R_DS], cpl, retaddr);
        tss_load_seg(env, R_FS, new_segs[R_FS], cpl, retaddr);
        tss_load_seg(env, R_GS, new_segs[R_GS], cpl, retaddr);
    }

#if __Use_Original_Qemu != 1 /* ours (U754) */
    /*
     * NoVmp (ledger U754): SDM Vol3A 10.3 step 15, in the context of the new task (its
     * faults report the new task's CS:EIP, so they are raised without restoring the old
     * instruction's state). "Enabled at the current CPL" here is CR4.CET and the enable bit
     * of IA32_U_CET (CPL 3) / IA32_S_CET (CPL < 3) - EFLAGS.VM = 1 with either is #TS(new
     * TSS). CALL / JMP / exception / interrupt with shadow stacks: the 32-bit SSP at TSS
     * offset 104 must be 8-byte aligned and hold a free supervisor token (busy bit set by
     * the locked compare-exchange; else #TS(new TSS)), then the old task's CS / LIP / SSP
     * are pushed when step 8 asked for it. IRET: the popped CS / LIP must equal CS and
     * CS.base + EIP (#CP(FAR-RET/IRET)); with shadow stacks at the new CPL SSP becomes the
     * popped SSP, or IA32_PL3_SSP when nothing was popped (#CP unless 4-byte aligned and
     * below 4 GB). The IBT tracker of the new CPL waits for ENDBRANCH except after IRET.
     */
    {
        int ncpl = env->hflags & HF_CPL_MASK;
        bool nvm = env->eflags & VM_MASK;
        uint64_t ncet = cet2_msr(env, ncpl);
        bool ss_on = cet2_ss_cpuid(env) && (env->cr[4] & CR4_CET_MASK) &&
                     (ncet & CET_SH_STK_EN);
        bool ibt_on = cet2_ibt_cpuid(env) && (env->cr[4] & CR4_CET_MASK) &&
                      (ncet & CET_ENDBR_EN);

        if ((ss_on || ibt_on) && nvm) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, 0);
        }
        if (ss_on && source != SWITCH_TSS_IRET) {
            uint64_t nssp = cpu_ldl_kernel_ra(env, tss_base + 104, 0);

            if ((nssp & 7) ||
                cet2_cmpxchg8(env, nssp, nssp | 1, nssp, false, ncpl == 3, 0) != nssp) {
                raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, 0);
            }
            if (cet_push) {
                nssp = cet2_push_frame(env, nssp, cet_cs, cet_lip, cet_ssp, false,
                                       ncpl == 3, 0);
            }
            env->ssp = nssp;
        }
        if (source == SWITCH_TSS_IRET) {
            if (cet_verify &&
                (cet_cs != env->segs[R_CS].selector ||
                 cet_lip != (uint32_t)(env->segs[R_CS].base + env->eip))) {
                raise_exception_err_ra(env, EXCP15_CP, CP_FAR_RET_IRET, 0);
            }
            if (cet2_ss_en(env, ncpl, nvm)) {
                if (!cet_verify) {
                    cet_ssp = env->pl_ssp[3];
                }
                if ((cet_ssp & 3) || (cet_ssp >> 32)) {
                    raise_exception_err_ra(env, EXCP15_CP, CP_FAR_RET_IRET, 0);
                }
                env->ssp = cet_ssp;
            }
        } else if (cet2_ibt_en(env, ncpl, nvm)) {
            cet2_ibt_wait(env, ncpl);
        }
    }
#endif /* __Use_Original_Qemu (U754) */
    /* check that env->eip is in the CS segment limits */
    if (new_eip > env->segs[R_CS].limit) {
        /* XXX: different exception if CALL? */
        raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
    }

    /* reset local breakpoints */
    if (env->dr[7] & DR7_LOCAL_BP_MASK) {
        cpu_x86_update_dr7(env, env->dr[7] & ~DR7_LOCAL_BP_MASK);
    }
}

static void switch_tss(CPUX86State *env, int tss_selector,
                       uint32_t e1, uint32_t e2, int source,
                        uint32_t next_eip)
{
    switch_tss_ra(env, tss_selector, e1, e2, source, next_eip, 0);
}

static inline unsigned int get_sp_mask(unsigned int e2)
{
#ifdef TARGET_X86_64
    if (e2 & DESC_L_MASK) {
        return 0;
    } else
#endif
    if (e2 & DESC_B_MASK) {
        return 0xffffffff;
    } else {
        return 0xffff;
    }
}

static int exception_has_error_code(int intno)
{
    switch (intno) {
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 17:
#if __Use_Original_Qemu != 1 /* ours (U114) */
    case 21:    /* #CP pushes an error code (SDM Vol3 7.15, interrupt 21) */
#endif /* __Use_Original_Qemu (U114) */
        return 1;
    }
    return 0;
}

#ifdef TARGET_X86_64
#define SET_ESP(val, sp_mask)                                   \
    do {                                                        \
        if ((sp_mask) == 0xffff) {                              \
            env->regs[R_ESP] = (env->regs[R_ESP] & ~0xffff) |   \
                ((val) & 0xffff);                               \
        } else if ((sp_mask) == 0xffffffffLL) {                 \
            env->regs[R_ESP] = (uint32_t)(val);                 \
        } else {                                                \
            env->regs[R_ESP] = (val);                           \
        }                                                       \
    } while (0)
#else
#define SET_ESP(val, sp_mask)                                   \
    do {                                                        \
        env->regs[R_ESP] = (env->regs[R_ESP] & ~(sp_mask)) |    \
            ((val) & (sp_mask));                                \
    } while (0)
#endif

/* in 64-bit machines, this can overflow. So this segment addition macro
 * can be used to trim the value to 32-bit whenever needed */
#if __Use_Original_Qemu == 1 /* original QEMU (U591) */
#define SEG_ADDL(ssp, sp, sp_mask) ((uint32_t)((ssp) + (sp & (sp_mask))))
#else /* ours (U591) */
/*
 * NoVmp (ledger U591): a 64-bit all-ones sp_mask is the 64-bit stack pointer of 64-bit mode
 * (SDM Vol2B POP: "in 64-bit mode, the size of the stack pointer is always 64 bits"), also
 * for the 16/32-bit pops and pushes of RETF/IRET/far CALL there: no 32-bit wrap of the
 * address. Every other mask (FFFFh, FFFFFFFFh: SS.B outside 64-bit mode) wraps as before.
 */
#define SEG_ADDL(ssp, sp, sp_mask)                                              \
    ((target_ulong)(sp_mask) == ~(target_ulong)0 ? (target_ulong)((ssp) + (sp)) \
                                                 : (target_ulong)(uint32_t)((ssp) + ((sp) & (sp_mask))))

/*
 * NoVmp (ledger U591): the stack of a far transfer that starts in 64-bit mode is RSP with all
 * 64 bits and SS.base 0 (SDM Vol1 7.3.1.5, Vol3A 3.4.4), whatever the operand size and SS.B
 * (Unicorn's reset leaves the SS cache zero: B = 0 would mean SP, a 16-bit stack pointer).
 */
static inline target_ulong x86_stack_mask64(CPUX86State *env, uint32_t ss_flags)
{
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        return ~(target_ulong)0;
    }
#endif
    return get_sp_mask(ss_flags);
}
#endif /* __Use_Original_Qemu (U591) */

/* XXX: add a is_user flag to have proper security support */
#define PUSHW_RA(ssp, sp, sp_mask, val, ra)                      \
    {                                                            \
        sp -= 2;                                                 \
        cpu_stw_kernel_ra(env, (ssp) + (sp & (sp_mask)), (val), ra); \
    }

#define PUSHL_RA(ssp, sp, sp_mask, val, ra)                             \
    {                                                                   \
        sp -= 4;                                                        \
        cpu_stl_kernel_ra(env, SEG_ADDL(ssp, sp, sp_mask), (uint32_t)(val), ra); \
    }

/*
 * backport 0bd385e7e3 (U481): the stack pops of IRET and RET far are ordinary data
 * accesses at the current privilege level, not supervisor accesses (with CR4.SMAP = 1 a
 * CPL3 IRET/RETF from a user stack must not fault; at CPL3 U/S is checked).
 */
#define POPW_RA(ssp, sp, sp_mask, val, ra)                       \
    {                                                            \
        val = cpu_lduw_data_ra(env, (ssp) + (sp & (sp_mask)), ra); \
        sp += 2;                                                 \
    }

#define POPL_RA(ssp, sp, sp_mask, val, ra)                              \
    {                                                                   \
        val = (uint32_t)cpu_ldl_data_ra(env, SEG_ADDL(ssp, sp, sp_mask), ra); \
        sp += 4;                                                        \
    }

/*
 * backport e136648c5c (U481): far CALL stack accesses at the privilege level of the stack
 * they use - the current CPL, or the gate's DPL for the new (inner) stack - instead of
 * supervisor accesses (upstream x86_mmu_index_pl()).
 */
static inline int x86_mmu_index_pl(CPUX86State *env, unsigned pl)
{
    return pl == 3 ? MMU_USER_IDX :
        (!(env->hflags & HF_SMAP_MASK) || (env->eflags & AC_MASK))
        ? MMU_KNOSMAP_IDX : MMU_KSMAP_IDX;
}

#if __Use_Original_Qemu != 1 /* ours (U708) */
/*
 * NoVmp (ledger U708): far CALL pushes 2 (direct, same-privilege gate) or 4 + parameters
 * (gate to an inner level, onto the new stack) slots; a #PF / #SS on a later slot left the
 * earlier ones written (and Unicorn, whose store to a page it has not mapped only requests an
 * exit, even completed the CALL). The n slots of 'size' bytes below sp are checked first, in
 * push order and addressed as PUSHW_PL / PUSHL_PL / PUSHQ_PL do, at privilege level pl
 * (x86_probe_write_mmu writes nothing). SDM Vol3A 6.15: the faulting instruction is not
 * executed.
 */
static void far_probe_pushes(CPUX86State *env, target_ulong ssp, target_ulong sp,
                             target_ulong sp_mask, int size, int n, unsigned pl, uintptr_t ra)
{
    int mmu_idx = x86_mmu_index_pl(env, pl);
    int i;

    for (i = 1; i <= n; i++) {
        target_ulong s = sp - (target_ulong)size * i;
        target_ulong a = size == 8 ? s : size == 4 ? SEG_ADDL(ssp, s, sp_mask)
                                                   : (ssp) + (s & (sp_mask));

        x86_probe_write_mmu(env, a, size, mmu_idx, ra);
    }
}
#endif /* __Use_Original_Qemu (U708) */

#define PUSHW_PL(ssp, sp, sp_mask, val, pl, ra)                          \
    {                                                                    \
        sp -= 2;                                                         \
        cpu_stw_mmuidx_ra(env, (ssp) + (sp & (sp_mask)), (val),          \
                          x86_mmu_index_pl(env, pl), ra);                \
    }

#define PUSHL_PL(ssp, sp, sp_mask, val, pl, ra)                          \
    {                                                                    \
        sp -= 4;                                                         \
        cpu_stl_mmuidx_ra(env, SEG_ADDL(ssp, sp, sp_mask), (uint32_t)(val), \
                          x86_mmu_index_pl(env, pl), ra);                \
    }

#define PUSHW(ssp, sp, sp_mask, val) PUSHW_RA(ssp, sp, sp_mask, val, 0)
#define PUSHL(ssp, sp, sp_mask, val) PUSHL_RA(ssp, sp, sp_mask, val, 0)
#define POPW(ssp, sp, sp_mask, val) POPW_RA(ssp, sp, sp_mask, val, 0)
#define POPL(ssp, sp, sp_mask, val) POPL_RA(ssp, sp, sp_mask, val, 0)

#if __Use_Original_Qemu != 1 /* ours (U755) */
/*
 * NoVmp (ledger U755): CET on event delivery through an interrupt or trap gate (SDM Vol2
 * INT n/INTO/INT3/INT1 Operation - which "applies ... also to the delivery of external
 * interrupts, NMIs and exceptions" - and Vol3A 7.12.1.1, 7.14.5). Called after the stack
 * frame is written and before the transfer commits; commits SSP, IA32_PL3_SSP and the
 * IBT tracker. inner: the handler runs at dpl < cpl (vm86: from virtual-8086 mode, dpl
 * = 0); ist: the IST index of a 64-bit gate; old_eip: the return EIP pushed.
 *  - INTER-PRIVILEGE: from CPL 3 with user shadow stacks IA32_PL3_SSP := SSP (LA_adjust
 *    in IA-32e mode); with shadow stacks at dpl the new SSP is IA32_PLdpl_SSP, or in
 *    IA-32e mode with IST != 0 the entry IST of the table at IA32_INTERRUPT_SSP_TABLE_ADDR
 *    (8-byte supervisor data read, done only when shadow stacks are enabled at CPL 0);
 *    its supervisor token is acquired (#GP(0)) and, unless the old SS.DPL is 3 or the
 *    event came from virtual-8086 mode, CS / LIP / SSP of the interrupted code pushed;
 *  - INTRA-PRIVILEGE: with shadow stacks at the CPL the frame goes on the current shadow
 *    stack (4 zero bytes at SSP - 4, SSP aligned down to 8) - in IA-32e mode with IST != 0
 *    on the IST shadow stack after its token is acquired;
 *  - EndbranchEnabled at the new CPL: its tracker waits for ENDBRANCH, unsuppressed.
 * In Unicorn exceptions and INT n are reported to UC_HOOK_INTR instead of being delivered
 * through the IDT, so this runs only for events the IDT path delivers.
 */
static void cet2_event(CPUX86State *env, bool inner, int cpl, int dpl, uint32_t new_e2,
                       int ist, target_ulong old_eip)
{
    bool lma = env->hflags & HF_LMA_MASK;
    bool vm = env->eflags & VM_MASK;
    uint64_t ssp = env->ssp, nssp;
    int ncpl = inner ? dpl : cpl;
    Cet2Xfer x;

    memset(&x, 0, sizeof(x));
    if (inner && cpl == 3 && cet2_ss_en(env, cpl, vm)) {
        x.pl3 = true;
        x.pl3_val = lma ? cet2_la_adjust(env, ssp) : ssp;
    }
    if (cet2_ss_en(env, ncpl, false)) {
        x.en = true;
        x.lm = lma && (new_e2 & DESC_L_MASK);
        x.user = ncpl == 3;
        x.cs = env->segs[R_CS].selector;
        x.lip = cet2_cur_lip(env, old_eip);
        x.saved = ssp;
        if (inner) {
            nssp = env->pl_ssp[dpl];
            if (lma && ist) {
                nssp = 0;
                if (cet2_ss_en(env, 0, false)) {
                    nssp = cpu_ldq_kernel(env, env->int_ssp_table + ((uint64_t)ist << 3));
                }
            }
            x.user = false;
            cet2_plan_token(env, &x, nssp, 0);
            if (!vm && ((env->segs[R_SS].flags >> DESC_DPL_SHIFT) & 3) != 3) {
                cet2_plan_frame(env, &x, nssp, 0);
            }
        } else {
            nssp = ssp;
            if (lma && ist) {
                nssp = cpu_ldq_kernel(env, env->int_ssp_table + ((uint64_t)ist << 3));
                cet2_plan_token(env, &x, nssp, 0);
            }
            cet2_plan_align(env, &x, nssp, 0);
        }
    }
    cet2_commit(env, &x, 0);
    if (cet2_ibt_en(env, ncpl, false)) {
        cet2_ibt_wait(env, ncpl);
    }
}

#endif /* __Use_Original_Qemu (U755) */
/* protected mode interrupt */
static void do_interrupt_protected(CPUX86State *env, int intno, int is_int,
                                   int error_code, unsigned int next_eip,
                                   int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr, ssp;
    int type, dpl, selector, ss_dpl, cpl;
    int has_error_code, new_stack, shift;
    uint32_t e1, e2, offset, ss = 0, esp, ss_e1 = 0, ss_e2 = 0;
    uint32_t old_eip, sp_mask;
    int vm86 = env->eflags & VM_MASK;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        has_error_code = exception_has_error_code(intno);
    }
    if (is_int) {
        old_eip = next_eip;
    } else {
        old_eip = env->eip;
    }

    dt = &env->idt;
    if (intno * 8 + 7 > dt->limit) {
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
    }
    ptr = dt->base + intno * 8;
    e1 = cpu_ldl_kernel(env, ptr);
    e2 = cpu_ldl_kernel(env, ptr + 4);
    /* check gate type */
    type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
    switch (type) {
    case 5: /* task gate */
        /* must do that check here to return the correct error code */
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0B_NOSEG, intno * 8 + 2);
        }
        switch_tss(env, intno * 8, e1, e2, SWITCH_TSS_CALL, old_eip);
        if (has_error_code) {
            int type;
            uint32_t mask;

            /* push the error code */
            type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
            shift = type >> 3;
            if (env->segs[R_SS].flags & DESC_B_MASK) {
                mask = 0xffffffff;
            } else {
                mask = 0xffff;
            }
            esp = (env->regs[R_ESP] - (2 << shift)) & mask;
            ssp = env->segs[R_SS].base + esp;
            if (shift) {
                cpu_stl_kernel(env, ssp, error_code);
            } else {
                cpu_stw_kernel(env, ssp, error_code);
            }
            SET_ESP(esp, mask);
        }
        return;
    case 6: /* 286 interrupt gate */
    case 7: /* 286 trap gate */
    case 14: /* 386 interrupt gate */
    case 15: /* 386 trap gate */
        break;
    default:
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
        break;
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    /* check privilege if software int */
    if (is_int && dpl < cpl) {
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
    }
    /* check valid bit */
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, intno * 8 + 2);
    }
    selector = e1 >> 16;
    offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
    if ((selector & 0xfffc) == 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
    if (load_segment(env, &e1, &e2, selector) != 0) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (dpl > cpl) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, selector & 0xfffc);
    }
    if (e2 & DESC_C_MASK) {
        dpl = cpl;
    }
    if (dpl < cpl) {
        /* to inner privilege */
        get_ss_esp_from_tss(env, &ss, &esp, dpl, 0);
        if ((ss & 0xfffc) == 0) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if ((ss & 3) != dpl) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if (load_segment(env, &ss_e1, &ss_e2, ss) != 0) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        ss_dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
        if (ss_dpl != dpl) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if (!(ss_e2 & DESC_S_MASK) ||
            (ss_e2 & DESC_CS_MASK) ||
            !(ss_e2 & DESC_W_MASK)) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        if (!(ss_e2 & DESC_P_MASK)) {
            raise_exception_err(env, EXCP0A_TSS, ss & 0xfffc);
        }
        new_stack = 1;
        sp_mask = get_sp_mask(ss_e2);
        ssp = get_seg_base(ss_e1, ss_e2);
    } else  {
        /* to same privilege */
        if (vm86) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        sp_mask = get_sp_mask(env->segs[R_SS].flags);
        ssp = env->segs[R_SS].base;
        esp = env->regs[R_ESP];
    }

    shift = type >> 3;

#if 0
    /* XXX: check that enough room is available */
    push_size = 6 + (new_stack << 2) + (has_error_code << 1);
    if (vm86) {
        push_size += 8;
    }
    push_size <<= shift;
#endif
    if (shift == 1) {
        if (new_stack) {
            if (vm86) {
                PUSHL(ssp, esp, sp_mask, env->segs[R_GS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_FS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_DS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_ES].selector);
            }
            PUSHL(ssp, esp, sp_mask, env->segs[R_SS].selector);
            PUSHL(ssp, esp, sp_mask, env->regs[R_ESP]);
        }
        PUSHL(ssp, esp, sp_mask, cpu_compute_eflags(env));
        PUSHL(ssp, esp, sp_mask, env->segs[R_CS].selector);
        PUSHL(ssp, esp, sp_mask, old_eip);
        if (has_error_code) {
            PUSHL(ssp, esp, sp_mask, error_code);
        }
    } else {
        if (new_stack) {
            if (vm86) {
                PUSHW(ssp, esp, sp_mask, env->segs[R_GS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_FS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_DS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_ES].selector);
            }
            PUSHW(ssp, esp, sp_mask, env->segs[R_SS].selector);
            PUSHW(ssp, esp, sp_mask, env->regs[R_ESP]);
        }
        PUSHW(ssp, esp, sp_mask, cpu_compute_eflags(env));
        PUSHW(ssp, esp, sp_mask, env->segs[R_CS].selector);
        PUSHW(ssp, esp, sp_mask, old_eip);
        if (has_error_code) {
            PUSHW(ssp, esp, sp_mask, error_code);
        }
    }

#if __Use_Original_Qemu != 1 /* ours (U755) */
    cet2_event(env, new_stack, cpl, dpl, e2, 0, old_eip);   /* CET (U755) */
#endif /* __Use_Original_Qemu (U755) */
    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);

    if (new_stack) {
        if (vm86) {
            cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0, 0);
        }
        ss = (ss & ~3) | dpl;
        cpu_x86_load_seg_cache(env, R_SS, ss,
                               ssp, get_seg_limit(ss_e1, ss_e2), ss_e2);
    }
    SET_ESP(esp, sp_mask);

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector,
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    env->eip = offset;
}

#ifdef TARGET_X86_64

#define PUSHQ_RA(sp, val, ra)                   \
    {                                           \
        sp -= 8;                                \
        cpu_stq_kernel_ra(env, sp, (val), ra);  \
    }

/* backport 0bd385e7e3 (U481): data access at the current privilege level */
#define POPQ_RA(sp, val, ra)                    \
    {                                           \
        val = cpu_ldq_data_ra(env, sp, ra);     \
        sp += 8;                                \
    }

/* backport e136648c5c (U481): push at privilege level 'pl' */
#define PUSHQ_PL(sp, val, pl, ra)                                             \
    {                                                                         \
        sp -= 8;                                                              \
        cpu_stq_mmuidx_ra(env, sp, (val), x86_mmu_index_pl(env, pl), ra);     \
    }

#define PUSHQ(sp, val) PUSHQ_RA(sp, val, 0)
#define POPQ(sp, val) POPQ_RA(sp, val, 0)

static inline target_ulong get_rsp_from_tss(CPUX86State *env, int level)
{
    X86CPU *cpu = env_archcpu(env);
    int index;

#if 0
    printf("TR: base=" TARGET_FMT_lx " limit=%x\n",
           env->tr.base, env->tr.limit);
#endif

    if (!(env->tr.flags & DESC_P_MASK)) {
        cpu_abort(CPU(cpu), "invalid tss");
    }
    index = 8 * level + 4;
    if ((index + 7) > env->tr.limit) {
        raise_exception_err(env, EXCP0A_TSS, env->tr.selector & 0xfffc);
    }
    {
        /*
         * backport 50fcc7cbb6 (U496): a non-canonical RSPn / ISTn from the 64-bit TSS is
         * #SS (SDM Vol2A CALL, 64-bit call gate: "IF pushing 32 bytes on the stack would use a
         * non-canonical address THEN #SS(NewSS)"; NewSS is the null selector with RPL = new
         * CPL, i.e. 0 for a ring-0 target). 7.2 lacks get_pg_mode(): CR4.LA57 selects the width.
         */
        target_ulong rsp = cpu_ldq_kernel(env, env->tr.base + index);
        int64_t sext = (int64_t)rsp >> ((env->cr[4] & CR4_LA57_MASK) ? 56 : 47);

        if (sext != 0 && sext != -1) {
            raise_exception_err(env, EXCP0C_STACK, 0);
        }
        return rsp;
    }
}

/* 64 bit interrupt */
static void do_interrupt64(CPUX86State *env, int intno, int is_int,
                           int error_code, target_ulong next_eip, int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr;
    int type, dpl, selector, cpl, ist;
    int has_error_code, new_stack;
    uint32_t e1, e2, e3, ss;
    target_ulong old_eip, esp, offset;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        has_error_code = exception_has_error_code(intno);
    }
    if (is_int) {
        old_eip = next_eip;
    } else {
        old_eip = env->eip;
    }

    dt = &env->idt;
    if (intno * 16 + 15 > dt->limit) {
        raise_exception_err(env, EXCP0D_GPF, intno * 16 + 2);
    }
    ptr = dt->base + intno * 16;
    e1 = cpu_ldl_kernel(env, ptr);
    e2 = cpu_ldl_kernel(env, ptr + 4);
    e3 = cpu_ldl_kernel(env, ptr + 8);
    /* check gate type */
    type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
    switch (type) {
    case 14: /* 386 interrupt gate */
    case 15: /* 386 trap gate */
        break;
    default:
        raise_exception_err(env, EXCP0D_GPF, intno * 16 + 2);
        break;
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    /* check privilege if software int */
    if (is_int && dpl < cpl) {
        raise_exception_err(env, EXCP0D_GPF, intno * 16 + 2);
    }
    /* check valid bit */
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, intno * 16 + 2);
    }
    selector = e1 >> 16;
    offset = ((target_ulong)e3 << 32) | (e2 & 0xffff0000) | (e1 & 0x0000ffff);
    ist = e2 & 7;
    if ((selector & 0xfffc) == 0) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }

    if (load_segment(env, &e1, &e2, selector) != 0) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (dpl > cpl) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err(env, EXCP0B_NOSEG, selector & 0xfffc);
    }
    if (!(e2 & DESC_L_MASK) || (e2 & DESC_B_MASK)) {
        raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
    }
    if (e2 & DESC_C_MASK) {
        dpl = cpl;
    }
    if (dpl < cpl || ist != 0) {
        /* to inner privilege */
        new_stack = 1;
        esp = get_rsp_from_tss(env, ist != 0 ? ist + 3 : dpl);
        ss = 0;
    } else {
        /* to same privilege */
        if (env->eflags & VM_MASK) {
            raise_exception_err(env, EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        esp = env->regs[R_ESP];
    }
    esp &= ~0xfLL; /* align stack */

    PUSHQ(esp, env->segs[R_SS].selector);
    PUSHQ(esp, env->regs[R_ESP]);
    PUSHQ(esp, cpu_compute_eflags(env));
    PUSHQ(esp, env->segs[R_CS].selector);
    PUSHQ(esp, old_eip);
    if (has_error_code) {
        PUSHQ(esp, error_code);
    }

#if __Use_Original_Qemu != 1 /* ours (U755) */
    cet2_event(env, dpl < cpl, cpl, dpl, e2, ist, old_eip);   /* CET (U755) */
#endif /* __Use_Original_Qemu (U755) */
    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);

    if (new_stack) {
        ss = 0 | dpl;
        cpu_x86_load_seg_cache(env, R_SS, ss, 0, 0, dpl << DESC_DPL_SHIFT);
    }
    env->regs[R_ESP] = esp;

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector,
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    env->eip = offset;
}
#endif

#if __Use_Original_Qemu != 1 /* ours (U901) */
/*
 * NoVmp (ledger U901, decision A1): the UC_X86_INS_SYSCALL / UC_X86_INS_SYSENTER hooks after an
 * SDM transition. insn_eip is the instruction's address (the hooks' range check). The state is
 * already the new one (the translator synced EIP and the flags before the helper), so it is not
 * restored to the instruction as Unicorn's hook-only mode does (cpu_restore_state would put EIP
 * back to the instruction). The hooks see the kernel-entry state and may change it; whatever
 * they leave is where execution resumes.
 */
static void sys_entry_hooks(CPUX86State *env, int insn, target_ulong insn_eip)
{
    struct hook *hook;
    uc_engine *uc = env->uc;

    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(uc, hook, UC_HOOK_INSN) {
        if (hook->to_delete) {
            continue;
        }
        if (!HOOK_BOUND_CHECK(hook, insn_eip)) {
            continue;
        }
        if (hook->insn == insn) {
            JIT_CALLBACK_GUARD(((uc_cb_insn_syscall_t)hook->callback)(uc, hook->user_data));
        }
        if (uc->stop_request) {
            break;
        }
    }
}

#endif /* __Use_Original_Qemu (U901) */
#ifdef TARGET_X86_64
#if __Use_Original_Qemu != 1 /* ours (U901) */
/*
 * NoVmp (ledger U901, decision A1): SYSCALL as the SDM defines it (Vol2B SYSCALL Operation, CR4.FRED
 * = 0: the CPU model has no FRED), the default UC_CTL_X86_SYSCALL_MODE. The #UD checks (not 64-bit
 * mode: translator, U460; IA32_EFER.SCE = 0: helper_syscall, U594) come first; then
 *   RCX := RIP (the next instruction); R11 := RFLAGS; RFLAGS := RFLAGS AND NOT(IA32_FMASK)
 *   (the reserved bits keep their fixed values: bit 1 stays 1);
 *   CS.Selector := IA32_STAR[47:32] AND FFFCH, base 0, limit FFFFFH with G = 1, type 11, S = 1,
 *   DPL 0, P = 1, L = 1, D = 0; SS.Selector := IA32_STAR[47:32] + 8 (as printed: not masked),
 *   base 0, limit FFFFFH with G = 1, type 3, S = 1, DPL 0, P = 1, B = 1; CPL := 0;
 *   CET: IF ShadowStackEnabled(old CPL) THEN IA32_PL3_SSP := LA_adjust(SSP); IF
 *   ShadowStackEnabled(0) THEN SSP := 0; IF EndbranchEnabled(0) THEN IA32_S_CET.TRACKER :=
 *   WAIT_FOR_ENDBRANCH, SUPPRESS := 0;
 *   RIP := IA32_LSTAR (WRMSR keeps it canonical).
 * R11 is the RFLAGS image as it is (Intel: "R11 := RFLAGS"; RF too - RF is cleared afterwards
 * like at the end of any instruction, by gen_eob, unless IA32_FMASK clears it first). Upstream
 * QEMU's helper_syscall clears RF in R11 (AMD's pseudocode) and lost the arithmetic flags when
 * masking (cpu_load_eflags on env->eflags alone). The single-step trap after SYSCALL uses the new
 * TF (translator: gen_eob_worker recheck_tf, upstream). Outside 64-bit mode SYSCALL only runs on a
 * non-Intel CPU model (U460); that keeps upstream QEMU's legacy / compatibility-mode transition
 * (AMD APM: IA32_CSTAR, legacy-mode IA32_STAR[31:0]). The UC_X86_INS_SYSCALL hooks run after the
 * transition (sys_entry_hooks).
 */
static void syscall_sdm(CPUX86State *env, int next_eip_addend)
{
    target_ulong insn_eip = env->eip;
    int selector = (env->star >> 32) & 0xffff;
    int cpl = env->hflags & HF_CPL_MASK;

    if (env->hflags & HF_CS64_MASK) {
        uint32_t fl = cpu_compute_eflags(env);
        bool ss_old = cet2_ss_en(env, cpl, false);

        env->regs[R_ECX] = env->eip + next_eip_addend;
        env->regs[11] = fl;
        cpu_load_eflags(env, fl & ~(uint32_t)env->fmask,
                        TF_MASK | IF_MASK | IOPL_MASK | NT_MASK | RF_MASK | VM_MASK |
                        AC_MASK | VIF_MASK | VIP_MASK | ID_MASK);
        if (ss_old) {
            env->pl_ssp[3] = cet2_la_adjust(env, env->ssp);
        }
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc, 0, 0xffffffff,
                               DESC_G_MASK | DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                               DESC_R_MASK | DESC_A_MASK | DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xffff, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        if (cet2_ss_en(env, 0, false)) {
            env->ssp = 0;
        }
        if (cet2_ibt_en(env, 0, false)) {
            cet2_ibt_wait(env, 0);
        }
        env->eip = env->lstar;
    } else if (env->hflags & HF_LMA_MASK) {
        /* upstream QEMU (non-Intel model, compatibility mode) */
        env->regs[R_ECX] = env->eip + next_eip_addend;
        env->regs[11] = cpu_compute_eflags(env) & ~RF_MASK;
        env->eflags &= ~(env->fmask | RF_MASK);
        cpu_load_eflags(env, env->eflags, 0);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc, 0, 0xffffffff,
                               DESC_G_MASK | DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                               DESC_R_MASK | DESC_A_MASK | DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eip = env->cstar;
    } else {
        /* upstream QEMU (non-Intel model, legacy mode) */
        env->regs[R_ECX] = (uint32_t)(env->eip + next_eip_addend);
        env->eflags &= ~(IF_MASK | RF_MASK | VM_MASK);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eip = (uint32_t)env->star;
    }
    sys_entry_hooks(env, UC_X86_INS_SYSCALL, insn_eip);
}

#endif /* __Use_Original_Qemu (U901) */
void helper_syscall(CPUX86State *env, int next_eip_addend)
{
    // Unicorn: call registered syscall hooks
    struct hook *hook;
    uc_engine *uc = env->uc;
    bool synced = false;

    HOOK_FOREACH_VAR_DECLARE;
#if __Use_Original_Qemu != 1 /* ours (U594) */
    /*
     * NoVmp (ledger U594): SDM Vol2B SYSCALL: "IF (CS.L != 1) or (IA32_EFER.LMA != 1) or
     * (IA32_EFER.SCE != 1) THEN #UD" (the CS.L/LMA part is U460, at translation). Unicorn's
     * SYSCALL runs the UC_X86_INS_SYSCALL hooks instead of the system-call transition and
     * ignored SCE (upstream QEMU's helper_syscall has this check). The 64-bit reset state
     * sets SCE like every 64-bit OS (unicorn.c reg_reset), so the hook API is unchanged
     * unless the guest or the user clears IA32_EFER.SCE.
     */
    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
#endif /* __Use_Original_Qemu (U594) */
#if __Use_Original_Qemu != 1 /* ours (U901) */
    /* NoVmp (ledger U901): the SDM transition unless UC_CTL_X86_SYSCALL_MODE is hook-only */
    if (uc->x86_syscall_mode != UC_X86_SYSCALL_HOOK_ONLY) {
        syscall_sdm(env, next_eip_addend);
        return;
    }
#endif /* __Use_Original_Qemu (U901) */
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN) {
        if (hook->to_delete)
            continue;
        if (!HOOK_BOUND_CHECK(hook, env->eip))
            continue;
        if (hook->insn == UC_X86_INS_SYSCALL) {
            uintptr_t pc = GETPC();
            if (!synced && !uc->skip_sync_pc_on_exit && pc) {
                cpu_restore_state(uc->cpu, pc, false);
                synced = true;
            }
            JIT_CALLBACK_GUARD(((uc_cb_insn_syscall_t)hook->callback)(env->uc, hook->user_data));
        }

        // the last callback may already asked to stop emulation
        if (env->uc->stop_request)
            break;
    }

    env->eip += next_eip_addend;
}
#endif

#ifdef TARGET_X86_64
void helper_sysret(CPUX86State *env, int dflag)
{
    int cpl, selector;

    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
    cpl = env->hflags & HF_CPL_MASK;
    if (!(env->cr[0] & CR0_PE_MASK) || cpl != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    selector = (env->star >> 48) & 0xffff;
    if (env->hflags & HF_LMA_MASK) {
        /*
         * backport 51aa3f3e05 (U483): Intel checks that RCX is canonical (SDM Vol2B SYSRET:
         * "#GP(0) If the return is to 64-bit mode and the target address is non-canonical")
         * before any state changes, so RFLAGS is loaded last. 7.2 lacks get_pg_mode():
         * CR4.LA57 selects the width directly.
         */
        if (dflag == 2) {
            uint64_t new_rip = env->regs[R_ECX];
            if (IS_INTEL_CPU(env)) {
                int shift = (env->cr[4] & CR4_LA57_MASK) ? 56 : 47;
                int64_t sext = (int64_t)new_rip >> shift;
                if (sext != 0 && sext != -1) {
                    raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
                }
            }
            cpu_x86_load_seg_cache(env, R_CS, (selector + 16) | 3,
                                   0, 0xffffffff,
                                   DESC_G_MASK | DESC_P_MASK |
                                   DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                                   DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                                   DESC_L_MASK);
            env->eip = new_rip;
        } else {
            cpu_x86_load_seg_cache(env, R_CS, selector | 3,
                                   0, 0xffffffff,
                                   DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                                   DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                                   DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
            env->eip = (uint32_t)env->regs[R_ECX];
        }
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) | 3,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);

        cpu_load_eflags(env, (uint32_t)(env->regs[11]), TF_MASK | AC_MASK
                        | ID_MASK | IF_MASK | IOPL_MASK | VM_MASK | RF_MASK |
                        NT_MASK);
#if __Use_Original_Qemu != 1 /* ours (U753) */
        /*
         * NoVmp (ledger U753): SDM Vol2B SYSRET: "CPL := 3; IF ShadowStackEnabled(CPL) SSP :=
         * IA32_PL3_SSP" (the SDM's RFLAGS image has VM = 0: R11 AND 3C7FD7H)
         */
        if (cet2_ss_en(env, 3, false)) {
            env->ssp = env->pl_ssp[3];
        }
#endif /* __Use_Original_Qemu (U753) */
    } else {
        env->eflags |= IF_MASK;
        cpu_x86_load_seg_cache(env, R_CS, selector | 3,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        env->eip = (uint32_t)env->regs[R_ECX];
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) | 3,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
    }
}
#endif

/* real mode interrupt */
static void do_interrupt_real(CPUX86State *env, int intno, int is_int,
                              int error_code, unsigned int next_eip)
{
    SegmentCache *dt;
    target_ulong ptr, ssp;
    int selector;
    uint32_t offset, esp;
    uint32_t old_cs, old_eip;

    /* real mode (simpler!) */
    dt = &env->idt;
    if (intno * 4 + 3 > dt->limit) {
        raise_exception_err(env, EXCP0D_GPF, intno * 8 + 2);
    }
    ptr = dt->base + intno * 4;
    offset = cpu_lduw_kernel(env, ptr);
    selector = cpu_lduw_kernel(env, ptr + 2);
    esp = env->regs[R_ESP];
    ssp = env->segs[R_SS].base;
    if (is_int) {
        old_eip = next_eip;
    } else {
        old_eip = env->eip;
    }
    old_cs = env->segs[R_CS].selector;
    /* XXX: use SS segment size? */
    PUSHW(ssp, esp, 0xffff, cpu_compute_eflags(env));
    PUSHW(ssp, esp, 0xffff, old_cs);
    PUSHW(ssp, esp, 0xffff, old_eip);

    /* update processor state */
    env->regs[R_ESP] = (env->regs[R_ESP] & ~0xffff) | (esp & 0xffff);
    env->eip = offset;
    env->segs[R_CS].selector = selector;
    env->segs[R_CS].base = (selector << 4);
    env->eflags &= ~(IF_MASK | TF_MASK | AC_MASK | RF_MASK);
}

static void handle_even_inj(CPUX86State *env, int intno, int is_int,
                            int error_code, int is_hw, int rm)
{
    CPUState *cs = env_cpu(env);
    uint32_t event_inj = x86_ldl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                          control.event_inj));

    if (!(event_inj & SVM_EVTINJ_VALID)) {
        int type;

        if (is_int) {
            type = SVM_EVTINJ_TYPE_SOFT;
        } else {
            type = SVM_EVTINJ_TYPE_EXEPT;
        }
        event_inj = intno | type | SVM_EVTINJ_VALID;
        if (!rm && exception_has_error_code(intno)) {
            event_inj |= SVM_EVTINJ_VALID_ERR;
            x86_stl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                             control.event_inj_err),
                     error_code);
        }
        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj);
    }
}

/*
 * Begin execution of an interruption. is_int is TRUE if coming from
 * the int instruction. next_eip is the env->eip value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE.
 */
static void do_interrupt_all(X86CPU *cpu, int intno, int is_int,
                             int error_code, target_ulong next_eip, int is_hw)
{
    CPUX86State *env = &cpu->env;

#if 0
    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        if ((env->cr[0] & CR0_PE_MASK)) {
            // static int count;

            qemu_log("%6d: v=%02x e=%04x i=%d cpl=%d IP=%04x:" TARGET_FMT_lx
                     " pc=" TARGET_FMT_lx " SP=%04x:" TARGET_FMT_lx,
                     count, intno, error_code, is_int,
                     env->hflags & HF_CPL_MASK,
                     env->segs[R_CS].selector, env->eip,
                     (int)env->segs[R_CS].base + env->eip,
                     env->segs[R_SS].selector, env->regs[R_ESP]);
            if (intno == 0x0e) {
                qemu_log(" CR2=" TARGET_FMT_lx, env->cr[2]);
            } else {
                qemu_log(" env->regs[R_EAX]=" TARGET_FMT_lx, env->regs[R_EAX]);
            }
            qemu_log("\n");
            log_cpu_state(CPU(cpu), CPU_DUMP_CCOP);
#if 0
            {
                int i;
                target_ulong ptr;

                qemu_log("       code=");
                ptr = env->segs[R_CS].base + env->eip;
                for (i = 0; i < 16; i++) {
                    qemu_log(" %02x", ldub(ptr + i));
                }
                qemu_log("\n");
            }
#endif
            count++;
        }
    }
#endif

    if (env->cr[0] & CR0_PE_MASK) {
        if (env->hflags & HF_GUEST_MASK) {
            handle_even_inj(env, intno, is_int, error_code, is_hw, 0);
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            do_interrupt64(env, intno, is_int, error_code, next_eip, is_hw);
        } else
#endif
        {
            do_interrupt_protected(env, intno, is_int, error_code, next_eip,
                                   is_hw);
        }
    } else {
        if (env->hflags & HF_GUEST_MASK) {
            handle_even_inj(env, intno, is_int, error_code, is_hw, 1);
        }
        do_interrupt_real(env, intno, is_int, error_code, next_eip);
    }

    if (env->hflags & HF_GUEST_MASK) {
        CPUState *cs = CPU(cpu);
        uint32_t event_inj = x86_ldl_phys(cs, env->vm_vmcb +
                                      offsetof(struct vmcb,
                                               control.event_inj));

        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj & ~SVM_EVTINJ_VALID);
    }
}

void x86_cpu_do_interrupt(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (cs->exception_index >= EXCP_VMEXIT) {
        assert(env->old_exception == -1);
        do_vmexit(env, cs->exception_index - EXCP_VMEXIT, env->error_code);
    } else {
        do_interrupt_all(cpu, cs->exception_index,
                         env->exception_is_int,
                         env->error_code,
                         env->exception_next_eip, 0);
        /* successfully delivered */
        env->old_exception = -1;
    }
}

void do_interrupt_x86_hardirq(CPUX86State *env, int intno, int is_hw)
{
    do_interrupt_all(env_archcpu(env), intno, 0, 0, 0, is_hw);
}

bool x86_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int intno;

    interrupt_request = x86_cpu_pending_interrupt(cs, interrupt_request);
    if (!interrupt_request) {
        return false;
    }

    /* Don't process multiple interrupt requests in a single call.
     * This is required to make icount-driven execution deterministic.
     */
    switch (interrupt_request) {
    case CPU_INTERRUPT_POLL:
        cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
        // apic_poll_irq(cpu->apic_state);
        break;
    case CPU_INTERRUPT_SIPI:
        do_cpu_sipi(cpu);
        break;
    case CPU_INTERRUPT_SMI:
        cpu_svm_check_intercept_param(env, SVM_EXIT_SMI, 0, 0);
        cs->interrupt_request &= ~CPU_INTERRUPT_SMI;
        do_smm_enter(cpu);
        break;
    case CPU_INTERRUPT_NMI:
        cpu_svm_check_intercept_param(env, SVM_EXIT_NMI, 0, 0);
        cs->interrupt_request &= ~CPU_INTERRUPT_NMI;
        env->hflags2 |= HF2_NMI_MASK;
        do_interrupt_x86_hardirq(env, EXCP02_NMI, 1);
        break;
    case CPU_INTERRUPT_MCE:
        cs->interrupt_request &= ~CPU_INTERRUPT_MCE;
        do_interrupt_x86_hardirq(env, EXCP12_MCHK, 0);
        break;
    case CPU_INTERRUPT_HARD:
        cpu_svm_check_intercept_param(env, SVM_EXIT_INTR, 0, 0);
        cs->interrupt_request &= ~(CPU_INTERRUPT_HARD |
                                   CPU_INTERRUPT_VIRQ);
        // intno = cpu_get_pic_interrupt(env);
        intno = 0;
        //qemu_log_mask(CPU_LOG_TB_IN_ASM,
        //              "Servicing hardware INT=0x%02x\n", intno);
        do_interrupt_x86_hardirq(env, intno, 1);
        break;
    case CPU_INTERRUPT_VIRQ:
        /* FIXME: this should respect TPR */
        cpu_svm_check_intercept_param(env, SVM_EXIT_VINTR, 0, 0);
        intno = x86_ldl_phys(cs, env->vm_vmcb
                             + offsetof(struct vmcb, control.int_vector));
        //qemu_log_mask(CPU_LOG_TB_IN_ASM,
        //              "Servicing virtual hardware INT=0x%02x\n", intno);
        do_interrupt_x86_hardirq(env, intno, 1);
        cs->interrupt_request &= ~CPU_INTERRUPT_VIRQ;
        break;
#if __Use_Original_Qemu != 1 /* ours (U104) */
    case CPU_INTERRUPT_UINTR:
        x86_uintr_deliver(env);
        break;
#endif /* __Use_Original_Qemu (U104) */
    }

    /* Ensure that no TB jump will be modified as the program flow was changed.  */
    return true;
}

void helper_lldt(CPUX86State *env, int selector)
{
    SegmentCache *dt;
    uint32_t e1, e2;
    int index, entry_limit;
    target_ulong ptr;

    selector &= 0xffff;
    if ((selector & 0xfffc) == 0) {
        /* XXX: NULL selector case: invalid LDT */
        env->ldt.base = 0;
        env->ldt.limit = 0;
    } else {
        if (selector & 0x4) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        dt = &env->gdt;
        index = selector & ~7;
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            entry_limit = 15;
        } else
#endif
        {
            entry_limit = 7;
        }
        if ((index + entry_limit) > dt->limit) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        ptr = dt->base + index;
        e1 = cpu_ldl_kernel_ra(env, ptr, GETPC());
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, GETPC());
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint32_t e3;

            e3 = cpu_ldl_kernel_ra(env, ptr + 8, GETPC());
            load_seg_cache_raw_dt(&env->ldt, e1, e2);
            env->ldt.base |= (target_ulong)e3 << 32;
        } else
#endif
        {
            load_seg_cache_raw_dt(&env->ldt, e1, e2);
        }
    }
    env->ldt.selector = selector;
}

void helper_ltr(CPUX86State *env, int selector)
{
    SegmentCache *dt;
    uint32_t e1, e2;
    int index, type, entry_limit;
    target_ulong ptr;

    selector &= 0xffff;
    if ((selector & 0xfffc) == 0) {
        /* NULL selector case: invalid TR */
        env->tr.base = 0;
        env->tr.limit = 0;
        env->tr.flags = 0;
    } else {
        if (selector & 0x4) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        dt = &env->gdt;
        index = selector & ~7;
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            entry_limit = 15;
        } else
#endif
        {
            entry_limit = 7;
        }
        if ((index + entry_limit) > dt->limit) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        ptr = dt->base + index;
        e1 = cpu_ldl_kernel_ra(env, ptr, GETPC());
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, GETPC());
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((e2 & DESC_S_MASK) ||
            (type != 1 && type != 9)) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
        }
#if __Use_Original_Qemu == 1 /* original QEMU (U877) */
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint32_t e3, e4;

            e3 = cpu_ldl_kernel_ra(env, ptr + 8, GETPC());
            e4 = cpu_ldl_kernel_ra(env, ptr + 12, GETPC());
            if ((e4 >> DESC_TYPE_SHIFT) & 0xf) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
            load_seg_cache_raw_dt(&env->tr, e1, e2);
            env->tr.base |= (target_ulong)e3 << 32;
        } else
#endif
        {
            load_seg_cache_raw_dt(&env->tr, e1, e2);
        }
        e2 |= DESC_TSS_BUSY_MASK;
        cpu_stl_kernel_ra(env, ptr + 4, e2, GETPC());
#else /* ours (U877) */
        /*
         * NoVmp (ledger U877): the descriptor is marked busy before TR is loaded (SDM Vol2A
         * LTR Operation: "TSSsegmentDescriptor(busy) := 1; TaskRegister := ..."): the store
         * came last, so when it faulted (a read-only GDT page: #PF with CR0.WP, or memory
         * Unicorn maps read-only) TR had already changed.
         */
        {
            uint32_t e3 = 0;

#ifdef TARGET_X86_64
            if (env->hflags & HF_LMA_MASK) {
                uint32_t e4;

                e3 = cpu_ldl_kernel_ra(env, ptr + 8, GETPC());
                e4 = cpu_ldl_kernel_ra(env, ptr + 12, GETPC());
                if ((e4 >> DESC_TYPE_SHIFT) & 0xf) {
                    raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
                }
            }
#endif
            cpu_stl_kernel_ra(env, ptr + 4, e2 | DESC_TSS_BUSY_MASK, GETPC());
            load_seg_cache_raw_dt(&env->tr, e1, e2);
#ifdef TARGET_X86_64
            if (env->hflags & HF_LMA_MASK) {
                env->tr.base |= (target_ulong)e3 << 32;
            }
#endif
        }
#endif /* __Use_Original_Qemu (U877) */
    }
    env->tr.selector = selector;
}

// Unicorn: check the arguments before run cpu_x86_load_seg().
int uc_check_cpu_x86_load_seg(CPUX86State *env, int seg_reg, int sel)
{
    int selector;
    uint32_t e2;
    int cpl, dpl, rpl;
    SegmentCache *dt;
    int index;
    target_ulong ptr;

    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        return 0;
    } else {
        selector = sel & 0xffff;
        cpl = env->hflags & HF_CPL_MASK;
        if ((selector & 0xfffc) == 0) {
            /* null selector case */
            if (seg_reg == R_SS
#ifdef TARGET_X86_64
                && (!(env->hflags & HF_CS64_MASK) || cpl == 3)
#endif
                ) {
                return UC_ERR_EXCEPTION;
            }
            return 0;
        } else {
            if (selector & 0x4) {
                dt = &env->ldt;
            } else {
                dt = &env->gdt;
            }
            index = selector & ~7;
            if ((index + 7) > dt->limit) {
                return UC_ERR_EXCEPTION;
            }
            ptr = dt->base + index;
            e2 = cpu_ldl_kernel(env, ptr + 4);

            if (!(e2 & DESC_S_MASK)) {
                return UC_ERR_EXCEPTION;
            }
            rpl = selector & 3;
            dpl = (e2 >> DESC_DPL_SHIFT) & 3;
            if (seg_reg == R_SS) {
                /* must be writable segment */
                if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
                    return UC_ERR_EXCEPTION;
                }
                if (rpl != cpl || dpl != cpl) {
                    return UC_ERR_EXCEPTION;
                }
            } else {
                /* must be readable segment */
                if ((e2 & (DESC_CS_MASK | DESC_R_MASK)) == DESC_CS_MASK) {
                    return UC_ERR_EXCEPTION;
                }

                if (!(e2 & DESC_CS_MASK) || !(e2 & DESC_C_MASK)) {
                    /* if not conforming code, test rights */
                    if (dpl < cpl || dpl < rpl) {
                        return UC_ERR_EXCEPTION;
                    }
                }
            }

            if (!(e2 & DESC_P_MASK)) {
                if (seg_reg == R_SS) {
                    return UC_ERR_EXCEPTION;
                } else {
                    return UC_ERR_EXCEPTION;
                }
            }
        }
    }

    return 0;
}

/* only works if protected mode and not VM86. seg_reg must be != R_CS */
void helper_load_seg(CPUX86State *env, int seg_reg, int selector)
{
    uint32_t e1, e2;
    int cpl, dpl, rpl;
    SegmentCache *dt;
    int index;
    target_ulong ptr;

    selector &= 0xffff;
    cpl = env->hflags & HF_CPL_MASK;
    if ((selector & 0xfffc) == 0) {
        /* null selector case */
        if (seg_reg == R_SS
#ifdef TARGET_X86_64
            && (!(env->hflags & HF_CS64_MASK) || cpl == 3)
#endif
            ) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }
        cpu_x86_load_seg_cache(env, seg_reg, selector, 0, 0, 0);
    } else {

        if (selector & 0x4) {
            dt = &env->ldt;
        } else {
            dt = &env->gdt;
        }
        index = selector & ~7;
        if ((index + 7) > dt->limit) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        ptr = dt->base + index;
        e1 = cpu_ldl_kernel_ra(env, ptr, GETPC());
        e2 = cpu_ldl_kernel_ra(env, ptr + 4, GETPC());

        if (!(e2 & DESC_S_MASK)) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        rpl = selector & 3;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (seg_reg == R_SS) {
            /* must be writable segment */
            if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
            if (rpl != cpl || dpl != cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
        } else {
            /* must be readable segment */
            if ((e2 & (DESC_CS_MASK | DESC_R_MASK)) == DESC_CS_MASK) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }

            if (!(e2 & DESC_CS_MASK) || !(e2 & DESC_C_MASK)) {
                /* if not conforming code, test rights */
                if (dpl < cpl || dpl < rpl) {
                    raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
                }
            }
        }

        if (!(e2 & DESC_P_MASK)) {
            if (seg_reg == R_SS) {
                raise_exception_err_ra(env, EXCP0C_STACK, selector & 0xfffc, GETPC());
            } else {
                raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
            }
        }

        /* set the access bit if not already set */
        if (!(e2 & DESC_A_MASK)) {
            e2 |= DESC_A_MASK;
            cpu_stl_kernel_ra(env, ptr + 4, e2, GETPC());
        }

        cpu_x86_load_seg_cache(env, seg_reg, selector,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
#if 0
        qemu_log("load_seg: sel=0x%04x base=0x%08lx limit=0x%08lx flags=%08x\n",
                selector, (unsigned long)sc->base, sc->limit, sc->flags);
#endif
    }
}

#if __Use_Original_Qemu != 1 /* ours (U805) */
/*
 * NoVmp (ledger U805): LKGS r/m16 (SDM Vol2A LKGS Operation; the translator has checked 64-bit
 * mode, CPL 0 and CPUID): MOV to GS except that the descriptor's base goes to
 * IA32_KERNEL_GS_BASE (bits 63:32 cleared) and the GS base in the descriptor cache is kept. A
 * null selector (0-3) loads GS.selector, marks GS null and clears IA32_KERNEL_GS_BASE. Otherwise
 * #GP(selector) if the index is outside the GDT/LDT limit, if the descriptor is not a data or
 * readable code segment or "SRC.RPL > descriptor.DPL" (LKGS has no conforming-code exemption
 * and no CPL test, CPL being 0); #NP(selector) if not present; the accessed bit is set in the
 * descriptor as by MOV to GS (helper_load_seg).
 */
void helper_lkgs(CPUX86State *env, uint32_t selector)
{
    uintptr_t ra = GETPC();
    uint32_t e1, e2;
    SegmentCache *dt;
    target_ulong ptr;
    int index;

    selector &= 0xffff;
    if ((selector & 0xfffc) == 0) {
        cpu_x86_load_seg_cache(env, R_GS, selector, env->segs[R_GS].base, 0, 0);
        env->kernelgsbase = 0;
        return;
    }
    dt = (selector & 4) ? &env->ldt : &env->gdt;
    index = selector & ~7;
    if ((index + 7) > dt->limit) {
        raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, ra);
    }
    ptr = dt->base + index;
    e1 = cpu_ldl_kernel_ra(env, ptr, ra);
    e2 = cpu_ldl_kernel_ra(env, ptr + 4, ra);
    if (!(e2 & DESC_S_MASK) || (e2 & (DESC_CS_MASK | DESC_R_MASK)) == DESC_CS_MASK ||
        (int)(selector & 3) > (int)((e2 >> DESC_DPL_SHIFT) & 3)) {
        raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, ra);
    }
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, ra);
    }
    if (!(e2 & DESC_A_MASK)) {
        e2 |= DESC_A_MASK;
        cpu_stl_kernel_ra(env, ptr + 4, e2, ra);
    }
    cpu_x86_load_seg_cache(env, R_GS, selector, env->segs[R_GS].base, get_seg_limit(e1, e2), e2);
    env->kernelgsbase = get_seg_base(e1, e2);
}

#endif /* __Use_Original_Qemu (U805) */
#if __Use_Original_Qemu != 1 /* ours (U707) */
/*
 * NoVmp (ledger U707): far JMP / CALL to a code segment in IA-32e mode: "IF L-Bit = 1 and
 * D-BIT = 1 and IA32_EFER.LMA = 1 THEN GP(new code segment selector)" (SDM Vol2A JMP / CALL,
 * CONFORMING- and NONCONFORMING-CODE-SEGMENT, the first check); QEMU loaded such a segment.
 */
static void far_check_l_d(CPUX86State *env, int sel, uint32_t e2, uintptr_t ra)
{
    if ((env->hflags & HF_LMA_MASK) && (e2 & DESC_L_MASK) && (e2 & DESC_B_MASK)) {
        raise_exception_err_ra(env, EXCP0D_GPF, sel & 0xfffc, ra);
    }
}

/*
 * NoVmp (ledger U707): the target of a far JMP / CALL through a call gate: a 16-bit gate's
 * offset is "tempEIP AND 0000FFFFH"; outside IA-32e mode it must be within the target code
 * segment's limit, a 64-bit gate's target must be canonical; #GP(0) either way (SDM Vol2A
 * JMP CALL-GATE / CALL "IF CallGate(InstructionPointer) not within code segment limit THEN
 * #GP(0)", "IF (CallGate(InstructionPointer) is non-canonical) THEN #GP(0)"; JMP 64-Bit Mode
 * Exceptions "#GP(0) If target offset in destination operand is non-canonical"). Returns the
 * (masked) offset. QEMU did not mask a far CALL's 286-gate offset, did not limit-check the
 * CALL gate's target and checked neither gate kind for a canonical 64-bit target.
 */
static target_ulong far_gate_target(CPUX86State *env, target_ulong offset, int gate_bits,
                                    uint32_t e1, uint32_t e2, uintptr_t ra)
{
    if (gate_bits == 16) {
        offset &= 0xffff;
    }
    if (gate_bits == 64) {
        if (!x86_ip_is_canonical(env, offset)) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, ra);
        }
    } else if (offset > get_seg_limit(e1, e2)) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, ra);
    }
    return offset;
}
#endif /* __Use_Original_Qemu (U707) */

/* protected mode jump */
void helper_ljmp_protected(CPUX86State *env, int new_cs, target_ulong new_eip,
                           target_ulong next_eip)
{
    int gate_cs, type;
    uint32_t e1, e2, cpl, dpl, rpl, limit;

    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    if (load_segment_ra(env, &e1, &e2, new_cs, GETPC()) != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
    }
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK)) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
        }
#if __Use_Original_Qemu != 1 /* ours (U707) */
        far_check_l_d(env, new_cs, e2, GETPC());
#endif /* __Use_Original_Qemu (U707) */
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_C_MASK) {
            /* conforming code segment */
            if (dpl > cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            if (dpl != cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, GETPC());
        }
        limit = get_seg_limit(e1, e2);
        if (new_eip > limit &&
            (!(env->hflags & HF_LMA_MASK) || !(e2 & DESC_L_MASK))) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }
#if __Use_Original_Qemu != 1 /* ours (U52) */
        /* NoVmp (ledger U52): 64-bit target, RIP must be canonical */
        if ((env->hflags & HF_LMA_MASK) && (e2 & DESC_L_MASK) &&
            !x86_ip_is_canonical(env, new_eip)) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }
#endif /* __Use_Original_Qemu (U52) */
        cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                       get_seg_base(e1, e2), limit, e2);
        env->eip = new_eip;
    } else {
        /* jump to call or task gate */
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        rpl = new_cs & 3;
        cpl = env->hflags & HF_CPL_MASK;
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;

#ifdef TARGET_X86_64
        if (env->efer & MSR_EFER_LMA) {
            if (type != 12) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        }
#endif
        switch (type) {
        case 1: /* 286 TSS */
        case 9: /* 386 TSS */
        case 5: /* task gate */
            if (dpl < cpl || dpl < rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            switch_tss_ra(env, new_cs, e1, e2, SWITCH_TSS_JMP, next_eip, GETPC());
            break;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            if ((dpl < cpl) || (dpl < rpl)) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            if (!(e2 & DESC_P_MASK)) {
                raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, GETPC());
            }
            gate_cs = e1 >> 16;
            new_eip = (e1 & 0xffff);
            if (type == 12) {
                new_eip |= (e2 & 0xffff0000);
            }

#ifdef TARGET_X86_64
            if (env->efer & MSR_EFER_LMA) {
                /* load the upper 8 bytes of the 64-bit call gate */
                if (load_segment_ra(env, &e1, &e2, new_cs + 8, GETPC())) {
                    raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc,
                                           GETPC());
                }
                type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
                if (type != 0) {
                    raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc,
                                           GETPC());
                }
                new_eip |= ((target_ulong)e1) << 32;
            }
#endif

            if (load_segment_ra(env, &e1, &e2, gate_cs, GETPC()) != 0) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
            dpl = (e2 >> DESC_DPL_SHIFT) & 3;
            /* must be code segment */
            if (((e2 & (DESC_S_MASK | DESC_CS_MASK)) !=
                 (DESC_S_MASK | DESC_CS_MASK))) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
            if (((e2 & DESC_C_MASK) && (dpl > cpl)) ||
                (!(e2 & DESC_C_MASK) && (dpl != cpl))) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
#ifdef TARGET_X86_64
            if (env->efer & MSR_EFER_LMA) {
                if (!(e2 & DESC_L_MASK)) {
                    raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
                }
                if (e2 & DESC_B_MASK) {
                    raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
                }
            }
#endif
            if (!(e2 & DESC_P_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, gate_cs & 0xfffc, GETPC());
            }
            limit = get_seg_limit(e1, e2);
#if __Use_Original_Qemu == 1 /* original QEMU (U707) */
            if (new_eip > limit &&
                (!(env->hflags & HF_LMA_MASK) || !(e2 & DESC_L_MASK))) {
                raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
            }
#else /* ours (U707) */
            new_eip = far_gate_target(env, new_eip,
                                      (env->efer & MSR_EFER_LMA) ? 64 : type == 12 ? 32 : 16,
                                      e1, e2, GETPC());
#endif /* __Use_Original_Qemu (U707) */
            cpu_x86_load_seg_cache(env, R_CS, (gate_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            env->eip = new_eip;
            break;
        default:
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            break;
        }
    }
}

/* real mode call */
/*
 * backport 8c03ab9f74 (U497): the target offset is 32 bits and zero-extended into EIP (it was
 * an int, so an offset >= 80000000h sign-extended into the 64-bit eip)
 */
void helper_lcall_real(CPUX86State *env, uint32_t new_cs, uint32_t new_eip,
                       int shift, uint32_t next_eip)
{
    uint32_t esp, esp_mask;
    target_ulong ssp;

    esp = env->regs[R_ESP];
    esp_mask = get_sp_mask(env->segs[R_SS].flags);
    ssp = env->segs[R_SS].base;
#if __Use_Original_Qemu != 1 /* ours (U708) */
    /* real / virtual-8086 mode: the 2 slots at the current privilege level (U708) */
    far_probe_pushes(env, ssp, esp, esp_mask, shift ? 4 : 2, 2, env->hflags & HF_CPL_MASK,
                     GETPC());
#endif /* __Use_Original_Qemu (U708) */
    if (shift) {
        PUSHL_RA(ssp, esp, esp_mask, env->segs[R_CS].selector, GETPC());
        PUSHL_RA(ssp, esp, esp_mask, next_eip, GETPC());
    } else {
        PUSHW_RA(ssp, esp, esp_mask, env->segs[R_CS].selector, GETPC());
        PUSHW_RA(ssp, esp, esp_mask, next_eip, GETPC());
    }

    SET_ESP(esp, esp_mask);
    env->eip = new_eip;
    env->segs[R_CS].selector = new_cs;
    env->segs[R_CS].base = (new_cs << 4);
}

/* protected mode call */
void helper_lcall_protected(CPUX86State *env, int new_cs, target_ulong new_eip,
                            int shift, target_ulong next_eip)
{
    int new_stack, i;
    uint32_t e1, e2, cpl, dpl, rpl, selector, param_count;
#if __Use_Original_Qemu == 1 /* original QEMU (U591) */
    uint32_t ss = 0, ss_e1 = 0, ss_e2 = 0, type, ss_dpl, sp_mask;
#else /* ours (U591) */
    /* sp_mask all ones (64 bits) = the 64-bit stack pointer of 64-bit mode (SEG_ADDL) */
    uint32_t ss = 0, ss_e1 = 0, ss_e2 = 0, type, ss_dpl;
    target_ulong sp_mask;
#endif /* __Use_Original_Qemu (U591) */
    uint32_t val, limit, old_sp_mask;
    target_ulong ssp, old_ssp, offset, sp;
#if __Use_Original_Qemu != 1 /* ours (U707) */
    int gate_bits = 32;     /* 16, 32 or 64: the call gate's size */
#endif /* __Use_Original_Qemu (U707) */
#if __Use_Original_Qemu != 1 /* ours (U750) */
    Cet2Xfer cet;

    memset(&cet, 0, sizeof(cet));
#endif /* __Use_Original_Qemu (U750) */

    LOG_PCALL("lcall %04x:" TARGET_FMT_lx " s=%d\n", new_cs, new_eip, shift);
    LOG_PCALL_STATE(env_cpu(env));
    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    if (load_segment_ra(env, &e1, &e2, new_cs, GETPC()) != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
    }
    cpl = env->hflags & HF_CPL_MASK;
    LOG_PCALL("desc=%08x:%08x\n", e1, e2);
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK)) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
        }
#if __Use_Original_Qemu != 1 /* ours (U707) */
        far_check_l_d(env, new_cs, e2, GETPC());
#endif /* __Use_Original_Qemu (U707) */
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_C_MASK) {
            /* conforming code segment */
            if (dpl > cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            if (dpl != cpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, GETPC());
        }

#ifdef TARGET_X86_64
        /* XXX: check 16/32 bit cases in long mode */
        if (shift == 2) {
            target_ulong rsp;

#if __Use_Original_Qemu != 1 /* ours (U52) */
            /* NoVmp (ledger U52): 64-bit target RIP canonical, before the pushes */
            if ((e2 & DESC_L_MASK) && !x86_ip_is_canonical(env, new_eip)) {
                raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
            }
#endif /* __Use_Original_Qemu (U52) */
            /* 64 bit case */
            rsp = env->regs[R_ESP];
#if __Use_Original_Qemu != 1 /* ours (U834) */
            /* CALL far at CPL3 to the same level: the stack pushes are checked (#AC) (U834) */
            x86_ac_check(env, rsp - 8, 7, GETPC());
#endif /* __Use_Original_Qemu (U834) */
#if __Use_Original_Qemu != 1 /* ours (U708) */
            far_probe_pushes(env, 0, rsp, ~(target_ulong)0, 8, 2, cpl, GETPC());
#endif /* __Use_Original_Qemu (U708) */
#if __Use_Original_Qemu != 1 /* ours (U750) */
            /* shadow-stack frame (CS, LIP, SSP): checked and probed before any push (U750) */
            cet2_plan_same(env, &cet, e2, cet2_call_lip(env, e2, shift, next_eip), true, GETPC());
#endif /* __Use_Original_Qemu (U750) */
            /* backport e136648c5c (U481): at the current CPL */
            PUSHQ_PL(rsp, env->segs[R_CS].selector, cpl, GETPC());
            PUSHQ_PL(rsp, next_eip, cpl, GETPC());
            /* from this point, not restartable */
            env->regs[R_ESP] = rsp;
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2),
                                   get_seg_limit(e1, e2), e2);
            env->eip = new_eip;
#if __Use_Original_Qemu != 1 /* ours (U750) */
            cet2_commit(env, &cet, GETPC());   /* shadow stack, SSP (U750) */
#endif /* __Use_Original_Qemu (U750) */
        } else
#endif
        {
            sp = env->regs[R_ESP];
#if __Use_Original_Qemu == 1 /* original QEMU (U591) */
            sp_mask = get_sp_mask(env->segs[R_SS].flags);
            ssp = env->segs[R_SS].base;
#else /* ours (U591) */
            /* CALL FAR m16:32 / m16:16 in 64-bit mode pushes through RSP (SS.base 0) */
            sp_mask = x86_stack_mask64(env, env->segs[R_SS].flags);
            ssp = (env->hflags & HF_CS64_MASK) ? 0 : env->segs[R_SS].base;
#endif /* __Use_Original_Qemu (U591) */
#if __Use_Original_Qemu != 1 /* ours (U591/U707) */
            /*
             * SDM Vol2A CALL (CONFORMING/NONCONFORMING-CODE-SEGMENT): the limit is checked
             * only "IF (IA32_EFER.LMA = 0 or target mode = Compatibility mode)": a 64-bit
             * target (Windows' 33h has limit 0) is not limit-checked, as for far JMP above
             * (U591). U707: "... THEN #GP(0)", before anything is pushed (QEMU pushed the
             * return address first and raised #GP(new code segment selector)).
             */
            limit = get_seg_limit(e1, e2);
            if (new_eip > limit &&
                (!(env->hflags & HF_LMA_MASK) || !(e2 & DESC_L_MASK))) {
                raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
            }
#endif /* __Use_Original_Qemu (U591/U707) */
#if __Use_Original_Qemu != 1 /* ours (U834) */
            x86_ac_check(env, ssp + ((sp - (shift ? 4 : 2)) & sp_mask), shift ? 3 : 1, GETPC());
#endif /* __Use_Original_Qemu (U834) */
#if __Use_Original_Qemu != 1 /* ours (U708) */
            far_probe_pushes(env, ssp, sp, sp_mask, shift ? 4 : 2, 2, cpl, GETPC());
#endif /* __Use_Original_Qemu (U708) */
#if __Use_Original_Qemu != 1 /* ours (U750) */
            cet2_plan_same(env, &cet, e2, cet2_call_lip(env, e2, shift, next_eip), true, GETPC());
#endif /* __Use_Original_Qemu (U750) */
            /* backport e136648c5c (U481): at the current CPL */
            if (shift) {
                PUSHL_PL(ssp, sp, sp_mask, env->segs[R_CS].selector, cpl, GETPC());
                PUSHL_PL(ssp, sp, sp_mask, next_eip, cpl, GETPC());
            } else {
                PUSHW_PL(ssp, sp, sp_mask, env->segs[R_CS].selector, cpl, GETPC());
                PUSHW_PL(ssp, sp, sp_mask, next_eip, cpl, GETPC());
            }

            limit = get_seg_limit(e1, e2);
#if __Use_Original_Qemu == 1 /* original QEMU (U591/U707) */
            if (new_eip > limit) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
#endif /* __Use_Original_Qemu (U591/U707) */
            /* from this point, not restartable */
            SET_ESP(sp, sp_mask);
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            env->eip = new_eip;
#if __Use_Original_Qemu != 1 /* ours (U750) */
            cet2_commit(env, &cet, GETPC());   /* shadow stack, SSP (U750) */
#endif /* __Use_Original_Qemu (U750) */
        }
    } else {
        /* check gate type */
        type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        rpl = new_cs & 3;

#ifdef TARGET_X86_64
        if (env->efer & MSR_EFER_LMA) {
            if (type != 12) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
        }
#endif

        switch (type) {
        case 1: /* available 286 TSS */
        case 9: /* available 386 TSS */
        case 5: /* task gate */
            if (dpl < cpl || dpl < rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            }
            switch_tss_ra(env, new_cs, e1, e2, SWITCH_TSS_CALL, next_eip, GETPC());
            return;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            break;
        default:
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
            break;
        }
        shift = type >> 3;
#if __Use_Original_Qemu != 1 /* ours (U707) */
        gate_bits = (env->efer & MSR_EFER_LMA) ? 64 : shift ? 32 : 16;
#endif /* __Use_Original_Qemu (U707) */

        if (dpl < cpl || dpl < rpl) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, GETPC());
        }
        /* check valid bit */
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG,  new_cs & 0xfffc, GETPC());
        }
        selector = e1 >> 16;
        param_count = e2 & 0x1f;
        offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
#ifdef TARGET_X86_64
        if (env->efer & MSR_EFER_LMA) {
            /* load the upper 8 bytes of the 64-bit call gate */
            if (load_segment_ra(env, &e1, &e2, new_cs + 8, GETPC())) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc,
                                       GETPC());
            }
            type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
            if (type != 0) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc,
                                       GETPC());
            }
            offset |= ((target_ulong)e1) << 32;
        }
#endif
        if ((selector & 0xfffc) == 0) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }

        if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (dpl > cpl) {
            raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
        }
#ifdef TARGET_X86_64
        if (env->efer & MSR_EFER_LMA) {
            if (!(e2 & DESC_L_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
            if (e2 & DESC_B_MASK) {
                raise_exception_err_ra(env, EXCP0D_GPF, selector & 0xfffc, GETPC());
            }
            shift++;
        }
#endif
        if (!(e2 & DESC_P_MASK)) {
            raise_exception_err_ra(env, EXCP0B_NOSEG, selector & 0xfffc, GETPC());
        }

        if (!(e2 & DESC_C_MASK) && dpl < cpl) {
            /* to inner privilege */
#ifdef TARGET_X86_64
            if (shift == 2) {
                sp = get_rsp_from_tss(env, dpl);
                ss = dpl;  /* SS = NULL selector with RPL = new CPL */
                new_stack = 1;
                sp_mask = 0;
                ssp = 0;  /* SS base is always zero in IA-32e mode */
                LOG_PCALL("new ss:rsp=%04x:%016llx env->regs[R_ESP]="
                          TARGET_FMT_lx "\n", ss, sp, env->regs[R_ESP]);
            } else
#endif
            {
                uint32_t sp32;
                get_ss_esp_from_tss(env, &ss, &sp32, dpl, GETPC());
                LOG_PCALL("new ss:esp=%04x:%08x param_count=%d env->regs[R_ESP]="
                          TARGET_FMT_lx "\n", ss, sp32, param_count,
                          env->regs[R_ESP]);
                sp = sp32;
                if ((ss & 0xfffc) == 0) {
                    raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
                }
                if ((ss & 3) != dpl) {
                    raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
                }
                if (load_segment_ra(env, &ss_e1, &ss_e2, ss, GETPC()) != 0) {
                    raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
                }
                ss_dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
                if (ss_dpl != dpl) {
                    raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
                }
                if (!(ss_e2 & DESC_S_MASK) ||
                    (ss_e2 & DESC_CS_MASK) ||
                    !(ss_e2 & DESC_W_MASK)) {
                    raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
                }
                if (!(ss_e2 & DESC_P_MASK)) {
                    raise_exception_err_ra(env, EXCP0A_TSS, ss & 0xfffc, GETPC());
                }

                sp_mask = get_sp_mask(ss_e2);
                ssp = get_seg_base(ss_e1, ss_e2);
            }
#if __Use_Original_Qemu != 1 /* ours (U707) */
            /* after the new stack's checks, before anything is pushed (SDM CALL) */
            offset = far_gate_target(env, offset, gate_bits, e1, e2, GETPC());
#endif /* __Use_Original_Qemu (U707) */

            /* push_size = ((param_count * 2) + 8) << shift; */

            old_sp_mask = get_sp_mask(env->segs[R_SS].flags);
            old_ssp = env->segs[R_SS].base;
#if __Use_Original_Qemu == 1 /* original QEMU (U708) */
#ifdef TARGET_X86_64
            if (shift == 2) {
                /* XXX: verify if new stack address is canonical */
                /* backport e136648c5c (U481): new stack at the new CPL (dpl) */
                PUSHQ_PL(sp, env->segs[R_SS].selector, dpl, GETPC());
                PUSHQ_PL(sp, env->regs[R_ESP], dpl, GETPC());
                /* parameters aren't supported for 64-bit call gates */
            } else
#endif
            if (shift == 1) {
                /* backport e136648c5c (U481): new stack at dpl, parameters at CPL */
                PUSHL_PL(ssp, sp, sp_mask, env->segs[R_SS].selector, dpl, GETPC());
                PUSHL_PL(ssp, sp, sp_mask, env->regs[R_ESP], dpl, GETPC());
                for (i = param_count - 1; i >= 0; i--) {
                    val = cpu_ldl_data_ra(env, old_ssp +
                                          ((env->regs[R_ESP] + i * 4) &
                                           old_sp_mask), GETPC());
                    PUSHL_PL(ssp, sp, sp_mask, val, dpl, GETPC());
                }
            } else {
                /* backport e136648c5c (U481): new stack at dpl, parameters at CPL */
                PUSHW_PL(ssp, sp, sp_mask, env->segs[R_SS].selector, dpl, GETPC());
                PUSHW_PL(ssp, sp, sp_mask, env->regs[R_ESP], dpl, GETPC());
                for (i = param_count - 1; i >= 0; i--) {
                    val = cpu_lduw_data_ra(env, old_ssp +
                                           ((env->regs[R_ESP] + i * 2) &
                                            old_sp_mask), GETPC());
                    PUSHW_PL(ssp, sp, sp_mask, val, dpl, GETPC());
                }
            }
#else /* ours (U708) */
            {
                /*
                 * U708: the parameters are read from the old stack (at CPL) first, then the
                 * new stack's slots - SS, (E)SP, the parameters, CS, (E)IP - are checked
                 * (far_probe_pushes), then everything is pushed: a fault leaves both stacks
                 * as they were.
                 */
                uint32_t params[32];
                int psize = shift == 2 ? 8 : shift == 1 ? 4 : 2;
                int nparam = shift == 2 ? 0 : (int)param_count;

                for (i = nparam - 1; i >= 0; i--) {
                    params[i] = shift == 1
                        ? cpu_ldl_data_ra(env, old_ssp + ((env->regs[R_ESP] + i * 4) &
                                                          old_sp_mask), GETPC())
                        : cpu_lduw_data_ra(env, old_ssp + ((env->regs[R_ESP] + i * 2) &
                                                           old_sp_mask), GETPC());
                }
                far_probe_pushes(env, ssp, sp, sp_mask, psize, 4 + nparam, dpl, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U750) */
                /* shadow stack of the new CPL: token, frame (U750), before any push */
                cet2_plan_inner(env, &cet, dpl, e2, cet2_cur_lip(env, next_eip), GETPC());
#endif /* __Use_Original_Qemu (U750) */
#ifdef TARGET_X86_64
                if (shift == 2) {
                    /* backport e136648c5c (U481): new stack at the new CPL (dpl) */
                    PUSHQ_PL(sp, env->segs[R_SS].selector, dpl, GETPC());
                    PUSHQ_PL(sp, env->regs[R_ESP], dpl, GETPC());
                    /* parameters aren't supported for 64-bit call gates */
                } else
#endif
                if (shift == 1) {
                    PUSHL_PL(ssp, sp, sp_mask, env->segs[R_SS].selector, dpl, GETPC());
                    PUSHL_PL(ssp, sp, sp_mask, env->regs[R_ESP], dpl, GETPC());
                    for (i = nparam - 1; i >= 0; i--) {
                        PUSHL_PL(ssp, sp, sp_mask, params[i], dpl, GETPC());
                    }
                } else {
                    PUSHW_PL(ssp, sp, sp_mask, env->segs[R_SS].selector, dpl, GETPC());
                    PUSHW_PL(ssp, sp, sp_mask, env->regs[R_ESP], dpl, GETPC());
                    for (i = nparam - 1; i >= 0; i--) {
                        PUSHW_PL(ssp, sp, sp_mask, params[i], dpl, GETPC());
                    }
                }
            }
#endif /* __Use_Original_Qemu (U708) */
            new_stack = 1;
        } else {
            /* to same privilege */
            sp = env->regs[R_ESP];
#if __Use_Original_Qemu == 1 /* original QEMU (U591) */
            sp_mask = get_sp_mask(env->segs[R_SS].flags);
#else /* ours (U591) */
            /*
             * a 64-bit call gate (IA-32e mode, shift 2) pushes 8-byte slots through RSP; SS.B
             * must not truncate RSP afterwards (SET_ESP)
             */
            sp_mask = shift == 2 ? ~(target_ulong)0 : get_sp_mask(env->segs[R_SS].flags);
#endif /* __Use_Original_Qemu (U591) */
            ssp = env->segs[R_SS].base;
            /* push_size = (4 << shift); */
            new_stack = 0;
#if __Use_Original_Qemu != 1 /* ours (U707) */
            offset = far_gate_target(env, offset, gate_bits, e1, e2, GETPC());
#endif /* __Use_Original_Qemu (U707) */
#if __Use_Original_Qemu != 1 /* ours (U708) */
            far_probe_pushes(env, ssp, sp, sp_mask, shift == 2 ? 8 : shift ? 4 : 2, 2, cpl,
                             GETPC());
#endif /* __Use_Original_Qemu (U708) */
#if __Use_Original_Qemu != 1 /* ours (U750) */
            cet2_plan_same(env, &cet, e2, cet2_cur_lip(env, next_eip), false, GETPC());
#endif /* __Use_Original_Qemu (U750) */
        }

        /* backport e136648c5c (U481): new stack at dpl, else the current one at CPL */
#ifdef TARGET_X86_64
        if (shift == 2) {
            PUSHQ_PL(sp, env->segs[R_CS].selector, new_stack ? dpl : cpl, GETPC());
            PUSHQ_PL(sp, next_eip, new_stack ? dpl : cpl, GETPC());
        } else
#endif
        if (shift == 1) {
            PUSHL_PL(ssp, sp, sp_mask, env->segs[R_CS].selector, new_stack ? dpl : cpl,
                     GETPC());
            PUSHL_PL(ssp, sp, sp_mask, next_eip, new_stack ? dpl : cpl, GETPC());
        } else {
            PUSHW_PL(ssp, sp, sp_mask, env->segs[R_CS].selector, new_stack ? dpl : cpl,
                     GETPC());
            PUSHW_PL(ssp, sp, sp_mask, next_eip, new_stack ? dpl : cpl, GETPC());
        }

        /* from this point, not restartable */

        if (new_stack) {
#ifdef TARGET_X86_64
            if (shift == 2) {
                cpu_x86_load_seg_cache(env, R_SS, ss, 0, 0, 0);
            } else
#endif
            {
                ss = (ss & ~3) | dpl;
                cpu_x86_load_seg_cache(env, R_SS, ss,
                                       ssp,
                                       get_seg_limit(ss_e1, ss_e2),
                                       ss_e2);
            }
        }

        selector = (selector & ~3) | dpl;
        cpu_x86_load_seg_cache(env, R_CS, selector,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
        SET_ESP(sp, sp_mask);
        env->eip = offset;
#if __Use_Original_Qemu != 1 /* ours (U750) */
        /* call gate (U750): token busy, 4 zero bytes, frame, SSP / IA32_PL3_SSP */
        cet2_commit(env, &cet, GETPC());
#endif /* __Use_Original_Qemu (U750) */
    }
}

/* real and vm86 mode iret */
void helper_iret_real(CPUX86State *env, int shift)
{
    uint32_t sp, new_cs, new_eip, new_eflags, sp_mask;
    target_ulong ssp;
    int eflags_mask;

    sp_mask = 0xffff; /* XXXX: use SS segment size? */
    sp = env->regs[R_ESP];
    ssp = env->segs[R_SS].base;
    if (shift == 1) {
        /* 32 bits */
        POPL_RA(ssp, sp, sp_mask, new_eip, GETPC());
        POPL_RA(ssp, sp, sp_mask, new_cs, GETPC());
        new_cs &= 0xffff;
        POPL_RA(ssp, sp, sp_mask, new_eflags, GETPC());
    } else {
        /* 16 bits */
        POPW_RA(ssp, sp, sp_mask, new_eip, GETPC());
        POPW_RA(ssp, sp, sp_mask, new_cs, GETPC());
        POPW_RA(ssp, sp, sp_mask, new_eflags, GETPC());
    }
    env->regs[R_ESP] = (env->regs[R_ESP] & ~sp_mask) | (sp & sp_mask);
    env->segs[R_CS].selector = new_cs;
    env->segs[R_CS].base = (new_cs << 4);
    env->eip = new_eip;
    if (env->eflags & VM_MASK) {
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | IF_MASK | RF_MASK |
            NT_MASK;
    } else {
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | IF_MASK | IOPL_MASK |
            RF_MASK | NT_MASK;
    }
    if (shift == 0) {
        eflags_mask &= 0xffff;
    }
    cpu_load_eflags(env, new_eflags, eflags_mask);
    env->hflags2 &= ~HF2_NMI_MASK;
}

static inline void validate_seg(CPUX86State *env, int seg_reg, int cpl)
{
    int dpl;
    uint32_t e2;

    /* XXX: on x86_64, we do not want to nullify FS and GS because
       they may still contain a valid base. I would be interested to
       know how a real x86_64 CPU behaves */
    if ((seg_reg == R_FS || seg_reg == R_GS) &&
        (env->segs[seg_reg].selector & 0xfffc) == 0) {
        return;
    }

    e2 = env->segs[seg_reg].flags;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (!(e2 & DESC_CS_MASK) || !(e2 & DESC_C_MASK)) {
        /* data or non conforming code segment */
        if (dpl < cpl) {
            /*
             * backport c2ba0515f2 (U477): only the selector becomes null; the
             * descriptor cache keeps base and limit, P is cleared (SDM Vol2A
             * IRET / RET far: "the segment register is loaded with a NULL
             * segment selector")
             */
            cpu_x86_load_seg_cache(env, seg_reg, 0,
                                   env->segs[seg_reg].base,
                                   env->segs[seg_reg].limit,
                                   env->segs[seg_reg].flags & ~DESC_P_MASK);
        }
    }
}

/* protected mode iret */
#if __Use_Original_Qemu != 1 /* ours (U707) */
/*
 * NoVmp (ledger U707): RET far / IRET: "IF the return instruction pointer is not within the
 * return code segment limit THEN #GP(0)" (SDM Vol2B RET, Vol2A IRET), checked for a return
 * to legacy / compatibility-mode code (a 64-bit code segment has no limit check; its RIP must
 * be canonical, U52), before the segment registers and RSP change. QEMU did not check.
 */
static void ret_check_eip_limit(CPUX86State *env, target_ulong new_eip, uint32_t e1,
                                uint32_t e2, uintptr_t retaddr)
{
    if ((!(env->hflags & HF_LMA_MASK) || !(e2 & DESC_L_MASK)) &&
        new_eip > get_seg_limit(e1, e2)) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
    }
}
#endif /* __Use_Original_Qemu (U707) */

static inline void helper_ret_protected(CPUX86State *env, int shift,
                                        int is_iret, int addend,
                                        uintptr_t retaddr)
{
    uint32_t new_cs, new_eflags, new_ss;
    uint32_t new_es, new_ds, new_fs, new_gs;
    uint32_t e1, e2, ss_e1, ss_e2;
    int cpl, dpl, rpl, eflags_mask, iopl;
    target_ulong ssp, sp, new_eip, new_esp, sp_mask;

#ifdef TARGET_X86_64
    if (shift == 2) {
        sp_mask = -1;
    } else
#endif
    {
#if __Use_Original_Qemu == 1 /* original QEMU (U591) */
        sp_mask = get_sp_mask(env->segs[R_SS].flags);
#else /* ours (U591) */
        /* RETF/IRET with a 16/32-bit operand in 64-bit mode still pop through RSP */
        sp_mask = x86_stack_mask64(env, env->segs[R_SS].flags);
#endif /* __Use_Original_Qemu (U591) */
    }
    sp = env->regs[R_ESP];
    ssp = env->segs[R_SS].base;
#if __Use_Original_Qemu != 1 /* ours (U591) */
    if (env->hflags & HF_CS64_MASK) {
        ssp = 0;    /* SS.base is not used in 64-bit mode */
    }
#endif /* __Use_Original_Qemu (U591) */
#if __Use_Original_Qemu != 1 /* ours (U834) */
    /*
     * NoVmp (ledger U834): RET far / IRET at CPL3 pop their frame from the CPL3 stack: those
     * pops are checked (#AC, operand size), before the first one.
     */
    x86_ac_check(env, shift == 2 ? sp : ssp + (sp & sp_mask), (2u << shift) - 1, retaddr);
#endif /* __Use_Original_Qemu (U834) */
    new_eflags = 0; /* avoid warning */
#ifdef TARGET_X86_64
    if (shift == 2) {
        POPQ_RA(sp, new_eip, retaddr);
        POPQ_RA(sp, new_cs, retaddr);
        new_cs &= 0xffff;
        if (is_iret) {
            POPQ_RA(sp, new_eflags, retaddr);
        }
    } else
#endif
    {
        if (shift == 1) {
            /* 32 bits */
            POPL_RA(ssp, sp, sp_mask, new_eip, retaddr);
            POPL_RA(ssp, sp, sp_mask, new_cs, retaddr);
            new_cs &= 0xffff;
            if (is_iret) {
                POPL_RA(ssp, sp, sp_mask, new_eflags, retaddr);
                /*
                 * backport of QEMU 36f634fe4a: virtual-8086 mode only from CPL 0
                 * outside long mode (SDM Vol2 IRET, PROTECTED-MODE)
                 */
                bool allow_vm86 = ((env->hflags & HF_CPL_MASK) == 0) &&
                                  !(env->hflags & HF_LMA_MASK);
                if ((new_eflags & VM_MASK) && allow_vm86) {
                    goto return_to_vm86;
                }
            }
        } else {
            /* 16 bits */
            POPW_RA(ssp, sp, sp_mask, new_eip, retaddr);
            POPW_RA(ssp, sp, sp_mask, new_cs, retaddr);
            if (is_iret) {
                POPW_RA(ssp, sp, sp_mask, new_eflags, retaddr);
            }
        }
    }
    LOG_PCALL("lret new %04x:" TARGET_FMT_lx " s=%d addend=0x%x\n",
              new_cs, new_eip, shift, addend);
    LOG_PCALL_STATE(env_cpu(env));
    if ((new_cs & 0xfffc) == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
    }
    if (load_segment_ra(env, &e1, &e2, new_cs, retaddr) != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
    }
    if (!(e2 & DESC_S_MASK) ||
        !(e2 & DESC_CS_MASK)) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
    }
    cpl = env->hflags & HF_CPL_MASK;
    rpl = new_cs & 3;
    if (rpl < cpl) {
        raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (e2 & DESC_C_MASK) {
        if (dpl > rpl) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
        }
    } else {
        if (dpl != rpl) {
            raise_exception_err_ra(env, EXCP0D_GPF, new_cs & 0xfffc, retaddr);
        }
    }
#if __Use_Original_Qemu == 1 /* original QEMU (U52) */
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, retaddr);
    }
#else /* ours (U52) */
    if (!(e2 & DESC_P_MASK)) {
        raise_exception_err_ra(env, EXCP0B_NOSEG, new_cs & 0xfffc, retaddr);
    }
    /* NoVmp (ledger U52): returning to 64-bit code, RIP must be canonical
       (SDM RET far / IRET: #GP(0)); nothing committed yet */
    if ((env->hflags & HF_LMA_MASK) && (e2 & DESC_L_MASK) &&
        !x86_ip_is_canonical(env, new_eip)) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
    }
#endif /* __Use_Original_Qemu (U52) */

    sp += addend;
    if (rpl == cpl && (!(env->hflags & HF_CS64_MASK) ||
                       ((env->hflags & HF_CS64_MASK) && !is_iret))) {
        /* return to same privilege level */
#if __Use_Original_Qemu != 1 /* ours (U707) */
        ret_check_eip_limit(env, new_eip, e1, e2, retaddr);
#endif /* __Use_Original_Qemu (U707) */
#if __Use_Original_Qemu != 1 /* ours (U751) */
        /* NoVmp (ledger U751/U752): shadow-stack frame check, last before the commit */
        cet2_ret(env, is_iret, rpl, new_cs, e1, e2, new_eip, retaddr);
#endif /* __Use_Original_Qemu (U751) */
        cpu_x86_load_seg_cache(env, R_CS, new_cs,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
    } else {
        /* return to different privilege level */
#ifdef TARGET_X86_64
        if (shift == 2) {
            POPQ_RA(sp, new_esp, retaddr);
            POPQ_RA(sp, new_ss, retaddr);
            new_ss &= 0xffff;
        } else
#endif
        {
            if (shift == 1) {
                /* 32 bits */
                POPL_RA(ssp, sp, sp_mask, new_esp, retaddr);
                POPL_RA(ssp, sp, sp_mask, new_ss, retaddr);
                new_ss &= 0xffff;
            } else {
                /* 16 bits */
                POPW_RA(ssp, sp, sp_mask, new_esp, retaddr);
                POPW_RA(ssp, sp, sp_mask, new_ss, retaddr);
            }
        }
        LOG_PCALL("new ss:esp=%04x:" TARGET_FMT_lx "\n",
                  new_ss, new_esp);
        if ((new_ss & 0xfffc) == 0) {
#ifdef TARGET_X86_64
            /* NULL ss is allowed in long mode if cpl != 3 */
            /* XXX: test CS64? */
            if ((env->hflags & HF_LMA_MASK) && rpl != 3) {
#if __Use_Original_Qemu != 1 /* ours (U707) */
                ret_check_eip_limit(env, new_eip, e1, e2, retaddr);
#endif /* __Use_Original_Qemu (U707) */
#if __Use_Original_Qemu != 1 /* ours (U751) */
                /* shadow stack (U751/U752; IA-32e IRET to the same CPL too) */
                cet2_ret(env, is_iret, rpl, new_cs, e1, e2, new_eip, retaddr);
#endif /* __Use_Original_Qemu (U751) */
                cpu_x86_load_seg_cache(env, R_SS, new_ss,
                                       0, 0xffffffff,
                                       DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                                       DESC_S_MASK | (rpl << DESC_DPL_SHIFT) |
                                       DESC_W_MASK | DESC_A_MASK);
                ss_e2 = DESC_B_MASK; /* XXX: should not be needed? */
            } else
#endif
            {
                raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
            }
        } else {
            if ((new_ss & 3) != rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_ss & 0xfffc, retaddr);
            }
            if (load_segment_ra(env, &ss_e1, &ss_e2, new_ss, retaddr) != 0) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_ss & 0xfffc, retaddr);
            }
            if (!(ss_e2 & DESC_S_MASK) ||
                (ss_e2 & DESC_CS_MASK) ||
                !(ss_e2 & DESC_W_MASK)) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_ss & 0xfffc, retaddr);
            }
            dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
            if (dpl != rpl) {
                raise_exception_err_ra(env, EXCP0D_GPF, new_ss & 0xfffc, retaddr);
            }
            if (!(ss_e2 & DESC_P_MASK)) {
                raise_exception_err_ra(env, EXCP0B_NOSEG, new_ss & 0xfffc, retaddr);
            }
#if __Use_Original_Qemu != 1 /* ours (U707) */
            ret_check_eip_limit(env, new_eip, e1, e2, retaddr);
#endif /* __Use_Original_Qemu (U707) */
#if __Use_Original_Qemu != 1 /* ours (U751) */
            /* shadow stack (U751/U752), after the SS checks, before the commit */
            cet2_ret(env, is_iret, rpl, new_cs, e1, e2, new_eip, retaddr);
#endif /* __Use_Original_Qemu (U751) */
            cpu_x86_load_seg_cache(env, R_SS, new_ss,
                                   get_seg_base(ss_e1, ss_e2),
                                   get_seg_limit(ss_e1, ss_e2),
                                   ss_e2);
        }

        cpu_x86_load_seg_cache(env, R_CS, new_cs,
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
        sp = new_esp;
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK) {
            sp_mask = -1;
        } else
#endif
        {
            sp_mask = get_sp_mask(ss_e2);
        }

        /* validate data segments */
        validate_seg(env, R_ES, rpl);
        validate_seg(env, R_DS, rpl);
        validate_seg(env, R_FS, rpl);
        validate_seg(env, R_GS, rpl);

        sp += addend;
    }
    SET_ESP(sp, sp_mask);
    env->eip = new_eip;
    if (is_iret) {
        /* NOTE: 'cpl' is the _old_ CPL */
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | RF_MASK | NT_MASK;
        if (cpl == 0) {
            eflags_mask |= IOPL_MASK;
        }
        iopl = (env->eflags >> IOPL_SHIFT) & 3;
        if (cpl <= iopl) {
            eflags_mask |= IF_MASK;
        }
        if (shift == 0) {
            eflags_mask &= 0xffff;
        }
        cpu_load_eflags(env, new_eflags, eflags_mask);
    }
    return;

 return_to_vm86:
#if __Use_Original_Qemu != 1 /* ours (U752) */
    /*
     * NoVmp (ledger U752): SDM Vol2 IRET, RETURN-TO-VIRTUAL-8086-MODE: "IF CR4.CET AND
     * (IA32_U_CET.ENDBR_EN OR IA32_U_CET.SHSTK_EN) THEN #GP(0)" first; with shadow stacks
     * at CPL 0 SSP must be 8-byte aligned (#CP(FAR-RET/IRET)) and the supervisor token
     * at SSP is freed; SSP itself is not changed.
     */
    if ((env->cr[4] & CR4_CET_MASK) && (env->u_cet & (CET_ENDBR_EN | CET_SH_STK_EN))) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
    }
#endif /* __Use_Original_Qemu (U752) */
    POPL_RA(ssp, sp, sp_mask, new_esp, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_ss, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_es, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_ds, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_fs, retaddr);
    POPL_RA(ssp, sp, sp_mask, new_gs, retaddr);
#if __Use_Original_Qemu != 1 /* ours (U752) */
    if (cet2_ss_en(env, env->hflags & HF_CPL_MASK, false)) {
        if (env->ssp & 7) {
            raise_exception_err_ra(env, EXCP15_CP, CP_FAR_RET_IRET, retaddr);
        }
        cet2_token_release(env, env->ssp, false, false, retaddr);
    }
#endif /* __Use_Original_Qemu (U752) */

    /* modify processor state */
    cpu_load_eflags(env, new_eflags, TF_MASK | AC_MASK | ID_MASK |
                    IF_MASK | IOPL_MASK | VM_MASK | NT_MASK | VIF_MASK |
                    VIP_MASK);
    load_seg_vm(env, R_CS, new_cs & 0xffff);
    load_seg_vm(env, R_SS, new_ss & 0xffff);
    load_seg_vm(env, R_ES, new_es & 0xffff);
    load_seg_vm(env, R_DS, new_ds & 0xffff);
    load_seg_vm(env, R_FS, new_fs & 0xffff);
    load_seg_vm(env, R_GS, new_gs & 0xffff);

    env->eip = new_eip & 0xffff;
    env->regs[R_ESP] = new_esp;
}

void helper_iret_protected(CPUX86State *env, int shift, int next_eip)
{
    int tss_selector, type;
    uint32_t e1, e2;

    /* specific case for TSS */
    if (env->eflags & NT_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
        }
#endif
        tss_selector = cpu_lduw_kernel_ra(env, env->tr.base + 0, GETPC());
        if (tss_selector & 4) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, GETPC());
        }
        if (load_segment_ra(env, &e1, &e2, tss_selector, GETPC()) != 0) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, GETPC());
        }
        type = (e2 >> DESC_TYPE_SHIFT) & 0x17;
        /* NOTE: we check both segment and busy TSS */
        if (type != 3) {
            raise_exception_err_ra(env, EXCP0A_TSS, tss_selector & 0xfffc, GETPC());
        }
        switch_tss_ra(env, tss_selector, e1, e2, SWITCH_TSS_IRET, next_eip, GETPC());
    } else {
        helper_ret_protected(env, shift, 1, 0, GETPC());
    }
    env->hflags2 &= ~HF2_NMI_MASK;
}

void helper_lret_protected(CPUX86State *env, int shift, int addend)
{
    helper_ret_protected(env, shift, 0, addend, GETPC());
}

#if __Use_Original_Qemu != 1 /* ours (U902) */
/*
 * NoVmp (ledger U902, decision A1): SYSENTER as the SDM defines it (Vol2B SYSENTER Operation,
 * CR4.FRED = 0), the default UC_CTL_X86_SYSCALL_MODE. The #GP(0) checks (CR0.PE = 0: translator;
 * IA32_SYSENTER_CS[15:2] = 0: U852) come first; then
 *   RFLAGS.VM := 0; RFLAGS.IF := 0 (RF is cleared too: it is 0 after any completed instruction);
 *   IA-32e mode: RSP := IA32_SYSENTER_ESP, RIP := IA32_SYSENTER_EIP; otherwise ESP / EIP := bits
 *   31:0 of them;
 *   CS.Selector := IA32_SYSENTER_CS[15:0] AND FFFCH, base 0, limit FFFFFH with G = 1, type 11,
 *   S = 1, DPL 0, P = 1; IA-32e mode: L = 1, D = 0 (64-bit mode, also from compatibility mode),
 *   else L = 0, D = 1;
 *   CET: IF ShadowStackEnabled(CPL) THEN IA32_PL3_SSP := SSP (IA32_EFER.LMA = 0) or
 *   LA_adjust(SSP); CPL := 0; IF ShadowStackEnabled(0) THEN SSP := 0; IF EndbranchEnabled(0)
 *   THEN IA32_S_CET.TRACKER := WAIT_FOR_ENDBRANCH, SUPPRESS := 0. The old CPL's test follows
 *   the pseudocode's order: RFLAGS.VM is already 0 there (a SYSENTER from virtual-8086 mode is
 *   judged by IA32_U_CET like CPL 3 code);
 *   SS.Selector := CS.Selector + 8 (16 bits), base 0, limit FFFFFH with G = 1, type 3, S = 1,
 *   DPL 0, P = 1, B = 1.
 * Upstream QEMU's helper_sysenter tests the whole MSR against 0 (U852) and has no CET part.
 * The UC_X86_INS_SYSENTER hooks run after the transition (sys_entry_hooks).
 */
static void sysenter_sdm(CPUX86State *env)
{
    target_ulong insn_eip = env->eip;
    int cpl = env->hflags & HF_CPL_MASK;
    bool lma = (env->hflags & HF_LMA_MASK) != 0;
    uint32_t sel = env->sysenter_cs & 0xfffc;
    bool ss_old;

    env->eflags &= ~(VM_MASK | IF_MASK | RF_MASK);
    ss_old = cet2_ss_en(env, cpl, false);
    if (lma) {
        env->regs[R_ESP] = env->sysenter_esp;
        env->eip = env->sysenter_eip;
    } else {
        env->regs[R_ESP] = (uint32_t)env->sysenter_esp;
        env->eip = (uint32_t)env->sysenter_eip;
    }
    if (ss_old) {
        env->pl_ssp[3] = lma ? cet2_la_adjust(env, env->ssp) : env->ssp;
    }
    cpu_x86_load_seg_cache(env, R_CS, sel, 0, 0xffffffff,
                           DESC_G_MASK | DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK |
                           (lma ? DESC_L_MASK : DESC_B_MASK));
    cpu_x86_load_seg_cache(env, R_SS, (sel + 8) & 0xffff, 0, 0xffffffff,
                           DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                           DESC_W_MASK | DESC_A_MASK);
    if (cet2_ss_en(env, 0, false)) {
        env->ssp = 0;
    }
    if (cet2_ibt_en(env, 0, false)) {
        cet2_ibt_wait(env, 0);
    }
    sys_entry_hooks(env, UC_X86_INS_SYSENTER, insn_eip);
}

#endif /* __Use_Original_Qemu (U902) */
void helper_sysenter(CPUX86State *env, int next_eip_addend)
{
    // Unicorn: call registered SYSENTER hooks
    struct hook *hook;
    uc_engine *uc = env->uc;
    bool synced = false;

    HOOK_FOREACH_VAR_DECLARE;
#if __Use_Original_Qemu != 1 /* ours (U852) */
    /*
     * NoVmp (ledger U852): SDM Vol2B SYSENTER: "IF CR0.PE = 0 OR (CR4.FRED = 0 AND
     * IA32_SYSENTER_CS[15:2] = 0) THEN #GP(0)". CR0.PE = 0 is #GP at translation; the CPU model has
     * no FRED (CR4.FRED reserved), so the test is bits 15:2 of the MSR. Unicorn's hook-only SYSENTER
     * had dropped the check (upstream QEMU's helper_sysenter tests the whole MSR against 0, so
     * selectors 1-3 would pass there). The protected-mode reset state has an OS-like
     * IA32_SYSENTER_CS (unicorn.c reg_reset, U852), so the UC_X86_INS_SYSENTER hook API is
     * unchanged unless the guest or the user loads a NULL selector. The transition itself is
     * sysenter_sdm (U902) unless UC_CTL_X86_SYSCALL_MODE selects the hook-only API.
     */
    if ((env->sysenter_cs & 0xfffc) == 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
#endif /* __Use_Original_Qemu (U852) */
#if __Use_Original_Qemu != 1 /* ours (U902) */
    /* NoVmp (ledger U902): the SDM transition unless UC_CTL_X86_SYSCALL_MODE is hook-only */
    if (uc->x86_syscall_mode != UC_X86_SYSCALL_HOOK_ONLY) {
        sysenter_sdm(env);
        return;
    }
#endif /* __Use_Original_Qemu (U902) */
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN) {
        if (hook->to_delete)
            continue;
        if (!HOOK_BOUND_CHECK(hook, env->eip))
            continue;
        if (hook->insn == UC_X86_INS_SYSENTER) {
            uintptr_t pc = GETPC();
            if (!synced && !uc->skip_sync_pc_on_exit && pc) {
                cpu_restore_state(uc->cpu, pc, false);
                synced = true;
            }
            JIT_CALLBACK_GUARD(((uc_cb_insn_syscall_t)hook->callback)(env->uc, hook->user_data));
        }

        // the last callback may already asked to stop emulation
        if (env->uc->stop_request)
            break;
    }

    env->eip += next_eip_addend;
}

void helper_sysexit(CPUX86State *env, int dflag)
{
    int cpl;

    cpl = env->hflags & HF_CPL_MASK;
    if (env->sysenter_cs == 0 || cpl != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
#ifdef TARGET_X86_64
    if (dflag == 2) {
        cpu_x86_load_seg_cache(env, R_CS, ((env->sysenter_cs + 32) & 0xfffc) |
                               3, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                               DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, ((env->sysenter_cs + 40) & 0xfffc) |
                               3, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
    } else
#endif
    {
        cpu_x86_load_seg_cache(env, R_CS, ((env->sysenter_cs + 16) & 0xfffc) |
                               3, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        cpu_x86_load_seg_cache(env, R_SS, ((env->sysenter_cs + 24) & 0xfffc) |
                               3, 0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
    }
    env->regs[R_ESP] = env->regs[R_ECX];
    env->eip = env->regs[R_EDX];
#if __Use_Original_Qemu != 1 /* ours (U753) */
    /* NoVmp (ledger U753): SDM Vol2B SYSEXIT: CPL := 3; IF ShadowStackEnabled(CPL) SSP := IA32_PL3_SSP */
    if (cet2_ss_en(env, 3, env->eflags & VM_MASK)) {
        env->ssp = env->pl_ssp[3];
    }
#endif /* __Use_Original_Qemu (U753) */
}

target_ulong helper_lsl(CPUX86State *env, target_ulong selector1)
{
    unsigned int limit;
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl, type;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env, CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
        goto fail;
    }
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if ((e2 & DESC_CS_MASK) && (e2 & DESC_C_MASK)) {
            /* conforming */
        } else {
            if (dpl < cpl || dpl < rpl) {
                goto fail;
            }
        }
    } else {
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        switch (type) {
        case 1:
        case 2:
        case 3:
        case 9:
        case 11:
            break;
        default:
            goto fail;
        }
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return 0;
        }
    }
    limit = get_seg_limit(e1, e2);
    CC_SRC = eflags | CC_Z;
    return limit;
}

target_ulong helper_lar(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl, type;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env, CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
        goto fail;
    }
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if ((e2 & DESC_CS_MASK) && (e2 & DESC_C_MASK)) {
            /* conforming */
        } else {
            if (dpl < cpl || dpl < rpl) {
                goto fail;
            }
        }
    } else {
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        switch (type) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 9:
        case 11:
        case 12:
            break;
        default:
            goto fail;
        }
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return 0;
        }
    }
    CC_SRC = eflags | CC_Z;
    return e2 & 0x00f0ff00;
}

void helper_verr(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env, CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
        goto fail;
    }
    if (!(e2 & DESC_S_MASK)) {
        goto fail;
    }
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_CS_MASK) {
        if (!(e2 & DESC_R_MASK)) {
            goto fail;
        }
        if (!(e2 & DESC_C_MASK)) {
            if (dpl < cpl || dpl < rpl) {
                goto fail;
            }
        }
    } else {
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return;
        }
    }
    CC_SRC = eflags | CC_Z;
}

void helper_verw(CPUX86State *env, target_ulong selector1)
{
    uint32_t e1, e2, eflags, selector;
    int rpl, dpl, cpl;

    selector = selector1 & 0xffff;
    eflags = cpu_cc_compute_all(env, CC_OP);
    if ((selector & 0xfffc) == 0) {
        goto fail;
    }
    if (load_segment_ra(env, &e1, &e2, selector, GETPC()) != 0) {
        goto fail;
    }
    if (!(e2 & DESC_S_MASK)) {
        goto fail;
    }
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_CS_MASK) {
        goto fail;
    } else {
        if (dpl < cpl || dpl < rpl) {
            goto fail;
        }
        if (!(e2 & DESC_W_MASK)) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return;
        }
    }
    CC_SRC = eflags | CC_Z;
}

void cpu_x86_load_seg(CPUX86State *env, int seg_reg, int selector)
{
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        int dpl = (env->eflags & VM_MASK) ? 3 : 0;
        selector &= 0xffff;
        cpu_x86_load_seg_cache(env, seg_reg, selector,
                               (selector << 4), 0xffff,
                               DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                               DESC_A_MASK | (dpl << DESC_DPL_SHIFT));
    } else {
        helper_load_seg(env, seg_reg, selector);
    }
}

/* check if Port I/O is allowed in TSS */
static inline void check_io(CPUX86State *env, int addr, int size,
                            uintptr_t retaddr)
{
    int io_offset, val, mask;

    /* TSS must be a valid 32 bit one */
    if (!(env->tr.flags & DESC_P_MASK) ||
        ((env->tr.flags >> DESC_TYPE_SHIFT) & 0xf) != 9 ||
        env->tr.limit < 103) {
        goto fail;
    }
    io_offset = cpu_lduw_kernel_ra(env, env->tr.base + 0x66, retaddr);
    io_offset += (addr >> 3);
    /* Note: the check needs two bytes */
    if ((io_offset + 1) > env->tr.limit) {
        goto fail;
    }
    val = cpu_lduw_kernel_ra(env, env->tr.base + io_offset, retaddr);
    val >>= (addr & 7);
    mask = (1 << size) - 1;
    /* all bits must be zero to allow the I/O */
    if ((val & mask) != 0) {
    fail:
        raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
    }
}

void helper_check_iob(CPUX86State *env, uint32_t t0)
{
    check_io(env, t0, 1, GETPC());
}

void helper_check_iow(CPUX86State *env, uint32_t t0)
{
    check_io(env, t0, 2, GETPC());
}

void helper_check_iol(CPUX86State *env, uint32_t t0)
{
    check_io(env, t0, 4, GETPC());
}

void helper_check_io(CPUX86State *env, uint32_t addr, uint32_t size)
{
    check_io(env, addr, size, GETPC());
}

#if __Use_Original_Qemu != 1 /* ours (U104) */
/*
 * NoVmp (ledger U104): user interrupts (SDM Vol2 CLUI, STUI, TESTUI, UIRET,
 * SENDUIPI; Vol3A chapter 9). State: CPUX86State.uintr_*. A recognized user
 * interrupt (UIRR != 0) keeps CPU_INTERRUPT_UINTR requested; it is delivered
 * at an instruction boundary where x86_uintr_deliverable() holds (checked by
 * x86_cpu_pending_interrupt whenever the CPU loop looks at interrupts, which
 * includes every TB that ends with STUI, UIRET, SENDUIPI, WRMSR, MOV CR4,
 * IRET/SYSRET and the like).
 * Not modelled: the local APIC (an IPI that SENDUIPI sends to another APIC
 * ID, with a vector other than UINV, or while RFLAGS.IF = 0 is dropped),
 * external user-interrupt notifications, the XSAVES user-interrupt state
 * component, CET shadow-stack / IBT effects, MOV SS vs STI blocking (both
 * block here), enclaves, TSX aborts, and the WB / canonical checks of the
 * stack accesses (QEMU does not model those for any instruction).
 */
static bool uintr_canonical(CPUX86State *env, uint64_t addr)
{
    int shift = (env->cr[4] & CR4_LA57_MASK) ? 64 - 57 : 64 - 48;

    return (uint64_t)((int64_t)(addr << shift) >> shift) == addr;
}

void x86_uintr_update_request(CPUX86State *env)
{
    if (env->uintr_rr) {
        cpu_interrupt(env_cpu(env), CPU_INTERRUPT_UINTR);
    } else {
        cpu_reset_interrupt(env_cpu(env), CPU_INTERRUPT_UINTR);
    }
}

/* SDM Vol3A 9.4.2: CR4.UINTR, UIF, no MOV SS / POP SS blocking, CPL 3, 64-bit mode */
bool x86_uintr_deliverable(CPUX86State *env)
{
    return env->uintr_rr != 0 && (env->cr[4] & CR4_UINTR_MASK) && env->uintr_uif &&
           !(env->hflags & HF_INHIBIT_IRQ_MASK) && (env->hflags & HF_CPL_MASK) == 3 &&
           (env->hflags & HF_CS64_MASK);
}

/* user-interrupt delivery (SDM Vol3A 9.4.2 pseudocode); faults leave UIRR, UIF and RSP */
void x86_uintr_deliver(CPUX86State *env)
{
    uint64_t hold = env->regs[R_ESP], rsp;
    int v = 63 - clz64(env->uintr_rr);

    if (!uintr_canonical(env, env->uintr_handler)) {
        raise_exception_err(env, EXCP0D_GPF, 0);
    }
    rsp = (env->uintr_stackadjust & 1) ? env->uintr_stackadjust
                                       : hold - env->uintr_stackadjust;
    rsp &= ~0xfULL;
    cpu_stq_data_ra(env, rsp - 8, hold, 0);
    cpu_stq_data_ra(env, rsp - 16, cpu_compute_eflags(env), 0);
    cpu_stq_data_ra(env, rsp - 24, env->eip, 0);
    cpu_stq_data_ra(env, rsp - 32, (uint64_t)v, 0);   /* UIRRV, 64-bit push */
    env->regs[R_ESP] = rsp - 32;
    env->uintr_rr &= ~(1ULL << v);
    x86_uintr_update_request(env);
    env->uintr_uif = 0;
    env->eflags &= ~(TF_MASK | RF_MASK);
    env->eip = env->uintr_handler;
}

static void uintr_check_cr4(CPUX86State *env, uintptr_t ra)
{
    if (!(env->cr[4] & CR4_UINTR_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
}

void helper_clui(CPUX86State *env)
{
    uintr_check_cr4(env, GETPC());
    env->uintr_uif = 0;
}

void helper_stui(CPUX86State *env)
{
    uintr_check_cr4(env, GETPC());
    env->uintr_uif = 1;
    x86_uintr_update_request(env);
}

void helper_testui(CPUX86State *env)
{
    uintr_check_cr4(env, GETPC());
    CC_SRC = env->uintr_uif ? CC_C : 0;     /* CF := UIF; ZF, AF, OF, PF, SF := 0 */
}

void helper_uiret(CPUX86State *env)
{
    uintptr_t ra = GETPC();
    uint64_t sp = env->regs[R_ESP], rip, rflags, rsp;

    uintr_check_cr4(env, ra);
    rip = cpu_ldq_data_ra(env, env->segs[R_SS].base + sp, ra);
    rflags = cpu_ldq_data_ra(env, env->segs[R_SS].base + sp + 8, ra);
    rsp = cpu_ldq_data_ra(env, env->segs[R_SS].base + sp + 16, ra);
    if (!uintr_canonical(env, rip)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    env->eip = rip;
    /* only CF, PF, AF, ZF, SF, TF, DF, OF, NT, RF, AC and ID */
    cpu_load_eflags(env, (uint32_t)rflags, 0x254dd5);
    env->regs[R_ESP] = rsp;
    /* CPUID.(07H,01H):EDX.UIRET_UIF = 1 in this model: UIF := tempRFLAGS[1] */
    env->uintr_uif = (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_UIRET_UIF)
                     ? (rflags >> 1) & 1 : 1;
    x86_uintr_update_request(env);
}

/* user-interrupt notification processing (SDM Vol3A 9.5.2), supervisor accesses */
static void uintr_notification(CPUX86State *env, uintptr_t ra)
{
    int idx = cpu_mmu_index_kernel(env);
    uint64_t upid = env->uintr_pd, pir;

    cpu_stq_mmuidx_ra(env, upid, cpu_ldq_mmuidx_ra(env, upid, idx, ra) & ~1ULL, idx, ra);
    pir = cpu_ldq_mmuidx_ra(env, upid + 8, idx, ra);
    cpu_stq_mmuidx_ra(env, upid + 8, 0, idx, ra);
    if (pir) {
        env->uintr_rr |= pir;
        x86_uintr_update_request(env);
    }
}

void helper_senduipi(CPUX86State *env, target_ulong reg)
{
    uintptr_t ra = GETPC();
    int idx = cpu_mmu_index_kernel(env);
    uint64_t uitte_lo, uitte_hi, upid_lo, upid_hi, upid;
    unsigned uv;

    uintr_check_cr4(env, ra);
    if (!(env->uintr_tt & 1)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if ((uint64_t)reg > (uint32_t)env->uintr_misc) {          /* reg > UITTSZ */
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    uitte_lo = cpu_ldq_mmuidx_ra(env, (env->uintr_tt & ~0xfULL) + reg * 16, idx, ra);
    uitte_hi = cpu_ldq_mmuidx_ra(env, (env->uintr_tt & ~0xfULL) + reg * 16 + 8, idx, ra);
    /* V = 1; bits 7:1, 15:14 (UV < 64), 63:16 and 69:64 (64-byte aligned UPID) zero */
    if (!(uitte_lo & 1) || (uitte_lo & 0xffffffffffffc0feULL) || (uitte_hi & 0x3f) ||
        !uintr_canonical(env, uitte_hi)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    uv = (uitte_lo >> 8) & 0x3f;
    upid = uitte_hi;
    upid_lo = cpu_ldq_mmuidx_ra(env, upid, idx, ra);
    upid_hi = cpu_ldq_mmuidx_ra(env, upid + 8, idx, ra);
    if (upid_lo & 0xff00fffcULL) {                               /* bits 15:2, 31:24 */
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    upid_hi |= 1ULL << uv;                                       /* PIR[UV] := 1 */
    if (!(upid_lo & 3)) {                                        /* SN = ON = 0 */
        upid_lo |= 1;                                            /* ON := 1, notify */
        cpu_stq_mmuidx_ra(env, upid, upid_lo, idx, ra);
        cpu_stq_mmuidx_ra(env, upid + 8, upid_hi, idx, ra);
        /*
         * Ordinary IPI, vector NV, to xAPIC ID NDST[15:8] (this fork has no
         * x2APIC). Only a self-IPI that is a user-interrupt notification
         * (NV = UINV, CR4.UINTR = IA32_EFER.LMA = 1) and that the CPU accepts
         * at once (RFLAGS.IF = 1) is modelled; any other IPI is dropped.
         */
        if (((upid_lo >> 40) & 0xff) == (env_archcpu(env)->apic_id & 0xff) &&
            ((upid_lo >> 16) & 0xff) == ((env->uintr_misc >> 32) & 0xff) &&
            (env->efer & MSR_EFER_LMA) && (env->eflags & IF_MASK) &&
            !(env->hflags & HF_INHIBIT_IRQ_MASK)) {
            uintr_notification(env, ra);
        }
    } else {
        cpu_stq_mmuidx_ra(env, upid, upid_lo, idx, ra);
        cpu_stq_mmuidx_ra(env, upid + 8, upid_hi, idx, ra);
    }
}
#endif /* __Use_Original_Qemu (U104) */
