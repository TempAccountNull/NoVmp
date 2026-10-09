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
#define lock .special = X86_SPECIAL_HasLock,
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
        [2] = X86_OP_ENTRYr(LDMXCSR,    E,d, vex5 chk(VEX128)),     /* 183e6679e3 (U486) */
        [3] = X86_OP_ENTRYw(STMXCSR,    E,d, vex5 chk(VEX128)),
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
#if __Use_Original_Qemu != 1 /* ours (U180) */
    static const X86OpEntry amx_6c_reg[4] = {
        [0] = X86_OP_ENTRY0(TCMMRLFP16PS, vex22 cpuid(AMX_COMPLEX) p_00),
        [1] = X86_OP_ENTRY0(TCMMIMFP16PS, vex22 cpuid(AMX_COMPLEX) p_66),
    };
#endif /* __Use_Original_Qemu (U180) */
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
#if __Use_Original_Qemu != 1 /* ours (U180) */
    case 0x6c:
        if (mod == 3) {
            *entry = *decode_by_prefix(s, amx_6c_reg);
        }
        break;
#endif /* __Use_Original_Qemu (U180) */
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
#if __Use_Original_Qemu != 1 /* ours (U180) */
    [0x6c] = X86_OP_GROUP0(0F38_AMX),
#endif /* __Use_Original_Qemu (U180) */

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
#if __Use_Original_Qemu != 1 /* ours (U645) */
/*
 * NoVmp (ledger U645): Intel APX EVEX map 4, the instructions promoted from legacy maps 2 and 3
 * (APX spec 355828-009 3.1.2.3.1 2.(e), the table of 3.1.5, chapter 6; the map 0/1 ones are
 * decoded by apx_map4_decode in translate.c). pp = EVEX.pp (0 NP, 1 66, 2 F3, 3 F2):
 *   60 /r NP/66 MOVBE rv, rv/mv   61 /r NP/66 MOVBE rv/mv, rv   (also ModRM.mod = 11b)
 *   65 !(11) 66 WRUSSD/Q m, r     66 !(11) NP WRSSD/Q m, r
 *   66 /r 66 ADCX, F3 ADOX ry, ry/my; ND = 1: ADCX/ADOX ry_n, ry_r, ry/my (NDD)
 *   8A !(11) NP W0 MOVRS r8, m8   8B !(11) NP/66 MOVRS rv, mv
 *   F0 /r NP CRC32 ry, r/m8       F1 /r NP/66 CRC32 ry, rv/mv
 *   F8 !(11) 66 MOVDIR64B, F2 ENQCMD, F3 ENQCMDS; F8 11 F2 URDMSR, F3 UWRMSR (W0)
 *   F9 !(11) NP MOVDIRI           FC !(11) NP AADD, 66 AAND, F3 AXOR, F2 AOR
 * INVEPT / INVVPID / INVPCID (F3 F0 / F1 / F2) need VMX / INVPCID, which this CPU model does
 * not implement: #UD, like every other combination. Byte registers 4-7 are SPL..DIL.
 */
static void decode_apx_map4(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry movbe_ld = X86_OP_ENTRY3(MOVBE_APX, G,v, E,v, None,None, cpuid(MOVBE));
    static const X86OpEntry movbe_st = X86_OP_ENTRY3(MOVBE_APX, E,v, G,v, None,None, cpuid(MOVBE));
    static const X86OpEntry adcx_nd = X86_OP_ENTRY3(ADCX, B,y, G,y, E,y, cpuid(ADX));
    static const X86OpEntry adox_nd = X86_OP_ENTRY3(ADOX, B,y, G,y, E,y, cpuid(ADX));
    static const X86OpEntry crc32_b = X86_OP_ENTRY2(CRC32, G,d, E,b, cpuid(SSE42));
    static const X86OpEntry crc32_v = X86_OP_ENTRY2(CRC32, G,d, E,v, cpuid(SSE42));
    int pp = s->evex_p1 & 3;
    bool nd = (s->evex_p2 & 0x10) != 0;
    bool mem;

    *b = x86_ldub_code(env, s);
    mem = (get_modrm(s, env) >> 6) != 3;
    memset(entry, 0, sizeof(*entry));
    s->apx_promoted = true;
    s->prefix |= PREFIX_REX;            /* "GPR register ids [4,5,6,7] address [SPL..DIL]" */
    switch (*b) {
    case 0x60:
        if (pp <= 1) {
            *entry = movbe_ld;
        }
        break;
    case 0x61:
        if (pp <= 1) {
            *entry = movbe_st;
        }
        break;
    case 0x65:
        if (pp == 1 && mem) {
            *entry = opcodes_0F38_F0toFF[5][1];         /* WRUSSD / WRUSSQ */
        }
        break;
    case 0x66:
        if (pp == 0 && mem) {
            *entry = opcodes_0F38_F0toFF[6][0];         /* WRSSD / WRSSQ */
        } else if (pp == 1) {
            *entry = nd ? adcx_nd : opcodes_0F38_F0toFF[6][1];
        } else if (pp == 2) {
            *entry = nd ? adox_nd : opcodes_0F38_F0toFF[6][2];
        }
        break;
    case 0x8a:
        if (pp == 0 && mem && !s->vex_w) {
            *entry = opcodes_0F38_00toEF[0x8a];         /* MOVRS r8, m8 (W0) */
        }
        break;
    case 0x8b:
        if (pp <= 1 && mem) {
            *entry = opcodes_0F38_00toEF[0x8b];         /* MOVRS rv, mv */
        }
        break;
    case 0xf0:
        if (pp == 0) {
            *entry = crc32_b;
        }
        break;
    case 0xf1:
        if (pp <= 1) {
            *entry = crc32_v;
        }
        break;
    case 0xf8:
        if (pp == 1 && mem) {
            *entry = opcodes_0F38_F0toFF[8][1];         /* MOVDIR64B */
        } else if (pp >= 2) {
            /* F3: UWRMSR (11) / ENQCMDS, F2: URDMSR (11) / ENQCMD (group, by ModRM.mod) */
            *entry = opcodes_0F38_F0toFF[8][pp == 2 ? 2 : 3];
        }
        break;
    case 0xf9:
        if (pp == 0 && mem) {
            *entry = opcodes_0F38_F0toFF[9][0];         /* MOVDIRI */
        }
        break;
    case 0xfc:
        if (mem) {
            *entry = opcodes_0F38_F0toFF[12][pp];       /* AADD AAND AXOR AOR */
        }
        break;
    default:
        break;
    }
}

#endif /* __Use_Original_Qemu (U645) */
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

#if __Use_Original_Qemu != 1 /* ours (U141) */
/*
 * NoVmp (ledger U141): EVEX opcode maps 1-3 (EVEX.mmm = 001b/010b/011b), separate from
 * the VEX/legacy tables: 29 VEX/EVEX slots hold a different instruction and EVEX.W is
 * mostly an opcode extension (Emulator/data/evex_forms_summary.txt). An empty slot is #UD.
 */
#if __Use_Original_Qemu != 1 /* ours (U148) */
/*
 * NoVmp (ledger U148): EVEX table attributes (X86OpEntry.evex_*, decode-new.h), per form as
 * in Emulator/data/evex_forms.tsv (SDM tuple type, masking, {er}/{sae}, W, VL, class).
 */
#define ev_vl(v)     .evex_vl = (v),
#define ev_tt(t)     .evex_tt = X86_EVEX_TT_##t,
#define ev_mask(m)   .evex_mask = X86_EVEX_MASK_##m,
#define ev_rc(r)     .evex_rc = X86_EVEX_RC_##r,
#define ev_w(w)      .evex_w = X86_EVEX_##w,
#define ev_es(e)     .evex_es = X86_EVEX_ES_##e,
#define ev_e1        .evex_align = 1,
#define ev_fp        .evex_fp = 1,
#define ev_w32       .evex_w32 = 1,
#if __Use_Original_Qemu != 1 /* ours (U193) */
/* E*NF classes: memory operands without fault suppression (U193) */
#define ev_nf        .evex_nofs = 1,
#endif /* __Use_Original_Qemu (U193) */
#if __Use_Original_Qemu != 1 /* ours (U190) */
#define ev_dsrc      .evex_dsrc = 1,    /* destination is also a source (U190) */
#endif /* __Use_Original_Qemu (U190) */
#if __Use_Original_Qemu != 1 /* ours (U191) */
#define ev_msrc1     .evex_msrc1 = 1,   /* masked-off lanes take SRC1 (U191) */
#endif /* __Use_Original_Qemu (U191) */
#if __Use_Original_Qemu != 1 /* ours (U230) */
/* destination / writemask element size when it differs from ev_es (U230; U260 merged) */
#define ev_ds(d)     .evex_ds = X86_EVEX_ES_##d,
#endif /* __Use_Original_Qemu (U230) */
#define EV_VLALL     (X86_EVEX_VL128 | X86_EVEX_VL256 | X86_EVEX_VL512)
/* vector moves: Full Mem tuple, {k1}{z}, element size from EVEX.W, no EVEX.b */
#define EV_MOV(w)    cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(FULL_MEM) ev_mask(MZ) ev_w(w) ev_es(W)
#if __Use_Original_Qemu != 1 /* ours (U261) */
/*
 * NoVmp (ledger U261-U269): AVX512BW forms (Emulator/data/evex_forms.tsv, SDM Vol2B/2C):
 * CPUID AVX512BW (+ AVX512VL below 512 bits, validate_evex), byte/word writemasks of up
 * to 64 bits. EV_BW: Full Mem tuple, {k1}{z}, no {1toN}. ev_nf: exception class E4NF /
 * E9NF (the whole memory operand is read whatever the mask, no fault suppression).
 */
#define EV_BW(w, es) cpuid(AVX512BW) ev_vl(EV_VLALL) ev_tt(FULL_MEM) ev_mask(MZ) ev_w(w) ev_es(es)

/* EVEX.W as an opcode extension: entries[0] = W0, entries[1] = W1 */
static const X86OpEntry *decode_evex_by_w(DisasContext *s, const X86OpEntry entries[2])
{
    return &entries[s->vex_w];
}
#endif /* __Use_Original_Qemu (U261) */
#if __Use_Original_Qemu != 1 /* ours (U195) */

/*
 * NoVmp (ledger U195): F3/F2 0F 10/11 VMOVSS (W0) / VMOVSD (W1), EVEX.LLIG, class E10 (SDM
 * Vol2B MOVSS/MOVSD): 10 mod != 11b: xmm1{k1}{z}, m32/m64 (Tuple1 Scalar); 10 mod = 11b:
 * xmm1{k1}{z}, xmm2 (vvvv), xmm3 (r/m); 11 mod = 11b: xmm1 (r/m){k1}{z}, xmm2 (vvvv), xmm3
 * (reg); 11 mod != 11b: m32/m64{k1}, xmm1 - merging only ({z} #UD, Table 2-42), vvvv
 * reserved (1111b) in both memory forms.
 */
#define EV_MOVS(w)   cpuid(AVX512F) ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_w(w) ev_es(W)

static void decode_EVEX_movs(DisasContext *s, CPUX86State *env, X86OpEntry *entry, bool store)
{
    /* [store][mod = 11b][VMOVSD] */
    static const X86OpEntry opcodes_EVEX_movs[2][2][2] = {
        {
            {
                X86_OP_ENTRY3(EVEX_VMOVS, V,x, None,None, W,ss, EV_MOVS(W0) ev_mask(MZ)),
                X86_OP_ENTRY3(EVEX_VMOVS, V,x, None,None, W,sd, EV_MOVS(W1) ev_mask(MZ)),
            },
            {
                X86_OP_ENTRY3(EVEX_VMOVS, V,x, H,x, W,ss, EV_MOVS(W0) ev_mask(MZ)),
                X86_OP_ENTRY3(EVEX_VMOVS, V,x, H,x, W,sd, EV_MOVS(W1) ev_mask(MZ)),
            },
        },
        {
            {
                X86_OP_ENTRY3(EVEX_VMOVS, W,ss, None,None, V,ss, EV_MOVS(W0) ev_mask(M)),
                X86_OP_ENTRY3(EVEX_VMOVS, W,sd, None,None, V,sd, EV_MOVS(W1) ev_mask(M)),
            },
            {
                X86_OP_ENTRY3(EVEX_VMOVS, W,x, H,x, V,ss, EV_MOVS(W0) ev_mask(MZ)),
                X86_OP_ENTRY3(EVEX_VMOVS, W,x, H,x, V,sd, EV_MOVS(W1) ev_mask(MZ)),
            },
        },
    };
    bool reg = (get_modrm(s, env) >> 6) == 3;

    *entry = opcodes_EVEX_movs[store][reg][(s->prefix & PREFIX_REPNZ) != 0];
}
#undef EV_MOVS
#endif /* __Use_Original_Qemu (U195) */

/* 0F 10/11: VMOVUPS (NP W0), VMOVUPD (66 W1); E4.nb. F3/F2 = VMOVSS/VMOVSD (U195) */
static void decode_EVEX_0F10(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F10[4] = {
        X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, W,x, EV_MOV(W0)), /* vmovups */
        X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, W,x, EV_MOV(W1)), /* vmovupd */
        {},
        {},
    };
#if __Use_Original_Qemu != 1 /* ours (U195) */
    if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) {
        decode_EVEX_movs(s, env, entry, false);
        return;
    }
#endif /* __Use_Original_Qemu (U195) */
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F10);
}

static void decode_EVEX_0F11(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F11[4] = {
        X86_OP_ENTRY3(EVEX_MOV, W,x, None,None, V,x, EV_MOV(W0)), /* vmovups */
        X86_OP_ENTRY3(EVEX_MOV, W,x, None,None, V,x, EV_MOV(W1)), /* vmovupd */
        {},
        {},
    };
#if __Use_Original_Qemu != 1 /* ours (U195) */
    if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) {
        decode_EVEX_movs(s, env, entry, true);
        return;
    }
#endif /* __Use_Original_Qemu (U195) */
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F11);
}

/* 0F 28/29: VMOVAPS (NP W0), VMOVAPD (66 W1); E1 */
static void decode_EVEX_0F28(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F28[4] = {
        X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, W,x, EV_MOV(W0) ev_e1), /* vmovaps */
        X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, W,x, EV_MOV(W1) ev_e1), /* vmovapd */
        {},
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F28);
}

static void decode_EVEX_0F29(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F29[4] = {
        X86_OP_ENTRY3(EVEX_MOV, W,x, None,None, V,x, EV_MOV(W0) ev_e1), /* vmovaps */
        X86_OP_ENTRY3(EVEX_MOV, W,x, None,None, V,x, EV_MOV(W1) ev_e1), /* vmovapd */
        {},
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F29);
}

/* 0F 6F/7F: VMOVDQA32/64 (66, E1), VMOVDQU32/64 (F3, E4.nb); F2 = VMOVDQU8/16 (BW, not yet) */
static void decode_EVEX_0F6F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F6F[4] = {
        {},
        X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, W,x, EV_MOV(WSEL) ev_e1), /* vmovdqa32/64 */
        X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, W,x, EV_MOV(WSEL)),       /* vmovdqu32/64 */
        {},
    };
#if __Use_Original_Qemu != 1 /* ours (U261) */
    /* NoVmp (ledger U261): F2 W0 VMOVDQU8, F2 W1 VMOVDQU16 (AVX512BW, E4.nb) */
    static const X86OpEntry vmovdqu8_16[2] = {
        X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, W,x, EV_BW(W0, 8)),
        X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, W,x, EV_BW(W1, 16)),
    };

    if (s->prefix & PREFIX_REPNZ) {
        *entry = *decode_evex_by_w(s, vmovdqu8_16);
        return;
    }
#endif /* __Use_Original_Qemu (U261) */
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F6F);
}

static void decode_EVEX_0F7F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F7F[4] = {
        {},
        X86_OP_ENTRY3(EVEX_MOV, W,x, None,None, V,x, EV_MOV(WSEL) ev_e1), /* vmovdqa32/64 */
        X86_OP_ENTRY3(EVEX_MOV, W,x, None,None, V,x, EV_MOV(WSEL)),       /* vmovdqu32/64 */
        {},
    };
#if __Use_Original_Qemu != 1 /* ours (U261) */
    static const X86OpEntry vmovdqu8_16[2] = {
        X86_OP_ENTRY3(EVEX_MOV, W,x, None,None, V,x, EV_BW(W0, 8)),
        X86_OP_ENTRY3(EVEX_MOV, W,x, None,None, V,x, EV_BW(W1, 16)),
    };

    if (s->prefix & PREFIX_REPNZ) {
        *entry = *decode_evex_by_w(s, vmovdqu8_16);
        return;
    }
#endif /* __Use_Original_Qemu (U261) */
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F7F);
}
#endif /* __Use_Original_Qemu (U148) */
#if __Use_Original_Qemu != 1 /* ours (U149) */
/* NoVmp (ledger U149): Full tuple integer forms, 66 only, {k1}{z}, {1toN}; E4 */
#define EV_INT(w)    cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(w) ev_es(W) p_66
#endif /* __Use_Original_Qemu (U149) */
#if __Use_Original_Qemu != 1 /* ours (U151) */
/*
 * NoVmp (ledger U151): 0F 58/59/5C VADDPS/VMULPS/VSUBPS (NP W0), VADDPD/VMULPD/VSUBPD
 * (66 W1): Full tuple, {k1}{z}, {1toN}, {er}; E2 (#XM). F3/F2 = scalar forms (U194).
 */
#define EV_FP3(op, w) X86_OP_ENTRY3(op, V,x, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL) \
                          ev_tt(FULL) ev_mask(MZ) ev_rc(ER) ev_w(w) ev_es(W) ev_fp)
#if __Use_Original_Qemu != 1 /* ours (U194) */
/*
 * NoVmp (ledger U194): F3 (W0, m32) / F2 (W1, m64) scalar forms xmm1{k1}{z}, xmm2 (vvvv),
 * xmm3/m{er|sae}: EVEX.LLIG, Tuple1 Scalar, class E3 (SDM Vol2A/2B ADDSS ... SQRTSD).
 */
#define EV_FPSS(op, rc) X86_OP_ENTRY3(op, V,x, H,x, W,ss, cpuid(AVX512F) ev_vl(X86_EVEX_LIG) \
                          ev_tt(T1S) ev_mask(MZ) ev_rc(rc) ev_w(W0) ev_es(W) ev_fp)
#define EV_FPSD(op, rc) X86_OP_ENTRY3(op, V,x, H,x, W,sd, cpuid(AVX512F) ev_vl(X86_EVEX_LIG) \
                          ev_tt(T1S) ev_mask(MZ) ev_rc(rc) ev_w(W1) ev_es(W) ev_fp)
#endif /* __Use_Original_Qemu (U194) */

static void decode_EVEX_0F58(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F58[4] = {
        EV_FP3(EVEX_VADD, W0), EV_FP3(EVEX_VADD, W1),
#if __Use_Original_Qemu != 1 /* ours (U194) */
        EV_FPSS(EVEX_VADDS, ER), EV_FPSD(EVEX_VADDS, ER),
#endif /* __Use_Original_Qemu (U194) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F58);
}

static void decode_EVEX_0F59(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F59[4] = {
        EV_FP3(EVEX_VMUL, W0), EV_FP3(EVEX_VMUL, W1),
#if __Use_Original_Qemu != 1 /* ours (U194) */
        EV_FPSS(EVEX_VMULS, ER), EV_FPSD(EVEX_VMULS, ER),
#endif /* __Use_Original_Qemu (U194) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F59);
}

static void decode_EVEX_0F5C(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F5C[4] = {
        EV_FP3(EVEX_VSUB, W0), EV_FP3(EVEX_VSUB, W1),
#if __Use_Original_Qemu != 1 /* ours (U194) */
        EV_FPSS(EVEX_VSUBS, ER), EV_FPSD(EVEX_VSUBS, ER),
#endif /* __Use_Original_Qemu (U194) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F5C);
}
#endif /* __Use_Original_Qemu (U151) */
#if __Use_Original_Qemu != 1 /* ours (U154) */
/* NoVmp (ledger U154): 0F38 27: VPTESTMD/Q (66), VPTESTNMD/Q (F3); k1 {k2}, Full, E4 */
static void decode_EVEX_0F38_27(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_27[4] = {
        {},
        X86_OP_ENTRY3(EVEX_VPTESTM,  K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                      ev_tt(FULL) ev_mask(KDEST) ev_w(WSEL) ev_es(W)),
        X86_OP_ENTRY3(EVEX_VPTESTNM, K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                      ev_tt(FULL) ev_mask(KDEST) ev_w(WSEL) ev_es(W)),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_27);
}
#endif /* __Use_Original_Qemu (U154) */
#if __Use_Original_Qemu != 1 /* ours (U156) */
/*
 * NoVmp (ledger U156): 0F 51 VSQRTPS/PD ({er}), 5D VMINPS/PD ({sae}), 5E VDIVPS/PD ({er}),
 * 5F VMAXPS/PD ({sae}): NP W0 / 66 W1, Full tuple, {k1}{z}, {1toN}; E2.
 */
#define EV_FPR(op, w, rc) X86_OP_ENTRY3(op, V,x, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL) \
                          ev_tt(FULL) ev_mask(MZ) ev_rc(rc) ev_w(w) ev_es(W) ev_fp)

static void decode_EVEX_0F51(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F51[4] = {
        X86_OP_ENTRY3(EVEX_VSQRT, V,x, None,None, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                      ev_tt(FULL) ev_mask(MZ) ev_rc(ER) ev_w(W0) ev_es(W) ev_fp),
        X86_OP_ENTRY3(EVEX_VSQRT, V,x, None,None, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                      ev_tt(FULL) ev_mask(MZ) ev_rc(ER) ev_w(W1) ev_es(W) ev_fp),
#if __Use_Original_Qemu != 1 /* ours (U194) */
        EV_FPSS(EVEX_VSQRTS, ER), EV_FPSD(EVEX_VSQRTS, ER),
#endif /* __Use_Original_Qemu (U194) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F51);
}

static void decode_EVEX_0F5D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F5D[4] = {
        EV_FPR(EVEX_VMIN, W0, SAE), EV_FPR(EVEX_VMIN, W1, SAE),
#if __Use_Original_Qemu != 1 /* ours (U194) */
        EV_FPSS(EVEX_VMINS, SAE), EV_FPSD(EVEX_VMINS, SAE),
#endif /* __Use_Original_Qemu (U194) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F5D);
}

static void decode_EVEX_0F5E(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F5E[4] = {
        EV_FPR(EVEX_VDIV, W0, ER), EV_FPR(EVEX_VDIV, W1, ER),
#if __Use_Original_Qemu != 1 /* ours (U194) */
        EV_FPSS(EVEX_VDIVS, ER), EV_FPSD(EVEX_VDIVS, ER),
#endif /* __Use_Original_Qemu (U194) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F5E);
}

static void decode_EVEX_0F5F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F5F[4] = {
        EV_FPR(EVEX_VMAX, W0, SAE), EV_FPR(EVEX_VMAX, W1, SAE),
#if __Use_Original_Qemu != 1 /* ours (U194) */
        EV_FPSS(EVEX_VMAXS, SAE), EV_FPSD(EVEX_VMAXS, SAE),
#endif /* __Use_Original_Qemu (U194) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F5F);
}
#endif /* __Use_Original_Qemu (U156) */
#if __Use_Original_Qemu != 1 /* ours (U158) */
/*
 * NoVmp (ledger U158): 66 0F 72/73 groups, EVEX.vvvv = destination, r/m = source (register,
 * memory or {1toN}: Full tuple), imm8; ModRM.reg is the opcode extension; E4.
 * 72: /0 VPRORD/Q, /1 VPROLD/Q, /2 VPSRLD, /4 VPSRAD/Q, /6 VPSLLD; 73: /2 VPSRLQ, /6 VPSLLQ
 * (73 /3, /7 VPSRLDQ/VPSLLDQ are AVX512BW, not yet).
 */
#define EV_SHI(op, w) X86_OP_ENTRY4(op, H,x, W,x, None,None, cpuid(AVX512F) ev_vl(EV_VLALL) \
                          ev_tt(FULL) ev_mask(MZ) ev_w(w) ev_es(W) p_66)

static void decode_EVEX_0F72(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F72[8] = {
        EV_SHI(EVEX_VPRORI, WSEL),
        EV_SHI(EVEX_VPROLI, WSEL),
        EV_SHI(EVEX_VPSRLI, W0),
        {},
        EV_SHI(EVEX_VPSRAI, WSEL),
        {},
        EV_SHI(EVEX_VPSLLI, W0),
        {},
    };
    *entry = opcodes_EVEX_0F72[(get_modrm(s, env) >> 3) & 7];
}

static void decode_EVEX_0F73(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
#if __Use_Original_Qemu != 1 /* ours (U265) */
    /*
     * NoVmp (ledger U265): /3 VPSRLDQ, /7 VPSLLDQ zmm1(vvvv), zmm2/m512, imm8 (AVX512BW,
     * Full Mem, no writemask, E4NF)
     */
    static const X86OpEntry bytesh[2] = {
        X86_OP_ENTRY4(EVEX_VPSRLDQ, H,x, W,x, None,None, cpuid(AVX512BW) ev_vl(EV_VLALL)
                      ev_tt(FULL_MEM) ev_mask(NONE) ev_w(WIG) ev_es(8) ev_nf p_66),
        X86_OP_ENTRY4(EVEX_VPSLLDQ, H,x, W,x, None,None, cpuid(AVX512BW) ev_vl(EV_VLALL)
                      ev_tt(FULL_MEM) ev_mask(NONE) ev_w(WIG) ev_es(8) ev_nf p_66),
    };
    int ext = (get_modrm(s, env) >> 3) & 7;

    if (ext == 3 || ext == 7) {
        *entry = bytesh[ext == 7];
        return;
    }
#endif /* __Use_Original_Qemu (U265) */
    static const X86OpEntry opcodes_EVEX_0F73[8] = {
        {}, {},
        EV_SHI(EVEX_VPSRLI, W1),
        {}, {}, {},
        EV_SHI(EVEX_VPSLLI, W1),
        {},
    };
    *entry = opcodes_EVEX_0F73[(get_modrm(s, env) >> 3) & 7];
}
#endif /* __Use_Original_Qemu (U158) */
#if __Use_Original_Qemu != 1 /* ours (U196) */
/*
 * NoVmp (ledger U196): 0F 2E VUCOMISS (NP W0) / VUCOMISD (66 W1), 0F 2F VCOMISS / VCOMISD:
 * xmm1, xmm2/m32/m64{sae}; EVEX.LLIG, Tuple1 Scalar, no masking (aaa and z 0), vvvv
 * reserved, class E3NF (no fault suppression).
 */
#define EV_COMI(op, w, sz) X86_OP_ENTRY3(op, None,None, V,sz, W,sz, cpuid(AVX512F)          \
                               ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(NONE) ev_rc(SAE)      \
                               ev_w(w) ev_es(W) ev_fp ev_nf)

static void decode_EVEX_0F2E(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F2E[4] = {
        EV_COMI(EVEX_VUCOMI, W0, ss), EV_COMI(EVEX_VUCOMI, W1, sd), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F2E);
}

static void decode_EVEX_0F2F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F2F[4] = {
        EV_COMI(EVEX_VCOMI, W0, ss), EV_COMI(EVEX_VCOMI, W1, sd), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F2F);
}
#undef EV_COMI
#endif /* __Use_Original_Qemu (U196) */
#if __Use_Original_Qemu != 1 /* ours (U197) */
/*
 * NoVmp (ledger U197): 0F C2 ib VCMPPS (NP W0) / VCMPPD (66 W1) k1{k2}, xmm2/ymm2/zmm2,
 * r/m/{1toN}{sae}, imm8: Full tuple, k destination, class E2. U198: F3 VCMPSS (W0) / F2
 * VCMPSD (W1) k1{k2}, xmm2, xmm3/m32/m64{sae}, imm8: EVEX.LLIG, Tuple1 Scalar, class E3.
 */
static void decode_EVEX_0FC2(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0FC2[4] = {
        X86_OP_ENTRY4(EVEX_VCMP, K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(FULL)
                      ev_mask(KDEST) ev_rc(SAE) ev_w(W0) ev_es(W) ev_fp),
        X86_OP_ENTRY4(EVEX_VCMP, K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(FULL)
                      ev_mask(KDEST) ev_rc(SAE) ev_w(W1) ev_es(W) ev_fp),
#if __Use_Original_Qemu != 1 /* ours (U198) */
        X86_OP_ENTRY4(EVEX_VCMP, K,q, H,x, W,ss, cpuid(AVX512F) ev_vl(X86_EVEX_LIG) ev_tt(T1S)
                      ev_mask(KDEST) ev_rc(SAE) ev_w(W0) ev_es(W) ev_fp),
        X86_OP_ENTRY4(EVEX_VCMP, K,q, H,x, W,sd, cpuid(AVX512F) ev_vl(X86_EVEX_LIG) ev_tt(T1S)
                      ev_mask(KDEST) ev_rc(SAE) ev_w(W1) ev_es(W) ev_fp),
#endif /* __Use_Original_Qemu (U198) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0FC2);
}
#endif /* __Use_Original_Qemu (U197) */
#if __Use_Original_Qemu != 1 /* ours (U200) */
/*
 * NoVmp (ledger U200): scalar FMA (66 0F38 99/9B/9D/9F, A9/AB/AD/AF, B9/BB/BD/BF): W0 = SS
 * (m32), W1 = SD (m64); the memory operand size follows EVEX.W.
 */
static void decode_EVEX_FMAS(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    entry->s2 = s->vex_w ? X86_SIZE_sd : X86_SIZE_ss;
    entry->gen = gen_EVEX_VFMAS;
}
#endif /* __Use_Original_Qemu (U200) */
#if __Use_Original_Qemu != 1 /* ours (U211) */
/*
 * NoVmp (ledger U211): EVEX permutes / shuffles / unpacks (gen_evex_perm). Full tuple,
 * {k1}{z}, {1toN}, element size from EVEX.W, exception class E4NF: no memory fault
 * suppression (SDM Vol2A Table 2-45), so a masked memory operand is read whole.
 */
#define EV_VL256UP   (X86_EVEX_VL256 | X86_EVEX_VL512)
#define EV_PRM(vls, w)  cpuid(AVX512F) ev_vl(vls) ev_tt(FULL) ev_mask(MZ) ev_w(w) ev_es(W) ev_nf
#define EV_PRM3(op, vls, w, ...)  X86_OP_ENTRY3(op, V,x, H,x, W,x, EV_PRM(vls, w) __VA_ARGS__)
#define EV_PRM3I(op, vls, w, ...) X86_OP_ENTRY4(op, V,x, H,x, W,x, EV_PRM(vls, w) __VA_ARGS__)
#define EV_PRM2I(op, vls, w, ...) X86_OP_ENTRY4(op, V,x, None,None, W,x, EV_PRM(vls, w) __VA_ARGS__)

/* 0F 14/15: VUNPCKLPS/VUNPCKHPS (NP W0), VUNPCKLPD/VUNPCKHPD (66 W1) */
static void decode_EVEX_0F14(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F14[4] = {
        EV_PRM3(EVEX_UNPCKL, EV_VLALL, W0), EV_PRM3(EVEX_UNPCKL, EV_VLALL, W1), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F14);
}

static void decode_EVEX_0F15(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F15[4] = {
        EV_PRM3(EVEX_UNPCKH, EV_VLALL, W0), EV_PRM3(EVEX_UNPCKH, EV_VLALL, W1), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F15);
}

/* 0F C6: VSHUFPS (NP W0), VSHUFPD (66 W1) ib */
static void decode_EVEX_0FC6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0FC6[4] = {
        EV_PRM3I(EVEX_VSHUFP, EV_VLALL, W0), EV_PRM3I(EVEX_VSHUFP, EV_VLALL, W1), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0FC6);
}
#endif /* __Use_Original_Qemu (U211) */
#if __Use_Original_Qemu != 1 /* ours (U212) */
/*
 * NoVmp (ledger U212): 0F38 10-15 / 20-25 / 30-35.
 * 66 21-25 / 31-35: VPMOVSX / VPMOVZX BD, BQ, WD, WQ, DQ (Quarter/Eighth/Half Mem source,
 *   W ignored except DQ W0; E5: masked-off source elements are not read).
 * F3 11-15 / 21-25 / 31-35: VPMOVUS / VPMOVS / VPMOV DB, QB, DW, QW, QD (W0; Quarter/
 *   Eighth/Half Mem destination, register or memory; E6: masked-off elements not written).
 * The byte/word forms (x0: VPMOVZXBW, VPMOVWB, ...) are AVX512BW, 66 10-15 other
 * instructions (VPSRLVW, VCVTPH2PS, VPRORVD, ...): not here.
 */
#define EV_PMX(sz, tt, es, w) X86_OP_ENTRY3(EVEX_PMOVX, V,x, None,None, W,sz, cpuid(AVX512F) \
                          ev_vl(EV_VLALL) ev_tt(tt) ev_mask(MZ) ev_w(w) ev_es(es))
