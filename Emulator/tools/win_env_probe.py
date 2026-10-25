#!/usr/bin/env python3
"""win_env_probe.py - suite win_env_probe (plan 3.M5 part 2, ledger U2023): emu-oep --counters / --trace on the test PE
tp_probe (Emulator/tests/pe/tp_probe, self-written guest code, never sample-derived).

tp_probe executes a fixed probe sequence (CPUID leaf 0, 1, 7.0, 40000000H; RDTSC x2; RDTSCP; TEB ProcessEnvironmentBlock,
TEB NtTib.Self x2; PEB BeingDebugged, PEB NtGlobalFlag; KUSER KdDebuggerEnabled) and exits with one direct SYSCALL of
NtTerminateProcess (number patched from the VMware-guest environment's ntdll capture by win_env_t1.py's independent parser,
decision 3.0.8) with status 2FH. Checked (the image is read as data by emu-oep, never run by it):
  --counters, --Sys and --HLE: exit 0, guest status 2FH; CPUID 4 with exactly the four (leaf, sub-leaf) pairs, each x1 and
      answered; RDTSC 2, RDTSCP 1, RDPID 0; reads TEB.ProcessEnvironmentBlock x1, TEB.NtTib+0x30 x2, PEB.BeingDebugged x1,
      PEB.NtGlobalFlag x1, KUSER.KdDebuggerEnabled x1; system call NtTerminateProcess x1; API calls 0; exceptions 0; a tail
      whose last instruction is the SYSCALL; the observable lines equal to a run without --counters (instrumentation only
      observes)
  --trace FILE: one line per counted instruction (lines = "instructions executed"), the first line the entry, the last the
      SYSCALL; a trace without --counters gives the same observable lines
  without --counters: no "probe:" line
  the NATIVE REFERENCE (decision 3.0.7, only when the live ntdll equals the capture): tp_probe natively exits 2FH
Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I win_env_probe.py REPO_ROOT TESTS_DIR [--oep EMU_OEP_EXE] [--scratch DIR]
"""
import hashlib
import importlib.util
import os
import re
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


T1H = load('win_env_t1')


def say(msg):
    print('[win_env_probe] ' + msg)


WANT_CPUID = {(0x0, 0), (0x1, 0), (0x7, 0), (0x40000000, 0)}
WANT_READS = {'TEB.ProcessEnvironmentBlock': 1, 'TEB.NtTib+0x30': 2, 'PEB.BeingDebugged': 1, 'PEB.NtGlobalFlag': 1, 'KUSER.KdDebuggerEnabled': 1}


