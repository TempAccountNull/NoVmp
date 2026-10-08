/*
 * New-style decoder for i386 instructions
 *
 *  Copyright (c) 2022 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The decoder is mostly based on tables copied from the Intel SDM.  As
 * a result, most operand load and writeback is done entirely in common
 * table-driven code using the same operand type (X86_TYPE_*) and
 * size (X86_SIZE_*) codes used in the manual.
 *
 * The main difference is that the V, U and W types are extended to
 * cover MMX as well; if an instruction is like
 *
 *      por   Pq, Qq
 *  66  por   Vx, Hx, Wx
 *
 * only the second row is included and the instruction is marked as a
 * valid MMX instruction.  The MMX flag directs the decoder to rewrite
 * the V/U/H/W types to P/N/P/Q if there is no prefix, as well as changing
 * "x" to "q" if there is no prefix.
 *
 * In addition, the ss/ps/sd/pd types are sometimes mushed together as "x"
 * if the difference is expressed via prefixes.  Individual instructions
 * are separated by prefix in the generator functions.
 *
 * There are a couple cases in which instructions (e.g. MOVD) write the
 * whole XMM or MM register but are established incorrectly in the manual
 * as "d" or "q".  These have to be fixed for the decoder to work correctly.
 */

#define X86_OP_NONE { 0 },

#define X86_OP_GROUP3(op, op0_, s0_, op1_, s1_, op2_, s2_, ...) { \
    .decode = glue(decode_, op),                                  \
    .op0 = glue(X86_TYPE_, op0_),                                 \
    .s0 = glue(X86_SIZE_, s0_),                                   \
    .op1 = glue(X86_TYPE_, op1_),                                 \
    .s1 = glue(X86_SIZE_, s1_),                                   \
    .op2 = glue(X86_TYPE_, op2_),                                 \
    .s2 = glue(X86_SIZE_, s2_),                                   \
    .is_decode = true,                                            \
    __VA_ARGS__                                                   \
}

#define X86_OP_GROUP2(op, op0, s0, op1, s1, ...)                  \
    X86_OP_GROUP3(op, op0, s0, 2op, s0, op1, s1, __VA_ARGS__)
#define X86_OP_GROUP0(op, ...)                                    \
    X86_OP_GROUP3(op, None, None, None, None, None, None, __VA_ARGS__)

#define X86_OP_ENTRY3(op, op0_, s0_, op1_, s1_, op2_, s2_, ...) { \
    .gen = glue(gen_, op),                                        \
    .op0 = glue(X86_TYPE_, op0_),                                 \
    .s0 = glue(X86_SIZE_, s0_),                                   \
    .op1 = glue(X86_TYPE_, op1_),                                 \
    .s1 = glue(X86_SIZE_, s1_),                                   \
    .op2 = glue(X86_TYPE_, op2_),                                 \
    .s2 = glue(X86_SIZE_, s2_),                                   \
    __VA_ARGS__                                                   \
}

#define X86_OP_ENTRY4(op, op0_, s0_, op1_, s1_, op2_, s2_, ...)   \
    X86_OP_ENTRY3(op, op0_, s0_, op1_, s1_, op2_, s2_,            \
        .op3 = X86_TYPE_I, .s3 = X86_SIZE_b,                      \
        __VA_ARGS__)

#define X86_OP_ENTRY2(op, op0, s0, op1, s1, ...)                  \
    X86_OP_ENTRY3(op, op0, s0, 2op, s0, op1, s1, __VA_ARGS__)
#define X86_OP_ENTRYw(op, op0, s0, ...)                           \
    X86_OP_ENTRY3(op, op0, s0, None, None, None, None, __VA_ARGS__)
#define X86_OP_ENTRYr(op, op0, s0, ...)                           \
    X86_OP_ENTRY3(op, None, None, None, None, op0, s0, __VA_ARGS__)
#define X86_OP_ENTRY0(op, ...)                                    \
    X86_OP_ENTRY3(op, None, None, None, None, None, None, __VA_ARGS__)

#define cpuid(feat) .cpuid = X86_FEAT_##feat,
#define chk(a) .check = X86_CHECK_##a,
#define i64 .special = X86_SPECIAL_i64,
#define o64 .special = X86_SPECIAL_o64,
#define xchg .special = X86_SPECIAL_Locked,
#define mmx .special = X86_SPECIAL_MMX,
#define zext0 .special = X86_SPECIAL_ZExtOp0,
#define zext2 .special = X86_SPECIAL_ZExtOp2,
#define avx_movx .special = X86_SPECIAL_AVXExtMov,

#define vex1 .vex_class = 1,
#define vex1_rep3 .vex_class = 1, .vex_special = X86_VEX_REPScalar,
#define vex2 .vex_class = 2,
#define vex2_rep3 .vex_class = 2, .vex_special = X86_VEX_REPScalar,
#define vex3 .vex_class = 3,
#define vex4 .vex_class = 4,
#define vex4_unal .vex_class = 4, .vex_special = X86_VEX_SSEUnaligned,
#define vex4_rep5 .vex_class = 4, .vex_special = X86_VEX_REPScalar,
#define vex5 .vex_class = 5,
#define vex6 .vex_class = 6,
#define vex7 .vex_class = 7,
#define vex8 .vex_class = 8,
#define vex11 .vex_class = 11,
#define vex12 .vex_class = 12,
#define vex13 .vex_class = 13,
#if __Use_Original_Qemu != 1 /* ours (U129) */
/* VEX opmask instructions: SDM Vol2A 2.9 exception types K20 (no memory) / K21 */
#define vex20 .vex_class = 20,
#define vex21 .vex_class = 21,
#endif /* __Use_Original_Qemu (U129) */
#if __Use_Original_Qemu != 1 /* ours (U175) */
/* VEX Intel AMX instructions: SDM Vol2A 2.10 exception classes AMX-E1..E6 */
#define vex22 .vex_class = 22,
#endif /* __Use_Original_Qemu (U175) */

#define avx2_256 .vex_special = X86_VEX_AVX2_256,
#define sse4_novex .vex_class = 4, .vex_special = X86_VEX_NoVEX,

#define P_00          1
#define P_66          (1 << PREFIX_DATA)
#define P_F3          (1 << PREFIX_REPZ)
#define P_F2          (1 << PREFIX_REPNZ)

#define p_00          .valid_prefix = P_00,
#define p_66          .valid_prefix = P_66,
#define p_f3          .valid_prefix = P_F3,
#define p_f2          .valid_prefix = P_F2,
#define p_00_66       .valid_prefix = P_00 | P_66,
#define p_00_f3       .valid_prefix = P_00 | P_F3,
#define p_66_f2       .valid_prefix = P_66 | P_F2,
#define p_00_66_f3    .valid_prefix = P_00 | P_66 | P_F3,
#define p_66_f3_f2    .valid_prefix = P_66 | P_F3 | P_F2,
#define p_00_66_f3_f2 .valid_prefix = P_00 | P_66 | P_F3 | P_F2,

static uint8_t get_modrm(DisasContext *s, CPUX86State *env)
{
    if (!s->has_modrm) {
        s->modrm = x86_ldub_code(env, s);
        s->has_modrm = true;
    }
    return s->modrm;
}

static inline const X86OpEntry *decode_by_prefix(DisasContext *s, const X86OpEntry entries[4])
{
    if (s->prefix & PREFIX_REPNZ) {
        return &entries[3];
    } else if (s->prefix & PREFIX_REPZ) {
        return &entries[2];
    } else if (s->prefix & PREFIX_DATA) {
        return &entries[1];
    } else {
        return &entries[0];
    }
}

static void decode_group15(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    /* only includes ldmxcsr and stmxcsr, because they have AVX variants.  */
    static const X86OpEntry group15_reg[8] = {
    };

    static const X86OpEntry group15_mem[8] = {
        [2] = X86_OP_ENTRYr(LDMXCSR,    E,d, vex5),
        [3] = X86_OP_ENTRYw(STMXCSR,    E,d, vex5),
    };

    uint8_t modrm = get_modrm(s, env);
    if ((modrm >> 6) == 3) {
        *entry = group15_reg[(modrm >> 3) & 7];
    } else {
        *entry = group15_mem[(modrm >> 3) & 7];
    }
}

static void decode_group17(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86GenFunc group17_gen[8] = {
        NULL, gen_BLSR, gen_BLSMSK, gen_BLSI,
    };
    int op = (get_modrm(s, env) >> 3) & 7;
    entry->gen = group17_gen[op];
}

static void decode_group12(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_group12[8] = {
        {},
        {},
        X86_OP_ENTRY3(PSRLW_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
        X86_OP_ENTRY3(PSRAW_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
        X86_OP_ENTRY3(PSLLW_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
    };

    int op = (get_modrm(s, env) >> 3) & 7;
    *entry = opcodes_group12[op];
}

static void decode_group13(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_group13[8] = {
        {},
        {},
        X86_OP_ENTRY3(PSRLD_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
        X86_OP_ENTRY3(PSRAD_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
        X86_OP_ENTRY3(PSLLD_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
    };

    int op = (get_modrm(s, env) >> 3) & 7;
    *entry = opcodes_group13[op];
}

static void decode_group14(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_group14[8] = {
        /* grp14 */
        {},
        {},
        X86_OP_ENTRY3(PSRLQ_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        X86_OP_ENTRY3(PSRLDQ_i, H,x, U,x, I,b, vex7 avx2_256 p_66),
        {},
        {},
        X86_OP_ENTRY3(PSLLQ_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        X86_OP_ENTRY3(PSLLDQ_i, H,x, U,x, I,b, vex7 avx2_256 p_66),
    };

    int op = (get_modrm(s, env) >> 3) & 7;
    *entry = opcodes_group14[op];
}

static void decode_0F6F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F6F[4] = {
        X86_OP_ENTRY3(MOVDQ,       P,q, None,None, Q,q, vex5 mmx),  /* movq */
        X86_OP_ENTRY3(MOVDQ,       V,x, None,None, W,x, vex1),      /* movdqa */
        X86_OP_ENTRY3(MOVDQ,       V,x, None,None, W,x, vex4_unal), /* movdqu */
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F6F);
}

static void decode_0F70(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry pshufw[4] = {
        X86_OP_ENTRY3(PSHUFW,  P,q, Q,q, I,b, vex4 mmx),
        X86_OP_ENTRY3(PSHUFD,  V,x, W,x, I,b, vex4 avx2_256),
        X86_OP_ENTRY3(PSHUFHW, V,x, W,x, I,b, vex4 avx2_256),
        X86_OP_ENTRY3(PSHUFLW, V,x, W,x, I,b, vex4 avx2_256),
    };

    *entry = *decode_by_prefix(s, pshufw);
}

static void decode_0F77(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    if (!(s->prefix & PREFIX_VEX)) {
        entry->gen = gen_EMMS;
#if __Use_Original_Qemu != 1 /* ours (U77) */
        /* NP 0F 77: 66/F2/F3 #UD (SDM Vol2 EMMS; i5-13600K) (U77) */
        if (s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ)) {
            entry->gen = NULL;
        }
#endif /* __Use_Original_Qemu (U77) */
#if __Use_Original_Qemu != 1 /* ours (U79) */
    } else if (s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ)) {
        entry->gen = NULL;          /* VEX.66/F3/F2 0F 77: #UD (SDM: VEX.NP) (U79) */
#endif /* __Use_Original_Qemu (U79) */
    } else if (!s->vex_l) {
        entry->gen = gen_VZEROUPPER;
        entry->vex_class = 8;
    } else {
        entry->gen = gen_VZEROALL;
        entry->vex_class = 8;
    }
}

static void decode_0F78(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F78_f2 =
        X86_OP_ENTRY3(INSERTQ_i,     V,x, U,x, I,w,        cpuid(SSE4A)); /* AMD extension */
    static const X86OpEntry opcodes_0F78_66 =
        X86_OP_ENTRY3(EXTRQ_i,       U,x, None,None, I,w,  cpuid(SSE4A)); /* AMD extension */

    entry->gen = NULL;
    if (s->prefix & PREFIX_REPNZ) {
        *entry = opcodes_0F78_f2;
    } else if (s->prefix & PREFIX_REPZ) {
        /* undefined */
    } else if (s->prefix & PREFIX_DATA) {
        int op = (get_modrm(s, env) >> 3) & 7;
        if (op == 0) {
            *entry = opcodes_0F78_66;
        }
    }
}

static void decode_0F79(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    entry->gen = NULL;
    if (s->prefix & PREFIX_REPNZ) {
        entry->gen = gen_INSERTQ_r; /* AMD extension */
    } else if (s->prefix & PREFIX_REPZ) {
        /* undefined */
    } else if (s->prefix & PREFIX_DATA) {
        entry->gen = gen_EXTRQ_r; /* AMD extension */
    };
}

static void decode_0F7E(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F7E[4] = {
        X86_OP_ENTRY3(MOVD_from,  E,y, None,None, P,y, vex5 mmx),
#if __Use_Original_Qemu == 1 /* original QEMU (U79) */
        X86_OP_ENTRY3(MOVD_from,  E,y, None,None, V,y, vex5),
#else /* ours (U79) */
        X86_OP_ENTRY3(MOVD_from,  E,y, None,None, V,y, vex5 chk(VEX128)),
#endif /* __Use_Original_Qemu (U79) */
        X86_OP_ENTRY3(MOVQ,       V,dq,None,None, W,q, vex5),  /* wrong dest Vq on SDM! */
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F7E);
}

static void decode_0F7F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F7F[4] = {
        X86_OP_ENTRY3(MOVDQ,       W,x, None,None, V,x, vex5 mmx), /* movq */
        X86_OP_ENTRY3(MOVDQ,       W,x, None,None, V,x, vex1), /* movdqa */
        X86_OP_ENTRY3(MOVDQ,       W,x, None,None, V,x, vex4_unal), /* movdqu */
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F7F);
}

static void decode_0FD6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry movq[4] = {
        {},
        X86_OP_ENTRY3(MOVQ,    W,dq, None, None, V,q, vex5),
        X86_OP_ENTRY3(MOVq_dq, V,dq, None, None, N,q),
        X86_OP_ENTRY3(MOVq_dq, P,q,  None, None, U,q),
    };

    *entry = *decode_by_prefix(s, movq);
}

#if __Use_Original_Qemu != 1 /* ours (U85) */
/*
 * NoVmp (ledger U85): VEX.0F38.W0 50/51 select by prefix (SDM Vol2 VPDPBUSD,
 * VPDPB[SU,UU,SS]D[,S]): NP = VPDPBUUD(S), 66 = VPDPBUSD(S) (AVX-VNNI, U71),
 * F3 = VPDPBSUD(S), F2 = VPDPBSSD(S) (AVX-VNNI-INT8). All are VEX-only.
 */
static void decode_0F38_50(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F38_50[4] = {
        X86_OP_ENTRY3(VPDPBUUD,   V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT8) p_00),
        X86_OP_ENTRY3(VPDPBUSD,   V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI) p_66),
        X86_OP_ENTRY3(VPDPBSUD,   V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT8) p_f3),
        X86_OP_ENTRY3(VPDPBSSD,   V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT8) p_f2),
    };
    *entry = *decode_by_prefix(s, opcodes_0F38_50);
}

static void decode_0F38_51(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F38_51[4] = {
        X86_OP_ENTRY3(VPDPBUUDS,  V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT8) p_00),
        X86_OP_ENTRY3(VPDPBUSDS,  V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI) p_66),
        X86_OP_ENTRY3(VPDPBSUDS,  V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT8) p_f3),
        X86_OP_ENTRY3(VPDPBSSDS,  V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT8) p_f2),
    };
    *entry = *decode_by_prefix(s, opcodes_0F38_51);
}
#endif /* __Use_Original_Qemu (U85) */

#if __Use_Original_Qemu != 1 /* ours (U175) */
/*
 * NoVmp (ledger U175..U180): VEX.128.0F38.W0 49/4B/5C/5E/6C - Intel AMX (SDM Vol2A/2B,
 * "Opcode/Instruction"). Selected by ModRM.mod and the pp prefix; anything else (legacy
 * encoding, the other pp/mod combinations) stays an unknown opcode (#UD):
 *   49 !(11):000:bbb  NP LDTILECFG m512        66 STTILECFG m512
 *   49 C0             NP TILERELEASE           49 11:rrr:000  F2 TILEZERO tmm
 *   4B !(11):rrr:100  F2 TILELOADD   66 TILELOADDT1   F3 TILESTORED  (sibmem)
 *   5C 11:rrr:bbb     F3 TDPBF16PS   F2 TDPFP16PS
 *   5E 11:rrr:bbb     F2 TDPBSSD  F3 TDPBSUD  66 TDPBUSD  NP TDPBUUD
 *   6C 11:rrr:bbb     66 TCMMIMFP16PS  NP TCMMRLFP16PS
 * The fixed ModRM.reg / r/m fields (000b, C0h, SIB) are checked by the gen_* functions.
 */
static void decode_0F38_AMX(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry amx_49_mem[4] = {
        [0] = X86_OP_ENTRYr(LDTILECFG,   M,None, vex22 cpuid(AMX_TILE) p_00),
        [1] = X86_OP_ENTRYr(STTILECFG,   M,None, vex22 cpuid(AMX_TILE) p_66),
    };
    static const X86OpEntry amx_49_reg[4] = {
        [0] = X86_OP_ENTRY0(TILERELEASE, vex22 cpuid(AMX_TILE) p_00),
#if __Use_Original_Qemu != 1 /* ours (U176) */
        [3] = X86_OP_ENTRY0(TILEZERO,    vex22 cpuid(AMX_TILE) p_f2),
#endif /* __Use_Original_Qemu (U176) */
    };
#if __Use_Original_Qemu != 1 /* ours (U176) */
    static const X86OpEntry amx_4b_mem[4] = {
        [1] = X86_OP_ENTRYr(TILELOADDT1, M,None, vex22 cpuid(AMX_TILE) p_66),
        [2] = X86_OP_ENTRYr(TILESTORED,  M,None, vex22 cpuid(AMX_TILE) p_f3),
        [3] = X86_OP_ENTRYr(TILELOADD,   M,None, vex22 cpuid(AMX_TILE) p_f2),
    };
#endif /* __Use_Original_Qemu (U176) */
#if __Use_Original_Qemu != 1 /* ours (U177) */
    static const X86OpEntry amx_5e_reg[4] = {
        [0] = X86_OP_ENTRY0(TDPBUUD,      vex22 cpuid(AMX_INT8) p_00),
        [1] = X86_OP_ENTRY0(TDPBUSD,      vex22 cpuid(AMX_INT8) p_66),
        [2] = X86_OP_ENTRY0(TDPBSUD,      vex22 cpuid(AMX_INT8) p_f3),
        [3] = X86_OP_ENTRY0(TDPBSSD,      vex22 cpuid(AMX_INT8) p_f2),
    };
#endif /* __Use_Original_Qemu (U177) */
#if __Use_Original_Qemu != 1 /* ours (U178) */
    static const X86OpEntry amx_5c_reg[4] = {
        [2] = X86_OP_ENTRY0(TDPBF16PS,    vex22 cpuid(AMX_BF16) p_f3),
#if __Use_Original_Qemu != 1 /* ours (U179) */
        [3] = X86_OP_ENTRY0(TDPFP16PS,    vex22 cpuid(AMX_FP16) p_f2),
#endif /* __Use_Original_Qemu (U179) */
    };
#endif /* __Use_Original_Qemu (U178) */
    int mod = get_modrm(s, env) >> 6;

    memset(entry, 0, sizeof(*entry));
    if (!(s->prefix & PREFIX_VEX)) {
        return;
    }
    switch (*b) {
    case 0x49:
        *entry = *decode_by_prefix(s, mod == 3 ? amx_49_reg : amx_49_mem);
        break;
#if __Use_Original_Qemu != 1 /* ours (U176) */
    case 0x4b:
        if (mod != 3) {
            *entry = *decode_by_prefix(s, amx_4b_mem);
        }
        break;
#endif /* __Use_Original_Qemu (U176) */
#if __Use_Original_Qemu != 1 /* ours (U177) */
    case 0x5e:
        if (mod == 3) {
            *entry = *decode_by_prefix(s, amx_5e_reg);
        }
        break;
#endif /* __Use_Original_Qemu (U177) */
#if __Use_Original_Qemu != 1 /* ours (U178) */
    case 0x5c:
        if (mod == 3) {
            *entry = *decode_by_prefix(s, amx_5c_reg);
        }
        break;
#endif /* __Use_Original_Qemu (U178) */
    default:
        break;
    }
}
#endif /* __Use_Original_Qemu (U175) */

