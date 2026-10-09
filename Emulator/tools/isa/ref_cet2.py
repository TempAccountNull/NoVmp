r"""Independent reference model + expected-value case generator: Intel CET on far transfers.

Written from the Intel manuals only (the emulator's C sources were not read; no CPU measurements):
  * SDM Vol1 325462-092 ch. 18: 18.2.1 (SSP width / address size), 18.2.2 (ShadowStackEnabled,
    ShadowStackPush/Pop, shadow_stack_lock_cmpxchg8b), 18.2.3 (supervisor shadow-stack token:
    acquire / release, the 32-byte rule), 18.3 (IBT tracker); 13.5.9 (CET_U / CET_S state),
    13.4.2-13.4.3 (XSAVE header, compacted format), 13.11 (XSAVES), 13.12 (XRSTORS).
  * SDM Vol2A CALL (far CALL to code segments and 64-bit call gates, MORE-PRIVILEGE /
    SAME-PRIVILEGE), JMP (far JMP, IBT), IRET (IA-32e mode, RETURN-TO-SAME/OUTER-PRIVILEGE-LEVEL),
    ENDBR64, INCSSP, CLRSSBSY; Vol2B RET (far RET, IA-32E-MODE-RETURN-TO-SAME/OUTER),
    RDSSP, SETSSBSY, SYSEXIT, SYSRET; Vol2D XRSTORS / XSAVES (operation, #GP list).
  * SDM Vol4 IA32_U_CET (6A0H) / IA32_S_CET (6A2H) bit layout and WRMSR rules, IA32_PL0-3_SSP
    (6A4H-6A7H: bits 1:0 = 0, canonical), IA32_INTERRUPT_SSP_TABLE_ADDR (6A8H), IA32_XSS (DA0H),
    IA32_SYSENTER_CS (174H), IA32_STAR (C0000081H).
  * Intel APX spec 355828-009 3.1.2.1 (REX2) and XED cet-isa.xed.txt (ENDBR64 = F3 0F 1E FA,
    MOD=3 REG=7 RM=2, no register operand) for ENDBR64 with a REX2 prefix.

Modelling decisions (where the SDM is not explicit):
  a. A faulting check leaves every register unchanged (SSP, the CET MSRs, RSP, RFLAGS, CS/SS,
     CPL ...: the model snapshots the registers at the instruction start and restores them on a
     fault); memory stores already performed stay (e.g. a supervisor token set busy before a later
     fault stays busy).
  b. Shadow-stack address size: pops and the token release on the OLD shadow stack use the mode
     the transfer starts in, pushes and the token acquisition on the NEW shadow stack the target
     mode (Vol1 18.2.1). A non-canonical 64-bit shadow-stack address is #GP(0). (All cases here
     run 64-bit -> 64-bit.)
  c. LIP pushed by a far CALL to a code segment: conforming target: RIP for 64-bit operand size,
     else CS.base+EIP / CS.base+IP zero-extended; non-conforming target: RIP if the CALL executes
     in 64-bit mode, else CS.base+EIP; CS.base is 0 in 64-bit mode. Call gates: old CS.base + old
     RIP (RIP in 64-bit mode).
  d. IA32_PL3_SSP save on a CPL3 -> CPL<3 call gate uses LA_adjust in IA-32e mode (bits 63:N :=
     bit N-1, N = 57 because CPUID.(7,0):ECX.LA57 = 1 in the emulator's CPU model - checked by a
     loose guard case). The cases use SSP values for which LA_adjust is the identity.
  e. XINUSE for CET_U / CET_S is value-based: XINUSE[11] = (IA32_U_CET | IA32_PL3_SSP) != 0,
     XINUSE[12] = (PL0_SSP | PL1_SSP | PL2_SSP) != 0. XSAVES writes XSTATE_BV = RFBM & XINUSE
     and XCOMP_BV = RFBM | 2^63 (nothing else of the header) and saves only in-use components
     (compacted format from offset 576; CET_U 16 bytes then CET_S 24 bytes, no 64-byte alignment:
     CPUID.(0DH,0BH/0CH):ECX[1] = 0). No XSAVES follows an XRSTORS of the same area (the modified
     optimization never applies).
  f. XRSTORS #GP(0)s before loading anything when a CET value would be refused by WRMSR:
     IA32_U_CET bits 9:6 set, bits 11:10 (TRACKER, SUPPRESS) both 1, EB_LEG_BITMAP_BASE (63:12)
     non-canonical; IA32_PLi_SSP bits 1:0 != 0 or non-canonical. "Canonical" = CPU canonical
     (SDM Vol3A 4.5.3: WRMSR and XRSTORS loads of IA32_U_CET / IA32_S_CET / IA32_PLi_SSP /
     IA32_INTERRUPT_SSP_TABLE_ADDR are checked "independent of the current paging mode"): 57 bits
     when CPUID.(7,0):ECX.LA57 is reported (the emulator's model does, checked by a loose guard
     case), else 48 - constant MSR_CANON_BITS. (The original reading "relative to the current
     paging mode" was replaced by this pure-SDM rule, coordinator ruling / ledger U760.) Initialising a
     component (RFBM[i] = 1 and XSTATE_BV[i] = 0, or XCOMP_BV[i] = 0) zeroes its MSRs.
     IA32_S_CET and IA32_INTERRUPT_SSP_TABLE_ADDR are not XSAVE-managed.
  g. F3 REX2(M0 = 1, other payload bits arbitrary) 1E FA is ENDBR64 when REX2 is usable
     (CR4.OSXSAVE = 1, XCR0[19] = 1): APX 3.1.2.1 applies REX2 to every map-1 opcode except rows
     3x/8x and XSAVE*/XRSTOR*, and ENDBR64 has no register operand.
  h. SYSRET / SYSEXIT: after CPL := 3, if ShadowStackEnabled(3) then SSP := IA32_PL3_SSP (the full
     64-bit value; no alignment or canonical check, no supervisor-token release).
  Own choices (not exercised in a way that depends on them, stated for completeness):
  i. WRMSR uses the same CPU-canonical width as (f) (MSR_CANON_BITS = 57). Shadow-stack
     addresses, RIP and the SSP loaded by far RET / IRET are paging canonical: relative to the
     current paging mode, CR4.LA57 = 0 -> 48 bits (RET/IRET pseudocode "canonical relative to the
     current paging mode", Vol3A 4.5.1/4.5.2). So IA32_PL3_SSP = 0000800000000000h is accepted by
     WRMSR, loaded as is by SYSRET (no check, decision h), and makes a far RET / IRET to CPL3 #GP(0).
  j. Only the 64-bit operand size / 64-bit mode paths are implemented (the far-transfer
     functions raise ModelError elsewhere); LDT selectors are not modelled.

Harness (Emulator\tests\alltest\at_engine.hpp, docs\emu-alltest.md): each case runs in a fresh
Unicorn engine, 64-bit mode, CPL0, CS selector 0 (flat 64-bit cache), SS 0, no GDT/IDT/TSS, no
paging, IA32_EFER = 501H (SCE|LME|LMA), CET MSRs 0, CR0 = 10011H (--cr0), XCR0 = 80207H (--apx).
The snippet starts at 30000199H; operand memory MEM = 30020000H..3002FFFFH (compared), RSI = R14 =
MEM+8000H, RDI = MEM+9000H, RSP = 30013F00H (DATA, not compared). Every snippet here is encoded by
this file (".byte"), so its layout (far pointers, LIPs, gate offsets) is known exactly.

usage:
  python -I ref_cet2.py --selftest       hand-derived checks of the model (exit 0 on pass)
  python -I ref_cet2.py --cases FILE     writes the emu-alltest case file FILE
                                         (Emulator\data\cases_cet2.txt)
"""
import copy
import struct
import sys

M64 = (1 << 64) - 1

# ---- harness layout ----------------------------------------------------------------------------
CODE, DATA, MEM, MEM_SIZE = 0x30000000, 0x30010000, 0x30020000, 0x10000
SNIP = 0x30000199
STACK_TOP = DATA + 0x3F00
RSI0, RDI0 = MEM + 0x8000, MEM + 0x9000
RFLAGS0 = 0x202

# ---- machine environment (harness reset state, see the guard cases) ----------------------------
XCR0 = 0x80207          # x87 | SSE | AVX | PKRU | APX (bit 19), the --apx reset value
CR0 = 0x10011           # PE | ET | WP (--cr0)
EFER = 0x501            # SCE | LME | LMA
LA_PAGING = 48          # CR4.LA57 = 0: canonical relative to the current paging mode
LA_MAX = 57             # CPUID.(7,0):ECX.LA57 = 1: maximum linear-address width (LA_adjust N)
XSS_SUPPORTED = 0x1800  # CPUID.(0DH,1):ECX[12:11] (CET_U, CET_S)
CPUID_LA57 = True       # CPUID.(7,0):ECX[16] of the emulator's CPU model (loose guard case)
# canonicality width of WRMSR / XRSTORS loads of the CET MSRs: CPU canonical (SDM Vol3A 4.5.3),
# i.e. the maximum linear-address width, independent of CR4.LA57 (decision f)
MSR_CANON_BITS = LA_MAX if CPUID_LA57 else 48
# component sizes (CPUID.(0DH,i):EAX): AVX 256, PKRU 8 (Vol1 13.5), CET_U 16 / CET_S 24 (13.5.9),
# APX extended GPRs R16-R31 = 128 (APX spec); none has the 64-byte alignment bit
XCOMP_SIZE = {2: 256, 9: 8, 11: 16, 12: 24, 19: 128}

# ---- MSRs / bits -------------------------------------------------------------------------------
U_CET, S_CET, PL0_SSP, PL1_SSP, PL2_SSP, PL3_SSP, ISST = 0x6A0, 0x6A2, 0x6A4, 0x6A5, 0x6A6, 0x6A7, 0x6A8
XSS, SYSENTER_CS, STAR = 0xDA0, 0x174, 0xC0000081
SH_STK_EN, WR_SHSTK_EN, ENDBR_EN, NO_TRACK_EN = 1, 2, 4, 16
SUPPRESS, TRACKER = 1 << 10, 1 << 11
CR4_CET = 1 << 23

RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 = range(16)
REGN = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]


class Fault(Exception):
    def __init__(self, name, code=0, why=""):
        Exception.__init__(self, "#%s(%#x) %s" % (name, code, why))
        self.name, self.code, self.why = name, code, why


class ModelError(Exception):
    pass


def canonical(v, width=LA_PAGING):
    top = (v & M64) >> (width - 1)
    return top == 0 or top == (1 << (65 - width)) - 1


def la_adjust(v, n=LA_MAX):
    """CALL pseudocode: bits 63:N get the value of bit N-1."""
    low = v & ((1 << n) - 1)
    return low | ((M64 ^ ((1 << n) - 1)) if (v >> (n - 1)) & 1 else 0)


def q(v):
    return struct.pack("<Q", v & M64)


# ---- descriptors (generator side) --------------------------------------------------------------
def d_code64(dpl, conf=False):
    """64-bit code segment: L = 1, D = 0, G = 1, limit FFFFFH, base 0, accessed."""
    acc = 0x80 | (dpl << 5) | 0x10 | (0xF if conf else 0xB)
    return (0xAF << 48) | (acc << 40) | 0xFFFF


def d_data(dpl):
    """writable data segment, accessed, B = 1, G = 1."""
    return (0xCF << 48) | ((0x80 | (dpl << 5) | 0x13) << 40) | 0xFFFF


def d_gate64(sel, off, dpl):
    lo = (off & 0xFFFF) | (sel << 16) | ((0x80 | (dpl << 5) | 0xC) << 40) | (((off >> 16) & 0xFFFF) << 48)
    return lo, (off >> 32) & 0xFFFFFFFF


def d_tss64(base, limit):
    lo = ((limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89 << 40) | (((limit >> 16) & 0xF) << 48) |
          (((base >> 24) & 0xFF) << 56))
    return lo, (base >> 32) & 0xFFFFFFFF


class Desc:
    pass


def parse_desc(lo, hi=0):
    d = Desc()
    acc = (lo >> 40) & 0xFF
    d.type, d.s, d.dpl, d.p = acc & 0xF, (acc >> 4) & 1, (acc >> 5) & 3, acc >> 7
    d.limit = (lo & 0xFFFF) | (((lo >> 48) & 0xF) << 16)
    d.base = ((lo >> 16) & 0xFFFFFF) | (((lo >> 56) & 0xFF) << 24)
    d.L, d.D, d.G = (lo >> 53) & 1, (lo >> 54) & 1, (lo >> 55) & 1
    if d.G:
        d.limit = (d.limit << 12) | 0xFFF
    d.code = bool(d.s and d.type & 8)
    d.conf = bool(d.code and d.type & 4)
    d.wdata = bool(d.s and not d.type & 8 and d.type & 2)
    d.gsel = (lo >> 16) & 0xFFFF
    d.goff = (lo & 0xFFFF) | (((lo >> 48) & 0xFFFF) << 16) | ((hi & 0xFFFFFFFF) << 32)
    d.hi = hi
    if not d.s:
        d.base |= (hi & 0xFFFFFFFF) << 32
    return d


