r"""Independent reference model + expected-value case generator (ledger U800-U829, agent "sysins"):

  * AVX512DQ VPMOVD2M / VPMOVQ2M / VPMOVM2D / VPMOVM2Q (U800)       -> Emulator\data\cases_sysins_dq.txt
  * CPL0 system instructions, MAX model, no opt-in                  -> Emulator\data\cases_sysins.txt
      WRMSRNS (U801), RDMSRLIST / WRMSRLIST (U802), RDMSR / WRMSRNS imm32 VEX (U803), HRESET (U804),
      LKGS (U805)
  * the same, Intel APX EVEX forms (--apx)                          -> Emulator\data\cases_sysins_apx.txt
      RDMSR / WRMSRNS imm32 EVEX map 7 (U803)

Written from the Intel manuals only (the emulator's C sources were not read for the expected values;
no CPU measurements - the i5-13600K has no AVX-512):
  * SDM Vol2C 325462-092 VPMOVB2M/VPMOVW2M/VPMOVD2M/VPMOVQ2M (opcode table, Operation:
    "(KL, VL) = (4, 128), (8, 256), (16, 512) ... IF SRC[i+31] THEN DEST[j] := 1 ... DEST[MAX_KL-1:KL]
    := 0") and VPMOVM2B/VPMOVM2W/VPMOVM2D/VPMOVM2Q ("DEST[i+31:i] := -1 ... DEST[MAXVL-1:VL] := 0").
  * SDM Vol2A 2.7 (EVEX P0/P1/P2 layout, Table 2-40 opcode-independent fields, Table 2-41 operand
    encoding: EVEX.R / EVEX.R' must be 1 when ModRM.reg encodes a k-reg, EVEX.X / EVEX.B ignored
    when ModRM.r/m encodes a k-reg; Table 2-42: aaa != 000b and z != 0 #UD for VPMOVM2x / VPMOVx2M;
    Table 2-43: EVEX.b = 1 #UD for "other instruction classes"), 2.8.7 Table 2-57 (E7NM: vvvv !=
    1111b, V' = 0, L'L = 11b #UD), and the "RM" operand encoding (ModRM.r/m register only).

System instructions, from:
  * SDM Vol2D WRMSRNS (NP 0F 01 C6; Operation "MSR[ECX] := EDX:EAX"; #GP(0) CPL > 0, reserved MSR
    bits; #UD LOCK); Vol2B RDMSR (0F 32: "EDX:EAX := MSR[ECX]", high 32 bits of RAX/RDX cleared);
    Vol1 Table 21-22 (CPUID.(07H,1):EAX[19] WRMSRNS); XED wrmsrns-isa.xed.txt (no_refining_prefix
    = no 66/F2/F3: 66 0F 01 C6 is not WRMSRNS -> #UD).
  * SDM Vol2B RDMSRLIST / Vol2D WRMSRLIST (F2 / F3 0F 01 C6, 64-bit mode only; Operation: DO WHILE
    RCX != 0, the lowest set bit n, entry = 8 bytes at RSI + 8n, entry[63:32] != 0 #GP(0), the
    MSR value stored to / loaded from RDI + 8n, RCX[n] := 0; #GP(0) CPL > 0, RSI[2:0] or RDI[2:0]
    != 0, an RDMSR / WRMSR #GP; partial completion on faults); Vol1 Table 21-22 (CPUID.(07H,1):
    EAX[27] MSRLIST); Vol4 IA32_BARRIER (2FH, R/O, 0); XED msrlist-isa.xed.txt (f2/f3_refining_
    prefix: a 66 prefix does not change the instruction).
  * SDM Vol2B RDMSR / Vol2D WRMSRNS immediate forms (VEX.128.F2/F3.MAP7:W0 F6 11:000:bbb id,
    64-bit mode, CPUID.(07H,1):ECX.MSR_IMM[5] (Vol1 Table 21-24), "DEST := MSR[SRC]" /
    "MSR[DEST] := SRC" with the full 64-bit register); APX spec 355828-009 6.49 / 6.70 (EVEX.128
    map 7 forms) and 4.2.21 (class MSR-IMM-EVEX: payload byte 3 all 0 apart from V4 = 1, L = 0,
    vvvv = 1111b; ModRM.mod = 11b needs U = 1, 3.1.2.3); XED msr-imm-isa.xed.txt /
    apx-f-msr-imm-isa.xed.txt (REG = 0, MOD = 3, UBIT = 1).
  * SDM Vol2A HRESET (F3 0F 3A F0 C0 ib; #GP(0) "If CPL > 0 or (EAX AND NOT IA32_HRESET_ENABLE)
    != 0", imm8 ignored, EAX = 0 a NOP); Vol1 Table 21-22 (CPUID.(07H,1):EAX[22]) and Table 21-72
    (leaf 20H: EBX[0] THREAD_DIRECTOR_HRESET, the model reports 1); Vol3B 17.6.11.1 / Vol4
    IA32_HRESET_ENABLE (17DAH: bit 0, 31:1 and 63:32 reserved; "only the bits enumerated by
    CPUID.20H.00H:EBX can be set"); XED hreset-isa.xed.txt (MOD = 3, REG = 0, RM = 0,
    f3_refining_prefix).
  * SDM Vol2A LKGS (F2 0F 00 /6, 64-bit mode only; Operation: CPL > 0 #UD; null selector:
    GS.selector := SRC, IA32_KERNEL_GS_BASE := 0; index outside the table limit, not a data or
    readable code segment, or SRC.RPL > DPL: #GP(selector); not present #NP(selector);
    IA32_KERNEL_GS_BASE := descriptor.base (63:32 cleared), the GS base not modified); Vol1
    Table 21-22 (CPUID.(07H,1):EAX[18]); Vol3A 3.4.5 descriptor layout; MOV (to GS) for the
    accessed bit; Vol2A LGDT (64-bit: 2-byte limit, 8-byte base); MOV r16, Sreg (low 16 bits).
  * SDM Vol4 Table 2-2 MSR layouts used by the cases: IA32_KERNEL_GS_BASE (C0000102H, canonical),
    IA32_UMWAIT_CONTROL (E1H: bit 1 and 63:32 reserved), IA32_PASID (D93H: 30:20 and 63:32
    reserved), IA32_UARCH_MISC_CTL (1B01H: 63:1 reserved). An MSR write that sets a reserved bit is
    #GP(0) and changes nothing.

Modelling decisions:
  a. The opcode table lists only register forms ("k1, xmm1" / "xmm1, k1"): ModRM.mod != 11b #UD.
  b. NP / F2 forms of EVEX.0F38 38/39 do not exist (#UD); the 66 forms are VPMINSB / VPMINSD/Q
     (other instructions, not generated here).
  c. A faulting instruction changes no register (the case harness saves the state at the faulting
     instruction); instructions before it in the snippet keep their effects.
  d. REX prefixes before 0F 01 C6 are ignored (the opcode is fixed by the ModRM byte).
  e. Only canonical values are written to address MSRs (the emulator's WRMSR does not check them;
     outside this model).
  f. RDMSRLIST / WRMSRLIST check RSI / RDI alignment first, also when RCX = 0 (the SDM lists the
     #GP without a condition on RCX); entries are processed strictly in order, no load-ahead
     (a #PF on entry n happens after entries < n completed).
  g. MSR-IMM W1 is #UD for the VEX form (SDM "W0") and for the EVEX form (the APX table prints
     "N/A"; read as the VEX form's W0, as asmjit's db does). NP / 66 F6 in map 7 is no instruction.

Usage:
  python -I ref_sysins.py --selftest   hand-derived checks of the model (exit 0 on pass)
  python -I ref_sysins.py --write      regenerate the case files listed above (CRLF)
"""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, '..', '..', 'data'))


