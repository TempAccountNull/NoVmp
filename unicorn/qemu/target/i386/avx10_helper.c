/*
 * Intel AVX10.2 instruction helpers (NoVmp, ledgers U372-U399; AVX10.2 architecture
 * specification 361050-007 rev 7.0, chapters 5, 7, 8, 11 and 12).
 *
 * Every helper computes the full-length result of one instruction into its destination
 * pointer (or returns an opmask / EFLAGS value); merging/zeroing-masking, {1toN} broadcast,
 * masked memory and the MXCSR/#XM frame ({er}/{sae}) are done by the EVEX engine
 * (emit.c.inc gen_evex_insn, U146/U147). The destination may alias any source: every
 * element is computed from the same-index source elements before it is written.
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U372-U399): whole file */
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "avx10_helper.h"

/* ---------------------------------------------------------------------------------------
 * BF16 (spec chapter 7): 1 sign, 8 exponent, 7 fraction bits. The arithmetic does not
 * consult or update MXCSR and raises no exception: denormal inputs are zeros (DAZ),
 * denormal results are flushed to zero (FTZ: tiny after rounding, as x86 detects
 * underflow, SDM Vol1 10.2.3.3), rounding is always RNE ("RoundFPControl_RNE", "//DAZ,
 * FTZ, RNE, SAE"). NaN operands are returned quietened, the first one in operand order
 * (SDM Vol1 Table 4-7); invalid operations return the QNaN indefinite FFC0h.
 * --------------------------------------------------------------------------------------- */
#define BF16_SIGN       0x8000
#define BF16_EXP        0x7f80
#define BF16_FRAC       0x007f
#define BF16_QBIT       0x0040
#define BF16_INF        0x7f80
#define BF16_ONE        0x3f80
#define BF16_INDEFINITE 0xffc0

static inline bool bf16_nan(uint16_t a)
{
    return (a & BF16_EXP) == BF16_EXP && (a & BF16_FRAC);
}

static inline bool bf16_inf(uint16_t a)
{
    return (a & 0x7fff) == BF16_INF;
}

/* zero exponent field: zero or denormal */
static inline bool bf16_zero_or_denormal(uint16_t a)
{
    return !(a & BF16_EXP);
}

static inline uint16_t bf16_daz(uint16_t a)
{
    return bf16_zero_or_denormal(a) ? (a & BF16_SIGN) : a;
}

static inline uint16_t bf16_quiet(uint16_t a)
{
    return a | BF16_QBIT;
}

/* RNE, DAZ, FTZ (after rounding), x86 NaN encoding, flags discarded by the caller */
static void bf16_status(CPUX86State *env, float_status *st)
{
    *st = env->sse_status;
    set_float_rounding_mode(float_round_nearest_even, st);
    set_flush_to_zero(true, st);
    set_flush_inputs_to_zero(true, st);
    set_float_ftz_detection(float_ftz_after_rounding, st);
    set_default_nan_mode(false, st);
    set_float_exception_flags(0, st);
}

/* exact arithmetic on float64 (RNE, no DAZ/FTZ): BF16 values and their scalings are exact */
static void f64_exact_status(CPUX86State *env, float_status *st)
{
    *st = env->sse_status;
    set_float_rounding_mode(float_round_nearest_even, st);
    set_flush_to_zero(false, st);
    set_flush_inputs_to_zero(false, st);
    set_default_nan_mode(false, st);
    set_float_exception_flags(0, st);
}

/* the relation of two DAZ'd BF16 values: 0 LT, 1 EQ, 2 GT, 3 unordered */
static int bf16_relation(uint16_t a, uint16_t b, float_status *st)
{
    switch (bfloat16_compare_quiet(bf16_daz(a), bf16_daz(b), st)) {
    case float_relation_less:
        return 0;
    case float_relation_equal:
        return 1;
    case float_relation_greater:
        return 2;
    default:
        return 3;
    }
}

/* VGETEXPBF16 (7.7.3 getexp_bf16): floor(log2|x|); the pseudocode's literal paths */
static uint16_t bf16_getexp(uint16_t a, float_status *st)
{
    if (bf16_nan(a)) {
        return bf16_quiet(a);
    }
    if (a == BF16_INF) {
        return BF16_INF;                                /* "src is positive infinity" */
    }
    if (bf16_zero_or_denormal(a)) {
        return BF16_SIGN | BF16_INF;                    /* -INF (DAZ) */
    }
    /*
     * "tmp := ((src & 0x7F80) >> 7) - 127": every other input, -INF included (-> 128.0;
     * only +INF is special-cased by the pseudocode)
     */
    return int32_to_bfloat16((int32_t)((a & BF16_EXP) >> 7) - 127, st);
}

