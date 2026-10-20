#!/usr/bin/env python3
"""check_instruction_table.py - suite instruction_table_check (plan 1.J.57.a, ledger U1790-U1792).

Checks the per-profile labels of the cross section ("Instructions that can't be supported for now", the ❌ rows)
that gen_instruction_table.py writes (U1632, plan 1.J.57): the section is decided by the gate profile (the VMware
guest, --guest / --vm, Emulator/data/cpuid_i5-13600k.txt) but every reason must say whether it also holds on the
bare host (--host, Emulator/data/cpuid_i5-13600k_bare_host.txt).

The expectations are derived here, not copied from a table:
  - the rows and their ISA families from the generator's input Emulator/data/isa_manual_forms.tsv;
  - every CPUID bit value from the two profile files (own parser);
  - the bit of each family from this file's own list (SDM 253665-093 Vol1 chapter 21 Tables 21-9, 21-19 to 21-25,
    21-62; APX 355828-009; AVX10.2 361050-007; ACE 1.15), independent of the generator's CPUID_BIT.
The generator runs into a temporary folder on its own inputs (Emulator/data/alltest_baseline/alltest.csv,
isa_manual_forms.tsv, verified_forms.tsv; it reads the two profiles next to isa_manual_forms.tsv); its .json and
.md output are checked:
  1. every isa_manual_forms row is in the table exactly once, with the same ISA set;
  2. section membership: a row whose families all have a listed CPUID bit is ❌ exactly when all those bits are 0 on
     the guest profile (CET and AVX10-version rows excluded, see below);
  3. per row of the ❌ section, the reason text:
     - bit 0 on both profiles: "<CPUID.LH[.S]:REG[b]> = 0 on --host and --guest" (exact for a one-family row,
       a fragment for a multi-family row; AVX10 / ACE add a label in parentheses after the bit);
     - bit 1 on --host, 0 on --guest ("enumerated on --host"): "--guest: <bit> = 0; --host: enumerated (<bit> = 1 on
       the bare host)" (+ ", CPUID.19H:EBX[0] AESKLE = 0 ..." for Key Locker when the host's AESKLE is 0); SMX names
       both machines (U1500 text: "CPUID.01H:ECX[6] = 0 ... the bare host enumerates SMX (CPUID.01H:ECX[6] = 1");
       the JSON flag host_enumerates is set and the emulator column no longer says "the i5-13600K lacks it";
     - CET: "the CPU has CET ..." holds only if CET_SS = 1 on both profiles; the IBT value per profile
       "CET_IBT CPUID.7H:EDX[20] = <host> on --host, <guest> on --guest";
     - PKU: "the CPU has PKU but Windows leaves CR4.PKE off" holds only if PKU = 1 and OSPKE = 0 on both profiles;
     - families whose reason names no bit (APX, AMX-FP8 / TF32 / MOVRS / AVX512, AVX512_GFNI / VAES / VPCLMULQDQ):
       their prerequisite bit (APX_F, AMX_TILE, AVX512F) is 0 on both profiles and the reason says
       "on --host and --guest"; AMD / VIA-only families: both profiles are GenuineIntel; TDX / IBHF: the label only;
     - no reason keeps the pre-U1632 "on this CPU", and every reason names --host (SMX: "the bare host");
     - a family of a ❌ row that this file does not know fails the check (extend the lists, do not skip it);
  4. the .md section: the same ❌ rows with the same reason as the JSON, and its "N rows are **enumerated on
     --host**" equals the derived count;
  5. cross-check only: the count of "enumerated on --host" rows (derived from the profiles) equals the JSON
     host_enumerated value, the flagged rows and the .md paragraph (19 at U1632 time; never the main assertion);
  6. negative controls (in memory, every run): the checks above must fail on three altered copies of the generated
     rows (the pre-U1632 labels, the host flags inverted, the IBT values swapped) - so the suite cannot pass vacuously.

usage: python check_instruction_table.py [--gen GENERATOR.py] [--keep DIR]
       (--gen: check another generator file, e.g. a scratch copy; --keep: write the table there and keep it)
Exit status 0 = all checks passed, 1 = a check failed, 2 = usage / the generator failed.
"""
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, '..', '..', 'data'))
GUEST_FILE = 'cpuid_i5-13600k.txt'                 # --guest / --vm, the gate profile (VMware guest)
HOST_FILE = 'cpuid_i5-13600k_bare_host.txt'        # --host (bare i5-13600K, P-core)
EAX, EBX, ECX, EDX = range(4)
REGN = ('EAX', 'EBX', 'ECX', 'EDX')
CROSS = '❌'                                   # the table's cross mark

