r"""Independent reference model for FRED event delivery, ERETS / ERETU and linear-address masking
(LAM) (ledger U1170-U1199, agent "fredlam", plan 1.J.10, decision A11).

Written from the Intel documents only (the emulator's C sources were not read for the expected
values; the i5-13600K has neither FRED nor LAM and SDE cannot run CPL0 code, so every value here is
"specification-validated"):
  * SDM 325462-092 Vol3A chapter 8 "Flexible Return and Event Delivery (FRED)": 8.2.3 (MSRs), 8.3.1
    (new RIP / stack level / RSP / SSP), 8.3.1.3 (new context), 8.3.2.1 (the 64-byte frame and its
    augmented CS / SS fields, Figure 8-1), 8.3.2.2 (shadow stack), 8.3.3, 8.3.4 (faults during
    delivery), 8.3.5 (pseudocode), 8.4.1 / 8.4.2 (ERETS / ERETU);
  * SDM 092 Vol2A ERETS (3-298..3-300) and ERETU (3-301..3-304) Operation and exception lists;
  * FRED spec 346446-009 (rev. 9.0, June 2025) sections 4-6, 8, Appendix A and B - identical to the
    SDM in every point modelled here;
  * SDM 092 Vol3A 4.4 "Linear-Address Masking" and 4.5.1 (canonicality after masking).

Model state (dict): cpl (0 / 3), cs64 (64-bit mode), rip, rsp, rflags, cs, ss (selectors), gs_base,
kgs_base, star, config (IA32_FRED_CONFIG, CSL = bits 1:0), stklvls, rspN (list of 4), sspN (list of
4: [0] = IA32_PL0_SSP), ssp, pl3_ssp, la57 (CR4.LA57), cpu_la57 (CPUID LA57), sss (supervisor shadow
stacks: CR4.CET and IA32_S_CET.SH_STK_EN), uss (user shadow stacks), s_cet (IA32_S_CET), cet
(CR4.CET), sti (STI blocking in effect).

Usage:
  python -I ref_fred_lam.py --selftest   hand-derived checks of the model (exit 0 on pass)
  python -I ref_fred_lam.py --unit       print the expected values used by the unit tests
  python -I ref_fred_lam.py --write      regenerate Emulator\data\cases_lam.txt (CRLF)
"""
import sys

M64 = (1 << 64) - 1

# event types (SDM 092 Vol3A 8.3.2.1, bits 51:48 of the augmented SS)
EXTINT, NMI, HWEXC, SWINT, PRIVSW, SWEXC, OTHER = 0, 2, 3, 4, 5, 6, 7
SOFT_TYPES = (SWINT, PRIVSW, SWEXC, OTHER)          # the instruction length is saved
EXT_TYPES = (EXTINT, NMI, HWEXC, PRIVSW)            # #GP / #SS during delivery: error code 1
# exceptions that are faults (SDM 092 Vol3A Table 7-1): RF is saved as 1 for them (8.3.2.1)
FAULT_VECTORS = (0, 5, 6, 7, 10, 11, 12, 13, 14, 16, 17, 19, 20, 21)


class Fault(Exception):
    def __init__(self, vector, code=None):
        Exception.__init__(self, vector, code)
        self.vector = vector
        self.code = code


def sext(v, bit):
    """sign-extend v from bit 'bit' (bits 63:bit+1 := bit 'bit')"""
    v &= M64
    if (v >> bit) & 1:
        return v | (M64 & ~((1 << (bit + 1)) - 1))
    return v & ((1 << (bit + 1)) - 1)


def paging_canonical(st, a):
    """Vol3A 4.5: bits 63:47 (4-level) / 63:56 (5-level) identical"""
    b = 56 if st.get('la57') else 47
    return sext(a, b) == (a & M64)


def cpu_canonical(st, a):
    b = 56 if st.get('cpu_la57') else 47
    return sext(a, b) == (a & M64)


