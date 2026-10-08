#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m2_perm.py -- independent reference model (Python 3 stdlib only) of the EVEX
milestone-M2 permute / move / insert / extract / broadcast instructions (ledger U210-U229)
and generator of the expected-value case file Emulator/data/cases_evex_m2_perm.txt.

Written from the Intel SDM (rev. 092) text only: Vol2A 2.7 (EVEX encoding, Tables 2-36 /
2-37 disp8*N, Tables 2-38 .. 2-43 #UD rules), Vol2A 2.8 (exception classes, Tables 2-44 ..
2-59) and the "Operation" pseudocode of every instruction page. No C implementation was
read. The EVEX encoder and the case/line format are reused from ref_evex_m1.py (import).

Instructions (AVX512F unless noted; the modelled CPU has AVX512F/DQ/BW/VL, no AVX10.2):
  1  VPUNPCKL/HDQ, VPUNPCKL/HQDQ, VPSHUFD, VSHUFPS/PD, VUNPCKL/HPS/PD, VPERMD/Q, VPERMPS/PD,
     VPERMQ/VPERMPD imm8, VPERMI2D/Q/PS/PD, VPERMT2D/Q/PS/PD, VALIGND/Q, VPERMILPS/PD
     (imm8 and vector control), VSHUFF32X4/F64X2/I32X4/I64X2               (Full, E4NF)
  2  VPMOVZX/SX BD BQ WD WQ DQ (E5); VPMOV[S|US] DB DW QB QW QD (E6, reg / mem dest)
  3  VPCOMPRESSD/Q, VCOMPRESSPS/PD, VPEXPANDD/Q, VEXPANDPS/PD          (Tuple1 Scalar, E4.nb)
  4  VMOVNTPS/PD, VMOVNTDQ, VMOVNTDQA (E1NF); VMOVDDUP (E5NF), VMOVSHDUP/SLDUP (E4NF.nb);
     VMOVD/VMOVQ, VMOVHPS/HPD/LPS/LPD (E9NF), VMOVHLPS/VMOVLHPS (E7NM.128)
  5  VPINSRD/Q, VPEXTRD/Q (DQ), VINSERTPS, VEXTRACTPS (E9NF); VINSERTx/VEXTRACTx 32X4 64X2
     32X8 64X4 (E6NF); VBROADCASTF/I 32X2 32X4 64X2 32X8 64X4 (E6)

