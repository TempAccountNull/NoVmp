/*
 * NoVmp (ledger U331): AVX512-FP16 (EVEX maps 5 and 6) - the interface between the
 * translator (emit_fp16.c.inc) and the helpers (fp16_helper.c.inc). Our code only; not
 * used when __Use_Original_Qemu == 1.
 *
 * desc (i32) of every FP16 helper:
 *   bits  7:0   operation (FP16_OP_*)
 *   bits 13:8   number of elements processed (destination elements; 1 for scalar forms)
 *   bits 16:14  rounding: 0-3 static {er} rounding (RNE, RD, RU, RZ; implies SAE),
 *               FP16_RC_MXCSR (MXCSR.RC), FP16_RC_SAE ({sae}: MXCSR.RC, no flags, no #XM)
 *   bit  17     zeroing-masking (EVEX.z)
 *   bit  18     scalar form (DEST[127:esz] from SRC1, element 0 only)
 *   bit  19     conversions to integers: truncate (VCVTT*)
 *   bits 31:24  imm8, FMA variant or conversion types (FP16_CVT)
 */
#ifndef TARGET_I386_FP16_H
#define TARGET_I386_FP16_H

enum {
    FP16_OP_ADD,
    FP16_OP_SUB,
    FP16_OP_MUL,
    FP16_OP_DIV,
    FP16_OP_MIN,
    FP16_OP_MAX,
    FP16_OP_SQRT,
    FP16_OP_RCP,
    FP16_OP_RSQRT,
    FP16_OP_SCALEF,
    FP16_OP_GETEXP,
    FP16_OP_GETMANT,
    FP16_OP_REDUCE,
    FP16_OP_RNDSCALE,
    FP16_OP_FMA,        /* imm: FP16_FMA_* */
    FP16_OP_FMADDSUB,   /* even elements x*y-z, odd x*y+z */
    FP16_OP_FMSUBADD,   /* even elements x*y+z, odd x*y-z */
    FP16_OP_CMUL,       /* VFMULCPH/VFMULCSH */
    FP16_OP_CMULC,      /* VFCMULCPH/VFCMULCSH (complex conjugate of SRC2) */
    FP16_OP_CFMA,       /* VFMADDCPH/VFMADDCSH */
    FP16_OP_CFMAC,      /* VFCMADDCPH/VFCMADDCSH */
    FP16_OP_MOVSH,      /* register form: DEST[15:0] = SRC2, DEST[127:16] = SRC1 */
    FP16_OP_MOVSH_LD,   /* load form: DEST[15:0] = m16, DEST[127:16] = 0 */
    FP16_OP_CVT,        /* imm = FP16_CVT(src type, dst type) */
    FP16_OP_CMP,        /* helper_fp16_cmp: imm = predicate */
    FP16_OP_FPCLASS,    /* helper_fp16_cmp: imm = categories */
};

#define FP16_RC_MXCSR   4
#define FP16_RC_SAE     5

#define FP16_DESC(op, n, rc)  ((uint32_t)(op) | ((uint32_t)(n) << 8) | ((uint32_t)(rc) << 14))
#define FP16_D_Z        (1u << 17)
#define FP16_D_SCALAR   (1u << 18)
#define FP16_D_TRUNC    (1u << 19)
#define FP16_D_IMM(x)   ((uint32_t)((x) & 0xff) << 24)

#define FP16_DESC_OP(d)     ((d) & 0xff)
#define FP16_DESC_N(d)      (((d) >> 8) & 0x3f)
#define FP16_DESC_RC(d)     (((d) >> 14) & 7)
#define FP16_DESC_IMM(d)    ((d) >> 24)

/* FMA variants (imm): operand order and negations, SDM Vol1 14.5.1 / Table 14-17 */
#define FP16_FMA_132    0   /* x = DEST, y = SRC3, z = SRC2 */
#define FP16_FMA_213    1   /* x = SRC2, y = DEST, z = SRC3 */
#define FP16_FMA_231    2   /* x = SRC2, y = SRC3, z = DEST */
#define FP16_FMA_NEGP   4   /* -(x*y) */
#define FP16_FMA_NEGC   8   /* -z */

/* conversion element types (imm = src << 4 | dst) */
enum {
    FP16_T_F16,
    FP16_T_F32,
    FP16_T_F64,
    FP16_T_I16,
    FP16_T_U16,
    FP16_T_I32,
    FP16_T_U32,
    FP16_T_I64,
    FP16_T_U64,
};
#define FP16_CVT(src, dst)  (((src) << 4) | (dst))

#endif /* TARGET_I386_FP16_H */
