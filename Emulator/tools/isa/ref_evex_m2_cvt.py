#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m2_cvt.py -- independent reference model (Python 3 stdlib only) of the EVEX
milestone-M2 conversion / FP-special / shift instructions, and generator of the
expected-value case file Emulator/data/cases_evex_m2_cvt.txt.

Written from the Intel SDM text only (Vol2A 2.7 / Tables 2-36..2-43, the instruction pages'
Operation pseudocode, special-case tables and "SIMD Floating-Point Exceptions" lists;
Vol1 4.8/4.9/10.2/11.5), not from any C implementation. The generic EVEX machinery
(encoder, Case, exact-rational rounding) is the one of ref_evex_m1.py.

Instructions
  A  VCVTDQ2PS VCVTUDQ2PS VCVTPS2DQ VCVTPS2UDQ VCVTTPS2DQ VCVTTPS2UDQ VCVTPD2DQ VCVTPD2UDQ
     VCVTTPD2DQ VCVTTPD2UDQ VCVTDQ2PD VCVTUDQ2PD VCVTPS2PD VCVTPD2PS VCVTPH2PS VCVTPS2PH
     VCVTQQ2PS VCVTUQQ2PS VCVTQQ2PD VCVTUQQ2PD VCVTPS2QQ VCVTPS2UQQ VCVTPD2QQ VCVTPD2UQQ
     VCVTTPS2QQ VCVTTPS2UQQ VCVTTPD2QQ VCVTTPD2UQQ, VCVTSI2SS/SD VCVTUSI2SS/SD
     VCVT[T]SS2SI/SD2SI VCVT[T]SS2USI/SD2USI VCVTSS2SD VCVTSD2SS
  B  VRCP14PS/PD/SS/SD VRSQRT14PS/PD/SS/SD (documented stand-in, see rcp14 / rsqrt14)
  C  VGETEXP VGETMANT VSCALEF VFIXUPIMM VRNDSCALE (PS/PD/SS/SD)
  D  VPSLLD/Q VPSRLD/Q VPSRAD/Q by xmm3/m128 (Mem128), VPROLVD/Q VPRORVD/Q

Interpretation decisions (SDM section in brackets; also listed in the final report):
  * FP -> integer: NaN/Inf/out of range -> integer indefinite (signed 80..0, unsigned all
    ones) with IE and no PE; inexact -> PE; no DE (not in the exception lists); DAZ turns a
    denormal source into 0 (exact, no PE)   [CVTPS2DQ.. pages, Vol1 4.8.4/10.2.3.4]
  * unsigned conversions: the value is rounded first (MXCSR.RC / {er} / truncation), then
    range-checked: -0.7 -> RNE -1 -> invalid; -0.7 truncated -> 0 + PE
  * VCVTPS2PD/VCVTSS2SD: DE for a denormal source unless DAZ (then signed 0); SNaN -> IE
  * VCVTPD2PS/VCVTSD2SS: DAZ, DE, OE/UE/PE as an arithmetic result, FTZ honoured
  * VCVTPH2PS: DAZ ignored, no DE, SNaN -> IE (only Invalid listed)          [5-54]
  * VCVTPS2PH: rounding imm8[1:0] or MXCSR.RC (imm8[2]); imm8[7:3] ignored; FTZ ignored
    (tiny -> FP16 denormal); DAZ honoured ("Denormal (if MXCSR.DAZ=0)"); IE OE UE PE DE
  * NaN conversions keep the sign and the high payload bits, set the quiet bit
  * VGETEXP: |x|, -inf for 0 (and DAZ denormal), +inf for +-inf, DE for a denormal, IE SNaN
  * VGETMANT: pseudocode getmant_fp32/64 literally; flags only IE / DE; imm8[7:4] not used
    (the cases keep them 0); a negative denormal with imm8[3]=1 -> IE only (no DE)
  * VSCALEF: Table 5-37 literally (QNaN src1 with src2 = +Inf -> +INF, -Inf -> +0); DE only
    for a denormal src1 (DAZ=0) when no NaN is involved; src2 denormal without DAZ is used
    as is (floor(+denormal) = 0, floor(-denormal) = -1); DAZ zeroes both
  * VFIXUPIMM: DAZ turns a denormal SRC1 (vvvv) into a zero WITH ITS SIGN (Vol1 10.2.3.4;
    the pseudocode writes "0.0"), a -0 stays -0; MAX_FLOAT = the format's max normal; no DE;
    ZE/IE from imm8 only, never #XM; {sae} suppresses the flags; response 0010 QNaN(tsrc)
    is only generated for NaN tokens (undefined for numbers)
  * VRNDSCALE: imm8[2] -> MXCSR.RC; PE unless imm8[3]; DAZ zeroes a denormal (signed), PE
    then compares against the zeroed source (no PE); no DE; sign of zero kept
  * RCP14/RSQRT14: the coordinator's stand-in policy (see rcp14 / rsqrt14)
  * forms without masking ({k} not in the syntax: VCVTSI2S*, VCVT*2SI ...): aaa != 0 -> #UD
    (Table 2-42 row 1); EVEX.b on a register form of a form without {er}/{sae} -> #UD
    (Table 2-43 "other instruction classes")

Usage:
  python ref_evex_m2_cvt.py --selftest
  python ref_evex_m2_cvt.py --cases > Emulator/data/cases_evex_m2_cvt.txt
  python ref_evex_m2_cvt.py --hwgen HWFILE EXPECT.json   (writes HWFILE)
  python ref_evex_m2_cvt.py --hwcmp LOG EXPECT.json
"""

import json
import random
import re
import sys
from fractions import Fraction

MEM_RSI = 0x8000
RSI, RDI, R14 = 6, 7, 14
IE, DE, ZE, OE, UE, PE = 1, 2, 4, 8, 16, 32
MXCSR_DEFAULT = 0x1F80
DAZ, FTZ = 0x40, 0x8000
GPR_NAMES = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
             "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]


# ---------------------------------------------------------------------------------------
# EVEX encoding (SDM Vol2A 2.7.1) -- as ref_evex_m1.py
# ---------------------------------------------------------------------------------------
class Mem:
    def __init__(self, base=RSI, disp=0, index=None, scale=0, disp32=False):
        self.base, self.disp, self.index, self.scale, self.disp32 = base, disp, index, scale, disp32


def evex(mmm, pp, w, opc, reg, rm, vvvv=None, ll=0, b=0, z=0, aaa=0, imm=None, n=1,
         p0_or=0, p1_and=0xFF, p2_vp=None, prefixes=b""):
    r, rr = (reg >> 3) & 1, (reg >> 4) & 1
    x = bb = 0
    if isinstance(rm, Mem):
        m = rm
        need_sib = m.index is not None or (m.base & 7) == 4
        bb = (m.base >> 3) & 1
        if m.index is not None:
            x = (m.index >> 3) & 1
        disp = m.disp
        if not m.disp32 and disp % n == 0 and -128 <= disp // n <= 127 and not (disp == 0 and (m.base & 7) != 5):
            mod, dbytes = 1, bytes([(disp // n) & 0xFF])
        elif disp == 0 and (m.base & 7) != 5 and not m.disp32:
            mod, dbytes = 0, b""
        else:
            mod, dbytes = 2, (disp & 0xFFFFFFFF).to_bytes(4, "little")
        if need_sib:
            idx = 4 if m.index is None else m.index & 7
            sib = (m.scale << 6) | (idx << 3) | (m.base & 7)
            tail = bytes([(mod << 6) | ((reg & 7) << 3) | 4, sib]) + dbytes
        else:
            tail = bytes([(mod << 6) | ((reg & 7) << 3) | (m.base & 7)]) + dbytes
    else:
        bb, x = (rm >> 3) & 1, (rm >> 4) & 1
        tail = bytes([0xC0 | ((reg & 7) << 3) | (rm & 7)])
    v = 0 if vvvv is None else vvvv
    vp = (v >> 4) & 1
    p0 = ((r ^ 1) << 7) | ((x ^ 1) << 6) | ((bb ^ 1) << 5) | ((rr ^ 1) << 4) | mmm
    p0 |= p0_or
    if vvvv is None:
        p1 = (w << 7) | (0xF << 3) | 4 | pp
    else:
        p1 = (w << 7) | (((~v) & 0xF) << 3) | 4 | pp
    p1 &= p1_and
    vbit = (vp ^ 1) if p2_vp is None else p2_vp
    p2 = (z << 7) | (ll << 5) | (b << 4) | (vbit << 3) | aaa
    out = prefixes + bytes([0x62, p0, p1, p2, opc]) + tail
    if imm is not None:
        out += bytes([imm & 0xFF])
    return out


def byte_list(bs):
    return ".byte " + ", ".join("0x%02x" % c for c in bs)


def elems(buf, esz):
    return [int.from_bytes(buf[i * esz:(i + 1) * esz], "little") for i in range(len(buf) // esz)]


def pack(vals, esz):
    return b"".join((v & ((1 << (8 * esz)) - 1)).to_bytes(esz, "little") for v in vals)


def hexs(buf):
    return "".join("%02X" % c for c in buf)


# ---------------------------------------------------------------------------------------
# IEEE formats and exact rounding (as ref_evex_m1.py; F16 added)
# ---------------------------------------------------------------------------------------
class Fmt:
    def __init__(self, bits, p, ebits):
        self.bits, self.p, self.ebits = bits, p, ebits
        self.bias = (1 << (ebits - 1)) - 1
        self.emin = 1 - self.bias
        self.emax = self.bias
        self.fbits = p - 1
        self.sign = 1 << (bits - 1)
        self.inf = ((1 << ebits) - 1) << self.fbits
        self.maxn = self.inf - 1
        self.qnan_indef = self.sign | self.inf | (1 << (self.fbits - 1))


F16 = Fmt(16, 11, 5)
F32 = Fmt(32, 24, 8)
F64 = Fmt(64, 53, 11)
FMT = {"f16": F16, "f32": F32, "f64": F64}


def classify(x, f):
    sign = x >> (f.bits - 1)
    e = (x >> f.fbits) & ((1 << f.ebits) - 1)
    m = x & ((1 << f.fbits) - 1)
    if e == (1 << f.ebits) - 1:
        if m == 0:
            return "inf", sign, None
        return ("qnan" if m >> (f.fbits - 1) else "snan"), sign, None
    if e == 0:
        if m == 0:
            return "zero", sign, Fraction(0)
        v = Fraction(m, 1 << f.fbits) * Fraction(2) ** f.emin
        return "denorm", sign, -v if sign else v
    v = Fraction((1 << f.fbits) | m, 1 << f.fbits) * Fraction(2) ** (e - f.bias)
    return "normal", sign, -v if sign else v


def is_nan(x, f):
    return classify(x, f)[0] in ("snan", "qnan")


def quiet(x, f):
    return x | (1 << (f.fbits - 1))


def floor_log2(q):
    n, d = q.numerator, q.denominator
    e = n.bit_length() - d.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    if Fraction(2) ** (e + 1) <= q:
        e += 1
    return e


def round_frac(q, quantum, rc, negative):
    """round the magnitude q >= 0 to a multiple of quantum: (count, inexact). rc 0 RNE,
    1 RD, 2 RU, 3 RZ; negative = sign of the value being rounded"""
    t = q / quantum
    fl = t.numerator // t.denominator
    rem = t - fl
    if rem == 0:
        return fl, False
    if rc == 0:
        if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl & 1):
            fl += 1
    elif rc == 1:
        if negative:
            fl += 1
    elif rc == 2:
        if not negative:
            fl += 1
    return fl, True


def encode_value(v, f, rc, ftz):
    """round the exact non-zero rational v to format f: (bits, flags) (Vol1 4.9.1.5:
    tininess after rounding with unbounded exponent; FTZ: tiny -> signed 0, UE+PE)"""
    neg = v < 0
    q = -v if neg else v
    sign = f.sign if neg else 0
    flags = 0
    e = floor_log2(q)
    cnt_u, inexact_u = round_frac(q, Fraction(2) ** (e - f.fbits), rc, neg)
    ru = cnt_u * Fraction(2) ** (e - f.fbits)
    tiny = ru < Fraction(2) ** f.emin
    if tiny and ftz:
        return sign, UE | PE
    if e < f.emin:
        cnt, inexact = round_frac(q, Fraction(2) ** (f.emin - f.fbits), rc, neg)
        if tiny and inexact:
            flags |= UE | PE
        elif inexact:
            flags |= PE
        if cnt >= (1 << f.fbits):
            return sign | (1 << f.fbits) | (cnt - (1 << f.fbits)), flags
        return sign | cnt, flags
    cnt, inexact = round_frac(q, Fraction(2) ** (e - f.fbits), rc, neg)
    if cnt == (1 << (f.fbits + 1)):
        cnt >>= 1
        e += 1
    if inexact:
        flags |= PE
    if e > f.emax:
        flags |= OE | PE
        inf, maxf = sign | f.inf, sign | f.maxn
        if rc == 0:
            return inf, flags
        if rc == 3:
            return maxf, flags
        if rc == 1:
            return (inf if neg else maxf), flags
        return (maxf if neg else inf), flags
    return sign | ((e + f.bias) << f.fbits) | (cnt - (1 << f.fbits)), flags


def exact_bits(v, f):
    """bits of an exactly representable non-zero value (asserts exactness)"""
    r, fl = encode_value(v, f, 0, False)
    assert fl == 0, (v, fl)
    return r


def fval(x, f):
    """Fraction value of a finite non-NaN encoding"""
    return classify(x, f)[2]


# ---------------------------------------------------------------------------------------
# element models.  ctx: mxcsr (DAZ/FTZ/RC) and rc = embedded rounding (None: MXCSR.RC)
# every model returns (result bits, MXCSR flags of this element)
# ---------------------------------------------------------------------------------------
class Ctx:
    def __init__(self, mxcsr=MXCSR_DEFAULT, rc=None):
        self.mxcsr = mxcsr
        self.rc = ((mxcsr >> 13) & 3) if rc is None else rc
        self.daz = bool(mxcsr & DAZ)
        self.ftz = bool(mxcsr & FTZ)


def nan_convert(x, fs, fd):
    """NaN to another format: sign kept, payload's high bits kept, quiet bit set"""
    sign = x >> (fs.bits - 1)
    frac = x & ((1 << fs.fbits) - 1)
    if fd.fbits >= fs.fbits:
        frac <<= fd.fbits - fs.fbits
    else:
        frac >>= fs.fbits - fd.fbits
    frac |= 1 << (fd.fbits - 1)
    return (sign << (fd.bits - 1)) | fd.inf | frac


def cvt_f2f(x, fs, fd, ctx, rc=None, daz=None, ftz=None, report_de=True):
    """FP -> FP (CVTPS2PD/CVTPD2PS/CVTSS2SD/CVTSD2SS/VCVTPH2PS/VCVTPS2PH)"""
    rc = ctx.rc if rc is None else rc
    daz = ctx.daz if daz is None else daz
    ftz = ctx.ftz if ftz is None else ftz
    c, s, v = classify(x, fs)
    sb = fd.sign if s else 0
    if c in ("snan", "qnan"):
        return nan_convert(x, fs, fd), (IE if c == "snan" else 0)
    if c == "inf":
        return sb | fd.inf, 0
    flags = 0
    if c == "denorm":
        if daz:
            return sb, 0
        if report_de:
            flags |= DE
    if c == "zero":
        return sb, 0
    r, fl = encode_value(v, fd, rc, ftz)
    return r, flags | fl


def cvt_f2i(x, f, nbits, sgn, ctx, trunc=False):
    """FP -> integer (CVTPS2DQ family, signed or unsigned, nbits 32/64)"""
    rc = 3 if trunc else ctx.rc
    c, s, v = classify(x, f)
    indef = (1 << (nbits - 1)) if sgn else (1 << nbits) - 1
    if c in ("snan", "qnan", "inf"):
        return indef, IE
    if c == "zero" or (c == "denorm" and ctx.daz):
        return 0, 0
    q = -v if s else v
    cnt, inexact = round_frac(q, Fraction(1), rc, bool(s))
    val = -cnt if s else cnt
    lo, hi = ((-(1 << (nbits - 1)), (1 << (nbits - 1)) - 1) if sgn else (0, (1 << nbits) - 1))
    if val < lo or val > hi:
        return indef, IE
    return val & ((1 << nbits) - 1), (PE if inexact else 0)


