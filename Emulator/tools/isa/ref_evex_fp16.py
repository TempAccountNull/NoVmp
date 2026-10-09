#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_fp16.py -- independent reference model (Python 3 stdlib only) of the Intel AVX512-FP16
instructions and generator of the expected-value case file Emulator/data/cases_evex_fp16.txt.

Written from the Intel SDM text only (325383-092: Vol2C chapter 5 FP16 pages, Vol2A 2.7/2.8
EVEX encoding / Tables 2-36..2-62, Vol1 4.8/4.9/11.5 and 14.5.2), never from the C code of the
emulator. The EVEX encoder, the case-line format, the exact-rational helpers (Fmt, classify,
round_frac, floor_log2, isqrt) and the harness layout come from ref_evex_m1.py (same directory).

Forms (all 116 AVX512_FP16 rows of Emulator/data/evex_forms.tsv, checked by --selftest):
  VADD/SUB/MUL/DIV/SQRT/MIN/MAX PH/SH, VRCP/VRSQRT PH/SH, VSCALEF PH/SH, VGETEXP PH/SH,
  VGETMANT PH/SH, VREDUCE PH/SH, VRNDSCALE PH/SH, VFPCLASS PH/SH, VCMP PH/SH, VCOMISH, VUCOMISH,
  VF[N]MADD/VF[N]MSUB 132/213/231 PH/SH, VFMADDSUB/VFMSUBADD 132/213/231 PH, VF[C]MADDC PH/SH,
  VF[C]MULC PH/SH, VMOVSH (4 forms), VMOVW (2 forms), VCVTPH2PSX/PD/DQ/UDQ/QQ/UQQ/W/UW,
  VCVTTPH2DQ/UDQ/QQ/UQQ/W/UW, VCVTPS2PHX, VCVTPD2PH, VCVT(U)DQ2PH, VCVT(U)QQ2PH, VCVT(U)W2PH,
  VCVTSH2SS/SD, VCVTSS2SH, VCVTSD2SH, VCVTSH2SI/USI, VCVTTSH2SI/USI, VCVT(U)SI2SH.

