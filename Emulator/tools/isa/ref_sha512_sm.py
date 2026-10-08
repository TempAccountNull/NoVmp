"""Independent reference for the VEX SHA512 / SM3 / SM4 instructions (plan 1.15c, ledger U82-U84).

Every instruction below is a literal transcription of the Operation section of Intel SDM Vol. 2C
(325383-092): VSHA512MSG1 5-744, VSHA512MSG2 5-746, VSHA512RNDS2 5-748, VSM3MSG1 5-755,
VSM3MSG2 5-757, VSM3RNDS2 5-759, VSM4KEY4 5-761, VSM4RNDS4 5-763 (identical in the ISE reference
319433-062). The i5-13600K has none of them (CPUID.(EAX=7,ECX=1):EAX[2:0] = 0), so the reference is
proven against the published standard test vectors instead of hardware:

  * SHA-512("abc")  FIPS 180-4 / NIST example   (whole hash built only from VSHA512MSG1/MSG2/RNDS2)
  * SM3("abc")      GB/T 32905-2016 example A.1 (whole hash built only from VSM3MSG1/MSG2/RNDS2)
  * SM4 example     GB/T 32907-2016 example A.1 (key schedule from VSM4KEY4, rounds from VSM4RNDS4)

The SHA-512 round constants and initial value are computed here (first 64 fraction bits of the cube
/ square roots of the first primes, FIPS 180-4 4.2.3 / 5.3.5); the SM4 CK constants are computed
(ck[i,j] = (4i+j)*7 mod 256, GB/T 32907 7.3.2). FK, the SM3 IV and the SM4 S-box are the standards'
algorithm constants (the S-box is the table printed in the SDM VSM4KEY4 page).

Registers are Python ints holding the little-endian register value (bit 0 = bit 0 of the XMM/YMM).

usage:
  python ref_sha512_sm.py               self-test against the standard vectors (exit 1 on mismatch)
  python ref_sha512_sm.py --vectors ISA          C table for unicorn/tests/unit/test_x86.c (ISA = sha512|sm3|sm4)
  python ref_sha512_sm.py --cases sha512,sm3,sm4  lines for Emulator/data/cases_sha_sm.txt
"""
import hashlib
import sys

M32 = 0xFFFFFFFF
M64 = 0xFFFFFFFFFFFFFFFF


# ---------------------------------------------------------------- lane access helpers
def dw(v, i):
    return (v >> (32 * i)) & M32


def qw(v, i):
    return (v >> (64 * i)) & M64


def from_dw(ds):
    r = 0
    for i, d in enumerate(ds):
        r |= (d & M32) << (32 * i)
    return r


def from_qw(qs):
    r = 0
    for i, q in enumerate(qs):
        r |= (q & M64) << (64 * i)
    return r


def xmm_lane(v, i):
    return (v >> (128 * i)) & ((1 << 128) - 1)


# ---------------------------------------------------------------- SHA512 (SDM 5-744 .. 5-749)
def ROR64(q, n):
    count = n % 64
    return ((q >> count) | (q << (64 - count))) & M64


def SHR64(q, n):
    return q >> n


def s0(q):
    return ROR64(q, 1) ^ ROR64(q, 8) ^ SHR64(q, 7)


def s1(q):
    return ROR64(q, 19) ^ ROR64(q, 61) ^ SHR64(q, 6)


def cap_sigma0(q):
    return ROR64(q, 28) ^ ROR64(q, 34) ^ ROR64(q, 39)


def cap_sigma1(q):
    return ROR64(q, 14) ^ ROR64(q, 18) ^ ROR64(q, 41)


def MAJ(a, b, c):
    return (a & b) ^ (a & c) ^ (b & c)


def CH(e, f, g):
    return (e & f) ^ (g & ~e & M64)


def vsha512msg1(srcdest, src1):
    """VSHA512MSG1 ymm1, xmm2 (VEX.256.F2.0F38.W0 CC 11:rrr:bbb)."""
    W = {}
    W[4] = qw(src1, 0)
    W[3] = qw(srcdest, 3)
    W[2] = qw(srcdest, 2)
    W[1] = qw(srcdest, 1)
    W[0] = qw(srcdest, 0)
    return from_qw([(W[0] + s0(W[1])) & M64, (W[1] + s0(W[2])) & M64,
                    (W[2] + s0(W[3])) & M64, (W[3] + s0(W[4])) & M64])


def vsha512msg2(srcdest, src1):
    """VSHA512MSG2 ymm1, ymm2 (VEX.256.F2.0F38.W0 CD 11:rrr:bbb)."""
    W = {}
    W[14] = qw(src1, 2)
    W[15] = qw(src1, 3)
    W[16] = (qw(srcdest, 0) + s1(W[14])) & M64
    W[17] = (qw(srcdest, 1) + s1(W[15])) & M64
    W[18] = (qw(srcdest, 2) + s1(W[16])) & M64
    W[19] = (qw(srcdest, 3) + s1(W[17])) & M64
    return from_qw([W[16], W[17], W[18], W[19]])