# ---- the CPUID bit of each family (leaf, subleaf, register, bit), own list ---------------------------------------
# SDM 253665-093 Vol1 chapter 21: Table 21-9 (01H ECX), 21-19 / 21-20 / 21-21 (07H.0 EBX / ECX / EDX), 21-22 /
# 21-23 / 21-24 / 21-25 (07H.1 EAX / EBX / ECX / EDX), 21-62 (19H), 80000008H EBX; family names = the table's
# (XED ISA set minus the _128 / _256 / _512 / _SCALAR / _KOPx suffix). The reason text names the bit as
# CPUID.<leaf>H[.<subleaf>]:<reg>[<bit>].
BIT = {
    'MONITOR': (1, 0, ECX, 3),
    'SGX': (7, 0, EBX, 2), 'SGX_ENCLV': (7, 0, EBX, 2), 'HLE': (7, 0, EBX, 4), 'RTM': (7, 0, EBX, 11),
    'MPX': (7, 0, EBX, 14), 'AVX512F': (7, 0, EBX, 16), 'AVX512DQ': (7, 0, EBX, 17), 'AVX512_IFMA': (7, 0, EBX, 21),
    'AVX512PF': (7, 0, EBX, 26), 'AVX512ER': (7, 0, EBX, 27), 'AVX512CD': (7, 0, EBX, 28), 'AVX512BW': (7, 0, EBX, 30),
    'PREFETCHWT1': (7, 0, ECX, 0), 'AVX512_VBMI': (7, 0, ECX, 1), 'WAITPKG': (7, 0, ECX, 5),
    'AVX512_VBMI2': (7, 0, ECX, 6), 'AVX512_VNNI': (7, 0, ECX, 11), 'AVX512_BITALG': (7, 0, ECX, 12),
    'AVX512_VPOPCNTDQ': (7, 0, ECX, 14), 'KEYLOCKER': (7, 0, ECX, 23), 'CLDEMOTE': (7, 0, ECX, 25),
    'ENQCMD': (7, 0, ECX, 29),
    'AVX512_4VNNIW': (7, 0, EDX, 2), 'AVX512_4FMAPS': (7, 0, EDX, 3), 'UINTR': (7, 0, EDX, 5),
    'AVX512_VP2INTERSECT': (7, 0, EDX, 8), 'TSX_LDTRK': (7, 0, EDX, 16), 'PCONFIG': (7, 0, EDX, 18),
    'AMX_BF16': (7, 0, EDX, 22), 'AVX512_FP16': (7, 0, EDX, 23), 'AMX_TILE': (7, 0, EDX, 24),
    'AMX_TILE_BASE': (7, 0, EDX, 24), 'AMX_INT8': (7, 0, EDX, 25),
    'SHA512': (7, 1, EAX, 0), 'SM3': (7, 1, EAX, 1), 'SM4': (7, 1, EAX, 2), 'RAO_INT': (7, 1, EAX, 3),
    'AVX512_BF16': (7, 1, EAX, 5), 'CMPCCXADD': (7, 1, EAX, 7), 'FRED': (7, 1, EAX, 17), 'LKGS': (7, 1, EAX, 18),
    'WRMSRNS': (7, 1, EAX, 19), 'AMX_FP16': (7, 1, EAX, 21), 'HRESET': (7, 1, EAX, 22), 'AVX_IFMA': (7, 1, EAX, 23),
    'MSRLIST': (7, 1, EAX, 27), 'MOVRS': (7, 1, EAX, 31),
    'PBNDKB': (7, 1, EBX, 1), 'MSR_IMM': (7, 1, ECX, 5),
    'AVX_VNNI_INT8': (7, 1, EDX, 4), 'AVX_NE_CONVERT': (7, 1, EDX, 5), 'AMX_COMPLEX': (7, 1, EDX, 8),
    'AVX_VNNI_INT16': (7, 1, EDX, 10), 'ICACHE_PREFETCH': (7, 1, EDX, 14), 'USER_MSR': (7, 1, EDX, 15),
    'KEYLOCKER_WIDE': (0x19, 0, EBX, 2),
    'WBNOINVD': (0x80000008, 0, EBX, 9),
}
# families whose reason adds a label in parentheses after the bit ("CPUID.7H.1:EDX[19] (AVX10) = 0 ...")
# AVX10: SDM Vol1 Table 21-25 EDX[19] + AVX10.2 361050-007 ch. 2 (the version in 24H decides; the version logic is
# not modelled here - a profile with the AVX10 bit set fails the check, extend it then); ACE: ACE 1.15 3.1
AVX10 = (7, 1, EDX, 19)
LABELLED = {f: AVX10 for f in ('AVX10_2_BF16', 'AVX10_MOVRS', 'AVX10_V2_AUX', 'AVX512_COM_EF', 'AVX512_FP16_CONVERT',
                               'AVX512_FP8_CONVERT', 'AVX512_MEDIAX', 'AVX512_MINMAX', 'AVX512_MOVZXC',
                               'AVX512_SAT_CVT', 'AVX512_SAT_CVT_DS', 'AVX512_VNNI_FP16', 'AVX512_VNNI_INT16',
                               'AVX512_VNNI_INT8')}