#define EV_PMN(sz, tt, es) X86_OP_ENTRY3(EVEX_PMOVN, W,sz, None,None, V,x, cpuid(AVX512F) \
                          ev_vl(EV_VLALL) ev_tt(tt) ev_mask(MZ) ev_w(W0) ev_es(es))

static void decode_EVEX_0F38_PMOV(DisasContext *s, CPUX86State *env, X86OpEntry *entry,
                                  uint8_t *b)
{
    static const X86OpEntry pmovx[6] = {
        {},
        EV_PMX(xq, QUARTER_MEM, 32, WIG),   /* x1 BD */
        EV_PMX(xo, EIGHTH_MEM, 64, WIG),    /* x2 BQ */
        EV_PMX(xh, HALF_MEM, 32, WIG),      /* x3 WD */
        EV_PMX(xq, QUARTER_MEM, 64, WIG),   /* x4 WQ */
        EV_PMX(xh, HALF_MEM, 64, W0),       /* x5 DQ */
    };
    static const X86OpEntry pmovn[6] = {
        {},
        EV_PMN(xq, QUARTER_MEM, 8),         /* x1 DB */
        EV_PMN(xo, EIGHTH_MEM, 8),          /* x2 QB */
        EV_PMN(xh, HALF_MEM, 16),           /* x3 DW */
        EV_PMN(xq, QUARTER_MEM, 16),        /* x4 QW */
        EV_PMN(xh, HALF_MEM, 32),           /* x5 QD */
    };
#if __Use_Original_Qemu != 1 /* ours (U265) */
    /* 66 0F38 11 / 12 (W1): VPSRAVW / VPSLLVW, AVX512BW, Full Mem, E4 (U265) */
    static const X86OpEntry vpshiftvw[2] = {
        X86_OP_ENTRY3(EVEX_VPSRAVW, V,x, H,x, W,x, EV_BW(W1, 16) p_66),
        X86_OP_ENTRY3(EVEX_VPSLLVW, V,x, H,x, W,x, EV_BW(W1, 16) p_66),
    };
#endif /* __Use_Original_Qemu (U265) */
#if __Use_Original_Qemu != 1 /* ours (U235) */
    /* 66 0F38 14 / 15: VPRORVD/Q / VPROLVD/Q, AVX512F, Full, {1toN}, E4 (U235) */
    static const X86OpEntry vprolv[2] = {
        X86_OP_ENTRY3(EVEX_VPRORV, V,x, H,x, W,x, EV_INT(WSEL)),
        X86_OP_ENTRY3(EVEX_VPROLV, V,x, H,x, W,x, EV_INT(WSEL)),
    };
#endif /* __Use_Original_Qemu (U235) */
    int lo = *b & 15;

    memset(entry, 0, sizeof(*entry));
    if (lo < 1 || lo > 5) {
        return;
    }
    if (s->prefix & PREFIX_REPZ) {
        *entry = pmovn[lo];
    } else if ((s->prefix & PREFIX_DATA) && (*b >> 4) != 1) {
        *entry = pmovx[lo];
    }
#if __Use_Original_Qemu != 1 /* ours (U265) */
    else if ((s->prefix & PREFIX_DATA) && (*b == 0x11 || *b == 0x12)) {
        *entry = vpshiftvw[*b - 0x11];
    }
#endif /* __Use_Original_Qemu (U265) */
#if __Use_Original_Qemu != 1 /* ours (U235) */
    else if ((s->prefix & PREFIX_DATA) && (*b == 0x14 || *b == 0x15)) {
        *entry = vprolv[*b - 0x14];
    }
#endif /* __Use_Original_Qemu (U235) */
}
#undef EV_PMX
#undef EV_PMN
#endif /* __Use_Original_Qemu (U212) */
#if __Use_Original_Qemu != 1 /* ours (U214) */
/*
 * NoVmp (ledger U214): EVEX moves.
 * - 0F 12/13/16/17 (SDM Vol2B MOVLPS/MOVLPD/MOVHPS/MOVHPD/MOVHLPS/MOVLHPS): EVEX.128 only,
 *   no masking, E9NF (memory, Tuple2 for PS / Tuple1 Scalar for PD, N = 8) or E7NM.128
 *   (NP 0F 12/16 with ModRM.mod = 11b: VMOVHLPS/VMOVLHPS); 66 forms are memory only.
 *   F3 0F 12/16: VMOVSLDUP/VMOVSHDUP (W0, Full Mem, E4NF.nb); F2 0F 12: VMOVDDUP (W1,
 *   MOVDDUP tuple: m64 at 128 bits, E5NF).
 * - 0F 2B VMOVNTPS/PD, 66 0F E7 VMOVNTDQ, 66 0F38 2A VMOVNTDQA: memory only, no masking,
 *   E1NF (VL-aligned, #GP(0)).
 * - 66 0F 6E/7E VMOVD/VMOVQ with a GPR or memory (W1 = VMOVQ; EVEX.W ignored outside 64-bit
 *   mode, SDM footnote "EVEX.W in non-64 bit is ignored"), F3 0F 7E W1 / 66 0F D6 W1 VMOVQ
 *   xmm-xmm/m64; the W0 forms of F3 0F 7E and 66 0F D6 are AVX10.2 (#UD here).
 */
#define EV_M128(tt, w)  cpuid(AVX512F) ev_vl(X86_EVEX_VL128) ev_tt(tt) ev_mask(NONE) ev_w(w) \
                        ev_es(W) ev_nf
#define EV_DUP(op, sz, tt, w) X86_OP_ENTRY3(op, V,x, None,None, W,sz, cpuid(AVX512F) \
                        ev_vl(EV_VLALL) ev_tt(tt) ev_mask(MZ) ev_w(w) ev_es(W) ev_nf)

static void decode_EVEX_0F12(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vmovhlps =
        X86_OP_ENTRY3(EVEX_MOVHLPS, V,dq, H,dq, U,dq, EV_M128(NONE, W0));
    static const X86OpEntry opcodes_EVEX_0F12[4] = {
        X86_OP_ENTRY3(EVEX_MOVLP_LD, V,dq, H,dq, WM,q, EV_M128(T2, W0)),     /* vmovlps */
        X86_OP_ENTRY3(EVEX_MOVLP_LD, V,dq, H,dq, WM,q, EV_M128(T1S, W1)),    /* vmovlpd */
        EV_DUP(EVEX_DUP_EVEN, x, FULL_MEM, W0),                              /* vmovsldup */
        EV_DUP(EVEX_DUP_EVEN, x, MOVDDUP, W1),                               /* vmovddup */
    };
    static const X86OpEntry vmovddup_m64 = EV_DUP(EVEX_DUP_EVEN, q, MOVDDUP, W1);
    int mod = get_modrm(s, env) >> 6;

    if (!(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ)) && mod == 3) {
        *entry = vmovhlps;
    } else if ((s->prefix & PREFIX_REPNZ) && mod != 3 && s->evex_ll == 0) {
        *entry = vmovddup_m64;          /* VMOVDDUP xmm1{k1}{z}, m64 */
    } else {
        *entry = *decode_by_prefix(s, opcodes_EVEX_0F12);
    }
}

static void decode_EVEX_0F13(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F13[4] = {
        X86_OP_ENTRY3(EVEX_MOVLP_ST, WM,q, None,None, V,dq, EV_M128(T2, W0)),  /* vmovlps */
        X86_OP_ENTRY3(EVEX_MOVLP_ST, WM,q, None,None, V,dq, EV_M128(T1S, W1)), /* vmovlpd */
        {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F13);
}

static void decode_EVEX_0F16(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vmovlhps =
        X86_OP_ENTRY3(EVEX_MOVLHPS, V,dq, H,dq, U,dq, EV_M128(NONE, W0));
    static const X86OpEntry opcodes_EVEX_0F16[4] = {
        X86_OP_ENTRY3(EVEX_MOVHP_LD, V,dq, H,dq, WM,q, EV_M128(T2, W0)),     /* vmovhps */
        X86_OP_ENTRY3(EVEX_MOVHP_LD, V,dq, H,dq, WM,q, EV_M128(T1S, W1)),    /* vmovhpd */
        EV_DUP(EVEX_DUP_ODD, x, FULL_MEM, W0),                               /* vmovshdup */
        {},
    };

    if (!(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ)) &&
        (get_modrm(s, env) >> 6) == 3) {
        *entry = vmovlhps;
    } else {
        *entry = *decode_by_prefix(s, opcodes_EVEX_0F16);
    }
}

static void decode_EVEX_0F17(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F17[4] = {
        X86_OP_ENTRY3(EVEX_MOVHP_ST, WM,q, None,None, V,dq, EV_M128(T2, W0)),  /* vmovhps */
        X86_OP_ENTRY3(EVEX_MOVHP_ST, WM,q, None,None, V,dq, EV_M128(T1S, W1)), /* vmovhpd */
        {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F17);
}

#define EV_NT(w) cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(FULL_MEM) ev_mask(NONE) ev_w(w) ev_es(W) \
                 ev_e1 ev_nf

static void decode_EVEX_0F2B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F2B[4] = {
        X86_OP_ENTRY3(EVEX_MOV, WM,x, None,None, V,x, EV_NT(W0)),   /* vmovntps */
        X86_OP_ENTRY3(EVEX_MOV, WM,x, None,None, V,x, EV_NT(W1)),   /* vmovntpd */
        {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F2B);
}

/*
 * 0F 7E: 66 VMOVD/VMOVQ r/m, xmm (W selects, ignored outside 64-bit mode); F3 W1 VMOVQ xmm,
 * xmm/m64; F3 W0 the AVX10.2 zero-extending VMOVD xmm1, xmm2/m32 (U410, spec 13.1)
 */
static void decode_EVEX_0F7E(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F7E[2][4] = {
        {
            {},
            X86_OP_ENTRY3(EVEX_MOVD_FROM, E,y, None,None, V,dq, EV_M128(T1S, WSEL) ev_w32), /* vmovd/q */
            X86_OP_ENTRY3(EVEX_VMOVZX_LD, V,dq, None,None, W,d, cpuid(AVX10_2)
                          ev_vl(X86_EVEX_VL128) ev_tt(T1S) ev_mask(NONE) ev_w(W0) ev_es(32)
                          .evex_nofs = 1,),                                        /* vmovd */
            {},
        }, {
            {},
            X86_OP_ENTRY3(EVEX_MOVD_FROM, E,y, None,None, V,dq, EV_M128(T1S, WSEL) ev_w32), /* vmovd/q */
            X86_OP_ENTRY3(EVEX_MOVQ_XX, V,dq, None,None, W,q, EV_M128(T1S, W1)),           /* vmovq */
            {},
        },
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F7E[s->vex_w]);
}
#endif /* __Use_Original_Qemu (U214) */
#if __Use_Original_Qemu != 1 /* ours (U215) */
/*
 * NoVmp (ledger U215): 128/256-bit chunk forms, the element size / form selected by EVEX.W
 * (SDM Vol2C VBROADCAST, VINSERTF32x4.., VEXTRACTF32x4..): the 32X4/64X4 forms are
 * AVX512F, the 64X2/32X8 forms and VBROADCASTF32X2/I32X2 AVX512DQ. Broadcasts are E6
 * (memory fault suppression, Tuple2/4/8), inserts / extracts E6NF.
 */
#define EV_CHUNK(feat, vls, tt) cpuid(feat) ev_vl(vls) ev_tt(tt) ev_mask(MZ) ev_w(WIG) ev_es(W) \
                        p_66
#define EV_BCX(feat, sz, vls, tt) \
    X86_OP_ENTRY3(EVEX_VBCASTX, V,x, None,None, WM,sz, EV_CHUNK(feat, vls, tt))
#define EV_INS(feat, sz, vls, tt) \
    X86_OP_ENTRY4(EVEX_VINSERT, V,x, H,x, W,sz, EV_CHUNK(feat, vls, tt) ev_nf)
#define EV_EXT(feat, sz, vls, tt) \
    X86_OP_ENTRY4(EVEX_VEXTRACT, W,sz, None,None, V,x, EV_CHUNK(feat, vls, tt) ev_nf)
#define EV_VL512     X86_EVEX_VL512

static void decode_EVEX_0F38_19(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes[2] = {
        /* W0: VBROADCASTF32X2 ymm1{k1}{z}, xmm2/m64 (DQ, EVEX.256/512) */
        X86_OP_ENTRY3(EVEX_VBCASTX, V,x, None,None, W,q,
                      EV_CHUNK(AVX512DQ, EV_VL256UP, T2)),
        /* W1: VBROADCASTSD (U153) */
        X86_OP_ENTRY3(VPBROADCASTQ, V,x, None,None, W,q, cpuid(AVX512F)
                      ev_vl(X86_EVEX_VL256 | X86_EVEX_VL512)
                      ev_tt(T1S) ev_mask(MZ) ev_w(W1) ev_es(W) p_66),
    };
    *entry = opcodes[s->vex_w];
}

static void decode_EVEX_0F38_59(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes[2] = {
        /* W0: VBROADCASTI32X2 zmm1{k1}{z}, xmm2/m64 (DQ) */
        X86_OP_ENTRY3(EVEX_VBCASTX, V,x, None,None, W,q, EV_CHUNK(AVX512DQ, EV_VLALL, T2)),
        /* W1: VPBROADCASTQ (U153) */
        X86_OP_ENTRY3(VPBROADCASTQ, V,x, None,None, W,q, cpuid(AVX512F) ev_vl(EV_VLALL)
                      ev_tt(T1S) ev_mask(MZ) ev_w(W1) ev_es(W) p_66),
    };
    *entry = opcodes[s->vex_w];
}

/* 0F38 1A/5A: VBROADCASTF/I32X4 (W0), F/I64X2 (W1, DQ); 1B/5B: F/I32X8 (W0, DQ), F/I64X4 (W1) */
static void decode_EVEX_0F38_1A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes[2] = {
        EV_BCX(AVX512F, dq, EV_VL256UP, T4),
        EV_BCX(AVX512DQ, dq, EV_VL256UP, T2),
    };
    *entry = opcodes[s->vex_w];
}

static void decode_EVEX_0F38_1B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes[2] = {
        EV_BCX(AVX512DQ, qq, EV_VL512, T8),
        EV_BCX(AVX512F, qq, EV_VL512, T4),
    };
    *entry = opcodes[s->vex_w];
}

/* 0F3A 18/38: VINSERTF/I32X4 (W0), 64X2 (W1, DQ); 1A/3A: 32X8 (W0, DQ), 64X4 (W1) */
static void decode_EVEX_0F3A_18(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes[2] = {
        EV_INS(AVX512F, dq, EV_VL256UP, T4),
        EV_INS(AVX512DQ, dq, EV_VL256UP, T2),
    };
    *entry = opcodes[s->vex_w];
}

static void decode_EVEX_0F3A_1A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes[2] = {
        EV_INS(AVX512DQ, qq, EV_VL512, T8),
        EV_INS(AVX512F, qq, EV_VL512, T4),
    };
    *entry = opcodes[s->vex_w];
}

/* 0F3A 19/39: VEXTRACTF/I32X4 (W0), 64X2 (W1, DQ); 1B/3B: 32X8 (W0, DQ), 64X4 (W1) */
static void decode_EVEX_0F3A_19(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes[2] = {
        EV_EXT(AVX512F, dq, EV_VL256UP, T4),
        EV_EXT(AVX512DQ, dq, EV_VL256UP, T2),
    };
    *entry = opcodes[s->vex_w];
}

static void decode_EVEX_0F3A_1B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes[2] = {
        EV_EXT(AVX512DQ, qq, EV_VL512, T8),
        EV_EXT(AVX512F, qq, EV_VL512, T4),
    };
    *entry = opcodes[s->vex_w];
}
#endif /* __Use_Original_Qemu (U215) */
#if __Use_Original_Qemu != 1 /* ours (U264) */
/*
 * NoVmp (ledger U264): 0F 70: F3 VPSHUFHW, F2 VPSHUFLW zmm1{k1}{z}, zmm2/m512, imm8
 * (AVX512BW, Full Mem, word writemask, E4NF); 66 W0 = VPSHUFD (AVX512F, U211).
 */
static void decode_EVEX_0F70(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F70[4] = {
        {},
#if __Use_Original_Qemu != 1 /* ours (U211) */
        X86_OP_ENTRY4(EVEX_VPSHUFD, V,x, None,None, W,x, EV_PRM(EV_VLALL, W0) p_66),
#endif /* __Use_Original_Qemu (U211) */
        X86_OP_ENTRY4(EVEX_PSHUFHW, V,x, W,x, None,None, EV_BW(WIG, 16) ev_nf),
        X86_OP_ENTRY4(EVEX_PSHUFLW, V,x, W,x, None,None, EV_BW(WIG, 16) ev_nf),
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F70);
}
#endif /* __Use_Original_Qemu (U264) */
#if __Use_Original_Qemu != 1 /* ours (U265) */
/*
 * NoVmp (ledger U265): 66 0F 71: /2 VPSRLW, /4 VPSRAW, /6 VPSLLW zmm1(vvvv){k1}{z},
 * zmm2/m512, imm8 (AVX512BW, Full Mem, word writemask, E4NF)
 */
#define EV_SHW(op) X86_OP_ENTRY4(op, H,x, W,x, None,None, EV_BW(WIG, 16) ev_nf p_66)

static void decode_EVEX_0F71(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F71[8] = {
        {},
        {},
        EV_SHW(EVEX_VPSRLI),
        {},
        EV_SHW(EVEX_VPSRAI),
        {},
        EV_SHW(EVEX_VPSLLI),
        {},
    };
    *entry = opcodes_EVEX_0F71[(get_modrm(s, env) >> 3) & 7];
}
#undef EV_SHW
#endif /* __Use_Original_Qemu (U265) */
#if __Use_Original_Qemu != 1 /* ours (U266) */
/*
 * NoVmp (ledger U266): byte/word compares into k1 {k2} (AVX512BW, Full Mem, E4.nb),
 * VPMOVB2M/W2M and VPMOVM2B/W (register only, no writemask, E7NM). EVEX.W selects the
 * element size where B and W share an opcode.
 */
#define EV_BWK(w, es) cpuid(AVX512BW) ev_vl(EV_VLALL) ev_tt(FULL_MEM) ev_mask(KDEST) ev_w(w) \
                      ev_es(es)

/* 0F38 26: 66 VPTESTMB/W, F3 VPTESTNMB/W k1{k2}, zmm2, zmm3/m512 */
static void decode_EVEX_0F38_26(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vptestm[2] = {
        X86_OP_ENTRY3(EVEX_VPTESTM, K,q, H,x, W,x, EV_BWK(W0, 8)),
        X86_OP_ENTRY3(EVEX_VPTESTM, K,q, H,x, W,x, EV_BWK(W1, 16)),
    };
    static const X86OpEntry vptestnm[2] = {
        X86_OP_ENTRY3(EVEX_VPTESTNM, K,q, H,x, W,x, EV_BWK(W0, 8)),
        X86_OP_ENTRY3(EVEX_VPTESTNM, K,q, H,x, W,x, EV_BWK(W1, 16)),
    };
    static const X86OpEntry none = { 0 };

    if (s->prefix & PREFIX_REPZ) {
        *entry = *decode_evex_by_w(s, vptestnm);
    } else if (s->prefix & PREFIX_DATA) {
        *entry = *decode_evex_by_w(s, vptestm);
    } else {
        *entry = none;                      /* NP / F2: #UD */
    }
}

/* 0F38 28: 66 W1 VPMULDQ (U155, moved here), F3 VPMOVM2B (W0) / VPMOVM2W (W1) */
static void decode_EVEX_0F38_28(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vpmulldq = X86_OP_ENTRY3(EVEX_PMULDQ, V,x, H,x, W,x, EV_INT(W1));
    static const X86OpEntry vpmovm2[2] = {
        X86_OP_ENTRY3(EVEX_VPMOVM2, V,x, None,None, KU,q, cpuid(AVX512BW) ev_vl(EV_VLALL)
                      ev_mask(NONE) ev_w(W0) ev_es(8)),
        X86_OP_ENTRY3(EVEX_VPMOVM2, V,x, None,None, KU,q, cpuid(AVX512BW) ev_vl(EV_VLALL)
                      ev_mask(NONE) ev_w(W1) ev_es(16)),
    };

    if (s->prefix & PREFIX_REPZ) {
        *entry = *decode_evex_by_w(s, vpmovm2);
    } else {
        *entry = vpmulldq;                  /* p_66: any other prefix #UD */
    }
}

/* 0F38 29: 66 W1 VPCMPEQQ (U152, moved here), F3 VPMOVB2M (W0) / VPMOVW2M (W1) */
static void decode_EVEX_0F38_29(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vpcmpeqq = X86_OP_ENTRY3(EVEX_VPCMPEQ, K,q, H,x, W,x,
        cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(KDEST) ev_w(W1) ev_es(W) p_66);
    static const X86OpEntry vpmov2m[2] = {
        X86_OP_ENTRY3(EVEX_VPMOV2M, K,q, None,None, U,x, cpuid(AVX512BW) ev_vl(EV_VLALL)
                      ev_mask(NONE) ev_w(W0) ev_es(8)),
        X86_OP_ENTRY3(EVEX_VPMOV2M, K,q, None,None, U,x, cpuid(AVX512BW) ev_vl(EV_VLALL)
                      ev_mask(NONE) ev_w(W1) ev_es(16)),
    };

    if (s->prefix & PREFIX_REPZ) {
        *entry = *decode_evex_by_w(s, vpmov2m);
    } else {
        *entry = vpcmpeqq;
    }
}

/* 0F3A 3E: VPCMPUB (W0) / VPCMPUW (W1); 3F: VPCMPB / VPCMPW k1{k2}, zmm2, zmm3/m512, imm8 */
static void decode_EVEX_0F3A_3E(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vpcmpu[2] = {
        X86_OP_ENTRY4(EVEX_VPCMPU, K,q, H,x, W,x, EV_BWK(W0, 8) p_66),
        X86_OP_ENTRY4(EVEX_VPCMPU, K,q, H,x, W,x, EV_BWK(W1, 16) p_66),
    };
    *entry = *decode_evex_by_w(s, vpcmpu);
}

static void decode_EVEX_0F3A_3F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vpcmp[2] = {
        X86_OP_ENTRY4(EVEX_VPCMP, K,q, H,x, W,x, EV_BWK(W0, 8) p_66),
        X86_OP_ENTRY4(EVEX_VPCMP, K,q, H,x, W,x, EV_BWK(W1, 16) p_66),
    };
    *entry = *decode_evex_by_w(s, vpcmp);
}
#endif /* __Use_Original_Qemu (U266) */
#if __Use_Original_Qemu != 1 /* ours (U268) */
/*
 * NoVmp (ledger U268): 0F38 10/20/30: 66 = VPSRLVW (U265) / VPMOVSXBW / VPMOVZXBW
 * zmm1{k1}{z}, ymm2/m256 (Half Mem, word writemask, E5); F3 W0 = VPMOVUSWB / VPMOVSWB /
 * VPMOVWB ymm1/m256{k1}{z}, zmm2 (Half Mem, byte writemask of the VL/2 destination, E6).
 */
#define EV_PMOVWB(op) X86_OP_ENTRY3(op, W,xh, None,None, V,x, cpuid(AVX512BW) ev_vl(EV_VLALL) \
                          ev_tt(HALF_MEM) ev_mask(MZ) ev_w(W0) ev_es(8))
