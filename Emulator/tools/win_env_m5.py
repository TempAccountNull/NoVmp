#!/usr/bin/env python3
"""win_env_m5.py - suite win_env_m5 (plan 3.M5, ledger U2016): the test PEs T2 / T3 / T4 in the emulated Windows environment.

T2 (Emulator/tests/pe/t2_import): kernel32.dll imports GetCurrentProcessId / GetModuleHandleW / ExitProcess - exit status 2BH.
T3 (Emulator/tests/pe/t3_tls): a TLS directory with one callback, the exit by a direct SYSCALL - exit status 2EH.
T4 (Emulator/tests/pe/t4_direct): direct SYSCALLs from its own code and from allocated memory (NtQueryInformationProcess with
EAX bits 31:16 set, NtAllocateVirtualMemory, NtQueryVirtualMemory through a stub the guest writes, NtFreeVirtualMemory,
NtTerminateProcess) - exit status 2DH.
All three are self-written guest code (never sample-derived). The service numbers T3 / T4 need are not built in: this harness
reads them from the VMware-guest environment's ntdll capture with its own PE parser (win_env_t1.py's, decision 3.0.8), requires
emu-oep's parser to agree, and writes patched copies into the build output (<name>.vmware_guest.exe). Then emu-oep (the image is
read as data and mapped into guest memory, never run by emu-oep):
  T4  --Sys and --HLE: exit 0, guest exit status 2DH, 6 SYSCALLs all handled by the emulated kernel layer, the 2nd dispatched on
      EAX AND FFFFH with bits 31:16 set, the 4th executed from allocated memory (plan 1.K.6), the "observable:" lines identical
      between the modes; the unpatched T4 (EAX FFFFFFFFH) = a loud stop with a state dump (exit 4)
  T2  --HLE: guest exit status 2BH through the API stubs (3 API calls, no SYSCALL); --Sys: REFUSED (exit 3) - the real
      ntdll!LdrInitializeThunk start is not modelled in milestone M5 (no cited source; notes/eread/P35_phase3_m5.md)
  T3  --HLE: guest exit status 2EH (the callback ran once before the entry with DLL_PROCESS_ATTACH, the TLS block right);
      --Sys: REFUSED (exit 3), as T2
  the NATIVE REFERENCE (decision 3.0.7: our own self-written test PEs may run natively in the VMware guest as a labelled
  reference): only when the live %SystemRoot%\\System32\\ntdll.dll equals the capture (the numbers are then right for this
  kernel), T2, the patched T3 and T4 run natively; each exit code must equal the emulated --HLE exit status.
The milestone's done criterion "all pass in both modes" is NOT claimed by this suite: --Sys runs T4 only; T2 / T3 under --Sys
are checked to be refused loudly (the open item of P35). Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I win_env_m5.py REPO_ROOT TESTS_DIR
"""
import hashlib
import importlib.util
import os
import re
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


T1H = load('win_env_t1')   # capture_dir, manifest, stub_numbers (the independent export parser), run, observable


def say(msg):
    print('[win_env_m5] ' + msg)


