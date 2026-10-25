#!/usr/bin/env python3
"""win_env_process.py - suite win_env_process (plan 3.M5 part 2, ledger U2029): the process state of emu::win (system DLL images
as data, PEB loader lists, process parameters, process heap, NtGlobalFlag) on the VMware-guest environment's capture, checked by
the test PE tl_ldr (Emulator/tests/pe/tl_ldr, self-written guest code, never sample-derived, no imports).

tl_ldr walks PEB->Ldr: InLoadOrderModuleList = the image, ntdll.dll, kernel32.dll, kernelbase.dll; InInitializationOrderModuleList
starts with ntdll.dll; ProcessParameters->ImagePathName ends in .exe; ProcessHeap Flags has HEAP_GROWABLE and ForceFlags 0;
NtGlobalFlag has no heap-checking bit - exit status 30H (101H..108H name the failed check). Checked:
  the capture holds the core files (U2024: kernel32.dll, kernelbase.dll, apisetschema.dll, the NLS files) with the manifest's sizes
      and SHA-256
  the NATIVE REFERENCE first (decision 3.0.7, only when the live ntdll equals the capture): the real Windows process state passes
      tl_ldr's checks (exit 30H) - the model's facts are those of the real environment
  emu-oep --guest --HLE and --Sys: guest exit status 30H; the environment lines name the three system images at their slots, the
      PEB_LDR_DATA lists, the placeholder process parameters (no real user / computer name: "user" / "WIN10-EMU"), the heap flags
  --HLE: a call into a system image's code stops loudly naming the export (selftest win_env_process_state; here: T2 still exits 2BH)
Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I win_env_process.py REPO_ROOT TESTS_DIR [--oep EMU_OEP_EXE]
"""
import hashlib
import importlib.util
import os
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


T1H = load('win_env_t1')
CORE = ('kernel32.dll', 'kernelbase.dll', 'apisetschema.dll')


def say(msg):
    print('[win_env_process] ' + msg)


def main(argv):
    oep_given = None
    if len(argv) == 4 and argv[2] == '--oep':
        oep_given = os.path.abspath(argv[3])
        argv = argv[:2]
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    oep = oep_given or os.path.join(tests, 'emu-oep.exe')
    pe_dir = os.path.join(tests, 'test_pe')
    tl = os.path.join(pe_dir, 'tl_ldr.exe')
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in (oep, tl):
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    cap = T1H.capture_dir(root, 'vmware_guest')
    if not cap:
        say('FAIL no VMware-guest system-DLL capture')
        return 1
    man = T1H.manifest(cap)
    files = {k[5:]: v.split(' ') for k, v in man.items() if k.startswith('file_')}
    good = True
    for n in CORE + tuple(man[k].split(' ')[-1] for k in ('nls_acp', 'nls_oemcp', 'nls_case') if k in man):
        p = os.path.join(cap, n)
        ok = n in files and os.path.isfile(p) and os.path.getsize(p) == int(files[n][0]) and hashlib.sha256(open(p, 'rb').read()).hexdigest() == files[n][1]
        good = good and ok
    check(good and all(k in man for k in ('nls_acp', 'nls_oemcp', 'nls_case')),
          'capture %s: the core files (%s) and the three NLS files match their manifest lines (U2024: capture_sysdlls.py --dll / --nls)' % (cap, ', '.join(CORE)))
    nt = open(os.path.join(cap, 'ntdll.dll'), 'rb').read()
    n = T1H.stub_numbers(nt).get('NtTerminateProcess')
    img = open(tl, 'rb').read()
    at = img.find(b'TLSYSNUM')
    if not check(n is not None and img.count(b'TLSYSNUM') == 1 and struct.unpack_from('<I', img, at + 8)[0] == 0xFFFFFFFF, 'tl_ldr: marker once, field unpatched; NtTerminateProcess stub found'):
        return 1
    b = bytearray(img)
    struct.pack_into('<I', b, at + 8, n)
    tlp = os.path.join(pe_dir, 'tl_ldr.vmware_guest.exe')
    open(tlp, 'wb').write(bytes(b))
    live = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', 'ntdll.dll')
    if check(hashlib.sha256(open(live, 'rb').read()).hexdigest() == hashlib.sha256(nt).hexdigest(), 'the live ntdll.dll equals the guest capture (SHA-256)'):
        r = subprocess.run([tlp], capture_output=True, timeout=60)
        check(r.returncode == 0x30, 'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest): tl_ldr exit code %s == 30H (the real process '
              'state passes the same checks)' % hex(r.returncode & 0xFFFFFFFF))

    def status(out):
        s = [l for l in out if l.startswith('[emu-oep] observable: exit status: ')]
        return s[0].split(': ', 2)[2] if len(s) == 1 else None

    for mode in ('--HLE', '--Sys'):
        rc, out = T1H.run([oep, '--guest', mode, tlp])
        check(rc == 0 and status(out) == '0x00000030 (48)', 'tl_ldr %s: emu-oep exit 0, guest status 30H (got rc %d, %s)' % (mode, rc, status(out)))
        env = [l for l in T1H.observable(out) if l.startswith('[emu-oep] observable: env: ')]
        imgs = [l for l in env if 'env: system image ' in l]
        check(len(imgs) == 3 and 'ntdll.dll at 0x7ffb00000000' in imgs[0] and 'kernel32.dll at 0x7ffb10000000' in imgs[1] and 'kernelbase.dll at 0x7ffb20000000' in imgs[2],
              '%s: the three system images as data at their slots' % mode)
        check(any('InLoadOrder / InMemoryOrder tl_ldr.vmware_guest.exe 0x140000000, ntdll.dll 0x7ffb00000000, kernel32.dll 0x7ffb10000000, kernelbase.dll 0x7ffb20000000; '
                  'InInitializationOrder ntdll.dll, kernelbase.dll, kernel32.dll' in l for l in env), '%s: the PEB_LDR_DATA lists' % mode)
        check(any('placeholders (user "user", computer "WIN10-EMU")' in l for l in env) and any('ForceFlags (74H) = 0' in l for l in env) and
              any('PEB NtGlobalFlag (BCH) = 0' in l for l in env), '%s: placeholder process parameters, heap ForceFlags 0, NtGlobalFlag 0' % mode)
    rc, out = T1H.run([oep, '--guest', '--HLE', os.path.join(pe_dir, 't2_import.exe')])
    check(rc == 0 and status(out) == '0x0000002b (43)', 'T2 --HLE still exits 2BH with the process state present')
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: the capture holds the core DLLs and NLS files; tl_ldr exits 30H natively and in --HLE / --Sys (loader lists, process parameters, heap '
        'flags, NtGlobalFlag as the real process has them)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
