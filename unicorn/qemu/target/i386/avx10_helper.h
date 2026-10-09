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

/* helper_avx10_vcomx flags (desc bits 31:24; U375) */
#define AVX10_VCOMX_SIGNALING (1u << 24)  /* VCOMX* (IE on any NaN), not VUCOMX* */

/* helper_avx10_cvt / helper_avx10_cvts: the spec 5.3 helper (desc operation; U376) */
enum {
    AVX10_CVT_B_S_R,        /* convert_fp16/fp32_to_signed_byte_saturate */
    AVX10_CVT_B_S_T32,      /* convert_fp32_to_signed_byte_truncate_saturate */
    AVX10_CVT_B_S_T16,      /* convert_fp16_to_signed_byte_truncate_saturate */
    AVX10_CVT_B_U_R32,      /* convert_fp32_to_unsigned_byte_saturate */
    AVX10_CVT_B_U_T32,      /* convert_fp32_to_unsigned_byte_truncate_saturate */
    AVX10_CVT_B_U_R16,      /* convert_fp16_to_unsigned_byte_saturate */
    AVX10_CVT_B_U_T16,      /* convert_fp16_to_unsigned_byte_truncate_saturate */
    AVX10_CVT_DW_S,         /* convert_SP/DP_to_DW_SignedInteger_TruncateSaturate */
    AVX10_CVT_DW_U,         /* convert_SP_to_DW_UnSignedInteger_TruncateSaturate */
    AVX10_CVT_DW_U_PD,      /* convert_DP_to_DW_UnSignedInteger_TruncateSaturate */
    AVX10_CVT_QW_S,         /* convert_SP/DP_to_QW_SignedInteger_TruncateSaturate */
    AVX10_CVT_QW_U,         /* convert_SP_to_QW_UnSignedInteger_TruncateSaturate */
    AVX10_CVT_QW_U_PD,      /* convert_DP_to_QW_UnSignedInteger_TruncateSaturate */
    AVX10_CVT_BF_S_R,       /* convert_bf16_to_signed_byte_rne_saturate */
    AVX10_CVT_BF_S_T,       /* convert_bf16_to_signed_byte_truncate_saturate */
    AVX10_CVT_BF_U_R,       /* convert_bf16_to_unsigned_byte_rne_saturate */
    AVX10_CVT_BF_U_T,       /* convert_bf16_to_unsigned_byte_truncate_saturate */
};

#endif /* __Use_Original_Qemu (U372-U399) */
#endif /* NOVMP_AVX10_HELPER_H */
