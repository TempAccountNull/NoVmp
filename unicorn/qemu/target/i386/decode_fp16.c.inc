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
#if __Use_Original_Qemu != 1 /* ours (U335) */
/*
 * NoVmp (ledger U335): conversions. Source / destination sizes: "xh" = half, "xq" =
 * quarter of the vector length (U210); evex_es = source element, ev_ds (U230) = destination.
 */
/* packed rm with element sizes es -> des, tuple tt */
#define FP16_CV(op, d_, s_, tt, es, des, rc, w) X86_OP_ENTRY3(op, V,d_, None,None, W,s_,   \
                          cpuid(AVX512_FP16) ev_vl(EV16_VL) ev_tt(tt) ev_mask(MZ) ev_rc(rc) \
                          ev_w(w) ev_es(es) ev_ds(des))
/* scalar rvm es -> des */
#define FP16_CS(op, s_, es, des, rc, w) X86_OP_ENTRY3(op, V,x, H,x, W,s_,                  \
                          cpuid(AVX512_FP16) ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(MZ)      \
                          ev_rc(rc) ev_w(w) ev_es(es) ev_ds(des))
/* GPR destination: VCVT[T]SH2[U]SI r32/r64, xmm1/m16 (W1 = r64 in 64-bit mode) */
#define FP16_C2I(op, rc)  X86_OP_ENTRY3(op, G,y, None,None, W,w, cpuid(AVX512_FP16)          \
                          ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(NONE) ev_rc(rc) ev_w(WSEL)  \
                          ev_es(16) ev_w32)
/* GPR source: VCVT[U]SI2SH xmm1, xmm2, r/m32/64 */
#define FP16_I2C(op)      X86_OP_ENTRY3(op, V,x, H,x, E,y, cpuid(AVX512_FP16)                \
                          ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(NONE) ev_rc(ER) ev_w(WSEL)  \
                          ev_w32)

/* 1D: VCVTSS2SH (NP), VCVTPS2PHX (66) */
static void decode_FP16_M5_1D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_CS(FP16_CVTPS2PH, d, 32, 16, ER, W0),
        FP16_CV(FP16_CVTPS2PH, xh, x, FULL, 32, 16, ER, W0),
        {}, {},
    };
    decode_FP16_by_prefix(s, entry, tab);
}

/* 2A F3: VCVTSI2SH; 2C F3: VCVTTSH2SI; 2D F3: VCVTSH2SI */
static void decode_FP16_M5_2A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = { {}, {}, FP16_I2C(FP16_CVTSI2SH), {}, };
    decode_FP16_by_prefix(s, entry, tab);
}

static void decode_FP16_M5_2C(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = { {}, {}, FP16_C2I(FP16_CVTTSH2SI, SAE), {}, };
    decode_FP16_by_prefix(s, entry, tab);
}

static void decode_FP16_M5_2D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = { {}, {}, FP16_C2I(FP16_CVTSH2SI, ER), {}, };
    decode_FP16_by_prefix(s, entry, tab);
}

/* 5A: VCVTPH2PD (NP), VCVTPD2PH (66 W1), VCVTSH2SD (F3 W0), VCVTSD2SH (F2 W1) */
static void decode_FP16_M5_5A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_CV(FP16_CVTPH2PD, x, xq, QUARTER, 16, 64, SAE, W0),
        FP16_CV(FP16_CVTPD2PH, xq, x, FULL, 64, 16, ER, W1),
        FP16_CS(FP16_CVTPH2PD, w, 16, 64, SAE, W0),
        FP16_CS(FP16_CVTPD2PH, q, 64, 16, ER, W1),
    };
    decode_FP16_by_prefix(s, entry, tab);
}

/* 5B: VCVTDQ2PH (NP W0), VCVTQQ2PH (NP W1), VCVTPH2DQ (66), VCVTTPH2DQ (F3) */
static void decode_FP16_M5_5B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_CV(FP16_CVTDQ2PH, xh, x, FULL, 32, 16, ER, W0),
        FP16_CV(FP16_CVTPH2DQ, x, xh, HALF, 16, 32, ER, W0),
        FP16_CV(FP16_CVTTPH2DQ, x, xh, HALF, 16, 32, SAE, W0),
        {},
    };
    static const X86OpEntry qq2ph =
        FP16_CV(FP16_CVTQQ2PH, xq, x, FULL, 64, 16, ER, W1);

    if (!(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ)) && s->vex_w) {
        *entry = qq2ph;
        return;
    }
    decode_FP16_by_prefix(s, entry, tab);
}

