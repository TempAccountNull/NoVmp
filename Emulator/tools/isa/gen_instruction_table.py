"""Generate the "Instruction Table Implementation" section of PLAN_emulation.md.

One row per (mnemonic, encoding) of Emulator/data/isa_manual_forms.tsv (Intel SDM / XED forms),
status from the full emu-alltest run (alltest.csv, Unicorn vs the i5-13600K) plus the
hardware-verified ledger items that the sweep cannot express (CHANGES_LEDGER.md).

  ✅ implemented and identical to the i5-13600K (or decided manual behaviour + quirk bit)
  ⏳ implemented, open item (difference under decision, CPL3 check in Phase 2, SDM vectors pending)
  ⬜ not implemented / not reachable yet

usage: python gen_instruction_table.py <alltest.csv> <isa_manual_forms.tsv> <out.md>
"""
import csv
import re
import sys
from collections import OrderedDict, defaultdict

PREFIX_WORDS = {'rep', 'repe', 'repz', 'repne', 'repnz', 'lock', 'xacquire', 'xrelease', 'bnd', 'notrack', 'data16'}

# Hardware-verified or decided items the sweep alone cannot state (ledger rows).
OVERRIDES = {
    # mnemonic: (status, note) ; applies to every encoding of the mnemonic unless key is (mn, enc)
    'f2xm1': ('✅', 'U56 Goldmont-microcode model, 100% bit-exact (value + FSW)'),
    'fyl2x': ('✅', 'U56 microcode model, 100% bit-exact'),
    'fyl2xp1': ('✅', 'U56 microcode model, 100% bit-exact; x < -1: manual #IA, quirk bit 2 = hardware'),
    'fptan': ('✅', 'U56 microcode model, 100% bit-exact'),
    'fpatan': ('✅', 'U56 microcode model, 100% bit-exact'),
    'fsin': ('✅', 'U56 microcode model, 100% bit-exact'),
    'fcos': ('✅', 'U56 microcode model, 100% bit-exact'),
    'fsincos': ('✅', 'U56 microcode model, 100% bit-exact'),
    'fprem': ('✅', 'ROM model == hw 148/148, fork == hw 388/388'),
    'fprem1': ('✅', 'ROM model == hw, fork == hw'),
    'fscale': ('✅', 'ROM model == hw, fork == hw'),
    'fnstenv': ('✅', 'U61/U62/U64 (FCW masked after, reserved FFFF, FIP/FOP/FDP model)'),
    'fstenv': ('✅', 'U61/U62/U64'),
    'fnsave': ('✅', 'U61-U64'),
    'fsave': ('✅', 'U61-U64'),
    'fldenv': ('✅', 'U64'),
    'frstor': ('✅', 'U63/U64'),
    'fxsave': ('✅', 'U64 (FOP/FIP/FDP, REX.W layout)'),
    'fxsave64': ('✅', 'U64'),
    'fxrstor': ('✅', 'U64'),
    'fxrstor64': ('✅', 'U64'),
    'fcomi': ('✅', 'manual C1=0 default, quirk bit 0 = hardware (U38)'),
    'fcomip': ('✅', 'quirk bit 0 (U38)'),
    'fucomi': ('✅', 'quirk bit 0 (U38)'),
    'fucomip': ('✅', 'quirk bit 0 (U38)'),
    'cvtpi2ps': ('✅', 'm64 form: manual transition default, quirk bit 1 = hardware (U44/U50)'),
    'ptwrite': ('✅', 'U80: SDM #UD default (CPUID.14 = 0), quirk bit 3 = hardware (operand read)'),
    'xsavec': ('✅', 'U66 compacted format'),
    'xsavec64': ('✅', 'U66'),
    'xsaves': ('⏳', 'CPL0: #GP at CPL3 in Phase 2 (D6); compacted format shared with U66'),
    'xsaves64': ('⏳', 'CPL0: Phase 2 (D6)'),
    'xrstors': ('⏳', 'CPL0: Phase 2 (D6)'),
    'xrstors64': ('⏳', 'CPL0: Phase 2 (D6)'),
    'rdrand': ('✅', 'U65 host entropy (RtlGenRandom), CF/flags per SDM'),
    'rdseed': ('✅', 'U65'),
    'cpuid': ('✅', 'U68 i5-13600K profile + UC_CTL_X86_CPUID(_STRICT); only per-core APIC IDs vary'),
    'serialize': ('✅', 'U74, identical to the CPU'),
    'movdiri': ('✅', 'U72, identical to the CPU'),
    'movdir64b': ('✅', 'U73, identical to the CPU'),
    'gf2p8mulb': ('✅', 'U70, identical to the CPU'),
    'gf2p8affineqb': ('✅', 'U70'),
    'gf2p8affineinvqb': ('✅', 'U70'),
    'vgf2p8mulb': ('✅', 'U70 (VEX); EVEX not implemented'),
    'vgf2p8affineqb': ('✅', 'U70 (VEX); EVEX not implemented'),
    'vgf2p8affineinvqb': ('✅', 'U70 (VEX); EVEX not implemented'),
    'vpclmulqdq': ('✅', 'U69 (VEX.128/256)'),
    'vpdpbusd': ('✅', 'U71 AVX-VNNI (VEX)'),
    'vpdpbusds': ('✅', 'U71'),
    'vpdpwssd': ('✅', 'U71'),
    'vpdpwssds': ('✅', 'U71'),
    'rcpps': ('⏳', 'F10: approximation table reverse-engineering in progress (exact division today, within SDM bound)'),
    'rcpss': ('⏳', 'F10'),
    'rsqrtps': ('⏳', 'F10'),
    'rsqrtss': ('⏳', 'F10'),
    'vrcpps': ('⏳', 'F10'),
    'vrcpss': ('⏳', 'F10'),
    'vrsqrtps': ('⏳', 'F10'),
    'vrsqrtss': ('⏳', 'F10'),
    'invpcid': ('⏳', 'CPL0: #GP at CPL3 in Phase 2 (D6); #UD today'),
    'hreset': ('⏳', 'CPU: #GP at CPL3 even with CPUID bit 0 (Phase 2, D6)'),
    'rdtsc': ('⏳', 'TSC determinism hook: Phase 2'),
    'rdtscp': ('⏳', 'TSC/TSC_AUX: Phase 2'),
    'rdpid': ('⏳', 'TSC_AUX value: Phase 2 environment'),
    'rdgsbase': ('⏳', 'value = TEB base: Phase 2 environment'),
    'sgdt': ('⏳', 'values: Phase 2 environment'),
    'sidt': ('⏳', 'values: Phase 2 environment'),
    'sldt': ('⏳', 'values: Phase 2 environment'),
    'str': ('⏳', 'values: Phase 2 environment'),
    'smsw': ('⏳', 'CR0 value: Phase 2 environment'),
}


