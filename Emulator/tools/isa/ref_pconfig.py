r"""Independent reference model + case generator for PCONFIG and the TME / TME-MK MSRs (ledger
U1020-U1039, agent "pconfig"):

  * expected-value cases, MAX model, CPL0 (Unicorn only)           -> Emulator\data\cases_pconfig.txt
  * CPL3 cases against the i5-13600K with its strict CPUID profile -> Emulator\data\cases_pconfig_hw.txt

Written from the Intel documents only (the emulator's C sources were not read for the expected values;
no CPU measurements - the i5-13600K does not report PCONFIG or TME_EN):
  * SDM 325462-092 Vol2B PCONFIG (NP 0F 01 C5; Operation: #UD if CPUID.07H.00H:EDX.PCONFIG[18] = 0 or
    CPL > 0; #GP(0) if EAX > 2; leaf 0 MKTME_KEY_PROGRAM: #GP(0) if CPUID.1BH does not enumerate
    target 1, if IA32_TME_ACTIVATE[0] = 0 or [1] = 0 or [35:32] = 0, if DS:RBX is not 256-byte
    aligned; the 192-byte MKTME_KEY_PROGRAM_STRUCT is then loaded; #GP(0) for KEYID_CTRL bits 31:24,
    COMMAND > 3, KEYID = 0 or > MK_TME_MAX_KEYS, KEYID[15:k] != 0, KEYID[k-1:k-p] != 0 outside SEAM,
    ENC_ALG not exactly one bit or not in IA32_TME_ACTIVATE[63:48]; success RAX := 0, ZF := 0,
    CF/PF/AF/OF/SF := 0; leaves 1 / 2 (TSE) #GP(0) without the TSE target; "64-Bit Mode Exceptions":
    #GP(0) for a non-canonical memory operand, #PF; #UD with LOCK); Table 4-16 (KEYID 0..1,
    KEYID_CTRL 2..5, bytes 6..63 ignored, KEY_FIELD_1 64..127, KEY_FIELD_2 128..191) and the NOTES
    ("bytes 63:6 ... PCONFIG ignores those bytes", key-field upper bytes ignored, the former EAX
    error codes 1/3/4 are #GP now).
  * SDM 092 Vol1 Table 21-64 / 21-65 (CPUID.1BH: sub-leaf type in EAX[11:0], 1 = target identifiers in
    EBX/ECX/EDX; type 0 = invalid, all later sub-leaves invalid; target 1 = TME-MK); ISE 319433-062
    (target 2 = TSE).
  * Intel Architecture Memory Encryption Technologies Specification 336907-007 rev. 1.7 (and -005
    rev. 1.5, identical in these points except rev. 1.6's integrity rule) 4.1.3 Table 4-1
    (IA32_TME_CAPABILITY), 4.2.1 Table 4-2 (IA32_TME_ACTIVATE), 4.2.2 Table 4-3 (WRMSR response),
    4.2.4 Table 4-5 (IA32_MKTME_KEYID_PARTITIONING), 4.2.5 Tables 4-6 / 4-7 (exclusion range);
    SDM 092 Vol4 Table 2-2 (981H-984H, 87H: NUM_TDX_PRIV_KEYIDS "supported on all parts that
    enumerate support for SEAM mode").

Modelling decisions (the model CPU, documented in the emulator as U1020 / U1021):
  a. IA32_TME_CAPABILITY = AES-XTS-128, AES-XTS-128 + integrity, AES-XTS-256, AES-XTS-256 + integrity
     (bits 3:0), bypass supported (31), MK_TME_MAX_KEYID_BITS = 6, MK_TME_MAX_KEYS = 63.
  g. A write to a locked IA32_TME_ACTIVATE is ignored: SDM Vol4 Table 2-2 982H ("Any write to the
     following MSRs will be ignored after they are locked") against MKTME spec Table 4-3 ("WRMSR while
     lock status = 1. #GP(0)"); the SDM wins. IA32_TME_EXCLUDE_MASK / _BASE keep #GP(0) when locked
     (SDM Vol4 983H / 984H say so explicitly).
  b. No standby storage: key select = 1 takes the "zero key restored" row (not enabled, x..x100b,
     not committed with MK_TME_KEYID_BITS > 0). The TME key RNG never fails (seeded RDRAND source).
  c. No SEAM: NUM_TDX_PRIV_KEYIDS = 0; "not in SEAM" is always true for PCONFIG.
  d. TMEEMASK contiguous = its set bits run from MAXPHYADDR-1 down without a gap (0 allowed);
     MAXPHYADDR = 40 (the MAX model's CPUID.80000008H:EAX[7:0]).
  e. A faulting instruction changes nothing (the harness saves the state at the faulting instruction).
  f. The key table is not architecturally visible: the cases check RAX / RFLAGS / faults; the unit test
     test_x86_pc_pconfig reads the table through UC_CTL_X86_MKTME_KEY.

Usage:
  python -I ref_pconfig.py --selftest   hand-derived checks of the model (exit 0 on pass)
  python -I ref_pconfig.py --write      regenerate the case files listed above (CRLF)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, '..', '..', 'data'))

REGS = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi',
        'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15']
MEM = 0x30020000                    # emu-alltest operand memory (64 KiB), RSI = MEM + 0x8000
MEM_PTR = MEM + 0x8000
MEM_DST = MEM + 0x9000
M64 = (1 << 64) - 1
MAXPHYADDR = 40

CAP = 0xF | (1 << 31) | (6 << 32) | (63 << 36)
MAX_KEYID_BITS = 6
MAX_KEYS = 63

PCONFIG = b'\x0f\x01\xc5'


def hexbytes(b):
    return bytes(b).hex().upper()


def bytelist(b):
    return ', '.join('0x%02x' % x for x in b)


class Fault(Exception):
    pass


def bits(v, hi, lo):
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)


# ------------------------------------------------------------------------------------------------
# MSRs (MKTME spec 336907-007 4.1-4.2, SDM Vol4)
# ------------------------------------------------------------------------------------------------
def activate_write(m, val):
    """IA32_TME_ACTIVATE WRMSR, Table 4-3. Raises Fault('#GP') or updates m.act."""
    if m.act & 1:                                           # locked: SDM Vol4 "ignored" (the
        return                                              # MKTME spec says #GP; the SDM wins)
    # reserved: 30:8, 47:40, 63:52 (MK_TME_CRYPTO_ALGS bits 63:52 "Reserved (#GP)")
    if bits(val, 30, 8) or bits(val, 47, 40) or bits(val, 63, 52):
        raise Fault('#GP')
    pol = bits(val, 7, 4)
    if not (CAP >> pol) & 1 or pol > 3:                     # not enumerated in the capability
        raise Fault('#GP')
    if pol in (1, 3):                                       # integrity algorithms (rev. 1.6)
        raise Fault('#GP')
    k, p = bits(val, 35, 32), bits(val, 39, 36)
    if k > MAX_KEYID_BITS:
        raise Fault('#GP')
    if k > 0 and not (val >> 1) & 1:
        raise Fault('#GP')
    if p > k:
        raise Fault('#GP')
    enable, keysel = (val >> 1) & 1, (val >> 2) & 1
    if not enable:
        m.act = (val & ~3) | 1                              # TME disabled, locked: x..x01b
    elif not keysel:
        m.act = (val & ~3) | 3                              # new key, RNG success: x..x011b
    elif k == 0:
        m.act = val & ~3                                    # zero key restored: x..x100b
    # else: MK_TME_KEYID_BITS > 0 and TME not activated: write not committed


def pa_field():
    return ((1 << MAXPHYADDR) - 1) & ~0xFFF


def wrmsr(m, msr, val):
    if msr == 0x981 or msr == 0x87:                         # read-only
        raise Fault('#GP')
    if msr == 0x982:
        activate_write(m, val)
    elif msr == 0x983:
        if m.act & 1 or val & ~(pa_field() | 0x800) & M64:
            raise Fault('#GP')
        f = val & pa_field()
        if f:
            low = f & -f
            if f != pa_field() & ~(low - 1):                # not one run up to MAXPHYADDR-1
                raise Fault('#GP')
        m.exmask = val
    elif msr == 0x984:
        if m.act & 1 or val & ~pa_field() & M64:
            raise Fault('#GP')
        m.exbase = val
    else:
        raise ValueError('MSR %X not in the model' % msr)


def rdmsr(m, msr):
    if msr == 0x981:
        return CAP
    if msr == 0x982:
        return m.act
    if msr == 0x983:
        return m.exmask
    if msr == 0x984:
        return m.exbase
    if msr == 0x87:
        k, p = bits(m.act, 35, 32), bits(m.act, 39, 36)
        if not m.act & 1 or k == 0:
            return 0
        return min((1 << (k - p)) - 1, MAX_KEYS)            # NUM_TDX_PRIV_KEYIDS 0: no SEAM
    raise ValueError('MSR %X not in the model' % msr)


# ------------------------------------------------------------------------------------------------
# Machine and instructions (64-bit mode, CPL0 unless cpl=3)
# ------------------------------------------------------------------------------------------------
class Machine:
    def __init__(self, inputs):
        self.r = {k: 0 for k in REGS}
        self.r['rsi'] = MEM_PTR
        self.r['rdi'] = MEM_DST
        self.r['r14'] = MEM_PTR
        self.rflags = 0x202
        self.cpl = 0
        self.mem = {}
        self.act = self.exmask = self.exbase = 0
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
        return dict(self.r), self.rflags, (self.act, self.exmask, self.exbase)

    def restore(self, s):
        self.r, self.rflags = dict(s[0]), s[1]
        self.act, self.exmask, self.exbase = s[2]


def ld8(m, a):
    if not (MEM <= a < MEM + 0x10000):
        raise Fault('#PF')
    return m.mem.get(a, 0)


def i_wrmsr():
    def f(m):
        if m.cpl:
            raise Fault('#GP')
        wrmsr(m, m.r['rcx'] & 0xFFFFFFFF, (m.r['rax'] & 0xFFFFFFFF) | ((m.r['rdx'] & 0xFFFFFFFF) << 32))
    return b'\x0f\x30', f


def i_rdmsr():
    def f(m):
        if m.cpl:
            raise Fault('#GP')
        v = rdmsr(m, m.r['rcx'] & 0xFFFFFFFF)
        m.r['rax'] = v & 0xFFFFFFFF
        m.r['rdx'] = v >> 32
    return b'\x0f\x32', f


def i_mov32(reg, imm):
    """MOV r32, imm32 (B8+r id): zero-extends to 64 bits; no flags."""
    idx = REGS.index(reg)
    enc = (b'\x41' if idx >= 8 else b'') + bytes([0xB8 + (idx & 7)]) + imm.to_bytes(4, 'little')

    def f(m):
        m.r[reg] = imm
    return enc, f


def i_mov64(reg, imm):
    """MOV r64, imm64 (REX.W B8+r io)."""
    idx = REGS.index(reg)
    enc = bytes([0x48 | (1 if idx >= 8 else 0), 0xB8 + (idx & 7)]) + imm.to_bytes(8, 'little')

    def f(m):
        m.r[reg] = imm
    return enc, f


def i_movreg(dst, src):
    """MOV r64, r64 (REX.W 89 /r, ModRM.mod = 11b: r/m = dst, reg = src)."""
    d, r = REGS.index(dst), REGS.index(src)
    enc = bytes([0x48 | (4 if r >= 8 else 0) | (1 if d >= 8 else 0), 0x89, 0xC0 | ((r & 7) << 3) | (d & 7)])

    def f(m):
        m.r[dst] = m.r[src]
    return enc, f


def i_cpuid_1b():
    """CPUID with EAX = 1BH (Vol1 Table 21-64/21-65): sub-leaf 0 = type 1, EBX = target 1 (TME-MK),
    ECX = target 2 (TSE: the model reports PBNDKB / IA32_TSE_CAPABILITY, U807)."""
    def f(m):
        sub = m.r['rcx'] & 0xFFFFFFFF
        if sub == 0:
            v = (1, 1, 2, 0)                                # targets 1 (TME-MK), 2 (TSE)
        else:
            v = (0, 0, 0, 0)                                # type 0: invalid, and every later one
        m.r['rax'], m.r['rbx'], m.r['rcx'], m.r['rdx'] = v
    return b'\x0f\xa2', f


def canonical48(v):
    top = v >> 47
    return top == 0 or top == (1 << 17) - 1


def i_pconfig(prefix=b''):
    def f(m):
        # #UD: LOCK, and the NP form only (66 / F2 / F3 are not PCONFIG); CPUID; CPL > 0
        if prefix:
            raise Fault('#UD')
        if m.cpl > 0:
            raise Fault('#UD')
        leaf = m.r['rax'] & 0xFFFFFFFF
        if leaf > 2:
            raise Fault('#GP')
        if leaf != 0:                                       # TSE target not enumerated
            raise Fault('#GP')
        act = m.act
        if not act & 1 or not (act >> 1) & 1 or bits(act, 35, 32) == 0:
            raise Fault('#GP')
        addr = m.r['rbx']
        if not canonical48(addr) or addr & 0xFF:
            raise Fault('#GP')
        s = bytes(ld8(m, addr + i) for i in range(192))
        keyid = int.from_bytes(s[0:2], 'little')
        ctrl = int.from_bytes(s[2:6], 'little')
        if ctrl >> 24:
            raise Fault('#GP')
        if ctrl & 0xFF > 3:
            raise Fault('#GP')
        if keyid == 0 or keyid > MAX_KEYS:
            raise Fault('#GP')
        k, p = bits(act, 35, 32), bits(act, 39, 36)
        if keyid >> k:
            raise Fault('#GP')
        if p and bits(keyid, k - 1, k - p):
            raise Fault('#GP')
        alg = bits(ctrl, 23, 8)
        if bin(alg).count('1') != 1 or not alg & bits(act, 63, 48):
            raise Fault('#GP')
        # the lock is always acquired (one logical processor), the RNG never fails (seeded source)
        m.r['rax'] = 0
        m.rflags &= ~(0x1 | 0x4 | 0x10 | 0x40 | 0x80 | 0x800)   # ZF := 0, CF PF AF OF SF := 0
    return prefix + PCONFIG, f


def run_case(ins, inputs, loose=False, expect=None):
    """Run the instruction list on the model; returns the case line."""
    m = Machine(inputs)
    r0, f0 = dict(m.r), m.rflags
    code = b''.join(e for e, _ in ins)
    fault = None
    for _, f in ins:
        saved = m.snapshot()
        try:
            f(m)
        except Fault as e:
            m.restore(saved)
            fault = str(e)
            break
    exp = []
    for k in REGS:
        if (expect is None and m.r[k] != r0[k]) or (expect is not None and k in expect):
            exp.append('%s=0x%X' % (k, m.r[k]))
    if m.rflags != f0 and expect is None:
        exp.append('rflags=0x%X' % m.rflags)
    if fault:
        exp.append(fault + ('(0)' if fault == '#GP' else ''))
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
    line += (' =>! ' if loose else ' => ') + ' '.join(exp)
    return line.rstrip()


def kps(keyid, cmd, alg, ign=0, key1=b'', key2=b'', rsvd=0):
    """MKTME_KEY_PROGRAM_STRUCT (192 bytes, Table 4-16)."""
    s = bytearray(192)
    s[0:2] = keyid.to_bytes(2, 'little')
    s[2:6] = (cmd | (alg << 8) | (rsvd << 24)).to_bytes(4, 'little')
    for i in range(6, 64):
        s[i] = ign
    s[64:64 + len(key1)] = key1
    s[128:128 + len(key2)] = key2
    return bytes(s)


def act_val(enable=1, keysel=0, save=0, pol=0, bypass=0, k=0, p=0, algs=0):
    return enable << 1 | keysel << 2 | save << 3 | pol << 4 | bypass << 31 | k << 32 | p << 36 | algs << 48


# ------------------------------------------------------------------------------------------------
def cases_exp():
    lines = []
    a = lines.append
    a('# PCONFIG (NP 0F 01 C5) and the TME / TME-MK MSRs (ledger U1020-U1022): expected values from the')
    a('# independent model Emulator/tools/isa/ref_pconfig.py (regenerate with --write, do not edit).')
    a('# Unicorn only, MAX model, CPL0 (the i5-13600K reports neither PCONFIG nor TME_EN):')
    a('#   emu-alltest --cases Emulator\\data\\cases_pconfig.txt --expect-only')
    a('# The key table is not software-visible: these cases check RAX, RFLAGS, the MSRs and the faults;')
    a('# the programmed keys are checked by the unit test test_x86_pc_pconfig (UC_CTL_X86_MKTME_KEY).')
    wr, rd = i_wrmsr(), i_rdmsr()

    a('# --- CPUID: (07H,0):ECX.TME_EN[13] and EDX.PCONFIG[18] (masked, loose), leaf 1BH sub-leaves 0..2')
    a('.byte 0x0f, 0xa2, 0x81, 0xe1, 0x00, 0x20, 0x00, 0x00, 0x81, 0xe2, 0x00, 0x00, 0x04, 0x00 | rax=0x7 '
      'rcx=0x0 =>! rcx=0x2000 rdx=0x40000')
    for sub in (0, 1, 2, 0xFFFFFFFF):
        a(run_case([i_cpuid_1b()], {'rax': 0x1B, 'rcx': sub}))

    a('# --- IA32_TME_CAPABILITY (981H): read; WRMSR #GP(0) (read-only)')
    a(run_case([i_mov32('rcx', 0x981), rd], {}))
    a(run_case([wr], {'rcx': 0x981, 'rax': 0xF, 'rdx': 0}))
    a('# --- IA32_MKTME_KEYID_PARTITIONING (87H): 0 before activation; WRMSR #GP(0)')
    a(run_case([i_mov32('rcx', 0x87), rd], {'rax': 0x1111, 'rdx': 0x2222}))
    a(run_case([wr], {'rcx': 0x87, 'rax': 0, 'rdx': 0}))

    a('# --- IA32_TME_ACTIVATE (982H) Table 4-3: #GP(0) rows (reserved 30:8 / 47:40 / 63:52, policy not')
    a('# enumerated or with integrity, MK_TME_KEYID_BITS > 6, KeyID bits without enable, TDX bits > k)')
    for v in (1 << 8, 1 << 30, 1 << 40, 1 << 47, 1 << 52, 1 << 63, act_val(pol=1), act_val(pol=3),
              act_val(pol=4), act_val(pol=15), act_val(k=7), act_val(enable=0, k=1),
              act_val(k=2, p=3), act_val(enable=0, pol=1)):
        a(run_case([wr], {'rcx': 0x982, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    a('# successful writes, read back (and IA32_MKTME_KEYID_PARTITIONING): enable = 0 -> x..x01b;')
    a('# enable = 1, key select = 0 -> x..x011b; key select = 1 -> x..x100b (k = 0) / not committed')
    for v in (0, act_val(enable=0, bypass=1, algs=1), act_val(), act_val(pol=2), act_val(bypass=1),
              act_val(k=6, algs=0xF), act_val(pol=2, k=6, p=2, algs=5), act_val(k=1, p=1, algs=2),
              act_val(k=4, p=0, algs=4, save=1), act_val(keysel=1), act_val(keysel=1, pol=2, save=1),
              act_val(keysel=1, k=3, algs=1)):
        a(run_case([wr, rd, i_movreg('r8', 'rax'), i_movreg('r9', 'rdx'), i_mov32('rcx', 0x87), rd],
                   {'rcx': 0x982, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    a('# locked: a second WRMSR is ignored (SDM Vol4 982H "Any write ... will be ignored after they are locked";')
    a('# the MKTME spec Table 4-3 says #GP(0) - docs/quirks.md "Specification conflicts"); unlocked after')
    a('# x..x100b: a second write works')
    v = act_val(k=6, algs=1)
    a(run_case([wr, wr], {'rcx': 0x982, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    v = act_val(enable=0)
    a(run_case([wr, wr], {'rcx': 0x982, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    v, v2 = act_val(k=6, algs=1), act_val(enable=0)
    a(run_case([wr, i_mov32('rax', v2 & 0xFFFFFFFF), i_mov32('rdx', 0x80000000), wr, rd],
               {'rcx': 0x982, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    v, v2 = act_val(keysel=1), act_val(k=2, algs=1)
    a(run_case([wr, i_mov32('rax', v2 & 0xFFFFFFFF), i_mov32('rdx', v2 >> 32), wr, rd],
               {'rcx': 0x982, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))

    a('# --- IA32_TME_EXCLUDE_MASK / _BASE (983H / 984H), MAXPHYADDR 40: reserved bits, contiguous')
    a('# TMEEMASK, #GP(0) once IA32_TME_ACTIVATE is locked')
    for v in (0xFFFFF00800, 0x800, 0, 0x8000000000, 0xFFFFFFF800, 0xFFFFFFF000):
        a(run_case([wr, rd], {'rcx': 0x983, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    for v in (0xFFFFF00801, 0xFFFFF00C00, 0x1FFFFF00800, 0xFF0F000800, 0x7F00000800, 0x1000):
        a(run_case([wr], {'rcx': 0x983, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    for v in (0x7654321000, 0xFFFFFFF000, 0):
        a(run_case([wr, rd], {'rcx': 0x984, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    for v in (0x800, 0x1, 0x10000000000, 1 << 63):
        a(run_case([wr], {'rcx': 0x984, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    v = act_val(k=2, algs=1)
    a(run_case([wr, i_mov32('rcx', 0x983), i_mov32('rax', 0), i_mov32('rdx', 0), wr],
               {'rcx': 0x982, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))
    a(run_case([wr, i_mov32('rcx', 0x984), i_mov32('rax', 0), i_mov32('rdx', 0), wr],
               {'rcx': 0x982, 'rax': v & 0xFFFFFFFF, 'rdx': v >> 32}))

    # ---- PCONFIG
    def pc(actv, struct, leaf=0, pre=b'', rbx=MEM_PTR, extra=None, cpl=0, flags=0xED7, mov=None):
        ins = [i_wrmsr()] if actv is not None else []
        ins.append(mov if mov is not None else i_mov32('rax', leaf))
        ins.append(i_pconfig(pre))
        inp = {}
        if actv is not None:
            inp.update({'rcx': 0x982, 'rax': actv & 0xFFFFFFFF, 'rdx': actv >> 32})
        inp.update({'rbx': rbx, 'rflags': flags})
        if struct is not None:
            inp['m+0x8000'] = struct
        if cpl:
            inp['cpl'] = cpl
        if extra:
            inp.update(extra)
        return run_case(ins, inp)

    A6 = act_val(pol=2, k=6, algs=0xF)                      # 63 KeyIDs, all four MK algorithms
    A6T = act_val(k=6, p=2, algs=5)                         # 15 TME-MK KeyIDs (2 TDX bits)
    A4 = act_val(k=4, algs=1)                               # 15 KeyIDs, AES-XTS-128 only
    key1 = bytes((i * 7 + 1) & 0xFF for i in range(64))
    key2 = bytes((i * 11 + 3) & 0xFF for i in range(64))
    a('# --- PCONFIG leaf 0 MKTME_KEY_PROGRAM: RBX = MEM+8000h (256-byte aligned); WRMSR activates first.')
    a('# Success: RAX = 0, ZF = 0, CF/PF/AF/OF/SF = 0 (input RFLAGS ED7h: DF and IF kept) -> 602h')
    for actv, kid, cmd, alg in ((A6, 1, 0, 4), (A6, 63, 0, 1), (A6, 17, 1, 2), (A6, 2, 1, 8),
                                (A6, 3, 2, 1), (A6, 4, 3, 4), (A6T, 15, 0, 4), (A6T, 1, 1, 1),
                                (A4, 15, 0, 1), (A4, 8, 2, 1), (A4, 1, 3, 1)):
        a(pc(actv, kps(kid, cmd, alg, key1=key1, key2=key2)))
    a('# bytes 63:6 and the key-field bytes beyond the algorithm\'s key size are ignored (SDM NOTES)')
    a(pc(A6, kps(5, 0, 1, ign=0xFF, key1=b'\xff' * 64, key2=b'\xff' * 64)))
    a(pc(A6, kps(6, 1, 4, ign=0xA5, key1=b'\x5a' * 64)))
    a('# RAX[63:32] ignored (EAX selects the leaf), RAX := 0')
    a(pc(A6, kps(7, 0, 4), mov=i_mov64('rax', 0xFFFFFFFF00000000)))
    a('# #GP(0): EAX > 2; EAX = 1 / 2 (TSE leaves: CPUID.1BH enumerates no TSE target)')
    for leaf in (3, 0x80000000, 0xFFFFFFFF, 1, 2):
        a(pc(A6, kps(1, 0, 4), leaf=leaf))
    a('# #GP(0): IA32_TME_ACTIVATE not locked / locked with TME disabled / no TME-MK KeyID bits /')
    a('# key select 1 (not committed, not locked) - before the structure is read (RBX unmapped)')
    for actv in (None, act_val(enable=0), act_val(), act_val(keysel=1, k=6, algs=1)):
        a(pc(actv, kps(1, 0, 1)))
        a(pc(actv, None, rbx=MEM + 0x10000))
    a('# #GP(0): RBX not 256-byte aligned (checked before the read), not canonical')
    for rbx in (MEM_PTR + 0x40, MEM_PTR + 1, MEM_PTR + 0x80, MEM + 0x10040, 0x0000800000000000,
                0xFFFF7FFFFFFFFF00):
        a(pc(A6, None, rbx=rbx))
    a('# #PF: the structure on the unmapped page (RBX = MEM+10000h); at MEM+FF00h the 192 bytes end')
    a('# before that page: success')
    a(pc(A6, None, rbx=MEM + 0x10000))
    a(pc(A6, None, rbx=MEM + 0x10100))
    a(pc(A6, None, rbx=MEM + 0xFF00, extra={'m+0xFF00': kps(1, 0, 4)}))
    a('# #GP(0): KEYID_CTRL bits 31:24, COMMAND > 3, KEYID 0 / > MK_TME_MAX_KEYS / beyond k bits /')
    a('# in the TDX bits, ENC_ALG 0 / two bits / not activated / bits 15:4 (also for CLEAR / NO_ENCRYPT)')
    for actv, s in ((A6, kps(1, 0, 4, rsvd=1)), (A6, kps(1, 0, 4, rsvd=0x80)), (A6, kps(1, 4, 4)),
                    (A6, kps(1, 0xFF, 4)), (A6, kps(0, 0, 4)), (A6, kps(0, 2, 4)), (A6, kps(64, 0, 4)),
                    (A6, kps(0xFFFF, 3, 4)), (A4, kps(16, 0, 1)), (A4, kps(0x8001, 0, 1)),
                    (A6T, kps(16, 0, 4)), (A6T, kps(48, 1, 1)), (A6T, kps(63, 3, 1)), (A6, kps(1, 0, 0)),
                    (A6, kps(1, 0, 5)), (A6, kps(1, 0, 0x10)), (A6, kps(1, 0, 0x8000)),
                    (A6T, kps(1, 0, 2)), (A6T, kps(1, 0, 8)), (A4, kps(1, 0, 4)), (A4, kps(1, 2, 0)),
                    (A4, kps(1, 3, 2)), (A6, kps(1, 2, 3))):
        a(pc(actv, s))
    a('# #UD: LOCK, 66, F2, F3 prefixes; CPL3 (before EAX or the MSR state is looked at)')
    for pre in (b'\xf0', b'\x66', b'\xf2', b'\xf3'):
        a(pc(A6, kps(1, 0, 4), pre=pre))
    a(run_case([i_mov32('rax', 0), i_pconfig()], {'rbx': MEM_PTR, 'cpl': 3}))
    a(run_case([i_mov32('rax', 7), i_pconfig()], {'rbx': MEM_PTR + 1, 'cpl': 3}))
    a('# two programmings in a row (the key table is per KeyID; RAX = 0 each time)')
    a(run_case([i_wrmsr(), i_mov32('rax', 0), i_pconfig(), i_mov32('rax', 0), i_pconfig()],
               {'rcx': 0x982, 'rax': A6 & 0xFFFFFFFF, 'rdx': A6 >> 32, 'rbx': MEM_PTR,
                'm+0x8000': kps(9, 1, 4)}))
    return lines


def cases_hw():
    lines = []
    a = lines.append
    a('# PCONFIG and the TME MSRs at CPL3 (ledger U1020-U1022): the i5-13600K (Windows, CPL3) vs Unicorn')
    a('# with the strict i5-13600K CPUID profile and cpl=3. Generated by Emulator/tools/isa/ref_pconfig.py')
    a('# (--write, do not edit):')
    a('#   emu-alltest --cases Emulator\\data\\cases_pconfig_hw.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt')
    a('# PCONFIG: CPUID.(07H,0):EDX.PCONFIG[18] = 0 on this CPU (and CPL3) -> #UD on both; the hardware')
    a('# cannot tell which of the two #UD conditions it took')
    a('.byte 0x0f, 0x01, 0xc5 | rax=0x0 rbx=0x%X cpl=3' % MEM_PTR)
    a('.byte 0x0f, 0x01, 0xc5 | rax=0x3 rbx=0x%X cpl=3' % (MEM_PTR + 1))
    a('.byte 0xf0, 0x0f, 0x01, 0xc5 | rax=0x0 rbx=0x%X cpl=3' % MEM_PTR)
    a('# RDMSR / WRMSR of the TME MSRs at CPL3: #GP(0) on both (CPL is checked first; TME_EN = 0 here)')
    for msr in (0x981, 0x982, 0x983, 0x984, 0x87):
        a('.byte 0x0f, 0x32 | rcx=0x%X cpl=3' % msr)
    a('.byte 0x0f, 0x30 | rcx=0x982 rax=0x2 rdx=0x0 cpl=3')
    a('# CPUID leaf 1BH: all sub-leaves 0 on this CPU (the profile is its capture)')
    a('.byte 0x0f, 0xa2 | rax=0x1B rcx=0x0 cpl=3')
    a('.byte 0x0f, 0xa2 | rax=0x1B rcx=0x1 cpl=3')
    return lines


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

    def wr_result(val, pre=0):
        m = Machine({})
        m.act = pre
        try:
            activate_write(m, val)
        except Fault as e:
            return str(e)
        return m.act

    # Table 4-1 layout of the capability: 3F68000000Fh
    chk('cap', CAP, 0x3F68000000F)
    # Table 4-3 rows
    chk('locked: ignored', wr_result(2 | (1 << 63), pre=3), 3)
    chk('reserved 8', wr_result(0x100), '#GP')
    chk('reserved 52', wr_result(1 << 52), '#GP')
    chk('policy 1 integrity', wr_result(0x12), '#GP')
    chk('policy 2', wr_result(0x22), 0x23)
    chk('enable 0', wr_result(0), 1)
    chk('enable 0 bypass', wr_result(0x80000000), 0x80000001)
    chk('new key', wr_result(2), 3)
    chk('restore no key', wr_result(6), 4)
    chk('restore no key, k > 0', wr_result(6 | (1 << 32)), 0)
    chk('k > max', wr_result(2 | (7 << 32)), '#GP')
    chk('k without enable', wr_result(1 << 32), '#GP')
    chk('p > k', wr_result(2 | (1 << 32) | (2 << 36)), '#GP')
    chk('p = k', wr_result(2 | (3 << 32) | (3 << 36)), 3 | (3 << 32) | (3 << 36))
    # partitioning: k = 6, p = 2 -> 15; k = 6, p = 0 -> 63; k = 3, p = 3 -> 0
    for k, p, n in ((6, 2, 15), (6, 0, 63), (3, 3, 0), (1, 0, 1)):
        m = Machine({})
        m.act = 3 | (k << 32) | (p << 36)
        chk('partitioning %d %d' % (k, p), rdmsr(m, 0x87), n)
    # exclusion mask contiguity (MAXPHYADDR 40)
    for v, good in ((0xFFFFF00800, True), (0x8000000800, True), (0xFF0F000800, False),
                    (0x7F00000800, False), (0x800, True), (0x801, False)):
        m = Machine({})
        try:
            wrmsr(m, 0x983, v)
            r = True
        except Fault:
            r = False
        chk('exclude mask %X' % v, r, good)
    # PCONFIG KeyID checks: k = 6, p = 2: KeyIDs 1..15 valid, 16..63 TDX
    for kid, good in ((1, True), (15, True), (16, False), (63, False), (0, False), (64, False)):
        m = Machine({'rbx': MEM_PTR, 'm+0x8000': kps(kid, 0, 4)})
        m.act = 3 | (6 << 32) | (2 << 36) | (5 << 48)
        try:
            i_pconfig()[1](m)
            r = m.r['rax'] == 0
        except Fault:
            r = False
        chk('keyid %d' % kid, r, good)
    # success flags: ED7h -> 602h
    m = Machine({'rbx': MEM_PTR, 'rflags': 0xED7, 'm+0x8000': kps(1, 0, 1)})
    m.act = 3 | (6 << 32) | (1 << 48)
    i_pconfig()[1](m)
    chk('flags', m.rflags, 0x602)
    # encodings
    chk('mov eax', i_mov32('rax', 0x87)[0], bytes.fromhex('B887000000'))
    chk('mov rax imm64', i_mov64('rax', 1)[0], bytes.fromhex('48B80100000000000000'))
    chk('mov r8, rax', i_movreg('r8', 'rax')[0], bytes.fromhex('4989C0'))
    chk('mov r9, rdx', i_movreg('r9', 'rdx')[0], bytes.fromhex('4989D1'))
    try:
        cases_exp()
        cases_hw()
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
        write_file('cases_pconfig.txt', cases_exp())
        write_file('cases_pconfig_hw.txt', cases_hw())
        return
    print(__doc__)
    sys.exit(2)


if __name__ == '__main__':
    main()
