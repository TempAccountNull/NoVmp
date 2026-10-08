#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_avx10_b.py -- independent reference model (Python 3 stdlib only) of the AVX10.2
instructions of the avx10_b work (ledger U400-U412) and generator of the expected-value
case file Emulator/data/cases_avx10_b.txt.

Written from the Intel documents only (AVX10.2 architecture specification 361050-007:
chapters 3.2, 4, 5.1, 9, 10, 13; ISE 319433-062 MOVRS; SDM Vol2A 2.7 for EVEX), not from
any C implementation:

  FP8 (3.2, Table 3.5/3.6)  BF8 = E5M2 (bias 15, max 57344, Inf S.11111.00), HF8 = E4M3
                            (bias 7, max 448, no Inf, NaN S.1111.111). The values are
                            exact rationals (fractions.Fraction); the RNE forms round to
                            nearest-even on the FP8 grid with gradual underflow; the BIAS
                            forms add the 8-bit bias below the LSB and truncate (9.3.2:
                            "added to the 8 bits below the LSB of the FP16 numbers and then
                            ... truncation"); HF8 bias forms shift the bias right by 1 for
                            normal inputs (9.3.2, 5.1 convert_fp16_to_hf8_bias).
  VCVTPH2BF8[S] VCVTPH2HF8[S]       (0F38/MAP5 74/18/1B F3)  one source, VL/2 result bytes
  VCVT2PH2BF8[S] VCVT2PH2HF8[S]     (0F38/MAP5 74/18/1B F2)  two sources, VL result bytes
  VCVTBIASPH2BF8[S] VCVTBIASPH2HF8[S] (0F38/MAP5 74/18/1B NP) bias = src1.byte[2i]
  VCVTHF82PH        (MAP5 F2 1E)     exact E4M3 -> FP16
  VCVT2PS2PHX       (0F38 66 67)     IEEE FP32 -> FP16 per MXCSR.RC / {er}, DAZ, no FTZ,
                                     flags as if masked, never #XM
  VPDPB[SS,SU,UU]D[S] (0F38 50/51)   via ref_vnni_ifma_ne.vpdpb (the VEX model, U85)
  VPDPW[SU,US,UU]D[S] (0F38 D2/D3)   via ref_vnni_ifma_ne.vpdpw (U86)
  VDPPHPS           (0F38 NP 52)     fma32 (DAZ=FTZ=1, RNE) of ref_amx (U178): odd pair first
  VMPSADBW          (0F3A F3 42)     spec 10.2 emulate_vmpsadbw, lanes 2/3 reuse imm8
  VMOVRSB/W/D/Q     (MAP5 F2/F3 6F)  masked vector load, memory only, 64-bit mode
  VMOVD / VMOVW     (0F F3 7E, 66 D6; MAP5 F3 6E/7E) zero-extending copies
  VSM4KEY4 / VSM4RNDS4 (0F38 F3/F2 DA, EVEX) via ref_sha512_sm (the VEX model, U84), 512 bits

Generic EVEX wrappers as in ref_evex_m1.py (MASK, BCST, DEST[MAXVL-1:VL] := 0), plus:
  the FP8 down-conversions mask per destination byte (KL = VL/8) and zero above the bytes
  written; VCVT2PS2PHX masks per FP16 word; classes E4NF / E9NF (FP8, VMPSADBW, VMOVD/W,
  VCVT2PS2PHX: "does not support memory fault suppression") fault on any element of the
  memory operand; E4 (VCVTHF82PH, VNNI, VDPPHPS, VMOVRS) suppress faults of masked-off ones.

Usage:
  python ref_avx10_b.py --selftest          hand-derived checks + exhaustive FP16 checks of the
                                            FP8 model against a literal transcription of the
                                            spec 5.1 pseudocode; exit 0 on pass
  python ref_avx10_b.py --cases             Emulator/data/cases_avx10_b.txt (stdout)
  python ref_avx10_b.py --hwgen EXP.json    hardware cases (VEX AVX-VNNI on the host) + expected
  python ref_avx10_b.py --hwcmp LOG EXP.json compare the host results with this model
"""

import os
import random
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ref_evex_m1 import (Mem, evex, byte_list, hexs, elems, pack, Case, Fmt, classify,  # noqa: E402
                         encode_value, F32, MEM_RSI, RSI, IE, DE, ZE, OE, UE, PE)
import ref_vnni_ifma_ne as vnni  # noqa: E402
import ref_amx  # noqa: E402
import ref_sha512_sm as sm  # noqa: E402

RNG = random.Random(0xA10B_2026)
VL_LL = {16: 0, 32: 1, 64: 2}
F16 = Fmt(16, 11, 5)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


# ======================================================================================
# FP16 / FP8 numerics (exact rationals)
# ======================================================================================
def f16_fields(h):
    return (h >> 15) & 1, (h >> 10) & 0x1F, h & 0x3FF


def f16_class(h):
    s, e, m = f16_fields(h)
    if e == 0x1F:
        return ("inf" if m == 0 else "nan"), s, None
    if e == 0:
        v = Fraction(m, 1 << 24)
    else:
        v = Fraction(1024 + m, 1 << 10) * Fraction(2) ** (e - 15)
    return ("zero" if v == 0 else "fin"), s, v


def floor_log2(q):
    n, d = q.numerator, q.denominator
    e = n.bit_length() - d.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    if Fraction(2) ** (e + 1) <= q:
        e += 1
    return e


class FP8:
    """an OFP8 format: E5M2 (BF8) or E4M3 (HF8)"""

    def __init__(self, name, ebits, mbits, bias, maxval):
        self.name, self.ebits, self.mbits, self.bias, self.maxval = name, ebits, mbits, bias, maxval
        self.emin = 1 - bias

    def quantum(self, q):
        """grid spacing at magnitude q (> 0); subnormal spacing below 2^emin"""
        e = max(floor_log2(q), self.emin)
        return Fraction(2) ** (e - self.mbits)

    def encode(self, val):
        """bit pattern (sign 0) of an exact grid value 0 <= val <= maxval"""
        if val == 0:
            return 0
        if val < Fraction(2) ** self.emin:
            cnt = val / (Fraction(2) ** (self.emin - self.mbits))
            assert cnt.denominator == 1
            return int(cnt)
        e = floor_log2(val)
        frac = val / (Fraction(2) ** (e - self.mbits)) - (1 << self.mbits)
        assert frac.denominator == 1
        return ((e + self.bias) << self.mbits) | int(frac)


BF8 = FP8("BF8", 5, 2, 15, Fraction(57344))
HF8 = FP8("HF8", 4, 3, 7, Fraction(448))


def rne_int(t):
    fl = t.numerator // t.denominator
    rem = t - fl
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl & 1):
        fl += 1
    return fl


def fp8_overflow(fmt, sign, sat):
    if fmt is BF8:
        return (sign << 7) | (0x7B if sat else 0x7C)       # E5M2_MAX or Inf
    return (sign << 7) | (0x7E if sat else 0x7F)           # E4M3_MAX or NaN


def fp8_special(h, fmt, sat):
    """Inf / NaN inputs (Table 3.6, 9.1.2): None for finite inputs"""
    cls, s, _ = f16_class(h)
    if cls == "inf":
        return fp8_overflow(fmt, s, sat)                    # Inf: max (S) / Inf (BF8) / NaN (HF8)
    if cls == "nan":
        if fmt is BF8:
            return (h >> 8) | 0x02                          # NaN: QNaN, upper payload kept
        return (s << 7) | 0x7F
    return None


def cvt_fp16_to_fp8(h, fmt, sat):
    """VCVT[2]PH2[B,H]F8[S]: RNE on the FP8 grid (no DAZ, no FTZ); overflow (result above
    the format's maximum) -> max (saturating) or Inf (BF8) / NaN (HF8)"""
    sp = fp8_special(h, fmt, sat)
    if sp is not None:
        return sp
    _, s, v = f16_class(h)
    a = -v if v < 0 else v
    if a == 0:
        return s << 7
    qt = fmt.quantum(a)
    r = rne_int(a / qt) * qt
    if r > fmt.maxval:
        return fp8_overflow(fmt, s, sat)
    return (s << 7) | fmt.encode(r)


def cvt_fp16_to_fp8_bias(h, b, fmt, sat):
    """VCVTBIASPH2[B,H]F8[S]: |x| + bias below the destination LSB, then truncation.
    BF8: b/256 of an E5M2 LSB = b FP16 ulps. HF8: normal inputs (FP16 exponent > 8) add
    (b >> 1) FP16 ulps (an E4M3 LSB is 128 ulps there); inputs below 2^-6 (HF8 denormal
    range, FP16 exponent field <= 8, FP16 denormals included) add b/256 of the HF8 denormal
    LSB 2^-9 exactly, i.e. b * 2^-17. The truncated value overflows when it is above the
    format's maximum (the next grid value: 65536 for BF8, 480 for HF8)."""
    sp = fp8_special(h, fmt, sat)
    if sp is not None:
        return sp
    _, s, v = f16_class(h)
    a = -v if v < 0 else v
    e16 = (h >> 10) & 0x1F
    ulp16 = Fraction(2) ** (max(e16, 1) - 25)
    if fmt is BF8:
        t = a + b * ulp16
    elif e16 <= 8:
        t = a + b * Fraction(1, 1 << 17)
    else:
        t = a + (b >> 1) * ulp16
    if t == 0:
        return s << 7
    qt = fmt.quantum(t)
    r = (t / qt).numerator // (t / qt).denominator * qt
    if r > fmt.maxval:
        return fp8_overflow(fmt, s, sat)
    return (s << 7) | fmt.encode(r)


def cvt_hf8_to_fp16(x):
    """VCVTHF82PH: exact; S.1111.111 (NaN) -> FP16 NaN with the 3 mantissa bits on top"""
    s, e, m = (x >> 7) & 1, (x >> 3) & 0xF, x & 7
    if e == 0xF and m == 7:
        return (s << 15) | 0x7C00 | (m << 7)
    if e == 0:
        v = Fraction(m, 1 << 9)
    else:
        v = Fraction(8 + m, 8) * Fraction(2) ** (e - 7)
    if v == 0:
        return s << 15
    eb = floor_log2(v)
    if eb < -14:
        cnt = v / Fraction(1, 1 << 24)
        return (s << 15) | int(cnt)
    frac = v / (Fraction(2) ** (eb - 10)) - 1024
    assert frac.denominator == 1
    return (s << 15) | ((eb + 15) << 10) | int(frac)


def cvt_fp32_to_fp16(x, mxcsr, rc=None):
    """VCVT2PS2PHX element: IEEE binary32 -> binary16 with MXCSR.RC (or {er}), MXCSR.DAZ
    for the input, no FTZ; returns (bits, flags)"""
    if rc is None:
        rc = (mxcsr >> 13) & 3
    daz = bool(mxcsr & 0x40)
    cls, s, v = classify(x, F32)
    sign = s << 15
    if cls in ("qnan", "snan"):
        fl = IE if cls == "snan" else 0
        return sign | 0x7C00 | 0x200 | ((x >> 13) & 0x3FF), fl
    if cls == "inf":
        return sign | 0x7C00, 0
    if cls == "zero":
        return sign, 0
    flags = 0
    if cls == "denorm":
        if daz:
            return sign, 0
        flags |= DE
    bits, fl = encode_value(v, F16, rc, False)
    return bits, flags | fl


# --------------------------------------------------------------------------------------
# literal transcription of the spec 5.1 pseudocode (bit operations), used by --selftest
# only, to cross-check the rational model above over every FP16 input
# --------------------------------------------------------------------------------------
def pc_f16_inf(x):
    return (x & 0x7FFF) == 0x7C00


def pc_f16_nan(x):
    return (x & 0x7C00) == 0x7C00 and (x & 0x3FF) != 0


def pc_bf8(x, sat):
    if pc_f16_inf(x):
        return ((x >> 8) & 0x80) | 0x7B if sat else x >> 8
    if pc_f16_nan(x):
        return (x >> 8) | 2
    lsb = (x >> 8) & 1
    temp = (x + 0x7F + lsb) & 0xFFFF
    if ((temp >> 8) & 0x7F) == 0x7C and sat:
        return ((temp >> 8) & 0x80) | 0x7B
    return temp >> 8


def pc_bf8_bias(x, b, sat):
    if pc_f16_inf(x):
        return ((x >> 8) & 0x80) | 0x7B if sat else x >> 8
    if pc_f16_nan(x):
        return (x >> 8) | 2
    temp = (x + b) & 0xFFFF
    if ((temp >> 8) & 0x7F) == 0x7C and sat:
        return ((temp >> 8) & 0x80) | 0x7B
    return temp >> 8


def pc_hf8(x, sat):
    rb = 8
    sign = (x & 0x8000) >> 8
    e16, m16 = (x & 0x7C00) >> 10, x & 0x3FF
    if pc_f16_inf(x):
        e, m = 0xF, 6 if sat else 7
    elif pc_f16_nan(x):
        e, m = 0xF, 7
    elif e16 > rb + 15 or (e16 == rb + 15 and m16 > 0x340):
        e, m = 0xF, 6 if sat else 7
    elif e16 == 0 and m16 == 0:
        e, m = 0, 0
    elif e16 <= rb:
        m = m16 | 0x400
        m >>= rb + 1 - e16
        m |= ((m16 & 0x7F) + 0x7F) >> 7
        m = m + 0x3F + ((m >> 7) & 1)
        e = m >> 10
        m = (m >> 7) & 7
    else:
        rne = x + 0x3F + ((m16 >> 7) & 1)
        e = ((rne & 0x7C00) >> 10) - rb
        m = (rne & 0x3FF) >> 7
    return sign | (e << 3) | m


def pc_hf8_bias(x, b, sat):
    rb = 8
    sign = (x & 0x8000) >> 8
    e16, m16 = (x & 0x7C00) >> 10, x & 0x3FF
    xb = (x + (b >> 1)) & 0xFFFF
    eb, mb = (xb & 0x7C00) >> 10, xb & 0x3FF
    if pc_f16_inf(x):
        e, m = 0xF, 6 if sat else 7
    elif pc_f16_nan(x):
        e, m = 0xF, 7
    elif eb > rb + 15 or (eb == rb + 15 and mb >= 0x380):
        e, m = 0xF, 6 if sat else 7
    elif e16 == 0:
        m = (m16 + (b << 7)) >> (rb + 7)
        e = 0
    elif eb <= rb:
        m = m16 | 0x400
        m = m + (b << (rb - e16))
        m = m >> (rb + 1 - e16)
        e = m >> 10
        m = (m >> 7) & 7
    else:
        e = eb - rb
        m = mb >> 7
    return sign | (e << 3) | m


def pc_hf8_to_fp16(x):
    s = (x & 0x80) << 8
    e, m = (x & 0x78) >> 3, x & 7
    en = e + 8
    if e == 0 and m != 0:
        lz = 2
        lz = 1 if m > 1 else lz
        lz = 0 if m > 3 else lz
        en -= lz
        m = (m << (lz + 1)) & 7
    elif e == 0 and m == 0:
        en = 0
    elif e == 0xF and m == 7:
        en = 0x1F
    return (en << 10) | (m << 7) | s


# ======================================================================================
# instruction models: 64-byte register images in, 64-byte destination image out
# ======================================================================================
def w16(buf, i):
    return buf[2 * i] | (buf[2 * i + 1] << 8)


def d32(buf, i):
    return int.from_bytes(buf[4 * i:4 * i + 4], "little")


def bcst(img, esz, vl):
    """{1toN}: the first element of the memory image replicated over VL"""
    return (img[:esz] * (vl // esz)) + bytes(64 - vl)


def model_cvt_fp8(kind, fmt, sat, old, s1, s2, vl, kmask, zero):
    """kind 'one' (VCVTPH2*), 'two' (VCVT2PH2*), 'bias' (VCVTBIASPH2*)"""
    n = vl if kind == "two" else vl // 2
    out = bytearray(64)
    for i in range(n):
        if kmask is None or (kmask >> i) & 1:
            if kind == "two":
                x = w16(s2, i) if i < n // 2 else w16(s1, i - n // 2)
            else:
                x = w16(s2, i)
            if kind == "bias":
                out[i] = cvt_fp16_to_fp8_bias(x, s1[2 * i], fmt, sat)
            else:
                out[i] = cvt_fp16_to_fp8(x, fmt, sat)
        else:
            out[i] = 0 if zero else old[i]
    return bytes(out)


def model_cvthf82ph(old, src, vl, kmask, zero):
    out = bytearray(64)
    for i in range(vl // 2):
        if kmask is None or (kmask >> i) & 1:
            r = cvt_hf8_to_fp16(src[i])
        else:
            r = 0 if zero else w16(old, i)
        out[2 * i:2 * i + 2] = r.to_bytes(2, "little")
    return bytes(out)


def model_cvt2ps2phx(old, s1, s2, vl, kmask, zero, mxcsr, rc=None):
    """returns (image, new mxcsr); {er} (rc given): no flag"""
    n = vl // 2
    out = bytearray(64)
    flags = 0
    for i in range(n):
        if kmask is None or (kmask >> i) & 1:
            x = d32(s2, i) if i < n // 2 else d32(s1, i - n // 2)
            r, fl = cvt_fp32_to_fp16(x, mxcsr, rc)
            flags |= fl
        else:
            r = 0 if zero else w16(old, i)
        out[2 * i:2 * i + 2] = r.to_bytes(2, "little")
    if rc is not None:
        flags = 0
    return bytes(out), mxcsr | flags


def mask_dwords(old, res, vl, kmask, zero, esz=4):
    o, r = elems(old[:vl], esz), elems(res[:vl], esz)
    out = []
    for j in range(vl // esz):
        if kmask is None or (kmask >> j) & 1:
            out.append(r[j])
        else:
            out.append(0 if zero else o[j])
    return pack(out, esz) + bytes(64 - vl)


def model_vnni(op, sat, old, s1, s2, vl, kmask, zero):
    """op: ('B', 'SS'|'SU'|'UU') or ('W', 'SU'|'US'|'UU'); the VEX model per 256 bits"""
    fn = vnni.vpdpb if op[0] == "B" else vnni.vpdpw
    res = b""
    for h in range(0, vl, 32):
        w = min(32, vl - h)
        res += fn(op[1], sat, old[h:h + w], s1[h:h + w], s2[h:h + w])[:w]
    return mask_dwords(old, res, vl, kmask, zero)


def model_vdpphps(old, s1, s2, vl, kmask, zero):
    res = bytearray(vl)
    for i in range(vl // 4):
        acc = d32(old, i)
        s1o = ref_amx.cvt_fp16_to_fp32(w16(s1, 2 * i + 1))
        s2o = ref_amx.cvt_fp16_to_fp32(w16(s2, 2 * i + 1))
        s1e = ref_amx.cvt_fp16_to_fp32(w16(s1, 2 * i))
        s2e = ref_amx.cvt_fp16_to_fp32(w16(s2, 2 * i))
        acc = ref_amx.fma32(acc, s1o, s2o)
        acc = ref_amx.fma32(acc, s1e, s2e)
        res[4 * i:4 * i + 4] = acc.to_bytes(4, "little")
    return mask_dwords(old, bytes(res), vl, kmask, zero)


def model_mpsadbw(old, s1, s2, vl, kmask, zero, imm):
    """spec 10.2 emulate_vmpsadbw per 128-bit lane; lanes 1/3 use imm8 >> 3"""
    res = bytearray(vl)
    for lane in range(vl // 16):
        ctl = imm >> 3 if lane & 1 else imm
        a = s1[16 * lane:16 * lane + 16]
        b = s2[16 * lane:16 * lane + 16]
        blk2 = (ctl & 3) * 4
        blk1 = ((ctl >> 2) & 1) * 4
        b1 = [a[i + blk1] for i in range(11)]
        b2 = [b[i + blk2] for i in range(4)]
        for i in range(8):
            sm = sum(abs(b1[j + i] - b2[j]) for j in range(4))
            res[16 * lane + 2 * i:16 * lane + 2 * i + 2] = sm.to_bytes(2, "little")
    return mask_dwords(old, bytes(res), vl, kmask, zero, esz=2)


# ======================================================================================
# case generation
# ======================================================================================
cases = []


def emit(c):
    cases.append(c)


def comment(text):
    cases.append("# " + text)


def mem_case(c, off, data):
    c.mem[MEM_RSI + off] = data


# FP16 inputs that exercise every branch: zeros, denormals, the FP8 denormal/normal
# boundaries, halfway cases (ties to even / odd), the overflow thresholds, Inf, NaN
F16_SPECIAL = [
    0x0000, 0x8000, 0x0001, 0x8001, 0x03FF, 0x0400, 0x3C00, 0xBC00, 0x7BFF, 0xFBFF,
    0x7C00, 0xFC00, 0x7E00, 0xFE00, 0x7C01, 0x7D55, 0xFF80, 0x7FFF,
    # BF8: halfway between E5M2 values (x[7:0] = 80h with even / odd x[8]), 57344 .. 65504
    0x3C80, 0x3D80, 0x3C7F, 0x3D81, 0x7B00, 0x7B7F, 0x7B80, 0x7B81, 0x7A80, 0x0080, 0x0180,
    0x0100, 0x00FF, 0x0380, 0x0280,
    # HF8: 448 (5F00), 464 (5F40, tie -> 448), 465, 479.x, 480 (5F80), denormal 2^-9 grid
    0x5F00, 0x5F3F, 0x5F40, 0x5F41, 0x5F7F, 0x5F80, 0x6000, 0x1800, 0x1C00, 0x1C40,
    0x1C3F, 0x1840, 0x1820, 0x1860, 0x1400, 0x1000, 0x1200, 0x1300, 0x2400, 0x2440, 0x2460,
    0x2420, 0x2040, 0xA040, 0x9C40, 0x1BFF, 0x1A00, 0x1900,
]


def rnd_f16():
    k = RNG.random()
    s = RNG.getrandbits(1) << 15
    if k < 0.15:
        return RNG.choice(F16_SPECIAL)
    if k < 0.55:
        e = RNG.randint(1, 30)                  # any finite exponent
    elif k < 0.8:
        e = RNG.randint(0, 9)                   # FP8 denormal ranges / FP16 denormals
    else:
        e = RNG.randint(20, 30)                 # near the FP8 overflow thresholds
    return s | (e << 10) | RNG.getrandbits(10)


def f16_vec(n):
    return b"".join(rnd_f16().to_bytes(2, "little") for _ in range(n))


BIAS_SPECIAL = [0x00, 0x01, 0x7F, 0x80, 0x81, 0xFE, 0xFF, 0x40, 0x3F]


def bias_vec(n):
    """src1 of the bias forms: bias in byte 2i, the odd bytes are ignored"""
    out = bytearray(rnd_bytes(n))
    for i in range(0, n, 2):
        out[i] = RNG.choice(BIAS_SPECIAL) if RNG.random() < 0.3 else RNG.getrandbits(8)
    return bytes(out)


#           name             map opc  pp  kind    fmt  sat
FP8_FORMS = [("VCVTPH2BF8", 2, 0x74, 2, "one", BF8, 0),
             ("VCVTPH2BF8S", 5, 0x74, 2, "one", BF8, 1),
             ("VCVTPH2HF8", 5, 0x18, 2, "one", HF8, 0),
             ("VCVTPH2HF8S", 5, 0x1B, 2, "one", HF8, 1),
             ("VCVT2PH2BF8", 2, 0x74, 3, "two", BF8, 0),
             ("VCVT2PH2BF8S", 5, 0x74, 3, "two", BF8, 1),
             ("VCVT2PH2HF8", 5, 0x18, 3, "two", HF8, 0),
             ("VCVT2PH2HF8S", 5, 0x1B, 3, "two", HF8, 1),
             ("VCVTBIASPH2BF8", 2, 0x74, 0, "bias", BF8, 0),
             ("VCVTBIASPH2BF8S", 5, 0x74, 0, "bias", BF8, 1),
             ("VCVTBIASPH2HF8", 5, 0x18, 0, "bias", HF8, 0),
             ("VCVTBIASPH2HF8S", 5, 0x1B, 0, "bias", HF8, 1)]

VARIANTS = ["reg", "merge", "zero", "mem", "bcst", "bcst_merge", "alias", "hi"]


def gen_fp8():
    comment("--- FP16 -> FP8 down-conversions (spec 9.1, 9.3): opmask per destination byte, E4NF")
    for name, mmm, opc, pp, kind, fmt, sat in FP8_FORMS:
        comment(name)
        for vl in (16, 32, 64):
            for var in VARIANTS:
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                dst, r1, r2 = 1, 2, 3
                if var == "hi":
                    dst, r1, r2 = 17, 30, 25
                old = rnd_bytes(64)
                s1 = (bias_vec(64) if kind == "bias" else f16_vec(32))
                s2 = f16_vec(32)
                kmask = None
                zero = 0
                aaa = 0
                if var in ("merge", "zero", "bcst_merge", "hi"):
                    aaa = 5 if var != "hi" else 7
                    kmask = RNG.getrandbits(64)
                    c.k[aaa] = kmask
                    zero = 1 if var in ("zero", "hi") else 0
                vvvv = r1 if kind != "one" else None
                if var in ("mem", "bcst", "bcst_merge"):
                    b = 1 if var.startswith("bcst") else 0
                    off = RNG.choice([0, 16, 64, -64, 0x40 * 3])
                    nbytes = 2 if b else vl
                    data = s2[:nbytes]
                    mem_case(c, off, data)
                    rm = Mem(RSI, off)
                    n = 2 if b else vl
                    s2v = bcst(data, 2, vl) if b else data + bytes(64 - vl)
                else:
                    b = 0
                    rm, n = r2, 1
                    s2v = s2
                    c.set_zmm(r2, s2)
                if kind != "one":
                    c.set_zmm(r1, s1)
                if var == "alias":
                    # destination = a source register (sources are read before the write)
                    dst = r2
                    old = s2
                else:
                    c.set_zmm(dst, old)
                c.code = evex(mmm, pp, 0, opc, dst, rm, vvvv=vvvv, ll=VL_LL[vl], b=b, z=zero, aaa=aaa, n=n)
                img = model_cvt_fp8(kind, fmt, sat, old, s1, s2v, vl, kmask, zero)
                c.exp.append("zmm%d=%s" % (dst, hexs(img)))
                emit(c)
        # #UD: W1, L'L = 11b, EVEX.b on a register form, {z} without a mask
        for title, kw in (("W1 #UD", dict(w=1)), ("L'L=11b #UD", dict(ll=3)),
                          ("EVEX.b reg #UD", dict(b=1)), ("{z} aaa=0 #UD", dict(z=1)),
                          ("66 prefix slot #UD", dict(pp=1))):
            a = dict(w=0, ll=2, b=0, z=0, pp=pp)
            a.update(kw)
            c = Case("%s %s" % (name, title))
            c.code = evex(mmm, a["pp"], a["w"], opc, 1, 2, vvvv=3 if kind != "one" else None,
                          ll=a["ll"], b=a["b"], z=a["z"])
            c.fault = "#UD"
            emit(c)
        if kind == "one":
            c = Case("%s vvvv != 1111b #UD" % name)
            c.code = evex(mmm, pp, 0, opc, 1, 2, vvvv=4, ll=2)
            c.fault = "#UD"
            emit(c)


def gen_fp8_exhaustive():
    """every FP16 input through every RNE form (VCVT2PH2*: 64 values per case) and through
    every bias form with varied bias bytes (VCVTBIASPH2* zmm: 32 values per case)"""
    comment("--- exhaustive: all 65536 FP16 inputs (RNE forms, 64 per case; bias forms, 32 per case)")
    for name, mmm, opc, pp, kind, fmt, sat in FP8_FORMS:
        if kind == "one":
            continue
        comment("%s all FP16 inputs" % name)
        per = 64 if kind == "two" else 32
        for base in range(0, 65536, per):
            vals = list(range(base, base + per))
            c = Case("%s inputs %04X..%04X" % (name, base, base + per - 1))
            if kind == "two":
                s2 = b"".join(v.to_bytes(2, "little") for v in vals[:32])
                s1 = b"".join(v.to_bytes(2, "little") for v in vals[32:])
            else:
                s2 = b"".join(v.to_bytes(2, "little") for v in vals)
                bb = bytearray(64)
                for i in range(32):
                    bb[2 * i] = (base // 32 * 37 + i * 101 + (i >> 3) * 13) & 0xFF
                    if (base // 32 + i) % 11 == 0:
                        bb[2 * i] = RNG.choice(BIAS_SPECIAL)
                s1 = bytes(bb)
            c.set_zmm(2, s1)
            c.set_zmm(3, s2)
            old = bytes(64)
            c.code = evex(mmm, pp, 0, opc, 1, 3, vvvv=2, ll=2)
            img = model_cvt_fp8(kind, fmt, sat, old, s1, s2, 64, None, 0)
            c.exp.append("zmm1=%s" % hexs(img))
            emit(c)


def gen_cvthf82ph():
    comment("--- VCVTHF82PH (spec 9.4): Half Mem tuple, E4 (fault suppression)")
    allv = bytes(range(256))
    for vl in (16, 32, 64):
        for var in ("reg", "merge", "zero", "mem", "mem_merge", "alias", "hi"):
            c = Case("VCVTHF82PH VL%d %s" % (vl * 8, var))
            dst, src = 1, 2
            if var == "hi":
                dst, src = 20, 31
            old = rnd_bytes(64)
            s = bytes(RNG.getrandbits(8) for _ in range(64))
            kmask, zero, aaa = None, 0, 0
            if var in ("merge", "zero", "mem_merge", "hi"):
                aaa = 3
                kmask = RNG.getrandbits(64)
                c.k[aaa] = kmask
                zero = 1 if var in ("zero", "hi") else 0
            if var.startswith("mem"):
                off = RNG.choice([0, 8, 0x40, -32])
                data = s[:vl // 2]
                mem_case(c, off, data)
                rm, n = Mem(RSI, off), vl // 2
                sv = data + bytes(64 - vl // 2)
            else:
                rm, n = src, 1
                sv = s
                c.set_zmm(src, s)
            if var == "alias":
                dst = src
                old = s
            else:
                c.set_zmm(dst, old)
            c.code = evex(5, 3, 0, 0x1E, dst, rm, ll=VL_LL[vl], z=zero, aaa=aaa, n=n)
            c.exp.append("zmm%d=%s" % (dst, hexs(model_cvthf82ph(old, sv, vl, kmask, zero))))
            emit(c)
    comment("VCVTHF82PH all 256 E4M3 inputs")
    for base in range(0, 256, 32):
        c = Case("VCVTHF82PH inputs %02X..%02X" % (base, base + 31))
        s = allv[base:base + 32] + bytes(32)
        c.set_zmm(4, s)
        c.code = evex(5, 3, 0, 0x1E, 5, 4, ll=2)
        c.exp.append("zmm5=%s" % hexs(model_cvthf82ph(bytes(64), s, 64, None, 0)))
        emit(c)
    comment("VCVTHF82PH fault suppression next to the unmapped page")
    base = 0x10000 - MEM_RSI - 16
    for kv, fault in ((0xFFFF, None), (0x1FFFF, "#PF")):
        c = Case("VCVTHF82PH zmm k=%X [last 16 mapped bytes]%s" % (kv, " #PF" if fault else ""))
        old = rnd_bytes(64)
        c.set_zmm(6, old)
        c.k[2] = kv
        data = rnd_bytes(16)
        mem_case(c, base, data)
        c.code = evex(5, 3, 0, 0x1E, 6, Mem(RSI, base, disp32=True), ll=2, aaa=2)
        if fault:
            c.fault = fault
        else:
            c.exp.append("zmm6=%s" % hexs(model_cvthf82ph(old, data + bytes(48), 64, kv, 0)))
        emit(c)
    for title, kw in (("W1 #UD", dict(w=1)), ("EVEX.b mem #UD", dict(b=1, mem=1)),
                      ("EVEX.b reg #UD", dict(b=1)), ("vvvv != 1111b #UD", dict(vvvv=3)),
                      ("L'L=11b #UD", dict(ll=3)), ("NP prefix slot #UD", dict(pp=0))):
        c = Case("VCVTHF82PH " + title)
        rm = Mem(RSI, 0) if kw.get("mem") else 2
        c.code = evex(5, kw.get("pp", 3), kw.get("w", 0), 0x1E, 1, rm, vvvv=kw.get("vvvv"),
                      ll=kw.get("ll", 2), b=kw.get("b", 0))
        c.fault = "#UD"
        emit(c)


F32_SPECIAL = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000,
               0x7FC00000, 0x7FA00001, 0xFFC12345, 0x00000001, 0x80400000, 0x7F7FFFFF,
               0x477FE000, 0x477FEFFF, 0x477FF000, 0x47800000, 0xC77FF000, 0x33800000,
               0x33000000, 0x33000001, 0x337FFFFF, 0xB3000001, 0x38800000, 0x387FC000,
               0x387FE000, 0x38800001, 0x3EAAAAAB, 0x3F801000, 0x3F803000, 0x3F802000,
               0x3F800FFF, 0x00800000]


def rnd_f32():
    k = RNG.random()
    s = RNG.getrandbits(1) << 31
    if k < 0.25:
        return RNG.choice(F32_SPECIAL)
    if k < 0.6:
        e = RNG.randint(97, 145)                # around the FP16 range
    elif k < 0.8:
        e = RNG.randint(100, 113)               # FP16 denormal / underflow range
    else:
        e = RNG.randint(1, 254)
    return s | (e << 23) | RNG.getrandbits(23)


def f32_vec(n):
    return b"".join(rnd_f32().to_bytes(4, "little") for _ in range(n))


def gen_cvt2ps2phx():
    comment("--- VCVT2PS2PHX (spec 9.2): FP32 -> FP16, MXCSR flags as if masked, no #XM, {er}")
    mxcsrs = [0x1F80, 0x3F80, 0x5F80, 0x7F80, 0x1FC0, 0x9F80, 0x1F00, 0x0000, 0x8040]
    for vl in (16, 32, 64):
        for var in ("reg", "merge", "zero", "mem", "bcst", "alias", "hi"):
            for mx in mxcsrs:
                c = Case("VCVT2PS2PHX VL%d %s mxcsr=%X" % (vl * 8, var, mx))
                dst, r1, r2 = 1, 2, 3
                if var == "hi":
                    dst, r1, r2 = 28, 19, 16
                old = rnd_bytes(64)
                s1, s2 = f32_vec(16), f32_vec(16)
                kmask, zero, aaa = None, 0, 0
                if var in ("merge", "zero", "hi"):
                    aaa = 4
                    kmask = RNG.getrandbits(64)
                    c.k[aaa] = kmask
                    zero = 1 if var != "merge" else 0
                c.set_zmm(r1, s1)
                b = 0
                if var in ("mem", "bcst"):
                    b = 1 if var == "bcst" else 0
                    off = RNG.choice([0, 64, -16, 0x100])
                    data = s2[:4] if b else s2[:vl]
                    mem_case(c, off, data)
                    rm, n = Mem(RSI, off), (4 if b else vl)
                    s2v = bcst(data, 4, vl) if b else data + bytes(64 - vl)
                else:
                    rm, n = r2, 1
                    s2v = s2
                    c.set_zmm(r2, s2)
                if var == "alias":
                    dst = r1
                    old = s1
                else:
                    c.set_zmm(dst, old)
                c.mxcsr = mx
                c.code = evex(2, 1, 0, 0x67, dst, rm, vvvv=r1, ll=VL_LL[vl], b=b, z=zero, aaa=aaa, n=n)
                img, mxo = model_cvt2ps2phx(old, s1, s2v, vl, kmask, zero, mx)
                c.exp.append("zmm%d=%s" % (dst, hexs(img)))
                if mxo != mx:
                    c.exp.append("mxcsr=0x%X" % mxo)
                emit(c)
    comment("VCVT2PS2PHX {er}: register form, 512 bits, static rounding, no MXCSR flag")
    for rc in range(4):
        for masked in (0, 1):
            c = Case("VCVT2PS2PHX zmm {rc=%d}%s" % (rc, " {k}{z}" if masked else ""))
            old = rnd_bytes(64)
            s1, s2 = f32_vec(16), f32_vec(16)
            c.set_zmm(1, old); c.set_zmm(2, s1); c.set_zmm(3, s2)
            kmask = RNG.getrandbits(64) if masked else None
            if masked:
                c.k[6] = kmask
            c.mxcsr = 0x1F80 ^ (((rc + 1) & 3) << 13)
            c.code = evex(2, 1, 0, 0x67, 1, 3, vvvv=2, ll=rc, b=1, z=masked, aaa=6 if masked else 0)
            img, _ = model_cvt2ps2phx(old, s1, s2, 64, kmask, masked, c.mxcsr, rc)
            c.exp.append("zmm1=%s" % hexs(img))
            emit(c)
    comment("VCVT2PS2PHX: a masked-off SNaN raises no IE; unmasked IM = 0 still no #XM")
    c = Case("VCVT2PS2PHX xmm masked-off SNaN")
    s1 = (0x7F800001).to_bytes(4, "little") * 4
    s2 = (0x3F800000).to_bytes(4, "little") * 4 + bytes(48)
    c.set_zmm(1, bytes(64)); c.set_zmm(2, s1 + bytes(48)); c.set_zmm(3, s2)
    c.k[1] = 0x0F
    c.code = evex(2, 1, 0, 0x67, 1, 3, vvvv=2, ll=0, aaa=1)
    img, mxo = model_cvt2ps2phx(bytes(64), s1 + bytes(48), s2, 16, 0x0F, 0, 0x1F80)
    c.exp.append("zmm1=%s" % hexs(img))
    emit(c)
    c = Case("VCVT2PS2PHX xmm SNaN with MXCSR.IM = 0: IE set, no #XM")
    c.set_zmm(1, bytes(64)); c.set_zmm(2, s1 + bytes(48)); c.set_zmm(3, s2)
    c.mxcsr = 0x1F00
    c.code = evex(2, 1, 0, 0x67, 1, 3, vvvv=2, ll=0)
    img, mxo = model_cvt2ps2phx(bytes(64), s1 + bytes(48), s2, 16, None, 0, 0x1F00)
    c.exp.append("zmm1=%s" % hexs(img))
    c.exp.append("mxcsr=0x%X" % mxo)
    emit(c)
    comment("VCVT2PS2PHX: no memory fault suppression (#PF with every element masked off)")
    c = Case("VCVT2PS2PHX zmm k=0 [unmapped] #PF")
    c.set_zmm(1, rnd_bytes(64))
    c.k[3] = 0
    c.code = evex(2, 1, 0, 0x67, 1, Mem(RSI, 0x10000 - MEM_RSI - 32, disp32=True), vvvv=2, ll=2, aaa=3)
    c.fault = "#PF"
    emit(c)
    for title, kw in (("W1 #UD", dict(w=1)), ("EVEX.b on xmm reg-reg? {er} -> 512", None),
                      ("NP prefix slot #UD", dict(pp=0)), ("F3 prefix slot #UD", dict(pp=2)),
                      ("{z} aaa=0 #UD", dict(z=1)), ("L'L=11b #UD", dict(ll=3))):
        if kw is None:
            continue
        c = Case("VCVT2PS2PHX " + title)
        c.code = evex(2, kw.get("pp", 1), kw.get("w", 0), 0x67, 1, 2, vvvv=3, ll=kw.get("ll", 2),
                      z=kw.get("z", 0))
        c.fault = "#UD"
        emit(c)


VNNI_FORMS = [("VPDPBUUD", 0x50, 0, ("B", "UU"), 0), ("VPDPBSUD", 0x50, 2, ("B", "SU"), 0),
              ("VPDPBSSD", 0x50, 3, ("B", "SS"), 0), ("VPDPBUUDS", 0x51, 0, ("B", "UU"), 1),
              ("VPDPBSUDS", 0x51, 2, ("B", "SU"), 1), ("VPDPBSSDS", 0x51, 3, ("B", "SS"), 1),
              ("VPDPWUUD", 0xD2, 0, ("W", "UU"), 0), ("VPDPWUSD", 0xD2, 1, ("W", "US"), 0),
              ("VPDPWSUD", 0xD2, 2, ("W", "SU"), 0), ("VPDPWUUDS", 0xD3, 0, ("W", "UU"), 1),
              ("VPDPWUSDS", 0xD3, 1, ("W", "US"), 1), ("VPDPWSUDS", 0xD3, 2, ("W", "SU"), 1)]

ACC_SPECIAL = [0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x00000000, 0x7FFFFF00, 0x80000100,
               0xFFFFFF00, 0x0000FF00]


def vnni_vec(kind, n):
    """sources with saturation-provoking extremes mixed in"""
    out = bytearray(rnd_bytes(n))
    for i in range(n):
        if RNG.random() < 0.25:
            out[i] = RNG.choice([0x00, 0x7F, 0x80, 0xFF, 0x01])
    return bytes(out)


def acc_vec():
    v = bytearray(rnd_bytes(64))
    for i in range(16):
        if RNG.random() < 0.4:
            v[4 * i:4 * i + 4] = RNG.choice(ACC_SPECIAL).to_bytes(4, "little")
    return bytes(v)


def gen_vnni():
    comment("--- AVX10.2 VNNI (spec 10.3, 10.4): EVEX VPDPB[SS,SU,UU]D[S], VPDPW[SU,US,UU]D[S]")
    for name, opc, pp, op, sat in VNNI_FORMS:
        comment(name)
        for vl in (16, 32, 64):
            for var in ("reg", "merge", "zero", "mem", "bcst", "bcst_zero", "alias1", "alias2", "hi"):
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                dst, r1, r2 = 1, 2, 3
                if var == "hi":
                    dst, r1, r2 = 31, 16, 24
                old = acc_vec()
                s1, s2 = vnni_vec(op, 64), vnni_vec(op, 64)
                kmask, zero, aaa = None, 0, 0
                if var in ("merge", "zero", "bcst_zero", "hi"):
                    aaa = 2
                    kmask = RNG.getrandbits(64)
                    c.k[aaa] = kmask
                    zero = 0 if var == "merge" else 1
                b = 0
                if var in ("mem", "bcst", "bcst_zero"):
                    b = 1 if var.startswith("bcst") else 0
                    off = RNG.choice([0, 64, -64, 0x200, 4])
                    data = s2[:4] if b else s2[:vl]
                    if not b and off % vl:
                        off = 0
                    mem_case(c, off, data)
                    rm, n = Mem(RSI, off), (4 if b else vl)
                    s2v = bcst(data, 4, vl) if b else data + bytes(64 - vl)
                else:
                    rm, n = r2, 1
                    s2v = s2
                    c.set_zmm(r2, s2)
                c.set_zmm(r1, s1)
                if var == "alias1":
                    dst = r1
                    old = s1
                elif var == "alias2":
                    dst = r2
                    old = s2
                    s2v = s2
                else:
                    c.set_zmm(dst, old)
                c.code = evex(2, pp, 0, opc, dst, rm, vvvv=r1, ll=VL_LL[vl], b=b, z=zero, aaa=aaa, n=n)
                s1v = old if var == "alias1" else s1
                img = model_vnni(op, sat, old, s1v, s2v, vl, kmask, zero)
                c.exp.append("zmm%d=%s" % (dst, hexs(img)))
                emit(c)
        for title, kw in (("W1 #UD", dict(w=1)), ("EVEX.b reg #UD", dict(b=1)),
                          ("L'L=11b #UD", dict(ll=3))):
            c = Case("%s %s" % (name, title))
            c.code = evex(2, pp, kw.get("w", 0), opc, 1, 2, vvvv=3, ll=kw.get("ll", 2), b=kw.get("b", 0))
            c.fault = "#UD"
            emit(c)
    comment("VNNI fault suppression: E4")
    base = 0x10000 - MEM_RSI - 32
    for kv, fault in ((0xFF, None), (0x1FF, "#PF")):
        c = Case("VPDPBSSD zmm k=%X [last 32 mapped bytes]%s" % (kv, " #PF" if fault else ""))
        old, s1 = acc_vec(), vnni_vec(None, 64)
        c.set_zmm(1, old); c.set_zmm(2, s1)
        c.k[7] = kv
        data = rnd_bytes(32)
        mem_case(c, base, data)
        c.code = evex(2, 3, 0, 0x50, 1, Mem(RSI, base, disp32=True), vvvv=2, ll=2, aaa=7)
        if fault:
            c.fault = fault
        else:
            img = model_vnni(("B", "SS"), 0, old, s1, data + bytes(32), 64, kv, 0)
            c.exp.append("zmm1=%s" % hexs(img))
        emit(c)


F16_DP_SPECIAL = [0x0000, 0x8000, 0x3C00, 0xBC00, 0x7C00, 0xFC00, 0x7E00, 0x7D00, 0xFE01,
                  0x0001, 0x8001, 0x03FF, 0x7BFF, 0xFBFF, 0x0400, 0x3555, 0x2E66, 0x5BFF]
F32_ACC_SPECIAL = [0x00000000, 0x80000000, 0x3F800000, 0x7F800000, 0xFF800000, 0x7FC00000,
                   0x7FA00000, 0xFF800001, 0x00000001, 0x807FFFFF, 0x00800000, 0x7F7FFFFF,
                   0xFF7FFFFF, 0x4B800000, 0x33800000]


def dp_f16():
    if RNG.random() < 0.3:
        return RNG.choice(F16_DP_SPECIAL)
    return (RNG.getrandbits(1) << 15) | (RNG.randint(0, 30) << 10) | RNG.getrandbits(10)


def dp_vec():
    return b"".join(dp_f16().to_bytes(2, "little") for _ in range(32))


def dp_acc():
    out = b""
    for _ in range(16):
        if RNG.random() < 0.35:
            v = RNG.choice(F32_ACC_SPECIAL)
        else:
            v = (RNG.getrandbits(1) << 31) | (RNG.randint(80, 175) << 23) | RNG.getrandbits(23)
        out += v.to_bytes(4, "little")
    return out


def gen_vdpphps():
    comment("--- VDPPHPS (spec 10.1): fma32 DAZ=FTZ=1 RNE, odd pair first, MXCSR untouched")
    for vl in (16, 32, 64):
        for var in ("reg", "merge", "zero", "mem", "bcst", "alias", "hi", "mxcsr"):
            c = Case("VDPPHPS VL%d %s" % (vl * 8, var))
            dst, r1, r2 = 1, 2, 3
            if var == "hi":
                dst, r1, r2 = 18, 29, 21
            old, s1, s2 = dp_acc(), dp_vec(), dp_vec()
            kmask, zero, aaa = None, 0, 0
            if var in ("merge", "zero", "hi"):
                aaa = 1
                kmask = RNG.getrandbits(64)
                c.k[aaa] = kmask
                zero = 0 if var == "merge" else 1
            b = 0
            if var in ("mem", "bcst"):
                b = 1 if var == "bcst" else 0
                off = RNG.choice([0, 64, -64])
                data = s2[:4] if b else s2[:vl]
                mem_case(c, off, data)
                rm, n = Mem(RSI, off), (4 if b else vl)
                s2v = bcst(data, 4, vl) if b else data + bytes(64 - vl)
            else:
                rm, n = r2, 1
                s2v = s2
                c.set_zmm(r2, s2)
            c.set_zmm(r1, s1)
            if var == "alias":
                dst = r1
                old = s1
            else:
                c.set_zmm(dst, old)
            if var == "mxcsr":
                c.mxcsr = 0x0040 | (3 << 13)    # DAZ, RZ, every exception unmasked: no effect
            c.code = evex(2, 0, 0, 0x52, dst, rm, vvvv=r1, ll=VL_LL[vl], b=b, z=zero, aaa=aaa, n=n)
            s1v = old if var == "alias" else s1
            c.exp.append("zmm%d=%s" % (dst, hexs(model_vdpphps(old, s1v, s2v, vl, kmask, zero))))
            emit(c)
    for title, kw in (("W1 #UD", dict(w=1)), ("EVEX.b reg #UD", dict(b=1)), ("F2 slot #UD", dict(pp=3))):
        c = Case("VDPPHPS " + title)
        c.code = evex(2, kw.get("pp", 0), kw.get("w", 0), 0x52, 1, 2, vvvv=3, ll=2, b=kw.get("b", 0))
        c.fault = "#UD"
        emit(c)


def gen_mpsadbw():
    comment("--- VMPSADBW (spec 10.2): Full Mem tuple, mask per word, E4NF")
    for vl in (16, 32, 64):
        for imm in (0x00, 0x05, 0x2A, 0x3F, 0x13, 0x24, 0xC7, 0xFF):
            for var in ("reg", "merge", "zero", "mem", "alias"):
                if var != "reg" and imm not in (0x2A, 0x13):
                    continue
                c = Case("VMPSADBW VL%d imm=%02X %s" % (vl * 8, imm, var))
                dst, r1, r2 = 4, 5, 6
                old, s1, s2 = rnd_bytes(64), rnd_bytes(64), rnd_bytes(64)
                kmask, zero, aaa = None, 0, 0
                if var in ("merge", "zero"):
                    aaa = 6
                    kmask = RNG.getrandbits(64)
                    c.k[aaa] = kmask
                    zero = 1 if var == "zero" else 0
                if var == "mem":
                    off = RNG.choice([0, 64, -64])
                    mem_case(c, off, s2[:vl])
                    rm, n = Mem(RSI, off), vl
                    s2v = s2[:vl] + bytes(64 - vl)
                else:
                    rm, n = r2, 1
                    s2v = s2
                    c.set_zmm(r2, s2)
                c.set_zmm(r1, s1)
                if var == "alias":
                    dst = r2
                    old = s2
                else:
                    c.set_zmm(dst, old)
                c.code = evex(3, 2, 0, 0x42, dst, rm, vvvv=r1, ll=VL_LL[vl], z=zero, aaa=aaa, imm=imm, n=n)
                c.exp.append("zmm%d=%s" % (dst, hexs(model_mpsadbw(old, s1, s2v, vl, kmask, zero, imm))))
                emit(c)
    c = Case("VMPSADBW zmm k=0 [unmapped] #PF (E4NF)")
    c.k[6] = 0
    c.code = evex(3, 2, 0, 0x42, 4, Mem(RSI, 0x10000 - MEM_RSI - 32, disp32=True), vvvv=5, ll=2, aaa=6,
                  imm=0)
    c.fault = "#PF"
    emit(c)
    for title, kw in (("W1 #UD", dict(w=1)), ("EVEX.b reg #UD", dict(b=1)), ("EVEX.b mem #UD", dict(b=1, mem=1)),
                      ("F2 slot #UD", dict(pp=3))):
        c = Case("VMPSADBW " + title)
        rm = Mem(RSI, 0) if kw.get("mem") else 2
        c.code = evex(3, kw.get("pp", 2), kw.get("w", 0), 0x42, 1, rm, vvvv=3, ll=2, b=kw.get("b", 0), imm=1)
        c.fault = "#UD"
        emit(c)


MOVRS_FORMS = [("VMOVRSB", 3, 0, 1), ("VMOVRSW", 3, 1, 2), ("VMOVRSD", 2, 0, 4), ("VMOVRSQ", 2, 1, 8)]


def gen_movrs():
    comment("--- VMOVRSB/W/D/Q (ISE 319433-062 MOVRS): masked vector load, memory only, E4")
    for name, pp, w, esz in MOVRS_FORMS:
        for vl in (16, 32, 64):
            for var in ("plain", "merge", "zero", "hi"):
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                dst = 9 if var != "hi" else 26
                old = rnd_bytes(64)
                c.set_zmm(dst, old)
                kmask, zero, aaa = None, 0, 0
                if var != "plain":
                    aaa = 4
                    kmask = RNG.getrandbits(64)
                    c.k[aaa] = kmask
                    zero = 1 if var in ("zero", "hi") else 0
                off = RNG.choice([0, vl, -vl, 3 * vl])
                data = rnd_bytes(vl)
                mem_case(c, off, data)
                c.code = evex(5, pp, w, 0x6F, dst, Mem(RSI, off), ll=VL_LL[vl], z=zero, aaa=aaa, n=vl)
                c.exp.append("zmm%d=%s" % (dst, hexs(mask_dwords(old, data + bytes(64 - vl), vl, kmask, zero, esz))))
                emit(c)
        base = 0x10000 - MEM_RSI - 32
        nlo = 32 // esz
        for kv, fault in (((1 << nlo) - 1, None), ((1 << nlo) | 1, "#PF")):
            c = Case("%s zmm fault suppression k=%X%s" % (name, kv, " #PF" if fault else ""))
            old = rnd_bytes(64)
            c.set_zmm(9, old)
            c.k[4] = kv
            data = rnd_bytes(32)
            mem_case(c, base, data)
            c.code = evex(5, pp, w, 0x6F, 9, Mem(RSI, base, disp32=True), ll=2, aaa=4)
            if fault:
                c.fault = fault
            else:
                c.exp.append("zmm9=%s" % hexs(mask_dwords(old, data + bytes(32), 64, kv, 0, esz)))
            emit(c)
        for title, kw in (("register source #UD", dict(reg=1)), ("EVEX.b #UD", dict(b=1)),
                          ("vvvv != 1111b #UD", dict(vvvv=3)), ("{z} aaa=0 #UD", dict(z=1)),
                          ("L'L=11b #UD", dict(ll=3))):
            c = Case("%s %s" % (name, title))
            rm = 2 if kw.get("reg") else Mem(RSI, 0)
            c.code = evex(5, pp, w, 0x6F, 1, rm, vvvv=kw.get("vvvv"), ll=kw.get("ll", 2), b=kw.get("b", 0),
                          z=kw.get("z", 0))
            c.fault = "#UD"
            emit(c)
    c = Case("MAP5 66 6F (no AVX10 form) #UD")
    c.code = evex(5, 1, 0, 0x6F, 1, Mem(RSI, 0), ll=2)
    c.fault = "#UD"
    emit(c)


#            name      map opc  pp  esz  store
MOVZ_FORMS = [("VMOVD", 1, 0x7E, 2, 4, 0), ("VMOVD", 1, 0xD6, 1, 4, 1),
              ("VMOVW", 5, 0x6E, 2, 2, 0), ("VMOVW", 5, 0x7E, 2, 2, 1)]


def gen_movz():
    comment("--- zero-extending VMOVD / VMOVW (spec 13): EVEX.128.W0 only, no masking, E9NF")
    for name, mmm, opc, pp, esz, st in MOVZ_FORMS:
        form = "%s %s %02X" % (name, "MAP5" if mmm == 5 else "0F", opc)
        for var in ("reg", "reg_hi", "same", "mem"):
            c = Case("%s %s %s" % (form, "store" if st else "load", var))
            a, b2 = (1, 2) if var == "reg" else (21, 9) if var == "reg_hi" else (7, 7)
            va, vb = rnd_bytes(64), rnd_bytes(64)
            c.set_zmm(a, va)
            if b2 != a:
                c.set_zmm(b2, vb)
            else:
                vb = va
            if var == "mem":
                off = RNG.choice([0, esz, -esz, 127 * esz])
                if st:
                    oldm = rnd_bytes(esz)
                    mem_case(c, off, oldm)
                    c.code = evex(mmm, pp, 0, opc, a, Mem(RSI, off), ll=0, n=esz)
                    c.exp.append("m+0x%X=%s" % (MEM_RSI + off, hexs(va[:esz])))
                else:
                    data = rnd_bytes(esz)
                    mem_case(c, off, data)
                    c.code = evex(mmm, pp, 0, opc, a, Mem(RSI, off), ll=0, n=esz)
                    c.exp.append("zmm%d=%s" % (a, hexs(data + bytes(64 - esz))))
            else:
                # load: reg = a (dest), rm = b2 (source); store: rm = b2 (dest), reg = a (source)
                c.code = evex(mmm, pp, 0, opc, a, b2, ll=0)
                if st:
                    c.exp.append("zmm%d=%s" % (b2, hexs(va[:esz] + bytes(64 - esz))))
                else:
                    c.exp.append("zmm%d=%s" % (a, hexs(vb[:esz] + bytes(64 - esz))))
            emit(c)
        for title, kw in (("W1 (VMOVQ form, not AVX10.2) skip", None), ("L'L=01b #UD", dict(ll=1)),
                          ("L'L=10b #UD", dict(ll=2)), ("masking #UD", dict(aaa=1)),
                          ("EVEX.b reg #UD", dict(b=1)), ("EVEX.b mem #UD", dict(b=1, mem=1)),
                          ("vvvv != 1111b #UD", dict(vvvv=3)), ("V' = 0 #UD", dict(vp=0))):
            if kw is None:
                continue
            c = Case("%s %s" % (form, title))
            rm = Mem(RSI, 0) if kw.get("mem") else 2
            c.code = evex(mmm, pp, 0, opc, 1, rm, vvvv=kw.get("vvvv"), ll=kw.get("ll", 0), b=kw.get("b", 0),
                          aaa=kw.get("aaa", 0), p2_vp=kw.get("vp"))
            c.fault = "#UD"
            emit(c)
        if mmm == 5:
            c = Case("%s W1 #UD" % form)
            c.code = evex(mmm, pp, 1, opc, 1, 2, ll=0)
            c.fault = "#UD"
            emit(c)


def gen_sm4():
    comment("--- EVEX VSM4KEY4 / VSM4RNDS4 (ISE 319433-062, AVX10 AND SM4): 128-bit lanes, no masking, E6")
    for name, pp, fn in (("VSM4KEY4", 2, sm.vsm4key4), ("VSM4RNDS4", 3, sm.vsm4rnds4)):
        for vl in (16, 32, 64):
            for var in ("reg", "mem", "alias", "hi"):
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                dst, r1, r2 = (1, 2, 3) if var != "hi" else (30, 17, 23)
                old, s1, s2 = rnd_bytes(64), rnd_bytes(64), rnd_bytes(64)
                c.set_zmm(r1, s1)
                if var == "mem":
                    off = RNG.choice([0, vl, -vl])
                    mem_case(c, off, s2[:vl])
                    rm, n = Mem(RSI, off), vl
                else:
                    rm, n = r2, 1
                    c.set_zmm(r2, s2)
                if var == "alias":
                    dst = r1
                else:
                    c.set_zmm(dst, old)
                c.code = evex(2, pp, 0, 0xDA, dst, rm, vvvv=r1, ll=VL_LL[vl], n=n)
                r = fn(int.from_bytes(s1[:vl], "little"), int.from_bytes(s2[:vl], "little"), vl * 8)
                c.exp.append("zmm%d=%s" % (dst, hexs(r.to_bytes(vl, "little") + bytes(64 - vl))))
                emit(c)
        for title, kw in (("masking #UD", dict(aaa=1)), ("EVEX.b reg #UD", dict(b=1)),
                          ("EVEX.b mem #UD", dict(b=1, mem=1)), ("W1 #UD", dict(w=1)),
                          ("L'L=11b #UD", dict(ll=3))):
            c = Case("%s %s" % (name, title))
            rm = Mem(RSI, 0) if kw.get("mem") else 2
            c.code = evex(2, pp, kw.get("w", 0), 0xDA, 1, rm, vvvv=3, ll=kw.get("ll", 2), b=kw.get("b", 0),
                          aaa=kw.get("aaa", 0))
            c.fault = "#UD"
            emit(c)
    # GB/T 32907-2016 example A.1 through the 512-bit forms: the same round keys in every lane
    c = Case("VSM4KEY4 zmm: GB/T 32907 example key, four lanes")
    mk = bytes.fromhex("0123456789abcdeffedcba9876543210")
    fk = [0xA3B1BAC6, 0x56AA3350, 0x677D9197, 0xB27022DC]
    k = [int.from_bytes(mk[4 * i:4 * i + 4], "big") ^ fk[i] for i in range(4)]
    ck = [sum((((4 * i + j) * 7) & 0xFF) << (24 - 8 * j) for j in range(4)) for i in range(4)]
    s1 = b"".join(x.to_bytes(4, "little") for x in k) * 4
    s2 = b"".join(x.to_bytes(4, "little") for x in ck) * 4
    c.set_zmm(2, s1); c.set_zmm(3, s2); c.set_zmm(1, rnd_bytes(64))
    c.code = evex(2, 2, 0, 0xDA, 1, 3, vvvv=2, ll=2)
    r = sm.vsm4key4(int.from_bytes(s1, "little"), int.from_bytes(s2, "little"), 512)
    rk0 = r & 0xFFFFFFFF
    assert rk0 == 0xF12186F9, hex(rk0)          # rk[0] of GB/T 32907 example A.1
    c.exp.append("zmm1=%s" % hexs(r.to_bytes(64, "little")))
    emit(c)


def gen_misc():
    comment("--- disp8*N: Full (m16bcst / VL), Half Mem (VL/2), Full Mem (VL), Tuple1 Scalar")
    for d8 in (1, -1, 127, -128):
        for vl in (16, 64):
            c = Case("VCVTPH2HF8 disp8=%d N=%d" % (d8, vl))
            data = f16_vec(vl // 2)
            mem_case(c, d8 * vl, data)
            old = rnd_bytes(64)
            c.set_zmm(1, old)
            c.code = evex(5, 2, 0, 0x18, 1, Mem(RSI, d8 * vl), ll=VL_LL[vl], n=vl)
            c.exp.append("zmm1=%s" % hexs(model_cvt_fp8("one", HF8, 0, old, bytes(64), data + bytes(64 - vl),
                                                        vl, None, 0)))
            emit(c)
            c = Case("VCVTPH2HF8 {1to%d} disp8=%d N=2" % (vl // 2, d8))
            data = f16_vec(1)
            mem_case(c, d8 * 2, data)
            c.set_zmm(1, old)
            c.code = evex(5, 2, 0, 0x18, 1, Mem(RSI, d8 * 2), ll=VL_LL[vl], b=1, n=2)
            c.exp.append("zmm1=%s" % hexs(model_cvt_fp8("one", HF8, 0, old, bytes(64), bcst(data, 2, vl),
                                                        vl, None, 0)))
            emit(c)
            c = Case("VCVTHF82PH disp8=%d N=%d" % (d8, vl // 2))
            data = rnd_bytes(vl // 2)
            mem_case(c, d8 * vl // 2, data)
            c.set_zmm(1, old)
            c.code = evex(5, 3, 0, 0x1E, 1, Mem(RSI, d8 * vl // 2), ll=VL_LL[vl], n=vl // 2)
            c.exp.append("zmm1=%s" % hexs(model_cvthf82ph(old, data + bytes(64), vl, None, 0)))
            emit(c)
    comment("--- feature-independent control: VPXORD after the AVX10.2 forms")
    c = Case("VPXORD zmm1, zmm1, zmm1 (control)")
    c.set_zmm(1, rnd_bytes(64))
    c.code = evex(1, 1, 0, 0xEF, 1, 1, vvvv=1, ll=2)
    c.exp.append("zmm1=%s" % hexs(bytes(64)))
    emit(c)


# ======================================================================================
# hardware cross-check of the VNNI model: the i5-13600K has AVX-VNNI (VEX VPDPBUSD[S],
# VPDPWSSD[S]) but no AVX10.2; where the semantics coincide the host results must equal
# this model's EVEX forms:
#   VPDPBUSD  d, a(u8), b(s8)  == VPDPBSUD  d, b, a       (operands swapped: s8 * u8)
#   VPDPBUSDS d, a, b          == VPDPBSUDS d, b, a       (signed saturation both)
#   VPDPBUSD with b < 80h      == VPDPBUUD  d, a, b       (non-saturating)
#   VPDPBUSD with a < 80h      == VPDPBSSD  d, a, b;  VPDPBUSDS a < 80h == VPDPBSSDS
#   VPDPWSSD with b < 8000h    == VPDPWSUD  d, a, b;  VPDPWSSDS b < 8000h == VPDPWSUDS
#   VPDPWSSD with a < 8000h    == VPDPWUSD  d, a, b;  VPDPWSSDS a < 8000h == VPDPWUSDS
#   VPDPWSSD with a, b < 8000h == VPDPWUUD  d, a, b       (non-saturating)
# ======================================================================================
HW_FORMS = [  # host op (VEX pp/opcode), restriction, model EVEX form, swap
    ("VPDPBUSD", 0x50, None, ("B", "SU"), 0, True),
    ("VPDPBUSDS", 0x51, None, ("B", "SU"), 1, True),
    ("VPDPBUSD", 0x50, "b7", ("B", "UU"), 0, False),
    ("VPDPBUSD", 0x50, "a7", ("B", "SS"), 0, False),
    ("VPDPBUSDS", 0x51, "a7", ("B", "SS"), 1, False),
    ("VPDPWSSD", 0x52, "b15", ("W", "SU"), 0, False),
    ("VPDPWSSDS", 0x53, "b15", ("W", "SU"), 1, False),
    ("VPDPWSSD", 0x52, "a15", ("W", "US"), 0, False),
    ("VPDPWSSDS", 0x53, "a15", ("W", "US"), 1, False),
    ("VPDPWSSD", 0x52, "ab15", ("W", "UU"), 0, False),
]


def restrict(buf, how, which):
    b = bytearray(buf)
    if how is None:
        return bytes(b)
    if how.endswith("7"):
        if which in how[:-1]:
            for i in range(len(b)):
                b[i] &= 0x7F
    else:
        if which in how[:-2]:
            for i in range(1, len(b), 2):
                b[i] &= 0x7F
    return bytes(b)


def vex_bytes(opc, L, dst, v, rm):
    """VEX.L.66.0F38.W0 opc /r, all registers < 8"""
    b1 = 0xE2                                     # R X B = 1 (not extended), map 0F38
    b2 = (((~v) & 0xF) << 3) | (L << 2) | 1       # W0, vvvv, L, pp = 66
    return bytes([0xC4, b1, b2, opc, 0xC0 | (dst << 3) | rm])


def hw_data():
    """the (form, VEX.L, DEST, SRC1, SRC2) inputs of the hardware cross-check, from a fixed
    seed so that --hwgen and --cases (the EVEX twins) use the same data"""
    global RNG
    saved, RNG = RNG, random.Random(0x13600)
    out = []
    for idx, (name, opc, how, op, sat, swap) in enumerate(HW_FORMS):
        for t in range(60):
            L = t & 1
            vl = 32 if L else 16
            d = acc_vec()[:vl]
            a = restrict(vnni_vec(None, vl), how, "a")
            b = restrict(vnni_vec(None, vl), how, "b")
            out.append((idx, L, d, a, b))
    RNG = saved
    return out


def gen_hw_evex():
    """the EVEX AVX10.2 twin of every hardware cross-check case (same data, same VL): Unicorn's
    EVEX result must equal the model, which --hwcmp checks against the host's VEX result"""
    comment("--- EVEX twins of the VEX AVX-VNNI hardware cross-check data (--hwgen / --hwcmp)")
    for idx, L, d, a, b in hw_data():
        name, opc, how, op, sat, swap = HW_FORMS[idx]
        vl = 32 if L else 16
        evex_name = "VPDP%s%sD%s" % (op[0], op[1], "S" if sat else "")
        form = [f for f in VNNI_FORMS if f[0] == evex_name][0]
        s1, s2 = (b, a) if swap else (a, b)
        c = Case("%s VL%d twin of %s(%s)" % (evex_name, vl * 8, name, how or "any"))
        old = d + rnd_bytes(64 - vl)
        c.set_zmm(0, old)
        c.set_zmm(1, s1 + rnd_bytes(64 - vl))
        c.set_zmm(2, s2 + rnd_bytes(64 - vl))
        c.code = evex(2, form[2], 0, form[1], 0, 2, vvvv=1, ll=L)
        img = model_vnni(op, sat, old, s1 + bytes(64 - vl), s2 + bytes(64 - vl), vl, None, 0)
        c.exp.append("zmm0=%s" % hexs(img))
        emit(c)


def hwcheck_gen(out, expect_path):
    import json
    exp = []
    for idx, L, d, a, b in hw_data():
        name, opc, how, op, sat, swap = HW_FORMS[idx]
        vl = 32 if L else 16
        code = vex_bytes(opc, L, 0, 1, 2)
        fields = "xmm0=%s xmm1=%s xmm2=%s" % (hexs(d[:16]), hexs(a[:16]), hexs(b[:16]))
        if L:
            fields += " ymmh0=%s ymmh1=%s ymmh2=%s" % (hexs(d[16:]), hexs(a[16:]), hexs(b[16:]))
        out.write("%s | %s\n" % (byte_list(code), fields))
        # the model's EVEX form on the same data (EVEX.vl = VEX.vl; masks none)
        s1, s2 = (b, a) if swap else (a, b)
        img = model_vnni(op, sat, d + bytes(64 - vl), s1 + bytes(64 - vl), s2 + bytes(64 - vl), vl, None, 0)
        exp.append({"form": idx, "vl": vl, "xmm0": hexs(img[:16]), "ymmh0": hexs(img[16:32]) if L else None})
    json.dump(exp, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    import json
    import re
    exp = json.load(open(expect_path))
    cur, bad, seen = None, 0, 0
    inp = {}
    per = {}
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] \S+ \.byte [^|]*\| (.*)$", line)
        if m:
            cur = int(m.group(1))
            inp = dict(x.split("=", 1) for x in m.group(2).split() if "=" in x)
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if m and cur is not None:
            fields = m.group(1)
            if fields.startswith("fault"):
                got_x, got_h = "FAULT", "FAULT"
            else:
                kv = dict(x.split("=", 1) for x in fields.split() if "=" in x)
                got_x = kv.get("xmm0", inp.get("xmm0"))
                got_h = kv.get("ymmh0", inp.get("ymmh0"))
            e = exp[cur]
            seen += 1
            key = "%s(%s) vs EVEX %s%s" % (HW_FORMS[e["form"]][0], HW_FORMS[e["form"]][2] or "any",
                                          "VPDP%s%sD%s" % (HW_FORMS[e["form"]][3][0], HW_FORMS[e["form"]][3][1],
                                                           "S" if HW_FORMS[e["form"]][4] else ""),
                                          " (swapped)" if HW_FORMS[e["form"]][5] else "")
            ok = got_x.upper() == e["xmm0"].upper() and (e["ymmh0"] is None or
                                                         (got_h or "").upper() == e["ymmh0"].upper())
            per.setdefault(key, [0, 0])
            per[key][0] += 1
            if not ok:
                per[key][1] += 1
                bad += 1
                if bad <= 20:
                    print("[%d] %s: hw xmm0=%s ymmh0=%s | model xmm0=%s ymmh0=%s" % (
                        cur, key, got_x, got_h, e["xmm0"], e["ymmh0"]))
            cur = None
    for k in sorted(per):
        print("  %-60s %4d cases, %d differ" % (k, per[k][0], per[k][1]))
    print("hwcheck: %d cases compared, %d differ from the model" % (seen, bad))
    return bad == 0 and seen == len(exp)


# ======================================================================================
# self test
# ======================================================================================
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            ok = False
            print("FAIL %s: got %r want %r" % (name, got, want))

    # Table 3.5 values
    chk("bf8 max 57344", cvt_fp16_to_fp8(0x7B00, BF8, 0), 0x7B)
    chk("hf8 448", cvt_fp16_to_fp8(0x5F00, HF8, 0), 0x7E)
    chk("hf8 min normal 2^-6", cvt_fp16_to_fp8(0x2400, HF8, 0), 0x08)
    chk("hf8 min denormal 2^-9", cvt_fp16_to_fp8(0x1800, HF8, 0), 0x01)
    chk("bf8 min denormal 2^-16", cvt_fp16_to_fp8(0x0100, BF8, 0), 0x01)
    # RNE ties
    chk("hf8 464 tie -> 448", cvt_fp16_to_fp8(0x5F40, HF8, 0), 0x7E)
    chk("hf8 465 -> NaN", cvt_fp16_to_fp8(0x5F41, HF8, 0), 0x7F)
    chk("hf8S 465 -> max", cvt_fp16_to_fp8(0x5F41, HF8, 1), 0x7E)
    chk("bf8 61440 tie -> Inf", cvt_fp16_to_fp8(0x7B80, BF8, 0), 0x7C)
    chk("bf8S 61440 -> max", cvt_fp16_to_fp8(0x7B80, BF8, 1), 0x7B)
    chk("bf8 1+2^-3 tie even down", cvt_fp16_to_fp8(0x3C80, BF8, 0), 0x3C)
    chk("bf8 1.25+2^-3 tie even up", cvt_fp16_to_fp8(0x3D80, BF8, 0), 0x3E)
    chk("bf8 Inf", cvt_fp16_to_fp8(0xFC00, BF8, 0), 0xFC)
    chk("bf8S -Inf", cvt_fp16_to_fp8(0xFC00, BF8, 1), 0xFB)
    chk("bf8 SNaN quieted", cvt_fp16_to_fp8(0x7C01, BF8, 0), 0x7E)
    chk("hf8 Inf -> NaN", cvt_fp16_to_fp8(0x7C00, HF8, 0), 0x7F)
    chk("hf8S Inf -> max", cvt_fp16_to_fp8(0xFC00, HF8, 1), 0xFE)
    chk("hf8 -0", cvt_fp16_to_fp8(0x8000, HF8, 0), 0x80)
    # bias examples of 9.3.2: 0 = RZ, 255 = away from zero
    chk("bf8 bias 0 truncates", cvt_fp16_to_fp8_bias(0x3CFF, 0, BF8, 0), 0x3C)
    chk("bf8 bias 255 rounds away", cvt_fp16_to_fp8_bias(0x3C01, 255, BF8, 0), 0x3D)
    chk("bf8 bias 255 exact stays", cvt_fp16_to_fp8_bias(0x3C00, 255, BF8, 0), 0x3C)
    chk("hf8 bias 255 rounds away", cvt_fp16_to_fp8_bias(0x3C01, 255, HF8, 0), 0x39)
    chk("hf8 FP16-denormal bias 255 -> 2^-9", cvt_fp16_to_fp8_bias(0x03FF, 255, HF8, 0), 0x01)
    chk("hf8 FP16-denormal bias 255 -> 0", cvt_fp16_to_fp8_bias(0x0001, 255, HF8, 0), 0x00)
    # HF8 -> FP16
    chk("hf8->fp16 1.0", cvt_hf8_to_fp16(0x38), 0x3C00)
    chk("hf8->fp16 NaN", cvt_hf8_to_fp16(0xFF), 0xFF80)
    chk("hf8->fp16 2^-9", cvt_hf8_to_fp16(0x01), 0x1800)
    chk("hf8->fp16 448", cvt_hf8_to_fp16(0x7E), 0x5F00)
    # FP32 -> FP16
    chk("fp32->fp16 1.0", cvt_fp32_to_fp16(0x3F800000, 0x1F80), (0x3C00, 0))
    chk("fp32->fp16 65520 -> Inf", cvt_fp32_to_fp16(0x477FF000, 0x1F80), (0x7C00, OE | PE))
    chk("fp32->fp16 65520 RZ -> max, no overflow", cvt_fp32_to_fp16(0x477FF000, 0x7F80), (0x7BFF, PE))
    chk("fp32->fp16 65536 RZ -> max, overflow", cvt_fp32_to_fp16(0x47800000, 0x7F80), (0x7BFF, OE | PE))
    chk("fp32->fp16 2^-25 tie -> 0", cvt_fp32_to_fp16(0x33000000, 0x1F80), (0x0000, UE | PE))
    chk("fp32->fp16 denormal DE", cvt_fp32_to_fp16(0x00000001, 0x1F80), (0x0000, DE | UE | PE))
    chk("fp32->fp16 denormal RU", cvt_fp32_to_fp16(0x00000001, 0x5F80), (0x0001, DE | UE | PE))
    chk("fp32->fp16 denormal DAZ", cvt_fp32_to_fp16(0x80000001, 0x1FC0), (0x8000, 0))
    chk("fp32->fp16 SNaN", cvt_fp32_to_fp16(0x7FA00001, 0x1F80), (0x7F00, IE))
    # VMPSADBW spec example shape: zero data -> zero
    chk("mpsadbw zeros", model_mpsadbw(bytes(64), bytes(64), bytes(64), 16, None, 0, 0)[:16], bytes(16))
    # exhaustive: rational model == literal spec 5.1 pseudocode
    bad = 0
    for x in range(65536):
        for sat in (0, 1):
            if cvt_fp16_to_fp8(x, BF8, sat) != pc_bf8(x, sat):
                bad += 1
                if bad < 10:
                    print("bf8 %04X s=%d model %02X pseudocode %02X" % (x, sat, cvt_fp16_to_fp8(x, BF8, sat),
                                                                         pc_bf8(x, sat)))
            if cvt_fp16_to_fp8(x, HF8, sat) != pc_hf8(x, sat):
                bad += 1
                if bad < 10:
                    print("hf8 %04X s=%d model %02X pseudocode %02X" % (x, sat, cvt_fp16_to_fp8(x, HF8, sat),
                                                                         pc_hf8(x, sat)))
    print("exhaustive RNE: 4 x 65536 inputs, %d differ" % bad)
    ok = ok and bad == 0
    bad = 0
    biases = list(range(256)) if "--full" in sys.argv else [0, 1, 2, 3, 0x3F, 0x40, 0x7E, 0x7F, 0x80, 0x81,
                                                              0xBF, 0xC0, 0xFE, 0xFF, 0x55, 0xAA]
    for b in biases:
        for x in range(65536):
            for sat in (0, 1):
                if cvt_fp16_to_fp8_bias(x, b, BF8, sat) != pc_bf8_bias(x, b, sat):
                    bad += 1
                    if bad < 10:
                        print("bf8 bias %04X b=%02X s=%d model %02X pseudocode %02X" % (
                            x, b, sat, cvt_fp16_to_fp8_bias(x, b, BF8, sat), pc_bf8_bias(x, b, sat)))
                if cvt_fp16_to_fp8_bias(x, b, HF8, sat) != pc_hf8_bias(x, b, sat):
                    bad += 1
                    if bad < 10:
                        print("hf8 bias %04X b=%02X s=%d model %02X pseudocode %02X" % (
                            x, b, sat, cvt_fp16_to_fp8_bias(x, b, HF8, sat), pc_hf8_bias(x, b, sat)))
    print("exhaustive BIAS: 4 x 65536 inputs x %d biases, %d differ" % (len(biases), bad))
    ok = ok and bad == 0
    bad = sum(1 for x in range(256) if cvt_hf8_to_fp16(x) != pc_hf8_to_fp16(x))
    print("exhaustive HF8->FP16: 256 inputs, %d differ" % bad)
    ok = ok and bad == 0
    return ok


def main():
    if "--hwgen" in sys.argv:
        hwcheck_gen(sys.stdout, sys.argv[sys.argv.index("--hwgen") + 1])
        return
    if "--hwcmp" in sys.argv:
        i = sys.argv.index("--hwcmp")
        sys.exit(0 if hwcheck_cmp(sys.argv[i + 1], sys.argv[i + 2]) else 1)
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_fp8()
        gen_cvthf82ph()
        gen_cvt2ps2phx()
        gen_vnni()
        gen_vdpphps()
        gen_mpsadbw()
        gen_movrs()
        gen_movz()
        gen_sm4()
        gen_misc()
        gen_hw_evex()
        if "--exhaustive" in sys.argv:
            gen_fp8_exhaustive()
        out = sys.stdout
        out.write("# AVX10.2 (avx10_b, ledger U400-U412): expected values from the independent model\n")
        out.write("# Emulator/tools/isa/ref_avx10_b.py --cases (regenerate, do not edit). The i5-13600K has no\n")
        out.write("# AVX10.2 / AVX-512: expected-value cases only, run with AVX10.2 enabled:\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_avx10_b.txt --avx10 --xcr0 0xE7 --expect-only\n")
        out.write("# RSI = R14 = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).\n")
        for c in cases:
            if isinstance(c, str):
                out.write(c + "\n")
            else:
                out.write("# " + c.title + "\n")
                out.write(c.line() + "\n")
        return
    print(__doc__)


if __name__ == "__main__":
    main()
