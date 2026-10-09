#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_apx_core.py -- independent reference model (Python 3 stdlib only) of Intel APX part 1
(ledger U610-U616): the extended GPRs R16-R31, the REX2 prefix and the APX state, and generator
of the case files Emulator/data/cases_apx_core.txt (expected values) and
Emulator/data/cases_apx_core_hw.txt (hardware pairs).

Written from the Intel APX architecture specification 355828-009 (rev 9.0, August 2026) and the
Intel SDM text, not from any C implementation:

  REX2 (3.1.2.1, Figure 3.1)  D5H, then the payload M0 R4 X4 B4 W R3 X3 B3 (bit 7 .. 0). W/R3/
              X3/B3 mean what REX.W/R/X/B mean; R4/X4/B4 are bit 4 of the ModRM.reg, SIB.index
              and ModRM.r/m / SIB.base / opcode register ids, so all 32 GPRs are reachable. The
              next byte is the opcode in legacy map M0 (0 = one-byte, 1 = 0F map, no escape).
              With REX2, byte register ids 4-7 are SPL/BPL/SIL/DIL (never AH..BH). R4/X4/B4 are
              ignored when the id names a vector register (no XMM16-31) or when there is no
              index; R4 and R3 are ignored for a segment register; R4 is NOT ignored for a
              control/debug register (CR16+/DR16+ do not exist: #UD). RIP-relative (mod 00, r/m
              101) and "no base" (SIB base 101, mod 00) depend on the low three bits only; SIB
              index 100 is "no index" only with X4 = X3 = 0 (X4 = 1 -> R20/R28 are indexes).
  #UD         legacy map 0 rows 4x, 7x, Ax (A1 with W = 0 is JMPABS, APX part 3) and Ex; map 1
              rows 3x and 8x; after M0 = 0 the escape 0F and every prefix byte (66 67 F0 F2 F3
              2E 36 3E 26 64 65 4x C4 C5 62 D5); a REX right before REX2 (Table 3.1); XSAVE*/
              XRSTOR* with REX2; LOCK with a register destination (as without REX2); opcodes
              that #UD in 64-bit mode anyway (06 07 0E 16 17 1E 1F 27 2F 37 3F 60 61 82 9A CE D4
              D6). Legacy prefixes (66 67 F0 F2 F3, segment overrides) before REX2 keep their
              meaning.
  Enabling    (3.1.4.2.1, Table 3.8) REX2 only in 64-bit mode with CR4.OSXSAVE = 1 and
              XCR0[APX_F = 19] = 1, else #UD; outside 64-bit mode D5H is AAD.
  State       (3.1.4.3) CPUID.(7,1):EDX.APX_F[21]; leaf 29H (EAX 0, EBX[0] APX_NCI_NDD_NF);
              XSAVE component 19 = R16..R31 (8 bytes each, R16 first), 128 bytes at offset 960
              (3C0H) of the standard format (the MPX area: no CPU has MPX and APX), CPUID.(0DH,
              19) = 128 / 3C0H / ECX 0; compacted format: after the components below 19;
              XINUSE[19] = 0 when every EGPR is 0 (the emulator: value-based).