#if __Use_Original_Qemu != 1 /* ours (U86) */
/*
 * NoVmp (ledger U86): AVX-VNNI-INT16, VEX.0F38.W0 D2/D3 by prefix (SDM Vol2
 * VPDPW[SU,US,UU]D[,S]): NP = VPDPWUUD(S), 66 = VPDPWUSD(S), F3 = VPDPWSUD(S);
 * F2 is undefined. VEX-only.
 */
static void decode_0F38_D2(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F38_D2[4] = {
        X86_OP_ENTRY3(VPDPWUUD,   V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT16) p_00),
        X86_OP_ENTRY3(VPDPWUSD,   V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT16) p_66),
        X86_OP_ENTRY3(VPDPWSUD,   V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT16) p_f3),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F38_D2);
}

static void decode_0F38_D3(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F38_D3[4] = {
        X86_OP_ENTRY3(VPDPWUUDS,  V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT16) p_00),
        X86_OP_ENTRY3(VPDPWUSDS,  V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT16) p_66),
        X86_OP_ENTRY3(VPDPWSUDS,  V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI_INT16) p_f3),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F38_D3);
}
#endif /* __Use_Original_Qemu (U86) */

#if __Use_Original_Qemu != 1 /* ours (U88) */
/*
 * NoVmp (ledger U88): AVX-NE-CONVERT, VEX.0F38.W0 B0/B1 by prefix (SDM Vol2):
 * B0 NP = VCVTNEOPH2PS, 66 = VCVTNEEPH2PS, F3 = VCVTNEEBF162PS,
 * F2 = VCVTNEOBF162PS (m128/m256); B1 66 = VBCSTNESH2PS, F3 = VBCSTNEBF162PS
 * (m16). All memory only (!(11):rrr:bbb), VEX only, VEX.vvvv = 1111b.
 */
static void decode_0F38_B0(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F38_B0[4] = {
        X86_OP_ENTRY3(VCVTNEOPH2PS,   V,x, None,None, WM,x, vex6 chk(W0) cpuid(AVX_NE_CONVERT) p_00),
        X86_OP_ENTRY3(VCVTNEEPH2PS,   V,x, None,None, WM,x, vex6 chk(W0) cpuid(AVX_NE_CONVERT) p_66),
        X86_OP_ENTRY3(VCVTNEEBF162PS, V,x, None,None, WM,x, vex6 chk(W0) cpuid(AVX_NE_CONVERT) p_f3),
        X86_OP_ENTRY3(VCVTNEOBF162PS, V,x, None,None, WM,x, vex6 chk(W0) cpuid(AVX_NE_CONVERT) p_f2),
    };
    *entry = *decode_by_prefix(s, opcodes_0F38_B0);
}

static void decode_0F38_B1(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F38_B1[4] = {
        {},
        X86_OP_ENTRY3(VBCSTNESH2PS,   V,x, None,None, WM,w, vex6 chk(W0) cpuid(AVX_NE_CONVERT) p_66),
        X86_OP_ENTRY3(VBCSTNEBF162PS, V,x, None,None, WM,w, vex6 chk(W0) cpuid(AVX_NE_CONVERT) p_f3),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F38_B1);
}
#endif /* __Use_Original_Qemu (U88) */

#if __Use_Original_Qemu != 1 /* ours (U100) */
/*
 * NoVmp (ledger U100): Key Locker. F3 0F 38 DC-DF share the opcode bytes of
 * the 66-prefixed AES-NI instructions: F3 (legacy) selects LOADIWKEY (DC,
 * register form) or AESENC128KL/AESDEC128KL/AESENC256KL/AESDEC256KL (memory
 * form). F3 0F 38 D8 /0-/3 (memory) are the wide forms (SDM Vol2).
 */
static void decode_0F38_DCtoDF(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry aesni[4] = {
        X86_OP_ENTRY3(VAESENC,     V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
        X86_OP_ENTRY3(VAESENCLAST, V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
        X86_OP_ENTRY3(VAESDEC,     V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
        X86_OP_ENTRY3(VAESDECLAST, V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
    };
    static const X86OpEntry loadiwkey =
        X86_OP_ENTRY3(LOADIWKEY,   V,dq, None,None, U,dq, sse4_novex cpuid(KL) p_f3);
    static const X86OpEntry aeskl =
        X86_OP_ENTRY3(AESKL,       V,dq, None,None, M,b,  sse4_novex cpuid(AESKLE) p_f3);

    if ((s->prefix & PREFIX_REPZ) && !(s->prefix & PREFIX_VEX)) {
        if ((get_modrm(s, env) >> 6) != 3) {
            *entry = aeskl;
        } else if (*b == 0xdc) {
            *entry = loadiwkey;
        } else {
            entry->gen = NULL;              /* F3 0F 38 DD-DF 11:rrr:bbb: #UD */
        }
        return;
    }
    *entry = aesni[*b - 0xdc];
}

static void decode_0F38_D8(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry wide =
        X86_OP_ENTRY3(AESKLWIDE, None,None, None,None, M,b, sse4_novex cpuid(AESKLE_WIDE) p_f3);

    if (((get_modrm(s, env) >> 3) & 7) < 4) {
        *entry = wide;
    } else {
        entry->gen = NULL;                  /* /4-/7 reserved: #UD */
    }
}

#endif /* __Use_Original_Qemu (U100) */
static const X86OpEntry opcodes_0F38_00toEF[240] = {
    [0x00] = X86_OP_ENTRY3(PSHUFB,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x01] = X86_OP_ENTRY3(PHADDW,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x02] = X86_OP_ENTRY3(PHADDD,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x03] = X86_OP_ENTRY3(PHADDSW,   V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x04] = X86_OP_ENTRY3(PMADDUBSW, V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x05] = X86_OP_ENTRY3(PHSUBW,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x06] = X86_OP_ENTRY3(PHSUBD,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x07] = X86_OP_ENTRY3(PHSUBSW,   V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),

#if __Use_Original_Qemu == 1 /* original QEMU (U79) */
    [0x10] = X86_OP_ENTRY2(PBLENDVB,  V,x,         W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
#else /* ours (U79) */
    /* legacy only: the VEX forms are 0F3A 4C/4A/4B (SDM Vol2) (U79) */
    [0x10] = X86_OP_ENTRY2(PBLENDVB,  V,x,         W,x,  sse4_novex cpuid(SSE41) p_66),
#endif /* __Use_Original_Qemu (U79) */
    [0x13] = X86_OP_ENTRY2(VCVTPH2PS, V,x,         W,xh, vex11 chk(W0) cpuid(F16C) p_66),
#if __Use_Original_Qemu == 1 /* original QEMU (U79) */
    [0x14] = X86_OP_ENTRY2(BLENDVPS,  V,x,         W,x,  vex4 cpuid(SSE41) p_66),
    [0x15] = X86_OP_ENTRY2(BLENDVPD,  V,x,         W,x,  vex4 cpuid(SSE41) p_66),
#else /* ours (U79) */
    [0x14] = X86_OP_ENTRY2(BLENDVPS,  V,x,         W,x,  sse4_novex cpuid(SSE41) p_66),
    [0x15] = X86_OP_ENTRY2(BLENDVPD,  V,x,         W,x,  sse4_novex cpuid(SSE41) p_66),
#endif /* __Use_Original_Qemu (U79) */
    /* Listed incorrectly as type 4 */
    [0x16] = X86_OP_ENTRY3(VPERMD,    V,qq, H,qq,      W,qq,  vex6 chk(W0) cpuid(AVX2) p_66), /* vpermps */
    [0x17] = X86_OP_ENTRY3(VPTEST,    None,None, V,x,  W,x,   vex4 cpuid(SSE41) p_66),

    /*
     * Source operand listed as Mq/Ux and similar in the manual; incorrectly listed
     * as 128-bit only in 2-17.
     */
    [0x20] = X86_OP_ENTRY3(VPMOVSXBW, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x21] = X86_OP_ENTRY3(VPMOVSXBD, V,x,  None,None, W,d,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x22] = X86_OP_ENTRY3(VPMOVSXBQ, V,x,  None,None, W,w,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x23] = X86_OP_ENTRY3(VPMOVSXWD, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x24] = X86_OP_ENTRY3(VPMOVSXWQ, V,x,  None,None, W,d,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x25] = X86_OP_ENTRY3(VPMOVSXDQ, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),

    /* Same as PMOVSX.  */
    [0x30] = X86_OP_ENTRY3(VPMOVZXBW, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x31] = X86_OP_ENTRY3(VPMOVZXBD, V,x,  None,None, W,d,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x32] = X86_OP_ENTRY3(VPMOVZXBQ, V,x,  None,None, W,w,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x33] = X86_OP_ENTRY3(VPMOVZXWD, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x34] = X86_OP_ENTRY3(VPMOVZXWQ, V,x,  None,None, W,d,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x35] = X86_OP_ENTRY3(VPMOVZXDQ, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x36] = X86_OP_ENTRY3(VPERMD,    V,qq, H,qq,      W,qq,  vex6 chk(W0) cpuid(AVX2) p_66),
    [0x37] = X86_OP_ENTRY3(PCMPGTQ,   V,x,  H,x,       W,x,   vex4 cpuid(SSE42) avx2_256 p_66),

    [0x40] = X86_OP_ENTRY3(PMULLD,      V,x,  H,x,       W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x41] = X86_OP_ENTRY3(VPHMINPOSUW, V,dq, None,None, W,dq, vex4 cpuid(SSE41) p_66),
    /* Listed incorrectly as type 4 */
    [0x45] = X86_OP_ENTRY3(VPSRLV,      V,x,  H,x,       W,x,  vex6 cpuid(AVX2) p_66),
    [0x46] = X86_OP_ENTRY3(VPSRAV,      V,x,  H,x,       W,x,  vex6 chk(W0) cpuid(AVX2) p_66),
    [0x47] = X86_OP_ENTRY3(VPSLLV,      V,x,  H,x,       W,x,  vex6 cpuid(AVX2) p_66),
#if __Use_Original_Qemu != 1 /* ours (U175) */
    /* Intel AMX (VEX only, decode_0F38_AMX) */
    [0x49] = X86_OP_GROUP0(0F38_AMX),
#endif /* __Use_Original_Qemu (U175) */
#if __Use_Original_Qemu != 1 /* ours (U176) */
    [0x4b] = X86_OP_GROUP0(0F38_AMX),
#endif /* __Use_Original_Qemu (U176) */
#if __Use_Original_Qemu != 1 /* ours (U178) */
    [0x5c] = X86_OP_GROUP0(0F38_AMX),
#endif /* __Use_Original_Qemu (U178) */
#if __Use_Original_Qemu != 1 /* ours (U177) */
    [0x5e] = X86_OP_GROUP0(0F38_AMX),
#endif /* __Use_Original_Qemu (U177) */

    [0x90] = X86_OP_ENTRY3(VPGATHERD, V,x,  H,x,  M,d,  vex12 cpuid(AVX2) p_66), /* vpgatherdd/q */
    [0x91] = X86_OP_ENTRY3(VPGATHERQ, V,x,  H,x,  M,q,  vex12 cpuid(AVX2) p_66), /* vpgatherqd/q */
    [0x92] = X86_OP_ENTRY3(VPGATHERD, V,x,  H,x,  M,d,  vex12 cpuid(AVX2) p_66), /* vgatherdps/d */
    [0x93] = X86_OP_ENTRY3(VPGATHERQ, V,x,  H,x,  M,q,  vex12 cpuid(AVX2) p_66), /* vgatherqps/d */

    /* Should be exception type 2 but they do not have legacy SSE equivalents? */
    [0x96] = X86_OP_ENTRY3(VFMADDSUB132Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x97] = X86_OP_ENTRY3(VFMSUBADD132Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xa6] = X86_OP_ENTRY3(VFMADDSUB213Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xa7] = X86_OP_ENTRY3(VFMSUBADD213Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xb6] = X86_OP_ENTRY3(VFMADDSUB231Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xb7] = X86_OP_ENTRY3(VFMSUBADD231Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0x08] = X86_OP_ENTRY3(PSIGNB,    V,x,        H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x09] = X86_OP_ENTRY3(PSIGNW,    V,x,        H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x0a] = X86_OP_ENTRY3(PSIGND,    V,x,        H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x0b] = X86_OP_ENTRY3(PMULHRSW,  V,x,        H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    /* Listed incorrectly as type 4 */
    [0x0c] = X86_OP_ENTRY3(VPERMILPS, V,x,        H,x,  W,x,  vex6 chk(W0) cpuid(AVX) p_66),
    [0x0d] = X86_OP_ENTRY3(VPERMILPD, V,x,        H,x,  W,x,  vex6 chk(W0) cpuid(AVX) p_66),
    [0x0e] = X86_OP_ENTRY3(VTESTPS,   None,None,  V,x,  W,x,  vex6 chk(W0) cpuid(AVX) p_66),
    [0x0f] = X86_OP_ENTRY3(VTESTPD,   None,None,  V,x,  W,x,  vex6 chk(W0) cpuid(AVX) p_66),

    [0x18] = X86_OP_ENTRY3(VPBROADCASTD,   V,x,  None,None, W,d,  vex6 chk(W0) cpuid(AVX) p_66), /* vbroadcastss */
    [0x19] = X86_OP_ENTRY3(VPBROADCASTQ,   V,qq, None,None, W,q,  vex6 chk(W0) cpuid(AVX) p_66), /* vbroadcastsd */
    [0x1a] = X86_OP_ENTRY3(VBROADCASTx128, V,qq, None,None, WM,dq,vex6 chk(W0) cpuid(AVX) p_66),
    [0x1c] = X86_OP_ENTRY3(PABSB,          V,x,  None,None, W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x1d] = X86_OP_ENTRY3(PABSW,          V,x,  None,None, W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x1e] = X86_OP_ENTRY3(PABSD,          V,x,  None,None, W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),

    [0x28] = X86_OP_ENTRY3(PMULDQ,        V,x, H,x,       W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x29] = X86_OP_ENTRY3(PCMPEQQ,       V,x, H,x,       W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x2a] = X86_OP_ENTRY3(MOVDQ,         V,x, None,None, WM,x, vex1 cpuid(SSE41) avx2_256 p_66), /* movntdqa */
    [0x2b] = X86_OP_ENTRY3(VPACKUSDW,     V,x, H,x,       W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x2c] = X86_OP_ENTRY3(VMASKMOVPS,    V,x, H,x,       WM,x, vex6 chk(W0) cpuid(AVX) p_66),
    [0x2d] = X86_OP_ENTRY3(VMASKMOVPD,    V,x, H,x,       WM,x, vex6 chk(W0) cpuid(AVX) p_66),
    /* Incorrectly listed as Mx,Hx,Vx in the manual */
    [0x2e] = X86_OP_ENTRY3(VMASKMOVPS_st, M,x, V,x,       H,x,  vex6 chk(W0) cpuid(AVX) p_66),
    [0x2f] = X86_OP_ENTRY3(VMASKMOVPD_st, M,x, V,x,       H,x,  vex6 chk(W0) cpuid(AVX) p_66),

    [0x38] = X86_OP_ENTRY3(PMINSB,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x39] = X86_OP_ENTRY3(PMINSD,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3a] = X86_OP_ENTRY3(PMINUW,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3b] = X86_OP_ENTRY3(PMINUD,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3c] = X86_OP_ENTRY3(PMAXSB,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3d] = X86_OP_ENTRY3(PMAXSD,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3e] = X86_OP_ENTRY3(PMAXUW,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3f] = X86_OP_ENTRY3(PMAXUD,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),

    /* VPBROADCASTQ not listed as W0 in table 2-16 */
    [0x58] = X86_OP_ENTRY3(VPBROADCASTD,   V,x,  None,None, W,d,  vex6 chk(W0) cpuid(AVX2) p_66),
    [0x59] = X86_OP_ENTRY3(VPBROADCASTQ,   V,x,  None,None, W,q,  vex6 chk(W0) cpuid(AVX2) p_66),
    [0x5a] = X86_OP_ENTRY3(VBROADCASTx128, V,qq, None,None, WM,dq,vex6 chk(W0) cpuid(AVX2) p_66),

#if __Use_Original_Qemu != 1 /* ours (U88) */
    /*
     * VCVTNEPS2BF16 (U88): VEX.F3.0F38.W0 72, xmm1 <- xmm2/m128 (L0) or
     * ymm2/m256 (L1); the helper writes the whole YMM (upper half zero).
     */
    [0x72] = X86_OP_ENTRY3(VCVTNEPS2BF16,  V,x,  None,None, W,x,  vex6 chk(W0) cpuid(AVX_NE_CONVERT) p_f3),
#endif /* __Use_Original_Qemu (U88) */
    [0x78] = X86_OP_ENTRY3(VPBROADCASTB,   V,x,  None,None, W,b,  vex6 chk(W0) cpuid(AVX2) p_66),
    [0x79] = X86_OP_ENTRY3(VPBROADCASTW,   V,x,  None,None, W,w,  vex6 chk(W0) cpuid(AVX2) p_66),

#if __Use_Original_Qemu != 1 /* ours (U102) */
    /*
     * MOVRS r8, m8 / MOVRS rv, mv: NOREP 0F 38 8A/8B (ISE 319433-062), a load
     * with a read-shared hint. 64-bit mode only; F2/F3/LOCK/VEX/mod=3 #UD.
     */
    [0x8a] = X86_OP_ENTRY3(MOVRS,        G,b,  None,None, M,b, cpuid(MOVRS) o64 p_00_66),
    [0x8b] = X86_OP_ENTRY3(MOVRS,        G,v,  None,None, M,v, cpuid(MOVRS) o64 p_00_66),
#endif /* __Use_Original_Qemu (U102) */
    [0x8c] = X86_OP_ENTRY3(VPMASKMOV,    V,x,  H,x, WM,x, vex6 cpuid(AVX2) p_66),
    [0x8e] = X86_OP_ENTRY3(VPMASKMOV_st, M,x,  V,x, H,x,  vex6 cpuid(AVX2) p_66),

    /* Should be exception type 2 or 3 but they do not have legacy SSE equivalents? */
    [0x98] = X86_OP_ENTRY3(VFMADD132Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x99] = X86_OP_ENTRY3(VFMADD132Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9a] = X86_OP_ENTRY3(VFMSUB132Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9b] = X86_OP_ENTRY3(VFMSUB132Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9c] = X86_OP_ENTRY3(VFNMADD132Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9d] = X86_OP_ENTRY3(VFNMADD132Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9e] = X86_OP_ENTRY3(VFNMSUB132Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9f] = X86_OP_ENTRY3(VFNMSUB132Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xa8] = X86_OP_ENTRY3(VFMADD213Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xa9] = X86_OP_ENTRY3(VFMADD213Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xaa] = X86_OP_ENTRY3(VFMSUB213Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xab] = X86_OP_ENTRY3(VFMSUB213Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xac] = X86_OP_ENTRY3(VFNMADD213Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xad] = X86_OP_ENTRY3(VFNMADD213Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xae] = X86_OP_ENTRY3(VFNMSUB213Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xaf] = X86_OP_ENTRY3(VFNMSUB213Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

#if __Use_Original_Qemu != 1 /* ours (U88) */
    /* AVX-NE-CONVERT (U88): B0/B1 by prefix, memory only */
    [0xb0] = X86_OP_GROUP0(0F38_B0),
    [0xb1] = X86_OP_GROUP0(0F38_B1),
#endif /* __Use_Original_Qemu (U88) */
#if __Use_Original_Qemu != 1 /* ours (U87) */
    /* AVX-IFMA (U87): VEX.66.0F38.W1 B4/B5, VEX only (EVEX is AVX512-IFMA) */
    [0xb4] = X86_OP_ENTRY3(VPMADD52LUQ,  V,x,  H,x, W,x,  vex6 chk(W1) cpuid(AVX_IFMA) p_66),
    [0xb5] = X86_OP_ENTRY3(VPMADD52HUQ,  V,x,  H,x, W,x,  vex6 chk(W1) cpuid(AVX_IFMA) p_66),
#endif /* __Use_Original_Qemu (U87) */
    [0xb8] = X86_OP_ENTRY3(VFMADD231Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xb9] = X86_OP_ENTRY3(VFMADD231Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xba] = X86_OP_ENTRY3(VFMSUB231Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbb] = X86_OP_ENTRY3(VFMSUB231Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbc] = X86_OP_ENTRY3(VFNMADD231Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbd] = X86_OP_ENTRY3(VFNMADD231Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbe] = X86_OP_ENTRY3(VFNMSUB231Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbf] = X86_OP_ENTRY3(VFNMSUB231Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xc8] = X86_OP_ENTRY2(SHA1NEXTE,   V,dq, W,dq, sse4_novex cpuid(SHA_NI) p_00),
    [0xc9] = X86_OP_ENTRY2(SHA1MSG1,    V,dq, W,dq, sse4_novex cpuid(SHA_NI) p_00),
    [0xca] = X86_OP_ENTRY2(SHA1MSG2,    V,dq, W,dq, sse4_novex cpuid(SHA_NI) p_00),
    [0xcb] = X86_OP_ENTRY2(SHA256RNDS2, V,dq, W,dq, sse4_novex cpuid(SHA_NI) p_00),
    [0xcc] = X86_OP_ENTRY2(SHA256MSG1,  V,dq, W,dq, sse4_novex cpuid(SHA_NI) p_00),
#if __Use_Original_Qemu != 1 /* ours (U71) */
    /* AVX-VNNI (U71): VEX.66.0F38.W0 50-53 */
    /* 50/51: the 66 forms moved into decode_0F38_50/51 with AVX-VNNI-INT8 (U85) */
    [0x50] = X86_OP_GROUP0(0F38_50),
    [0x51] = X86_OP_GROUP0(0F38_51),
    [0x52] = X86_OP_ENTRY3(VPDPWSSD,    V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI) p_66),
    [0x53] = X86_OP_ENTRY3(VPDPWSSDS,   V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX_VNNI) p_66),
#endif /* __Use_Original_Qemu (U71) */
    [0xcd] = X86_OP_ENTRY2(SHA256MSG2,  V,dq, W,dq, sse4_novex cpuid(SHA_NI) p_00),
#if __Use_Original_Qemu != 1 /* ours (U70) */
    /* GFNI (U70): 66 0F 38 CF / VEX.66.0F38.W0 CF */
    [0xcf] = X86_OP_ENTRY3(GF2P8MULB,   V,x,  H,x,  W,x,  vex4 chk(VEX_W0) cpuid(GFNI) p_66),
#endif /* __Use_Original_Qemu (U70) */

#if __Use_Original_Qemu != 1 /* ours (U86) */
    [0xd2] = X86_OP_GROUP0(0F38_D2),
    [0xd3] = X86_OP_GROUP0(0F38_D3),
#endif /* __Use_Original_Qemu (U86) */
    [0xdb] = X86_OP_ENTRY3(VAESIMC,     V,dq, None,None, W,dq, vex4 cpuid(AES) p_66),
#if __Use_Original_Qemu == 1 /* original QEMU (U100) */
    [0xdc] = X86_OP_ENTRY3(VAESENC,     V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
    [0xdd] = X86_OP_ENTRY3(VAESENCLAST, V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
    [0xde] = X86_OP_ENTRY3(VAESDEC,     V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
    [0xdf] = X86_OP_ENTRY3(VAESDECLAST, V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
#else /* ours (U100) */
    /* 66: AES-NI; F3: Key Locker (decode_0F38_DCtoDF, decode_0F38_D8) */
    [0xd8] = X86_OP_GROUP0(0F38_D8),
    [0xdc] = X86_OP_GROUP0(0F38_DCtoDF),
    [0xdd] = X86_OP_GROUP0(0F38_DCtoDF),
    [0xde] = X86_OP_GROUP0(0F38_DCtoDF),
    [0xdf] = X86_OP_GROUP0(0F38_DCtoDF),
#endif /* __Use_Original_Qemu (U100) */

    /*
     * REG selects srcdest2 operand, VEX.vvvv selects src3.  VEX class not found
     * in manual, assumed to be 13 from the VEX.L0 constraint.
     * 7.2's decoder gives M operands no unit, which its LOCK path rejects, so no
     * "xchg" special: gen_CMPccXADD does the load/compare/select/store itself
     * (Unicorn is never parallel) and also performs the o64 check.
     */
    [0xe0] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xe1] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xe2] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xe3] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xe4] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xe5] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xe6] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xe7] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),

    [0xe8] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xe9] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xea] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xeb] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xec] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xed] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xee] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
    [0xef] = X86_OP_ENTRY3(CMPccXADD,   M,y, G,y, B,y,  vex13 cpuid(CMPCCXADD) p_66),
};