#define EV_PMOVXBW(op) X86_OP_ENTRY3(op, V,x, None,None, W,xh, cpuid(AVX512BW) \
                          ev_vl(EV_VLALL) ev_tt(HALF_MEM) ev_mask(MZ) ev_w(WIG) ev_es(16))

static void decode_EVEX_0F38_10(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_10[4] = {
        {},
        X86_OP_ENTRY3(EVEX_VPSRLVW, V,x, H,x, W,x, EV_BW(W1, 16)),
        EV_PMOVWB(EVEX_VPMOVUSWB),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_10);
}

static void decode_EVEX_0F38_20(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_20[4] = {
        {},
        EV_PMOVXBW(EVEX_PMOVSXBW),
        EV_PMOVWB(EVEX_VPMOVSWB),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_20);
}

static void decode_EVEX_0F38_30(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_30[4] = {
        {},
        EV_PMOVXBW(EVEX_PMOVZXBW),
        EV_PMOVWB(EVEX_VPMOVWB),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_30);
}
#undef EV_PMOVWB
#undef EV_PMOVXBW
#endif /* __Use_Original_Qemu (U268) */
#if __Use_Original_Qemu != 1 /* ours (U269) */
/*
 * NoVmp (ledger U269): 66.0F38 66 VPBLENDMB (W0) / VPBLENDMW (W1) (Full Mem, E4); 75 / 7D
 * / 8D W1 VPERMI2W / VPERMT2W / VPERMW (Full Mem, word writemask, E4NF); their W0 forms
 * are VPERMI2B / VPERMT2B / VPERMB (AVX512_VBMI, U325: opcodes_evex_0F38_66W0, taken by
 * decode_evex_0F38 before these groups, so their W0 slots stay empty).
 */
static void decode_EVEX_0F38_66(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vpblendm[2] = {
        /* SRC2 moved by gen_EVEX_MOV; masked-off lanes take SRC1: ev_msrc1 (U191) */
        X86_OP_ENTRY3(EVEX_MOV, V,x, H,x, W,x, EV_BW(W0, 8) ev_msrc1 p_66),
        X86_OP_ENTRY3(EVEX_MOV, V,x, H,x, W,x, EV_BW(W1, 16) ev_msrc1 p_66),
    };
    *entry = *decode_evex_by_w(s, vpblendm);
}

static void decode_EVEX_0F38_75(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vpermi2[2] = {
        {},
        X86_OP_ENTRY3(EVEX_VPERMI2W, V,x, H,x, W,x, EV_BW(W1, 16) ev_nf p_66),
    };
    *entry = *decode_evex_by_w(s, vpermi2);
}

static void decode_EVEX_0F38_7D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vpermt2[2] = {
        {},
        X86_OP_ENTRY3(EVEX_VPERMT2W, V,x, H,x, W,x, EV_BW(W1, 16) ev_nf p_66),
    };
    *entry = *decode_evex_by_w(s, vpermt2);
}

static void decode_EVEX_0F38_8D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry vperm[2] = {
        {},
        X86_OP_ENTRY3(EVEX_VPERMW, V,x, H,x, W,x, EV_BW(W1, 16) ev_nf p_66),
    };
    *entry = *decode_evex_by_w(s, vperm);
}
#endif /* __Use_Original_Qemu (U269) */
#if __Use_Original_Qemu != 1 /* ours (U290) */
/*
 * NoVmp (ledger U290): 66 0F38 40: VPMULLD (W0, AVX512F, U155), VPMULLQ (W1, AVX512DQ):
 * low 32/64 bits of the signed product (SDM Vol2B PMULLD/PMULLQ); Full tuple, {k1}{z},
 * {1toN}; E4.
 */
static void decode_EVEX_0F38_40(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_40[2] = {
        X86_OP_ENTRY3(PMULLD, V,x, H,x, W,x, EV_INT(W0)),
        X86_OP_ENTRY3(EVEX_PMULLQ, V,x, H,x, W,x, cpuid(AVX512DQ) ev_vl(EV_VLALL) ev_tt(FULL)
                      ev_mask(MZ) ev_w(W1) ev_es(W) p_66),
    };
    *entry = opcodes_EVEX_0F38_40[s->vex_w];
}
#endif /* __Use_Original_Qemu (U290) */
#if __Use_Original_Qemu != 1 /* ours (U291) */
/*
 * NoVmp (ledger U291): 0F 54/55/56/57 VANDPS/VANDNPS/VORPS/VXORPS (NP W0) and VANDPD/
 * VANDNPD/VORPD/VXORPD (66 W1), AVX512DQ: bitwise on the whole vector (SDM Vol2A ANDPS ..
 * XORPD "EVEX encoded versions"; VANDN: DEST := NOT(SRC1) AND SRC2); the element size
 * (EVEX.W) gives the masking granularity and {1toN}. Full tuple, {k1}{z}; E4, no MXCSR
 * flags. NP W1 and 66 W0 are #UD.
 */
#define EV_LOGP(op, w) X86_OP_ENTRY3(op, V,x, H,x, W,x, cpuid(AVX512DQ) ev_vl(EV_VLALL) \
                          ev_tt(FULL) ev_mask(MZ) ev_w(w) ev_es(W))

static void decode_EVEX_0F54(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F54[4] = {
        EV_LOGP(PAND, W0), EV_LOGP(PAND, W1), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F54);
}

static void decode_EVEX_0F55(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F55[4] = {
        EV_LOGP(PANDN, W0), EV_LOGP(PANDN, W1), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F55);
}

static void decode_EVEX_0F56(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F56[4] = {
        EV_LOGP(POR, W0), EV_LOGP(POR, W1), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F56);
}

static void decode_EVEX_0F57(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F57[4] = {
        EV_LOGP(PXOR, W0), EV_LOGP(PXOR, W1), {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F57);
}
#undef EV_LOGP
#endif /* __Use_Original_Qemu (U291) */
#if __Use_Original_Qemu != 1 /* ours (U292) */
/*
 * NoVmp (ledger U292): 66 0F3A 66 VFPCLASSPS (W0) / VFPCLASSPD (W1) k2 {k1}, x/m/{1toN},
 * imm8 (Full tuple, E4) and 66 0F3A 67 VFPCLASSSS (W0) / VFPCLASSSD (W1) k2 {k1}, xmm/m32
 * (m64), imm8 (LIG, Tuple1 Scalar, E6), AVX512DQ (SDM Vol2C VFPCLASSPD .. VFPCLASSSS).
 * No MXCSR flag; EVEX.vvvv must be 1111b (validate_evex).
 */
/*
 * NoVmp (ledger U292 + U336, one decoder per opcode slot): 0F3A 66 / 67 also hold the
 * AVX512-FP16 NP W0 forms VFPCLASSPH (Full tuple, {1to8/16/32}) / VFPCLASSSH (m16) k1{k2},
 * r/m, imm8 (SDM Vol2C VFPCLASSPH, VFPCLASSSH; gen_FP16_FPCLASS of emit_fp16.c.inc).
 */
#define EV_FPCLASS_SH X86_OP_ENTRY4(FP16_FPCLASS, K,q, None,None, W,w, cpuid(AVX512_FP16)     \
                          ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(KDEST) ev_w(W0) ev_es(16))
static void decode_EVEX_0F3A_66(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_66[4] = {
        X86_OP_ENTRY4(FP16_FPCLASS, K,q, None,None, W,x, cpuid(AVX512_FP16) ev_vl(EV_VLALL)
                      ev_tt(FULL) ev_mask(KDEST) ev_w(W0) ev_es(16)),
        X86_OP_ENTRY3(EVEX_VFPCLASS, K,q, W,x, I,b, cpuid(AVX512DQ) ev_vl(EV_VLALL)
                      ev_tt(FULL) ev_mask(KDEST) ev_w(WSEL) ev_es(W) p_66),
        {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_66);
}

static void decode_EVEX_0F3A_67(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_67[2][4] = {
        {
            EV_FPCLASS_SH,
            X86_OP_ENTRY3(EVEX_VFPCLASS, K,q, W,ss, I,b, cpuid(AVX512DQ) ev_vl(X86_EVEX_LIG)
                          ev_tt(T1S) ev_mask(KDEST) ev_w(W0) ev_es(W) p_66),
            {}, {},
        }, {
            EV_FPCLASS_SH,      /* W1: #UD by its ev_w(W0) */
            X86_OP_ENTRY3(EVEX_VFPCLASS, K,q, W,sd, I,b, cpuid(AVX512DQ) ev_vl(X86_EVEX_LIG)
                          ev_tt(T1S) ev_mask(KDEST) ev_w(W1) ev_es(W) p_66),
            {}, {},
        },
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_67[s->vex_w]);
}
#undef EV_FPCLASS_SH
#endif /* __Use_Original_Qemu (U292) */
#if __Use_Original_Qemu != 1 /* ours (U295) */
/*
 * NoVmp (ledger U295): 66 0F3A 51 VRANGESS (W0) / VRANGESD (W1) xmm1 {k1}{z}, xmm2,
 * xmm3/m32 (m64){sae}, imm8 and (U296) 66 0F3A 57 VREDUCESS / VREDUCESD: LIG, Tuple1
 * Scalar, {k1}{z}, AVX512DQ; E3. DEST[127:esz] := SRC1[127:esz].
 */
#define EV_DQS(op, sz, w) X86_OP_ENTRY4(op, V,x, H,x, W,sz, cpuid(AVX512DQ) ev_vl(X86_EVEX_LIG) \
                          ev_tt(T1S) ev_mask(MZ) ev_rc(SAE) ev_w(w) ev_es(W) ev_fp p_66)

static void decode_EVEX_0F3A_51(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_51[2] = {
        EV_DQS(EVEX_VRANGE, ss, W0), EV_DQS(EVEX_VRANGE, sd, W1),
    };
    *entry = opcodes_EVEX_0F3A_51[s->vex_w];
}
#endif /* __Use_Original_Qemu (U295) */
#if __Use_Original_Qemu != 1 /* ours (U337) */
/*
 * NoVmp (ledger U337): the AVX512-FP16 NP W0 imm8 forms VRNDSCALEPH/SH (0F3A 08/0A),
 * VGETMANTPH/SH (26/27) and VREDUCEPH/SH (56/57), {sae}, share their opcode slots with the
 * 66 AVX-512 forms (U241 VRNDSCALE, U238 VGETMANT, U294/U296 VREDUCE): one decoder per slot
 * (decode_EVEX_0F3A_08/0A/26/27/56/57); packed: Full tuple, {1to8/16/32}; scalar: m16.
 */
#define EV16_P1I(op) X86_OP_ENTRY4(op, V,x, None,None, W,x, cpuid(AVX512_FP16) ev_vl(EV_VLALL) \
                     ev_tt(FULL) ev_mask(MZ) ev_rc(SAE) ev_w(W0) ev_es(16))
#define EV16_SSI(op) X86_OP_ENTRY4(op, V,x, H,x, W,w, cpuid(AVX512_FP16) ev_vl(X86_EVEX_LIG) \
                     ev_tt(T1S) ev_mask(MZ) ev_rc(SAE) ev_w(W0) ev_es(16))
#endif /* __Use_Original_Qemu (U337) */
#if __Use_Original_Qemu != 1 /* ours (U296) */
/* 0F3A 57: 66 W0/W1 VREDUCESS/SD; NP W0 VREDUCESH (AVX512-FP16, U337) */
static void decode_EVEX_0F3A_57(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_57[2][4] = {
        { EV16_SSI(FP16_REDUCE), EV_DQS(EVEX_VREDUCE, ss, W0), {}, {}, },
        { EV16_SSI(FP16_REDUCE), EV_DQS(EVEX_VREDUCE, sd, W1), {}, {}, },
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_57[s->vex_w]);
}
#endif /* __Use_Original_Qemu (U296) */
#if __Use_Original_Qemu != 1 /* ours (U295) */
#undef EV_DQS
#endif /* __Use_Original_Qemu (U295) */
#if __Use_Original_Qemu != 1 /* ours (U324) */
/*
 * NoVmp (ledger U324): AVX512_BITALG 66 0F38 54: VPOPCNTB (W0) / VPOPCNTW (W1) zmm1{k1}{z},
 * zmm2/m (SDM Vol2C VPOPCNT): Full Mem tuple (no {1toN}), byte / word masking, class E4.
 */
static void decode_EVEX_0F38_54(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_54[2] = {
        X86_OP_ENTRY3(EVEX_VPOPCNT, V,x, None,None, W,x, cpuid(AVX512_BITALG) ev_vl(EV_VLALL)
                      ev_tt(FULL_MEM) ev_mask(MZ) ev_w(W0) ev_es(8) p_66),
        X86_OP_ENTRY3(EVEX_VPOPCNT, V,x, None,None, W,x, cpuid(AVX512_BITALG) ev_vl(EV_VLALL)
                      ev_tt(FULL_MEM) ev_mask(MZ) ev_w(W1) ev_es(16) p_66),
    };
    *entry = opcodes_EVEX_0F38_54[s->vex_w ? 1 : 0];
}
#endif /* __Use_Original_Qemu (U324) */
#if __Use_Original_Qemu != 1 /* ours (U231) */
/*
 * NoVmp (ledger U231): EVEX conversions (Emulator/data/evex_forms.tsv rows; SDM Vol2A/2C).
 * EVEX.W is an opcode extension for most of them, so the slots are indexed [prefix][W]
 * (prefix: 0 NP, 1 66, 2 F3, 3 F2). ev_ds: element size of the destination / opmask when it
 * differs from the source (U230). Packed: {k1}{z}, Full or Half tuple with {1toN}
 * (VCVTPH2PS/VCVTPS2PH: Half Mem, no {1toN}), {er} or {sae} as the SDM lists; E2
 * (VCVT[U]DQ2PD: E5, no {er}; VCVTPH2PS / VCVTPS2PH: E11).
 */
#define EV_CVT(op, d, s, w, tt, rc, es, ds) X86_OP_ENTRY3(op, V,d, None,None, W,s,             \
                          cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(tt) ev_mask(MZ) ev_rc(rc) ev_w(w) \
                          ev_es(es) ev_ds(ds) ev_fp)

static const X86OpEntry *evex_by_prefix_w(DisasContext *s, const X86OpEntry e[4][2], bool w)
{
    int p = (s->prefix & PREFIX_REPNZ) ? 3 : (s->prefix & PREFIX_REPZ) ? 2 :
            (s->prefix & PREFIX_DATA) ? 1 : 0;

    return &e[p][w];
}

/* EVEX.W of a scalar form with a GPR operand: ignored outside 64-bit mode */
static bool evex_gpr_w(DisasContext *s)
{
    return CODE64(s) && s->vex_w;
}

/* EVEX.W for the [prefix][W] tables: F3/F2 slots of 0F 78/79/7B are the GPR scalar forms */
static bool evex_cvt_w(DisasContext *s)
{
    return (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) ? evex_gpr_w(s) : s->vex_w;
}
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U233) */
/* NoVmp (ledger U233): the AVX512DQ quadword conversions (same rules, CPUID AVX512DQ) */
#define EV_CVTQ(op, d, s, w, tt, rc, es, ds) X86_OP_ENTRY3(op, V,d, None,None, W,s,            \
                          cpuid(AVX512DQ) ev_vl(EV_VLALL) ev_tt(tt) ev_mask(MZ) ev_rc(rc)        \
                          ev_w(w) ev_es(es) ev_ds(ds) ev_fp)
#endif /* __Use_Original_Qemu (U233) */
#if __Use_Original_Qemu != 1 /* ours (U232) */
/*
 * NoVmp (ledger U232): scalar conversions, LIG, no masking except VCVTSS2SD/VCVTSD2SS.
 * GPR forms: EVEX.W1 = r/m64 / r64 in 64-bit mode; outside 64-bit mode EVEX.W is ignored
 * (W0 behaviour, also for VCVT[U]SI2SD whose W1 form alone has {er}). T1S (int -> FP),
 * T1F (FP -> int); E3NF / E10NF.
 */
#define EV_SI2F(op, w, rc, es) X86_OP_ENTRY3(op, V,x, H,x, E,y, cpuid(AVX512F)                \
                          ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(NONE) ev_rc(rc) ev_w(w)         \
                          ev_es(es) ev_w32 ev_fp)
#define EV_F2SI(op, sz, rc, es) X86_OP_ENTRY3(op, G,y, None,None, W,sz, cpuid(AVX512F)        \
                          ev_vl(X86_EVEX_LIG) ev_tt(T1F) ev_mask(NONE) ev_rc(rc) ev_w(WSEL)      \
                          ev_es(es) ev_w32 ev_fp)

/* 0F 2A: VCVTSI2SS (F3, {er}), VCVTSI2SD (F2; W0 r/m32 exact, no {er}; W1 r/m64 {er}) */
static void decode_EVEX_0F2A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F2A[4][2] = {
        [2][0] = EV_SI2F(EVEX_VCVTSI2SS, W0, ER, 32),
        [2][1] = EV_SI2F(EVEX_VCVTSI2SS, W1, ER, 32),
        [3][0] = EV_SI2F(EVEX_VCVTSI2SD, W0, NONE, 64),
        [3][1] = EV_SI2F(EVEX_VCVTSI2SD, W1, ER, 64),
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0F2A, evex_gpr_w(s));
}

/* 0F 2C: VCVTTSS2SI (F3), VCVTTSD2SI (F2) r32/r64, {sae} */
static void decode_EVEX_0F2C(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F2C[4][2] = {
        [2][0] = EV_F2SI(EVEX_VCVTTSS2SI, ss, SAE, 32),
        [2][1] = EV_F2SI(EVEX_VCVTTSS2SI, ss, SAE, 32),
        [3][0] = EV_F2SI(EVEX_VCVTTSD2SI, sd, SAE, 64),
        [3][1] = EV_F2SI(EVEX_VCVTTSD2SI, sd, SAE, 64),
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0F2C, evex_gpr_w(s));
}

/* 0F 2D: VCVTSS2SI (F3), VCVTSD2SI (F2) r32/r64, {er} */
static void decode_EVEX_0F2D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F2D[4][2] = {
        [2][0] = EV_F2SI(EVEX_VCVTSS2SI, ss, ER, 32),
        [2][1] = EV_F2SI(EVEX_VCVTSS2SI, ss, ER, 32),
        [3][0] = EV_F2SI(EVEX_VCVTSD2SI, sd, ER, 64),
        [3][1] = EV_F2SI(EVEX_VCVTSD2SI, sd, ER, 64),
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0F2D, evex_gpr_w(s));
}
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U231) */

/*
 * 0F 5A: VCVTPS2PD (NP W0, Half, {sae}), VCVTPD2PS (66 W1, {er}); VCVTSS2SD (F3 W0,
 * {sae}) and VCVTSD2SS (F2 W1, {er}): U232
 */
static void decode_EVEX_0F5A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F5A[4] = {
        [0] = EV_CVT(EVEX_VCVTPS2PD, x, xh, W0, HALF, SAE, W, 64),
        [1] = EV_CVT(EVEX_VCVTPD2PS, xh, x, W1, FULL, ER, W, 32),
#if __Use_Original_Qemu != 1 /* ours (U232) */
        /* xmm1{k1}{z}, xmm2, xmm3/m32 (m64): T1S, E3 */
        [2] = X86_OP_ENTRY3(EVEX_VCVTSS2SD, V,x, H,x, W,ss, cpuid(AVX512F) ev_vl(X86_EVEX_LIG)
                            ev_tt(T1S) ev_mask(MZ) ev_rc(SAE) ev_w(W0) ev_es(32) ev_ds(64) ev_fp),
        [3] = X86_OP_ENTRY3(EVEX_VCVTSD2SS, V,x, H,x, W,sd, cpuid(AVX512F) ev_vl(X86_EVEX_LIG)
                            ev_tt(T1S) ev_mask(MZ) ev_rc(ER) ev_w(W1) ev_es(64) ev_ds(32) ev_fp),
#endif /* __Use_Original_Qemu (U232) */
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F5A);
}

/*
 * 0F 5B: VCVTDQ2PS (NP W0, {er}), VCVTPS2DQ (66 W0, {er}), VCVTTPS2DQ (F3 W0, {sae});
 * VCVTQQ2PS (NP W1, DQ): U233
 */
static void decode_EVEX_0F5B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F5B[4][2] = {
        [0][0] = EV_CVT(EVEX_VCVTDQ2PS, x, x, W0, FULL, ER, W, 32),
        [1][0] = EV_CVT(EVEX_VCVTPS2DQ, x, x, W0, FULL, ER, W, 32),
        [2][0] = EV_CVT(EVEX_VCVTTPS2DQ, x, x, W0, FULL, SAE, W, 32),
#if __Use_Original_Qemu != 1 /* ours (U233) */
        [0][1] = EV_CVTQ(EVEX_VCVTQQ2PS, xh, x, W1, FULL, ER, W, 32),
#endif /* __Use_Original_Qemu (U233) */
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0F5B, s->vex_w);
}

/*
 * 0F 78: VCVTTPS2UDQ (NP W0, {sae}), VCVTTPD2UDQ (NP W1); VCVTTSS2USI / VCVTTSD2USI
 * (F3/F2): U232; VCVTTPS2UQQ / VCVTTPD2UQQ (66, DQ): U233
 */
static void decode_EVEX_0F78(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F78[4][2] = {
        [0][0] = EV_CVT(EVEX_VCVTTPS2UDQ, x, x, W0, FULL, SAE, W, 32),
        [0][1] = EV_CVT(EVEX_VCVTTPD2UDQ, xh, x, W1, FULL, SAE, W, 32),
#if __Use_Original_Qemu != 1 /* ours (U232) */
        [2][0] = EV_F2SI(EVEX_VCVTTSS2USI, ss, SAE, 32),
        [2][1] = EV_F2SI(EVEX_VCVTTSS2USI, ss, SAE, 32),
        [3][0] = EV_F2SI(EVEX_VCVTTSD2USI, sd, SAE, 64),
        [3][1] = EV_F2SI(EVEX_VCVTTSD2USI, sd, SAE, 64),
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U233) */
        [1][0] = EV_CVTQ(EVEX_VCVTTPS2UQQ, x, xh, W0, HALF, SAE, W, 64),
        [1][1] = EV_CVTQ(EVEX_VCVTTPD2UQQ, x, x, W1, FULL, SAE, W, 64),
#endif /* __Use_Original_Qemu (U233) */
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0F78, evex_cvt_w(s));
}

/*
 * 0F 79: VCVTPS2UDQ (NP W0, {er}), VCVTPD2UDQ (NP W1); VCVTSS2USI / VCVTSD2USI (F3/F2):
 * U232; VCVTPS2UQQ / VCVTPD2UQQ (66, DQ): U233
 */
static void decode_EVEX_0F79(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F79[4][2] = {
        [0][0] = EV_CVT(EVEX_VCVTPS2UDQ, x, x, W0, FULL, ER, W, 32),
        [0][1] = EV_CVT(EVEX_VCVTPD2UDQ, xh, x, W1, FULL, ER, W, 32),
#if __Use_Original_Qemu != 1 /* ours (U232) */
        [2][0] = EV_F2SI(EVEX_VCVTSS2USI, ss, ER, 32),
        [2][1] = EV_F2SI(EVEX_VCVTSS2USI, ss, ER, 32),
        [3][0] = EV_F2SI(EVEX_VCVTSD2USI, sd, ER, 64),
        [3][1] = EV_F2SI(EVEX_VCVTSD2USI, sd, ER, 64),
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U233) */
        [1][0] = EV_CVTQ(EVEX_VCVTPS2UQQ, x, xh, W0, HALF, ER, W, 64),
        [1][1] = EV_CVTQ(EVEX_VCVTPD2UQQ, x, x, W1, FULL, ER, W, 64),
#endif /* __Use_Original_Qemu (U233) */
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0F79, evex_cvt_w(s));
}

/*
 * 0F 7A: VCVTUDQ2PD (F3 W0, Half, no {er}), VCVTUDQ2PS (F2 W0, {er}); DQ (U233):
 * VCVTTPS2QQ / VCVTTPD2QQ (66, {sae}), VCVTUQQ2PD (F3 W1), VCVTUQQ2PS (F2 W1)
 */
static void decode_EVEX_0F7A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F7A[4][2] = {
        [2][0] = EV_CVT(EVEX_VCVTUDQ2PD, x, xh, W0, HALF, NONE, W, 64),
        [3][0] = EV_CVT(EVEX_VCVTUDQ2PS, x, x, W0, FULL, ER, W, 32),
#if __Use_Original_Qemu != 1 /* ours (U233) */
        [1][0] = EV_CVTQ(EVEX_VCVTTPS2QQ, x, xh, W0, HALF, SAE, W, 64),
        [1][1] = EV_CVTQ(EVEX_VCVTTPD2QQ, x, x, W1, FULL, SAE, W, 64),
        [2][1] = EV_CVTQ(EVEX_VCVTUQQ2PD, x, x, W1, FULL, ER, W, 64),
        [3][1] = EV_CVTQ(EVEX_VCVTUQQ2PS, xh, x, W1, FULL, ER, W, 32),
#endif /* __Use_Original_Qemu (U233) */
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0F7A, s->vex_w);
}

/*
 * 0F 7B: VCVTUSI2SS (F3) / VCVTUSI2SD (F2): U232; VCVTPS2QQ / VCVTPD2QQ (66, {er}, DQ): U233
 */
static void decode_EVEX_0F7B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F7B[4][2] = {
#if __Use_Original_Qemu != 1 /* ours (U232) */
        [2][0] = EV_SI2F(EVEX_VCVTUSI2SS, W0, ER, 32),
        [2][1] = EV_SI2F(EVEX_VCVTUSI2SS, W1, ER, 32),
        [3][0] = EV_SI2F(EVEX_VCVTUSI2SD, W0, NONE, 64),
        [3][1] = EV_SI2F(EVEX_VCVTUSI2SD, W1, ER, 64),
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U233) */
        [1][0] = EV_CVTQ(EVEX_VCVTPS2QQ, x, xh, W0, HALF, ER, W, 64),
        [1][1] = EV_CVTQ(EVEX_VCVTPD2QQ, x, x, W1, FULL, ER, W, 64),
#endif /* __Use_Original_Qemu (U233) */
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0F7B, evex_cvt_w(s));
}

/*
 * 0F E6: VCVTTPD2DQ (66 W1, {sae}), VCVTDQ2PD (F3 W0, Half), VCVTPD2DQ (F2 W1, {er});
 * VCVTQQ2PD (F3 W1, {er}, DQ): U233
 */
static void decode_EVEX_0FE6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0FE6[4][2] = {
        [1][1] = EV_CVT(EVEX_VCVTTPD2DQ, xh, x, W1, FULL, SAE, W, 32),
        [2][0] = EV_CVT(EVEX_VCVTDQ2PD, x, xh, W0, HALF, NONE, W, 64),
        [3][1] = EV_CVT(EVEX_VCVTPD2DQ, xh, x, W1, FULL, ER, W, 32),
#if __Use_Original_Qemu != 1 /* ours (U233) */
        [2][1] = EV_CVTQ(EVEX_VCVTQQ2PD, x, x, W1, FULL, ER, W, 64),
#endif /* __Use_Original_Qemu (U233) */
    };
    *entry = *evex_by_prefix_w(s, opcodes_EVEX_0FE6, s->vex_w);
}

/* 0F38 13: VCVTPH2PS (66 W0): Half Mem, no {1toN}, {sae}; E11 */
static void decode_EVEX_0F38_13(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_13[4] = {
        [1] = EV_CVT(EVEX_VCVTPH2PS, x, xh, W0, HALF_MEM, SAE, 16, 32),
    };
#if __Use_Original_Qemu != 1 /* ours (U212) */
    /* F3 0F38 13: VPMOVUSDW (U212, decode_EVEX_0F38_PMOV) shares the opcode byte */
    if (!(s->prefix & PREFIX_DATA)) {
        decode_EVEX_0F38_PMOV(s, env, entry, b);
        return;
    }
#endif /* __Use_Original_Qemu (U212) */
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_13);
}

