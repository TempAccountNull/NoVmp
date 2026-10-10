r"""Independent reference model + expected-value case generator for the emu-alltest sweep forms that
stay out of the native run (ledger U850-U852): WRFSBASE / WRGSBASE, SYSENTER's entry check and
LFS / LGS. Natively they would change the host thread's FS/GS (the GS base is the Windows TEB) or
enter the Windows kernel, so their only check is this model ("per manual").

Written from the Intel SDM 092 only (the emulator's C sources were not read for the expectations;
no CPU measurements):
  * Vol2D WRFSBASE/WRGSBASE, Vol2C RDFSBASE/RDGSBASE: "FS/GS segment base address := SRC"; a 32-bit
    operand ignores bits 63:32 of the source and clears bits 63:32 of the base; RD*BASE r32 reads
    bits 31:0 (zero-extended into the 64-bit register, Vol1 3.4.1.1). 64-bit-mode exceptions: #UD
    for LOCK and for CR4.FSGSBASE = 0; #GP(0) "if the source register contains a non-canonical
    address".
  * Vol3A 4.5.3 (System Registers Containing Linear Addresses): "WRFSBASE and WRGSBASE ... cause a
    #GP if [they] would load a base address with an address that is not paging canonical. Thus, if
    4-level paging is active, these instructions do not allow loading of addresses that are 57-bit
    canonical but not 48-bit canonical" (unlike WRMSR, which checks CPU canonicality).
  * Vol2B SYSENTER: "IF CR0.PE = 0 OR (CR4.FRED = 0 AND IA32_SYSENTER_CS[15:2] = 0) THEN #GP(0)";
    then (U902) "RSP := IA32_SYSENTER_ESP; RIP := IA32_SYSENTER_EIP" (IA-32e mode), "CS.Selector :=
    IA32_SYSENTER_CS[15:0] AND FFFCH", "SS.Selector := CS.Selector + 8".
  * Vol2A LDS/LES/LFS/LGS/LSS, 64-bit mode: the operand is m16:16 (66h), m16:32 (no prefix) or
    m16:64 (REX.W), offset first; "DEST := Offset(SRC)" (a 16-bit destination keeps bits 63:16, a
    32-bit one is zero-extended); FS/GS with a NULL selector: loaded, no check; with a non-NULL
    selector: "IF Segment selector index is not within descriptor table limits or access rights
    indicate segment neither data nor readable code segment or segment is data or
    nonconforming-code segment and (RPL > DPL or CPL > DPL) THEN #GP(selector)"; not present:
    #NP(selector). (The exception list of the same page says "both RPL and CPL are greater than
    DPL"; the pseudocode and Vol3A 5.7 - DPL >= max(CPL, RPL) for data access - say "or", which
    the model follows; the cases mark the one selector where the readings differ.)
Machine state (the harness, emu-alltest at_engine.hpp / docs\emu-alltest.md, not the SDM):
  a. 64-bit mode; CR4.LA57 = 0 (IA-32e mode cannot change LA57; the emulator's reset CR4 has it
     clear - loose guard case), so "paging canonical" is 48-bit canonical.
  b. CR4.FSGSBASE = 1 at reset (guard case); the cpl=3 cases keep the reset CR4.
  c. CR4.FRED = 0: the emulator's CPU model has no FRED (CR4 bit 32 reserved), so the SYSENTER check
     is IA32_SYSENTER_CS[15:2] = 0.
  d. cpl=3: the harness's Windows x64 GDT (limit 37h): 08h all-zero descriptor, 10h code64 DPL0,
     18h data DPL0, 23h code32 DPL3, 2Bh data DPL3, 33h code64 DPL3 (all present, code readable,
     nonconforming); no LDT (TI = 1 is outside the limit of the null LDTR).
  e. IA32_SYSENTER_CS: the SDM leaves its reset value open; the emulator's 64-bit reset state has the
     OS-like 10h (U852; guard case RDMSR 174h). The cases write it with WRMSR (CPL0) first.
Usage: python ref_sweep_sdm.py --selftest | --cases [fsgsbase,sysenter,lfslgs]
       (prints Emulator\data\cases_sweep_sdm.txt; default: all parts)
"""
import sys

