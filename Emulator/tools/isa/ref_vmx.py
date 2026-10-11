r"""Independent reference model for VMX (ledger U1300-U1399, agent "vmx", plan 1.J.12, decision A11).

Written from the Intel documents only (the emulator's C sources were not read for the expected values;
VMX instructions need CPL0 and VMX operation, which the i5-13600K test host cannot run from user mode,
and SDE does not model VMX, so every value here is "specification-validated"):
  * SDM 325462-092 Vol3D Appendix A (capability MSRs: allowed 0- / 1-settings, default1 classes, A.2-A.5),
    Appendix B (VMCS field encodings, Table B-1 ff. and 27.11.2 "the encoding of a field"),
    Appendix C (basic exit reasons);
  * Vol3C 27.6.4 / 27.6.9 (I/O and MSR bitmaps), 28.1.3 (conditional exits), 28.2 (exception bitmap and
    the #PF error-code mask / match), 28.3 (TSC offsetting and scaling), 30.2.1 Tables 30-3 / 30-4 /
    30-5 / 30-7 (exit qualifications), 30.2.5 Tables 30-10 / 30-11 / 30-12 (instruction information),
    31.3 (EPT translation, misconfigurations, violations, A/D flags), 32.1 / 32.2 (virtual APIC: VISR /
    VIRR layout, PPR / EOI / self-IPI virtualization, evaluation and delivery of virtual interrupts).

Usage:
  python -I ref_vmx.py --selftest   hand-derived checks of the model (exit 0 on pass)
  python -I ref_vmx.py --unit       print the expected values used by the unit tests (test_x86.c vmx_)
"""
import sys

M64 = (1 << 64) - 1

# ---- Appendix B / 27.11.2: a field encoding ----------------------------------------------------------
WIDTHS = {0: 16, 1: 64, 2: 32, 3: 'natural'}
TYPES = {0: 'control', 1: 'read-only data', 2: 'guest-state', 3: 'host-state'}


def field_parts(enc):
    """(access type 'full' / 'high', index, type, width) of a VMCS-field encoding (bits 31:15 are 0)"""
    if enc >> 15:
        raise ValueError('bits 31:15 of a field encoding are reserved')
    access = 'high' if enc & 1 else 'full'
    return access, (enc >> 1) & 0x1ff, TYPES[(enc >> 10) & 3], WIDTHS[(enc >> 13) & 3]


# ---- Appendix A: capability MSRs ---------------------------------------------------------------------
# A.2: the default1 classes of the control MSRs that have TRUE counterparts
DEFAULT1 = {
    'pin': (1 << 1) | (1 << 2) | (1 << 4),
    'proc': (1 << 1) | (7 << 4) | (1 << 8) | (0xf << 13) | (1 << 26),
    'exit': 0x1ff | (1 << 10) | (1 << 11) | (1 << 13) | (1 << 14) | (1 << 16) | (1 << 17),
    'entry': 0x1ff | (1 << 12),
}


def ctl_ok(value, cap):
    """a control word against a capability MSR (A.3: bits 31:0 allowed 0-settings, 63:32 allowed 1)"""
    allowed0, allowed1 = cap & 0xffffffff, cap >> 32
    return (value & allowed0) == allowed0 and (value & ~allowed1) == 0


# ---- 30.2.1 exit qualifications ------------------------------------------------------------------------
def qual_cr(cr, access, gpr=0, lmsw_src=0, lmsw_mem=False):
    """Table 30-3: access 0 MOV to CR, 1 MOV from CR, 2 CLTS, 3 LMSW"""
    q = (cr & 15) | (access << 4)
    if access == 3:
        q |= (1 << 6 if lmsw_mem else 0) | ((lmsw_src & 0xffff) << 16)
        return q
    if access in (0, 1):
        q |= (gpr & 15) << 8
    return q


def qual_dr(dr, from_dr, gpr):
    """Table 30-4"""
    return (dr & 7) | (0x10 if from_dr else 0) | ((gpr & 15) << 8)


def qual_io(size, is_in, string, rep, imm, port):
    """Table 30-5: size code 0 = 1 byte, 1 = 2 bytes, 3 = 4 bytes"""
    code = {1: 0, 2: 1, 4: 3}[size]
    return code | (8 if is_in else 0) | (0x10 if string else 0) | (0x20 if rep else 0) | \
        (0x40 if imm else 0) | ((port & 0xffff) << 16)


