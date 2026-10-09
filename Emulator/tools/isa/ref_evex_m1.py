#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_evex_m1.py -- independent reference model (Python 3 stdlib only) of the EVEX
milestone-M1 instructions (ledger U141-U153) and generator of the expected-value case
file Emulator/data/cases_evex_m1.txt.

Written from the Intel SDM text only (Vol2A 2.7 "Intel AVX-512 encoding", Tables 2-32 ..
2-46, the instruction pages' "Operation" pseudocode, Vol1 4.8/4.9/10.2/11.5 for the
floating-point rules), not from any C implementation:

  VMOVUPS/VMOVUPD (0F 10/11), VMOVAPS/VMOVAPD (0F 28/29, E1), VMOVDQA32/64 (66 0F 6F/7F,
  E1), VMOVDQU32/64 (F3 0F 6F/7F), VPADDD/Q (FE/D4), VPSUBD/Q (FA/FB), VPANDD/Q (DB),
  VPANDND/Q (DF), VPORD/Q (EB), VPXORD/Q (EF), VADDPS/PD (58), VMULPS/PD (59),
  VSUBPS/PD (5C) incl. {er}, VPCMPEQD (0F 76), VPCMPEQQ (0F38 29), VPCMPD/Q/UD/UQ
  (0F3A 1F/1E), VBROADCASTSS/SD (0F38 18/19), VPBROADCASTD/Q xmm/m (0F38 58/59) and r32/r64
  (0F38 7C).

Generic EVEX wrappers (SDM pseudocode of every page):
  MASK     FOR j := 0 TO KL-1: IF k1[j] OR *no writemask* THEN DEST[j] := op ELSE
           (*merging*: unchanged | *zeroing*: 0); DEST[MAXVL-1:VL] := 0
  BCST     IF (EVEX.b = 1) AND (SRC2 *is memory*) THEN SRC2[j] := SRC2[0] (one element)
  K-DEST   DEST[j] := k2[j] ? cmp : 0; DEST[MAX_KL-1:KL] := 0
  STORE    memory elements with k1[j] = 0 are not written (and never fault)
  LOAD     masked-off elements are not read (fault suppression, classes E1/E2/E4/E6)
  E1       #GP(0) when the memory operand is not VL-aligned, whatever the mask
  {er}     reg-reg only: L'L = rounding control (00 RNE, 01 RD, 10 RU, 11 RZ), VL = 512,
           SAE: all exceptions masked and no MXCSR flag is set
Floating point (exact rationals): IEEE binary32/64 with the four rounding modes, DAZ,
FTZ (tiny, exact or not -> signed 0, UE+PE), flags IE DE ZE OE UE PE (MXCSR bits 0-5),
underflow = tiny after rounding with unbounded exponent (Vol1 4.9.1.5), NaN rules of
Vol1 Table 4-7 (SRC1 = vvvv first, SNaN quietened, invalid -> QNaN indefinite),
unmasked exception -> #XM, destination unchanged, flags of the whole instruction set.

Usage:
  python ref_evex_m1.py --selftest   hand-derived checks of the model, exit 0 on pass
  python ref_evex_m1.py --cases      Emulator/data/cases_evex_m1.txt (stdout)
