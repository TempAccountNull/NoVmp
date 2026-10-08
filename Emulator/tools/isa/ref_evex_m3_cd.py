#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m3_cd.py -- independent reference model (Python 3 stdlib only) of the EVEX
AVX512CD instructions (ledger U320-U321) and of further AVX-512 extensions (U322-U325),
and generator of the expected-value case file Emulator/data/cases_evex_m3_cd.txt.

Written from the Intel SDM text only (Vol2A 2.7 "Intel AVX-512 encoding", Tables 2-36 ..
2-45, 2.8 exception classes E4 / E4NF / E6NF; Vol2C instruction pages, "Operation"
pseudocode), not from any C implementation:

  VPCONFLICTD/Q   EVEX.128/256/512.66.0F38.W0/W1 C4 /r   Full tuple, {k1}{z}, {1toN}, E4NF
  VPLZCNTD/Q      EVEX.128/256/512.66.0F38.W0/W1 44 /r   Full tuple, {k1}{z}, {1toN}, E4
  VPBROADCASTMB2Q EVEX.128/256/512.F3.0F38.W1 2A /r      zmm1, k1 (register only), E6NF
  VPBROADCASTMW2D EVEX.128/256/512.F3.0F38.W0 3A /r      zmm1, k1 (register only), E6NF
  AVX512_IFMA (U322):
  VPMADD52LUQ/HUQ EVEX.128/256/512.66.0F38.W1 B4/B5 /r   Full tuple, {1to8}, E4; DEST is the
                                                          accumulator
  AVX512_VPOPCNTDQ (U323):
  VPOPCNTD/Q      EVEX.128/256/512.66.0F38.W0/W1 55 /r   Full tuple, {1toN}, E4
  AVX512_BITALG (U324):
  VPOPCNTB/W      EVEX.128/256/512.66.0F38.W0/W1 54 /r   Full Mem tuple (no {1toN}), E4
  VPSHUFBITQMB    EVEX.128/256/512.66.0F38.W0 8F /r      k1{k2}, Full Mem tuple, E4
  AVX512_VBMI (U325; VPMULTISHIFTQB not modelled - not implemented yet):
  VPERMB          EVEX.128/256/512.66.0F38.W0 8D /r      Full Mem tuple, E4NF.nb
  VPERMI2B        EVEX.128/256/512.66.0F38.W0 75 /r      Full Mem tuple, E4NF.nb (index = DEST)
  VPERMT2B        EVEX.128/256/512.66.0F38.W0 7D /r      Full Mem tuple, E4NF.nb (table = DEST)

