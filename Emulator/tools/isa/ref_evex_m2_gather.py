#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m2_gather.py -- independent reference model (Python 3 stdlib only) of the EVEX
gathers and scatters (ledger U250-U252) and generator of the expected-value case file
Emulator/data/cases_evex_m2_gather.txt.

Written from the Intel SDM text only (Vol2A 2.7 "Intel AVX-512 encoding", Tables 2-32 ..
2-43, 2.8.11 Table 2-63 "Type E12 Class Exception Conditions"; Vol2C pages VPGATHERDD/
VPGATHERDQ, VPGATHERQD/VPGATHERQQ, VGATHERDPS/VGATHERDPD, VGATHERQPS/VGATHERQPD,
VPSCATTERDD/DQ/QD/QQ, VSCATTERDPS/DPD/QPS/QPD "Operation"), not from any C implementation:

  EVEX.66.0F38 W0/W1  90 VPGATHERDD/DQ   91 VPGATHERQD/QQ   92 VGATHERDPS/DPD  93 VGATHERQPS/QPD
                      A0 VPSCATTERDD/DQ  A1 VPSCATTERQD/QQ  A2 VSCATTERDPS/DPD A3 VSCATTERQPS/QPD

Operation (every page; KL = VL / max(index size, data size)):
  FOR j := 0 TO KL-1
      IF k1[j] THEN
          ADDR := BASE_ADDR + SignExtend(VINDEX[j]) * SCALE + DISP   (truncated to the address
                  size: "the most significant bits beyond the number of address bits are ignored")
          gather:  DEST[j] := MEM[ADDR]          scatter: MEM[ADDR] := SRC[j]
          k1[j] := 0
      FI
  ENDFOR
  k1[MAX_KL-1:KL] := 0;  gather only: DEST[MAXVL-1:KL*data size] := 0
  (VL for DD/DQ/QQ, VL/2 for QD/QPS: "DEST[MAXVL-1:VL/2] := 0")

Faults (all pages): "faults are delivered in a right-to-left manner": when element j faults,
every element closer to the LSB is complete; the completed elements are written and their k1
bits are 0; the instruction can be restarted. Elements are processed in element order (j = 0
first), so the elements above the faulting one are untouched and keep their k1 bits. On a
fault the parts with no element (DEST[MAXVL-1:...], k1[63:KL]) are left unchanged (the SDM
allows either). Scatter writes to overlapping addresses happen in element order (LSB to MSB),
so the highest overlapping element wins. A masked-off element is never accessed (no fault).

#UD (Table 2-63 E12 and Tables 2-40..2-43, 64-bit mode): k0 (EVEX.aaa = 000b), EVEX.z = 1,
EVEX.b = 1, EVEX.L'L = 11b, EVEX.vvvv != 1111b, ModRM.mod = 11b, ModRM.rm != 100b (no VSIB),
gathers: destination register == index register (all 32 registers), pp other than 66.
AVX512PF (VGATHERPF0/1*, VSCATTERPF0/1*: 0F38 C6/C7, Xeon Phi only): CPUID.AVX512PF = 0 -> #UD.
disp8*N: Tuple1 Scalar, N = the size of one data element (4 for W0, 8 for W1).

