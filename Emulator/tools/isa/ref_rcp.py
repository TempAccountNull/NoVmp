#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_rcp.py -- independent reference model (Python 3 stdlib only, exact integer arithmetic) of the
approximate reciprocal / reciprocal-square-root instructions (decision A9, worktree rcp14, U940-U959)
and generator of the expected-value case files Emulator/data/cases_rcp.txt (AVX-512 / AVX10.1 forms)
and Emulator/data/cases_rcp_bf16.txt (AVX10.2 BF16 forms).

Written from the Intel documents only (the emulator's helpers were not used to derive any value):
  * SDM 092 Vol2C: VRCP14PD/PS/SD/SS (Tables 5-24, 5-25), VRSQRT14PD/PS/SD/SS (Tables 5-32..5-35),
    VRCPPH/VRCPSH (Table 5-26), VRSQRTPH/VRSQRTSH (Table 5-36), VCVTPH2PSX ("The instructions
    associated with AVX512_FP16 always handle FP16 denormal number inputs"); Vol2B RCPPS/RSQRTPS
    (|relative error| <= 1.5 * 2^-12, tiny-result guarantees); Vol1 10.2.3.3 (FTZ), 10.2.3.4 (DAZ).
  * AVX10.2 spec 361050-007 (rev 7.0) 7.12 VRCPBF16 (Table 7.2), 7.15 VRSQRTBF16 (Table 7.3):
    DAZ, FTZ, RNE, MXCSR neither consulted nor updated.

Documented architectural requirements (all forms: MXCSR.RC ignored, no MXCSR flag, no #XM):
  VRCP14*   |rel err| < 2^-14; 0 -> INF (sign kept); |X| <= 2^-128 (PS) / 2^-1024 (PD) -> +-INF
            ("very small denormal"); |X| > 2^126 / 2^1022 -> underflow result ("mantissa shifted
            right by one or two bits"), flushed to a zero of the operand's sign only with MXCSR.FTZ;
            a denormal source is zero only with MXCSR.DAZ; 2^-n -> 2^n; SNaN -> QNaN, QNaN -> itself.
  VRSQRT14* |rel err| < 2^-14; any denormal -> normal (no overflow); 2^-2n -> 2^n; X < 0 (incl.
            -INF) -> QNaN indefinite; -0 -> -INF; +0 -> +INF; +INF -> +0; DAZ as above.
  VRCPPH/SH |rel err| < 2^-11 + 2^-14; 0 <= X <= 2^-16 -> INF (sign kept); +-INF -> +-0;
            2^-n -> 2^n; FP16 denormal inputs are never DAZ'd, FP16 results never FTZ'd.
  VRSQRTPH/SH |rel err| < 2^-11 + 2^-14; Table 5-36 = the VRSQRT14 table.
  VRCPBF16  |rel err| < 2^-8 + 2^-14; DAZ: |X| < 2^-126 -> +-INF; +-INF -> +-0; 2^-n -> 2^n; FTZ.
  VRSQRTBF16 |rel err| < 2^-8 + 2^-14; DAZ (+-0 / denormal -> +-INF); 2^-2n -> 2^n; X < 0 ->
            QNaN indefinite (incl. -INF); +INF -> +0.
  NOT documented (implementation-specific): the result bits inside the bound. Intel states that a
  "numerically exact implementation of VRCP14xx / VRSQRT14xx" exists (reference code at an Intel URL
  that is not available here); the FP16 / BF16 pages give no reference implementation.

MODEL CHOICE (the emulator's stand-ins U236 / U337 / U373, decision A9 keeps them because they meet
every documented requirement; NOT claimed bit-exact to Intel silicon):
  * RCP14 / RSQRT14 / RCPPH / RSQRTPH / RCPBF16 / RSQRTBF16: the exact 1/x resp. 1/sqrt(x) rounded to
    nearest-even to the destination precision (unbounded exponent; overflow -> INF);
  * VRCP14 tiny results (below 2^emin after the p-bit rounding, SDM Vol1 4.9.1.5): +-0 with
    MXCSR.FTZ, otherwise (U940, the SDM's "correct underflow result is written") the exact value
    rounded ONCE to nearest-even at the denormal quantum (the SDM's "mantissa shifted right by one
    or two bits"; U236 rounded to p bits first and then again);
  * VRCPPH tiny results: one rounding of the exact value at the FP16 denormal quantum (no FTZ);
  * VRCPBF16 tiny results (< 2^-126 after rounding): +-0 (FTZ).
  --selftest proves the choice against every documented requirement (exhaustive for FP16 / BF16,
  every exponent for FP32 / FP64) and checks the independent U81 legacy model (RCPPS / RSQRTPS)
  against the SDM's 1.5 * 2^-12 bound and tiny-result guarantees.

usage: ref_rcp.py --selftest | --cases | --cases-f16-all | --cases-bf16 | --cases-hw
"""
import random
import sys
from fractions import Fraction
from math import isqrt

BS = chr(92)

# ----------------------------------------------------------------------------- formats
class Fmt:
    def __init__(self, name, bits, p, ebits):
        self.name, self.bits, self.p, self.ebits = name, bits, p, ebits
        self.bias = (1 << (ebits - 1)) - 1
        self.emin, self.emax = 1 - self.bias, self.bias
        self.fmask = (1 << (p - 1)) - 1
        self.sign = 1 << (bits - 1)
        self.emask = ((1 << ebits) - 1) << (p - 1)
        self.inf = self.emask
        self.qbit = 1 << (p - 2)
        self.indef = self.sign | self.emask | self.qbit
        self.q = self.emin - p + 1          # exponent of the denormal quantum

F16 = Fmt("f16", 16, 11, 5)
BF16 = Fmt("bf16", 16, 8, 8)
F32 = Fmt("f32", 32, 24, 8)
F64 = Fmt("f64", 64, 53, 11)

MXCSR_DEFAULT = 0x1F80
DAZ, FTZ = 0x40, 0x8000


def is_nan(f, x):
    return (x & f.emask) == f.emask and (x & f.fmask) != 0


def is_inf(f, x):
    return (x & ~f.sign) == f.inf


def decode(f, x):
    """finite non-zero x -> (sign, M, E) with |x| = M * 2^E (M integer > 0)"""
    e = (x & f.emask) >> (f.p - 1)
    m = x & f.fmask
    if e == 0:
        return (x & f.sign) != 0, m, f.q
    return (x & f.sign) != 0, m | (1 << (f.p - 1)), e - f.bias - (f.p - 1)


def value(f, x):
    s, m, e = decode(f, x)
    v = Fraction(m) * (Fraction(2) ** e)
    return -v if s else v


def rnd_q(t, sticky, se, q):
    """round (t + frac) * 2^se (frac in [0,1), non-zero iff sticky) to a multiple of 2^q, RNE;
    returns n with the result = n * 2^q"""
    assert q > se                           # every caller rounds away at least one bit
    sh = q - se
    n = t >> sh
    rem = t & ((1 << sh) - 1)
    half = 1 << (sh - 1)
    if rem > half or (rem == half and (sticky or (n & 1))):
        n += 1
    return n


def rnd_p(t, sticky, se, p):
    """round to p significant bits (unbounded exponent): returns (n, e2), n in [2^(p-1), 2^p)"""
    L = t.bit_length()
    assert L >= p + 2
    q = se + L - p
    n = rnd_q(t, sticky, se, q)
    if n == 1 << p:
        n >>= 1
        q += 1
    return n, q


def encode(f, sign, n, e2):
    """value n * 2^e2 with n < 2^p; normal when n has p bits, denormal when e2 == f.q"""
    s = f.sign if sign else 0
    if n == 0:
        return s
    if n.bit_length() == f.p:
        lead = e2 + f.p - 1
        if lead > f.emax:
            return s | f.inf
        assert lead >= f.emin
        return s | ((lead + f.bias) << (f.p - 1)) | (n & f.fmask)
    assert e2 == f.q and n < (1 << (f.p - 1))
    return s | n                            # denormal (n = 2^(p-1) would be the smallest normal)


def recip_parts(f, x):
    """exact 1/|x| as (t, sticky, se) with t >= 2^(p+2)"""
    _, m, e = decode(f, x)
    k = f.p + 3 + m.bit_length()
    t, r = divmod(1 << k, m)
    return t, r != 0, -e - k


def rsqrt_parts(f, x):
    """exact 1/sqrt(|x|) as (t, sticky, se) with t >= 2^(p+2)"""
    _, m, e = decode(f, x)
    if e & 1:
        m <<= 1
        e -= 1
    k = f.p + 4 + (m.bit_length() + 1) // 2
    t = isqrt((1 << (2 * k)) // m)
    exact = t * t * m == (1 << (2 * k))
    return t, not exact, -k - e // 2


# ----------------------------------------------------------------------------- the models
def rcp14(f, x, mxcsr=MXCSR_DEFAULT):
    """VRCP14PS/PD/SS/SD element (f = F32 / F64)"""
    if is_nan(f, x):
        return x | f.qbit
    s = x & f.sign
    if (x & f.emask) == 0 and ((x & f.fmask) == 0 or (mxcsr & DAZ)):
        return s | f.inf
    if is_inf(f, x):
        return s
    n, e2 = rnd_p(*recip_parts(f, x), f.p)
    lead = e2 + f.p - 1
    if lead > f.emax:
        return s | f.inf
    if lead < f.emin:
        if mxcsr & FTZ:
            return s
        t, st, se = recip_parts(f, x)
        return encode(f, s, rnd_q(t, st, se, f.q), f.q)
    return encode(f, s, n, e2)


def rsqrt14(f, x, mxcsr=MXCSR_DEFAULT):
    """VRSQRT14PS/PD/SS/SD element"""
    if is_nan(f, x):
        return x | f.qbit
    s = x & f.sign
    if (x & f.emask) == 0 and ((x & f.fmask) == 0 or (mxcsr & DAZ)):
        return s | f.inf
    if s:
        return f.indef
    if is_inf(f, x):
        return 0
    n, e2 = rnd_p(*rsqrt_parts(f, x), f.p)
    return encode(f, 0, n, e2)


def rcp_ph(x):
    """VRCPPH/VRCPSH element: no DAZ, no FTZ, one rounding also for tiny results"""
    f = F16
    if is_nan(f, x):
        return x | f.qbit
    s = x & f.sign
    if (x & ~f.sign) == 0:
        return s | f.inf
    if is_inf(f, x):
        return s
    t, st, se = recip_parts(f, x)
    n, e2 = rnd_p(t, st, se, f.p)
    lead = e2 + f.p - 1
    if lead > f.emax:
        return s | f.inf
    if lead < f.emin:
        return encode(f, s, rnd_q(t, st, se, f.q), f.q)
    return encode(f, s, n, e2)


def rsqrt_ph(x):
    f = F16
    if is_nan(f, x):
        return x | f.qbit
    s = x & f.sign
    if (x & ~f.sign) == 0:
        return s | f.inf
    if s:
        return f.indef
    if is_inf(f, x):
        return 0
    n, e2 = rnd_p(*rsqrt_parts(f, x), f.p)
    return encode(f, 0, n, e2)


def bf16_daz(x):
    return x & BF16.sign if (x & BF16.emask) == 0 else x


def rcp_bf16(x):
    f = BF16
    if is_nan(f, x):
        return x | f.qbit
    x = bf16_daz(x)
    s = x & f.sign
    if (x & ~f.sign) == 0:
        return s | f.inf
    if is_inf(f, x):
        return s
    n, e2 = rnd_p(*recip_parts(f, x), f.p)
    lead = e2 + f.p - 1
    if lead > f.emax:
        return s | f.inf
    if lead < f.emin:
        return s                            # FTZ (tiny after rounding)
    return encode(f, s, n, e2)


def rsqrt_bf16(x):
    f = BF16
    if is_nan(f, x):
        return x | f.qbit
    x = bf16_daz(x)
    s = x & f.sign
    if (x & ~f.sign) == 0:
        return s | f.inf
    if s:
        return f.indef
    if is_inf(f, x):
        return 0
    n, e2 = rnd_p(*rsqrt_parts(f, x), f.p)
    return encode(f, 0, n, e2)


# legacy RCPPS / RSQRTPS (U81, hardware-validated): independent restatement of the analytic model
def rcp12(x):
    f = F32
    s, e, m = x & f.sign, (x >> 23) & 0xFF, x & 0x7FFFFF
    if e == 0xFF:
        return x | f.qbit if m else s
    if e == 0:
        return s | f.inf
    mid = Fraction(4097 + 2 * (m >> 12), 4096)            # midpoint of the 2^11-interval, in [1,2)
    q = Fraction(8192) / mid                               # 1/mid in units of 2^-13 (12 fraction bits)
    k = int(q)
    if q - k > Fraction(1, 2) or (q - k == Fraction(1, 2) and k & 1):
        k += 1
    re = 253 - e
    if re <= 0:
        return s
    return s | (re << 23) | ((k - 4096) << 11)


def rsqrt12(x):
    f = F32
    s, e, m = x & f.sign, (x >> 23) & 0xFF, x & 0x7FFFFF
    if e == 0xFF:
        return x | f.qbit if m else (f.indef if s else 0)
    if e == 0:
        return s | f.inf
    if s:
        return f.indef
    ue = e - 127
    d = 2049 + 2 * (m >> 13)                                # midpoint = d/2048 (x2 if ue odd)
    num = (1 << 38) if ue & 1 else (1 << 39)
    k = isqrt(num // 4 // d) + 2
    while (2 * k - 1) * (2 * k - 1) * d > num:
        k -= 1
    fe = 126 - ((ue - (ue & 1)) // 2)
    return (fe << 23) | ((k - 4096) << 11)


# ----------------------------------------------------------------------------- self-test
FAILS = []


def chk(what, got, exp):
    if got != exp:
        FAILS.append("%s: got %r expected %r" % (what, got, exp))


def rel_err_rcp(f, x, r):
    v = value(f, x)
    return abs(value(f, r) * v - 1)


def rel_err_rsqrt(f, x, r):
    """|r*sqrt(x) - 1| < B  <=>  (1-B)^2 < r^2 x < (1+B)^2; returns r^2 x (|r^2 x - 1| / 2 ~ the
    relative error, printed only)"""
    return value(f, r) ** 2 * value(f, x)


def within_rsqrt(r2x, b):
    return (1 - b) ** 2 < r2x < (1 + b) ** 2


def is_normal(f, x):
    e = (x & f.emask) >> (f.p - 1)
    return 0 < e < (1 << f.ebits) - 1


def is_denorm(f, x):
    return (x & f.emask) == 0 and (x & f.fmask) != 0


def selftest():
    B14 = Fraction(1, 2 ** 14)
    BPH = Fraction(1, 2 ** 11) + B14
    BBF = Fraction(1, 2 ** 8) + B14
    stats = {}

    # ---- FP16: every encoding
    worst_r = worst_s = Fraction(0)
    sub_r = 0
    for x in range(1 << 16):
        r, s = rcp_ph(x), rsqrt_ph(x)
        if is_nan(F16, x):
            chk("ph nan %04X" % x, (r, s), (x | 0x200, x | 0x200))
            continue
        sg = x & 0x8000
        if x & 0x7FFF == 0:
            chk("rcpph 0 %04X" % x, r, sg | 0x7C00)
            chk("rsqrtph 0 %04X" % x, s, sg | 0x7C00)
            continue
        if is_inf(F16, x):
            chk("rcpph inf", r, sg)
            chk("rsqrtph inf", s, F16.indef if sg else 0)
            continue
        v = abs(value(F16, x))
        if v <= Fraction(1, 2 ** 16):
            chk("rcpph |X| <= 2^-16 -> INF %04X" % x, r, sg | 0x7C00)
        elif is_normal(F16, r):
            e = rel_err_rcp(F16, x, r)
            worst_r = max(worst_r, e)
            if not e < BPH:
                FAILS.append("rcpph bound %04X" % x)
        else:
            sub_r += 1                       # tiny result: below the normal range (bound not attainable)
            chk("rcpph tiny keeps sign %04X" % x, r & 0x8000, sg)
        if v.denominator == 1 or v.numerator == 1:
            if v.numerator == 1 and v.denominator & (v.denominator - 1) == 0 and v.denominator <= 2 ** 15:
                chk("rcpph 2^-n -> 2^n %04X" % x, value(F16, r), (-1 if sg else 1) * Fraction(v.denominator))
        if sg:
            chk("rsqrtph X < 0 %04X" % x, s, F16.indef)
            continue
        if is_denorm(F16, x):
            chk("rsqrtph denormal -> normal %04X" % x, is_normal(F16, s), True)
        r2x = rel_err_rsqrt(F16, x, s)
        worst_s = max(worst_s, abs(r2x - 1))
        if not within_rsqrt(r2x, BPH):
            FAILS.append("rsqrtph bound %04X" % x)
    stats["f16"] = (worst_r, worst_s, sub_r)

    # ---- BF16: every encoding
    worst_r = worst_s = Fraction(0)
    for x in range(1 << 16):
        r, s = rcp_bf16(x), rsqrt_bf16(x)
        if is_nan(BF16, x):
            chk("bf16 nan %04X" % x, (r, s), (x | 0x40, x | 0x40))
            continue
        sg = x & 0x8000
        if (x & 0x7F80) == 0:                # +-0 and denormals (DAZ): |X| < 2^-126 -> +-INF
            chk("rcpbf16 DAZ %04X" % x, r, sg | 0x7F80)
            chk("rsqrtbf16 DAZ %04X" % x, s, sg | 0x7F80)
            continue
        if is_inf(BF16, x):
            chk("rcpbf16 inf", r, sg)
            chk("rsqrtbf16 inf", s, BF16.indef if sg else 0)
            continue
        if is_normal(BF16, r):
            e = rel_err_rcp(BF16, x, r)
            worst_r = max(worst_r, e)
            if not e < BBF:
                FAILS.append("rcpbf16 bound %04X" % x)
        else:
            chk("rcpbf16 FTZ %04X" % x, r, sg)
            chk("rcpbf16 FTZ only above 2^126 %04X" % x, abs(value(BF16, x)) > 2 ** 126, True)
        if sg:
            chk("rsqrtbf16 X < 0 %04X" % x, s, BF16.indef)
            continue
        r2x = rel_err_rsqrt(BF16, x, s)
        worst_s = max(worst_s, abs(r2x - 1))
        if not within_rsqrt(r2x, BBF):
            FAILS.append("rsqrtbf16 bound %04X" % x)
    stats["bf16"] = (worst_r, worst_s)

    # ---- FP32 / FP64: every exponent x sampled mantissas, every MXCSR mode that matters
    rng = random.Random(14)
    for f, nm in ((F32, 40), (F64, 16)):
        worst_r = worst_s = Fraction(0)
        for be in range(0, (1 << f.ebits) - 1, 1 if f.bits == 32 else 3):
            mans = [0, 1, 2, f.fmask, f.fmask - 1, 1 << (f.p - 2)] + [rng.getrandbits(f.p - 1) for _ in range(nm)]
            for m in mans:
                if be == 0 and m == 0:
                    continue
                for sg in (0, f.sign):
                    x = sg | (be << (f.p - 1)) | m
                    for mx in (MXCSR_DEFAULT, MXCSR_DEFAULT | DAZ, MXCSR_DEFAULT | FTZ,
                               MXCSR_DEFAULT | DAZ | FTZ, 0x7F80, 0x0000):
                        r = rcp14(f, x, mx)
                        q = rsqrt14(f, x, mx)
                        if mx & ~(DAZ | FTZ) != MXCSR_DEFAULT & ~(DAZ | FTZ):
                            chk("rcp14 ignores RC/masks %X" % x, r, rcp14(f, x, mx & (DAZ | FTZ) | MXCSR_DEFAULT))
                            continue
                        v = abs(value(f, x))
                        if be == 0 and (mx & DAZ):
                            chk("rcp14 DAZ", r, sg | f.inf)
                            chk("rsqrt14 DAZ", q, sg | f.inf)
                            continue
                        if v <= Fraction(1, 2 ** (f.bias + 1)):          # 2^-128 / 2^-1024
                            chk("rcp14 very small denormal -> INF %X" % x, r, sg | f.inf)
                        elif v > 2 ** (f.bias - 1):                     # X > 2^126 / 2^1022
                            if mx & FTZ:
                                chk("rcp14 underflow FTZ %X" % x, r, sg)
                            else:
                                chk("rcp14 underflow denormal %X" % x, is_denorm(f, r) or r & ~f.sign == 1 << (f.p - 1), True)
                                chk("rcp14 underflow sign %X" % x, r & f.sign, sg)
                                e = rel_err_rcp(f, x, r)
                                if not e < B14:
                                    FAILS.append("rcp14 underflow bound %X" % x)
                        else:
                            chk("rcp14 normal %X" % x, is_normal(f, r), True)
                            e = rel_err_rcp(f, x, r)
                            worst_r = max(worst_r, e)
                            if not e < B14:
                                FAILS.append("rcp14 bound %X" % x)
                        if sg:
                            chk("rsqrt14 X < 0 %X" % x, q, f.indef)
                            continue
                        chk("rsqrt14 normal %X" % x, is_normal(f, q), True)
                        r2x = rel_err_rsqrt(f, x, q)
                        worst_s = max(worst_s, abs(r2x - 1))
                        if not within_rsqrt(r2x, B14):
                            FAILS.append("rsqrt14 bound %X" % x)
        stats[f.name] = (worst_r, worst_s)
        # tables: 2^-n -> 2^n, 2^-2n -> 2^n, zeros, infinities, NaNs
        for n in range(-f.bias + 1, f.bias + 1):
            x = (n + f.bias) << (f.p - 1)
            chk("rcp14 2^%d" % n, value(f, rcp14(f, x)), Fraction(1) / value(f, x))      # 2^-127 / 2^-1023: exact denormal
            chk("rcp14 -2^%d" % n, value(f, rcp14(f, x | f.sign)), -Fraction(1) / value(f, x))
            if n % 2 == 0:
                chk("rsqrt14 2^%d" % n, rsqrt14(f, x), (-n // 2 + f.bias) << (f.p - 1))
        chk("rcp14 +0", rcp14(f, 0), f.inf)
        chk("rcp14 -0", rcp14(f, f.sign), f.sign | f.inf)
        chk("rcp14 +inf", rcp14(f, f.inf), 0)
        chk("rcp14 -inf", rcp14(f, f.sign | f.inf), f.sign)
        chk("rsqrt14 -inf", rsqrt14(f, f.sign | f.inf), f.indef)
        chk("rsqrt14 -0", rsqrt14(f, f.sign), f.sign | f.inf)
        chk("rsqrt14 +0", rsqrt14(f, 0), f.inf)
        chk("rsqrt14 +inf", rsqrt14(f, f.inf), 0)
        chk("rsqrt14 -denormal", rsqrt14(f, f.sign | 1), f.indef)
        chk("rsqrt14 -denormal DAZ", rsqrt14(f, f.sign | 1, MXCSR_DEFAULT | DAZ), f.sign | f.inf)
        chk("snan", rcp14(f, f.inf | 1), f.inf | 1 | f.qbit)
        chk("qnan", rsqrt14(f, f.sign | f.inf | f.qbit | 5), f.sign | f.inf | f.qbit | 5)
        # the INF boundary is exactly 2^-128 / 2^-1024: the next denormal gives a finite result
        b = 1 << (f.p - 3)                                   # 2^(q + p - 3) = 2^-128 / 2^-1024
        chk("rcp14 2^-(bias+1) -> INF", rcp14(f, b), f.inf)
        chk("rcp14 next denormal finite", is_normal(f, rcp14(f, b + 1)), True)

    # ---- legacy RCPPS / RSQRTPS (U81 model): SDM bound 1.5 * 2^-12 on every interval
    B12 = Fraction(3, 2 ** 13)
    worst = Fraction(0)
    for hi in range(1 << 11):
        r = rcp12(0x3F800000 | (hi << 12))
        rv = value(F32, r)
        for m in (hi << 12, (hi << 12) | 0xFFF):            # interval end points (error is monotone)
            e = abs(rv * value(F32, 0x3F800000 | m) - 1)
            worst = max(worst, e)
            if e > B12:
                FAILS.append("rcpps bound %06X" % m)
    worst_s = Fraction(0)
    for be in (127, 128):
        for hi in range(1 << 10):
            r = rsqrt12((be << 23) | (hi << 13))
            for m in (hi << 13, (hi << 13) | 0x1FFF):
                r2x = rel_err_rsqrt(F32, (be << 23) | m, r)
                worst_s = max(worst_s, abs(r2x - 1))
                if not within_rsqrt(r2x, B12):
                    FAILS.append("rsqrtps bound %06X" % m)
    # SDM RCPPS: inputs >= 1.11111111110100000000000B*2^125 never tiny, <= 1.00000000000110000000001B*2^126 always tiny
    lo = 0x7E000000 | 0b11111111110100000000000
    hi_ = 0x7E800000 | 0b00000000000110000000001
    chk("rcpps not tiny at the SDM lower guarantee", rcp12(lo) != 0, True)
    chk("rcpps tiny at the SDM upper guarantee", rcp12(hi_), 0)
    stats["rcp12"] = (worst, worst_s)
    chk("rcpps(1)", rcp12(0x3F800000), 0x3F7FF000)

    for k, v in stats.items():
        print("%-6s worst relative error: rcp %s, rsqrt %s%s" % (
            k, fmt_err(v[0]), fmt_err(v[1] / 2), (", tiny FP16 rcp results %d" % v[2]) if len(v) > 2 else ""))
    for f in FAILS[:40]:
        print("FAIL", f)
    print("failures: %d" % len(FAILS))
    return not FAILS


def fmt_err(e):
    if e == 0:
        return "0"
    import math
    return "2^%.4f" % (math.log2(e.numerator) - math.log2(e.denominator))


# ----------------------------------------------------------------------------- case generation
def evex(mp, pp, w, reg, rm, vvvv, ll, z, aaa, b, op, mem=None):
    """EVEX instruction bytes. rm: register number, or None with mem = (modrm_rm, disp8|None)"""
    r_, rp = (reg >> 3) & 1, (reg >> 4) & 1
    if mem is None:
        bb, xx = (rm >> 3) & 1, (rm >> 4) & 1
    else:
        bb = xx = 0
    p0 = ((r_ ^ 1) << 7) | ((xx ^ 1) << 6) | ((bb ^ 1) << 5) | ((rp ^ 1) << 4) | mp
    p1 = (w << 7) | (((~vvvv) & 15) << 3) | 4 | pp
    p2 = (z << 7) | (ll << 5) | (b << 4) | ((((vvvv >> 4) & 1) ^ 1) << 3) | aaa
    out = [0x62, p0, p1, p2, op]
    if mem is None:
        out.append(0xC0 | ((reg & 7) << 3) | (rm & 7))
    else:
        rmb, d8 = mem
        if d8 is None:
            out.append(((reg & 7) << 3) | rmb)
        else:
            out += [0x40 | ((reg & 7) << 3) | rmb, d8 & 0xFF]
    return out


def hexle(v, nbytes):
    return v.to_bytes(nbytes, "little").hex().upper()


def vec_hex(elems, esz):
    return "".join(hexle(e, esz // 8) for e in elems)


class Form:
    def __init__(self, name, mp, pp, w, op, f, fn, scalar, uses_mx):
        self.name, self.mp, self.pp, self.w, self.op = name, mp, pp, w, op
        self.f, self.fn, self.scalar, self.uses_mx = f, fn, scalar, uses_mx

    def elem(self, x, mx):
        return self.fn(x, mx) if self.uses_mx else self.fn(x)


M2, M6 = 2, 6
FORMS = [
    Form("VRCP14PS", M2, 1, 0, 0x4C, F32, lambda x, m: rcp14(F32, x, m), False, True),
    Form("VRCP14PD", M2, 1, 1, 0x4C, F64, lambda x, m: rcp14(F64, x, m), False, True),
    Form("VRSQRT14PS", M2, 1, 0, 0x4E, F32, lambda x, m: rsqrt14(F32, x, m), False, True),
    Form("VRSQRT14PD", M2, 1, 1, 0x4E, F64, lambda x, m: rsqrt14(F64, x, m), False, True),
    Form("VRCP14SS", M2, 1, 0, 0x4D, F32, lambda x, m: rcp14(F32, x, m), True, True),
    Form("VRCP14SD", M2, 1, 1, 0x4D, F64, lambda x, m: rcp14(F64, x, m), True, True),
    Form("VRSQRT14SS", M2, 1, 0, 0x4F, F32, lambda x, m: rsqrt14(F32, x, m), True, True),
    Form("VRSQRT14SD", M2, 1, 1, 0x4F, F64, lambda x, m: rsqrt14(F64, x, m), True, True),
    Form("VRCPPH", M6, 1, 0, 0x4C, F16, rcp_ph, False, False),
    Form("VRSQRTPH", M6, 1, 0, 0x4E, F16, rsqrt_ph, False, False),
    Form("VRCPSH", M6, 1, 0, 0x4D, F16, rcp_ph, True, False),
    Form("VRSQRTSH", M6, 1, 0, 0x4F, F16, rsqrt_ph, True, False),
]
FORMS_BF16 = [
    Form("VRCPBF16", M6, 0, 0, 0x4C, BF16, rcp_bf16, False, False),
    Form("VRSQRTBF16", M6, 0, 0, 0x4E, BF16, rsqrt_bf16, False, False),
]

MX_MODES = [
    (0x1F80, "default"), (0x1FC0, "DAZ"), (0x9F80, "FTZ"), (0x9FC0, "DAZ+FTZ"),
    (0x3F80, "RD"), (0x5F80, "RU"), (0x7F80, "RZ"), (0x0000, "all exceptions unmasked"),
    (0x9FBF, "every flag set, DAZ off"),
]


def specials(f):
    s, inf, q = f.sign, f.inf, f.qbit
    v = [0, s, inf, s | inf, inf | q, inf | q | 0x5, inf | 1, inf | (q >> 1), s | inf | 1, s | inf | q | 3, f.indef]
    one = f.bias << (f.p - 1)
    v += [1, 2, 3, f.fmask, f.fmask - 1, 1 << (f.p - 2), (1 << (f.p - 2)) + 1, (1 << (f.p - 3)), (1 << (f.p - 3)) + 1,
          (1 << (f.p - 3)) - 1, (1 << (f.p - 4)), (1 << (f.p - 2)) - 1]
    v += [x | s for x in (1, f.fmask, 1 << (f.p - 3), (1 << (f.p - 3)) + 1, 1 << (f.p - 2))]
    v += [1 << (f.p - 1), (1 << (f.p - 1)) + 1, (1 << (f.p - 1)) | f.fmask]               # min normal and neighbours
    v += [one, one | 1, one | f.fmask, one | (1 << (f.p - 2)), (one + (1 << (f.p - 1))) | (1 << (f.p - 2)),  # 1, 1+ulp, 2-ulp, 1.5, 3
          one | s, (one + (2 << (f.p - 1))) | s]
    emaxb = (2 * f.bias) << (f.p - 1)                                                    # 2^emax
    v += [emaxb - (1 << (f.p - 1)), emaxb - (1 << (f.p - 1)) + 1, emaxb - (1 << (f.p - 1)) - 1,  # 2^(emax-1) and neighbours
          emaxb, emaxb | f.fmask, emaxb | (1 << (f.p - 2)), s | emaxb | f.fmask, s | (emaxb - (1 << (f.p - 1)) + 1)]
    for n in (-f.bias + 1, -f.bias + 2, -3, -2, 2, 5, f.bias - 2, f.bias):               # 2^n, 2^-2n
        v.append((n + f.bias) << (f.p - 1))
    v += [(one - (5 << (f.p - 1))) | 0x2A5 & f.fmask, (one + (7 << (f.p - 1))) | (0x15555 & f.fmask)]
    # U940: underflow inputs whose one-rounding denormal differs from the U236 p-bit-then-shift value
    v += {32: [0x7ED38264, 0x7ED3F4E3, 0x7EFE0D50, 0x7F2FF991, 0xFF51BE5C, 0xFF2F5F15],
          64: [0x7FD90976DE723B2F, 0x7FD21D10659D3F69, 0xFFD3AD7EB0438396, 0x7FE45C082C420401,
               0x7FE9D5DD6E70754B, 0xFFE901DF5389CC72]}.get(f.bits, [])
    return v


def randoms(f, n, rng):
    out = []
    for _ in range(n):
        e = rng.randrange(1, (1 << f.ebits) - 1)
        out.append(rng.choice((0, f.sign)) | (e << (f.p - 1)) | rng.getrandbits(f.p - 1))
    return out


def fill(rng, nbytes):
    return rng.getrandbits(8 * nbytes)


class Gen:
    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.lines = []

    def emit(self, title, code, inp, exp):
        self.lines.append("# " + title)
        ins = " ".join("%s=%s" % kv for kv in inp)
        ex = " ".join("%s=%s" % kv for kv in exp)
        self.lines.append(".byte " + ", ".join("0x%02x" % b for b in code) + " | " + ins + " => " + ex)

    def zmm_full(self):
        return [fill(self.rng, 8) for _ in range(8)]


def zmm_hex(qw):
    return "".join(hexle(q, 8) for q in qw)


def elems_to_q(el, esz):
    b = b"".join(e.to_bytes(esz // 8, "little") for e in el)
    return [int.from_bytes(b[i:i + 8], "little") for i in range(0, 64, 8)]


def q_to_elems(qw, esz):
    b = b"".join(q.to_bytes(8, "little") for q in qw)
    n = esz // 8
    return [int.from_bytes(b[i:i + n], "little") for i in range(0, 64, n)]


def packed_case(g, fm, vl, srcs, mx, mxname, dst=1, src=2, aaa=0, z=0, kval=None, mem=None, title_extra="",
                dst_init=True):
    esz = fm.f.bits
    lanes = vl // esz
    tot = 512 // esz
    srcv = list(srcs[:lanes]) + [g.rng.getrandbits(esz) for _ in range(tot - len(srcs[:lanes]))]
    dold = q_to_elems(g.zmm_full(), esz) if dst_init else [0] * (512 // esz)
    ll = {128: 0, 256: 1, 512: 2}[vl]
    inp, exp = [], []
    if mem is None:
        code = evex(fm.mp, fm.pp, fm.w, dst, src, 0, ll, z, aaa, 0, fm.op)
        inp.append(("zmm%d" % src, zmm_hex(elems_to_q(srcv, esz))))
    elif mem == "bcst":
        code = evex(fm.mp, fm.pp, fm.w, dst, None, 0, ll, z, aaa, 1, fm.op, mem=(6, None))
        srcv = [srcs[0]] * tot
        inp.append(("m+0x8000", hexle(srcs[0], esz // 8)))
    else:
        code = evex(fm.mp, fm.pp, fm.w, dst, None, 0, ll, z, aaa, 0, fm.op, mem=(6, None))
        inp.append(("m+0x8000", vec_hex(srcv[:lanes], esz)))
    if dst_init:
        inp.append(("zmm%d" % dst, zmm_hex(elems_to_q(dold, esz))))
    if aaa:
        inp.append(("k%d" % aaa, "0x%X" % kval))
    inp.append(("mxcsr", "0x%X" % mx))
    res = []
    for i in range(tot):
        if i >= lanes:
            res.append(0)
        elif aaa and not (kval >> i) & 1:
            res.append(0 if z else dold[i])
        else:
            res.append(fm.elem(srcv[i], mx))
    exp.append(("zmm%d" % dst, zmm_hex(elems_to_q(res, esz))))
    t = "%s VL%d %s%s, MXCSR %04X (%s)" % (fm.name, vl, "mem bcst" if mem == "bcst" else ("mem" if mem else "reg"),
                                           title_extra, mx, mxname)
    g.emit(t, code, inp, exp)


def scalar_case(g, fm, x, mx, mxname, aaa=0, z=0, kval=None, regs=(1, 3, 2), mem=False):
    esz = fm.f.bits
    dst, s1, s2 = regs
    dold = g.zmm_full()
    s1v = g.zmm_full()
    s2v = q_to_elems(g.zmm_full(), esz)
    s2v[0] = x
    code = evex(fm.mp, fm.pp, fm.w, dst, None if mem else s2, s1, 0, z, aaa, 0, fm.op, mem=(6, None) if mem else None)
    inp = [("zmm%d" % dst, zmm_hex(dold)), ("zmm%d" % s1, zmm_hex(s1v))]
    if mem:
        inp.append(("m+0x8000", hexle(x, esz // 8)))
    else:
        inp.append(("zmm%d" % s2, zmm_hex(elems_to_q(s2v, esz))))
    if aaa:
        inp.append(("k%d" % aaa, "0x%X" % kval))
    inp.append(("mxcsr", "0x%X" % mx))
    s1e = q_to_elems(s1v, esz)
    de = q_to_elems(dold, esz)
    if aaa and not (kval & 1):
        r0 = 0 if z else de[0]
    else:
        r0 = fm.elem(x, mx)
    res = [r0] + s1e[1:128 // esz] + [0] * (512 // esz - 128 // esz)
    # s1 == dst register: the merge source is the old destination, the upper bits come from src1 (same register)
    g.emit("%s %s%s x=%s, MXCSR %04X (%s)" % (fm.name, "mem" if mem else "reg", " {k%d}%s" % (aaa, "{z}" if z else "") if aaa else "",
                                              "%0*X" % (esz // 4, x), mx, mxname),
           code, inp, [("zmm%d" % dst, zmm_hex(elems_to_q(res, esz)))])


def gen_forms(g, forms, exhaustive16, only_exhaustive=False):
    for fm in forms:
        f = fm.f
        esz = f.bits
        if only_exhaustive:
            if esz == 16 and not fm.scalar:
                g.lines.append("# %s: every one of the 65536 encodings (32 per case)" % fm.name)
                for base in range(0, 1 << 16, 32):
                    packed_case(g, fm, 512, list(range(base, base + 32)), 0x1F80, "default",
                                title_extra=" inputs %04X..%04X" % (base, base + 31), dst_init=False)
            continue
        sp = specials(f)
        g.lines.append("# === %s (%s %s W%d %02X): MXCSR modes %s" % (
            fm.name, "MAP6" if fm.mp == M6 else "0F38", "66" if fm.pp == 1 else "NP", fm.w, fm.op,
            "honoured: DAZ (input), FTZ (tiny result); RC, masks and flags ignored" if fm.uses_mx else "never consulted, never updated"))
        mxlist = MX_MODES if fm.uses_mx else [MX_MODES[0], MX_MODES[3], MX_MODES[6], MX_MODES[7], MX_MODES[8]]
        if not fm.scalar:
            for vl in (128, 256, 512):
                lanes = vl // esz
                for mx, mxn in mxlist:
                    for i in range(0, len(sp), lanes):
                        chunk = sp[i:i + lanes]
                        chunk += randoms(f, lanes - len(chunk), g.rng)
                        packed_case(g, fm, vl, chunk, mx, mxn)
                # masking, memory, broadcast, high registers
                packed_case(g, fm, vl, randoms(f, lanes, g.rng), 0x1F80, "default", aaa=3, kval=g.rng.getrandbits(64), title_extra=" {k3}")
                packed_case(g, fm, vl, sp[:lanes], 0x9FC0, "DAZ+FTZ", aaa=5, z=1, kval=g.rng.getrandbits(64), title_extra=" {k5}{z}")
                packed_case(g, fm, vl, randoms(f, lanes, g.rng), 0x1F80, "default", mem="mem")
                packed_case(g, fm, vl, [sp[g.rng.randrange(len(sp))]], 0x1F80, "default", mem="bcst", aaa=2, kval=g.rng.getrandbits(64), title_extra=" {k2}")
                packed_case(g, fm, vl, randoms(f, lanes, g.rng), 0x1FC0, "DAZ", dst=17, src=26, title_extra=" zmm17, zmm26")
            # random normals over the whole exponent range
            for _ in range(8):
                packed_case(g, fm, 512, randoms(f, 512 // esz, g.rng), 0x1F80, "default")
            if exhaustive16 and esz == 16:
                g.lines.append("# %s: every one of the 65536 encodings (32 per case)" % fm.name)
                for base in range(0, 1 << 16, 32):
                    packed_case(g, fm, 512, list(range(base, base + 32)), 0x1F80, "default",
                                title_extra=" inputs %04X..%04X" % (base, base + 31), dst_init=False)
            if fm.f is BF16 and fm.op == 0x4C:
                # AVX10.2 defines no scalar BF16 reciprocal (MAP6 NP 4D / 4F) and no W1 form
                for op, w, why in ((0x4D, 0, "MAP6 NP W0 4D (no scalar VRCPBF16 form)"),
                                   (0x4F, 0, "MAP6 NP W0 4F (no scalar VRSQRTBF16 form)"),
                                   (0x4C, 1, "VRCPBF16 with EVEX.W1"), (0x4E, 1, "VRSQRTBF16 with EVEX.W1")):
                    code = evex(fm.mp, fm.pp, w, 1, 2, 3 if op in (0x4D, 0x4F) else 0, 0, 0, 0, 0, op)
                    g.lines.append("# %s -> #UD" % why)
                    g.lines.append(".byte " + ", ".join("0x%02x" % b for b in code) + " | zmm2=803F => #UD")
            if fm.f is F16 and fm.op == 0x4C:
                # the FP16 forms are W0 only (SDM: EVEX.*.66.MAP6.W0)
                for op in (0x4C, 0x4D, 0x4E, 0x4F):
                    code = evex(fm.mp, fm.pp, 1, 1, 2, 3 if op in (0x4D, 0x4F) else 0, 0, 0, 0, 0, op)
                    g.lines.append("# MAP6 66 W1 %02X (the FP16 reciprocal forms are W0 only) -> #UD" % op)
                    g.lines.append(".byte " + ", ".join("0x%02x" % b for b in code) + " | zmm2=003C => #UD")
            # vvvv != 1111b -> #UD
            code = evex(fm.mp, fm.pp, fm.w, 1, 2, 5, 2, 0, 0, 0, fm.op)
            g.lines.append("# %s EVEX.vvvv != 1111b -> #UD" % fm.name)
            g.lines.append(".byte " + ", ".join("0x%02x" % b for b in code) + " | zmm2=0000803F => #UD")
        else:
            for mx, mxn in mxlist:
                for x in sp:
                    scalar_case(g, fm, x, mx, mxn)
            for x in randoms(f, 24, g.rng):
                scalar_case(g, fm, x, 0x1F80, "default")
            scalar_case(g, fm, sp[30], 0x1F80, "default", aaa=4, kval=0xFE)
            scalar_case(g, fm, sp[30], 0x1F80, "default", aaa=4, z=1, kval=0xFE)
            scalar_case(g, fm, sp[31], 0x1FC0, "DAZ", aaa=4, z=1, kval=0x01)
            scalar_case(g, fm, sp[12], 0x9F80, "FTZ", mem=True)
            scalar_case(g, fm, sp[40], 0x1F80, "default", regs=(20, 29, 11))


def header(out, files, opt):
    out.write("# Approximate reciprocal / reciprocal square root (decision A9, U940-U959): expected values from the\n")
    out.write("# independent model Emulator%stools%sisa%sref_rcp.py %s (regenerate, do not edit). Sources: SDM 092\n" % (BS, BS, BS, files))
    out.write("# Vol2C (VRCP14*/VRSQRT14* Tables 5-24/5-25/5-32..5-35, VRCPPH/VRSQRTPH Tables 5-26/5-36) and the AVX10.2\n")
    out.write("# spec 361050-007 (VRCPBF16/VRSQRTBF16 Tables 7.2/7.3). The documents give only an error bound; the\n")
    out.write("# expected values are the emulator's documented stand-in (correctly rounded, see docs%sreciprocal.md),\n" % BS)
    out.write("# NOT Intel silicon bits. The i5-13600K has no AVX-512: expected-value cases only:\n")
    out.write("#   %s\n" % opt)
    out.write("# RSI = MEM + 0x8000.\n")


def gen_hw():
    """hardware cases (no "=>"): the legacy forms on the i5-13600K vs Unicorn (U81 model)"""
    rng = random.Random(0x81)
    lines = []
    sp = specials(F32)
    # SDM RCPPS tiny-result guarantees and the interval end points of both models
    sp += [0x7E7FE800, 0x7E7FE7FF, 0x7E800C01, 0x7E800C00, 0x7E7FF000, 0x7E7FFFFF, 0xFE7FE800, 0xFE800C01]
    sp += [0x3F800000 | (h << 12) | t for h in (0, 1, 0x3FF, 0x400, 0x7FE, 0x7FF) for t in (0, 0xFFF)]
    sp += [(e << 23) | (h << 13) for e in (0x7E, 0x7F) for h in (0, 1, 0x1FF, 0x200, 0x3FF)]
    forms = [  # (title, asm, lanes, scalar)
        ("RCPPS", "rcpps xmm1, xmm2", 4, False), ("RSQRTPS", "rsqrtps xmm1, xmm2", 4, False),
        ("RCPSS", "rcpss xmm1, xmm2", 4, True), ("RSQRTSS", "rsqrtss xmm1, xmm2", 4, True),
        ("VRCPPS xmm", "vrcpps xmm1, xmm2", 4, False), ("VRCPPS ymm", "vrcpps ymm1, ymm2", 8, False),
        ("VRSQRTPS xmm", "vrsqrtps xmm1, xmm2", 4, False), ("VRSQRTPS ymm", "vrsqrtps ymm1, ymm2", 8, False),
        ("VRCPSS", "vrcpss xmm1, xmm3, xmm2", 4, True), ("VRSQRTSS", "vrsqrtss xmm1, xmm3, xmm2", 4, True),
    ]
    for title, asm, lanes, scalar in forms:
        lines.append("# === %s" % title)
        for mx, mxn in MX_MODES:
            step = 1 if scalar else lanes
            vals = sp if not scalar else sp[::2]
            for i in range(0, len(vals), step):
                ch = vals[i:i + step]
                ch = ch + [rng.getrandbits(32) for _ in range(lanes - len(ch))] if not scalar else ch + [rng.getrandbits(32) for _ in range(3)]
                src = "".join(hexle(v, 4) for v in ch[:4])
                inp = "xmm2=%s xmm1=%s xmm3=%s" % (src, hexle(rng.getrandbits(128), 16), hexle(rng.getrandbits(128), 16))
                if lanes == 8:
                    inp += " ymmh2=%s ymmh1=%s" % ("".join(hexle(v, 4) for v in ch[4:8]), hexle(rng.getrandbits(128), 16))
                else:
                    inp += " ymmh1=%s" % hexle(rng.getrandbits(128), 16)
                lines.append("%s | %s mxcsr=0x%X" % (asm, inp, mx))
        # memory operand forms
        m = asm.replace("xmm2", "xmmword ptr [rsi]" if not scalar else "dword ptr [rsi]").replace("ymm2", "ymmword ptr [rsi]")
        for _ in range(4):
            ch = [rng.choice(sp) for _ in range(lanes)]
            lines.append("%s | m+0x8000=%s xmm1=%s" % (m, "".join(hexle(v, 4) for v in ch), hexle(rng.getrandbits(128), 16)))
    return lines


def main():
    if "--cases-hw" in sys.argv:
        out = sys.stdout
        out.write("# Legacy RCPPS/RCPSS/RSQRTPS/RSQRTSS (+ VEX 128/256): hardware cases, the i5-13600K vs Unicorn (U81\n")
        out.write("# analytic model), decision A9 reference forms. Generated by Emulator%stools%sisa%sref_rcp.py --cases-hw\n" % (BS, BS, BS))
        out.write("# (regenerate, do not edit): every special value / boundary / SDM tiny-result guarantee / model\n")
        out.write("# interval end point under nine MXCSR settings (RC, DAZ, FTZ, unmasked, flags set). Only these\n")
        out.write("# self-generated snippets run natively:\n")
        out.write("#   emu-alltest --cases Emulator%sdata%scases_rcp_hw.txt --cpuid Emulator%sdata%scpuid_i5-13600k.txt\n" % (BS, BS, BS, BS))
        out.write("\n".join(gen_hw()) + "\n")
        return
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        g = Gen(0xA9)
        gen_forms(g, FORMS, False)
        out = sys.stdout
        header(out, "--cases", "emu-alltest --cases Emulator%sdata%scases_rcp.txt --avx512 --expect-only (also --avx10 1)" % (BS, BS))
        out.write("\n".join(g.lines) + "\n")
        return
    if "--cases-f16-all" in sys.argv:
        g = Gen(0xF16)
        gen_forms(g, FORMS, False, only_exhaustive=True)
        out = sys.stdout
        header(out, "--cases-f16-all", "emu-alltest --cases Emulator%sdata%scases_rcp_f16_all.txt --avx512 --expect-only" % (BS, BS))
        out.write("\n".join(g.lines) + "\n")
        return
    if "--cases-bf16" in sys.argv:
        g = Gen(0xBF16)
        gen_forms(g, FORMS_BF16, True)
        out = sys.stdout
        header(out, "--cases-bf16", "emu-alltest --cases Emulator%sdata%scases_rcp_bf16.txt --avx10 2 --expect-only" % (BS, BS))
        out.write("\n".join(g.lines) + "\n")
        return
    print(__doc__)


if __name__ == "__main__":
    main()
