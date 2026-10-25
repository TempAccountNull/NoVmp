#!/usr/bin/env python3
"""win_env_t1.py - suite win_env_t1 (plan 3.M4, ledger U1986): the test PE T1 in the emulated Windows environment.

T1 (Emulator/tests/pe/t1_exit, self-written guest code, never sample-derived) makes ONE direct SYSCALL of
NtTerminateProcess( NtCurrentProcess(), 2AH ). Its service number is not built in: Windows renumbers services between builds,
and decision 3.0.8 says the numbers come from the emulated environment's own ntdll (the local capture made by
capture_sysdlls.py, never committed). How the number reaches T1: this harness
  1. finds the VMware-guest capture (<capture root>/sysdlls/vmware_guest: manifest.txt + ntdll.dll; capture root =
     $NOVMP_CAPTURE_ROOT or the first ancestor of the repository holding capture/sysdlls/vmware_guest/manifest.txt) and checks
     the copy's size and SHA-256 against the manifest;
  2. reads the number of NtTerminateProcess from the capture's export stub (4C 8B D1 B8 imm32) with its OWN PE parser and
     requires emu-oep's parser (--syscall-number) to report the same number;
  3. writes a copy of the built T1 with the 4-byte field after the marker "T1SYSNUM" set to that number
     (build/x64/<cfg>/tests/test_pe/t1_exit.vmware_guest.exe; build output, never committed) - plus negative variants;
then runs emu-oep (the image is read as data and mapped into guest memory, never run by emu-oep):
  a. --guest --Sys and --guest --HLE: exit 0, guest exit status 0000002AH, and the "observable:" lines of the two runs
     IDENTICAL (the mode is recorded in the log only);
  b. the same at another base (--image-base 7FF700000000H, the relocation applied): exit status 2AH, identical between modes;
  c. --host: refused loudly (exit 3) while no bare-host DLL capture exists (a capture appears with the host package);
  d. loud failures with a state dump (exit 4): the unpatched T1 (EAX FFFFFFFFH), a number one above the table's highest,
     and NtClose's number (known, not implemented in M4);
  e. runtime "no host pass-through": the emulated NtTerminateProcess ended only the emulated process - emu-oep printed its
     result after it and exited 0 (not 2AH), and the guest's one SYSCALL was handled by the emulated kernel layer;
  f. the NATIVE REFERENCE (decision 3.0.7: our own self-written test PE may run natively in the VMware guest as a labelled
     reference): only when the live %SystemRoot%\\System32\\ntdll.dll equals the capture (same SHA-256 - the number is then
     right for this kernel), T1 runs natively and its exit code must equal the emulated exit status.
Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I win_env_t1.py REPO_ROOT TESTS_DIR
"""
import hashlib
import os
import re
import struct
import subprocess
import sys

ENV_VAR = 'NOVMP_CAPTURE_ROOT'
EXIT_CODE = 0x2A
REBASE = '0x7ff700000000'


def say(msg):
    print('[win_env_t1] ' + msg)


def capture_dir(root, env):
    r = os.environ.get(ENV_VAR, '').strip()
    cands = [os.path.join(r, 'sysdlls', env)] if r else []
    p = os.path.abspath(root)
    while True:
        cands.append(os.path.join(p, 'capture', 'sysdlls', env))
        q = os.path.dirname(p)
        if q == p:
            break
        p = q
    for c in cands:
        if os.path.isfile(os.path.join(c, 'manifest.txt')):
            return c
    return None


def manifest(d):
    kv = {}
    for line in open(os.path.join(d, 'manifest.txt'), encoding='ascii'):
        line = line.rstrip('\r\n')
        if line and not line.startswith('#') and ' ' in line:
            k, v = line.split(' ', 1)
            kv[k] = v
    return kv


def pe_sections(data):
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE')
    nsec, optsz = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from('<H', data, opt)[0] != 0x20B:
        raise ValueError('not PE32+')
    dirs = [struct.unpack_from('<II', data, opt + 112 + 8 * i) for i in range(16)]
    secs = []
    so = opt + optsz
    for _ in range(nsec):
        vs, va, rs, rp = struct.unpack_from('<IIII', data, so + 8)
        secs.append((va, vs, rp, rs))
        so += 40
    return dirs, secs


def rva_off(secs, rva):
    for va, vs, rp, rs in secs:
        if va <= rva < va + rs:   # inside the section's raw data
            return rva - va + rp
    raise ValueError('rva %x not in the file' % rva)