LIN_BITS = 48            # paging canonical with CR4.LA57 = 0 (a)
GDT = [0, 0, 0x00209B0000000000, 0x00CF93000000FFFF, 0x00CFFB000000FFFF, 0x00CFF3000000FFFF, 0x0020FB0000000000]   # (d)
GDT_LIMIT = len(GDT) * 8 - 1
SYSENTER_CS_RESET = 0x10  # (e)
M64 = (1 << 64) - 1


class Fault(Exception):
    def __init__(self, name):
        Exception.__init__(self, name)
        self.name = name


def canonical(v, bits=LIN_BITS):
    top = v >> (bits - 1)
    return top == 0 or top == (1 << (65 - bits)) - 1


# ---- WRFSBASE / WRGSBASE / RDFSBASE / RDGSBASE (Vol2D, Vol2C, Vol3A 4.5.3) ----
def wrbase(src, opsize, fsgsbase=True, lock=False):
    """the new segment base, or Fault"""
    if lock or not fsgsbase:
        raise Fault('#UD')
    v = src & 0xFFFFFFFF if opsize == 32 else src & M64
    if opsize == 64 and not canonical(v):
        raise Fault('#GP')
    return v


def rdbase(base, opsize):
    return base & 0xFFFFFFFF if opsize == 32 else base & M64


# ---- SYSENTER entry check (Vol2B) ----
def sysenter_faults(sysenter_cs, pe=True, fred=False):
    return (not pe) or ((not fred) and ((sysenter_cs >> 2) & 0x3FFF) == 0)


def sysenter_selectors(sysenter_cs):
    """U902 (Vol2B SYSENTER Operation): CS.Selector := IA32_SYSENTER_CS[15:0] AND FFFCH;
    SS.Selector := CS.Selector + 8 (a 16-bit selector)"""
    cs = sysenter_cs & 0xFFFC
    return cs, (cs + 8) & 0xFFFF


# ---- LFS / LGS in 64-bit mode (Vol2A) ----
def load_fsgs(sel, cpl):
    """None when the selector loads, else the Fault"""
    if (sel & ~3) == 0:
        return None                                   # NULL selector: loaded, no check
    rpl, idx, ti = sel & 3, sel >> 3, (sel >> 2) & 1
    if ti or idx * 8 + 7 > GDT_LIMIT:
        return Fault('#GP')                           # outside the descriptor table
    d = GDT[idx]
    s, typ, dpl, p = (d >> 44) & 1, (d >> 40) & 0xF, (d >> 45) & 3, (d >> 47) & 1
    data = s and not (typ & 8)
    code = s and (typ & 8)
    readable, conforming = code and (typ & 2), code and (typ & 4)
    if not (data or readable):
        return Fault('#GP')
    if (data or (code and not conforming)) and (rpl > dpl or cpl > dpl):
        return Fault('#GP')
    if not p:
        return Fault('#NP')
    return None


def lfs_dest(old, offset, opsize):
    if opsize == 16:
        return (old & ~0xFFFF & M64) | (offset & 0xFFFF)
    return offset & ((1 << opsize) - 1)


# ---- case lines ----
def hx(v):
    return '0x%X' % v


def le(v, n):
    return ''.join('%02X' % ((v >> (8 * k)) & 0xFF) for k in range(n))


W64 = [0, 1, 0x00007FFFFFFFFFFF, 0x0000800000000000, 0xFFFF800000000000, 0xFFFF7FFFFFFFFFFF, 0x8000000000000000,
       0xFFFFFFFFFFFFFFFF, 0x00FF000000000000, 0x0100000000000000, 0x00007FFF12345678, 0xFFFFFFFF80000000]
W32 = [0x0000000000000000, 0xFFFFFFFF89ABCDEF, 0x8000000080000000, 0x12345678FFFFFFFF, 0x0000800000001000]


PARTS = ('fsgsbase', 'sysenter', 'lfslgs')


