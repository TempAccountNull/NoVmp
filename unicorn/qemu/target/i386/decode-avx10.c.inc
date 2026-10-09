/*
 * Intel AVX10.2 instructions: EVEX decoding tables (NoVmp, ledgers U372-U399; included by
 * decode-new.c.inc after the EVEX maps 1-3, so the ev_* attribute macros of U148 apply).
 * Forms per AVX10.2 spec 361050-007 (rev 7.0) and Emulator/data/evex_forms.tsv. The AVX10.2
 * forms share opcode slots with AVX-512 forms of other prefixes (0F3A 08 F2 VRNDSCALEBF16 /
 * 66 VRNDSCALEPS, ...): decode_avx10_override() takes the AVX10.2 entry of a slot only when
 * the prefix selects one, so the AVX-512 tables stay untouched.
 */
#define A10_VLALL      (X86_EVEX_VL128 | X86_EVEX_VL256 | X86_EVEX_VL512)
/* packed BF16, Full tuple, {k1}{z}, {1toN} (m16bcst), W0, class E4 */
#define A10_BF16       cpuid(AVX10_2) ev_vl(A10_VLALL) ev_tt(FULL) ev_mask(MZ) ev_w(W0) ev_es(16)
#define A10_BF16_K     cpuid(AVX10_2) ev_vl(A10_VLALL) ev_tt(FULL) ev_mask(KDEST) ev_w(W0) ev_es(16)
#define A10_FP(es)     cpuid(AVX10_2) ev_vl(A10_VLALL) ev_tt(FULL) ev_mask(MZ) ev_rc(SAE) \
                       ev_es(es) ev_fp
#define A10_FS(es)     cpuid(AVX10_2) ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(MZ) ev_rc(SAE) \
                       ev_es(es) ev_fp
#define A10_CX(es)     cpuid(AVX10_2) ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(NONE) ev_rc(SAE) \
                       ev_es(es) ev_fp .evex_nofs = 1,
#define A10_BF16_3(op) X86_OP_ENTRY3(op, V,x, H,x, W,x, A10_BF16)
#define A10_BF16_2(op) X86_OP_ENTRY3(op, V,x, None,None, W,x, A10_BF16)
#define A10_BF16_2I(op) X86_OP_ENTRY4(op, V,x, None,None, W,x, A10_BF16)

