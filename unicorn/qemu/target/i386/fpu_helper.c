/*
 *  x86 FPU, MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include <math.h>
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "fpu/softfloat-macros.h"
#include "uc_priv.h"
#if __Use_Original_Qemu != 1 /* ours (U53) */
#include "x87_trans.h"
#endif /* __Use_Original_Qemu (U53) */
#if __Use_Original_Qemu != 1 /* ours (U84) */
#include "crypto/sm4.h"
#endif /* __Use_Original_Qemu (U84) */

/* float macros */
#define FT0    (env->ft0)
#define ST0    (env->fpregs[env->fpstt].d)
#define ST(n)  (env->fpregs[(env->fpstt + (n)) & 7].d)
#define ST1    ST(1)

#define FPU_RC_SHIFT        10
#define FPU_RC_MASK         (3 << FPU_RC_SHIFT)
#define FPU_RC_NEAR         0x000
#define FPU_RC_DOWN         0x400
#define FPU_RC_UP           0x800
#define FPU_RC_CHOP         0xc00

#define MAXTAN 9223372036854775808.0

/* the following deal with x86 long double-precision numbers */
#define MAXEXPD 0x7fff
#define EXPBIAS 16383
#define EXPD(fp)        (fp.l.upper & 0x7fff)
#define SIGND(fp)       ((fp.l.upper) & 0x8000)
#define MANTD(fp)       (fp.l.lower)
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7fff)) | EXPBIAS

#define FPUS_IE (1 << 0)
#define FPUS_DE (1 << 1)
#define FPUS_ZE (1 << 2)
#define FPUS_OE (1 << 3)
#define FPUS_UE (1 << 4)
#define FPUS_PE (1 << 5)
#define FPUS_SF (1 << 6)
#define FPUS_SE (1 << 7)
#if __Use_Original_Qemu != 1 /* ours (U45) */
#define FPUS_C1 (1 << 9)
#endif /* __Use_Original_Qemu (U45) */
#define FPUS_B  (1 << 15)

#define FPUC_EM 0x3f

#define floatx80_lg2 make_floatx80(0x3ffd, 0x9a209a84fbcff799LL)
#define floatx80_lg2_d make_floatx80(0x3ffd, 0x9a209a84fbcff798LL)
#define floatx80_l2e make_floatx80(0x3fff, 0xb8aa3b295c17f0bcLL)
#define floatx80_l2e_d make_floatx80(0x3fff, 0xb8aa3b295c17f0bbLL)
#define floatx80_l2t make_floatx80(0x4000, 0xd49a784bcd1b8afeLL)
#define floatx80_l2t_u make_floatx80(0x4000, 0xd49a784bcd1b8affLL)
#define floatx80_ln2_d make_floatx80(0x3ffe, 0xb17217f7d1cf79abLL)
#define floatx80_pi_d make_floatx80(0x4000, 0xc90fdaa22168c234LL)

void x86_register_ferr_irq(qemu_irq irq)
{
    (void)irq;
}

void fpu_check_raise_ferr_irq(CPUX86State *env)
{
    (void)env;
}

static void cpu_clear_ignne(CPUX86State *env)
{
    env->hflags2 &= ~HF2_IGNNE_MASK;
}

void cpu_set_ignne(CPUX86State *env)
{
    env->hflags2 |= HF2_IGNNE_MASK;
}

static inline void fpush(CPUX86State *env)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fptags[env->fpstt] = 0; /* validate stack entry */
}

static inline void fpop(CPUX86State *env)
{
    env->fptags[env->fpstt] = 1; /* invalidate stack entry */
    env->fpstt = (env->fpstt + 1) & 7;
}

#if __Use_Original_Qemu != 1 /* ours (U480) */
/*
 * NoVmp (ledger U480): Unicorn reports memory it has not mapped only when an access is made,
 * and a store or load then goes on (the exit is only requested), so an instruction would
 * store or load part of its operand. Before anything is stored or loaded, a page Unicorn has
 * not mapped is reported through a one-byte read of it (UC_HOOK_MEM_READ_UNMAPPED; #PF in
 * emu-alltest, where the i5-13600K stores / loads nothing), as helper_evex_mstore does (U210);
 * if no hook maps it, the instruction stops there.
 */
static void x86_access_unicorn_mapped(CPUX86State *env, target_ulong ptr, MMUAccessType type,
                                      int mmu_idx, uintptr_t ra)
{
    struct uc_struct *uc = env->uc;
    target_ulong paddr;

    if (!tlb_vaddr_to_paddr(env, ptr, type, mmu_idx, &paddr) ||
        uc->memory_mapping(uc, paddr) != NULL) {
        return;
    }
    (void)cpu_ldub_data_ra(env, ptr, ra);
    if (uc->invalid_error != UC_ERR_OK && uc->nested_level > 0 && !uc->cpu->stopped) {
        cpu_loop_exit_restore(uc->cpu, ra);
    }
}
#endif /* __Use_Original_Qemu (U480) */

/*
 * backport d3e8b648ab / 4526f58a27 and the X86Access series (bc13c2dd01, 505e2ef744,
 * 94f60f8f1c, 6d030aab29, c6e6d1508a, d5dc3a927a), done by probing instead of importing
 * access.c (U480): every page of [ptr, ptr + len) is translated for 'type' before the
 * instruction stores or loads anything, so a #PF on a later page leaves memory and the
 * register state unchanged (upstream access_prepare()).
 */
static void x86_access_prepare(CPUX86State *env, target_ulong ptr, target_ulong len,
                               MMUAccessType type, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env, false);
    target_ulong p = ptr, l = len, n;

#if __Use_Original_Qemu == 1 /* original QEMU (U706) */
    while (l) {
        n = TARGET_PAGE_SIZE - (p & ~TARGET_PAGE_MASK);
        if (n > l) {
            n = l;
        }
        probe_access(env, p, (int)n, type, mmu_idx, ra);
        p += n;
        l -= n;
    }
#else /* ours (U480/U706) */
    /*
     * U706: page by page in address order, each page translated and (Unicorn) checked for
     * being mapped before the next one: these helpers' instructions (FXSAVE/FXRSTOR, XSAVE,
     * FSAVE/FRSTOR, FLDENV/FSTENV, FBLD, ...) are split by the CPU into parts, and the i5-13600K
     * reports the fault of the lowest part first (FBLD across 0000_7FFF_FFFF_FFFFh: #PF on the
     * not-present canonical page, not #GP; cases_fix3). Nothing is stored or loaded yet.
     */
    for (; l; p += n, l -= n) {
        n = TARGET_PAGE_SIZE - (p & ~TARGET_PAGE_MASK);
        if (n > l) {
            n = l;
        }
        probe_access(env, p, (int)n, type, mmu_idx, ra);
        x86_access_unicorn_mapped(env, p, type, mmu_idx, ra);
    }
#endif /* __Use_Original_Qemu (U706) */
}

static floatx80 do_fldt(CPUX86State *env, target_ulong ptr, uintptr_t retaddr)
{
    CPU_LDoubleU temp;

    temp.l.lower = cpu_ldq_data_ra(env, ptr, retaddr);
    temp.l.upper = cpu_lduw_data_ra(env, ptr + 8, retaddr);
    return temp.d;
}

static void do_fstt(CPUX86State *env, floatx80 f, target_ulong ptr,
                    uintptr_t retaddr)
{
    CPU_LDoubleU temp;

    temp.d = f;
    cpu_stq_data_ra(env, ptr, temp.l.lower, retaddr);
    cpu_stw_data_ra(env, ptr + 8, temp.l.upper, retaddr);
}

/* x87 FPU helpers */

static inline double floatx80_to_double(CPUX86State *env, floatx80 a)
{
    union {
        float64 f64;
        double d;
    } u;

    u.f64 = floatx80_to_float64(a, &env->fp_status);
    return u.d;
}

static inline floatx80 double_to_floatx80(CPUX86State *env, double a)
{
    union {
        float64 f64;
        double d;
    } u;

    u.d = a;
    return float64_to_floatx80(u.f64, &env->fp_status);
}

static void fpu_set_exception(CPUX86State *env, int mask)
{
    env->fpus |= mask;
    if (env->fpus & (~env->fpuc & FPUC_EM)) {
        env->fpus |= FPUS_SE | FPUS_B;
    }
}

static inline int save_exception_flags(CPUX86State *env)
{
    int old_flags = get_float_exception_flags(&env->fp_status);
    set_float_exception_flags(0, &env->fp_status);
    return old_flags;
}

#if __Use_Original_Qemu == 1 /* original QEMU (U28/U29/U45/U46/U47/U53/U54/U57) */
static void merge_exception_flags(CPUX86State *env, int old_flags)
{
    int new_flags = get_float_exception_flags(&env->fp_status);
    float_raise(old_flags, &env->fp_status);
    fpu_set_exception(env,
                      ((new_flags & float_flag_invalid ? FPUS_IE : 0) |
                       (new_flags & float_flag_divbyzero ? FPUS_ZE : 0) |
                       (new_flags & float_flag_overflow ? FPUS_OE : 0) |
                       (new_flags & float_flag_underflow ? FPUS_UE : 0) |
                       (new_flags & float_flag_inexact ? FPUS_PE : 0) |
                       (new_flags & float_flag_input_denormal_used ? FPUS_DE : 0)));
}

static inline floatx80 helper_fdiv(CPUX86State *env, floatx80 a, floatx80 b)
{
    int old_flags = save_exception_flags(env);
    floatx80 ret = floatx80_div(a, b, &env->fp_status);
    merge_exception_flags(env, old_flags);
    return ret;
}
#else /* ours (U28/U29/U45/U46/U47/U53/U54/U57) */
/*
 * NoVmp (ledger U45): set_c1 makes FSW.C1 report the rounding direction of
 * this operation, "1 = rounded up, 0 = not" (SDM Vol1 8.1.3.1 / 4.9.1.6;
 * the per-instruction "C1 ... set if result was rounded up; cleared
 * otherwise"). FPREM/FPREM1 (C1 = Q0) and FCOMI/FUCOMI (C1 = 0) keep
 * the C1 they computed and merge with set_c1 = false.
 */
static void merge_exception_flags_ex(CPUX86State *env, int old_flags,
                                     bool set_c1)
{
    int new_flags = get_float_exception_flags(&env->fp_status);
    float_raise(old_flags, &env->fp_status);
    if (set_c1) {
        env->fpus = (env->fpus & ~FPUS_C1) |
                    (new_flags & float_flag_rounded_up ? FPUS_C1 : 0);
    }
    fpu_set_exception(env,
                      ((new_flags & float_flag_invalid ? FPUS_IE : 0) |
                       (new_flags & float_flag_divbyzero ? FPUS_ZE : 0) |
                       (new_flags & float_flag_overflow ? FPUS_OE : 0) |
                       (new_flags & float_flag_underflow ? FPUS_UE : 0) |
                       (new_flags & float_flag_inexact ? FPUS_PE : 0) |
                       (new_flags & float_flag_input_denormal_used ? FPUS_DE : 0)));
}

static void merge_exception_flags(CPUX86State *env, int old_flags)
{
    merge_exception_flags_ex(env, old_flags, true);
}

/* U705: bytes of an x87 memory operand of format X87F_* */
static uint32_t x87_fmt_len(unsigned fmt)
{
    switch (fmt) {
    case X87F_I16:
        return 2;
    case X87F_F32:
    case X87F_I32:
        return 4;
    case X87F_F64:
    case X87F_I64:
        return 8;
    default:        /* F80, BCD */
        return 10;
    }
}

/*
 * U705: the masked #IS response of a store, the QNaN / integer indefinite in its format; each
 * part is checked first (x86_probe_store, U592: a page Unicorn has not mapped stops the
 * instruction before the part is stored). F80 / packed BCD: bytes 7:0, then bytes 9:8 (U480).
 */
static void x87_store_part(CPUX86State *env, target_ulong a0, uint64_t v, uint32_t len,
                           uintptr_t ra)
{
    uint8_t img[8];
    uint32_t k;

    for (k = 0; k < len; k++) {
        img[k] = (uint8_t)(v >> (8 * k));
    }
    x86_probe_store(env, a0, len, img, ra);
    switch (len) {
    case 2:
        cpu_stw_data_ra(env, a0, (uint16_t)v, ra);
        break;
    case 4:
        cpu_stl_data_ra(env, a0, (uint32_t)v, ra);
        break;
    default:
        cpu_stq_data_ra(env, a0, v, ra);
        break;
    }
}

static void x87_store_indefinite(CPUX86State *env, uint32_t desc, target_ulong a0, uintptr_t ra)
{
    switch (X87D_FMT(desc)) {
    case X87F_F32:
        x87_store_part(env, a0, 0xffc00000u, 4, ra);
        break;
    case X87F_F64:
        x87_store_part(env, a0, 0xfff8000000000000ull, 8, ra);
        break;
    case X87F_I16:
        x87_store_part(env, a0, 0x8000, 2, ra);
        break;
    case X87F_I32:
        x87_store_part(env, a0, 0x80000000u, 4, ra);
        break;
    case X87F_I64:
        x87_store_part(env, a0, 0x8000000000000000ull, 8, ra);
        break;
    default: {  /* F80 and packed-BCD indefinite share the encoding */
        floatx80 ind = floatx80_default_nan(&env->fp_status);

        x87_store_part(env, a0, ind.low, 8, ra);
        x87_store_part(env, a0 + 8, ind.high, 2, ra);
        break;
    }
    }
}

/*
 * NoVmp (ledger U46): x87 stack overflow / underflow, checked before every
 * x87 operation that reads or pushes registers (descriptor in cpu.h, built
 * by x87_stack_desc in translate.c). Returns nonzero when the translated
 * operation must be skipped: the fault was handled here.
 */
uint32_t helper_x87_pre(CPUX86State *env, uint32_t desc, target_ulong a0)
{
    uintptr_t ra = GETPC();

    env->x87_nopop = 0;
    env->ft0_den = 0;
    unsigned reads = X87D_READS(desc), cat = X87D_CAT(desc);
    unsigned dst = X87D_DST(desc), pops = X87D_POPS(desc), i;
    bool under = false, over = false;
    floatx80 ind;

    for (i = 0; i < 8; i++) {
        if ((reads & (1u << i)) && env->fptags[(env->fpstt + i) & 7]) {
            under = true;
        }
    }
    if ((desc & X87D_PUSH) && !env->fptags[(env->fpstt - 1) & 7]) {
        over = true;
    }
    if (!under && !over) {
        if (desc & X87D_C1CLR) {
            env->fpus &= ~FPUS_C1;
        }
        return 0;
    }
    if (cat == X87C_FSTPNCE) {
        /* undocumented FSTP1 alias: an empty ST0 is popped silently (i5-13600K) */
        env->fpus &= ~FPUS_C1;
        fpop(env);
        return 1;
    }
    /*
     * U705: the memory operand is accessed before the stack fault is reported. A memory source
     * (arithmetic, compare, FLD/FILD/FBLD m) is read first: a #PF there leaves the x87 state
     * unchanged (the i5-13600K faults with FSW, the tags and the registers as they were, also
     * with FCW.IM = 0). A masked store response stores the indefinite first and changes FSW,
     * C1 and the stack only after the store (FSTP m80 / FBSTP: bytes 7:0, then bytes 9:8, as
     * U480); an unmasked one does not access memory. SDM Vol3A 6.15: the faulting instruction
     * is not executed.
     */
    if (desc & X87D_MEM) {
        if (cat != X87C_STORE) {
            x86_access_prepare(env, a0, x87_fmt_len(X87D_FMT(desc)), MMU_DATA_LOAD, ra);
        } else if (env->fpuc & 0x0001) {            /* FCW.IM */
            x87_store_indefinite(env, desc, a0, ra);
        }
    }

    /* #IS: IE and SF; C1 = 1 for overflow, 0 for underflow (SDM 8.5.1.1) */
    env->fpus = (env->fpus & ~FPUS_C1) | (over ? FPUS_C1 : 0);
    if (desc & X87D_C2CLR) {
        env->fpus &= ~0x400;            /* FSIN/FCOS/FSINCOS/FPTAN: C2 := 0 (i5-13600K, SDM) */
    }
    fpu_set_exception(env, FPUS_IE | FPUS_SF);
    if (cat == X87C_COMPARE) {
        /* unordered, set whether or not IM is masked (i5-13600K) */
        if (desc & X87D_FCOMI) {
            CC_SRC = CC_Z | CC_P | CC_C;
        } else {
            env->fpus |= 0x4500;    /* C3, C2, C0 */
        }
    }
    if (env->fpuc & 0x0001) {      /* FCW.IM */
        /* masked response: QNaN floating-point indefinite */
        ind = floatx80_default_nan(&env->fp_status);
        switch (cat) {
        case X87C_DST:
            env->fpregs[(env->fpstt + dst) & 7].d = ind;
            env->fptags[(env->fpstt + dst) & 7] = 0;
            break;
        case X87C_STORE:
            break;          /* stored above, before FSW changed (U705) */
        case X87C_PUSH:
            fpush(env);
            ST0 = ind;
            break;
        case X87C_PUSH2:
            ST0 = ind;
            env->fptags[env->fpstt] = 0;
            fpush(env);
            ST0 = ind;
            break;
        case X87C_FXCH: {
            unsigned r0 = env->fpstt & 7, ri = (env->fpstt + dst) & 7;
            floatx80 t;
            if (env->fptags[r0]) {
                env->fpregs[r0].d = ind;
                env->fptags[r0] = 0;
            }
            if (env->fptags[ri]) {
                env->fpregs[ri].d = ind;
                env->fptags[ri] = 0;
            }
            t = env->fpregs[r0].d;
            env->fpregs[r0].d = env->fpregs[ri].d;
            env->fpregs[ri].d = t;
            break;
        }
        default:
            break;
        }
        for (i = 0; i < pops; i++) {
            fpop(env);
        }
    }
    return 1;
}

/*
 * NoVmp (ledger U47): "The precision-control bits only affect the results of
 * the following floating-point instructions: FADD, FADDP, FIADD, FSUB, FSUBP,
 * FISUB, FSUBR, FSUBRP, FISUBR, FMUL, FMULP, FIMUL, FDIV, FDIVP, FIDIV, FDIVR,
 * FDIVRP, FIDIVR, and FSQRT" (SDM Vol1 8.1.5.2). fp_status therefore stays at
 * 64-bit precision (update_fp_status) and only those helpers apply FCW.PC,
 * between x87_arith_begin and x87_arith_end. Before, FLD m32/m64 operands,
 * FPREM, FSCALE, FRNDINT and the transcendentals were rounded to PC as well.
 */
static int x87_arith_begin(CPUX86State *env)
{
    static const FloatX80RoundPrec pc[4] = {
        floatx80_precision_s, floatx80_precision_x,     /* 01B: reserved */
        floatx80_precision_d, floatx80_precision_x,
    };
    set_floatx80_rounding_precision(pc[(env->fpuc >> 8) & 3], &env->fp_status);
    return save_exception_flags(env);
}

static void x87_arith_end(CPUX86State *env, int old_flags)
{
    set_floatx80_rounding_precision(floatx80_precision_x, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

/* any of these FSW exception bits unmasked in FCW? */
static inline bool x87_unmasked(CPUX86State *env, int fsw_bits)
{
    return (fsw_bits & ~env->fpuc & 0x3f) != 0;
}

/* the softfloat flags of an x87 operation as FSW exception bits */
static int x87_fsw_bits(int f)
{
    return (f & float_flag_invalid ? FPUS_IE : 0) |
           (f & float_flag_divbyzero ? FPUS_ZE : 0) |
           (f & float_flag_overflow ? FPUS_OE : 0) |
           (f & float_flag_underflow ? FPUS_UE : 0) |
           (f & float_flag_inexact ? FPUS_PE : 0) |
           (f & float_flag_input_denormal_used ? FPUS_DE : 0);
}

/*
 * Unmasked #IA, #D or #Z among the operation's flags 'f': SDM Vol1 8.5.1.2,
 * 8.5.2, 8.5.3 - the flags are set, nothing is stored, TOP and the source
 * operands remain unchanged (env->x87_nopop cancels the instruction's pops).
 * Restores fp_status to its state before the operation.
 */
static bool x87_cancel_unmasked(CPUX86State *env, int old_flags, int f)
{
    int bits = x87_fsw_bits(f) & (FPUS_IE | FPUS_DE | FPUS_ZE);

    if (!x87_unmasked(env, bits)) {
        return false;
    }
    set_floatx80_rounding_precision(floatx80_precision_x, &env->fp_status);
    set_float_exception_flags(old_flags, &env->fp_status);
    env->fpus &= ~FPUS_C1;          /* C1 cleared, C0/C2/C3 kept (i5-13600K) */
    fpu_set_exception(env, bits);
    env->x87_nopop = 1;
    return true;
}

/*
 * NoVmp (ledger U54): FADD/FSUB/FSUBR/FMUL/FDIV/FDIVR (every form) and FSQRT
 * with the SDM Vol1 8.5 responses. Unmasked #IA/#D/#Z: nothing stored (see
 * x87_cancel_unmasked). A finite result that overflows, underflows or is tiny
 * is recomputed exactly (x87t_arith) and shaped by x87t_round_prec: masked
 * per Table 4-11 / 4.9.1.5, unmasked as the significand rounded to FCW.PC/RC
 * with the exponent biased by 2^-+24576 (8.5.4/8.5.5), C1 = round-up.
 * Returns true when *res is to be written to the destination.
 */
/*
 * FT0 (a converted m32fp/m64fp operand) on side 'ft0' (1 = a, 2 = b): its #D
 * counts unless the other operand is a NaN or unsupported (i5-13600K: QNaN
 * ST0 + denormal m32 sets no DE)
 */
static void x87_ft0_denormal(CPUX86State *env, int ft0, floatx80 other)
{
    if (ft0 && env->ft0_den && !floatx80_is_any_nan(other) && !floatx80_invalid_encoding(other)) {
        float_raise(float_flag_input_denormal_used, &env->fp_status);
    }
}

static bool x87_arith2_ft0(CPUX86State *env, int op, floatx80 a, floatx80 b, floatx80 *res, int ft0);

static bool x87_arith2(CPUX86State *env, int op, floatx80 a, floatx80 b, floatx80 *res)
{
    return x87_arith2_ft0(env, op, a, b, res, 0);
}

static bool x87_arith2_ft0(CPUX86State *env, int op, floatx80 a, floatx80 b, floatx80 *res, int ft0)
{
    static const int prec_of_pc[4] = { 24, 64, 53, 64 };   /* 01B reserved: 64 */
    int old_flags, f;
    floatx80 r;

    if (env->x87_nopop) {
        return false;       /* the memory operand took an unmasked #IA/#D already */
    }
    old_flags = x87_arith_begin(env);
    switch (op) {
    case X87T_OP_ADD:
        r = floatx80_add(a, b, &env->fp_status);
        break;
    case X87T_OP_SUB:
        r = floatx80_sub(a, b, &env->fp_status);
        break;
    case X87T_OP_MUL:
        r = floatx80_mul(a, b, &env->fp_status);
        break;
    case X87T_OP_DIV:
        r = floatx80_div(a, b, &env->fp_status);
        break;
    default:
        r = floatx80_sqrt(a, &env->fp_status);
        break;
    }
    /* a denormal dividend divided by zero is not used: #Z only (i5-13600K) */
    if (!(op == X87T_OP_DIV && ft0 == 1 && floatx80_is_zero(b))) {
        x87_ft0_denormal(env, ft0, ft0 == 1 ? b : a);
    }
    f = get_float_exception_flags(&env->fp_status);
    if (x87_cancel_unmasked(env, old_flags, f)) {
        return false;
    }
    if (!(f & (float_flag_invalid | float_flag_divbyzero)) &&
        ((f & (float_flag_overflow | float_flag_underflow)) ||
         (extractFloatx80Exp(r) == 0 && extractFloatx80Frac(r) != 0))) {
        /* overflow, underflow or a tiny result: exact value, SDM shaping */
        X87TOut o = { 0 };
        X87TVal v = x87t_arith(op, a, b);

        r = x87t_round_prec(v, 128, 'Z', prec_of_pc[(env->fpuc >> 8) & 3], (env->fpuc >> 10) & 3,
                            !x87_unmasked(env, FPUS_UE), !x87_unmasked(env, FPUS_OE), &o, false);
        set_floatx80_rounding_precision(floatx80_precision_x, &env->fp_status);
        set_float_exception_flags(old_flags, &env->fp_status);
        fpu_set_exception(env, (x87_fsw_bits(f) & FPUS_DE) | (o.flags & 0x3f));
        env->fpus = (env->fpus & ~FPUS_C1) | (o.c1 ? FPUS_C1 : 0);
        *res = r;
        return true;
    }
    x87_arith_end(env, old_flags);
    *res = r;
    return true;
}
#endif /* __Use_Original_Qemu (U28/U29/U45/U46/U47/U53/U54/U57) */

static void fpu_raise_exception(CPUX86State *env, uintptr_t retaddr)
{
    if (env->cr[0] & CR0_NE_MASK) {
        raise_exception_ra(env, EXCP10_COPR, retaddr);
    }
#if !defined(CONFIG_USER_ONLY)
    else {
        fpu_check_raise_ferr_irq(env);
    }
#endif
}

#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
void helper_flds_FT0(CPUX86State *env, uint32_t val)
{
    int old_flags = save_exception_flags(env);
    union {
        float32 f;
        uint32_t i;
    } u;

    u.i = val;
    FT0 = float32_to_floatx80(u.f, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fldl_FT0(CPUX86State *env, uint64_t val)
{
    int old_flags = save_exception_flags(env);
    union {
        float64 f;
        uint64_t i;
    } u;

    u.i = val;
    FT0 = float64_to_floatx80(u.f, &env->fp_status);
    merge_exception_flags(env, old_flags);
}
#else /* ours (U45/U47/U53/U54) */
/*
 * NoVmp (ledger U54): m32fp/m64fp operands of FADD...FCOM convert exactly with
 * an SNaN kept signalling and a denormal remembered (env->ft0_den), so the
 * operation reports IE/DE in the hardware's order: QNaN ST0 + SNaN m32
 * returns the QNaN (Vol1 Table 4-8), a NaN in ST0 hides the operand's #D.
 */
static floatx80 x87_exact_from_f32(uint32_t v)
{
    bool s = v >> 31;
    int e = (v >> 23) & 0xff;
    uint64_t f = v & 0x7fffff;

    if (e == 0xff) {
        return packFloatx80(s, 0x7fff, (1ULL << 63) | (f << 40));
    }
    if (e == 0) {
        int sh;
        if (!f) {
            return packFloatx80(s, 0, 0);
        }
        sh = clz64(f);
        return packFloatx80(s, 16383 - 126 - (sh - 40), f << sh);
    }
    return packFloatx80(s, e - 127 + 16383, (1ULL << 63) | (f << 40));
}

static floatx80 x87_exact_from_f64(uint64_t v)
{
    bool s = v >> 63;
    int e = (v >> 52) & 0x7ff;
    uint64_t f = v & 0xfffffffffffffULL;

    if (e == 0x7ff) {
        return packFloatx80(s, 0x7fff, (1ULL << 63) | (f << 11));
    }
    if (e == 0) {
        int sh;
        if (!f) {
            return packFloatx80(s, 0, 0);
        }
        sh = clz64(f);
        return packFloatx80(s, 16383 - 1022 - (sh - 11), f << sh);
    }
    return packFloatx80(s, e - 1023 + 16383, (1ULL << 63) | (f << 11));
}

void helper_flds_FT0(CPUX86State *env, uint32_t val)
{
    FT0 = x87_exact_from_f32(val);
    env->ft0_den = ((val >> 23) & 0xff) == 0 && (val & 0x7fffff) != 0;
}

void helper_fldl_FT0(CPUX86State *env, uint64_t val)
{
    FT0 = x87_exact_from_f64(val);
    env->ft0_den = ((val >> 52) & 0x7ff) == 0 && (val & 0xfffffffffffffULL) != 0;
}
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */

void helper_fildl_FT0(CPUX86State *env, int32_t val)
{
    FT0 = int32_to_floatx80(val, &env->fp_status);
#if __Use_Original_Qemu != 1 /* ours (U54) */
    env->ft0_den = 0;
#endif /* __Use_Original_Qemu (U54) */
}

void helper_flds_ST0(CPUX86State *env, uint32_t val)
{
    int old_flags = save_exception_flags(env);
    int new_fpstt;
    union {
        float32 f;
        uint32_t i;
    } u;

    new_fpstt = (env->fpstt - 1) & 7;
    u.i = val;
#if __Use_Original_Qemu == 1 /* original QEMU (U54) */
    env->fpregs[new_fpstt].d = float32_to_floatx80(u.f, &env->fp_status);
#else /* ours (U54) */
    {
        floatx80 r = float32_to_floatx80(u.f, &env->fp_status);
        /* NoVmp (U54): SNaN + unmasked IE: nothing pushed (DE unmasked still loads) */
        if (x87_cancel_unmasked(env, old_flags,
                                get_float_exception_flags(&env->fp_status) & float_flag_invalid)) {
            return;
        }
        env->fpregs[new_fpstt].d = r;
    }
#endif /* __Use_Original_Qemu (U54) */
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
    merge_exception_flags(env, old_flags);
}

void helper_fldl_ST0(CPUX86State *env, uint64_t val)
{
    int old_flags = save_exception_flags(env);
    int new_fpstt;
    union {
        float64 f;
        uint64_t i;
    } u;

    new_fpstt = (env->fpstt - 1) & 7;
    u.i = val;
#if __Use_Original_Qemu == 1 /* original QEMU (U54) */
    env->fpregs[new_fpstt].d = float64_to_floatx80(u.f, &env->fp_status);
#else /* ours (U54) */
    {
        floatx80 r = float64_to_floatx80(u.f, &env->fp_status);
        /* NoVmp (U54): SNaN + unmasked IE: nothing pushed (DE unmasked still loads) */
        if (x87_cancel_unmasked(env, old_flags,
                                get_float_exception_flags(&env->fp_status) & float_flag_invalid)) {
            return;
        }
        env->fpregs[new_fpstt].d = r;
    }
#endif /* __Use_Original_Qemu (U54) */
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
    merge_exception_flags(env, old_flags);
}

static FloatX80RoundPrec tmp_maximise_precision(float_status *st)
{
    FloatX80RoundPrec old = get_floatx80_rounding_precision(st);
    set_floatx80_rounding_precision(floatx80_precision_x, st);
    return old;
}

void helper_fildl_ST0(CPUX86State *env, int32_t val)
{
    int new_fpstt;
    FloatX80RoundPrec old = tmp_maximise_precision(&env->fp_status);

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = int32_to_floatx80(val, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */

    set_floatx80_rounding_precision(old, &env->fp_status);
}

void helper_fildll_ST0(CPUX86State *env, int64_t val)
{
    int new_fpstt;
    FloatX80RoundPrec old = tmp_maximise_precision(&env->fp_status);

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = int64_to_floatx80(val, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */

    set_floatx80_rounding_precision(old, &env->fp_status);
}

uint32_t helper_fsts_ST0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    union {
        float32 f;
        uint32_t i;
    } u;

    u.f = floatx80_to_float32(ST0, &env->fp_status);
#if __Use_Original_Qemu != 1 /* ours (U29) */
    /* FST m32fp never signals #D (SDM); it is reported as #U instead */
    set_float_exception_flags(get_float_exception_flags(&env->fp_status) &
                              ~float_flag_input_denormal_used,
                              &env->fp_status);
#endif /* __Use_Original_Qemu (U29) */
    merge_exception_flags(env, old_flags);
    return u.i;
}

uint64_t helper_fstl_ST0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    union {
        float64 f;
        uint64_t i;
    } u;

    u.f = floatx80_to_float64(ST0, &env->fp_status);
#if __Use_Original_Qemu != 1 /* ours (U29) */
    /* FST m64fp never signals #D (SDM); it is reported as #U instead */
    set_float_exception_flags(get_float_exception_flags(&env->fp_status) &
                              ~float_flag_input_denormal_used,
                              &env->fp_status);
#endif /* __Use_Original_Qemu (U29) */
    merge_exception_flags(env, old_flags);
    return u.i;
}

int32_t helper_fist_ST0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32(ST0, &env->fp_status);
    if (val != (int16_t)val) {
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        val = -32768;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fistl_ST0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x80000000;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int64_t helper_fistll_ST0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    int64_t val;

    val = floatx80_to_int64(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x8000000000000000ULL;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fistt_ST0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32_round_to_zero(ST0, &env->fp_status);
    if (val != (int16_t)val) {
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        val = -32768;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fisttl_ST0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32_round_to_zero(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x80000000;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int64_t helper_fisttll_ST0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    int64_t val;

    val = floatx80_to_int64_round_to_zero(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x8000000000000000ULL;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

#if __Use_Original_Qemu != 1 /* ours (U45/U46/U47/U53/U54) */
/*
 * NoVmp (ledger U54): FST/FSTP m32fp/m64fp, FIST/FISTP m16/m32/m64int and
 * FISTTP with the SDM Vol1 8.5 memory-destination responses: an unmasked
 * #IA, #O or #U stores nothing and cancels the pop; with #O/#U the PE flag
 * is not reported and C1 is cleared (8.5.6). #U is a tiny result even when
 * exact (i5-13600K: 2^-149 to m32fp with UM = 0).
 */
void helper_x87_store(CPUX86State *env, target_ulong a0, uint32_t kind)
{
    uintptr_t ra = GETPC();
    float_status *s = &env->fp_status;
    int old_flags = save_exception_flags(env), f;
    uint64_t v;
    bool fp = kind <= X87ST_F64, tiny = false;

    switch (kind) {
    case X87ST_F32: {
        float32 r = floatx80_to_float32(ST0, s);
        v = float32_val(r);
        tiny = float32_is_denormal(r);
        break;
    }
    case X87ST_F64: {
        float64 r = floatx80_to_float64(ST0, s);
        v = float64_val(r);
        tiny = float64_is_denormal(r);
        break;
    }
    case X87ST_I16:
    case X87ST_T16: {
        int32_t val = kind == X87ST_I16 ? floatx80_to_int32(ST0, s)
                                        : floatx80_to_int32_round_to_zero(ST0, s);
        if (val != (int16_t)val) {
            set_float_exception_flags(float_flag_invalid, s);
            val = -32768;
        }
        v = (uint16_t)val;
        break;
    }
    case X87ST_I32:
    case X87ST_T32: {
        int32_t val = kind == X87ST_I32 ? floatx80_to_int32(ST0, s)
                                        : floatx80_to_int32_round_to_zero(ST0, s);
        if (get_float_exception_flags(s) & float_flag_invalid) {
            val = (int32_t)0x80000000;
        }
        v = (uint32_t)val;
        break;
    }
    default: {
        int64_t val = kind == X87ST_I64 ? floatx80_to_int64(ST0, s)
                                        : floatx80_to_int64_round_to_zero(ST0, s);
        if (get_float_exception_flags(s) & float_flag_invalid) {
            val = (int64_t)0x8000000000000000ULL;
        }
        v = (uint64_t)val;
        break;
    }
    }
    f = get_float_exception_flags(s);
    if (fp) {
        /* FST m32fp/m64fp never signal #D (SDM); a tiny result is #U */
        f &= ~float_flag_input_denormal_used;
        set_float_exception_flags(f, s);
        if (f & float_flag_underflow) {
            tiny = true;
        }
    }
    if (x87_cancel_unmasked(env, old_flags, f & float_flag_invalid)) {
        return;                     /* unmasked #IA: nothing stored, no pop */
    }
    if (fp && (((f & float_flag_overflow) && x87_unmasked(env, FPUS_OE)) ||
               (tiny && x87_unmasked(env, FPUS_UE)))) {
        set_float_exception_flags(old_flags, s);
        env->fpus &= ~FPUS_C1;
        fpu_set_exception(env, (f & float_flag_overflow) ? FPUS_OE : FPUS_UE);
        env->x87_nopop = 1;
        return;
    }
    /*
     * NoVmp (ledger U592): the store is made here, inside the helper, so a page fault on it -
     * or memory Unicorn has not mapped, which only requests an exit - left the FPU flags set,
     * the pop and the FIP/FDP update done (and a page-crossing operand half written).
     * i5-13600K: #PF with nothing stored and TOP/FSW/FIP unchanged (cases_fixes2). The
     * destination is probed before anything changes.
     */
    {
        uint64_t img = cpu_to_le64(v);
        uint32_t len = (kind == X87ST_I16 || kind == X87ST_T16) ? 2 :
                       (kind == X87ST_F32 || kind == X87ST_I32 || kind == X87ST_T32) ? 4 : 8;

        int f_now = get_float_exception_flags(s);

        set_float_exception_flags(old_flags, s);   /* as on entry if the probe faults */
        x86_probe_store(env, a0, len, (const uint8_t *)&img, ra);
        set_float_exception_flags(f_now, s);
    }
    merge_exception_flags(env, old_flags);
    switch (kind) {
    case X87ST_F32:
    case X87ST_I32:
    case X87ST_T32:
        cpu_stl_data_ra(env, a0, (uint32_t)v, ra);
        break;
    case X87ST_I16:
    case X87ST_T16:
        cpu_stw_data_ra(env, a0, (uint16_t)v, ra);
        break;
    default:
        cpu_stq_data_ra(env, a0, v, ra);
        break;
    }
}

#endif /* __Use_Original_Qemu (U45/U46/U47/U53/U54) */
void helper_fldt_ST0(CPUX86State *env, target_ulong ptr)
{
    int new_fpstt;

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = do_fldt(env, ptr, GETPC());
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fstt_ST0(CPUX86State *env, target_ulong ptr)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U480) */
    /* backport d3e8b648ab: nothing is stored when any byte faults */
    x86_access_prepare(env, ptr, 10, MMU_DATA_STORE, GETPC());
    do_fstt(env, ST0, ptr, GETPC());
#else /* ours (U480) */
    /*
     * NoVmp (ledger U480): the SDM does not say whether FST/FSTP m80 may store part of its
     * operand. The i5-13600K stores bits 63:0 and then bits 79:64 (emu-alltest
     * cases_backport_t2: at a page end the low qword is written before the #PF on the last
     * two bytes; with the qword itself crossing nothing is written), so each of the two
     * stores is prepared on its own instead of upstream's whole-operand probe.
     */
    CPU_LDoubleU temp;

    temp.d = ST0;
    x86_access_prepare(env, ptr, 8, MMU_DATA_STORE, GETPC());
    cpu_stq_data_ra(env, ptr, temp.l.lower, GETPC());
    x86_access_prepare(env, ptr + 8, 2, MMU_DATA_STORE, GETPC());
    cpu_stw_data_ra(env, ptr + 8, temp.l.upper, GETPC());
#endif /* __Use_Original_Qemu (U480) */
}

void helper_fpush(CPUX86State *env)
{
    fpush(env);
}

void helper_fpop(CPUX86State *env)
{
#if __Use_Original_Qemu != 1 /* ours (U54) */
    if (env->x87_nopop) {
        return;             /* unmasked #IA/#D/#Z: TOP unchanged (U54) */
    }
#endif /* __Use_Original_Qemu (U54) */
    fpop(env);
}

void helper_fdecstp(CPUX86State *env)
{
    env->fpstt = (env->fpstt - 1) & 7;
#if __Use_Original_Qemu == 1 /* original QEMU (U54) */
    env->fpus &= ~0x4700;
#else /* ours (U54) */
    env->fpus &= ~FPUS_C1;     /* C1 cleared; C0, C2, C3 kept (i5-13600K) */
#endif /* __Use_Original_Qemu (U54) */
}

void helper_fincstp(CPUX86State *env)
{
    env->fpstt = (env->fpstt + 1) & 7;
#if __Use_Original_Qemu == 1 /* original QEMU (U54) */
    env->fpus &= ~0x4700;
#else /* ours (U54) */
    env->fpus &= ~FPUS_C1;     /* C1 cleared; C0, C2, C3 kept (i5-13600K) */
#endif /* __Use_Original_Qemu (U54) */
}

/* FPU move */

void helper_ffree_STN(CPUX86State *env, int st_index)
{
    env->fptags[(env->fpstt + st_index) & 7] = 1;
}

void helper_fmov_ST0_FT0(CPUX86State *env)
{
    ST0 = FT0;
}

void helper_fmov_FT0_STN(CPUX86State *env, int st_index)
{
    FT0 = ST(st_index);
#if __Use_Original_Qemu != 1 /* ours (U54) */
    env->ft0_den = 0;
#endif /* __Use_Original_Qemu (U54) */
}

void helper_fmov_ST0_STN(CPUX86State *env, int st_index)
{
    ST0 = ST(st_index);
}

void helper_fmov_STN_ST0(CPUX86State *env, int st_index)
{
    ST(st_index) = ST0;
    env->fptags[(env->fpstt + st_index) & 7] = 0;
}

void helper_fxchg_ST0_STN(CPUX86State *env, int st_index)
{
    floatx80 tmp;

    tmp = ST(st_index);
    ST(st_index) = ST0;
    ST0 = tmp;

    env->fptags[env->fpstt] = 0;
    env->fptags[(env->fpstt + st_index) & 7] = 0;

    /* C1 is unconditionally cleared to 0 */
    env->fpus &= ~0x0200;
}

/* FPU operations */

static const int fcom_ccval[4] = {0x0100, 0x4000, 0x0000, 0x4500};

#if __Use_Original_Qemu != 1 /* ours (U47/U54) */
/*
 * NoVmp (ledger U54): end of FCOM/FUCOM/FCOMI/FUCOMI (all forms). FT0's #D
 * counts unless ST0 is a NaN; an unmasked #IA/#D still writes the condition
 * codes / EFLAGS but cancels the pops of FCOMP/FCOMPP/FUCOMPP/FCOMIP/FUCOMIP
 * (i5-13600K). Returns the new softfloat flags.
 */
static int x87_compare_flags(CPUX86State *env)
{
    int f;

    x87_ft0_denormal(env, 2, ST0);
    f = get_float_exception_flags(&env->fp_status);
    if (x87_unmasked(env, x87_fsw_bits(f) & (FPUS_IE | FPUS_DE))) {
        env->x87_nopop = 1;
    }
    return f;
}

static void x87_compare_end(CPUX86State *env, int old_flags)
{
    x87_compare_flags(env);
    merge_exception_flags(env, old_flags);
}

#endif /* __Use_Original_Qemu (U47/U54) */
#if __Use_Original_Qemu != 1 /* ours (U431) */
/*
 * NoVmp (ledger U431): FCOM/FCOMP/FCOMPP, FUCOM/FUCOMP/FUCOMPP (SDM Vol2A: "if the
 * operation results in an invalid-arithmetic-operand exception being raised, the
 * condition code flags are set only if the exception is masked"; Operation: IF
 * FPUControlWord.IM = 1 THEN C3, C2, C0 := 111) and FCOMI/FCOMIP/FUCOMI/FUCOMIP
 * ("the status flags in the EFLAGS register are set only if the exception is
 * masked"; OF/SF/AF are cleared regardless). True when this result must leave
 * them unchanged: #IA raised and FCW.IM = 0 (U537: the only behaviour; the
 * i5-13600K writes "unordered" anyway, documented in docs/quirks.md). FTST and
 * FICOM/FICOMP have no such condition (always "unordered").
 */
static bool x87_cmp_cc_kept(CPUX86State *env, int f)
{
    return (f & float_flag_invalid) && !(env->fpuc & 0x0001);
}

/* FSW condition codes of FCOM/FUCOM (im_rule) or FTST/FICOM (!im_rule), C1 = 0 */
static void x87_compare_cc(CPUX86State *env, FloatRelation ret, int old_flags,
                           bool im_rule)
{
    uint16_t cc_old = env->fpus & 0x4500;
    int f;

    env->fpus = (env->fpus & ~0x4700) | fcom_ccval[ret + 1];
    f = x87_compare_flags(env);
    merge_exception_flags(env, old_flags);
    if (im_rule && x87_cmp_cc_kept(env, f)) {
        env->fpus = (env->fpus & ~0x4500) | cc_old;
    }
}

/* FTST, FICOM/FICOMP: "unordered" regardless of FCW.IM (SDM Operation) */
void helper_fcom_unord_ST0_FT0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
    x87_compare_cc(env, ret, old_flags, false);
}

#endif /* __Use_Original_Qemu (U431) */
void helper_fcom_ST0_FT0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
    /* C1 is unconditionally cleared to 0 */
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54/U431) */
    env->fpus = (env->fpus & ~0x4700) | fcom_ccval[ret + 1];
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54/U431) */
    x87_compare_cc(env, ret, old_flags, true);
#endif /* __Use_Original_Qemu (U45/U47/U53/U54/U431) */
}

void helper_fucom_ST0_FT0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare_quiet(ST0, FT0, &env->fp_status);
    /* C1 is unconditionally cleared to 0 */
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54/U431) */
    env->fpus = (env->fpus & ~0x4700) | fcom_ccval[ret + 1];
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54/U431) */
    x87_compare_cc(env, ret, old_flags, true);
#endif /* __Use_Original_Qemu (U45/U47/U53/U54/U431) */
}

static const int fcomi_ccval[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void helper_fcomi_ST0_FT0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
#if __Use_Original_Qemu == 1 /* original QEMU (U54/U431) */
    /* OF, SF, and AF are unconditionally cleared to 0 */
    CC_SRC = fcomi_ccval[ret + 1];
#else /* ours (U54/U431) */
    {
        /* U431: ZF/PF/CF stay unchanged on an unmasked #IA (OF/SF/AF still 0) */
        uint32_t zpc_old = cpu_cc_compute_all(env, CC_OP) & (CC_Z | CC_P | CC_C);
        int f = x87_compare_flags(env);

        CC_SRC = x87_cmp_cc_kept(env, f) ? zpc_old : fcomi_ccval[ret + 1];
    }
#endif /* __Use_Original_Qemu (U54/U431) */
    /* C1 is unconditionally cleared to 0 */
    env->fpus &= ~0x0200;
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54) */
    merge_exception_flags_ex(env, old_flags, false);
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */
}

void helper_fucomi_ST0_FT0(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare_quiet(ST0, FT0, &env->fp_status);
#if __Use_Original_Qemu == 1 /* original QEMU (U54/U431) */
    /* OF, SF, and AF are unconditionally cleared to 0 */
    CC_SRC = fcomi_ccval[ret + 1];
#else /* ours (U54/U431) */
    {
        /* U431: see helper_fcomi_ST0_FT0 */
        uint32_t zpc_old = cpu_cc_compute_all(env, CC_OP) & (CC_Z | CC_P | CC_C);
        int f = x87_compare_flags(env);

        CC_SRC = x87_cmp_cc_kept(env, f) ? zpc_old : fcomi_ccval[ret + 1];
    }
#endif /* __Use_Original_Qemu (U54/U431) */
    /* C1 is unconditionally cleared to 0 */
    env->fpus &= ~0x0200;
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54) */
    merge_exception_flags_ex(env, old_flags, false);
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */
}

void helper_fadd_ST0_FT0(CPUX86State *env)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
    int old_flags = save_exception_flags(env);
    ST0 = floatx80_add(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54) */
    floatx80 r;
    if (x87_arith2_ft0(env, X87T_OP_ADD, ST0, FT0, &r, 2)) {
        ST0 = r;
    }
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */
}

void helper_fmul_ST0_FT0(CPUX86State *env)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
    int old_flags = save_exception_flags(env);
    ST0 = floatx80_mul(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54) */
    floatx80 r;
    if (x87_arith2_ft0(env, X87T_OP_MUL, ST0, FT0, &r, 2)) {
        ST0 = r;
    }
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */
}

void helper_fsub_ST0_FT0(CPUX86State *env)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
    int old_flags = save_exception_flags(env);
    ST0 = floatx80_sub(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54) */
    floatx80 r;
    if (x87_arith2_ft0(env, X87T_OP_SUB, ST0, FT0, &r, 2)) {
        ST0 = r;
    }
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */
}

void helper_fsubr_ST0_FT0(CPUX86State *env)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
    int old_flags = save_exception_flags(env);
    ST0 = floatx80_sub(FT0, ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54) */
    floatx80 r;
    if (x87_arith2_ft0(env, X87T_OP_SUB, FT0, ST0, &r, 1)) {
        ST0 = r;
    }
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */
}

void helper_fdiv_ST0_FT0(CPUX86State *env)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U54) */
    ST0 = helper_fdiv(env, ST0, FT0);
#else /* ours (U54) */
    floatx80 r;
    if (x87_arith2_ft0(env, X87T_OP_DIV, ST0, FT0, &r, 2)) {
        ST0 = r;
    }
#endif /* __Use_Original_Qemu (U54) */
}

void helper_fdivr_ST0_FT0(CPUX86State *env)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U54) */
    ST0 = helper_fdiv(env, FT0, ST0);
#else /* ours (U54) */
    floatx80 r;
    if (x87_arith2_ft0(env, X87T_OP_DIV, FT0, ST0, &r, 1)) {
        ST0 = r;
    }
#endif /* __Use_Original_Qemu (U54) */
}

/* fp operations between STN and ST0 */

void helper_fadd_STN_ST0(CPUX86State *env, int st_index)
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U46/U47/U53/U54) */
{
    int old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_add(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fmul_STN_ST0(CPUX86State *env, int st_index)
{
    int old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_mul(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsub_STN_ST0(CPUX86State *env, int st_index)
{
    int old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_sub(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsubr_STN_ST0(CPUX86State *env, int st_index)
{
    int old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_sub(ST0, ST(st_index), &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fdiv_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 *p;

    p = &ST(st_index);
    *p = helper_fdiv(env, *p, ST0);
}

void helper_fdivr_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 *p;

    p = &ST(st_index);
    *p = helper_fdiv(env, ST0, *p);
}

/* misc FPU operations */
void helper_fchs_ST0(CPUX86State *env)
{
    ST0 = floatx80_chs(ST0);
}
#else /* ours (U45/U46/U47/U53/U54) */
{
    floatx80 r;
    if (x87_arith2(env, X87T_OP_ADD, ST(st_index), ST0, &r)) {
        ST(st_index) = r;
    }
}

void helper_fmul_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 r;
    if (x87_arith2(env, X87T_OP_MUL, ST(st_index), ST0, &r)) {
        ST(st_index) = r;
    }
}

void helper_fsub_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 r;
    if (x87_arith2(env, X87T_OP_SUB, ST(st_index), ST0, &r)) {
        ST(st_index) = r;
    }
}

void helper_fsubr_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 r;
    if (x87_arith2(env, X87T_OP_SUB, ST0, ST(st_index), &r)) {
        ST(st_index) = r;
    }
}

void helper_fdiv_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 r;
    if (x87_arith2(env, X87T_OP_DIV, ST(st_index), ST0, &r)) {
        ST(st_index) = r;
    }
}

void helper_fdivr_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 r;
    if (x87_arith2(env, X87T_OP_DIV, ST0, ST(st_index), &r)) {
        ST(st_index) = r;
    }
}

/* misc FPU operations */
/* FCHS/FABS: "C1 Set to 0" (SDM Vol2; NoVmp ledger U45) */
void helper_fchs_ST0(CPUX86State *env)
{
    ST0 = floatx80_chs(ST0);
    env->fpus &= ~FPUS_C1;
}
#endif /* __Use_Original_Qemu (U45/U46/U47/U53/U54) */

void helper_fabs_ST0(CPUX86State *env)
{
    ST0 = floatx80_abs(ST0);
#if __Use_Original_Qemu != 1 /* ours (U45/U46/U54) */
    env->fpus &= ~FPUS_C1;
#endif /* __Use_Original_Qemu (U45/U46/U54) */
}

void helper_fld1_ST0(CPUX86State *env)
{
    ST0 = floatx80_one;
}

void helper_fldl2t_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_UP:
        ST0 = floatx80_l2t_u;
        break;
    default:
        ST0 = floatx80_l2t;
        break;
    }
}

void helper_fldl2e_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_l2e_d;
        break;
    default:
        ST0 = floatx80_l2e;
        break;
    }
}

void helper_fldpi_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_pi_d;
        break;
    default:
        ST0 = floatx80_pi;
        break;
    }
}

void helper_fldlg2_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_lg2_d;
        break;
    default:
        ST0 = floatx80_lg2;
        break;
    }
}

void helper_fldln2_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_ln2_d;
        break;
    default:
        ST0 = floatx80_ln2;
        break;
    }
}

void helper_fldz_ST0(CPUX86State *env)
{
    ST0 = floatx80_zero;
}

void helper_fldz_FT0(CPUX86State *env)
{
    FT0 = floatx80_zero;
#if __Use_Original_Qemu != 1 /* ours (U54) */
    env->ft0_den = 0;
#endif /* __Use_Original_Qemu (U54) */
}

uint32_t helper_fnstsw(CPUX86State *env)
{
    return (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
}

uint32_t helper_fnstcw(CPUX86State *env)
{
    return env->fpuc;
}

static void set_x86_rounding_mode(unsigned mode, float_status *status)
{
    static FloatRoundMode x86_round_mode[4] = {
        float_round_nearest_even,
        float_round_down,
        float_round_up,
        float_round_to_zero
    };
    assert(mode < ARRAY_SIZE(x86_round_mode));
    set_float_rounding_mode(x86_round_mode[mode], status);
}

void update_fp_status(CPUX86State *env)
{
    int rnd_mode;
    FloatX80RoundPrec rnd_prec;

    /* set rounding mode */
    rnd_mode = (env->fpuc & FPU_RC_MASK) >> FPU_RC_SHIFT;
    set_x86_rounding_mode(rnd_mode, &env->fp_status);

#if __Use_Original_Qemu == 1 /* original QEMU (U47/U53) */
    switch ((env->fpuc >> 8) & 3) {
    case 0:
        rnd_prec = floatx80_precision_s;
        break;
    case 2:
        rnd_prec = floatx80_precision_d;
        break;
    case 3:
    default:
        rnd_prec = floatx80_precision_x;
        break;
    }
#else /* ours (U47/U53) */
    /* FCW.PC is applied per instruction by x87_arith_begin (U47) */
    rnd_prec = floatx80_precision_x;
#endif /* __Use_Original_Qemu (U47/U53) */
    set_floatx80_rounding_precision(rnd_prec, &env->fp_status);
}

void helper_fldcw(CPUX86State *env, uint32_t val)
{
    cpu_set_fpuc(env, val);
}

void helper_fclex(CPUX86State *env)
{
    env->fpus &= 0x7f00;
}

void helper_fwait(CPUX86State *env)
{
    if (env->fpus & FPUS_SE) {
        fpu_raise_exception(env, GETPC());
    }
}

static void do_fninit(CPUX86State *env)
{
    env->fpus = 0;
    env->fpstt = 0;
    env->fpcs = 0;
    env->fpds = 0;
    env->fpip = 0;
    env->fpdp = 0;
#if __Use_Original_Qemu != 1 /* ours (U64) */
    env->fpop = 0;
#endif /* __Use_Original_Qemu (U64) */
    cpu_set_fpuc(env, 0x37f);
    env->fptags[0] = 1;
    env->fptags[1] = 1;
    env->fptags[2] = 1;
    env->fptags[3] = 1;
    env->fptags[4] = 1;
    env->fptags[5] = 1;
    env->fptags[6] = 1;
    env->fptags[7] = 1;
}

void helper_fninit(CPUX86State *env)
{
    do_fninit(env);
}

/* BCD ops */

void helper_fbld_ST0(CPUX86State *env, target_ulong ptr)
{
    floatx80 tmp;
    uint64_t val;
    unsigned int v;
    int i;
#if __Use_Original_Qemu == 1 /* original QEMU (U706) */
    val = 0;
    for (i = 8; i >= 0; i--) {
        v = cpu_ldub_data_ra(env, ptr + i, GETPC());
        val = (val * 100) + ((v >> 4) * 10) + (v & 0xf);
    }
    tmp = int64_to_floatx80(val, &env->fp_status);
    if (cpu_ldub_data_ra(env, ptr + 9, GETPC()) & 0x80) {
        tmp = floatx80_chs(tmp);
    }
#else /* ours (U706) */
    /*
     * U706: the 10-byte operand is read as bytes 7:0 and then bytes 9:8 (as FLD m80): an
     * operand across a page boundary faults on its lower part first (i5-13600K: FBLD across
     * 0000_7FFF_FFFF_FFFFh is #PF on the not-present canonical page, not #GP). QEMU read the
     * bytes from the highest one down.
     */
    uintptr_t ra = GETPC();
    uint64_t lo = cpu_ldq_data_ra(env, ptr, ra);
    uint16_t hi = cpu_lduw_data_ra(env, ptr + 8, ra);

    val = 0;
    for (i = 8; i >= 0; i--) {
        v = i < 8 ? (uint8_t)(lo >> (8 * i)) : (uint8_t)hi;
        val = (val * 100) + ((v >> 4) * 10) + (v & 0xf);
    }
    tmp = int64_to_floatx80(val, &env->fp_status);
    if ((hi >> 8) & 0x80) {
        tmp = floatx80_chs(tmp);
    }
#endif /* __Use_Original_Qemu (U706) */
    fpush(env);
    ST0 = tmp;
}

#if __Use_Original_Qemu != 1 /* ours (U480) */
/* FBSTP: store the 10-byte image as bytes 7:0, then bytes 9:8 (see helper_fbst_ST0) */
static void fbst_commit(CPUX86State *env, target_ulong ptr, const uint8_t *img,
                        uintptr_t ra)
{
    x86_access_prepare(env, ptr, 8, MMU_DATA_STORE, ra);
    cpu_stq_data_ra(env, ptr, ldq_le_p(img), ra);
    x86_access_prepare(env, ptr + 8, 2, MMU_DATA_STORE, ra);
    cpu_stw_data_ra(env, ptr + 8, lduw_le_p(img + 8), ra);
}
#endif /* __Use_Original_Qemu (U480) */

void helper_fbst_ST0(CPUX86State *env, target_ulong ptr)
{
    int old_flags = save_exception_flags(env);
    int v;
    target_ulong mem_ref, mem_end;
    int64_t val;
    CPU_LDoubleU temp;

#if __Use_Original_Qemu == 1 /* original QEMU (U480) */
    /* backport 4526f58a27: nothing is stored when any byte faults */
    x86_access_prepare(env, ptr, 10, MMU_DATA_STORE, GETPC());
#define FBST_PUT(a, v) cpu_stb_data_ra(env, (a), (v), GETPC())
#else /* ours (U480) */
    /*
     * NoVmp (ledger U480): the packed BCD image is built first and stored like FSTP m80:
     * bytes 7:0, then bytes 9:8 (i5-13600K: at a page end the low qword is written before
     * the #PF on bytes 9:8; the SDM does not say)
     */
    uint8_t img[10];
#define FBST_PUT(a, v) (img[(a) - ptr] = (uint8_t)(v))
#endif /* __Use_Original_Qemu (U480) */

    temp.d = ST0;

    val = floatx80_to_int64(ST0, &env->fp_status);
    mem_ref = ptr;
    if (val >= 1000000000000000000LL || val <= -1000000000000000000LL) {
#if __Use_Original_Qemu != 1 /* ours (U54) */
        /* NoVmp (U54): unmasked #IA stores nothing, no pop (SDM Vol1 8.5.1.2) */
        if (x87_cancel_unmasked(env, old_flags, float_flag_invalid)) {
            return;
        }
#endif /* __Use_Original_Qemu (U54) */
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        while (mem_ref < ptr + 7) {
            FBST_PUT(mem_ref++, 0);
        }
        FBST_PUT(mem_ref++, 0xc0);
        FBST_PUT(mem_ref++, 0xff);
        FBST_PUT(mem_ref++, 0xff);
#if __Use_Original_Qemu != 1 /* ours (U480) */
        fbst_commit(env, ptr, img, GETPC());
#endif /* __Use_Original_Qemu (U480) */
        merge_exception_flags(env, old_flags);
        return;
    }
    mem_end = mem_ref + 9;
    if (SIGND(temp)) {
        FBST_PUT(mem_end, 0x80);
        val = -val;
    } else {
        FBST_PUT(mem_end, 0x00);
    }
    while (mem_ref < mem_end) {
        if (val == 0) {
            break;
        }
        v = val % 100;
        val = val / 100;
        v = ((v / 10) << 4) | (v % 10);
        FBST_PUT(mem_ref++, v);
    }
    while (mem_ref < mem_end) {
        FBST_PUT(mem_ref++, 0);
    }
#if __Use_Original_Qemu != 1 /* ours (U480) */
    fbst_commit(env, ptr, img, GETPC());
#endif /* __Use_Original_Qemu (U480) */
    merge_exception_flags(env, old_flags);
}
#undef FBST_PUT

/* 128-bit significand of log(2).  */
#define ln2_sig_high 0xb17217f7d1cf79abULL
#define ln2_sig_low 0xc9e3b39803f2f6afULL

/*
 * Polynomial coefficients for an approximation to (2^x - 1) / x, on
 * the interval [-1/64, 1/64].
 */
#define f2xm1_coeff_0 make_floatx80(0x3ffe, 0xb17217f7d1cf79acULL)
#define f2xm1_coeff_0_low make_floatx80(0xbfbc, 0xd87edabf495b3762ULL)
#define f2xm1_coeff_1 make_floatx80(0x3ffc, 0xf5fdeffc162c7543ULL)
#define f2xm1_coeff_2 make_floatx80(0x3ffa, 0xe35846b82505fcc7ULL)
#define f2xm1_coeff_3 make_floatx80(0x3ff8, 0x9d955b7dd273b899ULL)
#define f2xm1_coeff_4 make_floatx80(0x3ff5, 0xaec3ff3c4ef4ac0cULL)
#define f2xm1_coeff_5 make_floatx80(0x3ff2, 0xa184897c3a7f0de9ULL)
#define f2xm1_coeff_6 make_floatx80(0x3fee, 0xffe634d0ec30d504ULL)
#define f2xm1_coeff_7 make_floatx80(0x3feb, 0xb160111d2db515e4ULL)

struct f2xm1_data {
    /*
     * A value very close to a multiple of 1/32, such that 2^t and 2^t - 1
     * are very close to exact floatx80 values.
     */
    floatx80 t;
    /* The value of 2^t.  */
    floatx80 exp2;
    /* The value of 2^t - 1.  */
    floatx80 exp2m1;
};

static const struct f2xm1_data f2xm1_table[65] = {
    { make_floatx80_init(0xbfff, 0x8000000000000000ULL),
      make_floatx80_init(0x3ffe, 0x8000000000000000ULL),
      make_floatx80_init(0xbffe, 0x8000000000000000ULL) },
    { make_floatx80_init(0xbffe, 0xf800000000002e7eULL),
      make_floatx80_init(0x3ffe, 0x82cd8698ac2b9160ULL),
      make_floatx80_init(0xbffd, 0xfa64f2cea7a8dd40ULL) },
    { make_floatx80_init(0xbffe, 0xefffffffffffe960ULL),
      make_floatx80_init(0x3ffe, 0x85aac367cc488345ULL),
      make_floatx80_init(0xbffd, 0xf4aa7930676ef976ULL) },
    { make_floatx80_init(0xbffe, 0xe800000000006f10ULL),
      make_floatx80_init(0x3ffe, 0x88980e8092da5c14ULL),
      make_floatx80_init(0xbffd, 0xeecfe2feda4b47d8ULL) },
    { make_floatx80_init(0xbffe, 0xe000000000008a45ULL),
      make_floatx80_init(0x3ffe, 0x8b95c1e3ea8ba2a5ULL),
      make_floatx80_init(0xbffd, 0xe8d47c382ae8bab6ULL) },
    { make_floatx80_init(0xbffe, 0xd7ffffffffff8a9eULL),
      make_floatx80_init(0x3ffe, 0x8ea4398b45cd8116ULL),
      make_floatx80_init(0xbffd, 0xe2b78ce97464fdd4ULL) },
    { make_floatx80_init(0xbffe, 0xd0000000000019a0ULL),
      make_floatx80_init(0x3ffe, 0x91c3d373ab11b919ULL),
      make_floatx80_init(0xbffd, 0xdc785918a9dc8dceULL) },
    { make_floatx80_init(0xbffe, 0xc7ffffffffff14dfULL),
      make_floatx80_init(0x3ffe, 0x94f4efa8fef76836ULL),
      make_floatx80_init(0xbffd, 0xd61620ae02112f94ULL) },
    { make_floatx80_init(0xbffe, 0xc000000000006530ULL),
      make_floatx80_init(0x3ffe, 0x9837f0518db87fbbULL),
      make_floatx80_init(0xbffd, 0xcf901f5ce48f008aULL) },
    { make_floatx80_init(0xbffe, 0xb7ffffffffff1723ULL),
      make_floatx80_init(0x3ffe, 0x9b8d39b9d54eb74cULL),
      make_floatx80_init(0xbffd, 0xc8e58c8c55629168ULL) },
    { make_floatx80_init(0xbffe, 0xb00000000000b5e1ULL),
      make_floatx80_init(0x3ffe, 0x9ef5326091a0c366ULL),
      make_floatx80_init(0xbffd, 0xc2159b3edcbe7934ULL) },
    { make_floatx80_init(0xbffe, 0xa800000000006f8aULL),
      make_floatx80_init(0x3ffe, 0xa27043030c49370aULL),
      make_floatx80_init(0xbffd, 0xbb1f79f9e76d91ecULL) },
    { make_floatx80_init(0xbffe, 0x9fffffffffff816aULL),
      make_floatx80_init(0x3ffe, 0xa5fed6a9b15171cfULL),
      make_floatx80_init(0xbffd, 0xb40252ac9d5d1c62ULL) },
    { make_floatx80_init(0xbffe, 0x97ffffffffffb621ULL),
      make_floatx80_init(0x3ffe, 0xa9a15ab4ea7c30e6ULL),
      make_floatx80_init(0xbffd, 0xacbd4a962b079e34ULL) },
    { make_floatx80_init(0xbffe, 0x8fffffffffff162bULL),
      make_floatx80_init(0x3ffe, 0xad583eea42a1b886ULL),
      make_floatx80_init(0xbffd, 0xa54f822b7abc8ef4ULL) },
    { make_floatx80_init(0xbffe, 0x87ffffffffff4d34ULL),
      make_floatx80_init(0x3ffe, 0xb123f581d2ac7b51ULL),
      make_floatx80_init(0xbffd, 0x9db814fc5aa7095eULL) },
    { make_floatx80_init(0xbffe, 0x800000000000227dULL),
      make_floatx80_init(0x3ffe, 0xb504f333f9de539dULL),
      make_floatx80_init(0xbffd, 0x95f619980c4358c6ULL) },
    { make_floatx80_init(0xbffd, 0xefffffffffff3978ULL),
      make_floatx80_init(0x3ffe, 0xb8fbaf4762fbd0a1ULL),
      make_floatx80_init(0xbffd, 0x8e08a1713a085ebeULL) },
    { make_floatx80_init(0xbffd, 0xe00000000000df81ULL),
      make_floatx80_init(0x3ffe, 0xbd08a39f580bfd8cULL),
      make_floatx80_init(0xbffd, 0x85eeb8c14fe804e8ULL) },
    { make_floatx80_init(0xbffd, 0xd00000000000bccfULL),
      make_floatx80_init(0x3ffe, 0xc12c4cca667062f6ULL),
      make_floatx80_init(0xbffc, 0xfb4eccd6663e7428ULL) },
    { make_floatx80_init(0xbffd, 0xc00000000000eff0ULL),
      make_floatx80_init(0x3ffe, 0xc5672a1155069abeULL),
      make_floatx80_init(0xbffc, 0xea6357baabe59508ULL) },
    { make_floatx80_init(0xbffd, 0xb000000000000fe6ULL),
      make_floatx80_init(0x3ffe, 0xc9b9bd866e2f234bULL),
      make_floatx80_init(0xbffc, 0xd91909e6474372d4ULL) },
    { make_floatx80_init(0xbffd, 0x9fffffffffff2172ULL),
      make_floatx80_init(0x3ffe, 0xce248c151f84bf00ULL),
      make_floatx80_init(0xbffc, 0xc76dcfab81ed0400ULL) },
    { make_floatx80_init(0xbffd, 0x8fffffffffffafffULL),
      make_floatx80_init(0x3ffe, 0xd2a81d91f12afb2bULL),
      make_floatx80_init(0xbffc, 0xb55f89b83b541354ULL) },
    { make_floatx80_init(0xbffc, 0xffffffffffff81a3ULL),
      make_floatx80_init(0x3ffe, 0xd744fccad69d7d5eULL),
      make_floatx80_init(0xbffc, 0xa2ec0cd4a58a0a88ULL) },
    { make_floatx80_init(0xbffc, 0xdfffffffffff1568ULL),
      make_floatx80_init(0x3ffe, 0xdbfbb797daf25a44ULL),
      make_floatx80_init(0xbffc, 0x901121a0943696f0ULL) },
    { make_floatx80_init(0xbffc, 0xbfffffffffff68daULL),
      make_floatx80_init(0x3ffe, 0xe0ccdeec2a94f811ULL),
      make_floatx80_init(0xbffb, 0xf999089eab583f78ULL) },
    { make_floatx80_init(0xbffc, 0x9fffffffffff4690ULL),
      make_floatx80_init(0x3ffe, 0xe5b906e77c83657eULL),
      make_floatx80_init(0xbffb, 0xd237c8c41be4d410ULL) },
    { make_floatx80_init(0xbffb, 0xffffffffffff8aeeULL),
      make_floatx80_init(0x3ffe, 0xeac0c6e7dd24427cULL),
      make_floatx80_init(0xbffb, 0xa9f9c8c116ddec20ULL) },
    { make_floatx80_init(0xbffb, 0xbfffffffffff2d18ULL),
      make_floatx80_init(0x3ffe, 0xefe4b99bdcdb06ebULL),
      make_floatx80_init(0xbffb, 0x80da33211927c8a8ULL) },
    { make_floatx80_init(0xbffa, 0xffffffffffff8ccbULL),
      make_floatx80_init(0x3ffe, 0xf5257d152486d0f4ULL),
      make_floatx80_init(0xbffa, 0xada82eadb792f0c0ULL) },
    { make_floatx80_init(0xbff9, 0xffffffffffff11feULL),
      make_floatx80_init(0x3ffe, 0xfa83b2db722a0846ULL),
      make_floatx80_init(0xbff9, 0xaf89a491babef740ULL) },
    { floatx80_zero_init,
      make_floatx80_init(0x3fff, 0x8000000000000000ULL),
      floatx80_zero_init },
    { make_floatx80_init(0x3ff9, 0xffffffffffff2680ULL),
      make_floatx80_init(0x3fff, 0x82cd8698ac2b9f6fULL),
      make_floatx80_init(0x3ff9, 0xb361a62b0ae7dbc0ULL) },
    { make_floatx80_init(0x3ffb, 0x800000000000b500ULL),
      make_floatx80_init(0x3fff, 0x85aac367cc488345ULL),
      make_floatx80_init(0x3ffa, 0xb5586cf9891068a0ULL) },
    { make_floatx80_init(0x3ffb, 0xbfffffffffff4b67ULL),
      make_floatx80_init(0x3fff, 0x88980e8092da7cceULL),
      make_floatx80_init(0x3ffb, 0x8980e8092da7cce0ULL) },
    { make_floatx80_init(0x3ffb, 0xffffffffffffff57ULL),
      make_floatx80_init(0x3fff, 0x8b95c1e3ea8bd6dfULL),
      make_floatx80_init(0x3ffb, 0xb95c1e3ea8bd6df0ULL) },
    { make_floatx80_init(0x3ffc, 0x9fffffffffff811fULL),
      make_floatx80_init(0x3fff, 0x8ea4398b45cd4780ULL),
      make_floatx80_init(0x3ffb, 0xea4398b45cd47800ULL) },
    { make_floatx80_init(0x3ffc, 0xbfffffffffff9980ULL),
      make_floatx80_init(0x3fff, 0x91c3d373ab11b919ULL),
      make_floatx80_init(0x3ffc, 0x8e1e9b9d588dc8c8ULL) },
    { make_floatx80_init(0x3ffc, 0xdffffffffffff631ULL),
      make_floatx80_init(0x3fff, 0x94f4efa8fef70864ULL),
      make_floatx80_init(0x3ffc, 0xa7a77d47f7b84320ULL) },
    { make_floatx80_init(0x3ffc, 0xffffffffffff2499ULL),
      make_floatx80_init(0x3fff, 0x9837f0518db892d4ULL),
      make_floatx80_init(0x3ffc, 0xc1bf828c6dc496a0ULL) },
    { make_floatx80_init(0x3ffd, 0x8fffffffffff80fbULL),
      make_floatx80_init(0x3fff, 0x9b8d39b9d54e3a79ULL),
      make_floatx80_init(0x3ffc, 0xdc69cdceaa71d3c8ULL) },
    { make_floatx80_init(0x3ffd, 0x9fffffffffffbc23ULL),
      make_floatx80_init(0x3fff, 0x9ef5326091a10313ULL),
      make_floatx80_init(0x3ffc, 0xf7a993048d081898ULL) },
    { make_floatx80_init(0x3ffd, 0xafffffffffff20ecULL),
      make_floatx80_init(0x3fff, 0xa27043030c49370aULL),
      make_floatx80_init(0x3ffd, 0x89c10c0c3124dc28ULL) },
    { make_floatx80_init(0x3ffd, 0xc00000000000fd2cULL),
      make_floatx80_init(0x3fff, 0xa5fed6a9b15171cfULL),
      make_floatx80_init(0x3ffd, 0x97fb5aa6c545c73cULL) },
    { make_floatx80_init(0x3ffd, 0xd0000000000093beULL),
      make_floatx80_init(0x3fff, 0xa9a15ab4ea7c30e6ULL),
      make_floatx80_init(0x3ffd, 0xa6856ad3a9f0c398ULL) },
    { make_floatx80_init(0x3ffd, 0xe00000000000c2aeULL),
      make_floatx80_init(0x3fff, 0xad583eea42a17876ULL),
      make_floatx80_init(0x3ffd, 0xb560fba90a85e1d8ULL) },
    { make_floatx80_init(0x3ffd, 0xefffffffffff1e3fULL),
      make_floatx80_init(0x3fff, 0xb123f581d2abef6cULL),
      make_floatx80_init(0x3ffd, 0xc48fd6074aafbdb0ULL) },
    { make_floatx80_init(0x3ffd, 0xffffffffffff1c23ULL),
      make_floatx80_init(0x3fff, 0xb504f333f9de2cadULL),
      make_floatx80_init(0x3ffd, 0xd413cccfe778b2b4ULL) },
    { make_floatx80_init(0x3ffe, 0x8800000000006344ULL),
      make_floatx80_init(0x3fff, 0xb8fbaf4762fbd0a1ULL),
      make_floatx80_init(0x3ffd, 0xe3eebd1d8bef4284ULL) },
    { make_floatx80_init(0x3ffe, 0x9000000000005d67ULL),
      make_floatx80_init(0x3fff, 0xbd08a39f580c668dULL),
      make_floatx80_init(0x3ffd, 0xf4228e7d60319a34ULL) },
    { make_floatx80_init(0x3ffe, 0x9800000000009127ULL),
      make_floatx80_init(0x3fff, 0xc12c4cca6670e042ULL),
      make_floatx80_init(0x3ffe, 0x82589994cce1c084ULL) },
    { make_floatx80_init(0x3ffe, 0x9fffffffffff06f9ULL),
      make_floatx80_init(0x3fff, 0xc5672a11550655c3ULL),
      make_floatx80_init(0x3ffe, 0x8ace5422aa0cab86ULL) },
    { make_floatx80_init(0x3ffe, 0xa7fffffffffff80dULL),
      make_floatx80_init(0x3fff, 0xc9b9bd866e2f234bULL),
      make_floatx80_init(0x3ffe, 0x93737b0cdc5e4696ULL) },
    { make_floatx80_init(0x3ffe, 0xafffffffffff1470ULL),
      make_floatx80_init(0x3fff, 0xce248c151f83fd69ULL),
      make_floatx80_init(0x3ffe, 0x9c49182a3f07fad2ULL) },
    { make_floatx80_init(0x3ffe, 0xb800000000000e0aULL),
      make_floatx80_init(0x3fff, 0xd2a81d91f12aec5cULL),
      make_floatx80_init(0x3ffe, 0xa5503b23e255d8b8ULL) },
    { make_floatx80_init(0x3ffe, 0xc00000000000b7faULL),
      make_floatx80_init(0x3fff, 0xd744fccad69dd630ULL),
      make_floatx80_init(0x3ffe, 0xae89f995ad3bac60ULL) },
    { make_floatx80_init(0x3ffe, 0xc800000000003aa6ULL),
      make_floatx80_init(0x3fff, 0xdbfbb797daf25a44ULL),
      make_floatx80_init(0x3ffe, 0xb7f76f2fb5e4b488ULL) },
    { make_floatx80_init(0x3ffe, 0xd00000000000a6aeULL),
      make_floatx80_init(0x3fff, 0xe0ccdeec2a954685ULL),
      make_floatx80_init(0x3ffe, 0xc199bdd8552a8d0aULL) },
    { make_floatx80_init(0x3ffe, 0xd800000000004165ULL),
      make_floatx80_init(0x3fff, 0xe5b906e77c837155ULL),
      make_floatx80_init(0x3ffe, 0xcb720dcef906e2aaULL) },
    { make_floatx80_init(0x3ffe, 0xe00000000000582cULL),
      make_floatx80_init(0x3fff, 0xeac0c6e7dd24713aULL),
      make_floatx80_init(0x3ffe, 0xd5818dcfba48e274ULL) },
    { make_floatx80_init(0x3ffe, 0xe800000000001a5dULL),
      make_floatx80_init(0x3fff, 0xefe4b99bdcdb06ebULL),
      make_floatx80_init(0x3ffe, 0xdfc97337b9b60dd6ULL) },
    { make_floatx80_init(0x3ffe, 0xefffffffffffc1efULL),
      make_floatx80_init(0x3fff, 0xf5257d152486a2faULL),
      make_floatx80_init(0x3ffe, 0xea4afa2a490d45f4ULL) },
    { make_floatx80_init(0x3ffe, 0xf800000000001069ULL),
      make_floatx80_init(0x3fff, 0xfa83b2db722a0e5cULL),
      make_floatx80_init(0x3ffe, 0xf50765b6e4541cb8ULL) },
    { make_floatx80_init(0x3fff, 0x8000000000000000ULL),
      make_floatx80_init(0x4000, 0x8000000000000000ULL),
      make_floatx80_init(0x3fff, 0x8000000000000000ULL) },
};

#if __Use_Original_Qemu == 1 /* original QEMU (U45/U46/U47/U53/U54/U56/U58) */
void helper_f2xm1(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    uint64_t sig = extractFloatx80Frac(ST0);
    int32_t exp = extractFloatx80Exp(ST0);
    bool sign = extractFloatx80Sign(ST0);

    if (floatx80_invalid_encoding(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST0 = floatx80_silence_nan(ST0, &env->fp_status);
        }
    } else if (exp > 0x3fff ||
               (exp == 0x3fff && sig != (0x8000000000000000ULL))) {
        /* Out of range for the instruction, treat as invalid.  */
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
    } else if (exp == 0x3fff) {
        /* Argument 1 or -1, exact result 1 or -0.5.  */
        if (sign) {
            ST0 = make_floatx80(0xbffe, 0x8000000000000000ULL);
        }
    } else if (exp < 0x3fb0) {
        if (!floatx80_is_zero(ST0)) {
            /*
             * Multiplying the argument by an extra-precision version
             * of log(2) is sufficiently precise.  Zero arguments are
             * returned unchanged.
             */
            uint64_t sig0, sig1, sig2;
            if (exp == 0) {
                normalizeFloatx80Subnormal(sig, &exp, &sig);
            }
            mul128By64To192(ln2_sig_high, ln2_sig_low, sig, &sig0, &sig1,
                            &sig2);
            /* This result is inexact.  */
            sig1 |= 1;
            ST0 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                                sign, exp, sig0, sig1,
                                                &env->fp_status);
        }
    } else {
        floatx80 tmp, y, accum;
        bool asign, bsign;
        int32_t n, aexp, bexp;
        uint64_t asig0, asig1, asig2, bsig0, bsig1;
        FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
        FloatX80RoundPrec save_prec =
            env->fp_status.floatx80_rounding_precision;
        env->fp_status.float_rounding_mode = float_round_nearest_even;
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;

        /* Find the nearest multiple of 1/32 to the argument.  */
        tmp = floatx80_scalbn(ST0, 5, &env->fp_status);
        n = 32 + floatx80_to_int32(tmp, &env->fp_status);
        y = floatx80_sub(ST0, f2xm1_table[n].t, &env->fp_status);

        if (floatx80_is_zero(y)) {
            /*
             * Use the value of 2^t - 1 from the table, to avoid
             * needing to special-case zero as a result of
             * multiplication below.
             */
            ST0 = f2xm1_table[n].t;
            set_float_exception_flags(float_flag_inexact, &env->fp_status);
            env->fp_status.float_rounding_mode = save_mode;
        } else {
            /*
             * Compute the lower parts of a polynomial expansion for
             * (2^y - 1) / y.
             */
            accum = floatx80_mul(f2xm1_coeff_7, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_6, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_5, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_4, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_3, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_2, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_1, accum, &env->fp_status);
            accum = floatx80_mul(accum, y, &env->fp_status);
            accum = floatx80_add(f2xm1_coeff_0_low, accum, &env->fp_status);

            /*
             * The full polynomial expansion is f2xm1_coeff_0 + accum
             * (where accum has much lower magnitude, and so, in
             * particular, carry out of the addition is not possible).
             * (This expansion is only accurate to about 70 bits, not
             * 128 bits.)
             */
            aexp = extractFloatx80Exp(f2xm1_coeff_0);
            asign = extractFloatx80Sign(f2xm1_coeff_0);
            shift128RightJamming(extractFloatx80Frac(accum), 0,
                                 aexp - extractFloatx80Exp(accum),
                                 &asig0, &asig1);
            bsig0 = extractFloatx80Frac(f2xm1_coeff_0);
            bsig1 = 0;
            if (asign == extractFloatx80Sign(accum)) {
                add128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
            } else {
                sub128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
            }
            /* And thus compute an approximation to 2^y - 1.  */
            mul128By64To192(asig0, asig1, extractFloatx80Frac(y),
                            &asig0, &asig1, &asig2);
            aexp += extractFloatx80Exp(y) - 0x3ffe;
            asign ^= extractFloatx80Sign(y);
            if (n != 32) {
                /*
                 * Multiply this by the precomputed value of 2^t and
                 * add that of 2^t - 1.
                 */
                mul128By64To192(asig0, asig1,
                                extractFloatx80Frac(f2xm1_table[n].exp2),
                                &asig0, &asig1, &asig2);
                aexp += extractFloatx80Exp(f2xm1_table[n].exp2) - 0x3ffe;
                bexp = extractFloatx80Exp(f2xm1_table[n].exp2m1);
                bsig0 = extractFloatx80Frac(f2xm1_table[n].exp2m1);
                bsig1 = 0;
                if (bexp < aexp) {
                    shift128RightJamming(bsig0, bsig1, aexp - bexp,
                                         &bsig0, &bsig1);
                } else if (aexp < bexp) {
                    shift128RightJamming(asig0, asig1, bexp - aexp,
                                         &asig0, &asig1);
                    aexp = bexp;
                }
                /* The sign of 2^t - 1 is always that of the result.  */
                bsign = extractFloatx80Sign(f2xm1_table[n].exp2m1);
                if (asign == bsign) {
                    /* Avoid possible carry out of the addition.  */
                    shift128RightJamming(asig0, asig1, 1,
                                         &asig0, &asig1);
                    shift128RightJamming(bsig0, bsig1, 1,
                                         &bsig0, &bsig1);
                    ++aexp;
                    add128(asig0, asig1, bsig0, bsig1, &asig0, &asig1);
                } else {
                    sub128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
                    asign = bsign;
                }
            }
            env->fp_status.float_rounding_mode = save_mode;
            /* This result is inexact.  */
            asig1 |= 1;
            ST0 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                                asign, aexp, asig0, asig1,
                                                &env->fp_status);
        }

        env->fp_status.floatx80_rounding_precision = save_prec;
    }
    merge_exception_flags(env, old_flags);
}

void helper_fptan(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        fptemp = tan(fptemp);
        ST0 = double_to_floatx80(env, fptemp);
        fpush(env);
        ST0 = floatx80_one;
        env->fpus &= ~0x400; /* C2 <-- 0 */
        /* the above code is for |arg| < 2**52 only */
    }
}
#else /* ours (U45/U46/U47/U53/U54/U56/U58) */
/*
 * NoVmp (ledger U53): x87 transcendentals. Special operands and domains
 * follow the SDM tables (Vol2 FSIN/FCOS/FSINCOS/FPTAN/FPATAN/F2XM1/FYL2X/
 * FYL2XP1) and, where the SDM says "undefined", the i5-13600K (emu-uc72-risk
 * --x87-special); finite results come from the engine in x87_trans.c.
 * Unmasked IE/DE/ZE leave the operands untouched; unmasked UE/OE/PE store
 * the (scaled) result.
 */
enum { X87K_ZERO, X87K_DEN, X87K_NORM, X87K_INF, X87K_QNAN, X87K_SNAN, X87K_BAD };

static int x87k(floatx80 a, float_status *s)
{
    if (floatx80_invalid_encoding(a)) {
        return X87K_BAD;
    }
    if (floatx80_is_any_nan(a)) {
        return floatx80_is_signaling_nan(a, s) ? X87K_SNAN : X87K_QNAN;
    }
    if (floatx80_is_infinity(a)) {
        return X87K_INF;
    }
    if (floatx80_is_zero(a)) {
        return X87K_ZERO;
    }
    return extractFloatx80Exp(a) == 0 ? X87K_DEN : X87K_NORM;
}

/* FCW exception-mask bits sit at the X87T_IE..X87T_PE positions */
static inline bool x87_masked(CPUX86State *env, unsigned f)
{
    return (env->fpuc & f) != 0;
}

static inline int x87_rc(CPUX86State *env)
{
    return (env->fpuc >> 10) & 3;
}

static inline void x87_set_c1(CPUX86State *env, bool c1)
{
    env->fpus = (env->fpus & ~FPUS_C1) | (c1 ? FPUS_C1 : 0);
}

static inline floatx80 x87_one(void)
{
    return make_floatx80(0x3fff, 0x8000000000000000ULL);
}

/*
 * One-operand NaN / invalid-encoding prologue. Returns true when handled;
 * *res is the masked result to write (if *write).
 */
static bool x87_nan1(CPUX86State *env, floatx80 x, int k, floatx80 *res, bool *write)
{
    *write = false;
    if (k == X87K_BAD) {
        fpu_set_exception(env, FPUS_IE);
        if (x87_masked(env, X87T_IE)) {
            *res = floatx80_default_nan(&env->fp_status);
            *write = true;
        }
        x87_set_c1(env, false);    /* NaN / unsupported: C1 cleared (i5-13600K) */
        return true;
    }
    if (k == X87K_SNAN) {
        fpu_set_exception(env, FPUS_IE);
        if (x87_masked(env, X87T_IE)) {
            *res = floatx80_silence_nan(x, &env->fp_status);
            *write = true;
        }
        x87_set_c1(env, false);    /* NaN / unsupported: C1 cleared (i5-13600K) */
        return true;
    }
    if (k == X87K_QNAN) {
        *res = x;
        *write = true;
        x87_set_c1(env, false);    /* NaN / unsupported: C1 cleared (i5-13600K) */
        return true;
    }
    return false;
}

#if __Use_Original_Qemu != 1 /* ours (U97) */
/*
 * NoVmp (ledger U97): two NaNs of the same kind on the x87 FPU -> the one with the larger
 * significand (SDM Vol1 4.8.3.5 Table 4-8, x87 column); equal significands -> the
 * positive one (the SDM leaves the tie open; i5-13600K FPATAN/FYL2X/FYL2XP1/FSCALE, as
 * FADD & co. through softfloat's pickNaN, Emulator/data/cases_nan.txt).
 */
static floatx80 x87_nan_larger(floatx80 x, floatx80 y)
{
    uint64_t fx = extractFloatx80Frac(x), fy = extractFloatx80Frac(y);

    if (fx != fy) {
        return fx > fy ? x : y;
    }
    return extractFloatx80Sign(x) ? y : x;
}
#endif /* __Use_Original_Qemu (U97) */

/* Two-operand NaN prologue (FYL2X/FYL2XP1/FPATAN: result goes to ST1). */
static bool x87_nan2(CPUX86State *env, floatx80 x, floatx80 y, int kx, int ky,
                     floatx80 *res, bool *write)
{
    *write = false;
    if (kx == X87K_BAD || ky == X87K_BAD) {
        fpu_set_exception(env, FPUS_IE);
        if (x87_masked(env, X87T_IE)) {
            *res = floatx80_default_nan(&env->fp_status);
            *write = true;
        }
        x87_set_c1(env, false);    /* NaN / unsupported: C1 cleared (i5-13600K) */
        return true;
    }
    if (kx >= X87K_QNAN || ky >= X87K_QNAN) {
        bool snan = kx == X87K_SNAN || ky == X87K_SNAN;
        if (snan) {
            fpu_set_exception(env, FPUS_IE);
            if (!x87_masked(env, X87T_IE)) {
                x87_set_c1(env, false);    /* NaN / unsupported: C1 cleared (i5-13600K) */
                return true;
            }
        }
        /* SDM Table 4-8: two NaNs -> the larger significand, quieted (tie: U97) */
        *res = floatx80_silence_nan(
            kx >= X87K_QNAN && ky >= X87K_QNAN ? x87_nan_larger(x, y) :
            kx >= X87K_QNAN ? x : y, &env->fp_status);
        *write = true;
        x87_set_c1(env, false);    /* NaN / unsupported: C1 cleared (i5-13600K) */
        return true;
    }
    return false;
}

/* raise the engine's flags; returns false when an unmasked DE/IE/ZE blocks the write */
static bool x87_finish(CPUX86State *env, X87TOut *o)
{
    unsigned f = o->flags & 0x3f;

    bool ok;

    fpu_set_exception(env, f);
    ok = !((f & X87T_IE) && !x87_masked(env, X87T_IE)) &&
         !((f & X87T_DE) && !x87_masked(env, X87T_DE)) &&
         !((f & X87T_ZE) && !x87_masked(env, X87T_ZE));
    if (!ok) {
        x87_set_c1(env, false);     /* nothing written: C1 cleared (i5-13600K) */
    }
    return ok;
}

/* round an engine value for this CPU state */
static floatx80 x87_shape(CPUX86State *env, X87TVal v, int pbits, char pmode, X87TOut *o, bool second)
{
    return x87t_round(v, pbits, pmode, x87_rc(env), x87_masked(env, X87T_UE),
                      x87_masked(env, X87T_OE), o, second);
}

/*
 * Tiny-argument path of FSIN/FCOS/FSINCOS/FPTAN (i5-13600K): for |x| < 2^-68
 * (biased exponent < 0x3FBB) sine and tangent return x, cosine returns 1, with
 * PE set, C1 = 0, and UE when the (denormal) result is tiny.
 */
#define X87_TRIG_TINY_EXP 0x3fbb

static floatx80 x87_trig_tiny(CPUX86State *env, floatx80 x, bool cosine, X87TOut *o)
{
    o->flags |= X87T_PE;
    o->c1 = false;
    if (cosine) {
        return x87_one();
    }
    if (extractFloatx80Exp(x) == 0 && (extractFloatx80Frac(x) >> 63)) {
        /* pseudo-denormal: its value is the smallest normal's range, delivered normalised, no UE */
        return packFloatx80(extractFloatx80Sign(x), 1, extractFloatx80Frac(x));
    }
    if (extractFloatx80Exp(x) == 0) {
        o->flags |= X87T_UE;
        if (!x87_masked(env, X87T_UE)) {
            /* unmasked underflow: the operand normalised, exponent + 24576 */
            uint64_t m = extractFloatx80Frac(x);
            int sh = clz64(m);
            return packFloatx80(extractFloatx80Sign(x), 1 - sh + 24576, m << sh);
        }
    }
    return x;
}

void helper_f2xm1(CPUX86State *env)
{
    floatx80 x = ST0, res;
    int k = x87k(x, &env->fp_status);
    int32_t exp = extractFloatx80Exp(x);
    uint64_t sig = extractFloatx80Frac(x);
    bool write;
    X87TOut o = { 0 };

    if (x87_nan1(env, x, k, &res, &write)) {
        if (write) {
            ST0 = res;
        }
        return;
    }
    if (k == X87K_ZERO) {
        x87_set_c1(env, false);         /* +-0 unchanged, C1 cleared (i5-13600K) */
        return;
    }
    if (k == X87K_INF) {
        if (extractFloatx80Sign(x)) {
            ST0 = make_floatx80(0xbfff, 0x8000000000000000ULL);    /* 2^-inf - 1 = -1 */
        }
        x87_set_c1(env, false);
        return;
    }
    if (exp > 0x3fff || (exp == 0x3fff && sig != 0x8000000000000000ULL)) {
        /* |x| > 1: undefined in the SDM; the hardware leaves ST0 and sets PE */
        fpu_set_exception(env, FPUS_PE);
        x87_set_c1(env, false);
        return;
    }
    if (exp == 0x3fff) {
        /* +-1: exact 1 / -0.5, PE set all the same (hardware) */
        if (extractFloatx80Sign(x)) {
            ST0 = make_floatx80(0xbffe, 0x8000000000000000ULL);
        }
        fpu_set_exception(env, FPUS_PE);
        x87_set_c1(env, false);
        return;
    }
    if (k == X87K_DEN) {
        o.flags |= X87T_DE;
        if (!x87_masked(env, X87T_DE)) {
            x87_set_c1(env, false);
            fpu_set_exception(env, FPUS_DE);
            return;
        }
    }
    o.flags |= X87T_PE;                 /* PE on every engine result (i5-13600K) */
    res = x87_shape(env, x87t_f2xm1(x), 128, 'Z', &o, false);
    if (x87_finish(env, &o)) {
        ST0 = res;
        x87_set_c1(env, o.c1);
    }
}

/* FSIN / FCOS / FSINCOS / FPTAN common checks: returns true when finished */
static bool x87_trig_pre(CPUX86State *env, floatx80 x, int k, bool *out_of_range)
{
    *out_of_range = false;
    if (k == X87K_NORM && extractFloatx80Exp(x) >= 0x403e) {
        /* |x| >= 2^63: C2 = 1, operand unchanged */
        env->fpus |= 0x400;
        x87_set_c1(env, false);
        *out_of_range = true;
        return true;
    }
    env->fpus &= ~0x400;                /* C2 <- 0 */
    return false;
}

void helper_fsin(CPUX86State *env)
{
    floatx80 x = ST0, res;
    int k = x87k(x, &env->fp_status);
    bool write, oor;
    X87TVal sn, cs;
    X87TOut o = { 0 };

    if (x87_nan1(env, x, k, &res, &write)) {
        if (write) {
            ST0 = res;
        }
        env->fpus &= ~0x400;
        return;
    }
    if (x87_trig_pre(env, x, k, &oor)) {
        return;
    }
    if (k == X87K_ZERO) {
        x87_set_c1(env, false);
        return;
    }
    if (k == X87K_INF) {
        x87_set_c1(env, false);
        fpu_set_exception(env, FPUS_IE);
        if (x87_masked(env, X87T_IE)) {
            ST0 = floatx80_default_nan(&env->fp_status);
        }
        return;
    }
    if (k == X87K_DEN) {
        o.flags |= X87T_DE;
        if (!x87_masked(env, X87T_DE)) {
            x87_set_c1(env, false);
            fpu_set_exception(env, FPUS_DE);
            return;
        }
    }
    if (extractFloatx80Exp(x) < X87_TRIG_TINY_EXP) {
        res = x87_trig_tiny(env, x, false, &o);
    } else {
        x87t_trig(x, false, &sn, &cs);
        o.flags |= X87T_PE;             /* PE on every engine result (i5-13600K) */
        res = x87_shape(env, sn, 128, 'Z', &o, false);
    }
    if (x87_finish(env, &o)) {
        ST0 = res;
        x87_set_c1(env, o.c1);
    }
}

void helper_fcos(CPUX86State *env)
{
    floatx80 x = ST0, res;
    int k = x87k(x, &env->fp_status);
    bool write, oor;
    X87TVal sn, cs;
    X87TOut o = { 0 };

    if (x87_nan1(env, x, k, &res, &write)) {
        if (write) {
            ST0 = res;
        }
        env->fpus &= ~0x400;
        return;
    }
    if (x87_trig_pre(env, x, k, &oor)) {
        return;
    }
    if (k == X87K_ZERO) {
        ST0 = x87_one();
        x87_set_c1(env, false);
        return;
    }
    if (k == X87K_INF) {
        x87_set_c1(env, false);
        fpu_set_exception(env, FPUS_IE);
        if (x87_masked(env, X87T_IE)) {
            ST0 = floatx80_default_nan(&env->fp_status);
        }
        return;
    }
    if (k == X87K_DEN) {
        o.flags |= X87T_DE;
        if (!x87_masked(env, X87T_DE)) {
            x87_set_c1(env, false);
            fpu_set_exception(env, FPUS_DE);
            return;
        }
    }
    if (extractFloatx80Exp(x) < X87_TRIG_TINY_EXP) {
        res = x87_trig_tiny(env, x, true, &o);
    } else {
        x87t_trig(x, false, &sn, &cs);
        o.flags |= X87T_PE;             /* PE on every engine result (i5-13600K) */
        res = x87_shape(env, cs, 128, 'Z', &o, false);
    }
    if (x87_finish(env, &o)) {
        ST0 = res;
        x87_set_c1(env, o.c1);
    }
}

void helper_fsincos(CPUX86State *env)
{
    floatx80 x = ST0, res, rs, rc;
    int k = x87k(x, &env->fp_status);
    bool write, oor;
    X87TVal sn, cs;
    X87TOut o = { 0 };

    if (x87_nan1(env, x, k, &res, &write)) {
        if (write) {
            ST0 = res;
            fpush(env);
            ST0 = res;
        }
        env->fpus &= ~0x400;
        return;
    }
    if (x87_trig_pre(env, x, k, &oor)) {
        return;
    }
    if (k == X87K_ZERO) {
        fpush(env);
        ST0 = x87_one();                /* sin(+-0) = +-0 in ST1, cos = 1 in ST0 */
        x87_set_c1(env, false);
        return;
    }
    if (k == X87K_INF) {
        x87_set_c1(env, false);
        fpu_set_exception(env, FPUS_IE);
        if (x87_masked(env, X87T_IE)) {
            ST0 = floatx80_default_nan(&env->fp_status);
            fpush(env);
            ST0 = floatx80_default_nan(&env->fp_status);
        }
        return;
    }
    if (k == X87K_DEN) {
        o.flags |= X87T_DE;
        if (!x87_masked(env, X87T_DE)) {
            x87_set_c1(env, false);
            fpu_set_exception(env, FPUS_DE);
            return;
        }
    }
    if (extractFloatx80Exp(x) < X87_TRIG_TINY_EXP) {
        rs = x87_trig_tiny(env, x, false, &o);
        rc = x87_one();
    } else {
        x87t_trig(x, true, &sn, &cs);
        o.flags |= X87T_PE;             /* PE on every engine result (i5-13600K) */
        rs = x87_shape(env, sn, 128, 'Z', &o, true);
        rc = x87_shape(env, cs, 128, 'Z', &o, false);
    }
    if (x87_finish(env, &o)) {
        ST0 = rs;
        fpush(env);
        ST0 = rc;
        x87_set_c1(env, o.c1);          /* C1 follows the cosine (pushed last) */
    }
}

void helper_fptan(CPUX86State *env)
{
    floatx80 x = ST0, res;
    int k = x87k(x, &env->fp_status);
    bool write, oor;
    X87TOut o = { 0 };

    if (x87_nan1(env, x, k, &res, &write)) {
        if (write) {
            ST0 = res;
            fpush(env);
            ST0 = res;
        }
        env->fpus &= ~0x400;
        return;
    }
    if (x87_trig_pre(env, x, k, &oor)) {
        return;
    }
    if (k == X87K_ZERO) {
        fpush(env);
        ST0 = x87_one();
        x87_set_c1(env, false);
        return;
    }
    if (k == X87K_INF) {
        x87_set_c1(env, false);
        fpu_set_exception(env, FPUS_IE);
        if (x87_masked(env, X87T_IE)) {
            ST0 = floatx80_default_nan(&env->fp_status);
            fpush(env);
            ST0 = floatx80_default_nan(&env->fp_status);
        }
        return;
    }
    if (k == X87K_DEN) {
        o.flags |= X87T_DE;
        if (!x87_masked(env, X87T_DE)) {
            x87_set_c1(env, false);
            fpu_set_exception(env, FPUS_DE);
            return;
        }
    }
    if (extractFloatx80Exp(x) < X87_TRIG_TINY_EXP) {
        res = x87_trig_tiny(env, x, false, &o);
    } else {
        o.flags |= X87T_PE;             /* PE on every engine result (i5-13600K) */
        res = x87_shape(env, x87t_tan(x), 128, 'Z', &o, false);
    }
    if (x87_finish(env, &o)) {
        ST0 = res;
        fpush(env);
        ST0 = x87_one();
        x87_set_c1(env, o.c1);
    }
}

/*
 * A denormal operand raises DE before the infinity rules apply (i5-13600K,
 * FYL2X/FYL2XP1 with one infinite operand). Returns false when an unmasked
 * DE blocks the result.
 */
static bool x87_de2(CPUX86State *env, int kx, int ky)
{
    if (kx == X87K_DEN || ky == X87K_DEN) {
        fpu_set_exception(env, FPUS_DE);
        return x87_masked(env, X87T_DE);
    }
    return true;
}

/* FYL2X: ST1 := ST1 * log2(ST0), pop (SDM Vol2 Table 3-50) */
void helper_fyl2x(CPUX86State *env)
{
    floatx80 x = ST0, y = ST1, res;
    float_status *s = &env->fp_status;
    int kx = x87k(x, s), ky = x87k(y, s);
    bool write, xneg = extractFloatx80Sign(x), yneg = extractFloatx80Sign(y);
    X87TOut o = { 0 };

    if (x87_nan2(env, x, y, kx, ky, &res, &write)) {
        if (write) {
            ST1 = res;
            fpop(env);
        }
        return;
    }
    x87_set_c1(env, false);
    if (xneg && kx != X87K_ZERO) {
        goto invalid;                   /* log of a negative number */
    }
    if (kx == X87K_ZERO) {
        if (ky == X87K_ZERO) {
            goto invalid;
        }
        if (ky != X87K_INF) {
            fpu_set_exception(env, FPUS_ZE);
            if (!x87_masked(env, X87T_ZE)) {
                return;
            }
        }
        ST1 = packFloatx80(!yneg, 0x7fff, 0x8000000000000000ULL);   /* -sign(y) * inf */
        fpop(env);
        return;
    }
    if (kx == X87K_INF) {
        if (ky == X87K_ZERO) {
            goto invalid;
        }
        if (!x87_de2(env, kx, ky)) {
            return;
        }
        ST1 = packFloatx80(yneg, 0x7fff, 0x8000000000000000ULL);
        fpop(env);
        return;
    }
    /* x finite positive */
    if (extractFloatx80Exp(x) == 0x3fff && extractFloatx80Frac(x) == 0x8000000000000000ULL) {
        /* log2(1) = 0 */
        if (ky == X87K_INF) {
            goto invalid;
        }
        if (kx == X87K_DEN || ky == X87K_DEN) {
            fpu_set_exception(env, FPUS_DE);
            if (!x87_masked(env, X87T_DE)) {
                return;
            }
        }
        ST1 = packFloatx80(yneg, 0, 0);
        fpop(env);
        return;
    }
    {
        bool lneg = extractFloatx80Exp(x) < 0x3fff;     /* log2(x) < 0 */
        if (ky == X87K_INF) {
            if (!x87_de2(env, kx, ky)) {
                return;
            }
            ST1 = packFloatx80(yneg != lneg, 0x7fff, 0x8000000000000000ULL);
            fpop(env);
            return;
        }
        if (ky == X87K_ZERO) {
            if (kx == X87K_DEN) {
                fpu_set_exception(env, FPUS_DE);
                if (!x87_masked(env, X87T_DE)) {
                    return;
                }
            }
            ST1 = packFloatx80(yneg != lneg, 0, 0);
            fpop(env);
            return;
        }
    }
    if (kx == X87K_DEN || ky == X87K_DEN) {
        o.flags |= X87T_DE;
        if (!x87_masked(env, X87T_DE)) {
            x87_set_c1(env, false);
            fpu_set_exception(env, FPUS_DE);
            return;
        }
    }
    o.flags |= X87T_PE;                 /* PE on every engine result (i5-13600K) */
    res = x87_shape(env, x87t_fyl2x(x, y), 128, 'Z', &o, false);
    if (x87_finish(env, &o)) {
        ST1 = res;
        x87_set_c1(env, o.c1);
        fpop(env);
    }
    return;
invalid:
    x87_set_c1(env, false);
    fpu_set_exception(env, FPUS_IE);
    if (x87_masked(env, X87T_IE)) {
        ST1 = floatx80_default_nan(s);
        fpop(env);
    }
}

/* FYL2XP1: ST1 := ST1 * log2(ST0 + 1), pop (SDM Vol2 Table 3-51) */
void helper_fyl2xp1(CPUX86State *env)
{
    floatx80 x = ST0, y = ST1, res;
    float_status *s = &env->fp_status;
    int kx = x87k(x, s), ky = x87k(y, s);
    bool write, xneg = extractFloatx80Sign(x), yneg = extractFloatx80Sign(y);
    X87TOut o = { 0 };

    if (x87_nan2(env, x, y, kx, ky, &res, &write)) {
        if (write) {
            ST1 = res;
            fpop(env);
        }
        return;
    }
    x87_set_c1(env, false);
    if (kx == X87K_ZERO) {
        if (ky == X87K_INF) {
            goto invalid;
        }
        if (ky == X87K_DEN) {
            fpu_set_exception(env, FPUS_DE);
            if (!x87_masked(env, X87T_DE)) {
                return;
            }
        }
        ST1 = packFloatx80(xneg != yneg, 0, 0);
        fpop(env);
        return;
    }
    if (kx == X87K_INF) {
        if (xneg || ky == X87K_ZERO) {
            goto invalid;               /* -inf < -1; 0 * log2(+inf) */
        }
        /* +inf (outside the documented range): log2(+inf) = +inf, times y */
        if (!x87_de2(env, kx, ky)) {
            return;
        }
        ST1 = packFloatx80(yneg, 0x7fff, 0x8000000000000000ULL);
        fpop(env);
        return;
    }
#if __Use_Original_Qemu != 1 /* ours (U432) */
    /*
     * NoVmp (ledger U432): a finite x < -1 is #IA whatever y is (SDM Vol1 Table
     * 8-10 "FYL2XP1: operand more negative than -1"), so y = +-inf / +-0 do not
     * escape it (U533: the only behaviour; the i5-13600K returns +-0 / +-inf
     * there, documented in docs/quirks.md).
     */
    if ((ky == X87K_INF || ky == X87K_ZERO) && xneg &&
        extractFloatx80Exp(x) >= 0x3fff &&
        (extractFloatx80Exp(x) > 0x3fff ||
         extractFloatx80Frac(x) != 0x8000000000000000ULL)) {
        goto invalid;
    }
#endif /* __Use_Original_Qemu (U432) */
    /*
     * y = +-inf / +-0 come before the x = -1 rule: the hardware treats
     * log2(1 + x) as negative there (x < -1 is #IA above)
     */
    if (ky == X87K_INF) {
        if (!x87_de2(env, kx, ky)) {
            return;
        }
        ST1 = packFloatx80(xneg != yneg, 0x7fff, 0x8000000000000000ULL);
        fpop(env);
        return;
    }
    if (ky == X87K_ZERO) {
        if (!x87_de2(env, kx, ky)) {
            return;
        }
        ST1 = packFloatx80(xneg != yneg, 0, 0);
        fpop(env);
        return;
    }
    if (xneg && extractFloatx80Exp(x) >= 0x3fff) {
        bool below = extractFloatx80Exp(x) > 0x3fff ||
                     extractFloatx80Frac(x) != 0x8000000000000000ULL;
        if (below) {
            goto invalid;               /* x < -1: #IA (SDM Vol1 Table 8-10) */
        }
        /* x = -1 (undefined in the SDM): the hardware stores ST0 itself and
           sets PE */
        if (kx == X87K_DEN || ky == X87K_DEN) {
            fpu_set_exception(env, FPUS_DE);
            if (!x87_masked(env, X87T_DE)) {
                return;
            }
        }
        fpu_set_exception(env, FPUS_PE);
        ST1 = x;
        fpop(env);
        return;
    }
    if (kx == X87K_DEN || ky == X87K_DEN) {
        o.flags |= X87T_DE;
        if (!x87_masked(env, X87T_DE)) {
            x87_set_c1(env, false);
            fpu_set_exception(env, FPUS_DE);
            return;
        }
    }
    o.flags |= X87T_PE;                 /* PE on every engine result (i5-13600K) */
    res = x87_shape(env, x87t_fyl2xp1(x, y), 128, 'Z', &o, false);
    if (x87_finish(env, &o)) {
        ST1 = res;
        x87_set_c1(env, o.c1);
        fpop(env);
    }
    return;
invalid:
    x87_set_c1(env, false);
    fpu_set_exception(env, FPUS_IE);
    if (x87_masked(env, X87T_IE)) {
        ST1 = floatx80_default_nan(s);
        fpop(env);
    }
}

/* the FPATAN quotient shortcut applies below 2^-40 */
static bool x87_is_tiny_ratio(X87TVal q)
{
    return (int32_t)((q.v.high >> 48) & 0x7fff) - 16383 + q.scale < -40;
}

/* FPATAN: ST1 := arctan(ST1 / ST0), pop (SDM Vol2 Table 3-32) */
void helper_fpatan(CPUX86State *env)
{
    floatx80 x = ST0, y = ST1, res;
    float_status *s = &env->fp_status;
    int kx = x87k(x, s), ky = x87k(y, s);
    bool write, xneg = extractFloatx80Sign(x), yneg = extractFloatx80Sign(y);
    X87TOut o = { 0 };
    X87TVal v = { 0 };
    /* exact-quadrant constants as float128, rounded by x87_shape */
    static const float128 pi = make_float128_init(0x4000921FB54442D1ULL, 0x8469898CC51701B8ULL);
    static const float128 pi_2 = make_float128_init(0x3FFF921FB54442D1ULL, 0x8469898CC51701B8ULL);
    static const float128 pi_4 = make_float128_init(0x3FFE921FB54442D1ULL, 0x8469898CC51701B8ULL);
    static const float128 pi3_4 = make_float128_init(0x40002D97C7F3321DULL, 0x234F272993D1414AULL);

    if (x87_nan2(env, x, y, kx, ky, &res, &write)) {
        if (write) {
            ST1 = res;
            fpop(env);
        }
        return;
    }
    x87_set_c1(env, false);
    if (kx == X87K_DEN || ky == X87K_DEN) {
        o.flags |= X87T_DE;
        if (!x87_masked(env, X87T_DE)) {
            x87_set_c1(env, false);
            fpu_set_exception(env, FPUS_DE);
            return;
        }
    }
    if (ky == X87K_ZERO) {
        /* y = +-0: +-0 for x > 0 or x = +0, +-pi for x < 0 or x = -0 */
        if (!xneg) {
            ST1 = packFloatx80(yneg, 0, 0);
            fpop(env);
            fpu_set_exception(env, o.flags & 0x3f);
            return;
        }
        v.v = pi;
    } else if (ky == X87K_INF) {
        v.v = kx == X87K_INF ? (xneg ? pi3_4 : pi_4) : pi_2;
    } else if (kx == X87K_ZERO) {
        v.v = pi_2;
    } else if (kx == X87K_INF) {
        if (!xneg) {
            ST1 = packFloatx80(yneg, 0, 0);
            fpop(env);
            fpu_set_exception(env, o.flags & 0x3f);
            return;
        }
        v.v = pi;
    } else if (!xneg) {
        /*
         * |y/x| < 2^-40 (and x > 0): the hardware returns the quotient,
         * correctly rounded, C1 from that rounding, PE set
         */
        X87TVal qt = x87t_quot(y, x);
        if (x87_is_tiny_ratio(qt)) {
            o.flags |= X87T_PE;         /* PE on every engine result (i5-13600K) */
            res = x87_shape(env, qt, 128, 'Z', &o, false);     /* trunc67(y / x), one RC rounding */
            /* always inexact (atan q < q), so a tiny result is also an underflow */
            o.flags |= X87T_PE | ((o.flags & X87T_TINY) ? X87T_UE : 0);
            if (x87_finish(env, &o)) {
                ST1 = res;
                x87_set_c1(env, o.c1);
                fpop(env);
            }
            return;
        }
        v = x87t_fpatan(y, x);
        goto shape;
    } else {
        v = x87t_fpatan(y, x);
        goto shape;
    }
    if (yneg) {
        v.v = float128_chs(v.v);
    }
shape:
    o.flags |= X87T_PE;                 /* PE on every engine result (i5-13600K) */
    res = x87_shape(env, v, 128, 'Z', &o, false);
    if (x87_finish(env, &o)) {
        ST1 = res;
        x87_set_c1(env, o.c1);
        fpop(env);
    }
}

#endif /* __Use_Original_Qemu (U45/U46/U47/U53/U54/U56/U58) */

/* Values of pi/4, pi/2, 3pi/4 and pi, with 128-bit precision.  */
#define pi_4_exp 0x3ffe
#define pi_4_sig_high 0xc90fdaa22168c234ULL
#define pi_4_sig_low 0xc4c6628b80dc1cd1ULL
#define pi_2_exp 0x3fff
#define pi_2_sig_high 0xc90fdaa22168c234ULL
#define pi_2_sig_low 0xc4c6628b80dc1cd1ULL
#define pi_34_exp 0x4000
#define pi_34_sig_high 0x96cbe3f9990e91a7ULL
#define pi_34_sig_low 0x9394c9e8a0a5159dULL
#define pi_exp 0x4000
#define pi_sig_high 0xc90fdaa22168c234ULL
#define pi_sig_low 0xc4c6628b80dc1cd1ULL

/*
 * Polynomial coefficients for an approximation to atan(x), with only
 * odd powers of x used, for x in the interval [-1/16, 1/16].  (Unlike
 * for some other approximations, no low part is needed for the first
 * coefficient here to achieve a sufficiently accurate result, because
 * the coefficient in this minimax approximation is very close to
 * exactly 1.)
 */
#define fpatan_coeff_0 make_floatx80(0x3fff, 0x8000000000000000ULL)
#define fpatan_coeff_1 make_floatx80(0xbffd, 0xaaaaaaaaaaaaaa43ULL)
#define fpatan_coeff_2 make_floatx80(0x3ffc, 0xccccccccccbfe4f8ULL)
#define fpatan_coeff_3 make_floatx80(0xbffc, 0x92492491fbab2e66ULL)
#define fpatan_coeff_4 make_floatx80(0x3ffb, 0xe38e372881ea1e0bULL)
#define fpatan_coeff_5 make_floatx80(0xbffb, 0xba2c0104bbdd0615ULL)
#define fpatan_coeff_6 make_floatx80(0x3ffb, 0x9baf7ebf898b42efULL)

struct fpatan_data {
    /* High and low parts of atan(x).  */
    floatx80 atan_high, atan_low;
};

static const struct fpatan_data fpatan_table[9] = {
    { floatx80_zero_init,
      floatx80_zero_init },
    { make_floatx80_init(0x3ffb, 0xfeadd4d5617b6e33ULL),
      make_floatx80_init(0xbfb9, 0xdda19d8305ddc420ULL) },
    { make_floatx80_init(0x3ffc, 0xfadbafc96406eb15ULL),
      make_floatx80_init(0x3fbb, 0xdb8f3debef442fccULL) },
    { make_floatx80_init(0x3ffd, 0xb7b0ca0f26f78474ULL),
      make_floatx80_init(0xbfbc, 0xeab9bdba460376faULL) },
    { make_floatx80_init(0x3ffd, 0xed63382b0dda7b45ULL),
      make_floatx80_init(0x3fbc, 0xdfc88bd978751a06ULL) },
    { make_floatx80_init(0x3ffe, 0x8f005d5ef7f59f9bULL),
      make_floatx80_init(0x3fbd, 0xb906bc2ccb886e90ULL) },
    { make_floatx80_init(0x3ffe, 0xa4bc7d1934f70924ULL),
      make_floatx80_init(0x3fbb, 0xcd43f9522bed64f8ULL) },
    { make_floatx80_init(0x3ffe, 0xb8053e2bc2319e74ULL),
      make_floatx80_init(0xbfbc, 0xd3496ab7bd6eef0cULL) },
    { make_floatx80_init(0x3ffe, 0xc90fdaa22168c235ULL),
      make_floatx80_init(0xbfbc, 0xece675d1fc8f8cbcULL) },
};

#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
void helper_fpatan(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    uint64_t arg0_sig = extractFloatx80Frac(ST0);
    int32_t arg0_exp = extractFloatx80Exp(ST0);
    bool arg0_sign = extractFloatx80Sign(ST0);
    uint64_t arg1_sig = extractFloatx80Frac(ST1);
    int32_t arg1_exp = extractFloatx80Exp(ST1);
    bool arg1_sign = extractFloatx80Sign(ST1);

    if (floatx80_invalid_encoding(ST0) ||
        floatx80_invalid_encoding(ST1)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST0, &env->fp_status);
    } else if (floatx80_is_signaling_nan(ST1, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST1, &env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        ST1 = ST0;
    } else if (floatx80_is_any_nan(ST1)) {
        /* Pass this NaN through.  */
    } else if (floatx80_is_zero(ST1) && !arg0_sign) {
        /* Pass this zero through.  */
    } else if (((floatx80_is_infinity(ST0) && !floatx80_is_infinity(ST1)) ||
                 arg0_exp - arg1_exp >= 80) &&
               !arg0_sign) {
        /*
         * Dividing ST1 by ST0 gives the correct result up to
         * rounding, and avoids spurious underflow exceptions that
         * might result from passing some small values through the
         * polynomial approximation, but if a finite nonzero result of
         * division is exact, the result of fpatan is still inexact
         * (and underflowing where appropriate).
         */
        FloatX80RoundPrec save_prec =
            env->fp_status.floatx80_rounding_precision;
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;
        ST1 = floatx80_div(ST1, ST0, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = save_prec;
        if (!floatx80_is_zero(ST1) &&
            !(get_float_exception_flags(&env->fp_status) &
              float_flag_inexact)) {
            /*
             * The mathematical result is very slightly closer to zero
             * than this exact result.  Round a value with the
             * significand adjusted accordingly to get the correct
             * exceptions, and possibly an adjusted result depending
             * on the rounding mode.
             */
            uint64_t sig = extractFloatx80Frac(ST1);
            int32_t exp = extractFloatx80Exp(ST1);
            bool sign = extractFloatx80Sign(ST1);
            if (exp == 0) {
                normalizeFloatx80Subnormal(sig, &exp, &sig);
            }
            ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                                sign, exp, sig - 1,
                                                -1, &env->fp_status);
        }
    } else {
        /* The result is inexact.  */
        bool rsign = arg1_sign;
        int32_t rexp;
        uint64_t rsig0, rsig1;
        if (floatx80_is_zero(ST1)) {
            /*
             * ST0 is negative.  The result is pi with the sign of
             * ST1.
             */
            rexp = pi_exp;
            rsig0 = pi_sig_high;
            rsig1 = pi_sig_low;
        } else if (floatx80_is_infinity(ST1)) {
            if (floatx80_is_infinity(ST0)) {
                if (arg0_sign) {
                    rexp = pi_34_exp;
                    rsig0 = pi_34_sig_high;
                    rsig1 = pi_34_sig_low;
                } else {
                    rexp = pi_4_exp;
                    rsig0 = pi_4_sig_high;
                    rsig1 = pi_4_sig_low;
                }
            } else {
                rexp = pi_2_exp;
                rsig0 = pi_2_sig_high;
                rsig1 = pi_2_sig_low;
            }
        } else if (floatx80_is_zero(ST0) || arg1_exp - arg0_exp >= 80) {
            rexp = pi_2_exp;
            rsig0 = pi_2_sig_high;
            rsig1 = pi_2_sig_low;
        } else if (floatx80_is_infinity(ST0) || arg0_exp - arg1_exp >= 80) {
            /* ST0 is negative.  */
            rexp = pi_exp;
            rsig0 = pi_sig_high;
            rsig1 = pi_sig_low;
        } else {
            /*
             * ST0 and ST1 are finite, nonzero and with exponents not
             * too far apart.
             */
            int32_t adj_exp, num_exp, den_exp, xexp, yexp, n, texp, zexp, aexp;
            int32_t azexp, axexp;
            bool adj_sub, ysign, zsign;
            uint64_t adj_sig0, adj_sig1, num_sig, den_sig, xsig0, xsig1;
            uint64_t msig0, msig1, msig2, remsig0, remsig1, remsig2;
            uint64_t ysig0, ysig1, tsig, zsig0, zsig1, asig0, asig1;
            uint64_t azsig0, azsig1;
            uint64_t azsig2, azsig3, axsig0, axsig1;
            floatx80 x8;
            FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
            FloatX80RoundPrec save_prec =
                env->fp_status.floatx80_rounding_precision;
            env->fp_status.float_rounding_mode = float_round_nearest_even;
            env->fp_status.floatx80_rounding_precision = floatx80_precision_x;

            if (arg0_exp == 0) {
                normalizeFloatx80Subnormal(arg0_sig, &arg0_exp, &arg0_sig);
            }
            if (arg1_exp == 0) {
                normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
            }
            if (arg0_exp > arg1_exp ||
                (arg0_exp == arg1_exp && arg0_sig >= arg1_sig)) {
                /* Work with abs(ST1) / abs(ST0).  */
                num_exp = arg1_exp;
                num_sig = arg1_sig;
                den_exp = arg0_exp;
                den_sig = arg0_sig;
                if (arg0_sign) {
                    /* The result is subtracted from pi.  */
                    adj_exp = pi_exp;
                    adj_sig0 = pi_sig_high;
                    adj_sig1 = pi_sig_low;
                    adj_sub = true;
                } else {
                    /* The result is used as-is.  */
                    adj_exp = 0;
                    adj_sig0 = 0;
                    adj_sig1 = 0;
                    adj_sub = false;
                }
            } else {
                /* Work with abs(ST0) / abs(ST1).  */
                num_exp = arg0_exp;
                num_sig = arg0_sig;
                den_exp = arg1_exp;
                den_sig = arg1_sig;
                /* The result is added to or subtracted from pi/2.  */
                adj_exp = pi_2_exp;
                adj_sig0 = pi_2_sig_high;
                adj_sig1 = pi_2_sig_low;
                adj_sub = !arg0_sign;
            }

            /*
             * Compute x = num/den, where 0 < x <= 1 and x is not too
             * small.
             */
            xexp = num_exp - den_exp + 0x3ffe;
            remsig0 = num_sig;
            remsig1 = 0;
            if (den_sig <= remsig0) {
                shift128Right(remsig0, remsig1, 1, &remsig0, &remsig1);
                ++xexp;
            }
            xsig0 = estimateDiv128To64(remsig0, remsig1, den_sig);
            mul64To128(den_sig, xsig0, &msig0, &msig1);
            sub128(remsig0, remsig1, msig0, msig1, &remsig0, &remsig1);
            while ((int64_t) remsig0 < 0) {
                --xsig0;
                add128(remsig0, remsig1, 0, den_sig, &remsig0, &remsig1);
            }
            xsig1 = estimateDiv128To64(remsig1, 0, den_sig);
            /*
             * No need to correct any estimation error in xsig1; even
             * with such error, it is accurate enough.
             */

            /*
             * Split x as x = t + y, where t = n/8 is the nearest
             * multiple of 1/8 to x.
             */
            x8 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                               false, xexp + 3, xsig0,
                                               xsig1, &env->fp_status);
            n = floatx80_to_int32(x8, &env->fp_status);
            if (n == 0) {
                ysign = false;
                yexp = xexp;
                ysig0 = xsig0;
                ysig1 = xsig1;
                texp = 0;
                tsig = 0;
            } else {
                int shift = clz32(n) + 32;
                texp = 0x403b - shift;
                tsig = n;
                tsig <<= shift;
                if (texp == xexp) {
                    sub128(xsig0, xsig1, tsig, 0, &ysig0, &ysig1);
                    if ((int64_t) ysig0 >= 0) {
                        ysign = false;
                        if (ysig0 == 0) {
                            if (ysig1 == 0) {
                                yexp = 0;
                            } else {
                                shift = clz64(ysig1) + 64;
                                yexp = xexp - shift;
                                shift128Left(ysig0, ysig1, shift,
                                             &ysig0, &ysig1);
                            }
                        } else {
                            shift = clz64(ysig0);
                            yexp = xexp - shift;
                            shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                        }
                    } else {
                        ysign = true;
                        sub128(0, 0, ysig0, ysig1, &ysig0, &ysig1);
                        if (ysig0 == 0) {
                            shift = clz64(ysig1) + 64;
                        } else {
                            shift = clz64(ysig0);
                        }
                        yexp = xexp - shift;
                        shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                    }
                } else {
                    /*
                     * t's exponent must be greater than x's because t
                     * is positive and the nearest multiple of 1/8 to
                     * x, and if x has a greater exponent, the power
                     * of 2 with that exponent is also a multiple of
                     * 1/8.
                     */
                    uint64_t usig0, usig1;
                    shift128RightJamming(xsig0, xsig1, texp - xexp,
                                         &usig0, &usig1);
                    ysign = true;
                    sub128(tsig, 0, usig0, usig1, &ysig0, &ysig1);
                    if (ysig0 == 0) {
                        shift = clz64(ysig1) + 64;
                    } else {
                        shift = clz64(ysig0);
                    }
                    yexp = texp - shift;
                    shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                }
            }

            /*
             * Compute z = y/(1+tx), so arctan(x) = arctan(t) +
             * arctan(z).
             */
            zsign = ysign;
            if (texp == 0 || yexp == 0) {
                zexp = yexp;
                zsig0 = ysig0;
                zsig1 = ysig1;
            } else {
                /*
                 * t <= 1, x <= 1 and if both are 1 then y is 0, so tx < 1.
                 */
                int32_t dexp = texp + xexp - 0x3ffe;
                uint64_t dsig0, dsig1, dsig2;
                mul128By64To192(xsig0, xsig1, tsig, &dsig0, &dsig1, &dsig2);
                /*
                 * dexp <= 0x3fff (and if equal, dsig0 has a leading 0
                 * bit).  Add 1 to produce the denominator 1+tx.
                 */
                shift128RightJamming(dsig0, dsig1, 0x3fff - dexp,
                                     &dsig0, &dsig1);
                dsig0 |= 0x8000000000000000ULL;
                zexp = yexp - 1;
                remsig0 = ysig0;
                remsig1 = ysig1;
                remsig2 = 0;
                if (dsig0 <= remsig0) {
                    shift128Right(remsig0, remsig1, 1, &remsig0, &remsig1);
                    ++zexp;
                }
                zsig0 = estimateDiv128To64(remsig0, remsig1, dsig0);
                mul128By64To192(dsig0, dsig1, zsig0, &msig0, &msig1, &msig2);
                sub192(remsig0, remsig1, remsig2, msig0, msig1, msig2,
                       &remsig0, &remsig1, &remsig2);
                while ((int64_t) remsig0 < 0) {
                    --zsig0;
                    add192(remsig0, remsig1, remsig2, 0, dsig0, dsig1,
                           &remsig0, &remsig1, &remsig2);
                }
                zsig1 = estimateDiv128To64(remsig1, remsig2, dsig0);
                /* No need to correct any estimation error in zsig1.  */
            }

            if (zexp == 0) {
                azexp = 0;
                azsig0 = 0;
                azsig1 = 0;
            } else {
                floatx80 z2, accum;
                uint64_t z2sig0, z2sig1, z2sig2, z2sig3;
                /* Compute z^2.  */
                mul128To256(zsig0, zsig1, zsig0, zsig1,
                            &z2sig0, &z2sig1, &z2sig2, &z2sig3);
                z2 = normalizeRoundAndPackFloatx80(floatx80_precision_x, false,
                                                   zexp + zexp - 0x3ffe,
                                                   z2sig0, z2sig1,
                                                   &env->fp_status);

                /* Compute the lower parts of the polynomial expansion.  */
                accum = floatx80_mul(fpatan_coeff_6, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_5, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_4, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_3, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_2, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_1, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);

                /*
                 * The full polynomial expansion is z*(fpatan_coeff_0 + accum).
                 * fpatan_coeff_0 is 1, and accum is negative and much smaller.
                 */
                aexp = extractFloatx80Exp(fpatan_coeff_0);
                shift128RightJamming(extractFloatx80Frac(accum), 0,
                                     aexp - extractFloatx80Exp(accum),
                                     &asig0, &asig1);
                sub128(extractFloatx80Frac(fpatan_coeff_0), 0, asig0, asig1,
                       &asig0, &asig1);
                /* Multiply by z to compute arctan(z).  */
                azexp = aexp + zexp - 0x3ffe;
                mul128To256(asig0, asig1, zsig0, zsig1, &azsig0, &azsig1,
                            &azsig2, &azsig3);
            }

            /* Add arctan(t) (positive or zero) and arctan(z) (sign zsign).  */
            if (texp == 0) {
                /* z is positive.  */
                axexp = azexp;
                axsig0 = azsig0;
                axsig1 = azsig1;
            } else {
                bool low_sign = extractFloatx80Sign(fpatan_table[n].atan_low);
                int32_t low_exp = extractFloatx80Exp(fpatan_table[n].atan_low);
                uint64_t low_sig0 =
                    extractFloatx80Frac(fpatan_table[n].atan_low);
                uint64_t low_sig1 = 0;
                axexp = extractFloatx80Exp(fpatan_table[n].atan_high);
                axsig0 = extractFloatx80Frac(fpatan_table[n].atan_high);
                axsig1 = 0;
                shift128RightJamming(low_sig0, low_sig1, axexp - low_exp,
                                     &low_sig0, &low_sig1);
                if (low_sign) {
                    sub128(axsig0, axsig1, low_sig0, low_sig1,
                           &axsig0, &axsig1);
                } else {
                    add128(axsig0, axsig1, low_sig0, low_sig1,
                           &axsig0, &axsig1);
                }
                if (azexp >= axexp) {
                    shift128RightJamming(axsig0, axsig1, azexp - axexp + 1,
                                         &axsig0, &axsig1);
                    axexp = azexp + 1;
                    shift128RightJamming(azsig0, azsig1, 1,
                                         &azsig0, &azsig1);
                } else {
                    shift128RightJamming(axsig0, axsig1, 1,
                                         &axsig0, &axsig1);
                    shift128RightJamming(azsig0, azsig1, axexp - azexp + 1,
                                         &azsig0, &azsig1);
                    ++axexp;
                }
                if (zsign) {
                    sub128(axsig0, axsig1, azsig0, azsig1,
                           &axsig0, &axsig1);
                } else {
                    add128(axsig0, axsig1, azsig0, azsig1,
                           &axsig0, &axsig1);
                }
            }

            if (adj_exp == 0) {
                rexp = axexp;
                rsig0 = axsig0;
                rsig1 = axsig1;
            } else {
                /*
                 * Add or subtract arctan(x) (exponent axexp,
                 * significand axsig0 and axsig1, positive, not
                 * necessarily normalized) to the number given by
                 * adj_exp, adj_sig0 and adj_sig1, according to
                 * adj_sub.
                 */
                if (adj_exp >= axexp) {
                    shift128RightJamming(axsig0, axsig1, adj_exp - axexp + 1,
                                         &axsig0, &axsig1);
                    rexp = adj_exp + 1;
                    shift128RightJamming(adj_sig0, adj_sig1, 1,
                                         &adj_sig0, &adj_sig1);
                } else {
                    shift128RightJamming(axsig0, axsig1, 1,
                                         &axsig0, &axsig1);
                    shift128RightJamming(adj_sig0, adj_sig1,
                                         axexp - adj_exp + 1,
                                         &adj_sig0, &adj_sig1);
                    rexp = axexp + 1;
                }
                if (adj_sub) {
                    sub128(adj_sig0, adj_sig1, axsig0, axsig1,
                           &rsig0, &rsig1);
                } else {
                    add128(adj_sig0, adj_sig1, axsig0, axsig1,
                           &rsig0, &rsig1);
                }
            }

            env->fp_status.float_rounding_mode = save_mode;
            env->fp_status.floatx80_rounding_precision = save_prec;
        }
        /* This result is inexact.  */
        rsig1 |= 1;
        ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x, rsign, rexp,
                                            rsig0, rsig1, &env->fp_status);
    }

    fpop(env);
    merge_exception_flags(env, old_flags);
}
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */

/* fpush() only validates the new top. FXTRACT also needs ST(1) validated. */
static inline void fpush_fxtract(CPUX86State *env)
{
    fpush(env);
    env->fptags[(env->fpstt + 1) & 7] = 0;
}

void helper_fxtract(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    CPU_LDoubleU temp;
#if __Use_Original_Qemu == 1 /* original QEMU (U54) */

    temp.d = ST0;
#else /* ours (U54) */
    int f = 0;

    temp.d = ST0;
    /*
     * NoVmp (ledger U54): an unmasked #IA (SNaN, unsupported), #Z (zero) or
     * #D (denormal) leaves TOP and ST0 unchanged (SDM Vol1 8.5)
     */
    if (floatx80_invalid_encoding(ST0) || floatx80_is_signaling_nan(ST0, &env->fp_status)) {
        f = float_flag_invalid;
    } else if (floatx80_is_zero(ST0)) {
        f = float_flag_divbyzero;
    } else if (EXPD(temp) == 0) {
        f = float_flag_input_denormal_used;
    }
    if (f && x87_cancel_unmasked(env, old_flags, f)) {
        return;
    }
#endif /* __Use_Original_Qemu (U54) */

    if (floatx80_is_zero(ST0)) {
        /* Easy way to generate -inf and raising division by 0 exception */
        ST0 = floatx80_div(floatx80_chs(floatx80_one), floatx80_zero,
                           &env->fp_status);
        fpush_fxtract(env);
        ST0 = temp.d;
    } else if (floatx80_invalid_encoding(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
        fpush_fxtract(env);
        ST0 = ST1;
    } else if (floatx80_is_any_nan(ST0)) {
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST0 = floatx80_silence_nan(ST0, &env->fp_status);
        }
        fpush_fxtract(env);
        ST0 = ST1;
    } else if (floatx80_is_infinity(ST0)) {
        fpush_fxtract(env);
        ST0 = ST1;
        ST1 = floatx80_infinity;
    } else {
        int expdif;

        if (EXPD(temp) == 0) {
            int shift = clz64(temp.l.lower);
            temp.l.lower <<= shift;
            expdif = 1 - EXPBIAS - shift;
#if __Use_Original_Qemu == 1 /* original QEMU (U28/U29/U54) */
            float_raise(float_flag_input_denormal, &env->fp_status);
#else /* ours (U28/U29/U54) */
            float_raise(float_flag_input_denormal_used, &env->fp_status);
#endif /* __Use_Original_Qemu (U28/U29/U54) */
        } else {
            expdif = EXPD(temp) - EXPBIAS;
        }
        /* DP exponent bias */
        ST0 = int32_to_floatx80(expdif, &env->fp_status);
        fpush_fxtract(env);
        BIASEXPONENT(temp);
        ST0 = temp.d;
    }
    merge_exception_flags(env, old_flags);
}

static void helper_fprem_common(CPUX86State *env, bool mod)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U47/U53/U54) */
    int old_flags = save_exception_flags(env);
    uint64_t quotient;
    CPU_LDoubleU temp0, temp1;
    int exp0, exp1, expdiff;
#else /* ours (U47/U53/U54) */
    int old_flags = save_exception_flags(env), f;
    uint64_t quotient;
    CPU_LDoubleU temp0, temp1;
    int exp0, exp1, expdiff;
    floatx80 st0 = ST0;
    uint16_t fpus0 = env->fpus;
#endif /* __Use_Original_Qemu (U47/U53/U54) */

    temp0.d = ST0;
    temp1.d = ST1;
    exp0 = EXPD(temp0);
    exp1 = EXPD(temp1);

#if __Use_Original_Qemu == 1 /* original QEMU (U54) */
    env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
#else /* ours (U54) */
    if (floatx80_is_any_nan(ST0) || floatx80_is_any_nan(ST1) ||
        floatx80_invalid_encoding(ST0) || floatx80_invalid_encoding(ST1) ||
        floatx80_is_zero(ST1) || floatx80_is_infinity(ST0)) {
        /* a NaN or #IA result: C2 and C1 cleared, C0 and C3 kept (Table 8-10; i5-13600K) */
        env->fpus &= ~0x0600;
    } else {
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
    }
#endif /* __Use_Original_Qemu (U54) */
    if (floatx80_is_zero(ST0) || floatx80_is_zero(ST1) ||
        exp0 == 0x7fff || exp1 == 0x7fff ||
        floatx80_invalid_encoding(ST0) || floatx80_invalid_encoding(ST1)) {
        ST0 = floatx80_modrem(ST0, ST1, mod, &quotient, &env->fp_status);
    } else {
        if (exp0 == 0) {
            exp0 = 1 - clz64(temp0.l.lower);
        }
        if (exp1 == 0) {
            exp1 = 1 - clz64(temp1.l.lower);
        }
        expdiff = exp0 - exp1;
        if (expdiff < 64) {
            ST0 = floatx80_modrem(ST0, ST1, mod, &quotient, &env->fp_status);
            env->fpus |= (quotient & 0x4) << (8 - 2);  /* (C0) <-- q2 */
            env->fpus |= (quotient & 0x2) << (14 - 1); /* (C3) <-- q1 */
            env->fpus |= (quotient & 0x1) << (9 - 0);  /* (C1) <-- q0 */
        } else {
            /*
             * Partial remainder.  This choice of how many bits to
             * process at once is specified in AMD instruction set
             * manuals, and empirically is followed by Intel
             * processors as well; it ensures that the final remainder
             * operation in a loop does produce the correct low three
             * bits of the quotient.  AMD manuals specify that the
             * flags other than C2 are cleared, and empirically Intel
             * processors clear them as well.
             */
            int n = 32 + (expdiff % 32);
            temp1.d = floatx80_scalbn(temp1.d, expdiff - n, &env->fp_status);
            ST0 = floatx80_mod(ST0, temp1.d, &env->fp_status);
            env->fpus |= 0x400;  /* C2 <-- 1 */
        }
    }
#if __Use_Original_Qemu == 1 /* original QEMU (U45/U47/U53/U54) */
    merge_exception_flags(env, old_flags);
#else /* ours (U45/U47/U53/U54) */
    /*
     * NoVmp (ledger U54): an unmasked #IA/#D leaves ST0 and the condition
     * codes unchanged; a tiny remainder (exact) with #U unmasked is stored
     * with its exponent biased by 2^24576, UE set, no PE (SDM Vol1 8.5.5)
     */
    f = get_float_exception_flags(&env->fp_status);
    if (x87_unmasked(env, x87_fsw_bits(f) & (FPUS_IE | FPUS_DE | FPUS_ZE))) {
        ST0 = st0;
        env->fpus = fpus0 & ~0x0400;    /* C2 (and C1, below) cleared, C0/C3 kept (i5-13600K) */
        x87_cancel_unmasked(env, old_flags, f);
        return;
    }
    if (extractFloatx80Exp(ST0) == 0 && extractFloatx80Frac(ST0) != 0 &&
        !floatx80_is_infinity(ST1) &&           /* modulus inf: ST0 unchanged (Table 3-33) */
        x87_unmasked(env, FPUS_UE)) {
        uint64_t m = extractFloatx80Frac(ST0);
        int sh = clz64(m);
        ST0 = packFloatx80(extractFloatx80Sign(ST0), 1 - sh + 24576, m << sh);
        float_raise(float_flag_underflow, &env->fp_status);
    }
    merge_exception_flags_ex(env, old_flags, false);
#endif /* __Use_Original_Qemu (U45/U47/U53/U54) */
}

void helper_fprem1(CPUX86State *env)
{
    helper_fprem_common(env, false);
}

void helper_fprem(CPUX86State *env)
{
    helper_fprem_common(env, true);
}

/* 128-bit significand of log2(e).  */
#define log2_e_sig_high 0xb8aa3b295c17f0bbULL
#define log2_e_sig_low 0xbe87fed0691d3e89ULL

/*
 * Polynomial coefficients for an approximation to log2((1+x)/(1-x)),
 * with only odd powers of x used, for x in the interval [2*sqrt(2)-3,
 * 3-2*sqrt(2)], which corresponds to logarithms of numbers in the
 * interval [sqrt(2)/2, sqrt(2)].
 */
#define fyl2x_coeff_0 make_floatx80(0x4000, 0xb8aa3b295c17f0bcULL)
#define fyl2x_coeff_0_low make_floatx80(0xbfbf, 0x834972fe2d7bab1bULL)
#define fyl2x_coeff_1 make_floatx80(0x3ffe, 0xf6384ee1d01febb8ULL)
#define fyl2x_coeff_2 make_floatx80(0x3ffe, 0x93bb62877cdfa2e3ULL)
#define fyl2x_coeff_3 make_floatx80(0x3ffd, 0xd30bb153d808f269ULL)
#define fyl2x_coeff_4 make_floatx80(0x3ffd, 0xa42589eaf451499eULL)
#define fyl2x_coeff_5 make_floatx80(0x3ffd, 0x864d42c0f8f17517ULL)
#define fyl2x_coeff_6 make_floatx80(0x3ffc, 0xe3476578adf26272ULL)
#define fyl2x_coeff_7 make_floatx80(0x3ffc, 0xc506c5f874e6d80fULL)
#define fyl2x_coeff_8 make_floatx80(0x3ffc, 0xac5cf50cc57d6372ULL)
#define fyl2x_coeff_9 make_floatx80(0x3ffc, 0xb1ed0066d971a103ULL)

/*
 * Compute an approximation of log2(1+arg), where 1+arg is in the
 * interval [sqrt(2)/2, sqrt(2)].  It is assumed that when this
 * function is called, rounding precision is set to 80 and the
 * round-to-nearest mode is in effect.  arg must not be exactly zero,
 * and must not be so close to zero that underflow might occur.
 */
static void helper_fyl2x_common(CPUX86State *env, floatx80 arg, int32_t *exp,
                                uint64_t *sig0, uint64_t *sig1)
{
    uint64_t arg0_sig = extractFloatx80Frac(arg);
    int32_t arg0_exp = extractFloatx80Exp(arg);
    bool arg0_sign = extractFloatx80Sign(arg);
    bool asign;
    int32_t dexp, texp, aexp;
    uint64_t dsig0, dsig1, tsig0, tsig1, rsig0, rsig1, rsig2;
    uint64_t msig0, msig1, msig2, t2sig0, t2sig1, t2sig2, t2sig3;
    uint64_t asig0, asig1, asig2, asig3, bsig0, bsig1;
    floatx80 t2, accum;

    /*
     * Compute an approximation of arg/(2+arg), with extra precision,
     * as the argument to a polynomial approximation.  The extra
     * precision is only needed for the first term of the
     * approximation, with subsequent terms being significantly
     * smaller; the approximation only uses odd exponents, and the
     * square of arg/(2+arg) is at most 17-12*sqrt(2) = 0.029....
     */
    if (arg0_sign) {
        dexp = 0x3fff;
        shift128RightJamming(arg0_sig, 0, dexp - arg0_exp, &dsig0, &dsig1);
        sub128(0, 0, dsig0, dsig1, &dsig0, &dsig1);
    } else {
        dexp = 0x4000;
        shift128RightJamming(arg0_sig, 0, dexp - arg0_exp, &dsig0, &dsig1);
        dsig0 |= 0x8000000000000000ULL;
    }
    texp = arg0_exp - dexp + 0x3ffe;
    rsig0 = arg0_sig;
    rsig1 = 0;
    rsig2 = 0;
    if (dsig0 <= rsig0) {
        shift128Right(rsig0, rsig1, 1, &rsig0, &rsig1);
        ++texp;
    }
    tsig0 = estimateDiv128To64(rsig0, rsig1, dsig0);
    mul128By64To192(dsig0, dsig1, tsig0, &msig0, &msig1, &msig2);
    sub192(rsig0, rsig1, rsig2, msig0, msig1, msig2,
           &rsig0, &rsig1, &rsig2);
    while ((int64_t) rsig0 < 0) {
        --tsig0;
        add192(rsig0, rsig1, rsig2, 0, dsig0, dsig1,
               &rsig0, &rsig1, &rsig2);
    }
    tsig1 = estimateDiv128To64(rsig1, rsig2, dsig0);
    /*
     * No need to correct any estimation error in tsig1; even with
     * such error, it is accurate enough.  Now compute the square of
     * that approximation.
     */
    mul128To256(tsig0, tsig1, tsig0, tsig1,
                &t2sig0, &t2sig1, &t2sig2, &t2sig3);
    t2 = normalizeRoundAndPackFloatx80(floatx80_precision_x, false,
                                       texp + texp - 0x3ffe,
                                       t2sig0, t2sig1, &env->fp_status);

    /* Compute the lower parts of the polynomial expansion.  */
    accum = floatx80_mul(fyl2x_coeff_9, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_8, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_7, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_6, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_5, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_4, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_3, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_2, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_1, accum, &env->fp_status);
    accum = floatx80_mul(accum, t2, &env->fp_status);
    accum = floatx80_add(fyl2x_coeff_0_low, accum, &env->fp_status);

    /*
     * The full polynomial expansion is fyl2x_coeff_0 + accum (where
     * accum has much lower magnitude, and so, in particular, carry
     * out of the addition is not possible), multiplied by t.  (This
     * expansion is only accurate to about 70 bits, not 128 bits.)
     */
    aexp = extractFloatx80Exp(fyl2x_coeff_0);
    asign = extractFloatx80Sign(fyl2x_coeff_0);
    shift128RightJamming(extractFloatx80Frac(accum), 0,
                         aexp - extractFloatx80Exp(accum),
                         &asig0, &asig1);
    bsig0 = extractFloatx80Frac(fyl2x_coeff_0);
    bsig1 = 0;
    if (asign == extractFloatx80Sign(accum)) {
        add128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
    } else {
        sub128(bsig0, bsig1, asig0, asig1, &asig0, &asig1);
    }
    /* Multiply by t to compute the required result.  */
    mul128To256(asig0, asig1, tsig0, tsig1,
                &asig0, &asig1, &asig2, &asig3);
    aexp += texp - 0x3ffe;
    *exp = aexp;
    *sig0 = asig0;
    *sig1 = asig1;
}

#if __Use_Original_Qemu == 1 /* original QEMU (U28/U29/U45/U47/U53/U54) */
void helper_fyl2xp1(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    uint64_t arg0_sig = extractFloatx80Frac(ST0);
    int32_t arg0_exp = extractFloatx80Exp(ST0);
    bool arg0_sign = extractFloatx80Sign(ST0);
    uint64_t arg1_sig = extractFloatx80Frac(ST1);
    int32_t arg1_exp = extractFloatx80Exp(ST1);
    bool arg1_sign = extractFloatx80Sign(ST1);

    if (floatx80_invalid_encoding(ST0) ||
        floatx80_invalid_encoding(ST1)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST0, &env->fp_status);
    } else if (floatx80_is_signaling_nan(ST1, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST1, &env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        ST1 = ST0;
    } else if (floatx80_is_any_nan(ST1)) {
        /* Pass this NaN through.  */
    } else if (arg0_exp > 0x3ffd ||
               (arg0_exp == 0x3ffd && arg0_sig > (arg0_sign ?
                                                  0x95f619980c4336f7ULL :
                                                  0xd413cccfe7799211ULL))) {
        /*
         * Out of range for the instruction (ST0 must have absolute
         * value less than 1 - sqrt(2)/2 = 0.292..., according to
         * Intel manuals; AMD manuals allow a range from sqrt(2)/2 - 1
         * to sqrt(2) - 1, which we allow here), treat as invalid.
         */
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_zero(ST0) || floatx80_is_zero(ST1) ||
               arg1_exp == 0x7fff) {
        /*
         * One argument is zero, or multiplying by infinity; correct
         * result is exact and can be obtained by multiplying the
         * arguments.
         */
        ST1 = floatx80_mul(ST0, ST1, &env->fp_status);
    } else if (arg0_exp < 0x3fb0) {
        /*
         * Multiplying both arguments and an extra-precision version
         * of log2(e) is sufficiently precise.
         */
        uint64_t sig0, sig1, sig2;
        int32_t exp;
        if (arg0_exp == 0) {
            normalizeFloatx80Subnormal(arg0_sig, &arg0_exp, &arg0_sig);
        }
        if (arg1_exp == 0) {
            normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
        }
        mul128By64To192(log2_e_sig_high, log2_e_sig_low, arg0_sig,
                        &sig0, &sig1, &sig2);
        exp = arg0_exp + 1;
        mul128By64To192(sig0, sig1, arg1_sig, &sig0, &sig1, &sig2);
        exp += arg1_exp - 0x3ffe;
        /* This result is inexact.  */
        sig1 |= 1;
        ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                            arg0_sign ^ arg1_sign, exp,
                                            sig0, sig1, &env->fp_status);
    } else {
        int32_t aexp;
        uint64_t asig0, asig1, asig2;
        FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
        FloatX80RoundPrec save_prec =
            env->fp_status.floatx80_rounding_precision;
        env->fp_status.float_rounding_mode = float_round_nearest_even;
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;

        helper_fyl2x_common(env, ST0, &aexp, &asig0, &asig1);
        /*
         * Multiply by the second argument to compute the required
         * result.
         */
        if (arg1_exp == 0) {
            normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
        }
        mul128By64To192(asig0, asig1, arg1_sig, &asig0, &asig1, &asig2);
        aexp += arg1_exp - 0x3ffe;
        /* This result is inexact.  */
        asig1 |= 1;
        env->fp_status.float_rounding_mode = save_mode;
        ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                            arg0_sign ^ arg1_sign, aexp,
                                            asig0, asig1, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = save_prec;
    }
    fpop(env);
    merge_exception_flags(env, old_flags);
}

void helper_fyl2x(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    uint64_t arg0_sig = extractFloatx80Frac(ST0);
    int32_t arg0_exp = extractFloatx80Exp(ST0);
    bool arg0_sign = extractFloatx80Sign(ST0);
    uint64_t arg1_sig = extractFloatx80Frac(ST1);
    int32_t arg1_exp = extractFloatx80Exp(ST1);
    bool arg1_sign = extractFloatx80Sign(ST1);

    if (floatx80_invalid_encoding(ST0) ||
        floatx80_invalid_encoding(ST1)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST0, &env->fp_status);
    } else if (floatx80_is_signaling_nan(ST1, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST1, &env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        ST1 = ST0;
    } else if (floatx80_is_any_nan(ST1)) {
        /* Pass this NaN through.  */
    } else if (arg0_sign && !floatx80_is_zero(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_infinity(ST1)) {
        FloatRelation cmp = floatx80_compare(ST0, floatx80_one,
                                             &env->fp_status);
        switch (cmp) {
        case float_relation_less:
            ST1 = floatx80_chs(ST1);
            break;
        case float_relation_greater:
            /* Result is infinity of the same sign as ST1.  */
            break;
        default:
            float_raise(float_flag_invalid, &env->fp_status);
            ST1 = floatx80_default_nan(&env->fp_status);
            break;
        }
    } else if (floatx80_is_infinity(ST0)) {
        if (floatx80_is_zero(ST1)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST1 = floatx80_default_nan(&env->fp_status);
        } else if (arg1_sign) {
            ST1 = floatx80_chs(ST0);
        } else {
            ST1 = ST0;
        }
    } else if (floatx80_is_zero(ST0)) {
        if (floatx80_is_zero(ST1)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST1 = floatx80_default_nan(&env->fp_status);
        } else {
            /* Result is infinity with opposite sign to ST1.  */
            float_raise(float_flag_divbyzero, &env->fp_status);
            ST1 = make_floatx80(arg1_sign ? 0x7fff : 0xffff,
                                0x8000000000000000ULL);
        }
    } else if (floatx80_is_zero(ST1)) {
        if (floatx80_lt(ST0, floatx80_one, &env->fp_status)) {
            ST1 = floatx80_chs(ST1);
        }
        /* Otherwise, ST1 is already the correct result.  */
    } else if (floatx80_eq(ST0, floatx80_one, &env->fp_status)) {
        if (arg1_sign) {
            ST1 = floatx80_chs(floatx80_zero);
        } else {
            ST1 = floatx80_zero;
        }
    } else {
        int32_t int_exp;
        floatx80 arg0_m1;
        FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
        FloatX80RoundPrec save_prec =
            env->fp_status.floatx80_rounding_precision;
        env->fp_status.float_rounding_mode = float_round_nearest_even;
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;

        if (arg0_exp == 0) {
            normalizeFloatx80Subnormal(arg0_sig, &arg0_exp, &arg0_sig);
        }
        if (arg1_exp == 0) {
            normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
        }
        int_exp = arg0_exp - 0x3fff;
        if (arg0_sig > 0xb504f333f9de6484ULL) {
            ++int_exp;
        }
        arg0_m1 = floatx80_sub(floatx80_scalbn(ST0, -int_exp,
                                               &env->fp_status),
                               floatx80_one, &env->fp_status);
        if (floatx80_is_zero(arg0_m1)) {
            /* Exact power of 2; multiply by ST1.  */
            env->fp_status.float_rounding_mode = save_mode;
            ST1 = floatx80_mul(int32_to_floatx80(int_exp, &env->fp_status),
                               ST1, &env->fp_status);
        } else {
            bool asign = extractFloatx80Sign(arg0_m1);
            int32_t aexp;
            uint64_t asig0, asig1, asig2;
            helper_fyl2x_common(env, arg0_m1, &aexp, &asig0, &asig1);
            if (int_exp != 0) {
                bool isign = (int_exp < 0);
                int32_t iexp;
                uint64_t isig;
                int shift;
                int_exp = isign ? -int_exp : int_exp;
                shift = clz32(int_exp) + 32;
                isig = int_exp;
                isig <<= shift;
                iexp = 0x403e - shift;
                shift128RightJamming(asig0, asig1, iexp - aexp,
                                     &asig0, &asig1);
                if (asign == isign) {
                    add128(isig, 0, asig0, asig1, &asig0, &asig1);
                } else {
                    sub128(isig, 0, asig0, asig1, &asig0, &asig1);
                }
                aexp = iexp;
                asign = isign;
            }
            /*
             * Multiply by the second argument to compute the required
             * result.
             */
            if (arg1_exp == 0) {
                normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
            }
            mul128By64To192(asig0, asig1, arg1_sig, &asig0, &asig1, &asig2);
            aexp += arg1_exp - 0x3ffe;
            /* This result is inexact.  */
            asig1 |= 1;
            env->fp_status.float_rounding_mode = save_mode;
            ST1 = normalizeRoundAndPackFloatx80(floatx80_precision_x,
                                                asign ^ arg1_sign, aexp,
                                                asig0, asig1, &env->fp_status);
        }

        env->fp_status.floatx80_rounding_precision = save_prec;
    }
    fpop(env);
    merge_exception_flags(env, old_flags);
}

void helper_fsqrt(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    if (floatx80_is_neg(ST0)) {
        env->fpus &= ~0x4700;  /* (C3,C2,C1,C0) <-- 0000 */
        env->fpus |= 0x400;
    }
    ST0 = floatx80_sqrt(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsincos(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, sin(fptemp));
        fpush(env);
        ST0 = double_to_floatx80(env, cos(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**63 only */
    }
}

void helper_frndint(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    ST0 = floatx80_round_to_int(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fscale(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    if (floatx80_invalid_encoding(ST1) || floatx80_invalid_encoding(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST1)) {
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
        }
        ST0 = ST1;
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST0 = floatx80_silence_nan(ST0, &env->fp_status);
        }
    } else if (floatx80_is_infinity(ST1) &&
               !floatx80_invalid_encoding(ST0) &&
               !floatx80_is_any_nan(ST0)) {
        if (floatx80_is_neg(ST1)) {
            if (floatx80_is_infinity(ST0)) {
                float_raise(float_flag_invalid, &env->fp_status);
                ST0 = floatx80_default_nan(&env->fp_status);
            } else {
                ST0 = (floatx80_is_neg(ST0) ?
                       floatx80_chs(floatx80_zero) :
                       floatx80_zero);
            }
        } else {
            if (floatx80_is_zero(ST0)) {
                float_raise(float_flag_invalid, &env->fp_status);
                ST0 = floatx80_default_nan(&env->fp_status);
            } else {
                ST0 = (floatx80_is_neg(ST0) ?
                       floatx80_chs(floatx80_infinity) :
                       floatx80_infinity);
            }
        }
    } else {
        int n;
        FloatX80RoundPrec save = env->fp_status.floatx80_rounding_precision;
        int save_flags = get_float_exception_flags(&env->fp_status);
        set_float_exception_flags(0, &env->fp_status);
        n = floatx80_to_int32_round_to_zero(ST1, &env->fp_status);
        set_float_exception_flags(save_flags, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = floatx80_precision_x;
        ST0 = floatx80_scalbn(ST0, n, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = save;
    }
    merge_exception_flags(env, old_flags);
}

void helper_fsin(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, sin(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**53 only */
    }
}

void helper_fcos(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, cos(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**63 only */
    }
}
#else /* ours (U28/U29/U45/U47/U53/U54) */


void helper_fsqrt(CPUX86State *env)
{
    floatx80 r;
    if (x87_arith2(env, X87T_OP_SQRT, ST0, ST0, &r)) {
        ST0 = r;
    }
}


void helper_frndint(CPUX86State *env)
{
    int old_flags = save_exception_flags(env);
    /* FRNDINT signals #D for a denormal/pseudo-denormal source (SDM) */
    if (extractFloatx80Exp(ST0) == 0 && extractFloatx80Frac(ST0) != 0) {
        float_raise(float_flag_input_denormal_used, &env->fp_status);
    }
    {
        floatx80 r = floatx80_round_to_int(ST0, &env->fp_status);
        /* NoVmp (ledger U54): unmasked #IA/#D leave ST0 unchanged (SDM Vol1 8.5) */
        if (x87_cancel_unmasked(env, old_flags, get_float_exception_flags(&env->fp_status))) {
            return;
        }
        ST0 = r;
    }
    merge_exception_flags(env, old_flags);
}

/*
 * FSCALE: ST0 := ST0 * 2^RoundTowardZero(ST1) (SDM Vol2 FSCALE, Table 3-36;
 * NoVmp ledger U54). Order measured on the i5-13600K: unsupported/NaN
 * operands first (an SNaN with a QNaN returns the QNaN, Vol1 Table 4-8), then
 * #D for a denormal operand in either register (also with a zero or infinite
 * partner), then the table's infinity/zero cases; finite results are exact
 * scalings shaped like any arithmetic result (masked / unmasked #O/#U with the
 * 2^-+24576 bias, a massive over/underflow becomes a signed infinity/zero).
 */
void helper_fscale(CPUX86State *env)
{
    floatx80 x = ST0, y = ST1;
    float_status *s = &env->fp_status;
    int kx = x87k(x, s), ky = x87k(y, s);
    bool xneg = extractFloatx80Sign(x), yneg = extractFloatx80Sign(y);
    floatx80 res;

    x87_set_c1(env, false);     /* set below only when a result is rounded up */
    /* NaNs and unsupported encodings */
    if (kx == X87K_BAD || ky == X87K_BAD) {
        fpu_set_exception(env, FPUS_IE);
        if (x87_masked(env, X87T_IE)) {
            ST0 = floatx80_default_nan(s);
        } else {
            env->x87_nopop = 1;
        }
        return;
    }
    if (kx >= X87K_QNAN || ky >= X87K_QNAN) {
        if (kx == X87K_SNAN || ky == X87K_SNAN) {
            fpu_set_exception(env, FPUS_IE);
            if (!x87_masked(env, X87T_IE)) {
                env->x87_nopop = 1;
                return;
            }
        }
        if (kx >= X87K_QNAN && ky >= X87K_QNAN && kx != ky) {
            res = kx == X87K_QNAN ? x : y;      /* SNaN and QNaN: the QNaN */
        } else if (kx >= X87K_QNAN && ky >= X87K_QNAN) {
            res = x87_nan_larger(x, y);         /* tie: the positive one (U97) */
        } else {
            res = kx >= X87K_QNAN ? x : y;
        }
        ST0 = floatx80_silence_nan(res, s);
        return;
    }
    /* #D: a denormal in either operand */
    if (kx == X87K_DEN || ky == X87K_DEN) {
        fpu_set_exception(env, FPUS_DE);
        if (!x87_masked(env, X87T_DE)) {
            env->x87_nopop = 1;
            return;
        }
    }
    /* Table 3-36 */
    if (ky == X87K_INF) {
        if ((kx == X87K_INF && yneg) || (kx == X87K_ZERO && !yneg)) {
            fpu_set_exception(env, FPUS_IE);    /* inf * 2^-inf, 0 * 2^+inf */
            if (x87_masked(env, X87T_IE)) {
                ST0 = floatx80_default_nan(s);
            } else {
                env->x87_nopop = 1;
            }
            return;
        }
        ST0 = yneg ? packFloatx80(xneg, 0, 0) : packFloatx80(xneg, 0x7fff, 0x8000000000000000ULL);
        return;
    }
    if (kx == X87K_INF || kx == X87K_ZERO) {
        return;                                 /* +-inf, +-0 unchanged */
    }
    if (ky == X87K_ZERO) {
        /*
         * ST1 = +-0: ST0 unchanged, even a denormal (no #U); a pseudo-denormal
         * is written back in its normal encoding (i5-13600K)
         */
        if (extractFloatx80Exp(x) == 0 && (extractFloatx80Frac(x) >> 63)) {
            ST0 = packFloatx80(xneg, 1, extractFloatx80Frac(x));
        }
        return;
    }
    {
        /* finite nonzero x, nonzero ST1 (a count of 0 still normalises and checks tininess) */
        X87TOut o = { 0 };
        X87TVal v = x87t_arith(X87T_OP_SCALE, x, y);

        res = x87t_round_prec(v, 128, 'Z', 64, x87_rc(env), x87_masked(env, X87T_UE),
                              x87_masked(env, X87T_OE), &o, false);
        fpu_set_exception(env, o.flags & 0x3f);
        x87_set_c1(env, o.c1);
        ST0 = res;
    }
}
#endif /* __Use_Original_Qemu (U28/U29/U45/U47/U53/U54) */

void helper_fxam_ST0(CPUX86State *env)
{
    CPU_LDoubleU temp;
    int expdif;

    temp.d = ST0;

    env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
    if (SIGND(temp)) {
        env->fpus |= 0x200; /* C1 <-- 1 */
    }

    if (env->fptags[env->fpstt]) {
        env->fpus |= 0x4100; /* Empty */
        return;
    }

    expdif = EXPD(temp);
    if (expdif == MAXEXPD) {
        if (MANTD(temp) == 0x8000000000000000ULL) {
            env->fpus |= 0x500; /* Infinity */
        } else if (MANTD(temp) & 0x8000000000000000ULL) {
            env->fpus |= 0x100; /* NaN */
        }
    } else if (expdif == 0) {
        if (MANTD(temp) == 0) {
            env->fpus |=  0x4000; /* Zero */
        } else {
            env->fpus |= 0x4400; /* Denormal */
        }
    } else if (MANTD(temp) & 0x8000000000000000ULL) {
        env->fpus |= 0x400;
    }
}

#if __Use_Original_Qemu != 1 /* ours (U860) */
/*
 * NoVmp (ledger U860): FCS/FDS as FSTENV/FSAVE/FXSAVE/XSAVE save them: "If
 * CPUID.07H.00H:EBX[13] = 1, the processor deprecates FCS and FDS; it saves each as 0000H"
 * (SDM Vol1 8.1.8, the effective CPUID); otherwise the selectors themselves.
 */
static uint16_t x87_saved_sel(CPUX86State *env, uint16_t sel)
{
    return (x86_cpu_x87_ptr_bits(env) & X86_X87_FCS_FDS_DEPR) ? 0 : sel;
}

/*
 * NoVmp (ledger U860): FCS/FDS loaded by FLDENV/FRSTOR/FXRSTOR/XRSTOR (32-bit pointer
 * formats): from the image when CR0.PE = 1 (FLDENV/FRSTOR; FXRSTOR/XRSTOR always), else
 * cleared; while CPUID.07H.00H:EBX[13] = 1 they are deprecated and stay 0000H (the value
 * every save reports, U64).
 */
static void x87_load_sels(CPUX86State *env, uint16_t fcs, uint16_t fds, bool load)
{
    if (!load || (x86_cpu_x87_ptr_bits(env) & X86_X87_FCS_FDS_DEPR)) {
        fcs = fds = 0;
    }
    env->fpcs = fcs;
    env->fpds = fds;
}

#endif /* __Use_Original_Qemu (U860) */
static void do_fstenv(CPUX86State *env, target_ulong ptr, int data32,
                      uintptr_t retaddr)
{
    int fpus, fptag, exp, i;
    uint64_t mant;
    CPU_LDoubleU tmp;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
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
            } else if (exp == 0 || exp == MAXEXPD
                       || (mant & (1LL << 63)) == 0) {
                /* NaNs, infinity, denormal */
                fptag |= 2;
            }
        }
    }
    if (data32) {
        /* 32 bit */
#if __Use_Original_Qemu == 1 /* original QEMU (U62) */
        cpu_stl_data_ra(env, ptr, env->fpuc, retaddr);
        cpu_stl_data_ra(env, ptr + 4, fpus, retaddr);
        cpu_stl_data_ra(env, ptr + 8, fptag, retaddr);
        cpu_stl_data_ra(env, ptr + 12, env->fpip, retaddr); /* fpip */
        cpu_stl_data_ra(env, ptr + 16, env->fpcs, retaddr); /* fpcs */
        cpu_stl_data_ra(env, ptr + 20, env->fpdp, retaddr); /* fpoo */
        cpu_stl_data_ra(env, ptr + 24, env->fpds, retaddr); /* fpos */
#else /* ours (U62) */
        /*
         * NoVmp (ledger U62): the reserved upper words of the FCW, FSW, FTW and
         * FDS dwords read as FFFFh (i5-13600K, emu-alltest --cases hwcheck_gate1)
         */
        cpu_stl_data_ra(env, ptr, 0xffff0000u | env->fpuc, retaddr);
        cpu_stl_data_ra(env, ptr + 4, 0xffff0000u | fpus, retaddr);
        cpu_stl_data_ra(env, ptr + 8, 0xffff0000u | fptag, retaddr);
        cpu_stl_data_ra(env, ptr + 12, env->fpip, retaddr); /* fpip */
        /* FOP in bits 26:16 next to FCS (U64) */
        cpu_stl_data_ra(env, ptr + 16, ((uint32_t)(env->fpop & 0x7ff) << 16) |
                        x87_saved_sel(env, env->fpcs), retaddr);  /* U860 */
        cpu_stl_data_ra(env, ptr + 20, env->fpdp, retaddr); /* fpoo */
        cpu_stl_data_ra(env, ptr + 24, 0xffff0000u | x87_saved_sel(env, env->fpds),
                        retaddr); /* fpos (U860) */
#endif /* __Use_Original_Qemu (U62) */
    } else {
        /* 16 bit */
        cpu_stw_data_ra(env, ptr, env->fpuc, retaddr);
        cpu_stw_data_ra(env, ptr + 2, fpus, retaddr);
        cpu_stw_data_ra(env, ptr + 4, fptag, retaddr);
        cpu_stw_data_ra(env, ptr + 6, env->fpip, retaddr);
#if __Use_Original_Qemu == 1 /* original QEMU (U860) */
        cpu_stw_data_ra(env, ptr + 8, env->fpcs, retaddr);
        cpu_stw_data_ra(env, ptr + 10, env->fpdp, retaddr);
        cpu_stw_data_ra(env, ptr + 12, env->fpds, retaddr);
#else /* ours (U860) */
        cpu_stw_data_ra(env, ptr + 8, x87_saved_sel(env, env->fpcs), retaddr);
        cpu_stw_data_ra(env, ptr + 10, env->fpdp, retaddr);
        cpu_stw_data_ra(env, ptr + 12, x87_saved_sel(env, env->fpds), retaddr);
#endif /* __Use_Original_Qemu (U860) */
    }
}

#if __Use_Original_Qemu != 1 /* ours (U64) */
/*
 * NoVmp (ledger U64): after a non-control x87 instruction. This CPU sets
 * CPUID.(EAX=7,ECX=0):EBX[6] (FDP_EXCPTN_ONLY) and EBX[13] (FCS/FDS
 * deprecated): FOP and FDP change only when the instruction took an unmasked
 * exception (ES set afterwards); a register form keeps FDP (i5-13600K:
 * FDIV m32 by 0 with ZE unmasked -> FOP 0B6h, FDP = operand; masked -> both
 * unchanged; FDIVP ST(1),ST(0) unmasked -> FOP 6F9h).
 */
void helper_x87_ptrs(CPUX86State *env, uint32_t fop, target_ulong fdp, uint32_t mem)
{
    if (env->fpus & FPUS_SE) {
        env->fpop = fop & 0x7ff;
    }
    /*
     * NoVmp (ledger U860): 'mem' = bit 0 memory operand, bits 11:8 its segment register,
     * plus the effective CPUID.(EAX=07H,ECX=0):EBX bits 6 (FDP_EXCPTN_ONLY) and 13 (FCS/FDS
     * deprecated). SDM Vol1 8.1.8: "If CPUID.07H.00H:EBX[6] = 1, the data pointer is updated
     * only for x87 non-control instructions that incur unmasked x87 exceptions"; with the bit
     * clear every non-control instruction with a memory operand updates FDP (and FDS, the
     * selector of the segment it used; 0000H while EBX[13] = 1). A register-only instruction
     * leaves the data pointer as it was ("undefined (reserved)").
     */
    if ((mem & 1) && (!(mem & X86_X87_FDP_EXCPTN_ONLY) || (env->fpus & FPUS_SE))) {
        env->fpdp = fdp;
        env->fpds = (mem & X86_X87_FCS_FDS_DEPR) ? 0 : env->segs[(mem >> 8) & 7].selector;
    }
}

#endif /* __Use_Original_Qemu (U64) */

void helper_fstenv(CPUX86State *env, target_ulong ptr, int data32)
{
    /* backport d5dc3a927a (X86Access, U480): the whole image or nothing */
    x86_access_prepare(env, ptr, data32 ? 28 : 14, MMU_DATA_STORE, GETPC());
    do_fstenv(env, ptr, data32, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U61) */
    /*
     * NoVmp (ledger U61): "FSTENV/FNSTENV ... then masks all floating-point
     * exceptions" (SDM Vol2 FSTENV; i5-13600K: FCW 0360 -> 037F, ES/B cleared
     * with the masks, Goldmont MSROM U6c29)
     */
    cpu_set_fpuc(env, env->fpuc | 0x3f);
#endif /* __Use_Original_Qemu (U61) */
}

static void cpu_set_fpus(CPUX86State *env, uint16_t fpus)
{
    env->fpstt = (fpus >> 11) & 7;
#if __Use_Original_Qemu == 1 /* original QEMU (U41) */
    env->fpus = fpus & ~0x3800 & ~FPUS_B;
    env->fpus |= env->fpus & FPUS_SE ? FPUS_B : 0;
#else /* ours (U41) */
    env->fpus = fpus & ~0x3800;
    /* ES/B follow the flags and the (already loaded) masks, see cpu_set_fpuc */
    cpu_sync_fpus_es(env);
#endif /* __Use_Original_Qemu (U41) */
#if !defined(CONFIG_USER_ONLY)
    if (!(env->fpus & FPUS_SE)) {
        /*
         * Here the processor deasserts FERR#; in response, the chipset deasserts
         * IGNNE#.
         */
        cpu_clear_ignne(env);
    }
#endif
}

static void do_fldenv(CPUX86State *env, target_ulong ptr, int data32,
                      uintptr_t retaddr)
{
    int i, fpus, fptag;

    if (data32) {
        cpu_set_fpuc(env, cpu_lduw_data_ra(env, ptr, retaddr));
        fpus = cpu_lduw_data_ra(env, ptr + 4, retaddr);
        fptag = cpu_lduw_data_ra(env, ptr + 8, retaddr);
    } else {
        cpu_set_fpuc(env, cpu_lduw_data_ra(env, ptr, retaddr));
        fpus = cpu_lduw_data_ra(env, ptr + 2, retaddr);
        fptag = cpu_lduw_data_ra(env, ptr + 4, retaddr);
    }
    cpu_set_fpus(env, fpus);
    for (i = 0; i < 8; i++) {
        env->fptags[i] = ((fptag & 3) == 3);
        fptag >>= 2;
    }
#if __Use_Original_Qemu != 1 /* ours (U64) */
    /*
     * NoVmp (ledger U64): the instruction / data pointers and FOP come from the
     * image too; FCS/FDS are deprecated on this CPU and read as 0 (i5-13600K)
     */
    if (data32) {
        env->fpip = cpu_ldl_data_ra(env, ptr + 12, retaddr);
        env->fpop = (cpu_ldl_data_ra(env, ptr + 16, retaddr) >> 16) & 0x7ff;
        env->fpdp = cpu_ldl_data_ra(env, ptr + 20, retaddr);
        /* FCS / FDS (U860) */
        x87_load_sels(env, cpu_lduw_data_ra(env, ptr + 16, retaddr),
                      cpu_lduw_data_ra(env, ptr + 24, retaddr), env->cr[0] & CR0_PE_MASK);
    } else {
        env->fpip = cpu_lduw_data_ra(env, ptr + 6, retaddr);
        env->fpop = cpu_lduw_data_ra(env, ptr + 8, retaddr) & 0x7ff;
        env->fpdp = cpu_lduw_data_ra(env, ptr + 10, retaddr);
        x87_load_sels(env, cpu_lduw_data_ra(env, ptr + 8, retaddr),
                      cpu_lduw_data_ra(env, ptr + 12, retaddr), env->cr[0] & CR0_PE_MASK);
    }
#endif /* __Use_Original_Qemu (U64) */
}

void helper_fldenv(CPUX86State *env, target_ulong ptr, int data32)
{
    /*
     * backport d5dc3a927a (X86Access, U480): nothing is loaded when any byte faults
     * (upstream prepares this load as MMU_DATA_STORE; it is a load)
     */
    x86_access_prepare(env, ptr, data32 ? 28 : 14, MMU_DATA_LOAD, GETPC());
    do_fldenv(env, ptr, data32, GETPC());
}

/* offset of the register area in the FSAVE/FRSTOR image (U480, as do_fsave/do_frstor) */
#if __Use_Original_Qemu == 1 /* original QEMU (U480) */
#define FSAVE_REGS_OFF(data32) ((target_ulong)14 << (data32))
#else /* ours (U480) */
#define FSAVE_REGS_OFF(data32) ((target_ulong)((data32) ? 28 : 14))   /* U63 */
#endif /* __Use_Original_Qemu (U480) */

static void do_fsave(CPUX86State *env, target_ulong ptr, int data32,
                     uintptr_t retaddr)
{
    floatx80 tmp;
    int i;

    do_fstenv(env, ptr, data32, retaddr);

#if __Use_Original_Qemu == 1 /* original QEMU (U63) */
    ptr += (target_ulong)14 << data32;
#else /* ours (U63) */
    /* NoVmp (ledger U63): REX.W (data32 = 2) keeps the 108-byte layout (i5-13600K) */
    ptr += data32 ? 28 : 14;
#endif /* __Use_Original_Qemu (U63) */
    for (i = 0; i < 8; i++) {
        tmp = ST(i);
        do_fstt(env, tmp, ptr, retaddr);
        ptr += 10;
    }

    do_fninit(env);
}

void helper_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    /* backport d5dc3a927a (X86Access, U480): the whole image or nothing */
    x86_access_prepare(env, ptr, FSAVE_REGS_OFF(data32) + 80, MMU_DATA_STORE, GETPC());
    do_fsave(env, ptr, data32, GETPC());
}

static void do_frstor(CPUX86State *env, target_ulong ptr, int data32,
                      uintptr_t retaddr)
{
    floatx80 tmp;
    int i;

    do_fldenv(env, ptr, data32, retaddr);
#if __Use_Original_Qemu == 1 /* original QEMU (U63) */
    ptr += (target_ulong)14 << data32;
#else /* ours (U63) */
    ptr += data32 ? 28 : 14;
#endif /* __Use_Original_Qemu (U63) */

    for (i = 0; i < 8; i++) {
        tmp = do_fldt(env, ptr, retaddr);
        ST(i) = tmp;
        ptr += 10;
    }
}

void helper_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    /* backport d5dc3a927a (X86Access, U480): nothing is loaded when any byte faults */
    x86_access_prepare(env, ptr, FSAVE_REGS_OFF(data32) + 80, MMU_DATA_LOAD, GETPC());
    do_frstor(env, ptr, data32, GETPC());
}

#define XO(X)  offsetof(X86XSaveArea, X)

#if __Use_Original_Qemu != 1 /* ours (U40) */
/*
 * MXCSR bits this CPU implements; FXSAVE/XSAVE report it as MXCSR_MASK.
 * Writing 1 to any other (reserved) bit with LDMXCSR, VLDMXCSR, FXRSTOR or
 * XRSTOR is #GP(0) and nothing is loaded (Intel SDM Vol1 10.5.1.2 / 13.8.1,
 * Vol2 LDMXCSR, FXRSTOR, XRSTOR "Exceptions").  Upstream QEMU (11.1) does
 * not check this; NoVmp change (ledger U40).
 */
#define MXCSR_MASK 0x0000ffff

static void check_mxcsr(CPUX86State *env, uint32_t mxcsr, uintptr_t ra)
{
    if (mxcsr & ~MXCSR_MASK) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
}

#endif /* __Use_Original_Qemu (U40) */
static void do_xsave_fpu(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int fpus, fptag, i;
    target_ulong addr;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i = 0; i < 8; i++) {
        fptag |= (env->fptags[i] << i);
    }

    cpu_stw_data_ra(env, ptr + XO(legacy.fcw), env->fpuc, ra);
    cpu_stw_data_ra(env, ptr + XO(legacy.fsw), fpus, ra);
    cpu_stw_data_ra(env, ptr + XO(legacy.ftw), fptag ^ 0xff, ra);

#if __Use_Original_Qemu == 1 /* original QEMU (U64) */
    /* In 32-bit mode this is eip, sel; in 64-bit mode this is rip. */
    cpu_stq_data_ra(env, ptr + XO(legacy.fpip), env->fpip, ra);
    cpu_stq_data_ra(env, ptr + XO(legacy.fpdp), 0, ra); /* edp+sel; rdp */
#else /* ours (U64) */
    /*
     * NoVmp (ledger U64): FOP, FIP and FDP as the i5-13600K stores them; REX.W
     * gives 64-bit pointers, otherwise 32-bit offset + selector (0, deprecated)
     */
    cpu_stw_data_ra(env, ptr + XO(legacy.fpop), env->fpop & 0x7ff, ra);
    if (env->x87_fx64) {
        cpu_stq_data_ra(env, ptr + XO(legacy.fpip), env->fpip, ra);
        cpu_stq_data_ra(env, ptr + XO(legacy.fpdp), env->fpdp, ra);
    } else {
        cpu_stl_data_ra(env, ptr + XO(legacy.fpip), (uint32_t)env->fpip, ra);
        cpu_stl_data_ra(env, ptr + XO(legacy.fpip) + 4, x87_saved_sel(env, env->fpcs), ra);
        cpu_stl_data_ra(env, ptr + XO(legacy.fpdp), (uint32_t)env->fpdp, ra);
        cpu_stl_data_ra(env, ptr + XO(legacy.fpdp) + 4, x87_saved_sel(env, env->fpds), ra);
    }
#endif /* __Use_Original_Qemu (U64) */

    addr = ptr + XO(legacy.fpregs);
    for (i = 0; i < 8; i++) {
        floatx80 tmp = ST(i);
        do_fstt(env, tmp, addr, ra);
        addr += 16;
    }
}

static void do_xsave_mxcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    update_mxcsr_from_sse_status(env);
    cpu_stl_data_ra(env, ptr + XO(legacy.mxcsr), env->mxcsr, ra);
#if __Use_Original_Qemu == 1 /* original QEMU (U40) */
    cpu_stl_data_ra(env, ptr + XO(legacy.mxcsr_mask), 0x0000ffff, ra);
#else /* ours (U40) */
    cpu_stl_data_ra(env, ptr + XO(legacy.mxcsr_mask), MXCSR_MASK, ra);
#endif /* __Use_Original_Qemu (U40) */
}

static void do_xsave_sse(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;
    target_ulong addr;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    addr = ptr + XO(legacy.xmm_regs);
    for (i = 0; i < nb_xmm_regs; i++) {
        cpu_stq_data_ra(env, addr, env->xmm_regs[i].ZMM_Q(0), ra);
        cpu_stq_data_ra(env, addr + 8, env->xmm_regs[i].ZMM_Q(1), ra);
        addr += 16;
    }
}

static void do_xsave_ymmh(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    for (i = 0; i < nb_xmm_regs; i++, ptr += 16) {
        cpu_stq_data_ra(env, ptr, env->xmm_regs[i].ZMM_Q(2), ra);
        cpu_stq_data_ra(env, ptr + 8, env->xmm_regs[i].ZMM_Q(3), ra);
    }
}

static void do_xsave_bndregs(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    target_ulong addr = ptr + offsetof(XSaveBNDREG, bnd_regs);
    int i;

    for (i = 0; i < 4; i++, addr += 16) {
        cpu_stq_data_ra(env, addr, env->bnd_regs[i].lb, ra);
        cpu_stq_data_ra(env, addr + 8, env->bnd_regs[i].ub, ra);
    }
}

static void do_xsave_bndcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.cfgu),
                    env->bndcs_regs.cfgu, ra);
    cpu_stq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.sts),
                    env->bndcs_regs.sts, ra);
}

static void do_xsave_pkru(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stq_data_ra(env, ptr, env->pkru, ra);
}

#if __Use_Original_Qemu != 1 /* ours (U124) */
/*
 * NoVmp (ledger U124): AVX-512 state components (SDM Vol1 13.5.5, 13.13).
 *   5 opmask    (64 B): bytes 8i+7:8i = k[i].
 *   6 ZMM_Hi256 (512 B): bytes 32i+31:32i = ZMM[i] bits 511:256, i = 0..15; outside
 *                64-bit mode only ZMM0_H-ZMM7_H (bytes 255:0) are written / loaded.
 *   7 Hi16_ZMM  (1024 B): bytes 64(i-16)+63:64(i-16) = ZMM[i], i = 16..31; accessed
 *                only in 64-bit mode (outside it the section is neither written nor
 *                loaded and the registers are not initialised).
 */
static int xsave_nb_zmm_hi256(CPUX86State *env)
{
    return (env->hflags & HF_CS64_MASK) ? 16 : 8;
}

static void do_xsave_opmask(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < NB_OPMASK_REGS; i++) {
        cpu_stq_data_ra(env, ptr + 8 * i, env->opmask_regs[i], ra);
    }
}

static void do_xsave_zmm_hi256(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, j, n = xsave_nb_zmm_hi256(env);

    for (i = 0; i < n; i++) {
        for (j = 0; j < 4; j++) {
            cpu_stq_data_ra(env, ptr + 32 * i + 8 * j, env->xmm_regs[i].ZMM_Q(4 + j), ra);
        }
    }
}

static void do_xsave_hi16_zmm(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, j;

    if (!(env->hflags & HF_CS64_MASK)) {
        return;
    }
    for (i = 16; i < 32; i++) {
        for (j = 0; j < 8; j++) {
            cpu_stq_data_ra(env, ptr + 64 * (i - 16) + 8 * j, env->xmm_regs[i].ZMM_Q(j), ra);
        }
    }
}

static void do_xrstor_opmask(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < NB_OPMASK_REGS; i++) {
        env->opmask_regs[i] = cpu_ldq_data_ra(env, ptr + 8 * i, ra);
    }
}

static void do_xrstor_zmm_hi256(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, j, n = xsave_nb_zmm_hi256(env);

    for (i = 0; i < n; i++) {
        for (j = 0; j < 4; j++) {
            env->xmm_regs[i].ZMM_Q(4 + j) = cpu_ldq_data_ra(env, ptr + 32 * i + 8 * j, ra);
        }
    }
}

static void do_xrstor_hi16_zmm(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, j;

    if (!(env->hflags & HF_CS64_MASK)) {
        return;
    }
    for (i = 16; i < 32; i++) {
        for (j = 0; j < 8; j++) {
            env->xmm_regs[i].ZMM_Q(j) = cpu_ldq_data_ra(env, ptr + 64 * (i - 16) + 8 * j, ra);
        }
    }
}

/* the initial configuration of each component is all zeroes */
static void do_clear_opmask(CPUX86State *env)
{
    memset(env->opmask_regs, 0, sizeof(env->opmask_regs));
}

static void do_clear_zmm_hi256(CPUX86State *env)
{
    int i, j, n = xsave_nb_zmm_hi256(env);

    for (i = 0; i < n; i++) {
        for (j = 4; j < 8; j++) {
            env->xmm_regs[i].ZMM_Q(j) = 0;
        }
    }
}

static void do_clear_hi16_zmm(CPUX86State *env)
{
    if (env->hflags & HF_CS64_MASK) {
        memset(&env->xmm_regs[16], 0, 16 * sizeof(ZMMReg));
    }
}

/*
 * XINUSE policy for components 5-7 (SDM Vol1 13.6 allows XINUSE[i] = 1 in the initial
 * configuration; we choose the value-based form): opmask in use iff some k[i] != 0;
 * ZMM_Hi256 iff some ZMM_H of the registers the mode accesses != 0; Hi16_ZMM iff in
 * 64-bit mode and some ZMM16-31 != 0 ("outside 64-bit mode, Hi16_ZMM state is always
 * in its initial configuration"). Components 0-2 keep QEMU's "always in use".
 */
static uint64_t get_xinuse_avx512(CPUX86State *env)
{
    uint64_t inuse = 0, acc = 0;
    int i, j, n = xsave_nb_zmm_hi256(env);

    for (i = 0; i < NB_OPMASK_REGS; i++) {
        acc |= env->opmask_regs[i];
    }
    if (acc) {
        inuse |= XSTATE_OPMASK_MASK;
    }
    for (acc = 0, i = 0; i < n; i++) {
        for (j = 4; j < 8; j++) {
            acc |= env->xmm_regs[i].ZMM_Q(j);
        }
    }
    if (acc) {
        inuse |= XSTATE_ZMM_Hi256_MASK;
    }
    if (env->hflags & HF_CS64_MASK) {
        for (acc = 0, i = 16; i < 32; i++) {
            for (j = 0; j < 8; j++) {
                acc |= env->xmm_regs[i].ZMM_Q(j);
            }
        }
        if (acc) {
            inuse |= XSTATE_Hi16_ZMM_MASK;
        }
    }
    return inuse;
}
#endif /* __Use_Original_Qemu (U124) */

#if __Use_Original_Qemu != 1 /* ours (U172) */
/*
 * NoVmp (ledger U172): AMX state components (SDM Vol1 13.5.14, 19.4; Vol2A LDTILECFG).
 *   17 TILECFG  (64 B): env->xtilecfg, the LDTILECFG/STTILECFG memory layout - byte 0
 *                palette_id, 1 start_row, 16+2n colsb[n] (word), 48+n rows[n], the rest
 *                zero. All zero is the INIT state (TILES_CONFIGURED = 0); any other value
 *                held here passed amx_tilecfg_valid with palette_id 1.
 *   18 TILEDATA (8 KB): env->xtiledata, TMMn row r byte j at 1024n + 64r + j.
 * XSAVE* always save all 8 KB of TILEDATA and XRSTOR* always load all 8 KB, whatever
 * TILECFG says; XRSTOR* initialise TILECFG instead of faulting when the image is not a
 * configuration LDTILECFG would accept, and never touch TILEDATA when only TILECFG is
 * loaded.
 */
#define AMX_P1_MAX_NAMES     8      /* palette 1 (CPUID.(1DH,1)) */
#define AMX_P1_BYTES_PER_ROW 64
#define AMX_P1_MAX_ROWS      16

/* LDTILECFG's consistency checks on a 64-byte image (true = it would not #GP) */
bool x86_amx_tilecfg_ok(const uint8_t *buf, uint64_t xcr0)
{
    int n;

    if (buf[0] > 1) {                       /* palette_id > max_palette (1DH.0:EAX) */
        return false;
    }
    if (buf[0] == 1 && (xcr0 & XSTATE_AMX_MASK) != XSTATE_AMX_MASK) {
        return false;                       /* not xcr0_supports_palette(1) */
    }
    if (buf[0] == 0) {
        return true;                        /* INIT: the other bytes are not examined */
    }
    for (n = 2; n < 16; n++) {
        if (buf[n]) {
            return false;
        }
    }
    for (n = 0; n < AMX_P1_MAX_NAMES; n++) {
        if (lduw_le_p(buf + 16 + 2 * n) > AMX_P1_BYTES_PER_ROW) {
            return false;
        }
    }
    for (n = 16 + 2 * AMX_P1_MAX_NAMES; n < 48; n++) {
        if (buf[n]) {
            return false;
        }
    }
    for (n = 0; n < AMX_P1_MAX_NAMES; n++) {
        if (buf[48 + n] > AMX_P1_MAX_ROWS) {
            return false;
        }
    }
    for (n = 48 + AMX_P1_MAX_NAMES; n < 64; n++) {
        if (buf[n]) {
            return false;
        }
    }
    for (n = 0; n < AMX_P1_MAX_NAMES; n++) {
        /* a tile is valid with rows and colsb both non-zero, unused with both zero */
        if ((buf[48 + n] == 0) != (lduw_le_p(buf + 16 + 2 * n) == 0)) {
            return false;
        }
    }
    return true;
}

static void do_xsave_tilecfg(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < 64; i += 8) {
        cpu_stq_data_ra(env, ptr + i, ldq_le_p(env->xtilecfg + i), ra);
    }
}

static void do_xsave_tiledata(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < (int)sizeof(env->xtiledata); i += 8) {
        cpu_stq_data_ra(env, ptr + i, ldq_le_p(env->xtiledata + i), ra);
    }
}

static void do_xrstor_tilecfg(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    uint8_t buf[64];
    int i;

    for (i = 0; i < 64; i += 8) {
        stq_le_p(buf + i, cpu_ldq_data_ra(env, ptr + i, ra));
    }
    if (buf[0] != 0 && x86_amx_tilecfg_ok(buf, env->xcr0)) {
        memcpy(env->xtilecfg, buf, sizeof(env->xtilecfg));
    } else {
        memset(env->xtilecfg, 0, sizeof(env->xtilecfg));   /* TILES_CONFIGURED = 0 */
    }
}

static void do_xrstor_tiledata(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < (int)sizeof(env->xtiledata); i += 8) {
        stq_le_p(env->xtiledata + i, cpu_ldq_data_ra(env, ptr + i, ra));
    }
}

static void do_clear_tilecfg(CPUX86State *env)
{
    memset(env->xtilecfg, 0, sizeof(env->xtilecfg));
}

static void do_clear_tiledata(CPUX86State *env)
{
    memset(env->xtiledata, 0, sizeof(env->xtiledata));
}

/*
 * XINUSE for components 17-18, value-based like U124 (SDM Vol1 13.6): TILECFG in use iff
 * TILES_CONFIGURED, TILEDATA iff some byte of it is non-zero.
 */
static uint64_t get_xinuse_amx(CPUX86State *env)
{
    uint64_t inuse = 0;
    size_t i;

    if (env->xtilecfg[0]) {
        inuse |= XSTATE_XTILE_CFG_MASK;
    }
    for (i = 0; i < sizeof(env->xtiledata); i += 8) {
        if (ldq_le_p(env->xtiledata + i)) {
            inuse |= XSTATE_XTILE_DATA_MASK;
            break;
        }
    }
    return inuse;
}
#endif /* __Use_Original_Qemu (U172) */
#if __Use_Original_Qemu != 1 /* ours (U612) */
/*
 * NoVmp (ledger U612): Intel APX state component 19 (APX spec 355828-009 3.1.4.3.3, Table
 * 3.9): R16-R31 as 16 quadwords, R16 at offset 0. The XSAVE family saves and restores them in
 * every mode ("XSAVE/XRSTOR behavior for EGPRs has no modal specialization", 3.1.4.1.2).
 */
static void do_xsave_apx(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < 16; i++) {
        cpu_stq_data_ra(env, ptr + 8 * i, env->regs[16 + i], ra);
    }
}

static void do_xrstor_apx(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < 16; i++) {
        env->regs[16 + i] = cpu_ldq_data_ra(env, ptr + 8 * i, ra);
    }
}

/* initial configuration: all EGPRs 0 (3.1.4.1.2) */
static void do_clear_apx(CPUX86State *env)
{
    memset(&env->regs[16], 0, 16 * sizeof(env->regs[0]));
}

/*
 * XINUSE[19], value-based like U124/U172: "EGPR state (R16-R31) are considered to be in INIT
 * state if all of the registers have the value 0x0. XINUSE = 0 when this condition is met"
 * (3.1.4.1.2).
 */
static uint64_t get_xinuse_apx(CPUX86State *env)
{
    target_ulong acc = 0;
    int i;

    for (i = 16; i < 32; i++) {
        acc |= env->regs[i];
    }
    return acc ? XSTATE_APX_MASK : 0;
}
#endif /* __Use_Original_Qemu (U612) */
#if __Use_Original_Qemu != 1 /* ours (U756) */
/*
 * NoVmp (ledger U756): CET supervisor state components (SDM Vol1 13.5.9), managed only by
 * XSAVES / XRSTORS (IA32_XSS[12:11]):
 *   11 CET_U (16 bytes): bytes 7:0 IA32_U_CET, bytes 15:8 IA32_PL3_SSP;
 *   12 CET_S (24 bytes): bytes 8i+7:8i IA32_PLi_SSP, i = 0..2.
 * IA32_S_CET and IA32_INTERRUPT_SSP_TABLE_ADDR are not XSAVE-managed (13.5.9, footnote).
 * Initial configuration (13.6): all of the component's MSRs 0; XINUSE is value-based
 * like U124/U172/U612. XRSTORS checks every value it would load first (cet_xrstor_check,
 * #GP(0) on a value WRMSR refuses) and then loads them as WRMSR does.
 */
static uint64_t get_xinuse_cet(CPUX86State *env)
{
    uint64_t r = 0;

    if (env->u_cet | env->pl_ssp[3]) {
        r |= XSTATE_CET_U_MASK;
    }
    if (env->pl_ssp[0] | env->pl_ssp[1] | env->pl_ssp[2]) {
        r |= XSTATE_CET_S_MASK;
    }
    return r;
}

static void do_xsave_cet_u(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stq_data_ra(env, ptr, env->u_cet, ra);
    cpu_stq_data_ra(env, ptr + 8, env->pl_ssp[3], ra);
}

static void do_xsave_cet_s(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < 3; i++) {
        cpu_stq_data_ra(env, ptr + 8 * i, env->pl_ssp[i], ra);
    }
}

/* XRSTORS: #GP(0) before anything is loaded when a value would be refused by WRMSR */
static void cet_xrstor_check(CPUX86State *env, uint64_t restore, target_ulong at_u,
                             target_ulong at_s, uintptr_t ra)
{
    bool ok = true;
    int i;

    if (restore & XSTATE_CET_U_MASK) {
        ok &= x86_cet_msr_ok(env, MSR_IA32_U_CET, cpu_ldq_data_ra(env, at_u, ra));
        ok &= x86_cet_msr_ok(env, MSR_IA32_PL3_SSP, cpu_ldq_data_ra(env, at_u + 8, ra));
    }
    if (restore & XSTATE_CET_S_MASK) {
        for (i = 0; i < 3; i++) {
            ok &= x86_cet_msr_ok(env, MSR_IA32_PL0_SSP + i,
                                 cpu_ldq_data_ra(env, at_s + 8 * i, ra));
        }
    }
    if (!ok) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
}

static void do_xrstor_cet_u(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    x86_cet_msr_load(env, MSR_IA32_U_CET, cpu_ldq_data_ra(env, ptr, ra));
    x86_cet_msr_load(env, MSR_IA32_PL3_SSP, cpu_ldq_data_ra(env, ptr + 8, ra));
}

static void do_xrstor_cet_s(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i;

    for (i = 0; i < 3; i++) {
        x86_cet_msr_load(env, MSR_IA32_PL0_SSP + i, cpu_ldq_data_ra(env, ptr + 8 * i, ra));
    }
}

static void do_clear_cet_u(CPUX86State *env)
{
    env->u_cet = 0;
    env->pl_ssp[3] = 0;
    cpu_sync_cet_hflags(env);
}

static void do_clear_cet_s(CPUX86State *env)
{
    env->pl_ssp[0] = env->pl_ssp[1] = env->pl_ssp[2] = 0;
}
#endif /* __Use_Original_Qemu (U756) */

static void do_fxsave(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
#if __Use_Original_Qemu != 1 /* ours (U834) */
    /*
     * NoVmp (ledger U834): a misaligned FXSAVE / FXRSTOR / XSAVE* / XRSTOR operand is #GP(0), or
     * with alignment checking #AC: "signaling of #AC is not guaranteed and may vary with
     * implementation ... #AC might be signaled for a 2-byte misalignment, whereas a general
     * protection exception might be signaled for all other misalignments" (SDM Vol2A FXSAVE,
     * Vol2D XSAVE). Implementation choice, the i5-13600K's (cases_ac_hw): #AC when the address
     * is not 4-byte aligned, #GP when it is but not 16 / 64-byte aligned.
     */
    x86_ac_check(env, ptr, 3, ra);
#endif /* __Use_Original_Qemu (U834) */
    /* The operand must be 16 byte aligned */
    if (ptr & 0xf) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* backport 94f60f8f1c (X86Access, U480): the 512-byte image or nothing */
    x86_access_prepare(env, ptr, sizeof(X86LegacyXSaveArea), MMU_DATA_STORE, ra);
    do_xsave_fpu(env, ptr, ra);

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        do_xsave_mxcsr(env, ptr, ra);
        /* Fast FXSAVE leaves out the XMM registers */
        if (!(env->efer & MSR_EFER_FFXSR)
            || (env->hflags & HF_CPL_MASK)
            || !(env->hflags & HF_LMA_MASK)) {
            do_xsave_sse(env, ptr, ra);
        }
    }
}

void helper_fxsave(CPUX86State *env, target_ulong ptr)
{
    do_fxsave(env, ptr, GETPC());
}

static uint64_t get_xinuse(CPUX86State *env)
{
    uint64_t inuse = -1;

    /* For the most part, we don't track XINUSE.  We could calculate it
       here for all components, but it's probably less work to simply
       indicate in use.  That said, the state of BNDREGS is important
       enough to track in HFLAGS, so we might as well use that here.  */
    if ((env->hflags & HF_MPX_IU_MASK) == 0) {
       inuse &= ~XSTATE_BNDREGS_MASK;
    }
#if __Use_Original_Qemu != 1 /* ours (U124) */
    /* AVX-512 components: value-based (see get_xinuse_avx512) */
    inuse &= ~(XSTATE_OPMASK_MASK | XSTATE_ZMM_Hi256_MASK | XSTATE_Hi16_ZMM_MASK);
    inuse |= get_xinuse_avx512(env);
#endif /* __Use_Original_Qemu (U124) */
#if __Use_Original_Qemu != 1 /* ours (U172) */
    /* AMX components: value-based (see get_xinuse_amx) */
    inuse &= ~XSTATE_AMX_MASK;
    inuse |= get_xinuse_amx(env);
#endif /* __Use_Original_Qemu (U172) */
#if __Use_Original_Qemu != 1 /* ours (U612) */
    /* APX EGPRs: value-based (see get_xinuse_apx) */
    inuse &= ~XSTATE_APX_MASK;
    inuse |= get_xinuse_apx(env);
#endif /* __Use_Original_Qemu (U612) */
#if __Use_Original_Qemu != 1 /* ours (U756) */
    /* CET_U / CET_S: value-based (see get_xinuse_cet) */
    inuse &= ~(XSTATE_CET_U_MASK | XSTATE_CET_S_MASK);
    inuse |= get_xinuse_cet(env);
#endif /* __Use_Original_Qemu (U756) */
    return inuse;
}

/*
 * backport c6e6d1508a (X86Access, U480): end of the standard-format XSAVE area holding the
 * components of 'mask' (upstream xsave_area_size(mask, false)): legacy area and header,
 * then the end of the highest component.
 */
static target_ulong xsave_std_end(uint64_t mask)
{
    target_ulong end = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader);
    int i;

    for (i = 2; i < XSAVE_STATE_AREA_COUNT; i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];

        if (((mask >> i) & 1) && esa->size && esa->offset + esa->size > end) {
            end = esa->offset + esa->size;
        }
    }
    return end;
}

static void do_xsave(CPUX86State *env, target_ulong ptr, uint64_t rfbm,
                     uint64_t inuse, uint64_t opt, uintptr_t ra)
{
    uint64_t old_bv, new_bv;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }

#if __Use_Original_Qemu != 1 /* ours (U834) */
    x86_ac_check(env, ptr, 3, ra);      /* #AC before #GP, as do_fxsave (U834) */
#endif /* __Use_Original_Qemu (U834) */
    /* The operand must be 64 byte aligned.  */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

#if __Use_Original_Qemu != 1 /* ours (U173) */
    /*
     * NoVmp (ledger U173): SDM Vol1 13.14 - with XCR0[i] = IA32_XFD[i] = 1 the XSAVE family
     * does not #NM but "operates as if XINUSE[i] = 0": XSTATE_BV[i] = 0; XSAVE saves the
     * initial configuration, XSAVEOPT (opt != -1) does not save the component.
     */
    const uint64_t xfd = x86_cpu_xfd_armed(env);

    inuse &= ~xfd;
    if (opt != (uint64_t)-1) {
        opt &= ~xfd;
    }
#endif /* __Use_Original_Qemu (U173) */
    /* Never save anything not enabled by XCR0.  */
    rfbm &= env->xcr0;
    opt &= rfbm;

    /* backport c6e6d1508a (X86Access, U480): the whole area or nothing */
    x86_access_prepare(env, ptr, xsave_std_end(opt), MMU_DATA_STORE, ra);
    if (opt & XSTATE_FP_MASK) {
        do_xsave_fpu(env, ptr, ra);
    }
#if __Use_Original_Qemu == 1 /* original QEMU (U40) */
    if (rfbm & XSTATE_SSE_MASK) {
        /* Note that saving MXCSR is not suppressed by XSAVEOPT.  */
        do_xsave_mxcsr(env, ptr, ra);
    }
#else /* ours (U40) */
    if (rfbm & (XSTATE_SSE_MASK | XSTATE_YMM_MASK)) {
        /* Note that saving MXCSR is not suppressed by XSAVEOPT.  SDM Vol1
           13.7: XSAVE also saves MXCSR/MXCSR_MASK when RFBM[2] = 1, even
           if RFBM[1] = 0.  */
        do_xsave_mxcsr(env, ptr, ra);
    }
#endif /* __Use_Original_Qemu (U40) */
    if (opt & XSTATE_SSE_MASK) {
        do_xsave_sse(env, ptr, ra);
    }
    if (opt & XSTATE_YMM_MASK) {
        do_xsave_ymmh(env, ptr + XO(avx_state), ra);
    }
    if (opt & XSTATE_BNDREGS_MASK) {
        do_xsave_bndregs(env, ptr + XO(bndreg_state), ra);
    }
    if (opt & XSTATE_BNDCSR_MASK) {
        do_xsave_bndcsr(env, ptr + XO(bndcsr_state), ra);
    }
#if __Use_Original_Qemu != 1 /* ours (U124) */
    if (opt & XSTATE_OPMASK_MASK) {
        do_xsave_opmask(env, ptr + XO(opmask_state), ra);
    }
    if (opt & XSTATE_ZMM_Hi256_MASK) {
        do_xsave_zmm_hi256(env, ptr + XO(zmm_hi256_state), ra);
    }
    if (opt & XSTATE_Hi16_ZMM_MASK) {
        do_xsave_hi16_zmm(env, ptr + XO(hi16_zmm_state), ra);
    }
#endif /* __Use_Original_Qemu (U124) */
    if (opt & XSTATE_PKRU_MASK) {
        do_xsave_pkru(env, ptr + XO(pkru_state), ra);
    }
#if __Use_Original_Qemu != 1 /* ours (U172) */
    if (opt & XSTATE_XTILE_CFG_MASK) {
        do_xsave_tilecfg(env, ptr + x86_ext_save_areas[XSTATE_XTILE_CFG_BIT].offset, ra);
    }
    if (opt & XSTATE_XTILE_DATA_MASK) {
#if __Use_Original_Qemu != 1 /* ours (U173) */
        if (xfd & XSTATE_XTILE_DATA_MASK) {
            /* XFD: XSAVE stores the initial configuration (all zero) */
            target_ulong at = ptr + x86_ext_save_areas[XSTATE_XTILE_DATA_BIT].offset;
            int i;
            for (i = 0; i < (int)sizeof(env->xtiledata); i += 8) {
                cpu_stq_data_ra(env, at + i, 0, ra);
            }
        } else
#endif /* __Use_Original_Qemu (U173) */
        do_xsave_tiledata(env, ptr + x86_ext_save_areas[XSTATE_XTILE_DATA_BIT].offset, ra);
    }
#endif /* __Use_Original_Qemu (U172) */
#if __Use_Original_Qemu != 1 /* ours (U612) */
    if (opt & XSTATE_APX_MASK) {
        do_xsave_apx(env, ptr + x86_ext_save_areas[XSTATE_APX_BIT].offset, ra);
    }
#endif /* __Use_Original_Qemu (U612) */

    /* Update the XSTATE_BV field.  */
    old_bv = cpu_ldq_data_ra(env, ptr + XO(header.xstate_bv), ra);
    new_bv = (old_bv & ~rfbm) | (inuse & rfbm);
    cpu_stq_data_ra(env, ptr + XO(header.xstate_bv), new_bv, ra);
}

void helper_xsave(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    do_xsave(env, ptr, rfbm, get_xinuse(env), -1, GETPC());
}

void helper_xsaveopt(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    uint64_t inuse = get_xinuse(env);
    do_xsave(env, ptr, rfbm, inuse, inuse, GETPC());
}

#if __Use_Original_Qemu != 1 /* ours (U66) */
/*
 * NoVmp (ledger U66): XSAVEC and the compacted form of XRSTOR (SDM Vol2
 * XSAVEC/XRSTOR "Operation", Vol1 13.4.3/13.8.2/13.10; QEMU 7.2 and 11.1
 * have neither; i5-13600K: XSAVEC with EDX:EAX = 3 writes XSTATE_BV = 3,
 * XCOMP_BV = 8000000000000003h). Components 2..62 are packed from offset 576
 * in bit order (none of the components implemented here requires 64-byte
 * alignment, CPUID.(EAX=0DH,ECX=i):ECX[1] = 0). XINUSE as for XSAVE.
 */
static uint64_t xsave_comp_size(int i)
{
    return i >= 2 && i < XSAVE_STATE_AREA_COUNT ? x86_ext_save_areas[i].size : 0;
}

static void do_xsave_comp(CPUX86State *env, int i, target_ulong at, uintptr_t ra)
{
    switch (i) {
    case XSTATE_YMM_BIT:     do_xsave_ymmh(env, at, ra); break;
    case XSTATE_BNDREGS_BIT: do_xsave_bndregs(env, at, ra); break;
    case XSTATE_BNDCSR_BIT:  do_xsave_bndcsr(env, at, ra); break;
    case XSTATE_PKRU_BIT:    do_xsave_pkru(env, at, ra); break;
#if __Use_Original_Qemu != 1 /* ours (U124) */
    case XSTATE_OPMASK_BIT:    do_xsave_opmask(env, at, ra); break;
    case XSTATE_ZMM_Hi256_BIT: do_xsave_zmm_hi256(env, at, ra); break;
    case XSTATE_Hi16_ZMM_BIT:  do_xsave_hi16_zmm(env, at, ra); break;
#endif /* __Use_Original_Qemu (U124) */
#if __Use_Original_Qemu != 1 /* ours (U172) */
    case XSTATE_XTILE_CFG_BIT:  do_xsave_tilecfg(env, at, ra); break;
    case XSTATE_XTILE_DATA_BIT: do_xsave_tiledata(env, at, ra); break;
#endif /* __Use_Original_Qemu (U172) */
#if __Use_Original_Qemu != 1 /* ours (U612) */
    case XSTATE_APX_BIT:        do_xsave_apx(env, at, ra); break;
#endif /* __Use_Original_Qemu (U612) */
#if __Use_Original_Qemu != 1 /* ours (U756) */
    case XSTATE_CET_U_BIT:      do_xsave_cet_u(env, at, ra); break;
    case XSTATE_CET_S_BIT:      do_xsave_cet_s(env, at, ra); break;
#endif /* __Use_Original_Qemu (U756) */
    default:                 break;
    }
}

#if __Use_Original_Qemu != 1 /* ours (U172) */
/*
 * Compacted format (SDM Vol1 13.4.3): a component with CPUID.(EAX=0DH,ECX=i):ECX[1] = 1
 * starts on the next 64-byte boundary after the preceding one (the AMX components, U170).
 */
static target_ulong xsave_comp_align(int i, target_ulong next)
{
    if (i < XSAVE_STATE_AREA_COUNT &&
        (x86_ext_save_areas[i].ecx & ESA_FEATURE_ALIGN64_MASK)) {
        next = QEMU_ALIGN_UP(next, 64);
    }
    return next;
}
#endif /* __Use_Original_Qemu (U172) */

/* U726: ena = XCR0 (XSAVEC) or XCR0 | IA32_XSS (XSAVES); RFBM = EDX:EAX AND ena */
static void do_xsavec(CPUX86State *env, target_ulong ptr, uint64_t rfbm,
                      uint64_t inuse, uint64_t ena, uintptr_t ra)
{
    uint64_t save;
    target_ulong next = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader);
    int i;

    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
#if __Use_Original_Qemu != 1 /* ours (U834) */
    x86_ac_check(env, ptr, 3, ra);      /* #AC before #GP, as do_fxsave (U834) */
#endif /* __Use_Original_Qemu (U834) */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    rfbm &= ena;
#if __Use_Original_Qemu != 1 /* ours (U173) */
    /* XFD-disabled components: as if XINUSE[i] = 0, not saved (SDM Vol1 13.14) */
    inuse &= ~x86_cpu_xfd_armed(env);
#endif /* __Use_Original_Qemu (U173) */
    save = rfbm & inuse;
    if ((rfbm & XSTATE_SSE_MASK) && env->mxcsr != 0x1f80) {
        save |= XSTATE_SSE_MASK;
    }
    {
        /* U480: the compacted area up to the last saved component, or nothing */
        target_ulong at = next, end = next;

        for (i = 2; i < 63; i++) {
            if (rfbm & (1ULL << i)) {
                at = xsave_comp_align(i, at);
                if (save & (1ULL << i)) {
                    end = at + xsave_comp_size(i);
                }
                at += xsave_comp_size(i);
            }
        }
        x86_access_prepare(env, ptr, end, MMU_DATA_STORE, ra);
    }
    if (save & XSTATE_FP_MASK) {
        do_xsave_fpu(env, ptr, ra);
    }
    if (save & XSTATE_SSE_MASK) {
        /* SSE state: XMM registers, MXCSR and MXCSR_MASK */
        do_xsave_mxcsr(env, ptr, ra);
        do_xsave_sse(env, ptr, ra);
    }
    for (i = 2; i < 63; i++) {
        if (rfbm & (1ULL << i)) {
#if __Use_Original_Qemu != 1 /* ours (U172) */
            next = xsave_comp_align(i, next);
#endif /* __Use_Original_Qemu (U172) */
            if (save & (1ULL << i)) {
                do_xsave_comp(env, i, ptr + next, ra);
            }
            next += xsave_comp_size(i);
        }
    }
    cpu_stq_data_ra(env, ptr + XO(header.xstate_bv), save, ra);
    cpu_stq_data_ra(env, ptr + XO(header.xcomp_bv), rfbm | (1ULL << 63), ra);
}

void helper_xsavec(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    do_xsavec(env, ptr, rfbm, get_xinuse(env), env->xcr0, GETPC());
}
#endif /* __Use_Original_Qemu (U66) */
#if __Use_Original_Qemu != 1 /* ours (U726) */
/*
 * NoVmp (ledger U726): XSAVES (SDM Vol1 13.11, Vol2D XSAVES): XSAVEC's operation (compacted
 * format, XCOMP_BV = 8000000000000000h | RFBM, XSTATE_BV[i] = XINUSE[i] for RFBM[i] = 1, else
 * 0; the init optimisation, XSTATE_BV[1] = 1 when MXCSR != 1F80h) with RFBM = EDX:EAX AND
 * (XCR0 | IA32_XSS), so it also saves the supervisor state components enabled in IA32_XSS.
 * #UD CR4.OSXSAVE = 0 (the CPUID / LOCK / prefix #UDs and CR0.TS #NM are the translator's),
 * then #GP(0) if CPL > 0, then #GP(0) on a misaligned area. XFD-enabled components are saved
 * as if XINUSE[i] = 0 (13.14). The modified optimisation is not implemented: XMODIFIED is all
 * ones, which 13.6 allows ("a processor that does not do so implicitly maintains
 * XMODIFIED[i] = 1"), so no XRSTOR_INFO is kept. Supervisor state components are laid out by
 * xsave_comp_size / do_xsave_comp like the user ones; U756 adds CET_U / CET_S (IA32_XSS[12:11]).
 */
void helper_xsaves(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    uintptr_t ra = GETPC();

    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (env->hflags & HF_CPL_MASK) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    do_xsavec(env, ptr, rfbm, get_xinuse(env), env->xcr0 | env->xss, ra);
}
#endif /* __Use_Original_Qemu (U726) */

static void do_xrstor_fpu(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, fpuc, fpus, fptag;
    target_ulong addr;

    fpuc = cpu_lduw_data_ra(env, ptr + XO(legacy.fcw), ra);
    fpus = cpu_lduw_data_ra(env, ptr + XO(legacy.fsw), ra);
    fptag = cpu_lduw_data_ra(env, ptr + XO(legacy.ftw), ra);
    cpu_set_fpuc(env, fpuc);
    cpu_set_fpus(env, fpus);
    fptag ^= 0xff;
    for (i = 0; i < 8; i++) {
        env->fptags[i] = ((fptag >> i) & 1);
    }
#if __Use_Original_Qemu != 1 /* ours (U64) */
    /* NoVmp (ledger U64): FOP, FIP, FDP restored (REX.W: 64-bit pointers) */
    env->fpop = cpu_lduw_data_ra(env, ptr + XO(legacy.fpop), ra) & 0x7ff;
    if (env->x87_fx64) {
        env->fpip = cpu_ldq_data_ra(env, ptr + XO(legacy.fpip), ra);
        env->fpdp = cpu_ldq_data_ra(env, ptr + XO(legacy.fpdp), ra);
        env->fpcs = env->fpds = 0;      /* REX.W: "clears FCS and FDS" (Vol1 8.1.8) */
    } else {
        env->fpip = cpu_ldl_data_ra(env, ptr + XO(legacy.fpip), ra);
        env->fpdp = cpu_ldl_data_ra(env, ptr + XO(legacy.fpdp), ra);
        /* "loads FCS and FDS from memory" unless deprecated (U860) */
        x87_load_sels(env, cpu_lduw_data_ra(env, ptr + XO(legacy.fpip) + 4, ra),
                      cpu_lduw_data_ra(env, ptr + XO(legacy.fpdp) + 4, ra), true);
    }
#endif /* __Use_Original_Qemu (U64) */

    addr = ptr + XO(legacy.fpregs);
    for (i = 0; i < 8; i++) {
        floatx80 tmp = do_fldt(env, addr, ra);
        ST(i) = tmp;
        addr += 16;
    }
}

static void do_xrstor_mxcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_set_mxcsr(env, cpu_ldl_data_ra(env, ptr + XO(legacy.mxcsr), ra));
}

static void do_xrstor_sse(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;
    target_ulong addr;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    addr = ptr + XO(legacy.xmm_regs);
    for (i = 0; i < nb_xmm_regs; i++) {
        env->xmm_regs[i].ZMM_Q(0) = cpu_ldq_data_ra(env, addr, ra);
        env->xmm_regs[i].ZMM_Q(1) = cpu_ldq_data_ra(env, addr + 8, ra);
        addr += 16;
    }
}

static void do_clear_sse(CPUX86State *env)
{
    int i, nb_xmm_regs;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    for (i = 0; i < nb_xmm_regs; i++) {
        env->xmm_regs[i].ZMM_Q(0) = 0;
        env->xmm_regs[i].ZMM_Q(1) = 0;
    }
}

static void do_xrstor_ymmh(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    for (i = 0; i < nb_xmm_regs; i++, ptr += 16) {
        env->xmm_regs[i].ZMM_Q(2) = cpu_ldq_data_ra(env, ptr, ra);
        env->xmm_regs[i].ZMM_Q(3) = cpu_ldq_data_ra(env, ptr + 8, ra);
    }
}

static void do_clear_ymmh(CPUX86State *env)
{
    int i, nb_xmm_regs;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    for (i = 0; i < nb_xmm_regs; i++) {
        env->xmm_regs[i].ZMM_Q(2) = 0;
        env->xmm_regs[i].ZMM_Q(3) = 0;
    }
}

static void do_xrstor_bndregs(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    target_ulong addr = ptr + offsetof(XSaveBNDREG, bnd_regs);
    int i;

    for (i = 0; i < 4; i++, addr += 16) {
        env->bnd_regs[i].lb = cpu_ldq_data_ra(env, addr, ra);
        env->bnd_regs[i].ub = cpu_ldq_data_ra(env, addr + 8, ra);
    }
}

static void do_xrstor_bndcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    /* FIXME: Extend highest implemented bit of linear address.  */
    env->bndcs_regs.cfgu
        = cpu_ldq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.cfgu), ra);
    env->bndcs_regs.sts
        = cpu_ldq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.sts), ra);
}

static void do_xrstor_pkru(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    env->pkru = cpu_ldq_data_ra(env, ptr, ra);
}

static void do_fxrstor(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
#if __Use_Original_Qemu != 1 /* ours (U834) */
    x86_ac_check(env, ptr, 3, ra);      /* #AC before #GP, as do_fxsave (U834) */
#endif /* __Use_Original_Qemu (U834) */
    /* The operand must be 16 byte aligned */
    if (ptr & 0xf) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* backport 94f60f8f1c (X86Access, U480): nothing is loaded when any byte faults */
    x86_access_prepare(env, ptr, sizeof(X86LegacyXSaveArea), MMU_DATA_LOAD, ra);
#if __Use_Original_Qemu != 1 /* ours (U40) */
    /* Reserved MXCSR bits fault before any state is loaded.  */
    if (env->cr[4] & CR4_OSFXSR_MASK) {
        check_mxcsr(env, cpu_ldl_data_ra(env, ptr + XO(legacy.mxcsr), ra), ra);
    }

#endif /* __Use_Original_Qemu (U40) */
    do_xrstor_fpu(env, ptr, ra);

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        do_xrstor_mxcsr(env, ptr, ra);
        /* Fast FXRSTOR leaves out the XMM registers */
        if (!(env->efer & MSR_EFER_FFXSR)
            || (env->hflags & HF_CPL_MASK)
            || !(env->hflags & HF_LMA_MASK)) {
            do_xrstor_sse(env, ptr, ra);
        }
    }
}

void helper_fxrstor(CPUX86State *env, target_ulong ptr)
{
    do_fxrstor(env, ptr, GETPC());
}

#if __Use_Original_Qemu != 1 /* ours (U173) */
/*
 * NoVmp (ledger U173): SDM Vol1 13.14 - XRSTOR loading component i from memory (RFBM[i] =
 * XSTATE_BV[i] = 1) with XCR0[i] = IA32_XFD[i] = 1 raises #NM before any state is
 * modified; IA32_XFD_ERR := IA32_XFD AND the components it would load. Initialising
 * the component (XSTATE_BV[i] = 0) does not fault.
 */
static void xrstor_check_xfd(CPUX86State *env, uint64_t load, uintptr_t ra)
{
    uint64_t xfd = load & x86_cpu_xfd_armed(env);

    if (xfd) {
        env->msr_xfd_err = xfd;
        raise_exception_ra(env, EXCP07_PREX, ra);
    }
}
#endif /* __Use_Original_Qemu (U173) */

#if __Use_Original_Qemu != 1 /* ours (U66) */
/*
 * NoVmp (ledger U66): the compacted form of XRSTOR, see do_xsavec. U726: also XRSTORS
 * (xrstors = true): the components of XCR0 | IA32_XSS instead of XCR0 (rfbm is already
 * EDX:EAX AND that), and no XSAVEC enumeration needed (XRSTORS has only this form).
 */
static void do_xrstor_compact(CPUX86State *env, target_ulong ptr, uint64_t rfbm,
                              uint64_t xstate_bv, bool xrstors, uintptr_t ra)
{
    uint64_t xcomp_bv = cpu_ldq_data_ra(env, ptr + XO(header.xcomp_bv), ra);
    uint64_t format = xcomp_bv & ~(1ULL << 63), restore, init;
    uint64_t ena = xrstors ? env->xcr0 | env->xss : env->xcr0;
    target_ulong next = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader);
    int i;

    if (!xrstors && !(env->features[FEAT_XSAVE] & CPUID_XSAVE_XSAVEC)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);        /* compacted form not supported */
    }
    /* SDM Vol1 13.8.2 / 13.12: XCOMP_BV[62:0] within XCR0 (| IA32_XSS), XSTATE_BV within
       XCOMP_BV[62:0], bytes 63:16 of the header zero; all checked before any state is loaded */
    if ((format & ~ena) || (xstate_bv & ~format)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    for (i = 16; i < 64; i += 8) {
        if (cpu_ldq_data_ra(env, ptr + sizeof(X86LegacyXSaveArea) + i, ra)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
    }
    restore = format & rfbm & xstate_bv;
    init = (rfbm & ~xstate_bv) | (rfbm & ~format);
    if (restore & XSTATE_SSE_MASK) {
        check_mxcsr(env, cpu_ldl_data_ra(env, ptr + XO(legacy.mxcsr), ra), ra);
    }
#if __Use_Original_Qemu != 1 /* ours (U173) */
    xrstor_check_xfd(env, restore, ra);
#endif /* __Use_Original_Qemu (U173) */
    {
#if __Use_Original_Qemu != 1 /* ours (U756) */
        target_ulong at_cet_u = 0, at_cet_s = 0;   /* CET_U / CET_S offsets (U756) */
#endif /* __Use_Original_Qemu (U756) */
        /* U480: nothing is loaded when any byte up to the last restored component faults */
        target_ulong at = next, end = next;

        for (i = 2; i < 63; i++) {
            if (format & (1ULL << i)) {
                at = xsave_comp_align(i, at);
                if (restore & (1ULL << i)) {
                    end = at + xsave_comp_size(i);
                }
#if __Use_Original_Qemu != 1 /* ours (U756) */
                if (i == XSTATE_CET_U_BIT) {
                    at_cet_u = ptr + at;
                } else if (i == XSTATE_CET_S_BIT) {
                    at_cet_s = ptr + at;
                }
#endif /* __Use_Original_Qemu (U756) */
                at += xsave_comp_size(i);
            }
        }
        x86_access_prepare(env, ptr, end, MMU_DATA_LOAD, ra);
#if __Use_Original_Qemu != 1 /* ours (U756) */
        cet_xrstor_check(env, restore, at_cet_u, at_cet_s, ra);
#endif /* __Use_Original_Qemu (U756) */
    }

    if (restore & XSTATE_FP_MASK) {
        do_xrstor_fpu(env, ptr, ra);
    } else if (init & XSTATE_FP_MASK) {
        do_fninit(env);
        memset(env->fpregs, 0, sizeof(env->fpregs));
    }
    if (restore & XSTATE_SSE_MASK) {
        do_xrstor_mxcsr(env, ptr, ra);
        do_xrstor_sse(env, ptr, ra);
    } else if (init & XSTATE_SSE_MASK) {
        do_clear_sse(env);
        cpu_set_mxcsr(env, 0x1f80);
    }
    for (i = 2; i < 63; i++) {
        uint64_t bit = 1ULL << i;
        if (format & bit) {
#if __Use_Original_Qemu != 1 /* ours (U172) */
            next = xsave_comp_align(i, next);
#endif /* __Use_Original_Qemu (U172) */
            if (restore & bit) {
                switch (i) {
                case XSTATE_YMM_BIT:
                    do_xrstor_ymmh(env, ptr + next, ra);
                    break;
                case XSTATE_BNDREGS_BIT:
                    do_xrstor_bndregs(env, ptr + next, ra);
                    env->hflags |= HF_MPX_IU_MASK;
                    break;
                case XSTATE_BNDCSR_BIT:
                    do_xrstor_bndcsr(env, ptr + next, ra);
                    cpu_sync_bndcs_hflags(env);
                    break;
                case XSTATE_PKRU_BIT: {
                    uint64_t old_pkru = env->pkru;
                    do_xrstor_pkru(env, ptr + next, ra);
                    if (env->pkru != old_pkru) {
                        tlb_flush(env_cpu(env));
                    }
                    break;
                }
#if __Use_Original_Qemu != 1 /* ours (U124) */
                case XSTATE_OPMASK_BIT:
                    do_xrstor_opmask(env, ptr + next, ra);
                    break;
                case XSTATE_ZMM_Hi256_BIT:
                    do_xrstor_zmm_hi256(env, ptr + next, ra);
                    break;
                case XSTATE_Hi16_ZMM_BIT:
                    do_xrstor_hi16_zmm(env, ptr + next, ra);
                    break;
#endif /* __Use_Original_Qemu (U124) */
#if __Use_Original_Qemu != 1 /* ours (U172) */
                case XSTATE_XTILE_CFG_BIT:
                    do_xrstor_tilecfg(env, ptr + next, ra);
                    break;
                case XSTATE_XTILE_DATA_BIT:
                    do_xrstor_tiledata(env, ptr + next, ra);
                    break;
#endif /* __Use_Original_Qemu (U172) */
#if __Use_Original_Qemu != 1 /* ours (U612) */
                case XSTATE_APX_BIT:
                    do_xrstor_apx(env, ptr + next, ra);
                    break;
#endif /* __Use_Original_Qemu (U612) */
#if __Use_Original_Qemu != 1 /* ours (U756) */
                case XSTATE_CET_U_BIT:
                    do_xrstor_cet_u(env, ptr + next, ra);
                    break;
                case XSTATE_CET_S_BIT:
                    do_xrstor_cet_s(env, ptr + next, ra);
                    break;
#endif /* __Use_Original_Qemu (U756) */
                default:
                    break;
                }
            }
            next += xsave_comp_size(i);
        }
        if (init & bit) {
            switch (i) {
#if __Use_Original_Qemu != 1 /* ours (U172) */
            case XSTATE_XTILE_CFG_BIT:
                do_clear_tilecfg(env);
                break;
            case XSTATE_XTILE_DATA_BIT:
                do_clear_tiledata(env);
                break;
#endif /* __Use_Original_Qemu (U172) */
#if __Use_Original_Qemu != 1 /* ours (U612) */
            case XSTATE_APX_BIT:
                do_clear_apx(env);
                break;
#endif /* __Use_Original_Qemu (U612) */
#if __Use_Original_Qemu != 1 /* ours (U756) */
            case XSTATE_CET_U_BIT:
                do_clear_cet_u(env);
                break;
            case XSTATE_CET_S_BIT:
                do_clear_cet_s(env);
                break;
#endif /* __Use_Original_Qemu (U756) */
            case XSTATE_YMM_BIT:
                do_clear_ymmh(env);
                break;
            case XSTATE_BNDREGS_BIT:
                memset(env->bnd_regs, 0, sizeof(env->bnd_regs));
                env->hflags &= ~HF_MPX_IU_MASK;
                break;
            case XSTATE_BNDCSR_BIT:
                memset(&env->bndcs_regs, 0, sizeof(env->bndcs_regs));
                cpu_sync_bndcs_hflags(env);
                break;
            case XSTATE_PKRU_BIT:
                if (env->pkru) {
                    env->pkru = 0;
                    tlb_flush(env_cpu(env));
                }
                break;
#if __Use_Original_Qemu != 1 /* ours (U124) */
            case XSTATE_OPMASK_BIT:
                do_clear_opmask(env);
                break;
            case XSTATE_ZMM_Hi256_BIT:
                do_clear_zmm_hi256(env);
                break;
            case XSTATE_Hi16_ZMM_BIT:
                do_clear_hi16_zmm(env);
                break;
#endif /* __Use_Original_Qemu (U124) */
            default:
                break;
            }
        }
    }
}
#endif /* __Use_Original_Qemu (U66) */

static void do_xrstor(CPUX86State *env, target_ulong ptr, uint64_t rfbm, uintptr_t ra)
{
    uint64_t xstate_bv, xcomp_bv, reserve0;

    rfbm &= env->xcr0;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }

#if __Use_Original_Qemu != 1 /* ours (U834) */
    x86_ac_check(env, ptr, 3, ra);      /* #AC before #GP, as do_fxsave (U834) */
#endif /* __Use_Original_Qemu (U834) */
    /* The operand must be 64 byte aligned.  */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    xstate_bv = cpu_ldq_data_ra(env, ptr + XO(header.xstate_bv), ra);

#if __Use_Original_Qemu == 1 /* original QEMU (U66) */
    if ((int64_t)xstate_bv < 0) {
        /* FIXME: Compact form.  */
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
#else /* ours (U66) */
    /* XCOMP_BV[63] selects the compacted form (U66) */
    if ((int64_t)cpu_ldq_data_ra(env, ptr + XO(header.xcomp_bv), ra) < 0) {
        do_xrstor_compact(env, ptr, rfbm, xstate_bv, false, ra);
        return;
    }
#endif /* __Use_Original_Qemu (U66) */

    /* Standard form.  */

    /* The XSTATE_BV field must not set bits not present in XCR0.  */
    if (xstate_bv & ~env->xcr0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* The XCOMP_BV field must be zero.  Note that, as of the April 2016
       revision, the description of the XSAVE Header (Vol 1, Sec 13.4.2)
       describes only XCOMP_BV, but the description of the standard form
       of XRSTOR (Vol 1, Sec 13.8.1) checks bytes 23:8 for zero, which
       includes the next 64-bit field.  */
    xcomp_bv = cpu_ldq_data_ra(env, ptr + XO(header.xcomp_bv), ra);
    reserve0 = cpu_ldq_data_ra(env, ptr + XO(header.reserve0), ra);
    if (xcomp_bv || reserve0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

#if __Use_Original_Qemu != 1 /* ours (U40) */
    /* The standard form loads MXCSR from memory whenever RFBM[1] or RFBM[2]
       is set, whatever XSTATE_BV says (SDM Vol1 13.8.1); reserved bits fault
       before any state is loaded.  */
    if (rfbm & (XSTATE_SSE_MASK | XSTATE_YMM_MASK)) {
        check_mxcsr(env, cpu_ldl_data_ra(env, ptr + XO(legacy.mxcsr), ra), ra);
    }

#endif /* __Use_Original_Qemu (U40) */
#if __Use_Original_Qemu != 1 /* ours (U173) */
    xrstor_check_xfd(env, rfbm & xstate_bv, ra);
#endif /* __Use_Original_Qemu (U173) */
    /* backport c6e6d1508a (X86Access, U480): nothing is loaded when any byte faults */
    x86_access_prepare(env, ptr, xsave_std_end(rfbm & xstate_bv), MMU_DATA_LOAD, ra);
    if (rfbm & XSTATE_FP_MASK) {
        if (xstate_bv & XSTATE_FP_MASK) {
            do_xrstor_fpu(env, ptr, ra);
        } else {
            do_fninit(env);
            memset(env->fpregs, 0, sizeof(env->fpregs));
        }
    }
#if __Use_Original_Qemu == 1 /* original QEMU (U40) */
    if (rfbm & XSTATE_SSE_MASK) {
        /* Note that the standard form of XRSTOR loads MXCSR from memory
           whether or not the XSTATE_BV bit is set.  */
        do_xrstor_mxcsr(env, ptr, ra);
        if (xstate_bv & XSTATE_SSE_MASK) {
            do_xrstor_sse(env, ptr, ra);
        } else {
            do_clear_sse(env);
        }
    }
#else /* ours (U40) */
    if (rfbm & (XSTATE_SSE_MASK | XSTATE_YMM_MASK)) {
        do_xrstor_mxcsr(env, ptr, ra);
    }
    if (rfbm & XSTATE_SSE_MASK) {
        if (xstate_bv & XSTATE_SSE_MASK) {
            do_xrstor_sse(env, ptr, ra);
        } else {
            do_clear_sse(env);
        }
    }
#endif /* __Use_Original_Qemu (U40) */
    if (rfbm & XSTATE_YMM_MASK) {
        if (xstate_bv & XSTATE_YMM_MASK) {
            do_xrstor_ymmh(env, ptr + XO(avx_state), ra);
        } else {
            do_clear_ymmh(env);
        }
    }
    if (rfbm & XSTATE_BNDREGS_MASK) {
        if (xstate_bv & XSTATE_BNDREGS_MASK) {
            do_xrstor_bndregs(env, ptr + XO(bndreg_state), ra);
            env->hflags |= HF_MPX_IU_MASK;
        } else {
            memset(env->bnd_regs, 0, sizeof(env->bnd_regs));
            env->hflags &= ~HF_MPX_IU_MASK;
        }
    }
    if (rfbm & XSTATE_BNDCSR_MASK) {
        if (xstate_bv & XSTATE_BNDCSR_MASK) {
            do_xrstor_bndcsr(env, ptr + XO(bndcsr_state), ra);
        } else {
            memset(&env->bndcs_regs, 0, sizeof(env->bndcs_regs));
        }
        cpu_sync_bndcs_hflags(env);
    }
#if __Use_Original_Qemu != 1 /* ours (U124) */
    if (rfbm & XSTATE_OPMASK_MASK) {
        if (xstate_bv & XSTATE_OPMASK_MASK) {
            do_xrstor_opmask(env, ptr + XO(opmask_state), ra);
        } else {
            do_clear_opmask(env);
        }
    }
    if (rfbm & XSTATE_ZMM_Hi256_MASK) {
        if (xstate_bv & XSTATE_ZMM_Hi256_MASK) {
            do_xrstor_zmm_hi256(env, ptr + XO(zmm_hi256_state), ra);
        } else {
            do_clear_zmm_hi256(env);
        }
    }
    if (rfbm & XSTATE_Hi16_ZMM_MASK) {
        if (xstate_bv & XSTATE_Hi16_ZMM_MASK) {
            do_xrstor_hi16_zmm(env, ptr + XO(hi16_zmm_state), ra);
        } else {
            do_clear_hi16_zmm(env);
        }
    }
#endif /* __Use_Original_Qemu (U124) */
    if (rfbm & XSTATE_PKRU_MASK) {
        uint64_t old_pkru = env->pkru;
        if (xstate_bv & XSTATE_PKRU_MASK) {
            do_xrstor_pkru(env, ptr + XO(pkru_state), ra);
        } else {
            env->pkru = 0;
        }
        if (env->pkru != old_pkru) {
            CPUState *cs = env_cpu(env);
            tlb_flush(cs);
        }
    }
#if __Use_Original_Qemu != 1 /* ours (U172) */
    if (rfbm & XSTATE_XTILE_CFG_MASK) {
        if (xstate_bv & XSTATE_XTILE_CFG_MASK) {
            do_xrstor_tilecfg(env, ptr + x86_ext_save_areas[XSTATE_XTILE_CFG_BIT].offset, ra);
        } else {
            do_clear_tilecfg(env);
        }
    }
    if (rfbm & XSTATE_XTILE_DATA_MASK) {
        if (xstate_bv & XSTATE_XTILE_DATA_MASK) {
            do_xrstor_tiledata(env, ptr + x86_ext_save_areas[XSTATE_XTILE_DATA_BIT].offset, ra);
        } else {
            do_clear_tiledata(env);
        }
    }
#endif /* __Use_Original_Qemu (U172) */
#if __Use_Original_Qemu != 1 /* ours (U612) */
    if (rfbm & XSTATE_APX_MASK) {
        if (xstate_bv & XSTATE_APX_MASK) {
            do_xrstor_apx(env, ptr + x86_ext_save_areas[XSTATE_APX_BIT].offset, ra);
        } else {
            do_clear_apx(env);
        }
    }
#endif /* __Use_Original_Qemu (U612) */
}

#undef XO

void helper_xrstor(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    do_xrstor(env, ptr, rfbm, GETPC());
}

#if __Use_Original_Qemu != 1 /* ours (U726) */
/*
 * NoVmp (ledger U726): XRSTORS (SDM Vol1 13.12, Vol2D XRSTORS): the compacted form of XRSTOR
 * (do_xrstor_compact: XCOMP_BV[62:0] within XCR0 | IA32_XSS, XSTATE_BV within XCOMP_BV, bytes
 * 63:16 of the header 0, MXCSR, XFD #NM for a component loaded from memory, the AMX rules) with
 * RFBM = EDX:EAX AND (XCR0 | IA32_XSS). #UD CR4.OSXSAVE = 0, #GP(0) CPL > 0, #GP(0) misaligned,
 * #GP(0) XCOMP_BV[63] = 0 (no standard form). XRSTOR_INFO / XMODIFIED are not kept (see
 * helper_xsaves).
 */
void helper_xrstors(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    uintptr_t ra = GETPC();
    uint64_t xstate_bv;

    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (env->hflags & HF_CPL_MASK) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    xstate_bv = cpu_ldq_data_ra(env, ptr + 512, ra);
    if ((int64_t)cpu_ldq_data_ra(env, ptr + 512 + 8, ra) >= 0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);        /* XCOMP_BV[63] = 0 */
    }
    do_xrstor_compact(env, ptr, rfbm & (env->xcr0 | env->xss), xstate_bv, true, ra);
}
#endif /* __Use_Original_Qemu (U726) */

#if defined(CONFIG_USER_ONLY)
void cpu_x86_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fsave(env, ptr, data32, 0);
}

void cpu_x86_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    do_frstor(env, ptr, data32, 0);
}

void cpu_x86_fxsave(CPUX86State *env, target_ulong ptr)
{
    do_fxsave(env, ptr, 0);
}

void cpu_x86_fxrstor(CPUX86State *env, target_ulong ptr)
{
    do_fxrstor(env, ptr, 0);
}

void cpu_x86_xsave(CPUX86State *env, target_ulong ptr)
{
    do_xsave(env, ptr, -1, get_xinuse(env), -1, 0);
}

void cpu_x86_xrstor(CPUX86State *env, target_ulong ptr)
{
    do_xrstor(env, ptr, -1, 0);
}
#endif

uint64_t helper_xgetbv(CPUX86State *env, uint32_t ecx)
{
    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }

    switch (ecx) {
    case 0:
        return env->xcr0;
    case 1:
        if (env->features[FEAT_XSAVE] & CPUID_XSAVE_XGETBV1) {
            return env->xcr0 & get_xinuse(env);
        }
        break;
    }
    raise_exception_ra(env, EXCP0D_GPF, GETPC());
}

#if __Use_Original_Qemu != 1 /* ours (U614) */
/*
 * NoVmp (ledger U614): an Intel APX prefix (REX2) is usable only with CR4.OSXSAVE = 1 and
 * XCR0[APX_F = 19] = 1 (APX spec 355828-009 Table 3.8: every other combination "Fault (UD)";
 * "when CR4.OSXSAVE=0, XCR0 is treated as all 0's"). Called first in a REX2 instruction.
 */
void helper_apx_check(CPUX86State *env)
{
    if (!(env->cr[4] & CR4_OSXSAVE_MASK) || !(env->xcr0 & XSTATE_APX_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }
}

#endif /* __Use_Original_Qemu (U614) */
#if __Use_Original_Qemu != 1 /* ours (U643) */
/*
 * NoVmp (ledger U643): PUSH2 / POP2 "Alignment check: if (RSP % 16 != 0): #GP" before any
 * stack access (APX spec 355828-009 9.1.3, 9.3.3; 3.1.3.1.1).
 */
void helper_apx_rsp16(CPUX86State *env, target_ulong rsp)
{
    if (rsp & 15) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
}

#endif /* __Use_Original_Qemu (U643) */
void helper_xsetbv(CPUX86State *env, uint32_t ecx, uint64_t mask)
{
    uint32_t dummy, ena_lo, ena_hi;
    uint64_t ena;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }

    /* Only XCR0 is defined at present; the FPU may not be disabled.  */
    if (ecx != 0 || (mask & XSTATE_FP_MASK) == 0) {
        goto do_gpf;
    }

    /* SSE can be disabled, but only if AVX is disabled too.  */
    if ((mask & (XSTATE_SSE_MASK | XSTATE_YMM_MASK)) == XSTATE_YMM_MASK) {
        goto do_gpf;
    }

    /* Disallow enabling unimplemented features.  */
    cpu_x86_cpuid(env, 0x0d, 0, &ena_lo, &dummy, &dummy, &ena_hi);
    ena = ((uint64_t)ena_hi << 32) | ena_lo;
    if (mask & ~ena) {
        goto do_gpf;
    }

    /* Disallow enabling only half of MPX.  */
    if ((mask ^ (mask * (XSTATE_BNDCSR_MASK / XSTATE_BNDREGS_MASK)))
        & XSTATE_BNDCSR_MASK) {
        goto do_gpf;
    }

#if __Use_Original_Qemu != 1 /* ours (U122) */
    /*
     * NoVmp (ledger U122): SDM Vol1 13.3 - XSETBV #GP if EAX[7:5] is not 000b and any
     * bit is clear in EAX[2:1] or EAX[7:5] (AVX-512 state only all together and only
     * with SSE and AVX state). The supported mask is the CPUID check above (the
     * active UC_CTL_X86_CPUID profile, else the model).
     */
    {
        const uint64_t avx512 = XSTATE_OPMASK_MASK | XSTATE_ZMM_Hi256_MASK |
                                XSTATE_Hi16_ZMM_MASK;

        if ((mask & avx512) &&
            (mask & (avx512 | XSTATE_SSE_MASK | XSTATE_YMM_MASK)) !=
                (avx512 | XSTATE_SSE_MASK | XSTATE_YMM_MASK)) {
            goto do_gpf;
        }
    }
#endif /* __Use_Original_Qemu (U122) */
#if __Use_Original_Qemu != 1 /* ours (U171) */
    /*
     * NoVmp (ledger U171): SDM Vol1 13.3 - XSETBV #GP if ECX = 0 and EAX[17] != EAX[18]
     * (TILECFG and TILEDATA must be enabled together; XCR0[18:17] is 00b or 11b).
     */
    if (((mask >> XSTATE_XTILE_CFG_BIT) ^ (mask >> XSTATE_XTILE_DATA_BIT)) & 1) {
        goto do_gpf;
    }
#endif /* __Use_Original_Qemu (U171) */

    env->xcr0 = mask;
    cpu_sync_bndcs_hflags(env);
    cpu_sync_avx_hflag(env);
    return;

 do_gpf:
    raise_exception_ra(env, EXCP0D_GPF, GETPC());
}

#if __Use_Original_Qemu != 1 /* ours (U175) */
/*
 * NoVmp (ledger U175..U180): VEX-encoded Intel AMX instructions (SDM Vol2A 2.10 "Intel AMX
 * Instruction Exception Classes" AMX-E1..E6, Vol2A/2B instruction pages, Vol1 ch. 19).
 * The translator (validate_vex class 22) already raised #UD for: no VEX, outside 64-bit
 * mode (IA32_EFER.LMA = 0 or CS.L = 0), VEX.L = 1, VEX.W = 1, LOCK/66/F2/F3/REX before
 * VEX, VEX.vvvv != 1111b where unused, the CPUID feature bit clear, and the fixed ModRM
 * fields (mod, reg = 000b, r/m = 000b / 100b (SIB), C0h). The run-time part is here:
 *   1. #UD if CR4.OSXSAVE = 0 or XCR0[18:17] != 11b (all classes);
 *   2. #NM if XFD is enabled for TILEDATA (E3/E4/E5 only; LDTILECFG, STTILECFG and
 *      TILERELEASE never #NM - SDM Vol1 13.14), IA32_XFD_ERR := IA32_XFD AND 40000h;
 *   3. #UD on TILES_CONFIGURED = 0 and the tile checks of the class;
 *   4. memory faults (#GP/#SS non-canonical, #PF) of the accesses, LDTILECFG's #GP.
 * Ordering choice (not specified by the SDM): the static #UDs, then #NM, then the #UDs
 * that depend on TILECFG. No AMX class lists CR0.TS/CR0.EM (the VEX forms do not check
 * them); AMX memory accesses never raise #AC (ISE 319433 3.6).
 */
#define AMX_TILE(env, t)        ((env)->xtiledata + 1024 * (t))
#define AMX_ROW(env, t, r)      ((env)->xtiledata + 1024 * (t) + AMX_P1_BYTES_PER_ROW * (r))
#define AMX_ROWS(env, t)        ((env)->xtilecfg[48 + (t)])
#define AMX_COLSB(env, t)       lduw_le_p((env)->xtilecfg + 16 + 2 * (t))
#define AMX_START_ROW(env)      ((env)->xtilecfg[1])
#define AMX_CONFIGURED(env)     ((env)->xtilecfg[0] != 0)

/* step 1: CR4.OSXSAVE and XCR0[18:17] = 11b */
static void amx_check_enabled(CPUX86State *env, uintptr_t ra)
{
    if (!(env->cr[4] & CR4_OSXSAVE_MASK) ||
        (env->xcr0 & XSTATE_AMX_MASK) != XSTATE_AMX_MASK) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
}

/* steps 1-3 for the TILEDATA users (AMX-E3/E4/E5): + XFD #NM, + TILES_CONFIGURED */
static void amx_check_tiledata(CPUX86State *env, uintptr_t ra)
{
    amx_check_enabled(env, ra);
    if (x86_cpu_xfd_armed(env) & XSTATE_XTILE_DATA_MASK) {
        env->msr_xfd_err = env->msr_xfd & XSTATE_XTILE_DATA_MASK;
        raise_exception_ra(env, EXCP07_PREX, ra);
    }
    if (!AMX_CONFIGURED(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
}

/* "valid tile" for palette 1: a name below max_names whose rows/colsb are non-zero */
static bool amx_tile_valid(CPUX86State *env, unsigned t)
{
    return t < AMX_P1_MAX_NAMES && AMX_ROWS(env, t) != 0;
}

/* with paging, translate [addr, addr+len) for writing before any byte is stored */
static void amx_probe_write(CPUX86State *env, target_ulong addr, int len, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env, false);
    int first = TARGET_PAGE_SIZE - (int)(addr & ~TARGET_PAGE_MASK);

    if (!(env->cr[0] & CR0_PG_MASK) || len <= 0) {
        return;
    }
    if (first > len) {
        first = len;
    }
    probe_write(env, addr, first, mmu_idx, ra);
    if (first < len) {
        probe_write(env, addr + first, len - first, mmu_idx, ra);
    }
}

/* zero_all_tile_data() / zero_upper_rows(t, r) of SDM Vol1 19.4 */
static void amx_zero_all_tile_data(CPUX86State *env)
{
    /* "if XCR0[TILEDATA]": guaranteed by amx_check_enabled */
    memset(env->xtiledata, 0, sizeof(env->xtiledata));
}

static void amx_zero_upper_rows(CPUX86State *env, unsigned t, unsigned r)
{
    if (r < AMX_P1_MAX_ROWS) {
        memset(AMX_ROW(env, t, r), 0, AMX_P1_BYTES_PER_ROW * (AMX_P1_MAX_ROWS - r));
    }
}

/*
 * LDTILECFG m512 (AMX-E1): 64 bytes read first; #GP(0) if the consistency checks fail
 * (x86_amx_tilecfg_ok, U172); palette 0: TILECFG := 0; palette 1: TILECFG := image
 * (start_row included). Both zero TILEDATA. XFD never applies (SDM Vol1 13.14).
 */
void helper_amx_ldtilecfg(CPUX86State *env, target_ulong ptr)
{
    uintptr_t ra = GETPC();
    uint8_t buf[64];
    int i;

    amx_check_enabled(env, ra);
    for (i = 0; i < 64; i += 8) {
        stq_le_p(buf + i, cpu_ldq_data_ra(env, ptr + i, ra));
    }
    if (!x86_amx_tilecfg_ok(buf, env->xcr0)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (buf[0] == 0) {
        memset(env->xtilecfg, 0, sizeof(env->xtilecfg));    /* TILES_CONFIGURED := 0 */
    } else {
        memcpy(env->xtilecfg, buf, sizeof(env->xtilecfg));  /* TILES_CONFIGURED := 1 */
    }
    amx_zero_all_tile_data(env);
}

/*
 * STTILECFG m512 (AMX-E2): TILES_CONFIGURED = 0 stores 64 zero bytes, otherwise the
 * TILECFG image (palette_id, start_row, colsb[8], rows[8], reserved bytes zero) - which
 * is exactly env->xtilecfg. The destination is translated before any byte is written.
 */
void helper_amx_sttilecfg(CPUX86State *env, target_ulong ptr)
{
    uintptr_t ra = GETPC();
    int i;

    amx_check_enabled(env, ra);
    amx_probe_write(env, ptr, 64, ra);
    for (i = 0; i < 64; i += 8) {
        cpu_stq_data_ra(env, ptr + i, ldq_le_p(env->xtilecfg + i), ra);
    }
}

/* TILERELEASE (AMX-E6): TILEDATA := 0, TILECFG := 0, TILES_CONFIGURED := 0; never #NM */
void helper_amx_tilerelease(CPUX86State *env)
{
    amx_check_enabled(env, GETPC());
    amx_zero_all_tile_data(env);
    memset(env->xtilecfg, 0, sizeof(env->xtilecfg));
}
#endif /* __Use_Original_Qemu (U175) */

#if __Use_Original_Qemu != 1 /* ours (U176) */
/*
 * NoVmp (ledger U176): TILELOADD / TILELOADDT1 / TILESTORED (AMX-E3) and TILEZERO
 * (AMX-E5). info = tile number (ModRM.reg | VEX.R, 0..15) | 32-bit address size << 8.
 * sibmem: membegin = base + displacement (the translator's `base`), stride = index <<
 * scale (0 without an index); row r is at segment base + (membegin + r * stride) with the
 * effective address truncated to 32 bits under a 67H prefix (as every 64-bit-mode access).
 */
static target_ulong amx_row_byte(target_ulong base, target_ulong stride, target_ulong seg,
                                 unsigned r, unsigned j, bool a32)
{
    target_ulong ea = base + (target_ulong)r * stride + j;

    if (a32) {
        ea = (uint32_t)ea;
    }
    return seg + ea;
}

/* AMX-E3 tile checks: valid tile below max_names, colsb % 4 = 0, start_row < rows */
static void amx_check_e3(CPUX86State *env, unsigned t, uintptr_t ra)
{
    amx_check_tiledata(env, ra);
    if (!amx_tile_valid(env, t) || (AMX_COLSB(env, t) & 3) ||
        AMX_START_ROW(env) >= AMX_ROWS(env, t)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
}

/*
 * TILELOADD[,T1] tdest, tsib (SDM Vol2B): start := start_row; zero_upper_rows(tdest,
 * start); rows start..rows-1 read colsb bytes each (write_row_and_zero); start_row := 0.
 * A fault leaves start_row = the row that faulted (the restart point) and that row as it
 * was; earlier rows stay loaded. The T1 hint has no architectural effect.
 */
void helper_amx_tileload(CPUX86State *env, target_ulong base, target_ulong stride,
                         target_ulong seg, uint32_t info)
{
    uintptr_t ra = GETPC();
    unsigned t = info & 15, r, j, rows, nbytes;
    bool a32 = (info >> 8) & 1;

    amx_check_e3(env, t, ra);
    rows = AMX_ROWS(env, t);
    nbytes = AMX_COLSB(env, t);
    amx_zero_upper_rows(env, t, AMX_START_ROW(env));
    for (r = AMX_START_ROW(env); r < rows; r++) {
        uint8_t buf[AMX_P1_BYTES_PER_ROW];

        AMX_START_ROW(env) = r;
        for (j = 0; j < nbytes; j++) {
            buf[j] = cpu_ldub_data_ra(env, amx_row_byte(base, stride, seg, r, j, a32), ra);
        }
        memcpy(AMX_ROW(env, t, r), buf, nbytes);
        memset(AMX_ROW(env, t, r) + nbytes, 0, AMX_P1_BYTES_PER_ROW - nbytes);
    }
    AMX_START_ROW(env) = 0;
}

/*
 * TILESTORED tsib, tsrc (SDM Vol2B): rows start_row..rows-1, colsb bytes each;
 * start_row := 0. Each row is translated for writing (both pages when it crosses one)
 * before any of its bytes is stored; a fault leaves start_row = that row.
 */
void helper_amx_tilestore(CPUX86State *env, target_ulong base, target_ulong stride,
                          target_ulong seg, uint32_t info)
{
    uintptr_t ra = GETPC();
    unsigned t = info & 15, r, j, rows, nbytes;
    bool a32 = (info >> 8) & 1;

    amx_check_e3(env, t, ra);
    rows = AMX_ROWS(env, t);
    nbytes = AMX_COLSB(env, t);
    for (r = AMX_START_ROW(env); r < rows; r++) {
        target_ulong first = amx_row_byte(base, stride, seg, r, 0, a32);
        target_ulong last = amx_row_byte(base, stride, seg, r, nbytes - 1, a32);

        AMX_START_ROW(env) = r;
        if (last >= first) {
            amx_probe_write(env, first, nbytes, ra);
        } else {                    /* the 32-bit effective address wrapped in the row */
            for (j = 0; j < nbytes; j++) {
                amx_probe_write(env, amx_row_byte(base, stride, seg, r, j, a32), 1, ra);
            }
        }
        for (j = 0; j < nbytes; j++) {
            cpu_stb_data_ra(env, amx_row_byte(base, stride, seg, r, j, a32),
                            AMX_ROW(env, t, r)[j], ra);
        }
    }
    AMX_START_ROW(env) = 0;
}

/* TILEZERO tdest (AMX-E5): every row and byte of the palette's tile := 0; start_row := 0 */
void helper_amx_tilezero(CPUX86State *env, uint32_t t)
{
    uintptr_t ra = GETPC();

    amx_check_tiledata(env, ra);
    if (!amx_tile_valid(env, t)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    memset(AMX_TILE(env, t), 0, AMX_P1_BYTES_PER_ROW * AMX_P1_MAX_ROWS);
    AMX_START_ROW(env) = 0;
}
#endif /* __Use_Original_Qemu (U176) */

#if __Use_Original_Qemu != 1 /* ours (U177) */
/*
 * NoVmp (ledger U177..U180): the TMUL instructions, tsrcdest = ModRM.reg | VEX.R, tsrc1 =
 * ModRM.r/m | VEX.B, tsrc2 = VEX.vvvv (0..15). C[M][N] += A[M][K] * B[K][N] with
 * M = tsrcdest.rows, K = tsrc1.colsb / 4 = tsrc2.rows, N = tsrcdest.colsb / 4.
 */
enum {
    AMX_TDPBSSD, AMX_TDPBSUD, AMX_TDPBUSD, AMX_TDPBUUD,     /* U177 */
    AMX_TDPBF16PS,                                          /* U178 */
    AMX_TDPFP16PS,                                          /* U179 */
    AMX_TCMMIMFP16PS, AMX_TCMMRLFP16PS,                     /* U180 */
#if __Use_Original_Qemu != 1 /* ours (U724) */
    AMX_TDPBF8PS, AMX_TDPBHF8PS, AMX_TDPHBF8PS, AMX_TDPHF8PS,   /* U724 */
#endif /* __Use_Original_Qemu (U724) */
};

/* AMX-E4 (SDM Vol2A 2.10) after amx_check_enabled; then the result tile is d */
static void amx_check_e4(CPUX86State *env, unsigned d, unsigned s1, unsigned s2,
                         uintptr_t ra)
{
    if (d == s1 || s1 == s2 || d == s2) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    amx_check_tiledata(env, ra);                /* #NM (XFD), TILES_CONFIGURED */
    if (!amx_tile_valid(env, d) || !amx_tile_valid(env, s1) || !amx_tile_valid(env, s2) ||
        (AMX_COLSB(env, d) & 3) || (AMX_COLSB(env, s1) & 3) || (AMX_COLSB(env, s2) & 3) ||
        AMX_COLSB(env, d) != AMX_COLSB(env, s2) ||
        AMX_ROWS(env, d) != AMX_ROWS(env, s1) ||
        AMX_COLSB(env, s1) / 4 != AMX_ROWS(env, s2) ||
        AMX_COLSB(env, d) > 64 || AMX_COLSB(env, s2) > 64 ||    /* tmul_maxn (1EH) */
        AMX_COLSB(env, s1) / 4 > 16 || AMX_ROWS(env, s2) > 16) { /* tmul_maxk (1EH) */
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
}

/* the tail of every TMUL operation: zero_upper_rows(tsrcdest, rows), zero_tilecfg_start */
static void amx_tmul_finish(CPUX86State *env, unsigned d)
{
    amx_zero_upper_rows(env, d, AMX_ROWS(env, d));
    AMX_START_ROW(env) = 0;
}

/*
 * TDPBSSD / TDPBSUD / TDPBUSD / TDPBUUD (SDM Vol2B): DPBD(c, x, y) adds the four products
 * extend_src1(x.byte[i]) * extend_src2(y.byte[i]) to the dword c (wrapping); the first
 * letter is tsrc1's signedness, the second tsrc2's. Row m starts from the whole
 * tsrcdest.row[m] (tmp := tsrcdest.row[m]) and is written back with write_row_and_zero.
 */
static void amx_tmul_int8(CPUX86State *env, int op, unsigned d, unsigned s1, unsigned s2)
{
    bool sx1 = op == AMX_TDPBSSD || op == AMX_TDPBSUD;
    bool sx2 = op == AMX_TDPBSSD || op == AMX_TDPBUSD;
    unsigned rows = AMX_ROWS(env, d), kk = AMX_COLSB(env, s1) / 4;
    unsigned nn = AMX_COLSB(env, d) / 4, m, k, n, i;

    for (m = 0; m < rows; m++) {
        uint32_t tmp[16];
        for (n = 0; n < 16; n++) {
            tmp[n] = ldl_le_p(AMX_ROW(env, d, m) + 4 * n);
        }
        for (k = 0; k < kk; k++) {
            const uint8_t *x = AMX_ROW(env, s1, m) + 4 * k;
            for (n = 0; n < nn; n++) {
                const uint8_t *y = AMX_ROW(env, s2, k) + 4 * n;
                for (i = 0; i < 4; i++) {
                    int32_t a = sx1 ? (int8_t)x[i] : (int32_t)x[i];
                    int32_t b = sx2 ? (int8_t)y[i] : (int32_t)y[i];
                    tmp[n] += (uint32_t)(a * b);
                }
            }
        }
        for (n = 0; n < nn; n++) {
            stl_le_p(AMX_ROW(env, d, m) + 4 * n, tmp[n]);
        }
        memset(AMX_ROW(env, d, m) + 4 * nn, 0, AMX_P1_BYTES_PER_ROW - 4 * nn);
    }
    amx_tmul_finish(env, d);
}
#endif /* __Use_Original_Qemu (U177) */

#if __Use_Original_Qemu != 1 /* ours (U178) */
/*
 * NoVmp (ledger U178): FP32 arithmetic of the AMX floating-point dot products (ISE 319433
 * 3.2/3.4 fma32, SDM Vol2B pseudocode): "FP32 FMA with DAZ=FTZ=1, RNE rounding. MXCSR is
 * neither consulted nor updated. No exceptions raised or denoted."
 *   fma32(acc, x, y): denormal x, y, acc -> zero of the same sign (DAZ); v = x*y + acc
 *     with one rounding (RNE, gradual underflow); a denormal v -> zero of v's sign (FTZ).
 *   add32(a, b): the same for a + b.
 * NaN results (the SDM leaves them open; x86 SIMD convention chosen): a NaN operand ->
 * the first NaN in the order x, y, acc (a, b for add32), quieted; invalid (inf * 0,
 * inf - inf) -> the QNaN indefinite FFC00000h. MXCSR and env->sse_status are not used.
 */
static float32 amx_daz32(float32 a)
{
    return float32_is_zero_or_denormal(a) ? make_float32(float32_val(a) & 0x80000000u) : a;
}

static float32 amx_ftz32(float32 a)
{
    return float32_is_denormal(a) ? make_float32(float32_val(a) & 0x80000000u) : a;
}

static void amx_fp_status(float_status *st)
{
    memset(st, 0, sizeof(*st));
    set_float_rounding_mode(float_round_nearest_even, st);
#if __Use_Original_Qemu != 1 /* ours (U596) */
    /*
     * NoVmp (ledger U596): FTZ is the MXCSR.FTZ response to an underflow condition (SDM Vol1
     * 10.2.3.3), and the condition is "the magnitude of the rounded result ..., with unbounded
     * exponent, is less than the smallest possible normalized" (11.5.2.5): tininess after
     * rounding with an unbounded exponent, and the zero carries the sign of the true result.
     * Rounding to denormal precision first and then flushing a denormal (amx_ftz32 alone) kept
     * results that round up to 2^-126 only at denormal precision: 2^-126 + 2^-75 * -2^-75 =
     * 2^-126 - 2^-150 is tiny (24-bit rounding with unbounded exponent: 1.11..1b * 2^-127,
     * exact), so +0, where the old code gave 00800000h (RNE tie at denormal precision). The
     * i5-13600K's VFMADD231SS with MXCSR.FTZ = 1 gives +0 too (cases_fixes2). softfloat's
     * flush-to-zero with after-rounding detection is that rule (the SSE path's, U445).
     */
    set_flush_to_zero(true, st);
    set_float_ftz_detection(float_ftz_after_rounding, st);
#endif /* __Use_Original_Qemu (U596) */
}

static float32 amx_nan_result(float32 r, float_status *st)
{
    if (get_float_exception_flags(st) & float_flag_invalid) {
        return make_float32(0xffc00000u);               /* QNaN indefinite */
    }
    return r;
}

static float32 amx_fma32(float32 acc, float32 x, float32 y)
{
    float_status st;
    float32 r;

    x = amx_daz32(x);
    y = amx_daz32(y);
    acc = amx_daz32(acc);
    if (float32_is_any_nan(x)) {
        return make_float32(float32_val(x) | 0x00400000u);
    }
    if (float32_is_any_nan(y)) {
        return make_float32(float32_val(y) | 0x00400000u);
    }
    if (float32_is_any_nan(acc)) {
        return make_float32(float32_val(acc) | 0x00400000u);
    }
    amx_fp_status(&st);
    r = amx_nan_result(float32_muladd(x, y, acc, 0, &st), &st);
    return amx_ftz32(r);
}

static float32 amx_add32(float32 a, float32 b)
{
    float_status st;
    float32 r;

    a = amx_daz32(a);
    b = amx_daz32(b);
    if (float32_is_any_nan(a)) {
        return make_float32(float32_val(a) | 0x00400000u);
    }
    if (float32_is_any_nan(b)) {
        return make_float32(float32_val(b) | 0x00400000u);
    }
    amx_fp_status(&st);
    r = amx_nan_result(float32_add(a, b, &st), &st);
    return amx_ftz32(r);
}

/*
 * The common FP dot-product skeleton (TDPBF16PS, TDPFP16PS, TCMMIMFP16PS, TCMMRLFP16PS):
 * per row m, temp1[0 .. 2N-1] := 0; for k (ascending), for n:
 *   temp1[2n+0] := fma32(temp1[2n+0], ex(k, n), ey(k, n)),
 *   temp1[2n+1] := fma32(temp1[2n+1], ox(k, n), oy(k, n));
 * then tsrcdest.fp32[n] := add32(tsrcdest.fp32[n], add32(temp1[2n], temp1[2n+1])) and
 * write_row_and_zero. `cvt` converts one 16-bit element to FP32; `pair` selects which
 * elements of A = tsrc1.row[m].dword[k] (a0 = low, a1 = high half) and
 * B = tsrc2.row[k].dword[n] feed the even and odd accumulators.
 */
typedef float32 (*AmxCvt16)(uint16_t h);
typedef void (*AmxPair)(uint16_t a0, uint16_t a1, uint16_t b0, uint16_t b1,
                        uint16_t *ex, uint16_t *ey, uint16_t *ox, uint16_t *oy);

static void amx_tmul_fp(CPUX86State *env, unsigned d, unsigned s1, unsigned s2,
                        AmxCvt16 cvt, AmxPair pair)
{
    unsigned rows = AMX_ROWS(env, d), kk = AMX_COLSB(env, s1) / 4;
    unsigned nn = AMX_COLSB(env, d) / 4, m, k, n;

    for (m = 0; m < rows; m++) {
        float32 temp1[32];
        for (n = 0; n < 2 * nn; n++) {
            temp1[n] = float32_zero;
        }
        for (k = 0; k < kk; k++) {
            const uint8_t *a = AMX_ROW(env, s1, m) + 4 * k;
            for (n = 0; n < nn; n++) {
                const uint8_t *b = AMX_ROW(env, s2, k) + 4 * n;
                uint16_t ex, ey, ox, oy;
                pair(lduw_le_p(a), lduw_le_p(a + 2), lduw_le_p(b), lduw_le_p(b + 2),
                     &ex, &ey, &ox, &oy);
                temp1[2 * n] = amx_fma32(temp1[2 * n], cvt(ex), cvt(ey));
                temp1[2 * n + 1] = amx_fma32(temp1[2 * n + 1], cvt(ox), cvt(oy));
            }
        }
        for (n = 0; n < nn; n++) {
            uint8_t *c = AMX_ROW(env, d, m) + 4 * n;
            float32 tmpf32 = amx_add32(temp1[2 * n], temp1[2 * n + 1]);
            stl_le_p(c, float32_val(amx_add32(make_float32(ldl_le_p(c)), tmpf32)));
        }
        memset(AMX_ROW(env, d, m) + 4 * nn, 0, AMX_P1_BYTES_PER_ROW - 4 * nn);
    }
    amx_tmul_finish(env, d);
}

/* make_fp32 (TDPBF16PS): the bfloat16 bit pattern in bits 31:16 of a dword */
static float32 amx_cvt_bf16(uint16_t h)
{
    return make_float32((uint32_t)h << 16);
}

/* TDPBF16PS: even accumulator A.bf16[2k] * B.bf16[2n], odd A.bf16[2k+1] * B.bf16[2n+1] */
static void amx_pair_dot(uint16_t a0, uint16_t a1, uint16_t b0, uint16_t b1,
                         uint16_t *ex, uint16_t *ey, uint16_t *ox, uint16_t *oy)
{
    *ex = a0;
    *ey = b0;
    *ox = a1;
    *oy = b1;
}
#endif /* __Use_Original_Qemu (U178) */

#if __Use_Original_Qemu != 1 /* ours (U179) */
/*
 * NoVmp (ledger U179): cvt_fp16_to_fp32 of TDPFP16PS / TCMM*FP16PS - exact; "Input FP16
 * denormals are always handled and not treated as zero" (they become normal FP32 values,
 * so the FP32 DAZ of fma32 never sees them); a NaN keeps sign and payload (quieted by
 * fma32's NaN rule).
 */
static float32 amx_cvt_fp16(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    int exp = (h >> 10) & 0x1f;
    uint32_t frac = h & 0x3ff;

    if (exp == 0x1f) {
        return make_float32(sign | 0x7f800000u | (frac << 13));
    }
    if (exp == 0) {
        if (frac == 0) {
            return make_float32(sign);
        }
        exp = 1;
        while (!(frac & 0x400)) {               /* normalise the FP16 denormal */
            frac <<= 1;
            exp--;
        }
        frac &= 0x3ff;
    }
    return make_float32(sign | ((uint32_t)(exp - 15 + 127) << 23) | (frac << 13));
}
#endif /* __Use_Original_Qemu (U179) */

#if __Use_Original_Qemu != 1 /* ours (U180) */
/*
 * NoVmp (ledger U180): AMX-COMPLEX - every dword is a complex number, FP16 real part in
 * bits 15:0 (fp16[2k]) and FP16 imaginary part in bits 31:16 (fp16[2k+1]). SDM Vol2B:
 *   TCMMIMFP16PS: temp1[2n] += A.im * B.re; temp1[2n+1] += A.re * B.im
 *   TCMMRLFP16PS: temp1[2n] += A.re * B.re; temp1[2n+1] += (-A.im) * B.im
 * ("-" negates the FP16 value, i.e. flips its sign bit, before cvt_fp16_to_fp32).
 */
static void amx_pair_cmm_im(uint16_t a0, uint16_t a1, uint16_t b0, uint16_t b1,
                            uint16_t *ex, uint16_t *ey, uint16_t *ox, uint16_t *oy)
{
    *ex = a1;
    *ey = b0;
    *ox = a0;
    *oy = b1;
}

static void amx_pair_cmm_rl(uint16_t a0, uint16_t a1, uint16_t b0, uint16_t b1,
                            uint16_t *ex, uint16_t *ey, uint16_t *ox, uint16_t *oy)
{
    *ex = a0;
    *ey = b0;
    *ox = a1 ^ 0x8000;
    *oy = b1;
}
#endif /* __Use_Original_Qemu (U180) */

#if __Use_Original_Qemu != 1 /* ours (U724) */
/*
 * NoVmp (ledger U724): AMX-FP8 TDP[B,H,BH,HB]F8PS (ISE 319433-062 3.4, 3.7; FP8 formats 1.7,
 * Table 1-12: E5M2 "BF8" bias 15 with infinities S.11111.00 and NaNs S.11111.{01,10,11};
 * E4M3 "HF8" bias 7, no infinity, NaN S.1111.111 only). Per row m and column n the products
 * of every k are summed exactly in fixed point (convert_bf8/hf8_to_int64: 2^16 * x / 2^9 * x;
 * the int128 sum temp1.int128[n]) and rounded once by convert_int128_to_fp32 (RNE, factor 32
 * / 18 / 25); then tsrcdest.fp32[n] + tmpf32 with "DAZ==0 ... FTZ==1", RNE (the description;
 * also FP8 numerics 1.7.2 "as if MXCSR.DAZ is not set" for any input; FTZ as amx_fp_status,
 * U596). No MXCSR access. Specials, ISE "INF Computation treatment" / "NaN treatment":
 *   - a NaN among the elements used (tsrc1.row[m].float8[4k..4k+3], tsrc2.row[k].float8[4n..
 *     4n+3] for every k) or in tsrcdest.row[m].fp32[n] -> FFC00000h (QNaN indefinite);
 *   - INF * 0 and +INF + -INF -> FFC00000h; INF * finite non-zero (denormals included, DAZ =
 *     0) and INF * INF -> INF with the XOR of the signs; INF + finite -> that INF;
 *   - the final FP32 add: IEEE (INF + -INF -> FFC00000h).
 * The magnitudes are below 2^32 (BF8 7 << 29), so every product fits in 64 bits; the sum of at
 * most 64 of them needs 71 bits (a two's-complement 128-bit accumulator here) and its value
 * (2^-32 .. 2^41) is a normal FP32, so convert_int128_to_fp32 never under- or overflows.
 * The AVX10.2 FP8 conversion helpers (U400) are not used: AMX computes in fixed point.
 */
enum { AMX_FP8_FINITE, AMX_FP8_NAN, AMX_FP8_PINF, AMX_FP8_NINF };

/* convert_bf8_to_int64 / convert_hf8_to_int64 -> sign and magnitude, or a special class */
static int amx_fp8_fix(uint8_t x, bool hf8, uint64_t *mag, bool *neg)
{
    int exp, frac;
    uint64_t mant;

    *neg = (x >> 7) != 0;
    if (hf8) {
        exp = (x >> 3) & 15;
        frac = x & 7;
        if (exp == 15 && frac == 7) {
            return AMX_FP8_NAN;
        }
        mant = exp ? (uint64_t)(frac | 8) : (uint64_t)frac;     /* set Jbit */
    } else {
        exp = (x >> 2) & 31;
        frac = x & 3;
        if (exp == 31) {
            return frac ? AMX_FP8_NAN : (*neg ? AMX_FP8_NINF : AMX_FP8_PINF);
        }
        mant = exp ? (uint64_t)(frac | 4) : (uint64_t)frac;
    }
    *mag = mant << (exp ? exp - 1 : 0);                         /* e_count */
    return AMX_FP8_FINITE;
}

/* (hi:lo) >> n, low 64 bits, 0 <= n < 128 */
static uint64_t amx_shr128(uint64_t hi, uint64_t lo, int n)
{
    if (n == 0) {
        return lo;
    }
    if (n < 64) {
        return (lo >> n) | (hi << (64 - n));
    }
    return hi >> (n - 64);
}

/* convert_int128_to_fp32 (ISE 3.4): RNE of the integer (hi:lo) / 2^factor */
static float32 amx_fix128_to_fp32(uint64_t hi, uint64_t lo, int factor)
{
    uint32_t sign = 0;
    uint64_t mant, g, sticky;
    int p, sh;

    if (!hi && !lo) {
        return float32_zero;
    }
    if ((int64_t)hi < 0) {                                      /* magnitude = -in */
        sign = 1;
        hi = ~hi + (lo == 0);
        lo = ~lo + 1;
    }
    p = hi ? 127 - clz64(hi) : 63 - clz64(lo);                  /* Jbit_position */
    if (p <= 23) {
        mant = lo << (23 - p);                                  /* exact: G = sticky = 0 */
        g = sticky = 0;
    } else {
        sh = p - 23;                                            /* Lbit at bit sh */
        mant = amx_shr128(hi, lo, sh) & 0xffffff;
        g = amx_shr128(hi, lo, sh - 1) & 1;                     /* Gbit */
        /* sticky = OR of the bits below the Gbit (sh - 1 of them) */
        if (sh - 1 < 64) {
            sticky = (lo & ((1ULL << (sh - 1)) - 1)) != 0;
        } else {
            sticky = lo != 0 || (sh - 1 > 64 && (hi & ((1ULL << (sh - 1 - 64)) - 1)) != 0);
        }
    }
    mant += g & ((mant & 1) | (sticky != 0));                   /* RndAdd1 */
    return make_float32((sign << 31) |
                        ((uint32_t)(127 + p - factor + (int)(mant >> 24)) << 23) |
                        (uint32_t)(mant & 0x7fffff));
}

/* tsrcdest.fp32[n] + tmpf32: RNE, no DAZ, FTZ (U596 rule), MXCSR untouched */
static float32 amx_add32_nodaz(float32 a, float32 b)
{
    float_status st;
    float32 r;

    amx_fp_status(&st);
    r = amx_nan_result(float32_add(a, b, &st), &st);
    return amx_ftz32(r);
}

static void amx_tmul_fp8(CPUX86State *env, int op, unsigned d, unsigned s1, unsigned s2)
{
    bool hf1 = op == AMX_TDPHBF8PS || op == AMX_TDPHF8PS;      /* tsrc1 E4M3 */
    bool hf2 = op == AMX_TDPBHF8PS || op == AMX_TDPHF8PS;      /* tsrc2 E4M3 */
    int factor = hf1 && hf2 ? 18 : !hf1 && !hf2 ? 32 : 25;
    unsigned rows = AMX_ROWS(env, d), kk = AMX_COLSB(env, s1) / 4;
    unsigned nn = AMX_COLSB(env, d) / 4, m, k, n, i;

    for (m = 0; m < rows; m++) {
        uint64_t hi[16], lo[16];
        bool nan[16], pinf[16], ninf[16], inval[16];

        for (n = 0; n < nn; n++) {
            hi[n] = lo[n] = 0;
            nan[n] = pinf[n] = ninf[n] = inval[n] = false;
        }
        for (k = 0; k < kk; k++) {
            const uint8_t *a = AMX_ROW(env, s1, m) + 4 * k;
            for (n = 0; n < nn; n++) {
                const uint8_t *b = AMX_ROW(env, s2, k) + 4 * n;
                for (i = 0; i < 4; i++) {
                    uint64_t ma = 0, mb = 0, plo, phi;
                    bool na, nb;
                    int ca = amx_fp8_fix(a[i], hf1, &ma, &na);
                    int cb = amx_fp8_fix(b[i], hf2, &mb, &nb);

                    if (ca == AMX_FP8_NAN || cb == AMX_FP8_NAN) {
                        nan[n] = true;
                    } else if (ca != AMX_FP8_FINITE || cb != AMX_FP8_FINITE) {
                        if ((ca == AMX_FP8_FINITE && ma == 0) ||
                            (cb == AMX_FP8_FINITE && mb == 0)) {
                            inval[n] = true;                    /* INF * 0 */
                        } else if (na != nb) {
                            ninf[n] = true;
                        } else {
                            pinf[n] = true;
                        }
                    } else {
                        mulu64(&plo, &phi, ma, mb);             /* phi = 0: ma, mb < 2^32 */
                        if (na != nb) {                         /* temp1 -= product */
                            hi[n] -= phi + (lo[n] < plo);
                            lo[n] -= plo;
                        } else {                                /* temp1 += product */
                            lo[n] += plo;
                            hi[n] += phi + (lo[n] < plo);
                        }
                    }
                }
            }
        }
        for (n = 0; n < nn; n++) {
            uint8_t *c = AMX_ROW(env, d, m) + 4 * n;
            float32 acc = make_float32(ldl_le_p(c)), tmpf32;
            uint32_t res;

            if (nan[n] || float32_is_any_nan(acc) || inval[n] || (pinf[n] && ninf[n])) {
                res = 0xffc00000u;                              /* QNaN indefinite */
            } else {
                tmpf32 = pinf[n] ? float32_infinity :
                         ninf[n] ? float32_set_sign(float32_infinity, 1) :
                         amx_fix128_to_fp32(hi[n], lo[n], factor);
                res = float32_val(amx_add32_nodaz(acc, tmpf32));
            }
            stl_le_p(c, res);
        }
        memset(AMX_ROW(env, d, m) + 4 * nn, 0, AMX_P1_BYTES_PER_ROW - 4 * nn);
    }
    amx_tmul_finish(env, d);
}
#endif /* __Use_Original_Qemu (U724) */

#if __Use_Original_Qemu != 1 /* ours (U177) */
/* info = op | tsrcdest << 4 | tsrc1 << 8 | tsrc2 << 12 (each 0..15) */
void helper_amx_tmul(CPUX86State *env, uint32_t info)
{
    uintptr_t ra = GETPC();
    int op = info & 15;
    unsigned d = (info >> 4) & 15, s1 = (info >> 8) & 15, s2 = (info >> 12) & 15;

    amx_check_enabled(env, ra);
    amx_check_e4(env, d, s1, s2, ra);
    switch (op) {
    case AMX_TDPBSSD:
    case AMX_TDPBSUD:
    case AMX_TDPBUSD:
    case AMX_TDPBUUD:
        amx_tmul_int8(env, op, d, s1, s2);
        break;
#if __Use_Original_Qemu != 1 /* ours (U178) */
    case AMX_TDPBF16PS:
        amx_tmul_fp(env, d, s1, s2, amx_cvt_bf16, amx_pair_dot);
        break;
#endif /* __Use_Original_Qemu (U178) */
#if __Use_Original_Qemu != 1 /* ours (U179) */
    case AMX_TDPFP16PS:
        amx_tmul_fp(env, d, s1, s2, amx_cvt_fp16, amx_pair_dot);
        break;
#endif /* __Use_Original_Qemu (U179) */
#if __Use_Original_Qemu != 1 /* ours (U180) */
    case AMX_TCMMIMFP16PS:
        amx_tmul_fp(env, d, s1, s2, amx_cvt_fp16, amx_pair_cmm_im);
        break;
    case AMX_TCMMRLFP16PS:
        amx_tmul_fp(env, d, s1, s2, amx_cvt_fp16, amx_pair_cmm_rl);
        break;
#endif /* __Use_Original_Qemu (U180) */
#if __Use_Original_Qemu != 1 /* ours (U724) */
    case AMX_TDPBF8PS:
    case AMX_TDPBHF8PS:
    case AMX_TDPHBF8PS:
    case AMX_TDPHF8PS:
        amx_tmul_fp8(env, op, d, s1, s2);
        break;
#endif /* __Use_Original_Qemu (U724) */
    default:
        g_assert_not_reached();
    }
}
#endif /* __Use_Original_Qemu (U177) */

#if __Use_Original_Qemu != 1 /* ours (U725) */
/*
 * NoVmp (ledger U725): AMX-AVX512 (ISE 319433-062 3.7) - TCVTROWD2PS, TCVTROWPS2BF16H/L,
 * TCVTROWPS2PHH/L, TILEMOVROW, register (AMX-E8-EVEX) and immediate (AMX-E7-EVEX) forms.
 * info = op | zmm << 8 | tmm << 16 (gen_amx_rowop), row = r32 or imm8. The run-time part of
 * AMX-E7/E8-EVEX, in this order (the static #UDs came first, validate_amx_evex; the classes
 * list no priority - the same choice as U175: XCR0 #UDs, then #NM, then the TILECFG #UDs):
 *   #UD CR4.OSXSAVE != 1, XCR0[18:17] != 11b, XCR0[7:5] != 111b, XCR0[2:1] != 11b;
 *   #NM CR0.TS = 1 (IA32_XFD_ERR unchanged, SDM Vol1 13.14), then #NM XFD[18] (XFD_ERR set);
 *   #UD TILES_CONFIGURED = 0, tsrc not a valid tile / name >= max_names, tsrc.colsb % 4 != 0.
 * ISE 319433-062 lists TILEMOVROW r32 as AMX-E7-EVEX and imm8 as AMX-E8-EVEX, the reverse of
 * the TCVTROW* pages; E7's "EVEX.VVVV != 1111b" cannot apply to the form whose vvvv is the r32
 * operand, so the classes are taken as for TCVTROW* (Intel XED amx-dmr-isa: TILEMOVROW imm8
 * AMX-E7-EVEX, r32 AMX-E8-EVEX). Operation: row_index := row & 0xf; row_index >= tsrc.rows ->
 * zmm1 := 0; else element i < tsrc.colsb / 4 (bytes < colsb for TILEMOVROW) converted, the
 * rest zero; zero_tileconfig_start(). No MXCSR access, RNE, no SIMD exception.
 */
enum {
    AMX_TCVTROWD2PS, AMX_TCVTROWPS2BF16H, AMX_TCVTROWPS2BF16L, AMX_TCVTROWPS2PHH,
    AMX_TCVTROWPS2PHL, AMX_TILEMOVROW,
};

/* convert_fp32_to_bfloat16 (ISE 3.4), literally */
static uint16_t amx_cvt_fp32_to_bf16(uint32_t x)
{
    if ((x & 0x7f800000) == 0) {                    /* zero or denormal: signed zero */
        return (x >> 16) & 0x8000;
    }
    if ((x & 0x7fffffff) == 0x7f800000) {           /* infinity */
        return x >> 16;
    }
    if ((x & 0x7f800000) == 0x7f800000) {           /* NaN: truncate, force QNaN */
        return (x >> 16) | 0x0040;
    }
    return (x + 0x7fff + ((x >> 16) & 1)) >> 16;    /* normal: RNE by integer add */
}

/* vCvt_s2h with RNE: FP32 denormal inputs become FP16 zeros, FP16 denormal outputs kept */
static uint16_t amx_cvt_fp32_to_fp16(uint32_t x)
{
    float_status st;

    if ((x & 0x7f800000) == 0) {
        return (x >> 16) & 0x8000;                  /* zero / denormal -> zero of its sign */
    }
    if ((x & 0x7f800000) == 0x7f800000 && (x & 0x007fffff)) {
        /* NaN: quieted, the upper fraction bits kept (VCVTPS2PH, SDM Vol2C) */
        return ((x >> 16) & 0x8000) | 0x7e00 | ((x >> 13) & 0x3ff);
    }
    memset(&st, 0, sizeof(st));
    set_float_rounding_mode(float_round_nearest_even, &st);
    return float32_to_float16(make_float32(x), true, &st);
}

void helper_amx_rowop(CPUX86State *env, uint32_t info, uint32_t row)
{
    const uint64_t avx512 = XSTATE_SSE_MASK | XSTATE_YMM_MASK | XSTATE_OPMASK_MASK |
                            XSTATE_ZMM_Hi256_MASK | XSTATE_Hi16_ZMM_MASK;
    uintptr_t ra = GETPC();
    int op = info & 15;
    unsigned z = (info >> 8) & 31, t = (info >> 16) & 31, r, i, cols;
    ZMMReg res;

    if (!(env->cr[4] & CR4_OSXSAVE_MASK) ||
        (env->xcr0 & XSTATE_AMX_MASK) != XSTATE_AMX_MASK ||
        (env->xcr0 & avx512) != avx512) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (env->cr[0] & CR0_TS_MASK) {
        raise_exception_ra(env, EXCP07_PREX, ra);
    }
    if (x86_cpu_xfd_armed(env) & XSTATE_XTILE_DATA_MASK) {
        env->msr_xfd_err = env->msr_xfd & XSTATE_XTILE_DATA_MASK;
        raise_exception_ra(env, EXCP07_PREX, ra);
    }
    if (!AMX_CONFIGURED(env) || !amx_tile_valid(env, t) || (AMX_COLSB(env, t) & 3)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    memset(&res, 0, sizeof(res));
    r = row & 0xf;
    cols = AMX_COLSB(env, t);
    if (r < AMX_ROWS(env, t)) {
        const uint8_t *src = AMX_ROW(env, t, r);

        if (op == AMX_TILEMOVROW) {
            for (i = 0; i < cols; i++) {
                res.ZMM_B(i) = src[i];
            }
        } else {
            for (i = 0; i < cols / 4; i++) {
                uint32_t x = ldl_le_p(src + 4 * i);

                switch (op) {
                case AMX_TCVTROWD2PS: {
                    float_status st;

                    memset(&st, 0, sizeof(st));
                    set_float_rounding_mode(float_round_nearest_even, &st);
                    res.ZMM_L(i) = float32_val(int32_to_float32((int32_t)x, &st));
                    break;
                }
                case AMX_TCVTROWPS2BF16H:
                    res.ZMM_W(2 * i + 1) = amx_cvt_fp32_to_bf16(x);
                    break;
                case AMX_TCVTROWPS2BF16L:
                    res.ZMM_W(2 * i) = amx_cvt_fp32_to_bf16(x);
                    break;
                case AMX_TCVTROWPS2PHH:
                    res.ZMM_W(2 * i + 1) = amx_cvt_fp32_to_fp16(x);
                    break;
                case AMX_TCVTROWPS2PHL:
                    res.ZMM_W(2 * i) = amx_cvt_fp32_to_fp16(x);
                    break;
                default:
                    g_assert_not_reached();
                }
            }
        }
    }
    env->xmm_regs[z] = res;
    AMX_START_ROW(env) = 0;
}
#endif /* __Use_Original_Qemu (U725) */

/* MMX/SSE */
/* XXX: optimize by storing fptt and fptags in the static cpu state */

#define SSE_DAZ             0x0040
#define SSE_RC_SHIFT        13
#define SSE_RC_MASK         (3 << SSE_RC_SHIFT)
#define SSE_FZ              0x8000

void update_mxcsr_status(CPUX86State *env)
{
    uint32_t mxcsr = env->mxcsr;
    int rnd_type;

    /* set rounding mode */
    rnd_type = (mxcsr & SSE_RC_MASK) >> SSE_RC_SHIFT;
    set_x86_rounding_mode(rnd_type, &env->sse_status);

    /* Set exception flags.  */
    set_float_exception_flags((mxcsr & FPUS_IE ? float_flag_invalid : 0) |
                              (mxcsr & FPUS_DE ? float_flag_input_denormal_used : 0) |
                              (mxcsr & FPUS_ZE ? float_flag_divbyzero : 0) |
                              (mxcsr & FPUS_OE ? float_flag_overflow : 0) |
                              (mxcsr & FPUS_UE ? float_flag_underflow : 0) |
                              (mxcsr & FPUS_PE ? float_flag_inexact : 0),
                              &env->sse_status);

    /* set denormals are zero */
    set_flush_inputs_to_zero((mxcsr & SSE_DAZ) ? 1 : 0, &env->sse_status);

    /* set flush to zero */
    set_flush_to_zero((mxcsr & SSE_FZ) ? 1 : 0, &env->sse_status);
    /*
     * x86 does flush-to-zero detection after rounding (the SDM
     * section 10.2.3.3 on the FTZ bit of MXCSR says that we flush
     * when we detect underflow, which x86 does after rounding).
     */
    set_float_ftz_detection(float_ftz_after_rounding, &env->sse_status);
#if __Use_Original_Qemu == 1 /* original QEMU (U96) */
    /* two NaNs: x87 rule (larger significand), as for fp_status */
#else /* ours (U96) */
    /*
     * NoVmp (ledger U96): two NaN sources -> SRC1, quieted (SDM Vol1 4.8.3.5 Table 4-8,
     * SSE/AVX column; pickNaN in fpu/softfloat-specialize.c.inc). MINPS/MAXPS (SRC2 on any
     * NaN) and the FMA order (Q(x), Q(y), Q(z), Vol1 Table 14-17) do not depend on it.
     */
    set_use_first_nan(true, &env->sse_status);
#endif /* __Use_Original_Qemu (U96) */
#if __Use_Original_Qemu != 1 /* ours (U445) */
    /*
     * NoVmp (ledger U445): MXCSR.UM = 0 / MXCSR.OM = 0 change what the rounding
     * reports (SDM Vol1 4.9.1.5, 4.9.1.6, 10.2.3.3): exact tiny results raise
     * #U, FTZ is ignored, PE comes from the unbounded-exponent rounding.
     */
    env->sse_status.unmasked_underflow = !(mxcsr & (1 << 11));
    env->sse_status.unmasked_overflow = !(mxcsr & (1 << 10));
#endif /* __Use_Original_Qemu (U445) */
}

void update_mxcsr_from_sse_status(CPUX86State *env)
{
    int flags = get_float_exception_flags(&env->sse_status);
    env->mxcsr |= ((flags & float_flag_invalid ? FPUS_IE : 0) |
                   (flags & float_flag_input_denormal_used ? FPUS_DE : 0) |
                   (flags & float_flag_divbyzero ? FPUS_ZE : 0) |
                   (flags & float_flag_overflow ? FPUS_OE : 0) |
                   (flags & float_flag_underflow ? FPUS_UE : 0) |
                   (flags & float_flag_inexact ? FPUS_PE : 0) |
                   (flags & float_flag_output_denormal ? FPUS_UE | FPUS_PE :
                    0));
}

#if __Use_Original_Qemu != 1 /* ours (U67) */
/*
 * NoVmp (ledger U67): SIMD floating-point exceptions (SDM Vol1 11.5, Vol3
 * 6.15 #XM). An SSE/AVX FP instruction whose new MXCSR exceptions include an
 * unmasked one does not write its destination; the flags are still set
 * (an unmasked pre-computation exception IE/DE/ZE suppresses OE/UE/PE) and
 * #XM is raised, #UD when CR4.OSXMMEXCPT = 0 (i5-13600K: DIVPS 0/0 with
 * IM = 0 -> #XM, destination unchanged, MXCSR.IE = 1).
 */
void helper_sse_fp_begin(CPUX86State *env)
{
    env->xm_saved_flags = get_float_exception_flags(&env->sse_status);
    set_float_exception_flags(0, &env->sse_status);
    env->xm_cc_src = env->cc_src;
}

static int sse_flags_to_mxcsr(int flags)
{
    return (flags & float_flag_invalid ? FPUS_IE : 0) |
           (flags & float_flag_input_denormal_used ? FPUS_DE : 0) |
           (flags & float_flag_divbyzero ? FPUS_ZE : 0) |
           (flags & float_flag_overflow ? FPUS_OE : 0) |
           (flags & float_flag_underflow ? FPUS_UE : 0) |
           (flags & float_flag_inexact ? FPUS_PE : 0) |
           (flags & float_flag_output_denormal ? FPUS_UE | FPUS_PE : 0);
}

/* dst: env offset of the destination register (len bytes, 0 = none) */
void helper_sse_fp_end(CPUX86State *env, uint32_t dst, uint32_t len)
{
    int raised = get_float_exception_flags(&env->sse_status);
    int unmasked = sse_flags_to_mxcsr(raised) & ~(env->mxcsr >> 7) & 0x3f;

    if (unmasked) {
        if (unmasked & (FPUS_IE | FPUS_DE | FPUS_ZE)) {
            raised &= ~(float_flag_overflow | float_flag_underflow | float_flag_inexact |
                        float_flag_output_denormal);
        }
        set_float_exception_flags(env->xm_saved_flags | raised, &env->sse_status);
        if (len) {
            memcpy((uint8_t *)env + dst, &env->xm_save, len);
        }
        env->cc_src = env->xm_cc_src;
        update_mxcsr_from_sse_status(env);
        raise_exception_ra(env, (env->cr[4] & CR4_OSXMMEXCPT_MASK) ? EXCP13_XM : EXCP06_ILLOP,
                           GETPC());
    }
    set_float_exception_flags(env->xm_saved_flags | raised, &env->sse_status);
}
#endif /* __Use_Original_Qemu (U67) */

#if __Use_Original_Qemu != 1 /* ours (U446) */
/*
 * NoVmp (ledger U446): multi-step SSE FP instructions (DPPS, DPPD). "Exceptions
 * are determined separately for each add and multiply operation, in the order of
 * their execution", with "if unmasked exception reported, execute exception
 * handler" after each step (SDM Vol2A DPPS/DPPD Operation, Exceptions).
 * sse_fp_step() closes one step: its flags (an unmasked IE/DE/ZE suppresses that
 * step's OE/UE/PE, Vol1 11.5.3) are OR-ed into *acc; true when one of them is
 * unmasked, and the instruction then stops in sse_fp_step_raise() without writing
 * its destination, keeping the flags of the earlier steps. i5-13600K: DPPS with
 * DM = 0, a tiny inexact product then the add of that denormal -> #XM with DE, UE
 * and PE; MAX * 2 - MAX * 2 with OM = 0 -> #XM with OE only (no IE from inf - inf).
 */
static bool sse_fp_step(CPUX86State *env, int *acc)
{
    int f = get_float_exception_flags(&env->sse_status);
    int unmasked = sse_flags_to_mxcsr(f) & ~(env->mxcsr >> 7) & 0x3f;

    if (unmasked & (FPUS_IE | FPUS_DE | FPUS_ZE)) {
        f &= ~(float_flag_overflow | float_flag_underflow | float_flag_inexact |
               float_flag_output_denormal);
    }
    *acc |= f;
    set_float_exception_flags(0, &env->sse_status);
    return unmasked != 0;
}

/* stop after an unmasked step: flags of all executed steps, #XM (#UD if CR4.OSXMMEXCPT = 0) */
static void sse_fp_step_raise(CPUX86State *env, int acc, uintptr_t ra)
{
    set_float_exception_flags(env->xm_saved_flags | acc, &env->sse_status);
    update_mxcsr_from_sse_status(env);
    raise_exception_ra(env, (env->cr[4] & CR4_OSXMMEXCPT_MASK) ? EXCP13_XM : EXCP06_ILLOP, ra);
}
#endif /* __Use_Original_Qemu (U446) */

void helper_update_mxcsr(CPUX86State *env)
{
    update_mxcsr_from_sse_status(env);
}

void helper_ldmxcsr(CPUX86State *env, uint32_t val)
{
#if __Use_Original_Qemu != 1 /* ours (U40) */
    check_mxcsr(env, val, GETPC());
#endif /* __Use_Original_Qemu (U40) */
    cpu_set_mxcsr(env, val);
}

void helper_enter_mmx(CPUX86State *env)
{
    env->fpstt = 0;
    *(uint32_t *)(env->fptags) = 0;
    *(uint32_t *)(env->fptags + 4) = 0;
}

void helper_emms(CPUX86State *env)
{
    /* set to empty state */
    *(uint32_t *)(env->fptags) = 0x01010101;
    *(uint32_t *)(env->fptags + 4) = 0x01010101;
}

void helper_movq(CPUX86State *env, void *d, void *s)
{
    (void)env;
    memcpy(d, s, sizeof(MMXReg));
}

#if __Use_Original_Qemu != 1 /* ours (U43) */
/*
 * NoVmp (ledger U43): MIN/MAX(SS/SD/PS/PD, legacy and VEX). Intel: (-0,0),
 * (NaN, anything) and (anything, NaN) return the second argument. Under
 * MXCSR.DAZ a denormal source is replaced by a signed zero *before* the
 * operation, so the value written is that zero, not the original denormal
 * bits (measured on i5-13600K, emu-uc72-risk R9). The squash raises
 * float_flag_input_denormal only, which does not map to MXCSR.DE.
 */
static inline float32 sse_min_f32(float32 a, float32 b, float_status *s)
{
    a = float32_squash_input_denormal(a, s);
    b = float32_squash_input_denormal(b, s);
    return float32_lt(a, b, s) ? a : b;
}

static inline float32 sse_max_f32(float32 a, float32 b, float_status *s)
{
    a = float32_squash_input_denormal(a, s);
    b = float32_squash_input_denormal(b, s);
    return float32_lt(b, a, s) ? a : b;
}

static inline float64 sse_min_f64(float64 a, float64 b, float_status *s)
{
    a = float64_squash_input_denormal(a, s);
    b = float64_squash_input_denormal(b, s);
    return float64_lt(a, b, s) ? a : b;
}

static inline float64 sse_max_f64(float64 a, float64 b, float_status *s)
{
    a = float64_squash_input_denormal(a, s);
    b = float64_squash_input_denormal(b, s);
    return float64_lt(b, a, s) ? a : b;
}

#endif /* __Use_Original_Qemu (U43) */
#if __Use_Original_Qemu != 1 /* ours (U81) */
/*
 * NoVmp (ledger U81): Intel RCPPS/RCPSS and RSQRTPS/RSQRTSS, analytic model.
 * The 12-bit result is the reciprocal (square root) of the MIDPOINT of the
 * input interval selected by the top 11 (RCP) / top 10 + exponent parity
 * (RSQRT) mantissa bits, rounded to nearest at 12 fraction bits. Computed
 * here in exact integer arithmetic (no table, no host FP state). Verified
 * against the i5-13600K on all 2^32 inputs under six MXCSR settings (RC
 * nearest/up/down/zero, FTZ+DAZ): 0 mismatches. MXCSR is ignored, no flags,
 * no #XM; 0/denormal -> +-inf, tiny results -> +-0 (SDM "always flushed"),
 * NaN -> quieted, RSQRT of a negative non-zero normal or -inf -> default NaN.
 */
static uint32_t x86_rcp12(uint32_t x)
{
    uint32_t sign = x & 0x80000000u, e = (x >> 23) & 0xff, m = x & 0x7fffff;
    uint32_t d, q;
    int re;

    if (e == 0xff) {
        return m ? (x | 0x00400000u) : sign;
    }
    if (e == 0) {
        return sign | 0x7f800000u;
    }
    d = 4097 + 2 * (m >> 12);                   /* midpoint = d / 4096 */
    q = ((1u << 26) + d) / (2 * d);             /* round(2^25 / d), d odd: no tie */
    re = 253 - (int)e;                          /* result = (q / 8192) * 2^(127-e) */
    if (re <= 0) {
        return sign;                            /* tiny: flushed to zero */
    }
    return sign | ((uint32_t)re << 23) | ((q - 4096) << 11);
}

static uint32_t x86_rsqrt12(uint32_t x)
{
    uint32_t sign = x & 0x80000000u, e = (x >> 23) & 0xff, m = x & 0x7fffff;
    uint64_t n4, d;
    uint32_t k;
    int ue, fe;

    if (e == 0xff) {
        if (m) {
            return x | 0x00400000u;
        }
        return sign ? 0xffc00000u : 0;
    }
    if (e == 0) {
        return sign | 0x7f800000u;
    }
    if (sign) {
        return 0xffc00000u;
    }
    ue = (int)e - 127;
    d = 2049 + 2 * (m >> 13);                   /* midpoint = d / 2048 (x2 if ue odd) */
    n4 = (ue & 1) ? (1ull << 38) : (1ull << 39); /* 4 * 2^36 or 4 * 2^37 */
    /* q = round(sqrt(n4 / 4 / d)) = largest k with (2k-1)^2 d <= n4 (never equal) */
    k = (uint32_t)sqrt((double)(n4 / 4) / (double)d) + 2;
    while ((uint64_t)(2 * k - 1) * (2 * k - 1) * d > n4) {
        k--;
    }
    fe = 126 - ((ue - (ue & 1)) / 2);
    if (k == 8192) {                            /* not reached; kept for range */
        k = 4096;
        fe++;
    }
    return ((uint32_t)fe << 23) | ((k - 4096) << 11);
}

#endif /* __Use_Original_Qemu (U81) */
#if __Use_Original_Qemu != 1 /* ours (U709) */
/* U709: would an access of [addr, addr + size) fault (#PF, #GP/#SS, Unicorn-unmapped)? */
static bool x86_access_would_fault(CPUX86State *env, target_ulong addr, int size,
                                   MMUAccessType type, uintptr_t ra)
{
    struct uc_struct *uc = env->uc;
    int mmu_idx = cpu_mmu_index(env, false);
    target_ulong p = addr, n;
    int left = size;

    while (left > 0) {
        void *host;
        target_ulong paddr;
        MemoryRegion *mr;

        n = TARGET_PAGE_SIZE - (p & ~TARGET_PAGE_MASK);
        if (n > (target_ulong)left) {
            n = left;
        }
        if (probe_access_flags(env, p, type, mmu_idx, true, &host, ra) & TLB_INVALID_MASK) {
            return true;
        }
        if (!tlb_vaddr_to_paddr(env, p, type, mmu_idx, &paddr)) {
            return true;
        }
        mr = uc->memory_mapping(uc, paddr);
        if (mr == NULL ||
            !(mr->perms & (type == MMU_DATA_STORE ? UC_PROT_WRITE : UC_PROT_READ))) {
            return true;
        }
        p += n;
        left -= (int)n;
    }
    return false;
}

/*
 * U709: before the access of a gather / scatter element: if a data breakpoint hit by an
 * earlier element of this execution is pending and this access would fault, that #DB is
 * delivered instead (RF = 1; x86_deliver_pending_data_bp, bpt_helper.c).
 */
static void x86_vsib_elem_check(CPUX86State *env, target_ulong addr, int size,
                                MMUAccessType type, uintptr_t ra)
{
    if (env_cpu(env)->watchpoint_hit && x86_access_would_fault(env, addr, size, type, ra)) {
        x86_deliver_pending_data_bp(env, ra);
    }
}
#endif /* __Use_Original_Qemu (U709) */
#define SHIFT 0
#include "ops_sse.h"

#define SHIFT 1
#include "ops_sse.h"

#define SHIFT 2
#include "ops_sse.h"
#if __Use_Original_Qemu != 1 /* ours (U143) */

#define SHIFT 3
#include "ops_sse.h"
#endif /* __Use_Original_Qemu (U143) */
#if __Use_Original_Qemu != 1 /* ours (U146) */

/*
 * NoVmp (ledger U146): EVEX masking and masked memory (SDM Vol2A 2.7.4, 2.8; Vol1 15.6).
 * desc (emit.c.inc EVEX_DESC): bits 1:0 element size log2, bits 15:8 element count,
 * bit 16 zeroing-masking, bit 17 "one memory element shared by all lanes" (embedded or
 * Tuple1 broadcast: loaded when any lane is active).
 */
#define EVEX_DESC_ESZ(d)   ((d) & 3)
#define EVEX_DESC_N(d)     (((d) >> 8) & 0xff)
#define EVEX_DESC_Z        (1 << 16)
#define EVEX_DESC_ANY      (1 << 17)
#if __Use_Original_Qemu != 1 /* ours (U834) */
#define EVEX_DESC_AC       (1 << 19)  /* the instruction reports #AC (emit.c.inc) */
#endif /* __Use_Original_Qemu (U834) */

static uint64_t evex_get_elem(ZMMReg *r, int esz, int i)
{
    switch (esz) {
    case MO_8:
        return r->ZMM_B(i);
    case MO_16:
        return r->ZMM_W(i);
    case MO_32:
        return r->ZMM_L(i);
    default:
        return r->ZMM_Q(i);
    }
}

static void evex_set_elem(ZMMReg *r, int esz, int i, uint64_t v)
{
    switch (esz) {
    case MO_8:
        r->ZMM_B(i) = v;
        break;
    case MO_16:
        r->ZMM_W(i) = v;
        break;
    case MO_32:
        r->ZMM_L(i) = v;
        break;
    default:
        r->ZMM_Q(i) = v;
        break;
    }
}

static uint64_t evex_lanes(int n)
{
    return n >= 64 ? ~0ull : (1ull << n) - 1;
}

/* E1 (aligned vector moves): #GP(0) unless the memory operand is VL-aligned, whatever the mask */
void helper_evex_align(CPUX86State *env, target_ulong a0, uint32_t align_mask)
{
    if (a0 & align_mask) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
}

/*
 * Masked load with fault suppression: only the elements whose mask bit is set are read
 * (a masked-off element never faults); the others are zero in *d.
 */
void helper_evex_mload(CPUX86State *env, ZMMReg *d, target_ulong a0, uint64_t mask,
                       uint32_t desc)
{
    uintptr_t ra = GETPC();
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), i;
    int bytes = 1 << esz;
    ZMMReg r;

    memset(&r, 0, sizeof(r));
    if (desc & EVEX_DESC_ANY) {
        mask = (mask & evex_lanes(n)) ? 1 : 0;
        n = 1;
    }
#if __Use_Original_Qemu != 1 /* ours (U834) */
    /*
     * NoVmp (ledger U834): exception classes E2-E10 report #AC "for 2, 4, or 8 byte memory
     * access" (SDM Vol2A 2.8); a masked operand of that size (a scalar, a broadcast element,
     * a narrow tuple) is checked when an element is read (a fully masked-off operand reads
     * nothing).
     */
    if ((desc & EVEX_DESC_AC) && n * bytes <= 8 && (mask & evex_lanes(n))) {
        x86_ac_check(env, a0, n * bytes - 1, ra);
    }
#endif /* __Use_Original_Qemu (U834) */

    for (i = 0; i < n; i++) {
        if (mask & (1ull << i)) {
            target_ulong addr = a0 + i * bytes;
            switch (esz) {
            case MO_8:
                r.ZMM_B(i) = cpu_ldub_data_ra(env, addr, ra);
                break;
            case MO_16:
                r.ZMM_W(i) = cpu_lduw_data_ra(env, addr, ra);
                break;
            case MO_32:
                r.ZMM_L(i) = cpu_ldl_data_ra(env, addr, ra);
                break;
            default:
                r.ZMM_Q(i) = cpu_ldq_data_ra(env, addr, ra);
                break;
            }
        }
    }
    *d = r;
}

/* probe one element for a write, page by page (#PF before any byte is written) */
static void evex_probe_write(CPUX86State *env, target_ulong addr, int size, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env, false);
    target_ulong first = (target_ulong)0 - (addr | TARGET_PAGE_MASK);

    if (first >= (target_ulong)size) {
        probe_write(env, addr, size, mmu_idx, ra);
    } else {
        probe_write(env, addr, first, mmu_idx, ra);
        probe_write(env, addr + first, size - first, mmu_idx, ra);
    }
}

/* Unicorn: is the guest-physical page behind a (probed) virtual address mapped? */
static bool evex_mapped(CPUX86State *env, target_ulong addr)
{
    target_ulong paddr;

    if (!tlb_vaddr_to_paddr(env, addr, MMU_DATA_STORE, cpu_mmu_index(env, false), &paddr)) {
        return true;                    /* page fault: raised by the real access */
    }
    return env->uc->memory_mapping(env->uc, paddr) != NULL;
}

static void evex_store_elem(CPUX86State *env, ZMMReg *s, int esz, int i, target_ulong addr,
                            uintptr_t ra)
{
    switch (esz) {
    case MO_8:
        cpu_stb_data_ra(env, addr, s->ZMM_B(i), ra);
        break;
    case MO_16:
        cpu_stw_data_ra(env, addr, s->ZMM_W(i), ra);
        break;
    case MO_32:
        cpu_stl_data_ra(env, addr, s->ZMM_L(i), ra);
        break;
    default:
        cpu_stq_data_ra(env, addr, s->ZMM_Q(i), ra);
        break;
    }
}

/*
 * Masked store: only the elements whose mask bit is set are written. Every one of them is
 * probed first (#PF), and an element on memory Unicorn has not mapped is stored first:
 * if no UC_HOOK_MEM_WRITE_UNMAPPED hook maps it, the instruction stops there and memory is
 * unchanged (a plain Unicorn store would only request the exit and go on writing).
 */
void helper_evex_mstore(CPUX86State *env, ZMMReg *s, target_ulong a0, uint64_t mask,
                        uint32_t desc)
{
    uintptr_t ra = GETPC();
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), i;
    int bytes = 1 << esz;
    struct uc_struct *uc = env->uc;

#if __Use_Original_Qemu != 1 /* ours (U834) */
    /* a masked 2/4/8-byte operand with an active element: #AC first (as helper_evex_mload) */
    if ((desc & EVEX_DESC_AC) && n * bytes <= 8 && (mask & evex_lanes(n))) {
        x86_ac_check(env, a0, n * bytes - 1, ra);
    }
#endif /* __Use_Original_Qemu (U834) */

#if __Use_Original_Qemu != 1 /* ours (U193) */
    /*
     * NoVmp (ledger U193): exception classes E*NF ("no fault suppression", SDM Vol2A 2.8,
     * Table 2-44): a fault on a masked-off element is reported as well, so every byte of
     * the operand is probed for a write (#PF) before anything is written; only the active
     * elements are written. U210 (merged here): a page that Unicorn has not mapped is
     * reported through a one-byte read of it (UC_HOOK_MEM_UNMAPPED / #PF in emu-alltest), so
     * that a masked-off element on it is never skipped silently; if a hook maps the page the
     * instruction goes on. The operand is at most 64 bytes: its first and last byte cover
     * every page it touches.
     */
    if (desc & (1 << 18)) {
        target_ulong last = a0 + n * bytes - 1;

        evex_probe_write(env, a0, n * bytes, ra);
        if (!evex_mapped(env, a0) || !evex_mapped(env, last)) {
            (void)cpu_ldub_data_ra(env, evex_mapped(env, a0) ? last : a0, ra);
            if (uc->invalid_error != UC_ERR_OK && uc->nested_level > 0 && !uc->cpu->stopped) {
                cpu_loop_exit_restore(uc->cpu, ra);
            }
        }
    }
#endif /* __Use_Original_Qemu (U193) */
    for (i = 0; i < n; i++) {
        if (mask & (1ull << i)) {
            evex_probe_write(env, a0 + i * bytes, bytes, ra);
        }
    }
    for (i = 0; i < n; i++) {
        target_ulong addr = a0 + i * bytes;

        if ((mask & (1ull << i)) &&
            (!evex_mapped(env, addr) || !evex_mapped(env, addr + bytes - 1))) {
            evex_store_elem(env, s, esz, i, addr, ra);
            if (uc->invalid_error != UC_ERR_OK && uc->nested_level > 0 && !uc->cpu->stopped) {
                cpu_loop_exit_restore(uc->cpu, ra);
            }
        }
    }
    for (i = 0; i < n; i++) {
        if (mask & (1ull << i)) {
            evex_store_elem(env, s, esz, i, a0 + i * bytes, ra);
        }
    }
}

/* merging-/zeroing-masking of a computed result s into the destination register d */
void helper_evex_blend(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint64_t mask, uint32_t desc)
{
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), i;

    for (i = 0; i < n; i++) {
        if (mask & (1ull << i)) {
            evex_set_elem(d, esz, i, evex_get_elem(s, esz, i));
        } else if (desc & EVEX_DESC_Z) {
            evex_set_elem(d, esz, i, 0);
        }
    }
}

/*
 * Floating point: a copy of a source with +1.0 in the masked-off lanes, so that they
 * raise no MXCSR flag and no #XM (SDM Vol1 15.6.3: exceptions are reported only for
 * the elements that are written).
 */
void helper_evex_neutral(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint64_t mask, uint32_t desc)
{
    static const uint64_t one[4] = { 0, 0x3c00, 0x3f800000, 0x3ff0000000000000ull };
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), i;

    for (i = 0; i < n; i++) {
        evex_set_elem(d, esz, i, (mask & (1ull << i)) ? evex_get_elem(s, esz, i) : one[esz]);
    }
}
#endif /* __Use_Original_Qemu (U146) */
#if __Use_Original_Qemu != 1 /* ours (U250) */

/*
 * NoVmp (ledger U250): EVEX gathers (exception class E12; SDM Vol2C VPGATHERDD/DQ,
 * VPGATHERQD/QQ, VGATHERDPS/DPD, VGATHERQPS/QPD "Operation"). desc: emit.c.inc
 * gen_evex_vsib. a0 = BASE + DISP, seg = the segment base.
 * FOR j := 0 TO KL-1: IF k1[j] THEN element j := MEM[ADDR(j)]; k1[j] := 0. The elements
 * are processed in element order and k1[j] is cleared as soon as element j is written,
 * so a fault (or a Unicorn unmapped access without a mapping hook, which leaves through
 * cpu_loop_exit_restore with RIP at the instruction) leaves exactly the elements below the
 * faulting one completed and their mask bits clear: the restart gathers only the rest
 * ("faults are delivered in a right-to-left manner"). Only on completion k1[MAX_KL-1:KL]
 * := 0 (k1 is 0 then) and DEST[MAXVL-1:KL*data size] := 0 (VL, VL/2 for QD/QPS); a fault
 * leaves those parts unchanged, which the SDM allows ("may update ... even if").
 */
#define EVEX_VSIB_K(d)      ((d) & 7)
#define EVEX_VSIB_DQ        (1u << 3)
#define EVEX_VSIB_IQ        (1u << 4)
#define EVEX_VSIB_KL(d)     (((d) >> 8) & 0xff)
#define EVEX_VSIB_SCALE(d)  (((d) >> 16) & 3)
#define EVEX_VSIB_A64       (1u << 18)
#define EVEX_VSIB_L32       (1u << 19)

/*
 * ADDR(j) = BASE + SignExtend(VINDEX[j]) * SCALE + DISP; "the most significant bits beyond
 * the number of address bits are ignored": truncated to the address size, then the
 * segment base is added (linear addresses wrap at 32 bits outside 64-bit mode)
 */
static target_ulong evex_vsib_addr(ZMMReg *v, uint32_t desc, int j, target_ulong a0,
                                   target_ulong seg)
{
    target_ulong idx = (desc & EVEX_VSIB_IQ) ? (target_ulong)(int64_t)v->ZMM_Q(j)
                                             : (target_ulong)(int64_t)(int32_t)v->ZMM_L(j);
    target_ulong ea = a0 + (idx << EVEX_VSIB_SCALE(desc));

    if (!(desc & EVEX_VSIB_A64)) {
        ea = (uint32_t)ea;
    }
    ea += seg;
    if (desc & EVEX_VSIB_L32) {
        ea = (uint32_t)ea;
    }
    return ea;
}

void helper_evex_gather(CPUX86State *env, ZMMReg *d, ZMMReg *v, target_ulong a0,
                        target_ulong seg, uint32_t desc)
{
    uintptr_t ra = GETPC();
    uint64_t *k = &env->opmask_regs[EVEX_VSIB_K(desc)];
    bool q = desc & EVEX_VSIB_DQ;
    int kl = EVEX_VSIB_KL(desc), j;

    for (j = 0; j < kl; j++) {
        if (*k & (1ull << j)) {
            target_ulong addr = evex_vsib_addr(v, desc, j, a0, seg);

#if __Use_Original_Qemu != 1 /* ours (U709) */
            x86_vsib_elem_check(env, addr, q ? 8 : 4, MMU_DATA_LOAD, ra);
#endif /* __Use_Original_Qemu (U709) */
            if (q) {
                d->ZMM_Q(j) = cpu_ldq_data_ra(env, addr, ra);
            } else {
                d->ZMM_L(j) = cpu_ldl_data_ra(env, addr, ra);
            }
            *k &= ~(1ull << j);
        }
    }
    *k = 0;
    for (j = (kl << (q ? 3 : 2)) / 8; j < 8; j++) {
        d->ZMM_Q(j) = 0;
    }
}
#endif /* __Use_Original_Qemu (U250) */
#if __Use_Original_Qemu != 1 /* ours (U251) */

/*
 * NoVmp (ledger U251): EVEX scatters (class E12; SDM Vol2C VPSCATTERDD/DQ/QD/QQ,
 * VSCATTERDPS/DPD/QPS/QPD "Operation"): FOR j := 0 TO KL-1: IF k1[j] THEN MEM[ADDR(j)] :=
 * SRC[j]; k1[j] := 0, then k1 := 0. Element order, so writes to overlapping addresses
 * land LSB to MSB and the highest one wins ("writes to overlapping vector indices are
 * ordered ... from LSB to MSB"); a fault leaves the elements below it written and their
 * k1 bits clear, nothing of the faulting element written, RIP at the instruction.
 * Each element is probed first (#PF before any byte; U866: x86_probe_store, U592), and on
 * a page Unicorn cannot store as plain RAM (unmapped or read-only) the element's first
 * byte there is stored first with UC_HOOK_MEM_WRITE suppressed: the WRITE_UNMAPPED /
 * WRITE_PROT hooks run as for the real store, and when none fixes the page the instruction
 * stops there with nothing of the element written (a plain Unicorn store would only request
 * the exit and go on writing). UC_HOOK_MEM_WRITE sees only the real element stores (before
 * U866 it also saw a 1-byte store per probed byte).
 */
static void evex_scatter_elem(CPUX86State *env, target_ulong addr, uint64_t val, int size,
                              uintptr_t ra)
{
    uint8_t img[8];
    int i;

    for (i = 0; i < size; i++) {
        img[i] = (uint8_t)(val >> (8 * i));
    }
    x86_probe_store(env, addr, size, img, ra);
    if (size == 8) {
        cpu_stq_data_ra(env, addr, val, ra);
    } else {
        cpu_stl_data_ra(env, addr, (uint32_t)val, ra);
    }
}

void helper_evex_scatter(CPUX86State *env, ZMMReg *s, ZMMReg *v, target_ulong a0,
                         target_ulong seg, uint32_t desc)
{
    uintptr_t ra = GETPC();
    uint64_t *k = &env->opmask_regs[EVEX_VSIB_K(desc)];
    bool q = desc & EVEX_VSIB_DQ;
    int kl = EVEX_VSIB_KL(desc), j;

    for (j = 0; j < kl; j++) {
        if (*k & (1ull << j)) {
#if __Use_Original_Qemu != 1 /* ours (U709) */
            x86_vsib_elem_check(env, evex_vsib_addr(v, desc, j, a0, seg), q ? 8 : 4,
                                MMU_DATA_STORE, ra);
#endif /* __Use_Original_Qemu (U709) */
            evex_scatter_elem(env, evex_vsib_addr(v, desc, j, a0, seg),
                              q ? s->ZMM_Q(j) : s->ZMM_L(j), q ? 8 : 4, ra);
            *k &= ~(1ull << j);
        }
    }
    *k = 0;
}
#endif /* __Use_Original_Qemu (U251) */
#if __Use_Original_Qemu != 1 /* ours (U147) */

/*
 * NoVmp (ledger U147): {er} / {sae} (SDM Vol2A 2.7.8, 2.7.9, Table 2-38). rc = EVEX.L'L
 * as the rounding control (00 RNE, 01 RD, 10 RU, 11 RZ, the MXCSR.RC encoding) or -2 for
 * {sae}. Suppress all exceptions: masked responses (softfloat's default) and no MXCSR
 * flag set by the instruction; DAZ/FTZ stay in effect.
 */
void helper_evex_rc_begin(CPUX86State *env, int32_t rc)
{
    env->evex_saved_flags = get_float_exception_flags(&env->sse_status);
    env->evex_saved_rmode = get_float_rounding_mode(&env->sse_status);
    if (rc >= 0) {
        set_x86_rounding_mode(rc & 3, &env->sse_status);
    }
    set_float_exception_flags(0, &env->sse_status);
#if __Use_Original_Qemu != 1 /* ours (U147) */
    /*
     * U147 x U445: {er} / {sae} behave as if every MXCSR exception were masked, so the
     * unmasked-#U/#O rounding of U445 (FTZ ignored, exact tiny results reported) does not
     * apply: with MXCSR.UM = 0 and FTZ = 1 a tiny result is still flushed to zero.
     */
    env->sse_status.unmasked_underflow = false;
    env->sse_status.unmasked_overflow = false;
#endif /* __Use_Original_Qemu (U147) */
}

void helper_evex_rc_end(CPUX86State *env)
{
    set_float_exception_flags(env->evex_saved_flags, &env->sse_status);
    set_float_rounding_mode(env->evex_saved_rmode, &env->sse_status);
#if __Use_Original_Qemu != 1 /* ours (U147) */
    /* U147 x U445: back to MXCSR.UM / MXCSR.OM (update_mxcsr_status) */
    env->sse_status.unmasked_underflow = !(env->mxcsr & (1 << 11));
    env->sse_status.unmasked_overflow = !(env->mxcsr & (1 << 10));
#endif /* __Use_Original_Qemu (U147) */
}
#endif /* __Use_Original_Qemu (U147) */
#if __Use_Original_Qemu != 1 /* ours (U152) */

/*
 * NoVmp (ledger U152): VPCMP[U]B/W/D/Q, VPCMPEQx into a mask: bit j = (a[j] OP b[j]) for
 * the n elements, OP = desc bits 18:16 (0 EQ, 1 LT, 2 LE, 3 FALSE, 4 NEQ, 5 NLT, 6 NLE,
 * 7 TRUE), signed when desc bit 19 is set; bits n..63 are 0 (DEST[MAX_KL-1:KL] := 0).
 */
uint64_t helper_evex_pcmp(CPUX86State *env, ZMMReg *a, ZMMReg *b, uint32_t desc)
{
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), pred = (desc >> 16) & 7, i;
    bool sign = (desc >> 19) & 1;
    int bits = 8 << esz;
    uint64_t r = 0;

    for (i = 0; i < n; i++) {
        uint64_t x = evex_get_elem(a, esz, i), y = evex_get_elem(b, esz, i);
        bool lt, eq, c;

#if __Use_Original_Qemu != 1 /* ours (U154) */
        /* VPTESTM/VPTESTNM (U154): compare SRC1 AND SRC2 with 0 (pred NEQ / EQ) */
        if (desc & (1u << 20)) {
            x &= y;
            y = 0;
        }
#endif /* __Use_Original_Qemu (U154) */
        eq = x == y;

        if (sign) {
            int64_t sx = (int64_t)(x << (64 - bits)) >> (64 - bits);
            int64_t sy = (int64_t)(y << (64 - bits)) >> (64 - bits);
            lt = sx < sy;
        } else {
            lt = x < y;
        }
        switch (pred) {
        case 0:
            c = eq;
            break;
        case 1:
            c = lt;
            break;
        case 2:
            c = lt || eq;
            break;
        case 3:
            c = false;
            break;
        case 4:
            c = !eq;
            break;
        case 5:
            c = !lt;
            break;
        case 6:
            c = !(lt || eq);
            break;
        default:
            c = true;
            break;
        }
        if (c) {
            r |= 1ull << i;
        }
    }
    return r;
}
#endif /* __Use_Original_Qemu (U152) */
#if __Use_Original_Qemu != 1 /* ours (U159) */

/*
 * NoVmp (ledger U159): VPTERNLOGD/Q: every result bit is imm8[(a << 2) | (b << 1) | c] of
 * the bits of a (the destination before the instruction), b (EVEX.vvvv), c (r/m); desc =
 * imm8 | vector length in bytes << 8. d may be any of the sources.
 */
void helper_evex_pternlog(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, ZMMReg *c,
                          uint32_t desc)
{
    int imm = desc & 0xff, words = (desc >> 8) / 8, i, k;

    for (i = 0; i < words; i++) {
        uint64_t x = a->ZMM_Q(i), y = b->ZMM_Q(i), z = c->ZMM_Q(i), r = 0;

        for (k = 0; k < 8; k++) {
            if ((imm >> k) & 1) {
                r |= ((k & 4) ? x : ~x) & ((k & 2) ? y : ~y) & ((k & 1) ? z : ~z);
            }
        }
        d->ZMM_Q(i) = r;
    }
}
#endif /* __Use_Original_Qemu (U159) */
#if __Use_Original_Qemu != 1 /* ours (U197) */

/*
 * NoVmp (ledger U197): VCMPPS/PD/SS/SD into an opmask register (SDM Vol2A CMPPS/CMPPD,
 * Table 3-8 "Comparison Predicate for CMPPD and CMPPS Instructions"): bit j = SRC1[j] OP5
 * SRC2[j] for the n elements (desc bits 15:8; size log2 in bits 1:0), OP5 = imm8[4:0] in
 * desc bits 20:16. Predicates 0-15 give the true relations among {A > B, A < B, A = B,
 * unordered}; predicate p + 16 has the same relations with the opposite signalling
 * behaviour. A signalling predicate raises #IA for a QNaN operand as well (else only for
 * an SNaN); denormal operands set DE unless DAZ. Bits n..63 of the result are 0.
 */
uint64_t helper_evex_fcmp(CPUX86State *env, ZMMReg *a, ZMMReg *b, uint32_t desc)
{
    /* bit 0: A > B, bit 1: A < B, bit 2: A = B, bit 3: unordered (Table 3-8, rows 0H-FH) */
    static const uint8_t rel[16] = {
        0x4, 0x2, 0x6, 0x8, 0xb, 0xd, 0x9, 0x7,
        0xc, 0xa, 0xe, 0x0, 0x3, 0x5, 0x1, 0xf,
    };
    /* signalling ("Signals #IA on QNAN" = Yes) among predicates 0-15: 1, 2, 5, 6, 9, A, D, E */
    static const uint16_t sig16 = 0x6666;
    int esz = desc & 3, n = (desc >> 8) & 0xff, pred = (desc >> 16) & 31, i;
    bool sig = ((sig16 >> (pred & 15)) & 1) ^ (pred >> 4);
    uint64_t r = 0;

    for (i = 0; i < n; i++) {
        FloatRelation fr;
        int bit;

        if (esz == MO_64) {
            fr = sig ? float64_compare(a->ZMM_D(i), b->ZMM_D(i), &env->sse_status)
                     : float64_compare_quiet(a->ZMM_D(i), b->ZMM_D(i), &env->sse_status);
        } else {
            fr = sig ? float32_compare(a->ZMM_S(i), b->ZMM_S(i), &env->sse_status)
                     : float32_compare_quiet(a->ZMM_S(i), b->ZMM_S(i), &env->sse_status);
        }
        switch (fr) {
        case float_relation_greater:
            bit = 1;
            break;
        case float_relation_less:
            bit = 2;
            break;
        case float_relation_equal:
            bit = 4;
            break;
        default:
            bit = 8;
            break;
        }
        if (rel[pred & 15] & bit) {
            r |= 1ull << i;
        }
    }
    return r;
}
#endif /* __Use_Original_Qemu (U197) */
#if __Use_Original_Qemu != 1 /* ours (U211) */

/*
 * NoVmp (ledger U211): EVEX permutes, shuffles and unpacks, element by element as in the
 * SDM pseudocode (Vol2B PUNPCKLDQ/PUNPCKHDQ/PSHUFD/SHUFPS/SHUFPD/UNPCKLPS/UNPCKHPS, Vol2C
 * VPERMD/VPERMQ/VPERMPS/VPERMPD/VPERMI2x/VPERMT2x/VPERMILPS/VPERMILPD/VALIGND/VALIGNQ/
 * VSHUFF32x4). desc: EVEX_PERM_DESC (cpu.h). d = destination (register or scratch),
 * a = SRC1 (EVEX.vvvv), b = SRC2 (ModRM.r/m; {1toN} already replicated), c = the destination
 * register before the instruction (VPERMI2 indices, VPERMT2 table 1). The result is built
 * in a temporary: d may be any of the sources. Only the VL bytes of d are written.
 */
void helper_evex_perm(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, ZMMReg *c,
                      uint32_t desc)
{
    int imm = desc & 0xff, vl = (desc >> 8) & 0x7f, esz = (desc >> 16) & 3;
    int op = (desc >> 24) & 0xff;
    int n = vl >> esz, lane = 16 >> esz;    /* KL, elements per 128-bit lane */
    int oesz = esz, obytes = vl;            /* result element size / bytes written */
    ZMMReg r;
    int j;

#if __Use_Original_Qemu != 1 /* ours (U212) */
    int aux = (desc >> 20) & 3;

    if (op >= EVEX_PERM_PMOVTRUNC && op <= EVEX_PERM_PMOVUSAT) {
        oesz = aux;                         /* narrowing: KL source elements */
        obytes = n << aux;
    }
#endif /* __Use_Original_Qemu (U212) */
#if __Use_Original_Qemu != 1 /* ours (U215) */
    int cel = (1 << ((desc >> 20) & 15)) >> esz;    /* elements per chunk / group */
    int csel = imm & ((vl >> ((desc >> 20) & 15)) - 1);

    if (op == EVEX_PERM_EXTRACT) {
        n = cel;                            /* the chunk: a destination narrower than VL */
        obytes = n << esz;
    }
#endif /* __Use_Original_Qemu (U215) */
    memset(&r, 0, sizeof(r));
    for (j = 0; j < n; j++) {
        int l0 = j & ~(lane - 1), k = j & (lane - 1);
        uint64_t v, id;
        int sel, ch;

        switch (op) {
        case EVEX_PERM_UNPCKL:      /* per lane: a0 b0 a1 b1 ... (low half) */
            v = evex_get_elem((k & 1) ? b : a, esz, l0 + k / 2);
            break;
        case EVEX_PERM_UNPCKH:      /* per lane: high half */
            v = evex_get_elem((k & 1) ? b : a, esz, l0 + lane / 2 + k / 2);
            break;
        case EVEX_PERM_SHUFPS:      /* lane dwords 0,1 from SRC1, 2,3 from SRC2 */
            sel = (imm >> (2 * k)) & 3;
            v = evex_get_elem(k < 2 ? a : b, esz, l0 + sel);
            break;
        case EVEX_PERM_SHUFPD:      /* even qwords from SRC1, odd from SRC2; imm8[j] */
            v = evex_get_elem((j & 1) ? b : a, esz, (j & ~1) + ((imm >> j) & 1));
            break;
        case EVEX_PERM_PSHUFD:      /* Select4(SRC[lane], imm8[2k+1:2k]) */
            v = evex_get_elem(b, esz, l0 + ((imm >> (2 * k)) & 3));
            break;
        case EVEX_PERM_PERMILPD_I:  /* imm8[j] selects the qword of the lane */
            v = evex_get_elem(b, esz, (j & ~1) + ((imm >> j) & 1));
            break;
        case EVEX_PERM_PERMILPS_V:  /* Select4(SRC1[lane], SRC2[j][1:0]) */
            v = evex_get_elem(a, esz, l0 + (evex_get_elem(b, esz, j) & 3));
            break;
        case EVEX_PERM_PERMILPD_V:  /* SRC2[j][1] selects the qword of the lane */
            v = evex_get_elem(a, esz, (j & ~1) + ((evex_get_elem(b, esz, j) >> 1) & 1));
            break;
        case EVEX_PERM_PERM:        /* SRC2[SRC1[j] mod KL] */
            v = evex_get_elem(b, esz, evex_get_elem(a, esz, j) & (n - 1));
            break;
        case EVEX_PERM_PERMQ_I:     /* per 256 bits: imm8[2(j mod 4)+1:2(j mod 4)] */
            v = evex_get_elem(b, esz, (j & ~3) + ((imm >> (2 * (j & 3))) & 3));
            break;
        case EVEX_PERM_PERMI2:      /* DEST[j] bit log2(KL): SRC2, else SRC1 */
            id = evex_get_elem(c, esz, j);
            v = evex_get_elem((id & n) ? b : a, esz, id & (n - 1));
            break;
        case EVEX_PERM_PERMT2:      /* SRC1[j] bit log2(KL): SRC2, else DEST */
            id = evex_get_elem(a, esz, j);
            v = evex_get_elem((id & n) ? b : c, esz, id & (n - 1));
            break;
        case EVEX_PERM_ALIGN:       /* (SRC1:SRC2) >> (imm8 mod KL) elements */
            sel = j + (imm & (n - 1));
            v = sel < n ? evex_get_elem(b, esz, sel) : evex_get_elem(a, esz, sel - n);
            break;
        case EVEX_PERM_SHUF128:     /* 128-bit chunks: low half of the result from SRC1 */
            ch = j / lane;
            if (vl == 32) {
                sel = (imm >> ch) & 1;
                v = evex_get_elem(ch == 0 ? a : b, esz, sel * lane + k);
            } else {
                sel = (imm >> (2 * ch)) & 3;
                v = evex_get_elem(ch < 2 ? a : b, esz, sel * lane + k);
            }
            break;
#if __Use_Original_Qemu != 1 /* ours (U212) */
        /*
         * VPMOVZX/VPMOVSX: ZeroExtend / SignExtend of SRC element j (Vol2B PMOVZX/PMOVSX);
         * VPMOV: TruncateXToY; VPMOVS: SaturateSignedXToSignedY; VPMOVUS:
         * SaturateUnsignedXToUnsignedY (the source is unsigned) (Vol2C VPMOVDB/VPMOVSDB/...)
         */
        case EVEX_PERM_PMOVZX:
            v = evex_get_elem(b, aux, j);
            break;
        case EVEX_PERM_PMOVSX: {
            int sh = 64 - (8 << aux);

            v = (uint64_t)((int64_t)(evex_get_elem(b, aux, j) << sh) >> sh);
            break;
        }
        case EVEX_PERM_PMOVTRUNC:
            v = evex_get_elem(b, esz, j);
            break;
        case EVEX_PERM_PMOVSSAT: {
            int sh = 64 - (8 << esz), db = 8 << aux;
            int64_t x = (int64_t)(evex_get_elem(b, esz, j) << sh) >> sh;
            int64_t hi = ((int64_t)1 << (db - 1)) - 1, lo = -hi - 1;

            v = (uint64_t)(x > hi ? hi : x < lo ? lo : x);
            break;
        }
        case EVEX_PERM_PMOVUSAT: {
            uint64_t x = evex_get_elem(b, esz, j), hi = (1ull << (8 << aux)) - 1;

            v = x > hi ? hi : x;
            break;
        }
#endif /* __Use_Original_Qemu (U212) */
#if __Use_Original_Qemu != 1 /* ours (U214) */
        case EVEX_PERM_DUP_EVEN:    /* VMOVSLDUP, VMOVDDUP: SRC[j AND NOT 1] */
            v = evex_get_elem(b, esz, j & ~1);
            break;
        case EVEX_PERM_DUP_ODD:     /* VMOVSHDUP: SRC[j OR 1] */
            v = evex_get_elem(b, esz, j | 1);
            break;
#endif /* __Use_Original_Qemu (U214) */
#if __Use_Original_Qemu != 1 /* ours (U215) */
        case EVEX_PERM_INSERT:      /* TMP := SRC1; TMP[chunk imm8] := SRC2[chunk-1:0] */
            if (j >= csel * cel && j < (csel + 1) * cel) {
                v = evex_get_elem(b, esz, j - csel * cel);
            } else {
                v = evex_get_elem(a, esz, j);
            }
            break;
        case EVEX_PERM_EXTRACT:     /* SRC2[chunk imm8] */
            v = evex_get_elem(b, esz, csel * cel + j);
            break;
        case EVEX_PERM_BCAST:       /* SRC2[j modulo group] */
            v = evex_get_elem(b, esz, j % cel);
            break;
#endif /* __Use_Original_Qemu (U215) */
        default:
            g_assert_not_reached();
        }
        evex_set_elem(&r, oesz, j, v);
    }
    memcpy(d, &r, obytes);
}
#endif /* __Use_Original_Qemu (U211) */
#if __Use_Original_Qemu != 1 /* ours (U213) */

/*
 * NoVmp (ledger U213): VPEXPANDD/Q, VEXPANDPS/PD (SDM Vol2C): "k := 0; FOR j: IF k1[j] OR
 * *no writemask* THEN DEST[j] := SRC[k]; k := k + 1 ELSE merging / zeroing". s holds the
 * source elements contiguously (the register, or the popcount(k1) elements read from
 * memory); desc = EVEX_DESC(esz, KL) | EVEX_DESC_Z. Only the KL elements of d are written.
 */
void helper_evex_expand(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint64_t mask, uint32_t desc)
{
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), i, k = 0;
    ZMMReg src = *s, r = *d;

    for (i = 0; i < n; i++) {
        if (mask & (1ull << i)) {
            evex_set_elem(&r, esz, i, evex_get_elem(&src, esz, k++));
        } else if (desc & EVEX_DESC_Z) {
            evex_set_elem(&r, esz, i, 0);
        }
    }
    memcpy(d, &r, n << esz);
}

/*
 * NoVmp (ledger U213): VPCOMPRESSD/Q, VCOMPRESSPS/PD (SDM Vol2C): "k := 0; FOR j: IF k1[j]
 * OR *no writemask* THEN DEST[k] := SRC[j]; k := k + 1; IF *merging-masking* THEN
 * *DEST[VL-1:k] remains unchanged* ELSE DEST[VL-1:k] := 0". The memory form packs into
 * the scratch register (zeroing) and stores only the first k elements (gen_evex_cx).
 */
void helper_evex_compress(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint64_t mask, uint32_t desc)
{
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), i, k = 0;
    ZMMReg src = *s, r = *d;

    for (i = 0; i < n; i++) {
        if (mask & (1ull << i)) {
            evex_set_elem(&r, esz, k++, evex_get_elem(&src, esz, i));
        }
    }
    if (desc & EVEX_DESC_Z) {
        for (; k < n; k++) {
            evex_set_elem(&r, esz, k, 0);
        }
    }
    memcpy(d, &r, n << esz);
}
#endif /* __Use_Original_Qemu (U213) */
#if __Use_Original_Qemu != 1 /* ours (U263) */

/*
 * NoVmp (ledger U263): VDBPSADBW (SDM Vol2C): per 128-bit lane TMP1 dword i = SRC2 dword
 * imm8[2i+1:2i] of the lane; per 64-bit block of TMP1 four word SADs between unsigned
 * bytes: SRC1 bytes 0-3 with TMP1 bytes 0-3 and 1-4, SRC1 bytes 4-7 with TMP1 bytes 2-5 and
 * 3-6 (byte offsets within the block). desc = imm8 | vector length in bytes << 8.
 */
void helper_evex_dbpsadbw(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, uint32_t desc)
{
    int imm = desc & 0xff, vl = desc >> 8, ln, blk, w, i;
    ZMMReg r;

    for (ln = 0; ln < vl / 16; ln++) {
        uint8_t t[16];

        for (i = 0; i < 16; i++) {
            t[i] = b->ZMM_B(ln * 16 + 4 * ((imm >> (2 * (i / 4))) & 3) + (i & 3));
        }
        for (blk = 0; blk < 2; blk++) {
            for (w = 0; w < 4; w++) {
                int so = ln * 16 + blk * 8 + (w < 2 ? 0 : 4), to = blk * 8 + w;
                int sum = 0;

                for (i = 0; i < 4; i++) {
                    int dlt = (int)a->ZMM_B(so + i) - (int)t[to + i];

                    sum += dlt < 0 ? -dlt : dlt;
                }
                r.ZMM_W(ln * 8 + blk * 4 + w) = sum;
            }
        }
    }
    for (i = 0; i < vl / 2; i++) {
        d->ZMM_W(i) = r.ZMM_W(i);
    }
}
#endif /* __Use_Original_Qemu (U263) */
#if __Use_Original_Qemu != 1 /* ours (U265) */

/*
 * NoVmp (ledger U265): VPSLLVW / VPSRLVW / VPSRAVW (SDM Vol2C): each word of a shifted by
 * the unsigned word count in the same position of b; a count above 15 gives 0 (logical)
 * or the sign (arithmetic). desc = kind (0 left, 1 logical right, 2 arithmetic right) |
 * vector length in bytes << 8. d may be a or b.
 */
void helper_evex_pshiftvw(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, uint32_t desc)
{
    int kind = desc & 0xff, n = (desc >> 8) / 2, i;

    for (i = 0; i < n; i++) {
        uint16_t x = a->ZMM_W(i), c = b->ZMM_W(i);

        switch (kind) {
        case 0:
            d->ZMM_W(i) = c < 16 ? (uint16_t)(x << c) : 0;
            break;
        case 1:
            d->ZMM_W(i) = c < 16 ? x >> c : 0;
            break;
        default:
            d->ZMM_W(i) = (uint16_t)((int16_t)x >> (c < 16 ? c : 15));
            break;
        }
    }
}
#endif /* __Use_Original_Qemu (U265) */
#if __Use_Original_Qemu != 1 /* ours (U266) */

/*
 * NoVmp (ledger U266): VPMOVM2B / VPMOVM2W: element i = all ones if k[i] is set, else 0
 * (SDM Vol2C VPMOVM2B/VPMOVM2W/VPMOVM2D/VPMOVM2Q). desc = EVEX_DESC(esz, n).
 */
void helper_evex_movm2v(CPUX86State *env, ZMMReg *d, uint64_t k, uint32_t desc)
{
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), i;

    for (i = 0; i < n; i++) {
        evex_set_elem(d, esz, i, ((k >> i) & 1) ? ~0ull : 0);
    }
}
#endif /* __Use_Original_Qemu (U266) */
#if __Use_Original_Qemu != 1 /* ours (U268) */

/*
 * NoVmp (ledger U268): VPMOVWB / VPMOVSWB / VPMOVUSWB (SDM Vol2C): n words of s to n bytes
 * of d: truncation (kind 0), signed saturation (1: SaturateSignedWordToByte) or unsigned
 * saturation of the unsigned word (2: SaturateUnsignedWordToByte). desc = kind | n << 8.
 */
void helper_evex_pmovwb(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint32_t desc)
{
    int kind = desc & 0xff, n = desc >> 8, i;
    uint8_t r[32];

    for (i = 0; i < n; i++) {
        uint16_t x = s->ZMM_W(i);

        switch (kind) {
        case 0:
            r[i] = (uint8_t)x;
            break;
        case 1:
            r[i] = (int16_t)x > 127 ? 0x7f : (int16_t)x < -128 ? 0x80 : (uint8_t)x;
            break;
        default:
            r[i] = x > 255 ? 0xff : (uint8_t)x;
            break;
        }
    }
    for (i = 0; i < n; i++) {
        d->ZMM_B(i) = r[i];
    }
}
#endif /* __Use_Original_Qemu (U268) */
#if __Use_Original_Qemu != 1 /* ours (U269) */

/*
 * NoVmp (ledger U269): word permutes (SDM Vol2C VPERMW, VPERMI2W, VPERMT2W). n = VL / 2
 * words; mode 0 (VPERMW): d[j] = ta[idx[j] mod n]; mode 1 (two tables): d[j] = (idx[j]
 * bit log2(n) ? tb : ta)[idx[j] mod n]. Index bits above are ignored. desc = VL in bytes |
 * mode << 8. d may be any of the sources.
 */
void helper_evex_vpermw(CPUX86State *env, ZMMReg *d, ZMMReg *idx, ZMMReg *ta, ZMMReg *tb,
                        uint32_t desc)
{
    int n = (desc & 0xff) / 2, mode = (desc >> 8) & 1, j;
    ZMMReg r;

    for (j = 0; j < n; j++) {
        int x = idx->ZMM_W(j);
        ZMMReg *t = (mode && (x & n)) ? tb : ta;

        r.ZMM_W(j) = t->ZMM_W(x & (n - 1));
    }
    for (j = 0; j < n; j++) {
        d->ZMM_W(j) = r.ZMM_W(j);
    }
}
#endif /* __Use_Original_Qemu (U269) */
#if __Use_Original_Qemu != 1 /* ours (U292) */

/*
 * NoVmp (ledger U292): AVX512DQ VFPCLASSPS/PD/SS/SD (SDM Vol2C, CheckFPClassSP/DP): bit j of
 * the result = OR of the imm8-selected categories of element j - imm8[0] QNaN, [1] +0,
 * [2] -0, [3] +INF, [4] -INF, [5] denormal, [6] finite negative, [7] SNaN. With MXCSR.DAZ a
 * denormal counts as a zero ("IF (ExpAllZeros AND MXCSR.DAZ) THEN MantAllZeros := 1").
 * desc = element size log2 | count << 8 | imm8 << 16; bits count..63 are 0. No MXCSR flag.
 */
#define EVEX_DQ_IMM(d)     (((d) >> 16) & 0xff)
#define EVEX_DQ_SCALAR     (1u << 24)

static bool evex_fpclass1(uint64_t x, bool dbl, int imm, bool daz)
{
    int fb = dbl ? 52 : 23, eb = dbl ? 11 : 8;
    uint64_t emax = (1ull << eb) - 1;
    bool neg = (x >> (fb + eb)) & 1;
    uint64_t e = (x >> fb) & emax, m = x & ((1ull << fb) - 1);
    bool ones = e == emax, zeros = e == 0;
    bool mzero = (zeros && daz) || m == 0;
    bool zero = zeros && mzero;
    bool sig = (m >> (fb - 1)) & 1;

    return ((imm & 0x01) && ones && !mzero && sig) ||      /* QNaN */
           ((imm & 0x02) && !neg && zero) ||               /* +0 */
           ((imm & 0x04) && neg && zero) ||                /* -0 */
           ((imm & 0x08) && !neg && ones && mzero) ||      /* +INF */
           ((imm & 0x10) && neg && ones && mzero) ||       /* -INF */
           ((imm & 0x20) && zeros && !mzero) ||            /* denormal */
           ((imm & 0x40) && neg && !ones && !zero) ||      /* finite negative */
           ((imm & 0x80) && ones && !mzero && !sig);       /* SNaN */
}

uint64_t helper_evex_fpclass(CPUX86State *env, ZMMReg *s, uint32_t desc)
{
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), imm = EVEX_DQ_IMM(desc), i;
    bool daz = (env->mxcsr & SSE_DAZ) != 0;
    uint64_t r = 0;

    for (i = 0; i < n; i++) {
        if (evex_fpclass1(evex_get_elem(s, esz, i), esz == MO_64, imm, daz)) {
            r |= 1ull << i;
        }
    }
    return r;
}
#endif /* __Use_Original_Qemu (U292) */
#if __Use_Original_Qemu != 1 /* ours (U293) */

/*
 * NoVmp (ledger U293): VRANGEPS/PD (U295: SS/SD), SDM Vol2C RangeSP/RangeDP: an SNaN
 * operand (SRC1 first) returns that NaN quietened with IE; a denormal is a signed zero
 * under DAZ, else DE unless the other operand is a QNaN; a QNaN SRC2 returns SRC1, a QNaN
 * SRC1 returns SRC2; opposite-signed zeros give -0 (MIN, MIN_ABS) / +0 (MAX, MAX_ABS)
 * (Table 5-22); equal magnitudes of opposite sign give the negative (MIN_ABS) / positive
 * (MAX_ABS) one (Table 5-23); else imm8[1:0] 00 MIN, 01 MAX, 10 MIN_ABS, 11 MAX_ABS with
 * "SRC1 <= SRC2 ? ..." as written; then imm8[3:2] sign: 00 SRC1's, 01 the compare
 * result's, 10 cleared, 11 set. imm8[7:4] are not used. Flags go to sse_status.
 */
static uint64_t evex_range1(CPUX86State *env, uint64_t a, uint64_t b, bool dbl, int imm,
                            bool daz)
{
    int fb = dbl ? 52 : 23, eb = dbl ? 11 : 8, op = imm & 3;
    uint64_t sbit = 1ull << (fb + eb), mag = sbit - 1, qbit = 1ull << (fb - 1);
    uint64_t emax = ((1ull << eb) - 1) << fb, fmask = (1ull << fb) - 1;
    bool a_nan = (a & emax) == emax && (a & fmask), b_nan = (b & emax) == emax && (b & fmask);
    bool a_q = a_nan && (a & qbit), b_q = b_nan && (b & qbit);
    bool as = (a & sbit) != 0, bs = (b & sbit) != 0;
    uint64_t t;

    if (a_nan && !a_q) {
        float_raise(float_flag_invalid, &env->sse_status);
        return a | qbit;
    }
    if (b_nan && !b_q) {
        float_raise(float_flag_invalid, &env->sse_status);
        return b | qbit;
    }
    if (!(a & emax) && (a & fmask)) {
        if (daz) {
            a &= sbit;
        } else if (!b_q) {
            float_raise(float_flag_input_denormal_used, &env->sse_status);
        }
    }
    if (!(b & emax) && (b & fmask)) {
        if (daz) {
            b &= sbit;
        } else if (!a_q) {
            float_raise(float_flag_input_denormal_used, &env->sse_status);
        }
    }
    if (b_q) {
        t = a;
    } else if (a_q) {
        t = b;
    } else if (!(a & mag) && !(b & mag) && as != bs) {
        t = (op & 1) ? 0 : sbit;                            /* Table 5-22 */
    } else if ((a & mag) == (b & mag) && as != bs && op > 1) {
        t = (op == 2) == as ? a : b;                        /* Table 5-23 */
    } else if (op < 2) {
        /* SRC1 <= SRC2 (no NaN; opposite-signed zeros handled above) */
        bool le = as != bs ? as : as ? (a & mag) >= (b & mag) : (a & mag) <= (b & mag);

        t = (op == 0) == le ? a : b;
    } else {
        bool le = (a & mag) <= (b & mag);

        t = (op == 2) == le ? a : b;
    }
    switch ((imm >> 2) & 3) {
    case 0:
        return (t & mag) | (as ? sbit : 0);
    case 1:
        return t;
    case 2:
        return t & mag;
    default:
        return t | sbit;
    }
}

/* d: result (scratch), a/b: SRC1/SRC2 (masking copies), u: the SRC1 register (scalar) */
void helper_evex_range(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, ZMMReg *u,
                       uint32_t desc)
{
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), imm = EVEX_DQ_IMM(desc), i;
    bool daz = (env->mxcsr & SSE_DAZ) != 0;
    ZMMReg r;

    if (desc & EVEX_DQ_SCALAR) {
        r = *u;                                 /* DEST[127:esz] := SRC1[127:esz] (U295) */
        n = 1;
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(&r, esz, i, evex_range1(env, evex_get_elem(a, esz, i),
                                              evex_get_elem(b, esz, i), esz == MO_64, imm, daz));
    }
    if (desc & EVEX_DQ_SCALAR) {
        memcpy(d, &r, 16);
    } else {
        memcpy(d, &r, (size_t)n << esz);
    }
}
#endif /* __Use_Original_Qemu (U293) */
#if __Use_Original_Qemu != 1 /* ours (U294) */

/*
 * NoVmp (ledger U294): VREDUCEPS/PD (U296: SS/SD), SDM Vol2C ReduceArgumentSP/DP:
 * DEST = SRC - 2^-M * ROUND(2^M * SRC) with M = imm8[7:4], ROUND to an integer under
 * imm8[1:0] (00 RNE, 01 RD, 10 RU, 11 RZ) or MXCSR.RC when imm8[2] = 1, the subtraction
 * rounded under the same control; PE from the inexact ROUND unless imm8[3] (SPE) = 1. NaN:
 * quietened, IE for an SNaN; +-INF: +0.0; a zero result: +0.0, -0.0 when rounding down
 * (Table 5-27). Exact integer arithmetic: SRC = mant * 2^ex, 2^M * SRC = q + r / 2^shift.
 * The pseudocode has no DAZ/FTZ/DE/UE step (exceptions: Invalid, Precision only).
 */
static uint64_t evex_fp_pack(int sign, uint64_t c, int ex, int fb, int eb)
{
    int bias = (1 << (eb - 1)) - 1, len = 64 - clz64(c);
    int e = ex + (len - 1) + bias;
    uint64_t sb = (uint64_t)sign << (fb + eb);

    if (e >= 1) {
        c = len <= fb + 1 ? c << (fb + 1 - len) : c >> (len - fb - 1);
        return sb | ((uint64_t)e << fb) | (c & ((1ull << fb) - 1));
    }
    return sb | (c << (ex - (1 - bias - fb)));      /* denormal, exact */
}

static uint64_t evex_reduce1(CPUX86State *env, uint64_t x, bool dbl, int imm)
{
    int fb = dbl ? 52 : 23, eb = dbl ? 11 : 8, p = fb + 1;
    int bias = (1 << (eb - 1)) - 1;
    uint64_t sbit = 1ull << (fb + eb), emaxv = (1ull << eb) - 1, qbit = 1ull << (fb - 1);
    int sign = (x >> (fb + eb)) & 1;
    uint64_t e = (x >> fb) & emaxv, m = x & ((1ull << fb) - 1);
    int rc = (imm & 4) ? (int)((env->mxcsr >> 13) & 3) : (imm & 3);
    int M = (imm >> 4) & 15, ex, shift, k, cmp = 0, rsign;
    uint64_t zero = rc == 1 ? sbit : 0, mant, q, r, rh, rl, kept;
    bool inc, up = false;

    if (e == emaxv) {
        if (m == 0) {
            return 0;                           /* +-INF -> +0.0 */
        }
        if (!(m & qbit)) {
            float_raise(float_flag_invalid, &env->sse_status);
        }
        return x | qbit;
    }
    if (e == 0 && m == 0) {
        return zero;
    }
    mant = e ? m | (1ull << fb) : m;
    ex = (e ? (int)e : 1) - bias - fb;          /* SRC = mant * 2^ex */
    shift = -(ex + M);                          /* fraction bits of 2^M * SRC */
    if (shift <= 0) {
        return zero;                            /* integral: SRC - SRC */
    }
    if (shift < 64) {
        q = mant >> shift;
        r = mant & ((1ull << shift) - 1);
    } else {
        q = 0;
        r = mant;
    }
    if (r == 0) {
        return zero;
    }
    if (!(imm & 8)) {
        float_raise(float_flag_inexact, &env->sse_status);
    }
    switch (rc) {
    case 0:
        inc = shift <= 64 && (r > (1ull << (shift - 1)) ||
                              (r == (1ull << (shift - 1)) && (q & 1)));
        break;
    case 1:
        inc = sign;
        break;
    case 2:
        inc = !sign;
        break;
    default:
        inc = false;
        break;
    }
    if (!inc) {
        return evex_fp_pack(sign, r, ex, fb, eb);   /* SRC - trunc: r * 2^ex, exact */
    }
    /* SRC - TMP = -(2^shift - r) * 2^ex (sign of SRC), rounded to p bits under rc */
    rsign = !sign;
    if (shift <= p) {
        return evex_fp_pack(rsign, (1ull << shift) - r, ex, fb, eb);
    }
    k = shift - p;                              /* low bits dropped */
    rh = k < 64 ? r >> k : 0;
    rl = k < 64 ? r & ((1ull << k) - 1) : r;
    if (rl == 0) {
        kept = (1ull << p) - rh;
    } else {
        kept = (1ull << p) - rh - 1;            /* remainder 2^k - rl */
        if (k > 64) {
            cmp = 1;                            /* rl < 2^p < 2^(k-1): above half */
        } else {
            uint64_t half = 1ull << (k - 1);
            cmp = rl < half ? 1 : rl == half ? 0 : -1;
        }
        switch (rc) {
        case 0:
            up = cmp > 0 || (cmp == 0 && (kept & 1));
            break;
        case 1:
            up = rsign;
            break;
        case 2:
            up = !rsign;
            break;
        default:
            break;
        }
    }
    if (up && ++kept == (1ull << p)) {
        kept >>= 1;
        k++;
    }
    return evex_fp_pack(rsign, kept, ex + k, fb, eb);
}

/* d: result (scratch), s: source (masking copy), u: the SRC1 register (scalar) */
void helper_evex_reduce(CPUX86State *env, ZMMReg *d, ZMMReg *s, ZMMReg *u, uint32_t desc)
{
    int esz = EVEX_DESC_ESZ(desc), n = EVEX_DESC_N(desc), imm = EVEX_DQ_IMM(desc), i;
    ZMMReg r;

    if (desc & EVEX_DQ_SCALAR) {
        r = *u;                                 /* DEST[127:esz] := SRC1[127:esz] (U296) */
        n = 1;
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(&r, esz, i, evex_reduce1(env, evex_get_elem(s, esz, i), esz == MO_64, imm));
    }
    if (desc & EVEX_DQ_SCALAR) {
        memcpy(d, &r, 16);
    } else {
        memcpy(d, &r, (size_t)n << esz);
    }
}
#endif /* __Use_Original_Qemu (U294) */
#if __Use_Original_Qemu != 1 /* ours (U231) */

/*
 * NoVmp (ledger U231): EVEX conversions between integer and floating-point elements
 * (SDM Vol2A CVTDQ2PS, CVTPS2DQ, CVTTPS2DQ, CVTPD2DQ, CVTTPD2DQ, CVTDQ2PD, CVTPS2PD,
 * CVTPD2PS; Vol2C VCVTUDQ2PS, VCVTPS2UDQ, VCVTTPS2UDQ, VCVTPD2UDQ, VCVTTPD2UDQ,
 * VCVTUDQ2PD, VCVTPH2PS, VCVTPS2PH, and the AVX512DQ QQ forms of U233).
 * - Floating point -> integer: rounding by MXCSR.RC / {er}, or truncation (VCVTT*); a NaN,
 *   an infinity or a value out of range is invalid (IE) and returns the integer indefinite
 *   value (signed: the most negative integer; unsigned: 2^w - 1); IE replaces PE; a
 *   negative value that rounds to 0 is 0 for the unsigned forms (PE only). DAZ: a
 *   denormal source is 0 (exact); no DE (not in the forms' exception lists).
 * - Integer -> floating point: rounded by MXCSR.RC / {er} (PE).
 * - VCVTPS2PD/VCVTPD2PS: as the SSE forms (IE on SNaN, DE, OE/UE/PE, DAZ, FTZ).
 * - VCVTPH2PS: exact, SNaN quietened (IE); DAZ ignored, no DE (SDM VCVTPH2PS: "MXCSR.DAZ
 *   is ignored ... No denormal exception is reported", as U43 for the VEX form).
 * - VCVTPS2PH: rounding from imm8[1:0] unless imm8[2] = 1 (MXCSR.RC); MXCSR.FTZ is ignored
 *   (tiny results are converted to denormals); a denormal source sets DE (and the tiny
 *   inexact result UE/PE), as the SDM describes.
 * desc: bits 2:0 source type, 6:4 destination type (EVCVT_*), bit 8 truncation, bits
 * 15:9 element count, bits 23:16 imm8, bit 24 imm8 present (VCVTPS2PH).
 */
enum { EVCVT_I32, EVCVT_U32, EVCVT_I64, EVCVT_U64, EVCVT_F16, EVCVT_F32, EVCVT_F64 };

static const uint8_t evcvt_esz[] = { MO_32, MO_32, MO_64, MO_64, MO_16, MO_32, MO_64 };

/* floating point (f32/f64 in x) to a 32/64-bit signed/unsigned integer */
static uint64_t evex_cvt_f2i_one(CPUX86State *env, uint64_t x, int st, int dt, bool trunc)
{
    float_status *fs = &env->sse_status;
    int old = get_float_exception_flags(fs);
    uint64_t r, indef;

    set_float_exception_flags(0, fs);
    switch (dt) {
    case EVCVT_I32:
        indef = 0x80000000u;
        if (st == EVCVT_F32) {
            r = (uint32_t)(trunc ? float32_to_int32_round_to_zero(x, fs) : float32_to_int32(x, fs));
        } else {
            r = (uint32_t)(trunc ? float64_to_int32_round_to_zero(x, fs) : float64_to_int32(x, fs));
        }
        break;
    case EVCVT_U32:
        indef = 0xffffffffu;
        if (st == EVCVT_F32) {
            r = trunc ? float32_to_uint32_round_to_zero(x, fs) : float32_to_uint32(x, fs);
        } else {
            r = trunc ? float64_to_uint32_round_to_zero(x, fs) : float64_to_uint32(x, fs);
        }
        break;
    case EVCVT_I64:
        indef = 0x8000000000000000ull;
        if (st == EVCVT_F32) {
            r = trunc ? float32_to_int64_round_to_zero(x, fs) : float32_to_int64(x, fs);
        } else {
            r = trunc ? float64_to_int64_round_to_zero(x, fs) : float64_to_int64(x, fs);
        }
        break;
    default:
        indef = ~0ull;
        if (st == EVCVT_F32) {
            r = trunc ? float32_to_uint64_round_to_zero(x, fs) : float32_to_uint64(x, fs);
        } else {
            r = trunc ? float64_to_uint64_round_to_zero(x, fs) : float64_to_uint64(x, fs);
        }
        break;
    }
    if (get_float_exception_flags(fs) & float_flag_invalid) {
        r = indef;
    }
    set_float_exception_flags(old | get_float_exception_flags(fs), fs);
    return r;
}

/* integer (i32/u32/i64/u64 in x) to f32/f64 */
static uint64_t evex_cvt_i2f_one(CPUX86State *env, uint64_t x, int st, int dt)
{
    float_status *fs = &env->sse_status;

    if (dt == EVCVT_F32) {
        switch (st) {
        case EVCVT_I32:
            return int32_to_float32((int32_t)x, fs);
        case EVCVT_U32:
            return uint32_to_float32((uint32_t)x, fs);
        case EVCVT_I64:
            return int64_to_float32((int64_t)x, fs);
        default:
            return uint64_to_float32(x, fs);
        }
    }
    switch (st) {
    case EVCVT_I32:
        return int32_to_float64((int32_t)x, fs);
    case EVCVT_U32:
        return uint32_to_float64((uint32_t)x, fs);
    case EVCVT_I64:
        return int64_to_float64((int64_t)x, fs);
    default:
        return uint64_to_float64(x, fs);
    }
}

/* one element of any EVEX conversion; imm8 >= 0: VCVTPS2PH rounding control */
static uint64_t evex_cvt_one(CPUX86State *env, uint64_t x, int st, int dt, bool trunc, int imm)
{
    float_status *fs = &env->sse_status;

    if (st <= EVCVT_U64) {
        return evex_cvt_i2f_one(env, x, st, dt);
    }
    if (dt <= EVCVT_U64) {
        return evex_cvt_f2i_one(env, x, st, dt, trunc);
    }
    if (st == EVCVT_F32 && dt == EVCVT_F64) {
        return float32_to_float64(x, fs);
    }
    if (st == EVCVT_F64 && dt == EVCVT_F32) {
        return float64_to_float32(x, fs);
    }
    if (st == EVCVT_F16) {
        float_status st16 = *fs;
        uint32_t r;

        set_flush_inputs_to_zero(false, &st16);
        set_float_exception_flags(0, &st16);
        r = float16_to_float32(x, true, &st16);
        float_raise(get_float_exception_flags(&st16) &
                    ~(float_flag_input_denormal | float_flag_input_denormal_used), fs);
        return r;
    } else {
        float_status st16 = *fs;
        uint16_t r;

        set_flush_to_zero(false, &st16);
        if (imm >= 0 && !(imm & 4)) {
            set_x86_rounding_mode(imm & 3, &st16);
        }
        set_float_exception_flags(0, &st16);
        r = float32_to_float16(x, true, &st16);
        float_raise(get_float_exception_flags(&st16), fs);
        return r;
    }
}

void helper_evex_cvt(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint32_t desc)
{
    int st = desc & 7, dt = (desc >> 4) & 7, n = (desc >> 9) & 0x7f, i;
    int imm = (desc & (1u << 24)) ? (int)((desc >> 16) & 0xff) : -1;
    bool trunc = (desc >> 8) & 1;
    ZMMReg r;

    memset(&r, 0, sizeof(r));
    for (i = 0; i < n; i++) {
        evex_set_elem(&r, evcvt_esz[dt], i,
                      evex_cvt_one(env, evex_get_elem(s, evcvt_esz[st], i), st, dt, trunc, imm));
    }
    *d = r;
}
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U232) */

/*
 * NoVmp (ledger U232): scalar EVEX conversions (same element rules as U231). v is the
 * SRC1 register (vvvv) itself, so bits 127:element are its own also when the engine
 * substituted neutral lanes into a copy for masking.
 * - VCVTSS2SD / VCVTSD2SS: DEST[elem] := convert(SRC2[elem 0]), DEST[127:elem] := SRC1.
 * - VCVT[U]SI2SS/SD: the same with an integer (r/m32, or r/m64 with EVEX.W1 in 64-bit mode).
 * - VCVT[T]SS2[U]SI / VCVT[T]SD2[U]SI: 32/64-bit integer result for the GPR destination.
 */
void helper_evex_cvt_s(CPUX86State *env, ZMMReg *d, ZMMReg *v, ZMMReg *s, uint32_t desc)
{
    int st = desc & 7, dt = (desc >> 4) & 7;
    ZMMReg r;

    r.ZMM_Q(0) = v->ZMM_Q(0);
    r.ZMM_Q(1) = v->ZMM_Q(1);
    evex_set_elem(&r, evcvt_esz[dt], 0,
                  evex_cvt_one(env, evex_get_elem(s, evcvt_esz[st], 0), st, dt, false, -1));
    d->ZMM_Q(0) = r.ZMM_Q(0);
    d->ZMM_Q(1) = r.ZMM_Q(1);
}

void helper_evex_cvt_i2f(CPUX86State *env, ZMMReg *d, ZMMReg *v, uint64_t val, uint32_t desc)
{
    int st = desc & 7, dt = (desc >> 4) & 7;
    uint64_t q0 = v->ZMM_Q(0), q1 = v->ZMM_Q(1);
    uint64_t f = evex_cvt_i2f_one(env, val, st, dt);

    d->ZMM_Q(0) = q0;
    d->ZMM_Q(1) = q1;
    evex_set_elem(d, evcvt_esz[dt], 0, f);
}

uint64_t helper_evex_cvt_f2i(CPUX86State *env, ZMMReg *s, uint32_t desc)
{
    int st = desc & 7, dt = (desc >> 4) & 7;

    return evex_cvt_f2i_one(env, evex_get_elem(s, evcvt_esz[st], 0), st, dt, (desc >> 8) & 1);
}
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U234) */

/*
 * NoVmp (ledger U234): VPSLLD/Q, VPSRLD/Q, VPSRAD/Q (VPSRAQ) by xmm3/m128 (SDM Vol2B PSLLW/
 * PSLLD/PSLLQ, PSRLW/PSRLD/PSRLQ, PSRAW/PSRAD/PSRAQ "LOGICAL_LEFT_SHIFT_DWORDS ... (SRC2)"):
 * the count is the unsigned 64-bit COUNT = SRC2[63:0] for every element; COUNT > width - 1
 * gives 0 (logical) or the sign in every bit (arithmetic). desc: bits 1:0 kind (0 left,
 * 1 logical right, 2 arithmetic right), bits 7:4 element size (MO_32/MO_64), 15:8 count.
 */
void helper_evex_pshift(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *c, uint32_t desc)
{
    int kind = desc & 3, esz = (desc >> 4) & 0xf, n = (desc >> 8) & 0xff, bits = 8 << esz, i;
    uint64_t cnt = c->ZMM_Q(0);
    ZMMReg r;

    for (i = 0; i < n; i++) {
        uint64_t x = evex_get_elem(a, esz, i), y;

        if (kind == 2) {
            int64_t sx = (int64_t)(x << (64 - bits)) >> (64 - bits);

            y = (uint64_t)(sx >> (cnt > (uint64_t)bits - 1 ? bits - 1 : (int)cnt));
        } else if (cnt > (uint64_t)bits - 1) {
            y = 0;
        } else {
            y = kind == 0 ? x << cnt : x >> cnt;
        }
        evex_set_elem(&r, esz, i, y);
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(d, esz, i, evex_get_elem(&r, esz, i));
    }
}
#endif /* __Use_Original_Qemu (U234) */
#if __Use_Original_Qemu != 1 /* ours (U235) */

/*
 * NoVmp (ledger U235): VPROLVD/Q, VPRORVD/Q (SDM Vol2C PROLD/PROLVD/PROLQ/PROLVQ, PRORD/
 * PRORVD/PRORQ/PRORVQ): each element rotated by its own count modulo the width.
 * desc: bit 0 right, bits 7:4 element size, 15:8 count of elements.
 */
void helper_evex_prolv(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, uint32_t desc)
{
    int right = desc & 1, esz = (desc >> 4) & 0xf, n = (desc >> 8) & 0xff, bits = 8 << esz, i;
    uint64_t m = bits == 64 ? ~0ull : (1ull << bits) - 1;
    ZMMReg r;

    for (i = 0; i < n; i++) {
        uint64_t x = evex_get_elem(a, esz, i);
        int c = (int)(evex_get_elem(b, esz, i) & (bits - 1));

        if (right && c) {
            c = bits - c;
        }
        evex_set_elem(&r, esz, i, c ? ((x << c) | (x >> (bits - c))) & m : x);
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(d, esz, i, evex_get_elem(&r, esz, i));
    }
}
#endif /* __Use_Original_Qemu (U235) */
#if __Use_Original_Qemu != 1 /* ours (U236) */

/*
 * NoVmp (ledger U236..U241): AVX-512 floating-point element operations on binary32 /
 * binary64 bit patterns (esz MO_32 / MO_64). EVFMT describes the format.
 */
typedef struct EvFmt {
    int bits, fbits, bias;
    uint64_t sign, emask, fmask, quiet, one, indef, maxf;
} EvFmt;

static const EvFmt evfmt32 = { 32, 23, 127, 0x80000000u, 0x7f800000u, 0x007fffffu, 0x00400000u,
                               0x3f800000u, 0xffc00000u, 0x7f7fffffu };
static const EvFmt evfmt64 = { 64, 52, 1023, 0x8000000000000000ull, 0x7ff0000000000000ull,
                               0x000fffffffffffffull, 0x0008000000000000ull,
                               0x3ff0000000000000ull, 0xfff8000000000000ull,
                               0x7fefffffffffffffull };

static inline const EvFmt *evfmt(int esz)
{
    return esz == MO_64 ? &evfmt64 : &evfmt32;
}

static inline bool evf_isnan(const EvFmt *f, uint64_t x)
{
    return (x & f->emask) == f->emask && (x & f->fmask);
}

static inline bool evf_issnan(const EvFmt *f, uint64_t x)
{
    return evf_isnan(f, x) && !(x & f->quiet);
}

static inline bool evf_isinf(const EvFmt *f, uint64_t x)
{
    return (x & ~f->sign) == f->emask;
}

/* exponent field 0: zero or denormal */
static inline bool evf_expzero(const EvFmt *f, uint64_t x)
{
    return !(x & f->emask);
}

static inline bool evf_isdenorm(const EvFmt *f, uint64_t x)
{
    return evf_expzero(f, x) && (x & f->fmask);
}

/* an integer value (|v| < 2^53) as binary32/64 (exact) */
static uint64_t evf_from_int(const EvFmt *f, int64_t v)
{
    uint64_t m = v < 0 ? (uint64_t)-v : (uint64_t)v, s = v < 0 ? f->sign : 0;
    int l;

    if (!m) {
        return s;
    }
    l = 63 - clz64(m);                  /* m = 1.xxx * 2^l */
    m <<= f->fbits - l;                 /* l <= fbits here */
    return s | ((uint64_t)(l + f->bias) << f->fbits) | (m & f->fmask);
}

/*
 * NoVmp (ledger U236): VRCP14PS/PD/SS/SD and VRSQRT14PS/PD/SS/SD (SDM Vol2C). The SDM gives
 * the error bound (relative error < 2^-14), the special cases (Tables 5-24..5-27) and the
 * DAZ/FTZ rules, but not the exact approximation (Intel's reference RECIP14.c is not part of
 * the SDM and not available here). Implemented stand-in (well inside the bound; NOT
 * necessarily the silicon's bits): the exact 1/x (1/sqrt(x)) rounded to nearest even to the
 * destination precision with an unbounded exponent; overflow -> inf; a tiny VRCP14 result is
 * then denormalized (nearest even at the denormal quantum: the SDM's "mantissa shifted right
 * by one or two bits") or, with FTZ, a zero of the source's sign. MXCSR.RC ignored, no MXCSR
 * flag, no #XM; DAZ: a denormal source is a zero of its sign; 0 -> inf (sign kept), inf -> 0
 * (sign kept), SNaN -> QNaN, QNaN -> itself; RSQRT14: -0 -> -inf, any other negative -> QNaN
 * indefinite. 1/x is formed in the next wider format (float64 / float128): the binary
 * expansion of 1/m (m < 2^p) has no run of p + 1 equal bits, so that rounding cannot meet a
 * p-bit midpoint and the result is the correctly rounded one.
 */

/*
 * round sig (normalised: bit 63 set; value = sig * 2^(e - 63), sticky = bits below) to the
 * format's precision (RNE, unbounded exponent), then overflow / denormalize / FTZ
 */
static uint64_t evf_round_rcp(const EvFmt *f, uint64_t sign, int e, uint64_t sig, bool sticky,
                              bool ftz)
{
    int sh = 63 - f->fbits;
    uint64_t r = sig >> sh, rem = sig & ((1ull << sh) - 1), half = 1ull << (sh - 1);

    if (rem > half || (rem == half && (sticky || (r & 1)))) {
        r++;
        if (r >> (f->fbits + 1)) {
            r >>= 1;
            e++;
        }
    }
    if (e > f->bias) {
        return sign | f->emask;
    }
    if (e < 1 - f->bias) {
        int s2 = 1 - f->bias - e;
        uint64_t r2, rem2, half2;

        if (ftz || s2 > f->fbits + 1) {
            return sign;                /* FTZ, or below half the smallest denormal */
        }
        r2 = r >> s2;
        rem2 = r & ((1ull << s2) - 1);
        half2 = 1ull << (s2 - 1);
        if (rem2 > half2 || (rem2 == half2 && (r2 & 1))) {
            r2++;                       /* 2^fbits: the smallest normal, same bits */
        }
        return sign | r2;
    }
    return sign | ((uint64_t)(e + f->bias) << f->fbits) | (r & f->fmask);
}

static uint64_t evex_rcp14(CPUX86State *env, const EvFmt *f, uint64_t x, bool rsqrt)
{
    float_status st = env->sse_status;
    uint64_t s = x & f->sign;

    set_float_rounding_mode(float_round_nearest_even, &st);
    set_float_exception_flags(0, &st);
    /* U236 x U445: no MXCSR flag, so no unmasked-#U/#O rounding (FTZ applies as masked) */
    st.unmasked_underflow = false;
    st.unmasked_overflow = false;
    if (evf_isnan(f, x)) {
        return x | f->quiet;
    }
    if (evf_expzero(f, x) && (!(x & f->fmask) || (env->mxcsr & 0x40))) {
        return s | f->emask;                                        /* +-0 -> +-inf */
    }
    if (evf_isinf(f, x)) {
        return rsqrt && s ? f->indef : s;                           /* +-inf -> +-0 */
    }
    if (rsqrt) {
        float128 q, one = int32_to_float128(1, &st);

        if (s) {
            return f->indef;
        }
        set_flush_inputs_to_zero(false, &st);
        q = f->bits == 64 ? float64_to_float128(x, &st) : float32_to_float128(x, &st);
        q = float128_div(one, float128_sqrt(q, &st), &st);
        return f->bits == 64 ? float128_to_float64(q, &st) : float128_to_float32(q, &st);
    }
    set_flush_inputs_to_zero(false, &st);
    set_flush_to_zero(false, &st);
    if (f->bits == 64) {
        float128 q = float128_div(int32_to_float128(1, &st), float64_to_float128(x, &st), &st);
        uint64_t sig = (1ull << 63) | ((q.high & 0xffffffffffffull) << 15) | (q.low >> 49);

        return evf_round_rcp(f, s, (int)((q.high >> 48) & 0x7fff) - 16383, sig,
                             (q.low & ((1ull << 49) - 1)) != 0, env->mxcsr & 0x8000);
    } else {
        uint64_t q = float64_div(float64_one, float32_to_float64(x, &st), &st);

        return evf_round_rcp(f, s, (int)((q >> 52) & 0x7ff) - 1023,
                             ((q & 0xfffffffffffffull) | (1ull << 52)) << 11, false,
                             env->mxcsr & 0x8000);
    }
}
#endif /* __Use_Original_Qemu (U236) */
#if __Use_Original_Qemu != 1 /* ours (U237) */

/*
 * NoVmp (ledger U237): VGETEXPPS/PD/SS/SD (SDM Vol2C, Table 5-13 and the pseudocode
 * ConvertExpDPFP / NormalizeExpTinyDPFP): floor(log2(|x|)) as a floating-point value
 * (exact); NaN -> QNaN(SRC) (IE for SNaN), +-inf -> +inf, +-0 (and a denormal with DAZ)
 * -> -inf; a denormal (DAZ = 0) gives its true exponent and sets DE. Exceptions: IE, DE.
 */
static uint64_t evex_getexp(CPUX86State *env, const EvFmt *f, uint64_t x)
{
    uint64_t fr = x & f->fmask;

    if (evf_isnan(f, x)) {
        if (!(x & f->quiet)) {
            float_raise(float_flag_invalid, &env->sse_status);
        }
        return x | f->quiet;
    }
    if (evf_isinf(f, x)) {
        return f->emask;
    }
    if (evf_expzero(f, x)) {
        if (!fr || (env->mxcsr & 0x40)) {
            return f->sign | f->emask;
        }
        float_raise(float_flag_input_denormal_used, &env->sse_status);
        /* fr = 1.xxx * 2^(63 - clz): value = fr * 2^(1 - bias - fbits) */
        return evf_from_int(f, (int64_t)(63 - clz64(fr)) + 1 - f->bias - f->fbits);
    }
    return evf_from_int(f, (int64_t)((x & f->emask) >> f->fbits) - f->bias);
}
#endif /* __Use_Original_Qemu (U237) */
#if __Use_Original_Qemu != 1 /* ours (U238) */

/*
 * NoVmp (ledger U238): VGETMANTPS/PD/SS/SD (SDM Vol2C pseudocode getmant_fp64, Table 5-16,
 * Figure 5-15): imm8[1:0] interval ([1,2), [1/2,2), [1/2,1), [3/4,3/2)), imm8[3:2] sign
 * control (SC[0]: positive result; SC[1]: a negative source gives QNaN indefinite and IE).
 * The result is exact (no PE). Exceptions: IE, DE.
 */
static uint64_t evex_getmant(CPUX86State *env, const EvFmt *f, uint64_t x, int imm)
{
    bool sc0 = imm & 4, sc1 = imm & 8, neg = x & f->sign;
    uint64_t one = f->one, fr = x & f->fmask, sign = sc0 ? 0 : (x & f->sign);
    uint64_t signed_one = sc0 ? one : (one | f->sign);
    int64_t uexp;
    int e;

    if (evf_isnan(f, x)) {
        if (!(x & f->quiet)) {
            float_raise(float_flag_invalid, &env->sse_status);
        }
        return x | f->quiet;
    }
    if (evf_expzero(f, x) && (!fr || (env->mxcsr & 0x40))) {        /* zero */
        return neg ? signed_one : one;
    }
    if (evf_isinf(f, x)) {
        if (!neg) {
            return one;
        }
        if (sc1) {
            float_raise(float_flag_invalid, &env->sse_status);
            return f->indef;
        }
        return signed_one;
    }
    if (neg && sc1) {
        float_raise(float_flag_invalid, &env->sse_status);
        return f->indef;
    }
    if (evf_expzero(f, x)) {                                        /* denormal, DAZ = 0 */
        int sh = f->fbits - (63 - clz64(fr));                       /* shifts to the J bit */

        fr = (fr << sh) & f->fmask;
        uexp = -sh;
        float_raise(float_flag_input_denormal_used, &env->sse_status);
    } else {
        uexp = (int64_t)((x & f->emask) >> f->fbits) - f->bias;
    }
    switch (imm & 3) {
    case 0:
        e = f->bias;
        break;
    case 1:
        e = (uexp & 1) ? f->bias - 1 : f->bias;
        break;
    case 2:
        e = f->bias - 1;
        break;
    default:
        e = (fr & (f->fmask ^ (f->fmask >> 1))) ? f->bias - 1 : f->bias;
        break;
    }
    return sign | ((uint64_t)e << f->fbits) | fr;
}
#endif /* __Use_Original_Qemu (U238) */
#if __Use_Original_Qemu != 1 /* ours (U241) */

/*
 * NoVmp (ledger U241): VRNDSCALEPS/PD/SS/SD (SDM Vol2C RoundToIntegerDP, Table 5-29, Figure
 * 5-29): 2^-M * round_to_int(2^M * x) with M = imm8[7:4], rounding imm8[1:0] or MXCSR.RC
 * (imm8[2] = 1); exact integer arithmetic (2^M * x never overflows). Sign kept (also of
 * zero); NaN -> QNaN (IE for SNaN), inf and 0 unchanged; DAZ: a denormal is a zero of its
 * sign first. PE when the result differs from the source unless imm8[3] = 1 (SPE). No DE.
 */
static uint64_t evex_rndscale(CPUX86State *env, const EvFmt *f, uint64_t x, int imm)
{
    uint64_t s = x & f->sign, ex = (x & f->emask) >> f->fbits, mant, q, rem, half;
    int rc = (imm & 4) ? (env->mxcsr >> 13) & 3 : imm & 3, m = (imm >> 4) & 15;
    int e2, sh, l;
    bool up;

    if (evf_isnan(f, x)) {
        if (!(x & f->quiet)) {
            float_raise(float_flag_invalid, &env->sse_status);
        }
        return x | f->quiet;
    }
    if (evf_isinf(f, x) || !(x & ~f->sign)) {
        return x;
    }
    if (!ex) {
        if (env->mxcsr & 0x40) {
            return s;
        }
        mant = x & f->fmask;
        e2 = 1 - f->bias - f->fbits;
    } else {
        mant = (x & f->fmask) | (f->fmask + 1);
        e2 = (int)ex - f->bias - f->fbits;
    }
    /* value = mant * 2^e2; quantum 2^-m */
    if (e2 >= -m) {
        return x;
    }
    sh = -m - e2;
    if (sh > 62) {
        q = 0;
        rem = mant;
        half = 0;                       /* rem < 2^(sh-1): below half, not zero */
    } else {
        q = mant >> sh;
        rem = mant & ((1ull << sh) - 1);
        half = 1ull << (sh - 1);
    }
    if (!rem) {
        return x;
    }
    switch (rc) {
    case 0:
        up = half && (rem > half || (rem == half && (q & 1)));
        break;
    case 1:
        up = s != 0;
        break;
    case 2:
        up = s == 0;
        break;
    default:
        up = false;
        break;
    }
    q += up;
    if (!(imm & 8)) {
        float_raise(float_flag_inexact, &env->sse_status);
    }
    if (!q) {
        return s;
    }
    /* q * 2^-m, q < 2^(fbits + 2): normalise */
    l = 63 - clz64(q);
    q = l > f->fbits ? q >> (l - f->fbits) : q << (f->fbits - l);
    return s | ((uint64_t)(l - m + f->bias) << f->fbits) | (q & f->fmask);
}
#endif /* __Use_Original_Qemu (U241) */
#if __Use_Original_Qemu != 1 /* ours (U236) */

/*
 * desc (U236-U241): bits 3:0 operation (0 VRCP14, 1 VRSQRT14, 2 VGETEXP, 3 VGETMANT,
 * 4 VRNDSCALE), bits 7:4 element size, bits 15:8 element count, bits 23:16 imm8.
 */
static uint64_t evex_fp1_elem(CPUX86State *env, uint32_t desc, uint64_t x)
{
    const EvFmt *f = evfmt((desc >> 4) & 0xf);
    int imm = (desc >> 16) & 0xff;

    switch (desc & 0xf) {
    case 0:
        return evex_rcp14(env, f, x, false);
    case 1:
        return evex_rcp14(env, f, x, true);
#if __Use_Original_Qemu != 1 /* ours (U237) */
    case 2:
        return evex_getexp(env, f, x);
#endif /* __Use_Original_Qemu (U237) */
#if __Use_Original_Qemu != 1 /* ours (U238) */
    case 3:
        return evex_getmant(env, f, x, imm);
#endif /* __Use_Original_Qemu (U238) */
#if __Use_Original_Qemu != 1 /* ours (U241) */
    case 4:
        return evex_rndscale(env, f, x, imm);
#endif /* __Use_Original_Qemu (U241) */
    default:
        g_assert_not_reached();
    }
    return 0;
}

void helper_evex_fp1(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint32_t desc)
{
    int esz = (desc >> 4) & 0xf, n = (desc >> 8) & 0xff, i;
    ZMMReg r;

    for (i = 0; i < n; i++) {
        evex_set_elem(&r, esz, i, evex_fp1_elem(env, desc, evex_get_elem(s, esz, i)));
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(d, esz, i, evex_get_elem(&r, esz, i));
    }
}

/* scalar form: DEST[elem 0] := op(SRC2[elem 0]), DEST[127:elem] := SRC1 (v: the register) */
void helper_evex_fp1s(CPUX86State *env, ZMMReg *d, ZMMReg *v, ZMMReg *s, uint32_t desc)
{
    int esz = (desc >> 4) & 0xf;
    uint64_t q0 = v->ZMM_Q(0), q1 = v->ZMM_Q(1);
    uint64_t r = evex_fp1_elem(env, desc, evex_get_elem(s, esz, 0));

    d->ZMM_Q(0) = q0;
    d->ZMM_Q(1) = q1;
    evex_set_elem(d, esz, 0, r);
}
#endif /* __Use_Original_Qemu (U236) */
#if __Use_Original_Qemu != 1 /* ours (U239) */

/*
 * NoVmp (ledger U239): VSCALEFPS/PD/SS/SD (SDM Vol2C: DEST := SRC1 * 2^floor(SRC2), Tables
 * 5-37/5-38): DAZ applies to both sources; DE only for a denormal SRC1 (not reported for
 * SRC2, and not with a NaN SRC2); the result is rounded once (MXCSR.RC / {er}) with the
 * usual overflow / underflow / FTZ responses (softfloat scalbn). NaN and infinity cases
 * from Table 5-37 (incl. QNaN SRC1 with SRC2 = +inf -> +inf, -inf -> +0).
 * desc: bits 7:4 element size, 15:8 element count, bit 24 scalar (bits 127:elem from SRC1).
 */
static uint64_t evex_scalef_elem(CPUX86State *env, const EvFmt *f, uint64_t a, uint64_t b)
{
    bool daz = env->mxcsr & 0x40;
    uint64_t sa = a & f->sign;
    int64_t n;

    if (daz && evf_isdenorm(f, a)) {
        a = sa;
    }
    if (daz && evf_isdenorm(f, b)) {
        b &= f->sign;
    }
    if (evf_isnan(f, a) || evf_isnan(f, b)) {
        if (evf_issnan(f, a) || evf_issnan(f, b)) {
            float_raise(float_flag_invalid, &env->sse_status);
        }
        if (evf_isnan(f, a)) {
            if (!evf_issnan(f, a) && b == f->emask) {
                return f->emask;                                    /* QNaN, +inf: +inf */
            }
            if (!evf_issnan(f, a) && b == (f->sign | f->emask)) {
                return 0;                                           /* QNaN, -inf: +0 */
            }
            return a | f->quiet;
        }
        return b | f->quiet;
    }
    if (evf_isdenorm(f, a)) {
        float_raise(float_flag_input_denormal_used, &env->sse_status);
    }
    if (evf_isinf(f, b)) {
        bool binf_neg = b & f->sign;

        if (evf_isinf(f, a)) {
            if (binf_neg) {
                float_raise(float_flag_invalid, &env->sse_status);
                return f->indef;
            }
            return a;
        }
        if (!(a & ~f->sign)) {
            if (!binf_neg) {
                float_raise(float_flag_invalid, &env->sse_status);
                return f->indef;
            }
            return a;
        }
        return binf_neg ? sa : (sa | f->emask);
    }
    if (evf_isinf(f, a) || !(a & ~f->sign)) {
        return a;
    }
    /* n = floor(b), clamped (|n| > 2^16 over- / underflows every finite source anyway) */
    {
        uint64_t ex = (b & f->emask) >> f->fbits, mant;
        int e2 = (int)ex - f->bias - f->fbits;
        bool neg = b & f->sign;

        if (!(b & ~f->sign)) {
            n = 0;
        } else if (!ex || e2 < -f->fbits) {                         /* |b| < 1 */
            n = neg ? -1 : 0;
        } else if (e2 >= 0) {
            n = (ex - f->bias) > 20 ? 0x20000 : ((int64_t)((b & f->fmask) | (f->fmask + 1)) << e2);
            n = neg ? -n : n;
        } else {
            mant = (b & f->fmask) | (f->fmask + 1);
            n = (int64_t)(mant >> -e2);
            if (neg) {
                n = -n - ((mant & ((1ull << -e2) - 1)) ? 1 : 0);
            }
        }
        n = n > 0x20000 ? 0x20000 : n < -0x20000 ? -0x20000 : n;
    }
    {
        float_status *fs = &env->sse_status;
        int flags = get_float_exception_flags(fs);
        uint64_t r;

        /* inputs already DAZ-processed; a denormal SRC1 must not be flushed again */
        set_float_exception_flags(0, fs);
        if (f->bits == 64) {
            r = float64_scalbn(a, (int)n, fs);
        } else {
            r = float32_scalbn(a, (int)n, fs);
        }
        set_float_exception_flags(flags | (get_float_exception_flags(fs) &
                                           ~(float_flag_input_denormal |
                                             float_flag_input_denormal_used)), fs);
        return r;
    }
}

/* v: the SRC1 register itself (scalar form: bits 127:elem; a may be a neutral copy) */
void helper_evex_scalef(CPUX86State *env, ZMMReg *d, ZMMReg *v, ZMMReg *a, ZMMReg *b,
                        uint32_t desc)
{
    int esz = (desc >> 4) & 0xf, n = (desc >> 8) & 0xff, i;
    const EvFmt *f = evfmt(esz);
    ZMMReg r;

    if (desc & (1u << 24)) {
        r.ZMM_Q(0) = v->ZMM_Q(0);
        r.ZMM_Q(1) = v->ZMM_Q(1);
        n = 1;
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(&r, esz, i, evex_scalef_elem(env, f, evex_get_elem(a, esz, i),
                                                   evex_get_elem(b, esz, i)));
    }
    if (desc & (1u << 24)) {
        d->ZMM_Q(0) = r.ZMM_Q(0);
        d->ZMM_Q(1) = r.ZMM_Q(1);
        return;
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(d, esz, i, evex_get_elem(&r, esz, i));
    }
}
#endif /* __Use_Original_Qemu (U239) */
#if __Use_Original_Qemu != 1 /* ours (U240) */

/*
 * NoVmp (ledger U240): VFIXUPIMMPS/PD/SS/SD (SDM Vol2C FIXUPIMM_DP / FIXUPIMM_SP). Token of
 * tsrc (SRC1; with DAZ a denormal is a zero of its sign, Vol1 10.2.3.4): 0 QNaN,
 * 1 SNaN, 2 zero, 3 +1.0, 4 -inf, 5 +inf, 6 negative, 7 positive; response = SRC2 bits
 * [4j+3:4j] (0 keep DEST, 1 tsrc, 2 QNaN(tsrc) (tsrc with exponent all ones and the quiet
 * bit), 3 QNaN indefinite, 4 -inf, 5 +inf, 6 inf with tsrc's sign, 7 -0, 8 +0, 9 -1, A +1,
 * B 1/2, C 90.0, D pi/2, E max normal, F -max normal). imm8 selects ZE/IE reports; MXCSR
 * masks are ignored (never #XM), {sae} suppresses the flags; only active elements report.
 * desc: bits 7:4 element size, 15:8 element count, 23:16 imm8, bit 24 scalar, bit 25 {sae}.
 */
static uint64_t evex_fixupimm_elem(CPUX86State *env, const EvFmt *f, uint64_t dst, uint64_t a,
                                   uint64_t tbl, int imm, bool report)
{
    static const uint64_t c32[] = { 0xbf800000u, 0x3f800000u, 0x3f000000u, 0x42b40000u,
                                    0x3fc90fdbu };
    static const uint64_t c64[] = { 0xbff0000000000000ull, 0x3ff0000000000000ull,
                                    0x3fe0000000000000ull, 0x4056800000000000ull,
                                    0x3ff921fb54442d18ull };
    const uint64_t *c = f->bits == 64 ? c64 : c32;
    uint64_t t = (evf_expzero(f, a) && (env->mxcsr & 0x40)) ? (a & f->sign) : a;
    int j, flags = 0;

    if (evf_isnan(f, t)) {
        j = (t & f->quiet) ? 0 : 1;
    } else if (!(t & ~f->sign)) {
        j = 2;
    } else if (t == f->one) {
        j = 3;
    } else if (t == (f->sign | f->emask)) {
        j = 4;
    } else if (t == f->emask) {
        j = 5;
    } else {
        j = (t & f->sign) ? 6 : 7;
    }
    switch (j) {
    case 2:
        flags |= ((imm & 1) ? float_flag_divbyzero : 0) | ((imm & 2) ? float_flag_invalid : 0);
        break;
    case 3:
        flags |= ((imm & 4) ? float_flag_divbyzero : 0) | ((imm & 8) ? float_flag_invalid : 0);
        break;
    case 1:
        flags |= (imm & 0x10) ? float_flag_invalid : 0;
        break;
    case 4:
        flags |= (imm & 0x20) ? float_flag_invalid : 0;
        break;
    case 6:
        flags |= (imm & 0x40) ? float_flag_invalid : 0;
        break;
    case 5:
        flags |= (imm & 0x80) ? float_flag_invalid : 0;
        break;
    }
    if (report && flags) {
        float_raise(flags, &env->sse_status);
    }
    switch ((tbl >> (4 * j)) & 0xf) {
    case 0x0:
        return dst;
    case 0x1:
        return t;
    case 0x2:
        return t | f->emask | f->quiet;
    case 0x3:
        return f->indef;
    case 0x4:
        return f->sign | f->emask;
    case 0x5:
        return f->emask;
    case 0x6:
        return (t & f->sign) | f->emask;
    case 0x7:
        return f->sign;
    case 0x8:
        return 0;
    case 0xe:
        return f->maxf;
    case 0xf:
        return f->sign | f->maxf;
    default:
        return c[((tbl >> (4 * j)) & 0xf) - 9];
    }
}

void helper_evex_fixupimm(CPUX86State *env, ZMMReg *d, ZMMReg *dold, ZMMReg *a, ZMMReg *b,
                          uint64_t kmask, uint32_t desc)
{
    int esz = (desc >> 4) & 0xf, n = (desc >> 8) & 0xff, imm = (desc >> 16) & 0xff, i;
    bool scalar = desc & (1u << 24), sae = desc & (1u << 25);
    const EvFmt *f = evfmt(esz);
    ZMMReg r;

    if (scalar) {
        r.ZMM_Q(0) = a->ZMM_Q(0);
        r.ZMM_Q(1) = a->ZMM_Q(1);
        n = 1;
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(&r, esz, i,
                      evex_fixupimm_elem(env, f, evex_get_elem(dold, esz, i),
                                         evex_get_elem(a, esz, i), evex_get_elem(b, esz, i),
                                         imm, !sae && ((kmask >> i) & 1)));
    }
    if (scalar) {
        d->ZMM_Q(0) = r.ZMM_Q(0);
        d->ZMM_Q(1) = r.ZMM_Q(1);
        return;
    }
    for (i = 0; i < n; i++) {
        evex_set_elem(d, esz, i, evex_get_elem(&r, esz, i));
    }
}
#endif /* __Use_Original_Qemu (U240) */
#if __Use_Original_Qemu != 1 /* ours (U331) */
/* NoVmp (ledger U331): AVX512-FP16 helpers */
#include "fp16_helper.c.inc"
#endif /* __Use_Original_Qemu (U331) */
#if __Use_Original_Qemu != 1 /* ours (U402) */

/*
 * NoVmp (ledger U402): AVX10.2 FP16 -> FP8 down-conversions, the helper pseudocode of the
 * AVX10.2 spec 361050-007 chapter 5.1 transcribed bit for bit. BF8 = E5M2 (the upper byte
 * of an FP16), HF8 = E4M3 (no infinity, S.1111.111 = NaN). No MXCSR interaction: DAZ and
 * FTZ are not obeyed, no flag is set, no exception is raised (spec 3.2.2, 9.1.2, 9.3.2).
 *   convert_fp16_to_bf8(x, s): Inf -> S.11110.11 (s) or Inf; NaN -> x[15:8] with bit 1 set;
 *     else RNE by adding 7Fh + x[8] and taking the upper byte; with s a result
 *     exponent of 11111b saturates to S.11110.11.
 *   convert_fp16_to_hf8(x, s): Inf -> S.1111.110 (s) or S.1111.111; NaN -> S.1111.111;
 *     |x| > 464 (FP16 exponent 23, mantissa > 340h) -> the same overflow results; FP16
 *     exponent <= 8 -> HF8 denormal (J bit, shift, sticky, RNE); else RNE on bits 9:7.
 *   convert_fp16_to_bf8_bias(x, b, s): Inf/NaN as above; else the upper byte of x + b
 *     (bias below the BF8 LSB, then truncation), saturating like the RNE form.
 *   convert_fp16_to_hf8_bias(x, b, s): Inf/NaN as above; overflow from x + (b >> 1)
 *     (>= 480); FP16 denormal input: (m + (b << 7)) >> 15; FP16 exponent (after the bias)
 *     <= 8: J bit, + (b << (8 - e)), >> (9 - e), truncation; else truncation of x + (b >> 1).
 * The spec's text says the bias forms use RNE for denormal inputs; its pseudocode adds the
 * bias for every input and truncates - the pseudocode is followed.
 */
static bool avx10b_f16_inf(uint16_t x)
{
    return (x & 0x7fff) == 0x7c00;
}

static bool avx10b_f16_nan(uint16_t x)
{
    return (x & 0x7c00) == 0x7c00 && (x & 0x3ff) != 0;
}

static uint8_t avx10b_bf8_special(uint16_t x, bool sat)
{
    if (avx10b_f16_inf(x)) {
        return sat ? ((x >> 8) & 0x80) | 0x7b : x >> 8;
    }
    return (x >> 8) | 0x02;                     /* NaN: truncate, set the quiet bit */
}

static uint8_t avx10b_bf8_round(uint16_t temp, bool sat)
{
    if (((temp >> 8) & 0x7f) == 0x7c && sat) {
        return ((temp >> 8) & 0x80) | 0x7b;     /* E5M2_MAX */
    }
    return temp >> 8;
}

static uint8_t avx10b_fp16_to_bf8(uint16_t x, bool sat)
{
    if (avx10b_f16_inf(x) || avx10b_f16_nan(x)) {
        return avx10b_bf8_special(x, sat);
    }
    return avx10b_bf8_round((uint16_t)(x + 0x7f + ((x >> 8) & 1)), sat);
}

static uint8_t avx10b_fp16_to_bf8_bias(uint16_t x, uint8_t b, bool sat)
{
    if (avx10b_f16_inf(x) || avx10b_f16_nan(x)) {
        return avx10b_bf8_special(x, sat);
    }
    return avx10b_bf8_round((uint16_t)(x + b), sat);
}

#define AVX10B_HF8_REBIAS 8                     /* fp16_bias 15 - hf8_bias 7 */

static uint8_t avx10b_fp16_to_hf8(uint16_t x, bool sat)
{
    uint32_t sign = (x & 0x8000) >> 8, e16 = (x & 0x7c00) >> 10, m16 = x & 0x3ff;
    uint32_t e, m;

    if (avx10b_f16_inf(x)) {
        e = 0xf;
        m = sat ? 6 : 7;
    } else if (avx10b_f16_nan(x)) {
        e = 0xf;
        m = 7;
    } else if (e16 > AVX10B_HF8_REBIAS + 15 ||
               (e16 == AVX10B_HF8_REBIAS + 15 && m16 > 0x340)) {
        e = 0xf;                                /* overflow: NaN or E4M3_MAX */
        m = sat ? 6 : 7;
    } else if (e16 == 0 && m16 == 0) {
        e = 0;
        m = 0;
    } else if (e16 <= AVX10B_HF8_REBIAS) {      /* underflow: HF8 denormal */
        m = m16 | 0x400;
        m >>= AVX10B_HF8_REBIAS + 1 - e16;
        m |= ((m16 & 0x7f) + 0x7f) >> 7;        /* shift-out sticky into the LSB */
        m += 0x3f + ((m >> 7) & 1);             /* RNE */
        e = m >> 10;                            /* carry into the exponent */
        m = (m >> 7) & 7;
    } else {                                    /* normal: RNE */
        uint32_t rne = x + 0x3f + ((m16 >> 7) & 1);
        e = ((rne & 0x7c00) >> 10) - AVX10B_HF8_REBIAS;
        m = (rne & 0x3ff) >> 7;
    }
    return sign | (e << 3) | m;
}

static uint8_t avx10b_fp16_to_hf8_bias(uint16_t x, uint8_t b, bool sat)
{
    uint32_t sign = (x & 0x8000) >> 8, e16 = (x & 0x7c00) >> 10, m16 = x & 0x3ff;
    uint32_t xb = (uint16_t)(x + (b >> 1));
    uint32_t e16b = (xb & 0x7c00) >> 10, m16b = xb & 0x3ff;
    uint32_t e, m;

    if (avx10b_f16_inf(x)) {
        e = 0xf;
        m = sat ? 6 : 7;
    } else if (avx10b_f16_nan(x)) {
        e = 0xf;
        m = 7;
    } else if (e16b > AVX10B_HF8_REBIAS + 15 ||
               (e16b == AVX10B_HF8_REBIAS + 15 && m16b >= 0x380)) {
        e = 0xf;                                /* overflow: NaN or E4M3_MAX */
        m = sat ? 6 : 7;
    } else if (e16 == 0) {                      /* FP16 denormal or zero input */
        m = (m16 + ((uint32_t)b << 7)) >> (AVX10B_HF8_REBIAS + 7);
        e = 0;
    } else if (e16b <= AVX10B_HF8_REBIAS) {     /* underflow: HF8 denormal */
        m = m16 | 0x400;
        m += (uint32_t)b << (AVX10B_HF8_REBIAS - e16);
        m >>= AVX10B_HF8_REBIAS + 1 - e16;
        e = m >> 10;
        m = (m >> 7) & 7;
    } else {                                    /* normal: truncation */
        e = e16b - AVX10B_HF8_REBIAS;
        m = m16b >> 7;
    }
    return sign | (e << 3) | m;
}

/*
 * VCVTPH2[B,H]F8[S] (kind 0): dest.fp8[i] := cvt(src2.fp16[i]), i < VL/16, upper VL/2 zero;
 * VCVT2PH2[B,H]F8[S] (kind 1): dest.fp8[i], i < VL/8, from src2.fp16[i] (i < KL/2) or
 * src1.fp16[i - KL/2]; VCVTBIASPH2[B,H]F8[S] (kind 2): cvt_bias(src2.fp16[i], src1.byte[2i]).
 * Merging / zeroing per destination byte with mask bit i; DEST[MAXVL-1:n] := 0.
 */
void helper_avx10b_cvt_fp8(CPUX86State *env, ZMMReg *d, ZMMReg *s1, ZMMReg *s2,
                           uint64_t mask, uint32_t desc)
{
    int vl = desc & 0xff, kind = (desc >> 9) & 3, n = kind == 1 ? vl : vl / 2, i;
    bool zero = (desc >> 8) & 1, hf8 = (desc >> 11) & 1, sat = (desc >> 12) & 1;
    ZMMReg r = *d;

    for (i = 0; i < n; i++) {
        if ((mask >> i) & 1) {
            uint16_t x;

            if (kind == 1) {
                x = i < n / 2 ? s2->ZMM_W(i) : s1->ZMM_W(i - n / 2);
            } else {
                x = s2->ZMM_W(i);
            }
            if (kind == 2) {
                uint8_t b = s1->ZMM_B(2 * i);
                r.ZMM_B(i) = hf8 ? avx10b_fp16_to_hf8_bias(x, b, sat)
                                 : avx10b_fp16_to_bf8_bias(x, b, sat);
            } else {
                r.ZMM_B(i) = hf8 ? avx10b_fp16_to_hf8(x, sat) : avx10b_fp16_to_bf8(x, sat);
            }
        } else if (zero) {
            r.ZMM_B(i) = 0;
        }
    }
    for (i = n; i < (int)sizeof(ZMMReg); i++) {
        r.ZMM_B(i) = 0;
    }
    *d = r;
}
#endif /* __Use_Original_Qemu (U402) */
#if __Use_Original_Qemu != 1 /* ours (U403) */

/*
 * NoVmp (ledger U403): VCVTHF82PH, convert_hf8_to_fp16 of the AVX10.2 spec 5.1 (exact: an
 * HF8 denormal becomes a normal FP16, S.1111.111 becomes the FP16 NaN S.11111.1110000000);
 * d may alias s. The opmask and the zeroing above VL are applied by the EVEX engine.
 */
static uint16_t avx10b_hf8_to_fp16(uint8_t in)
{
    uint32_t s = (uint32_t)(in & 0x80) << 8, e = (in & 0x78) >> 3, m = in & 0x07;
    uint32_t e_norm = e + (15 - 7);

    if (e == 0 && m != 0) {
        uint32_t lz = 2;
        lz = m > 0x1 ? 1 : lz;
        lz = m > 0x3 ? 0 : lz;
        e_norm -= lz;
        m = (m << (lz + 1)) & 0x07;
    } else if (e == 0 && m == 0) {
        e_norm = 0;
    } else if (e == 0xf && m == 0x7) {
        e_norm = 0x1f;
    }
    return (e_norm << 10) | (m << 7) | s;
}

void helper_avx10b_cvthf82ph(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint32_t vl)
{
    ZMMReg r;
    int i;

    for (i = 0; i < (int)vl / 2; i++) {
        r.ZMM_W(i) = avx10b_hf8_to_fp16(s->ZMM_B(i));
    }
    for (i = 0; i < (int)vl / 2; i++) {
        d->ZMM_W(i) = r.ZMM_W(i);
    }
}
#endif /* __Use_Original_Qemu (U403) */
#if __Use_Original_Qemu != 1 /* ours (U404) */

/*
 * NoVmp (ledger U404): VCVT2PS2PHX (AVX10.2 spec 9.2): dest.word[i] (i < VL/16) :=
 * convert_fp32_to_fp16(src2.fp32[i]) for i < KL/2, else of src1.fp32[i - KL/2]. Rounding:
 * MXCSR.RC or {er}; MXCSR.DAZ applies to the FP32 inputs, FTZ is not applied to the FP16
 * results. MXCSR flags (IE DE OE UE PE) are set as if every exception were masked, from the
 * active elements only, and no #XM is raised; {er} implies SAE (no flag). NaN: quieted, the
 * upper fraction bits kept (m_fp16 = (m_fp32 | 400000h) >> 13). desc: bits 7:0 VL bytes,
 * bit 8 zeroing, bits 14:12 = {er} rounding + 1 (0 = MXCSR.RC).
 */
void helper_avx10b_cvt2ps2phx(CPUX86State *env, ZMMReg *d, ZMMReg *s1, ZMMReg *s2,
                              uint64_t mask, uint32_t desc)
{
    int vl = desc & 0xff, n = vl / 2, rc = (int)((desc >> 12) & 7) - 1, i;
    bool zero = (desc >> 8) & 1;
    float_status st = env->sse_status;
    ZMMReg r = *d;

    set_flush_to_zero(0, &st);
    set_float_exception_flags(0, &st);
    /* U404 x U445: MXCSR flags "as if all MXCSR numerical exceptions flags are masked" */
    st.unmasked_underflow = false;
    st.unmasked_overflow = false;
    if (rc >= 0) {
        set_x86_rounding_mode(rc, &st);
    }
    for (i = 0; i < n; i++) {
        if ((mask >> i) & 1) {
            uint32_t x = i < n / 2 ? s2->ZMM_L(i) : s1->ZMM_L(i - n / 2);
            r.ZMM_W(i) = float32_to_float16(make_float32(x), true, &st);
        } else if (zero) {
            r.ZMM_W(i) = 0;
        }
    }
    for (i = n; i < (int)sizeof(ZMMReg) / 2; i++) {
        r.ZMM_W(i) = 0;
    }
    *d = r;
    if (rc < 0) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) |
                                  (get_float_exception_flags(&st) &
                                   ~(float_flag_input_denormal | float_flag_output_denormal)),
                                  &env->sse_status);
    }
}
#endif /* __Use_Original_Qemu (U404) */
#if __Use_Original_Qemu != 1 /* ours (U407) */

/*
 * NoVmp (ledger U407): VDPPHPS (AVX10.2 spec 10.1): per dword i,
 *   srcdest.fp32[i] := fma32(srcdest.fp32[i], s1o, s2o); then fma32(., s1e, s2e)
 * with s1o/s1e = src1.fp16[2i+1]/[2i] and s2o/s2e = src2.fp16[2i+1]/[2i] converted exactly
 * (amx_cvt_fp16, FP16 denormals kept) and fma32 = amx_fma32 (DAZ = FTZ = 1, RNE, MXCSR
 * neither consulted nor updated; the NaN order gives the first NaN of src1.low, src2.low,
 * src1.high, src2.high, srcdest as the description requires). d may alias a or b.
 */
void helper_avx10b_vdpphps(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, uint32_t vl)
{
    ZMMReg r;
    int i;

    for (i = 0; i < (int)vl / 4; i++) {
        float32 acc = make_float32(d->ZMM_L(i));

        acc = amx_fma32(acc, amx_cvt_fp16(a->ZMM_W(2 * i + 1)), amx_cvt_fp16(b->ZMM_W(2 * i + 1)));
        acc = amx_fma32(acc, amx_cvt_fp16(a->ZMM_W(2 * i)), amx_cvt_fp16(b->ZMM_W(2 * i)));
        r.ZMM_L(i) = float32_val(acc);
    }
    for (i = 0; i < (int)vl / 4; i++) {
        d->ZMM_L(i) = r.ZMM_L(i);
    }
}
#endif /* __Use_Original_Qemu (U407) */
#if __Use_Original_Qemu != 1 /* ours (U552) */

/*
 * NoVmp (ledgers U552, U553): AVX512_VBMI2 concatenate-and-shift (SDM Vol2C VPSHLD, VPSHLDV,
 * VPSHRD, VPSHRDV). desc: bit 0 = right, bit 1 = variable count, bits 5:4 = element size
 * log2 (16/32/64-bit elements), bits 15:8 = VL in bytes, bits 23:16 = imm8. Per element, with
 * c = count modulo the element width w:
 *   VPSHLD  DEST := (concat(SRC2, SRC3) << c).upper half    (count imm8)
 *   VPSHLDV DEST := (concat(DEST, SRC2) << c).upper half    (count SRC3)
 *   VPSHRD  DEST := (concat(SRC3, SRC2) >> c).lower half    (count imm8)
 *   VPSHRDV DEST := (concat(SRC2, DEST) >> c).lower half    (count SRC3)
 * i.e. with x = SRC2 / DEST (the half that is shifted) and y = SRC3 / SRC2 (the half whose
 * bits are shifted in): left (x << c) | (y >> (w - c)), right (x >> c) | (y << (w - c)),
 * x for c = 0. d (the result, may be gen_evex_insn's scratch register holding DEST for the
 * variable forms) may alias a or b.
 */
void helper_evex_vbmi2_shd(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, uint32_t desc)
{
    int right = desc & 1, var = (desc >> 1) & 1, esz = (desc >> 4) & 3;
    int vl = (desc >> 8) & 0xff, imm = (desc >> 16) & 0xff;
    int bits = 8 << esz, n = vl >> esz, j;
    uint64_t wm = bits == 64 ? ~0ull : (1ull << bits) - 1;
    ZMMReg r;

    for (j = 0; j < n; j++) {
        uint64_t x, y, v;
        int c;

        if (var) {
            x = evex_get_elem(d, esz, j);
            y = evex_get_elem(a, esz, j);
            c = (int)(evex_get_elem(b, esz, j) & (bits - 1));
        } else {
            x = evex_get_elem(a, esz, j);
            y = evex_get_elem(b, esz, j);
            c = imm & (bits - 1);
        }
        if (c == 0) {
            v = x;
        } else if (right) {
            v = ((x >> c) | (y << (bits - c))) & wm;
        } else {
            v = ((x << c) | (y >> (bits - c))) & wm;
        }
        evex_set_elem(&r, esz, j, v);
    }
    memcpy(d, &r, vl);
}
#endif /* __Use_Original_Qemu (U552) */
#if __Use_Original_Qemu != 1 /* ours (U554) */

/*
 * NoVmp (ledger U554): VPMULTISHIFTQB (SDM Vol2C): for each qword i of SRC2 (tcur; the
 * broadcast element is replicated by gen_evex_insn) and byte j: ctrl := SRC1.qword[i].
 * byte[j] & 63; res.bit[k] := tcur.bit[(ctrl + k) mod 64], k = 0..7 - the low byte of tcur
 * rotated right by ctrl. vl = VL in bytes; d may alias a or b.
 */
void helper_evex_vpmultishiftqb(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, uint32_t vl)
{
    ZMMReg r;
    int i, j;

    for (i = 0; i < (int)vl / 8; i++) {
        uint64_t tcur = b->ZMM_Q(i);

        for (j = 0; j < 8; j++) {
            r.ZMM_B(8 * i + j) = (uint8_t)ror64(tcur, a->ZMM_B(8 * i + j) & 63);
        }
    }
    memcpy(d, &r, vl);
}
#endif /* __Use_Original_Qemu (U554) */
#if __Use_Original_Qemu != 1 /* ours (U556) */

/*
 * NoVmp (ledger U556): VCVTNE2PS2BF16 (SDM Vol2C): KL = VL/16 words; dest.word[i] :=
 * convert_fp32_to_bfloat16(i < KL/2 ? src2.fp32[i] : src1.fp32[i - KL/2]) - the lower half of
 * the result comes from SRC2 (b, ModRM.r/m), the upper half from SRC1 (a, EVEX.vvvv), over
 * the whole vector (not per 128-bit lane). convert_fp32_to_bfloat16 is the U88 function of
 * the VEX VCVTNEPS2BF16 (ops_sse.h ne_fp32_to_bf16: zero/denormal -> signed zero, inf
 * truncated, NaN truncated with bit 6 set, normal: RNE by integer add); MXCSR is neither
 * consulted nor updated. vl = VL in bytes; d may alias a or b.
 */
void helper_evex_cvtne2ps2bf16(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, uint32_t vl)
{
    int half = (int)vl / 4, i;          /* KL/2 = number of FP32 elements per source */
    ZMMReg r;

    for (i = 0; i < half; i++) {
        r.ZMM_W(i) = ne_fp32_to_bf16(b->ZMM_L(i));
        r.ZMM_W(half + i) = ne_fp32_to_bf16(a->ZMM_L(i));
    }
    memcpy(d, &r, vl);
}
#endif /* __Use_Original_Qemu (U556) */
#if __Use_Original_Qemu != 1 /* ours (U557) */

/*
 * NoVmp (ledger U557): one accumulation step of VDPBF16PS (SDM Vol2C: "FP32 FMA with daz in,
 * ftz out and RNE rounding. MXCSR neither consulted nor updated"; BF16 numerics 338302 1.2.1:
 * a three-way FP32 FMA with DAZ and FTZ "On", RNE, all exceptions masked): acc + x * y
 * rounded once (RNE) with denormal x / y / acc treated as zero and a tiny result flushed to
 * zero as MXCSR.FTZ does (tininess after rounding with an unbounded exponent, SDM Vol1
 * 4.9.1.5; the softfloat FTZ of the SSE helpers). NaNs (Table 5-4 "NaN Propagation
 * Priorities": src1 low, src2 low, src1 high, src2 high, srcdest, the low pair accumulating
 * last): a NaN operand gives the first NaN in the order x, y, acc, quieted; an invalid
 * operation (inf * 0, inf - inf) the QNaN indefinite FFC00000h.
 */
static float32 m4a_bf16_fma32(CPUX86State *env, float32 acc, float32 x, float32 y)
{
    float_status st = env->sse_status;
    float32 r;

    if (float32_is_any_nan(x)) {
        return make_float32(float32_val(x) | 0x00400000u);
    }
    if (float32_is_any_nan(y)) {
        return make_float32(float32_val(y) | 0x00400000u);
    }
    if (float32_is_any_nan(acc)) {
        return make_float32(float32_val(acc) | 0x00400000u);
    }
    set_float_rounding_mode(float_round_nearest_even, &st);
    set_flush_to_zero(true, &st);
    set_flush_inputs_to_zero(true, &st);
    set_float_ftz_detection(float_ftz_after_rounding, &st);
    set_default_nan_mode(false, &st);
    set_float_exception_flags(0, &st);
    st.unmasked_underflow = false;      /* U445 rules: MXCSR.UM / OM are not consulted */
    st.unmasked_overflow = false;
    r = float32_muladd(x, y, acc, 0, &st);
    if (get_float_exception_flags(&st) & float_flag_invalid) {
        return make_float32(0xffc00000u);
    }
    return r;
}

/*
 * VDPBF16PS: per dword i, make_fp32(bf16) = bf16 << 16 (exact):
 *   srcdest.fp32[i] += make_fp32(src1.bfloat16[2i+1]) * make_fp32(src2.bfloat16[2i+1])
 *   srcdest.fp32[i] += make_fp32(src1.bfloat16[2i+0]) * make_fp32(src2.bfloat16[2i+0])
 * d is the accumulator (gen_evex_insn's scratch register holding DEST, ev_dsrc) and the
 * result; vl = VL in bytes; d may alias a or b.
 */
void helper_evex_vdpbf16ps(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, uint32_t vl)
{
    ZMMReg r;
    int i;

    for (i = 0; i < (int)vl / 4; i++) {
        float32 acc = make_float32(d->ZMM_L(i));

        acc = m4a_bf16_fma32(env, acc, make_float32((uint32_t)a->ZMM_W(2 * i + 1) << 16),
                             make_float32((uint32_t)b->ZMM_W(2 * i + 1) << 16));
        acc = m4a_bf16_fma32(env, acc, make_float32((uint32_t)a->ZMM_W(2 * i) << 16),
                             make_float32((uint32_t)b->ZMM_W(2 * i) << 16));
        r.ZMM_L(i) = float32_val(acc);
    }
    memcpy(d, &r, vl);
}
#endif /* __Use_Original_Qemu (U557) */