def lines(parts=PARTS):
    out = []
    a = out.append
    a('# emu-alltest sweep forms without a native run (ledger U850-U852), expected values only:')
    a('#   emu-alltest --cases Emulator\\data\\cases_sweep_sdm.txt --expect-only')
    a('# Generated by: python Emulator\\tools\\isa\\ref_sweep_sdm.py --cases %s (SDM model, see its header).'
      % ','.join(parts))
    a('# Natively WRFSBASE/WRGSBASE and LFS/LGS would replace the host thread\'s FS/GS (GS base = the')
    a('# Windows TEB) and SYSENTER would enter the kernel, so these forms are checked against the SDM only.')
    a('#')
    a('# --- guards: the machine state the model assumes (CR4.LA57 = 0, CR4.FSGSBASE = 1%s)'
      % (', IA32_SYSENTER_CS = 10h' if 'sysenter' in parts else ''))
    a('mov rax, cr4; shr rax, 12; and eax, 1 =>! rax=0')
    a('mov rax, cr4; shr rax, 16; and eax, 1 =>! rax=1')
    if 'sysenter' in parts:
        a('mov ecx, 0x174; rdmsr | rax=0xFFFFFFFFFFFFFFFF rdx=0xFFFFFFFFFFFFFFFF => rax=%s rcx=0x174 rdx=0' % hx(SYSENTER_CS_RESET))
    for wr, rd in (('wrfsbase', 'rdfsbase'), ('wrgsbase', 'rdgsbase')) if 'fsgsbase' in parts else ():
        seg = wr[2:4].upper()
        a('#')
        a('# --- %s / %s: %s base := SRC; r64: #GP(0) unless 48-bit (paging) canonical; r32: zero-extended, no check'
          % (wr.upper(), rd.upper(), seg))
        for cpl in ('', 'cpl=3 '):
            for v in W64:
                try:
                    b = rdbase(wrbase(v, 64), 64)
                    a('%s rax; %s rcx | %srax=%s rcx=0x5A5A5A5A5A5A5A5A => rcx=%s' % (wr, rd, cpl, hx(v), hx(b)))
                except Fault as f:
                    a('%s rax; %s rcx | %srax=%s rcx=0x5A5A5A5A5A5A5A5A => %s' % (wr, rd, cpl, hx(v), f.name))
            for v in W32:
                b = rdbase(wrbase(v, 32), 64)
                a('%s eax; %s rcx | %srax=%s rcx=0x5A5A5A5A5A5A5A5A => rcx=%s' % (wr, rd, cpl, hx(v), hx(b)))
            # RD*BASE r32 of a 64-bit base; other registers
            v = 0xFFFF8000DEADBEEF
            a('%s rax; %s ecx | %srax=%s rcx=0x5A5A5A5A5A5A5A5A => rcx=%s' % (wr, rd, cpl, hx(v), hx(rdbase(wrbase(v, 64), 32))))
            v = 0x00001234567890AB
            a('%s r9; %s r10 | %sr9=%s r10=0x5A5A5A5A5A5A5A5A => r10=%s' % (wr, rd, cpl, hx(v), hx(rdbase(wrbase(v, 64), 64))))
            a('%s r15d; %s r14 | %sr15=0xFFFFFFFFFFFFFFFF r14=0x5A5A5A5A5A5A5A5A => r14=%s' % (wr, rd, cpl, hx(rdbase(wrbase(M64, 32), 64))))
        # two writes: the second one decides (a non-canonical second write faults after the first one)
        a('mov rbx, 0x1000; %s rbx; %s rax; %s rcx | rax=0x0000800000000000 rcx=0x5A5A5A5A5A5A5A5A => #GP rbx=0x1000'
          % (wr, wr, rd))
        a('mov rbx, 0x1000; %s rbx; %s rdx; %s rcx | rdx=0x3000 rcx=0x5A5A5A5A5A5A5A5A => rbx=0x1000 rcx=%s'
          % (wr, wr, rd, hx(rdbase(wrbase(0x3000, 64), 64))))
        # #UD: LOCK; CR4.FSGSBASE = 0 (CPL0: clear CR4 bit 16 first)
        modrm_wr = 0xD0 if wr == 'wrfsbase' else 0xD8
        modrm_rd = 0xC0 if rd == 'rdfsbase' else 0xC8
        for bytes_, what in (([0xF0, 0xF3, 0x48, 0x0F, 0xAE, modrm_wr], 'LOCK %s rax' % wr),
                             ([0xF0, 0xF3, 0x0F, 0xAE, modrm_rd], 'LOCK %s eax' % rd)):
            try:
                wrbase(0, 64, lock=True)
            except Fault as f:
                a('# %s: %s' % (what, f.name))
                a('.byte %s | rax=0x1000 => %s' % (', '.join('0x%02x' % b for b in bytes_), f.name))
        try:
            wrbase(0x1000, 64, fsgsbase=False)
        except Fault as f:
            a('mov rax, cr4; btr rax, 16; mov cr4, rax; %s rbx | rbx=0x1000 =>! %s' % (wr, f.name))
            a('mov rax, cr4; btr rax, 16; mov cr4, rax; %s rbx | rbx=0x1000 =>! %s' % (rd, f.name))
    if 'sysenter' in parts:
        a('#')
        a('# --- SYSENTER: #GP(0) when IA32_SYSENTER_CS[15:2] = 0 (CR4.FRED = 0); otherwise the transition')
        a('# (U902): IA32_SYSENTER_EIP = label k, IA32_SYSENTER_ESP = RSP; at k RBX = CS, RBP = SS')
        for cs in (0, 1, 2, 3, 4, 8, 0x10, 0xFFFC, 0xFFFF):
            if sysenter_faults(cs):
                a('mov ecx, 0x174; mov eax, %s; mov edx, 0; wrmsr; sysenter | rdx=0x77 => #GP rax=%s rcx=0x174 rdx=0'
                  % (hx(cs), hx(cs)))
            else:
                ncs, nss = sysenter_selectors(cs)
                a('mov ecx, 0x174; mov eax, %s; mov edx, 0; wrmsr; mov ecx, 0x176; lea rax, [rip + k]; '
                  'mov rdx, rax; shr rdx, 32; wrmsr; mov ecx, 0x175; mov rax, rsp; mov rdx, rax; shr rdx, 32; '
                  'wrmsr; sysenter; k: mov ebx, cs; mov ebp, ss | rdx=0x77 =>! rcx=0x175 rbx=%s rbp=%s'
                  % (hx(cs), hx(ncs), hx(nss)))
        assert not sysenter_faults(SYSENTER_CS_RESET)
    if 'lfslgs' in parts:
        a('#')
        a('# --- LFS / LGS r16/r32/r64, m16:16/m16:32/m16:64 at CPL3 (Windows GDT): DEST := offset, FS/GS := selector;')
        a('# the selector checks of the 64-bit mode pseudocode. RAX = all ones before (r16 keeps bits 63:16).')
    off = 0x8877665544332211
    for name, op in (('lfs', 0xB4), ('lgs', 0xB5)) if 'lfslgs' in parts else ():
        for opsize, pre in ((16, [0x66]), (32, []), (64, [0x48])):
            n = opsize // 8
            for sel in (0x2B, 0x00, 0x03, 0x23, 0x33, 0x28, 0x18, 0x1B, 0x10, 0x13, 0x08, 0x40, 0x2F):
                f = load_fsgs(sel, 3)
                enc = ', '.join('0x%02x' % b for b in pre + [0x0F, op, 0x06])
                mem = 'm+0x8000=%s%s' % (le(off, n), le(sel, 2))
                note = '  (the "both RPL and CPL > DPL" reading of the exception list would load it)' if sel == 0x18 else ''
                if note:
                    a('# %s r%d with selector 18h: CPL 3 > DPL 0, RPL 0 = DPL: #GP by the pseudocode%s' % (name, opsize, note))
                if f is None:
                    a('.byte %s | cpl=3 rax=0xFFFFFFFFFFFFFFFF %s => rax=%s' % (enc, mem, hx(lfs_dest(M64, off, opsize))))
                else:
                    a('.byte %s | cpl=3 rax=0xFFFFFFFFFFFFFFFF %s => %s' % (enc, mem, f.name))
    return out


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        if not cond:
            print('selftest FAILED:', what)
            ok = False
    # canonical edges, 48-bit
    check(canonical(0x00007FFFFFFFFFFF) and canonical(0xFFFF800000000000) and canonical(0) and canonical(M64), 'canonical 48 edges')
    check(not canonical(0x0000800000000000) and not canonical(0xFFFF7FFFFFFFFFFF) and not canonical(1 << 63), 'non-canonical 48')
    check(canonical(0x0000800000000000, 57) and not canonical(0x0100000000000000, 57), '57-bit canonical edges')
    # WR*BASE
    check(wrbase(0xFFFFFFFF89ABCDEF, 32) == 0x89ABCDEF, 'r32 zero-extends')
    check(wrbase(0x0000800000000000, 32) == 0, 'r32 never #GP')
    for v, exp in ((0x0000800000000000, '#GP'), (0x00007FFFFFFFFFFF, None)):
        try:
            wrbase(v, 64)
            got = None
        except Fault as f:
            got = f.name
        check(got == exp, 'wrbase r64 %x' % v)
    for kw in ({'lock': True}, {'fsgsbase': False}):
        try:
            wrbase(0, 64, **kw)
            check(False, 'wrbase #UD %s' % kw)
        except Fault as f:
            check(f.name == '#UD', 'wrbase #UD name')
    check(rdbase(0xFFFF8000DEADBEEF, 32) == 0xDEADBEEF, 'rdbase r32')
    # SYSENTER
    check(all(sysenter_faults(c) for c in (0, 1, 2, 3)) and not any(sysenter_faults(c) for c in (4, 8, 0x10, 0xFFFC)),
          'sysenter cs rule')
    check(sysenter_faults(8, pe=False) and not sysenter_faults(0, fred=True), 'sysenter PE / FRED')
    check(sysenter_selectors(0x13) == (0x10, 0x18) and sysenter_selectors(0xFFFF) == (0xFFFC, 4),
          'sysenter selectors (RPL dropped, SS wraps at 16 bits)')
    # LFS/LGS selector checks
    check(load_fsgs(0, 3) is None and load_fsgs(3, 3) is None, 'null selectors load')
    check(load_fsgs(0x2B, 3) is None and load_fsgs(0x28, 3) is None, 'data DPL3')
    check(load_fsgs(0x23, 3) is None and load_fsgs(0x33, 3) is None, 'readable code DPL3')
    for s in (0x18, 0x1B, 0x10, 0x13, 0x08, 0x40, 0x2F):
        check(isinstance(load_fsgs(s, 3), Fault), 'selector %x faults at CPL3' % s)
    check(load_fsgs(0x18, 0) is None and load_fsgs(0x10, 0) is None, 'DPL0 at CPL0')
    check(lfs_dest(M64, 0x8877665544332211, 16) == 0xFFFFFFFFFFFF2211 and
          lfs_dest(M64, 0x8877665544332211, 32) == 0x44332211 and
          lfs_dest(0, 0x8877665544332211, 64) == 0x8877665544332211, 'DEST := offset')
    n = len(lines())
    check(n > 100, 'case count %d' % n)
    print('ref_sweep_sdm selftest: %s (%d case-file lines)' % ('OK' if ok else 'FAILED', n))
    return ok


if __name__ == '__main__':
    good = self_test() if '--selftest' in sys.argv or '--cases' not in sys.argv else True
    if '--cases' in sys.argv:
        k = sys.argv.index('--cases') + 1
        parts = tuple(sys.argv[k].split(',')) if k < len(sys.argv) and not sys.argv[k].startswith('--') else PARTS
        bad = [p for p in parts if p not in PARTS]
        if bad:
            sys.exit('unknown part(s) %s; parts: %s' % (', '.join(bad), ','.join(PARTS)))
        sys.stdout.write('\n'.join(lines(parts)) + '\n')
    sys.exit(0 if good else 1)