/* 0F3A 1D: VCVTPS2PH xmm/m{k1}{z}, xmm2, imm8 (66 W0): Half Mem, {sae}; E11 */
static void decode_EVEX_0F3A_1D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_1D[4] = {
        [1] = X86_OP_ENTRY3(EVEX_VCVTPS2PH, W,xh, V,x, I,b, cpuid(AVX512F) ev_vl(EV_VLALL)
                            ev_tt(HALF_MEM) ev_mask(MZ) ev_rc(SAE) ev_w(W0) ev_es(32) ev_ds(16)
                            ev_fp),
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_1D);
}
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U236) */
/*
 * NoVmp (ledger U236-U241): VRCP14/VRSQRT14 (E4/E5, no MXCSR flags: not evex_fp), VGETEXP,
 * VGETMANT, VRNDSCALE, VSCALEF (evex_fp), VFIXUPIMM (flags without #XM: not evex_fp). 66 only.
 * Packed: Full tuple, {k1}{z}, {1toN}, EVEX.W = element size; scalar: LIG, Tuple1 Scalar,
 * {k1}{z}, xmm1, xmm2 (vvvv), xmm3/m32 (W0) or m64 (W1).
 */
#define EV_FPX(rc, fp) cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_rc(rc) \
                       ev_w(WSEL) ev_es(W) fp p_66
#define EV_FPS(w, rc, fp) cpuid(AVX512F) ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(MZ) ev_rc(rc) \
                       ev_w(w) ev_es(W) fp p_66
#define EV_NOFP

/* [W] = W0 (ss) / W1 (sd) scalar entry of a WSEL opcode */
#define EV_SCALAR_W(name, entry0, entry1)                                                     \
static void decode_EVEX_##name(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b) \
{                                                                                           \
    static const X86OpEntry opcodes[2] = { entry0, entry1 };                               \
    *entry = opcodes[s->vex_w];                                                             \
}

EV_SCALAR_W(0F38_4D,
            X86_OP_ENTRY3(EVEX_VRCP14, V,x, H,x, W,ss, EV_FPS(W0, NONE, EV_NOFP)),
            X86_OP_ENTRY3(EVEX_VRCP14, V,x, H,x, W,sd, EV_FPS(W1, NONE, EV_NOFP)))
EV_SCALAR_W(0F38_4F,
            X86_OP_ENTRY3(EVEX_VRSQRT14, V,x, H,x, W,ss, EV_FPS(W0, NONE, EV_NOFP)),
            X86_OP_ENTRY3(EVEX_VRSQRT14, V,x, H,x, W,sd, EV_FPS(W1, NONE, EV_NOFP)))
#endif /* __Use_Original_Qemu (U236) */
#if __Use_Original_Qemu != 1 /* ours (U237) */
EV_SCALAR_W(0F38_43,
            X86_OP_ENTRY3(EVEX_VGETEXP, V,x, H,x, W,ss, EV_FPS(W0, SAE, ev_fp)),
            X86_OP_ENTRY3(EVEX_VGETEXP, V,x, H,x, W,sd, EV_FPS(W1, SAE, ev_fp)))
#endif /* __Use_Original_Qemu (U237) */
#if __Use_Original_Qemu != 1 /* ours (U238) */
/* 0F3A 27: 66 W0/W1 VGETMANTSS/SD; NP W0 VGETMANTSH (AVX512-FP16, U337) */
static void decode_EVEX_0F3A_27(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_27[2][4] = {
        { EV16_SSI(FP16_GETMANT),
          X86_OP_ENTRY4(EVEX_VGETMANT, V,x, H,x, W,ss, EV_FPS(W0, SAE, ev_fp)), {}, {}, },
        { EV16_SSI(FP16_GETMANT),
          X86_OP_ENTRY4(EVEX_VGETMANT, V,x, H,x, W,sd, EV_FPS(W1, SAE, ev_fp)), {}, {}, },
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_27[s->vex_w]);
}
#endif /* __Use_Original_Qemu (U238) */
#if __Use_Original_Qemu != 1 /* ours (U239) */
EV_SCALAR_W(0F38_2D,
            X86_OP_ENTRY3(EVEX_VSCALEF, V,x, H,x, W,ss, EV_FPS(W0, ER, ev_fp)),
            X86_OP_ENTRY3(EVEX_VSCALEF, V,x, H,x, W,sd, EV_FPS(W1, ER, ev_fp)))
#endif /* __Use_Original_Qemu (U239) */
#if __Use_Original_Qemu != 1 /* ours (U240) */
EV_SCALAR_W(0F3A_55,
            X86_OP_ENTRY4(EVEX_VFIXUPIMM, V,x, H,x, W,ss, EV_FPS(W0, SAE, EV_NOFP)),
            X86_OP_ENTRY4(EVEX_VFIXUPIMM, V,x, H,x, W,sd, EV_FPS(W1, SAE, EV_NOFP)))
#endif /* __Use_Original_Qemu (U240) */
#if __Use_Original_Qemu != 1 /* ours (U337) */
/* 0F3A 08: 66 W0 VRNDSCALEPS (U241); NP W0 VRNDSCALEPH (AVX512-FP16) */
static void decode_EVEX_0F3A_08(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_08[4] = {
        EV16_P1I(FP16_RNDSCALE),
        X86_OP_ENTRY3(EVEX_VRNDSCALE, V,x, W,x, I,b, cpuid(AVX512F) ev_vl(EV_VLALL)
                      ev_tt(FULL) ev_mask(MZ) ev_rc(SAE) ev_w(W0) ev_es(W) ev_fp p_66),
        {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_08);
}

/* 0F3A 0A: 66 W0 VRNDSCALESS (U241); NP W0 VRNDSCALESH (AVX512-FP16) */
static void decode_EVEX_0F3A_0A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_0A[4] = {
        EV16_SSI(FP16_RNDSCALE),
        X86_OP_ENTRY4(EVEX_VRNDSCALE, V,x, H,x, W,ss, EV_FPS(W0, SAE, ev_fp)),
        {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_0A);
}

/* 0F3A 26: 66 VGETMANTPS/PD (U238); NP W0 VGETMANTPH (AVX512-FP16) */
static void decode_EVEX_0F3A_26(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_26[4] = {
        EV16_P1I(FP16_GETMANT),
        X86_OP_ENTRY3(EVEX_VGETMANT, V,x, W,x, I,b, EV_FPX(SAE, ev_fp)),
        {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_26);
}

/* 0F3A 56: 66 VREDUCEPS/PD (U294); NP W0 VREDUCEPH (AVX512-FP16) */
static void decode_EVEX_0F3A_56(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_56[4] = {
        EV16_P1I(FP16_REDUCE),
        X86_OP_ENTRY3(EVEX_VREDUCE, V,x, W,x, I,b, cpuid(AVX512DQ) ev_vl(EV_VLALL)
                      ev_tt(FULL) ev_mask(MZ) ev_rc(SAE) ev_w(WSEL) ev_es(W) ev_fp p_66),
        {}, {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_56);
}
#undef EV16_P1I
#undef EV16_SSI
#endif /* __Use_Original_Qemu (U337) */
#if __Use_Original_Qemu != 1 /* ours (U234) */
/* NoVmp (ledger U234): shifts by xmm3/m128 (Mem128 tuple, E4NF.nb: no fault suppression) */
#define EV_SHX(w)    cpuid(AVX512F) ev_vl(EV_VLALL) ev_tt(MEM128) ev_mask(MZ) ev_w(w) ev_es(W) \
                     .evex_nofs = 1, p_66
#endif /* __Use_Original_Qemu (U234) */
#if __Use_Original_Qemu != 1 /* ours (U402) */
/*
 * NoVmp (ledger U402): AVX10.2 FP16 -> FP8 conversions (AVX10.2 spec 361050-007 9.1, 9.3),
 * "AVX10.2 OR AVX10_V1_AUX", EVEX.W0, Full tuple (m16bcst), {k1}{z}, class E4NF (no memory
 * fault suppression). One destination byte per opmask bit: gen_avx10b_cvt_fp8 masks and
 * writes the whole destination register itself. NP: VCVTBIASPH2*F8* (vvvv = bias bytes),
 * F3: VCVTPH2*F8* (destination VL/2), F2: VCVT2PH2*F8* (two sources); 0F38 74 = BF8,
 * MAP5 74 = BF8S, MAP5 18 = HF8, MAP5 1B = HF8S.
 */
#define EV_A10F8     cpuid(AVX10_V1_AUX) ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W0) \
                     ev_es(16) .evex_nofs = 1,
static const X86OpEntry opcodes_avx10b_fp8[4] = {
    X86_OP_ENTRY3(EVEX_VCVTBIASPH2F8, V,xh, H,x, W,x, EV_A10F8),
    {},
    X86_OP_ENTRY3(EVEX_VCVTPH2F8, V,xh, None,None, W,x, EV_A10F8),
    X86_OP_ENTRY3(EVEX_VCVT2PH2F8, V,x, H,x, W,x, EV_A10F8),
};
#undef EV_A10F8

/* 0F38 74, MAP5 74 / 18 / 1B: by prefix (decode->b and the map select BF8/HF8 and S) */
static void decode_EVEX_A10_FP8(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *entry = *decode_by_prefix(s, opcodes_avx10b_fp8);
}
#endif /* __Use_Original_Qemu (U402) */
#if __Use_Original_Qemu != 1 /* ours (U403) */
/*
 * NoVmp (ledger U403): MAP5 1E: F2 = VCVTHF82PH (AVX10.2 spec 9.4), EVEX.W0, Half Mem
 * tuple (no broadcast), {k1}{z}, class E4 (fault suppression): one FP16 result per opmask bit.
 */
static void decode_EVEX_MAP5_1E(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_MAP5_1E[4] = {
        {}, {}, {},
        X86_OP_ENTRY3(EVEX_VCVTHF82PH, V,x, None,None, W,xh, cpuid(AVX10_V1_AUX)
                      ev_vl(EV_VLALL) ev_tt(HALF_MEM) ev_mask(MZ) ev_w(W0) ev_es(16)),
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_MAP5_1E);
}
#endif /* __Use_Original_Qemu (U403) */
#if __Use_Original_Qemu != 1 /* ours (U555) */
/*
 * NoVmp (ledger U555): AVX512_VNNI EVEX forms (SDM Vol2C VPDPBUSD, VPDPBUSDS, VPDPWSSD,
 * VPDPWSSDS): EVEX.66.0F38.W0 50 / 51 / 52 / 53 zmm1{k1}{z}, zmm2, zmm3/m/m32bcst, "AVX512_VNNI
 * OR AVX10.1" (+ AVX512VL below 512 bits), Full tuple, class E4; DEST is the accumulator
 * (ev_dsrc: gen_evex_insn reads it through the result scratch register, U190). 50/51/52 are
 * the 66 entries of the slot decoders below (U405, U407), 53 is a table entry.
 */
#define EV_VNNI512   cpuid(AVX512_VNNI) ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W0) \
                     ev_es(32) ev_dsrc
#endif /* __Use_Original_Qemu (U555) */
#if __Use_Original_Qemu != 1 /* ours (U557) */
/*
 * NoVmp (ledger U557): VDPBF16PS (SDM Vol2C), EVEX.F3.0F38.W0 52 zmm1{k1}{z}, zmm2,
 * zmm3/m/m32bcst, "(AVX512_BF16 AND AVX512F) OR AVX10.1", Full tuple, class E4; DEST is the
 * accumulator (ev_dsrc); MXCSR neither consulted nor updated (not an ev_fp form).
 */
#define EV_DPBF16PS  X86_OP_ENTRY3(EVEX_VDPBF16PS, V,x, H,x, W,x, cpuid(AVX512_BF16)      \
                     ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W0) ev_es(32) ev_dsrc)
#endif /* __Use_Original_Qemu (U557) */
#if __Use_Original_Qemu != 1 /* ours (U405) */
/*
 * NoVmp (ledger U405, U406): AVX10.2 VNNI (spec 10.3, 10.4), "AVX10.2 OR AVX10_V1_AUX",
 * EVEX.W0, Full tuple (m32bcst), {k1}{z}, class E4. 0F38 50/51: NP VPDPBUUD(S), F3
 * VPDPBSUD(S), F2 VPDPBSSD(S) (66 = VPDPBUSD(S), AVX512_VNNI: U555); 0F38 D2/D3: NP
 * VPDPWUUD(S), 66 VPDPWUSD(S), F3 VPDPWSUD(S).
 */
#define EV_A10VNNI   cpuid(AVX10_V1_AUX) ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W0) \
                     ev_es(32)
static void decode_EVEX_0F38_50(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_50[4] = {
        X86_OP_ENTRY3(EVEX_VPDPBUUD, V,x, H,x, W,x, EV_A10VNNI),
        X86_OP_ENTRY3(EVEX_VPDPBUSD, V,x, H,x, W,x, EV_VNNI512),    /* 66 (U555) */
        X86_OP_ENTRY3(EVEX_VPDPBSUD, V,x, H,x, W,x, EV_A10VNNI),
        X86_OP_ENTRY3(EVEX_VPDPBSSD, V,x, H,x, W,x, EV_A10VNNI),
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_50);
}

static void decode_EVEX_0F38_51(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_51[4] = {
        X86_OP_ENTRY3(EVEX_VPDPBUUDS, V,x, H,x, W,x, EV_A10VNNI),
        X86_OP_ENTRY3(EVEX_VPDPBUSDS, V,x, H,x, W,x, EV_VNNI512),   /* 66 (U555) */
        X86_OP_ENTRY3(EVEX_VPDPBSUDS, V,x, H,x, W,x, EV_A10VNNI),
        X86_OP_ENTRY3(EVEX_VPDPBSSDS, V,x, H,x, W,x, EV_A10VNNI),
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_51);
}
#endif /* __Use_Original_Qemu (U405) */
#if __Use_Original_Qemu != 1 /* ours (U406) */
static void decode_EVEX_0F38_D2(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_D2[4] = {
        X86_OP_ENTRY3(EVEX_VPDPWUUD, V,x, H,x, W,x, EV_A10VNNI),
        X86_OP_ENTRY3(EVEX_VPDPWUSD, V,x, H,x, W,x, EV_A10VNNI),
        X86_OP_ENTRY3(EVEX_VPDPWSUD, V,x, H,x, W,x, EV_A10VNNI),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_D2);
}

static void decode_EVEX_0F38_D3(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_D3[4] = {
        X86_OP_ENTRY3(EVEX_VPDPWUUDS, V,x, H,x, W,x, EV_A10VNNI),
        X86_OP_ENTRY3(EVEX_VPDPWUSDS, V,x, H,x, W,x, EV_A10VNNI),
        X86_OP_ENTRY3(EVEX_VPDPWSUDS, V,x, H,x, W,x, EV_A10VNNI),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_D3);
}
#endif /* __Use_Original_Qemu (U406) */
#if __Use_Original_Qemu != 1 /* ours (U405) */
#undef EV_A10VNNI
#endif /* __Use_Original_Qemu (U405) */
#if __Use_Original_Qemu != 1 /* ours (U407) */
/*
 * NoVmp (ledger U407): 0F38 52: NP = VDPPHPS (spec 10.1), AVX10.2, EVEX.W0, Full tuple
 * (m32bcst), {k1}{z}, class E4 (66 = VPDPWSSD, AVX512_VNNI: U555; F3 = VDPBF16PS,
 * AVX512_BF16: U557; F2 = VP4DPWSSD, AVX512_4VNNIW, not implemented: #UD).
 */
static void decode_EVEX_0F38_52(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_52[4] = {
        X86_OP_ENTRY3(EVEX_VDPPHPS, V,x, H,x, W,x, cpuid(AVX10_2) ev_vl(EV_VLALL) ev_tt(FULL)
                      ev_mask(MZ) ev_w(W0) ev_es(32)),
        X86_OP_ENTRY3(EVEX_VPDPWSSD, V,x, H,x, W,x, EV_VNNI512),    /* 66 (U555) */
        EV_DPBF16PS,                                                /* F3 (U557) */
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_52);
}
#endif /* __Use_Original_Qemu (U407) */
#if __Use_Original_Qemu != 1 /* ours (U557) */
#undef EV_DPBF16PS
#endif /* __Use_Original_Qemu (U557) */
#if __Use_Original_Qemu != 1 /* ours (U408) */
/*
 * NoVmp (ledger U408): 0F3A 42: F3 = VMPSADBW (spec 10.2), AVX10.2, EVEX.W0, Full Mem
 * tuple, {k1}{z} per word, imm8, class E4NF; 66 W0 = VDBPSADBW (AVX512BW, U263): one decoder
 * for the slot.
 */
static void decode_EVEX_0F3A_42(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F3A_42[4] = {
        {},
        X86_OP_ENTRY4(EVEX_VDBPSADBW, V,x, H,x, W,x, EV_BW(W0, 16) ev_nf p_66),
        X86_OP_ENTRY4(EVEX_VMPSADBW, V,x, H,x, W,x, cpuid(AVX10_2) ev_vl(EV_VLALL)
                      ev_tt(FULL_MEM) ev_mask(MZ) ev_w(W0) ev_es(16) .evex_nofs = 1,),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F3A_42);
}
#endif /* __Use_Original_Qemu (U408) */
#if __Use_Original_Qemu != 1 /* ours (U409) */
/*
 * NoVmp (ledger U409): MAP5 6F, memory source only: F2 W0/W1 = VMOVRSB/VMOVRSW, F3 W0/W1 =
 * VMOVRSD/VMOVRSQ (ISE 319433-062 MOVRS), "AVX10 and MOVRS", 64-bit mode only (V/N.E.),
 * Full Mem tuple, {k1}{z} per element, class E4. A plain (read-shared hint) vector load.
 */
#define EV_A10MOVRS(es) cpuid(AVX10_MOVRS) ev_vl(EV_VLALL) ev_tt(FULL_MEM) ev_mask(MZ) \
                        ev_es(es) o64
static void decode_EVEX_MAP5_6F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_MAP5_6F[2][4] = {
        {
            {}, {},
            X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, WM,x, EV_A10MOVRS(W) ev_w(W0)), /* d */
            X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, WM,x, EV_A10MOVRS(8) ev_w(W0)), /* b */
        },
        {
            {}, {},
            X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, WM,x, EV_A10MOVRS(W) ev_w(W1)), /* q */
            X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, WM,x, EV_A10MOVRS(16) ev_w(W1)), /* w */
        },
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_MAP5_6F[s->vex_w]);
}
#undef EV_A10MOVRS
#endif /* __Use_Original_Qemu (U409) */
#if __Use_Original_Qemu != 1 /* ours (U410) */
/*
 * NoVmp (ledger U410): zero-extending VMOVD (spec 13.1), AVX10.2, EVEX.128.W0 only, Tuple1
 * Scalar, no masking, no EVEX.b, vvvv = 1111b, class E9NF: F3 0F 7E VMOVD xmm1, xmm2/m32 and
 * 66 0F D6 VMOVD xmm1/m32, xmm2 (the W1 forms are VMOVQ, AVX512F; 66 0F 7E is VMOVD r32/m32).
 */
#define EV_A10MOVZ(es) cpuid(AVX10_2) ev_vl(X86_EVEX_VL128) ev_tt(T1S) ev_mask(NONE) ev_w(W0) \
                       ev_es(es) .evex_nofs = 1,
/* F3 0F 7E VMOVD: in U214's decode_EVEX_0F7E. 0F D6: 66 W0 VMOVD (here), 66 W1 VMOVQ (U214) */
static void decode_EVEX_0FD6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0FD6[2][4] = {
        {
            {},
            X86_OP_ENTRY3(EVEX_VMOVZX_ST, W,d, None,None, V,dq, EV_A10MOVZ(32)),
            {}, {},
        }, {
            {},
            X86_OP_ENTRY3(EVEX_MOVQ_XX, W,q, None,None, V,dq, EV_M128(T1S, W1) p_66),
            {}, {},
        },
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0FD6[s->vex_w]);
}
#endif /* __Use_Original_Qemu (U410) */
#if __Use_Original_Qemu != 1 /* ours (U411) */
/*
 * NoVmp (ledger U411): zero-extending VMOVW (spec 13.2), AVX10.2, EVEX.128.F3.MAP5.W0
 * 6E / 7E: decoded with the AVX512-FP16 66 VMOVW forms of the same slots
 * (decode_FP16_M5_6E/7E, decode_fp16.c.inc).
 */
#endif /* __Use_Original_Qemu (U411) */
#if __Use_Original_Qemu != 1 /* ours (U410) */
#undef EV_A10MOVZ
#endif /* __Use_Original_Qemu (U410) */
#if __Use_Original_Qemu != 1 /* ours (U412) */
/*
 * NoVmp (ledger U412): 0F38 DA: F3 = VSM4KEY4, F2 = VSM4RNDS4, EVEX forms (ISE 319433-062
 * "AVX10 AND SM4"): EVEX.W0, 128/256/512 bits (independent 128-bit lanes), Full Mem tuple,
 * no masking, no EVEX.b, class E6. The ops_sse.h SM4 helpers of the VEX forms (U84).
 */
static void decode_EVEX_0F38_DA(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_DA[4] = {
        {}, {},
        X86_OP_ENTRY3(EVEX_VSM4KEY4, V,x, H,x, W,x, cpuid(AVX10_SM4) ev_vl(EV_VLALL)
                      ev_tt(FULL_MEM) ev_mask(NONE) ev_w(W0) ev_es(32)),
        X86_OP_ENTRY3(EVEX_VSM4RNDS4, V,x, H,x, W,x, cpuid(AVX10_SM4) ev_vl(EV_VLALL)
                      ev_tt(FULL_MEM) ev_mask(NONE) ev_w(W0) ev_es(32)),
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_DA);
}
#endif /* __Use_Original_Qemu (U412) */
#if __Use_Original_Qemu != 1 /* ours (U550) */
/*
 * NoVmp (ledger U550): AVX512_VBMI2 forms (SDM Vol2C VPCOMPRESSB/VCOMPRESSW, VPEXPANDB/
 * VPEXPANDW, VPSHLD, VPSHLDV, VPSHRD, VPSHRDV), "AVX512_VBMI2 OR AVX10.1" (+ AVX512VL below
 * 512 bits), all EVEX.66, {k1}{z}, class E4 (fault suppression). The word forms (W1) use the
 * Full Mem tuple (no {1toN}), the D/Q forms the Full tuple (EVEX.W selects the element size).
 */
#define EV_VBMI2(tt) cpuid(AVX512_VBMI2) ev_vl(EV_VLALL) ev_tt(tt) ev_mask(MZ)
#endif /* __Use_Original_Qemu (U550) */
#if __Use_Original_Qemu != 1 /* ours (U551) */
/*
 * NoVmp (ledger U551): 0F38 62 VPEXPANDB (W0) / VPEXPANDW (W1) zmm1{k1}{z}, zmm2/m and 63
 * VPCOMPRESSB (W0) / VPCOMPRESSW (W1) zmm1/m{k1}{z}, zmm2: Tuple1 Scalar (N = element size),
 * EVEX.vvvv reserved, z with a memory destination #UD; the contiguous memory side is
 * accessed for popcount(k1) elements only (gen_evex_cx, U213).
 */
static void decode_EVEX_0F38_62(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_62[2] = {
        X86_OP_ENTRY3(EVEX_CX, V,x, None,None, W,x, EV_VBMI2(T1S) ev_w(W0) ev_es(8) p_66
                      .evex_cx = X86_EVEX_CX_EXPAND),
        X86_OP_ENTRY3(EVEX_CX, V,x, None,None, W,x, EV_VBMI2(T1S) ev_w(W1) ev_es(16) p_66
                      .evex_cx = X86_EVEX_CX_EXPAND),
    };
    *entry = *decode_evex_by_w(s, opcodes_EVEX_0F38_62);
}

static void decode_EVEX_0F38_63(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_63[2] = {
        X86_OP_ENTRY3(EVEX_CX, W,x, None,None, V,x, EV_VBMI2(T1S) ev_w(W0) ev_es(8) p_66
                      .evex_cx = X86_EVEX_CX_COMPRESS),
        X86_OP_ENTRY3(EVEX_CX, W,x, None,None, V,x, EV_VBMI2(T1S) ev_w(W1) ev_es(16) p_66
                      .evex_cx = X86_EVEX_CX_COMPRESS),
    };
    *entry = *decode_evex_by_w(s, opcodes_EVEX_0F38_63);
}
#endif /* __Use_Original_Qemu (U551) */
#if __Use_Original_Qemu != 1 /* ours (U556) */
/*
 * NoVmp (ledgers U553, U556): 0F38 72, one decoder for the slot: 66 W1 = VPSHRDVW (AVX512_VBMI2,
 * U553); F2 W0 = VCVTNE2PS2BF16 zmm1{k1}{z}, zmm2, zmm3/m/m32bcst (Full tuple, word writemask,
 * class E4NF) and F3 W0 = VCVTNEPS2BF16 ymm1{k1}{z}, zmm2/m/m32bcst (destination VL/2, Full
 * tuple, word writemask, class E4), "(AVX512_BF16 AND AVX512F) OR AVX10.1" (SDM Vol2C).
 */
static void decode_EVEX_0F38_72(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_EVEX_0F38_72[4] = {
        {},
        X86_OP_ENTRY3(EVEX_VPSHRDV, V,x, H,x, W,x, EV_VBMI2(FULL_MEM) ev_w(W1) ev_es(16)
                      ev_dsrc),
        X86_OP_ENTRY3(EVEX_VCVTNEPS2BF16, V,xh, None,None, W,x, cpuid(AVX512_BF16)
                      ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W0) ev_es(32) ev_ds(16)),
        X86_OP_ENTRY3(EVEX_VCVTNE2PS2BF16, V,x, H,x, W,x, cpuid(AVX512_BF16)
                      ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W0) ev_es(32) ev_ds(16)
                      ev_nf),
    };
    *entry = *decode_by_prefix(s, opcodes_EVEX_0F38_72);
}
#endif /* __Use_Original_Qemu (U556) */
#if __Use_Original_Qemu != 1 /* ours (U333) */
/* NoVmp (ledgers U333-U339): AVX512-FP16 tables (maps 5 and 6, NP/F3 forms of map 3) */
#include "decode_fp16.c.inc"
#endif /* __Use_Original_Qemu (U333) */