def cvt_i2f(x, nbits, sgn, fd, ctx):
    """integer -> FP (CVTDQ2PS family): exact or rounded, PE only"""
    v = x & ((1 << nbits) - 1)
    if sgn and v >> (nbits - 1):
        v -= 1 << nbits
    if v == 0:
        return 0, 0
    return encode_value(Fraction(v), fd, ctx.rc, False)


def rcp_policy_round(v, f, ftz):
    """RCP14 stand-in rounding: RNE to p bits with an unbounded exponent; overflow -> inf;
    a tiny result is then denormalized (RNE at the denormal quantum) or, with FTZ, flushed
    to a signed zero. No flags."""
    neg = v < 0
    q = -v if neg else v
    sign = f.sign if neg else 0
    e = floor_log2(q)
    cnt, _ = round_frac(q, Fraction(2) ** (e - f.fbits), 0, neg)
    r = cnt * Fraction(2) ** (e - f.fbits)
    if cnt == 1 << (f.fbits + 1):
        e += 1
    if e > f.emax:
        return sign | f.inf
    if r < Fraction(2) ** f.emin:
        if ftz:
            return sign
        c2, _ = round_frac(r, Fraction(2) ** (f.emin - f.fbits), 0, neg)
        return sign | c2           # c2 == 2^fbits gives the smallest normal (same bits)
    return exact_bits(-r if neg else r, f)


def rcp14(x, f, ctx):
    """VRCP14: policy documented in the task (SDM Table 5-24/5-25 special cases); no flags"""
    c, s, v = classify(x, f)
    sb = f.sign if s else 0
    if c == "snan":
        return quiet(x, f), 0
    if c == "qnan":
        return x, 0
    if c == "inf":
        return sb, 0
    if c == "zero" or (c == "denorm" and ctx.daz):
        return sb | f.inf, 0
    return rcp_policy_round(1 / v, f, ctx.ftz), 0


def isqrt(n):
    if n < 2:
        return n
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def rsqrt14(x, f, ctx):
    """VRSQRT14 stand-in: 1/sqrt(x) correctly rounded (RNE); Table 5-32 special cases"""
    c, s, v = classify(x, f)
    if c == "snan":
        return quiet(x, f), 0
    if c == "qnan":
        return x, 0
    if c == "denorm" and ctx.daz:
        c = "zero"
    if c == "zero":
        return (f.sign if s else 0) | f.inf, 0
    if s:
        return f.qnan_indef, 0
    if c == "inf":
        return 0, 0
    q = 1 / v                                   # y = sqrt(q)
    e = floor_log2(q) // 2                      # 2^e <= y < 2^(e+1)
    k = f.fbits - e + 2                         # y * 2^k has fbits+3 integer bits
    num = q.numerator << (2 * k) if k >= 0 else q.numerator
    den = q.denominator if k >= 0 else q.denominator << (-2 * k)
    t = num // den
    S = isqrt(t)
    sticky = S * S * den != num
    cnt, rem = S >> 2, S & 3
    if rem > 2 or (rem == 2 and (sticky or cnt & 1)):
        cnt += 1
    if cnt == 1 << (f.fbits + 1):
        cnt >>= 1
        e += 1
    return ((e + f.bias) << f.fbits) | (cnt - (1 << f.fbits)), 0


def getexp(x, f, ctx):
    c, s, v = classify(x, f)
    if c == "snan":
        return quiet(x, f), IE
    if c == "qnan":
        return x, 0
    if c == "inf":
        return f.inf, 0
    if c == "zero" or (c == "denorm" and ctx.daz):
        return f.sign | f.inf, 0
    flags = DE if c == "denorm" else 0
    e = floor_log2(-v if s else v)
    return (0 if e == 0 else exact_bits(Fraction(e), f)), flags


def getmant(x, f, imm, ctx):
    """getmant_fp32/fp64 pseudocode (Vol2C VGETMANTPD/PS)"""
    sc, interv = (imm >> 2) & 3, imm & 3
    c, s, v = classify(x, f)
    one = exact_bits(Fraction(1), f)
    signed_one = one if sc & 1 else one | f.sign
    if c == "snan":
        return quiet(x, f), IE
    if c == "qnan":
        return x, 0
    zero = c == "zero" or (c == "denorm" and ctx.daz)
    if not s and (zero or c == "inf"):
        return one, 0
    if s:
        if zero:
            return signed_one, 0
        if c == "inf":
            return (f.qnan_indef, IE) if sc & 2 else (signed_one, 0)
        if sc & 2:
            return f.qnan_indef, IE
    flags = DE if c == "denorm" else 0
    q = -v if s else v
    e = floor_log2(q)
    m = q / Fraction(2) ** e                    # [1, 2)
    if interv == 0:
        k = 0
    elif interv == 1:
        k = -1 if e % 2 else 0
    elif interv == 2:
        k = -1
    else:
        k = -1 if m >= Fraction(3, 2) else 0
    r = m * Fraction(2) ** k
    neg = (not (sc & 1)) and s
    return exact_bits(-r if neg else r, f), flags


def scalef(a, b, f, ctx):
    """SCALE(SRC1, SRC2) with Table 5-37 / 5-38"""
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    if ca == "snan":
        return quiet(a, f), IE
    if ca == "qnan":
        if cb == "snan":
            return a, IE
        if cb == "inf":
            return (0 if sb else f.inf), 0       # Table 5-37: +0 / +INF (literal)
        return a, 0
    if cb in ("snan", "qnan"):
        return quiet(b, f), (IE if cb == "snan" else 0)
    flags = 0
    if ca == "denorm":
        if ctx.daz:
            ca, va = "zero", Fraction(0)
        else:
            flags |= DE
    if cb == "denorm" and ctx.daz:
        cb, vb = "zero", Fraction(0)
    asg = f.sign if sa else 0
    if ca == "inf":
        if cb == "inf" and sb:
            return f.qnan_indef, flags | IE
        return a, flags
    if ca == "zero":
        if cb == "inf" and not sb:
            return f.qnan_indef, flags | IE
        return asg, flags
    if cb == "inf":
        return (asg if sb else asg | f.inf), flags
    n = vb.numerator // vb.denominator          # floor
    lim = 2 * (f.emax + f.p) + 8
    n = max(-lim, min(lim, n))
    r, fl = encode_value(va * Fraction(2) ** n, f, ctx.rc, ctx.ftz)
    return r, flags | fl


FIX_CONST = {
    "f32": {9: 0xBF800000, 10: 0x3F800000, 11: 0x3F000000, 12: 0x42B40000, 13: 0x3FC90FDB,
            14: 0x7F7FFFFF, 15: 0xFF7FFFFF},
    "f64": {9: 0xBFF0000000000000, 10: 0x3FF0000000000000, 11: 0x3FE0000000000000,
            12: 0x4056800000000000, 13: 0x3FF921FB54442D18, 14: 0x7FEFFFFFFFFFFFFF,
            15: 0xFFEFFFFFFFFFFFFF},
}


def fix_token(x, f, ctx):
    c, s, v = classify(x, f)
    if c == "denorm" and ctx.daz:
        c, x = "zero", (f.sign if s else 0)
    if c == "qnan":
        return 0, x
    if c == "snan":
        return 1, x
    if c == "zero":
        return 2, x
    if c == "inf":
        return (4 if s else 5), x
    if not s and v == 1:
        return 3, x
    return (6 if s else 7), x


def fixupimm(d, a, tbl, f, imm, ctx):
    """FIXUPIMM_SP/DP: d = DEST element, a = SRC1 (vvvv), tbl = SRC2 element"""
    j, tsrc = fix_token(a, f, ctx)
    resp = (tbl >> (4 * j)) & 0xF
    key = "f64" if f is F64 else "f32"
    if resp == 0:
        r = d
    elif resp == 1:
        r = tsrc
    elif resp == 2:
        r = quiet(tsrc, f)                       # only generated for NaN tokens
    elif resp == 3:
        r = f.qnan_indef
    elif resp == 4:
        r = f.sign | f.inf
    elif resp == 5:
        r = f.inf
    elif resp == 6:
        r = (f.sign if tsrc >> (f.bits - 1) else 0) | f.inf
    elif resp == 7:
        r = f.sign
    elif resp == 8:
        r = 0
    else:
        r = FIX_CONST[key][resp]
    flags = 0
    if j == 2 and imm & 1:
        flags |= ZE
    if j == 2 and imm & 2:
        flags |= IE
    if j == 3 and imm & 4:
        flags |= ZE
    if j == 3 and imm & 8:
        flags |= IE
    if j == 1 and imm & 16:
        flags |= IE
    if j == 4 and imm & 32:
        flags |= IE
    if j == 6 and imm & 64:
        flags |= IE
    if j == 5 and imm & 128:
        flags |= IE
    return r, flags


def rndscale(x, f, imm, ctx):
    """RoundToIntegerSP/DP (VRNDSCALE); with imm8[7:4] = 0 also ROUNDPS/PD/SS/SD"""
    rc = ((ctx.mxcsr >> 13) & 3) if imm & 4 else imm & 3
    M = imm >> 4
    c, s, v = classify(x, f)
    sb = f.sign if s else 0
    if c == "snan":
        return quiet(x, f), IE
    if c in ("qnan", "inf", "zero"):
        return x, 0
    if c == "denorm" and ctx.daz:
        return sb, 0
    q = -v if s else v
    cnt, inexact = round_frac(q * Fraction(2) ** M, Fraction(1), rc, bool(s))
    flags = PE if (inexact and not imm & 8) else 0
    if cnt == 0:
        return sb, flags
    r = cnt / Fraction(2) ** M
    return exact_bits(-r if s else r, f), flags


def shift_elem(op, a, cnt, bits):
    m = (1 << bits) - 1
    if op == "sll":
        return (a << cnt) & m if cnt < bits else 0
    if op == "srl":
        return a >> cnt if cnt < bits else 0
    if op == "sra":
        sa = a - (1 << bits) if a >> (bits - 1) else a
        return (sa >> min(cnt, bits - 1)) & m
    c = cnt % bits
    if op == "rol":
        return ((a << c) | (a >> (bits - c))) & m if c else a
    if op == "ror":
        return ((a >> c) | (a << (bits - c))) & m if c else a
    raise ValueError(op)


# ---------------------------------------------------------------------------------------
# instruction forms
# ---------------------------------------------------------------------------------------
TSIZE = {"f16": 2, "f32": 4, "f64": 8, "i32": 4, "u32": 4, "i64": 8, "u64": 8}
PPN = ["NP", "66", "F3", "F2"]
MAPN = ["", "0F", "0F38", "0F3A"]


class Form:
    def __init__(self, name, mmm, pp, opc, w, kind, st, dt, rc=None, bcst=True, op=None,
                 imm=False, trunc=False, tup="full", flags=True, wrong_w=None):
        self.name, self.mmm, self.pp, self.opc, self.w = name, mmm, pp, opc, w
        self.kind, self.st, self.dt, self.rc, self.bcst = kind, st, dt, rc, bcst
        self.op, self.imm, self.trunc, self.tup, self.flags = op, imm, trunc, tup, flags
        self.wrong_w = wrong_w
        self.ssz, self.dsz = TSIZE[st], TSIZE[dt]

    def elem(self, ctx, a, b, d, imm):
        """one element: a = SRC1 (vvvv), b = r/m source, d = old DEST"""
        st, dt, op = self.st, self.dt, self.op
        if op is None:                               # conversion by types
            if st[0] in "iu" and dt[0] == "f":
                return cvt_i2f(b, 8 * self.ssz, st[0] == "i", FMT[dt], ctx)
            if st[0] == "f" and dt[0] in "iu":
                return cvt_f2i(b, FMT[st], 8 * self.dsz, dt[0] == "i", ctx, self.trunc)
            if st == "f16":                          # VCVTPH2PS: DAZ ignored, no DE
                return cvt_f2f(b, F16, FMT[dt], ctx, daz=False, report_de=False)
            if dt == "f16":                          # VCVTPS2PH: imm rounding, FTZ ignored
                rc = ((ctx.mxcsr >> 13) & 3) if imm & 4 else imm & 3
                return cvt_f2f(b, FMT[st], F16, ctx, rc=rc, ftz=False)
            return cvt_f2f(b, FMT[st], FMT[dt], ctx)
        f = FMT.get(st)
        if op == "rcp14":
            return rcp14(b, f, ctx)
        if op == "rsqrt14":
            return rsqrt14(b, f, ctx)
        if op == "getexp":
            return getexp(b, f, ctx)
        if op == "getmant":
            return getmant(b, f, imm, ctx)
        if op == "rndscale":
            return rndscale(b, f, imm, ctx)
        if op == "scalef":
            return scalef(a, b, f, ctx)
        if op == "fixupimm":
            return fixupimm(d, a, b, f, imm, ctx)
        if op in ("rol", "ror"):
            return shift_elem(op, a, b, 8 * self.ssz), 0
        raise ValueError(op)


def F(*a, **k):
    return Form(*a, **k)


# packed unary conversions: kind "vec1"
CVT_FORMS = [
    F("VCVTDQ2PS", 1, 0, 0x5B, 0, "vec1", "i32", "f32", rc="er"),
    F("VCVTUDQ2PS", 1, 3, 0x7A, 0, "vec1", "u32", "f32", rc="er"),
    F("VCVTPS2DQ", 1, 1, 0x5B, 0, "vec1", "f32", "i32", rc="er", wrong_w=1),
    F("VCVTPS2UDQ", 1, 0, 0x79, 0, "vec1", "f32", "u32", rc="er"),
    F("VCVTTPS2DQ", 1, 2, 0x5B, 0, "vec1", "f32", "i32", rc="sae", trunc=True, wrong_w=1),
    F("VCVTTPS2UDQ", 1, 0, 0x78, 0, "vec1", "f32", "u32", rc="sae", trunc=True),
    F("VCVTPD2DQ", 1, 3, 0xE6, 1, "vec1", "f64", "i32", rc="er", wrong_w=0),
    F("VCVTPD2UDQ", 1, 0, 0x79, 1, "vec1", "f64", "u32", rc="er"),
    F("VCVTTPD2DQ", 1, 1, 0xE6, 1, "vec1", "f64", "i32", rc="sae", trunc=True, wrong_w=0),
    F("VCVTTPD2UDQ", 1, 0, 0x78, 1, "vec1", "f64", "u32", rc="sae", trunc=True),
    F("VCVTDQ2PD", 1, 2, 0xE6, 0, "vec1", "i32", "f64", rc=None, tup="half", flags=False),
    F("VCVTUDQ2PD", 1, 2, 0x7A, 0, "vec1", "u32", "f64", rc=None, tup="half", flags=False),
    F("VCVTPS2PD", 1, 0, 0x5A, 0, "vec1", "f32", "f64", rc="sae", tup="half", wrong_w=1),
    F("VCVTPD2PS", 1, 1, 0x5A, 1, "vec1", "f64", "f32", rc="er", wrong_w=0),
    F("VCVTPH2PS", 2, 1, 0x13, 0, "vec1", "f16", "f32", rc="sae", bcst=False, tup="halfmem", wrong_w=1),
    F("VCVTQQ2PS", 1, 0, 0x5B, 1, "vec1", "i64", "f32", rc="er"),
    F("VCVTUQQ2PS", 1, 3, 0x7A, 1, "vec1", "u64", "f32", rc="er"),
    F("VCVTQQ2PD", 1, 2, 0xE6, 1, "vec1", "i64", "f64", rc="er"),
    F("VCVTUQQ2PD", 1, 2, 0x7A, 1, "vec1", "u64", "f64", rc="er"),
    F("VCVTPS2QQ", 1, 1, 0x7B, 0, "vec1", "f32", "i64", rc="er", tup="half"),
    F("VCVTPS2UQQ", 1, 1, 0x79, 0, "vec1", "f32", "u64", rc="er", tup="half"),
    F("VCVTPD2QQ", 1, 1, 0x7B, 1, "vec1", "f64", "i64", rc="er"),
    F("VCVTPD2UQQ", 1, 1, 0x79, 1, "vec1", "f64", "u64", rc="er"),
    F("VCVTTPS2QQ", 1, 1, 0x7A, 0, "vec1", "f32", "i64", rc="sae", trunc=True, tup="half"),
    F("VCVTTPS2UQQ", 1, 1, 0x78, 0, "vec1", "f32", "u64", rc="sae", trunc=True, tup="half"),
    F("VCVTTPD2QQ", 1, 1, 0x7A, 1, "vec1", "f64", "i64", rc="sae", trunc=True),
    F("VCVTTPD2UQQ", 1, 1, 0x78, 1, "vec1", "f64", "u64", rc="sae", trunc=True),
]