#if __Use_Original_Qemu != 1 /* ours (U103, U112) */
/* F3 0F 38 F8: 11:rrr:bbb UWRMSR r64, r64 (U103); memory ENQCMDS r, m512 (U112) */
static void decode_0F38_F8_F3(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry uwrmsr = X86_OP_ENTRY3(UWRMSR, None,None, G,q, R,q, cpuid(USER_MSR) o64);
    static const X86OpEntry enqcmds = X86_OP_ENTRY3(ENQCMDS, None,None, G,y, M,b, cpuid(ENQCMD));

    *entry = ((get_modrm(s, env) >> 6) == 3) ? uwrmsr : enqcmds;
}

/* F2 0F 38 F8: 11:rrr:bbb URDMSR r64, r64 (U103); memory ENQCMD r, m512 (U112) */
static void decode_0F38_F8_F2(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry urdmsr = X86_OP_ENTRY3(URDMSR, R,q, G,q, None,None, cpuid(USER_MSR) o64);
    static const X86OpEntry enqcmd = X86_OP_ENTRY3(ENQCMD, None,None, G,y, M,b, cpuid(ENQCMD));

    *entry = ((get_modrm(s, env) >> 6) == 3) ? urdmsr : enqcmd;
}
#endif /* __Use_Original_Qemu (U103, U112) */

