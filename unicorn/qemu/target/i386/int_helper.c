/*
 *  x86 integer helpers
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
#include "exec/exec-all.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "qemu/guest-random.h"

//#define DEBUG_MULDIV

/* modulo 9 table */
static const uint8_t rclb_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 0, 1, 2, 3, 4, 5,
    6, 7, 8, 0, 1, 2, 3, 4,
};

/* modulo 17 table */
static const uint8_t rclw_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 9, 10, 11, 12, 13, 14,
};

/* division, flags are undefined */

void helper_divb_AL(CPUX86State *env, target_ulong t0)
{
    unsigned int num, den, q, r;

    num = (env->regs[R_EAX] & 0xffff);
    den = (t0 & 0xff);
    if (den == 0) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q = (num / den);
    if (q > 0xff) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q &= 0xff;
    r = (num % den) & 0xff;
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xffff) | (r << 8) | q;
}

void helper_idivb_AL(CPUX86State *env, target_ulong t0)
{
    int num, den, q, r;

    num = (int16_t)env->regs[R_EAX];
    den = (int8_t)t0;
    if (den == 0) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q = (num / den);
    if (q != (int8_t)q) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q &= 0xff;
    r = (num % den) & 0xff;
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xffff) | (r << 8) | q;
}

void helper_divw_AX(CPUX86State *env, target_ulong t0)
{
    unsigned int num, den, q, r;

    num = (env->regs[R_EAX] & 0xffff) | ((env->regs[R_EDX] & 0xffff) << 16);
    den = (t0 & 0xffff);
    if (den == 0) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q = (num / den);
    if (q > 0xffff) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q &= 0xffff;
    r = (num % den) & 0xffff;
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xffff) | q;
    env->regs[R_EDX] = (env->regs[R_EDX] & ~0xffff) | r;
}

void helper_idivw_AX(CPUX86State *env, target_ulong t0)
{
    int num, den, q, r;

    num = (env->regs[R_EAX] & 0xffff) | ((env->regs[R_EDX] & 0xffff) << 16);
    den = (int16_t)t0;
    if (den == 0) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q = ((int64_t)num / den);
    if (q != (int16_t)q) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q &= 0xffff;
    r = (num % den) & 0xffff;
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xffff) | q;
    env->regs[R_EDX] = (env->regs[R_EDX] & ~0xffff) | r;
}

void helper_divl_EAX(CPUX86State *env, target_ulong t0)
{
    unsigned int den, r;
    uint64_t num, q;

    num = ((uint32_t)env->regs[R_EAX]) | ((uint64_t)((uint32_t)env->regs[R_EDX]) << 32);
    den = (unsigned int)t0;
    if (den == 0) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q = (num / den);
    r = (num % den);
    if (q > 0xffffffff) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    env->regs[R_EAX] = (uint32_t)q;
    env->regs[R_EDX] = (uint32_t)r;
}

void helper_idivl_EAX(CPUX86State *env, target_ulong t0)
{
    int den, r;
    int64_t num, q;

    num = ((uint32_t)env->regs[R_EAX]) | ((uint64_t)((uint32_t)env->regs[R_EDX]) << 32);
    den = (int)t0;
    if (den == 0) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    q = (num / den);
    r = (num % den);
    if (q != (int32_t)q) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    env->regs[R_EAX] = (uint32_t)q;
    env->regs[R_EDX] = (uint32_t)r;
}

/* bcd */

/* XXX: exception */
void helper_aam(CPUX86State *env, int base)
{
    int al, ah;

    al = env->regs[R_EAX] & 0xff;
    ah = al / base;
    al = al % base;
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xffff) | al | (ah << 8);
    CC_DST = al;
}

void helper_aad(CPUX86State *env, int base)
{
    int al, ah;

    al = env->regs[R_EAX] & 0xff;
    ah = (env->regs[R_EAX] >> 8) & 0xff;
    al = ((ah * base) + al) & 0xff;
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xffff) | al;
    CC_DST = al;
}

