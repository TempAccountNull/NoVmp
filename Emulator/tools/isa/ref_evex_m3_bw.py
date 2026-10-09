#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m3_bw.py -- independent reference model (Python 3 stdlib only) of the AVX512BW
instructions (ledger U260-U269) and generator of the expected-value case file
Emulator/data/cases_evex_m3_bw.txt.

Written from the Intel SDM text only (Vol2A 2.7 / 2.8 EVEX rules, Tables 2-36 .. 2-46 and
2-57; the "Operation" pseudocode of every instruction page in Vol2B/2C), not from any C
implementation. The EVEX encoder and the generic case container come from ref_evex_m1.py.

  VMOVDQU8/16, VPADDB/W, VPADDSB/SW, VPADDUSB/USW, VPSUBB/W, VPSUBSB/SW, VPSUBUSB/USW,
  VPMULLW, VPMULHW, VPMULHUW, VPMULHRSW, VPMADDWD, VPMADDUBSW, VPAVGB/W, VPSADBW, VDBPSADBW,
  VPABSB/W, VPMINSB/SW/UB/UW, VPMAXSB/SW/UB/UW, VPSHUFB, VPSHUFHW/LW, VPALIGNR,
  VPUNPCKL/HBW, VPUNPCKL/HWD, VPACKSSWB/SSDW/USWB/USDW, VPEXTRB/W, VPINSRB/W,
  VPBROADCASTB/W (xmm/m, GPR), VPMOVZXBW/SXBW, VPMOVWB/SWB/USWB, VPERMW, VPERMI2W/T2W,
  VPBLENDMB/W, VPSLLW/VPSRLW/VPSRAW (imm8, xmm/m128 count), VPSLLVW/VPSRLVW/VPSRAVW,
  VPSLLDQ/VPSRLDQ, VPCMPB/UB/W/UW, VPCMPEQB/W, VPCMPGTB/W, VPTESTMB/W, VPTESTNMB/W,
  VPMOVB2M/W2M, VPMOVM2B/W; 64-bit opmasks with KMOVQ/KADDQ/KORTESTQ/KSHIFTRQ.

Generic EVEX wrappers (SDM pseudocode of every page):
  MASK     FOR j := 0 TO KL-1 (KL = VL / writemask element): k1[j] or no writemask -> result,
           else merging (unchanged; VPBLENDM: SRC1) or zeroing (0); DEST[MAXVL-1:VL] := 0
           (narrowing VPMOVWB: DEST[MAXVL-1:VL/2] := 0)
  K-DEST   DEST[j] := k2[j] ? cmp : 0, DEST[MAX_KL-1:KL] := 0
  LOAD     masked-off elements are not read (fault suppression) except in classes E4NF/E9NF
  STORE    masked-off elements are not written and never fault

Usage:
  python ref_evex_m3_bw.py --selftest       hand-derived checks of the model, exit 0 on pass
  python ref_evex_m3_bw.py --cases          Emulator/data/cases_evex_m3_bw.txt (stdout)
  python ref_evex_m3_bw.py --hwgen OUT.json hardware cases (VEX/SSE forms the i5-13600K has) on
                                            stdout, expected values of the model into OUT.json
  python ref_evex_m3_bw.py --hwcmp LOG OUT.json   compare an emu-alltest run of them to the model
