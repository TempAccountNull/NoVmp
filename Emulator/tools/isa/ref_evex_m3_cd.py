#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m3_cd.py -- independent reference model (Python 3 stdlib only) of the EVEX
AVX512CD instructions (ledger U320-U321) and generator of the expected-value case file
Emulator/data/cases_evex_m3_cd.txt.

Written from the Intel SDM text only (Vol2A 2.7 "Intel AVX-512 encoding", Tables 2-36 ..
2-45, 2.8 exception classes E4 / E4NF / E6NF; Vol2C instruction pages, "Operation"
pseudocode), not from any C implementation:

  VPCONFLICTD/Q   EVEX.128/256/512.66.0F38.W0/W1 C4 /r   Full tuple, {k1}{z}, {1toN}, E4NF
  VPLZCNTD/Q      EVEX.128/256/512.66.0F38.W0/W1 44 /r   Full tuple, {k1}{z}, {1toN}, E4
  VPBROADCASTMB2Q EVEX.128/256/512.F3.0F38.W1 2A /r      zmm1, k1 (register only), E6NF
  VPBROADCASTMW2D EVEX.128/256/512.F3.0F38.W0 3A /r      zmm1, k1 (register only), E6NF

CPUID: AVX512CD (EVEX.512) and AVX512VL AND AVX512CD (EVEX.128/256); emu-alltest --avx512
enables both. EVEX.vvvv is reserved (1111b, V' = 1) for all four.

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
            exp.append({"bits": 8 * esz, "x": x, "count": lzcnt(x, 8 * esz)})
    json.dump(exp, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    import json
    import re
    exp = json.load(open(expect_path))
    cur, bad, seen, uc_bad = None, 0, {32: 0, 64: 0}, 0
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] (SAME|DIFF) lzcnt", line)
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
            seen[e["bits"]] += 1
            if got != e["count"] or "fault" in m.group(1):
                bad += 1
                if bad <= 20:
                    print("[%d] lzcnt r%d 0x%X: hw %d, model %d" % (cur, e["bits"], e["x"], got, e["count"]))
            cur = None
    print("hwcheck: LZCNT r32 %d values, r64 %d values compared with the VPLZCNTD/Q element model: "
          "%d differ (Unicorn vs hw DIFF lines: %d)" % (seen[32], seen[64], bad, uc_bad))
    return bad == 0 and seen[32] + seen[64] == len(exp)


def gen_all():
    del out_lines[:]
    LZ_VALUES[4].clear()
    LZ_VALUES[8].clear()
    RNG.seed(0xCD_5EED_320)
    gen_cd_vector()
    gen_fault_suppression()
    gen_broadcastm()


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
        out.write("# AVX512CD (ledger U320-U321): expected values from the independent SDM model\n")
        out.write("# Emulator/tools/isa/ref_evex_m3_cd.py --cases (regenerate, do not edit). The i5-13600K has\n")
        out.write("# no AVX-512: expected-value cases only, run with AVX-512 enabled (F|DQ|BW|VL|CD):\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m3_cd.txt --avx512 --xcr0 0xE7 --expect-only\n")
        out.write("# RSI = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).\n")
        for l in out_lines:
            out.write(l + "\n")
        return
    print(__doc__)


if __name__ == "__main__":
    main()
