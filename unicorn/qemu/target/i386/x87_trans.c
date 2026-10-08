/*
 * x87 transcendental engine (NoVmp, ledgers U53/U56, plan 1.12m) and the exact
 * x87 arithmetic core (U54).
 *
 * F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN, FSIN, FCOS and FSINCOS for finite,
 * in-domain operands, bit-exact with the Intel i5-13600K (value and FSW):
 *   - each function is the micro-op sequence of the decrypted Goldmont MSROM
 *     (entry points U0a50 F2XM1, U0a58 FPTAN, U00b8 FSINCOS, U0a60 FSIN,
 *     U00a8 FCOS, U00c0 FYL2X, U00c8 FYL2XP1, U00b0 FPATAN), evaluated with the
 *     datapath's own roundings (truncate to 67 bits, nearest-even at 64 bits,
 *     exact final operation) in exact dyadic arithmetic (X87D);
 *   - the coefficients are the Pentium FP ROM's (Shirriff; read errors
 *     corrected), the tables analytic (x87_trans_tab.h, generated): no
 *     measured corrections;
 *   - the final value is rounded once with FCW.RC by x87t_round (tininess
 *     after rounding, masked/unmasked UE/OE per SDM Vol1 4.9.1.5 / 8.5.5).
 * Special operands, domains and per-path behaviour are decided by the callers
 * in fpu_helper.c; this file only does the arithmetic.
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U53-U54): whole file */
#include "cpu.h"
#include "fpu/softfloat.h"
#include "fpu/softfloat-macros.h"
#include "x87_trans.h"
#include "x87_trans_tab.h"

/* float128 constants: round-to-nearest of the exact values */
#define F128(h, l) make_float128_init(h, l)
static const float128 X87T_ONE = F128(0x3FFF000000000000ULL, 0);

static inline bool f128_sign(float128 v) { return v.high >> 63; }
static inline int32_t f128_exp(float128 v) { return (v.high >> 48) & 0x7FFF; }
static inline uint64_t f128_frac0(float128 v) { return v.high & 0x0000FFFFFFFFFFFFULL; }
static inline uint64_t f128_frac1(float128 v) { return v.low; }

static float_status x87t_status(void)
{
    float_status s;
    memset(&s, 0, sizeof(s));
    set_float_rounding_mode(float_round_nearest_even, &s);
    set_floatx80_rounding_precision(floatx80_precision_x, &s);
    return s;
}

#define ADD(a, b) float128_add(a, b, s)
#define SUB(a, b) float128_sub(a, b, s)
#define MUL(a, b) float128_mul(a, b, s)
#define DIV(a, b) float128_div(a, b, s)
#define INT(v) int64_to_float128(v, s)

/* a + b with the sign of the rounding error (Knuth TwoSum, exact in binary128) */
static X87TVal two_sum(float128 a, float128 b, float_status *s)
{
    X87TVal r = { 0 };
    float128 sum = ADD(a, b), bb = SUB(sum, a), err;

    err = ADD(SUB(a, SUB(sum, bb)), SUB(b, bb));
    r.v = sum;
    r.tail = float128_is_zero(err) ? 0 : (f128_sign(err) ? -1 : 1);
    return r;
}

/* round the 64-bit significand 'mant' with the left-justified ulp fraction frac_hi:frac_lo */
static bool x87t_round_up(bool sign, uint64_t mant, uint64_t frac_hi, uint64_t frac_lo, int rc)
{
    bool inexact = frac_hi || frac_lo;
    bool tie = frac_hi == (1ULL << 63) && !frac_lo;

    switch (rc) {
    case 0:  return (frac_hi >> 63) && (!tie || (mant & 1));
    case 1:  return inexact && sign;
    case 2:  return inexact && !sign;
    default: return false;
    }
}

/*
 * Round hi:lo to its top 'keep' bits with FCW.RC (keep <= 0: the whole value
 * lies below the last kept position). Returns the kept bits as an integer; the
 * caller adds *up. *inexact: bits were dropped.
 */
static uint64_t x87t_keep(bool sign, uint64_t hi, uint64_t lo, int keep, int rc,
                          bool *up, bool *inexact)
{
    uint64_t k, fh, fl;

    if (keep >= 64) {
        k = hi; fh = lo; fl = 0;
    } else if (keep > 0) {
        k = hi >> (64 - keep);
        fh = (hi << keep) | (lo >> (64 - keep));
        fl = lo << keep;
    } else if (keep == 0) {
        k = 0; fh = hi; fl = lo;
    } else {
        k = 0; fh = 0; fl = (hi | lo) != 0;
    }
    *inexact = fh || fl;
    *up = x87t_round_up(sign, k, fh, fl, rc);
    return k;
}

/*
 * Shape an engine value into the x87 result. The value is v + tail * eps
 * (tail = sign of the part below float128 precision, eps infinitesimal).
 * The internal value is v (+ tail) reduced by val.bias * 2^-73 relative, kept
 * to 'pbits' bits (val.trunc overrides; 'Z' truncate, 'U'/'D' directed; 128 =
 * all); it is rounded to 64 bits with FCW.RC (0 nearest, 1 down, 2 up, 3 chop). SDM Vol1 4.9.1.5 / 8.5.5: tininess after rounding with an
 * unbounded exponent; masked underflow delivers the denormal (UE if also
 * inexact); unmasked underflow / overflow deliver the rounded significand
 * with the exponent scaled by 2^24576 / 2^-24576; masked overflow follows RC
 * (Table 4-11). PE: the true value is not representable. C1: the result is
 * larger in magnitude than the internal value.
 */
floatx80 x87t_round(X87TVal val, int pbits, char pmode, int rc, bool ue_masked, bool oe_masked,
                    X87TOut *o, bool second)
{
    return x87t_round_prec(val, pbits, pmode, 64, rc, ue_masked, oe_masked, o, second);
}

/*
 * As x87t_round, rounding the significand to 'prec' bits (FCW.PC: 24, 53 or
 * 64; SDM Vol1 8.1.5.2). Unmasked underflow / overflow store the significand
 * rounded to 'prec' bits with the exponent biased by +-24576 (8.5.4/8.5.5);
 * a result still out of range after the bias (massive FSCALE over/underflow)
 * becomes a signed infinity / zero.
 */
