/*
 * New-style TCG opcode generator for i386 instructions
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

#define ZMM_OFFSET(reg) offsetof(CPUX86State, xmm_regs[reg])

typedef void (*SSEFunc_i_ep)(TCGContext *tcg_ctx, TCGv_i32 val, TCGv_ptr env,
                             TCGv_ptr reg);
typedef void (*SSEFunc_l_ep)(TCGContext *tcg_ctx, TCGv_i64 val, TCGv_ptr env,
                             TCGv_ptr reg);
typedef void (*SSEFunc_0_epp)(TCGContext *tcg_ctx, TCGv_ptr env,
                              TCGv_ptr reg_a, TCGv_ptr reg_b);
typedef void (*SSEFunc_0_eppp)(TCGContext *tcg_ctx, TCGv_ptr env,
                               TCGv_ptr reg_a, TCGv_ptr reg_b,
                               TCGv_ptr reg_c);
typedef void (*SSEFunc_0_epppp)(TCGContext *tcg_ctx, TCGv_ptr env,
                                TCGv_ptr reg_a, TCGv_ptr reg_b,
                                TCGv_ptr reg_c, TCGv_ptr reg_d);
typedef void (*SSEFunc_0_eppi)(TCGContext *tcg_ctx, TCGv_ptr env,
                               TCGv_ptr reg_a, TCGv_ptr reg_b,
                               TCGv_i32 val);
typedef void (*SSEFunc_0_epppi)(TCGContext *tcg_ctx, TCGv_ptr env,
                                TCGv_ptr reg_a, TCGv_ptr reg_b,
                                TCGv_ptr reg_c, TCGv_i32 val);
typedef void (*SSEFunc_0_ppi)(TCGContext *tcg_ctx, TCGv_ptr reg_a,
                              TCGv_ptr reg_b, TCGv_i32 val);
typedef void (*SSEFunc_0_pppi)(TCGContext *tcg_ctx, TCGv_ptr reg_a,
                               TCGv_ptr reg_b, TCGv_ptr reg_c,
                               TCGv_i32 val);
typedef void (*SSEFunc_0_eppt)(TCGContext *tcg_ctx, TCGv_ptr env,
                               TCGv_ptr reg_a, TCGv_ptr reg_b, TCGv val);
typedef void (*SSEFunc_0_eppptit)(TCGContext *tcg_ctx, TCGv_ptr env,
                                  TCGv_ptr reg_a, TCGv_ptr reg_b,
                                  TCGv_ptr reg_c, TCGv a0, TCGv_i32 scale,
                                  TCGv amask);
typedef void (*SSEFunc_0_eppppi)(TCGContext *tcg_ctx, TCGv_ptr env,
                                 TCGv_ptr reg_a, TCGv_ptr reg_b,
                                 TCGv_ptr reg_c, TCGv_ptr reg_d,
                                 TCGv_i32 flags);
typedef void (*SSEFunc_0_eppppii)(TCGContext *tcg_ctx, TCGv_ptr env,
                                  TCGv_ptr reg_a, TCGv_ptr reg_b,
                                  TCGv_ptr reg_c, TCGv_ptr reg_d,
                                  TCGv_i32 even, TCGv_i32 odd);

static inline TCGv_i32 tcg_constant8u_i32(TCGContext *tcg_ctx, uint8_t val)
{
    return tcg_constant_i32(tcg_ctx, val);
}

static void gen_NM_exception(DisasContext *s)
{
    gen_exception(s, EXCP07_PREX);
}

static void gen_load_ea(DisasContext *s, AddressParts *mem, bool is_vsib)
{
    TCGv ea = gen_lea_modrm_1(s, *mem, is_vsib);
    gen_lea_v_seg(s, s->aflag, ea, mem->def_seg, s->override);
}

static inline int mmx_offset(MemOp ot)
{
    switch (ot) {
    case MO_8:
        return offsetof(MMXReg, MMX_B(0));
    case MO_16:
        return offsetof(MMXReg, MMX_W(0));
    case MO_32:
        return offsetof(MMXReg, MMX_L(0));
    case MO_64:
        return offsetof(MMXReg, MMX_Q(0));
    default:
        g_assert_not_reached();
    }
    return 0;
}

static inline int xmm_offset(MemOp ot)
{
    switch (ot) {
    case MO_8:
        return offsetof(ZMMReg, ZMM_B(0));
    case MO_16:
        return offsetof(ZMMReg, ZMM_W(0));
    case MO_32:
        return offsetof(ZMMReg, ZMM_L(0));
    case MO_64:
        return offsetof(ZMMReg, ZMM_Q(0));
    case MO_128:
        return offsetof(ZMMReg, ZMM_X(0));
    case MO_256:
        return offsetof(ZMMReg, ZMM_Y(0));
#if __Use_Original_Qemu != 1 /* ours (U142) */
    case MO_512:            /* EVEX.512: the whole ZMM register (U142) */
        return 0;
#endif /* __Use_Original_Qemu (U142) */
    default:
        g_assert_not_reached();
    }
    return 0;
}

static int vector_reg_offset(X86DecodedOp *op)
{
    assert(op->unit == X86_OP_MMX || op->unit == X86_OP_SSE);

    if (op->unit == X86_OP_MMX) {
        return op->offset - mmx_offset(op->ot);
    } else {
        return op->offset - xmm_offset(op->ot);
    }
}

static int vector_elem_offset(X86DecodedOp *op, MemOp ot, int n)
{
    int base_ofs = vector_reg_offset(op);
    switch(ot) {
    case MO_8:
        if (op->unit == X86_OP_MMX) {
            return base_ofs + offsetof(MMXReg, MMX_B(n));
        } else {
            return base_ofs + offsetof(ZMMReg, ZMM_B(n));
        }
    case MO_16:
        if (op->unit == X86_OP_MMX) {
            return base_ofs + offsetof(MMXReg, MMX_W(n));
        } else {
            return base_ofs + offsetof(ZMMReg, ZMM_W(n));
        }
    case MO_32:
        if (op->unit == X86_OP_MMX) {
            return base_ofs + offsetof(MMXReg, MMX_L(n));
        } else {
            return base_ofs + offsetof(ZMMReg, ZMM_L(n));
        }
    case MO_64:
        if (op->unit == X86_OP_MMX) {
            return base_ofs;
        } else {
            return base_ofs + offsetof(ZMMReg, ZMM_Q(n));
        }
    case MO_128:
        assert(op->unit == X86_OP_SSE);
        return base_ofs + offsetof(ZMMReg, ZMM_X(n));
    case MO_256:
        assert(op->unit == X86_OP_SSE);
        return base_ofs + offsetof(ZMMReg, ZMM_Y(n));
    default:
        g_assert_not_reached();
    }
    return 0;
}

static void compute_mmx_offset(X86DecodedOp *op)
{
    if (!op->has_ea) {
        op->offset = offsetof(CPUX86State, fpregs[op->n].mmx) + mmx_offset(op->ot);
    } else {
        op->offset = offsetof(CPUX86State, mmx_t0) + mmx_offset(op->ot);
    }
}

static void compute_xmm_offset(X86DecodedOp *op)
{
    if (!op->has_ea) {
        op->offset = ZMM_OFFSET(op->n) + xmm_offset(op->ot);
    } else {
        op->offset = offsetof(CPUX86State, xmm_t0) + xmm_offset(op->ot);
    }
}

static void gen_load_sse(DisasContext *s, TCGv temp, MemOp ot, int dest_ofs, bool aligned)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    switch(ot) {
    case MO_8:
        gen_op_ld_v(s, MO_8, temp, s->A0);
        tcg_gen_st8_tl(tcg_ctx, temp, cpu_env, dest_ofs);
        break;
    case MO_16:
        gen_op_ld_v(s, MO_16, temp, s->A0);
        tcg_gen_st16_tl(tcg_ctx, temp, cpu_env, dest_ofs);
        break;
    case MO_32:
        gen_op_ld_v(s, MO_32, temp, s->A0);
        tcg_gen_st32_tl(tcg_ctx, temp, cpu_env, dest_ofs);
        break;
    case MO_64:
        gen_ldq_env_A0(s, dest_ofs);
        break;
    case MO_128:
        gen_ldo_env_A0(s, dest_ofs, aligned);
        break;
    case MO_256:
        gen_ldy_env_A0(s, dest_ofs, aligned);
        break;
#if __Use_Original_Qemu != 1 /* ours (U142) */
    case MO_512:
        gen_ldz_env_A0(s, dest_ofs, aligned);
        break;
#endif /* __Use_Original_Qemu (U142) */
    default:
        g_assert_not_reached();
    }
}

#if __Use_Original_Qemu != 1 /* ours (U874) */
static void gen_CVTPx2PI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode);
static void gen_CVTTPx2PI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode);
#endif /* __Use_Original_Qemu (U874) */

static bool sse_needs_alignment(DisasContext *s, X86DecodedInsn *decode, MemOp ot)
{
#if __Use_Original_Qemu != 1 /* ours (U874) */
    /*
     * NoVmp (ledger U874): CVTPD2PI / CVTTPD2PI mm, xmm/m128 (66 0F 2D / 2C): #GP(0) when the
     * m128 source is not 16-byte aligned (SDM Vol3B Table 25-4, "Legacy SIMD/MMX Instructions
     * with FP Exception and 16-Byte Alignment", listed for CVTPD2PI, CVTTPD2PI; i5-13600K:
     * #GP). Their MMX-state decode has exception class 0, which never checked it.
     */
    if ((decode->e.gen == gen_CVTPx2PI || decode->e.gen == gen_CVTTPx2PI) && ot >= MO_128) {
        return true;
    }
#endif /* __Use_Original_Qemu (U874) */
    switch (decode->e.vex_class) {
    case 2:
    case 4:
        if ((s->prefix & PREFIX_VEX) ||
            decode->e.vex_special == X86_VEX_SSEUnaligned) {
            /* MOST legacy SSE instructions require aligned memory operands, but not all.  */
            return false;
        }
        /* fall through */
    case 1:
        return ot >= MO_128;

    default:
        return false;
    }
}

static void gen_load(DisasContext *s, X86DecodedInsn *decode, int opn, TCGv v)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    X86DecodedOp *op = &decode->op[opn];

    switch (op->unit) {
    case X86_OP_SKIP:
        return;
    case X86_OP_SEG:
        tcg_gen_ld32u_tl(tcg_ctx, v, cpu_env,
                         offsetof(CPUX86State,segs[op->n].selector));
        break;
    case X86_OP_CR:
        tcg_gen_ld_tl(tcg_ctx, v, cpu_env, offsetof(CPUX86State, cr[op->n]));
        break;
    case X86_OP_DR:
        tcg_gen_ld_tl(tcg_ctx, v, cpu_env, offsetof(CPUX86State, dr[op->n]));
        break;
    case X86_OP_INT:
        if (op->has_ea) {
            gen_op_ld_v(s, op->ot, v, s->A0);
        } else {
            gen_op_mov_v_reg(s, op->ot, v, op->n);
        }
        break;
    case X86_OP_IMM:
        tcg_gen_movi_tl(tcg_ctx, v, decode->immediate);
        break;

    case X86_OP_MMX:
        compute_mmx_offset(op);
        goto load_vector;

    case X86_OP_SSE:
        compute_xmm_offset(op);
    load_vector:
        if (op->has_ea) {
            bool aligned = sse_needs_alignment(s, decode, op->ot);
            gen_load_sse(s, v, op->ot, op->offset, aligned);
        }
        break;

#if __Use_Original_Qemu != 1 /* ours (U129) */
    case X86_OP_KREG:
        /*
         * NoVmp (ledger U129): an opmask operand of op->ot (MO_8..MO_64) bits. Memory
         * reads exactly that size (KMOV k, m8/m16/m32/m64); a k register is read as
         * SRC[ot-1:0] zero-extended (SDM Vol2A KADD..KXOR operate on SRC[w-1:0]).
         * opmask_regs[] are 64-bit; target_ulong is 64-bit (x86_64 target only).
         */
        if (op->has_ea) {
            gen_op_ld_v(s, op->ot, v, s->A0);
        } else {
            tcg_gen_ld_tl(tcg_ctx, v, cpu_env, offsetof(CPUX86State, opmask_regs[op->n]));
            gen_extu(tcg_ctx, op->ot, v);
        }
        break;
#endif /* __Use_Original_Qemu (U129) */

    default:
        g_assert_not_reached();
    }
}

static TCGv_ptr op_ptr(TCGContext *tcg_ctx, X86DecodedInsn *decode, int opn)
{
    X86DecodedOp *op = &decode->op[opn];
    if (op->v_ptr) {
        return op->v_ptr;
    }
    op->v_ptr = tcg_temp_new_ptr(tcg_ctx);

    /* The temporary points to the MMXReg or ZMMReg.  */
    tcg_gen_addi_ptr(tcg_ctx, op->v_ptr, cpu_env, vector_reg_offset(op));
    return op->v_ptr;
}

#define OP_PTR0 op_ptr(tcg_ctx, decode, 0)
#define OP_PTR1 op_ptr(tcg_ctx, decode, 1)
#define OP_PTR2 op_ptr(tcg_ctx, decode, 2)

static void gen_writeback(DisasContext *s, X86DecodedInsn *decode, int opn, TCGv v)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    X86DecodedOp *op = &decode->op[opn];
    switch (op->unit) {
    case X86_OP_SKIP:
        break;
    case X86_OP_SEG:
        /* Note that gen_movl_seg_T0 takes care of interrupt shadow and TF.  */
        gen_movl_seg_T0(s, op->n, op->n == R_SS);
        break;
    case X86_OP_INT:
        if (op->has_ea) {
            gen_op_st_v(s, op->ot, v, s->A0);
        } else {
            gen_op_mov_reg_v(s, op->ot, op->n, v);
        }
        break;
    case X86_OP_MMX:
        break;
    case X86_OP_SSE:
#if __Use_Original_Qemu == 1 /* original QEMU (U126) */
        if (!op->has_ea && (s->prefix & PREFIX_VEX) && op->ot <= MO_128) {
            tcg_gen_gvec_dup_imm(tcg_ctx, MO_64,
                                 offsetof(CPUX86State, xmm_regs[op->n].ZMM_X(1)),
                                 16, 16, 0);
        }
#else /* ours (U126) */
        /*
         * NoVmp (ledger U126): a VEX.128/256 register destination has bits
         * MAXVL-1:VL zeroed with MAXVL = 512 (SDM Vol1 15.5: VEX.128 zeroes the
         * upper 384 bits of the ZMM register, VEX.256 the upper 256), not only
         * 255:128. Legacy SSE (no VEX) keeps bits MAXVL-1:128. Visible once ZMM
         * bits 511:256 are non-zero.
         */
        if (!op->has_ea && (s->prefix & PREFIX_VEX) && op->ot <= MO_256) {
            int vl = op->ot == MO_256 ? 32 : 16;
            tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, ZMM_OFFSET(op->n) + vl,
                                 sizeof(ZMMReg) - vl, sizeof(ZMMReg) - vl, 0);
        }
#endif /* __Use_Original_Qemu (U126) */
        break;
#if __Use_Original_Qemu != 1 /* ours (U129) */
    case X86_OP_KREG:
        /*
         * NoVmp (ledger U129): KMOV m8/m16/m32/m64 stores exactly op->ot; a k register
         * destination is DEST[ot-1:0] zero-extended to MAX_KL = 64 bits (SDM Vol2A:
         * "DEST[MAX_KL-1:w] := 0" in every opmask instruction).
         */
        if (op->has_ea) {
            gen_op_st_v(s, op->ot, v, s->A0);
        } else {
            gen_extu(tcg_ctx, op->ot, v);
            tcg_gen_st_tl(tcg_ctx, v, cpu_env, offsetof(CPUX86State, opmask_regs[op->n]));
        }
        break;
#endif /* __Use_Original_Qemu (U129) */
    case X86_OP_CR:
    case X86_OP_DR:
    default:
        g_assert_not_reached();
    }
}

static inline int vector_len(DisasContext *s, X86DecodedInsn *decode)
{
#if __Use_Original_Qemu != 1 /* ours (U142) */
    if (s->prefix & PREFIX_EVEX) {
        return s->evex_vl;      /* 16/32/64 bytes (U142) */
    }
#endif /* __Use_Original_Qemu (U142) */
    if (decode->e.special == X86_SPECIAL_MMX &&
        !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
        return 8;
    }
    return s->vex_l ? 32 : 16;
}

#if __Use_Original_Qemu != 1 /* ours (U143) */
/*
 * NoVmp (ledger U143): 3-way selection of an ops_sse.h helper by vector length: SHIFT 1
 * (_xmm, 16 bytes), SHIFT 2 (_ymm, 32) or SHIFT 3 (_zmm, 64 bytes: EVEX.512).
 */
#define SSE_FN3(s, decode, name)                                   \
    (vector_len(s, decode) == 64 ? gen_helper_##name##_zmm :       \
     vector_len(s, decode) == 32 ? gen_helper_##name##_ymm :       \
     gen_helper_##name##_xmm)
#endif /* __Use_Original_Qemu (U143) */

#if __Use_Original_Qemu != 1 /* ours (U146) */
/*
 * NoVmp (ledger U146): execution of an EVEX instruction (SDM Vol2A 2.7.4, 2.8; Vol1 15.6).
 * The form's gen function computes the full-length result of the operation; masking is
 * applied here, after it, in one place:
 * - E1 forms: #GP(0) unless the memory operand is VL-aligned, before any access and
 *   whatever the mask (Table 2-46).
 * - Memory source: without a mask (aaa = 000b, k0 = no masking) a plain load; with a mask
 *   (and fault suppression) element by element, so masked-off elements never fault.
 * - Floating point with a mask: the register sources are copied with +1.0 in the
 *   masked-off lanes (no flag, no #XM from them); the result goes to a scratch register
 *   and #XM (U67 sse_fp_begin/end) is raised before the destination is written.
 * - Vector register destination: merging- or zeroing-masking per element (EVEX.z), then
 *   DEST[MAXVL-1:VL] := 0. Memory destination: only the elements whose mask bit is set are
 *   stored, all of them probed first. Opmask destination (k1 {k2}): ANDed with k2, the bits
 *   above the element count are 0. Without a mask the result is written directly.
 * The gen function of a form with an opmask destination leaves the mask in tmp1_i64.
 */
#define EVEX_DESC(esz, n)   ((uint32_t)(esz) | ((uint32_t)(n) << 8))
#define EVEX_DESC_Z         (1u << 16)
#define EVEX_DESC_ANY       (1u << 17)
#if __Use_Original_Qemu != 1 /* ours (U193) */
#define EVEX_DESC_NOFS      (1u << 18)  /* E*NF masked store: probe every element (U193) */
#if __Use_Original_Qemu != 1 /* ours (U834) */
/* the instruction reports #AC: the helper checks a 2/4/8-byte operand (U834) */
#define EVEX_DESC_AC        (1u << 19)
#define EVEX_AC(s)          ((s)->ac_insn ? EVEX_DESC_AC : 0u)
#endif /* __Use_Original_Qemu (U834) */
#endif /* __Use_Original_Qemu (U193) */

static TCGv_ptr evex_env_ptr(DisasContext *s, int ofs)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr p = tcg_temp_new_ptr(tcg_ctx);

    tcg_gen_addi_ptr(tcg_ctx, p, cpu_env, ofs);
    return p;
}

static int evex_mem_bytes(X86DecodedOp *op)
{
    return op->ot <= MO_64 ? 1 << op->ot : 16 << (op->ot - MO_128);
}

#if __Use_Original_Qemu != 1 /* ours (U210) */
/*
 * NoVmp (ledger U210): bytes of the vector destination. A form whose register / memory
 * destination is narrower than VL (VPMOVDB xmm1/m128 from zmm2, VEXTRACTF32X4 xmm1/m128,
 * VMOVQ xmm1/m64, ...) is masked per destination element - KL = destination bytes /
 * element size (the forms give the destination element size as evex_es) - and zeroes
 * DEST[MAXVL-1:destination bytes] (SDM Vol2C VPMOVDB "DEST[MAXVL-1:VL/4] := 0",
 * VEXTRACTF32X4 "DEST[MAXVL-1:128] := 0"). Scalar (LIG) forms are not concerned.
 */
static int evex_dest_bytes(DisasContext *s, X86DecodedInsn *decode)
{
    X86DecodedOp *op0 = &decode->op[0];

    if (!(decode->e.evex_vl & X86_EVEX_LIG) && op0->unit == X86_OP_SSE &&
        evex_mem_bytes(op0) < s->evex_vl) {
        return evex_mem_bytes(op0);
    }
    return s->evex_vl;
}
#endif /* __Use_Original_Qemu (U210) */

/*
 * elements of the instruction that the opmask controls (scalar forms: element 0 only):
 * KL = destination bytes (U210) / destination element size (U230 evex_dsz: the writemask
 * element size, U260's evex_kes merged into it); up to 64 (byte elements of a ZMM).
 * Conversions: VCVTPD2PS zmm -> ymm: 32 bytes / 4 = 8; VCVTPS2PD ymm -> zmm: 64 / 8 = 8
 * (U230's "VL >> max(source, destination size)" gives the same count).
 */
static int evex_nelem(DisasContext *s, X86DecodedInsn *decode)
{
#if __Use_Original_Qemu != 1 /* ours (U210) */
    if (!(decode->e.evex_vl & X86_EVEX_LIG)) {
        return evex_dest_bytes(s, decode) >> s->evex_dsz;   /* U230: evex_dsz */
    }
#endif /* __Use_Original_Qemu (U210) */
    return (decode->e.evex_vl & X86_EVEX_LIG) ? 1 : s->evex_vl >> s->evex_esz;
}

/* load the memory source operand op into xmm_t0 (op->offset points there already) */
static void gen_evex_load(DisasContext *s, X86DecodedInsn *decode, X86DecodedOp *op,
                          TCGv_i64 kmask)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    X86OpEntry *e = &decode->e;
    int nelem = evex_nelem(s, decode);
    int bytes = evex_mem_bytes(op);
    TCGv_ptr p;
    uint32_t desc;

#if __Use_Original_Qemu != 1 /* ours (U147) */
    /*
     * NoVmp (ledger U147): embedded broadcast {1toN} (EVEX.b with a memory operand of a
     * Full/Half tuple form, SDM Vol2A 2.7.7): one element of the operation's size is read
     * (with a mask: only when an element is active) and replicated over the vector.
     */
    if (s->evex_b) {
        int base = op->offset - xmm_offset(op->ot);

        if (kmask && !e->evex_nofs) {
            p = evex_env_ptr(s, base);
            gen_helper_evex_mload(tcg_ctx, cpu_env, p, s->A0, kmask,
                                  tcg_constant_i32(tcg_ctx, EVEX_DESC(s->evex_esz, nelem) |
                                                   EVEX_DESC_ANY | EVEX_AC(s)));
            tcg_temp_free_ptr(tcg_ctx, p);
        } else {
            gen_load_sse(s, s->tmp4, s->evex_esz, base, false);
        }
        tcg_gen_gvec_dup_mem(tcg_ctx, s->evex_esz, base, base, s->evex_vl, s->evex_vl);
        return;
    }
#endif /* __Use_Original_Qemu (U147) */
    if (!kmask || e->evex_nofs) {
        gen_load_sse(s, s->tmp4, op->ot, op->offset, false);
        return;
    }
#if __Use_Original_Qemu != 1 /* ours (U210) */
    /*
     * NoVmp (ledger U210): Tuple2/4/8 with fault suppression (E6 VBROADCASTF32X2/F32X4/
     * F64X2/F32X8/F64X4/I...: lane j uses element j mod g of the g-element group, SDM Vol2C
     * VBROADCAST "n := (j modulo 4) * 32"): group element i is read only when a lane that
     * uses it is active - the mask folded modulo g (Vol1 15.6.1 "memory faults are
     * suppressed for elements with a mask bit of 0").
     */
    if (e->evex_tt == X86_EVEX_TT_T2 || e->evex_tt == X86_EVEX_TT_T4 ||
        e->evex_tt == X86_EVEX_TT_T8) {
        int g = bytes >> s->evex_esz, c;
        TCGv_i64 fm = tcg_temp_new_i64(tcg_ctx);
        TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);

        tcg_gen_andi_i64(tcg_ctx, fm, kmask, (1ull << g) - 1);
        for (c = 1; c < nelem / g; c++) {
            tcg_gen_shri_i64(tcg_ctx, t, kmask, c * g);
            tcg_gen_andi_i64(tcg_ctx, t, t, (1ull << g) - 1);
            tcg_gen_or_i64(tcg_ctx, fm, fm, t);
        }
        p = evex_env_ptr(s, op->offset - xmm_offset(op->ot));
        gen_helper_evex_mload(tcg_ctx, cpu_env, p, s->A0, fm,
                              tcg_constant_i32(tcg_ctx, EVEX_DESC(s->evex_esz, g) | EVEX_AC(s)));
        tcg_temp_free_ptr(tcg_ctx, p);
        tcg_temp_free_i64(tcg_ctx, t);
        tcg_temp_free_i64(tcg_ctx, fm);
        return;
    }
#endif /* __Use_Original_Qemu (U210) */
    if (e->evex_tt == X86_EVEX_TT_T1S || e->evex_tt == X86_EVEX_TT_T1F) {
        /* one element used by every lane: read when any lane is active */
        desc = EVEX_DESC(op->ot <= MO_64 ? op->ot : MO_64, nelem) | EVEX_DESC_ANY;
    } else {
        desc = EVEX_DESC(ctz32(bytes / nelem), nelem);
    }
    p = evex_env_ptr(s, op->offset - xmm_offset(op->ot));
    gen_helper_evex_mload(tcg_ctx, cpu_env, p, s->A0, kmask,
                          tcg_constant_i32(tcg_ctx, desc | EVEX_AC(s)));
    tcg_temp_free_ptr(tcg_ctx, p);
}

static void gen_evex_fp_begin(DisasContext *s)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

#if __Use_Original_Qemu != 1 /* ours (U147) */
    /*
     * NoVmp (ledger U147): {er} / {sae} on a register-register form (SDM Vol2A 2.7.8,
     * 2.7.9): static rounding overriding MXCSR.RC, all exceptions suppressed - the
     * instruction behaves as if every MXCSR mask were set and sets no MXCSR flag (no #XM).
     */
    if (s->evex_rc != -1) {
        gen_helper_evex_rc_begin(tcg_ctx, cpu_env, tcg_constant_i32(tcg_ctx, s->evex_rc));
        return;
    }
#endif /* __Use_Original_Qemu (U147) */
    gen_helper_sse_fp_begin(tcg_ctx, cpu_env);
}

static void gen_evex_fp_end(DisasContext *s)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

#if __Use_Original_Qemu != 1 /* ours (U147) */
    if (s->evex_rc != -1) {
        gen_helper_evex_rc_end(tcg_ctx, cpu_env);
        return;
    }
#endif /* __Use_Original_Qemu (U147) */
    /* the destination is not written yet: nothing to restore on #XM */
    gen_helper_sse_fp_end(tcg_ctx, cpu_env, tcg_constant_i32(tcg_ctx, 0),
                          tcg_constant_i32(tcg_ctx, 0));
}
#if __Use_Original_Qemu != 1 /* ours (U192) */

/*
 * NoVmp (ledger U192): scalar forms (EVEX.LLIG) with a vector register destination (SDM
 * Vol2A 2.7.13 "Scalar Instructions" and every EVEX scalar page, exception classes E3, E5,
 * E10): only element 0 is computed and masked with k1[0]; DEST[127:esz] := SRC1[127:esz]
 * (SRC1 = EVEX.vvvv), or DEST[127:esz] unchanged when the destination is itself the first
 * source (FMA: "DEST[127:32] := DEST[127:32]"), or 0 for a form without SRC1 (VMOVSS/SD
 * xmm1, m: "DEST[MAXVL-1:32] := 0"); DEST[MAXVL-1:128] := 0 (writeback). Those bits are put
 * into the result scratch register evex_t1 here, before the destination is written, so a
 * destination that is also SRC1 reads its old value.
 */
static void gen_evex_scalar_upper(DisasContext *s, X86DecodedInsn *decode, int esz)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    X86DecodedOp *op1 = &decode->op[1];
    int t1 = offsetof(CPUX86State, evex_t1), src = -1, ofs, sz;
    TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);

    if (decode->e.evex_dsrc) {
        src = ZMM_OFFSET(decode->op[0].n);
    } else if (op1->unit == X86_OP_SSE && !op1->has_ea) {
        src = ZMM_OFFSET(op1->n);
    }
    for (ofs = 1 << esz; ofs < 16; ofs += sz) {
        sz = (ofs & 7) == 0 ? 8 : (ofs & 3) == 0 ? 4 : (ofs & 1) == 0 ? 2 : 1;
        if (src < 0) {
            tcg_gen_movi_i64(tcg_ctx, t, 0);
        } else if (sz == 8) {
            tcg_gen_ld_i64(tcg_ctx, t, cpu_env, src + ofs);
        } else if (sz == 4) {
            tcg_gen_ld32u_i64(tcg_ctx, t, cpu_env, src + ofs);
        } else if (sz == 2) {
            tcg_gen_ld16u_i64(tcg_ctx, t, cpu_env, src + ofs);
        } else {
            tcg_gen_ld8u_i64(tcg_ctx, t, cpu_env, src + ofs);
        }
        if (sz == 8) {
            tcg_gen_st_i64(tcg_ctx, t, cpu_env, t1 + ofs);
        } else if (sz == 4) {
            tcg_gen_st32_i64(tcg_ctx, t, cpu_env, t1 + ofs);
        } else if (sz == 2) {
            tcg_gen_st16_i64(tcg_ctx, t, cpu_env, t1 + ofs);
        } else {
            tcg_gen_st8_i64(tcg_ctx, t, cpu_env, t1 + ofs);
        }
    }
    tcg_temp_free_i64(tcg_ctx, t);
}
#endif /* __Use_Original_Qemu (U192) */

#if __Use_Original_Qemu != 1 /* ours (U210) */
/* NoVmp (ledger U210): copy / zero env bytes for destinations of 2..64 bytes */
static void gen_evex_copy(DisasContext *s, int dst, int src, int bytes)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (bytes >= 8) {
        tcg_gen_gvec_mov(tcg_ctx, MO_64, dst, src, bytes, bytes);
    } else {
        TCGv_i32 t = tcg_temp_new_i32(tcg_ctx);

        if (bytes == 4) {
            tcg_gen_ld_i32(tcg_ctx, t, cpu_env, src);
            tcg_gen_st_i32(tcg_ctx, t, cpu_env, dst);
        } else {
            tcg_gen_ld16u_i32(tcg_ctx, t, cpu_env, src);
            tcg_gen_st16_i32(tcg_ctx, t, cpu_env, dst);
        }
        tcg_temp_free_i32(tcg_ctx, t);
    }
}

/* DEST[MAXVL-1:from] := 0 for a destination register at env offset dst; from = 2..64 */
static void gen_evex_zero_from(DisasContext *s, int dst, int from)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i64 z = tcg_constant_i64(tcg_ctx, 0);

    while (from < 16) {
        switch (from) {
        case 2:
            tcg_gen_st16_i64(tcg_ctx, z, cpu_env, dst + 2);
            break;
        case 4:
            tcg_gen_st32_i64(tcg_ctx, z, cpu_env, dst + 4);
            break;
        default:
            tcg_gen_st_i64(tcg_ctx, z, cpu_env, dst + 8);
            break;
        }
        from *= 2;
    }
    if (from < (int)sizeof(ZMMReg)) {
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, dst + from, sizeof(ZMMReg) - from,
                             sizeof(ZMMReg) - from, 0);
    }
}
#endif /* __Use_Original_Qemu (U210) */
#if __Use_Original_Qemu != 1 /* ours (U213) */

/*
 * NoVmp (ledger U213): expand / compress (VPEXPANDD/Q, VEXPANDPS/PD, VPCOMPRESSD/Q,
 * VCOMPRESSPS/PD; Tuple1 Scalar memory, E4.nb). The memory side is contiguous: only its
 * first popcount(k1) elements (all KL without a writemask) are read / written, so a masked
 * access never touches - or faults on - the elements beyond them. Expand masks the
 * destination per element; compress merges / zeroes the tail of a register destination.
 */