Harness (Emulator/tests/alltest/at_engine.hpp): MEM = 0x30020000 .. +0xFFFF mapped (compared),
RSI = R14 = MEM + 0x8000; MEM + 0x10000 and above unmapped (#PF); CODE/DATA below MEM are the
harness' own and never addressed by a case.

Usage:
  python ref_evex_m2_gather.py --selftest   hand-derived checks of the model, exit 0 on pass
  python ref_evex_m2_gather.py --cases      Emulator/data/cases_evex_m2_gather.txt (stdout)
"""

import random
import sys

MEM = 0x30020000
MEM_SIZE = 0x10000
MEM_RSI = 0x8000
HARNESS_LO = 0x30000000              # CODE/DATA: mapped, never a case address
RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI = range(8)
R13, R14 = 13, 14
GPR_NAMES = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
             "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
MASK64 = (1 << 64) - 1

# name, opcode, EVEX.W, scatter, qword indices
FORMS = [
    ("VPGATHERDD", 0x90, 0, False, False), ("VPGATHERDQ", 0x90, 1, False, False),
    ("VPGATHERQD", 0x91, 0, False, True), ("VPGATHERQQ", 0x91, 1, False, True),
    ("VGATHERDPS", 0x92, 0, False, False), ("VGATHERDPD", 0x92, 1, False, False),
    ("VGATHERQPS", 0x93, 0, False, True), ("VGATHERQPD", 0x93, 1, False, True),
    ("VPSCATTERDD", 0xA0, 0, True, False), ("VPSCATTERDQ", 0xA0, 1, True, False),
    ("VPSCATTERQD", 0xA1, 0, True, True), ("VPSCATTERQQ", 0xA1, 1, True, True),
    ("VSCATTERDPS", 0xA2, 0, True, False), ("VSCATTERDPD", 0xA2, 1, True, False),
    ("VSCATTERQPS", 0xA3, 0, True, True), ("VSCATTERQPD", 0xA3, 1, True, True),
]
VL_LL = {16: 0, 32: 1, 64: 2}


# ---------------------------------------------------------------------------------------
# encoding (SDM Vol2A 2.7.1 Table 2-32, 2.7.2 Table 2-33: VIDX = EVEX.V' EVEX.X SIB.index)
# ---------------------------------------------------------------------------------------
class Insn:
    """the fields of one EVEX VSIB instruction (encoded fields as the SDM names them)"""

    def __init__(self, opc, w, reg, index, base, scale, disp, ll, aaa, z=0, b=0, vvvv=0xF,
                 pp=1, mod=None, rm=4, addr32=False, disp32=False, n=4, nobase_b=0):
        self.opc, self.w, self.reg, self.index, self.base = opc, w, reg, index, base
        self.scale, self.disp, self.ll, self.aaa, self.z, self.b = scale, disp, ll, aaa, z, b
        self.vvvv, self.pp, self.mod, self.rm = vvvv, pp, mod, rm
        self.addr32, self.disp32, self.n = addr32, disp32, n
        self.nobase_b = nobase_b                     # EVEX.B without a base: ignored (Table 2-41)

    def encode(self):
        r, rr = (self.reg >> 3) & 1, (self.reg >> 4) & 1
        x, vp = (self.index >> 3) & 1, (self.index >> 4) & 1
        if self.base is None:                        # no base: mod 00, SIB.base 101b, disp32
            bb, base_f, mod = self.nobase_b, 5, 0
            dbytes = (self.disp & 0xFFFFFFFF).to_bytes(4, "little")
        else:
            bb, base_f = (self.base >> 3) & 1, self.base & 7
            d = self.disp
            if self.disp32:
                mod, dbytes = 2, (d & 0xFFFFFFFF).to_bytes(4, "little")
            elif d == 0 and base_f != 5:
                mod, dbytes = 0, b""
            elif d % self.n == 0 and -128 <= d // self.n <= 127:
                mod, dbytes = 1, bytes([(d // self.n) & 0xFF])
            else:
                mod, dbytes = 2, (d & 0xFFFFFFFF).to_bytes(4, "little")
        if self.mod is not None:
            mod = self.mod
        p0 = ((r ^ 1) << 7) | ((x ^ 1) << 6) | ((bb ^ 1) << 5) | ((rr ^ 1) << 4) | 2
        p1 = (self.w << 7) | ((self.vvvv & 0xF) << 3) | 4 | self.pp
        p2 = (self.z << 7) | (self.ll << 5) | (self.b << 4) | ((vp ^ 1) << 3) | self.aaa
        modrm = (mod << 6) | ((self.reg & 7) << 3) | (self.rm & 7)
        if self.rm == 4 and mod != 3:
            tail = bytes([modrm, (self.scale << 6) | ((self.index & 7) << 3) | base_f]) + dbytes
        else:
            tail = bytes([modrm]) + (dbytes if mod != 3 else b"")
        pre = b"\x67" if self.addr32 else b""
        return pre + bytes([0x62, p0, p1, p2, self.opc]) + tail


def byte_list(bs):
    return ".byte " + ", ".join("0x%02x" % c for c in bs)


def hexs(buf):
    return "".join("%02X" % c for c in buf)


# ---------------------------------------------------------------------------------------
# machine state and the instruction model
# ---------------------------------------------------------------------------------------
class State:
    def __init__(self):
        self.zmm = [bytearray(64) for _ in range(32)]
        self.k = [0] * 8
        self.mem = bytearray(MEM_SIZE)
        self.gpr = [0] * 16
        self.gpr[RSI] = self.gpr[R14] = MEM + MEM_RSI
        self.gpr[RDI] = MEM + 0x9000

    def copy(self):
        s = State()
        s.zmm = [bytearray(z) for z in self.zmm]
        s.k = list(self.k)
        s.mem = bytearray(self.mem)
        s.gpr = list(self.gpr)
        return s


class GenError(Exception):
    pass


def form_of(opc, w):
    for f in FORMS:
        if f[1] == opc and f[2] == w:
            return f
    return None


def ud_reason(ins):
    """#UD conditions of Table 2-63 (E12) and Tables 2-40..2-43 for these encodings, or None"""
    f = form_of(ins.opc, ins.w)
    if f is None:
        return "no such form (AVX512PF or empty opcode)"
    if ins.pp != 1:
        return "pp != 66"
    if ins.aaa == 0:
        return "k0"
    if ins.z:
        return "EVEX.z"
    if ins.b:
        return "EVEX.b (Tuple1 Scalar, Table 2-43)"
    if ins.ll == 3:
        return "L'L = 11b"
    if ins.vvvv != 0xF:
        return "vvvv != 1111b"
    mod = ins.mod if ins.mod is not None else 0
    if mod == 3:
        return "ModRM.mod = 11b"
    if ins.rm != 4:
        return "ModRM.rm != 100b"
    if not f[3] and ins.reg == ins.index:
        return "destination == index"
    return None


def elem(buf, size, j):
    return int.from_bytes(buf[j * size:(j + 1) * size], "little")


def sext(v, size):
    bits = 8 * size
    return v - (1 << bits) if v >> (bits - 1) else v


def mem_access(addr, size):
    """offset into MEM, or None for an unmapped address (#PF)"""
    lo, hi = addr, addr + size
    if lo >= MEM and hi <= MEM + MEM_SIZE:
        return lo - MEM
    if hi > HARNESS_LO and lo < MEM:
        raise GenError("address 0x%X touches the harness' own mapped memory" % addr)
    return None                              # unmapped, or straddling the end of MEM


def execute(ins, st, vl):
    """runs ins on st (modified in place); returns None or the fault token"""
    if ud_reason(ins):
        return "#UD"
    _, _, w, scatter, qidx = form_of(ins.opc, ins.w)
    dsz, isz = (8 if w else 4), (8 if qidx else 4)
    kl = vl // max(dsz, isz)
    base = 0 if ins.base is None else st.gpr[ins.base]
    amask = 0xFFFFFFFF if ins.addr32 else MASK64
    for j in range(kl):
        if not (st.k[ins.aaa] >> j) & 1:
            continue
        idx = sext(elem(st.zmm[ins.index], isz, j), isz)
        addr = (base + idx * (1 << ins.scale) + ins.disp) & amask
        off = mem_access(addr, dsz)
        if off is None:
            return "#PF"
        if scatter:
            st.mem[off:off + dsz] = st.zmm[ins.reg][j * dsz:(j + 1) * dsz]
        else:
            st.zmm[ins.reg][j * dsz:(j + 1) * dsz] = st.mem[off:off + dsz]
        st.k[ins.aaa] &= ~(1 << j) & MASK64
    st.k[ins.aaa] = 0
    if not scatter:
        st.zmm[ins.reg][kl * dsz:] = bytes(64 - kl * dsz)
    return None


# ---------------------------------------------------------------------------------------
# a test case: input state, instruction, expected state
# ---------------------------------------------------------------------------------------
RNG = random.Random(0x6A7E_5CA7)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


class Case:
    def __init__(self, title, ins, vl):
        self.title, self.ins, self.vl = title, ins, vl
        self.st = State()
        self.zin, self.kin, self.min, self.gin = set(), set(), [], set()

    def zmm(self, r, img):
        self.st.zmm[r][:] = img
        self.zin.add(r)

    def kreg(self, r, v):
        self.st.k[r] = v & MASK64
        self.kin.add(r)

    def mem(self, off, data):
        self.st.mem[off:off + len(data)] = data
        self.min.append((off, len(data)))

    def gpr(self, r, v):
        self.st.gpr[r] = v
        self.gin.add(r)

    def line(self):
        before = self.st.copy()
        after = self.st.copy()
        fault = execute(self.ins, after, self.vl)
        ins = ["zmm%d=%s" % (r, hexs(before.zmm[r])) for r in sorted(self.zin)]
        ins += ["k%d=0x%X" % (r, before.k[r]) for r in sorted(self.kin)]
        ins += ["%s=0x%X" % (GPR_NAMES[r], before.gpr[r]) for r in sorted(self.gin)]
        ins += ["m+0x%X=%s" % (o, hexs(before.mem[o:o + n])) for o, n in sorted(self.min)]
        exp = []
        for r in range(32):
            if after.zmm[r] != before.zmm[r]:
                exp.append("zmm%d=%s" % (r, hexs(after.zmm[r])))
        for r in range(8):
            if after.k[r] != before.k[r]:
                exp.append("k%d=0x%X" % (r, after.k[r]))
        o = 0
        while o < MEM_SIZE:                     # changed memory as runs of bytes
            if after.mem[o] != before.mem[o]:
                e = o
                while e < MEM_SIZE and after.mem[e] != before.mem[e]:
                    e += 1
                exp.append("m+0x%X=%s" % (o, hexs(after.mem[o:e])))
                o = e
            else:
                o += 1
        if fault:
            exp.append(fault)
        return "%s | %s => %s" % (byte_list(self.ins.encode()), " ".join(ins), " ".join(exp))


cases = []


def emit(c):
    cases.append(c)


def comment(text):
    cases.append("# " + text)


# ---------------------------------------------------------------------------------------
# case generators
# ---------------------------------------------------------------------------------------
def geometry(form, vl):
    _, opc, w, scatter, qidx = form
    dsz, isz = (8 if w else 4), (8 if qidx else 4)
    return dsz, isz, vl // max(dsz, isz)


def pick_regs(scatter, hi=False):
    """distinct data and index registers (0-15, or 16-31 with hi)"""
    pool = list(range(16, 32)) if hi else list(range(16))
    RNG.shuffle(pool)
    return pool[0], pool[1]


def index_image(isz, vals):
    out = bytearray(rnd_bytes(64))          # index register bits above the indices: noise
    for j, v in enumerate(vals):
        out[j * isz:(j + 1) * isz] = (v & ((1 << (8 * isz)) - 1)).to_bytes(isz, "little")
    return bytes(out)


WIN = 0x100                                  # memory window [RSI - WIN, RSI + WIN)


def window_indices(kl, dsz, scale, disp, overlap=False):
    """indices so that RSI + idx*scale + disp stays inside the window"""
    sc = 1 << scale
    lo = -(WIN - 8) - disp
    hi = (WIN - 8 - dsz) - disp
    vals = []
    for _ in range(kl):
        while True:
            i = RNG.randint(lo // sc - 1, hi // sc + 1)
            o = i * sc + disp
            if -(WIN - 8) <= o <= WIN - 8 - dsz:
                break
        vals.append(i)
    if overlap:
        vals = [vals[0]] * kl
    return vals


def base_case(form, vl, title, mask=None, hi=False, base=RSI, scale=None, disp=0, aaa=None,
              overlap=False, disp32=False, idx_vals=None):
    name, opc, w, scatter, qidx = form
    dsz, isz, kl = geometry(form, vl)
    data, index = pick_regs(scatter, hi)
    scale = RNG.randint(0, 3) if scale is None else scale
    aaa = RNG.randint(1, 7) if aaa is None else aaa
    ins = Insn(opc, w, data, index, base, scale, disp, VL_LL[vl], aaa, n=dsz, disp32=disp32)
    c = Case("%s %d-bit %s" % (name, vl * 8, title), ins, vl)
    vals = idx_vals if idx_vals is not None else window_indices(kl, dsz, scale, disp, overlap)
    c.zmm(index, index_image(isz, vals))
    c.zmm(data, rnd_bytes(64))
    c.kreg(aaa, RNG.getrandbits(64) if mask is None else mask)
    c.mem(MEM_RSI - WIN, rnd_bytes(2 * WIN))
    if base == R14:
        pass                                 # R14 = MEM + 0x8000 by default
    elif base == RBX:
        c.gpr(RBX, MEM + MEM_RSI)
    elif base is None:
        ins.disp = disp + MEM + MEM_RSI      # absolute: disp32 only
    return c


def gen_basic():
    comment("--- every form x VL: full mask, random mask, no active element (k1 bits only above KL)")
    for form in FORMS:
        for vl in (16, 32, 64):
            dsz, isz, kl = geometry(form, vl)
            emit(base_case(form, vl, "full mask", mask=MASK64))
            emit(base_case(form, vl, "random mask"))
            emit(base_case(form, vl, "random mask, zmm16-31 (R', V'), r14 base (EVEX.B)", hi=True,
                           base=R14))
            emit(base_case(form, vl, "no element active", mask=RNG.getrandbits(64) & ~((1 << kl) - 1)))
            emit(base_case(form, vl, "k1 = 0", mask=0))
            emit(base_case(form, vl, "no base (disp32), random mask", base=None))
            c = base_case(form, vl, "no base, EVEX.B = 1 ignored", base=None)
            c.ins.nobase_b = 1
            emit(c)
            emit(base_case(form, vl, "rbx base, scale 8, random mask", base=RBX, scale=3))


def gen_disp8():
    comment("--- disp8*N (Tuple1 Scalar: N = data element size) and disp32")
    for form in FORMS:
        dsz = 8 if form[2] else 4
        for d8 in (1, -1, 127, -127):
            emit(base_case(form, 64, "disp8=%d N=%d" % (d8, dsz), disp=d8 * dsz, scale=0,
                           mask=MASK64))
        emit(base_case(form, 32, "disp32 not a multiple of N", disp=dsz + 1 if dsz == 4 else 3,
                       disp32=True))


def gen_neg_scale():
    comment("--- negative indices and every scale")
    for form in FORMS:
        dsz, isz, kl = geometry(form, 64)
        for scale in range(4):
            sc = 1 << scale
            vals = [-RNG.randint(1, (WIN - 8) // sc) for _ in range(kl)]
            emit(base_case(form, 64, "negative indices, scale %d" % sc, scale=scale, idx_vals=vals,
                           mask=MASK64))


def gen_addr32():
    comment("--- 67h: 32-bit address size, ADDR truncated to 32 bits (qword indices >= 2^32)")
    for form in FORMS:
        name, opc, w, scatter, qidx = form
        if not qidx:
            continue
        dsz, isz, kl = geometry(form, 32)
        vals = [(1 << 32) + RNG.randint(-(WIN - 8) // 8, (WIN - 8 - dsz) // 8) for _ in range(kl)]
        c = base_case(form, 32, "67h, index 2^32 + i wraps", scale=0, idx_vals=vals, mask=MASK64)
        c.ins.addr32 = True
        emit(c)
        c = base_case(form, 32, "no 67h, index 2^32 + i is unmapped #PF", scale=0, idx_vals=vals,
                      mask=MASK64)
        emit(c)
    for form in FORMS:
        name, opc, w, scatter, qidx = form
        if qidx:
            continue
        # dword indices with a base near 2^32 under 67h: base 0xFFFFFF00 + idx wraps
        dsz, isz, kl = geometry(form, 16)
        c = base_case(form, 16, "67h, rbx = MEM+0x8000 - 2^32 (high bits ignored)", base=RBX,
                      scale=2, mask=MASK64)
        c.gpr(RBX, (MEM + MEM_RSI - (1 << 32)) & MASK64)
        c.ins.addr32 = True
        emit(c)


def gen_faults():
    comment("--- faults: unmapped element j (MEM+0x10000..): elements below j complete, their k1 bits")
    comment("    cleared; j and above untouched; masked-off unmapped elements never fault")
    for form in FORMS:
        dsz, isz, kl = geometry(form, 64)
        sc = 8
        for variant in range(4):
            fj = RNG.randint(0, kl - 1) if variant < 2 else (0 if variant == 2 else kl - 1)
            c = base_case(form, 64, "unmapped element %d #PF (restartable)" % fj, scale=3)
            vals = window_indices(kl, dsz, 3, 0)
            vals[fj] = (0x10000 - MEM_RSI + 0x40 * RNG.randint(0, 8)) // sc
            idx_img = index_image(isz, vals)
            c.zmm(c.ins.index, idx_img)
            k = RNG.getrandbits(64) | (1 << fj)
            if variant == 1:
                k |= (1 << kl) - 1               # all active: elements 0..fj-1 complete
            c.kreg(c.ins.aaa, k)
            emit(c)
        # masked-off unmapped elements: no fault
        c = base_case(form, 64, "unmapped elements masked off: no fault", scale=3)
        vals = window_indices(kl, dsz, 3, 0)
        k = RNG.getrandbits(64) | 1
        for j in range(1, kl):
            if j % 2:
                vals[j] = (0x10000 - MEM_RSI + 8 * j) // 8
                k &= ~(1 << j)
        c.zmm(c.ins.index, index_image(isz, vals))
        c.kreg(c.ins.aaa, k)
        emit(c)
        # an element straddling the end of MEM: faults, none of its bytes is accessed
        for fj in (0, kl // 2):
            c = base_case(form, 64, "element %d straddles MEM+0x10000 #PF" % fj, scale=0)
            vals = window_indices(kl, dsz, 0, 0)
            vals[fj] = 0x10000 - MEM_RSI - dsz // 2
            c.zmm(c.ins.index, index_image(isz, vals))
            c.kreg(c.ins.aaa, MASK64)
            c.mem(0xFF00, rnd_bytes(0x100))
            emit(c)


def gen_scatter_overlap():
    comment("--- scatter: overlapping addresses are written in element order, the highest wins")
    for form in FORMS:
        if not form[3]:
            continue
        dsz, isz, kl = geometry(form, 64)
        emit(base_case(form, 64, "all indices equal, full mask", mask=MASK64, overlap=True))
        emit(base_case(form, 64, "all indices equal, random mask", overlap=True))
        # partially overlapping: consecutive elements dsz/2 bytes apart (scale 1)
        vals = [j * (dsz // 2) - 0x40 for j in range(kl)]
        emit(base_case(form, 64, "partially overlapping (step = half an element)", scale=0,
                       idx_vals=vals, mask=MASK64))
        # duplicates in pairs, random mask
        vals = window_indices(kl, dsz, 2, 0)
        for j in range(1, kl, 2):
            vals[j] = vals[j - 1]
        emit(base_case(form, 64, "pairs of equal indices, random mask", scale=2, idx_vals=vals))
        # scatter source == index register is allowed (no E12 overlap rule)
        c = base_case(form, 32, "source register == index register (valid)", scale=0, mask=MASK64)
        idx = c.ins.index
        c.ins.reg = idx
        vals = window_indices(kl, dsz, 0, 0)
        img = bytearray(rnd_bytes(64))
        for j, v in enumerate(vals[:geometry(form, 32)[2]]):
            img[j * isz:(j + 1) * isz] = (v & ((1 << (8 * isz)) - 1)).to_bytes(isz, "little")
        c.zmm(idx, bytes(img))
        emit(c)


def gen_ud():
    comment("--- #UD matrix (Table 2-63 E12, Tables 2-40..2-43)")
    for form in FORMS:
        name, opc, w, scatter, qidx = form
        dsz = 8 if w else 4

        def ud(title, **kw):
            c = base_case(form, 64, title, mask=MASK64)
            for k, v in kw.items():
                setattr(c.ins, k, v)
            assert ud_reason(c.ins), title
            emit(c)
        ud("k0 #UD", aaa=0)
        ud("{z} #UD", z=1)
        ud("EVEX.b #UD", b=1)
        ud("L'L=11 #UD", ll=3)
        ud("vvvv=1110b #UD", vvvv=0xE)
        ud("vvvv=0111b #UD", vvvv=0x7)
        ud("ModRM.rm=110b (no VSIB) #UD", rm=6)
        ud("ModRM.mod=11b #UD", mod=3)
        ud("pp=00 #UD", pp=0)
        ud("pp=F3 #UD", pp=2)
        if not scatter:
            for d in (1, 9, 17, 31):
                c = base_case(form, 64, "destination == index zmm%d #UD" % d, mask=MASK64)
                c.ins.reg = c.ins.index = d
                c.zmm(d, rnd_bytes(64))
                emit(c)
            # same register at a different width (VPGATHERDQ ymm/zmm vs its xmm/ymm index)
            c = base_case(form, 32, "destination == index (other width) #UD", mask=MASK64)
            c.ins.index = c.ins.reg
            emit(c)
            # only bit 4 differs (zmm3 vs zmm19): valid
            c = base_case(form, 64, "destination zmm3, index zmm19 (valid)", mask=MASK64)
            dsz_, isz_, kl_ = geometry(form, 64)
            img = c.st.zmm[c.ins.index]
            c.ins.reg, c.ins.index = 3, 19
            c.zmm(19, bytes(img))
            c.zmm(3, rnd_bytes(64))
            emit(c)
    comment("--- AVX512PF (Xeon Phi) prefetch gathers/scatters 0F38 C6/C7: CPUID.AVX512PF = 0 -> #UD")
    for opc in (0xC6, 0xC7):
        for ext in (1, 2, 5, 6):
            for w in (0, 1):
                ins = Insn(opc, w, ext, 2, RSI, 2, 0, 2, 1, n=8 if w else 4)
                c = Case("AVX512PF %02X /%d W%d #UD" % (opc, ext, w), ins, 64)
                c.kreg(1, MASK64)
                emit(c)


# ---------------------------------------------------------------------------------------
# self test (hand-derived values from the SDM rules)
# ---------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    # VPGATHERDD zmm1{k1}, [rsi + zmm2*4]: P0 = R X B R' 0 010 = 1111 0010, P1 = W0 1111 1 01,
    # P2 = z0 L'L=10 b0 V'=1 aaa=001, ModRM 00 001 100, SIB 10 010 110
    chk("enc vpgatherdd", Insn(0x90, 0, 1, 2, RSI, 2, 0, 2, 1).encode(),
        bytes([0x62, 0xF2, 0x7D, 0x49, 0x90, 0x0C, 0x96]))
    # VPSCATTERQQ [r14 + zmm17*8 + 8*8]{k7}, zmm25: R=1 (25 = 11001b: bit3 1, bit4 1),
    # X = 0 (17 = 10001b), B = 1 (r14), R' = 1 -> P0 = 0 1 0 0 0 010; P2 V' = 0
    chk("enc vpscatterqq", Insn(0xA1, 1, 25, 17, R14, 3, 64, 2, 7, n=8).encode(),
        bytes([0x62, 0x42, 0xFD, 0x47, 0xA1, 0x4C, 0xCE, 0x08]))
    # gather model: dwords at RSI + {0, 8, 4, 12} * 1, k1 = 1011b -> element 2 untouched
    st = State()
    st.mem[MEM_RSI:MEM_RSI + 16] = bytes(range(16))
    st.zmm[2][:16] = b"".join(v.to_bytes(4, "little") for v in (0, 8, 4, 12))
    st.zmm[1][:] = b"\xEE" * 64
    st.k[1] = 0xB
    chk("gather fault", execute(Insn(0x90, 0, 1, 2, RSI, 0, 0, 0, 1), st, 16), None)
    chk("gather dest", bytes(st.zmm[1][:16]),
        bytes([0, 1, 2, 3, 8, 9, 10, 11, 0xEE, 0xEE, 0xEE, 0xEE, 12, 13, 14, 15]))
    chk("gather upper", bytes(st.zmm[1][16:]), bytes(48))
    chk("gather k", st.k[1], 0)
    # partial fault: element 2 unmapped -> elements 0,1 done (k bits 0,1 cleared), 2,3 untouched
    st = State()
    st.mem[MEM_RSI:MEM_RSI + 16] = bytes(range(16))
    st.zmm[2][:16] = b"".join(v.to_bytes(4, "little") for v in (0, 4, 0x8000, 12))
    st.zmm[1][:] = b"\xEE" * 64
    st.k[1] = 0xFFFF00000000000F
    chk("partial #PF", execute(Insn(0x90, 0, 1, 2, RSI, 0, 0, 0, 1), st, 16), "#PF")
    chk("partial dest", bytes(st.zmm[1][:16]), bytes([0, 1, 2, 3, 4, 5, 6, 7]) + b"\xEE" * 8)
    chk("partial upper kept", bytes(st.zmm[1][16:]), b"\xEE" * 48)
    chk("partial k", st.k[1], 0xFFFF00000000000C)
    # VPGATHERQD (VL 256): 4 dwords, DEST[MAXVL-1:128] := 0
    st = State()
    st.mem[MEM_RSI:MEM_RSI + 16] = bytes(range(16))
    st.zmm[2][:32] = b"".join(v.to_bytes(8, "little") for v in (12, 8, 4, 0))
    st.zmm[1][:] = b"\xEE" * 64
    st.k[3] = 0xF
    execute(Insn(0x91, 0, 1, 2, RSI, 0, 0, 1, 3), st, 32)
    chk("qd dest", bytes(st.zmm[1]), bytes([12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3]) + bytes(48))
    # scatter: equal indices -> the highest active element wins
    st = State()
    st.zmm[2][:16] = bytes(16)
    st.zmm[5][:16] = b"".join(v.to_bytes(4, "little") for v in (0x11111111, 0x22222222, 0x33333333, 0x44444444))
    st.k[2] = 0x7
    execute(Insn(0xA0, 0, 5, 2, RSI, 0, 0, 0, 2), st, 16)
    chk("scatter overlap", bytes(st.mem[MEM_RSI:MEM_RSI + 4]), (0x33333333).to_bytes(4, "little"))
    chk("ud k0", ud_reason(Insn(0x90, 0, 1, 2, RSI, 0, 0, 2, 0)), "k0")
    chk("ud dst=idx", ud_reason(Insn(0x92, 1, 20, 20, RSI, 0, 0, 2, 1)), "destination == index")
    chk("scatter src=idx ok", ud_reason(Insn(0xA2, 1, 20, 20, RSI, 0, 0, 2, 1)), None)
    return ok


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_basic()
        gen_disp8()
        gen_neg_scale()
        gen_addr32()
        gen_faults()
        gen_scatter_overlap()
        gen_ud()
        out = sys.stdout
        out.write("# EVEX gathers/scatters (ledger U250-U252): expected values from the independent SDM\n")
        out.write("# model Emulator/tools/isa/ref_evex_m2_gather.py --cases (regenerate, do not edit). The\n")
        out.write("# i5-13600K has no AVX-512: expected-value cases only, run with AVX-512 enabled:\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m2_gather.txt --avx512 --xcr0 0xE7 --expect-only\n")
        out.write("# RSI = R14 = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / partial-completion cases).\n")
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