Generic EVEX rules used (SDM text quoted where a choice is made):
  MASK   FOR j: IF k1[j] OR *no writemask* THEN DEST[j] := op ELSE merging: unchanged /
         zeroing: 0; DEST[MAXVL-1:VL] := 0 (narrowing register dest: DEST[MAXVL-1:VL/r] := 0,
         VEXTRACT register dest: DEST[MAXVL-1:128/256] := 0)
  STORE  memory dest: "ELSE *DEST[i+..:i] remains unchanged* ; merging-masking"; EVEX.z with
         a store is #UD (Table 2-42: "Store instructions or gather/scatter instructions.
         If EVEX.z != 0")
  NOMASK forms without {k1}: Table 2-42 "Instructions do not use opmask for conditional
         processing ... If aaa != 000b" -> #UD; "Instructions with EVEX.aaa = 000b. If
         EVEX.z != 0" -> #UD
  BCST   Table 2-37 forms: "Instruction classified in Table 2-37 do not use EVEX.b and EVEX.b
         must be 0, otherwise #UD will occur"; EVEX.b on a register operand of these
         non-rounding instructions: Table 2-43 "Other instruction classes ... If EVEX.b = 1"
  L'L    Table 2-38 "11b: Reserved (#UD)"; E9NF: "must be encoded with VEX.L'L = 0";
         E7NM.128: "#UD raised if EVEX.L'L != 00b (VL=128)"; per-page "#UD If EVEX.L'L = 0"
  vvvv   Table 2-41 "EVEX.vvvv ... Otherwise If != 1111b"; "EVEX.V' ... Otherwise If 0"
  FAULTS Table 2-44: E4/E5/E6 "Support fault suppression", E4NF/E5NF/E6NF/E9NF "No fault
         suppression", E1NF "Explicitly aligned, no fault suppression" (#GP(0) "EVEX.512:
         Memory operand is not 64-byte aligned" ...). Masked-off elements of an E4/E5/E6
         memory operand are not accessed; compress stores / expand loads access only the
         popcount(k) contiguous elements ("Only the contiguous vector is written to the
         destination memory location").

SDM ambiguities resolved here (see the report in the case-file header too):
  * VSHUFF32x4/VSHUFI32x4/.. VL=256 pseudocode reads "Select2(SRC2[255:0], imm8[1])" (SRC2,
    not TMP_SRC2) while the 512-bit branch uses TMP_SRC2: the {1toN} broadcast is applied
    for VL=256 too (Full tuple, N = element size: only one element is read).
  * VMOVDDUP 512-bit pseudocode has typos ("TMP_SRC[477:384] := SRC[477:384]"); modelled
    as qword 2i duplicated into qwords 2i, 2i+1 (Figure 4-2).
  * VPERMILPD imm8 EVEX pseudocode uses "SRC1[63:0]" for imm8[0] = 0 instead of TMP_SRC1:
    identical value with or without {1toN}.
  * VBROADCASTF64X2 pseudocode lacks "DEST[MAXVL-1:VL] := 0"; the generic EVEX rule is used.
  * E6NF (VINSERTx/VEXTRACTx) and E4NF/E5NF loads crossing into an unmapped page: #PF even
    when the elements using the unmapped bytes are masked off (Table 2-56 "Page Fault #PF
    ... For a page fault", no "If fault suppression not set").
  * VBROADCASTx32X4/64X4 (E6) with masking: memory element g of the tuple is accessed only
    when a destination lane j with j mod tuple_size == g is active (fault suppression per
    element, Vol1 "memory faults are suppressed for elements with a mask bit of 0").

Usage:
  python ref_evex_m2_perm.py --selftest   hand-derived checks, exit 0 on pass
  python ref_evex_m2_perm.py --cases      the case file (stdout)
  python ref_evex_m2_perm.py --stats      number of cases per section (stdout)
"""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_evex_m1 as m1  # noqa: E402

evex = m1.evex
Mem, Case = m1.Mem, m1.Case
elems, pack, hexs, mask_merge = m1.elems, m1.pack, m1.hexs, m1.mask_merge
MEM_RSI, RSI, R14 = m1.MEM_RSI, m1.RSI, m1.R14

RNG = random.Random(0x4D32_5045_524D)          # "M2PERM": deterministic output
VLS = (16, 32, 64)
LL = {16: 0, 32: 1, 64: 2}
MAPN = {1: "0F", 2: "0F38", 3: "0F3A"}
PPN = ["NP", "66", "F3", "F2"]
GPRN = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
GPR_OK = [0, 1, 2, 3, 5, 8, 9, 10, 11, 12, 13, 15]     # never RSP/RSI/RDI/R14 (harness bases)
IMMS = [0x00, 0xFF, 0x1B, 0xE4, 0x4E, 0xB1, 0x93, 0x39, 0x01, 0x80, 0x6C, 0xD8]
PAGE_END = 0x10000 - MEM_RSI                            # [rsi + PAGE_END] = MEM + 0x10000 (unmapped)


def rb(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


def rk():
    return RNG.getrandbits(64)


def regs(n):
    return RNG.sample(range(32), n)


def kr():
    return RNG.randint(1, 7)


def u(v, sz):
    return v & ((1 << (8 * sz)) - 1)


def sgn(v, sz):
    v = u(v, sz)
    return v - (1 << (8 * sz)) if v >> (8 * sz - 1) else v


# ---------------------------------------------------------------------------------------
# output bookkeeping
# ---------------------------------------------------------------------------------------
OUT = []
SECTIONS = []          # [name, count]


def section(name):
    SECTIONS.append([name, 0])
    OUT.append("# --- " + name)


def comment(text):
    OUT.append("# " + text)


def emit(c):
    OUT.append(c)
    SECTIONS[-1][1] += 1


def ud(title, mmm, pp, w, opc, reg, rm, **kw):
    c = Case(title + " #UD")
    c.code = evex(mmm, pp, w, opc, reg, rm, **kw)
    c.fault = "#UD"
    emit(c)


def set_mem(c, disp, data):
    c.mem[MEM_RSI + disp] = data


def exp_mem(c, disp, data):
    c.exp.append("m+0x%X=%s" % (MEM_RSI + disp, hexs(data)))


def exp_zmm(c, r, img):
    c.exp.append("zmm%d=%s" % (r, hexs(img)))


def exp_gpr(c, g, v):
    c.exp.append("%s=0x%X" % (GPRN[g], v))


def kbits(kmask, j):
    return kmask is None or (kmask >> j) & 1


# ---------------------------------------------------------------------------------------
# 1. Full-tuple permutes (E4NF). fn(vl, esz, a = SRC1 (vvvv) elements, b = SRC2 (r/m)
#    elements after {1toN}, d = old DEST elements, imm) -> result elements (KL)
# ---------------------------------------------------------------------------------------
def f_unpcklo(vl, esz, a, b, d, imm):
    """PUNPCKLDQ/UNPCKLPS: INTERLEAVE_DWORDS per 128-bit lane (low half); QDQ/PD: qwords"""
    per = 16 // esz
    out = []
    for lane in range(vl // 16):
        base = lane * per
        for t in range(per // 2):
            out += [a[base + t], b[base + t]]
    return out


def f_unpckhi(vl, esz, a, b, d, imm):
    per = 16 // esz
    out = []
    for lane in range(vl // 16):
        base = lane * per + per // 2
        for t in range(per // 2):
            out += [a[base + t], b[base + t]]
    return out


def f_sel4(vl, esz, a, b, d, imm):
    """VPSHUFD / VPERMILPS imm8: DEST dword k of each 128-bit lane := SRC[imm8[2k+1:2k]];
    VPERMQ/VPERMPD imm8: same on qwords in each 256-bit group (TMP_SRC[255:0] >> IMM8[..]*64)"""
    return [b[(j & ~3) + ((imm >> (2 * (j & 3))) & 3)] for j in range(vl // esz)]


def f_shufps(vl, esz, a, b, d, imm):
    """SHUFPS: dwords 0,1 of each lane from SRC1, dwords 2,3 from SRC2, Select4 by imm8"""
    return [(a if (j & 3) < 2 else b)[(j & ~3) + ((imm >> (2 * (j & 3))) & 3)] for j in range(vl // esz)]


def f_shufpd(vl, esz, a, b, d, imm):
    """SHUFPD: even qword j from SRC1, odd from SRC2; IMM0[j] selects high/low of the pair"""
    return [(a if j % 2 == 0 else b)[(j & ~1) + ((imm >> j) & 1)] for j in range(vl // esz)]


def f_perm(vl, esz, a, b, d, imm):
    """VPERMD/Q/PS/PD: id := SRC1[i+n:i] (n = log2 KL - 1); DEST[j] := SRC2[id]"""
    kl = vl // esz
    return [b[a[j] & (kl - 1)] for j in range(kl)]


def f_permi2(vl, esz, a, b, d, imm):
    """VPERMI2*: off := TMP_DEST[i+id:i]; DEST := TMP_DEST[i+id+1] ? SRC2[off] : SRC1[off]"""
    kl = vl // esz
    lg = kl.bit_length() - 1
    return [(b if (d[j] >> lg) & 1 else a)[d[j] & (kl - 1)] for j in range(kl)]


def f_permt2(vl, esz, a, b, d, imm):
    """VPERMT2*: off := SRC1[i+id:i]; DEST := SRC1[i+id+1] ? SRC2[off] : TMP_DEST[off]"""
    kl = vl // esz
    lg = kl.bit_length() - 1
    return [(b if (a[j] >> lg) & 1 else d)[a[j] & (kl - 1)] for j in range(kl)]


def f_valign(vl, esz, a, b, d, imm):
    """VALIGND/Q: tmp[VL-1:0] := src; tmp[2VL-1:VL] := SRC1; SHIFT = imm8[log2 KL - 1:0]"""
    kl = vl // esz
    t = b + a
    s = imm & (kl - 1)
    return t[s:s + kl]


def f_permilpd_imm(vl, esz, a, b, d, imm):
    """VPERMILPD imm8: qword j := TMP_SRC1[pair(j) + imm8[j]]"""
    return [b[(j & ~1) + ((imm >> j) & 1)] for j in range(vl // esz)]


def f_permilps_var(vl, esz, a, b, d, imm):
    """VPERMILPS vector: Select4(SRC1 lane, TMP_SRC2[i+1:i])"""
    return [a[(j & ~3) + (b[j] & 3)] for j in range(vl // esz)]


def f_permilpd_var(vl, esz, a, b, d, imm):
    """VPERMILPD vector: TMP_SRC2[i+1] selects the qword of the SRC1 pair"""
    return [a[(j & ~1) + ((b[j] >> 1) & 1)] for j in range(vl // esz)]


def f_shuf128(vl, esz, a, b, d, imm):
    """VSHUFF32x4..: VL256 Select2(SRC1, imm8[0]) | Select2(SRC2, imm8[1]); VL512
    Select4(SRC1, imm8[1:0]), Select4(SRC1, imm8[3:2]), Select4(SRC2, [5:4]), Select4(SRC2, [7:6])"""
    per = 16 // esz
    if vl == 32:
        sel = [(a, imm & 1), (b, (imm >> 1) & 1)]
    else:
        sel = [(a, imm & 3), (a, (imm >> 2) & 3), (b, (imm >> 4) & 3), (b, (imm >> 6) & 3)]
    out = []
    for src, cnum in sel:
        out += src[cnum * per:(cnum + 1) * per]
    return out


def f_shdup(vl, esz, a, b, d, imm):
    return [b[j | 1] for j in range(vl // esz)]


def f_ldup(vl, esz, a, b, d, imm):
    """VMOVSLDUP (dwords) and VMOVDDUP (qwords): element 2i duplicated into 2i, 2i+1"""
    return [b[j & ~1] for j in range(vl // esz)]


class FF:
    """one Full-tuple (or FullMem / MOVDDUP) EVEX form with {k1}{z}"""

    def __init__(self, name, mmm, pp, opc, w, lay, fn, vls=VLS, wud=False, nb=False, exc="E4NF",
                 memsz=None):
        self.name, self.mmm, self.pp, self.opc, self.w, self.lay, self.fn = name, mmm, pp, opc, w, lay, fn
        self.vls, self.wud, self.nb, self.exc = vls, wud, nb, exc
        self.esz = 8 if w else 4
        self.vvvv = lay in ("rvm", "rvmi")
        self.imm = lay in ("rvmi", "rmi")
        self.memsz = memsz or (lambda vl: vl)

    def desc(self):
        return "%s (EVEX.%s.%s.W%d %02X /r%s, %s%s)" % (
            self.name.split()[0], PPN[self.pp], MAPN[self.mmm], self.w, self.opc, " ib" if self.imm else "",
            self.exc, "" if not self.nb else ", no {1toN}")


FULL = [
    FF("VPUNPCKLDQ", 1, 1, 0x62, 0, "rvm", f_unpcklo, wud=True),
    FF("VPUNPCKHDQ", 1, 1, 0x6A, 0, "rvm", f_unpckhi, wud=True),
    FF("VPUNPCKLQDQ", 1, 1, 0x6C, 1, "rvm", f_unpcklo, wud=True),
    FF("VPUNPCKHQDQ", 1, 1, 0x6D, 1, "rvm", f_unpckhi, wud=True),
    FF("VPSHUFD", 1, 1, 0x70, 0, "rmi", f_sel4, wud=True),
    FF("VSHUFPS", 1, 0, 0xC6, 0, "rvmi", f_shufps, wud=True),
    FF("VSHUFPD", 1, 1, 0xC6, 1, "rvmi", f_shufpd, wud=True),
    FF("VUNPCKLPS", 1, 0, 0x14, 0, "rvm", f_unpcklo, wud=True),
    FF("VUNPCKHPS", 1, 0, 0x15, 0, "rvm", f_unpckhi, wud=True),
    FF("VUNPCKLPD", 1, 1, 0x14, 1, "rvm", f_unpcklo, wud=True),
    FF("VUNPCKHPD", 1, 1, 0x15, 1, "rvm", f_unpckhi, wud=True),
    FF("VPERMD", 2, 1, 0x36, 0, "rvm", f_perm, vls=(32, 64)),
    FF("VPERMQ", 2, 1, 0x36, 1, "rvm", f_perm, vls=(32, 64)),
    FF("VPERMPS", 2, 1, 0x16, 0, "rvm", f_perm, vls=(32, 64)),
    FF("VPERMPD", 2, 1, 0x16, 1, "rvm", f_perm, vls=(32, 64)),
    FF("VPERMQ imm8", 3, 1, 0x00, 1, "rmi", f_sel4, vls=(32, 64), wud=True),
    FF("VPERMPD imm8", 3, 1, 0x01, 1, "rmi", f_sel4, vls=(32, 64), wud=True),
    FF("VPERMI2D", 2, 1, 0x76, 0, "rvm", f_permi2),
    FF("VPERMI2Q", 2, 1, 0x76, 1, "rvm", f_permi2),
    FF("VPERMI2PS", 2, 1, 0x77, 0, "rvm", f_permi2),
    FF("VPERMI2PD", 2, 1, 0x77, 1, "rvm", f_permi2),
    FF("VPERMT2D", 2, 1, 0x7E, 0, "rvm", f_permt2),
    FF("VPERMT2Q", 2, 1, 0x7E, 1, "rvm", f_permt2),
    FF("VPERMT2PS", 2, 1, 0x7F, 0, "rvm", f_permt2),
    FF("VPERMT2PD", 2, 1, 0x7F, 1, "rvm", f_permt2),
    FF("VALIGND", 3, 1, 0x03, 0, "rvmi", f_valign),
    FF("VALIGNQ", 3, 1, 0x03, 1, "rvmi", f_valign),
    FF("VPERMILPS imm8", 3, 1, 0x04, 0, "rmi", f_sel4, wud=True),
    FF("VPERMILPD imm8", 3, 1, 0x05, 1, "rmi", f_permilpd_imm, wud=True),
    FF("VPERMILPS", 2, 1, 0x0C, 0, "rvm", f_permilps_var, wud=True),
    FF("VPERMILPD", 2, 1, 0x0D, 1, "rvm", f_permilpd_var, wud=True),
    FF("VSHUFF32X4", 3, 1, 0x23, 0, "rvmi", f_shuf128, vls=(32, 64)),
    FF("VSHUFF64X2", 3, 1, 0x23, 1, "rvmi", f_shuf128, vls=(32, 64)),
    FF("VSHUFI32X4", 3, 1, 0x43, 0, "rvmi", f_shuf128, vls=(32, 64)),
    FF("VSHUFI64X2", 3, 1, 0x43, 1, "rvmi", f_shuf128, vls=(32, 64)),
]

DUP = [
    # VMOVDDUP: MOVDDUP tuple (N = 8 / 32 / 64), the 128-bit form reads m64
    FF("VMOVDDUP", 1, 3, 0x12, 1, "rm", f_ldup, wud=True, nb=True, exc="E5NF",
       memsz=lambda vl: 8 if vl == 16 else vl),
    FF("VMOVSHDUP", 1, 2, 0x16, 0, "rm", f_shdup, wud=True, nb=True, exc="E4NF.nb"),
    FF("VMOVSLDUP", 1, 2, 0x12, 0, "rm", f_ldup, wud=True, nb=True, exc="E4NF.nb"),
]


def full_case(sp, vl, dst, s1, s2, src="reg", kreg=0, kval=None, z=0, imm=None, disp=0x40, pre=None,
              title=""):
    """one case of a FF form. src: reg / mem / bcst. pre: {reg: 64-byte image, 'mem': bytes}"""
    esz = sp.esz
    kl = vl // esz
    pre = pre or {}
    c = Case("%s VL%d %s" % (sp.name, vl * 8, title))
    imgs = {}
    order = [dst] + ([s1] if sp.vvvv else []) + ([s2] if src == "reg" else [])
    for r in order:
        if r not in imgs:
            imgs[r] = pre.get(r) or rb(64)
    for r in imgs:
        c.set_zmm(r, imgs[r])
    a = elems(imgs[s1][:vl], esz) if sp.vvvv else None
    d = elems(imgs[dst][:vl], esz)
    if src == "reg":
        b = elems(imgs[s2][:vl], esz)
        rm, n, bc = s2, 1, 0
    else:
        msz = esz if src == "bcst" else sp.memsz(vl)
        data = (pre.get("mem") or rb(msz))[:msz]
        set_mem(c, disp, data)
        b = elems(data, esz) * kl if src == "bcst" else elems(data + bytes(vl - msz), esz)
        rm = Mem(RSI, disp)
        n = esz if src == "bcst" else sp.memsz(vl)
        bc = 1 if src == "bcst" else 0
    if sp.imm and imm is None:
        imm = RNG.choice(IMMS + [RNG.getrandbits(8)])
    res = sp.fn(vl, esz, a, b, d, imm)
    if kreg:
        c.k[kreg] = kval
    out = mask_merge(imgs[dst], pack(res, esz), esz, vl, kval if kreg else None, z)
    c.code = evex(sp.mmm, sp.pp, sp.w, sp.opc, dst, rm, vvvv=s1 if sp.vvvv else None, ll=LL[vl], b=bc,
                  z=z, aaa=kreg, imm=imm if sp.imm else None, n=n)
    if sp.imm:
        c.title += " imm=0x%02X" % imm
    exp_zmm(c, dst, out)
    return c


def index_image(sp, vl, role):
    """crafted index vectors: every in-table index and both table-select bits"""
    esz = sp.esz
    kl = vl // esz
    lg = kl.bit_length() - 1
    vals = []
    for j in range(kl):
        idx = (j * 5 + 3) & (2 * kl - 1)                     # both tables, all offsets
        junk = RNG.getrandbits(8 * esz) & ~((2 << lg) - 1)    # bits above the used ones: ignored
        vals.append(idx | junk)
    return pack(vals, esz) + rb(64 - vl)


def full_uds(sp):
    vl = max(sp.vls)
    imm = 0x1B if sp.imm else None
    vv = 2 if sp.vvvv else None
    a = (sp.mmm, sp.pp, sp.w, sp.opc)
    ud("%s EVEX.b on a register operand" % sp.name, *a, 1, 3, vvvv=vv, ll=LL[vl], b=1, imm=imm)
    ud("%s L'L=11" % sp.name, *a, 1, 3, vvvv=vv, ll=3, imm=imm)
    if sp.name in ("VPUNPCKLDQ", "VPSHUFD", "VPERMD", "VMOVDDUP"):
        ud("%s L'L=11 memory" % sp.name, *a, 1, Mem(RSI, 0), vvvv=vv, ll=3, imm=imm, n=64)
    if 16 not in sp.vls:
        ud("%s EVEX.128 not encodable" % sp.name, *a, 1, 3, vvvv=vv, ll=0, imm=imm)
        ud("%s EVEX.128 memory not encodable" % sp.name, *a, 1, Mem(RSI, 0), vvvv=vv, ll=0, imm=imm, n=16)
    if sp.wud:
        ud("%s wrong EVEX.W%d" % (sp.name, 1 - sp.w), sp.mmm, sp.pp, 1 - sp.w, sp.opc, 1, 3, vvvv=vv, ll=LL[vl],
           imm=imm)
    if not sp.vvvv:
        ud("%s EVEX.vvvv != 1111b (no vvvv operand)" % sp.name, *a, 1, 3, vvvv=5, ll=LL[vl], imm=imm)
        ud("%s EVEX.V' = 0 (no vvvv operand)" % sp.name, *a, 1, 3, ll=LL[vl], imm=imm, p2_vp=0)
    ud("%s EVEX.z with aaa = 000b" % sp.name, *a, 1, 3, vvvv=vv, ll=LL[vl], z=1, imm=imm)
    if sp.nb:
        ud("%s EVEX.b with a memory operand (Table 2-37 form)" % sp.name, *a, 1, Mem(RSI, 0), vvvv=vv,
           ll=LL[vl], b=1, imm=imm, n=4)


def gen_full(forms, title):
    section(title)
    for sp in forms:
        comment(sp.desc())
        for vl in sp.vls:
            d, s1, s2 = regs(3)
            emit(full_case(sp, vl, d, s1, s2, title="reg nomask"))
            d, s1, s2 = regs(3)
            emit(full_case(sp, vl, d, s1, s2, kreg=kr(), kval=rk(), title="reg merge"))
            d, s1, s2 = regs(3)
            emit(full_case(sp, vl, d, s1, s2, kreg=kr(), kval=rk(), z=1, title="reg zero"))
            d, s1 = regs(2)
            emit(full_case(sp, vl, d, s1, 0, src="mem", disp=RNG.choice([0, 0x40, -0x80, 0x100]),
                           title="mem nomask"))
            d, s1 = regs(2)
            emit(full_case(sp, vl, d, s1, 0, src="mem", kreg=kr(), kval=rk(), z=RNG.getrandbits(1),
                           disp=-0x200, title="mem masked"))
            if not sp.nb:
                d, s1 = regs(2)
                emit(full_case(sp, vl, d, s1, 0, src="bcst", disp=8, title="{1toN} nomask"))
                d, s1 = regs(2)
                emit(full_case(sp, vl, d, s1, 0, src="bcst", kreg=kr(), kval=rk(), z=RNG.getrandbits(1),
                               disp=-0x14 & ~(sp.esz - 1), title="{1toN} masked"))
            if sp.imm:
                d, s1, s2 = regs(3)
                emit(full_case(sp, vl, d, s1, s2, kreg=kr(), kval=rk(), imm=RNG.getrandbits(8), title="imm variety"))
        vl = max(sp.vls)
        # aliasing: destination == each source, all equal
        r1, r2 = regs(2)
        if sp.vvvv:
            emit(full_case(sp, vl, r1, r1, r2, kreg=kr(), kval=rk(), title="dst=src1"))
            r1, r2 = regs(2)
            emit(full_case(sp, vl, r1, r2, r1, kreg=kr(), kval=rk(), z=1, title="dst=src2"))
            r1, r2 = regs(2)
            emit(full_case(sp, vl, r2, r1, r1, title="src1=src2"))
            r1 = regs(1)[0]
            emit(full_case(sp, vl, r1, r1, r1, title="dst=src1=src2"))
        else:
            emit(full_case(sp, vl, r1, 0, r1, kreg=kr(), kval=rk(), title="dst=src"))
            emit(full_case(sp, vl, r2, 0, r2, title="dst=src nomask"))
        # special masks
        d, s1, s2 = regs(3)
        emit(full_case(sp, vl, d, s1, s2, kreg=kr(), kval=0, title="k=0 merge (dest unchanged)"))
        d, s1, s2 = regs(3)
        emit(full_case(sp, vl, d, s1, s2, kreg=kr(), kval=0, z=1, title="k=0 zero"))
        d, s1, s2 = regs(3)
        emit(full_case(sp, vl, d, s1, s2, kreg=kr(), kval=(1 << 64) - 1, z=1, title="k=all ones zero"))
        # crafted indices for the index-driven permutes
        if sp.fn in (f_perm, f_permt2):
            for v in sp.vls:
                d, s1, s2 = regs(3)
                emit(full_case(sp, v, d, s1, s2, pre={s1: index_image(sp, v, "src1")}, kreg=kr(), kval=rk(),
                               title="crafted indices in SRC1"))
        if sp.fn is f_permi2:
            for v in sp.vls:
                d, s1, s2 = regs(3)
                emit(full_case(sp, v, d, s1, s2, pre={d: index_image(sp, v, "dest")}, kreg=kr(), kval=rk(),
                               title="crafted indices in DEST"))
                d, s1 = regs(2)
                emit(full_case(sp, v, d, s1, 0, src="bcst", pre={d: index_image(sp, v, "dest")},
                               title="crafted indices, {1toN} table 2"))
        if sp.fn in (f_permilps_var, f_permilpd_var):
            d, s1, s2 = regs(3)
            emit(full_case(sp, vl, d, s1, s2, pre={s2: pack(list(range(16)), 4)}, title="control 0..15"))
        if sp.imm:
            for imm in IMMS[:4] + [RNG.getrandbits(8), RNG.getrandbits(8)]:
                d, s1, s2 = regs(3)
                emit(full_case(sp, vl, d, s1, s2, imm=imm, title="imm sweep"))
            if sp.fn is f_valign:
                for imm in range(vl // sp.esz + 1):
                    d, s1, s2 = regs(3)
                    emit(full_case(sp, vl, d, s1, s2, imm=imm, title="shift sweep"))
        full_uds(sp)


# ---------------------------------------------------------------------------------------
# 2a. VPMOVZX / VPMOVSX (E5; Half/Quarter/Eighth Mem)
# ---------------------------------------------------------------------------------------
EXTF = [  # name, opc, source size, dest size, signed, W (None = WIG)
    ("VPMOVSXBD", 0x21, 1, 4, True, None), ("VPMOVSXBQ", 0x22, 1, 8, True, None),
    ("VPMOVSXWD", 0x23, 2, 4, True, None), ("VPMOVSXWQ", 0x24, 2, 8, True, None),
    ("VPMOVSXDQ", 0x25, 4, 8, True, 0),
    ("VPMOVZXBD", 0x31, 1, 4, False, None), ("VPMOVZXBQ", 0x32, 1, 8, False, None),
    ("VPMOVZXWD", 0x33, 2, 4, False, None), ("VPMOVZXWQ", 0x34, 2, 8, False, None),
    ("VPMOVZXDQ", 0x35, 4, 8, False, 0),
]
TUPLE_NAME = {2: "Half Mem", 4: "Quarter Mem", 8: "Eighth Mem"}


def spec_vals(sz, n):
    """mix of saturation / sign boundary values and random values"""
    top = 1 << (8 * sz)
    cand = [0, 1, 2, 0x7F, 0x80, 0x81, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000,
            0xFFFFFFFF, 0x100000000, (top >> 1) - 1, top >> 1, top - 1, top - 2, top - 0x80, top - 0x81,
            top - 0x8000, top - 0x8001, top - 0x80000000, top - 0x80000001]
    cand = [x for x in cand if 0 <= x < top]   # (negative "top - c" candidates of 1/2-byte sizes)
    return [RNG.choice(cand) if RNG.getrandbits(1) else RNG.getrandbits(8 * sz) for _ in range(n)]


def ext_case(f, vl, dst, src, mode="reg", kreg=0, kval=None, z=0, w=None, disp=0x40, title=""):
    name, opc, ssz, dsz, signed, wfix = f
    kl = vl // dsz
    nb = kl * ssz
    if w is None:
        w = wfix if wfix is not None else RNG.getrandbits(1)
    c = Case("%s VL%d %s W%d" % (name, vl * 8, title, w))
    sv = spec_vals(ssz, kl)
    raw = pack(sv, ssz)
    imgs = {}
    if mode == "reg":
        if src == dst:
            imgs[dst] = raw + rb(64 - nb)
        else:
            imgs[dst] = rb(64)
            imgs[src] = raw + rb(64 - nb)
        rm, n = src, 1
    else:
        imgs[dst] = rb(64)
        set_mem(c, disp, raw)
        rm, n = Mem(RSI, disp), nb
    for r in imgs:
        c.set_zmm(r, imgs[r])
    res = [u(sgn(v, ssz), dsz) if signed else v for v in sv]
    if kreg:
        c.k[kreg] = kval
    out = mask_merge(imgs[dst], pack(res, dsz), dsz, vl, kval if kreg else None, z)
    c.code = evex(2, 1, w, opc, dst, rm, ll=LL[vl], z=z, aaa=kreg, n=n)
    exp_zmm(c, dst, out)
    return c


def gen_ext():
    section("VPMOVZX/VPMOVSX widening loads (E5, Half/Quarter/Eighth Mem)")
    for f in EXTF:
        name, opc, ssz, dsz, signed, wfix = f
        comment("%s (EVEX.66.0F38.%s %02X /r, %s, E5)" % (name, "W0" if wfix == 0 else "WIG", opc,
                                                           TUPLE_NAME[dsz // ssz]))
        for vl in VLS:
            d, s = regs(2)
            emit(ext_case(f, vl, d, s, title="reg nomask"))
            d, s = regs(2)
            emit(ext_case(f, vl, d, s, kreg=kr(), kval=rk(), title="reg merge"))
            d, s = regs(2)
            emit(ext_case(f, vl, d, s, kreg=kr(), kval=rk(), z=1, title="reg zero"))
            d = regs(1)[0]
            emit(ext_case(f, vl, d, 0, mode="mem", disp=RNG.choice([0x20, -0x40, 0x88]), title="mem nomask"))
            d = regs(1)[0]
            emit(ext_case(f, vl, d, 0, mode="mem", kreg=kr(), kval=rk(), z=RNG.getrandbits(1), disp=0x300,
                          title="mem masked"))
        r = regs(1)[0]
        emit(ext_case(f, 64, r, r, kreg=kr(), kval=rk(), title="dst=src"))
        if wfix is None:
            for w in (0, 1):
                d, s = regs(2)
                emit(ext_case(f, 64, d, s, w=w, title="WIG"))
        else:
            ud("%s EVEX.W1" % name, 2, 1, 1, opc, 1, 2, ll=2)
        ud("%s EVEX.b with memory" % name, 2, 1, 0, opc, 1, Mem(RSI, 0), ll=2, b=1, n=4)
        ud("%s EVEX.b register" % name, 2, 1, 0, opc, 1, 2, ll=2, b=1)
        ud("%s L'L=11" % name, 2, 1, 0, opc, 1, 2, ll=3)
        ud("%s EVEX.vvvv != 1111b" % name, 2, 1, 0, opc, 1, 2, ll=2, vvvv=9)
        ud("%s EVEX.V' = 0" % name, 2, 1, 0, opc, 1, 2, ll=2, p2_vp=0)
        ud("%s EVEX.z with aaa = 000b" % name, 2, 1, 0, opc, 1, 2, ll=2, z=1)


# ---------------------------------------------------------------------------------------
# 2b. narrowing VPMOV* (E6; register or memory destination)
# ---------------------------------------------------------------------------------------
NARROW = [  # name, opcode (F3 0F38), source size, dest size, kind (t truncate, s signed, u unsigned)
    ("VPMOVDB", 0x31, 4, 1, "t"), ("VPMOVSDB", 0x21, 4, 1, "s"), ("VPMOVUSDB", 0x11, 4, 1, "u"),
    ("VPMOVQB", 0x32, 8, 1, "t"), ("VPMOVSQB", 0x22, 8, 1, "s"), ("VPMOVUSQB", 0x12, 8, 1, "u"),
    ("VPMOVDW", 0x33, 4, 2, "t"), ("VPMOVSDW", 0x23, 4, 2, "s"), ("VPMOVUSDW", 0x13, 4, 2, "u"),
    ("VPMOVQW", 0x34, 8, 2, "t"), ("VPMOVSQW", 0x24, 8, 2, "s"), ("VPMOVUSQW", 0x14, 8, 2, "u"),
    ("VPMOVQD", 0x35, 8, 4, "t"), ("VPMOVSQD", 0x25, 8, 4, "s"), ("VPMOVUSQD", 0x15, 8, 4, "u"),
]


def narrow(v, ssz, dsz, kind):
    """TruncateXToY / SaturateSignedXToY / SaturateUnsignedXToY (source unsigned for US)"""
    if kind == "t":
        return u(v, dsz)
    if kind == "s":
        s = sgn(v, ssz)
        lo, hi = -(1 << (8 * dsz - 1)), (1 << (8 * dsz - 1)) - 1
        return u(min(max(s, lo), hi), dsz)
    return min(u(v, ssz), (1 << (8 * dsz)) - 1)


def narrow_case(f, vl, src, dst, mode="reg", kreg=0, kval=None, z=0, disp=-0x100, sv=None, title=""):
    name, opc, ssz, dsz, kind = f
    kl = vl // ssz
    nb = kl * dsz
    c = Case("%s VL%d %s" % (name, vl * 8, title))
    sv = sv or spec_vals(ssz, kl)
    simg = pack(sv, ssz) + rb(64 - vl)
    res = [narrow(v, ssz, dsz, kind) for v in sv]
    kmask = kval if kreg else None
    if kreg:
        c.k[kreg] = kval
    if mode == "reg":
        if dst == src:
            dimg = simg
        else:
            dimg = rb(64)
            c.set_zmm(dst, dimg)
        c.set_zmm(src, simg)
        # register destination: masked bytes/words/dwords, DEST[MAXVL-1:VL/r] := 0
        out = mask_merge(dimg, pack(res, dsz), dsz, nb, kmask, z)
        c.code = evex(2, 2, 0, opc, src, dst, ll=LL[vl], z=z, aaa=kreg)
        exp_zmm(c, dst, out)
    else:
        c.set_zmm(src, simg)
        old = rb(nb)
        set_mem(c, disp, old)
        oe = elems(old, dsz)
        new = [res[j] if kbits(kmask, j) else oe[j] for j in range(kl)]
        c.code = evex(2, 2, 0, opc, src, Mem(RSI, disp), ll=LL[vl], aaa=kreg, n=nb)
        exp_mem(c, disp, pack(new, dsz))
    return c


def gen_narrow():
    section("narrowing VPMOV*/VPMOVS*/VPMOVUS* (E6, Half/Quarter/Eighth Mem)")
    for f in NARROW:
        name, opc, ssz, dsz, kind = f
        comment("%s (EVEX.F3.0F38.W0 %02X /r, %s, E6)" % (name, opc, TUPLE_NAME[ssz // dsz]))
        for vl in VLS:
            s, d = regs(2)
            emit(narrow_case(f, vl, s, d, title="reg nomask"))
            s, d = regs(2)
            emit(narrow_case(f, vl, s, d, kreg=kr(), kval=rk(), title="reg merge"))
            s, d = regs(2)
            emit(narrow_case(f, vl, s, d, kreg=kr(), kval=rk(), z=1, title="reg zero"))
            s = regs(1)[0]
            emit(narrow_case(f, vl, s, 0, mode="mem", disp=RNG.choice([-0x100, 0x48, 0x400]), title="mem nomask"))
            s = regs(1)[0]
            emit(narrow_case(f, vl, s, 0, mode="mem", kreg=kr(), kval=rk(), disp=-0x300, title="mem merge"))
        # boundary values in every lane of a 512-bit source
        kl = 64 // ssz
        top = 1 << (8 * ssz)
        bvals = [0, 1, top - 1, (top >> 1) - 1, top >> 1, (1 << (8 * dsz - 1)) - 1, 1 << (8 * dsz - 1),
                 (1 << (8 * dsz)) - 1, 1 << (8 * dsz), top - (1 << (8 * dsz - 1)), top - (1 << (8 * dsz - 1)) - 1,
                 (1 << (8 * dsz - 1)), top - 2, 0x5A, top - 0x5A, (1 << (8 * dsz)) + 1]
        s, d = regs(2)
        emit(narrow_case(f, 64, s, d, sv=(bvals * 2)[:kl], title="boundary values"))
        s, d = regs(2)
        emit(narrow_case(f, 64, s, d, sv=(bvals[::-1] * 2)[:kl], title="boundary values (2)"))
        r = regs(1)[0]
        emit(narrow_case(f, 64, r, r, kreg=kr(), kval=rk(), title="dst=src"))
        s = regs(1)[0]
        emit(narrow_case(f, 64, s, 0, mode="mem", kreg=kr(), kval=0, title="mem k=0 (nothing written)"))
        ud("%s store {z}" % name, 2, 2, 0, opc, 1, Mem(RSI, 0), ll=2, z=1, aaa=1, n=64 // ssz * dsz)
        ud("%s EVEX.W1" % name, 2, 2, 1, opc, 1, 2, ll=2)
        ud("%s EVEX.b register" % name, 2, 2, 0, opc, 1, 2, ll=2, b=1)
        ud("%s EVEX.b memory" % name, 2, 2, 0, opc, 1, Mem(RSI, 0), ll=2, b=1, n=4)
        ud("%s L'L=11" % name, 2, 2, 0, opc, 1, 2, ll=3)
        ud("%s EVEX.vvvv != 1111b" % name, 2, 2, 0, opc, 1, 2, ll=2, vvvv=3)
        ud("%s EVEX.V' = 0" % name, 2, 2, 0, opc, 1, 2, ll=2, p2_vp=0)


# ---------------------------------------------------------------------------------------
# 3. compress / expand (Tuple1 Scalar, N = element size, E4.nb)
# ---------------------------------------------------------------------------------------
COMPRESS = [("VPCOMPRESSD", 0x8B, 0), ("VPCOMPRESSQ", 0x8B, 1), ("VCOMPRESSPS", 0x8A, 0), ("VCOMPRESSPD", 0x8A, 1)]
EXPAND = [("VPEXPANDD", 0x89, 0), ("VPEXPANDQ", 0x89, 1), ("VEXPANDPS", 0x88, 0), ("VEXPANDPD", 0x88, 1)]


def compress_case(f, vl, src, dst, mode="reg", kreg=0, kval=None, z=0, disp=-0x80, title=""):
    name, opc, w = f
    esz = 8 if w else 4
    kl = vl // esz
    c = Case("%s VL%d %s" % (name, vl * 8, title))
    simg = rb(64)
    sv = elems(simg[:vl], esz)
    kmask = kval if kreg else None
    if kreg:
        c.k[kreg] = kval
    act = [sv[j] for j in range(kl) if kbits(kmask, j)]
    if mode == "reg":
        dimg = simg if dst == src else rb(64)
        c.set_zmm(src, simg)
        c.set_zmm(dst, dimg)
        old = elems(dimg[:vl], esz)
        # "IF *merging-masking* THEN *DEST[VL-1:k] remains unchanged* ELSE DEST[VL-1:k] := 0"
        rest = [0] * (kl - len(act)) if z else old[len(act):]
        out = pack(act + rest, esz) + bytes(64 - vl)
        c.code = evex(2, 1, w, opc, src, dst, ll=LL[vl], z=z, aaa=kreg)
        exp_zmm(c, dst, out)
    else:
        c.set_zmm(src, simg)
        old = rb(vl)
        set_mem(c, disp, old)
        new = pack(act, esz) + old[len(act) * esz:]
        c.code = evex(2, 1, w, opc, src, Mem(RSI, disp), ll=LL[vl], aaa=kreg, n=esz)
        exp_mem(c, disp, new)
    return c


def expand_case(f, vl, dst, src, mode="reg", kreg=0, kval=None, z=0, disp=0x40, title=""):
    name, opc, w = f
    esz = 8 if w else 4
    kl = vl // esz
    c = Case("%s VL%d %s" % (name, vl * 8, title))
    kmask = kval if kreg else None
    if kreg:
        c.k[kreg] = kval
    if mode == "reg":
        simg = rb(64)
        dimg = simg if dst == src else rb(64)
        c.set_zmm(src, simg)
        c.set_zmm(dst, dimg)
        sv = elems(simg[:vl], esz)
        rm, n = src, 1
    else:
        dimg = rb(64)
        c.set_zmm(dst, dimg)
        data = rb(vl)
        set_mem(c, disp, data)
        sv = elems(data, esz)
        rm, n = Mem(RSI, disp), esz
    old = elems(dimg[:vl], esz)
    out, k = [], 0
    for j in range(kl):
        if kbits(kmask, j):
            out.append(sv[k])
            k += 1
        else:
            out.append(0 if z else old[j])
    c.code = evex(2, 1, w, opc, dst, rm, ll=LL[vl], z=z, aaa=kreg, n=n)
    exp_zmm(c, dst, pack(out, esz) + bytes(64 - vl))
    return c


def gen_compress_expand():
    section("VPCOMPRESSD/Q VCOMPRESSPS/PD VPEXPANDD/Q VEXPANDPS/PD (Tuple1 Scalar, E4.nb)")
    for f in COMPRESS:
        name, opc, w = f
        comment("%s (EVEX.66.0F38.W%d %02X /r, Tuple1 Scalar N=%d, E4.nb)" % (name, w, opc, 8 if w else 4))
        for vl in VLS:
            s, d = regs(2)
            emit(compress_case(f, vl, s, d, title="reg nomask (copy)"))
            s, d = regs(2)
            emit(compress_case(f, vl, s, d, kreg=kr(), kval=rk(), title="reg merge"))
            s, d = regs(2)
            emit(compress_case(f, vl, s, d, kreg=kr(), kval=rk(), z=1, title="reg zero"))
            s = regs(1)[0]
            emit(compress_case(f, vl, s, 0, mode="mem", kreg=kr(), kval=rk(), title="mem"))
            s = regs(1)[0]
            emit(compress_case(f, vl, s, 0, mode="mem", title="mem nomask", disp=0x500))
        s, d = regs(2)
        emit(compress_case(f, 64, s, d, kreg=kr(), kval=0b1010, title="reg merge k=1010b"))
        s, d = regs(2)
        emit(compress_case(f, 64, s, d, kreg=kr(), kval=0, z=1, title="reg zero k=0"))
        s = regs(1)[0]
        emit(compress_case(f, 64, s, 0, mode="mem", kreg=kr(), kval=0, title="mem k=0 (nothing written)"))
        r = regs(1)[0]
        emit(compress_case(f, 64, r, r, kreg=kr(), kval=rk(), title="dst=src merge"))
        ud("%s store {z}" % name, 2, 1, w, opc, 1, Mem(RSI, 0), ll=2, z=1, aaa=1, n=8 if w else 4)
        ud("%s EVEX.b memory" % name, 2, 1, w, opc, 1, Mem(RSI, 0), ll=2, b=1, aaa=1, n=8 if w else 4)
        ud("%s EVEX.b register" % name, 2, 1, w, opc, 1, 2, ll=2, b=1, aaa=1)
        ud("%s L'L=11" % name, 2, 1, w, opc, 1, 2, ll=3, aaa=1)
        ud("%s EVEX.vvvv != 1111b" % name, 2, 1, w, opc, 1, 2, ll=2, vvvv=3, aaa=1)   # vvvv=0 would encode 1111b
        ud("%s EVEX.V' = 0" % name, 2, 1, w, opc, 1, 2, ll=2, aaa=1, p2_vp=0)
    for f in EXPAND:
        name, opc, w = f
        comment("%s (EVEX.66.0F38.W%d %02X /r, Tuple1 Scalar N=%d, E4.nb)" % (name, w, opc, 8 if w else 4))
        for vl in VLS:
            d, s = regs(2)
            emit(expand_case(f, vl, d, s, title="reg nomask (copy)"))
            d, s = regs(2)
            emit(expand_case(f, vl, d, s, kreg=kr(), kval=rk(), title="reg merge"))
            d, s = regs(2)
            emit(expand_case(f, vl, d, s, kreg=kr(), kval=rk(), z=1, title="reg zero"))
            d = regs(1)[0]
            emit(expand_case(f, vl, d, 0, mode="mem", kreg=kr(), kval=rk(), z=RNG.getrandbits(1), title="mem"))
            d = regs(1)[0]
            emit(expand_case(f, vl, d, 0, mode="mem", title="mem nomask", disp=-0x500))
        d, s = regs(2)
        emit(expand_case(f, 64, d, s, kreg=kr(), kval=0b1010, title="reg merge k=1010b"))
        d = regs(1)[0]
        emit(expand_case(f, 64, d, 0, mode="mem", kreg=kr(), kval=0b1010, z=1, title="mem zero k=1010b"))
        r = regs(1)[0]
        emit(expand_case(f, 64, r, r, kreg=kr(), kval=rk(), title="dst=src merge"))
        ud("%s EVEX.b memory" % name, 2, 1, w, opc, 1, Mem(RSI, 0), ll=2, b=1, n=8 if w else 4)
        ud("%s EVEX.b register" % name, 2, 1, w, opc, 1, 2, ll=2, b=1)
        ud("%s L'L=11" % name, 2, 1, w, opc, 1, 2, ll=3)
        ud("%s EVEX.vvvv != 1111b" % name, 2, 1, w, opc, 1, 2, ll=2, vvvv=14)
        ud("%s EVEX.V' = 0" % name, 2, 1, w, opc, 1, 2, ll=2, p2_vp=0)


# ---------------------------------------------------------------------------------------
# 4a. VMOVNT* (E1NF, Full Mem, no masking)
# ---------------------------------------------------------------------------------------
NT = [("VMOVNTPS", 1, 0, 0x2B, 0, "st"), ("VMOVNTPD", 1, 1, 0x2B, 1, "st"),
      ("VMOVNTDQ", 1, 1, 0xE7, 0, "st"), ("VMOVNTDQA", 2, 1, 0x2A, 0, "ld")]


def nt_case(f, vl, r, disp, title=""):
    name, mmm, pp, opc, w, kind = f
    c = Case("%s VL%d %s" % (name, vl * 8, title))
    img = rb(64)
    c.set_zmm(r, img)
    if kind == "st":
        set_mem(c, disp, rb(vl))
        exp_mem(c, disp, img[:vl])
    else:
        data = rb(vl)
        set_mem(c, disp, data)
        exp_zmm(c, r, data + bytes(64 - vl))
    c.code = evex(mmm, pp, w, opc, r, Mem(RSI, disp), ll=LL[vl], n=vl)
    return c


def gen_nt():
    section("VMOVNTPS/PD VMOVNTDQ VMOVNTDQA (E1NF, Full Mem, no {k})")
    for f in NT:
        name, mmm, pp, opc, w, kind = f
        comment("%s (EVEX.%s.%s.W%d %02X /r, memory only, E1NF)" % (name, PPN[pp], MAPN[mmm], w, opc))
        a = (mmm, pp, w, opc)
        for vl in VLS:
            emit(nt_case(f, vl, regs(1)[0], vl * RNG.choice([1, 2, -1, -3]), title="aligned"))
            c = Case("%s VL%d misaligned (+%d) #GP" % (name, vl * 8, vl // 2))
            c.set_zmm(3, rb(64))
            c.code = evex(*a, 3, Mem(RSI, vl // 2, disp32=True), ll=LL[vl], n=vl)
            c.fault = "#GP"
            emit(c)
            c = Case("%s VL%d misaligned (+4) #GP" % (name, vl * 8))
            c.set_zmm(19, rb(64))
            c.code = evex(*a, 19, Mem(RSI, 4, disp32=True), ll=LL[vl], n=vl)
            c.fault = "#GP"
            emit(c)
        ud("%s {k1} (aaa != 0)" % name, *a, 1, Mem(RSI, 0), ll=2, aaa=1, n=64)
        ud("%s EVEX.z" % name, *a, 1, Mem(RSI, 0), ll=2, z=1, n=64)
        ud("%s EVEX.b" % name, *a, 1, Mem(RSI, 0), ll=2, b=1, n=64)
        ud("%s register form (mod = 11b)" % name, *a, 1, 2, ll=2)
        ud("%s wrong EVEX.W%d" % (name, 1 - w), mmm, pp, 1 - w, opc, 1, Mem(RSI, 0), ll=2, n=64)
        ud("%s L'L=11" % name, *a, 1, Mem(RSI, 0), ll=3, n=64)
        ud("%s EVEX.vvvv != 1111b" % name, *a, 1, Mem(RSI, 0), ll=2, vvvv=1, n=64)


# ---------------------------------------------------------------------------------------
# 4b. VMOVD/VMOVQ, VMOVHPS/HPD/LPS/LPD, VMOVHLPS/VMOVLHPS (E9NF / E7NM.128, no {k})
# ---------------------------------------------------------------------------------------
def gpr_case_in(c, g):
    v = rk()
    c.inp.append("%s=0x%X" % (GPRN[g], v))
    return v


def e9_common_uds(name, mmm, pp, w, opc, reg, rm, vvvv=None, imm=None, n=4, mem_ok=True):
    """E9NF / no-opmask #UD entries"""
    a = (mmm, pp, w, opc)
    ud("%s L'L=01" % name, *a, reg, rm, vvvv=vvvv, ll=1, imm=imm, n=n)
    ud("%s L'L=10" % name, *a, reg, rm, vvvv=vvvv, ll=2, imm=imm, n=n)
    ud("%s {k1} (aaa != 0)" % name, *a, reg, rm, vvvv=vvvv, aaa=2, imm=imm, n=n)
    ud("%s EVEX.z" % name, *a, reg, rm, vvvv=vvvv, z=1, imm=imm, n=n)
    if mem_ok:
        ud("%s EVEX.b memory" % name, *a, reg, Mem(RSI, 0), vvvv=vvvv, b=1, imm=imm, n=n)
    if vvvv is None:
        ud("%s EVEX.vvvv != 1111b" % name, *a, reg, rm, vvvv=6, imm=imm, n=n)
        ud("%s EVEX.V' = 0" % name, *a, reg, rm, imm=imm, n=n, p2_vp=0)


def gen_movdq():
    section("VMOVD/VMOVQ (E9NF, Tuple1 Scalar, no {k})")
    for w in (0, 1):
        sz = 8 if w else 4
        nm = "VMOVQ" if w else "VMOVD"
        comment("%s xmm1, r%d/m%d (EVEX.128.66.0F.W%d 6E /r)" % (nm, sz * 8, sz * 8, w))
        for _ in range(3):
            x, g = regs(1)[0], RNG.choice(GPR_OK)
            c = Case("%s xmm%d, %s" % (nm, x, GPRN[g]))
            c.set_zmm(x, rb(64))
            v = gpr_case_in(c, g)
            c.code = evex(1, 1, w, 0x6E, x, g)
            exp_zmm(c, x, u(v, sz).to_bytes(sz, "little") + bytes(64 - sz))
            emit(c)
            x = regs(1)[0]
            disp = sz * RNG.choice([1, 3, -5, 64])
            c = Case("%s xmm%d, m%d" % (nm, x, sz * 8))
            c.set_zmm(x, rb(64))
            data = rb(sz)
            set_mem(c, disp, data)
            c.code = evex(1, 1, w, 0x6E, x, Mem(RSI, disp), n=sz)
            exp_zmm(c, x, data + bytes(64 - sz))
            emit(c)
        # Table 2-41: "EVEX.X ... ModRM.r/m encodes k-reg or GPR: None (ignored)"
        x = regs(1)[0]
        c = Case("%s xmm%d, rcx with EVEX.X = 0 (ignored for a GPR r/m)" % (nm, x))
        c.set_zmm(x, rb(64))
        v = gpr_case_in(c, 1)
        c.code = evex(1, 1, w, 0x6E, x, 16 + 1)
        exp_zmm(c, x, u(v, sz).to_bytes(sz, "little") + bytes(64 - sz))
        emit(c)
        e9_common_uds("%s xmm, r/m (6E)" % nm, 1, 1, w, 0x6E, 1, 2, n=sz)
        comment("%s r%d/m%d, xmm1 (EVEX.128.66.0F.W%d 7E /r)" % (nm, sz * 8, sz * 8, w))
        for _ in range(3):
            x, g = regs(1)[0], RNG.choice(GPR_OK)
            c = Case("%s %s, xmm%d" % (nm, GPRN[g], x))
            img = rb(64)
            c.set_zmm(x, img)
            gpr_case_in(c, g)
            c.code = evex(1, 1, w, 0x7E, x, g)
            exp_gpr(c, g, int.from_bytes(img[:sz], "little"))    # r32 write zero-extends
            emit(c)
            x = regs(1)[0]
            disp = sz * RNG.choice([2, -7, 100])
            c = Case("%s m%d, xmm%d" % (nm, sz * 8, x))
            img = rb(64)
            c.set_zmm(x, img)
            set_mem(c, disp, rb(sz))
            c.code = evex(1, 1, w, 0x7E, x, Mem(RSI, disp), n=sz)
            exp_mem(c, disp, img[:sz])
            emit(c)
        e9_common_uds("%s r/m, xmm (7E)" % nm, 1, 1, w, 0x7E, 1, 2, n=sz)
    comment("VMOVQ xmm1, xmm2/m64 (EVEX.128.F3.0F.W1 7E /r); W0 = AVX10.2 VMOVD xmm, xmm/m32: #UD here")
    for _ in range(3):
        d, s = regs(2)
        c = Case("VMOVQ xmm%d, xmm%d (F3 7E)" % (d, s))
        c.set_zmm(d, rb(64))
        img = rb(64)
        c.set_zmm(s, img)
        c.code = evex(1, 2, 1, 0x7E, d, s)
        exp_zmm(c, d, img[:8] + bytes(56))
        emit(c)
        d = regs(1)[0]
        c = Case("VMOVQ xmm%d, m64 (F3 7E)" % d)
        c.set_zmm(d, rb(64))
        data = rb(8)
        set_mem(c, -0x18, data)
        c.code = evex(1, 2, 1, 0x7E, d, Mem(RSI, -0x18), n=8)
        exp_zmm(c, d, data + bytes(56))
        emit(c)
    r = regs(1)[0]
    c = Case("VMOVQ xmm%d, xmm%d (F3 7E, dst=src)" % (r, r))
    img = rb(64)
    c.set_zmm(r, img)
    c.code = evex(1, 2, 1, 0x7E, r, r)
    exp_zmm(c, r, img[:8] + bytes(56))
    emit(c)
    ud("VMOVD xmm, xmm (EVEX.F3.0F.W0 7E, AVX10.2 only)", 1, 2, 0, 0x7E, 1, 2)
    ud("VMOVD xmm, m32 (EVEX.F3.0F.W0 7E, AVX10.2 only)", 1, 2, 0, 0x7E, 1, Mem(RSI, 0), n=4)
    e9_common_uds("VMOVQ xmm, xmm/m64 (F3 7E)", 1, 2, 1, 0x7E, 1, 2, n=8)
    comment("VMOVQ xmm1/m64, xmm2 (EVEX.128.66.0F.W1 D6 /r); W0 = AVX10.2 VMOVD xmm/m32, xmm: #UD here")
    for _ in range(3):
        d, s = regs(2)
        c = Case("VMOVQ xmm%d, xmm%d (66 D6)" % (d, s))
        c.set_zmm(d, rb(64))
        img = rb(64)
        c.set_zmm(s, img)
        c.code = evex(1, 1, 1, 0xD6, s, d)
        exp_zmm(c, d, img[:8] + bytes(56))
        emit(c)
        s = regs(1)[0]
        c = Case("VMOVQ m64, xmm%d (66 D6)" % s)
        img = rb(64)
        c.set_zmm(s, img)
        set_mem(c, 0x28, rb(8))
        c.code = evex(1, 1, 1, 0xD6, s, Mem(RSI, 0x28), n=8)
        exp_mem(c, 0x28, img[:8])
        emit(c)
    ud("VMOVD xmm, xmm (EVEX.66.0F.W0 D6, AVX10.2 only)", 1, 1, 0, 0xD6, 1, 2)
    ud("VMOVD m32, xmm (EVEX.66.0F.W0 D6, AVX10.2 only)", 1, 1, 0, 0xD6, 1, Mem(RSI, 0), n=4)
    e9_common_uds("VMOVQ xmm/m64, xmm (66 D6)", 1, 1, 1, 0xD6, 1, 2, n=8)


HL = [  # name, pp, opc, W, kind
    ("VMOVHPS", 0, 0x16, 0, "loadh"), ("VMOVHPD", 1, 0x16, 1, "loadh"),
    ("VMOVLPS", 0, 0x12, 0, "loadl"), ("VMOVLPD", 1, 0x12, 1, "loadl"),
    ("VMOVHPS store", 0, 0x17, 0, "storeh"), ("VMOVHPD store", 1, 0x17, 1, "storeh"),
    ("VMOVLPS store", 0, 0x13, 0, "storel"), ("VMOVLPD store", 1, 0x13, 1, "storel"),
]


def hl_case(f, d, s1, disp, title=""):
    name, pp, opc, w, kind = f
    c = Case("%s %s" % (name, title))
    if kind.startswith("load"):
        imgs = {d: rb(64)}
        if s1 not in imgs:
            imgs[s1] = rb(64)
        for r in imgs:
            c.set_zmm(r, imgs[r])
        data = rb(8)
        set_mem(c, disp, data)
        src1 = imgs[s1]
        # VMOVHPS: DEST[63:0] := SRC1[63:0]; DEST[127:64] := SRC2[63:0]; DEST[MAXVL-1:128] := 0
        out = (src1[:8] + data) if kind == "loadh" else (data + src1[8:16])
        c.code = evex(1, pp, w, opc, d, Mem(RSI, disp), vvvv=s1, n=8)
        exp_zmm(c, d, out + bytes(48))
    else:
        img = rb(64)
        c.set_zmm(d, img)
        set_mem(c, disp, rb(8))
        c.code = evex(1, pp, w, opc, d, Mem(RSI, disp), n=8)
        exp_mem(c, disp, img[8:16] if kind == "storeh" else img[:8])
    return c


def gen_hl():
    section("VMOVHPS/HPD/LPS/LPD (E9NF, Tuple2 / Tuple1 Scalar N=8), VMOVHLPS/VMOVLHPS (E7NM.128)")
    for f in HL:
        name, pp, opc, w, kind = f
        comment("%s (EVEX.128.%s.0F.W%d %02X /r, memory only)" % (name, PPN[pp], w, opc))
        load = kind.startswith("load")
        for _ in range(3):
            d, s1 = regs(2)
            emit(hl_case(f, d, s1, 8 * RNG.choice([1, -2, 9, 33]), title="xmm%d" % d))
        if load:
            r = regs(1)[0]
            emit(hl_case(f, r, r, 0x10, title="dst=src1"))
        vv = 2 if load else None
        ud("%s register form (mod = 11b)" % name, 1, pp, w, opc, 1, 3, vvvv=vv) if (pp == 1 or not load) else None
        ud("%s wrong EVEX.W%d" % (name, 1 - w), 1, pp, 1 - w, opc, 1, Mem(RSI, 0), vvvv=vv, n=8)
        e9_common_uds(name, 1, pp, w, opc, 1, Mem(RSI, 0), vvvv=vv, n=8)
    for name, opc in (("VMOVHLPS", 0x12), ("VMOVLHPS", 0x16)):
        comment("%s xmm1, xmm2, xmm3 (EVEX.128.NP.0F.W0 %02X /r mod=11, E7NM.128)" % (name, opc))
        for t in range(5):
            d, s1, s2 = regs(3)
            if t == 3:
                s1 = d
            if t == 4:
                s2 = d
            c = Case("%s xmm%d, xmm%d, xmm%d" % (name, d, s1, s2))
            imgs = {}
            for r in (d, s1, s2):
                if r not in imgs:
                    imgs[r] = rb(64)
            for r in imgs:
                c.set_zmm(r, imgs[r])
            a, b = imgs[s1], imgs[s2]
            # VMOVHLPS: DEST[63:0] := SRC2[127:64]; DEST[127:64] := SRC1[127:64]
            # VMOVLHPS: DEST[63:0] := SRC1[63:0]; DEST[127:64] := SRC2[63:0]
            out = (b[8:16] + a[8:16]) if opc == 0x12 else (a[:8] + b[:8])
            c.code = evex(1, 0, 0, opc, d, s2, vvvv=s1)
            exp_zmm(c, d, out + bytes(48))
            emit(c)
        ud("%s wrong EVEX.W1" % name, 1, 0, 1, opc, 1, 3, vvvv=2)
        ud("%s EVEX.b register" % name, 1, 0, 0, opc, 1, 3, vvvv=2, b=1)
        e9_common_uds(name, 1, 0, 0, opc, 1, 3, vvvv=2, mem_ok=False)
        ud("%s L'L=11" % name, 1, 0, 0, opc, 1, 3, vvvv=2, ll=3)


# ---------------------------------------------------------------------------------------
# 5a. VPINSRD/Q, VPEXTRD/Q, VINSERTPS, VEXTRACTPS (E9NF, Tuple1 Scalar)
# ---------------------------------------------------------------------------------------
def insertps(src1, src2, imm, is_reg):
    """VINSERTPS: COUNT_S := imm8[7:6] (register source only), COUNT_D := imm8[5:4],
    ZMASK := imm8[3:0]"""
    cs = (imm >> 6) & 3 if is_reg else 0
    tmp = src2[cs]
    t2 = list(src1[:4])
    t2[(imm >> 4) & 3] = tmp
    return [0 if (imm >> i) & 1 else t2[i] for i in range(4)]


def pinsr_case(w, d, s1, rm_gpr, imm, disp=0x30, title=""):
    sz = 8 if w else 4
    nm = "VPINSRQ" if w else "VPINSRD"
    c = Case("%s %s imm=0x%02X" % (nm, title, imm))
    imgs = {d: rb(64)}
    if s1 not in imgs:
        imgs[s1] = rb(64)
    for r in imgs:
        c.set_zmm(r, imgs[r])
    if rm_gpr is not None:
        v = u(gpr_case_in(c, rm_gpr), sz)
        rm, n = rm_gpr, 1
    else:
        data = rb(sz)
        set_mem(c, disp, data)
        v = int.from_bytes(data, "little")
        rm, n = Mem(RSI, disp), sz
    e = elems(imgs[s1][:16], sz)
    e[imm & (16 // sz - 1)] = v           # SEL := imm8[1:0] (D) / imm8[0] (Q)
    c.code = evex(3, 1, w, 0x22, d, rm, vvvv=s1, imm=imm, n=n)
    exp_zmm(c, d, pack(e, sz) + bytes(48))
    return c


def pextr_case(w, s, rm_gpr, imm, disp=-0x30, title=""):
    sz = 8 if w else 4
    nm = "VPEXTRQ" if w else "VPEXTRD"
    c = Case("%s %s imm=0x%02X" % (nm, title, imm))
    img = rb(64)
    c.set_zmm(s, img)
    v = elems(img[:16], sz)[imm & (16 // sz - 1)]
    if rm_gpr is not None:
        gpr_case_in(c, rm_gpr)
        c.code = evex(3, 1, w, 0x16, s, rm_gpr, imm=imm)
        exp_gpr(c, rm_gpr, v)                    # r32 destination: upper 32 bits zeroed
    else:
        set_mem(c, disp, rb(sz))
        c.code = evex(3, 1, w, 0x16, s, Mem(RSI, disp), imm=imm, n=sz)
        exp_mem(c, disp, v.to_bytes(sz, "little"))
    return c


def insertps_case(d, s1, s2, imm, disp=0x44, title=""):
    c = Case("VINSERTPS %s imm=0x%02X" % (title, imm))
    imgs = {}
    for r in (d, s1) + ((s2,) if s2 is not None else ()):
        if r not in imgs:
            imgs[r] = rb(64)
    for r in imgs:
        c.set_zmm(r, imgs[r])
    a = elems(imgs[s1][:16], 4)
    if s2 is not None:
        b = elems(imgs[s2][:16], 4)
        rm, n = s2, 1
    else:
        data = rb(4)
        set_mem(c, disp, data)
        b = elems(data, 4)
        rm, n = Mem(RSI, disp), 4
    out = insertps(a, b, imm, s2 is not None)
    c.code = evex(3, 1, 0, 0x21, d, rm, vvvv=s1, imm=imm, n=n)
    exp_zmm(c, d, pack(out, 4) + bytes(48))
    return c


def extractps_case(s, rm_gpr, imm, w=0, disp=0x64, title=""):
    c = Case("VEXTRACTPS W%d %s imm=0x%02X" % (w, title, imm))
    img = rb(64)
    c.set_zmm(s, img)
    v = elems(img[:16], 4)[imm & 3]
    if rm_gpr is not None:
        gpr_case_in(c, rm_gpr)
        c.code = evex(3, 1, w, 0x17, s, rm_gpr, imm=imm)
        exp_gpr(c, rm_gpr, v)                    # "DEST[63:32] := 0" in 64-bit mode
    else:
        set_mem(c, disp, rb(4))
        c.code = evex(3, 1, w, 0x17, s, Mem(RSI, disp), imm=imm, n=4)
        exp_mem(c, disp, v.to_bytes(4, "little"))
    return c


def gen_insext_scalar():
    section("VPINSRD/Q VPEXTRD/Q (DQ), VINSERTPS, VEXTRACTPS (E9NF, Tuple1 Scalar)")
    for w in (0, 1):
        nm = "VPINSRQ" if w else "VPINSRD"
        comment("%s xmm1, xmm2, r/m%d, imm8 (EVEX.128.66.0F3A.W%d 22 /r ib)" % (nm, 64 if w else 32, w))
        for imm in (0, 1, 2, 3, 0xFE, 0xFF):
            d, s1 = regs(2)
            emit(pinsr_case(w, d, s1, RNG.choice(GPR_OK), imm, title="xmm%d, xmm%d, gpr" % (d, s1)))
        for imm in (0, 1, 3, 0x81):
            d, s1 = regs(2)
            emit(pinsr_case(w, d, s1, None, imm, disp=RNG.choice([0x30, -8, 0x400]) & ~7, title="mem"))
        r = regs(1)[0]
        emit(pinsr_case(w, r, r, RNG.choice(GPR_OK), 1, title="dst=src1"))
        e9_common_uds(nm, 3, 1, w, 0x22, 1, 2, vvvv=3, imm=0, n=8 if w else 4)
        nm = "VPEXTRQ" if w else "VPEXTRD"
        comment("%s r/m%d, xmm2, imm8 (EVEX.128.66.0F3A.W%d 16 /r ib)" % (nm, 64 if w else 32, w))
        for imm in (0, 1, 2, 3, 0x7C, 0xFF):
            emit(pextr_case(w, regs(1)[0], RNG.choice(GPR_OK), imm, title="gpr"))
        for imm in (0, 1, 2, 0xC3):
            emit(pextr_case(w, regs(1)[0], None, imm, disp=RNG.choice([-0x30, 0x58, 0x1000]) & ~7, title="mem"))
        e9_common_uds(nm, 3, 1, w, 0x16, 1, 2, imm=0, n=8 if w else 4)
    comment("VINSERTPS xmm1, xmm2, xmm3/m32, imm8 (EVEX.128.66.0F3A.W0 21 /r ib)")
    for imm in (0x00, 0xFF, 0x1B, 0xE4, 0x30, 0xC0, 0x4A, 0x9F, 0x0F, 0x70, RNG.getrandbits(8), RNG.getrandbits(8)):
        d, s1, s2 = regs(3)
        emit(insertps_case(d, s1, s2, imm, title="reg"))
    for imm in (0x00, 0xF0, 0xE5, 0x2A, RNG.getrandbits(8)):
        d, s1 = regs(2)
        emit(insertps_case(d, s1, None, imm, title="m32 (COUNT_S ignored)"))
    r1, r2 = regs(2)
    emit(insertps_case(r1, r1, r2, 0x96, title="dst=src1"))
    emit(insertps_case(r2, r1, r2, 0x5C, title="dst=src2"))
    ud("VINSERTPS EVEX.W1", 3, 1, 1, 0x21, 1, 2, vvvv=3, imm=0)
    ud("VINSERTPS EVEX.b register", 3, 1, 0, 0x21, 1, 2, vvvv=3, imm=0, b=1)
    e9_common_uds("VINSERTPS", 3, 1, 0, 0x21, 1, 2, vvvv=3, imm=0, n=4)
    comment("VEXTRACTPS r/m32, xmm1, imm8 (EVEX.128.66.0F3A.WIG 17 /r ib)")
    for imm in (0, 1, 2, 3, 0xFE, 0x55):
        emit(extractps_case(regs(1)[0], RNG.choice(GPR_OK), imm, w=RNG.getrandbits(1), title="gpr"))
    for w in (0, 1):
        emit(extractps_case(regs(1)[0], RNG.choice(GPR_OK), 2, w=w, title="gpr WIG"))
        emit(extractps_case(regs(1)[0], None, 3, w=w, title="m32 WIG"))
    e9_common_uds("VEXTRACTPS", 3, 1, 0, 0x17, 1, 2, imm=0, n=4)


# ---------------------------------------------------------------------------------------
# 5b. VINSERTx / VEXTRACTx 32X4 64X2 32X8 64X4 (E6NF, Tuple2/4/8)
# ---------------------------------------------------------------------------------------
INS = [  # name, opc, W, element size, chunk bytes, VLs, tuple
    ("VINSERTF32X4", 0x18, 0, 4, 16, (32, 64), "Tuple4"), ("VINSERTF64X2", 0x18, 1, 8, 16, (32, 64), "Tuple2"),
    ("VINSERTF32X8", 0x1A, 0, 4, 32, (64,), "Tuple8"), ("VINSERTF64X4", 0x1A, 1, 8, 32, (64,), "Tuple4"),
    ("VINSERTI32X4", 0x38, 0, 4, 16, (32, 64), "Tuple4"), ("VINSERTI64X2", 0x38, 1, 8, 16, (32, 64), "Tuple2"),
    ("VINSERTI32X8", 0x3A, 0, 4, 32, (64,), "Tuple8"), ("VINSERTI64X4", 0x3A, 1, 8, 32, (64,), "Tuple4"),
]
EXTR = [
    ("VEXTRACTF32X4", 0x19, 0, 4, 16, (32, 64), "Tuple4"), ("VEXTRACTF64X2", 0x19, 1, 8, 16, (32, 64), "Tuple2"),
    ("VEXTRACTF32X8", 0x1B, 0, 4, 32, (64,), "Tuple8"), ("VEXTRACTF64X4", 0x1B, 1, 8, 32, (64,), "Tuple4"),
    ("VEXTRACTI32X4", 0x39, 0, 4, 16, (32, 64), "Tuple4"), ("VEXTRACTI64X2", 0x39, 1, 8, 16, (32, 64), "Tuple2"),
    ("VEXTRACTI32X8", 0x3B, 0, 4, 32, (64,), "Tuple8"), ("VEXTRACTI64X4", 0x3B, 1, 8, 32, (64,), "Tuple4"),
]


def insert_case(f, vl, d, s1, s2, imm, kreg=0, kval=None, z=0, disp=0x80, title=""):
    name, opc, w, esz, ch, vls, tup = f
    c = Case("%s VL%d %s imm=0x%02X" % (name, vl * 8, title, imm))
    imgs = {}
    for r in (d, s1) + ((s2,) if s2 is not None else ()):
        if r not in imgs:
            imgs[r] = rb(64)
    for r in imgs:
        c.set_zmm(r, imgs[r])
    if s2 is not None:
        piece = imgs[s2][:ch]
        rm, n = s2, 1
    else:
        piece = rb(ch)
        set_mem(c, disp, piece)
        rm, n = Mem(RSI, disp), ch
    pos = imm & (vl // ch - 1)                 # imm8[0] (VL256 / x8 / x4-64) or imm8[1:0] (VL512)
    tmp = bytearray(imgs[s1][:vl])
    tmp[pos * ch:(pos + 1) * ch] = piece
    if kreg:
        c.k[kreg] = kval
    out = mask_merge(imgs[d], bytes(tmp), esz, vl, kval if kreg else None, z)
    c.code = evex(3, 1, w, opc, d, rm, vvvv=s1, ll=LL[vl], z=z, aaa=kreg, imm=imm, n=n)
    exp_zmm(c, d, out)
    return c


def extract_case(f, vl, s, d, imm, kreg=0, kval=None, z=0, disp=-0x80, title=""):
    name, opc, w, esz, ch, vls, tup = f
    c = Case("%s VL%d %s imm=0x%02X" % (name, vl * 8, title, imm))
    simg = rb(64)
    pos = imm & (vl // ch - 1)
    piece = simg[pos * ch:(pos + 1) * ch]
    kmask = kval if kreg else None
    if kreg:
        c.k[kreg] = kval
    if d is not None:
        dimg = simg if d == s else rb(64)
        c.set_zmm(s, simg)
        c.set_zmm(d, dimg)
        out = mask_merge(dimg, piece, esz, ch, kmask, z)      # DEST[MAXVL-1:128/256] := 0
        c.code = evex(3, 1, w, opc, s, d, ll=LL[vl], z=z, aaa=kreg, imm=imm)
        exp_zmm(c, d, out)
    else:
        c.set_zmm(s, simg)
        old = rb(ch)
        set_mem(c, disp, old)
        oe, pe = elems(old, esz), elems(piece, esz)
        new = [pe[j] if kbits(kmask, j) else oe[j] for j in range(ch // esz)]
        c.code = evex(3, 1, w, opc, s, Mem(RSI, disp), ll=LL[vl], aaa=kreg, imm=imm, n=ch)
        exp_mem(c, disp, pack(new, esz))
    return c


def gen_insext_vec():
    section("VINSERTF/I 32X4 64X2 32X8 64X4, VEXTRACTF/I ... (E6NF, Tuple2/Tuple4/Tuple8)")
    for f in INS:
        name, opc, w, esz, ch, vls, tup = f
        comment("%s (EVEX.66.0F3A.W%d %02X /r ib, %s N=%d, E6NF)" % (name, w, opc, tup, ch))
        for vl in vls:
            for imm in range(vl // ch):
                d, s1, s2 = regs(3)
                emit(insert_case(f, vl, d, s1, s2, imm | (RNG.getrandbits(6) << 2), title="reg"))
            d, s1, s2 = regs(3)
            emit(insert_case(f, vl, d, s1, s2, RNG.getrandbits(8), kreg=kr(), kval=rk(), title="reg merge"))
            d, s1, s2 = regs(3)
            emit(insert_case(f, vl, d, s1, s2, RNG.getrandbits(8), kreg=kr(), kval=rk(), z=1, title="reg zero"))
            d, s1 = regs(2)
            emit(insert_case(f, vl, d, s1, None, RNG.getrandbits(8), title="mem"))
            d, s1 = regs(2)
            emit(insert_case(f, vl, d, s1, None, RNG.getrandbits(8), kreg=kr(), kval=rk(), z=RNG.getrandbits(1),
                             disp=-0x240, title="mem masked"))
        r1, r2 = regs(2)
        emit(insert_case(f, 64, r1, r1, r2, 0xFF, kreg=kr(), kval=rk(), title="dst=src1"))
        emit(insert_case(f, 64, r2, r1, r2, 0x00, title="dst=src2"))
        r1 = regs(1)[0]
        emit(insert_case(f, 64, r1, r1, r1, 0x01, title="dst=src1=src2"))
        a = (3, 1, w, opc)
        ud("%s EVEX.b register" % name, *a, 1, 3, vvvv=2, ll=2, b=1, imm=0)
        ud("%s EVEX.b memory" % name, *a, 1, Mem(RSI, 0), vvvv=2, ll=2, b=1, imm=0, n=ch)
        ud("%s L'L=11" % name, *a, 1, 3, vvvv=2, ll=3, imm=0)
        ud("%s VL%d not encodable" % (name, min(vls) * 4), *a, 1, 3, vvvv=2, ll=LL[min(vls)] - 1, imm=0)
        ud("%s EVEX.z with aaa = 000b" % name, *a, 1, 3, vvvv=2, ll=2, z=1, imm=0)
    for f in EXTR:
        name, opc, w, esz, ch, vls, tup = f
        comment("%s (EVEX.66.0F3A.W%d %02X /r ib, %s N=%d, E6NF)" % (name, w, opc, tup, ch))
        for vl in vls:
            for imm in range(vl // ch):
                s, d = regs(2)
                emit(extract_case(f, vl, s, d, imm | (RNG.getrandbits(6) << 2), title="reg"))
            s, d = regs(2)
            emit(extract_case(f, vl, s, d, RNG.getrandbits(8), kreg=kr(), kval=rk(), title="reg merge"))
            s, d = regs(2)
            emit(extract_case(f, vl, s, d, RNG.getrandbits(8), kreg=kr(), kval=rk(), z=1, title="reg zero"))
            emit(extract_case(f, vl, regs(1)[0], None, RNG.getrandbits(8), disp=0x140, title="mem"))
            emit(extract_case(f, vl, regs(1)[0], None, RNG.getrandbits(8), kreg=kr(), kval=rk(), title="mem merge"))
        r = regs(1)[0]
        emit(extract_case(f, 64, r, r, 0x3, kreg=kr(), kval=rk(), title="dst=src"))
        a = (3, 1, w, opc)
        ud("%s store {z}" % name, *a, 1, Mem(RSI, 0), ll=2, z=1, aaa=1, imm=0, n=ch)
        ud("%s EVEX.b register" % name, *a, 1, 3, ll=2, b=1, imm=0)
        ud("%s EVEX.b memory" % name, *a, 1, Mem(RSI, 0), ll=2, b=1, imm=0, n=ch)
        ud("%s L'L=11" % name, *a, 1, 3, ll=3, imm=0)
        ud("%s VL%d not encodable" % (name, min(vls) * 4), *a, 1, 3, ll=LL[min(vls)] - 1, imm=0)
        ud("%s EVEX.vvvv != 1111b" % name, *a, 1, 3, ll=2, vvvv=7, imm=0)
        ud("%s EVEX.V' = 0" % name, *a, 1, 3, ll=2, imm=0, p2_vp=0)


# ---------------------------------------------------------------------------------------
# 5c. VBROADCASTF/I 32X2 32X4 64X2 32X8 64X4 (E6, Tuple2/4/8)
# ---------------------------------------------------------------------------------------
BC = [  # name, opc, W, element size, tuple bytes, VLs, register source allowed, tuple
    ("VBROADCASTF32X2", 0x19, 0, 4, 8, (32, 64), True, "Tuple2"),
    ("VBROADCASTI32X2", 0x59, 0, 4, 8, (16, 32, 64), True, "Tuple2"),
    ("VBROADCASTF32X4", 0x1A, 0, 4, 16, (32, 64), False, "Tuple4"),
    ("VBROADCASTF64X2", 0x1A, 1, 8, 16, (32, 64), False, "Tuple2"),
    ("VBROADCASTF32X8", 0x1B, 0, 4, 32, (64,), False, "Tuple8"),
    ("VBROADCASTF64X4", 0x1B, 1, 8, 32, (64,), False, "Tuple4"),
    ("VBROADCASTI32X4", 0x5A, 0, 4, 16, (32, 64), False, "Tuple4"),
    ("VBROADCASTI64X2", 0x5A, 1, 8, 16, (32, 64), False, "Tuple2"),
    ("VBROADCASTI32X8", 0x5B, 0, 4, 32, (64,), False, "Tuple8"),
    ("VBROADCASTI64X4", 0x5B, 1, 8, 32, (64,), False, "Tuple4"),
]


def bcast_case(f, vl, d, s, kreg=0, kval=None, z=0, disp=0x60, title=""):
    name, opc, w, esz, tb, vls, regok, tup = f
    gs = tb // esz
    c = Case("%s VL%d %s" % (name, vl * 8, title))
    dimg = rb(64)
    c.set_zmm(d, dimg)
    if s is not None:
        simg = dimg if s == d else rb(64)
        c.set_zmm(s, simg)
        grp = simg[:tb]
        rm, n = s, 1
    else:
        grp = rb(tb)
        set_mem(c, disp, grp)
        rm, n = Mem(RSI, disp), tb
    g = elems(grp, esz)
    res = [g[j % gs] for j in range(vl // esz)]       # n := (j mod group) * size
    if kreg:
        c.k[kreg] = kval
    out = mask_merge(dimg, pack(res, esz), esz, vl, kval if kreg else None, z)
    c.code = evex(2, 1, w, opc, d, rm, ll=LL[vl], z=z, aaa=kreg, n=n)
    exp_zmm(c, d, out)
    return c


def gen_bcast():
    section("VBROADCASTF/I 32X2 32X4 64X2 32X8 64X4 (E6, Tuple2/Tuple4/Tuple8)")
    for f in BC:
        name, opc, w, esz, tb, vls, regok, tup = f
        comment("%s (EVEX.66.0F38.W%d %02X /r, %s N=%d, E6%s)" % (name, w, opc, tup, tb,
                                                                 "" if regok else ", m only"))
        for vl in vls:
            emit(bcast_case(f, vl, regs(1)[0], None, disp=RNG.choice([0x60, -0x20, 0x200]) & ~(tb - 1),
                            title="mem nomask"))
            emit(bcast_case(f, vl, regs(1)[0], None, kreg=kr(), kval=rk(), title="mem merge"))
            emit(bcast_case(f, vl, regs(1)[0], None, kreg=kr(), kval=rk(), z=1, disp=-0x400, title="mem zero"))
            if regok:
                d, s = regs(2)
                emit(bcast_case(f, vl, d, s, title="reg nomask"))
                d, s = regs(2)
                emit(bcast_case(f, vl, d, s, kreg=kr(), kval=rk(), z=RNG.getrandbits(1), title="reg masked"))
        if regok:
            r = regs(1)[0]
            emit(bcast_case(f, max(vls), r, r, kreg=kr(), kval=rk(), title="dst=src"))
        else:
            ud("%s register source (mod = 11b)" % name, 2, 1, w, opc, 1, 2, ll=2)
        a = (2, 1, w, opc)
        ud("%s EVEX.b memory" % name, *a, 1, Mem(RSI, 0), ll=2, b=1, n=esz)
        ud("%s L'L=11" % name, *a, 1, Mem(RSI, 0), ll=3, n=tb)
        for vl in VLS:
            if vl not in vls:
                ud("%s VL%d not encodable" % (name, vl * 8), *a, 1, Mem(RSI, 0), ll=LL[vl], n=tb)
        ud("%s EVEX.vvvv != 1111b" % name, *a, 1, Mem(RSI, 0), ll=2, vvvv=4, n=tb)
        ud("%s EVEX.V' = 0" % name, *a, 1, Mem(RSI, 0), ll=2, n=tb, p2_vp=0)
        ud("%s EVEX.z with aaa = 000b" % name, *a, 1, Mem(RSI, 0), ll=2, z=1, n=tb)


# ---------------------------------------------------------------------------------------
# 6. disp8*N for every tuple type of this milestone (Tables 2-36 / 2-37)
# ---------------------------------------------------------------------------------------
D8 = (1, -1, 127, -127)


def ff(name):
    for sp in FULL + DUP:
        if sp.name == name:
            return sp
    raise KeyError(name)


def gen_disp8():
    section("disp8*N: +-1 and +-127 for every tuple type (Tables 2-36 / 2-37)")
    comment("Full (Table 2-36): N = VL (EVEX.b = 0) / element size (EVEX.b = 1), W0 and W1")
    for name in ("VPUNPCKLDQ", "VPUNPCKLQDQ"):
        sp = ff(name)
        for vl in VLS:
            for bc in ("mem", "bcst"):
                n = sp.esz if bc == "bcst" else vl
                for d8 in D8:
                    d, s1 = regs(2)
                    emit(full_case(sp, vl, d, s1, 0, src=bc, disp=d8 * n, title="Full %s disp8=%d N=%d" % (bc, d8, n)))
    comment("Full Mem (Table 2-37): N = VL")
    for vl in VLS:
        for d8 in D8:
            d = regs(1)[0]
            emit(full_case(ff("VMOVSHDUP"), vl, d, 0, 0, src="mem", disp=d8 * vl, title="FullMem disp8=%d N=%d" % (d8, vl)))
            emit(nt_case(NT[2], vl, regs(1)[0], d8 * vl, title="FullMem disp8=%d N=%d" % (d8, vl)))
    comment("MOVDDUP tuple: N = 8 (VL128), 32, 64")
    for vl in VLS:
        n = 8 if vl == 16 else vl
        for d8 in D8:
            emit(full_case(ff("VMOVDDUP"), vl, regs(1)[0], 0, 0, src="mem", disp=d8 * n,
                           title="MOVDDUP disp8=%d N=%d" % (d8, n)))
    comment("Half / Quarter / Eighth Mem: N = VL/2, VL/4, VL/8 (load: VPMOVZX, store: VPMOV)")
    for ename, nname in (("VPMOVZXWD", "VPMOVDW"), ("VPMOVZXBD", "VPMOVDB"), ("VPMOVZXBQ", "VPMOVQB")):
        ef = [f for f in EXTF if f[0] == ename][0]
        nf = [f for f in NARROW if f[0] == nname][0]
        for vl in VLS:
            n = vl * ef[2] // ef[3]
            for d8 in D8:
                emit(ext_case(ef, vl, regs(1)[0], 0, mode="mem", disp=d8 * n, title="disp8=%d N=%d" % (d8, n)))
                emit(narrow_case(nf, vl, regs(1)[0], 0, mode="mem", disp=d8 * n, title="disp8=%d N=%d" % (d8, n)))
    comment("Tuple1 Scalar: N = 4 / 8 (VMOVD/VMOVQ, VPINSR, VPEXTR, VINSERTPS, VEXTRACTPS, compress/expand)")
    for d8 in D8:
        for w in (0, 1):
            sz = 8 if w else 4
            x = regs(1)[0]
            c = Case("%s xmm%d, m disp8=%d N=%d" % ("VMOVQ" if w else "VMOVD", x, d8, sz))
            c.set_zmm(x, rb(64))
            data = rb(sz)
            set_mem(c, d8 * sz, data)
            c.code = evex(1, 1, w, 0x6E, x, Mem(RSI, d8 * sz), n=sz)
            exp_zmm(c, x, data + bytes(64 - sz))
            emit(c)
            d, s1 = regs(2)
            emit(pinsr_case(w, d, s1, None, 1, disp=d8 * sz, title="disp8=%d N=%d" % (d8, sz)))
            emit(pextr_case(w, regs(1)[0], None, 1, disp=d8 * sz, title="disp8=%d N=%d" % (d8, sz)))
            emit(compress_case(COMPRESS[w], 64, regs(1)[0], 0, mode="mem", kreg=kr(), kval=rk(), disp=d8 * sz,
                               title="disp8=%d N=%d" % (d8, sz)))
            emit(expand_case(EXPAND[2 + w], 64, regs(1)[0], 0, mode="mem", kreg=kr(), kval=rk(), disp=d8 * sz,
                             title="disp8=%d N=%d" % (d8, sz)))
        d, s1 = regs(2)
        emit(insertps_case(d, s1, None, 0x20, disp=d8 * 4, title="disp8=%d N=4" % d8))
        emit(extractps_case(regs(1)[0], None, 2, disp=d8 * 4, title="disp8=%d N=4" % d8))
    comment("Tuple2: N = 8 (VMOVHPS/VMOVLPS, VBROADCASTF32X2), 16 (VBROADCASTF64X2, VINSERTF64X2, VEXTRACTF64X2)")
    for d8 in D8:
        d, s1 = regs(2)
        emit(hl_case(HL[0], d, s1, d8 * 8, title="disp8=%d N=8" % d8))
        emit(hl_case(HL[6], regs(1)[0], 0, d8 * 8, title="disp8=%d N=8" % d8))
        emit(bcast_case(BC[0], 64, regs(1)[0], None, disp=d8 * 8, title="disp8=%d N=8" % d8))
        emit(bcast_case(BC[3], 64, regs(1)[0], None, disp=d8 * 16, title="disp8=%d N=16" % d8))
        d, s1 = regs(2)
        emit(insert_case(INS[1], 64, d, s1, None, 2, disp=d8 * 16, title="disp8=%d N=16" % d8))
        emit(extract_case(EXTR[1], 64, regs(1)[0], None, 3, disp=d8 * 16, title="disp8=%d N=16" % d8))
    comment("Tuple4: N = 16 (32x4 forms), 32 (64x4 forms)")
    for d8 in D8:
        emit(bcast_case(BC[2], 32, regs(1)[0], None, disp=d8 * 16, title="disp8=%d N=16" % d8))
        d, s1 = regs(2)
        emit(insert_case(INS[0], 64, d, s1, None, 1, disp=d8 * 16, title="disp8=%d N=16" % d8))
        emit(extract_case(EXTR[4], 64, regs(1)[0], None, 2, disp=d8 * 16, title="disp8=%d N=16" % d8))
        emit(bcast_case(BC[5], 64, regs(1)[0], None, disp=d8 * 32, title="disp8=%d N=32" % d8))
        d, s1 = regs(2)
        emit(insert_case(INS[7], 64, d, s1, None, 1, disp=d8 * 32, title="disp8=%d N=32" % d8))
        emit(extract_case(EXTR[3], 64, regs(1)[0], None, 1, disp=d8 * 32, title="disp8=%d N=32" % d8))
    comment("Tuple8: N = 32 (VBROADCASTF32X8, VINSERTF32X8, VEXTRACTI32X8)")
    for d8 in D8:
        emit(bcast_case(BC[4], 64, regs(1)[0], None, disp=d8 * 32, title="disp8=%d N=32" % d8))
        d, s1 = regs(2)
        emit(insert_case(INS[2], 64, d, s1, None, 0, disp=d8 * 32, title="disp8=%d N=32" % d8))
        emit(extract_case(EXTR[6], 64, regs(1)[0], None, 1, disp=d8 * 32, title="disp8=%d N=32" % d8))
    comment("SIB / R14 base with disp8*N (VPERMD [r14 + rcx*8 + 2*64])")
    sp = ff("VPERMD")
    c = full_case(sp, 64, 5, 6, 0, src="mem", disp=0x80 + 0x40, title="[r14+rcx*8+disp8*64]")
    c.inp.append("rcx=0x8")
    c.code = evex(2, 1, 0, 0x36, 5, Mem(R14, 0x80, index=1, scale=3), vvvv=6, ll=2, n=64)
    emit(c)


# ---------------------------------------------------------------------------------------
# 7. memory fault suppression next to the unmapped page MEM+0x10000
# ---------------------------------------------------------------------------------------
def pf_mem(x):
    """[rsi + disp32]: the operand starts x bytes before MEM+0x10000"""
    return Mem(RSI, PAGE_END - x, disp32=True), PAGE_END - x


def gen_faults():
    section("memory fault suppression at the unmapped page MEM+0x10000 (E4/E5/E6 vs NF classes)")
    comment("(a) narrowing VPMOV* stores (E6): masked-off elements on the unmapped page are not written")
    for nm, x, kok, kbad in (("VPMOVDB", 8, 0x00FF, 0x01FF), ("VPMOVQW", 4, 0x03, 0x83), ("VPMOVSDW", 16, 0x00FF, 0x80FF),
                             ("VPMOVUSQD", 12, 0x07, 0x0F)):
        f = [g for g in NARROW if g[0] == nm][0]
        name, opc, ssz, dsz, kind = f
        rm, disp = pf_mem(x)
        s = regs(1)[0]
        c = narrow_case(f, 64, s, 0, mode="mem", kreg=3, kval=kok, disp=disp, title="store, %d bytes mapped, k=0x%X" % (x, kok))
        # the expectation of narrow_case covers 64/ssz elements; keep only the mapped bytes
        c.exp = [e for e in c.exp if not e.startswith("m+")]
        old = c.mem.pop(MEM_RSI + disp)
        c.mem[MEM_RSI + disp] = old[:x]
        res = [narrow(v, ssz, dsz, kind) for v in elems(c.zmm[s][:64], ssz)]
        oe = elems(old[:x], dsz)
        new = [res[j] if (kok >> j) & 1 else oe[j] for j in range(x // dsz)]
        exp_mem(c, disp, pack(new, dsz))
        c.code = evex(2, 2, 0, opc, s, rm, ll=2, aaa=3)
        emit(c)
        c2 = Case("%s VL512 store, element on the unmapped page active k=0x%X -> #PF, memory unchanged" % (nm, kbad))
        c2.set_zmm(s, c.zmm[s])
        c2.k[3] = kbad
        c2.mem[MEM_RSI + disp] = old[:x]
        c2.code = evex(2, 2, 0, opc, s, rm, ll=2, aaa=3)
        c2.fault = "#PF"
        emit(c2)
    c = Case("VPMOVQB VL512 store k=0 entirely on the unmapped page: no access")
    c.set_zmm(9, rb(64))
    c.k[5] = 0
    c.code = evex(2, 2, 0, 0x32, 9, Mem(RSI, PAGE_END, disp32=True), ll=2, aaa=5)
    emit(c)

    comment("(b) VPCOMPRESS/VCOMPRESS stores: only popcount(k) contiguous elements are accessed")
    for f, x, kok, kbad in ((COMPRESS[0], 16, 0x8421, 0x8423), (COMPRESS[1], 24, 0xA1, 0xA3),
                            (COMPRESS[2], 8, 0x0101, 0x0103), (COMPRESS[3], 8, 0x80, 0x81)):
        name, opc, w = f
        esz = 8 if w else 4
        rm, disp = pf_mem(x)
        for kv, bad in ((kok, False), (kbad, True)):
            s = regs(1)[0]
            c = Case("%s VL512 mem, %d bytes before the page, popcount(k=0x%X)=%d %s" %
                     (name, x, kv, bin(kv).count("1"), "-> #PF, memory unchanged" if bad else "fits"))
            img = rb(64)
            c.set_zmm(s, img)
            c.k[4] = kv
            old = rb(x)
            set_mem(c, disp, old)
            sv = elems(img, esz)
            act = [sv[j] for j in range(64 // esz) if (kv >> j) & 1]
            c.code = evex(2, 1, w, opc, s, rm, ll=2, aaa=4)
            if bad:
                c.fault = "#PF"
            else:
                exp_mem(c, disp, (pack(act, esz) + old[len(act) * esz:])[:x])
            emit(c)
    c = Case("VPCOMPRESSD VL512 mem k=0 on the unmapped page: no access")
    c.set_zmm(2, rb(64))
    c.k[1] = 0
    c.code = evex(2, 1, 0, 0x8B, 2, Mem(RSI, PAGE_END, disp32=True), ll=2, aaa=1)
    emit(c)

    comment("(c) VPEXPAND/VEXPAND loads: only popcount(k) contiguous elements are read")
    for f, x, kok, kbad in ((EXPAND[0], 16, 0xF000, 0xF800), (EXPAND[3], 24, 0x15, 0x55),
                            (EXPAND[2], 4, 0x8000, 0x8001), (EXPAND[1], 8, 0x02, 0x82)):
        name, opc, w = f
        esz = 8 if w else 4
        rm, disp = pf_mem(x)
        for kv, bad in ((kok, False), (kbad, True)):
            d = regs(1)[0]
            c = Case("%s VL512 mem, %d bytes before the page, popcount(k=0x%X)=%d %s" %
                     (name, x, kv, bin(kv).count("1"), "-> #PF" if bad else "fits"))
            dimg = rb(64)
            c.set_zmm(d, dimg)
            c.k[6] = kv
            data = rb(x)
            set_mem(c, disp, data)
            sv = elems(data, esz)
            c.code = evex(2, 1, w, opc, d, rm, ll=2, z=1, aaa=6)
            if bad:
                c.fault = "#PF"
            else:
                out, k = [], 0
                for j in range(64 // esz):
                    if (kv >> j) & 1:
                        out.append(sv[k])
                        k += 1
                    else:
                        out.append(0)
                exp_zmm(c, d, pack(out, esz))
            emit(c)

    comment("(d) VPMOVZX/VPMOVSX loads (E5): masked-off elements past the boundary are not read")
    for nm, x, kok, kbad in (("VPMOVZXBD", 8, 0x00FF, 0x01FF), ("VPMOVSXWQ", 6, 0x07, 0x0F),
                             ("VPMOVSXDQ", 16, 0x0F, 0x1F), ("VPMOVZXBQ", 1, 0x01, 0x03), ("VPMOVZXWD", 30, 0x7FFF, 0xFFFF)):
        f = [g for g in EXTF if g[0] == nm][0]
        name, opc, ssz, dsz, signed, wfix = f
        rm, disp = pf_mem(x)
        for kv, bad in ((kok, False), (kbad, True)):
            d = regs(1)[0]
            c = Case("%s VL512 mem, %d bytes before the page, k=0x%X %s" % (name, x, kv, "-> #PF" if bad else "suppressed"))
            dimg = rb(64)
            c.set_zmm(d, dimg)
            c.k[2] = kv
            data = rb(x)
            set_mem(c, disp, data)
            sv = elems(data, ssz)
            res = []
            for j in range(64 // dsz):
                if (kv >> j) & 1 and j < len(sv):
                    v = sv[j]
                    res.append(u(sgn(v, ssz), dsz) if signed else v)
                else:
                    res.append(None)
            dv = elems(dimg, dsz)
            out = [dv[j] if r is None else r for j, r in enumerate(res)]      # merging
            c.code = evex(2, 1, 0, opc, d, rm, ll=2, aaa=2)
            if bad:
                c.fault = "#PF"
            else:
                exp_zmm(c, d, pack(out, dsz))
            emit(c)

    comment("(e) VBROADCASTx (E6): tuple element g is read only if a lane j with j mod size == g is active")
    for f, x, kok, kbad in ((BC[2], 8, 0x3333, 0x3337), (BC[5], 16, 0x33, 0x37), (BC[1], 4, 0x5555, 0x5557),
                            (BC[4], 20, 0x1F1F, 0x3F1F), (BC[7], 8, 0x55, 0x57), (BC[0], 4, 0x5555, 0x7555)):
        name, opc, w, esz, tb, vls, regok, tup = f
        rm, disp = pf_mem(x)
        gs = tb // esz
        for kv, bad in ((kok, False), (kbad, True)):
            d = regs(1)[0]
            c = Case("%s VL512 m%d with %d bytes mapped, k=0x%X %s" % (name, tb * 8, x, kv, "-> #PF" if bad else "suppressed"))
            dimg = rb(64)
            c.set_zmm(d, dimg)
            c.k[7] = kv
            data = rb(x)
            set_mem(c, disp, data)
            g = elems(data + bytes(tb - x), esz)
            res = [g[j % gs] for j in range(64 // esz)]
            out = mask_merge(dimg, pack(res, esz), esz, 64, kv, 1)
            c.code = evex(2, 1, w, opc, d, rm, ll=2, z=1, aaa=7)
            if bad:
                c.fault = "#PF"
            else:
                exp_zmm(c, d, out)
            emit(c)

    comment("(f) no fault suppression: E4NF permutes, E5NF VMOVDDUP, E4NF.nb VMOVSHDUP, E6NF VINSERT/VEXTRACT")
    for name, x, kv in (("VPERMD", 32, 0x00FF), ("VPSHUFD", 48, 0x0FFF), ("VSHUFPS", 48, 0x0FFF),
                        ("VPUNPCKLQDQ", 56, 0x7F), ("VPERMI2Q", 8, 0x01), ("VALIGND", 60, 0x0001),
                        ("VPSHUFD", 16, 0x0000), ("VMOVDDUP", 32, 0x0F), ("VMOVSHDUP", 32, 0x00FF)):
        sp = ff(name)
        rm, disp = pf_mem(x)
        d, s1 = regs(2)
        c = Case("%s VL512 mem crossing into the unmapped page (%d bytes mapped), lanes using it masked k=0x%X -> #PF (%s)"
                 % (name, x, kv, sp.exc))
        c.set_zmm(d, rb(64))
        if sp.vvvv:
            c.set_zmm(s1, rb(64))
        c.k[1] = kv
        set_mem(c, disp, rb(x))
        c.code = evex(sp.mmm, sp.pp, sp.w, sp.opc, d, rm, vvvv=s1 if sp.vvvv else None, ll=2, aaa=1,
                      imm=0xE4 if sp.imm else None)
        c.fault = "#PF"
        emit(c)
    for f, x, imm, kv in ((INS[0], 8, 0, 0xFFFC), (INS[3], 16, 1, 0x0F)):
        name, opc, w, esz, ch, vls, tup = f
        rm, disp = pf_mem(x)
        d, s1 = regs(2)
        c = Case("%s VL512 m%d crossing into the unmapped page, inserted lanes using it masked k=0x%X -> #PF (E6NF)"
                 % (name, ch * 8, kv))
        c.set_zmm(d, rb(64))
        c.set_zmm(s1, rb(64))
        c.k[2] = kv
        set_mem(c, disp, rb(x))
        c.code = evex(3, 1, w, opc, d, rm, vvvv=s1, ll=2, aaa=2, imm=imm)
        c.fault = "#PF"
        emit(c)
    f = EXTR[0]
    rm, disp = pf_mem(8)
    c = Case("VEXTRACTF32X4 VL512 m128 store crossing the page, only mapped dwords active k=0x3 -> #PF (E6NF), memory unchanged")
    c.set_zmm(4, rb(64))
    c.k[3] = 0x3
    set_mem(c, disp, rb(8))
    c.code = evex(3, 1, 0, 0x19, 4, rm, ll=2, aaa=3, imm=2)
    c.fault = "#PF"
    emit(c)
    comment("E1NF: VMOVNT* is never fault-suppressed")
    c = Case("VMOVNTDQA VL512 at MEM+0x10000 (aligned, unmapped) -> #PF")
    c.set_zmm(1, rb(64))
    c.code = evex(2, 1, 0, 0x2A, 1, Mem(RSI, PAGE_END, disp32=True), ll=2)
    c.fault = "#PF"
    emit(c)
    c = Case("VMOVNTPS VL256 at MEM+0x10000-32 (fully mapped, aligned): stored")
    img = rb(64)
    c.set_zmm(1, img)
    c.code = evex(1, 0, 0, 0x2B, 1, Mem(RSI, PAGE_END - 32, disp32=True), ll=1)
    exp_mem(c, PAGE_END - 32, img[:32])
    emit(c)


# ---------------------------------------------------------------------------------------
# self test (hand-derived values from the SDM pseudocode / figures)
# ---------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, want):
        nonlocal ok
        if got != want:
            print("FAIL %s: got %r want %r" % (name, got, want))
            ok = False

    # encodings (SDM opcode tables; byte sequences as produced by an assembler)
    chk("enc vpermd zmm1,zmm2,zmm3", evex(2, 1, 0, 0x36, 1, 3, vvvv=2, ll=2), bytes.fromhex("62F26D4836CB"))
    chk("enc vpshufd zmm0,zmm1,1Bh", evex(1, 1, 0, 0x70, 0, 1, ll=2, imm=0x1B), bytes.fromhex("62F17D4870C11B"))
    chk("enc valignd zmm0,zmm1,zmm2,3", evex(3, 1, 0, 0x03, 0, 2, vvvv=1, ll=2, imm=3), bytes.fromhex("62F3754803C203"))
    chk("enc vpmovdb xmm1,zmm2", evex(2, 2, 0, 0x31, 2, 1, ll=2), bytes.fromhex("62F27E4831D1"))
    chk("enc vmovq xmm1,rax", evex(1, 1, 1, 0x6E, 1, 0), bytes.fromhex("62F1FD086EC8"))
    chk("enc vbroadcasti32x4 zmm1,[rsi+10h]", evex(2, 1, 0, 0x5A, 1, Mem(RSI, 0x10), ll=2, n=16),
        bytes.fromhex("62F27D485A4E01"))
    chk("enc vextractf32x4 xmm1,zmm2,1", evex(3, 1, 0, 0x19, 2, 1, ll=2, imm=1), bytes.fromhex("62F37D4819D101"))
    chk("enc vinsertf64x4 zmm1,zmm2,ymm3,1", evex(3, 1, 1, 0x1A, 1, 3, vvvv=2, ll=2, imm=1),
        bytes.fromhex("62F3ED481ACB01"))
    chk("enc vpcompressd [rsi+8]{k1},zmm2 (N=4)", evex(2, 1, 0, 0x8B, 2, Mem(RSI, 8), ll=2, aaa=1, n=4),
        bytes.fromhex("62F27D498B5602"))
    chk("enc vpermi2d zmm16,zmm17,zmm18", evex(2, 1, 0, 0x76, 16, 18, vvvv=17, ll=2),
        bytes.fromhex("62A2754076C2"))
    # VPSHUFD imm 0x1B reverses the dwords of every 128-bit lane
    a = list(range(16))
    chk("vpshufd 1Bh", f_sel4(64, 4, None, a, None, 0x1B), [3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12])
    chk("vpshufd E4h identity", f_sel4(64, 4, None, a, None, 0xE4), a)
    # VPUNPCKLDQ / HDQ / LQDQ interleave (Figure: X0 Y0 X1 Y1 per lane)
    x = [0x10 + i for i in range(8)]
    y = [0x20 + i for i in range(8)]
    chk("vpunpckldq", f_unpcklo(32, 4, x, y, None, 0), [0x10, 0x20, 0x11, 0x21, 0x14, 0x24, 0x15, 0x25])
    chk("vpunpckhdq", f_unpckhi(32, 4, x, y, None, 0), [0x12, 0x22, 0x13, 0x23, 0x16, 0x26, 0x17, 0x27])
    chk("vpunpcklqdq", f_unpcklo(32, 8, x[:4], y[:4], None, 0), [0x10, 0x20, 0x12, 0x22])
    # VALIGND: SRC1 = 16..31 (high), SRC2 = 0..15 (low), imm 3 -> 3..18; imm 0x13 -> SHIFT = imm8[3:0]
    hi, lo = list(range(16, 32)), list(range(16))
    chk("valignd 3", f_valign(64, 4, hi, lo, None, 3), list(range(3, 19)))
    chk("valignd 13h", f_valign(64, 4, hi, lo, None, 0x13), list(range(3, 19)))
    chk("valignq 128 imm 1", f_valign(16, 8, [7, 8], [5, 6], None, 1), [6, 7])
    # VPERMI2D (VL512): index bit 4 selects SRC2, bits 3:0 the element
    t1, t2 = [0x100 + i for i in range(16)], [0x200 + i for i in range(16)]
    idx = [5, 0x15, 0x1F, 0x0F, 0xFFFFFFE0] + [0] * 11
    chk("vpermi2d", f_permi2(64, 4, t1, t2, idx, 0)[:5], [0x105, 0x205, 0x20F, 0x10F, 0x100])
    chk("vpermt2d", f_permt2(64, 4, idx + [], t2, t1, 0)[:5], [0x105, 0x205, 0x20F, 0x10F, 0x100])
    chk("vpermd", f_perm(32, 4, [7, 0, 9, 1, 2, 3, 4, 5], list(range(40, 48)), None, 0)[:4], [47, 40, 41, 41])
    chk("vshuff32x4 512 imm 4Eh", f_shuf128(64, 4, list(range(16)), list(range(16, 32)), None, 0x4E),
        [8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23])
    chk("vshufpd imm 1", f_shufpd(16, 8, [1, 2], [3, 4], None, 1), [2, 3])
    chk("vshufps imm 1Bh", f_shufps(16, 4, [0, 1, 2, 3], [4, 5, 6, 7], None, 0x1B), [3, 2, 5, 4])
    chk("vpermilpd var", f_permilpd_var(16, 8, [1, 2], [2, 0], None, 0), [2, 1])
    # saturation
    chk("vpmovsdb 80000000", narrow(0x80000000, 4, 1, "s"), 0x80)
    chk("vpmovsdb 7FFFFFFF", narrow(0x7FFFFFFF, 4, 1, "s"), 0x7F)
    chk("vpmovsdb FFFFFF80", narrow(0xFFFFFF80, 4, 1, "s"), 0x80)
    chk("vpmovsdb FFFFFF7F", narrow(0xFFFFFF7F, 4, 1, "s"), 0x80)
    chk("vpmovsdb 5A", narrow(0x5A, 4, 1, "s"), 0x5A)
    chk("vpmovusdb FFFFFFFF", narrow(0xFFFFFFFF, 4, 1, "u"), 0xFF)
    chk("vpmovusdb 80000000", narrow(0x80000000, 4, 1, "u"), 0xFF)
    chk("vpmovusdb FF", narrow(0xFF, 4, 1, "u"), 0xFF)
    chk("vpmovusdw 10000", narrow(0x10000, 4, 2, "u"), 0xFFFF)
    chk("vpmovdb 12345678", narrow(0x12345678, 4, 1, "t"), 0x78)
    chk("vpmovsqd -1", narrow((1 << 64) - 1, 8, 4, "s"), 0xFFFFFFFF)
    chk("vpmovsqd 2^31", narrow(1 << 31, 8, 4, "s"), 0x7FFFFFFF)
    # compress / expand with k = 1010b (VL128 dwords)
    sv, kv = [0xA0, 0xA1, 0xA2, 0xA3], 0b1010
    act = [sv[j] for j in range(4) if (kv >> j) & 1]
    chk("compress k=1010b", act, [0xA1, 0xA3])
    out, k = [], 0
    mem = [0xB0, 0xB1, 0xB2, 0xB3]
    for j in range(4):
        if (kv >> j) & 1:
            out.append(mem[k])
            k += 1
        else:
            out.append(0xC0 + j)
    chk("expand k=1010b merge", out, [0xC0, 0xB0, 0xC2, 0xB1])
    # VINSERTPS imm: COUNT_S=3, COUNT_D=1, ZMASK=0001b
    chk("vinsertps", insertps([1, 2, 3, 4], [5, 6, 7, 8], 0xD1, True), [0, 8, 3, 4])
    chk("vinsertps m32", insertps([1, 2, 3, 4], [9], 0xD0, False), [1, 9, 3, 4])
    # broadcast tuple index j mod size
    chk("vbroadcasti32x2", [[1, 2][j % 2] for j in range(8)], [1, 2, 1, 2, 1, 2, 1, 2])
    return ok


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv or "--stats" in sys.argv:
        gen_full(FULL, "Full-tuple permutes / shuffles / unpacks (E4NF, {1toN}, Full: N = VL or element size)")
        gen_full(DUP, "VMOVDDUP (E5NF, MOVDDUP tuple), VMOVSHDUP/VMOVSLDUP (E4NF.nb, Full Mem)")
        gen_ext()
        gen_narrow()
        gen_compress_expand()
        gen_nt()
        gen_movdq()
        gen_hl()
        gen_insext_scalar()
        gen_insext_vec()
        gen_bcast()
        gen_disp8()
        gen_faults()
        if "--stats" in sys.argv:
            for name, n in SECTIONS:
                print("%5d  %s" % (n, name))
            print("%5d  total" % sum(n for _, n in SECTIONS))
            return
        out = sys.stdout
        out.write("# EVEX milestone M2 permutes/moves (ledger U210-U229): expected values from the independent SDM model\n")
        out.write("# Emulator/tools/isa/ref_evex_m2_perm.py --cases (regenerate, do not edit). The i5-13600K has no\n")
        out.write("# AVX-512: expected-value cases only, run with AVX-512 enabled:\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m2_perm.txt --avx512 --xcr0 0xE7 --expect-only\n")
        out.write("# RSI = R14 = MEM + 0x8000; MEM + 0x10000 is unmapped (#PF / fault suppression cases).\n")
        out.write("# Modelled CPU: AVX512F/DQ/BW/VL, no AVX10.2 (EVEX.F3.0F.W0 7E and EVEX.66.0F.W0 D6 are #UD).\n")
        for c in OUT:
            if isinstance(c, str):
                out.write(c + "\n")
            else:
                out.write("# " + c.title + "\n")
                out.write(c.line() + "\n")
        return
    print(__doc__)


if __name__ == "__main__":
    main()
