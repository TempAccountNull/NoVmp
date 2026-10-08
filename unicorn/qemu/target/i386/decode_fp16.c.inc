/*
 * NoVmp (ledgers U333-U339): AVX512-FP16 decoding - EVEX map 5 (EVEX.mmm = 101b), map 6
 * (110b) and the NP/F3 slots of map 3 (0F3A) - SDM Vol2C, rows of Emulator/data/
 * evex_forms.tsv with feature AVX512_FP16 (tuple type, masking, {er}/{sae}, W, VL).
 * Included from decode-new.c.inc inside the EVEX table section (the ev_* attribute
 * macros of U148 are defined there); our code only.
 *
 * Element sizes: evex_es = the source element size (broadcast, disp8*N, masked loads),
 * ev_ds (U230 evex_ds) = the destination / opmask element size of a conversion; a narrower
 * destination is declared by the size of operand 0 (xh / xq, U210 evex_dest_bytes).
 */
#if __Use_Original_Qemu != 1 /* ours (U333) */
#define EV16_VL      (X86_EVEX_VL128 | X86_EVEX_VL256 | X86_EVEX_VL512)
/* packed, {k1}{z}, tuple tt, element es */
#define EV16PX(rc, tt, es)  cpuid(AVX512_FP16) ev_vl(EV16_VL) ev_tt(tt) ev_mask(MZ) ev_rc(rc) \
                            ev_w(W0) ev_es(es)
/* packed FP16, Full tuple, {k1}{z}, {1to8/16/32} */
#define EV16P(rc)    EV16PX(rc, FULL, 16)
/* scalar, Tuple1 Scalar, {k1}{z} */
#define EV16SX(rc, es)      cpuid(AVX512_FP16) ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(MZ) \
                            ev_rc(rc) ev_w(W0) ev_es(es)
#define EV16S(rc)    EV16SX(rc, 16)

/* rvm: zmm1{k1}{z}, zmm2, zmm3/m/m16bcst | xmm1{k1}{z}, xmm2, xmm3/m16 */
#define FP16_PS(op, rc)  X86_OP_ENTRY3(op, V,x, H,x, W,x, EV16P(rc))
#define FP16_SS(op, rc)  X86_OP_ENTRY3(op, V,x, H,x, W,w, EV16S(rc))
/* rm: zmm1{k1}{z}, zmm2/m/m16bcst */
#define FP16_P1(op, rc)  X86_OP_ENTRY3(op, V,x, None,None, W,x, EV16P(rc))

static void decode_FP16_by_prefix(DisasContext *s, X86OpEntry *entry,
                                  const X86OpEntry tab[4])
{
    *entry = *decode_by_prefix(s, tab);
}

/* map 5: NP = packed PH, F3 = scalar SH */
#define FP16_M5_ARITH(opc, op, rc)                                                        \
static void decode_FP16_M5_##opc(DisasContext *s, CPUX86State *env, X86OpEntry *entry,     \
                                 uint8_t *b)                                               \
{                                                                                          \
    static const X86OpEntry tab[4] = {                                                     \
        FP16_PS(op, rc), {}, FP16_SS(op, rc), {},                                          \
    };                                                                                     \
    decode_FP16_by_prefix(s, entry, tab);                                                  \
}
FP16_M5_ARITH(58, FP16_ADD, ER)
FP16_M5_ARITH(59, FP16_MUL, ER)
FP16_M5_ARITH(5C, FP16_SUB, ER)
FP16_M5_ARITH(5D, FP16_MIN, SAE)
FP16_M5_ARITH(5E, FP16_DIV, ER)
FP16_M5_ARITH(5F, FP16_MAX, SAE)

/* 51: VSQRTPH zmm1, zmm2/m/m16bcst (NP), VSQRTSH xmm1, xmm2, xmm3/m16 (F3) */
static void decode_FP16_M5_51(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_P1(FP16_SQRT, ER), {}, FP16_SS(FP16_SQRT, ER), {},
    };
    decode_FP16_by_prefix(s, entry, tab);
}
#endif /* __Use_Original_Qemu (U333) */
#if __Use_Original_Qemu != 1 /* ours (U333) */

static const X86OpEntry opcodes_evex_map5[256] = {
    [0x51] = X86_OP_GROUP0(FP16_M5_51),
    [0x58] = X86_OP_GROUP0(FP16_M5_58),
    [0x59] = X86_OP_GROUP0(FP16_M5_59),
    [0x5c] = X86_OP_GROUP0(FP16_M5_5C),
    [0x5d] = X86_OP_GROUP0(FP16_M5_5D),
    [0x5e] = X86_OP_GROUP0(FP16_M5_5E),
    [0x5f] = X86_OP_GROUP0(FP16_M5_5F),
};

static const X86OpEntry opcodes_evex_map6[256] = {
};

static void decode_evex_map5(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    *entry = opcodes_evex_map5[*b];
}

static void decode_evex_map6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    *entry = opcodes_evex_map6[*b];
}

#undef FP16_M5_ARITH
#undef FP16_PS
#undef FP16_SS
#undef FP16_P1
#undef EV16P
#undef EV16S
#undef EV16PX
#undef EV16SX
#undef EV16_VL
#endif /* __Use_Original_Qemu (U333) */