# ---- 27.6.4 / 27.6.9 bitmaps ---------------------------------------------------------------------------
def io_exits(bitmap_a, bitmap_b, port, size):
    """bitmap_a / bitmap_b: sets of ports whose bit is 1; a wrap past FFFFH always exits (28.1.3)"""
    if port + size - 1 > 0xffff:
        return True
    return any((p in bitmap_a) if p < 0x8000 else (p in bitmap_b) for p in range(port, port + size))


def msr_bitmap_bit(msr, write):
    """(byte offset, bit) of an MSR in the MSR-bitmap page, None outside both ranges (exit)"""
    if msr <= 0x1fff:
        base = 0
    elif 0xc0000000 <= msr <= 0xc0001fff:
        base = 0x400
    else:
        return None
    base += 0x800 if write else 0
    return base + (msr & 0x1fff) // 8, msr & 7


# ---- 28.2 exception bitmap -------------------------------------------------------------------------------
def exception_exits(bitmap, vector, pfec=0, mask=0, match=0):
    bit = (bitmap >> vector) & 1
    if vector == 14 and (pfec & mask) != match:
        bit ^= 1
    return bool(bit)


# ---- 28.3 TSC offsetting / scaling -----------------------------------------------------------------------
def guest_tsc(tsc, offset, scaling=False, mult=0):
    if scaling:
        tsc = (tsc * mult) >> 48
    return (tsc + offset) & M64


# ---- 30.2.5 instruction information ------------------------------------------------------------------------
def info_mem(addr_size, seg, base=None, index=None, scale=0):
    """the memory-operand part of Tables 30-9 .. 30-14 (address size 0 / 1 / 2 = 16 / 32 / 64 bits)"""
    i = (addr_size << 7) | (seg << 15)
    i |= (scale | (index << 18)) if index is not None else (1 << 22)
    i |= (base << 23) if base is not None else (1 << 27)
    return i


def info_rdrand(reg, opsize):
    """Table 30-12: operand register 6:3, operand size 12:11 (0 16-bit, 1 32-bit, 2 64-bit)"""
    return (reg << 3) | ({16: 0, 32: 1, 64: 2}[opsize] << 11)


# ---- 31.3 EPT ------------------------------------------------------------------------------------------------
class EptMisconfig(Exception):
    pass


class EptViolation(Exception):
    def __init__(self, qual):
        Exception.__init__(self, qual)
        self.qual = qual


def ept_translate(mem, eptp, gpa, access, final=True, maxphy=46, mbec=False, user_la=False,
                  la_attr=(False, True, False)):
    """mem: dict physical address -> 64-bit entry; access: 'r', 'w' or 'x'; la_attr: (user, rw, xd) of
    the linear address for qualification bits 9-11 (final accesses). Returns the physical address,
    raises EptMisconfig / EptViolation(qualification)."""
    levels = ((eptp >> 3) & 7) + 1
    ad = (eptp >> 6) & 1
    table = eptp & 0x000ffffffffff000
    rsvd = 0x000ffffffffff000 & ~((1 << maxphy) - 1)
    perm = 0x407
    write = access == 'w' or (not final and ad)
    q = (4 if access == 'x' else 0) | (2 if write else 0) | (1 if (access == 'r' or not final) else 0)
    if final and access == 'w':
        q &= ~1
    present = True
    size = 0
    e = 0
    if levels == 4 and gpa >> 48:
        present = False
    else:
        for lvl in range(levels, 0, -1):
            shift = 12 + 9 * (lvl - 1)
            e = mem.get(table + ((gpa >> shift) & 0x1ff) * 8, 0)
            if not (e & 7) and not (mbec and (e >> 10) & 1):
                present = False
                break
            leaf = lvl == 1 or (lvl in (2, 3) and (e >> 7) & 1)
            if (not (e & 1) and (e & 2)) or (e & rsvd):
                raise EptMisconfig()
            if not leaf and e & (0xf8 if lvl >= 4 else 0x78):
                raise EptMisconfig()
            if leaf and lvl > 1 and e & ((1 << shift) - 1) & 0x000ffffffffff000:
                raise EptMisconfig()
            if leaf and ((e >> 3) & 7) in (2, 3, 7):
                raise EptMisconfig()
            perm &= e
            if leaf:
                size = 1 << shift
                break
            table = e & 0x000ffffffffff000
    if present:
        if access == 'x':
            ok = (perm >> 10) & 1 if (mbec and user_la) else (perm >> 2) & 1
        else:
            ok = bool(perm & 1) and not (write and not (perm & 2))
        if ok:
            return (e & 0x000ffffffffff000 & ~(size - 1)) | (gpa & (size - 1))
        q |= (perm & 7) << 3
        if mbec:
            q |= ((perm >> 10) & 1) << 6
    q |= 1 << 7
    if final:
        user, rw, xd = la_attr
        q |= (1 << 8) | (user << 9) | (rw << 10) | (xd << 11)
    raise EptViolation(q)


