#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_opmask.py -- independent reference model (Python 3 stdlib only)

Literal transcription of the Intel SDM Vol. 2A "Operation" pseudocode and
encoding tables of the VEX-encoded opmask instructions (EVEX milestone K,
ledger U127-U133):

  KAND KANDN KOR KXOR KXNOR KADD  W/B/Q/D   VEX.L1 0F 41/42/45/47/46/4A  RVR
  KUNPCKBW KUNPCKWD KUNPCKDQ                 VEX.L1 0F 4B                 RVR
  KNOT                            W/B/Q/D   VEX.L0 0F 44                 RR
  KORTEST KTEST                   W/B/Q/D   VEX.L0 0F 98/99              RR
  KMOV k, k/m                     W/B/Q/D   VEX.L0 0F 90                 RM
  KMOV m, k                       W/B/Q/D   VEX.L0 0F 91                 MR
  KMOV k, r32/r64                 W/B/D/Q   VEX.L0 0F 92                 RR
  KMOV r32/r64, k                 W/B/D/Q   VEX.L0 0F 93                 RR
  KSHIFTR / KSHIFTL               W/B/Q/D   VEX.L0 66 0F3A 30-31 / 32-33 RRI

Width from pp and W (0F map): NP.W0 = W, 66.W0 = B, NP.W1 = Q, 66.W1 = D;
KMOV 92/93 use F2 for D (W0) / Q (W1, 64-bit mode only: "VEX.W can only be
used to modify the size of the GPR operand in 64b mode", so F2.W1 is KMOVD
outside 64-bit mode); KSHIFT (66 0F3A) uses W1 = W/Q, W0 = B/D.

CPUID: B forms AVX512DQ; W forms AVX512F except KADDW/KTESTW (AVX512DQ);
D/Q forms AVX512BW; KUNPCKBW AVX512F, KUNPCKWD/DQ AVX512BW.

Exceptions (SDM Vol2A 2.9, Tables 2-65 K20 / 2-66 K21, 2-39, 2-41, 2-42):
  #UD  feature flag 0; CR4.OSXSAVE = 0; XCR0[7:5,1:0] != 111b,11b (111xxx11b);
       LOCK/66/F2/F3/REX before VEX; ModRM.mod != 11b where memory is not
       allowed (K20; KMOV m, k needs mod != 11b); VEX.L other than the form's;
       pp/W combinations the SDM does not list;
       Table 2-41: ModRM.reg encodes a k register and VEX.R = 0 (inverted bit,
       64-bit mode); vvvv not used and != 1111b;
       Table 2-42: vvvv encodes a k register and vvvv = 0xxxb (64-bit mode;
       ignored elsewhere); VEX.B is ignored when r/m encodes a k register, and
       outside 64-bit mode (SDM 2.3.5.4).
  #NM  CR0.TS = 1 (after every #UD condition).
All results are zero-extended to MAX_KL = 64 bits.

Usage:
  python ref_opmask.py --selftest   hand-derived checks of the model, exit 0 on pass
  python ref_opmask.py --cases      Emulator/data/cases_opmask.txt (stdout)
  python ref_opmask.py --cinc       unicorn/tests/unit/x86_opmask_vectors.inc (stdout)