floatx80 x87t_round_prec(X87TVal val, int pbits, char pmode, int prec, int rc, bool ue_masked,
                         bool oe_masked, X87TOut *o, bool second)
{
    float128 v = val.v;
    bool sign = f128_sign(v), up, any, c1;
    int32_t e = f128_exp(v), e0;
    uint64_t hi = f128_frac0(v), lo = f128_frac1(v), mant;
    int drop;

    if (e == 0 && !(hi | lo)) {
        return packFloatx80(sign, 0, 0);
    }
    /* 128-bit significand hi:lo, leading 1 at bit 127 */
    if (e == 0) {
        e = 1;
        while (!(hi & (1ULL << 48))) {
            shortShift128Left(hi, lo, 1, &hi, &lo);
            e--;
        }
    } else {
        hi |= 1ULL << 48;
    }
    shortShift128Left(hi, lo, 15, &hi, &lo);
    e += val.scale;
    /* fold in the tail: +-1 in the last of the 15 spare bits */
    if (val.tail) {
        bool away = (val.tail < 0) != sign;     /* true magnitude below |v| */
        if (!away) {
            lo |= 1;
        } else {
            sub128(hi, lo, 0, 1, &hi, &lo);
            if (!(hi >> 63)) {
                shortShift128Left(hi, lo, 1, &hi, &lo);
                lo |= 1;
                e--;
            }
        }
    }
    if (val.bias > 0) {
        /* internal value: |v| (1 - bias 2^-73); hi:lo has its leading 1 at bit 127 */
        uint64_t d = (hi >> 9) * (uint64_t)val.bias;
        sub128(hi, lo, 0, d, &hi, &lo);
        if (!(hi >> 63)) {
            shortShift128Left(hi, lo, 1, &hi, &lo);
            e--;
        }
        lo |= 1;                        /* the bias is far below the remaining bits */
    }
    /* PE: the value has bits below 'prec' (before any internal truncation) */
    if (prec >= 64 ? lo != 0 : ((hi << prec) != 0 || lo != 0)) {
        o->flags |= X87T_PE;
    }
    /* internal value: keep pbits bits */
    drop = 128 - (val.trunc ? val.trunc : pbits);
    if (val.trunc) {
        pmode = 'Z';
    }
    if (drop > 0) {
        uint64_t mhi = drop > 64 ? (1ULL << (drop - 64)) - 1 : 0;
        uint64_t mlo = drop >= 64 ? ~0ULL : (1ULL << drop) - 1;
        any = (hi & mhi) || (lo & mlo);
        hi &= ~mhi;
        lo &= ~mlo;
        if (any && ((pmode == 'U' && !sign) || (pmode == 'D' && sign))) {
            add128(hi, lo, drop > 64 ? 1ULL << (drop - 64) : 0,
                   drop >= 64 ? (drop == 64 ? 1 : 0) : 1ULL << drop, &hi, &lo);
            if (!hi) {                  /* carried out of bit 127 */
                hi = 1ULL << 63;
                e++;
            }
        }
    }
    e0 = e;                             /* the value's exponent */

    /* round to 'prec' bits with an unbounded exponent: decides tininess (after rounding) */
    {
        bool inexact;
        uint64_t k = x87t_keep(sign, hi, lo, prec, rc, &up, &inexact);

        k += up;
        if (prec >= 64) {
            mant = k;
            if (up && !mant) {
                mant = 1ULL << 63;
                e++;
            }
        } else {
            if (k >> prec) {
                k >>= 1;
                e++;
            }
            mant = k << (64 - prec);
        }
    }
    c1 = up;
    if (e < 1 && ue_masked) {
        /*
         * tiny, masked: the denormal. FCW.PC applies to the 64-bit significand
         * field (i5-13600K: PC = 53, RC up, 0 + 2^-16445 = 0000:0000000000000800),
         * so the rounding position is field bit 64 - prec: prec - sh bits kept.
         */
        int sh = 1 - e0, keep = prec - sh;
        bool inexact;
        uint64_t k = x87t_keep(sign, hi, lo, keep, rc, &up, &inexact);

        k += up;
        mant = k << (64 - prec);        /* the kept bits end at field bit 64 - prec */
        o->flags |= X87T_TINY;
        if (inexact || (o->flags & X87T_PE)) {
            o->flags |= X87T_PE | X87T_UE;
        }
        e = (mant >> 63) ? 1 : 0;       /* rounded up into the smallest normal */
        c1 = up;
    } else if (e < 1) {
        /* unmasked underflow: the rounded significand, exponent biased by 2^24576 */
        o->flags |= X87T_UE;
        e += 24576;
        if (e < 1) {                    /* massive underflow: a signed zero, inexact (i5-13600K) */
            e = 0;
            mant = 0;
            c1 = false;
            o->flags |= X87T_PE;
        }
    } else if (e >= 0x7FFF) {
        if (oe_masked) {
            /* masked overflow: SDM Vol1 Table 4-11 */
            bool to_inf = rc == 0 || (rc == 1 && sign) || (rc == 2 && !sign);
            o->flags |= X87T_OE | X87T_PE;
            c1 = to_inf;
            if (to_inf) {
                e = 0x7FFF;
                mant = 1ULL << 63;
            } else {
                e = 0x7FFE;
                mant = prec >= 64 ? ~0ULL : ~0ULL << (64 - prec);
            }
        } else {
            /* unmasked overflow: the rounded significand, exponent biased by 2^-24576 */
            o->flags |= X87T_OE;
            e -= 24576;
            if (e >= 0x7FFF) {          /* massive overflow: a signed infinity, rounded up (i5-13600K) */
                e = 0x7FFF;
                mant = 1ULL << 63;
                c1 = true;
                o->flags |= X87T_PE;
            }
        }
    }
    if (second) {
        o->c1_second = c1;
    } else {
        o->c1 = c1;
    }
    return packFloatx80(sign, e, mant);
}

/* --- the functions (finite, in-domain operands) ----------------------------------------- */

static X87TVal val0(float128 v)
{
    X87TVal r = { 0 };
    r.v = v;
    r.tail = 0;
    return r;
}

