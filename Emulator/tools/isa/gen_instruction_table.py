"""Generate the "Instruction Table Implementation" section of PLAN_emulation.md.

One row per (mnemonic, encoding) of Emulator/data/isa_manual_forms.tsv (Intel SDM / XED forms),
status from the full emu-alltest run (alltest.csv, Unicorn vs the i5-13600K) plus the
hardware-verified ledger items that the sweep cannot express (CHANGES_LEDGER.md).

  ✅ implemented and identical to the i5-13600K (or the SDM where the CPU deviates: docs/quirks.md)
  ⏳ implemented, open item (difference under decision, CPL3 check in Phase 2, SDM vectors pending)
  ⬜ not implemented / not reachable yet

usage: python gen_instruction_table.py <alltest.csv> <isa_manual_forms.tsv> <out.md> [verified_forms.tsv]

verified_forms.tsv (default: next to isa_manual_forms.tsv) holds the hardware verdicts for rows the
sweep cannot reach or only reaches with a #UD representative (plan 1.15b, evidence in
Emulator\\data\\cases_reach.txt); it applies to rows the sweep alone leaves ⬜.
"""
import csv
import os
import re
import sys
from collections import OrderedDict, defaultdict

PREFIX_WORDS = {'rep', 'repe', 'repz', 'repne', 'repnz', 'lock', 'xacquire', 'xrelease', 'bnd', 'notrack', 'data16'}