/* 78: VCVTTPH2UDQ (NP), VCVTTPH2UQQ (66), VCVTTSH2USI (F3) */
static void decode_FP16_M5_78(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_CV(FP16_CVTTPH2UDQ, x, xh, HALF, 16, 32, SAE, W0),
        FP16_CV(FP16_CVTTPH2UQQ, x, xq, QUARTER, 16, 64, SAE, W0),
        FP16_C2I(FP16_CVTTSH2USI, SAE),
        {},
    };
    decode_FP16_by_prefix(s, entry, tab);
}

/* 79: VCVTPH2UDQ (NP), VCVTPH2UQQ (66), VCVTSH2USI (F3) */
static void decode_FP16_M5_79(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_CV(FP16_CVTPH2UDQ, x, xh, HALF, 16, 32, ER, W0),
        FP16_CV(FP16_CVTPH2UQQ, x, xq, QUARTER, 16, 64, ER, W0),
        FP16_C2I(FP16_CVTSH2USI, ER),
        {},
    };
    decode_FP16_by_prefix(s, entry, tab);
}

/* 7A: VCVTTPH2QQ (66), VCVTUDQ2PH (F2 W0), VCVTUQQ2PH (F2 W1) */
static void decode_FP16_M5_7A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        {},
        FP16_CV(FP16_CVTTPH2QQ, x, xq, QUARTER, 16, 64, SAE, W0),
        {},
        FP16_CV(FP16_CVTUDQ2PH, xh, x, FULL, 32, 16, ER, W0),
    };
    static const X86OpEntry uqq2ph =
        FP16_CV(FP16_CVTUQQ2PH, xq, x, FULL, 64, 16, ER, W1);

    if ((s->prefix & PREFIX_REPNZ) && s->vex_w) {
        *entry = uqq2ph;
        return;
    }
    decode_FP16_by_prefix(s, entry, tab);
}

/* 7B: VCVTPH2QQ (66), VCVTUSI2SH (F3) */
static void decode_FP16_M5_7B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        {},
        FP16_CV(FP16_CVTPH2QQ, x, xq, QUARTER, 16, 64, ER, W0),
        FP16_I2C(FP16_CVTUSI2SH),
        {},
    };
    decode_FP16_by_prefix(s, entry, tab);
}

/* 7C: VCVTTPH2UW (NP), VCVTTPH2W (66) */
static void decode_FP16_M5_7C(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_P1(FP16_CVTTPH2UW, SAE),
        FP16_P1(FP16_CVTTPH2W, SAE),
        {}, {},
    };
    decode_FP16_by_prefix(s, entry, tab);
}

/* 7D: VCVTPH2UW (NP), VCVTPH2W (66), VCVTW2PH (F3), VCVTUW2PH (F2) */
static void decode_FP16_M5_7D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_P1(FP16_CVTPH2UW, ER),
        FP16_P1(FP16_CVTPH2W, ER),
        FP16_P1(FP16_CVTW2PH, ER),
        FP16_P1(FP16_CVTUW2PH, ER),
    };
    decode_FP16_by_prefix(s, entry, tab);
}

/* map 6 13: VCVTSH2SS (NP), VCVTPH2PSX (66) */
static void decode_FP16_M6_13(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry tab[4] = {
        FP16_CS(FP16_CVTPH2PS, w, 16, 32, SAE, W0),
        FP16_CV(FP16_CVTPH2PS, x, xh, HALF, 16, 32, SAE, W0),
        {}, {},
    };
    decode_FP16_by_prefix(s, entry, tab);
}
#endif /* __Use_Original_Qemu (U335) */
#if __Use_Original_Qemu != 1 /* ours (U333) */