CC_SYN = {'z': 'e', 'nz': 'ne', 'nbe': 'a', 'nb': 'ae', 'nae': 'b', 'c': 'b', 'nc': 'ae', 'na': 'be',
          'nle': 'g', 'nl': 'ge', 'nge': 'l', 'ng': 'le', 'pe': 'p', 'po': 'np'}
CC_PREFIX = ('fcmov', 'cmov', 'set', 'j')

# Not encodable in 64-bit mode: #UD there (one-byte-map sweep, identical to the CPU); the
# sample is x64, so 16/32-bit-mode semantics are out of scope.
INVALID_64 = {'aaa', 'aad', 'aam', 'aas', 'daa', 'das', 'bound', 'into', 'arpl', 'salc', 'lds', 'les',
              'pusha', 'pushaw', 'pushal', 'pushad', 'popa', 'popaw', 'popal', 'popad',
              'pushfd', 'pushfl', 'popfd', 'popfl', 'jcxz', 'callf_ptr', 'jmpf_ptr'}


def resolve(sweep, mn, enc):
    """sweep buckets for (mn, enc), trying XED -> Capstone name aliases; returns (buckets, alias)"""
    b = sweep.get((mn, enc))
    if b:
        return b, None
    m = re.fullmatch(r'(v?cmp)([a-z_]+?)(ps|pd|ss|sd)', mn)
    if m and m.group(2) not in ('', 'x'):
        base = m.group(1) + m.group(3)
        if (base, enc) in sweep:
            return sweep[(base, enc)], '%s imm8 predicate alias' % base.upper()
    for pre in CC_PREFIX:
        if mn.startswith(pre) and mn[len(pre):] in CC_SYN:
            alt = pre + CC_SYN[mn[len(pre):]]
            if (alt, enc) in sweep:
                return sweep[(alt, enc)], 'same opcode as %s' % alt.upper()
    return None, None

