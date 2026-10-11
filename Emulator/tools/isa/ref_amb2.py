#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_amb2.py -- AMB-2 (plan 1.J.32, ledger U1240-U1243): an independent checker of the DOCUMENTED
requirements of the approximate reciprocal / reciprocal-square-root instructions, an analytic
table-free model, the generator of Emulator/data/cases_amb2.txt and the SDE distance report.
Python 3 stdlib only, exact rational arithmetic (fractions.Fraction, math.isqrt).

Decision A9 (2026-10-09) / AMB-2 (2026-10-10): implement to the documented accuracy with an analytic
model; claim bit-exactness only with authoritative evidence; Intel SDE results are a LABELLED
REFERENCE only (they are never copied into the emulator or into an expected value here).

Sources (the only ones used; written from the document text, not from the emulator's helpers or from
ref_rcp.py, which --selftest uses only as a second, independent opinion):
  SDM 092 (326018-092US, June 2026) Vol2C:
    VRCP14PD 5-674 (Table 5-24), VRCP14PS 5-676 (Table 5-25), VRCP14SD 5-678, VRCP14SS 5-680,
    VRCPPH 5-682 (Table 5-26), VRCPSH 5-684, VRSQRT14PD 5-714 (Table 5-32), VRSQRT14PS 5-716
    (Table 5-33), VRSQRT14SD 5-718 (Table 5-34), VRSQRT14SS 5-720 (Table 5-35), VRSQRTPH 5-722
    (Table 5-36), VRSQRTSH 5-724.
  SDM 092 Vol1 4.8.3.5 Table 4-8 (one-operand NaN rule), 4.9.1.5 (tininess: the result rounded with
  an unbounded exponent), 10.2.3.3 / 10.2.3.4 (FTZ / DAZ).
  AVX10.2 spec 361050-007 (rev 7.0): 7.12 VRCPBF16 p.121-122 (Table 7.2), 7.15 VRSQRTBF16 p.127-128
  (Table 7.3); Operation "//DAZ, FTZ, SAE"; Arithmetic Flags N/A.