def main(argv):
    opts = {}
    pos = []
    i = 0
    while i < len(argv):
        if argv[i] in ('--oep', '--scratch') and i + 1 < len(argv):
            opts[argv[i]] = argv[i + 1]
            i += 2
        else:
            pos.append(argv[i])
            i += 1
    if len(pos) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(pos[0]), os.path.abspath(pos[1])
    oep = os.path.abspath(opts.get('--oep', os.path.join(tests, 'emu-oep.exe')))
    pe_dir = os.path.join(tests, 'test_pe')
    tp = os.path.join(pe_dir, 'tp_probe.exe')
    scratch = opts.get('--scratch') or tempfile.mkdtemp(prefix='win_env_probe_')
    os.makedirs(scratch, exist_ok=True)
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in (oep, tp):
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    cap = T1H.capture_dir(root, 'vmware_guest')
    if not cap:
        say('FAIL no VMware-guest system-DLL capture')
        return 1
    nt = open(os.path.join(cap, 'ntdll.dll'), 'rb').read()
    n = T1H.stub_numbers(nt).get('NtTerminateProcess')
    if not check(n is not None, 'NtTerminateProcess is a stub of the captured ntdll (own parser)'):
        return 1
    img = open(tp, 'rb').read()
    at = img.find(b'TPSYSNUM')
    if not check(img.count(b'TPSYSNUM') == 1 and struct.unpack_from('<I', img, at + 8)[0] == 0xFFFFFFFF, 'tp_probe: marker TPSYSNUM once, field unpatched'):
        return 1
    b = bytearray(img)
    struct.pack_into('<I', b, at + 8, n)
    tpp = os.path.join(pe_dir, 'tp_probe.vmware_guest.exe')
    open(tpp, 'wb').write(bytes(b))

    def status(out):
        s = [l for l in out if l.startswith('[emu-oep] observable: exit status: ')]
        return s[0].split(': ', 2)[2] if len(s) == 1 else None

    def probes(out):
        return [l[len('[emu-oep] probe: '):] for l in out if l.startswith('[emu-oep] probe: ')]

    obs = {}
    for mode in ('--Sys', '--HLE'):
        rc, plain = T1H.run([oep, '--guest', mode, tpp])
        check(rc == 0 and status(plain) == '0x0000002f (47)' and not probes(plain), 'tp_probe %s without --counters: exit 0, status 2FH, no probe line' % mode)
        rc, out = T1H.run([oep, '--guest', mode, '--counters', tpp])
        if not check(rc == 0 and status(out) == '0x0000002f (47)', 'tp_probe %s --counters: emu-oep exit 0, guest status 2FH (got rc %d, %s)' % (mode, rc, status(out))):
            continue
        pr = probes(out)
        head = [l for l in pr if l.startswith('instructions ')]
        check(len(head) == 1 and re.search(r'; CPUID 4 \(4 leaf / sub-leaf pairs\), RDTSC 2, RDTSCP 1, RDPID 0$', head[0]) is not None,
              '%s: CPUID 4 (4 pairs), RDTSC 2, RDTSCP 1, RDPID 0 (%s)' % (mode, head[0] if head else '-'))
        got = set()
        for l in pr:
            m = re.match(r'cpuid leaf ([0-9A-F]{8})H sub-leaf \(ECX\) ([0-9A-F]{8})H x(\d+), first at .*, answered EAX [0-9A-F]{8} ', l)
            if m and m.group(3) == '1':
                got.add((int(m.group(1), 16), int(m.group(2), 16)))
        check(got == WANT_CPUID, '%s: the four CPUID pairs, each x1 with an answer (%s)' % (mode, sorted(hex(a) for a, _ in got)))
        reads = {}
        for l in pr:
            m = re.match(r'read (\S+) x(\d+), first at ', l)
            if m:
                reads[m.group(1)] = int(m.group(2))
        check(reads == WANT_READS, '%s: environment reads exactly %s (got %s)' % (mode, WANT_READS, reads))
        check('system calls 1 (1 services)' in pr and 'syscall NtTerminateProcess x1' in pr and 'API calls 0 (0 APIs)' in pr and
              any(l.startswith('exceptions 0 ') for l in pr), '%s: 1 system call (NtTerminateProcess), 0 API calls, 0 exceptions' % mode)
        tail = [l for l in pr if l.startswith('tail #')]
        check(len(tail) >= 2 and tail[-1].endswith('syscall'), '%s: the tail ends with the SYSCALL (%d entries)' % (mode, len(tail)))
        obs[mode] = (T1H.observable(plain), T1H.observable(out))
        check(obs[mode][0] == obs[mode][1] and len(obs[mode][0]) > 20,
              '%s: the observable lines with --counters equal those without (instrumentation only observes; %d lines)' % (mode, len(obs[mode][0])))
        trace = os.path.join(scratch, 'tp_probe_%s.trace' % mode.strip('-').lower())
        if os.path.exists(trace):
            os.remove(trace)
        rc, tout = T1H.run([oep, '--guest', mode, '--trace', trace, tpp])
        insns = [l for l in tout if l.startswith('[emu-oep] observable: instructions executed: ')]
        lines = open(trace, encoding='ascii').read().splitlines() if os.path.isfile(trace) else []
        n_insn = int(insns[0].split(': ')[2].split(' ')[0]) if insns else -1
        check(rc == 0 and n_insn > 10 and len(lines) == n_insn and lines[0].startswith('1 ') and lines[-1].endswith('syscall') and
              T1H.observable(tout) == obs[mode][0],
              '%s --trace: %d lines = %d instructions, first = #1, last = the SYSCALL, observable lines unchanged' % (mode, len(lines), n_insn))
        if os.path.exists(trace):
            os.remove(trace)
    live = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', 'ntdll.dll')
    if check(hashlib.sha256(open(live, 'rb').read()).hexdigest() == hashlib.sha256(nt).hexdigest(),
             'the live ntdll.dll equals the guest capture (SHA-256): the number is valid for this kernel'):
        r = subprocess.run([tpp], capture_output=True, timeout=60)
        check(r.returncode == 0x2F, 'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest): tp_probe exit code %s == 2FH' % hex(r.returncode & 0xFFFFFFFF))
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: tp_probe --counters in --Sys and --HLE (CPUID 4 pairs answered, RDTSC 2, RDTSCP 1, 6 environment reads by member, 1 system call), '
        '--trace one line per instruction, instrumentation observable-neutral, native reference 2FH')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