void helper_aaa(CPUX86State *env)
{
    int icarry;
    int al, ah, af;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    af = eflags & CC_A;
    al = env->regs[R_EAX] & 0xff;
    ah = (env->regs[R_EAX] >> 8) & 0xff;

    icarry = (al > 0xf9);
    if (((al & 0x0f) > 9) || af) {
        al = (al + 6) & 0x0f;
        ah = (ah + 1 + icarry) & 0xff;
        eflags |= CC_C | CC_A;
    } else {
        eflags &= ~(CC_C | CC_A);
        al &= 0x0f;
    }
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xffff) | al | (ah << 8);
    /* Compute PF, ZF, SF from result AL, clear OF -- matches observed
       hardware behaviour (and the approach used by helper_daa/helper_das).
       Intel documents these flags as undefined after AAA, but real CPUs
       consistently set them based on the masked AL result. */
    eflags &= CC_C | CC_A;
    eflags |= (al == 0) << 6; /* zf */
    eflags |= parity_table[al]; /* pf */
    eflags |= (al & 0x80); /* sf */
    CC_SRC = eflags;
}

void helper_aas(CPUX86State *env)
{
    int icarry;
    int al, ah, af;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    af = eflags & CC_A;
    al = env->regs[R_EAX] & 0xff;
    ah = (env->regs[R_EAX] >> 8) & 0xff;

    icarry = (al < 6);
    if (((al & 0x0f) > 9) || af) {
        al = (al - 6) & 0x0f;
        ah = (ah - 1 - icarry) & 0xff;
        eflags |= CC_C | CC_A;
    } else {
        eflags &= ~(CC_C | CC_A);
        al &= 0x0f;
    }
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xffff) | al | (ah << 8);
    /* Compute PF, ZF, SF from result AL, clear OF -- same fix as AAA above. */
    eflags &= CC_C | CC_A;
    eflags |= (al == 0) << 6; /* zf */
    eflags |= parity_table[al]; /* pf */
    eflags |= (al & 0x80); /* sf */
    CC_SRC = eflags;
}

void helper_daa(CPUX86State *env)
{
    int old_al, al, af, cf;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    cf = eflags & CC_C;
    af = eflags & CC_A;
    old_al = al = env->regs[R_EAX] & 0xff;

    eflags = 0;
    if (((al & 0x0f) > 9) || af) {
        al = (al + 6) & 0xff;
        eflags |= CC_A;
    }
    if ((old_al > 0x99) || cf) {
        al = (al + 0x60) & 0xff;
        eflags |= CC_C;
    }
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xff) | al;
    /* well, speed is not an issue here, so we compute the flags by hand */
    eflags |= (al == 0) << 6; /* zf */
    eflags |= parity_table[al]; /* pf */
    eflags |= (al & 0x80); /* sf */
    CC_SRC = eflags;
}

void helper_das(CPUX86State *env)
{
    int al, al1, af, cf;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    cf = eflags & CC_C;
    af = eflags & CC_A;
    al = env->regs[R_EAX] & 0xff;

    eflags = 0;
    al1 = al;
    if (((al & 0x0f) > 9) || af) {
        eflags |= CC_A;
        if (al < 6 || cf) {
            eflags |= CC_C;
        }
        al = (al - 6) & 0xff;
    }
    if ((al1 > 0x99) || cf) {
        al = (al - 0x60) & 0xff;
        eflags |= CC_C;
    }
    env->regs[R_EAX] = (env->regs[R_EAX] & ~0xff) | al;
    /* well, speed is not an issue here, so we compute the flags by hand */
    eflags |= (al == 0) << 6; /* zf */
    eflags |= parity_table[al]; /* pf */
    eflags |= (al & 0x80); /* sf */
    CC_SRC = eflags;
}

#ifdef TARGET_X86_64
static void add128(uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b)
{
    *plow += a;
    /* carry test */
    if (*plow < a) {
        (*phigh)++;
    }
    *phigh += b;
}

static void neg128(uint64_t *plow, uint64_t *phigh)
{
    *plow = ~*plow;
    *phigh = ~*phigh;
    add128(plow, phigh, 1, 0);
}