def hexbytes(b):
    return bytes(b).hex().upper()


def bytelist(b):
    return ', '.join('0x%02x' % x for x in b)


# ------------------------------------------------------------------------------------------------
# EVEX encoding (SDM Vol2A 2.7.1, Figure 2-11): P0 = R X B R' 0 0 m m (R X B R' stored inverted),
# P1 = W vvvv 1 pp (vvvv inverted), P2 = z L'L b V' aaa (V' inverted). Logical register-number bits
# are passed; raw_* override a stored bit directly (for the #UD cases).
# ------------------------------------------------------------------------------------------------
def evex(op, mod, reg, rm, w, ll, pp, mm=2, vvvv=0, vhi=0, z=0, b=0, aaa=0, raw_r=None, raw_rp=None,
         raw_x=None, raw_b=None, tail=b''):
    r_bit = (~reg >> 3) & 1 if raw_r is None else raw_r
    rp_bit = (~reg >> 4) & 1 if raw_rp is None else raw_rp
    x_bit = (~rm >> 4) & 1 if raw_x is None else raw_x
    b_bit = (~rm >> 3) & 1 if raw_b is None else raw_b
    p0 = (r_bit << 7) | (x_bit << 6) | (b_bit << 5) | (rp_bit << 4) | mm
    p1 = (w << 7) | (((~vvvv) & 15) << 3) | 4 | pp
    p2 = (z << 7) | (ll << 5) | (b << 4) | ((~vhi & 1) << 3) | aaa
    modrm = (mod << 6) | ((reg & 7) << 3) | (rm & 7)
    return bytes([0x62, p0, p1, p2, op, modrm]) + tail