static void gen_evex_cx(DisasContext *s, X86DecodedInsn *decode, TCGv_i64 kmask)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    X86DecodedOp *dst = &decode->op[0], *src = &decode->op[2];
    int vl = s->evex_vl, esz = s->evex_esz, n = vl >> esz;
    uint32_t desc = EVEX_DESC(esz, n);
    TCGv_i64 m = kmask ? kmask : tcg_constant_i64(tcg_ctx, -1);
    TCGv_i64 lm = NULL;
    TCGv_ptr pd, ps;

    if (dst->has_ea || src->has_ea) {
        /* the first popcount(k1 & (2^KL - 1)) elements of the memory operand */
        lm = tcg_temp_new_i64(tcg_ctx);
        /*
         * NoVmp (ledger U551, was: (1 << n) - 1 and 1 << popcount): VPEXPANDB / VPCOMPRESSB
         * at 512 bits have KL = 64 elements: no shift by 64 (C and TCG leave it undefined):
         * (1 << ceil(c/2)) << floor(c/2) is 2^c for c = 0..64 (0 for c = 64: all ones)
         */
        {
            TCGv_i64 h = tcg_temp_new_i64(tcg_ctx);

            tcg_gen_andi_i64(tcg_ctx, lm, m, n >= 64 ? ~0ull : (1ull << n) - 1);
            tcg_gen_ctpop_i64(tcg_ctx, lm, lm);
            tcg_gen_shri_i64(tcg_ctx, h, lm, 1);
            tcg_gen_sub_i64(tcg_ctx, lm, lm, h);
            tcg_gen_shl_i64(tcg_ctx, lm, tcg_constant_i64(tcg_ctx, 1), lm);
            tcg_gen_shl_i64(tcg_ctx, lm, lm, h);
            tcg_gen_subi_i64(tcg_ctx, lm, lm, 1);
            tcg_temp_free_i64(tcg_ctx, h);
        }
    }
    if (decode->e.evex_cx == X86_EVEX_CX_EXPAND) {
        int sofs = src->has_ea ? offsetof(CPUX86State, xmm_t0) : ZMM_OFFSET(src->n);

        if (src->has_ea) {
            ps = evex_env_ptr(s, sofs);
            gen_helper_evex_mload(tcg_ctx, cpu_env, ps, s->A0, lm,
                                  tcg_constant_i32(tcg_ctx, desc));
            tcg_temp_free_ptr(tcg_ctx, ps);
        }
        pd = evex_env_ptr(s, ZMM_OFFSET(dst->n));
        ps = evex_env_ptr(s, sofs);
        gen_helper_evex_expand(tcg_ctx, cpu_env, pd, ps, m,
                               tcg_constant_i32(tcg_ctx, desc | (s->evex_z ? EVEX_DESC_Z : 0)));
        tcg_temp_free_ptr(tcg_ctx, pd);
        tcg_temp_free_ptr(tcg_ctx, ps);
        gen_evex_zero_from(s, ZMM_OFFSET(dst->n), vl);
    } else if (dst->has_ea) {
        pd = evex_env_ptr(s, offsetof(CPUX86State, evex_t1));
        ps = evex_env_ptr(s, ZMM_OFFSET(src->n));
        gen_helper_evex_compress(tcg_ctx, cpu_env, pd, ps, m,
                                 tcg_constant_i32(tcg_ctx, desc | EVEX_DESC_Z));
        gen_helper_evex_mstore(tcg_ctx, cpu_env, pd, s->A0, lm,
                               tcg_constant_i32(tcg_ctx, desc));
        tcg_temp_free_ptr(tcg_ctx, pd);
        tcg_temp_free_ptr(tcg_ctx, ps);
    } else {
        pd = evex_env_ptr(s, ZMM_OFFSET(dst->n));
        ps = evex_env_ptr(s, ZMM_OFFSET(src->n));
        gen_helper_evex_compress(tcg_ctx, cpu_env, pd, ps, m,
                                 tcg_constant_i32(tcg_ctx, desc | (s->evex_z ? EVEX_DESC_Z : 0)));
        tcg_temp_free_ptr(tcg_ctx, pd);
        tcg_temp_free_ptr(tcg_ctx, ps);
        gen_evex_zero_from(s, ZMM_OFFSET(dst->n), vl);
    }
    if (lm) {
        tcg_temp_free_i64(tcg_ctx, lm);
    }
}

/* the gen function of the expand / compress forms: everything is done by gen_evex_cx */
static void gen_EVEX_CX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
}
#endif /* __Use_Original_Qemu (U213) */
#if __Use_Original_Qemu != 1 /* ours (U991) */

/*
 * NoVmp (ledgers U991, U992): the 4-iteration Xeon Phi forms (SDM 325383-092 Vol2D 8-4 .. 8-7,
 * 8-16 .. 8-19; X86OpEntry.evex_blk4). "The entire memory operand is loaded if any of the 16
 * lowest significant mask bits is set to 1 or if a 'no masking' encoding is used" (the SS forms:
 * "the least significant mask bit"), else nothing is read and nothing faults (memory fault
 * suppression); a 16-byte access never reports #AC (E2/E4: "2, 4, or 8 byte memory access").
 * helper_evex_4blk then computes the destination register (merging / zeroing per element; SS:
 * DEST[511:128] := 0) from the four block registers zmm(vvvv & ~3) .. +3; the FP forms check
 * the MXCSR exceptions after each step there (#XM before the destination is written).
 * desc (fpu_helper.c EVEX_4BLK_*): bits 2:0 the operation (0 VP4DPWSSD, 1 VP4DPWSSDS, 2
 * V4FMADDPS, 3 V4FNMADDPS, 4 V4FMADDSS, 5 V4FNMADDSS), bit 8 zeroing-masking, bit 9 DEST is
 * one of the block registers, bits 11:10 its position in the block.
 */
static void gen_evex_4blk(DisasContext *s, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int dst = decode->op[0].n, base = decode->op[1].n & ~3;
    bool scalar = (decode->e.evex_vl & X86_EVEX_LIG) != 0;
    TCGv_i64 k = tcg_temp_new_i64(tcg_ctx);
    TCGv_i64 any = tcg_temp_new_i64(tcg_ctx);
    TCGv_ptr pd, pb, pm;
    uint32_t desc = s->evex_z ? 1u << 8 : 0;

    switch (decode->b) {
    case 0x52:
        desc |= 0;
        break;
    case 0x53:
        desc |= 1;
        break;
    case 0x9a:
        desc |= 2;
        break;
    case 0xaa:
        desc |= 3;
        break;
    case 0x9b:
        desc |= 4;
        break;
    default:            /* 0xab */
        desc |= 5;
        break;
    }
    if (dst >= base && dst < base + 4) {
        desc |= (1u << 9) | ((uint32_t)(dst - base) << 10);
    }
    if (s->evex_aaa) {
        tcg_gen_ld_i64(tcg_ctx, k, cpu_env, offsetof(CPUX86State, opmask_regs[s->evex_aaa]));
    } else {
        tcg_gen_movi_i64(tcg_ctx, k, -1);
    }
    gen_load_ea(s, &decode->mem, false);
    /* both quadwords of the operand when an element is active, else none */
    tcg_gen_andi_i64(tcg_ctx, any, k, scalar ? 1 : 0xffff);
    tcg_gen_setcondi_i64(tcg_ctx, TCG_COND_NE, any, any, 0);
    tcg_gen_neg_i64(tcg_ctx, any, any);
    pm = evex_env_ptr(s, offsetof(CPUX86State, xmm_t0));
    gen_helper_evex_mload(tcg_ctx, cpu_env, pm, s->A0, any,
                          tcg_constant_i32(tcg_ctx, EVEX_DESC(MO_64, 2)));
    pd = evex_env_ptr(s, ZMM_OFFSET(dst));
    pb = evex_env_ptr(s, ZMM_OFFSET(base));
    gen_helper_evex_4blk(tcg_ctx, cpu_env, pd, pb, pm, k, tcg_constant_i32(tcg_ctx, desc));
    tcg_temp_free_ptr(tcg_ctx, pd);
    tcg_temp_free_ptr(tcg_ctx, pb);
    tcg_temp_free_ptr(tcg_ctx, pm);
    tcg_temp_free_i64(tcg_ctx, any);
    tcg_temp_free_i64(tcg_ctx, k);
}

/* the gen functions of the 4-iteration forms: everything is done by gen_evex_4blk */
static void gen_EVEX_VP4DPWSSD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
}

static void gen_EVEX_VP4DPWSSDS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
}
#if __Use_Original_Qemu != 1 /* ours (U992) */

static void gen_EVEX_V4FMADDPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
}

static void gen_EVEX_V4FNMADDPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
}

static void gen_EVEX_V4FMADDSS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
}

static void gen_EVEX_V4FNMADDSS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
}
#endif /* __Use_Original_Qemu (U992) */
#endif /* __Use_Original_Qemu (U991) */

static void gen_evex_insn(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    X86OpEntry *e = &decode->e;
    X86DecodedOp *op0 = &decode->op[0];
    /*
     * esz: element size of the sources (neutral lanes of a masked FP source); U230: dsz =
     * element size of the destination and of the writemask (blend, scalar merge, DEST as a
     * source; = esz unless X86OpEntry.evex_ds: conversions, and U260's VPACKSSDW/VPACKUSDW
     * word writemask). A destination narrower than VL (VPMOVWB: VL/2, VCVTPD2PS: VL/2) comes
     * from evex_dest_bytes (U210); vl stays the vector length.
     */
    int vl = s->evex_vl, esz = s->evex_esz;
#if __Use_Original_Qemu != 1 /* ours (U230) */
    int dsz = s->evex_dsz;
#endif /* __Use_Original_Qemu (U230) */
    int nelem = evex_nelem(s, decode);
    bool fp = e->evex_fp;
    bool has_mem = op0->has_ea || decode->op[1].has_ea || decode->op[2].has_ea;
    TCGv_i64 kmask = NULL;
    bool to_scratch = false;
#if __Use_Original_Qemu != 1 /* ours (U192) */
    /* scalar form with a vector register destination (gen_evex_scalar_upper) */
    bool scalar = (e->evex_vl & X86_EVEX_LIG) && op0->unit == X86_OP_SSE && !op0->has_ea;
#endif /* __Use_Original_Qemu (U192) */
    int i;

#if __Use_Original_Qemu != 1 /* ours (U250) */
    /*
     * NoVmp (ledger U250): gathers/scatters (VSIB, class E12) do their own addressing,
     * element-wise memory access and opmask update (gen_evex_vsib).
     */
    if (e->vex_class == 12) {
        e->gen(s, env, decode);
        return;
    }
#endif /* __Use_Original_Qemu (U250) */
#if __Use_Original_Qemu != 1 /* ours (U991) */
    /* NoVmp (ledger U991): the 4-iteration Xeon Phi forms (gen_evex_4blk) */
    if (e->evex_blk4) {
        gen_evex_4blk(s, decode);
        return;
    }
#endif /* __Use_Original_Qemu (U991) */
    if (has_mem) {
        gen_load_ea(s, &decode->mem, e->vex_class == 12);
        if (e->evex_align) {
            gen_helper_evex_align(tcg_ctx, cpu_env, s->A0, tcg_constant_i32(tcg_ctx, vl - 1));
        }
    }
    if (s->evex_aaa) {
        kmask = tcg_temp_new_i64(tcg_ctx);
        tcg_gen_ld_i64(tcg_ctx, kmask, cpu_env,
                       offsetof(CPUX86State, opmask_regs[s->evex_aaa]));
    }
#if __Use_Original_Qemu != 1 /* ours (U213) */
    if (e->evex_cx) {
        gen_evex_cx(s, decode, kmask);
        if (kmask) {
            tcg_temp_free_i64(tcg_ctx, kmask);
        }
        return;
    }
#endif /* __Use_Original_Qemu (U213) */

    /* sources */
    for (i = 1; i < 3; i++) {
        X86DecodedOp *op = &decode->op[i];

        if (op->unit == X86_OP_SSE) {
            compute_xmm_offset(op);
            if (op->has_ea) {
                gen_evex_load(s, decode, op, kmask);
            }
        } else {
            gen_load(s, decode, i, i == 1 ? s->T0 : s->T1);
        }
    }

    /* destination: the scratch register evex_t1 unless written directly */
    if (op0->unit == X86_OP_SSE) {
        compute_xmm_offset(op0);
        to_scratch = op0->has_ea || kmask || fp;
#if __Use_Original_Qemu != 1 /* ours (U190) */
        to_scratch |= e->evex_dsrc;
#endif /* __Use_Original_Qemu (U190) */
#if __Use_Original_Qemu != 1 /* ours (U192) */
        to_scratch |= scalar;
#endif /* __Use_Original_Qemu (U192) */
        if (to_scratch) {
            op0->offset = offsetof(CPUX86State, evex_t1) + xmm_offset(op0->ot);
        }
    }
#if __Use_Original_Qemu != 1 /* ours (U190) */

    /*
     * NoVmp (ledger U190): a destination that is also a source (FMA "DEST := DEST*SRC3 +
     * SRC2" ...; SDM Vol2C, merging-masking keeps DEST) is read through the result scratch
     * register: evex_t1 := DEST, with +1.0 in the masked-off lanes of an FP form (no MXCSR
     * flag, no #XM from them, as for the other sources), so the gen function computes in
     * place and the masking below merges with the old destination register.
     */
    if (e->evex_dsrc && op0->unit == X86_OP_SSE && !op0->has_ea) {
        int t1 = offsetof(CPUX86State, evex_t1);

        if (fp && kmask) {
            TCGv_ptr pd = evex_env_ptr(s, t1);
            TCGv_ptr ps = evex_env_ptr(s, ZMM_OFFSET(op0->n));

            gen_helper_evex_neutral(tcg_ctx, cpu_env, pd, ps, kmask,
                                    tcg_constant_i32(tcg_ctx, EVEX_DESC(dsz, nelem)));
            tcg_temp_free_ptr(tcg_ctx, pd);
            tcg_temp_free_ptr(tcg_ctx, ps);
        } else {
            tcg_gen_gvec_mov(tcg_ctx, MO_64, t1, ZMM_OFFSET(op0->n), vl, vl);
        }
    }
#endif /* __Use_Original_Qemu (U190) */

    /* floating point with a mask: neutral (1.0) masked-off lanes in copies of the sources */
    if (fp && kmask) {
        for (i = 1; i < 3; i++) {
            X86DecodedOp *op = &decode->op[i];
            int scr;
            TCGv_ptr pd, ps;

            if (op->unit != X86_OP_SSE) {
                continue;
            }
            scr = op->has_ea ? offsetof(CPUX86State, xmm_t0)
                             : i == 1 ? offsetof(CPUX86State, evex_s1)
                                      : offsetof(CPUX86State, evex_s2);
            pd = evex_env_ptr(s, scr);
            ps = evex_env_ptr(s, vector_reg_offset(op));
            gen_helper_evex_neutral(tcg_ctx, cpu_env, pd, ps, kmask,
                                    tcg_constant_i32(tcg_ctx, EVEX_DESC(esz, nelem)));
            tcg_temp_free_ptr(tcg_ctx, pd);
            tcg_temp_free_ptr(tcg_ctx, ps);
            op->offset = scr + xmm_offset(op->ot);
        }
    }

    if (fp) {
        gen_evex_fp_begin(s);
    }
    e->gen(s, env, decode);
    if (fp) {
        gen_evex_fp_end(s);
    }

    /* masking and writeback */
    switch (op0->unit) {
    case X86_OP_SSE:
        if (op0->has_ea) {
            int bytes = evex_mem_bytes(op0);
            int n = (e->evex_vl & X86_EVEX_LIG) ? 1 : nelem;
            TCGv_ptr p = evex_env_ptr(s, offsetof(CPUX86State, evex_t1));
            uint32_t desc = EVEX_DESC(ctz32(bytes / n), n);

#if __Use_Original_Qemu != 1 /* ours (U193) */
            /*
             * NoVmp (ledger U193; U210 merged in): a masked store without fault suppression
             * (E*NF, e.g. E6NF VEXTRACTF32X4 m128{k1}): the whole operand faults like an
             * unmasked one (SDM Vol2A Table 2-56 "#PF: For a page fault"), only the active
             * elements are written (helper_evex_mstore, EVEX_DESC_NOFS). Without a mask
             * every element is probed and written anyway.
             */
            if (kmask && e->evex_nofs) {
                desc |= EVEX_DESC_NOFS;
            }
#endif /* __Use_Original_Qemu (U193) */
            gen_helper_evex_mstore(tcg_ctx, cpu_env, p, s->A0,
                                   kmask ? kmask : tcg_constant_i64(tcg_ctx, -1),
                                   tcg_constant_i32(tcg_ctx, desc | EVEX_AC(s)));
            tcg_temp_free_ptr(tcg_ctx, p);
        } else {
            int dst = ZMM_OFFSET(op0->n);
            /* a destination narrower than VL (U210): its own length, zeroed above it */
            int dbytes = evex_dest_bytes(s, decode);

#if __Use_Original_Qemu != 1 /* ours (U192) */
            if (scalar) {
                gen_evex_scalar_upper(s, decode, dsz);   /* U230: destination size */
            }
#endif /* __Use_Original_Qemu (U192) */
#if __Use_Original_Qemu != 1 /* ours (U191) */
            /*
             * NoVmp (ledger U191): VPBLENDMD/Q, VBLENDMPS/PD (SDM Vol2C): a masked-off lane
             * takes SRC1[j] under merging-masking ("ELSE IF *merging-masking* THEN DEST :=
             * SRC1"), 0 under zeroing-masking; no mask (aaa = 000b) selects SRC2 everywhere.
             * The gen function left SRC2 in evex_t1, so DEST := SRC1 first, then the blend.
             */
            if (e->evex_msrc1 && kmask && !s->evex_z) {
                tcg_gen_gvec_mov(tcg_ctx, MO_64, dst, ZMM_OFFSET(decode->op[1].n), vl, vl);
            }
#endif /* __Use_Original_Qemu (U191) */
            if (kmask) {
                TCGv_ptr pd = evex_env_ptr(s, dst);
                TCGv_ptr ps = evex_env_ptr(s, offsetof(CPUX86State, evex_t1));

                gen_helper_evex_blend(tcg_ctx, cpu_env, pd, ps, kmask,
                                      tcg_constant_i32(tcg_ctx, EVEX_DESC(dsz, nelem) |
                                                       (s->evex_z ? EVEX_DESC_Z : 0)));
                tcg_temp_free_ptr(tcg_ctx, pd);
                tcg_temp_free_ptr(tcg_ctx, ps);
#if __Use_Original_Qemu != 1 /* ours (U192) */
                if (scalar) {
                    /* element 0 blended; DEST[127:dsz] from the scratch register (U230) */
                    tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env,
                                   offsetof(CPUX86State, evex_t1) + 8);
                    tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, dst + 8);
                    if (dsz < MO_64) {
                        tcg_gen_ld32u_i64(tcg_ctx, s->tmp1_i64, cpu_env,
                                          offsetof(CPUX86State, evex_t1) + 4);
                        tcg_gen_st32_i64(tcg_ctx, s->tmp1_i64, cpu_env, dst + 4);
                    }
                    if (dsz < MO_32) {
                        tcg_gen_ld16u_i64(tcg_ctx, s->tmp1_i64, cpu_env,
                                          offsetof(CPUX86State, evex_t1) + 2);
                        tcg_gen_st16_i64(tcg_ctx, s->tmp1_i64, cpu_env, dst + 2);
                    }
                    if (dsz < MO_16) {
                        tcg_gen_ld8u_i64(tcg_ctx, s->tmp1_i64, cpu_env,
                                         offsetof(CPUX86State, evex_t1) + 1);
                        tcg_gen_st8_i64(tcg_ctx, s->tmp1_i64, cpu_env, dst + 1);
                    }
                }
#endif /* __Use_Original_Qemu (U192) */
            } else if (to_scratch) {
                /* U210: dbytes (was vl) */
                gen_evex_copy(s, dst, offsetof(CPUX86State, evex_t1), dbytes);
            }
            /* U210: DEST[MAXVL-1:dbytes] := 0 (was: from vl) */
            gen_evex_zero_from(s, dst, dbytes);
        }
        break;
    case X86_OP_KREG:
        if (kmask) {
            tcg_gen_and_i64(tcg_ctx, s->tmp1_i64, s->tmp1_i64, kmask);
        }
        tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env,
                       offsetof(CPUX86State, opmask_regs[op0->n]));
        break;
    default:
        gen_writeback(s, decode, 0, s->T0);
        break;
    }
    if (kmask) {
        tcg_temp_free_i64(tcg_ctx, kmask);
    }
}
#endif /* __Use_Original_Qemu (U146) */
#if __Use_Original_Qemu != 1 /* ours (U148) */

/* NoVmp (ledger U148): EVEX vector moves; gen_evex_insn does the masking and the memory */
static void gen_EVEX_MOV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vl = vector_len(s, decode);

    tcg_gen_gvec_mov(tcg_ctx, MO_64, decode->op[0].offset, decode->op[2].offset, vl, vl);
}
#endif /* __Use_Original_Qemu (U148) */
#if __Use_Original_Qemu != 1 /* ours (U151) */

/*
 * NoVmp (ledger U151): VADDPS/PD, VSUBPS/PD, VMULPS/PD on 128/256/512 bits (SSE_FN3:
 * the SHIFT 1/2/3 helpers of ops_sse.h, same MXCSR/DAZ/FTZ rules as SSE/AVX); masking,
 * {1toN} and {er} come from gen_evex_insn.
 */
#define EVEX_FP_PACKED(uname, lname)                                                 \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                            \
    SSEFunc_0_eppp fn = (s->prefix & PREFIX_DATA) ? SSE_FN3(s, decode, lname##pd)    \
                                                  : SSE_FN3(s, decode, lname##ps);   \
                                                                                     \
    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);                                 \
}
EVEX_FP_PACKED(VADD, add)
EVEX_FP_PACKED(VSUB, sub)
EVEX_FP_PACKED(VMUL, mul)
#endif /* __Use_Original_Qemu (U151) */
#if __Use_Original_Qemu != 1 /* ours (U152) */

/*
 * NoVmp (ledger U152): VPCMPEQD/Q, VPCMPD/Q, VPCMPUD/UQ into an opmask register
 * (helper_evex_pcmp: one bit per element, bits above the element count 0); the AND with
 * k2 is done by gen_evex_insn. SRC1 (vvvv) OP SRC2 (r/m) with imm8[2:0]: 0 EQ, 1 LT,
 * 2 LE, 3 FALSE, 4 NEQ, 5 NLT, 6 NLE, 7 TRUE; imm8[7:3] reserved (SDM Vol2C VPCMPD/VPCMPUD,
 * VPCMPQ/VPCMPUQ, Table 5-19); VPCMPEQD/Q = predicate 0.
 */
#define EVEX_PCMP_SIGNED (1u << 19)

static void gen_evex_pcmp(DisasContext *s, X86DecodedInsn *decode, int pred, bool sign)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = EVEX_DESC(s->evex_esz, evex_nelem(s, decode)) |
                    ((uint32_t)(pred & 7) << 16) | (sign ? EVEX_PCMP_SIGNED : 0);

    gen_helper_evex_pcmp(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR1, OP_PTR2,
                         tcg_constant_i32(tcg_ctx, desc));
}

static void gen_EVEX_VPCMPEQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pcmp(s, decode, 0, true);
}

static void gen_EVEX_VPCMP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pcmp(s, decode, decode->immediate, true);
}

static void gen_EVEX_VPCMPU(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pcmp(s, decode, decode->immediate, false);
}
#endif /* __Use_Original_Qemu (U152) */
#if __Use_Original_Qemu != 1 /* ours (U154) */

/*
 * NoVmp (ledger U154): VPCMPGTD/Q (signed NLE), VPTESTMD/Q (SRC1 AND SRC2 != 0) and
 * VPTESTNMD/Q (SRC1 AND SRC2 = 0) into an opmask register (helper_evex_pcmp, U152).
 */
#define EVEX_PCMP_TEST (1u << 20)

static void gen_EVEX_VPCMPGT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pcmp(s, decode, 6, true);
}

static void gen_EVEX_VPTESTM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = EVEX_DESC(s->evex_esz, evex_nelem(s, decode)) | (4u << 16) | EVEX_PCMP_TEST;

    gen_helper_evex_pcmp(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR1, OP_PTR2,
                         tcg_constant_i32(tcg_ctx, desc));
}

static void gen_EVEX_VPTESTNM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = EVEX_DESC(s->evex_esz, evex_nelem(s, decode)) | EVEX_PCMP_TEST;

    gen_helper_evex_pcmp(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR1, OP_PTR2,
                         tcg_constant_i32(tcg_ctx, desc));
}
#endif /* __Use_Original_Qemu (U154) */
#if __Use_Original_Qemu != 1 /* ours (U155) */

/*
 * NoVmp (ledger U155): integer forms whose element size is EVEX.W (gvec) and the
 * dword x dword -> qword multiplies (ops_sse.h pmuludq/pmuldq, SSE_FN3).
 */
#define EVEX_INT_GVEC3(uname, func)                                                  \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                            \
    int vl = vector_len(s, decode);                                                  \
                                                                                     \
    func(tcg_ctx, s->evex_esz, decode->op[0].offset, decode->op[1].offset,           \
         decode->op[2].offset, vl, vl);                                              \
}
EVEX_INT_GVEC3(PMINS, tcg_gen_gvec_smin)
EVEX_INT_GVEC3(PMINU, tcg_gen_gvec_umin)
EVEX_INT_GVEC3(PMAXS, tcg_gen_gvec_smax)
EVEX_INT_GVEC3(PMAXU, tcg_gen_gvec_umax)

static void gen_EVEX_PABS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vl = vector_len(s, decode);

    tcg_gen_gvec_abs(tcg_ctx, s->evex_esz, decode->op[0].offset, decode->op[2].offset, vl, vl);
}

static void gen_EVEX_PMULUDQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, pmuludq)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_EVEX_PMULDQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, pmuldq)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}
#endif /* __Use_Original_Qemu (U155) */
#if __Use_Original_Qemu != 1 /* ours (U156) */

/* NoVmp (ledger U156): VDIVPS/PD, VMINPS/PD, VMAXPS/PD (SSE_FN3), VSQRTPS/PD */
EVEX_FP_PACKED(VDIV, div)
EVEX_FP_PACKED(VMIN, min)
EVEX_FP_PACKED(VMAX, max)

static void gen_EVEX_VSQRT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_epp fn = (s->prefix & PREFIX_DATA) ? SSE_FN3(s, decode, sqrtpd)
                                                 : SSE_FN3(s, decode, sqrtps);

    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
}
#endif /* __Use_Original_Qemu (U156) */
#if __Use_Original_Qemu != 1 /* ours (U157) */

/*
 * NoVmp (ledger U157): VPSLLVD/Q, VPSRLVD/Q, VPSRAVD/Q (VPSRAVQ is new in AVX-512): a
 * count >= the element width gives 0 / the sign (ops_sse.h vpsxxv helpers, SSE_FN3).
 */
#define EVEX_VSHIFT(uname, lname)                                                    \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                            \
    SSEFunc_0_eppp fn = s->vex_w ? SSE_FN3(s, decode, lname##q)                      \
                                 : SSE_FN3(s, decode, lname##d);                     \
                                                                                     \
    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);                                 \
}
EVEX_VSHIFT(VPSLLV, vpsllv)
EVEX_VSHIFT(VPSRLV, vpsrlv)
EVEX_VSHIFT(VPSRAV, vpsrav)
#endif /* __Use_Original_Qemu (U157) */
#if __Use_Original_Qemu != 1 /* ours (U158) */

/*
 * NoVmp (ledger U158): shifts and rotates by imm8 (destination = EVEX.vvvv = op[0], source
 * op[1]): a count >= the element width gives 0 (logical) or the sign (arithmetic); rotates
 * use the count modulo the width (SDM Vol2B PSLLW/PSLLD/PSLLQ, PSRAW/PSRAD/PSRAQ,
 * PSRLW/PSRLD/PSRLQ; Vol2C PROLD/PROLQ/PRORD/PRORQ).
 */
static void gen_evex_shift_imm(DisasContext *s, X86DecodedInsn *decode, int kind)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vl = vector_len(s, decode), esz = s->evex_esz, bits = 8 << esz;
    int d = decode->op[0].offset, a = decode->op[1].offset;
    unsigned cnt = decode->immediate & 0xff;

    switch (kind) {
    case 0: /* left */
    case 1: /* logical right */
        if (cnt >= (unsigned)bits) {
            tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, d, vl, vl, 0);
        } else if (kind == 0) {
            tcg_gen_gvec_shli(tcg_ctx, esz, d, a, cnt, vl, vl);
        } else {
            tcg_gen_gvec_shri(tcg_ctx, esz, d, a, cnt, vl, vl);
        }
        break;
    case 2: /* arithmetic right */
        tcg_gen_gvec_sari(tcg_ctx, esz, d, a, cnt >= (unsigned)bits ? bits - 1 : cnt, vl, vl);
        break;
    default: /* 3: rotate left, 4: rotate right */
        cnt &= bits - 1;
        if (kind == 4) {
            cnt = (bits - cnt) & (bits - 1);
        }
        if (cnt == 0) {
            tcg_gen_gvec_mov(tcg_ctx, MO_64, d, a, vl, vl);
        } else {
            int t = offsetof(CPUX86State, evex_s2);

            tcg_gen_gvec_shri(tcg_ctx, esz, t, a, bits - cnt, vl, vl);
            tcg_gen_gvec_shli(tcg_ctx, esz, d, a, cnt, vl, vl);
            tcg_gen_gvec_or(tcg_ctx, MO_64, d, d, t, vl, vl);
        }
        break;
    }
}

static void gen_EVEX_VPSLLI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_shift_imm(s, decode, 0);
}

static void gen_EVEX_VPSRLI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_shift_imm(s, decode, 1);
}

static void gen_EVEX_VPSRAI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_shift_imm(s, decode, 2);
}

static void gen_EVEX_VPROLI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_shift_imm(s, decode, 3);
}

static void gen_EVEX_VPRORI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_shift_imm(s, decode, 4);
}
#endif /* __Use_Original_Qemu (U158) */
#if __Use_Original_Qemu != 1 /* ours (U159) */

/*
 * NoVmp (ledger U159): VPTERNLOGD/Q: bit = imm8[(DEST << 2) | (SRC2 << 1) | SRC3] with
 * DEST the destination register before the instruction, SRC2 = EVEX.vvvv, SRC3 = r/m /
 * {1toN}. U868: DEST comes in OP_PTR0 (X86OpEntry.evex_dsrc, U190: gen_evex_insn copies the
 * register to the result scratch), which is also the result.
 */
static void gen_EVEX_VPTERNLOG(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_pternlog(tcg_ctx, cpu_env, OP_PTR0, OP_PTR0, OP_PTR1, OP_PTR2,
                             tcg_constant_i32(tcg_ctx, (decode->immediate & 0xff) |
                                                       (vector_len(s, decode) << 8)));
}
#endif /* __Use_Original_Qemu (U159) */
#if __Use_Original_Qemu != 1 /* ours (U153) */

/* NoVmp (ledger U153): VPBROADCASTD r32 / VPBROADCASTQ r64 (EVEX.66.0F38 7C) */
static void gen_EVEX_VPBROADCAST_R(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vl = vector_len(s, decode);
    TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);

    tcg_gen_extu_tl_i64(tcg_ctx, t, s->T1);
    tcg_gen_gvec_dup_i64(tcg_ctx, s->evex_esz, decode->op[0].offset, vl, vl, t);
    tcg_temp_free_i64(tcg_ctx, t);
}
#endif /* __Use_Original_Qemu (U153) */
#if __Use_Original_Qemu != 1 /* ours (U250) */

/*
 * NoVmp (ledger U250): EVEX gathers and scatters, exception class E12 (SDM Vol2A 2.8.11;
 * Vol2C VPGATHERDD/DQ, VPGATHERQD/QQ, VGATHERDPS/DPD, VGATHERQPS/QPD, VPSCATTER*, VSCATTER*).
 * The helper walks the elements in element order under k1 = EVEX.aaa (never k0,
 * validate_evex) and clears k1[j] as element j completes, so a fault leaves the completed
 * elements done and their mask bits clear, with RIP at the instruction (restartable).
 * Element j's address: (BASE + DISP + SignExtend(VINDEX[j]) << SCALE) truncated to the
 * address size, plus the segment base by the rules of every memory operand (gen_lea_v_seg
 * of 0 gives that base), truncated to 32 bits outside 64-bit mode.
 * desc (fpu_helper.c EVEX_VSIB_*): bits 2:0 aaa, bit 3 qword data (EVEX.W), bit 4 qword
 * indices (opcode bit 0), bits 15:8 KL = VL / max(data size, index size), bits 17:16
 * SIB.scale, bit 18 64-bit address size, bit 19 32-bit linear addresses.
 */
typedef void (*EvexVsibHelper)(TCGContext *tcg_ctx, TCGv_ptr env, TCGv_ptr reg,
                               TCGv_ptr index, TCGv a0, TCGv seg, TCGv_i32 desc);