Documented requirements checked by check() (strict "less than" bounds, exact rationals):
  all forms   MXCSR.RC ignored; MXCSR flags not affected; no #XM ("SIMD Floating-Point Exceptions:
              None"; BF16: flags N/A) -- instruction level, checked by the case file / unit tests.
              SNaN -> the QNaN of the same payload, QNaN -> itself (14 pages; PH / BF16 pages have no
              NaN row: the one-operand rule of Vol1 Table 4-8).
  VRCP14*     |rel err| < 2^-14; +-0 -> +-INF; 0 <= |X| <= 2^-128 (PS) / 2^-1024 (PD) -> +-INF ("very
              small denormal"); a denormal source is zero only with MXCSR.DAZ; |X| > 2^126 / 2^1022
              -> underflow result of the operand's sign, a zero only with MXCSR.FTZ, otherwise the
              "correct underflow result" (non-zero, inside the bound); X = +-2^-n -> +-2^n;
              +-INF -> +-0 (no table row; the exact value 1/INF = 0 is the only result with a finite
              relative error).
  VRSQRT14*   |rel err| < 2^-14; any denormal -> a normal result; X = 2^-2n -> 2^n; X < 0 including
              -INF -> QNaN indefinite; -0 -> -INF; +0 -> +INF; +INF -> +0 (Tables 5-32..5-35; the
              VRSQRT14SS description's "INF -> zero with the sign of the source" contradicts its own
              Table 5-35 and every other VRSQRT14 page: the table is used); DAZ as VRCP14.
  VRCPPH/SH   |rel err| < 2^-11 + 2^-14; 0 <= |X| <= 2^-16 -> +-INF; +-INF -> +-0 (the table's
              "X > +INF" / "X < -INF"); X = +-2^-n -> +-2^n; FP16 denormal inputs are never DAZ'd and
              results never FTZ'd (Vol2C VCVTPH2PSX "always handle FP16 denormal number inputs").
              |X| > 2^14: 1/X is an FP16 denormal; the denormal grid has <= 10 significant bits and no
              FP16 value meets the bound for most of these inputs: the checker requires one of the two
              FP16 neighbours of 1/X (faithful) and reports the case as "bound unattainable".
  VRSQRTPH/SH |rel err| < 2^-11 + 2^-14; Table 5-36 (= the VRSQRT14 table); no DAZ.
  VRCPBF16    |rel err| < 2^-8 + 2^-14; 0 <= X < 2^-126 -> +INF, -2^-126 < X <= 0 -> -INF (DAZ);
              +-INF -> +-0; X = +-2^-n -> +-2^n; FTZ: |X| > 2^126 -> a zero of the operand's sign.
  VRSQRTBF16  |rel err| < 2^-8 + 2^-14; 0 <= X < 2^-126 -> +INF, -2^-126 < X <= 0 -> -INF (DAZ);
              X = 2^-2n -> 2^n; X < 0 including -INF -> QNaN indefinite; +INF -> +0.

Analytic model model() (the emulator's documented stand-in, decision A9; NOT Intel's bits): the exact
1/x resp. 1/sqrt(x) rounded to nearest-even to the destination precision p with an unbounded exponent
(SDM Vol1 4.9.1.5); overflow -> INF; a tiny rounded result is a zero of the operand's sign when FTZ
acts (VRCP14 with MXCSR.FTZ, every BF16 form), otherwise the exact value rounded ONCE at the denormal
quantum. One rounding gives |rel err| <= 2^-p for every normal result (2^-24, 2^-53, 2^-11, 2^-8), inside
every documented bound. No table, no measured constant, no host floating point.

usage: ref_amb2.py --selftest
       ref_amb2.py --cases [OUT]           (default: Emulator/data/cases_amb2.txt)
       ref_amb2.py --sde-report LOG [...]  per-form SDE (labelled reference) vs model and vs the
                                           documented requirements, from emu-alltest pass-1 or pass-2
                                           logs of the U1040 SDE cross-check
"""
import os
import re
import sys
from decimal import Decimal, getcontext
from fractions import Fraction
from math import isqrt

HERE = os.path.dirname(os.path.abspath(__file__))
getcontext().prec = 80
CRLF, LF = bytes([13, 10]), bytes([10])


# ----------------------------------------------------------------------------- formats
class Fmt:
    def __init__(s, name, bits, p, ebits):
        s.name, s.bits, s.p, s.ebits = name, bits, p, ebits
        s.bias = (1 << (ebits - 1)) - 1
        s.emin, s.emax = 1 - s.bias, s.bias
        s.fbits = p - 1
        s.fmask = (1 << s.fbits) - 1
        s.sign = 1 << (bits - 1)
        s.emask = ((1 << ebits) - 1) << s.fbits
        s.inf = s.emask
        s.qbit = 1 << (s.fbits - 1)
        s.indef = s.sign | s.emask | s.qbit
        s.qexp = s.emin - s.fbits            # exponent of the denormal quantum
        s.nbytes = bits // 8


F16 = Fmt("fp16", 16, 11, 5)
BF16 = Fmt("bf16", 16, 8, 8)
F32 = Fmt("fp32", 32, 24, 8)
F64 = Fmt("fp64", 64, 53, 11)
DAZ, FTZ = 0x40, 0x8000


def p2(k):
    return Fraction(1 << k) if k >= 0 else Fraction(1, 1 << -k)


def flog2(v):
    """floor(log2(v)) for a Fraction v > 0"""
    k = v.numerator.bit_length() - v.denominator.bit_length()
    return k - 1 if v < p2(k) else k


def rne(v):
    """nearest integer to the Fraction v >= 0, ties to even"""
    n = v.numerator // v.denominator
    r = v - n
    if r > Fraction(1, 2) or (r == Fraction(1, 2) and (n & 1)):
        n += 1
    return n


def rne_sqrt(t):
    """nearest integer to sqrt(t) for a Fraction t >= 0, ties to even (floor(sqrt(t)) =
    isqrt(floor(t)); the tie (n + 1/2)^2 = t is checked exactly)"""
    n = isqrt(t.numerator // t.denominator)
    h = Fraction((2 * n + 1) ** 2, 4)
    if t > h or (t == h and (n & 1)):
        n += 1
    return n


def kind(f, x):
    e = x & f.emask
    m = x & f.fmask
    if e == f.emask:
        return "nan" if m else "inf"
    if e == 0:
        return "den" if m else "zero"
    return "norm"


def val(f, x):
    """exact value of a finite encoding"""
    e = (x & f.emask) >> f.fbits
    m = x & f.fmask
    v = Fraction(m) * p2(f.qexp) if e == 0 else Fraction(m | (1 << f.fbits)) * p2(e - f.bias - f.fbits)
    return -v if x & f.sign else v


def enc_normal(f, sign, n, q):
    """n * 2^q with n in [2^fbits, 2^p): the normal encoding (caller checks the range)"""
    return sign | ((q + f.fbits + f.bias) << f.fbits) | (n & f.fmask)


def round_rat(f, v, ftz):
    """RNE of the non-zero Fraction v: p bits, unbounded exponent; overflow -> INF; tiny after that
    rounding -> zero (ftz) or v rounded once at the denormal quantum"""
    sign = f.sign if v < 0 else 0
    a = -v if v < 0 else v
    q = flog2(a) - f.fbits
    n = rne(a / p2(q))
    if n == 1 << f.p:
        n >>= 1
        q += 1
    lead = q + f.fbits
    if lead > f.emax:
        return sign | f.inf
    if lead < f.emin:
        if ftz:
            return sign
        return sign | rne(a / p2(f.qexp))    # 2^fbits = the smallest normal, same bits
    return enc_normal(f, sign, n, q)


def round_rsqrt(f, a):
    """RNE of 1/sqrt(a), a > 0 a Fraction, to p bits (never tiny or overflowing for these formats)"""
    L = flog2(1 / a)                          # 2^L <= 1/a < 2^(L+1)
    q = L // 2 - f.fbits                      # 2^(L//2) <= 1/sqrt(a) < 2^(L//2 + 1)
    n = rne_sqrt(p2(-2 * q) / a)
    if n == 1 << f.p:
        n >>= 1
        q += 1
    lead = q + f.fbits
    assert f.emin <= lead <= f.emax
    return enc_normal(f, 0, n, q)


# ----------------------------------------------------------------------------- forms
# name: (format, operation, family, map, pp, W, opcode, scalar, document reference)
FORMS = {
    "VRCP14PS": (F32, "rcp", "14", 2, 1, 0, 0x4C, False, "SDM 092 Vol2C 5-676, Table 5-25"),
    "VRCP14PD": (F64, "rcp", "14", 2, 1, 1, 0x4C, False, "SDM 092 Vol2C 5-674, Table 5-24"),
    "VRCP14SS": (F32, "rcp", "14", 2, 1, 0, 0x4D, True, "SDM 092 Vol2C 5-680, Table 5-25"),
    "VRCP14SD": (F64, "rcp", "14", 2, 1, 1, 0x4D, True, "SDM 092 Vol2C 5-678, Table 5-24"),
    "VRSQRT14PS": (F32, "rsqrt", "14", 2, 1, 0, 0x4E, False, "SDM 092 Vol2C 5-716, Table 5-33"),
    "VRSQRT14PD": (F64, "rsqrt", "14", 2, 1, 1, 0x4E, False, "SDM 092 Vol2C 5-714, Table 5-32"),
    "VRSQRT14SS": (F32, "rsqrt", "14", 2, 1, 0, 0x4F, True, "SDM 092 Vol2C 5-720, Table 5-35"),
    "VRSQRT14SD": (F64, "rsqrt", "14", 2, 1, 1, 0x4F, True, "SDM 092 Vol2C 5-718, Table 5-34"),
    "VRCPPH": (F16, "rcp", "ph", 6, 1, 0, 0x4C, False, "SDM 092 Vol2C 5-682, Table 5-26"),
    "VRCPSH": (F16, "rcp", "ph", 6, 1, 0, 0x4D, True, "SDM 092 Vol2C 5-684, Table 5-26"),
    "VRSQRTPH": (F16, "rsqrt", "ph", 6, 1, 0, 0x4E, False, "SDM 092 Vol2C 5-722, Table 5-36"),
    "VRSQRTSH": (F16, "rsqrt", "ph", 6, 1, 0, 0x4F, True, "SDM 092 Vol2C 5-724, Table 5-36"),
    "VRCPBF16": (BF16, "rcp", "bf16", 6, 0, 0, 0x4C, False, "AVX10.2 spec 361050-007 7.12 p.121, Table 7.2"),
    "VRSQRTBF16": (BF16, "rsqrt", "bf16", 6, 0, 0, 0x4E, False, "AVX10.2 spec 361050-007 7.15 p.127, Table 7.3"),
}
BOUND = {"14": p2(-14), "ph": p2(-11) + p2(-14), "bf16": p2(-8) + p2(-14)}
BOUND_TEXT = {"14": "2^-14", "ph": "2^-11 + 2^-14", "bf16": "2^-8 + 2^-14"}


def form_by_encoding(mp, pp, W, opc):
    for name, d in FORMS.items():
        if d[3] == mp and d[4] == pp and d[5] == W and d[6] == opc:
            return name
    return None


def eff_daz(fam, mxcsr):
    return fam == "bf16" or (fam == "14" and bool(mxcsr & DAZ))


def eff_ftz(fam, mxcsr):
    return fam == "bf16" or (fam == "14" and bool(mxcsr & FTZ))


# ----------------------------------------------------------------------------- the analytic model
def model(name, x, mxcsr=0x1F80):
    f, op, fam = FORMS[name][:3]
    k = kind(f, x)
    if k == "nan":
        return x | f.qbit
    s = x & f.sign
    if k == "zero" or (k == "den" and eff_daz(fam, mxcsr)):
        return s | f.inf
    if op == "rsqrt":
        if s:
            return f.indef
        if k == "inf":
            return 0
        return round_rsqrt(f, val(f, x))
    if k == "inf":
        return s
    return round_rat(f, 1 / val(f, x), eff_ftz(fam, mxcsr))


# ----------------------------------------------------------------------------- the checker
def relerr_rcp(f, x, r):
    """exact |r * x - 1| (= |r - 1/x| / |1/x|)"""
    return abs(val(f, r) * val(f, x) - 1)


def rsqrt_in_bound(f, x, r, b):
    """|r - 1/sqrt(x)| / (1/sqrt(x)) < b  <=>  (1 - b)^2 < r^2 x < (1 + b)^2  (r > 0)"""
    t = val(f, r) ** 2 * val(f, x)
    return (1 - b) ** 2 < t < (1 + b) ** 2


def relerr_rsqrt(f, x, r):
    t = val(f, r) ** 2 * val(f, x)
    return abs((Decimal(t.numerator) / Decimal(t.denominator)).sqrt() - 1)


def check(name, x, mxcsr, r):
    """-> (ok, rule, relerr) for result r of form name on element x under mxcsr; relerr (a Fraction or
    Decimal, |r - exact| / |exact|) is given for finite non-zero exact values, else None"""
    f, op, fam = FORMS[name][:3]
    B = BOUND[fam]
    k = kind(f, x)
    s = x & f.sign

    def req(want, rule):
        return (r == want, rule, None)

    if k == "nan":
        return req(x | f.qbit, "NaN: SNaN -> QNaN (same payload), QNaN -> itself")
    if k == "den" and eff_daz(fam, mxcsr):
        rule = "DAZ: denormal source = zero of its sign -> %sINF" % ("-" if s else "+")
        return req(s | f.inf, rule + (" (Table 7.2 / 7.3)" if fam == "bf16" else " (MXCSR.DAZ)"))
    if k == "zero":
        return req(s | f.inf, "%s0 -> %sINF" % ("-" if s else "+", "-" if s else "+"))
    if op == "rsqrt":
        if s:
            return req(f.indef, "X < 0 (including -INF, negative denormals) -> QNaN indefinite")
        if k == "inf":
            return req(0, "+INF -> +0")
        v = val(f, x)
        m = x & f.fmask
        if k == "norm" and m == 0 and ((x & f.emask) >> f.fbits) % 2 == f.bias % 2:
            # X = 2^-2n (even unbiased exponent) -> 2^n exactly
            e = ((x & f.emask) >> f.fbits) - f.bias
            return req(enc_normal(f, 0, 1 << f.fbits, -e // 2 - f.fbits), "X = 2^-2n -> 2^n")
        if k == "den" and v.numerator & (v.numerator - 1) == 0 and flog2(v) % 2 == 0:
            e = flog2(v)
            return req(enc_normal(f, 0, 1 << f.fbits, -e // 2 - f.fbits), "X = 2^-2n -> 2^n (denormal X)")
        if kind(f, r) not in ("norm",) or r & f.sign:
            return (False, "positive finite X -> positive normal result" +
                    (" ('Any denormal -> Normal')" if k == "den" else ""), None)
        ok = rsqrt_in_bound(f, x, r, B)
        return (ok, "|rel err| < %s" % BOUND_TEXT[fam], relerr_rsqrt(f, x, r))
    # reciprocal
    if k == "inf":
        return req(s, "%sINF -> %s0" % ("-" if s else "+", "-" if s else "+"))
    a = abs(val(f, x))
    if fam == "14" and a <= p2(-128 if f is F32 else -1024):
        return req(s | f.inf, "0 <= |X| <= %s -> %sINF (very small denormal)" %
                   ("2^-128" if f is F32 else "2^-1024", "-" if s else "+"))
    if fam == "ph" and a <= p2(-16):
        return req(s | f.inf, "0 <= |X| <= 2^-16 -> %sINF (very small denormal)" % ("-" if s else "+"))
    big = p2(126) if f in (F32, BF16) else (p2(1022) if f is F64 else p2(14))
    if a.numerator == 1 or (a.denominator == 1 and a.numerator & (a.numerator - 1) == 0):
        # X = +-2^-n -> +-2^n whenever 2^n is a representable result (else the rows below decide)
        n = -flog2(a)
        if f.emin <= n <= f.emax:
            return req(enc_normal(f, s, 1 << f.fbits, n - f.fbits), "X = 2^-n -> 2^n")
        if n < f.emin and not (eff_ftz(fam, mxcsr) and a > big) and n >= f.qexp:
            return req(s | (1 << (n - f.qexp)), "X = 2^-n -> 2^n (denormal result)")
    if fam == "bf16" and a > big:
        return req(s, "FTZ: |X| > 2^126 -> %s0 (tiny result)" % ("-" if s else "+"))
    if fam == "14" and a > big:
        if eff_ftz(fam, mxcsr):
            return req(s, "|X| > %s -> underflow, MXCSR.FTZ: %s0" % ("2^126" if f is F32 else "2^1022",
                                                                    "-" if s else "+"))
        if (r & f.sign) != s or kind(f, r) not in ("den", "norm"):
            return (False, "|X| > %s -> correct underflow result with the operand's sign" %
                    ("2^126" if f is F32 else "2^1022"), None)
        e = relerr_rcp(f, x, r)
        return (e < B, "|X| > %s -> correct underflow result, |rel err| < 2^-14" %
                ("2^126" if f is F32 else "2^1022"), e)
    if fam == "ph" and a > big:
        # 1/X < 2^-14: an FP16 denormal; faithful (one of the two neighbours of 1/X) required
        q = p2(F16.qexp)
        t = 1 / a / q
        lo = t.numerator // t.denominator
        hi = lo if t.denominator == 1 else lo + 1
        ok = (r & f.sign) == s and (r & ~f.sign) in (lo, hi) and kind(f, r) != "zero"
        e = relerr_rcp(f, x, r) if kind(f, r) in ("den", "norm") else None
        return (ok, "|X| > 2^14: FP16 denormal result, bound unattainable: a non-zero FP16 neighbour of 1/X", e)
    if (r & f.sign) != s or kind(f, r) not in ("den", "norm"):
        return (False, "finite X -> finite non-zero result of the operand's sign", None)
    e = relerr_rcp(f, x, r)
    return (e < B, "|rel err| < %s" % BOUND_TEXT[fam], e)


# ----------------------------------------------------------------------------- input sets
class Lcg:
    def __init__(s, seed):
        s.x = seed & 0xFFFFFFFFFFFFFFFF

    def next(s, bits):
        out = 0
        for _ in range((bits + 31) // 32):
            s.x = (s.x * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
            out = (out << 32) | (s.x >> 32)
        return out & ((1 << bits) - 1)


def edge_mantissas(f):
    fb = f.fbits
    m = [0, 1, 2, 3, f.fmask, f.fmask - 1, 1 << (fb - 1), (1 << (fb - 1)) - 1, (1 << (fb - 1)) + 1,
         1 << (fb - 2), (3 << (fb - 2)) - 1, 3 << (fb - 2), f.fmask // 3, 2 * (f.fmask // 3),
         f.fmask // 5, 1 << (fb // 2)]
    out = []
    for v in m:
        if v not in out:
            out.append(v & f.fmask)
    return out


def table_inputs(name):
    """documented table rows and the boundaries on both sides of every threshold"""
    f, op, fam = FORMS[name][:3]
    E = lambda e, m=0, s=0: (s and f.sign) | (((e + f.bias) & ((1 << f.ebits) - 1)) << f.fbits) | m
    v = [0, f.sign, 1, f.sign | 1, f.fmask, f.sign | f.fmask, 1 << f.fbits, f.sign | (1 << f.fbits),
         f.inf, f.sign | f.inf, f.inf | 1, f.inf | f.qbit | 5, f.sign | f.inf | 3, f.indef,
         E(0), E(0, 0, 1), E(1), E(-1), E(2), E(-2), E(3), E(-3), E(0, 1), E(1, f.fmask), E(f.emax, f.fmask),
         f.sign | E(f.emax, f.fmask), E(f.emax), E(f.emin), E(f.emin + 1), E(-1, f.fmask)]
    if fam == "14":
        lim = -128 if f is F32 else -1024              # 0 <= X <= 2^lim -> INF
        d = lim - f.qexp                                # 2^lim as a denormal: bit d
        big = f.emax - 1                                # |X| > 2^big -> underflow
        v += [1 << d, (1 << d) + 1, (1 << d) - 1, f.sign | (1 << d), f.sign | ((1 << d) + 1),
              E(big), E(big, 1), E(big, 0, 1), E(big, 1, 1), E(f.emax), E(f.emax, 1 << (f.fbits - 1)),
              E(big, f.fmask // 3), 1 << (f.fbits - 1), (1 << (f.fbits - 1)) | 1]
    elif fam == "ph":
        v += [1 << 8, (1 << 8) + 1, (1 << 8) - 1, f.sign | (1 << 8), f.sign | ((1 << 8) + 1),
              E(14), E(14, 1), E(15), E(14, 0x2AA), E(15, 0x155), f.sign | E(14, 1), E(-14, 1), 1 << 9,
              E(-13)]
    else:
        v += [E(126), E(126, 1), f.sign | E(126, 1), E(127), E(-126), E(-126, 1), f.sign | E(-126, 1),
              E(-125), E(-127 + 1)]
    out = []
    for x in v:
        x &= (1 << f.bits) - 1
        if x not in out:
            out.append(x)
    return out


def sweep_inputs(name, per_exp, seed, signs="alt"):
    """every biased exponent x per_exp mantissas (edge ones first, then LCG random)"""
    f = FORMS[name][0]
    g = Lcg(seed)
    em = edge_mantissas(f)
    out = []
    for e in range(1 << f.ebits):
        ms = em[:per_exp]
        while len(ms) < per_exp:
            ms.append(g.next(f.fbits))
        for i, m in enumerate(ms):
            if signs == "alt":
                s = f.sign if (i + e) & 1 else 0
            else:                                       # rsqrt: mostly positive
                s = f.sign if (i % 8) == 7 else 0
            out.append(s | (e << f.fbits) | m)
    return out


# ----------------------------------------------------------------------------- encoding / case lines
def evex(name, ll, reg, rm, vvvv=0, aaa=0, z=0):
    f, op, fam, mp, pp, W, opc, scalar = FORMS[name][:8]
    p0 = ((0 if reg & 8 else 1) << 7) | ((0 if rm & 16 else 1) << 6) | ((0 if rm & 8 else 1) << 5) | \
         ((0 if reg & 16 else 1) << 4) | mp
    p1 = (W << 7) | (((~vvvv) & 15) << 3) | 4 | pp
    p2b = (z << 7) | (ll << 5) | ((0 if vvvv & 16 else 1) << 3) | aaa
    modrm = 0xC0 | ((reg & 7) << 3) | (rm & 7)
    return [0x62, p0, p1, p2b, opc, modrm]


def hexle(vals, nbytes):
    return "".join(v.to_bytes(nbytes, "little").hex().upper() for v in vals)


def zmm_hex(lanes, f):
    """64-byte register from lane values (missing lanes zero)"""
    n = 64 // f.nbytes
    return hexle(list(lanes) + [0] * (n - len(lanes)), f.nbytes)


def lanes_of(hx, f):
    b = bytes.fromhex(hx)
    return [int.from_bytes(b[i:i + f.nbytes], "little") for i in range(0, len(b), f.nbytes)]


MX_NAMES = {0x1F80: "default", 0x1FC0: "DAZ", 0x9F80: "FTZ", 0x9FC0: "DAZ+FTZ",
            0x0000: "all exceptions unmasked", 0x6000: "RZ, all exceptions unmasked",
            0x7FBF: "RZ, every flag set, DAZ off", 0x3F80: "RD", 0x5F80: "RU"}


class Gen:
    def __init__(s):
        s.lines = []
        s.rng = Lcg(0xA3B2)

    def rnd_zmm(s):
        return "".join("%016X" % s.rng.next(64) for _ in range(8))

    def packed(s, name, xs, mxcsr, ll=2, aaa=0, z=0, kval=None, note=""):
        f = FORMS[name][0]
        n = (16 << ll) // f.nbytes
        dst = s.rnd_zmm()
        dl = lanes_of(dst, f)
        res = []
        for j in range(64 // f.nbytes):
            if j >= n:
                res.append(0)
            elif aaa and not (kval >> j) & 1:
                res.append(0 if z else dl[j])
            else:
                res.append(model(name, xs[j] if j < len(xs) else 0, mxcsr))
        src = zmm_hex(xs[:n] + [0] * (n - len(xs[:n])), f)
        b = evex(name, ll, 1, 2, aaa=aaa, z=z)
        k = " k1=0x%X" % kval if aaa else ""
        s.lines.append("# %s VL%d reg%s, MXCSR %04X (%s)%s" % (name, 128 << ll, " {k1}" + ("{z}" if z else "") if aaa else "",
                                                                 mxcsr, MX_NAMES.get(mxcsr, "?"), note))
        s.lines.append(".byte %s | zmm1=%s zmm2=%s%s mxcsr=0x%04X => zmm1=%s" % (
            ", ".join("0x%02x" % c for c in b), dst, src, k, mxcsr, zmm_hex(res, f)))

    def scalar(s, name, x, mxcsr, note=""):
        f = FORMS[name][0]
        dst, s1, s2 = s.rnd_zmm(), s.rnd_zmm(), s.rnd_zmm()
        l1 = lanes_of(s1, f)
        l2 = lanes_of(s2, f)
        l2[0] = x
        n = 16 // f.nbytes
        res = [model(name, x, mxcsr)] + l1[1:n] + [0] * (64 // f.nbytes - n)
        b = evex(name, 0, 1, 2, vvvv=3)
        s.lines.append("# %s xmm1, xmm3, xmm2: X = %0*X, MXCSR %04X (%s)%s" % (name, f.nbytes * 2, x, mxcsr,
                                                                             MX_NAMES.get(mxcsr, "?"), note))
        s.lines.append(".byte %s | zmm1=%s zmm2=%s zmm3=%s mxcsr=0x%04X => zmm1=%s" % (
            ", ".join("0x%02x" % c for c in b), dst, hexle(l2, f.nbytes), s1, mxcsr, zmm_hex(res, f)))


TABLE_MX = [0x1F80, 0x1FC0, 0x9F80, 0x9FC0, 0x0000, 0x6000, 0x7FBF]
SCALAR_MX = [0x1F80, 0x9FC0, 0x0000]


def gen_cases():
    g = Gen()
    L = g.lines
    L.append("# === A. documented special-case rows and their boundaries, every form, %d MXCSR settings" % len(TABLE_MX))
    for name, d in FORMS.items():
        if d[7]:
            continue
        f = d[0]
        xs = table_inputs(name)
        n = 64 // f.nbytes
        for mx in TABLE_MX:
            for i in range(0, len(xs), n):
                g.packed(name, xs[i:i + n], mx, note=" [%s]" % d[8])
    L.append("# === B. scalar forms: every row input x %d MXCSR settings; bits 127:esz from SRC1 (zmm3), 511:128 zero" % len(SCALAR_MX))
    for name, d in FORMS.items():
        if not d[7]:
            continue
        for x in table_inputs(name):
            for mx in SCALAR_MX:
                g.scalar(name, x, mx)
    L.append("# === C. VL128 / VL256, merging and zeroing masks (k1)")
    for name, d in FORMS.items():
        if d[7]:
            continue
        f = d[0]
        xs = table_inputs(name)
        for ll in (0, 1):
            g.packed(name, xs, 0x1F80, ll=ll, aaa=1, z=0, kval=0xA5C3A5C3A5C3A5C3 & ((1 << (64 // f.nbytes)) - 1))
            g.packed(name, xs, 0x1F80, ll=ll, aaa=1, z=1, kval=0x3C5A3C5A3C5A3C5A & ((1 << (64 // f.nbytes)) - 1))
    L.append("# === D. distance sample (inputs of the SDE labelled-reference run): every biased exponent x")
    L.append("#     edge + LCG mantissas, MXCSR default (FP16 / BF16: cases_rcp_f16_all.txt / cases_rcp_bf16.txt")
    L.append("#     already hold every encoding)")
    for name, per, sig in (("VRCP14PS", 64, "alt"), ("VRSQRT14PS", 64, "pos"),
                           ("VRCP14PD", 4, "alt"), ("VRSQRT14PD", 4, "pos")):
        f = FORMS[name][0]
        xs = sweep_inputs(name, per, 0x5EED0000 + FORMS[name][6] + 0x100 * FORMS[name][5], sig)
        n = 64 // f.nbytes
        for i in range(0, len(xs), n):
            g.packed(name, xs[i:i + n], 0x1F80, note=" sample")
    return g.lines


def write_cases(path):
    hdr = [
        "# AMB-2 (plan 1.J.32, ledger U1240-U1243): approximate reciprocal / reciprocal square root, every form",
        "# (VRCP14PS/PD/SS/SD, VRSQRT14PS/PD/SS/SD, VRCPPH/SH, VRSQRTPH/SH, VRCPBF16, VRSQRTBF16). Generated by",
        "# Emulator%stools%sisa%sref_amb2.py --cases (regenerate, do not edit). Expected values: the analytic" % ((chr(92),) * 3),
        "# model of decision A9 (exact value rounded once to nearest-even; documented compliance, NOT Intel's bits);",
        "# ref_amb2.py --selftest proves every one of them against the documented requirements (SDM 092 Vol2C",
        "# Tables 5-24..5-26, 5-32..5-36; AVX10.2 spec 361050-007 Tables 7.2 / 7.3): bound, special cases, DAZ / FTZ,",
        "# MXCSR.RC ignored; a strict line also checks MXCSR unchanged (no flag) and no #XM with every exception",
        "# unmasked. Section D is the input set of the Intel SDE labelled-reference run (ref_amb2.py --sde-report).",
        "#   emu-alltest --cases Emulator%sdata%scases_amb2.txt --avx10 2 --expect-only" % (chr(92), chr(92)),
    ]
    lines = gen_cases()
    with open(path, "w", newline="\r\n") as fh:      # CRLF, as git (core.autocrlf) checks it out
        fh.write("\n".join(hdr + lines) + "\n")
    return sum(1 for l in lines if not l.startswith("#"))


# ----------------------------------------------------------------------------- selftest
FAILS = []


def expect(cond, what):
    if not cond:
        FAILS.append(what)
        if len(FAILS) <= 20:
            print("FAIL:", what)


def selftest():
    import random
    rnd = random.Random(0xA3B2)
    n = 0
    worst = {}

    def run(name, xs, mxs):
        nonlocal n
        fam = FORMS[name][2]
        for mx in mxs:
            for x in xs:
                r = model(name, x, mx)
                ok, rule, e = check(name, x, mx, r)
                n += 1
                expect(ok, "%s model fails its documented requirement: x=%X mxcsr=%04X r=%X (%s)" %
                       (name, x, mx, r, rule))
                if e is not None and rule.startswith("|rel err|"):
                    w = worst.get(name)
                    if w is None or float(e) > w[0]:
                        worst[name] = (float(e), x)

    # (1) every FP16 / BF16 encoding; MXCSR must not matter for PH (no DAZ / FTZ) nor BF16 (always both)
    for name in ("VRCPPH", "VRSQRTPH", "VRCPBF16", "VRSQRTBF16"):
        run(name, range(1 << 16), [0x1F80])
        for x in range(0, 1 << 16, 7):
            expect(model(name, x, 0x9FC0) == model(name, x, 0x1F80) == model(name, x, 0x6000),
                   "%s depends on MXCSR at %04X" % (name, x))
    # (2) FP32 / FP64: every exponent x edge + random mantissas x 2 signs x 4 MXCSR, plus the table rows
    for name in ("VRCP14PS", "VRSQRT14PS"):
        xs = sweep_inputs(name, 24, 0x1234, "alt") + table_inputs(name)
        xs += [x ^ F32.sign for x in xs[:4096]]
        run(name, xs, [0x1F80, 0x1FC0, 0x9F80, 0x9FC0])
    for name in ("VRCP14PD", "VRSQRT14PD"):
        xs = sweep_inputs(name, 3, 0x4321, "alt") + table_inputs(name)
        run(name, xs, [0x1F80, 0x1FC0, 0x9F80, 0x9FC0])
    # (3) the underflow region exhaustively at its two ends (FP32): 2^126 .. +4096 ulp, max - 4096 ulp
    run("VRCP14PS", list(range(0x7E800000, 0x7E801000)) + list(range(0x7F7FF000, 0x7F800000)),
        [0x1F80, 0x9F80])
    # (4) the documented worst-case bound per normal result (one rounding): <= 2^-p
    for name, (e, x) in sorted(worst.items()):
        f = FORMS[name][0]
        expect(e <= float(p2(-f.p)), "%s worst relative error %r at %X exceeds 2^-p" % (name, e, x))
    # (5) the checker is sensitive: it rejects results outside the documented requirements
    def mutate(name, x, mx, r, label):
        ok, rule, _ = check(name, x, mx, r)
        expect(not ok, "%s checker accepted a wrong result (%s): x=%X r=%X" % (name, label, x, r))

    for name in FORMS:
        f, op, fam = FORMS[name][:3]
        bits = (1 << f.bits) - 1
        one = enc_normal(f, 0, 1 << f.fbits, -f.fbits)
        # a normal input with a normal result: move the result by just over the bound
        x = enc_normal(f, 0, (1 << f.fbits) | (f.fmask // 3), -f.fbits + 1)    # ~2.67
        r = model(name, x)
        b_ulps = int(BOUND[fam] * (1 << f.fbits)) + 2                           # > bound in ulps
        mutate(name, x, 0x1F80, r + 2 * b_ulps, "relative error beyond the bound")
        mutate(name, x, 0x1F80, r ^ f.sign, "wrong sign")
        mutate(name, x, 0x1F80, 0, "zero for a finite result")
        mutate(name, x, 0x1F80, f.inf, "INF for a finite result")
        within = int(BOUND[fam] * (1 << f.fbits)) // 3                          # inside the bound
        if within:                                                              # (FP16 / BF16: no room)
            expect(check(name, x, 0x1F80, r + within)[0], "%s checker rejected a result inside the bound" % name)
        mutate(name, 0, 0x1F80, 0, "+0 -> +0")
        mutate(name, f.sign, 0x1F80, f.inf, "-0 -> +INF")
        mutate(name, f.inf | 1, 0x1F80, f.inf | 1, "SNaN not quieted")
        mutate(name, f.inf | 1, 0x1F80, f.indef, "NaN payload lost")
        mutate(name, 1 << (f.fbits - 1), 0x1FC0, f.inf if fam == "ph" else f.indef,
               "denormal source: DAZ'd for FP16 / not DAZ'd for VRCP14 / BF16")
        if op == "rsqrt":
            mutate(name, f.sign | one, 0x1F80, f.sign | one, "negative X not indefinite")
            mutate(name, f.sign | f.inf, 0x1F80, f.sign, "-INF not indefinite")
            mutate(name, f.inf, 0x1F80, f.sign, "+INF -> -0")
            x4 = enc_normal(f, 0, 1 << f.fbits, 2 - f.fbits)                    # 4 -> exactly 1/2
            mutate(name, x4, 0x1F80, model(name, x4) + 1, "2^-2n -> 2^n not exact")
        else:
            mutate(name, f.inf, 0x1F80, f.sign, "+INF -> -0")
            x2 = enc_normal(f, 0, 1 << f.fbits, 3 - f.fbits)                    # 8 -> exactly 1/8
            mutate(name, x2, 0x1F80, model(name, x2) + 1, "2^-n -> 2^n not exact")
            if fam == "14":
                big = enc_normal(f, 0, (1 << f.fbits) | 1, f.emax - 1 - f.fbits)
                mutate(name, big, 0x9F80, model(name, big, 0x1F80), "FTZ ignored")
                mutate(name, big, 0x1F80, 0, "underflow flushed without FTZ")
                mutate(name, 1, 0x1FC0, model(name, 1, 0x1F80) if kind(f, model(name, 1, 0x1F80)) != "inf"
                       else 0, "DAZ ignored / wrong")
            if fam == "bf16":
                big = enc_normal(f, 0, (1 << f.fbits) | 1, 126 - f.fbits)
                mutate(name, big, 0x1F80, 1, "BF16 tiny result not flushed")
        # a 12-bit-quality result (legacy RCPPS accuracy) must fail the 14-bit bound
        if fam == "14":
            mutate(name, x, 0x1F80, r + (1 << (f.fbits - 11)), "12-bit accuracy")
    # (6) a second, independent opinion: ref_rcp.py (U941) must agree bit for bit on every FP16 / BF16
    # encoding and on the FP32 / FP64 samples (two models written separately from the same text)
    try:
        sys.path.insert(0, HERE)
        import ref_rcp as R
        pairs = [("VRCPPH", lambda x, mx: R.rcp_ph(x), range(1 << 16)),
                 ("VRSQRTPH", lambda x, mx: R.rsqrt_ph(x), range(1 << 16)),
                 ("VRCPBF16", lambda x, mx: R.rcp_bf16(x), range(1 << 16)),
                 ("VRSQRTBF16", lambda x, mx: R.rsqrt_bf16(x), range(1 << 16))]
        for name, fn, xs in pairs:
            d = sum(1 for x in xs if fn(x, 0) != model(name, x))
            expect(d == 0, "%s: ref_rcp.py differs on %d encodings" % (name, d))
        for name, fn, f in (("VRCP14PS", R.rcp14, R.F32), ("VRSQRT14PS", R.rsqrt14, R.F32),
                            ("VRCP14PD", R.rcp14, R.F64), ("VRSQRT14PD", R.rsqrt14, R.F64)):
            xs = sweep_inputs(name, 8, 0x77, "alt") + table_inputs(name)
            for mx in (0x1F80, 0x1FC0, 0x9F80, 0x9FC0):
                d = [x for x in xs if fn(f, x, mx) != model(name, x, mx)]
                expect(not d, "%s MXCSR %04X: ref_rcp.py differs on %d inputs (first %X)" %
                       (name, mx, len(d), d[0] if d else 0))
        print("cross-check with ref_rcp.py: done")
    except ImportError:
        print("cross-check with ref_rcp.py: skipped (not found)")
    # (7) the case file is reproducible from this model
    path = os.path.normpath(os.path.join(HERE, "..", "..", "data", "cases_amb2.txt"))
    if os.path.exists(path):
        tmp = path + ".selftest.tmp"
        write_cases(tmp)
        same = open(tmp, "rb").read().replace(CRLF, LF) == open(path, "rb").read().replace(CRLF, LF)
        os.remove(tmp)
        expect(same, "cases_amb2.txt differs from ref_amb2.py --cases (regenerate)")
    for name, (e, x) in sorted(worst.items()):
        print("worst |rel err| of the model, %-11s 2^%.4f (x = %X; bound %s)" %
              (name, _log2(e), x, BOUND_TEXT[FORMS[name][2]]))
    print("ref_amb2 selftest: %d element checks, %d failures" % (n, len(FAILS)))
    return 0 if not FAILS else 1


def _log2(e):
    """log2 of a Fraction / Decimal / float > 0, for printing"""
    import math
    e = abs(e)
    if e == 0:
        return float("-inf")
    if isinstance(e, Fraction):
        k = flog2(e)
        return k + math.log2(float(e / p2(k)))
    return math.log2(float(e))


# ----------------------------------------------------------------------------- SDE report
RE_CASE = re.compile(r"^\[\d+\] (?:SAME|DIFF|RAN) (.*)$")
RE_SDE = re.compile(r"^\s+sde: (.*)$")


def parse_line(line):
    """case line -> (bytes, {key: value})"""
    code, _, rest = line.partition("|")
    rest = rest.split("=>")[0]
    bs = [int(t.strip(), 16) for t in code.replace(".byte", "").split(",") if t.strip()]
    keys = dict(kv.split("=", 1) for kv in rest.split() if "=" in kv)
    return bs, keys


def reg_bytes(keys, i):
    """input zmm<i> as 64 bytes (absent = 0)"""
    h = keys.get("zmm%d" % i)
    b = bytearray(64)
    if h:
        hb = bytes.fromhex(h)
        b[:len(hb)] = hb
    return b


def apply_sde(b, fields, i):
    for kv in fields.split():
        if "=" not in kv:
            continue
        k, v = kv.split("=", 1)
        hb = bytes.fromhex(v) if re.fullmatch(r"[0-9A-Fa-f]+", v) else None
        if hb is None:
            continue
        if i < 16 and k == "xmm%d" % i:
            b[0:16] = hb
        elif i < 16 and k == "ymmh%d" % i:
            b[16:32] = hb
        elif i < 16 and k == "zmmh%d" % i:
            b[32:64] = hb
        elif i >= 16 and k == "zmm%d" % i:
            b[0:64] = hb
    return b


def ordinal(f, x):
    m = x & ~f.sign
    return -m if x & f.sign else m


def sde_report(paths):
    cases = {}
    for p in paths:
        cur = None
        with open(p, encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                raw = raw.rstrip("\r\n")
                m = RE_CASE.match(raw)
                if m:
                    cur = m.group(1)
                    continue
                m = RE_SDE.match(raw)
                if m and cur is not None:
                    cases[cur] = m.group(1)
                    cur = None
    st = {}
    skipped = {"memory operand": 0, "not a reciprocal form": 0, "SDE fault / not checked": 0}
    for line, sde in cases.items():
        try:
            bs, keys = parse_line(line)
        except ValueError:
            skipped["not a reciprocal form"] += 1
            continue
        if len(bs) < 6 or bs[0] != 0x62:
            skipped["not a reciprocal form"] += 1
            continue
        p0, p1, p2b, opc, modrm = bs[1:6]
        name = form_by_encoding(p0 & 7, p1 & 3, p1 >> 7, opc)
        if name is None:
            skipped["not a reciprocal form"] += 1
            continue
        if modrm >> 6 != 3:
            skipped["memory operand"] += 1
            continue
        if sde.startswith("fault") or sde.startswith("NOT CHECKED") or sde.startswith("ERROR"):
            skipped["SDE fault / not checked"] += 1
            continue
        f, op, fam, mp, pp, W, oc, scalar = FORMS[name][:8]
        reg = ((modrm >> 3) & 7) | (0 if p0 & 0x80 else 8) | (0 if p0 & 0x10 else 16)
        rm = (modrm & 7) | (0 if p0 & 0x20 else 8) | (0 if p0 & 0x40 else 16)
        aaa, z, ll = p2b & 7, p2b >> 7, (p2b >> 5) & 3
        mx = int(keys.get("mxcsr", "0x1F80"), 0)
        kv = int(keys.get("k%d" % aaa, "0"), 0) if aaa else -1
        src = lanes_of(reg_bytes(keys, rm).hex(), f)
        out = lanes_of(apply_sde(reg_bytes(keys, reg), sde, reg).hex(), f)
        n = 1 if scalar else (16 << ll) // f.nbytes
        s = st.setdefault(name, {"lanes": 0, "same": 0, "hist": {}, "maxulp": 0, "sde_ok": 0, "sde_bad": [],
                                 "unattain": 0, "worst_sde": None, "worst_model": None, "tz": None,
                                 "special_same": 0, "special": 0, "lines": 0})
        s["lines"] += 1
        for j in range(n):
            if aaa and not (kv >> j) & 1:
                continue
            x, rs = src[j], out[j]
            rm_ = model(name, x, mx)
            s["lanes"] += 1
            ok, rule, e = check(name, x, mx, rs)
            if ok:
                s["sde_ok"] += 1
            elif len(s["sde_bad"]) < 12:
                s["sde_bad"].append("x=%0*X mxcsr=%04X sde=%0*X model=%0*X (%s)" %
                                    (f.nbytes * 2, x, mx, f.nbytes * 2, rs, f.nbytes * 2, rm_, rule))
            else:
                s["sde_bad"].append(None)
            if "unattainable" in rule:
                s["unattain"] += 1
            if rs == rm_:
                s["same"] += 1
            special = not rule.startswith("|rel err|") and "underflow result, |rel" not in rule
            if special:
                s["special"] += 1
                s["special_same"] += rs == rm_
            if kind(f, rs) in ("nan", "inf") or kind(f, rm_) in ("nan", "inf"):
                d = 0 if rs == rm_ else -1
            else:
                d = abs(ordinal(f, rs) - ordinal(f, rm_))
            key = "differs (special)" if d < 0 else ("0" if d == 0 else "%d-%d" % (1 << (d.bit_length() - 1),
                                                                                  (1 << d.bit_length()) - 1))
            s["hist"][key] = s["hist"].get(key, 0) + 1
            if d > s["maxulp"]:
                s["maxulp"] = d
            if d > 0 and kind(f, rs) in ("den", "norm") and kind(f, rm_) in ("den", "norm"):
                rd = abs(float((val(f, rs) - val(f, rm_)) / val(f, rm_)))   # relative distance
                if rd > s.get("maxrel", 0.0):
                    s["maxrel"] = rd
            if e is not None and rule.startswith("|rel err|"):
                ef = abs(float(e))
                if s["worst_sde"] is None or ef > s["worst_sde"][0]:
                    s["worst_sde"] = (ef, x, rs)
                okm, rulem, em = check(name, x, mx, rm_)
                if em is not None:
                    efm = abs(float(em))
                    if s["worst_model"] is None or efm > s["worst_model"][0]:
                        s["worst_model"] = (efm, x, rm_)
                if kind(f, rs) == "norm":
                    tz = ((rs & f.fmask) | (1 << f.fbits))
                    tz = (tz & -tz).bit_length() - 1
                    s["tz"] = tz if s["tz"] is None else min(s["tz"], tz)
    import math
    print("SDE labelled-reference report (Intel SDE 10.13.1; SDE-validated reference, NOT hardware; the SDM / spec")
    print("wins; SDE values are never copied into the emulator). Distances in ulps of the format, model = the")
    print("analytic stand-in of decision A9 (= the emulator, see cases_amb2.txt / cases_rcp*.txt).")
    print("logs: %s; cases parsed: %d; skipped: %s" % (", ".join(paths), len(cases),
                                                      ", ".join("%s %d" % kv for kv in skipped.items())))
    for name in FORMS:
        if name not in st:
            continue
        s = st[name]
        lg = lambda v: ("2^%.3f" % math.log2(v)) if v and v > 0 else "0"
        print("\n%s (%s, bound %s): %d lines, %d lanes" % (name, FORMS[name][8], BOUND_TEXT[FORMS[name][2]],
                                                          s["lines"], s["lanes"]))
        print("  SDE == model (bit-identical): %d of %d lanes; documented special-case lanes: %d, identical %d" %
              (s["same"], s["lanes"], s["special"], s["special_same"]))
        order = sorted(s["hist"].items(), key=lambda kv: (kv[0].startswith("differs"),
                                                          int(kv[0].split("-")[0]) if kv[0][0].isdigit() else 0))
        print("  ulp distance SDE vs model: " + ", ".join("%s: %d" % kv for kv in order) + "; max %d" % s["maxulp"] +
              ("; max relative distance %s" % lg(s["maxrel"]) if s.get("maxrel") else ""))
        print("  SDE meets the documented requirements: %d of %d lanes%s" %
              (s["sde_ok"], s["lanes"], (" (bound unattainable lanes: %d)" % s["unattain"]) if s["unattain"] else ""))
        bad = [b for b in s["sde_bad"] if b]
        nbad = len(s["sde_bad"])
        if nbad:
            print("  SDE outside the documented requirements: %d lanes, e.g." % nbad)
            for b in bad:
                print("    " + b)
        if s["worst_sde"]:
            print("  worst |rel err|: SDE %s (x = %X), model %s (x = %X)" % (
                lg(s["worst_sde"][0]), s["worst_sde"][1], lg(s["worst_model"][0]) if s["worst_model"] else "-",
                s["worst_model"][1] if s["worst_model"] else 0))
        if s["tz"] is not None:
            print("  SDE normal results: at least %d trailing zero bits in the %d-bit significand (precision of"
                  " SDE's values <= %d bits)" % (s["tz"], FORMS[name][0].p, FORMS[name][0].p - s["tz"]))
    return 0


# ----------------------------------------------------------------------------- main
def main():
    a = sys.argv[1:]
    if "--selftest" in a:
        return selftest()
    if "--cases" in a:
        i = a.index("--cases")
        out = a[i + 1] if i + 1 < len(a) else os.path.normpath(os.path.join(HERE, "..", "..", "data", "cases_amb2.txt"))
        n = write_cases(out)
        print("wrote %s (%d case lines)" % (out, n))
        return 0
    if "--sde-report" in a:
        return sde_report(a[a.index("--sde-report") + 1:])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
