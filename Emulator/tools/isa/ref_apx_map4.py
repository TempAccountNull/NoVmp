#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_apx_map4.py -- independent reference model (Python 3 stdlib only) of Intel APX parts 2 and 3
(ledger U640-U689): EVEX map 4 (the promoted legacy instructions with NDD, NF and ZU), CCMPscc /
CTESTscc, CMOVcc / CFCMOVcc, PUSH2 / POP2, JMPABS, the promoted legacy map 2/3 instructions and
the promoted VEX instructions; generator of Emulator/data/cases_apx_map4.txt (expected values)
and Emulator/data/cases_apx_map4_hw.txt (hardware pairs).

Written from the Intel APX architecture specification 355828-009 (rev 9.0, August 2026) and the
Intel SDM text, not from any C implementation (the operand/flag semantics of the legacy forms come
from ref_apx_core.py, the part 1 model, and the SDM Operation sections):

  EVEX map 4 (3.1.2.3.1, Figure 3.3)  62H, P0 = ~R3 ~X3 ~B3 ~R4 B4 1 0 0, P1 = W ~V3..~V0 U p p
              (X4 = ~U, defined for ModRM.mod != 11b only; with mod = 11b U must be 1), P2 = 0 0 0
              ND ~V4 NF 0 0. The legacy opcodes of maps 0/1 keep their byte (map 1 SHLD/SHRD imm
              A4/AC -> 24/2C, POPCNT B8 -> 88, TZCNT BC -> F4, LZCNT BD -> F5, CMOVcc 40+cc,
              SETcc 90+cc -> F2 40+cc); pp = 66 is the OSIZE override (W wins); byte forms need
              pp = NP and ignore W; byte register ids 4-7 are SPL..DIL; disp8 is not scaled (N=1).
  NDD         (3.1.2.2, 3.1.2.4) ND = 1: V4..V0 is the destination, the result is zero-extended
              to 64 bits, r/m (memory or register) is only read. ND = 0: V must be 0.
  ZU          (3.1.2.3.1 2.(b)) IMUL 69/6B and SETcc: ND = 1 zeroes bits 63:OSIZE of the
              destination register (SETcc mem: one byte). V must be 0.
  NF          (3.1.2.3.1 2.(c)) NF = 1: the status flags are not updated (INC DEC NEG ADD SUB AND
              OR XOR SAL SAR SHL SHR ROL ROR SHLD SHRD IMUL IDIV MUL DIV LZCNT TZCNT POPCNT).
  #UD         (Table 4.12, APX-EVEX-INT) P2 bits 7..5, 1, 0 != 0; ND = 0 and V != 0; ND = 1 on an
              instruction without NDD/ZU; V != 0 on a ZU instruction; NF = 1 without NF support;
              mod = 11b and U = 0; a prefix other than 67H / segment before 62H; pp not listed;
              XCR0[19] = 0 or CR4.OSXSAVE = 0 (Table 3.8).

Usage:
  python ref_apx_map4.py --selftest     hand-derived checks of the model, exit 0 on pass
  python ref_apx_map4.py --cases        Emulator/data/cases_apx_map4.txt (stdout)
  python ref_apx_map4.py --hwgen        Emulator/data/cases_apx_map4_hw.txt (stdout): pairs
                                        "<legacy bytes> ~~ <EVEX bytes>" with registers R0-R15
                                        only: the i5-13600K runs the legacy encoding (for NDD a
                                        MOV + op + MOVZX sequence, for NF a PUSHFQ ... POPFQ
                                        frame), Unicorn (--apx) the EVEX one
  python ref_apx_map4.py --hwcmp LOG    the host results of the hardware file against the model