Written from the SDM text only (not from any C implementation).
"""

import sys

MAX_KL = 64
M64 = (1 << 64) - 1

# flags (EFLAGS bit masks)
CF, PF, AF, ZF, SF, OF = 0x001, 0x004, 0x010, 0x040, 0x080, 0x800

FEAT_F, FEAT_DQ, FEAT_BW = 1, 2, 4
FEAT_NAME = {FEAT_F: "AVX512F", FEAT_DQ: "AVX512DQ", FEAT_BW: "AVX512BW"}


def zx(x, bits):
    """ZeroExtension(x[bits-1:0]) to MAX_KL."""
    return x & ((1 << bits) - 1)


# --------------------------------------------------------------------------
# Operation (SDM Vol2A KADD..KXOR), operand values are full 64-bit k values
# --------------------------------------------------------------------------

def KAND(w, src1, src2):
    return zx(zx(src1, w) & zx(src2, w), w)


def KANDN(w, src1, src2):
    return zx((~zx(src1, w)) & zx(src2, w), w)


def KOR(w, src1, src2):
    return zx(zx(src1, w) | zx(src2, w), w)


def KXOR(w, src1, src2):
    return zx(zx(src1, w) ^ zx(src2, w), w)


def KXNOR(w, src1, src2):
    return zx(~(zx(src1, w) ^ zx(src2, w)), w)


def KADD(w, src1, src2):
    return zx(zx(src1, w) + zx(src2, w), w)


def KUNPCK(win, src1, src2):
    """KUNPCKBW (win 8) / WD (16) / DQ (32): DEST[win-1:0] := SRC2[win-1:0],
    DEST[2win-1:win] := SRC1[win-1:0], DEST[MAX_KL-1:2win] := 0."""
    return zx(src2, win) | (zx(src1, win) << win)


def KNOT(w, src):
    return zx(~zx(src, w), w)


def KORTEST(w, dest, src):
    """returns (ZF, CF); OF, SF, AF, PF := 0."""
    tmp = zx(dest, w) | zx(src, w)
    return (1 if tmp == 0 else 0, 1 if tmp == (1 << w) - 1 else 0)


def KTEST(w, src1, src2):
    """returns (ZF, CF): ZF from SRC2 AND SRC1, CF from SRC2 AND NOT SRC1."""
    zf = 1 if (zx(src2, w) & zx(src1, w)) == 0 else 0
    cf = 1 if (zx(src2, w) & zx(~src1, w)) == 0 else 0
    return (zf, cf)


def KSHIFTL(w, src, imm8):
    count = imm8 & 0xFF
    dest = 0
    if count <= w - 1:
        dest = zx(zx(src, w) << count, w)
    return dest


def KSHIFTR(w, src, imm8):
    count = imm8 & 0xFF
    dest = 0
    if count <= w - 1:
        dest = zx(src, w) >> count
    return dest


def flags_after_test(rflags_in, zf, cf):
    return (rflags_in & ~(CF | PF | AF | ZF | SF | OF)) | (ZF if zf else 0) | (CF if cf else 0)


# --------------------------------------------------------------------------
# forms (SDM opcode tables)
# --------------------------------------------------------------------------

PP = {"NP": 0, "66": 1, "F3": 2, "F2": 3}


class Form:
    def __init__(self, mnem, kind, map_, op, pp, W, L, width, feat, fn=None):
        self.mnem, self.kind, self.map, self.op, self.pp, self.W, self.L = mnem, kind, map_, op, pp, W, L
        self.width, self.feat, self.fn = width, feat, fn


def _wforms(base, kind, op, L, fn, featW=FEAT_F):
    """NP.W0 = W, 66.W0 = B, NP.W1 = Q, 66.W1 = D."""
    return [
        Form(base + "W", kind, 1, op, "NP", 0, L, 16, featW, fn),
        Form(base + "B", kind, 1, op, "66", 0, L, 8, FEAT_DQ, fn),
        Form(base + "Q", kind, 1, op, "NP", 1, L, 64, FEAT_BW, fn),
        Form(base + "D", kind, 1, op, "66", 1, L, 32, FEAT_BW, fn),
    ]


FORMS = []
FORMS += _wforms("KAND", "logic3", 0x41, 1, KAND)
FORMS += _wforms("KANDN", "logic3", 0x42, 1, KANDN)
FORMS += _wforms("KOR", "logic3", 0x45, 1, KOR)
FORMS += _wforms("KXNOR", "logic3", 0x46, 1, KXNOR)
FORMS += _wforms("KXOR", "logic3", 0x47, 1, KXOR)
FORMS += _wforms("KADD", "logic3", 0x4A, 1, KADD, featW=FEAT_DQ)
FORMS += [
    Form("KUNPCKBW", "unpck", 1, 0x4B, "66", 0, 1, 8, FEAT_F),
    Form("KUNPCKWD", "unpck", 1, 0x4B, "NP", 0, 1, 16, FEAT_BW),
    Form("KUNPCKDQ", "unpck", 1, 0x4B, "NP", 1, 1, 32, FEAT_BW),
]
FORMS += _wforms("KNOT", "not", 0x44, 0, KNOT)
FORMS += _wforms("KORTEST", "kortest", 0x98, 0, KORTEST)
FORMS += _wforms("KTEST", "ktest", 0x99, 0, KTEST, featW=FEAT_DQ)
FORMS += _wforms("KMOV", "kmov_kkm", 0x90, 0, None)
FORMS += _wforms("KMOV", "kmov_mk", 0x91, 0, None)
FORMS += [
    Form("KMOVW", "kmov_kr", 1, 0x92, "NP", 0, 0, 16, FEAT_F),
    Form("KMOVB", "kmov_kr", 1, 0x92, "66", 0, 0, 8, FEAT_DQ),
    Form("KMOVD", "kmov_kr", 1, 0x92, "F2", 0, 0, 32, FEAT_BW),
    Form("KMOVQ", "kmov_kr", 1, 0x92, "F2", 1, 0, 64, FEAT_BW),
    Form("KMOVW", "kmov_rk", 1, 0x93, "NP", 0, 0, 16, FEAT_F),
    Form("KMOVB", "kmov_rk", 1, 0x93, "66", 0, 0, 8, FEAT_DQ),
    Form("KMOVD", "kmov_rk", 1, 0x93, "F2", 0, 0, 32, FEAT_BW),
    Form("KMOVQ", "kmov_rk", 1, 0x93, "F2", 1, 0, 64, FEAT_BW),
]
FORMS += [
    Form("KSHIFTRW", "kshiftr", 3, 0x30, "66", 1, 0, 16, FEAT_F, KSHIFTR),
    Form("KSHIFTRB", "kshiftr", 3, 0x30, "66", 0, 0, 8, FEAT_DQ, KSHIFTR),
    Form("KSHIFTRQ", "kshiftr", 3, 0x31, "66", 1, 0, 64, FEAT_BW, KSHIFTR),
    Form("KSHIFTRD", "kshiftr", 3, 0x31, "66", 0, 0, 32, FEAT_BW, KSHIFTR),
    Form("KSHIFTLW", "kshiftl", 3, 0x32, "66", 1, 0, 16, FEAT_F, KSHIFTL),
    Form("KSHIFTLB", "kshiftl", 3, 0x32, "66", 0, 0, 8, FEAT_DQ, KSHIFTL),
    Form("KSHIFTLQ", "kshiftl", 3, 0x33, "66", 1, 0, 64, FEAT_BW, KSHIFTL),
    Form("KSHIFTLD", "kshiftl", 3, 0x33, "66", 0, 0, 32, FEAT_BW, KSHIFTL),
]

WSUF = {8: "B", 16: "W", 32: "D", 64: "Q"}


def form_name(f):
    if f.kind.startswith("kmov"):
        return "KMOV" + WSUF[f.width]
    return f.mnem


# --------------------------------------------------------------------------
# encoder
# --------------------------------------------------------------------------

def kv(n):
    """vvvv field (inverted) for register n."""
    return (~n) & 15


def vex(f, modrm, rbar=1, xbar=1, bbar=1, vvvv=0b1111, L=None, W=None, pp=None, c4=False, tail=b""):
    L = f.L if L is None else L
    W = f.W if W is None else W
    pp = PP[f.pp] if pp is None else PP[pp]
    if f.map == 1 and W == 0 and xbar == 1 and bbar == 1 and not c4:
        return bytes([0xC5, (rbar << 7) | (vvvv << 3) | (L << 2) | pp, f.op, modrm]) + tail
    return bytes([0xC4, (rbar << 7) | (xbar << 6) | (bbar << 5) | f.map,
                  (W << 7) | (vvvv << 3) | (L << 2) | pp, f.op, modrm]) + tail


def modrm(mod, reg, rm):
    return (mod << 6) | ((reg & 7) << 3) | (rm & 7)


# --------------------------------------------------------------------------
# vectors
# --------------------------------------------------------------------------

GPRS = ("rax", "r9", "r10")        # the GPRs the vectors read/write (64-bit mode)
FLAGS_SET = 0xAD7                  # IF + bit 1 + OF SF ZF AF PF CF
FLAGS_CLR = 0x602                  # IF + bit 1 + DF, the six flags clear
MEM_LEN = 16                       # bytes at RSI (ModRM 0E = [rsi], 4E 08 = [rsi+8])


class Vec:
    def __init__(self, name, f, code, mode=64):
        self.name, self.form, self.code, self.mode = name, f, code, mode
        self.k = [0] * 8
        self.g = {r: 0 for r in GPRS}
        self.flags = FLAGS_SET
        self.mem = bytes(range(0xA0, 0xA0 + MEM_LEN))
        self.ek = None
        self.eg = None
        self.eflags = None
        self.emem = None
        self.feat = f.feat

    def expect(self, k=None, g=None, flags=None, mem=None):
        self.ek = list(self.k)
        self.eg = dict(self.g)
        self.eflags = self.flags
        self.emem = self.mem
        if k:
            for i, v in k.items():
                self.ek[i] = v & M64
        if g:
            for r, v in g.items():
                self.eg[r] = v & M64
        if flags is not None:
            self.eflags = flags
        if mem is not None:
            self.emem = mem
        return self


# k register patterns: bits at and above every width boundary
KVALS = [
    0x0000000000000000, 0xFFFFFFFFFFFFFFFF, 0x8000000180018081, 0x0123456789ABCDEF,
    0xFEDCBA9876543210, 0x00000000000000FF, 0xAAAAAAAA55555555, 0x7FFFFFFF80008080,
    0xFFFFFFFF0000FFFF, 0x00FF00FF00FF00FF, 0x5A5A5A5A5A5A5A5A, 0xFFFFFFFFFFFFFF7F,
]


def _pairs(width):
    """(src1, src2) pairs: rotations of KVALS plus width edge cases."""
    p = [(KVALS[i], KVALS[(i * 5 + 3) % len(KVALS)]) for i in range(8)]
    m = (1 << width) - 1
    top = 1 << (width - 1)
    junk = M64 ^ m                      # every bit above the width
    p += [
        (m | junk, 1 | junk),           # carry out of the width (KADD wraps)
        (top, top),
        (junk, junk),                   # nothing inside the width
        (m, 0),
        (0, m),
    ]
    return p


def gen_vectors():
    vecs = []
    uid = [0]

    def nm(f, tag):
        uid[0] += 1
        return "%s_%s" % (form_name(f).lower(), tag)

    for f in FORMS:
        w = f.width
        if f.kind in ("logic3", "unpck"):
            fn = f.fn if f.kind == "logic3" else (lambda _w, a, b, win=w: KUNPCK(win, a, b))
            for i, (a, b) in enumerate(_pairs(w)):
                # dest k1, src1 (vvvv) k2, src2 (r/m) k3; vary the registers on some
                d, s1, s2 = 1, 2, 3
                variant = i % 4
                bbar = xbar = 1
                if variant == 1:
                    d, s1, s2 = 7, 0, 5         # k0 is an ordinary operand here
                elif variant == 2:
                    d, s1, s2 = 4, 6, 6         # both sources the same register
                    a = b
                elif variant == 3:
                    bbar = xbar = 0             # VEX.B/X ignored for a k register r/m
                v = Vec(nm(f, "%d" % i), f, vex(f, modrm(3, d, s2), vvvv=kv(s1), bbar=bbar, xbar=xbar))
                v.k = [0x1111111111111111 * (j + 1) & M64 for j in range(8)]
                v.k[s1] = a
                v.k[s2] = b
                v.expect(k={d: fn(w, v.k[s1], v.k[s2])})
                vecs.append(v)
            # destination is also a source (k1 = op(k1, k1) and k1 = op(k1, k2))
            v = Vec(nm(f, "alias"), f, vex(f, modrm(3, 1, 2), vvvv=kv(1)))
            v.k[1], v.k[2] = 0x0F0F0F0F0F0F0F0F ^ 0xC3, 0x3C3C3C3C3C3C3C3C
            v.expect(k={1: fn(w, v.k[1], v.k[2])})
            vecs.append(v)
        elif f.kind == "not":
            for i, a in enumerate(KVALS[:8] + [(1 << w) - 1, M64 ^ ((1 << w) - 1)]):
                bbar = 0 if i % 3 == 2 else 1
                d, s = (1, 3) if i % 2 == 0 else (6, 6)
                v = Vec(nm(f, "%d" % i), f, vex(f, modrm(3, d, s), bbar=bbar))
                v.k[s] = a
                v.k[d if d != s else 0] = 0xDEADBEEFDEADBEEF
                v.expect(k={d: KNOT(w, a)})
                vecs.append(v)
        elif f.kind in ("kortest", "ktest"):
            m = (1 << w) - 1
            junk = M64 ^ m
            tests = [(0, 0), (junk, junk), (m, 0), (0, m), (m | junk, m), (1, 1), (1, 2), (m ^ 1, 1),
                     (KVALS[3], KVALS[4]), (KVALS[6], ~KVALS[6] & M64), (KVALS[2], KVALS[7]), (top_bit(w), m)]
            for i, (a, b) in enumerate(tests):
                fl = FLAGS_SET if i % 2 == 0 else FLAGS_CLR
                r, s = (1, 2) if i % 3 else (5, 5)
                if r == s:
                    b = a
                v = Vec(nm(f, "%d" % i), f, vex(f, modrm(3, r, s), bbar=0 if i % 4 == 3 else 1))
                v.flags = fl
                v.k[r], v.k[s] = a, b
                zf, cf = (KORTEST if f.kind == "kortest" else KTEST)(w, v.k[r], v.k[s])
                v.expect(flags=flags_after_test(fl, zf, cf))
                vecs.append(v)
        elif f.kind in ("kshiftl", "kshiftr"):
            for i, cnt in enumerate([0, 1, 3, w - 1, w, w + 1, 0x3F, 0x40, 0x7F, 0x80, 0xFF, w // 2]):
                a = KVALS[(i + 2) % len(KVALS)] | (1 << (w - 1))
                d, s = (1, 2) if i % 2 == 0 else (3, 3)
                v = Vec(nm(f, "%d_c%d" % (i, cnt)), f, vex(f, modrm(3, d, s), tail=bytes([cnt])))
                v.k[s] = a
                if d != s:
                    v.k[d] = 0xDEADBEEFDEADBEEF
                v.expect(k={d: f.fn(w, a, cnt)})
                vecs.append(v)
        elif f.kind == "kmov_kkm":
            # register form, with VEX.B = 0 (ignored) on one
            for i, a in enumerate([KVALS[3], KVALS[4], M64, KVALS[7]]):
                v = Vec(nm(f, "kk%d" % i), f, vex(f, modrm(3, 1, 3), bbar=0 if i == 1 else 1))
                v.k[1], v.k[3] = 0xDEADBEEFDEADBEEF, a
                v.expect(k={1: zx(a, w)})
                vecs.append(v)
            # memory forms: exactly w bits are read
            v = Vec(nm(f, "m"), f, vex(f, modrm(0, 2, 6)))
            v.k[2] = M64
            v.mem = bytes([0x81, 0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8]) + bytes(range(0x10, 0x18))
            v.expect(k={2: int.from_bytes(v.mem[:w // 8], "little")})
            vecs.append(v)
            v = Vec(nm(f, "mdisp8"), f, vex(f, modrm(1, 5, 6), tail=b"\x08"))
            v.k[5] = 0x0123456789ABCDEF
            v.mem = bytes(range(0xF0, 0xF8)) + bytes([0xFF, 0x7F, 0x80, 0x01, 0x55, 0xAA, 0x33, 0xCC])
            v.expect(k={5: int.from_bytes(v.mem[8:8 + w // 8], "little")})
            vecs.append(v)
        elif f.kind == "kmov_mk":
            for i, (a, disp) in enumerate([(0x0123456789ABCDEF, 0), (0xFEDCBA9876543210, 8)]):
                mr = modrm(0, 4, 6) if disp == 0 else modrm(1, 4, 6)
                v = Vec(nm(f, "m%d" % i), f, vex(f, mr, tail=b"" if disp == 0 else bytes([disp])))
                v.k[4] = a
                v.mem = bytes([0xEE] * MEM_LEN)
                mem = bytearray(v.mem)
                mem[disp:disp + w // 8] = zx(a, w).to_bytes(w // 8, "little")
                v.expect(mem=bytes(mem))
                vecs.append(v)
        elif f.kind == "kmov_kr":
            # KMOV k1, r32/r64: rax, and r9 (VEX.B = 0 extends r/m to r8-r15)
            for i, (src, val) in enumerate([("rax", 0xFFFFFFFF12348765), ("r9", 0x8877665544332211),
                                            ("rax", 0x00000000800000FF)]):
                bbar = 0 if src == "r9" else 1
                v = Vec(nm(f, "%s%d" % (src, i)), f, vex(f, modrm(3, 3, 1 if src == "r9" else 0), bbar=bbar))
                v.k[3] = 0xDEADBEEFDEADBEEF
                v.g[src] = val
                v.expect(k={3: zx(val, w)})
                vecs.append(v)
        elif f.kind == "kmov_rk":
            # KMOV r32/r64, k1: rax, and r10 (VEX.R = 0 extends reg to r8-r15); GPR zero-extended
            for i, (dst, val) in enumerate([("rax", 0xFEDCBA9876543210), ("r10", 0x8000000180018081),
                                            ("rax", 0xFFFFFFFFFFFFFFFF)]):
                rbar = 0 if dst == "r10" else 1
                v = Vec(nm(f, "%s%d" % (dst, i)), f, vex(f, modrm(3, 2 if dst == "r10" else 0, 6), rbar=rbar))
                v.k[6] = val
                v.g[dst] = 0x5555555555555555
                v.expect(g={dst: zx(val, w)})
                vecs.append(v)
    return vecs


def top_bit(w):
    return 1 << (w - 1)


def find_form(mnem, kind=None):
    for f in FORMS:
        if f.mnem == mnem and (kind is None or f.kind == kind):
            return f
    raise KeyError(mnem)


def gen_vectors32():
    """32-bit protected mode (CS.D = 1) vectors: rules that differ outside 64-bit mode."""
    vecs = []
    # vvvv bit 3 (raw 0) ignored for a k register vvvv operand (Table 2-42)
    for mn in ("KANDW", "KADDD", "KUNPCKBW"):
        f = find_form(mn)
        for s1 in (2, 6):
            v = Vec("%s_32_vvvv3_k%d" % (mn.lower(), s1), f,
                    vex(f, modrm(3, 1, 3), vvvv=kv(s1) & 7, c4=True), mode=32)
            v.k[s1], v.k[3] = 0x0123456789ABCDEF, 0xF0F0F0F0FFFF00FF
            fn = f.fn if f.kind == "logic3" else (lambda _w, a, b: KUNPCK(8, a, b))
            v.expect(k={1: fn(f.width, v.k[s1], v.k[3])})
            vecs.append(v)
    # KMOV k, r32 / r32, k: F2.W1 is KMOVD outside 64-bit mode; VEX.B ignored there
    for kind in ("kmov_kr", "kmov_rk"):
        for W in (0, 1):
            fd = [f for f in FORMS if f.kind == kind and f.pp == "F2" and f.W == W][0]
            if kind == "kmov_kr":
                for bbar in (1, 0):
                    v = Vec("kmovd_32_k_r_w%d_b%d" % (W, bbar), fd, vex(fd, modrm(3, 3, 0), bbar=bbar), mode=32)
                    v.feat = FEAT_BW
                    v.g["rax"] = 0x12348765
                    v.k[3] = 0xDEADBEEFDEADBEEF
                    v.expect(k={3: 0x12348765})
                    vecs.append(v)
            else:
                v = Vec("kmovd_32_r_k_w%d" % W, fd, vex(fd, modrm(3, 0, 6)), mode=32)
                v.feat = FEAT_BW
                v.k[6] = 0xFEDCBA9876543210
                v.g["rax"] = 0x55555555
                v.expect(g={"rax": 0x76543210})
                vecs.append(v)
    # KMOVQ k, k/m64 is valid in 32-bit mode (only the GPR forms lose W)
    f = find_form("KMOVQ", "kmov_kkm")
    v = Vec("kmovq_32_k_m64", f, vex(f, modrm(0, 2, 6)), mode=32)
    v.mem = bytes([0x81, 0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8]) + bytes(range(0x10, 0x18))
    v.expect(k={2: 0xF8E7D6C5B4A39281})
    vecs.append(v)
    f = find_form("KMOVW", "kmov_kr")
    v = Vec("kmovw_32_k_r", f, vex(f, modrm(3, 1, 0)), mode=32)
    v.g["rax"] = 0xFFFF8001
    v.expect(k={1: 0x8001})
    vecs.append(v)
    return vecs


def gen_ud():
    """(name, code, mode) that #UD with every feature and state enabled."""
    ud = []
    by = lambda mn, kind=None: find_form(mn, kind)
    # wrong VEX.L
    for f in FORMS:
        if f.kind.startswith("kmov") and f.kind != "kmov_kkm":
            continue
        name = "%s with VEX.L%d" % (form_name(f), 1 - f.L)
        mr = modrm(3, 1, 3)
        tail = b"\x01" if f.kind.startswith("kshift") else b""
        vv = kv(2) if f.kind in ("logic3", "unpck") else 0b1111
        ud.append((name, vex(f, mr, vvvv=vv, L=1 - f.L, tail=tail), 64))
    for kind in ("kmov_mk", "kmov_kr", "kmov_rk"):
        f = [g for g in FORMS if g.kind == kind][0]
        mr = modrm(0, 1, 6) if kind == "kmov_mk" else modrm(3, 1, 0)
        ud.append(("%s (%s) with VEX.L1" % (form_name(f), kind), vex(f, mr, L=1), 64))
    # pp/W combinations the SDM does not list
    f41 = by("KANDW")
    for pp in ("F3", "F2"):
        for W in (0, 1):
            ud.append(("0F 41 %s.W%d" % (pp, W), vex(f41, modrm(3, 1, 3), vvvv=kv(2), pp=pp, W=W, c4=True), 64))
    fu = by("KUNPCKBW")
    ud.append(("KUNPCK 66.W1 (no KUNPCKQ...)", vex(fu, modrm(3, 1, 3), vvvv=kv(2), W=1), 64))
    ud.append(("KUNPCK F3.W0", vex(fu, modrm(3, 1, 3), vvvv=kv(2), pp="F3"), 64))
    ud.append(("KUNPCK F2.W1", vex(fu, modrm(3, 1, 3), vvvv=kv(2), pp="F2", W=1), 64))
    for mn, kind in (("KNOTW", None), ("KORTESTW", None), ("KTESTW", None), ("KMOVW", "kmov_kkm"), ("KMOVW", "kmov_mk")):
        f = by(mn, kind)
        mr = modrm(0, 1, 6) if kind == "kmov_mk" else modrm(3, 1, 3)
        for pp in ("F3", "F2"):
            ud.append(("%s %s prefix" % (form_name(f) if kind else mn, pp), vex(f, mr, pp=pp), 64))
    for kind in ("kmov_kr", "kmov_rk"):
        f = [g for g in FORMS if g.kind == kind][0]
        for pp, W in (("NP", 1), ("66", 1), ("F3", 0), ("F3", 1)):
            ud.append(("KMOV %s %s.W%d" % ("k, r" if kind == "kmov_kr" else "r, k", pp, W),
                       vex(f, modrm(3, 1, 0), pp=pp, W=W, c4=True), 64))
    fs = by("KSHIFTRW")
    for pp in ("NP", "F3", "F2"):
        for W in (0, 1):
            ud.append(("0F3A 30 %s.W%d" % (pp, W), vex(fs, modrm(3, 1, 2), pp=pp, W=W, tail=b"\x01"), 64))
    # ModRM.mod != 11b on register-only forms (K20) and mod = 11b on KMOV m, k
    for mn, kind in (("KANDW", None), ("KADDB", None), ("KUNPCKBW", None), ("KXNORQ", None), ("KNOTD", None),
                     ("KORTESTW", None), ("KTESTB", None), ("KSHIFTLW", None), ("KSHIFTRQ", None),
                     ("KMOVW", "kmov_kr"), ("KMOVQ", "kmov_kr"), ("KMOVW", "kmov_rk"), ("KMOVD", "kmov_rk")):
        f = by(mn, kind)
        vv = kv(2) if f.kind in ("logic3", "unpck") else 0b1111
        tail = b"\x01" if f.kind.startswith("kshift") else b""
        nm = form_name(f) + (" k, r" if kind == "kmov_kr" else " r, k" if kind == "kmov_rk" else "")
        ud.append(("%s with a memory operand" % nm, vex(f, modrm(0, 1, 6), vvvv=vv, tail=tail), 64))
    for mn in ("KMOVW", "KMOVQ"):
        f = by(mn, "kmov_mk")
        ud.append(("%s m, k with ModRM.mod = 11b" % mn, vex(f, modrm(3, 1, 2)), 64))
    # Table 2-41: ModRM.reg encodes a k register and VEX.R = 0
    for mn, kind in (("KANDW", None), ("KNOTB", None), ("KORTESTQ", None), ("KTESTD", None), ("KSHIFTLW", None),
                     ("KUNPCKWD", None), ("KMOVW", "kmov_kkm"), ("KMOVW", "kmov_mk"), ("KMOVD", "kmov_kr")):
        f = by(mn, kind)
        vv = kv(2) if f.kind in ("logic3", "unpck") else 0b1111
        tail = b"\x01" if f.kind.startswith("kshift") else b""
        mr = modrm(0, 1, 6) if kind == "kmov_mk" else modrm(3, 1, 3)
        ud.append(("%s with VEX.R = 0 (k9)" % form_name(f), vex(f, mr, rbar=0, vvvv=vv, tail=tail), 64))
    # Table 2-41: vvvv not used and != 1111b
    for mn, kind in (("KNOTW", None), ("KORTESTB", None), ("KTESTQ", None), ("KSHIFTRW", None), ("KMOVW", "kmov_kkm"),
                     ("KMOVQ", "kmov_kkm"), ("KMOVD", "kmov_mk"), ("KMOVB", "kmov_kr"), ("KMOVQ", "kmov_rk")):
        f = by(mn, kind)
        tail = b"\x01" if f.kind.startswith("kshift") else b""
        mr = modrm(0, 1, 6) if kind == "kmov_mk" else modrm(3, 1, 3 if kind != "kmov_kr" else 0)
        for vv in (kv(1), kv(8)):
            ud.append(("%s with vvvv = %s" % (form_name(f), format(vv, "04b")), vex(f, mr, vvvv=vv, tail=tail), 64))
    # Table 2-42: vvvv encodes a k register and vvvv = 0xxxb (64-bit mode)
    for mn in ("KANDW", "KORB", "KADDQ", "KUNPCKDQ", "KXNORD"):
        f = by(mn)
        ud.append(("%s with vvvv = 0xxxb (k10)" % mn, vex(f, modrm(3, 1, 3), vvvv=kv(10), c4=True), 64))
    # prefixes before VEX
    base = vex(by("KANDW"), modrm(3, 1, 3), vvvv=kv(2))
    for p, n in ((b"\x66", "66"), (b"\xf3", "F3"), (b"\xf2", "F2"), (b"\xf0", "LOCK"), (b"\x41", "REX.B")):
        ud.append(("%s before VEX (KANDW)" % n, p + base, 64))
    # 32-bit mode: unused vvvv must still be 1111b (bit 3 of the C4 form)
    f = by("KNOTW")
    ud.append(("KNOTW 32-bit with vvvv = 0111b", vex(f, modrm(3, 1, 3), vvvv=0b0111, c4=True), 32))
    f = by("KMOVW", "kmov_kkm")
    ud.append(("KMOVW 32-bit k, m16 with vvvv = 0111b", vex(f, modrm(0, 1, 6), vvvv=0b0111, c4=True), 32))
    return ud


