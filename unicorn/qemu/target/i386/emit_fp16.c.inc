/*
 * NoVmp (ledgers U333-U339): AVX512-FP16 code generation (SDM Vol2C VADDPH .. VUCOMISH).
 * Included from emit.c.inc after the EVEX execution engine (U146); our code only.
 *
 * gen_evex_insn (U146) loads the memory / {1toN} source, sends the result to evex_t1
 * when it masks it, then merges / zeroes it per destination element (U230 evex_dsz) and
 * zeroes the bytes above the destination (U210 evex_dest_bytes). The FP16 helpers
 * (fp16_helper.c.inc) get the opmask themselves and compute only the enabled elements, so
 * masked-off elements raise no MXCSR flag and no #XM; they also read the destination
 * register before the instruction (FMA and complex multiply-add operand, merging of a
 * scalar element). A scalar form's DEST[127:esz] (from SRC1, from DEST for the FMA forms)
 * is computed by the helper and again by the engine's scalar merge (U192, ev_dsrc on the
 * FMA forms): the same bytes.
 */
#if __Use_Original_Qemu != 1 /* ours (U333) */
#include "fp16.h"

/* the opmask of the instruction (all ones without one, EVEX.aaa = 000b) */
static TCGv_i64 gen_fp16_kmask(DisasContext *s)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i64 k = tcg_temp_new_i64(tcg_ctx);

    if (s->evex_aaa) {
        tcg_gen_ld_i64(tcg_ctx, k, cpu_env, offsetof(CPUX86State, opmask_regs[s->evex_aaa]));
    } else {
        tcg_gen_movi_i64(tcg_ctx, k, -1);
    }
    return k;
}

/* {er}: static rounding (SAE implied); {sae}; otherwise MXCSR */
static int fp16_rc(DisasContext *s)
{
    return s->evex_rc == -1 ? FP16_RC_MXCSR : s->evex_rc == -2 ? FP16_RC_SAE : s->evex_rc & 3;
}

static uint32_t fp16_desc(DisasContext *s, X86DecodedInsn *decode, int op, int imm)
{
    uint32_t d = FP16_DESC(op, evex_nelem(s, decode), fp16_rc(s)) | FP16_D_IMM(imm);

    if (decode->e.evex_vl & X86_EVEX_LIG) {
        d |= FP16_D_SCALAR;
    }
    if (s->evex_z) {
        d |= FP16_D_Z;
    }
    return d;
}

/* helper_fp16_op: d = op0, a = op1 (EVEX.vvvv; op2 when there is none), b = op2 */
static void gen_fp16_call(DisasContext *s, X86DecodedInsn *decode, int op, int imm,
                          uint32_t extra)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i64 k = gen_fp16_kmask(s);
    TCGv_ptr pdst = tcg_temp_new_ptr(tcg_ctx);
    TCGv_ptr pa = decode->op[1].unit == X86_OP_SSE ? OP_PTR1 : OP_PTR2;

    tcg_gen_addi_ptr(tcg_ctx, pdst, cpu_env, ZMM_OFFSET(decode->op[0].n));
    gen_helper_fp16_op(tcg_ctx, cpu_env, OP_PTR0, pa, OP_PTR2, pdst, k,
                       tcg_constant_i32(tcg_ctx, fp16_desc(s, decode, op, imm) | extra));
    tcg_temp_free_ptr(tcg_ctx, pdst);
    tcg_temp_free_i64(tcg_ctx, k);
}

#define FP16_GEN(name, op)                                                            \
static void gen_FP16_##name(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                     \
    gen_fp16_call(s, decode, op, 0, 0);                                               \
}
#define FP16_GEN_IMM(name, op)                                                        \
static void gen_FP16_##name(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                     \
    gen_fp16_call(s, decode, op, decode->immediate & 0xff, 0);                        \
}
#define FP16_GEN_CVT(name, st, dt, extra)                                             \
static void gen_FP16_##name(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                     \
    gen_fp16_call(s, decode, FP16_OP_CVT, FP16_CVT(st, dt), extra);                   \
}

/* U333: VADD/VSUB/VMUL/VDIV/VMIN/VMAX/VSQRT PH/SH */
FP16_GEN(ADD, FP16_OP_ADD)
FP16_GEN(SUB, FP16_OP_SUB)
FP16_GEN(MUL, FP16_OP_MUL)
FP16_GEN(DIV, FP16_OP_DIV)
FP16_GEN(MIN, FP16_OP_MIN)
FP16_GEN(MAX, FP16_OP_MAX)
FP16_GEN(SQRT, FP16_OP_SQRT)
#endif /* __Use_Original_Qemu (U333) */