ENC_ORDER = {'legacy': 0, 'vex': 1, 'xop': 2, 'evex': 3}


def family(isa):
    f = isa.split('+')[0]
    f = re.sub(r'_(128N?|256|512|SCALAR|KOP[BWDQ])$', '', f)
    return f or '?'


def load_sweep(path):
    per = defaultdict(lambda: defaultdict(int))
    with open(path, newline='', encoding='utf-8') as fh:
        for row in csv.DictReader(fh):
            words = row['form'].split()
            while words and words[0] in PREFIX_WORDS:
                words = words[1:]
            if not words:
                continue
            mn = words[0].lower()
            cls = 'legacy' if row['class'] == 'x87' else row['class']
            per[(mn, cls)][row['bucket']] += 1
    return per


def status_from(b):
    n = sum(b.values())
    m = b.get('match', 0)
    diff = b.get('differs', 0) + b.get('unicorn-#UD (hw runs it)', 0)
    priv = sum(v for k, v in b.items() if k.startswith('privileged'))
    sdm = b.get('host lacks, unicorn runs (needs SDM check)', 0) + \
        b.get('not native-safe, unicorn runs (needs SDM check)', 0)
    ud = b.get('host lacks + unicorn #UD', 0) + b.get('not native-safe, unicorn #UD', 0)
    err = b.get('harness error', 0)
    if diff:
        return '⏳', 'differs from the CPU on %d of %d forms' % (diff, n)
    if priv:
        return '⏳', 'CPL0 instruction (%d forms): CPL3 fault check in Phase 2 (D6)' % priv
    if err:
        return '⏳', 'harness error on %d forms' % err
    if m == n:
        return '✅', 'identical to the i5-13600K (%d forms)' % n
    if ud == n:
        return '⬜', 'not implemented (%d forms #UD; the i5-13600K lacks it)' % n
    if sdm and not ud:
        return '⏳', 'implemented; %d forms not runnable on the i5-13600K (CPU lacks it, or a memory form on a random base): SDM-vector check pending%s' % (
            sdm, ', %d identical' % m if m else '')
    parts = []
    if m:
        parts.append('%d identical' % m)
    if sdm:
        parts.append('%d SDM-vector check pending' % sdm)
    if ud:
        parts.append('%d not implemented' % ud)
    return '⏳', 'partial: ' + ', '.join(parts)


