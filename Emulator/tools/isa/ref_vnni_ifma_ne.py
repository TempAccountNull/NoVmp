#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_vnni_ifma_ne.py -- independent reference model (Python 3 stdlib only)

Literal transcription of the Intel SDM Vol. 2 "Operation" pseudocode for the
VEX-encoded forms of:

  AVX-VNNI-INT8   VPDPBSSD[S] VPDPBSUD[S] VPDPBUUD[S]      (0F38 50/51, W0)
  AVX-VNNI-INT16  VPDPWSUD[S] VPDPWUSD[S] VPDPWUUD[S]      (0F38 D2/D3, W0)
  AVX-IFMA        VPMADD52LUQ VPMADD52HUQ                  (66 0F38 B4/B5, W1)
  AVX-NE-CONVERT  VBCSTNEBF162PS VBCSTNESH2PS               (0F38 B1, W0, m16)
                  VCVTNEEBF162PS VCVTNEEPH2PS               (0F38 B0, W0, mem)
                  VCVTNEOBF162PS VCVTNEOPH2PS               (0F38 B0, W0, mem)
                  VCVTNEPS2BF16                             (F3 0F38 72, W0)

All vectors are bytes in little-endian memory order; 16 bytes = VL 128,
32 bytes = VL 256.  Every instruction function returns the full 32-byte YMM
image of the destination (bits above VL zeroed as the SDM says
"DEST[MAXVL-1:VL] := 0"; MAXVL is treated as 256 since only VEX exists here).

Usage:
  python ref_vnni_ifma_ne.py --selftest   hand-derived edge checks, exit 0 on pass
  python ref_vnni_ifma_ne.py --cases      emulator case-file lines (stdout)
  python ref_vnni_ifma_ne.py --cvectors   C table rows for the same cases
  python ref_vnni_ifma_ne.py --cinc       unicorn/tests/unit/x86_vnni_ifma_ne_vectors.inc