class Seg:
    def __init__(self, sel, base=0, L=1, D=0, dpl=0, conf=False, code=True):
        self.sel, self.base, self.L, self.D, self.dpl, self.conf, self.code = sel, base, L, D, dpl, conf, code


def seg_from(d, sel):
    return Seg(sel & 0xFFFF, d.base, d.L, d.D, d.dpl, d.conf, d.code)


# ---- the machine -------------------------------------------------------------------------------
class Machine:
    def __init__(self, regs=None, mem=None, rflags=RFLAGS0):
        self.g = [0] * 16
        self.g[RSP], self.g[RSI], self.g[RDI], self.g[R14] = STACK_TOP, RSI0, RDI0, RSI0
        for k, v in (regs or {}).items():
            self.g[REGN.index(k)] = v & M64
        self.rflags = rflags
        self.mem = {}
        for a, data in (mem or {}).items():
            for i, b in enumerate(data):
                self.mem[a + i] = b
        self.cpl = 0
        self.cs = Seg(0)
        self.ss = Seg(0, code=False)
        self.cr4 = 0                    # only the bits this model sets (CET); OSXSAVE assumed 1
        self.msr = {U_CET: 0, S_CET: 0, PL0_SSP: 0, PL1_SSP: 0, PL2_SSP: 0, PL3_SSP: 0, ISST: 0,
                    XSS: 0, SYSENTER_CS: 0, STAR: 0}
        self.ssp = 0
        self.gdtr = (0, 0xFFFF)
        self.tr = None
        self.lab = {}

    # registers other than memory, for decision (a)
    def snap(self):
        return {k: copy.deepcopy(v) for k, v in vars(self).items() if k not in ("mem", "lab")}

    def restore(self, s):
        for k, v in s.items():
            setattr(self, k, v)

    # memory
    @staticmethod
    def _chk(a, n):
        for lo, size in ((CODE, 0x10000), (DATA, 0x10000), (MEM, MEM_SIZE)):
            if lo <= a and a + n <= lo + size:
                return
        raise ModelError("access outside the harness regions: %#x" % a)

    def rd(self, a, n):
        self._chk(a, n)
        return int.from_bytes(bytes(self.mem.get(a + i, 0) for i in range(n)), "little")

    def wr(self, a, n, v):
        self._chk(a, n)
        for i, b in enumerate((v & ((1 << (8 * n)) - 1)).to_bytes(n, "little")):
            self.mem[a + i] = b

    def push8(self, v):
        self.g[RSP] = (self.g[RSP] - 8) & M64
        self.wr(self.g[RSP], 8, v)

    def pop8(self):
        v = self.rd(self.g[RSP], 8)
        self.g[RSP] = (self.g[RSP] + 8) & M64
        return v

    # ---- CET primitives (Vol1 18.2.2) ----
    def cet_msr(self, cpl):
        return U_CET if cpl == 3 else S_CET

    def ss_enabled(self, cpl):
        """ShadowStackEnabled(CPL): CR4.CET = 1 and CR0.PE = 1 and EFLAGS.VM = 0 and xx_CET.SH_STK_EN."""
        if not (self.cr4 & CR4_CET and CR0 & 1 and not self.rflags & (1 << 17)):
            return False
        return bool(self.msr[self.cet_msr(cpl)] & SH_STK_EN)

    def endbr_enabled(self, cpl):
        return bool(self.cr4 & CR4_CET and self.msr[self.cet_msr(cpl)] & ENDBR_EN)

    def ss_addr(self, a):
        a &= M64
        if not canonical(a):
            raise Fault("GP", 0, "non-canonical shadow-stack address (decision b)")
        return a

    def ss_store(self, a, n, v):
        self.wr(self.ss_addr(a), n, v)

    def ss_load8(self, a):
        return self.rd(self.ss_addr(a), 8)

    def ss_push8(self, v):
        self.ssp = (self.ssp - 8) & M64
        self.ss_store(self.ssp, 8, v)

    def ss_cmpxchg8(self, a, new, expected):
        """shadow_stack_lock_cmpxchg8B: writes new if equal, else writes back what it read."""
        old = self.ss_load8(a)
        self.ss_store(a, 8, new if old == expected else old)
        return old

    def set_tracker_wait(self, cpl):
        i = self.cet_msr(cpl)
        self.msr[i] = (self.msr[i] | TRACKER) & ~SUPPRESS

    # ---- descriptor tables ----
    def read_desc(self, sel):
        if sel & 4:
            raise ModelError("LDT selectors are not modelled")
        base, limit = self.gdtr
        idx = sel & 0xFFF8
        if idx + 7 > limit:
            raise Fault("GP", sel & 0xFFFC, "selector beyond the GDT limit")
        lo = self.rd(base + idx, 8)
        if not (lo >> 44) & 1:      # system descriptor: 16 bytes in IA-32e mode
            if idx + 15 > limit:
                raise Fault("GP", sel & 0xFFFC, "system descriptor beyond the GDT limit")
            return parse_desc(lo, self.rd(base + idx + 8, 8))
        return parse_desc(lo)

    # ---- MSRs (Vol4) ----
    def msr_ok(self, idx, v):
        if idx in (U_CET, S_CET):
            return not (v & 0x3C0) and (v & (SUPPRESS | TRACKER)) != (SUPPRESS | TRACKER) and \
                canonical(v & ~0xFFF, MSR_CANON_BITS)
        if idx in (PL0_SSP, PL1_SSP, PL2_SSP, PL3_SSP):
            return not v & 3 and canonical(v, MSR_CANON_BITS)
        if idx == ISST:
            return canonical(v, MSR_CANON_BITS)
        if idx == XSS:
            return not v & ~XSS_SUPPORTED
        if idx in (SYSENTER_CS, STAR):
            return True
        raise ModelError("MSR %#x not modelled" % idx)

    def wrmsr(self, idx, v):
        if self.cpl:
            raise Fault("GP", 0, "WRMSR at CPL > 0")
        if not self.msr_ok(idx, v):
            raise Fault("GP", 0, "WRMSR %#x value %#x refused" % (idx, v))
        self.msr[idx] = v & M64

    def rdmsr(self, idx):
        if self.cpl:
            raise Fault("GP", 0, "RDMSR at CPL > 0")
        if idx not in self.msr:
            raise ModelError("MSR %#x not modelled" % idx)
        return self.msr[idx]

    # ---- shadow-stack management instructions ----
    def setssbsy(self):
        if not self.cr4 & CR4_CET or not self.msr[S_CET] & SH_STK_EN:
            raise Fault("UD", 0, "SETSSBSY")
        if self.cpl:
            raise Fault("GP", 0, "SETSSBSY at CPL > 0")
        la = self.msr[PL0_SSP]
        if la & 7:
            raise Fault("GP", 0, "SETSSBSY: IA32_PL0_SSP not 8-byte aligned")
        if self.ss_cmpxchg8(la, la | 1, la) != la:
            raise Fault("CP", 5, "SETSSBSY: token busy or address mismatch")
        self.ssp = la

    def incssp(self, n, w64):
        if not (self.cr4 & CR4_CET and self.msr[self.cet_msr(self.cpl)] & SH_STK_EN):
            raise Fault("UD", 0, "INCSSP without shadow stacks")
        n &= 0xFF
        sz = 8 if w64 else 4
        self.rd(self.ss_addr(self.ssp), sz)
        if n:
            self.rd(self.ss_addr(self.ssp + sz * (n - 1)), sz)
        self.ssp = (self.ssp + n * sz) & M64

    def rdssp(self):
        """RDSSPQ: the SSP when shadow stacks are enabled at the CPL, else None (NOP)."""
        if self.cr4 & CR4_CET and self.msr[self.cet_msr(self.cpl)] & SH_STK_EN:
            return self.ssp
        return None

    # ---- far CALL (Vol2A CALL, 64-bit operand size, IA-32e mode) ----
    def far_call(self, off, sel, nxt):
        if not self.cs.L:
            raise ModelError("only far CALL in 64-bit mode is modelled")
        if not sel & 0xFFFC:
            raise Fault("GP", 0, "far CALL: null selector")
        d = self.read_desc(sel)
        if d.code:
            return self._call_code(d, off, sel, nxt)
        if not d.s and d.type == 0xC:
            return self._call_gate(d, sel, nxt)
        raise Fault("GP", sel & 0xFFFC, "far CALL: not a code segment or 64-bit call gate")

    def _call_code(self, d, off, sel, nxt):
        if d.L and d.D:
            raise Fault("GP", sel & 0xFFFC, "L = D = 1")
        if d.conf:
            if d.dpl > self.cpl:
                raise Fault("GP", sel & 0xFFFC, "conforming DPL > CPL")
        elif (sel & 3) > self.cpl or d.dpl != self.cpl:
            raise Fault("GP", sel & 0xFFFC, "non-conforming RPL > CPL or DPL != CPL")
        if not d.p:
            raise Fault("NP", sel & 0xFFFC)
        if not d.L:
            raise ModelError("far CALL into compatibility mode is not modelled")
        tgt = off & M64
        if not canonical(tgt):
            raise Fault("GP", 0, "target RIP non-canonical")
        sse = self.ss_enabled(self.cpl)
        if sse:
            # decision (c): conforming: RIP (operand size 64); non-conforming: RIP (64-bit mode)
            lip = nxt if (d.conf or self.cs.L) else (self.cs.base + nxt) & 0xFFFFFFFF
            pcs = self.cs.sel
        self.push8(self.cs.sel)
        self.push8(nxt)
        self.cs = seg_from(d, (sel & ~3) | self.cpl)
        if sse:
            tmp = self.ssp
            self.ss_store(self.ssp - 4, 4, 0)
            self.ssp &= ~7 & M64
            self.ss_push8(pcs)
            self.ss_push8(lip)
            self.ss_push8(tmp)
        if self.endbr_enabled(self.cpl):
            self.set_tracker_wait(self.cpl)
        return tgt

    def _call_gate(self, gate, sel, nxt):
        if gate.dpl < self.cpl or (sel & 3) > gate.dpl:
            raise Fault("GP", sel & 0xFFFC, "call gate DPL < CPL or RPL > DPL")
        if not gate.p:
            raise Fault("NP", sel & 0xFFFC)
        if (gate.hi >> 40) & 0x1F:
            raise Fault("GP", sel & 0xFFFC, "64-bit gate upper type field not 0")
        csel = gate.gsel
        if not csel & 0xFFFC:
            raise Fault("GP", 0, "gate code selector null")
        cd = self.read_desc(csel)
        if not cd.code or cd.dpl > self.cpl:
            raise Fault("GP", csel & 0xFFFC, "gate target not code or DPL > CPL")
        if not cd.L or cd.D:
            raise Fault("GP", csel & 0xFFFC, "gate target not 64-bit code")
        if not cd.p:
            raise Fault("NP", csel & 0xFFFC)
        tgt = gate.goff
        if not cd.conf and cd.dpl < self.cpl:
            # MORE-PRIVILEGE (64-bit TSS)
            new = cd.dpl
            if self.tr is None:
                raise ModelError("no TSS")
            tsel, tbase, tlimit = self.tr
            a = new * 8 + 4
            if a + 7 > tlimit:
                raise Fault("TS", tsel & 0xFFFC)
            newrsp = self.rd(tbase + a, 8)
            if not canonical(tgt):
                raise Fault("GP", 0, "gate target non-canonical")
            old_ss, old_rsp, old_cs, old_cpl = self.ss, self.g[RSP], self.cs, self.cpl
            self.ss = Seg(new, dpl=new, code=False)
            self.g[RSP] = newrsp
            self.cs = seg_from(cd, (csel & ~3) | new)
            self.push8(old_ss.sel)
            self.push8(old_rsp)
            self.push8(old_cs.sel)
            self.push8(nxt)
            if self.ss_enabled(old_cpl) and old_cpl == 3:
                self.msr[PL3_SSP] = la_adjust(self.ssp)          # decision (d)
            self.cpl = new
            if self.ss_enabled(self.cpl):
                old_ssp = self.ssp
                ssp = self.msr[PL0_SSP + self.cpl]
                self.ssp = ssp
                if ssp & 7:
                    raise Fault("GP", 0, "IA32_PL%d_SSP not 8-byte aligned" % self.cpl)
                if (ssp & ~0x1F) != ((ssp - 24) & ~0x1F & M64):
                    raise Fault("GP", 0, "token + frame cross a 32-byte boundary")
                if self.ss_cmpxchg8(ssp, ssp | 1, ssp) != ssp:
                    raise Fault("GP", 0, "supervisor token busy or address mismatch")
                if old_ss.dpl != 3:
                    self.ss_push8(old_cs.sel)
                    self.ss_push8(old_cs.base + nxt)
                    self.ss_push8(old_ssp)
            if self.endbr_enabled(self.cpl):
                self.set_tracker_wait(self.cpl)
            return tgt
        # SAME-PRIVILEGE
        if not canonical(tgt):
            raise Fault("GP", 0, "gate target non-canonical")
        old_cs = self.cs
        self.cs = seg_from(cd, (csel & ~3) | self.cpl)
        self.push8(old_cs.sel)
        self.push8(nxt)
        if self.ss_enabled(self.cpl):
            tmp = self.ssp
            self.ss_store(self.ssp - 4, 4, 0)
            self.ssp &= ~7 & M64
            self.ss_push8(old_cs.sel)
            self.ss_push8(old_cs.base + nxt)              # decision (c): call gate
            self.ss_push8(tmp)
        if self.endbr_enabled(self.cpl):
            self.set_tracker_wait(self.cpl)
        return tgt

    # ---- far JMP (code segments only) ----
    def far_jmp(self, off, sel):
        if not sel & 0xFFFC:
            raise Fault("GP", 0, "far JMP: null selector")
        d = self.read_desc(sel)
        if not d.code:
            raise ModelError("far JMP: only code segments are modelled")
        if d.L and d.D:
            raise Fault("GP", sel & 0xFFFC)
        if d.conf:
            if d.dpl > self.cpl:
                raise Fault("GP", sel & 0xFFFC)
        elif (sel & 3) > self.cpl or d.dpl != self.cpl:
            raise Fault("GP", sel & 0xFFFC)
        if not d.p:
            raise Fault("NP", sel & 0xFFFC)
        if not d.L or not canonical(off):
            raise ModelError("far JMP: 64-bit canonical targets only")
        self.cs = seg_from(d, (sel & ~3) | self.cpl)
        if self.endbr_enabled(self.cpl):
            self.set_tracker_wait(self.cpl)
        return off & M64

    # ---- return-path helpers ----
    def _ret_cs_checks(self, csel):
        if not csel & 0xFFFC:
            raise Fault("GP", 0, "return CS null")
        d = self.read_desc(csel)
        rpl = csel & 3
        if not d.code:
            raise Fault("GP", csel & 0xFFFC, "return CS not code")
        if d.L and d.D:
            raise Fault("GP", csel & 0xFFFC, "L = D = 1")
        if rpl < self.cpl:
            raise Fault("GP", csel & 0xFFFC, "RPL < CPL")
        if d.conf and d.dpl > rpl:
            raise Fault("GP", csel & 0xFFFC)
        if not d.conf and d.dpl != rpl:
            raise Fault("GP", csel & 0xFFFC)
        if not d.p:
            raise Fault("NP", csel & 0xFFFC)
        if not d.L:
            raise ModelError("returns to compatibility mode are not modelled")
        return d

    def _ret_ss_checks(self, ss_sel, d, rpl):
        if not ss_sel & 0xFFFC:
            if not d.L or (ss_sel & 3) == 3:
                raise Fault("GP", ss_sel & 0xFFFC, "null SS")
            return Seg(ss_sel, dpl=rpl, code=False)
        sd = self.read_desc(ss_sel)
        if (ss_sel & 3) != rpl or not sd.wdata or sd.dpl != rpl:
            raise Fault("GP", ss_sel & 0xFFFC, "bad return SS")
        if not sd.p:
            raise Fault("SS", ss_sel & 0xFFFC)
        return seg_from(sd, ss_sel)

    def _pop_ss_frame(self):
        """shadow-stack CS / LIP / SSP of a far return: SSP must be 8-aligned (#CP)."""
        if self.ssp & 7:
            raise Fault("CP", 4, "SSP not 8-byte aligned at the far return")
        sscs = self.ss_load8(self.ssp + 16)
        sslip = self.ss_load8(self.ssp + 8)
        tssp = self.ss_load8(self.ssp)
        self.ssp = (self.ssp + 24) & M64
        return sscs, sslip, tssp

    def _check_ss_frame(self, sscs, sslip, tssp, rip):
        if self.cs.sel != sscs:
            raise Fault("CP", 4, "shadow-stack CS mismatch")
        if (self.cs.base + rip) & M64 != sslip:
            raise Fault("CP", 4, "shadow-stack LIP mismatch")
        if tssp & 3:
            raise Fault("CP", 4, "popped SSP not 4-byte aligned")

    def _check_new_ssp(self, tssp):
        if (not self.cs.L and tssp >> 32) or (self.cs.L and not canonical(tssp)):
            raise Fault("GP", 0, "new SSP not canonical / above 4G")

    # ---- far RET (Vol2B RET, 64-bit operand size, IA-32e mode) ----
    def far_ret(self):
        rsp = self.g[RSP]
        rip = self.rd(rsp, 8)
        csel = self.rd(rsp + 8, 8) & 0xFFFF
        d = self._ret_cs_checks(csel)
        rpl = csel & 3
        if rpl == self.cpl:
            if not canonical(rip):
                raise Fault("GP", 0, "return RIP non-canonical")
            self.g[RSP] = (rsp + 16) & M64
            self.cs = seg_from(d, csel)
            if self.ss_enabled(self.cpl):
                sscs, sslip, tssp = self._pop_ss_frame()
                self._check_ss_frame(sscs, sslip, tssp, rip)
                self._check_new_ssp(tssp)
                self.ssp = tssp
            return rip
        # IA-32E-MODE-RETURN-TO-OUTER-PRIVILEGE-LEVEL
        trsp = self.rd(rsp + 16, 8)
        ss_sel = self.rd(rsp + 24, 8) & 0xFFFF
        nss = self._ret_ss_checks(ss_sel, d, rpl)
        if not canonical(rip):
            raise Fault("GP", 0, "return RIP non-canonical")
        self.cs = seg_from(d, csel)
        tssp = None
        if self.ss_enabled(self.cpl):
            if self.ssp & 7:
                raise Fault("CP", 4, "SSP not 8-byte aligned at the far return")
            if rpl != 3:
                sscs, sslip, tssp = self._pop_ss_frame()
                self._check_ss_frame(sscs, sslip, tssp, rip)
        old_cpl = self.cpl
        self.cpl = rpl
        self.g[RSP] = trsp
        self.ss = nss
        old_ssp = self.ssp
        if self.ss_enabled(self.cpl):
            if self.cpl == 3:
                tssp = self.msr[PL3_SSP]
            self._check_new_ssp(tssp)
            self.ssp = tssp
        if self.ss_enabled(old_cpl):
            self.ss_cmpxchg8(old_ssp, old_ssp, old_ssp | 1)        # free the supervisor token
        return rip

    # ---- IRETQ (Vol2A IRET, IA-32e mode, 64-bit operand size) ----
    def _iret_flags(self, tfl):
        mask = 0x4DD5 | 0x250000                  # CF PF AF ZF SF TF DF OF NT, RF AC ID
        iopl = (self.rflags >> 12) & 3
        if self.cpl <= iopl:
            mask |= 0x200                         # IF
        if self.cpl == 0:
            mask |= 0x3000 | 0x180000             # IOPL, VIF, VIP
        self.rflags = (self.rflags & ~mask) | (tfl & mask) | 2

    def iretq(self):
        if self.rflags & 0x4000:
            raise Fault("GP", 0, "IRET with NT = 1 in IA-32e mode")
        rsp = self.g[RSP]
        rip = self.rd(rsp, 8)
        csel = self.rd(rsp + 8, 8) & 0xFFFF
        tfl = self.rd(rsp + 16, 8)
        trsp = self.rd(rsp + 24, 8)
        ss_sel = self.rd(rsp + 32, 8) & 0xFFFF
        if (csel & 3) < self.cpl:
            raise Fault("GP", csel & 0xFFFC, "IRET CS.RPL < CPL")
        d = self._ret_cs_checks(csel)
        rpl = csel & 3
        nss = self._ret_ss_checks(ss_sel, d, rpl)
        if not canonical(rip):
            raise Fault("GP", 0, "IRET RIP non-canonical")
        self._iret_flags(tfl)
        self.cs = seg_from(d, csel)
        if rpl == self.cpl:
            # RETURN-TO-SAME-PRIVILEGE-LEVEL (64-bit mode always pops RSP / SS)
            self.g[RSP] = trsp
            self.ss = nss
            if self.ss_enabled(self.cpl):
                sscs, sslip, tssp = self._pop_ss_frame()
                self._check_ss_frame(sscs, sslip, tssp, rip)
                self._check_new_ssp(tssp)
                if tssp != self.ssp:              # stack switch: free the token at SSP (= old SSP + 24)
                    self.ss_cmpxchg8(self.ssp, self.ssp, self.ssp | 1)
                self.ssp = tssp
            return rip
        # RETURN-TO-OUTER-PRIVILEGE-LEVEL
        tssp = None
        if self.ss_enabled(self.cpl):
            if self.ssp & 7:
                raise Fault("CP", 4, "SSP not 8-byte aligned at IRET")
            if rpl != 3:
                sscs, sslip, tssp = self._pop_ss_frame()
                self._check_ss_frame(sscs, sslip, tssp, rip)
        old_cpl = self.cpl
        self.cpl = rpl
        self.g[RSP] = trsp
        self.ss = nss
        old_ssp = self.ssp
        if self.ss_enabled(self.cpl):
            if self.cpl == 3:
                tssp = self.msr[PL3_SSP]
            self._check_new_ssp(tssp)
            self.ssp = tssp
        if self.ss_enabled(old_cpl):
            self.ss_cmpxchg8(old_ssp, old_ssp, old_ssp | 1)
        return rip

    # ---- SYSRET / SYSEXIT (64-bit operand size) ----
    def sysret64(self):
        if not self.cs.L or not EFER & 0x400 or not EFER & 1:
            raise Fault("UD", 0, "SYSRET outside 64-bit mode / SCE = 0")
        if self.cpl:
            raise Fault("GP", 0, "SYSRET at CPL > 0")
        rcx = self.g[RCX]
        if not canonical(rcx):
            raise Fault("GP", 0, "SYSRET RCX non-canonical")
        self.rflags = (self.g[R11] & 0x3C7FD7) | 2
        base = (self.msr[STAR] >> 48) & 0xFFFF
        self.cs = Seg(((base + 16) | 3) & 0xFFFF, 0, 1, 0, 3)
        self.cpl = 3
        if self.ss_enabled(3):
            self.ssp = self.msr[PL3_SSP]                     # decision (h)
        self.ss = Seg(((base + 8) | 3) & 0xFFFF, dpl=3, code=False)
        return rcx

    def sysexit64(self):
        if not self.msr[SYSENTER_CS] & 0xFFFC or self.cpl:
            raise Fault("GP", 0, "SYSEXIT: IA32_SYSENTER_CS[15:2] = 0 or CPL > 0")
        rcx, rdx = self.g[RCX], self.g[RDX]
        if not canonical(rcx) or not canonical(rdx):
            raise Fault("GP", 0, "SYSEXIT RCX/RDX non-canonical")
        self.g[RSP] = rcx
        sel = (((self.msr[SYSENTER_CS] & 0xFFFF) + 32) | 3) & 0xFFFF
        self.cs = Seg(sel, 0, 1, 0, 3)
        self.cpl = 3
        if self.ss_enabled(3):
            self.ssp = self.msr[PL3_SSP]                     # decision (h)
        self.ss = Seg(sel + 8, dpl=3, code=False)
        return rdx

    # ---- XSAVES / XRSTORS (Vol1 13.11 / 13.12, decision e / f) ----
    def xinuse(self):
        x = 0
        if self.msr[U_CET] | self.msr[PL3_SSP]:
            x |= 1 << 11
        if self.msr[PL0_SSP] | self.msr[PL1_SSP] | self.msr[PL2_SSP]:
            x |= 1 << 12
        return x

    @staticmethod
    def _cet_only(rfbm):
        if rfbm & ~0x1800:
            raise ModelError("only the CET components are modelled in XSAVES/XRSTORS (RFBM %#x)" % rfbm)

    def xsaves(self, a, edx_eax):
        if self.cpl or a & 63:
            raise Fault("GP", 0, "XSAVES: CPL > 0 or area not 64-byte aligned")
        rfbm = edx_eax & (XCR0 | self.msr[XSS])
        self._cet_only(rfbm)
        inuse = self.xinuse()
        self.wr(a + 512, 8, rfbm & inuse)
        self.wr(a + 520, 8, rfbm | (1 << 63))
        off = 576
        for i in range(2, 63):
            if rfbm >> i & 1:
                if inuse >> i & 1:
                    if i == 11:
                        self.wr(a + off, 8, self.msr[U_CET])
                        self.wr(a + off + 8, 8, self.msr[PL3_SSP])
                    elif i == 12:
                        for k in range(3):
                            self.wr(a + off + 8 * k, 8, self.msr[PL0_SSP + k])
                off += XCOMP_SIZE[i]

    def xrstors(self, a, edx_eax):
        if self.cpl or a & 63:
            raise Fault("GP", 0, "XRSTORS: CPL > 0 or area not 64-byte aligned")
        rfbm = edx_eax & (XCR0 | self.msr[XSS])
        self._cet_only(rfbm)
        xstate_bv = self.rd(a + 512, 8)
        xcomp_bv = self.rd(a + 520, 8)
        if not xcomp_bv >> 63:
            raise Fault("GP", 0, "XCOMP_BV[63] = 0")
        fmt = xcomp_bv & ~(1 << 63)
        if fmt & ~(XCR0 | self.msr[XSS]):
            raise Fault("GP", 0, "XCOMP_BV bit not in XCR0 | IA32_XSS")
        if xstate_bv & ~xcomp_bv:
            raise Fault("GP", 0, "XSTATE_BV bit not in XCOMP_BV")
        if self.rd(a + 528, 48):
            raise Fault("GP", 0, "header bytes 63:16 not zero")
        restore = fmt & rfbm & xstate_bv
        init = (rfbm & ~xstate_bv) | (rfbm & ~fmt)
        new, off = {}, 576
        for i in range(2, 63):
            if fmt >> i & 1:
                if restore >> i & 1:
                    if i == 11:
                        new[U_CET] = self.rd(a + off, 8)
                        new[PL3_SSP] = self.rd(a + off + 8, 8)
                    elif i == 12:
                        for k in range(3):
                            new[PL0_SSP + k] = self.rd(a + off + 8 * k, 8)
                    else:
                        raise ModelError("component %d" % i)
                off += self._size_or_fail(i)
        for idx, v in new.items():                       # decision (f): check everything first
            if not self.msr_ok(idx, v):
                raise Fault("GP", 0, "XRSTORS: MSR %#x value %#x refused" % (idx, v))
        if init >> 11 & 1:
            self.msr[U_CET] = self.msr[PL3_SSP] = 0
        if init >> 12 & 1:
            self.msr[PL0_SSP] = self.msr[PL1_SSP] = self.msr[PL2_SSP] = 0
        self.msr.update(new)

    @staticmethod
    def _size_or_fail(i):
        if i not in XCOMP_SIZE:
            raise ModelError("component %d size unknown" % i)
        return XCOMP_SIZE[i]


