#!/usr/bin/env python3
r"""snippet_hw_sample.py - suite emu_snippet_hw_sample (plan 3.M3, ledger U1978; notes\eread\P33_phase3_m3.md).

Plan 3.M3 done criterion: "a sample of emu-alltest hardware lines gives emu-snippet state == emu-alltest's Unicorn engine state".
A fixed, deterministic sample of the HARDWARE case lines of test.cmd's hardware files (HW_FILES): per file, the untagged lines
grouped by family (the first mnemonic, ".byte" lines by their first opcode byte after the prefixes), at most PER_FAMILY lines
of each family per file, in file order, at most MAX_LINES in all. Excluded (stated, never silent):
  - tagged lines ("# known deviation:", "# host state:", "# native only:"): where the host CPU or host state is involved
  - "~~" pairs (two encodings), debug-register lines (dr0..dr7 / dbcount / dr6mask / nodr6ok), cr4.cet=1 lines: harness
    machinery emu-snippet does not model in M3 (the debug-register thread, the CET stub)
  - lines that read a host-valued source into a compared register: RDTSC / RDTSCP / RDPMC (the host clock until the virtual time
    base of M6), TPAUSE / UMWAIT (TSC deadlines)
The sample runs twice through emu-alltest --cases SAMPLE --snippet-export (the native side runs our own self-generated snippets,
as every hardware suite): with the captured VMware-guest profile (cpuid_i5-13600k.txt, the gate's) and with the captured bare-host
profile (cpuid_i5-13600k_bare_host.txt). Each export is replayed with emu-snippet --batch; every line must have a reference (the
thunk ran to its end) and emu-snippet's final state must equal it ("reference differing: 0"), with >= MIN_LINES lines per profile.
Writes only below TESTS_DIR\snippet_replay (removed again after a pass). Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I snippet_hw_sample.py REPO_ROOT TESTS_DIR [--keep]"""
import os
import re
import subprocess
import sys

HW_FILES = ['hwcheck_gate1', 'cases_reach', 'cases_tsx_cet', 'cases_fixes', 'cases_fixes2', 'cases_fix3', 'cases_fix4', 'cases_fix5', 'cases_nan',
            'cases_f16c', 'cases_dp_nan', 'cases_quirks', 'cases_x87_cmp_exc', 'cases_sse_exc', 'cases_rcp_hw', 'cases_waitpkg_hw', 'cases_cet2_hw',
            'cases_backport_t1', 'cases_backport_t2', 'cases_evex_m4a_hw', 'cases_evex_m4b_hw', 'cases_bsf_zero_hw', 'cases_bsf_flags_hw',
            'cases_probefix_hw', 'cases_dbgfix_hw', 'cases_lockpriv_hw', 'cases_bdf97_hw', 'cases_ac_hw', 'cases_sysins_hw', 'cases_pconfig_hw',
            'cases_st_hw', 'cases_gating_hw', 'cases_keylocker', 'cases_sha_sm', 'cases_vnni_ifma']
PER_FAMILY = 2
MAX_LINES = 320
MIN_LINES = 200
PROFILES = [('guest', 'cpuid_i5-13600k.txt'), ('host', 'cpuid_i5-13600k_bare_host.txt')]
HOST_VALUED = re.compile(r'(?i)\b(rdtscp?|rdpmc|tpause|umwait)\b')
DBG = re.compile(r'(?i)(^|\s)(dr[0-7]=|dbcount\b|dr6mask=|nodr6ok\b)')
PREFIXES = {0x66, 0x67, 0xF2, 0xF3, 0xF0, 0x2E, 0x3E, 0x26, 0x36, 0x64, 0x65}


def family(asm):
    a = asm.strip().lower()
    if a.startswith('.byte'):
        vals = [int(x, 0) for x in re.findall(r'0x[0-9a-f]+|\d+', a[5:])[:6]]
        k = 0
        while k < len(vals) and (vals[k] in PREFIXES or 0x40 <= vals[k] <= 0x4F):
            k += 1
        rest = vals[k:k + 2]
        if rest and rest[0] == 0x0F:
            return '.byte 0f %02x' % rest[1] if len(rest) > 1 else '.byte 0f'
        return '.byte %02x' % rest[0] if rest else '.byte'
    m = re.match(r'(?:(?:lock|rep|repe|repne|repz|repnz)\s+)?([a-z0-9_.]+)', a)
    return m.group(1) if m else a[:8]


