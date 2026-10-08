/*
 * Intel AVX10.2 helpers (NoVmp, ledgers U372-U399): descriptor layout shared by the
 * translator (emit-avx10.c.inc) and avx10_helper.c.
 */
#ifndef NOVMP_AVX10_HELPER_H
#define NOVMP_AVX10_HELPER_H
#if __Use_Original_Qemu != 1 /* ours (U372-U399) */

/* desc: bits 7:0 operation, 15:8 imm8, 23:16 element count, 31:24 operation flags */
#define AVX10_DESC(op, imm, n, fl) ((uint32_t)(op) | ((uint32_t)((imm) & 0xff) << 8) | \
                                    ((uint32_t)(n) << 16) | ((uint32_t)(fl) << 24))
#define AVX10_DESC_OP(d)   ((d) & 0xff)
#define AVX10_DESC_IMM(d)  (((d) >> 8) & 0xff)
#define AVX10_DESC_N(d)    (((d) >> 16) & 0xff)
#define AVX10_DESC_FL(d)   (((d) >> 24) & 0xff)

/* helper_avx10_bf16 / helper_avx10_bf16_k operations (U373) */
enum {
    AVX10_BF16_ADD,
    AVX10_BF16_SUB,
    AVX10_BF16_MUL,
    AVX10_BF16_DIV,
    AVX10_BF16_MIN,
    AVX10_BF16_MAX,
    AVX10_BF16_SCALEF,
    AVX10_BF16_SQRT,
    AVX10_BF16_RCP,
    AVX10_BF16_RSQRT,
    AVX10_BF16_GETEXP,
    AVX10_BF16_GETMANT,
    AVX10_BF16_REDUCE,
    AVX10_BF16_RNDSCALE,
    AVX10_BF16_CMP,
    AVX10_BF16_FPCLASS,
};

/* helper_avx10_bf16_fma flags (desc bits 31:24) */
#define AVX10_FMA_NEG (1u << 24)    /* a := -a (VFNM*) */
#define AVX10_FMA_SUB (2u << 24)    /* a * b - c (VF*MSUB) */

/* element formats of helper_avx10_minmax / helper_avx10_vcomx (desc operation; U374) */
enum {
    AVX10_FMT_BF16,
    AVX10_FMT_FP16,
    AVX10_FMT_FP32,
    AVX10_FMT_FP64,
};
#define AVX10_MINMAX_SCALAR   (1u << 24)  /* VMINMAXSH/SS/SD: bits 127:esz from SRC1 */

#endif /* __Use_Original_Qemu (U372-U399) */
#endif /* NOVMP_AVX10_HELPER_H */
