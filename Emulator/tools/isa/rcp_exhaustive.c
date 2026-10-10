/*
 * rcp_exhaustive.c (decision A9, U940-U959): exhaustive / large-sample check of the emulator's
 * approximate reciprocal instructions against an INDEPENDENT exact-integer model of the documented
 * stand-in (correctly rounded 1/x, 1/sqrt(x); see docs/reciprocal.md and ref_rcp.py), and against
 * the documented error bounds and special-case tables. Only Unicorn executes the instructions
 * (the i5-13600K has no AVX-512); nothing runs natively except this checker.
 *
 *   VRCP14PS / VRSQRT14PS   every one of the 2^32 FP32 encodings x MXCSR {1F80, DAZ, FTZ, DAZ+FTZ, RZ+unmasked}
 *   VRCP14PD / VRSQRT14PD   N random FP64 encodings (all exponents) + every exponent's edge mantissas x 4 MXCSR
 *   VRCPPH / VRSQRTPH       every FP16 encoding x 3 MXCSR (MXCSR must not matter)
 *   VRCPBF16 / VRSQRTBF16   every BF16 encoding x 3 MXCSR (AVX10.2)
 *
 * Build (x64 Native Tools prompt, from the worktree root, after build.cmd --release):
 *   cl /nologo /O2 /W3 /Iunicorn\include Emulator\tools\isa\rcp_exhaustive.c build\x64\Release\unicorn.lib
 * Run: rcp_exhaustive.exe [fp32] [fp64 N] [f16] [bf16]      (no argument: everything, fp64 N = 2^24)
 *      rcp_exhaustive.exe fp32one KIND MXCSR                 (one FP32 sweep: KIND 0 RCP14, 1 RSQRT14)
 * Exit status 0 when every element matches the model and every documented requirement holds.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <intrin.h>
#include <math.h>
#include <unicorn/unicorn.h>

#define CODE 0x10000ULL
#define DATA_IN 0x1000000ULL
#define DATA_OUT 0x3000000ULL
#define BLOCK (1u << 21) /* bytes per uc_emu_start (32768 zmm vectors) */

static uint64_t g_fail, g_checked;

/* ------------------------------------------------------------------ independent model */
typedef struct {
    int bits, p, ebits, bias, emin, emax, q;
    uint64_t sign, emask, fmask, qbit, indef;
} Fmt;

static Fmt mkfmt(int bits, int p, int ebits)
{
    Fmt f;
    f.bits = bits;
    f.p = p;
    f.ebits = ebits;
    f.bias = (1 << (ebits - 1)) - 1;
    f.emin = 1 - f.bias;
    f.emax = f.bias;
    f.q = f.emin - p + 1;
    f.sign = 1ULL << (bits - 1);
    f.fmask = (1ULL << (p - 1)) - 1;
    f.emask = ((1ULL << ebits) - 1) << (p - 1);
    f.qbit = 1ULL << (p - 2);
    f.indef = f.sign | f.emask | f.qbit;
    return f;
}

static int blen(uint64_t v)
{
    unsigned long i;
    return _BitScanReverse64(&i, v) ? (int)i + 1 : 0;
}

/* round (t + frac) * 2^se (frac != 0 iff sticky) to a multiple of 2^q (q > se), RNE */
static uint64_t rnd_q(uint64_t t, int sticky, int se, int q)
{
    int sh = q - se;
    uint64_t n, rem, half;

    if (sh >= 64) {
        return 0; /* below half the quantum: not reached by the callers' ranges */
    }
    n = t >> sh;
    rem = t & ((1ULL << sh) - 1);
    half = 1ULL << (sh - 1);
    if (rem > half || (rem == half && (sticky || (n & 1)))) {
        n++;
    }
    return n;
}

/* to p significant bits: *n in [2^(p-1), 2^p), returns the exponent of the quantum */
static int rnd_p(uint64_t t, int sticky, int se, int p, uint64_t *n)
{
    int q = se + blen(t) - p;
    *n = rnd_q(t, sticky, se, q);
    if (*n == (1ULL << p)) {
        *n >>= 1;
        q++;
    }
    return q;
}

