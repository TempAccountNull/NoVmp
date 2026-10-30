#!/usr/bin/env python3
r"""win_env_cpulp.py - suite win_env_cpulp (P57 step 4, ledger U2377; USER DECISION 2026-10-30 plan 3.M7.a P56 (4) "per-processor CPUID"):
the emulated CPUID answers the logical processor the thread runs on - measured with our own test PE T11 (Emulator/tests/pe/t11_cpulp: pinned
to each processor of the process affinity mask in turn with SetThreadAffinityMask, CPUID 01H / 04H.0-3 / 06H / 0BH.0-2 / 1AH / 1FH.0-2 there,
one text line per (processor, leaf) to the standard output, the exit status 3B000000H OR an FNV-1a of every value).
Checked (T11 is read as data by emu-oep, never run by it; the system DLLs are mapped as data and run only in emulation):
  1. the recorded native reference Emulator/data/t11_cpulp_vmware_guest.txt: "exit X" + 8 processors x 13 leaves, each processor its own
     APIC ID in 01H EBX[31:24], 0BH.0-2 EDX and 1FH.0-2 EDX (SDM 253665-093 Vol1 pp. 21-9 / 21-39 / 21-71), the IDs distinct
  2. --guest --Sys --loader: T11 runs to its end (emu-oep exit 0, the guest exit status = the recorded one) and its console capture (P41: the
     process is started detached, every WriteFile attempt is recorded with its text) holds exactly the recorded lines in order
  3. the NATIVE REFERENCE (labelled, decision 3.0.7), only when the live ntdll equals the VMware-guest capture: T11 run natively from a copy
     in a fresh folder below %TEMP% (stdout captured) prints the recorded lines and exits with the recorded status
The --host side (20 logical processors, the P-core / E-core rows) is checked by emu-selftest engine_cpuid_per_lp against the bare-host
per-LP table (a T11 run under --host needs the bare-host registry items - not part of this suite). Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I win_env_cpulp.py REPO_ROOT TESTS_DIR
"""
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


T1H = load('win_env_t1')
LEAVES = ['01.00', '04.00', '04.01', '04.02', '04.03', '06.00', '0B.00', '0B.01', '0B.02', '1A.00', '1F.00', '1F.01', '1F.02']
LINE = re.compile(r'^t11 lp (\d\d) ([0-9A-F]{2}\.[0-9A-F]{2}) ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8})$')


def say(msg):
    print('[win_env_cpulp] ' + msg)


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    oep = os.path.join(tests, 'emu-oep.exe')
    t11 = os.path.join(tests, 'test_pe', 't11_cpulp.exe')
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in (oep, t11):
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    # 1. the recorded reference
    rec = [l.rstrip('\r\n') for l in open(os.path.join(root, 'Emulator', 'data', 't11_cpulp_vmware_guest.txt'), encoding='ascii') if not l.startswith('#')]
    want_exit = int(rec[0].split()[1], 16) if rec and rec[0].startswith('exit ') else None
    body = rec[1:]
    parsed = [LINE.match(l) for l in body]
    lps = sorted({int(m.group(1)) for m in parsed if m})
    ok = want_exit is not None and all(parsed) and len(body) == 13 * len(lps) == 104 and all(
        [m.group(2) for m in parsed[13 * k:13 * k + 13]] == LEAVES and {int(m.group(1)) for m in parsed[13 * k:13 * k + 13]} == {lps[k]} for k in range(len(lps)))
    apic = {}
    if ok:
        for k, lp in enumerate(lps):
            rows = {m.group(2): [int(m.group(i), 16) for i in range(3, 7)] for m in parsed[13 * k:13 * k + 13]}
            ids = {rows['01.00'][1] >> 24} | {rows[x][3] for x in ('0B.00', '0B.01', '0B.02', '1F.00', '1F.01', '1F.02')}
            apic[lp] = ids.pop() if len(ids) == 1 else None
    check(ok and len(apic) == 8 and None not in apic.values() and len(set(apic.values())) == 8,
          'the recorded native reference: exit %s, %d lines = 8 processors x 13 leaves; each processor ONE APIC ID in 01H EBX[31:24] / 0BH / 1FH EDX, '
          'the 8 IDs distinct (%s)' % (hex(want_exit) if want_exit is not None else None, len(body), ' '.join('%X' % apic[l] for l in sorted(apic)) if apic else '-'))
    # 2. the emulated run (--guest --Sys --loader; the console capture)
    od = os.path.join(tests, 'cpulp_out')
    os.makedirs(od, exist_ok=True)
    for name in ('console_capture.txt', 'fs_access.txt'):
        f = os.path.join(od, name)
        if os.path.isfile(f):
            os.remove(f)   # this suite's own output file of an earlier run
    rc, out = T1H.run([oep, '--guest', '--Sys', '--loader', '--out-dir', od, t11])
    term = [l for l in out if l.startswith('[emu-oep] result: guest exit status ')]
    got_exit = int(re.search(r'0x([0-9a-f]+)', term[0]).group(1), 16) if term else None
    check(rc == 0 and got_exit == want_exit, 'T11 --guest --Sys --loader: emu-oep exit %d, the guest exit status %s = the recorded %s' % (
        rc, hex(got_exit) if got_exit is not None else None, hex(want_exit) if want_exit is not None else None))
    cf = os.path.join(od, 'console_capture.txt')
    cap = open(cf, encoding='utf-8').read() if os.path.isfile(cf) else ''
    emu = [m.group(1).replace('\\r\\n', '') for m in re.finditer(r'WriteFile handle 0x0 length 53 text "(.*?)" -> ', cap)]
    first = next((i for i, (a, b) in enumerate(zip(emu, body)) if a != b), None)
    check(emu == body, 'T11 --guest: the console capture holds the %d recorded lines in order (got %d; first difference at %s)' % (len(body), len(emu), first))
    moves = [l for l in out if 'NtSetInformationThread' in l and 'the thread now runs on logical processor' in l]
    check(len(moves) == 8, 'T11 --guest: the eight one-processor masks move the thread (NtSetInformationThread 4: "the thread now runs on logical processor N", %d)' % len(moves))
    # 3. the native reference
    capd = T1H.capture_dir(root, 'vmware_guest')
    live = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', 'ntdll.dll')
    if capd and os.path.isfile(live) and hashlib.sha256(open(live, 'rb').read()).hexdigest() == hashlib.sha256(open(os.path.join(capd, 'ntdll.dll'), 'rb').read()).hexdigest():
        folder = tempfile.mkdtemp(prefix='win_env_t11_')
        path = os.path.join(folder, 't11_cpulp.exe')
        shutil.copyfile(t11, path)
        r = subprocess.run([path], capture_output=True, text=True, timeout=120)
        for k in range(30):   # our one copy, then its folder (retried while the image is still held)
            try:
                if os.path.exists(path):
                    os.remove(path)
                os.rmdir(folder)
                break
            except OSError:
                time.sleep(0.5)
        nat = [l for l in r.stdout.splitlines()]
        check(r.returncode & 0xFFFFFFFF == want_exit and nat == body,
              'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest): exit %s, %d lines = the recorded reference' % (hex(r.returncode & 0xFFFFFFFF), len(nat)))
    else:
        say('native reference: not applicable here (the live ntdll.dll is not the VMware-guest capture)')
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: T11 pinned to each of the 8 processors reads each processor\'s own APIC ID under --guest --Sys --loader, as natively')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
