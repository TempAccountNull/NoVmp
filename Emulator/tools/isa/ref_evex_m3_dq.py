#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m3_dq.py -- independent reference model (Python 3 stdlib only) of the AVX512DQ
instructions of milestone M3 (ledger U290-U296) and generator of the expected-value case
files Emulator/data/cases_evex_m3_dq.txt and cases_evex_m3_dq_post.txt.

Written from the Intel SDM text only (Vol2A 2.7 EVEX encoding, Tables 2-36..2-43; the
Vol2B/2C instruction pages' "Operation" pseudocode; Vol1 4.8/4.9/11.5), not from the C code.
The generic EVEX machinery (encoder, case lines, IEEE rounding with exact rationals) is
reused from ref_evex_m1.py.

  VPMULLQ (EVEX.66.0F38.W1 40)                                       U290
  VANDPS/PD, VANDNPS/PD, VORPS/PD, VXORPS/PD (0F 54..57, NP W0 / 66 W1) U291
  VFPCLASSPS/PD (66 0F3A 66), VFPCLASSSS/SD (67) -> k                 U292
  VRANGEPS/PD (66 0F3A 50), VRANGESS/SD (51) {sae}, imm8              U293 / U295
  VREDUCEPS/PD (66 0F3A 56), VREDUCESS/SD (57) {sae}, imm8            U294 / U296

Second file (--cases-post): forms whose cases need code of other milestone-M2 worktrees,
to be run after the merge: masked scalar VRANGESS/SD, VREDUCESS/SD with a destination other
than SRC1 (engine "DEST[127:esz] := SRC1" under masking, m2_engine U192), the QQ
conversions VCVT[U]QQ2PS/PD, VCVT[T]PS/PD2[U]QQ ({er}/{sae}, integer indefinite; m2_cvt
U230/U233) and VINSERTF/I32X8/64X2, VEXTRACTF/I32X8/64X2, VBROADCASTF/I32X2/32X8/64X2
(Tuple2/Tuple8; m2_perm U210/U215).

Model notes (SDM pseudocode taken literally):
  VFPCLASS  CheckFPClass: with MXCSR.DAZ a denormal is classified as a zero. No flags.
  VRANGE    RangeSP/DP: SNaN (SRC1 first) -> quietened, IE, sign control ignored; denormal:
            DAZ -> signed zero, else DE unless the other operand is a QNaN; QNaN SRC2 -> SRC1,
            QNaN SRC1 -> SRC2; Table 5-22 (opposite zeros), Table 5-23 (equal magnitude);
            "SRC1 <= SRC2 ? ..."; imm8[3:2] sign select (also for a QNaN result).
  VREDUCE   SRC - 2^-M * ROUND(2^M * SRC) exactly, the subtraction rounded under the same
            RC (imm8[1:0], or MXCSR.RC when imm8[2] = 1); PE from the inexact ROUND unless
            SPE (imm8[3]); NaN -> quietened (IE for SNaN); +-INF -> +0; zero result +0
            (-0 when rounding down), Table 5-27. No DAZ/FTZ/DE/UE step in the pseudocode.
  CVT       int64 -> FP rounded (PE); FP -> int64/uint64 rounded (or truncated), NaN/INF/out
            of range -> IE and 8000000000000000h (signed) / FFFFFFFFFFFFFFFFh (unsigned);
            DAZ zeroes a denormal source (checked against the i5-13600K with the SSE forms).

Usage:
  python ref_evex_m3_dq.py --selftest          hand-derived checks, exit 0 on pass
  python ref_evex_m3_dq.py --cases             Emulator/data/cases_evex_m3_dq.txt (stdout)
  python ref_evex_m3_dq.py --cases-post        Emulator/data/cases_evex_m3_dq_post.txt
  python ref_evex_m3_dq.py --hwgen EXPECT.json hardware cases (stdout) for emu-alltest --strict
  python ref_evex_m3_dq.py --hwcmp LOG EXPECT.json   compare the hw: lines with the model
"""

import os
import random
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_evex_m1 as m1  # noqa: E402
from ref_evex_m1 import (evex, Mem, Case, elems, pack, hexs, F32, F64, classify, quiet,  # noqa: E402
                         encode_value, IE, DE, PE, DAZ, FTZ, MXCSR_DEFAULT, RSI, MEM_RSI,
                         VL_LL, SPECIAL32, SPECIAL64)

RNG = random.Random(0xD05EED29)
M64 = (1 << 64) - 1


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


def rnd_fp(f):
    """random finite value, mostly normal, spread exponents (never NaN)"""
    sign = RNG.getrandbits(1)
    if f is F32:
        e = RNG.choice([RNG.randint(110, 150), RNG.randint(1, 254), RNG.randint(120, 135)])
        return (sign << 31) | (e << 23) | RNG.getrandbits(23)
    e = RNG.choice([RNG.randint(1000, 1060), RNG.randint(1, 2046), RNG.randint(1015, 1035)])
    return (sign << 63) | (e << 52) | RNG.getrandbits(52)


# ---------------------------------------------------------------------------------------
# element models
# ---------------------------------------------------------------------------------------
def m_logic(op, a, b, esz):
    m = (1 << (8 * esz)) - 1
    return {"and": a & b, "andn": (~a & m) & b, "or": a | b, "xor": a ^ b,
            "mullq": (a * b) & M64}[op]


def m_fpclass(x, f, imm, mxcsr):
    """CheckFPClassSP/DP: OR of the imm8-selected categories"""
    c, s, _ = classify(x, f)
    if c == "denorm" and mxcsr & DAZ:
        c = "zero"
    cat = 0
    if c == "qnan":
        cat |= 0x01
    if c == "zero":
        cat |= 0x04 if s else 0x02
    if c == "inf":
        cat |= 0x10 if s else 0x08
    if c == "denorm":
        cat |= 0x20
    if s and c in ("normal", "denorm"):
        cat |= 0x40
    if c == "snan":
        cat |= 0x80
    return 1 if cat & imm else 0


def _key(c, s, v):
    """total order of the non-NaN values"""
    if c == "inf":
        return (-1, 0) if s else (1, 0)
    return (0, v)


def _akey(c, v):
    if c == "inf":
        return (1, 0)
    return (0, abs(v))


def m_range(a, b, f, imm, mxcsr):
    """RangeSP/RangeDP (SDM Vol2C VRANGEPD/PS/SD/SS); returns (bits, flags)"""
    sb_ = 1 << (f.bits - 1)
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    if ca == "snan":
        return quiet(a, f), IE
    if cb == "snan":
        return quiet(b, f), IE
    flags = 0
    sign1 = sa
    if ca == "denorm":
        if mxcsr & DAZ:
            a, ca, va = a & sb_, "zero", Fraction(0)
        elif cb != "qnan":
            flags |= DE
    if cb == "denorm":
        if mxcsr & DAZ:
            b, cb, vb = b & sb_, "zero", Fraction(0)
        elif ca != "qnan":
            flags |= DE
    op = imm & 3
    if cb == "qnan":
        t = a
    elif ca == "qnan":
        t = b
    elif ca == "zero" and cb == "zero" and sa != sb:
        t = sb_ if op in (0, 2) else 0                    # Table 5-22
    elif op > 1 and sa != sb and _akey(ca, va) == _akey(cb, vb):
        neg, pos = (a, b) if sa else (b, a)               # Table 5-23
        t = neg if op == 2 else pos
    elif op == 0:
        t = a if _key(ca, sa, va) <= _key(cb, sb, vb) else b
    elif op == 1:
        t = b if _key(ca, sa, va) <= _key(cb, sb, vb) else a
    elif op == 2:
        t = a if _akey(ca, va) <= _akey(cb, vb) else b
    else:
        t = b if _akey(ca, va) <= _akey(cb, vb) else a
    sc = (imm >> 2) & 3
    if sc == 0:
        t = (t & (sb_ - 1)) | (sb_ if sign1 else 0)
    elif sc == 2:
        t &= sb_ - 1
    elif sc == 3:
        t |= sb_
    return t, flags


def _round_int(t, rc):
    """round the rational t to an integer: 0 RNE, 1 RD, 2 RU, 3 RZ"""
    fl = t.numerator // t.denominator
    fr = t - fl
    if fr == 0:
        return fl, False
    if rc == 0:
        if fr > Fraction(1, 2) or (fr == Fraction(1, 2) and fl % 2):
            fl += 1
    elif rc == 2:
        fl += 1
    elif rc == 3:
        if t < 0:
            fl += 1
    return fl, True


def m_reduce(x, f, imm, mxcsr):
    """ReduceArgumentSP/DP (SDM Vol2C VREDUCEPD .., Table 5-27); returns (bits, flags)"""
    sb_ = 1 << (f.bits - 1)
    c, s, v = classify(x, f)
    rc = ((mxcsr >> 13) & 3) if imm & 4 else imm & 3
    M = imm >> 4
    if c in ("snan", "qnan"):
        return quiet(x, f), (IE if c == "snan" else 0)
    if c == "inf":
        return 0, 0
    zero = sb_ if rc == 1 else 0
    if c == "zero":
        return zero, 0
    r, inexact = _round_int(v * 2 ** M, rc)
    flags = PE if inexact and not imm & 8 else 0
    d = v - Fraction(r) / 2 ** M
    if d == 0:
        return zero, flags
    bits, _ = encode_value(d, f, rc, False)
    return bits, flags


def m_cvt_i2f(v, f, rc):
    """64-bit integer (Python int, signed or unsigned value) -> FP; (bits, flags)"""
    if v == 0:
        return 0, 0
    bits, fl = encode_value(Fraction(v), f, rc, False)
    return bits, fl & PE


def m_cvt_f2i(x, f, rc, signed, mxcsr):
    """FP -> 64-bit integer, rc = rounding (3 for the truncating forms); (value, flags)"""
    indef = (1 << 63) if signed else M64
    c, s, v = classify(x, f)
    if c in ("snan", "qnan", "inf"):
        return indef, IE
    if c == "denorm" and mxcsr & DAZ:
        v = Fraction(0)
    if c == "zero" or v == 0:
        return 0, 0
    r, inexact = _round_int(v, rc)
    lo, hi = (-(1 << 63), (1 << 63) - 1) if signed else (0, M64)
    if r < lo or r > hi:
        return indef, IE
    return r & M64, (PE if inexact else 0)


# ---------------------------------------------------------------------------------------
# cases
# ---------------------------------------------------------------------------------------
cases, post = [], []


def comment(text, to=None):
    (cases if to is None else to).append("# " + text)


def mem_case(c, off, data):
    c.mem[MEM_RSI + off] = data


def finish_fp(c, mxcsr, flags, unm):
    """#XM (destination unchanged) or the MXCSR flags of a completed instruction"""
    if unm:
        c.fault = "#XM"
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))