def xsave_compacted_size(bv):
    """CPUID.(0DH,1):EBX: legacy region + header + every enabled component i >= 2 (no alignment)."""
    return 576 + sum(XCOMP_SIZE[i] for i in range(2, 63) if bv >> i & 1)


def cpuid_d(sub, xss):
    """CPUID.(0DH,sub) for the parts this file checks: (EAX, EBX, ECX, EDX) or masks for sub 1."""
    if sub == 0xB:       # CET_U: 16 bytes, supervisor (ECX[0] = 1), no alignment, offset 0
        return 16, 0, 1, 0
    if sub == 0xC:       # CET_S: 24 bytes
        return 24, 0, 1, 0
    if sub == 1:         # EAX[3] XSAVES, EBX size of XCR0 | XSS, ECX[12:11] CET supervisor bits
        return 8, xsave_compacted_size(XCR0 | xss), XSS_SUPPORTED, None
    raise ModelError("CPUID 0DH sub-leaf %d" % sub)


# ---- snippet encoding + per-instruction semantics ---------------------------------------------
class Ins:
    def __init__(self, text, n, enc, sem=None, endbr=False):
        self.text, self.n, self.enc, self.sem, self.endbr = text, n, enc, sem, endbr
        self.addr = None


def ev(x, lab):
    if callable(x):
        return x(lab)
    if isinstance(x, str):
        return lab[x]
    return x