static void gen_evex_vsib(DisasContext *s, X86DecodedInsn *decode, X86DecodedOp *vreg,
                          EvexVsibHelper helper)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    bool qidx = decode->b & 1;
    int dsz = s->vex_w ? 8 : 4, isz = qidx ? 8 : 4;
    int kl = s->evex_vl / (dsz > isz ? dsz : isz);
    uint32_t desc = s->evex_aaa | (s->vex_w ? 8u : 0) | (qidx ? 16u : 0) | ((uint32_t)kl << 8) |
                    ((uint32_t)decode->mem.scale << 16) |
                    (s->aflag == MO_64 ? 1u << 18 : 0) | (CODE64(s) ? 0 : 1u << 19);
    TCGv ea = tcg_temp_new(tcg_ctx);
    TCGv zero = tcg_constant_tl(tcg_ctx, 0);
    TCGv_ptr preg = evex_env_ptr(s, ZMM_OFFSET(vreg->n));
    TCGv_ptr pidx = evex_env_ptr(s, ZMM_OFFSET(decode->mem.index));

    tcg_gen_mov_tl(tcg_ctx, ea, gen_lea_modrm_1(s, decode->mem, true));   /* BASE + DISP */
    gen_lea_v_seg(s, s->aflag, zero, decode->mem.def_seg, s->override);   /* A0 = seg base */
    helper(tcg_ctx, cpu_env, preg, pidx, ea, s->A0, tcg_constant_i32(tcg_ctx, desc));
    tcg_temp_free_ptr(tcg_ctx, preg);
    tcg_temp_free_ptr(tcg_ctx, pidx);
    tcg_temp_free(tcg_ctx, zero);    /* tcg_constant_* is a tcg_const_* shim here */
    tcg_temp_free(tcg_ctx, ea);
}

/* gathers: destination ModRM.reg (op0), DEST[MAXVL-1:KL*data size] := 0 on completion */
static void gen_EVEX_VGATHER(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_vsib(s, decode, &decode->op[0], gen_helper_evex_gather);
}
#endif /* __Use_Original_Qemu (U250) */
#if __Use_Original_Qemu != 1 /* ours (U251) */

/* NoVmp (ledger U251): scatters: source ModRM.reg (op2), written in element order */
static void gen_EVEX_VSCATTER(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_vsib(s, decode, &decode->op[2], gen_helper_evex_scatter);
}
#endif /* __Use_Original_Qemu (U251) */
#if __Use_Original_Qemu != 1 /* ours (U993) */

/*
 * NoVmp (ledger U993): AVX512PF VGATHERPF0/1 and VSCATTERPF0/1 DPS/QPS/DPD/QPD (SDM 325383-092
 * Vol2D 8-12 .. 8-15, 8-36 .. 8-39, class E12NP): prefetch hints without architectural effect
 * ("may happen in any order (or not at all)", "No FP nor memory faults may be produced", "The
 * mask is left unchanged"): no memory access, no fault, no state change; the #UD conditions
 * are decoded (decode_EVEX_0F38_C6, validate_evex).
 */
static void gen_EVEX_PREFETCHPF(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
}
#endif /* __Use_Original_Qemu (U993) */
#if __Use_Original_Qemu != 1 /* ours (U194) */

/*
 * NoVmp (ledger U194): VADDSS/SD, VSUBSS/SD, VMULSS/SD, VDIVSS/SD ({er}), VMINSS/SD,
 * VMAXSS/SD ({sae}), VSQRTSS/SD ({er}): element 0 by the SSE scalar helpers (same MXCSR,
 * DAZ/FTZ, NaN and #XM rules as SSE/AVX); F3 = single, F2 = double. k1[0] masking,
 * DEST[127:esz] := SRC1[127:esz] and the zeroing above bit 127 come from gen_evex_insn.
 */
#define EVEX_FP_SCALAR(uname, lname)                                                 \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                            \
    SSEFunc_0_eppp fn = (s->prefix & PREFIX_REPNZ) ? gen_helper_##lname##sd          \
                                                   : gen_helper_##lname##ss;         \
                                                                                     \
    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);                                 \
}
EVEX_FP_SCALAR(VADDS, add)
EVEX_FP_SCALAR(VSUBS, sub)
EVEX_FP_SCALAR(VMULS, mul)
EVEX_FP_SCALAR(VDIVS, div)
EVEX_FP_SCALAR(VMINS, min)
EVEX_FP_SCALAR(VMAXS, max)
EVEX_FP_SCALAR(VSQRTS, sqrt)
#undef EVEX_FP_SCALAR
#endif /* __Use_Original_Qemu (U194) */
#if __Use_Original_Qemu != 1 /* ours (U195) */

/*
 * NoVmp (ledger U195): VMOVSS/VMOVSD (SDM Vol2B MOVSS/MOVSD, class E10): element 0 of the
 * source (register, or memory read only when k1[0] = 1 / no mask) into element 0 of the
 * destination; the register forms take DEST[127:esz] from SRC1 (EVEX.vvvv), the load form
 * zeroes it (gen_evex_insn), the store form writes m32/m64 only when k1[0] = 1.
 */
static void gen_EVEX_VMOVS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (s->evex_esz == MO_64) {
        tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[2].offset);
        tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset);
    } else {
        tcg_gen_ld32u_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[2].offset);
        tcg_gen_st32_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset);
    }
}
#endif /* __Use_Original_Qemu (U195) */
#if __Use_Original_Qemu != 1 /* ours (U196) */

/*
 * NoVmp (ledger U196): VCOMISS/SD (signalling: #IA on any NaN) and VUCOMISS/SD (#IA on an
 * SNaN only) into ZF/PF/CF, OF/SF/AF cleared (SDM Vol2A COMISS/COMISD, Vol2B UCOMISS/
 * UCOMISD; E3NF); {sae} suppresses the flags and #XM (gen_evex_insn).
 */
static void gen_EVEX_VCOMI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_epp fn = (s->prefix & PREFIX_DATA) ? gen_helper_comisd : gen_helper_comiss;

    fn(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2);
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_EVEX_VUCOMI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_epp fn = (s->prefix & PREFIX_DATA) ? gen_helper_ucomisd : gen_helper_ucomiss;

    fn(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2);
    set_cc_op(s, CC_OP_EFLAGS);
}
#endif /* __Use_Original_Qemu (U196) */
#if __Use_Original_Qemu != 1 /* ours (U197) */

/*
 * NoVmp (ledger U197): VCMPPS/PD (U197) and VCMPSS/SD (U198) into an opmask register:
 * helper_evex_fcmp with the predicate imm8[4:0] (imm8[7:5] reserved, ignored as in the
 * VEX forms); the AND with k2 and DEST[MAX_KL-1:KL] := 0 are done by gen_evex_insn.
 */
static void gen_EVEX_VCMP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = EVEX_DESC(s->evex_esz, evex_nelem(s, decode)) |
                    ((uint32_t)(decode->immediate & 31) << 16);

    gen_helper_evex_fcmp(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR1, OP_PTR2,
                         tcg_constant_i32(tcg_ctx, desc));
}
#endif /* __Use_Original_Qemu (U197) */
#if __Use_Original_Qemu != 1 /* ours (U199) */

/*
 * NoVmp (ledger U199): EVEX FMA, packed (0F38 96-98/9A/9C/9E, A6-A8/AA/AC/AE, B6-B8/BA/BC/
 * BE; U199) and scalar (99/9B/9D/9F, A9/.., B9/..; U200). Opcode bits 7:4 = operand order
 * (9: 132 DEST*SRC3 op SRC2, A: 213 SRC2*DEST op SRC3, B: 231 SRC2*SRC3 op DEST), bits 3:0
 * = operation (6 FMADDSUB, 7 FMSUBADD, 8/9 FMADD, A/B FMSUB, C/D FNMADD, E/F FNMSUB). One
 * rounding of the infinitely precise x*y+z (softfloat muladd, SDM Vol1 14.5.2); NaN result
 * Q(x), Q(y), Q(z) in that order, 0*inf+QNaN returns Q(z) without #IA (Table 14-17), the
 * negations do not apply to a NaN. DEST is read from the scratch copy made by
 * gen_evex_insn (X86OpEntry.evex_dsrc), so OP_PTR0 is both a source and the result.
 */
static void gen_evex_fma_args(DisasContext *s, X86DecodedInsn *decode, TCGv_ptr *a,
                              TCGv_ptr *b, TCGv_ptr *c, int *even, int *odd)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr p0 = OP_PTR0, p1 = OP_PTR1, p2 = OP_PTR2;

    switch (decode->b >> 4) {
    case 0x9:       /* 132: DEST * SRC3 + SRC2 */
        *a = p0, *b = p2, *c = p1;
        break;
    case 0xa:       /* 213: SRC2 * DEST + SRC3 */
        *a = p1, *b = p0, *c = p2;
        break;
    default:        /* 231: SRC2 * SRC3 + DEST */
        *a = p1, *b = p2, *c = p0;
        break;
    }
    switch (decode->b & 0xe) {
    case 0x6:       /* FMADDSUB: even elements subtract, odd add */
        *even = (decode->b & 1) ? 0 : float_muladd_negate_c;
        *odd = (decode->b & 1) ? float_muladd_negate_c : 0;
        break;
    case 0x8:       /* FMADD */
        *even = *odd = 0;
        break;
    case 0xa:       /* FMSUB */
        *even = *odd = float_muladd_negate_c;
        break;
    case 0xc:       /* FNMADD */
        *even = *odd = float_muladd_negate_product;
        break;
    default:        /* 0xe: FNMSUB */
        *even = *odd = float_muladd_negate_c | float_muladd_negate_product;
        break;
    }
}

static void gen_EVEX_VFMAP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_eppppii fn = s->vex_w ? SSE_FN3(s, decode, fma4pd) : SSE_FN3(s, decode, fma4ps);
    TCGv_ptr a, b, c;
    int even, odd;

    gen_evex_fma_args(s, decode, &a, &b, &c, &even, &odd);
    fn(tcg_ctx, cpu_env, OP_PTR0, a, b, c, tcg_constant_i32(tcg_ctx, even),
       tcg_constant_i32(tcg_ctx, even ^ odd));
}
#endif /* __Use_Original_Qemu (U199) */
#if __Use_Original_Qemu != 1 /* ours (U200) */

static void gen_EVEX_VFMAS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_eppppi fn = s->vex_w ? gen_helper_fma4sd : gen_helper_fma4ss;
    TCGv_ptr a, b, c;
    int even, odd;

    gen_evex_fma_args(s, decode, &a, &b, &c, &even, &odd);
    fn(tcg_ctx, cpu_env, OP_PTR0, a, b, c, tcg_constant_i32(tcg_ctx, even));
}
#endif /* __Use_Original_Qemu (U200) */
#if __Use_Original_Qemu != 1 /* ours (U211) */

/*
 * NoVmp (ledger U211): EVEX permutes / shuffles / unpacks through helper_evex_perm: op[0]
 * the destination (or the masking scratch), op[1] SRC1 = EVEX.vvvv (absent in the
 * two-operand forms), op[2] SRC2 = r/m ({1toN} replicated by gen_evex_load); the
 * destination register as it was before the instruction is the fourth operand: for
 * VPERMI2/VPERMT2 (X86OpEntry.evex_dsrc, U868) OP_PTR0 itself, which gen_evex_insn loaded
 * with DEST (U190); the other forms do not read it (a placeholder pointer).
 */
static void gen_evex_perm(DisasContext *s, X86DecodedInsn *decode, int op, int aux)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr pc = decode->e.evex_dsrc ? NULL :
                  evex_env_ptr(s, ZMM_OFFSET(decode->op[0].n >= 0 ? decode->op[0].n : 0));
    TCGv_ptr c = pc ? pc : OP_PTR0;
    TCGv_ptr pa = decode->op[1].unit == X86_OP_SSE ? OP_PTR1 : c;

    gen_helper_evex_perm(tcg_ctx, cpu_env, OP_PTR0, pa, OP_PTR2, c,
                         tcg_constant_i32(tcg_ctx,
                                          EVEX_PERM_DESC(op, decode->immediate,
                                                         vector_len(s, decode),
                                                         s->evex_esz, aux)));
    if (pc) {
        tcg_temp_free_ptr(tcg_ctx, pc);
    }
}

#define EVEX_PERM_GEN(uname, opc)                                                     \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    gen_evex_perm(s, decode, opc, 0);                                                \
}
EVEX_PERM_GEN(UNPCKL, EVEX_PERM_UNPCKL)
EVEX_PERM_GEN(UNPCKH, EVEX_PERM_UNPCKH)
EVEX_PERM_GEN(VPSHUFD, EVEX_PERM_PSHUFD)
EVEX_PERM_GEN(VPERMILPD_I, EVEX_PERM_PERMILPD_I)
EVEX_PERM_GEN(VPERMILPS_V, EVEX_PERM_PERMILPS_V)
EVEX_PERM_GEN(VPERMILPD_V, EVEX_PERM_PERMILPD_V)
EVEX_PERM_GEN(VPERM, EVEX_PERM_PERM)
EVEX_PERM_GEN(VPERMQ_I, EVEX_PERM_PERMQ_I)
EVEX_PERM_GEN(VPERMI2, EVEX_PERM_PERMI2)
EVEX_PERM_GEN(VPERMT2, EVEX_PERM_PERMT2)
EVEX_PERM_GEN(VALIGN, EVEX_PERM_ALIGN)
EVEX_PERM_GEN(VSHUF128, EVEX_PERM_SHUF128)

/* VSHUFPS (NP) / VSHUFPD (66) */
static void gen_EVEX_VSHUFP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_perm(s, decode, (s->prefix & PREFIX_DATA) ? EVEX_PERM_SHUFPD : EVEX_PERM_SHUFPS, 0);
}
#endif /* __Use_Original_Qemu (U211) */
#if __Use_Original_Qemu != 1 /* ours (U212) */

/*
 * NoVmp (ledger U212): VPMOVZX/VPMOVSX (66 0F38 21-25 / 31-35) and the narrowing
 * VPMOVUS* / VPMOVS* / VPMOV* (F3 0F38 11-15 / 21-25 / 31-35). Opcode low nibble: 1 DB / BD,
 * 2 QB / BQ, 3 DW / WD, 4 QW / WQ, 5 QD / DQ. The table gives the destination element
 * size (evex_es); the source / destination of a narrowing VPMOV is narrower than VL
 * (Half/Quarter/Eighth Mem) and gen_evex_insn masks per destination element.
 */
static void gen_EVEX_PMOVX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    static const uint8_t src_esz[6] = { 0, MO_8, MO_8, MO_16, MO_16, MO_32 };

    gen_evex_perm(s, decode, (decode->b >> 4) == 2 ? EVEX_PERM_PMOVSX : EVEX_PERM_PMOVZX,
                  src_esz[decode->b & 7]);
}

static void gen_EVEX_PMOVN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    static const uint8_t src_esz[6] = { 0, MO_32, MO_64, MO_32, MO_64, MO_64 };
    static const uint8_t ops[4] = {
        0, EVEX_PERM_PMOVUSAT, EVEX_PERM_PMOVSSAT, EVEX_PERM_PMOVTRUNC
    };
    TCGv_ptr pc = evex_env_ptr(s, ZMM_OFFSET(0));

    gen_helper_evex_perm(tcg_ctx, cpu_env, OP_PTR0, pc, OP_PTR2, pc,
                         tcg_constant_i32(tcg_ctx,
                                          EVEX_PERM_DESC(ops[(decode->b >> 4) & 3], 0,
                                                         vector_len(s, decode),
                                                         src_esz[decode->b & 7],
                                                         s->evex_esz)));
    tcg_temp_free_ptr(tcg_ctx, pc);
}
#endif /* __Use_Original_Qemu (U212) */
#if __Use_Original_Qemu != 1 /* ours (U214) */

/*
 * NoVmp (ledger U214): VMOVSLDUP / VMOVDDUP (F3 / F2 0F 12) and VMOVSHDUP (F3 0F 16)
 * (SDM Vol2B MOVSLDUP/MOVDDUP/MOVSHDUP: element 2i and 2i+1 both from SRC[2i] / SRC[2i+1]).
 */
static void gen_EVEX_DUP_EVEN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_perm(s, decode, EVEX_PERM_DUP_EVEN, 0);
}

static void gen_EVEX_DUP_ODD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_perm(s, decode, EVEX_PERM_DUP_ODD, 0);
}

/*
 * NoVmp (ledger U214): 128-bit qword moves, no masking (E9NF / E7NM.128), DEST[MAXVL-1:128]
 * := 0 by gen_evex_insn. Both qwords are read before the destination is written.
 *   VMOVLPS/VMOVLPD xmm2, xmm1, m64: DEST = m64 : SRC1[127:64]   (q0 from op2, q1 from op1)
 *   VMOVHPS/VMOVHPD xmm2, xmm1, m64: DEST = SRC1[63:0] : m64     (q0 from op1, q1 from op2)
 *   VMOVHLPS xmm1, xmm2, xmm3:       DEST = SRC2[127:64] : SRC1[127:64]
 *   VMOVLHPS xmm1, xmm2, xmm3:       DEST = SRC1[63:0] : SRC2[63:0]
 */
static void gen_evex_q2(DisasContext *s, X86DecodedInsn *decode, int op_lo, int q_lo,
                        int op_hi, int q_hi)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i64 lo = tcg_temp_new_i64(tcg_ctx), hi = tcg_temp_new_i64(tcg_ctx);
    int d = vector_reg_offset(&decode->op[0]);

    tcg_gen_ld_i64(tcg_ctx, lo, cpu_env,
                   vector_reg_offset(&decode->op[op_lo]) + offsetof(ZMMReg, ZMM_Q(q_lo)));
    tcg_gen_ld_i64(tcg_ctx, hi, cpu_env,
                   vector_reg_offset(&decode->op[op_hi]) + offsetof(ZMMReg, ZMM_Q(q_hi)));
    tcg_gen_st_i64(tcg_ctx, lo, cpu_env, d + offsetof(ZMMReg, ZMM_Q(0)));
    tcg_gen_st_i64(tcg_ctx, hi, cpu_env, d + offsetof(ZMMReg, ZMM_Q(1)));
    tcg_temp_free_i64(tcg_ctx, lo);
    tcg_temp_free_i64(tcg_ctx, hi);
}

static void gen_EVEX_MOVLP_LD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_q2(s, decode, 2, 0, 1, 1);
}

static void gen_EVEX_MOVHP_LD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_q2(s, decode, 1, 0, 2, 0);
}

static void gen_EVEX_MOVHLPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_q2(s, decode, 2, 1, 1, 1);
}

static void gen_EVEX_MOVLHPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_q2(s, decode, 1, 0, 2, 0);
}

/*
 * VMOVLPS/VMOVLPD m64, xmm1 (0F 13): SRC[63:0]; VMOVHPS/VMOVHPD m64, xmm1 (0F 17):
 * SRC[127:64]; VMOVQ xmm1/m64, xmm2 (66 0F D6 W1) and VMOVQ xmm1, xmm2/m64 (F3 0F 7E W1):
 * SRC[63:0] (a register destination is zeroed above bit 63: a destination narrower than VL
 * for D6, op[0] = 128 bits with the upper qword zeroed here for 7E).
 */
static void gen_evex_movq_from(DisasContext *s, X86DecodedInsn *decode, int q, bool zero_hi)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);
    int d = vector_reg_offset(&decode->op[0]);

    tcg_gen_ld_i64(tcg_ctx, t, cpu_env,
                   vector_reg_offset(&decode->op[2]) + offsetof(ZMMReg, ZMM_Q(q)));
    tcg_gen_st_i64(tcg_ctx, t, cpu_env, d + offsetof(ZMMReg, ZMM_Q(0)));
    if (zero_hi) {
        tcg_gen_st_i64(tcg_ctx, tcg_constant_i64(tcg_ctx, 0), cpu_env,
                       d + offsetof(ZMMReg, ZMM_Q(1)));
    }
    tcg_temp_free_i64(tcg_ctx, t);
}

static void gen_EVEX_MOVLP_ST(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_movq_from(s, decode, 0, false);
}

static void gen_EVEX_MOVHP_ST(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_movq_from(s, decode, 1, false);
}

static void gen_EVEX_MOVQ_XX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_movq_from(s, decode, 0, decode->op[0].ot >= MO_128);
}

/*
 * VMOVD xmm1, r32/m32 / VMOVQ xmm1, r64/m64 (66 0F 6E): DEST[31:0 / 63:0] := SRC,
 * DEST[MAXVL-1:32 / 64] := 0. The GPR / memory value is in T1 (gen_evex_insn).
 */
static void gen_EVEX_MOVD_TO(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int d = vector_reg_offset(&decode->op[0]);
    TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);

    if (decode->op[2].ot == MO_64) {
        tcg_gen_extu_tl_i64(tcg_ctx, t, s->T1);
    } else {
        tcg_gen_extu_tl_i64(tcg_ctx, t, s->T1);
        tcg_gen_ext32u_i64(tcg_ctx, t, t);
    }
    tcg_gen_st_i64(tcg_ctx, t, cpu_env, d + offsetof(ZMMReg, ZMM_Q(0)));
    tcg_gen_st_i64(tcg_ctx, tcg_constant_i64(tcg_ctx, 0), cpu_env, d + offsetof(ZMMReg, ZMM_Q(1)));
    tcg_temp_free_i64(tcg_ctx, t);
}

/* VMOVD r32/m32, xmm1 / VMOVQ r64/m64, xmm1 (66 0F 7E): SRC[31:0 / 63:0] into T0 */
static void gen_EVEX_MOVD_FROM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int src = vector_reg_offset(&decode->op[2]);

    if (decode->op[0].ot == MO_64) {
        tcg_gen_ld_tl(tcg_ctx, s->T0, cpu_env, src + offsetof(ZMMReg, ZMM_Q(0)));
    } else {
        tcg_gen_ld32u_tl(tcg_ctx, s->T0, cpu_env, src + offsetof(ZMMReg, ZMM_L(0)));
    }
}
#endif /* __Use_Original_Qemu (U214) */
#if __Use_Original_Qemu != 1 /* ours (U215) */

/*
 * NoVmp (ledger U215): VINSERTF32X4/F64X2/F32X8/F64X4, VINSERTI..., VEXTRACTF/I...,
 * VBROADCASTF/I32X2/32X4/64X2/32X8/64X4 (helper_evex_perm; the chunk / group size in bytes
 * is the size of the r/m operand, or of the destination for VEXTRACT). A register source of
 * VBROADCASTx32X2 uses its low 64 bits.
 */
static void gen_EVEX_VINSERT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_perm(s, decode, EVEX_PERM_INSERT, ctz32(evex_mem_bytes(&decode->op[2])));
}

static void gen_EVEX_VEXTRACT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_perm(s, decode, EVEX_PERM_EXTRACT, ctz32(evex_mem_bytes(&decode->op[0])));
}

static void gen_EVEX_VBCASTX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_perm(s, decode, EVEX_PERM_BCAST, ctz32(evex_mem_bytes(&decode->op[2])));
}

/* VPINSRD/VPINSRQ xmm1, xmm2, r/m, imm8: SRC1 with dword/qword imm8[1:0]/[0] := r/m (T1) */
static void gen_EVEX_VPINSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int d = vector_reg_offset(&decode->op[0]);

    tcg_gen_gvec_mov(tcg_ctx, MO_64, d, vector_reg_offset(&decode->op[1]), 16, 16);
    if (decode->op[2].ot == MO_64) {
        tcg_gen_st_tl(tcg_ctx, s->T1, cpu_env,
                      d + offsetof(ZMMReg, ZMM_Q(decode->immediate & 1)));
    } else {
        tcg_gen_st32_tl(tcg_ctx, s->T1, cpu_env,
                        d + offsetof(ZMMReg, ZMM_L(decode->immediate & 3)));
    }
}

/* VPEXTRD/VPEXTRQ r/m, xmm2, imm8 and VEXTRACTPS r/m32, xmm1, imm8: the element into T0 */
static void gen_EVEX_VPEXTR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int src = vector_reg_offset(&decode->op[2]);

    if (decode->op[0].ot == MO_64) {
        tcg_gen_ld_tl(tcg_ctx, s->T0, cpu_env,
                      src + offsetof(ZMMReg, ZMM_Q(decode->immediate & 1)));
    } else {
        tcg_gen_ld32u_tl(tcg_ctx, s->T0, cpu_env,
                         src + offsetof(ZMMReg, ZMM_L(decode->immediate & 3)));
    }
}

/*
 * VINSERTPS xmm1, xmm2, xmm3/m32, imm8 (SDM Vol2A INSERTPS): TMP := register source
 * dword imm8[7:6] (memory: m32); DEST := SRC1 with dword imm8[5:4] := TMP; then the dwords
 * selected by imm8[3:0] are zeroed; DEST[MAXVL-1:128] := 0 (gen_evex_insn).
 */
static void gen_EVEX_VINSERTPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int imm = decode->immediate & 0xff, d = vector_reg_offset(&decode->op[0]), i;
    int count_s = decode->op[2].has_ea ? 0 : (imm >> 6) & 3;
    TCGv_i32 t = tcg_temp_new_i32(tcg_ctx);

    tcg_gen_ld_i32(tcg_ctx, t, cpu_env,
                   vector_reg_offset(&decode->op[2]) + offsetof(ZMMReg, ZMM_L(count_s)));
    tcg_gen_gvec_mov(tcg_ctx, MO_64, d, vector_reg_offset(&decode->op[1]), 16, 16);
    tcg_gen_st_i32(tcg_ctx, t, cpu_env, d + offsetof(ZMMReg, ZMM_L((imm >> 4) & 3)));
    for (i = 0; i < 4; i++) {
        if (imm & (1 << i)) {
            tcg_gen_st_i32(tcg_ctx, tcg_constant_i32(tcg_ctx, 0), cpu_env,
                           d + offsetof(ZMMReg, ZMM_L(i)));
        }
    }
    tcg_temp_free_i32(tcg_ctx, t);
}
#endif /* __Use_Original_Qemu (U215) */
#if __Use_Original_Qemu != 1 /* ours (U262) */

/*
 * NoVmp (ledger U262): AVX512BW byte/word arithmetic. The gvec forms (VPADDB/W, VPADDS*,
 * VPADDUS*, VPSUB*, VPMINS/U B/W, VPMAXS/U B/W, VPABSB/W, VPMULLW) reuse the VEX gen
 * functions (vector_len() = the EVEX vector length); the ops_sse.h forms take the SHIFT
 * 1/2/3 helper by vector length (SSE_FN3): DEST = op(SRC1 = vvvv, SRC2 = r/m).
 */
#define EVEX_SSE3(uname, lname)                                                      \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                            \
                                                                                     \
    SSE_FN3(s, decode, lname)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);          \
}
EVEX_SSE3(PAVGB, pavgb)
EVEX_SSE3(PAVGW, pavgw)
EVEX_SSE3(PMULHUW, pmulhuw)
EVEX_SSE3(PMULHW, pmulhw)
EVEX_SSE3(PMULHRSW, pmulhrsw)
#endif /* __Use_Original_Qemu (U262) */
#if __Use_Original_Qemu != 1 /* ours (U263) */

/*
 * NoVmp (ledger U263): VPMADDWD (dword writemask), VPMADDUBSW (SRC1 unsigned bytes, SRC2
 * signed bytes, signed word saturation), VPSADBW (no writemask): ops_sse.h helpers by
 * vector length; VDBPSADBW: helper_evex_dbpsadbw (imm8 | VL << 8).
 */
EVEX_SSE3(PMADDWD, pmaddwd)
EVEX_SSE3(PMADDUBSW, pmaddubsw)
EVEX_SSE3(PSADBW, psadbw)

static void gen_EVEX_VDBPSADBW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_dbpsadbw(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                             tcg_constant_i32(tcg_ctx, (decode->immediate & 0xff) |
                                                       (vector_len(s, decode) << 8)));
}
#endif /* __Use_Original_Qemu (U263) */
#if __Use_Original_Qemu != 1 /* ours (U264) */

/*
 * NoVmp (ledger U264): byte/word shuffles, unpacks and packs, all within 128-bit lanes
 * (SDM Vol2B PSHUFB, PSHUFHW, PSHUFLW, PALIGNR, PUNPCKLBW/WD, PUNPCKHBW/WD, PACKSSWB/
 * SSDW, PACKUSWB, PACKUSDW): the ops_sse.h helper of the vector length (SHIFT 3 for 512).
 */
EVEX_SSE3(PSHUFB, pshufb)
EVEX_SSE3(PUNPCKLBW, punpcklbw)
EVEX_SSE3(PUNPCKHBW, punpckhbw)
EVEX_SSE3(PUNPCKLWD, punpcklwd)
EVEX_SSE3(PUNPCKHWD, punpckhwd)
EVEX_SSE3(PACKSSWB, packsswb)
EVEX_SSE3(PACKUSWB, packuswb)
EVEX_SSE3(PACKSSDW, packssdw)
EVEX_SSE3(PACKUSDW, packusdw)

/* VPSHUFHW / VPSHUFLW zmm1{k1}{z}, zmm2/m512, imm8: op[1] = r/m source */
static void gen_EVEX_PSHUFHW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, pshufhw)(tcg_ctx, OP_PTR0, OP_PTR1,
                                tcg_constant8u_i32(tcg_ctx, decode->immediate));
}

static void gen_EVEX_PSHUFLW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, pshuflw)(tcg_ctx, OP_PTR0, OP_PTR1,
                                tcg_constant8u_i32(tcg_ctx, decode->immediate));
}

/* VPALIGNR: per lane (SRC1:SRC2) >> imm8 * 8, 0 for imm8 >= 32 */
static void gen_EVEX_PALIGNR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, palignr)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                                tcg_constant8u_i32(tcg_ctx, decode->immediate));
}
#endif /* __Use_Original_Qemu (U264) */
#if __Use_Original_Qemu != 1 /* ours (U265) */

/*
 * NoVmp (ledger U265): word shifts (SDM Vol2B PSLLW/PSRAW/PSRLW, PSLLDQ, PSRLDQ; Vol2C
 * VPSLLVW/VPSRAVW/VPSRLVW). By imm8 (71 /2 /4 /6, vvvv = destination): gen_evex_shift_imm
 * (U158) with 16-bit elements. By xmm3/m128 (D1/E1/F1): count = bits 63:0, above 15 the
 * result is 0 / the sign (ops_sse.h helpers). By a word count per element: helper_evex_
 * pshiftvw. VPSLLDQ/VPSRLDQ (73 /7 /3): each 128-bit lane by imm8 bytes (> 15: 0).
 */
EVEX_SSE3(PSRLW_r, psrlw)
EVEX_SSE3(PSRAW_r, psraw)
EVEX_SSE3(PSLLW_r, psllw)

static void gen_evex_pshiftvw(DisasContext *s, X86DecodedInsn *decode, int kind)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_pshiftvw(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                             tcg_constant_i32(tcg_ctx, kind | (vector_len(s, decode) << 8)));
}

static void gen_EVEX_VPSLLVW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pshiftvw(s, decode, 0);
}

static void gen_EVEX_VPSRLVW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pshiftvw(s, decode, 1);
}

static void gen_EVEX_VPSRAVW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pshiftvw(s, decode, 2);
}

/* byte shift count in evex_s2.L(0) (xmm_t0 may hold the memory source) */
static void gen_evex_bytesh(DisasContext *s, X86DecodedInsn *decode, bool left)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr cnt = evex_env_ptr(s, offsetof(CPUX86State, evex_s2));

    tcg_gen_st_i32(tcg_ctx, tcg_constant_i32(tcg_ctx, decode->immediate & 0xff), cpu_env,
                   offsetof(CPUX86State, evex_s2.ZMM_L(0)));
    if (left) {
        SSE_FN3(s, decode, pslldq)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, cnt);
    } else {
        SSE_FN3(s, decode, psrldq)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, cnt);
    }
    tcg_temp_free_ptr(tcg_ctx, cnt);
}

static void gen_EVEX_VPSLLDQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_bytesh(s, decode, true);
}

static void gen_EVEX_VPSRLDQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_bytesh(s, decode, false);
}
#endif /* __Use_Original_Qemu (U265) */
#if __Use_Original_Qemu != 1 /* ours (U266) */

/*
 * NoVmp (ledger U266): VPCMP[U]B/W, VPCMPEQB/W, VPCMPGTB/W, VPTESTMB/W, VPTESTNMB/W reuse
 * the U152/U154 gen functions with 8/16-bit elements (up to 64 result bits, ANDed with k2
 * by gen_evex_insn). VPMOVB2M / VPMOVW2M: bit j = the sign bit of element j ((SRC AND
 * SRC) < 0, signed, helper_evex_pcmp). VPMOVM2B / VPMOVM2W: helper_evex_movm2v.
 */
static void gen_EVEX_VPMOV2M(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = EVEX_DESC(s->evex_esz, evex_nelem(s, decode)) | (1u << 16) |
                    EVEX_PCMP_SIGNED | EVEX_PCMP_TEST;

    gen_helper_evex_pcmp(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR2, OP_PTR2,
                         tcg_constant_i32(tcg_ctx, desc));
}

static void gen_EVEX_VPMOVM2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i64 k = tcg_temp_new_i64(tcg_ctx);

    tcg_gen_extu_tl_i64(tcg_ctx, k, s->T1);
    gen_helper_evex_movm2v(tcg_ctx, cpu_env, OP_PTR0, k,
                           tcg_constant_i32(tcg_ctx, EVEX_DESC(s->evex_esz,
                                                               evex_nelem(s, decode))));
    tcg_temp_free_i64(tcg_ctx, k);
}
#endif /* __Use_Original_Qemu (U266) */
#if __Use_Original_Qemu != 1 /* ours (U268) */