SPECIAL_FORMS = [
    F("VRCP14PS", 2, 1, 0x4C, 0, "vec1", "f32", "f32", op="rcp14", flags=False),
    F("VRCP14PD", 2, 1, 0x4C, 1, "vec1", "f64", "f64", op="rcp14", flags=False),
    F("VRSQRT14PS", 2, 1, 0x4E, 0, "vec1", "f32", "f32", op="rsqrt14", flags=False),
    F("VRSQRT14PD", 2, 1, 0x4E, 1, "vec1", "f64", "f64", op="rsqrt14", flags=False),
    F("VGETEXPPS", 2, 1, 0x42, 0, "vec1", "f32", "f32", op="getexp", rc="sae"),
    F("VGETEXPPD", 2, 1, 0x42, 1, "vec1", "f64", "f64", op="getexp", rc="sae"),
    F("VGETMANTPS", 3, 1, 0x26, 0, "vec1", "f32", "f32", op="getmant", rc="sae", imm=True),
    F("VGETMANTPD", 3, 1, 0x26, 1, "vec1", "f64", "f64", op="getmant", rc="sae", imm=True),
    F("VRNDSCALEPS", 3, 1, 0x08, 0, "vec1", "f32", "f32", op="rndscale", rc="sae", imm=True, wrong_w=1),
    F("VRNDSCALEPD", 3, 1, 0x09, 1, "vec1", "f64", "f64", op="rndscale", rc="sae", imm=True, wrong_w=0),
    F("VSCALEFPS", 2, 1, 0x2C, 0, "vec2", "f32", "f32", op="scalef", rc="er"),
    F("VSCALEFPD", 2, 1, 0x2C, 1, "vec2", "f64", "f64", op="scalef", rc="er"),
    F("VFIXUPIMMPS", 3, 1, 0x54, 0, "vecfix", "f32", "f32", op="fixupimm", rc="sae", imm=True),
    F("VFIXUPIMMPD", 3, 1, 0x54, 1, "vecfix", "f64", "f64", op="fixupimm", rc="sae", imm=True),
]

SHIFT_FORMS = [
    F("VPSLLD", 1, 1, 0xF2, 0, "shift", "u32", "u32", op="sll", bcst=False, tup="mem128", flags=False, wrong_w=1),
    F("VPSLLQ", 1, 1, 0xF3, 1, "shift", "u64", "u64", op="sll", bcst=False, tup="mem128", flags=False, wrong_w=0),
    F("VPSRLD", 1, 1, 0xD2, 0, "shift", "u32", "u32", op="srl", bcst=False, tup="mem128", flags=False, wrong_w=1),
    F("VPSRLQ", 1, 1, 0xD3, 1, "shift", "u64", "u64", op="srl", bcst=False, tup="mem128", flags=False, wrong_w=0),
    F("VPSRAD", 1, 1, 0xE2, 0, "shift", "u32", "u32", op="sra", bcst=False, tup="mem128", flags=False),
    F("VPSRAQ", 1, 1, 0xE2, 1, "shift", "u64", "u64", op="sra", bcst=False, tup="mem128", flags=False),
    F("VPROLVD", 2, 1, 0x15, 0, "vec2", "u32", "u32", op="rol", flags=False),
    F("VPROLVQ", 2, 1, 0x15, 1, "vec2", "u64", "u64", op="rol", flags=False),
    F("VPRORVD", 2, 1, 0x14, 0, "vec2", "u32", "u32", op="ror", flags=False),
    F("VPRORVQ", 2, 1, 0x14, 1, "vec2", "u64", "u64", op="ror", flags=False),
]

# scalar, masked, xmm1{k}{z}, xmm2 (vvvv: bits above element 0), xmm3/m (Tuple1 Scalar)
SCALAR_FORMS = [
    F("VCVTSS2SD", 1, 2, 0x5A, 0, "sca", "f32", "f64", rc="sae", tup="t1s", wrong_w=1),
    F("VCVTSD2SS", 1, 3, 0x5A, 1, "sca", "f64", "f32", rc="er", tup="t1s", wrong_w=0),
    F("VRCP14SS", 2, 1, 0x4D, 0, "sca", "f32", "f32", op="rcp14", tup="t1s", flags=False),
    F("VRCP14SD", 2, 1, 0x4D, 1, "sca", "f64", "f64", op="rcp14", tup="t1s", flags=False),
    F("VRSQRT14SS", 2, 1, 0x4F, 0, "sca", "f32", "f32", op="rsqrt14", tup="t1s", flags=False),
    F("VRSQRT14SD", 2, 1, 0x4F, 1, "sca", "f64", "f64", op="rsqrt14", tup="t1s", flags=False),
    F("VGETEXPSS", 2, 1, 0x43, 0, "sca", "f32", "f32", op="getexp", rc="sae", tup="t1s"),
    F("VGETEXPSD", 2, 1, 0x43, 1, "sca", "f64", "f64", op="getexp", rc="sae", tup="t1s"),
    F("VGETMANTSS", 3, 1, 0x27, 0, "sca", "f32", "f32", op="getmant", rc="sae", imm=True, tup="t1s"),
    F("VGETMANTSD", 3, 1, 0x27, 1, "sca", "f64", "f64", op="getmant", rc="sae", imm=True, tup="t1s"),
    F("VSCALEFSS", 2, 1, 0x2D, 0, "sca2", "f32", "f32", op="scalef", rc="er", tup="t1s"),
    F("VSCALEFSD", 2, 1, 0x2D, 1, "sca2", "f64", "f64", op="scalef", rc="er", tup="t1s"),
    F("VFIXUPIMMSS", 3, 1, 0x55, 0, "scafix", "f32", "f32", op="fixupimm", rc="sae", imm=True, tup="t1s"),
    F("VFIXUPIMMSD", 3, 1, 0x55, 1, "scafix", "f64", "f64", op="fixupimm", rc="sae", imm=True, tup="t1s"),
    F("VRNDSCALESS", 3, 1, 0x0A, 0, "sca", "f32", "f32", op="rndscale", rc="sae", imm=True, tup="t1s", wrong_w=1),
    F("VRNDSCALESD", 3, 1, 0x0B, 1, "sca", "f64", "f64", op="rndscale", rc="sae", imm=True, tup="t1s", wrong_w=0),
]

# integer -> scalar FP (no masking): xmm1, xmm2 (vvvv), r/m32/64
SI2F_FORMS = [
    F("VCVTSI2SS", 1, 2, 0x2A, 0, "si2f", "i32", "f32", rc="er", tup="t1s"),
    F("VCVTSI2SS", 1, 2, 0x2A, 1, "si2f", "i64", "f32", rc="er", tup="t1s"),
    F("VCVTSI2SD", 1, 3, 0x2A, 0, "si2f", "i32", "f64", rc=None, tup="t1s", flags=False),
    F("VCVTSI2SD", 1, 3, 0x2A, 1, "si2f", "i64", "f64", rc="er", tup="t1s"),
    F("VCVTUSI2SS", 1, 2, 0x7B, 0, "si2f", "u32", "f32", rc="er", tup="t1s"),
    F("VCVTUSI2SS", 1, 2, 0x7B, 1, "si2f", "u64", "f32", rc="er", tup="t1s"),
    F("VCVTUSI2SD", 1, 3, 0x7B, 0, "si2f", "u32", "f64", rc=None, tup="t1s", flags=False),
    F("VCVTUSI2SD", 1, 3, 0x7B, 1, "si2f", "u64", "f64", rc="er", tup="t1s"),
]

# scalar FP -> GPR (no masking): r32/r64, xmm1/m32/m64 (Tuple1 Fixed)
F2SI_FORMS = []
for _nm, _pp, _opc, _st, _sg, _tr in [
        ("VCVTSS2SI", 2, 0x2D, "f32", "i", False), ("VCVTSD2SI", 3, 0x2D, "f64", "i", False),
        ("VCVTTSS2SI", 2, 0x2C, "f32", "i", True), ("VCVTTSD2SI", 3, 0x2C, "f64", "i", True),
        ("VCVTSS2USI", 2, 0x79, "f32", "u", False), ("VCVTSD2USI", 3, 0x79, "f64", "u", False),
        ("VCVTTSS2USI", 2, 0x78, "f32", "u", True), ("VCVTTSD2USI", 3, 0x78, "f64", "u", True)]:
    for _w in (0, 1):
        F2SI_FORMS.append(F(_nm, 1, _pp, _opc, _w, "f2si", _st, "%s%d" % (_sg, 64 if _w else 32),
                            rc="sae" if _tr else "er", trunc=_tr, tup="t1f"))

PS2PH = F("VCVTPS2PH", 3, 1, 0x1D, 0, "ps2ph", "f32", "f16", rc="sae", bcst=False, tup="halfmem",
          imm=True, wrong_w=1)


# ---------------------------------------------------------------------------------------
# values
# ---------------------------------------------------------------------------------------
RNG = random.Random(0x5EED_C0A7)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


SPEC = {
    "f32": [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000, 0x7FC00000,
            0x7FA00000, 0xFFC00001, 0xFF900001, 0x00000001, 0x807FFFFF, 0x00800000, 0x7F7FFFFF,
            0xFF7FFFFF, 0x3F000000, 0xBF000000, 0x3FC00000, 0x40200000, 0xBFC00000, 0xC0200000,
            0xBF333333, 0x3F333333, 0x4F000000, 0xCF000000, 0x4EFFFFFF, 0xCF000001, 0x4F800000,
            0x4F7FFFFF, 0x5F000000, 0xDF000000, 0x5EFFFFFF, 0xDF000001, 0x5F800000, 0x5F7FFFFF,
            0x477FE000, 0x477FF000, 0x477FEFFF, 0x33800000, 0x33000000, 0x33000001, 0x38800000,
            0x387FC000, 0x4B800001, 0x3EAAAAAB, 0x00400000, 0x80000001, 0x7F000000, 0x40490FDB],
    "f64": [0x0000000000000000, 0x8000000000000000, 0x3FF0000000000000, 0xBFF0000000000000,
            0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000, 0x7FF4000000000000,
            0xFFF8000000000123, 0xFFF0000000000123, 0x0000000000000001, 0x800FFFFFFFFFFFFF,
            0x0010000000000000, 0x7FEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF, 0x3FE0000000000000,
            0xBFE0000000000000, 0x3FF8000000000000, 0x4004000000000000, 0xBFF8000000000000,
            0xC004000000000000, 0xBFE6666666666666, 0x3FE6666666666666, 0x41E0000000000000,
            0xC1E0000000000000, 0x41DFFFFFFFC00000, 0x41DFFFFFFFE00000, 0xC1E0000000100000,
            0xC1E0000000200000, 0x41EFFFFFFFE00000, 0x41EFFFFFFFF00000, 0x41F0000000000000,
            0x43E0000000000000, 0xC3E0000000000000, 0x43DFFFFFFFFFFFFF, 0xC3E0000000000001,
            0x43F0000000000000, 0x43EFFFFFFFFFFFFF, 0x47EFFFFFE0000000, 0x47EFFFFFF0000000,
            0x47EFFFFFEFFFFFFF, 0x36A0000000000000, 0x3690000000000000, 0x3690000000000001,
            0x380FFFFFF0000000, 0x380FFFFFE0000000, 0x3810000000000000, 0x3FD5555555555555,
            0x4340000000000001, 0x400921FB54442D18, 0x0008000000000000, 0x7FE0000000000000],
    "f16": [0x0000, 0x8000, 0x3C00, 0xBC00, 0x7C00, 0xFC00, 0x7E00, 0x7D00, 0xFE01, 0xFC01,
            0x0001, 0x83FF, 0x0400, 0x7BFF, 0xFBFF, 0x3555, 0x8001, 0x3800, 0x4248, 0x0200],
    "i32": [0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x01000001, 0x01000003, 0x7FFFFFC0,
            0x7FFFFF80, 0xFEFFFFFF, 0x00FFFFFF, 0x12345678, 0x80000001, 0x00000002],
    "i64": [0, 1, 0xFFFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0x0020000000000001,
            0x0020000000000003, 0x7FFFFFFFFFFFFC00, 0x7FFFFFFFFFFFFE00, 0xFFDFFFFFFFFFFFFF,
            0x0000000100000001, 0x0000000001000001, 0x8000000000000001, 0x00000000FFFFFFFF,
            0x7FFFFF8000000000, 0x7FFFFFC000000000],
}
SPEC["u32"] = SPEC["i32"]
SPEC["u64"] = SPEC["i64"]


def rnd_fp(f):
    sign = RNG.getrandbits(1)
    if f is F32:
        e = RNG.choice([RNG.randint(100, 154), RNG.randint(1, 254)])
        return (sign << 31) | (e << 23) | RNG.getrandbits(23)
    if f is F16:
        e = RNG.randint(1, 30)
        return (sign << 15) | (e << 10) | RNG.getrandbits(10)
    e = RNG.choice([RNG.randint(1000, 1046), RNG.randint(1, 2046)])
    return (sign << 63) | (e << 52) | RNG.getrandbits(52)


def rnd_conv_fp(f, lo=-3, hi=66):
    """finite value with a magnitude in [2^lo, 2^hi): useful for FP -> integer"""
    sign = RNG.getrandbits(1)
    k = RNG.randint(lo, hi)
    return (sign << (f.bits - 1)) | ((k + f.bias) << f.fbits) | RNG.getrandbits(f.fbits)


def rnd_val(t, form=None):
    if t in FMT:
        f = FMT[t]
        if form is not None and form.op is None and form.dt[0] in "iu":
            r = RNG.random()
            if r < 0.7:
                return rnd_conv_fp(f, -3, 8 * form.dsz + 1)
            if r < 0.85:
                return rnd_conv_fp(f, -3, 8 * form.dsz - 1) & ~f.sign
            return rnd_fp(f)
        if form is not None and form.dt == "f16":
            if RNG.random() < 0.7:
                return rnd_conv_fp(f, -26, 16)
            return rnd_fp(f)
        if form is not None and form.dt == "f32" and t == "f64":
            if RNG.random() < 0.6:
                return rnd_conv_fp(f, -152, 129)
            return rnd_fp(f)
        return rnd_fp(f)
    bits = 8 * TSIZE[t]
    return RNG.choice([RNG.getrandbits(bits), RNG.getrandbits(bits), RNG.getrandbits(RNG.randint(1, bits)),
                       (1 << bits) - RNG.getrandbits(8) - 1])


def spec_vals(t, n, shift=0):
    sp = SPEC[t]
    return [sp[(i * 7 + shift) % len(sp)] for i in range(n)]


def rnd_imm(form):
    if form.op == "getmant":
        return RNG.getrandbits(4)
    if form.op == "rndscale":
        return RNG.getrandbits(8)
    if form.op == "fixupimm":
        return RNG.getrandbits(8)
    if form.dt == "f16":
        return RNG.getrandbits(8)
    return 0


def rnd_fix_table(f):
    """random response table; QNaN(tsrc) (response 2) only for the NaN tokens 0/1"""
    t = 0
    for j in range(8):
        r = RNG.getrandbits(4)
        if j >= 2 and r == 2:
            r = 1
        t |= r << (4 * j)
    if f is F64:
        t |= RNG.getrandbits(32) << 32
    return t