# ------------------------------------------------------------------------------------------- LAM
def lam_mask(a, cr3, cr4_lam_sup, la57):
    """SDM 092 Vol3A 4.4.1 / 4.4.2: bit 63 selects user (0) / supervisor (1); LAM48 replaces bits
    62:48 with bit 47, LAM57 bits 62:57 with bit 56; CR3[62] LAM_U48, CR3[61] LAM_U57 (overrides
    U48), CR4.LAM_SUP: LAM57 with 5-level paging, else LAM48"""
    a &= M64
    if a >> 63:
        if not cr4_lam_sup:
            return a
        width = 56 if la57 else 47
    else:
        if (cr3 >> 61) & 1:
            width = 56
        elif (cr3 >> 62) & 1:
            width = 47
        else:
            return a
    return (a & (1 << 63)) | (sext(a, width) & ~(1 << 63) & M64)


def lam_access_ok(a, cr3, cr4_lam_sup, la57):
    """Vol3A 4.5.1: canonicality is checked after masking (paging canonical)"""
    m = lam_mask(a, cr3, cr4_lam_sup, la57)
    return paging_canonical({'la57': la57}, m), m


# ---------------------------------------------------------------------------------- FRED delivery
def event_info(vector, etype, cs64, nested, insn_len):
    """bits 63:32 of the augmented SS (Vol3A 8.3.2.1)"""
    v = (vector & 0xff) | (etype << 16) | ((1 if cs64 else 0) << 25) | ((1 if nested else 0) << 26)
    if etype in SOFT_TYPES:
        v |= (insn_len & 0xf) << 28
    return v


def deliver(st, ev):
    """FRED event delivery (SDM 092 Vol3A 8.3.5). ev: type, vector, error_code, insn_len (soft types),
    nested (exception during FRED delivery), data (event data), rip_next (soft types: the next RIP).
    Returns (new state, frame) where frame[0] is the error code at the new RSP and frame[7] the zero
    word at new RSP + 56; raises Fault(vector, error code) - the state is then unchanged."""
    s = dict(st)
    t = ev['type']
    v = ev['vector']
    nested = ev.get('nested', False)
    ext = 1 if t in EXT_TYPES else 0
    old_cpl = s['cpl']
    old_csl = 0 if old_cpl == 3 else s['config'] & 3
    old_rip = ev['rip_next'] if t in SOFT_TYPES else s['rip']
    rfl = s['rflags']
    if t == HWEXC and (v in FAULT_VECTORS or (v == 1 and ev.get('data', 0) & (1 << 13))):
        rfl |= 1 << 16
    # augmented CS: bits 17:16 old CSL; bit 18 the supervisor IBT tracker (0 for INT n, SYSCALL,
    # SYSENTER and for CPL 3)
    acs = s['cs'] | (old_csl << 16)
    if old_cpl == 0 and t not in (SWINT, OTHER) and s.get('cet') and \
            (s.get('s_cet', 0) & 4) and (s.get('s_cet', 0) & (1 << 11)):
        acs |= 1 << 18
    ass = s['ss']
    if t == HWEXC and s.get('sti'):
        ass |= 1 << 16
    if t in (SWINT, OTHER):
        ass |= 1 << 17
    if t == NMI:
        ass |= 1 << 18
    ass |= event_info(v, t, s['cs64'], nested and not (t == HWEXC and v == 8),
                      ev.get('insn_len', 0)) << 32
    # new RIP
    base = s['config'] & ~0xfff & M64
    new_rip = base if old_cpl == 3 else (base + 256) & M64
    if not paging_canonical(s, new_rip):
        raise Fault(13, ext)
    # stack level
    if old_cpl == 3 and not nested and not (t == HWEXC and v == 8):
        esl = 0
    elif t == EXTINT:
        esl = (s['config'] >> 9) & 3
    elif t in (HWEXC, SWEXC, PRIVSW, NMI):
        esl = (s['stklvls'] >> (2 * v)) & 3
    else:
        esl = 0
    new_csl = max(esl, old_csl)
    if old_cpl == 3 or new_csl > old_csl:
        new_rsp = s['rspN'][new_csl]
    else:
        new_rsp = (s['rsp'] - (s['config'] & 0x1c0)) & ~0x3f & M64
    new_ssp = None
    if s.get('sss'):
        if old_cpl == 3 or new_csl > old_csl:
            new_ssp = s['sspN'][new_csl]
            if new_ssp & 4:
                raise Fault(13, ext)
        else:
            new_ssp = (s['ssp'] - (s['config'] & 8)) & M64
    # regular stack: 8 pushes below new_rsp, #SS(ext) for a non-canonical slot
    data = ev.get('data', 0)
    if t == HWEXC and v == 14:
        data = ev['cr2']
    frame_hi_to_lo = [0, data, ass, s['rsp'], rfl, acs, old_rip, ev.get('error_code', 0) & 0xffff]
    for i in range(8):
        a = (new_rsp - 8 * (i + 1)) & M64
        if not paging_canonical(s, a):
            raise Fault(12, ext)
    sstack = []
    if s.get('sss') and old_cpl == 0:
        p = new_ssp
        if not paging_canonical(s, (p - 4) & M64):
            raise Fault(13, ext)
        p &= ~7 & M64
        for val in (acs, old_rip, s['ssp']):
            p = (p - 8) & M64
            if not paging_canonical(s, p):
                raise Fault(13, ext)
            sstack.append((p, val))
        new_ssp = p
    # commit
    if old_cpl == 3:
        s['cs'] = (s['star'] >> 32) & 0xfffc
        s['ss'] = (s['cs'] + 8) & 0xffff
        s['gs_base'], s['kgs_base'] = s['kgs_base'], s['gs_base']
        s['cs64'] = True
        if s.get('uss'):
            s['pl3_ssp'] = sext(s['ssp'], 56 if s.get('cpu_la57') else 47)
    s['rip'] = new_rip
    s['rflags'] = 2
    s['rsp'] = (new_rsp - 64) & M64
    s['config'] = (s['config'] & ~3) | new_csl
    if s.get('sss'):
        s['ssp'] = new_ssp
    if s.get('cet') and (s.get('s_cet', 0) & 4):
        s['s_cet'] = s['s_cet'] & ~(3 << 10)
    s['cpl'] = 0
    s['sti'] = False
    frame = list(reversed(frame_hi_to_lo))
    s['_sstack'] = sstack
    return s, frame