static const X86OpEntry opcodes_evex_0F[256] = {
#if __Use_Original_Qemu != 1 /* ours (U148) */
    [0x10] = X86_OP_GROUP0(EVEX_0F10),
    [0x11] = X86_OP_GROUP0(EVEX_0F11),
    [0x28] = X86_OP_GROUP0(EVEX_0F28),
    [0x29] = X86_OP_GROUP0(EVEX_0F29),
    [0x6f] = X86_OP_GROUP0(EVEX_0F6F),
    [0x7f] = X86_OP_GROUP0(EVEX_0F7F),
#endif /* __Use_Original_Qemu (U148) */
#if __Use_Original_Qemu != 1 /* ours (U151) */
    [0x58] = X86_OP_GROUP0(EVEX_0F58),
    [0x59] = X86_OP_GROUP0(EVEX_0F59),
    [0x5c] = X86_OP_GROUP0(EVEX_0F5C),
#endif /* __Use_Original_Qemu (U151) */
#if __Use_Original_Qemu != 1 /* ours (U156) */
    [0x51] = X86_OP_GROUP0(EVEX_0F51),
    [0x5d] = X86_OP_GROUP0(EVEX_0F5D),
    [0x5e] = X86_OP_GROUP0(EVEX_0F5E),
    [0x5f] = X86_OP_GROUP0(EVEX_0F5F),
#endif /* __Use_Original_Qemu (U156) */
#if __Use_Original_Qemu != 1 /* ours (U154) */
    /* VPCMPGTD k1{k2}, H, W/m32bcst (66 W0); E4 */
    [0x66] = X86_OP_ENTRY3(EVEX_VPCMPGT, K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(KDEST) ev_w(W0) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U154) */
#if __Use_Original_Qemu != 1 /* ours (U158) */
    [0x72] = X86_OP_GROUP0(EVEX_0F72),
    [0x73] = X86_OP_GROUP0(EVEX_0F73),
#endif /* __Use_Original_Qemu (U158) */
#if __Use_Original_Qemu != 1 /* ours (U152) */
    /* VPCMPEQD k1{k2}, H, W/m32bcst (66 W0); E4 */
    [0x76] = X86_OP_ENTRY3(EVEX_VPCMPEQ, K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(KDEST) ev_w(W0) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U152) */
#if __Use_Original_Qemu != 1 /* ours (U149) */
    [0xd4] = X86_OP_ENTRY3(PADDQ, V,x, H,x, W,x, EV_INT(W1)),  /* vpaddq */
    [0xfa] = X86_OP_ENTRY3(PSUBD, V,x, H,x, W,x, EV_INT(W0)),  /* vpsubd */
    [0xfb] = X86_OP_ENTRY3(PSUBQ, V,x, H,x, W,x, EV_INT(W1)),  /* vpsubq */
    [0xfe] = X86_OP_ENTRY3(PADDD, V,x, H,x, W,x, EV_INT(W0)),  /* vpaddd */
#endif /* __Use_Original_Qemu (U149) */
#if __Use_Original_Qemu != 1 /* ours (U150) */
    /* NoVmp (ledger U150): bitwise logic; EVEX.W selects D/Q (masking granularity, {1toN}) */
    [0xdb] = X86_OP_ENTRY3(PAND,  V,x, H,x, W,x, EV_INT(WSEL)), /* vpandd/q */
    [0xdf] = X86_OP_ENTRY3(PANDN, V,x, H,x, W,x, EV_INT(WSEL)), /* vpandnd/q */
    [0xeb] = X86_OP_ENTRY3(POR,   V,x, H,x, W,x, EV_INT(WSEL)), /* vpord/q */
    [0xef] = X86_OP_ENTRY3(PXOR,  V,x, H,x, W,x, EV_INT(WSEL)), /* vpxord/q */
#endif /* __Use_Original_Qemu (U150) */
#if __Use_Original_Qemu != 1 /* ours (U155) */
    /* VPMULUDQ (66 W1 F4): unsigned dword x dword -> qword */
    [0xf4] = X86_OP_ENTRY3(EVEX_PMULUDQ, V,x, H,x, W,x, EV_INT(W1)),
#endif /* __Use_Original_Qemu (U155) */
#if __Use_Original_Qemu != 1 /* ours (U196) */
    [0x2e] = X86_OP_GROUP0(EVEX_0F2E),
    [0x2f] = X86_OP_GROUP0(EVEX_0F2F),
#endif /* __Use_Original_Qemu (U196) */
#if __Use_Original_Qemu != 1 /* ours (U197) */
    [0xc2] = X86_OP_GROUP0(EVEX_0FC2),
#endif /* __Use_Original_Qemu (U197) */
#if __Use_Original_Qemu != 1 /* ours (U211) */
    /* NoVmp (ledger U211): unpacks and shuffles (Full, E4NF) */
    [0x14] = X86_OP_GROUP0(EVEX_0F14),
    [0x15] = X86_OP_GROUP0(EVEX_0F15),
    [0x62] = X86_OP_ENTRY3(EVEX_UNPCKL, V,x, H,x, W,x, EV_PRM(EV_VLALL, W0) p_66), /* vpunpckldq */
    [0x6a] = X86_OP_ENTRY3(EVEX_UNPCKH, V,x, H,x, W,x, EV_PRM(EV_VLALL, W0) p_66), /* vpunpckhdq */
    [0x6c] = X86_OP_ENTRY3(EVEX_UNPCKL, V,x, H,x, W,x, EV_PRM(EV_VLALL, W1) p_66), /* vpunpcklqdq */
    [0x6d] = X86_OP_ENTRY3(EVEX_UNPCKH, V,x, H,x, W,x, EV_PRM(EV_VLALL, W1) p_66), /* vpunpckhqdq */
    /* 0x70: 66 VPSHUFD is the 66 slot of decode_EVEX_0F70 (with U264's F3/F2 VPSHUFHW/LW) */
    [0xc6] = X86_OP_GROUP0(EVEX_0FC6),
#endif /* __Use_Original_Qemu (U211) */
#if __Use_Original_Qemu != 1 /* ours (U214) */
    /* NoVmp (ledger U214): moves (decode_EVEX_0F12 .. decode_EVEX_0F7E) */
    [0x12] = X86_OP_GROUP0(EVEX_0F12),
    [0x13] = X86_OP_GROUP0(EVEX_0F13),
    [0x16] = X86_OP_GROUP0(EVEX_0F16),
    [0x17] = X86_OP_GROUP0(EVEX_0F17),
    [0x2b] = X86_OP_GROUP0(EVEX_0F2B),
    [0x6e] = X86_OP_ENTRY3(EVEX_MOVD_TO, V,dq, None,None, E,y, EV_M128(T1S, WSEL) ev_w32 p_66),
    [0x7e] = X86_OP_GROUP0(EVEX_0F7E),
    [0xd6] = X86_OP_GROUP0(EVEX_0FD6),     /* 66 W1 VMOVQ (U214), 66 W0 VMOVD (U410) */
    [0xe7] = X86_OP_ENTRY3(EVEX_MOV, WM,x, None,None, V,x, EV_NT(W0) p_66), /* vmovntdq */
#endif /* __Use_Original_Qemu (U214) */
#if __Use_Original_Qemu != 1 /* ours (U262) */
    /*
     * NoVmp (ledger U262): AVX512BW byte/word arithmetic, EVEX.66.0F.WIG, Full Mem tuple,
     * {k1}{z} per byte / word, E4 (fault suppression)
     */
    [0xd5] = X86_OP_ENTRY3(PMULLW,       V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xd8] = X86_OP_ENTRY3(PSUBUSB,      V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xd9] = X86_OP_ENTRY3(PSUBUSW,      V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xda] = X86_OP_ENTRY3(PMINUB,       V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xdc] = X86_OP_ENTRY3(PADDUSB,      V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xdd] = X86_OP_ENTRY3(PADDUSW,      V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xde] = X86_OP_ENTRY3(PMAXUB,       V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xe0] = X86_OP_ENTRY3(EVEX_PAVGB,   V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xe3] = X86_OP_ENTRY3(EVEX_PAVGW,   V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xe4] = X86_OP_ENTRY3(EVEX_PMULHUW, V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xe5] = X86_OP_ENTRY3(EVEX_PMULHW,  V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xe8] = X86_OP_ENTRY3(PSUBSB,       V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xe9] = X86_OP_ENTRY3(PSUBSW,       V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xea] = X86_OP_ENTRY3(PMINSW,       V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xec] = X86_OP_ENTRY3(PADDSB,       V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xed] = X86_OP_ENTRY3(PADDSW,       V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xee] = X86_OP_ENTRY3(PMAXSW,       V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xf8] = X86_OP_ENTRY3(PSUBB,        V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xf9] = X86_OP_ENTRY3(PSUBW,        V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0xfc] = X86_OP_ENTRY3(PADDB,        V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0xfd] = X86_OP_ENTRY3(PADDW,        V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
#endif /* __Use_Original_Qemu (U262) */
#if __Use_Original_Qemu != 1 /* ours (U263) */
    /*
     * NoVmp (ledger U263): VPMADDWD (dword writemask), VPSADBW (no writemask): Full Mem,
     * E4NF (no fault suppression)
     */
    [0xf5] = X86_OP_ENTRY3(EVEX_PMADDWD, V,x, H,x, W,x, EV_BW(WIG, 32) ev_nf p_66),
    [0xf6] = X86_OP_ENTRY3(EVEX_PSADBW,  V,x, H,x, W,x, cpuid(AVX512BW) ev_vl(EV_VLALL)
                           ev_tt(FULL_MEM) ev_mask(NONE) ev_w(WIG) ev_es(64) ev_nf p_66),
#endif /* __Use_Original_Qemu (U263) */
#if __Use_Original_Qemu != 1 /* ours (U264) */
    /*
     * NoVmp (ledger U264): unpacks / packs (66.0F.WIG; VPACKSSDW 66.0F.W0: Full tuple,
     * m32bcst, word writemask), VPSHUFHW/LW (0F 70 F3/F2): E4NF
     */
    [0x60] = X86_OP_ENTRY3(EVEX_PUNPCKLBW, V,x, H,x, W,x, EV_BW(WIG, 8) ev_nf p_66),
    [0x61] = X86_OP_ENTRY3(EVEX_PUNPCKLWD, V,x, H,x, W,x, EV_BW(WIG, 16) ev_nf p_66),
    [0x63] = X86_OP_ENTRY3(EVEX_PACKSSWB,  V,x, H,x, W,x, EV_BW(WIG, 8) ev_nf p_66),
    [0x67] = X86_OP_ENTRY3(EVEX_PACKUSWB,  V,x, H,x, W,x, EV_BW(WIG, 8) ev_nf p_66),
    [0x68] = X86_OP_ENTRY3(EVEX_PUNPCKHBW, V,x, H,x, W,x, EV_BW(WIG, 8) ev_nf p_66),
    [0x69] = X86_OP_ENTRY3(EVEX_PUNPCKHWD, V,x, H,x, W,x, EV_BW(WIG, 16) ev_nf p_66),
    [0x6b] = X86_OP_ENTRY3(EVEX_PACKSSDW,  V,x, H,x, W,x, cpuid(AVX512BW) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(MZ) ev_w(W0) ev_es(32) ev_ds(16) ev_nf p_66),
    [0x70] = X86_OP_GROUP0(EVEX_0F70),
#endif /* __Use_Original_Qemu (U264) */
#if __Use_Original_Qemu != 1 /* ours (U265) */
    /*
     * NoVmp (ledger U265): 71 /2 /4 /6 word shifts by imm8; D1/E1/F1 VPSRLW/VPSRAW/VPSLLW
     * zmm1{k1}{z}, zmm2, xmm3/m128 (Mem128 tuple: disp8*N = 16 at every VL, E4NF)
     */
    [0x71] = X86_OP_GROUP0(EVEX_0F71),
    [0xd1] = X86_OP_ENTRY3(EVEX_PSRLW_r, V,x, H,x, W,dq, cpuid(AVX512BW) ev_vl(EV_VLALL)
                           ev_tt(MEM128) ev_mask(MZ) ev_w(WIG) ev_es(16) ev_nf p_66),
    [0xe1] = X86_OP_ENTRY3(EVEX_PSRAW_r, V,x, H,x, W,dq, cpuid(AVX512BW) ev_vl(EV_VLALL)
                           ev_tt(MEM128) ev_mask(MZ) ev_w(WIG) ev_es(16) ev_nf p_66),
    [0xf1] = X86_OP_ENTRY3(EVEX_PSLLW_r, V,x, H,x, W,dq, cpuid(AVX512BW) ev_vl(EV_VLALL)
                           ev_tt(MEM128) ev_mask(MZ) ev_w(WIG) ev_es(16) ev_nf p_66),
#endif /* __Use_Original_Qemu (U265) */
#if __Use_Original_Qemu != 1 /* ours (U266) */
    /* NoVmp (ledger U266): VPCMPGTB/W 64/65, VPCMPEQB/W 74/75 k1{k2} (66.0F.WIG, E4.nb) */
    [0x64] = X86_OP_ENTRY3(EVEX_VPCMPGT, K,q, H,x, W,x, EV_BWK(WIG, 8) p_66),
    [0x65] = X86_OP_ENTRY3(EVEX_VPCMPGT, K,q, H,x, W,x, EV_BWK(WIG, 16) p_66),
    [0x74] = X86_OP_ENTRY3(EVEX_VPCMPEQ, K,q, H,x, W,x, EV_BWK(WIG, 8) p_66),
    [0x75] = X86_OP_ENTRY3(EVEX_VPCMPEQ, K,q, H,x, W,x, EV_BWK(WIG, 16) p_66),
#endif /* __Use_Original_Qemu (U266) */
#if __Use_Original_Qemu != 1 /* ours (U267) */
    /*
     * NoVmp (ledger U267): VPINSRW xmm1, xmm2, r32/m16, imm8 (C4) and VPEXTRW r32, xmm1,
     * imm8 (C5, register only): EVEX.128.66.0F.WIG, E9NF (no writemask, L'L != 00b #UD, no
     * AVX512VL needed), Tuple1 Scalar (disp8*N = 2). The VEX gen functions do the work.
     */
    [0xc4] = X86_OP_ENTRY4(PINSRW, V,dq, H,dq, E,w, cpuid(AVX512BW) ev_vl(X86_EVEX_VL128)
                           ev_tt(T1S) ev_mask(NONE) ev_w(WIG) ev_es(16) ev_nf p_66),
    [0xc5] = X86_OP_ENTRY3(PEXTRW, G,d, U,dq, I,b, cpuid(AVX512BW) ev_vl(X86_EVEX_VL128)
                           ev_mask(NONE) ev_w(WIG) ev_es(16) p_66),
#endif /* __Use_Original_Qemu (U267) */
#if __Use_Original_Qemu != 1 /* ours (U291) */
    [0x54] = X86_OP_GROUP0(EVEX_0F54),  /* vandps / vandpd */
    [0x55] = X86_OP_GROUP0(EVEX_0F55),  /* vandnps / vandnpd */
    [0x56] = X86_OP_GROUP0(EVEX_0F56),  /* vorps / vorpd */
    [0x57] = X86_OP_GROUP0(EVEX_0F57),  /* vxorps / vxorpd */
#endif /* __Use_Original_Qemu (U291) */
#if __Use_Original_Qemu != 1 /* ours (U231) */
    [0x5a] = X86_OP_GROUP0(EVEX_0F5A),
    [0x5b] = X86_OP_GROUP0(EVEX_0F5B),
    [0x78] = X86_OP_GROUP0(EVEX_0F78),
    [0x79] = X86_OP_GROUP0(EVEX_0F79),
    [0x7a] = X86_OP_GROUP0(EVEX_0F7A),
    [0x7b] = X86_OP_GROUP0(EVEX_0F7B),
    [0xe6] = X86_OP_GROUP0(EVEX_0FE6),
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U232) */
    [0x2a] = X86_OP_GROUP0(EVEX_0F2A),
    [0x2c] = X86_OP_GROUP0(EVEX_0F2C),
    [0x2d] = X86_OP_GROUP0(EVEX_0F2D),
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U234) */
    /*
     * NoVmp (ledger U234): VPSRLD/Q (D2/D3), VPSRAD/Q (E2 W0/W1), VPSLLD/Q (F2/F3) zmm1{k1}{z},
     * zmm2, xmm3/m128: the count is the low 64 bits of a 128-bit operand (Mem128 tuple,
     * disp8 N = 16, no {1toN}); E4NF.nb: the count is read whole, no fault suppression.
     */
    [0xd2] = X86_OP_ENTRY3(EVEX_VPSRL_X, V,x, H,x, W,dq, EV_SHX(W0)),
    [0xd3] = X86_OP_ENTRY3(EVEX_VPSRL_X, V,x, H,x, W,dq, EV_SHX(W1)),
    [0xe2] = X86_OP_ENTRY3(EVEX_VPSRA_X, V,x, H,x, W,dq, EV_SHX(WSEL)),
    [0xf2] = X86_OP_ENTRY3(EVEX_VPSLL_X, V,x, H,x, W,dq, EV_SHX(W0)),
    [0xf3] = X86_OP_ENTRY3(EVEX_VPSLL_X, V,x, H,x, W,dq, EV_SHX(W1)),
#endif /* __Use_Original_Qemu (U234) */
};

static const X86OpEntry opcodes_evex_0F38[256] = {
#if __Use_Original_Qemu != 1 /* ours (U154) */
    [0x27] = X86_OP_GROUP0(EVEX_0F38_27),
    /* VPCMPGTQ k1{k2}, H, W/m64bcst (66 W1); E4 */
    [0x37] = X86_OP_ENTRY3(EVEX_VPCMPGT, K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(KDEST) ev_w(W1) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U154) */
#if __Use_Original_Qemu != 1 /* ours (U155) */
    /*
     * NoVmp (ledger U155): Full tuple integer forms, {k1}{z}, {1toN}; E4. VPABSD (1E W0) /
     * VPABSQ (1F W1), VPMULDQ (28 W1), VPMINSD/Q (39), VPMINUD/Q (3B), VPMAXSD/Q (3D),
     * VPMAXUD/Q (3F), VPMULLD (40 W0; W1 = VPMULLQ is AVX512DQ, not yet).
     */
    [0x1e] = X86_OP_ENTRY3(EVEX_PABS, V,x, None,None, W,x, EV_INT(W0)),
    [0x1f] = X86_OP_ENTRY3(EVEX_PABS, V,x, None,None, W,x, EV_INT(W1)),
#if __Use_Original_Qemu != 1 /* ours (U266) */
    [0x28] = X86_OP_GROUP0(EVEX_0F38_28),   /* U266: with VPMOVM2B/W (F3) */
#endif /* __Use_Original_Qemu (U266) */
    [0x39] = X86_OP_ENTRY3(EVEX_PMINS, V,x, H,x, W,x, EV_INT(WSEL)),
    [0x3b] = X86_OP_ENTRY3(EVEX_PMINU, V,x, H,x, W,x, EV_INT(WSEL)),
    [0x3d] = X86_OP_ENTRY3(EVEX_PMAXS, V,x, H,x, W,x, EV_INT(WSEL)),
    [0x3f] = X86_OP_ENTRY3(EVEX_PMAXU, V,x, H,x, W,x, EV_INT(WSEL)),
#if __Use_Original_Qemu == 1 /* original QEMU (U290) */
    [0x40] = X86_OP_ENTRY3(PMULLD, V,x, H,x, W,x, EV_INT(W0)),
#else /* ours (U290) */
    /* NoVmp (ledger U290): W0 VPMULLD (U155), W1 VPMULLQ (AVX512DQ) */
    [0x40] = X86_OP_GROUP0(EVEX_0F38_40),
#endif /* __Use_Original_Qemu (U290) */
#endif /* __Use_Original_Qemu (U155) */
#if __Use_Original_Qemu != 1 /* ours (U157) */
    /* NoVmp (ledger U157): VPSRLVD/Q (45), VPSRAVD/Q (46), VPSLLVD/Q (47); Full, E4 */
    [0x45] = X86_OP_ENTRY3(EVEX_VPSRLV, V,x, H,x, W,x, EV_INT(WSEL)),
    [0x46] = X86_OP_ENTRY3(EVEX_VPSRAV, V,x, H,x, W,x, EV_INT(WSEL)),
    [0x47] = X86_OP_ENTRY3(EVEX_VPSLLV, V,x, H,x, W,x, EV_INT(WSEL)),
#endif /* __Use_Original_Qemu (U157) */
#if __Use_Original_Qemu != 1 /* ours (U153) */
    /*
     * NoVmp (ledger U153): broadcasts, Tuple1 Scalar memory operand, {k1}{z}; E6 (E7NM for
     * the GPR source). VBROADCASTSD has no EVEX.128 form; VPBROADCASTQ r64 outside 64-bit
     * mode behaves as VPBROADCASTD r32 (EVEX.W ignored).
     */
    [0x18] = X86_OP_ENTRY3(VPBROADCASTD, V,x, None,None, W,d, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(T1S) ev_mask(MZ) ev_w(W0) ev_es(W) p_66), /* vbroadcastss */
    /* 0x19 W1 vbroadcastsd: decode_EVEX_0F38_19 (U215, W0 = VBROADCASTF32X2) */
    [0x58] = X86_OP_ENTRY3(VPBROADCASTD, V,x, None,None, W,d, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(T1S) ev_mask(MZ) ev_w(W0) ev_es(W) p_66),
    /* 0x59 W1 vpbroadcastq: decode_EVEX_0F38_59 (U215, W0 = VBROADCASTI32X2) */
    [0x7c] = X86_OP_ENTRY3(EVEX_VPBROADCAST_R, V,x, None,None, R,y, cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_mask(MZ) ev_w(WSEL) ev_es(W) ev_w32 p_66),
#endif /* __Use_Original_Qemu (U153) */
#if __Use_Original_Qemu != 1 /* ours (U152) */
    /* VPCMPEQQ k1{k2}, H, W/m64bcst (66 W1); E4 */
#if __Use_Original_Qemu != 1 /* ours (U266) */
    [0x29] = X86_OP_GROUP0(EVEX_0F38_29),   /* U266: with VPMOVB2M/W2M (F3) */
#endif /* __Use_Original_Qemu (U266) */
#endif /* __Use_Original_Qemu (U152) */
#if __Use_Original_Qemu != 1 /* ours (U250) */
    /*
     * NoVmp (ledger U250): gathers, 66 0F38 90 VPGATHERDD/DQ, 91 VPGATHERQD/QQ, 92 VGATHERDPS/
     * DPD, 93 VGATHERQPS/QPD (EVEX.W: dword/qword data; opcode bit 0: qword indices). VSIB
     * memory operand (vex_class 12: EVEX.V' extends the index), k1 required and no {z},
     * Tuple1 Scalar: disp8*N with N = the data element size (the M,x operand makes
     * evex_disp8_scale use the EVEX.W element size). Exception class E12.
     */
    [0x90] = X86_OP_ENTRY3(EVEX_VGATHER, V,x, None,None, M,x, vex12 cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_tt(T1S) ev_mask(KREQ) ev_w(WSEL) ev_es(W) p_66),
    [0x91] = X86_OP_ENTRY3(EVEX_VGATHER, V,x, None,None, M,x, vex12 cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_tt(T1S) ev_mask(KREQ) ev_w(WSEL) ev_es(W) p_66),
    [0x92] = X86_OP_ENTRY3(EVEX_VGATHER, V,x, None,None, M,x, vex12 cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_tt(T1S) ev_mask(KREQ) ev_w(WSEL) ev_es(W) p_66),
    [0x93] = X86_OP_ENTRY3(EVEX_VGATHER, V,x, None,None, M,x, vex12 cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_tt(T1S) ev_mask(KREQ) ev_w(WSEL) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U250) */
#if __Use_Original_Qemu != 1 /* ours (U251) */
    /*
     * NoVmp (ledger U251): scatters, 66 0F38 A0 VPSCATTERDD/DQ, A1 VPSCATTERQD/QQ, A2
     * VSCATTERDPS/DPD, A3 VSCATTERQPS/QPD: VSIB destination (op0), source ModRM.reg (op2);
     * as the gathers (k1 required, no {z}, T1S, E12) but the source may be the index.
     */
    [0xa0] = X86_OP_ENTRY3(EVEX_VSCATTER, M,x, None,None, V,x, vex12 cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_tt(T1S) ev_mask(KREQ) ev_w(WSEL) ev_es(W) p_66),
    [0xa1] = X86_OP_ENTRY3(EVEX_VSCATTER, M,x, None,None, V,x, vex12 cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_tt(T1S) ev_mask(KREQ) ev_w(WSEL) ev_es(W) p_66),
    [0xa2] = X86_OP_ENTRY3(EVEX_VSCATTER, M,x, None,None, V,x, vex12 cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_tt(T1S) ev_mask(KREQ) ev_w(WSEL) ev_es(W) p_66),
    [0xa3] = X86_OP_ENTRY3(EVEX_VSCATTER, M,x, None,None, V,x, vex12 cpuid(AVX512F)
                           ev_vl(EV_VLALL) ev_tt(T1S) ev_mask(KREQ) ev_w(WSEL) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U251) */
#if __Use_Original_Qemu != 1 /* ours (U201) */
    /*
     * NoVmp (ledger U201): VPBLENDMD/Q (64), VBLENDMPS/PD (65): zmm1{k1}{z}, zmm2 (vvvv),
     * zmm3/m/{1toN}; Full tuple, class E4 (no SIMD FP exception); DEST[j] := SRC2[j] where
     * k1[j] or no mask, else SRC1[j] (merging) / 0 (zeroing) - X86OpEntry.evex_msrc1.
     */
    [0x64] = X86_OP_ENTRY3(EVEX_MOV, V,x, H,x, W,x, EV_INT(WSEL) ev_msrc1),
    [0x65] = X86_OP_ENTRY3(EVEX_MOV, V,x, H,x, W,x, EV_INT(WSEL) ev_msrc1),
#endif /* __Use_Original_Qemu (U201) */
#if __Use_Original_Qemu != 1 /* ours (U199) */
    /*
     * NoVmp (ledger U199): packed FMA zmm1{k1}{z}, zmm2 (vvvv), zmm3/m/{1toN}{er}: 66, W0 =
     * PS, W1 = PD, Full tuple, class E2; DEST is the first source (evex_dsrc).
     */
#define EV_FMAP X86_OP_ENTRY3(EVEX_VFMAP, V,x, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL) \
                    ev_tt(FULL) ev_mask(MZ) ev_rc(ER) ev_w(WSEL) ev_es(W) ev_fp ev_dsrc p_66)
    [0x96] = EV_FMAP, [0x97] = EV_FMAP, [0x98] = EV_FMAP, [0x9a] = EV_FMAP,
    [0x9c] = EV_FMAP, [0x9e] = EV_FMAP,
    [0xa6] = EV_FMAP, [0xa7] = EV_FMAP, [0xa8] = EV_FMAP, [0xaa] = EV_FMAP,
    [0xac] = EV_FMAP, [0xae] = EV_FMAP,
    [0xb6] = EV_FMAP, [0xb7] = EV_FMAP, [0xb8] = EV_FMAP, [0xba] = EV_FMAP,
    [0xbc] = EV_FMAP, [0xbe] = EV_FMAP,
#undef EV_FMAP
#endif /* __Use_Original_Qemu (U199) */
#if __Use_Original_Qemu != 1 /* ours (U200) */
    /*
     * NoVmp (ledger U200): scalar FMA xmm1{k1}{z}, xmm2 (vvvv), xmm3/m32/m64{er}: 66,
     * EVEX.LLIG, Tuple1 Scalar, class E3; DEST is the first source (evex_dsrc).
     */
#define EV_FMAS X86_OP_GROUP3(EVEX_FMAS, V,x, H,x, W,ss, cpuid(AVX512F) ev_vl(X86_EVEX_LIG) \
                    ev_tt(T1S) ev_mask(MZ) ev_rc(ER) ev_w(WSEL) ev_es(W) ev_fp ev_dsrc p_66)
    [0x99] = EV_FMAS, [0x9b] = EV_FMAS, [0x9d] = EV_FMAS, [0x9f] = EV_FMAS,
    [0xa9] = EV_FMAS, [0xab] = EV_FMAS, [0xad] = EV_FMAS, [0xaf] = EV_FMAS,
    [0xb9] = EV_FMAS, [0xbb] = EV_FMAS, [0xbd] = EV_FMAS, [0xbf] = EV_FMAS,
#undef EV_FMAS
#endif /* __Use_Original_Qemu (U200) */
#if __Use_Original_Qemu != 1 /* ours (U211) */
    /*
     * NoVmp (ledger U211): VPERMILPS/PD vector control (0C W0 / 0D W1), VPERMPS/PD (16) and
     * VPERMD/Q (36) EVEX.256/512 only, VPERMI2D/Q (76), VPERMI2PS/PD (77), VPERMT2D/Q (7E),
     * VPERMT2PS/PD (7F); all 66, Full, E4NF.
     */
    [0x0c] = EV_PRM3(EVEX_VPERMILPS_V, EV_VLALL, W0, p_66),
    [0x0d] = EV_PRM3(EVEX_VPERMILPD_V, EV_VLALL, W1, p_66),
    [0x16] = EV_PRM3(EVEX_VPERM, EV_VL256UP, WSEL, p_66),
    [0x36] = EV_PRM3(EVEX_VPERM, EV_VL256UP, WSEL, p_66),
    [0x76] = EV_PRM3(EVEX_VPERMI2, EV_VLALL, WSEL, p_66),
    [0x77] = EV_PRM3(EVEX_VPERMI2, EV_VLALL, WSEL, p_66),
    [0x7e] = EV_PRM3(EVEX_VPERMT2, EV_VLALL, WSEL, p_66),
    [0x7f] = EV_PRM3(EVEX_VPERMT2, EV_VLALL, WSEL, p_66),
#endif /* __Use_Original_Qemu (U211) */
#if __Use_Original_Qemu != 1 /* ours (U212) */
    /* NoVmp (ledger U212): VPMOVZX/SX (66), VPMOV/VPMOVS/VPMOVUS (F3), decode_EVEX_0F38_PMOV */
    [0x11] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x12] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    /* 0x13: decode_EVEX_0F38_13 (66 VCVTPH2PS, U231) falls back to EVEX_0F38_PMOV (F3) */
    [0x14] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x15] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x21] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x22] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x23] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x24] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x25] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x31] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x32] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x33] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x34] = X86_OP_GROUP0(EVEX_0F38_PMOV),
    [0x35] = X86_OP_GROUP0(EVEX_0F38_PMOV),