/* five rows for no prefix, 66, F3, F2, 66+F2  */
static const X86OpEntry opcodes_0F38_F0toFF[16][5] = {
    /*
     * MOVBE and CRC32 are incorrectly listed in SDM Table A-4 as always doing
     * 32-bit operation without prefix and 16-bit operation with 0x66; they
     * really use the operand-size attribute (v).  QEMU 76ad26dd17.
     */
    [0] = {
        X86_OP_ENTRY3(MOVBE, G,v, M,v, None,None, cpuid(MOVBE)),
        X86_OP_ENTRY3(MOVBE, G,v, M,v, None,None, cpuid(MOVBE)),
        {},
        X86_OP_ENTRY2(CRC32, G,d, E,b, cpuid(SSE42)),
        X86_OP_ENTRY2(CRC32, G,d, E,b, cpuid(SSE42)),
    },
    [1] = {
        X86_OP_ENTRY3(MOVBE, M,v, G,v, None,None, cpuid(MOVBE)),
        X86_OP_ENTRY3(MOVBE, M,v, G,v, None,None, cpuid(MOVBE)),
        {},
        X86_OP_ENTRY2(CRC32, G,d, E,v, cpuid(SSE42)),
        X86_OP_ENTRY2(CRC32, G,d, E,v, cpuid(SSE42)),
    },
    [2] = {
        X86_OP_ENTRY3(ANDN, G,y, B,y, E,y, vex13 cpuid(BMI1)),
        {},
        {},
        {},
        {},
    },
    [3] = {
        X86_OP_GROUP3(group17, B,y, E,y, None,None, vex13 cpuid(BMI1)),
        {},
        {},
        {},
        {},
    },
#if __Use_Original_Qemu == 1 /* original QEMU (U114) */
    [5] = {
        X86_OP_ENTRY3(BZHI, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        {},
        X86_OP_ENTRY3(PEXT, G,y, B,y, E,y, vex13 cpuid(BMI2)),
        X86_OP_ENTRY3(PDEP, G,y, B,y, E,y, vex13 cpuid(BMI2)),
        {},
    },
    [6] = {
        {},
        X86_OP_ENTRY2(ADCX, G,y, E,y, cpuid(ADX)),
#else /* ours (U114) */
    /* WRUSSD/WRUSSQ m, r: 66 0F 38 F5 (U114) */
    [5] = {
        X86_OP_ENTRY3(BZHI, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        X86_OP_ENTRY3(WRUSS, M,y, G,y, None,None, cpuid(CET_SS)),
        X86_OP_ENTRY3(PEXT, G,y, B,y, E,y, vex13 cpuid(BMI2)),
        X86_OP_ENTRY3(PDEP, G,y, B,y, E,y, vex13 cpuid(BMI2)),
        {},
    },
    /* WRSSD/WRSSQ m, r: NP 0F 38 F6 (U114) */
    [6] = {
        X86_OP_ENTRY3(WRSS, M,y, G,y, None,None, cpuid(CET_SS)),
        X86_OP_ENTRY2(ADCX, G,y, E,y, cpuid(ADX)),
#endif /* __Use_Original_Qemu (U114) */
        X86_OP_ENTRY2(ADOX, G,y, E,y, cpuid(ADX)),
        X86_OP_ENTRY3(MULX, /* B,y, */ G,y, E,y, 2,y, vex13 cpuid(BMI2)),
        {},
    },
#if __Use_Original_Qemu != 1 /* ours (U72) */
#if __Use_Original_Qemu != 1 /* ours (U73) */
    /* MOVDIR64B r16/r32/r64, m512: 66 0F 38 F8 (U73); F3 ENQCMDS, F2 ENQCMD (U112) */
    [8] = {
        {},
        X86_OP_ENTRY3(MOVDIR64B, None,None, G,y, M,b, cpuid(MOVDIR64B)),
        /*
         * F3 / F2 split on ModRM.mod (U103 + U112): 11 = UWRMSR / URDMSR r64, r64,
         * memory = ENQCMDS / ENQCMD r, m512 (SDM Vol2).
         */
        X86_OP_GROUP0(0F38_F8_F3),
        X86_OP_GROUP0(0F38_F8_F2),
        {},
    },
#endif /* __Use_Original_Qemu (U73) */
    /* MOVDIRI m32/m64, r32/r64: NP 0F 38 F9 (U72) */
    [9] = {
        X86_OP_ENTRY3(MOVDIRI, M,y, G,y, None,None, cpuid(MOVDIRI)),
        {},
        {},
        {},
        {},
    },
#endif /* __Use_Original_Qemu (U72) */
#if __Use_Original_Qemu != 1 /* ours (U100) */
    /* ENCODEKEY128 / ENCODEKEY256 r32, r32: F3 0F 38 FA/FB 11:rrr:bbb (U100) */
    [10] = {
        {},
        {},
        X86_OP_ENTRY3(ENCODEKEY128, G,d, None,None, R,d, sse4_novex cpuid(AESKLE)),
        {},
        {},
    },
    [11] = {
        {},
        {},
        X86_OP_ENTRY3(ENCODEKEY256, G,d, None,None, R,d, sse4_novex cpuid(AESKLE)),
        {},
        {},
    },
#endif /* __Use_Original_Qemu (U100) */
#if __Use_Original_Qemu != 1 /* ours (U101) */
    /*
     * RAO-INT (U101): NP AADD, 66 AAND, F3 AXOR, F2 AOR my, ry (0F 38 FC,
     * memory only; 32-bit, REX.W 64-bit). F3/F2 take priority over 66 as
     * for the SSE mandatory prefixes. LOCK #UDs (op0 is not an INT unit).
     */
    [12] = {
        X86_OP_ENTRY3(AADD, M,y, G,y, None,None, cpuid(RAO_INT)),
        X86_OP_ENTRY3(AAND, M,y, G,y, None,None, cpuid(RAO_INT)),
        X86_OP_ENTRY3(AXOR, M,y, G,y, None,None, cpuid(RAO_INT)),
        X86_OP_ENTRY3(AOR,  M,y, G,y, None,None, cpuid(RAO_INT)),
        X86_OP_ENTRY3(AOR,  M,y, G,y, None,None, cpuid(RAO_INT)),
    },
#endif /* __Use_Original_Qemu (U101) */
    [7] = {
        X86_OP_ENTRY3(BEXTR, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        X86_OP_ENTRY3(SHLX, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        X86_OP_ENTRY3(SARX, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        X86_OP_ENTRY3(SHRX, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        {},
    },
};

static void decode_0F38(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    if (*b < 0xf0) {
        *entry = opcodes_0F38_00toEF[*b];
    } else {
        int row = 0;
        if (s->prefix & PREFIX_REPZ) {
            /* The REPZ (F3) prefix has priority over 66 */
            row = 2;
        } else {
            row += s->prefix & PREFIX_REPNZ ? 3 : 0;
            row += s->prefix & PREFIX_DATA ? 1 : 0;
        }
        *entry = opcodes_0F38_F0toFF[*b & 15][row];
    }
#if __Use_Original_Qemu != 1 /* ours (U82) */
    /*
     * NoVmp (ledger U82): VEX.256.F2.0F38.W0 CB/CC/CD 11:rrr:bbb are
     * VSHA512RNDS2/VSHA512MSG1/VSHA512MSG2 (SDM Vol2, CPUID.(7,1):EAX.SHA512);
     * the legacy NP 0F 38 CB-CD (SHA-NI) entries above stay as they are.
     * Register-only (U), VEX.256 only (qq), W0, MSG1/MSG2 do not use vvvv.
     */
    if ((s->prefix & PREFIX_VEX) && *b >= 0xcb && *b <= 0xcd) {
        static const X86OpEntry vsha512[3] = {
            X86_OP_ENTRY3(VSHA512RNDS2, V,qq, H,qq, U,dq, vex6 chk(W0) cpuid(SHA512) p_f2),
            X86_OP_ENTRY2(VSHA512MSG1,  V,qq, U,dq,       vex6 chk(W0) cpuid(SHA512) p_f2),
            X86_OP_ENTRY2(VSHA512MSG2,  V,qq, U,qq,       vex6 chk(W0) cpuid(SHA512) p_f2),
        };
        *entry = vsha512[*b - 0xcb];
    }
#endif /* __Use_Original_Qemu (U82) */
#if __Use_Original_Qemu != 1 /* ours (U83) */
    /*
     * NoVmp (ledger U83): 0F38 DA is selected by the VEX pp field (SDM Vol2):
     * NP VSM3MSG1, 66 VSM3MSG2 (VEX.128.W0, SM3). VEX only (vex6: the legacy
     * encodings are #UD); SM3 is VEX.128 only (dq: VEX.L1 is #UD).
     */
    if (*b == 0xda) {
        static const X86OpEntry opcodes_0F38_DA[4] = {
            [0] = X86_OP_ENTRY3(VSM3MSG1, V,dq, H,dq, W,dq, vex6 chk(W0) cpuid(SM3) p_00),
            [1] = X86_OP_ENTRY3(VSM3MSG2, V,dq, H,dq, W,dq, vex6 chk(W0) cpuid(SM3) p_66),
#if __Use_Original_Qemu != 1 /* ours (U84) */
            /* F3 VSM4KEY4, F2 VSM4RNDS4: VEX.128/256.W0, independent 128-bit lanes */
            [2] = X86_OP_ENTRY3(VSM4KEY4,  V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(SM4) p_f3),
            [3] = X86_OP_ENTRY3(VSM4RNDS4, V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(SM4) p_f2),
#endif /* __Use_Original_Qemu (U84) */
        };
        *entry = *decode_by_prefix(s, opcodes_0F38_DA);
    }
#endif /* __Use_Original_Qemu (U83) */
}

static void decode_VINSERTPS(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry
        vinsertps_reg = X86_OP_ENTRY4(VINSERTPS_r, V,dq, H,dq, U,dq, vex5 cpuid(SSE41) p_66),
        vinsertps_mem = X86_OP_ENTRY4(VINSERTPS_m, V,dq, H,dq, M,d,  vex5 cpuid(SSE41) p_66);

    int modrm = get_modrm(s, env);
    *entry = (modrm >> 6) == 3 ? vinsertps_reg : vinsertps_mem;
}

#if __Use_Original_Qemu != 1 /* ours (U133) */
/*
 * NoVmp (ledger U133): KSHIFTR 30/31, KSHIFTL 32/33 k1, k2, imm8 (SDM Vol2A), VEX.66.0F3A,
 * RRI, VEX.L0, vvvv 1111b, ModRM.mod = 11b (K20). 30/32: W1 = W (AVX512F), W0 = B (DQ);
 * 31/33: W1 = Q (BW), W0 = D (BW); NP/F3/F2 and legacy 0F 3A 30-33 #UD.
 */
static void decode_K_pp_w(DisasContext *s, X86OpEntry *entry, const X86OpEntry entries[8],
                          bool w);
#define KSH(op, sz, feat) X86_OP_ENTRY3(op, K,sz, KU,sz, I,b, vex20 chk(VEX128) cpuid(feat))
static void decode_0F3A_KSHIFT(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    /* { W0: NP, 66, F3, F2, W1: NP, 66, F3, F2 } */
    static const X86OpEntry k30[8] = {
        X86_OP_NONE KSH(KSHIFTR, b, AVX512DQ), X86_OP_NONE X86_OP_NONE
        X86_OP_NONE KSH(KSHIFTR, w, AVX512F), X86_OP_NONE X86_OP_NONE
    };
    static const X86OpEntry k31[8] = {
        X86_OP_NONE KSH(KSHIFTR, d, AVX512BW), X86_OP_NONE X86_OP_NONE
        X86_OP_NONE KSH(KSHIFTR, q, AVX512BW), X86_OP_NONE X86_OP_NONE
    };
    static const X86OpEntry k32[8] = {
        X86_OP_NONE KSH(KSHIFTL, b, AVX512DQ), X86_OP_NONE X86_OP_NONE
        X86_OP_NONE KSH(KSHIFTL, w, AVX512F), X86_OP_NONE X86_OP_NONE
    };
    static const X86OpEntry k33[8] = {
        X86_OP_NONE KSH(KSHIFTL, d, AVX512BW), X86_OP_NONE X86_OP_NONE
        X86_OP_NONE KSH(KSHIFTL, q, AVX512BW), X86_OP_NONE X86_OP_NONE
    };
    static const X86OpEntry *const k[4] = { k30, k31, k32, k33 };

    decode_K_pp_w(s, entry, k[*b - 0x30], s->vex_w);
}
#undef KSH
#endif /* __Use_Original_Qemu (U133) */

static const X86OpEntry opcodes_0F3A[256] = {
    /*
     * These are VEX-only, but incorrectly listed in the manual as exception type 4.
     * Also the "qq" instructions are sometimes omitted by Table 2-17, but are VEX256
     * only.
     */
    [0x00] = X86_OP_ENTRY3(VPERMQ,      V,qq, W,qq, I,b,  vex6 chk(W1) cpuid(AVX2) p_66),
    [0x01] = X86_OP_ENTRY3(VPERMQ,      V,qq, W,qq, I,b,  vex6 chk(W1) cpuid(AVX2) p_66), /* VPERMPD */
    [0x02] = X86_OP_ENTRY4(VBLENDPS,    V,x,  H,x,  W,x,  vex6 chk(W0) cpuid(AVX2) p_66), /* VPBLENDD */
    [0x04] = X86_OP_ENTRY3(VPERMILPS_i, V,x,  W,x,  I,b,  vex6 chk(W0) cpuid(AVX) p_66),
    [0x05] = X86_OP_ENTRY3(VPERMILPD_i, V,x,  W,x,  I,b,  vex6 chk(W0) cpuid(AVX) p_66),
    [0x06] = X86_OP_ENTRY4(VPERM2x128,  V,qq, H,qq, W,qq, vex6 chk(W0) cpuid(AVX) p_66),

    [0x14] = X86_OP_ENTRY3(PEXTRB,     E,b,  V,dq, I,b,  vex5 cpuid(SSE41) zext0 p_66),
    [0x15] = X86_OP_ENTRY3(PEXTRW,     E,w,  V,dq, I,b,  vex5 cpuid(SSE41) zext0 p_66),
    [0x16] = X86_OP_ENTRY3(PEXTR,      E,y,  V,dq, I,b,  vex5 cpuid(SSE41) p_66),
    [0x17] = X86_OP_ENTRY3(VEXTRACTPS, E,d,  V,dq, I,b,  vex5 cpuid(SSE41) p_66),
    [0x1d] = X86_OP_ENTRY3(VCVTPS2PH,  W,xh, V,x,  I,b,  vex11 chk(W0) cpuid(F16C) p_66),

    [0x20] = X86_OP_ENTRY4(PINSRB,     V,dq, H,dq, E,b,  vex5 cpuid(SSE41) zext2 p_66),
    [0x21] = X86_OP_GROUP0(VINSERTPS),
    [0x22] = X86_OP_ENTRY4(PINSR,      V,dq, H,dq, E,y,  vex5 cpuid(SSE41) p_66),

    [0x40] = X86_OP_ENTRY4(VDPPS,      V,x,  H,x,  W,x,  vex2 cpuid(SSE41) p_66),
    [0x41] = X86_OP_ENTRY4(VDPPD,      V,dq, H,dq, W,dq, vex2 cpuid(SSE41) p_66),
    [0x42] = X86_OP_ENTRY4(VMPSADBW,   V,x,  H,x,  W,x,  vex2 cpuid(SSE41) avx2_256 p_66),
#if __Use_Original_Qemu == 1 /* original QEMU (U69) */
    [0x44] = X86_OP_ENTRY4(PCLMULQDQ,  V,dq, H,dq, W,dq, vex4 cpuid(PCLMULQDQ) p_66),
#else /* ours (U69) */
    /* VEX.256 VPCLMULQDQ: two lanes (CPUID.7.0:ECX.VPCLMULQDQ, U69) */
    [0x44] = X86_OP_ENTRY4(PCLMULQDQ,  V,x, H,x, W,x, vex4 cpuid(PCLMULQDQ) p_66),
#endif /* __Use_Original_Qemu (U69) */
#if __Use_Original_Qemu != 1 /* ours (U70) */
    /* GFNI (U70): 66 0F 3A CE/CF ib / VEX.66.0F3A.W1 CE/CF ib */
    [0xce] = X86_OP_ENTRY4(GF2P8AFFINEQB,    V,x, H,x, W,x, vex4 chk(VEX_W1) cpuid(GFNI) p_66),
    [0xcf] = X86_OP_ENTRY4(GF2P8AFFINEINVQB, V,x, H,x, W,x, vex4 chk(VEX_W1) cpuid(GFNI) p_66),
#endif /* __Use_Original_Qemu (U70) */
    [0x46] = X86_OP_ENTRY4(VPERM2x128, V,qq, H,qq, W,qq, vex6 chk(W0) cpuid(AVX2) p_66),

    [0x60] = X86_OP_ENTRY4(PCMPESTRM,  None,None, V,dq, W,dq, vex4_unal cpuid(SSE42) p_66),
    [0x61] = X86_OP_ENTRY4(PCMPESTRI,  None,None, V,dq, W,dq, vex4_unal cpuid(SSE42) p_66),
    [0x62] = X86_OP_ENTRY4(PCMPISTRM,  None,None, V,dq, W,dq, vex4_unal cpuid(SSE42) p_66),
    [0x63] = X86_OP_ENTRY4(PCMPISTRI,  None,None, V,dq, W,dq, vex4_unal cpuid(SSE42) p_66),

    [0x08] = X86_OP_ENTRY3(VROUNDPS,   V,x,  W,x,  I,b,  vex2 cpuid(SSE41) p_66),
    [0x09] = X86_OP_ENTRY3(VROUNDPD,   V,x,  W,x,  I,b,  vex2 cpuid(SSE41) p_66),
    /*
     * Not listed as four operand in the manual.  Also writes and reads 128-bits
     * from the first two operands due to the V operand picking higher entries of
     * the H operand; the "Vss,Hss,Wss" description from the manual is incorrect.
     * For other unary operations such as VSQRTSx this is hidden by the "REPScalar"
     * value of vex_special, because the table lists the operand types of VSQRTPx.
     */
    [0x0a] = X86_OP_ENTRY4(VROUNDSS,   V,x,  H,x, W,ss, vex3 cpuid(SSE41) p_66),
    [0x0b] = X86_OP_ENTRY4(VROUNDSD,   V,x,  H,x, W,sd, vex3 cpuid(SSE41) p_66),
    [0x0c] = X86_OP_ENTRY4(VBLENDPS,   V,x,  H,x,  W,x,  vex4 cpuid(SSE41) p_66),
    [0x0d] = X86_OP_ENTRY4(VBLENDPD,   V,x,  H,x,  W,x,  vex4 cpuid(SSE41) p_66),
    [0x0e] = X86_OP_ENTRY4(VPBLENDW,   V,x,  H,x,  W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x0f] = X86_OP_ENTRY4(PALIGNR,    V,x,  H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),

    [0x18] = X86_OP_ENTRY4(VINSERTx128,  V,qq, H,qq, W,dq, vex6 chk(W0) cpuid(AVX) p_66),
    [0x19] = X86_OP_ENTRY3(VEXTRACTx128, W,dq, V,qq, I,b,  vex6 chk(W0) cpuid(AVX) p_66),

    [0x38] = X86_OP_ENTRY4(VINSERTx128,  V,qq, H,qq, W,dq, vex6 chk(W0) cpuid(AVX2) p_66),
    [0x39] = X86_OP_ENTRY3(VEXTRACTx128, W,dq, V,qq, I,b,  vex6 chk(W0) cpuid(AVX2) p_66),

    /* Listed incorrectly as type 4 */
    [0x4a] = X86_OP_ENTRY4(VBLENDVPS, V,x,  H,x,  W,x,   vex6 chk(W0) cpuid(AVX) p_66),
    [0x4b] = X86_OP_ENTRY4(VBLENDVPD, V,x,  H,x,  W,x,   vex6 chk(W0) cpuid(AVX) p_66),
    [0x4c] = X86_OP_ENTRY4(VPBLENDVB, V,x,  H,x,  W,x,   vex6 chk(W0) cpuid(AVX) p_66 avx2_256),

    [0xcc] = X86_OP_ENTRY3(SHA1RNDS4,  V,dq, W,dq, I,b,  sse4_novex cpuid(SHA_NI) p_00),

#if __Use_Original_Qemu != 1 /* ours (U83) */
    /* VSM3RNDS2 xmm1, xmm2, xmm3/m128, imm8: VEX.128.66.0F3A.W0 DE /r ib (U83) */
    [0xde] = X86_OP_ENTRY4(VSM3RNDS2,  V,dq, H,dq, W,dq, vex6 chk(W0) cpuid(SM3) p_66),
#endif /* __Use_Original_Qemu (U83) */
    [0xdf] = X86_OP_ENTRY3(VAESKEYGEN, V,dq, W,dq, I,b,  vex4 cpuid(AES) p_66),

    [0xF0] = X86_OP_ENTRY3(RORX, G,y, E,y, I,b, vex13 cpuid(BMI2) p_f2),
#if __Use_Original_Qemu != 1 /* ours (U133) */

    /* VEX opmask: KSHIFTR, KSHIFTL (U133) */
    [0x30] = X86_OP_GROUP0(0F3A_KSHIFT),
    [0x31] = X86_OP_GROUP0(0F3A_KSHIFT),
    [0x32] = X86_OP_GROUP0(0F3A_KSHIFT),
    [0x33] = X86_OP_GROUP0(0F3A_KSHIFT),
#endif /* __Use_Original_Qemu (U133) */
};

#if __Use_Original_Qemu != 1 /* ours (U103) */
/*
 * NoVmp (ledger U103): VEX map 7 (3-byte VEX, m-mmmm = 00111): only F8,
 * VEX.128.F2.W0 F8 11:000:bbb id URDMSR r64, imm32 and VEX.128.F3.W0 F8
 * 11:000:bbb id UWRMSR imm32, r64 (SDM Vol2). VEX.L1 and VEX.vvvv other than
 * 1111b #UD in validate_vex (class 13); W1, reg other than 0, other opcodes
 * and other prefixes #UD here.
 */
static void decode_MAP7(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry urdmsr_i =
        X86_OP_ENTRY3(URDMSR_i, R,q, None,None, I,d, vex13 cpuid(USER_MSR) o64);
    static const X86OpEntry uwrmsr_i =
        X86_OP_ENTRY3(UWRMSR_i, None,None, R,q, I,d, vex13 cpuid(USER_MSR) o64);

    *b = x86_ldub_code(env, s);
    memset(entry, 0, sizeof(*entry));
    if (*b != 0xf8 || s->vex_w || ((get_modrm(s, env) >> 3) & 7) != 0) {
        return;
    }
    if (s->prefix & PREFIX_REPNZ) {
        *entry = urdmsr_i;
    } else if (s->prefix & PREFIX_REPZ) {
        *entry = uwrmsr_i;
    }
}

#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U79) */
/*
 * NoVmp (ledger U79): reached from the 0F map table, i.e. legacy 0F 38 / 0F 3A.
 * VEX selects maps 2/3 with m-mmmm; a 38/3A byte inside VEX map 1 is not an
 * escape and is #UD (SDM Vol2 Table A-3; i5-13600K).
 */
static void decode_0F38(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b);
static void decode_0F3A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b);
static void decode_0F38_esc(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    if (s->prefix & PREFIX_VEX) {
        entry->gen = NULL;
        return;
    }
    decode_0F38(s, env, entry, b);
}

static void decode_0F3A_esc(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    if (s->prefix & PREFIX_VEX) {
        entry->gen = NULL;
        return;
    }
    decode_0F3A(s, env, entry, b);
}

#endif /* __Use_Original_Qemu (U79) */
static void decode_0F3A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    *entry = opcodes_0F3A[*b];
}

/*
 * There are some mistakes in the operands in the manual, and the load/store/register
 * cases are easiest to keep separate, so the entries for 10-17 follow simplicity and
 * efficiency of implementation rather than copying what the manual says.
 *
 * In particular:
 *
 * 1) "VMOVSS m32, xmm1" and "VMOVSD m64, xmm1" do not support VEX.vvvv != 1111b,
 * but this is not mentioned in the tables.
 *
 * 2) MOVHLPS, MOVHPS, MOVHPD, MOVLPD, MOVLPS read the high quadword of one of their
 * operands, which must therefore be dq; MOVLPD and MOVLPS also write the high
 * quadword of the V operand.
 */
static void decode_0F10(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F10_reg[4] = {
        X86_OP_ENTRY3(MOVDQ,   V,x,  None,None, W,x, vex4_unal), /* MOVUPS */
        X86_OP_ENTRY3(MOVDQ,   V,x,  None,None, W,x, vex4_unal), /* MOVUPD */
        X86_OP_ENTRY3(VMOVSS,  V,x,  H,x,       W,x, vex5),
        X86_OP_ENTRY3(VMOVLPx, V,x,  H,x,       W,x, vex5), /* MOVSD */
    };

    static const X86OpEntry opcodes_0F10_mem[4] = {
        X86_OP_ENTRY3(MOVDQ,      V,x,  None,None, W,x,  vex4_unal), /* MOVUPS */
        X86_OP_ENTRY3(MOVDQ,      V,x,  None,None, W,x,  vex4_unal), /* MOVUPD */
#if __Use_Original_Qemu == 1 /* original QEMU (U79) */
        X86_OP_ENTRY3(VMOVSS_ld,  V,x,  H,x,       M,ss, vex5),
        X86_OP_ENTRY3(VMOVSD_ld,  V,x,  H,x,       M,sd, vex5),
#else /* ours (U79) */
        /* load form: VEX.vvvv reserved, 1111b or #UD (SDM Vol2 MOVSS/MOVSD) (U79) */
        X86_OP_ENTRY3(VMOVSS_ld,  V,x,  None,None, M,ss, vex5),
        X86_OP_ENTRY3(VMOVSD_ld,  V,x,  None,None, M,sd, vex5),
#endif /* __Use_Original_Qemu (U79) */
    };

    if ((get_modrm(s, env) >> 6) == 3) {
        *entry = *decode_by_prefix(s, opcodes_0F10_reg);
    } else {
        *entry = *decode_by_prefix(s, opcodes_0F10_mem);
    }
}

static void decode_0F11(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F11_reg[4] = {
        X86_OP_ENTRY3(MOVDQ,   W,x,  None,None, V,x, vex4_unal), /* MOVUPS */
        X86_OP_ENTRY3(MOVDQ,   W,x,  None,None, V,x, vex4_unal), /* MOVUPD */
        X86_OP_ENTRY3(VMOVSS,  W,x,  H,x,       V,x, vex5),
        X86_OP_ENTRY3(VMOVLPx, W,x,  H,x,       V,q, vex5), /* MOVSD */
    };

    static const X86OpEntry opcodes_0F11_mem[4] = {
        X86_OP_ENTRY3(MOVDQ,      W,x,  None,None, V,x, vex4_unal), /* MOVUPS */
        X86_OP_ENTRY3(MOVDQ,      W,x,  None,None, V,x, vex4_unal), /* MOVUPD */
        X86_OP_ENTRY3(VMOVSS_st,  M,ss, None,None, V,x, vex5),
        X86_OP_ENTRY3(VMOVLPx_st, M,sd, None,None, V,x, vex5), /* MOVSD */
    };

    if ((get_modrm(s, env) >> 6) == 3) {
        *entry = *decode_by_prefix(s, opcodes_0F11_reg);
    } else {
        *entry = *decode_by_prefix(s, opcodes_0F11_mem);
    }
}

static void decode_0F12(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F12_mem[4] = {
        /*
         * Use dq for operand for compatibility with gen_MOVSD and
         * to allow VEX128 only.
         */
        X86_OP_ENTRY3(VMOVLPx_ld, V,dq, H,dq,      M,q, vex5), /* MOVLPS */
        X86_OP_ENTRY3(VMOVLPx_ld, V,dq, H,dq,      M,q, vex5), /* MOVLPD */
        X86_OP_ENTRY3(VMOVSLDUP,  V,x,  None,None, W,x, vex4 cpuid(SSE3)),
        X86_OP_ENTRY3(VMOVDDUP,   V,x,  None,None, WM,q, vex5 cpuid(SSE3)), /* qq if VEX.256 */
    };
    static const X86OpEntry opcodes_0F12_reg[4] = {
        X86_OP_ENTRY3(VMOVHLPS,  V,dq, H,dq,       U,dq, vex7),
#if __Use_Original_Qemu == 1 /* original QEMU (U22) */
        X86_OP_ENTRY3(VMOVLPx,   W,dq, H,dq,       U,q,  vex5), /* MOVLPD */
#else /* ours (U22) */
        {}, /* (V)MOVLPD is m64-only (SDM): 66 0F 12 with mod=11 is #UD on hardware */
#endif /* __Use_Original_Qemu (U22) */
        X86_OP_ENTRY3(VMOVSLDUP, V,x,  None,None,  U,x,  vex4 cpuid(SSE3)),
        X86_OP_ENTRY3(VMOVDDUP,  V,x,  None,None,  U,x,  vex5 cpuid(SSE3)),
    };

    if ((get_modrm(s, env) >> 6) == 3) {
        *entry = *decode_by_prefix(s, opcodes_0F12_reg);
    } else {
        *entry = *decode_by_prefix(s, opcodes_0F12_mem);
        if ((s->prefix & PREFIX_REPNZ) && s->vex_l) {
            entry->s2 = X86_SIZE_qq;
        }
    }
}

static void decode_0F16(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F16_mem[4] = {
        /*
         * Operand 1 technically only reads the low 64 bits, but uses dq so that
         * it is easier to check for op0 == op1 in an endianness-neutral manner.
         */
        X86_OP_ENTRY3(VMOVHPx_ld, V,dq, H,dq,      M,q, vex5), /* MOVHPS */
        X86_OP_ENTRY3(VMOVHPx_ld, V,dq, H,dq,      M,q, vex5), /* MOVHPD */
        X86_OP_ENTRY3(VMOVSHDUP,  V,x,  None,None, W,x, vex4 cpuid(SSE3)),
        {},
    };
    static const X86OpEntry opcodes_0F16_reg[4] = {
        /* Same as above, operand 1 could be Hq if it wasn't for big-endian.  */
        X86_OP_ENTRY3(VMOVLHPS,  V,dq, H,dq,      U,q, vex7),
#if __Use_Original_Qemu == 1 /* original QEMU (U22) */
        X86_OP_ENTRY3(VMOVHPx,   V,x,  H,x,       U,x, vex5), /* MOVHPD */
#else /* ours (U22) */
        {}, /* (V)MOVHPD is m64-only (SDM): 66 0F 16 with mod=11 is #UD on hardware */
#endif /* __Use_Original_Qemu (U22) */
        X86_OP_ENTRY3(VMOVSHDUP, V,x,  None,None, U,x, vex4 cpuid(SSE3)),
        {},
    };

    if ((get_modrm(s, env) >> 6) == 3) {
        *entry = *decode_by_prefix(s, opcodes_0F16_reg);
    } else {
        *entry = *decode_by_prefix(s, opcodes_0F16_mem);
    }
}

static void decode_0F2A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F2A[4] = {
        X86_OP_ENTRY3(CVTPI2Px,  V,x,  None,None, Q,q),
        X86_OP_ENTRY3(CVTPI2Px,  V,x,  None,None, Q,q),
        X86_OP_ENTRY3(VCVTSI2Sx, V,x,  H,x, E,y,        vex3),
        X86_OP_ENTRY3(VCVTSI2Sx, V,x,  H,x, E,y,        vex3),
    };
    *entry = *decode_by_prefix(s, opcodes_0F2A);
}

static void decode_0F2B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F2B[4] = {
        X86_OP_ENTRY3(MOVDQ,      M,x,  None,None, V,x, vex1), /* MOVNTPS */
        X86_OP_ENTRY3(MOVDQ,      M,x,  None,None, V,x, vex1), /* MOVNTPD */
        /* AMD extensions */
        X86_OP_ENTRY3(VMOVSS_st,  M,ss, None,None, V,x, vex4 cpuid(SSE4A)), /* MOVNTSS */
        X86_OP_ENTRY3(VMOVLPx_st, M,sd, None,None, V,x, vex4 cpuid(SSE4A)), /* MOVNTSD */
    };

    *entry = *decode_by_prefix(s, opcodes_0F2B);
}

static void decode_0F2C(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F2C[4] = {
        /* Listed as ps/pd in the manual, but CVTTPS2PI only reads 64-bit.  */
        X86_OP_ENTRY3(CVTTPx2PI,  P,q,  None,None, W,q),
        X86_OP_ENTRY3(CVTTPx2PI,  P,q,  None,None, W,dq),
        X86_OP_ENTRY3(VCVTTSx2SI, G,y,  None,None, W,ss, vex3),
        X86_OP_ENTRY3(VCVTTSx2SI, G,y,  None,None, W,sd, vex3),
    };
    *entry = *decode_by_prefix(s, opcodes_0F2C);
}

static void decode_0F2D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F2D[4] = {
        /* Listed as ps/pd in the manual, but CVTPS2PI only reads 64-bit.  */
        X86_OP_ENTRY3(CVTPx2PI,  P,q,  None,None, W,q),
        X86_OP_ENTRY3(CVTPx2PI,  P,q,  None,None, W,dq),
        X86_OP_ENTRY3(VCVTSx2SI, G,y,  None,None, W,ss, vex3),
        X86_OP_ENTRY3(VCVTSx2SI, G,y,  None,None, W,sd, vex3),
    };
    *entry = *decode_by_prefix(s, opcodes_0F2D);
}

static void decode_VxCOMISx(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    /*
     * VUCOMISx and VCOMISx are different and use no-prefix and 0x66 for SS and SD
     * respectively.  Scalar values usually are associated with 0xF2 and 0xF3, for
     * which X86_VEX_REPScalar exists, but here it has to be decoded by hand.
     */
    entry->s1 = entry->s2 = (s->prefix & PREFIX_DATA ? X86_SIZE_sd : X86_SIZE_ss);
    entry->gen = (*b == 0x2E ? gen_VUCOMI : gen_VCOMI);
}

static void decode_sse_unary(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    if (!(s->prefix & (PREFIX_REPZ | PREFIX_REPNZ))) {
        entry->op1 = X86_TYPE_None;
        entry->s1 = X86_SIZE_None;
    }
    switch (*b) {
    case 0x51: entry->gen = gen_VSQRT; break;
    case 0x52: entry->gen = gen_VRSQRT; break;
    case 0x53: entry->gen = gen_VRCP; break;
    }
}

static void decode_0F5A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F5A[4] = {
        X86_OP_ENTRY2(VCVTPS2PD,  V,x,       W,xh, vex2),      /* VCVTPS2PD */
        X86_OP_ENTRY2(VCVTPD2PS,  V,x,       W,x,  vex2),      /* VCVTPD2PS */
        X86_OP_ENTRY3(VCVTSS2SD,  V,x,  H,x, W,x,  vex2_rep3), /* VCVTSS2SD */
        X86_OP_ENTRY3(VCVTSD2SS,  V,x,  H,x, W,x,  vex2_rep3), /* VCVTSD2SS */
    };
    *entry = *decode_by_prefix(s, opcodes_0F5A);
}

static void decode_0F5B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F5B[4] = {
        X86_OP_ENTRY2(VCVTDQ2PS,   V,x, W,x,      vex2),
        X86_OP_ENTRY2(VCVTPS2DQ,   V,x, W,x,      vex2),
        X86_OP_ENTRY2(VCVTTPS2DQ,  V,x, W,x,      vex2),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F5B);
}