def i32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def mrm_mem(reg, base, disp):
    assert base in (RAX, RCX, RDX, RBX, RSI, RDI)
    if -128 <= disp < 128:
        return bytes([0x40 | (reg & 7) << 3 | base]) + struct.pack("<b", disp)
    return bytes([0x80 | (reg & 7) << 3 | base]) + struct.pack("<i", disp)


def fixed(text, b, sem=None, endbr=False):
    return Ins(text, len(b), lambda ins, lab: b, sem, endbr)


def unknown_flags(m):
    m.rflags = None


def MOV32(r, v):
    pre = b"\x41" if r >= 8 else b""

    def sem(m, ins):
        m.g[r] = ev(v, m.lab) & 0xFFFFFFFF
    return Ins("mov %s(32), %s" % (REGN[r], v), len(pre) + 5, lambda ins, lab: pre + bytes([0xB8 + (r & 7)]) + i32(ev(v, lab)), sem)


def MOV64(r, v):
    def sem(m, ins):
        m.g[r] = ev(v, m.lab) & M64
    return Ins("mov %s, %s" % (REGN[r], v), 10,
               lambda ins, lab: bytes([0x48 | (r >> 3), 0xB8 + (r & 7)]) + q(ev(v, lab)), sem)


def MOVCL(v):
    def sem(m, ins):
        m.g[RCX] = (m.g[RCX] & ~0xFF) | v
    return fixed("mov cl, %#x" % v, bytes([0xB1, v]), sem)


def WRMSR():
    def sem(m, ins):
        m.wrmsr(m.g[RCX] & 0xFFFFFFFF, ((m.g[RDX] & 0xFFFFFFFF) << 32) | (m.g[RAX] & 0xFFFFFFFF))
    return fixed("wrmsr", b"\x0f\x30", sem)


def RDMSR():
    def sem(m, ins):
        v = m.rdmsr(m.g[RCX] & 0xFFFFFFFF)
        m.g[RAX], m.g[RDX] = v & 0xFFFFFFFF, v >> 32
    return fixed("rdmsr", b"\x0f\x32", sem)


def SET_CR4_CET():
    """mov rax, cr4; bts rax, 23; mov cr4, rax  (RAX and the flags become unknown)."""
    def sem(m, ins):
        if not CR0 & (1 << 16):
            raise Fault("GP", 0, "CR4.CET needs CR0.WP")
        m.cr4 |= CR4_CET
        m.g[RAX] = None
        unknown_flags(m)
    return fixed("cr4.CET := 1", b"\x0f\x20\xe0\x48\x0f\xba\xe8\x17\x0f\x22\xe0", sem)


def PUSHI(v):
    def sem(m, ins):
        m.push8(v if v >= 0 else v & M64)
    if -128 <= v < 128:
        return fixed("push %#x" % v, bytes([0x6A, v & 0xFF]), sem)
    assert -(1 << 31) <= v < (1 << 31)
    return fixed("push %#x" % v, b"\x68" + i32(v), sem)


def PUSHL(label):
    """push imm32 = a code label (sign-extended; the snippet is below 2 GiB)."""
    def sem(m, ins):
        m.push8(ev(label, m.lab))
    return Ins("push %s" % label, 5, lambda ins, lab: b"\x68" + i32(ev(label, lab)), sem)


def POPFQ():
    def sem(m, ins):
        v = m.pop8()
        if m.cpl:
            raise ModelError("POPFQ modelled at CPL0 only")
        keep = 0x1A0000                                   # VM, VIF, VIP unchanged; RF cleared
        old = m.rflags if m.rflags is not None else 0
        m.rflags = (v & 0x3F7FD5 & ~0x10000 & ~keep) | (old & keep) | 2
    return fixed("popfq", b"\x9d", sem)


def FLAGS202():
    return [PUSHI(0x202), POPFQ()]


def CR4CET():
    """CR4.CET := 1, then RFLAGS back to 202H (push 202h; popfq: BTS leaves OF/SF/AF/PF undefined)."""
    return [SET_CR4_CET()] + FLAGS202()


def LGDT(base, disp):
    def sem(m, ins):
        a = m.g[base] + disp
        m.gdtr = (m.rd(a + 2, 8), m.rd(a, 2))
    return fixed("lgdt [%s%+#x]" % (REGN[base], disp), b"\x0f\x01" + mrm_mem(2, base, disp), sem)


def LTR_AX():
    def sem(m, ins):
        sel = m.g[RAX] & 0xFFFF
        d = m.read_desc(sel)
        if d.s or d.type != 9:
            raise Fault("GP", sel & 0xFFFC, "LTR: not an available 64-bit TSS")
        if not d.p:
            raise Fault("NP", sel & 0xFFFC)
        base = m.gdtr[0] + (sel & 0xFFF8)
        m.wr(base + 5, 1, m.rd(base + 5, 1) | 2)          # busy
        m.tr = (sel, d.base, d.limit)
    return fixed("ltr ax", b"\x0f\x00\xd8", sem)


def JMPF(base, disp):
    def sem(m, ins):
        a = m.g[base] + disp
        return m.far_jmp(m.rd(a, 8), m.rd(a + 8, 2))
    return fixed("jmp far m16:64 [%s%+#x]" % (REGN[base], disp), b"\x48\xff" + mrm_mem(5, base, disp), sem)


def CALLF(base, disp):
    def sem(m, ins):
        a = m.g[base] + disp
        return m.far_call(m.rd(a, 8), m.rd(a + 8, 2), ins.addr + ins.n)
    return fixed("call far m16:64 [%s%+#x]" % (REGN[base], disp), b"\x48\xff" + mrm_mem(3, base, disp), sem)


def RETF():
    return fixed("retf (REX.W)", b"\x48\xcb", lambda m, ins: m.far_ret())


def IRETQ():
    return fixed("iretq", b"\x48\xcf", lambda m, ins: m.iretq())


def SYSRET64():
    return fixed("sysretq", b"\x48\x0f\x07", lambda m, ins: m.sysret64())


def SYSEXIT64():
    return fixed("sysexitq", b"\x48\x0f\x35", lambda m, ins: m.sysexit64())


def SETSSBSY():
    return fixed("setssbsy", b"\xf3\x0f\x01\xe8", lambda m, ins: m.setssbsy())


def INCSSPD_EAX():
    return fixed("incsspd eax", b"\xf3\x0f\xae\xe8", lambda m, ins: m.incssp(m.g[RAX], False))


def INCSSPQ_RAX():
    return fixed("incsspq rax", b"\xf3\x48\x0f\xae\xe8", lambda m, ins: m.incssp(m.g[RAX], True))


def RDSSPQ(r):
    def sem(m, ins):
        v = m.rdssp()
        if v is not None:
            m.g[r] = v
    return fixed("rdsspq %s" % REGN[r], bytes([0xF3, 0x48 | (r >> 3), 0x0F, 0x1E, 0xC8 | (r & 7)]), sem)


def MOV_M64_R(base, disp, r):
    def sem(m, ins):
        m.wr(m.g[base] + disp, 8, m.g[r])
    return fixed("mov [%s%+#x], %s" % (REGN[base], disp, REGN[r]),
                 bytes([0x48 | ((r >> 3) << 2), 0x89]) + mrm_mem(r, base, disp), sem)


def MOV_M32_R(base, disp, r):
    assert r < 8

    def sem(m, ins):
        m.wr(m.g[base] + disp, 4, m.g[r])
    return fixed("mov [%s%+#x], %s(32)" % (REGN[base], disp, REGN[r]), b"\x89" + mrm_mem(r, base, disp), sem)


def MOV_R64_M(r, base, disp):
    def sem(m, ins):
        m.g[r] = m.rd(m.g[base] + disp, 8)
    return fixed("mov %s, [%s%+#x]" % (REGN[r], REGN[base], disp),
                 bytes([0x48 | ((r >> 3) << 2), 0x8B]) + mrm_mem(r, base, disp), sem)


def XSAVES(base, disp):
    def sem(m, ins):
        m.xsaves(m.g[base] + disp, ((m.g[RDX] & 0xFFFFFFFF) << 32) | (m.g[RAX] & 0xFFFFFFFF))
    return fixed("xsaves [%s%+#x]" % (REGN[base], disp), b"\x0f\xc7" + mrm_mem(5, base, disp), sem)


def XRSTORS(base, disp):
    def sem(m, ins):
        m.xrstors(m.g[base] + disp, ((m.g[RDX] & 0xFFFFFFFF) << 32) | (m.g[RAX] & 0xFFFFFFFF))
    return fixed("xrstors [%s%+#x]" % (REGN[base], disp), b"\x0f\xc7" + mrm_mem(3, base, disp), sem)


def CPUID_D(sub, keep):
    """mov eax, 0DH; mov ecx, sub; cpuid; then the parts not modelled are masked / zeroed:
    keep = 'all' (sub-leaves 0BH/0CH), 'bits' (sub-leaf 1: and eax, 8; and ecx, 1800H; ebx = edx = 0;
    flags reset by push 202H / popfq) or 'ebx' (sub-leaf 1: eax = ecx = edx = 0)."""
    b = b"\xb8" + i32(0xD) + b"\xb9" + i32(sub) + b"\x0f\xa2"
    if keep == "bits":
        b += b"\x25" + i32(8) + b"\x81\xe1" + i32(0x1800) + b"\xbb" + i32(0) + b"\xba" + i32(0)
        b += b"\x68" + i32(0x202) + b"\x9d"
    elif keep == "ebx":
        b += b"\xb8" + i32(0) + b"\xb9" + i32(0) + b"\xba" + i32(0)

    def sem(m, ins):
        a, bb, c, d = cpuid_d(sub, m.msr[XSS])
        if keep == "all":
            m.g[RAX], m.g[RBX], m.g[RCX], m.g[RDX] = a, bb, c, d
        elif keep == "bits":
            m.g[RAX], m.g[RBX], m.g[RCX], m.g[RDX] = a & 8, 0, c & 0x1800, 0
            m.rflags = 0x202
        else:
            m.g[RAX], m.g[RBX], m.g[RCX], m.g[RDX] = 0, bb, 0, 0
    return fixed("cpuid(0DH,%#x) %s" % (sub, keep), b, sem)