/*
 * NoVmp (ledger U268): VPMOVZXBW / VPMOVSXBW (word destination from VL/2 source bytes,
 * ops_sse.h pmovzxbw/pmovsxbw of the vector length) and VPMOVWB / VPMOVSWB / VPMOVUSWB
 * (VL/2 destination bytes from the source words, helper_evex_pmovwb; the masking and the
 * zeroing of DEST[MAXVL-1:VL/2] use evex_narrow = 1, U260).
 */
static void gen_EVEX_PMOVZXBW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, pmovzxbw)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
}

static void gen_EVEX_PMOVSXBW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, pmovsxbw)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
}

static void gen_evex_pmovwb(DisasContext *s, X86DecodedInsn *decode, int kind)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_pmovwb(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2,
                           tcg_constant_i32(tcg_ctx, kind | ((s->evex_vl / 2) << 8)));
}

static void gen_EVEX_VPMOVWB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pmovwb(s, decode, 0);
}

static void gen_EVEX_VPMOVSWB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pmovwb(s, decode, 1);
}

static void gen_EVEX_VPMOVUSWB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pmovwb(s, decode, 2);
}
#endif /* __Use_Original_Qemu (U268) */
#if __Use_Original_Qemu != 1 /* ours (U269) */

/*
 * NoVmp (ledger U269): word permutes (helper_evex_vpermw, desc = VL | mode << 8).
 * VPERMW: indices SRC1 (vvvv), table SRC2. VPERMI2W: indices = the destination before the
 * instruction, tables SRC1 / SRC2. VPERMT2W: indices SRC1, tables = the destination before
 * the instruction / SRC2. The old destination comes in OP_PTR0 (X86OpEntry.evex_dsrc, U868;
 * gen_evex_insn loads the result scratch with it, U190).
 */
static void gen_EVEX_VPERMW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_vpermw(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, OP_PTR2,
                           tcg_constant_i32(tcg_ctx, vector_len(s, decode)));
}

static void gen_EVEX_VPERMI2W(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_vpermw(tcg_ctx, cpu_env, OP_PTR0, OP_PTR0, OP_PTR1, OP_PTR2,
                           tcg_constant_i32(tcg_ctx, vector_len(s, decode) | (1 << 8)));
}

static void gen_EVEX_VPERMT2W(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_vpermw(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR0, OP_PTR2,
                           tcg_constant_i32(tcg_ctx, vector_len(s, decode) | (1 << 8)));
}

/*
 * VPBLENDMB / VPBLENDMW (SDM Vol2C VPBLENDMB/VPBLENDMW): active elements (or all without
 * a writemask) = SRC2, masked-off elements = SRC1 with merging, 0 with zeroing: gen_EVEX_MOV
 * with X86OpEntry.evex_msrc1 (U191), as VPBLENDMD/Q (U201).
 */
#endif /* __Use_Original_Qemu (U269) */
#if __Use_Original_Qemu != 1 /* ours (U290) */

/* NoVmp (ledger U290): VPMULLQ: the low 64 bits of every 64 x 64-bit product */
static void gen_EVEX_PMULLQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vl = vector_len(s, decode);

    tcg_gen_gvec_mul(tcg_ctx, MO_64, decode->op[0].offset, decode->op[1].offset,
                     decode->op[2].offset, vl, vl);
}
#endif /* __Use_Original_Qemu (U290) */
#if __Use_Original_Qemu != 1 /* ours (U292) */

/*
 * NoVmp (ledger U292): VFPCLASSPS/PD/SS/SD: one bit per element (helper_evex_fpclass,
 * desc = EVEX_DESC | imm8 << 16) left in tmp1_i64; gen_evex_insn ANDs it with k1.
 */
static void gen_EVEX_VFPCLASS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = EVEX_DESC(s->evex_esz, evex_nelem(s, decode)) |
                    ((uint32_t)(decode->immediate & 0xff) << 16);

    gen_helper_evex_fpclass(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR1,
                            tcg_constant_i32(tcg_ctx, desc));
}
#endif /* __Use_Original_Qemu (U292) */
#if __Use_Original_Qemu != 1 /* ours (U293) */

/*
 * NoVmp (ledger U293): VRANGEPS/PD (helper_evex_range: desc = EVEX_DESC | imm8 << 16;
 * MXCSR flags IE/DE and #XM through gen_evex_insn's sse_fp_begin/end or {sae}).
 * U295: the scalar forms VRANGESS/SD compute element 0 and take DEST[127:esz] from SRC1,
 * the register itself (its masking copy only holds element 0).
 */
#define EVEX_DQ_SCALAR (1u << 24)

static void gen_EVEX_VRANGE(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = EVEX_DESC(s->evex_esz, evex_nelem(s, decode)) |
                    ((uint32_t)(decode->immediate & 0xff) << 16);

#if __Use_Original_Qemu != 1 /* ours (U295) */
    if (decode->e.evex_vl & X86_EVEX_LIG) {
        TCGv_ptr pu = evex_env_ptr(s, ZMM_OFFSET(decode->op[1].n));

        gen_helper_evex_range(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, pu,
                              tcg_constant_i32(tcg_ctx, desc | EVEX_DQ_SCALAR));
        tcg_temp_free_ptr(tcg_ctx, pu);
        return;
    }
#endif /* __Use_Original_Qemu (U295) */
    gen_helper_evex_range(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, OP_PTR1,
                          tcg_constant_i32(tcg_ctx, desc));
}
#endif /* __Use_Original_Qemu (U293) */
#if __Use_Original_Qemu != 1 /* ours (U294) */

/*
 * NoVmp (ledger U294): VREDUCEPS/PD, source = r/m (op[1]); helper_evex_reduce (desc =
 * EVEX_DESC | imm8 << 16), flags IE/PE. U296: VREDUCESS/SD, source = op[2], DEST[127:esz]
 * from the SRC1 register (op[1]).
 */
static void gen_EVEX_VREDUCE(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = EVEX_DESC(s->evex_esz, evex_nelem(s, decode)) |
                    ((uint32_t)(decode->immediate & 0xff) << 16);

#if __Use_Original_Qemu != 1 /* ours (U296) */
    if (decode->e.evex_vl & X86_EVEX_LIG) {
        TCGv_ptr pu = evex_env_ptr(s, ZMM_OFFSET(decode->op[1].n));

        gen_helper_evex_reduce(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2, pu,
                               tcg_constant_i32(tcg_ctx, desc | EVEX_DQ_SCALAR));
        tcg_temp_free_ptr(tcg_ctx, pu);
        return;
    }
#endif /* __Use_Original_Qemu (U296) */
    gen_helper_evex_reduce(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR1,
                           tcg_constant_i32(tcg_ctx, desc));
}
#endif /* __Use_Original_Qemu (U294) */
#if __Use_Original_Qemu != 1 /* ours (U231) */

/*
 * NoVmp (ledger U231): EVEX packed conversions (helper_evex_cvt, one element per opmask
 * bit; source and destination element sizes may differ, U230). Types as fpu_helper.c
 * EVCVT_*: 0 i32, 1 u32, 2 i64, 3 u64, 4 f16, 5 f32, 6 f64.
 */
#define EVCVT_DESC(st, dt, tr, n) ((uint32_t)(st) | ((uint32_t)(dt) << 4) | \
                                   ((uint32_t)(tr) << 8) | ((uint32_t)(n) << 9))

static void gen_evex_cvt(DisasContext *s, X86DecodedInsn *decode, int st, int dt, bool trunc)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_cvt(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2,
                        tcg_constant_i32(tcg_ctx, EVCVT_DESC(st, dt, trunc,
                                                             evex_nelem(s, decode))));
}

#define EVEX_CVT(uname, st, dt, tr)                                                  \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    gen_evex_cvt(s, decode, st, dt, tr);                                             \
}
EVEX_CVT(VCVTDQ2PS, 0, 5, 0)
EVEX_CVT(VCVTUDQ2PS, 1, 5, 0)
EVEX_CVT(VCVTPS2DQ, 5, 0, 0)
EVEX_CVT(VCVTPS2UDQ, 5, 1, 0)
EVEX_CVT(VCVTTPS2DQ, 5, 0, 1)
EVEX_CVT(VCVTTPS2UDQ, 5, 1, 1)
EVEX_CVT(VCVTPD2DQ, 6, 0, 0)
EVEX_CVT(VCVTPD2UDQ, 6, 1, 0)
EVEX_CVT(VCVTTPD2DQ, 6, 0, 1)
EVEX_CVT(VCVTTPD2UDQ, 6, 1, 1)
EVEX_CVT(VCVTDQ2PD, 0, 6, 0)
EVEX_CVT(VCVTUDQ2PD, 1, 6, 0)
EVEX_CVT(VCVTPS2PD, 5, 6, 0)
EVEX_CVT(VCVTPD2PS, 6, 5, 0)
EVEX_CVT(VCVTPH2PS, 4, 5, 0)
#if __Use_Original_Qemu != 1 /* ours (U233) */
/* NoVmp (ledger U233): AVX512DQ quadword conversions */
EVEX_CVT(VCVTQQ2PS, 2, 5, 0)
EVEX_CVT(VCVTUQQ2PS, 3, 5, 0)
EVEX_CVT(VCVTQQ2PD, 2, 6, 0)
EVEX_CVT(VCVTUQQ2PD, 3, 6, 0)
EVEX_CVT(VCVTPS2QQ, 5, 2, 0)
EVEX_CVT(VCVTPS2UQQ, 5, 3, 0)
EVEX_CVT(VCVTPD2QQ, 6, 2, 0)
EVEX_CVT(VCVTPD2UQQ, 6, 3, 0)
EVEX_CVT(VCVTTPS2QQ, 5, 2, 1)
EVEX_CVT(VCVTTPS2UQQ, 5, 3, 1)
EVEX_CVT(VCVTTPD2QQ, 6, 2, 1)
EVEX_CVT(VCVTTPD2UQQ, 6, 3, 1)
#endif /* __Use_Original_Qemu (U233) */

/* VCVTPS2PH: destination = ModRM.r/m (op0), source = ModRM.reg (op1), imm8 rounding */
static void gen_EVEX_VCVTPS2PH(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_cvt(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1,
                        tcg_constant_i32(tcg_ctx, EVCVT_DESC(5, 4, 0, evex_nelem(s, decode)) |
                                                  ((decode->immediate & 0xff) << 16) |
                                                  (1u << 24)));
}
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U232) */

/*
 * NoVmp (ledger U232): scalar EVEX conversions. SRC1 (vvvv) is passed as the register
 * itself (bits 127:element of the result), SRC2 as the operand (neutral copy under a mask).
 */
static TCGv_ptr gen_evex_src1_reg(DisasContext *s, X86DecodedInsn *decode)
{
    return evex_env_ptr(s, ZMM_OFFSET(decode->op[1].n));
}

static void gen_evex_cvt_ss(DisasContext *s, X86DecodedInsn *decode, int st, int dt)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr v = gen_evex_src1_reg(s, decode);

    gen_helper_evex_cvt_s(tcg_ctx, cpu_env, OP_PTR0, v, OP_PTR2,
                          tcg_constant_i32(tcg_ctx, EVCVT_DESC(st, dt, 0, 1)));
    tcg_temp_free_ptr(tcg_ctx, v);
}

static void gen_EVEX_VCVTSS2SD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_cvt_ss(s, decode, 5, 6);
}

static void gen_EVEX_VCVTSD2SS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_cvt_ss(s, decode, 6, 5);
}

/* VCVT[U]SI2SS/SD xmm1, xmm2, r/m32/64: the integer is in T1 (op2) */
static void gen_evex_cvt_i2f(DisasContext *s, X86DecodedInsn *decode, bool uns, int dt)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr v = gen_evex_src1_reg(s, decode);
    TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);
    int st = (decode->op[2].ot == MO_64 ? 2 : 0) + uns;

    tcg_gen_extu_tl_i64(tcg_ctx, t, s->T1);
    gen_helper_evex_cvt_i2f(tcg_ctx, cpu_env, OP_PTR0, v, t,
                            tcg_constant_i32(tcg_ctx, EVCVT_DESC(st, dt, 0, 1)));
    tcg_temp_free_i64(tcg_ctx, t);
    tcg_temp_free_ptr(tcg_ctx, v);
}

static void gen_EVEX_VCVTSI2SS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_cvt_i2f(s, decode, false, 5);
}

static void gen_EVEX_VCVTSI2SD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_cvt_i2f(s, decode, false, 6);
}

static void gen_EVEX_VCVTUSI2SS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_cvt_i2f(s, decode, true, 5);
}

static void gen_EVEX_VCVTUSI2SD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_cvt_i2f(s, decode, true, 6);
}

/* VCVT[T]SS/SD2[U]SI r32/r64, xmm/m: result in T0, written back by gen_evex_insn */
static void gen_evex_cvt_f2i(DisasContext *s, X86DecodedInsn *decode, int st, bool uns,
                             bool trunc)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int dt = (decode->op[0].ot == MO_64 ? 2 : 0) + uns;
    TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);

    gen_helper_evex_cvt_f2i(tcg_ctx, t, cpu_env, OP_PTR2,
                            tcg_constant_i32(tcg_ctx, EVCVT_DESC(st, dt, trunc, 1)));
    tcg_gen_trunc_i64_tl(tcg_ctx, s->T0, t);
    tcg_temp_free_i64(tcg_ctx, t);
}

#define EVEX_CVT_F2I(uname, st, uns, tr)                                             \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    gen_evex_cvt_f2i(s, decode, st, uns, tr);                                        \
}
EVEX_CVT_F2I(VCVTSS2SI, 5, false, false)
EVEX_CVT_F2I(VCVTSD2SI, 6, false, false)
EVEX_CVT_F2I(VCVTTSS2SI, 5, false, true)
EVEX_CVT_F2I(VCVTTSD2SI, 6, false, true)
EVEX_CVT_F2I(VCVTSS2USI, 5, true, false)
EVEX_CVT_F2I(VCVTSD2USI, 6, true, false)
EVEX_CVT_F2I(VCVTTSS2USI, 5, true, true)
EVEX_CVT_F2I(VCVTTSD2USI, 6, true, true)
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U234) */

/* NoVmp (ledger U234): VPSLLD/Q, VPSRLD/Q, VPSRAD/Q by the count in xmm3/m128 */
static void gen_evex_pshift(DisasContext *s, X86DecodedInsn *decode, int kind)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_pshift(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                           tcg_constant_i32(tcg_ctx, kind | (s->evex_esz << 4) |
                                                     (evex_nelem(s, decode) << 8)));
}

static void gen_EVEX_VPSLL_X(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pshift(s, decode, 0);
}

static void gen_EVEX_VPSRL_X(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pshift(s, decode, 1);
}

static void gen_EVEX_VPSRA_X(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_pshift(s, decode, 2);
}
#endif /* __Use_Original_Qemu (U234) */
#if __Use_Original_Qemu != 1 /* ours (U235) */

/* NoVmp (ledger U235): VPROLVD/Q, VPRORVD/Q */
static void gen_EVEX_VPROLV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_prolv(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                          tcg_constant_i32(tcg_ctx, (s->evex_esz << 4) |
                                                    (evex_nelem(s, decode) << 8)));
}

static void gen_EVEX_VPRORV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_prolv(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                          tcg_constant_i32(tcg_ctx, 1 | (s->evex_esz << 4) |
                                                    (evex_nelem(s, decode) << 8)));
}
#endif /* __Use_Original_Qemu (U235) */
#if __Use_Original_Qemu != 1 /* ours (U236) */

/*
 * NoVmp (ledger U236-U241): one-source element operations (helper_evex_fp1/_fp1s): 0
 * VRCP14, 1 VRSQRT14, 2 VGETEXP, 3 VGETMANT, 4 VRNDSCALE. Packed forms read the source from
 * op src (op2 for the rm forms, op1 for the rm+imm8 forms); scalar forms are xmm1, xmm2
 * (vvvv: bits 127:elem), xmm3/m (element 0).
 */
static void gen_evex_fp1(DisasContext *s, X86DecodedInsn *decode, int op, int src, int imm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = op | (s->evex_esz << 4) | (evex_nelem(s, decode) << 8) | ((imm & 0xff) << 16);

    if (decode->e.evex_vl & X86_EVEX_LIG) {
        TCGv_ptr v = evex_env_ptr(s, ZMM_OFFSET(decode->op[1].n));

        gen_helper_evex_fp1s(tcg_ctx, cpu_env, OP_PTR0, v, OP_PTR2,
                             tcg_constant_i32(tcg_ctx, desc));
        tcg_temp_free_ptr(tcg_ctx, v);
    } else {
        gen_helper_evex_fp1(tcg_ctx, cpu_env, OP_PTR0, src == 1 ? OP_PTR1 : OP_PTR2,
                            tcg_constant_i32(tcg_ctx, desc));
    }
}

static void gen_EVEX_VRCP14(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_fp1(s, decode, 0, 2, 0);
}

static void gen_EVEX_VRSQRT14(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_fp1(s, decode, 1, 2, 0);
}
#endif /* __Use_Original_Qemu (U236) */
#if __Use_Original_Qemu != 1 /* ours (U994) */

/*
 * NoVmp (ledger U994): AVX512ER through the one-source element helpers (helper_evex_fp1 /
 * _fp1s: 5 VRCP28, 6 VRSQRT28, 7 VEXP2; fpu_helper.c evex_rcp28 / evex_rsqrt28 / evex_exp2):
 * packed zmm1{k1}{z}, zmm2/m512/{1toN}{sae}; scalar xmm1{k1}{z}, xmm2, xmm3/m{sae} (bits 127:esz
 * from xmm2). Masking, {1toN}, {sae} and the MXCSR / #XM frame come from gen_evex_insn (evex_fp).
 */
static void gen_EVEX_VRCP28(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_fp1(s, decode, 5, 2, 0);
}

static void gen_EVEX_VRSQRT28(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_fp1(s, decode, 6, 2, 0);
}

static void gen_EVEX_VEXP2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_fp1(s, decode, 7, 2, 0);
}
#endif /* __Use_Original_Qemu (U994) */
#if __Use_Original_Qemu != 1 /* ours (U237) */

static void gen_EVEX_VGETEXP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_fp1(s, decode, 2, 2, 0);
}
#endif /* __Use_Original_Qemu (U237) */
#if __Use_Original_Qemu != 1 /* ours (U238) */

/* packed: V, W, I (source op1); scalar: V, H, W, I */
static void gen_EVEX_VGETMANT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_fp1(s, decode, 3, 1, decode->immediate);
}
#endif /* __Use_Original_Qemu (U238) */
#if __Use_Original_Qemu != 1 /* ours (U241) */

static void gen_EVEX_VRNDSCALE(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_fp1(s, decode, 4, 1, decode->immediate);
}
#endif /* __Use_Original_Qemu (U241) */
#if __Use_Original_Qemu != 1 /* ours (U239) */

/* NoVmp (ledger U239): VSCALEFPS/PD/SS/SD */
static void gen_EVEX_VSCALEF(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    bool scalar = decode->e.evex_vl & X86_EVEX_LIG;
    TCGv_ptr v = evex_env_ptr(s, ZMM_OFFSET(decode->op[1].n));

    gen_helper_evex_scalef(tcg_ctx, cpu_env, OP_PTR0, v, OP_PTR1, OP_PTR2,
                           tcg_constant_i32(tcg_ctx, (s->evex_esz << 4) |
                                                     (evex_nelem(s, decode) << 8) |
                                                     (scalar ? 1u << 24 : 0)));
    tcg_temp_free_ptr(tcg_ctx, v);
}
#endif /* __Use_Original_Qemu (U239) */
#if __Use_Original_Qemu != 1 /* ours (U240) */

/*
 * NoVmp (ledger U240): VFIXUPIMMPS/PD/SS/SD: DEST is an input (read from the register also
 * when the result goes to the scratch register for masking); the opmask is passed so that
 * only active elements report ZE/IE; not an evex_fp form (MXCSR masks ignored, no #XM).
 */
static void gen_EVEX_VFIXUPIMM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    bool scalar = decode->e.evex_vl & X86_EVEX_LIG;
    TCGv_ptr pd = evex_env_ptr(s, ZMM_OFFSET(decode->op[0].n));
    TCGv_i64 k = tcg_temp_new_i64(tcg_ctx);

    if (s->evex_aaa) {
        tcg_gen_ld_i64(tcg_ctx, k, cpu_env, offsetof(CPUX86State, opmask_regs[s->evex_aaa]));
    } else {
        tcg_gen_movi_i64(tcg_ctx, k, -1);
    }
    gen_helper_evex_fixupimm(tcg_ctx, cpu_env, OP_PTR0, pd, OP_PTR1, OP_PTR2, k,
                             tcg_constant_i32(tcg_ctx, (s->evex_esz << 4) |
                                                       (evex_nelem(s, decode) << 8) |
                                                       ((decode->immediate & 0xff) << 16) |
                                                       (scalar ? 1u << 24 : 0) |
                                                       (s->evex_rc == -2 ? 1u << 25 : 0)));
    tcg_temp_free_i64(tcg_ctx, k);
    tcg_temp_free_ptr(tcg_ctx, pd);
}
#endif /* __Use_Original_Qemu (U240) */
#if __Use_Original_Qemu != 1 /* ours (U333) */
/* NoVmp (ledgers U333-U339): AVX512-FP16 */
#include "emit_fp16.c.inc"
#endif /* __Use_Original_Qemu (U333) */
#if __Use_Original_Qemu != 1 /* ours (U372) */
#include "emit-avx10.c.inc"
#endif /* __Use_Original_Qemu (U372) */

#if __Use_Original_Qemu != 1 /* ours (U402) */

/*
 * NoVmp (ledger U402): AVX10.2 FP16 -> FP8 conversions. The opmask selects destination
 * bytes while EVEX.b and disp8*N use the FP16 source element (ev_es 16), so the helper
 * builds the whole destination register (merging/zeroing per byte, zero above the
 * converted bytes) and the engine's writeback is skipped (op[0] unit X86_OP_SKIP). The
 * sources are already loaded (and {1toN} replicated) by gen_evex_insn.
 * desc: bits 7:0 VL in bytes, bit 8 zeroing, bits 10:9 kind (0 one source, 1 two sources,
 * 2 bias), bit 11 HF8 (E4M3; else BF8 E5M2), bit 12 saturating.
 */
#define AVX10B_Z        (1u << 8)

static TCGv_i64 gen_avx10b_kmask(DisasContext *s)
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

static void gen_avx10b_cvt_fp8(DisasContext *s, X86DecodedInsn *decode, int kind)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    bool map5 = (s->evex_p0 & 7) == 5;
    bool hf8 = decode->b != 0x74;
    bool sat = decode->b == 0x1b || (decode->b == 0x74 && map5);
    uint32_t desc = s->evex_vl | (s->evex_z ? AVX10B_Z : 0) | ((uint32_t)kind << 9) |
                    (hf8 ? 1u << 11 : 0) | (sat ? 1u << 12 : 0);
    TCGv_i64 k = gen_avx10b_kmask(s);
    TCGv_ptr d = evex_env_ptr(s, ZMM_OFFSET(decode->op[0].n));

    gen_helper_avx10b_cvt_fp8(tcg_ctx, cpu_env, d, kind ? OP_PTR1 : OP_PTR2, OP_PTR2, k,
                              tcg_constant_i32(tcg_ctx, desc));
    tcg_temp_free_ptr(tcg_ctx, d);
    tcg_temp_free_i64(tcg_ctx, k);
    decode->op[0].unit = X86_OP_SKIP;
}

static void gen_EVEX_VCVTPH2F8(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_avx10b_cvt_fp8(s, decode, 0);
}

static void gen_EVEX_VCVT2PH2F8(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_avx10b_cvt_fp8(s, decode, 1);
}

static void gen_EVEX_VCVTBIASPH2F8(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_avx10b_cvt_fp8(s, decode, 2);
}
#endif /* __Use_Original_Qemu (U402) */
#if __Use_Original_Qemu != 1 /* ours (U403) */

/* NoVmp (ledger U403): VCVTHF82PH: VL/2 E4M3 bytes -> FP16 words; masking by gen_evex_insn */
static void gen_EVEX_VCVTHF82PH(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_avx10b_cvthf82ph(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2,
                                tcg_constant_i32(tcg_ctx, s->evex_vl));
}
#endif /* __Use_Original_Qemu (U403) */
#if __Use_Original_Qemu != 1 /* ours (U404) */

/*
 * NoVmp (ledger U404): VCVT2PS2PHX: the helper converts the active elements only (no flag
 * from a masked-off one), merges/zeroes per FP16 word and updates MXCSR as if every
 * exception were masked (never #XM); {er}: static rounding, no flags. desc: bits 7:0 VL in
 * bytes, bit 8 zeroing, bits 14:12 = {er} rounding control + 1 (0 = MXCSR.RC).
 */
static void gen_EVEX_VCVT2PS2PHX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = s->evex_vl | (s->evex_z ? AVX10B_Z : 0) |
                    ((uint32_t)(s->evex_rc >= 0 ? s->evex_rc + 1 : 0) << 12);
    TCGv_i64 k = gen_avx10b_kmask(s);
    TCGv_ptr d = evex_env_ptr(s, ZMM_OFFSET(decode->op[0].n));

    gen_helper_avx10b_cvt2ps2phx(tcg_ctx, cpu_env, d, OP_PTR1, OP_PTR2, k,
                                 tcg_constant_i32(tcg_ctx, desc));
    tcg_temp_free_ptr(tcg_ctx, d);
    tcg_temp_free_i64(tcg_ctx, k);
    decode->op[0].unit = X86_OP_SKIP;
}
#endif /* __Use_Original_Qemu (U404) */
#if __Use_Original_Qemu != 1 /* ours (U405) */

/*
 * NoVmp (ledger U405, U406): EVEX VPDPB[SS,SU,UU]D[S], VPDPW[SU,US,UU]D[S]: the ops_sse.h
 * helpers of the VEX forms (U85, U86) at 128/256/512 bits (SSE_FN3). They accumulate into
 * DEST: with masking gen_evex_insn sends the result to evex_t1, which then starts as DEST.
 */
static void gen_avx10b_acc_dest(DisasContext *s, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vl = vector_len(s, decode), dst = ZMM_OFFSET(decode->op[0].n);

    if (decode->op[0].offset != dst) {
        tcg_gen_gvec_mov(tcg_ctx, MO_64, decode->op[0].offset, dst, vl, vl);
    }
}

#define AVX10B_VNNI(uname, lname)                                                    \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                    \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                            \
                                                                                     \
    gen_avx10b_acc_dest(s, decode);                                                  \
    SSE_FN3(s, decode, lname)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);          \
}
AVX10B_VNNI(VPDPBSSD, vpdpbssd)
AVX10B_VNNI(VPDPBSSDS, vpdpbssds)
AVX10B_VNNI(VPDPBSUD, vpdpbsud)
AVX10B_VNNI(VPDPBSUDS, vpdpbsuds)
AVX10B_VNNI(VPDPBUUD, vpdpbuud)
AVX10B_VNNI(VPDPBUUDS, vpdpbuuds)
#endif /* __Use_Original_Qemu (U405) */
#if __Use_Original_Qemu != 1 /* ours (U406) */
AVX10B_VNNI(VPDPWSUD, vpdpwsud)
AVX10B_VNNI(VPDPWSUDS, vpdpwsuds)
AVX10B_VNNI(VPDPWUSD, vpdpwusd)
AVX10B_VNNI(VPDPWUSDS, vpdpwusds)
AVX10B_VNNI(VPDPWUUD, vpdpwuud)
AVX10B_VNNI(VPDPWUUDS, vpdpwuuds)
#endif /* __Use_Original_Qemu (U406) */
#if __Use_Original_Qemu != 1 /* ours (U407) */

/* NoVmp (ledger U407): VDPPHPS (accumulates into DEST like the VNNI forms) */
static void gen_EVEX_VDPPHPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_avx10b_acc_dest(s, decode);
    gen_helper_avx10b_vdpphps(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                              tcg_constant_i32(tcg_ctx, s->evex_vl));
}
#endif /* __Use_Original_Qemu (U407) */
#if __Use_Original_Qemu != 1 /* ours (U408) */

/*
 * NoVmp (ledger U408): EVEX VMPSADBW: the ops_sse.h mpsadbw helpers per 128-bit lane pair
 * (imm8[2:0] for lanes 0/2, imm8[5:3] for lanes 1/3: the 512-bit form replicates the
 * control of the lower two lanes to the upper two, spec 10.2); masking per word by
 * gen_evex_insn.
 */
static void gen_EVEX_VMPSADBW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant_i32(tcg_ctx, decode->immediate & 0xff);

    if (s->evex_vl == 16) {
        gen_helper_mpsadbw_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else {
        gen_helper_mpsadbw_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
        if (s->evex_vl == 64) {
            TCGv_ptr d = tcg_temp_new_ptr(tcg_ctx);
            TCGv_ptr v = tcg_temp_new_ptr(tcg_ctx);
            TCGv_ptr w = tcg_temp_new_ptr(tcg_ctx);

            tcg_gen_addi_ptr(tcg_ctx, d, OP_PTR0, 32);
            tcg_gen_addi_ptr(tcg_ctx, v, OP_PTR1, 32);
            tcg_gen_addi_ptr(tcg_ctx, w, OP_PTR2, 32);
            gen_helper_mpsadbw_ymm(tcg_ctx, cpu_env, d, v, w, imm);
            tcg_temp_free_ptr(tcg_ctx, d);
            tcg_temp_free_ptr(tcg_ctx, v);
            tcg_temp_free_ptr(tcg_ctx, w);
        }
    }
}
#endif /* __Use_Original_Qemu (U408) */
#if __Use_Original_Qemu != 1 /* ours (U410) */

/*
 * NoVmp (ledger U410, U411): zero-extending VMOVD / VMOVW (op[0] = destination, op[2] =
 * source, 32 or 16 bits). Register destination: DEST[MAXVL-1:size] := 0 (bits 511:128 by
 * gen_evex_insn); memory destination: a plain store of the element (no masking), the
 * engine's masked store is skipped.
 */
static void gen_EVEX_VMOVZX_LD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 t = tcg_temp_new_i32(tcg_ctx);
    int dst = ZMM_OFFSET(decode->op[0].n);

    if (decode->op[2].ot == MO_16) {
        tcg_gen_ld16u_i32(tcg_ctx, t, cpu_env, decode->op[2].offset);
    } else {
        tcg_gen_ld_i32(tcg_ctx, t, cpu_env, decode->op[2].offset);
    }
    tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, dst, 16, 16, 0);
    tcg_gen_st_i32(tcg_ctx, t, cpu_env, dst);
    tcg_temp_free_i32(tcg_ctx, t);
}

static void gen_EVEX_VMOVZX_ST(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;
    TCGv_i32 t = tcg_temp_new_i32(tcg_ctx);

    if (ot == MO_16) {
        tcg_gen_ld16u_i32(tcg_ctx, t, cpu_env, decode->op[2].offset);
    } else {
        tcg_gen_ld_i32(tcg_ctx, t, cpu_env, decode->op[2].offset);
    }
    if (decode->op[0].has_ea) {
        tcg_gen_extu_i32_tl(tcg_ctx, s->T0, t);
        gen_op_st_v(s, ot, s->T0, s->A0);
        decode->op[0].unit = X86_OP_SKIP;
    } else {
        int dst = ZMM_OFFSET(decode->op[0].n);

        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, dst, 16, 16, 0);
        tcg_gen_st_i32(tcg_ctx, t, cpu_env, dst);
    }
    tcg_temp_free_i32(tcg_ctx, t);
}
#endif /* __Use_Original_Qemu (U410) */
#if __Use_Original_Qemu != 1 /* ours (U412) */

/* NoVmp (ledger U412): EVEX VSM4KEY4 / VSM4RNDS4, 128-bit lanes at 128/256/512 bits */
static void gen_EVEX_VSM4KEY4(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, vsm4key4)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_EVEX_VSM4RNDS4(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, vsm4rnds4)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}
#endif /* __Use_Original_Qemu (U412) */
static void gen_store_sse(DisasContext *s, X86DecodedInsn *decode, int src_ofs)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;
    int vec_len = vector_len(s, decode);
    bool aligned = sse_needs_alignment(s, decode, ot);

    if (!decode->op[0].has_ea) {
        tcg_gen_gvec_mov(tcg_ctx, MO_64, decode->op[0].offset, src_ofs, vec_len, vec_len);
        return;
    }

    switch (ot) {
    case MO_64:
        gen_stq_env_A0(s, src_ofs);
        break;
    case MO_128:
        gen_sto_env_A0(s, src_ofs, aligned);
        break;
    case MO_256:
        gen_sty_env_A0(s, src_ofs, aligned);
        break;
    default:
        g_assert_not_reached();
    }
}

static void gen_helper_pavgusb(TCGContext *tcg_ctx, TCGv_ptr env,
                               TCGv_ptr reg_a, TCGv_ptr reg_b)
{
    gen_helper_pavgb_mmx(tcg_ctx, env, reg_a, reg_a, reg_b);
}

