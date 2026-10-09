#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m4b.py -- independent reference model (Python 3 stdlib only) of the EVEX forms of
AVX512_VP2INTERSECT, GFNI, VAES and VPCLMULQDQ (ledger U570-U575), and generator of the
expected-value case file Emulator/data/cases_evex_m4b.txt and of the hardware cross-check
file Emulator/data/cases_evex_m4b_hw.txt.

Written from the Intel SDM text only (Vol2A 2.7 "Intel AVX-512 encoding", Tables 2-36 ..
2-43, 2.8 exception classes E4 / E4NF; the instruction pages, "Operation" pseudocode; the
AES round functions as FIPS-197 defines them, which the SDM AESENC/AESDEC pages name), not
from any C implementation:

  VP2INTERSECTD/Q  EVEX.128/256/512.F2.0F38.W0/W1 68 /r   k1+1, src1 (vvvv), src2/m/{1toN}
                   Full tuple, no masking ("EVEX.aaa and EVEX.z must be zero"), E4NF
                   (Vol2C 5-433). CPUID: AVX512F (512) / AVX512VL (128/256) AND
                   AVX512_VP2INTERSECT (CPUID.(7,0):EDX[8]); no AVX10.1 alternative.
  VGF2P8AFFINEQB   EVEX.128/256/512.66.0F3A.W1 CE /r ib    {k1}{z} per byte, m64bcst, Full,
  VGF2P8AFFINEINVQB EVEX.128/256/512.66.0F3A.W1 CF /r ib   E4NF (Vol2A 3-426 .. 3-430)
  VGF2P8MULB       EVEX.128/256/512.66.0F38.W0 CF /r       {k1}{z} per byte, Full Mem, E4
                   (Vol2A 3-431). CPUID: (AVX512F OR AVX10.1) GFNI; 128/256: (AVX512VL OR
                   AVX10.1) GFNI.
  VAESENC/VAESENCLAST/VAESDEC/VAESDECLAST
                   EVEX.128/256/512.66.0F38.WIG DC/DD/DE/DF /r   Full Mem, no masking, E4NF
                   (Vol2A 3-33 .. 3-52). CPUID: VAES (AVX512F OR AVX10.1) / (AVX512VL OR
                   AVX10.1).
  VPCLMULQDQ       EVEX.128/256/512.66.0F3A.WIG 44 /r ib   Full Mem, no masking, E4NF
                   (Vol2B 4-242). CPUID: VPCLMULQDQ (AVX512F OR AVX10.1) / (AVX512VL OR
                   AVX10.1).

Generic EVEX rules used (SDM Vol2A 2.7):
  MASK     GFNI: FOR j (byte): IF k1[j] OR *no writemask* THEN DEST.byte[j] := op ELSE
           *zeroing* 0 / *merging* unchanged; DEST[MAX_VL-1:VL] := 0
  BCST     VGF2P8AFFINE*: "IF SRC2 is memory and EVEX.b==1: tsrc2 := SRC2.qword[0]"
  LOAD     E4 (VGF2P8MULB): masked-off bytes are not read (fault suppression); E4NF: the
           whole memory operand is read whatever the mask (Table 2-52 "#PF: for a page fault")
  #UD      L'L = 11b; EVEX.b on a register form (Table 2-43) or with a Full Mem tuple; EVEX.z
           with aaa = 000b; aaa != 000b or z = 1 on the forms without masking (Table 2-42);
           EVEX.R = 0 or EVEX.R' = 0 with a k register in ModRM.reg in 64-bit mode (Table 2-41);
           the wrong EVEX.W for W0/W1 forms; another pp (no form there).
  disp8*N  Full: N = VL, or the element size with EVEX.b; Full Mem: N = VL (Tables 2-36/2-37).

Usage:
  python ref_evex_m4b.py --selftest     hand-derived / FIPS-197 / SDM-table checks, exit 0 on pass
  python ref_evex_m4b.py --cases        Emulator/data/cases_evex_m4b.txt (stdout)
  python ref_evex_m4b.py --hwgen        Emulator/data/cases_evex_m4b_hw.txt (stdout): the GFNI,
                                        VAES and VPCLMULQDQ operations of every EVEX case on the
                                        host's VEX.256 / VEX.128 / legacy forms, the same data
  python ref_evex_m4b.py --hwcmp LOG    compare the host's results ("hw:" lines of emu-alltest
                                        --cases cases_evex_m4b_hw.txt) with this model
"""

import random
import re
import sys

# harness layout (Emulator/tests/alltest/at_engine.hpp): RSI = MEM + 0x8000, operand memory
# MEM .. MEM + 0xFFFF mapped, MEM + 0x10000 unmapped
MEM_RSI = 0x8000
RSI = 6


# ---------------------------------------------------------------------------------------
# encodings: EVEX (SDM Vol2A 2.7.1 Table 2-32: P0 = R X B R' 0 m m m, P1 = W v v v v 1 p p,
# P2 = z L' L b V' a a a; R/X/B/R'/vvvv/V' stored inverted) and 3-byte VEX (2.3.5)
# ---------------------------------------------------------------------------------------
class Mem:
    def __init__(self, disp=0, disp32=False):
        self.disp, self.disp32 = disp, disp32


def modrm_mem(reg, mem, n):
    """[rsi + disp]: mod 01 with disp8*N when it fits, else mod 10 with disp32"""
    d = mem.disp
    if not mem.disp32 and d != 0 and d % n == 0 and -128 <= d // n <= 127:
        return bytes([0x40 | ((reg & 7) << 3) | RSI, (d // n) & 0xFF])
    if d == 0 and not mem.disp32:
        return bytes([((reg & 7) << 3) | RSI])
    return bytes([0x80 | ((reg & 7) << 3) | RSI]) + (d & 0xFFFFFFFF).to_bytes(4, "little")


def evex(mmm, pp, w, opc, reg, rm, vvvv=0, ll=0, b=0, z=0, aaa=0, n=1, imm=None, p0_r=None,
         p0_rr=None):
    """one EVEX instruction; reg / vvvv / rm register numbers 0-31 (rm may be a Mem).
    p0_r / p0_rr: force the stored (inverted) EVEX.R / EVEX.R' bit."""
    r, rr = (reg >> 3) & 1, (reg >> 4) & 1
    if isinstance(rm, Mem):
        x, bb = 0, 0
        tail = modrm_mem(reg, rm, n)
    else:
        x, bb = (rm >> 4) & 1, (rm >> 3) & 1
        tail = bytes([0xC0 | ((reg & 7) << 3) | (rm & 7)])
    sr = (r ^ 1) if p0_r is None else p0_r
    srr = (rr ^ 1) if p0_rr is None else p0_rr
    p0 = (sr << 7) | ((x ^ 1) << 6) | ((bb ^ 1) << 5) | (srr << 4) | mmm
    p1 = (w << 7) | (((~vvvv) & 0xF) << 3) | 4 | pp
    p2 = (z << 7) | (ll << 5) | (b << 4) | ((((vvvv >> 4) & 1) ^ 1) << 3) | aaa
    out = bytes([0x62, p0, p1, p2, opc]) + tail
    if imm is not None:
        out += bytes([imm & 0xFF])
    return out