def LEA_RAX(label):
    def enc(ins, lab):
        return b"\x48\x8d\x05" + struct.pack("<i", lab[label] - (ins.addr + 7))

    def sem(m, ins):
        m.g[RAX] = m.lab[label]
    return Ins("lea rax, [rip -> %s]" % label, 7, enc, sem)


def JMP_RAX():
    def sem(m, ins):
        if m.endbr_enabled(m.cpl) and not m.msr[m.cet_msr(m.cpl)] & SUPPRESS:
            m.msr[m.cet_msr(m.cpl)] |= TRACKER
        return m.g[RAX]
    return fixed("jmp rax", b"\xff\xe0", sem)


def JMPS(label):
    def enc(ins, lab):
        rel = lab[label] - (ins.addr + 2)
        assert -128 <= rel < 128, rel
        return bytes([0xEB, rel & 0xFF])
    return Ins("jmp short %s" % label, 2, enc, lambda m, ins: m.lab[label])


def ENDBR64(rex2=None):
    b = b"\xf3\x0f\x1e\xfa" if rex2 is None else bytes([0xF3, 0xD5, rex2, 0x1E, 0xFA])

    def sem(m, ins):
        if m.endbr_enabled(m.cpl) and m.cs.L:
            i = m.cet_msr(m.cpl)
            m.msr[i] &= ~(TRACKER | SUPPRESS)
    return fixed("endbr64" + ("" if rex2 is None else " (REX2 %02X)" % rex2), b, sem, endbr=True)


def MOV_R11D(v):
    def sem(m, ins):
        m.g[R11] = v
    return fixed("mov r11d, %#x" % v, b"\x41\xbb" + i32(v), sem)


class Prog:
    def __init__(self):
        self.items = []
        self.lab = {}

    def __call__(self, *ins):
        for i in ins:
            if isinstance(i, list):
                self.items.extend(i)
            else:
                self.items.append(i)
        return self

    def label(self, name):
        self.items.append(name)
        return self

    def layout(self):
        a = SNIP
        for it in self.items:
            if isinstance(it, str):
                self.lab[it] = a
            else:
                it.addr = a
                a += it.n
        self.lab["END"] = a
        self.end = a
        code = b""
        for it in self.items:
            if not isinstance(it, str):
                b = it.enc(it, self.lab)
                assert len(b) == it.n, it.text
                code += b
        if len(code) > 255:
            raise ModelError("snippet is %d bytes (max 255)" % len(code))
        self.code = code
        self.at = {it.addr: it for it in self.items if not isinstance(it, str)}
        return self


def simulate(prog, regs, mem):
    """Runs the snippet in the model. Returns (machine, fault or None)."""
    m = Machine(regs, mem)
    m.lab = prog.lab
    rip, steps = SNIP, 0
    while rip != prog.end:
        steps += 1
        if steps > 1000 or rip not in prog.at:
            raise ModelError("control flow left the snippet at %#x" % rip)
        ins = prog.at[rip]
        s = m.snap()
        try:
            # IBT: WAIT_FOR_ENDBRANCH and the next instruction is not ENDBR64 -> #CP(ENDBRANCH)
            if m.endbr_enabled(m.cpl) and m.msr[m.cet_msr(m.cpl)] & TRACKER and not ins.endbr:
                raise Fault("CP", 3, "missing ENDBRANCH")
            nxt = ins.sem(m, ins) if ins.sem else None
            rip = ins.addr + ins.n if nxt is None else nxt
        except Fault as f:
            m.restore(s)
            return m, f, ins
        except TypeError:
            raise ModelError("unknown value used by %s" % ins.text)
    return m, None, None


# ---- cases -------------------------------------------------------------------------------------
GDT = RSI0                      # GDT at RSI+0
GDTR_D = 0x60                   # pseudo-descriptor at RSI+60H
JPTR_D = 0x70                   # far JMP pointer (m16:64) at RSI+70H
CPTR_D = -0x10                  # far CALL pointer at RSI-10H
GPTR_D = -0x20                  # call-gate pointer at RSI-20H
TSS = MEM + 0x7000
RSP0 = DATA + 0x3800            # TSS.RSP0 (test stack page, not compared)
RSP3 = DATA + 0x3E00            # CPL3 stack
T = MEM + 0x9FF8                # supervisor token (T & 1FH = 18H: token + 24-byte frame in 32 bytes)
T2 = MEM + 0x9FD8               # second token slot (also 18H mod 20H)
T3 = MEM + 0x9FB0               # 10H mod 20H: token + frame would cross a 32-byte boundary
U = MEM + 0xB000                # IA32_PL3_SSP (user shadow stack)
XAREA_D = 0x4000                # XSAVE area at RSI+4000H = MEM+C000H (64-byte aligned)
XAREA = RSI0 + XAREA_D
CS0, DS0, CS3, DS3, CSC, GATE, TSSSEL = 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x40


class Case:
    def __init__(self, comment, prog, regs=None, mem=None, loose=None):
        self.comment, self.prog, self.regs, self.mem, self.loose = comment, prog, regs or {}, mem or {}, loose


def gdt_mem(gate_off=None, gate_dpl=3):
    """GDT image (every descriptor already accessed) + GDTR pseudo-descriptor."""
    ents = [0, d_code64(0), d_data(0), d_code64(3), d_data(3), d_code64(0, conf=True)]
    glo, ghi = d_gate64(CS0, gate_off or 0, gate_dpl) if gate_off is not None else (0, 0)
    tlo, thi = d_tss64(TSS, 0x67)
    ents += [glo, ghi, tlo, thi]
    img = b"".join(q(e) for e in ents)
    return {GDT: img, RSI0 + GDTR_D: struct.pack("<H", len(img) - 1) + q(GDT)}


def farptr(off, sel):
    return q(off) + struct.pack("<H", sel)


def mk(comment, build, regs=None, mem=None, loose=None):
    """build(p) adds the instructions; mem may be a callable(lab) -> {addr: bytes}."""
    p = Prog()
    build(p)
    p.layout()
    mm = mem(p.lab) if callable(mem) else (mem or {})
    return Case(comment, p, regs, mm, loose)


def merge(*ds):
    out = {}
    for d in ds:
        out.update(d)
    return out


def cpl0_cet(p, s_cet=SH_STK_EN, pl0=T, setssbsy=True):
    """CR4.CET, IA32_S_CET, IA32_PL0_SSP, SETSSBSY, flags back to 202H (RCX = 6A2H, RDX = 0 inputs)."""
    p(CR4CET(), MOV32(RAX, s_cet), WRMSR())
    if pl0 is not None:
        p(MOVCL(0xA4), MOV32(RAX, pl0), WRMSR())
    if setssbsy:
        p(SETSSBSY())


def gdt_pro(p):
    p(LGDT(RSI, GDTR_D), JMPF(RSI, JPTR_D)).label("cs08")


def gdt_pro_mem(lab, gate_off=None, gate_dpl=3):
    return merge(gdt_mem(gate_off, gate_dpl), {RSI0 + JPTR_D: farptr(lab["cs08"], CS0)})


REGS_S = {"rcx": S_CET}


def cases_call_ret():
    cs = []
    tok = {T: q(T)}

    # 1. far CALL m16:64 to 64-bit non-conforming code at CPL0, SSP 8-aligned / SSP = 4 mod 8
    def b1(misalign):
        def b(p):
            gdt_pro(p)
            cpl0_cet(p)
            if misalign:
                p(MOV32(RAX, 1), INCSSPD_EAX())
            p(CALLF(RSI, CPTR_D)).label("ret1")
            p(RDSSPQ(RBP), JMPS("END")).label("callee")
            p(RDSSPQ(RBX), RETF())
        return b

    def m1(lab, extra=None):
        return merge(gdt_pro_mem(lab), tok, {RSI0 + CPTR_D: farptr(lab["callee"], CS0)}, extra or {})

    cs.append(mk("far CALL 08h:callee (CPL0, SSP = token T, 8-aligned): shadow frame CS / LIP / old SSP at "
                 "T-8 / T-16 / T-24, callee RDSSPQ rbx = T-24; RETF back: SSP = T (rbp)",
                 b1(False), REGS_S, m1))
    cs.append(mk("far CALL with SSP = T+4: 4-byte zero store at SSP-4 = T (token low dword cleared), SSP aligned "
                 "down to T, frame at T-24 holds the old SSP T+4; RETF restores SSP = T+4",
                 b1(True), REGS_S, m1))

    # 2. RETF variants: the callee changes the shadow frame (no paging: a plain store) then RETF
    def b2(pre):
        def b(p):
            gdt_pro(p)
            cpl0_cet(p)
            p(CALLF(RSI, CPTR_D)).label("ret1")
            p(RDSSPQ(RBP), JMPS("END")).label("callee")
            p(RDSSPQ(RBX))
            pre(p)
            p(RETF())
        return b

    cs.append(mk("RETF: SSP misaligned (INCSSPD 1 in the callee) -> #CP(FAR-RET/IRET); RSP/SSP unchanged",
                 b2(lambda p: p(MOV32(RAX, 1), INCSSPD_EAX())), REGS_S, m1))
    cs.append(mk("RETF: shadow-stack CS = 10008h (bit 16 set, 64-bit compare) -> #CP",
                 b2(lambda p: p(MOV32(RAX, 0x10008), MOV_M64_R(RBX, 16, RAX))), REGS_S, m1))
    cs.append(mk("RETF: shadow-stack LIP = return address + 1 -> #CP",
                 b2(lambda p: p(MOV32(RAX, lambda lab: lab["ret1"] + 1), MOV_M64_R(RBX, 8, RAX))), REGS_S, m1))
    cs.append(mk("RETF: popped SSP = T+2 (not 4-aligned) -> #CP",
                 b2(lambda p: p(MOV32(RAX, T + 2), MOV_M64_R(RBX, 0, RAX))), REGS_S, m1))
    cs.append(mk("RETF: popped SSP = 0100000000000000h (non-canonical) -> #GP(0)",
                 b2(lambda p: p(MOV64(RAX, 0x0100000000000000), MOV_M64_R(RBX, 0, RAX))), REGS_S, m1))
    cs.append(mk("RETF: popped SSP = 0000800000000000h (canonical for 57 bits, not for the current 4-level "
                 "paging mode, CR4.LA57 = 0) -> #GP(0)",
                 b2(lambda p: p(MOV64(RAX, 0x0000800000000000), MOV_M64_R(RBX, 0, RAX))), REGS_S, m1))
    cs.append(mk("RETF: popped SSP replaced by T-100h+4 (4-aligned, canonical): SSP := popped value (rbp)",
                 b2(lambda p: p(MOV32(RAX, T - 0x100 + 4), MOV_M64_R(RBX, 0, RAX))), REGS_S, m1))

    # conforming target (decision c) and a same-privilege 64-bit call gate
    def bconf(p):
        gdt_pro(p)
        cpl0_cet(p)
        p(CALLF(RSI, CPTR_D)).label("ret1")
        p(RDSSPQ(RBP), JMPS("END")).label("callee")
        p(RDSSPQ(RBX), RETF())
    cs.append(mk("far CALL to a conforming DPL0 64-bit segment (28h): LIP = RIP, shadow CS = 08h; RETF back",
                 bconf, REGS_S,
                 lambda lab: merge(gdt_pro_mem(lab), tok, {RSI0 + CPTR_D: farptr(lab["callee"], CSC)})))
    cs.append(mk("far CALL through a 64-bit call gate (30h, DPL3) to 08h at CPL0 = SAME-PRIVILEGE: shadow "
                 "frame CS 08h / LIP old RIP / old SSP; RETF back",
                 bconf, REGS_S,
                 lambda lab: merge(gdt_pro_mem(lab, gate_off=lab["callee"]), tok,
                                   {RSI0 + CPTR_D: farptr(0x1234, GATE)})))
    return cs


def gate_setup(p, u_cet=SH_STK_EN, pl0_after=None, pl3=U):
    """GDT, LTR 40h, CR4.CET, IA32_U_CET, IA32_S_CET = SH_STK_EN, PL0_SSP = T, PL3_SSP, SETSSBSY,
    optionally PL0_SSP := pl0_after, then IRETQ to CPL3 (CS 1Bh, SS 23h, RSP3, RFLAGS 202h).
    Inputs: rcx = 6A0h, rdx = 0."""
    p(LGDT(RSI, GDTR_D), MOV32(RAX, TSSSEL), LTR_AX(), CR4CET())
    p(MOV32(RAX, u_cet), WRMSR())
    p(MOVCL(0xA2), MOV32(RAX, SH_STK_EN), WRMSR())
    p(MOVCL(0xA4), MOV32(RAX, T), WRMSR())
    p(MOVCL(0xA7), MOV32(RAX, pl3), WRMSR())
    p(SETSSBSY())
    if pl0_after is not None:
        p(MOVCL(0xA4), MOV32(RAX, pl0_after), WRMSR())
    p(PUSHI(DS3 | 3), PUSHI(RSP3), PUSHI(0x202), PUSHI(CS3 | 3), PUSHL("cpl3"), IRETQ()).label("cpl3")


def gate_mem(lab, extra=None):
    return merge(gdt_mem(gate_off=lab["handler"]), {T: q(T), TSS + 4: q(RSP0),
                                                    RSI0 + GPTR_D: farptr(0x5678, GATE | 3)}, extra or {})


