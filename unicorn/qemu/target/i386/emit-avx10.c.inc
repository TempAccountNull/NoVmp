/*
 * Intel AVX10.2 instructions: code generation (NoVmp, ledgers U372-U399; included by
 * emit.c.inc). The EVEX engine gen_evex_insn (U146) does masking, {1toN}, masked memory and
 * the MXCSR / {er} / {sae} frame; these functions compute the full-length result.
 */
#include "avx10_helper.h"

/* ---- BF16 arithmetic (spec ch.7; U373) ---- */
static void gen_avx10_bf16(DisasContext *s, X86DecodedInsn *decode, int op, bool unary)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = AVX10_DESC(op, decode->immediate, evex_nelem(s, decode), 0);

    gen_helper_avx10_bf16(tcg_ctx, cpu_env, OP_PTR0, unary ? OP_PTR2 : OP_PTR1, OP_PTR2,
                          tcg_constant_i32(tcg_ctx, desc));
}

#define AVX10_BF16_GEN(uname, op, unary)                                              \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)    \
{                                                                                     \
    gen_avx10_bf16(s, decode, op, unary);                                             \
}
AVX10_BF16_GEN(VADDBF16, AVX10_BF16_ADD, false)
AVX10_BF16_GEN(VSUBBF16, AVX10_BF16_SUB, false)
AVX10_BF16_GEN(VMULBF16, AVX10_BF16_MUL, false)
AVX10_BF16_GEN(VDIVBF16, AVX10_BF16_DIV, false)
AVX10_BF16_GEN(VMINBF16, AVX10_BF16_MIN, false)
AVX10_BF16_GEN(VMAXBF16, AVX10_BF16_MAX, false)
AVX10_BF16_GEN(VSCALEFBF16, AVX10_BF16_SCALEF, false)
AVX10_BF16_GEN(VSQRTBF16, AVX10_BF16_SQRT, true)
AVX10_BF16_GEN(VRCPBF16, AVX10_BF16_RCP, true)
AVX10_BF16_GEN(VRSQRTBF16, AVX10_BF16_RSQRT, true)
AVX10_BF16_GEN(VGETEXPBF16, AVX10_BF16_GETEXP, true)
AVX10_BF16_GEN(VGETMANTBF16, AVX10_BF16_GETMANT, true)
AVX10_BF16_GEN(VREDUCEBF16, AVX10_BF16_REDUCE, true)
AVX10_BF16_GEN(VRNDSCALEBF16, AVX10_BF16_RNDSCALE, true)
#undef AVX10_BF16_GEN

/*
 * VF[N]M{ADD,SUB}{132,213,231}BF16 (7.5.3): 132: a = DEST, b = SRC3, c = SRC2; 213: a =
 * SRC2, b = DEST, c = SRC3; 231: a = SRC2, b = SRC3, c = DEST. DEST is read from the
 * register itself (op[0] may point to the engine's scratch result register).
 */
static void gen_avx10_bf16_fma(DisasContext *s, X86DecodedInsn *decode, int form, uint32_t fl)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr dst = tcg_temp_new_ptr(tcg_ctx);
    TCGv_ptr a, b, c;
    uint32_t desc = AVX10_DESC(0, 0, evex_nelem(s, decode), 0) | fl;

    tcg_gen_addi_ptr(tcg_ctx, dst, cpu_env, ZMM_OFFSET(decode->op[0].n));
    switch (form) {
    case 132:
        a = dst, b = OP_PTR2, c = OP_PTR1;
        break;
    case 213:
        a = OP_PTR1, b = dst, c = OP_PTR2;
        break;
    default:
        a = OP_PTR1, b = OP_PTR2, c = dst;
        break;
    }
    gen_helper_avx10_bf16_fma(tcg_ctx, cpu_env, OP_PTR0, a, b, c,
                              tcg_constant_i32(tcg_ctx, desc));
    tcg_temp_free_ptr(tcg_ctx, dst);
}