static uint64_t encode(const Fmt *f, uint64_t s, uint64_t n, int e2)
{
    if (!n) {
        return s;
    }
    if (blen(n) == f->p) {
        int lead = e2 + f->p - 1;
        if (lead > f->emax) {
            return s | f->emask;
        }
        return s | ((uint64_t)(lead + f->bias) << (f->p - 1)) | (n & f->fmask);
    }
    return s | n; /* e2 == q: denormal (or 2^(p-1) = the smallest normal, same bits) */
}

static void decode(const Fmt *f, uint64_t x, uint64_t *m, int *e)
{
    int be = (int)((x & f->emask) >> (f->p - 1));
    *m = x & f->fmask;
    if (be) {
        *m |= 1ULL << (f->p - 1);
        *e = be - f->bias - (f->p - 1);
    } else {
        *e = f->q;
    }
}

/* exact 1/|x| = (t + frac) * 2^se, t >= 2^(p+2) */
static void recip_parts(const Fmt *f, uint64_t x, uint64_t *t, int *sticky, int *se)
{
    uint64_t m, rem;
    int e, k;

    decode(f, x, &m, &e);
    k = f->p + 3 + blen(m);
    if (k < 64) {
        *t = (1ULL << k) / m;
        rem = (1ULL << k) % m;
    } else {
        *t = _udiv128(1ULL << (k - 64), 0, m, &rem);
    }
    *sticky = rem != 0;
    *se = -e - k;
}

/* t^2 * m compared with 2^(2k): -1, 0, 1 (t < 2^64, m < 2^55, 2k < 192) */
static int cmp_sq(uint64_t t, uint64_t m, int k2)
{
    uint64_t sh, sl = _umul128(t, t, &sh), p0, p1, p2, c, w[3], pw[3] = {0, 0, 0};
    int i;

    p0 = _umul128(sl, m, &c);
    p1 = _umul128(sh, m, &p2);
    p1 += c;
    p2 += p1 < c;
    w[0] = p0;
    w[1] = p1;
    w[2] = p2;
    pw[k2 / 64] = 1ULL << (k2 % 64);
    for (i = 2; i >= 0; i--) {
        if (w[i] != pw[i]) {
            return w[i] < pw[i] ? -1 : 1;
        }
    }
    return 0;
}

/* exact 1/sqrt(|x|) = (t + frac) * 2^se, t >= 2^(p+2) */
static void rsqrt_parts(const Fmt *f, uint64_t x, uint64_t *t, int *sticky, int *se)
{
    uint64_t m, tt;
    int e, k, c;
    double est;

    decode(f, x, &m, &e);
    if (e & 1) {
        m <<= 1;
        e -= 1;
    }
    k = f->p + 4 + (blen(m) + 1) / 2;
    /* t = floor(2^k / sqrt(m)): estimate in double, then exact correction */
    est = ldexp(1.0, k) / sqrt((double)m);
    tt = (uint64_t)est;
    while (cmp_sq(tt, m, 2 * k) > 0) {
        tt--;
    }
    while (cmp_sq(tt + 1, m, 2 * k) <= 0) {
        tt++;
    }
    c = cmp_sq(tt, m, 2 * k);
    *t = tt;
    *sticky = c != 0;
    *se = -k - e / 2;
}

static int isnan_(const Fmt *f, uint64_t x)
{
    return (x & f->emask) == f->emask && (x & f->fmask);
}

static int isinf_(const Fmt *f, uint64_t x)
{
    return (x & ~f->sign) == f->emask;
}