# --------------------------------------------------------------------------
# selftest: hand-derived values (SDM pseudocode by hand)
# --------------------------------------------------------------------------

def selftest():
    bad = []

    def eq(name, got, exp):
        if got != exp:
            bad.append("%s: got %r expected %r" % (name, got, exp))

    eq("KADDB carry", KADD(8, 0xFFFFFFFFFFFFFFFF, 0x0101), 0x00)
    eq("KADDB", KADD(8, 0xFFFFFFFFFFFFFF7F, 0x017F), 0xFE)
    eq("KADDW wrap", KADD(16, 0xFFFF, 1), 0)
    eq("KADDQ wrap", KADD(64, M64, 2), 1)
    eq("KADDD", KADD(32, 0x1_8000_0000, 0x8000_0000), 0)
    eq("KANDNW", KANDN(16, 0x00FF, 0x0FF0), 0x0F00)
    eq("KANDNB upper", KANDN(8, 0, M64), 0xFF)
    eq("KXNORB", KXNOR(8, 0xF0, 0x0F), 0x00)
    eq("KXNORW all ones", KXNOR(16, 0x1234, 0x1234), 0xFFFF)
    eq("KXNORQ", KXNOR(64, 0, 0), M64)
    eq("KORD", KOR(32, 0xFFFF_0000_0000_0001, 0x10), 0x11)
    eq("KANDQ", KAND(64, 0xF0F0, 0xFF00), 0xF000)
    eq("KXORW", KXOR(16, 0x1_FFFF, 0x00FF), 0xFF00)
    eq("KUNPCKBW", KUNPCK(8, 0x1234, 0xABCD), 0x34CD)
    eq("KUNPCKWD", KUNPCK(16, 0x11112222, 0x33334444), 0x22224444)
    eq("KUNPCKDQ", KUNPCK(32, 0x1_0000_0002, 0xFFFF_FFFF_0000_0003), 0x0000000200000003)
    eq("KNOTB", KNOT(8, 0x5A), 0xA5)
    eq("KNOTW junk", KNOT(16, 0xFFFF_0000), 0xFFFF)
    eq("KNOTQ", KNOT(64, 0), M64)
    eq("KSHIFTLW 15", KSHIFTL(16, 1, 15), 0x8000)
    eq("KSHIFTLW 16", KSHIFTL(16, 1, 16), 0)
    eq("KSHIFTLB 1", KSHIFTL(8, 0x81, 1), 0x02)
    eq("KSHIFTLQ 63", KSHIFTL(64, 3, 63), 1 << 63)
    eq("KSHIFTLQ 64", KSHIFTL(64, 3, 64), 0)
    eq("KSHIFTLD 0x80", KSHIFTL(32, 1, 0x80), 0)
    eq("KSHIFTRW junk", KSHIFTR(16, 0xFFFF_8000, 15), 1)
    eq("KSHIFTRB 7", KSHIFTR(8, 0x180, 7), 1)
    eq("KSHIFTRB 8", KSHIFTR(8, 0x80, 8), 0)
    eq("KSHIFTRD 0xFF", KSHIFTR(32, M64, 0xFF), 0)
    eq("KORTESTB zero", KORTEST(8, 0xFF00, 0x100), (1, 0))
    eq("KORTESTB ones", KORTEST(8, 0xF0, 0x0F), (0, 1))
    eq("KORTESTQ ones", KORTEST(64, M64, 0), (0, 1))
    eq("KORTESTW mid", KORTEST(16, 1, 2), (0, 0))
    eq("KTESTW zero", KTEST(16, 0, 0), (1, 1))
    eq("KTESTB and", KTEST(8, 0x0F, 0x01), (0, 1))
    eq("KTESTB andn", KTEST(8, 0x0F, 0x10), (1, 0))
    eq("KTESTD both", KTEST(32, 0x3, 0x6), (0, 0))
    eq("KTESTQ junk", KTEST(64, 0, M64), (1, 0))
    eq("flags", flags_after_test(0xAD7, 1, 0), 0x242)
    eq("flags2", flags_after_test(0x602, 0, 1), 0x603)
    # encoder: KANDW k1, k2, k3 = C5 EC 41 CB; KMOVQ k1, rax = C4 E1 FB 92 C8; KSHIFTLW k1, k2, 3 = C4 E3 F9 32 CA 03
    eq("enc kandw", vex(find_form("KANDW"), modrm(3, 1, 3), vvvv=kv(2)), bytes.fromhex("C5EC41CB"))
    eq("enc kmovq", vex(find_form("KMOVQ", "kmov_kr"), modrm(3, 1, 0)), bytes.fromhex("C4E1FB92C8"))
    eq("enc kshiftlw", vex(find_form("KSHIFTLW"), modrm(3, 1, 2), tail=b"\x03"), bytes.fromhex("C4E3F932CA03"))
    eq("enc kmovb m", vex(find_form("KMOVB", "kmov_mk"), modrm(0, 1, 6)), bytes.fromhex("C5F9910E"))
    # form count: 6 x 4 three-operand + 3 KUNPCK + 4 KNOT + 4 KORTEST + 4 KTEST + 16 KMOV + 8 KSHIFT
    eq("forms", len(FORMS), 63)
    for v in gen_vectors() + gen_vectors32():
        if v.ek is None:
            bad.append("%s: no expectation" % v.name)
    for b in bad:
        print("FAIL " + b)
    print("selftest: %d checks failed" % len(bad) if bad else "selftest: OK")
    return 1 if bad else 0