LABELLED['ACE_1'] = (7, 1, ECX, 11)
SMX = (1, 0, ECX, 6)                               # Table 21-9; reason text of U1500 (names both machines)
AESKLE = (0x19, 0, EBX, 0)                         # Table 21-62
CET_SS, CET_IBT = (7, 0, ECX, 7), (7, 0, EDX, 20)  # Tables 21-20 / 21-21
PKU, OSPKE = (7, 0, ECX, 3), (7, 0, ECX, 4)        # Table 21-20
APX_F = (7, 1, EDX, 21)                            # APX 355828-009 "CPUID.(EAX=0x7,ECX=0x1):EDX.APX_F[21]"
# families whose reason names no bit: the prerequisite bit that must be 0 on both profiles for "on --host and --guest"
UNNAMED = {'AMX_AVX512': (7, 0, EDX, 24), 'AMX_FP8': (7, 0, EDX, 24), 'AMX_TF32': (7, 0, EDX, 24),
           'AMX_MOVRS': (7, 0, EDX, 24),           # AMX-TILE (the ISE 319433 AMX extensions need it)
           'AVX512_GFNI': (7, 0, EBX, 16), 'AVX512_VAES': (7, 0, EBX, 16),
           'AVX512_VPCLMULQDQ': (7, 0, EBX, 16)}   # AVX512F (EVEX forms)
VENDOR = {'3DNOW', 'AMD_INVLPGB', 'CLZERO', 'FMA4', 'LWP', 'MCOMMIT', 'MONITORX', 'RDPRU', 'SNP', 'SSE4a', 'SVM',
          'TBM', 'XOP', 'VIA_PADLOCK_AES', 'VIA_PADLOCK_MONTMUL', 'VIA_PADLOCK_RNG', 'VIA_PADLOCK_SHA'}
LABEL_ONLY = {'TDX', 'IBHF'}                       # constant reasons (server-only / not enumerated): the label only
PAIR = ' on --host and --guest'


def bitname(t):
    leaf, sub, reg, b = t
    return 'CPUID.%XH%s:%s[%d]' % (leaf, ('.%d' % sub) if sub else '', REGN[reg], b)


def load_profile(path):
    """own parser: '<leaf> <subleaf> <eax> <ebx> <ecx> <edx>' (hex; leaf and registers 8 digits), '#' comments"""
    regs = {}
    for line in open(path, encoding='utf-8'):
        t = line.split('#', 1)[0].split()
        if len(t) == 6 and re.fullmatch(r'[0-9A-Fa-f]{1,8}', t[1]) and \
                all(re.fullmatch(r'[0-9A-Fa-f]{8}', x) for x in t[:1] + t[2:]):
            regs[(int(t[0], 16), int(t[1], 16))] = [int(x, 16) for x in t[2:]]
    return regs


