#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_cases_sse_exc.py -- SSE/AVX/FMA post-computation exception hardware cases (ledger U445-U469),
Python 3 stdlib only.

  python gen_cases_sse_exc.py > Emulator/data/cases_sse_exc.txt
      hardware cases (no "=>"), run against the host CPU:
        emu-alltest --cases Emulator\\data\\cases_sse_exc.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt
                    --strict
      The emulator implements the SDM: DPPS checks for unmasked exceptions after the products,
      Temp2, Temp3 and Temp4 of each 128-bit DP_Primitive in turn. The i5-13600K checks after
      all products, after Temp2 and Temp3 together and after Temp4, both VDPPS halves per step
      (docs/quirks.md "DPPS exception step grouping"). Every (V)DPPS case where the two orders
      give a different outcome (dpps_steps.py, an exact binary32 model of each operation) is
      tagged "# known deviation: DPPS exception step grouping" (U538).
  python gen_cases_sse_exc.py --family NAME     only one family (see FAMILIES)

SDM rules exercised (Vol1 4.9.1.4-4.9.1.6, 10.2.3.3, 11.5.2.4-11.5.2.6):
  #O: the rounded result exceeds the largest finite value (rounding with the current RC).
  #U: "the result of rounding with unbounded exponent ... is non-zero and tiny" (tininess after
      rounding); masked: reported only when tiny AND inexact; unmasked: reported for every non-zero
      tiny result, exact or not. FTZ: masked underflow -> signed zero, UE and PE; "If the underflow
      exception is not masked, the flush-to-zero bit is ignored."
  #P with an unmasked #O/#U: "If an inexact result occurs along with unmasked overflow or underflow
      ... the OE or UE flag and the PE flag are set" where the result is the one rounded to the
      destination precision with an unbounded exponent (Table 4-12 note 1).
  Unmasked: #XM, destination (and its upper bits) unchanged, MXCSR flags set.
  DPPS/DPPD (Vol2A): "Exceptions are determined separately for each add and multiply operation,
      in the order of their execution"; an unmasked step invokes the handler before the next one.