# ---------------------------------------------------------------------------------------
# cases
# ---------------------------------------------------------------------------------------
class Case:
    def __init__(self, title):
        self.title = title
        self.code = b""
        self.inp = []
        self.exp = []
        self.fault = None
        self.zmm = {}
        self.k = {}
        self.mem = {}
        self.mxcsr = None

    def line(self):
        ins = []
        for r in sorted(self.zmm):
            ins.append("zmm%d=%s" % (r, hexs(self.zmm[r])))
        for r in sorted(self.k):
            ins.append("k%d=0x%X" % (r, self.k[r]))
        if self.mxcsr is not None:
            ins.append("mxcsr=0x%X" % self.mxcsr)
        for off in sorted(self.mem):
            ins.append("m+0x%X=%s" % (off, hexs(self.mem[off])))
        ins += self.inp
        exp = list(self.exp)
        if self.fault:
            exp.append(self.fault)
        return "%s | %s => %s" % (byte_list(self.code), " ".join(ins), " ".join(exp))


cases = []


def emit(c):
    cases.append(c)


def comment(text):
    cases.append("# " + text)


VL_LL = {16: 0, 32: 1, 64: 2}


def finish_flags(form, flags, mxcsr, erb):
    """(flags to set, fault?) from the OR of the active elements' flags"""
    if erb or not form.flags and form.op != "fixupimm":
        return (0 if erb else flags), False
    if form.op == "fixupimm":                     # MXCSR masks ignored: never #XM
        return flags, False
    unm = flags & ~(mxcsr >> 7) & 0x3F
    if unm & (IE | DE | ZE):
        flags &= ~(OE | UE | PE)
    return flags, bool(unm)


def gen_vec(form, vl, title, dst=1, s1=2, s2=3, kreg=0, kval=None, z=0, mem=None, bcst=0,
            bvals=None, avals=None, dvals=None, mxcsr=MXCSR_DEFAULT, rc=None, sae=False, ll=None,
            imm=None, disp=0x40):
    """packed forms: vec1 (dst, r/m), vec2 (dst, vvvv, r/m), vecfix (dst rw, vvvv, r/m),
    shift (dst, vvvv, xmm/m128 count). mem: True for a memory r/m at [rsi+disp]."""
    ssz, dsz = form.ssz, form.dsz
    kl = vl // max(ssz, dsz)
    if form.kind == "shift":
        kl = vl // ssz
    c = Case("%s VL%d %s" % (form.name, vl * 8, title))
    if imm is None and form.imm:
        imm = rnd_imm(form)
    if imm is None:
        imm = 0
    two = form.kind in ("vec2", "vecfix", "shift")
    regs = {}
    # sources
    if form.kind == "shift":
        if bvals is None:
            bvals = [RNG.choice([RNG.randint(0, 8 * ssz + 2), RNG.getrandbits(64), 8 * ssz,
                                 8 * ssz - 1, 0, 1 << 32, (1 << 32) | 1]), RNG.getrandbits(64)]
        cnt_img = pack(bvals[:1], 8) + pack(bvals[1:2], 8)
        count = bvals[0]
    else:
        nb = 1 if bcst else kl
        if bvals is None:
            if form.op == "fixupimm":
                bvals = [rnd_fix_table(FMT[form.st]) for _ in range(nb)]
            else:
                bvals = [rnd_val(form.st, form) for _ in range(nb)]
        bvals = list(bvals[:nb])
    if two:
        if avals is None:
            if form.op == "fixupimm":
                avals = [RNG.choice(SPEC[form.st] + [rnd_fp(FMT[form.st])]) for _ in range(kl)]
            else:
                avals = [rnd_val(form.st, form) for _ in range(kl)]
        if form.op == "scalef" and not (mem is None and s1 == s2):
            bb = bvals * kl if bcst else bvals
            one = exact_bits(Fraction(1), FMT[form.st])
            avals = [one if is_nan(x, FMT[form.st]) and is_nan(y, FMT[form.st]) else x
                     for x, y in zip(avals, bb)]
        regs[s1] = pack(avals, ssz) + rnd_bytes(64 - kl * ssz)
    if mem is None:
        if form.kind == "shift":
            img = cnt_img + rnd_bytes(48)
        else:
            img = pack(bvals, ssz) + rnd_bytes(64 - kl * ssz)
        if s2 in regs:
            img = regs[s2]
            if form.kind == "shift":
                count = elems(img[:8], 8)[0]
            else:
                bvals = elems(img[:kl * ssz], ssz)
        regs[s2] = img
    if dst not in regs:
        if dvals is not None:
            regs[dst] = pack(dvals, dsz) + rnd_bytes(64 - kl * dsz)
        else:
            regs[dst] = rnd_bytes(64)
    for r, im in regs.items():
        c.zmm[r] = im
    if two:
        avals = elems(regs[s1][:kl * ssz], ssz)
    old = regs[dst]
    dold = elems(old[:kl * dsz], dsz)
    if mem is not None:
        if form.kind == "shift":
            c.mem[MEM_RSI + disp] = cnt_img
            rm, nn = Mem(RSI, disp), 16
        else:
            c.mem[MEM_RSI + disp] = pack(bvals, ssz)
            rm, nn = Mem(RSI, disp), (ssz if bcst else kl * ssz)
        if bcst:
            bvals = bvals * kl
    else:
        rm, nn = s2, 1
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    kmask = kval if kreg else None
    erb = rc is not None or sae
    ctx = Ctx(mxcsr, rc)
    res, flags = [], 0
    for j in range(kl):
        if kmask is not None and not (kmask >> j) & 1:
            res.append(None)
            continue
        if form.kind == "shift":
            r, fl = shift_elem(form.op, avals[j], count, 8 * ssz), 0
        else:
            r, fl = form.elem(ctx, avals[j] if two else 0, bvals[j], dold[j], imm)
        res.append(r)
        flags |= fl
    flags, fault = finish_flags(form, flags, mxcsr, erb)
    if ll is None:
        ll = rc if rc is not None else VL_LL[vl]
    b = 1 if (bcst or erb) else 0
    c.code = evex(form.mmm, form.pp, form.w, form.opc, dst, rm, vvvv=s1 if two else None, ll=ll, b=b,
                  z=z, aaa=kreg, imm=imm if form.imm else None, n=nn)
    if fault:
        c.fault = "#XM"
    else:
        out = pack([(0 if z else dold[j]) if res[j] is None else res[j] for j in range(kl)], dsz)
        c.exp.append("zmm%d=%s" % (dst, hexs(out + bytes(64 - kl * dsz))))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    return c


def xm_trigger(form):
    """a source value raising an exception for #XM tests: (value, MXCSR with that exception
    unmasked) or None"""
    st, f = form.st, FMT.get(form.st)
    if form.op in ("rcp14", "rsqrt14", "fixupimm") or form.kind == "shift" or form.op in ("rol", "ror"):
        return None
    if form.op is None and st[0] in "iu":
        if form.dsz == 8 and form.ssz == 4:
            return None                                  # exact
        v = 0x01000001 if form.ssz == 4 else 0x0020000000000001
        if form.dt == "f32" and form.ssz == 8:
            v = 0x0000000001000001
        return v, MXCSR_DEFAULT & ~0x1000                # PM = 0
    snan = {"f16": 0x7D00, "f32": 0x7FA00000, "f64": 0x7FF4000000000000}[st]
    return snan, MXCSR_DEFAULT & ~0x80                   # IM = 0


def gen_vec_ud(form):
    """#UD encodings of a packed form"""
    if form.kind in ("vec1",):
        c = Case("%s vvvv != 1111b #UD" % form.name)
        c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, 3, vvvv=4, ll=2, imm=0 if form.imm else None)
        c.fault = "#UD"
        emit(c)
        c = Case("%s V' = 0 #UD" % form.name)
        c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, 3, ll=2, p2_vp=0, imm=0 if form.imm else None)
        c.fault = "#UD"
        emit(c)
    c = Case("%s L'L = 11b without EVEX.b #UD" % form.name)
    c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, 3, vvvv=2 if form.kind != "vec1" else None,
                  ll=3, imm=0 if form.imm else None)
    c.fault = "#UD"
    emit(c)
    if form.wrong_w is not None:
        c = Case("%s wrong W (W%d) #UD" % (form.name, form.wrong_w))
        c.code = evex(form.mmm, form.pp, form.wrong_w, form.opc, 1, 3, vvvv=2 if form.kind != "vec1" else None,
                      ll=2, imm=0 if form.imm else None)
        c.fault = "#UD"
        emit(c)
    if not form.bcst:
        c = Case("%s EVEX.b with a memory operand (%s tuple) #UD" % (form.name, form.tup))
        c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, Mem(RSI, 0), vvvv=2 if form.kind != "vec1" else None,
                      ll=2, b=1, imm=0 if form.imm else None)
        c.fault = "#UD"
        emit(c)
    c = Case("%s {z} with aaa=0 #UD" % form.name)
    c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, 3, vvvv=2 if form.kind != "vec1" else None,
                  ll=2, z=1, imm=0 if form.imm else None)
    c.fault = "#UD"
    emit(c)
    if form.rc is None and form.kind != "vec1":
        c = Case("%s EVEX.b on a register form (no {er}/{sae}) #UD" % form.name)
        c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, 3, vvvv=2, ll=2, b=1, imm=0 if form.imm else None)
        c.fault = "#UD"
        emit(c)


def gen_vec_form(form):
    comment("%s (EVEX.%s.%s.W%d %02X%s) %s -> %s, %s" % (
        form.name, PPN[form.pp], MAPN[form.mmm], form.w, form.opc, " ib" if form.imm else "",
        form.st, form.dt, form.rc or "no rc"))
    ssz, dsz = form.ssz, form.dsz
    for vl in (16, 32, 64):
        kl = vl // max(ssz, dsz) if form.kind != "shift" else vl // ssz
        emit(gen_vec(form, vl, "nomask", dst=1, s1=2, s2=3))
        emit(gen_vec(form, vl, "merge", dst=20, s1=22, s2=9, kreg=3, kval=RNG.getrandbits(64)))
        emit(gen_vec(form, vl, "zero", dst=4, s1=5, s2=26, kreg=6, kval=RNG.getrandbits(64), z=1))
        emit(gen_vec(form, vl, "mem", dst=6, s1=8, s2=0, mem=True))
        if form.bcst:
            emit(gen_vec(form, vl, "{1toN} merge", dst=7, s1=9, s2=0, mem=True, bcst=1, kreg=2,
                         kval=RNG.getrandbits(64)))
            emit(gen_vec(form, vl, "{1toN} k=0 (no lane)", dst=7, s1=9, s2=0, mem=True, bcst=1, kreg=2,
                         kval=0))
        if form.kind == "shift":
            continue
        # special values (every value of the list across the lanes)
        sp = SPEC[form.st]
        if form.op == "fixupimm":
            emit(gen_vec(form, vl, "specials", dst=11, s1=12, s2=13, avals=spec_vals(form.st, kl, vl),
                         bvals=[rnd_fix_table(FMT[form.st]) for _ in range(kl)]))
            continue
        for sh in range(0, len(sp), max(1, kl)):
            if form.kind == "vec2":
                av = spec_vals(form.st, kl, sh)
                bv = [sp[(i * 3 + sh + 1) % len(sp)] for i in range(kl)]
                emit(gen_vec(form, vl, "specials %d" % sh, dst=11, s1=12, s2=13, avals=av, bvals=bv))
            else:
                bv = [sp[(i + sh) % len(sp)] for i in range(kl)]
                emit(gen_vec(form, vl, "specials %d" % sh, dst=11, s1=12, s2=13, bvals=bv))
            if vl != 64:
                break
    n64 = 64 // max(ssz, dsz) if form.kind != "shift" else 64 // ssz
    if form.kind == "shift":
        bits = 8 * ssz
        for cnt in (0, 1, bits - 1, bits, bits + 1, 255, 1 << 32, (1 << 63) | 1, 0xFFFFFFFFFFFFFFFF):
            emit(gen_vec(form, 64, "count=%X" % cnt, dst=10, s1=11, s2=12, bvals=[cnt, RNG.getrandbits(64)]))
        emit(gen_vec(form, 64, "count only low 64 bits (high qword 1)", dst=10, s1=11, s2=12, bvals=[3, 1]))
        emit(gen_vec(form, 64, "dst=src1", dst=13, s1=13, s2=14, kreg=5, kval=RNG.getrandbits(16)))
        emit(gen_vec(form, 64, "dst=count", dst=14, s1=13, s2=14, kreg=5, kval=RNG.getrandbits(16), z=1))
        for d8 in (1, -1, 127, -127):
            emit(gen_vec(form, 32, "Mem128 disp8=%d N=16" % d8, dst=15, s1=16, s2=0, mem=True, disp=16 * d8))
        gen_vec_ud(form)
        return
    if form.kind == "vec2" and form.op in ("rol", "ror"):
        bits = 8 * ssz
        cnts = [0, 1, bits - 1, bits, bits + 1, (1 << bits) - 1, 1 << (bits - 1), 33]
        emit(gen_vec(form, 64, "counts", dst=10, s1=11, s2=12, bvals=(cnts * 2)[:n64]))
        emit(gen_vec(form, 64, "dst=src1", dst=13, s1=13, s2=14, kreg=5, kval=RNG.getrandbits(16)))
        emit(gen_vec(form, 64, "dst=src2", dst=14, s1=13, s2=14, kreg=5, kval=RNG.getrandbits(16), z=1))
        emit(gen_vec(form, 64, "src1=src2", dst=15, s1=16, s2=16))
        gen_vec_ud(form)
        return
    # MXCSR rounding / DAZ / FTZ with special and random values
    for mx in (0x3F80, 0x5F80, 0x7F80, 0x1FC0, 0x9F80, 0x9FC0):
        for sh in (0, 1):
            sp = SPEC[form.st]
            vals = [sp[(i * 5 + sh * 3 + 2) % len(sp)] if (i + sh) % 2 == 0 else rnd_val(form.st, form)
                    for i in range(n64)]
            kw = {}
            if form.op == "fixupimm":
                kw = dict(avals=vals, bvals=[rnd_fix_table(FMT[form.st]) for _ in range(n64)])
            elif form.kind == "vec2":
                kw = dict(avals=vals, bvals=[sp[(i * 11 + sh) % len(sp)] if i % 3 == 0 else rnd_val(form.st)
                                             for i in range(n64)])
            else:
                kw = dict(bvals=vals)
            emit(gen_vec(form, 64, "mxcsr=%X" % mx, dst=14, s1=15, s2=16, mxcsr=mx, **kw))
    # {er} / {sae} (reg-reg, VL 512): flags suppressed, no #XM with MXCSR all unmasked
    trig = xm_trigger(form)
    vals = [rnd_val(form.st, form) for _ in range(n64)]
    if trig:
        vals[1] = trig[0]
    if form.kind == "vecfix":
        avs = spec_vals(form.st, n64, 3)
        tb = [rnd_fix_table(FMT[form.st]) for _ in range(n64)]
    if form.rc == "er":
        for rc in range(4):
            kw = dict(bvals=vals)
            if form.kind == "vec2":
                kw = dict(avals=vals, bvals=[rnd_val(form.st) for _ in range(n64)])
            emit(gen_vec(form, 64, "{er} rc=%d MXCSR unmasked" % rc, dst=31, s1=2, s2=3, rc=rc, mxcsr=0x0000, **kw))
            emit(gen_vec(form, 64, "{er} rc=%d MXCSR RD, zero-masked" % rc, dst=31, s1=2, s2=3, rc=rc,
                         mxcsr=0x3F80 | FTZ, kreg=4, kval=RNG.getrandbits(16), z=1, **kw))
    elif form.rc == "sae":
        for ll in (0, 1, 2, 3):
            kw = dict(bvals=vals)
            if form.kind == "vecfix":
                kw = dict(avals=avs, bvals=tb, imm=0xFF)
            emit(gen_vec(form, 64, "{sae} L'L=%d (ignored, VL 512) MXCSR unmasked" % ll, dst=31, s1=2, s2=3,
                         sae=True, mxcsr=0x0000, ll=ll, **kw))
    else:
        c = Case("%s EVEX.b on a register form (no {er}/{sae}) #UD" % form.name)
        c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, 3, vvvv=2 if form.kind != "vec1" else None,
                      ll=2, b=1, imm=0 if form.imm else None)
        c.fault = "#UD"
        emit(c)
    # #XM / masked-off lanes
    if trig:
        v, mx = trig
        vals = [rnd_val(form.st, form) for _ in range(n64)]
        vals = [x if not (form.op is None and form.st[0] == "f" and is_nan(x, FMT[form.st])) else 0 for x in vals]
        bv = list(vals)
        kv = 0
        for j in range(n64):
            if j % 3 == 0:
                kv |= 1 << j
            else:
                bv[j] = v
        if form.op is None and form.st[0] in "iu":
            bv = [x if (kv >> j) & 1 == 0 else 0 for j, x in enumerate(bv)]     # exact active lanes
        kw = dict(bvals=bv) if form.kind == "vec1" else dict(avals=bv, bvals=[0] * n64)
        emit(gen_vec(form, 64, "trigger only in masked-off lanes, mxcsr=%X" % mx, dst=21, s1=22, s2=23,
                     kreg=5, kval=kv, mxcsr=mx, **kw))
        emit(gen_vec(form, 64, "trigger in an active lane, mxcsr=%X -> #XM" % mx, dst=21, s1=22, s2=23,
                     kreg=5, kval=kv | 2, mxcsr=mx, **kw))
        if form.kind == "vec1":
            emit(gen_vec(form, 64, "trigger, zeroing, all unmasked mxcsr=0 -> #XM", dst=21, s2=23,
                         kreg=5, kval=0xFFFF, z=1, mxcsr=0, bvals=bv))
    if form.op in ("rcp14", "rsqrt14"):
        for mx in (0x0000, 0x0040, 0x8000):
            emit(gen_vec(form, 64, "specials, MXCSR all unmasked %X (no flags, no #XM)" % mx, dst=24, s2=26,
                         bvals=spec_vals(form.st, n64, mx >> 6), mxcsr=mx))
    if form.flags and form.op in (None, "rndscale", "scalef") and not (form.st[0] in "iu" and form.dsz > form.ssz):
        # PM = 0: an inexact element faults
        vals = [rnd_val(form.st, form) for _ in range(n64)]
        if form.kind == "vec2":
            emit(gen_vec(form, 64, "random, PM=0", dst=24, s1=25, s2=26, avals=vals, mxcsr=0x0F80))
        else:
            emit(gen_vec(form, 64, "random, PM=0", dst=24, s2=26, bvals=vals, mxcsr=0x0F80,
                         imm=0x04 if form.imm else None))
    if form.op == "fixupimm":
        # table nibble 2 (zero token) = 1, not 2: QNaN(tsrc) of a number is undefined (see fixupimm)
        for imm in (0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF):
            emit(gen_vec(form, 64, "imm=%02X all tokens, MXCSR all unmasked (no #XM)" % imm, dst=24, s1=25,
                         s2=26, avals=spec_vals(form.st, n64, imm), bvals=[0x76543110 if j % 2 else 0xFEDCBA98
                                                                            for j in range(n64)],
                         imm=imm, mxcsr=0x0000))
        for mx in (0x1FC0, 0x9FC0):
            av = [0x00000001, 0x80000001, 0x00000000, 0x80000000] if form.st == "f32" else \
                [0x0000000000000001, 0x8000000000000001, 0x0000000000000000, 0x8000000000000000]
            av = (av * 4)[:n64]
            emit(gen_vec(form, 64, "DAZ denormal / zero passthrough and sign response", dst=24, s1=25, s2=26,
                         avals=av, bvals=[0x00000600 if j % 2 else 0x00000100 for j in range(n64)], imm=0x03,
                         mxcsr=mx))
        emit(gen_vec(form, 64, "response 0000 keeps DEST, merge", dst=27, s1=28, s2=29, kreg=1,
                     kval=0x5A5A, bvals=[0] * n64))
    if form.op == "getmant":
        for imm in range(16):
            emit(gen_vec(form, 64, "imm=%X specials" % imm, dst=24, s2=26, imm=imm,
                         bvals=spec_vals(form.st, n64, imm)))
    if form.op == "rndscale":
        for imm in list(range(16)) + [0x10, 0x23, 0x38, 0x4C, 0x81, 0xF0, 0xF4, 0xFB, 0x5E]:
            vals = [rnd_conv_fp(FMT[form.st], -20, 30) if j % 2 else SPEC[form.st][(j * 3 + imm) % len(SPEC[form.st])]
                    for j in range(n64)]
            emit(gen_vec(form, 64, "imm=%02X" % imm, dst=24, s2=26, imm=imm, bvals=vals,
                         mxcsr=[0x1F80, 0x3F80, 0x5F80, 0x7F80][imm % 4]))
        emit(gen_vec(form, 64, "imm=0x0C with PM=0 (precision suppressed)", dst=24, s2=26, imm=0x0C,
                     bvals=[rnd_conv_fp(FMT[form.st], -3, 10) for _ in range(n64)], mxcsr=0x0F80))
    # destination = source
    if form.kind == "vec1":
        emit(gen_vec(form, 64, "dst=src", dst=27, s2=27, kreg=6, kval=0x5555 if kl >= 16 else 0x55))
    elif form.kind == "vec2":
        emit(gen_vec(form, 64, "dst=src1", dst=27, s1=27, s2=28, kreg=6, kval=0x5555))
        emit(gen_vec(form, 64, "dst=src2", dst=28, s1=27, s2=28, kreg=6, kval=0x3333, z=1))
    else:
        emit(gen_vec(form, 64, "dst=src1 (fixup DEST = SRC1)", dst=27, s1=27, s2=28, kreg=6, kval=0x5555))
    gen_vec_ud(form)