static void decode_0FE6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0FE6[4] = {
        {},
        X86_OP_ENTRY2(VCVTTPD2DQ,  V,x, W,x,      vex2),
        X86_OP_ENTRY2(VCVTDQ2PD,   V,x, W,x,      vex5),
        X86_OP_ENTRY2(VCVTPD2DQ,   V,x, W,x,      vex2),
    };
    *entry = *decode_by_prefix(s, opcodes_0FE6);
}

#if __Use_Original_Qemu != 1 /* ours (U130) */
/*
 * NoVmp (ledger U130): VEX-encoded opmask instructions (SDM Vol2A KADD..KXOR). Only VEX
 * reaches these slots (legacy 0F 41-4B / 90-99 are CMOVcc / SETcc in the old decoder).
 * The width comes from pp and VEX.W: entries[] = { W0: NP, 66, F3, F2, W1: NP, 66,
 * F3, F2 }; an empty slot (gen == NULL) is #UD. Operand sizes: b/w/d/q = 8/16/32/64.
 */
static void decode_K_pp_w(DisasContext *s, X86OpEntry *entry, const X86OpEntry entries[8],
                          bool w)
{
    int pp = s->prefix & PREFIX_REPNZ ? 3 : s->prefix & PREFIX_REPZ ? 2 :
             s->prefix & PREFIX_DATA ? 1 : 0;

    if (!(s->prefix & PREFIX_VEX)) {
        memset(entry, 0, sizeof(*entry));
        return;
    }
    *entry = entries[(w ? 4 : 0) + pp];
}

/* RVR, VEX.L1: NP.W0 = W (featw), 66.W0 = B (DQ), NP.W1 = Q (BW), 66.W1 = D (BW) */
#define K3_ENTRY(op, sz, feat)                                                        \
    X86_OP_ENTRY3(op, K,sz, KH,sz, KU,sz, vex20 chk(VEX256) cpuid(feat))
#define K3_WBQD(op, featw) {                                                          \
    K3_ENTRY(op, w, featw), K3_ENTRY(op, b, AVX512DQ), X86_OP_NONE X86_OP_NONE        \
    K3_ENTRY(op, q, AVX512BW), K3_ENTRY(op, d, AVX512BW), X86_OP_NONE X86_OP_NONE }
/* RR, VEX.L0, vvvv 1111b: same widths; op0 None for KTEST/KORTEST (flags only) */
#define K2_ENTRY(op, t0, t1, sz, feat)                                                \
    X86_OP_ENTRY3(op, t0,sz, t1,sz, KU,sz, vex20 chk(VEX128) cpuid(feat))
#define K2_WBQD(op, t0, t1, featw) {                                                  \
    K2_ENTRY(op, t0, t1, w, featw), K2_ENTRY(op, t0, t1, b, AVX512DQ),                \
    X86_OP_NONE X86_OP_NONE                                                           \
    K2_ENTRY(op, t0, t1, q, AVX512BW), K2_ENTRY(op, t0, t1, d, AVX512BW),             \
    X86_OP_NONE X86_OP_NONE }

/* KAND 41, KANDN 42, KNOT 44, KOR 45, KXNOR 46, KXOR 47, KADD 4A, KUNPCK 4B */
static void decode_0F_K(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry k41[8] = K3_WBQD(KAND, AVX512F);
    static const X86OpEntry k42[8] = K3_WBQD(KANDN, AVX512F);
    static const X86OpEntry k44[8] = K2_WBQD(KNOT, K, None, AVX512F);
    static const X86OpEntry k45[8] = K3_WBQD(KOR, AVX512F);
    static const X86OpEntry k46[8] = K3_WBQD(KXNOR, AVX512F);
    static const X86OpEntry k47[8] = K3_WBQD(KXOR, AVX512F);
    static const X86OpEntry k4a[8] = K3_WBQD(KADD, AVX512DQ);    /* KADDW: DQ */
    /* KUNPCKBW 66.W0 (F), KUNPCKWD NP.W0 (BW), KUNPCKDQ NP.W1 (BW); 66.W1 #UD */
    static const X86OpEntry k4b[8] = {
        X86_OP_ENTRY3(KUNPCK, K,d, KH,w, KU,w, vex20 chk(VEX256) cpuid(AVX512BW)),
        X86_OP_ENTRY3(KUNPCK, K,w, KH,b, KU,b, vex20 chk(VEX256) cpuid(AVX512F)),
        X86_OP_NONE X86_OP_NONE
        X86_OP_ENTRY3(KUNPCK, K,q, KH,d, KU,d, vex20 chk(VEX256) cpuid(AVX512BW)),
        X86_OP_NONE X86_OP_NONE X86_OP_NONE
    };
    const X86OpEntry *t;

    switch (*b) {
    case 0x41: t = k41; break;
    case 0x42: t = k42; break;
    case 0x44: t = k44; break;
    case 0x45: t = k45; break;
    case 0x46: t = k46; break;
    case 0x47: t = k47; break;
    case 0x4a: t = k4a; break;
    default:   t = k4b; break;
    }
    decode_K_pp_w(s, entry, t, s->vex_w);
}
#endif /* __Use_Original_Qemu (U130) */

#if __Use_Original_Qemu != 1 /* ours (U131) */
/*
 * NoVmp (ledger U131): KMOVW/KMOVB/KMOVQ/KMOVD (SDM Vol2A), VEX.L0, vvvv 1111b.
 * 90 k1, k2/m (RM, K21) and 91 m, k1 (MR, K21; mod = 11b #UD): NP.W0 = W (AVX512F),
 * 66.W0 = B (DQ), NP.W1 = Q (BW), 66.W1 = D (BW); memory is exactly 8/16/32/64 bits.
 * 92 k1, r32/r64 and 93 r32/r64, k1 (RR, K20; mod != 11b #UD): NP.W0 = W, 66.W0 = B,
 * F2.W0 = D, F2.W1 = Q (r64); NP/66.W1 and F3 #UD. "VEX.W can only be used to modify the
 * size of the GPR operand in 64b mode" (KMOVQ k, r64 / r64, k are V/I): outside 64-bit
 * mode F2.W1 is KMOVD. A GPR destination is ZeroExtension(SRC[w-1:0]) (G,d writes
 * zero-extend to 64 bits in 64-bit mode); a k destination is zero-extended to 64.
 */
static void decode_0F_KMOV(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry k90[8] = {
        X86_OP_ENTRY3(KMOV, K,w, None,None, KW,w, vex21 chk(VEX128) cpuid(AVX512F)),
        X86_OP_ENTRY3(KMOV, K,b, None,None, KW,b, vex21 chk(VEX128) cpuid(AVX512DQ)),
        X86_OP_NONE X86_OP_NONE
        X86_OP_ENTRY3(KMOV, K,q, None,None, KW,q, vex21 chk(VEX128) cpuid(AVX512BW)),
        X86_OP_ENTRY3(KMOV, K,d, None,None, KW,d, vex21 chk(VEX128) cpuid(AVX512BW)),
        X86_OP_NONE X86_OP_NONE
    };
    static const X86OpEntry k91[8] = {
        X86_OP_ENTRY3(KMOV, KM,w, None,None, K,w, vex21 chk(VEX128) cpuid(AVX512F)),
        X86_OP_ENTRY3(KMOV, KM,b, None,None, K,b, vex21 chk(VEX128) cpuid(AVX512DQ)),
        X86_OP_NONE X86_OP_NONE
        X86_OP_ENTRY3(KMOV, KM,q, None,None, K,q, vex21 chk(VEX128) cpuid(AVX512BW)),
        X86_OP_ENTRY3(KMOV, KM,d, None,None, K,d, vex21 chk(VEX128) cpuid(AVX512BW)),
        X86_OP_NONE X86_OP_NONE
    };
    static const X86OpEntry k92[8] = {
        X86_OP_ENTRY3(KMOV, K,w, None,None, R,d, vex20 chk(VEX128) cpuid(AVX512F)),
        X86_OP_ENTRY3(KMOV, K,b, None,None, R,d, vex20 chk(VEX128) cpuid(AVX512DQ)),
        X86_OP_NONE
        X86_OP_ENTRY3(KMOV, K,d, None,None, R,d, vex20 chk(VEX128) cpuid(AVX512BW)),
        X86_OP_NONE X86_OP_NONE X86_OP_NONE
        X86_OP_ENTRY3(KMOV, K,q, None,None, R,q, vex20 chk(VEX128) cpuid(AVX512BW)),
    };
    static const X86OpEntry k93[8] = {
        X86_OP_ENTRY3(KMOV, G,d, None,None, KU,w, vex20 chk(VEX128) cpuid(AVX512F)),
        X86_OP_ENTRY3(KMOV, G,d, None,None, KU,b, vex20 chk(VEX128) cpuid(AVX512DQ)),
        X86_OP_NONE
        X86_OP_ENTRY3(KMOV, G,d, None,None, KU,d, vex20 chk(VEX128) cpuid(AVX512BW)),
        X86_OP_NONE X86_OP_NONE X86_OP_NONE
        X86_OP_ENTRY3(KMOV, G,q, None,None, KU,q, vex20 chk(VEX128) cpuid(AVX512BW)),
    };
    bool w = s->vex_w;

    switch (*b) {
    case 0x90:
        decode_K_pp_w(s, entry, k90, w);
        break;
    case 0x91:
        decode_K_pp_w(s, entry, k91, w);
        break;
    default:
        if (!CODE64(s) && (s->prefix & PREFIX_REPNZ)) {
            w = false;   /* F2.W1 outside 64-bit mode: KMOVD */
        }
        decode_K_pp_w(s, entry, *b == 0x92 ? k92 : k93, w);
        break;
    }
}
#endif /* __Use_Original_Qemu (U131) */

#if __Use_Original_Qemu != 1 /* ours (U132) */
/*
 * NoVmp (ledger U132): KORTEST 98 / KTEST 99 k1, k2 (SDM Vol2A), RR, VEX.L0, vvvv 1111b,
 * both operands read, only EFLAGS written. NP.W0 = W (KORTESTW AVX512F, KTESTW
 * AVX512DQ), 66.W0 = B (DQ), NP.W1 = Q (BW), 66.W1 = D (BW).
 */
static void decode_0F_KTEST(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry k98[8] = K2_WBQD(KORTEST, None, K, AVX512F);
    static const X86OpEntry k99[8] = K2_WBQD(KTEST, None, K, AVX512DQ);

    decode_K_pp_w(s, entry, *b == 0x98 ? k98 : k99, s->vex_w);
}
#endif /* __Use_Original_Qemu (U132) */