REGS_U = {"rcx": U_CET}


def cases_gate():
    cs = []

    def bg(u_cet=SH_STK_EN, incssp=True, pl0_after=None, handler=None):
        def b(p):
            gate_setup(p, u_cet, pl0_after)
            if incssp:
                p(MOV32(RAX, 1), INCSSPQ_RAX())
            p(CALLF(RSI, GPTR_D)).label("back3")
            p(RDSSPQ(RBP), JMPS("END")).label("handler")
            if handler:
                handler(p)
            else:
                p(RDSSPQ(RBX), MOV_R64_M(R8, RBX, 0), RDMSR(), RETF())
        return b

    cs.append(mk("call gate CPL3 -> CPL0 (IA32_U_CET.SH_STK_EN = 1): SSP(3) = U+8 saved in IA32_PL3_SSP "
                 "(handler RDMSR), token at IA32_PL0_SSP = T acquired (r8 = T|1), SSP = T (rbx), no frame "
                 "pushed; RETF to CPL3: no shadow pop, SSP := IA32_PL3_SSP (rbp), token freed",
                 bg(), REGS_U, gate_mem))
    cs.append(mk("call gate CPL3 -> CPL0 with IA32_U_CET.SH_STK_EN = 0: IA32_PL3_SSP not written (RDMSR = U); "
                 "token acquired and freed; RDSSPQ at CPL3 is a NOP (rbp unchanged)",
                 bg(u_cet=0, incssp=False), dict(REGS_U, rbp=0x5555), gate_mem))
    cs.append(mk("call gate: IA32_PL0_SSP = T2+4 (not 8-aligned) -> #GP(0) at the CALL (CPL3)",
                 bg(incssp=False, pl0_after=T2 + 4), REGS_U, gate_mem))
    cs.append(mk("call gate: token at T2 busy (T2|1) -> #GP(0), token written back unchanged",
                 bg(incssp=False, pl0_after=T2), REGS_U, lambda lab: gate_mem(lab, {T2: q(T2 | 1)})))
    cs.append(mk("call gate: token at T2 holds T2+20h (address mismatch) -> #GP(0)",
                 bg(incssp=False, pl0_after=T2), REGS_U, lambda lab: gate_mem(lab, {T2: q(T2 + 0x20)})))
    cs.append(mk("call gate: token at T2 has reserved bit 2 set -> #GP(0)",
                 bg(incssp=False, pl0_after=T2), REGS_U, lambda lab: gate_mem(lab, {T2: q(T2 | 4)})))
    cs.append(mk("call gate: IA32_PL0_SSP = T3 (10h mod 20h): token + 24-byte frame cross a 32-byte "
                 "boundary -> #GP(0) even from CPL3 (no frame pushed); free token at T3 untouched",
                 bg(incssp=False, pl0_after=T3), REGS_U, lambda lab: gate_mem(lab, {T3: q(T3)})))
    cs.append(mk("RETF CPL0 -> CPL3 with SSP misaligned (INCSSPD 1 in the handler) -> #CP; token stays busy",
                 bg(incssp=False, handler=lambda p: p(MOV32(RAX, 1), INCSSPD_EAX(), RETF())), REGS_U, gate_mem))
    return cs


def cases_iret():
    cs = []
    tok = {T: q(T)}

    def bi(pre=None, frame_rip="ret1", frame_cs=CS0, flags=0x287):
        def b(p):
            gdt_pro(p)
            cpl0_cet(p)
            p(CALLF(RSI, CPTR_D)).label("ret1")
            p(RDSSPQ(RBP), JMPS("END")).label("callee")
            p(RDSSPQ(RBX))
            if pre:
                pre(p)
            p(PUSHI(0), PUSHI(STACK_TOP), PUSHI(flags), PUSHI(frame_cs))
            p(PUSHL(frame_rip))
            p(IRETQ())
        return b

    def mi(lab):
        return merge(gdt_pro_mem(lab), tok, {RSI0 + CPTR_D: farptr(lab["callee"], CS0)})

    cs.append(mk("IRETQ CPL0 -> CPL0 (frame of a far CALL, SS = 0, RFLAGS 287h): shadow frame popped and "
                 "checked, popped SSP = SSP+24 -> no token release (T stays busy); SSP = T",
                 bi(), REGS_S, mi))
    cs.append(mk("IRETQ CPL0 -> CPL0, popped SSP changed to T-200h+4: the busy token at SSP+24 = T is freed, "
                 "SSP = T-200h+4",
                 bi(lambda p: p(MOV32(RAX, T - 0x200 + 4), MOV_M64_R(RBX, 0, RAX))), REGS_S, mi))
    cs.append(mk("IRETQ CPL0 -> CPL0 with SSP misaligned -> #CP; RFLAGS / RSP unchanged (frame RFLAGS 287h)",
                 bi(lambda p: p(MOV32(RAX, 1), INCSSPD_EAX())), REGS_S, mi))
    cs.append(mk("IRETQ CPL0 -> CPL0 to CS 28h (conforming DPL0) while the shadow CS is 08h -> #CP",
                 bi(frame_cs=CSC), REGS_S, mi))
    cs.append(mk("IRETQ CPL0 -> CPL0 with RIP = return address + 1 (LIP mismatch) -> #CP",
                 bi(frame_rip=lambda lab: lab["ret1"] + 1), REGS_S, mi))
    cs.append(mk("IRETQ CPL0 -> CPL0 with popped SSP = T+2 -> #CP",
                 bi(lambda p: p(MOV32(RAX, T + 2), MOV_M64_R(RBX, 0, RAX))), REGS_S, mi))
    return cs


def cases_iret_outer():
    cs = []

    def bo(u_cet=SH_STK_EN, pre=None, pl3=U):
        def b(p):
            p(LGDT(RSI, GDTR_D), CR4CET())
            p(MOV32(RAX, u_cet), WRMSR())
            p(MOVCL(0xA2), MOV32(RAX, SH_STK_EN), WRMSR())
            p(MOVCL(0xA4), MOV32(RAX, T), WRMSR())
            if pl3 >> 32:
                p(MOVCL(0xA7), MOV32(RAX, pl3 & 0xFFFFFFFF), MOV32(RDX, pl3 >> 32), WRMSR(), MOV32(RDX, 0))
            else:
                p(MOVCL(0xA7), MOV32(RAX, pl3), WRMSR())
            p(SETSSBSY())
            if pre:
                pre(p)
            p(PUSHI(DS3 | 3), PUSHI(RSP3), PUSHI(0x202), PUSHI(CS3 | 3), PUSHL("cpl3"), IRETQ()).label("cpl3")
            p(RDSSPQ(RBP))
        return b

    def mo(lab):
        return merge(gdt_mem(), {T: q(T)})

    cs.append(mk("IRETQ CPL0 -> CPL3 (U_CET.SH_STK_EN = 1): no shadow pop, busy token at SSP = T freed, "
                 "SSP := IA32_PL3_SSP = U (RDSSPQ at CPL3)", bo(), REGS_U, mo))
    cs.append(mk("IRETQ CPL0 -> CPL3 with U_CET.SH_STK_EN = 0: token freed, RDSSPQ at CPL3 is a NOP",
                 bo(u_cet=0), dict(REGS_U, rbp=0x5555), mo))
    cs.append(mk("IRETQ CPL0 -> CPL3 with SSP = T+4 (INCSSPD 1) -> #CP; token stays busy",
                 bo(pre=lambda p: p(MOV32(RAX, 1), INCSSPD_EAX())), REGS_U, mo))
    cs.append(mk("IRETQ CPL0 -> CPL3 with SSP = T+8 (INCSSPQ 1): [T+8] = 0 is not a busy token for T+8 -> "
                 "release skipped silently, T stays busy; SSP := U",
                 bo(pre=lambda p: p(MOV32(RAX, 1), INCSSPQ_RAX())), REGS_U, mo))
    cs.append(mk("IRETQ CPL0 -> CPL3 with IA32_PL3_SSP = 0000800000000000h (accepted by WRMSR: CPU canonical) "
                 "-> #GP(0): the new SSP must be canonical relative to the current paging mode (48 bits); "
                 "the token is released only after the last check, so T stays busy",
                 bo(pl3=0x0000800000000000), REGS_U, mo))
    return cs


def cases_sys():
    cs = []
    P3 = 0x00007FFF12345674          # bits 1:0 = 0, bit 2 set, bits 63:32 non-zero

    def bs(u_cet, sysexit=False, token=False, pl3=P3):
        def b(p):
            p(CR4CET())
            p(MOV32(RAX, u_cet), WRMSR())
            p(MOVCL(0xA7), MOV32(RAX, pl3 & 0xFFFFFFFF), MOV32(RDX, pl3 >> 32), WRMSR())
            if token:
                p(MOVCL(0xA2), MOV32(RAX, SH_STK_EN), MOV32(RDX, 0), WRMSR())
                p(MOVCL(0xA4), MOV32(RAX, T), WRMSR(), SETSSBSY())
            if sysexit:
                p(MOV32(RCX, SYSENTER_CS), MOV32(RAX, 0x10), MOV32(RDX, 0), WRMSR())
                p(MOV32(RCX, STACK_TOP), MOV32(RDX, "user"), SYSEXIT64())
            else:
                p(MOV32(RCX, STAR), MOV32(RAX, 0), MOV32(RDX, 0x00100000), WRMSR())
                p(MOV32(RCX, "user"), MOV_R11D(0x202), SYSRET64())
            p.label("user")
            p(RDSSPQ(RBP))
        return b

    cs.append(mk("SYSRET (REX.W) with IA32_U_CET.SH_STK_EN = 1: SSP := IA32_PL3_SSP = 00007FFF12345674h "
                 "(full 64-bit value, not 8-aligned), CS = 23h", bs(SH_STK_EN), REGS_U))
    cs.append(mk("SYSRET with IA32_U_CET.SH_STK_EN = 0: SSP not loaded, RDSSPQ at CPL3 is a NOP",
                 bs(0), dict(REGS_U, rbp=0x5555)))
    cs.append(mk("SYSRET from CPL0 with an active supervisor shadow stack (SETSSBSY on T): SYSRET does not "
                 "free the token (T stays busy); SSP := IA32_PL3_SSP", bs(SH_STK_EN, token=True), REGS_U,
                 {T: q(T)}))
    cs.append(mk("SYSEXIT (REX.W, IA32_SYSENTER_CS = 10h -> CS 33h) with IA32_U_CET.SH_STK_EN = 1: "
                 "SSP := IA32_PL3_SSP, RSP := RCX", bs(SH_STK_EN, sysexit=True), REGS_U))
    cs.append(mk("SYSEXIT with IA32_U_CET.SH_STK_EN = 0: RDSSPQ at CPL3 is a NOP",
                 bs(0, sysexit=True), dict(REGS_U, rbp=0x5555)))
    cs.append(mk("SYSRET with IA32_PL3_SSP = 0000800000000000h (CPU canonical, not paging canonical): SYSRET "
                 "checks nothing, SSP := 0000800000000000h (RDSSPQ at CPL3)",
                 bs(SH_STK_EN, pl3=0x0000800000000000), REGS_U))
    return cs


def cases_wrmsr():
    """WRMSR canonicality of the CET MSRs (Vol3A 4.5.3: CPU canonical, 57 bits with LA57)."""
    cs = []
    GAP, BAD = 0x0000800000000000, 0x0100000000000000
    names = {U_CET: "IA32_U_CET", S_CET: "IA32_S_CET", PL3_SSP: "IA32_PL3_SSP"}
    for idx in (U_CET, S_CET, PL3_SSP):
        cs.append(mk("WRMSR %s = 0000800000000000h (57-bit canonical, not 48-bit) -> no fault, RDMSR reads it "
                     "back" % names[idx],
                     lambda p: p(WRMSR(), MOV32(RDX, 0), RDMSR()), {"rcx": idx, "rdx": GAP >> 32}))
        cs.append(mk("WRMSR %s = 0100000000000000h (not 57-bit canonical) -> #GP(0) (registers unchanged; the "
                     "MSR itself cannot be read after the fault in this harness)" % names[idx],
                     lambda p: p(WRMSR(), MOV32(RDX, 0), RDMSR()), {"rcx": idx, "rdx": BAD >> 32}))
    return cs


VALS = {U_CET: 0x00007FFFFFFFF03F, PL3_SSP: 0x00007FFFFFFFFFF8, PL0_SSP: 0x30021000,
        PL1_SSP: 0x0000123456789AB4, PL2_SSP: 0xFFFF800000001000}
PAT = 0xA5


def wr_msrs(p, vals, ecx_now):
    """WRMSR each (idx, value); ecx_now = the ECX value before (input), returns nothing."""
    cur, edx = ecx_now, 0
    for idx, v in vals:
        if (cur >> 8) == (idx >> 8):
            p(MOVCL(idx & 0xFF))
        else:
            p(MOV32(RCX, idx))
        cur = idx
        p(MOV32(RAX, v & 0xFFFFFFFF))
        if v >> 32 != edx:
            p(MOV32(RDX, v >> 32))
            edx = v >> 32
        p(WRMSR())
    if edx:
        p(MOV32(RDX, 0))