def gen_disp8_vec():
    comment("--- disp8*N for the Half / Half Mem / Full forms (N = memory operand size; bcst: element)")
    for form in (CVT_FORMS[10], CVT_FORMS[12], CVT_FORMS[14], CVT_FORMS[13], CVT_FORMS[19]):
        for vl in (16, 32, 64):
            kl = vl // max(form.ssz, form.dsz)
            n = kl * form.ssz
            for d8 in (1, -1, 127, -127):
                emit(gen_vec(form, vl, "disp8=%d N=%d" % (d8, n), dst=17, s2=0, mem=True, disp=d8 * n))
            if form.bcst:
                for d8 in (1, -127):
                    emit(gen_vec(form, vl, "{1toN} disp8=%d N=%d" % (d8, form.ssz), dst=17, s2=0, mem=True,
                                 bcst=1, disp=d8 * form.ssz))


# -- scalar forms with masking -------------------------------------------------------------
def gen_sca(form, title, dst=1, s1=2, s2=3, kreg=0, kval=None, z=0, mem=False, bval=None, aval=None,
            dval=None, mxcsr=MXCSR_DEFAULT, rc=None, sae=False, ll=None, imm=None, disp=0x20):
    ssz, dsz = form.ssz, form.dsz
    c = Case("%s %s" % (form.name, title))
    if imm is None and form.imm:
        imm = rnd_imm(form)
    if imm is None:
        imm = 0
    f = FMT[form.st]
    if bval is None:
        bval = rnd_fix_table(f) if form.op == "fixupimm" else rnd_val(form.st, form)
    if aval is None:
        aval = RNG.choice(SPEC[form.st] + [rnd_fp(f)] * 4)
    if form.op == "scalef" and is_nan(aval, f) and is_nan(bval, f) and not (not mem and s1 == s2):
        aval = exact_bits(Fraction(1), f)
    regs = {}
    regs[s1] = pack([aval], ssz) + rnd_bytes(64 - ssz)
    if not mem:
        if s2 in regs:
            bval = elems(regs[s2][:ssz], ssz)[0]
        else:
            regs[s2] = pack([bval], ssz) + rnd_bytes(64 - ssz)
    if dst not in regs:
        regs[dst] = (pack([dval], dsz) if dval is not None else rnd_bytes(dsz)) + rnd_bytes(64 - dsz)
    for r, im in regs.items():
        c.zmm[r] = im
    aval = elems(regs[s1][:ssz], ssz)[0]
    src1 = regs[s1]
    dold = elems(regs[dst][:dsz], dsz)[0]
    if mem:
        c.mem[MEM_RSI + disp] = pack([bval], ssz)
        rm, nn = Mem(RSI, disp), ssz
    else:
        rm, nn = s2, 1
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    erb = rc is not None or sae
    ctx = Ctx(mxcsr, rc)
    flags = 0
    active = kreg == 0 or (kval & 1)
    if active:
        r, flags = form.elem(ctx, aval, bval, dold, imm)
    flags, fault = finish_flags(form, flags, mxcsr, erb)
    if ll is None:
        ll = rc if rc is not None else RNG.choice([0, 1, 2])
    c.code = evex(form.mmm, form.pp, form.w, form.opc, dst, rm, vvvv=s1, ll=ll, b=1 if erb else 0, z=z,
                  aaa=kreg, imm=imm if form.imm else None, n=nn)
    if fault:
        c.fault = "#XM"
    else:
        e0 = r if active else (0 if z else dold)
        out = pack([e0], dsz) + src1[dsz:16] + bytes(48)
        c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    return c


def gen_sca_form(form):
    comment("%s (EVEX.LLIG.%s.%s.W%d %02X%s) scalar %s -> %s, %s" % (
        form.name, PPN[form.pp], MAPN[form.mmm], form.w, form.opc, " ib" if form.imm else "", form.st,
        form.dt, form.rc or "no rc"))
    f = FMT[form.st]
    sp = SPEC[form.st]
    emit(gen_sca(form, "nomask", dst=1, s1=2, s2=3))
    emit(gen_sca(form, "merge k=1", dst=17, s1=18, s2=19, kreg=1, kval=0xFFFFFFFE | 1))
    emit(gen_sca(form, "merge k=0 (element 0 kept, upper from SRC1)", dst=17, s1=18, s2=19, kreg=1,
                 kval=0xFFFFFFFE))
    emit(gen_sca(form, "zero k=0", dst=4, s1=5, s2=6, kreg=2, kval=0xFE, z=1))
    emit(gen_sca(form, "zero k=1", dst=4, s1=5, s2=6, kreg=2, kval=0x01, z=1))
    emit(gen_sca(form, "mem", dst=7, s1=8, mem=True))
    for d8 in (1, -1, 127, -127):
        emit(gen_sca(form, "mem disp8=%d N=%d" % (d8, form.ssz), dst=7, s1=8, mem=True, disp=d8 * form.ssz))
    emit(gen_sca(form, "dst=src1", dst=9, s1=9, s2=10))
    emit(gen_sca(form, "dst=src2", dst=10, s1=9, s2=10, kreg=3, kval=1))
    emit(gen_sca(form, "src1=src2", dst=11, s1=12, s2=12))
    for i, v in enumerate(sp):
        if form.op == "fixupimm":
            emit(gen_sca(form, "special SRC1 %08X" % v, dst=13, s1=14, s2=15, aval=v,
                         bval=rnd_fix_table(f), imm=RNG.getrandbits(8)))
        elif form.kind == "sca2":
            emit(gen_sca(form, "special SRC1 %X" % v, dst=13, s1=14, s2=15, aval=v,
                         bval=sp[(i * 7 + 3) % len(sp)]))
            emit(gen_sca(form, "special SRC2 %X" % v, dst=13, s1=14, s2=15, bval=v,
                         aval=RNG.choice([rnd_fp(f), sp[(i * 5 + 1) % len(sp)]])))
        else:
            emit(gen_sca(form, "special %X" % v, dst=13, s1=14, s2=15, bval=v))
    for mx in (0x3F80, 0x5F80, 0x7F80, 0x1FC0, 0x9F80, 0x9FC0):
        for t in range(3):
            bv = sp[(t * 13 + mx // 0x2000) % len(sp)] if t else rnd_val(form.st, form)
            if form.op == "fixupimm":
                emit(gen_sca(form, "mxcsr=%X" % mx, dst=16, s1=20, s2=21, mxcsr=mx, aval=bv))
            else:
                emit(gen_sca(form, "mxcsr=%X" % mx, dst=16, s1=20, s2=21, mxcsr=mx, bval=bv))
    # denormal / tiny examples
    tinies = [1, f.sign | 1, (1 << f.fbits) - 1]
    for v in tinies:
        for mx in (0x1F80, 0x1FC0, 0x9F80):
            kw = dict(aval=v) if form.op in ("fixupimm",) else dict(bval=v)
            emit(gen_sca(form, "denormal %X mxcsr=%X" % (v, mx), dst=16, s1=20, s2=21, mxcsr=mx, **kw))
    trig = xm_trigger(form)
    if form.rc == "er":
        for rc in range(4):
            bv = RNG.choice(sp) if rc % 2 else rnd_val(form.st, form)
            emit(gen_sca(form, "{er} rc=%d MXCSR unmasked" % rc, dst=22, s1=23, s2=24, rc=rc, mxcsr=0, bval=bv))
            if trig:
                emit(gen_sca(form, "{er} rc=%d trigger, no #XM" % rc, dst=22, s1=23, s2=24, rc=rc, mxcsr=0,
                             bval=trig[0]))
    elif form.rc == "sae":
        for ll in (0, 3):
            bv = trig[0] if trig else RNG.choice(sp)
            kw = dict(bval=bv)
            if form.op == "fixupimm":
                kw = dict(aval=0, imm=0xFF)
            emit(gen_sca(form, "{sae} L'L=%d MXCSR unmasked" % ll, dst=22, s1=23, s2=24, sae=True, mxcsr=0,
                         ll=ll, **kw))
    if trig:
        emit(gen_sca(form, "trigger, mask bit 0 = 0 (no #XM)", dst=25, s1=26, s2=27, kreg=7, kval=0xFE,
                     mxcsr=trig[1], bval=trig[0]))
        emit(gen_sca(form, "trigger, active -> #XM", dst=25, s1=26, s2=27, kreg=7, kval=0xFF,
                     mxcsr=trig[1], bval=trig[0]))
    if form.op in ("rcp14", "rsqrt14"):
        for v in (FMT[form.st].inf | 1, FMT[form.st].sign | 1, FMT[form.st].sign):
            emit(gen_sca(form, "%X with MXCSR all unmasked: no flags, no #XM" % v, dst=25, s1=26, s2=27, mxcsr=0,
                         bval=v))
    if form.op == "fixupimm":
        emit(gen_sca(form, "IE from imm8, MXCSR all unmasked: no #XM", dst=25, s1=26, s2=27, mxcsr=0,
                     aval=0, bval=0x88888888, imm=0x03))
    if form.op == "getmant":
        for imm in range(16):
            emit(gen_sca(form, "imm=%X" % imm, dst=28, s1=29, s2=30, imm=imm, bval=RNG.choice(sp + [rnd_fp(f)] * 4)))
    if form.op == "rndscale":
        for imm in (0x00, 0x01, 0x02, 0x03, 0x04, 0x08, 0x0B, 0x10, 0x31, 0x4A, 0xF3, 0xFC):
            emit(gen_sca(form, "imm=%02X" % imm, dst=28, s1=29, s2=30, imm=imm, bval=rnd_conv_fp(f, -12, 24),
                         mxcsr=0x5F80))
    # #UD: EVEX.b with memory (Tuple1 Scalar), wrong W
    c = Case("%s EVEX.b with a memory operand #UD" % form.name)
    c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, Mem(RSI, 0), vvvv=2, ll=0, b=1,
                  imm=0 if form.imm else None)
    c.fault = "#UD"
    emit(c)
    if form.rc is None:
        c = Case("%s EVEX.b on the register form (no {er}/{sae}) #UD" % form.name)
        c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, 3, vvvv=2, ll=0, b=1, imm=0 if form.imm else None)
        c.fault = "#UD"
        emit(c)
    if form.wrong_w is not None:
        c = Case("%s wrong W #UD" % form.name)
        c.code = evex(form.mmm, form.pp, form.wrong_w, form.opc, 1, 3, vvvv=2, ll=0, imm=0 if form.imm else None)
        c.fault = "#UD"
        emit(c)