def vsha512rnds2(srcdest, src1, src2):
    """VSHA512RNDS2 ymm1, ymm2, xmm3 (VEX.256.F2.0F38.W0 CB 11:rrr:bbb)."""
    A = [qw(src1, 3), 0, 0]
    B = [qw(src1, 2), 0, 0]
    C = [qw(srcdest, 3), 0, 0]
    D = [qw(srcdest, 2), 0, 0]
    E = [qw(src1, 1), 0, 0]
    F = [qw(src1, 0), 0, 0]
    G = [qw(srcdest, 1), 0, 0]
    H = [qw(srcdest, 0), 0, 0]
    WK = [qw(src2, 0), qw(src2, 1)]
    for i in range(2):
        A[i + 1] = (CH(E[i], F[i], G[i]) + cap_sigma1(E[i]) + WK[i] + H[i] +
                    MAJ(A[i], B[i], C[i]) + cap_sigma0(A[i])) & M64
        B[i + 1] = A[i]
        C[i + 1] = B[i]
        D[i + 1] = C[i]
        E[i + 1] = (CH(E[i], F[i], G[i]) + cap_sigma1(E[i]) + WK[i] + H[i] + D[i]) & M64
        F[i + 1] = E[i]
        G[i + 1] = F[i]
        H[i + 1] = G[i]
    return from_qw([F[2], E[2], B[2], A[2]])


# ---------------------------------------------------------------- SM3 (SDM 5-755 .. 5-760)
def ROL32(d, n):
    count = n % 32
    return ((d << count) | (d >> (32 - count))) & M32


def P1(x):
    return x ^ ROL32(x, 15) ^ ROL32(x, 23)


def P0(x):
    return x ^ ROL32(x, 9) ^ ROL32(x, 17)


def FF(x, y, z, rnd):
    if rnd < 16:
        return x ^ y ^ z
    return (x & y) | (x & z) | (y & z)


def GG(x, y, z, rnd):
    if rnd < 16:
        return x ^ y ^ z
    return (x & y) | (~x & M32 & z)


def vsm3msg1(srcdest, src1, src2):
    """VSM3MSG1 xmm1, xmm2, xmm3/m128 (VEX.128.NP.0F38.W0 DA /r)."""
    W = {}
    W[0], W[1], W[2], W[3] = (dw(src2, i) for i in range(4))
    W[7], W[8], W[9], W[10] = (dw(srcdest, i) for i in range(4))
    W[13], W[14], W[15] = (dw(src1, i) for i in range(3))
    TMP0 = W[7] ^ W[0] ^ ROL32(W[13], 15)
    TMP1 = W[8] ^ W[1] ^ ROL32(W[14], 15)
    TMP2 = W[9] ^ W[2] ^ ROL32(W[15], 15)
    TMP3 = W[10] ^ W[3]
    return from_dw([P1(TMP0), P1(TMP1), P1(TMP2), P1(TMP3)])


def vsm3msg2(srcdest, src1, src2):
    """VSM3MSG2 xmm1, xmm2, xmm3/m128 (VEX.128.66.0F38.W0 DA /r)."""
    WTMP = [dw(srcdest, i) for i in range(4)]
    W = {}
    W[3], W[4], W[5], W[6] = (dw(src1, i) for i in range(4))
    W[10], W[11], W[12], W[13] = (dw(src2, i) for i in range(4))
    W[16] = ROL32(W[3], 7) ^ W[10] ^ WTMP[0]
    W[17] = ROL32(W[4], 7) ^ W[11] ^ WTMP[1]
    W[18] = ROL32(W[5], 7) ^ W[12] ^ WTMP[2]
    W[19] = ROL32(W[6], 7) ^ W[13] ^ WTMP[3]
    W[19] = W[19] ^ ROL32(W[16], 6) ^ ROL32(W[16], 15) ^ ROL32(W[16], 30)
    return from_dw([W[16], W[17], W[18], W[19]])


def vsm3rnds2(srcdest, src1, src2, imm8):
    """VSM3RNDS2 xmm1, xmm2, xmm3/m128, imm8 (VEX.128.66.0F3A.W0 DE /r ib)."""
    A = [dw(src1, 3), 0, 0]
    B = [dw(src1, 2), 0, 0]
    C = [dw(srcdest, 3), 0, 0]
    D = [dw(srcdest, 2), 0, 0]
    E = [dw(src1, 1), 0, 0]
    F = [dw(src1, 0), 0, 0]
    G = [dw(srcdest, 1), 0, 0]
    H = [dw(srcdest, 0), 0, 0]
    W = {0: dw(src2, 0), 1: dw(src2, 1), 4: dw(src2, 2), 5: dw(src2, 3)}
    C[0] = ROL32(C[0], 9)
    D[0] = ROL32(D[0], 9)
    G[0] = ROL32(G[0], 19)
    H[0] = ROL32(H[0], 19)
    ROUND = imm8 & 0x3E
    CONST = 0x79CC4519 if ROUND < 16 else 0x7A879D8A
    CONST = ROL32(CONST, ROUND)
    for i in range(2):
        S1 = ROL32((ROL32(A[i], 12) + E[i] + CONST) & M32, 7)
        S2 = S1 ^ ROL32(A[i], 12)
        T1 = (FF(A[i], B[i], C[i], ROUND) + D[i] + S2 + (W[i] ^ W[i + 4])) & M32
        T2 = (GG(E[i], F[i], G[i], ROUND) + H[i] + S1 + W[i]) & M32
        D[i + 1] = C[i]
        C[i + 1] = ROL32(B[i], 9)
        B[i + 1] = A[i]
        A[i + 1] = T1
        H[i + 1] = G[i]
        G[i + 1] = ROL32(F[i], 19)
        F[i + 1] = E[i]
        E[i + 1] = P0(T2)
        CONST = ROL32(CONST, 1)
    return from_dw([F[2], E[2], B[2], A[2]])