def sample(data):
    picked, skipped = [], {'tagged': 0, 'pair': 0, 'debug registers': 0, 'cr4.cet': 0, 'host-valued source': 0, 'expected-value': 0}
    families = set()
    for f in HW_FILES:
        p = os.path.join(data, f + '.txt')
        if not os.path.isfile(p):
            raise SystemExit('missing hardware file %s' % p)
        per = {}
        for raw in open(p, 'rb').read().decode('latin-1').split('\n'):
            line = raw[:-1] if raw.endswith('\r') else raw
            if not line or line[0] == '#':
                continue
            body = line.split('  #')[0]
            if '=>' in line:
                skipped['expected-value'] += 1
                continue
            if re.search(r'#\s*(known deviation|host state|native only):', line):
                skipped['tagged'] += 1
                continue
            if '~~' in body:
                skipped['pair'] += 1
                continue
            if DBG.search(body):
                skipped['debug registers'] += 1
                continue
            if 'cr4.cet=1' in body:
                skipped['cr4.cet'] += 1
                continue
            asm = body.split('|')[0]
            if HOST_VALUED.search(asm):
                skipped['host-valued source'] += 1
                continue
            fam = family(asm.split(';')[0])
            if per.get(fam, 0) >= PER_FAMILY:
                continue
            per[fam] = per.get(fam, 0) + 1
            families.add(fam)
            picked.append((f, line))
    # an even spread when the candidates exceed MAX_LINES: every k-th line, deterministic
    if len(picked) > MAX_LINES:
        step = len(picked) / MAX_LINES
        picked = [picked[int(i * step)] for i in range(MAX_LINES)]
        families = set(family(l.split('|')[0].split(';')[0]) for _, l in picked)
    return picked, families, skipped


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    keep = '--keep' in argv
    data = os.path.join(root, 'Emulator', 'data')
    out = os.path.join(tests, 'snippet_replay')
    os.makedirs(out, exist_ok=True)
    picked, families, skipped = sample(data)
    files = sorted(set(f for f, _ in picked))
    print('hw sample: %d lines from %d files, %d families (at most %d per family per file); excluded: %s' % (
        len(picked), len(files), len(families), PER_FAMILY, ', '.join('%s %d' % kv for kv in skipped.items())))
    case_file = os.path.join(out, 'hw_sample_cases.txt')
    with open(case_file, 'w', encoding='latin-1', newline='\n') as fh:
        fh.write('# emu_snippet_hw_sample (U1978): the deterministic sample of snippet_hw_sample.py\n')
        for _, l in picked:
            fh.write(l + '\n')
    fails = []
    produced = [case_file]
    for name, prof in PROFILES:
        jf = os.path.join(out, 'hw_sample_%s.jsonl' % name)
        produced.append(jf)
        r1 = subprocess.run([os.path.join(tests, 'emu-alltest.exe'), '--cases', case_file, '--cpuid', os.path.join(data, prof), '--snippet-export', jf],
                            capture_output=True, text=True, errors='replace', cwd=root)
        acct = re.findall(r'^case lines: (\d+), run: (\d+).*harness errors: (\d+)', r1.stdout, re.M)
        exp = re.findall(r'^snippet export: (\d+) record\(s\), (\d+) not exported .*?, (\d+) without a reference \(the thunk.*?, (\d+) without a reference \(guest', r1.stdout, re.M)
        print('hw sample %s (%s): emu-alltest exit %d; %s; %s' % (name, prof, r1.returncode, 'case lines %s, run %s, harness errors %s' % acct[0] if acct else 'no accounting line',
                                                                  'exported %s, not exported %s, no reference %s + %s (paging)' % exp[0] if exp else 'no export line'))
        if not exp or int(exp[0][0]) != len(picked) or int(exp[0][2]) or int(exp[0][3]):
            fails.append('%s: not every sampled line exported with a reference' % name)
            continue
        r2 = subprocess.run([os.path.join(tests, 'emu-snippet.exe'), '--batch', jf, '--quiet'], capture_output=True, text=True, errors='replace', cwd=root)
        summ = re.findall(r'^snippets: (\d+), expect checked: (\d+), expect differing: (\d+), reference checked: (\d+), reference differing: (\d+), errors: (\d+)', r2.stdout, re.M)
        diff_lines = [l for l in r2.stdout.splitlines() if l.startswith('[') and (' DIFF ' in l or ' ERROR ' in l)]
        for l in r2.stdout.splitlines():
            if l.startswith('[') or l.startswith('    '):
                print('  %s | %s' % (name, l[:220]))
        if not summ:
            fails.append('%s: emu-snippet printed no summary (exit %d)' % (name, r2.returncode))
            continue
        n, _e, _ed, rc, rd, er = (int(x) for x in summ[0])
        print('hw sample %s: snippets %d, reference checked %d, reference differing %d, errors %d (emu-snippet exit %d)' % (name, n, rc, rd, er, r2.returncode))
        if n != len(picked) or rc != n or rd or er or r2.returncode != 0 or n < MIN_LINES:
            fails.append('%s: snippets %d (sample %d, at least %d), reference checked %d, differing %d, errors %d; %d DIFF / ERROR line(s)' % (
                name, n, len(picked), MIN_LINES, rc, rd, er, len(diff_lines)))
    for f in fails:
        print('FAIL ' + f)
    if fails:
        print('emu_snippet_hw_sample: FAIL (%d problem(s))' % len(fails))
        return 1
    if not keep:
        for p in produced:
            if os.path.isfile(p):
                os.remove(p)
    print('emu_snippet_hw_sample: PASS - %d sampled hardware lines x %d profiles: emu-snippet state == emu-alltest Unicorn engine state on every line' % (
        len(picked), len(PROFILES)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
