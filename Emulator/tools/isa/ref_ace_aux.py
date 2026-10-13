#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_ace_aux.py -- independent reference model (Python 3 stdlib only) of the AVX10_V2_AUX
instructions (ledger U1095, plan 1.J.20) and generator of the expected-value case file
Emulator/data/cases_avx10_v2aux.txt.

Source: "AI Compute Extensions (ACE) Specification" 1.15 (x86 Ecosystem Advisory Group, 05/15/2026,
E:\\pdfs\\x86docs\\Third-Parties\\ACE\\ACE_v1_Specification_public_1_15.pdf): 3.1 CPUID (AVX10_V2_AUX =
CPUID.(EAX=24H,ECX=1):ECX[3], "Requires AVX10"), 4.3, 5 (exception classes), 6.2 (encodings),
9 (operations), 16.1/16.3/16.4 (helper pseudocode). Encodings cross-checked with XED
(<NOVMP_SOURCES_ROOT, default E:\\sources>\\xed-main\\datafiles\\avx10-2-aux, read only). Not written from any C implementation.

Each helper exists twice: lit_* is a literal transcription of the ACE 16.x pseudocode (bit
operations, branch structure as printed); nat_* derives the same conversion from exact rationals
(fractions.Fraction) and the description of chapter 9 (OCP OFP8 / MX formats of 2.4, the rounding
modes of 2.6, the SAT / NONSAT table of 9.2.1). --selftest compares both (exhaustively for the
8/6/4-bit sources, on a dense sample of FP32 inputs) and reports every point where the printed
pseudocode differs from its own description; the expected values follow the pseudocode except at
the points documented in RESOLVED below.

  VCVTPS2BF8[S] / VCVTPS2HF8[S]    MAP5 F3 W0 39/3B/38/3A  FP32 -> E5M2/E4M3, RTNE, [S]aturating
  VCVTROPS2HF8[S]                  MAP5 66 W0 38/3A        FP32 -> E4M3, round to odd
  VCVTBIASPS2BF8[S] / ...HF8[S]    MAP5 NP W0 39/3B/38/3A  FP32 -> FP8, bias rounding: bias of element
                                                           i = SRC1 (EVEX.vvvv).dword[i] (see GAPS)
  VCVTBF82PS / VCVTHF82PS          MAP5 NP W1/W0 36        FP8 -> FP32, exact, Quarter Mem source
  VCVTBF82BF4S / VCVTHF82BF4S      MAP5 F3 W1/W0 3D        FP8 -> FP4 E2M1 (nibbles), r/m destination
                                                           (register or VL/16-byte store), no masking
  VCVTBF42HF8                      MAP5 NP W0 37           FP4 -> E4M3, Half Mem source
  VCVTBF82BF6S / VCVTHF82HF6S      MAP5 F3 W1 3E / W0 3C   FP8 -> FP6 E3M2/E2M3 (6-bit packed),
                                                           register only, r/m destination
  VCVTBF62HF8 / VCVTHF62HF8        MAP5 66 W1/W0 37        FP6 -> E4M3, register only
  VPMOVSSDB                        0F38 F3 W0 41           dword -> byte, symmetric [-127, 127],
                                                           r/m destination (E6: masked store)
  VUNPACKB                         0F3A NP W0 3D ib        sub-byte fields (2..7 bits) -> bytes