def stub_numbers(data):
    """{name: number} of every Nt* export whose code is 4C 8B D1 B8 imm32 (an independent parser of the export table)."""
    dirs, secs = pe_sections(data)
    erva, esz = dirs[0]
    e = rva_off(secs, erva)
    # IMAGE_EXPORT_DIRECTORY (winnt.h): Characteristics, TimeDateStamp, Major / MinorVersion, Name, Base, NumberOfFunctions,
    # NumberOfNames, AddressOfFunctions, AddressOfNames, AddressOfNameOrdinals
    _, _, _, _, _, _, nfun, nnam, afun, anam, aord = struct.unpack_from('<IIHHIIIIIII', data, e)
    fo, no, oo = rva_off(secs, afun), rva_off(secs, anam), rva_off(secs, aord)
    out = {}
    for i in range(nnam):
        nrva = struct.unpack_from('<I', data, no + 4 * i)[0]
        idx = struct.unpack_from('<H', data, oo + 2 * i)[0]
        so = rva_off(secs, nrva)
        name = data[so:data.index(b'\0', so)].decode('ascii')
        frva = struct.unpack_from('<I', data, fo + 4 * idx)[0]
        if not name.startswith('Nt') or erva <= frva < erva + esz:
            continue
        b = data[rva_off(secs, frva):rva_off(secs, frva) + 8]
        if b[:4] == b'\x4c\x8b\xd1\xb8':
            out[name] = struct.unpack_from('<I', b, 4)[0]
    return out


def run(cmd, timeout=300):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return r.returncode, r.stdout.splitlines()