/* x = m * 2^e with m in [1, 2) as float128 (finite, nonzero x; exact) */
static float128 f128_mant(floatx80 x, int32_t *e, float_status *s)
{
    uint64_t f = extractFloatx80Frac(x);
    int32_t ex = extractFloatx80Exp(x);

    *e = ex ? ex - 16383 : -16382 - clz64(f);
    return make_float128((extractFloatx80Sign(x) ? 0x8000000000000000ULL : 0) |
                         ((uint64_t)16383 << 48) | ((f << clz64(f) << 1) >> 16),
                         (f << clz64(f) << 1) << 48);
}

/* y * (l + tail eps) */
static X87TVal mul_tail(float128 y, X87TVal l, float_status *s)
{
    X87TVal r = { 0 };
    r.v = MUL(y, l.v);
    r.scale = l.scale;
    r.bias = l.bias;
    r.trunc = l.trunc;
    r.tail = l.tail * (f128_sign(y) ? -1 : 1);
    if (!float128_is_zero(r.v) && r.tail == 0) {
        /* the product rounding itself: exact iff y * l fits (fma residual) */
        float128 res = float128_muladd(y, l.v, float128_chs(r.v), 0, s);
        r.tail = float128_is_zero(res) ? 0 : (f128_sign(res) ? -1 : 1);
    }
    return r;
}

/* a / b plus the division error and the tails of a, b to first order */
static X87TVal div_tail(X87TVal a, X87TVal b, float_status *s)
{
    float128 q = DIV(a.v, b.v);
    float128 rem = float128_muladd(float128_chs(q), b.v, a.v, 0, s);
    X87TVal r = { 0 };
    r.v = q;
    r.scale = a.scale - b.scale;
    if (!float128_is_zero(rem)) {
        r.tail = (f128_sign(rem) != f128_sign(b.v)) ? -1 : 1;
    } else {
        /* exact quotient: the operand tails decide (a+da)/(b+db) ~ q (1 + da/a - db/b) */
        int ta = a.tail * (f128_sign(a.v) ? -1 : 1), tb = b.tail * (f128_sign(b.v) ? -1 : 1);
        int rel = ta != 0 ? ta : -tb;   /* relative change, crude when both are set */
        r.tail = rel * (f128_sign(q) ? -1 : 1);
    }
    return r;
}

/*
 * a op b (X87T_OP_*) for finite nonzero operands, exactly: the operands are
 * taken as significand and exponent, the float128 result keeps 113 bits and
 * the sign of the rest as the tail, the exponent goes to the scale. Rounded by
 * x87t_round_prec this gives FADD/FSUB/FMUL/FDIV/FSQRT/FSCALE results at any
 * exponent (overflow, underflow, the 2^+-24576 biased results). a - a = 0 is
 * not handled here (the caller keeps softfloat's signed zero).
 */
X87TVal x87t_arith(int op, floatx80 a, floatx80 b)
{
    float_status st = x87t_status(), *s = &st;
    int32_t ea, eb = 0;
    float128 ma, mb = float128_zero;
    X87TVal r = { 0 };

    if ((op == X87T_OP_ADD || op == X87T_OP_SUB) &&
        !extractFloatx80Exp(a) && !extractFloatx80Frac(a)) {
        /* 0 +- b = +-b exactly */
        r.v = f128_mant(b, &r.scale, s);
        if (op == X87T_OP_SUB) {
            r.v = float128_chs(r.v);
        }
        return r;
    }
    ma = f128_mant(a, &ea, s);

    if ((op == X87T_OP_ADD || op == X87T_OP_SUB) &&
        !extractFloatx80Exp(b) && !extractFloatx80Frac(b)) {
        r.v = ma;           /* a +- 0 = a exactly */
        r.scale = ea;
        return r;
    }
    if (op != X87T_OP_SQRT && op != X87T_OP_SCALE) {
        mb = f128_mant(b, &eb, s);
    }

    switch (op) {
    case X87T_OP_MUL:
        r = mul_tail(ma, val0(mb), s);
        r.scale += ea + eb;
        break;
    case X87T_OP_DIV:
        r = div_tail(val0(ma), val0(mb), s);
        r.scale += ea - eb;
        break;
    case X87T_OP_SQRT: {
        /* a > 0: sqrt(m 2^e) = sqrt(m 2^(e & 1)) 2^(e >> 1) */
        float128 m = (ea & 1) ? float128_scalbn(ma, 1, s) : ma, res;
        r.v = float128_sqrt(m, s);
        res = float128_muladd(r.v, r.v, float128_chs(m), 0, s);     /* r^2 - m, exact */
        r.tail = float128_is_zero(res) ? 0 : (f128_sign(res) ? 1 : -1);
        r.scale = (ea - (ea & 1)) / 2;
        break;
    }
    case X87T_OP_SCALE:
        /* a * 2^n: exact; n = the integer in b (already truncated by the caller) */
    {
        /* |n| beyond 2^17 is a massive over/underflow either way */
        int64_t n = floatx80_to_int64_round_to_zero(b, s);
        n = n > (1 << 17) ? (1 << 17) : (n < -(1 << 17) ? -(1 << 17) : n);
        r.v = ma;
        r.scale = ea + (int32_t)n;
        break;
    }
    default: {
        /* add / sub: align the smaller operand to the larger one's exponent */
        bool sub = op == X87T_OP_SUB;
        float128 big = ma, small = sub ? float128_chs(mb) : mb;
        int32_t eg = ea, d;

        if (eb > ea) {
            big = small;
            small = ma;
            eg = eb;
            d = eb - ea;
        } else {
            d = ea - eb;
        }
        /*
         * beyond 160 bits below the larger operand the smaller one only
         * decides the sticky bit: a same-signed 2^-160 stands in for it
         */
        small = d > 160 ? (f128_sign(small) ? float128_chs(float128_scalbn(X87T_ONE, -160, s))
                                            : float128_scalbn(X87T_ONE, -160, s))
                        : float128_scalbn(small, -d, s);
        r = two_sum(big, small, s);
        r.scale = eg;
        break;
    }
    }
    r.bias = 0;
    r.trunc = 0;
    return r;
}
/* --- exact dyadic engine for the transcendentals (U56) ------------------------------------ */