/* one AVX10.2 form in a slot: NP, 66, F3, F2 */
#define A10_SLOT(name, np, p66, f3, f2)                                                 \
static void decode_##name(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b) \
{                                                                                       \
    static const X86OpEntry opcodes_##name[4] = { np, p66, f3, f2 };                   \
    *entry = *decode_by_prefix(s, opcodes_##name);                                      \
}

/* ---- U373: BF16 instructions (spec ch.7) ---- */
A10_SLOT(A10_MAP5_51, {}, A10_BF16_2(VSQRTBF16), {}, {})
A10_SLOT(A10_MAP5_58, {}, A10_BF16_3(VADDBF16), {}, {})
A10_SLOT(A10_MAP5_59, {}, A10_BF16_3(VMULBF16), {}, {})
A10_SLOT(A10_MAP5_5C, {}, A10_BF16_3(VSUBBF16), {}, {})
A10_SLOT(A10_MAP5_5D, {}, A10_BF16_3(VMINBF16), {}, {})
A10_SLOT(A10_MAP5_5E, {}, A10_BF16_3(VDIVBF16), {}, {})
A10_SLOT(A10_MAP5_5F, {}, A10_BF16_3(VMAXBF16), {}, {})
A10_SLOT(A10_MAP6_2C, A10_BF16_3(VSCALEFBF16), {}, {}, {})
A10_SLOT(A10_MAP6_42, A10_BF16_2(VGETEXPBF16), {}, {}, {})
A10_SLOT(A10_MAP6_4C, A10_BF16_2(VRCPBF16), {}, {}, {})
A10_SLOT(A10_MAP6_4E, A10_BF16_2(VRSQRTBF16), {}, {}, {})
A10_SLOT(A10_MAP6_98, A10_BF16_3(VFMADD132BF16), {}, {}, {})
A10_SLOT(A10_MAP6_9A, A10_BF16_3(VFMSUB132BF16), {}, {}, {})
A10_SLOT(A10_MAP6_9C, A10_BF16_3(VFNMADD132BF16), {}, {}, {})
A10_SLOT(A10_MAP6_9E, A10_BF16_3(VFNMSUB132BF16), {}, {}, {})
A10_SLOT(A10_MAP6_A8, A10_BF16_3(VFMADD213BF16), {}, {}, {})
A10_SLOT(A10_MAP6_AA, A10_BF16_3(VFMSUB213BF16), {}, {}, {})
A10_SLOT(A10_MAP6_AC, A10_BF16_3(VFNMADD213BF16), {}, {}, {})
A10_SLOT(A10_MAP6_AE, A10_BF16_3(VFNMSUB213BF16), {}, {}, {})
A10_SLOT(A10_MAP6_B8, A10_BF16_3(VFMADD231BF16), {}, {}, {})
A10_SLOT(A10_MAP6_BA, A10_BF16_3(VFMSUB231BF16), {}, {}, {})
A10_SLOT(A10_MAP6_BC, A10_BF16_3(VFNMADD231BF16), {}, {}, {})
A10_SLOT(A10_MAP6_BE, A10_BF16_3(VFNMSUB231BF16), {}, {}, {})
A10_SLOT(A10_0F3A_08, {}, {}, {}, A10_BF16_2I(VRNDSCALEBF16))
A10_SLOT(A10_0F3A_26, {}, {}, {}, A10_BF16_2I(VGETMANTBF16))
A10_SLOT(A10_0F3A_56, {}, {}, {}, A10_BF16_2I(VREDUCEBF16))

/* BF16 compares (spec 7.2, 7.3, 7.6) */
A10_SLOT(A10_0F3A_66, {}, {}, {},
         X86_OP_ENTRY4(VFPCLASSBF16, K,q, None,None, W,x, A10_BF16_K))
A10_SLOT(A10_0F3A_C2, {}, {}, {},
         X86_OP_ENTRY4(VCMPBF16, K,q, H,x, W,x, A10_BF16_K))
/* VCOMISBF16 xmm1, xmm2/m16: LLIG, Tuple1 Scalar, no masking, E10NF */
#define A10_VCOMISBF16 X86_OP_ENTRY3(VCOMISBF16, None,None, V,x, W,w, cpuid(AVX10_2)    \
                           ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(NONE) ev_w(W0) ev_es(16) \
                           .evex_nofs = 1,)

static void decode_A10_MAP5_2F(DisasContext *s, CPUX86State *env, X86OpEntry *entry,
                               uint8_t *b)
{
    static const X86OpEntry opcodes_A10_MAP5_2F[4] = {
        {}, A10_VCOMISBF16,
        /* U375: F3 W0 VCOMXSH (NP: VCOMISH, AVX512-FP16) */
        X86_OP_ENTRY3(VCOMXSH, None,None, V,x, W,w, A10_CX(16) ev_w(W0)),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_A10_MAP5_2F);
    if (s->evex_ll && (s->prefix & PREFIX_REPZ)) {
        memset(entry, 0, sizeof(*entry));               /* VCOMXSH: EVEX.LL != 00b #UD */
    }
}

/* ---- U374: MINMAX (spec ch.11) ---- */
/* packed PH/PS/PD: Full tuple, {k1}{z}, {1toN}, {sae} (512-bit reg-reg), class E2 */
/* scalar SH/SS/SD: LLIG, Tuple1 Scalar, {k1}{z}, {sae}, class E3 */
A10_SLOT(A10_0F3A_52,
         X86_OP_ENTRY4(VMINMAXPH, V,x, H,x, W,x, A10_FP(16) ev_w(W0)),
         X86_OP_ENTRY4(VMINMAXPx, V,x, H,x, W,x, A10_FP(W) ev_w(WSEL)),
         {},
         X86_OP_ENTRY4(VMINMAXBF16, V,x, H,x, W,x, A10_BF16))