#define FN_3DNOW_MOVE ((SSEFunc_0_epp) (uintptr_t) 1)
static const SSEFunc_0_epp fns_3dnow[] = {
    [0x0c] = gen_helper_pi2fw,
    [0x0d] = gen_helper_pi2fd,
    [0x1c] = gen_helper_pf2iw,
    [0x1d] = gen_helper_pf2id,
    [0x8a] = gen_helper_pfnacc,
    [0x8e] = gen_helper_pfpnacc,
    [0x90] = gen_helper_pfcmpge,
    [0x94] = gen_helper_pfmin,
    [0x96] = gen_helper_pfrcp,
    [0x97] = gen_helper_pfrsqrt,
    [0x9a] = gen_helper_pfsub,
    [0x9e] = gen_helper_pfadd,
    [0xa0] = gen_helper_pfcmpgt,
    [0xa4] = gen_helper_pfmax,
    [0xa6] = FN_3DNOW_MOVE, /* PFRCPIT1; no need to actually increase precision */
    [0xa7] = FN_3DNOW_MOVE, /* PFRSQIT1 */
    [0xb6] = FN_3DNOW_MOVE, /* PFRCPIT2 */
    [0xaa] = gen_helper_pfsubr,
    [0xae] = gen_helper_pfacc,
    [0xb0] = gen_helper_pfcmpeq,
    [0xb4] = gen_helper_pfmul,
    [0xb7] = gen_helper_pmulhrw_mmx,
    [0xbb] = gen_helper_pswapd,
    [0xbf] = gen_helper_pavgusb,
};

static void gen_3dnow(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint8_t b = decode->immediate;
    SSEFunc_0_epp fn = b < ARRAY_SIZE(fns_3dnow) ? fns_3dnow[b] : NULL;

    if (!fn) {
        gen_illegal_opcode(s);
        return;
    }
    if (s->flags & HF_TS_MASK) {
        gen_NM_exception(s);
        return;
    }
    if (s->flags & HF_EM_MASK) {
        gen_illegal_opcode(s);
        return;
    }

    gen_helper_enter_mmx(tcg_ctx, cpu_env);
    if (fn == FN_3DNOW_MOVE) {
       tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[1].offset);
       tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset);
    } else {
       fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1);
    }
}

/*
 * 00 = v*ps Vps, Hps, Wpd
 * 66 = v*pd Vpd, Hpd, Wps
 * f3 = v*ss Vss, Hss, Wps
 * f2 = v*sd Vsd, Hsd, Wps
 */
static inline void gen_unary_fp_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                              SSEFunc_0_epp pd_xmm, SSEFunc_0_epp ps_xmm,
                              SSEFunc_0_epp pd_ymm, SSEFunc_0_epp ps_ymm,
                              SSEFunc_0_eppp sd, SSEFunc_0_eppp ss)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if ((s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) != 0) {
        SSEFunc_0_eppp fn = s->prefix & PREFIX_REPZ ? ss : sd;
        if (!fn) {
            gen_illegal_opcode(s);
            return;
        }
        fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        SSEFunc_0_epp ps, pd, fn;
        ps = s->vex_l ? ps_ymm : ps_xmm;
        pd = s->vex_l ? pd_ymm : pd_xmm;
        fn = s->prefix & PREFIX_DATA ? pd : ps;
        if (!fn) {
            gen_illegal_opcode(s);
            return;
        }
        fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    }
}
#define UNARY_FP_SSE(uname, lname)                                                 \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_fp_sse(s, env, decode,                                               \
                     gen_helper_##lname##pd_xmm,                                   \
                     gen_helper_##lname##ps_xmm,                                   \
                     gen_helper_##lname##pd_ymm,                                   \
                     gen_helper_##lname##ps_ymm,                                   \
                     gen_helper_##lname##sd,                                       \
                     gen_helper_##lname##ss);                                      \
}
UNARY_FP_SSE(VSQRT, sqrt)

/*
 * 00 = v*ps Vps, Hps, Wpd
 * 66 = v*pd Vpd, Hpd, Wps
 * f3 = v*ss Vss, Hss, Wps
 * f2 = v*sd Vsd, Hsd, Wps
 */
static inline void gen_fp_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                              SSEFunc_0_eppp pd_xmm, SSEFunc_0_eppp ps_xmm,
                              SSEFunc_0_eppp pd_ymm, SSEFunc_0_eppp ps_ymm,
                              SSEFunc_0_eppp sd, SSEFunc_0_eppp ss)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_eppp ps, pd, fn;
    if ((s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) != 0) {
        fn = s->prefix & PREFIX_REPZ ? ss : sd;
    } else {
        ps = s->vex_l ? ps_ymm : ps_xmm;
        pd = s->vex_l ? pd_ymm : pd_xmm;
        fn = s->prefix & PREFIX_DATA ? pd : ps;
    }
    if (fn) {
        fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        gen_illegal_opcode(s);
    }
}

#define FP_SSE(uname, lname)                                                       \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_fp_sse(s, env, decode,                                                     \
               gen_helper_##lname##pd_xmm,                                         \
               gen_helper_##lname##ps_xmm,                                         \
               gen_helper_##lname##pd_ymm,                                         \
               gen_helper_##lname##ps_ymm,                                         \
               gen_helper_##lname##sd,                                             \
               gen_helper_##lname##ss);                                            \
}
FP_SSE(VADD, add)
FP_SSE(VMUL, mul)
FP_SSE(VSUB, sub)
FP_SSE(VMIN, min)
FP_SSE(VDIV, div)
FP_SSE(VMAX, max)

#define FMA_SSE_PACKED(uname, ptr0, ptr1, ptr2, even, odd)                         \
static void gen_##uname##Px(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                          \
    SSEFunc_0_eppppii xmm = s->vex_w ? gen_helper_fma4pd_xmm : gen_helper_fma4ps_xmm; \
    SSEFunc_0_eppppii ymm = s->vex_w ? gen_helper_fma4pd_ymm : gen_helper_fma4ps_ymm; \
    SSEFunc_0_eppppii fn = s->vex_l ? ymm : xmm;                                   \
                                                                                   \
    fn(tcg_ctx, cpu_env, OP_PTR0, ptr0, ptr1, ptr2,                                         \
       tcg_constant_i32(tcg_ctx, even),                                                     \
       tcg_constant_i32(tcg_ctx, (even) ^ (odd)));                                          \
}

#define FMA_SSE(uname, ptr0, ptr1, ptr2, flags)                                    \
FMA_SSE_PACKED(uname, ptr0, ptr1, ptr2, flags, flags)                              \
static void gen_##uname##Sx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                          \
    SSEFunc_0_eppppi fn = s->vex_w ? gen_helper_fma4sd : gen_helper_fma4ss;        \
                                                                                   \
    fn(tcg_ctx, cpu_env, OP_PTR0, ptr0, ptr1, ptr2,                                         \
       tcg_constant_i32(tcg_ctx, flags));                                                   \
}                                                                                  \

FMA_SSE(VFMADD231,  OP_PTR1, OP_PTR2, OP_PTR0, 0)
FMA_SSE(VFMADD213,  OP_PTR1, OP_PTR0, OP_PTR2, 0)
FMA_SSE(VFMADD132,  OP_PTR0, OP_PTR2, OP_PTR1, 0)

FMA_SSE(VFNMADD231, OP_PTR1, OP_PTR2, OP_PTR0, float_muladd_negate_product)
FMA_SSE(VFNMADD213, OP_PTR1, OP_PTR0, OP_PTR2, float_muladd_negate_product)
FMA_SSE(VFNMADD132, OP_PTR0, OP_PTR2, OP_PTR1, float_muladd_negate_product)

FMA_SSE(VFMSUB231,  OP_PTR1, OP_PTR2, OP_PTR0, float_muladd_negate_c)
FMA_SSE(VFMSUB213,  OP_PTR1, OP_PTR0, OP_PTR2, float_muladd_negate_c)
FMA_SSE(VFMSUB132,  OP_PTR0, OP_PTR2, OP_PTR1, float_muladd_negate_c)

FMA_SSE(VFNMSUB231, OP_PTR1, OP_PTR2, OP_PTR0, float_muladd_negate_c|float_muladd_negate_product)
FMA_SSE(VFNMSUB213, OP_PTR1, OP_PTR0, OP_PTR2, float_muladd_negate_c|float_muladd_negate_product)
FMA_SSE(VFNMSUB132, OP_PTR0, OP_PTR2, OP_PTR1, float_muladd_negate_c|float_muladd_negate_product)

FMA_SSE_PACKED(VFMADDSUB231, OP_PTR1, OP_PTR2, OP_PTR0, float_muladd_negate_c, 0)
FMA_SSE_PACKED(VFMADDSUB213, OP_PTR1, OP_PTR0, OP_PTR2, float_muladd_negate_c, 0)
FMA_SSE_PACKED(VFMADDSUB132, OP_PTR0, OP_PTR2, OP_PTR1, float_muladd_negate_c, 0)

FMA_SSE_PACKED(VFMSUBADD231, OP_PTR1, OP_PTR2, OP_PTR0, 0, float_muladd_negate_c)
FMA_SSE_PACKED(VFMSUBADD213, OP_PTR1, OP_PTR0, OP_PTR2, 0, float_muladd_negate_c)
FMA_SSE_PACKED(VFMSUBADD132, OP_PTR0, OP_PTR2, OP_PTR1, 0, float_muladd_negate_c)

#define FP_UNPACK_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    /* PS maps to the DQ integer instruction, PD maps to QDQ.  */                  \
    gen_fp_sse(s, env, decode,                                                     \
               gen_helper_##lname##qdq_xmm,                                        \
               gen_helper_##lname##dq_xmm,                                         \
               gen_helper_##lname##qdq_ymm,                                        \
               gen_helper_##lname##dq_ymm,                                         \
               NULL, NULL);                                                        \
}
FP_UNPACK_SSE(VUNPCKLPx, punpckl)
FP_UNPACK_SSE(VUNPCKHPx, punpckh)

/*
 * 00 = v*ps Vps, Wpd
 * f3 = v*ss Vss, Wps
 */
static inline void gen_unary_fp32_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                      SSEFunc_0_epp ps_xmm,
                                      SSEFunc_0_epp ps_ymm,
                                      SSEFunc_0_eppp ss)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if ((s->prefix & (PREFIX_DATA | PREFIX_REPNZ)) != 0) {
        goto illegal_op;
    } else if (s->prefix & PREFIX_REPZ) {
        if (!ss) {
            goto illegal_op;
        }
        ss(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        SSEFunc_0_epp fn = s->vex_l ? ps_ymm : ps_xmm;
        if (!fn) {
            goto illegal_op;
        }
        fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    }
    return;

illegal_op:
    gen_illegal_opcode(s);
}
#define UNARY_FP32_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_fp32_sse(s, env, decode,                                             \
                       gen_helper_##lname##ps_xmm,                                 \
                       gen_helper_##lname##ps_ymm,                                 \
                       gen_helper_##lname##ss);                                    \
}
UNARY_FP32_SSE(VRSQRT, rsqrt)
UNARY_FP32_SSE(VRCP, rcp)

/*
 * 66 = v*pd Vpd, Hpd, Wpd
 * f2 = v*ps Vps, Hps, Wps
 */
static inline void gen_horizontal_fp_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                         SSEFunc_0_eppp pd_xmm, SSEFunc_0_eppp ps_xmm,
                                         SSEFunc_0_eppp pd_ymm, SSEFunc_0_eppp ps_ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_eppp ps, pd, fn;
    ps = s->vex_l ? ps_ymm : ps_xmm;
    pd = s->vex_l ? pd_ymm : pd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}
#define HORIZONTAL_FP_SSE(uname, lname)                                            \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_horizontal_fp_sse(s, env, decode,                                          \
                          gen_helper_##lname##pd_xmm, gen_helper_##lname##ps_xmm,  \
                          gen_helper_##lname##pd_ymm, gen_helper_##lname##ps_ymm); \
}
HORIZONTAL_FP_SSE(VHADD, hadd)
HORIZONTAL_FP_SSE(VHSUB, hsub)
HORIZONTAL_FP_SSE(VADDSUB, addsub)

static inline void gen_ternary_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                   int op3, SSEFunc_0_epppp xmm, SSEFunc_0_epppp ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_epppp fn = s->vex_l ? ymm : xmm;
    TCGv_ptr ptr3 = tcg_temp_new_ptr(tcg_ctx);

#if __Use_Original_Qemu != 1 /* ours (U93) */
    /*
     * NoVmp (ledger U93): /is4 selects the register with imm8[7:4] in 64-bit mode
     * and imm8[6:4] in 32-bit mode (SDM Vol2A 3.1.1.2; VBLENDVPS/VBLENDVPD/
     * VPBLENDVB "In 32-bit mode, imm8[7] is ignored").
     */
    if (!CODE64(s)) {
        op3 &= 7;
    }
#endif /* __Use_Original_Qemu (U93) */
    /* The format of the fourth input is Lx */
    tcg_gen_addi_ptr(tcg_ctx, ptr3, cpu_env, ZMM_OFFSET(op3));
    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, ptr3);
    tcg_temp_free_ptr(tcg_ctx, ptr3);
}
#define TERNARY_SSE(uname, uvname, lname)                                          \
static void gen_##uvname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_ternary_sse(s, env, decode, (uint8_t)decode->immediate >> 4,               \
                    gen_helper_##lname##_xmm, gen_helper_##lname##_ymm);           \
}                                                                                  \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_ternary_sse(s, env, decode, 0,                                             \
                  gen_helper_##lname##_xmm, gen_helper_##lname##_ymm);             \
}
TERNARY_SSE(BLENDVPS, VBLENDVPS, blendvps)
TERNARY_SSE(BLENDVPD, VBLENDVPD, blendvpd)
TERNARY_SSE(PBLENDVB, VPBLENDVB, pblendvb)

static inline void gen_binary_imm_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                      SSEFunc_0_epppi xmm, SSEFunc_0_epppi ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    if (!s->vex_l) {
        xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else {
        ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    }
}

#define BINARY_IMM_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_binary_imm_sse(s, env, decode,                                             \
                       gen_helper_##lname##_xmm,                                   \
                       gen_helper_##lname##_ymm);                                  \
}

BINARY_IMM_SSE(VBLENDPD,   blendpd)
BINARY_IMM_SSE(VBLENDPS,   blendps)
BINARY_IMM_SSE(VPBLENDW,   pblendw)
BINARY_IMM_SSE(VDPPS,      dpps)
#define gen_helper_dppd_ymm NULL
BINARY_IMM_SSE(VDPPD,      dppd)
BINARY_IMM_SSE(VMPSADBW,   mpsadbw)
BINARY_IMM_SSE(PCLMULQDQ,  pclmulqdq)
#if __Use_Original_Qemu != 1 /* ours (U70) */
BINARY_IMM_SSE(GF2P8AFFINEQB,    gf2p8affineqb)
BINARY_IMM_SSE(GF2P8AFFINEINVQB, gf2p8affineinvqb)
#endif /* __Use_Original_Qemu (U70) */


#define UNARY_INT_GVEC(uname, func, ...)                                           \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                          \
    int vec_len = vector_len(s, decode);                                          \
                                                                                   \
    func(tcg_ctx, __VA_ARGS__, decode->op[0].offset,                               \
         decode->op[2].offset, vec_len, vec_len);                                  \
}
UNARY_INT_GVEC(PABSB,          tcg_gen_gvec_abs, MO_8)
UNARY_INT_GVEC(PABSW,          tcg_gen_gvec_abs, MO_16)
UNARY_INT_GVEC(PABSD,          tcg_gen_gvec_abs, MO_32)
UNARY_INT_GVEC(VBROADCASTx128, tcg_gen_gvec_dup_mem, MO_128)
UNARY_INT_GVEC(VPBROADCASTB,   tcg_gen_gvec_dup_mem, MO_8)
UNARY_INT_GVEC(VPBROADCASTW,   tcg_gen_gvec_dup_mem, MO_16)
UNARY_INT_GVEC(VPBROADCASTD,   tcg_gen_gvec_dup_mem, MO_32)
UNARY_INT_GVEC(VPBROADCASTQ,   tcg_gen_gvec_dup_mem, MO_64)


#define BINARY_INT_GVEC(uname, func, ...)                                          \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                          \
    int vec_len = vector_len(s, decode);                                          \
                                                                                   \
    func(tcg_ctx, __VA_ARGS__,                                                     \
         decode->op[0].offset, decode->op[1].offset,                               \
         decode->op[2].offset, vec_len, vec_len);                                  \
}

BINARY_INT_GVEC(PADDB,   tcg_gen_gvec_add, MO_8)
BINARY_INT_GVEC(PADDW,   tcg_gen_gvec_add, MO_16)
BINARY_INT_GVEC(PADDD,   tcg_gen_gvec_add, MO_32)
BINARY_INT_GVEC(PADDQ,   tcg_gen_gvec_add, MO_64)
BINARY_INT_GVEC(PADDSB,  tcg_gen_gvec_ssadd, MO_8)
BINARY_INT_GVEC(PADDSW,  tcg_gen_gvec_ssadd, MO_16)
BINARY_INT_GVEC(PADDUSB, tcg_gen_gvec_usadd, MO_8)
BINARY_INT_GVEC(PADDUSW, tcg_gen_gvec_usadd, MO_16)
BINARY_INT_GVEC(PAND,    tcg_gen_gvec_and, MO_64)
BINARY_INT_GVEC(PCMPEQB, tcg_gen_gvec_cmp, TCG_COND_EQ, MO_8)
BINARY_INT_GVEC(PCMPEQD, tcg_gen_gvec_cmp, TCG_COND_EQ, MO_32)
BINARY_INT_GVEC(PCMPEQW, tcg_gen_gvec_cmp, TCG_COND_EQ, MO_16)
BINARY_INT_GVEC(PCMPEQQ, tcg_gen_gvec_cmp, TCG_COND_EQ, MO_64)
BINARY_INT_GVEC(PCMPGTB, tcg_gen_gvec_cmp, TCG_COND_GT, MO_8)
BINARY_INT_GVEC(PCMPGTW, tcg_gen_gvec_cmp, TCG_COND_GT, MO_16)
BINARY_INT_GVEC(PCMPGTD, tcg_gen_gvec_cmp, TCG_COND_GT, MO_32)
BINARY_INT_GVEC(PCMPGTQ, tcg_gen_gvec_cmp, TCG_COND_GT, MO_64)
BINARY_INT_GVEC(PMAXSB,  tcg_gen_gvec_smax, MO_8)
BINARY_INT_GVEC(PMAXSW,  tcg_gen_gvec_smax, MO_16)
BINARY_INT_GVEC(PMAXSD,  tcg_gen_gvec_smax, MO_32)
BINARY_INT_GVEC(PMAXUB,  tcg_gen_gvec_umax, MO_8)
BINARY_INT_GVEC(PMAXUW,  tcg_gen_gvec_umax, MO_16)
BINARY_INT_GVEC(PMAXUD,  tcg_gen_gvec_umax, MO_32)
BINARY_INT_GVEC(PMINSB,  tcg_gen_gvec_smin, MO_8)
BINARY_INT_GVEC(PMINSW,  tcg_gen_gvec_smin, MO_16)
BINARY_INT_GVEC(PMINSD,  tcg_gen_gvec_smin, MO_32)
BINARY_INT_GVEC(PMINUB,  tcg_gen_gvec_umin, MO_8)
BINARY_INT_GVEC(PMINUW,  tcg_gen_gvec_umin, MO_16)
BINARY_INT_GVEC(PMINUD,  tcg_gen_gvec_umin, MO_32)
BINARY_INT_GVEC(PMULLW,  tcg_gen_gvec_mul, MO_16)
BINARY_INT_GVEC(PMULLD,  tcg_gen_gvec_mul, MO_32)
BINARY_INT_GVEC(POR,     tcg_gen_gvec_or, MO_64)
BINARY_INT_GVEC(PSUBB,   tcg_gen_gvec_sub, MO_8)
BINARY_INT_GVEC(PSUBW,   tcg_gen_gvec_sub, MO_16)
BINARY_INT_GVEC(PSUBD,   tcg_gen_gvec_sub, MO_32)
BINARY_INT_GVEC(PSUBQ,   tcg_gen_gvec_sub, MO_64)
BINARY_INT_GVEC(PSUBSB,  tcg_gen_gvec_sssub, MO_8)
BINARY_INT_GVEC(PSUBSW,  tcg_gen_gvec_sssub, MO_16)
BINARY_INT_GVEC(PSUBUSB, tcg_gen_gvec_ussub, MO_8)
BINARY_INT_GVEC(PSUBUSW, tcg_gen_gvec_ussub, MO_16)
BINARY_INT_GVEC(PXOR,    tcg_gen_gvec_xor, MO_64)


/*
 * 00 = p*  Pq, Qq (if mmx not NULL; no VEX)
 * 66 = vp* Vx, Hx, Wx
 *
 * These are really the same encoding, because 1) V is the same as P when VEX.V
 * is not present 2) P and Q are the same as H and W apart from MM/XMM
 */
static inline void gen_binary_int_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                      SSEFunc_0_eppp mmx, SSEFunc_0_eppp xmm, SSEFunc_0_eppp ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    assert(!!mmx == !!(decode->e.special == X86_SPECIAL_MMX));

    if (mmx && (s->prefix & PREFIX_VEX) && !(s->prefix & PREFIX_DATA)) {
        /* VEX encoding is not applicable to MMX instructions.  */
        gen_illegal_opcode(s);
        return;
    }
    if (!(s->prefix & PREFIX_DATA)) {
        mmx(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else if (!s->vex_l) {
        xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    }
}


#define BINARY_INT_MMX(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_binary_int_sse(s, env, decode,                                             \
                          gen_helper_##lname##_mmx,                                \
                          gen_helper_##lname##_xmm,                                \
                          gen_helper_##lname##_ymm);                               \
}
BINARY_INT_MMX(PUNPCKLBW,  punpcklbw)
BINARY_INT_MMX(PUNPCKLWD,  punpcklwd)
BINARY_INT_MMX(PUNPCKLDQ,  punpckldq)
BINARY_INT_MMX(PACKSSWB,   packsswb)
BINARY_INT_MMX(PACKUSWB,   packuswb)
BINARY_INT_MMX(PUNPCKHBW,  punpckhbw)
BINARY_INT_MMX(PUNPCKHWD,  punpckhwd)
BINARY_INT_MMX(PUNPCKHDQ,  punpckhdq)
BINARY_INT_MMX(PACKSSDW,   packssdw)

BINARY_INT_MMX(PAVGB,   pavgb)
BINARY_INT_MMX(PAVGW,   pavgw)
BINARY_INT_MMX(PMADDWD, pmaddwd)
BINARY_INT_MMX(PMULHUW, pmulhuw)
BINARY_INT_MMX(PMULHW,  pmulhw)
BINARY_INT_MMX(PMULUDQ, pmuludq)
BINARY_INT_MMX(PSADBW,  psadbw)

BINARY_INT_MMX(PSLLW_r, psllw)
BINARY_INT_MMX(PSLLD_r, pslld)
BINARY_INT_MMX(PSLLQ_r, psllq)
BINARY_INT_MMX(PSRLW_r, psrlw)
BINARY_INT_MMX(PSRLD_r, psrld)
BINARY_INT_MMX(PSRLQ_r, psrlq)
BINARY_INT_MMX(PSRAW_r, psraw)
BINARY_INT_MMX(PSRAD_r, psrad)

BINARY_INT_MMX(PHADDW,    phaddw)
BINARY_INT_MMX(PHADDSW,   phaddsw)
BINARY_INT_MMX(PHADDD,    phaddd)
BINARY_INT_MMX(PHSUBW,    phsubw)
BINARY_INT_MMX(PHSUBSW,   phsubsw)
BINARY_INT_MMX(PHSUBD,    phsubd)
BINARY_INT_MMX(PMADDUBSW, pmaddubsw)
BINARY_INT_MMX(PSHUFB,    pshufb)
BINARY_INT_MMX(PSIGNB,    psignb)
BINARY_INT_MMX(PSIGNW,    psignw)
BINARY_INT_MMX(PSIGND,    psignd)
BINARY_INT_MMX(PMULHRSW,  pmulhrsw)

/* Instructions with no MMX equivalent.  */
#define BINARY_INT_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_binary_int_sse(s, env, decode,                                             \
                          NULL,                                                    \
                          gen_helper_##lname##_xmm,                                \
                          gen_helper_##lname##_ymm);                               \
}

/* Instructions with no MMX equivalent.  */
#if __Use_Original_Qemu != 1 /* ours (U70) */
BINARY_INT_SSE(GF2P8MULB,  gf2p8mulb)
#endif /* __Use_Original_Qemu (U70) */
#if __Use_Original_Qemu != 1 /* ours (U71) */
BINARY_INT_SSE(VPDPBUSD,   vpdpbusd)
BINARY_INT_SSE(VPDPBUSDS,  vpdpbusds)
BINARY_INT_SSE(VPDPWSSD,   vpdpwssd)
BINARY_INT_SSE(VPDPWSSDS,  vpdpwssds)
#endif /* __Use_Original_Qemu (U71) */
#if __Use_Original_Qemu != 1 /* ours (U85) */
/*
 * VEX-only three-operand integer ops whose prefix (NP/F3/F2) is an opcode
 * extension: gen_binary_int_sse treats "no 66" as MMX, so pick by VEX.L only.
 */
static inline void gen_binary_int_vex(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                      SSEFunc_0_eppp xmm, SSEFunc_0_eppp ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if (!s->vex_l) {
        xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    }
}

#define BINARY_INT_VEX(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_binary_int_vex(s, env, decode,                                             \
                       gen_helper_##lname##_xmm,                                   \
                       gen_helper_##lname##_ymm);                                  \
}

BINARY_INT_VEX(VPDPBSSD,   vpdpbssd)
BINARY_INT_VEX(VPDPBSSDS,  vpdpbssds)
BINARY_INT_VEX(VPDPBSUD,   vpdpbsud)
BINARY_INT_VEX(VPDPBSUDS,  vpdpbsuds)
BINARY_INT_VEX(VPDPBUUD,   vpdpbuud)
BINARY_INT_VEX(VPDPBUUDS,  vpdpbuuds)
#endif /* __Use_Original_Qemu (U85) */
#if __Use_Original_Qemu != 1 /* ours (U86) */
BINARY_INT_VEX(VPDPWSUD,   vpdpwsud)
BINARY_INT_VEX(VPDPWSUDS,  vpdpwsuds)
BINARY_INT_VEX(VPDPWUSD,   vpdpwusd)
BINARY_INT_VEX(VPDPWUSDS,  vpdpwusds)
BINARY_INT_VEX(VPDPWUUD,   vpdpwuud)
BINARY_INT_VEX(VPDPWUUDS,  vpdpwuuds)
#endif /* __Use_Original_Qemu (U86) */
#if __Use_Original_Qemu != 1 /* ours (U87) */
BINARY_INT_SSE(VPMADD52LUQ, vpmadd52luq)
BINARY_INT_SSE(VPMADD52HUQ, vpmadd52huq)
#endif /* __Use_Original_Qemu (U87) */
BINARY_INT_SSE(PUNPCKLQDQ, punpcklqdq)
BINARY_INT_SSE(PUNPCKHQDQ, punpckhqdq)
BINARY_INT_SSE(VPACKUSDW,  packusdw)
BINARY_INT_SSE(VPERMILPS,  vpermilps)
BINARY_INT_SSE(VPERMILPD,  vpermilpd)
#if __Use_Original_Qemu == 1 /* original QEMU (U703) */
BINARY_INT_SSE(VMASKMOVPS, vpmaskmovd)
BINARY_INT_SSE(VMASKMOVPD, vpmaskmovq)
#else /* ours (U703) */
/*
 * NoVmp (ledger U703): the masked loads read their memory operand in the helper, element by
 * element (only the selected ones); disas_insn_new does not load operand 2 for them.
 */
static void gen_VMASKMOVPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (!s->vex_l) {
        gen_helper_vpmaskmovd_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, s->A0);
    } else {
        gen_helper_vpmaskmovd_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, s->A0);
    }
}

static void gen_VMASKMOVPD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (!s->vex_l) {
        gen_helper_vpmaskmovq_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, s->A0);
    } else {
        gen_helper_vpmaskmovq_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, s->A0);
    }
}
#endif /* __Use_Original_Qemu (U703) */

BINARY_INT_SSE(PMULDQ,    pmuldq)

BINARY_INT_SSE(VAESDEC, aesdec)
BINARY_INT_SSE(VAESDECLAST, aesdeclast)
BINARY_INT_SSE(VAESENC, aesenc)
BINARY_INT_SSE(VAESENCLAST, aesenclast)

#define UNARY_CMP_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                          \
    if (!s->vex_l) {                                                               \
        gen_helper_##lname##_xmm(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2);              \
    } else {                                                                       \
        gen_helper_##lname##_ymm(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2);              \
    }                                                                              \
    set_cc_op(s, CC_OP_EFLAGS);                                                    \
}
UNARY_CMP_SSE(VPTEST,     ptest)
UNARY_CMP_SSE(VTESTPS,    vtestps)
UNARY_CMP_SSE(VTESTPD,    vtestpd)

static inline void gen_unary_int_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                     SSEFunc_0_epp xmm, SSEFunc_0_epp ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if (!s->vex_l) {
        xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    } else {
        ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    }
}

#define UNARY_INT_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_int_sse(s, env, decode,                                              \
                      gen_helper_##lname##_xmm,                                    \
                      gen_helper_##lname##_ymm);                                   \
}

UNARY_INT_SSE(VPMOVSXBW,    pmovsxbw)
UNARY_INT_SSE(VPMOVSXBD,    pmovsxbd)
UNARY_INT_SSE(VPMOVSXBQ,    pmovsxbq)
UNARY_INT_SSE(VPMOVSXWD,    pmovsxwd)
UNARY_INT_SSE(VPMOVSXWQ,    pmovsxwq)
UNARY_INT_SSE(VPMOVSXDQ,    pmovsxdq)

UNARY_INT_SSE(VPMOVZXBW,    pmovzxbw)
UNARY_INT_SSE(VPMOVZXBD,    pmovzxbd)
UNARY_INT_SSE(VPMOVZXBQ,    pmovzxbq)
UNARY_INT_SSE(VPMOVZXWD,    pmovzxwd)
UNARY_INT_SSE(VPMOVZXWQ,    pmovzxwq)
UNARY_INT_SSE(VPMOVZXDQ,    pmovzxdq)

UNARY_INT_SSE(VMOVSLDUP,    pmovsldup)
UNARY_INT_SSE(VMOVSHDUP,    pmovshdup)
UNARY_INT_SSE(VMOVDDUP,     pmovdldup)

UNARY_INT_SSE(VCVTDQ2PD, cvtdq2pd)
UNARY_INT_SSE(VCVTPD2DQ, cvtpd2dq)
UNARY_INT_SSE(VCVTTPD2DQ, cvttpd2dq)
UNARY_INT_SSE(VCVTDQ2PS, cvtdq2ps)
UNARY_INT_SSE(VCVTPS2DQ, cvtps2dq)
UNARY_INT_SSE(VCVTTPS2DQ, cvttps2dq)
UNARY_INT_SSE(VCVTPH2PS, cvtph2ps)
#if __Use_Original_Qemu != 1 /* ours (U88) */
UNARY_INT_SSE(VBCSTNEBF162PS, vbcstnebf162ps)
UNARY_INT_SSE(VBCSTNESH2PS,   vbcstnesh2ps)
UNARY_INT_SSE(VCVTNEEBF162PS, vcvtneebf162ps)
UNARY_INT_SSE(VCVTNEEPH2PS,   vcvtneeph2ps)
UNARY_INT_SSE(VCVTNEOBF162PS, vcvtneobf162ps)
UNARY_INT_SSE(VCVTNEOPH2PS,   vcvtneoph2ps)
UNARY_INT_SSE(VCVTNEPS2BF16,  vcvtneps2bf16)
#endif /* __Use_Original_Qemu (U88) */


static inline void gen_unary_imm_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                     SSEFunc_0_ppi xmm, SSEFunc_0_ppi ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    if (!s->vex_l) {
        xmm(tcg_ctx, OP_PTR0, OP_PTR1, imm);
    } else {
        ymm(tcg_ctx, OP_PTR0, OP_PTR1, imm);
    }
}

#define UNARY_IMM_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_imm_sse(s, env, decode,                                              \
                      gen_helper_##lname##_xmm,                                    \
                      gen_helper_##lname##_ymm);                                   \
}

UNARY_IMM_SSE(PSHUFD,     pshufd)
UNARY_IMM_SSE(PSHUFHW,    pshufhw)
UNARY_IMM_SSE(PSHUFLW,    pshuflw)
#define gen_helper_vpermq_xmm NULL
UNARY_IMM_SSE(VPERMQ,      vpermq)
UNARY_IMM_SSE(VPERMILPS_i, vpermilps_imm)
UNARY_IMM_SSE(VPERMILPD_i, vpermilpd_imm)

static inline void gen_unary_imm_fp_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                        SSEFunc_0_eppi xmm, SSEFunc_0_eppi ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    if (!s->vex_l) {
        xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, imm);
    } else {
        ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, imm);
    }
}