def area_pattern():
    """XSAVE area prefill: legacy bytes 0-15 and 496-511, header 512-575, components 576-639."""
    return {XAREA: bytes([PAT]) * 16, XAREA + 496: bytes([PAT]) * 16, XAREA + 512: bytes([PAT]) * 128}


def cases_xsave():
    cs = []
    REGS_X = {"rcx": XSS, "rax": 0x1800}

    def bx(vals, mask=0x1800, xss=0x1800):
        def b(p):
            p(MOV32(RAX, xss), WRMSR())
            wr_msrs(p, vals, XSS)
            p(MOV32(RAX, mask), XSAVES(RSI, XAREA_D))
        return b
    allv = sorted(VALS.items())
    cs.append(mk("XSAVES RFBM = 1800h, all CET MSRs non-zero: XSTATE_BV = 1800h, XCOMP_BV = 8000000000001800h, "
                 "CET_U (U_CET, PL3_SSP) at 576, CET_S (PL0-2_SSP) at 592; legacy region and header bytes "
                 "63:16 untouched", bx(allv), REGS_X, area_pattern()))
    cs.append(mk("XSAVES with only CET_S in use (U_CET = PL3_SSP = 0): XSTATE_BV = 1000h, CET_U slot not "
                 "written, CET_S still at 592 (XCOMP_BV = RFBM)",
                 bx([(k, v) for k, v in allv if k in (PL0_SSP, PL1_SSP, PL2_SSP)]), REGS_X, area_pattern()))
    cs.append(mk("XSAVES EDX:EAX = 1000h: RFBM = 1000h, XCOMP_BV = 8000000000001000h, CET_S at 576",
                 bx(allv, mask=0x1000), REGS_X, area_pattern()))
    cs.append(mk("XSAVES with every CET MSR 0: XSTATE_BV = 0, XCOMP_BV = 8000000000001800h, nothing else "
                 "written", bx([]), REGS_X, area_pattern()))
    cs.append(mk("XSAVES with IA32_XSS = 800h, EDX:EAX = 1800h: RFBM = 800h, CET_U only at 576",
                 bx(allv, xss=0x800), {"rcx": XSS, "rax": 0x800}, area_pattern()))
    return cs


def xr_area(xstate_bv, xcomp_bv, comps, rest=None):
    img = q(xstate_bv) + q(xcomp_bv) + (rest or bytes(48))
    data = b"".join(q(v) for v in comps)
    return {XAREA + 512: img + data}


def cases_xrstors():
    cs = []
    REGS_X = {"rcx": XSS, "rax": 0x1800}
    order = [U_CET, PL3_SSP, PL0_SSP, PL1_SSP, PL2_SSP]

    def bx(pre_vals=(), mask=0x1800, readback=True):
        def b(p):
            p(MOV32(RAX, 0x1800), WRMSR())
            wr_msrs(p, list(pre_vals), XSS)
            p(MOV32(RAX, mask), XRSTORS(RSI, XAREA_D))
            if readback:
                cur = XSS
                for k, idx in enumerate(order):
                    p(MOVCL(idx & 0xFF) if (cur >> 8) == (idx >> 8) else MOV32(RCX, idx))
                    cur = idx
                    p(RDMSR(), MOV_M32_R(RDI, 8 * k, RAX), MOV_M32_R(RDI, 8 * k + 4, RDX))
        return b

    store_pat = {RDI0: bytes([0xEE]) * 40}
    new = [0x0000123456781015, 0x00007FFF00001230, 0x30022000, 0x30023004, 0xFFFF800012345678]
    pre = [(PL0_SSP, 0x11110000), (PL1_SSP, 0x22220000), (PL2_SSP, 0x33330000), (U_CET, 1), (PL3_SSP, 0x44440000)]
    X18 = (1 << 63) | 0x1800
    cs.append(mk("XRSTORS XSTATE_BV = XCOMP_BV[62:0] = 1800h: U_CET, PL3_SSP, PL0-2_SSP loaded (RDMSR to [rdi])",
                 bx(), REGS_X, merge(store_pat, xr_area(0x1800, X18, new))))
    cs.append(mk("XRSTORS XSTATE_BV = 0 (RFBM 1800h): both components initialised, all five MSRs 0",
                 bx(pre), REGS_X, merge(store_pat, xr_area(0, X18, new))))
    cs.append(mk("XRSTORS XCOMP_BV = 8000000000000800h, XSTATE_BV = 800h, RFBM 1800h: CET_U loaded from 576, "
                 "CET_S force-initialised (not in XCOMP_BV)",
                 bx(pre), REGS_X, merge(store_pat, xr_area(0x800, (1 << 63) | 0x800, new[:2]))))
    cs.append(mk("XRSTORS EDX:EAX = 800h with XSTATE_BV = 1800h: CET_U loaded, CET_S unchanged",
                 bx(pre, mask=0x800), REGS_X, merge(store_pat, xr_area(0x1800, X18, new))))
    cs.append(mk("XRSTORS XSTATE_BV = 1000h with an invalid CET_U image (U_CET bits 9:6): CET_U is "
                 "initialised, not loaded -> no #GP; CET_S loaded",
                 bx(pre), REGS_X, merge(store_pat, xr_area(0x1000, X18, [0x3C0, 3] + new[2:]))))

    def gp(comment, xstate_bv, xcomp_bv, comps, rest=None):
        cs.append(mk("XRSTORS #GP: " + comment, bx(readback=False), REGS_X,
                     xr_area(xstate_bv, xcomp_bv, comps, rest)))
    gp("XCOMP_BV[63] = 0", 0x1800, 0x1800, new)
    gp("XSTATE_BV[12] set, XCOMP_BV[12] clear", 0x1800, (1 << 63) | 0x800, new)
    gp("XCOMP_BV[13] set (not in XCR0 | IA32_XSS)", 0x1800, (1 << 63) | 0x3800, new)
    gp("header byte 16 non-zero", 0x1800, X18, new, b"\x01" + bytes(47))
    gp("IA32_U_CET bits 9:6 (40h)", 0x1800, X18, [0x40] + new[1:])
    gp("IA32_U_CET SUPPRESS and TRACKER both 1", 0x1800, X18, [0xC04] + new[1:])
    gp("IA32_U_CET EB_LEG_BITMAP_BASE non-canonical (0100000000000000h)", 0x1800, X18,
       [0x0100000000000004] + new[1:])
    gp("IA32_PL3_SSP bits 1:0 = 2", 0x1800, X18, [new[0], 0x00007FFF00001232] + new[2:])
    cs.append(mk("XRSTORS IA32_PL3_SSP = 0000800000000000h, IA32_U_CET.EB_LEG_BITMAP_BASE = 0000800000000000h "
                 "(57-bit canonical, not 48-bit): CPU canonical (Vol3A 4.5.3) -> loaded, no #GP",
                 bx(), REGS_X, merge(store_pat, xr_area(0x1800, X18, [0x0000800000000004, 0x0000800000000000]
                                                        + new[2:]))))
    gp("IA32_PL3_SSP = 0100000000000000h (not 57-bit canonical: bit 56 set, bits 63:57 clear)", 0x1800, X18,
       [new[0], 0x0100000000000000] + new[2:])
    gp("IA32_PL0_SSP non-canonical (0100000000000000h)", 0x1800, X18, new[:2] + [0x0100000000000000] + new[3:])
    gp("IA32_PL2_SSP bit 0 set", 0x1800, X18, new[:4] + [0xFFFF800012345679])
    return cs


def cases_cpuid():
    cs = []
    cs.append(mk("CPUID.(0DH,1): EAX[3] XSAVES = 1, ECX[12:11] = 11b (CET_U, CET_S supervisor states)",
                 lambda p: p(CPUID_D(1, "bits"))))
    cs.append(mk("CPUID.(0DH,0BH) CET_U: EAX 16, EBX 0 (supervisor), ECX 1 (IA32_XSS, no alignment), EDX 0",
                 lambda p: p(CPUID_D(0xB, "all"))))
    cs.append(mk("CPUID.(0DH,0CH) CET_S: EAX 24, EBX 0, ECX 1, EDX 0", lambda p: p(CPUID_D(0xC, "all"))))
    cs.append(mk("CPUID.(0DH,1):EBX = compacted size of XCR0 | IA32_XSS: 576 + AVX 256 + PKRU 8 + APX 128 "
                 "(XCR0 80207h) with IA32_XSS = 0", lambda p: p(CPUID_D(1, "ebx"))))
    cs.append(mk("CPUID.(0DH,1):EBX after IA32_XSS := 1800h: + CET_U 16 + CET_S 24",
                 lambda p: p(WRMSR(), CPUID_D(1, "ebx")), {"rcx": XSS, "rax": 0x1800}))
    return cs


def cases_ibt():
    cs = []

    def bj(rex2):
        def b(p):
            p(CR4CET(), MOV32(RAX, ENDBR_EN), WRMSR())
            p(LEA_RAX("tgt"), JMP_RAX()).label("tgt")
            p(ENDBR64(rex2), RDMSR())
        return b
    cs.append(mk("IBT (IA32_S_CET.ENDBR_EN = 1): JMP RAX to F3 REX2(80h) 1E FA = ENDBR64 -> no #CP, tracker "
                 "IDLE (RDMSR IA32_S_CET = 4)", bj(0x80), REGS_S))
    cs.append(mk("IBT: JMP RAX to F3 REX2(FFh: M0 W R4 X4 B4 R3 X3 B3) 1E FA = ENDBR64 -> tracker IDLE",
                 bj(0xFF), REGS_S))
    cs.append(mk("IBT: JMP RAX to F3 0F 1E FA (legacy ENDBR64) -> tracker IDLE", bj(None), REGS_S))

    def bf(p):
        gdt_pro(p)
        p(CR4CET(), MOV32(RAX, ENDBR_EN), WRMSR())
        p(CALLF(RSI, CPTR_D)).label("ret1")
        p(RDMSR(), JMPS("END")).label("callee")
        p(ENDBR64(0x80), RETF())
    cs.append(mk("IBT: far CALL to 08h with IA32_S_CET.ENDBR_EN = 1 sets WAIT_FOR_ENDBRANCH; the target "
                 "REX2 ENDBR64 returns the tracker to IDLE; RETF; RDMSR IA32_S_CET = 4",
                 bf, REGS_S, lambda lab: merge(gdt_pro_mem(lab), {RSI0 + CPTR_D: farptr(lab["callee"], CS0)})))
    return cs


def cases_guard():
    cs = []
    cs.append(mk("layout guard: the snippet starts at 30000199h (lea rax, [rip] = 300001A0h)",
                 lambda p: p(fixed("lea rax, [rip]", b"\x48\x8d\x05\x00\x00\x00\x00",
                                   lambda m, ins: m.g.__setitem__(RAX, ins.addr + 7)))))
    return cs


def gen_case_lines():
    out = []
    groups = [
        ("layout guard", cases_guard()),
        ("environment guards (loose): CR4.OSXSAVE = 1, CR4.LA57 = 0 (paging canonical = 48 bits); "
         "CPUID.(7,0): ECX.CET_SS[7] = 1, ECX.LA57[16] = 1 (CPU canonical / LA_adjust N = 57, decisions d, f), "
         "EDX.CET_IBT[20] = 1",
         None),
        ("1/2. far CALL m16:64 (REX.W FF /3) at CPL0 with IA32_S_CET.SH_STK_EN = 1 and far RET (REX.W CB). "
         "Setup: LGDT [rsi+60h] (GDT at rsi: 08h code64 DPL0, 10h data, 18h code64 DPL3, 20h data DPL3, "
         "28h code64 conforming DPL0, 30h call gate, 40h TSS), JMP far 08h, CR4.CET, IA32_S_CET, "
         "IA32_PL0_SSP = T = 30029FF8h, SETSSBSY", cases_call_ret()),
        ("3/4. 64-bit call gate CPL3 -> CPL0 (64-bit TSS RSP0 = 30013800h, LTR 40h; CPL3 reached by IRETQ, "
         "CS 1Bh, SS 23h, RSP 30013E00h) and RETF back to CPL3", cases_gate()),
        ("5. IRETQ CPL0 -> CPL0 (frame built by the callee of a far CALL)", cases_iret()),
        ("5. IRETQ CPL0 -> CPL3", cases_iret_outer()),
        ("6. SYSRET / SYSEXIT (IA32_PL3_SSP = 00007FFF12345674h)", cases_sys()),
        ("6b. WRMSR of the CET MSRs: CPU canonical (57 bits, Vol3A 4.5.3)", cases_wrmsr()),
        ("7. CPUID leaf 0DH for CET state", cases_cpuid()),
        ("7. XSAVES (NP 0F C7 /5, IA32_XSS = 1800h, area at MEM+C000h prefilled with A5h)", cases_xsave()),
        ("7. XRSTORS (NP 0F C7 /3; [rdi] = RDMSR of U_CET, PL3_SSP, PL0_SSP, PL1_SSP, PL2_SSP)",
         cases_xrstors()),
        ("8. IBT: ENDBR64 with a REX2 prefix (decision g), only non-faulting cases", cases_ibt()),
    ]
    n = 0
    for title, cases in groups:
        out.append("#")
        out.append("# --- " + title)
        if cases is None:
            out.append(".byte 0x0f, 0x20, 0xe0, 0x25, 0x00, 0x10, 0x04, 0x00 =>! rax=0x40000")
            out.append(".byte 0xb8, 0x07, 0x00, 0x00, 0x00, 0x31, 0xc9, 0x0f, 0xa2, 0x81, 0xe1, 0x80, 0x00, 0x01, "
                       "0x00, 0x81, 0xe2, 0x00, 0x00, 0x10, 0x00 =>! rcx=0x10080 rdx=0x100000")
            n += 2
            continue
        for c in cases:
            out.append("# " + c.comment)
            out.append(case_line(c))
            n += 1
    return out, n