# ---- 32.1 / 32.2 virtual APIC ----------------------------------------------------------------------------------
class VApic:
    def __init__(self):
        self.page = {}          # offset -> 32-bit value
        self.rvi = 0
        self.svi = 0
        self.pending = False

    def rd(self, off):
        return self.page.get(off, 0)

    def bit(self, base, vec, value):
        off = base | ((vec & 0xe0) >> 1)
        w = self.rd(off)
        self.page[off] = (w | (1 << (vec & 31))) if value else (w & ~(1 << (vec & 31)))

    def highest(self, base):
        for i in range(7, -1, -1):
            w = self.rd(base | (i << 4))
            if w:
                return i * 32 + w.bit_length() - 1
        return None

    def ppr(self):
        vtpr = self.rd(0x80)
        self.page[0xa0] = (vtpr & 0xff) if ((vtpr >> 4) & 0xf) >= (self.svi >> 4) else (self.svi & 0xf0)

    def evaluate(self, intr_window=False):
        self.pending = not intr_window and (self.rvi >> 4) > ((self.rd(0xa0) >> 4) & 0xf)

    def self_ipi(self, vec):
        self.bit(0x200, vec, True)
        self.rvi = max(self.rvi, vec)
        self.evaluate()

    def deliver(self):
        vec = self.rvi
        self.bit(0x100, vec, True)
        self.svi = vec
        self.page[0xa0] = vec & 0xf0
        self.bit(0x200, vec, False)
        h = self.highest(0x200)
        self.rvi = 0 if h is None else h
        self.pending = False
        return vec

    def eoi(self, eoi_exit_bitmap):
        """returns the qualification of an EOI-induced VM exit, or None"""
        vec = self.svi
        self.bit(0x100, vec, False)
        h = self.highest(0x100)
        self.svi = 0 if h is None else h
        self.ppr()
        if (eoi_exit_bitmap >> vec) & 1:
            return vec
        self.evaluate()
        return None

    def status(self):
        return self.rvi | (self.svi << 8)


# ---- the unit-test scenarios -----------------------------------------------------------------------------------
VMX_DATA = 0x200000
VMX_SCRATCH = VMX_DATA + 0x13000
VMX_PT = VMX_DATA + 0xa000


def ept_identity(ad=False):
    """the EPT of test_x86_vmx_ept: 0-2 MB one 2-MByte page, 2-4 MB 4-KByte pages, RWX, WB"""
    pml4, pdpt, pd, pt = VMX_DATA + 0x14000, VMX_DATA + 0x15000, VMX_DATA + 0x16000, VMX_DATA + 0x17000
    mem = {pml4: pdpt | 7, pdpt: pd | 7, pd: 0x80 | 0x30 | 7, pd + 8: pt | 7}
    for i in range(512):
        mem[pt + 8 * i] = (0x200000 + 0x1000 * i) | 0x30 | 7
    return mem, pml4 | 0x1e | (0x40 if ad else 0), pt


