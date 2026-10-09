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
  python ref_apx_map4.py --ext          Emulator/data/cases_apx_map4_ext.txt (stdout): the promoted
                                        KMOV* / AMX forms, run with --apx --avx512 --amx
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
# --------------------------------------------------------------------------------------------
# U641: CCMPscc / CTESTscc (3.1.3.2.1, Figure 3.7/3.8/3.9, chapter 8.1/8.3)
#   P1 = W OF SF ZF CF U p p (the default flags value DFV, not inverted), P2 = 0 0 0 ND=0 SC3..SC0
#   SCC 1010b = T (always true), 1011b = F (always false), else the x86 condition code.
#   SCC true: the CMP / TEST updates the flags; false: OF SF ZF CF = DFV, PF = CF, AF = 0.
#   The memory operand is read either way (no fault suppression).
# --------------------------------------------------------------------------------------------
def scc_true(scc, fl):
    if scc == 0xA:
        return True
    if scc == 0xB:
        return False
    return cond(scc, fl)


def dfv_flags(dfv):
    return (OF if dfv & 8 else 0) | (SF if dfv & 4 else 0) | (ZF if dfv & 2 else 0) | \
           ((CF | PF) if dfv & 1 else 0)


def jcc8(cc, disp):
    return bytes([0x70 + cc, disp & 0xFF])


def set_dfv_legacy(dfv):
    """pushfq; and qword ptr [rsp], ~(OF SF ZF AF PF CF); or qword ptr [rsp], DFV; popfq"""
    return PUSHFQ + b"\x48\x81\x24\x24" + struct.pack("<i", ~STATUS) + \
        b"\x48\x81\x0c\x24" + struct.pack("<i", dfv_flags(dfv)) + POPFQ


def m4_ccmp(kind, n, form, dst, src, imm, scc, dfv):
    """kind 'cmp': form EG (38/39), GE (3A/3B), EI (80/81 /7), EI8 (83 /7); kind 'test': EG (84/85),
    EI (F6/F7 /0), EI1 (F6/F7 /1)"""
    w, pp = wpp(n)
    if n == 8:
        w = 0
    lpfx = [0x66] if n == 16 else []
    b8 = "reg,rm" if n == 8 else ""
    if kind == "cmp":
        if form == "EG":
            opc, reg, rm, ib = (0x38 if n == 8 else 0x39), src.r, dst, b""
        elif form == "GE":
            opc, reg, rm, ib = (0x3A if n == 8 else 0x3B), dst.r, src, b""
        elif form == "EI8":
            opc, reg, rm, ib = 0x83, 7, dst, imm_bytes(imm, 8)
        else:
            opc, reg, rm, ib = (0x80 if n == 8 else 0x81), 7, dst, imm_bytes(imm, min(n, 32))
    else:
        if form == "EG":
            opc, reg, rm, ib = (0x84 if n == 8 else 0x85), src.r, dst, b""
        else:
            opc, reg, rm, ib = (0xF6 if n == 8 else 0xF7), (1 if form == "EI1" else 0), dst, imm_bytes(imm, min(n, 32))
    # legacy: j!scc skip; cmp/test; jmp done; skip: set DFV; done
    body = Enc(opc, pfx=lpfx, reg=reg, rm=rm, w=w, imm=ib, b8=b8).legacy()
    if body is None:
        leg = None
    else:
        setf = set_dfv_legacy(dfv)
        if scc == 0xA:
            leg = body
        elif scc == 0xB:
            leg = setf
        else:
            leg = jcc8(scc ^ 1, len(body) + 2) + body + bytes([0xEB, len(setf)]) + setf
    e = Ev(opc, reg, rm, w=w, pp=pp, imm=ib, scc=scc, dfv=dfv, leg=leg)

    def operands(st):
        a = rd_op(st, dst, n)
        if form in ("EG", "GE"):
            b = rd_op(st, src, n)
        elif form == "EI8":
            b = sx(imm, 8) & mask(n)
        else:
            b = (sx(imm, 32) if n == 64 else imm) & mask(n)
        return a, b

    def sem(st):
        a, b = operands(st)          # read (and fault) whatever the SCC
        if scc_true(scc, st.rflags):
            _, f, _ = alu_core("cmp" if kind == "cmp" else "test", a, b, n, 0)
            set_flags(st, f, STATUS)
        else:
            set_flags(st, dfv_flags(dfv), STATUS)
    ins = Ins(e, sem)
    ins.undef_fn = lambda st: AF if (kind == "test" and scc_true(scc, st.rflags)) else 0
    return ins


# --------------------------------------------------------------------------------------------
# U642: CMOVcc ndd / CFCMOVcc (3.1.3.2.2, Table 3.5, chapter 8.2; 3.1.2.4: register
# destinations zero-extended; "memory faults are suppressed" when the condition is false)
# --------------------------------------------------------------------------------------------
def mov_r32_0(r):
    return Enc(0xB8, oreg=r, imm=bytes(4))          # mov r32, 0 (no flag change)


def m4_cmov(cc, n, reg, rmop, nd=0, nf=0, ndd=0):
    """map 4 40+cc /r (NP/66): ND NF = 00 CFCMOVcc reg, r/m; 01 CFCMOVcc r/m, reg; 10 CMOVcc ndd,
    reg, r/m; 11 CFCMOVcc ndd, reg, r/m"""
    w, pp = wpp(n)
    lpfx = [0x66] if n == 16 else []
    e_args = dict(w=w, pp=pp, nd=nd, nf=nf, v=ndd if nd else 0)
    # legacy equivalents (no faults in the hardware pairs)
    if nd:
        leg = leg_bytes(mov64(ndd, reg), Enc(0x40 + cc, map1=True, pfx=lpfx, reg=ndd, rm=rmop, w=w), zext(n, ndd))
    elif not nf:
        a = leg_bytes(Enc(0x8B, pfx=lpfx, reg=reg, rm=rmop, w=w), zext(n, reg))
        bb = leg_bytes(mov_r32_0(reg))
        leg = None if a is None or bb is None else jcc8(cc ^ 1, len(a) + 2) + a + bytes([0xEB, len(bb)]) + bb
    else:
        if isinstance(rmop, Reg):
            a = leg_bytes(Enc(0x89, pfx=lpfx, reg=reg, rm=rmop, w=w), zext(n, rmop.r))
            bb = leg_bytes(mov_r32_0(rmop.r))
        else:
            a = leg_bytes(Enc(0x89, pfx=lpfx, reg=reg, rm=rmop, w=w))
            bb = b""
        leg = None if a is None or bb is None else jcc8(cc ^ 1, len(a) + 2) + a + bytes([0xEB, len(bb)]) + bb
    e = Ev(0x40 + cc, reg, rmop, leg=leg, **e_args)

    def sem(st):
        c = cond(cc, st.rflags)
        if nd and not nf:
            t = rd_op(st, rmop, n)                     # faults whatever the condition
            st.regs[ndd] = (t if c else st.regs[reg]) & mask(n)
        elif nd:
            st.regs[ndd] = (rd_op(st, rmop, n) if c else st.regs[reg]) & mask(n)
        elif not nf:
            st.regs[reg] = rd_op(st, rmop, n) if c else 0
        elif c:
            if isinstance(rmop, Reg):
                st.regs[rmop.r] = st.regs[reg] & mask(n)
            else:
                wr_op(st, rmop, n, st.regs[reg])
        elif isinstance(rmop, Reg):
            st.regs[rmop.r] = 0
    return Ins(e, sem)