def xm_filter(flags, mxcsr, sae):
    if sae:
        return 0, 0
    unm = flags & ~(mxcsr >> 7) & 0x3F
    if unm & (IE | m1.DE | m1.ZE):
        flags &= ~(m1.OE | m1.UE | PE)
    return flags, unm


# form descriptions: layout rvm (V,H,W), rvmi (V,H,W,ib), rmi (V,W,ib), krmi (K,W,ib),
# scalar srvmi (V,H,W,ib) and skrmi (K,W,ib)
FORMS = [
    dict(name="VPMULLQ", mmm=2, pp=1, opc=0x40, w=1, lay="rvm", op="mullq"),
    dict(name="VANDPS", mmm=1, pp=0, opc=0x54, w=0, lay="rvm", op="and"),
    dict(name="VANDPD", mmm=1, pp=1, opc=0x54, w=1, lay="rvm", op="and"),
    dict(name="VANDNPS", mmm=1, pp=0, opc=0x55, w=0, lay="rvm", op="andn"),
    dict(name="VANDNPD", mmm=1, pp=1, opc=0x55, w=1, lay="rvm", op="andn"),
    dict(name="VORPS", mmm=1, pp=0, opc=0x56, w=0, lay="rvm", op="or"),
    dict(name="VORPD", mmm=1, pp=1, opc=0x56, w=1, lay="rvm", op="or"),
    dict(name="VXORPS", mmm=1, pp=0, opc=0x57, w=0, lay="rvm", op="xor"),
    dict(name="VXORPD", mmm=1, pp=1, opc=0x57, w=1, lay="rvm", op="xor"),
    dict(name="VFPCLASSPS", mmm=3, pp=1, opc=0x66, w=0, lay="krmi", op="fpclass"),
    dict(name="VFPCLASSPD", mmm=3, pp=1, opc=0x66, w=1, lay="krmi", op="fpclass"),
    dict(name="VFPCLASSSS", mmm=3, pp=1, opc=0x67, w=0, lay="skrmi", op="fpclass"),
    dict(name="VFPCLASSSD", mmm=3, pp=1, opc=0x67, w=1, lay="skrmi", op="fpclass"),
    dict(name="VRANGEPS", mmm=3, pp=1, opc=0x50, w=0, lay="rvmi", op="range", fp=True),
    dict(name="VRANGEPD", mmm=3, pp=1, opc=0x50, w=1, lay="rvmi", op="range", fp=True),
    dict(name="VRANGESS", mmm=3, pp=1, opc=0x51, w=0, lay="srvmi", op="range", fp=True),
    dict(name="VRANGESD", mmm=3, pp=1, opc=0x51, w=1, lay="srvmi", op="range", fp=True),
    dict(name="VREDUCEPS", mmm=3, pp=1, opc=0x56, w=0, lay="rmi", op="reduce", fp=True),
    dict(name="VREDUCEPD", mmm=3, pp=1, opc=0x56, w=1, lay="rmi", op="reduce", fp=True),
    dict(name="VREDUCESS", mmm=3, pp=1, opc=0x57, w=0, lay="srvmi", op="reduce", fp=True),
    dict(name="VREDUCESD", mmm=3, pp=1, opc=0x57, w=1, lay="srvmi", op="reduce", fp=True),
]


def elem_op(sp, a, b, f, imm, mxcsr):
    """(result, flags) of one element; a = SRC1 (vvvv), b = r/m operand"""
    op = sp["op"]
    if op == "fpclass":
        return m_fpclass(b, f, imm, mxcsr), 0
    if op == "range":
        return m_range(a, b, f, imm, mxcsr)
    if op == "reduce":
        return m_reduce(b, f, imm, mxcsr)
    return m_logic(op, a, b, 8 if sp["w"] else 4), 0


def rnd_elem(sp, esz):
    if sp["op"] in ("fpclass", "range", "reduce"):
        f = F64 if esz == 8 else F32
        r = RNG.random()
        if r < 0.15:
            return RNG.choice(SPECIAL64 if esz == 8 else SPECIAL32)
        return rnd_fp(f)
    return RNG.getrandbits(8 * esz)