def unit_values():
    v = {}
    v['cr0_mov_rbx'] = qual_cr(0, 0, gpr=3)
    v['clts'] = qual_cr(0, 2)
    v['lmsw_ax_1'] = qual_cr(0, 3, lmsw_src=1)
    v['lmsw_mem_b'] = qual_cr(0, 3, lmsw_src=0xb, lmsw_mem=True)
    v['mov_cr3_rcx'] = qual_cr(3, 0, gpr=1)
    v['mov_rdx_cr3'] = qual_cr(3, 1, gpr=2)
    v['mov_cr8_rax'] = qual_cr(8, 0, gpr=0)
    v['mov_rbx_cr8'] = qual_cr(8, 1, gpr=3)
    v['mov_rax_dr7'] = qual_dr(7, True, 0)
    v['mov_dr0_rcx'] = qual_dr(0, False, 1)
    v['out_dx_al_80'] = qual_io(1, False, False, False, False, 0x80)
    v['in_al_71'] = qual_io(1, True, False, False, True, 0x71)
    v['out_dx_eax_fffe'] = qual_io(4, False, False, False, False, 0xfffe)
    v['outsb_80'] = qual_io(1, False, True, False, False, 0x80)
    v['rep_outsb_80'] = qual_io(1, False, True, True, False, 0x80)
    v['msrbm_c0000080_r'] = msr_bitmap_bit(0xc0000080, False)
    v['msrbm_1d9_w'] = msr_bitmap_bit(0x1d9, True)
    v['sgdt_rax_info'] = info_mem(2, 3, base=0)
    v['rdrand_rax_info'] = info_rdrand(0, 64)
    mem, eptp, pt = ept_identity()
    mem[pt + 8 * ((VMX_SCRATCH - 0x200000) >> 12)] = VMX_SCRATCH | 0x30 | 5
    try:
        ept_translate(mem, eptp, VMX_SCRATCH, 'w')
    except EptViolation as e:
        v['ept_write_ro'] = e.qual
    mem[pt + 8 * ((VMX_SCRATCH - 0x200000) >> 12)] = 0
    try:
        ept_translate(mem, eptp, VMX_SCRATCH + 8, 'r')
    except EptViolation as e:
        v['ept_read_np'] = e.qual
    mem[pt + 8 * ((VMX_PT - 0x200000) >> 12)] = 0
    try:
        ept_translate(mem, eptp, VMX_PT, 'r', final=False)
    except EptViolation as e:
        v['ept_walk_np'] = e.qual
    a = VApic()
    a.ppr()
    a.self_ipi(0x40)
    v['vintr_recognized'] = a.pending
    a.deliver()
    v['gis_after_delivery'] = a.status()
    v['vppr_after_delivery'] = a.rd(0xa0)
    v['eoi_exit_qual'] = a.eoi(1 << 0x40)
    v['gis_after_eoi'] = a.status()
    return v