"""

import random
import sys
from fractions import Fraction

# ---------------------------------------------------------------------------------------
# harness layout (Emulator/tests/alltest/at_engine.hpp): RSI = R14 = MEM + 0x8000,
# RDI = MEM + 0x9000, operand memory MEM .. MEM + 0xFFFF mapped, MEM + 0x10000 unmapped
# ---------------------------------------------------------------------------------------
MEM_RSI = 0x8000
RSI, RDI, R14 = 6, 7, 14

IE, DE, ZE, OE, UE, PE = 1, 2, 4, 8, 16, 32
MXCSR_DEFAULT = 0x1F80
DAZ, FTZ = 0x40, 0x8000


# ---------------------------------------------------------------------------------------
# EVEX encoding (SDM Vol2A 2.7.1, Table 2-32; Tables 2-36/2-37 for disp8*N)
# ---------------------------------------------------------------------------------------
class Mem:
    def __init__(self, base=RSI, disp=0, index=None, scale=0, disp32=False):
        self.base, self.disp, self.index, self.scale, self.disp32 = base, disp, index, scale, disp32


def evex(mmm, pp, w, opc, reg, rm, vvvv=None, ll=0, b=0, z=0, aaa=0, imm=None, n=1,
         p0_or=0, p1_and=0xFF, p2_vp=None, prefixes=b""):
    """bytes of one EVEX instruction. reg/vvvv: 0-31 (vvvv None = unused: 1111b, V' = 1);
    rm: register number 0-31 or Mem. n = disp8*N scale of the form."""
    r, rr = (reg >> 3) & 1, (reg >> 4) & 1
    x = bb = 0
    modrm_rm, tail = 0, b""
    if isinstance(rm, Mem):
        m = rm
        need_sib = m.index is not None or (m.base & 7) == 4
        bb = (m.base >> 3) & 1
        if m.index is not None:
            x = (m.index >> 3) & 1
        disp = m.disp
        if not m.disp32 and disp % n == 0 and -128 <= disp // n <= 127 and not (disp == 0 and (m.base & 7) != 5):
            mod, dbytes = 1, bytes([(disp // n) & 0xFF])
        elif disp == 0 and (m.base & 7) != 5 and not m.disp32:
            mod, dbytes = 0, b""
        else:
            mod, dbytes = 2, (disp & 0xFFFFFFFF).to_bytes(4, "little")
        if need_sib:
            idx = 4 if m.index is None else m.index & 7
            sib = (m.scale << 6) | (idx << 3) | (m.base & 7)
            modrm = (mod << 6) | ((reg & 7) << 3) | 4
            tail = bytes([modrm, sib]) + dbytes
        else:
            modrm = (mod << 6) | ((reg & 7) << 3) | (m.base & 7)
            tail = bytes([modrm]) + dbytes
    else:
        bb, x = (rm >> 3) & 1, (rm >> 4) & 1
        tail = bytes([0xC0 | ((reg & 7) << 3) | (rm & 7)])
    v = 0 if vvvv is None else vvvv
    vp = (v >> 4) & 1
    p0 = ((r ^ 1) << 7) | ((x ^ 1) << 6) | ((bb ^ 1) << 5) | ((rr ^ 1) << 4) | mmm
    p0 |= p0_or
    p1 = (w << 7) | (((~v) & 0xF) << 3) | 4 | pp
    if vvvv is None:
        p1 = (w << 7) | (0xF << 3) | 4 | pp
    p1 &= p1_and
    vbit = (vp ^ 1) if p2_vp is None else p2_vp
    p2 = (z << 7) | (ll << 5) | (b << 4) | (vbit << 3) | aaa
    out = prefixes + bytes([0x62, p0, p1, p2, opc]) + tail
    if imm is not None:
        out += bytes([imm & 0xFF])
    return out


def byte_list(bs):
    return ".byte " + ", ".join("0x%02x" % c for c in bs)


# ---------------------------------------------------------------------------------------
# vector helpers: a register / memory image is a bytes object, little endian
# ---------------------------------------------------------------------------------------
def elems(buf, esz):
    n = len(buf) // esz
    return [int.from_bytes(buf[i * esz:(i + 1) * esz], "little") for i in range(n)]


def pack(vals, esz):
    return b"".join((v & ((1 << (8 * esz)) - 1)).to_bytes(esz, "little") for v in vals)


def hexs(buf):
    return "".join("%02X" % c for c in buf)


def mask_merge(old, res, esz, vl, kmask, zero):
    """MASK wrapper + DEST[MAXVL-1:VL] := 0 (old: 64-byte register, res: vl-byte result)."""
    o, r = elems(old[:vl], esz), elems(res, esz)
    out = []
    for j in range(vl // esz):
        if kmask is None or (kmask >> j) & 1:
            out.append(r[j])
        else:
            out.append(0 if zero else o[j])
    return pack(out, esz) + bytes(64 - vl)


# ---------------------------------------------------------------------------------------
# IEEE binary32/64 with x86 MXCSR semantics (Vol1 4.8, 4.9, 10.2.3, 11.5)
# ---------------------------------------------------------------------------------------
class Fmt:
    def __init__(self, bits, p, ebits):
        self.bits, self.p, self.ebits = bits, p, ebits
        self.bias = (1 << (ebits - 1)) - 1
        self.emin = 1 - self.bias
        self.emax = self.bias
        self.fbits = p - 1
        self.qnan_indef = (1 << (bits - 1)) | (((1 << ebits) - 1) << self.fbits) | (1 << (self.fbits - 1))


F32 = Fmt(32, 24, 8)
F64 = Fmt(64, 53, 11)


def classify(x, f):
    sign = x >> (f.bits - 1)
    e = (x >> f.fbits) & ((1 << f.ebits) - 1)
    m = x & ((1 << f.fbits) - 1)
    if e == (1 << f.ebits) - 1:
        if m == 0:
            return "inf", sign, None
        return ("qnan" if m >> (f.fbits - 1) else "snan"), sign, None
    if e == 0:
        if m == 0:
            return "zero", sign, Fraction(0)
        v = Fraction(m, 1 << f.fbits) * Fraction(2) ** f.emin
        return "denorm", sign, -v if sign else v
    v = Fraction((1 << f.fbits) | m, 1 << f.fbits) * Fraction(2) ** (e - f.bias)
    return "normal", sign, -v if sign else v


def quiet(x, f):
    return x | (1 << (f.fbits - 1))


def floor_log2(q):
    """largest e with 2^e <= q (q > 0 Fraction)"""
    n, d = q.numerator, q.denominator
    e = n.bit_length() - d.bit_length()
    if Fraction(2) ** e > q:
        e -= 1
    if Fraction(2) ** (e + 1) <= q:
        e += 1
    return e


def round_frac(q, quantum, rc, negative):
    """round the non-negative magnitude q to a multiple of quantum; returns (integer
    count of quanta, inexact). rc: 0 RNE, 1 RD, 2 RU, 3 RZ; negative = sign of the value"""
    t = q / quantum
    fl = t.numerator // t.denominator
    rem = t - fl
    if rem == 0:
        return fl, False
    if rc == 0:
        if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl & 1):
            fl += 1
    elif rc == 1:       # toward -inf: magnitude up for negatives
        if negative:
            fl += 1
    elif rc == 2:       # toward +inf
        if not negative:
            fl += 1
    return fl, True


def encode_value(v, f, rc, ftz):
    """round the exact non-zero rational v to format f: (bits, flags)"""
    neg = v < 0
    q = -v if neg else v
    sign = (1 << (f.bits - 1)) if neg else 0
    flags = 0
    e = floor_log2(q)
    # rounding with unbounded exponent (tininess after rounding, Vol1 4.9.1.5)
    cnt_u, inexact_u = round_frac(q, Fraction(2) ** (e - f.fbits), rc, neg)
    ru = cnt_u * Fraction(2) ** (e - f.fbits)
    tiny = ru < Fraction(2) ** f.emin
    if tiny and ftz:
        # FTZ (Vol1 10.2.3.3): an underflow condition (a tiny result, exact or not) returns
        # a zero with the sign of the true result and sets UE and PE (the i5-13600K does
        # the same for SSE ADDPS with an exact denormal result)
        return sign, UE | PE
    if e < f.emin:
        cnt, inexact = round_frac(q, Fraction(2) ** (f.emin - f.fbits), rc, neg)
        if tiny and inexact:
            flags |= UE | PE
        elif inexact:
            flags |= PE
        if cnt >= (1 << f.fbits):            # rounded up to the smallest normal
            return sign | (1 << f.fbits) | (cnt - (1 << f.fbits)), flags
        return sign | cnt, flags
    cnt, inexact = round_frac(q, Fraction(2) ** (e - f.fbits), rc, neg)
    if cnt == (1 << (f.fbits + 1)):
        cnt >>= 1
        e += 1
    if inexact:
        flags |= PE
    if e > f.emax:
        flags |= OE | PE
        inf = sign | (((1 << f.ebits) - 1) << f.fbits)
        maxf = sign | ((((1 << f.ebits) - 2) << f.fbits) | ((1 << f.fbits) - 1))
        if rc == 0:
            return inf, flags
        if rc == 3:
            return maxf, flags
        if rc == 1:
            return (inf if neg else maxf), flags
        return (maxf if neg else inf), flags
    return sign | ((e + f.bias) << f.fbits) | (cnt - (1 << f.fbits)), flags


def fp_binop(op, a, b, f, mxcsr, rc=None):
    """op in add/sub/mul; a = SRC1, b = SRC2 (bit patterns). Returns (bits, flags)."""
    if rc is None:
        rc = (mxcsr >> 13) & 3
    daz, ftz = bool(mxcsr & DAZ), bool(mxcsr & FTZ)
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    flags = 0
    if ca == "snan" or cb == "snan" or ca == "qnan" or cb == "qnan":
        if ca == "snan" or cb == "snan":
            flags |= IE
        if ca in ("snan", "qnan"):
            return quiet(a, f), flags
        return quiet(b, f), flags
    if daz:
        if ca == "denorm":
            ca, va = "zero", Fraction(0)
        if cb == "denorm":
            cb, vb = "zero", Fraction(0)
    elif ca == "denorm" or cb == "denorm":
        flags |= DE
    if op == "sub":
        sb ^= 1
        if vb is not None:
            vb = -vb
    signbit = 1 << (f.bits - 1)
    inf_bits = ((1 << f.ebits) - 1) << f.fbits
    if op in ("add", "sub"):
        if ca == "inf" and cb == "inf":
            if sa != sb:
                return f.qnan_indef, flags | IE
            return (signbit if sa else 0) | inf_bits, flags
        if ca == "inf":
            return (signbit if sa else 0) | inf_bits, flags
        if cb == "inf":
            return (signbit if sb else 0) | inf_bits, flags
        s = va + vb
        if s == 0:
            if ca == "zero" and cb == "zero" and sa == sb:
                return (signbit if sa else 0), flags
            return (signbit if rc == 1 else 0), flags
        r, fl = encode_value(s, f, rc, ftz)
        return r, flags | fl
    # mul
    sgn = sa ^ sb
    if ca == "inf" or cb == "inf":
        if ca == "zero" or cb == "zero":
            return f.qnan_indef, flags | IE
        return (signbit if sgn else 0) | inf_bits, flags
    p = va * vb
    if p == 0:
        return (signbit if sgn else 0), flags
    r, fl = encode_value(p, f, rc, ftz)
    return r, flags | fl


def fp_div(a, b, f, mxcsr, rc=None):
    """DIVPS/DIVPD element: SRC1 / SRC2 (Vol1 4.9.1.3 #Z, Table 4-7 NaNs)."""
    if rc is None:
        rc = (mxcsr >> 13) & 3
    daz, ftz = bool(mxcsr & DAZ), bool(mxcsr & FTZ)
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    flags = 0
    if ca in ("snan", "qnan") or cb in ("snan", "qnan"):
        if ca == "snan" or cb == "snan":
            flags |= IE
        return (quiet(a, f) if ca in ("snan", "qnan") else quiet(b, f)), flags
    if daz:
        if ca == "denorm":
            ca, va = "zero", Fraction(0)
        if cb == "denorm":
            cb, vb = "zero", Fraction(0)
    elif (ca == "denorm" or cb == "denorm") and not (ca == "denorm" and cb == "zero"):
        # divide-by-zero (priority 3) is above the denormal operand (priority 4, Vol1 4.9.2):
        # denormal / 0 sets ZE only (i5-13600K DIVPS/DIVPD hardware cases)
        flags |= DE
    signbit = 1 << (f.bits - 1)
    inf_bits = ((1 << f.ebits) - 1) << f.fbits
    sgn = signbit if sa ^ sb else 0
    if ca == "inf" and cb == "inf" or ca == "zero" and cb == "zero":
        return f.qnan_indef, flags | IE
    if ca == "inf":
        return sgn | inf_bits, flags
    if cb == "inf" or ca == "zero":
        return sgn, flags
    if cb == "zero":
        return sgn | inf_bits, flags | ZE
    r, fl = encode_value(va / vb, f, rc, ftz)
    return r, flags | fl


def isqrt(n):
    if n < 2:
        return n
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def fp_sqrt(a, f, mxcsr, rc=None):
    """SQRTPS/SQRTPD element (Vol1 4.8.3.7: sqrt(-0) = -0, negative -> #IA indefinite)."""
    if rc is None:
        rc = (mxcsr >> 13) & 3
    daz = bool(mxcsr & DAZ)
    ca, sa, va = classify(a, f)
    signbit = 1 << (f.bits - 1)
    if ca in ("snan", "qnan"):
        return quiet(a, f), (IE if ca == "snan" else 0)
    flags = 0
    if ca == "denorm":
        if daz:
            return (signbit if sa else 0), 0
        flags |= DE
    if ca == "zero":
        return a, flags
    if sa:
        # invalid takes precedence over the denormal operand (i5-13600K: SQRTPD of a
        # negative denormal sets IE only)
        return f.qnan_indef, IE
    if ca == "inf":
        return a, flags
    # va = n / d with d a power of two; sqrt rounded to p bits (never tiny, never overflows)
    e = floor_log2(va)
    E = e // 2                                    # 2^E <= sqrt(va) < 2^(E+1)
    F = 2 * (f.p + 8) - 2 * E                     # even: sqrt(va * 2^F) = sqrt(va) * 2^(F/2)
    yf = va * Fraction(2) ** F
    assert yf.denominator == 1
    Y = yf.numerator
    s = isqrt(Y)
    inexact_sqrt = s * s != Y
    sh = F // 2 + E - f.fbits                      # sqrt(Y) / 2^sh = significand count
    c = s >> sh
    r = s - (c << sh)
    half = 1 << (sh - 1)
    if rc == 0:
        up = r > half or (r == half and (inexact_sqrt or c & 1))
    elif rc == 2:
        up = r > 0 or inexact_sqrt
    else:
        up = False
    inexact = r > 0 or inexact_sqrt
    if up:
        c += 1
    if c == 1 << (f.fbits + 1):
        c >>= 1
        E += 1
    return ((E + f.bias) << f.fbits) | (c - (1 << f.fbits)), (flags | PE) if inexact else flags


def fp_minmax(op, a, b, f, mxcsr):
    """MINPS/MAXPS element (SDM pseudocode): (0, 0) or a NaN -> SRC2; IE for any NaN."""
    daz = bool(mxcsr & DAZ)
    ca, sa, va = classify(a, f)
    cb, sb, vb = classify(b, f)
    flags = 0
    # DAZ first: with a NaN SRC1, a denormal SRC2 is returned as a signed zero (i5-13600K)
    if daz:
        if ca == "denorm":
            a, ca, va = (a & (1 << (f.bits - 1))), "zero", Fraction(0)
        if cb == "denorm":
            b, cb, vb = (b & (1 << (f.bits - 1))), "zero", Fraction(0)
    if ca in ("snan", "qnan") or cb in ("snan", "qnan"):
        # a NaN operand: IE only, no DE for a denormal other operand (i5-13600K)
        return b, IE
    if not daz and (ca == "denorm" or cb == "denorm"):
        flags |= DE

    def key(c, s, v):
        """total order of the non-NaN values: -inf < finite < +inf"""
        if c == "inf":
            return (-1, 0) if s else (1, 0)
        return (0, v)

    if ca == "zero" and cb == "zero":
        return b, flags
    ka, kb = key(ca, sa, va), key(cb, sb, vb)
    lt, gt = ka < kb, ka > kb
    if op == "min":
        return (a if lt else b), flags
    return (a if gt else b), flags


# ---------------------------------------------------------------------------------------
# a test case: input state, instruction bytes, expected changes
# ---------------------------------------------------------------------------------------
class Case:
    def __init__(self, title):
        self.title = title
        self.code = b""
        self.inp = []            # "key=value" strings
        self.exp = []
        self.fault = None
        self.zmm = {}            # register -> 64-byte image (inputs)
        self.k = {}
        self.mem = {}            # offset -> bytes
        self.mxcsr = None

    def set_zmm(self, r, img):
        self.zmm[r] = img

    def line(self):
        ins = []
        for r in sorted(self.zmm):
            ins.append("zmm%d=%s" % (r, hexs(self.zmm[r])))
        for r in sorted(self.k):
            ins.append("k%d=0x%X" % (r, self.k[r]))
        if self.mxcsr is not None:
            ins.append("mxcsr=0x%X" % self.mxcsr)
        for off in sorted(self.mem):
            ins.append("m+0x%X=%s" % (off, hexs(self.mem[off])))
        ins += self.inp
        exp = list(self.exp)
        if self.fault:
            exp.append(self.fault)
        return "%s | %s => %s" % (byte_list(self.code), " ".join(ins), " ".join(exp))


RNG = random.Random(0x5EED_E7E8)


def rnd_bytes(n):
    return bytes(RNG.getrandbits(8) for _ in range(n))


VL_LL = {16: 0, 32: 1, 64: 2}


# ---------------------------------------------------------------------------------------
# instruction models
# ---------------------------------------------------------------------------------------
def int_op(name, a, b, esz):
    m = (1 << (8 * esz)) - 1
    if name == "add":
        return (a + b) & m
    if name == "sub":
        return (a - b) & m
    if name == "and":
        return a & b
    if name == "andn":
        return (~a & m) & b
    if name == "or":
        return a | b
    if name == "xor":
        return a ^ b
    raise ValueError(name)


def signed(v, esz):
    bits = 8 * esz
    return v - (1 << bits) if v >> (bits - 1) else v


def pcmp(pred, a, b, esz, sign):
    if sign:
        a, b = signed(a, esz), signed(b, esz)
    return [a == b, a < b, a <= b, False, a != b, not a < b, not a <= b, True][pred & 7]


# ---------------------------------------------------------------------------------------
# case generators
# ---------------------------------------------------------------------------------------
cases = []


def emit(c):
    cases.append(c)


def comment(text):
    cases.append("# " + text)


def src_mem_image(vl, esz, bcst):
    if bcst:
        return rnd_bytes(esz)
    return rnd_bytes(vl)


def mem_case(c, off, data):
    c.mem[MEM_RSI + off] = data


# -- integer / logic: V, H, W forms -----------------------------------------------------
INT_FORMS = [
    # name, opcode, map, W rule ('W0','W1','WSEL'), op
    ("VPADDD", 0xFE, 1, 0, "add"), ("VPADDQ", 0xD4, 1, 1, "add"),
    ("VPSUBD", 0xFA, 1, 0, "sub"), ("VPSUBQ", 0xFB, 1, 1, "sub"),
    ("VPANDD", 0xDB, 1, 0, "and"), ("VPANDQ", 0xDB, 1, 1, "and"),
    ("VPANDND", 0xDF, 1, 0, "andn"), ("VPANDNQ", 0xDF, 1, 1, "andn"),
    ("VPORD", 0xEB, 1, 0, "or"), ("VPORQ", 0xEB, 1, 1, "or"),
    ("VPXORD", 0xEF, 1, 0, "xor"), ("VPXORQ", 0xEF, 1, 1, "xor"),
]


def gen_int_case(name, opc, w, op, vl, dst, s1, s2, kreg=0, kval=None, z=0, mem=None, bcst=0,
                 title_extra=""):
    esz = 8 if w else 4
    c = Case("%s %s" % (name, title_extra))
    imgs = {}
    for r in {dst, s1} | ({s2} if mem is None else set()):
        imgs[r] = rnd_bytes(64)
        c.set_zmm(r, imgs[r])
    if kreg:
        c.k[kreg] = kval
    a = elems(imgs[s1][:vl], esz)
    if mem is None:
        bvals = elems(imgs[s2][:vl], esz)
        rm = s2
        n = 1
    else:
        data = src_mem_image(vl, esz, bcst)
        mem_case(c, mem.disp, data)
        bvals = elems(data, esz) * (vl // esz) if bcst else elems(data, esz)
        rm = mem
        n = esz if bcst else vl
    res = pack([int_op(op, x, y, esz) for x, y in zip(a, bvals)], esz)
    kmask = None if kreg == 0 else kval
    out = mask_merge(imgs[dst], res, esz, vl, kmask, z)
    c.code = evex(1, 1, w, opc, dst, rm, vvvv=s1, ll=VL_LL[vl], b=bcst, z=z, aaa=kreg, n=n)
    c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    return c


def gen_int():
    comment("--- VPADDD/Q VPSUBD/Q VPANDD/Q VPANDND/Q VPORD/Q VPXORD/Q (Full tuple, E4)")
    for name, opc, mmm, w, op in INT_FORMS:
        comment("%s (EVEX.66.0F.W%d %02X /r)" % (name, w, opc))
        for vl in (16, 32, 64):
            emit(gen_int_case(name, opc, w, op, vl, 1, 2, 3, title_extra="nomask"))
            emit(gen_int_case(name, opc, w, op, vl, 17, 30, 9, kreg=3, kval=RNG.getrandbits(64),
                              title_extra="merge zmm16+"))
            emit(gen_int_case(name, opc, w, op, vl, 4, 5, 25, kreg=7, kval=RNG.getrandbits(64), z=1,
                              title_extra="zero"))
            emit(gen_int_case(name, opc, w, op, vl, 6, 7, 0, mem=Mem(RSI, 0), title_extra="mem"))
            emit(gen_int_case(name, opc, w, op, vl, 8, 31, 0, kreg=2, kval=RNG.getrandbits(64),
                              mem=Mem(RSI, 0), bcst=1, title_extra="bcst merge"))
        # destination = each source
        emit(gen_int_case(name, opc, w, op, 64, 5, 5, 6, kreg=1, kval=0xA5A5, title_extra="dst=src1"))
        emit(gen_int_case(name, opc, w, op, 64, 6, 5, 6, kreg=1, kval=0x5A5A, z=1, title_extra="dst=src2"))
        emit(gen_int_case(name, opc, w, op, 64, 7, 7, 7, title_extra="dst=src1=src2"))


# -- moves -------------------------------------------------------------------------------
MOV_FORMS = [
    # name, pp, opcode load, opcode store, W, aligned
    ("VMOVUPS", 0, 0x10, 0x11, 0, False), ("VMOVUPD", 1, 0x10, 0x11, 1, False),
    ("VMOVAPS", 0, 0x28, 0x29, 0, True), ("VMOVAPD", 1, 0x28, 0x29, 1, True),
    ("VMOVDQA32", 1, 0x6F, 0x7F, 0, True), ("VMOVDQA64", 1, 0x6F, 0x7F, 1, True),
    ("VMOVDQU32", 2, 0x6F, 0x7F, 0, False), ("VMOVDQU64", 2, 0x6F, 0x7F, 1, False),
]


def gen_mov():
    comment("--- vector moves (Full Mem tuple; VMOVAPS/PD, VMOVDQA32/64 E1 aligned)")
    for name, pp, lo, so, w, al in MOV_FORMS:
        esz = 8 if w else 4
        comment("%s (EVEX.%s.0F.W%d %02X / %02X)" % (name, ["NP", "66", "F3", "F2"][pp], w, lo, so))
        for vl in (16, 32, 64):
            ll = VL_LL[vl]
            for kreg, z in ((0, 0), (5, 0), (5, 1)):
                kval = RNG.getrandbits(64) if kreg else None
                # load reg <- reg
                c = Case("%s load reg" % name)
                d, s = (21, 13) if kreg else (2, 3)
                c.set_zmm(d, rnd_bytes(64)); c.set_zmm(s, rnd_bytes(64))
                if kreg:
                    c.k[kreg] = kval
                out = mask_merge(c.zmm[d], c.zmm[s][:vl], esz, vl, kval, z)
                c.code = evex(1, pp, w, lo, d, s, ll=ll, z=z, aaa=kreg)
                c.exp.append("zmm%d=%s" % (d, hexs(out)))
                emit(c)
                # load reg <- mem (aligned; disp8*N = VL)
                c = Case("%s load mem" % name)
                d = 9
                c.set_zmm(d, rnd_bytes(64))
                if kreg:
                    c.k[kreg] = kval
                data = rnd_bytes(vl)
                mem_case(c, vl, data)
                out = mask_merge(c.zmm[d], data, esz, vl, kval, z)
                c.code = evex(1, pp, w, lo, d, Mem(RSI, vl), ll=ll, z=z, aaa=kreg, n=vl)
                c.exp.append("zmm%d=%s" % (d, hexs(out)))
                emit(c)
                # store reg-reg form (0F 11/29/7F with mod = 11b: destination is r/m)
                c = Case("%s store form reg" % name)
                d, s = (26, 4)
                c.set_zmm(d, rnd_bytes(64)); c.set_zmm(s, rnd_bytes(64))
                if kreg:
                    c.k[kreg] = kval
                out = mask_merge(c.zmm[d], c.zmm[s][:vl], esz, vl, kval, z)
                c.code = evex(1, pp, w, so, s, d, ll=ll, z=z, aaa=kreg)
                c.exp.append("zmm%d=%s" % (d, hexs(out)))
                emit(c)
                if z:
                    continue
                # store mem <- reg (only active elements written)
                c = Case("%s store mem" % name)
                s = 18
                c.set_zmm(s, rnd_bytes(64))
                if kreg:
                    c.k[kreg] = kval
                old = rnd_bytes(vl)
                mem_case(c, -vl, old)
                ov, sv = elems(old, esz), elems(c.zmm[s][:vl], esz)
                nv = [sv[j] if (kval is None or (kval >> j) & 1) else ov[j] for j in range(vl // esz)]
                c.code = evex(1, pp, w, so, s, Mem(RSI, -vl), ll=ll, aaa=kreg, n=vl)
                c.exp.append("m+0x%X=%s" % (MEM_RSI - vl, hexs(pack(nv, esz))))
                emit(c)
            # z with a memory destination: #UD (Table 2-42)
            c = Case("%s store {z} #UD" % name)
            c.set_zmm(1, rnd_bytes(64)); c.k[1] = 0xFF
            c.code = evex(1, pp, w, so, 1, Mem(RSI, 0), ll=ll, z=1, aaa=1, n=vl)
            c.fault = "#UD"
            emit(c)
            # alignment
            mis = Mem(RSI, 8, disp32=True)
            c = Case("%s load misaligned" % name)
            c.set_zmm(3, rnd_bytes(64))
            data = rnd_bytes(vl)
            mem_case(c, 8, data)
            c.code = evex(1, pp, w, lo, 3, mis, ll=ll, n=vl)
            if al:
                c.fault = "#GP"
            else:
                c.exp.append("zmm3=%s" % hexs(data + bytes(64 - vl)))
            emit(c)
            if al:
                # E1: #GP also with an all-zero mask (k = 0) and for stores
                c = Case("%s load misaligned k=0 #GP" % name)
                c.set_zmm(3, rnd_bytes(64)); c.k[4] = 0
                c.code = evex(1, pp, w, lo, 3, mis, ll=ll, aaa=4, n=vl)
                c.fault = "#GP"
                emit(c)
                c = Case("%s store misaligned k=0 #GP" % name)
                c.set_zmm(3, rnd_bytes(64)); c.k[4] = 0
                c.code = evex(1, pp, w, so, 3, mis, ll=ll, aaa=4, n=vl)
                c.fault = "#GP"
                emit(c)
        # W mismatch for the fixed-W forms: #UD
        if name in ("VMOVUPS", "VMOVUPD", "VMOVAPS", "VMOVAPD"):
            c = Case("%s wrong W #UD" % name)
            c.code = evex(1, pp, 1 - w, lo, 1, 2, ll=2)
            c.fault = "#UD"
            emit(c)


def gen_fault_suppression():
    comment("--- masked memory: fault suppression next to the unmapped page MEM+0x10000")
    # [rsi + 0x7FC0 + 32]: elements 8..15 of a 512-bit dword access are unmapped
    base = 0x10000 - MEM_RSI - 32           # disp so that the access starts 32 bytes before the end
    for name, pp, lo, so, w, al in MOV_FORMS:
        if al:
            continue
        esz = 8 if w else 4
        nlo = 32 // esz                     # mapped elements
        c = Case("%s load fault suppressed" % name)
        c.set_zmm(2, rnd_bytes(64))
        kval = (1 << nlo) - 1
        c.k[1] = kval
        data = rnd_bytes(32)
        mem_case(c, base, data)
        out = mask_merge(c.zmm[2], data + bytes(32), esz, 64, kval, 1)
        c.code = evex(1, pp, w, lo, 2, Mem(RSI, base, disp32=True), ll=2, z=1, aaa=1)
        c.exp.append("zmm2=%s" % hexs(out))
        emit(c)
        c = Case("%s load not suppressed #PF" % name)
        c.set_zmm(2, rnd_bytes(64))
        c.k[1] = kval | (1 << nlo)
        mem_case(c, base, data)
        c.code = evex(1, pp, w, lo, 2, Mem(RSI, base, disp32=True), ll=2, z=1, aaa=1)
        c.fault = "#PF"
        emit(c)
        c = Case("%s store fault suppressed" % name)
        c.set_zmm(2, rnd_bytes(64))
        c.k[1] = kval
        old = rnd_bytes(32)
        mem_case(c, base, old)
        c.code = evex(1, pp, w, so, 2, Mem(RSI, base, disp32=True), ll=2, aaa=1)
        c.exp.append("m+0x%X=%s" % (MEM_RSI + base, hexs(c.zmm[2][:32])))
        emit(c)
        c = Case("%s store not suppressed #PF, memory unchanged" % name)
        c.set_zmm(2, rnd_bytes(64))
        c.k[1] = kval | (1 << (64 // esz - 1))
        mem_case(c, base, old)
        c.code = evex(1, pp, w, so, 2, Mem(RSI, base, disp32=True), ll=2, aaa=1)
        c.fault = "#PF"
        emit(c)
    # VPADDD with a masked {1toN} load: no lane active -> no access at all
    c = Case("VPADDD {1to16} k=0 on the unmapped page")
    c.set_zmm(1, rnd_bytes(64)); c.set_zmm(2, rnd_bytes(64)); c.k[6] = 0
    c.code = evex(1, 1, 0, 0xFE, 1, Mem(RSI, 0x10000 - MEM_RSI, disp32=True), vvvv=2, ll=2, b=1, aaa=6)
    c.exp.append("zmm1=%s" % hexs(c.zmm[1]))
    emit(c)
    c = Case("VPADDD {1to16} k=1 on the unmapped page #PF")
    c.set_zmm(1, rnd_bytes(64)); c.set_zmm(2, rnd_bytes(64)); c.k[6] = 1
    c.code = evex(1, 1, 0, 0xFE, 1, Mem(RSI, 0x10000 - MEM_RSI, disp32=True), vvvv=2, ll=2, b=1, aaa=6)
    c.fault = "#PF"
    emit(c)


def gen_disp8():
    comment("--- disp8*N: +-1 and +-127 for every tuple type x VL x W x EVEX.b of M1")
    for w in (0, 1):
        esz = 8 if w else 4
        for vl in (16, 32, 64):
            for bcst in (0, 1):
                n = esz if bcst else vl            # Full tuple (Table 2-36)
                for d8 in (1, -1, 127, -127):
                    off = d8 * n
                    emit(gen_int_case("VPADDD" if not w else "VPADDQ", 0xD4 if w else 0xFE, w, "add",
                                      vl, 10, 11, 0, mem=Mem(RSI, off), bcst=bcst,
                                      title_extra="Full disp8=%d N=%d" % (d8, n)))
            # Full Mem (moves)
            for d8 in (1, -1, 127, -127):
                c = Case("VMOVDQU%d disp8=%d N=%d" % (esz * 8, d8, vl))
                c.set_zmm(12, rnd_bytes(64))
                data = rnd_bytes(vl)
                mem_case(c, d8 * vl, data)
                c.code = evex(1, 2, w, 0x6F, 12, Mem(RSI, d8 * vl), ll=VL_LL[vl], n=vl)
                c.exp.append("zmm12=%s" % hexs(data + bytes(64 - vl)))
                emit(c)
            # Tuple1 Scalar (broadcast forms): N = element size
            for d8 in (1, -1, 127, -127):
                c = Case("VPBROADCAST%s disp8=%d N=%d" % ("Q" if w else "D", d8, esz))
                c.set_zmm(13, rnd_bytes(64))
                data = rnd_bytes(esz)
                mem_case(c, d8 * esz, data)
                c.code = evex(2, 1, w, 0x59 if w else 0x58, 13, Mem(RSI, d8 * esz), ll=VL_LL[vl], n=esz)
                c.exp.append("zmm13=%s" % hexs(data * (vl // esz) + bytes(64 - vl)))
                emit(c)
    # 16-bit / SIB / R14 base variants
    c = Case("VPADDD [r14 + rcx*4 + disp8*64]")
    c.set_zmm(1, rnd_bytes(64)); c.set_zmm(2, rnd_bytes(64))
    c.inp.append("rcx=0x10")
    data = rnd_bytes(64)
    mem_case(c, 0x40 + 0x40, data)
    a, b = elems(c.zmm[2], 4), elems(data, 4)
    c.code = evex(1, 1, 0, 0xFE, 1, Mem(R14, 0x40, index=1, scale=2), vvvv=2, ll=2, n=64)
    c.exp.append("zmm1=%s" % hexs(pack([(x + y) & 0xFFFFFFFF for x, y in zip(a, b)], 4)))
    emit(c)


# -- floating point -----------------------------------------------------------------------
def fbits32(x):
    import struct
    return struct.unpack("<I", struct.pack("<f", x))[0]


def fbits64(x):
    import struct
    return struct.unpack("<Q", struct.pack("<d", x))[0]


SPECIAL32 = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000,
             0x7FC00000, 0x7FA00000, 0x00000001, 0x80400000, 0x7F7FFFFF, 0x00800000,
             0x3EAAAAAB, 0x4B800001, 0x33800000, 0x1F800000]
SPECIAL64 = [0x0000000000000000, 0x8000000000000000, 0x3FF0000000000000, 0xBFF0000000000000,
             0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000, 0x7FF4000000000000,
             0x0000000000000001, 0x8008000000000000, 0x7FEFFFFFFFFFFFFF, 0x0010000000000000,
             0x3FD5555555555555, 0x4340000000000001, 0x3CA0000000000000, 0x1FF0000000000000]


def rnd_fp(f):
    """random finite value, mostly normal, spread exponents (never NaN)"""
    sign = RNG.getrandbits(1)
    if f is F32:
        e = RNG.choice([RNG.randint(100, 154), RNG.randint(1, 254)])
        return (sign << 31) | (e << 23) | RNG.getrandbits(23)
    e = RNG.choice([RNG.randint(1000, 1046), RNG.randint(1, 2046)])
    return (sign << 63) | (e << 52) | RNG.getrandbits(52)


FP_FORMS = [("VADDPS", 0x58, "add", 0), ("VADDPD", 0x58, "add", 1),
            ("VMULPS", 0x59, "mul", 0), ("VMULPD", 0x59, "mul", 1),
            ("VSUBPS", 0x5C, "sub", 0), ("VSUBPD", 0x5C, "sub", 1)]


def is_nan(x, f):
    return classify(x, f)[0] in ("snan", "qnan")


def no_nan_pairs(av, bv, f):
    """Lanes where both sources are NaN are left out of the EVEX cases (SRC1 becomes 1.0):
    the fork's SSE NaN propagation (QEMU float_2nan_prop_x87 on sse_status, see QEMU's own
    TODO) returns the QNaN / larger significand, the SDM (Vol1 4.8.3.5, Table 4-7) and the
    i5-13600K return SRC1. Known deviation of the shared SSE helpers, outside milestone M1."""
    one = 0x3FF0000000000000 if f is F64 else 0x3F800000
    return [one if is_nan(x, f) and is_nan(y, f) else x for x, y in zip(av, bv)] + av[len(bv):]


def fp_vector(op, w, a, b, kmask, mxcsr, rc=None, sae=False):
    """per element op on the active lanes; returns (results list or None lanes, flags)"""
    f = F64 if w else F32
    res, flags = [], 0
    for j, (x, y) in enumerate(zip(a, b)):
        if kmask is not None and not (kmask >> j) & 1:
            res.append(None)
            continue
        r, fl = fp_binop(op, x, y, f, mxcsr, rc)
        res.append(r)
        flags |= fl
    if sae:
        flags = 0
    return res, flags


def gen_fp_case(name, opc, op, w, vl, dst, s1, s2, kreg=0, kval=None, z=0, mem=None, bcst=0,
                avals=None, bvals=None, mxcsr=MXCSR_DEFAULT, rc=None, title=""):
    esz = 8 if w else 4
    f = F64 if w else F32
    n = vl // esz
    c = Case("%s %s" % (name, title))
    if avals is None:
        avals = [rnd_fp(f) for _ in range(n)]
    if bvals is None:
        bvals = [rnd_fp(f) for _ in range(1 if (mem is not None and bcst) else n)]
    if not (mem is None and s2 == s1):
        avals = no_nan_pairs(avals, bvals * n if (mem is not None and bcst) else bvals, f)
    regs = {}
    regs[s1] = pack(avals, esz) + rnd_bytes(64 - vl)
    if dst not in regs:
        regs[dst] = rnd_bytes(64)
    if mem is None:
        if s2 == s1:
            bvals = avals
        else:
            regs[s2] = pack(bvals, esz) + rnd_bytes(64 - vl)
        rm, nn = s2, 1
    else:
        data = pack(bvals, esz)
        mem_case(c, mem.disp, data)
        if bcst:
            bvals = bvals * n
        rm, nn = mem, (esz if bcst else vl)
    for r, img in regs.items():
        c.set_zmm(r, img)
    if dst == s1:
        old = regs[s1]
    elif mem is None and dst == s2:
        old = regs[s2]
    else:
        old = regs[dst]
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    kmask = kval if kreg else None
    sae = rc is not None
    res, flags = fp_vector(op, w, avals, bvals, kmask, mxcsr, rc, sae)
    unmasked = flags & ~(mxcsr >> 7) & 0x3F
    ll = rc if rc is not None else VL_LL[vl]
    c.code = evex(1, w, w, opc, dst, rm, vvvv=s1, ll=ll, b=1 if (bcst or rc is not None) else 0,
                  z=z, aaa=kreg, n=nn)
    if unmasked:
        if unmasked & (IE | DE | ZE):
            flags &= ~(OE | UE | PE)
        c.fault = "#XM"
        if (mxcsr | flags) != mxcsr:
            c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
        return c
    out_e = []
    o = elems(old[:vl], esz)
    for j in range(n):
        if res[j] is None:
            out_e.append(0 if z else o[j])
        else:
            out_e.append(res[j])
    out = pack(out_e, esz) + bytes(64 - vl)
    c.exp.append("zmm%d=%s" % (dst, hexs(out)))
    if (mxcsr | flags) != mxcsr:
        c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    return c


def gen_fp():
    comment("--- VADDPS/PD VMULPS/PD VSUBPS/PD (Full tuple, {er}, E2 #XM)")
    for name, opc, op, w in FP_FORMS:
        f = F64 if w else F32
        esz = 8 if w else 4
        sp = SPECIAL64 if w else SPECIAL32
        comment("%s (EVEX.%s.0F.W%d %02X /r)" % (name, "66" if w else "NP", w, opc))
        for vl in (16, 32, 64):
            n = vl // esz
            emit(gen_fp_case(name, opc, op, w, vl, 1, 2, 3, title="random"))
            emit(gen_fp_case(name, opc, op, w, vl, 20, 2, 29, kreg=1, kval=RNG.getrandbits(64), title="merge"))
            emit(gen_fp_case(name, opc, op, w, vl, 4, 5, 6, kreg=2, kval=RNG.getrandbits(64), z=1, title="zero"))
            emit(gen_fp_case(name, opc, op, w, vl, 7, 8, 0, mem=Mem(RSI, 0), title="mem"))
            emit(gen_fp_case(name, opc, op, w, vl, 9, 10, 0, mem=Mem(RSI, 0), bcst=1, kreg=3,
                             kval=RNG.getrandbits(64), title="{1toN} merge"))
            # special operands (all pairs spread over the lanes)
            a = [sp[(i * 3) % len(sp)] for i in range(n)]
            b = [sp[(i * 5 + 1) % len(sp)] for i in range(n)]
            emit(gen_fp_case(name, opc, op, w, vl, 11, 12, 13, avals=a, bvals=b, title="specials"))
            emit(gen_fp_case(name, opc, op, w, vl, 11, 12, 13, avals=b, bvals=a, title="specials swapped"))
        # rounding modes through MXCSR.RC, DAZ, FTZ
        for mx in (0x3F80, 0x5F80, 0x7F80, MXCSR_DEFAULT | DAZ, MXCSR_DEFAULT | FTZ,
                   MXCSR_DEFAULT | DAZ | FTZ):
            n = 64 // esz
            a = [rnd_fp(f) for _ in range(n)]
            b = [rnd_fp(f) for _ in range(n)]
            # some tiny / denormal lanes
            if f is F32:
                a[0], b[0] = 0x00400001, 0x00400003
                a[1], b[1] = 0x00800001, 0x80800000
                a[2], b[2] = 0x1F800001, 0x1F800001
            else:
                a[0], b[0] = 0x0008000000000001, 0x0008000000000003
                a[1], b[1] = 0x0010000000000001, 0x8010000000000000
                a[2], b[2] = 0x1FF0000000000001, 0x1FF0000000000001
            emit(gen_fp_case(name, opc, op, w, 64, 14, 15, 16, avals=a, bvals=b, mxcsr=mx,
                             title="mxcsr=%X" % mx))
        # {er}: L'L = rounding control, VL 512, flags suppressed, no #XM
        for rc in range(4):
            n = 64 // esz
            a = [rnd_fp(f) for _ in range(n)]
            b = [rnd_fp(f) for _ in range(n)]
            a[0] = sp[7]                                   # SNaN: IE suppressed
            emit(gen_fp_case(name, opc, op, w, 64, 17, 18, 19, avals=a, bvals=b, rc=rc,
                             title="{er} rc=%d" % rc))
            emit(gen_fp_case(name, opc, op, w, 64, 17, 18, 19, avals=a, bvals=b, rc=rc,
                             mxcsr=0x1F00 | (1 << 13), kreg=4, kval=RNG.getrandbits(16), z=1,
                             title="{er} rc=%d unmasked MXCSR, no #XM" % rc))
        # masked-off lanes raise nothing: SNaN in every masked-off lane, IM = 0
        n = 64 // esz
        a = [sp[7]] * n
        b = [rnd_fp(f) for _ in range(n)]
        kv = 0
        for j in range(n):
            if j % 3 == 0:
                a[j] = rnd_fp(f)
                kv |= 1 << j
        emit(gen_fp_case(name, opc, op, w, 64, 21, 22, 23, avals=a, bvals=b, kreg=5, kval=kv,
                         mxcsr=0x1F80 & ~0x80, title="SNaN only in masked-off lanes, IM=0"))
        # #XM: unmasked invalid in an active lane: destination unchanged, IE set
        emit(gen_fp_case(name, opc, op, w, 64, 21, 22, 23, avals=a, bvals=b, kreg=5,
                         kval=kv | 2, mxcsr=0x1F80 & ~0x80, title="SNaN active, IM=0 -> #XM"))
        # destination = source
        emit(gen_fp_case(name, opc, op, w, 64, 24, 24, 25, kreg=6, kval=0x5555, title="dst=src1"))
        emit(gen_fp_case(name, opc, op, w, 64, 25, 24, 25, kreg=6, kval=0x3333, z=1, title="dst=src2"))
        # EVEX.b on a 128/256-bit register form is {er} as well (VL = 512 implied)
        c = gen_fp_case(name, opc, op, w, 64, 26, 27, 28, rc=3, title="{rz-sae} L'L=11 is RC, not #UD")
        emit(c)


def gen_pcmp():
    comment("--- VPCMPEQD/Q, VPCMP[U]D/Q -> k (Full tuple, k1 {k2}, E4)")
    forms = [("VPCMPEQD", 1, 0x76, 0, None, True), ("VPCMPEQQ", 2, 0x29, 1, None, True),
             ("VPCMPD", 3, 0x1F, 0, "imm", True), ("VPCMPQ", 3, 0x1F, 1, "imm", True),
             ("VPCMPUD", 3, 0x1E, 0, "imm", False), ("VPCMPUQ", 3, 0x1E, 1, "imm", False)]
    for name, mmm, opc, w, imm, sign in forms:
        esz = 8 if w else 4
        comment("%s (EVEX.66.%s.W%d %02X)" % (name, ["", "0F", "0F38", "0F3A"][mmm], w, opc))
        preds = range(8) if imm else [0]
        for vl in (16, 32, 64):
            n = vl // esz
            for pred in preds:
                for variant in ("reg", "mask", "mem", "bcst"):
                    c = Case("%s pred=%d %s" % (name, pred, variant))
                    a = [RNG.choice([RNG.getrandbits(8 * esz), 0, 1, (1 << (8 * esz)) - 1,
                                     1 << (8 * esz - 1)]) for _ in range(n)]
                    b = [x if RNG.getrandbits(2) == 0 else RNG.choice([RNG.getrandbits(8 * esz), 0,
                         (1 << (8 * esz)) - 1, 1 << (8 * esz - 1)]) for x in a]
                    c.set_zmm(2, pack(a, esz) + rnd_bytes(64 - vl))
                    kd = 1 if variant != "mask" else 6
                    c.k[kd] = RNG.getrandbits(64)
                    kreg = 0
                    if variant in ("mask", "bcst"):
                        kreg = 3
                        c.k[3] = RNG.getrandbits(64)
                    if variant == "reg" or variant == "mask":
                        c.set_zmm(17, pack(b, esz) + rnd_bytes(64 - vl))
                        rm, nn, bc = 17, 1, 0
                    elif variant == "mem":
                        mem_case(c, -vl, pack(b, esz))
                        rm, nn, bc = Mem(RSI, -vl), vl, 0
                    else:
                        b = [b[0]] * n
                        mem_case(c, 8, pack(b[:1], esz))
                        rm, nn, bc = Mem(RSI, 8), esz, 1
                    r = 0
                    for j in range(n):
                        if (kreg == 0 or (c.k[kreg] >> j) & 1) and pcmp(pred, a[j], b[j], esz, sign):
                            r |= 1 << j
                    c.code = evex(mmm, 1, w, opc, kd, rm, vvvv=2, ll=VL_LL[vl], b=bc, aaa=kreg,
                                  imm=(pred | (0xF8 if pred == 5 else 0)) if imm else None, n=nn)
                    c.exp.append("k%d=0x%X" % (kd, r))
                    emit(c)
        # z on a k destination, R' = 0 (k1 = "k17"), R = 0 (k9): #UD
        c = Case("%s {z} #UD" % name)
        c.k[2] = 0xF
        c.code = evex(mmm, 1, w, opc, 1, 3, vvvv=2, ll=2, z=1, aaa=2, imm=0 if imm else None)
        c.fault = "#UD"
        emit(c)
        c = Case("%s ModRM.reg k with EVEX.R' = 0 #UD" % name)
        c.code = evex(mmm, 1, w, opc, 17, 3, vvvv=2, ll=2, imm=0 if imm else None)
        c.fault = "#UD"
        emit(c)
        c = Case("%s ModRM.reg k with EVEX.R = 0 #UD" % name)
        c.code = evex(mmm, 1, w, opc, 9, 3, vvvv=2, ll=2, imm=0 if imm else None)
        c.fault = "#UD"
        emit(c)


def gen_bcst():
    comment("--- VBROADCASTSS/SD, VPBROADCASTD/Q xmm/m (Tuple1 Scalar, E6), r32/r64 (E7NM)")
    forms = [("VBROADCASTSS", 0x18, 0), ("VBROADCASTSD", 0x19, 1),
             ("VPBROADCASTD", 0x58, 0), ("VPBROADCASTQ", 0x59, 1)]
    for name, opc, w in forms:
        esz = 8 if w else 4
        for vl in (16, 32, 64):
            if name == "VBROADCASTSD" and vl == 16:
                c = Case("VBROADCASTSD EVEX.128 #UD")
                c.code = evex(2, 1, 1, 0x19, 1, 2, ll=0)
                c.fault = "#UD"
                emit(c)
                continue
            for kreg, z in ((0, 0), (2, 0), (2, 1)):
                kval = RNG.getrandbits(64) if kreg else None
                c = Case("%s xmm" % name)
                c.set_zmm(1, rnd_bytes(64)); c.set_zmm(22, rnd_bytes(64))
                if kreg:
                    c.k[kreg] = kval
                e0 = c.zmm[22][:esz]
                out = mask_merge(c.zmm[1], e0 * (vl // esz), esz, vl, kval, z)
                c.code = evex(2, 1, w, opc, 1, 22, ll=VL_LL[vl], z=z, aaa=kreg)
                c.exp.append("zmm1=%s" % hexs(out))
                emit(c)
                c = Case("%s mem" % name)
                c.set_zmm(30, rnd_bytes(64))
                if kreg:
                    c.k[kreg] = kval
                data = rnd_bytes(esz)
                mem_case(c, 3 * esz, data)
                out = mask_merge(c.zmm[30], data * (vl // esz), esz, vl, kval, z)
                c.code = evex(2, 1, w, opc, 30, Mem(RSI, 3 * esz), ll=VL_LL[vl], z=z, aaa=kreg, n=esz)
                c.exp.append("zmm30=%s" % hexs(out))
                emit(c)
        # EVEX.b on a Tuple1 Scalar form: #UD (Table 2-43)
        c = Case("%s EVEX.b #UD" % name)
        c.code = evex(2, 1, w, opc, 1, Mem(RSI, 0), ll=2, b=1, n=esz)
        c.fault = "#UD"
        emit(c)
    # GPR source
    for w in (0, 1):
        esz = 8 if w else 4
        for vl in (16, 32, 64):
            for kreg, z in ((0, 0), (7, 0), (7, 1)):
                kval = RNG.getrandbits(64) if kreg else None
                c = Case("VPBROADCAST%s r%d" % ("Q" if w else "D", esz * 8))
                c.set_zmm(3, rnd_bytes(64))
                g = RNG.getrandbits(64)
                c.inp.append("r9=0x%X" % g)
                if kreg:
                    c.k[kreg] = kval
                e0 = (g & ((1 << (8 * esz)) - 1)).to_bytes(esz, "little")
                out = mask_merge(c.zmm[3], e0 * (vl // esz), esz, vl, kval, z)
                c.code = evex(2, 1, w, 0x7C, 3, 9, ll=VL_LL[vl], z=z, aaa=kreg)
                c.exp.append("zmm3=%s" % hexs(out))
                emit(c)
    c = Case("VPBROADCASTD r32 memory form #UD")
    c.code = evex(2, 1, 0, 0x7C, 3, Mem(RSI, 0), ll=2)
    c.fault = "#UD"
    emit(c)
    c = Case("VPBROADCASTD r32: EVEX.X ignored for a GPR r/m (zmm16 bit not a GPR bit)")
    c.set_zmm(3, rnd_bytes(64))
    c.inp.append("rax=0x1234567890ABCDEF")
    c.code = evex(2, 1, 0, 0x7C, 3, 16, ll=2)                 # r/m = 16: X = 1, B = 0 -> eax
    c.exp.append("zmm3=%s" % hexs((0x90ABCDEF).to_bytes(4, "little") * 16))
    emit(c)


def gen_ud():
    comment("--- #UD matrix (SDM Vol2A Tables 2-40 .. 2-43, 2-46)")
    base = dict(mmm=1, pp=1, w=0, opc=0xFE, reg=1, rm=2, vvvv=3, ll=2)

    def ud(title, **kw):
        a = dict(base)
        a.update(kw)
        c = Case(title)
        c.code = evex(a.pop("mmm"), a.pop("pp"), a.pop("w"), a.pop("opc"), a.pop("reg"), a.pop("rm"),
                      **a)
        c.fault = "#UD"
        emit(c)

    for pfx, nm in ((b"\x66", "66"), (b"\xf2", "F2"), (b"\xf3", "F3"), (b"\xf0", "LOCK"), (b"\x48", "REX.W"),
                    (b"\x40", "REX")):
        ud("%s before EVEX" % nm, prefixes=pfx)
    ud("P0[3] = 1", p0_or=0x08)
    ud("P1[2] (U) = 0", p1_and=0xFB)
    for m in (0, 4, 5, 6, 7):
        ud("EVEX.mmm = %d" % m, mmm=m)
    ud("L'L = 11b (vector form, no EVEX.b)", ll=3)
    ud("EVEX.b on a register form without {er}/{sae}", b=1)
    ud("{z} with aaa = 000b", z=1)
    ud("VMOVDQU32 EVEX.b with memory (Full Mem tuple)", pp=2, opc=0x6F, rm=Mem(RSI, 0), vvvv=None, b=1)
    ud("VMOVDQU32 vvvv != 1111b", pp=2, opc=0x6F, vvvv=5)
    ud("VMOVDQU32 V' = 0 (vvvv unused)", pp=2, opc=0x6F, vvvv=None, p2_vp=0)
    ud("VPADDD with pp = NP", pp=0)
    ud("VPADDD with W1", w=1)
    ud("VPADDQ with W0", opc=0xD4)
    ud("VPCMPEQD with W1", opc=0x76, w=1)
    ud("unassigned EVEX 0F 00", opc=0x00)
    # a valid encoding right after for contrast: VPXORD zmm1, zmm1, zmm1 = 0
    c = Case("VPXORD zmm1, zmm1, zmm1 (control)")
    c.set_zmm(1, rnd_bytes(64))
    c.code = evex(1, 1, 0, 0xEF, 1, 1, vvvv=1, ll=2)
    c.exp.append("zmm1=%s" % hexs(bytes(64)))
    emit(c)
    # EVEX.X selects zmm16-31 for r/m; in 64-bit mode R' is valid for vector registers
    c = Case("VMOVDQU64 zmm31, zmm16 (R', X)")
    c.set_zmm(31, rnd_bytes(64)); c.set_zmm(16, rnd_bytes(64))
    c.code = evex(1, 2, 1, 0x6F, 31, 16, ll=2)
    c.exp.append("zmm31=%s" % hexs(c.zmm[16]))
    emit(c)


# ---------------------------------------------------------------------------------------
# further AVX512F forms (ledger U154-U159): one generic generator per operand layout
#   rvm  zmm1{k}{z}, zmm2 (vvvv), zmm3/m/{1toN}      rm   zmm1{k}{z}, zmm2/m/{1toN}
#   vmi  zmm1 (vvvv){k}{z}, zmm2/m/{1toN}, imm8       kvm  k1{k2}, zmm2 (vvvv), zmm3/m/{1toN}
#   rvmi zmm1{k}{z}, zmm2 (vvvv), zmm3/m/{1toN}, imm8 (VPTERNLOG: DEST is also a source)
# ---------------------------------------------------------------------------------------
def u(v, esz):
    return v & ((1 << (8 * esz)) - 1)


def elem_int(op, a, b, esz, imm=0, d=0):
    bits = 8 * esz
    sa, sb = signed(a, esz), signed(b, esz)
    if op == "mins":
        return u(min(sa, sb), esz)
    if op == "minu":
        return min(a, b)
    if op == "maxs":
        return u(max(sa, sb), esz)
    if op == "maxu":
        return max(a, b)
    if op == "mulld":
        return u(a * b, esz)
    if op == "muludq":
        return (a & 0xFFFFFFFF) * (b & 0xFFFFFFFF)
    if op == "muldq":
        return u(signed(a & 0xFFFFFFFF, 4) * signed(b & 0xFFFFFFFF, 4), 8)
    if op == "abs":
        return u(abs(sb), esz)
    if op == "sllv":
        return u(a << b, esz) if b < bits else 0
    if op == "srlv":
        return a >> b if b < bits else 0
    if op == "srav":
        return u(sa >> min(b, bits - 1), esz)
    if op == "slli":
        return u(b << imm, esz) if imm < bits else 0
    if op == "srli":
        return b >> imm if imm < bits else 0
    if op == "srai":
        return u(sb >> min(imm, bits - 1), esz)
    if op == "roli":
        c = imm % bits
        return u((b << c) | (b >> (bits - c)), esz) if c else b
    if op == "rori":
        c = imm % bits
        return u((b >> c) | (b << (bits - c)), esz) if c else b
    if op == "ternlog":
        r = 0
        for i in range(bits):
            idx = (((d >> i) & 1) << 2) | (((a >> i) & 1) << 1) | ((b >> i) & 1)
            r |= ((imm >> idx) & 1) << i
        return r
    raise ValueError(op)


def elem_kcmp(op, a, b, esz):
    if op == "gt":
        return signed(a, esz) > signed(b, esz)
    if op == "testm":
        return (a & b) != 0
    if op == "testnm":
        return (a & b) == 0
    raise ValueError(op)


def rnd_elem(esz, fp=False):
    if fp:
        return rnd_fp(F64 if esz == 8 else F32)
    bits = 8 * esz
    return RNG.choice([RNG.getrandbits(bits), 0, 1, (1 << bits) - 1, 1 << (bits - 1),
                       (1 << (bits - 1)) - 1, RNG.getrandbits(6), RNG.getrandbits(bits)])


def gen_generic(sp, vl, variant, dst=1, s1=2, s2=3, kreg=0, kval=None, z=0, imm=None,
                avals=None, bvals=None, mxcsr=MXCSR_DEFAULT, rc=None, sae=False, ll=None):
    """one case of the form described by sp (dict: name mmm pp opc w layout op kind fp reg)"""
    w, esz = sp["w"], 8 if sp["w"] else 4
    n = vl // esz
    lay, fp = sp["layout"], sp.get("fp", False)
    c = Case("%s VL%d %s" % (sp["name"], vl * 8, variant))
    if imm is None and lay in ("vmi", "rvmi"):
        imm = RNG.getrandbits(8)
    mem = variant in ("mem", "bcst")
    bcst = variant == "bcst"
    if avals is None:
        avals = [rnd_elem(esz, fp) for _ in range(n)]
    if bvals is None:
        bvals = [rnd_elem(esz, fp) for _ in range(1 if bcst else n)]
    if fp and lay == "rvm" and sp["op"] in ("add", "sub", "mul", "div") and (mem or s1 != s2):
        avals = no_nan_pairs(avals, bvals * n if bcst else bvals, F64 if w else F32)
    regs = {}
    # register numbers per layout: dst / src1 (vvvv) / src2 (r/m)
    if lay in ("rvm", "kvm", "rvmi"):
        regs[s1] = pack(avals, esz) + rnd_bytes(64 - vl)
    if not mem:
        if s2 in regs:
            bvals = elems(regs[s2][:vl], esz)
        else:
            regs[s2] = pack(bvals, esz) + rnd_bytes(64 - vl)
    if lay != "kvm" and dst not in regs:
        regs[dst] = rnd_bytes(64)
    for r, img in regs.items():
        c.set_zmm(r, img)
    if lay in ("rvm", "kvm", "rvmi") and s1 in regs:
        avals = elems(regs[s1][:vl], esz)
    if mem:
        mem_case(c, 0x40, pack(bvals, esz))
        rm = Mem(RSI, 0x40)
        nn = esz if bcst else vl
        if bcst:
            bvals = bvals * n
    else:
        rm, nn = s2, 1
    if kreg:
        c.k[kreg] = kval
    if mxcsr != MXCSR_DEFAULT:
        c.mxcsr = mxcsr
    kmask = kval if kreg else None
    old = regs.get(dst, bytes(64)) if lay != "kvm" else None
    dvals = elems(old[:vl], esz) if old is not None else None
    # operation
    flags, unm = 0, 0
    if lay == "kvm":
        r = 0
        for j in range(n):
            if (kmask is None or (kmask >> j) & 1) and elem_kcmp(sp["op"], avals[j], bvals[j], esz):
                r |= 1 << j
        c.k.setdefault(dst, RNG.getrandbits(64))
        c.exp.append("k%d=0x%X" % (dst, r))
    else:
        if fp:
            res, flags, unm = model_vec(sp["op"], w, avals if lay == "rvm" else bvals, bvals, mxcsr,
                                        kmask, rc)
            if sae:
                flags, unm = 0, 0
        else:
            res = []
            for j in range(n):
                if kmask is not None and not (kmask >> j) & 1:
                    res.append(None)
                elif lay == "rm":
                    res.append(elem_int(sp["op"], 0, bvals[j], esz))
                elif lay == "vmi":
                    res.append(elem_int(sp["op"], 0, bvals[j], esz, imm))
                elif lay == "rvmi":
                    res.append(elem_int(sp["op"], avals[j], bvals[j], esz, imm, dvals[j]))
                else:
                    res.append(elem_int(sp["op"], avals[j], bvals[j], esz))
        if unm:
            c.fault = "#XM"
        else:
            out = pack([(0 if z else dvals[j]) if res[j] is None else res[j] for j in range(n)], esz)
            c.exp.append("zmm%d=%s" % (dst, hexs(out + bytes(64 - vl))))
        if (mxcsr | flags) != mxcsr:
            c.exp.append("mxcsr=0x%X" % (mxcsr | flags))
    # encoding
    if ll is None:
        ll = rc if rc is not None else VL_LL[vl]
    b = 1 if (bcst or rc is not None or sae) else 0
    if lay == "vmi":
        c.code = evex(sp["mmm"], sp["pp"], w, sp["opc"], sp["reg"], rm, vvvv=dst, ll=ll, b=b, z=z,
                      aaa=kreg, imm=imm, n=nn)
    elif lay == "rm":
        c.code = evex(sp["mmm"], sp["pp"], w, sp["opc"], dst, rm, ll=ll, b=b, z=z, aaa=kreg, n=nn)
    else:
        c.code = evex(sp["mmm"], sp["pp"], w, sp["opc"], dst, rm, vvvv=s1, ll=ll, b=b, z=z, aaa=kreg,
                      imm=imm, n=nn)
    return c


EXT_FORMS = [
    # U154: k destinations
    dict(name="VPCMPGTD", mmm=1, pp=1, opc=0x66, w=0, layout="kvm", op="gt"),
    dict(name="VPCMPGTQ", mmm=2, pp=1, opc=0x37, w=1, layout="kvm", op="gt"),
    dict(name="VPTESTMD", mmm=2, pp=1, opc=0x27, w=0, layout="kvm", op="testm"),
    dict(name="VPTESTMQ", mmm=2, pp=1, opc=0x27, w=1, layout="kvm", op="testm"),
    dict(name="VPTESTNMD", mmm=2, pp=2, opc=0x27, w=0, layout="kvm", op="testnm"),
    dict(name="VPTESTNMQ", mmm=2, pp=2, opc=0x27, w=1, layout="kvm", op="testnm"),
    # U155: integer
    dict(name="VPABSD", mmm=2, pp=1, opc=0x1E, w=0, layout="rm", op="abs"),
    dict(name="VPABSQ", mmm=2, pp=1, opc=0x1F, w=1, layout="rm", op="abs"),
    dict(name="VPMULDQ", mmm=2, pp=1, opc=0x28, w=1, layout="rvm", op="muldq"),
    dict(name="VPMINSD", mmm=2, pp=1, opc=0x39, w=0, layout="rvm", op="mins"),
    dict(name="VPMINSQ", mmm=2, pp=1, opc=0x39, w=1, layout="rvm", op="mins"),
    dict(name="VPMINUD", mmm=2, pp=1, opc=0x3B, w=0, layout="rvm", op="minu"),
    dict(name="VPMINUQ", mmm=2, pp=1, opc=0x3B, w=1, layout="rvm", op="minu"),
    dict(name="VPMAXSD", mmm=2, pp=1, opc=0x3D, w=0, layout="rvm", op="maxs"),
    dict(name="VPMAXSQ", mmm=2, pp=1, opc=0x3D, w=1, layout="rvm", op="maxs"),
    dict(name="VPMAXUD", mmm=2, pp=1, opc=0x3F, w=0, layout="rvm", op="maxu"),
    dict(name="VPMAXUQ", mmm=2, pp=1, opc=0x3F, w=1, layout="rvm", op="maxu"),
    dict(name="VPMULLD", mmm=2, pp=1, opc=0x40, w=0, layout="rvm", op="mulld"),
    dict(name="VPMULUDQ", mmm=1, pp=1, opc=0xF4, w=1, layout="rvm", op="muludq"),
    # U156: floating point
    dict(name="VSQRTPS", mmm=1, pp=0, opc=0x51, w=0, layout="rm", op="sqrt", fp=True, rcform="er"),
    dict(name="VSQRTPD", mmm=1, pp=1, opc=0x51, w=1, layout="rm", op="sqrt", fp=True, rcform="er"),
    dict(name="VMINPS", mmm=1, pp=0, opc=0x5D, w=0, layout="rvm", op="min", fp=True, rcform="sae"),
    dict(name="VMINPD", mmm=1, pp=1, opc=0x5D, w=1, layout="rvm", op="min", fp=True, rcform="sae"),
    dict(name="VDIVPS", mmm=1, pp=0, opc=0x5E, w=0, layout="rvm", op="div", fp=True, rcform="er"),
    dict(name="VDIVPD", mmm=1, pp=1, opc=0x5E, w=1, layout="rvm", op="div", fp=True, rcform="er"),
    dict(name="VMAXPS", mmm=1, pp=0, opc=0x5F, w=0, layout="rvm", op="max", fp=True, rcform="sae"),
    dict(name="VMAXPD", mmm=1, pp=1, opc=0x5F, w=1, layout="rvm", op="max", fp=True, rcform="sae"),
    # U157: variable shifts
    dict(name="VPSRLVD", mmm=2, pp=1, opc=0x45, w=0, layout="rvm", op="srlv"),
    dict(name="VPSRLVQ", mmm=2, pp=1, opc=0x45, w=1, layout="rvm", op="srlv"),
    dict(name="VPSRAVD", mmm=2, pp=1, opc=0x46, w=0, layout="rvm", op="srav"),
    dict(name="VPSRAVQ", mmm=2, pp=1, opc=0x46, w=1, layout="rvm", op="srav"),
    dict(name="VPSLLVD", mmm=2, pp=1, opc=0x47, w=0, layout="rvm", op="sllv"),
    dict(name="VPSLLVQ", mmm=2, pp=1, opc=0x47, w=1, layout="rvm", op="sllv"),
    # U158: shifts / rotates by imm8 (ModRM.reg = opcode extension)
    dict(name="VPRORD", mmm=1, pp=1, opc=0x72, reg=0, w=0, layout="vmi", op="rori"),
    dict(name="VPRORQ", mmm=1, pp=1, opc=0x72, reg=0, w=1, layout="vmi", op="rori"),
    dict(name="VPROLD", mmm=1, pp=1, opc=0x72, reg=1, w=0, layout="vmi", op="roli"),
    dict(name="VPROLQ", mmm=1, pp=1, opc=0x72, reg=1, w=1, layout="vmi", op="roli"),
    dict(name="VPSRLD", mmm=1, pp=1, opc=0x72, reg=2, w=0, layout="vmi", op="srli"),
    dict(name="VPSRAD", mmm=1, pp=1, opc=0x72, reg=4, w=0, layout="vmi", op="srai"),
    dict(name="VPSRAQ", mmm=1, pp=1, opc=0x72, reg=4, w=1, layout="vmi", op="srai"),
    dict(name="VPSLLD", mmm=1, pp=1, opc=0x72, reg=6, w=0, layout="vmi", op="slli"),
    dict(name="VPSRLQ", mmm=1, pp=1, opc=0x73, reg=2, w=1, layout="vmi", op="srli"),
    dict(name="VPSLLQ", mmm=1, pp=1, opc=0x73, reg=6, w=1, layout="vmi", op="slli"),
    # U159
    dict(name="VPTERNLOGD", mmm=3, pp=1, opc=0x25, w=0, layout="rvmi", op="ternlog"),
    dict(name="VPTERNLOGQ", mmm=3, pp=1, opc=0x25, w=1, layout="rvmi", op="ternlog"),
]


def gen_ext():
    comment("--- further AVX512F forms (U154-U159), same generic EVEX machinery")
    for sp in EXT_FORMS:
        esz = 8 if sp["w"] else 4
        comment("%s (EVEX.%s.%s.W%d %02X%s)" % (sp["name"], ["NP", "66", "F3", "F2"][sp["pp"]],
                ["", "0F", "0F38", "0F3A"][sp["mmm"]], sp["w"], sp["opc"],
                (" /%d" % sp["reg"]) if "reg" in sp else ""))
        kd = sp["layout"] == "kvm"
        for vl in (16, 32, 64):
            n = vl // esz
            emit(gen_generic(sp, vl, "nomask", dst=1, s1=2, s2=3))
            emit(gen_generic(sp, vl, "merge", dst=(5 if kd else 20), s1=22, s2=7, kreg=3,
                             kval=RNG.getrandbits(64)))
            if not kd:
                emit(gen_generic(sp, vl, "zero", dst=4, s1=5, s2=26, kreg=6, kval=RNG.getrandbits(64), z=1))
            emit(gen_generic(sp, vl, "mem", dst=6, s1=8, s2=0))
            emit(gen_generic(sp, vl, "bcst", dst=7, s1=9, s2=0, kreg=2, kval=RNG.getrandbits(64)))
        if sp["layout"] == "vmi":
            # counts 0, 1, width-1, width, width+1, 0xFF
            for cnt in (0, 1, 8 * esz - 1, 8 * esz, 8 * esz + 1, 0xFF):
                emit(gen_generic(sp, 64, "imm=%d" % cnt, dst=10, s2=11, imm=cnt))
        if sp["op"] in ("sllv", "srlv", "srav"):
            n = 64 // esz
            counts = [0, 1, 8 * esz - 1, 8 * esz, 8 * esz + 1, (1 << (8 * esz)) - 1, 1 << (8 * esz - 1), 5]
            counts = (counts * 2)[:n]
            emit(gen_generic(sp, 64, "counts 0..>=width", dst=12, s1=13, s2=14, bvals=counts))
        if sp["layout"] == "rvmi":
            for imm in (0x00, 0xFF, 0x96, 0xE8, 0xCA, 0xF0, 0xCC, 0xAA):
                emit(gen_generic(sp, 64, "imm=%02X" % imm, dst=15, s1=16, s2=17, imm=imm))
            emit(gen_generic(sp, 64, "dst=src2 merge", dst=18, s1=18, s2=19, imm=0x96, kreg=4,
                             kval=0xA5A5))
        if not kd:
            emit(gen_generic(sp, 64, "dst=src1", dst=21, s1=21, s2=23, kreg=5, kval=RNG.getrandbits(64),
                             imm=0x6A if sp["layout"] in ("vmi", "rvmi") else None))
        if sp.get("fp"):
            f = F64 if sp["w"] else F32
            spl = SPECIAL64 if sp["w"] else SPECIAL32
            n = 64 // esz
            a = [spl[(i * 3) % len(spl)] for i in range(n)]
            b = [spl[(i * 5 + 1) % len(spl)] for i in range(n)]
            emit(gen_generic(sp, 64, "specials", dst=24, s1=25, s2=27, avals=a, bvals=b))
            emit(gen_generic(sp, 64, "specials swapped", dst=24, s1=25, s2=27, avals=b, bvals=a))
            for mx in (0x3F80, 0x5F80, 0x7F80, MXCSR_DEFAULT | DAZ, MXCSR_DEFAULT | FTZ):
                a = [rnd_fp(f) for _ in range(n)]
                b = [rnd_fp(f) for _ in range(n)]
                a[0], b[0] = (0x00400001, 0x00400003) if f is F32 else (0x0008000000000001, 0x0008000000000003)
                emit(gen_generic(sp, 64, "mxcsr=%X" % mx, dst=28, s1=29, s2=30, avals=a, bvals=b, mxcsr=mx))
            # {er} / {sae}: flags suppressed, no #XM even with unmasked MXCSR and SNaN operands
            a = [rnd_fp(f) for _ in range(n)]
            b = [rnd_fp(f) for _ in range(n)]
            b[1] = spl[7]
            if sp["rcform"] == "er":
                for rc in range(4):
                    emit(gen_generic(sp, 64, "{er} rc=%d" % rc, dst=31, s1=2, s2=3, avals=a, bvals=b,
                                     rc=rc, mxcsr=0x1F00))
            else:
                for ll in (0, 2, 3):
                    emit(gen_generic(sp, 64, "{sae} L'L=%d (ignored, VL 512)" % ll, dst=31, s1=2, s2=3,
                                     avals=a, bvals=b, sae=True, mxcsr=0x1F00, ll=ll))
            # #XM: unmasked invalid in an active lane
            a = [rnd_fp(f) for _ in range(n)]
            b = [rnd_fp(f) for _ in range(n)]
            b[2] = spl[7]
            emit(gen_generic(sp, 64, "SNaN active, IM=0 -> #XM", dst=4, s1=5, s2=6, avals=a, bvals=b,
                             mxcsr=0x1F00))
            emit(gen_generic(sp, 64, "SNaN masked off, IM=0", dst=4, s1=5, s2=6, avals=a, bvals=b,
                             mxcsr=0x1F00, kreg=1, kval=~4 & 0xFFFF))
        else:
            # EVEX.b on a register form of an integer instruction: #UD
            c = Case("%s EVEX.b on a register form #UD" % sp["name"])
            if sp["layout"] == "vmi":
                c.code = evex(sp["mmm"], sp["pp"], sp["w"], sp["opc"], sp["reg"], 3, vvvv=1, ll=2, b=1, imm=1)
            elif sp["layout"] == "rm":
                c.code = evex(sp["mmm"], sp["pp"], sp["w"], sp["opc"], 1, 3, ll=2, b=1)
            else:
                c.code = evex(sp["mmm"], sp["pp"], sp["w"], sp["opc"], 1, 3, vvvv=2, ll=2, b=1,
                              imm=0 if sp["layout"] == "rvmi" else None)
            c.fault = "#UD"
            emit(c)
        if sp["layout"] == "rm":
            c = Case("%s vvvv != 1111b #UD" % sp["name"])
            c.code = evex(sp["mmm"], sp["pp"], sp["w"], sp["opc"], 1, 3, vvvv=4, ll=2)
            c.fault = "#UD"
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

    # VPADDD zmm1, zmm2, zmm3 = 62 F1 6D 48 FE CB (SDM: EVEX.512.66.0F.W0 FE /r)
    chk("enc vpaddd", evex(1, 1, 0, 0xFE, 1, 3, vvvv=2, ll=2), bytes([0x62, 0xF1, 0x6D, 0x48, 0xFE, 0xCB]))
    # VPADDD zmm1{k1}{z}, zmm2, [rsi+0x40] -> disp8 = 1 with N = 64
    chk("enc disp8", evex(1, 1, 0, 0xFE, 1, Mem(RSI, 0x40), vvvv=2, ll=2, z=1, aaa=1, n=64),
        bytes([0x62, 0xF1, 0x6D, 0xC9, 0xFE, 0x4E, 0x01]))
    # VMOVDQU64 zmm31, zmm16: R=0 R'=0 (both set), B=0 X=1 -> P0 = 0 0 1 0 0 001 = 0x21
    chk("enc r'x", evex(1, 2, 1, 0x6F, 31, 16, ll=2), bytes([0x62, 0x21, 0xFE, 0x48, 0x6F, 0xF8]))
    # FP: 1 + 2^-24 (RNE) = 1, PE; RU = 1 + 2^-23
    chk("add rne", fp_binop("add", 0x3F800000, 0x33800000, F32, 0x1F80), (0x3F800000, PE))
    chk("add ru", fp_binop("add", 0x3F800000, 0x33800000, F32, 0x5F80), (0x3F800001, PE))
    chk("add rd neg", fp_binop("add", 0xBF800000, 0xB3800000, F32, 0x3F80), (0xBF800001, PE))
    chk("x-x rd", fp_binop("sub", 0x3F800000, 0x3F800000, F32, 0x3F80), (0x80000000, 0))
    chk("x-x rn", fp_binop("sub", 0x3F800000, 0x3F800000, F32, 0x1F80), (0x00000000, 0))
    chk("inf-inf", fp_binop("sub", 0x7F800000, 0x7F800000, F32, 0x1F80), (0xFFC00000, IE))
    chk("0*inf", fp_binop("mul", 0x00000000, 0xFF800000, F32, 0x1F80), (0xFFC00000, IE))
    chk("snan+qnan", fp_binop("add", 0x7FA00000, 0x7FC00001, F32, 0x1F80), (0x7FE00000, IE))
    chk("qnan2", fp_binop("add", 0x3F800000, 0xFFC00001, F32, 0x1F80), (0xFFC00001, 0))
    chk("ovf rn", fp_binop("mul", 0x7F7FFFFF, 0x40000000, F32, 0x1F80), (0x7F800000, OE | PE))
    chk("ovf rz", fp_binop("mul", 0x7F7FFFFF, 0x40000000, F32, 0x7F80), (0x7F7FFFFF, OE | PE))
    chk("denorm in", fp_binop("add", 0x00000001, 0x00000001, F32, 0x1F80), (0x00000002, DE))
    chk("daz", fp_binop("add", 0x00000001, 0x00000001, F32, 0x1F80 | DAZ), (0x00000000, 0))
    # 2^-126 * 0.5000001 -> tiny, inexact: UE PE; FTZ -> +0
    chk("tiny", fp_binop("mul", 0x00800001, 0x3F000000, F32, 0x1F80), (0x00400000, UE | PE | DE * 0))
    chk("ftz", fp_binop("mul", 0x00800001, 0x3F000000, F32, 0x1F80 | FTZ), (0x00000000, UE | PE))
    chk("ftz exact", fp_binop("add", 0x00800001, 0x80800000, F32, 0x1F80 | FTZ), (0x00000000, UE | PE))
    chk("exact tiny", fp_binop("add", 0x00800001, 0x80800000, F32, 0x1F80), (0x00000001, 0))
    # 2^-126 * (1 - 2^-24) is exact with an unbounded exponent and below 2^-126: tiny;
    # the denormal rounding (tie, to even) gives 2^-126: UE PE
    chk("tiny exact unbounded", fp_binop("mul", 0x00800000, 0x3F7FFFFF, F32, 0x1F80), (0x00800000, UE | PE))
    # 2^-126 (1 + 2^-23)(1 - 2^-23) = 2^-126 (1 - 2^-46) rounds (RNE, unbounded) to 2^-126:
    # not tiny after rounding (Vol1 4.9.1.5), inexact -> PE only
    chk("tiny after rounding", fp_binop("mul", 0x00800001, 0x3F7FFFFE, F32, 0x1F80), (0x00800000, PE))
    chk("d add", fp_binop("add", 0x3FF0000000000000, 0x3CA0000000000000, F64, 0x1F80), (0x3FF0000000000000, PE))
    chk("pcmp lt signed", pcmp(1, 0xFFFFFFFF, 0, 4, True), True)
    chk("pcmp lt unsigned", pcmp(1, 0xFFFFFFFF, 0, 4, False), False)
    return ok


# ---------------------------------------------------------------------------------------
# validation of the floating-point model against the host CPU: the legacy SSE forms of the
# same operations (the i5-13600K has SSE but no AVX-512) run as emu-alltest *hardware* cases
# (self-generated snippets); --hwcmp compares the host's results ("hw:" lines) with the model
# ---------------------------------------------------------------------------------------
HW_OPS = [("addps", "add", 0), ("addpd", "add", 1), ("subps", "sub", 0), ("subpd", "sub", 1),
          ("mulps", "mul", 0), ("mulpd", "mul", 1), ("divps", "div", 0), ("divpd", "div", 1),
          ("minps", "min", 0), ("minpd", "min", 1), ("maxps", "max", 0), ("maxpd", "max", 1),
          ("sqrtps", "sqrt", 0), ("sqrtpd", "sqrt", 1)]
HW_MXCSR = [0x1F80, 0x3F80, 0x5F80, 0x7F80, 0x1FC0, 0x9F80, 0x9FC0, 0x1F80 & ~0x80]


def model_elem(op, a, b, f, mxcsr, rc=None):
    if op in ("add", "sub", "mul"):
        return fp_binop(op, a, b, f, mxcsr, rc)
    if op == "div":
        return fp_div(a, b, f, mxcsr, rc)
    if op == "sqrt":
        return fp_sqrt(b, f, mxcsr, rc)
    return fp_minmax(op, a, b, f, mxcsr)


def model_vec(op, w, av, bv, mxcsr, kmask=None, rc=None):
    """(results or None per lane, flags, unmasked) of one packed operation"""
    f = F64 if w else F32
    res, flags = [], 0
    for j, (x, y) in enumerate(zip(av, bv)):
        if kmask is not None and not (kmask >> j) & 1:
            res.append(None)
            continue
        r, fl = model_elem(op, x, y, f, mxcsr, rc)
        res.append(r)
        flags |= fl
    if rc is not None:
        return res, 0, 0
    unmasked = flags & ~(mxcsr >> 7) & 0x3F
    if unmasked & (IE | DE | ZE):
        flags &= ~(OE | UE | PE)
    return res, flags, unmasked


def hw_values(f):
    sp = SPECIAL64 if f is F64 else SPECIAL32
    vals = list(sp)
    for _ in range(24):
        vals.append(rnd_fp(f))
    if f is F32:
        vals += [0x00400001, 0x80000003, 0x00800001, 0x7F7FFFFE, 0x40000000, 0x3F7FFFFE,
                 0x00FFFFFF, 0x3F800001, 0xC0490FDB]
    else:
        vals += [0x0008000000000001, 0x8000000000000003, 0x0010000000000001, 0x7FEFFFFFFFFFFFFE,
                 0x4000000000000000, 0x3FEFFFFFFFFFFFFE, 0x001FFFFFFFFFFFFF, 0x3FF0000000000001]
    return vals


def hwcheck_gen(out, expect_path):
    import json
    exp = []
    for name, op, w in HW_OPS:
        f = F64 if w else F32
        esz = 8 if w else 4
        n = 16 // esz
        vals = hw_values(f)
        sp = SPECIAL64 if w else SPECIAL32
        # every ordered pair of the special values, then mixed / random lanes
        pairs = [(x, y) for x in sp for y in sp]
        for mx in HW_MXCSR:
            for t in range(len(pairs) // n + len(vals) // n + 2):
                if t < len(pairs) // n:
                    av = [pairs[t * n + j][0] for j in range(n)]
                    bv = [pairs[t * n + j][1] for j in range(n)]
                else:
                    t2 = t - len(pairs) // n
                    av = [vals[(t2 * n + j) % len(vals)] for j in range(n)]
                    bv = [vals[(t2 * n * 7 + j * 3 + 5) % len(vals)] for j in range(n)]
                    if t2 % 3 == 1:
                        bv = [rnd_fp(f) for _ in range(n)]
                res, flags, unm = model_vec(op, w, av, bv, mx)
                xa, xb = pack(av, esz), pack(bv, esz)
                out.write("%s xmm0, xmm1 | xmm0=%s xmm1=%s mxcsr=0x%X\n" % (name, hexs(xa), hexs(xb), mx))
                if unm:
                    exp.append({"fault": 19, "mxcsr": mx | flags, "xmm0": hexs(xa)})
                else:
                    exp.append({"fault": -1, "mxcsr": mx | flags, "xmm0": hexs(pack(res, esz))})
    json.dump(exp, open(expect_path, "w"))


def hwcheck_cmp(log_path, expect_path):
    import json
    import re
    exp = json.load(open(expect_path))
    cur, bad, seen = None, 0, 0
    inp = {}
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] \S+ (\S+) xmm0, xmm1 \| xmm0=(\S+) xmm1=(\S+) mxcsr=(\S+)", line)
        if m:
            cur = int(m.group(1))
            inp = {"xmm0": m.group(3), "mxcsr": int(m.group(5), 16), "op": m.group(2),
                   "xmm1": m.group(4)}
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if m and cur is not None:
            fields = m.group(1)
            fault = -1
            fm = re.match(r"fault #(\d+)\s*(.*)$", fields)
            if fm:
                fault, fields = int(fm.group(1)), fm.group(2)
            kv = dict(x.split("=", 1) for x in fields.split() if "=" in x)
            got_x = kv.get("xmm0", inp["xmm0"])
            got_m = int(kv["mxcsr"], 16) if "mxcsr" in kv else inp["mxcsr"]
            e = exp[cur]
            seen += 1
            if got_x.upper() != e["xmm0"].upper() or got_m != e["mxcsr"] or fault != e["fault"]:
                bad += 1
                if bad <= 20:
                    print("[%d] %s a=%s b=%s mxcsr=%X: hw xmm0=%s mxcsr=%X fault=%d | model xmm0=%s mxcsr=%X fault=%d"
                          % (cur, inp["op"], inp["xmm0"], inp["xmm1"], inp["mxcsr"], got_x, got_m, fault,
                             e["xmm0"], e["mxcsr"], e["fault"]))
            cur = None
    print("hwcheck: %d cases compared, %d differ from the model" % (seen, bad))
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
        gen_mov()
        gen_int()
        gen_fp()
        gen_pcmp()
        gen_bcst()
        gen_disp8()
        gen_fault_suppression()
        gen_ud()
        gen_ext()
        out = sys.stdout
        out.write("# EVEX milestone M1 (ledger U141-U153): expected values from the independent SDM model\n")
        out.write("# Emulator/tools/isa/ref_evex_m1.py --cases (regenerate, do not edit). The i5-13600K has no\n")
        out.write("# AVX-512: expected-value cases only, run with AVX-512 enabled:\n")
        out.write("#   emu-alltest --cases Emulator\\data\\cases_evex_m1.txt --avx512 --expect-only\n")
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