/* kind: 0 RCP14, 1 RSQRT14, 2 RCPPH, 3 RSQRTPH, 4 RCPBF16, 5 RSQRTBF16 */
static uint64_t model(const Fmt *f, int kind, uint64_t x, uint32_t mxcsr)
{
    uint64_t s = x & f->sign, t, n;
    int st, se, e2, lead, rsq = kind & 1;
    int daz = kind >= 4 || (kind < 2 && (mxcsr & 0x40));

    if (isnan_(f, x)) {
        return x | f->qbit;
    }
    if (!(x & f->emask) && (!(x & f->fmask) || daz)) {
        return s | f->emask;
    }
    if (isinf_(f, x)) {
        return rsq ? (s ? f->indef : 0) : s;
    }
    if (rsq) {
        if (s) {
            return f->indef;
        }
        rsqrt_parts(f, x, &t, &st, &se);
        e2 = rnd_p(t, st, se, f->p, &n);
        return encode(f, 0, n, e2);
    }
    recip_parts(f, x, &t, &st, &se);
    e2 = rnd_p(t, st, se, f->p, &n);
    lead = e2 + f->p - 1;
    if (lead > f->emax) {
        return s | f->emask;
    }
    if (lead < f->emin) {
        if (kind == 4 || (kind == 0 && (mxcsr & 0x8000))) {
            return s;                                       /* FTZ */
        }
        return encode(f, s, rnd_q(t, st, se, f->q), f->q); /* one rounding (U940 for RCP14) */
    }
    return encode(f, s, n, e2);
}

/* documented-requirement checks on the emulator's result r (independent of the model) */
static double val(const Fmt *f, uint64_t x)
{
    uint64_t m;
    int e;
    double v;

    decode(f, x, &m, &e);
    v = ldexp((double)m, e);
    return (x & f->sign) ? -v : v;
}

static uint64_t g_req_fail;
static double g_worst[6];

static void req(const Fmt *f, int kind, uint64_t x, uint64_t r, uint32_t mxcsr)
{
    static const double bound[6] = {0x1p-14, 0x1p-14, 0x1p-11 + 0x1p-14, 0x1p-11 + 0x1p-14,
                                    0x1p-8 + 0x1p-14, 0x1p-8 + 0x1p-14};
    int be = (int)((x & f->emask) >> (f->p - 1)), rbe = (int)((r & f->emask) >> (f->p - 1));
    int rsq = kind & 1, rnorm = rbe > 0 && rbe < (1 << f->ebits) - 1;
    double xv, rv, err;

    if (isnan_(f, x) || isinf_(f, x) || !(x & ~f->sign)) {
        return; /* NaN / INF / zero rows: covered by the model comparison */
    }
    if (be == 0 && (kind >= 4 || (kind < 2 && (mxcsr & 0x40)))) {
        return;
    }
    if (rsq && (x & f->sign)) {
        if (r != f->indef) {
            g_req_fail++;
        }
        return;
    }
    if (!rnorm) {
        if (rsq) {
            g_req_fail++; /* "any denormal -> normal, cannot generate overflow" */
        }
        return;           /* tiny RCP results: SDM underflow row (checked by the model) */
    }
    if (f->bits == 64) {
        /* FP64: double cannot hold the error exactly; |1 - r*x| in double is good to 2^-50 */
        xv = val(f, x);
        rv = val(f, r);
        err = rsq ? fabs(rv * sqrt(xv) - 1.0) : fabs(rv * xv - 1.0);
    } else {
        xv = val(f, x);
        rv = val(f, r);
        err = rsq ? fabs(rv * sqrt(xv) - 1.0) : fabs(rv * xv - 1.0);
    }
    if (err > g_worst[kind]) {
        g_worst[kind] = err;
    }
    if (!(err < bound[kind])) {
        g_req_fail++;
    }
}

/* ------------------------------------------------------------------ Unicorn runner */
typedef struct {
    uc_engine *uc;
    uint64_t end;
} Run;