/* VGETMANTBF16 (7.8.3 getmant_bf16), sc = imm8[3:2], interv = imm8[1:0] */
static uint16_t bf16_getmant(uint16_t a, int sc, int interv)
{
    uint16_t sign = (sc & 1) ? 0 : (a & BF16_SIGN);
    uint16_t signed_one = (sc & 1) ? BF16_ONE : (BF16_SIGN | BF16_ONE);
    int exp = (a & BF16_EXP) >> 7, frac = a & BF16_FRAC;
    int unbiased, bexp;

    if (bf16_nan(a)) {
        return bf16_quiet(a);
    }
    if (a == 0 || a == BF16_INF) {                      /* +0 or +INF */
        return BF16_ONE;
    }
    if (a & BF16_SIGN) {
        if ((a & 0x7fff) == 0) {
            return signed_one;
        }
        if (bf16_inf(a)) {
            return (sc & 2) ? BF16_INDEFINITE : signed_one;
        }
        if (sc & 2) {
            return BF16_INDEFINITE;
        }
    }
    if (exp == 0) {                                     /* denormal: fraction := 0 */
        frac = 0;
    }
    unbiased = exp - 127;
    switch (interv) {
    case 0:
        bexp = 127;
        break;
    case 1:
        bexp = (unbiased & 1) ? 126 : 127;
        break;
    case 2:
        bexp = 126;
        break;
    default:
        bexp = (frac & 0x40) ? 126 : 127;
        break;
    }
    return sign | (bexp << 7) | frac;
}

/*
 * VREDUCEBF16 (7.13.3 reduce_bf16_ne): src - 2^-m * ROUND(2^m * src, RNE) (DAZ), the
 * difference rounded RNE with FTZ; imm8[3:0] is not used (always RNE). The scaled and
 * rounded values and the difference are exact in float64 (8 significant bits, m <= 15);
 * +-INF gives INF - INF = QNaN indefinite.
 */
static uint16_t bf16_reduce(CPUX86State *env, uint16_t a, int m, float_status *st)
{
    float_status ex;
    float64 x, t;

    if (bf16_nan(a)) {
        return bf16_quiet(a);
    }
    if (bf16_inf(a)) {
        return BF16_INDEFINITE;
    }
    f64_exact_status(env, &ex);
    x = bfloat16_to_float64(bf16_daz(a), &ex);
    t = float64_scalbn(float64_round_to_int(float64_scalbn(x, m, &ex), &ex), -m, &ex);
    return float64_to_bfloat16(float64_sub(x, t, &ex), st);
}

/* VRNDSCALEBF16 (7.14.3): 2^-m * ROUND_TO_NEAREST_EVEN_INTEGER(2^m * src), DAZ, FTZ */
static uint16_t bf16_rndscale(CPUX86State *env, uint16_t a, int m, float_status *st)
{
    float_status ex;
    float64 x;

    if (bf16_nan(a)) {
        return bf16_quiet(a);
    }
    f64_exact_status(env, &ex);
    x = bfloat16_to_float64(bf16_daz(a), &ex);
    x = float64_scalbn(float64_round_to_int(float64_scalbn(x, m, &ex), &ex), -m, &ex);
    return float64_to_bfloat16(x, st);
}

/*
 * VSCALEFBF16 (7.16.3 scale_bf16): src1 * 2^floor(src2), denormals of both as zeros, the
 * product rounded RNE with FTZ (one rounding: bfloat16_scalbn). Infinite src2 multiplies
 * by +INF / +0: 0 * 2^+INF and INF * 2^-INF are invalid (QNaN indefinite).
 */