static const X86OpEntry opcodes_0F[256] = {
    [0x0E] = X86_OP_ENTRY0(EMMS,                              cpuid(3DNOW)), /* femms */
    /*
     * 3DNow!'s opcode byte comes *after* modrm and displacements, making it
     * more like an Ib operand.  Dispatch to the right helper in a single gen_*
     * function.
     */
    [0x0F] = X86_OP_ENTRY3(3dnow,       P,q, Q,q, I,b,        cpuid(3DNOW)),

    [0x10] = X86_OP_GROUP0(0F10),
    [0x11] = X86_OP_GROUP0(0F11),
    [0x12] = X86_OP_GROUP0(0F12),
#if __Use_Original_Qemu == 1 /* original QEMU (U79) */
    [0x13] = X86_OP_ENTRY3(VMOVLPx_st,  M,q, None,None, V,q,  vex5 p_00_66),
#else /* ours (U79) */
    [0x13] = X86_OP_ENTRY3(VMOVLPx_st,  M,q, None,None, V,q,  vex5 chk(VEX128) p_00_66),
#endif /* __Use_Original_Qemu (U79) */
    [0x14] = X86_OP_ENTRY3(VUNPCKLPx,   V,x, H,x, W,x,        vex4 p_00_66),
    [0x15] = X86_OP_ENTRY3(VUNPCKHPx,   V,x, H,x, W,x,        vex4 p_00_66),
    [0x16] = X86_OP_GROUP0(0F16),
    /* Incorrectly listed as Mq,Vq in the manual */
    [0x17] = X86_OP_ENTRY3(VMOVHPx_st,  M,q, None,None, V,dq, vex5 p_00_66),

    [0x50] = X86_OP_ENTRY3(MOVMSK,     G,y, None,None, U,x, vex7 p_00_66),
    [0x51] = X86_OP_GROUP3(sse_unary,  V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2), /* sqrtps */
    [0x52] = X86_OP_GROUP3(sse_unary,  V,x, H,x, W,x, vex4_rep5 p_00_f3), /* rsqrtps */
    [0x53] = X86_OP_GROUP3(sse_unary,  V,x, H,x, W,x, vex4_rep5 p_00_f3), /* rcpps */
    [0x54] = X86_OP_ENTRY3(PAND,       V,x, H,x, W,x,  vex4 p_00_66), /* vand */
    [0x55] = X86_OP_ENTRY3(PANDN,      V,x, H,x, W,x,  vex4 p_00_66), /* vandn */
    [0x56] = X86_OP_ENTRY3(POR,        V,x, H,x, W,x,  vex4 p_00_66), /* vor */
    [0x57] = X86_OP_ENTRY3(PXOR,       V,x, H,x, W,x,  vex4 p_00_66), /* vxor */

    [0x60] = X86_OP_ENTRY3(PUNPCKLBW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x61] = X86_OP_ENTRY3(PUNPCKLWD,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x62] = X86_OP_ENTRY3(PUNPCKLDQ,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x63] = X86_OP_ENTRY3(PACKSSWB,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x64] = X86_OP_ENTRY3(PCMPGTB,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x65] = X86_OP_ENTRY3(PCMPGTW,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x66] = X86_OP_ENTRY3(PCMPGTD,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x67] = X86_OP_ENTRY3(PACKUSWB,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),

    [0x70] = X86_OP_GROUP0(0F70),
    [0x71] = X86_OP_GROUP0(group12),
    [0x72] = X86_OP_GROUP0(group13),
    [0x73] = X86_OP_GROUP0(group14),
    [0x74] = X86_OP_ENTRY3(PCMPEQB,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x75] = X86_OP_ENTRY3(PCMPEQW,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x76] = X86_OP_ENTRY3(PCMPEQD,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x77] = X86_OP_GROUP0(0F77),

    [0x28] = X86_OP_ENTRY3(MOVDQ,      V,x,  None,None, W,x, vex1 p_00_66), /* MOVAPS */
    [0x29] = X86_OP_ENTRY3(MOVDQ,      W,x,  None,None, V,x, vex1 p_00_66), /* MOVAPS */
    [0x2A] = X86_OP_GROUP0(0F2A),
    [0x2B] = X86_OP_GROUP0(0F2B),
    [0x2C] = X86_OP_GROUP0(0F2C),
    [0x2D] = X86_OP_GROUP0(0F2D),
    [0x2E] = X86_OP_GROUP3(VxCOMISx,   None,None, V,x, W,x,  vex3 p_00_66), /* VUCOMISS/SD */
    [0x2F] = X86_OP_GROUP3(VxCOMISx,   None,None, V,x, W,x,  vex3 p_00_66), /* VCOMISS/SD */

#if __Use_Original_Qemu == 1 /* original QEMU (U79) */
    [0x38] = X86_OP_GROUP0(0F38),
    [0x3a] = X86_OP_GROUP0(0F3A),
#else /* ours (U79) */
    [0x38] = X86_OP_GROUP0(0F38_esc),
    [0x3a] = X86_OP_GROUP0(0F3A_esc),
#endif /* __Use_Original_Qemu (U79) */
#if __Use_Original_Qemu != 1 /* ours (U130) */

    /* VEX opmask: KAND, KANDN, KNOT, KOR, KXNOR, KXOR, KADD, KUNPCK (U130) */
    [0x41] = X86_OP_GROUP0(0F_K),
    [0x42] = X86_OP_GROUP0(0F_K),
    [0x44] = X86_OP_GROUP0(0F_K),
    [0x45] = X86_OP_GROUP0(0F_K),
    [0x46] = X86_OP_GROUP0(0F_K),
    [0x47] = X86_OP_GROUP0(0F_K),
    [0x4a] = X86_OP_GROUP0(0F_K),
    [0x4b] = X86_OP_GROUP0(0F_K),
#endif /* __Use_Original_Qemu (U130) */
#if __Use_Original_Qemu != 1 /* ours (U131) */
    /* VEX opmask: KMOV k, k/m; m, k; k, r; r, k (U131) */
    [0x90] = X86_OP_GROUP0(0F_KMOV),
    [0x91] = X86_OP_GROUP0(0F_KMOV),
    [0x92] = X86_OP_GROUP0(0F_KMOV),
    [0x93] = X86_OP_GROUP0(0F_KMOV),
#endif /* __Use_Original_Qemu (U131) */
#if __Use_Original_Qemu != 1 /* ours (U132) */
    /* VEX opmask: KORTEST, KTEST (U132) */
    [0x98] = X86_OP_GROUP0(0F_KTEST),
    [0x99] = X86_OP_GROUP0(0F_KTEST),
#endif /* __Use_Original_Qemu (U132) */

    [0x58] = X86_OP_ENTRY3(VADD,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x59] = X86_OP_ENTRY3(VMUL,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5a] = X86_OP_GROUP0(0F5A),
    [0x5b] = X86_OP_GROUP0(0F5B),
    [0x5c] = X86_OP_ENTRY3(VSUB,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5d] = X86_OP_ENTRY3(VMIN,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5e] = X86_OP_ENTRY3(VDIV,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5f] = X86_OP_ENTRY3(VMAX,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),

    [0x68] = X86_OP_ENTRY3(PUNPCKHBW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x69] = X86_OP_ENTRY3(PUNPCKHWD,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x6a] = X86_OP_ENTRY3(PUNPCKHDQ,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x6b] = X86_OP_ENTRY3(PACKSSDW,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x6c] = X86_OP_ENTRY3(PUNPCKLQDQ, V,x, H,x, W,x,  vex4 p_66 avx2_256),
    [0x6d] = X86_OP_ENTRY3(PUNPCKHQDQ, V,x, H,x, W,x,  vex4 p_66 avx2_256),
    [0x6e] = X86_OP_ENTRY3(MOVD_to,    V,dq,None,None, E,y, vex5 mmx p_00_66),  /* wrong dest Vy on SDM! */
    [0x6f] = X86_OP_GROUP0(0F6F),

    [0x78] = X86_OP_GROUP0(0F78),
    [0x79] = X86_OP_GROUP2(0F79,       V,x, U,x,       cpuid(SSE4A)),
    [0x7c] = X86_OP_ENTRY3(VHADD,      V,x, H,x, W,x,  vex2 cpuid(SSE3) p_66_f2),
    [0x7d] = X86_OP_ENTRY3(VHSUB,      V,x, H,x, W,x,  vex2 cpuid(SSE3) p_66_f2),
    [0x7e] = X86_OP_GROUP0(0F7E),
    [0x7f] = X86_OP_GROUP0(0F7F),

    [0xae] = X86_OP_GROUP0(group15),

    [0xc2] = X86_OP_ENTRY4(VCMP,       V,x, H,x, W,x,       vex2_rep3 p_00_66_f3_f2),
    [0xc4] = X86_OP_ENTRY4(PINSRW,     V,dq,H,dq,E,w,       vex5 mmx p_00_66),
    [0xc5] = X86_OP_ENTRY3(PEXTRW,     G,d, U,dq,I,b,       vex5 mmx p_00_66),
    [0xc6] = X86_OP_ENTRY4(VSHUF,      V,x, H,x, W,x,       vex4 p_00_66),

    [0xd0] = X86_OP_ENTRY3(VADDSUB,   V,x, H,x, W,x,        vex2 cpuid(SSE3) p_66_f2),
    [0xd1] = X86_OP_ENTRY3(PSRLW_r,   V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd2] = X86_OP_ENTRY3(PSRLD_r,   V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd3] = X86_OP_ENTRY3(PSRLQ_r,   V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd4] = X86_OP_ENTRY3(PADDQ,     V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd5] = X86_OP_ENTRY3(PMULLW,    V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd6] = X86_OP_GROUP0(0FD6),
    [0xd7] = X86_OP_ENTRY3(PMOVMSKB,  G,d, None,None, U,x,  vex7 mmx avx2_256 p_00_66),

    [0xe0] = X86_OP_ENTRY3(PAVGB,     V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xe1] = X86_OP_ENTRY3(PSRAW_r,   V,x, H,x, W,x,        vex7 mmx avx2_256 p_00_66),
    [0xe2] = X86_OP_ENTRY3(PSRAD_r,   V,x, H,x, W,x,        vex7 mmx avx2_256 p_00_66),
    [0xe3] = X86_OP_ENTRY3(PAVGW,     V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xe4] = X86_OP_ENTRY3(PMULHUW,   V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xe5] = X86_OP_ENTRY3(PMULHW,    V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xe6] = X86_OP_GROUP0(0FE6),
#if __Use_Original_Qemu == 1 /* original QEMU (U77) */
    [0xe7] = X86_OP_ENTRY3(MOVDQ,     W,x, None,None, V,x,  vex1 mmx p_00_66), /* MOVNTQ/MOVNTDQ */
#else /* ours (U77) */
    /* m64/m128 destination only, register form #UD (SDM Vol2 MOVNTQ/MOVNTDQ) (U77) */
    [0xe7] = X86_OP_ENTRY3(MOVDQ,     M,x, None,None, V,x,  vex1 mmx p_00_66), /* MOVNTQ/MOVNTDQ */
#endif /* __Use_Original_Qemu (U77) */

    [0xf0] = X86_OP_ENTRY3(MOVDQ,    V,x, None,None, WM,x,  vex4_unal cpuid(SSE3) p_f2), /* LDDQU */
    [0xf1] = X86_OP_ENTRY3(PSLLW_r,  V,x, H,x, W,x,         vex7 mmx avx2_256 p_00_66),
    [0xf2] = X86_OP_ENTRY3(PSLLD_r,  V,x, H,x, W,x,         vex7 mmx avx2_256 p_00_66),
    [0xf3] = X86_OP_ENTRY3(PSLLQ_r,  V,x, H,x, W,x,         vex7 mmx avx2_256 p_00_66),
    [0xf4] = X86_OP_ENTRY3(PMULUDQ,  V,x, H,x, W,x,         vex4 mmx avx2_256 p_00_66),
    [0xf5] = X86_OP_ENTRY3(PMADDWD,  V,x, H,x, W,x,         vex4 mmx avx2_256 p_00_66),
    [0xf6] = X86_OP_ENTRY3(PSADBW,   V,x, H,x, W,x,         vex4 mmx avx2_256 p_00_66),
    [0xf7] = X86_OP_ENTRY3(MASKMOV,  None,None, V,dq, U,dq, vex4_unal avx2_256 mmx p_00_66),

    /* Incorrectly missing from 2-17 */
    [0xd8] = X86_OP_ENTRY3(PSUBUSB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xd9] = X86_OP_ENTRY3(PSUBUSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xda] = X86_OP_ENTRY3(PMINUB,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xdb] = X86_OP_ENTRY3(PAND,     V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xdc] = X86_OP_ENTRY3(PADDUSB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xdd] = X86_OP_ENTRY3(PADDUSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xde] = X86_OP_ENTRY3(PMAXUB,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xdf] = X86_OP_ENTRY3(PANDN,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),

    [0xe8] = X86_OP_ENTRY3(PSUBSB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xe9] = X86_OP_ENTRY3(PSUBSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xea] = X86_OP_ENTRY3(PMINSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xeb] = X86_OP_ENTRY3(POR,     V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xec] = X86_OP_ENTRY3(PADDSB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xed] = X86_OP_ENTRY3(PADDSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xee] = X86_OP_ENTRY3(PMAXSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xef] = X86_OP_ENTRY3(PXOR,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),

    [0xf8] = X86_OP_ENTRY3(PSUBB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xf9] = X86_OP_ENTRY3(PSUBW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfa] = X86_OP_ENTRY3(PSUBD,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfb] = X86_OP_ENTRY3(PSUBQ,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfc] = X86_OP_ENTRY3(PADDB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfd] = X86_OP_ENTRY3(PADDW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfe] = X86_OP_ENTRY3(PADDD,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    /* 0xff = UD0 */
};

static void do_decode_0F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *entry = opcodes_0F[*b];
}

static void decode_0F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    do_decode_0F(s, env, entry, b);
}

static const X86OpEntry opcodes_root[256] = {
    [0x0F] = X86_OP_GROUP0(0F),
};

#undef mmx
#undef vex1
#undef vex2
#undef vex3
#undef vex4
#undef vex4_unal
#undef vex5
#undef vex6
#undef vex7
#undef vex8
#undef vex11
#undef vex12
#undef vex13
#if __Use_Original_Qemu != 1 /* ours (U129) */
#undef vex20
#undef vex21
#endif /* __Use_Original_Qemu (U129) */
#undef chk

/*
 * Decode the fixed part of the opcode and place the last
 * in b.
 */
static void decode_root(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *entry = opcodes_root[*b];
}


static int decode_modrm(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                        X86DecodedOp *op, X86OpType type)
{
    int modrm = get_modrm(s, env);
    if ((modrm >> 6) == 3) {
        if (s->prefix & PREFIX_LOCK) {
            decode->e.gen = gen_illegal;
            return 0xff;
        }
        op->n = (modrm & 7);
        if (type != X86_TYPE_Q && type != X86_TYPE_N) {
            op->n |= REX_B(s);
        }
    } else {
        op->has_ea = true;
        op->n = -1;
        decode->mem = gen_lea_modrm_0(env, s, modrm,
                                      decode->e.vex_class == 12);
    }
    return modrm;
}

static bool decode_op_size(DisasContext *s, X86OpEntry *e, X86OpSize size, MemOp *ot)
{
    switch (size) {
    case X86_SIZE_b:  /* byte */
        *ot = MO_8;
        return true;

    case X86_SIZE_d:  /* 32-bit */
    case X86_SIZE_ss: /* SSE/AVX scalar single precision */
        *ot = MO_32;
        return true;

    case X86_SIZE_p:  /* Far pointer, return offset size */
    case X86_SIZE_s:  /* Descriptor, return offset size */
    case X86_SIZE_v:  /* 16/32/64-bit, based on operand size */
        *ot = s->dflag;
        return true;

    case X86_SIZE_pi: /* MMX */
    case X86_SIZE_q:  /* 64-bit */
    case X86_SIZE_sd: /* SSE/AVX scalar double precision */
        *ot = MO_64;
        return true;

    case X86_SIZE_w:  /* 16-bit */
        *ot = MO_16;
        return true;

    case X86_SIZE_y:  /* 32/64-bit, based on operand size */
        *ot = s->dflag == MO_16 ? MO_32 : s->dflag;
        return true;

    case X86_SIZE_z:  /* 16-bit for 16-bit operand size, else 32-bit */
        *ot = s->dflag == MO_16 ? MO_16 : MO_32;
        return true;

    case X86_SIZE_dq: /* SSE/AVX 128-bit */
        if (e->special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            *ot = MO_64;
            return true;
        }
        if (s->vex_l && e->s0 != X86_SIZE_qq && e->s1 != X86_SIZE_qq) {
            return false;
        }
        *ot = MO_128;
        return true;

    case X86_SIZE_qq: /* AVX 256-bit */
        if (!s->vex_l) {
            return false;
        }
        *ot = MO_256;
        return true;

    case X86_SIZE_x:  /* 128/256-bit, based on operand size */
        if (e->special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            *ot = MO_64;
            return true;
        }
        /* fall through */
    case X86_SIZE_ps: /* SSE/AVX packed single precision */
    case X86_SIZE_pd: /* SSE/AVX packed double precision */
        *ot = s->vex_l ? MO_256 : MO_128;
        return true;

    case X86_SIZE_xh: /* SSE/AVX packed half register */
        *ot = s->vex_l ? MO_128 : MO_64;
        return true;

    case X86_SIZE_d64:  /* Default to 64-bit in 64-bit mode */
        *ot = CODE64(s) && s->dflag == MO_32 ? MO_64 : s->dflag;
        return true;

    case X86_SIZE_f64:  /* Ignore size override prefix in 64-bit mode */
        *ot = CODE64(s) ? MO_64 : s->dflag;
        return true;

    default:
        *ot = -1;
        return true;
    }
}

static bool decode_op(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                      X86DecodedOp *op, X86OpType type, int b)
{
    int modrm;

    switch (type) {
    case X86_TYPE_None:  /* Implicit or absent */
    case X86_TYPE_A:  /* Implicit */
    case X86_TYPE_F:  /* EFLAGS/RFLAGS */
        break;

    case X86_TYPE_B:  /* VEX.vvvv selects a GPR */
        op->unit = X86_OP_INT;
#if __Use_Original_Qemu == 1 /* original QEMU (U93) */
        op->n = s->vex_v;
#else /* ours (U93) */
        /*
         * NoVmp (ledger U93): outside 64-bit mode a register named by VEX.vvvv uses
         * vvvv[2:0]; vvvv[3] (3-byte VEX byte 2 bit 6) is ignored (SDM Vol2A
         * 2.3.5.6, Table 2-41 "P[14] ignored"; XED VGPR32_N_32 / XMM_N_32). An
         * unused vvvv still has to be 1111b (Table 2-41), so s->vex_v keeps 4 bits.
         */
        op->n = s->vex_v & (CODE64(s) ? 0xf : 7);
#endif /* __Use_Original_Qemu (U93) */
        break;

    case X86_TYPE_C:  /* REG in the modrm byte selects a control register */
        op->unit = X86_OP_CR;
        goto get_reg;

    case X86_TYPE_D:  /* REG in the modrm byte selects a debug register */
        op->unit = X86_OP_DR;
        goto get_reg;

    case X86_TYPE_G:  /* REG in the modrm byte selects a GPR */
        op->unit = X86_OP_INT;
        goto get_reg;

    case X86_TYPE_S:  /* reg selects a segment register */
        op->unit = X86_OP_SEG;
        goto get_reg;

    case X86_TYPE_P:
        op->unit = X86_OP_MMX;
        goto get_reg;

    case X86_TYPE_V:  /* reg in the modrm byte selects an XMM/YMM register */
        if (decode->e.special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            op->unit = X86_OP_MMX;
        } else {
            op->unit = X86_OP_SSE;
        }
    get_reg:
        op->n = ((get_modrm(s, env) >> 3) & 7);
        if (op->unit != X86_OP_MMX) {
            op->n |= REX_R(s);
        }
        break;

    case X86_TYPE_E:  /* ALU modrm operand */
        op->unit = X86_OP_INT;
        goto get_modrm;

    case X86_TYPE_Q:  /* MMX modrm operand */
        op->unit = X86_OP_MMX;
        goto get_modrm;

    case X86_TYPE_W:  /* XMM/YMM modrm operand */
        if (decode->e.special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            op->unit = X86_OP_MMX;
        } else {
            op->unit = X86_OP_SSE;
        }
        goto get_modrm;

    case X86_TYPE_N:  /* R/M in the modrm byte selects an MMX register */
        op->unit = X86_OP_MMX;
        goto get_modrm_reg;

    case X86_TYPE_U:  /* R/M in the modrm byte selects an XMM/YMM register */
        if (decode->e.special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            op->unit = X86_OP_MMX;
        } else {
            op->unit = X86_OP_SSE;
        }
        goto get_modrm_reg;

    case X86_TYPE_R:  /* R/M in the modrm byte selects a register */
        op->unit = X86_OP_INT;
    get_modrm_reg:
        modrm = get_modrm(s, env);
        if ((modrm >> 6) != 3) {
            return false;
        }
        goto get_modrm;

    case X86_TYPE_WM:  /* modrm byte selects an XMM/YMM memory operand */
        op->unit = X86_OP_SSE;
        /* fall through */
    case X86_TYPE_M:  /* modrm byte selects a memory operand */
        modrm = get_modrm(s, env);
        if ((modrm >> 6) == 3) {
            return false;
        }
    get_modrm:
        decode_modrm(s, env, decode, op, type);
        break;

    case X86_TYPE_O:  /* Absolute address encoded in the instruction */
        op->unit = X86_OP_INT;
        op->has_ea = true;
        op->n = -1;
        decode->mem = (AddressParts) {
            .def_seg = R_DS,
            .base = -1,
            .index = -1,
            .disp = insn_get_addr(env, s, s->aflag)
        };
        break;

    case X86_TYPE_H:  /* For AVX, VEX.vvvv selects an XMM/YMM register */
        if ((s->prefix & PREFIX_VEX)) {
            op->unit = X86_OP_SSE;
#if __Use_Original_Qemu == 1 /* original QEMU (U93) */
            op->n = s->vex_v;
#else /* ours (U93) */
            /* NoVmp (ledger U93): vvvv[3] ignored outside 64-bit mode (see X86_TYPE_B) */
            op->n = s->vex_v & (CODE64(s) ? 0xf : 7);
#endif /* __Use_Original_Qemu (U93) */
            break;
        }
        if (op == &decode->op[0]) {
            /* shifts place the destination in VEX.vvvv, use modrm */
            return decode_op(s, env, decode, op, decode->e.op1, b);
        } else {
            return decode_op(s, env, decode, op, decode->e.op0, b);
        }

    case X86_TYPE_I:  /* Immediate */
        op->unit = X86_OP_IMM;
        decode->immediate = insn_get_signed(env, s, op->ot);
        break;

    case X86_TYPE_J:  /* Relative offset for a jump */
        op->unit = X86_OP_IMM;
        decode->immediate = insn_get_signed(env, s, op->ot);
        decode->immediate += s->pc - s->cs_base;
        if (s->dflag == MO_16) {
            decode->immediate &= 0xffff;
        } else if (!CODE64(s)) {
            decode->immediate &= 0xffffffffu;
        }
        break;

    case X86_TYPE_L:  /* The upper 4 bits of the immediate select a 128-bit register */
        op->n = insn_get(env, s, op->ot) >> 4;
        break;

    case X86_TYPE_X:  /* string source */
        op->n = -1;
        decode->mem = (AddressParts) {
            .def_seg = R_DS,
            .base = R_ESI,
            .index = -1,
        };
        break;

    case X86_TYPE_Y:  /* string destination */
        op->n = -1;
        decode->mem = (AddressParts) {
            .def_seg = R_ES,
            .base = R_EDI,
            .index = -1,
        };
        break;

    case X86_TYPE_2op:
        *op = decode->op[0];
        break;

    case X86_TYPE_LoBits:
        op->n = (b & 7) | REX_B(s);
        op->unit = X86_OP_INT;
        break;

    case X86_TYPE_0: case X86_TYPE_1: case X86_TYPE_2: case X86_TYPE_3: case X86_TYPE_4: case X86_TYPE_5: case X86_TYPE_6: case X86_TYPE_7:
        op->n = type - X86_TYPE_0;
        op->unit = X86_OP_INT;
        break;

    case X86_TYPE_ES: case X86_TYPE_CS: case X86_TYPE_SS: case X86_TYPE_DS: case X86_TYPE_FS: case X86_TYPE_GS:
        op->n = type - X86_TYPE_ES;
        op->unit = X86_OP_SEG;
        break;

#if __Use_Original_Qemu != 1 /* ours (U129) */
    /*
     * NoVmp (ledger U129): opmask registers k0-k7 (SDM Vol2A 2.9, Tables 2-41/2-42).
     * Only 3 bits select a k register: ModRM.reg with VEX.R = 0 and VEX.vvvv[3] = 1
     * in 64-bit mode #UD in validate_vex (K20/K21); vvvv[3] is ignored outside 64-bit
     * mode; VEX.B/X are ignored for a k register in ModRM.r/m.
     */
    case X86_TYPE_K:  /* REG in the modrm byte selects an opmask register */
        op->unit = X86_OP_KREG;
        op->n = (get_modrm(s, env) >> 3) & 7;
        break;

    case X86_TYPE_KH: /* VEX.vvvv selects an opmask register */
        op->unit = X86_OP_KREG;
        op->n = s->vex_v & 7;
        break;

    case X86_TYPE_KU: /* R/M selects an opmask register; mod != 11b #UD (K20) */
    case X86_TYPE_KW: /* opmask register or memory */
    case X86_TYPE_KM: /* memory only; mod = 11b #UD (KMOV m, k) */
        op->unit = X86_OP_KREG;
        modrm = get_modrm(s, env);
        if ((modrm >> 6) == 3) {
            if (type == X86_TYPE_KM) {
                return false;
            }
            op->n = modrm & 7;
        } else {
            if (type == X86_TYPE_KU) {
                return false;
            }
            decode_modrm(s, env, decode, op, type);
        }
        break;
#endif /* __Use_Original_Qemu (U129) */
    }

    return true;
}

