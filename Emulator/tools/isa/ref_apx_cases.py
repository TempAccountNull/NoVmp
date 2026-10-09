#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_apx_cases.py -- independent reference model (Python 3 stdlib only) of the ten Intel APX forms
that had no expected-value case after U690 (plan item 1.F.13, ledger U790): JMPABS, EVEX ENQCMD /
ENQCMDS, EVEX URDMSR / UWRMSR (register forms), EVEX WRSSD / WRSSQ, EVEX WRUSSD / WRUSSQ and EVEX
TILELOADDT1; generator of Emulator/data/cases_apx_sys.txt.

Written from the Intel APX architecture specification 355828-009 (rev 9.0) and the Intel SDM 092
text, not from any C implementation:

  JMPABS      (APX 3.1.3.3) REX2 M0 = 0 W = 0, opcode A1, target64: RIP := target64; all other
              REX2 payload bits are ignored; segment overrides are allowed and ignored. The case
              snippet starts at SNIP = 30000199H (emu-alltest: CODE + the fixed prologue; the first
              case of the file checks it with LEA RAX, [RIP]), so the targets are absolute
              addresses inside the snippet.
  ENQCMD(S)   (SDM Vol2A ENQCMD / ENQCMDS, APX 6.21 / 6.22, Table 4.11) EVEX.LLZ.F2/F3.MAP4 F8
              !(11):rrr:bbb; ModRM.reg = destination address (64-bit, 32-bit with 67H), r/m = the
              64-byte source. ENQCMD: #GP(0) if IA32_PASID[31] = 0 or the source bits 31:0 are
              not 0 (the APX table says 30:0; only bits both texts name are tested); ENQCMDS: #GP(0)
              if CPL != 0 or the source bits 30:20 are not 0; both #GP(0) if the destination is
              not 64-byte aligned. The destination is ordinary memory, not an enqueue register:
              "the store is dropped and is written neither to MMIO nor to memory", ZF = 1 (retry),
              OF SF AF PF CF = 0.
  URDMSR      (SDM Vol2B URDMSR / UWRMSR, APX 6.68 / 6.69, Table 4.23) EVEX.LLZ.F2/F3.MAP4.W0 F8
  UWRMSR      11:rrr:bbb. URDMSR: bbb := MSR[rrr]; UWRMSR: MSR[rrr] := bbb. #UD if
              IA32_USER_MSR_CTL (1CH).ENABLE[0] = 0; #GP(0) if the address has bits 63:14 set or its
              bit in the 4-KByte bitmap at IA32_USER_MSR_CTL[63:12] is 0 (read bitmap: the low 2
              KBytes, write bitmap: the high 2 KBytes); UWRMSR: #GP(0) for any MSR other than
              IA32_UARCH_MISC_CTL (1B01H) and for its reserved bits (63:1, only DOITM[0] exists).
  WRSSD/Q     (SDM Vol2D WRSSD/WRSSQ, APX 6.70 / Table 4.7) EVEX.LLZ.NP.MAP4.W0/W1 66 !(11):rrr:bbb.
              CPL0: #UD if CR4.CET = 0, IA32_S_CET.SH_STK_EN = 0 or IA32_S_CET.WR_SHSTK_EN = 0;
              #GP(0) if the destination is misaligned (WRSSD 4 bytes; WRSSQ: the Operation section
              checks 8, the exception list 4 - the cases use addresses both texts agree on); then
              a shadow-stack store of 4 / 8 bytes. Without paging (the harness has a flat map) the
              shadow-stack store is a plain store, as in cases_tsx_cet_max.txt / cases_cet2.txt.
  WRUSSD/Q    (SDM Vol2D WRUSSD/WRUSSQ, APX 6.73 / 6.74, Table 4.8) EVEX.LLZ.66.MAP4.W0/W1 65
              !(11):rrr:bbb. #UD if CR4.CET = 0 (checked first), #GP(0) if CPL > 0, alignment as
              above, then a user-mode shadow-stack store (plain store without paging).
  TILELOADDT1 (SDM Vol2B TILELOADD/TILELOADDT1, APX 6.66, AMX-E3-EVEX Table 4.4) EVEX.128.66.0F38.W0
              4B !(11):rrr:100: rows start_row .. rows-1 of tmm from [base + disp + row * (index <<
              scale)], colsb bytes each (the rest of the row zero), start_row := 0. LDTILECFG /
              TILESTORED / STTILECFG / TILERELEASE (VEX, SDM Vol2A/2B) set the configuration and
              store the tile so the result is in the compared operand memory.