#define AVX10_FMA_GEN(name, form, fl)                                                 \
static void gen_##name(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)     \
{                                                                                     \
    gen_avx10_bf16_fma(s, decode, form, fl);                                          \
}
AVX10_FMA_GEN(VFMADD132BF16, 132, 0)
AVX10_FMA_GEN(VFMADD213BF16, 213, 0)
AVX10_FMA_GEN(VFMADD231BF16, 231, 0)
AVX10_FMA_GEN(VFMSUB132BF16, 132, AVX10_FMA_SUB)
AVX10_FMA_GEN(VFMSUB213BF16, 213, AVX10_FMA_SUB)
AVX10_FMA_GEN(VFMSUB231BF16, 231, AVX10_FMA_SUB)
AVX10_FMA_GEN(VFNMADD132BF16, 132, AVX10_FMA_NEG)
AVX10_FMA_GEN(VFNMADD213BF16, 213, AVX10_FMA_NEG)
AVX10_FMA_GEN(VFNMADD231BF16, 231, AVX10_FMA_NEG)
AVX10_FMA_GEN(VFNMSUB132BF16, 132, AVX10_FMA_NEG | AVX10_FMA_SUB)
AVX10_FMA_GEN(VFNMSUB213BF16, 213, AVX10_FMA_NEG | AVX10_FMA_SUB)
AVX10_FMA_GEN(VFNMSUB231BF16, 231, AVX10_FMA_NEG | AVX10_FMA_SUB)
#undef AVX10_FMA_GEN

/* ---- BF16 compares (spec 7.2, 7.3, 7.6; U373) ---- */
static void gen_VCMPBF16(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = AVX10_DESC(AVX10_BF16_CMP, decode->immediate & 0x1f,
                               evex_nelem(s, decode), 0);

    gen_helper_avx10_bf16_k(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR1, OP_PTR2,
                            tcg_constant_i32(tcg_ctx, desc));
}

static void gen_VFPCLASSBF16(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = AVX10_DESC(AVX10_BF16_FPCLASS, decode->immediate, evex_nelem(s, decode), 0);

    gen_helper_avx10_bf16_k(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR2, OP_PTR2,
                            tcg_constant_i32(tcg_ctx, desc));
}

static void gen_VCOMISBF16(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_avx10_vcomisbf16(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2);
    set_cc_op(s, CC_OP_EFLAGS);
}

/* ---- MINMAX (spec ch.11; U374) ---- */
static void gen_avx10_minmax(DisasContext *s, X86DecodedInsn *decode, int fmt, bool scalar)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr s1reg = tcg_temp_new_ptr(tcg_ctx);
    uint32_t desc = AVX10_DESC(fmt, decode->immediate, scalar ? 1 : evex_nelem(s, decode), 0) |
                    (scalar ? AVX10_MINMAX_SCALAR : 0);

    tcg_gen_addi_ptr(tcg_ctx, s1reg, cpu_env, ZMM_OFFSET(decode->op[1].n));
    gen_helper_avx10_minmax(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, s1reg,
                            tcg_constant_i32(tcg_ctx, desc));
    tcg_temp_free_ptr(tcg_ctx, s1reg);
}

static void gen_VMINMAXBF16(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_avx10_minmax(s, decode, AVX10_FMT_BF16, false);
}

static void gen_VMINMAXPH(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_avx10_minmax(s, decode, AVX10_FMT_FP16, false);
}

static void gen_VMINMAXPx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_avx10_minmax(s, decode, s->vex_w ? AVX10_FMT_FP64 : AVX10_FMT_FP32, false);
}

static void gen_VMINMAXSH(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_avx10_minmax(s, decode, AVX10_FMT_FP16, true);
}

static void gen_VMINMAXSx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_avx10_minmax(s, decode, s->vex_w ? AVX10_FMT_FP64 : AVX10_FMT_FP32, true);
}

/* ---- VCOMX / VUCOMX (spec ch.8; U375) ---- */
static void gen_avx10_vcomx(DisasContext *s, X86DecodedInsn *decode, int fmt, uint32_t sig)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_avx10_vcomx(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2,
                           tcg_constant_i32(tcg_ctx, AVX10_DESC(fmt, 0, 1, 0) | sig));
    set_cc_op(s, CC_OP_EFLAGS);
}

#define AVX10_VCOMX_GEN(name, fmt, sig) \
static void gen_##name(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{ \
    gen_avx10_vcomx(s, decode, fmt, sig); \
}
AVX10_VCOMX_GEN(VCOMXSS, AVX10_FMT_FP32, AVX10_VCOMX_SIGNALING)
AVX10_VCOMX_GEN(VCOMXSD, AVX10_FMT_FP64, AVX10_VCOMX_SIGNALING)
AVX10_VCOMX_GEN(VCOMXSH, AVX10_FMT_FP16, AVX10_VCOMX_SIGNALING)
AVX10_VCOMX_GEN(VUCOMXSS, AVX10_FMT_FP32, 0)
AVX10_VCOMX_GEN(VUCOMXSD, AVX10_FMT_FP64, 0)
AVX10_VCOMX_GEN(VUCOMXSH, AVX10_FMT_FP16, 0)
#undef AVX10_VCOMX_GEN