def bit(prof, t):
    leaf, sub, reg, b = t
    return (prof.get((leaf, sub), [0, 0, 0, 0])[reg] >> b) & 1


def vendor(prof):
    v = prof.get((0, 0), [0, 0, 0, 0])
    return b''.join(x.to_bytes(4, 'little') for x in (v[1], v[3], v[2])).decode('latin-1')


def family(isa):
    f = re.sub(r'_(128N?|256|512|SCALAR|KOP[BWDQ])$', '', isa)
    return f or '?'


def unnamed_bit(f):
    if f.startswith('APX_F'):
        return APX_F
    return UNNAMED.get(f)


def load_forms(path):
    rows = {}
    for line in open(path, encoding='utf-8'):
        if not line.strip() or line.startswith('#'):
            continue
        t = (line.rstrip('\r\n').split('\t') + [''] * 5)[:5]
        rows.setdefault((t[0].lower(), t[1]), set()).add(t[3])
    return rows


def reason_of(note):
    m = re.search(r'\*\*NOT SUPPORTED on our i5-13600K\*\* \((.*?)\) — ', note)
    return m.group(1) if m else None


# ------------------------------------------------------------------------------------------------ the row checks
def expect_rows(forms, H, G):
    """derived per row: (families, expected host-enumerated, membership expectation True/False/None)"""
    out = {}
    for key, isas in forms.items():
        fams = sorted(set(family(f) for isa in isas for f in isa.split('+')))
        bits = [BIT.get(f) or LABELLED.get(f) or (SMX if f == 'SMX' else None) for f in fams]
        host_on = any(t and bit(H, t) and not bit(G, t) for t in bits)
        member = None
        if 'CET' not in fams and all(bits):
            avx10_on = any(f in LABELLED and LABELLED[f] == AVX10 and bit(G, AVX10) for f in fams)
            if not avx10_on:
                member = not any(bit(G, t) for t in bits)
        out[key] = (fams, host_on, member)
    return out