#define UNARY_IMM_FP_SSE(uname, lname)                                             \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_imm_fp_sse(s, env, decode,                                           \
                      gen_helper_##lname##_xmm,                                    \
                      gen_helper_##lname##_ymm);                                   \
}

UNARY_IMM_FP_SSE(VROUNDPS,    roundps)
UNARY_IMM_FP_SSE(VROUNDPD,    roundpd)

static inline void gen_vexw_avx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                SSEFunc_0_eppp d_xmm, SSEFunc_0_eppp q_xmm,
                                SSEFunc_0_eppp d_ymm, SSEFunc_0_eppp q_ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_eppp d = s->vex_l ? d_ymm : d_xmm;
    SSEFunc_0_eppp q = s->vex_l ? q_ymm : q_xmm;
    SSEFunc_0_eppp fn = s->vex_w ? q : d;
    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

/* VEX.W affects whether to operate on 32- or 64-bit elements.  */
#define VEXW_AVX(uname, lname)                                                     \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_vexw_avx(s, env, decode,                                                   \
                 gen_helper_##lname##d_xmm, gen_helper_##lname##q_xmm,             \
                 gen_helper_##lname##d_ymm, gen_helper_##lname##q_ymm);            \
}
VEXW_AVX(VPSLLV,    vpsllv)
VEXW_AVX(VPSRLV,    vpsrlv)
VEXW_AVX(VPSRAV,    vpsrav)
#if __Use_Original_Qemu == 1 /* original QEMU (U703) */
VEXW_AVX(VPMASKMOV, vpmaskmov)
#else /* ours (U703) */
static void gen_VPMASKMOV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    if (s->vex_w) {
        gen_VMASKMOVPD(s, env, decode);
    } else {
        gen_VMASKMOVPS(s, env, decode);
    }
}
#endif /* __Use_Original_Qemu (U703) */

/* Same as above, but with extra arguments to the helper.  */
static inline void gen_vsib_avx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                SSEFunc_0_eppptit d_xmm, SSEFunc_0_eppptit q_xmm,
                                SSEFunc_0_eppptit d_ymm, SSEFunc_0_eppptit q_ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_eppptit d = s->vex_l ? d_ymm : d_xmm;
    SSEFunc_0_eppptit q = s->vex_l ? q_ymm : q_xmm;
    SSEFunc_0_eppptit fn = s->vex_w ? q : d;
    TCGv_i32 scale = tcg_constant_i32(tcg_ctx, decode->mem.scale);
    TCGv_ptr index = tcg_temp_new_ptr(tcg_ctx);
    /* VSIB EA wraps at the address size: 32 bits with aflag=MO_32, 64 with MO_64 */
    TCGv amask = tcg_constant_tl(tcg_ctx, MAKE_64BIT_MASK(0, 8 << s->aflag));

    /* Pass third input as (index, base, scale) */
    tcg_gen_addi_ptr(tcg_ctx, index, cpu_env, ZMM_OFFSET(decode->mem.index));
    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, index, s->A0, scale, amask);

#if __Use_Original_Qemu == 1 /* original QEMU (U126) */
    /*
     * There are two output operands, so zero OP1's high 128 bits
     * in the VEX.128 case.
     */
    if (!s->vex_l) {
        int ymmh_ofs = vector_elem_offset(&decode->op[1], MO_128, 1);
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, ymmh_ofs, 16, 16, 0);
    }
#else /* ours (U126) */
    /*
     * NoVmp (ledger U126): the mask (OP1) is the second output; like the
     * destination (gen_writeback) its bits MAXVL-1:VL are zeroed (SDM VPGATHERDD
     * etc.: MASK[MAXVL-1:128] or MASK[MAXVL-1:256] := 0).
     */
    {
        int vl = s->vex_l ? 32 : 16;
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, vector_reg_offset(&decode->op[1]) + vl,
                             sizeof(ZMMReg) - vl, sizeof(ZMMReg) - vl, 0);
    }
#endif /* __Use_Original_Qemu (U126) */
    tcg_temp_free(tcg_ctx, amask);   /* tcg_constant_* is a tcg_const_* shim here */
    tcg_temp_free_i32(tcg_ctx, scale);
    tcg_temp_free_ptr(tcg_ctx, index);
}
#define VSIB_AVX(uname, lname)                                                     \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_vsib_avx(s, env, decode,                                                   \
                 gen_helper_##lname##d_xmm, gen_helper_##lname##q_xmm,             \
                 gen_helper_##lname##d_ymm, gen_helper_##lname##q_ymm);            \
}
VSIB_AVX(VPGATHERD, vpgatherd)
VSIB_AVX(VPGATHERQ, vpgatherq)

static void gen_ADCOX(DisasContext *s, CPUX86State *env, MemOp ot, int cc_op)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int opposite_cc_op;
    TCGv carry_in = NULL;
    TCGv carry_out = (cc_op == CC_OP_ADCX ? cpu_cc_dst : cpu_cc_src2);
    TCGv zero;

    if (cc_op == s->cc_op || s->cc_op == CC_OP_ADCOX) {
        /* Re-use the carry-out from a previous round.  */
        carry_in = carry_out;
    } else {
        /* We don't have a carry-in, get it out of EFLAGS.  */
        if (s->cc_op != CC_OP_ADCX && s->cc_op != CC_OP_ADOX) {
            gen_compute_eflags(s);
        }
        carry_in = s->tmp0;
        tcg_gen_extract_tl(tcg_ctx, carry_in, cpu_cc_src,
            ctz32(cc_op == CC_OP_ADCX ? CC_C : CC_O), 1);
    }

    switch (ot) {
#ifdef TARGET_X86_64
    case MO_32:
        /* If TL is 64-bit just do everything in 64-bit arithmetic.  */
        tcg_gen_ext32u_tl(tcg_ctx, s->T0, s->T0);
        tcg_gen_ext32u_tl(tcg_ctx, s->T1, s->T1);
        tcg_gen_add_i64(tcg_ctx, s->T0, s->T0, s->T1);
        tcg_gen_add_i64(tcg_ctx, s->T0, s->T0, carry_in);
        tcg_gen_shri_i64(tcg_ctx, carry_out, s->T0, 32);
        break;
#endif
    default:
        zero = tcg_constant_tl(tcg_ctx, 0);
        tcg_gen_add2_tl(tcg_ctx, s->T0, carry_out, s->T0, zero, carry_in, zero);
        tcg_gen_add2_tl(tcg_ctx, s->T0, carry_out, s->T0, carry_out, s->T1, zero);
        break;
    }

    opposite_cc_op = cc_op == CC_OP_ADCX ? CC_OP_ADOX : CC_OP_ADCX;
    if (s->cc_op == CC_OP_ADCOX || s->cc_op == opposite_cc_op) {
        /* Merge with the carry-out from the opposite instruction.  */
        set_cc_op(s, CC_OP_ADCOX);
    } else {
        set_cc_op(s, cc_op);
    }
}

static void gen_ADCX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_ADCOX(s, env, decode->op[0].ot, CC_OP_ADCX);
}

static void gen_ADOX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_ADCOX(s, env, decode->op[0].ot, CC_OP_ADOX);
}

static void gen_ANDN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;

    tcg_gen_andc_tl(tcg_ctx, s->T0, s->T1, s->T0);
    gen_op_update1_cc(s);
    set_cc_op(s, CC_OP_LOGICB + ot);
}

static void gen_BEXTR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;
    TCGv bound = tcg_constant_tl(tcg_ctx, ot == MO_64 ? 63 : 31);
    TCGv zero = tcg_constant_tl(tcg_ctx, 0);
    TCGv mone = tcg_constant_tl(tcg_ctx, -1);

    /*
     * Extract START, and shift the operand.
     * Shifts larger than operand size get zeros.
     */
    tcg_gen_ext8u_tl(tcg_ctx, s->A0, s->T1);
    if (TARGET_LONG_BITS == 64 && ot == MO_32) {
        tcg_gen_ext32u_tl(tcg_ctx, s->T0, s->T0);
    }
    tcg_gen_shr_tl(tcg_ctx, s->T0, s->T0, s->A0);

    tcg_gen_movcond_tl(tcg_ctx, TCG_COND_LEU, s->T0, s->A0, bound, s->T0, zero);

    /*
     * Extract the LEN into an inverse mask.  Lengths larger than
     * operand size get all zeros, length 0 gets all ones.
     */
    tcg_gen_extract_tl(tcg_ctx, s->A0, s->T1, 8, 8);
    tcg_gen_shl_tl(tcg_ctx, s->T1, mone, s->A0);
    tcg_gen_movcond_tl(tcg_ctx, TCG_COND_LEU, s->T1, s->A0, bound, s->T1, zero);
    tcg_gen_andc_tl(tcg_ctx, s->T0, s->T0, s->T1);

    gen_op_update1_cc(s);
    set_cc_op(s, CC_OP_LOGICB + ot);
}

static void gen_BLSI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;

    tcg_gen_mov_tl(tcg_ctx, cpu_cc_src, s->T0);
    tcg_gen_neg_tl(tcg_ctx, s->T1, s->T0);
    tcg_gen_and_tl(tcg_ctx, s->T0, s->T0, s->T1);
    tcg_gen_mov_tl(tcg_ctx, cpu_cc_dst, s->T0);
    set_cc_op(s, CC_OP_BLSIB + ot);
}

static void gen_BLSMSK(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;

    tcg_gen_mov_tl(tcg_ctx, cpu_cc_src, s->T0);
    tcg_gen_subi_tl(tcg_ctx, s->T1, s->T0, 1);
    tcg_gen_xor_tl(tcg_ctx, s->T0, s->T0, s->T1);
    tcg_gen_mov_tl(tcg_ctx, cpu_cc_dst, s->T0);
    set_cc_op(s, CC_OP_BMILGB + ot);
}

static void gen_BLSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;

    tcg_gen_mov_tl(tcg_ctx, cpu_cc_src, s->T0);
    tcg_gen_subi_tl(tcg_ctx, s->T1, s->T0, 1);
    tcg_gen_and_tl(tcg_ctx, s->T0, s->T0, s->T1);
    tcg_gen_mov_tl(tcg_ctx, cpu_cc_dst, s->T0);
    set_cc_op(s, CC_OP_BMILGB + ot);
}

static void gen_BZHI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;
    TCGv bound = tcg_constant_tl(tcg_ctx, ot == MO_64 ? 63 : 31);
    TCGv zero = tcg_constant_tl(tcg_ctx, 0);
    TCGv mone = tcg_constant_tl(tcg_ctx, -1);

    tcg_gen_ext8u_tl(tcg_ctx, s->T1, s->T1);

    /*
     * Note that since we're using BMILG (in order to get O
     * cleared) we need to store the inverse into C.
     */
    tcg_gen_setcond_tl(tcg_ctx, TCG_COND_LEU, cpu_cc_src, s->T1, bound);

    tcg_gen_shl_tl(tcg_ctx, s->A0, mone, s->T1);
    tcg_gen_movcond_tl(tcg_ctx, TCG_COND_LEU, s->A0, s->T1, bound, s->A0, zero);
    tcg_gen_andc_tl(tcg_ctx, s->T0, s->T0, s->A0);

    gen_op_update1_cc(s);
    set_cc_op(s, CC_OP_BMILGB + ot);
}

/*
 * CMPccXADD m, r2, r3 (SDM Vol.2A "CMPccXADD"):
 *   tmp1 := load srcdest1; tmp2 := tmp1 + src3;
 *   EFLAGS := CMP tmp1, srcdest2;
 *   srcdest1 := cc ? tmp2 : tmp1   (memory is ALWAYS written);
 *   srcdest2 := tmp1
 *
 * 7.2 adaptation of upstream 405c7c0708:
 *  - 7.2 TCG temps (s->T0/T1/A0/tmp0, tcg_const_*) die at every brcond/label,
 *    so upstream's cmpxchg retry loop is replaced by straight-line code.
 *    Unicorn never sets CF_PARALLEL, where tcg_gen_atomic_cmpxchg_tl is
 *    itself a plain ld+movcond+st, so this is equivalent and does exactly one
 *    read and one write (one UC_HOOK_MEM_READ + one UC_HOOK_MEM_WRITE).
 *  - flags are computed by hand (not gen_jcc1) and committed only after
 *    the store, so a #PF on the store leaves RCX/EFLAGS untouched.
 *  - no decode->cc_* framework in 7.2: cc_dst/cc_src/cc_op are set here.
 *  - o64 check done here because .special is taken by "xchg".
 */
static void gen_CMPccXADD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv newv, cmpv, zero, cmp_lhs, cmp_rhs;
    TCGCond cond;
    MemOp ot, ot_full;

    int jcc_op = (decode->b >> 1) & 7;
    static const TCGCond cond_table[8] = {
        [JCC_O] = TCG_COND_LT,  /* test sign bit by comparing against 0 */
        [JCC_B] = TCG_COND_LTU,
        [JCC_Z] = TCG_COND_EQ,
        [JCC_BE] = TCG_COND_LEU,
        [JCC_S] = TCG_COND_LT,  /* test sign bit by comparing against 0 */
        [JCC_P] = TCG_COND_EQ,  /* even parity - tests low bit of popcount */
        [JCC_L] = TCG_COND_LT,
        [JCC_LE] = TCG_COND_LE,
    };

    if (!CODE64(s)) {
        /* V/N.E.: #UD outside 64-bit mode */
        gen_illegal_opcode(s);
        return;
    }

    cond = cond_table[jcc_op];
    if (decode->b & 1) {
        cond = tcg_invert_cond(cond);
    }

    ot = decode->op[0].ot;
    ot_full = ot | MO_LE;
    if (jcc_op >= JCC_S && ot < MO_64) {
        /*
         * Sign-extend values before subtracting for S, P (zero/sign extension
         * does not matter there) L, LE and their inverses.  Not for 64-bit
         * operands: nothing to extend, and the 7.2 branch's 5.0-era TCG
         * backend aborts on a sign-extending 64-bit load (MO_SQ).
         */
        ot_full |= MO_SIGN;
    }

    newv = tcg_temp_new(tcg_ctx);
    cmpv = tcg_temp_new(tcg_ctx);
    zero = tcg_const_tl(tcg_ctx, 0);

    /* cmpv := srcdest2, extended the same way as the memory operand */
    tcg_gen_mov_tl(tcg_ctx, cmpv, cpu_regs[decode->op[1].n]);
    gen_ext_tl(tcg_ctx, cmpv, cmpv, ot, (ot_full & MO_SIGN) != 0);

    /*
     * - s->T1: addition operand (src3, loaded by disas_insn_new)
     * - s->A0: dest address
     * - s->cc_srcT: memory operand (lhs for comparison)
     * - cmpv: rhs for comparison
     */
    gen_op_ld_v(s, ot_full, s->cc_srcT, s->A0);
    tcg_gen_sub_tl(tcg_ctx, s->T0, s->cc_srcT, cmpv);

    /* Compute the comparison result by hand, to avoid clobbering cc_*.  */
    switch (jcc_op) {
    case JCC_O:
        /* (src1 ^ src2) & (src1 ^ dst). newv is only used here for a moment */
        tcg_gen_xor_tl(tcg_ctx, newv, s->cc_srcT, s->T0);
        tcg_gen_xor_tl(tcg_ctx, s->tmp0, s->cc_srcT, cmpv);
        tcg_gen_and_tl(tcg_ctx, s->tmp0, s->tmp0, newv);
        tcg_gen_sextract_tl(tcg_ctx, s->tmp0, s->tmp0, 0, 8 << ot);
        cmp_lhs = s->tmp0, cmp_rhs = zero;
        break;

    case JCC_P:
        tcg_gen_ext8u_tl(tcg_ctx, s->tmp0, s->T0);
        tcg_gen_ctpop_tl(tcg_ctx, s->tmp0, s->tmp0);
        tcg_gen_andi_tl(tcg_ctx, s->tmp0, s->tmp0, 1);
        cmp_lhs = s->tmp0, cmp_rhs = zero;
        break;

    case JCC_S:
        tcg_gen_sextract_tl(tcg_ctx, s->tmp0, s->T0, 0, 8 << ot);
        cmp_lhs = s->tmp0, cmp_rhs = zero;
        break;

    default:
        cmp_lhs = s->cc_srcT, cmp_rhs = cmpv;
        break;
    }

    /* New value: if condition does not hold, store back s->cc_srcT unchanged */
    tcg_gen_add_tl(tcg_ctx, newv, s->cc_srcT, s->T1);
    tcg_gen_movcond_tl(tcg_ctx, cond, newv, cmp_lhs, cmp_rhs, newv, s->cc_srcT);
    gen_op_st_v(s, ot, newv, s->A0);

    /* Store old value to registers only after a successful store.  */
    gen_writeback(s, decode, 1, s->cc_srcT);

    /* EFLAGS := CMP tmp1, srcdest2 */
    tcg_gen_mov_tl(tcg_ctx, cpu_cc_dst, s->T0);
    tcg_gen_mov_tl(tcg_ctx, cpu_cc_src, cmpv);
    set_cc_op(s, CC_OP_SUBB + ot);

    tcg_temp_free(tcg_ctx, newv);
    tcg_temp_free(tcg_ctx, cmpv);
    tcg_temp_free(tcg_ctx, zero);
}

static void gen_CRC32(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[2].ot;

    tcg_gen_trunc_tl_i32(tcg_ctx, s->tmp2_i32, s->T0);
    gen_helper_crc32(tcg_ctx, s->T0, s->tmp2_i32, s->T1, tcg_constant_i32(tcg_ctx, 8 << ot));
}

static void gen_CVTPI2Px(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
#if __Use_Original_Qemu == 1 /* original QEMU (U44) */
    gen_helper_enter_mmx(tcg_ctx, cpu_env);
#else /* ours (U44) */
    /*
     * Only the MMX-register source transitions to MMX state. CVTPI2PD xmm, m64:
     * "does not cause a transition to MMX technology" (SDM Vol2 CVTPI2PD);
     * CVTPI2PS xmm, m64 transitions (its SDM page exempts no form; U532: the
     * i5-13600K does not, a documented deviation in docs/quirks.md).
     */
    if (!decode->op[2].has_ea || !(s->prefix & PREFIX_DATA)) {
        gen_helper_enter_mmx(tcg_ctx, cpu_env);
    }
#endif /* __Use_Original_Qemu (U44) */
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvtpi2pd(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtpi2ps(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_CVTPx2PI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_enter_mmx(tcg_ctx, cpu_env);
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvtpd2pi(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtps2pi(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_CVTTPx2PI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_enter_mmx(tcg_ctx, cpu_env);
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvttpd2pi(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvttps2pi(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_EMMS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_emms(tcg_ctx, cpu_env);
}

static void gen_EXTRQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 length = tcg_constant_i32(tcg_ctx, decode->immediate & 63);
    TCGv_i32 index = tcg_constant_i32(tcg_ctx, (decode->immediate >> 8) & 63);

    gen_helper_extrq_i(tcg_ctx, cpu_env, OP_PTR0, index, length);
}

static void gen_EXTRQ_r(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_extrq_r(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
}

static void gen_INSERTQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 length = tcg_constant_i32(tcg_ctx, decode->immediate & 63);
    TCGv_i32 index = tcg_constant_i32(tcg_ctx, (decode->immediate >> 8) & 63);

    gen_helper_insertq_i(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, index, length);
}

static void gen_INSERTQ_r(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_insertq_r(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
}

#if __Use_Original_Qemu != 1 /* ours (U130) */
/*
 * NoVmp (ledger U130): VEX opmask logic/arithmetic (SDM Vol2A KADD, KAND, KANDN, KNOT,
 * KOR, KUNPCK, KXNOR, KXOR). gen_load delivers SRC1 (VEX.vvvv) in T0 and SRC2 (r/m) in
 * T1, each SRC[w-1:0] zero-extended (X86_OP_KREG, U129); the KREG writeback keeps
 * DEST[w-1:0] and zeroes DEST[MAX_KL-1:w] (KADD's carry out of bit w-1 is dropped).
 */
static void gen_KADD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_add_tl(tcg_ctx, s->T0, s->T0, s->T1);
}

static void gen_KAND(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_and_tl(tcg_ctx, s->T0, s->T0, s->T1);
}

/* DEST := (BITWISE NOT SRC1) BITWISE AND SRC2 */
static void gen_KANDN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_andc_tl(tcg_ctx, s->T0, s->T1, s->T0);
}

/* KNOT k1, k2: the source is the r/m operand (T1) */
static void gen_KNOT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_not_tl(tcg_ctx, s->T0, s->T1);
}

static void gen_KOR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_or_tl(tcg_ctx, s->T0, s->T0, s->T1);
}

/*
 * KUNPCKBW/WD/DQ: DEST[w-1:0] := SRC2[w-1:0], DEST[2w-1:w] := SRC1[w-1:0] with w the
 * source size (8/16/32); the destination is 2w bits.
 */
static void gen_KUNPCK(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int w = 8 << decode->op[2].ot;

    tcg_gen_deposit_tl(tcg_ctx, s->T0, s->T1, s->T0, w, w);
}

/* DEST := NOT (SRC1 BITWISE XOR SRC2) */
static void gen_KXNOR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_eqv_tl(tcg_ctx, s->T0, s->T0, s->T1);
}

static void gen_KXOR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_xor_tl(tcg_ctx, s->T0, s->T0, s->T1);
}
#endif /* __Use_Original_Qemu (U130) */

#if __Use_Original_Qemu != 1 /* ours (U131) */
/*
 * NoVmp (ledger U131): KMOV k/m/r <- k/m/r. The source (op 2) is already SRC[w-1:0]
 * (k register or exactly w bits of memory) or the GPR; the writeback stores exactly w
 * bits to memory, zero-extends a k register to 64 bits, or writes the GPR at its size
 * (r32 zero-extends in 64-bit mode).
 */
static void gen_KMOV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_mov_tl(tcg_ctx, s->T0, s->T1);
}
#endif /* __Use_Original_Qemu (U131) */

#if __Use_Original_Qemu != 1 /* ours (U132) */
/*
 * NoVmp (ledger U132): KORTEST / KTEST set ZF and CF and clear OF, SF, AF, PF (SDM Vol2A
 * "Flags Affected"); the other EFLAGS bits are untouched. T0 = first operand (ModRM.reg),
 * T1 = second operand (r/m), both SRC[w-1:0]. CC_OP_EFLAGS: CC_SRC holds the six flags.
 */
static void gen_KORTEST(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int w = 8 << decode->op[1].ot;
    TCGv cf = tcg_temp_new(tcg_ctx);

    /* TMP := DEST OR SRC; ZF := TMP = 0; CF := TMP = all ones (w bits) */
    tcg_gen_or_tl(tcg_ctx, s->T0, s->T0, s->T1);
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, cf, s->T0,
                        (target_ulong)MAKE_64BIT_MASK(0, w));
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, s->T0, s->T0, 0);
    tcg_gen_shli_tl(tcg_ctx, s->T0, s->T0, ctz32(CC_Z));
    tcg_gen_or_tl(tcg_ctx, cpu_cc_src, s->T0, cf);
    set_cc_op(s, CC_OP_EFLAGS);
    tcg_temp_free(tcg_ctx, cf);
}

static void gen_KTEST(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv cf = tcg_temp_new(tcg_ctx);

    /* ZF := (SRC2 AND SRC1) = 0; CF := (SRC2 AND NOT SRC1) = 0 */
    tcg_gen_andc_tl(tcg_ctx, cf, s->T1, s->T0);
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, cf, cf, 0);
    tcg_gen_and_tl(tcg_ctx, s->T0, s->T1, s->T0);
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, s->T0, s->T0, 0);
    tcg_gen_shli_tl(tcg_ctx, s->T0, s->T0, ctz32(CC_Z));
    tcg_gen_or_tl(tcg_ctx, cpu_cc_src, s->T0, cf);
    set_cc_op(s, CC_OP_EFLAGS);
    tcg_temp_free(tcg_ctx, cf);
}
#endif /* __Use_Original_Qemu (U132) */

#if __Use_Original_Qemu != 1 /* ours (U133) */
/*
 * NoVmp (ledger U133): KSHIFTL / KSHIFTR k1, k2, imm8. COUNT := imm8[7:0] (unsigned);
 * DEST := 0 if COUNT > w - 1, else SRC1[w-1:0] shifted (the KREG writeback keeps the low
 * w bits of a left shift and zero-extends to 64). The source is T0 (op 1), already
 * SRC1[w-1:0]; the count is known at translation time.
 */
static void gen_KSHIFTL(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    unsigned w = 8u << decode->op[0].ot, count = decode->immediate & 0xff;

    if (count > w - 1) {
        tcg_gen_movi_tl(tcg_ctx, s->T0, 0);
    } else {
        tcg_gen_shli_tl(tcg_ctx, s->T0, s->T0, count);
    }
}

static void gen_KSHIFTR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    unsigned w = 8u << decode->op[0].ot, count = decode->immediate & 0xff;

    if (count > w - 1) {
        tcg_gen_movi_tl(tcg_ctx, s->T0, 0);
    } else {
        tcg_gen_shri_tl(tcg_ctx, s->T0, s->T0, count);
    }
}
#endif /* __Use_Original_Qemu (U133) */

static void gen_LDMXCSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    /* VEX.L = 1: chk(VEX128) in the decoder (backport 183e6679e3, U486) */
    tcg_gen_trunc_tl_i32(tcg_ctx, s->tmp2_i32, s->T1);
    gen_helper_ldmxcsr(tcg_ctx, cpu_env, s->tmp2_i32);
}

static void gen_MASKMOV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_mov_tl(tcg_ctx, s->A0, cpu_regs[R_EDI]);
    gen_extu(tcg_ctx, s->aflag, s->A0);
    gen_add_A0_ds_seg(s);

    if (s->prefix & PREFIX_DATA) {
        gen_helper_maskmov_xmm(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2, s->A0);
    } else {
#if __Use_Original_Qemu != 1 /* ours (U834) */
        /* MASKMOVQ: #AC for its 8-byte operand (SDM Vol3B Table 25-8); MASKMOVDQU is
           exception type 4 (no #AC row, Vol2A Table 2-21) (U834) */
        gen_ac_check(s, s->A0, 7);
#endif /* __Use_Original_Qemu (U834) */
        gen_helper_maskmov_mmx(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2, s->A0);
#if __Use_Original_Qemu != 1 /* ours (U873) */
        /* the x87-to-MMX transition after the store: none when it faults (U873) */
        gen_helper_enter_mmx(tcg_ctx, cpu_env);
#endif /* __Use_Original_Qemu (U873) */
    }
}

static void gen_MOVBE(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;

    /* M operand type does not load/store */
    if (decode->e.op0 == X86_TYPE_M) {
#if __Use_Original_Qemu != 1 /* ours (U834) */
        gen_ac_check_ot(s, s->A0, ot);
#endif /* __Use_Original_Qemu (U834) */
        tcg_gen_qemu_st_tl(tcg_ctx, s->T0, s->A0, s->mem_index, ot | MO_BE);
    } else {
#if __Use_Original_Qemu != 1 /* ours (U834) */
        gen_ac_check_ot(s, s->A0, ot);
#endif /* __Use_Original_Qemu (U834) */
        tcg_gen_qemu_ld_tl(tcg_ctx, s->T0, s->A0, s->mem_index, ot | MO_BE);
    }
}

#if __Use_Original_Qemu != 1 /* ours (U645) */
/*
 * NoVmp (ledger U645): APX EVEX map 4 MOVBE (60: rv, rv/mv; 61: rv/mv, rv), which also has the
 * register-register form ("MOVBE reg1, reg2", APX spec 3.1.2.3.1 2.(e)): T0 holds the source
 * (a memory source loaded little-endian), its bytes are swapped and written back to operand 0
 * (a memory destination stored little-endian): the same as a big-endian load / store.
 */
static void gen_MOVBE_APX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    switch (decode->op[0].ot) {
    case MO_16:
        tcg_gen_ext16u_tl(tcg_ctx, s->T0, s->T0);
        tcg_gen_bswap16_tl(tcg_ctx, s->T0, s->T0);
        break;
    case MO_32:
        tcg_gen_ext32u_tl(tcg_ctx, s->T0, s->T0);
        tcg_gen_bswap32_tl(tcg_ctx, s->T0, s->T0);
        break;
    default:
        tcg_gen_bswap64_i64(tcg_ctx, s->T0, s->T0);
        break;
    }
}

#endif /* __Use_Original_Qemu (U645) */

#if __Use_Original_Qemu != 1 /* ours (U72) */
/*
 * NoVmp (ledger U72): MOVDIRI, a direct store (write-combining protocol on
 * hardware; architecturally a 32/64-bit store). The M operand is not written
 * back by the generic code, so store here. 66h is not a valid prefix (#UD
 * via the table row); LOCK #UDs in disas_insn_new (op0 is not an INT unit).
 */
static void gen_MOVDIRI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

#if __Use_Original_Qemu != 1 /* ours (U834) */
    gen_ac_check_ot(s, s->A0, decode->op[0].ot);
#endif /* __Use_Original_Qemu (U834) */
    tcg_gen_qemu_st_tl(tcg_ctx, s->T0, s->A0, s->mem_index, decode->op[0].ot);
}

#endif /* __Use_Original_Qemu (U72) */
#if __Use_Original_Qemu != 1 /* ours (U73) */
/*
 * NoVmp (ledger U73): MOVDIR64B r, m512. Source: the memory operand (any
 * alignment, segment overrides apply); destination: ES:[register] at the
 * address size, 64-byte aligned or #GP (SDM Vol2 MOVDIR64B).
 */
static void gen_MOVDIR64B(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv src = tcg_temp_new(tcg_ctx);

    tcg_gen_mov_tl(tcg_ctx, src, s->A0);
    gen_lea_v_seg(s, s->aflag, cpu_regs[decode->op[1].n], R_ES, -1);
    gen_helper_movdir64b(tcg_ctx, cpu_env, s->A0, src);
    tcg_temp_free(tcg_ctx, src);
}

#endif /* __Use_Original_Qemu (U73) */
#if __Use_Original_Qemu != 1 /* ours (U100) */
/*
 * NoVmp (ledger U100): Key Locker. The work (CR4.KL #UD, CPL/reserved-bit
 * #GP, handle checks, AES-GCM-SIV wrap/unwrap, AES) is in misc_helper.c;
 * the helpers set CC_SRC to ZF (other arithmetic flags 0).
 */
static void gen_LOADIWKEY(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_loadiwkey(tcg_ctx, cpu_env, tcg_constant_i32(tcg_ctx, decode->op[0].n),
                         tcg_constant_i32(tcg_ctx, decode->op[2].n));
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_ENCODEKEY128(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_encodekey(tcg_ctx, s->T0, cpu_env, s->T1, tcg_constant_i32(tcg_ctx, 128));
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_ENCODEKEY256(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_encodekey(tcg_ctx, s->T0, cpu_env, s->T1, tcg_constant_i32(tcg_ctx, 256));
    set_cc_op(s, CC_OP_EFLAGS);
}

/* F3 0F 38 DC/DD/DE/DF: AESENC128KL, AESDEC128KL, AESENC256KL, AESDEC256KL */
static void gen_AESKL(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_aeskl(tcg_ctx, cpu_env, s->A0, tcg_constant_i32(tcg_ctx, decode->op[0].n),
                     tcg_constant_i32(tcg_ctx, decode->b - 0xdc));
    set_cc_op(s, CC_OP_EFLAGS);
}

/* F3 0F 38 D8 /0../3: AESENCWIDE128KL, AESDECWIDE128KL, AESENCWIDE256KL, AESDECWIDE256KL */
static void gen_AESKLWIDE(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_aeskl(tcg_ctx, cpu_env, s->A0, tcg_constant_i32(tcg_ctx, 0),
                     tcg_constant_i32(tcg_ctx, 4 | ((s->modrm >> 3) & 3)));
    set_cc_op(s, CC_OP_EFLAGS);
}

#endif /* __Use_Original_Qemu (U100) */
#if __Use_Original_Qemu != 1 /* ours (U101) */
/* NoVmp (ledger U101): RAO-INT, see helper_rao (mem_helper.c) */
static void gen_rao(DisasContext *s, X86DecodedInsn *decode, int op)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_rao(tcg_ctx, cpu_env, s->A0, s->T0,
                   tcg_constant_i32(tcg_ctx, (op << 4) | decode->op[0].ot));
}

static void gen_AADD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_rao(s, decode, 0);
}

static void gen_AAND(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_rao(s, decode, 1);
}

static void gen_AOR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_rao(s, decode, 2);
}

static void gen_AXOR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_rao(s, decode, 3);
}

#endif /* __Use_Original_Qemu (U101) */
#if __Use_Original_Qemu != 1 /* ours (U102) */
/* NoVmp (ledger U102): MOVRS r, m: DEST := SRC (the read-shared hint has no architectural effect) */
static void gen_MOVRS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_op_ld_v(s, decode->op[0].ot, s->T0, s->A0);
}

#endif /* __Use_Original_Qemu (U102) */
#if __Use_Original_Qemu != 1 /* ours (U103) */
/*
 * NoVmp (ledger U103): URDMSR / UWRMSR (helpers in misc_helper.c). The MSR
 * address is a 64-bit register or the zero-extended imm32.
 */