# Hardware-verified or decided items the sweep alone cannot state (ledger rows).
OVERRIDES = {
    # mnemonic: (status, note) ; applies to every encoding of the mnemonic unless key is (mn, enc)
    'f2xm1': ('✅', 'U56 Goldmont-microcode model, 100% bit-exact (value + FSW)'),
    'fyl2x': ('✅', 'U56 microcode model, 100% bit-exact'),
    'fyl2xp1': ('✅', 'U56 microcode model, 100% bit-exact; x < -1: SDM #IA (U533; the CPU deviates, docs/quirks.md)'),
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
    'fcomi': ('✅', 'SDM C1 = 0 (U531; the CPU keeps C1: docs/quirks.md)'),
    'fcomip': ('✅', 'SDM C1 = 0 (U531, docs/quirks.md)'),
    'fucomi': ('✅', 'SDM C1 = 0 (U531, docs/quirks.md)'),
    'fucomip': ('✅', 'SDM C1 = 0 (U531, docs/quirks.md)'),
    'cvtpi2ps': ('✅', 'm64 form: SDM x87 transition + #MF (U532; the CPU deviates, docs/quirks.md)'),
    'ptwrite': ('✅', 'SDM #UD (CPUID.14 = 0, U534; the CPU executes it: docs/quirks.md)'),
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
    'rcpps': ('✅', 'U81 analytic Intel 12-bit model (RN of 1/midpoint); 2^32 inputs x 6 MXCSR == CPU'),
    'rcpss': ('✅', 'U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU'),
    'rsqrtps': ('✅', 'U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU'),
    'rsqrtss': ('✅', 'U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU'),
    'vrcpps': ('✅', 'U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU'),
    'vrcpss': ('✅', 'U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU'),
    'vrsqrtps': ('✅', 'U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU'),
    'vrsqrtss': ('✅', 'U81 analytic Intel 12-bit model; 2^32 inputs x 6 MXCSR == CPU'),
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


# XED / SDM name -> Capstone name for the same opcode (plan 1.15b)
NAME_ALIAS = {
    'fcmovnp': 'fcmovnu',   # DB D8+i (XED FCMOVNP, Capstone FCMOVNU)
    'fcmovp': 'fcmovu',     # DA D8+i
    'nop2': 'nop', 'nop3': 'nop', 'nop4': 'nop', 'nop5': 'nop', 'nop6': 'nop', 'nop7': 'nop',
    'nop8': 'nop', 'nop9': 'nop',   # XED length-specific names of 66 90 / 0F 1F /0
}

# verified_forms.tsv status -> (table status, note prefix)
VERIFIED_STATUS = {
    'ok': ('✅', 'identical to the i5-13600K (cases_reach)'),
    'sdm': ('✅', 'implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors'),
    'ud': ('⬜', 'not implemented (cases_reach)'),
    'cpl0': ('⏳', 'CPL0 instruction (cases_reach): CPL3 fault in Phase 2 (D6)'),
    'oos': ('⬜', 'out of scope'),
}


def load_verified(path):
    v = {}
    if not path or not os.path.exists(path):
        return v
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            line = line.rstrip('\r\n')
            if not line.strip() or line.startswith('#'):
                continue
            mn, enc, st, note = (line.split('\t') + ['', '', '', ''])[:4]
            if st not in VERIFIED_STATUS:
                raise SystemExit('verified_forms.tsv: unknown status %r for %s/%s' % (st, mn, enc))
            v[(mn.lower(), enc)] = (st, note)
    return v


def resolve(sweep, mn, enc):
    """sweep buckets for (mn, enc), trying XED -> Capstone name aliases; returns (buckets, alias)"""
    b = sweep.get((mn, enc))
    if b:
        return b, None
    alt = NAME_ALIAS.get(mn)
    if alt and (alt, enc) in sweep:
        return sweep[(alt, enc)], 'same opcode as %s (Capstone name)' % alt.upper()
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
    # U539: a documented i5-13600K deviation (docs/quirks.md) is the SDM behaviour, counted as identical
    m = b.get('match', 0) + b.get('known deviation (docs/quirks.md)', 0) + b.get('known deviation not observed (matches)', 0)
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


# ---- CPU support: does the i5-13600K (our CPU) execute the instruction? -------------------------
# Evaluated against the captured CPUID profile (Emulator/data/cpuid_i5-13600k.txt), SDM Vol2 CPUID
# bit numbering. Rows of an unsupported family are marked a cross (never a checkmark), whatever the
# emulator does; the row text still says whether the emulator implements it per the manual.
ALWAYS = {'I86', 'I186', 'I286PROTECTED', 'I286REAL', 'I386', 'I486', 'I486REAL', 'PENTIUMREAL', 'PPRO',
          'PPRO_UD0_LONG', 'PPRO_UD0_SHORT', 'LONGMODE', 'FAT_NOP', 'PAUSE', 'PREFETCH_NOP', 'RDPMC', '?'}
CPUID_BIT = {
    # family: (leaf, subleaf, reg 0=eax 1=ebx 2=ecx 3=edx, bit)
    'X87': (1, 0, 3, 0), 'FCOMI': (1, 0, 3, 15), 'FCMOV': (1, 0, 3, 15), 'CMOV': (1, 0, 3, 15),
    'SEP': (1, 0, 3, 11), 'CLFSH': (1, 0, 3, 19), 'PENTIUMMMX': (1, 0, 3, 23), 'FXSAVE': (1, 0, 3, 24),
    'FXSAVE64': (1, 0, 3, 24), 'SSE': (1, 0, 3, 25), 'SSEMXCSR': (1, 0, 3, 25), 'SSE_PREFETCH': (1, 0, 3, 25),
    'SSE2': (1, 0, 3, 26), 'SSE2MMX': (1, 0, 3, 26),
    'SSE3': (1, 0, 2, 0), 'SSE3X87': (1, 0, 2, 0), 'PCLMULQDQ': (1, 0, 2, 1), 'MONITOR': (1, 0, 2, 3),
    'VTX': (1, 0, 2, 5), 'VMFUNC': (1, 0, 2, 5), 'SMX': (1, 0, 2, 6), 'SSSE3': (1, 0, 2, 9), 'SSSE3MMX': (1, 0, 2, 9),
    'FMA': (1, 0, 2, 12), 'CMPXCHG16B': (1, 0, 2, 13), 'SSE4': (1, 0, 2, 19), 'SSE42': (1, 0, 2, 20),
    'MOVBE': (1, 0, 2, 22), 'POPCNT': (1, 0, 2, 23), 'AES': (1, 0, 2, 25), 'XSAVE': (1, 0, 2, 26),
    'AVX': (1, 0, 2, 28), 'AVXAES': (1, 0, 2, 25), 'F16C': (1, 0, 2, 29), 'RDRAND': (1, 0, 2, 30),
    'RDWRFSGS': (7, 0, 1, 0), 'SGX': (7, 0, 1, 2), 'SGX_ENCLV': (7, 0, 1, 2), 'BMI1': (7, 0, 1, 3), 'HLE': (7, 0, 1, 4),
    'AVX2': (7, 0, 1, 5), 'AVX2GATHER': (7, 0, 1, 5), 'BMI2': (7, 0, 1, 8), 'INVPCID': (7, 0, 1, 10),
    'RTM': (7, 0, 1, 11), 'MPX': (7, 0, 1, 14), 'AVX512F': (7, 0, 1, 16), 'AVX512DQ': (7, 0, 1, 17),
    'RDSEED': (7, 0, 1, 18), 'ADOX_ADCX': (7, 0, 1, 19), 'SMAP': (7, 0, 1, 20), 'AVX512_IFMA': (7, 0, 1, 21),
    'CLFLUSHOPT': (7, 0, 1, 23), 'CLWB': (7, 0, 1, 24), 'AVX512PF': (7, 0, 1, 26), 'AVX512ER': (7, 0, 1, 27),
    'AVX512CD': (7, 0, 1, 28), 'SHA': (7, 0, 1, 29), 'AVX512BW': (7, 0, 1, 30),
    'PREFETCHWT1': (7, 0, 2, 0), 'AVX512_VBMI': (7, 0, 2, 1), 'WAITPKG': (7, 0, 2, 5),
    'AVX512_VBMI2': (7, 0, 2, 6), 'CET': (7, 0, 2, 7), 'GFNI': (7, 0, 2, 8), 'AVX_GFNI': (7, 0, 2, 8),
    'VAES': (7, 0, 2, 9), 'VPCLMULQDQ': (7, 0, 2, 10), 'AVX512_VNNI': (7, 0, 2, 11), 'AVX512_BITALG': (7, 0, 2, 12),
    'AVX512_VPOPCNTDQ': (7, 0, 2, 14), 'RDPID': (7, 0, 2, 22), 'KEYLOCKER': (7, 0, 2, 23),
    'CLDEMOTE': (7, 0, 2, 25), 'MOVDIRI': (7, 0, 2, 27), 'MOVDIR64B': (7, 0, 2, 28), 'ENQCMD': (7, 0, 2, 29),
    'AVX512_4VNNIW': (7, 0, 3, 2), 'AVX512_4FMAPS': (7, 0, 3, 3), 'UINTR': (7, 0, 3, 5),
    'AVX512_VP2INTERSECT': (7, 0, 3, 8), 'SERIALIZE': (7, 0, 3, 14), 'TSX_LDTRK': (7, 0, 3, 16),
    'PCONFIG': (7, 0, 3, 18), 'AMX_BF16': (7, 0, 3, 22), 'AVX512_FP16': (7, 0, 3, 23), 'AMX_TILE': (7, 0, 3, 24),
    'AMX_INT8': (7, 0, 3, 25),
    'SHA512': (7, 1, 0, 0), 'SM3': (7, 1, 0, 1), 'SM4': (7, 1, 0, 2), 'RAO_INT': (7, 1, 0, 3),
    'AVX_VNNI': (7, 1, 0, 4), 'AVX512_BF16': (7, 1, 0, 5), 'CMPCCXADD': (7, 1, 0, 7), 'FRED': (7, 1, 0, 17),
    'LKGS': (7, 1, 0, 18), 'WRMSRNS': (7, 1, 0, 19), 'AMX_FP16': (7, 1, 0, 21), 'HRESET': (7, 1, 0, 22),
    'AVX_IFMA': (7, 1, 0, 23), 'MSRLIST': (7, 1, 0, 27), 'MOVRS': (7, 1, 0, 31),
    'PBNDKB': (7, 1, 1, 1), 'MSR_IMM': (7, 1, 2, 5),
    'AVX_VNNI_INT8': (7, 1, 3, 4), 'AVX_NE_CONVERT': (7, 1, 3, 5), 'AMX_COMPLEX': (7, 1, 3, 8),
    'AVX_VNNI_INT16': (7, 1, 3, 10), 'ICACHE_PREFETCH': (7, 1, 3, 14), 'USER_MSR': (7, 1, 3, 15),
    'XSAVEOPT': (0xd, 1, 0, 0), 'XSAVEC': (0xd, 1, 0, 1), 'XSAVES': (0xd, 1, 0, 3),
    'PTWRITE': (0x14, 0, 1, 4), 'KEYLOCKER_WIDE': (0x19, 0, 1, 2),
    'LAHF': (0x80000001, 0, 2, 0), 'LZCNT': (0x80000001, 0, 2, 5), 'RDTSCP': (0x80000001, 0, 3, 27),
    'WBNOINVD': (0x80000008, 0, 1, 9),
}
CPU_FIXED = {
    'PTWRITE': (True, 'CPUID.14H reports it absent, but the CPU executes it (documented deviation, docs/quirks.md; the emulator #UDs, U534)'),
}
CPU_NOT = {
    '3DNOW': 'AMD 3DNow!', 'SSE4a': 'AMD SSE4a', 'XOP': 'AMD XOP', 'FMA4': 'AMD FMA4', 'TBM': 'AMD TBM',
    'LWP': 'AMD LWP', 'CLZERO': 'AMD CLZERO', 'MONITORX': 'AMD MONITORX', 'RDPRU': 'AMD RDPRU',
    'MCOMMIT': 'AMD MCOMMIT', 'AMD': 'AMD-only', 'AMD_INVLPGB': 'AMD INVLPGB', 'SVM': 'AMD SVM',
    'SNP': 'AMD SEV-SNP', 'VIA_PADLOCK_AES': 'VIA PadLock', 'VIA_PADLOCK_SHA': 'VIA PadLock',
    'VIA_PADLOCK_RNG': 'VIA PadLock', 'VIA_PADLOCK_MONTMUL': 'VIA PadLock', 'ACE_1': 'VIA/Zhaoxin ACE',
    'TDX': 'Intel TDX (server, VMX root only)', 'IBHF': 'not reported by this CPU',
}
OS_OFF = {
    'PKU': 'the CPU has PKU but Windows leaves CR4.PKE off: RDPKRU/WRPKRU #UD in user mode',
}
CET_USER_OK = {'endbr32', 'endbr64', 'rdsspd', 'rdsspq'}


def load_cpuid(path):
    regs = {}
    try:
        for line in open(path, encoding='utf-8'):
            t = line.split()
            if len(t) == 6 and len(t[0]) == 8 and all(len(x) == 8 for x in t[2:6]):
                regs[(int(t[0], 16), int(t[1], 16))] = [int(x, 16) for x in t[2:6]]
    except OSError:
        pass
    return regs


def family_on_cpu(fam, cpuid):
    if fam in CPU_FIXED:
        return CPU_FIXED[fam]
    if fam in ALWAYS:
        return True, ''
    if fam in CPU_NOT:
        return False, CPU_NOT[fam]
    if fam in OS_OFF:
        return False, OS_OFF[fam]
    bit = CPUID_BIT.get(fam)
    if bit is None:
        best = None
        for k in CPUID_BIT:
            if fam.startswith(k + '_') and (best is None or len(k) > len(best)):
                best = k
        bit = CPUID_BIT.get(best) if best else None
    if bit is None:
        return False, '%s not reported by this CPU' % fam
    leaf, sub, reg, b = bit
    v = cpuid.get((leaf, sub))
    if v and (v[reg] >> b) & 1:
        return True, ''
    return False, 'CPUID.%XH%s:%s[%d] = 0 on this CPU' % (leaf, ('.%d' % sub) if sub else '',
                                                           ['EAX', 'EBX', 'ECX', 'EDX'][reg], b)


def row_on_cpu(mn, isas, cpuid):
    fams = set()
    for isa in isas:
        for f in isa.split('+'):
            fams.add(family(f))
    reasons = []
    for f in sorted(fams):
        ok, why = family_on_cpu(f, cpuid)
        if ok:
            if f == 'CET' and mn not in CET_USER_OK:
                return False, 'the CPU has CET but Windows does not enable shadow stacks for our process: #UD/#GP in user mode'
            return True, why
        reasons.append(why)
    return False, '; '.join(r for r in reasons if r)


def main():
    sweep = load_sweep(sys.argv[1])
    vpath = sys.argv[4] if len(sys.argv) > 4 else os.path.join(os.path.dirname(os.path.abspath(sys.argv[2])),
                                                               'verified_forms.tsv')
    verified = load_verified(vpath)
    vcount = defaultdict(int)
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
    cpuid = load_cpuid(os.path.join(os.path.dirname(os.path.abspath(sys.argv[2])), 'cpuid_i5-13600k.txt'))
    impl_unsup = defaultdict(int)
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
        if st == '⬜' and (mn, enc) in verified:
            vst, vnote = verified[(mn, enc)]
            st, note = VERIFIED_STATUS[vst][0], VERIFIED_STATUS[vst][1] + (': ' + vnote if vnote else '')
            vcount[vst] += 1
        on_cpu, why = row_on_cpu(mn, r['isa'], cpuid)
        if not on_cpu:
            impl = {'✅': 'implemented per the manual (SDM-vector verified)',
                    '⏳': 'implemented per the manual, open item',
                    '⬜': 'not implemented yet'}[st]
            impl_unsup[st] += 1
            note = '**NOT SUPPORTED on our i5-13600K** (%s) — %s: %s' % (why or 'not reported by this CPU', impl, note)
            st = '❌'
        totals[st] += 1
        vl = '/'.join(sorted(r['vl'], key=lambda v: int(v) if v.isdigit() else 0)) or '-'
        by_family[fam].append((mn, enc, vl, '+'.join(sorted(r['isa'])), st, note))

    out = []
    out.append('## Instruction Table Implementation\n')
    out.append('Every instruction form in the Intel SDM / XED list (`Emulator\\data\\isa_manual_forms.tsv`), one row per '
               'mnemonic and encoding. Status: ✅ implemented and identical to the i5-13600K (or the SDM where the '
               'CPU deviates, docs/quirks.md), ⏳ implemented with an open item, ⬜ not implemented / not '
               'reachable yet. Generated by `NoVmp\\Emulator\\tools\\isa\\gen_instruction_table.py` from the full '
               'emu-alltest run (`--full`, 13,504 Capstone forms vs the CPU) plus the ledger; regenerate after every '
               'fix.\n')
    out.append('**Legend:** ✅ runs on our i5-13600K and the emulator is identical to it · ⏳ runs on our CPU, the emulator has an open item · ⬜ runs on our CPU (or is reachable) but not implemented / not verified yet · **❌ NOT SUPPORTED on our i5-13600K**: the CPU does not execute it (CPUID bit clear, AMD/VIA-only, or OS-disabled), so it can never be checked against our hardware; it is still implemented from the manual where possible, and the row says whether it is implemented (SDM-vector verified) or not yet.\n')
    out.append('- ✅ %d  ·  ⏳ %d  ·  ⬜ %d  ·  ❌ %d not supported on our CPU (implemented per the manual: %d, open item: %d, not implemented yet: %d) — %d mnemonic/encoding rows\n' % (
        totals['✅'], totals['⏳'], totals['⬜'], totals['❌'], impl_unsup['✅'], impl_unsup['⏳'], impl_unsup['⬜'], sum(totals.values())))
    if verified:
        out.append('- Rows the sweep cannot reach, verified one by one against the i5-13600K '
                   '(`Emulator\\data\\cases_reach.txt` → `Emulator\\data\\verified_forms.tsv`): '
                   '✅ identical %d  ·  ⬜ #UD in both (CPU lacks it / not enabled) %d  ·  '
                   '⏳ CPL0 %d  ·  ⬜ out of scope (AMD / VIA / non-SDM) %d\n' % (
                       vcount['ok'], vcount['ud'], vcount['cpl0'], vcount['oos']))
    out.append('### Microcode, firmware-modelled and decided behaviour\n')
    for line in MICROCODE:
        out.append(line)
    out.append('')
    out.append('### Encoding / decode validity (whole-map sweeps vs the CPU)\n')
    for line in DECODE:
        out.append(line)
    out.append('')
    fam_order = sorted(by_family, key=lambda f: (f.startswith(('AVX512', 'AVX10', 'APX', 'AMX')), f))
    # first everything the i5-13600K runs, then (own section) everything it cannot run
    for fam in fam_order:
        items = [t for t in sorted(by_family[fam], key=lambda t: (t[0], ENC_ORDER.get(t[1], 9))) if t[4] != '❌']
        if not items:
            continue
        c = defaultdict(int)
        for it in items:
            c[it[4]] += 1
        out.append('### %s — ✅ %d · ⏳ %d · ⬜ %d\n' % (fam, c['✅'], c['⏳'], c['⬜']))
        out.append('| | instruction | encoding | vector bits | ISA | status |')
        out.append('|---|---|---|---|---|---|')
        for mn, enc, vl, isa, st, note in items:
            out.append('| %s | %s | %s | %s | %s | %s |' % (st, mn.upper(), enc, vl, isa, note))
        out.append('')
    out.append("### Instructions that can't be supported for now:\n")
    out.append('**❌ %d rows the i5-13600K cannot honestly run** (CPUID bit clear, AMD/VIA-only, or OS-disabled) — never '
               'marked as supported by our CPU. Column **CPU cannot support** = why; **emulator** = implemented per the '
               'manual (SDM-vector verified) / open item / not implemented yet.\n' % totals['❌'])
    for fam in fam_order:
        items = [t for t in sorted(by_family[fam], key=lambda t: (t[0], ENC_ORDER.get(t[1], 9))) if t[4] == '❌']
        if not items:
            continue
        out.append('#### %s — ❌ %d\n' % (fam, len(items)))
        out.append('| | instruction | encoding | vector bits | ISA | **CPU cannot support** | emulator |')
        out.append('|---|---|---|---|---|---|---|')
        for mn, enc, vl, isa, st, note in items:
            n = note.replace('**', '')
            m = re.search(r'NOT SUPPORTED on our i5-13600K \((.*?)\) — (.*)', n)
            why, emu = (m.group(1), m.group(2)) if m else ('not reported by this CPU', n)
            out.append('| ❌ | %s | %s | %s | %s | ❌ **cannot run** (%s) | %s |' % (mn.upper(), enc, vl, isa, why, emu))
        out.append('')
    open(sys.argv[3], 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    # machine-readable copy for gen_status_docs.py (README / docs)
    import json
    rows_json = []
    for fam in fam_order:
        for mn, enc, vl, isa, st, note in sorted(by_family[fam], key=lambda t: (t[0], ENC_ORDER.get(t[1], 9))):
            rows_json.append({'family': fam, 'mnemonic': mn, 'encoding': enc, 'vl': vl, 'isa': isa,
                              'status': st, 'note': note,
                              'vendor': CPU_NOT.get(fam, '') if fam in CPU_NOT and fam != 'TDX' and fam != 'IBHF' else ''})
    json.dump({'totals': dict(totals), 'unsupported_impl': dict(impl_unsup), 'rows': rows_json},
              open(os.path.splitext(sys.argv[3])[0] + '.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print('rows', sum(totals.values()), dict(totals), 'families', len(by_family), 'verified', dict(vcount), 'unsupported', dict(impl_unsup))


MICROCODE = [
    '- ✅ x87 transcendentals F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN, FSIN, FCOS, FSINCOS — Goldmont MSROM micro-op sequences in exact dyadic arithmetic (U56); 15,962,504 hardware rows, 0 mismatches (value + FSW).',
    '- ✅ FPREM / FPREM1 / FSCALE — ROM routine model == hardware 148/148; fork == hardware 388/388.',
    '- ✅ FXTRACT / FRNDINT / FBSTP / FBLD — ROM routines decoded; fork matches hardware (U28, invalid BCD).',
    '- ✅ FSQRT / FCOM / FXAM / FIST — hardware uops (not MSROM); fork matches hardware.',
    '- ✅ x87 environment / save images — FNSTENV/FNSAVE mask afterwards (U61), reserved FFFF (U62), FNSAVE REX.W (U63), FIP/FOP/FDP + FCS/FDS deprecation model (U64), no FIP update by FNSTSW AX / DB E0, E1, E4 and FIP update by FNOP like the CPU (U90).',
    '- ✅ x87 condition codes — trig stack fault clears C2 (U57), F2XM1(±0) clears C1 (U58), special operands and unmasked exceptions (U54).',
    '- ✅ XSAVE family — XSAVE/XSAVEOPT/XRSTOR match; XSAVEC + compacted XRSTOR (U66); XSAVES/XRSTORS ⏳ CPL3 in Phase 2.',
    '- ✅ String microcode — REP + 67h with ECX = 0 writes nothing (SDM, U536; the CPU zero-extends RCX/RSI/RDI: docs/quirks.md).',
    '- ✅ RDRAND / RDSEED — host entropy (RtlGenRandom), flags per SDM (U65).',
    '- ✅ CPUID — i5-13600K profile (94 rows), override table + strict #UD switch (U68).',
    '- ✅ #XM — unmasked SSE exceptions with CR4.OSXMMEXCPT (U67).',
    '- ✅ RCPPS / RSQRTPS / RCPSS / RSQRTSS (+ VEX) — F10 solved (U81): RN(1/midpoint) over the top 11 mantissa bits, RN(1/sqrt(midpoint)) over the top 10 bits + exponent parity, 12 fraction bits, exact integer arithmetic; all 2^32 inputs x 6 MXCSR settings identical to the CPU.',
    '- ✅ Pure SDM (U531-U539, user decision 2026-10-08): no hardware-quirk switch; the i5-13600K deviations (FCOMI C1, CVTPI2PS m64, FYL2XP1 below −1, PTWRITE, DPPD two NaN products, REP 67h ECX=0, x87 compare unmasked #IA, DPPS step grouping) are documented in docs/quirks.md and tagged in the hardware case files.',
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