Floating point (exact rationals, binary16 = p 11, 5 exponent bits, bias 15, emin -14):
  * round to the destination with the four rounding modes; RC = EVEX.RC ({er}: reg-reg 512-bit
    or scalar forms, L'L = RC) else MXCSR.RC; {er}/{sae}: no MXCSR flag, no #XM.
  * flags IE DE ZE OE UE PE. Tininess is detected after rounding with an unbounded exponent
    (Vol1 4.9.1.5, Table 4-12 half precision |x| < 2^-14). Underflow masked: UE only if tiny AND
    inexact; UNMASKED (MXCSR.UM = 0, no SAE): UE if tiny regardless of inexactness (Vol1
    4.9.1.5, 11.5.2.5) -> #XM.
  * learned from the i5-13600K (F16C VCVTPS2PH, see --hwgen), consistent with Vol1 4.9.1.5/4.9.1.6
    (the unmasked response refers to the result rounded with an unbounded exponent): with UM
    (OM) unmasked, a tiny (overflowing) result sets PE only when the rounding with UNBOUNDED
    exponent is inexact (3*2^-26 -> UE without PE; 65536 -> OE without PE); masked responses
    always use the delivered result (masked overflow: OE and PE).
  * a denormal FP32/FP64 source of a conversion to FP16 (DAZ = 0) sets DE, UE and PE (VCVTPS2PH
    page: "with DM masked and at least one of PM or UM unmasked; a SIMD exception will be raised
    with DE, UE and PE set"; the hardware does so also when the unbounded rounding is exact,
    e.g. 2^-149); the same rule is used for VCVTPS2PHX/VCVTSS2SH/VCVTPD2PH/VCVTSD2SH.
  * any unmasked exception of an active (mask-enabled) lane -> #XM, the destination is
    unchanged, the MXCSR flags of the whole instruction are still set, except that an unmasked
    IE/DE/ZE (pre-computation) drops OE/UE/PE (same convention as ref_evex_m1.py). Masked-off
    lanes raise nothing.
  * FP16 operands: denormal inputs are always used as such (MXCSR.DAZ never applies, VCVTPH2PS
    page: "AVX512_FP16 ... always handle FP16 denormal number inputs"), FP16 results are never
    flushed (MXCSR.FTZ never applies: VCVTPS2PHX/VCVTPD2PH "FP16 outputs ... are not
    conditionally flushed"). DE is set for a denormal FP16 input only when the page lists
    "Denormal" among its SIMD FP exceptions.
  * FP32/FP64 SOURCE operands (VCVTPS2PHX, VCVTPD2PH, VCVTSS2SH, VCVTSD2SH): MXCSR.DAZ applies
    (VCVTPS2PHX: "uses MXCSR.DAZ for handling FP32 inputs", VCVTPD2PH: "... FP64 inputs";
    CHOICE: the VCVTSS2SH/VCVTSD2SH pages are silent, the standard SSE DAZ rule (Vol1
    10.2.3.4) is used for them as well). With DAZ = 0 a denormal source sets DE.
  * exception priority (Vol1 4.9.2): a NaN operand (SNaN -> IE, QNaN) and invalid / divide by
    zero suppress DE (denormal / 0 sets ZE only; sqrt of a negative denormal sets IE only).
  * NaN results (Vol1 Table 4-7): SRC1 NaN -> SRC1 quietened, else SRC2 quietened; invalid ->
    FP16 QNaN indefinite FE00h. FMA (Vol1 14.5.2, Table 14-17): x = multiplicand, y =
    multiplier, z = addend; Q(x) before Q(y) before Q(z), the result NaN is never negated;
    0 * INF + QNaN gives Q(z) with IE only if an operand is an SNaN; 132: x = DEST, y = SRC3,
    z = SRC2; 213: x = SRC2, y = DEST, z = SRC3; 231: x = SRC2, y = SRC3, z = DEST (Table 5-5).
    Exact-zero FMA results follow Table 14-16 (equal-sign zeros keep the sign, otherwise +0,
    -0 in round down).
  * NaN conversions: FP16 -> FP32/FP64 keeps the payload (shifted to the top of the wider
    fraction) and sets the quiet bit; FP32/FP64 -> FP16 truncates the payload to its upper 10
    bits and sets the quiet bit; SNaN -> IE.

Rules chosen where the SDM is silent or contradicts itself (page references are Vol2C):
  1. VCVTPH2PSX (5-54..5-57): the shared description says "No denormal exception is reported"
     but the exception list says "EVEX-encoded instructions with broadcast (VCVTPH2PSX): Invalid,
     Denormal" -> DE IS reported for an FP16 denormal input of VCVTPH2PSX (the page's own
     exception list wins). F16C VCVTPH2PS (hardware cross-check) reports no DE.
  2. VF[C]MADDCPH / VF[C]MULCPH / ...CSH (5-180..5-190): the opcode table descriptions call
     the F3 (non-C) forms "complex conjugate" but the Operation pseudocode makes the F2 'C'
     forms the conjugate ones (dest0 = tmp0 + a1*b1, dest1 = tmp1 - a0*b1). The pseudocode is
     modelled. Every pseudocode statement is one fused operation rounded to FP16 (tmp is an FP16
     value): tmp0 = fma(a0, b0, acc0), tmp1 = fma(a1, b0, acc1) (VFMULC: plain products), then
     dest0 = fma(-+a1, b1, tmp0), dest1 = fma(+-a0, b1, tmp1). NaN priority per Table 14-17 with
     x = SRC1 element, y = SRC2 element, z = accumulator. "Execution occurs as if all MXCSR
     exceptions are masked": never #XM, masked underflow semantics, flags = OR of the flags of
     every fused step of the active pairs ({er}: none). #UD if DEST = SRC1 or DEST = SRC2 (reg).
     The scalar forms copy DEST[127:32] from SRC1 (not from the accumulator).
  3. Scalar FMA (VF*SH): DEST[127:16] remains unchanged (from DEST, page 5-260..); all other
     scalar FP16 forms copy the upper bits from SRC1.
  4. VGETEXPPH/SH (5-376): NaN input -> QNaN(src1) with its sign (Table 5-14; the pseudocode
     clears the sign before 'return qnan(src)', but the VGETEXPPS pseudocode returns QNAN(SRC)
     of the unmodified source). -INF -> +INF, +-0 -> -INF, DE for a denormal.
  5. VGETMANT (5-393): imm8[7:4] "must be zero" -> cases only use imm8 < 16. Negative input with
     SC[1] = 1 returns QNaN indefinite with IE before any denormal handling (no DE).
  6. VREDUCE (5-688): SNaN -> QNaN with IE (Invalid is listed; the pseudocode only quietens).
     dest = Round(src - 2^-M * ROUND(2^M * src)) with the same RC (the subtraction is exact except
     when |src| < 2^-M and ROUND gives +-1: Table 5-28 "Round(Src1 -+ 2^-M)"). PE iff the ROUND
     step is inexact and imm8[3] (SPE) = 0 (an inexact subtraction implies an inexact ROUND);
     never UE ("does not report underflow"), no DE; a zero result is +0 (-0 when the rounding
     used is RD); +-INF -> +0; RC = imm8[2] ? MXCSR.RC : imm8[1:0].
  7. VRNDSCALE (5-702): result = 2^-M * round_int(2^M * src) (exactly representable), sign of a
     zero result = sign of src, SNaN -> QNaN with IE, no DE. PE iff src != dst and SPE = 0. UE
     iff the result is tiny (non-zero, |r| < 2^-14) AND src != dst, whatever MXCSR.UM ("can set
     MXCSR.UE without MXCSR.PE" with SPE; the 2^-M scaling itself is exact). Exact tiny results
     are not used in unmasked-UM cases.
  8. VSCALEF (5-728): Table 5-39 is applied literally, including Src1 = QNaN with Src2 = +INF
     -> +INF and Src2 = -INF -> +0. DE only for a denormal Src1 and only when Src2 is not a NaN
     ("Denormal is not reported for Src2"; the same rule is used for VSCALEFSH whose list just
     says Denormal). floor() of a denormal Src2 is 0 / -1. Overflow / underflow responses of
     Table 5-40 are the normal rounding responses.
  9. VRCPPH/SH, VRSQRTPH/SH (agreed convention): exact 1/x (1/sqrt(x)) rounded to nearest-even
     binary16 regardless of MXCSR.RC, the special tables honoured (rcp: +-0 -> +-INF, +-INF ->
     +-0; rsqrt: x < 0 incl. -INF and negative denormals -> QNaN indefinite, -0 -> -INF, +0 ->
     +INF, +INF -> +0), NaN input -> the quietened NaN (not in the tables), NO MXCSR flags.
     --selftest checks the 2^-11 + 2^-14 bound for every normal result and the tables.
 10. VMIN/VMAX PH/SH (5-419): IE for an SNaN or QNaN operand (as legacy MINPS/MAXPS), the
     result is SRC2 unchanged (an SNaN SRC2 is not quietened); both zeros -> SRC2; DE only if
     no operand is a NaN.
 11. VCMPPH/SH: Vol2A Table 3-8 semantics for the 32 predicates (signalling predicates set IE
     for a QNaN operand, every predicate for an SNaN); DE only if no operand is a NaN.
     VCOMISH: IE for any NaN, VUCOMISH: IE for an SNaN only; ZF PF CF from the result, OF SF AF
     cleared; EFLAGS unchanged on #XM.
 12. Integer conversions from FP16 list "Invalid, Precision" only: no DE. Out of range (also a
     negative value that rounds below 0 for the unsigned forms), NaN or INF -> integer
     indefinite (80..0h signed, FF..Fh unsigned) with IE. The VCVT[T]SH2USI page text is a copy
     of the signed one ("range limits of signed ..."): the unsigned range is used.
 13. Scalar forms are LIG: cases use L'L = 0, 1, 2 without EVEX.b; L'L = 11b without EVEX.b on
     a scalar form is not tested (Table 2-38 reserves 11b only as a vector length).
     {sae} on reg-reg packed forms: L'L ignored, VL = 512 (Table 2-38/2-43); {er}: L'L = RC,
     VL = 512 (the M1 convention).
 14. VMOVW is EVEX.128 only (E9NF "instruction specific L'L restriction"): L'L != 00b -> #UD.
 15. Whole-instruction flags (M1 convention): flags are ORed over the active lanes, then an
     unmasked IE/DE/ZE anywhere drops OE/UE/PE.

Usage:
  python ref_evex_fp16.py --selftest                 hand-derived checks, exit 0 on pass
  python ref_evex_fp16.py --cases [--only M1,M2] [--out FILE]
                                                     Emulator/data/cases_evex_fp16.txt (stdout, or
                                                     FILE with CRLF; --out avoids PowerShell's
                                                     UTF-16 '>' redirection)
  python ref_evex_fp16.py --stats                    number of cases per mnemonic
  python ref_evex_fp16.py --hwgen OUT.json > HW.txt  F16C hardware cases (no "=>") + expectations
  python ref_evex_fp16.py --hwcmp LOG OUT.json       compare the emu-alltest log with the model

Hardware cross-check (the i5-13600K has F16C only): --hwgen writes emu-alltest HARDWARE cases
(no "=>") of VEX.128 VCVTPH2PS (all 65536 FP16 patterns, specials under DAZ/FTZ/IM=0/DM=0) and
VCVTPS2PH (FP16 midpoints +-1 ulp, the overflow threshold 65504..65536, the denormal range, FP32
denormals, random FP32, imm8 0..3 and 4 with every MXCSR.RC, DAZ, FTZ (ignored), every exception
unmasked); the expectations come from cvt_widen / cvt_narrow, the functions VCVTPH2PSX,
VCVTSH2SS/SD, VCVTPH2PD, VCVTPS2PHX, VCVTSS2SH, VCVTPD2PH and VCVTSD2SH use:
  emu-alltest --cases HW.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt --strict > LOG
  python ref_evex_fp16.py --hwcmp LOG OUT.json
Result (2026-10-08): 72177 cases compared, 0 differ (vcvtph2ps 16601, vcvtps2ph 55576).
"""

import json
import os
import random
import re
import struct
import sys
import zlib
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_evex_m1 as m1                                     # noqa: E402
from ref_evex_m1 import (Mem, evex, elems, pack, hexs, Fmt, F32, F64, classify, floor_log2,  # noqa: E402
                         round_frac, isqrt, Case, RSI, R14, MEM_RSI, IE, DE, ZE, OE, UE, PE,
                         MXCSR_DEFAULT, DAZ, FTZ)

F16 = Fmt(16, 11, 5)
TWO = Fraction(2)
NANS = ("qnan", "snan")
UM_BIT = 0x800
H_INDEF = 0xFE00

# mnemonics left out of --cases (the coordinator lists the forms the C side lacks)
EXCLUDE = []

GPRS = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12",
        "r13", "r14", "r15"]
SAFE_GPR = [0, 1, 2, 3, 5, 8, 9, 10, 11, 12, 13, 15]       # never RSP/RSI/RDI/R14 (harness)
VL_LL = {16: 0, 32: 1, 64: 2}


# ---------------------------------------------------------------------------------------
# exact rounding
# ---------------------------------------------------------------------------------------
def sgnbit(f):
    return 1 << (f.bits - 1)


def inf_bits(f, s):
    return (sgnbit(f) if s else 0) | (((1 << f.ebits) - 1) << f.fbits)


def zero_bits(f, s):
    return sgnbit(f) if s else 0


def quiet(x, f=F16):
    return x | (1 << (f.fbits - 1))


def cls(x, f=F16):
    return classify(x, f)


def round_to(v, f, rc, um=False, om=False):
    """round the exact non-zero rational v to format f: (bits, flags). rc 0 RNE 1 RD 2 RU 3 RZ.
    um / om: MXCSR.UM / MXCSR.OM unmasked (and no SAE). Masked: UE only for a tiny AND inexact
    result (inexact = the delivered denormalized result differs), OE always with PE. Unmasked
    (Vol1 4.9.1.5: underflow "reported when the result is non-zero tiny, regardless of
    inexactness"): UE for any tiny result, and PE (OE/UE "and the PE flag are set" for an inexact
    result, 4.9.1.6) only when the rounding with UNBOUNDED exponent is inexact -- the result the
    unmasked response refers to (i5-13600K F16C VCVTPS2PH: 3*2^-26 with UM = 0 sets UE only)."""
    neg = v < 0
    q = -v if neg else v
    sign = sgnbit(f) if neg else 0
    e = floor_log2(q)
    cnt_u, inexact_u = round_frac(q, TWO ** (e - f.fbits), rc, neg)
    tiny = cnt_u * TWO ** (e - f.fbits) < TWO ** f.emin
    if e < f.emin:
        cnt, inexact = round_frac(q, TWO ** (f.emin - f.fbits), rc, neg)
        if tiny and um:
            flags = UE | (PE if inexact_u else 0)
        else:
            flags = PE if inexact else 0
            if tiny and inexact:
                flags |= UE
        if cnt >= (1 << f.fbits):
            return sign | (1 << f.fbits) | (cnt - (1 << f.fbits)), flags
        return sign | cnt, flags
    cnt, inexact = round_frac(q, TWO ** (e - f.fbits), rc, neg)
    if cnt == (1 << (f.fbits + 1)):
        cnt >>= 1
        e += 1
    flags = PE if inexact else 0
    if e > f.emax:
        flags = (OE | flags) if om else (OE | PE)
        inf = sign | (((1 << f.ebits) - 1) << f.fbits)
        maxf = sign | ((((1 << f.ebits) - 2) << f.fbits) | ((1 << f.fbits) - 1))
        if rc == 0:
            return inf, flags
        if rc == 3:
            return maxf, flags
        if rc == 1:
            return (inf if neg else maxf), flags
        return (maxf if neg else inf), flags
    return sign | ((e + f.bias) << f.fbits) | (cnt - (1 << f.fbits)), flags


def exact_bits(v, f=F16):
    """bits of an exactly representable rational (0 -> +0)"""
    if v == 0:
        return 0
    r, fl = round_to(v, f, 0)
    assert fl == 0, (v, fl)
    return r


def round_int(v, rc):
    neg = v < 0
    q = -v if neg else v
    cnt, inex = round_frac(q, Fraction(1), rc, neg)
    return (-cnt if neg else cnt), inex


def sqrt_frac(x, prec=44):
    """a rational that rounds like sqrt(x) (x > 0) to any format with < prec - 4 bits: the exact
    root when it is a dyadic rational, else floor(sqrt(x) 2^K)/2^K + 2^-(K+1)"""
    e = floor_log2(x)
    K = prec - e // 2
    y = x * TWO ** (2 * K)
    fl = y.numerator // y.denominator
    s = isqrt(fl)
    if y.denominator == 1 and s * s == fl:
        return Fraction(s) / TWO ** K
    return Fraction(2 * s + 1) / TWO ** (K + 1)


class Env:
    """rounding / exception environment of one instruction"""

    def __init__(self, mxcsr=MXCSR_DEFAULT, rc=None, sae=False, masked_all=False):
        self.mxcsr = mxcsr
        self.mx_rc = (mxcsr >> 13) & 3
        self.rc = self.mx_rc if rc is None else rc
        self.sae = sae or rc is not None
        self.um = not self.sae and not masked_all and not (mxcsr & UM_BIT)
        self.om = not self.sae and not masked_all and not (mxcsr & 0x400)
        self.daz = bool(mxcsr & DAZ)


# ---------------------------------------------------------------------------------------
# FP16 element operations: (result bits, flags)
# ---------------------------------------------------------------------------------------
def fp_arith(op, a, b, env, f=F16):
    """add / sub / mul / div of SRC1 a and SRC2 b"""
    ca, sa, va = cls(a, f)
    cb, sb, vb = cls(b, f)
    if ca in NANS or cb in NANS:
        fl = IE if "snan" in (ca, cb) else 0
        return (quiet(a, f) if ca in NANS else quiet(b, f)), fl
    den = ca == "denorm" or cb == "denorm"
    if op == "div":
        sg = sa ^ sb
        if (ca == "inf" and cb == "inf") or (ca == "zero" and cb == "zero"):
            return f.qnan_indef, IE
        flags = DE if den and not (ca == "denorm" and cb == "zero") else 0
        if ca == "inf":
            return inf_bits(f, sg), flags
        if cb == "inf" or ca == "zero":
            return zero_bits(f, sg), flags
        if cb == "zero":
            return inf_bits(f, sg), flags | ZE
        r, fl = round_to(va / vb, f, env.rc, env.um, env.om)
        return r, flags | fl
    if op == "mul":
        sg = sa ^ sb
        if (ca == "inf" and cb == "zero") or (ca == "zero" and cb == "inf"):
            return f.qnan_indef, IE
        flags = DE if den else 0
        if ca == "inf" or cb == "inf":
            return inf_bits(f, sg), flags
        p = va * vb
        if p == 0:
            return zero_bits(f, sg), flags
        r, fl = round_to(p, f, env.rc, env.um, env.om)
        return r, flags | fl
    if op == "sub":
        sb ^= 1
        vb = -vb if vb is not None else None
    if ca == "inf" and cb == "inf" and sa != sb:
        return f.qnan_indef, IE
    flags = DE if den else 0
    if ca == "inf":
        return inf_bits(f, sa), flags
    if cb == "inf":
        return inf_bits(f, sb), flags
    s = va + vb
    if s == 0:
        if ca == "zero" and cb == "zero" and sa == sb:
            return zero_bits(f, sa), flags
        return zero_bits(f, env.rc == 1), flags
    r, fl = round_to(s, f, env.rc, env.um, env.om)
    return r, flags | fl


def fp_sqrt(b, env):
    c, s, v = cls(b)
    if c in NANS:
        return quiet(b), (IE if c == "snan" else 0)
    if c == "zero":
        return b, 0
    if s:
        return H_INDEF, IE
    flags = DE if c == "denorm" else 0
    if c == "inf":
        return b, flags
    r, fl = round_to(sqrt_frac(v), F16, env.rc, env.um, env.om)
    return r, flags | fl


def fkey(c, s, v):
    if c == "inf":
        return Fraction(-10 ** 9) if s else Fraction(10 ** 9)
    return v


def fp_minmax(op, a, b):
    ca, sa, va = cls(a)
    cb, sb, vb = cls(b)
    if ca in NANS or cb in NANS:
        return b, IE
    flags = DE if "denorm" in (ca, cb) else 0
    if ca == "zero" and cb == "zero":
        return b, flags
    ka, kb = fkey(ca, sa, va), fkey(cb, sb, vb)
    if op == "max":
        return (a if ka > kb else b), flags
    return (a if ka < kb else b), flags


def fma16(x, y, z, negmul, negadd, env, f=F16):
    """RoundFPControl(+-(x*y) +- z)"""
    cx, sx, vx = cls(x, f)
    cy, sy, vy = cls(y, f)
    cz, sz, vz = cls(z, f)
    flags = IE if "snan" in (cx, cy, cz) else 0
    if cx in NANS:
        return quiet(x, f), flags
    if cy in NANS:
        return quiet(y, f), flags
    if cz in NANS:
        return quiet(z, f), flags
    if (cx == "inf" and cy == "zero") or (cx == "zero" and cy == "inf"):
        return f.qnan_indef, IE
    ps = sx ^ sy ^ negmul
    zs = sz ^ negadd
    den = DE if "denorm" in (cx, cy, cz) else 0
    if cx == "inf" or cy == "inf":
        if cz == "inf" and zs != ps:
            return f.qnan_indef, IE
        return inf_bits(f, ps), den
    if cz == "inf":
        return inf_bits(f, zs), den
    p = vx * vy
    if negmul:
        p = -p
    zz = -vz if negadd else vz
    s = p + zz
    if s == 0:
        if p == 0 and zz == 0:
            return (zero_bits(f, ps) if ps == zs else zero_bits(f, env.rc == 1)), den
        return zero_bits(f, env.rc == 1), den
    r, fl = round_to(s, f, env.rc, env.um, env.om)
    return r, den | fl


# Vol2A Table 3-8: (A > B, A < B, A = B, unordered) for predicates 0-15; 16-31 repeat them with
# the signalling property inverted
CMP_TRUTH = [(0, 0, 1, 0), (0, 1, 0, 0), (0, 1, 1, 0), (0, 0, 0, 1), (1, 1, 0, 1), (1, 0, 1, 1),
             (1, 0, 0, 1), (1, 1, 1, 0), (0, 0, 1, 1), (0, 1, 0, 1), (0, 1, 1, 1), (0, 0, 0, 0),
             (1, 1, 0, 0), (1, 0, 1, 0), (1, 0, 0, 0), (1, 1, 1, 1)]
CMP_SIGNAL = [0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0]


def fp_cmp(pred, a, b):
    ca, sa, va = cls(a)
    cb, sb, vb = cls(b)
    p = pred & 0xF
    signal = CMP_SIGNAL[p] ^ ((pred >> 4) & 1)
    gt, lt, eq, un = CMP_TRUTH[p]
    if ca in NANS or cb in NANS:
        fl = IE if ("snan" in (ca, cb) or signal) else 0
        return bool(un), fl
    flags = DE if "denorm" in (ca, cb) else 0
    ka, kb = fkey(ca, sa, va), fkey(cb, sb, vb)
    if ka > kb:
        return bool(gt), flags
    if ka < kb:
        return bool(lt), flags
    return bool(eq), flags


def fp_comi(a, b, signal_qnan):
    """(ZF, PF, CF, flags)"""
    ca, sa, va = cls(a)
    cb, sb, vb = cls(b)
    if ca in NANS or cb in NANS:
        fl = IE if ("snan" in (ca, cb) or signal_qnan) else 0
        return 1, 1, 1, fl
    flags = DE if "denorm" in (ca, cb) else 0
    ka, kb = fkey(ca, sa, va), fkey(cb, sb, vb)
    if ka > kb:
        return 0, 0, 0, flags
    if ka < kb:
        return 0, 0, 1, flags
    return 1, 0, 0, flags


def getexp16(x):
    c, s, v = cls(x)
    if c in NANS:
        return quiet(x), (IE if c == "snan" else 0)
    if c == "inf":
        return 0x7C00, 0
    if c == "zero":
        return 0xFC00, 0
    e = floor_log2(abs(v))
    return exact_bits(Fraction(e)), (DE if c == "denorm" else 0)


def getmant16(x, imm):
    sc, interv = (imm >> 2) & 3, imm & 3
    c, s, v = cls(x)
    if c in NANS:
        return quiet(x), (IE if c == "snan" else 0)
    one = 0x3C00 if sc & 1 else 0xBC00
    if not s and c in ("zero", "inf"):
        return 0x3C00, 0
    if s:
        if c == "zero":
            return one, 0
        if c == "inf":
            if sc & 2:
                return H_INDEF, IE
            return one, 0
        if sc & 2:
            return H_INDEF, IE
    flags = DE if c == "denorm" else 0
    q = abs(v)
    e = floor_log2(q)
    m = q / TWO ** e
    if interv == 1 and (e & 1):
        m /= 2
    elif interv == 2:
        m /= 2
    elif interv == 3 and m >= Fraction(3, 2):
        m /= 2
    sign = 0 if sc & 1 else s
    return exact_bits(m) | (0x8000 if sign else 0), flags


def reduce16(x, imm, env):
    c, s, v = cls(x)
    if c in NANS:
        return quiet(x), (IE if c == "snan" else 0)
    m = imm >> 4
    rc = env.mx_rc if imm & 4 else imm & 3
    spe = imm & 8
    if c == "inf":
        return 0, 0
    if c == "zero":
        return (0x8000 if rc == 1 else 0), 0
    n, inex = round_int(v * TWO ** m, rc)
    r = v - Fraction(n) / TWO ** m
    flags = PE if (inex and not spe) else 0
    if r == 0:
        return (0x8000 if rc == 1 else 0), flags
    # exact unless |src| < 2^-M rounded to +-1 (Table 5-28: Round(Src1 -+ 2^-M)): same RC, only PE
    return round_to(r, F16, rc)[0], flags


def rndscale16(x, imm, env):
    c, s, v = cls(x)
    if c in NANS:
        return quiet(x), (IE if c == "snan" else 0)
    if c in ("inf", "zero"):
        return x, 0
    m = imm >> 4
    rc = env.mx_rc if imm & 4 else imm & 3
    spe = imm & 8
    n, inex = round_int(v * TWO ** m, rc)
    r = Fraction(n) / TWO ** m
    flags = PE if (inex and not spe) else 0
    if r == 0:
        return (0x8000 if s else 0), flags
    if abs(r) < TWO ** -14 and inex:
        flags |= UE
    return exact_bits(r), flags


def scalef16(a, b, env):
    ca, sa, va = cls(a)
    cb, sb, vb = cls(b)
    ie = IE if "snan" in (ca, cb) else 0
    if ca == "qnan":
        if cb == "inf":
            return (0 if sb else 0x7C00), ie
        return quiet(a), ie
    if ca == "snan":
        return quiet(a), IE
    if cb in NANS:
        return quiet(b), ie
    if ca == "inf":
        if cb == "inf" and sb:
            return H_INDEF, IE
        return a, 0
    if ca == "zero":
        if cb == "inf" and not sb:
            return H_INDEF, IE
        return a, 0
    flags = DE if ca == "denorm" else 0
    if cb == "inf":
        return (zero_bits(F16, sa) if sb else inf_bits(F16, sa)), flags
    n = vb.numerator // vb.denominator if cb != "zero" else 0
    n = max(-80, min(80, n))
    r, fl = round_to(va * TWO ** n, F16, env.rc, env.um, env.om)
    return r, flags | fl


def rcp16(x):
    c, s, v = cls(x)
    if c in NANS:
        return quiet(x), 0
    if c == "zero":
        return inf_bits(F16, s), 0
    if c == "inf":
        return zero_bits(F16, s), 0
    return round_to(1 / v, F16, 0)[0], 0


def rsqrt16(x):
    c, s, v = cls(x)
    if c in NANS:
        return quiet(x), 0
    if c == "zero":
        return inf_bits(F16, s), 0
    if s:
        return H_INDEF, 0
    if c == "inf":
        return 0, 0
    return round_to(sqrt_frac(1 / v), F16, 0)[0], 0


def fpclass16(x, imm):
    neg = x >> 15
    e = (x >> 10) & 0x1F
    mz = (x & 0x3FF) == 0
    zero = e == 0 and mz
    sbit = (x >> 9) & 1
    snan = e == 0x1F and not mz and not sbit
    qnan = e == 0x1F and not mz and sbit
    tests = [qnan, not neg and zero, neg and zero, not neg and e == 0x1F and mz, neg and e == 0x1F and mz,
             e == 0 and not mz, neg and e != 0x1F and not zero, snan]
    return any(((imm >> i) & 1) and t for i, t in enumerate(tests)), 0


def cvt_widen(x, fo, de):
    """FP16 -> FP32/FP64 (exact)"""
    c, s, v = cls(x)
    if c in NANS:
        r = (s << (fo.bits - 1)) | (((1 << fo.ebits) - 1) << fo.fbits) | ((x & 0x3FF) << (fo.fbits - 10)) \
            | (1 << (fo.fbits - 1))
        return r, (IE if c == "snan" else 0)
    if c == "inf":
        return inf_bits(fo, s), 0
    if c == "zero":
        return zero_bits(fo, s), 0
    return exact_bits(v, fo), (DE if (de and c == "denorm") else 0)


def cvt_narrow(x, fi, env, daz):
    """FP32/FP64 -> FP16"""
    c, s, v = cls(x, fi)
    if c in NANS:
        frac = x & ((1 << fi.fbits) - 1)
        return (s << 15) | 0x7C00 | 0x200 | (frac >> (fi.fbits - 10)), (IE if c == "snan" else 0)
    if c == "inf":
        return inf_bits(F16, s), 0
    if c == "zero":
        return zero_bits(F16, s), 0
    flags = 0
    if c == "denorm":
        if daz:
            return zero_bits(F16, s), 0
        # VCVTPS2PH page: a source denormal in its input format "with DM masked and at least one
        # of PM or UM unmasked; a SIMD exception will be raised with DE, UE and PE set" (also the
        # masked result: every FP32/FP64 denormal is tiny and inexact in FP16)
        r, fl = round_to(v, F16, env.rc, env.um, env.om)
        return r, DE | UE | PE
    r, fl = round_to(v, F16, env.rc, env.um, env.om)
    return r, flags | fl


def cvt_h2int(x, bits, signed, env, trunc):
    rc = 3 if trunc else env.rc
    c, s, v = cls(x)
    indef = (1 << (bits - 1)) if signed else (1 << bits) - 1
    if c in NANS or c == "inf":
        return indef, IE
    if c == "zero":
        return 0, 0
    n, inex = round_int(v, rc)
    lo, hi = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)
    if n < lo or n > hi:
        return indef, IE
    return n & ((1 << bits) - 1), (PE if inex else 0)


def cvt_int2h(i, bits, signed, env):
    i &= (1 << bits) - 1
    if signed and i >> (bits - 1):
        i -= 1 << bits
    if i == 0:
        return 0, 0
    return round_to(Fraction(i), F16, env.rc, env.um, env.om)


def cplx(a, b, d, conj, acc, env):
    """one complex pair (32-bit: real = low FP16, imaginary = high FP16)"""
    a0, a1, b0, b1 = a & 0xFFFF, a >> 16, b & 0xFFFF, b >> 16
    if acc:
        t0, f0 = fma16(a0, b0, d & 0xFFFF, 0, 0, env)
        t1, f1 = fma16(a1, b0, d >> 16, 0, 0, env)
    else:
        t0, f0 = fp_arith("mul", a0, b0, env)
        t1, f1 = fp_arith("mul", a1, b0, env)
    if conj:
        r0, f2 = fma16(a1, b1, t0, 0, 0, env)
        r1, f3 = fma16(a0, b1, t1, 1, 0, env)
    else:
        r0, f2 = fma16(a1, b1, t0, 1, 0, env)
        r1, f3 = fma16(a0, b1, t1, 0, 0, env)
    return r0 | (r1 << 16), f0 | f1 | f2 | f3


# ---------------------------------------------------------------------------------------
# value domains
# ---------------------------------------------------------------------------------------
DOM_SIZE = {"h": 2, "s": 4, "d": 8, "w": 2, "uw": 2, "dw": 4, "udw": 4, "q": 8, "uq": 8, "c": 4}


def f32b(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def f64b(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


SPECIAL16 = [0x0000, 0x8000, 0x3C00, 0xBC00, 0x7C00, 0xFC00, 0x7E00, 0x7D00, 0x0001, 0x8200, 0x7BFF,
             0x0400, 0x3555, 0x6801, 0x1400, 0xFBFF, 0x03FF, 0xFE01, 0xFC01, 0x8400, 0x4248, 0x0200]
EDGE16 = [0x7BFF, 0xFBFF, 0x7800, 0x7A00, 0x0400, 0x8400, 0x0401, 0x83FF, 0x0001, 0x8001, 0x3800,
          0xB800, 0x3C01, 0x3BFF, 0x2400, 0x1000, 0x0800, 0x4000, 0xC000, 0x5640, 0x6400, 0x7000,
          0x0200, 0x3E00, 0x4100, 0xC100, 0x1C00, 0x9C00, 0x2000, 0x0002, 0x3A00, 0x4180, 0xB4CD]
SPECIAL32 = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000, 0x7FC00000,
             0x7FA00000, 0xFF800001, 0x00000001, 0x80400000, 0x7F7FFFFF, 0x00800000,
             f32b(65504.0), f32b(65520.0), f32b(65520.0) - 1, f32b(2.0 ** -24), f32b(2.0 ** -25),
             f32b(2.0 ** -25) + 1, f32b(1023 * 2.0 ** -24), f32b(2.0 ** -14), f32b(1 + 2.0 ** -11),
             f32b(1 + 2.0 ** -11) + 1, f32b(-(1 + 3 * 2.0 ** -11)), 0xC77FF000, f32b(3 * 2.0 ** -26)]
SPECIAL64 = [0x0000000000000000, 0x8000000000000000, 0x3FF0000000000000, 0xBFF0000000000000,
             0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000, 0x7FF4000000000000,
             0xFFF0000000000001, 0x0000000000000001, 0x8008000000000000, 0x7FEFFFFFFFFFFFFF,
             0x0010000000000000, f64b(65504.0), f64b(65520.0), f64b(65520.0) - 1, f64b(2.0 ** -24),
             f64b(2.0 ** -25), f64b(2.0 ** -25) + 1, f64b(1023 * 2.0 ** -24), f64b(2.0 ** -14),
             f64b(1 + 2.0 ** -11), f64b(1 + 2.0 ** -11) + 1, f64b(-(1 + 3 * 2.0 ** -11)),
             f64b(1e300), f64b(3 * 2.0 ** -26)]


def int_specials(bits, signed):
    m = (1 << bits) - 1
    v = [0, 1, m, 2, 3, 2049, 2051, 4097, 65504, 65519, 65520, 65535, 12345, 0x7FF, 0x800,
         (1 << (bits - 1)) - 1, 1 << (bits - 1), (-1) & m, (-2049) & m, (-65520) & m, (-65519) & m,
         (-32768) & m, 32767, 32768, 0x1001]
    return [x & m for x in v]


SPECIALS = {"h": SPECIAL16, "s": SPECIAL32, "d": SPECIAL64,
            "w": int_specials(16, True), "uw": int_specials(16, False),
            "dw": int_specials(32, True), "udw": int_specials(32, False),
            "q": int_specials(64, True), "uq": int_specials(64, False)}
SPECIALS["c"] = [SPECIAL16[i] | (SPECIAL16[(i * 7 + 3) % len(SPECIAL16)] << 16) for i in range(len(SPECIAL16))]


class Rng:
    """deterministic per-mnemonic random source"""

    def __init__(self, seed):
        self.r = random.Random(seed)

    def bits(self, n):
        return self.r.getrandbits(n)

    def bytes(self, n):
        return bytes(self.r.getrandbits(8) for _ in range(n))

    def choice(self, s):
        return self.r.choice(s)

    def randint(self, a, b):
        return self.r.randint(a, b)

    def random(self):
        return self.r.random()

    def h(self, mode="mix"):
        r = self.r.random()
        if mode == "normal" or r < 0.80:
            e = self.r.choice([self.r.randint(10, 20), self.r.randint(1, 30)])
            return (self.r.getrandbits(1) << 15) | (e << 10) | self.r.getrandbits(10)
        if r < 0.92:
            return (self.r.getrandbits(1) << 15) | self.r.randint(1, 0x3FF)
        return self.r.choice(SPECIAL16 + EDGE16)

    def val(self, dom, mode="mix"):
        r = self.r
        if dom == "h":
            return self.h(mode)
        if dom == "c":
            return self.h(mode) | (self.h(mode) << 16)
        if dom == "s":
            x = r.random()
            if x < 0.6 or mode == "normal":
                e = r.choice([r.randint(127 - 16, 127 + 16), r.randint(127 - 26, 127 + 17), r.randint(1, 254)])
                return (r.getrandbits(1) << 31) | (e << 23) | r.getrandbits(23)
            if x < 0.85:          # FP16 midpoint neighbourhood
                h = r.randint(1, 0x7BFE)
                mid = (Fraction(cls(h)[2]) + Fraction(cls(h + 1)[2])) / 2
                return (f32b(float(mid)) + r.choice([-1, 0, 0, 1])) | (r.getrandbits(1) << 31)
            if x < 0.92:
                return (r.getrandbits(1) << 31) | r.randint(1, 0x7FFFFF)
            return r.choice(SPECIAL32)
        if dom == "d":
            x = r.random()
            if x < 0.6 or mode == "normal":
                e = r.choice([r.randint(1023 - 16, 1023 + 16), r.randint(1023 - 26, 1023 + 17), r.randint(1, 2046)])
                return (r.getrandbits(1) << 63) | (e << 52) | r.getrandbits(52)
            if x < 0.85:
                h = r.randint(1, 0x7BFE)
                mid = (Fraction(cls(h)[2]) + Fraction(cls(h + 1)[2])) / 2
                return (f64b(float(mid)) + r.choice([-1, 0, 0, 1])) | (r.getrandbits(1) << 63)
            if x < 0.92:
                return (r.getrandbits(1) << 63) | r.randint(1, (1 << 52) - 1)
            return r.choice(SPECIAL64)
        bits = DOM_SIZE[dom] * 8
        x = r.random()
        if x < 0.35:
            return r.getrandbits(bits)
        if x < 0.70:
            return r.randint(-70000, 70000) & ((1 << bits) - 1)
        if x < 0.85:
            return (r.randint(-3000, 3000) << r.randint(0, max(0, bits - 14))) & ((1 << bits) - 1)
        return r.choice(SPECIALS[dom])


# ---------------------------------------------------------------------------------------
# forms
# ---------------------------------------------------------------------------------------
EXC_NAMES = {"I": IE, "D": DE, "Z": ZE, "O": OE, "U": UE, "P": PE}


class Form:
    def __init__(self, name, mmm, pp, opc, w, lay, fn=None, scalar=False, sdom="h", adom="h",
                 ddom="h", bcst=True, rcsae=None, imms=None, exc="", upper="a", cplx=False,
                 rounds=False, daz_src=False):
        self.name, self.mmm, self.pp, self.opc, self.w, self.lay = name, mmm, pp, opc, w, lay
        self.fn, self.scalar, self.sdom, self.adom, self.ddom = fn, scalar, sdom, adom, ddom
        self.bcst = bcst and not scalar
        self.rcsae, self.imms, self.upper, self.cplx = rcsae, imms, upper, cplx
        self.excmask = 0
        for ch in exc:
            self.excmask |= EXC_NAMES[ch]
        self.sesz, self.aesz, self.desz = DOM_SIZE[sdom], DOM_SIZE[adom], DOM_SIZE[ddom]
        self.rounds = rounds          # result depends on the rounding mode
        self.daz_src = daz_src
        self.rng = None

    def kl(self, vl):
        return 1 if self.scalar else vl // max(self.sesz, self.desz)

    def wbit(self, rng=None):
        if self.w == "ig":
            return rng.bits(1) if rng else 0
        return self.w


FORMS = []


def form(*a, **kw):
    f = Form(*a, **kw)
    FORMS.append(f)
    return f


M5, M6, M3 = 5, 6, 3
NP, P66, PF3, PF2 = 0, 1, 2, 3


def _bin(op):
    return lambda env, j, d, a, b, imm: fp_arith(op, a, b, env)


def _mm(op):
    return lambda env, j, d, a, b, imm: fp_minmax(op, a, b)


ARITH = [("ADD", 0x58, "add", "IUOPD", "er"), ("SUB", 0x5C, "sub", "IUOPD", "er"),
         ("MUL", 0x59, "mul", "IUOPD", "er"), ("DIV", 0x5E, "div", "IUOPDZ", "er")]
for nm, opc, op, exc, rs in ARITH:
    form("V%sPH" % nm, M5, NP, opc, 0, "rvm", _bin(op), rcsae=rs, exc=exc, rounds=True)
    form("V%sSH" % nm, M5, PF3, opc, 0, "rvm", _bin(op), scalar=True, rcsae=rs, exc=exc, rounds=True)
for nm, opc, op in (("MIN", 0x5D, "min"), ("MAX", 0x5F, "max")):
    form("V%sPH" % nm, M5, NP, opc, 0, "rvm", _mm(op), rcsae="sae", exc="ID")
    form("V%sSH" % nm, M5, PF3, opc, 0, "rvm", _mm(op), scalar=True, rcsae="sae", exc="ID")
form("VSQRTPH", M5, NP, 0x51, 0, "rm", lambda env, j, d, a, b, imm: fp_sqrt(b, env), rcsae="er", exc="IPD",
     rounds=True)
form("VSQRTSH", M5, PF3, 0x51, 0, "rvm", lambda env, j, d, a, b, imm: fp_sqrt(b, env), scalar=True, rcsae="er",
     exc="IPD", rounds=True)
form("VRCPPH", M6, P66, 0x4C, 0, "rm", lambda env, j, d, a, b, imm: rcp16(b))
form("VRCPSH", M6, P66, 0x4D, 0, "rvm", lambda env, j, d, a, b, imm: rcp16(b), scalar=True)
form("VRSQRTPH", M6, P66, 0x4E, 0, "rm", lambda env, j, d, a, b, imm: rsqrt16(b))
form("VRSQRTSH", M6, P66, 0x4F, 0, "rvm", lambda env, j, d, a, b, imm: rsqrt16(b), scalar=True)
form("VSCALEFPH", M6, P66, 0x2C, 0, "rvm", lambda env, j, d, a, b, imm: scalef16(a, b, env), rcsae="er",
     exc="OUIPD", rounds=True)
form("VSCALEFSH", M6, P66, 0x2D, 0, "rvm", lambda env, j, d, a, b, imm: scalef16(a, b, env), scalar=True,
     rcsae="er", exc="OUIPD", rounds=True)
form("VGETEXPPH", M6, P66, 0x42, 0, "rm", lambda env, j, d, a, b, imm: getexp16(b), rcsae="sae", exc="ID")
form("VGETEXPSH", M6, P66, 0x43, 0, "rvm", lambda env, j, d, a, b, imm: getexp16(b), scalar=True, rcsae="sae",
     exc="ID")
GETMANT_IMMS = list(range(16))
RND_IMMS = [0x00, 0x01, 0x02, 0x03, 0x04, 0x08, 0x0B, 0x10, 0x21, 0x32, 0x43, 0x58, 0x7C, 0xA0, 0xF2, 0x24]
form("VGETMANTPH", M3, NP, 0x26, 0, "rm", lambda env, j, d, a, b, imm: getmant16(b, imm), rcsae="sae", exc="ID",
     imms=GETMANT_IMMS)
form("VGETMANTSH", M3, NP, 0x27, 0, "rvm", lambda env, j, d, a, b, imm: getmant16(b, imm), scalar=True,
     rcsae="sae", exc="ID", imms=GETMANT_IMMS)
form("VREDUCEPH", M3, NP, 0x56, 0, "rm", lambda env, j, d, a, b, imm: reduce16(b, imm, env), rcsae="sae",
     exc="IP", imms=RND_IMMS)
form("VREDUCESH", M3, NP, 0x57, 0, "rvm", lambda env, j, d, a, b, imm: reduce16(b, imm, env), scalar=True,
     rcsae="sae", exc="IP", imms=RND_IMMS)
form("VRNDSCALEPH", M3, NP, 0x08, 0, "rm", lambda env, j, d, a, b, imm: rndscale16(b, imm, env), rcsae="sae",
     exc="IUP", imms=RND_IMMS)
form("VRNDSCALESH", M3, NP, 0x0A, 0, "rvm", lambda env, j, d, a, b, imm: rndscale16(b, imm, env), scalar=True,
     rcsae="sae", exc="IUP", imms=RND_IMMS)
FPCLASS_IMMS = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF, 0x81, 0x66, 0x18, 0x00, 0x5A]
form("VFPCLASSPH", M3, NP, 0x66, 0, "krm", lambda env, j, d, a, b, imm: fpclass16(b, imm), imms=FPCLASS_IMMS)
form("VFPCLASSSH", M3, NP, 0x67, 0, "krm", lambda env, j, d, a, b, imm: fpclass16(b, imm), scalar=True,
     imms=FPCLASS_IMMS)
CMP_IMMS = list(range(32)) + [0xE5, 0x3F]
form("VCMPPH", M3, NP, 0xC2, 0, "krvm", lambda env, j, d, a, b, imm: fp_cmp(imm & 0x1F, a, b), rcsae="sae",
     exc="ID", imms=CMP_IMMS)
form("VCMPSH", M3, PF3, 0xC2, 0, "krvm", lambda env, j, d, a, b, imm: fp_cmp(imm & 0x1F, a, b), scalar=True,
     rcsae="sae", exc="ID", imms=CMP_IMMS)
form("VCOMISH", M5, NP, 0x2F, 0, "comi", None, scalar=True, rcsae="sae", exc="ID")
form("VUCOMISH", M5, NP, 0x2E, 0, "comi", None, scalar=True, rcsae="sae", exc="ID")


def _fma(order, negmul, addsub):
    """addsub: 'add', 'sub', 'addsub' (even lanes subtract), 'subadd' (even lanes add)"""
    def fn(env, j, d, a, b, imm):
        if addsub == "addsub":
            negadd = 1 if j % 2 == 0 else 0
        elif addsub == "subadd":
            negadd = 0 if j % 2 == 0 else 1
        else:
            negadd = 1 if addsub == "sub" else 0
        if order == 132:
            x, y, z = d, b, a
        elif order == 213:
            x, y, z = a, d, b
        else:
            x, y, z = a, b, d
        return fma16(x, y, z, negmul, negadd, env)
    return fn


for order, base in ((132, 0x96), (213, 0xA6), (231, 0xB6)):
    form("VFMADDSUB%dPH" % order, M6, P66, base, 0, "fma", _fma(order, 0, "addsub"), rcsae="er", exc="IUOPD",
         rounds=True, upper="d")
    form("VFMSUBADD%dPH" % order, M6, P66, base + 1, 0, "fma", _fma(order, 0, "subadd"), rcsae="er",
         exc="IUOPD", rounds=True, upper="d")
    for k, (nm, negmul, sub) in enumerate((("VFMADD", 0, "add"), ("VFMSUB", 0, "sub"), ("VFNMADD", 1, "add"),
                                           ("VFNMSUB", 1, "sub"))):
        form("%s%dPH" % (nm, order), M6, P66, base + 2 + 2 * k, 0, "fma", _fma(order, negmul, sub), rcsae="er",
             exc="IUOPD", rounds=True, upper="d")
        form("%s%dSH" % (nm, order), M6, P66, base + 3 + 2 * k, 0, "fma", _fma(order, negmul, sub), scalar=True,
             rcsae="er", exc="IUOPD", rounds=True, upper="d")


def _cplx(conj, acc):
    return lambda env, j, d, a, b, imm: cplx(a, b, d, conj, acc, env)


form("VFCMADDCPH", M6, PF2, 0x56, 0, "fma", _cplx(1, 1), sdom="c", adom="c", ddom="c", rcsae="er",
     exc="IUOPD", cplx=True, rounds=True, upper="a")
form("VFMADDCPH", M6, PF3, 0x56, 0, "fma", _cplx(0, 1), sdom="c", adom="c", ddom="c", rcsae="er",
     exc="IUOPD", cplx=True, rounds=True, upper="a")
form("VFCMADDCSH", M6, PF2, 0x57, 0, "fma", _cplx(1, 1), sdom="c", adom="c", ddom="c", scalar=True,
     rcsae="er", exc="IUOPD", cplx=True, rounds=True, upper="a")
form("VFMADDCSH", M6, PF3, 0x57, 0, "fma", _cplx(0, 1), sdom="c", adom="c", ddom="c", scalar=True,
     rcsae="er", exc="IUOPD", cplx=True, rounds=True, upper="a")
form("VFCMULCPH", M6, PF2, 0xD6, 0, "rvm", _cplx(1, 0), sdom="c", adom="c", ddom="c", rcsae="er",
     exc="IUOPD", cplx=True, rounds=True)
form("VFMULCPH", M6, PF3, 0xD6, 0, "rvm", _cplx(0, 0), sdom="c", adom="c", ddom="c", rcsae="er",
     exc="IUOPD", cplx=True, rounds=True)
form("VFCMULCSH", M6, PF2, 0xD7, 0, "rvm", _cplx(1, 0), sdom="c", adom="c", ddom="c", scalar=True,
     rcsae="er", exc="IUOPD", cplx=True, rounds=True)
form("VFMULCSH", M6, PF3, 0xD7, 0, "rvm", _cplx(0, 0), sdom="c", adom="c", ddom="c", scalar=True,
     rcsae="er", exc="IUOPD", cplx=True, rounds=True)

# conversions FP <-> FP
form("VCVTPH2PSX", M6, P66, 0x13, 0, "rm", lambda env, j, d, a, b, imm: cvt_widen(b, F32, True), sdom="h",
     ddom="s", rcsae="sae", exc="ID")
form("VCVTSH2SS", M6, NP, 0x13, 0, "rvm", lambda env, j, d, a, b, imm: cvt_widen(b, F32, True), scalar=True,
     sdom="h", adom="s", ddom="s", rcsae="sae", exc="ID")
form("VCVTPH2PD", M5, NP, 0x5A, 0, "rm", lambda env, j, d, a, b, imm: cvt_widen(b, F64, True), sdom="h",
     ddom="d", rcsae="sae", exc="ID")
form("VCVTSH2SD", M5, PF3, 0x5A, 0, "rvm", lambda env, j, d, a, b, imm: cvt_widen(b, F64, True), scalar=True,
     sdom="h", adom="d", ddom="d", rcsae="sae", exc="ID")
form("VCVTPS2PHX", M5, P66, 0x1D, 0, "rm", lambda env, j, d, a, b, imm: cvt_narrow(b, F32, env, env.daz),
     sdom="s", ddom="h", rcsae="er", exc="IUOPD", rounds=True, daz_src=True)
form("VCVTSS2SH", M5, NP, 0x1D, 0, "rvm", lambda env, j, d, a, b, imm: cvt_narrow(b, F32, env, env.daz),
     scalar=True, sdom="s", adom="h", ddom="h", rcsae="er", exc="IUOPD", rounds=True, daz_src=True)
form("VCVTPD2PH", M5, P66, 0x5A, 1, "rm", lambda env, j, d, a, b, imm: cvt_narrow(b, F64, env, env.daz),
     sdom="d", ddom="h", rcsae="er", exc="IUOPD", rounds=True, daz_src=True)
form("VCVTSD2SH", M5, PF2, 0x5A, 1, "rvm", lambda env, j, d, a, b, imm: cvt_narrow(b, F64, env, env.daz),
     scalar=True, sdom="d", adom="h", ddom="h", rcsae="er", exc="IUOPD", rounds=True, daz_src=True)

# conversions FP16 -> integer
INT_DOM = {"dw": (32, True), "udw": (32, False), "q": (64, True), "uq": (64, False), "w": (16, True),
           "uw": (16, False)}


def _h2i(dom, trunc):
    bits, sg = INT_DOM[dom]
    return lambda env, j, d, a, b, imm: cvt_h2int(b, bits, sg, env, trunc)


def _i2h(dom):
    bits, sg = INT_DOM[dom]
    return lambda env, j, d, a, b, imm: cvt_int2h(b, bits, sg, env)


for nm, pp, opc, dom in (("DQ", P66, 0x5B, "dw"), ("UDQ", NP, 0x79, "udw"), ("QQ", P66, 0x7B, "q"),
                         ("UQQ", P66, 0x79, "uq"), ("W", P66, 0x7D, "w"), ("UW", NP, 0x7D, "uw")):
    form("VCVTPH2%s" % nm, M5, pp, opc, 0, "rm", _h2i(dom, False), sdom="h", ddom=dom, rcsae="er", exc="IP",
         rounds=True)
for nm, pp, opc, dom in (("DQ", PF3, 0x5B, "dw"), ("UDQ", NP, 0x78, "udw"), ("QQ", P66, 0x7A, "q"),
                         ("UQQ", P66, 0x78, "uq"), ("W", P66, 0x7C, "w"), ("UW", NP, 0x7C, "uw")):
    form("VCVTTPH2%s" % nm, M5, pp, opc, 0, "rm", _h2i(dom, True), sdom="h", ddom=dom, rcsae="sae", exc="IP")
for nm, pp, opc, w, dom, exc in (("DQ", NP, 0x5B, 0, "dw", "OP"), ("QQ", NP, 0x5B, 1, "q", "OP"),
                                 ("UDQ", PF2, 0x7A, 0, "udw", "OP"), ("UQQ", PF2, 0x7A, 1, "uq", "OP"),
                                 ("W", PF3, 0x7D, 0, "w", "P"), ("UW", PF2, 0x7D, 0, "uw", "OP")):
    form("VCVT%s2PH" % nm, M5, pp, opc, w, "rm", _i2h(dom), sdom=dom, ddom="h", rcsae="er", exc=exc, rounds=True)

# GPR forms (W0 = 32-bit GPR, W1 = 64-bit GPR)
for w in (0, 1):
    form("VCVTSH2SI", M5, PF3, 0x2D, w, "gdst", None, scalar=True, rcsae="er", exc="IP")
    form("VCVTSH2USI", M5, PF3, 0x79, w, "gdst", None, scalar=True, rcsae="er", exc="IP")
    form("VCVTTSH2SI", M5, PF3, 0x2C, w, "gdst", None, scalar=True, rcsae="sae", exc="IP")
    form("VCVTTSH2USI", M5, PF3, 0x78, w, "gdst", None, scalar=True, rcsae="sae", exc="IP")
    form("VCVTSI2SH", M5, PF3, 0x2A, w, "gsrc", None, scalar=True, rcsae="er", exc="OP")
    form("VCVTUSI2SH", M5, PF3, 0x7B, w, "gsrc", None, scalar=True, rcsae="er", exc="OP")
# moves
form("VMOVSH", M5, PF3, 0x10, 0, "movsh_ld", None, scalar=True)
form("VMOVSH", M5, PF3, 0x11, 0, "movsh_st", None, scalar=True)
form("VMOVSH", M5, PF3, 0x10, 0, "movsh_rr", None, scalar=True)
form("VMOVSH", M5, PF3, 0x11, 0, "movsh_rr11", None, scalar=True)
form("VMOVW", M5, P66, 0x6E, "ig", "movw_ld", None, scalar=True)
form("VMOVW", M5, P66, 0x7E, "ig", "movw_st", None, scalar=True)


def all_mnemonics():
    seen = []
    for f in FORMS:
        if f.name not in seen:
            seen.append(f.name)
    return seen


# ---------------------------------------------------------------------------------------
# case generation
# ---------------------------------------------------------------------------------------
OUT = []          # current group's lines (Case or str)
R = None          # current group's Rng


def emit(c):
    OUT.append(c)


def note(t):
    OUT.append("# " + t)


def mem_disp_for(n):
    return n * R.choice([1, -1, 2, -3, 5, 127, -128])


def flags_after(mxcsr, flags, unm):
    if unm & (IE | DE | ZE):
        flags &= ~(OE | UE | PE)
    return flags


def finish_fp(c, mxcsr, flags, env, nofault=False):
    """returns True when the instruction faults with #XM (sets c.fault); appends mxcsr"""
    if env.sae:
        flags = 0
    unm = 0 if (env.sae or nofault) else (flags & ~(mxcsr >> 7) & 0x3F)
    flags = flags_after(mxcsr, flags, unm)
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    if unm:
        c.fault = "#XM"
        return True
    return False


def vcase(sp, vl, title, d=1, a=2, b=3, kreg=0, kval=None, z=0, mem=None, bcst=0, vals=None,
          mxcsr=MXCSR_DEFAULT, rc=None, sae=False, ll=None, imm=None, w=None):
    """one case of a vector / scalar form with layout rvm, rm, fma, krvm or krm"""
    lay = sp.lay
    KL = sp.kl(vl)
    kdest = lay in ("krvm", "krm")
    has_a = lay in ("rvm", "fma", "krvm")
    if imm is None and sp.imms:
        imm = R.choice(sp.imms)
    vals = dict(vals or {})
    c = Case("%s %s%s" % (sp.name, "" if sp.scalar else "VL%d " % (vl * 8), title))
    nb = 1 if bcst else KL
    if "b" not in vals:
        vals["b"] = [R.val(sp.sdom) for _ in range(nb)]
    if has_a and "a" not in vals:
        vals["a"] = [R.val(sp.adom) for _ in range(KL)]
    if lay == "fma" and "d" not in vals:
        vals["d"] = [R.val(sp.ddom) for _ in range(KL)]
    imgs = {}

    def place(reg, esz, values):
        if reg in imgs:
            return
        img = bytearray(R.bytes(64))
        for j, v in enumerate(values):
            img[j * esz:(j + 1) * esz] = v.to_bytes(esz, "little")
        imgs[reg] = img

    if mem is None:
        place(b, sp.sesz, vals["b"])
    if has_a:
        place(a, sp.aesz, vals["a"])
    if not kdest:
        place(d, sp.desz, vals.get("d", []))
    if mem is None:
        bv = elems(bytes(imgs[b][:KL * sp.sesz]), sp.sesz)
    else:
        data = pack(vals["b"], sp.sesz)
        c.mem[MEM_RSI + mem.disp] = data
        bv = [vals["b"][0]] * KL if bcst else list(vals["b"])
    av = elems(bytes(imgs[a][:KL * sp.aesz]), sp.aesz) if has_a else [None] * KL
    dv = elems(bytes(imgs[d][:KL * sp.desz]), sp.desz) if not kdest else [None] * KL
    for r, img in imgs.items():
        c.set_zmm(r, bytes(img))
    if kreg:
        c.k[kreg] = kval
    if kdest:
        c.k[d] = R.bits(64)
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    env = Env(mxcsr, rc=rc, sae=sae, masked_all=sp.cplx)
    kmask = kval if kreg else None
    res, flags = [], 0
    for j in range(KL):
        if kmask is not None and not (kmask >> j) & 1:
            res.append(None)
            continue
        r, fl = sp.fn(env, j, dv[j], av[j], bv[j], imm)
        res.append(r)
        flags |= fl
    assert flags & ~sp.excmask == 0, (sp.name, hex(flags))
    if not finish_fp(c, mxcsr, flags, env, nofault=sp.cplx):
        if kdest:
            bits = 0
            for j, r in enumerate(res):
                if r:
                    bits |= 1 << j
            c.exp.append("k%d=0x%X" % (d, bits))
        else:
            out = bytearray(imgs[d])
            esz = sp.desz
            for j in range(KL):
                if res[j] is None:
                    if z:
                        out[j * esz:(j + 1) * esz] = bytes(esz)
                else:
                    out[j * esz:(j + 1) * esz] = res[j].to_bytes(esz, "little")
            if sp.scalar:
                up = imgs[a] if sp.upper == "a" else imgs[d]
                if sp.upper == "a":
                    out[esz:16] = up[esz:16]
                out[16:64] = bytes(48)
            else:
                out[KL * esz:64] = bytes(64 - KL * esz)
            c.exp.append("zmm%d=%s" % (d, hexs(bytes(out))))
    if ll is None:
        if rc is not None:
            ll = rc
        elif sp.scalar:
            ll = R.choice([0, 0, 1, 2])
        else:
            ll = VL_LL[vl]
    bbit = 1 if (bcst or rc is not None or sae) else 0
    rmop = mem if mem is not None else b
    n = sp.sesz if (bcst or sp.scalar) else KL * sp.sesz
    c.code = evex(sp.mmm, sp.pp, sp.wbit(R) if w is None else w, sp.opc, d, rmop,
                  vvvv=a if has_a else None, ll=ll, b=bbit, z=z, aaa=kreg, imm=imm, n=n)
    return c


def ud(sp, title, **kw):
    """#UD case: kw are evex() overrides"""
    a = dict(mmm=sp.mmm, pp=sp.pp, w=sp.wbit(), opc=sp.opc, reg=1, rm=2, vvvv=None, ll=0, imm=None)
    if sp.lay in ("rvm", "fma", "krvm", "gsrc", "movsh_rr", "movsh_rr11"):
        a["vvvv"] = 3
    if sp.imms:
        a["imm"] = sp.imms[0]
    a.update(kw)
    c = Case("%s %s #UD" % (sp.name, title))
    c.code = evex(a.pop("mmm"), a.pop("pp"), a.pop("w"), a.pop("opc"), a.pop("reg"), a.pop("rm"), **a)
    c.fault = "#UD"
    emit(c)


def sibling_w(sp):
    return any(f is not sp and f.mmm == sp.mmm and f.pp == sp.pp and f.opc == sp.opc and f.w != sp.w
               for f in FORMS)


def pool(dom):
    if dom == "h":
        return SPECIAL16 + EDGE16
    return SPECIALS[dom]


def find_lane(sp, target, mxcsr, imm, j=0, want_exact_ue=False, tries=6000, benign=False):
    """values (d, a, b) for lane j whose flags contain target (benign: do not contain target)"""
    env = Env(mxcsr, masked_all=sp.cplx)
    doms = (sp.ddom, sp.adom, sp.sdom)
    def exactish(dom):
        """values that are exact in FP16 (so a conversion / small operation stays exact)"""
        x = R.randint(1, 30) | (R.bits(1) << 6)
        h = (((x >> 6) & 1) << 15) | (R.randint(10, 20) << 10) | (R.bits(3) << 7)
        if dom == "s":
            return cvt_widen(h, F32, False)[0]
        if dom == "d":
            return cvt_widen(h, F64, False)[0]
        if dom == "c":
            return h | ((h ^ 0x8000) << 16)
        if dom == "h":
            return h
        return R.randint(-2048, 2048) & ((1 << (8 * DOM_SIZE[dom])) - 1)

    for t in range(tries):
        vv = []
        for dom in doms:
            if benign:
                vv.append(R.val(dom, "normal") if t % 2 == 0 else exactish(dom))
            elif R.random() < 0.6:
                vv.append(R.choice(pool(dom)))
            else:
                vv.append(R.val(dom))
        try:
            r, fl = sp.fn(env, j, vv[0], vv[1], vv[2], imm)
        except Exception:
            continue
        if benign:
            if not fl & target:
                return vv
        elif fl & target and (not want_exact_ue or not fl & PE):
            return vv
    return None


def gen_vec(sp, first):
    """generic generator for layouts rvm / rm / fma / krvm / krm"""
    lay = sp.lay
    kdest = lay in ("krvm", "krm")
    has_a = lay in ("rvm", "fma", "krvm")
    vls = (16,) if sp.scalar else (16, 32, 64)
    note("%s (EVEX.%s.%s.W%s %02X%s)%s" % (sp.name, "LLIG" if sp.scalar else "128/256/512",
         ["NP", "66", "F3", "F2"][sp.pp], sp.w, sp.opc, " ib" if sp.imms else "",
         " rc_sae=%s" % sp.rcsae if sp.rcsae else ""))
    # complex: DEST must differ from both sources
    dreg = (lambda r: r)
    for vl in vls:
        KL = sp.kl(vl)
        emit(vcase(sp, vl, "nomask", d=(1 if not kdest else 2), a=2 if kdest else 4, b=3))
        emit(vcase(sp, vl, "merge zmm16+", d=(5 if kdest else 17), a=30, b=9, kreg=3, kval=R.bits(64)))
        if not kdest:
            emit(vcase(sp, vl, "zero", d=4, a=5, b=26, kreg=7, kval=R.bits(64), z=1))
        disp = mem_disp_for(sp.sesz if sp.scalar else KL * sp.sesz)
        emit(vcase(sp, vl, "mem disp=%d" % disp, d=(6 if not kdest else 1), a=8, b=0, mem=Mem(RSI, disp)))
        emit(vcase(sp, vl, "mem R14 zero-mask", d=(19 if not kdest else 6), a=21, b=0, kreg=2,
                   kval=R.bits(64), z=0 if kdest else 1, mem=Mem(R14, -(sp.sesz if sp.scalar else KL * sp.sesz))))
        if sp.bcst:
            disp = sp.sesz * R.choice([1, -1, 3, 127, -128])
            emit(vcase(sp, vl, "{1to%d} merge disp=%d" % (KL, disp), d=(7 if not kdest else 5), a=10, b=0,
                       kreg=4, kval=R.bits(64), mem=Mem(RSI, disp), bcst=1))
        if sp.scalar:
            for t in range(3):
                emit(vcase(sp, vl, "random %d" % t, d=R.randint(0, 31) if not kdest else R.randint(1, 7),
                           a=R.randint(0, 31) if not sp.cplx else 20 + t, b=R.randint(0, 31) if not sp.cplx else 11)
                     if not sp.cplx else vcase(sp, vl, "random %d" % t, d=12 + t, a=20 + t, b=28))
            emit(vcase(sp, vl, "mask bit0=0 merge", d=(13 if not kdest else 3), a=14, b=15, kreg=5,
                       kval=R.bits(64) & ~1))
            if not kdest:
                emit(vcase(sp, vl, "mask bit0=0 zero", d=13, a=14, b=15, kreg=5, kval=R.bits(64) & ~1, z=1))
    vl = 64
    KL = sp.kl(vl)
    # special values
    spec = SPECIALS[sp.sdom]
    aspec = SPECIALS[sp.adom]
    dspec = SPECIALS[sp.ddom]
    nrep = 12 if sp.scalar else 2
    for t in range(nrep):
        if sp.scalar:
            vb = [spec[(t * 5 + 1) % len(spec)]]
            va = [aspec[(t * 3) % len(aspec)]]
            vd = [dspec[(t * 7 + 2) % len(dspec)]]
            if sp.cplx and t % 2:
                va = [R.val("c")]
        else:
            if t == 0:
                vb = [spec[(i * 5 + 1) % len(spec)] for i in range(KL)]
                va = [aspec[(i * 3) % len(aspec)] for i in range(KL)]
                vd = [dspec[(i * 7 + 2) % len(dspec)] for i in range(KL)]
            else:
                vb = [spec[(i * 3) % len(spec)] for i in range(KL)]
                va = [aspec[(i * 5 + 1) % len(aspec)] for i in range(KL)]
                vd = [dspec[(i * 11 + 5) % len(dspec)] for i in range(KL)]
        vals = {"b": vb}
        if has_a:
            vals["a"] = va
        if lay == "fma":
            vals["d"] = vd
        imm = sp.imms[t % len(sp.imms)] if sp.imms else None
        emit(vcase(sp, vl, "specials %d" % t, d=(24 if not kdest else 7), a=25, b=27, vals=vals, imm=imm))
    # edge values (the same edge list in every lane / random pairs)
    if sp.sdom == "h":
        n = 1 if sp.scalar else KL
        for t in range(4 if sp.scalar else 2):
            vals = {"b": [R.choice(EDGE16) for _ in range(n)]}
            if has_a:
                vals["a"] = [R.choice(EDGE16) if sp.adom == "h" else R.val(sp.adom) for _ in range(n)]
            if lay == "fma":
                vals["d"] = [R.choice(EDGE16) if sp.ddom == "h" else R.val(sp.ddom) for _ in range(n)]
            emit(vcase(sp, vl, "edge values %d" % t, d=(29 if not kdest else 2), a=30, b=31, vals=vals))
    # every imm8 value of the list
    if sp.imms:
        for imm in sp.imms:
            emit(vcase(sp, vl, "imm=0x%02X" % imm, d=(9 if not kdest else 6), a=10, b=11, imm=imm,
                       vals={"b": [R.choice(spec + spec) if R.random() < 0.3 else R.val(sp.sdom)
                                   for _ in range(1 if sp.scalar else KL)]}))
    # rounding modes from MXCSR, DAZ/FTZ (never applied to FP16 data)
    if sp.rounds or (sp.imms and sp.name.startswith(("VREDUCE", "VRNDSCALE"))):
        for rcm in (1, 2, 3):
            mx = MXCSR_DEFAULT | (rcm << 13)
            imm = (0x04 | (R.bits(4) << 4)) if sp.name.startswith(("VREDUCE", "VRNDSCALE")) else None
            for t in range(3 if sp.scalar else 1):
                emit(vcase(sp, vl, "MXCSR.RC=%d" % rcm, d=12, a=13, b=14, mxcsr=mx, imm=imm))
    for mx in (MXCSR_DEFAULT | DAZ | FTZ, MXCSR_DEFAULT | DAZ):
        n = 1 if sp.scalar else KL
        vals = {"b": [(R.bits(1) << (8 * sp.sesz - 1)) | R.randint(1, 0x3FF) if sp.sdom == "h" else R.val(sp.sdom)
                      for _ in range(n)]}
        if sp.daz_src:
            vals["b"] = [R.choice([(R.bits(1) << 31) | R.randint(1, 0x7FFFFF), R.val("s")]) if sp.sdom == "s" else
                         R.choice([(R.bits(1) << 63) | R.randint(1, (1 << 52) - 1), R.val("d")]) for _ in range(n)]
        if has_a and sp.adom == "h":
            vals["a"] = [R.choice([0x0001, 0x8003, 0x03FF, 0x0400, R.val("h")]) for _ in range(n)]
        emit(vcase(sp, vl, "MXCSR=%X (DAZ/FTZ: FP16 denormals kept)" % mx, d=(15 if not kdest else 4), a=16, b=17,
                   mxcsr=mx, vals=vals))
    # {er} / {sae}: flags suppressed, no #XM even with every exception unmasked
    if sp.rcsae:
        n = 1 if sp.scalar else KL
        for t, rr in enumerate((0, 1, 2, 3)):
            vals = {"b": [R.val(sp.sdom) for _ in range(n)]}
            if sp.sdom == "h":
                vals["b"][0] = 0x7D00 if t % 2 == 0 else 0x0001
            mx = 0x0000 | (((rr + 1) & 3) << 13)
            if sp.rcsae == "er":
                emit(vcase(sp, vl, "{er} rc=%d MXCSR=%X" % (rr, mx), d=18, a=19, b=20, vals=vals, rc=rr, mxcsr=mx,
                           kreg=6 if t == 3 else 0, kval=R.bits(64), z=1 if t == 3 else 0))
            elif t < 3:
                ll = (0, 2, 3)[t]
                emit(vcase(sp, vl, "{sae} L'L=%d MXCSR=%X" % (ll, mx), d=18 if not kdest else 3, a=19, b=20,
                           vals=vals, sae=True, ll=ll, mxcsr=mx))
    # destination = a source
    if not kdest:
        if sp.cplx:
            for t, (dd, aa, bb) in enumerate(((5, 5, 6), (6, 5, 6))):
                ud(sp, "DEST = SRC%d" % (t + 1), reg=dd, vvvv=aa, rm=bb, ll=2)
            ud(sp, "DEST = SRC1 (memory SRC2)", reg=7, vvvv=7, rm=Mem(RSI, 0), ll=2)
            c = vcase(sp, vl, "DEST != SRC1 with memory SRC2", d=8, a=9, b=0, mem=Mem(RSI, 0x40))
            emit(c)
        else:
            if has_a:
                emit(vcase(sp, vl, "dst=src1", d=21, a=21, b=23, kreg=5, kval=R.bits(64)))
            emit(vcase(sp, vl, "dst=src2", d=22, a=21, b=22, kreg=5, kval=R.bits(64), z=1))
            if has_a:
                emit(vcase(sp, vl, "dst=src1=src2", d=23, a=23, b=23))
    elif has_a:
        emit(vcase(sp, vl, "src1=src2", d=1, a=23, b=23))
    # unmasked exceptions -> #XM (one offending lane, active / masked off)
    if sp.excmask and not sp.cplx:
        imm = None
        if sp.imms:
            imm = sp.imms[0] if not sp.name.startswith(("VREDUCE", "VRNDSCALE")) else 0x13
        for fl in (IE, DE, ZE, OE, UE, PE):
            if not sp.excmask & fl:
                continue
            mx = MXCSR_DEFAULT & ~(fl << 7)
            if fl == UE and sp.name.startswith("VRNDSCALE"):
                imm = 0xF1                    # M = 15, round down: tiny inexact results
            hit = find_lane(sp, fl, mx, imm, 0, want_exact_ue=(fl == UE and sp.name[:7] not in ("VRNDSCA",)))
            if hit is None and fl == UE:
                hit = find_lane(sp, fl, mx, imm, 0)
            if hit is None:
                note("%s: no input found for unmasked %s" % (sp.name, fl))
                continue
            n = 1 if sp.scalar else KL
            lanes = [hit] + [find_lane(sp, fl, mx, imm, j, benign=True) for j in range(1, n)]
            if None in lanes:
                raise RuntimeError("%s: no benign lane for flag %d" % (sp.name, fl))
            vals = {"b": [x[2] for x in lanes]}
            if has_a:
                vals["a"] = [x[1] for x in lanes]
            if lay == "fma":
                vals["d"] = [x[0] for x in lanes]
            nm = {IE: "IM", DE: "DM", ZE: "ZM", OE: "OM", UE: "UM", PE: "PM"}[fl]
            emit(vcase(sp, vl, "%s=0 offending lane active -> #XM" % nm, d=(26 if not kdest else 5), a=27, b=28,
                       vals=vals, mxcsr=mx, imm=imm))
            emit(vcase(sp, vl, "%s=0 offending lane masked off" % nm, d=(26 if not kdest else 5), a=27, b=28,
                       vals=vals, mxcsr=mx, imm=imm, kreg=1, kval=R.bits(64) & ~1, z=R.bits(1) if not kdest else 0))
    # #UD matrix
    if not sibling_w(sp) and sp.w != "ig":
        ud(sp, "EVEX.W%d" % (1 - sp.w), w=1 - sp.w, ll=0 if sp.scalar else 2)
    if not sp.scalar:
        ud(sp, "L'L=11b without EVEX.b", ll=3)
    if not sp.rcsae:
        ud(sp, "EVEX.b on the register form", b=1, ll=0 if sp.scalar else 2)
    if not sp.bcst:
        ud(sp, "EVEX.b with a memory operand", b=1, rm=Mem(RSI, 0), ll=0 if sp.scalar else 2,
           reg=1 if not sp.cplx else 1, vvvv=3 if has_a else None)
    ud(sp, "EVEX.z with aaa=000b", z=1, aaa=0, ll=0 if sp.scalar else 2, reg=1 if not kdest else 1)
    if kdest:
        ud(sp, "EVEX.z on a k destination", z=1, aaa=2, reg=1)
        ud(sp, "ModRM.reg k with EVEX.R'=0", reg=17)
        ud(sp, "ModRM.reg k with EVEX.R=0", reg=9)
    if not has_a:
        ud(sp, "vvvv != 1111b", vvvv=5, ll=0 if sp.scalar else 2)
        ud(sp, "EVEX.V'=0 (vvvv unused)", p2_vp=0, ll=0 if sp.scalar else 2)
    # disp8*N +-1 and the broadcast N
    n_full = sp.sesz if sp.scalar else KL * sp.sesz
    for vl2 in ((16,) if sp.scalar else (16, 64)):
        nf = sp.sesz if sp.scalar else sp.kl(vl2) * sp.sesz
        for s in (1, -1):
            emit(vcase(sp, vl2, "disp8*N: disp8=%d N=%d" % (s, nf), d=(2 if not kdest else 3), a=3, b=0,
                       mem=Mem(RSI, s * nf)))
        if sp.bcst:
            emit(vcase(sp, vl2, "disp8*N bcst: disp8=-1 N=%d" % sp.sesz, d=(2 if not kdest else 3), a=3, b=0,
                       mem=Mem(RSI, -sp.sesz), bcst=1))
    _ = n_full, dreg, first


# ---- VCOMISH / VUCOMISH ------------------------------------------------------------------
RF_MASK = 0x8D5          # OF SF ZF AF PF CF


def comi_case(sp, title, a, b, mem=None, mxcsr=MXCSR_DEFAULT, sae=False, rflags=None, ll=None, ra=1, rb=2):
    c = Case("%s %s" % (sp.name, title))
    img_a = bytearray(R.bytes(64))
    img_a[0:2] = a.to_bytes(2, "little")
    c.set_zmm(ra, bytes(img_a))
    if mem is None:
        if rb != ra:
            img_b = bytearray(R.bytes(64))
            img_b[0:2] = b.to_bytes(2, "little")
            c.set_zmm(rb, bytes(img_b))
        else:
            b = a
    else:
        c.mem[MEM_RSI + mem.disp] = b.to_bytes(2, "little")
    if rflags is None:
        rflags = R.choice([0x202, 0xAD7, 0x203, 0x2C6])
    if rflags != 0x202:
        c.inp.append("rflags=0x%X" % rflags)
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    env = Env(mxcsr, sae=sae)
    zf, pf, cf, fl = fp_comi(a, b, sp.name == "VCOMISH")
    if not finish_fp(c, mxcsr, fl, env):
        nf = (rflags & ~RF_MASK) | (zf << 6) | (pf << 2) | cf
        if nf != rflags:
            c.exp.append("rflags=0x%X" % nf)
    if ll is None:
        ll = R.choice([0, 0, 1, 2])
    c.code = evex(sp.mmm, sp.pp, 0, sp.opc, ra, mem if mem is not None else rb, ll=ll, b=1 if sae else 0, n=2)
    return c


def gen_comi(sp, first):
    note("%s (EVEX.LLIG.NP.MAP5.W0 %02X /r) {sae}" % (sp.name, sp.opc))
    for i, a in enumerate(SPECIAL16):
        b = SPECIAL16[(i * 5 + 3) % len(SPECIAL16)]
        emit(comi_case(sp, "specials %04X,%04X" % (a, b), a, b, ra=R.randint(0, 31), rb=R.randint(0, 31)))
    for a, b in ((0x3C00, 0x3C00), (0x0000, 0x8000), (0x0001, 0x0002), (0x7BFF, 0x7C00), (0xFC00, 0xFBFF),
                 (0x3C00, 0x3C01), (0x8001, 0x0001)):
        emit(comi_case(sp, "order %04X,%04X" % (a, b), a, b, ra=3, rb=4))
        emit(comi_case(sp, "order %04X,%04X swapped" % (b, a), b, a, ra=4, rb=3))
    for t in range(4):
        emit(comi_case(sp, "random", R.h(), R.h(), ra=R.randint(0, 31), rb=R.randint(0, 31)))
    emit(comi_case(sp, "same register", 0x3555, 0, ra=7, rb=7))
    emit(comi_case(sp, "mem", R.h(), R.h(), mem=Mem(RSI, 2 * R.choice([1, -1, 64]))))
    emit(comi_case(sp, "mem QNaN", 0x3C00, 0x7E00, mem=Mem(RSI, 2)))
    for a, b, nm in ((0x7E00, 0x3C00, "QNaN"), (0x3C00, 0x7D00, "SNaN"), (0x0001, 0x3C00, "denormal")):
        for mx in (0x1F80 & ~0x80, 0x1F80 & ~0x100):
            emit(comi_case(sp, "%s MXCSR=%X" % (nm, mx), a, b, mxcsr=mx))
        emit(comi_case(sp, "%s {sae} all unmasked" % nm, a, b, mxcsr=0x0000, sae=True, ll=R.choice([0, 3])))
    ud(sp, "EVEX.W1", w=1)
    ud(sp, "aaa != 000b", aaa=1)
    ud(sp, "EVEX.z", z=1)
    ud(sp, "vvvv != 1111b", vvvv=6)
    ud(sp, "EVEX.V'=0", p2_vp=0)
    ud(sp, "EVEX.b with memory", b=1, rm=Mem(RSI, 0))


# ---- GPR destination: VCVT[T]SH2[U]SI ------------------------------------------------------
def gdst_fn(sp):
    trunc = sp.name.startswith("VCVTT")
    signed = not sp.name.endswith("USI")
    bits = 64 if sp.w else 32
    return lambda env, x: cvt_h2int(x, bits, signed, env, trunc)


def gdst_case(sp, title, x, g=0, rx=1, mem=None, mxcsr=MXCSR_DEFAULT, rc=None, sae=False, ll=None):
    c = Case("%s r%d %s" % (sp.name, 64 if sp.w else 32, title))
    if mem is None:
        img = bytearray(R.bytes(64))
        img[0:2] = x.to_bytes(2, "little")
        c.set_zmm(rx, bytes(img))
    else:
        c.mem[MEM_RSI + mem.disp] = x.to_bytes(2, "little")
    old = R.bits(64)
    c.inp.append("%s=0x%X" % (GPRS[g], old))
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    env = Env(mxcsr, rc=rc, sae=sae)
    r, fl = gdst_fn(sp)(env, x)
    if not finish_fp(c, mxcsr, fl, env):
        c.exp.append("%s=0x%X" % (GPRS[g], r))
    if ll is None:
        ll = rc if rc is not None else R.choice([0, 0, 1, 2])
    c.code = evex(sp.mmm, sp.pp, sp.w, sp.opc, g, mem if mem is not None else rx, ll=ll,
                  b=1 if (rc is not None or sae) else 0, n=2)
    return c


def gen_gdst(sp, first):
    note("%s r%d (EVEX.LLIG.F3.MAP5.W%d %02X /r) %s" % (sp.name, 64 if sp.w else 32, sp.w, sp.opc, sp.rcsae))
    xs = SPECIAL16 + [0x7800, 0x77FF, 0xF800, 0xF7FF, 0x3800, 0xB800, 0x3E00, 0xBE00, 0x4100, 0xC100, 0x3A00,
                      0xBA00, 0x7BFF, 0xFBFF, 0xBBFF, 0x0400, 0x8400]
    for i, x in enumerate(xs):
        emit(gdst_case(sp, "special %04X" % x, x, g=SAFE_GPR[i % len(SAFE_GPR)], rx=R.randint(0, 31)))
    for t in range(4):
        emit(gdst_case(sp, "random", R.h(), g=R.choice(SAFE_GPR), rx=R.randint(0, 31)))
    emit(gdst_case(sp, "mem", R.h(), g=3, mem=Mem(RSI, 2 * R.choice([1, -1, 100]))))
    if not sp.name.startswith("VCVTT"):
        for rcm in (1, 2, 3):
            for x in (0x3E00, 0xBE00, 0x4100, 0xC100, R.h()):
                emit(gdst_case(sp, "MXCSR.RC=%d" % rcm, x, g=R.choice(SAFE_GPR), mxcsr=0x1F80 | (rcm << 13)))
        for rr in range(4):
            emit(gdst_case(sp, "{er} rc=%d all unmasked" % rr, R.choice([0x3E00, 0xC100, 0x7D00, 0x7C00]),
                           g=R.choice(SAFE_GPR), rc=rr, mxcsr=0x0000 | (((rr + 1) & 3) << 13)))
    else:
        for x in (0x3E00, 0xBE00, 0xC100):
            emit(gdst_case(sp, "MXCSR.RC=RU (ignored, truncation)", x, g=1, mxcsr=0x5F80))
        for ll in (0, 3):
            emit(gdst_case(sp, "{sae} L'L=%d all unmasked" % ll, R.choice([0x3E00, 0x7D00, 0xFC00]), g=2,
                           sae=True, mxcsr=0x0000, ll=ll))
    for x, mx, nm in ((0x7E00, 0x1F00, "IM=0 QNaN"), (0xFC00, 0x1F00, "IM=0 -INF"), (0x3E00, 0x1F80 & ~0x1000, "PM=0 inexact"),
                      (0x3C00, 0x1F80 & ~0x1000, "PM=0 exact"), (0x0001, 0x1F80 & ~0x100, "DM=0 denormal (no DE)"),
                      (0xBC00, 0x1F00, "IM=0 -1.0")):
        emit(gdst_case(sp, nm, x, g=R.choice(SAFE_GPR), mxcsr=mx))
    ud(sp, "ModRM.reg GPR with EVEX.R'=0", reg=16 + 1, rm=2)
    ud(sp, "aaa != 000b", aaa=2)
    ud(sp, "EVEX.z", z=1)
    ud(sp, "vvvv != 1111b", vvvv=4)
    ud(sp, "EVEX.V'=0", p2_vp=0)
    ud(sp, "EVEX.b with memory", b=1, rm=Mem(RSI, 0))


# ---- GPR source: VCVT[U]SI2SH ----------------------------------------------------------------
def gsrc_case(sp, title, i, d=1, a=2, g=0, mem=None, mxcsr=MXCSR_DEFAULT, rc=None, ll=None, rm_x=False):
    bits = 64 if sp.w else 32
    signed = sp.name == "VCVTSI2SH"
    c = Case("%s r%d %s" % (sp.name, bits, title))
    imgs = {}
    for r in (a, d):
        if r not in imgs:
            imgs[r] = bytearray(R.bytes(64))
            c.set_zmm(r, bytes(imgs[r]))
    if mem is None:
        full = (R.bits(64) & ~((1 << bits) - 1)) | (i & ((1 << bits) - 1))
        c.inp.append("%s=0x%X" % (GPRS[g], full))
    else:
        c.mem[MEM_RSI + mem.disp] = (i & ((1 << bits) - 1)).to_bytes(bits // 8, "little")
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    env = Env(mxcsr, rc=rc)
    r, fl = cvt_int2h(i, bits, signed, env)
    assert fl & ~(OE | PE) == 0
    if not finish_fp(c, mxcsr, fl, env):
        out = bytearray(imgs[a])
        out[0:2] = r.to_bytes(2, "little")
        out[16:] = bytes(48)
        c.exp.append("zmm%d=%s" % (d, hexs(bytes(out))))
    if ll is None:
        ll = rc if rc is not None else R.choice([0, 0, 1, 2])
    rm = mem if mem is not None else (g + 16 if rm_x else g)
    c.code = evex(sp.mmm, sp.pp, sp.w, sp.opc, d, rm, vvvv=a, ll=ll, b=1 if rc is not None else 0,
                  n=bits // 8)
    return c


def gen_gsrc(sp, first):
    bits = 64 if sp.w else 32
    dom = ("q" if sp.w else "dw") if sp.name == "VCVTSI2SH" else ("uq" if sp.w else "udw")
    note("%s r%d (EVEX.LLIG.F3.MAP5.W%d %02X /r) {er}" % (sp.name, bits, sp.w, sp.opc))
    for t, i in enumerate(SPECIALS[dom]):
        emit(gsrc_case(sp, "special %X" % i, i, d=R.randint(0, 31), a=R.randint(0, 31), g=SAFE_GPR[t % len(SAFE_GPR)]))
    for t in range(4):
        emit(gsrc_case(sp, "random", R.val(dom), d=R.randint(0, 31), a=R.randint(0, 31), g=R.choice(SAFE_GPR)))
    emit(gsrc_case(sp, "dst=src1", R.val(dom), d=9, a=9, g=1))
    emit(gsrc_case(sp, "mem", R.val(dom), d=3, a=4, mem=Mem(RSI, (bits // 8) * R.choice([1, -1, 7]))))
    emit(gsrc_case(sp, "r/m GPR with EVEX.X=0 (ignored)", R.val(dom), d=5, a=6, g=1, rm_x=True))
    for rcm in (1, 2, 3):
        for i in (2049, 65520, (-2049) & ((1 << bits) - 1), R.val(dom)):
            emit(gsrc_case(sp, "MXCSR.RC=%d" % rcm, i, d=7, a=8, g=2, mxcsr=0x1F80 | (rcm << 13)))
    for rr in range(4):
        emit(gsrc_case(sp, "{er} rc=%d all unmasked" % rr, R.choice([2049, 65520, 4097, 70000]), d=10, a=11, g=3,
                       rc=rr, mxcsr=((rr + 1) & 3) << 13))
    for i, mx, nm in ((65520, 0x1F80 & ~0x400, "OM=0 overflow"), (2049, 0x1F80 & ~0x1000, "PM=0 inexact"),
                      (2048, 0x1F80 & ~0x1000, "PM=0 exact"), (65504, 0x1F80 & ~0x400, "OM=0 max exact")):
        emit(gsrc_case(sp, nm, i, d=12, a=13, g=5, mxcsr=mx))
    ud(sp, "aaa != 000b", aaa=1)
    ud(sp, "EVEX.z", z=1)
    ud(sp, "EVEX.b with memory", b=1, rm=Mem(RSI, 0))


# ---- VMOVSH / VMOVW --------------------------------------------------------------------------
def gen_movsh(sp, first):
    note("VMOVSH %s (EVEX.LLIG.F3.MAP5.W0 %02X /r)" % (sp.lay, sp.opc))
    for t, (kreg, z) in enumerate(((0, 0), (2, 0), (2, 1), (3, 0), (3, 1))):
        kval = None
        if kreg:
            kval = (R.bits(64) | 1) if kreg == 2 else (R.bits(64) & ~1)
        c = Case("VMOVSH %s k%d z%d" % (sp.lay, kreg, z))
        if kreg:
            c.k[kreg] = kval
        act = kval is None or (kval & 1)
        ll = R.choice([0, 1, 2])
        if sp.lay == "movsh_ld":
            d = R.randint(0, 31)
            old = R.bytes(64)
            c.set_zmm(d, old)
            x = R.h()
            disp = 2 * R.choice([1, -1, 9])
            c.mem[MEM_RSI + disp] = x.to_bytes(2, "little")
            e0 = x.to_bytes(2, "little") if act else (bytes(2) if z else old[:2])
            c.exp.append("zmm%d=%s" % (d, hexs(e0 + bytes(62))))
            c.code = evex(5, 2, 0, 0x10, d, Mem(RSI, disp), ll=ll, z=z, aaa=kreg, n=2)
        elif sp.lay == "movsh_st":
            if z:
                continue
            s = R.randint(0, 31)
            c.set_zmm(s, R.bytes(64))
            disp = 2 * R.choice([1, -1, 9])
            old = R.bytes(2)
            c.mem[MEM_RSI + disp] = old
            if act:
                c.exp.append("m+0x%X=%s" % (MEM_RSI + disp, hexs(c.zmm[s][:2])))
            c.code = evex(5, 2, 0, 0x11, s, Mem(RSI, disp), ll=ll, aaa=kreg, n=2)
        else:
            d, s1, s2 = R.randint(0, 31), R.randint(0, 31), R.randint(0, 31)
            imgs = {}
            for r in (s2, s1, d):
                if r not in imgs:
                    imgs[r] = R.bytes(64)
                    c.set_zmm(r, imgs[r])
            e0 = imgs[s2][:2] if act else (bytes(2) if z else imgs[d][:2])
            c.exp.append("zmm%d=%s" % (d, hexs(e0 + imgs[s1][2:16] + bytes(48))))
            if sp.lay == "movsh_rr":
                c.code = evex(5, 2, 0, 0x10, d, s2, vvvv=s1, ll=ll, z=z, aaa=kreg)
            else:
                c.code = evex(5, 2, 0, 0x11, s2, d, vvvv=s1, ll=ll, z=z, aaa=kreg)
        emit(c)
    if sp.lay in ("movsh_rr", "movsh_rr11"):
        for nm, (d, s1, s2) in (("dst=src1", (4, 4, 5)), ("dst=src2", (5, 4, 5)), ("all same", (6, 6, 6))):
            c = Case("VMOVSH %s %s" % (sp.lay, nm))
            imgs = {}
            for r in (s2, s1, d):
                if r not in imgs:
                    imgs[r] = R.bytes(64)
                    c.set_zmm(r, imgs[r])
            c.exp.append("zmm%d=%s" % (d, hexs(imgs[s2][:2] + imgs[s1][2:16] + bytes(48))))
            c.code = evex(5, 2, 0, sp.opc, d if sp.lay == "movsh_rr" else s2, s2 if sp.lay == "movsh_rr" else d,
                          vvvv=s1)
            emit(c)
        ud(sp, "EVEX.b on the register form", b=1)
    else:
        ud(sp, "vvvv != 1111b", rm=Mem(RSI, 0), vvvv=3)
        ud(sp, "EVEX.V'=0", rm=Mem(RSI, 0), p2_vp=0)
        ud(sp, "EVEX.b with memory", rm=Mem(RSI, 0), b=1)
        ud(sp, "EVEX.W1", rm=Mem(RSI, 0), w=1)
        if sp.lay == "movsh_st":
            ud(sp, "EVEX.z with a memory destination", rm=Mem(RSI, 0), z=1, aaa=1)
            # masked store next to the unmapped page: no access, no fault
            c = Case("VMOVSH store k=0 on the unmapped page (no fault)")
            c.set_zmm(1, R.bytes(64))
            c.k[1] = 0
            c.code = evex(5, 2, 0, 0x11, 1, Mem(RSI, 0x10000 - MEM_RSI, disp32=True), aaa=1)
            emit(c)
            c = Case("VMOVSH store k=1 on the unmapped page #PF")
            c.set_zmm(1, R.bytes(64))
            c.k[1] = 1
            c.code = evex(5, 2, 0, 0x11, 1, Mem(RSI, 0x10000 - MEM_RSI, disp32=True), aaa=1)
            c.fault = "#PF"
            emit(c)
        else:
            c = Case("VMOVSH load k=0 on the unmapped page (no fault)")
            c.set_zmm(1, R.bytes(64))
            c.k[1] = 0
            old = c.zmm[1]
            c.code = evex(5, 2, 0, 0x10, 1, Mem(RSI, 0x10000 - MEM_RSI, disp32=True), aaa=1)
            c.exp.append("zmm1=%s" % hexs(old[:2] + bytes(62)))
            emit(c)
    ud(sp, "EVEX.z with aaa=000b", z=1, aaa=0, rm=Mem(RSI, 0) if sp.lay in ("movsh_ld", "movsh_st") else 2)


def gen_movw(sp, first):
    note("VMOVW %s (EVEX.128.66.MAP5.WIG %02X /r)" % (sp.lay, sp.opc))
    for t in range(8):
        w = t & 1
        c = Case("VMOVW %s W%d" % (sp.lay, w))
        x = R.bits(64)
        if sp.lay == "movw_ld":
            d = R.randint(0, 31)
            c.set_zmm(d, R.bytes(64))
            if t < 4:
                g = R.choice(SAFE_GPR)
                c.inp.append("%s=0x%X" % (GPRS[g], x))
                rm = g + (16 if t == 3 else 0)          # EVEX.X ignored for a GPR r/m
            else:
                disp = 2 * R.choice([1, -1, 33])
                c.mem[MEM_RSI + disp] = (x & 0xFFFF).to_bytes(2, "little")
                rm = Mem(RSI, disp)
            c.exp.append("zmm%d=%s" % (d, hexs((x & 0xFFFF).to_bytes(2, "little") + bytes(62))))
            c.code = evex(5, 1, w, 0x6E, d, rm, ll=0, n=2)
        else:
            s = R.randint(0, 31)
            c.set_zmm(s, R.bytes(64))
            word = c.zmm[s][:2]
            if t < 4:
                g = R.choice(SAFE_GPR)
                c.inp.append("%s=0x%X" % (GPRS[g], x))
                c.exp.append("%s=0x%X" % (GPRS[g], int.from_bytes(word, "little")))
                rm = g + (16 if t == 3 else 0)
            else:
                disp = 2 * R.choice([1, -1, 33])
                c.mem[MEM_RSI + disp] = R.bytes(4)
                c.exp.append("m+0x%X=%s" % (MEM_RSI + disp, hexs(word)))
                rm = Mem(RSI, disp)
            c.code = evex(5, 1, w, 0x7E, s, rm, ll=0, n=2)
        emit(c)
    ud(sp, "EVEX.L'L=01b", ll=1)
    ud(sp, "EVEX.L'L=10b (memory)", ll=2, rm=Mem(RSI, 0))
    ud(sp, "aaa != 000b", aaa=1)
    ud(sp, "EVEX.z", z=1)
    ud(sp, "vvvv != 1111b", vvvv=2)
    ud(sp, "EVEX.V'=0", p2_vp=0)
    ud(sp, "EVEX.b (register)", b=1)
    ud(sp, "EVEX.b (memory)", b=1, rm=Mem(RSI, 0))


# ---- fault suppression -----------------------------------------------------------------------
def gen_fault_supp(sp):
    """packed load with the upper elements on the unmapped page MEM + 0x10000; scalar (E3/E10)
    load with mask bit 0 clear on the unmapped page"""
    if sp.lay not in ("rvm", "rm", "fma"):
        return
    imm0 = sp.imms[0] if sp.imms else None
    if sp.scalar:
        disp = 0x10000 - MEM_RSI
        c = vcase(sp, 16, "k bit0=0, m%d on the unmapped page (fault suppressed)" % (8 * sp.sesz), d=2, a=3, b=0,
                  mem=Mem(RSI, disp, disp32=True), kreg=1, kval=R.bits(64) & ~1, z=R.bits(1))
        c.mem = {}
        emit(c)
        c = Case("%s k bit0=1, m%d on the unmapped page -> #PF" % (sp.name, 8 * sp.sesz))
        c.set_zmm(2, R.bytes(64))
        c.set_zmm(3, R.bytes(64))
        c.k[1] = R.bits(64) | 1
        c.code = evex(sp.mmm, sp.pp, sp.wbit(), sp.opc, 2, Mem(RSI, disp, disp32=True), vvvv=3, ll=0, aaa=1, imm=imm0)
        c.fault = "#PF"
        emit(c)
        return
    KL = sp.kl(64)
    nmap = KL // 2
    base = 0x10000 - MEM_RSI - nmap * sp.sesz
    vals = {"b": [R.val(sp.sdom) for _ in range(KL)]}
    kv = (1 << nmap) - 1
    c = vcase(sp, 64, "fault suppression: masked-off elements on the unmapped page", d=2, a=3, b=0,
              mem=Mem(RSI, base, disp32=True), kreg=1, kval=kv, z=1, vals=vals)
    c.mem = {k: v[:nmap * sp.sesz] for k, v in c.mem.items()}
    emit(c)
    c = Case("%s VL512 element %d on the unmapped page active -> #PF" % (sp.name, nmap))
    c.set_zmm(2, R.bytes(64))
    c.set_zmm(3, R.bytes(64))
    c.k[1] = kv | (1 << nmap)
    c.code = evex(sp.mmm, sp.pp, sp.wbit(), sp.opc, 2, Mem(RSI, base, disp32=True),
                  vvvv=3 if sp.lay != "rm" else None, ll=2, z=1, aaa=1, imm=imm0)
    c.fault = "#PF"
    emit(c)
    if sp.bcst:
        c = Case("%s {1toN} k=0 on the unmapped page (no access)" % sp.name)
        c.set_zmm(2, R.bytes(64))
        c.set_zmm(3, R.bytes(64))
        c.k[6] = 0
        c.code = evex(sp.mmm, sp.pp, sp.wbit(), sp.opc, 2, Mem(RSI, 0x10000 - MEM_RSI, disp32=True),
                      vvvv=3 if sp.lay != "rm" else None, ll=2, b=1, aaa=6, imm=imm0)
        # no element written (merge, k = 0), but DEST[MAXVL-1:KL*element] := 0 still applies
        # (VL/2 for VCVTPS2PHX / VCVT(U)DQ2PH, VL/4 for VCVTPD2PH / VCVT(U)QQ2PH)
        nb = KL * sp.desz
        c.exp.append("zmm2=%s" % hexs(c.zmm[2][:nb] + bytes(64 - nb)))
        emit(c)


FAULT_SUPP_FORMS = ("VADDPH", "VCVTPH2PD", "VFMULCPH", "VCVTPS2PHX", "VFMADD231PH", "VCVTPH2QQ", "VRCPPH",
                    "VADDSH", "VFMADD231SH", "VFMULCSH", "VCVTSS2SH", "VCVTSD2SH", "VRCPSH", "VGETMANTSH")


def gen_group(name):
    global R
    R = Rng(zlib.crc32(name.encode()) ^ 0xF16)
    del OUT[:]
    note("--- %s" % name)
    for i, sp in enumerate([f for f in FORMS if f.name == name]):
        sp.rng = R
        if sp.lay in ("rvm", "rm", "fma", "krvm", "krm"):
            gen_vec(sp, i == 0)
            if name in FAULT_SUPP_FORMS:
                gen_fault_supp(sp)
        elif sp.lay == "comi":
            gen_comi(sp, i == 0)
        elif sp.lay == "gdst":
            gen_gdst(sp, i == 0)
        elif sp.lay == "gsrc":
            gen_gsrc(sp, i == 0)
        elif sp.lay.startswith("movsh"):
            gen_movsh(sp, i == 0)
        elif sp.lay.startswith("movw"):
            gen_movw(sp, i == 0)
    return list(OUT)


def gen_all(only=None):
    groups = []
    for name in all_mnemonics():
        if name in EXCLUDE:
            continue
        if only and name not in only:
            continue
        groups.append((name, gen_group(name)))
    return groups


def write_cases(out, groups):
    out.write("# AVX512-FP16: expected values from the independent SDM model\n")
    out.write("# Emulator/tools/isa/ref_evex_fp16.py --cases (regenerate, do not edit). The i5-13600K has no\n")
    out.write("# AVX512-FP16: expected-value cases only, run with AVX-512 (incl. FP16) enabled:\n")
    out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_fp16.txt --avx512 --expect-only\n")
    out.write("# RSI = R14 = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).\n")
    out.write("# One '# --- MNEMONIC' header per instruction (filter with --only / EXCLUDE).\n")
    if EXCLUDE:
        out.write("# excluded: %s\n" % ", ".join(EXCLUDE))
    for name, lines in groups:
        for c in lines:
            if isinstance(c, str):
                out.write(c + "\n")
            else:
                out.write("# " + c.title + "\n")
                out.write(c.line() + "\n")


# ---------------------------------------------------------------------------------------
# self test
# ---------------------------------------------------------------------------------------
def selftest():
    ok = [True]

    def chk(name, got, want):
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok[0] = False

    E = Env()
    ERD, ERU, ERZ = Env(0x3F80), Env(0x5F80), Env(0x7F80)
    EUM = Env(0x1F80 & ~UM_BIT)
    # encodings: VADDPH zmm1, zmm2, zmm3 = 62 F5 6C 48 58 CB; VFMADD132PH zmm1, zmm2, zmm3 = 62 F6 6D 48 98 CB
    chk("enc vaddph", evex(5, 0, 0, 0x58, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF5, 0x6C, 0x48, 0x58, 0xCB]))
    chk("enc vfmadd132ph", evex(6, 1, 0, 0x98, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF6, 0x6D, 0x48, 0x98, 0xCB]))
    chk("enc vcmpph imm", evex(3, 0, 0, 0xC2, 1, 3, vvvv=2, ll=0, imm=4),
        bytes([0x62, 0xF3, 0x6C, 0x08, 0xC2, 0xCB, 0x04]))
    # rounding (1 + 2^-11 is a tie)
    one_t = 1 + TWO ** -11
    chk("rne tie", round_to(one_t, F16, 0), (0x3C00, PE))
    chk("ru tie", round_to(one_t, F16, 2), (0x3C01, PE))
    chk("rd neg", round_to(-(1 + TWO ** -12), F16, 1), (0xBC01, PE))
    chk("rz neg", round_to(-(1 + 3 * TWO ** -12), F16, 3), (0xBC00, PE))
    chk("rne 1+3*2^-11", round_to(1 + 3 * TWO ** -11, F16, 0), (0x3C02, PE))
    # overflow per mode
    # 65520 overflows only when it rounds up (RNE tie to even / away from zero); RZ, RD keep 65504
    for rc, want in ((0, (0x7C00, OE | PE)), (1, (0x7BFF, PE)), (2, (0x7C00, OE | PE)), (3, (0x7BFF, PE))):
        chk("+65520 rc%d" % rc, round_to(Fraction(65520), F16, rc), want)
    for rc, want in ((0, (0xFC00, OE | PE)), (1, (0xFC00, OE | PE)), (2, (0xFBFF, PE)), (3, (0xFBFF, PE))):
        chk("-65520 rc%d" % rc, round_to(Fraction(-65520), F16, rc), want)
    for rc, want in ((0, 0x7C00), (1, 0x7BFF), (2, 0x7C00), (3, 0x7BFF)):
        chk("ovf +65536 rc%d" % rc, round_to(Fraction(65536), F16, rc), (want, OE | PE))
    for rc, want in ((0, 0xFC00), (1, 0xFC00), (2, 0xFBFF), (3, 0xFBFF)):
        chk("ovf -65536 rc%d" % rc, round_to(Fraction(-65536), F16, rc), (want, OE | PE))
    chk("65519 rne", round_to(Fraction(65519), F16, 0), (0x7BFF, PE))
    chk("65519 ru", round_to(Fraction(65519), F16, 2), (0x7C00, OE | PE))
    # tiny / underflow
    chk("2^-25 rne", round_to(TWO ** -25, F16, 0), (0x0000, UE | PE))
    chk("2^-25 ru", round_to(TWO ** -25, F16, 2), (0x0001, UE | PE))
    chk("3*2^-26 rne", round_to(3 * TWO ** -26, F16, 0), (0x0001, UE | PE))
    chk("exact tiny masked", round_to(TWO ** -24, F16, 0), (0x0001, 0))
    chk("exact tiny unmasked", round_to(TWO ** -24, F16, 0, um=True), (0x0001, UE))
    chk("tiny after rounding", round_to(TWO ** -14 * (1 - TWO ** -12), F16, 0), (0x0400, PE))
    # unmasked UM / OM: PE from the rounding with unbounded exponent (i5-13600K F16C)
    chk("UM=0 3*2^-26 (exact unbounded)", round_to(3 * TWO ** -26, F16, 0, um=True)[1], UE)
    chk("UM=0 2^-25(1+2^-23) rd", round_to(TWO ** -25 * (1 + TWO ** -23), F16, 1, um=True)[1], UE | PE)
    chk("UM=0 2^-20(1+2^-10)", round_to(TWO ** -20 * (1 + TWO ** -10), F16, 0, um=True)[1], UE)
    chk("UM=1 2^-20(1+2^-10)", round_to(TWO ** -20 * (1 + TWO ** -10), F16, 0)[1], UE | PE)
    chk("OM=0 65536 exact", round_to(Fraction(65536), F16, 0, om=True), (0x7C00, OE))
    chk("OM=0 65520 inexact", round_to(Fraction(65520), F16, 0, om=True), (0x7C00, OE | PE))
    chk("OM=1 65536", round_to(Fraction(65536), F16, 3), (0x7BFF, OE | PE))
    chk("tiny before rounding rz", round_to(TWO ** -14 * (1 - TWO ** -12), F16, 3), (0x03FF, UE | PE))
    # arithmetic
    chk("1+1", fp_arith("add", 0x3C00, 0x3C00, E), (0x4000, 0))
    chk("max+max", fp_arith("add", 0x7BFF, 0x7BFF, E), (0x7C00, OE | PE))
    chk("max+max rz", fp_arith("add", 0x7BFF, 0x7BFF, ERZ), (0x7BFF, OE | PE))
    chk("min normal * 0.5 exact", fp_arith("mul", 0x0400, 0x3800, E), (0x0200, 0))
    chk("min normal * 0.5 exact UM=0", fp_arith("mul", 0x0400, 0x3800, EUM), (0x0200, UE))
    chk("0x0401 * 0.5 tie", fp_arith("mul", 0x0401, 0x3800, E), (0x0200, UE | PE))
    chk("denorm + denorm", fp_arith("add", 0x0001, 0x0001, Env(0x1F80 | DAZ | FTZ)), (0x0002, DE))
    chk("x - x rd", fp_arith("sub", 0x3C00, 0x3C00, ERD), (0x8000, 0))
    chk("-0 + -0", fp_arith("add", 0x8000, 0x8000, E), (0x8000, 0))
    chk("inf - inf", fp_arith("sub", 0x7C00, 0x7C00, E), (0xFE00, IE))
    chk("snan + qnan", fp_arith("add", 0x7D00, 0xFE01, E), (0x7F00, IE))
    chk("1 + qnan", fp_arith("add", 0x3C00, 0xFE01, E), (0xFE01, 0))
    chk("1 / 0", fp_arith("div", 0x3C00, 0x0000, E), (0x7C00, ZE))
    chk("denorm / 0", fp_arith("div", 0x8001, 0x0000, E), (0xFC00, ZE))
    chk("0 / 0", fp_arith("div", 0x0000, 0x8000, E), (0xFE00, IE))
    chk("1 / 3", fp_arith("div", 0x3C00, 0x4200, E), (0x3555, PE))
    chk("sqrt 2", fp_sqrt(0x4000, E), (0x3DA8, PE))
    chk("sqrt 4", fp_sqrt(0x4400, E), (0x4000, 0))
    chk("sqrt -0", fp_sqrt(0x8000, E), (0x8000, 0))
    chk("sqrt -denorm", fp_sqrt(0x8001, E), (0xFE00, IE))
    chk("sqrt denorm", fp_sqrt(0x0001, E), (0x0C00, DE))
    chk("max nan", fp_minmax("max", 0x7E00, 0x3C00), (0x3C00, IE))
    chk("max snan src2 unchanged", fp_minmax("max", 0x3C00, 0x7D00), (0x7D00, IE))
    chk("min 0,-0", fp_minmax("min", 0x0000, 0x8000), (0x8000, 0))
    chk("max -inf,denorm", fp_minmax("max", 0xFC00, 0x0001), (0x0001, DE))
    # FMA zero sign (Table 14-16) and NaN rules (Table 14-17)
    chk("fma +0*1 + -0 rne", fma16(0x0000, 0x3C00, 0x8000, 0, 0, E), (0x0000, 0))
    chk("fma +0*1 + -0 rd", fma16(0x0000, 0x3C00, 0x8000, 0, 0, ERD), (0x8000, 0))
    chk("fma -0*1 + -0", fma16(0x8000, 0x3C00, 0x8000, 0, 0, E), (0x8000, 0))
    chk("fma +0*1 - +0", fma16(0x0000, 0x3C00, 0x0000, 0, 1, E), (0x0000, 0))
    chk("fma -(+0*1) - (+0)", fma16(0x0000, 0x3C00, 0x0000, 1, 1, E), (0x8000, 0))
    chk("fma F*1 - F rd", fma16(0x4200, 0x3C00, 0x4200, 0, 1, ERD), (0x8000, 0))
    chk("fma F*1 - F ru", fma16(0x4200, 0x3C00, 0x4200, 0, 1, ERU), (0x0000, 0))
    chk("fma 0*inf + qnan", fma16(0x0000, 0x7C00, 0x7E01, 0, 0, E), (0x7E01, 0))
    chk("fma 0*inf + snan", fma16(0x0000, 0x7C00, 0x7D01, 0, 0, E), (0x7F01, IE))
    chk("fma 0*inf + 1", fma16(0x0000, 0x7C00, 0x3C00, 0, 0, E), (0xFE00, IE))
    chk("fma nan order", fma16(0x3C00, 0xFE02, 0x7E01, 1, 1, E), (0xFE02, 0))
    chk("fma inf - inf", fma16(0x7C00, 0x3C00, 0x7C00, 0, 1, E), (0xFE00, IE))
    chk("fma fused", fma16(0x3C01, 0x3C01, 0xBC00, 0, 0, E), (0x1800, PE))   # (1+2^-10)^2 - 1 = 2^-9 (1 + 2^-11): tie
    chk("fma fused2", fma16(0x3C01, 0x3C01, 0xBC02, 0, 0, E), (0x0010, 0))   # exactly 2^-20 (denormal, exact)
    # complex: (1+2i)(3+4i) = -5+10i; conjugate (1+2i)(3-4i) = 11+2i
    a = 0x3C00 | (0x4000 << 16)
    b = 0x4200 | (0x4400 << 16)
    chk("vfmulcph", cplx(a, b, 0, 0, 0, Env(masked_all=True)), (0xC500 | (0x4900 << 16), 0))
    chk("vfcmulcph", cplx(a, b, 0, 1, 0, Env(masked_all=True)), (0x4980 | (0x4000 << 16), 0))
    chk("vfmaddcph", cplx(a, b, 0x3C00 | (0x3C00 << 16), 0, 1, Env(masked_all=True)),
        (0xC400 | (0x4980 << 16), 0))
    # conversions
    chk("ps2ph 65520", cvt_narrow(f32b(65520.0), F32, E, False), (0x7C00, OE | PE))
    chk("ps2ph 65519.99", cvt_narrow(f32b(65520.0) - 1, F32, E, False), (0x7BFF, PE))
    chk("ps2ph fp32 denorm", cvt_narrow(0x00000001, F32, E, False), (0x0000, DE | UE | PE))
    chk("ps2ph fp32 denorm ru", cvt_narrow(0x00000001, F32, ERU, False), (0x0001, DE | UE | PE))
    chk("ps2ph fp32 denorm daz", cvt_narrow(0x80000001, F32, E, True), (0x8000, 0))
    chk("ps2ph snan", cvt_narrow(0x7F800001, F32, E, False), (0x7E00, IE))
    chk("ps2ph qnan payload", cvt_narrow(0xFFC02000, F32, E, False), (0xFE01, 0))
    chk("pd2ph snan payload", cvt_narrow(0x7FF4000000000000, F64, E, False), (0x7F00, IE))
    chk("ps2ph 2^-24", cvt_narrow(f32b(2.0 ** -24), F32, E, False), (0x0001, 0))
    chk("ps2ph 2^-25", cvt_narrow(f32b(2.0 ** -25), F32, E, False), (0x0000, UE | PE))
    chk("ps2ph 2^-25+", cvt_narrow(f32b(2.0 ** -25) + 1, F32, E, False), (0x0001, UE | PE))
    chk("ph2ps denorm", cvt_widen(0x0001, F32, True), (0x33800000, DE))
    chk("ph2ps snan", cvt_widen(0xFD01, F32, True), (0xFFE02000, IE))
    chk("ph2pd max", cvt_widen(0x7BFF, F64, True), (f64b(65504.0), 0))
    chk("ph2dq nan", cvt_h2int(0x7E00, 32, True, E, False), (0x80000000, IE))
    chk("ph2dq 2.5", cvt_h2int(0x4100, 32, True, E, False), (2, PE))
    chk("ph2dq -2.5", cvt_h2int(0xC100, 32, True, E, False), (0xFFFFFFFE, PE))
    chk("ph2dq -2.5 rd", cvt_h2int(0xC100, 32, True, ERD, False), (0xFFFFFFFD, PE))
    chk("tph2dq -2.5", cvt_h2int(0xC100, 32, True, ERU, True), (0xFFFFFFFE, PE))
    chk("ph2uw -1", cvt_h2int(0xBC00, 16, False, E, False), (0xFFFF, IE))
    chk("ph2uw -0.5", cvt_h2int(0xB800, 16, False, E, False), (0, PE))
    chk("ph2uw -0.5 rd", cvt_h2int(0xB800, 16, False, ERD, False), (0xFFFF, IE))
    chk("ph2uw max", cvt_h2int(0x7BFF, 16, False, E, False), (65504, 0))
    chk("ph2w 32768", cvt_h2int(0x7800, 16, True, E, False), (0x8000, IE))
    chk("ph2w 32752", cvt_h2int(0x77FF, 16, True, E, False), (32752, 0))
    chk("ph2uqq inf", cvt_h2int(0x7C00, 64, False, E, False), ((1 << 64) - 1, IE))
    chk("dq2ph 65520", cvt_int2h(65520, 32, True, E), (0x7C00, OE | PE))
    chk("dq2ph 65519", cvt_int2h(65519, 32, True, E), (0x7BFF, PE))
    chk("uw2ph 65535 rz", cvt_int2h(65535, 16, False, ERZ), (0x7BFF, PE))
    chk("uw2ph 65535", cvt_int2h(65535, 16, False, E), (0x7C00, OE | PE))
    chk("w2ph -1", cvt_int2h(0xFFFF, 16, True, E), (0xBC00, 0))
    chk("dq2ph 2049", cvt_int2h(2049, 32, True, E), (0x6800, PE))
    chk("dq2ph 2051", cvt_int2h(2051, 32, True, E), (0x6802, PE))
    chk("uqq2ph 2^64-1", cvt_int2h((1 << 64) - 1, 64, False, E), (0x7C00, OE | PE))
    # getexp / getmant / reduce / rndscale / scalef / fpclass
    chk("getexp 1", getexp16(0x3C00), (0x0000, 0))
    chk("getexp 2", getexp16(0x4000), (0x3C00, 0))
    chk("getexp min denorm", getexp16(0x0001), (0xCE00, DE))
    chk("getexp 0", getexp16(0x8000), (0xFC00, 0))
    chk("getexp -inf", getexp16(0xFC00), (0x7C00, 0))
    chk("getexp snan", getexp16(0xFD00), (0xFF00, IE))
    chk("getmant 12 [1,2)", getmant16(0x4A00, 0), (0x3E00, 0))
    chk("getmant 12 [1/2,2) odd exp", getmant16(0x4A00, 1), (0x3A00, 0))
    chk("getmant 6 [1/2,2) even exp", getmant16(0x4600, 1), (0x3E00, 0))
    chk("getmant 12 [1/2,1)", getmant16(0x4A00, 2), (0x3A00, 0))
    chk("getmant 12 [3/4,3/2)", getmant16(0x4A00, 3), (0x3A00, 0))
    chk("getmant 10 [3/4,3/2)", getmant16(0x4900, 3), (0x3D00, 0))
    chk("getmant -12 sc0", getmant16(0xCA00, 0), (0xBE00, 0))
    chk("getmant -12 sc1", getmant16(0xCA00, 4), (0x3E00, 0))
    chk("getmant -12 sc2", getmant16(0xCA00, 8), (0xFE00, IE))
    chk("getmant -0 sc0", getmant16(0x8000, 0), (0xBC00, 0))
    chk("getmant +inf", getmant16(0x7C00, 3), (0x3C00, 0))
    chk("getmant denorm", getmant16(0x0003, 0), (0x3E00, DE))
    chk("reduce 2.75 m0", reduce16(0x4180, 0x00, E), (0xB400, PE))
    chk("reduce 2.75 m1", reduce16(0x4180, 0x10, E), (0xB400, PE))
    chk("reduce 2.75 m2", reduce16(0x4180, 0x20, E), (0x0000, 0))
    chk("reduce 2.75 m2 rd", reduce16(0x4180, 0x21, E), (0x8000, 0))
    chk("reduce 2.75 rd", reduce16(0x4180, 0x01, E), (0x3A00, PE))
    chk("reduce 2.75 spe", reduce16(0x4180, 0x08, E), (0xB400, 0))
    chk("reduce -inf", reduce16(0xFC00, 0x00, E), (0x0000, 0))
    chk("reduce mxcsr ru", reduce16(0x4180, 0x04, ERU), (0xB400, PE))
    # 2^-24 - 1 (RU, M = 0) = -(1 - 2^-24) rounded up to -(1 - 2^-11)
    chk("reduce min denorm ru", reduce16(0x0001, 0x02, E), (0xBBFF, PE))
    chk("reduce min denorm rne", reduce16(0x0001, 0x00, E), (0x0001, PE))
    chk("rndscale 2.75", rndscale16(0x4180, 0x00, E), (0x4200, PE))
    chk("rndscale 2.75 m2", rndscale16(0x4180, 0x20, E), (0x4180, 0))
    chk("rndscale 2.75 spe", rndscale16(0x4180, 0x08, E), (0x4200, 0))
    chk("rndscale -0.3", rndscale16(0xB4CD, 0x00, E), (0x8000, PE))
    chk("rndscale -0.3 ru", rndscale16(0xB4CD, 0x02, E), (0x8000, PE))
    chk("rndscale tiny inexact", rndscale16(0x0003, 0xF0 | 0x0, E), (0x0000, PE))
    chk("rndscale tiny result", rndscale16(0x0300, 0xF2, E), (0x0400, PE))
    chk("rndscale 1.5*2^-15 m15 rd", rndscale16(0x0300, 0xF1, E), (0x0200, UE | PE))
    chk("rndscale snan", rndscale16(0x7D00, 0, E), (0x7F00, IE))
    chk("scalef 1*2^2.5", scalef16(0x3C00, 0x4100, E), (0x4400, 0))
    chk("scalef 3*2^-1.5", scalef16(0x4200, 0xBE00, E), (0x3A00, 0))
    chk("scalef 0*2^inf", scalef16(0x0000, 0x7C00, E), (0xFE00, IE))
    chk("scalef inf*2^-inf", scalef16(0xFC00, 0xFC00, E), (0xFE00, IE))
    chk("scalef qnan*2^inf", scalef16(0xFE00, 0x7C00, E), (0x7C00, 0))
    chk("scalef qnan*2^-inf", scalef16(0xFE00, 0xFC00, E), (0x0000, 0))
    chk("scalef 1*2^snan", scalef16(0x3C00, 0x7D00, E), (0x7F00, IE))
    chk("scalef denorm*2^-inf", scalef16(0x8001, 0xFC00, E), (0x8000, DE))
    chk("scalef max*2^1", scalef16(0x7BFF, 0x3C00, E), (0x7C00, OE | PE))
    chk("scalef 1*2^-25", scalef16(0x3C00, 0xCE40, E), (0x0000, UE | PE))
    chk("fpclass qnan", fpclass16(0x7E00, 0x01), (True, 0))
    chk("fpclass snan", fpclass16(0x7D00, 0x01), (False, 0))
    chk("fpclass neg denorm", fpclass16(0x8001, 0x40), (True, 0))
    chk("fpclass -0 negative", fpclass16(0x8000, 0x40), (False, 0))
    chk("cmp lt_os qnan", fp_cmp(1, 0x7E00, 0x3C00), (False, IE))
    chk("cmp eq_oq qnan", fp_cmp(0, 0x7E00, 0x3C00), (False, 0))
    chk("cmp neq_uq qnan", fp_cmp(4, 0x7E00, 0x3C00), (True, 0))
    chk("cmp eq_os qnan", fp_cmp(16, 0x7E00, 0x3C00), (False, IE))
    chk("cmp eq_oq snan", fp_cmp(0, 0x7D00, 0x3C00), (False, IE))
    chk("cmp ge_oq", fp_cmp(29, 0x4000, 0x3C00), (True, 0))
    chk("cmp 0 = -0", fp_cmp(0, 0x0000, 0x8000), (True, 0))
    chk("cmp denorm", fp_cmp(1, 0x0001, 0x0002), (True, DE))
    chk("comi qnan", fp_comi(0x7E00, 0x3C00, True), (1, 1, 1, IE))
    chk("ucomi qnan", fp_comi(0x7E00, 0x3C00, False), (1, 1, 1, 0))
    chk("comi lt", fp_comi(0xBC00, 0x3C00, True), (0, 0, 1, 0))
    # rcp / rsqrt tables and the 2^-11 + 2^-14 bound for every normal result
    bound = TWO ** -11 + TWO ** -14
    worst_r = worst_s = Fraction(0)
    for x in range(1, 0x7C00):
        v = cls(x)[2]
        r = rcp16(x)[0]
        if 0x0400 <= r < 0x7C00:
            e = abs(cls(r)[2] * v - 1)
            worst_r = max(worst_r, e)
        s = rsqrt16(x)[0]
        sv = cls(s)[2]
        # |s sqrt(v) - 1| < B  <=>  (1 - B)^2 < s^2 v < (1 + B)^2 (exact)
        if not (1 - bound) ** 2 < sv * sv * v < (1 + bound) ** 2:
            print("FAIL rsqrt bound %04X -> %04X" % (x, s))
            ok[0] = False
        e2 = abs(sv * sv * v - 1)
        worst_s = max(worst_s, e2 / 2)
        if x <= 0x0100:
            chk("rcp <= 2^-16 -> INF %04X" % x, r, 0x7C00)
        if (x & 0x3FF) == 0 and x >= 0x0400 and 1 / v <= 65504:
            chk("rcp 2^-n -> 2^n %04X" % x, cls(rcp16(x)[0])[2], 1 / v)
        if (x & 0x8000) == 0 and cls(x)[0] == "denorm":
            chk("rsqrt denorm normal %04X" % x, cls(s)[0], "normal")
    if worst_r >= bound:
        print("FAIL rcp/rsqrt bound: rcp %.3g rsqrt %.3g bound %.3g" % (worst_r, worst_s, bound))
        ok[0] = False
    print("rcp worst relative error %.6g, rsqrt %.6g (bound 2^-11 + 2^-14 = %.6g)" % (worst_r, worst_s, bound))
    chk("rcp +0", rcp16(0x0000), (0x7C00, 0))
    chk("rcp -0", rcp16(0x8000), (0xFC00, 0))
    chk("rcp -denorm small", rcp16(0x8100), (0xFC00, 0))
    chk("rcp +inf", rcp16(0x7C00), (0x0000, 0))
    chk("rcp -inf", rcp16(0xFC00), (0x8000, 0))
    chk("rcp 2^-10", rcp16(0x1400), (0x6400, 0))
    chk("rcp -2^4", rcp16(0xCC00), (0xAC00, 0))
    chk("rsqrt 2^-2n", rsqrt16(0x2C00), (0x4400, 0))           # 2^-4 -> 4
    chk("rsqrt -1", rsqrt16(0xBC00), (0xFE00, 0))
    chk("rsqrt -inf", rsqrt16(0xFC00), (0xFE00, 0))
    chk("rsqrt -0", rsqrt16(0x8000), (0xFC00, 0))
    chk("rsqrt +0", rsqrt16(0x0000), (0x7C00, 0))
    chk("rsqrt +inf", rsqrt16(0x7C00), (0x0000, 0))
    chk("rsqrt min denorm", rsqrt16(0x0001), (0x6C00, 0))      # 2^-24 -> 2^12
    chk("rsqrt snan", rsqrt16(0x7D00), (0x7F00, 0))
    # forms vs Emulator/data/evex_forms.tsv
    tsv = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "evex_forms.tsv")
    rows = []
    hdr = None
    for line in open(tsv, encoding="utf-8"):
        if line.startswith("#"):
            continue
        p = line.rstrip("\n").split("\t")
        if hdr is None:
            hdr = p
            continue
        r = dict(zip(hdr, p))
        if r["feature"] == "AVX512_FP16":
            rows.append(r)
    mapn = {"0F3A": 3, "MAP5": 5, "MAP6": 6}
    ppn = {"NP": 0, "66": 1, "F3": 2, "F2": 3}
    matched = 0
    for r in rows:
        w = "ig" if r["W"] in ("ig", "WIG") else int(r["W"])
        cands = [f for f in FORMS if f.name == r["mnemonic"].upper() and f.mmm == mapn[r["map"]] and
                 f.pp == ppn[r["pp"]] and f.opc == int(r["opcode"], 16) and f.w == w]
        if r["mnemonic"] == "vmovsh":
            lay = {("10", "mem"): "movsh_ld", ("11", "mem"): "movsh_st", ("10", "reg"): "movsh_rr",
                   ("11", "reg"): "movsh_rr11"}[(r["opcode"], r["mod"])]
            cands = [f for f in cands if f.lay == lay]
        if len(cands) != 1:
            print("FAIL tsv row %s %s %s %s W%s: %d model forms" % (r["mnemonic"], r["map"], r["pp"], r["opcode"],
                                                                     r["W"], len(cands)))
            ok[0] = False
            continue
        f = cands[0]
        matched += 1
        rs = None if r["rc_sae"] == "-" else r["rc_sae"]
        if f.lay not in ("movsh_ld", "movsh_st", "movsh_rr", "movsh_rr11", "movw_ld", "movw_st") and rs != f.rcsae:
            print("FAIL %s rc_sae tsv %s model %s" % (f.name, rs, f.rcsae))
            ok[0] = False
        if r["vl"] != "LIG" and r["vl"] == "128,256,512":
            for vl, col in ((16, "N128"), (32, "N256"), (64, "N512")):
                if int(r[col]) != f.kl(vl) * f.sesz:
                    print("FAIL %s N%d tsv %s model %d" % (f.name, vl * 8, r[col], f.kl(vl) * f.sesz))
                    ok[0] = False
            nb = r["N_bcst"]
            if (nb != "-") != f.bcst or (nb != "-" and int(nb) != f.sesz):
                print("FAIL %s N_bcst tsv %s model %s" % (f.name, nb, f.sesz if f.bcst else "-"))
                ok[0] = False
        elif r["vl"] == "LIG" and f.lay in ("rvm", "rm", "fma", "krvm", "krm"):
            if int(r["N128"]) != f.sesz:
                print("FAIL %s scalar N tsv %s model %d" % (f.name, r["N128"], f.sesz))
                ok[0] = False
        mk = {"merge+zero": "mz", "k-dest": "k", "none": "none", "merge": "m"}[r["masking"]]
        if (mk == "k") != (f.lay in ("krvm", "krm")):
            print("FAIL %s masking %s" % (f.name, r["masking"]))
            ok[0] = False
    chk("tsv rows matched", (matched, len(rows)), (len(FORMS), len(FORMS)))
    return ok[0]


# ---------------------------------------------------------------------------------------
# hardware cross-check of the shared FP32 <-> FP16 conversion functions: F16C VCVTPS2PH and
# VCVTPH2PS (VEX.128) run natively on the i5-13600K as emu-alltest hardware cases
# ---------------------------------------------------------------------------------------
def f16c_ps2ph(vals, imm, mxcsr):
    """F16C VCVTPS2PH xmm0, xmm1, imm: imm[2] ? MXCSR.RC : imm[1:0]; DAZ applies to the FP32
    inputs (Denormal "if MXCSR.DAZ=0"), MXCSR.FTZ ignored. -> (4 results, flags, unmasked)"""
    rc = (mxcsr >> 13) & 3 if imm & 4 else imm & 3
    env = Env(mxcsr, rc=None)
    env.rc = rc
    res, flags = [], 0
    for x in vals:
        r, fl = cvt_narrow(x, F32, env, bool(mxcsr & DAZ))
        res.append(r)
        flags |= fl
    unm = flags & ~(mxcsr >> 7) & 0x3F
    return res, flags_after(mxcsr, flags, unm), unm


def f16c_ph2ps(vals, mxcsr):
    """F16C VCVTPH2PS: exact, DAZ ignored, no DE ("No denormal exception is reported"), IE for SNaN"""
    res, flags = [], 0
    for x in vals:
        r, fl = cvt_widen(x, F32, False)
        res.append(r)
        flags |= fl
    unm = flags & ~(mxcsr >> 7) & 0x3F
    return res, flags_after(mxcsr, flags, unm), unm


def hw_ps_values(rng):
    vals = []
    # midpoints between neighbouring FP16 values (every 5th FP16 value) and their FP32 neighbours
    for h in range(0, 0x7C00, 5):
        mid = (cls(h)[2] + cls(h + 1)[2]) / 2
        mb = f32b(float(mid))
        for d in (-1, 0, 1):
            s = rng.bits(1) << 31
            vals.append(s | (mb + d))
    # the overflow threshold 65504 .. 65536 in FP32 steps of 16 ulps
    for x in range(f32b(65504.0), f32b(65536.0) + 1, 16):
        vals.append(x | (rng.bits(1) << 31))
    # the FP16 denormal / normal boundary and the smallest denormal
    for base in (2.0 ** -14, 2.0 ** -24, 2.0 ** -25, 2.0 ** -15, 3 * 2.0 ** -26):
        b = f32b(base)
        for d in range(-6, 7):
            vals.append((b + d) | (rng.bits(1) << 31))
    # FP32 denormals, random FP32 over the whole range, specials
    for _ in range(1200):
        vals.append((rng.bits(1) << 31) | rng.randint(1, 0x7FFFFF))
    for _ in range(3000):
        vals.append(rng.bits(32))
    for _ in range(3000):
        vals.append(rng.val("s"))
    vals += SPECIAL32 + [0x7FBFFFFF, 0xFFC00001, 0x7F802000, 0x7FC01FFF, 0xFF801FFF]
    while len(vals) % 4:
        vals.append(rng.val("s"))
    return vals


def hwcheck_gen(out, expect_path):
    rng = Rng(0xF16C)
    exp = []
    lines = []

    def add_ps2ph(v4, imm, mx):
        x0 = rng.bytes(16)
        res, flags, unm = f16c_ps2ph(v4, imm, mx)
        lines.append("vcvtps2ph xmm0, xmm1, %d | xmm0=%s xmm1=%s mxcsr=0x%X" % (imm, hexs(x0), hexs(pack(v4, 4)), mx))
        if unm:
            exp.append({"fault": 19, "mxcsr": mx | flags, "xmm0": hexs(x0)})
        else:
            exp.append({"fault": -1, "mxcsr": mx | flags, "xmm0": hexs(pack(res, 2) + bytes(8))})

    def add_ph2ps(v4, mx):
        x0 = rng.bytes(16)
        res, flags, unm = f16c_ph2ps(v4, mx)
        lines.append("vcvtph2ps xmm0, xmm1 | xmm0=%s xmm1=%s mxcsr=0x%X" % (hexs(x0), hexs(pack(v4, 2) + rng.bytes(8)), mx))
        if unm:
            exp.append({"fault": 19, "mxcsr": mx | flags, "xmm0": hexs(x0)})
        else:
            exp.append({"fault": -1, "mxcsr": mx | flags, "xmm0": hexs(pack(res, 4))})

    # VCVTPH2PS: every FP16 bit pattern, default MXCSR; specials with DAZ/FTZ and IM/DM unmasked
    allh = list(range(0x10000))
    for i in range(0, 0x10000, 4):
        add_ph2ps(allh[i:i + 4], 0x1F80)
    sp = SPECIAL16 + EDGE16 + [0x7C01, 0xFDFF, 0x7FFF, 0x83FF]
    while len(sp) % 4:
        sp.append(0x3C00)
    for mx in (0x1FC0, 0x9F80, 0x9FC0, 0x1F00, 0x1E80, 0x0000, 0x7F80):
        for i in range(0, len(sp), 4):
            add_ph2ps(sp[i:i + 4], mx)
        for i in range(0, 64, 4):
            add_ph2ps([rng.randint(1, 0x3FF) | (rng.bits(1) << 15) for _ in range(4)], mx)
    # VCVTPS2PH: the full value set with imm 0..3 (MXCSR default) and imm 4 with each MXCSR.RC
    vals = hw_ps_values(rng)
    for imm in (0, 1, 2, 3):
        for i in range(0, len(vals), 4):
            add_ps2ph(vals[i:i + 4], imm, 0x1F80)
    for rcm in (0, 1, 2, 3):
        for i in range(0, len(vals), 4):
            add_ps2ph(vals[i:i + 4], 4 | (rng.bits(1) << 3) | (rng.choice([0, 0xF0])), 0x1F80 | (rcm << 13))
    # DAZ / FTZ / unmasked exceptions on a subset (and imm[7:3] ignored)
    sub = SPECIAL32 + [vals[rng.randint(0, len(vals) - 1)] for _ in range(400)] + \
        [(rng.bits(1) << 31) | rng.randint(1, 0x7FFFFF) for _ in range(100)]
    while len(sub) % 4:
        sub.append(0x3F800000)
    for mx in (0x1FC0, 0x9F80, 0x9FC0, 0x1F00, 0x1E80, 0x1D80, 0x1B80, 0x1780, 0x0F80, 0x0000, 0x0040,
               0x1F80 & ~0x1800, 0x1F80 & ~0x0900):
        for i in range(0, len(sub), 4):
            add_ps2ph(sub[i:i + 4], rng.choice([0, 1, 2, 3, 4, 0xF8, 0x0D]), mx)
    # single offending lane with unmasked exceptions (others exact)
    for mx, xs in ((0x1F80 & ~0x1000, [f32b(1 + 2.0 ** -11), f32b(0.1), f32b(3 * 2.0 ** -26), f32b(2.0 ** -25)]),
                   (0x1F80 & ~0x0800, [f32b(2.0 ** -24), f32b(2.0 ** -20), f32b(3 * 2.0 ** -26), 0x00000001,
                                       f32b(2.0 ** -25), f32b(2.0 ** -25) + 1, f32b(2.0 ** -20 * (1 + 2.0 ** -10)),
                                       f32b(2.0 ** -20 * (1 + 2.0 ** -11)), f32b(-2.0 ** -30), 0x807FFFFF,
                                       f32b(2.0 ** -14 * (1 - 2.0 ** -12)), f32b(2.0 ** -14 * (1 - 2.0 ** -11))]),
                   (0x1F80 & ~0x1800, [f32b(3 * 2.0 ** -26), f32b(2.0 ** -25) + 1, 0x00000001]),
                   (0x1F80 & ~0x0400, [f32b(65520.0), f32b(1e10), f32b(65536.0), f32b(-131072.0),
                                       f32b(2.0 ** 20), f32b(65536.0 + 32), f32b(65536.0 + 64)]),
                   (0x1F80, [f32b(65536.0), f32b(-131072.0), f32b(2.0 ** 20)]),
                   (0x1F80 & ~0x1400, [f32b(65536.0), f32b(65520.0)]),
                   (0x1F80 & ~0x0100, [0x00000001, 0x80400000]),
                   (0x1F80 & ~0x0100 | DAZ, [0x00000001, 0x80400000]),
                   (0x1F80 & ~0x0080, [0x7F800001, 0x7FC00000])):
        for x in xs:
            for lane in range(4):
                v4 = [f32b(1.0), f32b(2.0), f32b(0.5), f32b(-4.0)]
                v4[lane] = x
                add_ps2ph(v4, rng.choice([0, 4]), mx)
    for ln in lines:
        out.write(ln + "\n")
    json.dump(exp, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    exp = json.load(open(expect_path))
    cur, bad, seen, inp = None, 0, 0, {}
    per = {}
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] (?:SAME|DIFF) (\S+) .*\| xmm0=(\S+) xmm1=(\S+) mxcsr=(\S+)", line)
        if m:
            cur = int(m.group(1))
            inp = {"op": m.group(2), "xmm0": m.group(3), "xmm1": m.group(4), "mxcsr": int(m.group(5), 16),
                   "line": line}
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if m and cur is not None:
            fields = m.group(1)
            fault = -1
            fm = re.match(r"fault #(\d+)\s*(.*)$", fields)
            if fm:
                fault, fields = int(fm.group(1)), fm.group(2)
            kv = dict(x.split("=", 1) for x in fields.split() if "=" in x)
            got_x = kv.get("xmm0", inp["xmm0"])
            got_m = int(kv["mxcsr"], 16) if "mxcsr" in kv else inp["mxcsr"]
            e = exp[cur]
            seen += 1
            key = inp["op"]
            per.setdefault(key, [0, 0])
            per[key][0] += 1
            if got_x.upper() != e["xmm0"].upper() or got_m != e["mxcsr"] or fault != e["fault"]:
                bad += 1
                per[key][1] += 1
                if bad <= 40:
                    print("%s\n    hw: xmm0=%s mxcsr=%X fault=%d | model: xmm0=%s mxcsr=%X fault=%d"
                          % (inp["line"][:200], got_x, got_m, fault, e["xmm0"], e["mxcsr"], e["fault"]))
            cur = None
    for k, (n, b) in sorted(per.items()):
        print("  %s: %d cases, %d differ" % (k, n, b))
    print("hwcheck: %d cases compared (of %d generated), %d differ from the model" % (seen, len(exp), bad))
    return bad == 0 and seen == len(exp)


def main():
    argv = sys.argv[1:]
    if "--hwgen" in argv:
        hwcheck_gen(sys.stdout, argv[argv.index("--hwgen") + 1])
        return
    if "--hwcmp" in argv:
        i = argv.index("--hwcmp")
        sys.exit(0 if hwcheck_cmp(argv[i + 1], argv[i + 2]) else 1)
    if "--selftest" in argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    only = None
    if "--only" in argv:
        only = [x.strip().upper() for x in argv[argv.index("--only") + 1].split(",") if x.strip()]
        unknown = [x for x in only if x not in all_mnemonics()]
        if unknown:
            print("unknown mnemonic(s): %s" % ", ".join(unknown))
            sys.exit(2)
    if "--stats" in argv:
        tot = 0
        for name, lines in gen_all(only):
            n = sum(1 for c in lines if not isinstance(c, str))
            tot += n
            print("%-16s %5d" % (name, n))
        print("%-16s %5d" % ("total", tot))
        return
    if "--cases" in argv:
        groups = gen_all(only)
        if "--out" in argv:
            # CRLF like the other files of the working tree (core.autocrlf)
            with open(argv[argv.index("--out") + 1], "w", encoding="ascii", newline="\r\n") as fo:
                write_cases(fo, groups)
        else:
            write_cases(sys.stdout, groups)
        return
    print(__doc__)


if __name__ == "__main__":
    main()