"""
import os
import random
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref_apx_core as core                                       # noqa: E402
from ref_apx_core import (St, Reg, Mem, Fault, Enc, Ins, Case, mask, sx, szp, alu_core,  # noqa: E402
                          cond, set_flags, rd_op, wr_op, dotbyte, fmt_inputs, fmt_changes,
                          mem_at, mk_state, CF, PF, AF, ZF, SF, OF, STATUS, M64, MEM, MEM_PTR,
                          NAMES, ALU)

GP_OK = [r for r in range(32) if r != 4]
EGPR = list(range(16, 32))
LOW = [r for r in range(16) if r != 4]


# --------------------------------------------------------------------------------------------
# encoders
# --------------------------------------------------------------------------------------------
def modrm_parts(reg, rm):
    """-> (r, x, b, ModRM/SIB/disp bytes, mod3) with 5-bit register ids (ref_apx_core.Enc)"""
    r, x, b, tail = Enc(0, reg=reg, rm=rm)._modrm()
    return r, x, b, tail, isinstance(rm, Reg)


class Ev:
    """One EVEX-encoded instruction (APX extended EVEX prefix, Figure 3.2/3.3). opc: the opcode
    byte in map mapid; reg: ModRM.reg (register id 0-31 or opcode extension); rm: Reg/Mem;
    v: the V register id (5 bits) or, with dfv/scc, the CCMP/CTEST payload. leg: the legacy
    bytes of an equivalent instruction (sequence) for the hardware pairs, or None."""

    def __init__(self, opc, reg=0, rm=None, w=0, pp=0, nd=0, nf=0, v=0, imm=b"", mapid=4,
                 scc=None, dfv=0, p2or=0, ubit=None, ll=0, leg=None):
        self.opc, self.reg, self.rm, self.w, self.pp, self.nd, self.nf, self.v = opc, reg, rm, w, pp, nd, nf, v
        self.imm, self.mapid, self.scc, self.dfv, self.p2or, self.ubit, self.ll = imm, mapid, scc, dfv, p2or, ubit, ll
        self.leg = leg

    def rex2(self):        # the APX encoding (name shared with ref_apx_core.Ins)
        r, x, b, tail, mod3 = modrm_parts(self.reg, self.rm)
        p0 = (((~r >> 3) & 1) << 7) | (((~x >> 3) & 1) << 6) | (((~b >> 3) & 1) << 5) | \
             (((~r >> 4) & 1) << 4) | (((b >> 4) & 1) << 3) | self.mapid
        u = 1 if mod3 else ((~x >> 4) & 1)
        if self.ubit is not None:
            u = self.ubit
        if self.scc is not None:
            p1 = (self.w << 7) | ((self.dfv & 15) << 3) | (u << 2) | self.pp
            p2 = self.scc & 15
        else:
            p1 = (self.w << 7) | (((~self.v) & 15) << 3) | (u << 2) | self.pp
            p2 = (self.ll << 5) | (self.nd << 4) | (((~self.v >> 4) & 1) << 3) | (self.nf << 2)
        p2 |= self.p2or
        return bytes([0x62, p0, p1, p2, self.opc]) + tail + self.imm

    def legacy(self):
        return self.leg


class Raw:
    """fixed bytes (APX side) with an optional legacy equivalent"""

    def __init__(self, b, leg=None):
        self.b, self.leg = b, leg

    def rex2(self):
        return self.b

    def legacy(self):
        return self.leg


def leg_bytes(*encs):
    """legacy byte sequence of ref_apx_core.Enc objects / bytes; None if an id needs REX2"""
    out = b""
    for e in encs:
        if isinstance(e, (bytes, bytearray)):
            out += bytes(e)
            continue
        lb = e.legacy()
        if lb is None:
            return None
        out += lb
    return out


PUSHFQ, POPFQ = b"\x9c", b"\x9d"


def mov64(dst, src):
    return Enc(0x89, reg=src, rm=Reg(dst), w=1)


def mov_n(n, dst, src_op):
    """mov dst(n), r/m(n): 8A/8B"""
    return Enc(0x8A if n == 8 else 0x8B, pfx=[0x66] if n == 16 else [], reg=dst, rm=src_op,
               w=1 if n == 64 else 0, b8="reg,rm" if n == 8 else "")


def zext(n, r):
    """zero-extend register r from n bits to 64 (movzx r32, r8/r16; n=32: mov r32, r32)"""
    if n == 8:
        return Enc(0xB6, map1=True, reg=r, rm=Reg(r), b8="rm")
    if n == 16:
        return Enc(0xB7, map1=True, reg=r, rm=Reg(r))
    if n == 32:
        return Enc(0x89, reg=r, rm=Reg(r))
    return b""


def imm_bytes(v, n):
    return (v & mask(n)).to_bytes(n // 8, "little")


def wpp(n):
    """EVEX.W and pp of a scalable form: OSIZE 16 = pp 66, 64 = W1 (byte: NP, W ignored)"""
    return (1 if n == 64 else 0), (1 if n == 16 else 0)


def nd_write(st, ndd, n, v):
    st.regs[ndd] = v & mask(n)      # NDD: OSIZE result, bits 63:OSIZE zeroed (3.1.2.4)


def frame(nf, *encs):
    """legacy equivalent: NF = 1 -> PUSHFQ ... POPFQ (the status flags are restored)"""
    body = leg_bytes(*encs)
    if body is None:
        return None
    return (PUSHFQ + body + POPFQ) if nf else body


# --------------------------------------------------------------------------------------------
# EVEX map 4: promoted legacy map 0/1 instructions (3.1.2.3.1, the table of 3.1.5)
# --------------------------------------------------------------------------------------------
def m4_alu(name, n, form, dst, src, imm=None, nd=0, nf=0, ndd=0):
    """ADD OR ADC SBB AND SUB XOR: form 'EG' (op r/m, r: 00+8k / 01+8k), 'GE' (op r, r/m: 02/03),
    'EI' (80/81 /k), 'EI8' (83 /k ib); nd: NDD ndd := dst op src"""
    k = ALU.index(name)
    w, pp = wpp(n)
    if n == 8:
        w = 0
    if form == "EG":
        opc, reg, rm, ib = k * 8 + (0 if n == 8 else 1), src.r, dst, b""
    elif form == "GE":
        opc, reg, rm, ib = k * 8 + (2 if n == 8 else 3), dst.r, src, b""
    elif form == "EI8":
        opc, reg, rm, ib = 0x83, k, dst, imm_bytes(imm, 8)
    else:
        opc, reg, rm, ib = (0x80 if n == 8 else 0x81), k, dst, imm_bytes(imm, min(n, 32))
    # legacy equivalent
    lpfx = [0x66] if n == 16 else []
    b8 = "reg,rm" if n == 8 else ""
    if nd:
        a_op = dst
        if form == "GE":
            legop = Enc(opc, pfx=lpfx, reg=ndd, rm=src, w=w, b8=b8)
            seq = [mov64(ndd, dst.r), legop, zext(n, ndd)] if n < 64 else [mov64(ndd, dst.r), legop]
        else:
            first = mov64(ndd, a_op.r) if isinstance(a_op, Reg) else mov_n(n, ndd, a_op)
            if form == "EG":
                legop = Enc(opc, pfx=lpfx, reg=src.r, rm=Reg(ndd), w=w, b8=b8)
            else:
                legop = Enc(opc, pfx=lpfx, reg=reg, rm=Reg(ndd), w=w, imm=ib, b8=b8)
            seq = [first, legop] + ([zext(n, ndd)] if n < 64 else [])
        leg = frame(nf, *seq)
    else:
        leg = frame(nf, Enc(opc, pfx=lpfx, reg=reg, rm=rm, w=w, imm=ib, b8=b8))
    e = Ev(opc, reg, rm, w=w, pp=pp, nd=nd, nf=nf, v=ndd if nd else 0, imm=ib, leg=leg)

    def sem(st):
        a = rd_op(st, dst, n)
        if form in ("EG", "GE"):
            b = rd_op(st, src, n)
        elif form == "EI8":
            b = sx(imm, 8) & mask(n)
        else:
            b = (sx(imm, 32) if n == 64 else imm) & mask(n)
        res, f, _ = alu_core(name, a, b, n, st.rflags & CF)
        if nd:
            nd_write(st, ndd, n, res)
        else:
            wr_op(st, dst, n, res)
        if not nf:
            set_flags(st, f, STATUS)
    u = AF if (name in ("and", "or", "xor") and not nf) else 0
    return Ins(e, sem, u)


def m4_unary(name, n, op, nd=0, nf=0, ndd=0):
    """INC DEC (FE/FF /0 /1), NOT NEG (F6/F7 /2 /3)"""
    w, pp = wpp(n)
    if n == 8:
        w = 0
    ext = {"inc": 0, "dec": 1, "not": 2, "neg": 3}[name]
    opc = (0xFE if n == 8 else 0xFF) if name in ("inc", "dec") else (0xF6 if n == 8 else 0xF7)
    lpfx = [0x66] if n == 16 else []
    b8 = "reg,rm" if n == 8 else ""
    if nd:
        first = mov64(ndd, op.r) if isinstance(op, Reg) else mov_n(n, ndd, op)
        leg = frame(nf, first, Enc(opc, pfx=lpfx, reg=ext, rm=Reg(ndd), w=w, b8=b8), zext(n, ndd))
    else:
        leg = frame(nf, Enc(opc, pfx=lpfx, reg=ext, rm=op, w=w, b8=b8))
    e = Ev(opc, ext, op, w=w, pp=pp, nd=nd, nf=nf, v=ndd if nd else 0, leg=leg)

    def sem(st):
        a = rd_op(st, op, n)
        if name == "not":
            res, f, fm = ~a, 0, 0
        elif name == "neg":
            res, f, _ = alu_core("sub", 0, a, n, 0)
            fm = STATUS
        else:
            res, f, _ = alu_core("add" if name == "inc" else "sub", a, 1, n, 0)
            fm = STATUS & ~CF                       # INC/DEC: CF unaffected
        if nd:
            nd_write(st, ndd, n, res)
        else:
            wr_op(st, op, n, res)
        if not nf:
            set_flags(st, f, fm)
    return Ins(e, sem)


SHIFT_EXT = {"rol": 0, "ror": 1, "rcl": 2, "rcr": 3, "shl": 4, "shr": 5, "sal": 6, "sar": 7}


def shift_core(name, n, v, c_raw, cf_in):
    """SDM SAL/SAR/SHL/SHR and RCL/RCR/ROL/ROR Operation: -> (result, flags, written mask,
    undefined mask); c_raw = the count operand (imm8 / CL / 1)"""
    cm = 63 if n == 64 else 31
    c = c_raw & cm
    msb = lambda x: (x >> (n - 1)) & 1
    if c == 0:
        return v & mask(n), 0, 0, 0                  # no flags affected, operand unchanged
    if name in ("shl", "sal", "shr", "sar"):
        if name in ("shl", "sal"):
            res = (v << c) & mask(n)
            cf = (v >> (n - c)) & 1 if c <= n else 0
            of = msb(res) ^ cf
        elif name == "shr":
            res = (v >> c) & mask(n)
            cf = (v >> (c - 1)) & 1
            of = msb(v)
        else:
            res = (sx(v, n) >> c) & mask(n)
            cf = (sx(v, n) >> (c - 1)) & 1
            of = 0
        f = szp(res, n) | (CF if cf else 0) | (OF if of else 0)
        und = AF | (0 if c == 1 else OF)
        # "it is undefined for SHL and SHR instructions where the count is greater than or
        # equal to the size (in bits) of the destination operand"
        if c >= n and name in ("shl", "sal", "shr"):
            und |= CF
        return res, f, STATUS, und
    if name in ("rol", "ror"):
        t = c % n
        if name == "rol":
            res = ((v << t) | (v >> (n - t))) & mask(n) if t else v & mask(n)
            cf = res & 1
            of = msb(res) ^ cf
        else:
            res = ((v >> t) | (v << (n - t))) & mask(n) if t else v & mask(n)
            cf = msb(res)
            of = msb(res) ^ ((res >> (n - 2)) & 1)
        return res, (CF if cf else 0) | (OF if of else 0), CF | OF, (0 if c == 1 else OF)
    # RCL / RCR: tempCOUNT = (COUNT & 1FH) MOD 9 / 17 for 8 / 16 bits
    t = c % (n + 1) if n in (8, 16) else c
    d, cf = v & mask(n), cf_in
    of = 0
    if name == "rcr" and c == 1:
        of = msb(d) ^ cf
    for _ in range(t):
        if name == "rcl":
            tcf = msb(d)
            d = ((d << 1) | cf) & mask(n)
            cf = tcf
        else:
            tcf = d & 1
            d = (d >> 1) | (cf << (n - 1))
            cf = tcf
    if name == "rcl" and c == 1:
        of = msb(d) ^ cf
    return d, (CF if cf else 0) | (OF if of else 0), CF | OF, (0 if c == 1 else OF)


def m4_shift(name, n, op, how, cnt=1, nd=0, nf=0, ndd=0):
    """C0/C1 /e ib, D0/D1 /e (1), D2/D3 /e (CL); name rol ror rcl rcr shl shr sal(/6) sar"""
    w, pp = wpp(n)
    if n == 8:
        w = 0
    ext = SHIFT_EXT[name]
    if how == "1":
        opc, ib = (0xD0 if n == 8 else 0xD1), b""
    elif how == "ib":
        opc, ib = (0xC0 if n == 8 else 0xC1), bytes([cnt & 0xFF])
    else:
        opc, ib = (0xD2 if n == 8 else 0xD3), b""
    lpfx = [0x66] if n == 16 else []
    b8 = "reg,rm" if n == 8 else ""
    if nd:
        first = mov64(ndd, op.r) if isinstance(op, Reg) else mov_n(n, ndd, op)
        leg = frame(nf, first, Enc(opc, pfx=lpfx, reg=ext, rm=Reg(ndd), w=w, imm=ib, b8=b8), zext(n, ndd))
    else:
        leg = frame(nf, Enc(opc, pfx=lpfx, reg=ext, rm=op, w=w, imm=ib, b8=b8))
    e = Ev(opc, ext, op, w=w, pp=pp, nd=nd, nf=nf, v=ndd if nd else 0, imm=ib, leg=leg)
    nm = "shl" if name == "sal" else name

    def count(st):
        return 1 if how == "1" else (cnt if how == "ib" else st.regs[1] & 0xFF)

    def sem(st):
        v = rd_op(st, op, n)
        res, f, fm, _ = shift_core(nm, n, v, count(st), st.rflags & CF)
        if nd:
            nd_write(st, ndd, n, res)
        else:
            wr_op(st, op, n, res)
        if not nf:
            set_flags(st, f, fm)
    ins = Ins(e, sem)
    ins.undef_fn = lambda st: 0 if nf else shift_core(nm, n, rd_op(st, op, n), count(st), st.rflags & CF)[3]
    return ins


def m4_shxd(name, n, dst, src, how, cnt=None, nd=0, nf=0, ndd=0):
    """SHLD/SHRD r/m, r, ib|CL: map 4 24/2C ib (map 1 A4/AC), A5/AD (CL); n = 16/32/64"""
    w, pp = wpp(n)
    opc = {("shld", "ib"): 0x24, ("shld", "cl"): 0xA5, ("shrd", "ib"): 0x2C, ("shrd", "cl"): 0xAD}[(name, how)]
    lopc = {0x24: 0xA4, 0x2C: 0xAC}.get(opc, opc)
    ib = bytes([cnt & 0xFF]) if how == "ib" else b""
    lpfx = [0x66] if n == 16 else []
    if nd:
        first = mov64(ndd, dst.r) if isinstance(dst, Reg) else mov_n(n, ndd, dst)
        leg = frame(nf, first, Enc(lopc, map1=True, pfx=lpfx, reg=src.r, rm=Reg(ndd), w=w, imm=ib), zext(n, ndd))
    else:
        leg = frame(nf, Enc(lopc, map1=True, pfx=lpfx, reg=src.r, rm=dst, w=w, imm=ib))
    e = Ev(opc, src.r, dst, w=w, pp=pp, nd=nd, nf=nf, v=ndd if nd else 0, imm=ib, leg=leg)

    def count(st):
        return (cnt if how == "ib" else st.regs[1] & 0xFF) & (63 if n == 64 else 31)

    def sem(st):
        c = count(st)
        d, s = rd_op(st, dst, n), st.getr(src.r, n)
        if c == 0:
            res, f = d, None
        else:
            if name == "shld":
                res = ((d << c) | (s >> (n - c))) & mask(n)
                cf = (d >> (n - c)) & 1
            else:
                res = ((d >> c) | (s << (n - c))) & mask(n)
                cf = (d >> (c - 1)) & 1
            of = ((res ^ d) >> (n - 1)) & 1
            f = szp(res, n) | (CF if cf else 0) | (OF if of else 0)
        # "DEST := tempDEST" also for COUNT = 0 (a 32-bit destination is zero-extended)
        if nd:
            nd_write(st, ndd, n, res)
        else:
            wr_op(st, dst, n, res)
        if f is not None and not nf:
            set_flags(st, f, STATUS)
    ins = Ins(e, sem)
    ins.undef_fn = lambda st: 0 if (nf or count(st) == 0) else (AF | (0 if count(st) == 1 else OF))
    return ins


def m4_imul2(n, dst, src, nd=0, nf=0, ndd=0):
    """IMUL r, r/m (map 4 AF; ND: IMUL ndd, r, r/m)"""
    w, pp = wpp(n)
    lpfx = [0x66] if n == 16 else []
    if nd:
        leg = frame(nf, mov64(ndd, dst.r), Enc(0xAF, map1=True, pfx=lpfx, reg=ndd, rm=src, w=w), zext(n, ndd))
    else:
        leg = frame(nf, Enc(0xAF, map1=True, pfx=lpfx, reg=dst.r, rm=src, w=w))
    e = Ev(0xAF, dst.r, src, w=w, pp=pp, nd=nd, nf=nf, v=ndd if nd else 0, leg=leg)

    def sem(st):
        a, b = sx(st.getr(dst.r, n), n), sx(rd_op(st, src, n), n)
        p = a * b
        res = p & mask(n)
        if nd:
            nd_write(st, ndd, n, res)
        else:
            st.setr(dst.r, n, res)
        if not nf:
            set_flags(st, (CF | OF) if sx(res, n) != p else 0, CF | OF)
    return Ins(e, sem, 0 if nf else (SF | ZF | AF | PF))


def m4_imul3(n, dst, src, imm, short, zu=0, nf=0):
    """IMUL r, r/m, iz / ib (69 / 6B); ZU (ND = 1): bits 63:OSIZE of r zeroed"""
    w, pp = wpp(n)
    lpfx = [0x66] if n == 16 else []
    ib = imm_bytes(imm, 8 if short else min(n, 32))
    opc = 0x6B if short else 0x69
    if zu:
        leg = frame(nf, Enc(opc, pfx=lpfx, reg=dst.r, rm=src, w=w, imm=ib), zext(n, dst.r))
    else:
        leg = frame(nf, Enc(opc, pfx=lpfx, reg=dst.r, rm=src, w=w, imm=ib))
    e = Ev(opc, dst.r, src, w=w, pp=pp, nd=zu, nf=nf, imm=ib, leg=leg)

    def sem(st):
        a = sx(rd_op(st, src, n), n)
        b = sx(imm, 8 if short else min(n, 32))
        p = a * b
        res = p & mask(n)
        if zu:
            st.regs[dst.r] = res
        else:
            st.setr(dst.r, n, res)
        if not nf:
            set_flags(st, (CF | OF) if sx(res, n) != p else 0, CF | OF)
    return Ins(e, sem, 0 if nf else (SF | ZF | AF | PF))


def m4_muldiv(name, n, op, nf=0):
    """F6/F7 /4 MUL, /5 IMUL, /6 DIV, /7 IDIV (n = 8..64); NF"""
    w, pp = wpp(n)
    if n == 8:
        w = 0
    ext = {"mul": 4, "imul": 5, "div": 6, "idiv": 7}[name]
    opc = 0xF6 if n == 8 else 0xF7
    lpfx = [0x66] if n == 16 else []
    leg = frame(nf, Enc(opc, pfx=lpfx, reg=ext, rm=op, w=w, b8="reg,rm" if n == 8 else ""))
    e = Ev(opc, ext, op, w=w, pp=pp, nf=nf, leg=leg)

    def sem(st):
        s = rd_op(st, op, n)
        if n == 8:
            lo, hi = st.regs[0] & 0xFF, (st.regs[0] >> 8) & 0xFF
        else:
            lo, hi = st.getr(0, n), st.getr(2, n)
        if name in ("mul", "imul"):
            if name == "mul":
                p = lo * s
                over = (p >> n) != 0
            else:
                p = sx(lo, n) * sx(s, n)
                over = sx(p & mask(n), n) != p
            if n == 8:
                st.setr(0, 16, p)
            else:
                st.setr(0, n, p)
                st.setr(2, n, p >> n)
            if not nf:
                set_flags(st, (CF | OF) if over else 0, CF | OF)
            return
        dd = hi << n | lo
        if s == 0:
            raise Fault(0)
        if name == "div":
            q, r = divmod(dd, s)
            if q >> n:
                raise Fault(0)
        else:
            a, b = sx(dd, 2 * n), sx(s, n)
            q = abs(a) // abs(b)
            if (a < 0) != (b < 0):
                q = -q
            r = a - q * b
            if q < -(1 << (n - 1)) or q >= (1 << (n - 1)):
                raise Fault(0)
        if n == 8:
            st.setr(0, 16, (q & 0xFF) | ((r & 0xFF) << 8))
        else:
            st.setr(0, n, q)
            st.setr(2, n, r)
    und = 0 if nf else ((SF | ZF | AF | PF) if name in ("mul", "imul") else STATUS)
    return Ins(e, sem, und)


def m4_bitcount(name, n, dst, src, nf=0):
    """POPCNT (map 4 88), TZCNT (F4), LZCNT (F5): rv, rv/mv, NP/66; NF"""
    w, pp = wpp(n)
    opc = {"popcnt": 0x88, "tzcnt": 0xF4, "lzcnt": 0xF5}[name]
    lopc = {"popcnt": 0xB8, "tzcnt": 0xBC, "lzcnt": 0xBD}[name]
    leg = frame(nf, Enc(lopc, map1=True, pfx=([0x66] if n == 16 else []) + [0xF3], reg=dst.r, rm=src, w=w))
    e = Ev(opc, dst.r, src, w=w, pp=pp, nf=nf, leg=leg)

    def sem(st):
        v = rd_op(st, src, n)
        if name == "popcnt":
            res = bin(v).count("1")
            f, fm = (ZF if v == 0 else 0), STATUS
        elif name == "lzcnt":
            res = n - v.bit_length()
            f, fm = (CF if v == 0 else 0) | (ZF if res == 0 else 0), CF | ZF
        else:
            res = n if v == 0 else (v & -v).bit_length() - 1
            f, fm = (CF if v == 0 else 0) | (ZF if res == 0 else 0), CF | ZF
        st.setr(dst.r, n, res)
        if not nf:
            set_flags(st, f, fm)
    und = 0 if (nf or name == "popcnt") else (OF | SF | PF | AF)
    return Ins(e, sem, und)


def m4_setcc(cc, op, zu=0, w=0):
    """SETcc r/m8: map 4 F2 40+cc (map 1 90+cc); ZU (ND = 1): a register gets 0 / 1 in 63:0"""
    if zu and isinstance(op, Reg):
        leg = leg_bytes(Enc(0x90 + cc, map1=True, reg=0, rm=op, b8="rm"), zext(8, op.r))
    else:
        leg = leg_bytes(Enc(0x90 + cc, map1=True, reg=0, rm=op, b8="rm"))
    e = Ev(0x40 + cc, 0, op, w=w, pp=3, nd=zu, leg=leg)

    def sem(st):
        v = 1 if cond(cc, st.rflags) else 0
        if zu and isinstance(op, Reg):
            st.regs[op.r] = v
        else:
            wr_op(st, op, 8, v)
    return Ins(e, sem)


# --------------------------------------------------------------------------------------------
# case text helpers
# --------------------------------------------------------------------------------------------
class MCase(Case):
    """ref_apx_core.Case with state-dependent undefined flags (Ins.undef_fn)"""

    def __init__(self, ins, st, regs_set=(), mem_runs=(), title=None):
        Case.__init__(self, ins, st, regs_set, mem_runs)
        if hasattr(ins, "undef_fn"):
            ins.undef = ins.undef_fn(st)


def fresh(st, runs):
    """the input memory runs re-read from the state (after a generator changed bytes there)"""
    return [(o, bytes(st.mem[o:o + len(dt)])) for o, dt in runs]


def line_ud(b, inp=""):
    return "%s%s => #UD" % (dotbyte(b), (" | " + inp) if inp else "")


# --------------------------------------------------------------------------------------------
# generators
# --------------------------------------------------------------------------------------------
SIZES = [8, 16, 32, 64]


def gen_items(rng, egpr, hw=False):
    """yield MCase objects; egpr: at least one R16-R31 per instruction; hw: registers 0-15 only
    and NDD destinations that do not alias the other source (legacy MOV + op equivalent)"""
    pool = GP_OK if egpr else LOW

    def regs(k, excl=()):
        pl = [r for r in pool if r not in excl]
        if not egpr:
            return rng.sample(pl, k)
        e = rng.choice(EGPR)
        rest = rng.sample([r for r in pl if r != e], k - 1)
        v = [e] + rest
        rng.shuffle(v)
        return v

    def operands(n, memf, nbytes=8, excl=()):
        """-> (st, a(reg id), b(reg id), mem or None, used regs, runs)"""
        a, b, base, idx = regs(4, excl)
        st = mk_state(rng, [a, b])
        if memf:
            m, run = mem_at(rng, st, base, idx if rng.random() < 0.6 else None, rng.choice([1, 2, 4, 8]), nbytes)
            used = [a, b, base] + ([m.index] if m.index is not None else [])
            return st, a, b, m, used, [run]
        return st, a, b, None, [a, b], []

    def pick_ndd(avoid, egpr_ok=True):
        cands = [r for r in pool if r not in avoid]
        return rng.choice(cands)

    nd_nf = [(0, 0), (1, 0), (0, 1), (1, 1)]
    # ---- ALU ----
    for name in ALU[:7]:
        for n in SIZES:
            for form in ("EG", "GE", "EI", "EI8"):
                if form == "EI8" and n == 8:
                    continue
                for memf in (False, True):
                    for nd, nf in nd_nf:
                        if nf and name in ("adc", "sbb"):
                            continue
                        st, a, b, m, used, runs = operands(n, memf)
                        imm = rng.getrandbits(8 if form == "EI8" else min(n, 32))
                        if form == "GE":
                            dst, src = Reg(a), (m if memf else Reg(b))
                        else:
                            dst, src = (m if memf else Reg(a)), Reg(b)
                        avoid = [src.r] if isinstance(src, Reg) and form != "GE" else []
                        if form == "GE" and isinstance(src, Reg):
                            avoid = [src.r]
                        if memf:
                            avoid += [m.base] + ([m.index] if m.index is not None else [])
                        ndd = pick_ndd(avoid) if nd else 0
                        if hw and nd and form in ("EI", "EI8") and isinstance(dst, Reg):
                            ndd = rng.choice([dst.r, ndd])
                        if nd:
                            st.regs[ndd] = rng.getrandbits(64)
                            used = used + [ndd]
                        yield MCase(m4_alu(name, n, form, dst, src, imm, nd, nf, ndd), st, used, runs)
    # ---- INC DEC NOT NEG ----
    for name in ("inc", "dec", "not", "neg"):
        for n in SIZES:
            for memf in (False, True):
                for nd, nf in nd_nf:
                    if nf and name == "not":
                        continue
                    st, a, b, m, used, runs = operands(n, memf)
                    op = m if memf else Reg(a)
                    avoid = ([m.base] + ([m.index] if m.index is not None else [])) if memf else []
                    ndd = pick_ndd(avoid) if nd else 0
                    if nd:
                        st.regs[ndd] = rng.getrandbits(64)
                        used = used + [ndd]
                    yield MCase(m4_unary(name, n, op, nd, nf, ndd), st, used, runs)
    # ---- shifts and rotates ----
    for name in ("rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"):
        for n in SIZES:
            for how in ("1", "ib", "cl"):
                for memf in (False, True):
                    for nd, nf in nd_nf:
                        if nf and name in ("rcl", "rcr"):
                            continue
                        st, a, b, m, used, runs = operands(n, memf)
                        op = m if memf else Reg(a)
                        cnt = rng.choice([0, 1, 2, 3, n - 1, n, n + 1, rng.randrange(0, 256)])
                        if how == "cl":
                            if hw and (op == Reg(1) or (memf and 1 in (m.base, m.index))):
                                continue
                            st.regs[1] = (st.regs[1] & ~0xFF) | cnt
                            used = used + [1]
                        avoid = ([m.base] + ([m.index] if m.index is not None else [])) if memf else []
                        if how == "cl":
                            avoid.append(1)
                        ndd = pick_ndd(avoid) if nd else 0
                        if nd:
                            st.regs[ndd] = rng.getrandbits(64)
                            used = used + [ndd]
                        if how == "cl" and not memf and a == 1:
                            continue
                        yield MCase(m4_shift(name, n, op, how, cnt, nd, nf, ndd), st, used, runs)
    # ---- SHLD / SHRD ----
    for name in ("shld", "shrd"):
        for n in (16, 32, 64):
            for how in ("ib", "cl"):
                for memf in (False, True):
                    for nd, nf in nd_nf:
                        st, a, b, m, used, runs = operands(n, memf)
                        dst = m if memf else Reg(a)
                        cnt = rng.choice([0, 1, 2, n - 1, rng.randrange(0, n)] + ([n, 33] if n != 16 else []))
                        if how == "cl":
                            if a == 1 or b == 1 or (memf and 1 in (m.base, m.index)):
                                continue
                            st.regs[1] = (st.regs[1] & ~0xFF) | cnt
                            used = used + [1]
                        avoid = [b, 1] + (([m.base] + ([m.index] if m.index is not None else [])) if memf else [])
                        ndd = pick_ndd(avoid) if nd else 0
                        if nd:
                            st.regs[ndd] = rng.getrandbits(64)
                            used = used + [ndd]
                        yield MCase(m4_shxd(name, n, dst, Reg(b), how, cnt, nd, nf, ndd), st, used, runs)
    # ---- IMUL ----
    for n in (16, 32, 64):
        for memf in (False, True):
            for nd, nf in nd_nf:
                st, a, b, m, used, runs = operands(n, memf)
                src = m if memf else Reg(b)
                avoid = [] if memf else [b]
                if memf:
                    avoid += [m.base] + ([m.index] if m.index is not None else [])
                ndd = pick_ndd(avoid) if nd else 0
                if nd:
                    st.regs[ndd] = rng.getrandbits(64)
                    used = used + [ndd]
                yield MCase(m4_imul2(n, Reg(a), src, nd, nf, ndd), st, used, runs)
            for short in (False, True):
                for zu, nf in nd_nf:
                    st, a, b, m, used, runs = operands(n, memf)
                    src = m if memf else Reg(b)
                    imm = rng.getrandbits(8 if short else min(n, 32))
                    yield MCase(m4_imul3(n, Reg(a), src, imm, short, zu, nf), st, used, runs)
    # ---- MUL IMUL DIV IDIV ----
    for name in ("mul", "imul", "div", "idiv"):
        for n in SIZES:
            for memf in (False, True):
                for nf in (0, 1):
                    st, a, b, m, used, runs = operands(n, memf, excl=(0, 2))
                    op = m if memf else Reg(a)
                    st.regs[0] = rng.getrandbits(64)
                    st.regs[2] = rng.getrandbits(64)
                    if name in ("div", "idiv"):
                        # a quotient in range (no #DE) most of the time
                        if n == 8:
                            st.regs[0] = (st.regs[0] & ~0xFFFF) | rng.getrandbits(8)
                        else:
                            st.regs[2] = st.regs[2] & ~mask(n) | rng.getrandbits(max(1, n // 4) - 1)
                            if name == "idiv":
                                st.regs[2] &= ~mask(n)
                        if not memf:
                            st.regs[a] |= (1 << (n - 2))
                        else:
                            off = m.ea(st) - MEM
                            st.mem[off + n // 8 - 1] = 0x40
                    used = used + [0, 2]
                    yield MCase(m4_muldiv(name, n, op, nf), st, used, fresh(st, runs))
    # ---- POPCNT LZCNT TZCNT ----
    for name in ("popcnt", "lzcnt", "tzcnt"):
        for n in (16, 32, 64):
            for memf in (False, True):
                for nf in (0, 1):
                    st, a, b, m, used, runs = operands(n, memf)
                    if rng.random() < 0.2:
                        if memf:
                            off = m.ea(st) - MEM
                            st.mem[off:off + n // 8] = bytes(n // 8)
                        else:
                            st.regs[b] = 0
                    src = m if memf else Reg(b)
                    yield MCase(m4_bitcount(name, n, Reg(a), src, nf), st, used, fresh(st, runs))
    # ---- SETcc / SETcc.ZU ----
    for cc in range(16):
        for memf in (False, True):
            for zu in (0, 1):
                st, a, b, m, used, runs = operands(8, memf)
                op = m if memf else Reg(a)
                yield MCase(m4_setcc(cc, op, zu, w=rng.getrandbits(1)), st, used, runs)


# --------------------------------------------------------------------------------------------
# hand-written lines: encodings, payload bits and every #UD rule
# --------------------------------------------------------------------------------------------
class Pre:
    """an instruction with extra prefix bytes in front of its APX encoding"""

    def __init__(self, pfx, e):
        self.pfx, self.e = pfx, e

    def rex2(self):
        return bytes(self.pfx) + self.e.rex2()

    def legacy(self):
        return None


def mk_line(ins, regs=None, mem=None, rflags=0x202, pfx=None):
    """an expected-value line of ins from an explicit input state (the model computes it)"""
    st = St()
    used = []
    for r, v in (regs or {}).items():
        st.regs[r] = v & M64
        used.append(r)
    st.rflags = rflags
    runs = []
    for off, data in (mem or {}).items():
        st.mem[off:off + len(data)] = data
        runs.append((off, data))
    if pfx:
        ins2 = Ins(Pre(pfx, ins.encs[0]), ins.sem, ins.undef)
        if hasattr(ins, "undef_fn"):
            ins2.undef_fn = ins.undef_fn
        ins = ins2
    return MCase(ins, st, used, runs).line_rex2()


def special_lines(rng):
    L = []
    c = lambda s: L.append("# " + s)
    c("--- EVEX map 4 payload: R4/R3, X4 (= ~U)/X3, B4/B3, V4..V0, byte registers 4-7 = SPL..DIL ---")
    L.append(mk_line(m4_alu("add", 64, "EG", Reg(16), Reg(17), nd=1, ndd=31), {16: 7, 17: 9, 31: -1}))
    L.append(mk_line(m4_alu("add", 16, "EG", Reg(0), Reg(1), nd=1, ndd=29), {0: 0xFFFFFFFFFFFF8000, 1: 0x8000, 29: 0x1234567812345678}))
    L.append(mk_line(m4_alu("add", 8, "EG", Reg(6), Reg(7)), {6: 0x1210, 7: 0x3405}))       # SIL, DIL
    L.append(mk_line(m4_alu("add", 8, "EG", Reg(5), Reg(7)), {5: 0x1210, 7: 0x3405}))       # BPL, DIL
    L.append(mk_line(m4_alu("add", 64, "GE", Reg(10), Mem(3, 20, 4, 0)), {10: 1, 3: MEM_PTR, 20: 0x10}, {0x8040: bytes([2, 0, 0, 0, 0, 0, 0, 0])}))
    L.append(mk_line(m4_alu("add", 64, "GE", Reg(0), Mem(3, 20, 1, 0)), {3: MEM_PTR, 20: 8}, {0x8008: bytes([5, 0, 0, 0, 0, 0, 0, 0])}))
    L.append(mk_line(m4_alu("add", 64, "GE", Reg(0), Mem(3, 28, 8, -8)), {3: MEM_PTR, 28: 2, 0: 1}, {0x8008: bytes([5, 0, 0, 0, 0, 0, 0, 0])}))
    L.append(mk_line(m4_alu("add", 64, "GE", Reg(0), Mem(21, None, 1, 8)), {21: MEM_PTR, 0: 1}, {0x8008: bytes([1, 0, 0, 0, 0, 0, 0, 0])}))   # disp8 * 1
    L.append(mk_line(m4_alu("add", 32, "GE", Reg(24), Mem(29, None, 1, 0x400)), {29: MEM_PTR, 24: 3}, {0x8400: bytes([0xFF, 0xFF, 0xFF, 0x7F])}))
    # RIP-relative with B4 = 1 and X4 = 1 (U = 0): ignored; add eax, [rip - 4] reads the disp32
    L.append("%s | rax=0x0 => rax=0xFFFFFFFC rflags=0x286" % dotbyte(bytes([0x62, 0xFC, 0x78, 0x08, 0x03, 0x05, 0xFC, 0xFF, 0xFF, 0xFF])))
    c("--- W wins over pp = 66; byte forms ignore W ---")
    L.append(mk_line(Ins(Ev(0x01, 1, Reg(0), w=1, pp=1), m4_alu("add", 64, "EG", Reg(0), Reg(1)).sem), {0: 1, 1: -1}))
    L.append(mk_line(Ins(Ev(0x00, 1, Reg(0), w=1), m4_alu("add", 8, "EG", Reg(0), Reg(1)).sem), {0: 0xFF, 1: 1}))
    c("--- NDD: zero-extended even when the NDD is a source; a memory source is not written ---")
    L.append(mk_line(m4_unary("inc", 8, Reg(0), nd=1, ndd=0), {0: 0xFFFFFFFFFFFFFF7F}))
    L.append(mk_line(m4_alu("add", 16, "EG", Mem(3, None, 1, 0), Reg(0), nd=1, ndd=2), {3: MEM_PTR, 0: 1, 2: -1}, {0x8000: bytes([0xFF, 0xFF])}))
    L.append(mk_line(m4_alu("sub", 32, "GE", Reg(2), Mem(3, None, 1, 0), nd=1, ndd=2), {3: MEM_PTR, 2: 0xFFFFFFFF00000010}, {0x8000: bytes([0x11, 0, 0, 0])}))
    L.append(mk_line(m4_alu("xor", 64, "EG", Reg(5), Reg(5), nd=1, ndd=9), {5: 0x1234, 9: -1}))      # xor r, r with NDD
    L.append(mk_line(m4_alu("xor", 32, "GE", Reg(5), Reg(5), nd=1, ndd=9), {5: 0x1234, 9: -1}))
    c("--- NF: no status flag changes (all six kept) ---")
    L.append(mk_line(m4_unary("inc", 32, Reg(0), nf=1), {0: 0x7FFFFFFF}, rflags=0x8D7))
    L.append(mk_line(m4_alu("sub", 64, "EG", Reg(0), Reg(1), nf=1), {0: 1, 1: 1}))
    L.append(mk_line(m4_alu("xor", 64, "EG", Reg(0), Reg(0), nf=1), {0: 5}, rflags=0x8D7))
    L.append(mk_line(m4_alu("xor", 64, "EG", Reg(0), Reg(0), nd=1, nf=1, ndd=9), {0: 5, 9: 1}, rflags=0x8D7))
    L.append(mk_line(m4_shift("shl", 32, Reg(0), "cl", 0, nf=1), {0: 3, 1: 0x21}, rflags=0x8D7))
    L.append(mk_line(m4_muldiv("div", 32, Reg(3), nf=1), {0: 100, 2: 0, 3: 7}, rflags=0x8D7))
    c("--- SETcc.ZU / IMUL.ZU ---")
    L.append(mk_line(m4_setcc(4, Reg(0), zu=1), {0: -1}, rflags=0x242))
    L.append(mk_line(m4_setcc(4, Reg(0), zu=0), {0: -1}))
    L.append(mk_line(m4_setcc(5, Reg(20), zu=1, w=1), {20: -1}))
    L.append(mk_line(m4_setcc(5, Mem(3, None, 1, 0), zu=1), {3: MEM_PTR}, {0x8000: bytes([0xAA, 0xBB])}))
    L.append(mk_line(m4_imul3(16, Reg(1), Reg(0), 2, True, zu=1), {0: 0xFFFFFFFFFFFF0003, 1: -1}))
    L.append(mk_line(m4_imul3(16, Reg(1), Reg(0), 2, True, zu=0), {0: 0xFFFFFFFFFFFF0003, 1: -1}))
    c("--- #UD: reserved payload bits, prefixes, mod = 11b with U = 0, V without ND, ND/NF/pp not listed ---")
    base = Ev(0x01, 1, Reg(0), w=1)
    for bit in (0x80, 0x40, 0x20, 0x02, 0x01):
        L.append(line_ud(Ev(0x01, 1, Reg(0), w=1, p2or=bit).rex2()))
    L.append(line_ud(Ev(0x01, 1, Reg(0), w=1, ubit=0).rex2()))           # mod 11b, U = 0
    for pfx in (0x66, 0xF2, 0xF3, 0xF0, 0x48, 0x40):
        L.append(line_ud(bytes([pfx]) + base.rex2()))
    for pfx in (0x67, 0x2E, 0x64):                                       # allowed before EVEX
        L.append(mk_line(m4_alu("add", 64, "EG", Reg(0), Reg(1)), {0: 1, 1: 2}, pfx=[pfx]))
    L.append(mk_line(m4_alu("add", 32, "GE", Reg(0), Mem(3, None, 1, 0, 32)), {3: MEM_PTR, 0: 1}, {0x8000: bytes([5, 0, 0, 0])}, pfx=[0x67]))
    L.append(line_ud(Ev(0x01, 1, Reg(0), w=1, v=5).rex2()))              # ND = 0, V != 0
    L.append(line_ud(Ev(0x01, 1, Reg(0), w=1, v=16).rex2()))             # ND = 0, V4 = 1
    for opc, reg, extra in ((0x10, 1, {}), (0x18, 1, {}), (0xF6, 2, {}), (0xC0, 2, {"imm": b"\x01"}), (0xD3, 3, {})):
        L.append(line_ud(Ev(opc, reg, Reg(0), nf=1, **extra).rex2()))   # NF on ADC/SBB/NOT/RCL/RCR
    for opc, reg in ((0xF7, 4), (0xF7, 5), (0xF7, 6), (0xF7, 7), (0x88, 0), (0xF4, 0), (0xF5, 0)):
        L.append(line_ud(Ev(opc, reg, Reg(1), nd=1, v=3).rex2()))       # ND on non-NDD
    L.append(line_ud(Ev(0x6B, 1, Reg(0), nd=1, v=3, imm=b"\x02").rex2()))   # ZU with V != 0
    L.append(line_ud(Ev(0x69, 1, Reg(0), v=1, imm=b"\x02\x00\x00\x00").rex2()))
    L.append(line_ud(Ev(0x44, 0, Reg(0), pp=3, v=2).rex2()))               # SETcc V != 0
    L.append(line_ud(Ev(0x44, 0, Reg(0), pp=3, nd=1, v=2).rex2()))
    L.append(line_ud(Ev(0x44, 0, Reg(0), pp=3, nf=1).rex2()))              # SETcc NF
    L.append(line_ud(Ev(0x44, 0, Reg(0), pp=2).rex2()))                    # 40+cc with F3
    L.append(line_ud(Ev(0x00, 1, Reg(0), pp=1).rex2()))                    # byte forms with 66
    L.append(line_ud(Ev(0xD0, 4, Reg(0), pp=1).rex2()))
    L.append(line_ud(Ev(0xF6, 3, Reg(0), pp=1).rex2()))
    for pp in (2, 3):
        L.append(line_ud(Ev(0x01, 1, Reg(0), pp=pp).rex2()))               # F3 / F2 on ADD
        L.append(line_ud(Ev(0xAF, 1, Reg(0), pp=pp).rex2()))
        L.append(line_ud(Ev(0x88, 1, Reg(0), pp=pp).rex2()))
    for opc, reg in ((0xFE, 2), (0xFE, 7), (0xFF, 2), (0xFF, 3), (0xFF, 4), (0xFF, 5), (0x8F, 1),
                     (0x82, 0), (0x04, 0), (0x05, 0), (0x86, 0), (0x87, 0), (0x8D, 0), (0xC6, 0),
                     (0xC7, 0), (0x0F, 0), (0x62, 0), (0x90, 0), (0xA4, 0), (0xAC, 0), (0xB8, 0),
                     (0xBC, 0), (0xBD, 0), (0xB6, 0), (0xFA, 0), (0x07, 0), (0x3C, 0), (0xA8, 0),
                     (0x92, 0), (0xA3, 0), (0xAB, 0)):
        L.append(line_ud(Ev(opc, reg, Reg(1)).rex2() + bytes(4)))
    c("--- XCR0[19] = 0 (XSETBV 7 first): every map 4 instruction #UD (Table 3.8) ---")
    L.append(line_ud(bytes([0x0F, 0x01, 0xD1]) + Ev(0x01, 1, Reg(0), w=1).rex2(), "rcx=0x0 rax=0x7 rdx=0x0"))
    L.append(line_ud(bytes([0x0F, 0x01, 0xD1]) + Ev(0x44, 0, Reg(0), pp=3).rex2(), "rcx=0x0 rax=0x7 rdx=0x0"))
    return L


def gen_cases(out):
    rng = random.Random(0x0A9C0DE4)
    out.write("# Intel APX parts 2/3 (ledger U640-U689): EVEX map 4 (promoted legacy instructions with NDD,\n")
    out.write("# NF and ZU), the APX conditional, PUSH2/POP2 and JMPABS instructions, the promoted map 2/3\n")
    out.write("# and VEX instructions. Expected values from the independent model\n")
    out.write("# Emulator/tools/isa/ref_apx_map4.py --cases (regenerate, do not edit). The i5-13600K has no\n")
    out.write("# APX: run with the APX opt-in:\n")
    out.write("#   emu-alltest --cases Emulator\\data\\cases_apx_map4.txt --apx --expect-only\n")
    out.write("# Undefined flags are cleared by a \"pushfq; and qword ptr [rsp], M; popfq\" suffix (the stack is\n")
    out.write("# not compared).\n")
    for case in gen_items(rng, True):
        out.write(case.line_rex2() + "\n")
    for l in special_lines(rng):
        out.write(l + "\n")


def hw_cases():
    rng = random.Random(0x0A9C0DE5)
    items = []
    for case in gen_items(rng, False, hw=True):
        if case.model()[1] is not None:
            continue                        # no faulting pairs
        l = case.line_pair()
        if l is not None:
            items.append((l, case))
    return items


def gen_hw(out):
    out.write("# Intel APX parts 2/3 hardware pairs: \"<legacy bytes> ~~ <EVEX map 4 bytes>\" with registers\n")
    out.write("# R0-R15 only. The i5-13600K runs the legacy encoding of the same operation (ND = 1: MOV to the\n")
    out.write("# NDD + the legacy op on it + MOVZX; NF = 1: inside PUSHFQ ... POPFQ), Unicorn the EVEX one;\n")
    out.write("# the whole state must be equal (test.cmd, 0 differing):\n")
    out.write("#   emu-alltest --cases Emulator\\data\\cases_apx_map4_hw.txt --apx\n")
    out.write("# Generated by Emulator/tools/isa/ref_apx_map4.py --hwgen (do not edit); --hwcmp LOG checks\n")
    out.write("# the CPU's results against the model.\n")
    for l, _ in hw_cases():
        out.write(l + "\n")


def hw_cmp(log_path):
    items = hw_cases()
    cur, bad, seen = None, 0, 0
    for line in open(log_path, encoding="utf-8-sig", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(\d+)\] (SAME|DIFF)", line)
        if m:
            cur = int(m.group(1))
            continue
        m = re.match(r"^\s+hw:\s*(.*)$", line)
        if not (m and cur is not None):
            continue
        fields = m.group(1).strip()
        case = items[cur][1]
        out, fault = case.model()
        seen += 1
        ok, why = True, ""
        if fields.startswith("fault"):
            ok, why = False, "hw fault %s" % fields
        elif fault is not None:
            ok, why = False, "model faults %d" % fault
        else:
            got = dict(x.split("=", 1) for x in fields.split() if "=" in x)
            exp = dict(x.split("=", 1) for x in fmt_changes(case.st, out).split() if "=" in x)
            und = case.ins.undef
            for k in set(got) | set(exp):
                if k in ("env", "ftw") or k.startswith("st") or k.startswith("fx+"):
                    continue
                g, e = got.get(k), exp.get(k)
                if k == "rflags":
                    gv = int(g, 0) if g else case.st.rflags
                    ev = int(e, 0) if e else case.st.rflags
                    ev &= ~und
                    if (gv ^ ev) & ~und:
                        ok, why = False, "rflags hw %X model %X (undefined %X)" % (gv, ev, und)
                    continue
                if k.startswith("m+"):
                    if g != e:
                        ok, why = False, "%s hw %s model %s" % (k, g, e)
                    continue
                if g is None or e is None or int(g, 0) != int(e, 0):
                    ok, why = False, "%s hw %s model %s" % (k, g, e)
        if not ok:
            bad += 1
            if bad <= 30:
                print("[%d] %s\n    %s" % (cur, items[cur][0], why))
        cur = None
    print("hwcmp: %d cases compared (%d generated), %d differ from the model" % (seen, len(items), bad))
    return bad == 0 and seen == len(items)


def selftest():
    ok = True

    def chk(name, got, exp):
        nonlocal ok
        if got != exp:
            ok = False
            print("FAIL %s: got %r expected %r" % (name, got, exp))
    # encodings worked by hand from Figure 3.3:
    # add r8, rax, rbx (W1 ND): P0 = 1111 0100, P1 = 1 0111 1 00, P2 = 000 1 1 0 00
    chk("add ndd", Ev(0x01, 3, Reg(0), w=1, nd=1, v=8).rex2(), bytes([0x62, 0xF4, 0xBC, 0x18, 0x01, 0xD8]))
    # add r31, r16, r17: R4 = 1 (P0[4] = 0), B4 = 1 (P0[3] = 1), V = 31 (vvvv 0000, V4 -> P2[3] = 0)
    chk("add egprs", Ev(0x01, 17, Reg(16), w=1, nd=1, v=31).rex2(), bytes([0x62, 0xEC, 0x84, 0x10, 0x01, 0xC8]))
    # index r20 (X4: U = 0, X3 = 0): P1 = 0 1111 0 00
    chk("x4", Ev(0x03, 0, Mem(3, 20, 1, 0)).rex2(), bytes([0x62, 0xF4, 0x78, 0x08, 0x03, 0x04, 0x23]))
    # shifts (SDM): shl 8-bit by 9 -> 0, CF undefined; rol 8-bit by 8 -> CF = LSB
    chk("shl8 9", shift_core("shl", 8, 0x81, 9, 0)[0], 0)
    chk("shl8 9 undef", shift_core("shl", 8, 0x81, 9, 0)[3] & CF, CF)
    chk("rol8 8", shift_core("rol", 8, 0x81, 8, 0)[:2], (0x81, CF))
    chk("rcl8 1", shift_core("rcl", 8, 0x80, 1, 1)[:2], (0x01, CF | OF))
    chk("rcr16 17", shift_core("rcr", 16, 0x1234, 17, 0)[0], 0x1234)
    chk("sar 0", shift_core("sar", 32, 5, 0, 0), (5, 0, 0, 0))
    return ok


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_cases(sys.stdout)
        return
    if "--hwgen" in sys.argv:
        gen_hw(sys.stdout)
        return
    if "--hwcmp" in sys.argv:
        sys.exit(0 if hw_cmp(sys.argv[sys.argv.index("--hwcmp") + 1]) else 1)
    print(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main()