def check_rows(rows, forms, exp, H, G, fail, st):
    """the per-row checks 1 to 3 on the generated JSON rows; calls fail(msg) per failed check, counts in st"""
    seen = {}
    for x in rows:
        key = (x['mnemonic'], x['encoding'])
        if key in seen:
            fail('%s %s: twice in the table' % key)
        seen[key] = x
    for key in forms:
        if key not in seen:
            fail('%s %s: in isa_manual_forms.tsv, missing from the table' % key)
    for key in seen:
        if key not in forms:
            fail('%s %s: in the table, not in isa_manual_forms.tsv' % key)
    nrow = 0
    for key, x in seen.items():
        if key not in forms:
            continue
        if x['isa'] != '+'.join(sorted(forms[key])):
            fail('%s %s: ISA %s, isa_manual_forms.tsv says %s' % (key + (x['isa'], '+'.join(sorted(forms[key])))))
        fams, host_on, member = exp[key]
        cross = x['status'] == CROSS
        flag = x.get('host_enumerates')
        if member is not None and member != cross:
            fail('%s %s (%s): status %s, but the guest profile bits say %s' % (
                key + ('+'.join(fams), 'X' if cross else x['status'], 'cross' if member else 'not cross')))
        if not cross:
            if flag:
                fail('%s %s: host_enumerates set on a row outside the cross section' % key)
            continue
        nrow += 1
        why = reason_of(x['note'])
        if why is None:
            fail('%s %s: no "NOT SUPPORTED on our i5-13600K (...)" reason in the note' % key)
            continue
        tag = '%s %s (%s)' % (key + ('+'.join(fams),))
        if flag is None:
            fail('%s: no host_enumerates flag in the JSON row' % tag)
        elif bool(flag) != host_on:
            fail('%s: host_enumerates %s, the profiles say %s' % (tag, bool(flag), host_on))
        if ' on this CPU' in why:
            fail('%s: reason still says "on this CPU" (no per-profile label): %s' % (tag, why))
        if '--host' not in why and not ('SMX' in fams and 'the bare host' in why):   # SMX: U1500 names the bare host
            fail('%s: reason does not name --host: %s' % (tag, why))
        unknown = [f for f in fams if not (f in BIT or f in LABELLED or f in VENDOR or f in LABEL_ONLY or
                                           unnamed_bit(f) or f in ('SMX', 'CET', 'PKU'))]
        if unknown:
            fail('%s: family %s unknown to check_instruction_table.py (add its CPUID bit or class)' % (
                tag, ', '.join(unknown)))
        bitfams = [f for f in fams if f in BIT or f in LABELLED]
        if bitfams or [f for f in fams if f in ('SMX', 'CET', 'PKU') or unnamed_bit(f)]:
            st['rows whose reason depends on a CPUID bit (asserted per profile)'] += 1
        if host_on:
            st['enumerated on --host (derived)'] += 1
            if 'the i5-13600K lacks it' in x['note']:
                fail('%s: enumerated on --host, the emulator column still says "the i5-13600K lacks it"' % tag)
            if 'SMX' in fams:
                st['  SMX text naming both machines'] += 1
                for want in ('%s = 0' % 'CPUID.01H:ECX[6]', 'the bare host enumerates SMX (CPUID.01H:ECX[6] = 1'):
                    if want not in why:
                        fail('%s: SMX reason lacks "%s": %s' % (tag, want, why))
            elif len(fams) == 1 and fams[0] in BIT:
                b = bitname(BIT[fams[0]])
                base = '--guest: %s = 0; --host: enumerated (%s = 1 on the bare host)' % (b, b)
                st['  exact "--guest: <bit> = 0; --host: enumerated (<bit> = 1 ...)"'] += 1
                if fams[0] in ('KEYLOCKER', 'KEYLOCKER_WIDE') and not bit(H, AESKLE):
                    if not why.startswith(base + ', CPUID.19H:EBX[0] AESKLE = 0'):
                        fail('%s: expected "%s, CPUID.19H:EBX[0] AESKLE = 0 ..." (AESKLE = 0 on --host), got: %s' % (
                            tag, base, why))
                elif why != base:
                    fail('%s: expected "%s", got: %s' % (tag, base, why))
            else:
                st['  multi-family enumerated text'] += 1
                if not why.startswith('--guest: ') or '; --host: enumerated (' not in why:
                    fail('%s: expected "--guest: ...; --host: enumerated (...)", got: %s' % (tag, why))
                for f in bitfams:
                    t = BIT.get(f) or LABELLED[f]
                    if bit(H, t) and not re.search(r'--host: enumerated \([^)]*%s' % re.escape(bitname(t)), why):
                        fail('%s: %s = 1 on --host is not named as enumerated: %s' % (tag, bitname(t), why))
            continue
        # not enumerated on --host: every bit family is 0 on both profiles
        for f in bitfams:
            t = BIT.get(f) or LABELLED[f]
            if bit(G, t) or bit(H, t):
                if 'CET' not in fams:
                    fail('%s: %s is %d on --host, %d on --guest, but the row is in the cross section without '
                         '"enumerated on --host"' % (tag, bitname(t), bit(H, t), bit(G, t)))
                continue
            lab = r' \([^)]*\)' if f in LABELLED else ''
            st['bit fragments "<bit> = 0 on --host and --guest"'] += 1
            if not re.search(re.escape(bitname(t)) + lab + ' = 0' + re.escape(PAIR), why):
                fail('%s: expected "%s%s = 0%s" in the reason, got: %s' % (
                    tag, bitname(t), ' (...)' if lab else '', PAIR, why))
        if len(fams) == 1 and fams[0] in BIT and 'CET' not in fams:
            want = '%s = 0%s' % (bitname(BIT[fams[0]]), PAIR)
            st['  of them exact one-family reasons'] += 1
            if why != want:
                fail('%s: expected exactly "%s", got: %s' % (tag, want, why))
        if 'CET' in fams:
            st['CET rows (CET_SS on both, IBT per profile)'] += 1
            for t, nm in ((CET_SS, 'CET_SS'),):
                if not (bit(H, t) and bit(G, t)):
                    fail('%s: "the CPU has CET" needs %s = 1 on both profiles (host %d, guest %d)' % (
                        tag, nm, bit(H, t), bit(G, t)))
            want = 'CET_IBT CPUID.7H:EDX[20] = %d on --host, %d on --guest' % (bit(H, CET_IBT), bit(G, CET_IBT))
            if not why.endswith(want) or PAIR + ';' not in why:
                fail('%s: expected "...%s; %s", got: %s' % (tag, PAIR, want, why))
        if 'PKU' in fams:
            st['PKU rows (PKU = 1, OSPKE = 0 on both)'] += 1
            if not (bit(H, PKU) and bit(G, PKU)) or bit(H, OSPKE) or bit(G, OSPKE):
                fail('%s: "the CPU has PKU but Windows leaves CR4.PKE off" needs PKU = 1, OSPKE = 0 on both '
                     'profiles (PKU %d / %d, OSPKE %d / %d)' % (tag, bit(H, PKU), bit(G, PKU), bit(H, OSPKE),
                                                                bit(G, OSPKE)))
        for f in fams:
            t = unnamed_bit(f)
            st['families without a named bit: prerequisite 0 on both'] += bool(t)
            st['AMD / VIA-only families: GenuineIntel on both'] += f in VENDOR
            st['label only (TDX / IBHF)'] += f in LABEL_ONLY
            if t and (bit(H, t) or bit(G, t)):
                fail('%s: the reason says "not reported" on --host and --guest, but the prerequisite %s is %d on '
                     '--host, %d on --guest' % (tag, bitname(t), bit(H, t), bit(G, t)))
            if f in VENDOR and not (vendor(H) == vendor(G) == 'GenuineIntel'):
                fail('%s: vendor-only family, but the profiles report %r / %r' % (tag, vendor(H), vendor(G)))
        if [f for f in fams if f not in BIT and f not in LABELLED] and PAIR not in why:
            fail('%s: expected "%s" in the reason, got: %s' % (tag, PAIR, why))
    return nrow