static const X86OpEntry opcodes_evex_map5[256] = {
    [0x51] = X86_OP_GROUP0(FP16_M5_51),
    [0x58] = X86_OP_GROUP0(FP16_M5_58),
    [0x59] = X86_OP_GROUP0(FP16_M5_59),
    [0x5c] = X86_OP_GROUP0(FP16_M5_5C),
    [0x5d] = X86_OP_GROUP0(FP16_M5_5D),
    [0x5e] = X86_OP_GROUP0(FP16_M5_5E),
    [0x5f] = X86_OP_GROUP0(FP16_M5_5F),
#if __Use_Original_Qemu != 1 /* ours (U335) */
    [0x1d] = X86_OP_GROUP0(FP16_M5_1D),
    [0x2a] = X86_OP_GROUP0(FP16_M5_2A),
    [0x2c] = X86_OP_GROUP0(FP16_M5_2C),
    [0x2d] = X86_OP_GROUP0(FP16_M5_2D),
    [0x5a] = X86_OP_GROUP0(FP16_M5_5A),
    [0x5b] = X86_OP_GROUP0(FP16_M5_5B),
    [0x78] = X86_OP_GROUP0(FP16_M5_78),
    [0x79] = X86_OP_GROUP0(FP16_M5_79),
    [0x7a] = X86_OP_GROUP0(FP16_M5_7A),
    [0x7b] = X86_OP_GROUP0(FP16_M5_7B),
    [0x7c] = X86_OP_GROUP0(FP16_M5_7C),
    [0x7d] = X86_OP_GROUP0(FP16_M5_7D),
#endif /* __Use_Original_Qemu (U335) */
};

static const X86OpEntry opcodes_evex_map6[256] = {
#if __Use_Original_Qemu != 1 /* ours (U334) */
    /*
     * NoVmp (ledger U334): 66 96..BF VF[N]MADD/VF[N]MSUB 132/213/231 PH (even opcodes
     * 98..9E, A8..AE, B8..BE) and SH (odd ones), VFMADDSUB/VFMSUBADD 132/213/231 PH (x6/x7)
     */
    /*
     * ev_dsrc (U190): the destination is a source; for the SH forms the engine's scalar
     * merge (U192) then keeps DEST[127:16] (SDM "DEST[127:16] remains unchanged"), as the
     * helper does (it reads the old destination through its dst pointer).
     */
#define FP16_FP(b_) [b_] = X86_OP_ENTRY3(FP16_FMA, V,x, H,x, W,x, EV16P(ER) ev_dsrc p_66)
#define FP16_FS(b_) [b_] = X86_OP_ENTRY3(FP16_FMA, V,x, H,x, W,w, EV16S(ER) ev_dsrc p_66)
    FP16_FP(0x96), FP16_FP(0x97), FP16_FP(0x98), FP16_FS(0x99), FP16_FP(0x9a), FP16_FS(0x9b),
    FP16_FP(0x9c), FP16_FS(0x9d), FP16_FP(0x9e), FP16_FS(0x9f),
    FP16_FP(0xa6), FP16_FP(0xa7), FP16_FP(0xa8), FP16_FS(0xa9), FP16_FP(0xaa), FP16_FS(0xab),
    FP16_FP(0xac), FP16_FS(0xad), FP16_FP(0xae), FP16_FS(0xaf),
    FP16_FP(0xb6), FP16_FP(0xb7), FP16_FP(0xb8), FP16_FS(0xb9), FP16_FP(0xba), FP16_FS(0xbb),
    FP16_FP(0xbc), FP16_FS(0xbd), FP16_FP(0xbe), FP16_FS(0xbf),
#undef FP16_FP
#undef FP16_FS
#endif /* __Use_Original_Qemu (U334) */
#if __Use_Original_Qemu != 1 /* ours (U335) */
    [0x13] = X86_OP_GROUP0(FP16_M6_13),
#endif /* __Use_Original_Qemu (U335) */
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
#if __Use_Original_Qemu != 1 /* ours (U335) */
#undef FP16_CV
#undef FP16_CS
#undef FP16_C2I
#undef FP16_I2C
#endif /* __Use_Original_Qemu (U335) */