# --------------------------------------------------------------------------------- ERETS / ERETU
def erets(st, mem, ssmem=None):
    """Vol2A ERETS Operation; mem(addr) reads 8 bytes of the regular stack, ssmem the shadow stack"""
    s = dict(st)
    if not s.get('fred') or s['cpl'] != 0:
        raise Fault(6)
    rsp = (s['rsp'] + 8) & M64
    f = []
    for i in range(5):
        a = (rsp + 8 * i) & M64
        if not paging_canonical(s, a):
            raise Fault(12, 0)
        f.append(mem(a))
    new_rip, tcs, nfl, new_rsp, tss = f
    if (not paging_canonical(s, new_rip) or (tcs & 0xFFFFFFFFFFF8FFFF) != s['cs'] or
            (nfl & 0xFFFFFFFFFFC2802A) != 2 or (tss & 0xFFF8FFFF) != s['ss']):
        raise Fault(13, 0)
    csl = s['config'] & 3
    new_csl = min(csl, (tcs >> 16) & 3)
    if s.get('sss'):
        ssp = s['ssp']
        if ssp & 7:
            raise Fault(21, 2)
        new_ssp, lip, scs = ssmem(ssp), ssmem(ssp + 8), ssmem(ssp + 16)
        ssp += 24
        if scs != tcs or lip != new_rip or (new_ssp & 3):
            raise Fault(21, 2)
        if not cpu_canonical(s, new_ssp):
            raise Fault(13, 0)
        if new_csl < csl and s['sspN'][csl] != ssp:
            raise Fault(21, 2)
        s['ssp'] = new_ssp
    pend_ss = bool(s['rflags'] & 0x100) or (bool(tss & (1 << 17)) and bool(nfl & 0x100))
    if (tss >> 16) & 1 and (nfl & 0x200) and not s.get('sti'):
        s['sti'] = True
    else:
        s['sti'] = False
    if s.get('cet') and (s.get('s_cet', 0) & 4) and not (s.get('s_cet', 0) & (1 << 10)) and \
            (tcs >> 18) & 1:
        s['s_cet'] |= 1 << 11
    s['rip'], s['rflags'], s['rsp'] = new_rip, nfl, new_rsp
    s['config'] = (s['config'] & ~3) | new_csl
    if (tss >> 18) & 1:
        s['nmi_blocked'] = False
    s['pend_single_step'] = pend_ss
    return s