static void run_open(Run *r, const uint8_t *insn, size_t n, int avx10)
{
    static const uint8_t pro[] = {0x62, 0xF1, 0x7E, 0x48, 0x6F, 0x0E}; /* vmovdqu32 zmm1, [rsi] */
    static const uint8_t epi[] = {0x62, 0xF1, 0x7E, 0x48, 0x7F, 0x07, /* vmovdqu32 [rdi], zmm0 */
                                  0x48, 0x83, 0xC6, 0x40,             /* add rsi, 64 */
                                  0x48, 0x83, 0xC7, 0x40,             /* add rdi, 64 */
                                  0x48, 0xFF, 0xC9};                  /* dec rcx */
    uint8_t code[64];
    size_t len = 0;
    uc_err e;

    memcpy(code, pro, sizeof(pro));
    len = sizeof(pro);
    memcpy(code + len, insn, n);
    len += n;
    memcpy(code + len, epi, sizeof(epi));
    len += sizeof(epi);
    code[len++] = 0x75;                       /* jnz loop */
    code[len] = (uint8_t)(0 - (len + 1));
    len++;
    r->end = CODE + len;
    if ((e = uc_open(UC_ARCH_X86, UC_MODE_64, &r->uc)) != UC_ERR_OK) {
        printf("uc_open: %s\n", uc_strerror(e));
        exit(2);
    }
    uc_ctl_set_cpu_model(r->uc, UC_CPU_X86_MAX);
    if (avx10) {
        uc_ctl_set_x86_avx10(r->uc, UC_X86_AVX10_2);
    } else {
        uc_ctl_set_x86_avx512(r->uc, UC_X86_AVX512_F | UC_X86_AVX512_DQ | UC_X86_AVX512_BW |
                                         UC_X86_AVX512_VL | UC_X86_AVX512_FP16);
    }
    uc_mem_map(r->uc, CODE, 0x1000, UC_PROT_ALL);
    uc_mem_map(r->uc, DATA_IN, BLOCK, UC_PROT_ALL);
    uc_mem_map(r->uc, DATA_OUT, BLOCK, UC_PROT_ALL);
    uc_mem_write(r->uc, CODE, code, len);
}

static void run_block(Run *r, const void *in, void *out, uint32_t bytes, uint32_t mxcsr)
{
    uint64_t rsi = DATA_IN, rdi = DATA_OUT, rcx = bytes / 64;
    uc_err e;

    uc_mem_write(r->uc, DATA_IN, in, bytes);
    uc_reg_write(r->uc, UC_X86_REG_RSI, &rsi);
    uc_reg_write(r->uc, UC_X86_REG_RDI, &rdi);
    uc_reg_write(r->uc, UC_X86_REG_RCX, &rcx);
    uc_reg_write(r->uc, UC_X86_REG_MXCSR, &mxcsr);
    if ((e = uc_emu_start(r->uc, CODE, r->end, 0, 0)) != UC_ERR_OK) {
        printf("uc_emu_start: %s\n", uc_strerror(e));
        exit(2);
    }
    uc_reg_read(r->uc, UC_X86_REG_MXCSR, &rcx);
    if ((uint32_t)rcx != mxcsr) {
        printf("MXCSR changed: %08X -> %08X\n", mxcsr, (uint32_t)rcx);
        g_fail++;
    }
    uc_mem_read(r->uc, DATA_OUT, out, bytes);
}

static const char *KNAME[6] = {"RCP14", "RSQRT14", "RCPPH", "RSQRTPH", "RCPBF16", "RSQRTBF16"};

static void check_elems(const Fmt *f, int kind, const uint8_t *in, const uint8_t *out, uint32_t n,
                        uint32_t mxcsr, uint64_t *mism)
{
    uint32_t i;
    int esz = f->bits / 8;

    for (i = 0; i < n; i++) {
        uint64_t x = 0, r = 0, m;
        memcpy(&x, in + (size_t)i * esz, esz);
        memcpy(&r, out + (size_t)i * esz, esz);
        m = model(f, kind, x, mxcsr);
        req(f, kind, x, r, mxcsr);
        g_checked++;
        if (m != r) {
            if (*mism < 8) {
                printf("  MISMATCH %s%d x=%0*llX mxcsr=%04X emulator=%0*llX model=%0*llX\n", KNAME[kind],
                       f->bits, esz * 2, (unsigned long long)x, mxcsr, esz * 2, (unsigned long long)r, esz * 2,
                       (unsigned long long)m);
            }
            (*mism)++;
        }
    }
}

