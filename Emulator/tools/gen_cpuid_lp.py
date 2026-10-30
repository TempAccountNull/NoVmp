#!/usr/bin/env python3
"""gen_cpuid_lp.py (P57 step 4, ledger U2374): the PER-LOGICAL-PROCESSOR CPUID table of a CPU profile, from the capture tool's raw
per-LP rows (capture\\host\\cpuid_capture.c: every logical processor pinned with SetThreadGroupAffinity, the processor number checked
before / after each sample; the raw TSV columns lp group number pinned off_processor_samples leaf subleaf call eax ebx ecx edx kind rule).

SDM 253665-093 Vol1 Chapter 21 "Processor Identification and Feature Determination" (Vol2A 253666-093 CPUID p. 3-203 points there) gives
each field a Domain; the fields of Domain "Logical Processor" / "Core" differ between the processors of one package:
  01H EBX[31:24] Initial APIC ID (Table 21-6, pp. 21-9 / 21-10: "replaced by the 32-bit x2APIC ID, available in Leaf 0BH and Leaf 1FH"),
  0BH / 1FH EDX x2APIC ID (Tables 21-31, p. 21-39 / 21-70, p. 21-71), 04H (Table 21-14, p. 21-21), 18H (Table 21-60, p. 21-63),
  1AH EAX[31:24] CORE_TYPE 20H Intel Atom / 40H Intel Core (Table 21-63, p. 21-66), and on a hybrid part the leaves that describe the
  core (06H, 14H, 16H, 1CH, 80000006H - measured, see the table's rows).
Output (Emulator/data/cpuid_lp_<profile>.txt; the profile = the file the engine installs for --host / --guest):
  start LP                  the logical processor whose answers the profile holds (the one the emulated thread starts on)
  lp N apic X               the APIC ID of LP N (01H EBX[31:24] = X AND FFH; 0BH / 1FH EDX = X) - the engine sets it through the fork's
                            UC_CTL_X86_APIC_ID (x86_apic_cpuid_fixup)
  row N LEAF SUB EAX EBX ECX EDX   every OTHER profile row whose answer on LP N differs from the start LP: the profile's row with the
                            registers that differ between the two raw LPs replaced (profile XOR raw[start] XOR raw[N] - the profile's own
                            treatment of the row is kept); a profile row the LP does not enumerate (an invalid sub-leaf there: "EAX/EBX/
                            ECX/EDX return 0", Vol1 pp. 21-21 / 21-63) = 0 0 0 0
Checks (exit 1): every LP has one APIC ID that 01H EBX[31:24] and every 0BH / 1FH sub-leaf EDX agree on; the APIC IDs are distinct; the
start LP's raw rows equal the profile rows except the APIC fields (or the difference is listed as such).
usage: python -I -X utf8 gen_cpuid_lp.py RAW_TSV PROFILE_TXT OUT_TXT START_LP SOURCE_LABEL"""
import collections, os, sys

raw_p, prof_p, out_p, start, label = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5]
rows = [l.rstrip('\n').split('\t') for l in open(raw_p, encoding='utf-8') if l.strip() and not l.startswith('#')]
hdr, rows = rows[0], rows[1:]
ix = {h: i for i, h in enumerate(hdr)}
raw = collections.defaultdict(dict)   # lp -> (leaf, sub) -> (eax, ebx, ecx, edx)
for r in rows:
    raw[int(r[ix['lp']])][(int(r[ix['leaf']], 16), int(r[ix['subleaf']], 16))] = tuple(int(r[ix[k]], 16) for k in ('eax', 'ebx', 'ecx', 'edx'))
lps = sorted(raw)
prof = collections.OrderedDict()
for l in open(prof_p, encoding='utf-8'):
    if not l.strip() or l.startswith('#'):
        continue
    f = l.split()
    prof[(int(f[0], 16), int(f[1], 16))] = tuple(int(x, 16) for x in f[2:6])