def eretu(st, mem):
    """Vol2A ERETU Operation, the two standard IA32_STAR configurations (descriptor-table case not
    modelled here)"""
    s = dict(st)
    if not s.get('fred') or s['cpl'] != 0 or not s['cs64']:
        raise Fault(6)
    if s['config'] & 3:
        raise Fault(13, 0)
    rsp = (s['rsp'] + 8) & M64
    f = []
    for i in range(5):
        a = (rsp + 8 * i) & M64
        if not paging_canonical(s, a):
            raise Fault(12, 0)
        f.append(mem(a))
    new_rip, tcs, nfl, new_rsp, tss = f
    if ((tcs & 0xFFFFFFFFFFFF0003) != 3 or (nfl & 0xFFFFFFFFFFC2B02A) != 2 or
            (tss & 0xFFF80003) != 3):
        raise Fault(13, 0)
    hi = (s['star'] >> 48) & 0xffff
    cs, ss = tcs & 0xffff, tss & 0xffff
    if cs == (hi + 16) & 0xffff and ss == (hi + 8) & 0xffff:
        l64 = True
    elif cs == hi and ss == (hi + 8) & 0xffff:
        l64 = False
    else:
        raise NotImplementedError('descriptor-table configuration')
    if l64:
        if not paging_canonical(s, new_rip):
            raise Fault(13, 0)
    else:
        new_rip &= 0xffffffff
        new_rsp &= 0xffffffff
    if s.get('uss') and not l64 and (s['pl3_ssp'] >> 32):
        raise Fault(13, 0)
    if s.get('sss') and s['sspN'][0] != s['ssp']:
        raise Fault(21, 2)
    pend_ss = bool(s['rflags'] & 0x100) or (bool(tss & (1 << 17)) and bool(nfl & 0x100))
    s['rip'], s['rflags'], s['rsp'] = new_rip, nfl, new_rsp
    s['cs'], s['ss'], s['cs64'], s['cpl'] = cs, ss, l64, 3
    s['gs_base'], s['kgs_base'] = s['kgs_base'], s['gs_base']
    if s.get('uss'):
        s['ssp'] = s['pl3_ssp']
    if (tss >> 18) & 1:
        s['nmi_blocked'] = False
    s['pend_single_step'] = pend_ss
    s['sti'] = False
    return s


# ------------------------------------------------------------------------------------- scenarios
K = 0xffff800000000000


def base_state():
    """the unit-test setup (test_x86_fl_*): IA32_STAR = 0023_0008_0000_0000 (kernel CS 08H / SS
    10H, user CS 33H / SS 2BH, compatibility CS 23H), FRED entry page FL_ENTRY = 0x205000"""
    return {
        'cpl': 0, 'cs64': True, 'rip': 0x1000, 'rsp': 0x20f000, 'rflags': 0x202, 'cs': 0x08,
        'ss': 0x10, 'gs_base': 0x1111, 'kgs_base': 0x2222, 'star': 0x0023000800000000,
        'config': 0x205000 | 0x40, 'stklvls': 0, 'rspN': [0x20e000, 0x20d000, 0x20c000, 0x20b000],
        'sspN': [0, 0, 0, 0], 'ssp': 0, 'pl3_ssp': 0, 'la57': False, 'cpu_la57': False,
        'sss': False, 'uss': False, 'cet': False, 's_cet': 0, 'sti': False, 'fred': True,
    }