/* 0F3A 53: NP W0 VMINMAXSH, 66 W0 VMINMAXSS, 66 W1 VMINMAXSD */
static void decode_A10_0F3A_53(DisasContext *s, CPUX86State *env, X86OpEntry *entry,
                               uint8_t *b)
{
    static const X86OpEntry sh =
        X86_OP_ENTRY4(VMINMAXSH, V,x, H,x, W,w, A10_FS(16) ev_w(W0));
    static const X86OpEntry ss =
        X86_OP_ENTRY4(VMINMAXSx, V,x, H,x, W,d, A10_FS(32) ev_w(W0));
    static const X86OpEntry sd =
        X86_OP_ENTRY4(VMINMAXSx, V,x, H,x, W,q, A10_FS(64) ev_w(W1));

    if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) {
        memset(entry, 0, sizeof(*entry));
    } else if (s->prefix & PREFIX_DATA) {
        *entry = s->vex_w ? sd : ss;
    } else {
        *entry = sh;
    }
}

/* ---- U375: VCOMX / VUCOMX (spec ch.8): LLIG but "#UD if EVEX.LL != 00b" (8.1.2) ---- */

/* 0F 2E/2F: F3 W0 VUCOMXSS/VCOMXSS, F2 W1 VUCOMXSD/VCOMXSD (NP/66: VUCOMISS/D, VCOMISS/D) */
static void decode_A10_0F_2E(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_A10_0F_2E[4] = {
        {}, {},
        X86_OP_ENTRY3(VUCOMXSS, None,None, V,x, W,d, A10_CX(32) ev_w(W0)),
        X86_OP_ENTRY3(VUCOMXSD, None,None, V,x, W,q, A10_CX(64) ev_w(W1)),
    };
    *entry = *decode_by_prefix(s, opcodes_A10_0F_2E);
    if (s->evex_ll) {
        memset(entry, 0, sizeof(*entry));
    }
}

static void decode_A10_0F_2F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_A10_0F_2F[4] = {
        {}, {},
        X86_OP_ENTRY3(VCOMXSS, None,None, V,x, W,d, A10_CX(32) ev_w(W0)),
        X86_OP_ENTRY3(VCOMXSD, None,None, V,x, W,q, A10_CX(64) ev_w(W1)),
    };
    *entry = *decode_by_prefix(s, opcodes_A10_0F_2F);
    if (s->evex_ll) {
        memset(entry, 0, sizeof(*entry));
    }
}

/* MAP5 2E: F3 W0 VUCOMXSH (NP: VUCOMISH, AVX512-FP16) */
static void decode_A10_MAP5_2E(DisasContext *s, CPUX86State *env, X86OpEntry *entry,
                               uint8_t *b)
{
    static const X86OpEntry opcodes_A10_MAP5_2E[4] = {
        {}, {},
        X86_OP_ENTRY3(VUCOMXSH, None,None, V,x, W,w, A10_CX(16) ev_w(W0)),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_A10_MAP5_2E);
    if (s->evex_ll && entry->gen) {
        memset(entry, 0, sizeof(*entry));
    }
}

/* ---- U376: saturating conversions (spec ch.12), EVEX map 5 68-6D ---- */
/* packed FP16/FP32/FP64 sources: {k1}{z}, {1toN}, {er}/{sae} (512-bit reg-reg), class E2 */
#define A10_CVT(tt, rc, es, w) cpuid(AVX10_2) ev_vl(A10_VLALL) ev_tt(tt) ev_mask(MZ) \
                               ev_rc(rc) ev_w(w) ev_es(es) ev_fp
#define A10_CVTP(op, tt, rc, es) X86_OP_ENTRY3(op, V,x, None,None, W,x, A10_CVT(tt, rc, es, W0))
/* scalar to r32/r64: LLIG, Tuple1 Scalar, no masking, {sae}, class E3NF; W ignored outside
   64-bit mode (r32) */
#define A10_CVTS(op, sz, es) X86_OP_ENTRY3(op, G,y, None,None, W,sz, cpuid(AVX10_2)          \
                                 ev_vl(X86_EVEX_LIG) ev_tt(T1S) ev_mask(NONE) ev_rc(SAE)      \
                                 ev_w(WSEL) ev_es(es) ev_fp ev_w32 .evex_nofs = 1,)

/* 68/6A: NP VCVTT PH2I[U]BS, 66 VCVTT PS2I[U]BS, F2 VCVTT BF162I[U]BS; 69/6B: the {er} forms */
A10_SLOT(A10_MAP5_68, A10_CVTP(VCVTTPH2IBS, FULL, SAE, 16), A10_CVTP(VCVTTPS2IBS, FULL, SAE, 32),
         {}, A10_BF16_2(VCVTTBF162IBS))