# -- integer -> scalar FP (no masking) -------------------------------------------------------
def gen_si2f(form, title, dst=1, s1=2, gpr=3, mem=False, val=None, mxcsr=MXCSR_DEFAULT, rc=None, disp=0x10,
             ll=None):
    c = Case("%s %s" % (form.name, title))
    nb = 8 * form.ssz
    if val is None:
        val = rnd_val(form.st)
    full = val | (RNG.getrandbits(64 - nb) << nb if nb == 32 else 0)
    c.zmm[s1] = rnd_bytes(64)
    if dst != s1:
        c.zmm[dst] = rnd_bytes(64)
    if mem:
        c.mem[MEM_RSI + disp] = pack([val], form.ssz)
        rm, nn = Mem(RSI, disp), form.ssz
    else:
        c.inp.append("%s=0x%X" % (GPR_NAMES[gpr], full))
        rm, nn = gpr, 1
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    ctx = Ctx(mxcsr, rc)
    r, flags = form.elem(ctx, 0, val, 0, 0)
    flags, fault = finish_flags(form, flags, mxcsr, rc is not None)
    if ll is None:
        ll = rc if rc is not None else RNG.choice([0, 1, 2])
    c.code = evex(form.mmm, form.pp, form.w, form.opc, dst, rm, vvvv=s1, ll=ll, b=1 if rc is not None else 0, n=nn)
    if fault:
        c.fault = "#XM"
    else:
        out = pack([r], form.dsz) + c.zmm[s1][form.dsz:16] + bytes(48)
        c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    return c


def gen_si2f_form(form):
    comment("%s (EVEX.LLIG.%s.0F.W%d %02X) %s -> %s, %s" % (form.name, PPN[form.pp], form.w, form.opc,
                                                          form.st, form.dt, form.rc or "no rc"))
    for i, v in enumerate(SPEC[form.st]):
        emit(gen_si2f(form, "special %X" % v, dst=1 + i % 3, s1=4, gpr=[0, 1, 9, 15][i % 4], val=v))
    for t in range(4):
        emit(gen_si2f(form, "random", dst=17, s1=18, gpr=10))
    emit(gen_si2f(form, "mem", dst=5, s1=6, mem=True))
    for d8 in (1, -1, 127, -127):
        emit(gen_si2f(form, "mem disp8=%d N=%d" % (d8, form.ssz), dst=5, s1=6, mem=True, disp=d8 * form.ssz))
    emit(gen_si2f(form, "dst=src1", dst=7, s1=7, gpr=2))
    hard = SPEC[form.st][5:9]
    for mx in (0x3F80, 0x5F80, 0x7F80):
        for v in hard:
            emit(gen_si2f(form, "mxcsr=%X" % mx, dst=8, s1=9, gpr=3, val=v, mxcsr=mx))
    trig = xm_trigger(form)
    if form.rc == "er":
        for rc in range(4):
            for v in hard[:2]:
                emit(gen_si2f(form, "{er} rc=%d, PM=0 no #XM" % rc, dst=10, s1=11, gpr=8, val=v, rc=rc, mxcsr=0x0F80))
    if trig and form.flags:
        emit(gen_si2f(form, "inexact, PM=0 -> #XM", dst=12, s1=13, gpr=1, val=trig[0], mxcsr=trig[1]))
        emit(gen_si2f(form, "exact, PM=0", dst=12, s1=13, gpr=1, val=5, mxcsr=trig[1]))
    # #UD
    for title, kw in (("aaa != 0 (no masking in the syntax)", dict(aaa=1)), ("{z}", dict(z=1, aaa=1)),
                      ("EVEX.b with memory", dict(rm=Mem(RSI, 0), b=1))):
        c = Case("%s %s #UD" % (form.name, title))
        rm = kw.pop("rm", 3)
        c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, rm, vvvv=2, ll=0, **kw)
        c.fault = "#UD"
        emit(c)
    if form.rc is None:
        c = Case("%s EVEX.b on the register form (no {er}) #UD" % form.name)
        c.code = evex(form.mmm, form.pp, form.w, form.opc, 1, 3, vvvv=2, ll=0, b=1)
        c.fault = "#UD"
        emit(c)


# -- scalar FP -> GPR -------------------------------------------------------------------------
def gen_f2si(form, title, gdst=0, src=1, mem=False, val=None, mxcsr=MXCSR_DEFAULT, rc=None, sae=False,
             disp=0x18, ll=None):
    c = Case("%s r%d %s" % (form.name, 8 * form.dsz, title))
    if val is None:
        val = rnd_val(form.st, form)
    old = RNG.getrandbits(64)
    c.inp.append("%s=0x%X" % (GPR_NAMES[gdst], old))
    if mem:
        c.mem[MEM_RSI + disp] = pack([val], form.ssz)
        rm, nn = Mem(RSI, disp), form.ssz
    else:
        c.zmm[src] = pack([val], form.ssz) + rnd_bytes(64 - form.ssz)
        rm, nn = src, 1
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    erb = rc is not None or sae
    ctx = Ctx(mxcsr, rc)
    r, flags = form.elem(ctx, 0, val, 0, 0)
    flags, fault = finish_flags(form, flags, mxcsr, erb)
    if ll is None:
        ll = rc if rc is not None else RNG.choice([0, 1, 2])
    c.code = evex(form.mmm, form.pp, form.w, form.opc, gdst, rm, ll=ll, b=1 if erb else 0, n=nn)
    if fault:
        c.fault = "#XM"
    elif r != old:
        c.exp.append("%s=0x%X" % (GPR_NAMES[gdst], r))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    return c


def gen_f2si_form(form):
    comment("%s (EVEX.LLIG.%s.0F.W%d %02X) %s -> %s, %s" % (form.name, PPN[form.pp], form.w, form.opc,
                                                          form.st, form.dt, form.rc))
    sp = SPEC[form.st]
    for i, v in enumerate(sp):
        emit(gen_f2si(form, "special %X" % v, gdst=[0, 1, 9, 15, 3][i % 5], src=[1, 17, 31][i % 3], val=v))
    for t in range(6):
        emit(gen_f2si(form, "random", gdst=2, src=4))
    emit(gen_f2si(form, "mem", gdst=8, mem=True))
    for d8 in (1, -1, 127, -127):
        emit(gen_f2si(form, "mem disp8=%d N=%d (Tuple1 Fixed)" % (d8, form.ssz), gdst=8, mem=True, disp=d8 * form.ssz))
    ties = [v for v in sp if classify(v, FMT[form.st])[0] == "normal"][:24]
    for mx in (0x3F80, 0x5F80, 0x7F80, 0x1FC0):
        for v in ties[::3] + [1, FMT[form.st].sign | 1]:
            emit(gen_f2si(form, "mxcsr=%X" % mx, gdst=11, src=5, val=v, mxcsr=mx))
    if form.rc == "er":
        for rc in range(4):
            for v in ties[14:18] + [0x7FA00000 if form.st == "f32" else 0x7FF4000000000000]:
                emit(gen_f2si(form, "{er} rc=%d MXCSR unmasked" % rc, gdst=12, src=6, val=v, rc=rc, mxcsr=0))
    else:
        for ll in (0, 3):
            for v in ties[14:18:2] + [FMT[form.st].inf]:
                emit(gen_f2si(form, "{sae} L'L=%d MXCSR unmasked" % ll, gdst=12, src=6, val=v, sae=True, mxcsr=0,
                              ll=ll))
    emit(gen_f2si(form, "NaN, IM=0 -> #XM", gdst=13, src=7, val=FMT[form.st].qnan_indef, mxcsr=0x1F00))
    emit(gen_f2si(form, "inexact, PM=0 -> #XM", gdst=13, src=7, val=sp[17], mxcsr=0x0F80))
    for title, kw in (("aaa != 0", dict(aaa=1)), ("vvvv != 1111b", dict(vvvv=3)),
                      ("EVEX.b with memory", dict(rm=Mem(RSI, 0), b=1)), ("ModRM.reg GPR with EVEX.R'=0", dict(reg=16))):
        c = Case("%s %s #UD" % (form.name, title))
        rm = kw.pop("rm", 3)
        reg = kw.pop("reg", 0)
        c.code = evex(form.mmm, form.pp, form.w, form.opc, reg, rm, ll=0, **kw)
        c.fault = "#UD"
        emit(c)


# -- VCVTPS2PH ---------------------------------------------------------------------------------
def gen_ps2ph(title, vl, dst=1, src=2, kreg=0, kval=None, z=0, mem=False, vals=None, imm=None,
              mxcsr=MXCSR_DEFAULT, sae=False, ll=None, disp=0x40, disp32=False):
    form = PS2PH
    kl = vl // 4
    c = Case("VCVTPS2PH VL%d %s" % (vl * 8, title))
    if imm is None:
        imm = RNG.getrandbits(8)
    if vals is None:
        vals = [rnd_val("f32", form) for _ in range(kl)]
    simg = pack(vals, 4) + rnd_bytes(64 - 4 * kl)
    c.zmm[src] = simg
    vals = elems(simg[:4 * kl], 4)
    if mem:
        old = rnd_bytes(2 * kl)
        c.mem[MEM_RSI + disp] = old
    else:
        if dst != src:
            c.zmm[dst] = rnd_bytes(64)
        old = c.zmm[dst][:2 * kl]
    dold = elems(old, 2)
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    ctx = Ctx(mxcsr)
    res, flags = [], 0
    for j in range(kl):
        if kreg and not (kval >> j) & 1:
            res.append(None)
            continue
        r, fl = form.elem(ctx, 0, vals[j], 0, imm)
        res.append(r)
        flags |= fl
    flags, fault = finish_flags(form, flags, mxcsr, sae)
    if ll is None:
        ll = VL_LL[vl]
    rm = Mem(RSI, disp, disp32=disp32) if mem else dst
    c.code = evex(3, 1, 0, 0x1D, src, rm, ll=ll, b=1 if sae else 0, z=z, aaa=kreg, imm=imm, n=2 * kl)
    if fault:
        c.fault = "#XM"
    else:
        out = [(0 if z else dold[j]) if res[j] is None else res[j] for j in range(kl)]
        if mem:
            if pack(out, 2) != old:
                c.exp.append("m+0x%X=%s" % (MEM_RSI + disp, hexs(pack(out, 2))))
        else:
            c.exp.append("zmm%d=%s" % (dst, hexs(pack(out, 2) + bytes(64 - 2 * kl))))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    return c