def observable(lines):
    return [l for l in lines if l.startswith('[emu-oep] observable: ')]


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    oep = os.path.join(tests, 'emu-oep.exe')
    t1 = os.path.join(tests, 'test_pe', 't1_exit.exe')
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in (oep, t1):
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    # 1. the environment's capture (decision 3.0.8)
    cap = capture_dir(root, 'vmware_guest')
    if not cap:
        say('FAIL no VMware-guest system-DLL capture (run python -I Emulator\\tools\\capture_sysdlls.py --env vmware_guest in the guest)')
        return 1
    man = manifest(cap)
    nt = open(os.path.join(cap, 'ntdll.dll'), 'rb').read()
    cap_sha = hashlib.sha256(nt).hexdigest()
    check(man.get('environment') == 'vmware_guest' and int(man.get('ntdll_size', '-1')) == len(nt) and man.get('ntdll_sha256') == cap_sha,
          'capture %s: manifest environment / size / SHA-256 match the ntdll.dll copy (%d bytes)' % (cap, len(nt)))
    # 2. the number: own parser == emu-oep's parser
    stubs = stub_numbers(nt)
    num = stubs.get('NtTerminateProcess')
    if not check(num is not None, 'NtTerminateProcess is a stub of the captured ntdll (%d Nt* stubs, own parser)' % len(stubs)):
        return 1
    rc, out = run([oep, '--guest', '--syscall-number', 'NtTerminateProcess'])
    m = [re.search(r'syscall NtTerminateProcess = 0x([0-9a-f]+) ', l) for l in out]
    m = [x for x in m if x]
    check(rc == 0 and len(m) == 1 and int(m[0].group(1), 16) == num,
          'emu-oep --syscall-number NtTerminateProcess = %s (own parser %s)' % (m[0].group(1) if m else '?', hex(num)))
    unknown = max(stubs.values()) + 1
    close = stubs.get('NtClose')
    check(close is not None and unknown <= 0xFFF and unknown not in stubs.values(), 'negative numbers: one above the highest (%s), NtClose (%s)' % (hex(unknown), hex(close or 0)))
    # 3. the patched copies (build output)
    img = open(t1, 'rb').read()
    at = img.find(b'T1SYSNUM')
    if not check(img.count(b'T1SYSNUM') == 1 and struct.unpack_from('<I', img, at + 8)[0] == 0xFFFFFFFF, 'T1 marker found once, field unpatched (FFFFFFFFH)'):
        return 1

    def variant(tag, n):
        b = bytearray(img)
        struct.pack_into('<I', b, at + 8, n)
        p = os.path.join(tests, 'test_pe', 't1_exit.%s.exe' % tag)
        open(p, 'wb').write(bytes(b))
        return p
    patched = variant('vmware_guest', num)
    v_unknown = variant('unknown_number', unknown)
    v_close = variant('ntclose', close or 0)
    say('T1 for the environment: %s (NtTerminateProcess %s); negative variants: %s, %s' % (patched, hex(num), os.path.basename(v_unknown), os.path.basename(v_close)))

    # a / b / e: both modes, both bases
    for base_args, label in (([], 'ImageBase'), (['--image-base', REBASE], 'rebased ' + REBASE)):
        res = {}
        for mode in ('--Sys', '--HLE'):
            rc, out = run([oep, '--guest', mode] + base_args + [patched])
            res[mode] = (rc, out)
            status = [l for l in out if l.startswith('[emu-oep] observable: exit status: ')]
            check(rc == 0 and status == ['[emu-oep] observable: exit status: 0x%08x (%d)' % (EXIT_CODE, EXIT_CODE)],
                  '%s %s: emu-oep exit 0, guest exit status %02XH' % (label, mode, EXIT_CODE))
            check(any(l == '[emu-oep] boundary: --%s' % mode[2:] or l.startswith('[emu-oep] boundary: --%s (' % mode[2:]) for l in out),
                  '%s %s: the mode is recorded in the log' % (label, mode))
            check(any('syscall instructions executed by the guest: 1; handled by the emulated kernel layer: 1; forwarded to the host: 0' in l for l in out) and
                  out and out[-1].startswith('[emu-oep] result: guest exit status 0x%08x; the emu-oep process continues' % EXIT_CODE),
                  '%s %s: runtime no host pass-through - the SYSCALL was handled in the engine, emu-oep continued after the guest\'s '
                  'NtTerminateProcess( NtCurrentProcess() ) and exited 0, not %02XH' % (label, mode, EXIT_CODE))
            if base_args:
                check(any('relocated (1 fixups)' in l for l in out), '%s %s: the DIR64 relocation applied' % (label, mode))
        o1, o2 = observable(res['--Sys'][1]), observable(res['--HLE'][1])
        diff = [i for i in range(max(len(o1), len(o2))) if i >= len(o1) or i >= len(o2) or o1[i] != o2[i]]
        check(len(o1) > 100 and not diff, '%s: --Sys and --HLE observable results identical (%d lines; differing %d)' % (label, len(o1), len(diff)))
    # c: --host
    host_cap = capture_dir(root, 'bare_host')
    rc, out = run([oep, '--host', patched])
    if host_cap:
        check(rc == 0, '--host with the bare-host capture %s: exit 0' % host_cap)
    else:
        check(rc == 3 and any('no system-DLL capture for the environment bare_host' in l for l in out),
              '--host: refused loudly (exit 3) - no bare-host DLL capture yet (decision 3.0.8: host package)')
    # d: loud failures with a state dump
    for path, what, kind in ((t1, 'unpatched T1 (EAX FFFFFFFFH)', 'unknown system service'), (v_unknown, 'number %s (not in the table)' % hex(unknown), 'unknown system service'),
                             (v_close, 'NtClose %s (known, not implemented in M4)' % hex(close or 0), 'unimplemented system service')):
        rc, out = run([oep, '--guest', path])
        check(rc == 4 and any(l.startswith('[emu-oep] STATE DUMP (stop: %s)' % kind) for l in out) and any('user mode at the SYSCALL: rip' in l for l in out) and
              any(l.startswith('[emu-oep]   rip = ') for l in out) and not any('observable: exit status' in l for l in out),
              '%s: loud stop "%s" with a state dump, exit 4, no exit status' % (what, kind))
    # f: the native reference (decision 3.0.7), only when the live ntdll IS the captured one
    live = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', 'ntdll.dll')
    live_sha = hashlib.sha256(open(live, 'rb').read()).hexdigest()
    if check(live_sha == cap_sha, 'the live ntdll.dll equals the guest capture (SHA-256): the number is valid for this kernel'):
        r = subprocess.run([patched], capture_output=True, timeout=60)
        check(r.returncode == EXIT_CODE, 'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest): exit code %s == the emulated %s'
              % (hex(r.returncode & 0xFFFFFFFF), hex(EXIT_CODE)))
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: T1 exits with %02XH in --Sys and --HLE (identical observable results, also relocated), loud stops with state dumps, '
        'no host pass-through, native reference equal' % EXIT_CODE)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
