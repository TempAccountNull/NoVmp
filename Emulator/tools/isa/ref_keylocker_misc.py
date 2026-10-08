"""Independent reference for plan item 1.15c part 3 (ledger U100-U104).

Written from the manuals only (no CPU measurements):
  * Key Locker  - SDM Vol2 325383-092 (LOADIWKEY, ENCODEKEY128/256, AES*KL, AES*WIDE*KL) and the
                  Intel Key Locker Specification 343965-001 (handle format 1.4, CPUID.19H table 2-1,
                  A.3 functions, A.5 AES-GCM-SIV C code, footnote on page 45 with the zero-IWKey
                  vector).
  * AES         - FIPS-197 (S-box derived from the GF(2^8) inverse + affine map, not tabled),
                  checked against the FIPS-197 Appendix C vectors.
  * RAO-INT     - ISE 319433-062 AADD/AAND/AOR/AXOR.
  * MOVRS       - ISE 319433-062 MOVRS (legacy forms) / PREFETCHRST2.
  * USER_MSR    - SDM Vol2 URDMSR/UWRMSR, Vol4 IA32_USER_MSR_CTL (1CH), IA32_UARCH_MISC_CTL (1B01H).
  * UINTR       - SDM Vol2 CLUI/STUI/TESTUI/UIRET/SENDUIPI, Vol3A chapter 9 (user interrupts).

The AES-GCM-SIV key wrap is computed twice and cross-checked:
  (a) a literal port of the Key Locker spec's C code (A.5.2) with every SSE/AES-NI/PCLMUL intrinsic
      emulated from its SDM definition, and
  (b) an RFC 8452 formulation (POLYVAL as polynomial arithmetic with the x^-128 factor, standard
      FIPS-197 AES-256 key expansion).

usage:
  python ref_keylocker_misc.py            self-checks (exit status 1 on any mismatch)
  python ref_keylocker_misc.py --cases F  also writes the emu-alltest case file F (expected values)
"""
import sys

# ----------------------------------------------------------------------------------------------
# FIPS-197 AES
# ----------------------------------------------------------------------------------------------


def gf_mul(a, b):
    """GF(2^8) multiply modulo x^8 + x^4 + x^3 + x + 1 (FIPS-197 4.2)."""
    r = 0
    while b:
        if b & 1:
            r ^= a
        a = ((a << 1) ^ 0x11B) if a & 0x80 else (a << 1)
        b >>= 1
    return r


def gf_inv(a):
    """Multiplicative inverse (FIPS-197 5.1.1: 0 maps to 0); a^254."""
    if a == 0:
        return 0
    r, e, x = 1, 254, a
    while e:
        if e & 1:
            r = gf_mul(r, x)
        x = gf_mul(x, x)
        e >>= 1
    return r


def _make_sbox():
    sbox = []
    for a in range(256):
        b = gf_inv(a)
        c = 0
        for i in range(8):  # FIPS-197 eq. 5.1: b'_i = b_i ^ b_(i+4) ^ b_(i+5) ^ b_(i+6) ^ b_(i+7) ^ c_i
            bit = ((b >> i) ^ (b >> ((i + 4) % 8)) ^ (b >> ((i + 5) % 8)) ^
                   (b >> ((i + 6) % 8)) ^ (b >> ((i + 7) % 8)) ^ (0x63 >> i)) & 1
            c |= bit << i
        sbox.append(c)
    inv = [0] * 256
    for i, v in enumerate(sbox):
        inv[v] = i
    return sbox, inv


SBOX, INV_SBOX = _make_sbox()


def sub_bytes(s):
    return bytes(SBOX[x] for x in s)


def inv_sub_bytes(s):
    return bytes(INV_SBOX[x] for x in s)


def shift_rows(s):  # state byte i = row i%4, column i//4 (FIPS-197 3.4)
    return bytes(s[(i + 4 * (i % 4)) % 16] for i in range(16))


def inv_shift_rows(s):
    return bytes(s[(i - 4 * (i % 4)) % 16] for i in range(16))


def mix_columns(s):
    o = bytearray(16)
    for c in range(4):
        a = s[4 * c:4 * c + 4]
        for r in range(4):
            o[4 * c + r] = (gf_mul(a[r], 2) ^ gf_mul(a[(r + 1) % 4], 3) ^ a[(r + 2) % 4] ^ a[(r + 3) % 4])
    return bytes(o)


def inv_mix_columns(s):
    o = bytearray(16)
    for c in range(4):
        a = s[4 * c:4 * c + 4]
        for r in range(4):
            o[4 * c + r] = (gf_mul(a[r], 14) ^ gf_mul(a[(r + 1) % 4], 11) ^
                            gf_mul(a[(r + 2) % 4], 13) ^ gf_mul(a[(r + 3) % 4], 9))
    return bytes(o)