def gen_ps2ph_all():
    comment("VCVTPS2PH (EVEX.66.0F3A.W0 1D /r ib, Half Mem, destination = ModRM.r/m, {sae})")
    sp = SPEC["f32"]
    for vl in (16, 32, 64):
        emit(gen_ps2ph("reg nomask", vl, dst=1, src=2))
        emit(gen_ps2ph("reg merge", vl, dst=17, src=3, kreg=1, kval=RNG.getrandbits(16)))
        emit(gen_ps2ph("reg zero", vl, dst=4, src=29, kreg=2, kval=RNG.getrandbits(16), z=1))
        emit(gen_ps2ph("mem nomask", vl, src=5, mem=True))
        emit(gen_ps2ph("mem masked (only active elements written)", vl, src=6, mem=True, kreg=3,
                       kval=RNG.getrandbits(16)))
        emit(gen_ps2ph("mem k=0 (nothing written)", vl, src=6, mem=True, kreg=3, kval=0))
        for d8 in (1, -1, 127, -127):
            emit(gen_ps2ph("mem disp8=%d N=%d" % (d8, vl // 2), vl, src=7, mem=True, disp=d8 * vl // 2))
        for imm in range(8):
            for sh in range(0, len(sp), vl // 4 * 2):
                kl = vl // 4
                vals = [sp[(i + sh) % len(sp)] if i % 2 == 0 else rnd_val("f32", PS2PH) for i in range(kl)]
                emit(gen_ps2ph("imm=%d specials %d" % (imm, sh), vl, dst=8, src=9, vals=vals, imm=imm,
                               mxcsr=[0x1F80, 0x3F80, 0x5F80, 0x7F80][(imm + sh) % 4]))
                if vl != 64:
                    break
    for mx in (0x1FC0, 0x9F80, 0x9FC0, 0x3FC0):
        for imm in (0, 4, 0xFB):
            vals = [sp[(i * 3 + imm) % len(sp)] for i in range(16)]
            vals[0], vals[1], vals[2] = 0x00000001, 0x80400000, 0x33000001
            emit(gen_ps2ph("mxcsr=%X imm=%02X" % (mx, imm), 64, dst=10, src=11, vals=vals, imm=imm, mxcsr=mx))
    emit(gen_ps2ph("imm8[7:3] ignored", 64, dst=10, src=11, imm=0xF9))
    # {sae}
    for ll in (0, 2, 3):
        vals = [rnd_val("f32", PS2PH) for _ in range(16)]
        vals[3] = 0x7FA00000
        emit(gen_ps2ph("{sae} L'L=%d MXCSR unmasked" % ll, 64, dst=12, src=13, vals=vals, imm=1, mxcsr=0, sae=True, ll=ll))
    # #XM
    vals = [rnd_val("f32", PS2PH) & 0x3FFFFFFF for _ in range(16)]
    vals = [0x3F800000 + (v & 0x3FF) * 0x2000 if j % 3 == 0 else v for j, v in enumerate(vals)]  # exact active
    for j in range(16):
        if j % 3:
            vals[j] = 0x7FA00000
    kv = sum(1 << j for j in range(16) if j % 3 == 0)
    emit(gen_ps2ph("SNaN only in masked-off lanes, IM=0", 64, dst=14, src=15, vals=vals, imm=0, kreg=4, kval=kv,
                   mxcsr=0x1F00))
    emit(gen_ps2ph("SNaN active, IM=0 -> #XM", 64, dst=14, src=15, vals=vals, imm=0, kreg=4, kval=kv | 2,
                   mxcsr=0x1F00))
    emit(gen_ps2ph("SNaN active, IM=0, memory destination unchanged -> #XM", 64, src=15, mem=True, vals=vals, imm=0,
                   kreg=4, kval=kv | 2, mxcsr=0x1F00))
    emit(gen_ps2ph("denormal with DM masked, PM=0 -> #XM with DE UE PE", 16, dst=14, src=15,
                   vals=[0x00000001, 0x3F800000, 0x3F800000, 0x3F800000], imm=0, mxcsr=0x0F80))
    emit(gen_ps2ph("inexact, PM=0 -> #XM", 32, dst=14, src=15, imm=0, mxcsr=0x0F80))
    emit(gen_ps2ph("dst=src", 64, dst=16, src=16, kreg=5, kval=0x0FF0))
    # fault suppression of the masked store at the end of the mapped area (E11)
    base = 0x10000 - MEM_RSI - 16
    emit(gen_ps2ph("masked store, elements past MEM+0x10000 masked off (no #PF)", 64, src=18, mem=True,
                   kreg=6, kval=0x00FF, disp=base, disp32=True))
    c = gen_ps2ph("masked store, an element past MEM+0x10000 active -> #PF", 64, src=18, mem=True,
                  kreg=6, kval=0x01FF, disp=base, disp32=True)
    c.exp = [e for e in c.exp if not e.startswith("m+")]
    c.fault = "#PF"
    emit(c)
    for title, kw in (("{z} with a memory destination", dict(rm=Mem(RSI, 0), z=1, aaa=1)),
                      ("EVEX.b with a memory destination", dict(rm=Mem(RSI, 0), b=1)),
                      ("W1", dict(w=1)), ("vvvv != 1111b", dict(vvvv=5)), ("L'L=11 without EVEX.b", dict(ll=3)),
                      ("{z} with aaa=0", dict(z=1))):
        c = Case("VCVTPS2PH %s #UD" % title)
        rm = kw.pop("rm", 3)
        w = kw.pop("w", 0)
        kw.setdefault("ll", 2)
        c.code = evex(3, 1, w, 0x1D, 2, rm, imm=0, **kw)
        c.fault = "#UD"
        emit(c)


def gen_fault_misc():
    comment("--- fault behaviour: Mem128 count read fully (E4NF.nb), Half Mem load suppression (E11)")
    for form in (SHIFT_FORMS[0], SHIFT_FORMS[5]):
        c = Case("%s count m128 on the unmapped page with k=0 -> #PF (no fault suppression)" % form.name)
        c.zmm[1] = rnd_bytes(64)
        c.zmm[2] = rnd_bytes(64)
        c.k[1] = 0
        c.code = evex(1, 1, form.w, form.opc, 1, Mem(RSI, 0x10000 - MEM_RSI - 8, disp32=True), vvvv=2, ll=2, aaa=1)
        c.fault = "#PF"
        emit(c)
    form = CVT_FORMS[14]   # VCVTPH2PS
    c = Case("VCVTPH2PS zmm load: elements past MEM+0x10000 masked off (fault suppressed)")
    base = 0x10000 - MEM_RSI - 16
    c.zmm[3] = rnd_bytes(64)
    c.k[2] = 0x00FF
    data = pack([RNG.choice(SPEC["f16"]) for _ in range(8)], 2)
    c.mem[MEM_RSI + base] = data
    ctx = Ctx()
    res = [form.elem(ctx, 0, h, 0, 0)[0] for h in elems(data, 2)]
    flags = 0
    for h in elems(data, 2):
        flags |= form.elem(ctx, 0, h, 0, 0)[1]
    c.code = evex(2, 1, 0, 0x13, 3, Mem(RSI, base, disp32=True), ll=2, z=1, aaa=2)
    c.exp.append("zmm3=%s" % hexs(pack(res, 4) + bytes(32)))
    if flags:
        c.exp.append("mxcsr=0x%X" % (MXCSR_DEFAULT | flags))
    emit(c)
    c = Case("VCVTPH2PS zmm load: an element past MEM+0x10000 active -> #PF")
    c.zmm[3] = rnd_bytes(64)
    c.k[2] = 0x01FF
    c.mem[MEM_RSI + base] = data
    c.code = evex(2, 1, 0, 0x13, 3, Mem(RSI, base, disp32=True), ll=2, z=1, aaa=2)
    c.fault = "#PF"
    emit(c)


# ---------------------------------------------------------------------------------------
# self test
# ---------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    ctx = Ctx()
    # encodings
    chk("enc vcvtdq2ps", evex(1, 0, 0, 0x5B, 1, 2, ll=2), bytes([0x62, 0xF1, 0x7C, 0x48, 0x5B, 0xCA]))
    chk("enc vcvtps2ph ymm1, zmm2, 4", evex(3, 1, 0, 0x1D, 2, 1, ll=2, imm=4),
        bytes([0x62, 0xF3, 0x7D, 0x48, 0x1D, 0xD1, 0x04]))
    chk("enc vcvtph2ps zmm1, [rsi+0x20] N=32", evex(2, 1, 0, 0x13, 1, Mem(RSI, 0x20), ll=2, n=32),
        bytes([0x62, 0xF2, 0x7D, 0x48, 0x13, 0x4E, 0x01]))
    chk("enc vcvtsd2si rax, xmm1 {rz-sae}", evex(1, 3, 1, 0x2D, 0, 1, ll=3, b=1),
        bytes([0x62, 0xF1, 0xFF, 0x78, 0x2D, 0xC1]))
    # FP -> integer
    chk("2.5 rne", cvt_f2i(0x40200000, F32, 32, True, ctx), (2, PE))
    chk("-2.5 rne", cvt_f2i(0xC0200000, F32, 32, True, ctx), (0xFFFFFFFE, PE))
    chk("1.5 rne", cvt_f2i(0x3FC00000, F32, 32, True, ctx), (2, PE))
    chk("2^31", cvt_f2i(0x4F000000, F32, 32, True, ctx), (0x80000000, IE))
    chk("-2^31", cvt_f2i(0xCF000000, F32, 32, True, ctx), (0x80000000, 0))
    chk("nan->u64", cvt_f2i(0x7FC00000, F32, 64, False, ctx), (0xFFFFFFFFFFFFFFFF, IE))
    chk("-0.7 trunc u32", cvt_f2i(0xBF333333, F32, 32, False, ctx, True), (0, PE))
    chk("-0.7 rne u32", cvt_f2i(0xBF333333, F32, 32, False, ctx), (0xFFFFFFFF, IE))
    chk("2^32-1 u32", cvt_f2i(0x41EFFFFFFFE00000, F64, 32, False, ctx), (0xFFFFFFFF, 0))
    chk("2^32-0.5 u32 rne", cvt_f2i(0x41EFFFFFFFF00000, F64, 32, False, ctx), (0xFFFFFFFF, IE))
    chk("2^32-0.5 u32 rz", cvt_f2i(0x41EFFFFFFFF00000, F64, 32, False, Ctx(0x7F80)), (0xFFFFFFFF, PE))
    chk("denorm ru", cvt_f2i(0x00000001, F32, 32, True, Ctx(0x5F80)), (1, PE))
    chk("denorm daz ru", cvt_f2i(0x00000001, F32, 32, True, Ctx(0x5FC0)), (0, 0))
    # integer -> FP
    chk("2^24+1", cvt_i2f(0x01000001, 32, True, F32, ctx), (0x4B800000, PE))
    chk("2^24+3", cvt_i2f(0x01000003, 32, True, F32, ctx), (0x4B800002, PE))
    chk("u32 max", cvt_i2f(0xFFFFFFFF, 32, False, F32, ctx), (0x4F800000, PE))
    chk("i32 -1", cvt_i2f(0xFFFFFFFF, 32, True, F32, ctx), (0xBF800000, 0))
    chk("u64 max -> f64", cvt_i2f(0xFFFFFFFFFFFFFFFF, 64, False, F64, ctx), (0x43F0000000000000, PE))
    # FP -> FP
    chk("pd2ps max+", cvt_f2f(0x47EFFFFFF0000000, F64, F32, ctx), (0x7F800000, OE | PE))
    chk("pd2ps max+half ulp rz: no overflow", cvt_f2f(0x47EFFFFFF0000000, F64, F32, Ctx(0x7F80)), (0x7F7FFFFF, PE))
    chk("pd2ps 2^128 rz", cvt_f2f(0x47F0000000000000, F64, F32, Ctx(0x7F80)), (0x7F7FFFFF, OE | PE))
    chk("pd2ps 2^-150", cvt_f2f(0x3690000000000000, F64, F32, ctx), (0x00000000, UE | PE))
    chk("pd2ps 2^-149", cvt_f2f(0x36A0000000000000, F64, F32, ctx), (0x00000001, 0))
    chk("pd2ps snan", cvt_f2f(0x7FF4000000000000, F64, F32, ctx), (0x7FE00000, IE))
    chk("ps2pd denorm", cvt_f2f(0x00000001, F32, F64, ctx), (0x36A0000000000000, DE))
    chk("ps2pd snan", cvt_f2f(0xFFA00001, F32, F64, ctx), (0xFFFC000020000000, IE))
    chk("ph2ps denorm", cvt_f2f(0x0001, F16, F32, Ctx(0x1FC0), daz=False, report_de=False), (0x33800000, 0))
    chk("ps2ph 65504", cvt_f2f(0x477FE000, F32, F16, ctx, rc=0, ftz=False), (0x7BFF, 0))
    chk("ps2ph 65520", cvt_f2f(0x477FF000, F32, F16, ctx, rc=0, ftz=False), (0x7C00, OE | PE))
    chk("ps2ph 65520 rz", cvt_f2f(0x477FF000, F32, F16, ctx, rc=3, ftz=False), (0x7BFF, PE))
    chk("ps2ph 65536 rz", cvt_f2f(0x47800000, F32, F16, ctx, rc=3, ftz=False), (0x7BFF, OE | PE))
    chk("ps2ph 2^-25 tie", cvt_f2f(0x33000000, F32, F16, ctx, rc=0, ftz=False), (0x0000, UE | PE))
    chk("ps2ph 2^-25+", cvt_f2f(0x33000001, F32, F16, ctx, rc=0, ftz=False), (0x0001, UE | PE))
    chk("ps2ph ftz ignored", cvt_f2f(0x33800000, F32, F16, Ctx(0x9F80), rc=0, ftz=False), (0x0001, 0))
    # VGETEXP (Table 5-13)
    chk("getexp 1", getexp(0x3F800000, F32, ctx), (0, 0))
    chk("getexp 8", getexp(0x41000000, F32, ctx), (0x40400000, 0))
    chk("getexp -0.1", getexp(0xBDCCCCCD, F32, ctx), (0xC0800000, 0))
    chk("getexp 0", getexp(0x80000000, F32, ctx), (0xFF800000, 0))
    chk("getexp -inf", getexp(0xFF800000, F32, ctx), (0x7F800000, 0))
    chk("getexp 2^-149", getexp(0x00000001, F32, ctx), (0xC3150000, DE))
    chk("getexp denorm daz", getexp(0x00000001, F32, Ctx(0x1FC0)), (0xFF800000, 0))
    # VGETMANT (Figure 5-15 / Table 5-16): 12 = 1.5 * 2^3
    chk("getmant 12 [1,2)", getmant(0x41400000, F32, 0, ctx), (0x3FC00000, 0))
    chk("getmant 12 [1/2,2) odd", getmant(0x41400000, F32, 1, ctx), (0x3F400000, 0))
    chk("getmant 12 [1/2,1)", getmant(0x41400000, F32, 2, ctx), (0x3F400000, 0))
    chk("getmant 12 [3/4,3/2)", getmant(0x41400000, F32, 3, ctx), (0x3F400000, 0))
    chk("getmant 10 [3/4,3/2)", getmant(0x41200000, F32, 3, ctx), (0x3FA00000, 0))
    chk("getmant -12 sc=0", getmant(0xC1400000, F32, 0, ctx), (0xBFC00000, 0))
    chk("getmant -12 sc=1", getmant(0xC1400000, F32, 4, ctx), (0x3FC00000, 0))
    chk("getmant -12 sc=2", getmant(0xC1400000, F32, 8, ctx), (0xFFC00000, IE))
    chk("getmant -0 sc=0", getmant(0x80000000, F32, 0, ctx), (0xBF800000, 0))
    chk("getmant +inf", getmant(0x7F800000, F32, 3, ctx), (0x3F800000, 0))
    chk("getmant -inf sc=2", getmant(0xFF800000, F32, 8, ctx), (0xFFC00000, IE))
    chk("getmant denorm", getmant(0x00000003, F32, 0, ctx), (0x3FC00000, DE))
    # VSCALEF (Table 5-37)
    chk("scalef 3*2^floor(2.7)", scalef(0x40400000, 0x402CCCCD, F32, ctx), (0x41400000, 0))
    chk("scalef 3*2^floor(-0.5)", scalef(0x40400000, 0xBF000000, F32, ctx), (0x3FC00000, 0))
    chk("scalef qnan,+inf", scalef(0x7FC00000, 0x7F800000, F32, ctx), (0x7F800000, 0))
    chk("scalef qnan,-inf", scalef(0xFFC00000, 0xFF800000, F32, ctx), (0x00000000, 0))
    chk("scalef inf,-inf", scalef(0x7F800000, 0xFF800000, F32, ctx), (0xFFC00000, IE))
    chk("scalef 0,+inf", scalef(0x80000000, 0x7F800000, F32, ctx), (0xFFC00000, IE))
    chk("scalef -2,+inf", scalef(0xC0000000, 0x7F800000, F32, ctx), (0xFF800000, 0))
    chk("scalef -2,-inf", scalef(0xC0000000, 0xFF800000, F32, ctx), (0x80000000, 0))
    chk("scalef 1,-denorm", scalef(0x3F800000, 0x80000001, F32, ctx), (0x3F000000, 0))
    chk("scalef ovf", scalef(0x3F800000, 0x43000000, F32, ctx), (0x7F800000, OE | PE))
    chk("scalef 1,-149", scalef(0x3F800000, 0xC3150000, F32, ctx), (0x00000001, 0))
    chk("scalef denorm src1", scalef(0x00000001, 0x3F800000, F32, ctx), (0x00000002, DE))
    # VFIXUPIMM: +0 with table nibble 2 = 0101 -> +INF, imm8[0] -> ZE
    chk("fixup zero->+inf", fixupimm(0x12345678, 0x00000000, 0x00000500, F32, 0x01, ctx), (0x7F800000, ZE))
    chk("fixup +1 -> pi/2", fixupimm(0, 0x3F800000, 0x0000D000, F32, 0x0C, ctx), (0x3FC90FDB, ZE | IE))
    chk("fixup keep dest", fixupimm(0xDEADBEEF, 0xC0000000, 0x00000000, F32, 0x40, ctx), (0xDEADBEEF, IE))
    chk("fixup snan qnan", fixupimm(0, 0x7FA00000, 0x00000020, F32, 0x10, ctx), (0x7FE00000, IE))
    chk("fixup -inf sign", fixupimm(0, 0xFF800000, 0x00060000, F32, 0, ctx), (0xFF800000, 0))
    chk("fixup daz -denorm sign", fixupimm(0, 0x80000001, 0x00000600, F32, 0, Ctx(0x1FC0)), (0xFF800000, 0))
    chk("fixup pd 90", fixupimm(0, 0x4000000000000000, 0xC0000000, F64, 0, ctx), (0x4056800000000000, 0))
    # VRNDSCALE: ROUND(x) = 2^-M Round_to_INT(x 2^M)
    chk("rndscale 1.3 M=1", rndscale(0x3FA66666, F32, 0x10, ctx), (0x3FC00000, PE))
    chk("rndscale 2.5 rne", rndscale(0x40200000, F32, 0x00, ctx), (0x40000000, PE))
    chk("rndscale 2.5 spe", rndscale(0x40200000, F32, 0x08, ctx), (0x40000000, 0))
    chk("rndscale -0.3 ru", rndscale(0xBE99999A, F32, 0x02, ctx), (0x80000000, PE))
    chk("rndscale mxcsr rc", rndscale(0x40200000, F32, 0x04, Ctx(0x5F80)), (0x40400000, PE))
    chk("rndscale denorm daz", rndscale(0x80000001, F32, 0x02, Ctx(0x1FC0)), (0x80000000, 0))
    chk("rndscale snan", rndscale(0x7FA00000, F32, 0, ctx), (0x7FE00000, IE))
    # RCP14 / RSQRT14 stand-in
    chk("rcp14 2", rcp14(0x40000000, F32, ctx), (0x3F000000, 0))
    chk("rcp14 3", rcp14(0x40400000, F32, ctx), (0x3EAAAAAB, 0))
    chk("rcp14 -0", rcp14(0x80000000, F32, ctx), (0xFF800000, 0))
    chk("rcp14 2^-128", rcp14(0x00200000, F32, ctx), (0x7F800000, 0))
    chk("rcp14 max", rcp14(0x7F7FFFFF, F32, ctx), (0x00200000, 0))
    chk("rcp14 max ftz", rcp14(0x7F7FFFFF, F32, Ctx(0x9F80)), (0x00000000, 0))
    chk("rsqrt14 4", rsqrt14(0x40800000, F32, ctx), (0x3F000000, 0))
    chk("rsqrt14 2", rsqrt14(0x40000000, F32, ctx), (0x3F3504F3, 0))
    chk("rsqrt14 -1", rsqrt14(0xBF800000, F32, ctx), (0xFFC00000, 0))
    chk("rsqrt14 -0", rsqrt14(0x80000000, F32, ctx), (0xFF800000, 0))
    chk("rsqrt14 -denorm daz", rsqrt14(0x80000001, F32, Ctx(0x1FC0)), (0xFF800000, 0))
    chk("rsqrt14 -denorm", rsqrt14(0x80000001, F32, ctx), (0xFFC00000, 0))
    chk("rsqrt14 +inf", rsqrt14(0x7F800000, F32, ctx), (0x00000000, 0))
    chk("rsqrt14 2 pd", rsqrt14(0x4000000000000000, F64, ctx), (0x3FE6A09E667F3BCD, 0))
    # shifts / rotates
    chk("sra 64", shift_elem("sra", 0x80000000, 64, 32), 0xFFFFFFFF)
    chk("sll 32", shift_elem("sll", 1, 32, 32), 0)
    chk("rol 33", shift_elem("rol", 0x80000001, 33, 32), 0x00000003)
    return ok


# ---------------------------------------------------------------------------------------
# hardware cross-check: legacy SSE / AVX / F16C forms evaluated by the same element models
# ---------------------------------------------------------------------------------------
HW_MXCSR = [0x1F80, 0x3F80, 0x5F80, 0x7F80, 0x1FC0, 0x9F80, 0x9FC0, 0x1F00, 0x0F80]


def hw_packed(asm, form, nsrc, ndst_bytes, imm=None, ymm_src=False, ymm_dst=False, vex=False):
    return dict(asm=asm, form=form, nsrc=nsrc, ndst=ndst_bytes, imm=imm, ymm_src=ymm_src, ymm_dst=ymm_dst,
                vex=vex)


def hw_ops():
    cf = {f.name: f for f in CVT_FORMS}
    sf = {(f.name, f.w): f for f in SI2F_FORMS}
    gf = {(f.name, f.w): f for f in F2SI_FORMS}
    ops = []
    # packed: (family, asm, form, src elements, ...)
    ops.append(("cvtdq2ps", "cvtdq2ps xmm0, xmm1", cf["VCVTDQ2PS"], "packed", 4, 16))
    ops.append(("cvtps2dq", "cvtps2dq xmm0, xmm1", cf["VCVTPS2DQ"], "packed", 4, 16))
    ops.append(("cvttps2dq", "cvttps2dq xmm0, xmm1", cf["VCVTTPS2DQ"], "packed", 4, 16))
    ops.append(("cvtpd2dq", "cvtpd2dq xmm0, xmm1", cf["VCVTPD2DQ"], "packed", 2, 16))
    ops.append(("cvttpd2dq", "cvttpd2dq xmm0, xmm1", cf["VCVTTPD2DQ"], "packed", 2, 16))
    ops.append(("cvtdq2pd", "cvtdq2pd xmm0, xmm1", cf["VCVTDQ2PD"], "packed", 2, 16))
    ops.append(("cvtps2pd", "cvtps2pd xmm0, xmm1", cf["VCVTPS2PD"], "packed", 2, 16))
    ops.append(("cvtpd2ps", "cvtpd2ps xmm0, xmm1", cf["VCVTPD2PS"], "packed", 2, 16))
    ops.append(("vcvtph2ps", "vcvtph2ps xmm0, xmm1", cf["VCVTPH2PS"], "packedvex", 4, 16))
    ops.append(("vcvtph2ps", "vcvtph2ps ymm0, xmm1", cf["VCVTPH2PS"], "packedvex", 8, 32))
    for imm in range(8):
        ops.append(("vcvtps2ph", "vcvtps2ph xmm0, xmm1, %d" % imm, PS2PH, "ps2ph", 4, imm))
        ops.append(("vcvtps2ph", "vcvtps2ph xmm0, ymm1, %d" % imm, PS2PH, "ps2ph", 8, imm))
        ops.append(("vcvtps2ph", "vcvtps2ph qword ptr [rsi], xmm1, %d" % imm, PS2PH, "ps2phm", 4, imm))
    for nm, ins in (("VCVTSI2SS", "cvtsi2ss"), ("VCVTSI2SD", "cvtsi2sd")):
        for w, r in ((0, "eax"), (1, "rax")):
            ops.append((ins, "%s xmm0, %s" % (ins, r), sf[(nm, w)], "si2f", w, None))
    for nm, ins in (("VCVTSS2SI", "cvtss2si"), ("VCVTSD2SI", "cvtsd2si"), ("VCVTTSS2SI", "cvttss2si"),
                    ("VCVTTSD2SI", "cvttsd2si")):
        for w, r in ((0, "eax"), (1, "rax")):
            ops.append((ins, "%s %s, xmm1" % (ins, r), gf[(nm, w)], "f2si", w, None))
    sc = {f.name: f for f in SCALAR_FORMS}
    ops.append(("cvtss2sd", "cvtss2sd xmm0, xmm1", sc["VCVTSS2SD"], "sca", 0, None))
    ops.append(("cvtsd2ss", "cvtsd2ss xmm0, xmm1", sc["VCVTSD2SS"], "sca", 0, None))
    rp = {f.name: f for f in SPECIAL_FORMS}
    for imm in range(16):
        ops.append(("roundps", "roundps xmm0, xmm1, %d" % imm, rp["VRNDSCALEPS"], "round", 4, imm))
        ops.append(("roundpd", "roundpd xmm0, xmm1, %d" % imm, rp["VRNDSCALEPD"], "round", 2, imm))
        ops.append(("roundss", "roundss xmm0, xmm1, %d" % imm, sc["VRNDSCALESS"], "roundsca", 1, imm))
        ops.append(("roundsd", "roundsd xmm0, xmm1, %d" % imm, sc["VRNDSCALESD"], "roundsca", 1, imm))
    shf = {f.name: f for f in SHIFT_FORMS}
    for ins, nm in (("pslld", "VPSLLD"), ("psllq", "VPSLLQ"), ("psrld", "VPSRLD"), ("psrlq", "VPSRLQ"),
                    ("psrad", "VPSRAD")):
        ops.append((ins, "%s xmm0, xmm1" % ins, shf[nm], "shift", 0, None))
    return ops


def hw_finish(flags, mx, form):
    unm = flags & ~(mx >> 7) & 0x3F
    if unm & (IE | DE | ZE):
        flags &= ~(OE | UE | PE)
    return flags, bool(unm)


def hw_src_vals(form, n, t, mx):
    """source values: special values (in turn) and random values"""
    sp = SPEC[form.st]
    if t < (len(sp) + n - 1) // n:
        return [sp[(t * n + j) % len(sp)] for j in range(n)]
    return [rnd_val(form.st, form) if (t + j) % 3 else RNG.choice(sp) for j in range(n)]


def hwcheck_gen(out, expect_path):
    exps = []
    for fam, asm, form, kind, a1, a2 in hw_ops():
        nsp = len(SPEC[form.st])
        for mx in HW_MXCSR:
            if kind in ("si2f", "f2si", "sca", "roundsca", "shift"):
                ntests = nsp + 6
            elif kind in ("round", "ps2ph", "ps2phm"):
                ntests = (nsp + a1 - 1) // a1 + 2
            else:
                ntests = (nsp + a1 - 1) // a1 + 4
            for t in range(ntests):
                ctx = Ctx(mx)
                inp, exp = {"mxcsr": mx}, {}
                flags = 0
                x0 = rnd_bytes(16)
                if kind in ("packed", "packedvex", "round"):
                    n = a1
                    vals = hw_src_vals(form, n, t, mx)
                    ssz, dsz = form.ssz, form.dsz
                    srcb = pack(vals, ssz)
                    if kind == "packedvex" and n == 8:
                        inp["xmm1"] = srcb
                    else:
                        inp["xmm1"] = srcb + rnd_bytes(16 - len(srcb))
                    inp["xmm0"] = x0
                    imm = a2 if kind == "round" else 0
                    res = []
                    for v in vals:
                        r, fl = form.elem(ctx, 0, v, 0, imm)
                        res.append(r)
                        flags |= fl
                    outb = pack(res, dsz)
                    if kind == "packedvex":
                        inp["ymmh0"] = rnd_bytes(16)
                        exp["xmm0"] = outb[:16]
                        exp["ymmh0"] = outb[16:32] if n == 8 else bytes(16)
                    else:
                        exp["xmm0"] = outb + bytes(16 - len(outb))
                elif kind in ("ps2ph", "ps2phm"):
                    n, imm = a1, a2
                    vals = hw_src_vals(form, n, t, mx)
                    srcb = pack(vals, 4)
                    inp["xmm1"] = srcb[:16]
                    if n == 8:
                        inp["ymmh1"] = srcb[16:32]
                    res = []
                    for v in vals:
                        r, fl = form.elem(ctx, 0, v, 0, imm)
                        res.append(r)
                        flags |= fl
                    if kind == "ps2ph":
                        inp["xmm0"] = x0
                        inp["ymmh0"] = rnd_bytes(16)
                        outb = pack(res, 2)
                        exp["xmm0"] = outb + bytes(16 - len(outb))
                        exp["ymmh0"] = bytes(16)
                    else:
                        old = rnd_bytes(8)
                        inp["m+0x8000"] = old
                        exp["m+0x8000"] = pack(res, 2)
                elif kind == "si2f":
                    w = a1
                    v = SPEC[form.st][t] if t < nsp else rnd_val(form.st)
                    full = v if w else (v | (RNG.getrandbits(32) << 32))
                    inp["rax"] = full
                    inp["xmm0"] = x0
                    r, flags = form.elem(ctx, 0, v, 0, 0)
                    exp["xmm0"] = pack([r], form.dsz) + x0[form.dsz:]
                elif kind == "f2si":
                    v = SPEC[form.st][t] if t < nsp else rnd_val(form.st, form)
                    inp["xmm1"] = pack([v], form.ssz) + rnd_bytes(16 - form.ssz)
                    inp["rax"] = RNG.getrandbits(64)
                    r, flags = form.elem(ctx, 0, v, 0, 0)
                    exp["rax"] = r
                elif kind in ("sca", "roundsca"):
                    v = SPEC[form.st][t] if t < nsp else rnd_val(form.st, form)
                    inp["xmm1"] = pack([v], form.ssz) + rnd_bytes(16 - form.ssz)
                    inp["xmm0"] = x0
                    r, flags = form.elem(ctx, 0, v, 0, a2 if kind == "roundsca" else 0)
                    exp["xmm0"] = pack([r], form.dsz) + x0[form.dsz:]
                elif kind == "shift":
                    esz = form.ssz
                    bits = 8 * esz
                    cnts = [0, 1, bits - 1, bits, bits + 1, 255, 1 << 32, 1 << 63, (1 << 64) - 1]
                    cnt = cnts[t % len(cnts)] if t < len(cnts) else RNG.choice([RNG.randint(0, bits + 3),
                                                                               RNG.getrandbits(64)])
                    inp["xmm1"] = pack([cnt, RNG.getrandbits(64)], 8)
                    inp["xmm0"] = x0
                    a = elems(x0, esz)
                    exp["xmm0"] = pack([shift_elem(form.op, x, cnt, bits) for x in a], esz)
                flags, fault = hw_finish(flags, mx, form)
                if kind == "shift":
                    flags, fault = 0, False
                if fault:
                    exp = {}
                exp["mxcsr"] = mx | flags
                exp["fault"] = 19 if fault else -1
                ins = []
                for k in ("rax", "xmm0", "xmm1", "ymmh0", "ymmh1", "mxcsr", "m+0x8000"):
                    if k in inp:
                        v = inp[k]
                        ins.append("%s=%s" % (k, hexs(v) if isinstance(v, bytes) else "0x%X" % v))
                out.write("%s | %s\n" % (asm, " ".join(ins)))
                e = {"fam": fam, "asm": asm, "in": {k: (hexs(v) if isinstance(v, bytes) else v) for k, v in inp.items()},
                     "exp": {k: (hexs(v) if isinstance(v, bytes) else v) for k, v in exp.items()}}
                exps.append(e)
    json.dump(exps, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    exps = json.load(open(expect_path))
    per = {}
    bad_total, seen = 0, 0
    cur = None
    shown = {}
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] \S+ ", line)
        if m:
            cur = int(m.group(1))
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if not (m and cur is not None):
            continue
        fields = m.group(1)
        fault = -1
        fm = re.match(r"fault #(\d+)\s*(.*)$", fields)
        if fm:
            fault, fields = int(fm.group(1)), fm.group(2)
        kv = dict(x.split("=", 1) for x in fields.split() if "=" in x)
        e = exps[cur]
        cur = None
        fam = e["fam"]
        st = per.setdefault(fam, [0, 0])
        st[0] += 1
        seen += 1
        diffs = []
        if fault != e["exp"]["fault"]:
            diffs.append("fault hw %d model %d" % (fault, e["exp"]["fault"]))
        for k, want in e["exp"].items():
            if k == "fault":
                continue
            if k == "mxcsr":
                got = int(kv["mxcsr"], 16) if "mxcsr" in kv else e["in"]["mxcsr"]
                if got != want:
                    diffs.append("mxcsr hw %X model %X" % (got, want))
            elif k == "rax":
                got = int(kv["rax"], 16) if "rax" in kv else e["in"]["rax"]
                if got != want:
                    diffs.append("rax hw %X model %X" % (got, want))
            elif k.startswith("m+"):
                old = bytes.fromhex(e["in"][k])
                buf = bytearray(old)
                for kk, vv in kv.items():
                    if kk.startswith("m+"):
                        off = int(kk[2:], 16) - 0x8000
                        b = bytes.fromhex(vv)
                        for i, x in enumerate(b):
                            if 0 <= off + i < len(buf):
                                buf[off + i] = x
                if hexs(bytes(buf)) != want.upper():
                    diffs.append("%s hw %s model %s" % (k, hexs(bytes(buf)), want))
            else:
                got = kv.get(k, e["in"].get(k, "00" * 16))
                if got.upper() != want.upper():
                    diffs.append("%s hw %s model %s" % (k, got, want))
        # fields the hardware changed that the model does not list
        for k in kv:
            if k not in e["exp"] and not k.startswith("m+") and not k.startswith("fx+") and k not in ("rflags", "env"):
                diffs.append("hw changed %s=%s (model: unchanged)" % (k, kv[k]))
        if diffs:
            st[1] += 1
            bad_total += 1
            shown.setdefault(fam, 0)
            if shown[fam] < 8:
                shown[fam] += 1
                print("[%d] %s | %s : %s" % (exps.index(e), e["asm"], " ".join("%s=%s" % (k, v if isinstance(v, str)
                                                                                         else "%X" % v)
                                                                                for k, v in e["in"].items()),
                                              "; ".join(diffs)))
    for fam in sorted(per):
        print("  %-10s compared %5d  differ %5d" % (fam, per[fam][0], per[fam][1]))
    print("hwcheck: %d cases compared, %d differ from the model" % (seen, bad_total))
    return bad_total == 0


# ---------------------------------------------------------------------------------------
def main():
    if "--hwgen" in sys.argv:
        i = sys.argv.index("--hwgen")
        with open(sys.argv[i + 1], "w", newline="\n") as out:
            out.write("# hardware cases of ref_evex_m2_cvt.py --hwgen (legacy SSE / F16C forms of the M2 models)\n")
            hwcheck_gen(out, sys.argv[i + 2])
        return
    if "--hwcmp" in sys.argv:
        i = sys.argv.index("--hwcmp")
        sys.exit(0 if hwcheck_cmp(sys.argv[i + 1], sys.argv[i + 2]) else 1)
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        comment("--- A. packed conversions")
        for form in CVT_FORMS:
            gen_vec_form(form)
        gen_ps2ph_all()
        comment("--- A. scalar conversions")
        for form in SCALAR_FORMS[:2]:
            gen_sca_form(form)
        for form in SI2F_FORMS:
            gen_si2f_form(form)
        for form in F2SI_FORMS:
            gen_f2si_form(form)
        comment("--- B/C. RCP14 RSQRT14 GETEXP GETMANT RNDSCALE SCALEF FIXUPIMM")
        for form in SPECIAL_FORMS:
            gen_vec_form(form)
        for form in SCALAR_FORMS[2:]:
            gen_sca_form(form)
        comment("--- D. shifts by xmm/m128 count, variable rotates")
        for form in SHIFT_FORMS:
            gen_vec_form(form)
        gen_disp8_vec()
        gen_fault_misc()
        out = sys.stdout
        out.write("# EVEX milestone M2 (conversions, FP specials, shifts): expected values from the independent\n")
        out.write("# SDM model Emulator/tools/isa/ref_evex_m2_cvt.py --cases (regenerate, do not edit).\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m2_cvt.txt --avx512 --expect-only\n")
        out.write("# RSI = R14 = MEM + 0x8000; MEM + 0x10000 is unmapped.\n")
        n = 0
        for c in cases:
            if isinstance(c, str):
                out.write(c + "\n")
            else:
                out.write("# " + c.title + "\n")
                out.write(c.line() + "\n")
                n += 1
        sys.stderr.write("%d cases\n" % n)
        return
    print(__doc__)


if __name__ == "__main__":
    main()