def gen(sp, vl, variant, dst=1, s1=2, s2=3, kreg=0, kval=None, z=0, imm=None, avals=None,
        bvals=None, mxcsr=MXCSR_DEFAULT, sae=False, ll=None, memoff=0x40, disp32=False):
    """one case of form sp; returns a Case"""
    w = sp["w"]
    esz = 8 if w else 4
    f = F64 if w else F32
    lay = sp["lay"]
    scalar = lay.startswith("s")
    kd = lay in ("krmi", "skrmi")
    if kd:
        dst &= 7                    # k1..k7 / k0 destination
    if lay == "rvm":
        imm = None
    n = 1 if scalar else vl // esz
    c = Case("%s %s%s" % (sp["name"], "" if scalar else "VL%d " % (vl * 8), variant))
    if imm is None and lay not in ("rvm",):
        imm = RNG.getrandbits(8)
    mem = variant.startswith("mem") or variant.startswith("bcst")
    bcst = variant.startswith("bcst")
    regs = {}
    uses_s1 = lay in ("rvm", "rvmi", "srvmi")
    if avals is None:
        avals = [rnd_elem(sp, esz) for _ in range(n)]
    if bvals is None:
        bvals = [rnd_elem(sp, esz) for _ in range(1 if bcst else n)]
    if uses_s1:
        regs[s1] = pack(avals, esz) + rnd_bytes((16 if scalar else 64) - n * esz)
        if scalar:
            regs[s1] += rnd_bytes(48)
    if not mem:
        if s2 in regs:
            bvals = elems(regs[s2][:n * esz], esz)
        else:
            regs[s2] = pack(bvals, esz) + rnd_bytes(64 - n * esz)
    if not kd and dst not in regs:
        regs[dst] = rnd_bytes(64)
    for r, img in regs.items():
        c.set_zmm(r, img)
    if uses_s1:
        avals = elems(regs[s1][:n * esz], esz)
    if mem:
        mem_case(c, memoff, pack(bvals, esz))
        rm = Mem(RSI, memoff, disp32=disp32)
        nn = esz if (bcst or scalar) else vl
        if bcst:
            bvals = bvals * n
    else:
        rm, nn = s2, 1
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    kmask = kval if kreg else None
    res, flags = [], 0
    for j in range(n):
        if kmask is not None and not (kmask >> j) & 1:
            res.append(None)
            continue
        r, fl = elem_op(sp, avals[j] if uses_s1 else 0, bvals[j], f, imm, mxcsr)
        res.append(r)
        flags |= fl
    if kd:
        kr = 0
        for j in range(n):
            if res[j]:
                kr |= 1 << j
        c.k.setdefault(dst, RNG.getrandbits(64))
        c.exp.append("k%d=0x%X" % (dst, kr))
    else:
        flags, unm = xm_filter(flags, mxcsr, sae) if sp.get("fp") else (0, 0)
        if not unm:
            old = regs[dst]
            if scalar:
                up = regs[s1][esz:16]
                e0 = res[0] if res[0] is not None else (0 if z else elems(old[:esz], esz)[0])
                out = pack([e0], esz) + up + bytes(48)
            else:
                o = elems(old[:vl], esz)
                out = pack([(0 if z else o[j]) if res[j] is None else res[j] for j in range(n)],
                           esz) + bytes(64 - vl)
            c.exp.append("zmm%d=%s" % (dst, hexs(out)))
        finish_fp(c, mxcsr, flags, unm)
    if ll is None:
        ll = VL_LL[vl] if not scalar else RNG.randint(0, 3)
    b = 1 if (bcst or sae) else 0
    if lay in ("krmi", "skrmi", "rmi"):
        c.code = evex(sp["mmm"], sp["pp"], w, sp["opc"], dst, rm, ll=ll, b=b, z=z, aaa=kreg,
                      imm=imm, n=nn)
    else:
        c.code = evex(sp["mmm"], sp["pp"], w, sp["opc"], dst, rm, vvvv=s1, ll=ll, b=b, z=z,
                      aaa=kreg, imm=imm, n=nn)
    return c


def emit(c, to=None):
    (cases if to is None else to).append(c)


def fp_specials(esz):
    sp = list(SPECIAL64 if esz == 8 else SPECIAL32)
    if esz == 4:
        sp += [0x80000001, 0x807FFFFF, 0x3F000000, 0xBF000000, 0x3FC00000, 0x40200000,
               0x4B000000, 0xCB7FFFFF, 0x3E800000, 0x3F400000]
    else:
        sp += [0x8000000000000001, 0x800FFFFFFFFFFFFF, 0x3FE0000000000000, 0xBFE0000000000000,
               0x3FF8000000000000, 0x4004000000000000, 0x4330000000000000, 0xC33FFFFFFFFFFFFF,
               0x3FD0000000000000, 0x3FE8000000000000]
    return sp


def no_both_nan(av, bv, f):
    """lanes with two NaN sources are left out (U96, the SSE two-NaN rule, is elsewhere)"""
    one = 0x3FF0000000000000 if f is F64 else 0x3F800000
    out = []
    for x, y in zip(av, bv):
        if classify(x, f)[0] in ("snan", "qnan") and classify(y, f)[0] in ("snan", "qnan"):
            x = one
        out.append(x)
    return out