A10_SLOT(A10_MAP5_69, A10_CVTP(VCVTPH2IBS, FULL, ER, 16), A10_CVTP(VCVTPS2IBS, FULL, ER, 32),
         {}, A10_BF16_2(VCVTBF162IBS))
A10_SLOT(A10_MAP5_6A, A10_CVTP(VCVTTPH2IUBS, FULL, SAE, 16),
         A10_CVTP(VCVTTPS2IUBS, FULL, SAE, 32), {}, A10_BF16_2(VCVTTBF162IUBS))
A10_SLOT(A10_MAP5_6B, A10_CVTP(VCVTPH2IUBS, FULL, ER, 16), A10_CVTP(VCVTPS2IUBS, FULL, ER, 32),
         {}, A10_BF16_2(VCVTBF162IUBS))

/*
 * 6C / 6D: NP W0 VCVTTPS2UDQS / VCVTTPS2DQS, NP W1 VCVTTPD2UDQS / VCVTTPD2DQS (dword results
 * of qword sources: destination VL/2), 66 W0 VCVTTPS2UQQS / VCVTTPS2QQS (Half tuple source),
 * 66 W1 VCVTTPD2UQQS / VCVTTPD2QQS, F3 VCVTTSS2[U]SIS, F2 VCVTTSD2[U]SIS.
 */
static void decode_A10_MAP5_6C(DisasContext *s, CPUX86State *env, X86OpEntry *entry,
                               uint8_t *b)
{
    static const X86OpEntry ops[2][4] = {
        {
            A10_CVTP(VCVTTPS2UDQS, FULL, SAE, 32),
            X86_OP_ENTRY3(VCVTTPS2UQQS, V,x, None,None, W,xh, A10_CVT(HALF, SAE, 32, W0)
                          .evex_ds = X86_EVEX_ES_64,),
            A10_CVTS(VCVTTSS2USIS, d, 32),
            A10_CVTS(VCVTTSD2USIS, q, 64),
        }, {
            X86_OP_ENTRY3(VCVTTPD2UDQS, V,xh, None,None, W,x, A10_CVT(FULL, SAE, 64, W1)
                          .evex_ds = X86_EVEX_ES_32,),
            X86_OP_ENTRY3(VCVTTPD2UQQS, V,x, None,None, W,x, A10_CVT(FULL, SAE, 64, W1)),
            A10_CVTS(VCVTTSS2USIS, d, 32),
            A10_CVTS(VCVTTSD2USIS, q, 64),
        },
    };
    *entry = *decode_by_prefix(s, ops[s->vex_w]);
}

static void decode_A10_MAP5_6D(DisasContext *s, CPUX86State *env, X86OpEntry *entry,
                               uint8_t *b)
{
    static const X86OpEntry ops[2][4] = {
        {
            A10_CVTP(VCVTTPS2DQS, FULL, SAE, 32),
            X86_OP_ENTRY3(VCVTTPS2QQS, V,x, None,None, W,xh, A10_CVT(HALF, SAE, 32, W0)
                          .evex_ds = X86_EVEX_ES_64,),
            A10_CVTS(VCVTTSS2SIS, d, 32),
            A10_CVTS(VCVTTSD2SIS, q, 64),
        }, {
            X86_OP_ENTRY3(VCVTTPD2DQS, V,xh, None,None, W,x, A10_CVT(FULL, SAE, 64, W1)
                          .evex_ds = X86_EVEX_ES_32,),
            X86_OP_ENTRY3(VCVTTPD2QQS, V,x, None,None, W,x, A10_CVT(FULL, SAE, 64, W1)),
            A10_CVTS(VCVTTSS2SIS, d, 32),
            A10_CVTS(VCVTTSD2SIS, q, 64),
        },
    };
    *entry = *decode_by_prefix(s, ops[s->vex_w]);
}

/* ---- tables ---- */
static const X86OpEntry opcodes_avx10_0F[256] = {
    /* U375 */
    [0x2e] = X86_OP_GROUP0(A10_0F_2E),
    [0x2f] = X86_OP_GROUP0(A10_0F_2F),
};