# ---------------------------------------------------------------- SM4 (SDM 5-761 .. 5-764)
SBOX = [
    0xD6, 0x90, 0xE9, 0xFE, 0xCC, 0xE1, 0x3D, 0xB7, 0x16, 0xB6, 0x14, 0xC2, 0x28, 0xFB, 0x2C, 0x05,
    0x2B, 0x67, 0x9A, 0x76, 0x2A, 0xBE, 0x04, 0xC3, 0xAA, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
    0x9C, 0x42, 0x50, 0xF4, 0x91, 0xEF, 0x98, 0x7A, 0x33, 0x54, 0x0B, 0x43, 0xED, 0xCF, 0xAC, 0x62,
    0xE4, 0xB3, 0x1C, 0xA9, 0xC9, 0x08, 0xE8, 0x95, 0x80, 0xDF, 0x94, 0xFA, 0x75, 0x8F, 0x3F, 0xA6,
    0x47, 0x07, 0xA7, 0xFC, 0xF3, 0x73, 0x17, 0xBA, 0x83, 0x59, 0x3C, 0x19, 0xE6, 0x85, 0x4F, 0xA8,
    0x68, 0x6B, 0x81, 0xB2, 0x71, 0x64, 0xDA, 0x8B, 0xF8, 0xEB, 0x0F, 0x4B, 0x70, 0x56, 0x9D, 0x35,
    0x1E, 0x24, 0x0E, 0x5E, 0x63, 0x58, 0xD1, 0xA2, 0x25, 0x22, 0x7C, 0x3B, 0x01, 0x21, 0x78, 0x87,
    0xD4, 0x00, 0x46, 0x57, 0x9F, 0xD3, 0x27, 0x52, 0x4C, 0x36, 0x02, 0xE7, 0xA0, 0xC4, 0xC8, 0x9E,
    0xEA, 0xBF, 0x8A, 0xD2, 0x40, 0xC7, 0x38, 0xB5, 0xA3, 0xF7, 0xF2, 0xCE, 0xF9, 0x61, 0x15, 0xA1,
    0xE0, 0xAE, 0x5D, 0xA4, 0x9B, 0x34, 0x1A, 0x55, 0xAD, 0x93, 0x32, 0x30, 0xF5, 0x8C, 0xB1, 0xE3,
    0x1D, 0xF6, 0xE2, 0x2E, 0x82, 0x66, 0xCA, 0x60, 0xC0, 0x29, 0x23, 0xAB, 0x0D, 0x53, 0x4E, 0x6F,
    0xD5, 0xDB, 0x37, 0x45, 0xDE, 0xFD, 0x8E, 0x2F, 0x03, 0xFF, 0x6A, 0x72, 0x6D, 0x6C, 0x5B, 0x51,
    0x8D, 0x1B, 0xAF, 0x92, 0xBB, 0xDD, 0xBC, 0x7F, 0x11, 0xD9, 0x5C, 0x41, 0x1F, 0x10, 0x5A, 0xD8,
    0x0A, 0xC1, 0x31, 0x88, 0xA5, 0xCD, 0x7B, 0xBD, 0x2D, 0x74, 0xD0, 0x12, 0xB8, 0xE5, 0xB4, 0xB0,
    0x89, 0x69, 0x97, 0x4A, 0x0C, 0x96, 0x77, 0x7E, 0x65, 0xB9, 0xF1, 0x09, 0xC5, 0x6E, 0xC6, 0x84,
    0x18, 0xF0, 0x7D, 0xEC, 0x3A, 0xDC, 0x4D, 0x20, 0x79, 0xEE, 0x5F, 0x3E, 0xD7, 0xCB, 0x39, 0x48,
]


def SBOX_BYTE(d, i):
    return SBOX[(d >> (8 * i)) & 0xFF]


def lower_t(d):
    return SBOX_BYTE(d, 0) | SBOX_BYTE(d, 1) << 8 | SBOX_BYTE(d, 2) << 16 | SBOX_BYTE(d, 3) << 24


def L_KEY(d):
    return d ^ ROL32(d, 13) ^ ROL32(d, 23)


def T_KEY(d):
    return L_KEY(lower_t(d))


def F_KEY(X0, X1, X2, X3, round_key):
    return X0 ^ T_KEY(X1 ^ X2 ^ X3 ^ round_key)


def L_RND(d):
    tmp = d
    tmp = tmp ^ ROL32(d, 2)
    tmp = tmp ^ ROL32(d, 10)
    tmp = tmp ^ ROL32(d, 18)
    tmp = tmp ^ ROL32(d, 24)
    return tmp


def T_RND(d):
    return L_RND(lower_t(d))


def F_RND(X0, X1, X2, X3, round_key):
    return X0 ^ T_RND(X1 ^ X2 ^ X3 ^ round_key)


def _sm4_4rounds(src1, src2, VL, F):
    KL = VL // 128
    out = 0
    for i in range(KL):
        a, b = xmm_lane(src1, i), xmm_lane(src2, i)
        P = [dw(a, k) for k in range(4)]
        C0 = F(P[0], P[1], P[2], P[3], dw(b, 0))
        C1 = F(P[1], P[2], P[3], C0, dw(b, 1))
        C2 = F(P[2], P[3], C0, C1, dw(b, 2))
        C3 = F(P[3], C0, C1, C2, dw(b, 3))
        out |= from_dw([C0, C1, C2, C3]) << (128 * i)
    return out  # DEST[MAXVL-1:VL] := 0


def vsm4key4(src1, src2, VL=128):
    """VSM4KEY4 xmm1/ymm1, xmm2/ymm2, xmm3/m128 or ymm3/m256 (VEX.128/256.F3.0F38.W0 DA /r)."""
    return _sm4_4rounds(src1, src2, VL, F_KEY)


def vsm4rnds4(src1, src2, VL=128):
    """VSM4RNDS4 xmm1/ymm1, xmm2/ymm2, xmm3/m128 or ymm3/m256 (VEX.128/256.F2.0F38.W0 DA /r)."""
    return _sm4_4rounds(src1, src2, VL, F_RND)


