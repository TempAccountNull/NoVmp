#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m2_engine.py -- independent reference model (Python 3 stdlib only) of the EVEX
milestone-M2 "engine" instructions (ledger U190-U201) and generator of the expected-value
case file Emulator/data/cases_evex_m2_engine.txt.

Written from the Intel SDM text only (Vol2A 2.7/2.8 and Tables 2-36 .. 2-61, the instruction
pages' "Operation" pseudocode, Vol1 4.8/4.9/10.2/11.5 and 14.5 "FMA" for the floating-point
rules), not from any C implementation. The EVEX encoder, the IEEE binary32/64 model (exact
rationals: RNE/RD/RU/RZ, DAZ, FTZ, IE DE ZE OE UE PE, tininess after rounding) and the
ADD/SUB/MUL/DIV/MIN/MAX/SQRT element models are imported from ref_evex_m1.py (milestone M1,
already checked against the i5-13600K). New here:

  engine    scalar forms (EVEX.LLIG): element 0 under k1[0], DEST[127:esz] := SRC1[127:esz]
            (DEST[127:esz] for FMA, 0 for VMOVSS/SD m), DEST[MAXVL-1:128] := 0;
            VPBLENDMx/VBLENDMPx: masked-off lanes take SRC1 (merging) or 0 (zeroing);
            FMA: the destination is the first source, merging keeps the old destination;
            {sae} on compares into k.
  scalar    VADDSS/SD VSUBSS/SD VMULSS/SD VDIVSS/SD VSQRTSS/SD ({er}), VMINSS/SD VMAXSS/SD
            ({sae}), VMOVSS/SD (load, store, both register merge forms), VCOMISS/SD and
            VUCOMISS/SD ({sae}, EFLAGS), VCMPSS/SD -> k (imm8 0-31, {sae})
  packed    VCMPPS/PD -> k (32 predicates, {1toN}, {sae}); VFMADD/VFMSUB/VFNMADD/VFNMSUB
            132/213/231 PS/PD/SS/SD and VFMADDSUB/VFMSUBADD 132/213/231 PS/PD ({er},
            masking with DEST as a source, {1toN}); VPBLENDMD/Q, VBLENDMPS/PD
  FMA       x*y+z with one rounding of the exact value (Vol1 14.5.2), NaN result Q(x), Q(y),
            Q(z) in that order (x = multiplicand, y = multiplier, z = addend), #IA for any
            SNaN, for INF*0 (unless z is a QNaN: Table 14-17 returns Q(z) without #IA) and for
            INF - INF; negation does not apply to a NaN; zero-result signs of Table 14-16.
  compare   Table 3-8: relations {>, <, =, unordered} per predicate, #IA on an SNaN, and on a
            QNaN for the signalling predicates; DE for a denormal operand unless a NaN operand.

Two-NaN propagation of the 2-source SSE helpers is being fixed separately (ledger U96): the
scalar arithmetic cases leave out element-0 operand pairs where both sources are NaN (the
same rule as ref_evex_m1.no_nan_pairs). FMA (three sources) and the compares are not
affected.

Usage:
  python ref_evex_m2_engine.py --selftest              hand-derived checks, exit 0 on pass
  python ref_evex_m2_engine.py --cases                 cases_evex_m2_engine.txt (stdout)
  python ref_evex_m2_engine.py --hwgen EXPECT.json     hardware cases (stdout) for the legacy
                                                       SSE / VEX equivalents on the host
  python ref_evex_m2_engine.py --hwcmp LOG EXPECT.json compare an emu-alltest log of those
"""

import json
import os
import random
import re
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_evex_m1 as m1                                       # noqa: E402
from ref_evex_m1 import (evex, Mem, Case, elems, pack, hexs, classify, quiet,      # noqa: E402
                         encode_value, F32, F64, IE, DE, ZE, OE, UE, PE, MXCSR_DEFAULT, DAZ,
                         FTZ, MEM_RSI, RSI, R14, VL_LL, SPECIAL32, SPECIAL64, model_elem,
                         is_nan, rnd_fp)

RNG = random.Random(0x5EED_0190)
m1.RNG = RNG                    # ref_evex_m1.rnd_fp / rnd_bytes draw from this generator


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


SIGN = {F32: 1 << 31, F64: 1 << 63}
INF = {F32: 0x7F800000, F64: 0x7FF0000000000000}
ONE = {F32: 0x3F800000, F64: 0x3FF0000000000000}

# RFLAGS bits written by (U)COMISS/SD
CF, PF, AF, ZF, SF, OF = 0x1, 0x4, 0x10, 0x40, 0x80, 0x800
RFLAGS_DEFAULT = 0x202


# ---------------------------------------------------------------------------------------
# FMA (SDM Vol1 14.5.2, Tables 14-16 and 14-17)
# ---------------------------------------------------------------------------------------
def fp_fma(x, y, z, f, mxcsr, rc=None, negp=False, negc=False):
    """r = (+/-)(x*y) (+/-) z rounded once; x multiplicand, y multiplier, z addend.
    Returns (bits, flags)."""
    if rc is None:
        rc = (mxcsr >> 13) & 3
    daz, ftz = bool(mxcsr & DAZ), bool(mxcsr & FTZ)
    cls = [classify(v, f) for v in (x, y, z)]
    kinds = [c[0] for c in cls]
    flags = 0
    # NaN operands (Table 14-17 rows 1-3): Q(x), Q(y), Q(z); #IA if any of them is an SNaN
    if any(k in ("snan", "qnan") for k in kinds):
        if "snan" in kinds:
            flags |= IE
        for v, k in zip((x, y, z), kinds):
            if k in ("snan", "qnan"):
                return quiet(v, f), flags
    (cx, sx, vx), (cy, sy, vy), (cz, sz, vz) = cls
    if daz:
        if cx == "denorm":
            cx, vx = "zero", Fraction(0)
        if cy == "denorm":
            cy, vy = "zero", Fraction(0)
        if cz == "denorm":
            cz, vz = "zero", Fraction(0)
    signbit, inf_bits = SIGN[f], INF[f]
    sp = sx ^ sy ^ (1 if negp else 0)                # sign of the (negated) product
    szz = sz ^ (1 if negc else 0)                    # sign of the (negated) addend
    # invalid operations: INF * 0, INF - INF (Table 14-17; Vol1 4.9.1.2)
    if (cx == "inf" and cy == "zero") or (cx == "zero" and cy == "inf"):
        return f.qnan_indef, flags | IE
    if (cx == "inf" or cy == "inf") and cz == "inf" and sp != szz:
        return f.qnan_indef, flags | IE
    if not daz and "denorm" in (cx, cy, cz):
        flags |= DE
    if cx == "inf" or cy == "inf":
        return (signbit if sp else 0) | inf_bits, flags
    if cz == "inf":
        return (signbit if szz else 0) | inf_bits, flags
    p = vx * vy
    if negp:
        p = -p
    zc = -vz if negc else vz
    s = p + zc
    if s == 0:
        # Table 14-16: two zeros of the same sign keep it, otherwise +0 (-0 rounding down)
        if (cx == "zero" or cy == "zero") and cz == "zero" and sp == szz:
            return (signbit if sp else 0), flags
        return (signbit if rc == 1 else 0), flags
    r, fl = encode_value(s, f, rc, ftz)
    return r, flags | fl


FMA_KIND = {
    # low opcode nibble & 0xE -> (name, even (negp, negc), odd (negp, negc))
    0x6: ("FMADDSUB", (False, True), (False, False)),
    0x7: ("FMSUBADD", (False, False), (False, True)),
    0x8: ("FMADD", (False, False), (False, False)),
    0xA: ("FMSUB", (False, True), (False, True)),
    0xC: ("FNMADD", (True, False), (True, False)),
    0xE: ("FNMSUB", (True, True), (True, True)),
}
FMA_ORDER = {0x9: "132", 0xA: "213", 0xB: "231"}


def fma_kind(opc):
    if (opc & 0xF) == 0x7:
        return FMA_KIND[0x7]
    return FMA_KIND[opc & 0xE]


def fma_elem(opc, j, d, s2, s3, f, mxcsr, rc=None):
    """one element of an FMA form (opcode byte opc, element index j): DEST d, SRC2 s2 (vvvv),
    SRC3 s3 (r/m). SDM: 132 = DEST*SRC3 op SRC2, 213 = SRC2*DEST op SRC3, 231 = SRC2*SRC3 op
    DEST; FMADDSUB subtracts in the even elements, FMSUBADD in the odd ones."""
    order = FMA_ORDER[opc >> 4]
    x, y, z = {"132": (d, s3, s2), "213": (s2, d, s3), "231": (s2, s3, d)}[order]
    _, ev, od = fma_kind(opc)
    negp, negc = ev if j % 2 == 0 else od
    return fp_fma(x, y, z, f, mxcsr, rc, negp, negc)


# ---------------------------------------------------------------------------------------
# compares (SDM Vol2A CMPPS/CMPPD Table 3-8, COMISS/COMISD, UCOMISS/UCOMISD)
# ---------------------------------------------------------------------------------------
# predicate: (A > B, A < B, A = B, unordered, signals #IA on QNaN)
CMP_TABLE = [
    ("EQ_OQ", 0, 0, 1, 0, 0), ("LT_OS", 0, 1, 0, 0, 1), ("LE_OS", 0, 1, 1, 0, 1),
    ("UNORD_Q", 0, 0, 0, 1, 0), ("NEQ_UQ", 1, 1, 0, 1, 0), ("NLT_US", 1, 0, 1, 1, 1),
    ("NLE_US", 1, 0, 0, 1, 1), ("ORD_Q", 1, 1, 1, 0, 0), ("EQ_UQ", 0, 0, 1, 1, 0),
    ("NGE_US", 0, 1, 0, 1, 1), ("NGT_US", 0, 1, 1, 1, 1), ("FALSE_OQ", 0, 0, 0, 0, 0),
    ("NEQ_OQ", 1, 1, 0, 0, 0), ("GE_OS", 1, 0, 1, 0, 1), ("GT_OS", 1, 0, 0, 0, 1),
    ("TRUE_UQ", 1, 1, 1, 1, 0), ("EQ_OS", 0, 0, 1, 0, 1), ("LT_OQ", 0, 1, 0, 0, 0),
    ("LE_OQ", 0, 1, 1, 0, 0), ("UNORD_S", 0, 0, 0, 1, 1), ("NEQ_US", 1, 1, 0, 1, 1),
    ("NLT_UQ", 1, 0, 1, 1, 0), ("NLE_UQ", 1, 0, 0, 1, 0), ("ORD_S", 1, 1, 1, 0, 1),
    ("EQ_US", 0, 0, 1, 1, 1), ("NGE_UQ", 0, 1, 0, 1, 0), ("NGT_UQ", 0, 1, 1, 1, 0),
    ("FALSE_OS", 0, 0, 0, 0, 1), ("NEQ_OS", 1, 1, 0, 0, 1), ("GE_OQ", 1, 0, 1, 0, 0),
    ("GT_OQ", 1, 0, 0, 0, 0), ("TRUE_US", 1, 1, 1, 1, 1),
]


def fp_relation(a, b, f, mxcsr, signalling):
    """('gt'|'lt'|'eq'|'un', flags) of SRC1 a vs SRC2 b"""
    daz = bool(mxcsr & DAZ)
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    if ca in ("snan", "qnan") or cb in ("snan", "qnan"):
        fl = IE if (ca == "snan" or cb == "snan" or signalling) else 0
        return "un", fl
    flags = 0
    if daz:
        if ca == "denorm":
            ca, va = "zero", Fraction(0)
        if cb == "denorm":
            cb, vb = "zero", Fraction(0)
    elif ca == "denorm" or cb == "denorm":
        flags |= DE

    def key(c, s, v):
        if c == "inf":
            return (-1, 0) if s else (1, 0)
        return (0, v)

    ka, kb = key(ca, sa, va), key(cb, sb, vb)
    if ka == kb:
        return "eq", flags
    return ("lt" if ka < kb else "gt"), flags


def fp_cmp(pred, a, b, f, mxcsr):
    name, gt, lt, eq, un, sig = CMP_TABLE[pred & 31]
    rel, flags = fp_relation(a, b, f, mxcsr, sig)
    return bool({"gt": gt, "lt": lt, "eq": eq, "un": un}[rel]), flags


def fp_comis(a, b, f, mxcsr, ordered):
    """COMISS (ordered = signalling) / UCOMISS: (ZF PF CF, flags); OF SF AF := 0"""
    rel, flags = fp_relation(a, b, f, mxcsr, ordered)
    return {"un": ZF | PF | CF, "gt": 0, "lt": CF, "eq": ZF}[rel], flags


def comis_rflags(bits, old=RFLAGS_DEFAULT):
    return (old & ~(OF | SF | ZF | AF | PF | CF)) | bits


# ---------------------------------------------------------------------------------------
# generic expected-value case: one EVEX form, every lane modelled by spec["elem"]
#   spec: name mmm pp opc w scalar(bool) dest('vec'|'k'|'flags') fp(bool) elem(j,a,b,d,mx,rc)
#         upper('src1'|'dest'|'zero', scalar only) dsrc msrc1 vvvv(bool) imm(int|None)
# ---------------------------------------------------------------------------------------
def esz_of(spec):
    return 8 if spec["w"] else 4


def fmt_of(spec):
    return F64 if spec["w"] else F32


def build_regs(c, vl, esz, assign):
    """assign: list of (reg, values) applied in order on random 64-byte images; returns
    the images (aliasing: a later assignment of the same register wins)"""
    imgs = {}
    for r, vals in assign:
        if r is None:
            continue
        if r not in imgs:
            imgs[r] = bytearray(rnd_bytes(64))
        if vals is not None:
            imgs[r][:len(vals) * esz] = pack(vals, esz)
    for r in imgs:
        imgs[r] = bytes(imgs[r])
        c.set_zmm(r, imgs[r])
    return imgs


def gen_case(spec, vl, variant="reg", dst=1, s1=2, s2=3, kreg=0, kval=None, z=0, avals=None,
             bvals=None, dvals=None, mxcsr=MXCSR_DEFAULT, rc=None, sae=False, ll=None, imm=None,
             mem_off=0x40, mem_disp32=False, title=None, no_nan_pair=False, rflags=None):
    esz, f = esz_of(spec), fmt_of(spec)
    scalar = spec.get("scalar", False)
    n = 1 if scalar else vl // esz
    if scalar:
        vl = 16
    dest = spec.get("dest", "vec")
    fp = spec.get("fp", True)
    mem = variant in ("mem", "bcst")
    bcst = variant == "bcst"
    gen = (lambda: rnd_fp(f)) if fp else (lambda: RNG.getrandbits(8 * esz))
    if avals is None:
        avals = [gen() for _ in range(n)]
    if bvals is None:
        bvals = [gen() for _ in range(1 if bcst else n)]
    if dvals is None:
        dvals = [gen() for _ in range(n)]
    if no_nan_pair:
        bl = bvals * n if bcst else bvals
        avals = [ONE[f] if is_nan(x, f) and is_nan(y, f) else x for x, y in zip(avals, bl)]
    assert dest != "k" or 0 <= dst < 8, "k destination must be k0-k7"
    if imm is None and spec.get("imm") is not None:
        imm = spec["imm"]
    c = Case(title or "%s VL%d %s" % (spec["name"], vl * 8, variant))
    has_v = spec.get("vvvv", True)
    order = []
    if dest == "vec":
        order.append((dst, dvals))
    if dest == "flags":
        order.append((s1, avals))
    elif has_v:
        order.append((s1, avals))
    if not mem:
        order.append((s2, bvals))
    imgs = build_regs(c, vl, esz, order)
    # read back the values every operand really holds (aliasing)
    if dest == "flags" or has_v:
        avals = elems(imgs[s1][:n * esz], esz)
    if not mem:
        bvals = elems(imgs[s2][:n * esz], esz)
    if dest == "vec":
        dvals = elems(imgs[dst][:n * esz], esz)
    if mem:
        data = pack(bvals[:1] if bcst else bvals[:n], esz)
        if mem_disp32 and mem_off >= 0x10000 - MEM_RSI:
            pass                                 # the operand is on the unmapped page
        else:
            c.mem[MEM_RSI + mem_off] = data
        if bcst:
            bvals = bvals[:1] * n
        rm = Mem(RSI, mem_off, disp32=mem_disp32)
        nn = esz if (bcst or scalar) else vl
    else:
        rm, nn = s2, 1
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    if rflags is not None:
        c.inp.append("rflags=0x%X" % rflags)
    kmask = kval if kreg else None

    def active(j):
        return kmask is None or (kmask >> j) & 1

    res, flags = [None] * n, 0
    for j in range(n):
        if not active(j):
            continue
        r, fl = spec["elem"](j, avals[j], bvals[j], dvals[j], mxcsr, rc)
        res[j] = r
        flags |= fl
    if rc is not None or sae or not fp:
        flags = 0
    unm = flags & ~(mxcsr >> 7) & 0x3F
    if unm:
        if unm & (IE | DE | ZE):
            flags &= ~(OE | UE | PE)
        c.fault = "#XM"
    else:
        if dest == "vec":
            old = imgs[dst]
            out = []
            for j in range(n):
                if res[j] is not None:
                    out.append(res[j])
                elif z:
                    out.append(0)
                elif spec.get("msrc1"):
                    out.append(avals[j])
                else:
                    out.append(elems(old[:n * esz], esz)[j])
            img = pack(out, esz)
            if scalar:
                up = spec.get("upper", "src1")
                if up == "src1":
                    img += imgs[s1][esz:16]
                elif up == "dest":
                    img += old[esz:16]
                else:
                    img += bytes(16 - esz)
            img += bytes(64 - len(img))
            c.exp.append("zmm%d=%s" % (dst, hexs(img)))
        elif dest == "k":
            r = 0
            for j in range(n):
                if res[j]:
                    r |= 1 << j
            c.k.setdefault(dst, RNG.getrandbits(64))
            c.exp.append("k%d=0x%X" % (dst, r))
        else:
            old = rflags if rflags is not None else RFLAGS_DEFAULT
            c.exp.append("rflags=0x%X" % comis_rflags(res[0], old))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    if ll is None:
        ll = rc if rc is not None else (0 if scalar else VL_LL[vl])
    b = 1 if (bcst or rc is not None or sae) else 0
    if dest == "flags":
        c.code = evex(spec["mmm"], spec["pp"], spec["w"], spec["opc"], s1, rm, ll=ll, b=b, z=z,
                      aaa=kreg, n=nn)
    else:
        c.code = evex(spec["mmm"], spec["pp"], spec["w"], spec["opc"], dst, rm,
                      vvvv=s1 if has_v else None, ll=ll, b=b, z=z, aaa=kreg, imm=imm, n=nn)
    return c


def ud_case(title, spec, **kw):
    """an encoding of spec's opcode that must #UD (kw: evex() keyword overrides)"""
    a = dict(mmm=spec["mmm"], pp=spec["pp"], w=spec["w"], opc=spec["opc"], reg=1, rm=3, vvvv=2,
             ll=0 if spec.get("scalar") else 2)
    if spec.get("imm") is not None:
        a["imm"] = spec["imm"]
    a.update(kw)
    c = Case(title)
    c.code = evex(a.pop("mmm"), a.pop("pp"), a.pop("w"), a.pop("opc"), a.pop("reg"), a.pop("rm"), **a)
    c.fault = "#UD"
    return c


# ---------------------------------------------------------------------------------------
# case generators
# ---------------------------------------------------------------------------------------
cases = []


def emit(c):
    cases.append(c)


def comment(text):
    cases.append("# " + text)


def specials(f):
    return SPECIAL64 if f is F64 else SPECIAL32


UNMAPPED = 0x10000 - MEM_RSI                    # disp of MEM + 0x10000 (unmapped) from RSI


# -- scalar arithmetic ------------------------------------------------------------------
SCALAR_OPS = [
    # name, opc, op, rc form
    ("VADDS", 0x58, "add", "er"), ("VMULS", 0x59, "mul", "er"), ("VSUBS", 0x5C, "sub", "er"),
    ("VMINS", 0x5D, "min", "sae"), ("VDIVS", 0x5E, "div", "er"), ("VMAXS", 0x5F, "max", "sae"),
    ("VSQRTS", 0x51, "sqrt", "er"),
]


def scalar_spec(name, opc, op, w):
    f = F64 if w else F32
    return dict(name=name + ("D" if w else "S"), mmm=1, pp=3 if w else 2, opc=opc, w=w,
                scalar=True, fp=True, upper="src1",
                elem=lambda j, a, b, d, mx, rc, op=op, f=f: model_elem(op, a, b, f, mx, rc))


def gen_scalar_arith():
    comment("--- scalar FP: VADDSS/SD VSUBSS/SD VMULSS/SD VDIVSS/SD VSQRTSS/SD ({er}), "
            "VMINSS/SD VMAXSS/SD ({sae}); EVEX.LLIG, Tuple1 Scalar, E3")
    for name, opc, op, rcf in SCALAR_OPS:
        for w in (0, 1):
            sp = scalar_spec(name, opc, op, w)
            f, esz = fmt_of(sp), esz_of(sp)
            two = op in ("add", "sub", "mul", "div")
            comment("%s (EVEX.LLIG.%s.0F.W%d %02X /r)" % (sp["name"], "F2" if w else "F3", w, opc))
            G = lambda **kw: emit(gen_case(sp, 16, no_nan_pair=two, **kw))  # noqa: E731
            G(variant="reg", dst=1, s1=2, s2=3)
            G(variant="reg", dst=17, s1=30, s2=9, title="%s zmm16+" % sp["name"])
            for kb in (0, 1):
                G(variant="reg", dst=4, s1=5, s2=6, kreg=3, kval=(RNG.getrandbits(63) << 1) | kb,
                  title="%s merge k1[0]=%d" % (sp["name"], kb))
                G(variant="reg", dst=7, s1=8, s2=9, kreg=4, kval=(RNG.getrandbits(63) << 1) | kb, z=1,
                  title="%s zero k1[0]=%d" % (sp["name"], kb))
                G(variant="mem", dst=10, s1=11, kreg=5, kval=kb | 0xF0,
                  title="%s mem merge k1[0]=%d" % (sp["name"], kb))
            G(variant="mem", dst=12, s1=13, title="%s mem" % sp["name"])
            # L'L is ignored without EVEX.b (scalar: LIG)
            for ll in (1, 2, 3):
                G(variant="reg", dst=14, s1=15, s2=16, ll=ll, title="%s L'L=%d ignored" % (sp["name"], ll))
            # destination = each source
            G(variant="reg", dst=18, s1=18, s2=19, title="%s dst=src1" % sp["name"])
            G(variant="reg", dst=19, s1=18, s2=19, kreg=1, kval=1, title="%s dst=src2" % sp["name"])
            G(variant="reg", dst=20, s1=20, s2=20, title="%s dst=src1=src2" % sp["name"])
            # specials (element 0 pairs)
            spl = specials(f)
            for i in range(len(spl)):
                a, b = spl[i], spl[(i * 7 + 3) % len(spl)]
                G(variant="reg", dst=21, s1=22, s2=23, avals=[a], bvals=[b],
                  title="%s specials %d" % (sp["name"], i))
            # rounding / DAZ / FTZ
            tiny = (0x00400001, 0x00400003) if f is F32 else (0x0008000000000001, 0x0008000000000003)
            for mx in (0x3F80, 0x5F80, 0x7F80, MXCSR_DEFAULT | DAZ, MXCSR_DEFAULT | FTZ,
                       MXCSR_DEFAULT | DAZ | FTZ):
                G(variant="reg", dst=24, s1=25, s2=26, mxcsr=mx, title="%s mxcsr=%X" % (sp["name"], mx))
                G(variant="reg", dst=24, s1=25, s2=26, avals=[tiny[0]], bvals=[tiny[1]], mxcsr=mx,
                  title="%s tiny mxcsr=%X" % (sp["name"], mx))
            # {er} / {sae}: flags suppressed, no #XM with unmasked MXCSR and an SNaN
            snan = spl[7]
            if rcf == "er":
                for rc in range(4):
                    G(variant="reg", dst=27, s1=28, s2=29, rc=rc, title="%s {er} rc=%d" % (sp["name"], rc))
                    G(variant="reg", dst=27, s1=28, s2=29, rc=rc, bvals=[snan], mxcsr=0x1F00,
                      title="%s {er} rc=%d SNaN, MXCSR unmasked: no #XM" % (sp["name"], rc))
                # rounding from L'L overrides MXCSR.RC
                G(variant="reg", dst=27, s1=28, s2=29, rc=0, mxcsr=0x7F80,
                  title="%s {rn-sae} with MXCSR.RC=RZ" % sp["name"])
            else:
                for ll in (0, 1, 2, 3):
                    G(variant="reg", dst=27, s1=28, s2=29, sae=True, ll=ll, bvals=[snan], mxcsr=0x1F00,
                      title="%s {sae} L'L=%d" % (sp["name"], ll))
            # #XM: unmasked invalid, destination unchanged
            G(variant="reg", dst=30, s1=31, s2=0, bvals=[snan], mxcsr=0x1F00,
              title="%s SNaN IM=0 -> #XM" % sp["name"])
            # masked off (k1[0] = 0): no exception at all
            G(variant="reg", dst=30, s1=31, s2=0, bvals=[snan], mxcsr=0x1F00, kreg=2, kval=0xFE,
              title="%s SNaN masked off IM=0: no #XM" % sp["name"])
            G(variant="reg", dst=30, s1=31, s2=0, bvals=[snan], mxcsr=0x1F00, kreg=2, kval=0xFE, z=1,
              title="%s SNaN masked off {z} IM=0: no #XM" % sp["name"])
            # disp8*N with N = element size (Tuple1 Scalar)
            for d8 in (1, -1, 127, -127):
                G(variant="mem", dst=1, s1=2, mem_off=d8 * esz, title="%s disp8=%d N=%d" % (sp["name"], d8, esz))
            # fault suppression: the m32/m64 operand on the unmapped page
            G(variant="mem", dst=3, s1=4, kreg=6, kval=0, mem_off=UNMAPPED, mem_disp32=True,
              title="%s mem unmapped, k1[0]=0: suppressed" % sp["name"])
            G(variant="mem", dst=3, s1=4, kreg=6, kval=0, z=1, mem_off=UNMAPPED, mem_disp32=True,
              title="%s mem unmapped, k1[0]=0 {z}: suppressed" % sp["name"])
            c = Case("%s mem unmapped, k1[0]=1: #PF" % sp["name"])
            c.set_zmm(3, rnd_bytes(64)); c.set_zmm(4, rnd_bytes(64)); c.k[6] = 1
            c.code = evex(1, sp["pp"], w, opc, 3, Mem(RSI, UNMAPPED, disp32=True), vvvv=4, aaa=6)
            c.fault = "#PF"
            emit(c)
            # #UD
            emit(ud_case("%s EVEX.b with memory #UD" % sp["name"], sp, rm=Mem(RSI, 0), b=1, n=esz))
            emit(ud_case("%s wrong W #UD" % sp["name"], sp, w=1 - w))
            emit(ud_case("%s {z} with aaa=0 #UD" % sp["name"], sp, z=1))
            emit(ud_case("%s 66 prefix before EVEX #UD" % sp["name"], sp, prefixes=b"\x66"))


# -- VMOVSS / VMOVSD ----------------------------------------------------------------------
def gen_movs():
    comment("--- VMOVSS/VMOVSD (EVEX.LLIG F3/F2 0F 10/11, E10): load, store, register merge forms")
    for w in (0, 1):
        name = "VMOVSD" if w else "VMOVSS"
        pp = 3 if w else 2
        esz = 8 if w else 4
        comment("%s (EVEX.LLIG.%s.0F.W%d 10/11)" % (name, "F2" if w else "F3", w))
        for kreg, kv, z in ((0, None, 0), (2, 1, 0), (2, 0xFE, 0), (2, 1, 1), (2, 0xFE, 1)):
            tag = "nomask" if not kreg else ("k1[0]=%d%s" % (kv & 1, " {z}" if z else ""))
            # load: xmm1{k1}{z}, m -> DEST[MAXVL-1:esz] := 0
            c = Case("%s load %s" % (name, tag))
            old = rnd_bytes(64); c.set_zmm(9, old)
            data = rnd_bytes(esz); c.mem[MEM_RSI + 3 * esz] = data
            if kreg:
                c.k[kreg] = kv
            act = kreg == 0 or kv & 1
            e0 = data if act else (bytes(esz) if z else old[:esz])
            c.code = evex(1, pp, w, 0x10, 9, Mem(RSI, 3 * esz), z=z, aaa=kreg, n=esz)
            c.exp.append("zmm9=%s" % hexs(e0 + bytes(64 - esz)))
            emit(c)
            # register form 10: xmm1{k1}{z}, xmm2 (vvvv), xmm3 (r/m)
            for opc, (d, s1, s2) in ((0x10, (1, 22, 3)), (0x11, (17, 5, 30))):
                c = Case("%s %02X reg %s" % (name, opc, tag))
                imgs = {d: rnd_bytes(64), s1: rnd_bytes(64), s2: rnd_bytes(64)}
                for r, im in imgs.items():
                    c.set_zmm(r, im)
                if kreg:
                    c.k[kreg] = kv
                e0 = imgs[s2][:esz] if act else (bytes(esz) if z else imgs[d][:esz])
                if opc == 0x10:
                    c.code = evex(1, pp, w, 0x10, d, s2, vvvv=s1, z=z, aaa=kreg)
                else:                       # 11: destination in r/m, SRC2 in reg
                    c.code = evex(1, pp, w, 0x11, s2, d, vvvv=s1, z=z, aaa=kreg)
                c.exp.append("zmm%d=%s" % (d, hexs(e0 + imgs[s1][esz:16] + bytes(48))))
                emit(c)
            if z:
                continue
            # store m{k1}, xmm1: m written only when k1[0] = 1
            c = Case("%s store %s" % (name, tag))
            src = rnd_bytes(64); c.set_zmm(18, src)
            oldm = rnd_bytes(esz); c.mem[MEM_RSI - 2 * esz] = oldm
            if kreg:
                c.k[kreg] = kv
            c.code = evex(1, pp, w, 0x11, 18, Mem(RSI, -2 * esz), aaa=kreg, n=esz)
            c.exp.append("m+0x%X=%s" % (MEM_RSI - 2 * esz, hexs(src[:esz] if act else oldm)))
            emit(c)
        # aliasing of the register forms
        for opc in (0x10, 0x11):
            c = Case("%s %02X reg dst=src1=src2" % (name, opc))
            im = rnd_bytes(64); c.set_zmm(6, im)
            c.code = evex(1, pp, w, opc, 6, 6, vvvv=6)
            c.exp.append("zmm6=%s" % hexs(im[:16] + bytes(48)))
            emit(c)
            c = Case("%s %02X reg dst=src2, merge k1[0]=1" % (name, opc))
            a, b = rnd_bytes(64), rnd_bytes(64); c.set_zmm(7, a); c.set_zmm(8, b); c.k[1] = 0x81
            if opc == 0x10:
                c.code = evex(1, pp, w, 0x10, 7, 7, vvvv=8, aaa=1)
            else:
                c.code = evex(1, pp, w, 0x11, 7, 7, vvvv=8, aaa=1)
            c.exp.append("zmm7=%s" % hexs(a[:esz] + b[esz:16] + bytes(48)))
            emit(c)
        # disp8*N = element size
        for d8 in (1, -1, 127, -127):
            c = Case("%s load disp8=%d N=%d" % (name, d8, esz))
            c.set_zmm(12, rnd_bytes(64))
            data = rnd_bytes(esz); c.mem[MEM_RSI + d8 * esz] = data
            c.code = evex(1, pp, w, 0x10, 12, Mem(RSI, d8 * esz), n=esz)
            c.exp.append("zmm12=%s" % hexs(data + bytes(64 - esz)))
            emit(c)
            c = Case("%s store disp8=%d N=%d" % (name, d8, esz))
            src = rnd_bytes(64); c.set_zmm(13, src)
            c.mem[MEM_RSI + d8 * esz] = rnd_bytes(esz)
            c.code = evex(1, pp, w, 0x11, 13, Mem(RSI, d8 * esz), n=esz)
            c.exp.append("m+0x%X=%s" % (MEM_RSI + d8 * esz, hexs(src[:esz])))
            emit(c)
        # fault suppression (E10): k1[0] = 0 on the unmapped page
        c = Case("%s load unmapped k1[0]=0 {z}: suppressed" % name)
        c.set_zmm(2, rnd_bytes(64)); c.k[3] = 0xFE
        c.code = evex(1, pp, w, 0x10, 2, Mem(RSI, UNMAPPED, disp32=True), z=1, aaa=3)
        c.exp.append("zmm2=%s" % hexs(bytes(64)))
        emit(c)
        c = Case("%s load unmapped k1[0]=1: #PF" % name)
        c.set_zmm(2, rnd_bytes(64)); c.k[3] = 1
        c.code = evex(1, pp, w, 0x10, 2, Mem(RSI, UNMAPPED, disp32=True), z=1, aaa=3)
        c.fault = "#PF"
        emit(c)
        c = Case("%s store unmapped k1[0]=0: suppressed" % name)
        c.set_zmm(2, rnd_bytes(64)); c.k[3] = 0
        c.code = evex(1, pp, w, 0x11, 2, Mem(RSI, UNMAPPED, disp32=True), aaa=3)
        emit(c)
        c = Case("%s store unmapped no mask: #PF" % name)
        c.set_zmm(2, rnd_bytes(64))
        c.code = evex(1, pp, w, 0x11, 2, Mem(RSI, UNMAPPED, disp32=True))
        c.fault = "#PF"
        emit(c)
        # #UD (Tables 2-41/2-42/2-43, the MOVSS/MOVSD page)
        for opc in (0x10, 0x11):
            sp = dict(mmm=1, pp=pp, w=w, opc=opc, scalar=True)
            emit(ud_case("%s %02X mem vvvv != 1111b #UD" % (name, opc), sp, rm=Mem(RSI, 0), vvvv=5))
            emit(ud_case("%s %02X mem V'=0 #UD" % (name, opc), sp, rm=Mem(RSI, 0), vvvv=None, p2_vp=0))
            emit(ud_case("%s %02X EVEX.b mem #UD" % (name, opc), sp, rm=Mem(RSI, 0), vvvv=None, b=1))
            emit(ud_case("%s %02X EVEX.b reg #UD" % (name, opc), sp, b=1))
            emit(ud_case("%s %02X wrong W #UD" % (name, opc), sp, w=1 - w))
            emit(ud_case("%s %02X {z} aaa=0 #UD" % (name, opc), sp, z=1))
        emit(ud_case("%s store m{k1}{z} #UD (Table 2-42: z with a memory destination)" % name,
                     dict(mmm=1, pp=pp, w=w, opc=0x11, scalar=True), rm=Mem(RSI, 0), vvvv=None, z=1,
                     aaa=1))


# -- (U)COMISS/SD -------------------------------------------------------------------------
def comis_spec(w, ordered):
    f = F64 if w else F32
    name = ("VCOMIS" if ordered else "VUCOMIS") + ("D" if w else "S")
    return dict(name=name, mmm=1, pp=1 if w else 0, opc=0x2F if ordered else 0x2E, w=w,
                scalar=True, fp=True, dest="flags", vvvv=False,
                elem=lambda j, a, b, d, mx, rc, f=f, o=ordered: fp_comis(a, b, f, mx, o))


def gen_comis():
    comment("--- VCOMISS/SD, VUCOMISS/SD (EVEX.LLIG 0F 2F/2E, {sae}, E3NF): ZF PF CF, OF SF AF := 0")
    for ordered in (1, 0):
        for w in (0, 1):
            sp = comis_spec(w, ordered)
            f, esz = fmt_of(sp), esz_of(sp)
            comment("%s (EVEX.LLIG.%s.0F.W%d %02X /r)" % (sp["name"], "66" if w else "NP", w, sp["opc"]))
            spl = specials(f)
            for i, a in enumerate(spl):
                for jj in range(0, len(spl), 3):
                    b = spl[(jj + i) % len(spl)]
                    emit(gen_case(sp, 16, dst=None, s1=1 + (i % 30), s2=2 + (jj % 29), avals=[a], bvals=[b],
                                  rflags=RNG.choice([0x202, 0xAD7, 0x8D7 | 0x200]),
                                  title="%s %08X vs %08X" % (sp["name"], a & 0xFFFFFFFF, b & 0xFFFFFFFF)))
            emit(gen_case(sp, 16, variant="mem", dst=None, s1=17, title="%s mem" % sp["name"]))
            emit(gen_case(sp, 16, variant="reg", dst=None, s1=5, s2=5, title="%s same register" % sp["name"]))
            for mx in (MXCSR_DEFAULT | DAZ, 0x1F00):
                emit(gen_case(sp, 16, dst=None, s1=3, s2=4, avals=[spl[8]], bvals=[0],
                              mxcsr=mx, title="%s denormal vs 0 mxcsr=%X" % (sp["name"], mx)))
            # {sae}: no IE, no #XM; L'L ignored
            for ll in (0, 3):
                emit(gen_case(sp, 16, dst=None, s1=3, s2=4, avals=[spl[7]], bvals=[spl[2]], sae=True, ll=ll,
                              mxcsr=0x1F00, title="%s {sae} SNaN L'L=%d" % (sp["name"], ll)))
            emit(gen_case(sp, 16, dst=None, s1=3, s2=4, avals=[spl[6]], bvals=[spl[2]], mxcsr=0x1F00,
                          title="%s QNaN IM=0" % sp["name"]))
            emit(gen_case(sp, 16, dst=None, s1=3, s2=4, avals=[spl[2]], bvals=[spl[7]], mxcsr=0x1F00,
                          title="%s SNaN IM=0" % sp["name"]))
            for d8 in (1, -127):
                emit(gen_case(sp, 16, variant="mem", dst=None, s1=6, mem_off=d8 * esz,
                              title="%s disp8=%d N=%d" % (sp["name"], d8, esz)))
            # E3NF: no masking, the memory operand always faults
            c = Case("%s mem unmapped #PF" % sp["name"])
            c.set_zmm(1, rnd_bytes(64))
            c.code = evex(1, sp["pp"], w, sp["opc"], 1, Mem(RSI, UNMAPPED, disp32=True))
            c.fault = "#PF"
            emit(c)
            emit(ud_case("%s aaa != 0 #UD" % sp["name"], sp, vvvv=None, aaa=1))
            emit(ud_case("%s z #UD" % sp["name"], sp, vvvv=None, z=1, aaa=1))
            emit(ud_case("%s vvvv != 1111b #UD" % sp["name"], sp, vvvv=6))
            emit(ud_case("%s V'=0 #UD" % sp["name"], sp, vvvv=None, p2_vp=0))
            emit(ud_case("%s EVEX.b mem #UD" % sp["name"], sp, vvvv=None, rm=Mem(RSI, 0), b=1))
            emit(ud_case("%s wrong W #UD" % sp["name"], sp, vvvv=None, w=1 - w))
            emit(ud_case("%s F3 prefix (no form) #UD" % sp["name"], sp, vvvv=None, pp=2))


# -- VCMPPS/PD/SS/SD -> k ------------------------------------------------------------------
def cmp_spec(w, scalar, pred):
    f = F64 if w else F32
    name = "VCMP%s%s" % ("S" if scalar else "P", "D" if w else "S")
    pp = (3 if w else 2) if scalar else (1 if w else 0)
    return dict(name=name, mmm=1, pp=pp, opc=0xC2, w=w, scalar=scalar, fp=True, dest="k", imm=pred,
                elem=lambda j, a, b, d, mx, rc, f=f, p=pred: fp_cmp(p, a, b, f, mx))


def cmp_values(f, n):
    spl = specials(f)
    out = []
    for _ in range(n):
        out.append(RNG.choice(spl) if RNG.getrandbits(1) else rnd_fp(f))
    return out


def gen_vcmp():
    comment("--- VCMPPS/PD (Full, E2) and VCMPSS/SD (LIG, Tuple1 Scalar, E3) -> k1{k2}, imm8[4:0], {sae}")
    for scalar in (False, True):
        for w in (0, 1):
            f = F64 if w else F32
            esz = 8 if w else 4
            base = cmp_spec(w, scalar, 0)
            comment("%s (EVEX.%s.%s.0F.W%d C2 /r ib)" % (base["name"], "LLIG" if scalar else "128/256/512",
                    ["NP", "66", "F3", "F2"][base["pp"]], w))
            vls = (16,) if scalar else (16, 32, 64)
            for pred in range(32):
                sp = cmp_spec(w, scalar, pred)
                for vl in vls:
                    n = 1 if scalar else vl // esz
                    # half of the lanes compare a value with itself or a near value
                    a = cmp_values(f, n)
                    b = [x if RNG.getrandbits(2) == 0 else y for x, y in zip(a, cmp_values(f, n))]
                    emit(gen_case(sp, vl, variant="reg", dst=1, s1=2, s2=3, avals=a, bvals=b,
                                  title="%s pred=%02X VL%d reg" % (sp["name"], pred, vl * 8)))
                    emit(gen_case(sp, vl, variant="reg", dst=6, s1=21, s2=17, kreg=3, kval=RNG.getrandbits(64),
                                  avals=a, bvals=b, title="%s pred=%02X VL%d k2" % (sp["name"], pred, vl * 8)))
                    emit(gen_case(sp, vl, variant="mem", dst=5, s1=8, avals=a, bvals=b,
                                  title="%s pred=%02X VL%d mem" % (sp["name"], pred, vl * 8)))
                    if not scalar:
                        emit(gen_case(sp, vl, variant="bcst", dst=7, s1=9, kreg=2, kval=RNG.getrandbits(64),
                                      avals=a, bvals=b[:1], title="%s pred=%02X VL%d {1toN}" % (sp["name"], pred,
                                                                                               vl * 8)))
                # {sae}: VL 512 (vector) / LIG, flags and #XM suppressed (QNaN on a signalling predicate)
                nn = 1 if scalar else 64 // esz
                a = cmp_values(f, nn); a[0] = specials(f)[6]
                b = cmp_values(f, nn)
                emit(gen_case(sp, 64, variant="reg", dst=4, s1=10, s2=11, avals=a, bvals=b, sae=True,
                              ll=RNG.choice([0, 1, 2, 3]), mxcsr=0x1F00,
                              title="%s pred=%02X {sae}" % (sp["name"], pred)))
                # imm8[7:5] reserved: ignored
                emit(gen_case(sp, 16 if scalar else 64, variant="reg", dst=4, s1=12, s2=13,
                              imm=pred | (RNG.getrandbits(3) << 5),
                              title="%s pred=%02X imm8[7:5] != 0" % (sp["name"], pred)))
            sp0 = cmp_spec(w, scalar, 1)                                 # LT_OS (signalling)
            sq = cmp_spec(w, scalar, 0x11)                               # LT_OQ (quiet)
            nn = 1 if scalar else 64 // esz
            spl = specials(f)
            for s_, nm in ((sp0, "LT_OS"), (sq, "LT_OQ")):
                for mx in (0x1F00, MXCSR_DEFAULT | DAZ, MXCSR_DEFAULT):
                    a = [spl[(i * 3) % len(spl)] for i in range(nn)]
                    b = [spl[(i * 5 + 6) % len(spl)] for i in range(nn)]
                    emit(gen_case(s_, 64, variant="reg", dst=6, s1=15, s2=16, avals=a, bvals=b, mxcsr=mx,
                                  title="%s %s specials mxcsr=%X" % (s_["name"], nm, mx)))
                # NaN only in masked-off lanes, IM = 0: no #XM
                a = [spl[6]] * nn; b = [spl[7]] * nn
                kv = 0
                for i in range(nn):
                    if i % 2:
                        a[i], b[i] = rnd_fp(f), rnd_fp(f)
                        kv |= 1 << i
                emit(gen_case(s_, 64, variant="reg", dst=6, s1=15, s2=16, avals=a, bvals=b, mxcsr=0x1F00,
                              kreg=5, kval=kv, title="%s %s NaN only masked off, IM=0" % (s_["name"], nm)))
            # disp8*N
            for d8 in (1, -1, 127, -127):
                for vl in vls:
                    nN = esz if scalar else vl
                    emit(gen_case(sp0, vl, variant="mem", dst=1, s1=2, mem_off=d8 * nN,
                                  title="%s disp8=%d N=%d" % (sp0["name"], d8, nN)))
                    if not scalar:
                        emit(gen_case(sp0, vl, variant="bcst", dst=1, s1=2, mem_off=d8 * esz,
                                      title="%s {1toN} disp8=%d N=%d" % (sp0["name"], d8, esz)))
            # fault suppression: masked memory
            if scalar:
                emit(gen_case(sp0, 16, variant="mem", dst=3, s1=4, kreg=6, kval=0xFE, mem_off=UNMAPPED,
                              mem_disp32=True, title="%s mem unmapped k2[0]=0: suppressed" % sp0["name"]))
            else:
                c = Case("%s mem: elements on the unmapped page masked off" % sp0["name"])
                nl = 32 // esz
                a = [rnd_fp(f) for _ in range(64 // esz)]
                b = [rnd_fp(f) for _ in range(nl)]
                c.set_zmm(4, pack(a, esz)); c.k[6] = (1 << nl) - 1; c.k[3] = RNG.getrandbits(64)
                c.mem[MEM_RSI + UNMAPPED - 32] = pack(b, esz)
                r = 0
                for j in range(nl):
                    if fp_cmp(1, a[j], b[j], f, MXCSR_DEFAULT)[0]:
                        r |= 1 << j
                fl = 0
                for j in range(nl):
                    fl |= fp_cmp(1, a[j], b[j], f, MXCSR_DEFAULT)[1]
                c.code = evex(1, sp0["pp"], w, 0xC2, 3, Mem(RSI, UNMAPPED - 32, disp32=True), vvvv=4, ll=2,
                              aaa=6, imm=1)
                c.exp.append("k3=0x%X" % r)
                if fl:
                    c.exp.append("mxcsr=0x%X" % (MXCSR_DEFAULT | fl))
                emit(c)
                c = Case("%s mem: an active element on the unmapped page #PF" % sp0["name"])
                c.set_zmm(4, pack(a, esz)); c.k[6] = 1 << nl
                c.code = evex(1, sp0["pp"], w, 0xC2, 3, Mem(RSI, UNMAPPED - 32, disp32=True), vvvv=4, ll=2,
                              aaa=6, imm=1)
                c.fault = "#PF"
                emit(c)
            # #UD
            emit(ud_case("%s {z} #UD" % sp0["name"], sp0, z=1, aaa=2))
            emit(ud_case("%s k1 with EVEX.R'=0 #UD" % sp0["name"], sp0, reg=17))
            emit(ud_case("%s k1 with EVEX.R=0 #UD" % sp0["name"], sp0, reg=9))
            emit(ud_case("%s wrong W #UD" % sp0["name"], sp0, w=1 - w))
            if scalar:
                emit(ud_case("%s EVEX.b mem (Tuple1 Scalar) #UD" % sp0["name"], sp0, rm=Mem(RSI, 0), b=1,
                             n=esz))
            else:
                emit(ud_case("%s L'L=11b #UD" % sp0["name"], sp0, ll=3))


# -- FMA ----------------------------------------------------------------------------------
FMA_PACKED_OPC = [0x96, 0x97, 0x98, 0x9A, 0x9C, 0x9E, 0xA6, 0xA7, 0xA8, 0xAA, 0xAC, 0xAE,
                  0xB6, 0xB7, 0xB8, 0xBA, 0xBC, 0xBE]
FMA_SCALAR_OPC = [0x99, 0x9B, 0x9D, 0x9F, 0xA9, 0xAB, 0xAD, 0xAF, 0xB9, 0xBB, 0xBD, 0xBF]


def fma_name(opc, w, scalar):
    kind = fma_kind(opc)[0]
    return "V%s%s%s%s" % (kind, FMA_ORDER[opc >> 4], "S" if scalar else "P", "D" if w else "S")


def fma_spec(opc, w, scalar):
    f = F64 if w else F32
    return dict(name=fma_name(opc, w, scalar), mmm=2, pp=1, opc=opc, w=w, scalar=scalar, fp=True,
                upper="dest", dsrc=True,
                elem=lambda j, a, b, d, mx, rc, f=f, o=opc: fma_elem(o, j, d, a, b, f, mx, rc))


def fma_special_triples(f, n):
    spl = specials(f)
    extra = ([0x00400001, 0x80000003, 0x3F800001, 0x3F7FFFFF, 0xBF800000, 0x34000000]
             if f is F32 else
             [0x0008000000000001, 0x8000000000000003, 0x3FF0000000000001, 0x3FEFFFFFFFFFFFFF,
              0xBFF0000000000000, 0x3CB0000000000000])
    pool = spl + extra
    return ([RNG.choice(pool) for _ in range(n)], [RNG.choice(pool) for _ in range(n)],
            [RNG.choice(pool) for _ in range(n)])


def gen_fma():
    comment("--- EVEX FMA: VFMADD/VFMSUB/VFNMADD/VFNMSUB 132/213/231 PS/PD/SS/SD, VFMADDSUB/VFMSUBADD "
            "132/213/231 PS/PD ({er}, DEST is a source, masking merges with the old DEST)")
    for scalar in (False, True):
        for opc in (FMA_SCALAR_OPC if scalar else FMA_PACKED_OPC):
            for w in (0, 1):
                sp = fma_spec(opc, w, scalar)
                f, esz = fmt_of(sp), esz_of(sp)
                comment("%s (EVEX.%s.66.0F38.W%d %02X /r)" % (sp["name"], "LLIG" if scalar else "128/256/512",
                                                             w, opc))
                vls = (16,) if scalar else (16, 32, 64)
                for vl in vls:
                    emit(gen_case(sp, vl, variant="reg", dst=1, s1=2, s2=3))
                    emit(gen_case(sp, vl, variant="reg", dst=20, s1=2, s2=29, kreg=1, kval=RNG.getrandbits(64),
                                  title="%s VL%d merge" % (sp["name"], vl * 8)))
                    emit(gen_case(sp, vl, variant="reg", dst=4, s1=5, s2=6, kreg=2, kval=RNG.getrandbits(64),
                                  z=1, title="%s VL%d zero" % (sp["name"], vl * 8)))
                    emit(gen_case(sp, vl, variant="mem", dst=7, s1=8))
                    if scalar:
                        emit(gen_case(sp, vl, variant="mem", dst=9, s1=10, kreg=3, kval=RNG.getrandbits(64),
                                      title="%s mem merge" % sp["name"]))
                    else:
                        emit(gen_case(sp, vl, variant="bcst", dst=9, s1=10, kreg=3, kval=RNG.getrandbits(64),
                                      title="%s VL%d {1toN} merge" % (sp["name"], vl * 8)))
                nn = 1 if scalar else 64 // esz
                vlx = 16 if scalar else 64
                # special operand triples
                for t in range(3 if scalar else 2):
                    a, b, d = fma_special_triples(f, nn)
                    emit(gen_case(sp, vlx, dst=11, s1=12, s2=13, avals=a, bvals=b, dvals=d,
                                  title="%s specials %d" % (sp["name"], t)))
                # MXCSR rounding / DAZ / FTZ, with tiny and cancelling lanes
                for mx in (0x3F80, 0x5F80, 0x7F80, MXCSR_DEFAULT | DAZ, MXCSR_DEFAULT | FTZ):
                    a = [rnd_fp(f) for _ in range(nn)]
                    b = [rnd_fp(f) for _ in range(nn)]
                    d = [rnd_fp(f) for _ in range(nn)]
                    if f is F32:
                        d[0], a[0], b[0] = 0x00800001, 0x3F000000, 0x00000001
                    else:
                        d[0], a[0], b[0] = 0x0010000000000001, 0x3FE0000000000000, 0x0000000000000001
                    emit(gen_case(sp, vlx, dst=14, s1=15, s2=16, avals=a, bvals=b, dvals=d, mxcsr=mx,
                                  title="%s mxcsr=%X" % (sp["name"], mx)))
                # {er}: L'L = RC, VL 512 / LIG, flags suppressed
                for rc in range(4):
                    a = [rnd_fp(f) for _ in range(nn)]
                    b = [rnd_fp(f) for _ in range(nn)]
                    b[0] = specials(f)[7]
                    emit(gen_case(sp, vlx, dst=17, s1=18, s2=19, avals=a, bvals=b, rc=rc, mxcsr=0x1F00,
                                  title="%s {er} rc=%d" % (sp["name"], rc)))
                # #XM: unmasked IE in an active lane; SNaN in DEST masked off only: nothing
                d = [rnd_fp(f) for _ in range(nn)]
                d[0] = specials(f)[7]
                emit(gen_case(sp, vlx, dst=21, s1=22, s2=23, dvals=d, mxcsr=0x1F00,
                              title="%s SNaN in DEST, IM=0 -> #XM" % sp["name"]))
                emit(gen_case(sp, vlx, dst=21, s1=22, s2=23, dvals=d, mxcsr=0x1F00, kreg=4,
                              kval=RNG.getrandbits(64) & ~1, title="%s SNaN in DEST masked off, IM=0" % sp["name"]))
                emit(gen_case(sp, vlx, dst=21, s1=22, s2=23, dvals=d, mxcsr=0x1F00, kreg=4, z=1,
                              kval=RNG.getrandbits(64) & ~1,
                              title="%s SNaN in DEST masked off {z}, IM=0" % sp["name"]))
                # destination = other sources
                emit(gen_case(sp, vlx, dst=24, s1=24, s2=25, kreg=5, kval=RNG.getrandbits(64),
                              title="%s dst=src2" % sp["name"]))
                emit(gen_case(sp, vlx, dst=25, s1=26, s2=25, kreg=5, kval=RNG.getrandbits(64), z=1,
                              title="%s dst=src3" % sp["name"]))
                emit(gen_case(sp, vlx, dst=27, s1=27, s2=27, title="%s dst=src2=src3" % sp["name"]))
                emit(ud_case("%s wrong pp #UD" % sp["name"], sp, pp=0))
                emit(ud_case("%s {z} aaa=0 #UD" % sp["name"], sp, z=1))
                if scalar:
                    emit(ud_case("%s EVEX.b mem #UD" % sp["name"], sp, rm=Mem(RSI, 0), b=1, n=esz))
                    for d8 in (1, -127):
                        emit(gen_case(sp, 16, variant="mem", dst=28, s1=29, mem_off=d8 * esz,
                                      title="%s disp8=%d N=%d" % (sp["name"], d8, esz)))
                    emit(gen_case(sp, 16, variant="mem", dst=30, s1=31, kreg=6, kval=0xFE, mem_off=UNMAPPED,
                                  mem_disp32=True, title="%s mem unmapped k1[0]=0: suppressed" % sp["name"]))
                else:
                    emit(ud_case("%s L'L=11b #UD" % sp["name"], sp, ll=3))
                    for d8 in (1, -127):
                        emit(gen_case(sp, 64, variant="mem", dst=28, s1=29, mem_off=d8 * 64,
                                      title="%s disp8=%d N=64" % (sp["name"], d8)))
                        emit(gen_case(sp, 32, variant="bcst", dst=28, s1=29, mem_off=d8 * esz,
                                      title="%s {1toN} disp8=%d N=%d" % (sp["name"], d8, esz)))
                    # masked load next to the unmapped page
                    nl = 32 // esz
                    kv = (1 << nl) - 1
                    c = gen_case(sp, 64, variant="mem", dst=30, s1=31, kreg=6, kval=kv, mem_off=UNMAPPED - 32,
                                 mem_disp32=True, title="%s mem upper half unmapped, masked off" % sp["name"])
                    # the generic builder wrote 64 bytes at MEM+0xFFE0: keep only the mapped 32
                    key = MEM_RSI + UNMAPPED - 32
                    c.mem[key] = c.mem[key][:32]
                    emit(c)


# -- VPBLENDMD/Q, VBLENDMPS/PD --------------------------------------------------------------
def blend_spec(opc, w):
    names = {0x64: ("VPBLENDMD", "VPBLENDMQ"), 0x65: ("VBLENDMPS", "VBLENDMPD")}
    return dict(name=names[opc][w], mmm=2, pp=1, opc=opc, w=w, fp=False, msrc1=True,
                elem=lambda j, a, b, d, mx, rc: (b, 0))


def gen_blend():
    comment("--- VPBLENDMD/Q (66 0F38 64), VBLENDMPS/PD (66 0F38 65): DEST[j] := k1[j] ? SRC2 : SRC1 "
            "(merging) / 0 (zeroing); no mask: SRC2; E4")
    for opc in (0x64, 0x65):
        for w in (0, 1):
            sp = blend_spec(opc, w)
            esz = 8 if w else 4
            comment("%s (EVEX.66.0F38.W%d %02X /r)" % (sp["name"], w, opc))
            for vl in (16, 32, 64):
                emit(gen_case(sp, vl, variant="reg", dst=1, s1=2, s2=3, title="%s VL%d nomask" % (sp["name"], vl * 8)))
                emit(gen_case(sp, vl, variant="reg", dst=17, s1=30, s2=9, kreg=3, kval=RNG.getrandbits(64),
                              title="%s VL%d merge (SRC1 lanes)" % (sp["name"], vl * 8)))
                emit(gen_case(sp, vl, variant="reg", dst=4, s1=5, s2=25, kreg=7, kval=RNG.getrandbits(64), z=1,
                              title="%s VL%d zero" % (sp["name"], vl * 8)))
                emit(gen_case(sp, vl, variant="mem", dst=6, s1=7, kreg=1, kval=RNG.getrandbits(64),
                              title="%s VL%d mem merge" % (sp["name"], vl * 8)))
                emit(gen_case(sp, vl, variant="bcst", dst=8, s1=31, kreg=2, kval=RNG.getrandbits(64),
                              title="%s VL%d {1toN} merge" % (sp["name"], vl * 8)))
            # aliasing
            emit(gen_case(sp, 64, dst=10, s1=10, s2=11, kreg=4, kval=0xA5A5, title="%s dst=src1" % sp["name"]))
            emit(gen_case(sp, 64, dst=11, s1=10, s2=11, kreg=4, kval=0x5A5A, title="%s dst=src2" % sp["name"]))
            emit(gen_case(sp, 64, dst=12, s1=13, s2=13, kreg=4, kval=0x3C3C, title="%s src1=src2" % sp["name"]))
            emit(gen_case(sp, 64, dst=12, s1=13, s2=14, kreg=4, kval=0, title="%s k=0 merge -> SRC1" % sp["name"]))
            emit(gen_case(sp, 64, dst=12, s1=13, s2=14, kreg=4, kval=0, z=1, title="%s k=0 {z} -> 0" % sp["name"]))
            # SNaN / any bit pattern passes unchanged, MXCSR untouched (no SIMD FP exception)
            emit(gen_case(sp, 64, dst=15, s1=16, s2=18, mxcsr=0x1F00 | DAZ,
                          avals=[specials(F64 if w else F32)[7]] * (64 // esz),
                          bvals=[specials(F64 if w else F32)[8]] * (64 // esz), kreg=1, kval=0x55,
                          title="%s SNaN/denormal lanes, IM=0 DAZ: copied, no exception" % sp["name"]))
            for d8 in (1, -127):
                emit(gen_case(sp, 64, variant="mem", dst=19, s1=20, mem_off=d8 * 64,
                              title="%s disp8=%d N=64" % (sp["name"], d8)))
                emit(gen_case(sp, 16, variant="bcst", dst=19, s1=20, mem_off=d8 * esz,
                              title="%s {1toN} disp8=%d N=%d" % (sp["name"], d8, esz)))
            # fault suppression (E4)
            nl = 32 // esz
            c = gen_case(sp, 64, variant="mem", dst=21, s1=22, kreg=5, kval=(1 << nl) - 1, mem_off=UNMAPPED - 32,
                         mem_disp32=True, title="%s mem upper half unmapped, masked off" % sp["name"])
            key = MEM_RSI + UNMAPPED - 32
            c.mem[key] = c.mem[key][:32]
            emit(c)
            c = Case("%s mem: active element unmapped #PF" % sp["name"])
            c.set_zmm(22, rnd_bytes(64)); c.k[5] = 1 << nl
            c.code = evex(2, 1, w, opc, 21, Mem(RSI, UNMAPPED - 32, disp32=True), vvvv=22, ll=2, aaa=5)
            c.fault = "#PF"
            emit(c)
            emit(ud_case("%s EVEX.b reg #UD" % sp["name"], sp, b=1))
            emit(ud_case("%s L'L=11b #UD" % sp["name"], sp, ll=3))
            emit(ud_case("%s pp=NP #UD" % sp["name"], sp, pp=0))
            emit(ud_case("%s {z} aaa=0 #UD" % sp["name"], sp, z=1))


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

    mx = MXCSR_DEFAULT
    # encodings (SDM Vol2A 2.7.1 field layout)
    chk("enc vfmadd231ps", evex(2, 1, 0, 0xB8, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF2, 0x6D, 0x48, 0xB8, 0xCB]))
    chk("enc vaddss", evex(1, 2, 0, 0x58, 1, 3, vvvv=2, ll=0), bytes([0x62, 0xF1, 0x6E, 0x08, 0x58, 0xCB]))
    chk("enc vcmpps k1", evex(1, 0, 0, 0xC2, 1, 3, vvvv=2, ll=2, imm=0x11),
        bytes([0x62, 0xF1, 0x6C, 0x48, 0xC2, 0xCB, 0x11]))
    # FMA: one rounding
    chk("fma 1*1+1", fp_fma(0x3F800000, 0x3F800000, 0x3F800000, F32, mx), (0x40000000, 0))
    # (1 + 2^-23)(1 - 2^-23) - 1 = -2^-46 exactly (a separate multiply would give 0)
    chk("fma fused", fp_fma(0x3F800001, 0x3F7FFFFE, 0x3F800000, F32, mx, negc=True), (0xA8800000, 0))
    chk("fma 0*inf+qnan", fp_fma(0, 0x7F800000, 0x7FC00001, F32, mx), (0x7FC00001, 0))
    chk("fma 0*inf+snan", fp_fma(0, 0x7F800000, 0x7FA00000, F32, mx), (0x7FE00000, IE))
    chk("fma 0*inf+1", fp_fma(0, 0x7F800000, 0x3F800000, F32, mx), (0xFFC00000, IE))
    chk("fma inf-inf", fp_fma(0x7F800000, 0x3F800000, 0xFF800000, F32, mx), (0xFFC00000, IE))
    chk("fma nan order", fp_fma(0x7FC00002, 0x7FA00000, 0x7FC00003, F32, mx), (0x7FC00002, IE))
    chk("fma nan y", fp_fma(0x3F800000, 0xFFA00001, 0x7FC00003, F32, mx), (0xFFE00001, IE))
    chk("fma -nan not negated", fp_fma(0x3F800000, 0x3F800000, 0x7FC00003, F32, mx, negp=True, negc=True),
        (0x7FC00003, 0))
    chk("fma +0+-0 rn", fp_fma(0, 0x3F800000, 0x80000000, F32, mx), (0, 0))
    chk("fma +0+-0 rd", fp_fma(0, 0x3F800000, 0x80000000, F32, 0x3F80), (0x80000000, 0))
    chk("fma -0-0", fp_fma(0x80000000, 0x3F800000, 0x80000000, F32, mx), (0x80000000, 0))
    chk("fma F-F rd", fp_fma(0x3F800000, 0x3F800000, 0xBF800000, F32, 0x3F80), (0x80000000, 0))
    chk("fma -(x*y)-z", fp_fma(0x3F800000, 0x40000000, 0x3F800000, F32, mx, negp=True, negc=True),
        (0xC0400000, 0))
    chk("fma denorm DE", fp_fma(0x00000001, 0x3F800000, 0, F32, mx), (0x00000001, DE))
    chk("fma order 231", fma_elem(0xB8, 0, 0x3F800000, 0x40000000, 0x40400000, F32, mx), (0x40E00000, 0))
    chk("fma order 132", fma_elem(0x98, 0, 0x40000000, 0x3F800000, 0x40400000, F32, mx), (0x40E00000, 0))
    chk("fmaddsub even", fma_elem(0xA6, 0, 0x40000000, 0x40400000, 0x3F800000, F32, mx), (0x40A00000, 0))
    chk("fmaddsub odd", fma_elem(0xA6, 1, 0x40000000, 0x40400000, 0x3F800000, F32, mx), (0x40E00000, 0))
    # compares
    chk("cmp lt_os qnan", fp_cmp(1, 0x7FC00000, 0, F32, mx), (False, IE))
    chk("cmp lt_oq qnan", fp_cmp(0x11, 0x7FC00000, 0, F32, mx), (False, 0))
    chk("cmp neq_uq qnan", fp_cmp(4, 0x7FC00000, 0, F32, mx), (True, 0))
    chk("cmp eq_oq snan", fp_cmp(0, 0x7FA00000, 0, F32, mx), (False, IE))
    chk("cmp -0 = +0", fp_cmp(0, 0x80000000, 0, F32, mx), (True, 0))
    chk("cmp gt denorm", fp_cmp(0xE, 0x00000001, 0, F32, mx), (True, DE))
    chk("cmp gt denorm daz", fp_cmp(0xE, 0x00000001, 0, F32, mx | DAZ), (False, 0))
    chk("comis lt", fp_comis(0, 0x3F800000, F32, mx, True), (CF, 0))
    chk("comis qnan", fp_comis(0x7FC00000, 0, F32, mx, True), (ZF | PF | CF, IE))
    chk("ucomis qnan", fp_comis(0x7FC00000, 0, F32, mx, False), (ZF | PF | CF, 0))
    chk("rflags", comis_rflags(ZF, 0x8D7), 0x42)
    return ok


# ---------------------------------------------------------------------------------------
# validation of the model against the host CPU through the legacy SSE / VEX equivalents
# (the i5-13600K has SSE scalar, AVX VCMPPS/PD/SS/SD with 32 predicates and VEX FMA3 but no
# AVX-512): emu-alltest *hardware* cases (self-generated snippets); --hwcmp compares the host's
# results ("hw:" lines) with the model, and counts the cases where Unicorn differs from the host
# ---------------------------------------------------------------------------------------
HW_MXCSR = [0x1F80, 0x3F80, 0x5F80, 0x7F80, 0x1FC0, 0x9F80, 0x9FC0, 0x1F00]


def vex3(mmmmm, pp, w, opc, reg, rm, vvvv, l=0, imm=None):
    b = bytes([0xC4, 0xE0 | mmmmm, (w << 7) | ((~vvvv & 0xF) << 3) | (l << 2) | pp, opc,
               0xC0 | (reg << 3) | rm])
    if imm is not None:
        b += bytes([imm])
    return b


def hw_pool(f):
    sp = specials(f)
    vals = list(sp)
    if f is F32:
        vals += [0x00400001, 0x80000003, 0x00800001, 0x7F7FFFFE, 0x40000000, 0x3F7FFFFE,
                 0x00FFFFFF, 0x3F800001, 0xC0490FDB, 0x34000000, 0xB4000000]
    else:
        vals += [0x0008000000000001, 0x8000000000000003, 0x0010000000000001, 0x7FEFFFFFFFFFFFFE,
                 0x4000000000000000, 0x3FEFFFFFFFFFFFFE, 0x001FFFFFFFFFFFFF, 0x3FF0000000000001,
                 0x3CB0000000000000, 0xBCB0000000000000]
    return vals


def hw_value(f):
    return RNG.choice(hw_pool(f)) if RNG.getrandbits(2) else rnd_fp(f)


def finish(res, flags, mx):
    """(fault vector or -1, mxcsr after, unmasked) for a packed/scalar result"""
    unm = flags & ~(mx >> 7) & 0x3F
    if unm & (IE | DE | ZE):
        flags &= ~(OE | UE | PE)
    return (19 if unm else -1), mx | flags


def hwcheck_gen(out, expect_path):
    exp = []

    def add(text, regs, mx, fault, xmm0=None, rflags=None):
        ins = " ".join("%s=%s" % (k, hexs(v)) for k, v in regs)
        out.write("%s | %s mxcsr=0x%X\n" % (text, ins, mx))
        e = {"fault": fault, "mxcsr": mx_after[0], "xmm0": hexs(xmm0 if xmm0 is not None else regs[0][1])}
        if rflags is not None:
            e["rflags"] = rflags
        exp.append(e)

    mx_after = [0]
    # 1. legacy SSE scalar arithmetic: element 0, xmm0[127:esz] unchanged
    for opn, op in (("add", "add"), ("sub", "sub"), ("mul", "mul"), ("div", "div"), ("min", "min"),
                    ("max", "max"), ("sqrt", "sqrt")):
        for w in (0, 1):
            f, esz = (F64, 8) if w else (F32, 4)
            mn = "%s%s" % (opn, "sd" if w else "ss")
            for mx in HW_MXCSR:
                cnt = 40 if mx == 0x1F80 else 14
                for _ in range(cnt):
                    a, b = hw_value(f), hw_value(f)
                    if op in ("add", "sub", "mul", "div") and is_nan(a, f) and is_nan(b, f):
                        a = ONE[f]
                    x0 = pack([a], esz) + rnd_bytes(16 - esz)
                    x1 = pack([b], esz) + rnd_bytes(16 - esz)
                    r, fl = model_elem(op, a, b, f, mx)
                    fault, mxa = finish(r, fl, mx)
                    mx_after[0] = mxa
                    add("%s xmm0, xmm1" % mn, [("xmm0", x0), ("xmm1", x1)], mx, fault,
                        xmm0=x0 if fault >= 0 else pack([r], esz) + x0[esz:])
    # 2. (U)COMISS/SD: EFLAGS
    for nm, ordered in (("comis", True), ("ucomis", False)):
        for w in (0, 1):
            f, esz = (F64, 8) if w else (F32, 4)
            spl = specials(f)
            for mx in (0x1F80, 0x1FC0, 0x1F00, 0x1F00 | 0x40):
                pairs = [(a, b) for a in spl for b in spl] if mx == 0x1F80 else \
                    [(hw_value(f), hw_value(f)) for _ in range(48)]
                for a, b in pairs:
                    x0 = pack([a], esz) + rnd_bytes(16 - esz)
                    x1 = pack([b], esz) + rnd_bytes(16 - esz)
                    bits, fl = fp_comis(a, b, f, mx, ordered)
                    fault, mxa = finish(0, fl, mx)
                    mx_after[0] = mxa
                    add("%s%s xmm0, xmm1" % (nm, "d" if w else "s"), [("xmm0", x0), ("xmm1", x1)], mx, fault,
                        rflags=RFLAGS_DEFAULT if fault >= 0 else comis_rflags(bits))
    # 3. VEX VCMPPS/PD (xmm, all-ones / zero lanes) and VCMPSS/SD, imm8 0-31
    for scalar in (False, True):
        for w in (0, 1):
            f, esz = (F64, 8) if w else (F32, 4)
            n = 1 if scalar else 16 // esz
            pp = (3 if w else 2) if scalar else (1 if w else 0)
            spl = specials(f)
            pairs = [(a, b) for a in spl for b in spl]
            for pred in range(32):
                for mx in (0x1F80, 0x1FC0, 0x1F00):
                    if mx == 0x1F80:
                        groups = [pairs[i:i + n] for i in range(0, len(pairs), n)]
                        if scalar:
                            groups = groups[pred % 4::4]
                    else:
                        groups = [[(hw_value(f), hw_value(f)) for _ in range(n)] for _ in range(4)]
                    for g in groups:
                        av = [p[0] for p in g]
                        bv = [p[1] for p in g]
                        x0 = rnd_bytes(16)
                        x1 = pack(av, esz) + rnd_bytes(16 - n * esz)
                        x2 = pack(bv, esz) + rnd_bytes(16 - n * esz)
                        fl, lanes = 0, []
                        for a, b in zip(av, bv):
                            t, f1 = fp_cmp(pred, a, b, f, mx)
                            fl |= f1
                            lanes.append((1 << (8 * esz)) - 1 if t else 0)
                        fault, mxa = finish(0, fl, mx)
                        mx_after[0] = mxa
                        res = pack(lanes, esz) + (x1[esz:16] if scalar else b"")
                        add(".byte " + ", ".join("0x%02x" % c for c in vex3(1, pp, 0, 0xC2, 0, 2, 1, 0, pred)),
                            [("xmm0", x0), ("xmm1", x1), ("xmm2", x2)], mx, fault,
                            xmm0=x0 if fault >= 0 else res)
    # 4. VEX FMA3: xmm0 = DEST, xmm1 = SRC2 (vvvv), xmm2 = SRC3 (r/m)
    for scalar in (False, True):
        for opc in (FMA_SCALAR_OPC if scalar else FMA_PACKED_OPC):
            for w in (0, 1):
                f, esz = (F64, 8) if w else (F32, 4)
                n = 1 if scalar else 16 // esz
                for mx in HW_MXCSR:
                    cnt = 24 if mx == 0x1F80 else 5
                    for _ in range(cnt):
                        d = [hw_value(f) for _ in range(n)]
                        s2 = [hw_value(f) for _ in range(n)]
                        s3 = [hw_value(f) for _ in range(n)]
                        x0 = pack(d, esz) + rnd_bytes(16 - n * esz)
                        x1 = pack(s2, esz) + rnd_bytes(16 - n * esz)
                        x2 = pack(s3, esz) + rnd_bytes(16 - n * esz)
                        fl, rs = 0, []
                        for j in range(n):
                            r, f1 = fma_elem(opc, j, d[j], s2[j], s3[j], f, mx)
                            rs.append(r)
                            fl |= f1
                        fault, mxa = finish(0, fl, mx)
                        mx_after[0] = mxa
                        res = pack(rs, esz) + (x0[esz:16] if scalar else b"")
                        add(".byte " + ", ".join("0x%02x" % c for c in vex3(2, 1, w, opc, 0, 2, 1)),
                            [("xmm0", x0), ("xmm1", x1), ("xmm2", x2)], mx, fault,
                            xmm0=x0 if fault >= 0 else res)
    json.dump(exp, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    exp = json.load(open(expect_path))
    cur, bad, seen, uc_diff = None, 0, 0, 0
    inp = {}
    shown = 0
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] (SAME|DIFF) (.*)$", line)
        if m:
            cur = int(m.group(1))
            uc_diff += m.group(2) == "DIFF"
            body = m.group(3)
            inp = dict(x.split("=", 1) for x in body.split("|", 1)[1].split() if "=" in x)
            inp["text"] = body.split("|", 1)[0].strip()
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if m and cur is not None:
            fields = m.group(1)
            fault = -1
            fm = re.match(r"fault #(\d+)\s*(.*)$", fields)
            if fm:
                fault, fields = int(fm.group(1)), fm.group(2)
            kv = dict(x.split("=", 1) for x in fields.split() if "=" in x)
            e = exp[cur]
            got_x = kv.get("xmm0", inp["xmm0"]).upper()
            got_m = int(kv.get("mxcsr", inp["mxcsr"]), 16)
            seen += 1
            okc = got_x == e["xmm0"].upper() and got_m == e["mxcsr"] and fault == e["fault"]
            if "rflags" in e:
                got_f = int(kv.get("rflags", "0x202"), 16)
                okc = okc and got_f == e["rflags"]
            if not okc:
                bad += 1
                if shown < 25:
                    shown += 1
                    print("[%d] %s | %s: hw xmm0=%s mxcsr=%X fault=%d rflags=%s | model xmm0=%s mxcsr=%X fault=%d "
                          "rflags=%s" % (cur, inp["text"], " ".join("%s=%s" % (k, v) for k, v in inp.items()
                                                                     if k != "text"),
                                         got_x, got_m, fault, kv.get("rflags", "-"), e["xmm0"], e["mxcsr"],
                                         e["fault"], hex(e["rflags"]) if "rflags" in e else "-"))
            cur = None
    print("hwcheck: %d hardware cases compared, %d differ from the model; Unicorn differs from the host "
          "in %d" % (seen, bad, uc_diff))
    return bad == 0


def main():
    if "--hwgen" in sys.argv:
        hwcheck_gen(sys.stdout, sys.argv[sys.argv.index("--hwgen") + 1])
        return
    if "--hwcmp" in sys.argv:
        i = sys.argv.index("--hwcmp")
        sys.exit(0 if hwcheck_cmp(sys.argv[i + 1], sys.argv[i + 2]) else 1)
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_scalar_arith()
        gen_movs()
        gen_comis()
        gen_vcmp()
        gen_fma()
        gen_blend()
        out = sys.stdout
        out.write("# EVEX milestone M2 engine (ledger U190-U201): expected values from the independent SDM model\n")
        out.write("# Emulator/tools/isa/ref_evex_m2_engine.py --cases (regenerate, do not edit). The i5-13600K has\n")
        out.write("# no AVX-512: expected-value cases only, run with AVX-512 enabled:\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m2_engine.txt --avx512 --expect-only\n")
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