# --------------------------------------------------------------------------------------------
# U643: PUSH2 / POP2 (3.1.3.1.1, Figure 3.6, Table 3.4, chapter 9.1/9.3): EVEX map 4 pp=0 ND=1
#   FF /6 mod=11 PUSH2 v64, b64 = PUSH v64; PUSH b64 ; 8F /0 mod=11 POP2 v64, b64 = POP v64; POP
#   b64; #GP(0) if RSP % 16 != 0; W = PPX hint (no functional effect)
# --------------------------------------------------------------------------------------------
def m4_push2(v, b, w=0):
    e = Ev(0xFF, 6, Reg(b), w=w, nd=1, v=v)

    def sem(st):
        sp = st.regs[4]
        if sp % 16:
            raise Fault(13)
        st.wr(sp - 8, 8, st.regs[v])
        st.wr(sp - 16, 8, st.regs[b])
        st.regs[4] = sp - 16
    return Ins(e, sem)


def m4_pop2(v, b, w=0):
    e = Ev(0x8F, 0, Reg(b), w=w, nd=1, v=v)

    def sem(st):
        sp = st.regs[4]
        if sp % 16:
            raise Fault(13)
        x, y = st.rd(sp, 8), st.rd(sp + 8, 8)
        st.regs[v] = x
        st.regs[b] = y
        st.regs[4] = sp + 16
    return Ins(e, sem)


def m4_push2_pop2(v, b, v2, b2, w=0, w2=0):
    """PUSH2 v, b; POP2 v2, b2 (legacy: push v; push b; pop v2; pop b2)"""
    e1, e2 = m4_push2(v, b, w), m4_pop2(v2, b2, w2)
    leg = leg_bytes(Enc(0x50, oreg=v), Enc(0x50, oreg=b), Enc(0x58, oreg=v2), Enc(0x58, oreg=b2))
    raw = Raw(e1.rex2_bytes() + e2.rex2_bytes(), leg)

    def sem(st):
        x, y = st.regs[v], st.regs[b]      # the stack top is b's value
        st.regs[v2] = y
        st.regs[b2] = x
    return Ins(raw, sem)