#endif /* __Use_Original_Qemu (U212) */
#if __Use_Original_Qemu != 1 /* ours (U213) */
    /*
     * NoVmp (ledger U213): VEXPANDPS/PD (88), VPEXPANDD/Q (89), VCOMPRESSPS/PD (8A),
     * VPCOMPRESSD/Q (8B): 66, W selects the element size, Tuple1 Scalar (N = element size),
     * {k1}{z} (z with a memory destination #UD), E4.nb (fault suppression, no {1toN}).
     */
    [0x88] = X86_OP_ENTRY3(EVEX_CX, V,x, None,None, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(T1S) ev_mask(MZ) ev_w(WSEL) ev_es(W) p_66
                           .evex_cx = X86_EVEX_CX_EXPAND),
    [0x89] = X86_OP_ENTRY3(EVEX_CX, V,x, None,None, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(T1S) ev_mask(MZ) ev_w(WSEL) ev_es(W) p_66
                           .evex_cx = X86_EVEX_CX_EXPAND),
    [0x8a] = X86_OP_ENTRY3(EVEX_CX, W,x, None,None, V,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(T1S) ev_mask(MZ) ev_w(WSEL) ev_es(W) p_66
                           .evex_cx = X86_EVEX_CX_COMPRESS),
    [0x8b] = X86_OP_ENTRY3(EVEX_CX, W,x, None,None, V,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(T1S) ev_mask(MZ) ev_w(WSEL) ev_es(W) p_66
                           .evex_cx = X86_EVEX_CX_COMPRESS),
#endif /* __Use_Original_Qemu (U213) */
#if __Use_Original_Qemu != 1 /* ours (U214) */
    /* NoVmp (ledger U214): VMOVNTDQA zmm1, m512 (66 W0, E1NF, no masking) */
    [0x2a] = X86_OP_ENTRY3(EVEX_MOV, V,x, None,None, WM,x, EV_NT(W0) p_66),
#endif /* __Use_Original_Qemu (U214) */
#if __Use_Original_Qemu != 1 /* ours (U215) */
    /* NoVmp (ledger U215): VBROADCASTx32X2/32X4/64X2/32X8/64X4 (66; by EVEX.W) */
    [0x19] = X86_OP_GROUP0(EVEX_0F38_19),
    [0x1a] = X86_OP_GROUP0(EVEX_0F38_1A),
    [0x1b] = X86_OP_GROUP0(EVEX_0F38_1B),
    [0x59] = X86_OP_GROUP0(EVEX_0F38_59),
    [0x5a] = X86_OP_GROUP0(EVEX_0F38_1A),
    [0x5b] = X86_OP_GROUP0(EVEX_0F38_1B),
#endif /* __Use_Original_Qemu (U215) */
#if __Use_Original_Qemu != 1 /* ours (U262) */
    /* NoVmp (ledger U262): EVEX.66.0F38.WIG, Full Mem tuple, {k1}{z}, E4 */
    [0x0b] = X86_OP_ENTRY3(EVEX_PMULHRSW, V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0x1c] = X86_OP_ENTRY3(PABSB,         V,x, None,None, W,x, EV_BW(WIG, 8) p_66),
    [0x1d] = X86_OP_ENTRY3(PABSW,         V,x, None,None, W,x, EV_BW(WIG, 16) p_66),
    [0x38] = X86_OP_ENTRY3(PMINSB,        V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0x3a] = X86_OP_ENTRY3(PMINUW,        V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
    [0x3c] = X86_OP_ENTRY3(PMAXSB,        V,x, H,x, W,x, EV_BW(WIG, 8) p_66),
    [0x3e] = X86_OP_ENTRY3(PMAXUW,        V,x, H,x, W,x, EV_BW(WIG, 16) p_66),
#endif /* __Use_Original_Qemu (U262) */
#if __Use_Original_Qemu != 1 /* ours (U263) */
    /* NoVmp (ledger U263): VPMADDUBSW (66.0F38.WIG 04): Full Mem, word writemask, E4NF */
    [0x04] = X86_OP_ENTRY3(EVEX_PMADDUBSW, V,x, H,x, W,x, EV_BW(WIG, 16) ev_nf p_66),
#endif /* __Use_Original_Qemu (U263) */
#if __Use_Original_Qemu != 1 /* ours (U264) */
    /* NoVmp (ledger U264): VPSHUFB (00), VPACKUSDW (2B W0: Full, m32bcst, word writemask) */
    [0x00] = X86_OP_ENTRY3(EVEX_PSHUFB,   V,x, H,x, W,x, EV_BW(WIG, 8) ev_nf p_66),
    [0x2b] = X86_OP_ENTRY3(EVEX_PACKUSDW, V,x, H,x, W,x, cpuid(AVX512BW) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(MZ) ev_w(W0) ev_es(32) ev_ds(16) ev_nf p_66),
#endif /* __Use_Original_Qemu (U264) */
#if __Use_Original_Qemu != 1 /* ours (U265) */
    /* NoVmp (ledger U265): VPSRLVW 10, VPSRAVW 11, VPSLLVW 12 (66.0F38.W1, Full Mem, E4) */
#if __Use_Original_Qemu != 1 /* ours (U268) */
    [0x10] = X86_OP_GROUP0(EVEX_0F38_10),   /* U268: with VPMOVUSWB (F3) */
#endif /* __Use_Original_Qemu (U268) */
    /* 0x11 / 0x12 (66): VPSRAVW / VPSLLVW in decode_EVEX_0F38_PMOV (F3: VPMOVUSDB/QB, U212) */
#endif /* __Use_Original_Qemu (U265) */
#if __Use_Original_Qemu != 1 /* ours (U266) */
    [0x26] = X86_OP_GROUP0(EVEX_0F38_26),   /* VPTESTMB/W (66), VPTESTNMB/W (F3) */
#endif /* __Use_Original_Qemu (U266) */
#if __Use_Original_Qemu != 1 /* ours (U267) */
    /*
     * NoVmp (ledger U267): VPBROADCASTB/W zmm1{k1}{z}, xmm2/m8 / xmm2/m16 (78/79, Tuple1
     * Scalar, E6: the element is read when any lane is active) and from a GPR (7A/7B, r32,
     * E7NM); 66.0F38.W0
     */
    [0x78] = X86_OP_ENTRY3(VPBROADCASTB, V,x, None,None, W,b, cpuid(AVX512BW) ev_vl(EV_VLALL)
                           ev_tt(T1S) ev_mask(MZ) ev_w(W0) ev_es(8) p_66),
    [0x79] = X86_OP_ENTRY3(VPBROADCASTW, V,x, None,None, W,w, cpuid(AVX512BW) ev_vl(EV_VLALL)
                           ev_tt(T1S) ev_mask(MZ) ev_w(W0) ev_es(16) p_66),
    [0x7a] = X86_OP_ENTRY3(EVEX_VPBROADCAST_R, V,x, None,None, R,d, cpuid(AVX512BW)
                           ev_vl(EV_VLALL) ev_mask(MZ) ev_w(W0) ev_es(8) p_66),
    [0x7b] = X86_OP_ENTRY3(EVEX_VPBROADCAST_R, V,x, None,None, R,d, cpuid(AVX512BW)
                           ev_vl(EV_VLALL) ev_mask(MZ) ev_w(W0) ev_es(16) p_66),
#endif /* __Use_Original_Qemu (U267) */
#if __Use_Original_Qemu != 1 /* ours (U268) */
    [0x20] = X86_OP_GROUP0(EVEX_0F38_20),   /* VPMOVSXBW (66), VPMOVSWB (F3) */
    [0x30] = X86_OP_GROUP0(EVEX_0F38_30),   /* VPMOVZXBW (66), VPMOVWB (F3) */
#endif /* __Use_Original_Qemu (U268) */
#if __Use_Original_Qemu != 1 /* ours (U269) */
    [0x66] = X86_OP_GROUP0(EVEX_0F38_66),   /* VPBLENDMB/W */
    [0x75] = X86_OP_GROUP0(EVEX_0F38_75),   /* VPERMI2W (W1) */
    [0x7d] = X86_OP_GROUP0(EVEX_0F38_7D),   /* VPERMT2W (W1) */
    [0x8d] = X86_OP_GROUP0(EVEX_0F38_8D),   /* VPERMW (W1) */
#endif /* __Use_Original_Qemu (U269) */
#if __Use_Original_Qemu != 1 /* ours (U321) */
    /*
     * NoVmp (ledger U321): AVX512CD (SDM Vol2C VPLZCNTD/Q, VPCONFLICTD/Q): zmm1{k1}{z},
     * zmm2/m/{1toN}, EVEX.vvvv reserved; Full tuple, W selects D/Q. VPLZCNT is class E4
     * (fault suppression), VPCONFLICT E4NF (the whole memory operand is read).
     */
    [0x44] = X86_OP_ENTRY3(EVEX_VPLZCNT, V,x, None,None, W,x, cpuid(AVX512CD) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(MZ) ev_w(WSEL) ev_es(W) p_66),
    [0xc4] = X86_OP_ENTRY3(EVEX_VPCONFLICT, V,x, None,None, W,x, cpuid(AVX512CD)
                           ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(WSEL) ev_es(W)
                           .evex_nofs = 1, p_66),
#endif /* __Use_Original_Qemu (U321) */
#if __Use_Original_Qemu != 1 /* ours (U322) */
    /*
     * NoVmp (ledger U322): AVX512_IFMA (SDM Vol2C VPMADD52LUQ/HUQ, EVEX forms) zmm1{k1}{z},
     * zmm2, zmm3/m/{1to8}: 66 W1, Full tuple, class E4; the destination is the accumulator (ev_dsrc, U190).
     */
    [0xb4] = X86_OP_ENTRY3(EVEX_VPMADD52LUQ, V,x, H,x, W,x, cpuid(AVX512_IFMA)
                           ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W1) ev_es(W) ev_dsrc p_66),
    [0xb5] = X86_OP_ENTRY3(EVEX_VPMADD52HUQ, V,x, H,x, W,x, cpuid(AVX512_IFMA)
                           ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W1) ev_es(W) ev_dsrc p_66),
#endif /* __Use_Original_Qemu (U322) */
#if __Use_Original_Qemu != 1 /* ours (U323) */
    /*
     * NoVmp (ledger U323): AVX512_VPOPCNTDQ VPOPCNTD/Q zmm1{k1}{z}, zmm2/m/{1toN} (SDM Vol2C
     * VPOPCNT): 66 W0/W1 55, Full tuple, class E4, EVEX.vvvv reserved.
     */
    [0x55] = X86_OP_ENTRY3(EVEX_VPOPCNT, V,x, None,None, W,x, cpuid(AVX512_VPOPCNTDQ)
                           ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(WSEL) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U323) */
#if __Use_Original_Qemu != 1 /* ours (U324) */
    /*
     * NoVmp (ledger U324): AVX512_BITALG. 54: VPOPCNTB/W (decode_EVEX_0F38_54). 8F (66 W0):
     * VPSHUFBITQMB k1{k2}, zmm2 (vvvv), zmm3/m: Full Mem tuple, one mask bit per byte,
     * class E4 (SDM Vol2C VPSHUFBITQMB).
     */
    [0x54] = X86_OP_GROUP0(EVEX_0F38_54),
    [0x8f] = X86_OP_ENTRY3(EVEX_VPSHUFBITQMB, K,q, H,x, W,x, cpuid(AVX512_BITALG)
                           ev_vl(EV_VLALL) ev_tt(FULL_MEM) ev_mask(KDEST) ev_w(W0) ev_es(8) p_66),
#endif /* __Use_Original_Qemu (U324) */
#if __Use_Original_Qemu != 1 /* ours (U231) */
    [0x13] = X86_OP_GROUP0(EVEX_0F38_13),
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U235) */
    /*
     * NoVmp (ledger U235): VPRORVD/Q (14), VPROLVD/Q (15): Full, {k1}{z}, {1toN}; E4. The
     * opcode bytes are shared with F3 VPMOVUSQW/VPMOVUSQD (U212): the 66 forms are an
     * overlay in decode_EVEX_0F38_PMOV, [0x14]/[0x15] stay EVEX_0F38_PMOV.
     */
#endif /* __Use_Original_Qemu (U235) */
#if __Use_Original_Qemu != 1 /* ours (U236) */
    /* VRCP14PS/PD (4C), VRSQRT14PS/PD (4E): E4, no {er}/{sae}; SS/SD (4D/4F): E5 */
    [0x4c] = X86_OP_ENTRY3(EVEX_VRCP14, V,x, None,None, W,x, EV_FPX(NONE, EV_NOFP)),
    [0x4d] = X86_OP_GROUP0(EVEX_0F38_4D),
    [0x4e] = X86_OP_ENTRY3(EVEX_VRSQRT14, V,x, None,None, W,x, EV_FPX(NONE, EV_NOFP)),
    [0x4f] = X86_OP_GROUP0(EVEX_0F38_4F),
#endif /* __Use_Original_Qemu (U236) */
#if __Use_Original_Qemu != 1 /* ours (U237) */
    /* VGETEXPPS/PD (42), VGETEXPSS/SD (43): {sae}; E2 / E3 */
    [0x42] = X86_OP_ENTRY3(EVEX_VGETEXP, V,x, None,None, W,x, EV_FPX(SAE, ev_fp)),
    [0x43] = X86_OP_GROUP0(EVEX_0F38_43),
#endif /* __Use_Original_Qemu (U237) */
#if __Use_Original_Qemu != 1 /* ours (U239) */
    /* VSCALEFPS/PD (2C), VSCALEFSS/SD (2D): {er}; E2 / E3 */
    [0x2c] = X86_OP_ENTRY3(EVEX_VSCALEF, V,x, H,x, W,x, EV_FPX(ER, ev_fp)),
    [0x2d] = X86_OP_GROUP0(EVEX_0F38_2D),
#endif /* __Use_Original_Qemu (U239) */
#if __Use_Original_Qemu != 1 /* ours (U402) */
    [0x74] = X86_OP_GROUP0(EVEX_A10_FP8),
#endif /* __Use_Original_Qemu (U402) */
#if __Use_Original_Qemu != 1 /* ours (U404) */
    /*
     * NoVmp (ledger U404): VCVT2PS2PHX (spec 9.2), "AVX10.2 OR AVX10_V1_AUX", 66 W0, Full
     * tuple (m32bcst), {k1}{z} per FP16 result, {er}; MXCSR flags as if masked, never #XM, no
     * memory fault suppression: gen_avx10b_cvt2ps2phx does the masking and the MXCSR update.
     */
    [0x67] = X86_OP_ENTRY3(EVEX_VCVT2PS2PHX, V,x, H,x, W,x, cpuid(AVX10_V1_AUX)
                           ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_rc(ER) ev_w(W0) ev_es(32)
                           .evex_nofs = 1, p_66),
#endif /* __Use_Original_Qemu (U404) */
#if __Use_Original_Qemu != 1 /* ours (U405) */
    [0x50] = X86_OP_GROUP0(EVEX_0F38_50),
    [0x51] = X86_OP_GROUP0(EVEX_0F38_51),
#endif /* __Use_Original_Qemu (U405) */
#if __Use_Original_Qemu != 1 /* ours (U406) */
    [0xd2] = X86_OP_GROUP0(EVEX_0F38_D2),
    [0xd3] = X86_OP_GROUP0(EVEX_0F38_D3),
#endif /* __Use_Original_Qemu (U406) */
#if __Use_Original_Qemu != 1 /* ours (U407) */
    [0x52] = X86_OP_GROUP0(EVEX_0F38_52),
#endif /* __Use_Original_Qemu (U407) */
#if __Use_Original_Qemu != 1 /* ours (U412) */
    [0xda] = X86_OP_GROUP0(EVEX_0F38_DA),
#endif /* __Use_Original_Qemu (U412) */
#if __Use_Original_Qemu != 1 /* ours (U571) */
    /*
     * NoVmp (ledger U571): VP2INTERSECTD (W0) / VP2INTERSECTQ (W1) k1+1, H, W/mbcst (SDM Vol2C
     * VP2INTERSECTD/VP2INTERSECTQ): EVEX.F2.0F38 68, Full tuple ({1toN}), "EVEX.aaa and
     * EVEX.z must be zero" (no masking), class E4NF. ModRM.reg names the pair k(reg & ~1),
     * k(reg | 1) (gen_EVEX_VP2INTERSECT); an opmask destination, so EVEX.R/R' must be 0.
     */
    [0x68] = X86_OP_ENTRY3(EVEX_VP2INTERSECT, K,q, H,x, W,x, cpuid(AVX512_VP2INTERSECT)
                           ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(NONE) ev_w(WSEL) ev_es(W)
                           ev_nf p_f2),
#endif /* __Use_Original_Qemu (U571) */
#if __Use_Original_Qemu != 1 /* ours (U572) */
    /*
     * NoVmp (ledger U572): VGF2P8MULB zmm1{k1}{z}, zmm2, zmm3/m (SDM Vol2A 3-431):
     * EVEX.66.0F38.W0 CF, Full Mem tuple (no broadcast), byte masking, class E4 (memory fault
     * suppression per byte). The U70 ops_sse.h helper of the legacy/VEX forms (SHIFT 1/2/3).
     */
    [0xcf] = X86_OP_ENTRY3(EVEX_VGF2P8MULB, V,x, H,x, W,x, cpuid(EVEX_GFNI) ev_vl(EV_VLALL)
                           ev_tt(FULL_MEM) ev_mask(MZ) ev_w(W0) ev_es(8) p_66),
#endif /* __Use_Original_Qemu (U572) */
#if __Use_Original_Qemu != 1 /* ours (U573) */
    /*
     * NoVmp (ledger U573): VAESENC (DC), VAESENCLAST (DD), VAESDEC (DE), VAESDECLAST (DF)
     * zmm1, zmm2, zmm3/m (SDM Vol2A 3-33 .. 3-52): EVEX.66.0F38.WIG, one AES round per 128-bit
     * lane, Full Mem tuple, no masking (aaa / z #UD), class E4NF. The ops_sse.h AES-NI helpers
     * of the legacy/VEX forms (SHIFT 1/2/3).
     */
#define EV_VAES(op) X86_OP_ENTRY3(op, V,x, H,x, W,x, cpuid(EVEX_VAES) ev_vl(EV_VLALL)                         ev_tt(FULL_MEM) ev_mask(NONE) ev_w(WIG) ev_es(32) ev_nf p_66)
    [0xdc] = EV_VAES(EVEX_VAESENC),
    [0xdd] = EV_VAES(EVEX_VAESENCLAST),
    [0xde] = EV_VAES(EVEX_VAESDEC),
    [0xdf] = EV_VAES(EVEX_VAESDECLAST),
#undef EV_VAES
#endif /* __Use_Original_Qemu (U573) */
#if __Use_Original_Qemu != 1 /* ours (U551) */
    [0x62] = X86_OP_GROUP0(EVEX_0F38_62),   /* VPEXPANDB/W (U551) */
    [0x63] = X86_OP_GROUP0(EVEX_0F38_63),   /* VPCOMPRESSB/W (U551) */
#endif /* __Use_Original_Qemu (U551) */
#if __Use_Original_Qemu != 1 /* ours (U553) */
    /*
     * NoVmp (ledger U553): VPSHLDVW (70 W1), VPSHLDVD/Q (71), VPSHRDVD/Q (73) zmm1{k1}{z},
     * zmm2, zmm3/m: the shift counts come from SRC3, DEST is the first half of the
     * concatenation (ev_dsrc); 72 66 W1 VPSHRDVW: decode_EVEX_0F38_72 (with the BF16 forms).
     */
    [0x70] = X86_OP_ENTRY3(EVEX_VPSHLDV, V,x, H,x, W,x, EV_VBMI2(FULL_MEM) ev_w(W1) ev_es(16)
                           ev_dsrc p_66),
    [0x71] = X86_OP_ENTRY3(EVEX_VPSHLDV, V,x, H,x, W,x, EV_VBMI2(FULL) ev_w(WSEL) ev_es(W)
                           ev_dsrc p_66),
    [0x73] = X86_OP_ENTRY3(EVEX_VPSHRDV, V,x, H,x, W,x, EV_VBMI2(FULL) ev_w(WSEL) ev_es(W)
                           ev_dsrc p_66),
#endif /* __Use_Original_Qemu (U553) */
#if __Use_Original_Qemu != 1 /* ours (U556) */
    [0x72] = X86_OP_GROUP0(EVEX_0F38_72),   /* 66 VPSHRDVW (U553), F3/F2 BF16 (U556) */
#endif /* __Use_Original_Qemu (U556) */
#if __Use_Original_Qemu != 1 /* ours (U554) */
    /*
     * NoVmp (ledger U554): VPMULTISHIFTQB zmm1{k1}{z}, zmm2, zmm3/m/m64bcst (SDM Vol2C),
     * "AVX512_VBMI OR AVX10.1", EVEX.66.0F38.W1 83, Full tuple: {1to8} and disp8*N by qwords
     * (evex_es 64), one opmask bit per destination byte (evex_ds 8, U230), class E4NF.
     */
    [0x83] = X86_OP_ENTRY3(EVEX_VPMULTISHIFTQB, V,x, H,x, W,x, cpuid(AVX512_VBMI)
                           ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W1) ev_es(64) ev_ds(8)
                           ev_nf p_66),
#endif /* __Use_Original_Qemu (U554) */
#if __Use_Original_Qemu != 1 /* ours (U555) */
    /* 53: 66 VPDPWSSDS (F2 = VP4DPWSSDS, AVX512_4VNNIW, not implemented: #UD) */
    [0x53] = X86_OP_ENTRY3(EVEX_VPDPWSSDS, V,x, H,x, W,x, EV_VNNI512 p_66),
#endif /* __Use_Original_Qemu (U555) */
};

static const X86OpEntry opcodes_evex_0F3A[256] = {
#if __Use_Original_Qemu != 1 /* ours (U152) */
    /* NoVmp (ledger U152): VPCMPUD/UQ (1E), VPCMPD/Q (1F) k1{k2}, H, W/mbcst, imm8; E4 */
    [0x1e] = X86_OP_ENTRY4(EVEX_VPCMPU, K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(KDEST) ev_w(WSEL) ev_es(W) p_66),
    [0x1f] = X86_OP_ENTRY4(EVEX_VPCMP, K,q, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(KDEST) ev_w(WSEL) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U152) */
#if __Use_Original_Qemu != 1 /* ours (U159) */
    /* NoVmp (ledger U159): VPTERNLOGD/Q (66 25 ib, W0/W1): Full, {k1}{z}, {1toN}; E4 */
    [0x25] = X86_OP_ENTRY4(EVEX_VPTERNLOG, V,x, H,x, W,x, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(MZ) ev_w(WSEL) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U159) */
#if __Use_Original_Qemu != 1 /* ours (U211) */
    /*
     * NoVmp (ledger U211): VPERMQ/VPERMPD imm8 (00/01 W1, EVEX.256/512), VALIGND/Q (03),
     * VPERMILPS/PD imm8 (04 W0 / 05 W1), VSHUFF32X4/F64X2 (23) and VSHUFI32X4/I64X2 (43)
     * EVEX.256/512; all 66, Full, E4NF.
     */
    [0x00] = EV_PRM2I(EVEX_VPERMQ_I, EV_VL256UP, W1, p_66),
    [0x01] = EV_PRM2I(EVEX_VPERMQ_I, EV_VL256UP, W1, p_66),
    [0x03] = EV_PRM3I(EVEX_VALIGN, EV_VLALL, WSEL, p_66),
    [0x04] = EV_PRM2I(EVEX_VPSHUFD, EV_VLALL, W0, p_66),
    [0x05] = EV_PRM2I(EVEX_VPERMILPD_I, EV_VLALL, W1, p_66),
    [0x23] = EV_PRM3I(EVEX_VSHUF128, EV_VL256UP, WSEL, p_66),
    [0x43] = EV_PRM3I(EVEX_VSHUF128, EV_VL256UP, WSEL, p_66),
#endif /* __Use_Original_Qemu (U211) */
#if __Use_Original_Qemu != 1 /* ours (U215) */
    /*
     * NoVmp (ledger U215): VPEXTRD/Q (16, AVX512DQ, W ignored outside 64-bit mode),
     * VEXTRACTPS (17, WIG), VINSERTPS (21, W0), VPINSRD/Q (22, AVX512DQ): EVEX.128, no
     * masking, Tuple1 Scalar, E9NF; VINSERTx / VEXTRACTx by EVEX.W (18/19/1A/1B, 38/39/3A/3B).
     */
    [0x16] = X86_OP_ENTRY4(EVEX_VPEXTR, E,y, None,None, V,dq, cpuid(AVX512DQ)
                           ev_vl(X86_EVEX_VL128) ev_tt(T1S) ev_mask(NONE) ev_w(WSEL) ev_es(W)
                           ev_w32 p_66),
    [0x17] = X86_OP_ENTRY4(EVEX_VPEXTR, E,d, None,None, V,dq, cpuid(AVX512F)
                           ev_vl(X86_EVEX_VL128) ev_tt(T1S) ev_mask(NONE) ev_w(WIG) ev_es(32)
                           p_66),
    [0x18] = X86_OP_GROUP0(EVEX_0F3A_18),
    [0x19] = X86_OP_GROUP0(EVEX_0F3A_19),
    [0x1a] = X86_OP_GROUP0(EVEX_0F3A_1A),
    [0x1b] = X86_OP_GROUP0(EVEX_0F3A_1B),
    [0x21] = X86_OP_ENTRY4(EVEX_VINSERTPS, V,dq, H,dq, W,d, cpuid(AVX512F)
                           ev_vl(X86_EVEX_VL128) ev_tt(T1S) ev_mask(NONE) ev_w(W0) ev_es(W)
                           p_66),
    [0x22] = X86_OP_ENTRY4(EVEX_VPINSR, V,dq, H,dq, E,y, cpuid(AVX512DQ)
                           ev_vl(X86_EVEX_VL128) ev_tt(T1S) ev_mask(NONE) ev_w(WSEL) ev_es(W)
                           ev_w32 p_66),
    [0x38] = X86_OP_GROUP0(EVEX_0F3A_18),
    [0x39] = X86_OP_GROUP0(EVEX_0F3A_19),
    [0x3a] = X86_OP_GROUP0(EVEX_0F3A_1A),
    [0x3b] = X86_OP_GROUP0(EVEX_0F3A_1B),
#endif /* __Use_Original_Qemu (U215) */
#if __Use_Original_Qemu != 1 /* ours (U263) */
    /* NoVmp (ledger U263): VDBPSADBW (66.0F3A.W0 42 ib): Full Mem, word writemask, E4NF;
       U408: decode_EVEX_0F3A_42 also has F3 VMPSADBW (AVX10.2) */
    [0x42] = X86_OP_GROUP0(EVEX_0F3A_42),
#endif /* __Use_Original_Qemu (U263) */
#if __Use_Original_Qemu != 1 /* ours (U264) */
    /* NoVmp (ledger U264): VPALIGNR (66.0F3A.WIG 0F ib): Full Mem, byte writemask, E4NF */
    [0x0f] = X86_OP_ENTRY4(EVEX_PALIGNR, V,x, H,x, W,x, EV_BW(WIG, 8) ev_nf p_66),
#endif /* __Use_Original_Qemu (U264) */
#if __Use_Original_Qemu != 1 /* ours (U266) */
    [0x3e] = X86_OP_GROUP0(EVEX_0F3A_3E),   /* VPCMPUB/UW k1{k2}, ..., imm8 */
    [0x3f] = X86_OP_GROUP0(EVEX_0F3A_3F),   /* VPCMPB/W k1{k2}, ..., imm8 */
#endif /* __Use_Original_Qemu (U266) */
#if __Use_Original_Qemu != 1 /* ours (U267) */
    /*
     * NoVmp (ledger U267): VPEXTRB reg/m8 (14), VPEXTRW reg/m16 (15), VPINSRB xmm1, xmm2,
     * r32/m8 (20): EVEX.128.66.0F3A.WIG, E9NF, Tuple1 Scalar; a GPR destination is
     * zero-extended
     */
    [0x14] = X86_OP_ENTRY3(PEXTRB, E,b, V,dq, I,b, cpuid(AVX512BW) ev_vl(X86_EVEX_VL128)
                           ev_tt(T1S) ev_mask(NONE) ev_w(WIG) ev_es(8) ev_nf zext0 p_66),
    [0x15] = X86_OP_ENTRY3(PEXTRW, E,w, V,dq, I,b, cpuid(AVX512BW) ev_vl(X86_EVEX_VL128)
                           ev_tt(T1S) ev_mask(NONE) ev_w(WIG) ev_es(16) ev_nf zext0 p_66),
    [0x20] = X86_OP_ENTRY4(PINSRB, V,dq, H,dq, E,b, cpuid(AVX512BW) ev_vl(X86_EVEX_VL128)
                           ev_tt(T1S) ev_mask(NONE) ev_w(WIG) ev_es(8) ev_nf zext2 p_66),
#endif /* __Use_Original_Qemu (U267) */
#if __Use_Original_Qemu != 1 /* ours (U292) */
    /* VFPCLASSPS/PD k2 {k1}, x/m/{1toN}, imm8 (66 W0/W1): Full, E4; 67: SS/SD (U292) */
    /* U336: one decoder per slot - NP VFPCLASSPH / VFPCLASSSH (AVX512-FP16), 66 PS/PD/SS/SD */
    [0x66] = X86_OP_GROUP0(EVEX_0F3A_66),
    [0x67] = X86_OP_GROUP0(EVEX_0F3A_67),
#endif /* __Use_Original_Qemu (U292) */
#if __Use_Original_Qemu != 1 /* ours (U336) */
    /* NoVmp (ledger U336): AVX512-FP16 VCMPPH (NP) / VCMPSH (F3) k1{k2}, ..., imm8 {sae} */
    [0xc2] = X86_OP_GROUP0(FP16_0F3A_C2),
#endif /* __Use_Original_Qemu (U336) */
#if __Use_Original_Qemu != 1 /* ours (U293) */
    /*
     * NoVmp (ledger U293): VRANGEPS/PD (66 50 W0/W1) x1 {k1}{z}, x2, x3/m/{1toN}{sae}, imm8:
     * Full tuple, AVX512DQ; E2 (IE, DE). 51: VRANGESS/SD (U295).
     */
    [0x50] = X86_OP_ENTRY4(EVEX_VRANGE, V,x, H,x, W,x, cpuid(AVX512DQ) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(MZ) ev_rc(SAE) ev_w(WSEL) ev_es(W) ev_fp p_66),
#endif /* __Use_Original_Qemu (U293) */
#if __Use_Original_Qemu != 1 /* ours (U295) */
    [0x51] = X86_OP_GROUP0(EVEX_0F3A_51),
#endif /* __Use_Original_Qemu (U295) */
#if __Use_Original_Qemu != 1 /* ours (U294) */
    /*
     * NoVmp (ledger U294): VREDUCEPS/PD (66 56 W0/W1) x1 {k1}{z}, x2/m/{1toN}{sae}, imm8:
     * Full tuple, AVX512DQ; E2 (IE, PE); vvvv must be 1111b. 57: VREDUCESS/SD (U296).
     */
    [0x56] = X86_OP_GROUP0(EVEX_0F3A_56),   /* + NP VREDUCEPH (U337) */
#endif /* __Use_Original_Qemu (U294) */
#if __Use_Original_Qemu != 1 /* ours (U296) */
    [0x57] = X86_OP_GROUP0(EVEX_0F3A_57),   /* + NP VREDUCESH (U337) */
#endif /* __Use_Original_Qemu (U296) */
#if __Use_Original_Qemu != 1 /* ours (U231) */
    [0x1d] = X86_OP_GROUP0(EVEX_0F3A_1D),
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U238) */
    /* VGETMANTPS/PD (26 ib), VGETMANTSS/SD (27 ib): {sae}; E2 / E3 */
    [0x26] = X86_OP_GROUP0(EVEX_0F3A_26),   /* + NP VGETMANTPH (U337) */
    [0x27] = X86_OP_GROUP0(EVEX_0F3A_27),   /* + NP VGETMANTSH (U337) */