/* return TRUE if overflow */
static int div64(uint64_t *plow, uint64_t *phigh, uint64_t b)
{
    uint64_t q, r, a1, a0;
    int i, qb, ab;

    a0 = *plow;
    a1 = *phigh;
    if (a1 == 0) {
        q = a0 / b;
        r = a0 % b;
        *plow = q;
        *phigh = r;
    } else {
        if (a1 >= b) {
            return 1;
        }
        /* XXX: use a better algorithm */
        for (i = 0; i < 64; i++) {
            ab = a1 >> 63;
            a1 = (a1 << 1) | (a0 >> 63);
            if (ab || a1 >= b) {
                a1 -= b;
                qb = 1;
            } else {
                qb = 0;
            }
            a0 = (a0 << 1) | qb;
        }
#if defined(DEBUG_MULDIV)
        printf("div: 0x%016" PRIx64 "%016" PRIx64 " / 0x%016" PRIx64
               ": q=0x%016" PRIx64 " r=0x%016" PRIx64 "\n",
               *phigh, *plow, b, a0, a1);
#endif
        *plow = a0;
        *phigh = a1;
    }
    return 0;
}

/* return TRUE if overflow */
static int idiv64(uint64_t *plow, uint64_t *phigh, int64_t b)
{
    int sa, sb;

    sa = ((int64_t)*phigh < 0);
    if (sa) {
        neg128(plow, phigh);
    }
    sb = (b < 0);
    if (sb && (b != 0x8000000000000000LL)) {
        b = -b;
    }
    if (div64(plow, phigh, b) != 0) {
        return 1;
    }
    if (sa ^ sb) {
        if (*plow > (1ULL << 63)) {
            return 1;
        }
        *plow = 0-*plow;
    } else {
        if (*plow >= (1ULL << 63)) {
            return 1;
        }
    }
    if (sa) {
        *phigh = 0-*phigh;
    }
    return 0;
}

void helper_divq_EAX(CPUX86State *env, target_ulong t0)
{
    uint64_t r0, r1;

    if (t0 == 0) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    r0 = env->regs[R_EAX];
    r1 = env->regs[R_EDX];
    if (div64(&r0, &r1, t0)) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    env->regs[R_EAX] = r0;
    env->regs[R_EDX] = r1;
}

void helper_idivq_EAX(CPUX86State *env, target_ulong t0)
{
    uint64_t r0, r1;

    if (t0 == 0) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    r0 = env->regs[R_EAX];
    r1 = env->regs[R_EDX];
    if (idiv64(&r0, &r1, t0)) {
        raise_exception_ra(env, EXCP00_DIVZ, GETPC());
    }
    env->regs[R_EAX] = r0;
    env->regs[R_EDX] = r1;
}
#endif

#if TARGET_LONG_BITS == 32
# define ctztl  ctz32
# define clztl  clz32
#else
# define ctztl  ctz64
# define clztl  clz64
#endif

target_ulong helper_pdep(target_ulong src, target_ulong mask)
{
    target_ulong dest = 0;
    int i, o;

    for (i = 0; mask != 0; i++) {
        o = ctztl(mask);
        mask &= mask - 1;
        dest |= ((src >> i) & 1) << o;
    }
    return dest;
}

target_ulong helper_pext(target_ulong src, target_ulong mask)
{
    target_ulong dest = 0;
    int i, o;

    for (o = 0; mask != 0; o++) {
        i = ctztl(mask);
        mask &= mask - 1;
        dest |= ((src >> i) & 1) << o;
    }
    return dest;
}

#define SHIFT 0
#include "shift_helper_template.h"
#undef SHIFT

#define SHIFT 1
#include "shift_helper_template.h"
#undef SHIFT

#define SHIFT 2
#include "shift_helper_template.h"
#undef SHIFT

#ifdef TARGET_X86_64
#define SHIFT 3
#include "shift_helper_template.h"
#undef SHIFT
#endif

/* Test that BIT is enabled in CR4.  If not, raise an illegal opcode
   exception.  This reduces the requirements for rare CR4 bits being
   mapped into HFLAGS.  */
void helper_cr4_testbit(CPUX86State *env, uint32_t bit)
{
    if (unlikely((env->cr[4] & bit) == 0)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }
}

