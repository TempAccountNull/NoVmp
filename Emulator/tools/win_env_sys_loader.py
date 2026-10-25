#!/usr/bin/env python3
"""win_env_sys_loader.py - suite win_env_sys_loader (plan 3.M5 part 2, ledger U2039): the opt-in --Sys --loader start through the
VMware-guest environment's real ntdll!LdrInitializeThunk, up to its FRONTIER - the first system service no allowed source
describes (notes/eread/P36_phase3_m5b.md).

Checked (the test PE T2 is read as data by emu-oep, never run by it; the environment's ntdll / kernel32 / kernelbase are mapped as
data and their code runs only in emulation):
  - the capture holds the loader files and the registry subset (capture_sysdlls.py --dll / --nls / --reg / --reg-value)
  - T2 --Sys without --loader: refused (exit 3) as in milestone M5 (an image with imports needs the loader start)
  - T2 --Sys --loader: the kernel-side start (ntdll mapped, the NLS and API set views, the CONTEXT on the stack, IRETQ to
    LdrInitializeThunk with RCX = the CONTEXT, RDX = the ntdll base) and every system service the real loader calls is ANSWERED
    (status in RAX) up to the frontier, in this order: FRONTIER_PATH; then the loud stop (exit 4, a state dump) at
    NtQuerySecurityAttributesToken - no SDK / WDK prototype and no implementation in Wine 11.0 / ReactOS 0.4.16
  - nothing reached the host (0 forwarded); the loader read the PEB NLS / process parameter members the kernel wrote (--counters)
The done criterion of plan 3.M5 (T2 / T3 pass under --Sys) is NOT met while this frontier stands; this suite fails if the frontier
moves (a newly answered service must update FRONTIER_PATH and the notes). Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I win_env_sys_loader.py REPO_ROOT TESTS_DIR
"""
import importlib.util
import os
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


T1H = load('win_env_t1')
FRONTIER_PATH = ['NtQueryPerformanceCounter', 'NtProtectVirtualMemory', 'NtProtectVirtualMemory', 'NtCreateEvent', 'NtQuerySystemInformation',
                 'NtQueryInformationProcess', 'NtQuerySystemInformation', 'NtQueryVirtualMemory', 'NtQueryVirtualMemory', 'NtProtectVirtualMemory',
                 'NtOpenKey', 'NtQueryValueKey', 'NtClose', 'NtOpenKey', 'NtOpenKey', 'NtOpenKey', 'NtQueryInformationProcess', 'NtQueryInformationProcess']
FRONTIER = 'NtQuerySecurityAttributesToken'


def say(msg):
    print('[win_env_sys_loader] ' + msg)


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    oep = os.path.join(tests, 'emu-oep.exe')
    t2 = os.path.join(tests, 'test_pe', 't2_import.exe')
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in (oep, t2):
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    cap = T1H.capture_dir(root, 'vmware_guest')
    man = T1H.manifest(cap) if cap else {}
    check(cap and all('file_' + n in man for n in ('kernel32.dll', 'kernelbase.dll', 'apisetschema.dll')) and 'nls_case' in man and
          os.path.isfile(os.path.join(cap, 'registry.txt')), 'the capture holds the loader files and the registry subset (U2024 / U2034)')
    rc, out = T1H.run([oep, '--guest', '--Sys', t2])
    check(rc == 3 and any(l.startswith('[emu-oep] error: --Sys:') and 'LdrInitializeThunk' in l for l in out), 'T2 --Sys without --loader: refused, exit 3')
    rc, out = T1H.run([oep, '--guest', '--Sys', '--loader', '--counters', t2])
    obs = T1H.observable(out)
    check(any('env: NLS view at ' in l for l in obs) and any('env: API set schema view at ' in l and 'schema version 6' in l for l in obs) and
          any('env: --Sys loader start: CONTEXT at ' in l and 'RDX = the ntdll base' in l for l in obs) and
          any(l.startswith('[emu-oep] run: IRETQ to ntdll!LdrInitializeThunk') for l in out),
          'T2 --Sys --loader: the kernel-side start (NLS / API set views, the CONTEXT, IRETQ to LdrInitializeThunk with RCX / RDX)')
    sc = [l for l in obs if re.match(r'\[emu-oep\] observable: syscall \d+: ', l) and ' number ' in l]
    names = [re.match(r'\[emu-oep\] observable: syscall \d+: (\S+) ', l).group(1) for l in sc]
    answered = [' -> status ' in l for l in sc]
    check(names[:-1] == FRONTIER_PATH and all(answered[:-1]), 'the %d services before the frontier, in order, all answered (got %s)' % (len(FRONTIER_PATH), names[:-1]))
    check(rc == 4 and names and names[-1] == FRONTIER and not answered[-1] and any(l.startswith('[emu-oep] STATE DUMP (stop: unimplemented system service)') for l in out),
          'the frontier: a loud stop with a state dump at %s (exit 4)' % FRONTIER)
    check(any('forwarded to the host: 0 (no such path exists)' in l for l in obs), 'nothing forwarded to the host')
    reads = [l for l in out if l.startswith('[emu-oep] probe: read ')]
    check(any('PEB.AnsiCodePageData' in l for l in reads) and any('PEB.UnicodeCaseTableData' in l for l in reads) and any('PARAMS.ImagePathName' in l for l in reads),
          'the real loader read the NLS pointers and the process parameters the kernel side wrote')
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: the --Sys --loader start runs the real LdrInitializeThunk through %d answered services to the frontier %s (loud stop); the plan 3.M5 '
        'done criterion for T2 / T3 under --Sys is NOT met (P36)' % (len(FRONTIER_PATH), FRONTIER))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