# --------------------------------------------------------------------------
# output
# --------------------------------------------------------------------------

def _hexb(b):
    return bytes(b).hex().upper()


def case_line(v):
    s = ".byte " + ", ".join("0x%02x" % c for c in v.code) + " |"
    for i in range(8):
        if v.k[i]:
            s += " k%d=0x%X" % (i, v.k[i])
    for r in GPRS:
        if v.g[r]:
            s += " %s=0x%X" % (r, v.g[r])
    s += " rflags=0x%X m+0x8000=%s =>" % (v.flags, _hexb(v.mem))
    for i in range(8):
        if v.ek[i] != v.k[i]:
            s += " k%d=0x%X" % (i, v.ek[i])
    for r in GPRS:
        if v.eg[r] != v.g[r]:
            s += " %s=0x%X" % (r, v.eg[r])
    if v.eflags != v.flags:
        s += " rflags=0x%X" % v.eflags
    if v.emem != v.mem:
        s += " m+0x8000=%s" % _hexb(v.emem)
    return s


def print_cases():
    w = sys.stdout.write
    w("# VEX-encoded opmask instructions (EVEX milestone K, ledger U127-U133)\n")
    w("# Generated by Emulator/tools/isa/ref_opmask.py --cases (independent SDM model); regenerate, do\n")
    w("# not edit. The i5-13600K has no AVX-512: expected-value cases only, run with AVX-512 enabled:\n")
    w("#   emu-alltest --cases Emulator\\data\\cases_opmask.txt --avx512 --expect-only\n")
    w("# Every line lists the input k/GPR/RFLAGS/memory state and, after \"=>\", what changes (strict:\n")
    w("# everything else must stay). Memory forms use [rsi] / [rsi+8] (RSI = MEM + 0x8000).\n")
    last = None
    for v in gen_vectors():
        if v.form is not last:
            f = v.form
            w("# --- %s (%s VEX.L%d.%s.%s.W%d %02X, %s)\n" % (
                form_name(f), f.kind, f.L, f.pp, {1: "0F", 3: "0F3A"}[f.map], f.W, f.op, FEAT_NAME[f.feat]))
            last = f
        w("# %s\n" % v.name)
        w(case_line(v) + "\n")
    w("# --- #UD encodings (64-bit mode; every feature and XCR0[7:5,1:0] enabled)\n")
    for name, code, mode in gen_ud():
        if mode != 64:
            continue
        w("# %s\n" % name)
        w(".byte " + ", ".join("0x%02x" % c for c in code) + " => #UD\n")