T4_ORDER = ('NtQueryInformationProcess', 'NtAllocateVirtualMemory', 'NtQueryVirtualMemory', 'NtFreeVirtualMemory', 'NtTerminateProcess')


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    oep = os.path.join(tests, 'emu-oep.exe')
    pe_dir = os.path.join(tests, 'test_pe')
    t2, t3, t4 = (os.path.join(pe_dir, n) for n in ('t2_import.exe', 't3_tls.exe', 't4_direct.exe'))
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in (oep, t2, t3, t4):
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    cap = T1H.capture_dir(root, 'vmware_guest')
    if not cap:
        say('FAIL no VMware-guest system-DLL capture (run python -I Emulator\\tools\\capture_sysdlls.py --env vmware_guest in the guest)')
        return 1
    man = T1H.manifest(cap)
    nt = open(os.path.join(cap, 'ntdll.dll'), 'rb').read()
    cap_sha = hashlib.sha256(nt).hexdigest()
    check(man.get('environment') == 'vmware_guest' and int(man.get('ntdll_size', '-1')) == len(nt) and man.get('ntdll_sha256') == cap_sha,
          'capture %s: manifest environment / size / SHA-256 match the ntdll.dll copy' % cap)
    stubs = T1H.stub_numbers(nt)
    nums = {}
    for name in T4_ORDER:
        n = stubs.get(name)
        if not check(n is not None, '%s is a stub of the captured ntdll (own parser)' % name):
            return 1
        rc, out = T1H.run([oep, '--guest', '--syscall-number', name])
        m = [x for x in (re.search(r'syscall %s = 0x([0-9a-f]+) ' % name, l) for l in out) if x]
        check(rc == 0 and len(m) == 1 and int(m[0].group(1), 16) == n, 'emu-oep --syscall-number %s = %s (own parser %s)' % (name, m[0].group(1) if m else '?', hex(n)))
        nums[name] = n

    def patched(src, marker, values, tag):
        img = open(src, 'rb').read()
        at = img.find(marker)
        if not check(img.count(marker) == 1 and all(struct.unpack_from('<I', img, at + 8 + 4 * i)[0] == 0xFFFFFFFF for i in range(len(values))),
                     '%s: marker %s once, %d field(s) unpatched (FFFFFFFFH)' % (os.path.basename(src), marker.decode(), len(values))):
            return None
        b = bytearray(img)
        for i, v in enumerate(values):
            struct.pack_into('<I', b, at + 8 + 4 * i, v)
        p = os.path.join(pe_dir, os.path.basename(src)[:-4] + '.%s.exe' % tag)
        open(p, 'wb').write(bytes(b))
        return p

    t3p = patched(t3, b'T3SYSNUM', [nums['NtTerminateProcess']], 'vmware_guest')
    t4p = patched(t4, b'T4SYSNUM', [nums[n] for n in T4_ORDER], 'vmware_guest')
    if not t3p or not t4p:
        return 1
    say('patched copies: %s, %s' % (os.path.basename(t3p), os.path.basename(t4p)))

    def status(out):
        s = [l for l in out if l.startswith('[emu-oep] observable: exit status: ')]
        return s[0].split(': ', 2)[2] if len(s) == 1 else None

    want = {'t2': 0x2B, 't3': 0x2E, 't4': 0x2D}
    # T4: both modes
    res = {}
    for mode in ('--Sys', '--HLE'):
        rc, out = T1H.run([oep, '--guest', mode, t4p])
        res[mode] = out
        check(rc == 0 and status(out) == '0x%08x (%d)' % (want['t4'], want['t4']), 'T4 %s: emu-oep exit 0, guest exit status 2DH (got %s)' % (mode, status(out)))
        check(any('syscall instructions executed by the guest: 6; handled by the emulated kernel layer: 6; forwarded to the host: 0' in l for l in out),
              'T4 %s: 6 SYSCALLs, all handled by the emulated kernel layer, none forwarded' % mode)
        sc = [l for l in out if l.startswith('[emu-oep] observable: syscall ') and ' number ' in l]
        check(len(sc) == 6 and 'rax 0xabcd%04x' % nums['NtQueryInformationProcess'] in sc[1] and 'status 00000000H' in sc[1],
              'T4 %s: the 2nd SYSCALL had EAX bits 31:16 set (ABCDH) and was dispatched on EAX AND FFFFH (plan 6.4.a)' % mode)
        if len(sc) == 6:
            at = re.search(r' at 0x([0-9a-f]+) ', sc[3])
            check(at is not None and 'NtQueryVirtualMemory' in sc[3] and not (0x140000000 <= int(at.group(1), 16) < 0x140010000),
                  'T4 %s: the 4th SYSCALL (NtQueryVirtualMemory) came from allocated memory %s, not from the image (plan 1.K.6)' % (mode, at.group(1) if at else '?'))
        check(out and out[-1].startswith('[emu-oep] result: guest exit status 0x%08x; the emu-oep process continues' % want['t4']),
              'T4 %s: runtime no host pass-through - emu-oep continued after the guest\'s NtTerminateProcess and exited 0' % mode)
    o1, o2 = T1H.observable(res['--Sys']), T1H.observable(res['--HLE'])
    diff = [i for i in range(max(len(o1), len(o2))) if i >= len(o1) or i >= len(o2) or o1[i] != o2[i]]
    check(len(o1) > 100 and not diff, 'T4: --Sys and --HLE observable results identical (%d lines; differing %d)' % (len(o1), len(diff)))
    rc, out = T1H.run([oep, '--guest', t4])
    check(rc == 4 and any(l.startswith('[emu-oep] STATE DUMP (stop: unknown system service)') for l in out) and not any('observable: exit status' in l for l in out),
          'T4 unpatched (EAX FFFFFFFFH): loud stop "unknown system service" with a state dump, exit 4')
    # T2 / T3: --HLE runs them, --Sys refuses them
    for tag, path in (('t2', t2), ('t3', t3p)):
        rc, out = T1H.run([oep, '--guest', '--HLE', path])
        check(rc == 0 and status(out) == '0x%08x (%d)' % (want[tag], want[tag]), '%s --HLE: guest exit status %02XH (got %s)' % (tag.upper(), want[tag], status(out)))
        if tag == 't2':
            api = [l for l in out if l.startswith('[emu-oep] observable: api call ')]
            check(len(api) == 3 and 'KERNEL32.dll!GetCurrentProcessId' in api[0] and 'KERNEL32.dll!GetModuleHandleW' in api[1] and 'KERNEL32.dll!ExitProcess' in api[2] and
                  any('syscall instructions executed by the guest: 0;' in l for l in out),
                  'T2 --HLE: 3 API calls through the stubs (GetCurrentProcessId, GetModuleHandleW( NULL ), ExitProcess), no SYSCALL')
        else:
            check(any('TLS callback 0x' in l and 'DLL_PROCESS_ATTACH' in l for l in out) and any('SYSRETQ to the TLS start trampoline' in l for l in out),
                  'T3 --HLE: the callback run from the start trampoline before the entry')
        rc, out = T1H.run([oep, '--guest', '--Sys', path])
        check(rc == 3 and any('LdrInitializeThunk' in l and l.startswith('[emu-oep] error: --Sys:') for l in out),
              '%s --Sys: refused loudly (exit 3) - the real LdrInitializeThunk start is not modelled in M5 (P35 open item)' % tag.upper())
    # the native reference (decision 3.0.7), only when the live ntdll IS the captured one
    live = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', 'ntdll.dll')
    live_sha = hashlib.sha256(open(live, 'rb').read()).hexdigest()
    if check(live_sha == cap_sha, 'the live ntdll.dll equals the guest capture (SHA-256): the numbers are valid for this kernel'):
        for tag, path in (('t2', t2), ('t3', t3p), ('t4', t4p)):
            r = subprocess.run([path], capture_output=True, timeout=60)
            check(r.returncode == want[tag], 'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest): %s exit code %s == the emulated %s'
                  % (tag.upper(), hex(r.returncode & 0xFFFFFFFF), hex(want[tag])))
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: T4 2DH in --Sys and --HLE (identical observable results, SYSCALL from allocated memory, EAX AND FFFFH), T2 2BH and T3 2EH '
        'under --HLE, T2 / T3 refused under --Sys (M5 open item), native references equal')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