def selftest():
    ok = True

    def check(name, got, exp):
        nonlocal ok
        if got != exp:
            ok = False
            print('FAIL %s: got %r expected %r' % (name, got, exp))

    # LAM (Vol3A 4.4.2 / 4.5.1 examples)
    check('lam48 user', lam_mask(0x7f12000012345678, 1 << 62, False, False), 0x0000000012345678)
    check('lam48 user b47', lam_mask(0x7f12800012345678, 1 << 62, False, False),
          0x7fff800012345678)
    check('lam57 user', lam_mask(0x7e00123412345678, 1 << 61, False, False), 0x0000123412345678)
    check('lam57 overrides u48', lam_mask(0x7e00123412345678, 3 << 61, False, False),
          0x0000123412345678)
    check('no lam user', lam_mask(0x7f12000012345678, 0, True, False), 0x7f12000012345678)
    check('lam_sup 4-level', lam_mask(0x8012ffff80001000, 0, True, False), 0xffffffff80001000)
    check('lam_sup 5-level', lam_mask(0xa5ff800080001000, 0, True, True), 0xffff800080001000)
    check('lam_sup off', lam_mask(0x8012ffff80001000, 1 << 62, False, False), 0x8012ffff80001000)
    check('lam48 5-level canonical', lam_access_ok(0x7fff000000001000, 1 << 62, False, True)[0], True)
    check('lam57 4-level bits 56:47', lam_access_ok(0x7e00800000001000, 1 << 61, False, False)[0],
          False)
    check('lam57 4-level ok', lam_access_ok(0x7e007fffffff1000, 1 << 61, False, False)[0], True)
    check('lam57 4-level low', lam_access_ok(0x7e00000000001000, 1 << 61, False, False)[0], True)
    # FRED: INT3 at CPL0, same stack level, RSP decrement 1 line (config bits 8:6 = 1)
    st = base_state()
    s, fr = deliver(st, {'type': SWEXC, 'vector': 3, 'insn_len': 1, 'rip_next': 0x1001})
    check('int3 rip', s['rip'], 0x205100)
    check('int3 rsp', s['rsp'], ((0x20f000 - 0x40) & ~0x3f) - 64)
    check('int3 frame', fr, [0, 0x1001, 0x08, 0x202, 0x20f000,
                             0x10 | (event_info(3, SWEXC, True, False, 1) << 32), 0, 0])
    check('int3 info', event_info(3, SWEXC, True, False, 1), 0x12060003)
    # #PF at CPL3 (64-bit): FRED_RSP0, swapgs, CS/SS from STAR, event data = CR2, RF set (fault)
    st = base_state()
    st.update({'cpl': 3, 'cs': 0x33, 'ss': 0x2b, 'rip': 0x4000, 'rsp': 0x7000})
    s, fr = deliver(st, {'type': HWEXC, 'vector': 14, 'error_code': 6, 'cr2': 0xdead000})
    check('pf rip', s['rip'], 0x205000)
    check('pf rsp', s['rsp'], 0x20e000 - 64)
    check('pf seg', (s['cs'], s['ss'], s['gs_base'], s['kgs_base']), (0x08, 0x10, 0x2222, 0x1111))
    check('pf frame', fr, [6, 0x4000, 0x33, 0x10202, 0x7000,
                           0x2b | (event_info(14, HWEXC, True, False, 0) << 32), 0xdead000, 0])
    # nested / #DF / external interrupt stack levels (Appendix B)
    st = base_state()
    st.update({'stklvls': 2 << 16, 'config': 0x205000 | (1 << 9) | 1})
    s, fr = deliver(st, {'type': HWEXC, 'vector': 8, 'error_code': 0})
    check('df csl', s['config'] & 3, 2)
    check('df rsp', s['rsp'], 0x20c000 - 64)
    s, fr = deliver(st, {'type': EXTINT, 'vector': 0x60})
    check('irq csl', s['config'] & 3, 1)
    check('irq rsp', s['rsp'], ((0x20f000 - 0) & ~0x3f) - 64)
    # ERETS round trip of the INT3 frame
    st = base_state()
    s, fr = deliver(st, {'type': SWEXC, 'vector': 3, 'insn_len': 1, 'rip_next': 0x1001})
    mem = {}
    for i, w in enumerate(fr):
        mem[s['rsp'] + 8 * i] = w
    r = erets(s, lambda a: mem[a])
    check('erets', (r['rip'], r['rsp'], r['rflags'], r['config'] & 3), (0x1001, 0x20f000, 0x202, 0))
    return ok