def main():
    sweep = load_sweep(sys.argv[1])
    rows = OrderedDict()
    with open(sys.argv[2], encoding='utf-8') as fh:
        for line in fh:
            if not line.strip() or line.startswith('#'):
                continue
            mn, enc, vlen, isa, flags = (line.rstrip('\n').split('\t') + ['', '', '', '', ''])[:5]
            key = (mn.lower(), enc)
            r = rows.setdefault(key, {'isa': set(), 'vl': set(), 'cpl0': False})
            r['isa'].add(isa)
            if vlen and vlen != '0':
                r['vl'].add(vlen)
            try:
                if int(flags) & 1:
                    r['cpl0'] = True
            except ValueError:
                pass

    by_family = defaultdict(list)
    totals = defaultdict(int)
    for (mn, enc), r in rows.items():
        fam = family(sorted(r['isa'])[0])
        swept_enc = 'vex' if enc == 'xop' else enc
        b, alias = resolve(sweep, mn, swept_enc)
        if mn in INVALID_64 and enc == 'legacy':
            st, note = '✅', '#UD in 64-bit mode, identical to the CPU (one-byte-map sweep); 16/32-bit modes out of scope (x64 sample)'
        elif re.fullmatch(r'cmpn?[a-z]{1,2}xadd', mn) and enc == 'vex' and 'APX' not in ''.join(r['isa']):
            st, note = '⏳', 'implemented (QEMU 7.2 CMPccXADD + U14 port fixes); not on the i5-13600K and no Capstone form: SDM-vector test pending (Phase 3/5)'
        elif mn in OVERRIDES and enc == 'evex' and (not b or all(k == 'host lacks + unicorn #UD' for k in b)):
            st, note = '⬜', 'EVEX form not implemented (the i5-13600K lacks AVX-512)'
        elif mn in OVERRIDES:
            st, note = OVERRIDES[mn]
        elif b:
            st, note = status_from(b)
        elif r['cpl0']:
            st, note = '⏳', 'CPL0 instruction, not reachable by the sweep: CPL3 fault in Phase 2 (D6)'
        else:
            st, note = '⬜', 'not reachable by the sweep yet (no Capstone form; decoder plan 5.3/5.3b)'
        if alias and st != '⬜':
            note += ' — ' + alias
        totals[st] += 1
        vl = '/'.join(sorted(r['vl'], key=lambda v: int(v) if v.isdigit() else 0)) or '-'
        by_family[fam].append((mn, enc, vl, '+'.join(sorted(r['isa'])), st, note))

    out = []
    out.append('## Instruction Table Implementation\n')
    out.append('Every instruction form in the Intel SDM / XED list (`Emulator\\data\\isa_manual_forms.tsv`), one row per '
               'mnemonic and encoding. Status: ✅ implemented and identical to the i5-13600K (or the decided manual '
               'behaviour with a quirk bit for the CPU), ⏳ implemented with an open item, ⬜ not implemented / not '
               'reachable yet. Generated by `NoVmp\\Emulator\\tools\\isa\\gen_instruction_table.py` from the full '
               'emu-alltest run (`--full`, 13,504 Capstone forms vs the CPU) plus the ledger; regenerate after every '
               'fix.\n')
    out.append('- ✅ %d  ·  ⏳ %d  ·  ⬜ %d  (of %d mnemonic/encoding rows)\n' % (
        totals['✅'], totals['⏳'], totals['⬜'], sum(totals.values())))
    out.append('### Microcode, firmware-modelled and decided behaviour\n')
    for line in MICROCODE:
        out.append(line)
    out.append('')
    out.append('### Encoding / decode validity (whole-map sweeps vs the CPU)\n')
    for line in DECODE:
        out.append(line)
    out.append('')
    fam_order = sorted(by_family, key=lambda f: (f.startswith(('AVX512', 'AVX10', 'APX', 'AMX')), f))
    for fam in fam_order:
        items = sorted(by_family[fam], key=lambda t: (t[0], ENC_ORDER.get(t[1], 9)))
        c = defaultdict(int)
        for it in items:
            c[it[4]] += 1
        out.append('### %s — ✅ %d · ⏳ %d · ⬜ %d\n' % (fam, c['✅'], c['⏳'], c['⬜']))
        out.append('| | instruction | encoding | vector bits | ISA | status |')
        out.append('|---|---|---|---|---|---|')
        for mn, enc, vl, isa, st, note in items:
            out.append('| %s | %s | %s | %s | %s | %s |' % (st, mn.upper(), enc, vl, isa, note))
        out.append('')
    open(sys.argv[3], 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print('rows', sum(totals.values()), dict(totals), 'families', len(by_family))


MICROCODE = [
    '- ✅ x87 transcendentals F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN, FSIN, FCOS, FSINCOS — Goldmont MSROM micro-op sequences in exact dyadic arithmetic (U56); 15,962,504 hardware rows, 0 mismatches (value + FSW).',
    '- ✅ FPREM / FPREM1 / FSCALE — ROM routine model == hardware 148/148; fork == hardware 388/388.',
    '- ✅ FXTRACT / FRNDINT / FBSTP / FBLD — ROM routines decoded; fork matches hardware (U28, invalid BCD).',
    '- ✅ FSQRT / FCOM / FXAM / FIST — hardware uops (not MSROM); fork matches hardware.',
    '- ✅ x87 environment / save images — FNSTENV/FNSAVE mask afterwards (U61), reserved FFFF (U62), FNSAVE REX.W (U63), FIP/FOP/FDP + FCS/FDS deprecation model (U64).',
    '- ✅ x87 condition codes — trig stack fault clears C2 (U57), F2XM1(±0) clears C1 (U58), special operands and unmasked exceptions (U54).',
    '- ✅ XSAVE family — XSAVE/XSAVEOPT/XRSTOR match; XSAVEC + compacted XRSTOR (U66); XSAVES/XRSTORS ⏳ CPL3 in Phase 2.',
    '- ✅ String microcode — REP + 67h with ECX = 0 zero-extends RCX/RSI/RDI (U60).',
    '- ✅ RDRAND / RDSEED — host entropy (RtlGenRandom), flags per SDM (U65).',
    '- ✅ CPUID — i5-13600K profile (94 rows), override table + strict #UD switch (U68).',
    '- ✅ #XM — unmasked SSE exceptions with CR4.OSXMMEXCPT (U67).',
    '- ⏳ RCPPS / RSQRTPS / RCPSS / RSQRTSS (+ VEX) — F10: reverse-engineering the 12-bit approximation now (exhaustive capture done; result = 12 significant bits, mantissa-only + exponent parity, denormal in → ±∞, denormal out → 0).',
    '- ✅ Decided manual-vs-hardware quirks (UC_CTL_X86_HW_QUIRKS, default = manual): bit 0 FCOMI keeps C1, bit 1 CVTPI2PS m64 keeps x87, bit 2 FYL2XP1 below −1, bit 3 PTWRITE executes (U80).',
    '- ⏳ RDTSC / RDTSCP determinism hook — Phase 2 (emulator side).',
    '- ⬜ Microcode-assisted CPL0 paths (VMX, SMX, SGX, SMM) — exact CPL3 faults only (D6), Phase 2/5.',
]

DECODE = [
    '- ✅ 0F AE group 15 mandatory prefixes (U75).',
    '- ✅ 64-bit near branches ignore 66h (U76).',
    '- ✅ 0F 0D / 0F 18 register forms NOP; MOVNTI / EMMS NP; MOVNTQ / MOVNTDQ memory only (U77).',
    '- ✅ Group 11 (C6/C7) reg ≠ 0 #UD (U78).',
    '- ✅ VEX validity: MMX-only forms, 38/3A escapes, VZEROUPPER pp, scalar VEX.LIG, reserved vvvv, VEX128 (U79).',
    '- ✅ Sweeps with 0 decode differences: 0F map 2,855 · one-byte map 2,108 · 0F38/0F3A 5,120 · VEX C4 49,152 · VEX C5 + prefixed VEX 8,190 · EVEX/XOP 9.',
    '- ⏳ CPL3-only faults (LLDT, LTR, LGDT, LIDT, LMSW, CLTS, INVD, WBINVD, MOV CR/DR, RDMSR, WRMSR, RDPMC, SWAPGS, CLAC, STAC, MONITOR, MWAIT, IN/OUT/INS/OUTS, HLT, CLI, STI, INVPCID, WRUSS, XSAVES, XRSTORS, HRESET) — Phase 2 (D6).',
    '- ✅ Undefined flags (MUL/IMUL SF ZF AF PF, SHLD/SHRD/rotates OF AF for count > 1, ANDN/BEXTR PF AF SF) — SDM "undefined", masked by the sweep (manual wins).',
]

main()
