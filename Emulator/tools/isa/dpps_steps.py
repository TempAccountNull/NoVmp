#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
dpps_steps.py -- which (V)DPPS cases show the documented i5-13600K deviation "DPPS exception
step grouping" (docs/quirks.md; ledger U446, quirk removed in U538). Python 3 stdlib only.

The emulator implements the SDM (DPPS Operation + "Exceptions are determined separately for
each add and multiply operation, in the order of their execution"): DP_Primitive on each 128-bit
half in turn, the #XM handler check after the products, after Temp2, after Temp3 and after Temp4.
The i5-13600K checks after all products (both halves), after Temp2 and Temp3 together (both
halves) and after Temp4 (both halves). Both orders run the same operations on the same values, so
they can only differ in where an unmasked step stops and which flags are set by then.

deviates(line) evaluates both orders on the operands of a "dpps xmm1, xmm2, imm" or
"vdpps ymm0, ymm1, ymm2, imm" case line (gen_cases_sse_exc.py syntax) with an exact binary32
model of every multiply and add (Vol1 4.9.1.4-4.9.1.6, 10.2.3.3, 11.5.2: tininess after rounding
with an unbounded exponent, masked underflow only when inexact, unmasked #U for every tiny result,
FTZ / DAZ, #D on denormal operands, an unmasked #I/#D suppresses the step's #O/#U/#P) and returns
True when the outcome (#XM or not, MXCSR flags) of the two orders differs.

  python dpps_steps.py FILE     prints the case index and line of every deviating case in FILE
"""

import re
import sys
from fractions import Fraction as F

P, EMIN, EMAX = 24, -126, 127
MINN = F(2) ** EMIN
MIND = F(2) ** (EMIN - P + 1)
MAX = (2 - F(1, 2 ** (P - 1))) * F(2) ** EMAX
IE, DE, ZE, OE, UE, PE = 1, 2, 4, 8, 16, 32
INF = "inf"


def decode(u):
    """binary32 bits -> Fraction, ('inf', sign) or 'nan'"""
    s = -1 if u >> 31 else 1
    e = (u >> 23) & 0xFF
    m = u & 0x7FFFFF
    if e == 0xFF:
        return "nan" if m else (INF, s)
    if e == 0:
        return s * m * MIND
    return s * (m | 0x800000) * F(2) ** (e - 150)


def is_den(v):
    return isinstance(v, F) and v != 0 and abs(v) < MINN


def ulog2(x):
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if x < F(2) ** e:
        e -= 1
    return e


def round_to(x, q, rc, neg):
    """round |x| to a multiple of q; rc 0 RN, 1 RD, 2 RU, 3 RZ, neg = sign of the value"""
    n = x / q
    lo = n.numerator // n.denominator
    frac = n - lo
    if frac == 0:
        return lo * q
    up = (rc == 0 and (frac > F(1, 2) or (frac == F(1, 2) and lo % 2 == 1))) or \
         (rc == 1 and neg) or (rc == 2 and not neg)
    return (lo + 1) * q if up else lo * q


def round32(v, mx):
    """exact non-zero Fraction -> (result, flags) under MXCSR mx (masks, RC, FTZ)"""
    rc = (mx >> 13) & 3
    neg = v < 0
    a = abs(v)
    q = F(2) ** (ulog2(a) - P + 1)
    r = round_to(a, q, rc, neg)                     # unbounded exponent
    inexact = r != a
    if r > MAX:
        if not (mx & 0x0400):                       # OM = 0: #O (+P if inexact)
            return (INF, -1 if neg else 1), OE | (PE if inexact else 0)
        big = (rc == 0) or (rc == 1 and neg) or (rc == 2 and not neg)
        return ((INF, -1 if neg else 1) if big else (-MAX if neg else MAX)), OE | PE
    if r < MINN:                                    # tiny after rounding
        if not (mx & 0x0800):                       # UM = 0: #U for every tiny result
            return (-r if neg else r), UE | (PE if inexact else 0)
        if mx & 0x8000:                             # FTZ: signed zero, UE and PE
            return (F(0) if not neg else -F(0)), UE | PE
        d = round_to(a, MIND, rc, neg)
        if d != a:
            return (-d if neg else d), UE | PE
        return (-d if neg else d), 0
    return (-r if neg else r), (PE if inexact else 0)


def operand(v, mx):
    """DAZ / #D for one source of an operation: (value, flags)"""
    if is_den(v):
        if mx & 0x40:
            return F(0), 0
        return v, DE
    return v, 0


def fop(kind, x, y, mx):
    """one binary32 multiply or add of x, y: (result, flags)"""
    x, fx = operand(x, mx)
    y, fy = operand(y, mx)
    fl = fx | fy
    if x == "nan" or y == "nan":
        return "nan", fl
    if kind == "mul":
        if isinstance(x, tuple) or isinstance(y, tuple):
            if (isinstance(x, F) and x == 0) or (isinstance(y, F) and y == 0):
                return "nan", fl | IE
            sx = x[1] if isinstance(x, tuple) else (1 if x > 0 else -1)
            sy = y[1] if isinstance(y, tuple) else (1 if y > 0 else -1)
            return (INF, sx * sy), fl
        p = x * y
    else:
        if isinstance(x, tuple) and isinstance(y, tuple):
            if x[1] != y[1]:
                return "nan", fl | IE
            return x, fl
        if isinstance(x, tuple):
            return x, fl
        if isinstance(y, tuple):
            return y, fl
        p = x + y
    if p == 0:
        return F(0), fl
    r, f = round32(p, mx)
    return r, fl | f


def step_flags(ops, mx):
    """flags of one checked step (list of per-operation flags); unmasked IE/DE/ZE suppress OE/UE/PE"""
    f = 0
    for o in ops:
        f |= o
    unmasked = f & ~(mx >> 7) & 0x3F
    if unmasked & (IE | DE | ZE):
        f &= ~(OE | UE | PE)
    return f, unmasked != 0


def outcome(steps, mx):
    acc = 0
    for ops in steps:
        f, stop = step_flags(ops, mx)
        acc |= f
        if stop:
            return True, acc
    return False, acc


def half_ops(a, b, imm, mx):
    """per-operation flags of one 128-bit DP_Primitive: (products, temp2, temp3, temp4)"""
    prod, pf = [], []
    for k in range(4):
        if imm & (0x10 << k):
            r, f = fop("mul", a[k], b[k], mx)
        else:
            r, f = F(0), 0
        prod.append(r)
        pf.append(f)
    t2, f2 = fop("add", prod[0], prod[1], mx)
    t3, f3 = fop("add", prod[2], prod[3], mx)
    _, f4 = fop("add", t2, t3, mx)
    return pf, f2, f3, f4


def deviates_ops(srcs1, srcs2, imm, mx):
    """srcs1/srcs2: lists of 4-element halves (Fractions). True when the SDM order and the
    i5-13600K grouping give a different outcome"""
    halves = [half_ops(a, b, imm, mx) for a, b in zip(srcs1, srcs2)]
    sdm = []
    for pf, f2, f3, f4 in halves:
        sdm += [pf, [f2], [f3], [f4]]
    cpu = [[f for h in halves for f in h[0]], [f for h in halves for f in (h[1], h[2])], [h[3] for h in halves]]
    return outcome(sdm, mx) != outcome(cpu, mx)


def lanes(hexs):
    return [decode(int.from_bytes(bytes.fromhex(hexs[i:i + 8]), "little")) for i in range(0, 32, 8)]


LINE = re.compile(r"^(v?dpps) (xmm1, xmm2|ymm0, ymm1, ymm2|xmm0, xmm1, xmm2), (0x[0-9A-Fa-f]+) \| (.*)$")


def deviates(line):
    """True when the case line is a (V)DPPS case whose outcome differs between the two orders"""
    m = LINE.match(line.split(" # ")[0].strip())
    if not m:
        return False
    imm = int(m.group(3), 16)
    keys = dict(kv.split("=", 1) for kv in m.group(4).split())
    mx = int(keys["mxcsr"], 16)
    if m.group(1) == "dpps":
        s1, s2 = [lanes(keys["xmm1"])], [lanes(keys["xmm2"])]
    elif m.group(2).startswith("ymm"):
        s1 = [lanes(keys["xmm1"]), lanes(keys["ymmh1"])]
        s2 = [lanes(keys["xmm2"]), lanes(keys["ymmh2"])]
    else:
        s1, s2 = [lanes(keys["xmm1"])], [lanes(keys["xmm2"])]
    return deviates_ops(s1, s2, imm, mx)


def main():
    n = 0
    with open(sys.argv[1]) as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            if deviates(line):
                print("[%d] %s" % (n, line))
            n += 1


if __name__ == "__main__":
    main()