Instruction semantics (SDM Vol2): ADD/ADC/SUB/SBB/CMP/AND/OR/XOR/TEST/INC/DEC/NEG/NOT,
MOV/MOVZX/MOVSX/MOVSXD/LEA/XCHG/BSWAP/PUSH/POP, CMOVcc ("in 64-bit mode CMOVcc with a 32-bit
operand size clears the upper 32 bits even if the condition is false"), SETcc, IMUL/MUL/DIV,
BT/BTS/BTR/BTC, SHL/SHR/SAR/ROL/ROR, SHLD/SHRD, POPCNT/LZCNT/TZCNT/BSF/BSR, XADD, CMPXCHG,
MOVD/MOVQ/PMOVMSKB/PEXTRW/PINSRW/MOVMSKPS/MOVNTI/CVTSI2SD/CVTTSD2SI/MOVDQA/MOVDQU/PADDD,
FLD/FSTP m64. Flags the SDM leaves undefined are cleared by a "pushfq; and qword ptr [rsp], M;
popfq" suffix in the expected-value cases, so every expectation is SDM-defined; the hardware
comparison (--hwcmp) ignores them.

Usage:
  python ref_apx_core.py --selftest     hand-derived checks of the model, exit 0 on pass
  python ref_apx_core.py --cases        Emulator/data/cases_apx_core.txt (stdout)
  python ref_apx_core.py --evex         Emulator/data/cases_apx_evex.txt (stdout): the APX extension of
                                        EVEX instructions (U616), run with --apx --avx512
  python ref_apx_core.py --hwgen        Emulator/data/cases_apx_core_hw.txt (stdout): pairs
                                        "<legacy bytes> ~~ <REX2 bytes>" with registers R0-R15 only:
                                        the i5-13600K runs the legacy encoding, Unicorn (--apx)
                                        the REX2 one; both must give the same state
  python ref_apx_core.py --hwcmp LOG    the host results ("hw:" lines of the emu-alltest --cases
                                        log of the hardware file) against the model
"""
import random
import re
import struct
import sys

DATA = 0x30010000
MEM = 0x30020000
MEM_SIZE = 0x10000
MEM_PTR = MEM + 0x8000
MEM_DST = MEM + 0x9000
STACK_TOP = DATA + 0x3F00

NAMES = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"] + ["r%d" % i for i in range(8, 32)]
CF, PF, AF, ZF, SF, OF = 0x1, 0x4, 0x10, 0x40, 0x80, 0x800
STATUS = CF | PF | AF | ZF | SF | OF
M64 = (1 << 64) - 1


def mask(n):
    return (1 << n) - 1


def sx(v, n):
    v &= mask(n)
    return v - (1 << n) if v >> (n - 1) else v


def parity(v):
    return 1 if bin(v & 0xFF).count("1") % 2 == 0 else 0


def szp(res, n):
    f = 0
    if res & mask(n) == 0:
        f |= ZF
    if (res >> (n - 1)) & 1:
        f |= SF
    if parity(res):
        f |= PF
    return f


# --------------------------------------------------------------------------------------------
# machine state (the emu-alltest case defaults, at_cases.hpp case_default)
# --------------------------------------------------------------------------------------------
class St:
    def __init__(self):
        self.regs = [0] * 32
        self.regs[4] = STACK_TOP
        self.regs[6] = MEM_PTR
        self.regs[7] = MEM_DST
        self.regs[14] = MEM_PTR
        self.rflags = 0x202
        self.mem = bytearray(MEM_SIZE)
        self.xmm = [bytes(16)] * 16
        self.st7 = None        # x87 physical register reached by FLD/FSTP (FXSAVE ST7 slot)

    def copy(self):
        s = St()
        s.regs = list(self.regs)
        s.rflags = self.rflags
        s.mem = bytearray(self.mem)
        s.xmm = list(self.xmm)
        s.st7 = self.st7
        return s

    # memory at an absolute address (operand region only)
    def rd(self, a, n):
        o = a - MEM
        if o < 0 or o + n > MEM_SIZE:
            raise Fault(14)
        return int.from_bytes(self.mem[o:o + n], "little")

    def wr(self, a, n, v):
        o = a - MEM
        if o < 0 or o + n > MEM_SIZE:
            raise Fault(14)
        self.mem[o:o + n] = (v & mask(8 * n)).to_bytes(n, "little")

    # GPR by operand size; REX/REX2 present: byte ids 4-7 are SPL..DIL
    def getr(self, r, n):
        return self.regs[r] & mask(n)

    def setr(self, r, n, v):
        if n == 64:
            self.regs[r] = v & M64
        elif n == 32:
            self.regs[r] = v & mask(32)
        else:
            self.regs[r] = (self.regs[r] & ~mask(n) & M64) | (v & mask(n))


class Fault(Exception):
    def __init__(self, vec):
        Exception.__init__(self, vec)
        self.vec = vec


# --------------------------------------------------------------------------------------------
# operands
# --------------------------------------------------------------------------------------------
class Reg:
    def __init__(self, r):
        self.r = r


class Mem:
    """[base + index*scale + disp]; base None = no base (SIB base 101 / mod 00), 'rip' = RIP-
    relative (the model is given the absolute target and the bytes there), index None = none."""

    def __init__(self, base, index=None, scale=1, disp=0, asize=64):
        self.base, self.index, self.scale, self.disp, self.asize = base, index, scale, disp, asize

    def ea(self, st):
        a = self.disp
        if self.base is not None:
            a += st.regs[self.base]
        if self.index is not None:
            a += st.regs[self.index] * self.scale
        return a & mask(self.asize)


def rd_op(st, op, n):
    if isinstance(op, Reg):
        return st.getr(op.r, n)
    return st.rd(op.ea(st), n // 8)


def wr_op(st, op, n, v):
    if isinstance(op, Reg):
        st.setr(op.r, n, v)
    else:
        st.wr(op.ea(st), n // 8, v)


# --------------------------------------------------------------------------------------------
# encoder: REX2 and (registers 0-15 only) the legacy REX/no-REX form of the same instruction
# --------------------------------------------------------------------------------------------
class Enc:
    """One instruction: legacy prefixes, map (0 or 1), opcode, ModRM reg field (a register id
    0-31 or an opcode extension 0-7), r/m operand (Reg/Mem/None), W, immediate bytes, plus the
    opcode-embedded register (oreg) for 50+r, 58+r, 90+r, B0+r, B8+r, 0F C8+r."""

    def __init__(self, op, map1=False, pfx=(), reg=0, rm=None, w=0, imm=b"", oreg=None,
                 rex_needed=False, xbits=(0, 0, 0), b8=""):
        self.op, self.map1, self.pfx, self.reg, self.rm = op, map1, list(pfx), reg, rm
        self.w, self.imm, self.oreg, self.rex_needed = w, imm, oreg, rex_needed
        # byte registers in the ModRM.reg field ("reg"), in ModRM.r/m or the opcode ("rm"), or
        # both: ids 4-7 need a REX in the legacy form (else AH..BH; REX2 always means SPL..DIL)
        self.b8 = b8
        # extra ignored-bit settings (R4, X4, B4) forced to 1 for "ignored" tests
        self.xbits = xbits

    def _modrm(self):
        """-> (r, x, b) 5-bit ids used for the prefix and the ModRM/SIB/disp bytes"""
        reg = self.reg & 31
        if self.oreg is not None:
            return 0, 0, self.oreg, b""
        rm = self.rm
        if isinstance(rm, Reg):
            return reg, 0, rm.r, bytes([0xC0 | (reg & 7) << 3 | (rm.r & 7)])
        m = rm
        if m.base == "rip":
            return reg, 0, 0, bytes([(reg & 7) << 3 | 5]) + struct.pack("<i", m.disp)
        x = 0
        if m.base is None:
            # SIB with base 101 and mod 00: [index*scale + disp32]; the base id's low bits = 101
            idx = m.index if m.index is not None else 4
            x = idx
            ss = {1: 0, 2: 1, 4: 2, 8: 3}[m.scale]
            b = m.nobase_b if hasattr(m, "nobase_b") else 5
            return reg, x, b, bytes([(reg & 7) << 3 | 4, ss << 6 | (idx & 7) << 3 | 5]) + struct.pack("<i", m.disp)
        need_sib = m.index is not None or (m.base & 7) == 4
        if m.disp == 0 and (m.base & 7) != 5:
            mod, dsp = 0, b""
        elif -128 <= m.disp <= 127:
            mod, dsp = 1, struct.pack("<b", m.disp)
        else:
            mod, dsp = 2, struct.pack("<i", m.disp)
        if need_sib:
            idx = m.index if m.index is not None else (m.noidx if hasattr(m, "noidx") else 4)
            ss = {1: 0, 2: 1, 4: 2, 8: 3}[m.scale]
            return reg, idx, m.base, bytes([mod << 6 | (reg & 7) << 3 | 4, ss << 6 | (idx & 7) << 3 | (m.base & 7)]) + dsp
        return reg, 0, m.base, bytes([mod << 6 | (reg & 7) << 3 | (m.base & 7)]) + dsp

    def rex2(self, m0_override=None):
        r, x, b, tail = self._modrm()
        r4, x4, b4 = (r >> 4) & 1, (x >> 4) & 1, (b >> 4) & 1
        r4 |= self.xbits[0]
        x4 |= self.xbits[1]
        b4 |= self.xbits[2]
        m0 = (1 if self.map1 else 0) if m0_override is None else m0_override
        p = m0 << 7 | r4 << 6 | x4 << 5 | b4 << 4 | self.w << 3 | ((r >> 3) & 1) << 2 | ((x >> 3) & 1) << 1 | ((b >> 3) & 1)
        opc = self.op + ((b & 7) if self.oreg is not None else 0)
        return bytes(self.pfx) + bytes([0xD5, p, opc]) + tail + self.imm

    def legacy(self):
        r, x, b, tail = self._modrm()
        if (r | x | b) & 16:
            return None
        rex = 0x40 | self.w << 3 | ((r >> 3) & 1) << 2 | ((x >> 3) & 1) << 1 | ((b >> 3) & 1)
        opc = self.op + ((b & 7) if self.oreg is not None else 0)
        body = (b"\x0f" if self.map1 else b"") + bytes([opc]) + tail + self.imm
        need = self.rex_needed
        if "reg" in self.b8 and 4 <= r <= 7:
            need = True
        if "rm" in self.b8 and (self.oreg is not None or isinstance(self.rm, Reg)) and 4 <= b <= 7:
            need = True
        if rex != 0x40 or need:
            return bytes(self.pfx) + bytes([rex]) + body
        return bytes(self.pfx) + body


def dotbyte(b):
    return ".byte " + ", ".join("0x%02x" % c for c in b)


# --------------------------------------------------------------------------------------------
# instruction semantics (SDM Vol2 Operation sections)
# --------------------------------------------------------------------------------------------
ALU = ["add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"]


def alu_core(name, a, b, n, cf_in):
    """-> (result, flag bits, undefined flag mask)"""
    m = mask(n)
    if name in ("add", "adc"):
        c = cf_in if name == "adc" else 0
        full = a + b + c
        res = full & m
        f = szp(res, n)
        if full >> n:
            f |= CF
        if ((a ^ res) & (b ^ res)) >> (n - 1) & 1:
            f |= OF
        if (a ^ b ^ res) & 0x10:
            f |= AF
        return res, f, 0
    if name in ("sub", "sbb", "cmp"):
        c = cf_in if name == "sbb" else 0
        res = (a - b - c) & m
        f = szp(res, n)
        if a < b + c:
            f |= CF
        if ((a ^ b) & (a ^ res)) >> (n - 1) & 1:
            f |= OF
        if (a ^ b ^ res) & 0x10:
            f |= AF
        return res, f, 0
    res = {"and": a & b, "or": a | b, "xor": a ^ b, "test": a & b}[name]
    return res, szp(res, n), AF          # CF = OF = 0, AF undefined (SDM AND/OR/XOR/TEST)


def cond(cc, fl):
    c = (fl & CF) != 0
    z = (fl & ZF) != 0
    s = (fl & SF) != 0
    o = (fl & OF) != 0
    p = (fl & PF) != 0
    r = [o, c, z, c or z, s, p, s != o, z or (s != o)][cc >> 1]
    return r if not (cc & 1) else not r


def set_flags(st, bits, written):
    st.rflags = (st.rflags & ~written) | (bits & written)


class Ins:
    """An instruction (or a short sequence) with its encoding(s) and its semantics."""

    def __init__(self, encs, sem, undef=0, size=64, note=""):
        self.encs = encs if isinstance(encs, list) else [encs]
        self.sem = sem            # f(st) -> None (may raise Fault)
        self.undef = undef        # status flags the SDM leaves undefined
        self.note = note

    def rex2_bytes(self):
        return b"".join(e.rex2() for e in self.encs)

    def legacy_bytes(self):
        parts = [e.legacy() for e in self.encs]
        if any(p is None for p in parts):
            return None
        # every instruction but the last gets null 3EH prefixes (64-bit mode) up to its REX2
        # length, so the following instructions sit at the same addresses (x87 FIP)
        for i in range(len(parts) - 1):
            pad = len(self.encs[i].rex2()) - len(parts[i])
            parts[i] = bytes([0x3E] * max(pad, 0)) + parts[i]
        return b"".join(parts)


def i_alu(name, n, form, dst, src, imm=None):
    """form: 'EG' (op r/m, r), 'GE' (op r, r/m), 'EI' (80/81/83 r/m, imm), test: 'EG' / 'EI'"""
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    if name == "test":
        if form == "EG":
            e = Enc(0x84 if n == 8 else 0x85, pfx=pfx, reg=src.r, rm=dst, w=w, b8="reg,rm" if n == 8 else "")
        else:
            e = Enc(0xF6 if n == 8 else 0xF7, pfx=pfx, reg=0, rm=dst, w=w, imm=imm_bytes(imm, min(n, 32)), b8="reg,rm" if n == 8 else "")
    else:
        k = ALU.index(name)
        if form == "EG":
            e = Enc(k * 8 + (0 if n == 8 else 1), pfx=pfx, reg=src.r, rm=dst, w=w, b8="reg,rm" if n == 8 else "")
        elif form == "GE":
            e = Enc(k * 8 + (2 if n == 8 else 3), pfx=pfx, reg=dst.r, rm=src, w=w, b8="reg,rm" if n == 8 else "")
        elif form == "EI8":   # 83 /k ib (sign-extended)
            e = Enc(0x83, pfx=pfx, reg=k, rm=dst, w=w, imm=imm_bytes(imm, 8))
        else:
            e = Enc(0x80 if n == 8 else 0x81, pfx=pfx, reg=k, rm=dst, w=w, imm=imm_bytes(imm, min(n, 32)), b8="reg,rm" if n == 8 else "")

    def sem(st):
        a = rd_op(st, dst, n)
        if form in ("EG", "GE"):
            b = rd_op(st, src, n)
        elif form == "EI8":
            b = sx(imm, 8) & mask(n)
        else:
            b = (sx(imm, 32) if n == 64 else imm) & mask(n)
        res, f, u = alu_core(name, a, b, n, st.rflags & CF)
        if name not in ("cmp", "test"):
            wr_op(st, dst, n, res)
        set_flags(st, f, STATUS)
    u = AF if name in ("and", "or", "xor", "test") else 0
    return Ins(e, sem, u)


def imm_bytes(v, n):
    return (v & mask(n)).to_bytes(n // 8, "little")


def i_unary(name, n, op):
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    ext = {"inc": 0, "dec": 1, "not": 2, "neg": 3}[name]
    opc = (0xFE if n == 8 else 0xFF) if name in ("inc", "dec") else (0xF6 if n == 8 else 0xF7)
    e = Enc(opc, pfx=pfx, reg=ext, rm=op, w=w, b8="reg,rm" if n == 8 else "")

    def sem(st):
        a = rd_op(st, op, n)
        if name == "not":
            wr_op(st, op, n, ~a)
            return
        if name == "neg":
            res, f, _ = alu_core("sub", 0, a, n, 0)
            wr_op(st, op, n, res)
            set_flags(st, f, STATUS)
            return
        res, f, _ = alu_core("add" if name == "inc" else "sub", a, 1, n, 0)
        wr_op(st, op, n, res)
        set_flags(st, f, STATUS & ~CF)      # INC/DEC: CF unaffected
    return Ins(e, sem)


def i_mov(n, form, dst, src=None, imm=None):
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    if form == "EG":
        e = Enc(0x88 if n == 8 else 0x89, pfx=pfx, reg=src.r, rm=dst, w=w, b8="reg,rm" if n == 8 else "")
    elif form == "GE":
        e = Enc(0x8A if n == 8 else 0x8B, pfx=pfx, reg=dst.r, rm=src, w=w, b8="reg,rm" if n == 8 else "")
    elif form == "EI":
        e = Enc(0xC6 if n == 8 else 0xC7, pfx=pfx, reg=0, rm=dst, w=w, imm=imm_bytes(imm, min(n, 32)), b8="reg,rm" if n == 8 else "")
    else:   # 'OI': B0+r / B8+r (imm64 with W)
        e = Enc(0xB0 if n == 8 else 0xB8, pfx=pfx, oreg=dst.r, w=w, imm=imm_bytes(imm, n), b8="reg,rm" if n == 8 else "")

    def sem(st):
        if form in ("EG", "GE"):
            v = rd_op(st, src, n)
        elif form == "EI":
            v = (sx(imm, 32) if n == 64 else imm)
        else:
            v = imm
        wr_op(st, dst, n, v)
    return Ins(e, sem)


def i_movx(kind, n, dst, src, sn):
    """movzx/movsx Gv, Eb/Ew (0F B6/B7/BE/BF), movsxd Gq, Ed (63 + W)"""
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    if kind == "movsxd":
        e = Enc(0x63, reg=dst.r, rm=src, w=1)
    else:
        e = Enc({("zx", 8): 0xB6, ("zx", 16): 0xB7, ("sx", 8): 0xBE, ("sx", 16): 0xBF}[(kind, sn)],
                map1=True, pfx=pfx, reg=dst.r, rm=src, w=w, b8="rm" if sn == 8 else "")

    def sem(st):
        v = rd_op(st, src, sn)
        if kind in ("sx", "movsxd"):
            v = sx(v, sn)
        st.setr(dst.r, n, v)
    return Ins(e, sem)


def i_lea(n, dst, m):
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    e = Enc(0x8D, pfx=pfx + ([0x67] if m.asize == 32 else []), reg=dst.r, rm=m, w=w)

    def sem(st):
        st.setr(dst.r, n, m.ea(st))
    return Ins(e, sem)


def i_xchg(n, a, b):
    """87/86 r/m, r (b may be memory)"""
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    e = Enc(0x86 if n == 8 else 0x87, pfx=pfx, reg=a.r, rm=b, w=w, b8="reg,rm" if n == 8 else "")

    def sem(st):
        x, y = st.getr(a.r, n), rd_op(st, b, n)
        wr_op(st, b, n, x)
        st.setr(a.r, n, y)
    return Ins(e, sem)


def i_xchg_ax(n, r):
    """90+r: xchg rAX, r (r != 0)"""
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    e = Enc(0x90, pfx=pfx, oreg=r, w=w)

    def sem(st):
        x, y = st.getr(0, n), st.getr(r, n)
        st.setr(0, n, y)
        st.setr(r, n, x)
    return Ins(e, sem)


def i_bswap(n, r):
    e = Enc(0xC8, map1=True, oreg=r, w=1 if n == 64 else 0)

    def sem(st):
        v = st.getr(r, n)
        st.setr(r, n, int.from_bytes(v.to_bytes(n // 8, "little"), "big"))
    return Ins(e, sem)


def i_pushpop(src, dst, ppx=0):
    """push src (50+r); pop dst (58+r); REX2.W = 1 is the PPX hint (3.1.3.1.2): same semantics"""
    e1 = Enc(0x50, oreg=src, w=ppx)
    e2 = Enc(0x58, oreg=dst, w=ppx)

    def sem(st):
        v = st.regs[src]
        st.regs[dst] = v
    return Ins([e1, e2], sem)


def i_push_pop_rm(src, dst):
    """FF /6 push r/m64; 8F /0 pop r/m64 (operands Reg or Mem)"""
    e1 = Enc(0xFF, reg=6, rm=src)
    e2 = Enc(0x8F, reg=0, rm=dst)

    def sem(st):
        v = rd_op(st, src, 64)
        # POP r/m with an RSP-based address uses RSP after the pop; not generated here
        wr_op(st, dst, 64, v)
    return Ins([e1, e2], sem)


def i_cmov(cc, n, dst, src):
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    e = Enc(0x40 + cc, map1=True, pfx=pfx, reg=dst.r, rm=src, w=w)

    def sem(st):
        v = rd_op(st, src, n)     # the source is read (and may fault) whatever the condition
        if cond(cc, st.rflags):
            st.setr(dst.r, n, v)
        elif n == 32:
            st.setr(dst.r, 32, st.getr(dst.r, 32))
    return Ins(e, sem)


def i_setcc(cc, op):
    e = Enc(0x90 + cc, map1=True, reg=0, rm=op, b8="rm")

    def sem(st):
        wr_op(st, op, 8, 1 if cond(cc, st.rflags) else 0)
    return Ins(e, sem)


def i_imul2(n, dst, src):
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    e = Enc(0xAF, map1=True, pfx=pfx, reg=dst.r, rm=src, w=w)

    def sem(st):
        a, b = sx(st.getr(dst.r, n), n), sx(rd_op(st, src, n), n)
        p = a * b
        res = p & mask(n)
        st.setr(dst.r, n, res)
        set_flags(st, (CF | OF) if sx(res, n) != p else 0, CF | OF)
    return Ins(e, sem, SF | ZF | AF | PF)


def i_imul3(n, dst, src, imm, short):
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    e = Enc(0x6B if short else 0x69, pfx=pfx, reg=dst.r, rm=src, w=w,
            imm=imm_bytes(imm, 8 if short else min(n, 32)))

    def sem(st):
        a = sx(rd_op(st, src, n), n)
        b = sx(imm, 8 if short else min(n, 32))
        p = a * b
        res = p & mask(n)
        st.setr(dst.r, n, res)
        set_flags(st, (CF | OF) if sx(res, n) != p else 0, CF | OF)
    return Ins(e, sem, SF | ZF | AF | PF)


def i_muldiv(name, n, op):
    """F7 /4 mul, /5 imul, /6 div, /7 idiv (n = 32/64)"""
    w = 1 if n == 64 else 0
    e = Enc(0xF7, reg={"mul": 4, "imul": 5, "div": 6, "idiv": 7}[name], rm=op, w=w)

    def sem(st):
        s = rd_op(st, op, n)
        lo, hi = st.getr(0, n), st.getr(2, n)
        if name in ("mul", "imul"):
            if name == "mul":
                p = lo * s
                over = (p >> n) != 0
            else:
                p = sx(lo, n) * sx(s, n)
                over = sx(p & mask(n), n) != p
            st.setr(0, n, p)
            st.setr(2, n, p >> n)
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
        st.setr(0, n, q)
        st.setr(2, n, r)
    und = (SF | ZF | AF | PF) if name in ("mul", "imul") else STATUS
    return Ins(e, sem, und)


def i_bt(name, n, dst, src=None, imm=None):
    """0F A3/AB/B3/BB r/m, r (register r/m only); 0F BA /4-7 r/m, ib"""
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    k = ["bt", "bts", "btr", "btc"].index(name)
    if imm is None:
        e = Enc([0xA3, 0xAB, 0xB3, 0xBB][k], map1=True, pfx=pfx, reg=src.r, rm=dst, w=w)
    else:
        e = Enc(0xBA, map1=True, pfx=pfx, reg=4 + k, rm=dst, w=w, imm=bytes([imm & 0xFF]))

    def sem(st):
        bit = (st.getr(src.r, n) if imm is None else imm) % n
        v = rd_op(st, dst, n)
        set_flags(st, CF if (v >> bit) & 1 else 0, CF)
        if name == "bts":
            wr_op(st, dst, n, v | (1 << bit))
        elif name == "btr":
            wr_op(st, dst, n, v & ~(1 << bit))
        elif name == "btc":
            wr_op(st, dst, n, v ^ (1 << bit))
    return Ins(e, sem, OF | SF | AF | PF)


def i_shift(name, n, op, how, cnt):
    """D1 /e (by 1), C1 /e ib, D3 /e (by CL); name rol ror shl shr sar. Counts whose masked
    value is 0 and, for 8/16-bit shifts, counts >= the operand size are not generated."""
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    ext = {"rol": 0, "ror": 1, "shl": 4, "shr": 5, "sar": 7}[name]
    if how == "1":
        e = Enc(0xD0 if n == 8 else 0xD1, pfx=pfx, reg=ext, rm=op, w=w, b8="reg,rm" if n == 8 else "")
    elif how == "ib":
        e = Enc(0xC0 if n == 8 else 0xC1, pfx=pfx, reg=ext, rm=op, w=w, imm=bytes([cnt]), b8="reg,rm" if n == 8 else "")
    else:
        e = Enc(0xD2 if n == 8 else 0xD3, pfx=pfx, reg=ext, rm=op, w=w, b8="reg,rm" if n == 8 else "")

    def sem(st):
        c = 1 if how == "1" else (cnt if how == "ib" else st.regs[1] & 0xFF)
        c &= 63 if n == 64 else 31
        v = rd_op(st, op, n)
        msb = lambda x: (x >> (n - 1)) & 1
        if name in ("rol", "ror"):
            t = c % n
            if name == "rol":
                res = ((v << t) | (v >> (n - t))) & mask(n) if t else v
                cf = res & 1
                of = msb(res) ^ cf
            else:
                res = ((v >> t) | (v << (n - t))) & mask(n) if t else v
                cf = msb(res)
                of = msb(res) ^ ((res >> (n - 2)) & 1)
            wr_op(st, op, n, res)
            set_flags(st, (CF if cf else 0) | (OF if of else 0), CF | OF)
            return
        if name == "shl":
            res = (v << c) & mask(n)
            cf = (v >> (n - c)) & 1
            of = msb(res) ^ cf
        elif name == "shr":
            res = v >> c
            cf = (v >> (c - 1)) & 1
            of = msb(v)
        else:
            res = (sx(v, n) >> c) & mask(n)
            cf = (sx(v, n) >> (c - 1)) & 1
            of = 0
        wr_op(st, op, n, res)
        set_flags(st, szp(res, n) | (CF if cf else 0) | (OF if of else 0), CF | OF | SF | ZF | PF)
    c0 = 1 if how == "1" else cnt
    und = AF
    if name in ("rol", "ror"):
        und = 0
    if how != "1" and not (how == "ib" and (cnt & (63 if n == 64 else 31)) == 1):
        und |= OF
    return Ins(e, sem, und)


def i_shxd(name, n, dst, src, how, cnt=None):
    """SHLD/SHRD r/m, r, ib|CL (0F A4/A5/AC/AD), n = 16/32/64, count < n"""
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    opc = {("shld", "ib"): 0xA4, ("shld", "cl"): 0xA5, ("shrd", "ib"): 0xAC, ("shrd", "cl"): 0xAD}[(name, how)]
    e = Enc(opc, map1=True, pfx=pfx, reg=src.r, rm=dst, w=w, imm=bytes([cnt]) if how == "ib" else b"")

    def sem(st):
        c = (cnt if how == "ib" else st.regs[1] & 0xFF) & (63 if n == 64 else 31)
        d, s = rd_op(st, dst, n), st.getr(src.r, n)
        if name == "shld":
            res = ((d << c) | (s >> (n - c))) & mask(n)
            cf = (d >> (n - c)) & 1
        else:
            res = ((d >> c) | (s << (n - c))) & mask(n)
            cf = (d >> (c - 1)) & 1
        of = ((res ^ d) >> (n - 1)) & 1
        wr_op(st, dst, n, res)
        set_flags(st, szp(res, n) | (CF if cf else 0) | (OF if of else 0), CF | OF | SF | ZF | PF)
    c0 = cnt if how == "ib" else None
    und = AF | (0 if c0 == 1 else OF)
    return Ins(e, sem, und)


def i_bitcount(name, n, dst, src):
    """F3 0F B8 popcnt, F3 0F BD lzcnt, F3 0F BC tzcnt, 0F BC bsf, 0F BD bsr (src != 0 for bsf/bsr)"""
    w = 1 if n == 64 else 0
    pfx = ([0x66] if n == 16 else []) + ([0xF3] if name in ("popcnt", "lzcnt", "tzcnt") else [])
    opc = {"popcnt": 0xB8, "lzcnt": 0xBD, "tzcnt": 0xBC, "bsf": 0xBC, "bsr": 0xBD}[name]
    e = Enc(opc, map1=True, pfx=pfx, reg=dst.r, rm=src, w=w)

    def sem(st):
        v = rd_op(st, src, n)
        if name == "popcnt":
            res = bin(v).count("1")
            st.setr(dst.r, n, res)
            set_flags(st, ZF if v == 0 else 0, STATUS)
        elif name == "lzcnt":
            res = n - v.bit_length()
            st.setr(dst.r, n, res)
            set_flags(st, (CF if v == 0 else 0) | (ZF if res == 0 else 0), CF | ZF)
        elif name == "tzcnt":
            res = n if v == 0 else (v & -v).bit_length() - 1
            st.setr(dst.r, n, res)
            set_flags(st, (CF if v == 0 else 0) | (ZF if res == 0 else 0), CF | ZF)
        else:
            res = (v & -v).bit_length() - 1 if name == "bsf" else v.bit_length() - 1
            st.setr(dst.r, n, res)
            set_flags(st, 0, ZF)
    und = {"popcnt": 0, "lzcnt": OF | SF | PF | AF, "tzcnt": OF | SF | PF | AF,
           "bsf": CF | OF | SF | AF | PF, "bsr": CF | OF | SF | AF | PF}[name]
    return Ins(e, sem, und)


def i_xadd(n, dst, src):
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    e = Enc(0xC0 if n == 8 else 0xC1, map1=True, pfx=pfx, reg=src.r, rm=dst, w=w, b8="reg,rm" if n == 8 else "")

    def sem(st):
        d, s = rd_op(st, dst, n), st.getr(src.r, n)
        res, f, _ = alu_core("add", d, s, n, 0)
        st.setr(src.r, n, d)
        wr_op(st, dst, n, res)
        set_flags(st, f, STATUS)
    return Ins(e, sem)


def i_cmpxchg(n, dst, src):
    """0F B0/B1 r/m, r; 32-bit only with a memory destination (see the module text)"""
    w = 1 if n == 64 else 0
    pfx = [0x66] if n == 16 else []
    e = Enc(0xB0 if n == 8 else 0xB1, map1=True, pfx=pfx, reg=src.r, rm=dst, w=w, b8="reg,rm" if n == 8 else "")

    def sem(st):
        acc, d = st.getr(0, n), rd_op(st, dst, n)
        _, f, _ = alu_core("cmp", acc, d, n, 0)
        if acc == d:
            wr_op(st, dst, n, st.getr(src.r, n))
        else:
            if not isinstance(dst, Reg):
                wr_op(st, dst, n, d)     # memory: the old value is written back
            st.setr(0, n, d)
        set_flags(st, f, STATUS)
    return Ins(e, sem)


# ---- SSE forms with a GPR (EGPR) operand or an EGPR address --------------------------------
def xmm_get(st, i):
    return int.from_bytes(st.xmm[i & 15], "little")


def xmm_set(st, i, v):
    st.xmm[i & 15] = (v & mask(128)).to_bytes(16, "little")


def i_movd_to_xmm(n, x, src):
    """66 0F 6E /r: MOVD xmm, r/m32 / MOVQ xmm, r/m64 (W)"""
    e = Enc(0x6E, map1=True, pfx=[0x66], reg=x, rm=src, w=1 if n == 64 else 0)

    def sem(st):
        xmm_set(st, x, rd_op(st, src, n))
    return Ins(e, sem)


def i_movd_from_xmm(n, dst, x):
    """66 0F 7E /r: MOVD r/m32, xmm / MOVQ r/m64, xmm (W)"""
    e = Enc(0x7E, map1=True, pfx=[0x66], reg=x, rm=dst, w=1 if n == 64 else 0)

    def sem(st):
        wr_op(st, dst, n, xmm_get(st, x))
    return Ins(e, sem)


def i_pmovmskb(dst, x, w=0):
    e = Enc(0xD7, map1=True, pfx=[0x66], reg=dst, rm=Reg(x), w=w)

    def sem(st):
        b = st.xmm[x & 15]
        st.setr(dst, 64, sum(((b[i] >> 7) & 1) << i for i in range(16)))
    return Ins(e, sem)


def i_movmskps(dst, x):
    e = Enc(0x50, map1=True, reg=dst, rm=Reg(x))

    def sem(st):
        v = xmm_get(st, x)
        st.setr(dst, 64, sum(((v >> (32 * i + 31)) & 1) << i for i in range(4)))
    return Ins(e, sem)


def i_pextrw(dst, x, imm):
    e = Enc(0xC5, map1=True, pfx=[0x66], reg=dst, rm=Reg(x), imm=bytes([imm]))

    def sem(st):
        st.setr(dst, 64, (xmm_get(st, x) >> (16 * (imm & 7))) & 0xFFFF)
    return Ins(e, sem)


def i_pinsrw(x, src, imm):
    e = Enc(0xC4, map1=True, pfx=[0x66], reg=x, rm=src, imm=bytes([imm]))

    def sem(st):
        v = rd_op(st, src, 16) if not isinstance(src, Reg) else st.getr(src.r, 16)
        k = 16 * (imm & 7)
        xmm_set(st, x, (xmm_get(st, x) & ~(0xFFFF << k)) | (v << k))
    return Ins(e, sem)


def i_movnti(n, m, src):
    e = Enc(0xC3, map1=True, reg=src, rm=m, w=1 if n == 64 else 0)

    def sem(st):
        wr_op(st, m, n, st.getr(src, n))
    return Ins(e, sem)


def i_cvtsi2sd(x, src):
    """F2 REX.W 0F 2A: CVTSI2SD xmm, r/m64 (exactly representable integers only)"""
    e = Enc(0x2A, map1=True, pfx=[0xF2], reg=x, rm=src, w=1)

    def sem(st):
        v = sx(rd_op(st, src, 64), 64)
        bits = struct.unpack("<Q", struct.pack("<d", float(v)))[0]
        xmm_set(st, x, (xmm_get(st, x) & ~mask(64)) | bits)
    return Ins(e, sem)


def i_cvttsd2si(dst, x):
    """F2 REX.W 0F 2C: CVTTSD2SI r64, xmm (integral doubles in range only)"""
    e = Enc(0x2C, map1=True, pfx=[0xF2], reg=dst, rm=Reg(x), w=1)

    def sem(st):
        d = struct.unpack("<d", struct.pack("<Q", xmm_get(st, x) & mask(64)))[0]
        st.setr(dst, 64, int(d))
    return Ins(e, sem)


def i_movdqa_rr(xd, xs, xbits=(0, 0, 0)):
    """66 0F 6F /r MOVDQA xmm, xmm; xbits forces R4/X4/B4 = 1 (ignored for XMM registers)"""
    e = Enc(0x6F, map1=True, pfx=[0x66], reg=xd, rm=Reg(xs), xbits=xbits)

    def sem(st):
        xmm_set(st, xd, xmm_get(st, xs))
    return Ins(e, sem)


def i_movdqu(load, x, m):
    e = Enc(0x6F if load else 0x7F, map1=True, pfx=[0xF3], reg=x, rm=m)

    def sem(st):
        if load:
            xmm_set(st, x, st.rd(m.ea(st), 16))
        else:
            st.wr(m.ea(st), 16, xmm_get(st, x))
    return Ins(e, sem)


def i_paddd_m(x, m):
    e = Enc(0xFE, map1=True, pfx=[0x66], reg=x, rm=m)

    def sem(st):
        a = st.xmm[x & 15]
        ea = m.ea(st)
        if ea & 15:
            raise Fault(13)
        b = st.rd(ea, 16).to_bytes(16, "little")
        r = b""
        for i in range(4):
            r += ((int.from_bytes(a[4 * i:4 * i + 4], "little") + int.from_bytes(b[4 * i:4 * i + 4], "little")) & mask(32)).to_bytes(4, "little")
        st.xmm[x & 15] = r
    return Ins(e, sem)


def ext80(bits64):
    """double bits -> (sign/exponent word, mantissa) of the x87 80-bit format (normal values)"""
    s = bits64 >> 63
    ex = (bits64 >> 52) & 0x7FF
    fr = bits64 & mask(52)
    assert 0 < ex < 0x7FF
    return (s << 15) | (ex - 1023 + 16383), (1 << 63) | (fr << 11)


def i_fld_fstp(src, dst):
    """DD /0 FLD m64; DD /3 FSTP m64: the double is copied; the popped physical register keeps
    the value (FXSAVE slot ST7 after the pop)"""
    e1 = Enc(0xDD, reg=0, rm=src)
    e2 = Enc(0xDD, reg=3, rm=dst)

    def sem(st):
        v = st.rd(src.ea(st), 8)
        st.wr(dst.ea(st), 8, v)
        st.st7 = ext80(v)
    return Ins([e1, e2], sem)


# --------------------------------------------------------------------------------------------
# case text
# --------------------------------------------------------------------------------------------
def fmt_mem_runs(old, new):
    out = []
    a = 0
    while a < MEM_SIZE:
        if old[a] == new[a]:
            a += 1
            continue
        b = a
        while b < MEM_SIZE and b - a < 32 and old[b] != new[b]:
            b += 1
        out.append("m+0x%X=%s" % (a, new[a:b].hex().upper()))
        a = b
    return out


def fmt_inputs(st, regs_set, mem_runs, xmm_set_=(), extra=()):
    f = []
    for r in sorted(regs_set):
        f.append("%s=0x%X" % (NAMES[r], st.regs[r]))
    if st.rflags != 0x202:
        f.append("rflags=0x%X" % st.rflags)
    for off, b in mem_runs:
        f.append("m+0x%X=%s" % (off, b.hex().upper()))
    for i in xmm_set_:
        f.append("xmm%d=%s" % (i, st.xmm[i].hex().upper()))
    f.extend(extra)
    return " ".join(f)


def fmt_changes(a, b, undef=0, egpr_ok=True):
    """the fields of state b that differ from a (expected-value syntax)"""
    f = []
    for r in range(32):
        if a.regs[r] != b.regs[r]:
            f.append("%s=0x%X" % (NAMES[r], b.regs[r]))
    fa, fb = a.rflags, b.rflags & ~undef
    if fa != fb:
        f.append("rflags=0x%X" % fb)
    for i in range(16):
        if a.xmm[i] != b.xmm[i]:
            f.append("xmm%d=%s" % (i, b.xmm[i].hex().upper()))
    if b.st7 is not None and b.st7 != a.st7:
        f.append("st7=%04X:%016X" % b.st7)
        f.append("ftw=0x0")     # the popped register is empty: the st7 key sets its abridged
                                #   tag bit, clear it again (no other x87 register is used)
    f.extend(fmt_mem_runs(a.mem, b.mem))
    return " ".join(f)


def flag_mask_suffix(undef):
    """clear the SDM-undefined status flags of the result (stack bytes are not checked)"""
    if not undef:
        return ""
    return "; pushfq; and qword ptr [rsp], %d; popfq" % (~undef if True else 0)


class Case:
    def __init__(self, ins, st, regs_set=(), mem_runs=(), xmm_in=(), title=None):
        self.ins, self.st, self.regs_set, self.mem_runs, self.xmm_in = ins, st, set(regs_set), list(mem_runs), list(xmm_in)
        self.title = title

    def model(self):
        out = self.st.copy()
        try:
            self.ins.sem(out)
            return out, None
        except Fault as f:
            return self.st.copy(), f.vec

    def line_rex2(self):
        out, fault = self.model()
        text = dotbyte(self.ins.rex2_bytes()) + (flag_mask_suffix(self.ins.undef) if fault is None else "")
        inp = fmt_inputs(self.st, self.regs_set, self.mem_runs, self.xmm_in)
        exp = (("#%s" % {0: "DE", 13: "GP", 14: "PF", 6: "UD"}[fault]) if fault is not None
               else fmt_changes(self.st, out, self.ins.undef))
        return "%s | %s => %s" % (text, inp, exp)

    def line_pair(self):
        leg = self.ins.legacy_bytes()
        if leg is None:
            return None
        inp = fmt_inputs(self.st, self.regs_set, self.mem_runs, self.xmm_in)
        sfx = flag_mask_suffix(self.ins.undef)     # SDM-undefined flags: cleared on both sides
        return "%s%s ~~ %s%s | %s" % (dotbyte(leg), sfx, dotbyte(self.ins.rex2_bytes()), sfx, inp)


def mk_state(rng, regs, flags=True):
    st = St()
    for r in regs:
        st.regs[r] = rng.choice([rng.getrandbits(64), rng.getrandbits(8), rng.getrandbits(32),
                                 (1 << 63) | rng.getrandbits(8), M64, 0x7FFFFFFF, 0x80000000])
    if flags:
        st.rflags = 0x202 | (rng.getrandbits(12) & STATUS)
    return st


def mem_at(rng, st, base, index, scale, nbytes, align=1):
    """choose register values so that [base + index*scale + disp] is MEM + off (off in the
    upper half, past MEM_PTR's default data), fill nbytes there; -> (Mem, run)"""
    off = 0xA000 + (rng.randrange(0, 0x4000) & ~(align - 1))
    disp = rng.choice([0, rng.randrange(-128, 128), rng.randrange(-0x800, 0x800)])
    if align > 1:
        disp &= ~(align - 1)
    a = MEM + off
    if index is not None:
        iv = rng.randrange(0, 0x100)
        st.regs[index] = iv
        a -= iv * scale
    st.regs[base] = (a - disp) & M64
    m = Mem(base, index, scale, disp)
    data = bytes(rng.getrandbits(8) for _ in range(nbytes))
    st.mem[off:off + nbytes] = data
    return m, (off, data)


# --------------------------------------------------------------------------------------------
# generators
# --------------------------------------------------------------------------------------------
GP_OK = [r for r in range(32) if r != 4]          # no RSP as a data register
EGPR = list(range(16, 32))
LOW = [r for r in range(16) if r != 4]


def pick(rng, pool, k):
    return rng.sample(pool, k)


def gen_items(rng, egpr):
    """yield Case objects; egpr=True: at least one register of each instruction is R16-R31 (the
    expected-value file), False: registers 0-15 only (the hardware-pair file)"""
    pool = GP_OK if egpr else LOW
    def regs(k):
        if not egpr:
            return pick(rng, pool, k)
        e = rng.choice(EGPR)
        return [e] + pick(rng, [r for r in pool if r != e], k - 1)

    def shuffled(v):
        v = list(v)
        rng.shuffle(v)
        return v
    sizes = [8, 16, 32, 64]
    # ALU, register forms and memory forms
    for name in ALU + ["test"]:
        for n in sizes:
            for form in (["EG", "GE", "EI", "EI8"] if name != "test" else ["EG", "EI"]):
                if form == "EI8" and n == 8:
                    continue
                for memf in (False, True):
                    a, b, base, idx = shuffled(regs(4))
                    st = mk_state(rng, [a, b])
                    imm = rng.getrandbits(8 if form == "EI8" else min(n, 32))
                    runs = []
                    if memf:
                        m, run = mem_at(rng, st, base, idx if rng.random() < 0.6 else None, rng.choice([1, 2, 4, 8]), 8)
                        runs.append(run)
                        dst, src = (m, Reg(b)) if form != "GE" else (Reg(a), m)
                        used = [b] if form != "GE" else [a]
                        used += [base] + ([m.index] if m.index is not None else [])
                    else:
                        dst, src = Reg(a), Reg(b)
                        used = [a, b]
                    yield Case(i_alu(name, n, form, dst, src, imm), st, used, runs)
    for name in ("inc", "dec", "not", "neg"):
        for n in sizes:
            for memf in (False, True):
                a, base, idx = regs(3)
                st = mk_state(rng, [a])
                if memf:
                    m, run = mem_at(rng, st, base, idx, rng.choice([1, 2, 4, 8]), 8)
                    yield Case(i_unary(name, n, m), st, [base, idx], [run])
                else:
                    yield Case(i_unary(name, n, Reg(a)), st, [a])
    # MOV family
    for n in sizes:
        for form in ("EG", "GE", "EI", "OI"):
            a, b, base, idx = shuffled(regs(4))
            st = mk_state(rng, [a, b])
            imm = rng.getrandbits(n if form == "OI" else min(n, 32))
            yield Case(i_mov(n, form, Reg(a), Reg(b), imm), st, [a, b])
            if form != "OI":
                m, run = mem_at(rng, st, base, idx, 4, 8)
                if form == "GE":
                    yield Case(i_mov(n, form, Reg(a), m, imm), st, [a, base, idx], [run])
                else:
                    yield Case(i_mov(n, form, m, Reg(b), imm), st, [b, base, idx], [run])
    for kind, sn in (("zx", 8), ("zx", 16), ("sx", 8), ("sx", 16), ("movsxd", 32)):
        for n in ((16, 32, 64) if kind != "movsxd" else (64,)):
            if sn == 16 and n == 16:
                continue
            a, b, base, idx = shuffled(regs(4))
            st = mk_state(rng, [a, b])
            yield Case(i_movx(kind, n, Reg(a), Reg(b), sn), st, [a, b])
            m, run = mem_at(rng, st, base, idx, 2, 4)
            yield Case(i_movx(kind, n, Reg(a), m, sn), st, [a, base, idx], [run])
    for n in (16, 32, 64):
        for asz in (64, 32):
            a, base, idx = shuffled(regs(3))
            st = mk_state(rng, [a, base, idx])
            m = Mem(base, idx, rng.choice([1, 2, 4, 8]), rng.randrange(-0x10000, 0x10000), asz)
            yield Case(i_lea(n, Reg(a), m), st, [a, base, idx])
    for n in sizes:
        a, b, base, idx = shuffled(regs(4))
        st = mk_state(rng, [a, b])
        yield Case(i_xchg(n, Reg(a), Reg(b)), st, [a, b])
        m, run = mem_at(rng, st, base, idx, 8, 8)
        yield Case(i_xchg(n, Reg(a), m), st, [a, base, idx], [run])
    for n in (16, 32, 64):
        r = regs(1)[0] if egpr else rng.choice([x for x in LOW if x != 0])
        st = mk_state(rng, [0, r])
        yield Case(i_xchg_ax(n, r), st, [0, r])
    for n in (32, 64):
        r = regs(1)[0]
        st = mk_state(rng, [r])
        yield Case(i_bswap(n, r), st, [r])
    for ppx in (0, 1):
        a, b = shuffled(regs(2))
        st = mk_state(rng, [a, b])
        yield Case(i_pushpop(a, b, ppx), st, [a, b])
    a, base, idx = regs(3)
    st = mk_state(rng, [a])
    m, run = mem_at(rng, st, base, idx, 4, 8)
    yield Case(i_push_pop_rm(Reg(a), m), st, [a, base, idx], [run])
    # CMOVcc / SETcc
    for cc in range(16):
        n = rng.choice([16, 32, 64])
        a, b, base, idx = shuffled(regs(4))
        st = mk_state(rng, [a, b])
        yield Case(i_cmov(cc, n, Reg(a), Reg(b)), st, [a, b])
        m, run = mem_at(rng, st, base, idx, 1, 8)
        yield Case(i_cmov(cc, n, Reg(a), m), st, [a, base, idx], [run])
        st = mk_state(rng, [a])
        yield Case(i_setcc(cc, Reg(a)), st, [a])
        m, run = mem_at(rng, st, base, idx, 2, 1)
        yield Case(i_setcc(cc, m), st, [base, idx], [run])
    # IMUL / MUL / DIV
    for n in (16, 32, 64):
        a, b, base, idx = shuffled(regs(4))
        st = mk_state(rng, [a, b])
        yield Case(i_imul2(n, Reg(a), Reg(b)), st, [a, b])
        yield Case(i_imul3(n, Reg(a), Reg(b), rng.getrandbits(8), True), st, [a, b])
        yield Case(i_imul3(n, Reg(a), Reg(b), rng.getrandbits(min(n, 32)), False), st, [a, b])
        m, run = mem_at(rng, st, base, idx, 8, 8)
        yield Case(i_imul2(n, Reg(a), m), st, [a, base, idx], [run])
    for name in ("mul", "imul", "div", "idiv"):
        for n in (32, 64):
            r = regs(1)[0] if egpr else rng.choice([x for x in LOW if x not in (0, 2)])
            st = mk_state(rng, [0, 2, r])
            if name in ("div", "idiv"):
                st.regs[r] |= 1 << rng.randrange(0, n)
                st.regs[2] = rng.getrandbits(8) if name == "div" else 0
                if name == "idiv":
                    st.regs[0] = rng.getrandbits(n - 1)
            yield Case(i_muldiv(name, n, Reg(r)), st, [0, 2, r])
    if egpr:
        r = regs(1)[0]
        st = mk_state(rng, [0, 2])
        st.regs[r] = 0
        yield Case(i_muldiv("div", 64, Reg(r)), st, [0, 2, r])    # #DE
    # BT family
    for name in ("bt", "bts", "btr", "btc"):
        for n in (16, 32, 64):
            a, b, base, idx = shuffled(regs(4))
            st = mk_state(rng, [a, b])
            yield Case(i_bt(name, n, Reg(a), Reg(b)), st, [a, b])
            m, run = mem_at(rng, st, base, idx, 4, 8)
            yield Case(i_bt(name, n, m, imm=rng.getrandbits(8)), st, [base, idx], [run])
    # shifts / rotates
    for name in ("rol", "ror", "shl", "shr", "sar"):
        for n in sizes:
            for how in ("1", "ib", "cl"):
                a = regs(1)[0] if egpr else rng.choice([x for x in LOW if x != 1])
                if a == 1:
                    continue
                st = mk_state(rng, [a])
                lim = n if n >= 32 else n
                cnt = rng.randrange(1, lim)
                regs_in = [a]
                if how == "cl":
                    st.regs[1] = cnt | (rng.getrandbits(2) << 6 if n == 64 else 0) if n >= 32 else cnt
                    regs_in.append(1)
                yield Case(i_shift(name, n, Reg(a), how, cnt), st, regs_in)
    for name in ("shld", "shrd"):
        for n in (16, 32, 64):
            for how in ("ib", "cl"):
                a, b = shuffled(regs(2)) if egpr else rng.sample([x for x in LOW if x != 1], 2)
                if 1 in (a, b):
                    continue
                st = mk_state(rng, [a, b])
                cnt = rng.randrange(1, n)
                regs_in = [a, b]
                if how == "cl":
                    st.regs[1] = cnt
                    regs_in.append(1)
                yield Case(i_shxd(name, n, Reg(a), Reg(b), how, cnt), st, regs_in)
    for name in ("popcnt", "lzcnt", "tzcnt", "bsf", "bsr"):
        for n in (16, 32, 64):
            a, b, base, idx = shuffled(regs(4))
            st = mk_state(rng, [a, b])
            if name in ("bsf", "bsr") and st.getr(b, n) == 0:
                st.regs[b] |= 1 << rng.randrange(0, n)
            yield Case(i_bitcount(name, n, Reg(a), Reg(b)), st, [a, b])
            m, run = mem_at(rng, st, base, idx, 2, 8)
            if name in ("bsf", "bsr") and st.rd(m.ea(st), n // 8) == 0:
                continue
            yield Case(i_bitcount(name, n, Reg(a), m), st, [a, base, idx], [run])
    for n in sizes:
        a, b, base, idx = shuffled(regs(4))
        if 0 in (a, b):
            continue
        st = mk_state(rng, [a, b, 0])
        yield Case(i_xadd(n, Reg(a), Reg(b)), st, [a, b])
        m, run = mem_at(rng, st, base, idx, 4, 8)
        yield Case(i_xadd(n, m, Reg(b)), st, [b, base, idx], [run])
        st2 = st.copy()
        if rng.random() < 0.5:
            st2.regs[0] = st2.rd(m.ea(st2), n // 8)       # equal case
        yield Case(i_cmpxchg(n, m, Reg(b)), st2, [0, b, base, idx], [run])
        if n != 32:
            yield Case(i_cmpxchg(n, Reg(a), Reg(b)), st, [0, a, b])
    # SSE forms with GPR operands / GPR addresses
    for n in (32, 64):
        r, base, idx = regs(3)
        x = rng.randrange(16)
        st = mk_state(rng, [r], flags=False)
        st.xmm[x] = bytes(rng.getrandbits(8) for _ in range(16))
        yield Case(i_movd_to_xmm(n, x, Reg(r)), st, [r], xmm_in=[x])
        yield Case(i_movd_from_xmm(n, Reg(r), x), st, [r], xmm_in=[x])
        m, run = mem_at(rng, st, base, idx, 8, 8)
        yield Case(i_movd_to_xmm(n, x, m), st, [base, idx], [run], xmm_in=[x])
        yield Case(i_movd_from_xmm(n, m, x), st, [base, idx], [run], xmm_in=[x])
        yield Case(i_movnti(n, m, r), st, [r, base, idx], [run])
    for k in range(4):
        r = regs(1)[0]
        x = rng.randrange(16)
        st = mk_state(rng, [r], flags=False)
        st.xmm[x] = bytes(rng.getrandbits(8) for _ in range(16))
        yield Case(i_pmovmskb(r, x, k & 1), st, [r], xmm_in=[x])
        yield Case(i_movmskps(r, x), st, [r], xmm_in=[x])
        imm = rng.getrandbits(8)
        yield Case(i_pextrw(r, x, imm), st, [r], xmm_in=[x])
        yield Case(i_pinsrw(x, Reg(r), imm), st, [r], xmm_in=[x])
        v = rng.randrange(-(1 << 52), 1 << 52)
        st2 = st.copy()
        st2.regs[r] = v & M64
        yield Case(i_cvtsi2sd(x, Reg(r)), st2, [r], xmm_in=[x])
        st3 = st.copy()
        d = float(rng.randrange(-(1 << 40), 1 << 40))
        st3.xmm[x] = struct.pack("<d", d) + st.xmm[x][8:]
        yield Case(i_cvttsd2si(r, x), st3, [r], xmm_in=[x])
    for k in range(6):
        xd, xs = rng.randrange(16), rng.randrange(16)
        st = St()
        st.xmm[xs] = bytes(rng.getrandbits(8) for _ in range(16))
        st.xmm[xd] = bytes(rng.getrandbits(8) for _ in range(16))
        xb = [(1, 0, 1), (1, 0, 0), (0, 0, 1), (1, 1, 1), (0, 1, 0), (0, 0, 0)][k]
        yield Case(i_movdqa_rr(xd, xs, xb), st, [], xmm_in=sorted({xd, xs}))
    for k in range(4):
        base, idx = regs(2)
        x = rng.randrange(16)
        st = St()
        m, run = mem_at(rng, st, base, idx, rng.choice([1, 2, 4, 8]), 16, align=16)
        st.xmm[x] = bytes(rng.getrandbits(8) for _ in range(16))
        yield Case(i_movdqu(True, x, m), st, [base, idx], [run], xmm_in=[x])
        yield Case(i_movdqu(False, x, m), st, [base, idx], [run], xmm_in=[x])
        yield Case(i_paddd_m(x, m), st, [base, idx], [run], xmm_in=[x])
    # x87 with GPR addresses
    for k in range(3):
        b1, i1, b2 = regs(3)
        st = St()
        m1, run1 = mem_at(rng, st, b1, i1, 8, 8)
        val = struct.unpack("<Q", struct.pack("<d", rng.uniform(-1e6, 1e6)))[0]
        st.wr(m1.ea(st), 8, val)
        run1 = (run1[0], val.to_bytes(8, "little"))
        off2 = 0xE000 + 16 * k
        st.regs[b2] = MEM + off2 + 8
        m2 = Mem(b2, None, 1, -8)
        yield Case(i_fld_fstp(m1, m2), st, [b1, i1, b2], [run1])


# --------------------------------------------------------------------------------------------
# hand-written REX2 cases: decode rules, #UD, gating, the XSAVE image, CPUID (expected values)
# --------------------------------------------------------------------------------------------
def bstr(*b):
    return dotbyte(bytes(b))


def special_lines(rng):
    L = []
    c = lambda s: L.append("# " + s)
    c("--- REX2 payload bits: R4/X4/B4 extend ModRM.reg, SIB.index, ModRM.r/m / SIB.base (3.1.2.1) ---")
    # add r16d, eax: payload B4
    L.append("%s | r16=0x5 rax=0x3 => r16=0x8" % bstr(0xD5, 0x10, 0x01, 0xC0))
    # REX2.W with all extension bits: add r16, r16
    L.append("%s | r16=0xFFFFFFFF00000005 => r16=0xFFFFFFFE0000000A rflags=0x287" % bstr(0xD5, 0x58, 0x01, 0xC0))
    # mov r31, r16 (89 /r: reg r16 via R4, rm r31 via B4 B3)
    L.append("%s | r16=0x1122334455667788 => r31=0x1122334455667788" % bstr(0xD5, 0x59, 0x89, 0xC7))
    # SIB index 100 with X4 = 1: R20 is an index; with X4 = X3 = 0: no index
    L.append("%s | rbx=0x%X r20=0x10 m+0x8040=44332211 => rax=0x11223344" % (bstr(0xD5, 0x20, 0x8B, 0x04, 0xA3), MEM_PTR))
    L.append("%s | rbx=0x%X r20=0x10 m+0x8000=44332211 => rax=0x11223344" % (bstr(0xD5, 0x00, 0x8B, 0x04, 0xA3), MEM_PTR))
    # SIB index 100 with X3 = 1, X4 = 1: R28
    L.append("%s | rbx=0x%X r28=0x3 m+0x8018=8877665544332211 => rax=0x1122334455667788" % (bstr(0xD5, 0x2A, 0x8B, 0x04, 0xE3), MEM_PTR))
    # no base: SIB base 101 with mod 00 and B4 = 1 (R21 would be the base): [r20*2 + disp32]
    L.append("%s | r21=0x123456 r20=0x8 m+0x8010=DDCCBBAA => rax=0xAABBCCDD" % (bstr(0xD5, 0x30, 0x8B, 0x04, 0x65) + ", " + ", ".join("0x%02x" % b for b in struct.pack("<I", MEM_PTR))))
    # RIP-relative with B4/X4 = 1 (ignored): mov eax, [rip - 4] reads the disp32 itself
    L.append("%s => rax=0xFFFFFFFC" % (bstr(0xD5, 0x30, 0x8B, 0x05, 0xFC, 0xFF, 0xFF, 0xFF)))
    # mod 00 with base R21 (B4 = 1, low bits 101) is RIP-relative too: [rip + disp32]
    L.append("%s => rax=0xFFFFFFFC" % (bstr(0xD5, 0x10, 0x8B, 0x05, 0xFC, 0xFF, 0xFF, 0xFF)))
    # X4 without an index (no SIB) is ignored
    L.append("%s | r16=0x%X m+0x8008=0123456789ABCDEF => rax=0xEFCDAB8967452301" % (bstr(0xD5, 0x38, 0x8B, 0x40, 0x08), MEM_PTR))
    c("--- byte registers with REX2: ids 4-7 are SPL BPL SIL DIL, R16B-R31B (no AH..BH) ---")
    L.append("%s | rsi=0x1234 rdi=0x5678 => rdi=0x5634" % bstr(0xD5, 0x00, 0x88, 0xF7))      # mov dil, sil
    L.append("%s | rbx=0x1234 rdx=0x5678 => rbx=0x5634" % bstr(0x88, 0xF7))                   # legacy: mov bh, dh
    L.append("%s | rbp=0xA5 => rax=0xA5" % bstr(0xD5, 0x00, 0x8A, 0xC5))                      # mov al, bpl
    L.append("%s | r23=0x1122334455667788 => rcx=0x88" % bstr(0xD5, 0x90, 0xB6, 0xCF))         # movzx ecx, r23b
    L.append("%s | r16=0xFF => r16=0x0 rflags=0x256" % bstr(0xD5, 0x10, 0xFE, 0xC0))           # inc r16b: 0xFF + 1 (CF kept)
    c("--- opcode-embedded registers (B4/B3): push/pop (REX2.W = PPX hint), xchg, mov imm, bswap ---")
    L.append("%s | r29=0x1122334455667788 => r17=0x1122334455667788" % (bstr(0xD5, 0x11, 0x55, 0xD5, 0x10, 0x59)))
    L.append("%s | r29=0x1122334455667788 => r17=0x1122334455667788" % (bstr(0xD5, 0x19, 0x55, 0xD5, 0x18, 0x59)))
    L.append("%s | r29=0x1122334455667788 => r17=0x1122334455667788" % (bstr(0x66, 0xD5, 0x19, 0x55, 0xD5, 0x18, 0x59)))
    L.append("%s | rax=0x1 r24=0x2 => rax=0x2 r24=0x1" % bstr(0xD5, 0x19, 0x90))              # xchg rax, r24
    L.append("%s | rax=0x1 => rax=0x1" % bstr(0xD5, 0x00, 0x90))                              # nop
    L.append("%s => r30=0x1122334455667788" % bstr(0xD5, 0x19, 0xBE, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11))
    L.append("%s | r18=0xFFFFFFFF00112233 => r18=0x33221100" % bstr(0xD5, 0x90, 0xCA))          # bswap r18d
    c("--- legacy prefixes before REX2 keep their meaning; REX2 is the last prefix ---")
    L.append("%s | r16=0xFFFF0000FFFF r17=0x1 => r16=0xFFFF00000000 rflags=0x257" % bstr(0x66, 0xD5, 0x50, 0x01, 0xC8))   # add r16w, r17w... reg r17? see below
    L.append("%s | r16=0x%X rax=0x7 m+0x8000=01000000 => m+0x8000=08000000" % (bstr(0xF0, 0xD5, 0x10, 0x01, 0x00), MEM_PTR))   # lock add [r16], eax
    L.append("%s | r16=0x%X m+0x8000=0100000000000000 => rax=0x1" % (bstr(0x3E, 0x2E, 0xD5, 0x18, 0x8B, 0x00), MEM_PTR))
    L.append("%s | r16=0x%X => rax=0x%X" % (bstr(0x67, 0xD5, 0x10, 0x8D, 0x40, 0x10), 0xFFFFFFFF00001000, 0x1010))  # lea eax, [r16d+0x10]
    c("--- #UD: rows 4x/7x/Ax/Ex of map 0 and 3x/8x of map 1 (A1 with W = 0 = JMPABS, APX part 3) ---")
    for row in (0x40, 0x70, 0xA0, 0xE0):
        for lo in range(16):
            op = row | lo
            if op == 0xA1:
                L.append("%s => #UD" % bstr(0xD5, 0x08, op, 0, 0, 0, 0, 0, 0, 0, 0))     # REX2.W = 1 + A1: #UD
                continue
            L.append("%s => #UD" % bstr(0xD5, 0x00, op, 0xC0, 0, 0, 0, 0))
    for row in (0x30, 0x80):
        for lo in range(16):
            L.append("%s => #UD" % bstr(0xD5, 0x80, row | lo, 0xC0, 0, 0, 0, 0))
    c("--- #UD: escape and prefix bytes after REX2.M0 = 0; REX right before REX2 ---")
    for op in (0x0F, 0x66, 0x67, 0xF0, 0xF2, 0xF3, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65, 0x62, 0xC4, 0xC5, 0xD5):
        L.append("%s => #UD" % bstr(0xD5, 0x00, op, 0x01, 0xC0, 0x90, 0x90))
    for rex in (0x40, 0x41, 0x48, 0x4F):
        L.append("%s => #UD" % bstr(rex, 0xD5, 0x00, 0x01, 0xC0))
    # a REX that is not right before REX2 is cleared by the 66 (REX.W would make it 64-bit)
    L.append("%s | rax=0xFFFF0000 rcx=0x1234 => rax=0xFFFF1234" % bstr(0x48, 0x66, 0xD5, 0x00, 0x8B, 0xC1))
    c("--- #UD: opcodes invalid in 64-bit mode stay #UD with REX2 ---")
    for op in (0x06, 0x07, 0x0E, 0x16, 0x17, 0x1E, 0x1F, 0x27, 0x2F, 0x37, 0x3F, 0x60, 0x61, 0x82, 0x9A, 0xCE, 0xD4, 0xD6):
        L.append("%s => #UD" % bstr(0xD5, 0x00, op, 0xC0, 0, 0, 0, 0, 0, 0))
    c("--- #UD: XSAVE*/XRSTOR* with REX2 (3.1.2.1); FXSAVE is allowed ---")
    for ext in (4, 5, 6):        # 0F AE /4 xsave, /5 xrstor, /6 xsaveopt
        L.append("%s | rax=0x80000 => #UD" % bstr(0xD5, 0x80, 0xAE, (ext << 3) | 6))
        L.append("%s | rax=0x80000 => #UD" % bstr(0xD5, 0x88, 0xAE, (ext << 3) | 6))
    L.append("%s | rax=0x80000 => #UD" % bstr(0xD5, 0x80, 0xC7, 0x26))        # 0F C7 /4 xsavec
    L.append("%s | r16=0x%X =>! m+0xE000=7F03" % (bstr(0xD5, 0x90, 0xAE, 0x00), MEM + 0xE000))   # fxsave [r16]
    c("--- LOCK: a register destination #UD with REX2 as without ---")
    L.append("%s => #UD" % bstr(0xF0, 0xD5, 0x10, 0x01, 0xC0))
    L.append("%s => #UD" % bstr(0xF0, 0xD5, 0x00, 0x8B, 0x06))
    c("--- vector registers ignore R4/B4 (no XMM16-31 with REX2); GPRs keep them ---")
    L.append("%s | r17=0x11223344 => xmm1=44332211000000000000000000000000" % bstr(0x66, 0xD5, 0xD0, 0x6E, 0xC9))   # movd xmm1 (R4 ignored), r17d (B4)
    L.append("%s | xmm1=80FF00800000000000000000000000FF => r20=0x800B" % bstr(0x66, 0xD5, 0xD0, 0xD7, 0xE1))        # pmovmskb r20d (R4), xmm1 (B4 ignored)
    c("--- segment register: R4 and R3 ignored (8C /3 is DS whatever REX2.R3/R4) ---")
    L.append("%s | r16=0xFFFFFFFFFFFFFFFF => r16=0x0" % bstr(0xD5, 0x54, 0x8C, 0xD8))      # mov r16d, ds (R4 = R3 = 1)
    L.append("%s | r16=0xFFFFFFFFFFFFFFFF => r16=0xFFFFFFFFFFFF0000" % bstr(0x66, 0xD5, 0x10, 0x8C, 0xD8))
    c("--- control/debug registers: R4 selects CR16+/DR16+ (#UD); B4 selects the GPR ---")
    L.append("%s | rax=0x1234 => #UD" % bstr(0xD5, 0xC0, 0x20, 0xC0))           # mov rax, cr16
    L.append("%s | rax=0x1234 => #UD" % bstr(0xD5, 0xC4, 0x20, 0xC0))           # mov rax, cr24
    L.append("%s | rax=0x1234 => rax=0x0" % bstr(0xD5, 0x84, 0x20, 0xC0))       # mov rax, cr8 (TPR 0)
    L.append("%s | r16=0x1234 => r16=0x0" % bstr(0xD5, 0x94, 0x20, 0xC0))       # mov r16, cr8
    L.append("%s | rax=0x1234 => #UD" % bstr(0xD5, 0xC0, 0x21, 0xC0))           # mov rax, dr16
    L.append("%s | r16=0x1234 => r16=0x400" % bstr(0xD5, 0x90, 0x21, 0xF8))     # mov r16, dr7 (reset 400H)
    c("--- APX enabling: XCR0 = 80001H (x87 + APX) still runs REX2; restored afterwards ---")
    L.append("pushfq; xor ecx, ecx; xgetbv; mov r15d, eax; mov eax, 0x80001; xor edx, edx; xsetbv; %s; mov eax, r15d; xor edx, edx; xsetbv; xor eax, eax; xor r15d, r15d; popfq | r16=0x5 r17=0x9 => r16=0xE" % bstr(0xD5, 0x50, 0x01, 0xC8))
    c("--- XSAVE / XSAVEOPT / XSAVEC / XRSTOR of component 19 (R16-R31 at 3C0H / compacted 240H) ---")
    vals = [rng.getrandbits(64) for _ in range(16)]
    regs_in = " ".join("r%d=0x%X" % (16 + i, v) for i, v in enumerate(vals))
    img = b"".join(v.to_bytes(8, "little") for v in vals)
    bv = (0x80000).to_bytes(8, "little").hex().upper()
    L.append("xsave [rsi] | rax=0x80000 %s => m+0x8200=%s m+0x83C0=%s" % (regs_in, bv, img.hex().upper()))
    L.append("xsaveopt [rsi] | rax=0x80000 %s => m+0x8200=%s m+0x83C0=%s" % (regs_in, bv, img.hex().upper()))
    L.append("xsave64 [rsi] | rax=0x80000 %s => m+0x8200=%s m+0x83C0=%s" % (regs_in, bv, img.hex().upper()))
    L.append("xsavec [rsi] | rax=0x80000 %s => m+0x8200=%s m+0x8208=0000080000000080 m+0x8240=%s" % (regs_in, bv, img.hex().upper()))
    # XSAVEC with AVX (component 2, in use: YMMH0 non-zero) then APX: 19 follows 2 at 240H + 100H
    L.append("xsavec [rsi] | rax=0x80004 ymmh0=0102030405060708090A0B0C0D0E0F10 %s => m+0x8200=0400080000000000 m+0x8208=0400080000000080 m+0x8240=0102030405060708090A0B0C0D0E0F10 m+0x8340=%s" % (regs_in, img.hex().upper()))
    # standard XRSTOR loads R16-R31; a REX2 read right after sees the new value
    L.append("xrstor [rsi]; %s | rax=0x80000 m+0x8200=%s m+0x83C0=%s => rax=0x%X %s" % (
        bstr(0xD5, 0x18, 0x8B, 0xC0), bv, img.hex().upper(), vals[0], " ".join("r%d=0x%X" % (16 + i, v) for i, v in enumerate(vals))))
    # XSTATE_BV[19] = 0: XRSTOR puts R16-R31 in their initial configuration (0)
    L.append("xrstor [rsi] | rax=0x80000 %s => %s" % (regs_in, " ".join("r%d=0x0" % (16 + i) for i in range(16))))
    # compacted XRSTOR: XCOMP_BV = 8000000000080000H, component 19 at 240H
    L.append("xrstor [rsi] | rax=0x80000 m+0x8200=%s m+0x8208=0000080000000080 m+0x8240=%s => %s" % (
        bv, img.hex().upper(), " ".join("r%d=0x%X" % (16 + i, v) for i, v in enumerate(vals))))
    # RFBM[19] = 0: the EGPRs are not touched by XRSTOR
    L.append("xrstor [rsi] | rax=0x1 m+0x8200=%s m+0x83C0=%s %s =>" % (bv, img.hex().upper(), regs_in))
    # XGETBV(1) = XCR0 AND XINUSE: bit 19 follows the EGPR values (value-based XINUSE)
    L.append("pushfq; mov ecx, 1; xgetbv; and eax, 0x80000; xor edx, edx; popfq | r21=0x1 => rax=0x80000 rcx=0x1")
    L.append("pushfq; mov ecx, 1; xgetbv; and eax, 0x80000; xor edx, edx; popfq | rax=0x5 => rax=0x0 rcx=0x1")
    c("--- CPUID: 7.1:EDX.APX_F[21], 7.0:EBX.MPX[14] = 0, leaf 29H, 0DH.0:EAX[19], 0DH.19 ---")
    L.append("pushfq; mov eax, 7; mov ecx, 1; cpuid; and edx, 0x200000; xor eax, eax; xor ebx, ebx; xor ecx, ecx; popfq => rdx=0x200000")
    L.append("pushfq; mov eax, 7; xor ecx, ecx; cpuid; and ebx, 0x4000; xor eax, eax; xor ecx, ecx; xor edx, edx; popfq =>")
    L.append("pushfq; mov eax, 0x29; xor ecx, ecx; cpuid; popfq => rbx=0x1")
    L.append("pushfq; mov eax, 0x29; mov ecx, 1; cpuid; popfq => rcx=0x0")
    L.append("pushfq; mov eax, 0xd; xor ecx, ecx; cpuid; and eax, 0x80018; xor ebx, ebx; xor ecx, ecx; xor edx, edx; popfq => rax=0x80000")
    L.append("pushfq; mov eax, 0xd; mov ecx, 19; cpuid; popfq => rax=0x80 rbx=0x3C0 rcx=0x0")
    L.append("pushfq; mov eax, 0xd; mov ecx, 3; cpuid; popfq => rcx=0x0")
    L.append("pushfq; mov eax, 0xd; mov ecx, 4; cpuid; popfq => rcx=0x0")
    return L


# --------------------------------------------------------------------------------------------
def gen_cases(out):
    rng = random.Random(0x0A9C0DE1)
    out.write("# Intel APX part 1 (ledger U610-U616): EGPRs R16-R31 through REX2, the REX2 decode rules\n")
    out.write("# and #UD conditions, the APX state (XSAVE component 19), CPUID. Expected values from the\n")
    out.write("# independent model Emulator/tools/isa/ref_apx_core.py --cases (regenerate, do not edit).\n")
    out.write("# The i5-13600K has no APX: expected-value cases only, run with the APX opt-in:\n")
    out.write("#   emu-alltest --cases Emulator\\data\\cases_apx_core.txt --apx --expect-only\n")
    out.write("# Unicorn runs at CPL0 (MOV CR8/DR7, XSETBV). Undefined flags are cleared by a\n")
    out.write("# \"pushfq; and qword ptr [rsp], M; popfq\" suffix (the stack is not compared).\n")
    lines = []
    for case in gen_items(rng, True):
        lines.append(case.line_rex2())
    for l in lines:
        out.write(l + "\n")
    for l in special_lines(rng):
        out.write(l + "\n")


def hw_cases():
    rng = random.Random(0x0A9C0DE2)
    items = []
    for case in gen_items(rng, False):
        l = case.line_pair()
        if l is not None:
            items.append((l, case))
    return items


def gen_hw(out):
    out.write("# Intel APX part 1 (ledger U614/U615) hardware pairs: \"<legacy bytes> ~~ <REX2 bytes>\" with\n")
    out.write("# registers R0-R15 only. The i5-13600K runs the legacy (REX / no-REX) encoding, Unicorn the\n")
    out.write("# REX2 encoding of the same instruction; the whole state must be equal (test.cmd, 0 differing):\n")
    out.write("#   emu-alltest --cases Emulator\\data\\cases_apx_core_hw.txt --apx\n")
    out.write("# Vector-register pairs set REX2.R4/X4/B4 = 1 (ignored for XMM). Generated by\n")
    out.write("# Emulator/tools/isa/ref_apx_core.py --hwgen (do not edit); --hwcmp LOG checks the CPU's\n")
    out.write("# results against the model.\n")
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
        ok = True
        why = ""
        if fields.startswith("fault"):
            vec = int(fields.split()[1].lstrip("#"))
            ok = fault == vec
            why = "fault %d vs model %s" % (vec, fault)
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
                    if case.model()[1] is None:
                        ev &= ~und        # the suffix cleared them on the CPU too
                    if (gv ^ ev) & ~und:
                        ok, why = False, "rflags hw %X model %X (undefined %X)" % (gv, ev, und)
                    continue
                if k.startswith("m+") or k.startswith("xmm"):
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
    # flags (SDM: ADD 0FFH + 1 -> 0, CF ZF AF PF; SUB 0 - 1 -> FF, CF SF AF PF)
    chk("add8", alu_core("add", 0xFF, 1, 8, 0), (0, CF | ZF | AF | PF, 0))
    chk("sub8", alu_core("sub", 0, 1, 8, 0), (0xFF, CF | SF | AF | PF, 0))
    chk("add32 of", alu_core("add", 0x7FFFFFFF, 1, 32, 0)[1] & (OF | SF), OF | SF)
    chk("sbb borrow", alu_core("sbb", 5, 5, 16, 1), (0xFFFF, CF | SF | PF | AF, 0))
    chk("cond l", cond(0xC, SF), True)
    chk("cond nle", cond(0xF, ZF), False)
    # encodings (3.1.2.1 Figure 3.1; examples worked by hand)
    chk("rex2 add r16d, eax", i_alu("add", 32, "EG", Reg(16), Reg(0)).rex2_bytes(), bytes([0xD5, 0x10, 0x01, 0xC0]))
    chk("rex2 mov r31, r16", i_mov(64, "EG", Reg(31), Reg(16)).rex2_bytes(), bytes([0xD5, 0x59, 0x89, 0xC7]))
    chk("legacy mov r15, r8", i_mov(64, "EG", Reg(15), Reg(8)).legacy_bytes(), bytes([0x4D, 0x89, 0xC7]))
    chk("rex2 map1 movzx", i_movx("zx", 32, Reg(1), Reg(23), 8).rex2_bytes(), bytes([0xD5, 0x90, 0xB6, 0xCF]))
    chk("legacy mov sil, dil needs REX", i_mov(8, "EG", Reg(6), Reg(7)).legacy_bytes(), bytes([0x40, 0x88, 0xFE]))
    chk("rex2 sib index r20", Enc(0x8B, reg=0, rm=Mem(3, 20, 4, 0)).rex2(), bytes([0xD5, 0x20, 0x8B, 0x04, 0xA3]))
    chk("legacy no-sib base r13", Enc(0x8B, reg=0, rm=Mem(13, None, 1, 0)).legacy(), bytes([0x41, 0x8B, 0x45, 0x00]))
    chk("rex2 push r29", Enc(0x50, oreg=29).rex2(), bytes([0xD5, 0x11, 0x55]))
    chk("ext80 1.0", ext80(0x3FF0000000000000), (0x3FFF, 1 << 63))
    st = St()
    st.regs[16] = 0x10
    m = Mem(3, 20, 4, 0)
    st.regs[3] = 0x1000
    st.regs[20] = 0x10
    chk("ea", m.ea(st), 0x1040)
    return ok


# --------------------------------------------------------------------------------------------
# U616: the APX extension of existing EVEX instructions (3.1.2.3.3, Figure 3.5, Table 3.3)
#   P0 = ~R3 ~X3 ~B3 ~R4 B4 m m m, P1 = W ~vvvv U p p (X4 = ~U), P2 = z L'L b ~V4 aaa
#   B4: bit 4 of a GPR in ModRM.r/m and of the base; X4: bit 4 of the index (mod != 11b only;
#   with mod = 11b U must be 1, else #UD); R4: bit 4 of a GPR in ModRM.reg; ignored for vector
#   registers / a VSIB index (V4 X3 index) / no index. Expected values from the SDM pages of
#   VMOVDQU64, VPBROADCASTD, VMOVD/VMOVQ, VCVTSS2SI, VCVTTSD2SI, VPEXTRD, VPINSRQ, VPADDD,
#   VPGATHERDD (EVEX.128 writes zero bits MAXVL-1:128).
# --------------------------------------------------------------------------------------------
def evex(mmm, pp, w, ll, opc, tail, vvvv=0, R3=0, X3=0, B3=0, R4=0, B4=0, X4=0, V4=0, z=0, b=0, aaa=0):
    p0 = ((R3 ^ 1) << 7) | ((X3 ^ 1) << 6) | ((B3 ^ 1) << 5) | ((R4 ^ 1) << 4) | (B4 << 3) | mmm
    p1 = (w << 7) | (((~vvvv) & 15) << 3) | ((X4 ^ 1) << 2) | pp
    p2 = (z << 7) | (ll << 5) | (b << 4) | ((V4 ^ 1) << 3) | aaa
    return bytes([0x62, p0, p1, p2, opc]) + bytes(tail)


def zhex(b):
    return bytes(b).hex().upper()


def evex_lines():
    rng = random.Random(0x0A9C0DE3)
    L = []
    c = lambda s: L.append("# " + s)
    rnd = lambda n: bytes(rng.getrandbits(8) for _ in range(n))
    c("--- VMOVDQU64 zmm1, [r20 + r21*8 + 40H] (B4 base, X4 = ~U index, disp8*64) ---")
    data = rnd(64)
    r21 = 3
    r20 = MEM + 0xA000 - r21 * 8 - 0x40
    code = evex(1, 2, 1, 2, 0x6F, [0x4C, 0xEC, 0x01], B4=1, X4=1)
    L.append("%s | r20=0x%X r21=0x%X m+0xA000=%s => zmm1=%s" % (dotbyte(code), r20, r21, zhex(data), zhex(data)))
    c("--- the same with U = 1 (X4 = 0): index R5 = RBP ---")
    code = evex(1, 2, 1, 2, 0x6F, [0x4C, 0xEC, 0x01], B4=1, X4=0)
    L.append("%s | r20=0x%X rbp=0x%X m+0xA000=%s => zmm1=%s" % (dotbyte(code), r20, r21, zhex(data), zhex(data)))
    c("--- VMOVDQU64 [r28 + r16*2], zmm5 (store; B4 + B3 base, X4 index) ---")
    z5 = rnd(64)
    r16 = 0x20
    r28 = MEM + 0xB000 - r16 * 2
    code = evex(1, 2, 1, 2, 0x7F, [0x2C, 0x44], B4=1, B3=1, X4=1)    # modrm 00 101 100, SIB 01 000 100
    L.append("%s | r28=0x%X r16=0x%X zmm5=%s => m+0xB000=%s m+0xB020=%s" % (dotbyte(code), r28, r16, zhex(z5), zhex(z5[:32]), zhex(z5[32:])))
    c("--- VPBROADCASTD zmm2, r18d (EVEX.512.66.0F38.W0 7C: B4 GPR r/m) ---")
    v = rng.getrandbits(32)
    code = evex(2, 1, 0, 2, 0x7C, [0xD2], B4=1)
    L.append("%s | r18=0x%X => zmm2=%s" % (dotbyte(code), (rng.getrandbits(32) << 32) | v, zhex(v.to_bytes(4, "little") * 16)))
    c("--- VMOVD r29d, xmm1 / VMOVQ xmm3, r30 (B4 B3 GPR r/m; EVEX.128 zeroes the upper bits) ---")
    x1 = rnd(64)
    code = evex(1, 1, 0, 0, 0x7E, [0xCD], B4=1, B3=1)
    L.append("%s | zmm1=%s r29=0xFFFFFFFFFFFFFFFF => r29=0x%X" % (dotbyte(code), zhex(x1), int.from_bytes(x1[:4], "little")))
    r30 = rng.getrandbits(64)
    code = evex(1, 1, 1, 0, 0x6E, [0xDE], B4=1, B3=1)
    L.append("%s | zmm3=%s r30=0x%X => zmm3=%s" % (dotbyte(code), zhex(rnd(64)), r30, zhex(r30.to_bytes(8, "little") + bytes(56))))
    c("--- VCVTSS2SI r27, xmm1 / VCVTTSD2SI r16, xmm0 (R4 R3 / R4: GPR in ModRM.reg) ---")
    f = struct.pack("<f", 1234.0)
    code = evex(1, 2, 1, 0, 0x2D, [0xD9], R4=1, R3=1)
    L.append("%s | xmm1=%s => r27=0x4D2" % (dotbyte(code), zhex(f + bytes(12))))
    dd = struct.pack("<d", -77.0)
    code = evex(1, 3, 1, 0, 0x2C, [0xC0], R4=1)
    L.append("%s | xmm0=%s => r16=0x%X" % (dotbyte(code), zhex(dd + bytes(8)), (-77) & M64))
    c("--- VPEXTRD r30d, xmm1, 2 / VPINSRQ xmm1, xmm2, r19, 1 (B4 GPR r/m) ---")
    x1 = rnd(16)
    code = evex(3, 1, 0, 0, 0x16, [0xCE, 0x02], B4=1, B3=1)
    L.append("%s | xmm1=%s r30=0xFFFFFFFFFFFFFFFF => r30=0x%X" % (dotbyte(code), zhex(x1), int.from_bytes(x1[8:12], "little")))
    x2 = rnd(64)
    r19 = rng.getrandbits(64)
    code = evex(3, 1, 1, 0, 0x22, [0xCB, 0x01], vvvv=2, B4=1)
    L.append("%s | zmm1=%s zmm2=%s r19=0x%X => zmm1=%s" % (dotbyte(code), zhex(rnd(64)), zhex(x2), r19, zhex(x2[:8] + r19.to_bytes(8, "little") + bytes(48))))
    c("--- vector r/m: B4 ignored (VPADDD zmm1, zmm2, zmm3 with B4 = 1) ---")
    a, b_ = rnd(64), rnd(64)
    s = b"".join(((int.from_bytes(a[i:i + 4], "little") + int.from_bytes(b_[i:i + 4], "little")) & mask(32)).to_bytes(4, "little") for i in range(0, 64, 4))
    code = evex(1, 1, 0, 2, 0xFE, [0xCB], vvvv=2, B4=1)
    L.append("%s | zmm2=%s zmm3=%s => zmm1=%s" % (dotbyte(code), zhex(a), zhex(b_), zhex(s)))
    c("--- U = 0 with ModRM.mod = 11b: #UD ---")
    code = evex(1, 1, 0, 2, 0xFE, [0xCB], vvvv=2, X4=1)
    L.append("%s | zmm2=%s zmm3=%s => #UD" % (dotbyte(code), zhex(a), zhex(b_)))
    c("--- memory without SIB: X4 (U = 0) ignored; VPADDD zmm1, zmm2, [r19] ---")
    r19 = MEM + 0xC000
    code = evex(1, 1, 0, 2, 0xFE, [0x0B], vvvv=2, B4=1, X4=1)
    L.append("%s | zmm2=%s r19=0x%X m+0xC000=%s => zmm1=%s" % (dotbyte(code), zhex(a), r19, zhex(b_), zhex(s)))
    c("--- VSIB: X4 (U = 0) not used; VPGATHERDD zmm1{k1}, [rax + zmm2*4] ---")
    idx = [rng.randrange(0, 0x400) for _ in range(16)]
    mem = bytearray(0x1000)
    for i in range(0x1000):
        mem[i] = rng.getrandbits(8)
    gat = b"".join(mem[4 * k:4 * k + 4] for k in idx)
    zi = b"".join(k.to_bytes(4, "little") for k in idx)
    runs = " ".join("m+0x%X=%s" % (0xD000 + o, zhex(mem[o:o + 512])) for o in range(0, 0x1000, 512))
    for x4 in (0, 1):
        code = evex(2, 1, 0, 2, 0x90, [0x0C, 0x90], aaa=1, X4=x4)
        L.append("%s | rax=0x%X zmm2=%s k1=0xFFFF %s => zmm1=%s k1=0x0" % (dotbyte(code), MEM + 0xD000, zhex(zi), runs, zhex(gat)))
    c("--- RIP-relative with B4/X4 set (ignored): VMOVD xmm4, [rip - 4] ---")
    code = evex(1, 1, 0, 0, 0x6E, [0x25, 0xFC, 0xFF, 0xFF, 0xFF], B4=1, X4=1)
    L.append("%s => zmm4=%s" % (dotbyte(code), zhex(bytes([0xFC, 0xFF, 0xFF, 0xFF]) + bytes(60))))
    # U791: a k register in ModRM.reg does not use R4 (Table 3.3 REG: GPR / vector): with APX
    # enabled the bit is ignored (3.1.2.3.3); R3 (EVEX.R) with a k register stays #UD (SDM Table 2-41)
    rk = random.Random(0x0791)
    a = bytes(rk.getrandbits(8) for _ in range(64))
    eq = [0, 3, 4, 9, 15]
    b_ = b"".join(a[4 * i:4 * i + 4] if i in eq else bytes(x ^ 0x5A for x in a[4 * i:4 * i + 4]) for i in range(16))
    km = sum(1 << i for i in eq)
    c("--- k register in ModRM.reg with R4 = 1 (U791): ignored with APX enabled; VPCMPEQD k1, zmm2, zmm3 (EVEX.512.66.0F.W0 76) ---")
    for r4 in (0, 1):
        code = evex(1, 1, 0, 2, 0x76, [0xCB], vvvv=2, R4=r4)
        L.append("%s | zmm2=%s zmm3=%s k1=0xFFFF => k1=0x%X" % (dotbyte(code), zhex(a), zhex(b_), km))
    c("--- ... VPCMPEQD k5, zmm2, zmm3 with R4 = 1 and a k register written: k5 (not k21) ---")
    code = evex(1, 1, 0, 2, 0x76, [0xEB], vvvv=2, R4=1)
    L.append("%s | zmm2=%s zmm3=%s => k5=0x%X" % (dotbyte(code), zhex(a), zhex(b_), km))
    c("--- ... R3 = 1 with a k register: #UD (unchanged, SDM Table 2-41) ---")
    code = evex(1, 1, 0, 2, 0x76, [0xCB], vvvv=2, R3=1)
    L.append("%s | zmm2=%s zmm3=%s => #UD" % (dotbyte(code), zhex(a), zhex(b_)))
    return L


def gen_evex(out):
    out.write("# Intel APX part 1, ledger U616: the APX extension of EVEX instructions (EVEX.B4, EVEX.X4 =\n")
    out.write("# ~EVEX.U, EVEX.R4 for GPRs). Expected values from Emulator/tools/isa/ref_apx_core.py --evex\n")
    out.write("# (regenerate, do not edit); Unicorn only, APX and AVX-512 opt-ins:\n")
    out.write("#   emu-alltest --cases Emulator\\data\\cases_apx_evex.txt --apx --avx512 --expect-only\n")
    for l in evex_lines():
        out.write(l + "\n")


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_cases(sys.stdout)
        return
    if "--evex" in sys.argv:
        gen_evex(sys.stdout)
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