#endif /* __Use_Original_Qemu (U238) */
#if __Use_Original_Qemu != 1 /* ours (U240) */
    /* VFIXUPIMMPS/PD (54 ib), VFIXUPIMMSS/SD (55 ib): DEST is read; {sae}; E2 / E3 */
    [0x54] = X86_OP_ENTRY4(EVEX_VFIXUPIMM, V,x, H,x, W,x, EV_FPX(SAE, EV_NOFP)),
    [0x55] = X86_OP_GROUP0(EVEX_0F3A_55),
#endif /* __Use_Original_Qemu (U240) */
#if __Use_Original_Qemu != 1 /* ours (U241) */
    /* VRNDSCALEPS (08 W0) / PD (09 W1) ib, VRNDSCALESS (0A W0) / SD (0B W1) ib: {sae}; E2/E3 */
    [0x08] = X86_OP_GROUP0(EVEX_0F3A_08),   /* + NP VRNDSCALEPH (U337) */
    [0x09] = X86_OP_ENTRY3(EVEX_VRNDSCALE, V,x, W,x, I,b, cpuid(AVX512F) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(MZ) ev_rc(SAE) ev_w(W1) ev_es(W) ev_fp p_66),
    [0x0a] = X86_OP_GROUP0(EVEX_0F3A_0A),   /* + NP VRNDSCALESH (U337) */
    [0x0b] = X86_OP_ENTRY4(EVEX_VRNDSCALE, V,x, H,x, W,sd, EV_FPS(W1, SAE, ev_fp)),
#endif /* __Use_Original_Qemu (U241) */
#if __Use_Original_Qemu != 1 /* ours (U572) */
    /*
     * NoVmp (ledger U572): VGF2P8AFFINEQB (CE) / VGF2P8AFFINEINVQB (CF) zmm1{k1}{z}, zmm2,
     * zmm3/m/m64bcst, imm8 (SDM Vol2A 3-426, 3-429): EVEX.66.0F3A.W1, Full tuple with qword
     * {1toN} and disp8*N (evex_es 64), byte masking (evex_ds 8: KL = VL bytes), class E4NF
     * (the whole memory operand is read whatever the mask). U70 ops_sse.h helpers.
     */
    [0xce] = X86_OP_ENTRY4(EVEX_VGF2P8AFFINEQB, V,x, H,x, W,x, cpuid(EVEX_GFNI) ev_vl(EV_VLALL)
                           ev_tt(FULL) ev_mask(MZ) ev_w(W1) ev_es(64) ev_ds(8) ev_nf p_66),
    [0xcf] = X86_OP_ENTRY4(EVEX_VGF2P8AFFINEINVQB, V,x, H,x, W,x, cpuid(EVEX_GFNI)
                           ev_vl(EV_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W1) ev_es(64) ev_ds(8)
                           ev_nf p_66),
#endif /* __Use_Original_Qemu (U572) */
#if __Use_Original_Qemu != 1 /* ours (U574) */
    /*
     * NoVmp (ledger U574): VPCLMULQDQ zmm1, zmm2, zmm3/m, imm8 (SDM Vol2B 4-242):
     * EVEX.66.0F3A.WIG 44, one carry-less multiply per 128-bit lane (imm8[0] selects the
     * SRC1 qword, imm8[4] the SRC2 qword), Full Mem tuple, no masking, class E4NF. The
     * ops_sse.h helper of the legacy/VEX forms (SHIFT 1/2/3).
     */
    [0x44] = X86_OP_ENTRY4(EVEX_VPCLMULQDQ, V,x, H,x, W,x, cpuid(EVEX_VPCLMULQDQ)
                           ev_vl(EV_VLALL) ev_tt(FULL_MEM) ev_mask(NONE) ev_w(WIG) ev_es(64)
                           ev_nf p_66),
#endif /* __Use_Original_Qemu (U574) */
#if __Use_Original_Qemu != 1 /* ours (U552) */
    /*
     * NoVmp (ledger U552): VPSHLDW (70 W1), VPSHLDD/Q (71), VPSHRDW (72 W1), VPSHRDD/Q (73)
     * zmm1{k1}{z}, zmm2, zmm3/m, imm8 (AVX512_VBMI2): the count is imm8 modulo the element
     * width; Full Mem tuple for the word forms, Full ({1toN}) for D/Q.
     */
    [0x70] = X86_OP_ENTRY4(EVEX_VPSHLD, V,x, H,x, W,x, EV_VBMI2(FULL_MEM) ev_w(W1) ev_es(16)
                           p_66),
    [0x71] = X86_OP_ENTRY4(EVEX_VPSHLD, V,x, H,x, W,x, EV_VBMI2(FULL) ev_w(WSEL) ev_es(W) p_66),
    [0x72] = X86_OP_ENTRY4(EVEX_VPSHRD, V,x, H,x, W,x, EV_VBMI2(FULL_MEM) ev_w(W1) ev_es(16)
                           p_66),
    [0x73] = X86_OP_ENTRY4(EVEX_VPSHRD, V,x, H,x, W,x, EV_VBMI2(FULL) ev_w(WSEL) ev_es(W) p_66),
#endif /* __Use_Original_Qemu (U552) */
};
#if __Use_Original_Qemu != 1 /* ours (U321) */
/*
 * NoVmp (ledger U321): EVEX.F3.0F38 forms whose opcode byte also holds a 66 form in
 * opcodes_evex_0F38 (2A: VMOVNTDQA, 3A: VPMINUW); decode_evex_0F38 takes them first.
 * VPBROADCASTMB2Q zmm1, k1 (W1 2A) / VPBROADCASTMW2D zmm1, k1 (W0 3A), AVX512CD: no
 * masking (Table 2-42 note 1: aaa != 000b #UD), k1 in ModRM.r/m (mod != 11b #UD, EVEX.B/X
 * ignored), EVEX.vvvv reserved, EVEX.b #UD; class E6NF.
 */
static const X86OpEntry opcodes_evex_0F38_F3[256] = {
    [0x2a] = X86_OP_ENTRY3(EVEX_VPBROADCASTM, V,x, None,None, KU,b, cpuid(AVX512CD)
                           ev_vl(EV_VLALL) ev_mask(NONE) ev_w(W1) ev_es(64) p_f3),
    [0x3a] = X86_OP_ENTRY3(EVEX_VPBROADCASTM, V,x, None,None, KU,w, cpuid(AVX512CD)
                           ev_vl(EV_VLALL) ev_mask(NONE) ev_w(W0) ev_es(32) p_f3),
};
#endif /* __Use_Original_Qemu (U321) */
#if __Use_Original_Qemu != 1 /* ours (U325) */
/*
 * NoVmp (ledger U325): EVEX.66.0F38.W0 forms whose opcode byte holds an AVX512BW W1 form in
 * opcodes_evex_0F38 (75 VPERMI2W, 7D VPERMT2W, 8D VPERMW); decode_evex_0F38 takes them
 * first. AVX512_VBMI (SDM Vol2C VPERMB, VPERMI2B, VPERMT2B): zmm1{k1}{z}, zmm2 (vvvv),
 * zmm3/m, Full Mem tuple (no {1toN}), byte masking, class E4NF.nb (the whole memory operand
 * is read). VPERMI2B: index = DEST, tables SRC1/SRC2; VPERMT2B: index = SRC1, tables
 * DEST/SRC2; the table-select bit is bit 4/5/6 (VL 128/256/512) of the index byte.
 */
#define EV_VBMI(op) X86_OP_ENTRY3(op, V,x, H,x, W,x, cpuid(AVX512_VBMI) ev_vl(EV_VLALL) \
                        ev_tt(FULL_MEM) ev_mask(MZ) ev_w(W0) ev_es(8) .evex_nofs = 1, p_66)
static const X86OpEntry opcodes_evex_0F38_66W0[256] = {
    [0x75] = EV_VBMI(EVEX_VPERMI2B),
    [0x7d] = EV_VBMI(EVEX_VPERMT2B),
    [0x8d] = EV_VBMI(EVEX_VPERMB),
};
#undef EV_VBMI
#endif /* __Use_Original_Qemu (U325) */
#if __Use_Original_Qemu != 1 /* ours (U550) */
#undef EV_VBMI2
#endif /* __Use_Original_Qemu (U550) */
#if __Use_Original_Qemu != 1 /* ours (U555) */
#undef EV_VNNI512
#endif /* __Use_Original_Qemu (U555) */
#if __Use_Original_Qemu != 1 /* ours (U372) */
#include "decode-avx10.c.inc"
#endif /* __Use_Original_Qemu (U372) */
#if __Use_Original_Qemu != 1 /* ours (U149) */
#undef EV_INT
#endif /* __Use_Original_Qemu (U149) */
#if __Use_Original_Qemu != 1 /* ours (U151) */
#undef EV_FP3
#if __Use_Original_Qemu != 1 /* ours (U194) */
#undef EV_FPSS
#undef EV_FPSD
#endif /* __Use_Original_Qemu (U194) */
#endif /* __Use_Original_Qemu (U151) */
#if __Use_Original_Qemu != 1 /* ours (U156) */
#undef EV_FPR
#endif /* __Use_Original_Qemu (U156) */
#if __Use_Original_Qemu != 1 /* ours (U158) */
#undef EV_SHI
#endif /* __Use_Original_Qemu (U158) */
#if __Use_Original_Qemu != 1 /* ours (U261) */
#undef EV_BW
#endif /* __Use_Original_Qemu (U261) */
#if __Use_Original_Qemu != 1 /* ours (U215) */
#undef EV_VL512
#undef EV_EXT
#undef EV_INS
#undef EV_BCX
#undef EV_CHUNK
#endif /* __Use_Original_Qemu (U215) */
#if __Use_Original_Qemu != 1 /* ours (U214) */
#undef EV_NT
#undef EV_DUP
#undef EV_M128
#endif /* __Use_Original_Qemu (U214) */
#if __Use_Original_Qemu != 1 /* ours (U211) */
#undef EV_PRM2I
#undef EV_PRM3I
#undef EV_PRM3
#undef EV_PRM
#undef EV_VL256UP
#endif /* __Use_Original_Qemu (U211) */
#if __Use_Original_Qemu != 1 /* ours (U266) */
#undef EV_BWK
#endif /* __Use_Original_Qemu (U266) */
#if __Use_Original_Qemu != 1 /* ours (U232) */
#undef EV_SI2F
#undef EV_F2SI
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U233) */
#undef EV_CVTQ
#endif /* __Use_Original_Qemu (U233) */
#if __Use_Original_Qemu != 1 /* ours (U231) */
#undef EV_CVT
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U234) */
#undef EV_SHX
#endif /* __Use_Original_Qemu (U234) */
#if __Use_Original_Qemu != 1 /* ours (U236) */
#undef EV_FPX
#undef EV_FPS
#undef EV_NOFP
#undef EV_SCALAR_W
#endif /* __Use_Original_Qemu (U236) */
#if __Use_Original_Qemu != 1 /* ours (U148) */
#undef EV_MOV
#undef EV_VLALL
#undef ev_vl
#undef ev_tt
#undef ev_mask
#undef ev_rc
#undef ev_w
#undef ev_es
#undef ev_e1
#undef ev_fp
#undef ev_w32
#if __Use_Original_Qemu != 1 /* ours (U193) */
#undef ev_nf
#endif /* __Use_Original_Qemu (U193) */
#if __Use_Original_Qemu != 1 /* ours (U190) */
#undef ev_dsrc
#endif /* __Use_Original_Qemu (U190) */
#if __Use_Original_Qemu != 1 /* ours (U191) */
#undef ev_msrc1
#endif /* __Use_Original_Qemu (U191) */
#if __Use_Original_Qemu != 1 /* ours (U230) */
#undef ev_ds
#endif /* __Use_Original_Qemu (U230) */
#endif /* __Use_Original_Qemu (U148) */

static void decode_evex_0F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    *entry = opcodes_evex_0F[*b];
#if __Use_Original_Qemu != 1 /* ours (U372) */
    decode_avx10_override(s, env, entry, b, opcodes_avx10_0F);
#endif /* __Use_Original_Qemu (U372) */
}

static void decode_evex_0F38(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
#if __Use_Original_Qemu != 1 /* ours (U321) */
    if ((s->prefix & PREFIX_REPZ) && opcodes_evex_0F38_F3[*b].gen) {
        *entry = opcodes_evex_0F38_F3[*b];
        return;
    }
#endif /* __Use_Original_Qemu (U321) */
#if __Use_Original_Qemu != 1 /* ours (U325) */
    if ((s->prefix & PREFIX_DATA) && !s->vex_w && opcodes_evex_0F38_66W0[*b].gen) {
        *entry = opcodes_evex_0F38_66W0[*b];
        return;
    }
#endif /* __Use_Original_Qemu (U325) */
    *entry = opcodes_evex_0F38[*b];
}

static void decode_evex_0F3A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    *entry = opcodes_evex_0F3A[*b];
#if __Use_Original_Qemu != 1 /* ours (U372) */
    decode_avx10_override(s, env, entry, b, opcodes_avx10_0F3A);
#endif /* __Use_Original_Qemu (U372) */
}
#if __Use_Original_Qemu != 1 /* ours (U372) */

/*
 * NoVmp (ledgers U333, U372): EVEX maps 5 and 6 (EVEX.mmm = 101b/110b, SDM Vol2A 2.7.1,
 * Table 2-40): the AVX512-FP16 tables opcodes_evex_map5/6 (decode_fp16.c.inc), then the
 * AVX10.2 forms of the same slot and prefix (decode_avx10_override). Empty slots #UD.
 */
static void decode_evex_map5(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    *entry = opcodes_evex_map5[*b];
    decode_avx10_override(s, env, entry, b, opcodes_avx10_map5);
}

static void decode_evex_map6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    *entry = opcodes_evex_map6[*b];
    decode_avx10_override(s, env, entry, b, opcodes_avx10_map6);
}
#endif /* __Use_Original_Qemu (U372) */
#endif /* __Use_Original_Qemu (U141) */

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


#if __Use_Original_Qemu != 1 /* ours (U144) */
/*
 * NoVmp (ledger U144): compressed displacement disp8*N of an EVEX memory operand (SDM
 * Vol2A 2.7.5, Tables 2-36/2-37; also with 16-bit addressing). N from the tuple type, the
 * vector length, the element size (EVEX.W / the form) and EVEX.b (embedded broadcast);
 * Tuple1 Scalar/Fixed use the size of the memory operand itself.
 */
static int evex_disp8_scale(DisasContext *s, X86OpEntry *e, X86DecodedOp *op)
{
    int vl = s->evex_vl, esz = 1 << s->evex_esz;

#if __Use_Original_Qemu != 1 /* ours (U645) */
    /* "When a promoted legacy or VEX instruction has a memory operand with an 8b
       displacement (disp8), its scaling factor N is always 1" (APX spec 3.1.2.3) */
    if (s->apx_promoted) {
        return 1;
    }
#endif /* __Use_Original_Qemu (U645) */
    switch (e->evex_tt) {
    case X86_EVEX_TT_FULL:
        return s->evex_b ? esz : vl;
    case X86_EVEX_TT_HALF:
        return s->evex_b ? esz : vl / 2;
    case X86_EVEX_TT_FULL_MEM:
        return vl;
    case X86_EVEX_TT_T1S:
    case X86_EVEX_TT_T1F:
        return op->ot <= MO_64 ? 1 << op->ot : esz;
    case X86_EVEX_TT_T2:
        return 2 * esz;
    case X86_EVEX_TT_T4:
        return 4 * esz;
    case X86_EVEX_TT_T8:
        return 8 * esz;
    case X86_EVEX_TT_HALF_MEM:
        return vl / 2;
    case X86_EVEX_TT_QUARTER_MEM:
        return vl / 4;
    case X86_EVEX_TT_EIGHTH_MEM:
        return vl / 8;
    case X86_EVEX_TT_MEM128:
        return 16;
    case X86_EVEX_TT_MOVDDUP:
        return vl == 16 ? 8 : vl;
#if __Use_Original_Qemu != 1 /* ours (U332) */
    case X86_EVEX_TT_QUARTER:
        return s->evex_b ? esz : vl / 4;
#endif /* __Use_Original_Qemu (U332) */
    default:
        return 1;
    }
}
#endif /* __Use_Original_Qemu (U144) */

static int decode_modrm(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                        X86DecodedOp *op, X86OpType type)
{
    int modrm = get_modrm(s, env);
    if ((modrm >> 6) == 3) {
        op->n = (modrm & 7);
        /*
         * backport 45230bca85 (U613): no REX.B for an MMX register, whatever the operand type
         * (W/U with the mmx special also select MMX registers; REX.B made them MM8-MM15 and
         * read past fpregs[]: i5-13600K 41 0F 60 C1 = PUNPCKLBW MM0, MM1)
         */
#if __Use_Original_Qemu == 1 /* original QEMU (U615) */
        if (op->unit != X86_OP_MMX) {
            op->n |= REX_B(s);
        }
#else /* ours (U615) */
        /*
         * NoVmp (ledger U615): REX2.B4 (bit 4 of rex_b, U614) selects R16-R31 for a GPR only;
         * "REX2.R4, REX2.X4 or REX2.B4 are generally ignored" when the register is a vector
         * register (APX spec 355828-009 3.1.2.1: no XMM16-31 through REX2).
         */
        if (op->unit == X86_OP_SSE) {
            op->n |= REX_B(s) & 8;
        } else if (op->unit != X86_OP_MMX) {
            op->n |= REX_B(s);
        }
#endif /* __Use_Original_Qemu (U615) */
#if __Use_Original_Qemu != 1 /* ours (U142) */
        /* EVEX.X selects ZMM16-31 for a vector ModRM.r/m (SDM Vol2A Table 2-33; U142) */
        if ((s->prefix & PREFIX_EVEX) && op->unit == X86_OP_SSE) {
            op->n |= s->evex_x4;
        }
#endif /* __Use_Original_Qemu (U142) */
    } else {
        op->has_ea = true;
        op->n = -1;
#if __Use_Original_Qemu != 1 /* ours (U144) */
        if (s->prefix & PREFIX_EVEX) {
            s->evex_n = evex_disp8_scale(s, &decode->e, op);
        }
#endif /* __Use_Original_Qemu (U144) */
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
#if __Use_Original_Qemu != 1 /* ours (U210) */
        /*
         * NoVmp (ledger U210): an EVEX dq operand is 128 bits whatever the vector length
         * (VINSERTF32X4 xmm3/m128, VEXTRACTF32X4 xmm1/m128, VBROADCASTF32X4 m128; U260: the
         * xmm3/m128 count of VPSLLW/VPSRLW/VPSRAW, Mem128 tuple, same rule; U234: the same
         * count of VPSLLD/Q, VPSRLD/Q, VPSRAD/Q): the
         * allowed lengths are in the form's evex_vl (decode_evex_vl).
         */
        if (s->prefix & PREFIX_EVEX) {
            *ot = MO_128;
            return true;
        }
#endif /* __Use_Original_Qemu (U210) */
        if (s->vex_l && e->s0 != X86_SIZE_qq && e->s1 != X86_SIZE_qq) {
            return false;
        }
        *ot = MO_128;
        return true;

    case X86_SIZE_qq: /* AVX 256-bit */
#if __Use_Original_Qemu != 1 /* ours (U210) */
        /* NoVmp (ledger U210): EVEX qq (VINSERTF64X4 ymm3/m256, ...): 256 bits, see dq */
        if (s->prefix & PREFIX_EVEX) {
            *ot = MO_256;
            return true;
        }
#endif /* __Use_Original_Qemu (U210) */
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
#if __Use_Original_Qemu != 1 /* ours (U142) */
        /* EVEX: 128/256/512 bits from EVEX.L'L, or 512 with {er}/{sae} (decode_evex_vl, U142) */
        if (s->prefix & PREFIX_EVEX) {
            *ot = s->evex_vl == 64 ? MO_512 : s->evex_vl == 32 ? MO_256 : MO_128;
            return true;
        }
#endif /* __Use_Original_Qemu (U142) */
        *ot = s->vex_l ? MO_256 : MO_128;
        return true;

    case X86_SIZE_xh: /* SSE/AVX packed half register */
#if __Use_Original_Qemu != 1 /* ours (U142) */
        if (s->prefix & PREFIX_EVEX) {
            *ot = s->evex_vl == 64 ? MO_256 : s->evex_vl == 32 ? MO_128 : MO_64;
            return true;
        }
#endif /* __Use_Original_Qemu (U142) */
        *ot = s->vex_l ? MO_128 : MO_64;
        return true;

#if __Use_Original_Qemu != 1 /* ours (U210) */
    /*
     * NoVmp (ledger U210): EVEX Quarter Mem / Eighth Mem operands (VPMOVZXBD, VPMOVDB, ...):
     * VL/4 and VL/8 bits (SDM Vol2A Table 2-37); EVEX only.
     */
    case X86_SIZE_xq:
        if (!(s->prefix & PREFIX_EVEX)) {
            return false;
        }
        *ot = s->evex_vl == 64 ? MO_128 : s->evex_vl == 32 ? MO_64 : MO_32;
        return true;

    case X86_SIZE_xo:
        if (!(s->prefix & PREFIX_EVEX)) {
            return false;
        }
        *ot = s->evex_vl == 64 ? MO_64 : s->evex_vl == 32 ? MO_32 : MO_16;
        return true;
#endif /* __Use_Original_Qemu (U210) */

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
         * U95: in 64-bit mode the whole s->vex_v is used, so EVEX.V' (bit 4,
         * U141, registers 16-31) is kept; only outside 64-bit mode is it cut to 3 bits.
         */
        op->n = (CODE64(s) ? s->vex_v : (s->vex_v & 7));
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
#if __Use_Original_Qemu == 1 /* original QEMU (U615) */
        if (op->unit != X86_OP_MMX) {
            op->n |= REX_R(s);
        }
#else /* ours (U615) */
        /* REX2.R4 is ignored for a vector register, kept for a GPR/CR/DR (APX 3.1.2.1, U615) */
        if (op->unit == X86_OP_SSE) {
            op->n |= REX_R(s) & 8;
        } else if (op->unit != X86_OP_MMX) {
            op->n |= REX_R(s);
        }
#endif /* __Use_Original_Qemu (U615) */
#if __Use_Original_Qemu != 1 /* ours (U142) */
        /* EVEX.R' selects ZMM16-31 for a vector ModRM.reg (SDM Vol2A Table 2-33; U142) */
        if ((s->prefix & PREFIX_EVEX) && op->unit == X86_OP_SSE) {
            op->n |= s->evex_rr;
        }
        /*
         * U616: with APX_F, EVEX.R4 (the R' bit) is bit 4 of a GPR in ModRM.reg (APX spec
         * Table 3.3 "REG: EVEX.R4 EVEX.R3 modrm.reg ... GPR"); without APX validate_evex #UDs it.
         */
        if ((s->prefix & PREFIX_EVEX) && op->unit == X86_OP_INT &&
            (s->cpuid_7_1_edx_features & CPUID_7_1_EDX_APX_F)) {
            op->n |= s->evex_rr;
        }
#endif /* __Use_Original_Qemu (U142) */
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
            op->n = (CODE64(s) ? s->vex_v : (s->vex_v & 7));
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

#if __Use_Original_Qemu != 1 /* ours (U142) */
/*
 * NoVmp (ledger U142): EVEX.W, element size and vector length of an EVEX form, before
 * the operand sizes are computed (SDM Vol2A 2.7.10, Tables 2-38 and 2-43). false = #UD.
 * - EVEX.W must match the form (W0/W1); WSEL forms take the element size from it; forms
 *   whose W is ignored outside 64-bit mode behave as W0 there.
 * - EVEX.b with ModRM.mod = 11b: only {er}/{sae} forms; L'L is then the rounding control
 *   (or ignored for {sae}) and the vector length is 512 (scalar forms: 128).
 * - EVEX.b with a memory operand: only Full/Half tuple forms (embedded broadcast).
 * - Otherwise L'L = 11b is #UD and the form must exist at that length; LIG forms are 128.
 * vex_l keeps meaning "wider than 128 bits".
 */
static bool decode_evex_vl(DisasContext *s, CPUX86State *env, X86OpEntry *e)
{
    static const uint8_t es_ot[] = { MO_32, MO_8, MO_16, MO_32, MO_64 };
    bool reg;
    int vl;

    if (!e->gen) {
        return true;                    /* empty slot: #UD as unknown opcode */
    }
    if (!e->evex_vl) {
        return false;
    }
    reg = (get_modrm(s, env) >> 6) == 3;
    if (e->evex_w32 && !CODE64(s)) {
        s->vex_w = 0;
    }
    if ((e->evex_w == X86_EVEX_W0 && s->vex_w) || (e->evex_w == X86_EVEX_W1 && !s->vex_w)) {
        return false;
    }
    s->evex_esz = e->evex_es == X86_EVEX_ES_W ? (s->vex_w ? MO_64 : MO_32) : es_ot[e->evex_es];
#if __Use_Original_Qemu != 1 /* ours (U230) */
    /* destination / opmask element size (U230): differs from the source on conversions */
    s->evex_dsz = e->evex_ds ? es_ot[e->evex_ds] : s->evex_esz;
#endif /* __Use_Original_Qemu (U230) */

    if (s->evex_b && reg) {
        if (e->evex_rc == X86_EVEX_RC_NONE) {
            return false;
        }
        s->evex_rc = e->evex_rc == X86_EVEX_RC_ER ? s->evex_ll : -2;
        vl = (e->evex_vl & X86_EVEX_LIG) ? 16 : 64;
    } else {
#if __Use_Original_Qemu == 1 /* original QEMU (U332) */
        if (s->evex_b && e->evex_tt != X86_EVEX_TT_FULL && e->evex_tt != X86_EVEX_TT_HALF) {
            return false;
        }
#else /* ours (U332) */
        /* U332: also the FP16 Quarter tuple (m16bcst of VCVTPH2PD/QQ/UQQ) */
        if (s->evex_b && e->evex_tt != X86_EVEX_TT_FULL && e->evex_tt != X86_EVEX_TT_HALF &&
            e->evex_tt != X86_EVEX_TT_QUARTER) {
            return false;
        }
#endif /* __Use_Original_Qemu (U332) */
        if (e->evex_vl & X86_EVEX_LIG) {
            vl = 16;
        } else if (s->evex_ll == 3 || !(e->evex_vl & (1 << s->evex_ll))) {
            return false;
        } else {
            vl = 16 << s->evex_ll;
        }
    }
    s->evex_vl = vl;
    s->vex_l = vl > 16;
    return true;
}
#endif /* __Use_Original_Qemu (U142) */

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
#if __Use_Original_Qemu != 1 /* ours (U142) */
#if __Use_Original_Qemu == 1 /* original QEMU (U645) */
    if ((s->prefix & PREFIX_EVEX) && !decode_evex_vl(s, env, e)) {
        return false;
    }
#else /* ours (U645) */
    /* APX-promoted forms have no EVEX vector length / tuple (U645) */
    if ((s->prefix & PREFIX_EVEX) && !s->apx_promoted && !decode_evex_vl(s, env, e)) {
        return false;
    }
#endif /* __Use_Original_Qemu (U645) */
#endif /* __Use_Original_Qemu (U142) */

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
#if __Use_Original_Qemu != 1 /* ours (U321) */
    case X86_FEAT_AVX512CD:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512CD);