Each case puts the interesting value in lane 0 (other lanes compute 1.0 op 1.0 style exact values)
so MXCSR names the flags of that value alone. Every value x exception mask x RC x FTZ/DAZ.
"""

import os
import sys
from fractions import Fraction as F

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dpps_steps  # noqa: E402  (the documented DPPS step-grouping deviation, U538)

OUT = []


def w(line=""):
    OUT.append(line)


# ---------------------------------------------------------------- encoding

class Fmt:
    def __init__(self, name, p, emin, emax, nbytes):
        self.name, self.p, self.emin, self.emax, self.n = name, p, emin, emax, nbytes
        self.u = F(1, 2 ** (p - 1))                         # ulp of 1.0
        self.MAX = (2 - self.u) * F(2) ** emax
        self.MINN = F(2) ** emin
        self.MIND = F(2) ** (emin - p + 1)


S = Fmt("s", 24, -126, 127, 4)
D = Fmt("d", 53, -1022, 1023, 8)

INF = "inf"
NINF = "-inf"
QNAN = "qnan"
NZERO = "-0"


_ENC = {}


def enc(fmt, v):
    """exact encoding of a Fraction (must be representable), or a special name"""
    key = (fmt.name, v if isinstance(v, str) else F(v))
    if key not in _ENC:
        _ENC[key] = _enc(fmt, v)
    return _ENC[key]


def _enc(fmt, v):
    ebits = fmt.n * 8 - fmt.p
    fbits = fmt.p - 1
    sbit = 1 << (fmt.n * 8 - 1)
    if v == INF:
        return ((1 << ebits) - 1) << fbits
    if v == NINF:
        return sbit | ((1 << ebits) - 1) << fbits
    if v == QNAN:
        return (((1 << ebits) - 1) << fbits) | (1 << (fbits - 1))
    if v == NZERO:
        return sbit
    v = F(v)
    sign = 0
    if v < 0:
        sign, v = sbit, -v
    if v == 0:
        return sign
    # floor(log2(v)) from the bit lengths, corrected by one step
    e = v.numerator.bit_length() - v.denominator.bit_length()
    if v < F(2) ** e:
        e -= 1
    e = max(e, fmt.emin)
    assert e <= fmt.emax, (fmt.name, v)
    if v < fmt.MINN:                                        # denormal
        m = v / fmt.MIND
        assert m.denominator == 1, ("not representable", fmt.name, v)
        return sign | int(m)
    m = v / F(2) ** e * F(2) ** fbits
    assert m.denominator == 1, ("not representable", fmt.name, float(v))
    return sign | ((e + (2 ** (ebits - 1) - 1)) << fbits) | (int(m) - (1 << fbits))


def neg(v):
    if v == INF:
        return NINF
    if v == NINF:
        return INF
    if v == QNAN:
        return v
    if v == 0 and v != NZERO:
        return NZERO
    if v == NZERO:
        return F(0)
    return -F(v)


def le(v, n):
    return "".join("%02X" % b for b in (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little"))


def reg(fmt, vals, nlanes):
    vals = list(vals) + [F(1)] * (nlanes - len(vals))
    return "".join(le(enc(fmt, v), fmt.n) for v in vals)


def xmm(fmt, lane0, rest=F(1)):
    return reg(fmt, [lane0] + [rest] * (16 // fmt.n - 1), 16 // fmt.n)


# ---------------------------------------------------------------- MXCSR configurations

MASKS = [("masked", 0x1F80), ("OM=0", 0x1F80 & ~0x0400), ("UM=0", 0x1F80 & ~0x0800),
         ("PM=0", 0x1F80 & ~0x1000), ("all unmasked", 0x0000)]
MASKS_DM = MASKS + [("DM=0", 0x1F80 & ~0x0100)]
RCS = [0, 1, 2, 3]                    # RN, RD, RU, RZ
FTZDAZ = [(0, 0), (1, 0), (0, 1), (1, 1)]


def mx(masks, rc, ftz, daz):
    return masks | (rc << 13) | (0x8000 if ftz else 0) | (0x40 if daz else 0)


def configs(masks=MASKS_DM, rcs=RCS, fd=FTZDAZ):
    for _, m in masks:
        for rc in rcs:
            for ftz, daz in fd:
                yield mx(m, rc, ftz, daz)


JUNK_X = "A5" * 16
JUNK_H = "5A" * 16

# ---------------------------------------------------------------- value sets (symbolic per format)


def add_vals(f):
    u, MAX, MINN, MIND = f.u, f.MAX, f.MINN, f.MIND
    E = f.emax
    return [
        ("exact normal 1+2", F(1), F(2)),
        ("inexact tie 1+2^-p", F(1), u / 2),
        ("exact tiny 1.5*minN - minN", F(3, 2) * MINN, -MINN),
        ("-exact tiny", -F(3, 2) * MINN, MINN),
        ("exact zero minN - minN", MINN, -MINN),
        ("denormal src minD + minN", MIND, MINN),
        ("denormal src tiny minN/2 + minD", MINN / 2, MIND),
        ("overflow tie MAX + half ulp", MAX, F(2) ** (E - f.p)),
        ("RU-only overflow MAX + quarter ulp", MAX, F(2) ** (E - f.p - 1)),
        ("-overflow tie", -MAX, -F(2) ** (E - f.p)),
        ("exact overflow MAX + MAX", MAX, MAX),
        ("exact large 2^emax + 2^(emax-1)", F(2) ** E, F(2) ** (E - 1)),
        ("denormal src minN - minD = max denormal (exact tiny)", MINN, -MIND),
    ]


def sub_vals(f):
    return [(n, a, neg(b)) for n, a, b in add_vals(f)]


def mul_vals(f):
    u, MAX, MINN, MIND = f.u, f.MAX, f.MINN, f.MIND
    E, m = f.emax, f.emin
    return [
        ("exact normal 1.5*2", F(3, 2), F(2)),
        ("inexact (1+u)^2", 1 + u, 1 + u),
        ("exact tiny 2^(emin+26)*2^-30", F(2) ** (m + 26), F(2) ** -30),
        ("-exact tiny", -F(2) ** (m + 26), F(2) ** -30),
        ("tiny unbounded-exact bounded-inexact", (1 + u) * F(2) ** (m + 26), F(2) ** -30),
        ("inexact tiny", (1 + u) * F(2) ** (m + 26), (1 + u) * F(2) ** -30),
        ("tiny rounds up to minN at RN: (1+u)minN*(1-u)", (1 + u) * MINN, 1 - u),
        ("-tiny rounds up to minN", -(1 + u) * MINN, 1 - u),
        ("tiny unbounded-exact, bounded tie: (1-u/2)*minN", 1 - u / 2, MINN),
        ("overflow threshold (1+u)2^emax*(2-2u)", (1 + u) * F(2) ** E, 2 - 2 * u),
        ("-overflow threshold", -(1 + u) * F(2) ** E, 2 - 2 * u),
        ("exact overflow 2^emax*2", F(2) ** E, F(2)),
        ("inexact overflow MAX*MAX", MAX, MAX),
        ("exact huge overflow 2^emax*2^emax", F(2) ** E, F(2) ** E),
        ("exact large MAX*1", MAX, F(1)),
        ("denormal src minD*2^100", MIND, F(2) ** 100),
        ("denormal src tiny tie minD*0.5", MIND, F(1, 2)),
        ("denormal src tiny exact (minN/2)*0.5", MINN / 2, F(1, 2)),
    ]


def div_vals(f):
    u, MAX, MINN, MIND = f.u, f.MAX, f.MINN, f.MIND
    E, m = f.emax, f.emin
    return [
        ("exact 3/2", F(3), F(2)),
        ("inexact 1/3", F(1), F(3)),
        ("exact tiny 2^(emin+26)/2^30", F(2) ** (m + 26), F(2) ** 30),
        ("exact tiny minN/2", MINN, F(2)),
        ("tiny unbounded-exact bounded-inexact", (1 + u) * F(2) ** (m + 26), F(2) ** 30),
        ("inexact tiny minN/3", MINN, F(3)),
        ("-inexact tiny", -MINN, F(3)),
        ("exact overflow 2^emax/0.5", F(2) ** E, F(1, 2)),
        ("exact overflow MAX/(1-u/2)", MAX, 1 - u / 2),
        ("overflow MAX/0.75 (exact for single, inexact for double)", MAX, F(3, 4)),
        ("exact large MAX/1", MAX, F(1)),
        ("divide by zero 1/0", F(1), F(0)),
        ("invalid 0/0", F(0), F(0)),
        ("denormal src minD/1 (exact tiny)", MIND, F(1)),
        ("denormal src 1/minD (overflow)", F(1), MIND),
    ]


def sqrt_vals(f):
    return [
        ("exact sqrt 4", F(4)),
        ("inexact sqrt 2", F(2)),
        ("exact sqrt minN*4", f.MINN * 4),
        ("denormal src minD", f.MIND),
        ("invalid sqrt -1", F(-1)),
        ("-0", NZERO),
    ]


def fma_vals(f):
    """(name, a, b, c): value = a*b + c"""
    u, MAX, MINN, MIND = f.u, f.MAX, f.MINN, f.MIND
    E, m = f.emax, f.emin
    return [
        ("exact 1*1+1", F(1), F(1), F(1)),
        ("inexact (1+u)(1+u)-1", 1 + u, 1 + u, F(-1)),
        ("exact tiny 2^(emin+26)*2^-30+0", F(2) ** (m + 26), F(2) ** -30, F(0)),
        ("exact tiny with addend", F(2) ** (m + 26), F(2) ** -30, F(2) ** (m - 4)),
        ("tiny unbounded-exact bounded-inexact", (1 + u) * F(2) ** (m + 26), F(2) ** -30, F(0)),
        ("inexact tiny", (1 + u) * F(2) ** (m + 26), (1 + u) * F(2) ** -30, F(0)),
        ("tiny rounds up to minN at RN", (1 + u) * MINN, 1 - u, F(0)),
        ("exact tiny by cancellation (1+u)minN(1-u)-minN", (1 + u) * MINN, 1 - u, -MINN),
        ("overflow threshold", (1 + u) * F(2) ** E, 2 - 2 * u, F(0)),
        ("exact overflow 2^emax*2+0", F(2) ** E, F(2), F(0)),
        ("exact large MAX*2-MAX (unbounded intermediate)", MAX, F(2), -MAX),
        ("RU-only overflow MAX*1 + quarter ulp", MAX, F(1), F(2) ** (E - f.p - 1)),
        ("inexact overflow MAX*MAX+1", MAX, MAX, F(1)),
        ("denormal src minD*1+0", MIND, F(1), F(0)),
        ("denormal addend 1*1+minD", F(1), F(1), MIND),
        ("invalid 0*inf+1", F(0), INF, F(1)),
    ]


def cvt_narrow_vals():
    """double sources for CVTPD2PS / CVTSD2SS"""
    u = S.u
    return [
        ("exact 1.5", F(3, 2)),
        ("inexact 1+2^-30", 1 + F(1, 2 ** 30)),
        ("exact tiny 2^-130", F(2) ** -130),
        ("-exact tiny", -F(2) ** -130),
        ("tiny unbounded-exact bounded-inexact 2^-130(1+u)", F(2) ** -130 * (1 + u)),
        ("inexact tiny 2^-130(1+2^-40)", F(2) ** -130 * (1 + F(1, 2 ** 40))),
        ("tiny rounds up to minN at RN: minN(1-2^-30)", S.MINN * (1 - F(1, 2 ** 30))),
        ("tiny unbounded-exact bounded tie minN(1-u/2)", S.MINN * (1 - u / 2)),
        ("below min denormal 2^-160", F(2) ** -160),
        ("exact overflow 2^128", F(2) ** 128),
        ("overflow tie MAX+half ulp", S.MAX + F(2) ** 103),
        ("RU-only overflow MAX+quarter ulp", S.MAX + F(2) ** 102),
        ("-overflow tie", -(S.MAX + F(2) ** 103)),
        ("exact large FLT_MAX", S.MAX),
        ("double denormal 2^-1070", F(2) ** -1070),
        ("double max", D.MAX),
        ("QNaN", QNAN),
    ]


# ---------------------------------------------------------------- families

OPS2 = {"add": add_vals, "sub": sub_vals, "mul": mul_vals, "div": div_vals}


def fam_arith(op):
    def gen():
        vals = OPS2[op]
        w("# ===== %s PS/SS/PD/SD: legacy SSE and VEX.128 (ymmh0 junk: VEX.128 zeroes it unless #XM)" % op.upper())
        for sfx, f in (("ps", S), ("ss", S), ("pd", D), ("sd", D)):
            for name, a, b in vals(f):
                w("# %s%s: %s" % (op, sfx, name))
                x1, x2 = xmm(f, a), xmm(f, b)
                for m in configs():
                    w("%s%s xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (op, sfx, m, x1, x2))
                    w("v%s%s xmm0, xmm1, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s xmm2=%s"
                      % (op, sfx, m, JUNK_X, JUNK_H, x1, x2))
        w("# ===== %s PS/PD VEX.256: value in lane 0 and lane 7 (other lanes exact)" % op.upper())
        for sfx, f in (("ps", S), ("pd", D)):
            nl = 16 // f.n
            for name, a, b in vals(f):
                w("# v%s%s ymm: %s" % (op, sfx, name))
                h1 = reg(f, [F(1)] * (nl - 1) + [a], nl)
                h2 = reg(f, [F(1)] * (nl - 1) + [b], nl)
                for m in configs(MASKS, (0, 3), ((0, 0), (1, 1))):
                    w("v%s%s ymm0, ymm1, ymm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s ymmh1=%s xmm2=%s ymmh2=%s"
                      % (op, sfx, m, JUNK_X, JUNK_H, xmm(f, a), h1, xmm(f, b), h2))
    return gen


def fam_sqrt():
    w("# ===== SQRT PS/SS/PD/SD: legacy and VEX.128")
    for sfx, f in (("ps", S), ("ss", S), ("pd", D), ("sd", D)):
        for name, a in sqrt_vals(f):
            w("# sqrt%s: %s" % (sfx, name))
            x2 = xmm(f, a, F(4))
            for m in configs():
                w("sqrt%s xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (sfx, m, JUNK_X, x2))
                if sfx in ("ps", "pd"):
                    w("vsqrt%s xmm0, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm2=%s"
                      % (sfx, m, JUNK_X, JUNK_H, x2))
                else:
                    w("vsqrt%s xmm0, xmm1, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s xmm2=%s"
                      % (sfx, m, JUNK_X, JUNK_H, xmm(f, F(1)), x2))


def fam_horiz():
    w("# ===== HADD/HSUB/ADDSUB PS/PD: lane 0 computes the value (HADD: a0+a1, HSUB: a0-a1,")
    w("# ADDSUB lane 0: a0-b0), other lanes exact")
    for f, sfx in ((S, "ps"), (D, "pd")):
        nl = 16 // f.n
        for name, a, b in add_vals(f):
            for op in ("hadd", "hsub", "addsub"):
                if op == "hadd":
                    s1 = reg(f, [a, b] + [F(1)] * (nl - 2), nl)
                    s2 = reg(f, [F(1)] * nl, nl)
                elif op == "hsub":
                    s1 = reg(f, [a, neg(b)] + [F(1)] * (nl - 2), nl)
                    s2 = reg(f, [F(1)] * nl, nl)
                else:
                    s1 = reg(f, [a] + [F(1)] * (nl - 1), nl)
                    s2 = reg(f, [neg(b)] + [F(1)] * (nl - 1), nl)
                w("# %s%s: %s" % (op, sfx, name))
                for m in configs():
                    w("%s%s xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (op, sfx, m, s1, s2))
                    w("v%s%s xmm0, xmm1, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s xmm2=%s"
                      % (op, sfx, m, JUNK_X, JUNK_H, s1, s2))


def fam_dp():
    w("# ===== DPPS/DPPD: products rounded, then summed (SDM DPPS operation); imm selects lanes")
    for f, sfx, imms in ((S, "ps", (0x11, 0x31, 0xF1, 0x1F)), (D, "pd", (0x11, 0x31, 0x33))):
        nl = 16 // f.n
        cases = [(n, a, b, F(0), F(0)) for n, a, b in mul_vals(f)]
        cases += [
            ("tiny product + exact: p0 tiny exact, p1 = -tiny", F(2) ** (f.emin + 26), F(2) ** -30,
             -F(2) ** (f.emin + 26), F(2) ** -30),
            ("tiny inexact product + 1", (1 + f.u) * F(2) ** (f.emin + 26), (1 + f.u) * F(2) ** -30,
             F(1), F(1)),
            ("two exact tiny products sum tiny", F(2) ** (f.emin + 26), F(2) ** -30,
             F(2) ** (f.emin + 26), F(2) ** -30),
            ("overflow product - overflow product", f.MAX, F(2), -f.MAX, F(2)),
            ("large products cancel to normal", f.MAX, F(1), -f.MAX, F(1, 2) + F(1, 2) * f.u),
        ]
        for name, a0, b0, a1, b1 in cases:
            s1 = reg(f, [a0, a1] + [F(1)] * (nl - 2), nl)
            s2 = reg(f, [b0, b1] + [F(1)] * (nl - 2), nl)
            w("# dp%s: %s" % (sfx, name))
            for imm in imms:
                for m in configs(MASKS_DM, RCS, ((0, 0), (1, 1))):
                    w("dp%s xmm1, xmm2, 0x%02X | mxcsr=0x%04X xmm1=%s xmm2=%s" % (sfx, imm, m, s1, s2))
                    w("vdp%s xmm0, xmm1, xmm2, 0x%02X | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s xmm2=%s"
                      % (sfx, imm, m, JUNK_X, JUNK_H, s1, s2))
    # DPPS step order: are the four products one step, Temp2 and Temp3 separate steps (SDM
    # pseudo-code checks after each), and the two 128-bit halves of VDPPS ymm one step?
    f, u = S, S.u
    one = [F(1)] * 4
    steps = [
        ("p0 exact tiny (U), p2 inexact (P): products one step?",
         [F(2) ** (f.emin + 26), F(0), 1 + u, F(0)], [F(2) ** -30, F(0), 1 + u, F(0)]),
        ("Temp2 exact tiny (U), Temp3 inexact (P)",
         [F(3, 2) * f.MINN, -f.MINN, F(1), u / 2], one),
        ("Temp2 inexact (P), Temp3 exact tiny (U)",
         [F(1), u / 2, F(3, 2) * f.MINN, -f.MINN], one),
        ("Temp2 overflow (O), Temp3 exact tiny (U)",
         [f.MAX, f.MAX, F(3, 2) * f.MINN, -f.MINN], one),
        ("Temp2 normal, Temp3 normal, Temp4 overflow",
         [f.MAX, F(0), f.MAX, F(0)], one),
        ("Temp4 exact tiny from normal Temp2/Temp3",
         [F(3, 2) * f.MINN, F(0), -f.MINN, F(0)], one),
    ]
    for name, a, b in steps:
        w("# dpps steps: %s" % name)
        for m in configs(MASKS_DM, (0, 3), ((0, 0), (1, 1))):
            w("dpps xmm1, xmm2, 0xF1 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (m, reg(f, a, 4), reg(f, b, 4)))
            # VDPPS ymm: the condition in the low half, then in the high half (other half exact)
            w("vdpps ymm0, ymm1, ymm2, 0xF1 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s ymmh1=%s xmm2=%s ymmh2=%s"
              % (m, JUNK_X, JUNK_H, reg(f, a, 4), reg(f, one, 4), reg(f, b, 4), reg(f, one, 4)))
            w("vdpps ymm0, ymm1, ymm2, 0xF1 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s ymmh1=%s xmm2=%s ymmh2=%s"
              % (m, JUNK_X, JUNK_H, reg(f, one, 4), reg(f, a, 4), reg(f, one, 4), reg(f, b, 4)))
    w("# vdpps ymm: low half Temp2 exact tiny (U), high half Temp2 inexact (P); and swapped")
    lo = [F(3, 2) * f.MINN, -f.MINN, F(0), F(0)]
    hi = [F(1), u / 2, F(0), F(0)]
    for A, B in ((lo, hi), (hi, lo)):
        for m in configs(MASKS_DM, (0, 3), ((0, 0), (1, 1))):
            w("vdpps ymm0, ymm1, ymm2, 0x33 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s ymmh1=%s xmm2=%s ymmh2=%s"
              % (m, JUNK_X, JUNK_H, reg(f, A, 4), reg(f, B, 4), reg(f, one, 4), reg(f, one, 4)))


FMA_KIND = ["fmadd", "fmsub", "fnmadd", "fnmsub"]


def fma_forms():
    for k in FMA_KIND:
        for o in ("132", "213", "231"):
            for t in ("ps", "pd", "ss", "sd"):
                yield k, o, t
    for k in ("fmaddsub", "fmsubadd"):
        for o in ("132", "213", "231"):
            for t in ("ps", "pd"):
                yield k, o, t


def fam_fma():
    w("# ===== FMA3 (all 60 VEX forms): lane 0 computes a*b+c (signs adjusted per form so every")
    w("# form evaluates the named value); 132: d=d*s3+s2, 213: d=s2*d+s3, 231: d=s2*s3+d")
    for k, o, t in fma_forms():
        f = S if t in ("ps", "ss") else D
        for name, a, b, c in fma_vals(f):
            # lane 0 operation: fmadd a*b+c, fmsub a*b-c, fnmadd -(a*b)+c, fnmsub -(a*b)-c,
            # fmaddsub lane 0 = a*b-c, fmsubadd lane 0 = a*b+c
            aa, cc = a, c
            if k in ("fmsub", "fnmsub", "fmaddsub"):
                cc = neg(c)
            if k in ("fnmadd", "fnmsub"):
                aa = neg(a)
            if o == "132":
                r0, r1, r2 = aa, cc, b
            elif o == "213":
                r0, r1, r2 = b, aa, cc
            else:
                r0, r1, r2 = cc, aa, b
            w("# v%s%s%s: %s" % (k, o, t, name))
            for m in configs(MASKS_DM, RCS, ((0, 0), (1, 0), (1, 1))):
                w("v%s%s%s xmm0, xmm1, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s xmm2=%s"
                  % (k, o, t, m, xmm(f, r0), JUNK_H, xmm(f, r1), xmm(f, r2)))
    w("# ===== FMA3 VEX.256 subset (vfmadd231ps/pd ymm), value in lane 0 and the top lane")
    for t, f in (("ps", S), ("pd", D)):
        nl = 16 // f.n
        for name, a, b, c in fma_vals(f):
            w("# vfmadd231%s ymm: %s" % (t, name))
            for m in configs(MASKS, (0, 3), ((0, 0), (1, 1))):
                w("vfmadd231%s ymm0, ymm1, ymm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s ymmh1=%s xmm2=%s ymmh2=%s"
                  % (t, m, xmm(f, c), reg(f, [F(1)] * (nl - 1) + [c], nl), xmm(f, a),
                     reg(f, [F(1)] * (nl - 1) + [a], nl), xmm(f, b), reg(f, [F(1)] * (nl - 1) + [b], nl)))


def fam_cvt():
    w("# ===== CVTPD2PS / CVTSD2SS (narrowing): legacy and VEX.128")
    for name, v in cvt_narrow_vals():
        x2 = reg(D, [v, F(1)], 2)
        w("# %s" % name)
        for m in configs():
            w("cvtpd2ps xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (m, JUNK_X, x2))
            w("cvtsd2ss xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (m, JUNK_X, x2))
            w("vcvtpd2ps xmm0, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm2=%s" % (m, JUNK_X, JUNK_H, x2))
            w("vcvtsd2ss xmm0, xmm1, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s xmm2=%s"
              % (m, JUNK_X, JUNK_H, JUNK_X, x2))
    w("# ===== VCVTPD2PS ymm source")
    for name, v in cvt_narrow_vals():
        for m in configs(MASKS, (0, 3), ((0, 0), (1, 1))):
            w("vcvtpd2ps xmm0, ymm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm2=%s ymmh2=%s"
              % (m, JUNK_X, JUNK_H, reg(D, [F(1), F(1)], 2), reg(D, [F(1), v], 2)))
    w("# ===== CVTSS2SD / CVTPS2PD (widening: exact; only IE/DE)")
    for name, v in (("exact 1.5", F(3, 2)), ("float denormal minD", S.MIND), ("float max", S.MAX),
                    ("tiny normal minN", S.MINN), ("QNaN", QNAN)):
        x2 = xmm(S, v)
        for m in configs():
            w("cvtps2pd xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (m, JUNK_X, x2))
            w("cvtss2sd xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (m, JUNK_X, x2))
    w("# ===== CVTSI2SS / CVTSI2SD / CVTDQ2PS (precision only)")
    ints = [0x01000001, 0x7FFFFFFF, -0x7FFFFFFF, 3, 0x00FFFFFF, 0x20000001 << 32 | 1,
            0x7FFFFFFFFFFFFFFF, 1 << 53 | 1, -(1 << 63)]
    for v in ints:
        for m in configs(MASKS, RCS, ((0, 0),)):
            r = v & 0xFFFFFFFFFFFFFFFF
            w("cvtsi2ss xmm1, eax | mxcsr=0x%04X xmm1=%s rax=0x%X" % (m, JUNK_X, r))
            w("cvtsi2ss xmm1, rax | mxcsr=0x%04X xmm1=%s rax=0x%X" % (m, JUNK_X, r))
            w("cvtsi2sd xmm1, rax | mxcsr=0x%04X xmm1=%s rax=0x%X" % (m, JUNK_X, r))
            w("vcvtsi2ss xmm0, xmm1, rax | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s rax=0x%X"
              % (m, JUNK_X, JUNK_H, JUNK_X, r))
            w("vcvtsi2sd xmm0, xmm1, rax | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s rax=0x%X"
              % (m, JUNK_X, JUNK_H, JUNK_X, r))
            d = le(r & 0xFFFFFFFF, 4) + le(1, 4) * 3
            w("cvtdq2ps xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (m, JUNK_X, d))
    w("# ===== CVTPS2DQ / CVTSD2SI / CVTTPD2DQ (to integer: IE, PE)")
    for name, v in (("exact 3", F(3)), ("inexact 2.5", F(5, 2)), ("denormal", S.MIND), ("big", F(2) ** 40)):
        for m in configs(MASKS_DM, RCS, ((0, 0), (0, 1))):
            w("cvtps2dq xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (m, JUNK_X, xmm(S, v)))
            w("cvttps2dq xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (m, JUNK_X, xmm(S, v)))
            w("cvtsd2si eax, xmm2 | mxcsr=0x%04X rax=0x1111111111111111 xmm2=%s" % (m, xmm(D, v)))


def fam_round():
    w("# ===== ROUNDPS/SS/PD/SD: precision only (imm8[3] = 1 suppresses it), RC from imm8 or MXCSR")
    vals = [("exact 3", F(3)), ("inexact 2.5", F(5, 2)), ("inexact -0.75", F(-3, 4)),
            ("denormal minD", None), ("min normal", None), ("QNaN", QNAN)]
    for sfx, f in (("ps", S), ("ss", S), ("pd", D), ("sd", D)):
        for name, v in vals:
            if name == "denormal minD":
                v = f.MIND
            elif name == "min normal":
                v = f.MINN
            x2 = xmm(f, v)
            for imm in (0x0, 0x1, 0x4, 0x8, 0xC, 0xB):
                for m in configs(MASKS_DM, (0, 2), ((0, 0), (1, 1))):
                    w("round%s xmm1, xmm2, 0x%X | mxcsr=0x%04X xmm1=%s xmm2=%s" % (sfx, imm, m, JUNK_X, x2))
                    if sfx in ("ps", "pd"):
                        w("vround%s xmm0, xmm2, 0x%X | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm2=%s"
                          % (sfx, imm, m, JUNK_X, JUNK_H, x2))


def fam_rcp():
    w("# ===== RCPPS/RCPSS/RSQRTPS/RSQRTSS: no floating-point exceptions at all")
    vals = [F(1), F(3), S.MIND, S.MINN, F(2) ** 127 * 3 / 2, F(0), F(-1), INF, QNAN, S.MAX]
    for v in vals:
        x2 = xmm(S, v)
        for m in configs(MASKS_DM, (0, 3), FTZDAZ):
            for op in ("rcpps", "rcpss", "rsqrtps", "rsqrtss"):
                w("%s xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (op, m, JUNK_X, x2))
            w("vrcpps xmm0, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm2=%s" % (m, JUNK_X, JUNK_H, x2))
            w("vrsqrtps xmm0, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm2=%s" % (m, JUNK_X, JUNK_H, x2))


def fam_minmax():
    w("# ===== MIN/MAX/CMP PS/SS/PD/SD: no #O/#U/#P; denormal -> DE, QNaN for ordered CMP -> IE")
    for sfx, f in (("ps", S), ("ss", S), ("pd", D), ("sd", D)):
        for name, a, b in (("denormal vs 1", f.MIND, F(1)), ("minN vs -minN", f.MINN, -f.MINN),
                           ("MAX vs inf", f.MAX, INF), ("tiny exact? minN/2 vs minN", f.MINN / 2, f.MINN),
                           ("QNaN vs 1", QNAN, F(1))):
            x1, x2 = xmm(f, a), xmm(f, b)
            for m in configs(MASKS_DM, (0,), FTZDAZ):
                for op in ("min", "max"):
                    w("%s%s xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (op, sfx, m, x1, x2))
                for imm in (1, 3):
                    w("cmp%s xmm1, xmm2, %d | mxcsr=0x%04X xmm1=%s xmm2=%s" % (sfx, imm, m, x1, x2))
                w("vcmp%s xmm0, xmm1, xmm2, 0x11 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s xmm2=%s"
                  % (sfx, m, JUNK_X, JUNK_H, x1, x2))
                if sfx in ("ss", "sd"):
                    w("comi%s xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (sfx, m, x1, x2))


def fam_mixed():
    w("# ===== packed lanes with different conditions (flags OR-ed; any unmasked lane -> #XM)")
    for f, sfx in ((S, "ps"), (D, "pd")):
        nl = 16 // f.n
        E, m_ = f.emax, f.emin
        a = [F(2) ** E, F(2) ** (m_ + 26)] + [F(1)] * (nl - 2)
        b = [F(2), F(2) ** -30] + [F(1)] * (nl - 2)
        a2 = [(1 + f.u) * F(2) ** (m_ + 26), f.MAX] + [F(1)] * (nl - 2)
        b2 = [(1 + f.u) * F(2) ** -30, f.MAX] + [F(1)] * (nl - 2)
        for A, B in ((a, b), (a2, b2)):
            for m in configs():
                w("mul%s xmm1, xmm2 | mxcsr=0x%04X xmm1=%s xmm2=%s" % (sfx, m, reg(f, A, nl), reg(f, B, nl)))
                w("vmul%s xmm0, xmm1, xmm2 | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s xmm2=%s"
                  % (sfx, m, JUNK_X, JUNK_H, reg(f, A, nl), reg(f, B, nl)))
    w("# ===== memory source and memory destination forms")
    for m in configs(MASKS, (0, 3), FTZDAZ):
        w("mulps xmm1, xmmword ptr [rsi+0x100] | mxcsr=0x%04X xmm1=%s m+0x8100=%s"
          % (m, xmm(S, (1 + S.u) * F(2) ** (S.emin + 26)), xmm(S, F(2) ** -30)))
        w("vmulsd xmm0, xmm1, qword ptr [rsi+0x100] | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s m+0x8100=%s"
          % (m, JUNK_X, JUNK_H, xmm(D, F(2) ** (D.emin + 26)), le(enc(D, F(2) ** -30), 8)))
        w("vfmadd213ss xmm0, xmm1, dword ptr [rsi+0x100] | mxcsr=0x%04X xmm0=%s ymmh0=%s xmm1=%s m+0x8100=%s"
          % (m, xmm(S, F(2) ** -30), JUNK_H, xmm(S, F(2) ** (S.emin + 26)), le(enc(S, F(0)), 4)))


DPPS_DEVIATION = " # known deviation: DPPS exception step grouping"

FAMILIES = {
    "add": fam_arith("add"), "sub": fam_arith("sub"), "mul": fam_arith("mul"), "div": fam_arith("div"),
    "sqrt": fam_sqrt, "horiz": fam_horiz, "dp": fam_dp, "fma": fam_fma, "cvt": fam_cvt,
    "round": fam_round, "rcp": fam_rcp, "minmax": fam_minmax, "mixed": fam_mixed,
}


def main():
    only = None
    if len(sys.argv) > 2 and sys.argv[1] == "--family":
        only = sys.argv[2].split(",")
    w("# SSE/AVX/FMA post-computation exceptions (ledger U445-U469): hardware cases, generated by")
    w("# Emulator/tools/isa/gen_cases_sse_exc.py (regenerate, do not edit). Only these self-generated")
    w("# snippets run natively:")
    w("#   emu-alltest --cases Emulator\\data\\cases_sse_exc.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt --strict")
    w("# The emulator implements the SDM DPPS step order; the cases where the i5-13600K's grouping gives")
    w("# another outcome are tagged \"# known deviation: DPPS exception step grouping\" (docs/quirks.md).")
    w("# SDM Vol1 4.9.1.4-4.9.1.6, 10.2.3.3, 11.5.2.4-11.5.2.6: tininess after rounding (unbounded")
    w("# exponent); unmasked #U for every non-zero tiny result; PE with unmasked #O/#U from the")
    w("# unbounded-exponent rounding; FTZ ignored when UM = 0; #XM leaves the destination unchanged.")
    for name, fn in FAMILIES.items():
        if only and name not in only:
            continue
        w("# ################ family %s" % name)
        fn()
    for i, line in enumerate(OUT):
        if not line.startswith("#") and dpps_steps.deviates(line):
            OUT[i] = line + DPPS_DEVIATION
    sys.stdout.write("\n".join(OUT) + "\n")


if __name__ == "__main__":
    main()