static uint16_t bf16_scalef(uint16_t a, uint16_t b, float_status *st)
{
    float_status ex = *st;
    float64 fb;
    int n;

    if (bf16_nan(a)) {
        return bf16_quiet(a);
    }
    if (bf16_nan(b)) {
        return bf16_quiet(b);
    }
    a = bf16_daz(a);
    b = bf16_daz(b);
    if (bf16_inf(b)) {
        if (!(b & BF16_SIGN)) {
            return (a & 0x7fff) == 0 ? BF16_INDEFINITE : ((a & BF16_SIGN) | BF16_INF);
        }
        return bf16_inf(a) ? BF16_INDEFINITE : (a & BF16_SIGN);
    }
    if (bf16_inf(a) || (a & 0x7fff) == 0) {
        return a;
    }
    set_flush_inputs_to_zero(false, &ex);
    set_flush_to_zero(false, &ex);
    set_float_rounding_mode(float_round_down, &ex);
    fb = float64_round_to_int(bfloat16_to_float64(b, &ex), &ex);     /* floor */
    if (float64_lt(fb, float64_scalbn(float64_one, 10, &ex), &ex) &&
        float64_lt(float64_chs(float64_scalbn(float64_one, 10, &ex)), fb, &ex)) {
        n = (int)float64_to_int32(fb, &ex);
    } else {
        n = float64_is_neg(fb) ? -1024 : 1024;          /* beyond every finite result */
    }
    return bfloat16_scalbn(a, n, st);
}

/*
 * VRCPBF16 / VRSQRTBF16 (7.12, 7.15): the spec only bounds the relative error (< 2^-8 +
 * 2^-14) and lists special cases (Tables 7.2/7.3). We return the correctly rounded (RNE)
 * BF16 value after DAZ, with FTZ, which meets the bound and every special case: +-0 ->
 * +-INF, +INF -> +0, -INF -> -0 (RCP), x < 0 -> QNaN indefinite (RSQRT), 2^-n -> 2^n.
 * RSQRT: the float64 1/sqrt(x) (two RNE roundings) rounds to the same BF16 as the exact
 * value for every one of the 32512 positive normal BF16 inputs (checked exhaustively
 * against the exact model of Emulator/tools/isa/ref_avx10_a.py).
 */
static uint16_t bf16_rcp(uint16_t a, float_status *st)
{
    if (bf16_nan(a)) {
        return bf16_quiet(a);
    }
    return bfloat16_div(BF16_ONE, bf16_daz(a), st);
}

static uint16_t bf16_rsqrt(CPUX86State *env, uint16_t a, float_status *st)
{
    float_status ex;
    float64 x;

    if (bf16_nan(a)) {
        return bf16_quiet(a);
    }
    a = bf16_daz(a);
    if ((a & 0x7fff) == 0) {
        return (a & BF16_SIGN) | BF16_INF;
    }
    if (a & BF16_SIGN) {
        return BF16_INDEFINITE;
    }
    if (a == BF16_INF) {
        return 0;
    }
    f64_exact_status(env, &ex);
    x = float64_div(float64_one, float64_sqrt(bfloat16_to_float64(a, &ex), &ex), &ex);
    return float64_to_bfloat16(x, st);
}

/* VMAXBF16 / VMINBF16 (7.9.3/7.10.3): both zero, or either NaN -> SRC2 (unchanged NaN) */
static uint16_t bf16_minmax_legacy(uint16_t a, uint16_t b, bool max, float_status *st)
{
    int r;

    a = bf16_daz(a);
    b = bf16_daz(b);
    if (((a | b) & 0x7fff) == 0 || bf16_nan(a) || bf16_nan(b)) {
        return b;
    }
    r = bf16_relation(a, b, st);
    return (max ? r == 2 : r == 0) ? a : b;
}