/*
 * value = (-1)^neg * m * 2^e with m < 2^256 (m[0] least significant). Results
 * keep at most X87D_W bits; anything shifted out is jammed into bit 0
 * (sticky), which is exact enough for every rounding below (<= 67 bits) and
 * for the final 64-bit rounding. The models' values stay far below that width
 * (products of two 67-bit values), so the jam is a safety net only.
 */
#define X87D_W 250

typedef struct X87D {
    uint64_t m[4];
    int32_t e;
    bool neg;
} X87D;

static int xd_bitlen(const X87D *a)
{
    int i;

    for (i = 3; i >= 0; i--) {
        if (a->m[i]) {
            return i * 64 + 64 - clz64(a->m[i]);
        }
    }
    return 0;
}

static bool xd_zero(const X87D *a)
{
    return !(a->m[0] | a->m[1] | a->m[2] | a->m[3]);
}

/* m >>= n, the shifted-out bits jammed into bit 0 */
static void xd_shr(uint64_t *m, int n, int limbs)
{
    bool st = false;
    int i, w = n / 64, b = n % 64;

    if (n <= 0) {
        return;
    }
    if (n >= limbs * 64) {
        for (i = 0; i < limbs; i++) {
            st |= m[i] != 0;
            m[i] = 0;
        }
        m[0] = st;
        return;
    }
    for (i = 0; i < w; i++) {
        st |= m[i] != 0;
    }
    if (b) {
        st |= (m[w] << (64 - b)) != 0;
    }
    for (i = 0; i < limbs; i++) {
        uint64_t lo = i + w < limbs ? m[i + w] : 0;
        uint64_t hi = i + w + 1 < limbs ? m[i + w + 1] : 0;
        m[i] = b ? (lo >> b) | (hi << (64 - b)) : lo;
    }
    m[0] |= st;
}

/* m <<= n (the caller guarantees it fits) */
static void xd_shl(uint64_t *m, int n)
{
    int i, w = n / 64, b = n % 64;

    if (n <= 0) {
        return;
    }
    for (i = 3; i >= 0; i--) {
        uint64_t lo = i - w >= 0 ? m[i - w] : 0;
        uint64_t lo2 = i - w - 1 >= 0 ? m[i - w - 1] : 0;
        m[i] = b ? (lo << b) | (lo2 >> (64 - b)) : lo;
    }
}

/* keep at most X87D_W bits */
static void xd_fit(X87D *a)
{
    int bl = xd_bitlen(a);

    if (bl > X87D_W) {
        xd_shr(a->m, bl - X87D_W, 4);
        a->e += bl - X87D_W;
    }
}

static X87D xd_c(const X87DC *c)
{
    X87D r = { { c->lo, c->hi, 0, 0 }, c->e, c->neg != 0 };
    return r;
}

/* n * 2^e */
static X87D xd_int(int64_t n, int32_t e)
{
    X87D r = { { n < 0 ? (uint64_t)0 - (uint64_t)n : (uint64_t)n, 0, 0, 0 }, e, n < 0 };
    return r;
}

/* the value of a floatx80 (finite; a denormal / pseudo-denormal uses exponent 1) */
static X87D xd_x80(floatx80 x)
{
    int32_t ex = extractFloatx80Exp(x);
    X87D r = { { extractFloatx80Frac(x), 0, 0, 0 }, (ex ? ex : 1) - 16383 - 63,
               extractFloatx80Sign(x) };
    return r;
}

static X87D xd_neg(X87D a)
{
    a.neg = !a.neg;
    return a;
}

static X87D xd_abs(X87D a)
{
    a.neg = false;
    return a;
}