#if __Use_Original_Qemu != 1 /* ours (U851) */
#ifdef TARGET_X86_64
/*
 * NoVmp (ledger U851): SDM Vol2D WRFSBASE/WRGSBASE, 64-bit mode exceptions: "#GP(0) If the source
 * register contains a non-canonical address"; Vol3A 4.5.3: unlike WRMSR (CPU canonicality) these
 * two check paging canonicality - 48 bits with 4-level paging, 57 with CR4.LA57 = 1. Called for
 * the 64-bit operand only (a 32-bit source is zero-extended, always canonical), before the base
 * changes. Upstream QEMU (7.2 translate.c, 11.1 gen_WRxxBASE) loads any value.
 */
void helper_wrxxbase_check(CPUX86State *env, target_ulong base)
{
    int shift = (env->cr[4] & CR4_LA57_MASK) ? 56 : 47;
    int64_t sext = (int64_t)base >> shift;

    if (sext != 0 && sext != -1) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
}
#endif
#endif /* __Use_Original_Qemu (U851) */

#if __Use_Original_Qemu == 1 /* original QEMU (U835) */
target_ulong HELPER(rdrand)(CPUX86State *env)
{
    target_ulong ret;

    if (qemu_guest_getrandom(&ret, sizeof(ret)) < 0) {
        // qemu_log_mask(LOG_UNIMP, "rdrand: Crypto failure: %s",
        //               error_get_pretty(err));
        // error_free(err);
        /* Failure clears CF and all other flags, and returns 0.  */
        env->cc_src = 0;
        return 0;
    }

    /* Success sets CF and clears all others.  */
    env->cc_src = CC_C;
    return ret;
}
#else /* ours (U835) */
/*
 * NoVmp (ledger U835, decision D8): RDRAND / RDSEED source, UC_CTL_X86_RDRAND.
 * Seeded (default): the n-th value drawn is SplitMix64(seed + (n + 1) * golden gamma)
 * (Steele, Lea, Flood 2014), CF = 1; seed and count live in env (uc_context carries them).
 * Host: the host CPU's RDRAND / RDSEED (chosen at run time from the host CPUID), CF as the
 * host returns it; the OS generator (CF = 1 unless it fails) when the host lacks it.
 * Both: CF = the success bit, OF SF ZF AF PF = 0 (SDM Vol2B RDRAND/RDSEED), destination 0
 * on failure. The translator truncates the value to the operand size.
 */
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#include <immintrin.h>
#endif

static uint64_t x86_rdrand_seeded(CPUX86State *env)
{
    uint64_t z = env->rdrand_seed + ++env->rdrand_count * 0x9e3779b97f4a7c15ULL;

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* host DRNG: 1 = success (*v valid), 0 = the host instruction reported CF = 0 */
static int x86_rdrand_host(uint64_t *v, bool seed)
{
#if defined(_MSC_VER) && defined(_M_X64)
    static int has_rdrand = -1, has_rdseed = -1;

    if (has_rdrand < 0) {
        int r[4];

        __cpuid(r, 0);
        if (r[0] >= 7) {
            __cpuidex(r, 7, 0);
            has_rdseed = (r[1] >> 18) & 1;
        } else {
            has_rdseed = 0;
        }
        __cpuid(r, 1);
        has_rdrand = (r[2] >> 30) & 1;
    }
    if (seed ? has_rdseed : has_rdrand) {
        unsigned __int64 x = 0;
        int ok = seed ? _rdseed64_step(&x) : _rdrand64_step(&x);

        *v = ok ? (uint64_t)x : 0;
        return ok;
    }
#endif
    *v = 0;
    if (qemu_guest_getrandom(v, sizeof(*v)) < 0) {
        *v = 0;
        return 0;
    }
    return 1;
}

static target_ulong x86_rdrand_draw(CPUX86State *env, bool seed)
{
    uint64_t v;

    if (!env->rdrand_host) {
        v = x86_rdrand_seeded(env);
    } else if (!x86_rdrand_host(&v, seed)) {
        env->cc_src = 0;            /* failure: CF = 0, destination 0 */
        return 0;
    }
    env->cc_src = CC_C;             /* success: CF = 1, the other flags 0 */
    return (target_ulong)v;
}

target_ulong HELPER(rdrand)(CPUX86State *env)
{
    return x86_rdrand_draw(env, false);
}

target_ulong HELPER(rdseed)(CPUX86State *env)
{
    return x86_rdrand_draw(env, true);
}
#endif /* __Use_Original_Qemu (U835) */
#if __Use_Original_Qemu != 1 /* ours (U1021) */
/*
 * NoVmp (ledger U1021): n bytes from the RDRAND source for instructions that use the hardware
 * RNG internally (PCONFIG KEYID_SET_KEY_RANDOM): one 64-bit RDRAND value per 8 bytes, little-
 * endian, in order; seeded mode draws from the shared sequence (and advances its count), host
 * mode returns false when the host DRNG fails (CF = 0). RFLAGS are not touched.
 */
bool x86_rdrand_bytes(CPUX86State *env, uint8_t *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i += 8) {
        uint64_t v;

        if (!env->rdrand_host) {
            v = x86_rdrand_seeded(env);
        } else if (!x86_rdrand_host(&v, false)) {
            return false;
        }
        stq_le_p(buf + i, v);
    }
    return true;
}
#endif /* __Use_Original_Qemu (U1021) */