def unit():
    """the values test_x86_fl_* checks (printed for reference)"""
    st = base_state()
    s, fr = deliver(st, {'type': SWEXC, 'vector': 3, 'insn_len': 1, 'rip_next': 0x1001})
    print('INT3 CPL0: rip %x rsp %x frame %s' % (s['rip'], s['rsp'], [hex(x) for x in fr]))


# ------------------------------------------------------------------------- LAM expected-value cases
MEM_PTR = 0x30020000 + 0x8000          # emu-alltest operand memory: RSI = MEM + 0x8000


def lam_cases():
    """cases_lam.txt: loads / stores through pointers with metadata, Unicorn only, CPL0, paging off
    (IA-32e mode, 4-level linear addresses: the MAX model's CR4.LA57 = 0). The snippet sets the CR3
    LAM bit(s), accesses [RBX] and restores CR3 (a fault stops it before the restore)."""
    out = []
    val = 0x1122334455667788
    mem = ''.join('%02x' % ((val >> (8 * i)) & 0xff) for i in range(8))
    metas = [0, 0x0001, 0x5a12, 0x7fff, 0x7e00, 0x0200, 0x0012, 0x4000, 0x7e01]
    for name, cr3bits, setbits in (('none', 0, ''), ('LAM48', 1 << 62, 'bts rax, 62; '),
                                   ('LAM57', 1 << 61, 'bts rax, 61; '),
                                   ('LAM57 over LAM48', 3 << 61, 'bts rax, 61; bts rax, 62; ')):
        out.append('# --- %s (CR3 bits 62:61 = %d%d)' % (name, (cr3bits >> 62) & 1, (cr3bits >> 61) & 1))
        for hi47 in (0, 1):
            for m in metas:
                ptr = MEM_PTR | (m << 48) | (hi47 << 47)
                ok, masked = lam_access_ok(ptr, cr3bits, False, False)
                asm = 'mov rcx, cr3; mov rax, rcx; %smov cr3, rax; mov rdx, qword ptr [rbx]; mov cr3, rcx' % setbits
                if ok:
                    if masked != MEM_PTR:
                        continue            # canonical but another (unmapped) address
                    out.append('%s | rbx=0x%X m+0x8000=%s =>! rdx=0x%X' % (asm, ptr, mem, val))
                    sto = asm.replace('mov rdx, qword ptr [rbx]', 'mov qword ptr [rbx + 8], rdx')
                    out.append('%s | rbx=0x%X rdx=0x%X =>! m+0x8008=%s' % (sto, ptr, val, mem))
                else:
                    out.append('%s | rbx=0x%X =>! #GP(0)' % (asm, ptr))
    return out


def write_cases():
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.normpath(os.path.join(here, '..', '..', 'data', 'cases_lam.txt'))
    head = ['# LAM (linear-address masking, ledger U1180-U1181): expected values from the independent model',
            '# Emulator/tools/isa/ref_fred_lam.py (lam_mask / lam_access_ok, SDM 325462-092 Vol3A 4.4.2 / 4.5.1);',
            '# regenerate with --write, do not edit. Unicorn only, MAX model with UC_X86_FEATURE_LAM, CPL0, paging',
            '# off (4-level linear addresses): emu-alltest --cases Emulator\\data\\cases_lam.txt --x86-features 8 --expect-only']
    lines = head + lam_cases()
    with open(path, 'wb') as f:
        f.write(('\r\n'.join(lines) + '\r\n').encode('ascii'))
    print('wrote %s (%d cases)' % (path, len([l for l in lines if not l.startswith('#')])))


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        r = selftest()
        print('ref_fred_lam selftest: %s' % ('OK' if r else 'FAILED'))
        sys.exit(0 if r else 1)
    if '--write' in sys.argv:
        write_cases()
        sys.exit(0)
    if '--unit' in sys.argv:
        unit()
        sys.exit(0)
    print(__doc__)
    sys.exit(2)