def selftest():
    ok = True

    def check(name, got, want):
        nonlocal ok
        if got != want:
            ok = False
            print('FAIL %s: %r, expected %r' % (name, got, want))

    # Appendix B: VPID 0000H (16-bit control), guest RIP 681EH (natural guest-state), VMCS link pointer
    # 2800H (64-bit guest-state full) / 2801H (high), exit reason 4402H (32-bit read-only data)
    check('enc VPID', field_parts(0x0000), ('full', 0, 'control', 16))
    check('enc guest RIP', field_parts(0x681e), ('full', 15, 'guest-state', 'natural'))
    check('enc link high', field_parts(0x2801), ('high', 0, 'guest-state', 64))
    check('enc exit reason', field_parts(0x4402), ('full', 1, 'read-only data', 32))
    # A.3: the model's capability values (docs/vmx.md) contain the default1 bits as allowed 0-settings
    check('pin default1', 0x00000016 & DEFAULT1['pin'], DEFAULT1['pin'])
    check('proc default1', 0x0401e172 & DEFAULT1['proc'], DEFAULT1['proc'])
    check('exit default1', 0x00036dff & DEFAULT1['exit'], DEFAULT1['exit'])
    check('entry default1', 0x000011ff & DEFAULT1['entry'], DEFAULT1['entry'])
    check('true proc frees CR3 exiting', DEFAULT1['proc'] & ~0x04006172, (1 << 15) | (1 << 16))
    check('ctl_ok', ctl_ok(0x04006172, 0xfffbfffe04006172), True)
    check('ctl_ok reserved 0', ctl_ok(0x04006170, 0xfffbfffe04006172), False)
    # 28.2: #PF with mask / match
    check('pf mask 0', exception_exits(1 << 14, 14, pfec=0, mask=0, match=0), True)
    check('pf reversed', exception_exits(1 << 14, 14, pfec=0, mask=1, match=1), False)
    check('pf never', exception_exits(1 << 14, 14, pfec=5, mask=0, match=0xffffffff), False)
    # 27.6.4: a wrap always exits
    check('io wrap', io_exits(set(), set(), 0xfffe, 4), True)
    check('io bit B', io_exits(set(), {0x8000}, 0x7fff, 2), True)
    check('io clear', io_exits({0x80}, set(), 0x81, 1), False)
    check('msr outside', msr_bitmap_bit(0x40000000, False), None)
    # 28.3: multiplier 1 is TSC >> 48
    check('tsc scale', guest_tsc(0x0001234500000000, 0x1000, True, 1), 0x1000 + 0x1)
    check('tsc offset', guest_tsc(5, M64), 4)
    # 31.3: a 1-GByte page with reserved bits 29:12 set is a misconfiguration
    mem = {0x1000: 0x2000 | 7, 0x2008: 0x40000000 | 0x80 | 0x30 | 7 | 0x1000}
    try:
        ept_translate(mem, 0x1000 | 0x1e, 0x40000123, 'r')
        check('ept 1G rsvd', 'translated', 'misconfig')
    except EptMisconfig:
        pass
    mem[0x2008] &= ~0x1000
    check('ept 1G', ept_translate(mem, 0x1000 | 0x1e, 0x40000123, 'r'), 0x40000123)
    mem[0x2008] = 0x40000000 | 0x80 | 0x30 | 4          # execute-only (EPT_VPID_CAP bit 0)
    check('ept exec-only fetch', ept_translate(mem, 0x1000 | 0x1e, 0x40000123, 'x'), 0x40000123)
    try:
        ept_translate(mem, 0x1000 | 0x1e, 0x40000123, 'r', la_attr=(False, True, False))
    except EptViolation as e:
        check('ept exec-only read', e.qual, 1 | (4 << 3) | 0x80 | 0x100 | 0x400)
    try:
        ept_translate(mem, 0x1000 | 0x1e, 1 << 48, 'r')
    except EptViolation as e:
        check('ept 4-level gpa[48]', e.qual & 0x38, 0)
    # the values the unit tests check
    v = unit_values()
    want = {
        'cr0_mov_rbx': 0x300, 'clts': 0x20, 'lmsw_ax_1': 0x10030, 'lmsw_mem_b': 0xb0070,
        'mov_cr3_rcx': 0x103, 'mov_rdx_cr3': 0x213, 'mov_cr8_rax': 8, 'mov_rbx_cr8': 0x318,
        'mov_rax_dr7': 0x17, 'mov_dr0_rcx': 0x100, 'out_dx_al_80': 0x800000, 'in_al_71': 0x710048,
        'out_dx_eax_fffe': 0xfffe0003, 'outsb_80': 0x800010, 'rep_outsb_80': 0x800030,
        'msrbm_c0000080_r': (0x410, 0), 'msrbm_1d9_w': (0x83b, 1),
        'sgdt_rax_info': (2 << 7) | (3 << 15) | (1 << 22), 'rdrand_rax_info': 2 << 11,
        'ept_write_ro': 0x5aa, 'ept_read_np': 0x581, 'ept_walk_np': 0x81,
        'vintr_recognized': True, 'gis_after_delivery': 0x4000, 'vppr_after_delivery': 0x40,
        'eoi_exit_qual': 0x40, 'gis_after_eoi': 0,
    }
    for k in sorted(want):
        check(k, v.get(k), want[k])
    return ok


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        r = selftest()
        print('ref_vmx selftest: %s' % ('OK' if r else 'FAILED'))
        sys.exit(0 if r else 1)
    if '--unit' in sys.argv:
        for k, val in sorted(unit_values().items()):
            print('%-22s %s' % (k, hex(val) if isinstance(val, int) and not isinstance(val, bool) else val))
        sys.exit(0)
    print(__doc__)
    sys.exit(2)