static void sweep32(int kind, const uint8_t *insn, uint32_t mxcsr, uint8_t *in, uint8_t *out)
{
    Fmt f = mkfmt(32, 24, 8);
    Run r;
    uint64_t base, mism = 0, rf0 = g_req_fail;
    uint32_t i, per = BLOCK / 4;

    run_open(&r, insn, 6, 0);
    for (base = 0; base < (1ULL << 32); base += per) {
        for (i = 0; i < per; i++) {
            ((uint32_t *)in)[i] = (uint32_t)(base + i);
        }
        run_block(&r, in, out, BLOCK, mxcsr);
        check_elems(&f, kind, in, out, per, mxcsr, &mism);
    }
    uc_close(r.uc);
    printf("%-8s FP32 all 2^32 inputs, MXCSR %04X: mismatches %llu, requirement failures %llu\n", KNAME[kind],
           mxcsr, (unsigned long long)mism, (unsigned long long)(g_req_fail - rf0));
    fflush(stdout);
    g_fail += mism;
}

static uint64_t rng_s = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd64(void)
{
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 7;
    rng_s ^= rng_s << 17;
    return rng_s;
}

static void sweep64(int kind, const uint8_t *insn, uint32_t mxcsr, uint64_t count, uint8_t *in, uint8_t *out)
{
    Fmt f = mkfmt(64, 53, 11);
    Run r;
    uint64_t done = 0, mism = 0, rf0 = g_req_fail, edges = 0;
    uint32_t i, per = BLOCK / 8;
    uint64_t e = 0, ei = 0;
    static const uint64_t edge_m[] = {0, 1, 2, 3, 0xFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFEULL, 0x8000000000000ULL,
                                      0x7FFFFFFFFFFFFULL, 0x8000000000001ULL, 0x4000000000000ULL,
                                      0x4000000000001ULL, 0x3FFFFFFFFFFFFULL, 0x5555555555555ULL,
                                      0xAAAAAAAAAAAAAULL};

    run_open(&r, insn, 6, 0);
    /* every biased exponent 0..2047 x the edge mantissas x both signs, then random */
    while (done < count + 2048 * 14 * 2) {
        for (i = 0; i < per; i++) {
            uint64_t x;
            if (edges < 2048 * 14 * 2) {
                x = ((edges & 1) << 63) | (((edges >> 1) / 14) << 52) | edge_m[(edges >> 1) % 14];
                edges++;
            } else {
                x = rnd64();
            }
            ((uint64_t *)in)[i] = x;
        }
        run_block(&r, in, out, BLOCK, mxcsr);
        check_elems(&f, kind, in, out, per, mxcsr, &mism);
        done += per;
    }
    (void)e;
    (void)ei;
    uc_close(r.uc);
    printf("%-8s FP64 %llu inputs (every exponent x 14 edge mantissas x 2 signs + random), MXCSR %04X: mismatches %llu, requirement failures %llu\n",
           KNAME[kind], (unsigned long long)done, mxcsr, (unsigned long long)mism,
           (unsigned long long)(g_req_fail - rf0));
    fflush(stdout);
    g_fail += mism;
}

static void sweep16(int kind, const uint8_t *insn, uint32_t mxcsr, int bf, uint8_t *in, uint8_t *out)
{
    Fmt f = bf ? mkfmt(16, 8, 8) : mkfmt(16, 11, 5);
    Run r;
    uint64_t mism = 0, rf0 = g_req_fail;
    uint32_t i;

    run_open(&r, insn, 6, bf);
    for (i = 0; i < 65536; i++) {
        ((uint16_t *)in)[i] = (uint16_t)i;
    }
    run_block(&r, in, out, 131072, mxcsr);
    check_elems(&f, kind, in, out, 65536, mxcsr, &mism);
    uc_close(r.uc);
    printf("%-8s %s all 65536 inputs, MXCSR %04X: mismatches %llu, requirement failures %llu\n", KNAME[kind],
           bf ? "BF16" : "FP16", mxcsr, (unsigned long long)mism, (unsigned long long)(g_req_fail - rf0));
    fflush(stdout);
    g_fail += mism;
}