def vex3(mmmmm, pp, w, l, opc, reg, rm, vvvv, imm=None):
    """3-byte VEX C4 (registers 0-15): R X B m-mmmm, W vvvv L pp (R/X/B/vvvv inverted)"""
    b1 = ((((reg >> 3) & 1) ^ 1) << 7) | (1 << 6) | ((((rm >> 3) & 1) ^ 1) << 5) | mmmmm
    b2 = (w << 7) | (((~vvvv) & 0xF) << 3) | (l << 2) | pp
    out = bytes([0xC4, b1, b2, opc, 0xC0 | ((reg & 7) << 3) | (rm & 7)])
    if imm is not None:
        out += bytes([imm & 0xFF])
    return out


def legacy(esc, opc, reg, rm, imm=None):
    """66 0F 38/3A opc /r [ib] with registers 0-7"""
    out = bytes([0x66, 0x0F, esc, opc, 0xC0 | ((reg & 7) << 3) | (rm & 7)])
    if imm is not None:
        out += bytes([imm & 0xFF])
    return out


def byte_list(bs):
    return ".byte " + ", ".join("0x%02x" % c for c in bs)


def hexs(buf):
    return "".join("%02X" % c for c in buf)


def elems(buf, esz):
    return [int.from_bytes(buf[i:i + esz], "little") for i in range(0, len(buf), esz)]


# ---------------------------------------------------------------------------------------
# GF(2^8) (SDM Vol2A GF2P8MULB / GF2P8AFFINEQB / GF2P8AFFINEINVQB Operation)
# ---------------------------------------------------------------------------------------
def gf2p8mul_byte(a, b):
    """tword := XOR of (src1byte << i) for src2byte.bit[i]; reduce bits 14..8 by 0x11B << (i-8)"""
    t = 0
    for i in range(8):
        if (b >> i) & 1:
            t ^= a << i
    for i in range(14, 7, -1):
        if (t >> i) & 1:
            t ^= 0x11B << (i - 8)
    return t & 0xFF


def _build_inverse():
    """inverse(x) w.r.t. x^8 + x^4 + x^3 + x + 1 by search (the y with x*y = 1); inverse(0) = 0
    (Table 3-52 row 0, column 0)"""
    inv = [0] * 256
    for x in range(1, 256):
        for y in range(1, 256):
            if gf2p8mul_byte(x, y) == 1:
                inv[x] = y
                break
    return inv


GF_INV = _build_inverse()


def parity(x):
    return bin(x).count("1") & 1


def affine_byte(qw, x, imm):
    """retbyte.bit[i] := parity(tsrc2qw.byte[7-i] AND src1byte) XOR imm8.bit[i]"""
    r = 0
    for i in range(8):
        row = (qw >> (8 * (7 - i))) & 0xFF
        r |= (parity(row & x) ^ ((imm >> i) & 1)) << i
    return r


