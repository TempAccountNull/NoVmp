#!/usr/bin/env python3
"""nt_measured.py - suite nt_measured (plan 3.M5 --Sys loader, P39; ledger U2051): the NATIVE MEASUREMENTS behind the undocumented
system services of the --Sys loader (USER DECISION 2026-10-25: prototypes from System Informer's MIT-licensed phnt headers, behaviour
measured by our own self-written test PE tm_nt - Emulator/tests/pe/tm_nt - run natively in the VMware guest, decision 3.0.7).

Checked:
  1. Emulator/data/nt_measured_vmware_guest.txt: a header of '#' lines naming the source / placeholders, then only 'tm_nt <case>.<item>:'
     lines, every case of tm_nt present, no line twice; the identifier screen finds nothing (no SID string S-1-5-21-, no DESKTOP- name,
     no path below \\Users\\ or a drive letter, no raw address: every hex number of 9 .. 16 digits must be one of the listed constants
     - the placeholders <va>, <handle>, <ptr:+XH>, <value:T> stand for addresses / handles / values)
  2. the NATIVE REFERENCE (labelled; decision 3.0.7): only when the live ntdll.dll equals the VMware-guest capture (the measurement's
     environment) - tm_nt.exe runs once per case (a fresh process each: a case may leave threads or registrations behind) and its output
     must equal the recorded lines exactly; otherwise the step is reported as not applicable (another environment: its own data file
     is needed)
  3. tm_nt.exe is our own test PE: it is built into build\\x64\\<cfg>\\tests\\test_pe\\ and never run by the emulator
The emulator side (the answers implemented from these lines) is checked by emu-selftest nt_measured_services / nt_measured_pool /
nt_measured_loader / nt_measured_csr (P40) / nt_measured_console (P41) and by suite win_env_sys_loader. Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I nt_measured.py REPO_ROOT TESTS_DIR
"""
import hashlib
import importlib.util
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
CASES = ['sat', 'qsi', 'hvp', 'qip', 'wcp', 'qsix', 'iocp', 'wkf', 'tmr2', 'awcp', 'swf', 'etw', 'kd', 'of', 'tls', 'ahc', 'vmi', 'csr', 'sec', 'csrc', 'nls',   # P40 (U2064): + csrc, nls
         'con', 'etwt', 'tok', 'acc', 'bno', 'mut', 'wil', 'wnf', 'wnfe', 'wnfs', 't2s', 'hku', 'alert', 'term']   # P41 (U2068): a process started without a console
# P41 (U2068): case term ends the process with exit code 2BH (its NtTerminateProcess( NULL, 2BH ) fixes the process' exit status)
EXIT_CODE = {'term': 0x2B}
# hex constants (9 .. 16 digits) that may appear: system constants and the pre-set fill pattern - never an address of the run
ALLOWED_HEX = {'FFFF800000000000', 'FFFFFFFFFF676980', 'FFFFFFFFDC3CBA00', 'FFFFFFFFFF676980H', 'CCCCCCCCCCCCCCCC',
               # P41 (U2068): WNF state names of the system DLLs, a worker-factory idle timeout - constants
               '0280032EA3BC0875', '418A073AA3BC7C75', '418A073AA3BC88F5', '418A073AA3BC1C75', 'FFFFFFFFEE1E5D00'}


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def say(msg):
    print('[nt_measured] ' + msg)


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    data = os.path.join(root, 'Emulator', 'data', 'nt_measured_vmware_guest.txt')
    tm = os.path.join(tests, 'test_pe', 'tm_nt.exe')
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    if not check(os.path.isfile(data) and os.path.isfile(tm), 'the data file and tm_nt.exe exist (build.cmd first)'):
        return 1
    text = open(data, 'rb').read().decode('ascii')
    lines = text.replace('\r\n', '\n').rstrip('\n').split('\n')
    head = [l for l in lines if l.startswith('#')]
    body = [l for l in lines if not l.startswith('#')]
    check(len(head) >= 5 and all(not l.startswith('#') for l in lines[len(head):]), 'header of %d comment lines, then data only' % len(head))
    check(all(l.startswith('tm_nt ') for l in body), 'every data line is a tm_nt output line (%d lines)' % len(body))
    keys = [l.split(':', 1)[0] for l in body]
    check(len(keys) == len(set(keys)), 'no item twice')
    by_case = {c: [l for l in body if re.match(r'tm_nt %s\d*(\.|:)' % re.escape(c), l)] for c in CASES}   # P41: + a digit suffix (etwt5, wnf2 ...)
    check(all(by_case[c] for c in CASES) and sum(len(v) for v in by_case.values()) == len(body), 'every tm_nt case recorded (%s), no other line' % ', '.join(CASES))
    bad = []
    for l in body:
        if re.search(r'S-1-5-21-|DESKTOP-[A-Z0-9]{4}|\\Users\\|[A-Za-z]:\\', l):
            bad.append(l[:60])
        for h in re.findall(r'\b([0-9A-F]{9,16})H?\b', l):
            if h not in ALLOWED_HEX and not re.fullmatch(r'(CC)+', h):
                bad.append('%s: %s' % (l.split(':', 1)[0], h))
    check(not bad, 'identifier screen: no SID / computer name / user path / raw address (%d finding(s)%s)' % (len(bad), (': ' + '; '.join(bad[:3])) if bad else ''))
    # 2. the native reference
    t1 = load('win_env_t1')
    cap = t1.capture_dir(root, 'vmware_guest')
    cap_sha = hashlib.sha256(open(os.path.join(cap, 'ntdll.dll'), 'rb').read()).hexdigest() if cap else ''
    live = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', 'ntdll.dll')
    live_sha = hashlib.sha256(open(live, 'rb').read()).hexdigest() if os.path.isfile(live) else ''
    if cap_sha and live_sha == cap_sha:
        say('the live ntdll.dll equals the VMware-guest capture: the native reference applies')
        diffs = []
        # P40 (U2062): every case runs with the current directory on the system volume (<SystemRoot>) - case 'of' measures the
        # current directory's VOLUME (FileFsDeviceInformation Characteristics), and the emulated image directory lies on the system
        # volume; P39 ran it from wherever test.cmd ran (a worktree on E: gave 60020H, the main checkout on C: 20020H)
        sysroot = os.environ.get('SystemRoot', r'C:\Windows')
        for c in CASES:
            r = subprocess.run([tm, c], capture_output=True, timeout=300, cwd=sysroot)
            got = [l for l in r.stdout.decode('ascii', 'replace').replace('\r\n', '\n').split('\n') if l.startswith('tm_nt ')]
            if r.returncode != EXIT_CODE.get(c, 0) or got != by_case[c]:
                diffs.append('%s (exit %d, %d vs %d lines%s)' % (c, r.returncode, len(got), len(by_case[c]),
                                                                ', first difference ' + next((g.split(':', 1)[0] for g, w in zip(got, by_case[c]) if g != w), '-') if r.returncode == EXIT_CODE.get(c, 0) else ''))
        check(not diffs, 'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest): tm_nt, one process per case, equals the recorded lines '
              '(%d cases%s)' % (len(CASES), ('; differing: ' + ', '.join(diffs)) if diffs else ''))
    else:
        say('native reference: not applicable here (the live ntdll.dll is not the VMware-guest capture) - the recorded lines are those of the guest')
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: %d measured lines of %d cases, identifier screen clean%s' % (len(body), len(CASES), ', native reference equal' if cap_sha and live_sha == cap_sha else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
