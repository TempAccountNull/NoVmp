#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_xeonphi.py -- independent reference model (Python 3 stdlib only: integers, fractions.Fraction,
decimal) of the Intel Xeon Phi-only instruction families (ledger U990-U995) and generator of the
expected-value case files Emulator/data/cases_xeonphi.txt (run with --xeonphi) and
Emulator/data/cases_xeonphi_off.txt (run with --avx512 only: every form #UD).

Written from the Intel documents only (no emulator source was read for the semantics):
  SDM 325383-092 (June 2026) Vol2D chapter 8 "Instruction Set Reference Unique to Intel Xeon Phi
  Processors" (printed pages 8-1 .. 8-39; the identical text is in the combined SDM 325462-080,
  June 2023, Vol2D chapter 8; no ISE 319433 revision present here (-034 .. -062) still carries
  these definitions: they hold only the CPUID bits and the Tuple1_4X disp8*N row):
    PREFETCHWT1                8-2 .. 8-3   0F 0D /2, CPUID.(7,0):ECX[0]
    V4FMADDPS/V4FNMADDPS       8-4 .. 8-5   EVEX.512.F2.0F38.W0 9A / AA, Tuple1_4X, "Type E2"
    V4FMADDSS/V4FNMADDSS       8-6 .. 8-7   EVEX.LLIG.F2.0F38.W0 9B / AB, Tuple1_4X, "Type E2"
    VEXP2PD / VEXP2PS          8-8 .. 8-11  EVEX.512.66.0F38.W1/W0 C8, Full, {sae}, E2, Tables 8-1/8-2
    VGATHERPF0{D,Q}P{S,D}      8-12 .. 8-13 EVEX.512.66.0F38 C6/C7 /1 vsib, T1S, E12NP
    VGATHERPF1{D,Q}P{S,D}      8-14 .. 8-15 /2
    VP4DPWSSDS / VP4DPWSSD     8-16 .. 8-19 EVEX.512.F2.0F38.W0 53 / 52, Tuple1_4X, "Type E4"
    VRCP28PD/SD/PS/SS          8-20 .. 8-27 66.0F38 CA (packed, E2) / CB (scalar, E3), Tables 8-3..8-6
    VRSQRT28PD/SD/PS/SS        8-28 .. 8-35 66.0F38 CC / CD, Tables 8-7..8-10
    VSCATTERPF0/1{D,Q}P{S,D}   8-36 .. 8-39 /5, /6
  SDM 092 Vol2A 2.7 (EVEX, Tables 2-36..2-43), 2.8 (Tables 2-48 E2, 2-49 E3, 2-51 E4, 2-64 E12NP),
  Vol1 4.8.3.5 / 4.9 (NaN, exceptions; tininess after rounding with unbounded exponent),
  10.2.3.3 / 10.2.3.4 (FTZ, DAZ), 11.5.2 (pre-/post-computation exceptions), 14.5.2 / Table 14-17
  (FMA: one rounding, NaN priority x, y, z), Vol1 Tables 21-19..21-21 (CPUID bits).
  ISE 319433-062 Table 1-11 (printed 1-53): Tuple1_4X, N = 16 (same in -034 Table 1-15).

Semantics modelled
------------------
4VNNIW  VP4DPWSSD[S] zmm1{k1}{z}, zmm2+3, m128 (KL 16): FOR lane i (if k1[i] or no mask):
        FOR m = 0..3: t := SRC2.dword[m]; DEST.dword[i] := [SIGNED_DWORD_SATURATE](DEST.dword[i] +
        reg[base+m].word[2i] * t.word[0] + reg[base+m].word[2i+1] * t.word[1]); base = vvvv & ~3
        (5-bit register index). The pseudocode updates DEST in place, so when DEST is one of the
        four block registers the later steps read the partly accumulated lane (taken literally).
        No SIMD FP exception. Memory: the 16 bytes are read when any of k1[15:0] is set or no
        mask is used ("memory fault suppression"), else nothing is read (no #PF).
4FMAPS  V4F[N]MADDPS zmm1{k1}{z}, zmm2+3, m128 / V4F[N]MADDSS xmm1{k1}{z}, xmm2+3, m128:
        tmpdest := dest; FOR j = 0..3: FOR lane i active: tmpdest[i] := RoundFPControl_MXCSR(
        tmpdest[i] +/- reg[base+j][i] * msrc[j]) (one FMA rounding: x = reg, y = msrc, z = tmpdest,
        NaN priority x, y, z; the negative form is -(x*y) + z); "Exceptions are also taken
        sequentially": the flags of step j are checked before step j+1 (an unmasked one raises #XM
        there, destination unchanged, MXCSR keeps the flags of steps 0..j; within step j an
        unmasked IE/DE/ZE drops OE/UE/PE). The block registers are read as they were before the
        instruction (dest is written only at the end). SS: element 0 only (k1[0]), bits 127:32
        from DEST, bits 511:128 zeroed. EVEX.b = 1 or ModRM.mod = 11b: #UD.
