#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_avx10_a.py -- independent reference model (Python 3 stdlib only, exact rationals with
fractions.Fraction) of the AVX10.2 instructions of worktree avx10_a, and generator of the
expected-value case file Emulator/data/cases_avx10_a.txt.

Written from the Intel documents only (no emulator source was read):
  * Intel AVX10.2 Architecture Specification 361050-007 (rev 7.0), text copy
    intel_docs_text/avx10_2_spec_361050_007.md ("spec Lnnnn" below = line in that file):
    ch.1 CHANGES (L971), ch.4 exception classes (L2644), ch.5.2 MIN/MAX helpers (L4056),
    ch.5.3 saturating conversion helpers (L4406), ch.7 BF16 instructions (L6919),
    ch.8 VCOMX (L9899), ch.11 MINMAX (L13014), ch.12 saturating converts (L13867).
  * Intel SDM (092): Vol2A 2.7 EVEX encoding / Tables 2-36/2-37 (disp8*N), Vol1 4.8.3.5 /
    Table 4-7 (NaN propagation), 4.9.1.5 (underflow = tiny after rounding with unbounded
    exponent), 10.2.3.3 (FTZ), 10.2.3.4 (DAZ), 14.5.2 / Table 14-17 (FMA NaN priority and
    single rounding), Vol2C VCOMISH ("Invalid, Denormal"; FP16 denormals never DAZ'd).
  * Encodings: Emulator/data/evex_forms.tsv rows sourced from "AVX10.2 spec 361050-007".

Instruction groups and the semantic rules modelled
--------------------------------------------------
A  BF16 (spec ch.7, all E4): no SIMD FP exception, MXCSR neither consulted nor updated (the
   cases run with MXCSR = 6000h = RZ, DAZ=FTZ=0, every exception unmasked to prove it).
   Arithmetic (ADD SUB MUL DIV SQRT FMA SCALEF REDUCE RNDSCALE RCP RSQRT): inputs DAZ
   (denormal -> zero of the same sign), result rounded RNE with unbounded exponent; a result
   that is tiny after rounding (|r| < 2^-126, exact or not) is flushed to a zero with the sign
   of the true result (FTZ), overflow -> +-Inf. NaNs (SDM Vol1 Table 4-7): the first NaN
   operand in source order (SRC1 = vvvv, then SRC2) is returned quietened (bit 6 set);
   invalid (Inf-Inf, 0*Inf, 0/0, Inf/Inf, sqrt(<0)) -> QNaN indefinite FFC0h.
   FMA: one rounding of a*b+c (132: a=DEST b=SRC3 c=SRC2, 213: a=SRC2 b=DEST c=SRC3,
   231: a=SRC2 b=SRC3 c=DEST), NaN priority a, b, c (SDM Table 14-17), exact zero sum:
   same-signed zeros keep the sign, otherwise +0. {1toN} broadcasts the memory operand SRC3.
   VMAX/VMINBF16: SDM MAXPS rules (both zero or any NaN -> SRC2 unchanged, SNaN not quietened)
   after DAZ (a denormal operand is returned as the signed zero).
   VCMPBF16 (32 predicates, DAZ, -0 == +0), VFPCLASSBF16 (spec pseudocode; denormal = zero),
   VCOMISBF16 (ZF PF CF, OF SF AF := 0, DAZ), VGETEXP/VGETMANT/VREDUCE/VRNDSCALE/VSCALEF per
   their pseudocode. VRCPBF16 / VRSQRTBF16 (the spec only bounds the error by 2^-8 + 2^-14):
   MODEL CHOICE = the correctly rounded (RNE) BF16 value of 1/x resp. 1/sqrt(x) after DAZ, FTZ
   of the result, plus the special cases of spec Tables 7.2 / 7.3; --selftest checks this
   choice against the bound for every positive normal input and against both tables.
B  Saturating conversions (spec ch.12 + helpers 5.3, E2 / E3NF; BF16 forms E4 without flags):
   the helper pseudocode is executed literally (branch structure and comparison operators
   exactly as printed, see AMBIGUOUS notes), result in the low byte (word lane for BF16/PH, dword
   lane for PS) with the upper bits zero for the *2I[U]BS forms. NaN -> 0, too big -> MAX,
   too small -> MIN; IE for NaN/Inf/out of range, PE for an inexact in-range conversion.
   FP32/FP64 inputs honour MXCSR.DAZ (SDM Vol1 10.2.3.4); FP16 inputs never (AVX512-FP16 rule).
   Unmasked IE/PE -> #XM, destination unchanged, MXCSR flags set (PE dropped when the unmasked
   exception is IE). {sae}: no flag, no #XM; {er}: L'L = RC, VL 512, SAE implied.
C  MINMAX (spec ch.11 + Figure 5.9 minmax()): imm8[1:0] op select, imm8[3:2] sign control,
   imm8[4] NaN-propagation select, exactly as Figures 5.1-5.9. DAZ: BF16 always (no flags),
   PS/PD/SS/SD MXCSR.DAZ, PH/SH never. Flags (except = true for PH PS PD SH SS SD): IE if an
   operand is an SNaN, else none if an operand is a QNaN, else DE if an operand is denormal.
   Scalar forms: bits 127:esz from SRC1, 511:128 zero.
D  VCOMX[S,US][SD,SS,SH] (spec ch.8): UNORDERED OF SF ZF PF CF = 11011, GT 00000, LT 10001,
   EQ 11100, AF = 0. VCOMX: IE for any NaN, VUCOMX: IE for an SNaN only; no DE when an operand
   is a NaN; DE for a denormal operand (SS/SD: not with DAZ, SH: always). Unmasked -> #XM with
   EFLAGS unchanged. {sae}: no flag, no #XM.
E  Gating / encoding: AVX-512F (milestone M1) forms run with AVX10.2 alone; #UD for EVEX.U = 0,
   W1 on W0-only forms, z with a k destination, aaa != 0 without masking, vvvv != 1111b without
   a vvvv operand, z without aaa, L'L = 11b, EVEX.b on a register form without {er}/{sae},
   EVEX.b with a scalar memory operand.

AMBIGUOUS spec points: inputs whose expected value depends on an ambiguous / contradictory spec
point are kept OUT of the ordinary cases (each lane function returns an "ambiguous" marker; the
generator replaces such inputs by 1.0) and are tested only in the "AMBIGUOUS-nn" cases at the end
of the file; search this file for "AMBIGUOUS:" for every decision and its spec line.

Usage:
  python ref_avx10_a.py --selftest   hand-derived checks of the model, exit 0 on pass
  python ref_avx10_a.py --cases      Emulator/data/cases_avx10_a.txt (stdout)