void helper_avx10_bf16(CPUX86State *env, ZMMReg *d, ZMMReg *s1, ZMMReg *s2, uint32_t desc)
{
    int op = AVX10_DESC_OP(desc), imm = AVX10_DESC_IMM(desc), n = AVX10_DESC_N(desc), i;
    float_status st;

    bf16_status(env, &st);
    for (i = 0; i < n; i++) {
        uint16_t a = s1->ZMM_W(i), b = s2->ZMM_W(i), r;

        switch (op) {
        case AVX10_BF16_ADD:
        case AVX10_BF16_SUB:
        case AVX10_BF16_MUL:
        case AVX10_BF16_DIV:
            if (bf16_nan(a)) {
                r = bf16_quiet(a);
            } else if (bf16_nan(b)) {
                r = bf16_quiet(b);
            } else {
                a = bf16_daz(a);
                b = bf16_daz(b);
                r = op == AVX10_BF16_ADD ? bfloat16_add(a, b, &st)
                  : op == AVX10_BF16_SUB ? bfloat16_sub(a, b, &st)
                  : op == AVX10_BF16_MUL ? bfloat16_mul(a, b, &st)
                  : bfloat16_div(a, b, &st);
            }
            break;
        case AVX10_BF16_MIN:
        case AVX10_BF16_MAX:
            r = bf16_minmax_legacy(a, b, op == AVX10_BF16_MAX, &st);
            break;
        case AVX10_BF16_SCALEF:
            r = bf16_scalef(a, b, &st);
            break;
        case AVX10_BF16_SQRT:
            r = bf16_nan(b) ? bf16_quiet(b) : bfloat16_sqrt(bf16_daz(b), &st);
            break;
        case AVX10_BF16_RCP:
            r = bf16_rcp(b, &st);
            break;
        case AVX10_BF16_RSQRT:
            r = bf16_rsqrt(env, b, &st);
            break;
        case AVX10_BF16_GETEXP:
            r = bf16_getexp(b, &st);
            break;
        case AVX10_BF16_GETMANT:
            r = bf16_getmant(b, (imm >> 2) & 3, imm & 3);
            break;
        case AVX10_BF16_REDUCE:
            r = bf16_reduce(env, b, (imm >> 4) & 15, &st);
            break;
        case AVX10_BF16_RNDSCALE:
            r = bf16_rndscale(env, b, (imm >> 4) & 15, &st);
            break;
        default:
            g_assert_not_reached();
        }
        d->ZMM_W(i) = r;
    }
}

/*
 * VF[N]M{ADD,SUB}{132,213,231}BF16 (7.5.3): a * b OP c with one rounding (RNE, DAZ, FTZ);
 * a, b, c are chosen by the caller from the form. The negated forms negate a ("a := -a",
 * also the sign of a NaN a), the SUB forms subtract c. NaN priority a, b, c (quietened).
 */
void helper_avx10_bf16_fma(CPUX86State *env, ZMMReg *d, ZMMReg *pa, ZMMReg *pb, ZMMReg *pc,
                           uint32_t desc)
{
    int n = AVX10_DESC_N(desc), i;
    bool neg = desc & AVX10_FMA_NEG, sub = desc & AVX10_FMA_SUB;
    float_status st;

    bf16_status(env, &st);
    for (i = 0; i < n; i++) {
        uint16_t a = pa->ZMM_W(i), b = pb->ZMM_W(i), c = pc->ZMM_W(i), r;

        if (neg) {
            a ^= BF16_SIGN;
        }
        if (bf16_nan(a)) {
            r = bf16_quiet(a);
        } else if (bf16_nan(b)) {
            r = bf16_quiet(b);
        } else if (bf16_nan(c)) {
            r = bf16_quiet(c);
        } else {
            r = bfloat16_muladd(bf16_daz(a), bf16_daz(b), bf16_daz(c),
                                sub ? float_muladd_negate_c : 0, &st);
        }
        d->ZMM_W(i) = r;
    }
}

/*
 * VCMPBF16 (7.2.3; imm8[4:0] predicate, DAZ, no exception) and VFPCLASSBF16 (7.6.3) into
 * an opmask: bit j per element, bits n..63 zero; the AND with k2 is the engine's.
 */
static bool bf16_fpclass(uint16_t a, int imm)
{
    bool neg = a & BF16_SIGN;
    bool exp_ones = (a & BF16_EXP) == BF16_EXP;
    bool exp_zeros = (a & BF16_EXP) == 0;
    bool mant_zeros = exp_zeros || !(a & BF16_FRAC);    /* exponent 0: mantissa as zero */
    bool qbit = a & BF16_QBIT;
    bool r = false;

    r |= (imm & 0x01) && exp_ones && !mant_zeros && qbit;           /* QNaN */
    r |= (imm & 0x02) && !neg && exp_zeros && mant_zeros;           /* +0 */
    r |= (imm & 0x04) && neg && exp_zeros && mant_zeros;            /* -0 */
    r |= (imm & 0x08) && !neg && exp_ones && mant_zeros;            /* +INF */
    r |= (imm & 0x10) && neg && exp_ones && mant_zeros;             /* -INF */
    r |= (imm & 0x20) && exp_zeros && !mant_zeros;                  /* denormal: never */
    r |= (imm & 0x40) && neg && !exp_ones && !(exp_zeros && mant_zeros); /* finite < 0 */
    r |= (imm & 0x80) && exp_ones && !mant_zeros && !qbit;          /* SNaN */
    return r;
}