static bool validate_sse_prefix(DisasContext *s, X86OpEntry *e)
{
    uint16_t sse_prefixes;

    if (!e->valid_prefix) {
        return true;
    }
    if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) {
        /* In SSE instructions, 0xF3 and 0xF2 cancel 0x66.  */
        s->prefix &= ~PREFIX_DATA;
    }

    /* Now, either zero or one bit is set in sse_prefixes.  */
    sse_prefixes = s->prefix & (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA);
    return e->valid_prefix & (1 << sse_prefixes);
}

static bool decode_insn(DisasContext *s, CPUX86State *env, X86DecodeFunc decode_func,
                        X86DecodedInsn *decode)
{
    X86OpEntry *e = &decode->e;

    decode_func(s, env, e, &decode->b);
    while (e->is_decode) {
        e->is_decode = false;
        e->decode(s, env, e, &decode->b);
    }

    if (!validate_sse_prefix(s, e)) {
        return false;
    }
#if __Use_Original_Qemu != 1 /* ours (U79) */
    /*
     * NoVmp (ledger U79): scalar SS/SD forms are VEX.LIG (SDM Vol2): VEX.L is
     * ignored, so decode them exactly as L=0 (bits 255:128 of the destination
     * zeroed). QEMU raised #UD for L=1; the i5-13600K executes them.
     */
    if ((s->prefix & PREFIX_VEX) && e->vex_special == X86_VEX_REPScalar &&
        (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ))) {
        s->vex_l = 0;
    }
#endif /* __Use_Original_Qemu (U79) */

    /* First compute size of operands in order to initialize s->rip_offset.  */
    if (e->op0 != X86_TYPE_None) {
        if (!decode_op_size(s, e, e->s0, &decode->op[0].ot)) {
            return false;
        }
        if (e->op0 == X86_TYPE_I) {
            s->rip_offset += 1 << decode->op[0].ot;
        }
    }
    if (e->op1 != X86_TYPE_None) {
        if (!decode_op_size(s, e, e->s1, &decode->op[1].ot)) {
            return false;
        }
        if (e->op1 == X86_TYPE_I) {
            s->rip_offset += 1 << decode->op[1].ot;
        }
    }
    if (e->op2 != X86_TYPE_None) {
        if (!decode_op_size(s, e, e->s2, &decode->op[2].ot)) {
            return false;
        }
        if (e->op2 == X86_TYPE_I) {
            s->rip_offset += 1 << decode->op[2].ot;
        }
    }
    if (e->op3 != X86_TYPE_None) {
        /*
         * A couple instructions actually use the extra immediate byte for an Lx
         * register operand; those are handled in the gen_* functions as one off.
         */
        assert(e->op3 == X86_TYPE_I && e->s3 == X86_SIZE_b);
        s->rip_offset += 1;
    }

    if (e->op0 != X86_TYPE_None &&
        !decode_op(s, env, decode, &decode->op[0], e->op0, decode->b)) {
        return false;
    }

    if (e->op1 != X86_TYPE_None &&
        !decode_op(s, env, decode, &decode->op[1], e->op1, decode->b)) {
        return false;
    }

    if (e->op2 != X86_TYPE_None &&
        !decode_op(s, env, decode, &decode->op[2], e->op2, decode->b)) {
        return false;
    }

    if (e->op3 != X86_TYPE_None) {
        decode->immediate = insn_get_signed(env, s, MO_8);
    }

    return true;
}

static bool has_cpuid_feature(DisasContext *s, X86CPUIDFeature cpuid)
{
    switch (cpuid) {
    case X86_FEAT_None:
        return true;
    case X86_FEAT_F16C:
        return (s->cpuid_ext_features & CPUID_EXT_F16C);
    case X86_FEAT_FMA:
        return (s->cpuid_ext_features & CPUID_EXT_FMA);
    case X86_FEAT_MOVBE:
        return (s->cpuid_ext_features & CPUID_EXT_MOVBE);
    case X86_FEAT_PCLMULQDQ:
#if __Use_Original_Qemu == 1 /* original QEMU (U69) */
        return (s->cpuid_ext_features & CPUID_EXT_PCLMULQDQ);
#else /* ours (U69) */
        if (!(s->cpuid_ext_features & CPUID_EXT_PCLMULQDQ)) {
            return false;
        }
        /* 256-bit form: VPCLMULQDQ (SDM Vol2 PCLMULQDQ) */
        return !s->vex_l || (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_VPCLMULQDQ);
#endif /* __Use_Original_Qemu (U69) */
    case X86_FEAT_SSE:
        return (s->cpuid_features & CPUID_SSE);
    case X86_FEAT_SSE2:
        return (s->cpuid_features & CPUID_SSE2);
    case X86_FEAT_SSE3:
        return (s->cpuid_ext_features & CPUID_EXT_SSE3);
    case X86_FEAT_SSSE3:
        return (s->cpuid_ext_features & CPUID_EXT_SSSE3);
    case X86_FEAT_SSE41:
        return (s->cpuid_ext_features & CPUID_EXT_SSE41);
    case X86_FEAT_SSE42:
        return (s->cpuid_ext_features & CPUID_EXT_SSE42);
    case X86_FEAT_AES:
        if (!(s->cpuid_ext_features & CPUID_EXT_AES)) {
            return false;
        } else if (!(s->prefix & PREFIX_VEX)) {
            return true;
        } else if (!(s->cpuid_ext_features & CPUID_EXT_AVX)) {
            return false;
        } else {
            return !s->vex_l || (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_VAES);
        }

    case X86_FEAT_AVX:
        return (s->cpuid_ext_features & CPUID_EXT_AVX);
#if __Use_Original_Qemu != 1 /* ours (U70) */
    case X86_FEAT_GFNI:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_GFNI);
#endif /* __Use_Original_Qemu (U70) */
#if __Use_Original_Qemu != 1 /* ours (U71) */
    case X86_FEAT_AVX_VNNI:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_AVX_VNNI);
#endif /* __Use_Original_Qemu (U71) */
#if __Use_Original_Qemu != 1 /* ours (U85) */
    case X86_FEAT_AVX_VNNI_INT8:
        return (s->cpuid_7_1_edx_features & CPUID_7_1_EDX_AVX_VNNI_INT8);
#endif /* __Use_Original_Qemu (U85) */
#if __Use_Original_Qemu != 1 /* ours (U86) */
    case X86_FEAT_AVX_VNNI_INT16:
        return (s->cpuid_7_1_edx_features & CPUID_7_1_EDX_AVX_VNNI_INT16);
#endif /* __Use_Original_Qemu (U86) */
#if __Use_Original_Qemu != 1 /* ours (U87) */
    case X86_FEAT_AVX_IFMA:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_AVX_IFMA);
#endif /* __Use_Original_Qemu (U87) */
#if __Use_Original_Qemu != 1 /* ours (U88) */
    case X86_FEAT_AVX_NE_CONVERT:
        return (s->cpuid_7_1_edx_features & CPUID_7_1_EDX_AVX_NE_CONVERT);
#endif /* __Use_Original_Qemu (U88) */
#if __Use_Original_Qemu != 1 /* ours (U72) */
    case X86_FEAT_MOVDIRI:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_MOVDIRI);
#endif /* __Use_Original_Qemu (U72) */
#if __Use_Original_Qemu != 1 /* ours (U73) */
    case X86_FEAT_MOVDIR64B:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_MOVDIR64B);
#endif /* __Use_Original_Qemu (U73) */
#if __Use_Original_Qemu != 1 /* ours (U82) */
    case X86_FEAT_SHA512:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_SHA512);
#endif /* __Use_Original_Qemu (U82) */
#if __Use_Original_Qemu != 1 /* ours (U83) */
    case X86_FEAT_SM3:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_SM3);
#endif /* __Use_Original_Qemu (U83) */
#if __Use_Original_Qemu != 1 /* ours (U84) */
    case X86_FEAT_SM4:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_SM4);
#endif /* __Use_Original_Qemu (U84) */
#if __Use_Original_Qemu != 1 /* ours (U100) */
    case X86_FEAT_KL:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_KeyLocker);
    case X86_FEAT_AESKLE:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_KeyLocker) &&
               (s->cpuid_19_ebx_features & CPUID_19_EBX_AESKLE);
    case X86_FEAT_AESKLE_WIDE:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_KeyLocker) &&
               (s->cpuid_19_ebx_features & CPUID_19_EBX_AESKLE) &&
               (s->cpuid_19_ebx_features & CPUID_19_EBX_AES_WIDE);
#endif /* __Use_Original_Qemu (U100) */
#if __Use_Original_Qemu != 1 /* ours (U101) */
    case X86_FEAT_RAO_INT:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_RAO_INT);
#endif /* __Use_Original_Qemu (U101) */
#if __Use_Original_Qemu != 1 /* ours (U102) */
    case X86_FEAT_MOVRS:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_MOVRS);
#endif /* __Use_Original_Qemu (U102) */
#if __Use_Original_Qemu != 1 /* ours (U103) */
    case X86_FEAT_USER_MSR:
        return (s->cpuid_7_1_edx_features & CPUID_7_1_EDX_USER_MSR);
#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U112) */
    case X86_FEAT_ENQCMD:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_ENQCMD);
#endif /* __Use_Original_Qemu (U112) */
#if __Use_Original_Qemu != 1 /* ours (U114) */
    case X86_FEAT_CET_SS:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_CET_SHSTK);
#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U129) */
    case X86_FEAT_AVX512F:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512F);
    case X86_FEAT_AVX512DQ:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512DQ);
    case X86_FEAT_AVX512BW:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512BW);
#endif /* __Use_Original_Qemu (U129) */
#if __Use_Original_Qemu != 1 /* ours (U175) */
    case X86_FEAT_AMX_TILE:
        return (s->cpuid_7_0_edx_features & CPUID_7_0_EDX_AMX_TILE);
    case X86_FEAT_AMX_INT8:
        return (s->cpuid_7_0_edx_features & CPUID_7_0_EDX_AMX_INT8);
    case X86_FEAT_AMX_BF16:
        return (s->cpuid_7_0_edx_features & CPUID_7_0_EDX_AMX_BF16);
    case X86_FEAT_AMX_FP16:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_AMX_FP16);
    case X86_FEAT_AMX_COMPLEX:
        return (s->cpuid_7_1_edx_features & CPUID_7_1_EDX_AMX_COMPLEX);
#endif /* __Use_Original_Qemu (U175) */

    case X86_FEAT_3DNOW:
        return (s->cpuid_ext2_features & CPUID_EXT2_3DNOW);
    case X86_FEAT_SSE4A:
        return (s->cpuid_ext3_features & CPUID_EXT3_SSE4A);

    case X86_FEAT_ADX:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_ADX);
    case X86_FEAT_BMI1:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_BMI1);
    case X86_FEAT_BMI2:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_BMI2);
    case X86_FEAT_AVX2:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX2);
    case X86_FEAT_SHA_NI:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_SHA_NI);

    case X86_FEAT_CMPCCXADD:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_CMPCCXADD);
    }
    g_assert_not_reached();
    return false;
}

static bool validate_vex(DisasContext *s, X86DecodedInsn *decode)
{
    X86OpEntry *e = &decode->e;

#if __Use_Original_Qemu != 1 /* ours (U68) */
    /* every VEX form needs CPUID.1:ECX.AVX (a strict CPUID profile can hide it, U68) */
    if ((s->prefix & PREFIX_VEX) && !(s->cpuid_ext_features & CPUID_EXT_AVX)) {
        goto illegal;
    }
#endif /* __Use_Original_Qemu (U68) */
#if __Use_Original_Qemu != 1 /* ours (U79) */
    /*
     * NoVmp (ledger U79): the NP forms of the MMX/SSE dual opcodes are MMX
     * only; VEX.NP on them is #UD (SDM Vol2 2.3.8; i5-13600K), and must
     * fault before the x87 -> MMX transition.
     */
    if ((s->prefix & PREFIX_VEX) && e->special == X86_SPECIAL_MMX &&
        !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
        goto illegal;
    }
#endif /* __Use_Original_Qemu (U79) */
    switch (e->vex_special) {
    case X86_VEX_None:
        break;

    case X86_VEX_REPScalar:
        /*
         * Instructions which differ between 00/66 and F2/F3 in the
         * exception classification and the size of the memory operand.
         */
        assert(e->vex_class == 1 || e->vex_class == 2 || e->vex_class == 4);
        if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) {
            e->vex_class = e->vex_class < 4 ? 3 : 5;
            if (s->vex_l) {
                goto illegal;
            }
            assert(decode->e.s2 == X86_SIZE_x);
            if (decode->op[2].has_ea) {
                decode->op[2].ot = s->prefix & PREFIX_REPZ ? MO_32 : MO_64;
            }
        }
        break;

    case X86_VEX_SSEUnaligned:
        /* handled in sse_needs_alignment.  */
        break;

    case X86_VEX_NoVEX:
        if (s->prefix & PREFIX_VEX) {
            goto illegal;
        }
        break;

    case X86_VEX_AVX2_256:
        if ((s->prefix & PREFIX_VEX) && s->vex_l && !has_cpuid_feature(s, X86_FEAT_AVX2)) {
            goto illegal;
        }
    }

    switch (e->vex_class) {
    case 0:
        if (s->prefix & PREFIX_VEX) {
            goto illegal;
        }
#if __Use_Original_Qemu != 1 /* ours (U50) */
        /*
         * NoVmp (ledger U50): the MMX-state forms decoded here skipped the
         * device-not-available checks: CR0.TS -> #NM, CR0.EM -> #UD, and
         * CR4.OSFXSR = 0 -> #UD for the SSE side (SDM Vol2 CVTPI2PS,
         * CVTPS2PI, CVTTPS2PI, MOVQ2DQ, MOVDQ2Q, EMMS "Exceptions").
         */
        if (e->gen == gen_CVTPI2Px || e->gen == gen_CVTPx2PI ||
            e->gen == gen_CVTTPx2PI || e->gen == gen_MOVq_dq ||
            e->gen == gen_EMMS) {
            if (s->flags & HF_TS_MASK) {
                goto nm_exception;
            }
            if (s->flags & HF_EM_MASK) {
                goto illegal;
            }
            if (e->gen != gen_EMMS && !(s->flags & HF_OSFXSR_MASK)) {
                goto illegal;
            }
        }
#endif /* __Use_Original_Qemu (U50) */
        return true;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
        if (s->prefix & PREFIX_VEX) {
            if (!(s->flags & HF_AVX_EN_MASK)) {
                goto illegal;
            }
        } else if (e->special != X86_SPECIAL_MMX ||
                   (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA))) {
            if (!(s->flags & HF_OSFXSR_MASK)) {
                goto illegal;
            }
        }
        break;
    case 12:
        /* Must have a VSIB byte and no address prefix.  */
        assert(s->has_modrm);
        if ((s->modrm & 7) != 4 || s->aflag == MO_16) {
            goto illegal;
        }

        /* Check no overlap between registers.  */
        if (!decode->op[0].has_ea &&
            (decode->op[0].n == decode->mem.index || decode->op[0].n == decode->op[1].n)) {
            goto illegal;
        }
        assert(!decode->op[1].has_ea);
        if (decode->op[1].n == decode->mem.index) {
            goto illegal;
        }
        if (!decode->op[2].has_ea &&
            (decode->op[2].n == decode->mem.index || decode->op[2].n == decode->op[1].n)) {
            goto illegal;
        }
        /* fall through */
    case 6:
    case 11:
        if (!(s->prefix & PREFIX_VEX)) {
            goto illegal;
        }
        if (!(s->flags & HF_AVX_EN_MASK)) {
            goto illegal;
        }
        break;
    case 8:
        /* Non-VEX case handled in decode_0F77.  */
        assert(s->prefix & PREFIX_VEX);
        if (!(s->flags & HF_AVX_EN_MASK)) {
            goto illegal;
        }
        break;