# ------------------------------------------------------------------------------------------------
# AVX512DQ VPMOVD2M / VPMOVQ2M / VPMOVM2D / VPMOVM2Q (SDM Vol2C Operation, transcribed)
# ------------------------------------------------------------------------------------------------
def vpmov2m(src, esz, vl):
    """VPMOVD2M (esz 32) / VPMOVQ2M (esz 64): bit j of the 64-bit k = SRC[j*esz + esz-1]."""
    kl = vl // esz
    k = 0
    for j in range(kl):
        i = j * esz
        bit = (src[(i + esz - 1) // 8] >> ((i + esz - 1) % 8)) & 1
        k |= bit << j
    return k                                    # DEST[MAX_KL-1:KL] := 0


def vpmovm2(k, esz, vl):
    """VPMOVM2D / VPMOVM2Q: element j = all ones if k[j], else 0; DEST[MAXVL-1:VL] := 0."""
    kl = vl // esz
    out = bytearray(64)
    for j in range(kl):
        if (k >> j) & 1:
            for x in range(j * esz // 8, (j + 1) * esz // 8):
                out[x] = 0xFF
    return out


def cases_dq():
    rnd = random.Random(0x5150800)
    lines = []
    a = lines.append
    a('# AVX512DQ VPMOVD2M / VPMOVQ2M / VPMOVM2D / VPMOVM2Q (ledger U800): expected values from the')
    a('# independent SDM model Emulator/tools/isa/ref_sysins.py (regenerate with --write, do not edit).')
    a('# The i5-13600K has no AVX-512: expected-value cases only, run with AVX-512 or AVX10.1:')
    a('#   emu-alltest --cases Emulator\\data\\cases_sysins_dq.txt --avx512 --expect-only')
    a('#   emu-alltest --cases Emulator\\data\\cases_sysins_dq.txt --avx10 1 --expect-only')
    names = {(0x38, 0): 'VPMOVM2D', (0x38, 1): 'VPMOVM2Q', (0x39, 0): 'VPMOVD2M', (0x39, 1): 'VPMOVQ2M'}
    vls = {0: 128, 1: 256, 2: 512}

    def rzmm():
        # mix of sign bits: random bytes, then force some element tops
        return bytearray(rnd.getrandbits(8) for _ in range(64))

    # register choices: VPMOVM2x (dest zmm, source k), VPMOVx2M (dest k, source zmm)
    m2_regs = [(0, 2), (9, 7), (17, 1), (31, 0), (12, 5)]
    tom_regs = [(0, 2), (7, 9), (1, 17), (3, 31), (5, 26)]
    for op in (0x38, 0x39):
        for w in (0, 1):
            esz = 64 if w else 32
            for ll in (0, 1, 2):
                vl = vls[ll]
                a('# %s VL%d' % (names[(op, w)], vl))
                regs = m2_regs if op == 0x38 else tom_regs
                for reg, rm in regs:
                    kin = rnd.getrandbits(64)
                    z = rzmm()
                    if op == 0x38:
                        enc = evex(op, 3, reg, rm, w, ll, 2)
                        exp = vpmovm2(kin, esz, vl)
                        a('.byte %s | zmm%d=%s k%d=0x%X => zmm%d=%s' %
                          (bytelist(enc), reg, hexbytes(z), rm, kin, reg, hexbytes(exp)))
                    else:
                        enc = evex(op, 3, reg, rm, w, ll, 2)
                        exp = vpmov2m(z, esz, vl)
                        a('.byte %s | zmm%d=%s k%d=0x%X => k%d=0x%X' %
                          (bytelist(enc), rm, hexbytes(z), reg, kin, reg, exp))
                # all-ones / all-zero / alternating patterns
                for pat in (0xFFFFFFFFFFFFFFFF, 0, 0x5555555555555555):
                    if op == 0x38:
                        z = rzmm()
                        enc = evex(op, 3, 3, 4, w, ll, 2)
                        a('.byte %s | zmm3=%s k4=0x%X => zmm3=%s' %
                          (bytelist(enc), hexbytes(z), pat, hexbytes(vpmovm2(pat, esz, vl))))
                    else:
                        z = bytearray(pat.to_bytes(8, 'little') * 8)
                        enc = evex(op, 3, 6, 4, w, ll, 2)
                        a('.byte %s | zmm4=%s k6=0xFFFFFFFFFFFFFFFF => k6=0x%X' %
                          (bytelist(enc), hexbytes(z), vpmov2m(z, esz, vl)))
    # Table 2-41: X / B are ignored when ModRM.r/m encodes a k-reg (VPMOVM2x)
    a('# VPMOVM2D/Q: EVEX.X / EVEX.B ignored for the k-reg in ModRM.r/m (Table 2-41)')
    for w in (0, 1):
        esz = 64 if w else 32
        for rx, rb in ((0, 1), (1, 0), (0, 0)):
            kin = rnd.getrandbits(64)
            z = rzmm()
            enc = evex(0x38, 3, 5, 3, w, 2, 2, raw_x=rx, raw_b=rb)
            a('.byte %s | zmm5=%s k3=0x%X => zmm5=%s' %
              (bytelist(enc), hexbytes(z), kin, hexbytes(vpmovm2(kin, esz, 512))))
    # #UD conditions
    a('# #UD: memory form, aaa != 0, z = 1, b = 1, L\'L = 11b, vvvv != 1111b, V\' = 0, k-reg in ModRM.reg')
    a('# with EVEX.R = 0 or EVEX.R\' = 0 (stored bits), NP / F2 forms of 38 and 39')
    for op in (0x38, 0x39):
        for w in (0, 1):
            ud = [
                evex(op, 0, 1, 6, w, 2, 2),                   # [rsi]
                evex(op, 1, 1, 6, w, 2, 2, tail=b'\x40'),     # [rsi+disp8]
                evex(op, 3, 1, 2, w, 2, 2, aaa=1),
                evex(op, 3, 1, 2, w, 2, 2, aaa=7),
                evex(op, 3, 1, 2, w, 2, 2, z=1),
                evex(op, 3, 1, 2, w, 2, 2, b=1),
                evex(op, 3, 1, 2, w, 3, 2),
                evex(op, 3, 1, 2, w, 2, 2, vvvv=1),
                evex(op, 3, 1, 2, w, 2, 2, vvvv=15),
                evex(op, 3, 1, 2, w, 2, 2, vhi=1),
                evex(op, 3, 1, 2, w, 2, 0),                   # NP
                evex(op, 3, 1, 2, w, 2, 3),                   # F2
            ]
            if op == 0x39:
                ud.append(evex(op, 3, 1, 2, w, 2, 2, raw_r=0))
                ud.append(evex(op, 3, 1, 2, w, 2, 2, raw_rp=0))
            for enc in ud:
                a('.byte %s => #UD' % bytelist(enc))
    return lines


# ------------------------------------------------------------------------------------------------
# System instructions: a small CPL0 machine (registers, the MSRs the cases use, operand memory) that
# runs the snippet instruction by instruction; the case expectation is its end state (or the fault
# and the state before the faulting instruction).
# ------------------------------------------------------------------------------------------------
REGS = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi',
        'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15'] + ['r%d' % i for i in range(16, 32)]
MEM = 0x30020000                    # emu-alltest operand memory (64 KiB), RSI = MEM + 0x8000
MEM_PTR = MEM + 0x8000
MEM_DST = MEM + 0x9000
M64 = (1 << 64) - 1


class Fault(Exception):
    """A fault; keep=True: the instruction's completed part stays (RDMSRLIST / WRMSRLIST)."""
    def __init__(self, name, keep=False):
        Exception.__init__(self, name)
        self.keep = keep


def canonical(v, bits=57):
    """CPU canonical (MSR values): the model reports LA57, so 57 bits (SDM Vol3A 4.5.3)."""
    top = v >> (bits - 1)
    return top == 0 or top == (1 << (65 - bits)) - 1


def msr_check(msr, val):
    """WRMSR / WRMSRNS value checks (SDM Vol4); False = #GP(0)."""
    if msr == 0xC0000102:                       # IA32_KERNEL_GS_BASE
        return canonical(val)
    if msr == 0xE1:                             # IA32_UMWAIT_CONTROL
        return (val & ~0xFFFFFFFD & M64) == 0
    if msr == 0xD93:                            # IA32_PASID
        return (val & ~0x800FFFFF & M64) == 0
    if msr == 0x1B01:                           # IA32_UARCH_MISC_CTL
        return (val & ~1 & M64) == 0
    if msr == 0x2F:                             # IA32_BARRIER: R/O
        return False
    if msr == 0x17DA:                           # IA32_HRESET_ENABLE: CPUID.20H.0:EBX = 1
        return (val & ~1 & M64) == 0
    if msr == 0xC0000101:                       # IA32_GS_BASE
        return canonical(val)
    raise ValueError('MSR %X not in the model' % msr)


def msr_read(m, msr):
    if msr == 0x2F:                             # IA32_BARRIER: always 0
        return 0
    return m.msr[msr]                           # the cases read only MSRs they wrote


def ld64(m, addr):
    """8-byte CPL0 data read of the operand memory; outside it (MEM + 10000h is unmapped) #PF."""
    if not (MEM <= addr and addr + 8 <= MEM + 0x10000):
        raise Fault('#PF', keep=True)
    return int.from_bytes(bytes(m.mem.get(addr + i, 0) for i in range(8)), 'little')


def st64(m, addr, v):
    if not (MEM <= addr and addr + 8 <= MEM + 0x10000):
        raise Fault('#PF', keep=True)
    for i in range(8):
        m.mem[addr + i] = (v >> (8 * i)) & 0xFF


class Machine:
    def __init__(self, inputs):
        self.r = {k: 0 for k in REGS}
        self.r['rsi'] = MEM_PTR
        self.r['rdi'] = MEM_DST
        self.r['r14'] = MEM_PTR
        self.rflags = 0x202
        self.msr = {}
        self.mem = {}
        self.cpl = 0
        self.gdtr = (0, 0)
        self.gs_sel = 0
        for k, v in inputs.items():
            if k in self.r:
                self.r[k] = v
            elif k == 'rflags':
                self.rflags = v
            elif k == 'cpl':
                self.cpl = v
            elif k.startswith('m+'):
                off = int(k[2:], 16)
                for i, x in enumerate(v):
                    self.mem[MEM + off + i] = x
            else:
                raise ValueError(k)

    def snapshot(self):
        return dict(self.r), self.rflags, dict(self.mem), dict(self.msr), self.gdtr, self.gs_sel

    def restore(self, s):
        self.r, self.rflags, self.mem, self.msr = dict(s[0]), s[1], dict(s[2]), dict(s[3])
        self.gdtr, self.gs_sel = s[4], s[5]


def i_mov32(reg, imm):
    """MOV r32, imm32 (B8+r id): zero-extends to 64 bits; no flags."""
    n = REGS.index(reg)
    enc = (b'\x41' if n >= 8 else b'') + bytes([0xB8 + (n & 7)]) + (imm & 0xFFFFFFFF).to_bytes(4, 'little')

    def f(m):
        m.r[reg] = imm & 0xFFFFFFFF
    return enc, f


def i_rdmsr():
    def f(m):
        if m.cpl:
            raise Fault('#GP')
        v = msr_read(m, m.r['rcx'] & 0xFFFFFFFF)
        m.r['rax'] = v & 0xFFFFFFFF
        m.r['rdx'] = v >> 32
    return b'\x0f\x32', f


def i_lea_rdi(disp):
    """LEA RDI, [RDI + disp32] (48 8D BF id); no flags."""
    def f(m):
        m.r['rdi'] = (m.r['rdi'] + disp) & M64
    return b'\x48\x8d\xbf' + (disp & 0xFFFFFFFF).to_bytes(4, 'little'), f


def i_msrlist(write, prefix=b''):
    """RDMSRLIST (F2 0F 01 C6) / WRMSRLIST (F3 0F 01 C6), SDM Vol2B / Vol2D Operation."""
    def f(m):
        if b'\xf0' in prefix:
            raise Fault('#UD')
        if m.cpl:
            raise Fault('#GP')
        if (m.r['rsi'] & 7) or (m.r['rdi'] & 7):
            raise Fault('#GP')
        while m.r['rcx']:
            n = (m.r['rcx'] & -m.r['rcx']).bit_length() - 1
            ent = ld64(m, (m.r['rsi'] + 8 * n) & M64)
            if ent >> 32:
                raise Fault('#GP', keep=True)
            if write:
                v = ld64(m, (m.r['rdi'] + 8 * n) & M64)
                if not msr_check(ent, v):
                    raise Fault('#GP', keep=True)
                m.msr[ent] = v
            else:
                st64(m, (m.r['rdi'] + 8 * n) & M64, msr_read(m, ent))
            m.r['rcx'] &= ~(1 << n)
    mand = b'\xf3' if write else b'\xf2'
    if prefix and 0x40 <= prefix[0] <= 0x4F:    # REX goes after the mandatory prefix
        return mand + prefix + b'\x0f\x01\xc6', f
    return prefix + mand + b'\x0f\x01\xc6', f


def i_wrmsrns(prefix=b''):
    """WRMSRNS (NP 0F 01 C6): MSR[ECX] := EDX:EAX."""
    def f(m):
        if b'\xf0' in prefix or b'\x66' in prefix:
            raise Fault('#UD')
        if m.cpl:
            raise Fault('#GP')
        msr = m.r['rcx'] & 0xFFFFFFFF
        val = ((m.r['rdx'] & 0xFFFFFFFF) << 32) | (m.r['rax'] & 0xFFFFFFFF)
        if not msr_check(msr, val):
            raise Fault('#GP')
        m.msr[msr] = val
    return prefix + b'\x0f\x01\xc6', f


def i_msrimm(write, reg, imm, evex_form=False, w=0, l=0, vvvv=0, regfield=0, pp=None, mod=3,
             prefix=b'', nf=0, nd=0, vhi=0, z=0, ubit=1, aaa=0):
    """RDMSR r64, imm32 (F2 MAP7 F6 /0) / WRMSRNS imm32, r64 (F3 MAP7 F6 /0), SDM Vol2B/2D +
    APX spec 6.49/6.70: VEX.128.W0 (C4, map 7) or the APX EVEX.128 map 7 form (62, P0[2:0] =
    111, B4 = P0[3], U = P1[2], P2 = 0 0 L 0 V4 NF 0 0). DEST := MSR[imm32] / MSR[imm32] := SRC."""
    n = REGS.index(reg)
    if pp is None:
        pp = 2 if write else 3
    if not evex_form:
        b1 = (1 << 7) | (1 << 6) | ((~n >> 3 & 1) << 5) | 0x07
        b2 = (w << 7) | ((~vvvv & 15) << 3) | (l << 2) | pp
        enc = prefix + bytes([0xC4, b1, b2])
    else:
        p0 = (1 << 7) | (1 << 6) | ((~n >> 3 & 1) << 5) | (1 << 4) | ((n >> 4 & 1) << 3) | 0x07
        p1 = (w << 7) | ((~vvvv & 15) << 3) | (ubit << 2) | pp
        p2 = (z << 7) | (l << 5) | (nd << 4) | ((~vhi & 1) << 3) | (nf << 2) | aaa
        enc = prefix + bytes([0x62, p0, p1, p2])
    enc += bytes([0xF6, (mod << 6) | ((regfield & 7) << 3) | (n & 7)])
    if mod == 0:
        enc += b''                              # [rax]-style memory form (only for #UD cases)
    enc += (imm & 0xFFFFFFFF).to_bytes(4, 'little')
    ud = (prefix != b'' or w or l or vvvv or regfield or mod != 3 or pp not in (2, 3) or
          (evex_form and (nf or nd or vhi or z or not ubit or aaa)))

    def f(m):
        if ud:
            raise Fault('#UD')
        if m.cpl:
            raise Fault('#GP')
        msr = imm & 0xFFFFFFFF
        if pp == 2:                             # F3: WRMSRNS imm32, r64
            if not msr_check(msr, m.r[reg]):
                raise Fault('#GP')
            m.msr[msr] = m.r[reg]
        else:                                   # F2: RDMSR r64, imm32
            m.r[reg] = msr_read(m, msr)
    return enc, f


def i_hreset(imm, prefix=b'', mand=b'\xf3', rex=b'', modrm=0xC0):
    """HRESET imm8, <EAX> (F3 0F 3A F0 C0 ib), SDM Vol2A: #GP(0) if CPL > 0 or (EAX AND NOT
    IA32_HRESET_ENABLE) != 0; otherwise a history reset (no architectural state)."""
    enc = prefix + mand + rex + b'\x0f\x3a\xf0' + bytes([modrm, imm & 0xFF])
    ud = b'\xf0' in prefix or mand != b'\xf3' or modrm != 0xC0

    def f(m):
        if ud:
            raise Fault('#UD')
        if m.cpl:
            raise Fault('#GP')
        if (m.r['rax'] & 0xFFFFFFFF) & ~m.msr.get(0x17DA, 0):
            raise Fault('#GP')
    return enc, f


def i_wrmsr():
    """WRMSR (0F 30): MSR[ECX] := EDX:EAX (the same checks as WRMSRNS)."""
    e, f = i_wrmsrns()
    return b'\x0f\x30', f


def i_lgdt():
    """LGDT [RSI] (0F 01 16), 64-bit: limit = 2 bytes, base = 8 bytes."""
    def f(m):
        a = m.r['rsi']
        lim = ld64(m, a) & 0xFFFF
        m.gdtr = (ld64(m, a + 2), lim)
    return b'\x0f\x01\x16', f


def i_mov_gs(reg):
    """MOV r16, GS (66 [41] 8C /5 mod = 11): the low 16 bits only."""
    n = REGS.index(reg)

    def f(m):
        m.r[reg] = (m.r[reg] & ~0xFFFF & M64) | m.gs_sel
    return b'\x66' + (b'\x41' if n >= 8 else b'') + bytes([0x8C, 0xE8 | (n & 7)]), f


def lkgs_op(m, sel):
    """SDM Vol2A LKGS Operation (64-bit mode, CPL 0)."""
    sel &= 0xFFFF
    if (sel & 0xFFFC) == 0:
        m.gs_sel = sel
        m.msr[0xC0000102] = 0
        return
    if sel & 4:                                 # no LDT in the cases (LDTR null, limit 0)
        raise Fault('#GP')
    base, limit = m.gdtr
    index = sel & ~7
    if index + 7 > limit:
        raise Fault('#GP')
    d = ld64(m, base + index)
    s, typ, dpl, p = (d >> 44) & 1, (d >> 40) & 15, (d >> 45) & 3, (d >> 47) & 1
    code, readable = typ & 8, typ & 2
    if not s or (code and not readable) or (sel & 3) > dpl:
        raise Fault('#GP')
    if not p:
        raise Fault('#NP')
    if not typ & 1:                             # accessed bit, as MOV to GS
        m.mem[base + index + 5] = (d >> 40 & 0xFF) | 1
    m.gs_sel = sel
    m.msr[0xC0000102] = ((d >> 16) & 0xFFFFFF) | (((d >> 56) & 0xFF) << 24)


def i_lkgs(reg=None, disp=None, prefix=b'', mand=b'\xf2'):
    """LKGS r/m16 (F2 0F 00 /6): register form (reg) or [RSI + disp8]."""
    if reg is not None:
        n = REGS.index(reg)
        enc = prefix + mand + (b'\x41' if n >= 8 else b'') + b'\x0f\x00' + bytes([0xF0 | (n & 7)])
    else:
        enc = prefix + mand + b'\x0f\x00\x76' + bytes([disp & 0xFF])
    ud = b'\xf0' in prefix or mand != b'\xf2'

    def f(m):
        if ud or m.cpl:                         # LKGS: CPL > 0 is #UD
            raise Fault('#UD')
        if reg is not None:
            sel = m.r[reg] & 0xFFFF
        else:
            a = m.r['rsi'] + disp
            sel = ld64(m, a) & 0xFFFF
        lkgs_op(m, sel)
    return enc, f


def desc(base, limit, typ, dpl, p=1, s=1, g=1, db=1):
    """A legacy segment descriptor (SDM Vol3A 3.4.5)."""
    return ((limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (typ << 40) | (s << 44) | (dpl << 45) |
            (p << 47) | (((limit >> 16) & 15) << 48) | (db << 54) | (g << 55) | ((base >> 24) << 56))


def run_case(ins, inputs):
    """Run the instruction list on the model; returns the case line."""
    m = Machine(inputs)
    r0, f0, mem0 = m.snapshot()[:3]
    code = b''.join(e for e, _ in ins)
    fault = None
    for _, f in ins:
        saved = m.snapshot()
        try:
            f(m)
        except Fault as e:
            if not e.keep:
                m.restore(saved)
            fault = str(e)
            break
    exp = []
    for k in REGS:
        if m.r[k] != r0[k]:
            exp.append('%s=0x%X' % (k, m.r[k]))
    if m.rflags != f0:
        exp.append('rflags=0x%X' % m.rflags)
    run = []                                    # contiguous changed bytes -> one m+OFF=HEX
    for a in sorted(set(m.mem) | set(mem0)):
        if m.mem.get(a, 0) != mem0.get(a, 0):
            if run and run[-1][0] + len(run[-1][1]) == a:
                run[-1][1].append(m.mem.get(a, 0))
            else:
                run.append((a, [m.mem.get(a, 0)]))
    for a, bs in run:
        exp.append('m+0x%X=%s' % (a - MEM, hexbytes(bs)))
    if fault:
        exp.append(fault)
    inp = []
    for k, v in inputs.items():
        if k == 'cpl':
            inp.append('cpl=%d' % v)
        elif k.startswith('m+'):
            inp.append('%s=%s' % (k, hexbytes(v)))
        else:
            inp.append('%s=0x%X' % (k, v))
    line = '.byte %s' % bytelist(code)
    if inp:
        line += ' | ' + ' '.join(inp)
    line += ' => ' + ' '.join(exp)
    return line.rstrip()


def cases_sys():
    lines = []
    a = lines.append
    a('# CPL0 system instructions (ledger U801-U829): expected values from the independent model')
    a('# Emulator/tools/isa/ref_sysins.py (regenerate with --write, do not edit). Unicorn only, MAX')
    a('# model (CPL0 instructions; the i5-13600K lacks most of them):')
    a('#   emu-alltest --cases Emulator\\data\\cases_sysins.txt --expect-only')
    a('# Without cpl=3 the snippet runs at CPL0; cpl=3 runs it at CPL3 (U452).')
    rd = [i_mov32('rax', 0), i_mov32('rdx', 0), i_rdmsr()]
    # ---- WRMSRNS (U801)
    a('# --- WRMSRNS (NP 0F 01 C6, U801): EDX:EAX -> MSR[ECX], high halves of RAX/RCX/RDX ignored;')
    a('# read back with RDMSR; REX ignored')
    a(run_case([i_wrmsrns()] + rd, {'rcx': 0xFFFFFFFFC0000102, 'rax': 0xAAAAAAAA89ABCDEF,
                                    'rdx': 0x55555555FFFF8000}))
    a(run_case([i_wrmsrns()] + rd, {'rcx': 0xC0000102, 'rax': 0x12345678, 'rdx': 0x00007FFF}))
    a(run_case([i_wrmsrns()] + rd, {'rcx': 0xE1, 'rax': 0x000186A1, 'rdx': 0}))
    a(run_case([i_wrmsrns()] + rd, {'rcx': 0xD93, 'rax': 0x800ABCDE, 'rdx': 0}))
    a(run_case([i_wrmsrns()] + rd, {'rcx': 0x1B01, 'rax': 1, 'rdx': 0}))
    a(run_case([i_wrmsrns(b'\x48')] + rd, {'rcx': 0x1B01, 'rax': 0, 'rdx': 0}))
    a(run_case([i_wrmsrns(b'\x41')] + rd, {'rcx': 0xE1, 'rax': 0x00000100, 'rdx': 0}))
    a('# reserved MSR bits -> #GP(0), registers and the MSR unchanged')
    a(run_case([i_wrmsrns()], {'rcx': 0xE1, 'rax': 0x2, 'rdx': 0}))
    a(run_case([i_wrmsrns()], {'rcx': 0xE1, 'rax': 0, 'rdx': 1}))
    a(run_case([i_wrmsrns()], {'rcx': 0xD93, 'rax': 0x00100000, 'rdx': 0}))
    a(run_case([i_wrmsrns()], {'rcx': 0x1B01, 'rax': 2, 'rdx': 0}))
    a(run_case([i_mov32('rcx', 0x1B01), i_mov32('rax', 1), i_mov32('rdx', 0), i_wrmsrns(),
                i_mov32('rax', 2), i_wrmsrns()], {}))
    a('# LOCK / 66 -> #UD; CPL3 -> #GP(0)')
    a(run_case([i_wrmsrns(b'\xf0')], {'rcx': 0x1B01, 'rax': 1}))
    a(run_case([i_wrmsrns(b'\x66')], {'rcx': 0x1B01, 'rax': 1}))
    a(run_case([i_wrmsrns()], {'rcx': 0x1B01, 'rax': 1, 'cpl': 3}))

    # ---- RDMSRLIST / WRMSRLIST (U802)
    def q(*v):
        return b''.join((x & M64).to_bytes(8, 'little') for x in v)
    rdl, wrl = i_msrlist(False), i_msrlist(True)
    a('# --- RDMSRLIST (F2 0F 01 C6) / WRMSRLIST (F3 0F 01 C6), U802: RSI = MSR-address table, RDI =')
    a('# data table, RCX = entry mask (bit n cleared when entry n is done); IA32_BARRIER (2FH) reads 0')
    a(run_case([wrl, i_mov32('rcx', 0xB), i_lea_rdi(0x100), rdl],
               {'rcx': 0xB, 'm+0x8000': q(0xC0000102, 0x1B01, 0xFFFFFFFF00000000, 0xE1),
                'm+0x9000': q(0x00007FFF12345678, 1, 0xDEAD, 0x100)}))
    a(run_case([rdl], {'rcx': 1, 'm+0x8000': q(0x2F), 'm+0x9000': q(0x1111111111111111)}))
    a(run_case([wrl, i_lea_rdi(0x200), i_mov32('rcx', 0x20), rdl],
               {'rcx': (1 << 63) | (1 << 5), 'm+0x8028': q(0xE1), 'm+0x81F8': q(0x1B01),
                'm+0x9028': q(0x00000FFC), 'm+0x91F8': q(1)}))
    a(run_case([i_msrlist(False, b'\x66'), i_msrlist(True, b'\x48')],
               {'rcx': 0, 'rsi': MEM_PTR + 0x40, 'rdi': MEM_DST + 0x40}))
    a(run_case([i_msrlist(True, b'\x66')], {'rcx': 2, 'm+0x8008': q(0x1B01), 'm+0x9008': q(1)}))
    a('# an entry with bits 63:32 set / a value WRMSR refuses / IA32_BARRIER written: #GP(0) at that')
    a('# entry, the entries before it completed (RCX bits cleared), RAX/RDX unchanged')
    a(run_case([wrl], {'rcx': 3, 'rax': 0xAAAA, 'rdx': 0xDDDD,
                       'm+0x8000': q(0x1B01, 0x1000000E1), 'm+0x9000': q(1, 0)}))
    a(run_case([wrl], {'rcx': 3, 'm+0x8000': q(0x1B01, 0x2F), 'm+0x9000': q(1, 0)}))
    a(run_case([wrl], {'rcx': 5, 'm+0x8000': q(0xE1, 0, 0xD93), 'm+0x9000': q(0x100, 0, 0x100000)}))
    a(run_case([wrl], {'rcx': 1, 'm+0x8000': q(0x1B01), 'm+0x9000': q(2)}))
    a(run_case([rdl], {'rcx': 3, 'm+0x8000': q(0x2F, 0x100000002F), 'm+0x9000': q(5, 5)}))
    a('# RSI / RDI not 8-byte aligned: #GP(0) before any entry (also with RCX = 0); RCX = 0: nothing')
    a(run_case([rdl], {'rcx': 1, 'rsi': MEM_PTR + 4, 'm+0x8004': q(0x2F)}))
    a(run_case([wrl], {'rcx': 1, 'rdi': MEM_DST + 1, 'm+0x8000': q(0x1B01)}))
    a(run_case([rdl], {'rcx': 0, 'rsi': MEM_PTR + 2}))
    a(run_case([rdl], {'rcx': 0, 'rsi': MEM + 0x20000, 'rdi': MEM + 0x30000}))
    a('# #PF on a table access (MEM + 10000h is unmapped): partial completion')
    a(run_case([rdl], {'rcx': 3, 'rsi': MEM + 0xFFF8, 'm+0xFFF8': q(0x2F), 'm+0x9000': q(7)}))
    a(run_case([rdl], {'rcx': 3, 'rdi': MEM + 0xFFF8, 'm+0x8000': q(0x2F, 0x2F), 'm+0xFFF8': q(7)}))
    a(run_case([wrl], {'rcx': 2, 'rdi': MEM + 0xFFF8, 'm+0x8008': q(0x1B01)}))
    a('# LOCK -> #UD; CPL3 -> #GP(0)')
    a(run_case([i_msrlist(False, b'\xf0')], {'rcx': 1, 'm+0x8000': q(0x2F)}))
    a(run_case([i_msrlist(True, b'\xf0')], {'rcx': 1, 'm+0x8000': q(0x1B01)}))
    a(run_case([rdl], {'rcx': 1, 'm+0x8000': q(0x2F), 'cpl': 3}))
    a(run_case([wrl], {'rcx': 1, 'm+0x8000': q(0x1B01), 'cpl': 3}))

    # ---- MSR-IMM, VEX map 7 (U803)
    a('# --- RDMSR r64, imm32 (VEX.128.F2.MAP7.W0 F6 /0) / WRMSRNS imm32, r64 (VEX.128.F3.MAP7.W0 F6 /0),')
    a('# U803: the full 64-bit register, RAX/RCX/RDX untouched; IA32_BARRIER reads 0')
    base = {'rax': 0x1111111111111111, 'rcx': 0x2222222222222222, 'rdx': 0x3333333333333333}
    a(run_case([i_msrimm(True, 'r9', 0xC0000102), i_msrimm(False, 'r10', 0xC0000102)],
               dict(base, r9=0xFFFF800012345678, r10=0x5555)))
    a(run_case([i_msrimm(True, 'rbx', 0xE1), i_msrimm(False, 'rdx', 0xE1)], dict(base, rbx=0xFFFFFFFD)))
    a(run_case([i_msrimm(True, 'r15', 0x1B01), i_msrimm(False, 'rax', 0x1B01)], dict(base, r15=1)))
    a(run_case([i_msrimm(False, 'r8', 0x2F)], dict(base, r8=0x8888)))
    a(run_case([i_msrimm(False, 'rcx', 0x2F)], dict(base)))
    a('# reserved MSR bits / IA32_BARRIER write: #GP(0), nothing changes')
    a(run_case([i_msrimm(True, 'r9', 0xE1)], dict(base, r9=2)))
    a(run_case([i_msrimm(True, 'r9', 0xE1)], dict(base, r9=0x100000000)))
    a(run_case([i_msrimm(True, 'rdx', 0x2F)], dict(base)))
    a(run_case([i_msrimm(True, 'r12', 0x1B01)], dict(base, r12=3)))
    a('# #UD: W1, L1, vvvv != 1111b, ModRM.reg != 0, memory form, NP / 66, LOCK; CPL3 -> #GP(0)')
    for kw in ({'w': 1}, {'l': 1}, {'vvvv': 1}, {'regfield': 1}, {'mod': 0}, {'pp': 0}, {'pp': 1},
               {'prefix': b'\xf0'}):
        a(run_case([i_msrimm(False, 'rax', 0x2F, **kw)], dict(base)))
        a(run_case([i_msrimm(True, 'rax', 0x1B01, **kw)], dict(base, rax=0)))
    a(run_case([i_msrimm(False, 'r8', 0x2F)], dict(base, cpl=3)))
    a(run_case([i_msrimm(True, 'r8', 0x1B01)], dict(base, cpl=3)))

    # ---- HRESET (U804)
    a('# --- HRESET imm8 (F3 0F 3A F0 C0 ib), U804: EAX = 0 is a NOP; EAX bits outside')
    a('# IA32_HRESET_ENABLE (17DAH, reset 0; only bit 0 = CPUID.20H.0:EBX may be set) -> #GP(0)')
    a(run_case([i_hreset(0x55)], {'rax': 0xFFFFFFFF00000000}))
    a(run_case([i_hreset(0)], {'rax': 1}))
    a(run_case([i_hreset(0)], {'rax': 0x80000000}))
    hre = [i_mov32('rcx', 0x17DA), i_mov32('rax', 1), i_mov32('rdx', 0), i_wrmsrns()]
    a(run_case(hre + [i_hreset(0xFF), i_mov32('rax', 0), i_rdmsr()], {}))
    a(run_case(hre + [i_hreset(0x01, b'\x66'), i_hreset(2, rex=b'\x41')], {}))
    a(run_case(hre + [i_mov32('rax', 3), i_hreset(0)], {}))
    a(run_case(hre + [i_mov32('rax', 0x80000001), i_hreset(0)], {}))
    a('# IA32_HRESET_ENABLE: bits other than 0 -> #GP(0)')
    a(run_case([i_wrmsrns()], {'rcx': 0x17DA, 'rax': 2}))
    a(run_case([i_wrmsrns()], {'rcx': 0x17DA, 'rax': 1, 'rdx': 1}))
    a('# #UD: ModRM other than C0, NP / F2 (no such instruction; NP F0 = RORX is VEX-only), LOCK;')
    a('# CPL3 -> #GP(0) (also with EAX = 0)')
    for kw in ({'modrm': 0xC1}, {'modrm': 0xC8}, {'modrm': 0x00}, {'mand': b''}, {'mand': b'\xf2'},
               {'prefix': b'\xf0'}):
        a(run_case([i_hreset(0, **kw)], {'rax': 0}))
    a(run_case([i_hreset(0)], {'rax': 0, 'cpl': 3}))

    # ---- LKGS (U805)
    gdt = [0,
           desc(0x12345678, 0xFFFFF, 0x2, 0),           # 08h data RW DPL0, not accessed
           desc(0xFEDCBA98, 0xFFFFF, 0x3, 3),           # 10h data RW DPL3, accessed
           desc(0, 0xFFFFF, 0x8, 0),                    # 18h code execute-only
           desc(0x00ABCDEF, 0x0FFFF, 0xA, 0, g=0),      # 20h code execute/read, not accessed
           desc(0x11111111, 0xFFFFF, 0x3, 0, p=0),      # 28h data, not present
           desc(0x22222222, 0x0FFFF, 0x2, 0, s=0),      # 30h system (LDT)
           desc(0x33333333, 0xFFFFF, 0xF, 0)]           # 38h conforming readable code DPL0
    gdt_img = b''.join(d.to_bytes(8, 'little') for d in gdt)
    gdtr_img = (len(gdt_img) - 1).to_bytes(2, 'little') + (MEM_PTR + 0x100).to_bytes(8, 'little')
    g = {'m+0x8000': gdtr_img, 'm+0x8100': gdt_img}
    rdk = [i_mov32('rcx', 0xC0000102), i_rdmsr()]
    a('# --- LKGS r/m16 (F2 0F 00 /6), U805: GDT at MEM+8100h (LGDT [RSI] first); the descriptor')
    a('# base goes to IA32_KERNEL_GS_BASE (read back with RDMSR), GS.selector is loaded (MOV R8W, GS),')
    a('# the GS base is kept, the accessed bit is set in the descriptor')
    for sel in (0x08, 0x10, 0x13, 0x20):
        a(run_case([i_lgdt(), i_mov32('rcx', sel), i_lkgs('rcx'), i_mov_gs('r8')] + rdk,
                   dict(g, r8=0xAAAAAAAAAAAAAAAA)))
    a(run_case([i_lgdt(), i_lkgs(disp=0x40), i_mov_gs('r8')] + rdk,
               dict(g, **{'m+0x8040': b'\x08\x00'})))
    a(run_case([i_lgdt(), i_mov32('r9', 0xFFFF0020), i_lkgs('r9', prefix=b'\x66'), i_mov_gs('r8')] + rdk,
               dict(g)))
    a('# the GS base (IA32_GS_BASE, C0000101H) is not changed')
    a(run_case([i_lgdt(), i_mov32('rcx', 0xC0000101), i_mov32('rax', 0x55AA0000), i_mov32('rdx', 0),
                i_wrmsr(), i_mov32('rbx', 0x08), i_lkgs('rbx'), i_mov32('rax', 0), i_rdmsr()], dict(g)))
    a('# null selector (0-3): GS.selector := SRC, IA32_KERNEL_GS_BASE := 0, no descriptor access')
    for sel in (0, 3):
        a(run_case([i_lgdt(), i_mov32('rcx', 0x08), i_lkgs('rcx'), i_mov32('rcx', sel), i_lkgs('rcx'),
                    i_mov_gs('r8')] + rdk, dict(g)))
    a('# #GP(selector): beyond the GDT limit, LDT (TI = 1, LDTR null), execute-only code, system')
    a('# descriptor, RPL > DPL (data, and conforming code: LKGS has no conforming exemption);')
    a('# #NP(selector): not present')
    for sel in (0x40, 0xF8, 0x0C, 0x18, 0x30, 0x0B, 0x3B, 0x28):
        a(run_case([i_lgdt(), i_mov32('rcx', sel), i_lkgs('rcx')], dict(g)))
    a('# #UD: CPL3, LOCK, NP / F3 0F 00 /6')
    a(run_case([i_lkgs('rcx')], {'rcx': 0, 'cpl': 3}))
    a(run_case([i_lkgs('rcx', prefix=b'\xf0')], {'rcx': 0}))
    a(run_case([i_lkgs('rcx', mand=b'')], {'rcx': 0}))
    a(run_case([i_lkgs('rcx', mand=b'\xf3')], {'rcx': 0}))
    return lines


def cases_apx():
    """The APX EVEX map 7 forms of MSR-IMM (needs --apx)."""
    lines = []
    a = lines.append
    a('# CPL0 system instructions, the Intel APX EVEX forms (ledger U803-U829): expected values from the')
    a('# independent model Emulator/tools/isa/ref_sysins.py (regenerate with --write, do not edit).')
    a('# Unicorn only, MAX model with the APX opt-in:')
    a('#   emu-alltest --cases Emulator\\data\\cases_sysins_apx.txt --apx --expect-only')
    a('# --- RDMSR r64, imm32 / WRMSRNS imm32, r64: EVEX.128.F2/F3.MAP7 F6 11:000:bbb (APX spec 6.49 /')
    a('# 6.70, U803); EGPRs through B4 / B3')
    base = {'rax': 0x1111111111111111, 'rcx': 0x2222222222222222, 'rdx': 0x3333333333333333}
    a(run_case([i_msrimm(True, 'r20', 0xC0000102, True), i_msrimm(False, 'r31', 0xC0000102, True)],
               dict(base, r20=0x00007FFF00ABCDEF, r31=0x5555)))
    a(run_case([i_msrimm(True, 'r9', 0xD93, True), i_msrimm(False, 'r16', 0xD93, True)],
               dict(base, r9=0x80000001)))
    a(run_case([i_msrimm(False, 'r24', 0x2F, True)], dict(base, r24=0x2424)))
    a(run_case([i_msrimm(True, 'r27', 0x1B01, True), i_msrimm(False, 'rbx', 0x1B01, True)],
               dict(base, r27=1)))
    a('# reserved MSR bits / IA32_BARRIER write: #GP(0)')
    a(run_case([i_msrimm(True, 'r17', 0xD93, True)], dict(base, r17=0x40000000)))
    a(run_case([i_msrimm(True, 'r18', 0x2F, True)], dict(base)))
    a('# #UD (class MSR-IMM-EVEX): W1, L1, vvvv != 1111b, V4 = 0, NF, ND (b), z, aaa, U = 0,')
    a('# ModRM.reg != 0, memory form, NP / 66, LOCK; CPL3 -> #GP(0)')
    for kw in ({'w': 1}, {'l': 1}, {'vvvv': 1}, {'vhi': 1}, {'nf': 1}, {'nd': 1}, {'z': 1},
               {'aaa': 1}, {'ubit': 0}, {'regfield': 1}, {'mod': 0}, {'pp': 0}, {'pp': 1},
               {'prefix': b'\xf0'}):
        a(run_case([i_msrimm(False, 'r19', 0x2F, True, **kw)], dict(base)))
        a(run_case([i_msrimm(True, 'r19', 0x1B01, True, **kw)], dict(base)))
    a(run_case([i_msrimm(False, 'r21', 0x2F, True)], dict(base, cpl=3)))
    a(run_case([i_msrimm(True, 'r21', 0x1B01, True)], dict(base, cpl=3)))
    return lines


def cases_hw():
    """Hardware cases (no expectations): the i5-13600K at CPL3 vs Unicorn with its strict profile."""
    lines = []
    a = lines.append
    a('# CPL0 system instructions at CPL3 (ledger U801-U829): the i5-13600K (Windows, CPL3) vs Unicorn')
    a('# with the strict i5-13600K CPUID profile and cpl=3. Generated by Emulator/tools/isa/ref_sysins.py')
    a('# (--write, do not edit):')
    a('#   emu-alltest --cases Emulator\\data\\cases_sysins_hw.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt')
    a('# WRMSRNS (U801): CPUID.(07H,1):EAX[19] = 0 on this CPU -> #UD on both')
    a('.byte 0x0f, 0x01, 0xc6 | rcx=0x1B01 rax=0x1 cpl=3')
    a('.byte 0x48, 0x0f, 0x01, 0xc6 | rcx=0xE1 cpl=3')
    a('# RDMSRLIST / WRMSRLIST (U802): CPUID.(07H,1):EAX[27] = 0 on this CPU -> #UD on both')
    a('.byte 0xf2, 0x0f, 0x01, 0xc6 | rcx=0x1 m+0x8000=2F00000000000000 cpl=3')
    a('.byte 0xf3, 0x0f, 0x01, 0xc6 | rcx=0x1 m+0x8000=2F00000000000000 cpl=3')
    a('# RDMSR / WRMSRNS imm32 (U803): no MSR_IMM (VEX map 7) and no APX (EVEX) on this CPU -> #UD on both')
    for enc in (i_msrimm(False, 'rax', 0x2F)[0], i_msrimm(True, 'r9', 0x1B01)[0],
                i_msrimm(False, 'rax', 0x2F, True)[0], i_msrimm(True, 'r9', 0x1B01, True)[0]):
        a('.byte %s | r9=0x1 cpl=3' % bytelist(enc))
    a('# HRESET (U804): CPUID.(07H,1):EAX[22] = 0 in the profile, so the SDM gives #UD; the i5-13600K')
    a('# decodes it and raises #GP(0) at CPL3 (docs/quirks.md "HRESET without CPUID"). Other ModRM')
    a('# bytes are #UD on both.')
    a('.byte %s | rax=0x0 cpl=3 # known deviation: HRESET without CPUID' % bytelist(i_hreset(0)[0]))
    a('.byte %s | rax=0x1 cpl=3 # known deviation: HRESET without CPUID' % bytelist(i_hreset(0)[0]))
    a('.byte %s | rax=0x0 cpl=3' % bytelist(i_hreset(0, modrm=0xC1)[0]))
    a('# LKGS (U805): CPUID.(07H,1):EAX[18] = 0 on this CPU (and CPL3) -> #UD on both')
    a('.byte %s | rcx=0x2B cpl=3' % bytelist(i_lkgs('rcx')[0]))
    a('.byte %s | m+0x8040=2B00 cpl=3' % bytelist(i_lkgs(disp=0x40)[0]))
    return lines


# ------------------------------------------------------------------------------------------------
def write_file(name, lines):
    path = os.path.join(DATA, name)
    with open(path, 'w', newline='\r\n') as f:
        for l in lines:
            f.write(l + '\n')
    n = sum(1 for l in lines if l and not l.startswith('#'))
    print('%d cases written to %s' % (n, path))


def selftest():
    ok = True

    def chk(name, got, exp):
        nonlocal ok
        if got != exp:
            ok = False
            print('FAIL %s: got %r expected %r' % (name, got, exp))

    # EVEX bytes against the encodings of the emu-alltest sweep (alltest.csv) / asmjit db:
    # vpmovm2d zmm0, k2 = 62 F2 7E 48 38 C2; vpmovq2m k0, xmm2 = 62 F2 FE 08 39 C2
    chk('evex vpmovm2d', evex(0x38, 3, 0, 2, 0, 2, 2), bytes.fromhex('62F27E4838C2'))
    chk('evex vpmovq2m', evex(0x39, 3, 0, 2, 1, 0, 2), bytes.fromhex('62F2FE0839C2'))
    chk('evex zmm31', evex(0x38, 3, 31, 0, 0, 2, 2)[1], 0x62)      # R = R' = 1 stored as 0
    # VPMOVD2M on 4 doublewords 80000000h, 7FFFFFFFh, FFFFFFFFh, 0 -> k = 0101b
    src = bytearray(64)
    for j, v in enumerate((0x80000000, 0x7FFFFFFF, 0xFFFFFFFF, 0)):
        src[4 * j:4 * j + 4] = v.to_bytes(4, 'little')
    src[16:] = b'\xff' * 48                     # beyond VL128: ignored
    chk('d2m 128', vpmov2m(src, 32, 128), 0b0101)
    chk('d2m 512', vpmov2m(src, 32, 512), 0xFFF5)
    chk('q2m 128', vpmov2m(src, 64, 128), 0)       # qwords 7FFFFFFF80000000h, FFFFFFFFh
    chk('q2m 256', vpmov2m(src, 64, 256), 0b1100)
    chk('m2d 128', vpmovm2(0b1001, 32, 128)[:16], bytes.fromhex('FFFFFFFF' '00000000' '00000000' 'FFFFFFFF'))
    chk('m2d 128 upper', vpmovm2(0xFFFFFFFF, 32, 128)[16:], bytes(48))
    chk('m2q 512', vpmovm2(0x81, 64, 512), bytearray(b'\xff' * 8 + bytes(48) + b'\xff' * 8))
    # system-instruction model (worked by hand)
    chk('canon57', (canonical(0x00FFFFFFFFFFFFFF), canonical(0x0100000000000000),
                    canonical(0xFF00000000000000)), (True, False, True))
    chk('umwait', (msr_check(0xE1, 0xFFFFFFFD), msr_check(0xE1, 2), msr_check(0xE1, 1 << 32)),
        (True, False, False))
    chk('mov32 r9', i_mov32('r9', 0x11223344)[0], bytes.fromhex('41B944332211'))
    chk('wrmsrns line', run_case([i_wrmsrns()], {'rcx': 0x1B01, 'rax': 1}),
        '.byte 0x0f, 0x01, 0xc6 | rcx=0x1B01 rax=0x1 =>')
    chk('wrmsrns cpl3', run_case([i_wrmsrns()], {'rcx': 0x1B01, 'rax': 1, 'cpl': 3}),
        '.byte 0x0f, 0x01, 0xc6 | rcx=0x1B01 rax=0x1 cpl=3 => #GP')
    chk('wrmsrns rdmsr', run_case([i_wrmsrns(), i_mov32('rax', 0), i_rdmsr()],
                                  {'rcx': 0xC0000102, 'rax': 0xFFFFFFFF00001000, 'rdx': 0x7F}),
        '.byte 0x0f, 0x01, 0xc6, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x32 | rcx=0xC0000102 '
        'rax=0xFFFFFFFF00001000 rdx=0x7F => rax=0x1000')
    # RDMSRLIST: entry 0 = IA32_BARRIER -> 0 stored at RDI; entry 1 (bit 63:32 set) #GP, RCX = 2
    chk('rdmsrlist partial', run_case([i_msrlist(False)], {'rcx': 3, 'm+0x8000':
        bytes.fromhex('2F00000000000000' '2F00000001000000'), 'm+0x9000': b'\x05'}),
        '.byte 0xf2, 0x0f, 0x01, 0xc6 | rcx=0x3 m+0x8000=2F000000000000002F00000001000000 '
        'm+0x9000=05 => rcx=0x2 m+0x9000=00 #GP')
    chk('wrmsrlist rex', i_msrlist(True, b'\x48')[0], bytes.fromhex('F3480F01C6'))
    # MSR-IMM encodings: URDMSR's EVEX map 7 example (unit test test_x86_ax4_user_msr) with F6:
    # 62 FF 7F 08 F8 C6 = urdmsr r22, imm -> rdmsr r22, imm = 62 FF 7F 08 F6 C6
    chk('evex rdmsr r22', i_msrimm(False, 'r22', 0x1C, True)[0], bytes.fromhex('62FF7F08F6C61C000000'))
    chk('vex rdmsr rax', i_msrimm(False, 'rax', 0x2F)[0], bytes.fromhex('C4E77BF6C02F000000'))
    chk('vex wrmsrns r9', i_msrimm(True, 'r9', 0x1B01)[0], bytes.fromhex('C4C77AF6C1011B0000'))
    chk('rdmsr imm line', run_case([i_msrimm(False, 'rcx', 0x2F)], {'rcx': 7}),
        '.byte 0xc4, 0xe7, 0x7b, 0xf6, 0xc1, 0x2f, 0x00, 0x00, 0x00 | rcx=0x7 => rcx=0x0')
    try:
        cases_apx()
        cases_sys()
        cases_dq()
    except Exception as e:                      # pragma: no cover
        ok = False
        print('FAIL case generation: %s' % e)
    return ok


def main():
    if '--selftest' in sys.argv:
        ok = selftest()
        print('selftest %s' % ('passed' if ok else 'FAILED'))
        sys.exit(0 if ok else 1)
    if '--write' in sys.argv:
        write_file('cases_sysins_dq.txt', cases_dq())
        write_file('cases_sysins.txt', cases_sys())
        write_file('cases_sysins_apx.txt', cases_apx())
        write_file('cases_sysins_hw.txt', cases_hw())
        return
    print(__doc__)
    sys.exit(2)


if __name__ == '__main__':
    main()