# --------------------------------------------------------------------------------------------
# U645: EVEX map 4 instructions promoted from legacy maps 2/3 (3.1.2.3.1 2.(e), chapter 6):
#   60/61 MOVBE (also reg-reg), 66 ADCX (66) / ADOX (F3) with NDD, F0/F1 CRC32, 8A/8B MOVRS,
#   F9 MOVDIRI, F8 66 MOVDIR64B, FC AADD/AAND/AXOR/AOR, 65/66 WRUSS/WRSS, F8 F2/F3 ENQCMD(S) /
#   URDMSR / UWRMSR. Semantics from the SDM pages of the legacy forms.
# --------------------------------------------------------------------------------------------
def bswap(v, n):
    return int.from_bytes((v & mask(n)).to_bytes(n // 8, "little"), "big")


def crc32c(crc, data, nbytes):
    """SDM CRC32: the CRC-32C polynomial 11EDC6F41H, bit-reflected, no pre/post inversion"""
    crc &= mask(32)
    for i in range(nbytes):
        crc ^= (data >> (8 * i)) & 0xFF
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc


def m4_movbe(load, n, reg, rmop):
    """60 /r MOVBE rv, rv/mv (load = True) or 61 /r MOVBE rv/mv, rv: bytes reversed"""
    w, pp = wpp(n)
    lpfx = [0x66] if n == 16 else []
    if isinstance(rmop, Reg):
        # legacy: mov + bswap (32/64) or a 16-bit rol by 8 inside pushfq/popfq
        dst, src = (reg, rmop.r) if load else (rmop.r, reg)
        mv = Enc(0x8B, pfx=lpfx, reg=dst, rm=Reg(src), w=w)
        if n == 16:
            leg = frame(1, mv, Enc(0xC1, pfx=lpfx, reg=0, rm=Reg(dst), imm=b"\x08"))
        else:
            leg = leg_bytes(mv, Enc(0xC8, map1=True, oreg=dst, w=w))
    else:
        leg = legacy_0f38(0xF0 if load else 0xF1, lpfx, reg, rmop, w)
    e = Ev(0x60 if load else 0x61, reg, rmop, w=w, pp=pp, leg=leg)

    def sem(st):
        if load:
            st.setr(reg, n, bswap(rd_op(st, rmop, n), n))
        else:
            wr_op(st, rmop, n, bswap(st.getr(reg, n), n))
    return Ins(e, sem)


def legacy_0f38(opc, pfx, reg, rm, w, imm=b""):
    """a legacy 0F 38 xx instruction (mandatory prefixes in pfx, before REX)"""
    e2 = Enc(0x38, map1=True, pfx=pfx, reg=reg, rm=rm, w=w)
    lb = e2.legacy()
    if lb is None:
        return None
    k = lb.index(b"\x0f\x38") + 2
    return lb[:k] + bytes([opc]) + lb[k:] + imm


def m4_crc32(n, w, reg, rmop):
    """F0 /r CRC32 ry, r/m8 (n = 8, NP) or F1 /r CRC32 ry, rv/mv (NP/66); W: 64-bit destination"""
    pp = 1 if n == 16 else 0
    opc = 0xF0 if n == 8 else 0xF1
    lpfx = ([0x66] if n == 16 else []) + [0xF2]
    e2 = Enc(0x38, map1=True, pfx=lpfx, reg=reg, rm=rmop, w=w, b8="rm" if n == 8 else "")
    lb = e2.legacy()
    if lb is not None:
        k = lb.index(b"\x0f\x38") + 2
        lb = lb[:k] + bytes([opc]) + lb[k:]
    e = Ev(opc, reg, rmop, w=w, pp=pp, leg=lb)

    def sem(st):
        st.regs[reg] = crc32c(st.regs[reg], rd_op(st, rmop, n), n // 8)    # 63:32 zeroed
    return Ins(e, sem)


def m4_adcox(name, n, reg, rmop, nd=0, ndd=0):
    """66 /r ADCX (pp 66) / ADOX (pp F3): ry, ry/my; ND = 1: ry_n := ry_r + ry/my + CF/OF"""
    w = 1 if n == 64 else 0
    pp = 1 if name == "adcx" else 2
    lpfx = [0x66 if name == "adcx" else 0xF3]
    if nd:
        lb = leg_bytes(mov64(ndd, reg), legacy_0f38(0xF6, lpfx, ndd, rmop, w) or b"")
        if legacy_0f38(0xF6, lpfx, ndd, rmop, w) is None:
            lb = None
    else:
        lb = legacy_0f38(0xF6, lpfx, reg, rmop, w)
    e = Ev(0x66, reg, rmop, w=w, pp=pp, nd=nd, v=ndd if nd else 0, leg=lb)
    fl = CF if name == "adcx" else OF

    def sem(st):
        c = 1 if st.rflags & fl else 0
        s = st.getr(reg, n) + rd_op(st, rmop, n) + c
        if nd:
            st.regs[ndd] = s & mask(n)
        else:
            st.setr(reg, n, s)
        set_flags(st, fl if s >> n else 0, fl)
    return Ins(e, sem)


def m4_movrs(n, reg, m):
    """8A MOVRS r8, m8 (NP W0) / 8B MOVRS rv, mv (NP/66): a load with a read-shared hint"""
    w, pp = wpp(n)
    if n == 8:
        w = 0
    e = Ev(0x8A if n == 8 else 0x8B, reg, m, w=w, pp=pp)

    def sem(st):
        st.setr(reg, n, rd_op(st, m, n))
    return Ins(e, sem)


def m4_movdiri(n, m, reg):
    """F9 MOVDIRI m32/m64, r32/r64 (NP)"""
    w = 1 if n == 64 else 0
    e = Ev(0xF9, reg, m, w=w, leg=legacy_0f38(0xF9, [], reg, m, w))

    def sem(st):
        wr_op(st, m, n, st.regs[reg])
    return Ins(e, sem)


def m4_movdir64b(reg, m):
    """F8 /r 66 MOVDIR64B r64, m512: 64 bytes from m to [r64] (64-byte aligned, else #GP)"""
    e = Ev(0xF8, reg, m, pp=1, leg=legacy_0f38(0xF8, [0x66], reg, m, 0))

    def sem(st):
        d = st.regs[reg]
        if d % 64:
            raise Fault(13)
        st.wr(d, 64, st.rd(m.ea(st), 64))
    return Ins(e, sem)


def m4_rao(name, n, m, reg):
    """FC !(11) AADD (NP) / AAND (66) / AXOR (F3) / AOR (F2) m32/m64, r32/r64; flags unchanged;
    #GP if the memory operand is not naturally aligned"""
    w = 1 if n == 64 else 0
    pp = {"aadd": 0, "aand": 1, "axor": 2, "aor": 3}[name]
    e = Ev(0xFC, reg, m, w=w, pp=pp)

    def sem(st):
        a = m.ea(st)
        if a % (n // 8):
            raise Fault(13)
        v = rd_op(st, m, n)
        r = st.getr(reg, n)
        res = {"aadd": v + r, "aand": v & r, "axor": v ^ r, "aor": v | r}[name]
        wr_op(st, m, n, res)
    return Ins(e, sem)


# --------------------------------------------------------------------------------------------
# U646: VEX instructions promoted to EVEX (3.1.2.3.2, Figure 3.4: P2 = 0 0 L 0 V4 NF 0 0) -
#   BMI1/BMI2 in EVEX maps 2/3 (NF on ANDN BEXTR BLSI BLSMSK BLSR BZHI), CMPccXADD (map 2
#   E0-EF), KMOV* (map 1 90-93), AMX LDTILECFG/STTILECFG/TILELOADD(T1)/TILESTORED (map 2 49/4B),
#   URDMSR/UWRMSR imm32 (map 7 F8). Semantics: the SDM pages (BMI flags as listed there).
# --------------------------------------------------------------------------------------------
def vex3(mm, pp, w, opc, reg, rm, v=0, imm=b"", l=0):
    """3-byte VEX encoding (registers 0-15) of the legacy equivalent; None if an id needs APX"""
    r, x, b, tail, _ = modrm_parts(reg, rm)
    if (r | x | b | v) & 16:
        return None
    b1 = (((~r >> 3) & 1) << 7) | (((~x >> 3) & 1) << 6) | (((~b >> 3) & 1) << 5) | mm
    b2 = (w << 7) | (((~v) & 15) << 3) | (l << 2) | pp
    return bytes([0xC4, b1, b2, opc]) + tail + imm


BMI_FORMS = {
    # name: (map, pp, opc, reg ext or None, NF allowed, form)
    #   form 'rvm': r_r := f(r_n, r/m); 'rmv': r_r := f(r/m, r_n); 'vm': r_n := f(r/m); 'rmi': imm
    "andn": (2, 0, 0xF2, None, True, "rvm"),
    "bextr": (2, 0, 0xF7, None, True, "rmv"),
    "blsr": (2, 0, 0xF3, 1, True, "vm"),
    "blsmsk": (2, 0, 0xF3, 2, True, "vm"),
    "blsi": (2, 0, 0xF3, 3, True, "vm"),
    "bzhi": (2, 0, 0xF5, None, True, "rmv"),
    "pext": (2, 2, 0xF5, None, False, "rvm"),
    "pdep": (2, 3, 0xF5, None, False, "rvm"),
    "mulx": (2, 3, 0xF6, None, False, "rvm"),
    "shlx": (2, 1, 0xF7, None, False, "rmv"),
    "sarx": (2, 2, 0xF7, None, False, "rmv"),
    "shrx": (2, 3, 0xF7, None, False, "rmv"),
    "rorx": (3, 3, 0xF0, None, False, "rmi"),
}


def pdep(src, msk, n):
    r, k = 0, 0
    for i in range(n):
        if (msk >> i) & 1:
            r |= ((src >> k) & 1) << i
            k += 1
    return r


def pext(src, msk, n):
    r, k = 0, 0
    for i in range(n):
        if (msk >> i) & 1:
            r |= ((src >> i) & 1) << k
            k += 1
    return r


def m4_bmi(name, n, rr, rn, rmop, imm=0, nf=0):
    """BMI1/BMI2 in EVEX map 2/3: rr = ModRM.reg, rn = V (vvvv), rmop = r/m"""
    mapid, pp, opc, ext, nf_ok, form = BMI_FORMS[name]
    w = 1 if n == 64 else 0
    reg = ext if ext is not None else rr
    vv = rn if form != "rmi" else 0
    ib = bytes([imm & 0xFF]) if form == "rmi" else b""
    lv = vex3(mapid, pp, w, opc, reg, rmop, vv, ib)
    leg = None if lv is None else (PUSHFQ + lv + POPFQ if nf else lv)
    e = Ev(opc, reg, rmop, w=w, pp=pp, nf=nf, v=vv, imm=ib, mapid=mapid, leg=leg)
    m = mask(n)

    def sem(st):
        src = rd_op(st, rmop, n)
        fl, fm = 0, 0
        if name == "andn":
            res = ~st.getr(rn, n) & src & m
            fl, fm = szp(res, n) & (ZF | SF), STATUS & ~(AF | PF)
            st.setr(rr, n, res)
        elif name == "bextr":
            c = st.getr(rn, n)
            start, ln = c & 0xFF, (c >> 8) & 0xFF
            res = (src >> start) & mask(ln) if start < n else 0
            fl, fm = (ZF if res == 0 else 0), STATUS & ~(AF | SF | PF)
            st.setr(rr, n, res)
        elif name in ("blsi", "blsmsk", "blsr"):
            if name == "blsi":
                res = (-src) & src & m
                fl = (szp(res, n) & (ZF | SF)) | (CF if src != 0 else 0)
            elif name == "blsmsk":
                res = ((src - 1) ^ src) & m
                fl = (szp(res, n) & SF) | (CF if src == 0 else 0)
            else:
                res = ((src - 1) & src) & m
                fl = (szp(res, n) & (ZF | SF)) | (CF if src == 0 else 0)
            fm = STATUS & ~(AF | PF)
            st.setr(rn, n, res)
        elif name == "bzhi":
            k = st.getr(rn, n) & 0xFF
            res = src & mask(k) if k < n else src
            fl = (szp(res, n) & (ZF | SF)) | (CF if k > n - 1 else 0)
            fm = STATUS & ~(AF | PF)
            st.setr(rr, n, res)
        elif name in ("pdep", "pext"):
            s1 = st.getr(rn, n)
            st.setr(rr, n, pdep(s1, src, n) if name == "pdep" else pext(s1, src, n))
        elif name == "mulx":
            p = st.getr(2, n) * src
            st.setr(rn, n, p)              # low half to vvvv, then the high half to reg
            st.setr(rr, n, p >> n)
        elif name in ("shlx", "sarx", "shrx"):
            c = st.getr(rn, n) & (n - 1)
            res = {"shlx": src << c, "shrx": src >> c, "sarx": sx(src, n) >> c}[name] & m
            st.setr(rr, n, res)
        else:   # rorx
            c = imm & (n - 1)
            st.setr(rr, n, ((src >> c) | (src << (n - c))) & m)
        if fm and not nf:
            set_flags(st, fl, fm)
    und = 0 if (nf or name not in ("andn", "bextr", "blsi", "blsmsk", "blsr", "bzhi")) else \
        (AF | PF | (SF if name == "bextr" else 0))
    return Ins(e, sem, und)


def m4_cmpccxadd(cc, n, m, r2, r3):
    """EVEX.128.66.0F38.W0/W1 E0+cc !(11): CMPccXADD m, r2 (ModRM.reg), r3 (vvvv) (SDM)"""
    w = 1 if n == 64 else 0
    e = Ev(0xE0 + cc, r2, m, w=w, pp=1, v=r3, mapid=2)

    def sem(st):
        t1 = rd_op(st, m, n)
        t2 = (t1 + st.getr(r3, n)) & mask(n)
        _, f, _ = alu_core("cmp", t1, st.getr(r2, n), n, 0)
        wr_op(st, m, n, t2 if cond(cc, f | 0x202) else t1)
        st.setr(r2, n, t1)
        set_flags(st, f, STATUS)
    return Ins(e, sem)


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
    # ---- CCMPscc / CTESTscc (U641) ----
    for kind in ("cmp", "test"):
        for n in SIZES:
            forms = ("EG", "GE", "EI", "EI8") if kind == "cmp" else ("EG", "EI", "EI1")
            for form in forms:
                if form == "EI8" and n == 8:
                    continue
                for memf in (False, True):
                    for scc in rng.sample(range(16), 6) + [0xA, 0xB]:
                        st, a, b, m, used, runs = operands(n, memf)
                        imm = rng.getrandbits(8 if form == "EI8" else min(n, 32))
                        if form == "GE":
                            dst, src = Reg(a), (m if memf else Reg(b))
                        else:
                            dst, src = (m if memf else Reg(a)), Reg(b)
                        if rng.random() < 0.3:
                            st.regs[b] = st.regs[a]          # equal operands (ZF)
                        yield MCase(m4_ccmp(kind, n, form, dst, src, imm, scc, rng.getrandbits(4)), st, used, runs)
    # ---- CMOVcc ndd / CFCMOVcc (U642) ----
    for n in (16, 32, 64):
        for memf in (False, True):
            for nd, nf in nd_nf:
                for cc in rng.sample(range(16), 8):
                    st, a, b, m, used, runs = operands(n, memf)
                    rmop = m if memf else Reg(b)
                    avoid = [b] if not memf else [m.base] + ([m.index] if m.index is not None else [])
                    ndd = pick_ndd(avoid) if nd else 0
                    if nd:
                        st.regs[ndd] = rng.getrandbits(64)
                        used = used + [ndd]
                    yield MCase(m4_cmov(cc, n, a, rmop, nd, nf, ndd), st, used, runs)
    # ---- PUSH2 / POP2 (U643) ----
    for k in range(48):
        v, b, v2, b2 = regs(4)
        st = mk_state(rng, [v, b])
        w, w2 = rng.getrandbits(1), rng.getrandbits(1)
        if hw:
            yield MCase(m4_push2_pop2(v, b, v2, b2, w, w2), st, [v, b, v2, b2])
            continue
        sp = MEM + 0x9000 + 16 * rng.randrange(0, 0x80)
        st.regs[4] = sp
        if k % 2 == 0:
            yield MCase(m4_push2(v, b, w), st, [v, b, 4])
        else:
            # the 8 bytes below RSP hold RFLAGS: emu-alltest loads the input flags through the
            # test stack (popfq), so that qword is input state too
            data = st.rflags.to_bytes(8, "little") + bytes(rng.getrandbits(8) for _ in range(16))
            st.mem[sp - 8 - MEM:sp - MEM + 16] = data
            yield MCase(m4_pop2(v2, b2, w), st, [4], [(sp - 8 - MEM, data)])
    # ---- promoted legacy map 2/3 instructions (U645) ----
    for n in (16, 32, 64):
        for load in (True, False):
            for memf in (False, True):
                st, a, b, m, used, runs = operands(n, memf)
                yield MCase(m4_movbe(load, n, a, m if memf else Reg(b)), st, used, runs)
    for n, w in ((8, 0), (8, 1), (16, 0), (32, 0), (64, 1)):
        for memf in (False, True):
            for k in range(2):
                st, a, b, m, used, runs = operands(n, memf)
                yield MCase(m4_crc32(n, w, a, m if memf else Reg(b)), st, used, runs)
    for name in ("adcx", "adox"):
        for n in (32, 64):
            for memf in (False, True):
                for nd in (0, 1):
                    st, a, b, m, used, runs = operands(n, memf)
                    rmop = m if memf else Reg(b)
                    avoid = [b] if not memf else [m.base] + ([m.index] if m.index is not None else [])
                    ndd = pick_ndd(avoid) if nd else 0
                    if nd:
                        st.regs[ndd] = rng.getrandbits(64)
                        used = used + [ndd]
                    yield MCase(m4_adcox(name, n, a, rmop, nd, ndd), st, used, runs)
    for n in (32, 64):
        st, a, b, m, used, runs = operands(n, True)
        yield MCase(m4_movdiri(n, m, a), st, used, runs)
    for k in range(4):
        st, a, b, m, used, runs = operands(64, True, nbytes=64)
        st.regs[a] = MEM + 0xC000 + 64 * rng.randrange(0, 0x40)
        yield MCase(m4_movdir64b(a, m), st, used, runs)
    if not hw:
        for n in (8, 16, 32, 64):
            st, a, b, m, used, runs = operands(n, True)
            yield MCase(m4_movrs(n, a, m), st, used, runs)
        for name in ("aadd", "aand", "axor", "aor"):
            for n in (32, 64):
                a, b, base, idx = regs(4)
                st = mk_state(rng, [a, b])
                m, run = mem_at(rng, st, base, None, 1, 8, align=8)
                yield MCase(m4_rao(name, n, m, a), st, [a, base], [run])
    # ---- promoted VEX instructions: BMI1/BMI2 (NF on six of them), CMPccXADD (U646) ----
    for name in BMI_FORMS:
        nf_ok = BMI_FORMS[name][4]
        for n in (32, 64):
            for memf in (False, True):
                for nf in ((0, 1) if nf_ok else (0,)):
                    st, a, b, m, used, runs = operands(n, memf, excl=(2,))
                    rmop = m if memf else Reg(b)
                    avoid = [a, 2] + ([b] if not memf else [m.base] + ([m.index] if m.index is not None else []))
                    rn = pick_ndd(avoid)
                    st.regs[rn] = rng.getrandbits(64)
                    if name in ("bextr",):
                        st.regs[rn] = rng.randrange(0, n + 8) | (rng.randrange(0, n + 8) << 8) | (rng.getrandbits(16) << 16)
                    elif name == "bzhi":
                        st.regs[rn] = (st.regs[rn] & ~0xFF) | rng.randrange(0, 80)
                    st.regs[2] = rng.getrandbits(64)
                    if rng.random() < 0.15:
                        if memf:
                            off = m.ea(st) - MEM
                            st.mem[off:off + n // 8] = bytes(n // 8)
                        else:
                            st.regs[b] = 0
                    yield MCase(m4_bmi(name, n, a, rn, rmop, rng.getrandbits(8), nf), st, used + [rn, 2], fresh(st, runs))
    if not hw:
        for n in (32, 64):
            for cc in range(16):
                a, b, base, idx = regs(4)
                st = mk_state(rng, [a, b])
                m, run = mem_at(rng, st, base, idx if rng.random() < 0.5 else None, rng.choice([1, 2, 4, 8]), 8)
                if rng.random() < 0.3:
                    st.regs[a] = int.from_bytes(run[1][:n // 8], "little")
                yield MCase(m4_cmpccxadd(cc, n, m, a, b), st, [a, b, base] + ([m.index] if m.index is not None else []), [run])


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
    c("--- CCMPscc / CTESTscc (U641): DFV in P1[6:3] (not inverted), SCC in P2[3:0], T / F ---")
    for scc in range(16):
        L.append(mk_line(m4_ccmp("cmp", 64, "EG", Reg(0), Reg(1), None, scc, scc ^ 0x5), {0: 5, 1: 7}, rflags=0x202 | (scc & 1) * CF | ((scc >> 1) & 1) * ZF | ((scc >> 2) & 1) * SF | ((scc >> 3) & 1) * OF))
    L.append(mk_line(m4_ccmp("cmp", 64, "EG", Reg(16), Reg(31), None, 0xB, 0xF), {16: 5, 31: 5}))       # F: all four DFV bits, PF = CF, AF = 0
    L.append(mk_line(m4_ccmp("cmp", 64, "EG", Reg(16), Reg(31), None, 0xB, 0x0), {16: 5, 31: 7}, rflags=0x8D7))
    L.append(mk_line(m4_ccmp("test", 32, "EI", Mem(20, None, 1, 0), None, 0x80, 0xA, 0x3), {20: MEM_PTR}, {0x8000: bytes([0x80, 0, 0, 0])}))
    L.append(mk_line(m4_ccmp("test", 8, "EG", Reg(6), Reg(7), None, 0x4, 0x2), {6: 0x10, 7: 0x01}, rflags=0x242))   # SIL, DIL
    # SCC false still reads the memory operand: #PF on an unmapped address
    L.append("%s | rbx=0x10 => #PF" % dotbyte(Ev(0x39, 0, Mem(3, None, 1, 0), w=1, scc=0xB, dfv=0).rex2()))
    c("--- CCMP / CTEST #UD: P2 bits 7..4 (incl. ND), mod = 11b with U = 0, pp ---")
    for bit in (0x80, 0x40, 0x20, 0x10):
        L.append(line_ud(Ev(0x39, 1, Reg(0), w=1, scc=2, dfv=0, p2or=bit).rex2()))
    L.append(line_ud(Ev(0x39, 1, Reg(0), w=1, scc=2, dfv=0, ubit=0).rex2()))
    L.append(line_ud(Ev(0x38, 1, Reg(0), pp=1, scc=2, dfv=0).rex2()))
    L.append(line_ud(Ev(0x39, 1, Reg(0), pp=2, scc=2, dfv=0).rex2()))
    L.append(line_ud(Ev(0x85, 1, Reg(0), pp=3, scc=2, dfv=0).rex2()))
    L.append(line_ud(Ev(0xF6, 0, Reg(0), pp=1, scc=2, dfv=0, imm=b"\x01").rex2()))
    L.append(line_ud(Ev(0x3C, 0, Reg(0), scc=2, dfv=0).rex2() + b"\x01"))          # CMP AL, ib: not promoted
    L.append(line_ud(Ev(0xA9, 0, Reg(0), scc=2, dfv=0).rex2() + bytes(4)))         # TEST eAX, iz: not promoted
    # V-bits other than DFV are not checked, NF bit is SC2: a CCMP with SC2 = 1 (SCC = 4, Z)
    L.append(mk_line(m4_ccmp("cmp", 32, "EG", Reg(0), Reg(1), None, 0x4, 0xF), {0: 1, 1: 1}, rflags=0x202))
    c("--- CFCMOVcc (U642): no memory access when the condition is false (an unmapped address) ---")
    UNM = 0x10                                   # not mapped
    for nd, nf in ((0, 0), (0, 1), (1, 1)):
        e = m4_cmov(0x4, 64, 0, Mem(3, None, 1, 0), nd, nf, 9)      # CFCMOVZ, ZF = 0: false
        L.append(mk_line(e, {3: UNM, 0: 0x1111, 9: 0x2222}))
        e = m4_cmov(0x5, 32, 0, Mem(3, None, 1, 0), nd, nf, 9)      # CFCMOVNZ, ZF = 0: true -> #PF
        L.append("%s | rbx=0x%X rax=0x1111 r9=0x2222 => #PF" % (dotbyte(e.rex2_bytes()), UNM))
    # CMOVcc ndd (ND = 1, NF = 0) reads the memory operand whatever the condition
    e = m4_cmov(0x4, 64, 0, Mem(3, None, 1, 0), 1, 0, 9)
    L.append("%s | rbx=0x%X rax=0x1111 r9=0x2222 => #PF" % (dotbyte(e.rex2_bytes()), UNM))
    # register forms: zeroing (false) and zero-extension (true) of 16/32-bit destinations
    L.append(mk_line(m4_cmov(0x4, 16, 0, Reg(1)), {0: -1, 1: 0x12345678}))
    L.append(mk_line(m4_cmov(0x4, 16, 0, Reg(1)), {0: -1, 1: 0x12345678}, rflags=0x242))
    L.append(mk_line(m4_cmov(0x4, 32, 0, Reg(1), 0, 1), {0: 0x12345678, 1: -1}))
    L.append(mk_line(m4_cmov(0x4, 32, 0, Reg(1), 0, 1), {0: 0x12345678, 1: -1}, rflags=0x242))
    L.append(mk_line(m4_cmov(0x4, 16, 0, Mem(3, None, 1, 0), 0, 1), {0: 0x12345678, 3: MEM_PTR}, {0x8000: bytes([0xAA, 0xBB, 0xCC, 0xDD])}, rflags=0x242))
    L.append(mk_line(m4_cmov(0xF, 64, 17, Reg(30), 1, 0, 25), {17: 1, 30: 2, 25: 3}, rflags=0x2C2))
    L.append(mk_line(m4_cmov(0xF, 64, 17, Reg(30), 1, 1, 25), {17: 1, 30: 2, 25: 3}, rflags=0x202))
    c("--- CMOVcc / CFCMOVcc #UD: V without ND, pp F3, mod = 11b with U = 0, reserved P2 bits ---")
    L.append(line_ud(Ev(0x44, 0, Reg(1), v=3).rex2()))
    L.append(line_ud(Ev(0x44, 0, Reg(1), nf=1, v=3).rex2()))
    L.append(line_ud(Ev(0x44, 0, Reg(1), pp=2).rex2()))
    L.append(line_ud(Ev(0x44, 0, Reg(1), ubit=0).rex2()))
    L.append(line_ud(Ev(0x44, 0, Reg(1), nd=1, v=3, p2or=0x01).rex2()))
    L.append(line_ud(Ev(0x44, 0, Reg(1), nd=1, v=3, p2or=0x20).rex2()))
    c("--- PUSH2 / POP2 (U643): order, RSP alignment #GP, PPX (W), #UD rules ---")
    SP = MEM + 0x9000
    L.append(mk_line(m4_push2(16, 31), {4: SP, 16: 0x1111111111111111, 31: 0x2222222222222222}))
    L.append(mk_line(m4_push2(1, 2, w=1), {4: SP, 1: 0xA, 2: 0xB}))
    fq = (0x202).to_bytes(8, "little")          # RFLAGS below RSP (emu-alltest's popfq)
    L.append(mk_line(m4_pop2(20, 3), {4: SP}, {0x8FF8: fq + bytes(range(16))}))
    L.append(mk_line(m4_pop2(3, 20, w=1), {4: SP}, {0x8FF8: fq + bytes(range(16))}))
    for off in (8, 4, 1):
        for e in (m4_push2(1, 2), m4_pop2(1, 2)):
            L.append("%s | rsp=0x%X m+0x%X=%s => #GP" % (dotbyte(e.rex2_bytes()), SP + off, 0x9000 + off - 8, fq.hex().upper()))
    for e in (Ev(0xFF, 6, Reg(4), nd=1, v=1), Ev(0xFF, 6, Reg(1), nd=1, v=4), Ev(0x8F, 0, Reg(4), nd=1, v=1),
              Ev(0x8F, 0, Reg(1), nd=1, v=4), Ev(0x8F, 0, Reg(1), nd=1, v=1), Ev(0x8F, 0, Reg(17), nd=1, v=17),
              Ev(0xFF, 6, Reg(1), v=0), Ev(0x8F, 0, Reg(1), v=0), Ev(0xFF, 6, Reg(1), nd=1, v=2, nf=1),
              Ev(0xFF, 6, Reg(1), nd=1, v=2, pp=1), Ev(0x8F, 0, Reg(1), nd=1, v=2, pp=2),
              Ev(0xFF, 6, Reg(1), nd=1, v=2, ubit=0), Ev(0xFF, 6, Reg(1), nd=1, v=2, p2or=0x20),
              Ev(0x8F, 1, Reg(1), nd=1, v=2), Ev(0xFF, 6, Mem(3, None, 1, 0), nd=1, v=2),
              Ev(0x8F, 0, Mem(3, None, 1, 0), nd=1, v=2), Ev(0xFF, 7, Reg(1), nd=1, v=2)):
        L.append(line_ud(e.rex2()))
    # RSP id through B4/V4: R20 (not RSP) is fine
    L.append(mk_line(m4_push2(20, 21), {4: SP, 20: 5, 21: 6}))
    c("--- promoted legacy map 2/3 instructions (U645): EGPRs, byte registers, MOVBE reg-reg ---")
    L.append(mk_line(m4_movbe(True, 64, 16, Reg(31)), {16: -1, 31: 0x0102030405060708}))
    L.append(mk_line(m4_movbe(False, 16, 17, Reg(30)), {17: 0x1122, 30: 0xFFFFFFFFFFFFFFFF}))
    L.append(mk_line(m4_movbe(True, 32, 2, Mem(20, 21, 2, -8)), {20: MEM_PTR, 21: 4, 2: -1}, {0x8000: bytes([1, 2, 3, 4])}))
    L.append(mk_line(m4_crc32(8, 0, 0, Reg(6)), {0: 0xFFFFFFFF, 6: 0x1261}))              # SIL, not DH
    L.append(mk_line(m4_crc32(8, 1, 19, Reg(7)), {19: 0xFFFFFFFFFFFFFFFF, 7: 0x31}))
    L.append(mk_line(m4_crc32(16, 0, 1, Reg(2)), {1: 0, 2: 0x3132}))
    L.append(mk_line(m4_adcox("adcx", 64, 1, Reg(2), nd=1, ndd=27), {1: -1, 2: 1, 27: 5}, rflags=0x203))
    L.append(mk_line(m4_adcox("adox", 32, 1, Reg(2), nd=1, ndd=27), {1: 0xFFFFFFFF, 2: 0, 27: -1}, rflags=0xA02))
    c("--- U645 #UD: ND/NF/V/pp/W/mod rules, INVEPT/INVVPID/INVPCID (not supported), U = 0 ---")
    for e in (Ev(0x60, 0, Reg(1), nd=1, v=2), Ev(0x60, 0, Reg(1), nf=1), Ev(0x60, 0, Reg(1), v=2),
              Ev(0x60, 0, Reg(1), pp=2), Ev(0x60, 0, Reg(1), pp=3), Ev(0x61, 0, Reg(1), pp=3),
              Ev(0xF0, 0, Reg(1), pp=1), Ev(0xF1, 0, Reg(1), pp=3), Ev(0xF0, 0, Reg(1), nd=1, v=1),
              Ev(0xF1, 0, Reg(1), nf=1), Ev(0x66, 0, Reg(1), pp=1, nf=1), Ev(0x66, 0, Reg(1), pp=3),
              Ev(0x66, 0, Reg(1), pp=1, v=3), Ev(0x66, 0, Reg(1)), Ev(0x65, 0, Reg(1), pp=1),
              Ev(0x65, 0, Mem(3, None, 1, 0), pp=0), Ev(0x8A, 0, Reg(1)), Ev(0x8B, 0, Reg(1)),
              Ev(0x8A, 0, Mem(3, None, 1, 0), w=1), Ev(0x8A, 0, Mem(3, None, 1, 0), pp=1),
              Ev(0x8B, 0, Mem(3, None, 1, 0), pp=2), Ev(0xF9, 0, Reg(1)), Ev(0xF9, 0, Mem(3, None, 1, 0), pp=1),
              Ev(0xF8, 0, Reg(1), pp=1), Ev(0xF8, 0, Mem(3, None, 1, 0)), Ev(0xFC, 0, Reg(1)),
              Ev(0xFC, 0, Mem(3, None, 1, 0), nd=1, v=1), Ev(0xF8, 0, Reg(1), pp=3, w=1),
              Ev(0xF8, 0, Reg(1), pp=2, w=1), Ev(0xF8, 0, Reg(1), pp=3, v=1),
              Ev(0xF0, 0, Mem(3, None, 1, 0), pp=2), Ev(0xF1, 0, Mem(3, None, 1, 0), pp=2),
              Ev(0xF2, 0, Mem(3, None, 1, 0), pp=2), Ev(0x60, 0, Reg(1), ubit=0),
              Ev(0x66, 0, Reg(1), pp=1, p2or=0x01), Ev(0x60, 0, Reg(1), p2or=0x20),
              Ev(0x62, 0, Reg(1)), Ev(0x67, 0, Reg(1)), Ev(0xFF, 0, Mem(3, None, 1, 0), pp=3)):
        L.append(line_ud(e.rex2(), "rbx=0x%X" % MEM_PTR))
    # CET shadow stacks are off (CR4.CET = 0): WRSS / WRUSS #UD as their legacy forms
    L.append(line_ud(Ev(0x66, 0, Mem(3, None, 1, 0)).rex2(), "rbx=0x%X" % MEM_PTR))
    L.append(line_ud(Ev(0x65, 0, Mem(3, None, 1, 0), pp=1).rex2(), "rbx=0x%X" % MEM_PTR))
    # MOVDIR64B: destination not 64-byte aligned -> #GP; RAO-INT misaligned -> #GP
    L.append("%s | rax=0x%X rbx=0x%X => #GP" % (dotbyte(m4_movdir64b(0, Mem(3, None, 1, 0)).rex2_bytes()), MEM + 0xC008, MEM_PTR))
    L.append("%s | rax=0x1 rbx=0x%X => #GP" % (dotbyte(m4_rao("aadd", 32, Mem(3, None, 1, 0), 0).rex2_bytes()), MEM_PTR + 2))
    c("--- promoted VEX instructions (U646): BMI with EGPRs (vvvv V4), NF, CMPccXADD ---")
    L.append(mk_line(m4_bmi("andn", 64, 16, 31, Reg(17)), {16: 0, 31: 0xF0F0, 17: 0xFFFF}))
    L.append(mk_line(m4_bmi("andn", 64, 16, 31, Reg(17), nf=1), {16: 0, 31: 0xF0F0, 17: 0xFFFF}, rflags=0x8D7))
    L.append(mk_line(m4_bmi("blsr", 32, 0, 20, Mem(21, 22, 4, 4)), {21: MEM_PTR, 22: 1, 20: -1}, {0x8008: bytes([0, 0, 0, 0])}))
    L.append(mk_line(m4_bmi("mulx", 64, 25, 25, Reg(26)), {2: -1, 26: -1, 25: 7}))         # same reg: high half
    L.append(mk_line(m4_bmi("rorx", 32, 18, 0, Reg(19), imm=0x24), {18: -1, 19: 0x12345678}))
    L.append(mk_line(m4_bmi("shlx", 64, 3, 28, Reg(29)), {28: 0x41, 29: 3}))
    c("--- U646 #UD: L = 1, ND, NF without support, V without use, reserved P2 bits, U = 0, pp ---")
    for e in (Ev(0xF2, 1, Reg(0), mapid=2, v=2, ll=1), Ev(0xF2, 1, Reg(0), mapid=2, v=2, nd=1),
              Ev(0xF2, 1, Reg(0), mapid=2, v=2, p2or=0x80), Ev(0xF2, 1, Reg(0), mapid=2, v=2, p2or=0x40),
              Ev(0xF2, 1, Reg(0), mapid=2, v=2, p2or=0x01), Ev(0xF2, 1, Reg(0), mapid=2, v=2, p2or=0x02),
              Ev(0xF6, 1, Reg(0), mapid=2, pp=3, v=2, nf=1), Ev(0xF5, 1, Reg(0), mapid=2, pp=3, v=2, nf=1),
              Ev(0xF7, 1, Reg(0), mapid=2, pp=1, v=2, nf=1), Ev(0xF0, 1, Reg(0), mapid=3, pp=3, nf=1, imm=b"\x01"),
              Ev(0xF0, 1, Reg(0), mapid=3, pp=3, v=2, imm=b"\x01"), Ev(0xF2, 1, Reg(0), mapid=2, v=2, ubit=0),
              Ev(0xF2, 1, Reg(0), mapid=2, pp=1, v=2), Ev(0xF3, 4, Reg(0), mapid=2, v=2),
              Ev(0xF6, 1, Reg(0), mapid=2, pp=1), Ev(0xF6, 1, Reg(0), mapid=2, pp=2), Ev(0xF5, 1, Mem(3, None, 1, 0), mapid=2, pp=1),
              Ev(0xE4, 1, Reg(0), mapid=2, pp=1, v=2), Ev(0xE4, 1, Mem(3, None, 1, 0), mapid=2, pp=0, v=2),
              Ev(0xE4, 1, Mem(3, None, 1, 0), mapid=2, pp=1, v=2, ll=1), Ev(0xE4, 1, Mem(3, None, 1, 0), mapid=2, pp=1, v=2, nf=1),
              Ev(0xE4, 1, Mem(3, None, 1, 0), mapid=2, pp=1, v=2, nd=1),
              Ev(0x90, 1, Reg(0), mapid=1), Ev(0x49, 0, Mem(3, None, 1, 0), mapid=2), Ev(0x4B, 0, Mem(3, 1, 1, 0), mapid=2, pp=3),
              Ev(0xF8, 0, Reg(0), mapid=7, pp=3, imm=bytes(4)), Ev(0xF6, 0, Reg(0), mapid=7, pp=3, imm=bytes(4))):
        L.append(line_ud(e.rex2(), "rbx=0x%X" % MEM_PTR))
    L.append(line_ud(bytes([0x0F, 0x01, 0xD1]) + Ev(0xF2, 1, Reg(0), mapid=2, v=2).rex2(), "rcx=0x0 rax=0x7 rdx=0x0"))
    c("--- JMPABS (U644): REX2 M0 = 0 W = 0 A1 target64; non-canonical target #GP; W = 1 and"
      " 66/67/F0/F2/F3/REX before it #UD (the jump itself: unit test test_x86_ax4_jmpabs) ---")
    tgt = struct.pack("<Q", 0x0000800000000000)
    L.append("%s => #GP" % dotbyte(b"\xd5\x00\xa1" + tgt))
    L.append("%s => #GP" % dotbyte(b"\xd5\x77\xa1" + struct.pack("<Q", 0xFFFF7FFFFFFFFFF0)))   # R4 X4 B4 R3 X3 B3 ignored
    L.append("%s => #GP" % dotbyte(b"\x2e\xd5\x00\xa1" + tgt))                              # segment override ignored
    L.append(line_ud(b"\xd5\x08\xa1" + bytes(8)))                                              # W = 1
    for pfx in (0x66, 0x67, 0xF0, 0xF2, 0xF3, 0x40, 0x48):
        L.append(line_ud(bytes([pfx]) + b"\xd5\x00\xa1" + bytes(8)))
    L.append(line_ud(bytes([0x0F, 0x01, 0xD1]) + b"\xd5\x00\xa1" + tgt, "rcx=0x0 rax=0x7 rdx=0x0"))
    c("--- XCR0[19] = 0 (XSETBV 7 first): every map 4 instruction #UD (Table 3.8) ---")
    L.append(line_ud(bytes([0x0F, 0x01, 0xD1]) + Ev(0x01, 1, Reg(0), w=1).rex2(), "rcx=0x0 rax=0x7 rdx=0x0"))
    L.append(line_ud(bytes([0x0F, 0x01, 0xD1]) + Ev(0x44, 0, Reg(0), pp=3).rex2(), "rcx=0x0 rax=0x7 rdx=0x0"))
    return L


# --------------------------------------------------------------------------------------------
# U646 with AVX-512 and AMX: the promoted KMOV* (APX-EVEX-KMOV) and AMX (LDTILECFG, STTILECFG,
# TILELOADD, TILELOADDT1, TILESTORED) forms - Emulator/data/cases_apx_map4_ext.txt, run with
# --apx --avx512 --amx (XCR0 = E02E7H: opmask/ZMM, AMX and APX state enabled)
# --------------------------------------------------------------------------------------------
KMOV_SZ = {  # (opcode, pp, W) -> bits of the k <-> k/m / r move
    (0x90, 0, 0): 16, (0x90, 0, 1): 64, (0x90, 1, 0): 8, (0x90, 1, 1): 32,
    (0x91, 0, 0): 16, (0x91, 0, 1): 64, (0x91, 1, 0): 8, (0x91, 1, 1): 32,
    (0x92, 0, 0): 16, (0x92, 1, 0): 8, (0x92, 3, 0): 32, (0x92, 3, 1): 64,
    (0x93, 0, 0): 16, (0x93, 1, 0): 8, (0x93, 3, 0): 32, (0x93, 3, 1): 64,
}


def ext_lines(rng):
    L = []
    c = lambda s: L.append("# " + s)
    c("--- KMOV (EVEX map 1 90-93, APX-promoted): GPRs R16-R31 (B4 / R4) and EGPR addresses ---")
    for (opc, pp, w), n in sorted(KMOV_SZ.items()):
        for k in range(3):
            kr, kb = rng.randrange(0, 8), rng.randrange(0, 8)
            gv = rng.getrandbits(64)
            kv = rng.getrandbits(64)
            if opc == 0x92:                       # k_r := zero-extend(r_b[n-1:0])
                rb = rng.choice(EGPR + LOW)
                e = Ev(opc, kr, Reg(rb), w=w, pp=pp, mapid=1)
                L.append("%s | %s=0x%X => k%d=0x%X" % (dotbyte(e.rex2()), NAMES[rb], gv, kr, gv & mask(n)))
            elif opc == 0x93:                     # r_r := zero-extend(k_b[n-1:0])
                rr = rng.choice(EGPR + LOW)
                e = Ev(opc, rr, Reg(kb), w=w, pp=pp, mapid=1)
                L.append("%s | k%d=0x%X %s=0x%X => %s=0x%X" % (dotbyte(e.rex2()), kb, kv, NAMES[rr], gv, NAMES[rr], kv & mask(n)))
            elif opc == 0x90 and k == 0:          # k_r := k_b
                e = Ev(opc, kr, Reg(kb), w=w, pp=pp, mapid=1)
                exp = (kv & mask(n)) if kr != kb else (kv & mask(n))
                L.append("%s | k%d=0x%X => k%d=0x%X" % (dotbyte(e.rex2()), kb, kv, kr, exp))
            else:                                 # memory: [r20 + r21*8 + disp]
                base, idx = rng.sample(EGPR, 2)
                off = 0x8000 + 8 * rng.randrange(0, 0x100)
                iv = rng.randrange(0, 0x20)
                disp = rng.choice([0, 8, -8, 0x40])
                bv = MEM + off - iv * 8 - disp
                e = Ev(opc, kr, Mem(base, idx, 8, disp), w=w, pp=pp, mapid=1)
                data = kv.to_bytes(8, "little")
                if opc == 0x90:
                    L.append("%s | %s=0x%X %s=0x%X m+0x%X=%s => k%d=0x%X" % (dotbyte(e.rex2()), NAMES[base], bv, NAMES[idx], iv,
                                                                          off, data.hex().upper(), kr, kv & mask(n)))
                else:
                    out = (kv & mask(n)).to_bytes(n // 8, "little")
                    L.append("%s | k%d=0x%X %s=0x%X %s=0x%X => m+0x%X=%s" % (dotbyte(e.rex2()), kr, kv, NAMES[base], bv, NAMES[idx], iv,
                                                                             off, out.hex().upper()))
    c("--- KMOV #UD: L = 1, V != 0, NF / ND, R3 with a k register in ModRM.reg, memory / register forms ---")
    for e in (Ev(0x92, 1, Reg(0), mapid=1, ll=1), Ev(0x92, 1, Reg(0), mapid=1, v=1), Ev(0x92, 1, Reg(0), mapid=1, nf=1),
              Ev(0x92, 1, Reg(0), mapid=1, nd=1), Ev(0x92, 9, Reg(0), mapid=1), Ev(0x90, 9, Reg(0), mapid=1),
              Ev(0x91, 1, Reg(0), mapid=1), Ev(0x92, 1, Mem(3, None, 1, 0), mapid=1), Ev(0x93, 1, Mem(3, None, 1, 0), mapid=1),
              Ev(0x92, 1, Reg(0), mapid=1, pp=2), Ev(0x92, 1, Reg(0), mapid=1, w=1), Ev(0x93, 1, Reg(0), mapid=1, ubit=0),
              Ev(0x92, 1, Reg(0), mapid=1, p2or=0x80), Ev(0x92, 1, Reg(0), mapid=1, p2or=0x01)):
        L.append(line_ud(e.rex2(), "rbx=0x%X" % MEM_PTR))
    # R4 with a k register (ignored) and V4 (must be 0)
    L.append("%s | r16=0x1234 => k1=0x1234" % dotbyte(Ev(0x92, 1 | 16, Reg(16), mapid=1).rex2()))
    L.append(line_ud(Ev(0x92, 1, Reg(16), mapid=1, v=16).rex2()))
    c("--- AMX (EVEX map 2 49 / 4B, APX-promoted): LDTILECFG [r16]; TILELOADD tmm0, [r17 + r18];"
      " TILESTORED [r19 + r20], tmm0; STTILECFG [r21]; TILERELEASE (VEX) ---")
    for k in range(4):
        rows, colsb = rng.randrange(1, 5), rng.choice([4, 8, 16, 32])
        stride_ld, stride_st = rng.choice([32, 64, 48]), rng.choice([64, 80])
        cfg = bytearray(64)
        cfg[0] = 1
        cfg[16:18] = colsb.to_bytes(2, "little")
        cfg[48] = rows
        src = bytes(rng.getrandbits(8) for _ in range(rows * stride_ld))
        prog = Ev(0x49, 0, Mem(16, None, 1, 0), mapid=2).rex2() + \
            Ev(0x4B, 0, Mem(17, 18, 1, 0), pp=3, mapid=2).rex2() + \
            Ev(0x4B, 0, Mem(19, 20, 1, 0), pp=2, mapid=2).rex2() + \
            Ev(0x49, 0, Mem(21, None, 1, 0), pp=1, mapid=2).rex2() + bytes([0xC4, 0xE2, 0x78, 0x49, 0xC0])
        st = St()
        regs = {16: MEM + 0x8000, 17: MEM + 0x8100, 18: stride_ld, 19: MEM + 0x9000, 20: stride_st, 21: MEM + 0x9800}
        for r, v in regs.items():
            st.regs[r] = v
        st.mem[0x8000:0x8040] = cfg
        st.mem[0x8100:0x8100 + len(src)] = src

        def sem(s2, rows=rows, colsb=colsb, stride_ld=stride_ld, stride_st=stride_st):
            s2.mem[0x9800:0x9840] = s2.mem[0x8000:0x8040]
            for i in range(rows):
                s2.mem[0x9000 + i * stride_st:0x9000 + i * stride_st + colsb] = \
                    s2.mem[0x8100 + i * stride_ld:0x8100 + i * stride_ld + colsb]
        L.append(MCase(Ins(Raw(prog), sem), st, list(regs), [(0x8000, bytes(cfg)), (0x8100, src)]).line_rex2())
    c("--- AMX #UD: register forms (TILERELEASE / TILEZERO are not promoted), W1, L1, V, TILELOADDRS (4A, not implemented) ---")
    for e in (Ev(0x49, 0, Reg(0), mapid=2), Ev(0x49, 0, Reg(0), pp=3, mapid=2), Ev(0x49, 0, Mem(16, None, 1, 0), w=1, mapid=2),
              Ev(0x49, 0, Mem(16, None, 1, 0), ll=1, mapid=2), Ev(0x49, 0, Mem(16, None, 1, 0), v=1, mapid=2),
              Ev(0x49, 0, Mem(16, None, 1, 0), nf=1, mapid=2), Ev(0x4B, 0, Mem(17, 18, 1, 0), pp=3, w=1, mapid=2),
              Ev(0x4A, 0, Mem(17, 18, 1, 0), pp=3, mapid=2), Ev(0x4A, 0, Mem(17, 18, 1, 0), pp=1, mapid=2),
              Ev(0x5E, 0, Reg(1), pp=3, mapid=2), Ev(0x4B, 0, Mem(17, 18, 1, 0), pp=0, mapid=2)):
        L.append(line_ud(e.rex2(), "r16=0x%X r17=0x%X r18=0x40" % (MEM + 0x8000, MEM + 0x8100)))
    return L


def gen_ext(out):
    rng = random.Random(0x0A9C0DE6)
    out.write("# Intel APX parts 2/3 (ledger U646): the APX-promoted KMOV* and AMX forms (EVEX maps 1/2)\n")
    out.write("# with EGPRs. Expected values from Emulator/tools/isa/ref_apx_map4.py --ext (regenerate, do\n")
    out.write("# not edit); Unicorn only, with the APX, AVX-512 and AMX opt-ins:\n")
    out.write("#   emu-alltest --cases Emulator\\data\\cases_apx_map4_ext.txt --apx --avx512 --amx --expect-only\n")
    for l in ext_lines(rng):
        out.write(l + "\n")


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
    if "--ext" in sys.argv:
        gen_ext(sys.stdout)
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