/* truth table of the 16 base predicates over (LT, EQ, GT, UNORDERED) = bits 0..3 */
static const uint8_t avx10_cmp_truth[16] = {
    0x2, 0x1, 0x3, 0x8, 0xd, 0xe, 0xc, 0x7, 0xa, 0x9, 0xb, 0x0, 0x5, 0x6, 0x4, 0xf,
};

uint64_t helper_avx10_bf16_k(CPUX86State *env, ZMMReg *s1, ZMMReg *s2, uint32_t desc)
{
    int op = AVX10_DESC_OP(desc), imm = AVX10_DESC_IMM(desc), n = AVX10_DESC_N(desc), i;
    float_status st;
    uint64_t k = 0;

    bf16_status(env, &st);
    for (i = 0; i < n; i++) {
        bool bit;

        if (op == AVX10_BF16_CMP) {
            int rel = bf16_relation(s1->ZMM_W(i), s2->ZMM_W(i), &st);
            bit = (avx10_cmp_truth[imm & 15] >> rel) & 1;
        } else {
            bit = bf16_fpclass(s2->ZMM_W(i), imm);
        }
        k |= (uint64_t)bit << i;
    }
    return k;
}

/* VCOMISBF16 (7.3.3): ZF/PF/CF from the DAZ'd comparison, OF/SF/AF 0; no exception */
void helper_avx10_vcomisbf16(CPUX86State *env, ZMMReg *s1, ZMMReg *s2)
{
    static const uint32_t fl[4] = { CC_C, CC_Z, 0, CC_Z | CC_P | CC_C };
    float_status st;

    bf16_status(env, &st);
    env->cc_src = fl[bf16_relation(s1->ZMM_W(0), s2->ZMM_W(0), &st)];
}

/* ---------------------------------------------------------------------------------------
 * Bit-level views of the four element formats (BF16, FP16, FP32, FP64) for MINMAX (spec
 * 5.2) and VCOMX (chapter 8): AVX10_FMT_* = desc operation of those helpers.
 * --------------------------------------------------------------------------------------- */
typedef struct Avx10Fmt {
    int ebits, fbits, bytes;
} Avx10Fmt;

static const Avx10Fmt avx10_fmt[4] = {
    { 8, 7, 2 },        /* AVX10_FMT_BF16 */
    { 5, 10, 2 },       /* AVX10_FMT_FP16 */
    { 8, 23, 4 },       /* AVX10_FMT_FP32 */
    { 11, 52, 8 },      /* AVX10_FMT_FP64 */
};

static inline uint64_t fmt_sign(const Avx10Fmt *f)
{
    return 1ull << (f->ebits + f->fbits);
}

static inline uint64_t fmt_mag(const Avx10Fmt *f, uint64_t x)
{
    return x & (fmt_sign(f) - 1);
}

static inline uint64_t fmt_exp(const Avx10Fmt *f, uint64_t x)
{
    return (x >> f->fbits) & ((1ull << f->ebits) - 1);
}

static inline uint64_t fmt_frac(const Avx10Fmt *f, uint64_t x)
{
    return x & ((1ull << f->fbits) - 1);
}

static inline bool fmt_nan(const Avx10Fmt *f, uint64_t x)
{
    return fmt_exp(f, x) == (1ull << f->ebits) - 1 && fmt_frac(f, x);
}

static inline bool fmt_snan(const Avx10Fmt *f, uint64_t x)
{
    return fmt_nan(f, x) && !((x >> (f->fbits - 1)) & 1);
}

static inline bool fmt_qnan(const Avx10Fmt *f, uint64_t x)
{
    return fmt_nan(f, x) && ((x >> (f->fbits - 1)) & 1);
}

static inline bool fmt_denormal(const Avx10Fmt *f, uint64_t x)
{
    return fmt_exp(f, x) == 0 && fmt_frac(f, x);
}

static inline bool fmt_zero(const Avx10Fmt *f, uint64_t x)
{
    return fmt_mag(f, x) == 0;
}

static inline uint64_t fmt_quiet(const Avx10Fmt *f, uint64_t x)
{
    return x | (1ull << (f->fbits - 1));
}