def xor16(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def key_expansion(key):
    """FIPS-197 5.2; returns the Nr+1 round keys (16 bytes each)."""
    nk = len(key) // 4
    nr = nk + 6
    w = [key[4 * i:4 * i + 4] for i in range(nk)]
    rcon = 1
    for i in range(nk, 4 * (nr + 1)):
        t = w[i - 1]
        if i % nk == 0:
            t = bytes(SBOX[x] for x in t[1:] + t[:1])
            t = bytes([t[0] ^ rcon]) + t[1:]
            rcon = gf_mul(rcon, 2)
        elif nk > 6 and i % nk == 4:
            t = bytes(SBOX[x] for x in t)
        w.append(bytes(x ^ y for x, y in zip(w[i - nk], t)))
    return [b''.join(w[4 * r:4 * r + 4]) for r in range(nr + 1)]


def aes_encrypt(key, block):
    rk = key_expansion(key)
    s = xor16(block, rk[0])
    for r in range(1, len(rk) - 1):
        s = xor16(mix_columns(shift_rows(sub_bytes(s))), rk[r])
    return xor16(shift_rows(sub_bytes(s)), rk[-1])


def aes_decrypt(key, block):
    """FIPS-197 5.3 inverse cipher."""
    rk = key_expansion(key)
    s = xor16(block, rk[-1])
    for r in range(len(rk) - 2, 0, -1):
        s = inv_mix_columns(xor16(inv_sub_bytes(inv_shift_rows(s)), rk[r]))
    return xor16(inv_sub_bytes(inv_shift_rows(s)), rk[0])


# ----------------------------------------------------------------------------------------------
# (a) Key Locker spec A.5.2 C code with emulated intrinsics (SDM Vol2 definitions)
# ----------------------------------------------------------------------------------------------
M128 = (1 << 128) - 1
M64 = (1 << 64) - 1


def b2i(b):
    return int.from_bytes(b, 'little')


def i2b(x):
    return (x & M128).to_bytes(16, 'little')


def setr_epi32(a, b, c, d):
    return (a & 0xFFFFFFFF) | (b & 0xFFFFFFFF) << 32 | (c & 0xFFFFFFFF) << 64 | (d & 0xFFFFFFFF) << 96


def setr_epi8(*v):
    return b2i(bytes(x & 0xFF for x in v))


def clmul64(a, b):
    r = 0
    for i in range(64):
        if (b >> i) & 1:
            r ^= a << i
    return r


def mm_clmulepi64(a, b, imm):  # PCLMULQDQ: imm[0] selects the qword of a, imm[4] that of b
    qa = (a >> 64) & M64 if imm & 0x01 else a & M64
    qb = (b >> 64) & M64 if imm & 0x10 else b & M64
    return clmul64(qa, qb)


def mm_slli_si128(a, n):
    return (a << (8 * n)) & M128


def mm_srli_si128(a, n):
    return a >> (8 * n)


def mm_shuffle_epi32(a, imm):
    d = [(a >> (32 * i)) & 0xFFFFFFFF for i in range(4)]
    return setr_epi32(*[d[(imm >> (2 * i)) & 3] for i in range(4)])


def mm_shuffle_epi8(a, m):  # PSHUFB
    ab, mb = i2b(a), i2b(m)
    return b2i(bytes(0 if mb[i] & 0x80 else ab[mb[i] & 15] for i in range(16)))


def mm_slli_epi64(a, n):
    return sum((((a >> (64 * i)) << n) & M64) << (64 * i) for i in range(2))


def mm_slli_epi32(a, n):
    return sum((((a >> (32 * i)) << n) & 0xFFFFFFFF) << (32 * i) for i in range(4))


def mm_add_epi32(a, b):
    return sum(((((a >> (32 * i)) + (b >> (32 * i))) & 0xFFFFFFFF) << (32 * i)) for i in range(4))


def mm_aesenc(a, k):  # SDM AESENC: ShiftRows, SubBytes, MixColumns, XOR round key
    return b2i(xor16(mix_columns(sub_bytes(shift_rows(i2b(a)))), i2b(k)))


def mm_aesenclast(a, k):
    return b2i(xor16(sub_bytes(shift_rows(i2b(a))), i2b(k)))


def horner_step(A, B, H):
    POLY = setr_epi32(0x1, 0, 0, 0xc2000000)
    _A = A ^ B
    TMP1 = mm_clmulepi64(_A, H, 0x00)
    TMP4 = mm_clmulepi64(_A, H, 0x11)
    TMP2 = mm_clmulepi64(_A, H, 0x10)
    TMP3 = mm_clmulepi64(_A, H, 0x01)
    TMP2 = TMP2 ^ TMP3
    TMP3 = mm_slli_si128(TMP2, 8)
    TMP2 = mm_srli_si128(TMP2, 8)
    TMP1 = TMP3 ^ TMP1
    TMP4 = TMP4 ^ TMP2
    TMP2 = mm_clmulepi64(TMP1, POLY, 0x10)
    TMP3 = mm_shuffle_epi32(TMP1, 78)
    TMP1 = TMP3 ^ TMP2
    TMP2 = mm_clmulepi64(TMP1, POLY, 0x10)
    TMP3 = mm_shuffle_epi32(TMP1, 78)
    TMP1 = TMP3 ^ TMP2
    return (TMP4 ^ TMP1) & M128


def aes256_ks1_enc1_on_the_fly(PT, key32):
    """Returns (CT, key schedule as 15 ints)."""
    KS = [0] * 15
    mask = setr_epi32(0x0c0f0e0d, 0x0c0f0e0d, 0x0c0f0e0d, 0x0c0f0e0d)
    con1 = setr_epi32(1, 1, 1, 1)
    con3 = setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 4, 5, 6, 7, 4, 5, 6, 7)
    xmm14 = 0
    xmm1 = b2i(key32[:16])
    xmm3 = b2i(key32[16:])
    KS[0] = xmm1
    b1 = PT ^ xmm1
    b1 = mm_aesenc(b1, xmm3)
    KS[1] = xmm3
    for i in range(6):
        xmm2 = mm_shuffle_epi8(xmm3, mask)
        xmm2 = mm_aesenclast(xmm2, con1)
        con1 = mm_slli_epi32(con1, 1)
        xmm4 = mm_slli_epi64(xmm1, 32)
        xmm1 = xmm1 ^ xmm4
        xmm4 = mm_shuffle_epi8(xmm1, con3)
        xmm1 = xmm1 ^ xmm4
        xmm1 = xmm1 ^ xmm2
        KS[(i + 1) * 2] = xmm1
        b1 = mm_aesenc(b1, xmm1)
        xmm2 = mm_shuffle_epi32(xmm1, 0xff)
        xmm2 = mm_aesenclast(xmm2, xmm14)
        xmm4 = mm_slli_epi64(xmm3, 32)
        xmm3 = xmm4 ^ xmm3
        xmm4 = mm_shuffle_epi8(xmm3, con3)
        xmm3 = xmm4 ^ xmm3
        xmm3 = xmm2 ^ xmm3
        KS[(i + 1) * 2 + 1] = xmm3
        b1 = mm_aesenc(b1, xmm3)
    xmm2 = mm_shuffle_epi8(xmm3, mask)
    xmm2 = mm_aesenclast(xmm2, con1)
    xmm4 = mm_slli_epi64(xmm1, 32)
    xmm1 = xmm1 ^ xmm4
    xmm4 = mm_shuffle_epi8(xmm1, con3)
    xmm1 = xmm1 ^ xmm4
    xmm1 = xmm1 ^ xmm2
    KS[14] = xmm1
    b1 = mm_aesenclast(b1, xmm1)
    return b1, KS


def aes_256_encrypt_ks(PT, KS):
    b = PT ^ KS[0]
    for r in range(1, 14):
        b = mm_aesenc(b, KS[r])
    return mm_aesenclast(b, KS[14])


TOP_ONE = setr_epi32(0, 0, 0, 0x80000000)
AND_MASK = setr_epi32(0xffffffff, 0xffffffff, 0xffffffff, 0x7fffffff)