CPUID: AVX512CD (EVEX.512) and AVX512VL AND AVX512CD (EVEX.128/256), likewise for every
extension; emu-alltest --avx512 enables all the UC_X86_AVX512_* bits. EVEX.vvvv is
reserved (1111b, V' = 1) for the four AVX512CD instructions.

Generic EVEX wrappers (SDM pseudocode of every page):
  MASK     FOR j := 0 TO KL-1: IF k1[j] OR *no writemask* THEN DEST[j] := op ELSE
           (*merging*: unchanged | *zeroing*: 0); DEST[MAXVL-1:VL] := 0
  BCST     IF (EVEX.b = 1) AND (SRC *is memory*) THEN SRC[j] := SRC[0] (one element)
  LOAD     E4: masked-off elements are not read (fault suppression); E4NF (VPCONFLICT): the
           whole operand is read whatever the mask (Table 2-52: #PF "For a page fault")
  VPBROADCASTM: no masking (Table 2-42 note 1: aaa != 000b #UD; z only with aaa: #UD),
           EVEX.b on a register form #UD (Table 2-43), ModRM.mod != 11b #UD (register-only
           operand k1), EVEX.B/X ignored for a k register in ModRM.r/m (Table 2-41).

Usage:
  python ref_evex_m3_cd.py --selftest   hand-derived checks of the model, exit 0 on pass
  python ref_evex_m3_cd.py --cases      Emulator/data/cases_evex_m3_cd.txt (stdout)
  python ref_evex_m3_cd.py --hwgen OUT.json
                                        hardware cases (stdout): scalar LZCNT r32/r64 on the
                                        element values of the VPLZCNT cases; OUT.json = model
  python ref_evex_m3_cd.py --hwcmp LOG OUT.json
                                        compare the host's LZCNT results ("hw:" lines of
                                        emu-alltest --cases) with the VPLZCNT element model
"""

import random
import sys

# harness layout (Emulator/tests/alltest/at_engine.hpp): RSI = R14 = MEM + 0x8000, operand
# memory MEM .. MEM + 0xFFFF mapped, MEM + 0x10000 unmapped
MEM_RSI = 0x8000
RSI, R14 = 6, 14


# ---------------------------------------------------------------------------------------
# EVEX encoding (SDM Vol2A 2.7.1 Table 2-32: P0 = R X B R' 0 m m m, P1 = W v v v v 1 p p,
# P2 = z L' L b V' a a a; register fields stored inverted)
# ---------------------------------------------------------------------------------------
class Mem:
    def __init__(self, base=RSI, disp=0, disp32=False):
        self.base, self.disp, self.disp32 = base, disp, disp32


def evex(mmm, pp, w, opc, reg, rm, vvvv=None, ll=0, b=0, z=0, aaa=0, n=1, rm_bx=None,
         p2_vp=None):
    """bytes of one EVEX instruction; reg / rm 0-31 (rm may be Mem); vvvv None = 1111b, V'=1.
    n = disp8*N of the form. rm_bx = (B, X) bits to store for a register rm (default: from rm)."""
    r, rr = (reg >> 3) & 1, (reg >> 4) & 1
    if isinstance(rm, Mem):
        x = 0
        bb = (rm.base >> 3) & 1
        d = rm.disp
        if not rm.disp32 and d % n == 0 and -128 <= d // n <= 127 and d != 0:
            mod, db = 1, bytes([(d // n) & 0xFF])
        elif d == 0 and not rm.disp32 and (rm.base & 7) != 5:
            mod, db = 0, b""
        else:
            mod, db = 2, (d & 0xFFFFFFFF).to_bytes(4, "little")
        tail = bytes([(mod << 6) | ((reg & 7) << 3) | (rm.base & 7)])
        if (rm.base & 7) == 4:
            tail += bytes([0x24])
        tail += db
    else:
        bb, x = (rm >> 3) & 1, (rm >> 4) & 1
        if rm_bx is not None:
            bb, x = rm_bx
        tail = bytes([0xC0 | ((reg & 7) << 3) | (rm & 7)])
    v = 0 if vvvv is None else vvvv
    p0 = ((r ^ 1) << 7) | ((x ^ 1) << 6) | ((bb ^ 1) << 5) | ((rr ^ 1) << 4) | mmm
    p1 = (w << 7) | (((~v) & 0xF) << 3) | 4 | pp
    vp = ((v >> 4) & 1) ^ 1 if p2_vp is None else p2_vp
    p2 = (z << 7) | (ll << 5) | (b << 4) | (vp << 3) | aaa
    return bytes([0x62, p0, p1, p2, opc]) + tail


def byte_list(bs):
    return ".byte " + ", ".join("0x%02x" % c for c in bs)


def elems(buf, esz):
    return [int.from_bytes(buf[i:i + esz], "little") for i in range(0, len(buf), esz)]


def pack(vals, esz):
    return b"".join((v & ((1 << (8 * esz)) - 1)).to_bytes(esz, "little") for v in vals)


def hexs(buf):
    return "".join("%02X" % c for c in buf)


# ---------------------------------------------------------------------------------------
# instruction models (Vol2C "Operation")
# ---------------------------------------------------------------------------------------
def lzcnt(x, bits):
    """VPLZCNTD/Q: temp := bits; DEST := 0; WHILE temp > 0 AND SRC[temp-1] = 0: temp--, DEST++"""
    temp, d = bits, 0
    while temp > 0 and not (x >> (temp - 1)) & 1:
        temp -= 1
        d += 1
    return d


def conflict(src, j):
    """VPCONFLICTD/Q element j: DEST[i+k] := (SRC[j] = SRC[k]) for k < j; DEST[i+..:i+j] := 0"""
    r = 0
    for k in range(j):
        if src[j] == src[k]:
            r |= 1 << k
    return r


def broadcastm(kval, esz):
    """VPBROADCASTMB2Q (esz 8): ZeroExtend(SRC[7:0]); VPBROADCASTMW2D (esz 4): SRC[15:0]"""
    return kval & (0xFF if esz == 8 else 0xFFFF)


def mask_merge(old, res, esz, vl, kmask, zero):
    """MASK wrapper + DEST[MAXVL-1:VL] := 0 (old: 64-byte register, res: element values)"""
    o = elems(old[:vl], esz)
    out = []
    for j in range(vl // esz):
        if kmask is None or (kmask >> j) & 1:
            out.append(res[j])
        else:
            out.append(0 if zero else o[j])
    return pack(out, esz) + bytes(64 - vl)


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


RNG = random.Random(0xCD_5EED_320)
VL_LL = {16: 0, 32: 1, 64: 2}
out_lines = []
LZ_VALUES = {4: [], 8: []}           # element values of the VPLZCNT cases (hardware check)


def emit(c):
    out_lines.append("# " + c.title)
    out_lines.append(c.line())


def comment(t):
    out_lines.append("# " + t)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


def rnd_elem(esz, kind):
    bits = 8 * esz
    if kind == "lz":
        # every leading-zero count is reachable: a random value shifted right by 0..bits
        s = RNG.randrange(bits + 1)
        v = RNG.getrandbits(bits) | (1 << (bits - 1))
        return v >> s
    if kind == "dup":
        # few distinct values so conflicts are frequent (also values equal except the top bit)
        pool = [0, 1, (1 << bits) - 1, 1 << (bits - 1), 0x5A5A5A5A5A5A5A5A & ((1 << bits) - 1),
                (1 << (bits - 1)) | 1]
        return RNG.choice(pool)
    return RNG.getrandbits(bits)


# name, pp, opcode, W, kind, fault-suppressing load (E4) or not (E4NF)
CD_FORMS = [
    ("VPCONFLICTD", 1, 0xC4, 0, "conflict", False),
    ("VPCONFLICTQ", 1, 0xC4, 1, "conflict", False),
    ("VPLZCNTD", 1, 0x44, 0, "lzcnt", True),
    ("VPLZCNTQ", 1, 0x44, 1, "lzcnt", True),
]


def op_vector(kind, src, esz):
    if kind == "conflict":
        return [conflict(src, j) for j in range(len(src))]
    return [lzcnt(x, 8 * esz) for x in src]


def gen_rm(form, vl, title, dst, src, kreg=0, kval=None, z=0, mem=None, bcst=0, svals=None):
    """VPCONFLICT / VPLZCNT zmm1{k}{z}, zmm2/m/{1toN}"""
    name, pp, opc, w, kind, fs = form
    esz = 8 if w else 4
    n = vl // esz
    c = Case("%s VL%d %s" % (name, vl * 8, title))
    ek = "dup" if kind == "conflict" and RNG.random() < 0.75 else ("lz" if kind == "lzcnt" else "r")
    if svals is None:
        svals = [rnd_elem(esz, ek) for _ in range(1 if bcst else n)]
    if mem is None:
        img = pack(svals, esz) + rnd_bytes(64 - vl)
        if src == dst:
            c.zmm[dst] = img
        else:
            c.zmm[src] = img
            c.zmm[dst] = rnd_bytes(64)
        rm, nn = src, 1
    else:
        c.zmm[dst] = rnd_bytes(64)
        c.mem[MEM_RSI + mem.disp] = pack(svals, esz)
        rm, nn = mem, (esz if bcst else vl)
    vec = svals * n if bcst else svals
    if kind == "lzcnt":
        LZ_VALUES[esz].extend(vec)
    if kreg:
        c.k[kreg] = kval
    res = op_vector(kind, vec, esz)
    out = mask_merge(c.zmm[dst], res, esz, vl, kval if kreg else None, z)
    c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    c.code = evex(2, pp, w, opc, dst, rm, ll=VL_LL[vl], b=bcst, z=z, aaa=kreg, n=nn)
    return c


def gen_cd_vector():
    comment("--- VPCONFLICTD/Q (E4NF) and VPLZCNTD/Q (E4): Full tuple, {k1}{z}, {1toN}")
    for form in CD_FORMS:
        name, pp, opc, w, kind, fs = form
        esz = 8 if w else 4
        comment("%s (EVEX.66.0F38.W%d %02X /r)" % (name, w, opc))
        for vl in (16, 32, 64):
            emit(gen_rm(form, vl, "nomask", 1, 2))
            emit(gen_rm(form, vl, "merge zmm16+", 17, 30, kreg=3, kval=RNG.getrandbits(64)))
            emit(gen_rm(form, vl, "zero", 4, 25, kreg=7, kval=RNG.getrandbits(64), z=1))
            emit(gen_rm(form, vl, "mem", 6, 0, mem=Mem(RSI, 0x40)))
            emit(gen_rm(form, vl, "mem merge", 9, 0, kreg=5, kval=RNG.getrandbits(64), mem=Mem(RSI, 0x40)))
            emit(gen_rm(form, vl, "bcst merge", 8, 0, kreg=2, kval=RNG.getrandbits(64), mem=Mem(RSI, 0x40),
                        bcst=1))
            emit(gen_rm(form, vl, "bcst nomask", 10, 0, mem=Mem(RSI, -0x40), bcst=1))
            emit(gen_rm(form, vl, "dst=src merge", 11, 11, kreg=1, kval=RNG.getrandbits(64)))
            emit(gen_rm(form, vl, "dst=src nomask", 12, 12))
        n = 64 // esz
        if kind == "conflict":
            emit(gen_rm(form, 64, "all equal", 13, 14, svals=[0x1234] * n))
            emit(gen_rm(form, 64, "all distinct", 13, 14, svals=list(range(n))))
            emit(gen_rm(form, 64, "differ in the top bit only", 13, 14,
                        svals=[(i & 1) << (8 * esz - 1) for i in range(n)]))
            emit(gen_rm(form, 64, "all equal zero-masked", 13, 14, svals=[7] * n, kreg=4,
                        kval=0x5555, z=1))
        else:
            bits = 8 * esz
            vals = [0, 1, (1 << bits) - 1, 1 << (bits - 1)] + [1 << i for i in range(bits)]
            for i in range(0, len(vals), n):
                chunk = (vals[i:i + n] + [0] * n)[:n]
                emit(gen_rm(form, 64, "edge values %d" % (i // n), 13, 14, svals=chunk))
        # disp8*N: Full tuple, N = VL (no EVEX.b) or the element size ({1toN})
        for vl in (16, 32, 64):
            for bcst in (0, 1):
                nn = esz if bcst else vl
                for d8 in (1, -1, 127, -127):
                    emit(gen_rm(form, vl, "disp8=%d N=%d" % (d8, nn), 15, 0, mem=Mem(RSI, d8 * nn),
                                bcst=bcst))
        # #UD
        for title, kw in (("vvvv != 1111b", dict(vvvv=5)), ("V' = 0 (vvvv unused)", dict(p2_vp=0)),
                          ("EVEX.b on a register form", dict(b=1)), ("{z} with aaa = 000b", dict(z=1)),
                          ("L'L = 11b", dict(ll=3))):
            c = Case("%s %s #UD" % (name, title))
            ll = kw.pop("ll", 2)
            c.code = evex(2, pp, w, opc, 1, 2, ll=ll, **kw)
            c.fault = "#UD"
            emit(c)
        c = Case("%s pp = NP #UD" % name)
        c.code = evex(2, 0, w, opc, 1, 2, ll=2)
        c.fault = "#UD"
        emit(c)


def gen_fault_suppression():
    comment("--- masked memory next to the unmapped page MEM+0x10000: VPLZCNT (E4) suppresses")
    comment("    faults of masked-off elements, VPCONFLICT (E4NF) reads the whole operand")
    base = 0x10000 - MEM_RSI - 32
    for form in CD_FORMS:
        name, pp, opc, w, kind, fs = form
        esz = 8 if w else 4
        nlo = 32 // esz
        kval = (1 << nlo) - 1
        svals = [rnd_elem(esz, "lz") for _ in range(nlo)]
        c = Case("%s zmm, [end-32] {k: mapped elements only} %s" % (name, "no fault" if fs else "#PF"))
        c.zmm[3] = rnd_bytes(64)
        c.k[1] = kval
        c.mem[MEM_RSI + base] = pack(svals, esz)
        c.code = evex(2, pp, w, opc, 3, Mem(RSI, base, disp32=True), ll=2, z=1, aaa=1)
        if fs:
            res = op_vector(kind, svals + [0] * (64 // esz - nlo), esz)
            c.exp.append("zmm3=%s" % hexs(mask_merge(c.zmm[3], res, esz, 64, kval, 1)))
        else:
            c.fault = "#PF"
        emit(c)
        c = Case("%s zmm, [end-32] {k: one unmapped element} #PF" % name)
        c.zmm[3] = rnd_bytes(64)
        c.k[1] = kval | (1 << nlo)
        c.mem[MEM_RSI + base] = pack(svals, esz)
        c.code = evex(2, pp, w, opc, 3, Mem(RSI, base, disp32=True), ll=2, z=1, aaa=1)
        c.fault = "#PF"
        emit(c)
        c = Case("%s {1toN} k=0 on the unmapped page %s" % (name, "no fault" if fs else "#PF"))
        c.zmm[3] = rnd_bytes(64)
        c.k[6] = 0
        c.code = evex(2, pp, w, opc, 3, Mem(RSI, 0x10000 - MEM_RSI, disp32=True), ll=2, b=1, aaa=6)
        if fs:
            c.exp.append("zmm3=%s" % hexs(c.zmm[3]))
        else:
            c.fault = "#PF"
        emit(c)


# name, W, opcode, element size
BM_FORMS = [("VPBROADCASTMB2Q", 1, 0x2A, 8), ("VPBROADCASTMW2D", 0, 0x3A, 4)]


def gen_broadcastm():
    comment("--- VPBROADCASTMB2Q / VPBROADCASTMW2D zmm, k (E6NF, no masking, register only)")
    for name, w, opc, esz in BM_FORMS:
        comment("%s (EVEX.F3.0F38.W%d %02X /r)" % (name, w, opc))
        for vl in (16, 32, 64):
            for dst, kr in ((1, 2), (29, 7), (5, 0)):
                c = Case("%s VL%d zmm%d, k%d" % (name, vl * 8, dst, kr))
                c.zmm[dst] = rnd_bytes(64)
                kval = RNG.getrandbits(64)
                c.k[kr] = kval
                v = broadcastm(kval, esz)
                c.exp.append("zmm%d=%s" % (dst, hexs(pack([v] * (vl // esz), esz) + bytes(64 - vl))))
                c.code = evex(2, 2, w, opc, dst, kr, ll=VL_LL[vl])
                emit(c)
        for kval in (0, 0xFF, 0xFFFF, 0x8000000000000080, 0xFFFFFFFFFFFF0000):
            c = Case("%s zmm3, k4 = 0x%X" % (name, kval))
            c.zmm[3] = rnd_bytes(64)
            c.k[4] = kval
            v = broadcastm(kval, esz)
            c.exp.append("zmm3=%s" % hexs(pack([v] * (64 // esz), esz)))
            c.code = evex(2, 2, w, opc, 3, 4, ll=2)
            emit(c)
        # EVEX.B / EVEX.X are ignored for a k register in ModRM.r/m (Table 2-41)
        for bx in ((1, 0), (0, 1), (1, 1)):
            c = Case("%s zmm3, k5 with EVEX.B=%d X=%d (ignored)" % (name, bx[0], bx[1]))
            c.zmm[3] = rnd_bytes(64)
            kval = RNG.getrandbits(64)
            c.k[5] = kval
            c.exp.append("zmm3=%s" % hexs(pack([broadcastm(kval, esz)] * (64 // esz), esz)))
            c.code = evex(2, 2, w, opc, 3, 5, ll=2, rm_bx=bx)
            emit(c)
        # #UD conditions
        uds = [
            ("aaa != 000b (no masking, Table 2-42)", dict(aaa=1)),
            ("aaa = 111b", dict(aaa=7)),
            ("{z} with aaa = 000b", dict(z=1)),
            ("{z} with aaa = 001b", dict(z=1, aaa=1)),
            ("EVEX.b = 1 (register form, Table 2-43)", dict(b=1)),
            ("vvvv != 1111b", dict(vvvv=2)),
            ("V' = 0", dict(p2_vp=0)),
            ("L'L = 11b", dict(ll=3)),
            ("wrong EVEX.W", dict(w=1 - w)),
        ]
        for title, kw in uds:
            c = Case("%s %s #UD" % (name, title))
            ww = kw.pop("w", w)
            ll = kw.pop("ll", 2)
            c.zmm[3] = rnd_bytes(64)
            c.code = evex(2, 2, ww, opc, 3, 1, ll=ll, **kw)
            c.fault = "#UD"
            emit(c)
        c = Case("%s with a memory operand (ModRM.mod != 11b) #UD" % name)
        c.code = evex(2, 2, w, opc, 3, Mem(RSI, 0x40), ll=2)
        c.fault = "#UD"
        emit(c)
        c = Case("%s with pp = NP (unassigned) #UD" % name)
        c.code = evex(2, 0, w, opc, 3, 1, ll=2)
        c.fault = "#UD"
        emit(c)


# ---------------------------------------------------------------------------------------
# further AVX-512 extensions (ledger U322-U325), one generic generator:
#   rvm  zmm1{k}{z}, zmm2 (vvvv), zmm3/m/{1toN}     rm  zmm1{k}{z}, zmm2/m/{1toN}
#   kvm  k1{k2}, zmm2 (vvvv), zmm3/m                 (DEST read: "dsrc" forms)
# form keys: name pp opc w layout fn mesz (mask element bytes) besz ({1toN} element bytes,
# None = Full Mem tuple, no broadcast) fs (fault suppression: class E4, else E4NF)
# fn(d, a, b, vl) -> result bytes (rvm/rm) or mask bits (kvm); d / a / b: vl-byte images
# ---------------------------------------------------------------------------------------
M52 = (1 << 52) - 1
M64 = (1 << 64) - 1


def madd52(d, a, b, hi):
    """VPMADD52LUQ/HUQ (Vol2C, EVEX Operation): Temp128 := ZX64(src1[51:0]) * ZX64(tsrc2[51:0]);
    DEST := DEST + ZX64(Temp128[51:0]) (LUQ) or ZX64(Temp128[103:52]) (HUQ), mod 2^64"""
    t = (a & M52) * (b & M52)
    return (d + ((t >> 52) & M52 if hi else t & M52)) & M64


def qmap(fn):
    """element function on qwords -> vector function"""
    def vec(d, a, b, vl):
        return pack([fn(x, y, z) for x, y, z in zip(elems(d, 8), elems(a, 8), elems(b, 8))], 8)
    return vec


EXT_FORMS = {
    "IFMA": [
        dict(name="VPMADD52LUQ", pp=1, opc=0xB4, w=1, layout="rvm", mesz=8, besz=8, fs=True,
             dsrc=True, fn=qmap(lambda d, a, b: madd52(d, a, b, False))),
        dict(name="VPMADD52HUQ", pp=1, opc=0xB5, w=1, layout="rvm", mesz=8, besz=8, fs=True,
             dsrc=True, fn=qmap(lambda d, a, b: madd52(d, a, b, True))),
    ],
    "VPOPCNTDQ": [
        dict(name="VPOPCNTD", pp=1, opc=0x55, w=0, layout="rm", mesz=4, besz=4, fs=True,
             fn=lambda d, a, b, vl: pack([popcnt(x) for x in elems(b, 4)], 4)),
        dict(name="VPOPCNTQ", pp=1, opc=0x55, w=1, layout="rm", mesz=8, besz=8, fs=True,
             fn=lambda d, a, b, vl: pack([popcnt(x) for x in elems(b, 8)], 8)),
    ],
    "BITALG": [
        dict(name="VPOPCNTB", pp=1, opc=0x54, w=0, layout="rm", mesz=1, besz=None, fs=True,
             fn=lambda d, a, b, vl: pack([popcnt(x) for x in elems(b, 1)], 1)),
        dict(name="VPOPCNTW", pp=1, opc=0x54, w=1, layout="rm", mesz=2, besz=None, fs=True,
             fn=lambda d, a, b, vl: pack([popcnt(x) for x in elems(b, 2)], 2)),
        dict(name="VPSHUFBITQMB", pp=1, opc=0x8F, w=0, layout="kvm", mesz=1, besz=None, fs=True,
             fn=lambda d, a, b, vl: shufbitqmb(a, b, vl)),
    ],
}
EXT_FORMS["VBMI"] = [
    dict(name="VPERMB", pp=1, opc=0x8D, w=0, layout="rvm", mesz=1, besz=None, fs=False, wsib=True,
         fn=lambda d, a, b, vl: permb(a, b, vl)),
    dict(name="VPERMI2B", pp=1, opc=0x75, w=0, layout="rvm", mesz=1, besz=None, fs=False, wsib=True,
         dsrc=True, fn=lambda d, a, b, vl: permi2b(d, a, b, vl)),
    dict(name="VPERMT2B", pp=1, opc=0x7D, w=0, layout="rvm", mesz=1, besz=None, fs=False, wsib=True,
         dsrc=True, fn=lambda d, a, b, vl: permt2b(d, a, b, vl)),
]
EXT_ORDER = ["IFMA", "VPOPCNTDQ", "BITALG", "VBMI"]


def permb(idx, table, vl):
    """VPERMB (Vol2C): id := SRC1[j*8+n:j*8] (n = 3/4/5 for VL 128/256/512);
    DEST.byte[j] := SRC2.byte[id]"""
    return bytes(table[idx[j] & (vl - 1)] for j in range(vl))


def permi2b(dest_idx, src1, src2, vl):
    """VPERMI2B (Vol2C): the index is the destination (Description: "using the byte indices in
    the first operand (the destination operand)"; the Operation box selects the table with
    TMP_DEST[j*8+id+1] but writes 'off := 8*SRC1[...]', which would make SRC1 both index and
    table - read as TMP_DEST); bit id+1 = log2(VL) selects SRC2 (1) or SRC1 (0)"""
    out = []
    for j in range(vl):
        i = dest_idx[j]
        out.append(src2[i & (vl - 1)] if i & vl else src1[i & (vl - 1)])
    return bytes(out)


def permt2b(dest_tab, idx, src2, vl):
    """VPERMT2B (Vol2C): off := SRC1.byte[j] & (VL-1);
    DEST.byte[j] := SRC1[j*8+id+1] ? SRC2.byte[off] : TMP_DEST.byte[off]"""
    out = []
    for j in range(vl):
        i = idx[j]
        out.append(src2[i & (vl - 1)] if i & vl else dest_tab[i & (vl - 1)])
    return bytes(out)


def shufbitqmb(src1, src2, vl):
    """VPSHUFBITQMB (Vol2C): FOR i (qword), j (byte): m := SRC2.qword[i].byte[j] & 0x3F;
    k1[i*8+j] := SRC1.qword[i].bit[m]  (k2 / KL handled by the generic generator)"""
    r = 0
    for i in range(vl // 8):
        q = int.from_bytes(src1[8 * i:8 * i + 8], "little")
        for j in range(8):
            r |= ((q >> (src2[8 * i + j] & 0x3F)) & 1) << (8 * i + j)
    return r


def popcnt(x):
    """VPOPCNT (Vol2C): POPCNT(element), the number of bits set to 1"""
    return bin(x).count("1")


def ext_vals(sp, n_bytes):
    """random operand bytes, with edge patterns now and then"""
    r = RNG.random()
    if r < 0.15:
        return bytes([0xFF]) * n_bytes
    if r < 0.25:
        return bytes(n_bytes)
    return rnd_bytes(n_bytes)


def gen_ext_case(sp, vl, title, dst, s1, s2, kreg=0, kval=None, z=0, mem=None, bcst=0,
                 aimg=None, bimg=None, dimg=None):
    lay = sp["layout"]
    if lay == "kvm":
        dst &= 7                        # k0-k7 (ModRM.reg with EVEX.R = R' = 0)
    c = Case("%s VL%d %s" % (sp["name"], vl * 8, title))
    regs = {}
    if lay in ("rvm", "kvm"):
        regs[s1] = (aimg if aimg is not None else ext_vals(sp, vl)) + rnd_bytes(64 - vl)
    if mem is None:
        if s2 not in regs:
            regs[s2] = (bimg if bimg is not None else ext_vals(sp, vl)) + rnd_bytes(64 - vl)
        b = regs[s2][:vl]
        rm, nn = s2, 1
    else:
        data = bimg if bimg is not None else ext_vals(sp, sp["besz"] if bcst else vl)
        c.mem[MEM_RSI + mem.disp] = data
        b = data * (vl // len(data)) if bcst else data
        rm, nn = mem, (sp["besz"] if bcst else vl)
    if lay != "kvm" and dst not in regs:
        regs[dst] = (dimg if dimg is not None else rnd_bytes(vl)) + rnd_bytes(64 - vl)
    c.zmm.update(regs)
    a = regs[s1][:vl] if lay in ("rvm", "kvm") else bytes(vl)
    if kreg:
        c.k[kreg] = kval
    kmask = kval if kreg else None
    if lay == "kvm":
        r = sp["fn"](None, a, b, vl)
        if kmask is not None:
            r &= kmask
        r &= (1 << (vl // sp["mesz"])) - 1
        c.k.setdefault(dst, RNG.getrandbits(64))
        c.exp.append("k%d=0x%X" % (dst, r))
    else:
        d = regs[dst][:vl]
        res = elems(sp["fn"](d, a, b, vl), sp["mesz"])
        out = mask_merge(regs[dst], res, sp["mesz"], vl, kmask, z)
        c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    vvvv = s1 if lay in ("rvm", "kvm") else None
    c.code = evex(2, sp["pp"], sp["w"], sp["opc"], dst, rm, vvvv=vvvv, ll=VL_LL[vl], b=bcst, z=z,
                  aaa=kreg, n=nn)
    return c


def gen_ext(ext):
    for sp in EXT_FORMS[ext]:
        lay = sp["layout"]
        kd = lay == "kvm"
        mb = 64 // sp["mesz"]
        comment("%s (EVEX.%s.0F38.W%d %02X /r), %s" % (sp["name"], ["NP", "66", "F3", "F2"][sp["pp"]],
                sp["w"], sp["opc"], "E4" if sp["fs"] else "E4NF"))
        for vl in (16, 32, 64):
            emit(gen_ext_case(sp, vl, "nomask", 1, 2, 3))
            emit(gen_ext_case(sp, vl, "merge zmm16+", 5 if kd else 17, 30, 9, kreg=3,
                              kval=RNG.getrandbits(64)))
            if not kd:
                emit(gen_ext_case(sp, vl, "zero", 4, 25, 26, kreg=7, kval=RNG.getrandbits(64), z=1))
            emit(gen_ext_case(sp, vl, "mem", 6, 8, 0, mem=Mem(RSI, 0x40)))
            emit(gen_ext_case(sp, vl, "mem masked", 7, 8, 0, mem=Mem(RSI, 0x40), kreg=5,
                              kval=RNG.getrandbits(64)))
            if sp["besz"]:
                emit(gen_ext_case(sp, vl, "bcst merge", 10, 11, 0, kreg=2, kval=RNG.getrandbits(64),
                                  mem=Mem(RSI, 0x40), bcst=1))
                emit(gen_ext_case(sp, vl, "bcst nomask", 12, 13, 0, mem=Mem(RSI, -0x40), bcst=1))
        # destination = each source (the destination is read by some forms)
        if not kd:
            emit(gen_ext_case(sp, 64, "dst=src2 merge", 14, 15, 14, kreg=1, kval=RNG.getrandbits(64)))
            if lay == "rvm":
                emit(gen_ext_case(sp, 64, "dst=src1 zero", 16, 16, 18, kreg=6, kval=RNG.getrandbits(64), z=1))
                emit(gen_ext_case(sp, 64, "dst=src1=src2", 19, 19, 19))
        else:
            emit(gen_ext_case(sp, 64, "src1=src2", 2, 20, 20))
        # disp8*N
        for vl in (16, 32, 64):
            for bcst in ((0, 1) if sp["besz"] else (0,)):
                nn = sp["besz"] if bcst else vl
                for d8 in (1, -1, 127, -127):
                    emit(gen_ext_case(sp, vl, "disp8=%d N=%d" % (d8, nn), 21, 22, 0,
                                      mem=Mem(RSI, d8 * nn), bcst=bcst))
        # memory operand next to the unmapped page: E4 suppresses faults of masked-off
        # elements, E4NF reads the whole operand
        base = 0x10000 - MEM_RSI - 32
        nlo = 32 // sp["mesz"]
        c = Case("%s VL512 [end-32] {k: mapped elements only} %s" % (sp["name"],
                 "no fault" if sp["fs"] else "#PF"))
        c.zmm[3] = rnd_bytes(64)
        if lay != "rm":
            c.zmm[4] = rnd_bytes(64)
        c.k[1] = (1 << nlo) - 1
        emit(fs_case(sp, c, base))
        # #UD
        uds = [("EVEX.b on a register form", dict(b=1)), ("L'L = 11b", dict(ll=3))]
        # (wsib: the other EVEX.W is an AVX512BW form - VPERMW, VPERMI2W, VPERMT2W)
        if not sp.get("wsib") and not any(o["opc"] == sp["opc"] and o["pp"] == sp["pp"] and
                                          o["w"] != sp["w"] for e in EXT_FORMS.values() for o in e):
            uds.append(("wrong EVEX.W", dict(w=1 - sp["w"])))
        if kd:
            uds.append(("{z} (k destination)", dict(z=1, aaa=1)))
        if lay == "rm":
            uds += [("vvvv != 1111b", dict(vvvv=5)), ("V' = 0 (vvvv unused)", dict(p2_vp=0))]
        for title, kw in uds:
            c = Case("%s %s #UD" % (sp["name"], title))
            w = kw.pop("w", sp["w"])
            ll = kw.pop("ll", 2)
            if lay != "rm":
                kw.setdefault("vvvv", 2)
            c.code = evex(2, sp["pp"], w, sp["opc"], 1, 3, ll=ll, **kw)
            c.fault = "#UD"
            emit(c)
        if not sp["besz"]:
            c = Case("%s EVEX.b with memory (Full Mem tuple) #UD" % sp["name"])
            c.code = evex(2, sp["pp"], sp["w"], sp["opc"], 1, Mem(RSI, 0x40), vvvv=2 if lay != "rm" else None,
                          ll=2, b=1)
            c.fault = "#UD"
            emit(c)


def fs_case(sp, c, base):
    """rebuild a fault-suppression case: 32 mapped bytes, the upper half unmapped"""
    lay = sp["layout"]
    c2 = Case(c.title)
    c2.zmm, c2.k = dict(c.zmm), dict(c.k)
    data = rnd_bytes(32)
    c2.mem[MEM_RSI + base] = data
    c2.code = evex(2, sp["pp"], sp["w"], sp["opc"], 3, Mem(RSI, base, disp32=True),
                   vvvv=4 if lay != "rm" else None, ll=2, aaa=1)
    if not sp["fs"]:
        c2.fault = "#PF"
        return c2
    vl = 64
    kval = c.k[1]
    b = data + bytes(32)                # masked-off elements: never read (their value is unused)
    a = c2.zmm[4][:vl] if lay != "rm" else bytes(vl)
    if lay == "kvm":
        r = sp["fn"](None, a, b, vl) & kval
        c2.k.setdefault(3, RNG.getrandbits(64))
        c2.exp.append("k3=0x%X" % r)
    else:
        res = elems(sp["fn"](c2.zmm[3][:vl], a, b, vl), sp["mesz"])
        c2.exp.append("zmm3=%s" % hexs(mask_merge(c2.zmm[3], res, sp["mesz"], vl, kval, 0)))
    return c2


def gen_exts():
    for ext in EXT_ORDER:
        comment("--- AVX512_%s (ledger U322-U325), F|DQ|BW|VL + the extension's UC_X86_AVX512_* bit" % ext)
        gen_ext(ext)


# ---------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    # VPCONFLICTD zmm1, zmm2 = 62 F2 7D 48 C4 CA (EVEX.512.66.0F38.W0 C4 /r)
    chk("enc vpconflictd", evex(2, 1, 0, 0xC4, 1, 2, ll=2), bytes([0x62, 0xF2, 0x7D, 0x48, 0xC4, 0xCA]))
    # VPLZCNTQ zmm17{k3}{z}, [rsi+0x40] (N = 64: disp8 = 1)
    chk("enc vplzcntq", evex(2, 1, 1, 0x44, 17, Mem(RSI, 0x40), ll=2, z=1, aaa=3, n=64),
        bytes([0x62, 0xE2, 0xFD, 0xCB, 0x44, 0x4E, 0x01]))
    # VPBROADCASTMB2Q zmm1, k2 = 62 F2 FE 48 2A CA
    chk("enc vpbroadcastmb2q", evex(2, 2, 1, 0x2A, 1, 2, ll=2), bytes([0x62, 0xF2, 0xFE, 0x48, 0x2A, 0xCA]))
    chk("lzcnt 0", lzcnt(0, 32), 32)
    chk("lzcnt 1", lzcnt(1, 32), 31)
    chk("lzcnt top", lzcnt(0x80000000, 32), 0)
    chk("lzcnt q", lzcnt(0x00000000FFFFFFFF, 64), 32)
    chk("conflict", [conflict([5, 3, 5, 5], j) for j in range(4)], [0, 0, 1, 0b101])
    chk("bcstm b", broadcastm(0x1234, 8), 0x34)
    chk("bcstm w", broadcastm(0x51234, 4), 0x1234)
    # U322: VPMADD52LUQ zmm1, zmm2, zmm3 = 62 F2 ED 48 B4 CB; 2^51 * 2^51 = 2^102: low 52
    # bits 0, bits 103:52 = 2^50; bits 63:52 of the sources are ignored; DEST wraps mod 2^64
    chk("enc vpmadd52luq", evex(2, 1, 1, 0xB4, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF2, 0xED, 0x48, 0xB4, 0xCB]))
    chk("madd52 lo", madd52(5, 1 << 51, 1 << 51, False), 5)
    chk("madd52 hi", madd52(5, 1 << 51, 1 << 51, True), 5 + (1 << 50))
    chk("madd52 ignore 63:52", madd52(0, (0xFFF << 52) | 3, (1 << 63) | 7, False), 21)
    chk("madd52 wrap", madd52(M64, 1, 1, False), 0)
    # U323: VPOPCNTQ zmm1, zmm3 = 62 F2 FD 48 55 CB
    chk("enc vpopcntq", evex(2, 1, 1, 0x55, 1, 3, ll=2), bytes([0x62, 0xF2, 0xFD, 0x48, 0x55, 0xCB]))
    chk("popcnt", [popcnt(0), popcnt(M64), popcnt(0x80000001)], [0, 64, 2])
    # U324: VPSHUFBITQMB k1, zmm2, zmm3 = 62 F2 6D 48 8F CB; byte j of SRC2 = j (+64 wraps)
    chk("enc vpshufbitqmb", evex(2, 1, 0, 0x8F, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF2, 0x6D, 0x48, 0x8F, 0xCB]))
    chk("shufbitqmb", shufbitqmb((0b10100101).to_bytes(8, "little") + bytes(8),
                                 bytes([0, 1, 2, 64, 66, 5, 6, 7]) + bytes([0] * 8), 16), 0b10111101)
    # U325: VPERMB zmm1, zmm2, zmm3 = 62 F2 6D 48 8D CB; VL 128: index bits 3:0, bit 4 = table
    chk("enc vpermb", evex(2, 1, 0, 0x8D, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF2, 0x6D, 0x48, 0x8D, 0xCB]))
    t1, t2 = bytes(range(16)), bytes(range(100, 116))
    chk("permb", permb(bytes([15, 0x10, 0xF3] + [0] * 13), t2, 16)[:3], bytes([115, 100, 103]))
    chk("permi2b", permi2b(bytes([0x11, 0x01, 0xFF] + [0] * 13), t1, t2, 16)[:3], bytes([101, 1, 115]))
    chk("permt2b", permt2b(t1, bytes([0x11, 0x01, 0xEF] + [0] * 13), t2, 16)[:3], bytes([101, 1, 15]))
    return ok


# ---------------------------------------------------------------------------------------
# hardware cross-check: VPLZCNT element counts vs the host's LZCNT r32 / r64
# ---------------------------------------------------------------------------------------
def hw_values():
    gen_all()           # fills LZ_VALUES with the element values of the VPLZCNT cases
    vals = {}
    for esz in (4, 8):
        bits = 8 * esz
        v = [0, 1, (1 << bits) - 1] + [1 << i for i in range(bits)] + [(1 << i) - 1 for i in range(bits)]
        seen = set()
        out = []
        for x in v + LZ_VALUES[esz]:
            if x not in seen:
                seen.add(x)
                out.append(x)
        vals[esz] = out
    return vals


def hwcheck_gen(out, expect_path):
    import json
    exp = []
    for esz, vals in sorted(hw_values().items()):
        for x in vals:
            if esz == 4:
                out.write("lzcnt eax, ecx | rcx=0x%X\n" % x)
            else:
                out.write("lzcnt rax, rcx | rcx=0x%X\n" % x)
            exp.append({"op": "lzcnt", "bits": 8 * esz, "x": x, "count": lzcnt(x, 8 * esz)})
    # U323/U324: the VPOPCNT element model vs the host's POPCNT r32 / r64 on the same values
    for esz, vals in sorted(hw_values().items()):
        for x in vals:
            out.write("popcnt %s | rcx=0x%X\n" % ("eax, ecx" if esz == 4 else "rax, rcx", x))
            exp.append({"op": "popcnt", "bits": 8 * esz, "x": x, "count": popcnt(x)})
    json.dump(exp, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    import json
    import re
    exp = json.load(open(expect_path))
    cur, bad, uc_bad = None, 0, 0
    seen = {("lzcnt", 32): 0, ("lzcnt", 64): 0, ("popcnt", 32): 0, ("popcnt", 64): 0}
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] (SAME|DIFF) (lzcnt|popcnt)", line)
        if m:
            cur = int(m.group(1))
            if m.group(2) != "SAME":
                uc_bad += 1
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if m and cur is not None:
            kv = dict(x.split("=", 1) for x in m.group(1).split() if "=" in x)
            got = int(kv.get("rax", "0"), 16) if kv.get("rax", "0").lower().startswith("0x") \
                else int(kv.get("rax", "0"), 0)
            e = exp[cur]
            seen[(e["op"], e["bits"])] += 1
            if got != e["count"] or "fault" in m.group(1):
                bad += 1
                if bad <= 20:
                    print("[%d] %s r%d 0x%X: hw %d, model %d" % (cur, e["op"], e["bits"], e["x"], got, e["count"]))
            cur = None
    print("hwcheck: LZCNT r32 %d / r64 %d values (VPLZCNTD/Q element model), POPCNT r32 %d / r64 %d "
          "values (VPOPCNT element model): %d differ (Unicorn vs hw DIFF lines: %d)"
          % (seen[("lzcnt", 32)], seen[("lzcnt", 64)], seen[("popcnt", 32)], seen[("popcnt", 64)], bad, uc_bad))
    return bad == 0 and sum(seen.values()) == len(exp)


def gen_all():
    del out_lines[:]
    LZ_VALUES[4].clear()
    LZ_VALUES[8].clear()
    RNG.seed(0xCD_5EED_320)
    gen_cd_vector()
    gen_fault_suppression()
    gen_broadcastm()
    gen_exts()


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
        gen_all()
        out = sys.stdout
        out.write("# AVX512CD (ledger U320-U321) and further AVX-512 extensions (U322-U325, sections\n")
        out.write("# '--- AVX512_*'): expected values from the independent SDM model\n")
        out.write("# Emulator/tools/isa/ref_evex_m3_cd.py --cases (regenerate, do not edit). The i5-13600K has\n")
        out.write("# no AVX-512: expected-value cases only, run with AVX-512 enabled (every bit):\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m3_cd.txt --avx512 --xcr0 0xE7 --expect-only\n")
        out.write("# RSI = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).\n")
        for l in out_lines:
            out.write(l + "\n")
        return
    print(__doc__)


if __name__ == "__main__":
    main()
