/*
 * NoVmp (ledgers U331-U339): AVX512-FP16 helpers (EVEX maps 5 and 6, SDM Vol2C VADDPH ..
 * VUCOMISH). Included at the end of fpu_helper.c; our code only (__Use_Original_Qemu != 1).
 *
 * Numerics (SDM Vol1 4.8, 4.9, 11.5; the Vol2C FP16 pages):
 * - binary16 arithmetic through softfloat's float16 routines on a private float_status:
 *   rounding from MXCSR.RC or {er}, tininess after rounding (Vol1 4.9.1.5), no flush:
 *   "The instructions associated with AVX512_FP16 always handle FP16 denormal number
 *   inputs; denormal inputs are not treated as zero" and FP16 outputs "are not
 *   conditionally flushed to zero" (VCVTPH2PS/PSX, VCVTPD2PH, VCVTPS2PHX pages) - MXCSR.DAZ
 *   applies only to the FP32/FP64 sources of VCVTPS2PHX/VCVTPD2PH/VCVTSS2SH/VCVTSD2SH.
 * - NaN operands are handled here before softfloat, by the SDM rules: two sources ->
 *   SRC1 if it is a NaN, else SRC2, quietened (Vol1 Table 4-7); FMA -> the first NaN of the
 *   multiplicand, the multiplier, the addend (Vol1 Table 14-17), no sign change; IE for any
 *   SNaN; a NaN operand also suppresses the denormal exception (Vol1 4.9.2).
 * - underflow: masked -> tiny and inexact; MXCSR.UM = 0 -> tiny, exact or not (Vol1
 *   4.9.1.5, 11.5.2.5).
 * - with MXCSR.OM = 0 / UM = 0 an overflowing / tiny result is rounded with an unbounded
 *   exponent (softfloat rebias_overflow / rebias_underflow): PE only when that rounding is
 *   inexact (Vol1 4.9.1.4/4.9.1.5; the i5-13600K's F16C VCVTPS2PH does the same: OE
 *   without PE for 65536.0, UE without PE for an exact tiny result). Such a result always
 *   raises #XM, so its (rebiased) value is never stored.
 * - only the elements the opmask enables are computed (masked-off elements raise
 *   nothing, Vol1 15.6); then, unless {er}/{sae} suppressed every exception, an unmasked
 *   exception raises #XM (#UD with CR4.OSXMMEXCPT = 0) before the destination is written,
 *   the flags of the instruction still set, except that an unmasked IE/DE/ZE suppresses
 *   OE/UE/PE (the U67 rule of helper_sse_fp_end).
 */
#if __Use_Original_Qemu != 1 /* ours (U331) */
#include "fp16.h"

#define FX_IE 0x01
#define FX_DE 0x02
#define FX_ZE 0x04
#define FX_OE 0x08
#define FX_UE 0x10
#define FX_PE 0x20

#define F16_INDEF 0xfe00        /* QNaN indefinite */
#define F16_ONE   0x3c00

typedef struct Fp16Ctx {
    float_status st;
    uint32_t flags;     /* MXCSR flag bits 5:0 raised by the computed elements */
    int rc;             /* x86 rounding control of the instruction */
    bool sae;           /* {er} / {sae}: no flag, no #XM */
    bool tiny_um;       /* MXCSR.UM = 0: an exact tiny result signals underflow too */
} Fp16Ctx;

static inline bool f16_isnan(uint16_t a)
{
    return (a & 0x7fff) > 0x7c00;
}

static inline bool f16_issnan(uint16_t a)
{
    return f16_isnan(a) && !(a & 0x0200);
}

static inline bool f16_isinf(uint16_t a)
{
    return (a & 0x7fff) == 0x7c00;
}

static inline bool f16_iszero(uint16_t a)
{
    return (a & 0x7fff) == 0;
}

static inline bool f16_isden(uint16_t a)
{
    return !(a & 0x7c00) && (a & 0x03ff);
}

static inline uint16_t f16_quiet(uint16_t a)
{
    return a | 0x0200;
}

/* finite FP16 -> sign, integer significand m (11 bits with the implicit bit), e2: m * 2^e2 */
static inline void f16_decode(uint16_t a, int *sign, uint32_t *m, int *e2)
{
    int e = (a >> 10) & 0x1f;

    *sign = a >> 15;
    *m = a & 0x3ff;
    if (e) {
        *m |= 0x400;
    } else {
        e = 1;
    }
    *e2 = e - 25;
}

static uint32_t fp16_sf2mx(int f)
{
    return (f & float_flag_invalid ? FX_IE : 0) |
           (f & float_flag_input_denormal_used ? FX_DE : 0) |
           (f & float_flag_divbyzero ? FX_ZE : 0) |
           (f & float_flag_overflow ? FX_OE : 0) |
           (f & float_flag_underflow ? FX_UE : 0) |
           (f & float_flag_inexact ? FX_PE : 0);
}

static int fp16_mx2sf(uint32_t f)
{
    return (f & FX_IE ? float_flag_invalid : 0) |
           (f & FX_DE ? float_flag_input_denormal_used : 0) |
           (f & FX_ZE ? float_flag_divbyzero : 0) |
           (f & FX_OE ? float_flag_overflow : 0) |
           (f & FX_UE ? float_flag_underflow : 0) |
           (f & FX_PE ? float_flag_inexact : 0);
}