#if __Use_Original_Qemu != 1 /* ours (U129) */
    case 20:
    case 21:
        /*
         * NoVmp (ledger U129): VEX-encoded opmask instructions, SDM Vol2A 2.9 Tables
         * 2-65 (K20, no memory operand) / 2-66 (K21). The CPUID feature, LOCK/66/F2/F3/
         * REX before VEX and ModRM.mod (X86_TYPE_KU/KM/R) are checked by the decoder.
         * #UD: CR4.OSXSAVE = 0 or XCR0 not 111xxx11b (Table 2-39, HF_OPMASK_EN, U127);
         * VEX.L other than the form's; ModRM.reg encoding a k register with VEX.R = 0
         * (Table 2-41); VEX.vvvv: as a k register 0xxxb in 64-bit mode (Table 2-42,
         * ignored elsewhere), unused != 1111b (Table 2-41). Then #NM if CR0.TS = 1.
         * CR0.EM is not a K20/K21 condition.
         */
        if (!(s->prefix & PREFIX_VEX)) {
            goto illegal;
        }
        if (!(s->flags & HF_OPMASK_EN_MASK)) {
            goto illegal;
        }
        if (((e->check & X86_CHECK_VEX128) && s->vex_l) ||
            ((e->check & X86_CHECK_VEX256) && !s->vex_l)) {
            goto illegal;
        }
        if ((e->op0 == X86_TYPE_K || e->op1 == X86_TYPE_K || e->op2 == X86_TYPE_K) &&
            REX_R(s)) {
            goto illegal;
        }
        if (e->op1 == X86_TYPE_KH) {
            if (CODE64(s) && (s->vex_v & 8)) {
                goto illegal;
            }
        } else if (s->vex_v != 0) {
            goto illegal;
        }
        if (!CODE64(s)) {
            /* VEX.B is ignored outside 64-bit mode (SDM 2.3.5.4): KMOV's GPR r/m */
            int i;
            for (i = 0; i < 3; i++) {
                if (decode->op[i].unit == X86_OP_INT && !decode->op[i].has_ea) {
                    decode->op[i].n &= 7;
                }
            }
        }
        if (s->flags & HF_TS_MASK) {
            goto nm_exception;
        }
        return true;
#endif /* __Use_Original_Qemu (U129) */
#if __Use_Original_Qemu != 1 /* ours (U175) */
    case 22:
        /*
         * NoVmp (ledger U175): VEX Intel AMX, SDM Vol2A 2.10 AMX-E1..E6 - the static #UD
         * conditions (CPUID and LOCK/66/F2/F3/REX before VEX are checked by the decoder):
         * IA32_EFER.LMA = 0 or CS.L = 0; VEX.L = 1 (all forms are VEX.128); VEX.W = 1
         * (W0); VEX.vvvv != 1111b unless it names tsrc2 (the TMUL forms, whose feature is
         * not AMX_TILE). CR4.OSXSAVE, XCR0[18:17], XFD (#NM) and TILECFG are run-time
         * checks in the helpers; no class lists CR0.TS or CR0.EM.
         */
        if (!(s->prefix & PREFIX_VEX) || !CODE64(s)) {
            goto illegal;
        }
        if (s->vex_l || s->vex_w) {
            goto illegal;
        }
        if (s->vex_v != 0 && e->cpuid == X86_FEAT_AMX_TILE) {
            goto illegal;
        }
        return true;
#endif /* __Use_Original_Qemu (U175) */
    case 13:
        if (!(s->prefix & PREFIX_VEX)) {
            goto illegal;
        }
        if (s->vex_l) {
            goto illegal;
        }
#if __Use_Original_Qemu != 1 /* ours (U79) */
        /*
         * ...except RORX (G, E, Ib): VEX.vvvv reserved, 1111b or #UD (U79).
         * MULX writes VEX.vvvv in gen_MULX (its B operand is not in the entry).
         */
        if (s->vex_v != 0 && e->gen != gen_MULX &&
            e->op0 != X86_TYPE_H && e->op0 != X86_TYPE_B &&
            e->op1 != X86_TYPE_H && e->op1 != X86_TYPE_B &&
            e->op2 != X86_TYPE_H && e->op2 != X86_TYPE_B) {
            goto illegal;
        }
#endif /* __Use_Original_Qemu (U79) */
        /* All integer instructions use VEX.vvvv, so exit.  */
        return true;
    }

    if (s->vex_v != 0 &&
        e->op0 != X86_TYPE_H && e->op0 != X86_TYPE_B &&
        e->op1 != X86_TYPE_H && e->op1 != X86_TYPE_B &&
        e->op2 != X86_TYPE_H && e->op2 != X86_TYPE_B) {
        goto illegal;
    }

    if (s->flags & HF_TS_MASK) {
        goto nm_exception;
    }
    if (s->flags & HF_EM_MASK) {
        goto illegal;
    }

    if (e->check & X86_CHECK_W0) {
        if (s->vex_w) {
            goto illegal;
        }
    }
#if __Use_Original_Qemu != 1 /* ours (U79) */
    if ((e->check & X86_CHECK_VEX128) && s->vex_l) {
        goto illegal;
    }
#endif /* __Use_Original_Qemu (U79) */
    if (e->check & X86_CHECK_W1) {
        if (!s->vex_w) {
            goto illegal;
        }
    }
#if __Use_Original_Qemu != 1 /* ours (U70) */
    if ((s->prefix & PREFIX_VEX) &&
        (((e->check & X86_CHECK_VEX_W0) && s->vex_w) ||
         ((e->check & X86_CHECK_VEX_W1) && !s->vex_w))) {
        goto illegal;
    }
#endif /* __Use_Original_Qemu (U70) */
    return true;

nm_exception:
    gen_NM_exception(s);
    return false;
illegal:
    gen_illegal_opcode(s);
    return false;
}

static void decode_temp_free(TCGContext *tcg_ctx, X86DecodedOp *op)
{
    if (op->v_ptr) {
        tcg_temp_free_ptr(tcg_ctx, op->v_ptr);
    }
}

static void decode_temps_free(TCGContext *tcg_ctx, X86DecodedInsn *decode)
{
    decode_temp_free(tcg_ctx, &decode->op[0]);
    decode_temp_free(tcg_ctx, &decode->op[1]);
    decode_temp_free(tcg_ctx, &decode->op[2]);
}

/*
 * Convert one instruction. s->base.is_jmp is set if the translation must
 * be stopped.
 */
static void disas_insn_new(DisasContext *s, CPUState *cpu, int b)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    CPUX86State *env = cpu->env_ptr;
    bool first = true;
    X86DecodedInsn decode;
    X86DecodeFunc decode_func = decode_root;

    s->has_modrm = false;

 next_byte:
    if (first) {
        first = false;
    } else {
        b = x86_ldub_code(env, s);
    }
    /* Collect prefixes.  */
    switch (b) {
    case 0xf3:
        s->prefix |= PREFIX_REPZ;
        s->prefix &= ~PREFIX_REPNZ;
        goto next_byte;
    case 0xf2:
        s->prefix |= PREFIX_REPNZ;
        s->prefix &= ~PREFIX_REPZ;
        goto next_byte;
    case 0xf0:
        s->prefix |= PREFIX_LOCK;
        goto next_byte;
    case 0x2e:
        s->override = R_CS;
        goto next_byte;
    case 0x36:
        s->override = R_SS;
        goto next_byte;
    case 0x3e:
        s->override = R_DS;
        goto next_byte;
    case 0x26:
        s->override = R_ES;
        goto next_byte;
    case 0x64:
        s->override = R_FS;
        goto next_byte;
    case 0x65:
        s->override = R_GS;
        goto next_byte;
    case 0x66:
        s->prefix |= PREFIX_DATA;
        goto next_byte;
    case 0x67:
        s->prefix |= PREFIX_ADR;
        goto next_byte;
#ifdef TARGET_X86_64
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
        if (CODE64(s)) {
            /* REX prefix */
            s->prefix |= PREFIX_REX;
            s->vex_w = (b >> 3) & 1;
            s->rex_r = (b & 0x4) << 1;
            s->rex_x = (b & 0x2) << 2;
            s->rex_b = (b & 0x1) << 3;
            goto next_byte;
        }
        break;
#endif
    case 0xc5: /* 2-byte VEX */
    case 0xc4: /* 3-byte VEX */
        /*
         * VEX prefixes cannot be used except in 32-bit mode.
         * Otherwise the instruction is LES or LDS.
         */
        if (CODE32(s) && !VM86(s)) {
            static const int pp_prefix[4] = {
                0, PREFIX_DATA, PREFIX_REPZ, PREFIX_REPNZ
            };
            int vex3, vex2 = x86_ldub_code(env, s);

            if (!CODE64(s) && (vex2 & 0xc0) != 0xc0) {
                /*
                 * 4.1.4.6: In 32-bit mode, bits [7:6] must be 11b,
                 * otherwise the instruction is LES or LDS.
                 */
                s->pc--; /* rewind the advance_pc() x86_ldub_code() did */
                break;
            }

            /* 4.1.1-4.1.3: No preceding lock, 66, f2, f3, or rex prefixes. */
            if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ
                             | PREFIX_LOCK | PREFIX_DATA | PREFIX_REX)) {
                goto illegal_op;
            }
#ifdef TARGET_X86_64
            s->rex_r = (~vex2 >> 4) & 8;
#endif
            if (b == 0xc5) {
                /* 2-byte VEX prefix: RVVVVlpp, implied 0f leading opcode byte */
                vex3 = vex2;
                decode_func = decode_0F;
            } else {
                /* 3-byte VEX prefix: RXBmmmmm wVVVVlpp */
                vex3 = x86_ldub_code(env, s);
#ifdef TARGET_X86_64
                s->rex_x = (~vex2 >> 3) & 8;
#if __Use_Original_Qemu == 1 /* original QEMU (U93) */
                s->rex_b = (~vex2 >> 2) & 8;
#else /* ours (U93) */
                /*
                 * NoVmp (ledger U93): VEX.B extends ModRM.rm / SIB.base only in
                 * 64-bit mode; "In 32-bit modes, this bit is ignored" (SDM Vol2A
                 * 2.3.5.4). VEX.R / VEX.X are 1 here (else LES/LDS, checked above),
                 * so rex_r / rex_x are 0 already.
                 */
                s->rex_b = CODE64(s) ? (~vex2 >> 2) & 8 : 0;
#endif /* __Use_Original_Qemu (U93) */
#endif
                s->vex_w = (vex3 >> 7) & 1;
                switch (vex2 & 0x1f) {
                case 0x01: /* Implied 0f leading opcode bytes.  */
                    decode_func = decode_0F;
                    break;
                case 0x02: /* Implied 0f 38 leading opcode bytes.  */
                    decode_func = decode_0F38;
                    break;
                case 0x03: /* Implied 0f 3a leading opcode bytes.  */
                    decode_func = decode_0F3A;
                    break;
#if __Use_Original_Qemu != 1 /* ours (U103) */
                case 0x07: /* map 7: URDMSR / UWRMSR imm32 (U103) */
                    decode_func = decode_MAP7;
                    break;
#endif /* __Use_Original_Qemu (U103) */
                default:   /* Reserved for future use.  */
                    goto unknown_op;
                }
            }
            s->vex_v = (~vex3 >> 3) & 0xf;
            s->vex_l = (vex3 >> 2) & 1;
            s->prefix |= pp_prefix[vex3 & 3] | PREFIX_VEX;
        }
        break;
    default:
        if (b >= 0x100) {
            b -= 0x100;
            decode_func = do_decode_0F;
        }
        break;
    }

    /* Post-process prefixes.  */
    if (CODE64(s)) {
        /*
         * In 64-bit mode, the default data size is 32-bit.  Select 64-bit
         * data with rex_w, and 16-bit data with 0x66; rex_w takes precedence
         * over 0x66 if both are present.
         */
        s->dflag = (REX_W(s) ? MO_64 : s->prefix & PREFIX_DATA ? MO_16 : MO_32);
        /* In 64-bit mode, 0x67 selects 32-bit addressing.  */
        s->aflag = (s->prefix & PREFIX_ADR ? MO_32 : MO_64);
    } else {
        /* In 16/32-bit mode, 0x66 selects the opposite data size.  */
        if (CODE32(s) ^ ((s->prefix & PREFIX_DATA) != 0)) {
            s->dflag = MO_32;
        } else {
            s->dflag = MO_16;
        }
        /* In 16/32-bit mode, 0x67 selects the opposite addressing.  */
        if (CODE32(s) ^ ((s->prefix & PREFIX_ADR) != 0)) {
            s->aflag = MO_32;
        }  else {
            s->aflag = MO_16;
        }
    }

    memset(&decode, 0, sizeof(decode));
    decode.b = b;
    if (!decode_insn(s, env, decode_func, &decode)) {
        goto illegal_op;
    }
    if (!decode.e.gen) {
        goto unknown_op;
    }

    if (!has_cpuid_feature(s, decode.e.cpuid)) {
        goto illegal_op;
    }

    switch (decode.e.special) {
    case X86_SPECIAL_None:
        break;

    case X86_SPECIAL_Locked:
        if (decode.op[0].has_ea) {
            s->prefix |= PREFIX_LOCK;
        }
        break;

    case X86_SPECIAL_ProtMode:
        if (!PE(s) || VM86(s)) {
            goto illegal_op;
        }
        break;

    case X86_SPECIAL_i64:
        if (CODE64(s)) {
            goto illegal_op;
        }
        break;
    case X86_SPECIAL_o64:
        if (!CODE64(s)) {
            goto illegal_op;
        }
        break;

    case X86_SPECIAL_ZExtOp0:
        assert(decode.op[0].unit == X86_OP_INT);
        if (!decode.op[0].has_ea) {
            decode.op[0].ot = MO_32;
        }
        break;

    case X86_SPECIAL_ZExtOp2:
        assert(decode.op[2].unit == X86_OP_INT);
        if (!decode.op[2].has_ea) {
            decode.op[2].ot = MO_32;
        }
        break;

    case X86_SPECIAL_AVXExtMov:
        if (!decode.op[2].has_ea) {
            decode.op[2].ot = s->vex_l ? MO_256 : MO_128;
        } else if (s->vex_l) {
            decode.op[2].ot++;
        }
        break;

    default:
        break;
    }

    if (!validate_vex(s, &decode)) {
        return;
    }
#if __Use_Original_Qemu != 1 /* ours (U50) */
    /*
     * NoVmp (ledger U50): "If an x87 FPU floating-point exception is pending
     * and the processor encounters an MMX instruction, the processor
     * generates a #MF prior to executing the MMX instruction" (SDM Vol3
     * 14.5.1, Tables 24-4..24-9), before any memory access. CVTPI2PD xmm, m64
     * takes no x87 exception (SDM); CVTPI2PS xmm, m64 does per its SDM page,
     * not on hardware (UC_X86_QUIRK_CVTPI2PS_M64_KEEPS_X87).
     */
    if ((decode.e.special == X86_SPECIAL_MMX &&
         !(s->prefix & (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA))) ||
        decode.e.gen == gen_CVTPx2PI || decode.e.gen == gen_CVTTPx2PI ||
        decode.e.gen == gen_MOVq_dq || decode.e.gen == gen_EMMS ||
        (decode.e.gen == gen_CVTPI2Px && !decode.op[2].has_ea)) {
        gen_helper_fwait(tcg_ctx, cpu_env);
    } else if (decode.e.gen == gen_CVTPI2Px && !(s->prefix & PREFIX_DATA)) {
        gen_helper_cvtpi2ps_m64_fwait(tcg_ctx, cpu_env);
    }
#endif /* __Use_Original_Qemu (U50) */
    if (decode.e.special == X86_SPECIAL_MMX &&
        !(s->prefix & (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA))) {
        gen_helper_enter_mmx(tcg_ctx, cpu_env);
    }

    if (decode.op[0].has_ea || decode.op[1].has_ea || decode.op[2].has_ea) {
        gen_load_ea(s, &decode.mem, decode.e.vex_class == 12);
    }
    if (s->prefix & PREFIX_LOCK) {
        if (decode.op[0].unit != X86_OP_INT || !decode.op[0].has_ea) {
            goto illegal_op;
        }
        gen_load(s, &decode, 2, s->T1);
        decode.e.gen(s, env, &decode);
    } else {
        if (decode.op[0].unit == X86_OP_MMX) {
            compute_mmx_offset(&decode.op[0]);
        } else if (decode.op[0].unit == X86_OP_SSE) {
            compute_xmm_offset(&decode.op[0]);
        }
        gen_load(s, &decode, 1, s->T0);
        gen_load(s, &decode, 2, s->T1);
#if __Use_Original_Qemu == 1 /* original QEMU (U67) */
        decode.e.gen(s, env, &decode);
#else /* ours (U67) */
        /*
         * NoVmp (ledger U67): SIMD floating-point instructions (exception
         * classes 2/3, FMA, F16C, the CVT MMX forms) check their new MXCSR
         * exceptions before the destination is written back: snapshot the
         * destination register, run, then helper_sse_fp_end restores it and
         * raises #XM/#UD on an unmasked exception. VCVTPS2PH m does it itself.
         */
        {
            bool xm = ((decode.e.vex_class == 2 || decode.e.vex_class == 3) &&
                       decode.e.gen != gen_VMPSADBW) ||
                      decode.e.cpuid == X86_FEAT_FMA || decode.e.vex_class == 11 ||
                      decode.e.gen == gen_CVTPI2Px || decode.e.gen == gen_CVTPx2PI ||
                      decode.e.gen == gen_CVTTPx2PI;
            int xm_len = 0, xm_ofs = 0;

            if (xm && decode.e.gen == gen_VCVTPS2PH && decode.op[0].has_ea) {
                xm = false;
            }
            if (xm && !decode.op[0].has_ea &&
                (decode.op[0].unit == X86_OP_SSE || decode.op[0].unit == X86_OP_MMX)) {
                xm_ofs = vector_reg_offset(&decode.op[0]);
                /* U126: the whole ZMM register (MAXVL = 512); xm_save is a ZMMReg */
                xm_len = decode.op[0].unit == X86_OP_SSE ? (int)sizeof(ZMMReg) : 8;
                tcg_gen_gvec_mov(tcg_ctx, MO_64, offsetof(CPUX86State, xm_save), xm_ofs,
                                 xm_len, xm_len);
            }
            if (xm) {
                gen_helper_sse_fp_begin(tcg_ctx, cpu_env);
            }
            decode.e.gen(s, env, &decode);
            if (xm) {
                gen_helper_sse_fp_end(tcg_ctx, cpu_env, tcg_constant_i32(tcg_ctx, xm_ofs),
                                      tcg_constant_i32(tcg_ctx, xm_len));
            }
        }
#endif /* __Use_Original_Qemu (U67) */
        gen_writeback(s, &decode, 0, s->T0);
#if __Use_Original_Qemu != 1 /* ours (U44) */
        if (decode.op[0].unit == X86_OP_MMX && !decode.op[0].has_ea) {
            /*
             * NoVmp (ledger U44): "When an MMX instruction writes to an MMX
             * register, it writes ones (11B) to the exponent part of the
             * corresponding floating-point register (bits 64 through 79)"
             * (SDM Vol1 9.5.2). MMX registers are not rotated by TOP.
             */
            TCGv_i32 ones = tcg_const_i32(tcg_ctx, 0xffff);
            tcg_gen_st16_i32(tcg_ctx, ones, cpu_env,
                             offsetof(CPUX86State, fpregs[decode.op[0].n & 7].d.high));
            tcg_temp_free_i32(tcg_ctx, ones);
        }
#endif /* __Use_Original_Qemu (U44) */
    }
    decode_temps_free(tcg_ctx, &decode);
    return;
 illegal_op:
    gen_illegal_opcode(s);
    return;
 unknown_op:
    gen_unknown_opcode(env, s);
}