#if __Use_Original_Qemu != 1 /* ours (U321) */
/*
 * NoVmp (ledger U321): one element-wise EVEX operation over the vector length (no masking:
 * gen_evex_insn merges / zeroes afterwards). desc = EVEX_UNOP_DESC(op, esz, vl); d may be
 * s (every source element is read before any result is written).
 * - EVEX_UNOP_LZCNT (VPLZCNTD/Q, SDM Vol2C): leading zero bits; the element width for 0.
 * - EVEX_UNOP_CONFLICT (VPCONFLICTD/Q): bit k of element j = (SRC[j] = SRC[k]) for k < j,
 *   bits j and up 0.
 */
static uint64_t evex_unop_get(const ZMMReg *r, int esz, int i)
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

static void evex_unop_set(ZMMReg *r, int esz, int i, uint64_t v)
{
    switch (esz) {
    case MO_8:
        r->ZMM_B(i) = (uint8_t)v;
        break;
    case MO_16:
        r->ZMM_W(i) = (uint16_t)v;
        break;
    case MO_32:
        r->ZMM_L(i) = (uint32_t)v;
        break;
    default:
        r->ZMM_Q(i) = v;
        break;
    }
}

void helper_evex_elem_unop(CPUX86State *env, ZMMReg *d, ZMMReg *s, uint32_t desc)
{
    int op = desc & 0xff, esz = (desc >> 8) & 3, vl = desc >> 16;
    int n = vl >> esz, bits = 8 << esz, i, k;
    uint64_t src[64];

    for (i = 0; i < n; i++) {
        src[i] = evex_unop_get(s, esz, i);
    }
    for (i = 0; i < n; i++) {
        uint64_t r = 0;

        switch (op) {
        case EVEX_UNOP_LZCNT:
            r = src[i] ? (uint64_t)(clz64(src[i]) - (64 - bits)) : (uint64_t)bits;
            break;
        case EVEX_UNOP_CONFLICT:
            for (k = 0; k < i; k++) {
                if (src[i] == src[k]) {
                    r |= 1ULL << k;
                }
            }
            break;
#if __Use_Original_Qemu != 1 /* ours (U323) */
        case EVEX_UNOP_POPCNT:      /* VPOPCNTB/W/D/Q: bits set in the element */
            r = ctpop64(src[i]);
            break;
#endif /* __Use_Original_Qemu (U323) */
        default:
            g_assert_not_reached();
        }
        evex_unop_set(d, esz, i, r);
    }
}
#endif /* __Use_Original_Qemu (U321) */

#if __Use_Original_Qemu != 1 /* ours (U324) */
/*
 * NoVmp (ledger U324): VPSHUFBITQMB (SDM Vol2C): k1[i*8+j] := SRC1.qword[i].bit[SRC2.qword[i].
 * byte[j] & 0x3F] for the vl / 8 qwords; bits vl and up are 0 (k2 is applied by the caller).
 */
