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

/* ---- tables ---- */
static const X86OpEntry opcodes_avx10_0F[256] = {
};

static const X86OpEntry opcodes_avx10_0F3A[256] = {
};

static const X86OpEntry opcodes_avx10_map5[256] = {
};

static const X86OpEntry opcodes_avx10_map6[256] = {
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