def run_checks(rows, j, md, forms, exp, H, G, verbose, label):
    fails = []

    def fail(msg):
        fails.append(msg)
    st = collections.Counter()
    nrow = check_rows(rows, forms, exp, H, G, fail, st)
    if verbose:
        print('[table-check] per-row assertions on the cross section:')
        order = ['rows whose reason depends on a CPUID bit (asserted per profile)', 'enumerated on --host (derived)',
                 '  exact "--guest: <bit> = 0; --host: enumerated (<bit> = 1 ...)"', '  SMX text naming both machines',
                 '  multi-family enumerated text', 'bit fragments "<bit> = 0 on --host and --guest"',
                 '  of them exact one-family reasons', 'CET rows (CET_SS on both, IBT per profile)',
                 'PKU rows (PKU = 1, OSPKE = 0 on both)', 'families without a named bit: prerequisite 0 on both',
                 'AMD / VIA-only families: GenuineIntel on both', 'label only (TDX / IBHF)']
        for k in order + [k for k in st if k not in order]:
            print('  | %-72s %5d' % (k, st[k]))   # not '    <name> <n>' (test_runner.ps1 reads that as a sweep bucket)
    want_n = sum(1 for k, (f, h, m) in exp.items() if h and any(
        r['status'] == CROSS for r in rows if (r['mnemonic'], r['encoding']) == k))
    flagged = sum(1 for r in rows if r['status'] == CROSS and r.get('host_enumerates'))
    if j is not None:
        if j.get('host_enumerated') != want_n:
            fail('cross-check: JSON host_enumerated = %r, derived from the profiles: %d' % (j.get('host_enumerated'),
                                                                                           want_n))
        if flagged != want_n:
            fail('cross-check: %d rows flagged host_enumerates, derived from the profiles: %d' % (flagged, want_n))
    if md is not None:
        m = re.search(r'(\d+) rows are \*\*enumerated on --host\*\*', md)
        if not m:
            fail('md: no "N rows are **enumerated on --host**" paragraph in the cross section')
        elif int(m.group(1)) != want_n:
            fail('md: "%s rows are **enumerated on --host**", derived from the profiles: %d' % (m.group(1), want_n))
        mdrows = {}
        sec = md.split("### Instructions that can't be supported for now:", 1)
        if len(sec) != 2:
            fail('md: no cross section heading')
        else:
            for line in sec[1].split('\n'):
                if not line.startswith('| %s | ' % CROSS):
                    continue
                p = line.split(' | ', 6)
                mw = re.fullmatch(r'%s \*\*cannot run\*\* \((.*)\)' % CROSS, p[5]) if len(p) == 7 else None
                if not mw:
                    fail('md: unparsable cross row: %s' % line[:160])
                    continue
                mdrows[(p[1].lower(), p[2])] = mw.group(1)
            jrows = {(r['mnemonic'], r['encoding']): reason_of(r['note']) for r in rows if r['status'] == CROSS}
            for k in sorted(set(jrows) | set(mdrows)):
                if k not in mdrows:
                    fail('md: %s %s missing from the cross section' % k)
                elif k not in jrows:
                    fail('md: %s %s in the cross section, not a cross row of the JSON' % k)
                elif mdrows[k] != jrows[k]:
                    fail('md: %s %s reason differs from the JSON: %s' % (k + (mdrows[k],)))
            if verbose:
                print('[table-check] md cross section: %d rows parsed, JSON cross rows %d' % (len(mdrows), len(jrows)))
    return fails, nrow, want_n, flagged