uint64_t helper_evex_shufbitqmb(CPUX86State *env, ZMMReg *a, ZMMReg *b, uint32_t vl)
{
    uint64_t r = 0;
    int i, j;

    for (i = 0; i < (int)vl / 8; i++) {
        uint64_t q = a->ZMM_Q(i);

        for (j = 0; j < 8; j++) {
            r |= ((q >> (b->ZMM_B(i * 8 + j) & 0x3f)) & 1) << (i * 8 + j);
        }
    }
    return r;
}
#endif /* __Use_Original_Qemu (U324) */

#if __Use_Original_Qemu != 1 /* ours (U325) */
/*
 * NoVmp (ledger U325): byte permutes (SDM Vol2C), desc = op | VL bytes << 8, n = VL - 1:
 * op 0 VPERMB:   DEST.byte[j] := SRC2.byte[SRC1.byte[j] & n]
 * op 1 VPERMI2B: i := DEST.byte[j]; DEST.byte[j] := (i & VL) ? SRC2.byte[i & n] : SRC1.byte[i & n]
 *                (the Operation box writes "off := 8*SRC1[...]" but selects the table with
 *                TMP_DEST and the Description names the destination as the index operand)
 * op 2 VPERMT2B: i := SRC1.byte[j]; DEST.byte[j] := (i & VL) ? SRC2.byte[i & n] : DEST.byte[i & n]
 * d is the result (maybe a scratch register), old the destination register before the
 * instruction; every source byte is read before any result byte is written.
 */
void helper_evex_permb(CPUX86State *env, ZMMReg *d, ZMMReg *old, ZMMReg *a, ZMMReg *b,
                       uint32_t desc)
{
    int op = desc & 0xff, vl = desc >> 8, n = vl - 1, j;
    uint8_t o[64], s1[64], s2[64], r[64];

    for (j = 0; j < vl; j++) {
        o[j] = old->ZMM_B(j);
        s1[j] = a->ZMM_B(j);
        s2[j] = b->ZMM_B(j);
    }
    for (j = 0; j < vl; j++) {
        uint8_t i;

        switch (op) {
        case 0:
            r[j] = s2[s1[j] & n];
            break;
        case 1:
            i = o[j];
            r[j] = (i & vl) ? s2[i & n] : s1[i & n];
            break;
        default:
            i = s1[j];
            r[j] = (i & vl) ? s2[i & n] : o[i & n];
            break;
        }
    }
    for (j = 0; j < vl; j++) {
        d->ZMM_B(j) = r[j];
    }
}
#endif /* __Use_Original_Qemu (U325) */
#if __Use_Original_Qemu != 1 /* ours (U571) */

/*
 * NoVmp (ledger U571): VP2INTERSECTD/Q (SDM Vol2C Operation): maskregs[dest_base+0] and
 * [dest_base+1] := 0 (all 64 bits: MAX_KL), then for every i, j < KL: match := (src1.elem[i]
 * == src2.elem[j]); maskregs[dest_base+0].bit[i] |= match; maskregs[dest_base+1].bit[j] |=
 * match. desc: bits 1:0 element size log2 (2 = dword, 3 = qword), bits 15:8 KL, bits 18:16
 * dest_base + 1 (written here); the dest_base mask is returned. Both sources are read before
 * any opmask is written (they are vector registers or memory, never opmasks).
 */
uint64_t helper_evex_vp2intersect(CPUX86State *env, ZMMReg *a, ZMMReg *b, uint32_t desc)
{
    int esz = desc & 3, kl = (desc >> 8) & 0xff, odd = (desc >> 16) & 7, i, j;
    uint64_t m0 = 0, m1 = 0;

    for (i = 0; i < kl; i++) {
        for (j = 0; j < kl; j++) {
            bool match = esz == 3 ? a->ZMM_Q(i) == b->ZMM_Q(j) : a->ZMM_L(i) == b->ZMM_L(j);

            if (match) {
                m0 |= 1ULL << i;
                m1 |= 1ULL << j;
            }
        }
    }
    env->opmask_regs[odd] = m1;
    return m0;
}
#endif /* __Use_Original_Qemu (U571) */