def siv_key_wrap_spec(K1, K2, AAD, PT):
    """SIV_KEY_WRAP_16B / _32B (PT is 16 or 32 bytes). Returns (CT bytes, T bytes)."""
    k1 = b2i(K1)
    blocks = [b2i(PT[i:i + 16]) for i in range(0, len(PT), 16)]
    LENBLK = setr_epi32(16 * 8, 0, len(PT) * 8, 0)
    TMP = horner_step(b2i(AAD), 0, k1)
    for blk in blocks:
        TMP = horner_step(blk, TMP, k1)
    TMP = horner_step(TMP, LENBLK, k1)
    TMP &= AND_MASK
    TMP, KS = aes256_ks1_enc1_on_the_fly(TMP, K2)
    T = TMP
    TMP = TMP | TOP_ONE
    ctr = [TMP]
    if len(blocks) == 2:
        ctr.append(mm_add_epi32(TMP, setr_epi32(1, 0, 0, 0)))
    CT = b''.join(i2b(aes_256_encrypt_ks(c, KS) ^ blk) for c, blk in zip(ctr, blocks))
    return CT, i2b(T)


def siv_key_unwrap_spec(K1, K2, AAD, CT, T):
    """SIV_KEY_UNWRAP_16B / _32B. Returns (ok, PT bytes)."""
    k1 = b2i(K1)
    TMP = b2i(T) | TOP_ONE
    blocks = [b2i(CT[i:i + 16]) for i in range(0, len(CT), 16)]
    ctr = [TMP]
    if len(blocks) == 2:
        ctr.append(mm_add_epi32(TMP, setr_epi32(1, 0, 0, 0)))
    first, KS = aes256_ks1_enc1_on_the_fly(ctr[0], K2)
    ks_out = [first] + [aes_256_encrypt_ks(c, KS) for c in ctr[1:]]
    pt = [k ^ c for k, c in zip(ks_out, blocks)]
    LENBLK = setr_epi32(16 * 8, 0, len(CT) * 8, 0)
    TMP2 = horner_step(b2i(AAD), 0, k1)
    for p in pt:
        TMP2 = horner_step(p, TMP2, k1)
    TMP2 = horner_step(TMP2, LENBLK, k1)
    TMP2 &= AND_MASK
    TMP2 = aes_256_encrypt_ks(TMP2, KS)
    return TMP2 == b2i(T), b''.join(i2b(p) for p in pt)


# ----------------------------------------------------------------------------------------------
# (b) RFC 8452 formulation: POLYVAL with explicit field arithmetic, FIPS-197 AES-256
# ----------------------------------------------------------------------------------------------
POLYVAL_P = (1 << 128) | (1 << 127) | (1 << 126) | (1 << 121) | 1


def gf128_mul_mod(a, b):
    r = 0
    for i in range(128):
        if (b >> i) & 1:
            r ^= a << i
    for i in range(255, 127, -1):
        if (r >> i) & 1:
            r ^= POLYVAL_P << (i - 128)
    return r


# x^-128 mod P: the inverse of x^128 (computed by exponentiation in the field, P is irreducible)
def _x_inv128():
    x128 = gf128_mul_mod(1 << 127, 2)  # x^128 mod P
    # inverse via a^(2^128 - 2)
    r, base, e = 1, x128, (1 << 128) - 2
    while e:
        if e & 1:
            r = gf128_mul_mod(r, base)
        base = gf128_mul_mod(base, base)
        e >>= 1
    return r


X_INV128 = _x_inv128()


def polyval_dot(a, b):  # RFC 8452 3: dot(a, b) = a * b * x^-128
    return gf128_mul_mod(gf128_mul_mod(a, b), X_INV128)


def polyval(h, blocks):
    s = 0
    for x in blocks:
        s = polyval_dot(s ^ x, h)
    return s