def gf_affine_vec(a, b, imm, vl, inverse):
    """VGF2P8AFFINE(INV)QB unmasked: DEST.qword[j].byte[b] := affine(SRC2.qword[j],
    (inverse of) SRC1.qword[j].byte[b], imm8); b is the effective SRC2 (broadcast applied)"""
    out = bytearray(vl)
    for j in range(vl // 8):
        qw = int.from_bytes(b[8 * j:8 * j + 8], "little")
        for k in range(8):
            x = a[8 * j + k]
            out[8 * j + k] = affine_byte(qw, GF_INV[x] if inverse else x, imm)
    return bytes(out)


def gf_mul_vec(a, b, imm, vl):
    return bytes(gf2p8mul_byte(a[j], b[j]) for j in range(vl))


# ---------------------------------------------------------------------------------------
# AES round functions (FIPS-197 5.1/5.3, named by the SDM AESENC/AESDEC Operation): the
# 128-bit state is 16 bytes in column order, byte k = row k mod 4, column k div 4
# ---------------------------------------------------------------------------------------
def _sbox_byte(x):
    """SubBytes: b := inverse(x) in GF(2^8), then b'_i := b_i ^ b_(i+4) ^ b_(i+5) ^ b_(i+6) ^
    b_(i+7) ^ c_i (indices mod 8, c = 63h)"""
    b = GF_INV[x]
    r = 0
    for i in range(8):
        bit = ((b >> i) ^ (b >> ((i + 4) % 8)) ^ (b >> ((i + 5) % 8)) ^ (b >> ((i + 6) % 8)) ^
               (b >> ((i + 7) % 8)) ^ (0x63 >> i)) & 1
        r |= bit << i
    return r


SBOX = [_sbox_byte(x) for x in range(256)]
INV_SBOX = [0] * 256
for _x in range(256):
    INV_SBOX[SBOX[_x]] = _x


def shift_rows(s):
    """row r rotated left by r: s'[r, c] = s[r, (c + r) mod 4]"""
    return [s[(k % 4) + 4 * (((k // 4) + (k % 4)) % 4)] for k in range(16)]


def inv_shift_rows(s):
    """s'[r, (c + r) mod 4] = s[r, c]"""
    return [s[(k % 4) + 4 * (((k // 4) - (k % 4)) % 4)] for k in range(16)]


def _mix(s, m):
    out = [0] * 16
    for c in range(4):
        col = s[4 * c:4 * c + 4]
        for r in range(4):
            v = 0
            for i in range(4):
                v ^= gf2p8mul_byte(m[r][i], col[i])
            out[4 * c + r] = v
    return out


MIX = [[2, 3, 1, 1], [1, 2, 3, 1], [1, 1, 2, 3], [3, 1, 1, 2]]
INV_MIX = [[0x0E, 0x0B, 0x0D, 0x09], [0x09, 0x0E, 0x0B, 0x0D], [0x0D, 0x09, 0x0E, 0x0B],
           [0x0B, 0x0D, 0x09, 0x0E]]


def aes_round(state, key, op):
    """op: enc, enclast, dec, declast (SDM Operation: STATE := SRC1; ...; DEST := STATE XOR
    RoundKey)"""
    s = list(state)
    if op in ("enc", "enclast"):
        s = shift_rows(s)
        s = [SBOX[x] for x in s]
        if op == "enc":
            s = _mix(s, MIX)
    else:
        s = inv_shift_rows(s)
        s = [INV_SBOX[x] for x in s]
        if op == "dec":
            s = _mix(s, INV_MIX)
    return bytes(x ^ k for x, k in zip(s, key))


def aes_vec(op):
    def fn(a, b, imm, vl):
        return b"".join(aes_round(a[16 * i:16 * i + 16], b[16 * i:16 * i + 16], op)
                        for i in range(vl // 16))
    return fn


# ---------------------------------------------------------------------------------------
# PCLMULQDQ (SDM Vol2B 4-242): per 128-bit lane, TEMP1 := SRC1.xmm[i].qword[imm8[0]],
# TEMP2 := SRC2.xmm[i].qword[imm8[4]], DEST.xmm[i] := PCLMUL128(TEMP1, TEMP2)
# ---------------------------------------------------------------------------------------
def pclmul128(x, y):
    """DEST[i] := XOR over j of X[j] AND Y[i-j] (i = 0..126); DEST[127] := 0"""
    r = 0
    for i in range(127):
        t = 0
        for j in range(max(0, i - 63), min(i, 63) + 1):
            t ^= ((x >> j) & 1) & ((y >> (i - j)) & 1)
        r |= t << i
    return r


def pclmul_vec(a, b, imm, vl):
    out = b""
    for i in range(vl // 16):
        q1 = elems(a[16 * i:16 * i + 16], 8)
        q2 = elems(b[16 * i:16 * i + 16], 8)
        out += pclmul128(q1[imm & 1], q2[(imm >> 4) & 1]).to_bytes(16, "little")
    return out


# ---------------------------------------------------------------------------------------
# VP2INTERSECTD/Q (SDM Vol2C 5-434)
# ---------------------------------------------------------------------------------------
def vp2intersect(a, b, esz):
    """maskregs[base+0/1] := 0; FOR i, j < KL: match := (src1[i] == src2[j]);
    maskregs[base+0].bit[i] |= match; maskregs[base+1].bit[j] |= match"""
    s1, s2 = elems(a, esz), elems(b, esz)
    m0 = m1 = 0
    for i in range(len(s1)):
        for j in range(len(s2)):
            if s1[i] == s2[j]:
                m0 |= 1 << i
                m1 |= 1 << j
    return m0, m1


# ---------------------------------------------------------------------------------------
# cases
# ---------------------------------------------------------------------------------------
class Case:
    def __init__(self, title):
        self.title = title
        self.code = b""
        self.zmm, self.k, self.mem = {}, {}, {}
        self.exp = []
        self.fault = None

    def line(self):
        ins = ["zmm%d=%s" % (r, hexs(self.zmm[r])) for r in sorted(self.zmm)]
        ins += ["k%d=0x%X" % (r, self.k[r]) for r in sorted(self.k)]
        ins += ["m+0x%X=%s" % (o, hexs(self.mem[o])) for o in sorted(self.mem)]
        exp = list(self.exp) + ([self.fault] if self.fault else [])
        return "%s | %s => %s" % (byte_list(self.code), " ".join(ins), " ".join(exp))


RNG = random.Random(0x4B_2_5EED_570)
VL_LL = {16: 0, 32: 1, 64: 2}
out_lines = []
HW_RECS = []        # (form name, vl, src1, effective src2, imm, unmasked result)


def emit(c):
    out_lines.append("# " + c.title)
    out_lines.append(c.line())


def comment(t):
    out_lines.append("# " + t)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


def mask_merge(old, res, vl, kmask, zero):
    """byte-element MASK wrapper + DEST[MAX_VL-1:VL] := 0"""
    out = bytearray(64)
    for j in range(vl):
        if kmask is None or (kmask >> j) & 1:
            out[j] = res[j]
        else:
            out[j] = 0 if zero else old[j]
    return bytes(out)


IDENTITY = 0x0102040810204080          # affine identity matrix (byte[7-i] = 1 << i)

# vector forms: name, map, opcode, W (None = WIG), imm8, mask (byte masking {k1}{z}),
# besz ({1toN} element bytes; None = Full Mem tuple), fs (E4 fault suppression), fn, hw
VEC_FORMS = [
    dict(name="VGF2P8AFFINEQB", mmm=3, opc=0xCE, w=1, imm=True, mask=True, besz=8, fs=False,
         fn=lambda a, b, imm, vl: gf_affine_vec(a, b, imm, vl, False), fam="GFNI"),
    dict(name="VGF2P8AFFINEINVQB", mmm=3, opc=0xCF, w=1, imm=True, mask=True, besz=8, fs=False,
         fn=lambda a, b, imm, vl: gf_affine_vec(a, b, imm, vl, True), fam="GFNI"),
    dict(name="VGF2P8MULB", mmm=2, opc=0xCF, w=0, imm=False, mask=True, besz=None, fs=True,
         fn=gf_mul_vec, fam="GFNI"),
    dict(name="VAESENC", mmm=2, opc=0xDC, w=None, imm=False, mask=False, besz=None, fs=False,
         fn=aes_vec("enc"), fam="VAES"),
    dict(name="VAESENCLAST", mmm=2, opc=0xDD, w=None, imm=False, mask=False, besz=None, fs=False,
         fn=aes_vec("enclast"), fam="VAES"),
    dict(name="VAESDEC", mmm=2, opc=0xDE, w=None, imm=False, mask=False, besz=None, fs=False,
         fn=aes_vec("dec"), fam="VAES"),
    dict(name="VAESDECLAST", mmm=2, opc=0xDF, w=None, imm=False, mask=False, besz=None, fs=False,
         fn=aes_vec("declast"), fam="VAES"),
    dict(name="VPCLMULQDQ", mmm=3, opc=0x44, w=None, imm=True, mask=False, besz=None, fs=False,
         fn=pclmul_vec, fam="VPCLMULQDQ"),
]


def rnd_imm(sp):
    if sp["name"] == "VPCLMULQDQ":
        # bits 0 and 4 select, the other bits are ignored
        return RNG.choice([0x00, 0x01, 0x10, 0x11]) | (RNG.getrandbits(8) & 0xEE)
    return RNG.getrandbits(8)


def src_data(sp, n):
    r = RNG.random()
    if r < 0.08:
        return bytes(n)
    if r < 0.16:
        return bytes([0xFF]) * n
    return rnd_bytes(n)


def gen_vec_case(sp, vl, title, dst, s1, s2, kreg=0, kval=None, z=0, mem=None, bcst=0, imm=None,
                 w=None, aimg=None, bimg=None, record=True):
    """sp zmm1{k}{z}, zmm2 (vvvv), zmm3/m/{1toN} [, imm8]"""
    c = Case("%s VL%d %s" % (sp["name"], vl * 8, title))
    regs = {}
    regs[s1] = (aimg if aimg is not None else src_data(sp, vl)) + rnd_bytes(64 - vl)
    if mem is None:
        if s2 not in regs:
            regs[s2] = (bimg if bimg is not None else src_data(sp, vl)) + rnd_bytes(64 - vl)
        b = regs[s2][:vl]
        rm, nn = s2, 1
    else:
        data = bimg if bimg is not None else src_data(sp, sp["besz"] if bcst else vl)
        c.mem[MEM_RSI + mem.disp] = data
        b = data * (vl // len(data)) if bcst else data
        rm, nn = mem, (sp["besz"] if bcst else vl)
    if dst not in regs:
        regs[dst] = rnd_bytes(64)
    c.zmm.update(regs)
    a = regs[s1][:vl]
    if imm is None and sp["imm"]:
        imm = rnd_imm(sp)
    if kreg:
        c.k[kreg] = kval
    res = sp["fn"](a, b, imm, vl)
    if record:
        HW_RECS.append((sp["name"], vl, a, b, imm or 0, res))
    out = mask_merge(regs[dst], res, vl, kval if kreg else None, z)
    c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    ww = sp["w"] if sp["w"] is not None else (w if w is not None else RNG.getrandbits(1))
    c.code = evex(sp["mmm"], 1, ww, sp["opc"], dst, rm, vvvv=s1, ll=VL_LL[vl], b=bcst, z=z,
                  aaa=kreg, n=nn, imm=imm if sp["imm"] else None)
    return c


def gen_vec_form(sp):
    name = sp["name"]
    comment("%s (EVEX.66.%s.%s %02X /r%s), %s" % (
        name, "0F38" if sp["mmm"] == 2 else "0F3A", "WIG" if sp["w"] is None else "W%d" % sp["w"],
        sp["opc"], " ib" if sp["imm"] else "", "E4" if sp["fs"] else "E4NF"))
    for vl in (16, 32, 64):
        emit(gen_vec_case(sp, vl, "nomask", 1, 2, 3))
        emit(gen_vec_case(sp, vl, "zmm16+ (EVEX.R'/V'/X)", 17, 30, 9))
        emit(gen_vec_case(sp, vl, "zmm16+ src2 (EVEX.X)", 5, 6, 28))
        emit(gen_vec_case(sp, vl, "mem", 6, 8, 0, mem=Mem(0x40)))
        if sp["w"] is None:
            emit(gen_vec_case(sp, vl, "EVEX.W1 (WIG)", 7, 11, 12, w=1))
            emit(gen_vec_case(sp, vl, "EVEX.W0 (WIG)", 7, 11, 12, w=0))
        if sp["mask"]:
            emit(gen_vec_case(sp, vl, "merge", 17, 30, 9, kreg=3, kval=RNG.getrandbits(64)))
            emit(gen_vec_case(sp, vl, "zero", 4, 25, 26, kreg=7, kval=RNG.getrandbits(64), z=1))
            emit(gen_vec_case(sp, vl, "mem merge", 7, 8, 0, mem=Mem(0x40), kreg=5,
                              kval=RNG.getrandbits(64)))
            emit(gen_vec_case(sp, vl, "mem zero", 7, 8, 0, mem=Mem(-0x40), kreg=6,
                              kval=RNG.getrandbits(64), z=1))
            emit(gen_vec_case(sp, vl, "k = 0 merge", 18, 19, 20, kreg=1, kval=0))
            emit(gen_vec_case(sp, vl, "k all ones zero", 21, 22, 23, kreg=2, kval=(1 << 64) - 1, z=1))
        if sp["besz"]:
            emit(gen_vec_case(sp, vl, "bcst", 10, 11, 0, mem=Mem(0x40), bcst=1))
            emit(gen_vec_case(sp, vl, "bcst merge", 10, 11, 0, mem=Mem(0x40), bcst=1, kreg=2,
                              kval=RNG.getrandbits(64)))
            emit(gen_vec_case(sp, vl, "bcst zero", 12, 13, 0, mem=Mem(-0x40), bcst=1, kreg=4,
                              kval=RNG.getrandbits(64), z=1))
        if sp["imm"]:
            imms = [0x00, 0x01, 0x10, 0x11, 0xEE, 0xFF] if name == "VPCLMULQDQ" else [0x00, 0xFF, 0x63, 0x5A]
            for imm in imms:
                emit(gen_vec_case(sp, vl, "imm8=0x%02X" % imm, 14, 15, 16, imm=imm))
    # destination = each source
    emit(gen_vec_case(sp, 64, "dst=src1", 16, 16, 18))
    emit(gen_vec_case(sp, 64, "dst=src2", 19, 20, 19))
    emit(gen_vec_case(sp, 64, "dst=src1=src2", 24, 24, 24))
    if sp["mask"]:
        emit(gen_vec_case(sp, 64, "dst=src1 merge", 16, 16, 18, kreg=1, kval=RNG.getrandbits(64)))
        emit(gen_vec_case(sp, 64, "dst=src2 zero", 19, 20, 19, kreg=6, kval=RNG.getrandbits(64), z=1))
    # value coverage
    if name in ("VGF2P8AFFINEQB", "VGF2P8AFFINEINVQB"):
        ident = IDENTITY.to_bytes(8, "little") * 8
        for q in range(4):
            emit(gen_vec_case(sp, 64, "identity matrix, x = %02X..%02X, imm 0" % (64 * q, 64 * q + 63),
                              1, 2, 3, imm=0, aimg=bytes(range(64 * q, 64 * q + 64)), bimg=ident))
        emit(gen_vec_case(sp, 64, "zero matrix, imm 0xA5 (= b)", 1, 2, 3, imm=0xA5, bimg=bytes(64)))
        emit(gen_vec_case(sp, 64, "all-ones matrix (parity)", 1, 2, 3, imm=0, bimg=bytes([0xFF]) * 64))
        emit(gen_vec_case(sp, 64, "identity bcst", 1, 2, 0, imm=0x11, mem=Mem(0x80), bcst=1,
                          bimg=IDENTITY.to_bytes(8, "little")))
    elif name == "VGF2P8MULB":
        for q in range(4):
            emit(gen_vec_case(sp, 64, "a = %02X..%02X, b = 03" % (64 * q, 64 * q + 63), 1, 2, 3,
                              aimg=bytes(range(64 * q, 64 * q + 64)), bimg=bytes([3]) * 64))
        for q in range(4):
            emit(gen_vec_case(sp, 64, "a = %02X..%02X, b = inverse(a) (product 1, 0 for 0)"
                              % (64 * q, 64 * q + 63), 1, 2, 3,
                              aimg=bytes(range(64 * q, 64 * q + 64)),
                              bimg=bytes(GF_INV[x] for x in range(64 * q, 64 * q + 64))))
        emit(gen_vec_case(sp, 64, "edge bytes", 1, 2, 3,
                          aimg=bytes([0x80, 0xFF, 0x1B, 0x01, 0x00, 0x53, 0xCA, 0x8D] * 8),
                          bimg=bytes([0x80, 0xFF, 0x80, 0x02, 0xFF, 0xCA, 0x53, 0x02] * 8)))
    elif name in ("VAESENCLAST", "VAESDECLAST", "VAESENC", "VAESDEC"):
        for q in range(4):
            emit(gen_vec_case(sp, 64, "state bytes %02X..%02X, key 0" % (64 * q, 64 * q + 63), 1, 2, 3,
                              aimg=bytes(range(64 * q, 64 * q + 64)), bimg=bytes(64)))
        emit(gen_vec_case(sp, 64, "state 0, key random", 1, 2, 3, aimg=bytes(64)))
    elif name == "VPCLMULQDQ":
        for imm in (0x00, 0x01, 0x10, 0x11):
            emit(gen_vec_case(sp, 64, "all ones x all ones imm 0x%02X" % imm, 1, 2, 3, imm=imm,
                              aimg=bytes([0xFF]) * 64, bimg=bytes([0xFF]) * 64))
            emit(gen_vec_case(sp, 64, "top bits imm 0x%02X" % imm, 1, 2, 3, imm=imm,
                              aimg=(1 << 63 | 1 << 127).to_bytes(16, "little") * 4,
                              bimg=(1 << 63 | 1 << 127 | 1).to_bytes(16, "little") * 4))
    # disp8*N: Full (N = VL, or 8 with {1to8}) / Full Mem (N = VL)
    for vl in (16, 32, 64):
        for bcst in ((0, 1) if sp["besz"] else (0,)):
            nn = sp["besz"] if bcst else vl
            for d8 in (1, -1, 127, -128):
                emit(gen_vec_case(sp, vl, "disp8=%d N=%d" % (d8, nn), 21, 22, 0,
                                  mem=Mem(d8 * nn), bcst=bcst))
    # memory next to the unmapped page MEM+0x10000
    base = 0x10000 - MEM_RSI - 32
    if sp["fs"]:
        # E4: masked-off bytes are not read
        c = gen_vec_case(sp, 64, "[end-32] {k: mapped bytes only} no fault", 3, 4, 0,
                         mem=Mem(base, disp32=True), kreg=1, kval=(1 << 32) - 1, z=1,
                         bimg=rnd_bytes(32) + bytes(32), record=False)
        # the unmapped upper half is never read: the masked-off result bytes are zeroed
        c.mem = {MEM_RSI + base: c.mem[MEM_RSI + base][:32]}
        emit(c)
        c = Case("%s VL512 [end-32] {k: one unmapped byte} #PF" % name)
        c.zmm[3], c.zmm[4] = rnd_bytes(64), rnd_bytes(64)
        c.k[1] = (1 << 33) - 1
        c.mem[MEM_RSI + base] = rnd_bytes(32)
        c.code = evex(sp["mmm"], 1, sp["w"] or 0, sp["opc"], 3, Mem(base, disp32=True), vvvv=4,
                      ll=2, aaa=1, imm=0 if sp["imm"] else None)
        c.fault = "#PF"
        emit(c)
    else:
        c = Case("%s VL512 [end-32] %s#PF (E4NF: the whole operand is read)"
                 % (name, "{k: mapped bytes only} " if sp["mask"] else ""))
        c.zmm[3], c.zmm[4] = rnd_bytes(64), rnd_bytes(64)
        if sp["mask"]:
            c.k[1] = (1 << 32) - 1
        c.mem[MEM_RSI + base] = rnd_bytes(32)
        c.code = evex(sp["mmm"], 1, sp["w"] or 0, sp["opc"], 3, Mem(base, disp32=True), vvvv=4,
                      ll=2, aaa=1 if sp["mask"] else 0, imm=0x11 if sp["imm"] else None)
        c.fault = "#PF"
        emit(c)
        c = gen_vec_case(sp, 32, "[end-32] fully mapped, no fault", 3, 4, 0,
                         mem=Mem(base, disp32=True))
        emit(c)
        if sp["besz"]:
            c = Case("%s VL512 {1to8} k = 0 on the unmapped page #PF (E4NF)" % name)
            c.zmm[3], c.zmm[4] = rnd_bytes(64), rnd_bytes(64)
            c.k[6] = 0
            c.code = evex(sp["mmm"], 1, sp["w"], sp["opc"], 3, Mem(0x10000 - MEM_RSI, disp32=True),
                          vvvv=4, ll=2, b=1, aaa=6, imm=0)
            c.fault = "#PF"
            emit(c)
    # #UD
    w = sp["w"] or 0
    uds = [("L'L = 11b", dict(ll=3)), ("EVEX.b on a register form", dict(b=1))]
    if sp["mask"]:
        uds.append(("{z} with aaa = 000b", dict(z=1)))
    else:
        uds += [("aaa = 001b (no masking)", dict(aaa=1)), ("aaa = 111b (no masking)", dict(aaa=7)),
                ("{z} (no masking)", dict(z=1)), ("{z} with aaa = 001b", dict(z=1, aaa=1))]
    if sp["w"] is not None:
        uds.append(("EVEX.W%d" % (1 - w), dict(w=1 - w)))
    for pp, pn in ((0, "NP"), (2, "F3"), (3, "F2")):
        uds.append(("pp = %s" % pn, dict(pp=pp)))
    for title, kw in uds:
        c = Case("%s %s #UD" % (name, title))
        ww = kw.pop("w", w)
        pp = kw.pop("pp", 1)
        ll = kw.pop("ll", 2)
        c.code = evex(sp["mmm"], pp, ww, sp["opc"], 1, 3, vvvv=2, ll=ll, imm=0 if sp["imm"] else None,
                      **kw)
        c.fault = "#UD"
        emit(c)
    if not sp["besz"]:
        c = Case("%s EVEX.b with memory (Full Mem tuple) #UD" % name)
        c.code = evex(sp["mmm"], 1, w, sp["opc"], 1, Mem(0x40), vvvv=2, ll=2, b=1,
                      imm=0 if sp["imm"] else None)
        c.fault = "#UD"
        emit(c)
    if sp["mask"]:
        c = Case("%s {z} with aaa = 000b, memory source #UD" % name)
        c.code = evex(sp["mmm"], 1, w, sp["opc"], 1, Mem(0x40), vvvv=2, ll=2, z=1,
                      imm=0 if sp["imm"] else None)
        c.fault = "#UD"
        emit(c)


# ---------------------------------------------------------------------------------------
# VP2INTERSECTD/Q
# ---------------------------------------------------------------------------------------
def vp2_vals(esz, n, kind):
    bits = 8 * esz
    if kind == "dup":
        pool = [RNG.getrandbits(bits) for _ in range(3)] + [0, (1 << bits) - 1]
        return [RNG.choice(pool) for _ in range(n)]
    return [RNG.getrandbits(bits) for _ in range(n)]


def pack(vals, esz):
    return b"".join((v & ((1 << (8 * esz)) - 1)).to_bytes(esz, "little") for v in vals)


def gen_vp2_case(w, vl, title, kd, s1, s2, mem=None, bcst=0, avals=None, bvals=None, kreg_field=None,
                 aaa=0, z=0):
    esz = 8 if w else 4
    n = vl // esz
    name = "VP2INTERSECTQ" if w else "VP2INTERSECTD"
    c = Case("%s VL%d %s" % (name, vl * 8, title))
    kind = "dup" if RNG.random() < 0.8 else "rnd"
    if avals is None:
        avals = vp2_vals(esz, n, kind)
    regs = {s1: pack(avals, esz) + rnd_bytes(64 - vl)}
    if mem is None:
        if s2 not in regs:
            if bvals is None:
                # SRC2 mostly drawn from SRC1's values so the masks are non-trivial
                bvals = [RNG.choice(avals) if RNG.random() < 0.5 else RNG.getrandbits(8 * esz)
                         for _ in range(n)]
            regs[s2] = pack(bvals, esz) + rnd_bytes(64 - vl)
        b = regs[s2][:vl]
        rm, nn = s2, 1
    else:
        if bvals is None:
            bvals = [RNG.choice(avals) if RNG.random() < 0.5 else RNG.getrandbits(8 * esz)
                     for _ in range(1 if bcst else n)]
        data = pack(bvals, esz)
        c.mem[MEM_RSI + mem.disp] = data
        b = data * n if bcst else data
        rm, nn = mem, (esz if bcst else vl)
    c.zmm.update(regs)
    a = regs[s1][:vl]
    for r in range(8):
        c.k[r] = RNG.getrandbits(64)
    m0, m1 = vp2intersect(a, b, esz)
    base = kd & ~1
    c.exp.append("k%d=0x%X" % (base, m0))
    c.exp.append("k%d=0x%X" % (base + 1, m1))
    c.code = evex(2, 3, w, 0x68, kd if kreg_field is None else kreg_field, rm, vvvv=s1,
                  ll=VL_LL[vl], b=bcst, n=nn, aaa=aaa, z=z)
    return c


def gen_vp2():
    for w in (0, 1):
        esz = 8 if w else 4
        name = "VP2INTERSECTQ" if w else "VP2INTERSECTD"
        comment("%s (EVEX.F2.0F38.W%d 68 /r) k1+1, zmm2, zmm3/m/m%dbcst, E4NF, no masking"
                % (name, w, 8 * esz))
        for vl in (16, 32, 64):
            n = vl // esz
            emit(gen_vp2_case(w, vl, "k2+k3 (ModRM.reg = 2)", 2, 2, 3))
            emit(gen_vp2_case(w, vl, "k2+k3 (ModRM.reg = 3: the low bit is ignored)", 3, 4, 5))
            emit(gen_vp2_case(w, vl, "k0+k1 (ModRM.reg = 1)", 1, 6, 7))
            emit(gen_vp2_case(w, vl, "k6+k7, zmm16+ sources", 6, 30, 17))
            emit(gen_vp2_case(w, vl, "k4+k5, src1 = src2", 4, 9, 9))
            emit(gen_vp2_case(w, vl, "mem", 4, 10, 0, mem=Mem(0x40)))
            emit(gen_vp2_case(w, vl, "bcst", 5, 11, 0, mem=Mem(0x40), bcst=1))
            emit(gen_vp2_case(w, vl, "all equal", 2, 12, 13, avals=[0x77] * n, bvals=[0x77] * n))
            emit(gen_vp2_case(w, vl, "all distinct, no match", 2, 12, 13,
                              avals=list(range(1, n + 1)), bvals=list(range(n + 1, 2 * n + 1))))
            emit(gen_vp2_case(w, vl, "reversed", 2, 12, 13, avals=list(range(n)),
                              bvals=list(range(n - 1, -1, -1))))
            emit(gen_vp2_case(w, vl, "one match at i = n-1, j = 0", 0, 14, 15,
                              avals=list(range(100, 100 + n)), bvals=[100 + n - 1] + [7] * (n - 1)))
            # elements equal except the top bit (no match); Q: equal low dwords only (no match)
            top = 1 << (8 * esz - 1)
            emit(gen_vp2_case(w, vl, "differ in the top bit only", 6, 16, 18,
                              avals=[top | i for i in range(n)], bvals=list(range(n))))
            if w:
                emit(gen_vp2_case(w, vl, "equal low dwords, different high dwords", 6, 16, 18,
                                  avals=[(1 << 32) | i for i in range(n)], bvals=list(range(n))))
            for d8 in (1, -1, 127, -128):
                emit(gen_vp2_case(w, vl, "disp8=%d N=%d" % (d8, vl), 2, 21, 0, mem=Mem(d8 * vl)))
                emit(gen_vp2_case(w, vl, "bcst disp8=%d N=%d" % (d8, esz), 2, 21, 0,
                                  mem=Mem(d8 * esz), bcst=1))
        # faults (E4NF, nothing to mask)
        base = 0x10000 - MEM_RSI - 32
        c = Case("%s VL512 [end-32] #PF" % name)
        c.zmm[2] = rnd_bytes(64)
        c.mem[MEM_RSI + base] = rnd_bytes(32)
        for r in range(8):
            c.k[r] = RNG.getrandbits(64)
        c.code = evex(2, 3, w, 0x68, 2, Mem(base, disp32=True), vvvv=2, ll=2)
        c.fault = "#PF"
        emit(c)
        emit(gen_vp2_case(w, 32, "[end-32] fully mapped, no fault", 2, 2, 0, mem=Mem(base, disp32=True)))
        emit(gen_vp2_case(w, 64, "bcst [end-%d] mapped, no fault" % esz, 2, 2, 0,
                          mem=Mem(0x10000 - MEM_RSI - esz, disp32=True), bcst=1))
        c = Case("%s VL512 bcst on the unmapped page #PF" % name)
        c.zmm[2] = rnd_bytes(64)
        for r in range(8):
            c.k[r] = RNG.getrandbits(64)
        c.code = evex(2, 3, w, 0x68, 2, Mem(0x10000 - MEM_RSI, disp32=True), vvvv=2, ll=2, b=1)
        c.fault = "#PF"
        emit(c)
        # #UD
        uds = [("aaa = 001b (EVEX.aaa must be zero)", dict(aaa=1)),
               ("aaa = 111b", dict(aaa=7)),
               ("{z} (EVEX.z must be zero)", dict(z=1)),
               ("{z} with aaa = 010b", dict(z=1, aaa=2)),
               ("EVEX.b on a register form", dict(b=1)),
               ("L'L = 11b", dict(ll=3)),
               ("EVEX.R = 0 with a k register in ModRM.reg", dict(p0_r=0)),
               ("EVEX.R' = 0 with a k register in ModRM.reg", dict(p0_rr=0)),
               ("pp = 66", dict(pp=1)), ("pp = NP", dict(pp=0)), ("pp = F3", dict(pp=2))]
        for title, kw in uds:
            c = Case("%s %s #UD" % (name, title))
            pp = kw.pop("pp", 3)
            ll = kw.pop("ll", 2)
            c.code = evex(2, pp, w, 0x68, 2, 3, vvvv=4, ll=ll, **kw)
            c.fault = "#UD"
            emit(c)


# ---------------------------------------------------------------------------------------
# hardware cross-check: the GFNI / VAES / VPCLMULQDQ operation of every EVEX case on the
# host's VEX.256 / VEX.128 (and legacy for 128-bit data), with the same operand data
# ---------------------------------------------------------------------------------------
# name -> (map 2 = 0F38 / 3 = 0F3A, opcode, VEX.W)
HW_OPS = {
    "VGF2P8AFFINEQB": (3, 0xCE, 1), "VGF2P8AFFINEINVQB": (3, 0xCF, 1), "VGF2P8MULB": (2, 0xCF, 0),
    "VAESENC": (2, 0xDC, 0), "VAESENCLAST": (2, 0xDD, 0), "VAESDEC": (2, 0xDE, 0),
    "VAESDECLAST": (2, 0xDF, 0), "VPCLMULQDQ": (3, 0x44, 0),
}


def hw_cases():
    """[(line, input xmm1, input ymmh1, expected xmm1, expected ymmh1, "FORM encoding")]"""
    gen_all()
    out = []
    for name, vl, a, b, imm, res in HW_RECS:
        mm, opc, w = HW_OPS[name]
        has_imm = name in ("VGF2P8AFFINEQB", "VGF2P8AFFINEINVQB", "VPCLMULQDQ")
        for off in range(0, vl, 32):
            ch = min(32, vl - off)
            ca, cb, cr = a[off:off + ch], b[off:off + ch], res[off:off + ch]
            d1 = rnd_bytes(16)
            h1 = rnd_bytes(16)
            if ch == 32:
                code = vex3(mm, 1, w, 1, opc, 1, 3, 2, imm if has_imm else None)
                ins = "xmm1=%s ymmh1=%s xmm2=%s ymmh2=%s xmm3=%s ymmh3=%s" % (
                    hexs(d1), hexs(h1), hexs(ca[:16]), hexs(ca[16:]), hexs(cb[:16]), hexs(cb[16:]))
                out.append(("%s | %s" % (byte_list(code), ins), d1, h1, cr[:16], cr[16:], name + " VEX.256"))
            else:
                code = vex3(mm, 1, w, 0, opc, 1, 3, 2, imm if has_imm else None)
                ins = "xmm1=%s ymmh1=%s xmm2=%s xmm3=%s" % (hexs(d1), hexs(h1), hexs(ca), hexs(cb))
                out.append(("%s | %s" % (byte_list(code), ins), d1, h1, cr, bytes(16), name + " VEX.128"))
                # legacy: xmm1 is SRC1 and the destination; bits 255:128 unchanged
                code = legacy(0x38 if mm == 2 else 0x3A, opc, 1, 2, imm if has_imm else None)
                ins = "xmm1=%s ymmh1=%s xmm2=%s" % (hexs(ca), hexs(h1), hexs(cb))
                out.append(("%s | %s" % (byte_list(code), ins), ca, h1, cr, h1, name + " legacy"))
    return out


def hwcheck_gen(out):
    cases = hw_cases()
    out.write("# Hardware cross-check of the EVEX GFNI / VAES / VPCLMULQDQ model (ledger U572-U575):\n")
    out.write("# the operation of every EVEX case of cases_evex_m4b.txt on the i5-13600K's VEX.256 /\n")
    out.write("# VEX.128 / legacy forms with the same operand data (a 512-bit case = two VEX.256 halves;\n")
    out.write("# a 128-bit case = VEX.128 + legacy). Generated by Emulator/tools/isa/ref_evex_m4b.py\n")
    out.write("# --hwgen (do not edit). Hardware cases (no '=>'): Unicorn's VEX/legacy forms must equal\n")
    out.write("# the CPU (test.cmd hw_zero); ref_evex_m4b.py --hwcmp LOG compares the CPU's results with\n")
    out.write("# the model. %d cases.\n" % len(cases))
    for line, _d1, _h1, _e1, _eh, _k in cases:
        out.write(line + "\n")


def hwcheck_cmp(log_path):
    cases = hw_cases()
    cur, bad, uc_diff, seen = None, 0, 0, 0
    per = {}
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] (SAME|DIFF|KNOWN DEVIATION|HOST STATE)", line)
        if m:
            cur = int(m.group(1))
            if m.group(2) != "SAME":
                uc_diff += 1
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if m and cur is not None:
            txt = m.group(1)
            kv = dict(x.split("=", 1) for x in txt.split() if "=" in x)
            _l, d1, h1, e1, eh, kind = cases[cur]
            got1 = bytes.fromhex(kv["xmm1"]) if "xmm1" in kv else d1
            goth = bytes.fromhex(kv["ymmh1"]) if "ymmh1" in kv else h1
            key = kind
            per.setdefault(key, [0, 0])
            per[key][0] += 1
            seen += 1
            if txt.startswith("fault") or got1 != e1 or goth != eh:
                bad += 1
                per[key][1] += 1
                if bad <= 20:
                    print("[%d] %s: hw xmm1=%s ymmh1=%s, model %s %s" % (cur, kind, hexs(got1), hexs(goth),
                                                                         hexs(e1), hexs(eh)))
            cur = None
    for k in sorted(per):
        print("hwcheck %s: %d cases, %d differ from the model" % (k, per[k][0], per[k][1]))
    print("hwcheck: %d of %d hardware results compared, %d differ from the model "
          "(Unicorn vs hw not SAME: %d)" % (seen, len(cases), bad, uc_diff))
    return bad == 0 and seen == len(cases)


def hw_form_counts():
    hw_cases()
    cnt = {}
    for name, vl, *_ in HW_RECS:
        cnt[name] = cnt.get(name, 0) + 1
    return cnt


# ---------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    # encodings (SDM opcode tables; ModRM/disp8*N by hand)
    # VP2INTERSECTD k2, zmm2, zmm3: 62 F2 6F 48 68 D3
    chk("enc vp2intersectd", evex(2, 3, 0, 0x68, 2, 3, vvvv=2, ll=2),
        bytes([0x62, 0xF2, 0x6F, 0x48, 0x68, 0xD3]))
    # VGF2P8AFFINEQB zmm1{k1}{z}, zmm2, [rsi+0x40], 5 (N = 64): 62 F3 ED C9 CE 4E 01 05
    chk("enc vgf2p8affineqb", evex(3, 1, 1, 0xCE, 1, Mem(0x40), vvvv=2, ll=2, z=1, aaa=1, n=64, imm=5),
        bytes([0x62, 0xF3, 0xED, 0xC9, 0xCE, 0x4E, 0x01, 0x05]))
    # VAESENC zmm17, zmm30, zmm9: R' = 1, V' = 1 -> 62 C2 0D 40 DC C9
    chk("enc vaesenc", evex(2, 1, 0, 0xDC, 17, 9, vvvv=30, ll=2),
        bytes([0x62, 0xC2, 0x0D, 0x40, 0xDC, 0xC9]))
    # VEX.256 VPCLMULQDQ ymm1, ymm2, ymm3, 0x11: C4 E3 6D 44 CB 11
    chk("enc vex vpclmulqdq", vex3(3, 1, 0, 1, 0x44, 1, 3, 2, 0x11),
        bytes([0xC4, 0xE3, 0x6D, 0x44, 0xCB, 0x11]))
    # GF(2^8) inverse vs SDM Table 3-52 (row 0 and row F) and the text "inverse of 0x95 is 0x8A"
    chk("inv row 0", GF_INV[0:16], [0x00, 0x01, 0x8D, 0xF6, 0xCB, 0x52, 0x7B, 0xD1, 0xE8, 0x4F, 0x29,
                                     0xC0, 0xB0, 0xE1, 0xE5, 0xC7])
    chk("inv row F", GF_INV[0xF0:0x100], [0x5B, 0x23, 0x38, 0x34, 0x68, 0x46, 0x03, 0x8C, 0xDD, 0x9C,
                                          0x7D, 0xA0, 0xCD, 0x1A, 0x41, 0x1C])
    chk("inv 95", GF_INV[0x95], 0x8A)
    chk("gfmul 53*CA", gf2p8mul_byte(0x53, 0xCA), 0x01)        # FIPS-197 4.2 example pair
    chk("gfmul 57*83", gf2p8mul_byte(0x57, 0x83), 0xC1)        # FIPS-197 4.2
    chk("affine identity", affine_byte(IDENTITY, 0xB5, 0), 0xB5)
    chk("affine imm", affine_byte(0, 0xB5, 0x3C), 0x3C)
    # S-box (FIPS-197 Figure 7): S(00) = 63, S(53) = ED, S(FF) = 16; InvS(63) = 00
    chk("sbox", [SBOX[0], SBOX[0x53], SBOX[0xFF]], [0x63, 0xED, 0x16])
    chk("inv sbox", INV_SBOX[0x63], 0)
    # FIPS-197 Appendix C.1 AES-128: AESENC x9 + AESENCLAST, and the equivalent inverse
    # cipher with AESDEC x9 (InvMixColumns'd round keys) + AESDECLAST
    key = bytes(range(16))
    pt = bytes.fromhex("00112233445566778899aabbccddeeff")
    ct = bytes.fromhex("69c4e0d86a7b0430d8cdb78070b4c55a")
    rk = expand_key(key)
    s = bytes(x ^ y for x, y in zip(pt, rk[0]))
    for r in range(1, 10):
        s = aes_round(s, rk[r], "enc")
    s = aes_round(s, rk[10], "enclast")
    chk("aes-128 encrypt", s.hex(), ct.hex())
    s = bytes(x ^ y for x, y in zip(ct, rk[10]))
    for r in range(9, 0, -1):
        s = aes_round(s, inv_mix_key(rk[r]), "dec")
    s = aes_round(s, rk[0], "declast")
    chk("aes-128 decrypt", s.hex(), pt.hex())
    # PCLMUL: (x + 1)^2 = x^2 + 1; all ones^2: bit i set for even i <= 126
    chk("clmul 3*3", pclmul128(3, 3), 5)
    chk("clmul ones", pclmul128((1 << 64) - 1, (1 << 64) - 1),
        sum(1 << i for i in range(0, 127, 2)))
    chk("clmul top", pclmul128(1 << 63, 1 << 63), 1 << 126)
    # VP2INTERSECTD: src1 = {1,2,3,4}, src2 = {4,4,9,1} -> m0 = 1001b, m1 = 1011b
    chk("vp2intersect", vp2intersect(pack([1, 2, 3, 4], 4), pack([4, 4, 9, 1], 4), 4), (0b1001, 0b1011))
    return ok


def expand_key(key):
    """FIPS-197 5.2 KeyExpansion for AES-128 (selftest only): 11 round keys"""
    rcon = 1
    w = [list(key[4 * i:4 * i + 4]) for i in range(4)]
    for i in range(4, 44):
        t = list(w[i - 1])
        if i % 4 == 0:
            t = t[1:] + t[:1]
            t = [SBOX[x] for x in t]
            t[0] ^= rcon
            rcon = gf2p8mul_byte(rcon, 2)
        w.append([x ^ y for x, y in zip(w[i - 4], t)])
    return [bytes(sum(w[4 * r:4 * r + 4], [])) for r in range(11)]


def inv_mix_key(k):
    return bytes(_mix(list(k), INV_MIX))


def gen_all():
    del out_lines[:]
    del HW_RECS[:]
    RNG.seed(0x4B_2_5EED_570)
    comment("--- AVX512_VP2INTERSECT (ledger U570-U571)")
    gen_vp2()
    for fam, led in (("GFNI", "U572"), ("VAES", "U573"), ("VPCLMULQDQ", "U574")):
        comment("--- EVEX %s (ledger %s)" % (fam, led))
        for sp in VEC_FORMS:
            if sp["fam"] == fam:
                gen_vec_form(sp)


def main():
    if "--hwgen" in sys.argv:
        hwcheck_gen(sys.stdout)
        return
    if "--hwcmp" in sys.argv:
        sys.exit(0 if hwcheck_cmp(sys.argv[sys.argv.index("--hwcmp") + 1]) else 1)
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_all()
        out = sys.stdout
        out.write("# EVEX AVX512_VP2INTERSECT, GFNI, VAES and VPCLMULQDQ (ledger U570-U575): expected values\n")
        out.write("# from the independent SDM model Emulator/tools/isa/ref_evex_m4b.py --cases (regenerate,\n")
        out.write("# do not edit). The i5-13600K has no AVX-512: expected-value cases only, run with AVX-512\n")
        out.write("# enabled (every UC_X86_AVX512_* bit, incl. VP2INTERSECT):\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m4b.txt --avx512 --xcr0 0xE7 --expect-only\n")
        out.write("# RSI = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).\n")
        out.write("# The GFNI/VAES/VPCLMULQDQ operation of each case also runs on the CPU's VEX/legacy forms:\n")
        out.write("# cases_evex_m4b_hw.txt (ref_evex_m4b.py --hwgen / --hwcmp).\n")
        for l in out_lines:
            out.write(l + "\n")
        return
    print(__doc__)


if __name__ == "__main__":
    main()