/* |a| <=> |b| on aligned 256-bit magnitudes */
static int xd_ucmp(const uint64_t *a, const uint64_t *b)
{
    int i;

    for (i = 3; i >= 0; i--) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

static void xd_uadd(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
    uint64_t c = 0;
    int i;

    for (i = 0; i < 4; i++) {
        uint64_t s = a[i] + c;
        c = s < c;
        r[i] = s + b[i];
        c += r[i] < s;
    }
}

static void xd_usub(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
    uint64_t c = 0;
    int i;

    for (i = 0; i < 4; i++) {
        uint64_t d = a[i] - c;
        c = a[i] < c;
        r[i] = d - b[i];
        c += d < b[i];
    }
}

/* a + b, exact (up to the X87D_W-bit jam) */
static X87D xd_add(X87D a, X87D b)
{
    X87D r = { { 0 }, 0, false };
    int la, lb, top, t;

    if (xd_zero(&a)) {
        return b;
    }
    if (xd_zero(&b)) {
        return a;
    }
    la = xd_bitlen(&a) + a.e;
    lb = xd_bitlen(&b) + b.e;
    top = la > lb ? la : lb;
    t = a.e < b.e ? a.e : b.e;
    if (t < top - X87D_W) {
        t = top - X87D_W;
    }
    if (a.e > t) {
        xd_shl(a.m, a.e - t);
    } else {
        xd_shr(a.m, t - a.e, 4);
    }
    if (b.e > t) {
        xd_shl(b.m, b.e - t);
    } else {
        xd_shr(b.m, t - b.e, 4);
    }
    r.e = t;
    if (a.neg == b.neg) {
        xd_uadd(r.m, a.m, b.m);
        r.neg = a.neg;
    } else if (xd_ucmp(a.m, b.m) >= 0) {
        xd_usub(r.m, a.m, b.m);
        r.neg = a.neg;
    } else {
        xd_usub(r.m, b.m, a.m);
        r.neg = b.neg;
    }
    if (xd_zero(&r)) {
        r.neg = false;
    }
    xd_fit(&r);
    return r;
}

static X87D xd_sub(X87D a, X87D b)
{
    return xd_add(a, xd_neg(b));
}

/* a * b, exact (up to the jam) */
static X87D xd_mul(X87D a, X87D b)
{
    uint64_t p[8] = { 0 };
    X87D r;
    int i, j, bl;

    for (i = 0; i < 4; i++) {
        uint64_t c = 0;
        if (!a.m[i]) {
            continue;
        }
        for (j = 0; j < 4; j++) {
            uint64_t hi, lo;
            mul64To128(a.m[i], b.m[j], &hi, &lo);
            lo += c;
            hi += lo < c;
            p[i + j] += lo;
            hi += p[i + j] < lo;
            c = hi;
        }
        for (j = i + 4; c && j < 8; j++) {
            p[j] += c;
            c = p[j] < c;
        }
    }
    r.e = a.e + b.e;
    r.neg = a.neg != b.neg;
    for (bl = 0, i = 7; i >= 0; i--) {
        if (p[i]) {
            bl = i * 64 + 64 - clz64(p[i]);
            break;
        }
    }
    if (bl > X87D_W) {
        xd_shr(p, bl - X87D_W, 8);
        r.e += bl - X87D_W;
    }
    memcpy(r.m, p, sizeof(r.m));
    if (xd_zero(&r)) {
        r.neg = false;
    }
    return r;
}

/* round to P significant bits: 'z' toward zero, 'n' to nearest-even */
static X87D xd_rnd(X87D a, int p, char mode)
{
    int bl = xd_bitlen(&a), sh, i;
    uint64_t rem[4], half[4] = { 0 };
    bool up = false;

    if (bl <= p) {
        return a;
    }
    sh = bl - p;
    memcpy(rem, a.m, sizeof(rem));
    for (i = 0; i < 4; i++) {           /* rem = the low sh bits */
        int lo = i * 64;
        if (lo >= sh) {
            rem[i] = 0;
        } else if (sh - lo < 64) {
            rem[i] &= (1ULL << (sh - lo)) - 1;
        }
    }
    if (mode == 'n') {
        half[(sh - 1) / 64] = 1ULL << ((sh - 1) % 64);
        {
            int c = xd_ucmp(rem, half);
            uint64_t lsb = (a.m[sh / 64] >> (sh % 64)) & 1;
            up = c > 0 || (c == 0 && lsb);
        }
    }
    /* drop the low bits without jamming */
    {
        int w = sh / 64, b = sh % 64;
        for (i = 0; i < 4; i++) {
            uint64_t lo = i + w < 4 ? a.m[i + w] : 0;
            uint64_t hi = i + w + 1 < 4 ? a.m[i + w + 1] : 0;
            a.m[i] = b ? (lo >> b) | (hi << (64 - b)) : lo;
        }
    }
    a.e += sh;
    if (up) {
        uint64_t one[4] = { 1, 0, 0, 0 };
        xd_uadd(a.m, a.m, one);
    }
    return a;
}

/* |a| <=> |b| */
static int xd_cmpabs(X87D a, X87D b)
{
    X87D d = xd_sub(xd_abs(a), xd_abs(b));
    return xd_zero(&d) ? 0 : (d.neg ? -1 : 1);
}

/* a <=> b */
static int xd_cmp(X87D a, X87D b)
{
    X87D d = xd_sub(a, b);
    return xd_zero(&d) ? 0 : (d.neg ? -1 : 1);
}

/* floor(log2 |a|), a != 0 */
static int32_t xd_ilog2(const X87D *a)
{
    return xd_bitlen(a) - 1 + a->e;
}

/* floor(|a| * 2^s) for a result below 2^63 */
static int64_t xd_floor(X87D a, int32_t s)
{
    int32_t e = a.e + s;

    if (e >= 0) {
        return (int64_t)(a.m[0] << e);
    }
    if (-e >= 256) {
        return 0;
    }
    {
        uint64_t m[4];
        int w = -e / 64, b = -e % 64;
        memcpy(m, a.m, sizeof(m));
        return (int64_t)(b ? (m[w] >> b) | (w + 1 < 4 ? m[w + 1] << (64 - b) : 0) : m[w]);
    }
}

/*
 * a / b truncated toward zero to P + 2 (<= 180) significant bits, the
 * remainder jammed into the last bit: exact for any later rounding to P bits.
 */
static X87D xd_div(X87D a, X87D b, int p)
{
    X87D r = { { 0 }, 0, false };
    uint64_t R[4], D[4], q[4] = { 0 };
    int la, lb, i, n = p + 2;
    int32_t qe;

    if (xd_zero(&a)) {
        return r;
    }
    /* both operands to <= 120 bits (the models' operands are <= 70 bits) */
    la = xd_bitlen(&a);
    if (la > 120) {
        xd_shr(a.m, la - 120, 4);
        a.e += la - 120;
        la = 120;
    }
    lb = xd_bitlen(&b);
    if (lb > 120) {
        xd_shr(b.m, lb - 120, 4);
        b.e += lb - 120;
        lb = 120;
    }
    memcpy(R, a.m, sizeof(R));
    memcpy(D, b.m, sizeof(D));
    qe = a.e - b.e;
    if (la >= lb) {
        xd_shl(D, la - lb);
        qe += la - lb;
    } else {
        xd_shl(R, lb - la);
        qe -= lb - la;
    }
    /* R / D in (1/2, 2): n quotient bits, q = floor(R/D * 2^(n-1)) */
    for (i = 0; i < n; i++) {
        xd_shl(q, 1);
        if (xd_ucmp(R, D) >= 0) {
            xd_usub(R, R, D);
            q[0] |= 1;
        }
        xd_shl(R, 1);
    }
    memcpy(r.m, q, sizeof(q));
    r.m[0] |= !(R[0] | R[1] | R[2] | R[3]) ? 0 : 1;
    r.e = qe - (n - 1);
    r.neg = a.neg != b.neg;
    return r;
}

/* the engine value for x87t_round: 113 bits truncated + the sign of the rest */
static X87TVal xd_val(X87D a)
{
    X87TVal r = { 0 };
    int bl = xd_bitlen(&a);
    uint64_t m[4];
    bool rest = false;
    int i;

    if (!bl) {
        r.v = make_float128(a.neg ? 0x8000000000000000ULL : 0, 0);
        return r;
    }
    memcpy(m, a.m, sizeof(m));
    if (bl > 113) {
        int sh = bl - 113;
        for (i = 0; i < sh; i++) {
            if ((m[i / 64] >> (i % 64)) & 1) {
                rest = true;
                break;
            }
        }
        {
            int w = sh / 64, b = sh % 64;
            for (i = 0; i < 4; i++) {
                uint64_t lo = i + w < 4 ? m[i + w] : 0;
                uint64_t hi = i + w + 1 < 4 ? m[i + w + 1] : 0;
                m[i] = b ? (lo >> b) | (hi << (64 - b)) : lo;
            }
        }
    } else {
        xd_shl(m, 113 - bl);
    }
    r.v = make_float128((a.neg ? 0x8000000000000000ULL : 0) | ((uint64_t)16383 << 48) |
                        (m[1] & 0x0000FFFFFFFFFFFFULL), m[0]);
    r.scale = bl - 1 + a.e;
    r.tail = rest ? (a.neg ? -1 : 1) : 0;
    return r;
}

/*
 * The microcode datapath (Goldmont MSROM, verified on the i5-13600K): FP uop
 * opcode bits [7:6] = 11 truncate to 67 bits (6e1 MUL, 6c9 ADD', 6e6 DIV),
 * 01 round to nearest-even at 64 bits (661 MUL', 649 ADD, 666 DIV), 10 is the
 * final operation: exact, then FCW.RC (x87t_round). The small trig path reads
 * the multiplier's second operand truncated to 64 bits (H64).
 */
static X87D M67(X87D a, X87D b)  { return xd_rnd(xd_mul(a, b), 67, 'z'); }
static X87D M64(X87D a, X87D b)  { return xd_rnd(xd_mul(a, b), 64, 'n'); }
static X87D A64(X87D a, X87D b)  { return xd_rnd(xd_add(a, b), 64, 'n'); }
static X87D A67(X87D a, X87D b)  { return xd_rnd(xd_add(a, b), 67, 'z'); }
static X87D H64(X87D b)          { return xd_rnd(b, 64, 'z'); }
static X87D M67h(X87D a, X87D b) { return M67(a, H64(b)); }
static X87D M64h(X87D a, X87D b) { return M64(a, H64(b)); }
#define XC(c) xd_c(&(c))

static const X87D XD_ONE = { { 1, 0, 0, 0 }, 0, false };

/* --- F2XM1 (U0a50: tiny / small U3fcd..U3ffe / table U6ad2..U6afc) ------------------------- */

X87TVal x87t_f2xm1(floatx80 x)
{
    X87D X = xd_x80(x), a = xd_abs(X), ln2 = XC(X87C_LN2);

    if (xd_ilog2(&X) < -68) {
        return xd_val(xd_mul(X, ln2));                          /* exact x ln2 */
    }
    if (xd_cmp(a, xd_int(1, -2)) < 0) {
        /* |x| < 1/4: 11-term series, two chains */
        X87D c[11], yl, y, z, t7, t6, t3;
        static const int k7s[4] = { 7, 5, 3, 1 };
        int i;

        for (i = 0; i < 11; i++) {
            c[i] = XC(X87C_EXP11[i]);
        }
        yl = M67(X, ln2);
        y = M64(X, ln2);
        z = M67(yl, y);
        t7 = M67(z, c[9]);
        t6 = M67(z, c[10]);
        for (i = 0; i < 4; i++) {
            t7 = A64(c[k7s[i]], t7);
            t6 = A64(c[k7s[i] + 1], t6);
            if (k7s[i] != 1) {
                t7 = M67(z, t7);
                t6 = M67(z, t6);
            }
        }
        t7 = M64(z, t7);
        t6 = M64(z, t6);
        t7 = M67(yl, t7);
        t6 = M67(z, t6);
        t3 = M67(z, c[0]);
        t7 = A64(t7, t6);
        t3 = A64(t3, t7);
        return xd_val(xd_add(yl, t3));
    }
    {
        /* table: n/128 nearest below |x| (odd n under 1/2, n = 66 + 4k above), 6-term series */
        int64_t n = xd_cmp(a, xd_int(1, -1)) < 0 ? xd_floor(a, 6) * 2 + 1
                                                  : 66 + 4 * (xd_floor(a, 5) - 16);
        X87D C[6], r, T = { { 0 } }, y, z, o, e, U, E, P;
        int i;

        for (i = 0; i < (int)ARRAY_SIZE(X87C_EXPN); i++) {
            if (X87C_EXPN[i] == n) {
                T = XC(X87C_EXPTAB[i][X.neg ? 1 : 0]);
                break;
            }
        }
        if (X.neg) {
            n = -n;
        }
        for (i = 0; i < 6; i++) {
            C[i] = XC(X87C_EXP6[i]);
        }
        r = xd_sub(X, xd_int(n, -7));
        y = M64(ln2, r);
        z = M67(y, y);
        o = M67(z, C[4]);
        e = M67(z, C[5]);
        o = A64(C[2], o);
        e = A64(C[3], e);
        o = M67(z, o);
        e = M67(z, e);
        o = A64(C[0], o);
        e = A64(C[1], e);
        o = M67(z, o);
        e = M67(z, e);
        o = A64(y, o);
        e = M67(e, y);
        U = A64(T, XD_ONE);
        E = A64(e, o);
        P = M67(U, E);
        return xd_val(xd_add(T, P));
    }
}

/* --- FPATAN (U00b0: tiny q < 2^-40, 6-coefficient path q < 3/64, table) ---------------------- */

static X87D xd_atan_core(X87D X, X87D Y, bool octant)
{
    X87D r, z, zz, a, b, at;
    int i;
    X87D x40 = X;

    x40.e -= 40;                        /* X * 2^-40 */
    if (xd_cmp(Y, x40) < 0) {
        return xd_rnd(xd_div(Y, X, 70), 67, 'z');               /* tiny: the truncated quotient */
    }
    if (xd_cmp(xd_mul(Y, xd_int(64, 0)), xd_mul(X, xd_int(3, 0))) < 0) {
        X87D A6[6];
        for (i = 0; i < 6; i++) {
            A6[i] = XC(X87C_ATAN6[i]);
        }
        r = xd_rnd(xd_div(Y, X, 70), 67, 'z');
        z = M64(r, r);
        zz = M67(z, z);
        a = M67(zz, A6[5]);
        b = M67(zz, A6[4]);
        a = A64(A6[3], a);
        b = A64(A6[2], b);
        a = M67(zz, a);
        b = M67(zz, b);
        a = A67(A6[1], a);
        b = A67(A6[0], b);
        at = xd_add(r, M67(M67(r, z), A64(M67(a, z), b)));
        return octant ? xd_rnd(at, 67, 'z') : at;
    }
    {
        /* n = round(32 q) (half up), c = n/32: atan q = atan c + atan((Y - cX) / (X + cY)) */
        int64_t n = xd_floor(xd_div(xd_add(xd_mul(Y, xd_int(64, 0)), X), xd_mul(X, xd_int(2, 0)), 20), 0);
        X87D c = xd_int(n, -5), num, den, A4[4];
        for (i = 0; i < 4; i++) {
            A4[i] = XC(X87C_ATAN4[i]);
        }
        num = xd_sub(Y, xd_mul(c, X));
        den = A67(M67(c, Y), X);
        r = xd_rnd(xd_div(num, den, 70), 67, 'z');
        z = M64(r, r);
        zz = M67(z, z);
        a = A67(A4[0], M67(zz, A4[2]));
        b = M67(z, A64(A4[1], M67(zz, A4[3])));
        at = A67(r, M67(M67(r, z), A64(a, b)));
        at = xd_add(XC(X87C_ATANTAB[n]), at);
        return octant ? xd_rnd(at, 67, 'z') : at;
    }
}

X87TVal x87t_fpatan(floatx80 y, floatx80 x)
{
    X87D X = xd_abs(xd_x80(x)), Y = xd_abs(xd_x80(y)), at;
    bool xneg = extractFloatx80Sign(x), swap = xd_cmp(X, Y) < 0;

    if (swap) {
        X87D t = X;
        X = Y;
        Y = t;
    }
    at = xd_atan_core(X, Y, swap || xneg);
    if (swap) {
        at = xd_sub(XC(X87C_PI2), at);
    }
    if (xneg) {
        at = xd_sub(XC(X87C_PI), at);
    }
    return xd_val(extractFloatx80Sign(y) ? xd_neg(at) : at);
}

/* |y / x| < 2^-40 shortcut (x > 0): the quotient truncated to 67 bits */
X87TVal x87t_quot(floatx80 y, floatx80 x)
{
    return xd_val(xd_rnd(xd_div(xd_x80(y), xd_x80(x), 70), 67, 'z'));
}

/* --- FYL2X / FYL2XP1 (U00c0 / U00c8: near-1 U6cb5, table U6e89) ------------------------------ */

/* log2 near 1: L = log2((1 + w') / (1 - w')) with w = 2 log2e num / den */
static X87D xd_log_near1(X87D num, X87D den)
{
    X87D B[6], k, w, z, zz, e, o, s;
    int i;

    for (i = 0; i < 6; i++) {
        B[i] = XC(X87C_LOGB[i]);
    }
    k = M67(XC(X87C_LOG2E2), num);
    w = xd_rnd(xd_div(k, xd_rnd(den, 67, 'z'), 70), 67, 'z');
    z = M64(w, H64(w));
    zz = M67(z, z);
    e = M67(zz, B[5]);
    o = M67(zz, B[4]);
    e = A64(B[3], e);
    o = A64(B[2], o);
    e = M67(zz, e);
    o = M67(zz, o);
    e = A64(B[1], e);
    o = A64(B[0], o);
    e = M67(zz, e);
    o = M67(z, o);
    s = A64(o, e);
    s = M67(w, s);
    return A67(w, s);
}

/* log2(M 2^m), M in [1, 2): table point F = 1 + n/64 (odd n), u = 2 (M - F) / (M + F) */
static X87D xd_log_table(X87D M, int32_t m)
{
    int64_t n = 2 * (xd_floor(M, 5) - 32) + 1;
    X87D F = xd_int(64 + n, -6), A[3], f, MF, u, lead, z, a, tail, s1, mt;
    int i;

    for (i = 0; i < 3; i++) {
        A[i] = XC(X87C_LOGA[i]);
    }
    f = A64(M, xd_neg(F));
    MF = A64(M, F);
    u = xd_rnd(xd_div(xd_mul(xd_int(2, 0), f), MF, 70), 64, 'n');
    lead = M67(XC(X87C_LOG2E), u);
    z = M67(u, u);
    a = M67(z, A[2]);
    a = A64(A[1], a);
    a = M67(z, a);
    a = A64(A[0], a);
    tail = M67(M67(z, a), u);
    s1 = A64(lead, tail);
    mt = A64(xd_int(m, 0), XC(X87C_LOGTOP[(n - 1) / 2]));
    return A67(mt, A67(XC(X87C_LOGBOT[(n - 1) / 2]), s1));
}

static X87D xd_log_split(X87D v)
{
    int32_t m = xd_ilog2(&v);
    X87D M = v;

    M.e -= m;
    return xd_log_table(M, m);
}

X87TVal x87t_fyl2x(floatx80 x, floatx80 y)
{
    X87D X = xd_x80(x), L;

    if (xd_cmp(X, xd_int(7, -3)) >= 0 && xd_cmp(X, xd_int(9, -3)) <= 0) {
        L = xd_log_near1(xd_sub(X, XD_ONE), xd_add(X, XD_ONE));
    } else {
        L = xd_log_split(X);
    }
    return xd_val(xd_mul(xd_x80(y), L));
}

X87TVal x87t_fyl2xp1(floatx80 x, floatx80 y)
{
    X87D X = xd_x80(x), L;
    int32_t ex = xd_ilog2(&X);

    if (ex < -70) {
        L = M67(XC(X87C_LOG2E), X);                             /* tiny (U5b6d) */
    } else if (ex >= -3) {
        L = xd_log_split(xd_rnd(xd_add(XD_ONE, X), 67, 'z'));   /* |x| >= 1/8: table on 6c9(x, 1) */
    } else {
        L = xd_log_near1(X, xd_add(xd_int(2, 0), X));
    }
    return xd_val(xd_mul(xd_x80(y), L));
}

/* --- FSIN / FCOS / FSINCOS / FPTAN (U0a60 / U00a8 / U00b8 / U0a58) --------------------------- */

/* x = N p/2 + r exactly, N = round-half-even(x / (p/2)), p = Intel's 66-bit pi */
static X87D xd_reduce(X87D x, uint64_t *quadrant)
{
    X87D a = xd_abs(x), p2 = XC(X87C_P2), q = xd_div(a, p2, 72), r;
    int64_t n = xd_floor(q, 0);
    X87D fr = xd_sub(q, xd_int(n, 0)), half = xd_int(1, -1);
    int c = xd_cmp(fr, half);

    if (c > 0 || (c == 0 && (n & 1))) {
        n++;
    }
    r = xd_sub(a, xd_mul(xd_int(n, 0), p2));
    *quadrant = (x.neg ? (uint64_t)0 - (uint64_t)n : (uint64_t)n) & 3;
    return x.neg ? xd_neg(r) : r;
}

static X87D xd_horner_h(const X87DC *co, X87D z)
{
    X87D t = M67h(z, XC(co[5]));
    int i;

    for (i = 4; i >= 0; i--) {
        t = A64(XC(co[i]), t);
        if (i) {
            t = M67h(z, t);
        }
    }
    return t;
}

/* small path |r| < 1/4: (s, c) before the quadrant; 'horner': the FSINCOS/FPTAN kernel */
static void xd_small(X87D r, bool horner, X87D *s, X87D *c, X87D *s67, X87D *c67)
{
    X87D a = xd_abs(r), z = M67h(a, a);

    if (!horner) {
        /* FSIN / FCOS: Estrin, E = c0 + z2 (c2 + z2 c4), O = c1 + z2 (c3 + z2 c5) */
        X87D z2 = M67h(z, z), E, O, S, T;
        const X87DC *k = X87C_SIN6;
        E = A64(XC(k[0]), M67h(z2, A64(XC(k[2]), M67h(z2, XC(k[4])))));
        O = A64(XC(k[1]), M67h(z2, A64(XC(k[3]), M67h(z2, XC(k[5])))));
        S = A64(M67h(z, E), M67h(z2, O));
        k = X87C_COS6;
        E = A64(XC(k[0]), M67h(z2, A64(XC(k[2]), M67h(z2, XC(k[4])))));
        O = A64(XC(k[1]), M67h(z2, A64(XC(k[3]), M67h(z2, XC(k[5])))));
        T = A67(M67h(z, E), M67h(z2, O));
        *s = xd_add(r, M67h(r, S));
        *c = xd_add(XD_ONE, T);
    } else {
        X87D zs = M64h(z, xd_horner_h(X87C_SIN6, z)), zc = M67h(z, xd_horner_h(X87C_COS6, z));
        *s = xd_add(r, M67h(r, zs));
        *c = xd_add(XD_ONE, zc);
        if (s67) {
            *s67 = A67(a, M67h(a, zs));
            if (r.neg) {
                *s67 = xd_neg(*s67);
            }
            *c67 = A67(XD_ONE, zc);
        }
    }
}

/* table path 1/4 <= |r| <= p/4: (sin, cos) of a = |r| around the table point n/64 */
static void xd_table(X87D a, X87D *s, X87D *c)
{
    int64_t n = xd_cmp(a, xd_int(1, -1)) < 0 ? 18 + 4 * (xd_floor(a, 4) - 4)
                                              : 36 + 8 * (xd_floor(a, 3) - 4);
    X87D S = { { 0 } }, C = { { 0 } }, d, z, t, u, cd1, tail, sd;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE(X87C_SCN); i++) {
        if (X87C_SCN[i] == n) {
            S = XC(X87C_SCTAB[i][0]);
            C = XC(X87C_SCTAB[i][1]);
            break;
        }
    }
    d = xd_sub(a, xd_int(n, -6));
    z = M67(d, d);
    t = M67(z, XC(X87C_SIN4[3]));
    u = M67(z, XC(X87C_COS4[3]));
    for (i = 2; i >= 0; i--) {
        t = A64(XC(X87C_SIN4[i]), t);
        u = A64(XC(X87C_COS4[i]), u);
        if (i) {
            t = M67(z, t);
            u = M67(z, u);
        }
    }
    t = M67(z, t);
    cd1 = M64(z, u);
    tail = M67(t, d);
    sd = A64(d, tail);
    *c = xd_add(C, A67(M67(xd_neg(S), sd), M67(C, cd1)));
    *s = xd_add(S, A67(M67(C, sd), M67(S, cd1)));
}