/* as_if_masked: "Execution occurs as if all MXCSR exceptions are masked" (VF[C]MULC*, VF[C]MADDC*) */
static void fp16_begin(CPUX86State *env, Fp16Ctx *c, uint32_t desc, bool as_if_masked)
{
    int rcf = FP16_DESC_RC(desc);

    memset(&c->st, 0, sizeof(c->st));
    c->sae = rcf != FP16_RC_MXCSR;
    c->rc = rcf < 4 ? rcf : (env->mxcsr >> 13) & 3;
    set_x86_rounding_mode(c->rc, &c->st);
    c->st.tininess_before_rounding = false;
    c->flags = 0;
    c->tiny_um = !c->sae && !as_if_masked && !(env->mxcsr & (1 << 11));
    c->st.rebias_overflow = !c->sae && !as_if_masked && !(env->mxcsr & (1 << 10));
    c->st.rebias_underflow = c->tiny_um;
}

/* account the softfloat flags of one FP16 result (forms that list Underflow) */
static uint16_t fp16_res(Fp16Ctx *c, uint16_t r)
{
    int f = get_float_exception_flags(&c->st);

    /*
     * rebias_underflow reports every result that is tiny before rounding; x86 detects
     * tininess after rounding (Vol1 4.9.1.5): one that rounds up to 2^-14 is not tiny and
     * is that normal value (rebiased exponent field 1 + 24).
     */
    if (c->st.rebias_underflow && (f & float_flag_underflow) && ((r >> 10) & 0x1f) == 25) {
        f &= ~float_flag_underflow;
        r = (r & 0x8000) | 0x0400;
    }
    c->flags |= fp16_sf2mx(f);
    if (c->tiny_um && f16_isden(r) && !(f & float_flag_inexact)) {
        c->flags |= FX_UE;
    }
    set_float_exception_flags(0, &c->st);
    return r;
}

/* the instruction's MXCSR flags; #XM (#UD) on an unmasked one, destination not written */
static void fp16_end(CPUX86State *env, Fp16Ctx *c, bool no_xm, uintptr_t ra)
{
    uint32_t f = c->flags, unm;

    if (c->sae) {
        return;
    }
    unm = no_xm ? 0 : f & ~(env->mxcsr >> 7) & 0x3f;
    if (unm & (FX_IE | FX_DE | FX_ZE)) {
        f &= ~(FX_OE | FX_UE | FX_PE);
    }
    float_raise(fp16_mx2sf(f), &env->sse_status);
    if (unm) {
        update_mxcsr_from_sse_status(env);
        raise_exception_ra(env, (env->cr[4] & CR4_OSXMMEXCPT_MASK) ? EXCP13_XM : EXCP06_ILLOP,
                           ra);
    }
}

/* two sources with a NaN: SRC1 if it is a NaN, else SRC2, quietened; IE for an SNaN */
static uint16_t fp16_nan2(Fp16Ctx *c, uint16_t a, uint16_t b)
{
    if (f16_issnan(a) || f16_issnan(b)) {
        c->flags |= FX_IE;
    }
    return f16_isnan(a) ? f16_quiet(a) : f16_quiet(b);
}

/* VADD/VSUB/VMUL/VDIV PH/SH */
static uint16_t fp16_arith(Fp16Ctx *c, int op, uint16_t a, uint16_t b)
{
    uint16_t r;

    if (f16_isnan(a) || f16_isnan(b)) {
        return fp16_nan2(c, a, b);
    }
    switch (op) {
    case FP16_OP_ADD:
        r = float16_add(a, b, &c->st);
        break;
    case FP16_OP_SUB:
        r = float16_sub(a, b, &c->st);
        break;
    case FP16_OP_MUL:
        r = float16_mul(a, b, &c->st);
        break;
    default:
        r = float16_div(a, b, &c->st);
        break;
    }
    return fp16_res(c, r);
}

/* total order key of a non-NaN FP16 value (+0 == -0) */
static inline int f16_key(uint16_t a)
{
    return (a & 0x8000) ? -(int)(a & 0x7fff) : (int)(a & 0x7fff);
}

/* VMIN/VMAX PH/SH (SDM pseudocode): both zero or either NaN -> SRC2 (unchanged, also an
   SNaN); IE for a NaN operand (signalling compare, as MINPS/MAXPS), DE for a denormal */
static uint16_t fp16_minmax(Fp16Ctx *c, bool max, uint16_t a, uint16_t b)
{
    if (f16_isnan(a) || f16_isnan(b)) {
        c->flags |= FX_IE;
        return b;
    }
    if (f16_iszero(a) && f16_iszero(b)) {
        return b;
    }
    if (f16_isden(a) || f16_isden(b)) {
        c->flags |= FX_DE;
    }
    if (max) {
        return f16_key(a) > f16_key(b) ? a : b;
    }
    return f16_key(a) < f16_key(b) ? a : b;
}

static uint16_t fp16_sqrt(Fp16Ctx *c, uint16_t a)
{
    if (f16_isnan(a)) {
        if (f16_issnan(a)) {
            c->flags |= FX_IE;
        }
        return f16_quiet(a);
    }
    return fp16_res(c, float16_sqrt(a, &c->st));
}