"""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ref_evex_m1 import evex, Mem, Case, byte_list, elems, pack, hexs, MEM_RSI, RSI, R14, VL_LL  # noqa: E402

RNG = random.Random(0xB3_E7E8)

GPR = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
       "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]


def rb(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


# ---------------------------------------------------------------------------------------
# element helpers
# ---------------------------------------------------------------------------------------
def E(buf, esz):
    return elems(buf, esz)


def P(vals, esz):
    return pack(vals, esz)


def sx(v, esz):
    bits = 8 * esz
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def sat_s(v, esz):
    lo, hi = -(1 << (8 * esz - 1)), (1 << (8 * esz - 1)) - 1
    return max(lo, min(hi, v))


def sat_u(v, esz):
    return max(0, min((1 << (8 * esz)) - 1, v))


def lanes(buf):
    return [buf[i:i + 16] for i in range(0, len(buf), 16)]


def um(v, esz):
    return v & ((1 << (8 * esz)) - 1)


class Ctx:
    """operands of one instruction: a = SRC1 (vvvv) bytes, b = SRC2 (r/m) bytes, d = the
    destination register before the instruction (64 bytes), imm, k (opmask source)"""

    def __init__(self, vl, a=None, b=None, d=None, imm=0, k=0):
        self.vl, self.a, self.b, self.d, self.imm, self.k = vl, a, b, d, imm, k


# ---------------------------------------------------------------------------------------
# instruction models (SDM Operation sections); every function returns the full-length
# result (bytes) before masking, or a list of booleans for an opmask destination
# ---------------------------------------------------------------------------------------
def ew(esz, f):
    return lambda c: P([um(f(x, y), esz) for x, y in zip(E(c.a, esz), E(c.b, esz))], esz)


def ew1(esz, f):
    return lambda c: P([um(f(y), esz) for y in E(c.b, esz)], esz)


OPS = {}
for _es, _sfx in ((1, "b"), (2, "w")):
    OPS["add" + _sfx] = ew(_es, lambda x, y: x + y)
    OPS["sub" + _sfx] = ew(_es, lambda x, y: x - y)
    OPS["adds" + _sfx] = ew(_es, (lambda e: lambda x, y: sat_s(sx(x, e) + sx(y, e), e))(_es))
    OPS["subs" + _sfx] = ew(_es, (lambda e: lambda x, y: sat_s(sx(x, e) - sx(y, e), e))(_es))
    OPS["addus" + _sfx] = ew(_es, (lambda e: lambda x, y: sat_u(x + y, e))(_es))
    OPS["subus" + _sfx] = ew(_es, (lambda e: lambda x, y: sat_u(x - y, e))(_es))
    OPS["mins" + _sfx] = ew(_es, (lambda e: lambda x, y: min(sx(x, e), sx(y, e)))(_es))
    OPS["maxs" + _sfx] = ew(_es, (lambda e: lambda x, y: max(sx(x, e), sx(y, e)))(_es))
    OPS["minu" + _sfx] = ew(_es, lambda x, y: min(x, y))
    OPS["maxu" + _sfx] = ew(_es, lambda x, y: max(x, y))
    OPS["avg" + _sfx] = ew(_es, lambda x, y: (x + y + 1) >> 1)
    OPS["abs" + _sfx] = ew1(_es, (lambda e: lambda y: abs(sx(y, e)))(_es))
OPS["mullw"] = ew(2, lambda x, y: x * y)
OPS["mulhw"] = ew(2, lambda x, y: (sx(x, 2) * sx(y, 2)) >> 16)
OPS["mulhuw"] = ew(2, lambda x, y: (x * y) >> 16)
# VPMULHRSW: temp := ((SRC1 * SRC2) >> 14) + 1; DEST := temp[16:1]
OPS["mulhrsw"] = ew(2, lambda x, y: (((sx(x, 2) * sx(y, 2)) >> 14) + 1) >> 1)


def op_maddwd(c):
    a, b = E(c.a, 2), E(c.b, 2)
    return P([sx(a[2 * j], 2) * sx(b[2 * j], 2) + sx(a[2 * j + 1], 2) * sx(b[2 * j + 1], 2)
              for j in range(c.vl // 4)], 4)


def op_maddubsw(c):
    # SRC1 (vvvv): unsigned bytes, SRC2 (r/m): signed bytes, signed word saturation
    a, b = E(c.a, 1), E(c.b, 1)
    return P([sat_s(a[2 * j] * sx(b[2 * j], 1) + a[2 * j + 1] * sx(b[2 * j + 1], 1), 2)
              for j in range(c.vl // 2)], 2)


def op_sadbw(c):
    a, b = E(c.a, 1), E(c.b, 1)
    return P([sum(abs(a[8 * q + i] - b[8 * q + i]) for i in range(8)) for q in range(c.vl // 8)], 8)


def op_dbpsadbw(c):
    out = []
    a = E(c.a, 1)
    for ln in range(c.vl // 16):
        bd = E(c.b[16 * ln:16 * ln + 16], 4)
        tmp = P([bd[(c.imm >> (2 * i)) & 3] for i in range(4)], 4)
        t = E(tmp, 1)
        for blk in range(2):
            I = 8 * blk
            s = a[16 * ln + I:16 * ln + I + 8]
            out.append(sum(abs(s[i] - t[I + i]) for i in range(4)))
            out.append(sum(abs(s[i] - t[I + 1 + i]) for i in range(4)))
            out.append(sum(abs(s[4 + i] - t[I + 2 + i]) for i in range(4)))
            out.append(sum(abs(s[4 + i] - t[I + 3 + i]) for i in range(4)))
    return P(out, 2)


def op_pshufb(c):
    out = []
    for la, lb in zip(lanes(c.a), lanes(c.b)):
        out += [0 if x & 0x80 else la[x & 15] for x in lb]
    return bytes(out)


def op_pshufhw(c):
    out = b""
    for lb in lanes(c.b):
        w = E(lb, 2)
        out += P(w[:4] + [w[4 + ((c.imm >> (2 * i)) & 3)] for i in range(4)], 2)
    return out


def op_pshuflw(c):
    out = b""
    for lb in lanes(c.b):
        w = E(lb, 2)
        out += P([w[(c.imm >> (2 * i)) & 3] for i in range(4)] + w[4:], 2)
    return out


def op_palignr(c):
    # per 128-bit lane: (SRC1:SRC2) >> imm8*8, the low 16 bytes (0 for imm8 >= 32)
    out = b""
    for la, lb in zip(lanes(c.a), lanes(c.b)):
        t = lb + la + bytes(256)
        out += t[c.imm:c.imm + 16]
    return out


def unpck(esz, high):
    def f(c):
        out = b""
        h = 8 // esz
        for la, lb in zip(lanes(c.a), lanes(c.b)):
            x, y = E(la, esz), E(lb, esz)
            o = h if high else 0
            r = []
            for i in range(h):
                r += [x[o + i], y[o + i]]
            out += P(r, esz)
        return out
    return f


def packop(src_es, signed_sat):
    d_es = src_es // 2

    def f(c):
        out = b""
        for la, lb in zip(lanes(c.a), lanes(c.b)):
            r = []
            for x in E(la, src_es) + E(lb, src_es):
                v = sx(x, src_es)
                r.append(sat_s(v, d_es) if signed_sat else sat_u(v, d_es))
            out += P(r, d_es)
        return out
    return f


def shift_word(kind, cnt, x):
    if kind == "sll":
        return (x << cnt) & 0xFFFF if cnt < 16 else 0
    if kind == "srl":
        return x >> cnt if cnt < 16 else 0
    return um(sx(x, 2) >> min(cnt, 15), 2)


def op_shift_imm(kind):
    return lambda c: P([shift_word(kind, c.imm & 0xFF, x) for x in E(c.b, 2)], 2)


def op_shift_xmm(kind):
    # count: bits 63:0 of xmm3/m128 (unsigned)
    return lambda c: P([shift_word(kind, int.from_bytes(c.b[:8], "little"), x) for x in E(c.a, 2)], 2)


def op_shift_var(kind):
    return lambda c: P([shift_word(kind, y, x) for x, y in zip(E(c.a, 2), E(c.b, 2))], 2)


def op_bytesh(left):
    def f(c):
        out = b""
        n = c.imm & 0xFF
        for lb in lanes(c.b):
            if n > 15:
                out += bytes(16)
            elif left:
                out += (bytes(n) + lb)[:16]
            else:
                out += (lb + bytes(16))[n:n + 16]
        return out
    return f


def op_movx(signed):
    return lambda c: P([sx(x, 1) if signed else x for x in E(c.b[:c.vl // 2], 1)], 2)


def op_movwb(kind):
    def f(c):
        r = []
        for x in E(c.b, 2):
            if kind == "t":
                r.append(x & 0xFF)
            elif kind == "s":
                r.append(sat_s(sx(x, 2), 1))
            else:
                r.append(sat_u(x, 1))           # unsigned word -> unsigned byte
        return P(r, 1)
    return f


def op_permw(c):
    n = c.vl // 2
    idx, t = E(c.a, 2), E(c.b, 2)
    return P([t[i & (n - 1)] for i in idx], 2)


def op_permi2w(c):
    n = c.vl // 2
    idx, t1, t2 = E(c.d[:c.vl], 2), E(c.a, 2), E(c.b, 2)
    return P([(t2 if i & n else t1)[i & (n - 1)] for i in idx], 2)


def op_permt2w(c):
    n = c.vl // 2
    idx, t1, t2 = E(c.a, 2), E(c.d[:c.vl], 2), E(c.b, 2)
    return P([(t2 if i & n else t1)[i & (n - 1)] for i in idx], 2)


def op_copy(c):
    return c.b[:c.vl]


def op_bcst(esz):
    return lambda c: c.b[:esz] * (c.vl // esz)


def op_movm2(esz):
    return lambda c: P([(1 << (8 * esz)) - 1 if (c.k >> j) & 1 else 0 for j in range(c.vl // esz)], esz)


def kcmp(esz, pred, sign):
    def f(c):
        r = []
        for x, y in zip(E(c.a, esz), E(c.b, esz)):
            if sign:
                x, y = sx(x, esz), sx(y, esz)
            r.append([x == y, x < y, x <= y, False, x != y, not x < y, not x <= y, True][pred & 7])
        return r
    return f


def kcmp_imm(esz, sign):
    return lambda c: kcmp(esz, c.imm & 7, sign)(c)


def ktest(esz, neg):
    return lambda c: [((x & y) == 0) if neg else ((x & y) != 0) for x, y in zip(E(c.a, esz), E(c.b, esz))]


def kmsb(esz):
    return lambda c: [bool(x >> (8 * esz - 1)) for x in E(c.b, esz)]


OPS.update({
    "maddwd": op_maddwd, "maddubsw": op_maddubsw, "sadbw": op_sadbw, "dbpsadbw": op_dbpsadbw,
    "pshufb": op_pshufb, "pshufhw": op_pshufhw, "pshuflw": op_pshuflw, "palignr": op_palignr,
    "punpcklbw": unpck(1, False), "punpckhbw": unpck(1, True),
    "punpcklwd": unpck(2, False), "punpckhwd": unpck(2, True),
    "packsswb": packop(2, True), "packuswb": packop(2, False),
    "packssdw": packop(4, True), "packusdw": packop(4, False),
    "slliw": op_shift_imm("sll"), "srliw": op_shift_imm("srl"), "sraiw": op_shift_imm("sra"),
    "sllw": op_shift_xmm("sll"), "srlw": op_shift_xmm("srl"), "sraw": op_shift_xmm("sra"),
    "sllvw": op_shift_var("sll"), "srlvw": op_shift_var("srl"), "sravw": op_shift_var("sra"),
    "pslldq": op_bytesh(True), "psrldq": op_bytesh(False),
    "pmovzxbw": op_movx(False), "pmovsxbw": op_movx(True),
    "pmovwb": op_movwb("t"), "pmovswb": op_movwb("s"), "pmovuswb": op_movwb("u"),
    "permw": op_permw, "permi2w": op_permi2w, "permt2w": op_permt2w,
    "copy": op_copy, "blendm": op_copy, "bcstb": op_bcst(1), "bcstw": op_bcst(2),
    "movm2b": op_movm2(1), "movm2w": op_movm2(2),
    "cmpeqb": kcmp(1, 0, True), "cmpeqw": kcmp(2, 0, True),
    "cmpgtb": kcmp(1, 6, True), "cmpgtw": kcmp(2, 6, True),
    "cmpb": kcmp_imm(1, True), "cmpw": kcmp_imm(2, True),
    "cmpub": kcmp_imm(1, False), "cmpuw": kcmp_imm(2, False),
    "testmb": ktest(1, False), "testmw": ktest(2, False),
    "testnmb": ktest(1, True), "testnmw": ktest(2, True),
    "b2m": kmsb(1), "w2m": kmsb(2),
})


# ---------------------------------------------------------------------------------------
# forms. lay: rvm  reg = dst, vvvv = SRC1, r/m = SRC2      rm   reg = dst, r/m = SRC
#             rmi  rm + imm8                               vmi  vvvv = dst, r/m = SRC, imm8
#             rvmi rvm + imm8          kvm(i) k dst        mr   r/m = dst (narrowing), reg = SRC
#             km   k (reg) <- vector (r/m)                 mk   vector (reg) <- k (r/m)
#             rg   vector (reg) <- GPR (r/m)
# tt: fm Full Mem (N = VL), full (N = VL / bcst element), half (VL/2), t1s (element), m128 (16)
# kes: writemask element size (bytes); nf: E4NF (no fault suppression); w: None = WIG
# ---------------------------------------------------------------------------------------
def F(name, mmm, pp, opc, lay, op, kes, tt="fm", w=None, nf=False, bes=None, mask="mz", reg=None,
      narrow=0, vls=(16, 32, 64), es=None):
    return dict(name=name, mmm=mmm, pp=pp, opc=opc, lay=lay, op=op, kes=kes, tt=tt, w=w, nf=nf,
                bes=bes, mask=mask, reg=reg, narrow=narrow, vls=vls, es=es if es else kes)


FORMS = [
    # U261
    F("VMOVDQU8", 1, 3, 0x6F, "rm", "copy", 1, w=0),
    F("VMOVDQU16", 1, 3, 0x6F, "rm", "copy", 2, w=1),
    F("VMOVDQU8 store", 1, 3, 0x7F, "mr", "copy", 1, w=0),
    F("VMOVDQU16 store", 1, 3, 0x7F, "mr", "copy", 2, w=1),
    # U262
    F("VPADDB", 1, 1, 0xFC, "rvm", "addb", 1), F("VPADDW", 1, 1, 0xFD, "rvm", "addw", 2),
    F("VPADDSB", 1, 1, 0xEC, "rvm", "addsb", 1), F("VPADDSW", 1, 1, 0xED, "rvm", "addsw", 2),
    F("VPADDUSB", 1, 1, 0xDC, "rvm", "addusb", 1), F("VPADDUSW", 1, 1, 0xDD, "rvm", "addusw", 2),
    F("VPSUBB", 1, 1, 0xF8, "rvm", "subb", 1), F("VPSUBW", 1, 1, 0xF9, "rvm", "subw", 2),
    F("VPSUBSB", 1, 1, 0xE8, "rvm", "subsb", 1), F("VPSUBSW", 1, 1, 0xE9, "rvm", "subsw", 2),
    F("VPSUBUSB", 1, 1, 0xD8, "rvm", "subusb", 1), F("VPSUBUSW", 1, 1, 0xD9, "rvm", "subusw", 2),
    F("VPMINSB", 2, 1, 0x38, "rvm", "minsb", 1), F("VPMINSW", 1, 1, 0xEA, "rvm", "minsw", 2),
    F("VPMINUB", 1, 1, 0xDA, "rvm", "minub", 1), F("VPMINUW", 2, 1, 0x3A, "rvm", "minuw", 2),
    F("VPMAXSB", 2, 1, 0x3C, "rvm", "maxsb", 1), F("VPMAXSW", 1, 1, 0xEE, "rvm", "maxsw", 2),
    F("VPMAXUB", 1, 1, 0xDE, "rvm", "maxub", 1), F("VPMAXUW", 2, 1, 0x3E, "rvm", "maxuw", 2),
    F("VPAVGB", 1, 1, 0xE0, "rvm", "avgb", 1), F("VPAVGW", 1, 1, 0xE3, "rvm", "avgw", 2),
    F("VPABSB", 2, 1, 0x1C, "rm", "absb", 1), F("VPABSW", 2, 1, 0x1D, "rm", "absw", 2),
    F("VPMULLW", 1, 1, 0xD5, "rvm", "mullw", 2), F("VPMULHW", 1, 1, 0xE5, "rvm", "mulhw", 2),
    F("VPMULHUW", 1, 1, 0xE4, "rvm", "mulhuw", 2), F("VPMULHRSW", 2, 1, 0x0B, "rvm", "mulhrsw", 2),
    # U263
    F("VPMADDWD", 1, 1, 0xF5, "rvm", "maddwd", 4, nf=True),
    F("VPMADDUBSW", 2, 1, 0x04, "rvm", "maddubsw", 2, nf=True),
    F("VPSADBW", 1, 1, 0xF6, "rvm", "sadbw", 8, nf=True, mask="none"),
    F("VDBPSADBW", 3, 1, 0x42, "rvmi", "dbpsadbw", 2, w=0, nf=True),
    # U264
    F("VPSHUFB", 2, 1, 0x00, "rvm", "pshufb", 1, nf=True),
    F("VPSHUFHW", 1, 2, 0x70, "rmi", "pshufhw", 2, nf=True),
    F("VPSHUFLW", 1, 3, 0x70, "rmi", "pshuflw", 2, nf=True),
    F("VPALIGNR", 3, 1, 0x0F, "rvmi", "palignr", 1, nf=True),
    F("VPUNPCKLBW", 1, 1, 0x60, "rvm", "punpcklbw", 1, nf=True),
    F("VPUNPCKHBW", 1, 1, 0x68, "rvm", "punpckhbw", 1, nf=True),
    F("VPUNPCKLWD", 1, 1, 0x61, "rvm", "punpcklwd", 2, nf=True),
    F("VPUNPCKHWD", 1, 1, 0x69, "rvm", "punpckhwd", 2, nf=True),
    F("VPACKSSWB", 1, 1, 0x63, "rvm", "packsswb", 1, nf=True),
    F("VPACKUSWB", 1, 1, 0x67, "rvm", "packuswb", 1, nf=True),
    F("VPACKSSDW", 1, 1, 0x6B, "rvm", "packssdw", 2, tt="full", w=0, nf=True, bes=4),
    F("VPACKUSDW", 2, 1, 0x2B, "rvm", "packusdw", 2, tt="full", w=0, nf=True, bes=4),
    # U265
    F("VPSRLW imm", 1, 1, 0x71, "vmi", "srliw", 2, nf=True, reg=2),
    F("VPSRAW imm", 1, 1, 0x71, "vmi", "sraiw", 2, nf=True, reg=4),
    F("VPSLLW imm", 1, 1, 0x71, "vmi", "slliw", 2, nf=True, reg=6),
    F("VPSRLW xmm", 1, 1, 0xD1, "rvm", "srlw", 2, tt="m128", nf=True),
    F("VPSRAW xmm", 1, 1, 0xE1, "rvm", "sraw", 2, tt="m128", nf=True),
    F("VPSLLW xmm", 1, 1, 0xF1, "rvm", "sllw", 2, tt="m128", nf=True),
    F("VPSRLVW", 2, 1, 0x10, "rvm", "srlvw", 2, w=1), F("VPSRAVW", 2, 1, 0x11, "rvm", "sravw", 2, w=1),
    F("VPSLLVW", 2, 1, 0x12, "rvm", "sllvw", 2, w=1),
    F("VPSRLDQ", 1, 1, 0x73, "vmi", "psrldq", 1, nf=True, reg=3, mask="none"),
    F("VPSLLDQ", 1, 1, 0x73, "vmi", "pslldq", 1, nf=True, reg=7, mask="none"),
    # U266
    F("VPCMPEQB", 1, 1, 0x74, "kvm", "cmpeqb", 1, mask="kdest"),
    F("VPCMPEQW", 1, 1, 0x75, "kvm", "cmpeqw", 2, mask="kdest"),
    F("VPCMPGTB", 1, 1, 0x64, "kvm", "cmpgtb", 1, mask="kdest"),
    F("VPCMPGTW", 1, 1, 0x65, "kvm", "cmpgtw", 2, mask="kdest"),
    F("VPCMPB", 3, 1, 0x3F, "kvmi", "cmpb", 1, w=0, mask="kdest"),
    F("VPCMPW", 3, 1, 0x3F, "kvmi", "cmpw", 2, w=1, mask="kdest"),
    F("VPCMPUB", 3, 1, 0x3E, "kvmi", "cmpub", 1, w=0, mask="kdest"),
    F("VPCMPUW", 3, 1, 0x3E, "kvmi", "cmpuw", 2, w=1, mask="kdest"),
    F("VPTESTMB", 2, 1, 0x26, "kvm", "testmb", 1, w=0, mask="kdest"),
    F("VPTESTMW", 2, 1, 0x26, "kvm", "testmw", 2, w=1, mask="kdest"),
    F("VPTESTNMB", 2, 2, 0x26, "kvm", "testnmb", 1, w=0, mask="kdest"),
    F("VPTESTNMW", 2, 2, 0x26, "kvm", "testnmw", 2, w=1, mask="kdest"),
    F("VPMOVB2M", 2, 2, 0x29, "km", "b2m", 1, w=0, mask="none", tt=None),
    F("VPMOVW2M", 2, 2, 0x29, "km", "w2m", 2, w=1, mask="none", tt=None),
    F("VPMOVM2B", 2, 2, 0x28, "mk", "movm2b", 1, w=0, mask="none", tt=None),
    F("VPMOVM2W", 2, 2, 0x28, "mk", "movm2w", 2, w=1, mask="none", tt=None),
    # U267
    F("VPBROADCASTB", 2, 1, 0x78, "rm", "bcstb", 1, tt="t1s", w=0),
    F("VPBROADCASTW", 2, 1, 0x79, "rm", "bcstw", 2, tt="t1s", w=0),
    F("VPBROADCASTB r32", 2, 1, 0x7A, "rg", "bcstb", 1, tt=None, w=0),
    F("VPBROADCASTW r32", 2, 1, 0x7B, "rg", "bcstw", 2, tt=None, w=0),
    # U268
    F("VPMOVZXBW", 2, 1, 0x30, "rm", "pmovzxbw", 2, tt="half"),
    F("VPMOVSXBW", 2, 1, 0x20, "rm", "pmovsxbw", 2, tt="half"),
    F("VPMOVWB", 2, 2, 0x30, "mr", "pmovwb", 1, tt="half", w=0, narrow=1),
    F("VPMOVSWB", 2, 2, 0x20, "mr", "pmovswb", 1, tt="half", w=0, narrow=1),
    F("VPMOVUSWB", 2, 2, 0x10, "mr", "pmovuswb", 1, tt="half", w=0, narrow=1),
    # U269
    F("VPERMW", 2, 1, 0x8D, "rvm", "permw", 2, w=1, nf=True),
    F("VPERMI2W", 2, 1, 0x75, "rvm", "permi2w", 2, w=1, nf=True),
    F("VPERMT2W", 2, 1, 0x7D, "rvm", "permt2w", 2, w=1, nf=True),
    F("VPBLENDMB", 2, 1, 0x66, "rvm", "blendm", 1, w=0),
    F("VPBLENDMW", 2, 1, 0x66, "rvm", "blendm", 2, w=1),
]

WSEL = {"VMOVDQU8", "VMOVDQU16", "VMOVDQU8 store", "VMOVDQU16 store", "VPCMPB", "VPCMPW", "VPCMPUB",
        "VPCMPUW", "VPTESTMB", "VPTESTMW", "VPTESTNMB", "VPTESTNMW", "VPMOVB2M", "VPMOVW2M", "VPMOVM2B",
        "VPMOVM2W", "VPBLENDMB", "VPBLENDMW", "VPERMW", "VPERMI2W", "VPERMT2W"}

PP_NAME = ["NP", "66", "F3", "F2"]
MAP_NAME = ["", "0F", "0F38", "0F3A"]


def src_size(f, vl):
    """bytes of the r/m source (or destination of 'mr') at vector length vl"""
    tt = f["tt"]
    if tt == "half":
        return vl // 2
    if tt == "m128":
        return 16
    if tt == "t1s":
        return f["kes"]
    return vl


def disp8_n(f, vl, bcst):
    tt = f["tt"]
    if tt == "full":
        return f["bes"] if bcst else vl
    if tt == "fm":
        return vl
    if tt == "half":
        return vl // 2
    if tt == "t1s":
        return f["kes"]
    if tt == "m128":
        return 16
    return 1


def apply_mask(old, res, kes, dvl, kmask, z, merge=None):
    base = merge if merge is not None else old
    o, r = E(base[:dvl], kes), E(res, kes)
    out = [r[j] if kmask is None or (kmask >> j) & 1 else (0 if z else o[j]) for j in range(dvl // kes)]
    return P(out, kes) + bytes(64 - dvl)


def store_mask(old, res, kes, kmask):
    o, r = E(old, kes), E(res, kes)
    return P([r[j] if kmask is None or (kmask >> j) & 1 else o[j] for j in range(len(o))], kes)


def special_vals(esz):
    bits = 8 * esz
    return [0, 1, (1 << bits) - 1, 1 << (bits - 1), (1 << (bits - 1)) - 1, (1 << (bits - 1)) + 1,
            2, (1 << bits) - 2]


def rnd_vec(n, esz, edge=0.3):
    sp = special_vals(esz)
    return P([RNG.choice(sp) if RNG.random() < edge else RNG.getrandbits(8 * esz) for _ in range(n // esz)], esz)


# ---------------------------------------------------------------------------------------
# one case of a form
# ---------------------------------------------------------------------------------------
cases = []


def emit(c):
    cases.append(c)


def comment(t):
    cases.append("# " + t)


def gen_case(f, vl, variant, dst=1, s1=2, s2=3, kreg=0, kval=None, z=0, imm=None, bcst=0,
             mem_off=0x40, w=None, aimg=None, bimg=None, ll=None, title="", fault=None, gpr=0):
    lay, kes = f["lay"], f["kes"]
    mem = variant == "mem"
    if lay in ("kvm", "kvmi", "km"):
        dst &= 7                            # opmask destination k0-k7
    w = (f["w"] if f["w"] is not None else 0) if w is None else w
    if imm is None and lay in ("vmi", "rmi", "rvmi", "kvmi"):
        imm = RNG.getrandbits(8)
    c = Case("%s VL%d %s%s" % (f["name"], vl * 8, variant, (" " + title) if title else ""))
    regs = {}
    ms = src_size(f, vl)
    es_src = f["bes"] if bcst else kes
    if lay in ("rvm", "rvmi", "kvm", "kvmi"):
        regs[s1] = aimg if aimg is not None else rnd_vec(64, kes)
    # SRC2 / r/m source
    rm_is_src = lay not in ("mr",)
    b = None
    n = 1
    if lay == "mk":
        # k register in ModRM.r/m: EVEX.B / EVEX.X are ignored (Table 2-41), only rm[2:0]
        rm = s2
        c.k[s2 & 7] = kval if kval is not None and kreg == 0 else RNG.getrandbits(64)
    elif lay == "rg":
        rm = gpr
        v = RNG.getrandbits(64)
        c.inp.append("%s=0x%X" % (GPR[gpr], v))
        b = v.to_bytes(8, "little")
    elif lay == "mr":
        rm = Mem(RSI, mem_off) if mem else dst
    elif mem:
        data = bimg if bimg is not None else rnd_vec(es_src if bcst else ms, kes if not bcst else es_src)
        c.mem[MEM_RSI + mem_off] = data
        rm = Mem(RSI, mem_off)
        n = disp8_n(f, vl, bcst)
        b = data * (vl // es_src) if bcst else data
    else:
        rm = s2
        if s2 not in regs:
            regs[s2] = bimg if bimg is not None else rnd_vec(64, kes)
        b = regs[s2][:ms]
    # destination register image before the instruction
    if lay in ("mr",):
        if not mem and dst not in regs:
            regs[dst] = rb(64)
        src = s1                     # vector source in ModRM.reg
        if src not in regs:
            regs[src] = rnd_vec(64, 2)
        b = regs[src][:vl]
    elif lay in ("kvm", "kvmi", "km"):
        pass
    elif dst not in regs:
        regs[dst] = rb(64)
    if lay == "km":
        regs[s2] = bimg if bimg is not None else rnd_vec(64, kes, 0.5)
        b = regs[s2][:vl]
        rm = s2
    for r, img in regs.items():
        c.set_zmm(r, img)
    a = regs[s1][:vl] if lay in ("rvm", "rvmi", "kvm", "kvmi") else None
    if kreg:
        c.k[kreg] = kval
    kmask = kval if kreg else None
    old = regs.get(dst, bytes(64))
    ctx = Ctx(vl, a, b, old, imm if imm is not None else 0, c.k.get(s2 & 7, 0) if lay == "mk" else 0)
    res = OPS[f["op"]](ctx)
    dvl = vl >> f["narrow"]
    if fault:
        c.fault = fault
    elif lay in ("kvm", "kvmi", "km"):
        r = 0
        for j, bit in enumerate(res):
            if bit and (kmask is None or (kmask >> j) & 1):
                r |= 1 << j
        c.k.setdefault(dst, RNG.getrandbits(64))
        if dst == kreg:
            pass
        c.exp.append("k%d=0x%X" % (dst, r))
    elif lay == "mr" and mem:
        oldm = rb(dvl)
        c.mem[MEM_RSI + mem_off] = oldm
        c.exp.append("m+0x%X=%s" % (MEM_RSI + mem_off, hexs(store_mask(oldm, res, kes, kmask))))
    else:
        merge = None
        if f["op"] == "blendm":
            merge = regs[s1]
            if kmask is None:
                merge = None
        out = apply_mask(old, res, kes, dvl, kmask, z, merge)
        c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    # encoding
    if ll is None:
        ll = VL_LL[vl]
    bb = 1 if bcst else 0
    mmm, pp, opc = f["mmm"], f["pp"], f["opc"]
    if lay in ("rvm", "rvmi", "kvm", "kvmi"):
        c.code = evex(mmm, pp, w, opc, dst, rm, vvvv=s1, ll=ll, b=bb, z=z, aaa=kreg, imm=imm, n=n)
    elif lay == "vmi":
        c.code = evex(mmm, pp, w, opc, f["reg"], rm, vvvv=dst, ll=ll, b=bb, z=z, aaa=kreg, imm=imm, n=n)
    elif lay == "mr":
        c.code = evex(mmm, pp, w, opc, s1, rm, ll=ll, b=bb, z=z, aaa=kreg, n=dvl if mem else 1)
    elif lay == "km":
        c.code = evex(mmm, pp, w, opc, dst, rm, ll=ll, b=bb, z=z, aaa=kreg)
    else:  # rm, rmi, mk, rg
        c.code = evex(mmm, pp, w, opc, dst, rm, ll=ll, b=bb, z=z, aaa=kreg, imm=imm, n=n)
    return c


def gen_forms():
    comment("--- AVX512BW forms x VL 128/256/512 x {no mask, merge, zero} x {register, memory}")
    for f in FORMS:
        lay = f["lay"]
        comment("%s (EVEX.%s.%s.%s %02X%s)" % (f["name"], PP_NAME[f["pp"]], MAP_NAME[f["mmm"]],
                "WIG" if f["w"] is None else "W%d" % f["w"], f["opc"],
                (" /%d" % f["reg"]) if f["reg"] is not None else ""))
        kd = lay in ("kvm", "kvmi", "km")
        regonly = lay in ("km", "mk", "rg")
        for vl in f["vls"]:
            nm = f["mask"] == "none"
            emit(gen_case(f, vl, "reg", dst=1, s1=2, s2=3))
            emit(gen_case(f, vl, "reg", dst=(5 if kd else 17), s1=30, s2=9, kreg=0 if nm else 3,
                          kval=None if nm else RNG.getrandbits(64), title="zmm16+" + ("" if nm else " merge")))
            if f["mask"] == "mz":
                emit(gen_case(f, vl, "reg", dst=4, s1=5, s2=26, kreg=6, kval=RNG.getrandbits(64), z=1,
                              title="zero"))
            if not regonly:
                emit(gen_case(f, vl, "mem", dst=6, s1=8, s2=0))
                if not nm:
                    emit(gen_case(f, vl, "mem", dst=(7 if kd else 7), s1=9, s2=0, kreg=2,
                                  kval=RNG.getrandbits(64), title="masked"))
                if f["bes"]:
                    emit(gen_case(f, vl, "mem", dst=10, s1=11, s2=0, bcst=1, title="{1to%d}" % (vl // f["bes"])))
                    emit(gen_case(f, vl, "mem", dst=12, s1=13, s2=0, bcst=1, kreg=4,
                                  kval=RNG.getrandbits(64), z=1, title="{1toN} zero"))
                if lay == "mr" and f["mask"] == "mz":
                    emit(gen_case(f, vl, "mem", dst=0, s1=20, s2=0, kreg=5, kval=RNG.getrandbits(64),
                                  title="store masked"))
        # aliasing and W-ignored at 512 bits
        mk = f["mask"] != "none"
        if lay in ("rvm", "rvmi"):
            emit(gen_case(f, 64, "reg", dst=5, s1=5, s2=6, kreg=1 if mk else 0,
                          kval=RNG.getrandbits(64) if mk else None, title="dst=src1"))
            emit(gen_case(f, 64, "reg", dst=6, s1=5, s2=6, kreg=1 if mk else 0,
                          kval=RNG.getrandbits(64) if mk else None, z=1 if mk else 0, title="dst=src2 zero"))
            emit(gen_case(f, 64, "reg", dst=7, s1=7, s2=7, title="dst=src1=src2"))
        if lay in ("rm", "rmi", "vmi"):
            emit(gen_case(f, 64, "reg", dst=8, s1=2, s2=8, kreg=2 if mk else 0,
                          kval=RNG.getrandbits(64) if mk else None, title="dst=src"))
        if f["w"] is None:
            emit(gen_case(f, 64, "reg", dst=14, s1=15, s2=16, w=1, title="EVEX.W1 (WIG)"))
        # immediates
        if lay in ("vmi", "rmi", "rvmi", "kvmi"):
            for imm in (0, 1, 7, 8, 15, 16, 17, 31, 32, 0x80, 0xFF, 0x1B, 0xE4):
                emit(gen_case(f, 64, "reg", dst=18, s1=19, s2=20, imm=imm, title="imm=0x%02X" % imm))
        # counts
        if f["op"] in ("sllw", "srlw", "sraw"):
            for cnt in (0, 1, 15, 16, 17, 255, 1 << 32, (1 << 64) - 1):
                img = cnt.to_bytes(8, "little") + rb(56)
                emit(gen_case(f, 64, "reg", dst=21, s1=22, s2=23, bimg=img, title="count=0x%X" % cnt))
            img = (17).to_bytes(8, "little") + rb(8)
            emit(gen_case(f, 64, "mem", dst=21, s1=22, s2=0, bimg=img, kreg=1, kval=RNG.getrandbits(64),
                          title="m128 count=17"))
        if f["op"] in ("sllvw", "srlvw", "sravw"):
            cnts = [0, 1, 15, 16, 17, 0xFFFF, 0x8000, 5] * 4
            emit(gen_case(f, 64, "reg", dst=24, s1=25, s2=27, bimg=P(cnts, 2), title="counts 0..0xFFFF"))
        if f["op"] in ("permw", "permi2w", "permt2w"):
            # index bits above the table select are ignored
            for vl in (16, 32, 64):
                idx = P([RNG.getrandbits(16) | 0xFFC0 for _ in range(vl // 2)], 2) + rb(64 - vl)
                if f["op"] == "permw":
                    emit(gen_case(f, vl, "reg", dst=28, s1=29, s2=31, aimg=idx, title="high index bits"))
                elif f["op"] == "permt2w":
                    emit(gen_case(f, vl, "reg", dst=28, s1=29, s2=31, aimg=idx, kreg=7,
                                  kval=RNG.getrandbits(64), title="high index bits merge"))
                else:
                    c = gen_case(f, vl, "reg", dst=28, s1=29, s2=31, kreg=7, kval=RNG.getrandbits(64),
                                 title="dest index merge")
                    emit(c)
        if f["op"] == "blendm":
            emit(gen_case(f, 64, "reg", dst=3, s1=4, s2=3, kreg=5, kval=RNG.getrandbits(64),
                          title="dst=src2 merge (masked-off = SRC1)"))
            emit(gen_case(f, 64, "reg", dst=3, s1=3, s2=4, kreg=5, kval=0, title="k=0 merge -> SRC1"))
        gen_ud(f)


# ---------------------------------------------------------------------------------------
# #UD matrix per form (SDM Vol2A Tables 2-40 .. 2-43, 2-45 note 4)
# ---------------------------------------------------------------------------------------
def ud_case(f, title, w=None, ll=2, b=0, z=0, aaa=0, vvvv="def", rm=3, reg=None, pp=None, imm="def"):
    lay = f["lay"]
    c = Case("%s %s #UD" % (f["name"], title))
    w = (f["w"] if f["w"] is not None else 0) if w is None else w
    if imm == "def":
        imm = 0x11 if lay in ("vmi", "rmi", "rvmi", "kvmi") else None
    pp = f["pp"] if pp is None else pp
    if lay in ("rvm", "rvmi", "kvm", "kvmi"):
        v = 2 if vvvv == "def" else vvvv
        r = 1 if reg is None else reg
    elif lay == "vmi":
        v = 2 if vvvv == "def" else vvvv
        r = f["reg"]
    else:
        v = None if vvvv == "def" else vvvv
        r = 1 if reg is None else reg
    c.code = evex(f["mmm"], pp, w, f["opc"], r, rm, vvvv=v, ll=ll, b=b, z=z, aaa=aaa, imm=imm,
                  n=disp8_n(f, 16 << ll if ll < 3 else 64, 0))
    c.fault = "#UD"
    emit(c)


def gen_ud(f):
    lay = f["lay"]
    ud_case(f, "L'L=11b", ll=3)
    ud_case(f, "EVEX.b register form", b=1)
    if not f["bes"] and lay not in ("km", "mk", "rg"):
        ud_case(f, "EVEX.b memory (no {1toN})", b=1, rm=Mem(RSI, 0))
    if f["mask"] == "none":
        ud_case(f, "aaa!=0 (no masking)", aaa=1)
    if f["mask"] == "kdest":
        ud_case(f, "z on k destination", z=1, aaa=1)
    if f["mask"] == "mz":
        ud_case(f, "z with aaa=0", z=1)
    if lay == "mr" and f["mask"] == "mz":
        ud_case(f, "z with memory destination", z=1, aaa=1, rm=Mem(RSI, 0))
    # EVEX.W of the other value is #UD unless it selects a sibling form (VMOVDQU8/16,
    # VPCMPB/W, VPTESTMB/W, VPMOVB2M/W2M, VPMOVM2B/W, VPBLENDMB/W; VPERMB/VPERMI2B/VPERMT2B)
    if f["w"] is not None and f["name"] not in WSEL:
        ud_case(f, "EVEX.W%d" % (1 - f["w"]), w=1 - f["w"])
    if lay in ("rm", "rmi", "mr", "km", "mk", "rg"):
        ud_case(f, "vvvv!=1111b", vvvv=5)
    if lay in ("km", "mk", "rg"):
        ud_case(f, "memory operand (register-only form)", rm=Mem(RSI, 0))
    if lay == "km":
        # Table 2-41: ModRM.reg = k register: EVEX.R' = 0 or EVEX.R = 0 #UD
        ud_case(f, "k destination with R'=0", reg=17)
        ud_case(f, "k destination with R=0", reg=9)
    if lay in ("kvm", "kvmi"):
        ud_case(f, "k destination with R'=0", reg=17)


# ---------------------------------------------------------------------------------------
# VPEXTRB/W, VPINSRB/W (E9NF, EVEX.128 only, no masking; AVX512BW without AVX512VL)
# ---------------------------------------------------------------------------------------
def gen_extins():
    comment("--- VPEXTRB/VPEXTRW (0F3A 14/15, 0F C5) and VPINSRB/VPINSRW (0F3A 20, 0F C4): E9NF, 128 only")
    for name, mmm, opc, esz in (("VPEXTRB", 3, 0x14, 1), ("VPEXTRW", 3, 0x15, 2)):
        for imm in (0, 1, 5, 7, 15, 0x1F, 0xFF, 0x83):
            for w in (0, 1):
                c = Case("%s r/m=GPR imm=%d W%d" % (name, imm, w))
                src = rb(64)
                c.set_zmm(9, src)
                c.inp.append("rcx=0xFFFFFFFFFFFFFFFF")
                sel = imm & (15 if esz == 1 else 7)
                v = int.from_bytes(src[sel * esz:sel * esz + esz], "little")
                c.exp.append("rcx=0x%X" % v)
                c.code = evex(mmm, 1, w, opc, 9, 1, ll=0, imm=imm)
                emit(c)
            c = Case("%s m%d imm=%d (disp8*N, N=%d)" % (name, esz * 8, imm, esz))
            src = rb(64)
            c.set_zmm(25, src)
            sel = imm & (15 if esz == 1 else 7)
            old = rb(4)
            c.mem[MEM_RSI - 3 * esz] = old
            c.exp.append("m+0x%X=%s" % (MEM_RSI - 3 * esz, hexs(src[sel * esz:sel * esz + esz] + old[esz:])))
            c.code = evex(mmm, 1, 0, opc, 25, Mem(RSI, -3 * esz), ll=0, imm=imm, n=esz)
            emit(c)
        for d8 in (1, -1, 127, -127):
            c = Case("%s disp8=%d N=%d" % (name, d8, esz))
            src = rb(64)
            c.set_zmm(4, src)
            c.exp.append("m+0x%X=%s" % (MEM_RSI + d8 * esz, hexs(src[2 * esz:3 * esz])))
            c.mem[MEM_RSI + d8 * esz] = bytes(esz)
            c.code = evex(mmm, 1, 0, opc, 4, Mem(RSI, d8 * esz), ll=0, imm=2, n=esz)
            emit(c)
    for imm in (0, 3, 7, 8, 0xFF):
        c = Case("VPEXTRW (0F C5) reg, xmm imm=%d" % imm)
        src = rb(64)
        c.set_zmm(18, src)
        c.inp.append("rdx=0xFFFFFFFFFFFFFFFF")
        sel = imm & 7
        c.exp.append("rdx=0x%X" % int.from_bytes(src[2 * sel:2 * sel + 2], "little"))
        c.code = evex(1, 1, 0, 0xC5, 2, 18, ll=0, imm=imm)
        emit(c)
    for name, mmm, opc, esz in (("VPINSRB", 3, 0x20, 1), ("VPINSRW", 1, 0xC4, 2)):
        for imm in (0, 1, 7, 15, 0x1F, 0xFF):
            for mem in (False, True):
                c = Case("%s %s imm=%d" % (name, "m" if mem else "r32", imm))
                s1 = rb(64)
                c.set_zmm(3, s1)
                c.set_zmm(1, rb(64))
                val = rb(esz)
                if mem:
                    c.mem[MEM_RSI + 2 * esz] = val
                    rm = Mem(RSI, 2 * esz)
                else:
                    c.inp.append("rax=0x%X" % (int.from_bytes(val, "little") | 0xABCDEF0000000000))
                    rm = 0
                sel = imm & (15 if esz == 1 else 7)
                out = bytearray(s1[:16])
                out[sel * esz:sel * esz + esz] = val
                c.exp.append("zmm1=%s" % hexs(bytes(out) + bytes(48)))
                c.code = evex(mmm, 1, 0, opc, 1, rm, vvvv=3, ll=0, imm=imm, n=esz)
                emit(c)
        c = Case("%s dst=src1 zmm16+, W1 (WIG)" % name)
        s1 = rb(64)
        c.set_zmm(20, s1)
        c.inp.append("rbx=0x1234567890ABCDEF")
        sel = 5 if esz == 1 else 5
        out = bytearray(s1[:16])
        out[sel * esz:sel * esz + esz] = (0x1234567890ABCDEF).to_bytes(8, "little")[:esz]
        c.exp.append("zmm20=%s" % hexs(bytes(out) + bytes(48)))
        c.code = evex(mmm, 1, 1, opc, 20, 3, vvvv=20, ll=0, imm=5)
        emit(c)
        for d8 in (1, -1, 127, -127):
            c = Case("%s disp8=%d N=%d" % (name, d8, esz))
            s1 = rb(64)
            c.set_zmm(2, s1)
            c.set_zmm(1, rb(64))
            val = rb(esz)
            c.mem[MEM_RSI + d8 * esz] = val
            out = bytearray(s1[:16])
            out[0:esz] = val
            c.exp.append("zmm1=%s" % hexs(bytes(out) + bytes(48)))
            c.code = evex(mmm, 1, 0, opc, 1, Mem(RSI, d8 * esz), vvvv=2, ll=0, imm=0, n=esz)
            emit(c)
    # #UD: L'L != 00b (Table 2-45 note 4), masking, EVEX.b, vvvv on extracts, C5 with memory
    for name, mmm, opc, ins in (("VPEXTRB", 3, 0x14, 0), ("VPEXTRW", 3, 0x15, 0), ("VPEXTRW C5", 1, 0xC5, 0),
                                ("VPINSRB", 3, 0x20, 1), ("VPINSRW", 1, 0xC4, 1)):
        for ll in (1, 2, 3):
            c = Case("%s L'L=%d #UD" % (name, ll))
            c.code = evex(mmm, 1, 0, opc, 1, 2, vvvv=3 if ins else None, ll=ll, imm=1)
            c.fault = "#UD"
            emit(c)
        c = Case("%s {k1} #UD" % name)
        c.code = evex(mmm, 1, 0, opc, 1, 2, vvvv=3 if ins else None, ll=0, aaa=1, imm=1)
        c.fault = "#UD"
        emit(c)
        c = Case("%s EVEX.b #UD" % name)
        c.code = evex(mmm, 1, 0, opc, 1, Mem(RSI, 0), vvvv=3 if ins else None, ll=0, b=1, imm=1)
        c.fault = "#UD"
        emit(c)
        if not ins:
            c = Case("%s vvvv!=1111b #UD" % name)
            c.code = evex(mmm, 1, 0, opc, 1, 2, vvvv=4, ll=0, imm=1)
            c.fault = "#UD"
            emit(c)
    c = Case("VPEXTRW C5 memory operand #UD")
    c.code = evex(1, 1, 0, 0xC5, 1, Mem(RSI, 0), ll=0, imm=1)
    c.fault = "#UD"
    emit(c)
    c = Case("VPEXTRW C5 GPR destination with R'=0 #UD (Table 2-41)")
    c.code = evex(1, 1, 0, 0xC5, 17, 2, ll=0, imm=1)
    c.fault = "#UD"
    emit(c)


# ---------------------------------------------------------------------------------------
# masked memory: fault suppression per byte / word element, E4NF without it
#   MEM + 0x10000 is unmapped; [rsi + 0x8000 - n] is the last n mapped bytes
# ---------------------------------------------------------------------------------------
END = 0x10000 - MEM_RSI


def fs_load(f, vl, nmapped, kval, title, w=None, z=1):
    """load form with the memory operand starting nmapped bytes before the unmapped page"""
    c = gen_case(f, vl, "mem", dst=2, s1=3, s2=0, kreg=1, kval=kval, z=z, mem_off=END - nmapped,
                 title=title, w=w)
    # only the mapped bytes exist: drop the rest of the input image
    key = MEM_RSI + END - nmapped
    c.mem[key] = c.mem[key][:nmapped]
    return c


def gen_fault_suppression():
    comment("--- masked memory next to the unmapped page MEM+0x10000: fault suppression per byte/word")
    fb = {x["name"]: x for x in FORMS}
    for name, es in (("VMOVDQU8", 1), ("VMOVDQU16", 2)):
        f = fb[name]
        for nmap in (32, 33, 63):
            nel = nmap // es
            k = (1 << nel) - 1
            emit(fs_load(f, 64, nmap, k, "only the %d mapped bytes active" % nmap))
            c = fs_load(f, 64, nmap, k | (1 << nel), "next element active -> #PF")
            c.exp = []
            c.fault = "#PF"
            emit(c)
        # a single active element: the last mapped one / the first unmapped one
        emit(fs_load(f, 64, 32, 1 << (32 // es - 1), "single last mapped element"))
        c = fs_load(f, 64, 32, 1 << (32 // es), "single first unmapped element -> #PF")
        c.exp, c.fault = [], "#PF"
        emit(c)
        # stores
        fs = fb[name + " store"]
        for nmap in (32, 17):
            nel = nmap // es
            k = (1 << nel) - 1
            c = gen_case(fs, 64, "mem", dst=0, s1=4, s2=0, kreg=1, kval=k, mem_off=END - nmap,
                         title="store, only %d mapped bytes active" % nmap)
            key = MEM_RSI + END - nmap
            c.mem[key] = c.mem[key][:nmap]
            c.exp = [e if not e.startswith("m+") else "m+0x%X=%s" % (key, e.split("=")[1][:2 * nmap])
                     for e in c.exp]
            emit(c)
            c = gen_case(fs, 64, "mem", dst=0, s1=4, s2=0, kreg=1, kval=k | (1 << (64 // es - 1)),
                         mem_off=END - nmap, title="store, last element unmapped -> #PF, memory unchanged")
            c.mem[key] = c.mem[key][:nmap]
            c.exp, c.fault = [], "#PF"
            emit(c)
    # E4 arithmetic with a masked memory source
    for name in ("VPADDB", "VPSUBUSW", "VPMAXSB", "VPMULHRSW", "VPBLENDMB"):
        f = fb[name]
        es = f["kes"]
        emit(fs_load(f, 64, 16, (1 << (16 // es)) - 1, "16 mapped bytes active", z=0))
        c = fs_load(f, 64, 16, 1 << (16 // es), "first unmapped element active -> #PF")
        c.exp, c.fault = [], "#PF"
        emit(c)
        c = gen_case(f, 64, "mem", dst=2, s1=3, s2=0, kreg=1, kval=0, mem_off=END, title="k=0 on the unmapped page")
        del c.mem[MEM_RSI + END]
        emit(c)
    # Half Mem load: byte source elements of word destination elements (E5)
    for name in ("VPMOVZXBW", "VPMOVSXBW"):
        f = fb[name]
        emit(fs_load(f, 64, 16, 0xFFFF, "16 source bytes mapped, words 0-15 active"))
        c = fs_load(f, 64, 16, 0x1FFFF, "word 16 active -> #PF")
        c.exp, c.fault = [], "#PF"
        emit(c)
    # narrowing store (E6): byte elements of the half-length destination
    for name in ("VPMOVWB", "VPMOVSWB", "VPMOVUSWB"):
        f = fb[name]
        key = MEM_RSI + END - 16
        c = gen_case(f, 64, "mem", dst=0, s1=6, s2=0, kreg=2, kval=0xFFFF, mem_off=END - 16,
                     title="store 16 mapped bytes")
        c.mem[key] = c.mem[key][:16]
        c.exp = ["m+0x%X=%s" % (key, e.split("=")[1][:32]) if e.startswith("m+") else e for e in c.exp]
        emit(c)
        c = gen_case(f, 64, "mem", dst=0, s1=6, s2=0, kreg=2, kval=0x1FFFF, mem_off=END - 16,
                     title="byte 16 active -> #PF, memory unchanged")
        c.mem[key] = c.mem[key][:16]
        c.exp, c.fault = [], "#PF"
        emit(c)
    # Tuple1 Scalar broadcast: the one element is read when any lane is active
    for name in ("VPBROADCASTB", "VPBROADCASTW"):
        f = fb[name]
        c = gen_case(f, 64, "mem", dst=2, s1=3, s2=0, kreg=1, kval=0, mem_off=END, title="k=0 on the unmapped page")
        del c.mem[MEM_RSI + END]
        emit(c)
        es = f["kes"]
        c = gen_case(f, 64, "mem", dst=2, s1=3, s2=0, kreg=1, kval=1 << (64 // es - 1), mem_off=END,
                     title="last lane active on the unmapped page -> #PF")
        del c.mem[MEM_RSI + END]
        c.exp, c.fault = [], "#PF"
        emit(c)
        if es == 2:
            # k1 bits 63:32 are above KL = 32: no lane active, no access
            c = gen_case(f, 64, "mem", dst=2, s1=3, s2=0, kreg=1, kval=0xFFFFFFFF00000000, mem_off=END,
                         title="only k1 bits above KL set: no access")
            del c.mem[MEM_RSI + END]
            emit(c)
    # E4NF / E9NF: no fault suppression - the whole operand is read whatever the mask
    for name in ("VPSHUFB", "VPUNPCKLBW", "VPACKSSWB", "VPALIGNR", "VPMADDWD", "VPMADDUBSW",
                 "VPSRLW imm", "VPSLLW xmm", "VPSRLDQ", "VPSADBW", "VPERMW", "VPACKSSDW", "VDBPSADBW"):
        f = fb[name]
        nm = f["mask"] == "none"
        size = src_size(f, 64)
        c = gen_case(f, 64, "mem", dst=2, s1=3, s2=0, kreg=0 if nm else 1, kval=None if nm else 1,
                     mem_off=END - size // 2, title="E4NF: half the operand unmapped, k=1 -> #PF")
        c.mem[MEM_RSI + END - size // 2] = c.mem[MEM_RSI + END - size // 2][:size // 2]
        c.exp, c.fault = [], "#PF"
        emit(c)
        if not nm:
            c = gen_case(f, 64, "mem", dst=2, s1=3, s2=0, kreg=1, kval=0, mem_off=END - size // 2,
                         title="E4NF: k=0 still #PF")
            c.mem[MEM_RSI + END - size // 2] = c.mem[MEM_RSI + END - size // 2][:size // 2]
            c.exp, c.fault = [], "#PF"
            emit(c)


# ---------------------------------------------------------------------------------------
# disp8*N for every byte/word tuple (Tables 2-36 / 2-37)
# ---------------------------------------------------------------------------------------
def gen_disp8():
    comment("--- disp8*N: +-1 and +-127 for Full Mem, Full (+{1toN}), Half Mem, Tuple1 Scalar, Mem128 x VL")
    fb = {x["name"]: x for x in FORMS}
    sel = [("VPADDB", 0), ("VMOVDQU16", 0), ("VPACKSSDW", 0), ("VPACKSSDW", 1), ("VPMOVZXBW", 0),
           ("VPMOVWB", 0), ("VPBROADCASTB", 0), ("VPBROADCASTW", 0), ("VPSLLW xmm", 0), ("VPSRLW imm", 0),
           ("VMOVDQU8 store", 0), ("VPCMPEQB", 0)]
    for name, bcst in sel:
        f = fb[name]
        for vl in (16, 32, 64):
            n = disp8_n(f, vl, bcst)
            for d8 in (1, -1, 127, -127):
                c = gen_case(f, vl, "mem", dst=10, s1=11, s2=0, bcst=bcst, mem_off=d8 * n,
                             title="disp8=%d N=%d" % (d8, n))
                emit(c)
    # 16-bit-free SIB form with R14 base and an index
    f = fb["VPADDW"]
    c = gen_case(f, 64, "mem", dst=1, s1=2, s2=0, mem_off=0x80, title="[r14 + rcx*2 + 0x40]")
    c.code = evex(1, 1, 0, 0xFD, 1, Mem(R14, 0x40, index=1, scale=1), vvvv=2, ll=2, n=64)
    c.inp.append("rcx=0x20")
    emit(c)


# ---------------------------------------------------------------------------------------
# 64-bit opmasks with byte elements: KMOVQ / KADDQ / KORTESTQ / KSHIFTRQ around BW compares
# ---------------------------------------------------------------------------------------
def vex3(mmmmm, w, vvvv, l, pp, opc, modrm):
    return bytes([0xC4, 0xE0 | mmmmm, (w << 7) | ((~vvvv & 0xF) << 3) | (l << 2) | pp, opc, modrm])


def kmovq_k_r(k, r):
    return vex3(1, 1, 0, 0, 3, 0x92, 0xC0 | (k << 3) | r)


def kmovq_r_k(r, k):
    return vex3(1, 1, 0, 0, 3, 0x93, 0xC0 | (r << 3) | k)


def kaddq(k1, k2, k3):
    return vex3(1, 1, k2, 1, 0, 0x4A, 0xC0 | (k1 << 3) | k3)


def kortestq(k1, k2):
    return vex3(1, 1, 0, 0, 0, 0x98, 0xC0 | (k1 << 3) | k2)


def kshiftrq(k1, k2, imm):
    return vex3(3, 1, 0, 0, 1, 0x31, 0xC0 | (k1 << 3) | k2) + bytes([imm])


def gen_kmask64():
    comment("--- 64-bit opmasks with 64 byte elements (KMOVQ, KADDQ, KORTESTQ, KSHIFTRQ, VPCMPEQB, VPMOVB2M)")
    for t in range(6):
        c = Case("KMOVQ k1, rax; VPCMPEQB k2{k1}, zmm1, zmm2; KADDQ k3, k2, k2; KMOVQ rbx, k3")
        a = rb(64)
        bl = bytearray(rb(64))
        for j in range(64):
            if RNG.random() < 0.6:
                bl[j] = a[j]
        k1 = RNG.getrandbits(64) if t else 0xFFFFFFFFFFFFFFFF
        c.set_zmm(1, a)
        c.set_zmm(2, bytes(bl))
        c.inp.append("rax=0x%X" % k1)
        eq = sum(1 << j for j in range(64) if a[j] == bl[j]) & k1
        k3 = (eq + eq) & 0xFFFFFFFFFFFFFFFF
        c.code = (kmovq_k_r(1, 0) + evex(1, 1, 0, 0x74, 2, 2, vvvv=1, ll=2, aaa=1) + kaddq(3, 2, 2) +
                  kmovq_r_k(3, 3))
        c.k[2] = RNG.getrandbits(64)
        c.k[3] = RNG.getrandbits(64)
        c.exp += ["k1=0x%X" % k1, "k2=0x%X" % eq, "k3=0x%X" % k3, "rbx=0x%X" % k3]
        emit(c)
    for kv in (0, 0x8000000000000000, 0xFFFFFFFFFFFFFFFF, 0x00000000FFFFFFFF):
        c = Case("VPMOVM2B zmm3, k4; VPMOVB2M k5, zmm3; KSHIFTRQ k6, k5, 32; KORTESTQ k5, k6 (k4=0x%X)" % kv)
        c.k[4] = kv
        c.k[5] = 0x1234
        c.k[6] = 0x5678
        c.set_zmm(3, rb(64))
        img = P([0xFF if (kv >> j) & 1 else 0 for j in range(64)], 1)
        k6 = kv >> 32
        orv = kv | k6
        rfl = 0x202 | (0x40 if orv == 0 else 0) | (0x1 if orv == 0xFFFFFFFFFFFFFFFF else 0)
        c.code = (evex(2, 2, 0, 0x28, 3, 4, ll=2) + evex(2, 2, 0, 0x29, 5, 3, ll=2) + kshiftrq(6, 5, 32) +
                  kortestq(5, 6))
        c.exp += ["zmm3=%s" % hexs(img), "k5=0x%X" % kv, "k6=0x%X" % k6, "rflags=0x%X" % rfl]
        emit(c)
    # VPTESTMW with all 32 word lanes into k, then KMOVQ (bits 63:32 are 0)
    c = Case("VPTESTMW k1, zmm1, zmm1 (32 words) ; KMOVQ rax, k1 - bits 63:32 cleared")
    c.k[1] = 0xFFFFFFFFFFFFFFFF
    v = P([0x8000] * 32, 2)
    c.set_zmm(1, v)
    c.code = evex(2, 1, 1, 0x26, 1, 1, vvvv=1, ll=2) + kmovq_r_k(0, 1)
    c.exp += ["k1=0xFFFFFFFF", "rax=0xFFFFFFFF"]
    emit(c)


# ---------------------------------------------------------------------------------------
# hardware cross-check through the VEX forms the i5-13600K has (emu-alltest hardware cases)
# ---------------------------------------------------------------------------------------
HW_OPS = [
    # mnemonic, model op, layout ('rvm', 'rm', 'rmi', 'rvmi', 'vmi', 'rvx'), element size
    ("vpaddb", "addb", "rvm", 1), ("vpaddw", "addw", "rvm", 2), ("vpaddsb", "addsb", "rvm", 1),
    ("vpaddsw", "addsw", "rvm", 2), ("vpaddusb", "addusb", "rvm", 1), ("vpaddusw", "addusw", "rvm", 2),
    ("vpsubb", "subb", "rvm", 1), ("vpsubw", "subw", "rvm", 2), ("vpsubsb", "subsb", "rvm", 1),
    ("vpsubsw", "subsw", "rvm", 2), ("vpsubusb", "subusb", "rvm", 1), ("vpsubusw", "subusw", "rvm", 2),
    ("vpminsb", "minsb", "rvm", 1), ("vpminsw", "minsw", "rvm", 2), ("vpminub", "minub", "rvm", 1),
    ("vpminuw", "minuw", "rvm", 2), ("vpmaxsb", "maxsb", "rvm", 1), ("vpmaxsw", "maxsw", "rvm", 2),
    ("vpmaxub", "maxub", "rvm", 1), ("vpmaxuw", "maxuw", "rvm", 2), ("vpavgb", "avgb", "rvm", 1),
    ("vpavgw", "avgw", "rvm", 2), ("vpabsb", "absb", "rm", 1), ("vpabsw", "absw", "rm", 2),
    ("vpmullw", "mullw", "rvm", 2), ("vpmulhw", "mulhw", "rvm", 2), ("vpmulhuw", "mulhuw", "rvm", 2),
    ("vpmulhrsw", "mulhrsw", "rvm", 2), ("vpmaddwd", "maddwd", "rvm", 2), ("vpmaddubsw", "maddubsw", "rvm", 1),
    ("vpsadbw", "sadbw", "rvm", 1), ("vpshufb", "pshufb", "rvm", 1), ("vpshufhw", "pshufhw", "rmi", 2),
    ("vpshuflw", "pshuflw", "rmi", 2), ("vpalignr", "palignr", "rvmi", 1),
    ("vpunpcklbw", "punpcklbw", "rvm", 1), ("vpunpckhbw", "punpckhbw", "rvm", 1),
    ("vpunpcklwd", "punpcklwd", "rvm", 2), ("vpunpckhwd", "punpckhwd", "rvm", 2),
    ("vpacksswb", "packsswb", "rvm", 2), ("vpackuswb", "packuswb", "rvm", 2),
    ("vpackssdw", "packssdw", "rvm", 4), ("vpackusdw", "packusdw", "rvm", 4),
    ("vpsrlw", "srliw", "vmi", 2), ("vpsraw", "sraiw", "vmi", 2), ("vpsllw", "slliw", "vmi", 2),
    ("vpsrlw", "srlw", "rvx", 2), ("vpsraw", "sraw", "rvx", 2), ("vpsllw", "sllw", "rvx", 2),
    ("vpsrldq", "psrldq", "vmi", 1), ("vpslldq", "pslldq", "vmi", 1),
    ("vpmovzxbw", "pmovzxbw", "rh", 1), ("vpmovsxbw", "pmovsxbw", "rh", 1),
    ("vpbroadcastb", "bcstb", "rh", 1), ("vpbroadcastw", "bcstw", "rh", 2),
]


def hw_vec(n, esz):
    return rnd_vec(n, esz, 0.4)


def hwcheck_gen(out, expect_path):
    import json
    exp = []
    for mn, op, lay, esz in HW_OPS:
        for vl in (16, 32):
            r = "xmm" if vl == 16 else "ymm"
            for t in range(40):
                a = hw_vec(vl, esz)
                b = hw_vec(vl, esz)
                imm = None
                if lay in ("rmi", "vmi", "rvmi"):
                    imm = [0, 1, 7, 8, 15, 16, 17, 31, 32, 0xFF, 0x1B, 0xE4][t % 12] if t < 24 else RNG.getrandbits(8)
                if lay == "rvx":
                    cnt = [0, 1, 7, 15, 16, 17, 255, 1 << 32][t % 8] if t < 16 else RNG.getrandbits(5)
                    b = cnt.to_bytes(8, "little") + rb(8) + bytes(vl - 16)
                d0 = rb(vl)
                if lay == "rvm" or lay == "rvx":
                    asm = "%s %s0, %s1, %s2" % (mn, r, r, "xmm" if lay == "rvx" else r)
                elif lay == "rvmi":
                    asm = "%s %s0, %s1, %s2, %d" % (mn, r, r, r, imm)
                elif lay in ("rmi",):
                    asm = "%s %s0, %s2, %d" % (mn, r, r, imm)
                elif lay == "vmi":
                    asm = "%s %s0, %s2, %d" % (mn, r, r, imm)
                elif lay == "rh":
                    asm = "%s %s0, xmm2" % (mn, r)
                else:
                    asm = "%s %s0, %s2" % (mn, r, r)
                ctx = Ctx(vl, a, b, d0 + bytes(64 - vl), imm or 0)
                if lay == "rh":
                    ctx.b = b[:16]
                res = OPS[op](ctx)
                ins = ["xmm0=%s" % hexs(d0[:16]), "xmm1=%s" % hexs(a[:16]), "xmm2=%s" % hexs(b[:16])]
                if vl == 32:
                    ins += ["ymmh0=%s" % hexs(d0[16:]), "ymmh1=%s" % hexs(a[16:]), "ymmh2=%s" % hexs(b[16:])]
                out.write("%s | %s\n" % (asm, " ".join(ins)))
                exp.append({"xmm0": hexs(res[:16]), "ymmh0": hexs(res[16:32]) if vl == 32 else hexs(bytes(16)),
                            "in_xmm0": hexs(d0[:16]), "in_ymmh0": hexs(d0[16:32]) if vl == 32 else hexs(bytes(16))})
    json.dump(exp, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    import json
    import re
    exp = json.load(open(expect_path))
    cur, bad, seen, ucbad = None, 0, 0, 0
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] (SAME|DIFF) (.*)$", line)
        if m:
            cur = int(m.group(1))
            text = m.group(3)
            continue
        m = re.match(r"^\s+(hw|uc):\s*(.*)$", line)
        if m and cur is not None:
            who, fields = m.group(1), m.group(2)
            fault = -1
            fm = re.match(r"fault #(\d+)\s*(.*)$", fields)
            if fm:
                fault, fields = int(fm.group(1)), fm.group(2)
            kv = dict(x.split("=", 1) for x in fields.split() if "=" in x)
            e = exp[cur]
            gx = kv.get("xmm0", e["in_xmm0"]).upper()
            gy = kv.get("ymmh0", e["in_ymmh0"]).upper()
            ok = fault == -1 and gx == e["xmm0"].upper() and gy == e["ymmh0"].upper()
            if who == "hw":
                seen += 1
                if not ok:
                    bad += 1
                    if bad <= 20:
                        print("[%d] %s: hw xmm0=%s ymmh0=%s fault=%d | model xmm0=%s ymmh0=%s"
                              % (cur, text, gx, gy, fault, e["xmm0"], e["ymmh0"]))
            else:
                if not ok:
                    ucbad += 1
                    if ucbad <= 20:
                        print("[%d] %s: unicorn xmm0=%s ymmh0=%s | model xmm0=%s ymmh0=%s"
                              % (cur, text, gx, gy, e["xmm0"], e["ymmh0"]))
                cur = None
    print("hwcheck: %d cases compared, %d differ from the model (hardware), %d (Unicorn)" % (seen, bad, ucbad))
    return bad == 0 and ucbad == 0


# ---------------------------------------------------------------------------------------
# self test (hand-derived values from the SDM pseudocode)
# ---------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    w16 = lambda *v: P(list(v) + [0] * (8 - len(v)), 2)
    c = Ctx(16, w16(0x4000, 0x8000, 0x7FFF, 0xFFFF), w16(0x4000, 0x8000, 0x7FFF, 0x0001))
    # 0.5 * 0.5 = 0.25; (-1)(-1) = +1 -> 0x8000 (wraps); 0x7FFF^2 >> 15 rounded; -1 * 1 -> 0
    chk("pmulhrsw", E(OPS["mulhrsw"](c), 2)[:4], [0x2000, 0x8000, 0x7FFE, 0x0000])
    chk("pmulhw", E(OPS["mulhw"](c), 2)[:4], [0x1000, 0x4000, 0x3FFF, 0xFFFF])
    c = Ctx(16, bytes([0xFF, 0xFF] + [0] * 14), bytes([0x7F, 0x7F] + [0] * 14))
    chk("pmaddubsw sat", E(OPS["maddubsw"](c), 2)[0], 0x7FFF)
    c = Ctx(16, bytes([0xFF, 0xFF] + [0] * 14), bytes([0x80, 0x80] + [0] * 14))
    chk("pmaddubsw neg sat", E(OPS["maddubsw"](c), 2)[0], 0x8000)
    c = Ctx(16, w16(0x8000, 0x8000), w16(0x8000, 0x8000))
    chk("pmaddwd wrap", E(OPS["maddwd"](c), 4)[0], 0x80000000)
    c = Ctx(16, w16(0x0100, 0xFF00, 0x0080, 0xFFFF), w16(0))
    chk("packsswb", OPS["packsswb"](c)[:4], bytes([0x7F, 0x80, 0x7F, 0xFF]))
    chk("packuswb", OPS["packuswb"](c)[:4], bytes([0xFF, 0x00, 0x80, 0x00]))
    chk("pmovuswb unsigned", OPS["pmovuswb"](Ctx(16, None, w16(0xFFFF, 0x0100, 0x00FF)))[:3], bytes([0xFF, 0xFF, 0xFF]))
    chk("pmovswb", OPS["pmovswb"](Ctx(16, None, w16(0xFFFF, 0x0100, 0x8000)))[:3], bytes([0xFF, 0x7F, 0x80]))
    a = bytes(range(16))
    chk("palignr 4", OPS["palignr"](Ctx(16, a, bytes(range(16, 32)), imm=4))[:2], bytes([20, 21]))
    chk("palignr 20", OPS["palignr"](Ctx(16, a, bytes(range(16, 32)), imm=20))[:2], bytes([4, 5]))
    chk("palignr 31", OPS["palignr"](Ctx(16, a, bytes(range(16, 32)), imm=31))[:2], bytes([15, 0]))
    chk("pshufb", OPS["pshufb"](Ctx(16, a, bytes([0x80, 0x0F, 0x11] + [0] * 13)))[:3], bytes([0, 15, 1]))
    # VDBPSADBW imm=0: TMP1 = dword 0 of SRC2 in every dword; SRC1 = 0 -> sums of the bytes
    s2 = bytes([1, 2, 3, 4] + [0] * 12)
    chk("dbpsadbw", E(OPS["dbpsadbw"](Ctx(16, bytes(16), s2, imm=0)), 2)[:4], [10, 9 + 1, 7 + 1 + 2, 4 + 1 + 2 + 3])
    idx = w16(0, 8, 15, 7)
    chk("vpermi2w", E(OPS["permi2w"](Ctx(16, w16(10, 11, 12, 13, 14, 15, 16, 17), w16(20, 21, 22, 23, 24, 25, 26, 27),
                                        d=idx + bytes(48))), 2)[:4], [10, 20, 27, 17])
    chk("sraw count>15", E(OPS["sraw"](Ctx(16, w16(0x8000, 0x7FFF), (99).to_bytes(16, "little"))), 2)[:2], [0xFFFF, 0])
    chk("srlvw", E(OPS["srlvw"](Ctx(16, w16(0x8000, 0x8000, 0x8000), w16(15, 16, 0xFFFF))), 2)[:3], [1, 0, 0])
    chk("pslldq 17", OPS["pslldq"](Ctx(16, None, a, imm=17)), bytes(16))
    chk("psrldq 3", OPS["psrldq"](Ctx(16, None, a, imm=3))[:2], bytes([3, 4]))
    chk("pshufhw", E(OPS["pshufhw"](Ctx(16, None, w16(0, 1, 2, 3, 4, 5, 6, 7), imm=0x1B)), 2), [0, 1, 2, 3, 7, 6, 5, 4])
    # encodings: VPADDB zmm1, zmm2, zmm3 = 62 F1 6D 48 FC CB
    chk("enc vpaddb", evex(1, 1, 0, 0xFC, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF1, 0x6D, 0x48, 0xFC, 0xCB]))
    # VMOVDQU8 zmm1{k1}{z}, [rsi+0x40] = 62 F1 7F C9 6F 4E 01 (disp8*64)
    chk("enc vmovdqu8", evex(1, 3, 0, 0x6F, 1, Mem(RSI, 0x40), ll=2, z=1, aaa=1, n=64),
        bytes([0x62, 0xF1, 0x7F, 0xC9, 0x6F, 0x4E, 0x01]))
    chk("kmovq k1, rax", kmovq_k_r(1, 0), bytes([0xC4, 0xE1, 0xFB, 0x92, 0xC8]))
    chk("kaddq k3, k2, k2", kaddq(3, 2, 2), bytes([0xC4, 0xE1, 0xEC, 0x4A, 0xDA]))
    return ok


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
        gen_forms()
        gen_extins()
        gen_fault_suppression()
        gen_disp8()
        gen_kmask64()
        out = sys.stdout
        out.write("# AVX512BW (ledger U260-U269): expected values from the independent SDM model\n")
        out.write("# Emulator/tools/isa/ref_evex_m3_bw.py --cases (regenerate, do not edit). The i5-13600K has no\n")
        out.write("# AVX-512: expected-value cases only, run with AVX-512 enabled:\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m3_bw.txt --avx512 --expect-only\n")
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
