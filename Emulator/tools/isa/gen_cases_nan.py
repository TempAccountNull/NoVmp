#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_cases_nan.py -- NaN-propagation cases (ledger U96), Python 3 stdlib only.

  python gen_cases_nan.py --hw     > Emulator/data/cases_nan.txt
      hardware cases (no "=>"): legacy SSE, VEX AVX/FMA and x87 forms of the i5-13600K with
      SNaN / QNaN / numbers in every operand position, both NaN orders, larger significand in
      either operand, equal significands with opposite signs; every operand has its own
      payload so the result names the propagated operand. Run against the host CPU:
        emu-alltest --cases Emulator\\data\\cases_nan.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt
                    --strict --xcr0 7
  python gen_cases_nan.py --evex   > Emulator/data/cases_nan_evex.txt
      EVEX expected-value cases (the host has no AVX-512) for VADD/VSUB/VMUL/VDIV/VMIN/VMAX/
      VSQRT PS/PD with NaNs in both sources; expected values from the SDM model of
      ref_evex_m1.py (Vol1 4.8.3.5 Table 4-8: SSE/AVX -> first source operand, quieted;
      MINPS/MAXPS pages: a NaN in either source -> the second source, unchanged, #I).
        emu-alltest --cases Emulator\\data\\cases_nan_evex.txt --avx512 --xcr0 0xE7 --expect-only

SDM rules exercised (Vol1 4.8.3.5, Table 4-8 "Rules for Handling NaNs"):
  SNaN and QNaN / two SNaNs / two QNaNs:
    x87 FPU                 -> the NaN with the larger significand (QNaN over SNaN), quieted
    SSE/SSE2/SSE3/SSE4.1/AVX -> the first source operand, quieted
  FMA (Vol1 14.5.2, Table 14-17): Q(x) if x (multiplicand) is a NaN, else Q(y) (multiplier),
    else Q(z); 0 * inf + QNaN -> Q(z) without #I.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_evex_m1 as m1  # noqa: E402  (evex() encoder and the SDM element models)

OUT = []


def w(line=""):
    OUT.append(line)


def hexs(buf):
    return "".join("%02X" % c for c in buf)


def pack(vals, esz):
    return b"".join((v & ((1 << (8 * esz)) - 1)).to_bytes(esz, "little") for v in vals)


# ---------------------------------------------------------------------------------------
# operand values (every operand position has its own payload)
# ---------------------------------------------------------------------------------------
V32 = dict(N1=0x3F800000, N2=0xC0000000,
           Qs=0x7FC11111, QL=0xFFE22222, Ss=0x7F833333, SL=0xFFB44444,
           Qtp=0x7FC55555, Qtn=0xFFC55555, Stp=0x7F955555, Stn=0xFF955555)
V64 = dict(N1=0x3FF0000000000000, N2=0xC000000000000000,
           Qs=0x7FF8111111111111, QL=0xFFFC222222222222, Ss=0x7FF0333333333333,
           SL=0xFFF6444444444444,
           Qtp=0x7FF8555555555555, Qtn=0xFFF8555555555555,
           Stp=0x7FF2555555555555, Stn=0xFFF2555555555555)
# (SRC1, SRC2) of the two-source operations: 16 lanes
PAIRS = [("N1", "Qs"), ("N1", "Ss"), ("Qs", "N2"), ("Ss", "N2"),
         ("Qs", "QL"), ("QL", "Qs"), ("Ss", "SL"), ("SL", "Ss"),
         ("Ss", "QL"), ("SL", "Qs"), ("Qs", "SL"), ("QL", "Ss"),
         ("Qtn", "Qtp"), ("Qtp", "Qtn"), ("Stn", "Stp"), ("Stp", "Stn")]
# one-source operations: 8 lanes
SINGLE = ["N1", "Qs", "Ss", "QL", "SL", "Qtn", "Stn", "N2"]
# junk for the untouched upper elements of scalar forms
J32 = [0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777]
J64 = [0x1111111111111111, 0x2222222222222222, 0x3333333333333333]


def pairs_vals(esz):
    v = V64 if esz == 8 else V32
    return [(v[a], v[b]) for a, b in PAIRS]


def chunks(lst, n):
    return [lst[i:i + n] for i in range(0, len(lst), n)]


def regkeys(reg, img):
    """xmmN= (and ymmhN= for a 32-byte image)"""
    s = "xmm%d=%s" % (reg, hexs(img[:16]))
    if len(img) > 16:
        s += " ymmh%d=%s" % (reg, hexs(img[16:32]))
    return s


# ---------------------------------------------------------------------------------------
# hardware cases
# ---------------------------------------------------------------------------------------
def two_src_packed(mn, esz, vex, vlb, imm=None):
    """mn xmm0, xmm1 (legacy: SRC1 = xmm0) / mn xmm0, xmm1, xmm2 (VEX: SRC1 = xmm1)"""
    n = vlb // esz
    r = "ymm" if vlb == 32 else "xmm"
    sfx = "" if imm is None else ", 0x%X" % imm
    for grp in chunks(pairs_vals(esz), n):
        a = pack([p[0] for p in grp], esz)
        b = pack([p[1] for p in grp], esz)
        if vex:
            w("%s %s0, %s1, %s2%s | %s %s %s" % (mn, r, r, r, sfx, regkeys(0, bytes(range(0x40, 0x40 + vlb))),
                                                regkeys(1, a), regkeys(2, b)))
        else:
            w("%s xmm0, xmm1%s | %s %s" % (mn, sfx, regkeys(0, a), regkeys(1, b)))


def two_src_scalar(mn, esz, vex, imm=None):
    junk = J64[:1] if esz == 8 else J32[:3]
    junk2 = J64[1:2] if esz == 8 else J32[3:6]
    sfx = "" if imm is None else ", 0x%X" % imm
    for a, b in pairs_vals(esz):
        x1 = pack([a] + junk, esz)
        x2 = pack([b] + junk2, esz)
        if vex:
            w("%s xmm0, xmm1, xmm2%s | xmm0=%s xmm1=%s xmm2=%s" % (mn, sfx, hexs(bytes(range(0x80, 0x90))),
                                                                    hexs(x1), hexs(x2)))
        else:
            w("%s xmm0, xmm1%s | xmm0=%s xmm1=%s" % (mn, sfx, hexs(x1), hexs(x2)))


def comi(mn, esz, vex):
    for a, b in pairs_vals(esz):
        w("%s xmm0, xmm1 | xmm0=%s xmm1=%s" % (mn, hexs(pack([a], esz)), hexs(pack([b], esz))))


def one_src(mn, esz_in, vex, vlb, imm=None, scalar=False):
    v = V64 if esz_in == 8 else V32
    vals = [v[k] for k in SINGLE]
    sfx = "" if imm is None else ", 0x%X" % imm
    r = "ymm" if vlb == 32 else "xmm"
    if scalar:
        junk = J64[:1] if esz_in == 8 else J32[:3]
        for x in vals:
            src = pack([x] + junk, esz_in)
            if vex:
                w("%s xmm0, xmm1, xmm2%s | xmm0=%s xmm1=%s xmm2=%s" % (mn, sfx, hexs(bytes(range(0x80, 0x90))),
                                                                        hexs(pack(J32[3:7], 4)), hexs(src)))
            else:
                w("%s xmm0, xmm1%s | xmm0=%s xmm1=%s" % (mn, sfx, hexs(pack(J32[:4], 4)), hexs(src)))
        return
    n = vlb // esz_in
    for grp in chunks(vals, n):
        src = pack(grp, esz_in)
        if vex:
            w("%s %s0, %s1%s | %s" % (mn, r, r, sfx, regkeys(1, src)))
        else:
            w("%s xmm0, xmm1%s | %s" % (mn, sfx, regkeys(1, src)))


def horizontal(mn, esz, vex, vlb):
    """HADD/HSUB: SRC1 pairs (lane 2i, 2i+1), then SRC2 pairs"""
    flat = []
    for a, b in pairs_vals(esz):
        flat += [a, b]
    n = vlb // esz
    r = "ymm" if vlb == 32 else "xmm"
    for grp in chunks(flat, 2 * n):
        if len(grp) < 2 * n:
            break
        a, b = pack(grp[:n], esz), pack(grp[n:], esz)
        if vex:
            w("%s %s0, %s1, %s2 | %s %s" % (mn, r, r, r, regkeys(1, a), regkeys(2, b)))
        else:
            w("%s xmm0, xmm1 | %s %s" % (mn, regkeys(0, a), regkeys(1, b)))


def dp(mn, esz, vex, vlb):
    """DPPS/DPPD with ONE NaN product per dot product: (SRC1, SRC2) of PAIRS in element k,
    numbers elsewhere; the product mask with and without element k. Two or more NaN products
    are left out: the i5-13600K does not follow the SDM pseudocode there (see DP_NOTE)."""
    nl = 16 // esz                       # elements per 128-bit lane
    r = "ymm" if vlb == 32 else "xmm"
    v = V64 if esz == 8 else V32
    full = 0x33 if esz == 8 else 0xFF
    for pi, (pa, pb) in enumerate(pairs_vals(esz)):
        for k in range(nl):
            for imm in (full, full & ~(1 << (4 + k))):
                av, bv = [], []
                for lane in range(vlb // 16):
                    kk = (k + lane + pi) % nl if lane else k
                    av += [pa if j == kk else v["N1"] for j in range(nl)]
                    bv += [pb if j == kk else v["N2"] for j in range(nl)]
                a, b = pack(av, esz), pack(bv, esz)
                if vex:
                    w("%s %s0, %s1, %s2, 0x%X | %s %s" % (mn, r, r, r, imm, regkeys(1, a), regkeys(2, b)))
                else:
                    w("%s xmm0, xmm1, 0x%X | %s %s" % (mn, imm, regkeys(0, a), regkeys(1, b)))


DP_NOTE = """\
# DPPS/DPPD with two or more NaN products are not in this file. The SDM pseudocode
# (DP_primitive: Temp2 := p0 + p1, Temp3 := p2 + p3, every selected element := Temp2 + Temp3;
# DPPD: both elements := p0 + p1) with the SSE NaN rule gives the first NaN product in the
# order p0, p1, p2, p3 in every element (what the fork does). The i5-13600K instead gives
# element-dependent NaNs - mostly t[j] := p[j^1] + p[j], element i := t[i] + t[i^2] (DPPS) and
# element i := p[i] + p[i^1] (DPPD) - and DPPS is not even repeatable: the same input gives
# another NaN in 0-4 of 176 such cases from run to run. Intel manuals win (AGENT_RULES.md)."""


# FMA: operand 1 / 2 / 3 values (instruction operand order), two palettes
F32N = [0x3F800000, 0x40000000, 0x40400000]
F32Q = [[0x7FC10001, 0xFFC20002, 0x7FC30003], [0xFFF30003, 0x7FE20002, 0xFFC10001]]
F32S = [[0x7F910001, 0xFF920002, 0x7F930003], [0xFFB30003, 0x7FA20002, 0xFF910001]]
F64N = [0x3FF0000000000000, 0x4000000000000000, 0x4008000000000000]
F64Q = [[0x7FF8100000000001, 0xFFF8200000000002, 0x7FF8300000000003],
        [0xFFFE300000000003, 0x7FFC200000000002, 0xFFF8100000000001]]
F64S = [[0x7FF2100000000001, 0xFFF2200000000002, 0x7FF2300000000003],
        [0xFFF6300000000003, 0x7FF4200000000002, 0xFFF2100000000001]]


def fma_triples(esz, palettes=(0, 1)):
    N = F64N if esz == 8 else F32N
    Q = F64Q if esz == 8 else F32Q
    S = F64S if esz == 8 else F32S
    inf = 0x7FF0000000000000 if esz == 8 else 0x7F800000
    out = []
    for p in palettes:
        for c1 in "NQS":
            for c2 in "NQS":
                for c3 in "NQS":
                    if c1 + c2 + c3 == "NNN":
                        continue
                    t = []
                    for i, c in enumerate((c1, c2, c3)):
                        t.append({"N": N, "Q": Q[p], "S": S[p]}[c][i])
                    out.append(tuple(t))
    # inf * 0 with a NaN in the third position of every arrangement (0 * inf + QNaN: no #I)
    for nanpos in range(3):
        for kind in ("Q", "S"):
            for order in (0, 1):
                t = [None, None, None]
                others = [i for i in range(3) if i != nanpos]
                t[others[0]] = inf if order == 0 else 0
                t[others[1]] = 0 if order == 0 else inf
                t[nanpos] = (Q[0] if kind == "Q" else S[0])[nanpos]
                out.append(tuple(t))
    return out


def fma_packed(mn, esz, vlb, palettes=(0, 1)):
    n = vlb // esz
    r = "ymm" if vlb == 32 else "xmm"
    tr = fma_triples(esz, palettes)
    while len(tr) % n:
        tr.append(tr[len(tr) % n])
    for grp in chunks(tr, n):
        regs = [pack([t[i] for t in grp], esz) for i in range(3)]
        w("%s %s0, %s1, %s2 | %s %s %s" % (mn, r, r, r, regkeys(0, regs[0]), regkeys(1, regs[1]),
                                          regkeys(2, regs[2])))


def fma_scalar(mn, esz):
    junk = [J64[:1], J64[1:2], J64[2:3]] if esz == 8 else [J32[0:3], J32[3:6], J32[4:7]]
    for t in fma_triples(esz, (0,)):
        regs = [pack([t[i]] + junk[i], esz) for i in range(3)]
        w("%s xmm0, xmm1, xmm2 | xmm0=%s xmm1=%s xmm2=%s" % (mn, hexs(regs[0]), hexs(regs[1]), hexs(regs[2])))


# x87: SEXP:MANT
X87 = dict(N1=(0x3FFF, 0x8000000000000000), N2=(0xC000, 0x8000000000000000),
           Qs=(0x7FFF, 0xC000000011111111), QL=(0xFFFF, 0xE000000022222222),
           Ss=(0x7FFF, 0x8000000033333333), SL=(0xFFFF, 0xA000000044444444),
           Qtp=(0x7FFF, 0xC000000055555555), Qtn=(0xFFFF, 0xC000000055555555),
           Stp=(0x7FFF, 0x8000000055555555), Stn=(0xFFFF, 0x8000000055555555))


def st(i, k):
    se, m = X87[k]
    return "st%d=%04X:%016X" % (i, se, m)


def x87_cases():
    w("# --- x87 (fp_status): Table 4-8 x87 column - larger significand, QNaN over SNaN")
    for mn in ("fadd st(0), st(1)", "fsub st(0), st(1)", "fsubr st(0), st(1)", "fmul st(0), st(1)",
               "fdiv st(0), st(1)", "fdivr st(0), st(1)", "fadd st(1), st(0)", "fdivp st(1), st(0)",
               "fprem", "fprem1", "fscale", "fpatan", "fyl2x", "fyl2xp1"):
        w("# %s (ST0 = first of the pair, ST1 = second)" % mn)
        for a, b in PAIRS:
            w("%s | %s %s" % (mn, st(0, a), st(1, b)))
    # memory operands: m32 / m64 NaN converted to extended before the operation
    for mn, esz, vals in (("fadd dword ptr [rsi]", 4, V32), ("fmul qword ptr [rsi]", 8, V64),
                          ("fsubr dword ptr [rsi]", 4, V32), ("fdiv qword ptr [rsi]", 8, V64)):
        w("# %s (ST0 = first of the pair, memory = second)" % mn)
        for a, b in PAIRS:
            w("%s | %s m+0x8000=%s" % (mn, st(0, a), hexs(pack([vals[b]], esz))))


def gen_hw():
    w("# NaN propagation (ledger U96): hardware cases, generated by Emulator/tools/isa/gen_cases_nan.py --hw")
    w("# (regenerate, do not edit). Only these self-generated snippets run natively:")
    w("#   emu-alltest --cases Emulator\\data\\cases_nan.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt --strict --xcr0 7")
    w("# Lanes (SRC1, SRC2) per element, 32-bit: N1 3F800000, N2 C0000000, Qs 7FC11111, QL FFE22222,")
    w("# Ss 7F833333, SL FFB44444, Qt+- 7FC55555/FFC55555, St+- 7F955555/FF955555 (64-bit alike):")
    w("# " + " ".join("%s/%s" % p for p in PAIRS))
    w("# SDM Vol1 4.8.3.5 Table 4-8: SSE/AVX -> SRC1 quieted; MIN/MAX -> SRC2; x87 -> larger significand.")
    for vex in (False, True):
        pre = "v" if vex else ""
        w("# ===== %s" % ("VEX (SRC1 = second operand)" if vex else "legacy SSE (SRC1 = destination)"))
        for op in ("add", "sub", "mul", "div", "min", "max", "addsub", "cmp"):
            for sfx, esz in (("ps", 4), ("pd", 8)):
                if op == "addsub":
                    mn = pre + "addsub" + sfx
                elif op == "cmp":
                    mn = pre + "cmp" + sfx
                else:
                    mn = pre + op + sfx
                w("# %s" % mn)
                for vlb in ((16, 32) if vex else (16,)):
                    if op == "cmp":
                        for imm in (0, 1, 3, 4):
                            two_src_packed(mn, esz, vex, vlb, imm)
                    else:
                        two_src_packed(mn, esz, vex, vlb)
            if op in ("addsub",):
                continue
            for sfx, esz in (("ss", 4), ("sd", 8)):
                mn = pre + ("cmp" if op == "cmp" else op) + sfx
                w("# %s" % mn)
                if op == "cmp":
                    two_src_scalar(mn, esz, vex, 1)
                else:
                    two_src_scalar(mn, esz, vex)
        for mn, esz in (("comiss", 4), ("ucomiss", 4), ("comisd", 8), ("ucomisd", 8)):
            w("# %s" % (pre + mn))
            comi(pre + mn, esz, vex)
        for mn, esz in (("haddps", 4), ("hsubps", 4), ("haddpd", 8), ("hsubpd", 8)):
            w("# %s" % (pre + mn))
            for vlb in ((16, 32) if vex else (16,)):
                horizontal(pre + mn, esz, vex, vlb)
        w("# %sdpps / %sdppd: one NaN product per dot product" % (pre, pre))
        w(DP_NOTE)
        for vlb in ((16, 32) if vex else (16,)):
            dp(pre + "dpps", 4, vex, vlb)
        dp(pre + "dppd", 8, vex, 16)
        w("# %ssqrt / %sround / conversions (one source)" % (pre, pre))
        for vlb in ((16, 32) if vex else (16,)):
            one_src(pre + "sqrtps", 4, vex, vlb)
            one_src(pre + "sqrtpd", 8, vex, vlb)
            one_src(pre + "roundps", 4, vex, vlb, 4)
            one_src(pre + "roundpd", 8, vex, vlb, 0xC)
        one_src(pre + "sqrtss", 4, vex, 16, scalar=True)
        one_src(pre + "sqrtsd", 8, vex, 16, scalar=True)
        one_src(pre + "roundss", 4, vex, 16, 4, scalar=True)
        one_src(pre + "roundsd", 8, vex, 16, 9, scalar=True)
        one_src(pre + "cvtss2sd", 4, vex, 16, scalar=True)
        one_src(pre + "cvtsd2ss", 8, vex, 16, scalar=True)
        if vex:
            for mn, esz in (("vcvtps2pd", 4), ("vcvtpd2ps", 8)):
                for x in (0, 1):
                    v = V64 if esz == 8 else V32
                    vals = [v[k] for k in SINGLE]
                    if mn == "vcvtps2pd":
                        w("%s ymm0, xmm1 | xmm1=%s" % (mn, hexs(pack(vals[4 * x:4 * x + 4], 4))))
                    else:
                        w("%s xmm0, ymm1 | %s" % (mn, regkeys(1, pack(vals[4 * x:4 * x + 4], 8))))
        else:
            for x in range(4):
                w("cvtps2pd xmm0, xmm1 | xmm1=%s" % hexs(pack([V32[k] for k in SINGLE][2 * x:2 * x + 2], 4) + bytes(8)))
            for x in range(4):
                w("cvtpd2ps xmm0, xmm1 | xmm0=%s xmm1=%s" % (hexs(pack(J32[:4], 4)),
                                                             hexs(pack([V64[k] for k in SINGLE][2 * x:2 * x + 2], 8))))
        if not vex:
            w("# memory source operand (SRC2 from memory)")
            for mn, esz in (("addps", 4), ("mulpd", 8), ("maxps", 4)):
                for grp in chunks(pairs_vals(esz), 16 // esz):
                    a = pack([p[0] for p in grp], esz)
                    b = pack([p[1] for p in grp], esz)
                    w("%s xmm0, xmmword ptr [rsi] | xmm0=%s m+0x8000=%s" % (mn, hexs(a), hexs(b)))
            w("# unmasked invalid (MXCSR.IM = 0): SNaN -> #XM; MIN with a QNaN -> #XM; ADD of QNaNs -> no fault")
            for mn in ("addps", "minps", "addss", "maxss"):
                for grp in chunks(pairs_vals(4), 4)[:3]:
                    a = pack([p[0] for p in grp], 4)
                    b = pack([p[1] for p in grp], 4)
                    w("ldmxcsr [rsi]; %s xmm0, xmm1 | m+0x8000=001F0000 xmm0=%s xmm1=%s" % (mn, hexs(a), hexs(b)))
    w("# ===== FMA (VEX): operands (1, 2, 3) = (xmm0, xmm1, xmm2); 132: x=1 y=3 z=2, 213: x=2 y=1 z=3,")
    w("# 231: x=2 y=3 z=1 (Vol1 14.5.2 Table 14-17: Q(x), else Q(y), else Q(z))")
    for kind in ("vfmadd", "vfmsub", "vfnmadd", "vfnmsub"):
        for form in ("132", "213", "231"):
            for sfx, esz in (("ps", 4), ("pd", 8)):
                mn = kind + form + sfx
                w("# %s" % mn)
                if kind == "vfmadd":
                    fma_packed(mn, esz, 32)
                    fma_packed(mn, esz, 16, (0,))
                else:
                    fma_packed(mn, esz, 32, (0,))
            if kind == "vfmadd" or form == "231":
                for sfx, esz in (("ss", 4), ("sd", 8)):
                    mn = kind + form + sfx
                    w("# %s" % mn)
                    fma_scalar(mn, esz)
    for kind in ("vfmaddsub", "vfmsubadd"):
        for form in ("132", "213", "231"):
            for sfx, esz in (("ps", 4), ("pd", 8)):
                mn = kind + form + sfx
                w("# %s" % mn)
                fma_packed(mn, esz, 32, (0,))
    x87_cases()


# ---------------------------------------------------------------------------------------
# EVEX expected-value cases
# ---------------------------------------------------------------------------------------
EVEX_FP = [("VADDPS", 0x58, "add", 0), ("VADDPD", 0x58, "add", 1),
           ("VSUBPS", 0x5C, "sub", 0), ("VSUBPD", 0x5C, "sub", 1),
           ("VMULPS", 0x59, "mul", 0), ("VMULPD", 0x59, "mul", 1),
           ("VDIVPS", 0x5E, "div", 0), ("VDIVPD", 0x5E, "div", 1),
           ("VMINPS", 0x5D, "min", 0), ("VMINPD", 0x5D, "min", 1),
           ("VMAXPS", 0x5F, "max", 0), ("VMAXPD", 0x5F, "max", 1)]


def fill(seed, n):
    return bytes(((seed * 37 + i * 11) & 0xFF) for i in range(n))


def evex_case(name, opc, op, wbit, vl, av, bv, dst, s1, s2, kreg=0, kval=None, z=0, mem=False,
              bcst=False, rc=None, sae=False, mxcsr=m1.MXCSR_DEFAULT, title=""):
    esz = 8 if wbit else 4
    n = vl // esz
    c = m1.Case("%s VL%d %s" % (name, vl * 8, title))
    regs = {}
    if op != "sqrt":
        regs[s1] = pack(av, esz) + fill(s1, 64 - vl)
    if not mem:
        regs[s2] = pack(bv, esz) + fill(s2, 64 - vl)
    if dst not in regs:
        regs[dst] = fill(dst + 100, 64)
    for r, img in regs.items():
        c.set_zmm(r, img)
    if mem:
        c.mem[m1.MEM_RSI + 0x40] = pack(bv[:1] if bcst else bv, esz)
        rm, nn = m1.Mem(m1.RSI, 0x40), (esz if bcst else vl)
        if bcst:
            bv = bv[:1] * n
    else:
        rm, nn = s2, 1
    if kreg:
        c.k[kreg] = kval
    if mxcsr != m1.MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    kmask = kval if kreg else None
    res, flags, unm = m1.model_vec(op, wbit, av if op != "sqrt" else bv, bv, mxcsr, kmask, rc)
    if sae:
        flags, unm = 0, 0
    old = m1.elems(regs[dst][:vl], esz)
    if unm:
        c.fault = "#XM"
    else:
        out = pack([(0 if z else old[j]) if res[j] is None else res[j] for j in range(n)], esz)
        c.exp.append("zmm%d=%s" % (dst, hexs(out + bytes(64 - vl))))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    ll = rc if rc is not None else (2 if sae else m1.VL_LL[vl])
    b = 1 if (bcst or rc is not None or sae) else 0
    pp = 1 if wbit else 0
    if op == "sqrt":
        c.code = m1.evex(1, pp, wbit, opc, dst, rm, ll=ll, b=b, z=z, aaa=kreg, n=nn)
    else:
        c.code = m1.evex(1, pp, wbit, opc, dst, rm, vvvv=s1, ll=ll, b=b, z=z, aaa=kreg, n=nn)
    return c


def gen_evex():
    w("# NaN propagation (ledger U96), EVEX expected-value cases: Emulator/tools/isa/gen_cases_nan.py --evex")
    w("# (regenerate, do not edit). Expected values: SDM model of ref_evex_m1.py (Vol1 4.8.3.5 Table 4-8:")
    w("# SSE/AVX/AVX-512 -> SRC1 (EVEX.vvvv) quieted when both sources are NaN; MINPS/MAXPS: any NaN ->")
    w("# SRC2 unchanged + #I; SQRT: the NaN source quieted). The i5-13600K has no AVX-512:")
    w("#   emu-alltest --cases Emulator\\data\\cases_nan_evex.txt --avx512 --xcr0 0xE7 --expect-only")
    w("# Lanes (SRC1, SRC2): " + " ".join("%s/%s" % p for p in PAIRS))
    cases = []
    for name, opc, op, wbit in EVEX_FP:
        esz = 8 if wbit else 4
        pv = pairs_vals(esz)
        av_all = [p[0] for p in pv]
        bv_all = [p[1] for p in pv]
        cases.append("# %s (EVEX.%s.0F.W%d %02X /r)" % (name, "66" if wbit else "NP", wbit, opc))
        for vl in (16, 32, 64):
            n = vl // esz
            for i in range(0, len(pv), n):
                av, bv = av_all[i:i + n], bv_all[i:i + n]
                if len(av) < n:
                    av = (av + av_all)[:n]
                    bv = (bv + bv_all)[:n]
                cases.append(evex_case(name, opc, op, wbit, vl, av, bv, 1, 2, 3, title="lanes %d.." % i))
        n = 64 // esz
        av, bv = (av_all * 2)[:n], (bv_all * 2)[:n]
        rot_a, rot_b = (av_all * 2)[5:5 + n], (bv_all * 2)[5:5 + n]
        cases.append(evex_case(name, opc, op, wbit, 64, av, bv, 20, 21, 29, kreg=1, kval=0xA5C3, title="merge"))
        cases.append(evex_case(name, opc, op, wbit, 64, rot_a, rot_b, 4, 5, 6, kreg=2, kval=0x3C5A, z=1,
                               title="zero"))
        cases.append(evex_case(name, opc, op, wbit, 64, av, bv, 7, 8, 0, mem=True, title="mem"))
        # {1toN}: a NaN broadcast element against NaN / number SRC1 lanes
        for bk in ("Ss", "Qs", "QL"):
            v = (V64 if wbit else V32)[bk]
            cases.append(evex_case(name, opc, op, wbit, 64, (av_all * 2)[:n], [v], 9, 10, 0, mem=True,
                                   bcst=True, title="{1toN} %s" % bk))
        # {er} / {sae}: flags suppressed, the NaN result is the same
        if op in ("min", "max"):
            cases.append(evex_case(name, opc, op, wbit, 64, av, bv, 11, 12, 13, sae=True, mxcsr=0x1F00,
                                   title="{sae} unmasked MXCSR, no #XM"))
        else:
            for rc in (0, 3):
                cases.append(evex_case(name, opc, op, wbit, 64, av, bv, 11, 12, 13, rc=rc, mxcsr=0x1F00,
                                       title="{er} rc=%d unmasked MXCSR, no #XM" % rc))
        # unmasked IM with a QNaN-only active lane set: no #I for arithmetic, #I (#XM) for MIN/MAX
        qq = [i for i, p in enumerate(PAIRS) if "S" not in p[0] + p[1]]
        aq = [av_all[i] for i in qq]
        bq = [bv_all[i] for i in qq]
        cases.append(evex_case(name, opc, op, wbit, 16 if wbit == 0 else 32, (aq * 4)[:4], (bq * 4)[:4],
                               14, 15, 16, mxcsr=0x1F00, title="QNaN pairs only, IM=0"))
        cases.append(evex_case(name, opc, op, wbit, 64, av, bv, 17, 18, 19, mxcsr=0x1F00,
                               title="SNaN active, IM=0 -> #XM"))
    for name, wbit in (("VSQRTPS", 0), ("VSQRTPD", 1)):
        esz = 8 if wbit else 4
        v = V64 if wbit else V32
        vals = [v[k] for k in SINGLE]
        cases.append("# %s (EVEX.%s.0F.W%d 51 /r)" % (name, "66" if wbit else "NP", wbit))
        for vl in (16, 32, 64):
            n = vl // esz
            bv = (vals * 4)[:n]
            cases.append(evex_case(name, 0x51, "sqrt", wbit, vl, None, bv, 1, 0, 3, title="NaN lanes"))
        cases.append(evex_case(name, 0x51, "sqrt", wbit, 64, None, (vals * 4)[3:3 + 64 // esz], 22, 0, 23,
                               kreg=3, kval=0x6969, title="merge"))
    for c in cases:
        if isinstance(c, str):
            w(c)
        else:
            w("# " + c.title)
            w(c.line())


def main():
    if "--hw" in sys.argv:
        gen_hw()
    elif "--evex" in sys.argv:
        gen_evex()
    else:
        print(__doc__)
        return
    sys.stdout.buffer.write(("\n".join(OUT) + "\n").encode("ascii"))


if __name__ == "__main__":
    main()
