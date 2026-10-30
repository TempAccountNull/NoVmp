#!/usr/bin/env python3
r"""hostref_import.py (P57 queued task 1, ledger U2381): the bare-host native references of a host_reg_ref package run as the data files of the
--host suites. Reads the run's output folder (LOCAL, never committed: registry.txt, refs.tsv, stdout\*.txt, manifest.txt) and writes
  Emulator/data/nt_measured_bare_host.txt   the tm_nt lines of every case the package ran (one process per case, as nt_measured.py), with a
                                            header naming the run (date, package commit, OS build) - the file nt_measured.py --env bare_host checks
  Emulator/data/hostref_bare_host.txt       "status T2 2B" ... (sysloader.T*: the test PEs started DETACHED by tm_nt run.detached / T6 normally),
                                            "tx TX1 61EDBFE4" (exc.pipe.tx*: the exit status on a pipe) and "text TX1 <line>" (its printed lines)
Screened before writing (exit 1, nothing written): nt_measured.py's identifier screen (no SID string, no DESKTOP- name, no \Users\ path, no
drive path, no hex number of 9 .. 16 digits outside the allowed constants) and no line naming the package / staging folders. The registry
subset is NOT written here (it stays local: capture\sysdlls\bare_host\registry.txt).
P60 (USER DECISION 2026-10-30, P60 step 1; --merge): a run of a ONE-CASE host package (host_u32_ref_v1: refs.tsv holds tm_nt.u32 only)
REPLACES the lines of exactly its case(s) in the existing nt_measured_bare_host.txt (in place, the other cases unchanged) and adds a header
line naming that run; hostref_bare_host.txt is not written (such a package has no test-PE statuses). Same screen; same refusals.
usage: python -I -X utf8 hostref_import.py RUN_DIR REPO_ROOT [--merge]"""
import importlib.util
import os
import re
import sys

run, root = sys.argv[1], sys.argv[2]
merge = sys.argv[3:] == ['--merge']   # P60
if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and not merge):
    sys.exit(__doc__.strip().splitlines()[-1])
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ntm', os.path.join(HERE, 'nt_measured.py'))
ntm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ntm)

man = {}
for l in open(os.path.join(run, 'manifest.txt'), encoding='utf-8'):
    if not l.startswith('#') and ' ' in l.strip():
        k, v = l.strip().split(' ', 1)
        man[k] = v
if man.get('environment') != 'bare_host' or man.get('detected') != 'bare_host':
    sys.exit('REFUSED: the run is not a detected bare-host run')
refs = [l.rstrip('\n').split('\t') for l in open(os.path.join(run, 'refs.tsv'), encoding='utf-8') if not l.startswith('#') and l.strip()]
out_dir = os.path.join(run, 'stdout')


def lines_of(rid):
    p = os.path.join(out_dir, rid + '.txt')
    return [l.rstrip('\r\n') for l in open(p, encoding='ascii', errors='replace')] if os.path.isfile(p) else None


bad = []
ntm_lines, cases = [], []
for r in refs:
    if r[0].startswith('tm_nt.'):
        c = r[0][6:]
        ls = [l for l in (lines_of(r[0]) or []) if l.startswith('tm_nt ')]
        cases.append(c)
        ntm_lines += ls
missing = [c for c in ntm.CASES if c not in cases]
status, tx, text = {}, {}, {}
for r in refs:
    if r[0].startswith('sysloader.'):
        ls = lines_of(r[0]) or []
        m = [re.search(r'exit code ([0-9A-F]+)H', l) for l in ls]
        m = [x for x in m if x]
        if len(m) != 1:
            bad.append('%s: no single exit code line' % r[0])
        else:
            status[r[0].split('.', 1)[1]] = m[0].group(1)
    if r[0].startswith('exc.pipe.'):
        tag = 'TX' + re.match(r'tx(\d)_', r[0][9:]).group(1)
        tx[tag] = r[1]
        text[tag] = [l for l in (lines_of(r[0]) or []) if l]
# the screen
allowed = getattr(ntm, 'ALLOWED_HEX', set())
for l in ntm_lines + [t for v in text.values() for t in v]:
    for rx in (r'S-1-5-21-', r'(?i)DESKTOP-[A-Z0-9]{4}', r'(?i)\\Users\\', r'(?i)\b[A-Z]:\\', r'(?i)programdata', r'(?i)host_[a-z0-9]+_ref'):
        if re.search(rx, l):
            bad.append('identifier screen (%s): %s' % (rx, l[:60]))
    for h in re.findall(r'\b[0-9A-Fa-f]{9,16}\b', l):
        if not re.fullmatch(r'[0-9]+', h) and h.upper() not in {a.upper() for a in allowed} and not l.startswith('TX'):
            bad.append('a hex number of %d digits: %s' % (len(h), l[:60]))