static void gen_URDMSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_urdmsr(tcg_ctx, s->T0, cpu_env, s->T0);
}

static void gen_URDMSR_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_urdmsr(tcg_ctx, s->T0, cpu_env,
                      tcg_constant_tl(tcg_ctx, (uint32_t)decode->immediate));
}

static void gen_UWRMSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_uwrmsr(tcg_ctx, cpu_env, s->T0, s->T1);
}

static void gen_UWRMSR_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_uwrmsr(tcg_ctx, cpu_env, tcg_constant_tl(tcg_ctx, (uint32_t)decode->immediate),
                      s->T0);
}

#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U803) */
/*
 * NoVmp (ledger U803): RDMSR r64, imm32 / WRMSRNS imm32, r64 (MSR-IMM): #GP(0) at CPL > 0, then
 * the access of the MSR named by the zero-extended imm32 through helper_rdmsr / helper_wrmsr
 * (x86_msr_access: same checks, faults and hooks); WRMSRNS ends the TB like WRMSR.
 */
static void gen_RDMSR_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (!check_cpl0(s)) {
        return;
    }
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_rdmsr_imm(tcg_ctx, s->T0, cpu_env,
                         tcg_constant_i32(tcg_ctx, (uint32_t)decode->immediate));
}

static void gen_WRMSRNS_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (!check_cpl0(s)) {
        return;
    }
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_wrmsr_imm(tcg_ctx, cpu_env, tcg_constant_i32(tcg_ctx, (uint32_t)decode->immediate),
                         s->T0);
    s->base.is_jmp = DISAS_EOB_NEXT;
}

#endif /* __Use_Original_Qemu (U803) */
#if __Use_Original_Qemu != 1 /* ours (U806) */
/*
 * NoVmp (ledger U806): INVPCID r32/r64, m128 (legacy 66 0F 38 82, APX EVEX map 4 F3 F2): #GP(0)
 * at CPL > 0 / virtual-8086 mode before the memory operand is read; then the 16-byte descriptor
 * (low quadword first) and helper_invpcid (type and descriptor checks, the TLB flush; the TB ends
 * as after INVLPG).
 */
static void gen_INVPCID(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv hi = tcg_temp_new(tcg_ctx);

    if (!check_cpl0(s)) {
        tcg_temp_free(tcg_ctx, hi);
        return;
    }
    if (!CODE64(s)) {
        tcg_gen_ext32u_tl(tcg_ctx, s->T0, s->T0);
    }
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_op_ld_v(s, MO_64, s->T1, s->A0);
    tcg_gen_addi_tl(tcg_ctx, s->A0, s->A0, 8);
    gen_op_ld_v(s, MO_64, hi, s->A0);
    gen_helper_invpcid(tcg_ctx, cpu_env, s->T0, s->T1, hi);
    tcg_temp_free(tcg_ctx, hi);
    s->base.is_jmp = DISAS_EOB_NEXT;
}

#endif /* __Use_Original_Qemu (U806) */
#if __Use_Original_Qemu != 1 /* ours (U804) */
/* NoVmp (ledger U804): HRESET imm8 (imm8 ignored): #GP(0) at CPL > 0 / virtual-8086, then EAX */
static void gen_HRESET(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (!check_cpl0(s)) {
        return;
    }
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_hreset(tcg_ctx, cpu_env);
}

#endif /* __Use_Original_Qemu (U804) */
#if __Use_Original_Qemu != 1 /* ours (U112) */
/*
 * NoVmp (ledger U112): ENQCMD (F2 0F 38 F8) / ENQCMDS (F3 0F 38 F8) r, m512.
 * Source: the memory operand (any alignment, overrides apply); destination:
 * ES:[register] at the address size, no override (SDM Vol2 ENQCMD/ENQCMDS);
 * the enqueue store itself is helper_enqcmd.
 */
static void gen_ENQCMD_common(DisasContext *s, X86DecodedInsn *decode, int sup)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv src = tcg_temp_new(tcg_ctx);

    tcg_gen_mov_tl(tcg_ctx, src, s->A0);
    gen_lea_v_seg(s, s->aflag, cpu_regs[decode->op[1].n], R_ES, -1);
    gen_helper_enqcmd(tcg_ctx, cpu_env, s->A0, src, tcg_constant_i32(tcg_ctx, sup));
    set_cc_op(s, CC_OP_EFLAGS);
    tcg_temp_free(tcg_ctx, src);
}

static void gen_ENQCMD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_ENQCMD_common(s, decode, 0);
}

static void gen_ENQCMDS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_ENQCMD_common(s, decode, 1);
}

#endif /* __Use_Original_Qemu (U112) */
#if __Use_Original_Qemu != 1 /* ours (U114) */
/*
 * NoVmp (ledger U114): WRSSD/WRSSQ (NP 0F 38 F6) and WRUSSD/WRUSSQ (66 0F 38
 * F5) m32/m64, r32/r64 (SDM Vol2): a shadow-stack store done by helper_wrss,
 * not by the generic write-back. #UD with LOCK and in real-address/virtual-8086
 * mode; WRSS also unless shadow stacks are enabled at the CPL (HF_CET_SS).
 */
static void gen_WRSS_common(DisasContext *s, X86DecodedInsn *decode, int user)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    decode->op[0].unit = X86_OP_SKIP;
    if ((s->prefix & PREFIX_LOCK) || !PE(s) || VM86(s)
        || (!user && !(s->flags & HF_CET_SS_MASK))) {
        gen_illegal_opcode(s);
        return;
    }
    gen_helper_wrss(tcg_ctx, cpu_env, s->A0, s->T0,
                    tcg_constant_i32(tcg_ctx, decode->op[0].ot == MO_64),
                    tcg_constant_i32(tcg_ctx, user));
}

static void gen_WRSS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_WRSS_common(s, decode, 0);
}

static void gen_WRUSS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_WRSS_common(s, decode, 1);
}

#endif /* __Use_Original_Qemu (U114) */
static void gen_MOVD_from(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[2].ot;

    switch (ot) {
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_ld32u_tl(tcg_ctx, s->T0, cpu_env, decode->op[2].offset);
        break;
    case MO_64:
#endif
        tcg_gen_ld_tl(tcg_ctx, s->T0, cpu_env, decode->op[2].offset);
        break;
    default:
        abort();
    }
}

static void gen_MOVD_to(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[2].ot;
    int vec_len = vector_len(s, decode);
    int lo_ofs = vector_elem_offset(&decode->op[0], ot, 0);

    tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);

    switch (ot) {
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_st32_tl(tcg_ctx, s->T1, cpu_env, lo_ofs);
        break;
    case MO_64:
#endif
        tcg_gen_st_tl(tcg_ctx, s->T1, cpu_env, lo_ofs);
        break;
    default:
        g_assert_not_reached();
    }
}

static void gen_MOVDQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_store_sse(s, decode, decode->op[2].offset);
}

static void gen_MOVMSK(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_i_ep ps, pd, fn;
    ps = s->vex_l ? gen_helper_movmskps_ymm : gen_helper_movmskps_xmm;
    pd = s->vex_l ? gen_helper_movmskpd_ymm : gen_helper_movmskpd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(tcg_ctx, s->tmp2_i32, cpu_env, OP_PTR2);
    tcg_gen_extu_i32_tl(tcg_ctx, s->T0, s->tmp2_i32);
}

static void gen_MOVQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);
    int lo_ofs = vector_elem_offset(&decode->op[0], MO_64, 0);

    tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[2].offset);
    if (decode->op[0].has_ea) {
#if __Use_Original_Qemu != 1 /* ours (U834) */
        gen_ac_check(s, s->A0, 7);
#endif /* __Use_Original_Qemu (U834) */
        tcg_gen_qemu_st_i64(tcg_ctx, s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    } else {
        /*
         * tcg_gen_gvec_dup_i64(tcg_ctx, MO_64, op0.offset, 8, vec_len, s->tmp1_64) would
         * seem to work, but it does not on big-endian platforms; the cleared parts
         * are always at higher addresses, but cross-endian emulation inverts the
         * byte order so that the cleared parts need to be at *lower* addresses.
         * Because oprsz is 8, we see this here even for SSE; but more in general,
         * it disqualifies using oprsz < maxsz to emulate VEX128.
         */
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
        tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, lo_ofs);
    }
}

static void gen_MOVq_dq(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_enter_mmx(tcg_ctx, cpu_env);
    /* Otherwise the same as any other movq.  */
    return gen_MOVQ(s, env, decode);
}

static void gen_MULX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;

    /* low part of result in VEX.vvvv, high in MODRM */
    switch (ot) {
    default:
        tcg_gen_trunc_tl_i32(tcg_ctx, s->tmp2_i32, s->T0);
        tcg_gen_trunc_tl_i32(tcg_ctx, s->tmp3_i32, s->T1);
        tcg_gen_mulu2_i32(tcg_ctx, s->tmp2_i32, s->tmp3_i32,
                          s->tmp2_i32, s->tmp3_i32);
#if __Use_Original_Qemu == 1 /* original QEMU (U93) */
        tcg_gen_extu_i32_tl(tcg_ctx, cpu_regs[s->vex_v], s->tmp2_i32);
#else /* ours (U93) */
        /* NoVmp (ledger U93): vvvv[3] ignored outside 64-bit mode (X86_TYPE_B) */
        tcg_gen_extu_i32_tl(tcg_ctx, cpu_regs[(CODE64(s) ? s->vex_v : (s->vex_v & 7))],
                            s->tmp2_i32);
#endif /* __Use_Original_Qemu (U93) */
        tcg_gen_extu_i32_tl(tcg_ctx, s->T0, s->tmp3_i32);
        break;
#ifdef TARGET_X86_64
    case MO_64:
        tcg_gen_mulu2_i64(tcg_ctx, cpu_regs[s->vex_v], s->T0, s->T0, s->T1);
        break;
#endif
    }

}

static void gen_PALIGNR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    if (!(s->prefix & PREFIX_DATA)) {
        gen_helper_palignr_mmx(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else if (!s->vex_l) {
        gen_helper_palignr_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else {
        gen_helper_palignr_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    }
}

static void gen_PANDN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    /* Careful, operand order is reversed!  */
    tcg_gen_gvec_andc(tcg_ctx, MO_64,
                      decode->op[0].offset, decode->op[2].offset,
                      decode->op[1].offset, vec_len, vec_len);
}

static void gen_PCMPESTRI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    gen_helper_pcmpestri_xmm(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2, imm);
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_PCMPESTRM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    gen_helper_pcmpestrm_xmm(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2, imm);
    set_cc_op(s, CC_OP_EFLAGS);
    if ((s->prefix & PREFIX_VEX) && !s->vex_l) {
#if __Use_Original_Qemu == 1 /* original QEMU (U126) */
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, offsetof(CPUX86State, xmm_regs[0].ZMM_X(1)),
                             16, 16, 0);
#else /* ours (U126) */
        /* SDM: in VEX.128 encoded versions bits (MAXVL-1:128) of XMM0 are zeroed */
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, offsetof(CPUX86State, xmm_regs[0].ZMM_X(1)),
                             48, 48, 0);
#endif /* __Use_Original_Qemu (U126) */
    }
}

static void gen_PCMPISTRI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    gen_helper_pcmpistri_xmm(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2, imm);
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_PCMPISTRM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    gen_helper_pcmpistrm_xmm(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2, imm);
    set_cc_op(s, CC_OP_EFLAGS);
    if ((s->prefix & PREFIX_VEX) && !s->vex_l) {
#if __Use_Original_Qemu == 1 /* original QEMU (U126) */
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, offsetof(CPUX86State, xmm_regs[0].ZMM_X(1)),
                             16, 16, 0);
#else /* ours (U126) */
        /* SDM: in VEX.128 encoded versions bits (MAXVL-1:128) of XMM0 are zeroed */
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, offsetof(CPUX86State, xmm_regs[0].ZMM_X(1)),
                             48, 48, 0);
#endif /* __Use_Original_Qemu (U126) */
    }
}

static void gen_PDEP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[1].ot;
    if (ot < MO_64) {
        tcg_gen_ext32u_tl(tcg_ctx, s->T0, s->T0);
    }
    gen_helper_pdep(tcg_ctx, s->T0, s->T0, s->T1);
}

static void gen_PEXT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[1].ot;
    if (ot < MO_64) {
        tcg_gen_ext32u_tl(tcg_ctx, s->T0, s->T0);
    }
    gen_helper_pext(tcg_ctx, s->T0, s->T0, s->T1);
}

static inline void gen_pextr(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode, MemOp ot)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);
    int mask = (vec_len >> ot) - 1;
    int val = decode->immediate & mask;

    switch (ot) {
    case MO_8:
        tcg_gen_ld8u_tl(tcg_ctx, s->T0, cpu_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_16:
        tcg_gen_ld16u_tl(tcg_ctx, s->T0, cpu_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_ld32u_tl(tcg_ctx, s->T0, cpu_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_64:
#endif
        tcg_gen_ld_tl(tcg_ctx, s->T0, cpu_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    default:
        abort();
    }
}

static void gen_PEXTRB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pextr(s, env, decode, MO_8);
}

static void gen_PEXTRW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pextr(s, env, decode, MO_16);
}

static void gen_PEXTR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    gen_pextr(s, env, decode, ot);
}

static inline void gen_pinsr(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode, MemOp ot)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);
    int mask = (vec_len >> ot) - 1;
    int val = decode->immediate & mask;

    if (decode->op[1].offset != decode->op[0].offset) {
        assert(vec_len == 16);
        gen_store_sse(s, decode, decode->op[1].offset);
    }

    switch (ot) {
    case MO_8:
        tcg_gen_st8_tl(tcg_ctx, s->T1, cpu_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_16:
        tcg_gen_st16_tl(tcg_ctx, s->T1, cpu_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_st32_tl(tcg_ctx, s->T1, cpu_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_64:
#endif
        tcg_gen_st_tl(tcg_ctx, s->T1, cpu_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    default:
        abort();
    }
}

static void gen_PINSRB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pinsr(s, env, decode, MO_8);
}

static void gen_PINSRW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pinsr(s, env, decode, MO_16);
}

static void gen_PINSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pinsr(s, env, decode, decode->op[2].ot);
}

static void gen_pmovmskb_i64(TCGContext *tcg_ctx, TCGv_i64 d, TCGv_i64 s)
{
    TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);

    tcg_gen_andi_i64(tcg_ctx, d, s, 0x8080808080808080ull);

    /*
     * After each shift+or pair:
     * 0:  a.......b.......c.......d.......e.......f.......g.......h.......
     * 7:  ab......bc......cd......de......ef......fg......gh......h.......
     * 14: abcd....bcde....cdef....defg....efgh....fgh.....gh......h.......
     * 28: abcdefghbcdefgh.cdefgh..defgh...efgh....fgh.....gh......h.......
     * The result is left in the high bits of the word.
     */
    tcg_gen_shli_i64(tcg_ctx, t, d, 7);
    tcg_gen_or_i64(tcg_ctx, d, d, t);
    tcg_gen_shli_i64(tcg_ctx, t, d, 14);
    tcg_gen_or_i64(tcg_ctx, d, d, t);
    tcg_gen_shli_i64(tcg_ctx, t, d, 28);
    tcg_gen_or_i64(tcg_ctx, d, d, t);
}

static void gen_pmovmskb_vec(TCGContext *tcg_ctx, unsigned vece,
                             TCGv_vec d, TCGv_vec s)
{
    TCGv_vec t = tcg_temp_new_vec_matching(tcg_ctx, d);
    TCGv_vec m = tcg_constant_vec_matching(tcg_ctx, d, MO_8, 0x80);

    /* See above */
    tcg_gen_and_vec(tcg_ctx, vece, d, s, m);
    tcg_gen_shli_vec(tcg_ctx, vece, t, d, 7);
    tcg_gen_or_vec(tcg_ctx, vece, d, d, t);
    tcg_gen_shli_vec(tcg_ctx, vece, t, d, 14);
    tcg_gen_or_vec(tcg_ctx, vece, d, d, t);
    tcg_gen_shli_vec(tcg_ctx, vece, t, d, 28);
    tcg_gen_or_vec(tcg_ctx, vece, d, d, t);
}

#ifdef TARGET_X86_64
#define TCG_TARGET_HAS_extract2_tl TCG_TARGET_HAS_extract2_i64
#else
#define TCG_TARGET_HAS_extract2_tl TCG_TARGET_HAS_extract2_i32
#endif

static void gen_PMOVMSKB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    static const TCGOpcode vecop_list[] = { INDEX_op_shli_vec, 0 };
    static const GVecGen2 g = {
        .fni8 = gen_pmovmskb_i64,
        .fniv = gen_pmovmskb_vec,
        .opt_opc = vecop_list,
        .vece = MO_64,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64
    };
    MemOp ot = decode->op[2].ot;
    int vec_len = vector_len(s, decode);
    TCGv t = tcg_temp_new(tcg_ctx);

    tcg_gen_gvec_2(tcg_ctx, offsetof(CPUX86State, xmm_t0) + xmm_offset(ot), decode->op[2].offset,
                   vec_len, vec_len, &g);
    tcg_gen_ld8u_tl(tcg_ctx, s->T0, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_B(vec_len - 1)));
    while (vec_len > 8) {
        vec_len -= 8;
        if (TCG_TARGET_HAS_extract2_tl) {
            /*
             * Load the next byte of the result into the high byte of T.
             * TCG does a similar expansion of deposit to shl+extract2; by
             * loading the whole word, the shift left is avoided.
             */
#ifdef TARGET_X86_64
            tcg_gen_ld_tl(tcg_ctx, t, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_Q((vec_len - 1) / 8)));
#else
            tcg_gen_ld_tl(tcg_ctx, t, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_L((vec_len - 1) / 4)));
#endif

            tcg_gen_extract2_tl(tcg_ctx, s->T0, t, s->T0, TARGET_LONG_BITS - 8);
        } else {
            /*
             * The _previous_ value is deposited into bits 8 and higher of t.  Because
             * those bits are known to be zero after ld8u, this becomes a shift+or
             * if deposit is not available.
             */
            tcg_gen_ld8u_tl(tcg_ctx, t, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_B(vec_len - 1)));
            tcg_gen_deposit_tl(tcg_ctx, s->T0, t, s->T0, 8, TARGET_LONG_BITS - 8);
        }
    }
    tcg_temp_free(tcg_ctx, t);
}

static void gen_PSHUFW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    gen_helper_pshufw_mmx(tcg_ctx, OP_PTR0, OP_PTR1, imm);
}

static void gen_PSRLW_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 16) {
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shri(tcg_ctx, MO_16,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSLLW_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 16) {
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shli(tcg_ctx, MO_16,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSRAW_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 16) {
        decode->immediate = 15;
    }
    tcg_gen_gvec_sari(tcg_ctx, MO_16,
                      decode->op[0].offset, decode->op[1].offset,
                      decode->immediate, vec_len, vec_len);
}

static void gen_PSRLD_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 32) {
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shri(tcg_ctx, MO_32,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSLLD_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 32) {
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shli(tcg_ctx, MO_32,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSRAD_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 32) {
        decode->immediate = 31;
    }
    tcg_gen_gvec_sari(tcg_ctx, MO_32,
                      decode->op[0].offset, decode->op[1].offset,
                      decode->immediate, vec_len, vec_len);
}

static void gen_PSRLQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 64) {
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shri(tcg_ctx, MO_64,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSLLQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 64) {
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shli(tcg_ctx, MO_64,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static TCGv_ptr make_imm8u_xmm_vec(TCGContext *tcg_ctx, uint8_t imm,
                                   int vec_len)
{
    MemOp ot = vec_len == 16 ? MO_128 : MO_256;
    TCGv_i32 imm_v = tcg_constant8u_i32(tcg_ctx, imm);
    TCGv_ptr ptr = tcg_temp_new_ptr(tcg_ctx);

    tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, offsetof(CPUX86State, xmm_t0) + xmm_offset(ot),
                         vec_len, vec_len, 0);

    tcg_gen_addi_ptr(tcg_ctx, ptr, cpu_env, offsetof(CPUX86State, xmm_t0));
    tcg_gen_st_i32(tcg_ctx, imm_v, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_L(0)));
    return ptr;
}

static void gen_PSRLDQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);
    TCGv_ptr imm_vec = make_imm8u_xmm_vec(tcg_ctx, decode->immediate,
                                          vec_len);

    if (s->vex_l) {
        gen_helper_psrldq_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, imm_vec);
    } else {
        gen_helper_psrldq_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, imm_vec);
    }
    tcg_temp_free_ptr(tcg_ctx, imm_vec);
}

static void gen_PSLLDQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);
    TCGv_ptr imm_vec = make_imm8u_xmm_vec(tcg_ctx, decode->immediate,
                                          vec_len);

    if (s->vex_l) {
        gen_helper_pslldq_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, imm_vec);
    } else {
        gen_helper_pslldq_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, imm_vec);
    }
    tcg_temp_free_ptr(tcg_ctx, imm_vec);
}

static void gen_RORX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;
    int b = decode->immediate;

    if (ot == MO_64) {
        tcg_gen_rotri_tl(tcg_ctx, s->T0, s->T0, b & 63);
    } else {
        tcg_gen_trunc_tl_i32(tcg_ctx, s->tmp2_i32, s->T0);
        tcg_gen_rotri_i32(tcg_ctx, s->tmp2_i32, s->tmp2_i32, b & 31);
        tcg_gen_extu_i32_tl(tcg_ctx, s->T0, s->tmp2_i32);
    }
}

static void gen_SARX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(tcg_ctx, s->T1, s->T1, mask);
    if (ot != MO_64) {
        tcg_gen_ext32s_tl(tcg_ctx, s->T0, s->T0);
    }
    tcg_gen_sar_tl(tcg_ctx, s->T0, s->T0, s->T1);
}

static void gen_SHA1NEXTE(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_sha1nexte(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA1MSG1(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_sha1msg1(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA1MSG2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_sha1msg2(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA1RNDS4(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    switch(decode->immediate & 3) {
    case 0:
        gen_helper_sha1rnds4_f0(tcg_ctx, OP_PTR0, OP_PTR0, OP_PTR1);
        break;
    case 1:
        gen_helper_sha1rnds4_f1(tcg_ctx, OP_PTR0, OP_PTR0, OP_PTR1);
        break;
    case 2:
        gen_helper_sha1rnds4_f2(tcg_ctx, OP_PTR0, OP_PTR0, OP_PTR1);
        break;
    case 3:
        gen_helper_sha1rnds4_f3(tcg_ctx, OP_PTR0, OP_PTR0, OP_PTR1);
        break;
    }
}

static void gen_SHA256MSG1(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_sha256msg1(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA256MSG2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_sha256msg2(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA256RNDS2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 wk0 = tcg_temp_new_i32(tcg_ctx);
    TCGv_i32 wk1 = tcg_temp_new_i32(tcg_ctx);

    /* implicit <XMM0>: WK0 = XMM0[31:0], WK1 = XMM0[63:32] */
    tcg_gen_ld_i32(tcg_ctx, wk0, cpu_env, ZMM_OFFSET(0) + offsetof(ZMMReg, ZMM_L(0)));
    tcg_gen_ld_i32(tcg_ctx, wk1, cpu_env, ZMM_OFFSET(0) + offsetof(ZMMReg, ZMM_L(1)));

    gen_helper_sha256rnds2(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2, wk0, wk1);
    tcg_temp_free_i32(tcg_ctx, wk0);
    tcg_temp_free_i32(tcg_ctx, wk1);
}

#if __Use_Original_Qemu != 1 /* ours (U82) */
/* SHA512 (VEX.256 only): op1 is SRCDEST (2op) or VEX.vvvv, op2 the ModRM:r/m register */
static void gen_VSHA512MSG1(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_vsha512msg1(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VSHA512MSG2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_vsha512msg2(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VSHA512RNDS2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_vsha512rnds2(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}
#endif /* __Use_Original_Qemu (U82) */

#if __Use_Original_Qemu != 1 /* ours (U83) */
/* SM3 (VEX.128 only): op0 SRCDEST, op1 VEX.vvvv, op2 xmm/m128 */
static void gen_VSM3MSG1(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_vsm3msg1(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VSM3MSG2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_vsm3msg2(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VSM3RNDS2(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    gen_helper_vsm3rnds2(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}
#endif /* __Use_Original_Qemu (U83) */

#if __Use_Original_Qemu != 1 /* ours (U84) */
/* SM4 (VEX.128 / VEX.256, 128-bit lanes): op0 DEST, op1 VEX.vvvv, op2 x/ymm or m128/m256 */
static void gen_VSM4KEY4(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if (!s->vex_l) {
        gen_helper_vsm4key4_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        gen_helper_vsm4key4_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    }
}

static void gen_VSM4RNDS4(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if (!s->vex_l) {
        gen_helper_vsm4rnds4_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        gen_helper_vsm4rnds4_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    }
}
#endif /* __Use_Original_Qemu (U84) */

static void gen_SHLX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(tcg_ctx, s->T1, s->T1, mask);
    tcg_gen_shl_tl(tcg_ctx, s->T0, s->T0, s->T1);
}

static void gen_SHRX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(tcg_ctx, s->T1, s->T1, mask);
    if (ot != MO_64) {
        tcg_gen_ext32u_tl(tcg_ctx, s->T0, s->T0);
    }
    tcg_gen_shr_tl(tcg_ctx, s->T0, s->T0, s->T1);
}

static void gen_VAESKEYGEN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    assert(!s->vex_l);
    gen_helper_aeskeygenassist_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, imm);
}

static void gen_STMXCSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    /* VEX.L = 1: chk(VEX128) in the decoder (backport 183e6679e3, U486) */
    gen_helper_update_mxcsr(tcg_ctx, cpu_env);
    tcg_gen_ld32u_tl(tcg_ctx, s->T0, cpu_env, offsetof(CPUX86State, mxcsr));
}

static void gen_VAESIMC(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    assert(!s->vex_l);
    gen_helper_aesimc_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
}

/*
 * 00 = v*ps Vps, Hps, Wpd
 * 66 = v*pd Vpd, Hpd, Wps
 * f3 = v*ss Vss, Hss, Wps
 * f2 = v*sd Vsd, Hsd, Wps
 */
#define SSE_CMP(x) { \
    gen_helper_ ## x ## ps ## _xmm, gen_helper_ ## x ## pd ## _xmm, \
    gen_helper_ ## x ## ss, gen_helper_ ## x ## sd, \
    gen_helper_ ## x ## ps ## _ymm, gen_helper_ ## x ## pd ## _ymm}
static const SSEFunc_0_eppp gen_helper_cmp_funcs[32][6] = {
    SSE_CMP(cmpeq),
    SSE_CMP(cmplt),
    SSE_CMP(cmple),
    SSE_CMP(cmpunord),
    SSE_CMP(cmpneq),
    SSE_CMP(cmpnlt),
    SSE_CMP(cmpnle),
    SSE_CMP(cmpord),

    SSE_CMP(cmpequ),
    SSE_CMP(cmpnge),
    SSE_CMP(cmpngt),
    SSE_CMP(cmpfalse),
    SSE_CMP(cmpnequ),
    SSE_CMP(cmpge),
    SSE_CMP(cmpgt),
    SSE_CMP(cmptrue),

    SSE_CMP(cmpeqs),
    SSE_CMP(cmpltq),
    SSE_CMP(cmpleq),
    SSE_CMP(cmpunords),
    SSE_CMP(cmpneqq),
    SSE_CMP(cmpnltq),
    SSE_CMP(cmpnleq),
    SSE_CMP(cmpords),

    SSE_CMP(cmpequs),
    SSE_CMP(cmpngeq),
    SSE_CMP(cmpngtq),
    SSE_CMP(cmpfalses),
    SSE_CMP(cmpnequs),
    SSE_CMP(cmpgeq),
    SSE_CMP(cmpgtq),
    SSE_CMP(cmptrues),
};
#undef SSE_CMP

static void gen_VCMP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int index = decode->immediate & (s->prefix & PREFIX_VEX ? 31 : 7);
    int b =
        s->prefix & PREFIX_REPZ  ? 2 /* ss */ :
        s->prefix & PREFIX_REPNZ ? 3 /* sd */ :
        !!(s->prefix & PREFIX_DATA) /* pd */ + (s->vex_l << 2);

    gen_helper_cmp_funcs[index][b](tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VCOMI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_epp fn;
    fn = s->prefix & PREFIX_DATA ? gen_helper_comisd : gen_helper_comiss;
    fn(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2);
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_VCVTPD2PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if (s->vex_l) {
        gen_helper_cvtpd2ps_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtpd2ps_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_VCVTPS2PD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if (s->vex_l) {
        gen_helper_cvtps2pd_ymm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtps2pd_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_VCVTPS2PH(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
#if __Use_Original_Qemu != 1 /* ours (U67) */
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    /* memory destination: #XM must come before the store (U67) */
    if (decode->op[0].has_ea) {
        gen_helper_sse_fp_begin(tcg_ctx, cpu_env);
    }
#endif /* __Use_Original_Qemu (U67) */
    gen_unary_imm_fp_sse(s, env, decode,
                      gen_helper_cvtps2ph_xmm,
                      gen_helper_cvtps2ph_ymm);
#if __Use_Original_Qemu != 1 /* ours (U67) */
    if (decode->op[0].has_ea) {
        gen_helper_sse_fp_end(tcg_ctx, cpu_env, tcg_constant_i32(tcg_ctx, 0),
                              tcg_constant_i32(tcg_ctx, 0));
    }
#endif /* __Use_Original_Qemu (U67) */
    /*
     * VCVTPS2PH is the only instruction that performs an operation on a
     * register source and then *stores* into memory.
     */
    if (decode->op[0].has_ea) {
        gen_store_sse(s, decode, decode->op[0].offset);
    }
}

static void gen_VCVTSD2SS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_cvtsd2ss(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VCVTSS2SD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_helper_cvtss2sd(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VCVTSI2Sx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);
    TCGv_i32 in;

    tcg_gen_gvec_mov(tcg_ctx, MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);

#ifdef TARGET_X86_64
    MemOp ot = decode->op[2].ot;
    if (ot == MO_64) {
        if (s->prefix & PREFIX_REPNZ) {
            gen_helper_cvtsq2sd(tcg_ctx, cpu_env, OP_PTR0, s->T1);
        } else {
            gen_helper_cvtsq2ss(tcg_ctx, cpu_env, OP_PTR0, s->T1);
        }
        return;
    }
    in = s->tmp2_i32;
    tcg_gen_trunc_tl_i32(tcg_ctx, in, s->T1);
#else
    in = s->T1;
#endif

    if (s->prefix & PREFIX_REPNZ) {
        gen_helper_cvtsi2sd(tcg_ctx, cpu_env, OP_PTR0, in);
    } else {
        gen_helper_cvtsi2ss(tcg_ctx, cpu_env, OP_PTR0, in);
    }
}

static inline void gen_VCVTtSx2SI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                  SSEFunc_i_ep ss2si, SSEFunc_l_ep ss2sq,
                                  SSEFunc_i_ep sd2si, SSEFunc_l_ep sd2sq)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 out;

#ifdef TARGET_X86_64
    MemOp ot = decode->op[0].ot;
    if (ot == MO_64) {
        if (s->prefix & PREFIX_REPNZ) {
            sd2sq(tcg_ctx, s->T0, cpu_env, OP_PTR2);
        } else {
            ss2sq(tcg_ctx, s->T0, cpu_env, OP_PTR2);
        }
        return;
    }

    out = s->tmp2_i32;
#else
    out = s->T0;
#endif
    if (s->prefix & PREFIX_REPNZ) {
        sd2si(tcg_ctx, out, cpu_env, OP_PTR2);
    } else {
        ss2si(tcg_ctx, out, cpu_env, OP_PTR2);
    }
#ifdef TARGET_X86_64
    tcg_gen_extu_i32_tl(tcg_ctx, s->T0, out);
#endif
}

#ifndef TARGET_X86_64
#define gen_helper_cvtss2sq NULL
#define gen_helper_cvtsd2sq NULL
#define gen_helper_cvttss2sq NULL
#define gen_helper_cvttsd2sq NULL
#endif

static void gen_VCVTSx2SI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_VCVTtSx2SI(s, env, decode,
                   gen_helper_cvtss2si, gen_helper_cvtss2sq,
                   gen_helper_cvtsd2si, gen_helper_cvtsd2sq);
}

static void gen_VCVTTSx2SI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_VCVTtSx2SI(s, env, decode,
                   gen_helper_cvttss2si, gen_helper_cvttss2sq,
                   gen_helper_cvttsd2si, gen_helper_cvttsd2sq);
}

static void gen_VEXTRACTx128(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int mask = decode->immediate & 1;
    int src_ofs = vector_elem_offset(&decode->op[1], MO_128, mask);
    if (decode->op[0].has_ea) {
        /* VEX-only instruction, no alignment requirements.  */
        gen_sto_env_A0(s, src_ofs, false);
    } else {
        tcg_gen_gvec_mov(tcg_ctx, MO_64, decode->op[0].offset, src_ofs, 16, 16);
    }
}

static void gen_VEXTRACTPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pextr(s, env, decode, MO_32);
}

static void gen_vinsertps(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int val = decode->immediate;
    int dest_word = (val >> 4) & 3;
    int new_mask = (val & 15) | (1 << dest_word);
    int vec_len = 16;

    assert(!s->vex_l);

    if (new_mask == 15) {
        /* All zeroes except possibly for the inserted element */
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else if (decode->op[1].offset != decode->op[0].offset) {
        gen_store_sse(s, decode, decode->op[1].offset);
    }

    if (new_mask != (val & 15)) {
        tcg_gen_st_i32(tcg_ctx, s->tmp2_i32, cpu_env,
                       vector_elem_offset(&decode->op[0], MO_32, dest_word));
    }

    if (new_mask != 15) {
        TCGv_i32 zero = tcg_constant_i32(tcg_ctx, 0); /* float32_zero */
        int i;
        for (i = 0; i < 4; i++) {
            if ((val >> i) & 1) {
                tcg_gen_st_i32(tcg_ctx, zero, cpu_env,
                               vector_elem_offset(&decode->op[0], MO_32, i));
            }
        }
    }
}

static void gen_VINSERTPS_r(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int val = decode->immediate;
    tcg_gen_ld_i32(tcg_ctx, s->tmp2_i32, cpu_env,
                   vector_elem_offset(&decode->op[2], MO_32, (val >> 6) & 3));
    gen_vinsertps(s, env, decode);
}

static void gen_VINSERTPS_m(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
#if __Use_Original_Qemu != 1 /* ours (U834) */
    gen_ac_check(s, s->A0, 3);
#endif /* __Use_Original_Qemu (U834) */
    tcg_gen_qemu_ld_i32(tcg_ctx, s->tmp2_i32, s->A0, s->mem_index, MO_LEUL);
    gen_vinsertps(s, env, decode);
}

static void gen_VINSERTx128(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int mask = decode->immediate & 1;
    tcg_gen_gvec_mov(tcg_ctx, MO_64,
                     decode->op[0].offset + offsetof(YMMReg, YMM_X(mask)),
                     decode->op[2].offset + offsetof(YMMReg, YMM_X(0)), 16, 16);
    tcg_gen_gvec_mov(tcg_ctx, MO_64,
                     decode->op[0].offset + offsetof(YMMReg, YMM_X(!mask)),
                     decode->op[1].offset + offsetof(YMMReg, YMM_X(!mask)), 16, 16);
}

static inline void gen_maskmov(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                               SSEFunc_0_eppt xmm, SSEFunc_0_eppt ymm)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if (!s->vex_l) {
        xmm(tcg_ctx, cpu_env, OP_PTR2, OP_PTR1, s->A0);
    } else {
        ymm(tcg_ctx, cpu_env, OP_PTR2, OP_PTR1, s->A0);
    }
}

static void gen_VMASKMOVPD_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_maskmov(s, env, decode, gen_helper_vpmaskmovq_st_xmm, gen_helper_vpmaskmovq_st_ymm);
}

static void gen_VMASKMOVPS_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_maskmov(s, env, decode, gen_helper_vpmaskmovd_st_xmm, gen_helper_vpmaskmovd_st_ymm);
}

static void gen_VMOVHPx_ld(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    gen_ldq_env_A0(s, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

static void gen_VMOVHPx_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_stq_env_A0(s, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
}

static void gen_VMOVHPx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    if (decode->op[0].offset != decode->op[2].offset) {
        tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
        tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    }
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

static void gen_VMOVHLPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
    tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(1)));
        tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    }
}

static void gen_VMOVLHPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[2].offset);
    tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

/*
 * Note that MOVLPx supports 256-bit operation unlike MOVHLPx, MOVLHPx, MOXHPx.
 * Use a gvec move to move everything above the bottom 64 bits.
 */

static void gen_VMOVLPx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(0)));
    tcg_gen_gvec_mov(tcg_ctx, MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
}