def main():
    gen = os.path.join(HERE, 'gen_instruction_table.py')
    keep = None
    a = sys.argv[1:]
    while a:
        if a[0] == '--gen' and len(a) > 1:
            gen = os.path.abspath(a[1])
            a = a[2:]
        elif a[0] == '--keep' and len(a) > 1:
            keep = os.path.abspath(a[1])
            a = a[2:]
        else:
            print(__doc__.split('\n\n')[-1].strip())
            return 2
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except (AttributeError, ValueError):
        pass
    csv_p = os.path.join(DATA, 'alltest_baseline', 'alltest.csv')
    tsv_p = os.path.join(DATA, 'isa_manual_forms.tsv')
    ver_p = os.path.join(DATA, 'verified_forms.tsv')
    G = load_profile(os.path.join(DATA, GUEST_FILE))
    H = load_profile(os.path.join(DATA, HOST_FILE))
    print('[table-check] generator: %s' % gen)
    print('[table-check] inputs: %s, %s, %s' % (csv_p, tsv_p, ver_p))
    print('[table-check] profiles: --guest %s (%d rows, %s), --host %s (%d rows, %s)' % (
        GUEST_FILE, len(G), vendor(G), HOST_FILE, len(H), vendor(H)))
    if not G or not H:
        print('[table-check] FAIL: a profile file has no rows')
        return 1
    forms = load_forms(tsv_p)
    exp = expect_rows(forms, H, G)
    print('[table-check] isa_manual_forms.tsv: %d mnemonic/encoding rows' % len(forms))
    print('[table-check] CPUID bits per profile (host / guest), own list:')
    named = sorted(set(list(BIT.items()) + list(LABELLED.items()) + [('SMX', SMX), ('AESKLE', AESKLE),
                   ('CET_SS', CET_SS), ('CET_IBT', CET_IBT), ('PKU', PKU), ('OSPKE', OSPKE), ('APX_F', APX_F)]),
                   key=lambda kv: (kv[1], kv[0]))
    for f, t in named:
        h, g = bit(H, t), bit(G, t)
        print('  %-22s %-24s host %d guest %d%s' % (f, bitname(t), h, g, '   <- enumerated on --host only' if h and not g else ''))

    tmp = keep or tempfile.mkdtemp(prefix='tablechk_')
    try:
        os.makedirs(tmp, exist_ok=True)
        out_md = os.path.join(tmp, 'instruction_table.md')
        cmd = [sys.executable, '-I', '-X', 'utf8', gen, csv_p, tsv_p, out_md, ver_p]
        print('[table-check] run: %s' % ' '.join('"%s"' % c if ' ' in c else c for c in cmd))
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        for line in p.stdout.decode('utf-8', 'replace').splitlines():
            print('  gen> ' + line)
        if p.returncode != 0:
            print('[table-check] FAIL: the generator exited with %d' % p.returncode)
            return 2
        md = open(out_md, encoding='utf-8').read()
        j = json.load(open(os.path.splitext(out_md)[0] + '.json', encoding='utf-8'))
    finally:
        if not keep:
            shutil.rmtree(tmp, ignore_errors=True)
    rows = j['rows']

    fails, nrow, want_n, flagged = run_checks(rows, j, md, forms, exp, H, G, True, 'generated')
    # per-family summary of the cross section (what was asserted)
    fam_rows = {}
    for r in rows:
        if r['status'] == CROSS:
            fs = exp[(r['mnemonic'], r['encoding'])][0]
            k = '+'.join(fs)
            n, hn = fam_rows.get(k, (0, 0))
            fam_rows[k] = (n + 1, hn + bool(exp[(r['mnemonic'], r['encoding'])][1]))
    print('[table-check] cross section: %d rows, %d family sets (rows / enumerated on --host):' % (nrow, len(fam_rows)))
    for k in sorted(fam_rows):
        n, hn = fam_rows[k]
        print('  %-40s %4d%s' % (k, n, ('   enumerated on --host: %d' % hn) if hn else ''))
    print('[table-check] enumerated on --host (hidden by the guest only), per row:')
    for r in rows:
        if r['status'] == CROSS and exp[(r['mnemonic'], r['encoding'])][1]:
            print('  %-16s %-6s %s' % (r['mnemonic'].upper(), r['encoding'], reason_of(r['note'])))
    print('[table-check] cross-check (count only): derived %d, JSON host_enumerated %r, flagged rows %d' % (
        want_n, j.get('host_enumerated'), flagged))
    member = sum(1 for v in exp.values() if v[2] is not None)
    print('[table-check] section membership asserted for %d of %d rows (all families with a listed CPUID bit)' % (
        member, len(exp)))

    # negative controls: the same checks on altered copies must fail
    controls = []
    old = []
    for r in rows:
        r2 = dict(r)
        if r2['status'] == CROSS:
            r2['note'] = r2['note'].replace(PAIR, ' on this CPU')
            r2.pop('host_enumerates', None)
        old.append(r2)
    controls.append(('pre-U1632 labels ("on this CPU", no host_enumerates)', old))
    inv = [dict(r, host_enumerates=not r.get('host_enumerates')) if r['status'] == CROSS else r for r in rows]
    controls.append(('host_enumerates inverted on every cross row', inv))
    ibt = re.compile(r'CET_IBT CPUID\.7H:EDX\[20\] = (\d) on --host, (\d) on --guest')
    sw = [dict(r, note=ibt.sub(lambda m: 'CET_IBT CPUID.7H:EDX[20] = %s on --host, %s on --guest' % (m.group(2), m.group(1)),
                               r['note'])) for r in rows]
    controls.append(('CET_IBT host / guest values swapped', sw))
    bad_controls = 0
    for name, rr in controls:
        f2, _, _, _ = run_checks(rr, None, None, forms, exp, H, G, False, name)
        ok = len(f2) > 0
        bad_controls += not ok
        print('[table-check] negative control "%s": %d failure(s) detected - %s' % (name, len(f2), 'ok' if ok else 'NOT DETECTED'))
        if f2:
            print('    first: ' + f2[0][:220])
    if bad_controls:
        fails.append('%d negative control(s) not detected: the checks are vacuous' % bad_controls)

    if fails:
        print('[table-check] FAIL: %d failed check(s):' % len(fails))
        for f in fails[:200]:
            print('  FAIL ' + f)
        if len(fails) > 200:
            print('  ... %d more' % (len(fails) - 200))
        return 1
    print('[table-check] OK: %d cross rows, every per-profile reason as derived from the two profiles; '
          '%d enumerated on --host; 3 negative controls detected' % (nrow, want_n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
