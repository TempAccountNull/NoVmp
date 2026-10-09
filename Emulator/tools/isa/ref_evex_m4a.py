#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m4a.py -- independent reference model (Python 3 stdlib only) of the EVEX
AVX512_VBMI2, AVX512_VBMI (VPMULTISHIFTQB), AVX512_VNNI and AVX512_BF16 instructions
(ledger U550-U557) and generator of the expected-value case file
Emulator/data/cases_evex_m4a.txt.

Written from the Intel SDM text only (SDM-092 Vol2A 2.7 "Intel AVX-512 encoding", Tables
2-36 .. 2-45, 2.8 exception classes E4 / E4NF; Vol2C instruction pages, "Operation"
pseudocode and Description; Vol1 4.9.1.5 underflow / tininess), and the BF16 numerics paper
338302 1.2.1 (the BF16 FMA unit), not from any C implementation:

  AVX512_VBMI2 (all EVEX.66, {k1}{z}, class E4 - fault suppression):
  VPEXPANDB/W      0F38.W0/W1 62 /r   Tuple1 Scalar (N = element), memory source contiguous
  VPCOMPRESSB/W    0F38.W0/W1 63 /r   Tuple1 Scalar, memory destination: merging only
  VPSHLDVW         0F38.W1 70 /r      Full Mem; VPSHLDVD/Q 0F38.W0/W1 71 /r Full ({1toN})
  VPSHRDVW         0F38.W1 72 /r      Full Mem; VPSHRDVD/Q 0F38.W0/W1 73 /r Full ({1toN})
  VPSHLDW          0F3A.W1 70 /r ib   Full Mem; VPSHLDD/Q  0F3A.W0/W1 71 /r ib Full
  VPSHRDW          0F3A.W1 72 /r ib   Full Mem; VPSHRDD/Q  0F3A.W0/W1 73 /r ib Full
  AVX512_VBMI:
  VPMULTISHIFTQB   0F38.W1 83 /r      Full tuple ({1to8}, disp8*N by qwords), byte writemask,
                                      class E4NF (the whole memory operand is read)
  AVX512_VNNI (EVEX.66.0F38.W0, Full, {1toN} m32bcst, class E4, DEST is the accumulator):
  VPDPBUSD 50, VPDPBUSDS 51, VPDPWSSD 52, VPDPWSSDS 53
  AVX512_BF16 ("(AVX512_BF16 AND AVX512F) OR AVX10.1", EVEX.W0, Full, m32bcst):
  VCVTNE2PS2BF16   F2.0F38 72 /r      word writemask, class E4NF
  VCVTNEPS2BF16    F3.0F38 72 /r      destination VL/2, word writemask, class E4
  VDPBF16PS        F3.0F38 52 /r      DEST is the accumulator, class E4

CPUID: every form is "<feature> OR AVX10.1", EVEX.128/256 also need AVX512VL; emu-alltest
--avx512 enables every UC_X86_AVX512_* bit (and --avx10 1 must give the same results).