def gen_packed(sp):
    w, esz = sp["w"], 8 if sp["w"] else 4
    f = F64 if w else F32
    lay = sp["lay"]
    kd = lay == "krmi"
    fp = sp.get("fp", False)
    comment("%s (EVEX.%s.%s.W%d %02X%s)" % (sp["name"], ["NP", "66", "F3", "F2"][sp["pp"]],
            ["", "0F", "0F38", "0F3A"][sp["mmm"]], w, sp["opc"], "" if lay == "rvm" else " ib"))
    for vl in (16, 32, 64):
        emit(gen(sp, vl, "nomask", dst=1, s1=2, s2=3))
        emit(gen(sp, vl, "merge zmm16+", dst=(5 if kd else 20), s1=22, s2=17, kreg=3,
                 kval=RNG.getrandbits(64)))
        if not kd:
            emit(gen(sp, vl, "zero", dst=4, s1=5, s2=26, kreg=6, kval=RNG.getrandbits(64), z=1))
        emit(gen(sp, vl, "mem", dst=6, s1=8))
        emit(gen(sp, vl, "bcst merge", dst=7, s1=9, kreg=2, kval=RNG.getrandbits(64)))
        emit(gen(sp, vl, "bcst", dst=7, s1=9, memoff=-4 * esz))
    emit(gen(sp, 64, "dst=src2", dst=11, s1=12, s2=11, kreg=1, kval=0x5A5A))
    if lay in ("rvm", "rvmi"):
        emit(gen(sp, 64, "dst=src1", dst=13, s1=13, s2=14, kreg=4, kval=0x3C3C, z=1))
        emit(gen(sp, 64, "dst=src1=src2", dst=15, s1=15, s2=15))
    # every special value against every other (two-NaN lanes left out)
    spv = fp_specials(esz)
    if sp["op"] in ("fpclass", "range", "reduce"):
        n = 64 // esz
        pairs = [(x, y) for x in spv for y in spv]
        imms = {"fpclass": [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF, 0x81, 0x66],
                "range": list(range(16)) + [0xF5],
                "reduce": [0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B, 0x04, 0x0C, 0x10, 0x33,
                           0x4A, 0x88, 0xF0, 0xFB, 0x7E]}[sp["op"]]
        for ci in range(len(pairs) // n):
            imm = imms[ci % len(imms)]
            chunk = pairs[ci * n:(ci + 1) * n]
            av = [x for x, _ in chunk]
            bv = [y for _, y in chunk]
            if sp["op"] == "range":
                av = no_both_nan(av, bv, f)
            emit(gen(sp, 64, "specials imm=%02X" % imm, dst=24, s1=25, s2=27, imm=imm,
                     avals=av, bvals=bv))
        for imm in imms:
            # every special as the r/m operand with this imm8
            for half in (0, 1):
                bv = ((spv * 2)[half * n:(half + 1) * n])
                av = [rnd_fp(f) for _ in range(n)]
                emit(gen(sp, 64, "specials rm imm=%02X" % imm, dst=24, s1=25, s2=27, imm=imm,
                         avals=av, bvals=bv))
        # MXCSR: rounding modes, DAZ, FTZ
        for mx in (0x3F80, 0x5F80, 0x7F80, MXCSR_DEFAULT | DAZ, MXCSR_DEFAULT | FTZ,
                   MXCSR_DEFAULT | DAZ | FTZ):
            if sp["op"] == "reduce" and mx & DAZ:
                continue        # the SDM pseudocode has no DAZ step: not asserted here
            bv = [rnd_fp(f) for _ in range(n)]
            av = [rnd_fp(f) for _ in range(n)]
            dn = [0x00000001, 0x80400000, 0x007FFFFF] if f is F32 else \
                [0x1, 0x8008000000000000, 0x000FFFFFFFFFFFFF]
            bv[0], bv[1], av[2] = dn[0], dn[1], dn[2]
            for imm in ({"fpclass": [0x20, 0x66], "range": [0x00, 0x05, 0x0A, 0x0F],
                         "reduce": [0x04, 0x14, 0x0C, 0xF4, 0x34]}[sp["op"]]):
                emit(gen(sp, 64, "mxcsr=%X imm=%02X" % (mx, imm), dst=28, s1=29, s2=30, imm=imm,
                         avals=av, bvals=bv, mxcsr=mx))
        if sp["op"] == "reduce":
            # Table 5-27 corners: |SRC| < 2^-M-1 / 2^-M under every rounding, M = 0..15
            for M in (0, 1, 4, 15):
                for rcv in range(4):
                    imm = (M << 4) | rcv
                    bv = []
                    for j in range(n):
                        e = RNG.randint(-M - 30, -M - 1) if j % 2 else RNG.randint(-M - 3, 30)
                        v = Fraction(RNG.getrandbits(20) | (1 << 20), 1 << 20) * Fraction(2) ** e
                        if j % 3 == 0:
                            v = -v
                        bv.append(encode_value(v, f, 0, False)[0])
                    emit(gen(sp, 64, "M=%d rc=%d" % (M, rcv), dst=10, s2=11, imm=imm, bvals=bv))
                    emit(gen(sp, 64, "M=%d rc=%d SPE" % (M, rcv), dst=10, s2=11, imm=imm | 8,
                             bvals=bv))
    if fp:
        n = 64 // esz
        spl = SPECIAL64 if w else SPECIAL32
        av = [rnd_fp(f) for _ in range(n)]
        bv = [rnd_fp(f) for _ in range(n)]
        bv[1] = spl[7]                          # SNaN
        bv[3] = spl[8]                          # denormal
        imm = 0x05 if sp["op"] == "range" else 0x24
        # {sae}: flags suppressed, no #XM; L'L ignored (VL 512)
        for ll in (0, 1, 2, 3):
            emit(gen(sp, 64, "{sae} L'L=%d" % ll, dst=31, s1=2, s2=3, avals=av, bvals=bv,
                     imm=imm, sae=True, mxcsr=0x1F00, ll=ll))
        emit(gen(sp, 64, "{sae} merge", dst=31, s1=2, s2=3, avals=av, bvals=bv, imm=imm,
                 sae=True, mxcsr=0x1F80, kreg=5, kval=RNG.getrandbits(16)))
        # #XM: unmasked IE in an active lane, destination unchanged
        emit(gen(sp, 64, "SNaN active IM=0 -> #XM", dst=4, s1=5, s2=6, avals=av, bvals=bv,
                 imm=imm, mxcsr=0x1F80 & ~0x80))
        emit(gen(sp, 64, "SNaN masked off IM=0", dst=4, s1=5, s2=6, avals=av, bvals=bv,
                 imm=imm, mxcsr=0x1F80 & ~0x80, kreg=1, kval=0xFFFF & ~2))
        if sp["op"] == "range":
            bv[1] = rnd_fp(f)
            emit(gen(sp, 64, "denormal DM=0 -> #XM", dst=4, s1=5, s2=6, avals=av, bvals=bv,
                     imm=imm, mxcsr=0x1F80 & ~0x100))
            emit(gen(sp, 64, "denormal DM=0 DAZ", dst=4, s1=5, s2=6, avals=av, bvals=bv,
                     imm=imm, mxcsr=(0x1F80 & ~0x100) | DAZ))
        else:
            bv[1] = rnd_fp(f)
            emit(gen(sp, 64, "inexact PM=0 -> #XM", dst=4, s1=5, s2=6, avals=av, bvals=bv,
                     imm=0x14, mxcsr=0x1F80 & ~0x1000))
            emit(gen(sp, 64, "inexact PM=0 SPE", dst=4, s1=5, s2=6, avals=av, bvals=bv,
                     imm=0x1C, mxcsr=0x1F80 & ~0x1000))
            emit(gen(sp, 64, "inexact PM=0 {sae}", dst=4, s1=5, s2=6, avals=av, bvals=bv,
                     imm=0x14, mxcsr=0x1F80 & ~0x1000, sae=True))
    # #UD
    ud = []
    if lay == "rvm" or (not fp):
        ud.append(("EVEX.b on a register form", dict(b=1)))
    if lay in ("rmi", "krmi"):
        ud.append(("vvvv != 1111b", dict(vvvv=6)))
        ud.append(("V' = 0", dict(p2_vp=0)))
    if kd:
        ud.append(("{z} with a k destination", dict(z=1, aaa=1)))
    ud.append(("L'L = 11b", dict(ll=3)))
    if lay == "rvm" and sp["op"] != "mullq":
        ud.append(("EVEX.W%d" % (1 - w), dict(w=1 - w)))
    # a wrong prefix: F3 for map 1 and for 0F3A 56/66 W0 (whose NP W0 forms are the
    # AVX512-FP16 VREDUCEPH / VFPCLASSPH, enabled by emu-alltest --avx512), NP otherwise
    if sp["mmm"] == 1 or (sp["opc"] in (0x56, 0x66) and w == 0):
        ud.append(("prefix F3", dict(pp=2)))
    else:
        ud.append(("prefix NP", dict(pp=0)))
    for title, kw in ud:
        c = Case("%s %s #UD" % (sp["name"], title))
        a = dict(mmm=sp["mmm"], pp=sp["pp"], w=w, opc=sp["opc"], reg=1, rm=3, ll=2,
                 imm=None if lay == "rvm" else 0x11)
        if lay not in ("rmi", "krmi"):
            a["vvvv"] = 2
        a.update(kw)
        c.code = evex(a.pop("mmm"), a.pop("pp"), a.pop("w"), a.pop("opc"), a.pop("reg"),
                      a.pop("rm"), **a)
        c.fault = "#UD"
        emit(c)


def gen_scalar(sp):
    w, esz = sp["w"], 8 if sp["w"] else 4
    f = F64 if w else F32
    kd = sp["lay"] == "skrmi"
    comment("%s (EVEX.LLIG.66.0F3A.W%d %02X ib)" % (sp["name"], w, sp["opc"]))
    spv = fp_specials(esz)
    for i in range(10):
        emit(gen(sp, 16, "nomask", dst=1, s1=2, s2=3))
        emit(gen(sp, 16, "mem", dst=6, s1=8, memoff=(-esz if i % 2 else 0x40)))
        if kd:
            emit(gen(sp, 16, "k merge", dst=5, s2=17, kreg=3, kval=RNG.getrandbits(64)))
        else:
            # merging / zeroing with DEST = SRC1: identical with and without U192
            emit(gen(sp, 16, "merge dst=src1", dst=20, s1=20, s2=17, kreg=3,
                     kval=RNG.getrandbits(64)))
            emit(gen(sp, 16, "zero dst=src1", dst=4, s1=4, s2=26, kreg=6,
                     kval=RNG.getrandbits(64), z=1))
            emit(gen(sp, 16, "mem merge dst=src1", dst=9, s1=9, kreg=2, kval=RNG.getrandbits(64)))
            # with DEST != SRC1 under a mask: needs U192 (post file)
            emit(gen(sp, 16, "merge dst!=src1", dst=21, s1=22, s2=23, kreg=3,
                     kval=RNG.getrandbits(64)), post)
            emit(gen(sp, 16, "zero dst!=src1", dst=24, s1=25, s2=23, kreg=7,
                     kval=RNG.getrandbits(64), z=1), post)
    imms = {"fpclass": [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF],
            "range": list(range(16)),
            "reduce": [0x00, 0x01, 0x02, 0x03, 0x0B, 0x04, 0x10, 0x33, 0xF0, 0x7E]}[sp["op"]]
    for imm in imms:
        for x in spv:
            y = RNG.choice(spv)
            if sp["op"] == "range" and classify(x, f)[0] in ("snan", "qnan") and \
                    classify(y, f)[0] in ("snan", "qnan"):
                y = rnd_fp(f)
            emit(gen(sp, 16, "special imm=%02X" % imm, dst=10, s1=11, s2=12, imm=imm, avals=[y],
                     bvals=[x]))
    for mx in (0x3F80, 0x5F80, 0x7F80, MXCSR_DEFAULT | DAZ):
        if sp["op"] == "reduce" and mx & DAZ:
            continue
        for x in ([1, (1 << (esz * 8 - 1)) | 3, 0x00400000 if esz == 4 else 0x0008000000000000]):
            emit(gen(sp, 16, "mxcsr=%X" % mx, dst=10, s1=11, s2=12,
                     imm=0x24 if sp["op"] == "reduce" else 0x26, avals=[rnd_fp(f)], bvals=[x],
                     mxcsr=mx))
    if not kd:
        spl = SPECIAL64 if w else SPECIAL32
        for ll in (0, 3):
            emit(gen(sp, 16, "{sae} L'L=%d SNaN IM=0" % ll, dst=13, s1=14, s2=15, imm=0x05,
                     bvals=[spl[7]], sae=True, mxcsr=0x1F00, ll=ll))
        emit(gen(sp, 16, "SNaN IM=0 -> #XM", dst=13, s1=14, s2=15, imm=0x05, bvals=[spl[7]],
                 mxcsr=0x1F00))
        emit(gen(sp, 16, "SNaN masked off IM=0", dst=13, s1=13, s2=15, imm=0x05,
                 bvals=[spl[7]], mxcsr=0x1F00, kreg=1, kval=0xFE))
    else:
        c = Case("%s {z} #UD" % sp["name"])
        c.code = evex(3, 1, w, sp["opc"], 1, 3, ll=0, z=1, aaa=1, imm=1)
        c.fault = "#UD"
        emit(c)
        c = Case("%s EVEX.b register #UD" % sp["name"])
        c.code = evex(3, 1, w, sp["opc"], 1, 3, ll=0, b=1, imm=1)
        c.fault = "#UD"
        emit(c)
        c = Case("%s vvvv != 1111b #UD" % sp["name"])
        c.code = evex(3, 1, w, sp["opc"], 1, 3, vvvv=4, ll=0, imm=1)
        c.fault = "#UD"
        emit(c)
    c = Case("%s EVEX.b with memory #UD" % sp["name"])
    c.code = evex(3, 1, w, sp["opc"], 1, Mem(RSI, 0), vvvv=None if kd else 2, ll=0, b=1, imm=1)
    c.fault = "#UD"
    emit(c)


def gen_misc():
    comment("--- fault suppression next to the unmapped page MEM+0x10000")
    base = 0x10000 - MEM_RSI - 32
    for name, sp in (("VRANGEPS", FORMS[13]), ("VREDUCEPD", FORMS[18]), ("VFPCLASSPS", FORMS[9]),
                     ("VPMULLQ", FORMS[0]), ("VXORPS", FORMS[7])):
        esz = 8 if sp["w"] else 4
        nlo = 32 // esz
        k = (1 << nlo) - 1
        kd = sp["lay"] == "krmi"
        c = gen(sp, 64, "mem fault suppressed", dst=2, s1=3, kreg=1, kval=k,
                z=0 if kd else 1, imm=0x05, memoff=base, disp32=True)
        c.mem = {MEM_RSI + base: c.mem[MEM_RSI + base][:32]}
        emit(c)
        c = gen(sp, 64, "mem not suppressed #PF", dst=2, s1=3, kreg=1, kval=k | (1 << nlo),
                z=0 if kd else 1, imm=0x05, memoff=base, disp32=True)
        c.mem = {MEM_RSI + base: c.mem[MEM_RSI + base][:32]}
        c.exp = []
        c.fault = "#PF"
        emit(c)
    comment("--- disp8*N: Full (VL or element with {1toN}), Tuple1 Scalar (element)")
    for sp in (FORMS[2], FORMS[0], FORMS[14], FORMS[17], FORMS[10]):
        esz = 8 if sp["w"] else 4
        for vl in (16, 32, 64):
            for bc in (0, 1):
                for d8 in (1, -1, 127, -127):
                    nn = esz if bc else vl
                    emit(gen(sp, vl, ("bcst" if bc else "mem") + " disp8=%d N=%d" % (d8, nn),
                             dst=10, s1=11, memoff=d8 * nn, imm=0x0A))
    for sp in (FORMS[15], FORMS[20], FORMS[12]):
        esz = 8 if sp["w"] else 4
        for d8 in (1, -1, 127, -127):
            emit(gen(sp, 16, "mem disp8=%d N=%d" % (d8, esz), dst=10, s1=11, memoff=d8 * esz,
                     imm=0x0A))


# ---------------------------------------------------------------------------------------
# post-merge file: QQ conversions (m2_cvt), Tuple2/Tuple8 chunk forms (m2_perm)
# ---------------------------------------------------------------------------------------
CVT_FORMS = [
    # name, pp, opc, W, src type, dst type, tuple, rc form
    ("VCVTQQ2PD", 2, 0xE6, 1, "i64", "f64", "Full", "er"),
    ("VCVTQQ2PS", 0, 0x5B, 1, "i64", "f32", "Full", "er"),
    ("VCVTUQQ2PD", 2, 0x7A, 1, "u64", "f64", "Full", "er"),
    ("VCVTUQQ2PS", 3, 0x7A, 1, "u64", "f32", "Full", "er"),
    ("VCVTPD2QQ", 1, 0x7B, 1, "f64", "i64", "Full", "er"),
    ("VCVTPD2UQQ", 1, 0x79, 1, "f64", "u64", "Full", "er"),
    ("VCVTPS2QQ", 1, 0x7B, 0, "f32", "i64", "Half", "er"),
    ("VCVTPS2UQQ", 1, 0x79, 0, "f32", "u64", "Half", "er"),
    ("VCVTTPD2QQ", 1, 0x7A, 1, "f64", "i64", "Full", "sae"),
    ("VCVTTPD2UQQ", 1, 0x78, 1, "f64", "u64", "Full", "sae"),
    ("VCVTTPS2QQ", 1, 0x7A, 0, "f32", "i64", "Half", "sae"),
    ("VCVTTPS2UQQ", 1, 0x78, 0, "f32", "u64", "Half", "sae"),
]
TSZ = {"i64": 8, "u64": 8, "f64": 8, "f32": 4}


def cvt_elem(name, st, dt, x, mxcsr, rc):
    if rc is None:
        rc = (mxcsr >> 13) & 3
    if st in ("i64", "u64"):
        v = x - (1 << 64) if (st == "i64" and x >> 63) else x
        return m_cvt_i2f(v, F64 if dt == "f64" else F32, rc)
    if name.startswith("VCVTT"):
        rc = 3
    return m_cvt_f2i(x, F64 if st == "f64" else F32, rc, dt == "i64", mxcsr)


def rnd_cvt_src(st):
    if st in ("i64", "u64"):
        return RNG.choice([RNG.getrandbits(64), RNG.getrandbits(RNG.randint(1, 63)),
                           (-RNG.getrandbits(RNG.randint(1, 63))) & M64, (1 << 63), M64, 0, 1,
                           (1 << 24) + 1, (1 << 53) + 1, (1 << 63) - 1, (1 << 64) - 1024])
    f = F64 if st == "f64" else F32
    r = RNG.random()
    if r < 0.15:
        return RNG.choice(SPECIAL64 if f is F64 else SPECIAL32)
    if r < 0.5:
        # values around the integer range limits and small fractions
        e = RNG.choice([62, 63, 64, -1, 0, 1, 10, 52, 23]) + (1023 if f is F64 else 127)
        sign = RNG.getrandbits(1)
        if f is F64:
            return (sign << 63) | (e << 52) | RNG.getrandbits(52)
        return (sign << 31) | (e << 23) | RNG.getrandbits(23)
    return rnd_fp(f)


def gen_cvt_case(form, vl, variant, dst=1, src=2, kreg=0, kval=None, z=0, mxcsr=MXCSR_DEFAULT,
                 rc=None, sae=False, svals=None, memoff=0x40):
    name, pp, opc, w, st, dt, tt, rcf = form
    ssz, dsz = TSZ[st], TSZ[dt]
    n = vl // max(ssz, dsz)                      # elements (the wider side fills VL)
    svl, dvl = n * ssz, n * dsz
    c = Case("%s VL%d %s" % (name, vl * 8, variant))
    mem = variant.startswith("mem") or variant.startswith("bcst")
    bcst = variant.startswith("bcst")
    if svals is None:
        svals = [rnd_cvt_src(st) for _ in range(1 if bcst else n)]
    if not mem:
        c.set_zmm(src, pack(svals, ssz) + rnd_bytes(64 - svl))
    if dst != src or mem:
        c.set_zmm(dst, rnd_bytes(64))
    old = c.zmm[dst]
    if mem:
        mem_case(c, memoff, pack(svals, ssz))
        if bcst:
            svals = svals * n
        rm = Mem(RSI, memoff)
        nn = ssz if bcst else (vl if tt == "Full" else vl // 2)
    else:
        rm, nn = src, 1
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    res, flags = [], 0
    for j in range(n):
        if kreg and not (kval >> j) & 1:
            res.append(None)
            continue
        r, fl = cvt_elem(name, st, dt, svals[j], mxcsr, rc)
        res.append(r)
        flags |= fl
    flags, unm = xm_filter(flags, mxcsr, sae or rc is not None)
    if not unm:
        o = elems(old[:dvl], dsz)
        out = pack([(0 if z else o[j]) if res[j] is None else res[j] for j in range(n)], dsz)
        c.exp.append("zmm%d=%s" % (dst, hexs(out + bytes(64 - dvl))))
    finish_fp(c, mxcsr, flags, unm)
    ll = rc if rc is not None else VL_LL[vl]
    c.code = evex(1, pp, w, opc, dst, rm, ll=ll, b=1 if (bcst or rc is not None or sae) else 0,
                  z=z, aaa=kreg, n=nn)
    return c


def gen_cvt():
    comment("=== QQ conversions (m2_cvt U230/U233; model: SDM VCVT*QQ* pages)", post)
    for form in CVT_FORMS:
        name, pp, opc, w, st, dt, tt, rcf = form
        comment("%s (EVEX.%s.0F.W%d %02X, %s tuple, {%s})" % (name, ["NP", "66", "F3", "F2"][pp],
                w, opc, tt, rcf), post)
        for vl in (16, 32, 64):
            for _ in range(2):
                emit(gen_cvt_case(form, vl, "nomask"), post)
            emit(gen_cvt_case(form, vl, "merge zmm16+", dst=17, src=30, kreg=3,
                              kval=RNG.getrandbits(64)), post)
            emit(gen_cvt_case(form, vl, "zero", dst=4, src=5, kreg=6, kval=RNG.getrandbits(64), z=1),
                 post)
            emit(gen_cvt_case(form, vl, "mem", dst=6), post)
            emit(gen_cvt_case(form, vl, "bcst merge", dst=7, kreg=2, kval=RNG.getrandbits(64)), post)
            emit(gen_cvt_case(form, vl, "dst=src", dst=8, src=8), post)
        for mx in (0x3F80, 0x5F80, 0x7F80, MXCSR_DEFAULT | DAZ):
            emit(gen_cvt_case(form, 64, "mxcsr=%X" % mx, dst=9, src=10, mxcsr=mx), post)
        if rcf == "er":
            for rcv in range(4):
                emit(gen_cvt_case(form, 64, "{er} rc=%d" % rcv, dst=11, src=12, rc=rcv,
                                  mxcsr=0x1F00), post)
        else:
            emit(gen_cvt_case(form, 64, "{sae}", dst=11, src=12, sae=True, mxcsr=0x1F00), post)
        if st.startswith("f"):
            f = F64 if st == "f64" else F32
            sv = [rnd_fp(f) for _ in range(64 // max(TSZ[st], TSZ[dt]))]
            sv[1] = 0x7FF0000000000000 if f is F64 else 0x7F800000
            emit(gen_cvt_case(form, 64, "INF IM=0 -> #XM", dst=13, src=14, svals=sv,
                              mxcsr=0x1F80 & ~0x80), post)
            emit(gen_cvt_case(form, 64, "INF masked off IM=0", dst=13, src=14, svals=sv,
                              mxcsr=0x1F80 & ~0x80, kreg=1, kval=0xFD), post)


CHUNK = [
    # name, map, opc, W, kind, chunk bytes, element bytes, VLs, tuple N
    ("VINSERTF64X2", 3, 0x18, 1, "ins", 16, 8, (32, 64)),
    ("VINSERTI64X2", 3, 0x38, 1, "ins", 16, 8, (32, 64)),
    ("VINSERTF32X8", 3, 0x1A, 0, "ins", 32, 4, (64,)),
    ("VINSERTI32X8", 3, 0x3A, 0, "ins", 32, 4, (64,)),
    ("VEXTRACTF64X2", 3, 0x19, 1, "ext", 16, 8, (32, 64)),
    ("VEXTRACTI64X2", 3, 0x39, 1, "ext", 16, 8, (32, 64)),
    ("VEXTRACTF32X8", 3, 0x1B, 0, "ext", 32, 4, (64,)),
    ("VEXTRACTI32X8", 3, 0x3B, 0, "ext", 32, 4, (64,)),
    ("VBROADCASTF32X2", 2, 0x19, 0, "bc", 8, 4, (32, 64)),
    ("VBROADCASTI32X2", 2, 0x59, 0, "bc", 8, 4, (16, 32, 64)),
    ("VBROADCASTF64X2", 2, 0x1A, 1, "bcm", 16, 8, (32, 64)),
    ("VBROADCASTI64X2", 2, 0x5A, 1, "bcm", 16, 8, (32, 64)),
    ("VBROADCASTF32X8", 2, 0x1B, 0, "bcm", 32, 4, (64,)),
    ("VBROADCASTI32X8", 2, 0x5B, 0, "bcm", 32, 4, (64,)),
]


def gen_chunk():
    comment("=== Tuple2/Tuple8 chunk forms (m2_perm U210/U215; SDM VINSERT/VEXTRACT/VBROADCAST)",
            post)
    for name, mmm, opc, w, kind, cb, esz, vls in CHUNK:
        comment("%s (EVEX.66.%s.W%d %02X)" % (name, ["", "0F", "0F38", "0F3A"][mmm], w, opc), post)
        for vl in vls:
            nlanes = vl // cb
            for kreg, z in ((0, 0), (3, 0), (5, 1)):
                for mem in (False, True):
                    if kind == "bcm" and not mem:
                        continue
                    if kind == "ext" and mem and z:
                        continue
                    kval = RNG.getrandbits(64) if kreg else None
                    imm = RNG.getrandbits(8)
                    c = Case("%s VL%d %s%s%s" % (name, vl * 8, "mem " if mem else "",
                                                ("k%d" % kreg) if kreg else "nomask", " z" if z else ""))
                    if kreg:
                        c.k[kreg] = kval
                    dst, s1, s2 = 1, 2, 3
                    if kind == "ins":
                        c.set_zmm(dst, rnd_bytes(64)); c.set_zmm(s1, rnd_bytes(64))
                        chunk = rnd_bytes(cb)
                        if mem:
                            mem_case(c, 2 * cb, chunk)
                            rm = Mem(RSI, 2 * cb)
                        else:
                            c.set_zmm(s2, chunk + rnd_bytes(64 - cb))
                            rm = s2
                        lane = imm % nlanes
                        res = bytearray(c.zmm[s1][:vl])
                        res[lane * cb:(lane + 1) * cb] = chunk
                        out = m1.mask_merge(c.zmm[dst], bytes(res), esz, vl, kval, z)
                        c.exp.append("zmm%d=%s" % (dst, hexs(out)))
                        c.code = evex(mmm, 1, w, opc, dst, rm, vvvv=s1, ll=VL_LL[vl], z=z, aaa=kreg,
                                      imm=imm, n=cb)
                    elif kind == "ext":
                        c.set_zmm(s1, rnd_bytes(64))
                        lane = imm % nlanes
                        chunk = c.zmm[s1][lane * cb:(lane + 1) * cb]
                        if mem:
                            oldm = rnd_bytes(cb)
                            mem_case(c, -cb, oldm)
                            ov, nv = elems(oldm, esz), elems(chunk, esz)
                            outm = pack([nv[j] if (kval is None or (kval >> j) & 1) else ov[j]
                                         for j in range(cb // esz)], esz)
                            c.exp.append("m+0x%X=%s" % (MEM_RSI - cb, hexs(outm)))
                            rm = Mem(RSI, -cb)
                        else:
                            c.set_zmm(dst, rnd_bytes(64))
                            out = m1.mask_merge(c.zmm[dst], chunk, esz, cb, kval, z)
                            c.exp.append("zmm%d=%s" % (dst, hexs(out)))
                            rm = dst
                        c.code = evex(mmm, 1, w, opc, s1, rm, ll=VL_LL[vl], z=z, aaa=kreg, imm=imm,
                                      n=cb)
                    else:
                        c.set_zmm(dst, rnd_bytes(64))
                        chunk = rnd_bytes(cb)
                        if mem:
                            mem_case(c, cb, chunk)
                            rm = Mem(RSI, cb)
                        else:
                            c.set_zmm(s2, chunk + rnd_bytes(64 - cb))
                            rm = s2
                        out = m1.mask_merge(c.zmm[dst], chunk * (vl // cb), esz, vl, kval, z)
                        c.exp.append("zmm%d=%s" % (dst, hexs(out)))
                        c.code = evex(mmm, 1, w, opc, dst, rm, ll=VL_LL[vl], z=z, aaa=kreg, n=cb)
                    emit(c, post)
        # #UD: VL not listed, memory-only forms with a register, EVEX.b
        low = min(vls)
        if low > 16:
            c = Case("%s VL%d #UD" % (name, low * 4))
            c.code = evex(mmm, 1, w, opc, 1, 3, vvvv=2 if kind == "ins" else None, ll=VL_LL[low // 2],
                          imm=0 if kind in ("ins", "ext") else None)
            c.fault = "#UD"
            emit(c, post)
        if kind == "bcm":
            c = Case("%s register source #UD" % name)
            c.code = evex(mmm, 1, w, opc, 1, 3, ll=2)
            c.fault = "#UD"
            emit(c, post)


# ---------------------------------------------------------------------------------------
# self test (hand-derived values from the SDM text)
# ---------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    one, two, m1f = 0x3F800000, 0x40000000, 0xBF800000
    # VFPCLASS (Table 5-11)
    chk("fpclass qnan", m_fpclass(0x7FC00000, F32, 0x01, 0x1F80), 1)
    chk("fpclass snan", m_fpclass(0x7F800001, F32, 0x80, 0x1F80), 1)
    chk("fpclass snan not qnan", m_fpclass(0x7F800001, F32, 0x01, 0x1F80), 0)
    chk("fpclass -0", m_fpclass(0x80000000, F32, 0x04, 0x1F80), 1)
    chk("fpclass -0 not neg finite", m_fpclass(0x80000000, F32, 0x40, 0x1F80), 0)
    chk("fpclass denorm", m_fpclass(0x00000001, F32, 0x20, 0x1F80), 1)
    chk("fpclass denorm daz -> +0", m_fpclass(0x00000001, F32, 0x02, 0x1F80 | DAZ), 1)
    chk("fpclass -denorm daz not neg", m_fpclass(0x80000001, F32, 0x40, 0x1F80 | DAZ), 0)
    chk("fpclass -denorm neg", m_fpclass(0x80000001, F32, 0x40, 0x1F80), 1)
    chk("fpclass -inf", m_fpclass(0xFFF0000000000000, F64, 0x10, 0x1F80), 1)
    chk("fpclass -inf not neg finite", m_fpclass(0xFFF0000000000000, F64, 0x40, 0x1F80), 0)
    # VRANGE (Figure 5-27, Tables 5-21..5-23)
    chk("range min", m_range(one, two, F32, 0x00, 0x1F80), (one, 0))
    chk("range max", m_range(one, two, F32, 0x01, 0x1F80), (two, 0))
    chk("range minabs", m_range(0xC0000000, one, F32, 0x02, 0x1F80), (0xBF800000 & 0x7FFFFFFF | 0x80000000, 0))
    chk("range maxabs sign src1", m_range(m1f, two, F32, 0x03, 0x1F80), (0xC0000000, 0))
    chk("range maxabs sign cmp", m_range(m1f, two, F32, 0x07, 0x1F80), (two, 0))
    chk("range clear sign", m_range(m1f, 0xC0000000, F32, 0x08, 0x1F80), (two, 0))
    chk("range set sign", m_range(one, two, F32, 0x0D, 0x1F80), (0xC0000000, 0))
    chk("range +0 -0 min", m_range(0, 0x80000000, F32, 0x04, 0x1F80), (0x80000000, 0))
    chk("range -0 +0 max", m_range(0x80000000, 0, F32, 0x05, 0x1F80), (0, 0))
    chk("range eq mag minabs", m_range(two, 0xC0000000, F32, 0x06, 0x1F80), (0xC0000000, 0))
    chk("range eq mag maxabs", m_range(0xC0000000, two, F32, 0x07, 0x1F80), (two, 0))
    chk("range snan1", m_range(0x7F800001, two, F32, 0x0D, 0x1F80), (0x7FC00001, IE))
    chk("range snan2", m_range(two, 0xFF800001, F32, 0x08, 0x1F80), (0xFFC00001, IE))
    chk("range qnan2 -> src1", m_range(two, 0x7FC00000, F32, 0x05, 0x1F80), (two, 0))
    chk("range qnan1 -> src2 sign src1", m_range(0xFFC00000, two, F32, 0x00, 0x1F80), (0xC0000000, 0))
    chk("range denorm DE", m_range(1, two, F32, 0x00, 0x1F80), (1, DE))
    chk("range denorm qnan no DE", m_range(1, 0x7FC00000, F32, 0x05, 0x1F80), (1, 0))
    chk("range denorm daz", m_range(0x80000001, two, F32, 0x00, 0x1F80 | DAZ), (0x80000000, 0))
    # VREDUCE (Table 5-27 and the formula)
    chk("reduce 1.75 M0 rne", m_reduce(0x3FE00000, F32, 0x00, 0x1F80), (0xBE800000, PE))
    chk("reduce 1.75 M0 rz", m_reduce(0x3FE00000, F32, 0x03, 0x1F80), (0x3F400000, PE))
    chk("reduce 1.75 M0 rd", m_reduce(0x3FE00000, F32, 0x01, 0x1F80), (0x3F400000, PE))
    chk("reduce 1.75 M0 ru", m_reduce(0x3FE00000, F32, 0x02, 0x1F80), (0xBE800000, PE))
    chk("reduce 1.75 M1 rz spe", m_reduce(0x3FE00000, F32, 0x1B, 0x1F80), (0x3E800000, 0))
    chk("reduce 2.0 -> +0", m_reduce(two, F32, 0x00, 0x1F80), (0, 0))
    chk("reduce 2.0 rd -> -0", m_reduce(two, F32, 0x01, 0x1F80), (0x80000000, 0))
    chk("reduce -0", m_reduce(0x80000000, F32, 0x00, 0x1F80), (0, 0))
    chk("reduce inf", m_reduce(0xFF800000, F32, 0x00, 0x1F80), (0, 0))
    chk("reduce snan", m_reduce(0x7F800001, F32, 0x00, 0x1F80), (0x7FC00001, IE))
    chk("reduce mxcsr rc", m_reduce(0x3FE00000, F32, 0x04, 0x7F80), (0x3F400000, PE))
    # |src| < 2^-M, RU, src > 0: Round(src - 2^-M): 2^-30 - 1 rounds (RU) to -(1 - 2^-24)
    chk("reduce tiny ru", m_reduce(0x30800000, F32, 0x02, 0x1F80), (0xBF7FFFFF, PE))
    chk("reduce tiny rne", m_reduce(0x30800000, F32, 0x00, 0x1F80), (0x30800000, PE))
    # CVT
    chk("cvt i2f 2^24+1 rne", m_cvt_i2f((1 << 24) + 1, F32, 0), (0x4B800000, PE))
    chk("cvt i2f 2^24+1 ru", m_cvt_i2f((1 << 24) + 1, F32, 2), (0x4B800001, PE))
    chk("cvt f2i 2^63", m_cvt_f2i(0x5F000000, F32, 0, True, 0x1F80), (1 << 63, IE))
    chk("cvt f2u 2^63", m_cvt_f2i(0x5F000000, F32, 0, False, 0x1F80), (1 << 63, 0))
    chk("cvt f2u -1", m_cvt_f2i(m1f, F32, 0, False, 0x1F80), (M64, IE))
    chk("cvt f2u -0.25 rne", m_cvt_f2i(0xBE800000, F32, 0, False, 0x1F80), (0, PE))
    chk("cvt f2i -1.5 rne", m_cvt_f2i(0xBFC00000, F32, 0, True, 0x1F80), ((-2) & M64, PE))
    chk("cvt f2i -1.5 rz", m_cvt_f2i(0xBFC00000, F32, 3, True, 0x1F80), ((-1) & M64, PE))
    chk("cvt f2i nan", m_cvt_f2i(0x7FC00000, F32, 0, True, 0x1F80), (1 << 63, IE))
    # encoding: VRANGEPS zmm1, zmm2, zmm3, 5 = 62 F3 6D 48 50 CB 05
    chk("enc vrangeps", evex(3, 1, 0, 0x50, 1, 3, vvvv=2, ll=2, imm=5),
        bytes([0x62, 0xF3, 0x6D, 0x48, 0x50, 0xCB, 0x05]))
    return ok


# ---------------------------------------------------------------------------------------
# validation of the model on the host CPU (no AVX-512): legacy SSE forms of the same
# operations run as emu-alltest hardware cases (self-generated snippets only)
# ---------------------------------------------------------------------------------------
HW_MX = [0x1F80, 0x3F80, 0x5F80, 0x7F80, 0x1FC0, 0x9F80, 0x1F80 & ~0x80, 0x1F80 & ~0x1000]


def hw_lines():
    """(asm, inputs, expect dict) per hardware case"""
    out = []
    # bitwise PS/PD (the EVEX forms' model m_logic)
    for nm, op in (("andps", "and"), ("andnps", "andn"), ("orps", "or"), ("xorps", "xor"),
                   ("andpd", "and"), ("andnpd", "andn"), ("orpd", "or"), ("xorpd", "xor")):
        for _ in range(16):
            a, b = rnd_bytes(16), rnd_bytes(16)
            esz = 8 if nm.endswith("pd") else 4
            r = pack([m_logic(op, x, y, esz) for x, y in zip(elems(a, esz), elems(b, esz))], esz)
            out.append(("%s xmm0, xmm1" % nm, "xmm0=%s xmm1=%s" % (hexs(a), hexs(b)),
                        {"xmm0": hexs(r)}))
    # 64-bit integer -> FP (VCVTQQ2PS/PD element model), RC through MXCSR
    ints = [0, 1, (1 << 63), M64, (1 << 63) - 1, (1 << 24) + 1, (1 << 53) + 1, (1 << 54) + 3,
            (-((1 << 24) + 1)) & M64, (-((1 << 53) + 1)) & M64, 0xFFFFFFFF, 0x8000000000000001]
    ints += [RNG.getrandbits(64) for _ in range(24)] + \
        [RNG.getrandbits(RNG.randint(20, 62)) for _ in range(24)]
    for nm, f in (("cvtsi2ss", F32), ("cvtsi2sd", F64)):
        for mx in HW_MX:
            for v in ints:
                sv = v - (1 << 64) if v >> 63 else v
                bits, fl = m_cvt_i2f(sv, f, (mx >> 13) & 3)
                unm = fl & ~(mx >> 7) & 0x3F
                xm = "0" * 32
                if unm:
                    e = {"fault": 19, "mxcsr": mx | fl}
                else:
                    sz = 8 if f is F64 else 4
                    e = {"xmm0": hexs(pack([bits], sz)) + xm[2 * sz:], "mxcsr": mx | fl}
                out.append(("%s xmm0, rax" % nm, "rax=0x%X xmm0=%s mxcsr=0x%X" % (v, xm, mx), e))
    # FP -> 64-bit integer (VCVT[T]PS/PD2QQ element model)
    for nm, f, tr in (("cvtss2si", F32, False), ("cvtsd2si", F64, False),
                      ("cvttss2si", F32, True), ("cvttsd2si", F64, True)):
        sz = 8 if f is F64 else 4
        vals = list(fp_specials(sz))
        for _ in range(40):
            vals.append(rnd_cvt_src("f64" if f is F64 else "f32"))
        vals += [0x5F000000, 0xDF000000, 0xDF000001, 0x5EFFFFFF] if f is F32 else \
            [0x43E0000000000000, 0xC3E0000000000000, 0xC3E0000000000001, 0x43DFFFFFFFFFFFFF]
        for mx in HW_MX:
            for x in vals:
                r, fl = m_cvt_f2i(x, f, 3 if tr else (mx >> 13) & 3, True, mx)
                unm = fl & ~(mx >> 7) & 0x3F
                if unm:
                    e = {"fault": 19, "mxcsr": mx | fl}
                else:
                    e = {"rax": "0x%X" % r, "mxcsr": mx | fl}
                xin = hexs(pack([x], sz)) + "00" * (16 - sz)
                out.append(("%s rax, xmm0" % nm, "rax=0x5555 xmm0=%s mxcsr=0x%X" % (xin, mx), e))
    # VREDUCE formula: SRC - ROUND(SRC * 2^M) * 2^-M with ROUNDPS/PD (SPE) and SUBPS/PD under
    # the same RC (MXCSR), no DAZ/FTZ; only the value is compared
    for sfx, f, sz in (("ps", F32, 4), ("pd", F64, 8)):
        n = 16 // sz
        for M in (0, 1, 3, 7, 15):
            for rcv in range(4):
                for t in range(8):
                    vals = []
                    for j in range(n):
                        if t < 2:
                            e = RNG.randint(-M - 40, -M)
                        else:
                            e = RNG.randint(-M - 3, (20 if f is F32 else 50) - M)
                        v = Fraction(RNG.getrandbits(f.p - 1) | (1 << (f.p - 1)), 1 << (f.p - 1)) * \
                            Fraction(2) ** e
                        if RNG.getrandbits(1):
                            v = -v
                        vals.append(encode_value(v, f, 0, False)[0])
                    res = [m_reduce(x, f, (M << 4) | rcv | 8, 0x1F80)[0] for x in vals]
                    mx = 0x1F80 | (rcv << 13)
                    p2m = encode_value(Fraction(2) ** M, f, 0, False)[0]
                    p2mi = encode_value(Fraction(1, 2 ** M), f, 0, False)[0]
                    asm = ("movaps xmm1, xmm0; mul%s xmm1, xmm2; round%s xmm1, xmm1, %d; "
                           "mul%s xmm1, xmm3; sub%s xmm0, xmm1" % (sfx, sfx, 8 | rcv, sfx, sfx))
                    inp = "xmm0=%s xmm2=%s xmm3=%s mxcsr=0x%X" % (
                        hexs(pack(vals, sz)), hexs(pack([p2m] * n, sz)), hexs(pack([p2mi] * n, sz)),
                        mx)
                    out.append((asm, inp, {"xmm0": hexs(pack(res, sz)), "valueonly": 1}))
    return out


def hwcheck_gen(outf, expect_path):
    import json
    exp = []
    for asm, inp, e in hw_lines():
        outf.write("%s | %s\n" % (asm, inp))
        exp.append(dict(e, asm=asm, inp=inp))
    json.dump(exp, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    import json
    import re
    exp = json.load(open(expect_path))
    cur, bad, seen = None, 0, 0
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] ", line)
        if m and " | " in line:
            cur = int(m.group(1))
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if not (m and cur is not None):
            continue
        fields = m.group(1)
        fault = -1
        fm = re.match(r"fault #(\d+)\s*(.*)$", fields)
        if fm:
            fault, fields = int(fm.group(1)), fm.group(2)
        kv = dict(x.split("=", 1) for x in fields.split() if "=" in x)
        e = exp[cur]
        inp = dict(x.split("=", 1) for x in e["inp"].split())
        seen += 1
        bad_here = []
        if fault != e.get("fault", -1):
            bad_here.append("fault %d vs %d" % (fault, e.get("fault", -1)))
        if "xmm0" in e and fault == -1:
            got = kv.get("xmm0", inp.get("xmm0", ""))
            if got.upper() != e["xmm0"].upper():
                bad_here.append("xmm0 %s vs %s" % (got, e["xmm0"]))
        if "rax" in e and fault == -1:
            got = int(kv.get("rax", inp["rax"]), 16)
            if got != int(e["rax"], 16):
                bad_here.append("rax %X vs %s" % (got, e["rax"]))
        if "mxcsr" in e and not e.get("valueonly"):
            got = int(kv.get("mxcsr", inp["mxcsr"]), 16)
            if got != e["mxcsr"]:
                bad_here.append("mxcsr %X vs %X" % (got, e["mxcsr"]))
        if bad_here:
            bad += 1
            if bad <= 30:
                print("[%d] %s | %s: %s" % (cur, e["asm"], e["inp"], "; ".join(bad_here)))
        cur = None
    print("hwcheck: %d cases compared, %d differ from the model" % (seen, bad))
    return bad == 0 and seen == len(exp)


def write(lst, out, header):
    for h in header:
        out.write("# " + h + "\n")
    for c in lst:
        if isinstance(c, str):
            out.write(c + "\n")
        else:
            out.write("# " + c.title + "\n")
            out.write(c.line() + "\n")


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
    if "--cases" in sys.argv or "--cases-post" in sys.argv:
        for sp in FORMS:
            if sp["lay"].startswith("s"):
                gen_scalar(sp)
            else:
                gen_packed(sp)
        gen_misc()
        gen_cvt()
        gen_chunk()
        hdr = ["AVX512DQ milestone M3 (ledger U290-U296): expected values from the independent SDM",
               "model Emulator/tools/isa/ref_evex_m3_dq.py (regenerate, do not edit). The i5-13600K",
               "has no AVX-512: expected-value cases only, run with AVX-512 enabled:"]
        if "--cases" in sys.argv:
            write(cases, sys.stdout, hdr + [
                "  emu-alltest --cases Emulator\\data\\cases_evex_m3_dq.txt --avx512 --xcr0 0xE7 --expect-only",
                "RSI = R14 = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases)."])
        else:
            write(post, sys.stdout, hdr + [
                "  emu-alltest --cases Emulator\\data\\cases_evex_m3_dq_post.txt --avx512 --xcr0 0xE7 --expect-only",
                "POST-MERGE file: needs m2_engine U192 (masked scalar, DEST != SRC1), m2_cvt U230/U233",
                "(QQ conversions) and m2_perm U210/U215 (Tuple2/Tuple8 insert/extract/broadcast)."])
        return
    print(__doc__)


if __name__ == "__main__":
    main()