Exception classes (ACE 5.1 table is garbled in the PDF text; the per-instruction "Other
Exceptions" sections and XED agree): E4 FP32->FP8 and FP8->FP32 (memory fault suppression for
masked-off elements); E4NF FP8->FP4, FP4->FP8, VUNPACKB (no fault suppression); E7NM FP8->FP6 and
FP6->FP8 (register only: a memory operand is #UD); E6 VPMOVSSDB (masked store, EVEX.z with a
memory destination #UD, EVEX.vvvv != 1111b #UD). No SIMD FP exception, MXCSR neither consulted nor
updated (9.2.1, 9.3.1, ...). EVEX.b: m32bcst only for the FP32 sources (Full tuple); #UD on any
register-register form and on the other memory forms (XED BCRC=0).

GAPS (ACE 1.15 does not define, recorded in docs/quirks.md "AVX10_V2_AUX (ACE 1.15): gaps and resolutions"):
  G1 vcvtbiasps2f8 is referenced by 9.2.5 ("separate 3-operand form with VVVV") but not printed:
     which bits of operand 2 (the vector "Bias rounding term", XED XMM_N3 f32) form the "bias"
     argument of the 16.1 helpers is not defined. SDE-RESOLVED (SDE 10.13.1 -future-ag probe,
     docs/quirks.md "AVX10_V2_AUX (ACE 1.15): gaps and resolutions"): the low byte of SRC1.dword[i], aligned to the most significant
     truncated bit - bias = (SRC1.dword[i] & FFh) << 13 for E5M2 (21 truncated bits), << 12 for
     E4M3 (20), the FP32 analogue of the FP16 helpers' byte bias (8 / 7 truncated bits).
  G2 operand tables of VCVTBF82BF4S/VCVTHF82BF4S, VCVTBF82BF6S/VCVTHF82HF6S and VPMOVSSDB say
     "Operand 1 MODRM.REG(w), Operand 2 MODRM.R/M(r)" while the Form column of BF4S / VPMOVSSDB
     has a memory destination ("xmm1/m64, xmm2"), only possible in ModRM.r/m: the destination is
     ModRM.r/m for all of them (XED: REG0 = XMM_B3 / MEM0 written); SDE-checked (BF6S, "G2").
  G3 fp32_to_fp8_e4m3 BIAS path: no clamp of the e=15, m=7 (NaN) encoding when saturating (the
     FP16 helper and the RTNE/RTO paths clamp to max_normal, the 9.2.1 SAT table says +-max): see
     RESOLVED.
  G4 fp32_to_fp8_e4m3 BIAS path: "newexp <= 0 -> e_o, m_o = 0, 0" (no subnormal results, i.e. FTZ,
     against 9.2.1 "FTZ is not obeyed and is always assumed FTZ=0" and the E5M2 BIAS path).
  G5 both FP32 BIAS helpers add the bias at the FP32 mantissa position and then shift into the
     subnormal range (E5M2) - the bias is not aligned to the destination LSB there, while the AVX10.2
     spec's own FP16 helper (361050-007 5.1 convert_fp16_to_hf8_bias: "bias rounding should be done
     after aligning to the destination format") aligns it. G4 + G5 SDE-RESOLVED (SDE 10.13.1): a
     subnormal result is floor(|x| / LSB + bias_byte / 256) (sub_bias), no lower cutoff.

Usage:
  python ref_ace_aux.py --selftest     literal vs rational models; exit 0 on pass
  python ref_ace_aux.py --cases        Emulator/data/cases_avx10_v2aux.txt (stdout)
"""

import os
import random
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ref_evex_m1 import Mem, evex, hexs, Case, MEM_RSI, RSI  # noqa: E402

RNG = random.Random(0xACE1_2026)
VL_LL = {16: 0, 32: 1, 64: 2}
cases = []

# RESOLVED: points where the printed pseudocode contradicts its own description and the
# expected value follows the description (and SDE 10.13.1 -future-ag, SDE-validated):
#   R1 (G3) FP32 -> E4M3 BIAS saturating: a result in the e=15, m=7 encoding is clamped to
#      max_normal (S.1111.110) as in every other saturating path (9.2.1 SAT table "+-max_E4M3").
#   G1 (SDE-resolved) the bias of element i: bias_arg() below.
RESOLVE_G3 = True


def bias_arg(fmt, dword):
    """G1 (SDE-resolved): the 16.1 helper's bias argument from SRC1.dword[i]"""
    return (dword & 0xFF) << (13 if fmt == "BF8" else 12)


def mask(n):
    return (1 << n) - 1


# ======================================================================================
# literal transcriptions of ACE 1.15 16.1 / 16.3 / 16.4
# ======================================================================================
def sub_bias(mant, shift, byte):
    """G5 (SDE-resolved): a subnormal FP8 result of a BIAS conversion - the bias byte added to the 8
    bits below the destination LSB (mant = J.mantissa of the FP32 source, LSB = 2^shift units),
    then truncation: floor(mant / 2^shift + byte / 256)"""
    if shift >= 32:
        return 0                                            # mant / 2^shift < 2^-8
    return (mant * 256 + (byte << shift)) >> (shift + 8)


def lit_fp32_to_e5m2(i, sat, rnd, bias=0, resolve=True):
    s_i = (i >> 31) & 1
    e_i = (i >> 23) & 0xFF
    m_i = i & 0x7FFFFF
    if e_i == 0xFF:
        if sat:
            if m_i == 0:
                e_o, m_o = 0x1E, 0x3
            else:
                e_o, m_o = 0x1F, 0x2 | ((m_i >> 21) & 1)
        else:
            e_o = 0x1F
            m_o = (0x2 | ((m_i >> 21) & 1)) if m_i != 0 else 0
    elif e_i == 0:
        e_o, m_o = 0, 0
    elif rnd == "RTNE":
        newexp = e_i - 127 + 15
        if newexp >= 31:
            e_o, m_o = (0x1E, 0x3) if sat else (0x1F, 0)
        elif newexp <= 0:
            e_o, m_o = 0, 0
            if (22 - newexp) <= 24:
                mant = m_i | 0x800000
                shift = 22 - newexp
                m_o = mant >> shift
                low = mant & mask(shift)
                half = 1 << (shift - 1)
                if low > half or (low == half and (m_o & 1)):
                    m_o += 1
                    if (m_o & 0x3) == 0:
                        e_o += 1
        else:
            e_o = newexp
            m_o = m_i >> 21
            if m_i & 0x100000:
                if (m_i & 0x1FFFFF) > 0x100000 or (m_o & 1):
                    if not (sat and e_o == 0x1E and m_o == 0x3):
                        m_o += 1
                        if (m_o & 0x3) == 0:
                            e_o += 1
    elif resolve and e_i - 127 + 15 <= 0:                   # BIAS, subnormal result (G5)
        n = sub_bias(m_i | 0x800000, 22 - (e_i - 127 + 15), (bias >> 13) & 0xFF)
        e_o, m_o = (1, n - 4) if n >= 4 else (0, n)
    else:                                                   # BIAS
        e_b = e_i
        m_b = m_i + (bias & 0x1FFFFF)
        if m_b & 0xFF800000:
            e_b += 1
        m_b &= 0x7FFFFF
        newexp = e_b - 127 + 15
        if newexp >= 31:
            e_o, m_o = (0x1E, 0x3) if sat else (0x1F, 0)
        elif newexp <= 0:
            e_o, m_o = 0, 0
            if (22 - newexp) <= 24:
                mant = m_b | 0x800000
                m_o = mant >> (22 - newexp)
        else:
            e_o = newexp
            m_o = m_b >> 21
    return ((s_i & 1) << 7) | ((e_o & 0x1F) << 2) | (m_o & 0x3)


def lit_fp32_to_e4m3(i, sat, rnd, bias=0, resolve=RESOLVE_G3):
    s_i = (i >> 31) & 1
    e_i = (i >> 23) & 0xFF
    m_i = i & 0x7FFFFF
    if e_i == 0xFF:
        e_o, m_o = 0xF, 0x7
        if sat and m_i == 0:
            m_o = 0x6
    elif e_i == 0:
        e_o, m_o = 0, 0
    elif rnd == "RTNE":
        newexp = e_i - 127 + 7
        if newexp >= 16:
            e_o = 0xF
            m_o = 0x6 if sat else 0x7
        elif newexp <= 0:
            e_o, m_o = 0, 0
            if (21 - newexp) <= 24:
                mant = m_i | 0x800000
                shift = 21 - newexp
                m_o = mant >> shift
                low = mant & mask(shift)
                half = 1 << (shift - 1)
                if low > half or (low == half and (m_o & 1)):
                    m_o += 1
                    if (m_o & 0x7) == 0:
                        e_o += 1
        else:
            e_o = newexp
            m_o = m_i >> 20
            if sat and e_o == 0xF and m_o == 0x7:
                m_o = 0x6
            if m_i & 0x80000:
                if (m_i & 0xFFFFF) > 0x80000 or (m_o & 1):
                    clamp_sat = sat and e_o == 0xF and m_o == 0x6
                    clamp_nan = (not sat) and e_o == 0xF and m_o == 0x7
                    if not (clamp_sat or clamp_nan):
                        m_o += 1
                        if (m_o & 0x7) == 0:
                            e_o += 1
    elif rnd == "RTO":
        newexp = e_i - 127 + 7
        if newexp >= 16:
            e_o = 0xF
            m_o = 0x6 if sat else 0x7
        elif newexp <= 0:
            e_o, m_o = 0, 0
            if (21 - newexp) <= 24:
                mant = m_i | 0x800000
                shift = 21 - newexp
                m_o = mant >> shift
                m_o |= 1 if (mant & mask(shift)) else 0
            else:
                m_o = 1
        else:
            e_o = newexp
            m_o = m_i >> 20
            m_o |= 1 if (m_i & 0xFFFFF) else 0
            if sat and e_o == 0xF and m_o == 0x7:
                m_o = 0x6
    elif resolve and e_i - 127 + 7 <= 0:                    # BIAS, subnormal result (G4/G5)
        n = sub_bias(m_i | 0x800000, 21 - (e_i - 127 + 7), (bias >> 12) & 0xFF)
        e_o, m_o = (1, n - 8) if n >= 8 else (0, n)
    else:                                                   # BIAS
        e_b = e_i
        m_b = m_i + (bias & 0xFFFFF)
        if m_b & 0xFF800000:
            e_b += 1
        m_b &= 0x7FFFFF
        newexp = e_b - 127 + 7
        if newexp >= 16:
            e_o = 0xF
            m_o = 0x6 if sat else 0x7
        elif newexp <= 0:
            e_o, m_o = 0, 0
        else:
            e_o = newexp
            m_o = m_b >> 20
            if resolve and sat and e_o == 0xF and m_o == 0x7:   # R1 (G3)
                m_o = 0x6
    return ((s_i & 1) << 7) | ((e_o & 0xF) << 3) | (m_o & 0x7)


def lit_e5m2_to_fp32(i):
    s_i, e_i, m_i = (i & 0x80) >> 7, (i & 0x7C) >> 2, i & 3
    if e_i == 0x1F:
        e_o, m_o = 0xFF, 0
        if m_i != 0:
            m_o = (m_i | 0x2) << 21
    elif e_i == 0:
        if m_i == 0:
            e_o, m_o = 0, 0
        elif m_i == 1:
            e_o, m_o = 127 - 16, 0
        else:
            e_o, m_o = 127 - 15, (m_i & 1) << 22
    else:
        e_o, m_o = e_i + (127 - 15), m_i << 21
    return (s_i << 31) | ((e_o & 0xFF) << 23) | m_o


def lit_e4m3_to_fp32(i):
    s_i, e_i, m_i = (i & 0x80) >> 7, (i & 0x78) >> 3, i & 7
    if e_i == 0xF:
        if m_i == 7:
            e_o, m_o = 0xFF, (m_i | 4) << 20
        else:
            e_o, m_o = e_i + (127 - 7), m_i << 20
    elif e_i == 0:
        if m_i == 0:
            e_o, m_o = 0, 0
        elif m_i == 1:
            e_o, m_o = 127 - 9, 0
        elif (m_i & 4) == 0:
            e_o, m_o = 127 - 8, (m_i & 1) << 22
        else:
            e_o, m_o = 127 - 7, (m_i & 3) << 21
    else:
        e_o, m_o = e_i + (127 - 7), m_i << 20
    return (s_i << 31) | ((e_o & 0xFF) << 23) | m_o


def lit_e5m2_to_e3m2(i):
    s_i, e_i, m_i = (i & 0x80) >> 7, (i & 0x7C) >> 2, i & 3
    e_o = m_o = 0
    reb = 15 - 3
    new_exp = e_i - reb
    if e_i == 0x1F:
        e_o, m_o = 7, 3
    elif e_i > reb + 7 or (e_i == reb + 7 and m_i > 3):
        e_o, m_o = 7, 3
    elif e_i == 0:
        e_o = m_o = 0
    elif new_exp <= 0:
        if (1 - new_exp) <= 3:
            mant = m_i | 4
            shift = 1 - new_exp
            m_o = mant >> shift
            low = mant & mask(shift)
            half = 1 << (shift - 1)
            if low > half or (low == half and (m_o & 1)):
                m_o += 1
                if (m_o & 3) == 0:
                    e_o += 1
    else:
        e_o, m_o = e_i - reb, m_i
    return (s_i << 5) | ((e_o & 7) << 2) | (m_o & 3)


def lit_e4m3_to_e2m3(i):
    s_i, e_i, m_i = (i & 0x80) >> 7, (i & 0x78) >> 3, i & 7
    e_o = m_o = 0
    reb = 7 - 1
    new_exp = e_i - reb
    if e_i == 0xF:
        e_o, m_o = 3, 7
    elif e_i > reb + 3 or (e_i == reb + 3 and m_i > 7):
        e_o, m_o = 3, 7
    elif e_i == 0 and m_i == 0:
        e_o = m_o = 0
    elif new_exp <= 0:
        if (1 - new_exp) <= 4:
            mant = m_i | 8
            shift = 1 - new_exp
            m_o = mant >> shift
            low = mant & mask(shift)
            half = 1 << (shift - 1)
            if low > half or (low == half and (m_o & 1)):
                m_o += 1
                if (m_o & 7) == 0:
                    e_o += 1
    else:
        e_o, m_o = e_i - reb, m_i
    return (s_i << 5) | ((e_o & 3) << 3) | (m_o & 7)


def lit_e4m3_to_e2m1(i):
    s_i, e_i, m_i = (i & 0x80) >> 7, (i & 0x78) >> 3, i & 7
    e_o = m_o = 0
    reb = 7 - 1
    new_exp = e_i - reb
    if e_i == 0xF and m_i == 7:
        e_o, m_o = 3, 1
    elif e_i > reb + 3 or (e_i == reb + 3 and m_i > 4):
        e_o, m_o = 3, 1
    elif e_i == 0:
        e_o = m_o = 0
    elif new_exp <= 0:
        if (3 - new_exp) <= 4:
            mant = m_i | 8
            shift = 3 - new_exp
            m_o = mant >> shift
            low = mant & mask(shift)
            half = 1 << (shift - 1)
            if low > half or (low == half and (m_o & 1)):
                m_o += 1
                if (m_o & 1) == 0:
                    e_o += 1
    else:
        fixup = (m_i >> 2) & 1
        rnex = i + 1 + fixup
        e_o = ((rnex & 0x78) >> 3) - reb
        m_o = (rnex & 7) >> 2
    return (s_i << 3) | ((e_o & 3) << 1) | (m_o & 1)


def lit_e5m2_to_e2m1(i):
    s_i, e_i, m_i = (i & 0x80) >> 7, (i & 0x7C) >> 2, i & 3
    e_o = m_o = 0
    reb = 15 - 1
    new_exp = e_i - reb
    if e_i == 0x1F:
        e_o, m_o = 3, 1
    elif e_i > reb + 3 or (e_i == reb + 3 and m_i > 2):
        e_o, m_o = 3, 1
    elif e_i == 0 and m_i == 0:
        e_o = m_o = 0
    elif new_exp <= 0:
        if (2 - new_exp) <= 3:
            mant = m_i | 4
            shift = 2 - new_exp
            m_o = mant >> shift
            low = mant & mask(shift)
            half = 1 << (shift - 1)
            if low > half or (low == half and (m_o & 1)):
                m_o += 1
                if (m_o & 1) == 0:
                    e_o += 1
    else:
        fixup = (m_i >> 1) & 1
        rnex = i + fixup
        e_o = ((rnex & 0x7C) >> 2) - reb
        m_o = (rnex & 3) >> 1
    return (s_i << 3) | ((e_o & 3) << 1) | (m_o & 1)


FP4_LUT = [0x00, 0x30, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C]


def lit_e2m1_to_e4m3(i):
    return FP4_LUT[i & 7] | (((i >> 3) & 1) << 7)


def lit_e3m2_to_e4m3(i):
    s_i, e_i, m_i = (i & 0x20) >> 5, (i & 0x1C) >> 2, i & 3
    if e_i == 0:
        if m_i == 0:
            e_o = m_o = 0
        elif m_i == 1:
            e_o, m_o = 3, 0
        else:
            e_o, m_o = 4, (m_i & 1) << 2
    else:
        e_o, m_o = e_i + 4, m_i << 1
    return (s_i << 7) | ((e_o & 0xF) << 3) | (m_o & 7)


def lit_e2m3_to_e4m3(i):
    s_i, e_i, m_i = (i & 0x20) >> 5, (i & 0x18) >> 3, i & 7
    if e_i == 0:
        if m_i == 0:
            e_o = m_o = 0
        elif m_i == 1:
            e_o, m_o = 4, 0
        elif m_i <= 3:
            e_o, m_o = 5, (m_i & 1) << 2
        else:
            e_o, m_o = 6, (m_i & 3) << 1
    else:
        e_o, m_o = e_i + 6, m_i
    return (s_i << 7) | ((e_o & 0xF) << 3) | (m_o & 7)


def sym_sat_byte(d):
    """saturate_int32_to_symmetric_int8 (9.8): [-127, 127]"""
    v = d - (1 << 32) if d & 0x80000000 else d
    return max(-127, min(127, v)) & 0xFF


# ======================================================================================
# rational models (formats of ACE 2.4: OCP OFP8 E5M2 / E4M3, MX FP6 E3M2 / E2M3, FP4 E2M1)
# ======================================================================================
class F:
    def __init__(self, name, e, m, bias, inf, nan):
        self.name, self.e, self.m, self.bias, self.inf, self.nan = name, e, m, bias, inf, nan
        self.bits = 1 + e + m
        self.emin = 1 - bias

    def value(self, x):
        """None for NaN, 'inf' with sign, else Fraction"""
        s = (x >> (self.e + self.m)) & 1
        e = (x >> self.m) & mask(self.e)
        m = x & mask(self.m)
        if self.nan == "ieee" and e == mask(self.e):
            if m:
                return None
            return ("-inf" if s else "inf")
        if self.nan == "e4m3" and e == mask(self.e) and m == mask(self.m):
            return None
        if e == 0:
            v = Fraction(m, 1 << self.m) * Fraction(2) ** self.emin
        else:
            v = (1 + Fraction(m, 1 << self.m)) * Fraction(2) ** (e - self.bias)
        return -v if s else v

    def maxv(self):
        if self.nan == "ieee":
            return (2 - Fraction(1, 1 << self.m)) * Fraction(2) ** (mask(self.e) - 1 - self.bias)
        if self.nan == "e4m3":
            return (1 + Fraction(mask(self.m) - 1, 1 << self.m)) * Fraction(2) ** (mask(self.e) - self.bias)
        return (2 - Fraction(1, 1 << self.m)) * Fraction(2) ** (mask(self.e) - self.bias)

    def grid(self):
        """every non-negative finite value -> encoding"""
        out = {}
        for x in range(1 << (self.e + self.m)):
            v = self.value(x)
            if isinstance(v, Fraction):
                out.setdefault(v, x)
        return out


E5M2 = F("E5M2", 5, 2, 15, True, "ieee")
E4M3 = F("E4M3", 4, 3, 7, False, "e4m3")
E3M2 = F("E3M2", 3, 2, 3, False, None)
E2M3 = F("E2M3", 2, 3, 1, False, None)
E2M1 = F("E2M1", 2, 1, 1, False, None)
GRIDS = {}


def grid(f):
    if f.name not in GRIDS:
        g = f.grid()
        GRIDS[f.name] = (sorted(g), g)
    return GRIDS[f.name]


def floor_log2(q):
    e = q.numerator.bit_length() - q.denominator.bit_length()
    while Fraction(2) ** e > q:
        e -= 1
    while Fraction(2) ** (e + 1) <= q:
        e += 1
    return e


def round_q(q, f, rnd):
    """q > 0 rational rounded to f's precision with gradual underflow and an UNBOUNDED exponent
    (2.6: RTNE ties to even, RTO = round to odd, RZ = truncation); returns the rational"""
    qe = max(floor_log2(q), f.emin)
    quantum = Fraction(2) ** (qe - f.m)
    t = q / quantum
    lo = t.numerator // t.denominator
    frac = t - lo
    if frac == 0 or rnd == "RZ":
        n = lo
    elif rnd == "RTO":
        n = lo if lo & 1 else lo + 1
    elif frac > Fraction(1, 2) or (frac == Fraction(1, 2) and lo & 1):
        n = lo + 1
    else:
        n = lo
    return n * quantum


def encode_q(r, f):
    return grid(f)[1][r] if r else 0


def fp32_value(i):
    s, e, m = (i >> 31) & 1, (i >> 23) & 0xFF, i & 0x7FFFFF
    if e == 0xFF:
        return None if m else ("-inf" if s else "inf")
    v = Fraction(m, 1 << 23) * Fraction(2) ** -126 if e == 0 else (1 + Fraction(m, 1 << 23)) * Fraction(2) ** (e - 127)
    return -v if s else v


def nat_fp32_to_fp8(i, f, sat, rnd):
    """9.2.1: MXCSR not used, DAZ = 1 (FP32 denormals are zeros), SAT / NONSAT table after rounding"""
    s = (i >> 31) & 1
    sign = s << 7
    e, m = (i >> 23) & 0xFF, i & 0x7FFFFF
    if e == 0xFF and m:                                     # NaN: quiet, upper payload bits
        if f is E5M2:
            return sign | 0x7C | 0x2 | ((m >> 21) & 1)
        return sign | 0x7F
    if e == 0xFF:                                           # +-Inf
        if sat:
            return sign | (0x7B if f is E5M2 else 0x7E)
        return sign | (0x7C if f is E5M2 else 0x7F)
    if e == 0:
        return sign
    r = round_q(abs(fp32_value(i)), f, rnd)
    if r > f.maxv():                                        # 9.2.1: greater than max OFP8 magnitude
        if sat:
            return sign | encode_q(f.maxv(), f)
        return sign | (0x7C if f is E5M2 else 0x7F)
    return sign | encode_q(r, f)


def nat_fp32_bias(i, f, sat, byte):
    """bias rounding of 2.6.3 as SDE resolves G1/G5: floor(|x| / LSB + byte / 256) on the destination
    grid (LSB of the binade, or of the subnormals), then the SAT / NONSAT rule of 9.2.1"""
    s = (i >> 31) & 1
    sign = s << 7
    e, m = (i >> 23) & 0xFF, i & 0x7FFFFF
    if e == 0xFF:
        return nat_fp32_to_fp8(i, f, sat, "RTNE")
    if e == 0:
        return sign
    q = abs(fp32_value(i))
    lsb = Fraction(2) ** (max(floor_log2(q), f.emin) - f.m)
    t = q / lsb + Fraction(byte, 256)
    r = (t.numerator // t.denominator) * lsb
    if r > f.maxv():
        if sat:
            return sign | encode_q(f.maxv(), f)
        return sign | (0x7C if f is E5M2 else 0x7F)
    return sign | encode_q(r, f)


def nat_fp8_to_fp32(x, f):
    v = f.value(x)
    s = (x >> 7) & 1
    if v is None:
        if f is E5M2:
            return (s << 31) | 0x7F800000 | (((x & 3) | 2) << 21)
        return (s << 31) | 0x7F800000 | (7 << 20)
    if isinstance(v, str):
        return (s << 31) | 0x7F800000
    if v == 0:
        return s << 31
    q = abs(v)
    e = q.numerator.bit_length() - q.denominator.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    frac = q / Fraction(2) ** e - 1
    mant = frac * (1 << 23)
    assert mant.denominator == 1
    return (s << 31) | ((e + 127) << 23) | int(mant)


def nat_narrow(x, fi, fo, daz=True):
    """FP8 -> FP6 / FP4: RTNE, saturating (NaN/Inf/too big -> +-max), FP8 subnormals as zeros"""
    s = (x >> (fi.bits - 1)) & 1
    sign = s << (fo.bits - 1)
    v = fi.value(x)
    if v is None or isinstance(v, str):
        return sign | grid(fo)[1][fo.maxv()]
    e = (x >> fi.m) & mask(fi.e)
    if daz and e == 0:
        return sign
    r = round_q(abs(v), fo, "RTNE")
    if r > fo.maxv():
        return sign | encode_q(fo.maxv(), fo)
    return sign | encode_q(r, fo)


def nat_widen(x, fi, fo):
    """FP4 / FP6 -> E4M3, exact"""
    s = (x >> (fi.bits - 1)) & 1
    q = abs(fi.value(x))
    return (s << 7) | grid(fo)[1][q]


# ======================================================================================
# instruction models (bytes images, lane 0 first)
# ======================================================================================
def d32(buf, i):
    return int.from_bytes(buf[4 * i:4 * i + 4], "little")


def cvt_ps2f8(fmt, sat, rnd, old, src, bias, vl, kmask, zero):
    """VCVT[RO,BIAS]PS2[B,H]F8[S] (9.2.5): VL/32 elements; dest[MAXVL-1:VL/4] = 0"""
    n = vl // 4
    out = bytearray(64)
    for i in range(n):
        if kmask is None or (kmask >> i) & 1:
            t = d32(src, i)
            b = bias_arg(fmt, d32(bias, i)) if bias is not None else 0
            out[i] = (lit_fp32_to_e5m2 if fmt == "BF8" else lit_fp32_to_e4m3)(t, sat, rnd, b)
        elif not zero:
            out[i] = old[i]
    return bytes(out)


def cvt_f82ps(fmt, old, src, vl, kmask, zero):
    n = vl // 4
    out = bytearray(64)
    for i in range(n):
        if kmask is None or (kmask >> i) & 1:
            r = (lit_e5m2_to_fp32 if fmt == "BF8" else lit_e4m3_to_fp32)(src[i])
        else:
            r = 0 if zero else d32(old, i)
        out[4 * i:4 * i + 4] = r.to_bytes(4, "little")
    return bytes(out)


def cvt_f82bf4(fmt, src, vl):
    """dest[4i+3:4i]; dest[MAXVL-1:VL/2] = 0 (bits): VL/16 result bytes"""
    out = bytearray(64)
    for i in range(vl):
        r = (lit_e5m2_to_e2m1 if fmt == "BF8" else lit_e4m3_to_e2m1)(src[i])
        out[i // 2] |= r << (4 * (i & 1))
    return bytes(out)


def cvt_bf42hf8(old, src, vl, kmask, zero):
    out = bytearray(64)
    for i in range(vl):
        if kmask is None or (kmask >> i) & 1:
            out[i] = lit_e2m1_to_e4m3((src[i // 2] >> (4 * (i & 1))) & 0xF)
        elif not zero:
            out[i] = old[i]
    return bytes(out)


def bits_get(buf, pos, n):
    v = int.from_bytes(buf, "little")
    return (v >> pos) & mask(n)


def cvt_f82f6(fmt, src, vl):
    """dest[6i+5:6i]; dest[MAXVL-1:VL*6/8] = 0"""
    acc = 0
    for i in range(vl):
        r = (lit_e5m2_to_e3m2 if fmt == "BF8" else lit_e4m3_to_e2m3)(src[i])
        acc |= r << (6 * i)
    return acc.to_bytes(64, "little")


def cvt_f62hf8(fmt, old, src, vl, kmask, zero):
    out = bytearray(64)
    for i in range(vl):
        if kmask is None or (kmask >> i) & 1:
            t = bits_get(src, 6 * i, 6)
            out[i] = (lit_e3m2_to_e4m3 if fmt == "BF6" else lit_e2m3_to_e4m3)(t)
        elif not zero:
            out[i] = old[i]
    return bytes(out)


def pmovssdb(old, src, vl, kmask, zero, nbytes=64):
    n = vl // 4
    out = bytearray(old[:nbytes]) if nbytes != 64 else bytearray(64)
    for i in range(n):
        if kmask is None or (kmask >> i) & 1:
            out[i] = sym_sat_byte(d32(src, i))
        elif nbytes == 64:
            out[i] = 0 if zero else old[i]
    return bytes(out)


def unpackb_params(imm):
    sign_ex = (imm >> 5) & 1
    size = max((imm >> 2) & 7, 2)
    if size == 2:
        start = imm & 3
    elif size in (3, 4):
        start = min(imm & 3, 1)
    else:
        start = 0
    return size, start, sign_ex


def vunpackb(old, src, imm, vl, kmask, zero):
    size, start, sx = unpackb_params(imm)
    kl = vl
    data = int.from_bytes(src[:vl], "little")
    out = bytearray(64)
    for i in range(kl):
        if kmask is None or (kmask >> i) & 1:
            j = (start * kl + i) * size
            e = (data >> j) & mask(size)
            if sx and (e >> (size - 1)) & 1:
                e |= mask(8) & ~mask(size)
            out[i] = e
        elif not zero:
            out[i] = old[i]
    return bytes(out)


def nat_vunpackb(src, imm, vl):
    """the description of 9.9.1 (size [2..7], start offset in units of a full VL/8-item block)"""
    size, start, sx = unpackb_params(imm)
    data = int.from_bytes(src[:vl], "little")
    out = bytearray(64)
    for i in range(vl):
        v = (data >> ((start * vl + i) * size)) & mask(size)
        if sx and v >= 1 << (size - 1):
            v -= 1 << size
        out[i] = v & 0xFF
    return bytes(out)


# ======================================================================================
# self test
# ======================================================================================
def fp32_samples():
    out = set()
    for e in list(range(90, 160)) + [0, 1, 254, 255]:
        for m in (0, 1, 0x7FFFFF, 0x400000, 0x200000, 0x100000, 0x080000, 0x0FFFFF, 0x1FFFFF, 0x080001,
                  0x100001, 0x180000, 0x280000, 0x300000, 0x380000, 0x600000, 0x700000, 0x780000, 0x6FFFFF,
                  0x0C0000, 0x140000, 0x040000, 0x020000, 0x010000, 0x060000, 0x0A0000):
            for s in (0, 1):
                out.add((s << 31) | (e << 23) | m)
    r = random.Random(1234)
    for _ in range(40000):
        out.add((r.getrandbits(1) << 31) | (r.randint(95, 160) << 23) | r.getrandbits(23))
    return sorted(out)


def selftest():
    ok = True
    n = 0

    def chk(name, got, want):
        nonlocal ok, n
        n += 1
        if got != want:
            ok = False
            print("FAIL %s: got %r want %r" % (name, got, want))

    # hand-derived values (ACE 2.4 tables)
    chk("e5m2 max", lit_fp32_to_e5m2(0x47600000, False, "RTNE"), 0x7B)          # 57344
    chk("e5m2 inf nonsat", lit_fp32_to_e5m2(0x7F800000, False, "RTNE"), 0x7C)
    chk("e5m2 inf sat", lit_fp32_to_e5m2(0xFF800000, True, "RTNE"), 0xFB)
    chk("e5m2 qnan", lit_fp32_to_e5m2(0x7FC00000, False, "RTNE"), 0x7E)
    chk("e5m2 snan payload", lit_fp32_to_e5m2(0x7FA00000, True, "RTNE"), 0x7F)
    chk("e5m2 1.0", lit_fp32_to_e5m2(0x3F800000, False, "RTNE"), 0x3C)
    chk("e5m2 min sub 2^-16", lit_fp32_to_e5m2(0x37800000, False, "RTNE"), 0x01)
    chk("e5m2 2^-17 tie -> 0", lit_fp32_to_e5m2(0x37000000, False, "RTNE"), 0x00)
    chk("e4m3 448", lit_fp32_to_e4m3(0x43E00000, False, "RTNE"), 0x7E)
    chk("e4m3 464 tie -> 448", lit_fp32_to_e4m3(0x43E80000, True, "RTNE"), 0x7E)
    chk("e4m3 465 nonsat NaN", lit_fp32_to_e4m3(0x43E88000, False, "RTNE"), 0x7F)
    chk("e4m3 inf nonsat NaN", lit_fp32_to_e4m3(0x7F800000, False, "RTNE"), 0x7F)
    chk("e4m3 inf sat max", lit_fp32_to_e4m3(0xFF800000, True, "RTNE"), 0xFE)
    chk("e4m3 2^-9", lit_fp32_to_e4m3(0x3B000000, False, "RTNE"), 0x01)
    chk("e4m3 RTO 1.0625 -> 1.125", lit_fp32_to_e4m3(0x3F880000, False, "RTO"), 0x39)
    chk("e4m3 RTO tiny -> min sub", lit_fp32_to_e4m3(0x30000000, False, "RTO"), 0x01)
    chk("e5m2 bias carry", lit_fp32_to_e5m2(0x3F900000, False, "BIAS", 0x100000), 0x3D)   # 1.125+0.125
    chk("e4m3 bias trunc", lit_fp32_to_e4m3(0x3F8FFFFF, False, "BIAS", 0), 0x38)
    chk("e4m3 bias sat NaN-code clamp (R1)", lit_fp32_to_e4m3(0x43F00000, True, "BIAS", 0), 0x7E)
    chk("e4m3 bias sat printed (G3)", lit_fp32_to_e4m3(0x43F00000, True, "BIAS", 0, False), 0x7F)
    chk("e4m3->fp32 448", lit_e4m3_to_fp32(0x7E), 0x43E00000)
    chk("e4m3->fp32 NaN", lit_e4m3_to_fp32(0xFF), 0xFFF00000)
    chk("e5m2->fp32 2^-16", lit_e5m2_to_fp32(0x01), 0x37800000)
    chk("fp4 6.0", lit_e2m1_to_e4m3(0x7), 0x4C)
    chk("fp4 -0.5", lit_e2m1_to_e4m3(0x9), 0xB0)
    chk("e4m3->fp4 5.0 tie -> 4", lit_e4m3_to_e2m1(0x4A), 0x6)
    chk("e4m3->fp4 7.0 -> 6", lit_e4m3_to_e2m1(0x4E), 0x7)
    chk("pmovssdb -128", sym_sat_byte(0xFFFFFF80), 0x81)
    chk("pmovssdb 1000", sym_sat_byte(1000), 0x7F)
    chk("pmovssdb -5", sym_sat_byte(0xFFFFFFFB), 0xFB)

    # FP32 -> FP8: literal vs rational
    diffs = {}
    for i in fp32_samples():
        for f, lf in ((E5M2, lit_fp32_to_e5m2), (E4M3, lit_fp32_to_e4m3)):
            for sat in (False, True):
                for rnd in (("RTNE",) if f is E5M2 else ("RTNE", "RTO")):
                    a, b = lf(i, sat, rnd), nat_fp32_to_fp8(i, f, sat, rnd)
                    if a != b:
                        diffs.setdefault((f.name, sat, rnd), []).append((i, a, b))
                    n += 1
                # bias 0 = truncation toward zero, overflow as SAT/NONSAT; printed helper vs RZ
                a = lf(i, sat, "BIAS", 0, resolve=False)
                b = nat_fp32_to_fp8(i, f, sat, "RZ")
                if a != b:
                    diffs.setdefault((f.name, sat, "BIAS0 printed"), []).append((i, a, b))
                # resolved (G1/G5) helper vs the rational bias rounding, several bias bytes
                for byte in (0, 1, 0x7F, 0x80, 0xA5, 0xFF, (i * 0x9E3779B1 >> 24) & 0xFF):
                    a = lf(i, sat, "BIAS", bias_arg("BF8" if f is E5M2 else "HF8", byte))
                    b = nat_fp32_bias(i, f, sat, byte)
                    if a != b:
                        diffs.setdefault((f.name, sat, "BIAS resolved"), []).append((i, a, b))
    for k, v in sorted(diffs.items()):
        print("NOTE literal != rational for FP32->%s sat=%s %s: %d inputs, e.g. %s" %
              (k[0], k[1], k[2], len(v), ", ".join("%08X: %02X vs %02X" % t for t in v[:4])))
    for k in diffs:
        if k[2] != "BIAS0 printed":
            ok = False                                      # RTNE / RTO / resolved BIAS must match

    # FP8 -> FP32 exhaustive
    for x in range(256):
        chk("e5m2->fp32 %02X" % x, lit_e5m2_to_fp32(x), nat_fp8_to_fp32(x, E5M2))
        chk("e4m3->fp32 %02X" % x, lit_e4m3_to_fp32(x), nat_fp8_to_fp32(x, E4M3))
    # FP8 -> FP4 / FP6 exhaustive
    for x in range(256):
        chk("e5m2->e2m1 %02X" % x, lit_e5m2_to_e2m1(x), nat_narrow(x, E5M2, E2M1))
        chk("e4m3->e2m1 %02X" % x, lit_e4m3_to_e2m1(x), nat_narrow(x, E4M3, E2M1))
        chk("e5m2->e3m2 %02X" % x, lit_e5m2_to_e3m2(x), nat_narrow(x, E5M2, E3M2))
        chk("e4m3->e2m3 %02X" % x, lit_e4m3_to_e2m3(x), nat_narrow(x, E4M3, E2M3))
    for x in range(16):
        chk("e2m1->e4m3 %X" % x, lit_e2m1_to_e4m3(x), nat_widen(x, E2M1, E4M3))
    for x in range(64):
        chk("e3m2->e4m3 %02X" % x, lit_e3m2_to_e4m3(x), nat_widen(x, E3M2, E4M3))
        chk("e2m3->e4m3 %02X" % x, lit_e2m3_to_e4m3(x), nat_widen(x, E2M3, E4M3))
    # VUNPACKB: pseudocode vs description, every imm8 with size >= 2
    r = random.Random(77)
    for imm in range(256):
        for vl in (16, 32, 64):
            src = bytes(r.getrandbits(8) for _ in range(64))
            chk("vunpackb %02X vl%d" % (imm, vl), vunpackb(bytes(64), src, imm, vl, None, 0)[:vl],
                nat_vunpackb(src, imm, vl)[:vl])
    print("selftest: %d checks" % n)
    return ok


# ======================================================================================
# cases
# ======================================================================================
def emit(c):
    cases.append(c)


def comment(t):
    cases.append("# " + t)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


F32_SPECIAL = [0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000,
               0x7FC00000, 0xFFC00001, 0x7FA00000, 0x7F800001, 0x47600000, 0x47700000, 0x47800000, 0x477FF000,
               0x43E00000, 0x43E80000, 0x43E80001, 0x43F00000, 0x43F80000, 0x44000000, 0xC3F00000,
               0x37800000, 0x37000000, 0x37000001, 0x37C00000, 0x3B000000, 0x3A800000, 0x3A800001, 0x3AC00000,
               0x3C800000, 0x387FFFFF, 0x3F880000, 0x3F900000, 0x3F980000, 0x3FA00000, 0x40400000, 0x41B00000,
               0x30000000, 0x00800000, 0x7F7FFFFF]


def rnd_f32():
    k = RNG.random()
    s = RNG.getrandbits(1) << 31
    if k < 0.3:
        return RNG.choice(F32_SPECIAL) ^ s
    if k < 0.85:
        e = RNG.randint(103, 145)                           # around the FP8 ranges
    else:
        e = RNG.randint(0, 255)
    return s | (e << 23) | RNG.getrandbits(23)


def f32_vec(n):
    return b"".join(rnd_f32().to_bytes(4, "little") for _ in range(n))


FP8_SPECIAL = [0x00, 0x80, 0x01, 0x81, 0x03, 0x04, 0x07, 0x08, 0x3C, 0x38, 0x7B, 0xFB, 0x7C, 0xFC, 0x7D, 0x7E,
               0x7F, 0xFF, 0x7A, 0x77, 0x47, 0x4A, 0x4B, 0x4C, 0x4E, 0x50, 0x56, 0x30, 0x2C, 0x28, 0x34, 0x44]


def fp8_vec(n):
    return bytes(RNG.choice(FP8_SPECIAL) if RNG.random() < 0.35 else RNG.getrandbits(8) for _ in range(n))


def kmask_for(n):
    k = RNG.getrandbits(64) & mask(n)
    return k


def mem_case(c, off, data):
    c.mem[MEM_RSI + off] = data


MEM_OFFS = [0, 64, -16, 0x100, 8]


# --- FP32 -> FP8 (9.2) ----------------------------------------------------------------
PS2F8 = [  # name, pp, opc, fmt, sat, rnd, bias form
    ("VCVTPS2BF8", 2, 0x39, "BF8", False, "RTNE", False),
    ("VCVTPS2BF8S", 2, 0x3B, "BF8", True, "RTNE", False),
    ("VCVTPS2HF8", 2, 0x38, "HF8", False, "RTNE", False),
    ("VCVTPS2HF8S", 2, 0x3A, "HF8", True, "RTNE", False),
    ("VCVTROPS2HF8", 1, 0x38, "HF8", False, "RTO", False),
    ("VCVTROPS2HF8S", 1, 0x3A, "HF8", True, "RTO", False),
    ("VCVTBIASPS2BF8", 0, 0x39, "BF8", False, "BIAS", True),
    ("VCVTBIASPS2BF8S", 0, 0x3B, "BF8", True, "BIAS", True),
    ("VCVTBIASPS2HF8", 0, 0x38, "HF8", False, "BIAS", True),
    ("VCVTBIASPS2HF8S", 0, 0x3A, "HF8", True, "BIAS", True),
]


def bias_vec(n, fmt):
    out = []
    for _ in range(n):
        k = RNG.random()
        if k < 0.2:
            out.append(RNG.choice([0, 0xFFFFFFFF, 0x000FFFFF, 0x001FFFFF, 0x00080000, 0x00100000, 0xFFF00000]))
        else:
            out.append(RNG.getrandbits(32))
    return b"".join(v.to_bytes(4, "little") for v in out)


def gen_ps2f8():
    comment("=== FP32 -> FP8 (ACE 9.2, helpers 16.1): MAP5 W0; E4 (fault suppression), m32bcst, no MXCSR")
    for name, pp, opc, fmt, sat, rnd, isb in PS2F8:
        comment("%s (EVEX.%s.MAP5.W0 %02X)" % (name, ["NP", "66", "F3", "F2"][pp], opc))
        for vl in (16, 32, 64):
            n = vl // 4
            for var in ("reg", "merge", "zero", "mem", "bcst", "hi"):
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                dst, s1, s2 = (1, 2, 3) if var != "hi" else (29, 18, 20)
                old = rnd_bytes(64)
                src = f32_vec(16)
                bias = bias_vec(16, fmt) if isb else None
                kmask, zero, aaa = None, 0, 0
                if var in ("merge", "zero", "hi"):
                    aaa = 5
                    kmask = kmask_for(n)
                    c.k[aaa] = kmask
                    zero = 1 if var == "zero" else 0
                c.set_zmm(dst, old)
                if isb:
                    c.set_zmm(s1, bias + bytes(0))
                b = 0
                if var in ("mem", "bcst"):
                    b = 1 if var == "bcst" else 0
                    off = RNG.choice(MEM_OFFS)
                    data = src[:4] if b else src[:vl]
                    mem_case(c, off, data)
                    rm = Mem(RSI, off)
                    srcv = (data * (vl // 4))[:vl] + bytes(64 - vl) if b else data + bytes(64 - vl)
                    nsc = 4 if b else vl
                else:
                    rm, nsc = s2, 1
                    srcv = src
                    c.set_zmm(s2, src)
                img = cvt_ps2f8(fmt, sat, rnd, old, srcv, bias, vl, kmask, zero)
                c.code = evex(5, pp, 0, opc, dst, rm, vvvv=s1 if isb else None, ll=VL_LL[vl], b=b, z=zero,
                              aaa=aaa, n=nsc)
                c.exp.append("zmm%d=%s" % (dst, hexs(img)))
                emit(c)
    comment("FP32 -> FP8 special inputs (every lane a listed value)")
    for name, pp, opc, fmt, sat, rnd, isb in PS2F8:
        for chunk in range(0, len(F32_SPECIAL), 16):
            vals = (F32_SPECIAL[chunk:chunk + 16] * 2)[:16]
            src = b"".join(v.to_bytes(4, "little") for v in vals)
            c = Case("%s specials %d" % (name, chunk // 16))
            bias = bias_vec(16, fmt) if isb else None
            c.set_zmm(1, rnd_bytes(64))
            c.set_zmm(3, src)
            if isb:
                c.set_zmm(2, bias)
            c.code = evex(5, pp, 0, opc, 1, 3, vvvv=2 if isb else None, ll=2)
            c.exp.append("zmm1=%s" % hexs(cvt_ps2f8(fmt, sat, rnd, bytes(64), src, bias, 64, None, 0)))
            emit(c)
    comment("G1 (SDE-resolved): the bias of element i is SRC1.dword[i][7:0] << 13 (BF8) / << 12 (HF8)")
    for name, pp, opc, fmt, sat, rnd, isb in PS2F8[6:]:
        c = Case("%s G1 bias per dword lane" % name)
        vals = [0x3F900000 + (i << 17) for i in range(16)]
        src = b"".join(v.to_bytes(4, "little") for v in vals)
        bias = b"".join(((0x1FFFFF if i & 1 else 0) | (0xABC00000 if i & 2 else 0)).to_bytes(4, "little")
                        for i in range(16))
        c.set_zmm(4, rnd_bytes(64)); c.set_zmm(5, bias); c.set_zmm(6, src)
        c.code = evex(5, pp, 0, opc, 4, 6, vvvv=5, ll=2)
        c.exp.append("zmm4=%s" % hexs(cvt_ps2f8(fmt, sat, rnd, bytes(64), src, bias, 64, None, 0)))
        emit(c)
    comment("R1 (G3): FP32 -> E4M3 BIAS saturating, a result in the S.1111.111 encoding -> +-max_normal")
    for name in ("VCVTBIASPS2HF8S", "VCVTBIASPS2HF8"):
        f = [x for x in PS2F8 if x[0] == name][0]
        c = Case("%s R1 [480, 512)" % name)
        vals = [0x43F00000, 0xC3F00000, 0x43F80000, 0x43FFFFFF, 0x43E80000, 0x43EFFFFF, 0x44000000, 0x3F800000] * 2
        src = b"".join(v.to_bytes(4, "little") for v in vals)
        bias = bytes(64)
        c.set_zmm(4, rnd_bytes(64)); c.set_zmm(5, bias); c.set_zmm(6, src)
        c.code = evex(5, f[1], 0, f[2], 4, 6, vvvv=5, ll=2)
        c.exp.append("zmm4=%s" % hexs(cvt_ps2f8("HF8", f[4], "BIAS", bytes(64), src, bias, 64, None, 0)))
        emit(c)
    comment("G4: FP32 -> E4M3 BIAS has no subnormal path (16.1: newexp <= 0 -> 0), E5M2 BIAS truncates into its subnormals")
    for name in ("VCVTBIASPS2HF8", "VCVTBIASPS2BF8S"):
        f = [x for x in PS2F8 if x[0] == name][0]
        c = Case("%s G4 subnormal range" % name)
        vals = [0x3B800000, 0x3B000000, 0x3AC00000, 0x3C000000, 0x38000000, 0x37800000, 0x37C00000, 0x3BFFFFFF,
                0xBB800000, 0x3B400000, 0x3A800000, 0x3C7FFFFF, 0x38800000, 0x37000000, 0x3B7FFFFF, 0x3BC00000]
        src = b"".join(v.to_bytes(4, "little") for v in vals)
        bias = b"".join(((0xFF if i & 1 else 0x80) | 0xA5A5A500).to_bytes(4, "little") for i in range(16))
        c.set_zmm(4, rnd_bytes(64)); c.set_zmm(5, bias); c.set_zmm(6, src)
        c.code = evex(5, f[1], 0, f[2], 4, 6, vvvv=5, ll=2)
        c.exp.append("zmm4=%s" % hexs(cvt_ps2f8(f[3], f[4], "BIAS", bytes(64), src, bias, 64, None, 0)))
        emit(c)
    comment("FP32 -> FP8: memory fault suppression (E4): only the active elements are read")
    for name, pp, opc, fmt, sat, rnd, isb in (PS2F8[0], PS2F8[8]):
        for k, fault in ((0x0000, False), (0x00FF, False), (0x0100, True)):
            c = Case("%s zmm [8 dwords mapped, 8 unmapped] k=%04X%s" % (name, k, " #PF" if fault else ""))
            old = rnd_bytes(64)
            src = f32_vec(8)
            bias = bias_vec(16, fmt) if isb else None
            c.set_zmm(1, old)
            if isb:
                c.set_zmm(2, bias)
            c.k[3] = k
            off = 0x10000 - MEM_RSI - 32
            mem_case(c, off, src)
            c.code = evex(5, pp, 0, opc, 1, Mem(RSI, off, disp32=True), vvvv=2 if isb else None, ll=2, aaa=3)
            if fault:
                c.fault = "#PF"
            else:
                c.exp.append("zmm1=%s" % hexs(cvt_ps2f8(fmt, sat, rnd, old, src + bytes(32), bias, 64, k, 0)))
            emit(c)
    comment("FP32 -> FP8 #UD: W1, EVEX.b on a register form, {z} without aaa; non-bias forms: vvvv != 1111b")
    for name, pp, opc, fmt, sat, rnd, isb in (PS2F8[0], PS2F8[4], PS2F8[6]):
        for title, kw in (("W1", dict(w=1)), ("b=1 reg", dict(b=1)), ("z aaa=0", dict(z=1))) + \
                (() if isb else (("vvvv=0011b", dict(vvvv=3)),)):
            c = Case("%s %s #UD" % (name, title))
            c.code = evex(5, pp, kw.get("w", 0), opc, 1, 2, vvvv=kw.get("vvvv", 4 if isb else None), ll=2,
                          b=kw.get("b", 0), z=kw.get("z", 0))
            c.fault = "#UD"
            emit(c)


# --- FP8 -> FP32 (9.3) ----------------------------------------------------------------
def gen_f82ps():
    comment("=== FP8 -> FP32 (ACE 9.3, helpers 16.3): MAP5 NP 36 (W1 BF8, W0 HF8); Quarter Mem, E4")
    for name, w, fmt in (("VCVTBF82PS", 1, "BF8"), ("VCVTHF82PS", 0, "HF8")):
        comment("%s (EVEX.NP.MAP5.W%d 36)" % (name, w))
        for vl in (16, 32, 64):
            n = vl // 4
            for var in ("reg", "merge", "zero", "mem", "hi"):
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                dst, s2 = (1, 3) if var != "hi" else (30, 17)
                old = rnd_bytes(64)
                src = fp8_vec(16)
                kmask, zero, aaa = None, 0, 0
                if var in ("merge", "zero", "hi"):
                    aaa = 6
                    kmask = kmask_for(n)
                    c.k[aaa] = kmask
                    zero = 1 if var == "zero" else 0
                c.set_zmm(dst, old)
                if var == "mem":
                    off = RNG.choice([0, 4, -4, 32])
                    mem_case(c, off, src[:n])
                    rm = Mem(RSI, off)
                else:
                    rm = s2
                    c.set_zmm(s2, src + rnd_bytes(48))
                img = cvt_f82ps(fmt, old, src, vl, kmask, zero)
                c.code = evex(5, 0, w, 0x36, dst, rm, ll=VL_LL[vl], z=zero, aaa=aaa, n=n)
                c.exp.append("zmm%d=%s" % (dst, hexs(img)))
                emit(c)
        for chunk in range(0, 256, 16):
            src = bytes(range(chunk, chunk + 16))
            c = Case("%s all inputs %02X-%02X" % (name, chunk, chunk + 15))
            c.set_zmm(1, rnd_bytes(64)); c.set_zmm(2, src + rnd_bytes(48))
            c.code = evex(5, 0, w, 0x36, 1, 2, ll=2)
            c.exp.append("zmm1=%s" % hexs(cvt_f82ps(fmt, bytes(64), src, 64, None, 0)))
            emit(c)
    comment("FP8 -> FP32: fault suppression; #UD for EVEX.b (no broadcast form), vvvv != 1111b")
    off = 0x10000 - MEM_RSI - 8
    for k, fault in ((0x00FF, False), (0x01FF, True)):
        c = Case("VCVTHF82PS zmm, [8 bytes mapped, 8 unmapped] k=%04X%s" % (k, " #PF" if fault else ""))
        old = rnd_bytes(64)
        src = fp8_vec(8)
        c.set_zmm(1, old)
        c.k[2] = k
        mem_case(c, off, src)
        c.code = evex(5, 0, 0, 0x36, 1, Mem(RSI, off, disp32=True), ll=2, aaa=2)
        if fault:
            c.fault = "#PF"
        else:
            c.exp.append("zmm1=%s" % hexs(cvt_f82ps("HF8", old, src + bytes(8), 64, k, 0)))
        emit(c)
    for title, kw in (("b=1 mem", dict(b=1, rm=Mem(RSI, 0))), ("b=1 reg", dict(b=1)), ("vvvv!=1111b", dict(vvvv=3))):
        c = Case("VCVTBF82PS %s #UD" % title)
        c.code = evex(5, 0, 1, 0x36, 1, kw.get("rm", 2), vvvv=kw.get("vvvv"), ll=2, b=kw.get("b", 0), n=16)
        c.fault = "#UD"
        emit(c)


# --- FP8 -> FP4 (9.4), FP4 -> FP8 (9.5) ---------------------------------------------------
def gen_fp4():
    comment("=== FP8 -> FP4 (ACE 9.4): MAP5 F3 3D (W1 BF8, W0 HF8), destination ModRM.r/m (G2): register")
    comment("    (dest[MAXVL-1:VL/2] = 0) or a VL/16-byte store, no masking (aaa / z #UD), E4NF")
    for name, w, fmt in (("VCVTBF82BF4S", 1, "BF8"), ("VCVTHF82BF4S", 0, "HF8")):
        for vl in (16, 32, 64):
            for var in ("reg", "mem", "hi"):
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                src = fp8_vec(64)
                d, s = (5, 6) if var != "hi" else (21, 13)
                c.set_zmm(s, src)
                img = cvt_f82bf4(fmt, src, vl)
                if var == "mem":
                    off = RNG.choice([0, 16, -8, 40])
                    pre = rnd_bytes(vl // 16 + 8)
                    mem_case(c, off, pre)
                    c.code = evex(5, 2, w, 0x3D, s, Mem(RSI, off), ll=VL_LL[vl], n=vl // 2)
                    c.exp.append("m+0x%X=%s" % (MEM_RSI + off, hexs(img[:vl // 2] + pre[vl // 2:])))
                else:
                    c.set_zmm(d, rnd_bytes(64))
                    c.code = evex(5, 2, w, 0x3D, s, d, ll=VL_LL[vl])
                    c.exp.append("zmm%d=%s" % (d, hexs(img)))
                emit(c)
        for chunk in range(0, 256, 64):
            src = bytes(range(chunk, chunk + 64))
            c = Case("%s all inputs %02X-%02X" % (name, chunk, chunk + 63))
            c.set_zmm(6, src); c.set_zmm(5, rnd_bytes(64))
            c.code = evex(5, 2, w, 0x3D, 6, 5, ll=2)
            c.exp.append("zmm5=%s" % hexs(cvt_f82bf4(fmt, src, 64)))
            emit(c)
        for title, kw in (("aaa=1", dict(aaa=1)), ("z", dict(z=1, aaa=1)), ("b=1", dict(b=1)), ("vvvv!=1111b", dict(vvvv=3))):
            c = Case("%s %s #UD" % (name, title))
            c.code = evex(5, 2, w, 0x3D, 6, 5, vvvv=kw.get("vvvv"), ll=2, b=kw.get("b", 0), z=kw.get("z", 0),
                          aaa=kw.get("aaa", 0))
            c.fault = "#UD"
            emit(c)
    comment("    store without fault suppression: the whole operand faults")
    c = Case("VCVTHF82BF4S [VL/16 bytes crossing into the unmapped page] #PF")
    c.set_zmm(6, fp8_vec(64))
    c.code = evex(5, 2, 0, 0x3D, 6, Mem(RSI, 0x10000 - MEM_RSI - 8, disp32=True), ll=2)
    c.fault = "#PF"
    emit(c)

    comment("=== FP4 -> FP8 (ACE 9.5): VCVTBF42HF8 MAP5 NP W0 37, Half Mem source, {k}{z}, E4NF")
    for vl in (16, 32, 64):
        for var in ("reg", "merge", "zero", "mem", "hi"):
            c = Case("VCVTBF42HF8 VL%d %s" % (vl * 8, var))
            d, s = (7, 8) if var != "hi" else (26, 11)
            old = rnd_bytes(64)
            src = rnd_bytes(32)
            kmask, zero, aaa = None, 0, 0
            if var in ("merge", "zero", "hi"):
                aaa = 1
                kmask = kmask_for(vl)
                c.k[aaa] = kmask
                zero = 1 if var == "zero" else 0
            c.set_zmm(d, old)
            if var == "mem":
                off = RNG.choice([0, 8, -32, 24])
                mem_case(c, off, src[:vl // 2])
                rm = Mem(RSI, off)
            else:
                rm = s
                c.set_zmm(s, src + rnd_bytes(32))
            c.code = evex(5, 0, 0, 0x37, d, rm, ll=VL_LL[vl], z=zero, aaa=aaa, n=vl // 2)
            c.exp.append("zmm%d=%s" % (d, hexs(cvt_bf42hf8(old, src, vl, kmask, zero))))
            emit(c)
    c = Case("VCVTBF42HF8 every nibble")
    src = bytes((i & 15) | (((15 - i) & 15) << 4) for i in range(32))
    c.set_zmm(8, src + bytes(32)); c.set_zmm(7, rnd_bytes(64))
    c.code = evex(5, 0, 0, 0x37, 7, 8, ll=2)
    c.exp.append("zmm7=%s" % hexs(cvt_bf42hf8(bytes(64), src, 64, None, 0)))
    emit(c)
    c = Case("VCVTBF42HF8 k=0 [unmapped] #PF (E4NF: no fault suppression)")
    c.k[2] = 0
    c.code = evex(5, 0, 0, 0x37, 7, Mem(RSI, 0x10000 - MEM_RSI - 16, disp32=True), ll=2, aaa=2)
    c.fault = "#PF"
    emit(c)
    for title, kw in (("W1", dict(w=1)), ("b=1", dict(b=1))):
        c = Case("VCVTBF42HF8 %s #UD" % title)
        c.code = evex(5, 0, kw.get("w", 0), 0x37, 7, 8, ll=2, b=kw.get("b", 0))
        c.fault = "#UD"
        emit(c)


# --- FP8 -> FP6 (9.6), FP6 -> FP8 (9.7) ---------------------------------------------------
def gen_fp6():
    comment("=== FP8 -> FP6 (ACE 9.6): VCVTBF82BF6S MAP5 F3 W1 3E, VCVTHF82HF6S MAP5 F3 W0 3C; register only")
    comment("    (E7NM), destination ModRM.r/m (G2), dest[MAXVL-1:VL*6/8] = 0, no masking")
    for name, w, opc, fmt in (("VCVTBF82BF6S", 1, 0x3E, "BF8"), ("VCVTHF82HF6S", 0, 0x3C, "HF8")):
        for vl in (16, 32, 64):
            for var in ("reg", "hi"):
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                d, s = (9, 10) if var == "reg" else (31, 16)
                src = fp8_vec(64)
                c.set_zmm(s, src); c.set_zmm(d, rnd_bytes(64))
                c.code = evex(5, 2, w, opc, s, d, ll=VL_LL[vl])
                c.exp.append("zmm%d=%s" % (d, hexs(cvt_f82f6(fmt, src, vl))))
                emit(c)
        for chunk in range(0, 256, 64):
            src = bytes(range(chunk, chunk + 64))
            c = Case("%s all inputs %02X-%02X" % (name, chunk, chunk + 63))
            c.set_zmm(10, src); c.set_zmm(9, rnd_bytes(64))
            c.code = evex(5, 2, w, opc, 10, 9, ll=2)
            c.exp.append("zmm9=%s" % hexs(cvt_f82f6(fmt, src, 64)))
            emit(c)
        for title, kw in (("memory operand", dict(rm=Mem(RSI, 0))), ("aaa=1", dict(aaa=1)), ("b=1", dict(b=1)),
                          ("vvvv!=1111b", dict(vvvv=3))):
            c = Case("%s %s #UD" % (name, title))
            c.code = evex(5, 2, w, opc, 10, kw.get("rm", 9), vvvv=kw.get("vvvv"), ll=2, b=kw.get("b", 0),
                          aaa=kw.get("aaa", 0), n=64)
            c.fault = "#UD"
            emit(c)
    comment("=== FP6 -> FP8 (ACE 9.7): VCVTBF62HF8 MAP5 66 W1 37, VCVTHF62HF8 MAP5 66 W0 37; register only, {k}{z}")
    for name, w, fmt in (("VCVTBF62HF8", 1, "BF6"), ("VCVTHF62HF8", 0, "HF6")):
        for vl in (16, 32, 64):
            for var in ("reg", "merge", "zero", "hi"):
                c = Case("%s VL%d %s" % (name, vl * 8, var))
                d, s = (11, 12) if var != "hi" else (24, 19)
                old = rnd_bytes(64)
                src = rnd_bytes(64)
                kmask, zero, aaa = None, 0, 0
                if var != "reg":
                    aaa = 7
                    kmask = kmask_for(vl)
                    c.k[aaa] = kmask
                    zero = 1 if var == "zero" else 0
                c.set_zmm(d, old); c.set_zmm(s, src)
                c.code = evex(5, 1, w, 0x37, d, s, ll=VL_LL[vl], z=zero, aaa=aaa)
                c.exp.append("zmm%d=%s" % (d, hexs(cvt_f62hf8(fmt, old, src, vl, kmask, zero))))
                emit(c)
        c = Case("%s every 6-bit input" % name)
        src = sum(i << (6 * i) for i in range(64)).to_bytes(64, "little")
        c.set_zmm(12, src); c.set_zmm(11, rnd_bytes(64))
        c.code = evex(5, 1, w, 0x37, 11, 12, ll=2)
        c.exp.append("zmm11=%s" % hexs(cvt_f62hf8(fmt, bytes(64), src, 64, None, 0)))
        emit(c)
        c = Case("%s memory operand #UD" % name)
        c.code = evex(5, 1, w, 0x37, 11, Mem(RSI, 0), ll=2)
        c.fault = "#UD"
        emit(c)


# --- VPMOVSSDB (9.8) ------------------------------------------------------------------
INT_SPECIAL = [0, 1, 0xFFFFFFFF, 127, 128, 0xFFFFFF81, 0xFFFFFF80, 0xFFFFFF7F, 0x7FFFFFFF, 0x80000000, 1000,
               0xFFFFFC18, 126, 0xFFFFFF82]


def gen_pmovssdb():
    comment("=== VPMOVSSDB (ACE 9.8): 0F38 F3 W0 41, dword -> byte, symmetric saturation [-127, 127];")
    comment("    destination ModRM.r/m (G2): register (dest[MAXVL-1:VL/4] = 0) or a masked VL/4-byte store (E6)")
    for vl in (16, 32, 64):
        n = vl // 4
        for var in ("reg", "merge", "zero", "mem", "memk", "hi"):
            c = Case("VPMOVSSDB VL%d %s" % (vl * 8, var))
            d, s = (13, 14) if var != "hi" else (27, 22)
            vals = [RNG.choice(INT_SPECIAL) if RNG.random() < 0.5 else RNG.getrandbits(32) for _ in range(16)]
            src = b"".join(v.to_bytes(4, "little") for v in vals)
            old = rnd_bytes(64)
            kmask, zero, aaa = None, 0, 0
            if var in ("merge", "zero", "memk", "hi"):
                aaa = 4
                kmask = kmask_for(n)
                c.k[aaa] = kmask
                zero = 1 if var == "zero" else 0
            c.set_zmm(s, src)
            if var in ("mem", "memk"):
                off = RNG.choice([0, 4, -12, 48])
                mem_case(c, off, old[:n + 4])
                c.code = evex(2, 2, 0, 0x41, s, Mem(RSI, off), ll=VL_LL[vl], aaa=aaa, n=n)
                img = pmovssdb(old[:n + 4], src, vl, kmask, 0, n + 4)
                c.exp.append("m+0x%X=%s" % (MEM_RSI + off, hexs(img)))
            else:
                c.set_zmm(d, old)
                c.code = evex(2, 2, 0, 0x41, s, d, ll=VL_LL[vl], z=zero, aaa=aaa)
                c.exp.append("zmm%d=%s" % (d, hexs(pmovssdb(old, src, vl, kmask, zero))))
            emit(c)
    comment("VPMOVSSDB: masked store fault suppression (E6); #UD: z with a memory destination, vvvv != 1111b, W1, b")
    off = 0x10000 - MEM_RSI - 8
    for k, fault in ((0x00FF, False), (0x01FF, True)):
        c = Case("VPMOVSSDB [8 bytes mapped, 8 unmapped], zmm k=%04X%s" % (k, " #PF" if fault else ""))
        src = b"".join(RNG.getrandbits(32).to_bytes(4, "little") for _ in range(16))
        pre = rnd_bytes(8)
        c.set_zmm(14, src)
        c.k[3] = k
        mem_case(c, off, pre)
        c.code = evex(2, 2, 0, 0x41, 14, Mem(RSI, off, disp32=True), ll=2, aaa=3)
        if fault:
            c.fault = "#PF"
            c.exp.append("m+0x%X=%s" % (MEM_RSI + off, hexs(pre)))
        else:
            c.exp.append("m+0x%X=%s" % (MEM_RSI + off, hexs(pmovssdb(pre, src, 32, k & 0xFF, 0, 8))))
        emit(c)
    for title, kw in (("z mem", dict(z=1, aaa=1, rm=Mem(RSI, 0))), ("vvvv!=1111b", dict(vvvv=3)), ("W1", dict(w=1)),
                      ("b=1", dict(b=1))):
        c = Case("VPMOVSSDB %s #UD" % title)
        c.code = evex(2, 2, kw.get("w", 0), 0x41, 14, kw.get("rm", 13), vvvv=kw.get("vvvv"), ll=2,
                      b=kw.get("b", 0), z=kw.get("z", 0), aaa=kw.get("aaa", 0), n=16)
        c.fault = "#UD"
        emit(c)


# --- VUNPACKB (9.9) -------------------------------------------------------------------
UNPACK_IMMS = [0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x10, 0x11, 0x14, 0x18, 0x1C, 0x28, 0x29, 0x2A, 0x2B, 0x2C,
               0x2D, 0x30, 0x31, 0x34, 0x38, 0x3C]
UNPACK_RSVD = [0x00, 0x03, 0x05, 0x0F, 0x13, 0x17, 0x1B, 0x1F, 0x23, 0xC8, 0x48, 0xFF, 0x83]


def gen_unpackb():
    comment("=== VUNPACKB (ACE 9.9): 0F3A NP W0 3D /r ib, {k}{z}, Full Mem source (no broadcast), E4NF;")
    comment("    reserved imm8 encodings follow the 9.9.4 conditioning (size >= 2, start clamped), no #UD")
    for vl in (16, 32, 64):
        for imm in UNPACK_IMMS:
            for var in (("reg", "merge", "zero", "mem") if imm in (0x0A, 0x2D, 0x34) else ("reg",)):
                c = Case("VUNPACKB VL%d imm=%02X %s" % (vl * 8, imm, var))
                d, s = 15, 16
                old = rnd_bytes(64)
                src = rnd_bytes(64)
                kmask, zero, aaa = None, 0, 0
                if var in ("merge", "zero"):
                    aaa = 2
                    kmask = kmask_for(vl)
                    c.k[aaa] = kmask
                    zero = 1 if var == "zero" else 0
                c.set_zmm(d, old)
                if var == "mem":
                    off = RNG.choice([0, 64, -16])
                    mem_case(c, off, src[:vl])
                    rm = Mem(RSI, off)
                else:
                    rm = s
                    c.set_zmm(s, src)
                c.code = evex(3, 0, 0, 0x3D, d, rm, ll=VL_LL[vl], z=zero, aaa=aaa, imm=imm, n=vl)
                c.exp.append("zmm%d=%s" % (d, hexs(vunpackb(old, src, imm, vl, kmask, zero))))
                emit(c)
    for imm in UNPACK_RSVD:
        c = Case("VUNPACKB VL512 reserved imm=%02X (conditioned, no #UD)" % imm)
        src = rnd_bytes(64)
        c.set_zmm(16, src); c.set_zmm(15, rnd_bytes(64))
        c.code = evex(3, 0, 0, 0x3D, 15, 16, ll=2, imm=imm)
        c.exp.append("zmm15=%s" % hexs(vunpackb(bytes(64), src, imm, 64, None, 0)))
        emit(c)
    c = Case("VUNPACKB k=0 [unmapped] #PF (E4NF: no fault suppression)")
    c.k[1] = 0
    c.code = evex(3, 0, 0, 0x3D, 15, Mem(RSI, 0x10000 - MEM_RSI - 32, disp32=True), ll=2, aaa=1, imm=0x08)
    c.fault = "#PF"
    emit(c)
    for title, kw in (("W1", dict(w=1)), ("b=1 mem", dict(b=1, rm=Mem(RSI, 0))), ("b=1 reg", dict(b=1)),
                      ("vvvv!=1111b", dict(vvvv=3)), ("66", dict(pp=1))):
        c = Case("VUNPACKB %s #UD" % title)
        c.code = evex(3, kw.get("pp", 0), kw.get("w", 0), 0x3D, 15, kw.get("rm", 16), vvvv=kw.get("vvvv"), ll=2,
                      b=kw.get("b", 0), imm=0x08, n=64)
        c.fault = "#UD"
        emit(c)


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_ps2f8()
        gen_f82ps()
        gen_fp4()
        gen_fp6()
        gen_pmovssdb()
        gen_unpackb()
        out = sys.stdout
        out.write("# AVX10_V2_AUX (ACE 1.15 chapter 9, ledger U1095): expected values from the independent model\n")
        out.write("# Emulator/tools/isa/ref_ace_aux.py --cases (regenerate, do not edit). The i5-13600K has none of\n")
        out.write("# these: expected-value cases only, run with AVX10.2 + AVX10_V2_AUX (UC_X86_AVX10_V2_AUX = 0x200):\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_avx10_v2aux.txt --avx10 0x202 --expect-only\n")
        out.write("# RSI = R14 = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).\n")
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