keys = [l.split(':', 1)[0] for l in ntm_lines]
if len(keys) != len(set(keys)):
    bad.append('a tm_nt item twice')
if bad:
    for b in bad[:20]:
        print('FAIL', b)
    sys.exit(1)
if merge:   # P60: replace the run's case(s) in place
    if not cases or status or tx:
        sys.exit('REFUSED: --merge takes a run of tm_nt case(s) only (cases %s, statuses %d, TX %d)' % (cases, len(status), len(tx)))
    path = os.path.join(root, 'Emulator', 'data', 'nt_measured_bare_host.txt')
    old = open(path, encoding='ascii').read().replace('\r\n', '\n').rstrip('\n').split('\n')
    ohead = [l for l in old if l.startswith('#')]
    obody = [l for l in old if not l.startswith('#')]
    isc = lambda l, c: re.match(r'tm_nt %s\d*(\.|:)' % re.escape(c), l) is not None
    out, done = [], set()
    for l in obody:
        c = next((c for c in cases if isc(l, c)), None)
        if c is None:
            out.append(l)
        elif c not in done:
            out += [x for x in ntm_lines if isc(x, c)]
            done.add(c)
    for c in cases:
        if c not in done:
            out += [x for x in ntm_lines if isc(x, c)]
    pkg = man.get('package', '?').split()
    ohead = [l for l in ohead if not any(l.startswith('# Case %s ' % c) for c in cases)]
    ohead += ['# Case %s (%d lines): from the host package %s (tm_nt + tm_u32 of %s), run by the user on %s (Windows %s.%s); merged by hostref_import.py --merge (P60).' % (
        c, len([x for x in ntm_lines if isc(x, c)]), pkg[0] if pkg else '?', pkg[-1][:8] if pkg else '?', man.get('captured', '?'), man.get('os_build'), man.get('os_ubr')) for c in cases]
    with open(path, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(ohead + out) + '\n')
    print('nt_measured_bare_host.txt: case(s) %s replaced in place (%d lines; was %d); %d lines in all; hostref_bare_host.txt not written' % (
        ', '.join(cases), len(ntm_lines), sum(1 for l in obody if any(isc(l, c) for c in cases)), len(out)))
    sys.exit(0)
hdr = ['# BARE-HOST native measurements of our own test PE tm_nt (the measured-service model, decision 3.0.7), environment bare_host: the',
       '# i5-13600K host, Windows 10 build %s.%s (the bare-host DLL capture capture\\sysdlls\\bare_host: the same ntdll.dll, SHA-256 %s...).' % (
           man.get('os_build'), man.get('os_ubr'), man.get('ntdll_sha256', '')[:16]),
       '# Source: the host package host_reg_ref_v2 (tm_nt of %s), run by the user on %s; one process per case from a copy in a fresh' % (
           man.get('package', '?').split()[-1][:8], man.get('captured', '?')),
       '# folder, current directory <SystemRoot> (as nt_measured.py). Placeholders as nt_measured_vmware_guest.txt (<va>, <handle>, <ptr:+XH>,',
       '# <value:T>, ...). Imported by Emulator/tools/hostref_import.py (P57, ledger U2381). Cases not in that package (newer than it): %s.' % (
           ', '.join(missing) if missing else 'none')]
with open(os.path.join(root, 'Emulator', 'data', 'nt_measured_bare_host.txt'), 'w', encoding='ascii', newline='\r\n') as f:
    f.write('\n'.join(hdr + ntm_lines) + '\n')
h2 = ['# BARE-HOST native references of the test PEs (environment bare_host; host_reg_ref_v2 run %s, Windows build %s.%s): "status Tn X" = the' % (
          man.get('captured', '?'), man.get('os_build'), man.get('os_ubr')),
      '# exit status of Tn started DETACHED by tm_nt run.detached (T6 normally: run.normal), "tx TXn X" = TXn\'s exit status with its standard',
      '# output on a pipe, "text TXn <line>" = its printed lines in order. Imported by Emulator/tools/hostref_import.py (P57, ledger U2381);',
      '# checked by win_env_sys_loader.py / win_env_exceptions.py --env bare_host.']
body = ['status %s %s' % (k, status[k]) for k in sorted(status, key=lambda t: int(t[1:]))] + ['tx %s %s' % (k, tx[k]) for k in sorted(tx)]
for k in sorted(text):
    body += ['text %s %s' % (k, t) for t in text[k]]
with open(os.path.join(root, 'Emulator', 'data', 'hostref_bare_host.txt'), 'w', encoding='ascii', newline='\r\n') as f:
    f.write('\n'.join(h2 + body) + '\n')
print('nt_measured_bare_host.txt: %d lines of %d cases (missing: %s); hostref_bare_host.txt: %d statuses, %d TX statuses, %d TX text lines' % (
    len(ntm_lines), len(cases), ', '.join(missing) or 'none', len(status), len(tx), sum(len(v) for v in text.values())))