def case_line(c):
    m, f, fins = simulate(c.prog, c.regs, c.mem)
    code = ".byte " + ", ".join("0x%02x" % b for b in c.prog.code)
    ins = []
    for k, v in c.regs.items():
        ins.append("%s=%#x" % (k, v))
    init = Machine(c.regs, c.mem)
    for a in sorted(c.mem):
        ins.append("m+%#x=%s" % (a - MEM, c.mem[a].hex().upper()))
    exp = []
    for r in range(16):
        if m.g[r] is None:
            raise ModelError("%s unknown at the end of: %s" % (REGN[r], c.comment))
        if m.g[r] != init.g[r]:
            exp.append("%s=%#x" % (REGN[r], m.g[r]))
    if m.rflags is None:
        raise ModelError("RFLAGS unknown at the end of: " + c.comment)
    if m.rflags != init.rflags:
        exp.append("rflags=%#x" % m.rflags)
    changed = sorted(a for a in set(m.mem) | set(init.mem)
                     if MEM <= a < MEM + MEM_SIZE and m.mem.get(a, 0) != init.mem.get(a, 0))
    runs = []
    for a in changed:
        if runs and a - runs[-1][1] <= 8:
            runs[-1][1] = a + 1
        else:
            runs.append([a, a + 1])
    for lo, hi in runs:
        exp.append("m+%#x=%s" % (lo - MEM, bytes(m.mem.get(x, 0) for x in range(lo, hi)).hex().upper()))
    if f:
        exp.append("#" + f.name)
    line = code
    if ins:
        line += " | " + " ".join(ins)
    line += (" => " if not c.loose else " =>! ") + " ".join(exp)
    return line.rstrip()


HEADER = """\
# Intel CET on far transfers (plan item 1.15c, cet2): expected-value cases, Unicorn only.
# Generated by Emulator\\tools\\isa\\ref_cet2.py --cases (an independent model written from the SDM
# text only: Vol1 ch. 18 / 13.11-13.12, Vol2 CALL / RET / IRET / SYSRET / SYSEXIT / XSAVES / XRSTORS,
# Vol4 CET MSRs, APX 3.1.2.1; see the module docstring for the modelling decisions). Regenerate, do
# not edit. Run:
#   emu-alltest --cases Emulator\\data\\cases_cet2.txt --expect-only --apx --cr0 0x10011
# (--apx: reset XCR0 has bit 19 for the REX2 ENDBR64 cases; --cr0 0x10011 = PE|ET|WP, CR4.CET needs
# CR0.WP). Every case starts at CPL0 in raw Unicorn (CS 0, no GDT) and sets up what it needs itself:
# LGDT from operand memory, JMP far to 08h, LTR, CR4.CET (mov rax, cr4; bts rax, 23; mov cr4, rax
# - RAX and the flags are then rewritten), the CET MSRs by WRMSR, SETSSBSY. Snippets are raw bytes
# (".byte") encoded by the generator, so far pointers, LIPs and gate offsets are exact. Shadow stacks
# live in operand memory (no paging: shadow-stack accesses are plain accesses, and the callees edit
# shadow frames with plain stores); SSP and the MSRs are observed with RDSSPQ / RDMSR. A fault is
# compared at the faulting instruction (the model restores every register, decision a). Layout:
# GDT at MEM+8000h (RSI), TSS at MEM+7000h, supervisor tokens T = 30029FF8h, T2 = 30029FD8h,
# T3 = 30029FB0h, IA32_PL3_SSP U = 3002B000h, XSAVE area MEM+C000h.
"""


def write_cases(path):
    lines, n = gen_case_lines()
    with open(path, "w", newline="\r\n") as f:
        f.write(HEADER)
        for ln in lines:
            f.write(ln + "\n")
    return n


# ---- self test ---------------------------------------------------------------------------------
def selftest():
    ok = True

    def chk(name, got, exp):
        nonlocal ok
        if got != exp:
            ok = False
            print("FAIL %s: got %r expected %r" % (name, got, exp))

    # canonical / LA_adjust (worked by hand)
    chk("canon48 top", canonical(0x00007FFFFFFFFFFF), True)
    chk("canon48 gap", canonical(0x0000800000000000), False)
    chk("canon57 gap", canonical(0x0000800000000000, 57), True)
    chk("canon high", canonical(0xFFFF800000000000), True)
    chk("la_adjust identity", la_adjust(0x00007FFFFFFFFFF8), 0x00007FFFFFFFFFF8)
    chk("la_adjust 57", la_adjust(0x0100000000000000), 0xFF00000000000000)
    chk("la_adjust 48", la_adjust(0x0000800000000000, 48), 0xFFFF800000000000)
    # descriptors (Vol3A 3.4.5 layout by hand)
    chk("code64 dpl0", d_code64(0), 0x00AF9B000000FFFF)
    chk("code64 dpl3", d_code64(3), 0x00AFFB000000FFFF)
    chk("data dpl3", d_data(3), 0x00CFF3000000FFFF)
    lo, hi = d_gate64(0x08, 0x123456789ABCDEF0, 3)
    chk("gate lo", lo, 0x9ABCEC000008DEF0)
    chk("gate hi", hi, 0x12345678)
    g = parse_desc(lo, hi)
    chk("gate parse", (g.gsel, g.goff, g.dpl, g.type, g.s), (8, 0x123456789ABCDEF0, 3, 0xC, 0))

    def mach():
        m = Machine()
        for a, b in gdt_mem(gate_off=0x30000300).items():
            for i, x in enumerate(b):
                m.mem[a + i] = x
        m.gdtr = (GDT, 0x4F)
        m.cs = Seg(CS0)
        m.cr4 = CR4_CET
        m.msr[S_CET] = SH_STK_EN
        m.wr(T, 8, T)
        m.msr[PL0_SSP] = T
        m.setssbsy()
        return m

    # SETSSBSY: token busy, SSP = T
    m = mach()
    chk("setssbsy", (m.ssp, m.rd(T, 8)), (T, T | 1))
    # far CALL with SSP = T: frame (CS 8, LIP = next RIP, old SSP) below T
    m.far_call(0x30000280, CS0, 0x30000210)
    chk("call frame", (m.ssp, m.rd(T - 8, 8), m.rd(T - 16, 8), m.rd(T - 24, 8)), (T - 24, 8, 0x30000210, T))
    chk("call regular stack", (m.g[RSP], m.rd(STACK_TOP - 8, 8), m.rd(STACK_TOP - 16, 8)),
        (STACK_TOP - 16, 8, 0x30000210))
    r = m.far_ret()
    chk("retf ok", (r, m.ssp, m.g[RSP]), (0x30000210, T, STACK_TOP))
    # SSP = T+4: zero dword at T, old SSP T+4 in the frame
    m = mach()
    m.incssp(1, False)
    m.far_call(0x30000280, CS0, 0x30000210)
    chk("call misaligned", (m.ssp, m.rd(T, 8), m.rd(T - 24, 8)), (T - 24, 0, T + 4))
    chk("retf to T+4", (m.far_ret(), m.ssp), (0x30000210, T + 4))

    def fault_of(fn):
        try:
            fn()
        except Fault as e:
            return e.name
        return None
    # RETF checks
    m = mach()
    m.far_call(0x30000280, CS0, 0x30000210)
    m.wr(m.ssp + 16, 8, 0x10008)
    chk("retf cs mismatch", fault_of(m.far_ret), "CP")
    m = mach()
    m.far_call(0x30000280, CS0, 0x30000210)
    m.wr(m.ssp, 8, 0x0000800000000000)
    chk("retf gap ssp", fault_of(m.far_ret), "GP")
    m = mach()
    m.far_call(0x30000280, CS0, 0x30000210)
    m.wr(m.ssp, 8, T + 2)
    chk("retf ssp align", fault_of(m.far_ret), "CP")
    # 32-byte rule: T3 (10h mod 20h) faults, T2 (18h mod 20h) passes
    for tok, exp in ((T3, "GP"), (T2, None)):
        m = mach()
        m.tr = (TSSSEL, TSS, 0x67)
        m.wr(TSS + 4, 8, RSP0)
        m.cpl, m.cs, m.ss = 3, Seg(CS3 | 3, dpl=3), Seg(DS3 | 3, dpl=3, code=False)
        m.msr[U_CET], m.ssp, m.msr[PL0_SSP] = SH_STK_EN, U + 8, tok
        m.wr(tok, 8, tok)
        chk("gate 32-byte %#x" % tok, fault_of(lambda: m.far_call(0, GATE | 3, 0x30000220)), exp)
        if exp is None:
            chk("gate state", (m.cpl, m.ssp, m.rd(tok, 8), m.msr[PL3_SSP], m.g[RSP], m.cs.sel),
                (0, tok, tok | 1, U + 8, RSP0 - 32, CS0))
            # RETF to CPL3: SSP := PL3_SSP, token freed
            chk("retf outer", (m.far_ret(), m.cpl, m.ssp, m.rd(tok, 8), m.g[RSP]),
                (0x30000220, 3, U + 8, tok, STACK_TOP))
    # IRET same privilege with a stack switch frees the token at SSP+24
    m = mach()
    m.far_call(0x30000280, CS0, 0x30000210)
    m.wr(m.ssp, 8, 0x30029E04)
    for v in (0, STACK_TOP, 0x287, CS0, 0x30000210):
        m.push8(v)
    chk("iret switch", (m.iretq(), m.ssp, m.rd(T, 8), m.rflags, m.g[RSP]),
        (0x30000210, 0x30029E04, T, 0x287, STACK_TOP))
    # SYSRET: RFLAGS mask 3C7FD7h, SSP := PL3_SSP when enabled
    m = Machine({"rcx": 0x30000300, "r11": 0xFFFFFFFF})
    m.cr4 = CR4_CET
    m.msr[U_CET], m.msr[PL3_SSP], m.msr[STAR] = 1, 0x00007FFF12345674, 0x0010 << 48
    chk("sysret", (m.sysret64(), m.rflags, m.ssp, m.cs.sel), (0x30000300, 0x3C7FD7, 0x00007FFF12345674, 0x23))
    # XSAVES layout (Vol1 13.4.3 compacted: 576, +16)
    m = Machine()
    m.msr.update(VALS)
    m.msr[XSS] = 0x1800
    m.xsaves(XAREA, 0x1800)
    chk("xsaves hdr", (m.rd(XAREA + 512, 8), m.rd(XAREA + 520, 8)), (0x1800, 0x8000000000001800))
    chk("xsaves cet_u", (m.rd(XAREA + 576, 8), m.rd(XAREA + 584, 8)), (VALS[U_CET], VALS[PL3_SSP]))
    chk("xsaves cet_s", m.rd(XAREA + 608, 8), VALS[PL2_SSP])
    m.wr(XAREA + 576, 8, 0x40)
    chk("xrstors bits 9:6", fault_of(lambda: m.xrstors(XAREA, 0x1800)), "GP")
    m.wr(XAREA + 576, 8, 0xC00)
    chk("xrstors suppress+tracker", fault_of(lambda: m.xrstors(XAREA, 0x1800)), "GP")
    m.wr(XAREA + 576, 8, 0x400)
    chk("xrstors suppress only", fault_of(lambda: m.xrstors(XAREA, 0x1800)), None)
    chk("xrstors loaded", m.msr[U_CET], 0x400)
    chk("wrmsr gap ok", Machine().msr_ok(PL3_SSP, 0x0000800000000000), True)
    chk("wrmsr 57 bad", Machine().msr_ok(U_CET, 0x0100000000000000), False)
    chk("wrmsr 57 high ok", Machine().msr_ok(S_CET, 0xFF00000000000000), True)
    chk("cpuid d1 ebx", xsave_compacted_size(XCR0), 968)
    chk("cpuid d1 ebx xss", xsave_compacted_size(XCR0 | 0x1800), 1008)
    # every case simulates without a model error and fits 255 bytes
    try:
        lines, n = gen_case_lines()
        chk("case count > 50", n > 50, True)
    except ModelError as e:
        ok = False
        print("FAIL case generation: %s" % e)
    return ok


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest %s" % ("passed" if ok else "FAILED"))
        sys.exit(0 if ok else 1)
    if "--cases" in sys.argv:
        path = sys.argv[sys.argv.index("--cases") + 1]
        n = write_cases(path)
        print("%d cases written to %s" % (n, path))
        return
    print(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main()