ER      DAZ/FTZ always (denormal inputs are zeros, no #DE; tiny results are zeros, no #UE); MXCSR.RC
        is not used. The SDM gives the error bound and the special cases (Tables 8-1..8-10) and
        refers to an Intel web reference implementation (not available here), so the exact bits of
        silicon are NOT known. MODEL CHOICE (documented compliance, not bit-exact to silicon):
        the exact value of 1/x, 1/sqrt(x), 2^x rounded to nearest even to the destination format.
        This is within every documented bound (< 2^-28 / < 2^-23 relative error); --selftest
        checks the bounds and every special-case table row.
        Flags: VRCP28 IE (SNaN), ZE (+-0 / denormal input); VRSQRT28 IE (SNaN, negative non-zero
        or -inf), ZE (+-0 / denormal); VEXP2 IE (SNaN), OE (2^x above the largest finite: +inf;
        the SDM lists Overflow, not Precision, so PE is never set). {sae} (EVEX.b, register form):
        no flag, no #XM. Tiny: VRCP28 x > 2^126 (2^1022) -> 0, VEXP2 x < -126 (-1022) -> +0.
PF      VGATHERPF0/1 / VSCATTERPF0/1 DPS/QPS/DPD/QPD vm{32,64}{y,z} {k1}: prefetch hints with no
        architectural effect: "No FP nor memory faults", "The mask is left unchanged". #UD (E12NP):
        k0, EVEX.z, no SIB (ModRM.rm != 100b), ModRM.mod = 11b, 16-bit addressing, vvvv != 1111b,
        EVEX.b, L'L != 10b, other ModRM.reg values (/0 /3 /4 /7).

Usage:
  python ref_xeonphi.py --selftest            model checks (SDM tables, bounds, hand examples), exit 0
  python ref_xeonphi.py --cases [--only F,..] Emulator/data/cases_xeonphi.txt (stdout); F in
                                              4vnniw, 4fmaps, pf, er (default: all)
  python ref_xeonphi.py --offcases [--only ..] Emulator/data/cases_xeonphi_off.txt (stdout)
"""

import argparse
import random
import sys
from decimal import Context, Decimal, ROUND_HALF_EVEN
from fractions import Fraction

MEM_RSI = 0x8000          # harness: RSI = MEM + 0x8000; MEM .. MEM + 0xFFFF mapped, MEM + 0x10000 not
RSI = 6

IE, DE, ZE, OE, UE, PE = 1, 2, 4, 8, 16, 32
DAZ, FTZ = 0x40, 0x8000
MXCSR_DEFAULT = 0x1F80
RNE, RD, RU, RZ = 0, 1, 2, 3


class NotModelled(Exception):
    """a case the generator does not emit (an unmasked #O / #U rounding response)"""


# ---------------------------------------------------------------------------------------------
# binary32 / binary64 (Vol1 4.2.2)
# ---------------------------------------------------------------------------------------------
class Fmt:
    def __init__(self, name, bits, p, ebits):
        self.name, self.bits, self.p, self.ebits = name, bits, p, ebits
        self.fbits = p - 1
        self.bias = (1 << (ebits - 1)) - 1
        self.emin, self.emax = 1 - self.bias, self.bias
        self.signbit = 1 << (bits - 1)
        self.expmask = ((1 << ebits) - 1) << self.fbits
        self.fracmask = (1 << self.fbits) - 1
        self.quietbit = 1 << (self.fbits - 1)
        self.inf = self.expmask
        self.indef = self.signbit | self.expmask | self.quietbit
        self.one = self.bias << self.fbits
        self.maxf = self.expmask - 1
        self.size = bits // 8


F32 = Fmt("f32", 32, 24, 8)
F64 = Fmt("f64", 64, 53, 11)


def classify(x, f):
    """(class, sign, exact value or None)"""
    s = 1 if x & f.signbit else 0
    e = (x & f.expmask) >> f.fbits
    m = x & f.fracmask
    if e == (1 << f.ebits) - 1:
        if m == 0:
            return "inf", s, None
        return ("qnan" if m & f.quietbit else "snan"), s, None
    if e == 0:
        if m == 0:
            return "zero", s, Fraction(0)
        v = Fraction(m, 1 << (f.bias - 1 + f.fbits))
        return "denorm", s, (-v if s else v)
    v = Fraction(m | (1 << f.fbits)) * Fraction(2) ** (e - f.bias - f.fbits)
    return "normal", s, (-v if s else v)


NAN = ("snan", "qnan")


def quiet(x, f):
    return x | f.quietbit


def floor_log2(q):
    """floor(log2(q)) for a positive rational"""
    n, d = q.numerator, q.denominator
    e = n.bit_length() - d.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    elif Fraction(2) ** (e + 1) <= q:
        e += 1
    return e


def round_count(q, quantum, rc, neg):
    """q / quantum rounded to an integer under rc (sign neg): (count, inexact)"""
    t = q / quantum
    fl = t.numerator // t.denominator
    rem = t - fl
    if rem == 0:
        return fl, False
    if rc == RNE:
        if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl & 1):
            fl += 1
    elif rc == RD:
        if neg:
            fl += 1
    elif rc == RU:
        if not neg:
            fl += 1
    return fl, True


def pack(f, neg, e, cnt):
    """normal number cnt * 2^(e - fbits), 2^fbits <= cnt < 2^p"""
    return (f.signbit if neg else 0) | ((e + f.bias) << f.fbits) | (cnt - (1 << f.fbits))


def encode(v, f, rc, mx):
    """round the exact non-zero rational v to f under rc with the MXCSR responses (Vol1 4.9.1.4,
    4.9.1.5, 10.2.3.3): (bits, flags). Tininess after rounding with unbounded exponent; FTZ: a tiny
    result becomes a zero of the true sign with UE PE. An unmasked overflow / underflow response is
    not modelled (NotModelled): the generator emits no such case."""
    neg = v < 0
    q = -v if neg else v
    sgn = f.signbit if neg else 0
    e = floor_log2(q)
    cnt, inexact = round_count(q, Fraction(2) ** (e - f.fbits), rc, neg)
    if cnt == 1 << f.p:
        cnt >>= 1
        e += 1
    tiny = e < f.emin
    if e > f.emax:
        if not (mx & 0x400):
            raise NotModelled("unmasked overflow")
        if rc == RNE or (rc == RD and neg) or (rc == RU and not neg):
            return sgn | f.inf, OE | PE
        return sgn | f.maxf, OE | PE
    if tiny:
        if not (mx & 0x800):
            raise NotModelled("unmasked underflow")
        if mx & FTZ:
            return sgn, UE | PE
        c2, inex2 = round_count(q, Fraction(2) ** (f.emin - f.fbits), rc, neg)
        return sgn | c2, (UE | PE) if inex2 else 0
    return pack(f, neg, e, cnt), (PE if inexact else 0)


def rne_exact(v, f):
    """RNE of the exact positive or negative rational v that is known to be normal after rounding
    (no flags): the ER result rounding"""
    neg = v < 0
    q = -v if neg else v
    e = floor_log2(q)
    cnt, _ = round_count(q, Fraction(2) ** (e - f.fbits), RNE, neg)
    if cnt == 1 << f.p:
        cnt >>= 1
        e += 1
    assert f.emin <= e <= f.emax, (v, e)
    return pack(f, neg, e, cnt)


def value(x, f):
    return classify(x, f)[2]


def bits_of(v, f):
    """exactly representable rational -> bits (selftest / inputs)"""
    if v == 0:
        return 0
    return rne_exact(Fraction(v), f)


# ---------------------------------------------------------------------------------------------
# FMA (Vol1 14.5.2, Table 14-17) with the MXCSR responses
# ---------------------------------------------------------------------------------------------
def fma(x, y, z, f, mx, negp):
    """r = (negp ? -(x*y) : x*y) + z, one rounding; (bits, flags)"""
    rc = (mx >> 13) & 3
    cl = [classify(t, f) for t in (x, y, z)]
    flags = IE if any(c[0] == "snan" for c in cl) else 0
    for i, t in enumerate((x, y, z)):
        if cl[i][0] in NAN:
            return quiet(t, f), flags                 # Q(x), Q(y), Q(z): no DE with a NaN
    (cx, sx, vx), (cy, sy, vy), (cz, sz, vz) = cl
    if mx & DAZ:
        if cx == "denorm":
            cx, vx = "zero", Fraction(0)
        if cy == "denorm":
            cy, vy = "zero", Fraction(0)
        if cz == "denorm":
            cz, vz = "zero", Fraction(0)
    elif "denorm" in (cx, cy, cz):
        flags |= DE
    sp = sx ^ sy ^ (1 if negp else 0)
    if (cx == "inf" and cy == "zero") or (cx == "zero" and cy == "inf"):
        return f.indef, flags | IE
    if cx == "inf" or cy == "inf":
        if cz == "inf" and sz != sp:
            return f.indef, flags | IE
        return (f.signbit if sp else 0) | f.inf, flags
    if cz == "inf":
        return (f.signbit if sz else 0) | f.inf, flags
    p = vx * vy
    if negp:
        p = -p
    s = p + vz
    if s == 0:
        if (cx == "zero" or cy == "zero") and cz == "zero" and sp == sz:
            return (f.signbit if sp else 0), flags
        return (f.signbit if rc == RD else 0), flags
    r, fl = encode(s, f, rc, mx)
    return r, flags | fl


# ---------------------------------------------------------------------------------------------
# AVX512ER element operations (SDM 092 Vol2D 8-8 .. 8-35, Tables 8-1 .. 8-10)
# ---------------------------------------------------------------------------------------------
def er_rcp28(x, f):
    c, s, v = classify(x, f)
    sgn = f.signbit if s else 0
    if c in NAN:
        return quiet(x, f), (IE if c == "snan" else 0)
    if c in ("zero", "denorm"):
        return sgn | f.inf, ZE                        # "0 <= X < 2^-126: INF ... #Z"
    if c == "inf":
        return sgn, 0
    r = 1 / v
    if abs(r) < Fraction(2) ** f.emin:                # X > 2^126 (2^1022): +-0, no #UE
        return sgn, 0
    return rne_exact(r, f), 0


def isqrt(n):
    if n < 2:
        return n
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def rne_rsqrt(v, f):
    """RNE of 1/sqrt(v), v > 0 rational, by integer square roots: y = 2^-K * sqrt(2^(2K) / v)"""
    t = 1 / v                                          # y^2
    e = floor_log2(t) // 2                             # 2^e <= y < 2^(e+1) (approximately)
    k = f.p + 8 - e                                    # >= p + 7 significant bits in s
    a = t * Fraction(4) ** k
    n = a.numerator // a.denominator
    s = isqrt(n)                                       # floor(2^k * y)
    exact = a.denominator == 1 and s * s == n
    sh = s.bit_length() - f.p
    assert sh >= 6
    c, rem = s >> sh, s & ((1 << sh) - 1)
    half = 1 << (sh - 1)
    if rem > half or (rem == half and (not exact or c & 1)):
        c += 1
    r = Fraction(c) * Fraction(2) ** (sh - k)
    return rne_exact(r, f)


def er_rsqrt28(x, f):
    c, s, v = classify(x, f)
    if c in NAN:
        return quiet(x, f), (IE if c == "snan" else 0)
    if c in ("zero", "denorm"):
        return (f.signbit if s else 0) | f.inf, ZE    # -0 / -denormal -> -INF, +0 / +denormal -> +INF
    if s:
        return f.indef, IE                             # X < 0 (incl. -INF): QNaN_Indefinite
    if c == "inf":
        return 0, 0
    return rne_rsqrt(v, f), 0


_DEC = Context(prec=130, rounding=ROUND_HALF_EVEN, Emax=999999, Emin=-999999)
_LN2 = _DEC.ln(Decimal(2))


def rne_exp2(v, f):
    """RNE of 2^v for a non-integral binary rational v with emin <= 2^v < 2^(emax+1): exp(v ln 2)
    at 130 decimal digits (relative error < 1e-125); the rounding is accepted only when the
    approximation is farther than 1e-110 (relative) from a rounding boundary"""
    d = _DEC.divide(Decimal(v.numerator), Decimal(v.denominator))
    y = _DEC.exp(_DEC.multiply(d, _LN2))
    q = Fraction(y)
    e = floor_log2(q)
    t = q / Fraction(2) ** (e - f.fbits)               # significand, 2^fbits <= t < 2^p
    fl = t.numerator // t.denominator
    rem = t - fl
    margin = t * Fraction(1, 10 ** 110)
    if abs(rem - Fraction(1, 2)) <= margin or rem <= margin or 1 - rem <= margin:
        raise ArithmeticError("exp2 too close to a rounding boundary: %r" % v)
    c = fl + (1 if rem > Fraction(1, 2) else 0)
    return rne_exact(Fraction(c) * Fraction(2) ** (e - f.fbits), f)


def er_exp2(x, f):
    c, s, v = classify(x, f)
    if c in NAN:
        return quiet(x, f), (IE if c == "snan" else 0)
    if c == "inf":
        return (0, 0) if s else (f.inf, 0)             # -INF -> +0, +INF -> +INF
    if c in ("zero", "denorm"):
        return f.one, 0                                # +-0 (and DAZ'd denormals) -> 1.0
    if v >= f.emax + 1:
        return f.inf, OE                               # overflow: +INF (masked), OE
    if v < f.emin:
        return 0, 0                                    # denormal result flushed to +0, no #UE
    if v.denominator == 1:
        return pack(f, False, int(v), 1 << f.fbits), 0  # integral N: 2^N exact
    return rne_exp2(v, f), 0


ER_OPS = {"rcp28": er_rcp28, "rsqrt28": er_rsqrt28, "exp2": er_exp2}


# ---------------------------------------------------------------------------------------------
# encodings (SDM Vol2A 2.7.1 Table 2-32): P0 = R X B R' 0 m m m, P1 = W v v v v 1 p p,
# P2 = z L' L b V' a a a; R X B R' vvvv V' stored inverted
# ---------------------------------------------------------------------------------------------
class Mem:
    """[rsi + disp] (mod 01 with disp8*N when it fits unless disp32)"""
    def __init__(self, disp=0, disp32=False):
        self.disp, self.disp32 = disp, disp32


class Vsib:
    """[base + index*scale + disp] with a vector index; base None = no base (mod 00, base 101)"""
    def __init__(self, index, scale=1, base=RSI, disp=0, disp32=False, nosib=False):
        self.index, self.scale, self.base, self.disp = index, scale, base, disp
        self.disp32, self.nosib = disp32, nosib


def modrm_tail(reg, rm, n, regfield=None):
    """ModRM (+SIB) (+disp) for operand rm; reg is the ModRM.reg value (0-7 bits used)"""
    r3 = reg & 7 if regfield is None else regfield
    if isinstance(rm, Mem):
        d = rm.disp
        if not rm.disp32 and d != 0 and d % n == 0 and -128 <= d // n <= 127:
            return bytes([0x40 | (r3 << 3) | RSI, (d // n) & 0xFF])
        if d == 0 and not rm.disp32:
            return bytes([(r3 << 3) | RSI])
        return bytes([0x80 | (r3 << 3) | RSI]) + (d & 0xFFFFFFFF).to_bytes(4, "little")
    if isinstance(rm, Vsib):
        if rm.nosib:
            return bytes([0x40 | (r3 << 3) | RSI, 0])      # [rsi + disp8]: no SIB byte
        ss = {1: 0, 2: 1, 4: 2, 8: 3}[rm.scale]
        if rm.base is None:
            return (bytes([(r3 << 3) | 4, (ss << 6) | ((rm.index & 7) << 3) | 5]) +
                    (rm.disp & 0xFFFFFFFF).to_bytes(4, "little"))
        sib = (ss << 6) | ((rm.index & 7) << 3) | (rm.base & 7)
        d = rm.disp
        if not rm.disp32 and d != 0 and d % n == 0 and -128 <= d // n <= 127:
            return bytes([0x44 | (r3 << 3), sib, (d // n) & 0xFF])
        if d == 0 and not rm.disp32 and (rm.base & 7) != 5:
            return bytes([0x04 | (r3 << 3), sib])
        return bytes([0x84 | (r3 << 3), sib]) + (d & 0xFFFFFFFF).to_bytes(4, "little")
    return bytes([0xC0 | (r3 << 3) | (rm & 7)])


def evex(pp, w, opc, reg, rm, vvvv=0, ll=2, b=0, z=0, aaa=0, n=1, regfield=None, mmm=2,
         vbar=None):
    """one EVEX instruction (map 0F38 by default); reg / vvvv / register rm 0-31"""
    if isinstance(rm, Mem):
        x, bb, vhi = 0, 0, (vvvv >> 4) & 1
    elif isinstance(rm, Vsib):
        x, bb, vhi = (rm.index >> 3) & 1, ((rm.base or 0) >> 3) & 1, (rm.index >> 4) & 1
    else:
        x, bb, vhi = (rm >> 4) & 1, (rm >> 3) & 1, (vvvv >> 4) & 1
    r, rr = (reg >> 3) & 1, (reg >> 4) & 1
    p0 = ((r ^ 1) << 7) | ((x ^ 1) << 6) | ((bb ^ 1) << 5) | ((rr ^ 1) << 4) | mmm
    p1 = (w << 7) | (((~vvvv) & 0xF) << 3) | 4 | pp
    vb = (vhi ^ 1) if vbar is None else vbar
    p2 = (z << 7) | (ll << 5) | (b << 4) | (vb << 3) | aaa
    return bytes([0x62, p0, p1, p2, opc]) + modrm_tail(reg, rm, n, regfield)


def byte_list(bs):
    return ".byte " + ", ".join("0x%02x" % c for c in bs)


def hexs(buf):
    return "".join("%02X" % c for c in buf)


def le(v, size):
    return (v & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


def lanes(buf, size):
    return [int.from_bytes(buf[i:i + size], "little") for i in range(0, len(buf), size)]


def join(vals, size):
    return b"".join(le(v, size) for v in vals)


# ---------------------------------------------------------------------------------------------
# instruction models on byte buffers
# ---------------------------------------------------------------------------------------------
def sat32(v):
    return max(-(1 << 31), min((1 << 31) - 1, v))


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def m_4vnniw(dest, block, dpos, mem, kmask, zero, sat):
    """VP4DPWSSD[S] (8-16 / 8-19): dest, block = 4 x 64-byte registers, dpos = position of the
    destination in the block or None, mem = 16 bytes; kmask None = no writemask"""
    d = lanes(dest, 4)
    out = list(d)
    t = lanes(mem, 4)
    for i in range(16):
        if kmask is None or (kmask >> i) & 1:
            acc = s32(d[i])
            for m in range(4):
                src = acc & 0xFFFFFFFF if m == dpos else lanes(block[m], 4)[i]
                p1 = s16(src) * s16(t[m])
                p2 = s16(src >> 16) * s16(t[m] >> 16)
                acc = sat32(acc + p1 + p2) if sat else s32(acc + p1 + p2)
            out[i] = acc & 0xFFFFFFFF
        elif zero:
            out[i] = 0
    return join(out, 4)


def m_4fmaps(dest, block, mem, kmask, zero, neg, scalar, mx):
    """V4F[N]MADDPS/SS (8-4 .. 8-7): (result bytes or None on #XM, mxcsr after, fault)"""
    d = lanes(dest, 4)
    r = list(d)
    msrc = lanes(mem, 4)
    n = 1 if scalar else 16
    acc = 0
    for j in range(4):
        step = 0
        src = lanes(block[j], 4)
        for i in range(n):
            if kmask is None or (kmask >> i) & 1:
                r[i], fl = fma(src[i], msrc[j], r[i], F32, mx, neg)
                step |= fl
        unmasked = step & ~(mx >> 7) & 0x3F
        if unmasked & (IE | DE | ZE):
            step &= ~(OE | UE | PE)
        acc |= step
        if unmasked:
            return None, mx | acc, "#XM"
    for i in range(n):
        if not (kmask is None or (kmask >> i) & 1) and zero:
            r[i] = 0
    if scalar:
        r = r[:4] + [0] * 12
    return join(r, 4), mx | acc, None


def m_er(op, f, src, kmask, zero, dest, mx, sae, scalar=False, src1=None):
    """packed (KL = 64 / size) or scalar ER: (result bytes or None on #XM, mxcsr, fault)"""
    fn = ER_OPS[op]
    n = 1 if scalar else 64 // f.size
    s = lanes(src, f.size)
    d = lanes(dest, f.size)
    out = list(d)
    flags = 0
    for i in range(n):
        if kmask is None or (kmask >> i) & 1:
            out[i], fl = fn(s[i], f)
            flags |= fl
        elif zero:
            out[i] = 0
    if scalar:
        upper = lanes(src1, f.size)
        out = [out[0]] + upper[1:16 // f.size] + [0] * (64 // f.size - 16 // f.size)
    if sae:
        return join(out, f.size), mx, None
    unmasked = flags & ~(mx >> 7) & 0x3F
    if unmasked:
        if unmasked & (IE | DE | ZE):
            flags &= ~(OE | UE | PE)
        return None, mx | flags, "#XM"
    return join(out, f.size), mx | flags, None


# ---------------------------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------------------------
def selftest():
    bad = []

    def chk(name, got, want):
        if got != want:
            bad.append("%s: got %r want %r" % (name, got, want))

    h = lambda v: v                                     # noqa: E731
    # encodings against the emu-alltest sweep's assembler (alltest_baseline/alltest.csv rows)
    chk("enc vp4dpwssd", hexs(evex(3, 0, 0x52, 0, Mem(), vvvv=1, n=16)), "62F2774852" + "06")
    chk("enc vp4dpwssds k1", hexs(evex(3, 0, 0x53, 0, Mem(), vvvv=1, aaa=1, n=16)), "62F277495306")
    chk("enc v4fmaddps", hexs(evex(3, 0, 0x9A, 0, Mem(), vvvv=1, n=16)), "62F277489A06")
    chk("enc v4fmaddss", hexs(evex(3, 0, 0x9B, 0, Mem(), vvvv=1, ll=0, n=16)), "62F277089B06")
    chk("enc vgatherpf0dps", hexs(evex(1, 0, 0xC6, 0, Vsib(1), aaa=1, regfield=1, n=4)),
        "62F27D49C60C0E")
    chk("enc vscatterpf1qps", hexs(evex(1, 0, 0xC7, 0, Vsib(1), aaa=1, regfield=6, n=4)),
        "62F27D49C7340E")
    chk("enc vrcp28ss", hexs(evex(1, 0, 0xCB, 0, 2, vvvv=1, ll=0)), "62F27508CBC2")
    chk("enc vrcp28ss sae", hexs(evex(1, 0, 0xCB, 0, 2, vvvv=1, ll=0, b=1)), "62F27518CBC2")
    chk("enc vexp2ps", hexs(evex(1, 0, 0xC8, 0, 2)), "62F27D48C8C2")
    chk("enc vexp2ps bcst", hexs(evex(1, 0, 0xC8, 0, Mem(), b=1, n=4)), "62F27D58C806")
    chk("enc vexp2ps sae", hexs(evex(1, 0, 0xC8, 0, 2, b=1, ll=0)), "62F27D18C8C2")
    for f in (F32, F64):
        one, two = f.one, bits_of(2, f)
        # Tables 8-3..8-6 (VRCP28)
        chk(f.name + " rcp +0", er_rcp28(0, f), (f.inf, ZE))
        chk(f.name + " rcp -0", er_rcp28(f.signbit, f), (f.signbit | f.inf, ZE))
        chk(f.name + " rcp +denorm", er_rcp28(1, f), (f.inf, ZE))
        chk(f.name + " rcp -denorm", er_rcp28(f.signbit | 1, f), (f.signbit | f.inf, ZE))
        chk(f.name + " rcp +inf", er_rcp28(f.inf, f), (0, 0))
        chk(f.name + " rcp -inf", er_rcp28(f.signbit | f.inf, f), (f.signbit, 0))
        chk(f.name + " rcp qnan", er_rcp28(f.inf | f.quietbit | 5, f), (f.inf | f.quietbit | 5, 0))
        chk(f.name + " rcp snan", er_rcp28(f.signbit | f.inf | 5, f),
            (f.signbit | f.inf | f.quietbit | 5, IE))
        lim = bits_of(Fraction(2) ** (f.emax - 1), f)       # 2^126 / 2^1022
        chk(f.name + " rcp 2^(emax-1)", er_rcp28(lim, f), (bits_of(Fraction(2) ** -(f.emax - 1), f), 0))
        chk(f.name + " rcp > 2^(emax-1)", er_rcp28(lim + 1, f), (0, 0))
        chk(f.name + " rcp < -2^(emax-1)", er_rcp28(f.signbit | (lim + 1), f), (f.signbit, 0))
        chk(f.name + " rcp max", er_rcp28(f.maxf, f), (0, 0))
        for nexp in range(-(f.emax - 1), f.emax):
            x = bits_of(Fraction(2) ** -nexp, f)
            chk(f.name + " rcp 2^-n", er_rcp28(x, f), (bits_of(Fraction(2) ** nexp, f), 0))
            chk(f.name + " rcp -2^-n", er_rcp28(f.signbit | x, f),
                (f.signbit | bits_of(Fraction(2) ** nexp, f), 0))
        # Tables 8-7..8-10 (VRSQRT28)
        chk(f.name + " rsqrt +0", er_rsqrt28(0, f), (f.inf, ZE))
        chk(f.name + " rsqrt -0", er_rsqrt28(f.signbit, f), (f.signbit | f.inf, ZE))
        chk(f.name + " rsqrt -denorm", er_rsqrt28(f.signbit | 3, f), (f.signbit | f.inf, ZE))
        chk(f.name + " rsqrt +denorm", er_rsqrt28(3, f), (f.inf, ZE))
        chk(f.name + " rsqrt +inf", er_rsqrt28(f.inf, f), (0, 0))
        chk(f.name + " rsqrt -inf", er_rsqrt28(f.signbit | f.inf, f), (f.indef, IE))
        chk(f.name + " rsqrt -1", er_rsqrt28(f.signbit | one, f), (f.indef, IE))
        chk(f.name + " rsqrt snan", er_rsqrt28(f.inf | 1, f), (f.inf | f.quietbit | 1, IE))
        for nn in range(-(f.emax // 2), f.emax // 2 + 1):
            x = bits_of(Fraction(2) ** (-2 * nn), f)
            chk(f.name + " rsqrt 2^-2n", er_rsqrt28(x, f), (bits_of(Fraction(2) ** nn, f), 0))
        chk(f.name + " rsqrt 4", er_rsqrt28(bits_of(4, f), f), (bits_of(Fraction(1, 2), f), 0))
        # Tables 8-1 / 8-2 (VEXP2)
        chk(f.name + " exp2 +0", er_exp2(0, f), (one, 0))
        chk(f.name + " exp2 -0", er_exp2(f.signbit, f), (one, 0))
        chk(f.name + " exp2 denorm", er_exp2(f.signbit | 7, f), (one, 0))
        chk(f.name + " exp2 +inf", er_exp2(f.inf, f), (f.inf, 0))
        chk(f.name + " exp2 -inf", er_exp2(f.signbit | f.inf, f), (0, 0))
        chk(f.name + " exp2 snan", er_exp2(f.signbit | f.inf | 2, f),
            (f.signbit | f.inf | f.quietbit | 2, IE))
        chk(f.name + " exp2 1", er_exp2(one, f), (two, 0))
        for nn in range(f.emin, f.emax + 1):
            chk(f.name + " exp2 N", er_exp2(bits_of(nn, f), f), (bits_of(Fraction(2) ** nn, f), 0))
        chk(f.name + " exp2 emax+1", er_exp2(bits_of(f.emax + 1, f), f), (f.inf, OE))
        chk(f.name + " exp2 emin-1", er_exp2(f.signbit | bits_of(-(f.emin - 1), f), f), (0, 0))
        chk(f.name + " exp2 0.5", er_exp2(bits_of(Fraction(1, 2), f), f),
            (rne_rsqrt(Fraction(1, 2), f), 0))       # 2^0.5 = 1/sqrt(1/2): two independent paths
        chk(f.name + " exp2 -0.5", er_exp2(f.signbit | bits_of(Fraction(1, 2), f), f),
            (rne_rsqrt(Fraction(2), f), 0))
        # documented bounds on random normal inputs: relative error of the result
        rnd = random.Random(1234 + f.bits)
        for _ in range(300):
            e = rnd.randint(f.emin + 2, f.emax - 2)
            m = rnd.getrandbits(f.fbits)
            x = ((e + f.bias) << f.fbits) | m
            v = value(x, f)
            r = value(er_rcp28(x, f)[0], f)
            if abs(r * v - 1) >= Fraction(1, 2 ** (23 if f is F32 else 28)):
                bad.append("rcp bound %x" % x)
            r = value(er_rsqrt28(x, f)[0], f)
            # |r - y| / y with y^2 = 1/v: compare r^2 v against 1 (|r^2 v - 1| < 2 * bound)
            if abs(r * r * v - 1) >= 2 * Fraction(1, 2 ** (23 if f is F32 else 28)):
                bad.append("rsqrt bound %x" % x)
        for _ in range(60):
            xv = Fraction(rnd.randint(-(f.emax - 2) * 2 ** 20, (f.emax - 2) * 2 ** 20), 2 ** 20)
            x = bits_of(xv, f) if xv else 0
            r, fl = er_exp2(x, f)
            if fl or x == 0:
                continue
            # 2^x via the rsqrt path is unavailable; check 2^x * 2^-x == 1 within 2 ulps instead
            r2 = er_exp2(x ^ f.signbit, f)[0]
            prod = value(r, f) * value(r2, f)
            if abs(prod - 1) > Fraction(3, 2 ** f.fbits):
                bad.append("exp2 consistency %x" % x)
    # 4VNNIW: Figure 8-1 c0 = c0 + a0*b0 + a1*b1 (one step shown), saturation
    blk = [join([0x00030002] * 16, 4)] + [bytes(64)] * 3
    mem = join([0x00050004, 0, 0, 0], 4)
    chk("4vnniw fig 8-1", lanes(m_4vnniw(join([7] * 16, 4), blk, None, mem, None, False, False),
                                4)[0], 7 + 2 * 4 + 3 * 5)
    blk = [join([0x80008000] * 16, 4)] * 4
    mem = join([0x80008000] * 4, 4)
    chk("4vnniw wrap", lanes(m_4vnniw(join([0x7FFFFFFF] * 16, 4), blk, None, mem, None, False,
                                      False), 4)[0], (0x7FFFFFFF + 8 * 0x40000000) & 0xFFFFFFFF)
    chk("4vnniws sat", lanes(m_4vnniw(join([0x7FFFFFFF] * 16, 4), blk, None, mem, None, False,
                                      True), 4)[0], 0x7FFFFFFF)
    # FMA: 1 * 2 + 3, -(1 * 2) + 3, NaN priority x, y, z
    o32 = lambda v: bits_of(v, F32)                     # noqa: E731
    chk("fma", fma(o32(1), o32(2), o32(3), F32, MXCSR_DEFAULT, False), (o32(5), 0))
    chk("fnma", fma(o32(1), o32(2), o32(3), F32, MXCSR_DEFAULT, True), (o32(1), 0))
    chk("fma nan y", fma(o32(1), 0x7F800001, 0x7FC00002, F32, MXCSR_DEFAULT, False),
        (0x7FC00001, IE))
    chk("fma inf*0+qnan", fma(F32.inf, 0, 0x7FC00002, F32, MXCSR_DEFAULT, False), (0x7FC00002, 0))
    chk("fma 1/3", fma(o32(1), o32(1), bits_of(Fraction(1, 3), F32), F32, MXCSR_DEFAULT, False)[1],
        PE)
    if bad:
        for b in bad[:40]:
            print("FAIL", b)
        print("selftest: %d failures" % len(bad))
        return 1
    print("selftest: ok")
    return 0


# ---------------------------------------------------------------------------------------------
# case generation
# ---------------------------------------------------------------------------------------------
OUT = []


def emit(s):
    OUT.append(s)


def comment(s):
    emit("# " + s)


def zmm_in(n, buf):
    return "zmm%d=%s" % (n, hexs(buf))


def rnd_bytes(rnd, n):
    return bytes(rnd.getrandbits(8) for _ in range(n))


def kregs_in(rnd):
    return ["k%d=0x%X" % (i, rnd.getrandbits(64)) for i in range(8)]


def case(enc, inputs, expects, title=None):
    if title:
        comment(title)
    emit(("%s | %s => %s" % (byte_list(enc), " ".join(inputs), " ".join(expects))).rstrip())


# ---- 4VNNIW -----------------------------------------------------------------------------------
def gen_4vnniw(rnd):
    comment("--- AVX512_4VNNIW (ledger U991): VP4DPWSSD (EVEX.512.F2.0F38.W0 52), VP4DPWSSDS (53)")
    comment("zmm1{k1}{z}, zmm2+3 (block vvvv & ~3), m128: Tuple1_4X (disp8*N, N = 16), E4,")
    comment("16 bytes read when any of k1[15:0] is set or no mask (SDM 092 Vol2D 8-16 .. 8-19)")
    wordpal = [0, 1, -1, 2, -2, 0x7FFF, -0x8000, 0x1234, -0x4321, 0x100, 3, 0x4000]

    def rw():
        return rnd.choice(wordpal) if rnd.random() < 0.5 else rnd.randint(-0x8000, 0x7FFF)

    def rdword():
        return ((rw() & 0xFFFF) | ((rw() & 0xFFFF) << 16))

    def rvec():
        return join([rdword() for _ in range(16)], 4)

    for sat, opc, name in ((False, 0x52, "VP4DPWSSD"), (True, 0x53, "VP4DPWSSDS")):
        configs = [
            dict(dst=0, base=4, k=None, z=False, disp=0, t="no mask"),
            dict(dst=1, base=8, k=0x5A5A, z=False, disp=0x40, t="k merge, disp8*16 = 0x40"),
            dict(dst=2, base=12, k=0x0F0F, z=True, disp=-0x10, t="k zero, disp8 -1*16"),
            dict(dst=29, base=16, vv=19, k=None, z=False, disp=0x18, d32=True,
                 t="zmm29 dest, block zmm16-19 (vvvv = 19 -> base 16), disp32 0x18"),
            dict(dst=17, base=20, vv=22, k=0xFFFF0000FFFFFFFF, z=False, disp=0,
                 t="k1 bits above 15 ignored, vvvv = 22 -> base 20"),
            dict(dst=5, base=4, vv=6, k=None, z=False, disp=0,
                 t="DEST = zmm5 inside the block zmm4-7 (pseudocode literal: DEST updated in place)"),
            dict(dst=3, base=0, vv=3, k=0x00FF, z=False, disp=0,
                 t="DEST = zmm3 = the last block register, vvvv = 3 -> base 0"),
        ]
        for c in configs:
            vv = c.get("vv", c["base"])
            aaa = 1 if c["k"] is not None else 0
            mem_off = MEM_RSI + c["disp"]
            enc = evex(3, 0, opc, c["dst"], Mem(c["disp"], c.get("d32", False)), vvvv=vv,
                       z=1 if c["z"] else 0, aaa=aaa, n=16)
            regs = {c["dst"]: rvec()}
            for m in range(4):
                if c["base"] + m != c["dst"]:
                    regs[c["base"] + m] = rvec()
            if sat:     # push some lanes to saturation
                regs[c["dst"]] = join([rnd.choice([0x7FFFFF00, 0x80000100, rdword()])
                                       for _ in range(16)], 4)
            mem = join([rdword() for _ in range(4)], 4)
            ins = [zmm_in(r, v) for r, v in sorted(regs.items())]
            ins += ["k1=0x%X" % c["k"]] if c["k"] is not None else []
            ins.append("m+0x%X=%s" % (mem_off, hexs(mem)))
            dpos = c["dst"] - c["base"] if 0 <= c["dst"] - c["base"] < 4 else None
            block = [regs[c["base"] + m] for m in range(4)]
            res = m_4vnniw(regs[c["dst"]], block, dpos, mem,
                           None if c["k"] is None else c["k"] & 0xFFFF, c["z"], sat)
            case(enc, ins, [zmm_in(c["dst"], res)], "%s %s" % (name, c["t"]))
        # fault suppression: the operand straddles into the unmapped page MEM + 0x10000
        enc = evex(3, 0, opc, 0, Mem(0x7FF8, True), vvvv=4, aaa=2, n=16)
        dst = rvec()
        case(enc, [zmm_in(0, dst), "k2=0xFFFF0000"], [],
             "%s k2[15:0] = 0: memory not read, no #PF (merging: DEST unchanged)" % name)
        enc = evex(3, 0, opc, 0, Mem(0x7FF8, True), vvvv=4, aaa=2, z=1, n=16)
        case(enc, [zmm_in(0, dst), "k2=0xFFFF0000"], [zmm_in(0, bytes(64))],
             "%s k2[15:0] = 0 zeroing: DEST zeroed, no #PF" % name)
        enc = evex(3, 0, opc, 0, Mem(0x7FF8, True), vvvv=4, aaa=2, n=16)
        case(enc, [zmm_in(0, dst), "k2=0x8000"], ["#PF"],
             "%s k2[15] = 1: the whole 16 bytes are read: #PF" % name)
        enc = evex(3, 0, opc, 0, Mem(0x7FF8, True), vvvv=4, n=16)
        case(enc, [zmm_in(0, dst)], ["#PF"], "%s no mask: #PF" % name)
        # #UD forms
        for t, e in (("EVEX.b = 1", evex(3, 0, opc, 0, Mem(), vvvv=4, b=1, n=16)),
                     ("ModRM.mod = 11b", evex(3, 0, opc, 0, 1, vvvv=4)),
                     ("EVEX.W1", evex(3, 1, opc, 0, Mem(), vvvv=4, n=16)),
                     ("EVEX.L'L = 01b (256)", evex(3, 0, opc, 0, Mem(), vvvv=4, ll=1, n=16)),
                     ("EVEX.L'L = 00b (128)", evex(3, 0, opc, 0, Mem(), vvvv=4, ll=0, n=16)),
                     ("EVEX.L'L = 11b", evex(3, 0, opc, 0, Mem(), vvvv=4, ll=3, n=16)),
                     ("EVEX.z without a mask", evex(3, 0, opc, 0, Mem(), vvvv=4, z=1, n=16))):
            case(e, [], ["#UD"], "%s %s: #UD" % (name, t))


# ---- 4FMAPS ----------------------------------------------------------------------------------
def gen_4fmaps(rnd):
    comment("--- AVX512_4FMAPS (ledger U992): V4FMADDPS (EVEX.512.F2.0F38.W0 9A), V4FNMADDPS (AA),")
    comment("V4FMADDSS (EVEX.LLIG 9B), V4FNMADDSS (AB): 4 sequential FMAs, rounding at each, MXCSR.RC,")
    comment("exceptions per step (SDM 092 Vol2D 8-4 .. 8-7, Type E2); Tuple1_4X N = 16")
    o = lambda v: bits_of(v, F32)                       # noqa: E731
    pal = [o(1), o(-1), o(Fraction(3, 2)), o(Fraction(1, 3)), o(1000), o(-0.25), 0, 0x80000000,
           o(Fraction(7, 8)), o(2.5), o(-6), o(Fraction(1, 1024))]

    def rf(exotic=False):
        if exotic and rnd.random() < 0.3:
            return rnd.choice([0x7F800000, 0xFF800000, 0x7FC00011, 0x7F800021, 0x00000003,
                               0x80400000, 0x7F7FFFFF, 0x00800000])
        if rnd.random() < 0.6:
            return rnd.choice(pal)
        e = rnd.randint(-20, 20) + 127
        return (rnd.getrandbits(1) << 31) | (e << 23) | rnd.getrandbits(23)

    def rvec(exotic=False):
        return join([rf(exotic) for _ in range(16)], 4)

    def one(name, opc, neg, scalar, dst, base, vv, k, z, mx, disp=0, exotic=False, title="",
            regs=None, mem=None):
        enc = evex(3, 0, opc, dst, Mem(disp), vvvv=vv, ll=0 if scalar else 2, z=1 if z else 0,
                   aaa=1 if k is not None else 0, n=16)
        if regs is None:
            regs = {dst: rvec(exotic)}
            for m in range(4):
                if base + m != dst:
                    regs[base + m] = rvec(exotic)
        if mem is None:
            mem = join([rf(exotic) for _ in range(4)], 4)
        block = [regs.get(base + m, bytes(64)) for m in range(4)]
        km = None if k is None else (k & (1 if scalar else 0xFFFF))
        res, mxo, fault = m_4fmaps(regs.get(dst, bytes(64)), block, mem, km, z, neg, scalar, mx)
        ins = [zmm_in(r, v) for r, v in sorted(regs.items())]
        ins += ["k1=0x%X" % k] if k is not None else []
        ins.append("m+0x%X=%s" % (MEM_RSI + disp, hexs(mem)))
        if mx != MXCSR_DEFAULT:
            ins.append("mxcsr=0x%X" % mx)
        exp = []
        if res is not None and res != regs.get(dst, bytes(64)):
            exp.append(zmm_in(dst, res))
        if mxo != mx:
            exp.append("mxcsr=0x%X" % mxo)
        if fault:
            exp.append(fault)
        case(enc, ins, exp, "%s %s" % (name, title))

    forms = ((0x9A, False, False, "V4FMADDPS"), (0xAA, True, False, "V4FNMADDPS"),
             (0x9B, False, True, "V4FMADDSS"), (0xAB, True, True, "V4FNMADDSS"))
    for opc, neg, scalar, name in forms:
        for tries in range(6):
            try:
                one(name, opc, neg, scalar, 0, 4, 4, None, False, MXCSR_DEFAULT, 0,
                    title="no mask, ordinary values")
                break
            except NotModelled:
                continue
        cfgs = [
            dict(dst=1, base=8, vv=9, k=0x3C3C, z=False, mx=MXCSR_DEFAULT, disp=0x20,
                 t="k merge, vvvv 9 -> block zmm8-11, disp8*16"),
            dict(dst=2, base=12, vv=15, k=0xF00F, z=True, mx=MXCSR_DEFAULT, disp=-0x30,
                 t="k zero, vvvv 15 -> block zmm12-15"),
            dict(dst=30, base=24, vv=27, k=None, z=False, mx=MXCSR_DEFAULT | (RZ << 13), disp=0,
                 t="MXCSR.RC = RZ, zmm30 dest, block zmm24-27"),
            dict(dst=21, base=16, vv=16, k=None, z=False, mx=MXCSR_DEFAULT | (RU << 13), disp=0,
                 t="MXCSR.RC = RU"),
            dict(dst=22, base=16, vv=17, k=None, z=False, mx=MXCSR_DEFAULT | (RD << 13), disp=0,
                 t="MXCSR.RC = RD"),
            dict(dst=6, base=4, vv=4, k=None, z=False, mx=MXCSR_DEFAULT, disp=0,
                 t="DEST zmm6 inside the block zmm4-7: the block is read as before the instruction"),
            dict(dst=7, base=8, vv=10, k=0xFFFF, z=False, mx=MXCSR_DEFAULT | DAZ | FTZ, disp=0,
                 exotic=True, t="specials (NaN, inf, denormals) with DAZ = FTZ = 1, masked flags"),
            dict(dst=9, base=12, vv=12, k=None, z=False, mx=MXCSR_DEFAULT, disp=0, exotic=True,
                 t="specials with DAZ = FTZ = 0, masked flags"),
        ]
        for c in cfgs:
            for tries in range(20):
                try:
                    one(name, opc, neg, scalar, c["dst"], c["base"], c["vv"], c["k"], c["z"],
                        c["mx"], c["disp"], c.get("exotic", False), c["t"])
                    break
                except NotModelled:
                    continue
            else:
                raise RuntimeError("no modelled case for " + c["t"])
        # sequential exceptions: step 2 (j = 2) produces an SNaN invalid with IM = 0; steps 0/1
        # inexact (masked PE): #XM, DEST unchanged, MXCSR = PE (steps 0..1) | IE (step 2)
        third = o(Fraction(1, 3))
        regs = {0: join([o(1)] * 16, 4), 4: join([third] * 16, 4), 5: join([third] * 16, 4),
                6: join([0x7F800001] + [o(1)] * 15, 4), 7: join([o(2)] * 16, 4)}
        mem = join([o(1), o(1), o(1), o(1)], 4)
        one(name, opc, neg, scalar, 0, 4, 4, None, False, MXCSR_DEFAULT & ~0x80, 0,
            title="IM = 0: SNaN in step 2 -> #XM after steps 0, 1 (PE) and 2 (IE)",
            regs=regs, mem=mem)
        # the same with step 3 being the only one: step 0..2 exact, IE in step 3
        regs = {0: join([o(1)] * 16, 4), 4: join([o(1)] * 16, 4), 5: join([o(2)] * 16, 4),
                6: join([o(3)] * 16, 4), 7: join([o(4)] * 16, 4)}
        mem = join([o(1), o(1), o(1), 0x7F800001], 4)
        one(name, opc, neg, scalar, 0, 4, 4, None, False, MXCSR_DEFAULT & ~0x80, 0,
            title="IM = 0: SNaN multiplier in step 3 -> #XM, steps 0..2 exact", regs=regs, mem=mem)
        # PM = 0: the first inexact step stops the instruction
        regs = {0: join([o(1)] * 16, 4), 4: join([o(2)] * 16, 4), 5: join([third] * 16, 4),
                6: join([third] * 16, 4), 7: join([third] * 16, 4)}
        mem = join([o(1), o(1), o(1), o(1)], 4)
        one(name, opc, neg, scalar, 0, 4, 4, None, False, MXCSR_DEFAULT & ~0x1000, 0,
            title="PM = 0: step 0 exact, step 1 inexact -> #XM with PE", regs=regs, mem=mem)
        # DM = 0 and a denormal in step 1 together with an inexact result: pre-computation DE
        # suppresses PE of that step (Vol1 11.5.2), PE of step 0 stays
        regs = {0: join([o(1)] * 16, 4), 4: join([third] * 16, 4), 5: join([0x00000005] * 16, 4),
                6: join([o(1)] * 16, 4), 7: join([o(1)] * 16, 4)}
        mem = join([o(1), third, o(1), o(1)], 4)
        one(name, opc, neg, scalar, 0, 4, 4, None, False, MXCSR_DEFAULT & ~0x100, 0,
            title="DM = 0: denormal in step 1 -> #XM with PE (step 0) | DE (step 1)", regs=regs,
            mem=mem)
        # masked-off lanes raise nothing: lane 15 holds an SNaN, k1 = 0x7FFF, IM = 0
        regs = {0: join([o(1)] * 16, 4), 4: join([o(1)] * 15 + [0x7F800001], 4),
                5: join([o(1)] * 16, 4), 6: join([o(1)] * 16, 4), 7: join([o(1)] * 16, 4)}
        mem = join([o(1)] * 4, 4)
        one(name, opc, neg, scalar, 0, 4, 4, 0x7FFF, False, MXCSR_DEFAULT & ~0x80, 0,
            title="IM = 0, SNaN only in masked-off lane 15 / element 1: no #XM", regs=regs, mem=mem)
        # fault suppression
        enc = evex(3, 0, opc, 0, Mem(0x7FF8, True), vvvv=4, ll=0 if scalar else 2, aaa=2, n=16)
        case(enc, ["k2=0x%X" % (0xFFFE if scalar else 0xFFFF0000)], [],
             "%s %s = 0: memory not read, no #PF" % (name, "k2[0]" if scalar else "k2[15:0]"))
        case(enc, ["k2=0x1"], ["#PF"], "%s k2[0] = 1: whole operand read, #PF" % name)
        for t, e in (("EVEX.b = 1", evex(3, 0, opc, 0, Mem(), vvvv=4, ll=0 if scalar else 2, b=1,
                                          n=16)),
                     ("ModRM.mod = 11b", evex(3, 0, opc, 0, 1, vvvv=4, ll=0 if scalar else 2)),
                     ("EVEX.W1", evex(3, 1, opc, 0, Mem(), vvvv=4, ll=0 if scalar else 2, n=16)),
                     ("EVEX.z without a mask", evex(3, 0, opc, 0, Mem(), vvvv=4,
                                                    ll=0 if scalar else 2, z=1, n=16))):
            case(e, [], ["#UD"], "%s %s: #UD" % (name, t))
        if not scalar:
            case(evex(3, 0, opc, 0, Mem(), vvvv=4, ll=1, n=16), [], ["#UD"],
                 "%s EVEX.L'L = 01b: #UD (EVEX.512 only)" % name)
        else:
            # LLIG: L'L = 01b / 10b / 11b all run as the scalar form
            regs = {0: join([o(5)] + [0x11111111] * 15, 4), 4: join([o(1)] * 16, 4),
                    5: join([o(2)] * 16, 4), 6: join([o(3)] * 16, 4), 7: join([o(4)] * 16, 4)}
            mem = join([o(1), o(1), o(1), o(1)], 4)
            for ll in (1, 2, 3):
                enc = evex(3, 0, opc, 0, Mem(), vvvv=4, ll=ll, n=16)
                res, mxo, _ = m_4fmaps(regs[0], [regs[4 + m] for m in range(4)], mem, None, False,
                                       neg, True, MXCSR_DEFAULT)
                case(enc, [zmm_in(r, v) for r, v in sorted(regs.items())] +
                     ["m+0x%X=%s" % (MEM_RSI, hexs(mem))],
                     [zmm_in(0, res)] + (["mxcsr=0x%X" % mxo] if mxo != MXCSR_DEFAULT else []),
                     "%s EVEX.L'L = %d: LLIG (bits 127:32 kept, 511:128 zeroed)" % (name, ll))


# ---- PF --------------------------------------------------------------------------------------
PF_FORMS = [  # (name, opcode, W, ModRM.reg, data size, index size)
    ("VGATHERPF0DPS", 0xC6, 0, 1, 4, 4), ("VGATHERPF0QPS", 0xC7, 0, 1, 4, 8),
    ("VGATHERPF0DPD", 0xC6, 1, 1, 8, 4), ("VGATHERPF0QPD", 0xC7, 1, 1, 8, 8),
    ("VGATHERPF1DPS", 0xC6, 0, 2, 4, 4), ("VGATHERPF1QPS", 0xC7, 0, 2, 4, 8),
    ("VGATHERPF1DPD", 0xC6, 1, 2, 8, 4), ("VGATHERPF1QPD", 0xC7, 1, 2, 8, 8),
    ("VSCATTERPF0DPS", 0xC6, 0, 5, 4, 4), ("VSCATTERPF0QPS", 0xC7, 0, 5, 4, 8),
    ("VSCATTERPF0DPD", 0xC6, 1, 5, 8, 4), ("VSCATTERPF0QPD", 0xC7, 1, 5, 8, 8),
    ("VSCATTERPF1DPS", 0xC6, 0, 6, 4, 4), ("VSCATTERPF1QPS", 0xC7, 0, 6, 4, 8),
    ("VSCATTERPF1DPD", 0xC6, 1, 6, 8, 4), ("VSCATTERPF1QPD", 0xC7, 1, 6, 8, 8),
]


def gen_pf(rnd):
    comment("--- AVX512PF (ledger U993): VGATHERPF0/1 (66.0F38 C6/C7 /1 /2), VSCATTERPF0/1 (/5 /6),")
    comment("EVEX.512, k1 required, T1S (disp8*N, N = data size), E12NP: hints only, no fault, the")
    comment("mask unchanged (SDM 092 Vol2D 8-12 .. 8-15, 8-36 .. 8-39). Indices aimed at unmapped and")
    comment("non-canonical addresses: nothing faults, nothing changes.")
    for name, opc, w, regf, dsz, isz in PF_FORMS:
        idx = 1 + rnd.randint(0, 2)
        enc = evex(1, w, opc, 0, Vsib(idx, scale=8, disp=dsz * 3), aaa=3, regfield=regf, n=dsz)
        if isz == 4:
            index = join([rnd.choice([0x7FFFFFFF, 0x80000000, 0x10000 // 8, 0x2000]) for _ in
                          range(16)], 4)
        else:
            index = join([rnd.choice([0x7FFF000000000000, 0x10000 // 8, 0xFFFF800000000000 // 8])
                          for _ in range(8)], 8)
        case(enc, [zmm_in(idx, index), "k3=0x%X" % rnd.getrandbits(64)], [],
             "%s vm%d{k3}, scale 8, disp8*%d: no fault, k3 unchanged" % (name, isz * 8, dsz))
        # index register zmm16-31 (EVEX.V'), base r14-free form without base (disp32 only)
        enc = evex(1, w, opc, 0, Vsib(25, scale=1, base=None, disp=0x7FFFFFF0), aaa=7,
                   regfield=regf, n=dsz)
        case(enc, [zmm_in(25, bytes(rnd.getrandbits(8) | 0x80 for _ in range(64))),
                   "k7=0xFFFFFFFFFFFFFFFF"], [],
             "%s no base, index zmm25 (EVEX.V'), disp32: no fault" % name)
    name, opc, w, regf, dsz, isz = PF_FORMS[0]
    for t, e in (
            ("k0 (EVEX.aaa = 000b)", evex(1, 0, 0xC6, 0, Vsib(1), aaa=0, regfield=1, n=4)),
            ("EVEX.z = 1", evex(1, 0, 0xC6, 0, Vsib(1), aaa=1, z=1, regfield=1, n=4)),
            ("no SIB byte (ModRM.rm != 100b)", evex(1, 0, 0xC6, 0, Vsib(1, nosib=True), aaa=1,
                                                    regfield=1, n=4)),
            ("ModRM.mod = 11b", evex(1, 0, 0xC6, 0, 1, aaa=1, regfield=1)),
            ("EVEX.b = 1", evex(1, 0, 0xC6, 0, Vsib(1), aaa=1, b=1, regfield=1, n=4)),
            ("EVEX.L'L = 01b", evex(1, 0, 0xC6, 0, Vsib(1), aaa=1, ll=1, regfield=1, n=4)),
            ("EVEX.L'L = 00b", evex(1, 0, 0xC6, 0, Vsib(1), aaa=1, ll=0, regfield=1, n=4)),
            ("EVEX.vvvv != 1111b", evex(1, 0, 0xC6, 0, Vsib(1), vvvv=2, aaa=1, regfield=1, n=4)),
            ("ModRM.reg = 0", evex(1, 0, 0xC6, 0, Vsib(1), aaa=1, regfield=0, n=4)),
            ("ModRM.reg = 3", evex(1, 0, 0xC7, 0, Vsib(1), aaa=1, regfield=3, n=4)),
            ("ModRM.reg = 4", evex(1, 1, 0xC6, 0, Vsib(1), aaa=1, regfield=4, n=8)),
            ("ModRM.reg = 7", evex(1, 1, 0xC7, 0, Vsib(1), aaa=1, regfield=7, n=8)),
            ("no 66 (NP)", evex(0, 0, 0xC6, 0, Vsib(1), aaa=1, regfield=1, n=4)),
            ("F3", evex(2, 0, 0xC6, 0, Vsib(1), aaa=1, regfield=1, n=4))):
        case(e, ["k1=0xFFFF"], ["#UD"], "VGATHERPF/VSCATTERPF %s: #UD" % t)


# ---- ER --------------------------------------------------------------------------------------
ER_FORMS = [  # (name, opcode, op, fmt, scalar)
    ("VEXP2PS", 0xC8, "exp2", F32, False), ("VEXP2PD", 0xC8, "exp2", F64, False),
    ("VRCP28PS", 0xCA, "rcp28", F32, False), ("VRCP28PD", 0xCA, "rcp28", F64, False),
    ("VRCP28SS", 0xCB, "rcp28", F32, True), ("VRCP28SD", 0xCB, "rcp28", F64, True),
    ("VRSQRT28PS", 0xCC, "rsqrt28", F32, False), ("VRSQRT28PD", 0xCC, "rsqrt28", F64, False),
    ("VRSQRT28SS", 0xCD, "rsqrt28", F32, True), ("VRSQRT28SD", 0xCD, "rsqrt28", F64, True),
]


def er_values(rnd, op, f, n, kind):
    o = lambda v: bits_of(v, f)                         # noqa: E731
    sp = [0, f.signbit, 1, f.signbit | 3, f.inf, f.signbit | f.inf, f.inf | f.quietbit | 9,
          f.inf | 9, f.signbit | f.inf | 1, o(1), o(-1), o(2), o(4), o(Fraction(1, 4)), f.maxf,
          f.signbit | f.maxf, 1 << f.fbits]
    if op == "exp2":
        sp += [o(f.emax + 1), o(f.emax), o(f.emin), o(f.emin - 1), o(Fraction(1, 2)),
               o(-Fraction(1, 2)), o(Fraction(f.emax * 2 + 1, 2)), o(-Fraction(f.emax * 2 - 1, 2)),
               o(Fraction(1, 3) if False else Fraction(3, 8)), o(-10)]
    if op == "rcp28":
        sp += [o(Fraction(2) ** (f.emax - 1)), (o(Fraction(2) ** (f.emax - 1)) + 1),
               f.signbit | (o(Fraction(2) ** (f.emax - 1)) + 1), o(3), o(Fraction(1, 3)), o(10)]
    if op == "rsqrt28":
        sp += [o(Fraction(1, 16)), o(2), o(3), o(Fraction(2) ** (f.emax - 1)), o(-5)]
    out = []
    for i in range(n):
        if kind == "special" and i < len(sp):
            out.append(sp[(i + rnd.randint(0, 0)) % len(sp)])
            continue
        if kind == "special":
            out.append(rnd.choice(sp))
            continue
        if op == "exp2":
            v = Fraction(rnd.randint(-(f.emax - 1) * 4096, f.emax * 4096), 4096)
            if rnd.random() < 0.5:
                v = Fraction(rnd.randint(-2 ** 30, 2 ** 30), 2 ** 30)
            out.append(o(v) if v else 0)
        else:
            e = rnd.randint(f.emin + 1, f.emax - 1)
            out.append((rnd.getrandbits(1) * f.signbit if op == "rcp28" else 0) |
                       ((e + f.bias) << f.fbits) | rnd.getrandbits(f.fbits))
    return out


def gen_er(rnd):
    comment("--- AVX512ER (ledger U994): VEXP2PS/PD (66.0F38 C8), VRCP28PS/PD (CA), VRCP28SS/SD (CB),")
    comment("VRSQRT28PS/PD (CC), VRSQRT28SS/SD (CD) (SDM 092 Vol2D 8-8 .. 8-35). MODEL CHOICE (the SDM")
    comment("gives only the error bound and Tables 8-1..8-10): the exact 1/x, 1/sqrt(x), 2^x rounded to")
    comment("nearest even (documented compliance, NOT bit-exact to silicon); DAZ/FTZ always, MXCSR.RC")
    comment("unused; flags IE (SNaN; RSQRT28 negative), ZE (zero/denormal input), OE (EXP2 overflow).")
    for name, opc, op, f, scalar in ER_FORMS:
        w = 1 if f is F64 else 0
        n = 1 if scalar else 64 // f.size
        es = f.size
        for kind, k, z, mx, srcform, sae, t in (
                ("random", None, False, MXCSR_DEFAULT, "reg", False, "random normal inputs"),
                ("special", None, False, MXCSR_DEFAULT, "reg", False,
                 "Table special values, masked flags"),
                ("special", 0xA5A5, False, MXCSR_DEFAULT | (RU << 13) | FTZ, "mem", False,
                 "specials from memory, k merge, RC = RU / FTZ ignored"),
                ("random", 0x3C3C, True, MXCSR_DEFAULT & ~0x40 & ~0x8000, "bcst", False,
                 "{1toN} broadcast, k zero"),
                ("special", None, False, MXCSR_DEFAULT & ~0x80 & ~0x200 & ~0x400, "reg", True,
                 "{sae}: IM = ZM = OM = 0 but no flag and no #XM"),
                ("special", None, False, MXCSR_DEFAULT & ~0x200, "reg", False,
                 "ZM = 0: #XM when a lane divides by zero")):
            if scalar and srcform == "bcst":
                srcform, t = "mem", "memory operand, k zero"
            srcvals = er_values(rnd, op, f, 64 // es, kind)
            if kind == "special" and scalar:
                srcvals = [rnd.choice(srcvals)] + srcvals[1:]
            src = join(srcvals, es)
            if srcform == "bcst":
                src = join([srcvals[0]] * (64 // es), es)
            dst_n, src_n, v_n = (3, 5, 9) if not scalar else (3, 5, 9)
            dest = bytes(rnd.getrandbits(8) for _ in range(64))
            src1 = bytes(rnd.getrandbits(8) for _ in range(64))
            aaa = 1 if k is not None else 0
            if srcform == "reg":
                enc = evex(1, w, opc, dst_n, src_n, vvvv=v_n if scalar else 0,
                           ll=0 if (scalar or sae) else 2, b=1 if sae else 0, z=1 if z else 0,
                           aaa=aaa)
            elif srcform == "mem":
                enc = evex(1, w, opc, dst_n, Mem(es if scalar else 64), vvvv=v_n if scalar else 0,
                           ll=0 if scalar else 2, z=1 if z else 0, aaa=aaa, n=es if scalar else 64)
            else:
                enc = evex(1, w, opc, dst_n, Mem(es * 2), b=1, ll=2, z=1 if z else 0, aaa=aaa, n=es)
            km = None if k is None else (k & ((1 << n) - 1))
            res, mxo, fault = m_er(op, f, src, km, z, dest, mx, sae, scalar,
                                   src1 if scalar else None)
            ins = [zmm_in(dst_n, dest)]
            if scalar:
                ins.append(zmm_in(v_n, src1))
            if srcform == "reg":
                ins.append(zmm_in(src_n, src))
            elif srcform == "mem":
                ins.append("m+0x%X=%s" % (MEM_RSI + (es if scalar else 64),
                                          hexs(src[:es] if scalar else src)))
            else:
                ins.append("m+0x%X=%s" % (MEM_RSI + es * 2, hexs(src[:es])))
            if k is not None:
                ins.append("k1=0x%X" % k)
            if mx != MXCSR_DEFAULT:
                ins.append("mxcsr=0x%X" % mx)
            exp = []
            if res is not None and res != dest:
                exp.append(zmm_in(dst_n, res))
            if mxo != mx:
                exp.append("mxcsr=0x%X" % mxo)
            if fault:
                exp.append(fault)
            case(enc, ins, exp, "%s %s" % (name, t))
        # #UD encodings
        ll_ok = 0 if scalar else 2
        uds = [("EVEX.W%d" % (w ^ 1), evex(1, w ^ 1, opc, 0, 1, vvvv=2 if scalar else 0,
                                          ll=ll_ok) if False else None)]
        uds = [("EVEX.z without a mask", evex(1, w, opc, 0, 1, vvvv=2 if scalar else 0, ll=ll_ok,
                                               z=1)),
               ("F2 instead of 66", evex(3, w, opc, 0, 1, vvvv=2 if scalar else 0, ll=ll_ok)),
               ("EVEX.b with a Tuple1 Scalar memory operand" if scalar else "EVEX.L'L = 01b",
                evex(1, w, opc, 0, Mem(), vvvv=2, ll=0, b=1, n=es) if scalar else
                evex(1, w, opc, 0, 1, ll=1))]
        if not scalar:
            uds += [("EVEX.vvvv != 1111b", evex(1, w, opc, 0, 1, vvvv=6, ll=2)),
                    ("EVEX.L'L = 00b", evex(1, w, opc, 0, 1, ll=0)),
                    ("EVEX.L'L = 11b", evex(1, w, opc, 0, 1, ll=3))]
        for t, e in uds:
            case(e, [], ["#UD"], "%s %s: #UD" % (name, t))
        if scalar:
            # LLIG: L'L = 10b still the scalar form
            srcv = er_values(rnd, op, f, 64 // es, "random")
            src = join(srcv, es)
            dest = bytes(64)
            src1 = join([0x3FF0000000000000 >> (32 if f is F32 else 0)] * (64 // es), es)
            res, mxo, fault = m_er(op, f, src, None, False, dest, MXCSR_DEFAULT, False, True, src1)
            case(evex(1, w, opc, 3, 5, vvvv=9, ll=2), [zmm_in(9, src1), zmm_in(5, src)],
                 [zmm_in(3, res)] + (["mxcsr=0x%X" % mxo] if mxo != MXCSR_DEFAULT else []),
                 "%s EVEX.L'L = 10b: LLIG" % name)


GENS = {"4vnniw": gen_4vnniw, "4fmaps": gen_4fmaps, "pf": gen_pf, "er": gen_er}


def gen_cases(only):
    rnd = random.Random(0x5EED0990)
    emit("# Intel Xeon Phi-only families (ledger U990-U995): expected values from the independent SDM")
    emit("# model Emulator/tools/isa/ref_xeonphi.py --cases (regenerate, do not edit). The i5-13600K has")
    emit("# no AVX-512: expected-value cases only, run with the Xeon Phi bits enabled:")
    emit("#   emu-alltest --cases Emulator\\data\\cases_xeonphi.txt --xeonphi --expect-only")
    emit("# RSI = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).")
    emit("# Sources: SDM 325383-092 Vol2D chapter 8 (pages 8-2 .. 8-39), Vol2A 2.7 / 2.8, Vol1 4.9,")
    emit("# 11.5.2, 14.5.2; ISE 319433-062 Table 1-11 (Tuple1_4X, N = 16).")
    for fam in ("4vnniw", "4fmaps", "pf", "er"):
        if fam in only:
            GENS[fam](rnd)


def gen_offcases(only):
    """every form #UD without the Xeon Phi opt-in (run with --avx512 only)"""
    emit("# Intel Xeon Phi-only families (ledger U990-U995) WITHOUT their UC_CTL_X86_AVX512 bits: every")
    emit("# form #UD with the rest of AVX-512 enabled. Generated by ref_xeonphi.py --offcases; run:")
    emit("#   emu-alltest --cases Emulator\\data\\cases_xeonphi_off.txt --avx512 --expect-only")
    if "4vnniw" in only:
        for opc, name in ((0x52, "VP4DPWSSD"), (0x53, "VP4DPWSSDS")):
            case(evex(3, 0, opc, 0, Mem(), vvvv=4, n=16), [], ["#UD"], name + " without AVX512_4VNNIW")
    if "4fmaps" in only:
        for opc, sc, name in ((0x9A, 0, "V4FMADDPS"), (0xAA, 0, "V4FNMADDPS"), (0x9B, 1, "V4FMADDSS"),
                              (0xAB, 1, "V4FNMADDSS")):
            case(evex(3, 0, opc, 0, Mem(), vvvv=4, ll=0 if sc else 2, n=16), [], ["#UD"],
                 name + " without AVX512_4FMAPS")
    if "pf" in only:
        for name, opc, w, regf, dsz, isz in PF_FORMS:
            case(evex(1, w, opc, 0, Vsib(1), aaa=1, regfield=regf, n=dsz), ["k1=0xFF"], ["#UD"],
                 name + " without AVX512PF")
    if "er" in only:
        for name, opc, op, f, scalar in ER_FORMS:
            w = 1 if f is F64 else 0
            case(evex(1, w, opc, 0, 1, vvvv=2 if scalar else 0, ll=0 if scalar else 2), [],
                 ["#UD"], name + " without AVX512ER")
    emit("# PREFETCHWT1 (0F 0D /2) is a NOP hint with or without CPUID.(7,0):ECX[0]")
    case(bytes([0x0F, 0x0D, 0x16]), [], [], "PREFETCHWT1 byte ptr [rsi]")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--cases", action="store_true")
    ap.add_argument("--offcases", action="store_true")
    ap.add_argument("--only", default="4vnniw,4fmaps,pf,er")
    a = ap.parse_args()
    only = set(a.only.split(","))
    try:
        sys.stdout.reconfigure(newline="\r\n")
    except Exception:
        pass
    if a.selftest:
        return selftest()
    if a.cases:
        gen_cases(only)
    elif a.offcases:
        gen_offcases(only)
    else:
        ap.print_help()
        return 2
    sys.stdout.write("\n".join(OUT) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