/* a <= b / a < b for numbers (no NaN); +0 and -0 compare equal */
static bool fmt_le(const Avx10Fmt *f, uint64_t a, uint64_t b)
{
    bool sa = a & fmt_sign(f), sb = b & fmt_sign(f);
    uint64_t ma = fmt_mag(f, a), mb = fmt_mag(f, b);

    if (ma == 0 && mb == 0) {
        return true;
    }
    if (sa != sb) {
        return sa;
    }
    return sa ? ma >= mb : ma <= mb;
}

static bool fmt_lt(const Avx10Fmt *f, uint64_t a, uint64_t b)
{
    return !fmt_le(f, b, a);
}

static uint64_t avx10_get(ZMMReg *r, int bytes, int i)
{
    switch (bytes) {
    case 2:
        return r->ZMM_W(i);
    case 4:
        return r->ZMM_L(i);
    default:
        return r->ZMM_Q(i);
    }
}

static void avx10_set(ZMMReg *r, int bytes, int i, uint64_t v)
{
    switch (bytes) {
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

/* ---- MINMAX (spec 5.2, Figures 5.1-5.9; chapter 11; U374) ---- */
static uint64_t mm_minimum(const Avx10Fmt *f, uint64_t a, uint64_t b)
{
    if (fmt_snan(f, a) || (fmt_qnan(f, a) && !fmt_snan(f, b))) {
        return fmt_quiet(f, a);
    }
    if (fmt_nan(f, b)) {
        return fmt_quiet(f, b);
    }
    if (fmt_zero(f, a) && fmt_zero(f, b) && ((a ^ b) & fmt_sign(f))) {
        return fmt_sign(f);                             /* -0.0 */
    }
    return fmt_le(f, a, b) ? a : b;
}

static uint64_t mm_maximum(const Avx10Fmt *f, uint64_t a, uint64_t b)
{
    if (fmt_snan(f, a) || (fmt_qnan(f, a) && !fmt_snan(f, b))) {
        return fmt_quiet(f, a);
    }
    if (fmt_nan(f, b)) {
        return fmt_quiet(f, b);
    }
    if (fmt_zero(f, a) && fmt_zero(f, b) && ((a ^ b) & fmt_sign(f))) {
        return 0;                                       /* +0.0 */
    }
    return fmt_le(f, b, a) ? a : b;                     /* a >= b */
}

/* the "Number" variants: both NaN -> QNAN(a) (QNAN(b) if only b is signaling), one NaN ->
   the other operand, else the plain operation */
static bool mm_number_nan(const Avx10Fmt *f, uint64_t a, uint64_t b, uint64_t *r)
{
    if (fmt_nan(f, a) && fmt_nan(f, b)) {
        *r = (fmt_snan(f, a) || (fmt_qnan(f, a) && fmt_qnan(f, b))) ? fmt_quiet(f, a)
                                                                  : fmt_quiet(f, b);
        return true;
    }
    if (fmt_nan(f, a)) {
        *r = b;
        return true;
    }
    if (fmt_nan(f, b)) {
        *r = a;
        return true;
    }
    return false;
}

static uint64_t mm_minmax(const Avx10Fmt *f, uint64_t a, uint64_t b, int imm, bool daz,
                          bool except, float_status *st)
{
    int op = imm & 3, sc = (imm >> 2) & 3;
    bool number = imm & 0x10;
    uint64_t t;

    if (daz) {
        if (fmt_denormal(f, a)) {
            a &= fmt_sign(f);                           /* a.fraction := 0 */
        }
        if (fmt_denormal(f, b)) {
            b &= fmt_sign(f);
        }
    }
    if (except) {
        if (fmt_snan(f, a) || fmt_snan(f, b)) {
            float_raise(float_flag_invalid, st);
        } else if (fmt_qnan(f, a) || fmt_qnan(f, b)) {
            /* a QNaN prevents the lower-priority exceptions (SDM Vol3A Table 6-8) */
        } else if (fmt_denormal(f, a) || fmt_denormal(f, b)) {
            float_raise(float_flag_input_denormal_used, st);
        }
    }

    if (number && mm_number_nan(f, a, b, &t)) {
        /* minimumNumber & co. with a NaN operand */
    } else if (op == 0) {
        t = mm_minimum(f, a, b);
    } else if (op == 1) {
        t = mm_maximum(f, a, b);
    } else if (fmt_nan(f, a) || fmt_nan(f, b)) {
        t = mm_minimum(f, a, b);                        /* the NaN paths of the magnitude ops */
    } else if (fmt_mag(f, a) != fmt_mag(f, b)) {
        bool a_smaller = fmt_mag(f, a) < fmt_mag(f, b);
        t = (op == 2) == a_smaller ? a : b;
    } else {
        t = op == 2 ? mm_minimum(f, a, b) : mm_maximum(f, a, b);
    }

    if (!fmt_nan(f, t)) {
        if (sc == 3) {
            t |= fmt_sign(f);
        } else if (sc == 2) {
            t &= ~fmt_sign(f);
        } else if (sc == 0 && !fmt_nan(f, a)) {
            t = (t & ~fmt_sign(f)) | (a & fmt_sign(f));
        }
    }
    return t;
}

/*
 * VMINMAXBF16/PH/PS/PD and VMINMAXSH/SS/SD (chapter 11): minmax(src1, src2, imm8, daz,
 * except) per element; BF16 daz = true, except = false (no MXCSR); PH/SH daz = false;
 * PS/PD/SS/SD daz = MXCSR.DAZ; IE/DE go to the MXCSR flags of the engine's #XM frame.
 * Scalar forms (AVX10_MINMAX_SCALAR): bits 127:esz from SRC1, taken from the register
 * itself (s1reg; s1 may be the engine's neutral-lane copy).
 */
void helper_avx10_minmax(CPUX86State *env, ZMMReg *d, ZMMReg *s1, ZMMReg *s2, ZMMReg *s1reg,
                         uint32_t desc)
{
    int fmt = AVX10_DESC_OP(desc), imm = AVX10_DESC_IMM(desc), n = AVX10_DESC_N(desc), i;
    const Avx10Fmt *f = &avx10_fmt[fmt];
    bool except = fmt != AVX10_FMT_BF16;
    bool daz = fmt == AVX10_FMT_BF16 ||
               (fmt != AVX10_FMT_FP16 && (env->mxcsr & 0x40));
    ZMMReg r;

    if (desc & AVX10_MINMAX_SCALAR) {
        r = *s1reg;
        n = 1;
    }
    for (i = 0; i < n; i++) {
        avx10_set(&r, f->bytes, i,
                  mm_minmax(f, avx10_get(s1, f->bytes, i), avx10_get(s2, f->bytes, i), imm,
                            daz, except, &env->sse_status));
    }
    if (desc & AVX10_MINMAX_SCALAR) {
        memcpy(d, &r, 16);
    } else {
        memcpy(d, &r, (size_t)n * f->bytes);
    }
}

/* ---- VCOMX / VUCOMX SS, SD, SH (chapter 8; U375) ---- */
/*
 * OrderedCompare of the low elements; OF,SF,ZF,PF,CF := UNORDERED 11011, GREATER 00000,
 * LESS 10001, EQUAL 11100; AF := 0. IE: VCOMX* on any NaN, VUCOMX* on an SNaN; DE: a
 * denormal operand and no NaN. SS/SD honour MXCSR.DAZ (no DE then), SH never does.
 */
void helper_avx10_vcomx(CPUX86State *env, ZMMReg *s1, ZMMReg *s2, uint32_t desc)
{
    int fmt = AVX10_DESC_OP(desc);
    const Avx10Fmt *f = &avx10_fmt[fmt];
    bool signaling = desc & AVX10_VCOMX_SIGNALING;
    uint64_t a = avx10_get(s1, f->bytes, 0), b = avx10_get(s2, f->bytes, 0);
    uint32_t fl;

    if (fmt != AVX10_FMT_FP16 && (env->mxcsr & 0x40)) {
        if (fmt_denormal(f, a)) {
            a &= fmt_sign(f);
        }
        if (fmt_denormal(f, b)) {
            b &= fmt_sign(f);
        }
    }
    if (fmt_nan(f, a) || fmt_nan(f, b)) {
        if (signaling || fmt_snan(f, a) || fmt_snan(f, b)) {
            float_raise(float_flag_invalid, &env->sse_status);
        }
        fl = CC_O | CC_S | CC_P | CC_C;
    } else {
        if (fmt_denormal(f, a) || fmt_denormal(f, b)) {
            float_raise(float_flag_input_denormal_used, &env->sse_status);
        }
        if (fmt_lt(f, b, a)) {
            fl = 0;
        } else if (fmt_lt(f, a, b)) {
            fl = CC_O | CC_C;
        } else {
            fl = CC_O | CC_S | CC_Z;
        }
    }
    env->cc_src = fl;
}

#endif /* __Use_Original_Qemu (U372-U399) */