/*
 * VRCPPH/VRCPSH, VRSQRTPH/VRSQRTSH: the SDM only bounds the relative error (< 2^-11 +
 * 2^-14) and lists special cases (Tables 5-26, 5-36); no MXCSR flag ("SIMD Floating-Point
 * Exceptions: None"). We return the exact value rounded to nearest-even binary16 (relative
 * error <= 2^-11), which meets the bound and every special case of the tables.
 */
static uint16_t fp16_rcp(uint16_t a)
{
    float_status s;

    if (f16_isnan(a)) {
        return f16_quiet(a);
    }
    memset(&s, 0, sizeof(s));
    set_float_rounding_mode(float_round_nearest_even, &s);
    return float16_div(F16_ONE, a, &s);
}

static uint64_t fp16_isqrt64(uint64_t n)
{
    uint64_t r = 0, bit = 1ull << 62;

    while (bit > n) {
        bit >>= 2;
    }
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

static uint16_t fp16_rsqrt(uint16_t a)
{
    int sign, e2, sh, exp2, len;
    uint32_t m;
    uint64_t q, rem, half, mant;
    bool exact;

    if (f16_isnan(a)) {
        return f16_quiet(a);
    }
    if (f16_iszero(a)) {
        return (a & 0x8000) ? 0xfc00 : 0x7c00;      /* -0 -> -INF, +0 -> +INF */
    }
    if (a & 0x8000) {
        return F16_INDEF;                           /* X < 0, including -INF */
    }
    if (f16_isinf(a)) {
        return 0;
    }
    f16_decode(a, &sign, &m, &e2);
    if (e2 & 1) {
        m <<= 1;
        e2 -= 1;
    }
    /* 1/sqrt(m * 2^e2) = sqrt(2^60 / m) * 2^(-e2/2 - 30); q = floor(sqrt(2^60 / m)) */
    q = fp16_isqrt64((1ull << 60) / m);
    exact = q * q * m == (1ull << 60);
    len = 64 - clz64(q);
    sh = len - 11;
    mant = q >> sh;
    rem = q & ((1ull << sh) - 1);
    half = 1ull << (sh - 1);
    if (rem > half || (rem == half && (!exact || (mant & 1)))) {
        mant++;
    }
    exp2 = sh - e2 / 2 - 30;
    if (mant == 0x800) {
        mant >>= 1;
        exp2++;
    }
    return (uint16_t)(((exp2 + 25) << 10) | (mant & 0x3ff));
}

/* VGETEXPPH/SH (SDM getexp_fp16): floor(log2(|x|)); NaN -> QNaN(src) (Table 5-14) */
static uint16_t fp16_getexp(Fp16Ctx *c, uint16_t a)
{
    int sign, e2;
    uint32_t m;
    int ex;

    if (f16_isnan(a)) {
        if (f16_issnan(a)) {
            c->flags |= FX_IE;
        }
        return f16_quiet(a);
    }
    if (f16_isinf(a)) {
        return 0x7c00;
    }
    if (f16_iszero(a)) {
        return 0xfc00;
    }
    if (f16_isden(a)) {
        c->flags |= FX_DE;
    }
    f16_decode(a, &sign, &m, &e2);
    ex = e2 + 10 - clz32(m << 21);              /* m in [1, 2^11) */
    return int32_to_float16(ex, &c->st);        /* exact (-24..15) */
}

/* VGETMANTPH/SH (SDM getmant_fp16, Tables 5-17/5-18) */
static uint16_t fp16_getmant(Fp16Ctx *c, uint16_t a, int imm)
{
    int sc = (imm >> 2) & 3, interv = imm & 3;
    int sign = a >> 15, e = (a >> 10) & 0x1f, ex, odd, j;
    uint32_t m = a & 0x3ff;
    uint16_t one = (sc & 1) ? F16_ONE : 0xbc00;

    if (f16_isnan(a)) {
        if (f16_issnan(a)) {
            c->flags |= FX_IE;
        }
        return f16_quiet(a);
    }
    if (!sign && (f16_iszero(a) || f16_isinf(a))) {
        return F16_ONE;
    }
    if (sign) {
        if (f16_iszero(a)) {
            return one;
        }
        if (f16_isinf(a)) {
            if (sc & 2) {
                c->flags |= FX_IE;
                return F16_INDEF;
            }
            return one;
        }
        if (sc & 2) {
            c->flags |= FX_IE;
            return F16_INDEF;
        }
    }
    if (e == 0) {
        ex = 15;
        j = 0;
        while (!j) {
            j = (m >> 9) & 1;
            m = (m << 1) & 0x3ff;
            ex--;
        }
        c->flags |= FX_DE;
        e = ex;
    }
    odd = (e - 15) & 1;
    switch (interv) {
    case 0:
        ex = 15;
        break;
    case 1:
        ex = odd ? 14 : 15;
        break;
    case 2:
        ex = 14;
        break;
    default:
        ex = ((m >> 9) & 1) ? 14 : 15;
        break;
    }
    return (uint16_t)((((sc & 1) ? 0 : sign) << 15) | (ex << 10) | m);
}

/* round sign * m / 2^s (s >= 1) to an integer magnitude per rc; *inexact */
static uint64_t fp16_round_shift(uint64_t m, int s, int sign, int rc, bool *inexact)
{
    uint64_t q, rem, half;

    if (s >= 64) {
        q = 0;
        rem = m;
        half = ~0ull;           /* rem < half */
    } else {
        q = m >> s;
        rem = m & ((1ull << s) - 1);
        half = 1ull << (s - 1);
    }
    *inexact = rem != 0;
    if (rem) {
        switch (rc) {
        case 0:
            if (rem > half || (rem == half && (q & 1))) {
                q++;
            }
            break;
        case 1:
            if (sign) {
                q++;
            }
            break;
        case 2:
            if (!sign) {
                q++;
            }
            break;
        default:
            break;
        }
    }
    return q;
}

/*
 * VREDUCEPH/SH (SDM reduce_fp16, Table 5-28): src - 2^-M * ROUND(2^M * src), both steps
 * with imm8[2] ? MXCSR.RC : imm8[1:0]; PE unless imm8[3] (SPE); no underflow; a zero
 * result is +0 (-0 when rounding down); +-INF -> +0.
 */
static uint16_t fp16_reduce(CPUX86State *env, Fp16Ctx *c, uint16_t a, int imm)
{
    int mm = imm >> 4, rc = (imm & 4) ? (env->mxcsr >> 13) & 3 : imm & 3;
    int sign, e2, f;
    uint32_t m;
    uint64_t q;
    int64_t n;
    bool inexact;
    uint16_t r;
    float_status ls;

    if (f16_isnan(a)) {
        if (f16_issnan(a)) {
            c->flags |= FX_IE;
        }
        return f16_quiet(a);
    }
    if (f16_isinf(a) || f16_iszero(a)) {
        return f16_isinf(a) ? 0 : (rc == 1 ? 0x8000 : 0);
    }
    f16_decode(a, &sign, &m, &e2);
    if (e2 + mm >= 0) {
        return rc == 1 ? 0x8000 : 0;            /* 2^M * src is an integer */
    }
    q = fp16_round_shift(m, -(e2 + mm), sign, rc, &inexact);
    n = (int64_t)m - (int64_t)(q << -(e2 + mm));  /* (src - 2^-M * R) / 2^e2, magnitude */
    if (sign) {
        n = -n;
    }
    if (n == 0) {
        return rc == 1 ? 0x8000 : 0;
    }
    memset(&ls, 0, sizeof(ls));                 /* no rebias: never an underflow */
    set_x86_rounding_mode(rc, &ls);
    r = int64_to_float16_scalbn(n, e2, &ls);
    f = get_float_exception_flags(&ls);
    if ((inexact || (f & float_flag_inexact)) && !(imm & 8)) {
        c->flags |= FX_PE;
    }
    return r;
}

/*
 * VRNDSCALEPH/SH (SDM round_fp16_to_integer, Tables 5-30/5-31): 2^-M * round(2^M * src)
 * with imm8[2] ? MXCSR.RC : imm8[1:0]; signed zero kept; PE when src != dst unless SPE;
 * UE for a tiny (denormal) result, with inexact unless MXCSR.UM = 0.
 */
static uint16_t fp16_rndscale(CPUX86State *env, Fp16Ctx *c, uint16_t a, int imm)
{
    int mm = imm >> 4, rc = (imm & 4) ? (env->mxcsr >> 13) & 3 : imm & 3;
    int sign, e2;
    uint32_t m;
    uint64_t q;
    bool inexact;
    uint16_t r;

    if (f16_isnan(a)) {
        if (f16_issnan(a)) {
            c->flags |= FX_IE;
        }
        return f16_quiet(a);
    }
    if (f16_isinf(a) || f16_iszero(a)) {
        return a;
    }
    f16_decode(a, &sign, &m, &e2);
    if (e2 + mm >= 0) {
        return a;
    }
    q = fp16_round_shift(m, -(e2 + mm), sign, rc, &inexact);
    if (q == 0) {
        r = (uint16_t)(sign << 15);
    } else {
        /* q * 2^-M is exact (q <= 2^11, M <= 15) */
        float_status ls;

        memset(&ls, 0, sizeof(ls));
        r = int64_to_float16_scalbn(sign ? -(int64_t)q : (int64_t)q, -mm, &ls);
    }
    if (inexact && !(imm & 8)) {
        c->flags |= FX_PE;
    }
    if (f16_isden(r) && (inexact || c->tiny_um)) {
        c->flags |= FX_UE;
    }
    return r;
}

/* floor() of a finite FP16 value as an integer, clamped to +-100 */
static int fp16_floor_int(uint16_t b)
{
    int sign, e2;
    uint32_t m;
    int64_t v;
    bool frac;

    if (f16_iszero(b)) {
        return 0;
    }
    f16_decode(b, &sign, &m, &e2);
    if (e2 >= 0) {
        v = (int64_t)m << e2;
        frac = false;
    } else if (e2 <= -12) {
        v = 0;
        frac = true;
    } else {
        v = m >> -e2;
        frac = (m & ((1u << -e2) - 1)) != 0;
    }
    if (sign) {
        v = -v - (frac ? 1 : 0);
    }
    return v > 100 ? 100 : v < -100 ? -100 : (int)v;
}

/* VSCALEFPH/SH (Tables 5-39/5-40): SRC1 * 2^floor(SRC2); DE for SRC1 only */
static uint16_t fp16_scalef(Fp16Ctx *c, uint16_t a, uint16_t b)
{
    uint16_t r;

    if (f16_isnan(a) || f16_isnan(b)) {
        if (f16_issnan(a) || f16_issnan(b)) {
            c->flags |= FX_IE;
        }
        if (f16_isnan(a)) {
            if (!f16_issnan(a) && f16_isinf(b)) {
                return (b & 0x8000) ? 0 : 0x7c00;   /* QNaN, +-INF: +INF / +0 (Table 5-39) */
            }
            return f16_quiet(a);
        }
        return f16_quiet(b);
    }
    if (f16_isinf(a)) {
        if (f16_isinf(b) && (b & 0x8000)) {
            c->flags |= FX_IE;
            return F16_INDEF;
        }
        return a;
    }
    if (f16_iszero(a)) {
        if (f16_isinf(b) && !(b & 0x8000)) {
            c->flags |= FX_IE;
            return F16_INDEF;
        }
        return a;
    }
    if (f16_isden(a)) {
        c->flags |= FX_DE;
    }
    if (f16_isinf(b)) {
        return (b & 0x8000) ? (a & 0x8000) : ((a & 0x8000) | 0x7c00);
    }
    r = float16_scalbn(a, fp16_floor_int(b), &c->st);
    c->st.float_exception_flags &= ~float_flag_input_denormal_used;
    return fp16_res(c, r);
}

/* +-(x * y) + z with the Table 14-17 NaN rules: Q(x), Q(y), Q(z) */
static uint16_t fp16_fma(Fp16Ctx *c, uint16_t x, uint16_t y, uint16_t z, bool negp, bool negc)
{
    if (f16_isnan(x) || f16_isnan(y) || f16_isnan(z)) {
        if (f16_issnan(x) || f16_issnan(y) || f16_issnan(z)) {
            c->flags |= FX_IE;
        }
        return f16_isnan(x) ? f16_quiet(x) : f16_isnan(y) ? f16_quiet(y) : f16_quiet(z);
    }
    return fp16_res(c, float16_muladd(x, y, z,
                                      (negp ? float_muladd_negate_product : 0) |
                                      (negc ? float_muladd_negate_c : 0), &c->st));
}

/* ---- conversions ---- */

static int fp16_type_size(int t)
{
    static const uint8_t sz[] = { 2, 4, 8, 2, 2, 4, 4, 8, 8 };
    return sz[t];
}

static uint64_t fp16_get(ZMMReg *r, int size, int i)
{
    switch (size) {
    case 2:
        return r->ZMM_W(i);
    case 4:
        return r->ZMM_L(i);
    default:
        return r->ZMM_Q(i);
    }
}

static void fp16_put(ZMMReg *r, int size, int i, uint64_t v)
{
    switch (size) {
    case 2:
        r->ZMM_W(i) = v;
        break;
    case 4:
        r->ZMM_L(i) = v;
        break;
    default:
        r->ZMM_Q(i) = v;
        break;
    }
}

/* FP16 -> integer: integer indefinite (signed 100..0b) / FFF..Fh (unsigned) on invalid */
static uint64_t fp16_to_int(Fp16Ctx *c, uint16_t a, int bits, bool uns, bool trunc)
{
    uint64_t indef = uns ? (bits == 64 ? ~0ull : (1ull << bits) - 1) : 1ull << (bits - 1);
    uint64_t q;
    int sign, e2;
    uint32_t m;
    bool inexact = false, bad;

    if (f16_isnan(a) || f16_isinf(a)) {
        c->flags |= FX_IE;
        return indef;
    }
    f16_decode(a, &sign, &m, &e2);
    if (e2 >= 0) {
        q = (uint64_t)m << e2;
    } else {
        q = fp16_round_shift(m, -e2, sign, trunc ? 3 : c->rc, &inexact);
    }
    if (uns) {
        bad = (sign && q != 0) || (bits < 64 && q > (1ull << bits) - 1);
    } else {
        bad = q > (1ull << (bits - 1)) - (sign ? 0 : 1);
    }
    if (bad) {
        c->flags |= FX_IE;
        return indef;
    }
    if (inexact) {
        c->flags |= FX_PE;
    }
    if (sign && !uns) {
        q = 0 - q;
    }
    return bits == 64 ? q : q & ((1ull << bits) - 1);
}

/* FP16 -> FP32 / FP64 (exact); de: the page lists the denormal exception */
static uint64_t fp16_widen(Fp16Ctx *c, uint16_t a, bool to64, bool de)
{
    uint64_t sign = a >> 15, m = a & 0x3ff;
    int e = (a >> 10) & 0x1f;

    if (e == 0x1f) {
        if (m && !(m & 0x200)) {
            c->flags |= FX_IE;
        }
        if (to64) {
            return (sign << 63) | 0x7ff0000000000000ull | (m ? (1ull << 51) | (m << 42) : 0);
        }
        return (sign << 31) | 0x7f800000u | (m ? (1u << 22) | (m << 13) : 0);
    }
    if (e == 0) {
        if (m == 0) {
            return to64 ? sign << 63 : sign << 31;
        }
        if (de) {
            c->flags |= FX_DE;
        }
        e = 1;
        while (!(m & 0x400)) {
            m <<= 1;
            e--;
        }
        m &= 0x3ff;
    }
    if (to64) {
        return (sign << 63) | ((uint64_t)(e - 15 + 1023) << 52) | (m << 42);
    }
    return (sign << 31) | ((uint64_t)(e - 15 + 127) << 23) | (m << 13);
}

/* FP32 / FP64 -> FP16: MXCSR.DAZ for the source; NaN: payload truncated, quietened */
static uint16_t fp16_narrow(CPUX86State *env, Fp16Ctx *c, uint64_t v, bool from64)
{
    uint16_t r;

    if (from64) {
        if ((v & 0x7fffffffffffffffull) > 0x7ff0000000000000ull) {
            if (!(v & (1ull << 51))) {
                c->flags |= FX_IE;
            }
            return (uint16_t)(((v >> 48) & 0x8000) | 0x7e00 | ((v >> 42) & 0x3ff));
        }
    } else if ((v & 0x7fffffffu) > 0x7f800000u) {
        if (!(v & (1u << 22))) {
            c->flags |= FX_IE;
        }
        return (uint16_t)(((v >> 16) & 0x8000) | 0x7e00 | ((v >> 13) & 0x3ff));
    }
    c->st.flush_inputs_to_zero = (env->mxcsr & 0x40) != 0;
    r = from64 ? float64_to_float16(v, true, &c->st) : float32_to_float16((uint32_t)v, true, &c->st);
    c->st.flush_inputs_to_zero = false;
    return fp16_res(c, r);
}

static uint64_t fp16_cvt(CPUX86State *env, Fp16Ctx *c, int st, int dt, uint64_t v, uint32_t desc)
{
    switch (dt) {
    case FP16_T_F16:
        switch (st) {
        case FP16_T_F32:
            return fp16_narrow(env, c, v, false);
        case FP16_T_F64:
            return fp16_narrow(env, c, v, true);
        case FP16_T_I16:
            return fp16_res(c, int64_to_float16((int16_t)v, &c->st));
        case FP16_T_U16:
            return fp16_res(c, uint64_to_float16((uint16_t)v, &c->st));
        case FP16_T_I32:
            return fp16_res(c, int64_to_float16((int32_t)v, &c->st));
        case FP16_T_U32:
            return fp16_res(c, uint64_to_float16((uint32_t)v, &c->st));
        case FP16_T_I64:
            return fp16_res(c, int64_to_float16((int64_t)v, &c->st));
        default:
            return fp16_res(c, uint64_to_float16(v, &c->st));
        }
    case FP16_T_F32:
        return fp16_widen(c, v, false, true);
    case FP16_T_F64:
        return fp16_widen(c, v, true, true);
    case FP16_T_I16:
        return fp16_to_int(c, v, 16, false, desc & FP16_D_TRUNC);
    case FP16_T_U16:
        return fp16_to_int(c, v, 16, true, desc & FP16_D_TRUNC);
    case FP16_T_I32:
        return fp16_to_int(c, v, 32, false, desc & FP16_D_TRUNC);
    case FP16_T_U32:
        return fp16_to_int(c, v, 32, true, desc & FP16_D_TRUNC);
    case FP16_T_I64:
        return fp16_to_int(c, v, 64, false, desc & FP16_D_TRUNC);
    default:
        return fp16_to_int(c, v, 64, true, desc & FP16_D_TRUNC);
    }
}

/* ---- one element (one FP16 pair for the complex forms) ---- */

static void fp16_elem(CPUX86State *env, Fp16Ctx *c, uint32_t desc, ZMMReg *r, ZMMReg *a,
                      ZMMReg *b, ZMMReg *dst, int i)
{
    int op = FP16_DESC_OP(desc), imm = FP16_DESC_IMM(desc);
    uint16_t x, y, z, t0, t1;

    switch (op) {
    case FP16_OP_ADD:
    case FP16_OP_SUB:
    case FP16_OP_MUL:
    case FP16_OP_DIV:
        r->ZMM_W(i) = fp16_arith(c, op, a->ZMM_W(i), b->ZMM_W(i));
        break;
    case FP16_OP_MIN:
    case FP16_OP_MAX:
        r->ZMM_W(i) = fp16_minmax(c, op == FP16_OP_MAX, a->ZMM_W(i), b->ZMM_W(i));
        break;
    case FP16_OP_SQRT:
        r->ZMM_W(i) = fp16_sqrt(c, b->ZMM_W(i));
        break;
    case FP16_OP_RCP:
        r->ZMM_W(i) = fp16_rcp(b->ZMM_W(i));
        break;
    case FP16_OP_RSQRT:
        r->ZMM_W(i) = fp16_rsqrt(b->ZMM_W(i));
        break;
    case FP16_OP_SCALEF:
        r->ZMM_W(i) = fp16_scalef(c, a->ZMM_W(i), b->ZMM_W(i));
        break;
    case FP16_OP_GETEXP:
        r->ZMM_W(i) = fp16_getexp(c, b->ZMM_W(i));
        break;
    case FP16_OP_GETMANT:
        r->ZMM_W(i) = fp16_getmant(c, b->ZMM_W(i), imm);
        break;
    case FP16_OP_REDUCE:
        r->ZMM_W(i) = fp16_reduce(env, c, b->ZMM_W(i), imm);
        break;
    case FP16_OP_RNDSCALE:
        r->ZMM_W(i) = fp16_rndscale(env, c, b->ZMM_W(i), imm);
        break;
    case FP16_OP_FMA:
    case FP16_OP_FMADDSUB:
    case FP16_OP_FMSUBADD:
        /* DEST = operand 1, SRC2 = EVEX.vvvv (a), SRC3 = r/m (b) */
        switch (imm & 3) {
        case FP16_FMA_132:
            x = dst->ZMM_W(i), y = b->ZMM_W(i), z = a->ZMM_W(i);
            break;
        case FP16_FMA_213:
            x = a->ZMM_W(i), y = dst->ZMM_W(i), z = b->ZMM_W(i);
            break;
        default:
            x = a->ZMM_W(i), y = b->ZMM_W(i), z = dst->ZMM_W(i);
            break;
        }
        r->ZMM_W(i) = fp16_fma(c, x, y, z, imm & FP16_FMA_NEGP,
                               op == FP16_OP_FMA ? (imm & FP16_FMA_NEGC) != 0 :
                               op == FP16_OP_FMADDSUB ? !(i & 1) : (i & 1));
        break;
    case FP16_OP_CMUL:
    case FP16_OP_CMULC:
    case FP16_OP_CFMA:
    case FP16_OP_CFMAC: {
        /* SDM VFCMADDCPH/VFMADDCPH, VFCMULCPH/VFMULCPH pseudocode, rounding at every step */
        uint16_t a0 = a->ZMM_W(2 * i), a1 = a->ZMM_W(2 * i + 1);
        uint16_t b0 = b->ZMM_W(2 * i), b1 = b->ZMM_W(2 * i + 1);
        bool conj = op == FP16_OP_CMULC || op == FP16_OP_CFMAC;

        if (op == FP16_OP_CMUL || op == FP16_OP_CMULC) {
            t0 = fp16_arith(c, FP16_OP_MUL, a0, b0);
            t1 = fp16_arith(c, FP16_OP_MUL, a1, b0);
        } else {
            t0 = fp16_fma(c, a0, b0, dst->ZMM_W(2 * i), false, false);
            t1 = fp16_fma(c, a1, b0, dst->ZMM_W(2 * i + 1), false, false);
        }
        r->ZMM_W(2 * i) = fp16_fma(c, a1, b1, t0, !conj, false);
        r->ZMM_W(2 * i + 1) = fp16_fma(c, a0, b1, t1, conj, false);
        break;
    }
    case FP16_OP_MOVSH:
    case FP16_OP_MOVSH_LD:
        r->ZMM_W(0) = b->ZMM_W(0);
        break;
    case FP16_OP_CVT: {
        int st = imm >> 4, dt = imm & 15;

        fp16_put(r, fp16_type_size(dt), i,
                 fp16_cvt(env, c, st, dt, fp16_get(b, fp16_type_size(st), i), desc));
        break;
    }
    default:
        g_assert_not_reached();
    }
}

/* destination element size in bytes */
static int fp16_dsize(uint32_t desc)
{
    switch (FP16_DESC_OP(desc)) {
    case FP16_OP_CMUL:
    case FP16_OP_CMULC:
    case FP16_OP_CFMA:
    case FP16_OP_CFMAC:
        return 4;
    case FP16_OP_CVT:
        return fp16_type_size(FP16_DESC_IMM(desc) & 15);
    default:
        return 2;
    }
}

/*
 * Vector FP16 operation. d: result (the destination register, or evex_t1 when
 * gen_evex_insn masks it), a: SRC1 (EVEX.vvvv), b: SRC2 (r/m, memory or {1toN} already in a
 * register image), dst: the destination register before the instruction (FMA and complex
 * multiply-add read it; merging of a scalar element). mask: the opmask (all ones without
 * one). Scalar forms: DEST[127:esz] from SRC1 (from DEST for the FMA forms), element 0
 * merged or zeroed here, the 16 bytes written to dst and d.
 */
void helper_fp16_op(CPUX86State *env, ZMMReg *d, ZMMReg *a, ZMMReg *b, ZMMReg *dst,
                    uint64_t mask, uint32_t desc)
{
    int op = FP16_DESC_OP(desc), n = FP16_DESC_N(desc), dsz = fp16_dsize(desc), i;
    bool cplx = op >= FP16_OP_CMUL && op <= FP16_OP_CFMAC;
    Fp16Ctx c;
    ZMMReg r;

    fp16_begin(env, &c, desc, cplx);
    memset(&r, 0, sizeof(r));
    if (desc & FP16_D_SCALAR) {
        if (op == FP16_OP_FMA) {
            memcpy(&r, dst, 16);
        } else if (op != FP16_OP_MOVSH_LD) {
            memcpy(&r, a, 16);
        }
    }
    for (i = 0; i < n; i++) {
        if ((mask >> i) & 1) {
            fp16_elem(env, &c, desc, &r, a, b, dst, i);
        }
    }
    fp16_end(env, &c, cplx, GETPC());
    if (desc & FP16_D_SCALAR) {
        if (!(mask & 1)) {
            fp16_put(&r, dsz, 0, (desc & FP16_D_Z) ? 0 : fp16_get(dst, dsz, 0));
        }
        memcpy(dst, &r, 16);
        memcpy(d, &r, 16);
    } else {
        memcpy(d, &r, n * dsz);
    }
}

/* VCMPPH/SH predicate truth tables over (LT, EQ, GT, UN) = bits 0..3 (Table 5-x / CMPPS) */
static const uint8_t fp16_cmp_truth[16] = {
    2, 1, 3, 8, 13, 14, 12, 7, 10, 9, 11, 0, 5, 6, 4, 15
};

/* relation of two FP16 values: 0 LT, 1 EQ, 2 GT, 3 unordered; IE/DE per the S/Q rules */
static int fp16_compare(Fp16Ctx *c, uint16_t a, uint16_t b, bool signaling)
{
    int ka, kb;

    if (f16_isnan(a) || f16_isnan(b)) {
        if (signaling || f16_issnan(a) || f16_issnan(b)) {
            c->flags |= FX_IE;
        }
        return 3;
    }
    if (f16_isden(a) || f16_isden(b)) {
        c->flags |= FX_DE;
    }
    ka = f16_key(a);
    kb = f16_key(b);
    return ka < kb ? 0 : ka == kb ? 1 : 2;
}

/* VFPCLASSPH/SH categories (Table 5-12) */
static bool fp16_class(uint16_t a, int imm)
{
    bool neg = a >> 15;

    return ((imm & 0x01) && f16_isnan(a) && !f16_issnan(a)) ||
           ((imm & 0x02) && !neg && f16_iszero(a)) ||
           ((imm & 0x04) && neg && f16_iszero(a)) ||
           ((imm & 0x08) && !neg && f16_isinf(a)) ||
           ((imm & 0x10) && neg && f16_isinf(a)) ||
           ((imm & 0x20) && f16_isden(a)) ||
           ((imm & 0x40) && neg && !f16_isnan(a) && !f16_isinf(a) && !f16_iszero(a)) ||
           ((imm & 0x80) && f16_issnan(a));
}

/* VCMPPH/SH (a = SRC1, b = SRC2) and VFPCLASSPH/SH (b) into an opmask: bit i for the
   enabled elements, 0 elsewhere (gen_evex_insn ANDs with k2 again) */
uint64_t helper_fp16_cmp(CPUX86State *env, ZMMReg *a, ZMMReg *b, uint64_t mask, uint32_t desc)
{
    int op = FP16_DESC_OP(desc), n = FP16_DESC_N(desc), imm = FP16_DESC_IMM(desc), i;
    int p = imm & 31;
    bool sig = ((0x6666 >> (p & 15)) & 1) ^ (p >> 4);
    uint64_t k = 0;
    Fp16Ctx c;

    fp16_begin(env, &c, desc, false);
    for (i = 0; i < n; i++) {
        if (!((mask >> i) & 1)) {
            continue;
        }
        if (op == FP16_OP_FPCLASS) {
            if (fp16_class(b->ZMM_W(i), imm)) {
                k |= 1ull << i;
            }
        } else if ((fp16_cmp_truth[p & 15] >> fp16_compare(&c, a->ZMM_W(i), b->ZMM_W(i), sig)) & 1) {
            k |= 1ull << i;
        }
    }
    if (op == FP16_OP_FPCLASS) {
        c.flags = 0;
    }
    fp16_end(env, &c, false, GETPC());
    return k;
}

/* VCOMISH (imm 1: IE for any NaN) / VUCOMISH (IE for an SNaN): ZF PF CF, OF SF AF = 0 */
void helper_fp16_comi(CPUX86State *env, ZMMReg *a, ZMMReg *b, uint32_t desc)
{
    static const int eflags[4] = { CC_C, CC_Z, 0, CC_Z | CC_P | CC_C };
    Fp16Ctx c;
    int rel;

    fp16_begin(env, &c, desc, false);
    rel = fp16_compare(&c, a->ZMM_W(0), b->ZMM_W(0), FP16_DESC_IMM(desc) & 1);
    fp16_end(env, &c, false, GETPC());
    CC_SRC = eflags[rel];
}

/* VCVT[T]SH2SI / VCVT[T]SH2USI r32/r64 (imm = FP16_CVT(F16, I32/U32/I64/U64)) */
uint64_t helper_fp16_cvt2i(CPUX86State *env, ZMMReg *a, uint32_t desc)
{
    Fp16Ctx c;
    uint64_t r;

    fp16_begin(env, &c, desc, false);
    r = fp16_cvt(env, &c, FP16_T_F16, FP16_DESC_IMM(desc) & 15, a->ZMM_W(0), desc);
    fp16_end(env, &c, false, GETPC());
    return r;
}

/* VCVTSI2SH / VCVTUSI2SH xmm1, xmm2, r/m32/64: DEST[15:0] = cvt, DEST[127:16] = SRC1 */
void helper_fp16_cvtsi(CPUX86State *env, ZMMReg *d, ZMMReg *a, uint64_t v, uint32_t desc)
{
    Fp16Ctx c;
    ZMMReg r;
    uint16_t h;

    fp16_begin(env, &c, desc, false);
    h = fp16_cvt(env, &c, FP16_DESC_IMM(desc) >> 4, FP16_T_F16, v, desc);
    fp16_end(env, &c, false, GETPC());
    memcpy(&r, a, 16);
    r.ZMM_W(0) = h;
    memcpy(d, &r, 16);
}
#endif /* __Use_Original_Qemu (U331) */