/* sin and cos of x after the quadrant; table path also gives them for FPTAN */
static void xd_sincos(floatx80 x, bool horner, X87D *sn, X87D *cs, uint64_t *quadrant, bool *small)
{
    uint64_t k;
    X87D r = xd_reduce(xd_x80(x), &k), s, c, t;

    *small = xd_cmpabs(r, xd_int(1, -2)) < 0;
    if (*small) {
        xd_small(r, horner, &s, &c, NULL, NULL);
    } else {
        xd_table(xd_abs(r), &s, &c);
        if (r.neg) {
            s = xd_neg(s);
        }
    }
    switch (k) {
    case 0:  break;
    case 1:  t = s; s = c; c = xd_neg(t); break;
    case 2:  s = xd_neg(s); c = xd_neg(c); break;
    default: t = s; s = xd_neg(c); c = t; break;
    }
    *sn = s;
    *cs = c;
    *quadrant = k;
}

/* sincos: the FSINCOS kernel (Horner); otherwise FSIN / FCOS (Estrin) */
void x87t_trig(floatx80 x, bool sincos, X87TVal *sn, X87TVal *cs)
{
    X87D s, c;
    uint64_t k;
    bool small;

    xd_sincos(x, sincos, &s, &c, &k, &small);
    *sn = xd_val(s);
    *cs = xd_val(c);
}

X87TVal x87t_tan(floatx80 x)
{
    uint64_t k;
    X87D r = xd_reduce(xd_x80(x), &k), s, c, s67, c67, t;

    if (xd_cmpabs(r, xd_int(1, -2)) < 0) {
        xd_small(r, true, &s, &c, &s67, &c67);
        t = (k & 1) ? xd_neg(xd_div(c67, s67, 130)) : xd_div(s67, c67, 130);
        return xd_val(t);
    }
    {
        bool small;
        xd_sincos(x, true, &s, &c, &k, &small);
        return xd_val(xd_div(xd_rnd(s, 67, 'z'), xd_rnd(c, 67, 'z'), 130));
    }
}
#endif /* __Use_Original_Qemu (U53-U54) */