int main(int argc, char **argv)
{
    static const uint8_t rcp14ps[] = {0x62, 0xF2, 0x7D, 0x48, 0x4C, 0xC1}, rsqrt14ps[] = {0x62, 0xF2, 0x7D, 0x48, 0x4E, 0xC1};
    static const uint8_t rcp14pd[] = {0x62, 0xF2, 0xFD, 0x48, 0x4C, 0xC1}, rsqrt14pd[] = {0x62, 0xF2, 0xFD, 0x48, 0x4E, 0xC1};
    static const uint8_t rcpph[] = {0x62, 0xF6, 0x7D, 0x48, 0x4C, 0xC1}, rsqrtph[] = {0x62, 0xF6, 0x7D, 0x48, 0x4E, 0xC1};
    static const uint8_t rcpbf[] = {0x62, 0xF6, 0x7C, 0x48, 0x4C, 0xC1}, rsqrtbf[] = {0x62, 0xF6, 0x7C, 0x48, 0x4E, 0xC1};
    static const uint32_t mx32[] = {0x1F80, 0x1FC0, 0x9F80, 0x9FC0, 0x6000};
    static const uint32_t mx16[] = {0x1F80, 0x9FC0, 0x6000};
    int all = argc < 2, i, k, a;
    uint64_t n64 = 1ULL << 24;
    uint8_t *in = malloc(BLOCK), *out = malloc(BLOCK);

    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "fp64") && a + 1 < argc) {
            n64 = _strtoui64(argv[a + 1], NULL, 0);
        }
    }
    if (argc == 4 && !strcmp(argv[1], "fp32one")) {
        /* one FP32 sweep (kind 0 RCP14 / 1 RSQRT14, MXCSR hex), for running the ten sweeps in parallel */
        uint32_t mx = (uint32_t)strtoul(argv[3], NULL, 16);
        sweep32(atoi(argv[2]), atoi(argv[2]) ? rsqrt14ps : rcp14ps, mx, in, out);
        printf("worst relative error of a normal result, %s: 2^%.4f\n", KNAME[atoi(argv[2])],
               g_worst[atoi(argv[2])] ? log2(g_worst[atoi(argv[2])]) : -999.0);
        printf("checked elements %llu, mismatches %llu, documented-requirement failures %llu\n",
               (unsigned long long)g_checked, (unsigned long long)g_fail, (unsigned long long)g_req_fail);
        return g_fail || g_req_fail ? 1 : 0;
    }
    for (a = 0; a < argc || all; a++) {
        const char *w = all ? NULL : argv[a];
        if (all || !strcmp(w, "f16")) {
            for (i = 0; i < 3; i++) {
                sweep16(2, rcpph, mx16[i], 0, in, out);
                sweep16(3, rsqrtph, mx16[i], 0, in, out);
            }
        }
        if (all || !strcmp(w, "bf16")) {
            for (i = 0; i < 3; i++) {
                sweep16(4, rcpbf, mx16[i], 1, in, out);
                sweep16(5, rsqrtbf, mx16[i], 1, in, out);
            }
        }
        if (all || !strcmp(w, "fp64")) {
            for (i = 0; i < 4; i++) {
                sweep64(0, rcp14pd, mx32[i], n64, in, out);
                sweep64(1, rsqrt14pd, mx32[i], n64, in, out);
            }
        }
        if (all || !strcmp(w, "fp32")) {
            for (i = 0; i < 5; i++) {
                sweep32(0, rcp14ps, mx32[i], in, out);
                sweep32(1, rsqrt14ps, mx32[i], in, out);
            }
        }
        if (all) {
            break;
        }
    }
    for (k = 0; k < 6; k++) {
        printf("worst relative error of a normal result, %s: 2^%.4f\n", KNAME[k], g_worst[k] ? log2(g_worst[k]) : -999.0);
    }
    printf("checked elements %llu, mismatches %llu, documented-requirement failures %llu\n",
           (unsigned long long)g_checked, (unsigned long long)g_fail, (unsigned long long)g_req_fail);
    return g_fail || g_req_fail ? 1 : 0;
}