#endif /* __Use_Original_Qemu (U321) */
#if __Use_Original_Qemu != 1 /* ours (U322) */
    case X86_FEAT_AVX512_IFMA:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512IFMA);
#endif /* __Use_Original_Qemu (U322) */
#if __Use_Original_Qemu != 1 /* ours (U323) */
    case X86_FEAT_AVX512_VPOPCNTDQ:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_AVX512_VPOPCNTDQ);
#endif /* __Use_Original_Qemu (U323) */
#if __Use_Original_Qemu != 1 /* ours (U324) */
    case X86_FEAT_AVX512_BITALG:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_AVX512BITALG);
#endif /* __Use_Original_Qemu (U324) */
#if __Use_Original_Qemu != 1 /* ours (U325) */
    case X86_FEAT_AVX512_VBMI:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_AVX512_VBMI);
#endif /* __Use_Original_Qemu (U325) */
#if __Use_Original_Qemu != 1 /* ours (U330) */
    case X86_FEAT_AVX512_FP16:
        return (s->cpuid_7_0_edx_features & CPUID_7_0_EDX_AVX512_FP16);
#endif /* __Use_Original_Qemu (U330) */
#if __Use_Original_Qemu != 1 /* ours (U372) */
    case X86_FEAT_AVX10_2:
        /* AVX10.2 spec 361050-007 chapter 2: AVX10[19] and version >= 2 (U371) */
        return s->avx10_version >= 2;
#endif /* __Use_Original_Qemu (U372) */
#if __Use_Original_Qemu != 1 /* ours (U400) */
    case X86_FEAT_AVX10_V1_AUX:
        /* "AVX10.2 OR AVX10_V1_AUX": the U371 avx10_v1_aux copy (24H.1:ECX[2]) or version >= 2 */
        return s->avx10_v1_aux || s->avx10_version >= 2;
    case X86_FEAT_AVX10_MOVRS:
        return s->avx10_version >= 1 && (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_MOVRS);
#endif /* __Use_Original_Qemu (U400) */
#if __Use_Original_Qemu != 1 /* ours (U412) */
    case X86_FEAT_AVX10_SM4:
        /* "AVX10 AND SM4" (ISE 319433-062): AVX10 version >= 1 and CPUID.(07H,1):EAX.SM4[2] */
        return s->avx10_version >= 1 && (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_SM4);
#endif /* __Use_Original_Qemu (U412) */
#if __Use_Original_Qemu != 1 /* ours (U570) */
    case X86_FEAT_AVX512_VP2INTERSECT:
        /*
         * SDM Vol2C VP2INTERSECTD/Q: "AVX512F AVX512_VP2INTERSECT" (EVEX.512), "AVX512VL
         * AVX512_VP2INTERSECT" (EVEX.128/256, AVX512VL checked by validate_evex); no AVX10.1
         * alternative, so AVX10 alone does not add it (CPUID.(07H,0):EDX[8] is required).
         */
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512F) &&
               (s->cpuid_7_0_edx_features & CPUID_7_0_EDX_AVX512_VP2INTERSECT);
#endif /* __Use_Original_Qemu (U570) */
#if __Use_Original_Qemu != 1 /* ours (U572) */
    case X86_FEAT_EVEX_GFNI:
        /*
         * SDM Vol2A GF2P8AFFINEINVQB / GF2P8AFFINEQB / GF2P8MULB: "(AVX512F OR AVX10.1) GFNI",
         * "(AVX512VL OR AVX10.1) GFNI" below 512 bits (AVX512VL: validate_evex); AVX10.1 sets
         * the translator's AVX512F/VL copies (U371), GFNI keeps its own bit (AVX10.2 spec
         * 361050-007 3.1.2 Note).
         */
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512F) &&
               (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_GFNI);
#endif /* __Use_Original_Qemu (U572) */
#if __Use_Original_Qemu != 1 /* ours (U573) */
    case X86_FEAT_EVEX_VAES:
        /*
         * SDM Vol2A AESDEC/AESDECLAST/AESENC/AESENCLAST: "VAES (AVX512F OR AVX10.1)", "VAES
         * (AVX512VL OR AVX10.1)" for EVEX.128/256 (AVX512VL: validate_evex); unlike the VEX
         * forms the EVEX forms need neither the AES nor the AVX bit, and EVEX.128 needs VAES.
         */
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512F) &&
               (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_VAES);
#endif /* __Use_Original_Qemu (U573) */
#if __Use_Original_Qemu != 1 /* ours (U574) */
    case X86_FEAT_EVEX_VPCLMULQDQ:
        /*
         * SDM Vol2B PCLMULQDQ: "VPCLMULQDQ (AVX512F OR AVX10.1)", "(AVX512VL OR AVX10.1)" for
         * EVEX.128/256 (AVX512VL: validate_evex); EVEX.128 needs VPCLMULQDQ (the VEX.128 form
         * needs only PCLMULQDQ and AVX).
         */
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512F) &&
               (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_VPCLMULQDQ);
#endif /* __Use_Original_Qemu (U574) */
#if __Use_Original_Qemu != 1 /* ours (U550) */
    /*
     * NoVmp (ledgers U550, U555, U556): "AVX512_VBMI2 OR AVX10.1", "AVX512_VNNI OR AVX10.1",
     * "(AVX512_BF16 AND AVX512F) OR AVX10.1" (SDM Vol2C CPUID columns): the translator's
     * copies, which U371 sets for AVX10.1 (AVX10_1_7_0_ECX / AVX10_1_7_1_EAX); AVX512F is
     * implied by the UC_CTL_X86_AVX512 opt-in and checked by HF_AVX512_EN.
     */
    case X86_FEAT_AVX512_VBMI2:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_AVX512_VBMI2);
#endif /* __Use_Original_Qemu (U550) */
#if __Use_Original_Qemu != 1 /* ours (U555) */
    case X86_FEAT_AVX512_VNNI:
        return (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_AVX512VNNI);
#endif /* __Use_Original_Qemu (U555) */
#if __Use_Original_Qemu != 1 /* ours (U556) */
    case X86_FEAT_AVX512_BF16:
        return (s->cpuid_7_1_eax_features & CPUID_7_1_EAX_AVX512_BF16) &&
               (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512F);
#endif /* __Use_Original_Qemu (U556) */

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

    /*
     * backport 183e6679e3 (U486): "Checks that result in #UD come first" - the VEX.W and
     * VEX.L restrictions of the entry (W0, W1, VEX128) before CR0.TS (#NM) and CR0.EM, so
     * e.g. VLDMXCSR/VSTMXCSR with VEX.L = 1 are #UD (SDM Vol2A LDMXCSR: "If VLDMXCSR is
     * encoded with VEX.L= 1, an attempt to execute the instruction ... will cause an #UD"),
     * not #NM or a memory fault.
     */
    if (e->check & X86_CHECK_W0) {
        if (s->vex_w) {
            goto illegal;
        }
    }
    if ((e->check & X86_CHECK_VEX128) && s->vex_l) {
        goto illegal;
    }
    if (e->check & X86_CHECK_W1) {
        if (!s->vex_w) {
            goto illegal;
        }
    }
#if __Use_Original_Qemu != 1 /* ours (U70) */
    /* U487: the VEX-only W0/W1 restriction is a #UD condition too, checked before #NM */
    if ((s->prefix & PREFIX_VEX) &&
        (((e->check & X86_CHECK_VEX_W0) && s->vex_w) ||
         ((e->check & X86_CHECK_VEX_W1) && !s->vex_w))) {
        goto illegal;
    }
#endif /* __Use_Original_Qemu (U70) */

    if (s->flags & HF_TS_MASK) {
        goto nm_exception;
    }
    if (s->flags & HF_EM_MASK) {
        goto illegal;
    }
    return true;

nm_exception:
    gen_NM_exception(s);
    return false;
illegal:
    gen_illegal_opcode(s);
    return false;
}

#if __Use_Original_Qemu != 1 /* ours (U145) */
/*
 * NoVmp (ledger U145): opcode-dependent EVEX #UD rules and #NM, after the CPUID feature
 * of the form (SDM Vol2A 2.7.11, Tables 2-39, 2-41, 2-42; 2.8 exception classes E1-E12).
 * The prefix rules (Table 2-40, LOCK/66/F2/F3/REX) are checked while parsing 62h, EVEX.W,
 * EVEX.b (Table 2-43) and the vector length in decode_evex_vl.
 * - Table 2-39: CR4.OSXSAVE = 1 and XCR0[7:5,2:1] = 111b,11b (HF_AVX512_EN, U121).
 * - CPUID: AVX512VL for EVEX.128/256 (not for {er}/{sae} reg-reg: 512 bits, or LIG).
 * - Table 2-41: ModRM.reg encoding a k register or GPR with EVEX.R' = 0 (64-bit), a k
 *   register with EVEX.R = 0 (64-bit); EVEX.V' = 0 outside 64-bit mode; vvvv not used:
 *   vvvv != 1111b or V' = 0.
 * - Table 2-42: z = 1 with aaa = 000b, with a memory destination, or for k destinations;
 *   aaa != 000b on forms without masking; aaa = 000b on gathers/scatters.
 * - #NM if CR0.TS = 1 (2.7.12); CR0.EM is not an EVEX condition.
 */
#if __Use_Original_Qemu != 1 /* ours (U645) */
/*
 * NoVmp (ledger U645): the #UD checks of an APX-promoted EVEX form (decode_apx_map4: EVEX map
 * 4 instructions promoted from legacy maps 2/3), used instead of validate_evex and
 * validate_vex: these GPR / memory instructions have no AVX-512 / XCR0[7:5] / CR0.TS
 * conditions (APX spec 355828-009 3.1.2.3). Run-time XCR0[19] / CR4.OSXSAVE check first;
 * mod = 11b needs U = 1; P2 = 0 0 0 ND V4 NF 0 0 with ND only for the NDD forms (ADCX/ADOX,
 * B destination) and no NF (Table 4.12 APX-EVEX-INT, 4.7/4.8 CET, 4.11 ENQCMD, 4.17 MOVRS,
 * 4.19 RAO-INT, 4.23 USER-MSR-EVEX); V must be 0 unless it is the NDD; URDMSR / UWRMSR W0.
 */
static bool validate_apx_promoted(DisasContext *s, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    X86OpEntry *e = &decode->e;
    bool uses_v = e->op0 == X86_TYPE_B || e->op1 == X86_TYPE_B || e->op2 == X86_TYPE_B;
    int p2 = s->evex_p2;

    gen_helper_apx_check(tcg_ctx, cpu_env);
    if (s->has_modrm && (s->modrm >> 6) == 3 && !(s->evex_p1 & 0x04)) {
        goto illegal;
    }
    if ((s->evex_p0 & 7) == 4) {
        if ((p2 & 0xe7) || ((p2 & 0x10) && e->op0 != X86_TYPE_B)) {
            goto illegal;
        }
        if (!uses_v && s->vex_v != 0) {
            goto illegal;
        }
        if ((e->gen == gen_URDMSR || e->gen == gen_UWRMSR) && s->vex_w) {
            goto illegal;
        }
        return true;
    }
illegal:
    gen_illegal_opcode(s);
    return false;
}

#endif /* __Use_Original_Qemu (U645) */
static bool validate_evex(DisasContext *s, X86DecodedInsn *decode)
{
    X86OpEntry *e = &decode->e;
    bool uses_vvvv = false, reg_k = false, reg_gpr = false;
    X86OpType t[3] = { e->op0, e->op1, e->op2 };
    int i;

    for (i = 0; i < 3; i++) {
        switch (t[i]) {
        case X86_TYPE_H:
        case X86_TYPE_B:
        case X86_TYPE_KH:
            uses_vvvv = true;
            break;
        case X86_TYPE_K:
            reg_k = true;
            break;
        case X86_TYPE_G:
            reg_gpr = true;
            break;
        default:
            break;
        }
    }

    if (!(s->flags & HF_AVX512_EN_MASK)) {
        goto illegal;
    }
    /*
     * NoVmp (ledger U210, was: every non-LIG length < 512): AVX512VL gates the 128/256-bit lengths of a form that also has a
     * 512-bit length; forms that exist only at 128 bits (E9NF: VMOVD/VMOVQ, VPINSRD,
     * VINSERTPS, VMOVHPS, ...; E7NM.128: VMOVHLPS/VMOVLHPS) need only their own feature
     * (SDM opcode tables: "AVX512F" without "AVX512VL"). U260 (merged here): the
     * EVEX.128-only AVX512BW forms (VPEXTRB/W, VPINSRB/W) likewise need no AVX512VL.
     */
    if (!(e->evex_vl & X86_EVEX_LIG) && (e->evex_vl & X86_EVEX_VL512) && s->evex_vl < 64 &&
        !(s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX512VL)) {
        goto illegal;
    }
    if (CODE64(s)) {
#if __Use_Original_Qemu == 1 /* original QEMU (U616) */
        if ((reg_k || reg_gpr) && s->evex_rr) {
            goto illegal;
        }
#else /* ours (U616) */
        bool apx = (s->cpuid_7_1_edx_features & CPUID_7_1_EDX_APX_F) != 0;

        if ((reg_k || (reg_gpr && !apx)) && s->evex_rr) {
            goto illegal;
        }
        /*
         * NoVmp (ledger U616): the APX extension of EVEX instructions (APX spec 355828-009
         * 3.1.2.3.3, Figure 3.5, Table 3.3): EVEX.B4 (P0[3]) is bit 4 of a GPR in ModRM.r/m and
         * of the base, EVEX.X4 = ~EVEX.U (P1[2]) bit 4 of the index (ModRM.mod != 11b only:
         * with mod = 11b U must be 1), EVEX.R4 (R') bit 4 of a GPR in ModRM.reg; unused bits
         * are ignored (a vector r/m, a VSIB index, no index). Before APX is enabled B4 and X4
         * keep their fixed values and R4 must be 1 for a GPR, else #UD (3.1.4.2.1): any of
         * them set needs CR4.OSXSAVE = 1 and XCR0[19] = 1 at run time (helper_apx_check).
         */
        if (apx) {
            bool b4 = (s->evex_p0 & 0x08) != 0, u0 = !(s->evex_p1 & 0x04);

            if (u0 && (s->modrm >> 6) == 3) {
                goto illegal;
            }
            if (b4 || u0 || (reg_gpr && s->evex_rr)) {
                TCGContext *tcg_ctx = s->uc->tcg_ctx;

                gen_helper_apx_check(tcg_ctx, cpu_env);
            }
        }
#endif /* __Use_Original_Qemu (U616) */
        if (reg_k && REX_R(s)) {
            goto illegal;
        }
    } else if (!(s->evex_p2 & 0x08)) {
        goto illegal;
    }
    if (!uses_vvvv && ((s->evex_p1 & 0x78) != 0x78 ||
                       (!(s->evex_p2 & 0x08) && e->vex_class != 12))) {
        goto illegal;               /* V' extends a VSIB index (class 12) instead */
    }
    switch (e->evex_mask) {
    case X86_EVEX_MASK_MZ:
        if (s->evex_z && (!s->evex_aaa || decode->op[0].has_ea)) {
            goto illegal;
        }
        break;
    case X86_EVEX_MASK_M:
    case X86_EVEX_MASK_KDEST:
        if (s->evex_z) {
            goto illegal;
        }
        break;
    case X86_EVEX_MASK_KREQ:
        if (s->evex_z || !s->evex_aaa) {
            goto illegal;
        }
        break;
    default:
        if (s->evex_z || s->evex_aaa) {
            goto illegal;
        }
        break;
    }
#if __Use_Original_Qemu != 1 /* ours (U250) */
    /*
     * NoVmp (ledger U250): exception class E12 (SDM Vol2A 2.8.11 Table 2-63): #UD if the
     * address size is 16 bits, if ModRM.rm != 100b (no VSIB byte; ModRM.mod = 11b fails in
     * decode_op), and for a gather if the destination is the index register (all 32
     * registers: ModRM.reg with EVEX.R/R' vs SIB.index with EVEX.X/V'). The mask is a k
     * register, so the VEX "mask == destination/index" rules do not apply.
     */
    if (e->vex_class == 12) {
        if ((s->modrm & 7) != 4 || s->aflag == MO_16) {
            goto illegal;
        }
        if (e->op0 == X86_TYPE_V && decode->op[0].n == decode->mem.index) {
            goto illegal;
        }
    }
#endif /* __Use_Original_Qemu (U250) */
    if (s->flags & HF_TS_MASK) {
        gen_NM_exception(s);
        return false;
    }
    return true;

illegal:
    gen_illegal_opcode(s);
    return false;
}
#endif /* __Use_Original_Qemu (U145) */

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
         * backport ed88bdcfbd (U484): bits 6-7 of the first byte must be set except in
         * 64-bit mode, otherwise the instruction is LES or LDS. Not allowed in real mode
         * (or virtual-8086 mode); 16-bit protected mode supports VEX.
         */
        if (PE(s) && !VM86(s)) {
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
#if __Use_Original_Qemu == 1 /* original QEMU (U455) */
            /* QEMU 0db1b556e4: ignore V3 in 32-bit mode */
            s->vex_v = (~vex3 >> 3) & (CODE64(s) ? 15 : 7);
#else /* ours (U455) */
            /*
             * NoVmp (ledger U455): SDM Vol2A 2.3.5.6: outside 64-bit mode bit 6 (vvvv[3])
             * is ignored when vvvv names a register (done where it is used, U93), but an
             * instruction that does not use vvvv still needs 1111b, else #UD: s->vex_v
             * keeps all four bits (i5-13600K: C4 E1 39 6F CA in compatibility mode is #UD).
             */
            s->vex_v = (~vex3 >> 3) & 0xf;
#endif /* __Use_Original_Qemu (U455) */
            s->vex_l = (vex3 >> 2) & 1;
            s->prefix |= pp_prefix[vex3 & 3] | PREFIX_VEX;
        }
        break;
#if __Use_Original_Qemu != 1 /* ours (U141) */
    case 0x62:
        /*
         * NoVmp (ledger U141): EVEX prefix, P0 = R X B R' 0 m m m, P1 = W v v v v 1 p p,
         * P2 = z L' L b V' a a a (SDM Vol2A 2.7.1, Table 2-32). disas_insn routes 62h here
         * only when it is not BOUND (64-bit mode, or P0[7:6] = 11b in protected mode).
         * #UD: LOCK/66/F2/F3/REX before EVEX (Table 2-46); P[3] != 0, P[10] != 1, map 0/4/7
         * (Table 2-40). Maps 5/6: AVX512-FP16 (U333) and AVX10.2 (U372). Outside 64-bit
         * mode EVEX.R/X/B/R' select nothing (Table 2-34: 8 registers; R and X are 1 there,
         * B and R' are ignored) and vvvv[3] is ignored (Table 2-41); V' is checked in
         * validate_evex.
         */
        {
            static const int pp_prefix[4] = {
                0, PREFIX_DATA, PREFIX_REPZ, PREFIX_REPNZ
            };
            int p0 = x86_ldub_code(env, s);
            int p1 = x86_ldub_code(env, s);
            int p2 = x86_ldub_code(env, s);
            /*
             * U616: with APX_F in 64-bit mode, P0[3] is EVEX.B4 and P1[2] is EVEX.U, X4 = ~U
             * (APX spec 355828-009 3.1.2.3, Figure 3.5): validate_evex checks them.
             */
            bool apx_evex = CODE64(s) && (s->cpuid_7_1_edx_features & CPUID_7_1_EDX_APX_F);

            if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ
                             | PREFIX_LOCK | PREFIX_DATA | PREFIX_REX)) {
                goto illegal_op;
            }
            if (!apx_evex && ((p0 & 0x08) || !(p1 & 0x04))) {
                goto illegal_op;
            }
            switch (p0 & 7) {
            case 1:
                decode_func = decode_evex_0F;
                break;
            case 2:
                decode_func = decode_evex_0F38;
                break;
            case 3:
                decode_func = decode_evex_0F3A;
                break;
#if __Use_Original_Qemu == 1 /* original QEMU (U333) */
            case 5: /* AVX512-FP16 maps */
            case 6:
                goto unknown_op;
#else /* ours (U333) */
            case 5: /* AVX512-FP16 maps (U333) */
                decode_func = decode_evex_map5;
                break;
            case 6:
                decode_func = decode_evex_map6;
                break;
#endif /* __Use_Original_Qemu (U333) */
#if __Use_Original_Qemu != 1 /* ours (U645) */
            case 4: /* Intel APX: promoted legacy instructions (U645) */
                if (!apx_evex) {
                    goto illegal_op;
                }
                decode_func = decode_apx_map4;
                break;
#endif /* __Use_Original_Qemu (U645) */
            default:
                goto illegal_op;
            }
            s->evex_p0 = p0;
            s->evex_p1 = p1;
            s->evex_p2 = p2;
#ifdef TARGET_X86_64
            if (CODE64(s)) {
                s->rex_r = (~p0 >> 4) & 8;
                s->rex_x = (~p0 >> 3) & 8;
                s->rex_b = (~p0 >> 2) & 8;
                s->evex_rr = ~p0 & 0x10;
                s->evex_x4 = (~p0 >> 2) & 0x10;
                s->evex_v4 = (~p2 << 1) & 0x10;
                if (apx_evex) {
                    /* U616: bit 4 of the B (GPR r/m, base) and X (index) ids */
                    s->rex_b |= (p0 & 0x08) << 1;
                    s->rex_x |= (~p1 & 0x04) << 2;
                }
            }
#endif
            s->vex_w = (p1 >> 7) & 1;
            s->vex_v = ((~p1 >> 3) & 0xf) | s->evex_v4;
            if (!CODE64(s)) {
                s->vex_v &= 7;
            }
            s->evex_aaa = p2 & 7;
            s->evex_b = (p2 >> 4) & 1;
            s->evex_ll = (p2 >> 5) & 3;
            s->evex_z = (p2 >> 7) & 1;
            s->prefix |= pp_prefix[p1 & 3] | PREFIX_VEX | PREFIX_EVEX;
        }
        break;
#endif /* __Use_Original_Qemu (U141) */
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
        decode.e.special = X86_SPECIAL_HasLock;
        /* fallthrough */
    case X86_SPECIAL_HasLock:
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

    /*
     * backport of QEMU b609db9477: LOCK is valid only on an instruction tagged as
     * lockable with a memory destination, else #UD (SDM Vol2A LOCK). No decode-new
     * entry is lockable here (the lockable integer forms are in the old decoder),
     * so LOCK on any of them is #UD, before the VEX/EVEX checks.
     */
    if (s->prefix & PREFIX_LOCK) {
        if (decode.e.special != X86_SPECIAL_HasLock || !decode.op[0].has_ea) {
            goto illegal_op;
        }
    }

#if __Use_Original_Qemu != 1 /* ours (U645) */
    /* Intel APX-promoted EVEX forms: their own checks, then the VEX/legacy code path (U645) */
    if (s->apx_promoted) {
        if (!validate_apx_promoted(s, &decode)) {
            decode_temps_free(tcg_ctx, &decode);
            return;
        }
        goto apx_promoted;
    }
#endif /* __Use_Original_Qemu (U645) */
#if __Use_Original_Qemu != 1 /* ours (U145) */
    if (s->prefix & PREFIX_EVEX) {
        if (validate_evex(s, &decode)) {
#if __Use_Original_Qemu != 1 /* ours (U146) */
            gen_evex_insn(s, env, &decode);
#endif /* __Use_Original_Qemu (U146) */
        }
        decode_temps_free(tcg_ctx, &decode);
        return;
    }
#endif /* __Use_Original_Qemu (U145) */
    if (!validate_vex(s, &decode)) {
        return;
    }
#if __Use_Original_Qemu != 1 /* ours (U645) */
 apx_promoted:
#endif /* __Use_Original_Qemu (U645) */
#if __Use_Original_Qemu != 1 /* ours (U50) */
    /*
     * NoVmp (ledger U50): "If an x87 FPU floating-point exception is pending
     * and the processor encounters an MMX instruction, the processor
     * generates a #MF prior to executing the MMX instruction" (SDM Vol3
     * 14.5.1, Tables 24-4..24-9), before any memory access. CVTPI2PD xmm, m64
     * takes no x87 exception (SDM); CVTPI2PS xmm, m64 does per its SDM page
     * (U532: the i5-13600K does not, a documented deviation in docs/quirks.md).
     */
    if ((decode.e.special == X86_SPECIAL_MMX &&
         !(s->prefix & (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA))) ||
        decode.e.gen == gen_CVTPx2PI || decode.e.gen == gen_CVTTPx2PI ||
        decode.e.gen == gen_MOVq_dq || decode.e.gen == gen_EMMS ||
        (decode.e.gen == gen_CVTPI2Px &&
         (!decode.op[2].has_ea || !(s->prefix & PREFIX_DATA)))) {
        gen_helper_fwait(tcg_ctx, cpu_env);
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