This file was written from the SDM text only (not from any C implementation).
"""

import struct
import sys

YMM = 32
MASK32 = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF


# --------------------------------------------------------------------------
# small element helpers (memory order = little endian)
# --------------------------------------------------------------------------

def _vl_check(*vecs):
    n = len(vecs[0])
    if n not in (16, 32):
        raise ValueError("vector length must be 16 or 32 bytes, got %d" % n)
    for v in vecs:
        if len(v) != n:
            raise ValueError("operand lengths differ: %r" % [len(x) for x in vecs])
    return n * 8  # VL in bits


def _byte(v, i):
    return v[i]


def _word(v, i):
    return v[2 * i] | (v[2 * i + 1] << 8)


def _dword(v, i):
    return struct.unpack_from("<I", v, 4 * i)[0]


def _qword(v, i):
    return struct.unpack_from("<Q", v, 8 * i)[0]


def _sign_extend(x, bits):
    x &= (1 << bits) - 1
    return x - (1 << bits) if x & (1 << (bits - 1)) else x


def _zero_extend(x, bits):
    return x & ((1 << bits) - 1)


def _ymm_image(lowbytes):
    """Pad the VL-sized result with zeros up to 256 bits (DEST[MAXVL-1:VL] := 0)."""
    b = bytes(lowbytes)
    if len(b) > YMM:
        raise ValueError("result wider than YMM")
    return b + bytes(YMM - len(b))


def SIGNED_DWORD_SATURATE(x):
    # SDM description: > 7FFF_FFFFH -> 7FFF_FFFFH, < 8000_0000H (i.e. -2^31) -> 8000_0000H
    if x > 0x7FFFFFFF:
        x = 0x7FFFFFFF
    elif x < -0x80000000:
        x = -0x80000000
    return x & MASK32


def UNSIGNED_DWORD_SATURATE(x):
    # SDM description: > FFFF_FFFFH -> FFFF_FFFFH.  (Below 0 cannot occur for the
    # UU forms because all products are non-negative; clamp to 0 for totality.)
    if x > 0xFFFFFFFF:
        x = 0xFFFFFFFF
    elif x < 0:
        x = 0
    return x


# --------------------------------------------------------------------------
# AVX-VNNI-INT8: VPDPB[SU,UU,SS]D[,S]
# --------------------------------------------------------------------------

def vpdpb(kind, sat, dst, src1, src2):
    """kind 'SS','SU','UU': first letter = src1 (VEX.vvvv) signedness,
    second letter = src2 (ModRM.r/m) signedness. sat -> the ...DS form."""
    if kind not in ("SS", "SU", "UU"):
        raise ValueError("vpdpb kind must be SS, SU or UU")
    VL = _vl_check(dst, src1, src2)
    KL = VL // 32
    ORIGDEST = bytes(dst)
    out = bytearray(VL // 8)
    # IF *src1 is signed*: SIGN_EXTEND // SU, SS  ELSE ZERO_EXTEND // UU
    src1extend = _sign_extend if kind[0] == "S" else _zero_extend
    # IF *src2 is signed*: SIGN_EXTEND // SS      ELSE ZERO_EXTEND // UU, SU
    src2extend = _sign_extend if kind[1] == "S" else _zero_extend
    for i in range(KL):
        # products are kept as exact integers (see report: "p1word" naming)
        p1word = src1extend(_byte(src1, 4 * i + 0), 8) * src2extend(_byte(src2, 4 * i + 0), 8)
        p2word = src1extend(_byte(src1, 4 * i + 1), 8) * src2extend(_byte(src2, 4 * i + 1), 8)
        p3word = src1extend(_byte(src1, 4 * i + 2), 8) * src2extend(_byte(src2, 4 * i + 2), 8)
        p4word = src1extend(_byte(src1, 4 * i + 3), 8) * src2extend(_byte(src2, 4 * i + 3), 8)
        od = _dword(ORIGDEST, i)
        if sat:
            if kind == "UU":
                # ORIGDEST taken as unsigned dword
                r = UNSIGNED_DWORD_SATURATE(od + p1word + p2word + p3word + p4word)
            else:
                # ORIGDEST taken as signed dword
                r = SIGNED_DWORD_SATURATE(_sign_extend(od, 32) + p1word + p2word + p3word + p4word)
        else:
            r = (od + p1word + p2word + p3word + p4word) & MASK32
        struct.pack_into("<I", out, 4 * i, r)
    return _ymm_image(out)


# --------------------------------------------------------------------------
# AVX-VNNI-INT16: VPDPW[SU,US,UU]D[,S]
# --------------------------------------------------------------------------

def vpdpw(kind, sat, dst, src1, src2):
    """kind 'SU','US','UU' (first letter = src1, second = src2), per the SDM
    Operation section ("IF *src1 is signed*: // SU", "IF *src2 is signed*: // US")."""
    if kind not in ("SU", "US", "UU"):
        raise ValueError("vpdpw kind must be SU, US or UU")
    VL = _vl_check(dst, src1, src2)
    KL = VL // 32
    ORIGDEST = bytes(dst)
    out = bytearray(VL // 8)
    src1extend = _sign_extend if kind[0] == "S" else _zero_extend
    src2extend = _sign_extend if kind[1] == "S" else _zero_extend
    for i in range(KL):
        p1dword = src1extend(_word(src1, 2 * i + 0), 16) * src2extend(_word(src2, 2 * i + 0), 16)
        p2dword = src1extend(_word(src1, 2 * i + 1), 16) * src2extend(_word(src2, 2 * i + 1), 16)
        od = _dword(ORIGDEST, i)
        if sat:
            if kind == "UU":
                r = UNSIGNED_DWORD_SATURATE(od + p1dword + p2dword)
            else:
                r = SIGNED_DWORD_SATURATE(_sign_extend(od, 32) + p1dword + p2dword)
        else:
            r = (od + p1dword + p2dword) & MASK32
        struct.pack_into("<I", out, 4 * i, r)
    return _ymm_image(out)


# --------------------------------------------------------------------------
# AVX-IFMA: VPMADD52LUQ / VPMADD52HUQ (VEX)
# --------------------------------------------------------------------------

def vpmadd52(hi, dst, src1, src2):
    VL = _vl_check(dst, src1, src2)
    KL = VL // 64
    out = bytearray(VL // 8)
    for i in range(KL):
        # temp128 := zeroextend64(src1.qword[i][51:0]) * zeroextend64(src2.qword[i][51:0])
        temp128 = (_qword(src1, i) & ((1 << 52) - 1)) * (_qword(src2, i) & ((1 << 52) - 1))
        if hi:
            add = (temp128 >> 52) & ((1 << 52) - 1)  # temp128[103:52]
        else:
            add = temp128 & ((1 << 52) - 1)  # temp128[51:0]
        # srcdest.qword[i] := srcdest.qword[i] + zeroextend64(...)   (64-bit wrap)
        r = (_qword(dst, i) + add) & MASK64
        struct.pack_into("<Q", out, 8 * i, r)
    return _ymm_image(out)


# --------------------------------------------------------------------------
# AVX-NE-CONVERT scalar helpers
# --------------------------------------------------------------------------

def bf16_to_fp32(x):
    """SDM make_fp32: dword := 0; dword[31:16] := x."""
    return (x & 0xFFFF) << 16


def fp32_to_bf16(x):
    """SDM 'Define convert_fp32_to_bfloat16(x)' (VCVTNEPS2BF16), literally."""
    x &= MASK32
    exp = (x >> 23) & 0xFF
    frac = x & 0x7FFFFF
    if exp == 0:  # x is zero or denormal
        dest = (x >> 31) << 15  # dest[15] := x[31]; dest[14:0] := 0
    elif exp == 0xFF and frac == 0:  # infinity
        dest = (x >> 16) & 0xFFFF  # dest[15:0] := x[31:16]
    elif exp == 0xFF:  # NaN
        dest = (x >> 16) & 0xFFFF  # truncate
        dest |= 1 << 6  # dest[6] := 1 (MSB of the bf16 mantissa)
    else:  # normal number
        LSB = (x >> 16) & 1
        rounding_bias = 0x00007FFF + LSB
        temp = (x + rounding_bias) & MASK32  # integer add, 32-bit
        dest = (temp >> 16) & 0xFFFF
    return dest


def fp16_to_fp32(h):
    """Exact IEEE binary16 -> binary32 by bit manipulation.  Denormals are
    normalized (never DAZ), +-0/+-inf preserved, NaN: sign kept, payload << 13,
    quiet bit (fp32 bit 22) forced to 1 (SNaN gets quieted)."""
    h &= 0xFFFF
    s = h >> 15
    e = (h >> 10) & 0x1F
    m = h & 0x3FF
    if e == 0:
        if m == 0:
            return s << 31
        # denormal: value = m * 2^-24.  Normalize.
        e32 = 127 - 15 + 1  # exponent of 2^-14 (the denormal scale) = 113
        while not (m & 0x400):
            m <<= 1
            e32 -= 1
        m &= 0x3FF
        return (s << 31) | (e32 << 23) | (m << 13)
    if e == 0x1F:
        if m == 0:
            return (s << 31) | 0x7F800000
        return (s << 31) | 0x7F800000 | 0x00400000 | (m << 13)
    return (s << 31) | ((e - 15 + 127) << 23) | (m << 13)


# --------------------------------------------------------------------------
# AVX-NE-CONVERT vector instructions
# --------------------------------------------------------------------------

def bcstne(kind, m16, vl_bytes):
    """VBCSTNEBF162PS (kind 'bf16') / VBCSTNESH2PS (kind 'fp16'), m16 = 2 bytes."""
    if kind not in ("bf16", "fp16"):
        raise ValueError("bcstne kind must be bf16 or fp16")
    if len(m16) != 2:
        raise ValueError("m16 must be 2 bytes")
    if vl_bytes not in (16, 32):
        raise ValueError("vl_bytes must be 16 or 32")
    VL = vl_bytes * 8
    KL = VL // 32
    tmp = [0] * KL
    for i in range(KL):
        tmp[i] = m16[0] | (m16[1] << 8)  # tmp.dword[i].word[0] = src.word[0]
    out = bytearray(vl_bytes)
    for i in range(KL):
        if kind == "bf16":
            r = bf16_to_fp32(tmp[i])  # make_fp32
        else:
            r = fp16_to_fp32(tmp[i])  # convert_fp16_to_fp32
        struct.pack_into("<I", out, 4 * i, r)
    return _ymm_image(out)


def cvtne_even_odd(kind, odd, src_bytes):
    """VCVTNE[E|O]BF162PS (kind 'bf16') / VCVTNE[E|O]PH2PS (kind 'fp16')."""
    if kind not in ("bf16", "fp16"):
        raise ValueError("cvtne kind must be bf16 or fp16")
    VL = _vl_check(src_bytes)
    KL = VL // 32
    out = bytearray(VL // 8)
    for i in range(KL):
        w = _word(src_bytes, 2 * i + (1 if odd else 0))  # src.dword[i].word[0 or 1]
        r = bf16_to_fp32(w) if kind == "bf16" else fp16_to_fp32(w)
        struct.pack_into("<I", out, 4 * i, r)
    return _ymm_image(out)


def cvtneps2bf16(src_bytes):
    """VCVTNEPS2BF16 (VEX). src 16 bytes (VEX.128) or 32 bytes (VEX.256).
    Returns the full 32-byte YMM image; the result lives in the low VL/2 bits
    (low 8 bytes for VEX.128, low 16 bytes for VEX.256), rest zero."""
    VL = _vl_check(src_bytes)
    KL = VL // 16
    out = bytearray(VL // 16)  # VL/2 bits
    for i in range(KL // 2):
        t = _dword(src_bytes, i)
        struct.pack_into("<H", out, 2 * i, fp32_to_bf16(t))
    return _ymm_image(out)  # DEST[MAXVL-1:VL/2] := 0


# --------------------------------------------------------------------------
# self-test
# --------------------------------------------------------------------------

class _T:
    def __init__(self):
        self.n = 0
        self.fail = 0

    def eq(self, name, got, exp):
        self.n += 1
        ok = got == exp
        if not ok:
            self.fail += 1
        if isinstance(got, (bytes, bytearray)):
            gs, es = bytes(got).hex().upper(), bytes(exp).hex().upper()
        else:
            gs, es = "0x%X" % got, "0x%X" % exp
        print("%s  %-62s got=%s exp=%s" % ("PASS" if ok else "FAIL", name, gs, es))
        return ok


def _lane_vec_b(a4):
    return bytes(a4) + bytes(12)


def _d(v):
    return struct.pack("<I", v & MASK32)


def _dpb1(kind, sat, dest, a4, b4):
    """Run one dword lane through the full 128-bit vector function."""
    r = vpdpb(kind, sat, _d(dest) + bytes(12), _lane_vec_b(a4), _lane_vec_b(b4))
    return _dword(r, 0)


def _dpw1(kind, sat, dest, a2, b2):
    s1 = struct.pack("<HH", *a2) + bytes(12)
    s2 = struct.pack("<HH", *b2) + bytes(12)
    r = vpdpw(kind, sat, _d(dest) + bytes(12), s1, s2)
    return _dword(r, 0)


def _m52(hi, dest, a, b):
    r = vpmadd52(hi, struct.pack("<QQ", dest, 0), struct.pack("<QQ", a, 0), struct.pack("<QQ", b, 0))
    return _qword(r, 0)


def _lcg(seed):
    x = seed & MASK64
    while True:
        x = (x * 6364136223846793005 + 1442695040888963407) & MASK64
        yield x >> 32


def selftest():
    t = _T()
    print("== VPDPB* (AVX-VNNI-INT8) ==")
    # SS: -128 * -128 = 16384 = 0x4000, four of them = 65536 = 0x10000
    t.eq("VPDPBSSD  0 + 4*(-128*-128)", _dpb1("SS", False, 0, [0x80] * 4, [0x80] * 4), 0x00010000)
    # SSDS upper boundary: 0x7FFEFFFF + 0x10000 = 0x7FFFFFFF exactly -> no saturation
    t.eq("VPDPBSSDS 0x7FFEFFFF + 0x10000 (exact max)", _dpb1("SS", True, 0x7FFEFFFF, [0x80] * 4, [0x80] * 4), 0x7FFFFFFF)
    # one past: 0x7FFF0000 + 0x10000 = 2^31 -> saturates to 0x7FFFFFFF; SSD wraps to 0x80000000
    t.eq("VPDPBSSDS 0x7FFF0000 + 0x10000 (max+1 -> sat)", _dpb1("SS", True, 0x7FFF0000, [0x80] * 4, [0x80] * 4), 0x7FFFFFFF)
    t.eq("VPDPBSSD  0x7FFF0000 + 0x10000 (wrap)", _dpb1("SS", False, 0x7FFF0000, [0x80] * 4, [0x80] * 4), 0x80000000)
    # lower boundary: -128*127 = -16256, x4 = -65024 = -0xFE00.
    # dest 0x8000FE00 = -2^31 + 0xFE00 -> sum = -2^31 exactly = 0x80000000
    t.eq("VPDPBSSDS 0x8000FE00 - 0xFE00 (exact min)", _dpb1("SS", True, 0x8000FE00, [0x80] * 4, [0x7F] * 4), 0x80000000)
    # dest 0x8000FDFF -> -2^31 - 1 -> sat 0x80000000; SSD wraps to 0x7FFFFFFF
    t.eq("VPDPBSSDS 0x8000FDFF - 0xFE00 (min-1 -> sat)", _dpb1("SS", True, 0x8000FDFF, [0x80] * 4, [0x7F] * 4), 0x80000000)
    t.eq("VPDPBSSD  0x8000FDFF - 0xFE00 (wrap)", _dpb1("SS", False, 0x8000FDFF, [0x80] * 4, [0x7F] * 4), 0x7FFFFFFF)
    # SS: src2 signed too: 0xFF*0xFF = (-1)*(-1) = 1, x4 = 4
    t.eq("VPDPBSSD  4*((-1)*(-1))", _dpb1("SS", False, 0, [0xFF] * 4, [0xFF] * 4), 4)
    # SU: src1 0x7F = 127 (signed), src2 0xFF = 255 (unsigned): 32385 x4 = 129540 = 0x1FA04
    t.eq("VPDPBSUD  0 + 4*(127*255)", _dpb1("SU", False, 0, [0x7F] * 4, [0xFF] * 4), 0x0001FA04)
    # 0x7FFE05FB + 0x1FA04 = 0x7FFFFFFF exact
    t.eq("VPDPBSUDS 0x7FFE05FB + 0x1FA04 (exact max)", _dpb1("SU", True, 0x7FFE05FB, [0x7F] * 4, [0xFF] * 4), 0x7FFFFFFF)
    t.eq("VPDPBSUDS 0x7FFE05FC + 0x1FA04 (max+1 -> sat)", _dpb1("SU", True, 0x7FFE05FC, [0x7F] * 4, [0xFF] * 4), 0x7FFFFFFF)
    t.eq("VPDPBSUDS 0x7FFFFFFF + 0x1FA04 (sat)", _dpb1("SU", True, 0x7FFFFFFF, [0x7F] * 4, [0xFF] * 4), 0x7FFFFFFF)
    t.eq("VPDPBSUD  0x7FFFFFFF + 0x1FA04 (wrap)", _dpb1("SU", False, 0x7FFFFFFF, [0x7F] * 4, [0xFF] * 4), 0x8001FA03)
    # SU: src1 0xFF = -1, src2 0xFF = 255 -> -255 x4 = -1020 = 0xFFFFFC04 (if src2 were signed: +4)
    t.eq("VPDPBSUD  4*((-1)*255) sign discriminator", _dpb1("SU", False, 0, [0xFF] * 4, [0xFF] * 4), 0xFFFFFC04)
    # SU min: -128*255 = -32640, x4 = -130560 = -0x1FE00. dest 0x80000000 -> sat; wrap = 0x7FFE0200
    t.eq("VPDPBSUDS 0x80000000 - 0x1FE00 (sat min)", _dpb1("SU", True, 0x80000000, [0x80] * 4, [0xFF] * 4), 0x80000000)
    t.eq("VPDPBSUD  0x80000000 - 0x1FE00 (wrap)", _dpb1("SU", False, 0x80000000, [0x80] * 4, [0xFF] * 4), 0x7FFE0200)
    # UU: 255*255 = 65025, x4 = 260100 = 0x3F804
    # 0xFFFFFF00 + 0x3F804 = 0x1_0003_F704 -> UUDS sat 0xFFFFFFFF, UUD wraps 0x0003F704
    t.eq("VPDPBUUDS 0xFFFFFF00 + 0x3F804 (sat)", _dpb1("UU", True, 0xFFFFFF00, [0xFF] * 4, [0xFF] * 4), 0xFFFFFFFF)
    t.eq("VPDPBUUD  0xFFFFFF00 + 0x3F804 (wrap)", _dpb1("UU", False, 0xFFFFFF00, [0xFF] * 4, [0xFF] * 4), 0x0003F704)
    # 0xFFFC07FB + 0x3F804 = 0xFFFFFFFF exactly; 0xFFFC07FC is one past
    t.eq("VPDPBUUDS 0xFFFC07FB + 0x3F804 (exact max)", _dpb1("UU", True, 0xFFFC07FB, [0xFF] * 4, [0xFF] * 4), 0xFFFFFFFF)
    t.eq("VPDPBUUDS 0xFFFC07FC + 0x3F804 (max+1 -> sat)", _dpb1("UU", True, 0xFFFC07FC, [0xFF] * 4, [0xFF] * 4), 0xFFFFFFFF)
    t.eq("VPDPBUUD  0xFFFC07FC + 0x3F804 (wrap to 0)", _dpb1("UU", False, 0xFFFC07FC, [0xFF] * 4, [0xFF] * 4), 0x00000000)
    # ORIGDEST unsigned for UUDS: 0x7FFFFFFF + 0x3F804 = 0x8003F803 (no saturation; a
    # signed-saturating implementation would wrongly give 0x7FFFFFFF)
    t.eq("VPDPBUUDS 0x7FFFFFFF + 0x3F804 (unsigned dest)", _dpb1("UU", True, 0x7FFFFFFF, [0xFF] * 4, [0xFF] * 4), 0x8003F803)
    # mixed lane: SS bytes (1,-1,127,-128)*( -1,-1,-128,127) = -1 + 1 - 16256 - 16256 = -32512
    t.eq("VPDPBSSD  mixed (1,-1,127,-128)x(-1,-1,-128,127)", _dpb1("SS", False, 0, [0x01, 0xFF, 0x7F, 0x80], [0xFF, 0xFF, 0x80, 0x7F]), (-32512) & MASK32)

    print("== VPDPW* (AVX-VNNI-INT16) ==")
    # SU: src1 0x8000 = -32768 (signed), src2 0xFFFF = 65535 (unsigned)
    #   product = -2147450880 = -0x7FFF8000; two = -0xFFFF0000
    #   dest 0 -> wrap 2^32 - 0xFFFF0000 = 0x00010000, signed-sat -> 0x80000000
    t.eq("VPDPWSUD  2*(0x8000s*0xFFFFu) wrap", _dpw1("SU", False, 0, (0x8000, 0x8000), (0xFFFF, 0xFFFF)), 0x00010000)
    t.eq("VPDPWSUDS 2*(0x8000s*0xFFFFu) sat", _dpw1("SU", True, 0, (0x8000, 0x8000), (0xFFFF, 0xFFFF)), 0x80000000)
    # US: src1 0xFFFF = 65535 (unsigned), src2 0x8000 = -32768 (signed) -> same numbers
    t.eq("VPDPWUSD  2*(0xFFFFu*0x8000s) wrap", _dpw1("US", False, 0, (0xFFFF, 0xFFFF), (0x8000, 0x8000)), 0x00010000)
    t.eq("VPDPWUSDS 2*(0xFFFFu*0x8000s) sat", _dpw1("US", True, 0, (0xFFFF, 0xFFFF), (0x8000, 0x8000)), 0x80000000)
    # operand-role discriminator: src1 = 0xFFFF, src2 = 2
    #   SU: (-1)*2 = -2 = 0xFFFFFFFE ; US: 65535*2 = 0x1FFFE
    t.eq("VPDPWSUD  0xFFFF*2 -> -2 (src1 signed)", _dpw1("SU", False, 0, (0xFFFF, 0), (2, 0)), 0xFFFFFFFE)
    t.eq("VPDPWUSD  0xFFFF*2 -> 0x1FFFE (src1 unsigned)", _dpw1("US", False, 0, (0xFFFF, 0), (2, 0)), 0x0001FFFE)
    # SU positive: 0x7FFF*0xFFFF = 32767*65535 = 2147385345 = 0x7FFE8001
    #   dest 0x17FFE + 0x7FFE8001 = 0x7FFFFFFF exactly; 0x17FFF -> one past
    t.eq("VPDPWSUDS 0x17FFE + 0x7FFE8001 (exact max)", _dpw1("SU", True, 0x17FFE, (0x7FFF, 0), (0xFFFF, 0)), 0x7FFFFFFF)
    t.eq("VPDPWSUDS 0x17FFF + 0x7FFE8001 (max+1 -> sat)", _dpw1("SU", True, 0x17FFF, (0x7FFF, 0), (0xFFFF, 0)), 0x7FFFFFFF)
    t.eq("VPDPWSUD  0x17FFF + 0x7FFE8001 (wrap)", _dpw1("SU", False, 0x17FFF, (0x7FFF, 0), (0xFFFF, 0)), 0x80000000)
    #   two such products = 0xFFFD0002 > 0x7FFFFFFF -> sat
    t.eq("VPDPWSUDS 2*(0x7FFF*0xFFFF) (sat)", _dpw1("SU", True, 0, (0x7FFF, 0x7FFF), (0xFFFF, 0xFFFF)), 0x7FFFFFFF)
    # UU: 0xFFFF*0xFFFF = 0xFFFE0001, two = 0x1_FFFC_0002
    t.eq("VPDPWUUD  2*(0xFFFF*0xFFFF) wrap", _dpw1("UU", False, 0, (0xFFFF, 0xFFFF), (0xFFFF, 0xFFFF)), 0xFFFC0002)
    t.eq("VPDPWUUDS 2*(0xFFFF*0xFFFF) sat", _dpw1("UU", True, 0, (0xFFFF, 0xFFFF), (0xFFFF, 0xFFFF)), 0xFFFFFFFF)
    #   dest 0x1FFFE + 0xFFFE0001 = 0xFFFFFFFF exact; 0x1FFFF one past (wrap -> 0)
    t.eq("VPDPWUUDS 0x1FFFE + 0xFFFE0001 (exact max)", _dpw1("UU", True, 0x1FFFE, (0xFFFF, 0), (0xFFFF, 0)), 0xFFFFFFFF)
    t.eq("VPDPWUUDS 0x1FFFF + 0xFFFE0001 (max+1 -> sat)", _dpw1("UU", True, 0x1FFFF, (0xFFFF, 0), (0xFFFF, 0)), 0xFFFFFFFF)
    t.eq("VPDPWUUD  0x1FFFF + 0xFFFE0001 (wrap to 0)", _dpw1("UU", False, 0x1FFFF, (0xFFFF, 0), (0xFFFF, 0)), 0x00000000)
    #   dest 0xFFFFFFFF + 0 products -> stays 0xFFFFFFFF (unsigned dest, no clamp)
    t.eq("VPDPWUUDS 0xFFFFFFFF + 0", _dpw1("UU", True, 0xFFFFFFFF, (0, 0), (0, 0)), 0xFFFFFFFF)
    #   UUDS with dest 0x80000000 (unsigned 2^31) + 0xFFFE0001 = 0x17FFE0001 -> sat 0xFFFFFFFF
    t.eq("VPDPWUUDS 0x80000000 + 0xFFFE0001 (sat)", _dpw1("UU", True, 0x80000000, (0xFFFF, 0), (0xFFFF, 0)), 0xFFFFFFFF)

    print("== VPMADD52LUQ / VPMADD52HUQ (AVX-IFMA) ==")
    M52 = (1 << 52) - 1
    # (2^52-1)^2 = 2^104 - 2^53 + 1 = 2^52*(2^52-2) + 1 -> low52 = 1, high52 = 2^52-2
    t.eq("VPMADD52LUQ 0 + lo((2^52-1)^2) = 1", _m52(False, 0, M52, M52), 1)
    t.eq("VPMADD52HUQ 0 + hi((2^52-1)^2) = 2^52-2", _m52(True, 0, M52, M52), 0xFFFFFFFFFFFFE)
    # bits 63:52 of the sources are ignored: all-ones qwords behave like 2^52-1
    t.eq("VPMADD52LUQ garbage 63:52 (all ones)", _m52(False, 0, MASK64, MASK64), 1)
    t.eq("VPMADD52HUQ garbage 63:52 (all ones)", _m52(True, 0, MASK64, MASK64), 0xFFFFFFFFFFFFE)
    # 0xFFF0000000000003 * 0xABC0000000000005 -> 3*5 = 15 low, 0 high
    t.eq("VPMADD52LUQ 0x10 + 3*5 (garbage tops)", _m52(False, 0x10, 0xFFF0000000000003, 0xABC0000000000005), 0x1F)
    t.eq("VPMADD52HUQ 0x10 + hi(3*5)=0", _m52(True, 0x10, 0xFFF0000000000003, 0xABC0000000000005), 0x10)
    # accumulator wraps mod 2^64: 0xFFFF_FFFF_FFFF_FFFF + 1 = 0
    t.eq("VPMADD52LUQ ~0 + 1 (wrap to 0)", _m52(False, MASK64, M52, M52), 0)
    # 0xFFFF_FFFF_FFFF_FFFF + (2^52-2) = 2^52 - 3 mod 2^64 = 0x000F_FFFF_FFFF_FFFD
    t.eq("VPMADD52HUQ ~0 + (2^52-2) (wrap)", _m52(True, MASK64, M52, M52), 0x000FFFFFFFFFFFFD)
    # 2^51 * 2^51 = 2^102: low52 = 0, high52 = 2^50
    t.eq("VPMADD52LUQ 2^51*2^51 low = 0", _m52(False, 0, 1 << 51, 1 << 51), 0)
    t.eq("VPMADD52HUQ 2^51*2^51 high = 2^50", _m52(True, 0, 1 << 51, 1 << 51), 1 << 50)
    # bit 52 alone is outside the field -> product 0
    t.eq("VPMADD52LUQ bit52 only -> 0", _m52(False, 7, 1 << 52, M52), 7)

    print("== convert_fp32_to_bfloat16 ==")
    bf = [
        # 0x3F80_8000: x[16]=0, +0x7FFF = 0x3F80FFFF -> 0x3F80 (tie, round to even)
        (0x3F808000, 0x3F80, "tie -> even (down)"),
        # 0x3F81_8000: x[16]=1, +0x8000 = 0x3F820000 -> 0x3F82 (tie, round to even)
        (0x3F818000, 0x3F82, "tie -> even (up)"),
        # 0x3F80_7FFF + 0x7FFF = 0x3F80FFFE -> 0x3F80
        (0x3F807FFF, 0x3F80, "below half"),
        # 0x3F80_8001 + 0x7FFF = 0x3F810000 -> 0x3F81
        (0x3F808001, 0x3F81, "above half"),
        # 0x7F7F_FFFF: x[16]=1, +0x8000 = 0x7F807FFF -> 0x7F80 (overflow into +inf)
        (0x7F7FFFFF, 0x7F80, "FLT_MAX rounds to +inf"),
        (0xFF7FFFFF, 0xFF80, "-FLT_MAX rounds to -inf"),
        # denormals and zeros -> signed zero
        (0x00400000, 0x0000, "+denormal -> +0"),
        (0x80000001, 0x8000, "-denormal -> -0"),
        (0x007FFFFF, 0x0000, "largest denormal -> +0 (not rounded up)"),
        (0x00000000, 0x0000, "+0"),
        (0x80000000, 0x8000, "-0"),
        # smallest normal: 0x0080_0000 + 0x7FFF -> 0x0080
        (0x00800000, 0x0080, "smallest normal"),
        # infinities: x[31:16]
        (0x7F800000, 0x7F80, "+inf"),
        (0xFF800000, 0xFF80, "-inf"),
        # NaN: x[31:16] then bit 6 set
        (0x7FC00000, 0x7FC0, "QNaN"),
        (0x7F800001, 0x7FC0, "SNaN payload only in low 16 -> 0x7F80|0x40"),
        (0xFF812345, 0xFFC1, "-SNaN 0xFF81|0x40"),
        (0x7FFFFFFF, 0x7FFF, "QNaN all ones (no rounding carry)"),
        (0x3F800000, 0x3F80, "1.0"),
    ]
    for x, e, why in bf:
        t.eq("fp32_to_bf16(0x%08X) %s" % (x, why), fp32_to_bf16(x), e)

    print("== convert_fp16_to_fp32 ==")
    fh = [
        # 0x0001 = 2^-24 -> exp 127-24 = 103 = 0x67 -> 0x33800000
        (0x0001, 0x33800000, "smallest denormal 2^-24"),
        # 0x03FF = 1023*2^-24 = 1.1111111110b * 2^-15 -> exp 112, frac 0x3FE<<13
        (0x03FF, 0x387FC000, "largest denormal"),
        # 0x0400 = 2^-14 -> exp 113 -> 0x38800000
        (0x0400, 0x38800000, "smallest normal 2^-14"),
        (0x8001, 0xB3800000, "-2^-24"),
        (0x7BFF, 0x477FE000, "65504"),
        (0x7C00, 0x7F800000, "+inf"),
        (0xFC00, 0xFF800000, "-inf"),
        # QNaN 0x7E00: payload 0x200<<13 = 0x400000 -> 0x7FC00000
        (0x7E00, 0x7FC00000, "QNaN"),
        # SNaN 0x7C01: payload 1<<13 = 0x2000, quiet bit 0x400000 -> 0x7FC02000
        (0x7C01, 0x7FC02000, "SNaN quieted"),
        (0xFD55, 0xFFEAA000, "-SNaN payload 0x155 -> quieted"),
        (0x8000, 0x80000000, "-0"),
        (0x0000, 0x00000000, "+0"),
        (0x3C00, 0x3F800000, "1.0"),
        (0xC000, 0xC0000000, "-2.0"),
    ]
    for h, e, why in fh:
        t.eq("fp16_to_fp32(0x%04X) %s" % (h, why), fp16_to_fp32(h), e)

    # exhaustive non-NaN cross-check against Python's own binary16 ('e') codec
    bad = 0
    for h in range(0x10000):
        if (h & 0x7C00) == 0x7C00 and (h & 0x3FF):
            r = fp16_to_fp32(h)
            if not ((r & 0x7FC00000) == 0x7FC00000 and (r >> 31) == (h >> 15)
                    and (r & 0x1FFF) == 0 and ((r >> 13) & 0x1FF) == (h & 0x1FF)):
                bad += 1
            continue
        f = struct.unpack("<e", struct.pack("<H", h))[0]
        ref = struct.unpack("<I", struct.pack("<f", f))[0]
        if fp16_to_fp32(h) != ref:
            bad += 1
    t.eq("fp16_to_fp32 all 65536 inputs vs struct 'e' (NaN: sign/quiet/payload)", bad, 0)

    # sampled cross-check of the normal-number path against an independent
    # round-to-nearest-even formulation on the top 16 bits
    bad = 0
    g = _lcg(0x5EED)
    for _ in range(200000):
        x = next(g)
        exp = (x >> 23) & 0xFF
        if exp == 0 or exp == 0xFF:
            continue
        hi, lo = x >> 16, x & 0xFFFF
        if lo > 0x8000 or (lo == 0x8000 and (hi & 1)):
            hi += 1
        if fp32_to_bf16(x) != (hi & 0xFFFF):
            bad += 1
    t.eq("fp32_to_bf16 200k normals vs independent RNE", bad, 0)
    # make_fp32
    t.eq("bf16_to_fp32(0x7F81) (SNaN kept, no quieting)", bf16_to_fp32(0x7F81), 0x7F810000)
    t.eq("bf16_to_fp32(0x8001)", bf16_to_fp32(0x8001), 0x80010000)

    print("== vector shapes / zeroing ==")
    ones16 = bytes([0xFF] * 16)
    r = vpdpb("UU", False, ones16, ones16, ones16)
    t.eq("VPDPBUUD VEX.128 upper 128 zero", r[16:], bytes(16))
    t.eq("VPDPBUUD VEX.128 lane = 0xFFFFFFFF+0x3F804", r[:4], _d(0xFFFFFFFF + 0x3F804))
    r = vpdpw("SU", True, bytes(32), bytes([0, 0x80] * 16), bytes([0xFF] * 32))
    t.eq("VPDPWSUDS VEX.256 all lanes 0x80000000", r, _d(0x80000000) * 8)
    r = vpmadd52(True, bytes(16), bytes([0xFF] * 16), bytes([0xFF] * 16))
    t.eq("VPMADD52HUQ VEX.128 image", r, struct.pack("<QQ", 0xFFFFFFFFFFFFE, 0xFFFFFFFFFFFFE) + bytes(16))
    r = cvtneps2bf16(struct.pack("<4I", 0x3F808000, 0x3F818000, 0x7F800001, 0x80000001))
    t.eq("VCVTNEPS2BF16 VEX.128 (bits 255:64 zero)", r, struct.pack("<4H", 0x3F80, 0x3F82, 0x7FC0, 0x8000) + bytes(24))
    src = struct.pack("<8I", *[0x3F800000 + i * 0x10000 for i in range(8)])
    r = cvtneps2bf16(src)
    t.eq("VCVTNEPS2BF16 VEX.256 (8 words, bits 255:128 zero)", r, struct.pack("<8H", *[0x3F80 + i for i in range(8)]) + bytes(16))
    r = bcstne("fp16", bytes([0x01, 0x7C]), 16)
    t.eq("VBCSTNESH2PS VEX.128 m16=0x7C01", r, _d(0x7FC02000) * 4 + bytes(16))
    r = bcstne("bf16", bytes([0x81, 0x7F]), 32)
    t.eq("VBCSTNEBF162PS VEX.256 m16=0x7F81", r, _d(0x7F810000) * 8)
    m = struct.pack("<8H", 0x0001, 0x3C00, 0x7C01, 0x8000, 0x03FF, 0xFC00, 0x7E00, 0x0400)
    t.eq("VCVTNEEPH2PS VEX.128 even words", cvtne_even_odd("fp16", False, m),
         struct.pack("<4I", 0x33800000, 0x7FC02000, 0x387FC000, 0x7FC00000) + bytes(16))
    t.eq("VCVTNEOPH2PS VEX.128 odd words", cvtne_even_odd("fp16", True, m),
         struct.pack("<4I", 0x3F800000, 0x80000000, 0xFF800000, 0x38800000) + bytes(16))
    t.eq("VCVTNEEBF162PS VEX.128 even words", cvtne_even_odd("bf16", False, m),
         struct.pack("<4I", 0x00010000, 0x7C010000, 0x03FF0000, 0x7E000000) + bytes(16))
    t.eq("VCVTNEOBF162PS VEX.128 odd words", cvtne_even_odd("bf16", True, m),
         struct.pack("<4I", 0x3C000000, 0x80000000, 0xFC000000, 0x04000000) + bytes(16))

    print("-- %d checks, %d failed" % (t.n, t.fail))
    return 0 if t.fail == 0 else 1


# --------------------------------------------------------------------------
# case generation
# --------------------------------------------------------------------------

PP = {"NP": 0, "66": 1, "F3": 2, "F2": 3}


def vex3(op, pp, L, W, vvvv_reg, modrm):
    """3-byte VEX, map 0F38, R/X/B = 1 (no extension).  vvvv_reg is the
    register number (encoded inverted); vvvv_reg = 0 gives the field 1111b."""
    b2 = (W << 7) | (((~vvvv_reg) & 15) << 3) | (L << 2) | PP[pp]
    return bytes([0xC4, 0xE2, b2, op, modrm])


MODRM_REG = 0xC2  # reg = xmm0, r/m = xmm2
MODRM_MEM = 0x06  # reg = xmm0, r/m = [rsi]

# junk patterns used to prove upper / unused bits are ignored or zeroed
JUNK0 = bytes([0xA5] * 16)          # ymmh0 initial (must become 0)
JUNK1 = bytes(range(0xC0, 0xD0))    # ymmh1 for VEX.128 (ignored)
JUNK2 = bytes(range(0xE0, 0xF0))    # ymmh2 for VEX.128 (ignored)
JUNKX = bytes(range(0x30, 0x50))    # whole xmm2/ymm2 when unused (memory forms)
JUNKV = bytes(range(0x50, 0x70))    # whole ymm1 when unused (NE ops)


def _h(b):
    return bytes(b).hex().upper()


class Case:
    def __init__(self, name, code, y0, y1, y2, mem, exp):
        assert len(y0) == 32 and len(y1) == 32 and len(y2) == 32 and len(exp) == 32
        self.name, self.code, self.y0, self.y1, self.y2, self.mem, self.exp = name, code, y0, y1, y2, mem, exp

    def line(self):
        s = ".byte " + ", ".join("0x%02x" % c for c in self.code) + " |"
        s += " xmm0=%s xmm1=%s xmm2=%s" % (_h(self.y0[:16]), _h(self.y1[:16]), _h(self.y2[:16]))
        s += " ymmh0=%s ymmh1=%s ymmh2=%s" % (_h(self.y0[16:]), _h(self.y1[16:]), _h(self.y2[16:]))
        if self.mem is not None:
            s += " m+0x8000=%s" % _h(self.mem)
        s += " => xmm0=%s ymmh0=%s" % (_h(self.exp[:16]), _h(self.exp[16:]))
        return s

    def cvec(self):
        def arr(b):
            return "{" + ",".join("0x%02x" % c for c in b) + "}"
        mem = (self.mem or b"") + bytes(32 - len(self.mem or b""))
        return '{ "%s", %s, %d, %s, %s, %s, %s, %s },' % (
            self.name, arr(self.code), len(self.code), arr(self.y0), arr(self.y1), arr(self.y2), arr(mem), arr(self.exp))


# lane tables: (dest dword, 4 src1 bytes, 4 src2 bytes)
B_LANES = {
    "SS": [
        (0x00000000, [0x80] * 4, [0x80] * 4),            # 4*16384 = 0x10000
        (0x7FFEFFFF, [0x80] * 4, [0x80] * 4),            # exact 0x7FFFFFFF
        (0x7FFF0000, [0x80] * 4, [0x80] * 4),            # max+1: sat / wrap 0x80000000
        (0x8000FE00, [0x80] * 4, [0x7F] * 4),            # exact 0x80000000
        (0x8000FDFF, [0x80] * 4, [0x7F] * 4),            # min-1: sat / wrap 0x7FFFFFFF
        (0x00000000, [0x01, 0xFF, 0x7F, 0x80], [0xFF, 0xFF, 0x80, 0x7F]),
        (0xFFFFFFFF, [0xFF] * 4, [0xFF] * 4),            # -1 + 4
        (0x12345678, [0x7F, 0x00, 0x80, 0x01], [0x7F, 0x55, 0x80, 0xFE]),
    ],
    "SU": [
        (0x7FFE05FB, [0x7F] * 4, [0xFF] * 4),            # exact max
        (0x7FFE05FC, [0x7F] * 4, [0xFF] * 4),            # max+1
        (0x80000000, [0x80] * 4, [0xFF] * 4),            # below min
        (0x00000000, [0xFF] * 4, [0xFF] * 4),            # -1020 (src2 unsigned)
        (0x7FFFFFFF, [0x7F] * 4, [0xFF] * 4),            # sat max
        (0x8001FE00, [0x80] * 4, [0xFF] * 4),            # exact min
        (0x80000000, [0x01, 0x02, 0x03, 0x04], [0x80, 0x80, 0x80, 0x80]),  # +1280
        (0xDEADBEEF, [0x80, 0x7F, 0xFF, 0x01], [0x01, 0xFF, 0x80, 0x7F]),
    ],
    "UU": [
        (0xFFFFFF00, [0xFF] * 4, [0xFF] * 4),            # sat / wrap 0x3F704
        (0xFFFC07FB, [0xFF] * 4, [0xFF] * 4),            # exact max
        (0xFFFC07FC, [0xFF] * 4, [0xFF] * 4),            # max+1 (wrap -> 0)
        (0x7FFFFFFF, [0xFF] * 4, [0xFF] * 4),            # 0x8003F803, unsigned dest
        (0x80000000, [0x80] * 4, [0x80] * 4),            # 0x80010000
        (0x00000000, [0x01, 0x02, 0x03, 0x04], [0x05, 0x06, 0x07, 0x08]),  # 70
        (0xFFFFFFFF, [0x00] * 4, [0xFF] * 4),            # unchanged
        (0x00000001, [0xFF, 0x00, 0x80, 0x7F], [0xFF, 0xFF, 0x80, 0x80]),
    ],
}

# (dest dword, 2 src1 words, 2 src2 words)
W_LANES = {
    "SU": [
        (0x00000000, (0x8000, 0x8000), (0xFFFF, 0xFFFF)),  # -0xFFFF0000
        (0x00017FFE, (0x7FFF, 0x0000), (0xFFFF, 0x0000)),  # exact max
        (0x00017FFF, (0x7FFF, 0x0000), (0xFFFF, 0x0000)),  # max+1
        (0x00000000, (0xFFFF, 0x0000), (0x0002, 0x0000)),  # -2 (role check)
        (0x00000000, (0x7FFF, 0x7FFF), (0xFFFF, 0xFFFF)),  # 0xFFFD0002 -> sat
        (0x80000000, (0xFFFF, 0x0001), (0xFFFF, 0x0000)),  # -65535 -> sat min
        (0x7FFF8000, (0x8000, 0x0000), (0xFFFF, 0x0000)),  # 0x7FFF8000-0x7FFF8000 = 0
        (0xCAFEBABE, (0x1234, 0xFEDC), (0x8765, 0x4321)),
    ],
    "US": [
        (0x00000000, (0xFFFF, 0xFFFF), (0x8000, 0x8000)),
        (0x00017FFE, (0xFFFF, 0x0000), (0x7FFF, 0x0000)),
        (0x00017FFF, (0xFFFF, 0x0000), (0x7FFF, 0x0000)),
        (0x00000000, (0xFFFF, 0x0000), (0x0002, 0x0000)),  # 0x1FFFE (role check)
        (0x00000000, (0xFFFF, 0xFFFF), (0x7FFF, 0x7FFF)),
        (0x80000000, (0xFFFF, 0x0000), (0xFFFF, 0x0000)),  # -65535 -> sat min
        (0x80000000, (0x0002, 0x0000), (0xFFFF, 0x0000)),  # -2 -> sat min
        (0xCAFEBABE, (0x1234, 0xFEDC), (0x8765, 0x4321)),
    ],
    "UU": [
        (0x00000000, (0xFFFF, 0xFFFF), (0xFFFF, 0xFFFF)),  # 0x1FFFC0002
        (0x0001FFFE, (0xFFFF, 0x0000), (0xFFFF, 0x0000)),  # exact max
        (0x0001FFFF, (0xFFFF, 0x0000), (0xFFFF, 0x0000)),  # max+1
        (0x80000000, (0xFFFF, 0x0000), (0xFFFF, 0x0000)),  # sat
        (0xFFFFFFFF, (0x0000, 0x0000), (0x0000, 0x0000)),
        (0x7FFFFFFF, (0x8000, 0x0000), (0x0001, 0x0000)),  # 0x80007FFF unsigned
        (0x00000000, (0x8000, 0x8000), (0x8000, 0x8000)),  # 0x80000000
        (0xCAFEBABE, (0x1234, 0xFEDC), (0x8765, 0x4321)),
    ],
}

M52_LANES = [
    (0x0000000000000000, 0x000FFFFFFFFFFFFF, 0x000FFFFFFFFFFFFF),
    (0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF),  # garbage tops + wrap
    (0x0123456789ABCDEF, 0xFFF0000000000003, 0xABC0000000000005),
    (0x0000000000000000, 0x0008000000000000, 0x0008000000000000),  # 2^51 * 2^51
    (0x8000000000000000, 0x0010000000000000, 0x000FFFFFFFFFFFFF),  # bit 52 only -> 0
    (0xFFFFFFFFFFFFFFF0, 0x000DEADBEEFCAFE1, 0x0007654321FEDCBA),
    (0x7FFFFFFFFFFFFFFF, 0x00000000FFFFFFFF, 0x00000000FFFFFFFF),
    (0x0000000000000001, 0x123456789ABCDEF0, 0xFEDCBA9876543210),
]

FP32_VALS = [0x3F808000, 0x3F818000, 0x3F807FFF, 0x7F7FFFFF, 0x00400000, 0x80000001, 0x7F800000, 0xFF800000,
             0x7FC00000, 0x7F800001, 0xFF812345, 0x3F808001, 0x007FFFFF, 0x00800000, 0xFF7FFFFF, 0xC0490FDB]

FP16_WORDS = [0x0001, 0x3C00, 0x7C01, 0x8000, 0x03FF, 0xFC00, 0x7E00, 0x0400,
              0x8001, 0x7BFF, 0xFD55, 0x7C00, 0x0000, 0xC000, 0x83FF, 0x3555]

BF16_WORDS = [0x7F81, 0x3F80, 0x0001, 0x8000, 0xFF80, 0x7FC0, 0x8001, 0x4049,
              0x007F, 0xFFFF, 0x0080, 0x7F80, 0xBF80, 0x7F7F, 0x0000, 0xC2F7]


def _rot(lst, k):
    k %= len(lst)
    return lst[k:] + lst[:k]


def _build_b(lanes):
    d = b"".join(_d(x[0]) for x in lanes)
    a = b"".join(bytes(x[1]) for x in lanes)
    b = b"".join(bytes(x[2]) for x in lanes)
    return d, a, b


def _build_w(lanes):
    d = b"".join(_d(x[0]) for x in lanes)
    a = b"".join(struct.pack("<HH", *x[1]) for x in lanes)
    b = b"".join(struct.pack("<HH", *x[2]) for x in lanes)
    return d, a, b


def _build_q(lanes):
    d = b"".join(struct.pack("<Q", x[0]) for x in lanes)
    a = b"".join(struct.pack("<Q", x[1]) for x in lanes)
    b = b"".join(struct.pack("<Q", x[2]) for x in lanes)
    return d, a, b


def _three_src_cases(name, op, pp, W, fn, lanes, builder, lanes_per_xmm):
    """VEX.128 reg, VEX.256 reg, VEX.256 mem for a dest+=f(src1,src2) op."""
    out = []
    # VEX.128 register: lanes[0:n]; upper halves of sources are junk (ignored)
    l128 = lanes[:lanes_per_xmm]
    d, a, b = builder(l128)
    y0, y1, y2 = d + JUNK0, a + JUNK1, b + JUNK2
    exp = fn(d, a, b)
    out.append(Case(name + "_128_reg", vex3(op, pp, 0, W, 1, MODRM_REG), y0, y1, y2, None, exp))
    # VEX.256 register: lanes rotated so the 128-bit lanes are not identical
    l256 = _rot(lanes, lanes_per_xmm // 2 + 1)[:2 * lanes_per_xmm]
    d, a, b = builder(l256)
    exp = fn(d, a, b)
    out.append(Case(name + "_256_reg", vex3(op, pp, 1, W, 1, MODRM_REG), d, a, b, None, exp))
    # VEX.256 memory ([rsi]); xmm2/ymm2 holds junk and must be ignored
    l256m = _rot(lanes, lanes_per_xmm)[:2 * lanes_per_xmm]
    d, a, b = builder(l256m)
    exp = fn(d, a, b)
    out.append(Case(name + "_256_mem", vex3(op, pp, 1, W, 1, MODRM_MEM), d, a, JUNKX, b, exp))
    return out


def gen_cases():
    cases = []
    sections = []

    # AVX-VNNI-INT8
    sec = []
    for pp, kind, base in (("F2", "SS", "vpdpbssd"), ("F3", "SU", "vpdpbsud"), ("NP", "UU", "vpdpbuud")):
        for sat in (False, True):
            op = 0x51 if sat else 0x50
            nm = base + ("s" if sat else "")
            fn = (lambda k, s: (lambda d, a, b: vpdpb(k, s, d, a, b)))(kind, sat)
            sec += _three_src_cases(nm, op, pp, 0, fn, B_LANES[kind], _build_b, 4)
    sections.append(("AVX-VNNI-INT8 (VEX.W0 0F38 50/51; dest=xmm0, src1=vvvv=xmm1, src2=r/m)", sec))

    # AVX-VNNI-INT16
    sec = []
    for pp, kind, base in (("F3", "SU", "vpdpwsud"), ("66", "US", "vpdpwusd"), ("NP", "UU", "vpdpwuud")):
        for sat in (False, True):
            op = 0xD3 if sat else 0xD2
            nm = base + ("s" if sat else "")
            fn = (lambda k, s: (lambda d, a, b: vpdpw(k, s, d, a, b)))(kind, sat)
            sec += _three_src_cases(nm, op, pp, 0, fn, W_LANES[kind], _build_w, 4)
    sections.append(("AVX-VNNI-INT16 (VEX.W0 0F38 D2/D3)", sec))

    # AVX-IFMA
    sec = []
    for hi, op, nm in ((False, 0xB4, "vpmadd52luq"), (True, 0xB5, "vpmadd52huq")):
        fn = (lambda h: (lambda d, a, b: vpmadd52(h, d, a, b)))(hi)
        sec += _three_src_cases(nm, op, "66", 1, fn, M52_LANES, _build_q, 2)
    sections.append(("AVX-IFMA (VEX.66.0F38.W1 B4/B5)", sec))

    # AVX-NE-CONVERT
    sec = []
    y0init = bytes(range(0x80, 0x90)) + JUNK0  # dest initial value: must be fully overwritten / zeroed
    # VBCSTNE*: m16, memory only.  Memory carries 2 junk bytes after the m16.
    for kind, pp, nm, m128, m256 in (("bf16", "F3", "vbcstnebf162ps", 0x7F81, 0x8001),
                                     ("fp16", "66", "vbcstnesh2ps", 0x7C01, 0x83FF)):
        for L, w in ((0, m128), (1, m256)):
            m16 = struct.pack("<H", w)
            exp = bcstne(kind, m16, 32 if L else 16)
            sec.append(Case("%s_%d_mem" % (nm, 256 if L else 128), vex3(0xB1, pp, L, 0, 0, MODRM_MEM),
                            y0init, JUNKV, JUNKX, m16 + b"\xEE\xDD", exp))
    # VCVTNE[E|O][BF16|PH]2PS: memory only
    for kind, odd, pp, nm in (("bf16", False, "F3", "vcvtneebf162ps"), ("fp16", False, "66", "vcvtneeph2ps"),
                              ("bf16", True, "F2", "vcvtneobf162ps"), ("fp16", True, "NP", "vcvtneoph2ps")):
        words = BF16_WORDS if kind == "bf16" else FP16_WORDS
        for L in (0, 1):
            n = 16 if L else 8
            ws = _rot(words, 3)[:n] if L else words[:n]
            mem = struct.pack("<%dH" % n, *ws)
            exp = cvtne_even_odd(kind, odd, mem)
            sec.append(Case("%s_%d_mem" % (nm, 256 if L else 128), vex3(0xB0, pp, L, 0, 0, MODRM_MEM),
                            y0init, JUNKV, JUNKX, mem, exp))
    # VCVTNEPS2BF16: reg 128, reg 256, mem 256
    v128 = struct.pack("<4I", *FP32_VALS[:4])
    y2 = v128 + struct.pack("<4I", *FP32_VALS[4:8])  # ymmh2 non-zero: ignored by VEX.128
    sec.append(Case("vcvtneps2bf16_128_reg", vex3(0x72, "F3", 0, 0, 0, MODRM_REG), y0init, JUNKV, y2, None,
                    cvtneps2bf16(v128)))
    v256 = struct.pack("<8I", *FP32_VALS[4:12])
    sec.append(Case("vcvtneps2bf16_256_reg", vex3(0x72, "F3", 1, 0, 0, MODRM_REG), y0init, JUNKV, v256, None,
                    cvtneps2bf16(v256)))
    v256m = struct.pack("<8I", *(FP32_VALS[8:16]))
    sec.append(Case("vcvtneps2bf16_256_mem", vex3(0x72, "F3", 1, 0, 0, MODRM_MEM), y0init, JUNKV, JUNKX, v256m,
                    cvtneps2bf16(v256m)))
    sections.append(("AVX-NE-CONVERT (VEX.W0, vvvv=1111b; B0/B1 memory only)", sec))

    for _, s in sections:
        cases += s
    return sections, cases


def gen_ud_lines():
    ud = []

    def add(comment, code):
        ud.append((comment, bytes(code)))

    # W1 variants of the W0 ops
    for pp, op, nm in (("F2", 0x50, "VPDPBSSD"), ("F2", 0x51, "VPDPBSSDS"), ("F3", 0x50, "VPDPBSUD"),
                       ("F3", 0x51, "VPDPBSUDS"), ("NP", 0x50, "VPDPBUUD"), ("NP", 0x51, "VPDPBUUDS"),
                       ("F3", 0xD2, "VPDPWSUD"), ("F3", 0xD3, "VPDPWSUDS"), ("66", 0xD2, "VPDPWUSD"),
                       ("66", 0xD3, "VPDPWUSDS"), ("NP", 0xD2, "VPDPWUUD"), ("NP", 0xD3, "VPDPWUUDS")):
        add("%s with VEX.W1 (VEX.128)" % nm, vex3(op, pp, 0, 1, 1, MODRM_REG))
    add("VPDPBSSD with VEX.W1 (VEX.256)", vex3(0x50, "F2", 1, 1, 1, MODRM_REG))
    add("VPDPWUUDS with VEX.W1 (VEX.256)", vex3(0xD3, "NP", 1, 1, 1, MODRM_REG))
    for pp, op, nm in (("F3", 0xB1, "VBCSTNEBF162PS"), ("66", 0xB1, "VBCSTNESH2PS"), ("F3", 0xB0, "VCVTNEEBF162PS"),
                       ("66", 0xB0, "VCVTNEEPH2PS"), ("F2", 0xB0, "VCVTNEOBF162PS"), ("NP", 0xB0, "VCVTNEOPH2PS")):
        add("%s with VEX.W1 (memory form)" % nm, vex3(op, pp, 0, 1, 0, MODRM_MEM))
    add("VCVTNEPS2BF16 with VEX.W1 (VEX.128 reg)", vex3(0x72, "F3", 0, 1, 0, MODRM_REG))
    add("VCVTNEPS2BF16 with VEX.W1 (VEX.256 reg)", vex3(0x72, "F3", 1, 1, 0, MODRM_REG))
    # W0 variants of VPMADD52
    add("VPMADD52LUQ with VEX.W0 (VEX.128)", vex3(0xB4, "66", 0, 0, 1, MODRM_REG))
    add("VPMADD52LUQ with VEX.W0 (VEX.256)", vex3(0xB4, "66", 1, 0, 1, MODRM_REG))
    add("VPMADD52HUQ with VEX.W0 (VEX.128)", vex3(0xB5, "66", 0, 0, 1, MODRM_REG))
    add("VPMADD52HUQ with VEX.W0 (VEX.256)", vex3(0xB5, "66", 1, 0, 1, MODRM_REG))
    # register-form ModRM on the memory-only NE ops
    for pp, op, nm in (("F3", 0xB1, "VBCSTNEBF162PS"), ("66", 0xB1, "VBCSTNESH2PS"), ("F3", 0xB0, "VCVTNEEBF162PS"),
                       ("66", 0xB0, "VCVTNEEPH2PS"), ("F2", 0xB0, "VCVTNEOBF162PS"), ("NP", 0xB0, "VCVTNEOPH2PS")):
        add("%s register form (ModRM 0xC2, VEX.128)" % nm, vex3(op, pp, 0, 0, 0, 0xC2))
        add("%s register form (ModRM 0xC1, VEX.256)" % nm, vex3(op, pp, 1, 0, 0, 0xC1))
    # VCVTNEPS2BF16 with vvvv != 1111b
    add("VCVTNEPS2BF16 VEX.128 with vvvv=1110b (xmm1)", vex3(0x72, "F3", 0, 0, 1, MODRM_REG))
    add("VCVTNEPS2BF16 VEX.256 with vvvv=0000b (xmm15)", vex3(0x72, "F3", 1, 0, 15, MODRM_REG))
    add("VCVTNEPS2BF16 VEX.256 mem with vvvv=1110b", vex3(0x72, "F3", 1, 0, 1, MODRM_MEM))
    # memory-only NE ops with vvvv != 1111b (SDM 2.3: "reserved and should contain 1111b")
    add("VBCSTNESH2PS mem with vvvv=1110b", vex3(0xB1, "66", 0, 0, 1, MODRM_MEM))
    add("VCVTNEEBF162PS mem with vvvv=1110b", vex3(0xB0, "F3", 0, 0, 1, MODRM_MEM))
    # F2 prefix on D2/D3 (no such instruction)
    add("VEX.128.F2.0F38.W0 D2 (undefined)", vex3(0xD2, "F2", 0, 0, 1, MODRM_REG))
    add("VEX.256.F2.0F38.W0 D2 (undefined)", vex3(0xD2, "F2", 1, 0, 1, MODRM_REG))
    add("VEX.128.F2.0F38.W0 D3 (undefined)", vex3(0xD3, "F2", 0, 0, 1, MODRM_REG))
    # legacy (non-VEX) encodings
    add("legacy 66 0F 38 B4 /r (no legacy IFMA)", [0x66, 0x0F, 0x38, 0xB4, 0xC1])
    add("legacy F3 0F 38 72 /r (no legacy VCVTNEPS2BF16)", [0xF3, 0x0F, 0x38, 0x72, 0xC1])
    # guard: never emit AVX-VNNI (66 0F38 50-53) here
    for _, c in ud:
        if c[0] == 0xC4 and (c[2] & 3) == 1 and c[3] in (0x50, 0x51, 0x52, 0x53):
            raise AssertionError("AVX-VNNI encoding leaked into #UD list")
    return ud


def print_cases():
    sections, _ = gen_cases()
    w = sys.stdout.write
    w("# Generated by tools/isa/ref_vnni_ifma_ne.py --cases (independent SDM reference model)\n")
    w("# Format: code | inputs => expected.  Hex = memory byte order. ymmhN = bits 255:128 of reg N.\n")
    w("# Register forms: dest ymm0, src1 (VEX.vvvv) ymm1, src2 r/m ymm2 (ModRM C2).\n")
    w("# Memory forms: ModRM 06 = [rsi], RSI -> MEM+0x8000.\n")
    for title, sec in sections:
        w("#\n# --- %s\n" % title)
        for c in sec:
            w("# %s\n" % c.name)
            w(c.line() + "\n")
    w("#\n# --- #UD expected everywhere (no AVX-VNNI-INT8/INT16/IFMA/NE-CONVERT)\n")
    for comment, code in gen_ud_lines():
        w("# %s\n" % comment)
        w(".byte " + ", ".join("0x%02x" % c for c in code) + "\n")


def print_cvectors():
    _, cases = gen_cases()
    w = sys.stdout.write
    w("/* Generated by tools/isa/ref_vnni_ifma_ne.py --cvectors.\n")
    w(" * { name, code[], code_len, ymm0_in[32], ymm1_in[32], ymm2_in[32], mem_at_rsi[32], ymm0_expected[32] }\n")
    w(" * All arrays are full 256-bit YMM images in memory byte order (bytes 16..31 = ymmh).\n")
    w(" * Memory forms: ModRM 06 = [rsi]; mem is zero-padded to 32 bytes. */\n")
    for c in cases:
        w("/* %s: %s */\n" % (c.name, " ".join("%02X" % x for x in c.code)))
        w(c.cvec() + "\n")


def print_cinc():
    """Complete C include for unicorn/tests/unit/test_x86.c (vectors + #UD list)."""
    _, cases = gen_cases()
    w = sys.stdout.write
    w("/*\n * Generated by Emulator/tools/isa/ref_vnni_ifma_ne.py --cinc (independent SDM\n")
    w(" * reference model); regenerate instead of editing. Used by test_x86.c\n")
    w(" * (test_x86_vnni_ifma_ne_*). Arrays are full 256-bit YMM images in memory byte\n")
    w(" * order; memory forms use ModRM 06 = [rsi], mem is the data at RSI (zero-padded).\n */\n")
    w("struct x86_vnni_vec {\n    const char *name;\n    uint8_t code[8];\n    int code_len;\n")
    w("    uint8_t ymm0[32], ymm1[32], ymm2[32], mem[32], expect[32];\n};\n\n")
    w("static const struct x86_vnni_vec x86_vnni_vecs[] = {\n")
    for c in cases:
        w("    /* %s: %s */\n" % (c.name, " ".join("%02X" % x for x in c.code)))
        w("    " + c.cvec() + "\n")
    w("};\n\n")
    w("struct x86_vnni_ud {\n    const char *name;\n    uint8_t code[8];\n    int code_len;\n};\n\n")
    w("/* #UD on every CPU model (also UC_CPU_X86_MAX) */\n")
    w("static const struct x86_vnni_ud x86_vnni_uds[] = {\n")
    for comment, code in gen_ud_lines():
        w('    { "%s", {%s}, %d },\n' % (comment, ",".join("0x%02x" % x for x in code), len(code)))
    w("};\n")


def main(argv):
    if len(argv) != 2 or argv[1] not in ("--selftest", "--cases", "--cvectors", "--cinc"):
        sys.stderr.write("usage: %s --selftest | --cases | --cvectors | --cinc\n" % argv[0])
        return 2
    try:
        sys.stdout.reconfigure(newline="\n")  # LF output on Windows too
    except (AttributeError, ValueError):
        pass
    if argv[1] == "--selftest":
        return selftest()
    if argv[1] == "--cases":
        print_cases()
        return 0
    if argv[1] == "--cinc":
        print_cinc()
        return 0
    print_cvectors()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