# ================================================================ standard-vector self tests
def _iroot(n, k):
    """floor(n ** (1/k)) for big ints."""
    x = 1 << ((n.bit_length() + k - 1) // k)
    while True:
        y = ((k - 1) * x + n // x ** (k - 1)) // k
        if y >= x:
            break
        x = y
    while x ** k > n:
        x -= 1
    while (x + 1) ** k <= n:
        x += 1
    return x


def _primes(n):
    ps, c = [], 2
    while len(ps) < n:
        if all(c % p for p in ps):
            ps.append(c)
        c += 1
    return ps


# FIPS 180-4 4.2.3: first 64 bits of the fractional parts of the cube roots of the first 80 primes
SHA512_K = [_iroot(p << 192, 3) & M64 for p in _primes(80)]
# FIPS 180-4 5.3.5: first 64 bits of the fractional parts of the square roots of the first 8 primes
SHA512_IV = [_iroot(p << 128, 2) & M64 for p in _primes(8)]


def _pad(msg, block, lenbytes):
    ml = len(msg) * 8
    m = msg + b'\x80'
    while (len(m) + lenbytes) % block:
        m += b'\x00'
    return m + ml.to_bytes(lenbytes, 'big')


def sha512_by_instructions(msg):
    h = list(SHA512_IV)
    m = _pad(msg, 128, 16)
    for blk in range(0, len(m), 128):
        w = [int.from_bytes(m[blk + 8 * i: blk + 8 * i + 8], 'big') for i in range(16)]
        Wv = [from_qw(w[4 * i: 4 * i + 4]) for i in range(4)]  # Wv[k] = W[4k..4k+3]
        for k in range(4, 20):
            x = vsha512msg1(Wv[k - 4], Wv[k - 3])                 # W[t-16+j] + s0(W[t-15+j])
            # + W[t-7+j]: qwords 1..3 of Wv[k-2] and qword 0 of Wv[k-1] (VPERMQ/VPALIGNR glue)
            w7 = [qw(Wv[k - 2], 1), qw(Wv[k - 2], 2), qw(Wv[k - 2], 3), qw(Wv[k - 1], 0)]
            x = from_qw([(qw(x, j) + w7[j]) & M64 for j in range(4)])
            Wv.append(vsha512msg2(x, Wv[k - 1]))                  # + s1(W[t-2+j])
        W = [qw(Wv[i // 4], i % 4) for i in range(80)]
        a, b, c, d, e, f, g, hh = h
        abef = from_qw([f, e, b, a])
        cdgh = from_qw([hh, g, d, c])
        for t in range(0, 80, 2):
            wk = from_qw([(W[t] + SHA512_K[t]) & M64, (W[t + 1] + SHA512_K[t + 1]) & M64])
            new = vsha512rnds2(cdgh, abef, wk)
            cdgh, abef = abef, new
        A, B, E, F = qw(abef, 3), qw(abef, 2), qw(abef, 1), qw(abef, 0)
        C, D, G, H = qw(cdgh, 3), qw(cdgh, 2), qw(cdgh, 1), qw(cdgh, 0)
        h = [(x + y) & M64 for x, y in zip(h, [A, B, C, D, E, F, G, H])]
    return b''.join(x.to_bytes(8, 'big') for x in h)


# GB/T 32905-2016 4.1 initial value
SM3_IV = [0x7380166F, 0x4914B2B9, 0x172442D7, 0xDA8A0600,
          0xA96F30BC, 0x163138AA, 0xE38DEE4D, 0xB0FB0E4E]


def ROR32(d, n):
    return ROL32(d, 32 - n % 32)


def sm3_by_instructions(msg):
    v = list(SM3_IV)
    m = _pad(msg, 64, 8)
    for blk in range(0, len(m), 64):
        W = [int.from_bytes(m[blk + 4 * i: blk + 4 * i + 4], 'big') for i in range(16)]
        while len(W) < 68:
            j = len(W)                                             # next four: W[j..j+3]
            src2 = from_dw(W[j - 16: j - 12])
            dst = from_dw(W[j - 9: j - 5])
            src1 = from_dw(W[j - 3: j] + [0])
            tmp = vsm3msg1(dst, src1, src2)
            W += [dw(vsm3msg2(tmp, from_dw(W[j - 13: j - 9]), from_dw(W[j - 6: j - 2])), i)
                  for i in range(4)]
        A, B, C, D, E, F, G, H = v
        abef = from_dw([F, E, B, A])
        # the instruction rotates C,D left by 9 and G,H by 19 on input: pre-rotate the IV back
        cdgh = from_dw([ROR32(H, 19), ROR32(G, 19), ROR32(D, 9), ROR32(C, 9)])
        for j in range(0, 64, 2):
            new = vsm3rnds2(cdgh, abef, from_dw([W[j], W[j + 1], W[j + 4], W[j + 5]]), j)
            cdgh, abef = abef, new
        A, B, E, F = dw(abef, 3), dw(abef, 2), dw(abef, 1), dw(abef, 0)
        C, D = ROL32(dw(cdgh, 3), 9), ROL32(dw(cdgh, 2), 9)
        G, H = ROL32(dw(cdgh, 1), 19), ROL32(dw(cdgh, 0), 19)
        v = [x ^ y for x, y in zip(v, [A, B, C, D, E, F, G, H])]
    return b''.join(x.to_bytes(4, 'big') for x in v)


# GB/T 32907-2016 7.3: FK, and CK[i] bytes = (4i+j)*7 mod 256
SM4_FK = [0xA3B1BAC6, 0x56AA3350, 0x677D9197, 0xB27022DC]
SM4_CK = [sum((((4 * i + j) * 7) & 0xFF) << (24 - 8 * j) for j in range(4)) for i in range(32)]


def sm4_by_instructions(key, pt):
    mk = [int.from_bytes(key[4 * i: 4 * i + 4], 'big') for i in range(4)]
    K = from_dw([mk[i] ^ SM4_FK[i] for i in range(4)])
    rk = []
    for i in range(0, 32, 4):
        K = vsm4key4(K, from_dw(SM4_CK[i: i + 4]))
        rk += [dw(K, k) for k in range(4)]
    X = from_dw([int.from_bytes(pt[4 * i: 4 * i + 4], 'big') for i in range(4)])
    for i in range(0, 32, 4):
        X = vsm4rnds4(X, from_dw(rk[i: i + 4]))
    return b''.join(dw(X, 3 - i).to_bytes(4, 'big') for i in range(4)), rk


def self_test():
    ok = True

    out = sys.stderr if ('--vectors' in sys.argv or '--cases' in sys.argv) else sys.stdout

    def check(name, got, want):
        nonlocal ok
        good = got == want
        ok &= good
        print('[ref] %-44s %s  %s' % (name, 'OK  ' if good else 'FAIL', got.hex()), file=out)
        if not good:
            print('[ref] %-44s       %s (expected)' % ('', want.hex()), file=out)

    print('[ref] SHA512_K[0]=%016x K[79]=%016x IV[0]=%016x' % (SHA512_K[0], SHA512_K[79], SHA512_IV[0]), file=out)
    fips_abc = bytes.fromhex('ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a'
                             '2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f')
    check('SHA-512("abc") FIPS 180-4', sha512_by_instructions(b'abc'), fips_abc)
    two = b'abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu'
    check('SHA-512(896-bit msg, 2 blocks) vs hashlib', sha512_by_instructions(two), hashlib.sha512(two).digest())
    check('SHA-512(1000 x "a") vs hashlib', sha512_by_instructions(b'a' * 1000), hashlib.sha512(b'a' * 1000).digest())

    check('SM3("abc") GB/T 32905 A.1', sm3_by_instructions(b'abc'),
          bytes.fromhex('66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0'))
    check('SM3("abcd" x 16) GB/T 32905 A.2', sm3_by_instructions(b'abcd' * 16),
          bytes.fromhex('debe9ff92275b8a138604889c18e5a4d6fdb70e5387e5765293dcba39c0c5732'))

    key = bytes.fromhex('0123456789abcdeffedcba9876543210')
    ct, rk = sm4_by_instructions(key, key)
    check('SM4 GB/T 32907 A.1 ciphertext', ct, bytes.fromhex('681edf34d206965e86b3e94f536e4246'))
    # GB/T 32907 A.1 also prints the round keys: rk0 = F12186F9, rk31 = 9124A012
    check('SM4 GB/T 32907 A.1 rk[0], rk[31]', bytes.fromhex('%08x%08x' % (rk[0], rk[31])),
          bytes.fromhex('f12186f99124a012'))

    # the VEX encoder used for the emulator vectors vs asmjit's assembler tests (x64)
    enc = [('C4E27FCCCA', vex3(2, 0, 1, 3, 0xCC, 1, 0, 2)),          # vsha512msg1 ymm1, xmm2
           ('C4E27FCDCA', vex3(2, 0, 1, 3, 0xCD, 1, 0, 2)),          # vsha512msg2 ymm1, ymm2
           ('C4E26FCBCB', vex3(2, 0, 1, 3, 0xCB, 1, 2, 3)),          # vsha512rnds2 ymm1, ymm2, xmm3
           ('C4E268DACB', vex3(2, 0, 0, 0, 0xDA, 1, 2, 3)),          # vsm3msg1 xmm1, xmm2, xmm3
           ('C4E269DACB', vex3(2, 0, 0, 1, 0xDA, 1, 2, 3)),          # vsm3msg2 xmm1, xmm2, xmm3
           ('C4E369DECB01', vex3(3, 0, 0, 1, 0xDE, 1, 2, 3, imm=1)), # vsm3rnds2 xmm1, xmm2, xmm3, 1
           ('C4E26ADACB', vex3(2, 0, 0, 2, 0xDA, 1, 2, 3)),          # vsm4key4 xmm1, xmm2, xmm3
           ('C4E26EDACB', vex3(2, 0, 1, 2, 0xDA, 1, 2, 3)),          # vsm4key4 ymm1, ymm2, ymm3
           ('C4E26BDACB', vex3(2, 0, 0, 3, 0xDA, 1, 2, 3)),          # vsm4rnds4 xmm1, xmm2, xmm3
           ('C4E26FDACB', vex3(2, 0, 1, 3, 0xDA, 1, 2, 3))]          # vsm4rnds4 ymm1, ymm2, ymm3
    check('VEX encoder vs asmjit (10 forms)', b''.join(bytes(e) for _, e in enc),
          bytes.fromhex(''.join(a for a, _ in enc)))
    return ok


# ================================================================ emulator test vectors
def _lcg(seed):
    x = seed
    while True:
        x = (x * 6364136223846793005 + 1442695040888963407) & M64
        yield x


def _rand(gen, bits):
    v = 0
    for i in range(0, bits, 64):
        v |= next(gen) << i
    return v & ((1 << bits) - 1)


def _hex_mem(v, nbytes):
    """Register / memory image, lowest byte first (the emu-alltest cases syntax)."""
    return v.to_bytes(nbytes, 'little').hex().upper()


def vex3(mmmmm, W, L, pp, opcode, reg, vvvv, rm, mod=3, imm=None):
    """C4 three-byte VEX: map 2 = 0F38, 3 = 0F3A; pp 0 = NP, 1 = 66, 2 = F3, 3 = F2."""
    b1 = (((reg >> 3) ^ 1) << 7) | (1 << 6) | (((rm >> 3) ^ 1) << 5) | mmmmm
    b2 = (W << 7) | ((~vvvv & 0xF) << 3) | (L << 2) | pp
    out = [0xC4, b1, b2, opcode, (mod << 6) | ((reg & 7) << 3) | (rm & 7)]
    if imm is not None:
        out.append(imm)
    return out


RSI = 6           # memory forms use [rsi]: emu-alltest points RSI at MEM + 0x8000
LO128 = (1 << 128) - 1
LO256 = (1 << 256) - 1


class Vectors:
    """Collects (encoding, inputs, expected) cases and #UD encodings for one ISA."""

    def __init__(self, isa):
        self.isa = isa
        self.ok = []
        self.ud = []

    def case(self, name, enc, regs, dest, expect, mem=None):
        self.ok.append((name, enc, regs, dest, expect & LO256, mem))

    def undef(self, name, enc):
        self.ud.append((name, enc))

    def c_table(self):
        out = ['/* generated by Emulator/tools/isa/ref_sha512_sm.py --vectors %s */' % self.isa,
               'static const X86VecCase test_x86_%s_cases[] = {' % self.isa]
        for name, enc, regs, dest, expect, mem in self.ok:
            rr = list(regs.items()) + [(-1, 0)] * (3 - len(regs))
            out.append('    { "%s", "%s",' % (name, bytes(enc).hex()))
            out.append('      { %s },' % ', '.join(str(r) for r, _ in rr))
            for r, v in rr:
                out.append('      "%s",' % (_hex_mem(v, 32) if r >= 0 else ''))
            out.append('      %s,' % ('"%s"' % _hex_mem(mem, 32) if mem is not None else 'NULL'))
            out.append('      %d, "%s" },' % (dest, _hex_mem(expect, 32)))
        out.append('};')
        out.append('static const X86UdCase test_x86_%s_ud[] = {' % self.isa)
        for name, enc in self.ud:
            out.append('    { "%s", "%s" },' % (name, bytes(enc).hex()))
        out.append('};')
        return '\n'.join(out)

    def cases_lines(self):
        out = []
        for name, enc, regs, dest, expect, mem in self.ok:
            ins = []
            for r, v in regs.items():
                ins.append('xmm%d=%s' % (r, _hex_mem(v & LO128, 16)))
                ins.append('ymmh%d=%s' % (r, _hex_mem(v >> 128, 16)))
            if mem is not None:
                ins.append('m+0x8000=%s' % _hex_mem(mem, 32))
            out.append('# %s' % name)
            out.append('.byte %s | %s => xmm%d=%s ymmh%d=%s' % (
                ', '.join('0x%02x' % x for x in enc), ' '.join(ins),
                dest, _hex_mem(expect & LO128, 16), dest, _hex_mem(expect >> 128, 16)))
        for name, enc in self.ud:
            out.append('# %s: #UD' % name)
            out.append('.byte %s' % ', '.join('0x%02x' % x for x in enc))
        return '\n'.join(out)


def vectors_sha512():
    v = Vectors('sha512')
    g = _lcg(0x5348413531325F53)
    for n in range(3):
        d, a, b = _rand(g, 256), _rand(g, 256), _rand(g, 256)
        v.case('vsha512msg1 ymm0, xmm1 #%d' % n, vex3(2, 0, 1, 3, 0xCC, 0, 0, 1),
               {0: d, 1: a}, 0, vsha512msg1(d, a & LO128))
        v.case('vsha512msg2 ymm0, ymm1 #%d' % n, vex3(2, 0, 1, 3, 0xCD, 0, 0, 1),
               {0: d, 1: a}, 0, vsha512msg2(d, a))
        v.case('vsha512rnds2 ymm0, ymm1, xmm2 #%d' % n, vex3(2, 0, 1, 3, 0xCB, 0, 1, 2),
               {0: d, 1: a, 2: b}, 0, vsha512rnds2(d, a, b & LO128))
    # high registers (VEX.R / VEX.B / vvvv bit 3) and operands aliasing the destination
    d, a, b = _rand(g, 256), _rand(g, 256), _rand(g, 256)
    v.case('vsha512msg1 ymm9, xmm12', vex3(2, 0, 1, 3, 0xCC, 9, 0, 12), {9: d, 12: a}, 9, vsha512msg1(d, a & LO128))
    v.case('vsha512msg2 ymm12, ymm9', vex3(2, 0, 1, 3, 0xCD, 12, 0, 9), {12: d, 9: a}, 12, vsha512msg2(d, a))
    v.case('vsha512rnds2 ymm9, ymm14, xmm3', vex3(2, 0, 1, 3, 0xCB, 9, 14, 3), {9: d, 14: a, 3: b}, 9,
           vsha512rnds2(d, a, b & LO128))
    v.case('vsha512msg1 ymm0, xmm0', vex3(2, 0, 1, 3, 0xCC, 0, 0, 0), {0: d}, 0, vsha512msg1(d, d & LO128))
    v.case('vsha512msg2 ymm0, ymm0', vex3(2, 0, 1, 3, 0xCD, 0, 0, 0), {0: d}, 0, vsha512msg2(d, d))
    v.case('vsha512rnds2 ymm0, ymm1, xmm0', vex3(2, 0, 1, 3, 0xCB, 0, 1, 0), {0: d, 1: a}, 0,
           vsha512rnds2(d, a, d & LO128))
    v.case('vsha512rnds2 ymm0, ymm0, xmm0', vex3(2, 0, 1, 3, 0xCB, 0, 0, 0), {0: d}, 0,
           vsha512rnds2(d, d, d & LO128))
    # #UD: VEX.L0, VEX.W1, memory operand (11:rrr:bbb only), vvvv != 1111b, wrong pp, legacy
    for op, nm in ((0xCC, 'vsha512msg1'), (0xCD, 'vsha512msg2'), (0xCB, 'vsha512rnds2')):
        vv = 1 if op == 0xCB else 0
        v.undef('%s VEX.L0' % nm, vex3(2, 0, 0, 3, op, 0, vv, 1))
        v.undef('%s VEX.W1' % nm, vex3(2, 1, 1, 3, op, 0, vv, 1))
        v.undef('%s [rsi] (register-only form)' % nm, vex3(2, 0, 1, 3, op, 0, vv, RSI, mod=0))
        v.undef('%s VEX.66' % nm, vex3(2, 0, 1, 1, op, 0, vv, 1))
        v.undef('%s VEX.NP (SHA-NI opcode, VEX)' % nm, vex3(2, 0, 1, 0, op, 0, vv, 1))
        v.undef('%s legacy F2 0F 38' % nm, [0xF2, 0x0F, 0x38, op, 0xC1])
    v.undef('vsha512msg1 vvvv=0010b', vex3(2, 0, 1, 3, 0xCC, 0, 2, 1))
    v.undef('vsha512msg2 vvvv=1101b', vex3(2, 0, 1, 3, 0xCD, 0, 13, 1))
    return v


def vectors_sm3():
    v = Vectors('sm3')
    g = _lcg(0x534D335F56454358)
    for n in range(2):
        d, a, b = _rand(g, 256), _rand(g, 256), _rand(g, 256)
        dl, al, bl = d & LO128, a & LO128, b & LO128
        # VEX.128: the upper half of the destination is zeroed (expected values are 128-bit)
        v.case('vsm3msg1 xmm0, xmm1, xmm2 #%d' % n, vex3(2, 0, 0, 0, 0xDA, 0, 1, 2),
               {0: d, 1: a, 2: b}, 0, vsm3msg1(dl, al, bl))
        v.case('vsm3msg2 xmm0, xmm1, xmm2 #%d' % n, vex3(2, 0, 0, 1, 0xDA, 0, 1, 2),
               {0: d, 1: a, 2: b}, 0, vsm3msg2(dl, al, bl))
        for imm in (0x00, 0x0E, 0x10, 0x3E, 0xC1, 0x11, 0xFF):
            v.case('vsm3rnds2 xmm0, xmm1, xmm2, 0x%02x #%d' % (imm, n), vex3(3, 0, 0, 1, 0xDE, 0, 1, 2, imm=imm),
                   {0: d, 1: a, 2: b}, 0, vsm3rnds2(dl, al, bl, imm))
    d, a, b = _rand(g, 256), _rand(g, 256), _rand(g, 256)
    dl, al, bl = d & LO128, a & LO128, b & LO128
    v.case('vsm3msg1 xmm0, xmm1, [rsi]', vex3(2, 0, 0, 0, 0xDA, 0, 1, RSI, mod=0), {0: d, 1: a}, 0,
           vsm3msg1(dl, al, bl), mem=bl)
    v.case('vsm3msg2 xmm0, xmm1, [rsi]', vex3(2, 0, 0, 1, 0xDA, 0, 1, RSI, mod=0), {0: d, 1: a}, 0,
           vsm3msg2(dl, al, bl), mem=bl)
    v.case('vsm3rnds2 xmm0, xmm1, [rsi], 0x22', vex3(3, 0, 0, 1, 0xDE, 0, 1, RSI, mod=0, imm=0x22), {0: d, 1: a}, 0,
           vsm3rnds2(dl, al, bl, 0x22), mem=bl)
    v.case('vsm3msg1 xmm10, xmm13, xmm8', vex3(2, 0, 0, 0, 0xDA, 10, 13, 8), {10: d, 13: a, 8: b}, 10,
           vsm3msg1(dl, al, bl))
    v.case('vsm3msg2 xmm8, xmm2, xmm15', vex3(2, 0, 0, 1, 0xDA, 8, 2, 15), {8: d, 2: a, 15: b}, 8,
           vsm3msg2(dl, al, bl))
    v.case('vsm3rnds2 xmm15, xmm9, xmm11, 0x30', vex3(3, 0, 0, 1, 0xDE, 15, 9, 11, imm=0x30), {15: d, 9: a, 11: b},
           15, vsm3rnds2(dl, al, bl, 0x30))
    v.case('vsm3msg1 xmm0, xmm0, xmm0', vex3(2, 0, 0, 0, 0xDA, 0, 0, 0), {0: d}, 0, vsm3msg1(dl, dl, dl))
    v.case('vsm3msg2 xmm0, xmm0, xmm0', vex3(2, 0, 0, 1, 0xDA, 0, 0, 0), {0: d}, 0, vsm3msg2(dl, dl, dl))
    v.case('vsm3rnds2 xmm0, xmm0, xmm0, 0x14', vex3(3, 0, 0, 1, 0xDE, 0, 0, 0, imm=0x14), {0: d}, 0,
           vsm3rnds2(dl, dl, dl, 0x14))
    # #UD: VEX.L1, VEX.W1, legacy encodings
    v.undef('vsm3msg1 VEX.L1', vex3(2, 0, 1, 0, 0xDA, 0, 1, 2))
    v.undef('vsm3msg2 VEX.L1', vex3(2, 0, 1, 1, 0xDA, 0, 1, 2))
    v.undef('vsm3rnds2 VEX.L1', vex3(3, 0, 1, 1, 0xDE, 0, 1, 2, imm=0))
    v.undef('vsm3msg1 VEX.W1', vex3(2, 1, 0, 0, 0xDA, 0, 1, 2))
    v.undef('vsm3msg2 VEX.W1', vex3(2, 1, 0, 1, 0xDA, 0, 1, 2))
    v.undef('vsm3rnds2 VEX.W1', vex3(3, 1, 0, 1, 0xDE, 0, 1, 2, imm=0))
    v.undef('vsm3rnds2 VEX.F2', vex3(3, 0, 0, 3, 0xDE, 0, 1, 2, imm=0))
    v.undef('legacy NP 0F 38 DA', [0x0F, 0x38, 0xDA, 0xC2])
    v.undef('legacy 66 0F 38 DA', [0x66, 0x0F, 0x38, 0xDA, 0xC2])
    v.undef('legacy 66 0F 3A DE', [0x66, 0x0F, 0x3A, 0xDE, 0xC2, 0x00])
    return v


def vectors_sm4():
    v = Vectors('sm4')
    g = _lcg(0x534D345F56454358)
    for n in range(2):
        d, a, b = _rand(g, 256), _rand(g, 256), _rand(g, 256)
        for L in (0, 1):
            VL, r = (256, 'ymm') if L else (128, 'xmm')
            m = (1 << VL) - 1
            v.case('vsm4key4 %s0, %s1, %s2 #%d' % (r, r, r, n), vex3(2, 0, L, 2, 0xDA, 0, 1, 2),
                   {0: d, 1: a, 2: b}, 0, vsm4key4(a & m, b & m, VL))
            v.case('vsm4rnds4 %s0, %s1, %s2 #%d' % (r, r, r, n), vex3(2, 0, L, 3, 0xDA, 0, 1, 2),
                   {0: d, 1: a, 2: b}, 0, vsm4rnds4(a & m, b & m, VL))
    # the standard example (GB/T 32907 A.1) as the instructions see it: first 4 rounds
    key = bytes.fromhex('0123456789abcdeffedcba9876543210')
    mk = from_dw([int.from_bytes(key[4 * i: 4 * i + 4], 'big') ^ SM4_FK[i] for i in range(4)])
    ck = from_dw(SM4_CK[0:4])
    v.case('vsm4key4 xmm0, xmm1, xmm2 (GB/T 32907 rk0-3)', vex3(2, 0, 0, 2, 0xDA, 0, 1, 2), {1: mk, 2: ck}, 0,
           vsm4key4(mk, ck))
    d, a, b = _rand(g, 256), _rand(g, 256), _rand(g, 256)
    for L in (0, 1):
        VL, r = (256, 'ymm') if L else (128, 'xmm')
        m = (1 << VL) - 1
        v.case('vsm4key4 %s0, %s1, [rsi]' % (r, r), vex3(2, 0, L, 2, 0xDA, 0, 1, RSI, mod=0), {0: d, 1: a}, 0,
               vsm4key4(a & m, b & m, VL), mem=b)
        v.case('vsm4rnds4 %s0, %s1, [rsi]' % (r, r), vex3(2, 0, L, 3, 0xDA, 0, 1, RSI, mod=0), {0: d, 1: a}, 0,
               vsm4rnds4(a & m, b & m, VL), mem=b)
        v.case('vsm4key4 %s11, %s14, %s9' % (r, r, r), vex3(2, 0, L, 2, 0xDA, 11, 14, 9), {11: d, 14: a, 9: b}, 11,
               vsm4key4(a & m, b & m, VL))
        v.case('vsm4rnds4 %s9, %s3, %s12' % (r, r, r), vex3(2, 0, L, 3, 0xDA, 9, 3, 12), {9: d, 3: a, 12: b}, 9,
               vsm4rnds4(a & m, b & m, VL))
        v.case('vsm4key4 %s0, %s0, %s0' % (r, r, r), vex3(2, 0, L, 2, 0xDA, 0, 0, 0), {0: d}, 0,
               vsm4key4(d & m, d & m, VL))
        v.case('vsm4rnds4 %s0, %s0, %s0' % (r, r, r), vex3(2, 0, L, 3, 0xDA, 0, 0, 0), {0: d}, 0,
               vsm4rnds4(d & m, d & m, VL))
    # #UD: VEX.W1, legacy encodings
    v.undef('vsm4key4 VEX.128.W1', vex3(2, 1, 0, 2, 0xDA, 0, 1, 2))
    v.undef('vsm4key4 VEX.256.W1', vex3(2, 1, 1, 2, 0xDA, 0, 1, 2))
    v.undef('vsm4rnds4 VEX.128.W1', vex3(2, 1, 0, 3, 0xDA, 0, 1, 2))
    v.undef('vsm4rnds4 VEX.256.W1', vex3(2, 1, 1, 3, 0xDA, 0, 1, 2))
    v.undef('legacy F3 0F 38 DA', [0xF3, 0x0F, 0x38, 0xDA, 0xC2])
    v.undef('legacy F2 0F 38 DA', [0xF2, 0x0F, 0x38, 0xDA, 0xC2])
    return v


VECTORS = {'sha512': vectors_sha512, 'sm3': vectors_sm3, 'sm4': vectors_sm4}


if __name__ == '__main__':
    good = self_test()
    if '--vectors' in sys.argv:
        v = VECTORS[sys.argv[sys.argv.index('--vectors') + 1]]()
        print(v.c_table())
    if '--cases' in sys.argv:
        for isa in sys.argv[sys.argv.index('--cases') + 1].split(','):
            print('# ---- %s (ref_sha512_sm.py --cases %s)' % (isa.upper(), isa))
            print(VECTORS[isa]().cases_lines())
    sys.exit(0 if good else 1)