CR4.CET needs CR0.WP (SDM Vol3A 2.5: MOV to CR4 #GP if it sets CET while WP = 0), so the CET cases
start with MOV RAX, CR0 / BTS RAX, 16 / MOV CR0, RAX / MOV RAX, CR4 / BTS RAX, 23 / MOV CR4, RAX /
PUSH 202H / POPFQ (BTS leaves flags undefined; POPFQ rewrites them) and then MOV EAX, imm32, so
every expectation is SDM-defined. Each instruction of a case is its own ".byte" run, so
apx_cov.py finds the APX form at the start of a run.

Usage:
  python ref_apx_cases.py --selftest    hand-derived checks of the model, exit 0 on pass
  python ref_apx_cases.py --cases       Emulator/data/cases_apx_sys.txt (stdout); run with
                                        emu-alltest --cases ... --apx --amx --expect-only
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ref_apx_core import St, Reg, Mem, Fault, dotbyte, fmt_inputs, fmt_changes, mask, sx, M64, MEM, MEM_PTR, MEM_DST  # noqa: E402
from ref_apx_map4 import Ev  # noqa: E402  (the EVEX encoder of Figure 3.3 / 3.4, from the spec)

SNIP = 0x30000199          # first byte of a case snippet (checked by the layout guard case)
CF, PF, AF, ZF, SF, OF = 0x1, 0x4, 0x10, 0x40, 0x80, 0x800
STATUS = CF | PF | AF | ZF | SF | OF

MSR_USER_MSR_CTL = 0x1C
MSR_S_CET = 0x6A2
MSR_PASID = 0xD93
MSR_UARCH_MISC_CTL = 0x1B01
UD, GP = 6, 13


class Mach:
    """the architectural state the cases touch: GPRs / RFLAGS / operand memory (ref_apx_core.St),
    CPL, CR0.WP, CR4.CET, the MSRs written by the cases (reset value 0) and the AMX tile state"""

    def __init__(self, st, cpl=0):
        self.st, self.cpl = st, cpl
        self.wp = 0                  # CR0.WP (Unicorn reset CR0 = 11H for expected-value cases)
        self.cet = 0                 # CR4.CET (reset 0)
        self.msr = {}
        self.tcfg = None             # None = TILES_CONFIGURED 0 (INIT); else the 64 config bytes
        self.tiles = [[bytes(64)] * 16 for _ in range(8)]

    def rdmsr(self, a):
        if a in (MSR_USER_MSR_CTL, MSR_S_CET, MSR_PASID, MSR_UARCH_MISC_CTL):
            return self.msr.get(a, 0)
        raise Fault(GP)


def r64(m, r):
    return m.st.regs[r]


# --------------------------------------------------------------------------------------------
# setup instructions (SDM Vol2): each returns (bytes, semantics)
# --------------------------------------------------------------------------------------------
def mov_r32_imm(r, v):
    """MOV r32, imm32 (B8+r id): zero-extends, flags unchanged"""
    def sem(m):
        m.st.regs[r] = v & 0xFFFFFFFF
    return bytes([0xB8 + r]) + (v & 0xFFFFFFFF).to_bytes(4, "little"), sem


def wrmsr():
    """WRMSR (0F 30): MSR[ECX] := EDX:EAX at CPL0; the reserved bits the cases could hit -> #GP"""
    def sem(m):
        if m.cpl:
            raise Fault(GP)
        a = r64(m, 1) & 0xFFFFFFFF
        v = ((r64(m, 2) & 0xFFFFFFFF) << 32) | (r64(m, 0) & 0xFFFFFFFF)
        if a == MSR_USER_MSR_CTL and v & 0xFFE:
            raise Fault(GP)          # bits 11:1 reserved (SDM Vol4 IA32_USER_MSR_CTL)
        if a == MSR_PASID and v & 0xFFFFFFFF7FF00000:
            raise Fault(GP)          # bits 30:20 and 63:32 reserved
        if a == MSR_S_CET and v & 0x3C0:
            raise Fault(GP)          # bits 9:6 reserved (SDM Vol4 IA32_S_CET)
        if a not in (MSR_USER_MSR_CTL, MSR_PASID, MSR_S_CET):
            raise AssertionError("WRMSR %X not modelled" % a)
        m.msr[a] = v
    return b"\x0f\x30", sem


def set_msr(a, v):
    """MOV ECX, a; MOV EAX, v[31:0]; MOV EDX, v[63:32]; WRMSR"""
    return [mov_r32_imm(1, a), mov_r32_imm(0, v), mov_r32_imm(2, v >> 32), wrmsr()]


def cet_on():
    """CR0.WP := 1, CR4.CET := 1 (CPL0), RFLAGS := 202H (POPFQ). RAX is left with CR4 and is
    rewritten by the MOV EAX, imm32 that follows in every case (set_msr / mov_r32_imm)."""
    b = bytes([0x0F, 0x20, 0xC0, 0x48, 0x0F, 0xBA, 0xE8, 0x10, 0x0F, 0x22, 0xC0,
               0x0F, 0x20, 0xE0, 0x48, 0x0F, 0xBA, 0xE8, 0x17, 0x0F, 0x22, 0xE0,
               0x68, 0x02, 0x02, 0x00, 0x00, 0x9D])

    def sem(m):
        if m.cpl:
            raise Fault(GP)
        m.wp = 1
        m.cet = 1
        m.st.rflags = 0x202
        m.st.regs[0] = None          # CR4 value: must be overwritten before the case ends
    return b, sem


# --------------------------------------------------------------------------------------------
# the APX forms
# --------------------------------------------------------------------------------------------
def jmpabs(target, payload=0x00, pfx=b""):
    """REX2 (D5, payload with M0 = W = 0) A1 target64; the payload's other bits are ignored"""
    assert payload & 0x88 == 0

    def sem(m):
        if sx(target, 48) != target:
            raise Fault(GP)          # non-canonical target (CR4.LA57 = 0: 48 bits)
        m.jump = target
    return pfx + bytes([0xD5, payload, 0xA1]) + target.to_bytes(8, "little"), sem


def enqcmd(sup, dst, src, a32=False):
    """EVEX ENQCMD (F2) / ENQCMDS (F3) ModRM.reg = dst (address register), r/m = src (Mem)"""
    e = Ev(0xF8, reg=dst, rm=src, pp=2 if sup else 3, w=0)
    b = (b"\x67" if a32 else b"") + e.rex2()

    def sem(m):
        am = mask(32 if a32 else 64)
        if sup and m.cpl != 0:
            raise Fault(GP)
        if not sup and not (m.msr.get(MSR_PASID, 0) >> 31) & 1:
            raise Fault(GP)
        d = r64(m, dst) & am
        if d & 63:
            raise Fault(GP)
        s = m.st.rd(src.ea(m.st) & am, 64)
        if sup and (s >> 20) & 0x7FF:
            raise Fault(GP)
        if not sup and s & 0xFFFFFFFF:
            raise Fault(GP)
        # memory destination: not an enqueue register -> retry, nothing written
        m.st.rflags = (m.st.rflags & ~STATUS) | ZF
    return b, sem


def user_msr_bit(m, a, write):
    ctl = m.msr.get(MSR_USER_MSR_CTL, 0)
    byte = m.st.rd((ctl & ~0xFFF) + (2048 if write else 0) + (a >> 3), 1)
    return (byte >> (a & 7)) & 1


def urdmsr(dst, src, w=0):
    """EVEX URDMSR: ModRM.r/m (bbb) = destination, ModRM.reg (rrr) = MSR address"""
    b = Ev(0xF8, reg=src, rm=Reg(dst), pp=3, w=w).rex2()

    def sem(m):
        if w:
            raise Fault(UD)          # EVEX.W0 form only (APX 6.68)
        if not m.msr.get(MSR_USER_MSR_CTL, 0) & 1:
            raise Fault(UD)
        a = r64(m, src)
        if a >> 14 or not user_msr_bit(m, a, False):
            raise Fault(GP)
        m.st.regs[dst] = m.rdmsr(a)
    return b, sem


def uwrmsr(addr, val):
    """EVEX UWRMSR: ModRM.reg (rrr) = MSR address, ModRM.r/m (bbb) = the value"""
    b = Ev(0xF8, reg=addr, rm=Reg(val), pp=2, w=0).rex2()

    def sem(m):
        if not m.msr.get(MSR_USER_MSR_CTL, 0) & 1:
            raise Fault(UD)
        a = r64(m, addr)
        if a >> 14 or not user_msr_bit(m, a, True):
            raise Fault(GP)
        v = r64(m, val)
        if a != MSR_UARCH_MISC_CTL or v & ~1 & M64:
            raise Fault(GP)
        m.msr[a] = v
    return b, sem


def wrss(q, mem, src):
    """EVEX WRSSD (W0) / WRSSQ (W1): NP MAP4 66"""
    b = Ev(0x66, reg=src, rm=mem, pp=0, w=1 if q else 0).rex2()

    def sem(m):
        assert m.cpl == 0
        s_cet = m.msr.get(MSR_S_CET, 0)
        if not m.cet or not s_cet & 1 or not s_cet & 2:
            raise Fault(UD)
        la = mem.ea(m.st)
        if la & (7 if q else 3):
            raise Fault(GP)
        m.st.wr(la, 8 if q else 4, r64(m, src))
    return b, sem


def wruss(q, mem, src):
    """EVEX WRUSSD (W0) / WRUSSQ (W1): 66 MAP4 65"""
    b = Ev(0x65, reg=src, rm=mem, pp=1, w=1 if q else 0).rex2()

    def sem(m):
        if not m.cet:
            raise Fault(UD)
        if m.cpl > 0:
            raise Fault(GP)
        la = mem.ea(m.st)
        if la & (7 if q else 3):
            raise Fault(GP)
        m.st.wr(la, 8 if q else 4, r64(m, src))
    return b, sem


# AMX (VEX forms for the configuration / store side; SDM Vol2A/2B)
def ldtilecfg_rsi():
    """LDTILECFG [rsi]: VEX.128.NP.0F38.W0 49 /0"""
    def sem(m):
        c = bytes(m.st.mem[m.st.regs[6] - MEM:m.st.regs[6] - MEM + 64])
        if c[0] == 0:
            m.tcfg = None
            m.tiles = [[bytes(64)] * 16 for _ in range(8)]
            return
        if c[0] != 1 or any(c[2:16]) or any(c[32:48]) or any(c[56:64]):
            raise Fault(GP)
        for t in range(8):
            colsb = c[16 + 2 * t] | c[17 + 2 * t] << 8
            rows = c[48 + t]
            if colsb > 64 or rows > 16 or (colsb == 0) != (rows == 0):
                raise Fault(GP)
        m.tcfg = bytearray(c)
        m.tiles = [[bytes(64)] * 16 for _ in range(8)]
    return bytes([0xC4, 0xE2, 0x78, 0x49, 0x06]), sem


def tile_dims(m, t):
    if m.tcfg is None:
        raise Fault(UD)              # AMX-E3: TILES_CONFIGURED = 0
    colsb = m.tcfg[16 + 2 * t] | m.tcfg[17 + 2 * t] << 8
    rows = m.tcfg[48 + t]
    if rows == 0:
        raise Fault(UD)              # tdest not a valid (configured) tile
    return colsb, rows


def tileloaddt1(t, mem):
    """EVEX.128.66.0F38.W0 4B !(11):rrr:100 TILELOADDT1 tmm, sibmem (APX-promoted; mem has an index)"""
    assert mem.index is not None
    e = Ev(0x4B, reg=t, rm=mem, pp=1, w=0, mapid=2)
    b = e.rex2()

    def sem(m):
        colsb, rows = tile_dims(m, t)
        start = m.tcfg[1]            # 0 in every case (LDTILECFG of a zero start_row byte)
        membegin = (m.st.regs[mem.base] if mem.base is not None else 0) + mem.disp
        stride = (m.st.regs[mem.index] if mem.index is not None else 0) * mem.scale
        tl = list(m.tiles[t])
        for r in range(start, 16):
            tl[r] = bytes(64)
        for r in range(start, rows):
            data = m.st.rd(membegin + r * stride, colsb).to_bytes(colsb, "little")
            tl[r] = data + bytes(64 - colsb)
        m.tiles[t] = tl
        m.tcfg[1] = 0
    return b, sem


def tileloaddt1_ud(t, mem, **kw):
    """an EVEX TILELOADDT1 encoding that must #UD (no SIB, W1, vvvv, EVEX.b ...)"""
    b = Ev(0x4B, reg=t, rm=mem, pp=1, mapid=2, **kw).rex2()

    def sem(m):
        raise Fault(UD)
    return b, sem


def tilestored_rdi_rcx(t):
    """TILESTORED [rdi + rcx*1], tmm: VEX.128.F3.0F38.W0 4B !(11):rrr:100"""
    def sem(m):
        colsb, rows = tile_dims(m, t)
        for r in range(m.tcfg[1], rows):
            m.st.wr(m.st.regs[7] + r * m.st.regs[1], colsb, int.from_bytes(m.tiles[t][r][:colsb], "little"))
        m.tcfg[1] = 0
    return bytes([0xC4, 0xE2, 0x7A, 0x4B, 0x04 | t << 3, 0x0F]), sem


def sttilecfg_rdi_800():
    """STTILECFG [rdi + 800H]: VEX.128.66.0F38.W0 49 /0"""
    def sem(m):
        m.st.wr(m.st.regs[7] + 0x800, 64, int.from_bytes(m.tcfg if m.tcfg is not None else bytes(64), "little"))
    return bytes([0xC4, 0xE2, 0x79, 0x49, 0x87, 0x00, 0x08, 0x00, 0x00]), sem


def tilerelease():
    def sem(m):
        m.tcfg = None
        m.tiles = [[bytes(64)] * 16 for _ in range(8)]
    return bytes([0xC4, 0xE2, 0x78, 0x49, 0xC0]), sem


# --------------------------------------------------------------------------------------------
# a case: a list of instructions (bytes, semantics) run from SNIP; JMPABS continues at the
# instruction that starts at the target
# --------------------------------------------------------------------------------------------
def run(steps, m):
    offs, o = [], SNIP
    for b, _ in steps:
        offs.append(o)
        o += len(b)
    end = o
    i = 0
    while i < len(steps):
        m.jump = None
        steps[i][1](m)
        if m.jump is not None:
            if m.jump == end:
                return
            if m.jump not in offs:
                raise AssertionError("JMPABS target %X is not an instruction boundary" % m.jump)
            i = offs.index(m.jump)
        else:
            i += 1


def line(steps, regs=None, mem=None, rflags=0x202, cpl=0, title=None):
    st = St()
    used = []
    for r, v in (regs or {}).items():
        st.regs[r] = v & M64
        used.append(r)
    st.rflags = rflags
    runs = []
    for off, data in (mem or {}).items():
        st.mem[off:off + len(data)] = data
        runs.append((off, bytes(data)))
    m = Mach(st.copy(), cpl)
    fault = None
    try:
        run(steps, m)
    except Fault as f:
        fault = f.vec
    if None in m.st.regs:
        raise AssertionError("a register left undefined (CR4 in RAX) by the model")
    code = "; ".join(dotbyte(b) for b, _ in steps)
    inp = fmt_inputs(st, used, runs, extra=(["cpl=3"] if cpl == 3 else []))
    exp = fmt_changes(st, m.st)
    if fault is not None:
        exp = (exp + " " if exp else "") + {UD: "#UD", GP: "#GP"}[fault]
    return "%s%s => %s" % (code, (" | " + inp) if inp else "", exp)


def hexb(rng, n):
    return bytes(rng.randrange(256) for _ in range(n))


# --------------------------------------------------------------------------------------------
# the case file
# --------------------------------------------------------------------------------------------
def gen_cases(out):
    rng = random.Random(0x1F13)
    L = []
    c = lambda s: L.append("# " + s)
    a = L.append
    c("Intel APX: expected-value cases for the ten forms without one after U690 (plan 1.F.13, ledger U790):")
    c("JMPABS, EVEX ENQCMD/ENQCMDS, EVEX URDMSR/UWRMSR, EVEX WRSSD/WRSSQ, EVEX WRUSSD/WRUSSQ, EVEX TILELOADDT1.")
    c("Generated by Emulator/tools/isa/ref_apx_cases.py --cases (independent model from APX spec 355828-009")
    c("and SDM 092; see its docstring); regenerate, do not edit. Unicorn only, CPL0 unless cpl=3:")
    c("  emu-alltest --cases Emulator\\data\\cases_apx_sys.txt --apx --amx --expect-only")
    c("Each instruction is its own .byte run. CET cases set CR0.WP and CR4.CET inline (MOV CR0/CR4, BTS,")
    c("PUSH 202H; POPFQ), then the MSRs by WRMSR; shadow stacks are plain memory (no paging in the harness).")
    c("")
    c("--- layout guard: the snippet starts at 30000199h (lea rax, [rip] = 300001A0h); the JMPABS targets depend on it")
    a(".byte 0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00 => rax=0x%x" % (SNIP + 7))
    a(".byte 0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00 | cpl=3 => rax=0x%x" % (SNIP + 7))

    # ---------------- JMPABS
    c("--- JMPABS (REX2 M0 = 0 W = 0 A1 target64): RIP := target64 inside the snippet; the skipped instruction would fault (UD2) or change RAX")
    ud2 = (b"\x0f\x0b", lambda m: (_ for _ in ()).throw(Fault(UD)))
    mov_eax_1 = mov_r32_imm(0, 1)
    mov_ebx_2 = mov_r32_imm(3, 2)
    for payload, pfx, skip, note in ((0x00, b"", [ud2], "forward over UD2"),
                                     (0x00, b"", [mov_eax_1], "forward over MOV EAX, 1"),
                                     (0x77, b"", [ud2], "payload R4 X4 B4 R3 X3 B3 = 1 (ignored)"),
                                     (0x00, b"\x2e", [ud2], "CS override (ignored)"),
                                     (0x00, b"\x64", [mov_eax_1, ud2], "FS override (ignored), over two instructions")):
        tgt = SNIP + 3 + len(pfx) + 8 + sum(len(s[0]) for s in skip)
        c("JMPABS %s" % note)
        a(line([jmpabs(tgt, payload, pfx)] + skip + [mov_ebx_2]))
    c("JMPABS forward to L1 (MOV EAX, 1), backward to L0 (MOV EBX, 2), then to the end of the snippet (the epilogue)")
    # snippet: jmpabs L1 ; L0: mov ebx, 2 ; jmpabs END ; L1: mov eax, 1 ; jmpabs L0
    j1 = SNIP
    l0 = j1 + 11
    j2 = l0 + 5
    l1 = j2 + 11
    j3 = l1 + 5
    end = j3 + 11
    a(line([jmpabs(l1), mov_ebx_2, jmpabs(end), mov_eax_1, jmpabs(l0)]))
    c("JMPABS at CPL3 (cpl=3: the same snippet address, checked by the second guard line)")
    a(line([jmpabs(SNIP + 13), ud2, mov_ebx_2], cpl=3))
    c("JMPABS to a non-canonical target: #GP(0), nothing changed")
    a(line([jmpabs(0x0000800000000000)]))

    # ---------------- ENQCMD / ENQCMDS
    c("--- EVEX ENQCMD / ENQCMDS (MAP4 F2 / F3 F8, ModRM.reg = destination address, r/m = m512 source):")
    c("ordinary memory is not an enqueue register: nothing is written, ZF = 1 (retry), OF SF AF PF CF = 0")
    pasid = set_msr(MSR_PASID, 0x80000000 | 0x5A5A5)
    src_ok = bytes(4) + hexb(rng, 60)                 # ENQCMD: bits 31:0 = 0
    regs = {17: MEM_DST + 0x40, 18: MEM_PTR + 0x200}
    c("ENQCMD r17, [r18] with IA32_PASID = 8005A5A5H; flags in 0xAD7 -> ZF only")
    a(line(pasid + [enqcmd(False, 17, Mem(18))], regs, {0x8200: src_ok}, rflags=0xAD7))
    c("ENQCMD rbx, [r20 + r21*8 + 40H] (EGPR base and index)")
    a(line(pasid + [enqcmd(False, 3, Mem(20, 21, 8, 0x40))], {3: MEM_DST + 0x1C0, 20: MEM_PTR + 0x100, 21: 0x10},
           {0x81C0: src_ok}, rflags=0x203))
    c("ENQCMD with 67H: 32-bit destination register r25d and source [r26d] (bits 63:32 of both registers ignored)")
    a(line(pasid + [enqcmd(False, 25, Mem(26, asize=32), a32=True)],
           {25: 0xDEAD000000000000 | (MEM_DST + 0x80), 26: 0xBEEF000000000000 | (MEM_PTR + 0x300)}, {0x8300: src_ok}, rflags=0x2C3))
    c("ENQCMD faults: IA32_PASID.valid = 0 (no WRMSR), destination not 64-byte aligned, source bits 19:0 / 30:20 not 0")
    a(line([enqcmd(False, 17, Mem(18))], regs, {0x8200: src_ok}))
    a(line(pasid + [enqcmd(False, 17, Mem(18))], {17: MEM_DST + 0x48, 18: MEM_PTR + 0x200}, {0x8200: src_ok}))
    a(line(pasid + [enqcmd(False, 17, Mem(18))], regs, {0x8200: b"\x01\x00\x00\x00" + src_ok[4:]}))
    a(line(pasid + [enqcmd(False, 17, Mem(18))], regs, {0x8200: b"\x00\x00\x10\x00" + src_ok[4:]}))
    src_s = (0x80000000 | 0x12345).to_bytes(4, "little") + hexb(rng, 60)   # bit 31 = supervisor, 19:0 = PASID
    c("ENQCMDS (CPL0) r16, [r19]: source bits 31 and 19:0 set (allowed) -> ZF = 1, nothing written")
    a(line([enqcmds_ins(16, Mem(19))], {16: MEM_DST + 0x100, 19: MEM_PTR + 0x400}, {0x8400: src_s}, rflags=0xA97))
    a(line([enqcmds_ins(30, Mem(29, 28, 2, -8))], {30: MEM_DST, 29: MEM_PTR + 0x40, 28: 0x84}, {0x8140: src_s}, rflags=0x202))
    c("ENQCMDS faults: source bits 30:20 not 0, destination misaligned, CPL3 (cpl=3)")
    a(line([enqcmds_ins(16, Mem(19))], {16: MEM_DST + 0x100, 19: MEM_PTR + 0x400},
           {0x8400: (0x80100000).to_bytes(4, "little") + src_s[4:]}))
    a(line([enqcmds_ins(16, Mem(19))], {16: MEM_DST + 0x101, 19: MEM_PTR + 0x400}, {0x8400: src_s}))
    a(line([enqcmds_ins(16, Mem(19))], {16: MEM_DST + 0x100, 19: MEM_PTR + 0x400}, {0x8400: src_s}, cpl=3))

    # ---------------- URDMSR / UWRMSR
    c("--- EVEX URDMSR / UWRMSR (MAP4.W0 F2 / F3 F8 11:rrr:bbb): IA32_USER_MSR_CTL (1CH) = bitmap at MEM+8000H (RSI) | ENABLE, by WRMSR")
    ctl = set_msr(MSR_USER_MSR_CTL, MEM_PTR | 1)
    rd1c = {0x8003: b"\x10"}                       # read bitmap bit 1CH (byte 3, bit 4)
    rd1b01 = {0x8360: b"\x02"}                     # read bitmap bit 1B01H (byte 360H, bit 1)
    wr1b01 = {0x8B60: b"\x02"}                     # write bitmap bit 1B01H (800H + 360H)
    c("URDMSR r20, r21 (r21 = 1CH): reads IA32_USER_MSR_CTL itself")
    a(line(ctl + [urdmsr(20, 21)], {21: MSR_USER_MSR_CTL, 20: 0x1111111111111111}, rd1c))
    c("URDMSR rbx, r31 / URDMSR r17, rcx")
    a(line(ctl + [urdmsr(3, 31)], {31: MSR_USER_MSR_CTL}, rd1c))
    a(line(ctl + [mov_r32_imm(1, MSR_USER_MSR_CTL), urdmsr(17, 1)], {17: M64}, rd1c))
    c("UWRMSR r16 (= 1B01H, IA32_UARCH_MISC_CTL), r17 (= 1: DOITM), then URDMSR r18, r16 reads it back; and a write of 0")
    m3 = dict(rd1b01)
    m3.update(wr1b01)
    a(line(ctl + [uwrmsr(16, 17), urdmsr(18, 16)], {16: MSR_UARCH_MISC_CTL, 17: 1, 18: 0x5555}, m3))
    a(line(ctl + [uwrmsr(16, 17), uwrmsr(16, 19), urdmsr(18, 16)], {16: MSR_UARCH_MISC_CTL, 17: 1, 19: 0, 18: 0x5555}, m3))
    c("URDMSR 1B01H after reset: 0")
    a(line(ctl + [urdmsr(22, 23)], {23: MSR_UARCH_MISC_CTL, 22: 0xFFFF}, rd1b01))
    c("faults: IA32_USER_MSR_CTL.ENABLE = 0 -> #UD; bitmap bit clear, address >= 4000H, UWRMSR of another MSR, reserved DOITM bits -> #GP; EVEX.W1 -> #UD")
    a(line([urdmsr(20, 21)], {21: MSR_USER_MSR_CTL}, rd1c))
    a(line([uwrmsr(16, 17)], {16: MSR_UARCH_MISC_CTL, 17: 1}, m3))
    a(line(ctl + [urdmsr(20, 21)], {21: MSR_USER_MSR_CTL}))
    a(line(ctl + [urdmsr(20, 21)], {21: 0x401C}, rd1c))
    a(line(ctl + [uwrmsr(16, 17)], {16: MSR_UARCH_MISC_CTL, 17: 1}, rd1b01))
    a(line(ctl + [uwrmsr(16, 17)], {16: MSR_USER_MSR_CTL, 17: MEM_PTR | 1}, {0x8803: b"\x10"}))
    a(line(ctl + [uwrmsr(16, 17)], {16: MSR_UARCH_MISC_CTL, 17: 2}, m3))
    a(line(ctl + [urdmsr(20, 21, w=1)], {21: MSR_USER_MSR_CTL}, rd1c))

    # ---------------- WRSSD / WRSSQ
    c("--- EVEX WRSSD / WRSSQ (NP MAP4.W0 / W1 66 !(11)): CPL0, CR4.CET = 1, IA32_S_CET (6A2H) = SH_STK_EN | WR_SHSTK_EN = 3")
    s_cet3 = [cet_on()] + set_msr(MSR_S_CET, 3)
    for q, mem, regs, note in ((1, Mem(16, disp=8), {16: MEM_PTR, 17: 0x0123456789ABCDEF}, "WRSSQ [r16 + 8], r17"),
                               (0, Mem(16, disp=8), {16: MEM_PTR, 17: 0x0123456789ABCDEF}, "WRSSD [r16 + 8], r17d"),
                               (1, Mem(20, 24, 4, -0x10), {20: MEM_DST, 24: 0x24, 17: 0xFEDCBA9876543210}, "WRSSQ [r20 + r24*4 - 10H], r17"),
                               (0, Mem(3, 29, 1, 0x7C), {3: MEM_PTR + 0x100, 29: 0x40, 17: 0xCAFEF00D}, "WRSSD [rbx + r29 + 7CH], r17d")):
        c(note)
        a(line(s_cet3 + [wrss(q, mem, 17)], regs, {0x8000: hexb(rng, 16)}))
    c("faults: IA32_S_CET.WR_SHSTK_EN = 0 or SH_STK_EN = 0 -> #UD; CR4.CET = 0 -> #UD; misaligned (+2) -> #GP")
    a(line([cet_on()] + set_msr(MSR_S_CET, 1) + [wrss(1, Mem(16, disp=8), 17)], {16: MEM_PTR, 17: 1}))
    a(line([cet_on()] + set_msr(MSR_S_CET, 2) + [wrss(0, Mem(16, disp=8), 17)], {16: MEM_PTR, 17: 1}))
    a(line(set_msr(MSR_S_CET, 3) + [wrss(1, Mem(16, disp=8), 17)], {16: MEM_PTR, 17: 1}))
    a(line(s_cet3 + [wrss(1, Mem(16, disp=10), 17)], {16: MEM_PTR, 17: 1}))
    a(line(s_cet3 + [wrss(0, Mem(16, disp=6), 17)], {16: MEM_PTR, 17: 1}))

    # ---------------- WRUSSD / WRUSSQ
    c("--- EVEX WRUSSD / WRUSSQ (66 MAP4.W0 / W1 65 !(11)): CPL0 with CR4.CET = 1 (no IA32_S_CET needed)")
    cet_only = [cet_on(), mov_r32_imm(0, 0)]
    for q, mem, regs, note in ((1, Mem(19), {19: MEM_DST + 0x18, 20: 0x8877665544332211}, "WRUSSQ [r19], r20"),
                               (0, Mem(19), {19: MEM_DST + 0x1C, 20: 0x8877665544332211}, "WRUSSD [r19], r20d"),
                               (1, Mem(27, 18, 8, 0x100), {27: MEM_PTR, 18: 3, 20: 0x0F1E2D3C4B5A6978}, "WRUSSQ [r27 + r18*8 + 100H], r20"),
                               (0, Mem(6, disp=-4), {20: 0x13579BDF}, "WRUSSD [rsi - 4], r20d")):
        c(note)
        a(line(cet_only + [wruss(q, mem, 20)], regs, {0x7FF0: hexb(rng, 32)}))
    c("faults: CR4.CET = 0 -> #UD (also at CPL3: checked before the CPL); CPL3 with CR4.CET = 1 cannot be set up from CPL3; misaligned -> #GP")
    a(line([wruss(1, Mem(19), 20)], {19: MEM_DST, 20: 1}))
    a(line([wruss(0, Mem(19), 20)], {19: MEM_DST, 20: 1}, cpl=3))
    a(line(cet_only + [wruss(1, Mem(19), 20)], {19: MEM_DST + 2, 20: 1}))
    a(line(cet_only + [wruss(0, Mem(19), 20)], {19: MEM_DST + 1, 20: 1}))

    # ---------------- TILELOADDT1
    c("--- EVEX TILELOADDT1 (MAP2 66 4B !(11):rrr:100, W0): LDTILECFG [rsi] (VEX), TILELOADDT1 tmm, [EGPR base + EGPR index],")
    c("TILESTORED [rdi + rcx], tmm (VEX), STTILECFG [rdi + 800H] (VEX), TILERELEASE: the tile rows land in operand memory")

    def cfg(t, colsb, rows):
        b = bytearray(64)
        b[0] = 1
        b[16 + 2 * t] = colsb & 0xFF
        b[17 + 2 * t] = colsb >> 8
        b[48 + t] = rows
        return bytes(b)

    for t, colsb, rows, mem, regs, stride_out in (
            (1, 16, 3, Mem(16, 17, 1), {16: MEM_PTR + 0x100, 17: 0x30}, 0x20),
            (5, 64, 2, Mem(20, 21, 2, 0x10), {20: MEM_PTR + 0x200, 21: 0x40}, 0x40),
            (0, 4, 16, Mem(30, 31, 4, -0x20), {30: MEM_PTR + 0x420, 31: 0x4}, 0x10),
            (7, 32, 2, Mem(18, 25, 8), {18: MEM_PTR + 0x700, 25: 0x10}, 0x40)):
        regs = dict(regs)
        regs[1] = stride_out
        mm = {0x8000: cfg(t, colsb, rows)}
        st = St()
        for r, v in regs.items():
            st.regs[r] = v
        b0 = st.regs[mem.base] + mem.disp - MEM          # membegin = base + displacement
        stride = st.regs[mem.index] * mem.scale          # the index register is the stride
        for r in range(rows):
            mm[b0 + r * stride] = hexb(rng, colsb)
        c("TILELOADDT1 tmm%d (rows %d, colsb %d), base r%d index r%d scale %d disp %d" % (t, rows, colsb, mem.base, mem.index, mem.scale, mem.disp))
        a(line([ldtilecfg_rsi(), tileloaddt1(t, mem), tilestored_rdi_rcx(t), sttilecfg_rdi_800(), tilerelease()], regs, mm))
    c("TILELOADDT1 without TILES_CONFIGURED -> #UD; on an unconfigured tile (rows 0) -> #UD")
    a(line([tileloaddt1(1, Mem(16, 17, 1))], {16: MEM_PTR + 0x100, 17: 0x30}))
    a(line([ldtilecfg_rsi(), tileloaddt1(2, Mem(16, 17, 1))], {16: MEM_PTR + 0x100, 17: 0x30}, {0x8000: cfg(1, 16, 3)}))
    c("#UD encodings: no SIB (ModRM.r/m != 100), EVEX.W1, EVEX.vvvv != 1111, EVEX.b = 1, EVEX.L = 1")
    for kw in ({"rm": Mem(16)}, {"w": 1}, {"v": 1}, {"p2or": 0x10}, {"ll": 1}):
        rm = kw.pop("rm", Mem(16, 17, 1))
        a(line([ldtilecfg_rsi(), tileloaddt1_ud(1, rm, **kw), tilerelease()], {16: MEM_PTR + 0x100, 17: 0x30}, {0x8000: cfg(1, 16, 3)}))
    for s in L:
        out.write(s.rstrip() + "\n")


def enqcmds_ins(dst, src):
    return enqcmd(True, dst, src)


# --------------------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, exp):
        nonlocal ok
        if got != exp:
            ok = False
            print("FAIL %s: got %r expected %r" % (name, got, exp))
    # encodings worked by hand from APX Figure 3.3 (P0 = ~R3 ~X3 ~B3 ~R4 B4 1 0 0 for map 4,
    # P1 = W ~V3..~V0 U pp, P2 = 0 0 0 ND ~V4 NF 0 0):
    # ENQCMD r17, [r18]: R = 17 (R3 0 -> 1, R4 1 -> 0), B = 18 (B3 0 -> 1, B4 = 1), X none (X3 1,
    # U = ~X4 = 1): P0 = 1 1 1 0 1 1 0 0 = ECh; P1 = 0 1111 1 11 = 7Fh; P2 = 08h; ModRM 00 001 010
    chk("enqcmd r17,[r18]", enqcmd(False, 17, Mem(18))[0], bytes([0x62, 0xEC, 0x7F, 0x08, 0xF8, 0x0A]))
    # ENQCMDS: pp = F3 (10b) -> P1 = 7Eh
    chk("enqcmds r17,[r18]", enqcmd(True, 17, Mem(18))[0], bytes([0x62, 0xEC, 0x7E, 0x08, 0xF8, 0x0A]))
    # URDMSR rax, rcx: R = 1, B = 0, mod 11: P0 = F4h, P1 = 0 1111 1 11 = 7Fh, ModRM 11 001 000 = C8h
    chk("urdmsr rax,rcx", urdmsr(0, 1)[0], bytes([0x62, 0xF4, 0x7F, 0x08, 0xF8, 0xC8]))
    # UWRMSR rcx(addr), rax(value): pp F3 -> P1 7Eh, ModRM C8h
    chk("uwrmsr rcx,rax", uwrmsr(1, 0)[0], bytes([0x62, 0xF4, 0x7E, 0x08, 0xF8, 0xC8]))
    # WRSSQ [rax], rcx: W1 NP -> P1 = 1 1111 1 00 = FCh, opcode 66, ModRM 00 001 000 = 08h
    chk("wrssq [rax],rcx", wrss(1, Mem(0), 1)[0], bytes([0x62, 0xF4, 0xFC, 0x08, 0x66, 0x08]))
    # WRUSSD [rax], rcx: W0 66 -> P1 = 0 1111 1 01 = 7Dh, opcode 65
    chk("wrussd [rax],rcx", wruss(0, Mem(0), 1)[0], bytes([0x62, 0xF4, 0x7D, 0x08, 0x65, 0x08]))
    # TILELOADDT1 tmm1, [rax + rcx]: map 2 (P0 = F2h), 66 W0, U = ~X4 = 1 -> P1 = 7Dh, ModRM 00 001 100 = 0Ch, SIB 00 001 000 = 08h
    chk("tileloaddt1", tileloaddt1(1, Mem(0, 1, 1))[0], bytes([0x62, 0xF2, 0x7D, 0x08, 0x4B, 0x0C, 0x08]))
    # JMPABS 0x1122334455667788: D5 00 A1 + imm64 little-endian, 11 bytes (APX 3.1.3.3)
    j = jmpabs(0x1122334455667788)[0]
    chk("jmpabs", (len(j), j[:3], j[3:]), (11, b"\xd5\x00\xa1", bytes([0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11])))
    # model: ENQCMD sets ZF only, writes nothing
    st = St()
    st.rflags = 0xAD7
    m = Mach(st)
    m.msr[MSR_PASID] = 0x80000001
    st.regs[17], st.regs[18] = MEM_DST, MEM_PTR
    enqcmd(False, 17, Mem(18))[1](m)
    chk("enqcmd flags", m.st.rflags, 0x242)
    chk("enqcmd mem", any(m.st.mem), False)
    # URDMSR bitmap: MSR 1CH -> byte 3 bit 4
    st = St()
    m = Mach(st)
    m.msr[MSR_USER_MSR_CTL] = MEM_PTR | 1
    st.mem[0x8003] = 0x10
    chk("user msr bit 1CH", user_msr_bit(m, 0x1C, False), 1)
    chk("user msr bit 1DH", user_msr_bit(m, 0x1D, False), 0)
    return ok


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        gen_cases(sys.stdout)
        return
    print(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main()
