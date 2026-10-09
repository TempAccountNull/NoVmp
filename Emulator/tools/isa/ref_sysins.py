r"""Independent reference model + expected-value case generator (ledger U800-U829, agent "sysins"):

  * AVX512DQ VPMOVD2M / VPMOVQ2M / VPMOVM2D / VPMOVM2Q (U800)       -> Emulator\data\cases_sysins_dq.txt
  * CPL0 system instructions, MAX model, no opt-in                  -> Emulator\data\cases_sysins.txt
      WRMSRNS (U801), RDMSRLIST / WRMSRLIST (U802)

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
        'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15']
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
        return dict(self.r), self.rflags, dict(self.mem), dict(self.msr)

    def restore(self, s):
        self.r, self.rflags, self.mem, self.msr = dict(s[0]), s[1], dict(s[2]), dict(s[3])


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


def run_case(ins, inputs):
    """Run the instruction list on the model; returns the case line."""
    m = Machine(inputs)
    r0, f0, mem0, _ = m.snapshot()
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
    try:
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
        write_file('cases_sysins_hw.txt', cases_hw())
        return
    print(__doc__)
    sys.exit(2)


if __name__ == '__main__':
    main()