bad = []
apic = {}
for lp in lps:
    ids = {raw[lp][(1, 0)][1] >> 24}
    x2 = {v[3] for (lf, sb), v in raw[lp].items() if lf in (0xB, 0x1F)}
    if len(x2) != 1 or ( next(iter(x2)) & 0xFF ) not in ids:
        bad.append('LP %d: APIC IDs disagree: 01H %s, 0BH / 1FH %s' % (lp, sorted(ids), sorted(x2)))
    apic[lp] = next(iter(x2)) if x2 else next(iter(ids))
if len(set(apic.values())) != len(lps):
    bad.append('APIC IDs not distinct: %s' % apic)
if start not in raw:
    bad.append('start LP %d not captured' % start)

def apic_masked(key, v):
    lf, sb = key
    if lf == 1:
        return (v[0], v[1] & 0x00FFFFFF, v[2], v[3])
    if lf in (0xB, 0x1F):
        return (v[0], v[1], v[2], 0)
    return v

notes = []
for key, pv in prof.items():
    rv = raw[start].get(key)
    if rv is not None and apic_masked(key, rv) != apic_masked(key, pv):
        notes.append('profile row %02XH.%02XH differs from the start LP\'s raw row outside the APIC fields: profile %s raw %s' % (
            key[0], key[1], ' '.join('%08X' % x for x in pv), ' '.join('%08X' % x for x in rv)))
out_rows = []
for lp in lps:
    if lp == start:
        continue
    for key, pv in prof.items():
        a, b = raw[start].get(key), raw[lp].get(key)
        if a is None:
            continue
        a, b0 = apic_masked(key, a), b
        if b is None:
            nv = (0, 0, 0, 0)
        else:
            b = apic_masked(key, b)
            if a == b:
                continue
            nv = tuple(p ^ x ^ y for p, x, y in zip(apic_masked(key, pv), a, b))
            if key[0] in (0xB, 0x1F):
                nv = (nv[0], nv[1], nv[2], apic[lp])
            if key[0] == 1:
                nv = (nv[0], (nv[1] & 0x00FFFFFF) | ((apic[lp] & 0xFF) << 24), nv[2], nv[3])
        out_rows.append((lp, key, nv))
for b in bad:
    print('FAIL:', b)
for n in notes:
    print('note:', n)
with open(out_p, 'w', encoding='utf-8', newline='\r\n') as f:
    f.write('# Per-logical-processor CPUID of the profile %s (P57 step 4, ledger U2374; generated by Emulator/tools/gen_cpuid_lp.py - do not edit).\n' % os.path.basename(prof_p))
    f.write('# Source: %s (%d logical processors, each pinned; values only - no identifier).\n' % (label, len(lps)))
    f.write('# SDM 253665-093 Vol1 Chapter 21: 01H EBX[31:24] Initial APIC ID (pp. 21-9 / 21-10), 0BH / 1FH EDX x2APIC ID (pp. 21-39 / 21-71),\n')
    f.write('# Domain "Logical Processor" / "Core" fields (04H p. 21-21, 18H p. 21-63, 1AH CORE_TYPE p. 21-66); an invalid sub-leaf returns 0.\n')
    f.write('# start = the LP the profile rows were taken from (the emulated thread starts there); lp N apic X; row N LEAF SUB EAX EBX ECX EDX =\n')
    f.write('# the profile row as LP N answers it (only rows that differ from the start LP outside the APIC fields).\n')
    for n in notes:
        f.write('# note: %s\n' % n)
    f.write('start %d\n' % start)
    for lp in lps:
        f.write('lp %d apic %X\n' % (lp, apic[lp]))
    for lp, key, nv in out_rows:
        f.write('row %d %08X %02X %08X %08X %08X %08X\n' % ((lp,) + key + nv))
print('%s: %d LPs, start %d, APIC IDs %s, %d override row(s), %d note(s), %d failure(s)' % (os.path.basename(out_p), len(lps), start,
      ' '.join('%X' % apic[l] for l in lps), len(out_rows), len(notes), len(bad)))
sys.exit(1 if bad else 0)