Generic EVEX wrappers (SDM pseudocode of every page):
  MASK     FOR j := 0 TO KL-1: IF k1[j] OR *no writemask* THEN DEST[j] := op ELSE
           (*merging*: unchanged | *zeroing*: 0); DEST[MAXVL-1:VL] := 0 (VCVTNEPS2BF16:
           KL = VL/32 words, DEST[MAXVL-1:VL/2] := 0)
  BCST     IF (EVEX.b = 1) AND (SRC2 *is memory*) THEN t := SRC2.elem[0]
  LOAD     E4: masked-off elements are not read (fault suppression); E4NF (VPMULTISHIFTQB,
           VCVTNE2PS2BF16): the whole operand is read whatever the mask (Table 2-52 #PF)
  COMPRESS / EXPAND: the memory side holds only the active elements, contiguously ("Only
           the contiguous vector is written"; expand reads SRC.elem[k] for k < popcount)

BF16 (Vol2C VCVTNEPS2BF16 "convert_fp32_to_bfloat16", VDPBF16PS "make_fp32", Table 5-4):
  convert_fp32_to_bfloat16(x): zero/denormal -> sign, 0; infinity -> x[31:16]; NaN ->
           x[31:16] with bit 6 set; normal -> (x + 7FFFh + x[16])[31:16] (integer add)
  VDPBF16PS: srcdest += make_fp32(src1.bf16[2i+1]) * make_fp32(src2.bf16[2i+1]), then
           srcdest += make_fp32(src1.bf16[2i]) * make_fp32(src2.bf16[2i]); each step one
           FP32 FMA (exact product and sum, one RNE rounding) with DAZ in (denormal src1,
           src2, srcdest -> signed zero) and FTZ out (338302: "a three-way FP32 FMA with DAZ
           and FTZ set to On": a result that is tiny after rounding with an unbounded exponent
           - Vol1 4.9.1.5 - becomes a zero of its sign); MXCSR neither consulted nor updated.
           NaNs (Table 5-4: src1 low, src2 low, src1 high, src2 high, srcdest): the first NaN
           operand of the step in the order src1, src2, srcdest, quieted; invalid (inf * 0,
           inf - inf) -> QNaN indefinite FFC00000h.

Usage:
  python ref_evex_m4a.py --selftest   hand-derived checks of the model, exit 0 on pass
  python ref_evex_m4a.py --cases      Emulator/data/cases_evex_m4a.txt (stdout)
  python ref_evex_m4a.py --hwgen       Emulator/data/cases_evex_m4a_hw.txt (stdout): hardware
                                       cases for the parts of the model the
                                       i5-13600K can check: VEX AVX-VNNI VPDPBUSD(S)/VPDPWSSD(S)
                                       (= the EVEX element function), SHLD/SHRD r16/r32/r64
                                       (= the VPSHLD*/VPSHRD* element function for counts below
                                       the width), ROR r64 (VPMULTISHIFTQB byte), VFMADD231SS
                                       with MXCSR = 9FC0h (FTZ, DAZ, RNE, masked) = one VDPBF16PS
                                       step
  python ref_evex_m4a.py --hwcmp LOG   compare the host results ("hw:" lines of the emu-alltest
                                       --cases log of that file) with the model
"""

import random
import sys
from fractions import Fraction

# harness layout (Emulator/tests/alltest/at_engine.hpp): RSI = R14 = MEM + 0x8000, operand
# memory MEM .. MEM + 0xFFFF mapped, MEM + 0x10000 unmapped
MEM_RSI = 0x8000
RSI = 6


# ---------------------------------------------------------------------------------------
# EVEX encoding (SDM Vol2A 2.7.1 Table 2-32: P0 = R X B R' 0 m m m, P1 = W v v v v 1 p p,
# P2 = z L' L b V' a a a; register fields stored inverted)
# ---------------------------------------------------------------------------------------
class Mem:
    def __init__(self, base=RSI, disp=0, disp32=False):
        self.base, self.disp, self.disp32 = base, disp, disp32


def evex(mmm, pp, w, opc, reg, rm, vvvv=None, ll=0, b=0, z=0, aaa=0, n=1, imm=None, p2_vp=None):
    """bytes of one EVEX instruction; reg / rm 0-31 (rm may be Mem); vvvv None = 1111b, V'=1.
    n = disp8*N of the form; imm = trailing imm8 or None."""
    r, rr = (reg >> 3) & 1, (reg >> 4) & 1
    if isinstance(rm, Mem):
        x = 0
        bb = (rm.base >> 3) & 1
        d = rm.disp
        if not rm.disp32 and d % n == 0 and -128 <= d // n <= 127 and d != 0:
            mod, db = 1, bytes([(d // n) & 0xFF])
        elif d == 0 and not rm.disp32 and (rm.base & 7) != 5:
            mod, db = 0, b""
        else:
            mod, db = 2, (d & 0xFFFFFFFF).to_bytes(4, "little")
        tail = bytes([(mod << 6) | ((reg & 7) << 3) | (rm.base & 7)])
        if (rm.base & 7) == 4:
            tail += bytes([0x24])
        tail += db
    else:
        bb, x = (rm >> 3) & 1, (rm >> 4) & 1
        tail = bytes([0xC0 | ((reg & 7) << 3) | (rm & 7)])
    v = 0 if vvvv is None else vvvv
    p0 = ((r ^ 1) << 7) | ((x ^ 1) << 6) | ((bb ^ 1) << 5) | ((rr ^ 1) << 4) | mmm
    p1 = (w << 7) | (((~v) & 0xF) << 3) | 4 | pp
    vp = ((v >> 4) & 1) ^ 1 if p2_vp is None else p2_vp
    p2 = (z << 7) | (ll << 5) | (b << 4) | (vp << 3) | aaa
    out = bytes([0x62, p0, p1, p2, opc]) + tail
    if imm is not None:
        out += bytes([imm & 0xFF])
    return out


def byte_list(bs):
    return ".byte " + ", ".join("0x%02x" % c for c in bs)


def elems(buf, esz):
    return [int.from_bytes(buf[i:i + esz], "little") for i in range(0, len(buf), esz)]


def pack(vals, esz):
    return b"".join((v & ((1 << (8 * esz)) - 1)).to_bytes(esz, "little") for v in vals)


def hexs(buf):
    return "".join("%02X" % c for c in buf)


def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


# ---------------------------------------------------------------------------------------
# instruction models (Vol2C "Operation")
# ---------------------------------------------------------------------------------------
def concat_shift_left(hi, lo, c, bits):
    """VPSHLD*: tmp := concat(hi, lo) << c; DEST := tmp.upper element (the element width)"""
    m = (1 << bits) - 1
    tmp = (((hi & m) << bits) | (lo & m)) << c
    return (tmp >> bits) & m


def concat_shift_right(hi, lo, c, bits):
    """VPSHRD*: DEST := (concat(hi, lo) >> c) truncated to the element width"""
    m = (1 << bits) - 1
    return ((((hi & m) << bits) | (lo & m)) >> c) & m


def vpshld(src2, src3, imm, bits):
    """VPSHLDW/D/Q: concat(SRC2, SRC3) << (imm8 & (bits-1)), upper element"""
    return concat_shift_left(src2, src3, imm & (bits - 1), bits)


def vpshrd(src2, src3, imm, bits):
    """VPSHRDW/D/Q: concat(SRC3, SRC2) >> (imm8 & (bits-1))"""
    return concat_shift_right(src3, src2, imm & (bits - 1), bits)


def vpshldv(dest, src2, src3, bits):
    """VPSHLDVW/D/Q: concat(DEST, SRC2) << (SRC3 & (bits-1)), upper element"""
    return concat_shift_left(dest, src2, src3 & (bits - 1), bits)


def vpshrdv(dest, src2, src3, bits):
    """VPSHRDVW/D/Q: concat(SRC2, DEST) >> (SRC3 & (bits-1))"""
    return concat_shift_right(src2, dest, src3 & (bits - 1), bits)


def multishift_byte(ctrl, tcur):
    """VPMULTISHIFTQB: res.bit[k] := tcur.bit[(ctrl + k) mod 64], k = 0..7; ctrl := byte & 63"""
    ctrl &= 63
    r = 0
    for k in range(8):
        r |= ((tcur >> ((ctrl + k) % 64)) & 1) << k
    return r


def vnni_bytes(acc, a4, b4, sat):
    """VPDPBUSD(S): acc + sum ZERO_EXTEND(SRC1.byte) * SIGN_EXTEND(SRC2.byte) over 4 bytes;
    the S form saturates the whole sum to a signed dword, the other wraps"""
    s = sx(acc, 32)
    for k in range(4):
        s += ((a4 >> (8 * k)) & 0xFF) * sx(b4 >> (8 * k), 8)
    return sat32(s) if sat else s & 0xFFFFFFFF


def vnni_words(acc, a2, b2, sat):
    """VPDPWSSD(S): acc + sum SIGN_EXTEND(SRC1.word) * SIGN_EXTEND(SRC2.word) over 2 words"""
    s = sx(acc, 32)
    for k in range(2):
        s += sx(a2 >> (16 * k), 16) * sx(b2 >> (16 * k), 16)
    return sat32(s) if sat else s & 0xFFFFFFFF


def sat32(s):
    if s > 0x7FFFFFFF:
        return 0x7FFFFFFF
    if s < -0x80000000:
        return 0x80000000
    return s & 0xFFFFFFFF


def cvt_fp32_to_bf16(x):
    """Vol2C VCVTNEPS2BF16 'convert_fp32_to_bfloat16' (literal)"""
    exp = (x >> 23) & 0xFF
    man = x & 0x7FFFFF
    if exp == 0:                                   # zero or denormal
        return (x >> 16) & 0x8000                  # dest[15] := x[31]; dest[14:0] := 0
    if exp == 0xFF and man == 0:                   # infinity
        return (x >> 16) & 0xFFFF
    if exp == 0xFF:                                # NaN: truncate, set the mantissa MSB
        return ((x >> 16) & 0xFFFF) | 0x40
    lsb = (x >> 16) & 1                            # normal number
    temp = (x + 0x7FFF + lsb) & 0xFFFFFFFF         # integer add
    return (temp >> 16) & 0xFFFF


# --- exact FP32 arithmetic for VDPBF16PS ----------------------------------------------
QNAN_INDEFINITE = 0xFFC00000


def f32_is_nan(x):
    return (x >> 23) & 0xFF == 0xFF and x & 0x7FFFFF


def f32_daz(x):
    """DAZ: a denormal (exponent field 0, fraction != 0) is a zero of its sign"""
    return x & 0x80000000 if (x >> 23) & 0xFF == 0 else x


def f32_value(x):
    """(sign, kind, magnitude) of a non-NaN FP32 value; kind 'zero' / 'inf' / 'fin'"""
    s = x >> 31
    e = (x >> 23) & 0xFF
    f = x & 0x7FFFFF
    if e == 0xFF:
        return s, "inf", None
    if e == 0 and f == 0:
        return s, "zero", Fraction(0)
    if e == 0:
        return s, "fin", Fraction(f, 1 << 149)
    return s, "fin", Fraction((1 << 23) | f) * Fraction(2) ** (e - 150)


def round_f32_ftz(sign, q):
    """RNE rounding of the exact magnitude q > 0 to FP32 with an unbounded exponent; FTZ: a
    result whose rounded magnitude is below 2^-126 (tiny after rounding, Vol1 4.9.1.5) is a
    zero of its sign; overflow (RNE) -> infinity"""
    e = q.numerator.bit_length() - q.denominator.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    while Fraction(2) ** (e + 1) <= q:
        e += 1
    # q in [2^e, 2^(e+1)): significand q / 2^(e-23) in [2^23, 2^24)
    m = q / Fraction(2) ** (e - 23)
    mi = m.numerator // m.denominator
    rem = m - mi
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and mi & 1):
        mi += 1
    if mi == 1 << 24:
        mi >>= 1
        e += 1
    if e < -126:
        return sign << 31                       # FTZ (tiny after rounding)
    if e > 127:
        return (sign << 31) | 0x7F800000        # overflow, RNE -> infinity
    return (sign << 31) | ((e + 127) << 23) | (mi - (1 << 23))


def fma32_daz_ftz(acc, x, y):
    """one VDPBF16PS step: acc + x * y (FP32 bit patterns) as described in the module text"""
    for v in (x, y, acc):                       # Table 5-4 order within the step
        if f32_is_nan(v):
            return v | 0x00400000
    x, y, acc = f32_daz(x), f32_daz(y), f32_daz(acc)
    sx_, kx, vx = f32_value(x)
    sy_, ky, vy = f32_value(y)
    sa, ka, va = f32_value(acc)
    sp = sx_ ^ sy_
    if (kx == "inf" and ky == "zero") or (kx == "zero" and ky == "inf"):
        return QNAN_INDEFINITE                  # inf * 0
    if kx == "inf" or ky == "inf":
        if ka == "inf" and sa != sp:
            return QNAN_INDEFINITE              # inf - inf
        return (sp << 31) | 0x7F800000
    if ka == "inf":
        return acc
    p = vx * vy
    total = (-p if sp else p) + (-va if sa else va)
    if total == 0:
        # exact zero: +0 in RNE unless both terms are zeros of negative sign (IEEE 754 6.3)
        if p == 0 and va == 0 and sp and sa:
            return 0x80000000
        return 0
    return round_f32_ftz(1 if total < 0 else 0, abs(total))


def vdpbf16ps_elem(acc, a2, b2):
    """VDPBF16PS dword: the high pair (bf16[2i+1]) first, then the low pair (bf16[2i])"""
    acc = fma32_daz_ftz(acc, ((a2 >> 16) & 0xFFFF) << 16, ((b2 >> 16) & 0xFFFF) << 16)
    acc = fma32_daz_ftz(acc, (a2 & 0xFFFF) << 16, (b2 & 0xFFFF) << 16)
    return acc


# ---------------------------------------------------------------------------------------
# cases
# ---------------------------------------------------------------------------------------
class Case:
    def __init__(self, title):
        self.title = title
        self.code = b""
        self.zmm, self.k, self.mem = {}, {}, {}
        self.exp = []
        self.fault = None

    def line(self):
        ins = ["zmm%d=%s" % (r, hexs(self.zmm[r])) for r in sorted(self.zmm)]
        ins += ["k%d=0x%X" % (r, self.k[r]) for r in sorted(self.k)]
        ins += ["m+0x%X=%s" % (o, hexs(self.mem[o])) for o in sorted(self.mem)]
        exp = list(self.exp) + ([self.fault] if self.fault else [])
        return "%s | %s => %s" % (byte_list(self.code), " ".join(ins), " ".join(exp))


SEED = 0x4A_5EED_550
RNG = random.Random(SEED)
VL_LL = {16: 0, 32: 1, 64: 2}
out_lines = []


def emit(c):
    out_lines.append("# " + c.title)
    out_lines.append(c.line())


def comment(t):
    out_lines.append("# " + t)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


def mask_merge(old, res, esz, dbytes, kmask, zero):
    """MASK wrapper over the destination's dbytes + DEST[MAXVL-1:dbytes] := 0"""
    o = elems(old[:dbytes], esz)
    out = []
    for j in range(dbytes // esz):
        if kmask is None or (kmask >> j) & 1:
            out.append(res[j])
        else:
            out.append(0 if zero else o[j])
    return pack(out, esz) + bytes(64 - dbytes)


# --- element-function -> vector-function adapters (d, a, b: VL-byte images, b already
#     broadcast) --------------------------------------------------------------------------
def shd_vec(kind, esz):
    bits = 8 * esz

    def fn(d, a, b, vl, imm):
        D, A, B = elems(d, esz), elems(a, esz), elems(b, esz)
        if kind == "shld":
            r = [vpshld(A[j], B[j], imm, bits) for j in range(vl // esz)]
        elif kind == "shrd":
            r = [vpshrd(A[j], B[j], imm, bits) for j in range(vl // esz)]
        elif kind == "shldv":
            r = [vpshldv(D[j], A[j], B[j], bits) for j in range(vl // esz)]
        else:
            r = [vpshrdv(D[j], A[j], B[j], bits) for j in range(vl // esz)]
        return pack(r, esz)
    return fn


def multishift_vec(d, a, b, vl, imm):
    out = []
    for i in range(vl // 8):
        tcur = int.from_bytes(b[8 * i:8 * i + 8], "little")
        for j in range(8):
            out.append(multishift_byte(a[8 * i + j], tcur))
    return bytes(out)


def vnni_vec(words, sat):
    def fn(d, a, b, vl, imm):
        D, A, B = elems(d, 4), elems(a, 4), elems(b, 4)
        f = vnni_words if words else vnni_bytes
        return pack([f(D[i], A[i], B[i], sat) for i in range(vl // 4)], 4)
    return fn


def cvtne2_vec(d, a, b, vl, imm):
    """VCVTNE2PS2BF16: word i < KL/2 from src2.fp32[i], else src1.fp32[i - KL/2]"""
    kl = vl // 2
    A, B = elems(a, 4), elems(b, 4)
    out = []
    for i in range(kl):
        out.append(cvt_fp32_to_bf16(B[i] if i < kl // 2 else A[i - kl // 2]))
    return pack(out, 2)


def cvtne1_vec(d, a, b, vl, imm):
    """VCVTNEPS2BF16: dest.word[i] := convert(src.fp32[i]), i < KL/2 (VL/2 destination bytes)"""
    return pack([cvt_fp32_to_bf16(x) for x in elems(b, 4)], 2)


def dpbf16_vec(d, a, b, vl, imm):
    D, A, B = elems(d, 4), elems(a, 4), elems(b, 4)
    return pack([vdpbf16ps_elem(D[i], A[i], B[i]) for i in range(vl // 4)], 4)


# form keys: name, map (2 = 0F38, 3 = 0F3A), pp (0 NP 1 66 2 F3 3 F2), opc, w, layout
# ("rvm": V, H, W; "rm": V, W; "cx_e" / "cx_c": expand / compress), imm (has imm8), mesz
# (mask / destination element bytes), besz ({1toN} element bytes, None = Full Mem tuple),
# fs (fault suppression: E4, else E4NF), dsrc (DEST read), dfrac (destination bytes =
# VL / dfrac), fn(d, a, b, vl, imm) -> result bytes, feat, kind (value generator)
def F(**kw):
    kw.setdefault("map", 2)
    kw.setdefault("pp", 1)
    kw.setdefault("imm", False)
    kw.setdefault("dsrc", False)
    kw.setdefault("dfrac", 1)
    kw.setdefault("fs", True)
    return kw


FORMS = {
    "VBMI2": [
        F(name="VPEXPANDB", opc=0x62, w=0, layout="cx_e", mesz=1, besz=None, feat="AVX512_VBMI2"),
        F(name="VPEXPANDW", opc=0x62, w=1, layout="cx_e", mesz=2, besz=None, feat="AVX512_VBMI2"),
        F(name="VPCOMPRESSB", opc=0x63, w=0, layout="cx_c", mesz=1, besz=None, feat="AVX512_VBMI2"),
        F(name="VPCOMPRESSW", opc=0x63, w=1, layout="cx_c", mesz=2, besz=None, feat="AVX512_VBMI2"),
        F(name="VPSHLDVW", opc=0x70, w=1, layout="rvm", mesz=2, besz=None, dsrc=True,
          fn=shd_vec("shldv", 2), feat="AVX512_VBMI2", kind="shift"),
        F(name="VPSHLDVD", opc=0x71, w=0, layout="rvm", mesz=4, besz=4, dsrc=True,
          fn=shd_vec("shldv", 4), feat="AVX512_VBMI2", kind="shift"),
        F(name="VPSHLDVQ", opc=0x71, w=1, layout="rvm", mesz=8, besz=8, dsrc=True,
          fn=shd_vec("shldv", 8), feat="AVX512_VBMI2", kind="shift"),
        F(name="VPSHRDVW", opc=0x72, w=1, layout="rvm", mesz=2, besz=None, dsrc=True,
          fn=shd_vec("shrdv", 2), feat="AVX512_VBMI2", kind="shift"),
        F(name="VPSHRDVD", opc=0x73, w=0, layout="rvm", mesz=4, besz=4, dsrc=True,
          fn=shd_vec("shrdv", 4), feat="AVX512_VBMI2", kind="shift"),
        F(name="VPSHRDVQ", opc=0x73, w=1, layout="rvm", mesz=8, besz=8, dsrc=True,
          fn=shd_vec("shrdv", 8), feat="AVX512_VBMI2", kind="shift"),
        F(name="VPSHLDW", map=3, opc=0x70, w=1, layout="rvm", imm=True, mesz=2, besz=None,
          fn=shd_vec("shld", 2), feat="AVX512_VBMI2"),
        F(name="VPSHLDD", map=3, opc=0x71, w=0, layout="rvm", imm=True, mesz=4, besz=4,
          fn=shd_vec("shld", 4), feat="AVX512_VBMI2"),
        F(name="VPSHLDQ", map=3, opc=0x71, w=1, layout="rvm", imm=True, mesz=8, besz=8,
          fn=shd_vec("shld", 8), feat="AVX512_VBMI2"),
        F(name="VPSHRDW", map=3, opc=0x72, w=1, layout="rvm", imm=True, mesz=2, besz=None,
          fn=shd_vec("shrd", 2), feat="AVX512_VBMI2"),
        F(name="VPSHRDD", map=3, opc=0x73, w=0, layout="rvm", imm=True, mesz=4, besz=4,
          fn=shd_vec("shrd", 4), feat="AVX512_VBMI2"),
        F(name="VPSHRDQ", map=3, opc=0x73, w=1, layout="rvm", imm=True, mesz=8, besz=8,
          fn=shd_vec("shrd", 8), feat="AVX512_VBMI2"),
    ],
    "VBMI": [
        F(name="VPMULTISHIFTQB", opc=0x83, w=1, layout="rvm", mesz=1, besz=8, fs=False,
          fn=multishift_vec, feat="AVX512_VBMI", kind="mshift"),
    ],
    "VNNI": [
        F(name="VPDPBUSD", opc=0x50, w=0, layout="rvm", mesz=4, besz=4, dsrc=True,
          fn=vnni_vec(False, False), feat="AVX512_VNNI", kind="vnni"),
        F(name="VPDPBUSDS", opc=0x51, w=0, layout="rvm", mesz=4, besz=4, dsrc=True,
          fn=vnni_vec(False, True), feat="AVX512_VNNI", kind="vnni"),
        F(name="VPDPWSSD", opc=0x52, w=0, layout="rvm", mesz=4, besz=4, dsrc=True,
          fn=vnni_vec(True, False), feat="AVX512_VNNI", kind="vnni"),
        F(name="VPDPWSSDS", opc=0x53, w=0, layout="rvm", mesz=4, besz=4, dsrc=True,
          fn=vnni_vec(True, True), feat="AVX512_VNNI", kind="vnni"),
    ],
    "BF16": [
        F(name="VCVTNE2PS2BF16", pp=3, opc=0x72, w=0, layout="rvm", mesz=2, besz=4, fs=False,
          fn=cvtne2_vec, feat="AVX512_BF16", kind="fp32"),
        F(name="VCVTNEPS2BF16", pp=2, opc=0x72, w=0, layout="rm", mesz=2, besz=4, dfrac=2,
          fn=cvtne1_vec, feat="AVX512_BF16", kind="fp32"),
        F(name="VDPBF16PS", pp=2, opc=0x52, w=0, layout="rvm", mesz=4, besz=4, dsrc=True,
          fn=dpbf16_vec, feat="AVX512_BF16", kind="bf16dp"),
    ],
}
ORDER = ["VBMI2", "VBMI", "VNNI", "BF16"]

# (map, pp, opc) -> the EVEX.W values with a form (for the "wrong W" #UD cases)
W_FORMS = {}
for _fs in FORMS.values():
    for _f in _fs:
        W_FORMS.setdefault((_f["map"], _f["pp"], _f["opc"]), set()).add(_f["w"])

# FP32 values for the BF16 conversions: zeros, denormals, normals near rounding ties,
# overflow to infinity, infinities, QNaN / SNaN with payloads
FP32_SPECIAL = [
    0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x00400000, 0x80000001,
    0x3F800000, 0xBF800000, 0x3F808000, 0x3F818000, 0x3F807FFF, 0x3F808001,
    0x3F80FFFF, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F7F8000, 0x7F7F7FFF, 0x00800000,
    0x80800000, 0x00808000, 0x00818000, 0x7F800000, 0xFF800000, 0x7FC00000,
    0xFFC00000, 0x7F800001, 0xFF800001, 0x7FBFFFFF, 0x7FFFFFFF, 0x7FA12345,
    0xFFE54321, 0x40490FDB, 0xC2F6E979, 0x33800000, 0x01000000, 0x7F000000,
]

# BF16 values for VDPBF16PS: zeros, denormals (DAZ), small normals (FTZ outputs), normals,
# large values (overflow), infinities, NaNs
BF16_SPECIAL = [
    0x0000, 0x8000, 0x0001, 0x807F, 0x0040, 0x0080, 0x8080, 0x00C0, 0x0100, 0x1F80,
    0x3F80, 0xBF80, 0x4000, 0xC040, 0x3F81, 0x3FFF, 0x7F7F, 0xFF7F, 0x7F00, 0x7F80,
    0xFF80, 0x7FC0, 0xFFC0, 0x7F81, 0xFF81, 0x7FA0, 0x2000, 0x2080, 0x1000, 0x0F80,
]


def rnd_fp32(rng):
    r = rng.random()
    if r < 0.30:
        return rng.choice(FP32_SPECIAL)
    if r < 0.45:            # exact rounding ties / near ties
        return (rng.getrandbits(16) << 16) | rng.choice([0x8000, 0x7FFF, 0x8001, 0x0000, 0xFFFF])
    return rng.getrandbits(32)


def rnd_bf16(rng):
    r = rng.random()
    if r < 0.30:
        return rng.choice(BF16_SPECIAL)
    if r < 0.70:            # moderate exponents: products and sums stay finite and exact-ish
        return (rng.getrandbits(1) << 15) | ((rng.randrange(0x70, 0x90)) << 7) | rng.getrandbits(7)
    return rng.getrandbits(16)


def rnd_acc32(rng):
    r = rng.random()
    if r < 0.25:
        return rng.choice(FP32_SPECIAL)
    if r < 0.70:
        return (rng.getrandbits(1) << 31) | (rng.randrange(0x70, 0x90) << 23) | rng.getrandbits(23)
    return rng.getrandbits(32)


def vals_for(sp, role, n_bytes, rng=None):
    """operand bytes for a form: role 'd' (destination / accumulator), 'a' (SRC1 / vvvv),
    'b' (SRC2 / r/m)"""
    rng = rng or RNG
    kind = sp.get("kind")
    if kind == "fp32" and role in ("a", "b"):
        return pack([rnd_fp32(rng) for _ in range(n_bytes // 4)], 4)
    if kind == "bf16dp":
        if role == "d":
            return pack([rnd_acc32(rng) for _ in range(n_bytes // 4)], 4)
        return pack([rnd_bf16(rng) for _ in range(n_bytes // 2)], 2)
    if kind == "vnni":
        r = rng.random()
        if role == "d" and r < 0.3:
            return pack([rng.choice([0x7FFFFFFF, 0x80000000, 0x7FFF0000, 0x80010000, 0, 0xFFFFFFFF])
                         for _ in range(n_bytes // 4)], 4)
        if role in ("a", "b") and r < 0.3:
            return bytes(rng.choice([0x00, 0x7F, 0x80, 0xFF, 0x01]) for _ in range(n_bytes))
        return bytes(rng.getrandbits(8) for _ in range(n_bytes))
    if kind == "shift" and role == "b":
        # counts: every value modulo the width, plus values >= the width
        esz = sp["mesz"]
        bits = 8 * esz
        return pack([rng.choice([0, 1, bits - 1, bits, bits + 1, rng.getrandbits(8), rng.getrandbits(8 * esz)])
                     for _ in range(n_bytes // esz)], esz)
    r = rng.random()
    if r < 0.1:
        return bytes([0xFF]) * n_bytes
    if r < 0.15:
        return bytes(n_bytes)
    return bytes(rng.getrandbits(8) for _ in range(n_bytes))


def rnd_imm(sp):
    bits = 8 * sp["mesz"]
    return RNG.choice([0, 1, bits - 1, bits, bits + 1, 0xFF, RNG.getrandbits(8), RNG.getrandbits(8)])


def gen_case(sp, vl, title, dst, s1, s2, kreg=0, kval=None, z=0, mem=None, bcst=0,
             aimg=None, bimg=None, dimg=None, imm=None):
    """rvm / rm forms: zmm(dst){k}{z}, zmm(s1) (vvvv, rvm only), zmm(s2) / mem / {1toN}"""
    lay = sp["layout"]
    c = Case("%s VL%d %s" % (sp["name"], vl * 8, title))
    if sp["imm"] and imm is None:
        imm = rnd_imm(sp)
    if sp["imm"]:
        c.title += " imm8=0x%02X" % imm
    regs = {}
    if lay == "rvm":
        regs[s1] = (aimg if aimg is not None else vals_for(sp, "a", vl)) + rnd_bytes(64 - vl)
    if mem is None:
        if s2 not in regs:
            regs[s2] = (bimg if bimg is not None else vals_for(sp, "b", vl)) + rnd_bytes(64 - vl)
        b = regs[s2][:vl]
        rm, nn = s2, 1
    else:
        data = bimg if bimg is not None else vals_for(sp, "b", sp["besz"] if bcst else vl)
        c.mem[MEM_RSI + mem.disp] = data
        b = data * (vl // len(data)) if bcst else data
        rm, nn = mem, (sp["besz"] if bcst else vl)
    if dst not in regs:
        regs[dst] = (dimg if dimg is not None else vals_for(sp, "d", vl)) + rnd_bytes(64 - vl)
    c.zmm.update(regs)
    a = regs[s1][:vl] if lay == "rvm" else bytes(vl)
    if kreg:
        c.k[kreg] = kval
    kmask = kval if kreg else None
    d = regs[dst][:vl]
    dbytes = vl // sp["dfrac"]
    res = elems(sp["fn"](d, a, b, vl, imm), sp["mesz"])
    out = mask_merge(regs[dst], res, sp["mesz"], dbytes, kmask, z)
    c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    vvvv = s1 if lay == "rvm" else None
    c.code = evex(sp["map"], sp["pp"], sp["w"], sp["opc"], dst, rm, vvvv=vvvv, ll=VL_LL[vl], b=bcst,
                  z=z, aaa=kreg, n=nn, imm=imm if sp["imm"] else None)
    return c


def gen_form(sp):
    lay = sp["layout"]
    name = sp["name"]
    comment("%s (EVEX.%s.%s.W%d %02X /r%s), %s" % (name, ["NP", "66", "F3", "F2"][sp["pp"]],
            "0F38" if sp["map"] == 2 else "0F3A", sp["w"], sp["opc"], " ib" if sp["imm"] else "",
            "E4" if sp["fs"] else "E4NF"))
    for vl in (16, 32, 64):
        emit(gen_case(sp, vl, "nomask", 1, 2, 3))
        emit(gen_case(sp, vl, "merge zmm16+", 17, 30, 9, kreg=3, kval=RNG.getrandbits(64)))
        emit(gen_case(sp, vl, "zero", 4, 25, 26, kreg=7, kval=RNG.getrandbits(64), z=1))
        emit(gen_case(sp, vl, "zero k=0", 5, 6, 7, kreg=2, kval=0, z=1))
        emit(gen_case(sp, vl, "merge k=all", 24, 23, 22, kreg=4, kval=(1 << 64) - 1))
        emit(gen_case(sp, vl, "mem", 6, 8, 0, mem=Mem(RSI, 0x40)))
        emit(gen_case(sp, vl, "mem masked", 7, 8, 0, mem=Mem(RSI, 0x40), kreg=5, kval=RNG.getrandbits(64)))
        emit(gen_case(sp, vl, "mem zero-masked", 31, 8, 0, mem=Mem(RSI, -0x80), kreg=6,
                      kval=RNG.getrandbits(64), z=1))
        if sp["besz"]:
            emit(gen_case(sp, vl, "bcst merge", 10, 11, 0, kreg=2, kval=RNG.getrandbits(64),
                          mem=Mem(RSI, 0x40), bcst=1))
            emit(gen_case(sp, vl, "bcst nomask", 12, 13, 0, mem=Mem(RSI, -0x40), bcst=1))
            emit(gen_case(sp, vl, "bcst zero", 20, 21, 0, kreg=1, kval=RNG.getrandbits(64), z=1,
                          mem=Mem(RSI, 0x100), bcst=1))
    # destination = each source (the destination is read by the dsrc forms)
    emit(gen_case(sp, 64, "dst=src2 merge", 14, 15, 14, kreg=1, kval=RNG.getrandbits(64)))
    emit(gen_case(sp, 64, "dst=src2 nomask", 27, 28, 27))
    if lay == "rvm":
        emit(gen_case(sp, 64, "dst=src1 zero", 16, 16, 18, kreg=6, kval=RNG.getrandbits(64), z=1))
        emit(gen_case(sp, 64, "dst=src1 nomask", 29, 29, 19))
        emit(gen_case(sp, 64, "dst=src1=src2", 19, 19, 19))
        emit(gen_case(sp, 64, "src1=src2", 3, 9, 9))
    # value sweeps (several cases of random / special operands at 512 bits)
    for t in range(6):
        emit(gen_case(sp, 64, "values %d" % t, 0, 1, 2))
    if sp["imm"]:
        bits = 8 * sp["mesz"]
        for imm in sorted(set([0, 1, bits // 2, bits - 1, bits, bits + 1, 2 * bits - 1, 0x80, 0xFF])):
            emit(gen_case(sp, 32, "count", 1, 2, 3, imm=imm))
    # disp8*N: Full Mem tuple N = VL; Full tuple N = VL or the element size with {1toN}
    for vl in (16, 32, 64):
        for bcst in ((0, 1) if sp["besz"] else (0,)):
            nn = sp["besz"] if bcst else vl
            for d8 in (1, -1, 127, -128):
                emit(gen_case(sp, vl, "disp8=%d N=%d" % (d8, nn), 21, 22, 0,
                              mem=Mem(RSI, d8 * nn), bcst=bcst))
            emit(gen_case(sp, vl, "disp32 not a multiple of N", 21, 22, 0,
                          mem=Mem(RSI, nn + 1 if nn < 64 else 65), bcst=bcst))
    gen_fault_cases(sp)
    gen_ud_cases(sp)


def gen_fault_cases(sp):
    """memory operand next to the unmapped page MEM+0x10000: E4 suppresses faults of masked-off
    elements, E4NF reads the whole operand"""
    lay = sp["layout"]
    base = 0x10000 - MEM_RSI - 32
    vl = 64
    nlo = 32 // sp["mesz"]
    if sp["dfrac"] == 2:
        # VCVTNEPS2BF16: mask bit i covers source dword i (KL/2 words <- KL/2 dwords)
        nlo = 32 // 4
    for title, kval, ok in (("mapped elements only", (1 << nlo) - 1, sp["fs"]),
                            ("one unmapped element", ((1 << nlo) - 1) | (1 << nlo), False)):
        c = Case("%s VL512 [end-32] {k: %s} %s" % (sp["name"], title, "no fault" if ok else "#PF"))
        c.zmm[3] = vals_for(sp, "d", 64)
        if lay == "rvm":
            c.zmm[4] = vals_for(sp, "a", 64)
        c.k[1] = kval
        data = vals_for(sp, "b", 32)
        c.mem[MEM_RSI + base] = data
        c.code = evex(sp["map"], sp["pp"], sp["w"], sp["opc"], 3, Mem(RSI, base, disp32=True),
                      vvvv=4 if lay == "rvm" else None, ll=2, aaa=1, imm=0x05 if sp["imm"] else None)
        if not ok:
            c.fault = "#PF"
        else:
            b = data + bytes(32)        # masked-off elements: never read (their value is unused)
            a = c.zmm[4][:vl] if lay == "rvm" else bytes(vl)
            res = elems(sp["fn"](c.zmm[3][:vl], a, b, vl, 0x05), sp["mesz"])
            c.exp.append("zmm3=%s" % hexs(mask_merge(c.zmm[3], res, sp["mesz"], vl // sp["dfrac"], kval, 0)))
        emit(c)
    if sp["besz"]:
        c = Case("%s {1toN} k=0 on the unmapped page %s" % (sp["name"], "no fault" if sp["fs"] else "#PF"))
        c.zmm[3] = rnd_bytes(64)
        c.k[6] = 0
        c.code = evex(sp["map"], sp["pp"], sp["w"], sp["opc"], 3, Mem(RSI, 0x10000 - MEM_RSI, disp32=True),
                      vvvv=4 if lay == "rvm" else None, ll=2, b=1, aaa=6, imm=0 if sp["imm"] else None)
        if sp["fs"]:
            dbytes = 64 // sp["dfrac"]
            c.exp.append("zmm3=%s" % hexs(c.zmm[3][:dbytes] + bytes(64 - dbytes)))
        else:
            c.fault = "#PF"
        emit(c)


def gen_ud_cases(sp):
    lay = sp["layout"]
    name = sp["name"]
    uds = [("EVEX.b on a register form", dict(b=1)), ("L'L = 11b", dict(ll=3)),
           ("{z} with aaa = 000b", dict(z=1))]
    others = W_FORMS[(sp["map"], sp["pp"], sp["opc"])]
    if 1 - sp["w"] not in others:
        uds.append(("wrong EVEX.W", dict(w=1 - sp["w"])))
    if lay in ("rm", "cx_e", "cx_c"):
        uds += [("vvvv != 1111b", dict(vvvv=5)), ("V' = 0 (vvvv unused)", dict(p2_vp=0))]
    for pp in range(4):
        if (sp["map"], pp, sp["opc"]) not in W_FORMS and pp != sp["pp"]:
            uds.append(("pp = %s (no form)" % ["NP", "66", "F3", "F2"][pp], dict(pp=pp)))
    if not sp["besz"]:
        uds.append(("EVEX.b with memory (no {1toN})", dict(b=1, mem=True)))
    for title, kw in uds:
        c = Case("%s %s #UD" % (name, title))
        w = kw.pop("w", sp["w"])
        ll = kw.pop("ll", 2)
        pp = kw.pop("pp", sp["pp"])
        rm = Mem(RSI, 0x40) if kw.pop("mem", False) else 3
        if lay == "rvm":
            kw.setdefault("vvvv", 2)
        if kw.get("z") and "aaa" not in kw:
            kw["aaa"] = 0
        c.code = evex(sp["map"], pp, w, sp["opc"], 1, rm, ll=ll, imm=0 if sp["imm"] else None, n=64, **kw)
        c.fault = "#UD"
        emit(c)


# --- expand / compress -----------------------------------------------------------------
def expand(dst_old, src, esz, vl, kmask, zero):
    """VPEXPANDB/W: k := 0; FOR j: IF k1[j] OR *no writemask*: DEST[j] := SRC[k]; k++ ELSE
    merging (unchanged) / zeroing (0); DEST[MAXVL-1:VL] := 0"""
    n = vl // esz
    S = elems(src, esz)
    D = elems(dst_old[:vl], esz)
    out, k = [], 0
    for j in range(n):
        if kmask is None or (kmask >> j) & 1:
            out.append(S[k])
            k += 1
        else:
            out.append(0 if zero else D[j])
    return pack(out, esz) + bytes(64 - vl)


def compress_reg(dst_old, src, esz, vl, kmask, zero):
    """VPCOMPRESSB/W reg-reg: k := 0; FOR j: IF active: DEST[k] := SRC[j]; k++; IF merging:
    DEST[VL-1:k*esz] unchanged ELSE 0; DEST[MAXVL-1:VL] := 0"""
    n = vl // esz
    S = elems(src[:vl], esz)
    D = elems(dst_old[:vl], esz)
    out = [S[j] for j in range(n) if kmask is None or (kmask >> j) & 1]
    k = len(out)
    out += [0] * (n - k) if zero else D[k:]
    return pack(out, esz) + bytes(64 - vl)


def compress_mem(src, esz, vl, kmask):
    """VPCOMPRESSB/W store form: only the contiguous active elements are written"""
    n = vl // esz
    S = elems(src[:vl], esz)
    return pack([S[j] for j in range(n) if kmask is None or (kmask >> j) & 1], esz)


def cx_case(sp, vl, title, reg, rm, kreg=0, kval=None, z=0, disp=None, mem_old=None, srcimg=None):
    """expand: reg = destination, rm = source (register or memory at disp);
    compress: reg = source, rm = destination (register or memory at disp)"""
    esz = sp["mesz"]
    n = vl // esz
    c = Case("%s VL%d %s" % (sp["name"], vl * 8, title))
    kmask = kval if kreg else None
    if kreg:
        c.k[kreg] = kval
    active = n if kmask is None else bin(kmask & ((1 << n) - 1)).count("1")
    if sp["layout"] == "cx_e":
        c.zmm[reg] = rnd_bytes(64)
        if disp is None:
            src = srcimg if srcimg is not None else rnd_bytes(64)
            if rm != reg:
                c.zmm[rm] = src
            else:
                src = c.zmm[reg]
            out = expand(c.zmm[reg], src[:vl], esz, vl, kmask, z)
            rmop = rm
        else:
            # (only the mapped part of the source: MEM + 0x10000 is unmapped)
            data = rnd_bytes(min(active * esz, 0x10000 - MEM_RSI - disp))
            if data:
                c.mem[MEM_RSI + disp] = data
            out = expand(c.zmm[reg], data + bytes(vl - len(data)), esz, vl, kmask, z)
            rmop = Mem(RSI, disp)
        c.exp.append("zmm%d=%s" % (reg, hexs(out)))
    else:
        src = srcimg if srcimg is not None else rnd_bytes(64)
        c.zmm[reg] = src
        if disp is None:
            if rm != reg:
                c.zmm[rm] = rnd_bytes(64)
            out = compress_reg(c.zmm[rm], src, esz, vl, kmask, z)
            c.exp.append("zmm%d=%s" % (rm, hexs(out)))
            rmop = rm
        else:
            old = mem_old if mem_old is not None else rnd_bytes(vl)
            c.mem[MEM_RSI + disp] = old
            st = compress_mem(src, esz, vl, kmask)
            new = st + old[len(st):]
            if new != old:
                c.exp.append("m+0x%X=%s" % (MEM_RSI + disp, hexs(new)))
            rmop = Mem(RSI, disp)
    c.code = evex(2, 1, sp["w"], sp["opc"], reg, rmop, ll=VL_LL[vl], z=z, aaa=kreg, n=esz)
    return c


def gen_cx(sp):
    esz = sp["mesz"]
    exp_ = sp["layout"] == "cx_e"
    comment("%s (EVEX.66.0F38.W%d %02X /r), Tuple1 Scalar (N = %d), E4" % (sp["name"], sp["w"], sp["opc"], esz))
    for vl in (16, 32, 64):
        n = vl // esz
        emit(cx_case(sp, vl, "reg nomask", 1, 2))
        emit(cx_case(sp, vl, "reg merge zmm16+", 17, 30, kreg=3, kval=RNG.getrandbits(64)))
        emit(cx_case(sp, vl, "reg zero", 4, 25, kreg=7, kval=RNG.getrandbits(64), z=1))
        emit(cx_case(sp, vl, "reg merge k=0", 5, 6, kreg=1, kval=0))
        emit(cx_case(sp, vl, "reg zero k=0", 5, 6, kreg=1, kval=0, z=1))
        emit(cx_case(sp, vl, "reg merge all active", 8, 9, kreg=2, kval=(1 << 64) - 1))
        emit(cx_case(sp, vl, "reg merge only the top element", 10, 11, kreg=4, kval=1 << (n - 1)))
        emit(cx_case(sp, vl, "reg merge mask bits above KL ignored", 10, 11, kreg=4,
                     kval=(RNG.getrandbits(64) & ((1 << n) - 1)) | (((1 << 64) - 1) ^ ((1 << n) - 1))))
        emit(cx_case(sp, vl, "dst = src merge", 12, 12, kreg=5, kval=RNG.getrandbits(64)))
        emit(cx_case(sp, vl, "mem nomask", 13, None, disp=0x40))
        emit(cx_case(sp, vl, "mem masked", 14, None, kreg=6, kval=RNG.getrandbits(64), disp=0x40))
        emit(cx_case(sp, vl, "mem k=0", 14, None, kreg=6, kval=0, disp=0x40))
        if exp_:
            emit(cx_case(sp, vl, "mem zero-masked", 15, None, kreg=3, kval=RNG.getrandbits(64), z=1, disp=0x80))
        # disp8*N = element size
        for d8 in (1, -1, 127, -128):
            emit(cx_case(sp, vl, "disp8=%d N=%d" % (d8, esz), 16, None, kreg=1, kval=RNG.getrandbits(64),
                         disp=d8 * esz))
    # contiguous memory side next to the unmapped page: popcount(k1) elements only
    end = 0x10000 - MEM_RSI
    for vl in (16, 64):
        n = vl // esz
        for cnt in (n // 2, 3):
            kval = 0
            pos = sorted(RNG.sample(range(n), cnt))
            for p in pos:
                kval |= 1 << p
            disp = end - cnt * esz
            # (a compress destination image only up to the end of the mapped memory)
            c = cx_case(sp, vl, "%d active elements end at the unmapped page: no fault" % cnt, 3, None,
                        kreg=1, kval=kval, disp=disp, mem_old=rnd_bytes(cnt * esz))
            emit(c)
            c = cx_case(sp, vl, "%d active elements, one past the mapped end #PF" % (cnt + 1), 3, None,
                        kreg=1, kval=kval | (1 << [i for i in range(n) if i not in pos][0]), disp=disp,
                        mem_old=rnd_bytes(cnt * esz))
            c.exp = []
            c.fault = "#PF"
            emit(c)
    # #UD
    uds = [("vvvv != 1111b", dict(vvvv=5)), ("V' = 0", dict(p2_vp=0)), ("EVEX.b (register form)", dict(b=1)),
           ("EVEX.b with memory", dict(b=1, mem=True)), ("L'L = 11b", dict(ll=3)),
           ("{z} with aaa = 000b", dict(z=1))]
    if not exp_:
        uds.append(("{z} with a memory destination", dict(z=1, aaa=1, mem=True)))
    for pp in (0, 2, 3):
        uds.append(("pp = %s" % ["NP", "66", "F3", "F2"][pp], dict(pp=pp)))
    for title, kw in uds:
        c = Case("%s %s #UD" % (sp["name"], title))
        ll = kw.pop("ll", 2)
        pp = kw.pop("pp", 1)
        rm = Mem(RSI, 0x40) if kw.pop("mem", False) else 3
        c.code = evex(2, pp, sp["w"], sp["opc"], 1, rm, ll=ll, n=esz, **kw)
        c.fault = "#UD"
        emit(c)


def gen_special():
    """targeted value cases beyond the random sweeps"""
    comment("--- targeted values")
    # VPSHLD/VPSHRD and V forms: every count 0..w-1 on a fixed pattern at 128 bits
    for sp in FORMS["VBMI2"]:
        if sp["layout"] != "rvm":
            continue
        esz = sp["mesz"]
        bits = 8 * esz
        n = 16 // esz
        for start in range(0, bits, n):
            if sp["imm"]:
                for cnt in (start, start + n - 1):
                    emit(gen_case(sp, 16, "pattern count %d" % cnt, 1, 2, 3, imm=cnt,
                                  aimg=pack([0x8421_0FED_CBA9_8765 & ((1 << bits) - 1)] * n, esz),
                                  bimg=pack([0x1357_9BDF_0246_8ACE & ((1 << bits) - 1)] * n, esz)))
            else:
                cnts = [(start + i) for i in range(n)]
                emit(gen_case(sp, 16, "pattern counts %d..%d" % (cnts[0], cnts[-1]), 1, 2, 3,
                              dimg=pack([0x8421_0FED_CBA9_8765 & ((1 << bits) - 1)] * n, esz),
                              aimg=pack([0x1357_9BDF_0246_8ACE & ((1 << bits) - 1)] * n, esz),
                              bimg=pack([c_ + bits * (i & 1) for i, c_ in enumerate(cnts)], esz)))
    # VPMULTISHIFTQB: every control 0..63 (and with high bits set) on two qwords
    sp = FORMS["VBMI"][0]
    for start in range(0, 64, 16):
        ctrl = bytes([(start + i) | (0x40 * (i & 3)) for i in range(16)])
        emit(gen_case(sp, 16, "controls %d..%d" % (start, start + 15), 1, 2, 3, aimg=ctrl,
                      bimg=pack([0x0123456789ABCDEF, 0xF0E1D2C3B4A59687], 8)))
    # VNNI: saturation in both directions, wrap of the non-saturating forms
    for sp in FORMS["VNNI"]:
        words = sp["name"].startswith("VPDPW")
        if words:
            a = pack([0x8000, 0x8000, 0x7FFF, 0x7FFF, 0x8000, 0x7FFF, 0xFFFF, 0x0001], 2)
            b = pack([0x8000, 0x8000, 0x7FFF, 0x7FFF, 0x7FFF, 0x8000, 0xFFFF, 0xFFFF], 2)
        else:
            a = bytes([0xFF] * 8 + [0xFF, 0xFF, 0x00, 0x01] + [0x80, 0x7F, 0xFF, 0x00])
            b = bytes([0x7F] * 4 + [0x80] * 4 + [0x80, 0x80, 0x7F, 0xFF] + [0x80, 0x7F, 0x80, 0x01])
        for dv in (0x7FFFFFFF, 0x80000000, 0x7FFF0000, 0x80008000, 0):
            emit(gen_case(sp, 16, "saturation acc=0x%08X" % dv, 1, 2, 3, dimg=pack([dv] * 4, 4), aimg=a, bimg=b))
    # BF16 conversions: every special value in each position
    for sp in FORMS["BF16"][:2]:
        vals = FP32_SPECIAL + [0x3F7FFFFF, 0x3F7F8000, 0x3F7F7FFF, 0x477FFFFF, 0x007FFFFF]
        per = 16
        for i in range(0, len(vals), per):
            chunk = (vals[i:i + per] + [0] * per)[:per]
            emit(gen_case(sp, 64, "specials %d" % (i // per), 1, 2, 3,
                          aimg=pack(list(reversed(chunk)), 4), bimg=pack(chunk, 4)))
    # VDPBF16PS: NaN priority (Table 5-4), invalid, FTZ / DAZ, overflow, signed zeros
    sp = FORMS["BF16"][2]
    pairs = [
        # (acc, src1 dword (hi:lo bf16), src2 dword)
        (0x3F800000, 0x7FC1_7FC2, 0x7FC3_7FC4),   # src1 low NaN wins
        (0x3F800000, 0x7FC1_3F80, 0x7FC3_7FC4),   # src2 low NaN
        (0x3F800000, 0x7F81_3F80, 0x7FC3_3F80),   # src1 high NaN (signaling, quieted)
        (0x3F800000, 0x3F80_3F80, 0x7F83_3F80),   # src2 high NaN
        (0x7F812345, 0x3F80_3F80, 0x3F80_3F80),   # srcdest NaN (quieted)
        (0x7FC00001, 0x7F80_3F80, 0x0000_3F80),   # srcdest NaN over inf * 0
        (0x3F800000, 0x7F80_3F80, 0x0000_3F80),   # inf * 0 high -> indefinite
        (0x3F800000, 0x3F80_7F80, 0x3F80_8000),   # inf * -0 low -> indefinite
        (0xFF800000, 0x7F80_3F80, 0x3F80_3F80),   # +inf + -inf -> indefinite
        (0x7F800000, 0x7F80_FF80, 0x3F80_3F80),   # +inf, then +inf + -inf -> indefinite
        (0x00000001, 0x0000_0000, 0x0000_0000),   # denormal srcdest (DAZ) -> +0
        (0x80000001, 0x8000_0000, 0x0000_0000),   # -denormal acc, -0*+0 products
        (0x80000000, 0x8000_8000, 0x0000_0000),   # -0 + (-0) + (-0) -> -0
        (0x00000000, 0x0040_0040, 0x3F80_3F80),   # denormal BF16 sources (DAZ)
        (0x00800000, 0x8000_BF80, 0x0000_0080),   # 2^-126 - 2^-126 = +0
        (0x00800000, 0x0000_BF00, 0x0000_0000),   # -2^-127 * 0
        (0x00000000, 0x0080_2000, 0x3F00_1F80),   # tiny products flushed
        (0x00800000, 0x0000_BF00, 0x0000_3F00),   # 2^-126 - 2^-126*0.25: tiny -> FTZ
        (0x7F7FFFFF, 0x7F7F_0000, 0x4000_0000),   # overflow -> +inf
        (0x3F800000, 0x3F80_3F80, 0x3380_3380),   # 1 + 2^-24 * 1 ... RNE ties
        (0x4B800000, 0x3F80_3F80, 0x3F80_3F80),   # 2^24 + 1 + 1
        (0x4B800000, 0x0000_3F80, 0x0000_3F80),   # 2^24 + 1: tie to even
        (0x4B800001, 0x0000_3F80, 0x0000_3F80),   # (2^24 + 2) + 1: tie to even (up)
    ]
    for t in range(0, len(pairs), 4):
        grp = (pairs[t:t + 4] + [(0, 0, 0)] * 4)[:4]
        emit(gen_case(sp, 16, "specials %d" % (t // 4), 1, 2, 3, dimg=pack([g[0] for g in grp], 4),
                      aimg=pack([g[1] for g in grp], 4), bimg=pack([g[2] for g in grp], 4)))
    # VDPBF16PS FTZ boundary: (2^24 - 1) * 2^-150 = 2^-126 - 2^-150 is exact (tiny -> 0)
    # 2^-126 * (1 - 2^-24): acc = 2^-126 (00800000), product = -2^-150 = -(2^-75 * 2^-75)
    emit(gen_case(sp, 16, "FTZ: 2^-126 - 2^-150 (tiny, exact) -> +0", 1, 2, 3,
                  dimg=pack([0x00800000] * 4, 4), aimg=pack([0x0000_1A00] * 4, 4),
                  bimg=pack([0x0000_9A00] * 4, 4)))
    # just below 2^-126 by less than half an ulp of 2^-126: rounds up to 2^-126 (not tiny)
    emit(gen_case(sp, 16, "2^-126 - 2^-152 rounds to 2^-126 (not tiny)", 1, 2, 3,
                  dimg=pack([0x00800000] * 4, 4), aimg=pack([0x0000_1980] * 4, 4),
                  bimg=pack([0x0000_9A00] * 4, 4)))


def gen_all():
    del out_lines[:]
    RNG.seed(SEED)
    for fam in ORDER:
        comment("--- AVX512_%s (ledger U550-U557), --avx512 (every UC_X86_AVX512_* bit) or --avx10 1" % fam)
        for sp in FORMS[fam]:
            if sp["layout"] in ("cx_e", "cx_c"):
                gen_cx(sp)
            else:
                gen_form(sp)
    gen_special()


# ---------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    # encodings (Vol2A Table 2-32); VPSHLDQ zmm1, zmm2, zmm3, 5 = 62 F3 ED 48 71 CB 05
    chk("enc vpshldq", evex(3, 1, 1, 0x71, 1, 3, vvvv=2, ll=2, imm=5),
        bytes([0x62, 0xF3, 0xED, 0x48, 0x71, 0xCB, 0x05]))
    # VPDPBUSD zmm1, zmm2, zmm3 = 62 F2 6D 48 50 CB
    chk("enc vpdpbusd", evex(2, 1, 0, 0x50, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF2, 0x6D, 0x48, 0x50, 0xCB]))
    # VCVTNEPS2BF16 ymm1, zmm2 = 62 F2 7E 48 72 CA
    chk("enc vcvtneps2bf16", evex(2, 2, 0, 0x72, 1, 2, ll=2), bytes([0x62, 0xF2, 0x7E, 0x48, 0x72, 0xCA]))
    # VPCOMPRESSB [rsi+0x40]{k1}, zmm1 (N = 1: disp8 = 0x40)
    chk("enc vpcompressb", evex(2, 1, 0, 0x63, 1, Mem(RSI, 0x40), ll=2, aaa=1, n=1),
        bytes([0x62, 0xF2, 0x7D, 0x49, 0x63, 0x4E, 0x40]))
    # shifts (hand-derived)
    chk("shldw", vpshld(0x1234, 0xABCD, 4, 16), 0x234A)
    chk("shldw 0", vpshld(0x1234, 0xABCD, 16, 16), 0x1234)        # 16 & 15 = 0
    chk("shldw 17", vpshld(0x1234, 0xABCD, 17, 16), 0x2469)       # 0x2468 | 1
    chk("shrdw", vpshrd(0x1234, 0xABCD, 4, 16), 0xD123)
    chk("shrdd 31", vpshrd(0x80000000, 0x00000001, 31, 32), 0x00000003)
    chk("shldq 63", vpshld(1, 0x8000000000000000, 63, 64), 0xC000000000000000)
    chk("shldvd", vpshldv(0x80000001, 0xC0000000, 33, 32), 0x00000003)   # count 33 & 31 = 1
    chk("shrdvq", vpshrdv(0x10, 0x1, 4, 64), 0x1000000000000001)
    # VPMULTISHIFTQB: ctrl 60 of 0x0123456789ABCDEF: bits 60..63 = 0, bits 0..3 = F -> F0
    chk("mshift 0", multishift_byte(0, 0x0123456789ABCDEF), 0xEF)
    chk("mshift 4", multishift_byte(4, 0x0123456789ABCDEF), 0xDE)
    chk("mshift 60", multishift_byte(60, 0x0123456789ABCDEF), 0xF0)
    chk("mshift 64+8", multishift_byte(0x48, 0x0123456789ABCDEF), 0xCD)
    # VNNI
    chk("busd", vnni_bytes(0, 0xFFFFFFFF, 0x80808080, False), (4 * 255 * -128) & 0xFFFFFFFF)
    chk("busds+", vnni_bytes(0x7FFFFFFF, 0x01, 0x01, True), 0x7FFFFFFF)
    chk("busd wrap", vnni_bytes(0x7FFFFFFF, 0x01, 0x01, False), 0x80000000)
    chk("wssds-", vnni_words(0x80000000, 0x00018000, 0x00017FFF, True), 0x80000000)
    chk("wssd", vnni_words(5, 0x80008000, 0x80008000, False), 5 + 2 * 0x40000000)
    # BF16 conversion (pseudocode)
    chk("cvt 1.0", cvt_fp32_to_bf16(0x3F800000), 0x3F80)
    chk("cvt tie even", cvt_fp32_to_bf16(0x3F808000), 0x3F80)
    chk("cvt tie odd", cvt_fp32_to_bf16(0x3F818000), 0x3F82)
    chk("cvt above tie", cvt_fp32_to_bf16(0x3F808001), 0x3F81)
    chk("cvt denormal", cvt_fp32_to_bf16(0x807FFFFF), 0x8000)
    chk("cvt overflow", cvt_fp32_to_bf16(0x7F7FFFFF), 0x7F80)
    chk("cvt snan", cvt_fp32_to_bf16(0x7F800001), 0x7FC0)
    chk("cvt nan payload", cvt_fp32_to_bf16(0xFFA12345), 0xFFE1)
    chk("cvt inf", cvt_fp32_to_bf16(0xFF800000), 0xFF80)
    # FMA step
    chk("fma 1+1*1", fma32_daz_ftz(0x3F800000, 0x3F800000, 0x3F800000), 0x40000000)
    chk("fma nan order x", fma32_daz_ftz(0x7FC00003, 0x7F800001, 0x7FC00002), 0x7FC00001)
    chk("fma nan order acc", fma32_daz_ftz(0x7F800003, 0x3F800000, 0x3F800000), 0x7FC00003)
    chk("fma inf*0", fma32_daz_ftz(0, 0x7F800000, 0), QNAN_INDEFINITE)
    chk("fma inf-inf", fma32_daz_ftz(0xFF800000, 0x7F800000, 0x3F800000), QNAN_INDEFINITE)
    chk("fma daz acc", fma32_daz_ftz(0x00000001, 0, 0), 0)
    chk("fma -0", fma32_daz_ftz(0x80000000, 0x80000000, 0), 0x80000000)
    chk("fma ftz exact tiny", fma32_daz_ftz(0x00800000, 0x1A000000, 0x9A000000), 0)
    chk("fma rounds to min normal", fma32_daz_ftz(0x00800000, 0x19800000, 0x9A000000), 0x00800000)
    chk("fma 2^24+1 tie", fma32_daz_ftz(0x4B800000, 0x3F800000, 0x3F800000), 0x4B800000)
    chk("fma 2^24+2+1 tie", fma32_daz_ftz(0x4B800001, 0x3F800000, 0x3F800000), 0x4B800002)
    chk("fma overflow", fma32_daz_ftz(0x7F7FFFFF, 0x7F7F0000, 0x40000000), 0x7F800000)
    chk("fma cancel", fma32_daz_ftz(0x00800000, 0x80800000, 0x3F800000), 0)
    # VDPBF16PS element: high pair first (NaN priority low over high)
    chk("dp nan low wins", vdpbf16ps_elem(0x3F800000, 0x7FC17FC2, 0x7FC37FC4), 0x7FC20000)
    # 1 + 2*3 (high pair) + 4*8 (low pair) = 39
    chk("dp 1+2*3+4*8", vdpbf16ps_elem(0x3F800000, 0x40004080, 0x40404100), 0x421C0000)
    return ok


# ---------------------------------------------------------------------------------------
# hardware cross-check (the i5-13600K has AVX-VNNI (VEX), SHLD/SHRD, ROR and FMA3, no AVX-512)
# ---------------------------------------------------------------------------------------
def vex3(mmmmm, pp, w, L, opc, reg, vvvv, rm):
    """VEX (C4) with registers < 8: R X B = 1"""
    b1 = 0xE0 | mmmmm
    b2 = (w << 7) | (((~vvvv) & 0xF) << 3) | (L << 2) | pp
    return bytes([0xC4, b1, b2, opc, 0xC0 | (reg << 3) | rm])


def hw_items():
    rng = random.Random(0x13600 + 550)
    items = []
    # VNNI (VEX.66.0F38.W0 50-53, AVX-VNNI): the EVEX element function on the same data
    vnni = [("VPDPBUSD", 0x50, False, False), ("VPDPBUSDS", 0x51, False, True),
            ("VPDPWSSD", 0x52, True, False), ("VPDPWSSDS", 0x53, True, True)]
    sp_v = {"kind": "vnni"}
    for name, opc, words, sat in vnni:
        for t in range(60):
            L = t & 1
            vl = 32 if L else 16
            d = vals_for(sp_v, "d", vl, rng)
            a = vals_for(sp_v, "a", vl, rng)
            b = vals_for(sp_v, "b", vl, rng)
            fn = vnni_vec(words, sat)
            res = fn(d, a, b, vl, None)
            fields = "xmm0=%s xmm1=%s xmm2=%s" % (hexs(d[:16]), hexs(a[:16]), hexs(b[:16]))
            if L:
                fields += " ymmh0=%s ymmh1=%s ymmh2=%s" % (hexs(d[16:]), hexs(a[16:]), hexs(b[16:]))
            items.append((byte_list(vex3(2, 1, 0, L, opc, 0, 1, 2)) + " | " + fields,
                          {"kind": name, "xmm0": hexs(res[:16]), "ymmh0": hexs(res[16:32]) if L else None}))
    # SHLD / SHRD r/m, reg, imm8 (count < width): DEST = r/m = rax (the half shifted), SRC = rcx
    for bits, reg_a, reg_c in ((16, "ax", "cx"), (32, "eax", "ecx"), (64, "rax", "rcx")):
        for t in range(48):
            x, y = rng.getrandbits(bits), rng.getrandbits(bits)
            cnt = t % bits if t < bits else rng.randrange(bits)
            for op in ("shld", "shrd"):
                model = vpshld(x, y, cnt, bits) if op == "shld" else vpshrd(x, y, cnt, bits)
                # (cmp edx, edx: defined flags - OF of SHLD/SHRD is undefined for counts != 1)
                items.append(("%s %s, %s, %d; cmp edx, edx | rax=0x%X rcx=0x%X" % (op, reg_a, reg_c, cnt, x, y),
                              {"kind": "%s%d" % (op.upper(), bits), "rax": model, "bits": bits}))
    # ROR r64, cl: low byte = VPMULTISHIFTQB byte for ctrl = cl
    for t in range(64):
        q = rng.getrandbits(64)
        items.append(("ror rax, cl; cmp edx, edx | rax=0x%X rcx=0x%X" % (q, t),
                      {"kind": "ROR64", "rax_lo8": multishift_byte(t, q)}))
    # VFMADD231SS xmm0, xmm1, xmm2 with MXCSR 9FC0h (FTZ, DAZ, RNE, all masked): xmm0 = acc,
    # xmm1 * xmm2 = make_fp32(bf16) values; at most one NaN operand (x86 FMA orders NaNs by
    # operand position, VDPBF16PS by Table 5-4)
    fixed = [
        (0x00800000, 0x1A000000, 0x9A000000),   # 2^-126 - 2^-150: exact, tiny -> +0 (FTZ)
        (0x00800000, 0x19800000, 0x9A000000),   # 2^-126 - 2^-151 rounds to 2^-126: not tiny
        (0x00800000, 0x1A000000, 0x99800000),
        (0x00800001, 0x1A000000, 0x9A000000),   # 2^-126 + 2^-150: tie -> 2^-126
        (0x80800000, 0x1A000000, 0x1A000000),   # -2^-126 + 2^-150 -> -0
        (0x00FFFFFF, 0x1A000000, 0x9A000000),
        (0x00000001, 0x3F800000, 0x3F800000),   # denormal acc (DAZ) + 1
        (0x3F800000, 0x00400000, 0x7F000000),   # denormal x (DAZ): 1 + 0 * 2^127
        (0x00000000, 0x20000000, 0x1F800000),   # 2^-63 * 2^-64 = 2^-127: tiny -> +0
        (0x80000000, 0xA0000000, 0x1F800000),   # -2^-127 + -0 -> -0
        (0x00000000, 0x20800000, 0x1F800000),   # 2^-62 * 2^-64 = 2^-126: normal
        (0x7F7FFFFF, 0x7F7F0000, 0x3F800000),   # overflow -> +inf
        (0xFF7FFFFF, 0x7F7F0000, 0xBF800000),   # -overflow -> -inf
        (0x4B800000, 0x3F800000, 0x3F800000),   # 2^24 + 1: tie to even (down)
        (0x4B800001, 0x3F800000, 0x3F800000),   # 2^24 + 2 + 1: tie to even (up)
        (0x3F800000, 0x7F800000, 0x00000000),   # inf * 0 -> QNaN indefinite
        (0xFF800000, 0x7F800000, 0x3F800000),   # inf - inf -> QNaN indefinite
        (0x7FA00000, 0x3F800000, 0x3F800000),   # SNaN acc quieted
        (0x3F800000, 0xFF810000, 0x3F800000),   # SNaN x quieted
    ]
    for t in range(240 + len(fixed)):
        x = rnd_bf16(rng) << 16
        y = rnd_bf16(rng) << 16
        acc = rnd_acc32(rng)
        if t >= 240:
            acc, x, y = fixed[t - 240]
        nans = [v for v in (x, y, acc) if f32_is_nan(v)]
        if len(nans) > 1:
            continue
        code = "ldmxcsr [rsi]; vfmadd231ss xmm0, xmm1, xmm2"
        fields = "m+0x8000=C09F0000 xmm0=%s xmm1=%s xmm2=%s" % (
            hexs(acc.to_bytes(4, "little")), hexs(x.to_bytes(4, "little")), hexs(y.to_bytes(4, "little")))
        items.append((code + " | " + fields, {"kind": "FMA32_DAZ_FTZ", "r": fma32_daz_ftz(acc, x, y),
                                              "in": [acc, x, y]}))
    return items


def hwcheck_gen(out):
    out.write("# VEX AVX-VNNI / SHLD / SHRD / ROR / VFMADD231SS (MXCSR 9FC0h) hardware cases: the parts of\n")
    out.write("# the AVX512_VBMI2 / VBMI / VNNI / BF16 model (ledger U550-U557) the i5-13600K can run.\n")
    out.write("# Generated by Emulator/tools/isa/ref_evex_m4a.py --hwgen (do not edit). Hardware cases (no\n")
    out.write("# '=>'): Unicorn must equal the CPU (test.cmd hw_zero); ref_evex_m4a.py --hwcmp LOG compares\n")
    out.write("# the CPU's results with the model.\n")
    for line, e in hw_items():
        out.write(line + "\n")


def hwcheck_cmp(log_path):
    import re
    exp = [e for line, e in hw_items()]
    cur, bad, seen = None, 0, 0
    inp = {}
    per = {}
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] \S+ [^|]*\| (.*)$", line)
        if m:
            cur = int(m.group(1))
            inp = dict(x.split("=", 1) for x in m.group(2).split() if "=" in x)
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if m and cur is not None:
            fields = m.group(1)
            kv = dict(x.split("=", 1) for x in fields.split() if "=" in x)
            e = exp[cur]
            k = e["kind"]
            seen += 1
            if fields.startswith("fault") or "#" in fields.split()[0] if fields.split() else False:
                ok = False
            elif k.startswith("VPDP"):
                gx = kv.get("xmm0", inp.get("xmm0", "")).upper()
                gh = kv.get("ymmh0", inp.get("ymmh0", "")).upper()
                ok = gx == e["xmm0"] and (e["ymmh0"] is None or gh == e["ymmh0"])
            elif k.startswith("SHLD") or k.startswith("SHRD"):
                got = int(kv.get("rax", inp.get("rax", "0")), 0)
                ok = (got & ((1 << e["bits"]) - 1)) == e["rax"]
            elif k == "ROR64":
                got = int(kv.get("rax", inp.get("rax", "0")), 0)
                ok = (got & 0xFF) == e["rax_lo8"]
            else:
                gx = kv.get("xmm0", inp.get("xmm0", ""))
                got = int.from_bytes(bytes.fromhex(gx[:8]), "little") if gx else -1
                ok = got == e["r"]
                if not ok and bad < 20:
                    print("   in acc/x/y = %s" % ["%08X" % v for v in e["in"]])
            per.setdefault(k, [0, 0])
            per[k][0] += 1
            if not ok:
                per[k][1] += 1
                bad += 1
                if bad <= 20:
                    print("[%d] %s: hw %s | model %s" % (cur, k, fields, e))
            cur = None
    for k in sorted(per):
        print("  %-16s %4d cases, %d differ" % (k, per[k][0], per[k][1]))
    print("hwcheck: %d cases compared (%d generated), %d differ from the model" % (seen, len(exp), bad))
    return bad == 0 and seen == len(exp)


def main():
    if "--hwgen" in sys.argv:
        hwcheck_gen(sys.stdout)
        return
    if "--hwcmp" in sys.argv:
        sys.exit(0 if hwcheck_cmp(sys.argv[sys.argv.index("--hwcmp") + 1]) else 1)
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_all()
        out = sys.stdout
        out.write("# AVX512_VBMI2, VPMULTISHIFTQB (AVX512_VBMI), AVX512_VNNI and AVX512_BF16 (ledger\n")
        out.write("# U550-U557): expected values from the independent SDM model\n")
        out.write("# Emulator/tools/isa/ref_evex_m4a.py --cases (regenerate, do not edit). The i5-13600K has\n")
        out.write("# no AVX-512: expected-value cases only, run with AVX-512 enabled (every bit) or AVX10.1:\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m4a.txt --avx512 --expect-only\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m4a.txt --avx10 1 --expect-only\n")
        out.write("# RSI = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).\n")
        for l in out_lines:
            out.write(l + "\n")
        return
    print(__doc__)


if __name__ == "__main__":
    main()