"""

import random
import sys
from fractions import Fraction

BS = chr(92)

# ---------------------------------------------------------------------------------------
# harness layout (Emulator/tests/alltest/at_engine.hpp): RSI = R14 = MEM + 0x8000
# ---------------------------------------------------------------------------------------
MEM_RSI = 0x8000
RSI, RDI, R14 = 6, 7, 14

IE, DE, ZE, OE, UE, PE = 1, 2, 4, 8, 16, 32
MXCSR_DEFAULT = 0x1F80
DAZ, FTZ = 0x40, 0x8000
RNE, RD, RU, RZ = 0, 1, 2, 3
MX_NOT_CONSULTED = 0x6000        # RZ, DAZ = FTZ = 0, every exception unmasked
NAN = ("snan", "qnan")


# ---------------------------------------------------------------------------------------
# EVEX encoding (SDM Vol2A 2.7.1; Tables 2-36/2-37 disp8*N) -- copied from ref_evex_m1.py
# ---------------------------------------------------------------------------------------
class Mem:
    def __init__(self, base=RSI, disp=0, index=None, scale=0, disp32=False):
        self.base, self.disp, self.index, self.scale, self.disp32 = base, disp, index, scale, disp32


def evex(mmm, pp, w, opc, reg, rm, vvvv=None, ll=0, b=0, z=0, aaa=0, imm=None, n=1,
         p0_or=0, p1_and=0xFF, p2_vp=None, prefixes=b""):
    """bytes of one EVEX instruction. reg/vvvv: 0-31 (vvvv None = unused: 1111b, V' = 1);
    rm: register number 0-31 or Mem. n = disp8*N scale of the form. mmm 5/6 = MAP5/MAP6."""
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
# IEEE binary formats with x86 rules (Vol1 4.8, 4.9, 10.2.3)
# ---------------------------------------------------------------------------------------
class Fmt:
    def __init__(self, name, bits, p, ebits):
        self.name, self.bits, self.p, self.ebits = name, bits, p, ebits
        self.bias = (1 << (ebits - 1)) - 1
        self.emin = 1 - self.bias
        self.emax = self.bias
        self.fbits = p - 1
        self.signbit = 1 << (bits - 1)
        self.inf = ((1 << ebits) - 1) << self.fbits
        self.one = self.bias << self.fbits
        self.qnan_indef = self.signbit | self.inf | (1 << (self.fbits - 1))
        self.esz = bits // 8


BF16 = Fmt("bf16", 16, 8, 8)
FP16 = Fmt("fp16", 16, 11, 5)
F32 = Fmt("fp32", 32, 24, 8)
F64 = Fmt("fp64", 64, 53, 11)


def classify(x, f):
    """(class, sign, signed exact value or None)"""
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
    return classify(x, f)[0] in NAN


def quiet(x, f):
    return x | (1 << (f.fbits - 1))


def daz_bits(x, f):
    """denormal -> zero with the sign of the operand (Vol1 10.2.3.4)"""
    return (x & f.signbit) if classify(x, f)[0] == "denorm" else x


def floor_log2(q):
    n, d = q.numerator, q.denominator
    e = n.bit_length() - d.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    if Fraction(2) ** (e + 1) <= q:
        e += 1
    return e


def round_frac(q, quantum, rc, negative):
    t = q / quantum
    fl = t.numerator // t.denominator
    rem = t - fl
    if rem == 0:
        return fl, False
    if rc == RNE:
        if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl & 1):
            fl += 1
    elif rc == RD:
        if negative:
            fl += 1
    elif rc == RU:
        if not negative:
            fl += 1
    return fl, True


def encode_value(v, f, rc, ftz):
    """round the exact non-zero rational v to format f: (bits, flags). Tininess is detected
    after rounding with unbounded exponent (Vol1 4.9.1.5); FTZ (Vol1 10.2.3.3): a tiny result,
    exact or not, becomes a zero with the sign of the true result (UE PE)."""
    neg = v < 0
    q = -v if neg else v
    sign = f.signbit if neg else 0
    flags = 0
    e = floor_log2(q)
    cnt_u, _ = round_frac(q, Fraction(2) ** (e - f.fbits), rc, neg)
    tiny = cnt_u * Fraction(2) ** (e - f.fbits) < Fraction(2) ** f.emin
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
        maxf = sign | ((((1 << f.ebits) - 2) << f.fbits) | ((1 << f.fbits) - 1))
        if rc == RNE:
            return sign | f.inf, flags
        if rc == RZ:
            return maxf, flags
        if rc == RD:
            return (sign | f.inf if neg else maxf), flags
        return (maxf if neg else sign | f.inf), flags
    return sign | ((e + f.bias) << f.fbits) | (cnt - (1 << f.fbits)), flags


def exact_bits(v, f):
    """bits of an exactly representable (or nearest RNE) value; v may be 0 / '-0'"""
    if v == 0:
        return 0
    return encode_value(Fraction(v), f, RNE, False)[0]


def fp_binop(op, a, b, f, mxcsr, rc=None):
    """add/sub/mul (copied from ref_evex_m1.py; generic in the format)"""
    if rc is None:
        rc = (mxcsr >> 13) & 3
    daz, ftz = bool(mxcsr & DAZ), bool(mxcsr & FTZ)
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    flags = 0
    if ca in NAN or cb in NAN:
        if ca == "snan" or cb == "snan":
            flags |= IE
        if ca in NAN:
            return quiet(a, f), flags
        return quiet(b, f), flags
    if daz:
        if ca == "denorm":
            ca, va = "zero", Fraction(0)
        if cb == "denorm":
            cb, vb = "zero", Fraction(0)
    elif ca == "denorm" or cb == "denorm":
        flags |= DE
    if op == "sub":
        sb ^= 1
        if vb is not None:
            vb = -vb
    if op in ("add", "sub"):
        if ca == "inf" and cb == "inf":
            if sa != sb:
                return f.qnan_indef, flags | IE
            return (f.signbit if sa else 0) | f.inf, flags
        if ca == "inf":
            return (f.signbit if sa else 0) | f.inf, flags
        if cb == "inf":
            return (f.signbit if sb else 0) | f.inf, flags
        s = va + vb
        if s == 0:
            if ca == "zero" and cb == "zero" and sa == sb:
                return (f.signbit if sa else 0), flags
            return (f.signbit if rc == RD else 0), flags
        r, fl = encode_value(s, f, rc, ftz)
        return r, flags | fl
    sgn = sa ^ sb
    if ca == "inf" or cb == "inf":
        if ca == "zero" or cb == "zero":
            return f.qnan_indef, flags | IE
        return (f.signbit if sgn else 0) | f.inf, flags
    p = va * vb
    if p == 0:
        return (f.signbit if sgn else 0), flags
    r, fl = encode_value(p, f, rc, ftz)
    return r, flags | fl


def fp_div(a, b, f, mxcsr, rc=None):
    if rc is None:
        rc = (mxcsr >> 13) & 3
    daz, ftz = bool(mxcsr & DAZ), bool(mxcsr & FTZ)
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    flags = 0
    if ca in NAN or cb in NAN:
        if ca == "snan" or cb == "snan":
            flags |= IE
        return (quiet(a, f) if ca in NAN else quiet(b, f)), flags
    if daz:
        if ca == "denorm":
            ca, va = "zero", Fraction(0)
        if cb == "denorm":
            cb, vb = "zero", Fraction(0)
    elif (ca == "denorm" or cb == "denorm") and not (ca == "denorm" and cb == "zero"):
        flags |= DE
    sgn = f.signbit if sa ^ sb else 0
    if (ca == "inf" and cb == "inf") or (ca == "zero" and cb == "zero"):
        return f.qnan_indef, flags | IE
    if ca == "inf":
        return sgn | f.inf, flags
    if cb == "inf" or ca == "zero":
        return sgn, flags
    if cb == "zero":
        return sgn | f.inf, flags | ZE
    r, fl = encode_value(va / vb, f, rc, ftz)
    return r, flags | fl


def isqrt(n):
    if n < 2:
        return n
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def rne_sqrt_value(q, p):
    """RNE (unbounded exponent) of sqrt(q) to p significant bits, q > 0 rational: exact value"""
    e = floor_log2(q)
    E = e // 2
    K = p + 24 - E
    T = q * Fraction(4) ** K
    s = isqrt(T.numerator // T.denominator)
    exact = T.denominator == 1 and s * s == T.numerator
    shift = s.bit_length() - p
    c = s >> shift
    rem = s - (c << shift)
    half = 1 << (shift - 1)
    if rem > half or (rem == half and (not exact or c & 1)):
        c += 1
    return Fraction(c) * Fraction(2) ** (shift - K), not (exact and rem == 0)


def fp_sqrt(a, f, mxcsr, rc=RNE):
    """IEEE sqrt, RNE only (all callers): sqrt(-0) = -0, negative -> indefinite"""
    daz = bool(mxcsr & DAZ)
    ca, sa, va = classify(a, f)
    if ca in NAN:
        return quiet(a, f), (IE if ca == "snan" else 0)
    flags = 0
    if ca == "denorm":
        if daz:
            return (f.signbit if sa else 0), 0
        flags |= DE
    if ca == "zero":
        return a, flags
    if sa:
        return f.qnan_indef, IE
    if ca == "inf":
        return a, flags
    r, inexact = rne_sqrt_value(va, f.p)
    bits, fl = encode_value(r, f, RNE, False)
    return bits, flags | (PE if inexact else 0)


def fp_fma(x, y, z, f, mxcsr, rc, negp, negz, lit_neg_x=False):
    """r = (+-)(x*y) (+-) z with one rounding (SDM Vol1 14.5.2, Table 14-17): NaN priority x, y,
    z (quietened); IE for an SNaN, inf*0, inf-inf. lit_neg_x: spec 7.5.3 line 19 'a := -a'
    applied literally to a NaN x of the negated forms (sign of the propagated NaN flipped)."""
    daz, ftz = bool(mxcsr & DAZ), bool(mxcsr & FTZ)
    cl = [classify(t, f) for t in (x, y, z)]
    flags = IE if any(c[0] == "snan" for c in cl) else 0
    for i, t in enumerate((x, y, z)):
        if cl[i][0] in NAN:
            r = quiet(t, f)
            if lit_neg_x and negp and i == 0:
                r ^= f.signbit
            return r, flags
    (cx, sx, vx), (cy, sy, vy), (cz, sz, vz) = cl
    if daz:
        if cx == "denorm":
            cx, vx = "zero", Fraction(0)
        if cy == "denorm":
            cy, vy = "zero", Fraction(0)
        if cz == "denorm":
            cz, vz = "zero", Fraction(0)
    elif "denorm" in (cx, cy, cz):
        flags |= DE
    sp = sx ^ sy ^ (1 if negp else 0)
    sz2 = sz ^ (1 if negz else 0)
    if (cx == "inf" and cy == "zero") or (cx == "zero" and cy == "inf"):
        return f.qnan_indef, flags | IE
    if cx == "inf" or cy == "inf":
        if cz == "inf" and sz2 != sp:
            return f.qnan_indef, flags | IE
        return (f.signbit if sp else 0) | f.inf, flags
    if cz == "inf":
        return (f.signbit if sz2 else 0) | f.inf, flags
    P = vx * vy
    if negp:
        P = -P
    C = -vz if negz else vz
    S = P + C
    if S == 0:
        if (cx == "zero" or cy == "zero") and cz == "zero" and sp == sz2:
            return (f.signbit if sp else 0), flags
        return (f.signbit if rc == RD else 0), flags
    r, fl = encode_value(S, f, rc, ftz)
    return r, flags | fl


def order_key(c, s, v):
    if c == "inf":
        return (-1, 0) if s else (1, 0)
    return (0, v)


def round_half_even(q):
    fl = q.numerator // q.denominator
    rem = q - fl
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl & 1):
        fl += 1
    return fl


def round_int(v, rc):
    fl = v.numerator // v.denominator
    rem = v - fl
    if rem == 0:
        return fl
    if rc == RNE:
        return round_half_even(v)
    if rc == RD:
        return fl
    if rc == RU:
        return fl + 1
    return fl + 1 if v < 0 else fl


# ---------------------------------------------------------------------------------------
# A. BF16 (spec ch.7). Every op: DAZ inputs, RNE, FTZ outputs, no flags, MXCSR ignored.
#    Lane functions return (result bits, flags, ambiguous-operand-index or None).
# ---------------------------------------------------------------------------------------
BF_MX = MXCSR_DEFAULT | DAZ | FTZ    # what every BF16 arithmetic op behaves as (spec 7.1.2 etc.)


def bf_arith(op, a, b):
    """VADD/VSUB/VMUL/VDIVBF16: spec 7.1.2 / 7.18.2 / 7.11.2 / 7.4.2 'DAZ, FTZ, RNE, SAE'"""
    if op == "div":
        return fp_div(a, b, BF16, BF_MX, RNE)[0]
    return fp_binop(op, a, b, BF16, BF_MX, RNE)[0]


def bf_sqrt(a):
    """VSQRTBF16 spec 7.17.2: IEEE sqrt, DAZ, FTZ, RNE"""
    return fp_sqrt(a, BF16, BF_MX, RNE)[0]


def bf_fma(order, negp, negz, d, s2, s3, literal=True):
    """spec 7.5.3: 132 a=DEST b=SRC3 c=SRC2 / 213 a=SRC2 b=DEST c=SRC3 / 231 a=SRC2 b=SRC3 c=DEST"""
    if order == 132:
        a, b, c, ai = d, s3, s2, 0
    elif order == 213:
        a, b, c, ai = s2, d, s3, 1
    else:
        a, b, c, ai = s2, s3, d, 1
    lit = fp_fma(a, b, c, BF16, BF_MX, RNE, negp, negz, lit_neg_x=True)[0]
    nat = fp_fma(a, b, c, BF16, BF_MX, RNE, negp, negz, lit_neg_x=False)[0]
    # AMBIGUOUS: spec 7.5.3 line 19 'IF *negative form*: a := -a' negates a NaN a as well
    # (sign of the propagated NaN flipped); SDM Vol1 Table 14-17 returns Q(x) unnegated for
    # -(x*y)+z. Literal pseudocode chosen (flipped); such inputs only in AMBIGUOUS-13.
    return (lit if literal else nat), (ai if lit != nat else None)


def bf_maxmin(a, b, is_max):
    """VMAXBF16/VMINBF16 spec 7.9.3 / 7.10.3 MAX()/MIN() after DAZ"""
    a, b = daz_bits(a, BF16), daz_bits(b, BF16)
    ca, sa, va = classify(a, BF16)
    cb, sb, vb = classify(b, BF16)
    if ca == "zero" and cb == "zero":
        return b
    if ca in NAN or cb in NAN:
        return b
    ka, kb = order_key(ca, sa, va), order_key(cb, sb, vb)
    if is_max:
        return a if ka > kb else b
    return a if ka < kb else b


def bf_cmp_pred(a, b, pred):
    """VCMPBF16 spec 7.2.3 (DAZ, no exceptions): predicate imm8[4:0]; S/Q irrelevant"""
    a, b = daz_bits(a, BF16), daz_bits(b, BF16)
    ca, sa, va = classify(a, BF16)
    cb, sb, vb = classify(b, BF16)
    unord = ca in NAN or cb in NAN
    if unord:
        lt = eq = gt = False
    else:
        ka, kb = order_key(ca, sa, va), order_key(cb, sb, vb)
        lt, eq, gt = ka < kb, ka == kb, ka > kb
    return {0: eq, 1: lt, 2: lt or eq, 3: unord, 4: unord or not eq, 5: unord or not lt,
            6: unord or not (lt or eq), 7: not unord, 8: unord or eq, 9: unord or lt,
            10: unord or lt or eq, 11: False, 12: (not unord) and not eq, 13: gt or eq,
            14: gt, 15: True}[pred & 15]


def bf_fpclass(x, imm):
    """check_fp_class_bf16, spec 7.6.3 (rev 7: imm8[5] guard; denormal is always 0)"""
    negative = x >> 15
    e = (x >> 7) & 0xFF
    ones, zeros = e == 0xFF, e == 0
    mzero = True if zeros else (x & 0x7F) == 0
    zero = zeros and mzero
    sig = (x >> 6) & 1
    snan = ones and not mzero and not sig
    qnan = ones and not mzero and sig
    pz = not negative and zeros and mzero
    nz = negative and zeros and mzero
    pinf = not negative and ones and mzero
    ninf = negative and ones and mzero
    denormal = zeros and not mzero
    fneg = negative and not ones and not zero
    bits = [qnan, pz, nz, pinf, ninf, denormal, fneg, snan]
    return any((imm >> i) & 1 and bits[i] for i in range(8))


def bf_comis(a, b):
    """VCOMISBF16 spec 7.3.3 -> EFLAGS bits (ZF PF CF; OF AF SF := 0)"""
    a, b = daz_bits(a, BF16), daz_bits(b, BF16)
    ca, sa, va = classify(a, BF16)
    cb, sb, vb = classify(b, BF16)
    if ca in NAN or cb in NAN:
        return 0x45
    ka, kb = order_key(ca, sa, va), order_key(cb, sb, vb)
    if ka > kb:
        return 0
    if ka < kb:
        return 0x01
    return 0x40


def int_to_bf16(e):
    return 0 if e == 0 else encode_value(Fraction(e), BF16, RNE, False)[0]


def bf_getexp(x, literal=True):
    """getexp_bf16 spec 7.7.3"""
    c, s, v = classify(x, BF16)
    if c in NAN:
        return quiet(x, BF16)
    if c == "inf" and not s:
        return BF16.inf
    if c in ("denorm", "zero"):
        return BF16.signbit | BF16.inf
    if c == "inf" and not literal:
        return BF16.inf
    # AMBIGUOUS: spec 7.7.3 handles only +Inf; -Inf falls through to line 9-11 and returns
    # (0xFF - 127) = 128.0 (4300h); description 'floor(log2(|x|))' and SDM VGETEXPPH Table 5-14
    # give +Inf. Literal pseudocode chosen; tested only in AMBIGUOUS-08.
    return int_to_bf16(((x & 0x7F80) >> 7) - 127)


def bf_getmant(x, imm, literal=True):
    """getmant_bf16 spec 7.8.3 (sign_control = imm8[3:2], interval = imm8[1:0])"""
    if not literal:
        x = daz_bits(x, BF16)
    sc, interv = (imm >> 2) & 3, imm & 3
    c, s, v = classify(x, BF16)
    sign = 0 if sc & 1 else s
    signed_one = BF16.one if sc & 1 else (BF16.signbit | BF16.one)
    if c in NAN:
        return quiet(x, BF16)
    if not s and c in ("zero", "inf"):
        return BF16.one
    if s:
        if c == "zero":
            return signed_one
        if c == "inf":
            return BF16.qnan_indef if sc & 2 else signed_one
        if sc & 2:
            return BF16.qnan_indef
    frac = x & 0x7F
    exp = (x >> 7) & 0xFF
    if c == "denorm":
        # AMBIGUOUS: spec 7.8.3 lines 22-23 keep a denormal on the computation path with
        # fraction := 0 (exponent field 0 -> unbiased -127, odd), whereas the description's
        # DAZ would make it a zero (-> 1.0 / signed_one). Literal pseudocode chosen;
        # tested only in AMBIGUOUS-09.
        frac = 0
    odd = (exp - 127) & 1
    sigbit = (frac >> 6) & 1
    nexp = [127, 126 if odd else 127, 126, 126 if sigbit else 127][interv]
    return (BF16.signbit if sign else 0) | (nexp << 7) | frac


def bf_reduce(x, imm, literal=True):
    """reduce_bf16_ne spec 7.13.3: m = imm8[7:4], ROUND is RNE (imm8[3:0] not used)"""
    c, s, v = classify(x, BF16)
    if c in NAN:
        return quiet(x, BF16)
    x = daz_bits(x, BF16)
    c, s, v = classify(x, BF16)
    if c == "inf":
        # AMBIGUOUS: spec 7.13.3 computes src - 2^-m*ROUND(2^m*src) = Inf - Inf -> QNaN
        # indefinite; SDM VREDUCEPH Table 5-28 returns +0.0 for +-Inf. Literal chosen
        # (FFC0h); tested only in AMBIGUOUS-10.
        return BF16.qnan_indef if literal else 0
    if c == "zero":
        return 0                                     # (+-0) - (+-0) = +0 (RNE)
    m = imm >> 4
    t = Fraction(round_half_even(v * 2 ** m)) / 2 ** m
    d = v - t
    if d == 0:
        return 0
    return encode_value(d, BF16, RNE, True)[0]


def bf_rndscale(x, imm):
    """round_bf16_to_integer_ne spec 7.14.3 (RNE always; sign of zero preserved, 7.14.2)"""
    c, s, v = classify(x, BF16)
    if c in NAN:
        return quiet(x, BF16)
    if c == "denorm":
        return x & BF16.signbit
    if c in ("zero", "inf"):
        return x
    m = imm >> 4
    r = Fraction(round_half_even(v * 2 ** m)) / 2 ** m
    if r == 0:
        return x & BF16.signbit
    return encode_value(r, BF16, RNE, True)[0]


def bf_scalef(a, b):
    """scale_bf16 spec 7.16.3: DAZ(src1), DAZ(src2), src1 * 2^floor(src2), FTZ, RNE;
    NaN: SRC1 first (Vol1 Table 4-7), 0*2^+Inf and Inf*2^-Inf invalid -> indefinite"""
    ca, sa, va = classify(a, BF16)
    cb, sb, vb = classify(b, BF16)
    if ca in NAN:
        return quiet(a, BF16)
    if cb in NAN:
        return quiet(b, BF16)
    a, b = daz_bits(a, BF16), daz_bits(b, BF16)
    ca, sa, va = classify(a, BF16)
    cb, sb, vb = classify(b, BF16)
    sgn = BF16.signbit if sa else 0
    if cb == "inf":
        if not sb:
            return BF16.qnan_indef if ca == "zero" else sgn | BF16.inf
        return BF16.qnan_indef if ca == "inf" else sgn
    if ca in ("inf", "zero"):
        return a
    n = vb.numerator // vb.denominator
    n = max(-400, min(400, n))
    return encode_value(va * Fraction(2) ** n, BF16, RNE, True)[0]


def bf_rcp(x, literal=True):
    """VRCPBF16 (spec 7.12): MODEL CHOICE correctly rounded RNE 1/x, DAZ, FTZ, Table 7.2"""
    c, s, v = classify(x, BF16)
    if c in NAN:
        return quiet(x, BF16)
    sgn = BF16.signbit if s else 0
    if c in ("zero", "denorm"):
        return sgn | BF16.inf
    if c == "inf":
        return sgn
    # AMBIGUOUS: 1/x is tiny for |x| > 2^126; pseudocode 7.12.3 says FTZ (-> signed 0), Table 7.2
    # row 'X = 2^-n -> 2^n' would give the denormal 2^-127 for X = 2^127. FTZ chosen;
    # tested only in AMBIGUOUS-12.
    return encode_value(1 / v, BF16, RNE, literal)[0]


def bf_rsqrt(x):
    """VRSQRTBF16 (spec 7.15): MODEL CHOICE correctly rounded RNE 1/sqrt(x), Table 7.3"""
    c, s, v = classify(x, BF16)
    if c in NAN:
        return quiet(x, BF16)
    if c in ("zero", "denorm"):
        return (BF16.signbit if s else 0) | BF16.inf
    if s:
        return BF16.qnan_indef
    if c == "inf":
        return 0
    r, _ = rne_sqrt_value(1 / v, BF16.p)
    return encode_value(r, BF16, RNE, True)[0]


# ---------------------------------------------------------------------------------------
# C. MINMAX (spec 5.2 Figures 5.1-5.9)
# ---------------------------------------------------------------------------------------
def _mm_val(x, f):
    c, s, v = classify(x, f)
    return order_key(c, s, v)


def _mm_abs(x, f):
    return _mm_val(x & ~f.signbit, f)


def _opp_zeros(a, b, f):
    return (a == 0 and b == f.signbit) or (a == f.signbit and b == 0)


def mm_minimum(a, b, f, is_max=False):
    """Figure 5.1 minimum / Figure 5.5 maximum"""
    ca, cb = classify(a, f)[0], classify(b, f)[0]
    if ca == "snan" or (ca == "qnan" and cb != "snan"):
        return quiet(a, f)
    if cb in NAN:
        return quiet(b, f)
    if _opp_zeros(a, b, f):
        return 0 if is_max else f.signbit
    if is_max:
        return a if _mm_val(a, f) >= _mm_val(b, f) else b
    return a if _mm_val(a, f) <= _mm_val(b, f) else b


def mm_number(a, b, f, is_max=False):
    """Figure 5.2 minimumNumber / Figure 5.6 maximumNumber"""
    ca, cb = classify(a, f)[0], classify(b, f)[0]
    if ca in NAN and cb in NAN:
        if ca == "snan" or (ca == "qnan" and cb == "qnan"):
            return quiet(a, f)
        return quiet(b, f)
    if ca in NAN:
        return b
    if cb in NAN:
        return a
    if _opp_zeros(a, b, f):
        return 0 if is_max else f.signbit
    if is_max:
        return a if _mm_val(a, f) >= _mm_val(b, f) else b
    return a if _mm_val(a, f) <= _mm_val(b, f) else b


def mm_magnitude(a, b, f, is_max=False, number=False):
    """Figures 5.3 / 5.4 / 5.7 / 5.8"""
    ca, cb = classify(a, f)[0], classify(b, f)[0]
    if number:
        if ca in NAN or cb in NAN:
            return mm_number(a, b, f, is_max)
    else:
        if ca == "snan" or (ca == "qnan" and cb != "snan"):
            return quiet(a, f)
        if cb in NAN:
            return quiet(b, f)
    ma, mb = _mm_abs(a, f), _mm_abs(b, f)
    if (ma > mb) if is_max else (ma < mb):
        return a
    if (mb > ma) if is_max else (mb < ma):
        return b
    return mm_number(a, b, f, is_max) if number else mm_minimum(a, b, f, is_max)


def minmax(a, b, imm, f, daz, exc):
    """Figure 5.9 minmax(a, b, imm, daz, except) -> (bits, flags, ambiguous-index)"""
    op, sc, nanp = imm & 3, (imm >> 2) & 3, (imm >> 4) & 1
    if daz:
        a, b = daz_bits(a, f), daz_bits(b, f)
    ca, cb = classify(a, f)[0], classify(b, f)[0]
    flags = 0
    if exc:
        if ca == "snan" or cb == "snan":
            flags |= IE
        elif ca == "qnan" or cb == "qnan":
            pass
        elif ca == "denorm" or cb == "denorm":
            flags |= DE
    is_max = bool(op & 1)
    if op < 2:
        tmp = mm_number(a, b, f, is_max) if nanp else mm_minimum(a, b, f, is_max)
    else:
        tmp = mm_magnitude(a, b, f, is_max, bool(nanp))
    if not is_nan(tmp, f):
        if sc == 3:
            tmp |= f.signbit
        elif sc == 2:
            tmp &= ~f.signbit
        elif sc == 1 or ca in NAN:
            pass
        else:
            tmp = (tmp & ~f.signbit) | (a & f.signbit)
    amb = None
    # AMBIGUOUS: spec 11.2.2 footnote 2 (L13283) '#IE if both are NaN, and either one is sNaN'
    # for the *Number operations contradicts Table 11.4 (sNaN1/Norm2 -> IE Yes), the text
    # 'raises #IE if either of the operands is an SNAN' and Figure 5.9 lines 9-11. Pseudocode
    # chosen (IE for any SNaN); SNaN + non-NaN with imm8[4] = 1 only in AMBIGUOUS-17.
    if exc and nanp and ((ca == "snan") != (cb == "snan")) and not (ca in NAN and cb in NAN):
        amb = 0 if ca == "snan" else 1
    return tmp, flags, amb


# ---------------------------------------------------------------------------------------
# B. saturating conversions (spec 5.3 helpers, executed literally) + a 'natural' reading
#    (IE for NaN/Inf/out of range after rounding, PE for inexact in-range) used only to mark
#    the inputs where the two differ as ambiguous.
# ---------------------------------------------------------------------------------------
P2 = lambda e: Fraction(2) ** e     # noqa: E731
HALF = Fraction(1, 2)


def ood_sbyte_rc(v, rc):
    """helpers 5.3 convert_fp16/fp32_to_signed_byte_saturate, EXP = 128 (L4813/L5277)"""
    if rc == RU:
        return v <= -129 or v > 127
    if rc == RD:
        return v < -128 or v >= 128
    if rc == RZ:
        return v <= -129 or v >= 128
    return v < -(128 + HALF) or v >= 128 - HALF


def ood_ubyte_rc(v, rc):
    """helper 5.3 convert_fp16_to_unsigned_byte_saturate, EXP = 256 (L4888)"""
    if rc == RU:
        return v <= -1 or v > 255
    if rc == RD:
        return v < 0 or v >= 256
    if rc == RZ:
        return v <= -1 or v >= 256
    return v < -HALF or v >= 256 - HALF


# helper key -> (signed, width, ood(v, rc), hi(v), lo(v), PE-in-saturation-branch, extra PE rule)
def _rng(lo, hi):
    return lambda v, rc: v <= lo or v >= hi


HELPERS = {
    # bytes (spec L4806-L5370)
    "B_S_R": (True, 8, ood_sbyte_rc, lambda v: v >= 127, lambda v: v <= -128, False, False),
    "B_S_T32": (True, 8, _rng(-129, 128), lambda v: v >= 127, lambda v: v <= -128, False, False),
    # AMBIGUOUS: convert_fp16_to_signed_byte_truncate_saturate (spec L4865-L4870) has '// PE=1'
    # in both saturation branches. The same helpers write the IE action as a comment ('// IE=1'),
    # so the comment is read as the action: PE is set for every saturated result (also 127.0,
    # -128.0, +-Inf). Only in AMBIGUOUS-02.
    "B_S_T16": (True, 8, _rng(-129, 128), lambda v: v >= 127, lambda v: v <= -128, True, False),
    # AMBIGUOUS: convert_fp32_to_unsigned_byte_saturate (VCVTPS2IUBS, spec L5297-L5335) has an
    # OutOfDestRepresentation that ignores MXCSR.RC (v <= -1 or v >= 256) and saturation
    # branches 'v > 255' / 'v < 0' (no PE there), unlike the FP16 helper (L4888). Literal chosen.
    "B_U_R32": (False, 8, _rng(-1, 256), lambda v: v > 255, lambda v: v < 0, False, False),
    "B_U_T32": (False, 8, _rng(-1, 256), lambda v: v > 255, lambda v: v < 0, False, False),
    "B_U_R16": (False, 8, ood_ubyte_rc, lambda v: v >= 255, lambda v: v <= 0, False, False),
    # (spec L4954 compares 'src.bf16 <= 0' in the FP16 helper: read as src.fp16 -- typo)
    "B_U_T16": (False, 8, _rng(-1, 256), lambda v: v >= 255, lambda v: v <= 0, False, False),
    # dwords / qwords (spec L5372-L6050)
    "DW_S": (True, 32, _rng(-(P2(31) + 1), P2(31)), lambda v: v >= P2(31) - 1, lambda v: v <= -P2(31), False, False),
    "DW_U": (False, 32, _rng(-1, P2(32)), lambda v: v >= P2(32) - 1, lambda v: v <= 0, False, False),
    # AMBIGUOUS: convert_DP_to_DW_UnSignedInteger_TruncateSaturate (VCVTTPD2UDQS, VCVTTSD2USIS
    # r32; spec L5741-L5743) alone has 'IF (src < 0) || (src > EXP - 1): Signal PE=1', i.e. PE
    # also together with IE (-1.0, 2^32, +-Inf). Literal chosen; only in AMBIGUOUS-03.
    "DW_U_PD": (False, 32, _rng(-1, P2(32)), lambda v: v >= P2(32) - 1, lambda v: v <= 0, False, True),
    # AMBIGUOUS (typo, intent chosen): convert_SP_to_QW_SignedInteger_TruncateSaturate (spec
    # L5577-L5580) saturates at '+2^31 - 1' / '-2^31' (copied from the DW helper) although
    # OutOfDestRepresentation uses EXP = 2^63; literal would return 7FFF..FFh for 3.0e9 without
    # IE. The QW bounds 2^63 - 1 / -2^63 are used.
    # AMBIGUOUS (typo, intent chosen): convert_DP_to_QW_SignedInteger_TruncateSaturate (spec
    # L5839) 'ELSE IF (src.fp64 <= EXP)' would make every value INT64_MIN; '<= -EXP' used.
    "QW_S": (True, 64, _rng(-(P2(63) + 1), P2(63)), lambda v: v >= P2(63) - 1, lambda v: v <= -P2(63), False, False),
    "QW_U": (False, 64, _rng(-1, P2(64)), lambda v: v >= P2(64) - 1, lambda v: v <= 0, False, False),
    # AMBIGUOUS (typo, intent chosen): convert_DP_to_QW_UnSignedInteger_TruncateSaturate (spec
    # L5890) leaves W undefined in 'EXP = 2^(W)'; W = 64 used. Its saturation test is
    # 'src >= EXP' (L5904): identical for FP64 inputs.
    "QW_U_PD": (False, 64, _rng(-1, P2(64)), lambda v: v >= P2(64), lambda v: v <= 0, False, False),
}

# BF16 -> byte helpers (spec L4410-L4560): no flags, RNE or RTZ, '> 127' / '< -128' / '> 255' / '< 0'
BF_HELPERS = {
    "BF_S_R": (True, lambda v: v > 127, lambda v: v < -128, RNE),
    "BF_S_T": (True, lambda v: v > 127, lambda v: v < -128, RZ),
    "BF_U_R": (False, lambda v: v > 255, lambda v: v < 0, RNE),
    "BF_U_T": (False, lambda v: v > 255, lambda v: v < 0, RZ),
}


def cvt_literal(key, x, f, rc):
    """literal execution of a spec 5.3 helper: (integer result masked to width, flags)"""
    signed, wbits, ood, hi, lo, pe_sat, extra = HELPERS[key]
    mask = (1 << wbits) - 1
    hiv = (1 << (wbits - 1)) - 1 if signed else mask
    lov = (1 << (wbits - 1)) if signed else 0
    c, s, v = classify(x, f)
    nan, inf = c in NAN, c == "inf"
    flags = 0
    o = (not nan and not inf) and ood(v, rc)
    if nan or inf or o:
        flags |= IE
    if extra and not nan and (inf or v < 0 or v > P2(32) - 1):
        flags |= PE                                  # spec L5741-L5743 (DW_U_PD only)
    if nan:
        return 0, flags
    if (inf and not s) or (not inf and hi(v)):
        return hiv, flags | (PE if pe_sat else 0)
    if (inf and s) or (not inf and lo(v)):
        return lov, flags | (PE if pe_sat else 0)
    r = round_int(v, rc)
    if r != v and not o:
        flags |= PE
    return r & mask, flags


def cvt_natural(signed, wbits, x, f, rc):
    mask = (1 << wbits) - 1
    lo, hi = (-(1 << (wbits - 1)), (1 << (wbits - 1)) - 1) if signed else (0, mask)
    c, s, v = classify(x, f)
    if c in NAN:
        return 0, IE
    if c == "inf":
        return ((lo if s else hi) & mask), IE
    r = round_int(v, rc)
    if r < lo:
        return lo & mask, IE
    if r > hi:
        return hi & mask, IE
    return r & mask, (PE if r != v else 0)


def bf_cvt_literal(key, x):
    signed, hi, lo, rc = BF_HELPERS[key]
    c, s, v = classify(x, BF16)
    if c in NAN:
        return 0
    if (c == "inf" and not s) or (c != "inf" and hi(v)):
        return 0x7F if signed else 0xFF
    if (c == "inf" and s) or (c != "inf" and lo(v)):
        return 0x80 if signed else 0
    return round_int(v, rc) & 0xFF


def bf_cvt_natural(key, x):
    signed, _, _, rc = BF_HELPERS[key]
    x = daz_bits(x, BF16)
    return cvt_natural(signed, 8, x, BF16, rc)[0]


def cvt_lane(key, x, f, rc, mxcsr):
    """one conversion lane: (result, flags, ambiguous-index or None)"""
    amb = False
    if f in (F32, F64) and classify(x, f)[0] == "denorm" and mxcsr & DAZ:
        # AMBIGUOUS: the 5.3 helpers never mention DAZ; SDM Vol1 10.2.3.4 makes DAZ apply to
        # every SIMD FP source operand. DAZ applied (denormal -> 0, no PE); AMBIGUOUS-07 only.
        x = x & f.signbit
        amb = True
    r1, f1 = cvt_literal(key, x, f, rc)
    signed, wbits = HELPERS[key][0], HELPERS[key][1]
    r2, f2 = cvt_natural(signed, wbits, x, f, rc)
    if (r1, f1) != (r2, f2):
        amb = True
    return r1, f1, (0 if amb else None)


# ---------------------------------------------------------------------------------------
# D. VCOMX (spec ch.8)
# ---------------------------------------------------------------------------------------
CLR = 0x8D5                     # OF SF ZF AF PF CF
COMX_FL = {"unord": 0x885, "gt": 0x000, "lt": 0x801, "eq": 0x8C0}


def comx(a, b, f, mxcsr, signal_qnan, daz_applies):
    """-> (relation, mxcsr flags)"""
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    if ca in NAN or cb in NAN:
        if ca == "snan" or cb == "snan" or signal_qnan:
            return "unord", IE
        return "unord", 0
    flags = 0
    if daz_applies and mxcsr & DAZ:
        if ca == "denorm":
            ca, va = "zero", Fraction(0)
        if cb == "denorm":
            cb, vb = "zero", Fraction(0)
    elif ca == "denorm" or cb == "denorm":
        flags |= DE
    ka, kb = order_key(ca, sa, va), order_key(cb, sb, vb)
    return ("gt" if ka > kb else "lt" if ka < kb else "eq"), flags


# ---------------------------------------------------------------------------------------
# test case container (same output format as ref_evex_m1.py)
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
        ins = ["zmm%d=%s" % (r, hexs(self.zmm[r])) for r in sorted(self.zmm)]
        ins += ["k%d=0x%X" % (r, self.k[r]) for r in sorted(self.k)]
        if self.mxcsr is not None:
            ins.append("mxcsr=0x%X" % self.mxcsr)
        ins += ["m+0x%X=%s" % (off, hexs(self.mem[off])) for off in sorted(self.mem)]
        ins += self.inp
        exp = list(self.exp)
        if self.fault:
            exp.append(self.fault)
        return "%s | %s => %s" % (byte_list(self.code), " ".join(ins), " ".join(exp))


RNG = random.Random(0xA10_2A)
cases = []
VL_LL = {16: 0, 32: 1, 64: 2}


def emit(c):
    cases.append(c)


def comment(text):
    cases.append("# " + text)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


# ---------------------------------------------------------------------------------------
# value generators
# ---------------------------------------------------------------------------------------
def rnd_fp(f, elo=None, ehi=None):
    """random normal value, exponent field mostly near the bias"""
    s = RNG.getrandbits(1)
    emaxf = (1 << f.ebits) - 2
    if elo is None:
        span = {8: 18, 5: 6, 11: 30}[f.ebits]
        e = RNG.choice([RNG.randint(f.bias - span, f.bias + span), RNG.randint(1, emaxf),
                        RNG.randint(f.bias - 3, f.bias + 3)])
    else:
        e = RNG.randint(max(1, f.bias + elo), min(emaxf, f.bias + ehi))
    return (s << (f.bits - 1)) | (e << f.fbits) | RNG.getrandbits(f.fbits)


BF16_SP = [0x0000, 0x8000, 0x3F80, 0xBF80, 0x7F80, 0xFF80, 0x7FC0, 0xFFC1, 0x7F81, 0xFFA0,
           0x0001, 0x807F, 0x0080, 0x8080, 0x7F7F, 0xFF7F, 0x4049, 0x3EAB, 0x4000, 0xC000,
           0x3F00, 0x0081, 0x4300, 0xC2FF, 0x3FC0, 0xBFC0, 0x0040, 0x00FF]
FP16_SP = [0x0000, 0x8000, 0x3C00, 0xBC00, 0x7C00, 0xFC00, 0x7E00, 0xFE01, 0x7C01, 0xFD00,
           0x0001, 0x83FF, 0x0400, 0x8400, 0x7BFF, 0xFBFF, 0x4000, 0xC000, 0x3555, 0x4248]
F32_SP = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000, 0x7FC00000,
          0xFFC00001, 0x7FA00000, 0xFF800001, 0x00000001, 0x80400000, 0x7F7FFFFF, 0x00800000,
          0x40000000, 0xC0000000, 0x3EAAAAAB, 0x80800000]
F64_SP = [0x0000000000000000, 0x8000000000000000, 0x3FF0000000000000, 0xBFF0000000000000,
          0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000, 0xFFF8000000000001,
          0x7FF4000000000000, 0xFFF0000000000001, 0x0000000000000001, 0x8008000000000000,
          0x7FEFFFFFFFFFFFFF, 0x0010000000000000, 0x4000000000000000, 0xC000000000000000]
SPECIALS = {BF16: BF16_SP, FP16: FP16_SP, F32: F32_SP, F64: F64_SP}


def rot(lst, start, n, step=1):
    return [lst[(start + i * step) % len(lst)] for i in range(n)]


# ---------------------------------------------------------------------------------------
# generic vector case: layouts rvm, rm, fma, krvm, krm
#   spec dict: name mmm pp w opc lay sesz desz fn(srcs, ctx) -> (res, flags, amb)
#              mx: 'none' (E4: no flags) | 'mxcsr'; gen(): random source element; safe
# ---------------------------------------------------------------------------------------
def vcase(sp, vl, variant, dst=1, s1=2, s2=3, kreg=0, kval=None, z=0, a=None, b=None, d=None,
          mxcsr=MXCSR_DEFAULT, er=None, sae=False, imm=None, disp=None, title="", allow_amb=False,
          ll=None):
    lay, sesz, desz = sp["lay"], sp["sesz"], sp["desz"]
    KL = vl // max(sesz, desz)
    sbytes, dbytes = KL * sesz, KL * desz
    mem = variant in ("mem", "bcst")
    bcst = variant == "bcst"
    if imm is None and sp.get("imm"):
        imm = sp["imm_gen"]() if "imm_gen" in sp else RNG.getrandbits(8)
    rc = er if er is not None else (mxcsr >> 13) & 3
    ctx = dict(imm=imm, mxcsr=mxcsr, rc=rc)
    gen = sp["gen"]
    two = lay in ("rvm", "krvm", "fma")
    a = list(a) if a is not None else ([gen() for _ in range(KL)] if two else [])
    b = list(b) if b is not None else [gen() for _ in range(1 if bcst else KL)]
    if bcst:
        b = b[:1]
    d = list(d) if d is not None else ([sp.get("dgen", gen)() for _ in range(KL)] if lay == "fma" else [])

    def lane(j):
        bj = b[0] if bcst else b[j]
        if lay in ("rvm", "krvm"):
            return (a[j], bj)
        if lay == "fma":
            return (d[j], a[j], bj)
        return (bj,)

    def sanitize():
        for _ in range(4):
            dirty = False
            for j in range(KL):
                r = sp["fn"](lane(j), ctx)
                if r[2] is None:
                    continue
                dirty = True
                ops = {"rvm": [a, b], "krvm": [a, b], "fma": [d, a, b], "rm": [b], "krm": [b]}[lay]
                arr = ops[r[2]]
                if arr is b and bcst:
                    b[0] = sp["safe"]
                else:
                    arr[j] = sp["safe"]
            if not dirty:
                return
        raise RuntimeError("cannot sanitize " + sp["name"])

    if not allow_amb:
        sanitize()
    regs = {}
    if lay not in ("krvm", "krm"):
        regs[dst] = (pack(d, sesz) + rnd_bytes(64 - KL * sesz)) if lay == "fma" else rnd_bytes(64)
    if two:
        regs[s1] = pack(a, sesz) + rnd_bytes(64 - sbytes)
    if not mem:
        regs[s2] = pack(b, sesz) + rnd_bytes(64 - sbytes)
    # read back (register overlaps)
    if two:
        a = elems(regs[s1][:sbytes], sesz)
    if not mem:
        b = elems(regs[s2][:sbytes], sesz)
    if lay == "fma":
        d = elems(regs[dst][:sbytes], sesz)
    if not allow_amb:
        for j in range(KL):
            if sp["fn"](lane(j), ctx)[2] is not None:
                raise RuntimeError("ambiguous lane after overlap in " + sp["name"])
    c = Case("%s VL%d %s%s%s%s" % (sp["name"], vl * 8, variant, " {k%d}" % kreg if kreg else "",
                                   "{z}" if z else "", (" " + title) if title else ""))
    for r, img in regs.items():
        c.zmm[r] = img
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    nn = sesz if bcst else sbytes
    if mem:
        if disp is None:
            disp = nn * RNG.choice([0, 1, -1, 2, 3, -2])
        mem_data = pack(b, sesz)
        c.mem[MEM_RSI + disp] = mem_data
        rm = Mem(RSI, disp)
    else:
        rm = s2
    kmask = kval if kreg else None
    res, flags = [], 0
    for j in range(KL):
        if kmask is not None and not (kmask >> j) & 1:
            res.append(None)
            continue
        r, fl, _ = sp["fn"](lane(j), ctx)
        res.append(r)
        flags |= fl
    fault = None
    if sp["mx"] == "mxcsr":
        if sae or er is not None:
            flags = 0
        unm = flags & ~(mxcsr >> 7) & 0x3F
        if unm:
            if unm & (IE | DE | ZE):
                flags &= ~(OE | UE | PE)
            fault = "#XM"
    else:
        flags = 0
    if lay in ("krvm", "krm"):
        kv = 0
        for j in range(KL):
            if res[j]:
                kv |= 1 << j
        c.k.setdefault(dst, RNG.getrandbits(64))
        c.exp.append("k%d=0x%X" % (dst, kv))
    elif not fault:
        o = elems(regs[dst][:dbytes], desz)
        out = [(0 if z else o[j]) if res[j] is None else res[j] for j in range(KL)]
        c.exp.append("zmm%d=%s" % (dst, hexs(pack(out, desz) + bytes(64 - dbytes))))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    c.fault = fault
    if ll is None:
        ll = er if er is not None else (2 if sae else VL_LL[vl])
    bb = 1 if (bcst or er is not None or sae) else 0
    if two:
        c.code = evex(sp["mmm"], sp["pp"], sp["w"], sp["opc"], dst, rm, vvvv=s1, ll=ll, b=bb, z=z,
                      aaa=kreg, imm=imm if sp.get("imm") else None, n=nn)
    else:
        c.code = evex(sp["mmm"], sp["pp"], sp["w"], sp["opc"], dst, rm, ll=ll, b=bb, z=z, aaa=kreg,
                      imm=imm if sp.get("imm") else None, n=nn)
    return c


def std_variants(sp, extra_title=""):
    """the 4 standard cases per VL: reg nomask / reg merge zmm16+ / mem zero / {1toN} merge"""
    for vl in (16, 32, 64):
        emit(vcase(sp, vl, "reg", dst=1, s1=2, s2=3, title=extra_title))
        emit(vcase(sp, vl, "reg", dst=17, s1=30, s2=9, kreg=3, kval=RNG.getrandbits(64), title=extra_title))
        emit(vcase(sp, vl, "mem", dst=4, s1=5, kreg=7, kval=RNG.getrandbits(64), z=1, title=extra_title))
        emit(vcase(sp, vl, "bcst", dst=8, s1=31, kreg=2, kval=RNG.getrandbits(64), title=extra_title))


def k_variants(sp):
    for vl in (16, 32, 64):
        emit(vcase(sp, vl, "reg", dst=1, s1=2, s2=3))
        emit(vcase(sp, vl, "reg", dst=6, s1=21, s2=19, kreg=4, kval=RNG.getrandbits(64)))
        emit(vcase(sp, vl, "mem", dst=2, s1=5))
        emit(vcase(sp, vl, "bcst", dst=3, s1=31, kreg=1, kval=RNG.getrandbits(64)))


# ---------------------------------------------------------------------------------------
# group A: BF16
# ---------------------------------------------------------------------------------------
def g_bf():
    return rnd_fp(BF16)


def g_bf_near():
    return rnd_fp(BF16, -6, 6)


def bf_spec(name, mmm, pp, opc, lay, fn, gen=g_bf, imm=False, **kw):
    d = dict(name=name, mmm=mmm, pp=pp, w=0, opc=opc, lay=lay, sesz=2, desz=2, fn=fn, mx="none",
             gen=gen, safe=BF16.one, imm=imm)
    d.update(kw)
    return d


def _bf2(op):
    return lambda s, ctx: (bf_arith(op, s[0], s[1]), 0, None)


BF_SPECS = [
    bf_spec("VADDBF16", 5, 1, 0x58, "rvm", _bf2("add"), g_bf_near),
    bf_spec("VSUBBF16", 5, 1, 0x5C, "rvm", _bf2("sub"), g_bf_near),
    bf_spec("VMULBF16", 5, 1, 0x59, "rvm", _bf2("mul")),
    bf_spec("VDIVBF16", 5, 1, 0x5E, "rvm", _bf2("div")),
    bf_spec("VMAXBF16", 5, 1, 0x5F, "rvm", lambda s, ctx: (bf_maxmin(s[0], s[1], True), 0, None)),
    bf_spec("VMINBF16", 5, 1, 0x5D, "rvm", lambda s, ctx: (bf_maxmin(s[0], s[1], False), 0, None)),
    bf_spec("VSQRTBF16", 5, 1, 0x51, "rm", lambda s, ctx: (bf_sqrt(s[0]), 0, None),
            lambda: rnd_fp(BF16) & 0x7FFF if RNG.getrandbits(3) else rnd_fp(BF16)),
    bf_spec("VSCALEFBF16", 6, 0, 0x2C, "rvm", lambda s, ctx: (bf_scalef(s[0], s[1]), 0, None)),
    bf_spec("VGETEXPBF16", 6, 0, 0x42, "rm",
            lambda s, ctx: (bf_getexp(s[0]), 0, None if bf_getexp(s[0]) == bf_getexp(s[0], False) else 0)),
    bf_spec("VRCPBF16", 6, 0, 0x4C, "rm",
            lambda s, ctx: (bf_rcp(s[0]), 0, None if bf_rcp(s[0]) == bf_rcp(s[0], False) else 0)),
    bf_spec("VRSQRTBF16", 6, 0, 0x4E, "rm", lambda s, ctx: (bf_rsqrt(s[0]), 0, None),
            lambda: rnd_fp(BF16) & 0x7FFF if RNG.getrandbits(3) else rnd_fp(BF16)),
    bf_spec("VGETMANTBF16", 3, 3, 0x26, "rm",
            lambda s, ctx: (bf_getmant(s[0], ctx["imm"]), 0,
                            None if bf_getmant(s[0], ctx["imm"]) == bf_getmant(s[0], ctx["imm"], False) else 0),
            imm=True, imm_gen=lambda: RNG.getrandbits(4)),
    bf_spec("VREDUCEBF16", 3, 3, 0x56, "rm",
            lambda s, ctx: (bf_reduce(s[0], ctx["imm"]), 0,
                            None if bf_reduce(s[0], ctx["imm"]) == bf_reduce(s[0], ctx["imm"], False) else 0),
            imm=True, imm_gen=lambda: RNG.getrandbits(4) << 4),
    bf_spec("VRNDSCALEBF16", 3, 3, 0x08, "rm", lambda s, ctx: (bf_rndscale(s[0], ctx["imm"]), 0, None),
            imm=True, imm_gen=lambda: RNG.getrandbits(4) << 4),
]


def scalef_gen():
    if RNG.getrandbits(1):
        return exact_bits(Fraction(RNG.randint(-160, 160), RNG.choice([1, 2, 4, 8])), BF16)
    return rnd_fp(BF16)


def bf_spec_by(name):
    return next(s for s in BF_SPECS if s["name"] == name)


def gen_bf16():
    comment("=== A. BF16 (spec ch.7, E4): DAZ, FTZ, RNE, no exception, MXCSR not consulted/updated")
    for sp in BF_SPECS:
        comment("%s (EVEX.%s.%s.W0 %02X%s)" % (sp["name"], ["NP", "66", "F3", "F2"][sp["pp"]],
                {3: "0F3A", 5: "MAP5", 6: "MAP6"}[sp["mmm"]], sp["opc"], " ib" if sp["imm"] else ""))
        if sp["name"] == "VSCALEFBF16":
            gen0 = sp["gen"]
            for vl in (16, 32, 64):
                n = vl // 2
                emit(vcase(sp, vl, "reg", 1, 2, 3, b=[scalef_gen() for _ in range(n)]))
                emit(vcase(sp, vl, "reg", 17, 30, 9, kreg=3, kval=RNG.getrandbits(64),
                           b=[scalef_gen() for _ in range(n)]))
                emit(vcase(sp, vl, "mem", 4, 5, kreg=7, kval=RNG.getrandbits(64), z=1,
                           b=[scalef_gen() for _ in range(n)]))
                emit(vcase(sp, vl, "bcst", 8, 31, kreg=2, kval=RNG.getrandbits(64), b=[scalef_gen()]))
            sp["gen"] = gen0
        else:
            std_variants(sp)
        n = 32
        a = rot(BF16_SP, 0, n)
        b = rot(BF16_SP, 5, n, 3)
        emit(vcase(sp, 64, "reg", 10, 11, 12, a=a, b=b, title="specials"))
        emit(vcase(sp, 64, "reg", 13, 14, 15, a=b, b=a, mxcsr=MX_NOT_CONSULTED,
                   title="specials, MXCSR=6000h (RZ, unmasked, no DAZ/FTZ) not consulted"))
        if sp.get("imm"):
            for imm in ((0x00, 0x01, 0x02, 0x03, 0x04, 0x08, 0x0C, 0x09) if sp["name"] == "VGETMANTBF16"
                        else (0x00, 0x30, 0xF0)):
                emit(vcase(sp, 64, "reg", 20, 21, 22, imm=imm, b=rot(BF16_SP, imm, 32, 7),
                           title="imm=%02X specials" % imm))
    # targeted values (rounding ties, FTZ boundary, DAZ, overflow, cancellation)
    comment("BF16 targeted: RNE ties, tininess after rounding (FTZ), DAZ, overflow, exact zero sign")
    add = bf_spec_by("VADDBF16")
    a = [0x3F80, 0x3F81, 0x3F80, 0x7F7F, 0x0001, 0x8001, 0x3F80, 0x8000, 0x0080, 0x0100, 0x4000, 0xC000,
         0x7F7F, 0x0081, 0x3F80, 0x0000]
    b = [0x3B80, 0x3B80, 0x3BC0, 0x7F7F, 0x0080, 0x0001, 0xBF80, 0x8000, 0x8001, 0x80C0, 0xC000, 0xC000,
         0x7400, 0x8080, 0x3380, 0x8000]
    emit(vcase(add, 32, "reg", 1, 2, 3, a=a, b=b, title="ties/overflow/DAZ/FTZ"))
    mul = bf_spec_by("VMULBF16")
    a = [0x0081, 0x0080, 0x8080, 0x0081, 0x7F00, 0x3F81, 0x0001, 0x2000, 0x1F80, 0x0100, 0x3F7F, 0x4000,
         0xFF7F, 0x00FF, 0x3FFF, 0x8081]
    b = [0x3F7E, 0x3F7F, 0x3F7F, 0x3F00, 0x4000, 0x3F81, 0x4780, 0x1F80, 0x2000, 0x3E80, 0x0080, 0x7F00,
         0x4000, 0x3F7F, 0x3FFF, 0x3F7E]
    emit(vcase(mul, 32, "reg", 4, 5, 6, a=a, b=b, title="FTZ boundary (tiny after rounding)"))
    div = bf_spec_by("VDIVBF16")
    a = [0x3F80, 0x3F80, 0x0000, 0x7F80, 0x8000, 0x0001, 0x3F80, 0x4040, 0x0080, 0x7F7F, 0xBF80, 0x0001,
         0x3F80, 0x0100, 0x4000, 0x3F80]
    b = [0x0000, 0x8001, 0x0000, 0x7F80, 0x3F80, 0x3F80, 0x4040, 0x4040, 0x4000, 0x0080, 0x7F80, 0x0001,
         0x7F7F, 0x4100, 0x3F80, 0x3E80]
    emit(vcase(div, 32, "reg", 7, 8, 9, a=a, b=b, title="x/0, DAZ, 1/3, tiny, overflow"))
    sq = bf_spec_by("VSQRTBF16")
    emit(vcase(sq, 32, "reg", 10, 11, 12, b=[0x4000, 0x4040, 0x3F81, 0x0080, 0x8001, 0x8000, 0xBF80, 0x7F80,
                                              0xFF80, 0x7F81, 0x4080, 0x3E80, 0x0001, 0x7F7F, 0x3F7F, 0x0081],
               title="sqrt 2/3/denormal/negatives"))
    sc = bf_spec_by("VSCALEFBF16")
    a = [0x3F80, 0x3F80, 0x3F80, 0x0000, 0x7F80, 0x3F80, 0x0080, 0x3F80, 0x0081, 0x7F7F, 0xBF80, 0x3F80,
         0x3FC0, 0x8000, 0x3F80, 0x0001]
    b = [0x4300, 0xC300, 0x7F80, 0x7F80, 0xFF80, 0xFF80, 0xBF80, 0x8001, 0xBF00, 0x3F80, 0x42FE, 0x3F7F,
         0xC2FC, 0x4000, 0xBF7F, 0x4000]
    emit(vcase(sc, 32, "reg", 13, 14, 15, a=a, b=b, title="2^+-128, Inf, DAZ floor(-den), FTZ"))
    comment("VF[N]M{ADD,SUB}{132,213,231}BF16 (EVEX.NP.MAP6.W0): one rounding, NaN priority a b c")
    fma_specs = []
    for nm, base, negp, negz in (("VFMADD", 0x98, False, False), ("VFMSUB", 0x9A, False, True),
                                 ("VFNMADD", 0x9C, True, False), ("VFNMSUB", 0x9E, True, True)):
        for order, off in ((132, 0), (213, 0x10), (231, 0x20)):
            def fn(s, ctx, order=order, negp=negp, negz=negz):
                r, amb = bf_fma(order, negp, negz, s[0], s[1], s[2])
                return r, 0, amb
            fma_specs.append(bf_spec("%s%dBF16" % (nm, order), 6, 0, base + off, "fma", fn, g_bf_near))
    for sp in fma_specs:
        comment("%s (EVEX.NP.MAP6.W0 %02X)" % (sp["name"], sp["opc"]))
        for vl in (16, 32, 64):
            emit(vcase(sp, vl, "reg", 1, 2, 3))
            emit(vcase(sp, vl, "mem", 20, 29, kreg=5, kval=RNG.getrandbits(64)))
            if sp["name"].endswith("213BF16"):
                emit(vcase(sp, vl, "bcst", 6, 7, kreg=1, kval=RNG.getrandbits(64), z=1))
            else:
                emit(vcase(sp, vl, "reg", 6, 7, 8, kreg=1, kval=RNG.getrandbits(64), z=1))
        emit(vcase(sp, 64, "reg", 10, 11, 12, d=rot(BF16_SP, 0, 32), a=rot(BF16_SP, 3, 32, 5),
                   b=rot(BF16_SP, 11, 32, 3), title="specials"))
    sp = fma_specs[0]
    # single rounding: (1+2^-7)^2 - (1+2^-6) = 2^-14 exactly (double rounding gives 0)
    emit(vcase(fma_specs[1], 16, "reg", 1, 2, 3, d=[0x3F81, 0x3F81, 0x0080, 0x3F80, 0x7F7F, 0x3F80, 0x0000, 0x8000],
               a=[0x3F81, 0x3F81, 0x3F00, 0x3F80, 0x4000, 0x3F80, 0x8000, 0x0000],
               b=[0xBF82, 0xBF81, 0x8000, 0xBF80, 0xFF7F, 0x3B80, 0x8000, 0x8000],
               title="VFMADD213: single rounding, exact-zero signs, FTZ"))
    comment("VFPCLASSBF16 k1{k2}, src, imm8 (EVEX.F2.0F3A.W0 66)")
    fpc = bf_spec("VFPCLASSBF16", 3, 3, 0x66, "krm", lambda s, ctx: (bf_fpclass(s[0], ctx["imm"]), 0, None),
                  imm=True)
    k_variants(fpc)
    for imm in (0x01, 0x06, 0x18, 0x20, 0x40, 0x80, 0xFF, 0x81):
        emit(vcase(fpc, 64, "reg", 5, 2, 7, imm=imm, b=rot(BF16_SP, imm, 32), title="imm=%02X" % imm))
    comment("VCMPBF16 k1{k2}, src1, src2, imm8 (EVEX.F2.0F3A.W0 C2): all 32 predicates")
    cmpb = bf_spec("VCMPBF16", 3, 3, 0xC2, "krvm", lambda s, ctx: (bf_cmp_pred(s[0], s[1], ctx["imm"]), 0, None),
                   imm=True, imm_gen=lambda: RNG.getrandbits(5))
    k_variants(cmpb)
    pairs = [(x, y) for x in BF16_SP[:14] for y in BF16_SP[:14]]
    for pred in range(32):
        pa = rot(pairs, pred * 32, 32)
        emit(vcase(cmpb, 64, "reg", 1 + pred % 7, 2, 3, imm=pred, a=[p[0] for p in pa], b=[p[1] for p in pa],
                   kreg=0 if pred % 4 else 6, kval=RNG.getrandbits(64), title="pred=%d" % pred))
    comment("VMINMAXBF16 (EVEX.F2.0F3A.W0 52): minmax(daz=true, except=false), all 32 imm8[4:0]")
    mmb = bf_spec("VMINMAXBF16", 3, 3, 0x52, "rvm",
                  lambda s, ctx: minmax(s[0], s[1], ctx["imm"], BF16, True, False), imm=True,
                  imm_gen=lambda: RNG.getrandbits(5))
    std_variants(mmb)
    mpairs = minmax_pairs(BF16)
    for imm in range(32):
        pa = rot(mpairs, imm * 32, 32)
        emit(vcase(mmb, 64, "reg", 1, 2, 3, imm=imm, a=[p[0] for p in pa], b=[p[1] for p in pa],
                   title="imm=%02X pairs" % imm))
    comment("VCOMISBF16 xmm1, xmm2/m16 (EVEX.LLIG.66.MAP5.W0 2F): ZF PF CF, OF AF SF := 0, DAZ")
    cp = [(0x3F80, 0x4000), (0x4000, 0x3F80), (0x3F80, 0x3F80), (0x0000, 0x8000), (0x0001, 0x8000),
          (0x0001, 0x0002), (0x7FC0, 0x3F80), (0x3F80, 0x7F81), (0xFF80, 0x7F80), (0x7F80, 0x7F80),
          (0xBF80, 0x0001), (0x7F7F, 0x7F80), (0x807F, 0x0080), (0xFFC1, 0xFFC1)]
    for i, (x, y) in enumerate(cp):
        emit(comisbf_case(x, y, mem=(i % 3 == 2), mxcsr=MX_NOT_CONSULTED if i % 4 == 3 else MXCSR_DEFAULT))


def comisbf_case(x, y, mem=False, mxcsr=MXCSR_DEFAULT, ll=0):
    c = Case("VCOMISBF16 %04X, %04X%s" % (x, y, " mem" if mem else ""))
    c.zmm[1] = pack([x], 2) + rnd_bytes(62)
    rfl = 0x202 | (RNG.getrandbits(12) & CLR)
    c.inp.append("rflags=0x%X" % rfl)
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    if mem:
        c.mem[MEM_RSI + 6] = pack([y], 2)
        rm = Mem(RSI, 6)
    else:
        c.zmm[18] = pack([y], 2) + rnd_bytes(62)
        rm = 18
    c.code = evex(5, 1, 0, 0x2F, 1, rm, ll=ll, n=2)
    c.exp.append("rflags=0x%X" % ((rfl & ~CLR) | bf_comis(x, y)))
    return c


def minmax_pairs(f):
    sp = SPECIALS[f]
    vals = sp[:12] + [sp[0] ^ f.signbit, f.one | f.signbit, exact_bits(2, f), exact_bits(-2, f)]
    return [(x, y) for x in vals for y in vals]


# ---------------------------------------------------------------------------------------
# group B: saturating conversions
# ---------------------------------------------------------------------------------------
def cvt_values(f, key):
    """boundary values (exact where representable) for the helper key"""
    signed, W = HELPERS[key][0], HELPERS[key][1]
    pts = [0, Fraction(1, 2), Fraction(-1, 2), Fraction(3, 2), Fraction(-3, 2), 1, -1, 2, -2,
           Fraction(-9, 10), Fraction(5, 2), Fraction(-5, 2), Fraction(7, 4), Fraction(-1, 4)]
    if signed:
        M = P2(W - 1)
        pts += [M - 1, M, -M, -M - 1, M - HALF, -M - HALF, M + 1, -M + HALF, 2 * M, -2 * M, M - 2, -M + 1]
    else:
        M = P2(W)
        pts += [M - 1, M, M - HALF, M + 1, -1, -2, M - 2, 2 * M, M / 2]
    out = []
    for p in pts:
        v = Fraction(p)
        if v == 0:
            out.append(0)
            continue
        bits = exact_bits(v, f)
        out.append(bits)
    # neighbours of the extreme bounds in the source format
    for p in ([P2(W - 1), -P2(W - 1)] if signed else [P2(W)]):
        bits = exact_bits(p, f)
        out += [bits - 1, bits + 1]
    out += [f.inf, f.signbit | f.inf, f.qnan_indef, quiet(f.inf | 5, f), f.inf | 1, 1, f.signbit | 3,
            f.signbit, (1 << f.fbits) | 7]
    return out


def cvt_gen(f, key):
    W = HELPERS[key][1]
    def g():
        r = RNG.getrandbits(3)
        if r == 0:
            return RNG.choice(cvt_values(f, key))
        if r == 1:
            return rnd_fp(f)
        hi = min(W + 1, f.emax)
        return rnd_fp(f, -3, hi)
    return g


CVT_SPECS = [
    # name, pp, w, opc, src fmt, sesz, desz, helper key, T (truncating) / R (rounding), rcf
    ("VCVTTPS2DQS", 0, 0, 0x6D, F32, 4, 4, "DW_S", "T", "sae"),
    ("VCVTTPS2UDQS", 0, 0, 0x6C, F32, 4, 4, "DW_U", "T", "sae"),
    ("VCVTTPS2QQS", 1, 0, 0x6D, F32, 4, 8, "QW_S", "T", "sae"),
    ("VCVTTPS2UQQS", 1, 0, 0x6C, F32, 4, 8, "QW_U", "T", "sae"),
    ("VCVTTPD2DQS", 0, 1, 0x6D, F64, 8, 4, "DW_S", "T", "sae"),
    ("VCVTTPD2UDQS", 0, 1, 0x6C, F64, 8, 4, "DW_U_PD", "T", "sae"),
    ("VCVTTPD2QQS", 1, 1, 0x6D, F64, 8, 8, "QW_S", "T", "sae"),
    ("VCVTTPD2UQQS", 1, 1, 0x6C, F64, 8, 8, "QW_U_PD", "T", "sae"),
    ("VCVTTPS2IBS", 1, 0, 0x68, F32, 4, 4, "B_S_T32", "T", "sae"),
    ("VCVTPS2IBS", 1, 0, 0x69, F32, 4, 4, "B_S_R", "R", "er"),
    ("VCVTTPS2IUBS", 1, 0, 0x6A, F32, 4, 4, "B_U_T32", "T", "sae"),
    ("VCVTPS2IUBS", 1, 0, 0x6B, F32, 4, 4, "B_U_R32", "R", "er"),
    ("VCVTTPH2IBS", 0, 0, 0x68, FP16, 2, 2, "B_S_T16", "T", "sae"),
    ("VCVTPH2IBS", 0, 0, 0x69, FP16, 2, 2, "B_S_R", "R", "er"),
    ("VCVTTPH2IUBS", 0, 0, 0x6A, FP16, 2, 2, "B_U_T16", "T", "sae"),
    ("VCVTPH2IUBS", 0, 0, 0x6B, FP16, 2, 2, "B_U_R16", "R", "er"),
]
BF_CVT_SPECS = [("VCVTTBF162IBS", 0x68, "BF_S_T"), ("VCVTBF162IBS", 0x69, "BF_S_R"),
                ("VCVTTBF162IUBS", 0x6A, "BF_U_T"), ("VCVTBF162IUBS", 0x6B, "BF_U_R")]


def cvt_spec(row):
    name, pp, w, opc, f, sesz, desz, key, kind, rcf = row

    def fn(s, ctx):
        rc = RZ if kind == "T" else ctx["rc"]
        return cvt_lane(key, s[0], f, rc, ctx["mxcsr"])
    return dict(name=name, mmm=5, pp=pp, w=w, opc=opc, lay="rm", sesz=sesz, desz=desz, fn=fn, mx="mxcsr",
                gen=cvt_gen(f, key), safe=f.one, fmt=f, key=key, kind=kind, rcf=rcf)


def bf_cvt_spec(row):
    name, opc, key = row

    def fn(s, ctx):
        x = daz_bits(s[0], BF16)
        r1 = bf_cvt_literal(key, x)
        return r1, 0, (None if r1 == bf_cvt_natural(key, x) else 0)

    def g():
        r = RNG.getrandbits(2)
        if r == 0:
            return RNG.choice(BF16_SP)
        return rnd_fp(BF16, -3, 9)
    return dict(name=name, mmm=5, pp=3, w=0, opc=opc, lay="rm", sesz=2, desz=2, fn=fn, mx="none", gen=g,
                safe=BF16.one, key=key)


def gen_cvt():
    comment("=== B. saturating conversions (spec ch.12, helpers 5.3)")
    for row in CVT_SPECS:
        sp = cvt_spec(row)
        f = sp["fmt"]
        comment("%s (EVEX.%s.MAP5.W%d %02X, %s, {%s})" % (sp["name"], ["NP", "66", "F3", "F2"][sp["pp"]],
                sp["w"], sp["opc"], "Half" if sp["sesz"] < sp["desz"] else "Full", sp["rcf"]))
        std_variants(sp)
        bv = cvt_values(f, sp["key"])
        KL = 64 // max(sp["sesz"], sp["desz"])
        for i in range(0, len(bv), KL):
            chunk = (bv[i:i + KL] + bv[:KL])[:KL]
            emit(vcase(sp, 64, "reg", 10, 0, 11, b=chunk, title="boundaries %d" % (i // KL)))
        allv = cvt_values(f, sp["key"])
        if sp["kind"] == "R":
            for mx in (0x3F80, 0x5F80, 0x7F80):
                emit(vcase(sp, 64, "reg", 12, 0, 13, b=rot(allv, mx >> 13, KL, 3), mxcsr=mx,
                           title="MXCSR.RC=%d" % ((mx >> 13) & 3)))
            for rc in range(4):
                emit(vcase(sp, 64, "reg", 14, 0, 15, b=rot(allv, rc * 5, KL, 2), er=rc, mxcsr=0x1F00 | (RU << 13),
                           title="{er} rc=%d, MXCSR RU unmasked: no flags, no #XM" % rc))
        else:
            emit(vcase(sp, 64, "reg", 12, 0, 13, b=rot(allv, 1, KL, 3), mxcsr=0x5F80,
                       title="MXCSR.RC=RU ignored (truncation)"))
            emit(vcase(sp, 64, "reg", 14, 0, 15, b=rot(allv, 2, KL, 2), sae=True, mxcsr=0x1F00,
                       title="{sae}, unmasked: no flags, no #XM"))
        # DAZ (FP32/FP64 only meaningful): denormal lanes are sanitized away for DAZ = 1 (AMBIGUOUS-07)
        emit(vcase(sp, 64, "reg", 16, 0, 17, b=rot(allv, 3, KL, 5), mxcsr=MXCSR_DEFAULT | DAZ | FTZ,
                   title="DAZ FTZ"))
        # #XM: NaN in an active lane with IM = 0; NaN only in masked-off lanes; PE unmasked
        bvals = [rnd_fp(f, -2, 4) for _ in range(KL)]
        bvals[1] = f.qnan_indef
        emit(vcase(sp, 64, "reg", 18, 0, 19, b=bvals, mxcsr=0x1F00, kreg=1, kval=0xFFFFFFFF,
                   title="NaN active, IM=0 -> #XM"))
        emit(vcase(sp, 64, "reg", 18, 0, 19, b=bvals, mxcsr=0x1F00, kreg=1, kval=0xFFFFFFFD,
                   title="NaN masked off, IM=0"))
        bvals2 = [exact_bits(Fraction(RNG.randint(-60, 60), 4), f) for _ in range(KL)]
        bvals2[0] = exact_bits(Fraction(5, 4), f)
        emit(vcase(sp, 64, "reg", 20, 0, 21, b=bvals2, mxcsr=MXCSR_DEFAULT & ~0x1000, title="inexact, PM=0 -> #XM"))
        # an {er} form has no plain {sae}: EVEX.b on reg-reg makes L'L the rounding control
        # (SDM Vol2A 2.7.8 / Table 2-38), so the suppress-all case is {rn-sae} there
        if sp["kind"] == "R":
            emit(vcase(sp, 64, "reg", 20, 0, 21, b=bvals2, mxcsr=MXCSR_DEFAULT & ~0x1000, er=RNE,
                       title="inexact, PM=0, {rn-sae}: no #XM"))
        else:
            emit(vcase(sp, 64, "reg", 20, 0, 21, b=bvals2, mxcsr=MXCSR_DEFAULT & ~0x1000, sae=True,
                       title="inexact, PM=0, {sae}: no #XM"))
    for row in BF_CVT_SPECS:
        sp = bf_cvt_spec(row)
        comment("%s (EVEX.F2.MAP5.W0 %02X, E4: no flags, DAZ, MXCSR not consulted)" % (sp["name"], sp["opc"]))
        std_variants(sp)
        vals = [0x0000, 0x8000, 0x3F00, 0xBF00, 0x3FC0, 0xBFC0, 0x4020, 0xC020, 0x42FE, 0x42FF, 0x4300, 0xC300,
                0xC301, 0x437F, 0x4380, 0x437E, 0xBF80, 0xBE80, 0x7F80, 0xFF80, 0x7FC0, 0x7F81, 0xFFC0, 0x0001,
                0x8001, 0x3F80, 0x7F7F, 0xFF7F, 0x42FD, 0xC2FF, 0x4310, 0x3E80]
        emit(vcase(sp, 64, "reg", 10, 0, 11, b=vals, title="boundaries"))
        emit(vcase(sp, 64, "reg", 12, 0, 13, b=rot(vals, 7, 32, 3), mxcsr=MX_NOT_CONSULTED,
                   title="MXCSR=6000h not consulted"))
    gen_cvt_scalar()


SCVT = [
    # name, pp, opc, src fmt, W -> helper key
    ("VCVTTSS2SIS", 2, 0x6D, F32, {0: "DW_S", 1: "QW_S"}),
    ("VCVTTSS2USIS", 2, 0x6C, F32, {0: "DW_U", 1: "QW_U"}),
    ("VCVTTSD2SIS", 3, 0x6D, F64, {0: "DW_S", 1: "QW_S"}),
    ("VCVTTSD2USIS", 3, 0x6C, F64, {0: "DW_U_PD", 1: "QW_U_PD"}),
]


def scvt_case(name, pp, opc, f, w, key, x, gpr=9, xr=21, mem=False, mxcsr=MXCSR_DEFAULT, sae=False,
              allow_amb=False, title=""):
    rlane = cvt_lane(key, x, f, RZ, mxcsr)
    if rlane[2] is not None and not allow_amb:
        x = f.one
        rlane = cvt_lane(key, x, f, RZ, mxcsr)
    r, flags, _ = rlane
    c = Case("%s r%d, %016X%s%s" % (name, 64 if w else 32, x, " mem" if mem else "", (" " + title) if title else ""))
    old = RNG.getrandbits(64)
    regn = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13",
            "r14", "r15"][gpr]
    c.inp.append("%s=0x%X" % (regn, old))
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    esz = f.esz
    if mem:
        c.mem[MEM_RSI - 3 * esz] = pack([x], esz)
        rm = Mem(RSI, -3 * esz)
    else:
        c.zmm[xr] = pack([x], esz) + rnd_bytes(64 - esz)
        rm = xr
    if sae:
        flags = 0
    unm = flags & ~(mxcsr >> 7) & 0x3F
    if unm:
        if unm & (IE | DE | ZE):
            flags &= ~(OE | UE | PE)
        c.fault = "#XM"
    else:
        c.exp.append("%s=0x%X" % (regn, r))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    c.code = evex(5, pp, w, opc, gpr, rm, ll=0, b=1 if sae else 0, n=esz)
    return c


def gen_cvt_scalar():
    comment("VCVTTS[S,D]2[U]SIS r32/r64, xmm/m (E3NF, Tuple1 Scalar, {sae}); r32 result zero-extends")
    for name, pp, opc, f, keys in SCVT:
        for w in (0, 1):
            key = keys[w]
            comment("%s r%d (EVEX.LLIG.%s.MAP5.W%d %02X, helper %s)" % (name, 64 if w else 32,
                    ["NP", "66", "F3", "F2"][pp], w, opc, key))
            vals = cvt_values(f, key)
            for i, x in enumerate(vals[::3] + [rnd_fp(f, -2, 34 if not w else 66) for _ in range(2)]):
                emit(scvt_case(name, pp, opc, f, w, key, x, gpr=[0, 9, 3, 15, 11][i % 5], xr=[1, 21, 30][i % 3],
                               mem=(i % 4 == 1)))
            emit(scvt_case(name, pp, opc, f, w, key, f.qnan_indef, mxcsr=0x1F00, title="NaN, IM=0 -> #XM"))
            emit(scvt_case(name, pp, opc, f, w, key, f.qnan_indef, mxcsr=0x1F00, sae=True,
                           title="NaN, IM=0, {sae}: no #XM, no flag"))
            emit(scvt_case(name, pp, opc, f, w, key, exact_bits(Fraction(7, 2), f), mxcsr=0x0F80,
                           title="3.5, PM=0 -> #XM"))
            emit(scvt_case(name, pp, opc, f, w, key, exact_bits(Fraction(7, 2), f), mxcsr=0x7F80 | DAZ,
                           title="3.5 RZ DAZ"))


# ---------------------------------------------------------------------------------------
# group C: MINMAX
# ---------------------------------------------------------------------------------------
MM_PACKED = [("VMINMAXPS", 1, 0, F32, "mx"), ("VMINMAXPD", 1, 1, F64, "mx"), ("VMINMAXPH", 0, 0, FP16, "no")]
MM_SCALAR = [("VMINMAXSS", 1, 0, F32, "mx"), ("VMINMAXSD", 1, 1, F64, "mx"), ("VMINMAXSH", 0, 0, FP16, "no")]


def mm_spec(name, pp, w, f, dazk):
    def fn(s, ctx):
        daz = bool(ctx["mxcsr"] & DAZ) if dazk == "mx" else False
        return minmax(s[0], s[1], ctx["imm"], f, daz, True)
    return dict(name=name, mmm=3, pp=pp, w=w, opc=0x52, lay="rvm", sesz=f.esz, desz=f.esz, fn=fn, mx="mxcsr",
                gen=lambda: (RNG.choice(SPECIALS[f]) if RNG.getrandbits(2) == 0 else rnd_fp(f)), safe=f.one,
                imm=True, imm_gen=lambda: RNG.getrandbits(5))


def gen_minmax():
    comment("=== C. MINMAX (spec ch.11, Figure 5.9): imm8[1:0] op, [3:2] sign control, [4] NaN select")
    for name, pp, w, f, dazk in MM_PACKED:
        sp = mm_spec(name, pp, w, f, dazk)
        comment("%s (EVEX.%s.0F3A.W%d 52 ib, E2, {sae}; DAZ %s)" % (name, ["NP", "66"][pp], w,
                "= MXCSR.DAZ" if dazk == "mx" else "never"))
        std_variants(sp)
        KL = 64 // f.esz
        pairs = minmax_pairs(f)
        for imm in range(32):
            pa = rot(pairs, imm * KL + imm * 7, KL)
            emit(vcase(sp, 64, "reg", 1 + imm % 5, 6, 7, imm=imm, a=[p[0] for p in pa], b=[p[1] for p in pa],
                       title="imm=%02X pairs" % imm))
        pa = rot(pairs, 3, KL, 5)
        av, bv = [p[0] for p in pa], [p[1] for p in pa]
        emit(vcase(sp, 64, "reg", 8, 9, 10, imm=0x11, a=av, b=bv, mxcsr=MXCSR_DEFAULT | DAZ, title="DAZ"))
        emit(vcase(sp, 64, "reg", 8, 9, 10, imm=0x02, a=av, b=bv, sae=True, mxcsr=0x1F00,
                   title="{sae} unmasked: no flags, no #XM"))
        snan = SPECIALS[f][8]
        av2 = [rnd_fp(f) for _ in range(KL)]
        bv2 = [rnd_fp(f) for _ in range(KL)]
        av2[3] = snan
        emit(vcase(sp, 64, "reg", 11, 12, 13, imm=0x00, a=av2, b=bv2, mxcsr=0x1F00, title="SNaN, IM=0 -> #XM"))
        emit(vcase(sp, 64, "reg", 11, 12, 13, imm=0x00, a=av2, b=bv2, mxcsr=0x1F00, kreg=2, kval=~8 & 0xFFFFFFFF,
                   title="SNaN masked off, IM=0"))
        av3 = list(av2)
        av3[3] = 1                      # smallest denormal
        emit(vcase(sp, 64, "reg", 11, 12, 13, imm=0x01, a=av3, b=bv2, mxcsr=MXCSR_DEFAULT & ~0x100,
                   title="denormal, DM=0 -> #XM"))
        emit(vcase(sp, 64, "reg", 11, 12, 13, imm=0x01, a=av3, b=bv2, mxcsr=(MXCSR_DEFAULT & ~0x100) | DAZ,
                   title="denormal, DM=0, DAZ"))
    sp = mm_spec("VMINMAXBF16", 3, 0, BF16, "x")
    for name, pp, w, f, dazk in MM_SCALAR:
        comment("%s (EVEX.LLIG.%s.0F3A.W%d 53 ib, E3, {sae}): bits 127:%d from SRC1" % (name, ["NP", "66"][pp], w,
                f.bits))
        pairs = minmax_pairs(f)
        for i in range(24):
            p = pairs[(i * 37 + 5) % len(pairs)]
            emit(sminmax_case(name, pp, w, f, dazk, p[0], p[1], imm=i, mem=(i % 4 == 1),
                              kreg=(1 if i % 5 == 2 else 0), kval=RNG.choice([0, 1, 2, 3]), z=(i % 10 == 2),
                              ll=i % 3))
        emit(sminmax_case(name, pp, w, f, dazk, SPECIALS[f][8], f.one, imm=0, mxcsr=0x1F00, title="SNaN IM=0 #XM"))
        emit(sminmax_case(name, pp, w, f, dazk, SPECIALS[f][8], f.one, imm=0, mxcsr=0x1F00, sae=True,
                          title="SNaN IM=0 {sae}"))
        emit(sminmax_case(name, pp, w, f, dazk, 1, f.one, imm=0, mxcsr=MXCSR_DEFAULT | DAZ, title="denormal DAZ"))
        emit(sminmax_case(name, pp, w, f, dazk, 1, f.signbit | 2, imm=0x05, title="two denormals"))


def sminmax_case(name, pp, w, f, dazk, a, b, imm, mxcsr=MXCSR_DEFAULT, mem=False, kreg=0, kval=0, z=0, sae=False,
                 ll=0, title="", allow_amb=False):
    esz = f.esz
    daz = bool(mxcsr & DAZ) if dazk == "mx" else False
    r, flags, amb = minmax(a, b, imm, f, daz, True)
    if amb is not None and not allow_amb:
        if amb == 0:
            a = f.one
        else:
            b = f.one
        r, flags, amb = minmax(a, b, imm, f, daz, True)
    c = Case("%s imm=%02X %X, %X%s%s" % (name, imm, a, b, " mem" if mem else "", (" " + title) if title else ""))
    dst, s1, s2 = 3, 20, 5
    c.zmm[dst] = rnd_bytes(64)
    c.zmm[s1] = pack([a], esz) + rnd_bytes(64 - esz)
    if mem:
        c.mem[MEM_RSI + 2 * esz] = pack([b], esz)
        rm = Mem(RSI, 2 * esz)
    else:
        c.zmm[s2] = pack([b], esz) + rnd_bytes(64 - esz)
        rm = s2
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    active = not kreg or kval & 1
    if sae or not active:
        flags = 0
    unm = flags & ~(mxcsr >> 7) & 0x3F
    if unm:
        c.fault = "#XM"
    else:
        low = pack([r], esz) if active else (bytes(esz) if z else c.zmm[dst][:esz])
        c.exp.append("zmm%d=%s" % (dst, hexs(low + c.zmm[s1][esz:16] + bytes(48))))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    c.code = evex(3, pp, w, 0x53, dst, rm, vvvv=s1, ll=ll, b=1 if sae else 0, z=z, aaa=kreg, imm=imm, n=esz)
    return c


# ---------------------------------------------------------------------------------------
# group D: VCOMX
# ---------------------------------------------------------------------------------------
COMX_FORMS = [  # name, mmm, pp, w, opc, fmt, signal_qnan, daz applies
    ("VCOMXSS", 1, 2, 0, 0x2F, F32, True, True), ("VUCOMXSS", 1, 2, 0, 0x2E, F32, False, True),
    ("VCOMXSD", 1, 3, 1, 0x2F, F64, True, True), ("VUCOMXSD", 1, 3, 1, 0x2E, F64, False, True),
    ("VCOMXSH", 5, 2, 0, 0x2F, FP16, True, False), ("VUCOMXSH", 5, 2, 0, 0x2E, FP16, False, False),
]


def comx_case(form, a, b, mxcsr=MXCSR_DEFAULT, mem=False, sae=False, ll=0, title=""):
    name, mmm, pp, w, opc, f, sq, dz = form
    esz = f.esz
    rel, flags = comx(a, b, f, mxcsr, sq, dz)
    c = Case("%s %X, %X%s%s" % (name, a, b, " mem" if mem else "", (" " + title) if title else ""))
    ra = RNG.choice([0, 7, 16, 31])
    c.zmm[ra] = pack([a], esz) + rnd_bytes(64 - esz)
    rfl = 0x202 | (RNG.getrandbits(12) & CLR)
    c.inp.append("rflags=0x%X" % rfl)
    if mem:
        c.mem[MEM_RSI + esz] = pack([b], esz)
        rm = Mem(RSI, esz)
    else:
        rb = RNG.choice([1, 9, 24])
        c.zmm[rb] = pack([b], esz) + rnd_bytes(64 - esz)
        rm = rb
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    if sae:
        flags = 0
    if flags & ~(mxcsr >> 7) & 0x3F:
        c.fault = "#XM"
    else:
        c.exp.append("rflags=0x%X" % ((rfl & ~CLR) | COMX_FL[rel]))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    c.code = evex(mmm, pp, w, opc, ra, rm, ll=ll, b=1 if sae else 0, n=esz)
    return c


def gen_comx():
    comment("=== D. VCOMX/VUCOMX (spec ch.8, E3NF): UNORD 11011 GT 00000 LT 10001 EQ 11100 (OF SF ZF PF CF)")
    for form in COMX_FORMS:
        name, mmm, pp, w, opc, f, sq, dz = form
        comment("%s (EVEX.LLIG.%s.%s.W%d %02X)" % (name, ["NP", "66", "F3", "F2"][pp], {1: "0F", 5: "MAP5"}[mmm],
                w, opc))
        sp = SPECIALS[f]
        two = exact_bits(2, f)
        pairs = [(f.one, two), (two, f.one), (f.one, f.one), (0, f.signbit), (sp[6], f.one), (f.one, sp[8]),
                 (sp[7], sp[6]), (f.inf, f.signbit | f.inf), (f.inf, f.inf), (1, 0), (1, 2), (f.signbit | 1, 1),
                 (sp[12], f.inf), (f.signbit | f.one, f.signbit | two), (rnd_fp(f), rnd_fp(f))]
        for i, (x, y) in enumerate(pairs):
            emit(comx_case(form, x, y, mem=(i % 3 == 1)))
        emit(comx_case(form, 1, 0, mxcsr=MXCSR_DEFAULT | DAZ, title="denormal vs 0, DAZ") if dz else
             comx_case(form, 2, 1, title="FP16 denormals"))
        emit(comx_case(form, sp[6], f.one, mxcsr=0x1F00, title="QNaN, IM=0"))
        emit(comx_case(form, f.one, sp[8], mxcsr=0x1F00, title="SNaN, IM=0"))
        emit(comx_case(form, f.one, sp[8], mxcsr=0x1F00, sae=True, title="SNaN, IM=0, {sae}"))
        emit(comx_case(form, 1, f.one, mxcsr=MXCSR_DEFAULT & ~0x100, title="denormal, DM=0"))
        emit(comx_case(form, 1, sp[6], mxcsr=MXCSR_DEFAULT & ~0x100, title="denormal + QNaN, DM=0: no DE"))


# ---------------------------------------------------------------------------------------
# group E: gating / encoding
# ---------------------------------------------------------------------------------------
def ud_case(title, *a, **kw):
    c = Case(title + " -> #UD")
    c.code = evex(*a, **kw)
    c.fault = "#UD"
    return c


def gen_gating():
    comment("=== E. AVX10.2 alone enables the AVX-512 (M1) forms; #UD encoding rules")
    m1 = dict(name="VADDPS", mmm=1, pp=0, w=0, opc=0x58, lay="rvm", sesz=4, desz=4, mx="mxcsr", safe=F32.one,
              gen=lambda: rnd_fp(F32, -20, 20), fn=lambda s, ctx: fp_binop("add", s[0], s[1], F32, ctx["mxcsr"]) + (None,))
    emit(vcase(m1, 64, "reg", 1, 2, 3, title="(AVX512F form, AVX512F CPUID bit off)"))
    emit(vcase(m1, 32, "reg", 17, 30, 9, kreg=3, kval=0xA5, title="(AVX512VL form)"))
    emit(vcase(m1, 16, "mem", 4, 5, kreg=7, kval=0x9, z=1, title="(AVX512VL form)"))
    emit(vcase(m1, 64, "bcst", 8, 31, kreg=2, kval=0x5A5A))
    m1i = dict(name="VPADDD", mmm=1, pp=1, w=0, opc=0xFE, lay="rvm", sesz=4, desz=4, mx="none", safe=1,
               gen=lambda: RNG.getrandbits(32), fn=lambda s, ctx: ((s[0] + s[1]) & 0xFFFFFFFF, 0, None))
    emit(vcase(m1i, 64, "reg", 1, 2, 3))
    emit(vcase(m1i, 32, "reg", 20, 21, 22, kreg=1, kval=0x3C))
    emit(vcase(m1i, 16, "bcst", 6, 7))
    emit(ud_case("VADDPS zmm EVEX.U=0", 1, 0, 0, 0x58, 1, 3, vvvv=2, ll=2, p1_and=0xFB))
    emit(ud_case("VADDBF16 zmm EVEX.U=0", 5, 1, 0, 0x58, 1, 3, vvvv=2, ll=2, p1_and=0xFB))
    emit(ud_case("VMINMAXPS ymm {sae} EVEX.U=0 (rev 1-3 YMM rounding form)", 3, 1, 0, 0x52, 1, 3, vvvv=2, ll=1, b=1,
                 imm=0, p1_and=0xFB))
    emit(ud_case("VMINMAXPS zmm {sae} EVEX.U=0", 3, 1, 0, 0x52, 1, 3, vvvv=2, ll=2, b=1, imm=0, p1_and=0xFB))
    emit(ud_case("VCVTTPS2DQS ymm {sae} EVEX.U=0", 5, 0, 0, 0x6D, 1, 3, ll=1, b=1, p1_and=0xFB))
    emit(ud_case("VCVTTSS2SIS EVEX.U=0", 5, 2, 0, 0x6D, 0, 3, p1_and=0xFB))
    emit(ud_case("VCOMXSS EVEX.U=0", 1, 2, 0, 0x2F, 0, 3, p1_and=0xFB))
    for t, a in (("VADDBF16 W1", (5, 1, 1, 0x58, 1, 3)), ("VMULBF16 W1", (5, 1, 1, 0x59, 1, 3)),
                 ("VFMADD132BF16 W1", (6, 0, 1, 0x98, 1, 3)), ("VSCALEFBF16 W1", (6, 0, 1, 0x2C, 1, 3))):
        emit(ud_case(t, *a, vvvv=2, ll=2))
    for t, a in (("VCMPBF16 W1", (3, 3, 1, 0xC2, 1, 3)), ("VMINMAXBF16 W1", (3, 3, 1, 0x52, 1, 3)),
                 ("VMINMAXSH W1", (3, 0, 1, 0x53, 1, 3)), ("VMINMAXPH W1", (3, 0, 1, 0x52, 1, 3))):
        emit(ud_case(t, *a, vvvv=2, ll=2, imm=0))
    for t, a in (("VCVTTPS2IBS W1", (5, 1, 1, 0x68, 1, 3)), ("VCVTTBF162IBS W1", (5, 3, 1, 0x68, 1, 3)),
                 ("VCVTPH2IBS W1", (5, 0, 1, 0x69, 1, 3)), ("VRCPBF16 W1", (6, 0, 1, 0x4C, 1, 3)),
                 ("VCOMISBF16 W1", (5, 1, 1, 0x2F, 1, 3)), ("VCOMXSS W1", (1, 2, 1, 0x2F, 1, 3)),
                 ("VCOMXSD W0", (1, 3, 0, 0x2F, 1, 3)), ("VUCOMXSH W1", (5, 2, 1, 0x2E, 1, 3))):
        emit(ud_case(t, *a, ll=0 if t.startswith(("VCOM", "VUCOM")) else 2))
    emit(ud_case("VFPCLASSBF16 W1", 3, 3, 1, 0x66, 1, 3, ll=2, imm=0))
    emit(ud_case("VCMPBF16 {z} (k destination)", 3, 3, 0, 0xC2, 1, 3, vvvv=2, ll=2, z=1, aaa=2, imm=0))
    emit(ud_case("VFPCLASSBF16 {z} (k destination)", 3, 3, 0, 0x66, 1, 3, ll=2, z=1, aaa=2, imm=0))
    for t, a in (("VCOMXSS aaa=1", (1, 2, 0, 0x2F, 1, 3)), ("VUCOMXSD aaa=3", (1, 3, 1, 0x2E, 1, 3)),
                 ("VCOMXSH aaa=7", (5, 2, 0, 0x2F, 1, 3)), ("VCOMISBF16 aaa=1", (5, 1, 0, 0x2F, 1, 3)),
                 ("VCVTTSS2SIS aaa=1", (5, 2, 0, 0x6D, 1, 3)), ("VCVTTSD2USIS aaa=2", (5, 3, 1, 0x6C, 1, 3))):
        emit(ud_case(t, *a, aaa=int(t[-1])))
    for t, a, kw in (("VSQRTBF16 vvvv=0101b", (5, 1, 0, 0x51, 1, 3), dict(ll=2)),
                     ("VGETEXPBF16 vvvv=0000b", (6, 0, 0, 0x42, 1, 3), dict(ll=2)),
                     ("VFPCLASSBF16 vvvv!=1111b", (3, 3, 0, 0x66, 1, 3), dict(ll=2, imm=0)),
                     ("VCVTTPS2DQS vvvv!=1111b", (5, 0, 0, 0x6D, 1, 3), dict(ll=2)),
                     ("VCVTTPH2IBS vvvv!=1111b", (5, 0, 0, 0x68, 1, 3), dict(ll=1)),
                     ("VCOMXSS vvvv!=1111b", (1, 2, 0, 0x2F, 1, 3), dict()),
                     ("VCOMISBF16 vvvv!=1111b", (5, 1, 0, 0x2F, 1, 3), dict()),
                     ("VCVTTSD2SIS vvvv!=1111b", (5, 3, 0, 0x6D, 1, 3), dict())):
        emit(ud_case(t, *a, vvvv=5, **kw))
    emit(ud_case("VCOMXSD V'=0 (no vvvv operand)", 1, 3, 1, 0x2F, 1, 3, p2_vp=0))
    emit(ud_case("VRSQRTBF16 V'=0 (no vvvv operand)", 6, 0, 0, 0x4E, 1, 3, ll=2, p2_vp=0))
    emit(ud_case("VADDBF16 {z} with aaa=0", 5, 1, 0, 0x58, 1, 3, vvvv=2, ll=2, z=1))
    emit(ud_case("VADDBF16 L'L=11b", 5, 1, 0, 0x58, 1, 3, vvvv=2, ll=3))
    emit(ud_case("VMINMAXPS L'L=11b (no EVEX.b)", 3, 1, 0, 0x52, 1, 3, vvvv=2, ll=3, imm=0))
    emit(ud_case("VADDBF16 EVEX.b reg-reg (no {er}/{sae})", 5, 1, 0, 0x58, 1, 3, vvvv=2, ll=2, b=1))
    emit(ud_case("VMINMAXBF16 EVEX.b reg-reg (no {sae})", 3, 3, 0, 0x52, 1, 3, vvvv=2, ll=2, b=1, imm=0))
    emit(ud_case("VCVTTBF162IBS EVEX.b reg-reg", 5, 3, 0, 0x68, 1, 3, ll=2, b=1))
    emit(ud_case("VMINMAXSS EVEX.b with memory", 3, 1, 0, 0x53, 1, Mem(RSI, 0), vvvv=2, b=1, imm=0, n=4))
    emit(ud_case("VCOMXSD EVEX.b with memory", 1, 3, 1, 0x2F, 1, Mem(RSI, 0), b=1, n=8))
    emit(ud_case("VADDBF16 P0[3]=1", 5, 1, 0, 0x58, 1, 3, vvvv=2, ll=2, p0_or=0x08))
    c = vcase(bf_spec_by("VADDBF16"), 64, "reg", 1, 2, 3, title="(control: valid encoding)")
    emit(c)


# ---------------------------------------------------------------------------------------
# AMBIGUOUS cases (expected value = the reading chosen in the AMBIGUOUS: comments)
# ---------------------------------------------------------------------------------------
def gen_ambiguous():
    comment("=== AMBIGUOUS spec points (expectation = reading chosen in ref_avx10_a.py; see its AMBIGUOUS: notes)")
    sps = {row[0]: cvt_spec(row) for row in CVT_SPECS}

    def amb(tag, name, vals, **kw):
        sp = sps[name]
        KL = 64 // max(sp["sesz"], sp["desz"])
        vals = (vals * KL)[:KL]
        emit(vcase(sp, 64, "reg", 10, 0, 11, b=vals, allow_amb=True, title="AMBIGUOUS-%s" % tag, **kw))

    eb = exact_bits
    amb("01 saturation branch without PE", "VCVTTPD2DQS",
        [eb(P2(31) - HALF, F64), eb(-P2(31) - HALF, F64), eb(P2(31) - 1, F64), eb(-P2(31), F64)])
    amb("01 saturation branch without PE", "VCVTPS2IBS",
        [eb(Fraction(1273, 10), F32), eb(Fraction(-257, 2), F32), eb(Fraction(1271, 10), F32), eb(-128, F32)])
    amb("02 VCVTTPH2IBS '// PE=1' in saturation branches", "VCVTTPH2IBS",
        [eb(127, FP16), eb(-128, FP16), FP16.inf, FP16.signbit | FP16.inf, eb(300, FP16), eb(Fraction(1275, 10), FP16)])
    amb("03 VCVTTPD2UDQS extra PE rule", "VCVTTPD2UDQS",
        [eb(-1, F64), eb(Fraction(-11, 2), F64), eb(P2(32), F64), F64.inf, F64.signbit | F64.inf,
         eb(P2(32) - HALF, F64), eb(Fraction(-1, 2), F64), eb(P2(33), F64)])
    amb("04 VCVTPS2IUBS RC-independent range", "VCVTPS2IUBS",
        [eb(Fraction(2553, 10), F32), eb(Fraction(511, 2), F32), eb(Fraction(-3, 10), F32), eb(Fraction(-7, 10), F32)])
    amb("04 VCVTPS2IUBS RC-independent range, RU", "VCVTPS2IUBS",
        [eb(Fraction(2552, 10), F32), eb(Fraction(-1, 10), F32), eb(Fraction(2549, 10), F32)], mxcsr=0x5F80)
    amb("05 unsigned (-1,0) saturation branch without PE", "VCVTTPS2UDQS", [eb(Fraction(-1, 2), F32)])
    amb("05 unsigned (-1,0) saturation branch without PE", "VCVTTPS2UQQS", [eb(Fraction(-3, 4), F32)])
    amb("05 unsigned (-1,0) saturation branch without PE", "VCVTTPH2IUBS", [eb(Fraction(-1, 2), FP16)])
    amb("05 unsigned (-1,0) saturation branch without PE", "VCVTPH2IUBS", [eb(Fraction(-1, 4), FP16)])
    amb("05 unsigned (-1,0) saturation branch without PE", "VCVTTPD2UQQS", [eb(Fraction(-1, 2), F64)])
    amb("06 VCVTPH2IBS RNE 127.25 / -128.5", "VCVTPH2IBS", [eb(Fraction(509, 4), FP16), eb(Fraction(-257, 2), FP16)])
    amb("07 DAZ on FP32/FP64 conversion inputs (applied)", "VCVTTPS2DQS", [1, F32.signbit | 0x7FFFFF],
        mxcsr=MXCSR_DEFAULT | DAZ)
    amb("07 DAZ on FP32/FP64 conversion inputs (applied)", "VCVTTPD2QQS", [1, F64.signbit | 5],
        mxcsr=(MXCSR_DEFAULT & ~0x1000) | DAZ)
    amb("07 DAZ on FP32/FP64 conversion inputs (applied)", "VCVTPS2IUBS", [3, F32.signbit | 1],
        mxcsr=0x5F80 | DAZ)
    emit(scvt_case("VCVTTSS2SIS", 2, 0x6D, F32, 0, "DW_S", 0x00000003, mxcsr=MXCSR_DEFAULT | DAZ, allow_amb=True,
                   title="AMBIGUOUS-07 DAZ"))
    emit(scvt_case("VCVTTSD2USIS", 3, 0x6C, F64, 0, "DW_U_PD", eb(-3, F64), allow_amb=True,
                   title="AMBIGUOUS-03 extra PE"))
    emit(scvt_case("VCVTTSD2SIS", 3, 0x6D, F64, 0, "DW_S", eb(P2(31) - HALF, F64), allow_amb=True,
                   title="AMBIGUOUS-01 saturation branch without PE"))
    # BF16
    bsp = {s["name"]: s for s in BF_SPECS}
    emit(vcase(bsp["VGETEXPBF16"], 16, "reg", 1, 0, 2, b=[0xFF80, 0x7F80, 0xFF80, 0x0001, 0xFF80, 0x8000, 0x3F80,
                                                        0xFF80], allow_amb=True, title="AMBIGUOUS-08 -Inf -> 128.0"))
    for imm in (0x00, 0x01, 0x02, 0x03, 0x04, 0x08):
        emit(vcase(bsp["VGETMANTBF16"], 16, "reg", 3, 0, 4, imm=imm,
                   b=[0x0001, 0x8001, 0x007F, 0x807F, 0x0040, 0x8040, 0x0000, 0x8000], allow_amb=True,
                   title="AMBIGUOUS-09 denormal inputs"))
    emit(vcase(bsp["VREDUCEBF16"], 16, "reg", 5, 0, 6, imm=0x20, b=[0x7F80, 0xFF80, 0x3FC0, 0x7F80, 0x4049, 0xFF80,
                                                                 0x0000, 0x8000], allow_amb=True,
               title="AMBIGUOUS-10 +-Inf -> QNaN indefinite"))
    for imm in (0x11, 0x23, 0x0B):
        emit(vcase(bsp["VREDUCEBF16"], 16, "reg", 5, 0, 6, imm=imm, b=[0x3FC0, 0xBFC0, 0x4020, 0xC020, 0x3FA0,
                                                                    0x4049, 0xC049, 0x3F90], allow_amb=True,
                   title="AMBIGUOUS-11 imm8[3:0] ignored (RNE)"))
        emit(vcase(bsp["VRNDSCALEBF16"], 16, "reg", 7, 0, 8, imm=imm, b=[0x3FC0, 0xBFC0, 0x4020, 0xC020, 0x3FA0,
                                                                      0x4049, 0xC049, 0x3F90], allow_amb=True,
                   title="AMBIGUOUS-11 imm8[3:0] ignored (RNE)"))
    emit(vcase(bsp["VRCPBF16"], 16, "reg", 9, 0, 10, b=[0x7F00, 0xFF00, 0x7EC0, 0x7F7F, 0x7E80, 0xFE80, 0x7F01,
                                                     0x3F80], allow_amb=True,
               title="AMBIGUOUS-12 tiny reciprocal flushed (FTZ)"))
    # FMA: NaN in 'a' of the negated forms
    for nm, base, negp, negz in (("VFNMADD", 0x9C, True, False), ("VFNMSUB", 0x9E, True, True)):
        for order, off in ((132, 0), (213, 0x10), (231, 0x20)):
            def fn(s, ctx, order=order, negp=negp, negz=negz):
                r, a_ = bf_fma(order, negp, negz, s[0], s[1], s[2])
                return r, 0, a_
            sp = bf_spec("%s%dBF16" % (nm, order), 6, 0, base + off, "fma", fn)
            nanv = [0x7FC0, 0xFFC1, 0x7F81, 0xFFA0, 0x7FC0, 0x7F81, 0xFFC1, 0xFFA0]
            ones = [0x3F80] * 8
            if order == 132:
                d, a, b = nanv, ones, [0x7FC5, 0x3F80, 0x7FC5, 0x3F80, 0x4000, 0x7F82, 0x4000, 0x3F80]
            else:
                d, a, b = [0x7FC5, 0x3F80, 0x7FC5, 0x3F80, 0x4000, 0x7F82, 0x4000, 0x3F80], nanv, ones
            emit(vcase(sp, 16, "reg", 11, 12, 13, d=d, a=a, b=b, allow_amb=True,
                       title="AMBIGUOUS-13 NaN in a of a negated form: sign flipped"))
    # FMA broadcast of the memory operand (SRC3) for 132 / 231
    for nm, base, negp, negz in (("VFMADD", 0x98, False, False), ("VFNMSUB", 0x9E, True, True)):
        for order, off in ((132, 0), (231, 0x20)):
            def fn(s, ctx, order=order, negp=negp, negz=negz):
                r, a_ = bf_fma(order, negp, negz, s[0], s[1], s[2])
                return r, 0, a_
            sp = bf_spec("%s%dBF16" % (nm, order), 6, 0, base + off, "fma", fn, g_bf_near)
            emit(vcase(sp, 32, "bcst", 14, 15, kreg=3, kval=RNG.getrandbits(16), title="AMBIGUOUS-14 {1to16} = SRC3"))
    # VCOMX L'L != 00b
    for form, ll in ((COMX_FORMS[0], 1), (COMX_FORMS[3], 2), (COMX_FORMS[4], 1)):
        name, mmm, pp, w, opc = form[:5]
        c = Case("%s L'L=%d (encoding 'LLIG' but '#UD if EVEX.LL != 00b') AMBIGUOUS-15" % (name, ll))
        c.code = evex(mmm, pp, w, opc, 1, 2, ll=ll)
        c.fault = "#UD"
        emit(c)
    emit(comx_case(COMX_FORMS[4], 1, 0, mxcsr=MXCSR_DEFAULT | DAZ, title="AMBIGUOUS-16 FP16 denormal not DAZ'd, DE"))
    emit(comx_case(COMX_FORMS[5], 0x8001, 0x8000, mxcsr=MXCSR_DEFAULT | DAZ,
                   title="AMBIGUOUS-16 FP16 denormal not DAZ'd, DE"))
    # MINMAX Number ops with one SNaN
    sp = mm_spec("VMINMAXPS", 1, 0, F32, "mx")
    av = [0x7FA00000, 0x3F800000, 0xFF800001, 0x40000000]
    bv = [0x3F800000, 0x7FA00000, 0xC0000000, 0xFF800001]
    for imm in (0x10, 0x13, 0x1E):
        emit(vcase(sp, 16, "reg", 12, 13, 14, imm=imm, a=av, b=bv, allow_amb=True,
                   title="AMBIGUOUS-17 SNaN + number, *Number op: IE"))
    emit(sminmax_case("VMINMAXSD", 1, 1, F64, "mx", 0x7FF4000000000000, 0x4000000000000000, 0x11, mxcsr=0x1F00,
                      allow_amb=True, title="AMBIGUOUS-17 SNaN + number, IM=0 -> #XM"))
    emit(sminmax_case("VMINMAXSH", 0, 0, FP16, "no", 0x3C00, 0x7D00, 0x14, allow_amb=True,
                      title="AMBIGUOUS-17 SNaN + number: IE"))


# ---------------------------------------------------------------------------------------
# self test: hand-derived values (spec / SDM line references in the comments)
# ---------------------------------------------------------------------------------------
def selftest():
    ok = True
    n = [0]

    def chk(name, got, want):
        nonlocal ok
        n[0] += 1
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    # --- encoding: VADDBF16 zmm1, zmm2, zmm3 = EVEX.512.66.MAP5.W0 58 /r (spec 7.1, L6929)
    chk("enc vaddbf16", evex(5, 1, 0, 0x58, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF5, 0x6D, 0x48, 0x58, 0xCB]))
    # VMINMAXPS zmm1{k1}, zmm2, [rsi+0x40], 3 (EVEX.512.66.0F3A.W0 52 /r ib, Full: N = 64)
    chk("enc vminmaxps", evex(3, 1, 0, 0x52, 1, Mem(RSI, 0x40), vvvv=2, ll=2, aaa=1, imm=3, n=64),
        bytes([0x62, 0xF3, 0x6D, 0x49, 0x52, 0x4E, 0x01, 0x03]))
    # VCVTTPS2QQS zmm1, [rsi+0x20] (Half tuple, N = 32 at VL 512)
    chk("enc half disp8", evex(5, 1, 0, 0x6D, 1, Mem(RSI, 0x20), ll=2, n=32)[-2:], bytes([0x4E, 0x01]))
    # --- BF16 RNE ties (spec 7.1.2 'Rounding Mode is always RNE')
    chk("rne tie even", bf_arith("add", 0x3F80, 0x3B80), 0x3F80)          # 1 + 2^-8 -> 1
    chk("rne tie odd", bf_arith("add", 0x3F81, 0x3B80), 0x3F82)           # (1+2^-7) + 2^-8 -> 1+2^-6
    chk("rne above half", bf_arith("add", 0x3F80, 0x3BC0), 0x3F81)        # 1 + 1.5*2^-8 -> 1+2^-7
    chk("rne neg tie", bf_arith("sub", 0xBF81, 0x3B80), 0xBF82)
    chk("overflow", bf_arith("add", 0x7F7F, 0x7F7F), 0x7F80)
    chk("x-x = +0", bf_arith("sub", 0x4049, 0x4049), 0x0000)
    chk("-0 + -0", bf_arith("add", 0x8000, 0x8000), 0x8000)
    # --- FTZ with tininess after rounding (SDM Vol1 4.9.1.5 / 10.2.3.3):
    # 2^-126 (1+2^-7)(1-2^-7) = 2^-126 (1-2^-14) rounds (8 bits, unbounded) to 2^-126: not tiny
    chk("ftz boundary up", bf_arith("mul", 0x0081, 0x3F7E), 0x0080)
    # 2^-126 (1-2^-8) is exact with 8 bits and < 2^-126: tiny -> +0 (and -0 for the negative)
    chk("ftz tiny exact", bf_arith("mul", 0x0080, 0x3F7F), 0x0000)
    chk("ftz tiny exact neg", bf_arith("mul", 0x8080, 0x3F7F), 0x8000)
    chk("ftz 2^-127", bf_arith("mul", 0x0080, 0x3F00), 0x0000)
    # --- DAZ (spec 7.1.2): denormal inputs are zeros
    chk("daz add", bf_arith("add", 0x0001, 0x0080), 0x0080)
    chk("daz div 0/0", bf_arith("div", 0x0001, 0x0001), 0xFFC0)
    chk("daz x/den", bf_arith("div", 0x3F80, 0x8001), 0xFF80)
    chk("daz cmp eq", bf_cmp_pred(0x0001, 0x8000, 0), True)
    chk("daz sqrt -den", bf_sqrt(0x8001), 0x8000)
    chk("daz max", bf_maxmin(0xBF80, 0x0001, True), 0x0000)
    # --- NaNs (SDM Vol1 Table 4-7: first source operand that is a NaN, quietened)
    chk("snan+1", bf_arith("add", 0x7F81, 0x3F80), 0x7FC1)
    chk("qnan1+snan2", bf_arith("add", 0xFFC5, 0x7F81), 0xFFC5)
    chk("1+snan2", bf_arith("mul", 0x3F80, 0x7F82), 0x7FC2)
    chk("inf-inf", bf_arith("sub", 0x7F80, 0x7F80), 0xFFC0)
    chk("sqrt(-1)", bf_sqrt(0xBF80), 0xFFC0)
    chk("max nan1", bf_maxmin(0x7FC0, 0x3F80, True), 0x3F80)               # spec 7.9.3 lines 4-5
    chk("max snan2 unchanged", bf_maxmin(0x3F80, 0x7F81, True), 0x7F81)    # spec 7.9.2
    chk("min zeros -> src2", bf_maxmin(0x8000, 0x0000, False), 0x0000)
    # --- FMA single rounding (spec 7.5.3 'RoundFPControl_RNE(a*b OP c)')
    chk("fma one rounding", bf_fma(213, False, False, 0x3F81, 0x3F81, 0xBF82)[0], 0x3880)
    chk("fma nan priority b", bf_fma(132, False, False, 0x3F80, 0x7FC2, 0x7FC3)[0], 0x7FC3)  # 132: b = SRC3
    chk("fma inf*0 + qnan", bf_fma(213, False, False, 0x7F80, 0x0000, 0x7FC7)[0], 0x7FC7)    # Table 14-17
    chk("fnmadd nan literal flip", bf_fma(213, True, False, 0x3F80, 0x7FC0, 0x3F80)[0], 0xFFC0)
    chk("fnmsub zero sign", bf_fma(213, True, True, 0x0000, 0x3F80, 0x0000)[0], 0x8000)
    # --- VGETEXP / VGETMANT / VREDUCE / VRNDSCALE / VSCALEF / VFPCLASS
    chk("getexp 1.5", bf_getexp(0x3FC0), 0x0000)
    chk("getexp 2^-126", bf_getexp(0x0080), 0xC2FC)
    chk("getexp den", bf_getexp(0x0001), 0xFF80)
    chk("getexp -inf literal", bf_getexp(0xFF80), 0x4300)
    chk("getmant 6.0 [1,2)", bf_getmant(0x40C0, 0x00), 0x3FC0)
    chk("getmant 6.0 [.5,1)", bf_getmant(0x40C0, 0x02), 0x3F40)
    chk("getmant -6 sc=01", bf_getmant(0xC0C0, 0x04), 0x3FC0)
    chk("getmant -6 sc=10", bf_getmant(0xC0C0, 0x08), 0xFFC0)
    chk("getmant 3.0 interv 01", bf_getmant(0x4040, 0x01), 0x3F40)        # exponent 1 odd -> [0.5,1)
    chk("getmant -0", bf_getmant(0x8000, 0x00), 0xBF80)
    chk("reduce 2.75 m=0", bf_reduce(0x4030, 0x00), 0xBE80)               # 2.75 - 3 = -0.25
    chk("reduce 2.75 m=1 tie", bf_reduce(0x4030, 0x10), 0xBE80)           # 5.5 -> 6 (even): 2.75 - 3
    chk("reduce 2.625 m=1", bf_reduce(0x4028, 0x10), 0x3E00)              # 5.25 -> 5: 2.625 - 2.5
    chk("rndscale 2.5", bf_rndscale(0x4020, 0x00), 0x4000)
    chk("rndscale -0.3", bf_rndscale(0xBE9A, 0x00), 0x8000)
    chk("rndscale 2.75 m=1", bf_rndscale(0x4030, 0x10), 0x4040)
    chk("scalef 1*2^floor(-0.5)", bf_scalef(0x3F80, 0xBF00), 0x3F00)
    chk("scalef daz src2", bf_scalef(0x3F80, 0x8001), 0x3F80)
    chk("scalef 0*2^inf", bf_scalef(0x0000, 0x7F80), 0xFFC0)
    chk("fpclass den is +0", bf_fpclass(0x0001, 0x02), True)
    chk("fpclass den bit5", bf_fpclass(0x0001, 0x20), False)
    chk("fpclass snan", bf_fpclass(0x7F81, 0x80), True)
    chk("fpclass -1 neg finite", bf_fpclass(0xBF80, 0x40), True)
    chk("comis lt", bf_comis(0x3F80, 0x4000), 0x01)
    chk("comis unord", bf_comis(0x7FC0, 0x4000), 0x45)
    # --- VRCP / VRSQRT model choice: bound 2^-8 + 2^-14 (spec 7.12.2 / 7.15.2) and Tables 7.2 / 7.3
    bound = P2(-8) + P2(-14)
    worst_r = worst_s = Fraction(0)
    for e in range(1, 255):
        for m in range(128):
            x = (e << 7) | m
            v = classify(x, BF16)[2]
            r = bf_rcp(x)
            if classify(r, BF16)[0] == "normal":
                err = abs(classify(r, BF16)[2] * v - 1)
                worst_r = max(worst_r, err)
            elif v <= P2(126):
                chk("rcp not normal %04X" % x, r, None)
            s = bf_rsqrt(x)
            t = classify(s, BF16)[2]
            worst_s = max(worst_s, abs(t - 1 / Fraction(rne_sqrt_value(v, 60)[0])) / t)
    chk("rcp bound", worst_r < bound, True)
    chk("rsqrt bound", worst_s < bound, True)
    chk("rcp +0", bf_rcp(0x0000), 0x7F80)
    chk("rcp +den", bf_rcp(0x0001), 0x7F80)
    chk("rcp -0", bf_rcp(0x8000), 0xFF80)
    chk("rcp -den", bf_rcp(0x807F), 0xFF80)
    chk("rcp +inf", bf_rcp(0x7F80), 0x0000)
    chk("rcp -inf", bf_rcp(0xFF80), 0x8000)
    for k in range(-126, 127):
        chk("rcp 2^%d" % k, bf_rcp(exact_bits(P2(k), BF16)), exact_bits(P2(-k), BF16))
        chk("rcp -2^%d" % k, bf_rcp(exact_bits(-P2(k), BF16)), exact_bits(-P2(-k), BF16))
    chk("rcp 2^127 ftz", bf_rcp(0x7F00), 0x0000)
    chk("rsqrt +0", bf_rsqrt(0x0000), 0x7F80)
    chk("rsqrt -den", bf_rsqrt(0x8001), 0xFF80)
    chk("rsqrt -1", bf_rsqrt(0xBF80), 0xFFC0)
    chk("rsqrt -inf", bf_rsqrt(0xFF80), 0xFFC0)
    chk("rsqrt +inf", bf_rsqrt(0x7F80), 0x0000)
    for k in range(-63, 64):
        chk("rsqrt 2^%d" % (2 * k), bf_rsqrt(exact_bits(P2(2 * k), BF16)), exact_bits(P2(-k), BF16))
    # --- MINMAX (spec 5.2, Tables 11.3-11.6)
    f = F32
    pz, nz, one, two, m2 = 0, 0x80000000, 0x3F800000, 0x40000000, 0xC0000000
    qn1, qn2, sn1, sn2 = 0x7FC00001, 0x7FC00002, 0x7F800003, 0x7F800004
    for imm, want in ((0x00, nz), (0x10, nz), (0x02, nz), (0x12, nz), (0x01, pz), (0x11, pz), (0x03, pz), (0x13, pz)):
        chk("T11.5 imm=%02X" % imm, minmax(nz, pz, imm | 0x04, f, False, True)[0], want)   # sc=01 keeps sign
        chk("T11.5r imm=%02X" % imm, minmax(pz, nz, imm | 0x04, f, False, True)[0], want)
    for imm, want in ((0x00, m2), (0x10, m2), (0x02, m2), (0x12, m2), (0x01, two), (0x11, two), (0x03, two),
                      (0x13, two)):
        chk("T11.6 imm=%02X" % imm, minmax(two, m2, imm | 0x04, f, False, True)[0], want)
    chk("T11.3 s1 s2", minmax(sn1, sn2, 0, f, False, True)[:2], (quiet(sn1, f), IE))
    chk("T11.3 s1 q2", minmax(sn1, qn2, 1, f, False, True)[:2], (quiet(sn1, f), IE))
    chk("T11.3 q1 s2", minmax(qn1, sn2, 2, f, False, True)[:2], (quiet(sn2, f), IE))
    chk("T11.3 q1 q2", minmax(qn1, qn2, 3, f, False, True)[:2], (qn1, 0))
    chk("T11.3 n1 q2", minmax(one, qn2, 0x0C, f, False, True)[:2], (qn2, 0))     # sign control ignored
    chk("T11.4 s1 n2", minmax(sn1, m2, 0x10, f, False, True)[:2], (m2, IE))      # sc=00 ignored
    chk("T11.4 s1 n2 sc=10", minmax(sn1, m2, 0x18, f, False, True)[:2], (two, IE))
    chk("T11.4 q1 n2", minmax(qn1, two, 0x1D, f, False, True)[:2], (two | 0x80000000, 0))
    chk("T11.4 n1 s2", minmax(m2, sn2, 0x11, f, False, True)[:2], (m2, IE))
    chk("T11.4 q1 q2", minmax(qn1, qn2, 0x10, f, False, True)[:2], (qn1, 0))
    chk("T11.4 q1 s2", minmax(qn1, sn2, 0x13, f, False, True)[:2], (quiet(sn2, f), IE))
    chk("sign ctl 00 = src1", minmax(m2, one, 0x01, f, False, True)[0], 0xBF800000)
    chk("sign ctl 10", minmax(m2, one, 0x08, f, False, True)[0], two)
    chk("sign ctl 11", minmax(m2, one, 0x0D, f, False, True)[0], 0xBF800000)
    chk("DE", minmax(1, one, 0, f, False, True)[1], DE)
    chk("DAZ no DE", minmax(1, one, 0, f, True, True)[:2], (0, 0))
    chk("QNaN blocks DE", minmax(1, qn1, 0, f, False, True)[1], 0)
    chk("minmag -1 vs 2", minmax(0xBF800000, two, 0x06, f, False, True)[0], 0xBF800000)
    chk("maxmag -3 vs 2", minmax(0xC0400000, two, 0x07, f, False, True)[0], 0xC0400000)
    chk("bf16 no flags", minmax(0x7F81, 0x3F80, 0, BF16, True, False)[:2], (0x7FC1, 0))
    # --- saturation of each conversion helper (spec 5.3)
    e64 = lambda v: exact_bits(v, F64)         # noqa: E731
    e32 = lambda v: exact_bits(v, F32)         # noqa: E731
    e16 = lambda v: exact_bits(v, FP16)        # noqa: E731
    chk("DW_S 2^31", cvt_literal("DW_S", e32(P2(31)), F32, RZ), (0x7FFFFFFF, IE))
    chk("DW_S -2^31", cvt_literal("DW_S", e32(-P2(31)), F32, RZ), (0x80000000, 0))
    chk("DW_S -2^31-256", cvt_literal("DW_S", e32(-P2(31) - 256), F32, RZ), (0x80000000, IE))
    chk("DW_S nan", cvt_literal("DW_S", F32.qnan_indef, F32, RZ), (0, IE))
    chk("DW_S -1.5", cvt_literal("DW_S", e32(Fraction(-3, 2)), F32, RZ), (0xFFFFFFFF, PE))
    chk("DW_S fp64 2^31-0.5 (literal, no PE)", cvt_literal("DW_S", e64(P2(31) - HALF), F64, RZ), (0x7FFFFFFF, 0))
    chk("DW_S fp64 -2^31-0.5 (literal)", cvt_literal("DW_S", e64(-P2(31) - HALF), F64, RZ), (0x80000000, 0))
    chk("DW_S fp64 -2^31-1", cvt_literal("DW_S", e64(-P2(31) - 1), F64, RZ), (0x80000000, IE))
    chk("DW_U -1", cvt_literal("DW_U", e32(-1), F32, RZ), (0, IE))
    chk("DW_U -0.5 (literal)", cvt_literal("DW_U", e32(-HALF), F32, RZ), (0, 0))
    chk("DW_U 2^32", cvt_literal("DW_U", e32(P2(32)), F32, RZ), (0xFFFFFFFF, IE))
    chk("DW_U 2^32-256", cvt_literal("DW_U", e32(P2(32) - 256), F32, RZ), (0xFFFFFF00, 0))
    chk("DW_U_PD -0.5", cvt_literal("DW_U_PD", e64(-HALF), F64, RZ), (0, PE))
    chk("DW_U_PD -1", cvt_literal("DW_U_PD", e64(-1), F64, RZ), (0, IE | PE))
    chk("DW_U_PD 2^32-0.5", cvt_literal("DW_U_PD", e64(P2(32) - HALF), F64, RZ), (0xFFFFFFFF, PE))
    chk("QW_S 3e9 (typo fixed)", cvt_literal("QW_S", e32(3000000000), F32, RZ), (3000000000, 0))
    chk("QW_S 2^63", cvt_literal("QW_S", e64(P2(63)), F64, RZ), (0x7FFFFFFFFFFFFFFF, IE))
    chk("QW_S -2^63", cvt_literal("QW_S", e64(-P2(63)), F64, RZ), (0x8000000000000000, 0))
    chk("QW_S 1.0 (typo fixed)", cvt_literal("QW_S", e64(1), F64, RZ), (1, 0))
    chk("QW_U 2^64", cvt_literal("QW_U", e32(P2(64)), F32, RZ), (0xFFFFFFFFFFFFFFFF, IE))
    chk("QW_U_PD 2^64-2048", cvt_literal("QW_U_PD", e64(P2(64) - 2048), F64, RZ), (0xFFFFFFFFFFFFF800, 0))
    chk("QW_U_PD -inf", cvt_literal("QW_U_PD", F64.signbit | F64.inf, F64, RZ), (0, IE))
    chk("B_S_R 127.5 RNE", cvt_literal("B_S_R", e32(Fraction(255, 2)), F32, RNE), (0x7F, IE))
    chk("B_S_R 126.5 RNE", cvt_literal("B_S_R", e32(Fraction(253, 2)), F32, RNE), (126, PE))
    chk("B_S_R -128.5 RNE (literal)", cvt_literal("B_S_R", e32(Fraction(-257, 2)), F32, RNE), (0x80, 0))
    chk("B_S_R 126.2 RU", cvt_literal("B_S_R", e32(Fraction(631, 5)), F32, RU), (127, PE))
    chk("B_S_R 127.2 RU", cvt_literal("B_S_R", e32(Fraction(636, 5)), F32, RU), (0x7F, IE))
    chk("B_S_R -128.2 RD", cvt_literal("B_S_R", e32(Fraction(-641, 5)), F32, RD), (0x80, IE))
    chk("B_S_R -5 ", cvt_literal("B_S_R", e32(-5), F32, RNE), (0xFB, 0))
    chk("B_S_T32 -128.9", cvt_literal("B_S_T32", e32(Fraction(-1289, 10)), F32, RZ), (0x80, 0))
    chk("B_S_T16 127.0 (literal PE)", cvt_literal("B_S_T16", e16(127), FP16, RZ), (0x7F, PE))
    chk("B_S_T16 +inf", cvt_literal("B_S_T16", FP16.inf, FP16, RZ), (0x7F, IE | PE))
    chk("B_U_R16 255.5 RNE", cvt_literal("B_U_R16", e16(Fraction(511, 2)), FP16, RNE), (0xFF, IE))
    chk("B_U_R16 254.5 RNE", cvt_literal("B_U_R16", e16(Fraction(509, 2)), FP16, RNE), (254, PE))
    chk("B_U_R16 -0.25 RD", cvt_literal("B_U_R16", e16(Fraction(-1, 4)), FP16, RD), (0, IE))
    chk("B_U_R32 255.5 RNE (literal)", cvt_literal("B_U_R32", e32(Fraction(511, 2)), F32, RNE), (0xFF, 0))
    chk("B_U_T16 0.75", cvt_literal("B_U_T16", e16(Fraction(3, 4)), FP16, RZ), (0, PE))
    chk("BF_S_R 2.5", bf_cvt_literal("BF_S_R", 0x4020), 2)
    chk("BF_S_R -2.5", bf_cvt_literal("BF_S_R", 0xC020), 0xFE)
    chk("BF_S_T -2.5", bf_cvt_literal("BF_S_T", 0xC020), 0xFE)
    chk("BF_S_T 127.5", bf_cvt_literal("BF_S_T", 0x42FF), 0x7F)
    chk("BF_U_R 255", bf_cvt_literal("BF_U_R", 0x437F), 0xFF)
    chk("BF_U_R -0.5", bf_cvt_literal("BF_U_R", 0xBF00), 0)
    chk("BF_U_T nan", bf_cvt_literal("BF_U_T", 0x7FC0), 0)
    chk("amb detect PS2IUBS 255.5", cvt_lane("B_U_R32", e32(Fraction(511, 2)), F32, RNE, MXCSR_DEFAULT)[2], 0)
    chk("no amb DW_S 1.5", cvt_lane("DW_S", e32(Fraction(3, 2)), F32, RZ, MXCSR_DEFAULT)[2], None)
    # --- VCOMX flag table (spec 8.1.3 lines 5-8)
    chk("comx lt", comx(one, two, f, MXCSR_DEFAULT, True, True), ("lt", 0))
    chk("comx flags lt", COMX_FL["lt"], 0x801)
    chk("comx flags eq", COMX_FL["eq"], 0x8C0)
    chk("comx flags unord", COMX_FL["unord"], 0x885)
    chk("comx qnan", comx(qn1, one, f, MXCSR_DEFAULT, True, True), ("unord", IE))
    chk("ucomx qnan", comx(qn1, one, f, MXCSR_DEFAULT, False, True), ("unord", 0))
    chk("ucomx snan", comx(one, sn2, f, MXCSR_DEFAULT, False, True), ("unord", IE))
    chk("comx -0 +0", comx(nz, pz, f, MXCSR_DEFAULT, True, True), ("eq", 0))
    chk("comx den", comx(1, pz, f, MXCSR_DEFAULT, True, True), ("gt", DE))
    chk("comx den daz", comx(1, pz, f, MXCSR_DEFAULT | DAZ, True, True), ("eq", 0))
    chk("comxsh den daz ignored", comx(1, 0, FP16, MXCSR_DEFAULT | DAZ, True, False), ("gt", DE))
    chk("comx den+qnan", comx(1, qn1, f, MXCSR_DEFAULT, False, True), ("unord", 0))
    print("selftest: %d checks" % n[0])
    return ok


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_bf16()
        gen_cvt()
        gen_minmax()
        gen_comx()
        gen_gating()
        gen_ambiguous()
        out = sys.stdout
        out.write("# AVX10.2 group avx10_a: expected values from the independent spec model\n")
        out.write("# Emulator/tools/isa/ref_avx10_a.py --cases (regenerate, do not edit). Sources: Intel AVX10.2 spec\n")
        out.write("# 361050-007 (rev 7.0) + SDM 092. The i5-13600K has no AVX10: expected-value cases only, run with\n")
        out.write("# AVX10.2 alone (the AVX-512 CPUID bits stay off):\n")
        out.write("#   emu-alltest --cases Emulator%sdata%scases_avx10_a.txt --avx10 2 --expect-only\n" % (BS, BS))
        out.write("# RSI = R14 = MEM + 0x8000. Cases titled AMBIGUOUS-nn test an ambiguous spec point (see the\n")
        out.write("# AMBIGUOUS: comments in ref_avx10_a.py); every other case avoids those inputs.\n")
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