static const X86OpEntry opcodes_avx10_0F3A[256] = {
    /* U373 */
    [0x08] = X86_OP_GROUP0(A10_0F3A_08),
    [0x26] = X86_OP_GROUP0(A10_0F3A_26),
    [0x56] = X86_OP_GROUP0(A10_0F3A_56),
    [0x66] = X86_OP_GROUP0(A10_0F3A_66),
    [0xc2] = X86_OP_GROUP0(A10_0F3A_C2),
    /* U374 */
    [0x52] = X86_OP_GROUP0(A10_0F3A_52),
    [0x53] = X86_OP_GROUP0(A10_0F3A_53),
};

static const X86OpEntry opcodes_avx10_map5[256] = {
    /* U373 (0x2f: VCOMISBF16; VCOMXSH from U375) */
    [0x2f] = X86_OP_GROUP0(A10_MAP5_2F),
    [0x51] = X86_OP_GROUP0(A10_MAP5_51),
    [0x58] = X86_OP_GROUP0(A10_MAP5_58),
    [0x59] = X86_OP_GROUP0(A10_MAP5_59),
    [0x5c] = X86_OP_GROUP0(A10_MAP5_5C),
    [0x5d] = X86_OP_GROUP0(A10_MAP5_5D),
    [0x5e] = X86_OP_GROUP0(A10_MAP5_5E),
    [0x5f] = X86_OP_GROUP0(A10_MAP5_5F),
    /* U375 */
    [0x2e] = X86_OP_GROUP0(A10_MAP5_2E),
    /* U376 */
    [0x68] = X86_OP_GROUP0(A10_MAP5_68),
    [0x69] = X86_OP_GROUP0(A10_MAP5_69),
    [0x6a] = X86_OP_GROUP0(A10_MAP5_6A),
    [0x6b] = X86_OP_GROUP0(A10_MAP5_6B),
    [0x6c] = X86_OP_GROUP0(A10_MAP5_6C),
    [0x6d] = X86_OP_GROUP0(A10_MAP5_6D),
};

static const X86OpEntry opcodes_avx10_map6[256] = {
    /* U373 */
    [0x2c] = X86_OP_GROUP0(A10_MAP6_2C),
    [0x42] = X86_OP_GROUP0(A10_MAP6_42),
    [0x4c] = X86_OP_GROUP0(A10_MAP6_4C),
    [0x4e] = X86_OP_GROUP0(A10_MAP6_4E),
    [0x98] = X86_OP_GROUP0(A10_MAP6_98),
    [0x9a] = X86_OP_GROUP0(A10_MAP6_9A),
    [0x9c] = X86_OP_GROUP0(A10_MAP6_9C),
    [0x9e] = X86_OP_GROUP0(A10_MAP6_9E),
    [0xa8] = X86_OP_GROUP0(A10_MAP6_A8),
    [0xaa] = X86_OP_GROUP0(A10_MAP6_AA),
    [0xac] = X86_OP_GROUP0(A10_MAP6_AC),
    [0xae] = X86_OP_GROUP0(A10_MAP6_AE),
    [0xb8] = X86_OP_GROUP0(A10_MAP6_B8),
    [0xba] = X86_OP_GROUP0(A10_MAP6_BA),
    [0xbc] = X86_OP_GROUP0(A10_MAP6_BC),
    [0xbe] = X86_OP_GROUP0(A10_MAP6_BE),
};

/*
 * The AVX10.2 entry of opcode *b in tab, resolved through its group decoder for the current
 * prefix/ModRM; replaces *entry only when there is one (other prefixes keep the slot's
 * AVX-512 / AVX512-FP16 form or #UD).
 */
static void decode_avx10_override(DisasContext *s, CPUX86State *env, X86OpEntry *entry,
                                  uint8_t *b, const X86OpEntry *tab)
{
    X86OpEntry e = tab[*b];

    while (e.is_decode) {
        e.is_decode = false;
        e.decode(s, env, &e, b);
    }
    if (e.gen) {
        *entry = e;
    }
}

#undef A10_VCOMISBF16
#undef A10_CVTS
#undef A10_CVTP
#undef A10_CVT
#undef A10_CX
#undef A10_FS
#undef A10_FP
#undef A10_SLOT
#undef A10_BF16_2I
#undef A10_BF16_2
#undef A10_BF16_3
#undef A10_BF16_K
#undef A10_BF16
#undef A10_VLALL