def _carr(b):
    return "{" + ",".join("0x%02x" % c for c in b) + "}"


def print_cinc():
    w = sys.stdout.write
    w("/*\n * Generated by Emulator/tools/isa/ref_opmask.py --cinc (independent SDM reference\n")
    w(" * model); regenerate instead of editing. Used by test_x86.c (test_x86_opmask_*).\n")
    w(" * k[8] / rax / r9 / r10 / rflags / mem[16] (at RSI) in and expected out; feat is the\n")
    w(" * CPUID bit set the form needs (1 AVX512F, 2 AVX512DQ, 4 AVX512BW); mode 64 or 32\n")
    w(" * (32: EAX/ESI/EFLAGS, r9/r10 unused); first = 1 on the first vector of each form.\n */\n")
    w("struct x86_kvec {\n    const char *name;\n    uint8_t code[8];\n    int code_len;\n    int mode;\n    int feat;\n")
    w("    int first;\n")
    w("    uint64_t k[8], rax, r9, r10, rflags;\n    uint8_t mem[16];\n")
    w("    uint64_t ek[8], erax, er9, er10, erflags;\n    uint8_t emem[16];\n};\n\n")
    w("static const struct x86_kvec x86_kvecs[] = {\n")

    def u64s(a):
        return "{" + ",".join("0x%016xULL" % x for x in a) + "}"

    last = None
    for v in gen_vectors() + gen_vectors32():
        first = 1 if (v.form, v.mode) != last else 0
        last = (v.form, v.mode)
        w("    /* %s: %s */\n" % (v.name, " ".join("%02X" % c for c in v.code)))
        w('    { "%s", %s, %d, %d, %d, %d,\n      %s, 0x%xULL, 0x%xULL, 0x%xULL, 0x%xULL, %s,\n      %s, 0x%xULL, 0x%xULL, 0x%xULL, 0x%xULL, %s },\n' % (
            v.name, _carr(v.code), len(v.code), v.mode, v.feat, first,
            u64s(v.k), v.g["rax"], v.g["r9"], v.g["r10"], v.flags, _carr(v.mem),
            u64s(v.ek), v.eg["rax"], v.eg["r9"], v.eg["r10"], v.eflags, _carr(v.emem)))
    w("};\n\n")
    w("struct x86_kud {\n    const char *name;\n    uint8_t code[8];\n    int code_len;\n    int mode;\n};\n\n")
    w("/* #UD with AVX512F/DQ/BW and XCR0 = E7h (reserved encodings, SDM Vol2A 2.9, Tables 2-41/2-42) */\n")
    w("static const struct x86_kud x86_kuds[] = {\n")
    for name, code, mode in gen_ud():
        w('    { "%s", %s, %d, %d },\n' % (name, _carr(code), len(code), mode))
    w("};\n")


def main(argv):
    if len(argv) != 2 or argv[1] not in ("--selftest", "--cases", "--cinc"):
        sys.stderr.write("usage: %s --selftest | --cases | --cinc\n" % argv[0])
        return 2
    try:
        sys.stdout.reconfigure(newline="\n")  # LF output on Windows too
    except (AttributeError, ValueError):
        pass
    if argv[1] == "--selftest":
        return selftest()
    if argv[1] == "--cases":
        print_cases()
        return 0
    print_cinc()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