static void gen_VMOVLPx_ld(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

#if __Use_Original_Qemu != 1 /* ours (U834) */
    gen_ac_check(s, s->A0, 7);
#endif /* __Use_Original_Qemu (U834) */
    tcg_gen_qemu_ld_i64(tcg_ctx, s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    tcg_gen_gvec_mov(tcg_ctx, MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, OP_PTR0, offsetof(ZMMReg, ZMM_Q(0)));
}

static void gen_VMOVLPx_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_ld_i64(tcg_ctx, s->tmp1_i64, OP_PTR2, offsetof(ZMMReg, ZMM_Q(0)));
#if __Use_Original_Qemu != 1 /* ours (U834) */
    gen_ac_check(s, s->A0, 7);
#endif /* __Use_Original_Qemu (U834) */
    tcg_gen_qemu_st_i64(tcg_ctx, s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
}

static void gen_VMOVSD_ld(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i64 zero = tcg_constant_i64(tcg_ctx, 0);

#if __Use_Original_Qemu != 1 /* ours (U834) */
    gen_ac_check(s, s->A0, 7);
#endif /* __Use_Original_Qemu (U834) */
    tcg_gen_qemu_ld_i64(tcg_ctx, s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    tcg_gen_st_i64(tcg_ctx, zero, OP_PTR0, offsetof(ZMMReg, ZMM_Q(1)));
    tcg_gen_st_i64(tcg_ctx, s->tmp1_i64, OP_PTR0, offsetof(ZMMReg, ZMM_Q(0)));
}

static void gen_VMOVSS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

    tcg_gen_ld_i32(tcg_ctx, s->tmp2_i32, OP_PTR2, offsetof(ZMMReg, ZMM_L(0)));
    tcg_gen_gvec_mov(tcg_ctx, MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i32(tcg_ctx, s->tmp2_i32, OP_PTR0, offsetof(ZMMReg, ZMM_L(0)));
}

static void gen_VMOVSS_ld(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vec_len = vector_len(s, decode);

#if __Use_Original_Qemu != 1 /* ours (U834) */
    gen_ac_check(s, s->A0, 3);
#endif /* __Use_Original_Qemu (U834) */
    tcg_gen_qemu_ld_i32(tcg_ctx, s->tmp2_i32, s->A0, s->mem_index, MO_LEUL);
    tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    tcg_gen_st_i32(tcg_ctx, s->tmp2_i32, OP_PTR0, offsetof(ZMMReg, ZMM_L(0)));
}

static void gen_VMOVSS_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    tcg_gen_ld_i32(tcg_ctx, s->tmp2_i32, OP_PTR2, offsetof(ZMMReg, ZMM_L(0)));
#if __Use_Original_Qemu != 1 /* ours (U834) */
    gen_ac_check(s, s->A0, 3);
#endif /* __Use_Original_Qemu (U834) */
    tcg_gen_qemu_st_i32(tcg_ctx, s->tmp2_i32, s->A0, s->mem_index, MO_LEUL);
}

static void gen_VPMASKMOV_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    if (s->vex_w) {
        gen_VMASKMOVPD_st(s, env, decode);
    } else {
        gen_VMASKMOVPS_st(s, env, decode);
    }
}

static void gen_VPERMD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    assert(s->vex_l);
    gen_helper_vpermd_ymm(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VPERM2x128(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    assert(s->vex_l);
    gen_helper_vpermdq_ymm(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VPHMINPOSUW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    assert(!s->vex_l);
    gen_helper_phminposuw_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
}

static void gen_VROUNDSD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    assert(!s->vex_l);
    gen_helper_roundsd_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VROUNDSS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant8u_i32(tcg_ctx, decode->immediate);
    assert(!s->vex_l);
    gen_helper_roundss_xmm(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VSHUF(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_i32 imm = tcg_constant_i32(tcg_ctx, decode->immediate);
    SSEFunc_0_pppi ps, pd, fn;
    ps = s->vex_l ? gen_helper_shufps_ymm : gen_helper_shufps_xmm;
    pd = s->vex_l ? gen_helper_shufpd_ymm : gen_helper_shufpd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(tcg_ctx, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VUCOMI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_epp fn;
    fn = s->prefix & PREFIX_DATA ? gen_helper_ucomisd : gen_helper_ucomiss;
    fn(tcg_ctx, cpu_env, OP_PTR1, OP_PTR2);
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_VZEROALL(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    TCGv_ptr ptr = tcg_temp_new_ptr(tcg_ctx);

    tcg_gen_addi_ptr(tcg_ctx, ptr, cpu_env, offsetof(CPUX86State, xmm_regs));
#if __Use_Original_Qemu == 1 /* original QEMU (U126) */
    gen_helper_memset(tcg_ctx, ptr, ptr, tcg_constant_i32(tcg_ctx, 0),
                      tcg_constant_ptr(tcg_ctx, CPU_NB_REGS * sizeof(ZMMReg)));
#else /* ours (U126) */
    /* NoVmp (ledger U126): SDM VZEROALL - ZMM0-15 (64-bit mode) or ZMM0-7
       (otherwise) MAXVL-1:0 := 0; ZMM16-31 are not modified */
    gen_helper_memset(tcg_ctx, ptr, ptr, tcg_constant_i32(tcg_ctx, 0),
                      tcg_constant_ptr(tcg_ctx, (CODE64(s) ? 16 : 8) * sizeof(ZMMReg)));
#endif /* __Use_Original_Qemu (U126) */
    tcg_temp_free_ptr(tcg_ctx, ptr);
}

static void gen_VZEROUPPER(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int i;

#if __Use_Original_Qemu == 1 /* original QEMU (U126) */
    for (i = 0; i < CPU_NB_REGS; i++) {
        int offset = offsetof(CPUX86State, xmm_regs[i].ZMM_X(1));
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, offset, 16, 16, 0);
    }
#else /* ours (U126) */
    /* NoVmp (ledger U126): SDM VZEROUPPER - ZMM0-15 (64-bit mode) or ZMM0-7
       (otherwise) MAXVL-1:128 := 0; ZMM16-31 are not modified */
    for (i = 0; i < (CODE64(s) ? 16 : 8); i++) {
        int offset = offsetof(CPUX86State, xmm_regs[i].ZMM_X(1));
        tcg_gen_gvec_dup_imm(tcg_ctx, MO_64, offset, 48, 48, 0);
    }
#endif /* __Use_Original_Qemu (U126) */
}

#if __Use_Original_Qemu != 1 /* ours (U175) */
/*
 * NoVmp (ledger U175..U180): Intel AMX. decode_0F38_AMX / validate_vex (class 22) did the
 * table checks; the fixed ModRM fields are checked here; everything that depends on
 * CR4/XCR0/IA32_XFD/TILECFG is checked at run time by the helpers (fpu_helper.c).
 */

/* LDTILECFG m512: VEX.128.NP.0F38.W0 49 !(11):000:bbb */
static void gen_LDTILECFG(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if ((s->modrm >> 3) & 7) {
        gen_illegal_opcode(s);
        return;
    }
    gen_helper_amx_ldtilecfg(tcg_ctx, cpu_env, s->A0);
}

/* STTILECFG m512: VEX.128.66.0F38.W0 49 !(11):000:bbb */
static void gen_STTILECFG(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if ((s->modrm >> 3) & 7) {
        gen_illegal_opcode(s);
        return;
    }
    gen_helper_amx_sttilecfg(tcg_ctx, cpu_env, s->A0);
}

/* TILERELEASE: VEX.128.NP.0F38.W0 49 C0 */
static void gen_TILERELEASE(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (s->modrm != 0xc0) {
        gen_illegal_opcode(s);
        return;
    }
    gen_helper_amx_tilerelease(tcg_ctx, cpu_env);
}
#endif /* __Use_Original_Qemu (U175) */

#if __Use_Original_Qemu != 1 /* ours (U176) */
/*
 * TILELOADD / TILELOADDT1 / TILESTORED tmm, sibmem: "required to use SIB addressing"
 * (ModRM.r/m = 100b, else #UD). The helper gets membegin = base + displacement (no index),
 * stride = index << scale (0 when the SIB has no index register), the segment base (an
 * FS/GS/... override, 0 otherwise - as gen_lea_v_seg in 64-bit mode) and 32-bit address
 * size (67H).
 */
static void gen_amx_tile_mem(DisasContext *s, X86DecodedInsn *decode, bool store)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    AddressParts a = decode->mem;
    TCGv stride, base, seg;
    uint32_t info = ((s->modrm >> 3) & 7) | REX_R(s) | (s->aflag == MO_32 ? 0x100 : 0);

    if ((s->modrm & 7) != 4) {
        gen_illegal_opcode(s);
        return;
    }
    stride = tcg_temp_new(tcg_ctx);
    if (a.index >= 0) {
        tcg_gen_shli_tl(tcg_ctx, stride, cpu_regs[a.index], a.scale);
    } else {
        tcg_gen_movi_tl(tcg_ctx, stride, 0);
    }
    a.index = -1;
    base = gen_lea_modrm_1(s, a, false);
    seg = s->override >= 0 ? cpu_seg_base[s->override] : tcg_constant_tl(tcg_ctx, 0);
    if (store) {
        gen_helper_amx_tilestore(tcg_ctx, cpu_env, base, stride, seg,
                                 tcg_constant_i32(tcg_ctx, info));
    } else {
        gen_helper_amx_tileload(tcg_ctx, cpu_env, base, stride, seg,
                                tcg_constant_i32(tcg_ctx, info));
    }
    tcg_temp_free(tcg_ctx, stride);
}

/* TILELOADD tmm, sibmem: VEX.128.F2.0F38.W0 4B !(11):rrr:100 */
static void gen_TILELOADD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tile_mem(s, decode, false);
}

/* TILELOADDT1 tmm, sibmem: VEX.128.66.0F38.W0 4B !(11):rrr:100 (cache hint only) */
static void gen_TILELOADDT1(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tile_mem(s, decode, false);
}

/* TILESTORED sibmem, tmm: VEX.128.F3.0F38.W0 4B !(11):rrr:100 */
static void gen_TILESTORED(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tile_mem(s, decode, true);
}
#if __Use_Original_Qemu != 1 /* ours (U722) */

/*
 * NoVmp (ledger U722): AMX-MOVRS (ISE 319433-062 TILELOADDRS[,T1]): the Operation is that of
 * TILELOADD (start_row, zero_upper_rows, write_row_and_zero, AMX-E3); the read-shared hint
 * and T1 have no architectural effect.
 */
/* TILELOADDRS tmm, sibmem: VEX.128.F2.0F38.W0 4A !(11):rrr:100 */
static void gen_TILELOADDRS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tile_mem(s, decode, false);
}

/* TILELOADDRST1 tmm, sibmem: VEX.128.66.0F38.W0 4A !(11):rrr:100 */
static void gen_TILELOADDRST1(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tile_mem(s, decode, false);
}
#endif /* __Use_Original_Qemu (U722) */

/* TILEZERO tmm: VEX.128.F2.0F38.W0 49 11:rrr:000 */
static void gen_TILEZERO(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    if (s->modrm & 7) {
        gen_illegal_opcode(s);
        return;
    }
    gen_helper_amx_tilezero(tcg_ctx, cpu_env,
                            tcg_constant_i32(tcg_ctx, ((s->modrm >> 3) & 7) | REX_R(s)));
}
#endif /* __Use_Original_Qemu (U176) */

#if __Use_Original_Qemu != 1 /* ours (U177) */
/*
 * TMUL tsrcdest, tsrc1, tsrc2 (ModRM 11:rrr:bbb): tsrcdest = ModRM.reg | VEX.R, tsrc1 =
 * ModRM.r/m | VEX.B, tsrc2 = VEX.vvvv; op numbers as in fpu_helper.c (AMX_TDPBSSD..).
 */
static void gen_amx_tmul(DisasContext *s, int op)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t d = ((s->modrm >> 3) & 7) | REX_R(s);
    uint32_t s1 = (s->modrm & 7) | REX_B(s);

    gen_helper_amx_tmul(tcg_ctx, cpu_env,
                        tcg_constant_i32(tcg_ctx, op | d << 4 | s1 << 8 | s->vex_v << 12));
}

/* TDPBSSD/TDPBSUD/TDPBUSD/TDPBUUD: VEX.128.{F2,F3,66,NP}.0F38.W0 5E 11:rrr:bbb */
static void gen_TDPBSSD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 0);
}

static void gen_TDPBSUD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 1);
}

static void gen_TDPBUSD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 2);
}

static void gen_TDPBUUD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 3);
}
#endif /* __Use_Original_Qemu (U177) */

#if __Use_Original_Qemu != 1 /* ours (U178) */
/* TDPBF16PS: VEX.128.F3.0F38.W0 5C 11:rrr:bbb */
static void gen_TDPBF16PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 4);
}
#endif /* __Use_Original_Qemu (U178) */

#if __Use_Original_Qemu != 1 /* ours (U179) */
/* TDPFP16PS: VEX.128.F2.0F38.W0 5C 11:rrr:bbb */
static void gen_TDPFP16PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 5);
}
#endif /* __Use_Original_Qemu (U179) */

#if __Use_Original_Qemu != 1 /* ours (U180) */
/* TCMMIMFP16PS: VEX.128.66.0F38.W0 6C 11:rrr:bbb; TCMMRLFP16PS: VEX.128.NP.0F38.W0 6C */
static void gen_TCMMIMFP16PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 6);
}

static void gen_TCMMRLFP16PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 7);
}
#endif /* __Use_Original_Qemu (U180) */
#if __Use_Original_Qemu != 1 /* ours (U724) */
/* AMX-FP8: VEX.128.{NP,F2,F3,66}.MAP5.W0 FD 11:rrr:bbb (op numbers AMX_TDPBF8PS.. in fpu_helper.c) */
static void gen_TDPBF8PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 8);
}

static void gen_TDPBHF8PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 9);
}

static void gen_TDPHBF8PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 10);
}

static void gen_TDPHF8PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_tmul(s, 11);
}
#endif /* __Use_Original_Qemu (U724) */
#if __Use_Original_Qemu != 1 /* ours (U725) */

/*
 * NoVmp (ledger U725): AMX-AVX512 (ISE 319433-062 3.7), EVEX.512.W0, ModRM 11:rrr:bbb:
 * zmm1 = ModRM.reg | EVEX.R | EVEX.R' (0..31), tmm2 = ModRM.r/m | EVEX.B | EVEX.X (a name
 * >= max_names #UDs at run time), the row = r32 (EVEX.vvvv, a GPR 0..15; AMX-E8-EVEX) or imm8
 * (AMX-E7-EVEX), "& 0xf" in the helper. validate_amx_evex did the static #UDs; everything that
 * depends on CR4/XCR0/CR0.TS/IA32_XFD/TILECFG is checked by helper_amx_rowop.
 * op: 0 TCVTROWD2PS, 1 TCVTROWPS2BF16H, 2 TCVTROWPS2BF16L, 3 TCVTROWPS2PHH,
 * 4 TCVTROWPS2PHL, 5 TILEMOVROW (as in fpu_helper.c).
 */
static void gen_amx_rowop(DisasContext *s, X86DecodedInsn *decode, int op)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t z = ((s->modrm >> 3) & 7) | REX_R(s) | s->evex_rr;
    uint32_t t = (s->modrm & 7) | REX_B(s) | s->evex_x4;
    TCGv_i32 row;

    if (decode->e.op3 == X86_TYPE_I) {
        row = tcg_constant_i32(tcg_ctx, decode->immediate & 0xff);
    } else {
        row = tcg_temp_new_i32(tcg_ctx);
        tcg_gen_trunc_tl_i32(tcg_ctx, row, cpu_regs[s->vex_v & 15]);
    }
    gen_helper_amx_rowop(tcg_ctx, cpu_env,
                         tcg_constant_i32(tcg_ctx, op | z << 8 | (t & 31) << 16), row);
}

/* TCVTROWD2PS zmm1, tmm2, r32 / imm8: EVEX.512.F3.0F38.W0 4A / EVEX.512.F3.0F3A.W0 07 ib */
static void gen_TCVTROWD2PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_rowop(s, decode, 0);
}

/* TCVTROWPS2BF16H: EVEX.512.F2.0F38.W0 6D / EVEX.512.F2.0F3A.W0 07 ib */
static void gen_TCVTROWPS2BF16H(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_rowop(s, decode, 1);
}

/* TCVTROWPS2BF16L: EVEX.512.F3.0F38.W0 6D / EVEX.512.F3.0F3A.W0 77 ib */
static void gen_TCVTROWPS2BF16L(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_rowop(s, decode, 2);
}

/* TCVTROWPS2PHH: EVEX.512.NP.0F38.W0 6D / EVEX.512.NP.0F3A.W0 07 ib */
static void gen_TCVTROWPS2PHH(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_rowop(s, decode, 3);
}

/* TCVTROWPS2PHL: EVEX.512.66.0F38.W0 6D / EVEX.512.F2.0F3A.W0 77 ib */
static void gen_TCVTROWPS2PHL(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_rowop(s, decode, 4);
}

/* TILEMOVROW: EVEX.512.66.0F38.W0 4A / EVEX.512.66.0F3A.W0 07 ib */
static void gen_TILEMOVROW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_amx_rowop(s, decode, 5);
}
#endif /* __Use_Original_Qemu (U725) */
#if __Use_Original_Qemu != 1 /* ours (U321) */

/*
 * NoVmp (ledger U321): AVX512CD. VPLZCNTD/Q and VPCONFLICTD/Q compute every element of the
 * source (r/m or {1toN}) with helper_evex_elem_unop; masking, fault suppression (VPLZCNT,
 * E4) / whole-operand loads (VPCONFLICT, E4NF) and DEST[MAXVL-1:VL] := 0 come from
 * gen_evex_insn. VPBROADCASTMB2Q / VPBROADCASTMW2D: the low byte / word of k1 (already
 * zero-extended by the KU,b / KU,w operand load) in every qword / dword.
 */
static void gen_evex_elem_unop(DisasContext *s, X86DecodedInsn *decode, int op)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_elem_unop(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2,
                              tcg_constant_i32(tcg_ctx, EVEX_UNOP_DESC(op, s->evex_esz,
                                                                      vector_len(s, decode))));
}

static void gen_EVEX_VPLZCNT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_elem_unop(s, decode, EVEX_UNOP_LZCNT);
}

static void gen_EVEX_VPCONFLICT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_elem_unop(s, decode, EVEX_UNOP_CONFLICT);
}

static void gen_EVEX_VPBROADCASTM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int vl = vector_len(s, decode);
    TCGv_i64 t = tcg_temp_new_i64(tcg_ctx);

    tcg_gen_extu_tl_i64(tcg_ctx, t, s->T1);
    tcg_gen_gvec_dup_i64(tcg_ctx, s->evex_esz, decode->op[0].offset, vl, vl, t);
    tcg_temp_free_i64(tcg_ctx, t);
}
#endif /* __Use_Original_Qemu (U321) */
#if __Use_Original_Qemu != 1 /* ours (U322) */

/*
 * NoVmp (ledger U322): EVEX VPMADD52LUQ/HUQ with the U87 ops_sse.h helpers (SHIFT 1/2/3).
 * DEST is also the accumulator: X86OpEntry.evex_dsrc (U190) makes gen_evex_insn copy the
 * destination register into the result scratch register before this runs (OP_PTR0 is both
 * the accumulator and the result); the masked-off lanes come from the destination by the
 * blend.
 */
static void gen_evex_madd52(DisasContext *s, X86DecodedInsn *decode, bool hi)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    SSEFunc_0_eppp fn = hi ? SSE_FN3(s, decode, vpmadd52huq) : SSE_FN3(s, decode, vpmadd52luq);

    fn(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_EVEX_VPMADD52LUQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_madd52(s, decode, false);
}

static void gen_EVEX_VPMADD52HUQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_madd52(s, decode, true);
}
#endif /* __Use_Original_Qemu (U322) */
#if __Use_Original_Qemu != 1 /* ours (U323) */

/* NoVmp (ledger U323): VPOPCNTD/Q (and U324 VPOPCNTB/W): bits set per element */
static void gen_EVEX_VPOPCNT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_elem_unop(s, decode, EVEX_UNOP_POPCNT);
}
#endif /* __Use_Original_Qemu (U323) */
#if __Use_Original_Qemu != 1 /* ours (U324) */

/* NoVmp (ledger U324): VPSHUFBITQMB: the mask goes to tmp1_i64, ANDed with k2 by gen_evex_insn */
static void gen_EVEX_VPSHUFBITQMB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_shufbitqmb(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR1, OP_PTR2,
                               tcg_constant_i32(tcg_ctx, vector_len(s, decode)));
}
#endif /* __Use_Original_Qemu (U324) */
#if __Use_Original_Qemu != 1 /* ours (U325) */

/*
 * NoVmp (ledger U325): VPERMB / VPERMI2B / VPERMT2B (helper_evex_permb). The destination
 * register before the instruction (index of VPERMI2B, table of VPERMT2B) comes in OP_PTR0
 * (X86OpEntry.evex_dsrc, U868: gen_evex_insn loads the result scratch with it, U190);
 * VPERMB does not read it.
 */
static void gen_evex_permb(DisasContext *s, X86DecodedInsn *decode, int op)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_permb(tcg_ctx, cpu_env, OP_PTR0, OP_PTR0, OP_PTR1, OP_PTR2,
                          tcg_constant_i32(tcg_ctx, op | (vector_len(s, decode) << 8)));
}

static void gen_EVEX_VPERMB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_permb(s, decode, 0);
}

static void gen_EVEX_VPERMI2B(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_permb(s, decode, 1);
}

static void gen_EVEX_VPERMT2B(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_permb(s, decode, 2);
}
#endif /* __Use_Original_Qemu (U325) */
#if __Use_Original_Qemu != 1 /* ours (U571) */

/*
 * NoVmp (ledger U571): VP2INTERSECTD/Q k1+1, src1, src2 (SDM Vol2C): helper_evex_vp2intersect
 * writes maskregs[dest_base + 1] itself and returns maskregs[dest_base + 0], which
 * gen_evex_insn stores to op[0] (no masking: aaa = 000b, so nothing is ANDed); dest_base =
 * ModRM.reg & ~1. desc = EVEX_DESC(element size, KL) | (dest_base + 1) << 16.
 */
static void gen_EVEX_VP2INTERSECT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    int base = decode->op[0].n & ~1;
    uint32_t desc = EVEX_DESC(s->evex_esz, s->evex_vl >> s->evex_esz) |
                    ((uint32_t)(base + 1) << 16);

    gen_helper_evex_vp2intersect(tcg_ctx, s->tmp1_i64, cpu_env, OP_PTR1, OP_PTR2,
                                 tcg_constant_i32(tcg_ctx, desc));
    decode->op[0].n = base;
}
#endif /* __Use_Original_Qemu (U571) */
#if __Use_Original_Qemu != 1 /* ours (U572) */

/*
 * NoVmp (ledger U572): EVEX GFNI forms with the U70 ops_sse.h helpers at 128/256/512 bits
 * (SSE_FN3); masking, {1to8} (VGF2P8AFFINE*) and the memory operand come from gen_evex_insn.
 */
static void gen_EVEX_VGF2P8MULB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, gf2p8mulb)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_EVEX_VGF2P8AFFINEQB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, gf2p8affineqb)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                                      tcg_constant8u_i32(tcg_ctx, decode->immediate));
}

static void gen_EVEX_VGF2P8AFFINEINVQB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, gf2p8affineinvqb)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                                         tcg_constant8u_i32(tcg_ctx, decode->immediate));
}
#endif /* __Use_Original_Qemu (U572) */
#if __Use_Original_Qemu != 1 /* ours (U573) */

/* NoVmp (ledger U573): EVEX VAES forms, one AES round per 128-bit lane (ops_sse.h, SSE_FN3) */
#define EVEX_VAES(uname, lname)                                                          static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) {                                                                                            TCGContext *tcg_ctx = s->uc->tcg_ctx;                                                                                                                                             SSE_FN3(s, decode, lname)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);              }
EVEX_VAES(VAESENC, aesenc)
EVEX_VAES(VAESENCLAST, aesenclast)
EVEX_VAES(VAESDEC, aesdec)
EVEX_VAES(VAESDECLAST, aesdeclast)
#undef EVEX_VAES
#endif /* __Use_Original_Qemu (U573) */
#if __Use_Original_Qemu != 1 /* ours (U574) */

/* NoVmp (ledger U574): EVEX VPCLMULQDQ, one 64x64 carry-less multiply per 128-bit lane */
static void gen_EVEX_VPCLMULQDQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, pclmulqdq)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                                  tcg_constant8u_i32(tcg_ctx, decode->immediate));
}
#endif /* __Use_Original_Qemu (U574) */
#if __Use_Original_Qemu != 1 /* ours (U552) */

/*
 * NoVmp (ledgers U552, U553): VPSHLD/VPSHRD imm8 and VPSHLDV/VPSHRDV (helper_evex_vbmi2_shd;
 * desc: bit 0 right, bit 1 variable, element size, VL, imm8). The variable forms read DEST:
 * ev_dsrc makes OP_PTR0 the result scratch register holding DEST (U190).
 */
static void gen_evex_vbmi2_shd(DisasContext *s, X86DecodedInsn *decode, int op)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;
    uint32_t desc = op | (s->evex_esz << 4) | (s->evex_vl << 8);

    if (!(op & 2)) {
        desc |= (decode->immediate & 0xff) << 16;
    }
    gen_helper_evex_vbmi2_shd(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                              tcg_constant_i32(tcg_ctx, desc));
}

static void gen_EVEX_VPSHLD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_vbmi2_shd(s, decode, 0);
}

static void gen_EVEX_VPSHRD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_vbmi2_shd(s, decode, 1);
}
#endif /* __Use_Original_Qemu (U552) */
#if __Use_Original_Qemu != 1 /* ours (U553) */

static void gen_EVEX_VPSHLDV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_vbmi2_shd(s, decode, 2);
}

static void gen_EVEX_VPSHRDV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_evex_vbmi2_shd(s, decode, 3);
}
#endif /* __Use_Original_Qemu (U553) */
#if __Use_Original_Qemu != 1 /* ours (U554) */

/* NoVmp (ledger U554): VPMULTISHIFTQB (helper_evex_vpmultishiftqb); masking per byte (ev_ds) */
static void gen_EVEX_VPMULTISHIFTQB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_vpmultishiftqb(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                                   tcg_constant_i32(tcg_ctx, s->evex_vl));
}
#endif /* __Use_Original_Qemu (U554) */
#if __Use_Original_Qemu != 1 /* ours (U555) */

/*
 * NoVmp (ledger U555): EVEX VPDPBUSD(S) / VPDPWSSD(S) with the ops_sse.h helpers of the VEX
 * AVX-VNNI forms (U71) at 128/256/512 bits (SSE_FN3): per dword, DEST + 4 x (unsigned byte of
 * SRC1 * signed byte of SRC2) or 2 x (signed word * signed word), the S forms saturating the
 * whole sum to int32. DEST is the accumulator: OP_PTR0 holds it (ev_dsrc, U190).
 */
#define M4A_VNNI(uname, lname)                                                          \
static void gen_EVEX_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                       \
    TCGContext *tcg_ctx = s->uc->tcg_ctx;                                               \
                                                                                        \
    SSE_FN3(s, decode, lname)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);             \
}
M4A_VNNI(VPDPBUSD, vpdpbusd)
M4A_VNNI(VPDPBUSDS, vpdpbusds)
M4A_VNNI(VPDPWSSD, vpdpwssd)
M4A_VNNI(VPDPWSSDS, vpdpwssds)
#undef M4A_VNNI
#endif /* __Use_Original_Qemu (U555) */
#if __Use_Original_Qemu != 1 /* ours (U556) */

/*
 * NoVmp (ledger U556): VCVTNEPS2BF16 EVEX: the ops_sse.h helper of the VEX form (U88) at
 * 128/256/512 bits (the converted words in the lower VL/2 bytes, the rest zero); masking per
 * word and DEST[MAXVL-1:VL/2] := 0 by gen_evex_insn (destination operand xh, U210/U230).
 */
static void gen_EVEX_VCVTNEPS2BF16(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    SSE_FN3(s, decode, vcvtneps2bf16)(tcg_ctx, cpu_env, OP_PTR0, OP_PTR2);
}

/* VCVTNE2PS2BF16 (helper_evex_cvtne2ps2bf16): lower half from SRC2, upper half from SRC1 */
static void gen_EVEX_VCVTNE2PS2BF16(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_cvtne2ps2bf16(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                                  tcg_constant_i32(tcg_ctx, s->evex_vl));
}
#endif /* __Use_Original_Qemu (U556) */
#if __Use_Original_Qemu != 1 /* ours (U557) */

/* NoVmp (ledger U557): VDPBF16PS (helper_evex_vdpbf16ps); OP_PTR0 holds DEST (ev_dsrc) */
static void gen_EVEX_VDPBF16PS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGContext *tcg_ctx = s->uc->tcg_ctx;

    gen_helper_evex_vdpbf16ps(tcg_ctx, cpu_env, OP_PTR0, OP_PTR1, OP_PTR2,
                              tcg_constant_i32(tcg_ctx, s->evex_vl));
}
#endif /* __Use_Original_Qemu (U557) */
