#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_amx_tf32.py -- independent reference model of AMX-TF32 TMMULTF32PS (Python 3 stdlib only;
ledger U1150, plan 1.J.2, user decision 2026-10-09 / 2026-10-10)

Source: Intel Architecture Instruction Set Extensions Programming Reference 319433-060 (NOV 2025),
the last revision that defines the instruction (-057 and -058 carry the same text; -062 removed it:
"The ... TMMULTF32PS instructions were removed", CPUID.1EH.01H:EAX[6] reserved):
  * 3.7 TMMULTF32PS, pp. 3-32/3-33: VEX.128.66.0F38.W0 48 11:rrr:bbb, Op/En A (ModRM:reg r/w =
    tsrcdest, ModRM:r/m r = tsrc1, VEX.vvvv r = tsrc2), CPUID AMX-TF32, 64-bit mode only; the
    helpers zero_lower_mantissa_bits_fp32 (keep x[31:13]) and silence_snan_fp32; exceptions AMX-E4;
  * Table 1-3 p. 1-27: CPUID.(EAX=1EH,ECX=1):EAX[6] "AMX-TF32 (FP19)";
  * 3.6 Table 3-2 p. 3-12: AMX-E4; 3.4 p. 3-7: fma32(acc, x, y, daz, ftz, sae, rc) ("traditional
    infinite precision fma"); 3.4 write_row_and_zero / zero_upper_rows / zero_tileconfig_start.
The tile machine, TILECFG checks, AMX-E4 checks, encodings of LDTILECFG / TILELOADD / TILESTORED and
the deterministic element generator come from ref_amx.py (independent model of SDM Vol1 ch. 19 /
Vol2A 2.10). The FP32 arithmetic below is written again here (own rounding code) and cross-checked
against ref_amx.fma32 / add32 by --selftest.

UNVERIFIED - THE PSEUDOCODE IS SELF-CONTRADICTORY on input denormals:
  Description (p. 3-32): "Output FP32 denormals are always flushed to zero. Input single precision
                          denormals are always handled and not treated as zero."
  Operation   (p. 3-33): "// FP32 FMA with DAZ=FTZ=1, RNE rounding." (k-loop) and
                          "// DAZ=FTZ=1, RNE rounding." (final add)
READING = "B" (the emulator's): the Operation: fma32 / add with DAZ = FTZ = 1, the arithmetic of
TDPBF16PS. Justification: (1) the Operation is the algorithm; it states DAZ twice; (2) the only
executable statement of this arithmetic for the AMX-TF32 feature, TTMMULTF32PS (ISE -058 p. 3-48,
"AMX-TRANSPOSE AND AMX-TF32"), calls "fma32(temp1.fp32[n], s1e, s2e, daz=1, ftz=1, sae=1, rc=RNE)"
and has FP32 add "with DAZ=FTZ=1"; (3) the Operation was copied from TDPFP16PS (SDM Vol2B) and the
copy dropped TDPFP16PS's "Handle FP16 denorms. Not forcing input FP16 denorms to 0." line, while
the Description sentence is TDPFP16PS's FP16 sentence with "FP16" replaced - in TDPFP16PS it is
consistent with DAZ = 1 only because FP16 denormals become normal FP32 values.
READING = "A" (rejected, docs/quirks.md): the Description: no DAZ (tsrc1 / tsrc2 / tsrcdest
denormals used as they are), FTZ = 1 on every result. Selectable here (--reading A) for
documentation; the case file uses B.

Other choices (the ISE text leaves them open; the emulator makes the same, fpu_helper.c U1150):
  * "temp1.fp32[n] += a * b" is one fused operation (one rounding): "FP32 FMA" (comment and
    Description "each accumulation of the FMA"; TTMMULTF32PS fma32);
  * write_row_and_zero(tsrcdest, m, tmp, ...) names an undefined "tmp": the row just computed
    ("srcdest.row[m]" of the line before it, srcdest = tsrcdest);
  * FTZ as MXCSR.FTZ defines it (U596, as ref_amx.py): a result that is tiny after rounding with an
    unbounded exponent becomes a zero with the sign of the true result;
  * NaN operands: the first NaN of x, y, acc (fma32: v = x*y + acc; x = tsrc1 element, y = tsrc2
    element, acc = temp1) or of a, b (add: a = tsrcdest element, b = temp1), quieted; invalid
    (inf * 0, inf - inf) -> QNaN indefinite FFC00000h (the convention of the other AMX FP
    operations, U178); an exact zero sum is +0 unless both addends are -0;
  * silence_snan_fp32 runs before zero_lower_mantissa_bits_fp32 (the order of the pseudocode), so
    an SNaN whose payload is only in bits 12:0 stays a NaN (7F800001h -> 7FC00000h).

Usage:
  python ref_amx_tf32.py --selftest            hand-derived checks + cross-check vs ref_amx.py
  python ref_amx_tf32.py --cases               Emulator/data/cases_amx_tf32.txt (stdout)
  python ref_amx_tf32.py --cases --reading A   the same programs under the rejected reading
                                                (documentation only, not a test file)

Written from the Intel documents only (not from any C implementation).
"""

import os
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_amx as R                                  # noqa: E402

AMX_TF32 = 256                  # UC_X86_AMX_TF32 (unicorn.h, U1150): never part of AMX_ALL (255)
QNAN_INDEFINITE = 0xFFC00000
UD, NM = R.UD, R.NM


# --------------------------------------------------------------------------
# binary32 (own code; cross-checked against ref_amx.py by --selftest)
# --------------------------------------------------------------------------
def sgn(b):
    return (b >> 31) & 1


def is_nan(b):
    return (b & 0x7F800000) == 0x7F800000 and (b & 0x007FFFFF) != 0


def is_inf(b):
    return (b & 0x7FFFFFFF) == 0x7F800000


def is_zero(b):
    return (b & 0x7FFFFFFF) == 0


def is_denormal(b):
    return (b & 0x7F800000) == 0 and (b & 0x007FFFFF) != 0


def value(b):
    """exact value of a finite binary32 as a Fraction"""
    e, f = (b >> 23) & 0xFF, b & 0x7FFFFF
    m, x = (f, -149) if e == 0 else (f | 0x800000, e - 150)
    v = Fraction(m) * (Fraction(2) ** x)
    return -v if sgn(b) else v


def _round_int(q):
    """round the non-negative Fraction q to an integer, ties to even"""
    n = q.numerator // q.denominator
    r = q - n
    if r > Fraction(1, 2) or (r == Fraction(1, 2) and n & 1):
        n += 1
    return n


def _ilog2(a):
    """floor(log2(a)) of a positive Fraction"""
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a:
        e -= 1
    elif Fraction(2) ** (e + 1) <= a:
        e += 1
    return e


def to_f32(v, ftz):
    """the non-zero exact v rounded to binary32, RNE, gradual underflow, overflow to infinity;
    ftz: an underflow condition (|v| rounded to 24 bits with an unbounded exponent is below
    2^-126, SDM Vol1 11.5.2.5) gives a zero with the sign of v (MXCSR.FTZ, Vol1 10.2.3.3; U596)"""
    s = 1 if v < 0 else 0
    a = -v if v < 0 else v
    e = _ilog2(a)
    if ftz:
        q = _round_int(a / (Fraction(2) ** (e - 23)))
        if q * (Fraction(2) ** (e - 23)) < Fraction(2) ** -126:
            return s << 31
    lsb = max(e, -126) - 23
    q = _round_int(a / (Fraction(2) ** lsb))
    if q == 1 << 24:
        q, lsb = 1 << 23, lsb + 1
    if q < (1 << 23):                    # denormal (lsb = -149) or zero
        return (s << 31) | q
    be = lsb + 150
    if be >= 255:
        return (s << 31) | 0x7F800000
    return (s << 31) | (be << 23) | (q - (1 << 23))


def _daz(b, on):
    return b & 0x80000000 if on and is_denormal(b) else b


def fma32(acc, x, y, daz, ftz=True):
    """ISE 319433-060 3.4 fma32(acc, x, y, daz, ftz, sae=1, rc=RNE): v = (x*y) + acc, one
    rounding; NaN / invalid as documented in the module header"""
    x, y, acc = _daz(x, daz), _daz(y, daz), _daz(acc, daz)
    for o in (x, y, acc):
        if is_nan(o):
            return o | 0x00400000
    ps = sgn(x) ^ sgn(y)
    if (is_inf(x) and is_zero(y)) or (is_inf(y) and is_zero(x)):
        return QNAN_INDEFINITE
    if is_inf(x) or is_inf(y):
        if is_inf(acc) and sgn(acc) != ps:
            return QNAN_INDEFINITE
        return (ps << 31) | 0x7F800000
    if is_inf(acc):
        return acc
    p = value(x) * value(y)
    v = p + value(acc)
    if v == 0:
        if p == 0 and is_zero(acc):
            return (ps & sgn(acc)) << 31
        return 0
    return to_f32(v, ftz)


def add32(a, b, daz, ftz=True):
    """a + b, RNE (the final accumulation into tsrcdest)"""
    a, b = _daz(a, daz), _daz(b, daz)
    for o in (a, b):
        if is_nan(o):
            return o | 0x00400000
    if is_inf(a) and is_inf(b):
        return a if sgn(a) == sgn(b) else QNAN_INDEFINITE
    if is_inf(a):
        return a
    if is_inf(b):
        return b
    v = value(a) + value(b)
    if v == 0:
        if is_zero(a) and is_zero(b):
            return (sgn(a) & sgn(b)) << 31
        return 0
    return to_f32(v, ftz)


# --------------------------------------------------------------------------
# ISE 319433-060 3.7 TMMULTF32PS helpers, literally
# --------------------------------------------------------------------------
def silence_snan_fp32(x):
    # "SNAN has exponent 255 and high fraction bit set to 0 and nonzero other fraction bits"
    if (x >> 23) & 0xFF == 255 and (x & 0x7FFFFF) != 0 and not (x >> 22) & 1:
        x |= 1 << 22
    return x


def zero_lower_mantissa_bits_fp32(x):
    # "the output is a fp32 number with the low 13 bits set to zero"
    return x & 0xFFFFE000


class AmxTf32(R.Amx):
    """ref_amx.Amx (TILECFG / TILEDATA / AMX-E4) plus TMMULTF32PS"""

    def __init__(self, reading="B", **kw):
        kw.setdefault("mask", R.AMX_ALL | AMX_TF32)
        R.Amx.__init__(self, **kw)
        assert reading in ("A", "B")
        self.daz = reading == "B"

    def TMMULTF32PS(self, d, s1, s2):
        if not self.mask & AMX_TF32:
            raise R.Fault(UD)
        self.check_e4(d, s1, s2)
        T = self.tiles
        # C = m x n (tsrcdest), A = m x k (tsrc1), B = k x n (tsrc2); elements are fp32
        elements_src1 = self.colsb[s1] // 4
        elements_dest = self.colsb[d] // 4
        for m in range(self.rows[d]):
            temp1 = [0] * elements_dest
            for k in range(elements_src1):
                for n in range(elements_dest):
                    a = silence_snan_fp32(R.u32(T[s1], 64 * m + 4 * k))
                    b = silence_snan_fp32(R.u32(T[s2], 64 * k + 4 * n))
                    temp1[n] = fma32(temp1[n], zero_lower_mantissa_bits_fp32(a),
                                     zero_lower_mantissa_bits_fp32(b), self.daz)
            row = bytearray(T[d][64 * m: 64 * m + 64])
            for n in range(elements_dest):
                R.put32(row, 4 * n, add32(R.u32(row, 4 * n), temp1[n], self.daz))
            self.write_row_and_zero(d, m, row, self.colsb[d])
        self.zero_upper_rows(d, self.rows[d])
        self.zero_tilecfg_start()


# --------------------------------------------------------------------------
# encoding: VEX.128.pp.0F38.W0 48 11:rrr:bbb (pp 66 = TMMULTF32PS)
# --------------------------------------------------------------------------
def enc_tf32(d, s1, s2, pp=1, L=0, W=0, modrm=None):
    out = R.vex3(pp, (d >> 3) & 1, 0, (s1 >> 3) & 1, W, s2, L)
    return out + [0x48, modrm if modrm is not None else (0xC0 | ((d & 7) << 3) | (s1 & 7))]


# --------------------------------------------------------------------------
# self test
# --------------------------------------------------------------------------
def run1(av, bv, cv, shape, reading="B"):
    """one TMMULTF32PS tmm0, tmm1, tmm2 on M x K x N element lists (row-major) -> tmm0 words"""
    M, K, N = shape
    a = AmxTf32(reading=reading)
    a.set_cfg((1, 0, [4 * N, 4 * K, 4 * N, 0, 0, 0, 0, 0], [M, M, K, 0, 0, 0, 0, 0]))
    for i, v in enumerate(av):
        R.put32(a.tiles[1], 64 * (i // K) + 4 * (i % K), v)
    for i, v in enumerate(bv):
        R.put32(a.tiles[2], 64 * (i // N) + 4 * (i % N), v)
    for i, v in enumerate(cv):
        R.put32(a.tiles[0], 64 * (i // N) + 4 * (i % N), v)
    a.TMMULTF32PS(0, 1, 2)
    return [R.u32(a.tiles[0], 64 * (i // N) + 4 * (i % N)) for i in range(M * N)], a


def selftest():
    fails = []

    def check(name, got, exp):
        if got != exp:
            fails.append("%s: got %s, expected %s" % (name, got, exp))

    one, half, two = 0x3F800000, 0x3F000000, 0x40000000
    # TF32: 13 low fraction bits of both sources dropped (1 + 2^-10 kept, 1 + (2^-10 - 2^-23) -> 1)
    check("tf32 truncation", run1([0x3F801FFF], [0x3F802000], [0], (1, 1, 1))[0], [0x3F802000])
    # -(1 + 2^-10)^2 = -(1 + 2^-9 + 2^-20), exact
    check("tf32 truncation neg", run1([0xBF803FFF], [0x3F803FFF], [0], (1, 1, 1))[0],
          [0xBF804008])
    # 2x2x2 small integers: C + A.B
    check("2x2x2", run1([one, two, 0x40400000, 0x40800000],
                        [0x40A00000, 0x40C00000, 0x40E00000, 0x41000000],
                        [half, 0, 0, 0xBF800000], (2, 2, 2))[0],
          [0x419C0000, 0x41B00000, 0x422C0000, 0x42440000])
    # input denormal: reading B (DAZ) 1.0; reading A 1.0 + 2^-127 * 2^126 = 1.5
    check("daz input B", run1([0x00400000], [0x7E800000], [one], (1, 1, 1))[0], [one])
    check("daz input A", run1([0x00400000], [0x7E800000], [one], (1, 1, 1), "A")[0], [0x3FC00000])
    # tsrcdest denormal: B 0 + 2^-126; A 2^-127 + 2^-126 = 1.5 * 2^-126
    check("daz acc B", run1([0x00800000], [one], [0x00400000], (1, 1, 1))[0], [0x00800000])
    check("daz acc A", run1([0x00800000], [one], [0x00400000], (1, 1, 1), "A")[0], [0x00C00000])
    # FTZ: 2^-126 * 0.5 = 2^-127 -> +0; -2^-126 * 0.5 -> -0, then +0 + -0 = +0
    check("ftz", run1([0x00800000], [half], [0], (1, 1, 1))[0], [0])
    check("ftz neg", run1([0x80800000], [half], [0], (1, 1, 1))[0], [0])
    check("ftz neg acc -0", run1([0x80800000], [half], [0x80000000], (1, 1, 1))[0], [0x80000000])
    # fused: 2^-125 + 2^-126 * -0.75 = 1.25 * 2^-126 (the product alone would be tiny)
    check("fused", run1([0x01000000, 0x00800000], [one, 0xBF400000], [0], (1, 2, 1))[0],
          [0x00A00000])
    # SNaN silenced before the truncation: 7F800001h -> 7FC00001h -> 7FC00000h (not +inf)
    check("snan low payload", run1([0x7F800001], [one], [0], (1, 1, 1))[0], [0x7FC00000])
    check("snan low payload neg", run1([one], [0xFF800FFF], [0], (1, 1, 1))[0], [0xFFC00000])
    check("qnan payload truncated", run1([0x7FC12345], [one], [0], (1, 1, 1))[0], [0x7FC12000])
    check("nan order x first", run1([0x7FC02000], [0xFFC04000], [0x7FC08000], (1, 1, 1))[0],
          [0x7FC08000])         # the add returns the tsrcdest NaN first
    check("nan order fma", run1([0x7FC02000], [0xFFC04000], [0], (1, 1, 1))[0], [0x7FC02000])
    check("inf * 0", run1([0x7F800000], [0], [one], (1, 1, 1))[0], [QNAN_INDEFINITE])
    check("inf - inf", run1([0x7F800000, 0xFF800000], [one, one], [0], (1, 2, 1))[0],
          [QNAN_INDEFINITE])
    check("inf + acc", run1([0x7F800000], [two], [0xC2C80000], (1, 1, 1))[0], [0x7F800000])
    check("acc inf - inf", run1([0x7F800000], [two], [0xFF800000], (1, 1, 1))[0],
          [QNAN_INDEFINITE])
    check("overflow", run1([0x7F7FE000, 0x7F7FE000], [0x40000000, 0x40000000], [0], (1, 2, 1))[0],
          [0x7F800000])
    check("cancel +0", run1([one, one], [one, 0xBF800000], [0x80000000], (1, 2, 1))[0], [0])
    # rounding: 1 + 2^-24 (tie, even -> 1); (1 + 2^-23) + 2^-24 (tie, odd -> 1 + 2^-22);
    # 1 + 2^-10 + 1.5 * 2^-24 (above the half -> 1 + 2^-10 + 2^-23); the final add rounds too
    check("tie even", run1([one, 0x33800000], [one, one], [0], (1, 2, 1))[0], [one])
    check("tie odd", run1([one, 0x34000000, 0x33800000], [one, one, one], [0], (1, 3, 1))[0],
          [0x3F800002])
    check("above half", run1([0x3F802000, 0x33C00000], [one, one], [0], (1, 2, 1))[0],
          [0x3F802001])
    check("final add tie", run1([0x33800000], [one], [0x3F800001], (1, 1, 1))[0], [0x3F800002])
    # U596 FTZ: 2^-126 - 2^-150 is tiny with an unbounded exponent (exact in 24 bits) -> +0
    check("ftz unbounded", run1([0x01000000, 0x00800000], [half, 0xB3800000], [0], (1, 2, 1))[0],
          [0])
    # write_row_and_zero / zero_upper_rows: bytes beyond colsb and rows beyond M are zeroed
    a = AmxTf32()
    a.set_cfg((1, 0, [8, 4, 8, 0, 0, 0, 0, 0], [2, 2, 1, 0, 0, 0, 0, 0]))
    a.tiles[0][:] = bytes([0xAB]) * 1024
    R.put32(a.tiles[1], 0, one)
    R.put32(a.tiles[1], 64, two)
    R.put32(a.tiles[2], 0, half)
    R.put32(a.tiles[2], 4, two)
    a.start_row = 1
    a.TMMULTF32PS(0, 1, 2)
    exp = bytearray(1024)
    abab = 0xABABABAB
    R.put32(exp, 0, add32(abab, half, True))
    R.put32(exp, 4, add32(abab, two, True))
    R.put32(exp, 64, add32(abab, one, True))
    R.put32(exp, 68, add32(abab, 0x40800000, True))
    check("write_row_and_zero", bytes(a.tiles[0]), bytes(exp))
    check("start_row cleared", a.start_row, 0)
    # faults: opt-in, AMX-E4
    for (nm, fn, vec) in [
            ("no opt-in", lambda: AmxTf32(mask=R.AMX_ALL).TMMULTF32PS(0, 1, 2), UD),
            ("same tiles", lambda: AmxTf32().TMMULTF32PS(1, 1, 2), UD),
            ("unconfigured", lambda: AmxTf32().TMMULTF32PS(0, 1, 2), UD)]:
        try:
            fn()
            fails.append("%s: no fault" % nm)
        except R.Fault as f:
            check(nm, f.vector, vec)
    x = AmxTf32(xfd=1 << 18)
    x.set_cfg((1, 0, [4, 4, 4, 0, 0, 0, 0, 0], [1, 1, 1, 0, 0, 0, 0, 0]))
    try:
        x.TMMULTF32PS(0, 1, 2)
        fails.append("xfd: no fault")
    except R.Fault as f:
        check("xfd #NM", f.vector, NM)
    # the own binary32 code against ref_amx.py (DAZ = FTZ = 1) on every class pair
    st = 0x7F32AB00
    n = 0
    for cx in R.CLASSES:
        for cy in R.CLASSES:
            for _ in range(60):
                st, r1 = R.splitmix64(st)
                st, r2 = R.splitmix64(st)
                st, r3 = R.splitmix64(st)
                x_ = R.gen_elem(R.F32, cx, r1)
                y_ = R.gen_elem(R.F32, cy, r2)
                c_ = R.gen_elem(R.F32, cx, r3)
                n += 1
                if fma32(c_, x_, y_, True) != R.fma32(c_, x_, y_):
                    fails.append("fma32 vs ref_amx: %08X %08X %08X" % (c_, x_, y_))
                if add32(x_, y_, True) != R.add32(x_, y_):
                    fails.append("add32 vs ref_amx: %08X %08X" % (x_, y_))
    # to_f32 against ref_amx.round_f32 without FTZ (gradual underflow)
    for i in range(2000):
        st, r = R.splitmix64(st)
        v = Fraction((r & 0xFFFFFFFFFF) | 1, 1 << ((r >> 40) % 300))
        if r >> 63:
            v = -v
        if to_f32(v, False) != R.round_f32(v):
            fails.append("to_f32 vs ref_amx.round_f32: %s" % v)
        if to_f32(v, True) != R.round_f32_ftz(v):
            fails.append("to_f32 ftz vs ref_amx.round_f32_ftz: %s" % v)
    for f_ in fails:
        print("FAIL", f_)
    print("ref_amx_tf32 selftest: %d checks failed (%d binary32 cross-checks)" % (len(fails), n))
    return 0 if not fails else 1


# --------------------------------------------------------------------------
# Emulator/data/cases_amx_tf32.txt
# --------------------------------------------------------------------------
def emit_cases(reading="B"):
    w = sys.stdout.write
    w("# AMX-TF32 TMMULTF32PS expected-value cases (ledger U1150). Generated by\n"
      "# Emulator/tools/isa/ref_amx_tf32.py --cases (independent model of ISE 319433-060 3.7,\n"
      "# pp. 3-32/3-33; the instruction was withdrawn in ISE -062); regenerate, do not edit.\n"
      "# UNVERIFIED: the ISE pseudocode is self-contradictory on input denormals; these values\n"
      "# follow the Operation (FP32 FMA / add with DAZ = FTZ = 1, reading B, docs/quirks.md).\n"
      "# Cases named *_dazB_* change under the rejected reading A. No CPU and no Intel SDE\n"
      "# (10.13.1 has no AMX_TF32 ISA set) runs TMMULTF32PS: expected-value cases only, with\n"
      "#   emu-alltest --cases Emulator\\data\\cases_amx_tf32.txt --amx --amx-tf32 --expect-only\n"
      "# (RSI = MEM+8000h: TILECFG image, tsrc1 at +100h, tsrc2 at +500h; RDI = MEM+9000h: tsrcdest).\n")
    if reading != "B":
        w("# !!! reading %s: documentation only, NOT the emulator's behaviour !!!\n" % reading)
    MEM = 0x10000000
    cases = []

    def case(name, code, ins, exp, loose=False):
        cases.append("# %s\n.byte %s | %s =>%s %s\n" % (name, R.bstr(code), " ".join(ins),
                                                         "!" if loose else "", exp))

    def program(d=0, s1=1, s2=2):
        return (R.enc("LDTILECFG", base="rsi") +
                R.enc("TILELOADD", reg=s1, base="rsi", index="rcx", disp=0x100) +
                R.enc("TILELOADD", reg=s2, base="rsi", index="rcx", disp=0x500) +
                R.enc("TILELOADD", reg=d, base="rdi", index="rcx") +
                enc_tf32(d, s1, s2) +
                R.enc("TILESTORED", reg=d, base="rdi", index="rcx") + R.enc("TILERELEASE"))

    def matmul_case(name, M, K, N, stride, A, B, C, regs=(0, 1, 2)):
        """A, B, C: 1024-byte tile images (row r at 64*r); memory rows at the stride"""
        d, s1, s2 = regs
        cfg = R.cfg_bytes({d: (M, 4 * N), s1: (M, 4 * K), s2: (K, 4 * N)})
        mem = {}

        def put(off, data):
            for i, b_ in enumerate(data):
                mem[off + i] = b_
        put(0x8000, cfg)
        for (off, t, rows, colsb) in [(0x8100, A, M, 4 * K), (0x8500, B, K, 4 * N),
                                      (0x9000, C, M, 4 * N)]:
            for r in range(rows):
                put(off + r * stride, t[64 * r: 64 * r + colsb])
        a = AmxTf32(reading=reading)
        for k_, v_ in mem.items():
            a.mem[MEM + k_] = v_
        rsi, rdi = MEM + 0x8000, MEM + 0x9000
        a.LDTILECFG(rsi)
        a.TILELOADD(s1, rsi + 0x100, stride)
        a.TILELOADD(s2, rsi + 0x500, stride)
        a.TILELOADD(d, rdi, stride)
        a.TMMULTF32PS(d, s1, s2)
        a.TILESTORED(rdi, stride, d)
        a.TILERELEASE()
        spans = [(0x8000, 0x8040), (0x8100, 0x8100 + (M - 1) * stride + 4 * K),
                 (0x8500, 0x8500 + (K - 1) * stride + 4 * N),
                 (0x9000, 0x9000 + (M - 1) * stride + 4 * N)]
        ins = ["rcx=%d" % stride] + ["m+0x%X=%s" % (lo, R.hexs(bytes(mem.get(i, 0)
                                                                     for i in range(lo, hi))))
                                     for lo, hi in spans]
        lo, hi = spans[3]
        res = bytes(a.mem.get(MEM + i, 0) for i in range(lo, hi))
        case(name, program(d, s1, s2), ins, "m+0x%X=%s" % (lo, R.hexs(res)))

    def words_tile(words):
        t = bytearray(1024)
        for i, v in enumerate(words):
            R.put32(t, 4 * i, v)
        return t

    def has_denormal_effect(A, B, C, M, K, N):
        """does reading A give another tsrcdest than reading B (the case is reading-dependent)?"""
        res = []
        for rd in ("A", "B"):
            a = AmxTf32(reading=rd)
            a.set_cfg((1, 0, [4 * N, 4 * K, 4 * N, 0, 0, 0, 0, 0], [M, M, K, 0, 0, 0, 0, 0]))
            a.tiles[0][:], a.tiles[1][:], a.tiles[2][:] = C, A, B
            a.TMMULTF32PS(0, 1, 2)
            res.append(bytes(a.tiles[0]))
        return res[0] != res[1]

    # ---- random tiles: (source class, accumulator class) x shapes ----
    seed = 0x7F320000
    for (cls, acc) in [("NORMAL", "NORMAL"), ("INT", "INT"), ("WIDE", "WIDE"), ("TINY", "TINY"),
                       ("HUGE", "NORMAL"), ("SPECIAL", "SPECIAL"), ("TINY", "NORMAL"),
                       ("NORMAL", "TINY")]:
        for (M, K, N, stride) in [(4, 4, 4, 64), (3, 2, 5, 24), (16, 16, 16, 64), (1, 1, 1, 4),
                                  (7, 13, 9, 52)]:
            seed += 1
            A = bytearray(R.f32_words(seed, 256, cls))
            B = bytearray(R.f32_words(seed ^ 0x5555, 256, cls))
            C = bytearray(R.f32_words(seed ^ 0xAAAA, 256, acc))
            tag = "dazB_" if has_denormal_effect(A, B, C, M, K, N) else ""
            matmul_case("tmmultf32ps_%s_%s_%s%dx%dx%d_stride%d" % (cls.lower(), acc.lower(), tag,
                                                                  M, K, N, stride),
                        M, K, N, stride, A, B, C)
    # other tile numbers (ModRM.reg / r/m / VEX.vvvv decoding)
    for regs in [(7, 3, 5), (2, 6, 0), (5, 0, 7)]:
        seed += 1
        A = bytearray(R.f32_words(seed, 256, "NORMAL"))
        B = bytearray(R.f32_words(seed ^ 0x5555, 256, "NORMAL"))
        C = bytearray(R.f32_words(seed ^ 0xAAAA, 256, "NORMAL"))
        matmul_case("tmmultf32ps_tmm%d_tmm%d_tmm%d_4x3x2" % regs, 4, 3, 2, 64, A, B, C, regs)
    # ---- hand-picked specials (A row-major M x K, B K x N, C M x N) ----
    one, half, two = 0x3F800000, 0x3F000000, 0x40000000
    specials = [
        ("tf32_truncation", (1, 1, 1), [0x3F801FFF], [0x3F802000], [0]),
        ("tf32_truncation_neg", (1, 1, 1), [0xBF803FFF], [0x3F803FFF], [0]),
        ("small_ints_2x2x2", (2, 2, 2), [one, two, 0x40400000, 0x40800000],
         [0x40A00000, 0x40C00000, 0x40E00000, 0x41000000], [half, 0, 0, 0xBF800000]),
        ("src_denormal", (1, 1, 1), [0x00400000], [0x7E800000], [one]),
        ("src2_denormal_neg", (1, 1, 1), [0x7E800000], [0x80600000], [one]),
        ("acc_denormal", (1, 1, 1), [0x00800000], [one], [0x00400000]),
        ("acc_denormal_only", (1, 1, 1), [0], [0], [0x80000001]),
        ("ftz_product", (1, 1, 1), [0x00800000], [half], [0]),
        ("ftz_product_neg", (1, 1, 1), [0x80800000], [half], [0]),
        ("ftz_product_neg_acc_minus0", (1, 1, 1), [0x80800000], [half], [0x80000000]),
        ("ftz_tiny_unbounded", (1, 2, 1), [0x01000000, 0x00800000], [half, 0xB3800000], [0]),
        ("fused_tiny_product", (1, 2, 1), [0x01000000, 0x00800000], [one, 0xBF400000], [0]),
        ("snan_low_payload", (1, 1, 1), [0x7F800001], [one], [0]),
        ("snan_low_payload_neg_src2", (1, 1, 1), [one], [0xFF800FFF], [0]),
        ("qnan_payload_truncated", (1, 1, 1), [0x7FC12345], [one], [0]),
        ("nan_acc_first", (1, 1, 1), [0x7FC02000], [0xFFC04000], [0x7FC08000]),
        ("nan_src1_first", (1, 1, 1), [0x7FC02000], [0xFFC04000], [0]),
        ("snan_acc", (1, 1, 1), [one], [one], [0x7F800001]),
        ("inf_times_zero", (1, 1, 1), [0x7F800000], [0], [one]),
        ("inf_times_denormal", (1, 1, 1), [0x7F800000], [0x00002000], [one]),
        ("inf_minus_inf", (1, 2, 1), [0x7F800000, 0xFF800000], [one, one], [0]),
        ("inf_plus_acc", (1, 1, 1), [0x7F800000], [two], [0xC2C80000]),
        ("inf_plus_acc_neg_inf", (1, 1, 1), [0x7F800000], [two], [0xFF800000]),
        ("overflow", (1, 2, 1), [0x7F7FE000, 0x7F7FE000], [two, two], [0]),
        ("cancel_plus_zero", (1, 2, 1), [one, one], [one, 0xBF800000], [0x80000000]),
        ("minus_zero_sum", (1, 1, 1), [0x80000000], [one], [0x80000000]),
        ("round_tie_even", (1, 2, 1), [one, 0x33800000], [one, one], [0]),
        ("round_tie_odd", (1, 3, 1), [one, 0x34000000, 0x33800000], [one, one, one], [0]),
        ("round_above_half", (1, 2, 1), [0x3F802000, 0x33C00000], [one, one], [0]),
        ("round_final_add", (1, 1, 1), [0x33800000], [one], [0x3F800001]),
    ]
    for (nm, (M, K, N), av, bv, cv) in specials:
        A, B, C = words_tile([]), words_tile([]), words_tile([])
        for i, v in enumerate(av):
            R.put32(A, 64 * (i // K) + 4 * (i % K), v)
        for i, v in enumerate(bv):
            R.put32(B, 64 * (i // N) + 4 * (i % N), v)
        for i, v in enumerate(cv):
            R.put32(C, 64 * (i // N) + 4 * (i % N), v)
        tag = "_dazB" if has_denormal_effect(A, B, C, M, K, N) else ""
        matmul_case("tmmultf32ps_special_%s%s" % (nm, tag), M, K, N, 4 * max(K, N), A, B, C)
    # ---- faults (AMX-E4 and the encoding) ----
    ins_ok = R.enc("LDTILECFG", base="rsi")
    cfgok = R.cfg_bytes({0: (2, 8), 1: (2, 4), 2: (1, 8)})
    good = enc_tf32(0, 1, 2)
    case("tmmultf32ps_unconfigured_ud", good, [], "#UD")
    case("tmmultf32ps_vexL1_ud", enc_tf32(0, 1, 2, L=1), [], "#UD")
    case("tmmultf32ps_vexW1_ud", enc_tf32(0, 1, 2, W=1), [], "#UD")
    case("tmmultf32ps_mem_ud", good[:4] + [0x06], [], "#UD")
    for (pp, nm) in [(0, "np_ttmmultf32ps"), (2, "f3"), (3, "f2")]:
        case("tmmultf32ps_%s_ud" % nm, ins_ok + enc_tf32(0, 1, 2, pp=pp),
             ["m+0x8000=" + R.hexs(cfgok)], "#UD", loose=True)
    case("tmmultf32ps_configured_ok", ins_ok + good + R.enc("TILERELEASE"),
         ["m+0x8000=" + R.hexs(cfgok)], "m+0x8000=" + R.hexs(cfgok))
    for (nm, regs) in [("dest_eq_src1", (1, 1, 2)), ("src1_eq_src2", (0, 2, 2)),
                       ("dest_eq_src2", (2, 1, 2))]:
        case("tmmultf32ps_%s_ud" % nm, ins_ok + enc_tf32(*regs),
             ["m+0x8000=" + R.hexs(cfgok)], "#UD", loose=True)
    for (nm, shapes) in [("rows_mismatch", {0: (2, 8), 1: (3, 4), 2: (1, 8)}),
                         ("colsb_dest_ne_src2", {0: (2, 8), 1: (2, 4), 2: (1, 12)}),
                         ("k_mismatch", {0: (2, 8), 1: (2, 8), 2: (1, 8)}),
                         ("colsb_not_mult4", {0: (2, 8), 1: (2, 6), 2: (1, 8)}),
                         ("src2_invalid", {0: (2, 8), 1: (2, 4)})]:
        case("tmmultf32ps_%s_ud" % nm, ins_ok + good,
             ["m+0x8000=" + R.hexs(R.cfg_bytes(shapes))], "#UD", loose=True)
    case("tmmultf32ps_tmm8_ud", ins_ok + enc_tf32(8, 1, 2),
         ["m+0x8000=" + R.hexs(cfgok)], "#UD", loose=True)
    wr_xfd = [0xB9, 0xC4, 0x01, 0, 0, 0xB8, 0, 0, 4, 0, 0xBA, 0, 0, 0, 0, 0x0F, 0x30]
    case("tmmultf32ps_xfd_nm", ins_ok + wr_xfd + good, ["m+0x8000=" + R.hexs(cfgok)], "#NM",
         loose=True)
    # CR0.TS is not an AMX-E4 condition: set it around the TMMULTF32PS only (the harness's own
    # state save after the snippet would #NM with TS = 1)
    mov_cr0_ts = [0x0F, 0x20, 0xC0, 0x48, 0x83, 0xC8, 0x08, 0x0F, 0x22, 0xC0]  # mov rax,cr0; or rax,8; mov cr0,rax
    clr_cr0_ts = [0x48, 0x83, 0xE0, 0xF7, 0x0F, 0x22, 0xC0]                   # and rax,-9; mov cr0,rax
    case("tmmultf32ps_cr0_ts_no_nm", ins_ok + mov_cr0_ts + good + clr_cr0_ts + R.enc("TILERELEASE"),
         ["m+0x8000=" + R.hexs(cfgok)], "m+0x8000=" + R.hexs(cfgok), loose=True)   # RAX = CR0
    for s in cases:
        w(s)


def main():
    if "--selftest" in sys.argv:
        return selftest()
    if "--cases" in sys.argv:
        reading = "B"
        if "--reading" in sys.argv:
            reading = sys.argv[sys.argv.index("--reading") + 1]
        emit_cases(reading)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