def siv_key_wrap_rfc(K1, K2, AAD, PT):
    blocks = [b2i(AAD)] + [b2i(PT[i:i + 16]) for i in range(0, len(PT), 16)]
    blocks.append((16 * 8) | ((len(PT) * 8) << 64))
    s = polyval(b2i(K1), blocks) & ~(1 << 127)
    tag = aes_encrypt(K2, i2b(s))
    ctr = b2i(tag) | (1 << 127)
    out = b''
    for j in range(0, len(PT), 16):
        c = (ctr & ~0xFFFFFFFF) | ((ctr + j // 16) & 0xFFFFFFFF)
        out += xor16(aes_encrypt(K2, i2b(c)), PT[j:j + 16])
    return out, tag


# ----------------------------------------------------------------------------------------------
# Key Locker instruction models (SDM Vol2 Operation sections)
# ----------------------------------------------------------------------------------------------
class IWKey:
    def __init__(self):
        self.integrity = bytes(16)
        self.encryption = bytes(32)
        self.nobackup = 0
        self.keysource = 0


# Our CPUID.19H model (documented in the commit / ledger): EAX = 7 (CPL0-only, no-encrypt,
# no-decrypt restrictions supported), EBX = AESKLE (CR4.KL) | AES_WIDE, ECX = 1 (NoBackup
# supported; KeySource 1 not supported -> #GP), backup MSRs not enumerated.
CPUID19_EAX = 0x7
CPUID19_ECX = 0x1


def loadiwkey(iw, cpl, eax, xmm0, src1, src2):
    """Returns ('#GP', None) or (None, zf)."""
    if cpl > 0:
        return '#GP', None
    ks = (eax >> 1) & 0xF
    if ks > 1 or (eax >> 5) != 0:
        return '#GP', None
    if (eax & 1) and not (CPUID19_ECX & 1):
        return '#GP', None
    if ks == 1 and not (CPUID19_ECX & 2):
        return '#GP', None
    iw.encryption = src2 + src1  # EncryptionKey[127:0] = SRC2, [255:128] = SRC1
    iw.integrity = xmm0
    iw.nobackup = eax & 1
    iw.keysource = ks
    return None, 0


def encodekey(iw, src, key, bits):
    """ENCODEKEY128/256: returns ('#GP', None) or (None, (dest32, handle bytes))."""
    src &= 0xFFFFFFFF
    reserved = 0xFFFFFFF8 | (~CPUID19_EAX & 7)
    if src & reserved:
        return '#GP', None
    meta = (src & 7) | ((0 if bits == 128 else 1) << 24)
    aad = meta.to_bytes(16, 'little')
    ct, tag = siv_key_wrap_spec(iw.integrity, iw.encryption, aad, key)
    ct2, tag2 = siv_key_wrap_rfc(iw.integrity, iw.encryption, aad, key)
    assert (ct, tag) == (ct2, tag2), 'spec C code and RFC 8452 formulation disagree'
    dest = iw.nobackup | (iw.keysource << 1)
    return None, (dest, aad + tag + ct)


def handle_ok(handle, bits, cpl, enc):
    aad = b2i(handle[:16])
    reserved = (((1 << 128) - 1) & ~((1 << 28) - 1)) | (((1 << 24) - 1) & ~7)
    if aad & reserved:
        return False
    if (aad & 1) and cpl > 0:
        return False
    if enc and (aad & 2):
        return False
    if not enc and (aad & 4):
        return False
    if ((aad >> 24) & 0xF) != (0 if bits == 128 else 1):
        return False
    return True


def aeskl(iw, handle, bits, cpl, enc, blocks):
    """AES{ENC,DEC}{128,256}KL and the WIDE forms. Returns (zf, blocks)."""
    if not handle_ok(handle, bits, cpl, enc):
        return 1, blocks
    n = 48 if bits == 128 else 64
    ok, key = siv_key_unwrap_spec(iw.integrity, iw.encryption, handle[:16], handle[32:n], handle[16:32])
    if not ok:
        return 1, blocks
    f = aes_encrypt if enc else aes_decrypt
    return 0, [f(key, b) for b in blocks]


# ----------------------------------------------------------------------------------------------
# RAO-INT / UINTR / USER_MSR models
# ----------------------------------------------------------------------------------------------
def rao(op, mem, src, size):
    m = (1 << (8 * size)) - 1
    if op == 'aadd':
        return (mem + src) & m
    if op == 'aand':
        return mem & src
    if op == 'aor':
        return mem | src
    return mem ^ src


def user_msr_allowed(bitmap, msr, write):
    if msr >> 14:
        return False
    base = 2048 if write else 0
    return (bitmap[base + (msr >> 3)] >> (msr & 7)) & 1 == 1


# ----------------------------------------------------------------------------------------------
# Self-checks
# ----------------------------------------------------------------------------------------------
def check(name, got, exp):
    ok = got == exp
    print('%-58s %s' % (name, 'OK' if ok else 'MISMATCH got %r exp %r' % (got, exp)))
    return ok


def selftest():
    ok = True
    h = bytes.fromhex
    # FIPS-197 Appendix C.1 / C.3
    pt = h('00112233445566778899aabbccddeeff')
    k128 = bytes(range(16))
    k256 = bytes(range(32))
    ok &= check('FIPS-197 C.1 AES-128 encrypt', aes_encrypt(k128, pt).hex(), '69c4e0d86a7b0430d8cdb78070b4c55a')
    ok &= check('FIPS-197 C.1 AES-128 decrypt', aes_decrypt(k128, h('69c4e0d86a7b0430d8cdb78070b4c55a')), pt)
    ok &= check('FIPS-197 C.3 AES-256 encrypt', aes_encrypt(k256, pt).hex(), '8ea2b7ca516745bfeafc49904b496089')
    ok &= check('FIPS-197 C.3 AES-256 decrypt', aes_decrypt(k256, h('8ea2b7ca516745bfeafc49904b496089')), pt)
    ok &= check('S-box[0x53] = 0xed (FIPS-197 5.1.1)', SBOX[0x53], 0xED)
    # spec AES-256 on-the-fly key schedule == FIPS-197 key expansion
    ct, ks = aes256_ks1_enc1_on_the_fly(b2i(pt), k256)
    ok &= check('KL A.5 AES256_KS1_ENC1 == FIPS-197 key schedule', [i2b(x) for x in ks], key_expansion(k256))
    ok &= check('KL A.5 AES256_KS1_ENC1 == FIPS-197 C.3', i2b(ct).hex(), '8ea2b7ca516745bfeafc49904b496089')
    # Horner_Step == RFC 8452 POLYVAL dot product (random-ish deterministic operands)
    a = b2i(h('66e94bd4ef8a2c3b884cfa59ca342b2e'))
    b = b2i(h('0388dace60b6a392f328c2b971b2fe78'))
    ok &= check('KL A.5 Horner_Step == RFC 8452 dot', horner_step(a, 0, b), polyval_dot(a, b))
    # Key Locker spec footnote (page 45): IWKey all 0, ENCODEKEY128 of key 0 ->
    # tag 0x8720849214a248ad_898940a278c095dc, ciphertext 0xd3e9d22b334fb3c2_3382228c8474c308
    iw = IWKey()
    _, (dest, hnd) = encodekey(iw, 0, bytes(16), 128)
    ok &= check('KL spec zero-IWKey vector: tag', hex(b2i(hnd[16:32])), hex(0x8720849214a248ad898940a278c095dc))
    ok &= check('KL spec zero-IWKey vector: ciphertext', hex(b2i(hnd[32:48])), hex(0xd3e9d22b334fb3c23382228c8474c308))
    # round trip through the handle, 128 and 256, incl. RFC/spec equality inside encodekey()
    iw = IWKey()
    loadiwkey(iw, 0, 0, h('000102030405060708090a0b0c0d0e0f'), h('202122232425262728292a2b2c2d2e2f'),
              h('101112131415161718191a1b1c1d1e1f'))
    _, (dest, h128) = encodekey(iw, 0, k128, 128)
    zf, out = aeskl(iw, h128, 128, 3, True, [pt])
    ok &= check('AESENC128KL(handle(FIPS key)) == FIPS-197 C.1', (zf, out[0].hex()), (0, '69c4e0d86a7b0430d8cdb78070b4c55a'))
    _, (dest, h256) = encodekey(iw, 0, k256, 256)
    zf, out = aeskl(iw, h256, 256, 3, False, [h('8ea2b7ca516745bfeafc49904b496089')])
    ok &= check('AESDEC256KL(handle(FIPS key)) == FIPS-197 C.3', (zf, out[0]), (0, pt))
    bad = bytearray(h256)
    bad[40] ^= 1
    ok &= check('tampered ciphertext -> ZF=1', aeskl(iw, bytes(bad), 256, 0, True, [pt])[0], 1)
    ok &= check('128-bit handle used as 256 -> ZF=1', aeskl(iw, h128 + bytes(16), 256, 0, True, [pt])[0], 1)
    _, (dest, hr) = encodekey(iw, 1, k128, 128)
    ok &= check('CPL0-only handle at CPL3 -> ZF=1', aeskl(iw, hr, 128, 3, True, [pt])[0], 1)
    ok &= check('CPL0-only handle at CPL0 -> ZF=0', aeskl(iw, hr, 128, 0, True, [pt])[0], 0)
    ok &= check('ENCODEKEY128 reserved SRC bit 3 -> #GP', encodekey(iw, 8, k128, 128)[0], '#GP')
    ok &= check('LOADIWKEY KeySource 1 (not enumerated) -> #GP', loadiwkey(iw, 0, 2, bytes(16), bytes(16), bytes(16))[0], '#GP')
    ok &= check('LOADIWKEY at CPL3 -> #GP', loadiwkey(iw, 3, 0, bytes(16), bytes(16), bytes(16))[0], '#GP')
    return ok


# ----------------------------------------------------------------------------------------------
# emu-alltest case file (expected-value cases; see Emulator/tests/alltest/at_cases.hpp)
# ----------------------------------------------------------------------------------------------
MEM = 0x30020000
MEM_PTR = MEM + 0x8000
RFLAGS0 = 0x202
ARITH = 0x8D5  # OF SF ZF AF PF CF


def hx(b):
    return b.hex().upper()


def byt(*seqs):
    out = []
    for s in seqs:
        out += list(s)
    return '.byte ' + ', '.join('0x%02x' % x for x in out)


# encodings (hand-assembled from the SDM opcode tables)
LOADIWKEY_X1_X2 = [0xF3, 0x0F, 0x38, 0xDC, 0xCA]       # modrm 11 001 010
ENCODEKEY128_EAX_ECX = [0xF3, 0x0F, 0x38, 0xFA, 0xC1]  # modrm 11 000 001
ENCODEKEY256_EAX_ECX = [0xF3, 0x0F, 0x38, 0xFB, 0xC1]
MOVDQU_ST = [[0xF3, 0x0F, 0x7F, 0x06], [0xF3, 0x0F, 0x7F, 0x4E, 0x10],
             [0xF3, 0x0F, 0x7F, 0x56, 0x20], [0xF3, 0x0F, 0x7F, 0x5E, 0x30]]  # movdqu [rsi+16i], xmmi
MOVDQA_X0_X3 = [0x66, 0x0F, 0x6F, 0xC3]
AESENC128KL_X7 = [0xF3, 0x0F, 0x38, 0xDC, 0x3E]       # xmm7, [rsi]
AESDEC128KL_X7 = [0xF3, 0x0F, 0x38, 0xDD, 0x3E]
AESENC256KL_X7 = [0xF3, 0x0F, 0x38, 0xDE, 0x3E]
AESDEC256KL_X7 = [0xF3, 0x0F, 0x38, 0xDF, 0x3E]
WIDE = {('enc', 128): 0x06, ('dec', 128): 0x0E, ('enc', 256): 0x16, ('dec', 256): 0x1E}


def write_cases(path):
    L = []
    a = L.append
    h = bytes.fromhex
    a('# Key Locker, RAO-INT, MOVRS/PREFETCHRST2, USER_MSR, UINTR (ledger U100-U104, plan 1.15c).')
    a('# Generated by Emulator/tools/isa/ref_keylocker_misc.py from the SDM / ISE / Key Locker spec')
    a('# pseudocode; do not edit by hand. Expected-value lines ("=>") run on Unicorn only.')
    a('# Hardware lines (no "=>") check that the i5-13600K #UDs every form; run them with')
    a('#   emu-alltest --cases Emulator\\data\\cases_keylocker.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt --strict --xcr0 7')
    a('# (uc #UD == hw #UD). Unicorn runs at CPL0 with CR4.KL = CR4.UINTR = 1 (reset state of a')
    a('# KL/UINTR-capable CPU in this fork, U100/U104) and IWKey = 0 at reset.')
    a('#')
    a('# --- hardware lines: every new encoding #UDs on the i5-13600K (and in --strict on Unicorn)')
    hw = [LOADIWKEY_X1_X2, ENCODEKEY128_EAX_ECX, ENCODEKEY256_EAX_ECX, AESENC128KL_X7, AESDEC128KL_X7,
          AESENC256KL_X7, AESDEC256KL_X7] + [[0xF3, 0x0F, 0x38, 0xD8, m] for m in WIDE.values()] + [
        [0x0F, 0x38, 0xFC, 0x06], [0x66, 0x0F, 0x38, 0xFC, 0x06], [0xF2, 0x0F, 0x38, 0xFC, 0x06],
        [0xF3, 0x0F, 0x38, 0xFC, 0x06], [0x48, 0x0F, 0x38, 0xFC, 0x06],
        [0x0F, 0x38, 0x8B, 0x06], [0x48, 0x0F, 0x38, 0x8B, 0x06], [0x0F, 0x38, 0x8A, 0x06],
        [0xF2, 0x0F, 0x38, 0xF8, 0xCB], [0xF3, 0x0F, 0x38, 0xF8, 0xCB],
        [0xC4, 0xE7, 0x7B, 0xF8, 0xC2, 0x01, 0x1B, 0x00, 0x00], [0xC4, 0xE7, 0x7A, 0xF8, 0xC3, 0x01, 0x1B, 0x00, 0x00],
        [0xF3, 0x0F, 0x01, 0xEC], [0xF3, 0x0F, 0x01, 0xED], [0xF3, 0x0F, 0x01, 0xEE], [0xF3, 0x0F, 0x01, 0xEF],
        [0xF3, 0x0F, 0xC7, 0xF0]]
    for e in hw:
        a(byt(e))
    a('#')
    a('# --- Key Locker: ENCODEKEY128 with the reset IWKey (all 0) and key 0 = the Key Locker spec')
    a('# footnote vector (tag 8720849214a248ad898940a278c095dc, ciphertext d3e9d22b334fb3c23382228c8474c308)')
    iw = IWKey()
    _, (dest, hnd) = encodekey(iw, 0, bytes(16), 128)
    a('%s => xmm1=%s xmm2=%s' % (byt(ENCODEKEY128_EAX_ECX), hx(hnd[16:32]), hx(hnd[32:48])))

    # common IWKey
    ik = h('000102030405060708090a0b0c0d0e0f')
    ek_hi = h('202122232425262728292a2b2c2d2e2f')  # xmm1 = SRC1 -> EncryptionKey[255:128]
    ek_lo = h('101112131415161718191a1b1c1d1e1f')  # xmm2 = SRC2 -> EncryptionKey[127:0]
    k128 = bytes(range(16))
    k256 = bytes(range(32))
    pt = h('00112233445566778899aabbccddeeff')
    inp_iw = 'xmm0=%s xmm1=%s xmm2=%s' % (hx(ik), hx(ek_hi), hx(ek_lo))

    a('#')
    a('# --- LOADIWKEY xmm1, xmm2 (EAX = 0), then ENCODEKEY128 eax, ecx of the FIPS-197 C.1 key (xmm3)')
    for restr in (0, 1, 2, 4, 7):
        iw = IWKey()
        loadiwkey(iw, 0, 0, ik, ek_hi, ek_lo)
        _, (dest, hnd) = encodekey(iw, restr, k128, 128)
        a('%s | %s xmm3=%s rcx=%d => xmm0=%s xmm1=%s xmm2=%s rax=%d rflags=0x%x' % (
            byt(LOADIWKEY_X1_X2, MOVDQA_X0_X3, ENCODEKEY128_EAX_ECX), inp_iw, hx(k128), restr,
            hx(hnd[:16]), hx(hnd[16:32]), hx(hnd[32:48]), dest, RFLAGS0 & ~ARITH))
    a('# NoBackup (EAX[0] = 1): ENCODEKEY reports it in DEST[0]')
    iw = IWKey()
    loadiwkey(iw, 0, 1, ik, ek_hi, ek_lo)
    _, (dest, hnd) = encodekey(iw, 0, k128, 128)
    a('%s | %s xmm3=%s rax=1 => xmm0=%s xmm1=%s xmm2=%s rax=%d' % (
        byt(LOADIWKEY_X1_X2, MOVDQA_X0_X3, ENCODEKEY128_EAX_ECX), inp_iw, hx(k128),
        hx(hnd[:16]), hx(hnd[16:32]), hx(hnd[32:48]), dest))
    a('# ENCODEKEY256: XMM1:XMM0 = key, handle -> XMM0-3, XMM4-6 zeroed')
    iw = IWKey()
    _, (dest, hnd) = encodekey(iw, 0, k256, 256)
    a('%s | xmm0=%s xmm1=%s xmm4=FF xmm5=FF xmm6=FF => xmm0=%s xmm1=%s xmm2=%s xmm3=%s xmm4=00 xmm5=00 xmm6=00' % (
        byt(ENCODEKEY256_EAX_ECX), hx(k256[:16]), hx(k256[16:]),
        hx(hnd[:16]), hx(hnd[16:32]), hx(hnd[32:48]), hx(hnd[48:64])))
    a('# reserved restriction bits -> #GP (SRC[31:3]); upper half of a 64-bit source is ignored')
    a('%s | rcx=8 => #GP' % byt(ENCODEKEY128_EAX_ECX))
    a('%s | rcx=0x80000000 => #GP' % byt(ENCODEKEY256_EAX_ECX))
    iw = IWKey()
    _, (dest, hnd) = encodekey(iw, 0, bytes(16), 128)
    a('%s | rcx=0x100000000 => xmm1=%s xmm2=%s' % (byt(ENCODEKEY128_EAX_ECX), hx(hnd[16:32]), hx(hnd[32:48])))
    a('# LOADIWKEY: KeySource 1 not enumerated, reserved EAX bits -> #GP; ENCODEKEY memory form -> #UD')
    a('%s | rax=2 => #GP' % byt(LOADIWKEY_X1_X2))
    a('%s | rax=0x20 => #GP' % byt(LOADIWKEY_X1_X2))
    a('%s | rax=4 => #GP' % byt(LOADIWKEY_X1_X2))
    a('%s => #UD' % byt([0xF3, 0x0F, 0x38, 0xFA, 0x06]))
    a('%s => #UD' % byt([0xF0, 0xF3, 0x0F, 0x38, 0xFA, 0xC1]))
    a('%s => #UD' % byt([0xF3, 0x0F, 0x38, 0xDD, 0xC1]))
    a('%s => #UD' % byt([0xF3, 0x0F, 0x38, 0xD8, 0x26]))

    a('#')
    a('# --- AES*KL: LOADIWKEY, ENCODEKEY of the FIPS-197 key, store the handle to [rsi], encrypt/decrypt')
    for bits, enc, insn in ((128, True, AESENC128KL_X7), (128, False, AESDEC128KL_X7),
                            (256, True, AESENC256KL_X7), (256, False, AESDEC256KL_X7)):
        for restr in (0, 1, 2, 4):
            iw = IWKey()
            loadiwkey(iw, 0, 0, ik, ek_hi, ek_lo)
            key = k128 if bits == 128 else k256
            enck = ENCODEKEY128_EAX_ECX if bits == 128 else ENCODEKEY256_EAX_ECX
            _, (dest, hnd) = encodekey(iw, restr, key, bits)
            blk = pt if enc else aes_encrypt(key, pt)
            zf, out = aeskl(iw, hnd, bits, 0, enc, [blk])
            stores = MOVDQU_ST[:3] if bits == 128 else MOVDQU_ST[:4]
            if bits == 128:
                pre = byt(LOADIWKEY_X1_X2, MOVDQA_X0_X3, enck, *stores, insn)
                inp = '%s xmm3=%s xmm7=%s rcx=%d' % (inp_iw, hx(k128), hx(blk), restr)
            else:
                # 256: xmm1:xmm0 = key; load key halves from xmm3/xmm4 after LOADIWKEY
                pre = byt(LOADIWKEY_X1_X2, MOVDQA_X0_X3, [0x66, 0x0F, 0x6F, 0xCC], enck, *stores, insn)
                inp = '%s xmm3=%s xmm4=%s xmm7=%s rcx=%d' % (inp_iw, hx(k256[:16]), hx(k256[16:]), hx(blk), restr)
            exp = 'xmm0=%s xmm1=%s xmm2=%s ' % (hx(hnd[:16]), hx(hnd[16:32]), hx(hnd[32:48]))
            if bits == 256:
                exp += 'xmm3=%s xmm4=00 ' % hx(hnd[48:64])
            exp += 'm+0x8000=%s xmm7=%s rax=0 rflags=0x%x' % (hx(hnd), hx(out[0]), (RFLAGS0 & ~ARITH) | (zf << 6))
            a('%s | %s => %s' % (pre, inp, exp))
    a('# handle checks (LOADIWKEY first: with IWKey = 0 the POLYVAL key is 0 and a changed ciphertext')
    a('# is not detected, Key Locker spec page 45): tampered handle / wrong key type / reserved AAD bits')
    iw = IWKey()
    loadiwkey(iw, 0, 0, ik, ek_hi, ek_lo)
    _, (dest, hnd) = encodekey(iw, 0, k128, 128)
    for desc, mod in (('valid', None), ('ciphertext bit flipped', (40, 0x10)), ('tag bit flipped', (20, 0x01)),
                      ('AAD key type 1', (3, 0x01)), ('AAD reserved bit 3', (0, 0x08)),
                      ('AAD reserved bit 127', (15, 0x80))):
        hh = bytearray(hnd)
        if mod:
            hh[mod[0]] ^= mod[1]
        zf, out = aeskl(iw, bytes(hh), 128, 0, True, [pt])
        a('# %s' % desc)
        a('%s | %s m+0x8000=%s xmm7=%s rflags=0x8d7 => xmm7=%s rflags=0x%x' % (
            byt(LOADIWKEY_X1_X2, AESENC128KL_X7), inp_iw, hx(bytes(hh)), hx(pt), hx(out[0]),
            (0x8d7 & ~ARITH) | (zf << 6)))
    a('# wide forms: XMM0-7 = 8 blocks (handle at [rsi] from the reset IWKey)')
    for (mode, bits), modrm in WIDE.items():
        iw = IWKey()
        key = k128 if bits == 128 else k256
        _, (dest, hnd) = encodekey(iw, 0, key, bits)
        blocks = [bytes((i * 16 + j) & 0xFF for j in range(16)) for i in range(8)]
        zf, out = aeskl(iw, hnd, bits, 0, mode == 'enc', blocks)
        inp = ' '.join('xmm%d=%s' % (i, hx(b)) for i, b in enumerate(blocks))
        exp = ' '.join('xmm%d=%s' % (i, hx(b)) for i, b in enumerate(out))
        a('%s | m+0x8000=%s %s => %s rflags=0x%x' % (byt([0xF3, 0x0F, 0x38, 0xD8, modrm]), hx(hnd), inp, exp,
                                                     (RFLAGS0 & ~ARITH) | (zf << 6)))
    a('# AESENCWIDE128KL with a no-encrypt handle: ZF = 1, XMM0-7 unchanged')
    iw = IWKey()
    _, (dest, hnd) = encodekey(iw, 2, k128, 128)
    a('%s | m+0x8000=%s xmm0=11 => rflags=0x%x' % (byt([0xF3, 0x0F, 0x38, 0xD8, 0x06]), hx(hnd), RFLAGS0 | 0x40))

    a('#')
    a('# --- RAO-INT: AADD/AAND/AOR/AXOR m32/m64, r (no flags), naturally aligned or #GP')
    ops = (('aadd', []), ('aand', [0x66]), ('aor', [0xF2]), ('axor', [0xF3]))
    m0 = 0xF0F0F0F0FFFFFFFF
    src = 0x0FF00FF000000001
    for op, pfx in ops:
        for w in (0, 1):
            size = 8 if w else 4
            mm = m0 & ((1 << (8 * size)) - 1)
            r = rao(op, mm, src & ((1 << (8 * size)) - 1), size)
            enc = pfx + ([0x48] if w else []) + [0x0F, 0x38, 0xFC, 0x06]
            a('%s | m+0x8000=%s rax=0x%x rflags=0x8d7 => m+0x8000=%s' % (
                byt(enc), hx(mm.to_bytes(size, 'little')), src, hx(r.to_bytes(size, 'little'))))
        a('%s | rax=1 => #GP' % byt(pfx + [0x0F, 0x38, 0xFC, 0x46, 0x02]))
        a('%s => #UD' % byt(pfx + [0x0F, 0x38, 0xFC, 0xC0]))
        a('%s => #UD' % byt([0xF0] + pfx + [0x0F, 0x38, 0xFC, 0x06]))
    a('# 64-bit operand misaligned on 8 (aligned on 4) -> #GP')
    a('%s => #GP' % byt([0x48, 0x0F, 0x38, 0xFC, 0x46, 0x04]))
    a('# AADD r9 -> [rsi], wraps')
    a('%s | m+0x8000=FFFFFFFFFFFFFFFF r9=2 => m+0x8000=0100000000000000' % byt([0x4C, 0x0F, 0x38, 0xFC, 0x0E]))

    a('#')
    a('# --- MOVRS r, m (64-bit mode): a plain load; F2/F3/LOCK/register form #UD')
    a('%s | m+0x8000=8877665544332211 => rax=0x44332211' % byt([0x0F, 0x38, 0x8B, 0x06]))
    a('%s | m+0x8000=8877665544332211 rax=-1 => rax=0x8877665544332211' % byt([0x48, 0x0F, 0x38, 0x8B, 0x06]))
    a('%s | m+0x8000=8877665544332211 rax=-1 => rax=0xFFFFFFFFFFFF2211' % byt([0x66, 0x0F, 0x38, 0x8B, 0x06]))
    a('%s | m+0x8000=8877665544332211 rax=-1 => rax=0xFFFFFFFFFFFFFF11' % byt([0x0F, 0x38, 0x8A, 0x06]))
    a('%s | m+0x8000=8877665544332211 rax=-1 => rax=0xFFFFFFFFFFFF11FF' % byt([0x0F, 0x38, 0x8A, 0x26]))
    a('%s | m+0x8000=8877665544332211 rsp=0x30013000 => rsp=0x30013011' % byt([0x40, 0x0F, 0x38, 0x8A, 0x26]))
    a('%s | m+0x8000=8877665544332211 => r15=0x8877665544332211' % byt([0x4C, 0x0F, 0x38, 0x8B, 0x3E]))
    for bad in ([0xF3, 0x0F, 0x38, 0x8B, 0x06], [0xF2, 0x0F, 0x38, 0x8A, 0x06], [0xF0, 0x0F, 0x38, 0x8B, 0x06],
                [0x0F, 0x38, 0x8B, 0xC0]):
        a('%s => #UD' % byt(bad))
    a('# PREFETCHRST2 m8 (0F 18 /4): a hint, nothing changes; LOCK #UD')
    a('%s =>' % byt([0x0F, 0x18, 0x26]))
    a('%s => #UD' % byt([0xF0, 0x0F, 0x18, 0x26]))

    a('#')
    a('# --- USER_MSR: IA32_USER_MSR_CTL (1CH) = bitmap MEM+0x8000 | ENABLE via WRMSR, then URDMSR/UWRMSR')
    a('# URDMSR/UWRMSR #UD while IA32_USER_MSR_CTL.ENABLE = 0 (reset)')
    a('%s | rcx=0x1c => #UD' % byt([0xF2, 0x0F, 0x38, 0xF8, 0xCB]))
    a('%s | rcx=0x1b01 => #UD' % byt([0xF3, 0x0F, 0x38, 0xF8, 0xCB]))
    wr = [0x0F, 0x30]
    urd_rbx_rcx = [0xF2, 0x0F, 0x38, 0xF8, 0xCB]   # URDMSR rbx, rcx: rrr = rcx (address), bbb = rbx
    a('# URDMSR rbx, rcx reads IA32_USER_MSR_CTL itself (bitmap bit 1CH set)')
    a('%s | rax=0x%x rcx=0x1c m+0x8003=10 => rbx=0x%x' % (byt(wr, urd_rbx_rcx), MEM_PTR | 1, MEM_PTR | 1))
    a('# bitmap bit clear -> #GP; address >= 4000H -> #GP')
    a('%s | rax=0x%x rcx=0x1c => #GP' % (byt(wr, urd_rbx_rcx), MEM_PTR | 1))
    a('%s | rax=0x%x rcx=0x1c rdx=0 r8=0x4000 m+0x8003=10 => #GP' % (byt(wr, [0xF2, 0x44, 0x0F, 0x38, 0xF8, 0xC3]),
                                                                      MEM_PTR | 1))
    a('# UWRMSR 1B01H (IA32_UARCH_MISC_CTL, DOITM) via the VEX imm32 forms, then URDMSR back')
    uwr_imm_rbx = [0xC4, 0xE7, 0x7A, 0xF8, 0xC3, 0x01, 0x1B, 0x00, 0x00]
    urd_imm_rdx = [0xC4, 0xE7, 0x7B, 0xF8, 0xC2, 0x01, 0x1B, 0x00, 0x00]
    a('%s | rax=0x%x rcx=0x1c rbx=1 m+0x8360=02 m+0x8B60=02 => rdx=1' % (byt(wr, uwr_imm_rbx, urd_imm_rdx), MEM_PTR | 1))
    a('# UWRMSR: an MSR other than 1B01H -> #GP even when its bitmap bit is set; reserved DOITM bits -> #GP')
    a('%s | rax=0x%x rcx=0x1c m+0x8803=10 => #GP' % (byt(wr, [0xF3, 0x0F, 0x38, 0xF8, 0xCB]), MEM_PTR | 1))
    a('%s | rax=0x%x rcx=0x1c rbx=2 m+0x8B60=02 => #GP' % (byt(wr, uwr_imm_rbx), MEM_PTR | 1))
    a('# memory forms / VEX.L1 / VEX.W1 / reg != 0 / LOCK -> #UD')
    for bad in ([0xF2, 0x0F, 0x38, 0xF8, 0x0B], [0xC4, 0xE7, 0x7F, 0xF8, 0xC2, 0x01, 0x1B, 0x00, 0x00],
                [0xC4, 0xE7, 0xFB, 0xF8, 0xC2, 0x01, 0x1B, 0x00, 0x00], [0xC4, 0xE7, 0x7B, 0xF8, 0xCA, 0x01, 0x1B, 0x00, 0x00],
                [0xF0, 0xF2, 0x0F, 0x38, 0xF8, 0xCB]):
        a('%s | rax=0x%x rcx=0x1c => #UD' % (byt(wr, bad), MEM_PTR | 1))

    a('#')
    a('# --- UINTR (CR4.UINTR = 1, CPL0): UIF starts 0; TESTUI -> CF, ZF/AF/OF/PF/SF cleared')
    testui, stui, clui = [0xF3, 0x0F, 0x01, 0xED], [0xF3, 0x0F, 0x01, 0xEF], [0xF3, 0x0F, 0x01, 0xEE]
    a('%s | rflags=0x8d7 => rflags=0x202' % byt(testui))
    a('%s | rflags=0x8d6 => rflags=0x203' % byt(stui, testui))
    a('%s | rflags=0x202 => rflags=0x202' % byt(stui, clui, testui))
    a('%s => #UD' % byt([0xF0] + testui))
    a('# SENDUIPI rax: UITT at MEM+0x8000 (UITTSZ 0), UITTE V=1 UV=5 -> UPID at MEM+0x8040')
    a('# UPID NV=0xEC NDST=0 (own xAPIC ID): PIR[5] := 1, ON := 1 (notification: NV != UINV, no local APIC -> dropped)')
    mov_ecx = lambda v: [0xB9] + list(v.to_bytes(4, 'little'))
    mov_eax = lambda v: [0xB8] + list(v.to_bytes(4, 'little'))
    mov_edx = lambda v: [0xBA] + list(v.to_bytes(4, 'little'))
    tt = MEM_PTR | 1
    setup = mov_ecx(0x98A) + mov_eax(tt & 0xFFFFFFFF) + mov_edx(tt >> 32) + wr + \
        mov_ecx(0x988) + mov_eax(0) + mov_edx(0x40) + wr + [0x31, 0xC0]   # UINV = 0x40, xor eax, eax
    uitte = (1 | (5 << 8)).to_bytes(8, 'little') + (MEM_PTR + 0x40).to_bytes(8, 'little')
    upid = (0xEC << 16).to_bytes(8, 'little') + bytes(8)
    upid_after = (1 | (0xEC << 16)).to_bytes(8, 'little') + (1 << 5).to_bytes(8, 'little')
    senduipi = [0xF3, 0x0F, 0xC7, 0xF0]
    a('%s | m+0x8000=%s m+0x8040=%s => m+0x8040=%s rax=0 rcx=0x988 rdx=0x40' % (
        byt(setup, senduipi), hx(uitte), hx(upid), hx(upid_after)))
    a('# NV = UINV (0x40), self, IF = 1: user-interrupt notification processing: ON := 0, PIR -> UIRR')
    a('# (RDMSR 985H afterwards reads UIRR = 1 << 5; CPL0 so no delivery)')
    upid = (0x40 << 16).to_bytes(8, 'little') + bytes(8)
    upid_after = (0x40 << 16).to_bytes(8, 'little') + bytes(8)
    a('%s | m+0x8000=%s m+0x8040=%s => m+0x8040=%s rax=0x20 rcx=0x985 rdx=0' % (
        byt(setup, senduipi, mov_ecx(0x985), [0x0F, 0x32]), hx(uitte), hx(upid), hx(upid_after)))
    a('# index > UITTSZ / invalid UITTE / reserved UPID bits -> #GP; SENDUIPI with IA32_UINTR_TT[0] = 0 -> #UD')
    a('%s | m+0x8000=%s m+0x8040=%s rax=1 => #GP' % (byt(setup[:-2], senduipi), hx(uitte), hx(upid)))
    a('%s | m+0x8000=%s m+0x8040=%s => #GP' % (byt(setup, senduipi), hx(bytes(16)), hx(upid)))
    a('%s | m+0x8000=%s m+0x8040=%s => #GP' % (byt(setup, senduipi), hx(uitte), hx((0x40 << 16 | 4).to_bytes(8, 'little'))))
    a('%s => #UD' % byt(senduipi))
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L) + '\n')
    print('wrote %d lines to %s' % (len(L), path))


if __name__ == '__main__':
    ok = selftest()
    if len(sys.argv) == 3 and sys.argv[1] == '--cases':
        write_cases(sys.argv[2])
    sys.exit(0 if ok else 1)
