#!/usr/bin/env python3
"""capture_sysdlls.py - the system-DLL capture of ONE emulated Windows environment (plan 3.M4, ledger U1981; decision 3.0.8:
"--Sys DLLs per emulated environment (guest now; a bare-host DLL capture via a host package later), local only, never
committed; syscall numbers from that environment's ntdll").

Run it IN the environment it captures (the VMware guest for --env vmware_guest; the bare host, through a host package, for
--env bare_host). It copies %SystemRoot%\\System32\\ntdll.dll (as bytes) into <capture root>\\sysdlls\\<env>\\ and writes
manifest.txt there (format 1): the OS version facts the emulated KUSER_SHARED_DATA / PEB need (from the registry of that
environment: CurrentMajorVersionNumber, CurrentMinorVersionNumber, CurrentBuildNumber, SystemRoot, ProductOptions\\ProductType)
and the size / SHA-256 of the copy. The capture root is OUTSIDE the repository: $NOVMP_CAPTURE_ROOT, else the first ancestor
folder of the repository that holds a folder "capture" (as bare_host_cpuid.py). Nothing here goes into Git; no value of the
manifest is an identifier of the machine (versions, sizes, hashes of a system file, a path).

The emulator never reads the running machine's System32: it reads this capture only (emu::win::locate_capture /
load_manifest / parse_syscall_table). DLLs beyond ntdll come with milestone M5 (--dll NAME, repeatable).

usage:
  python -I capture_sysdlls.py --env vmware_guest|bare_host [--root DIR] [--dll NAME ...] [--replace]
  python -I capture_sysdlls.py --env vmware_guest --check     the capture equals the live files (exit 0 / 1)
The environment is checked first: this machine's logical processor count must equal dwNumberOfProcessors of the named
environment's platform profile (Emulator\\data\\platform\\<env>\\views\\environment.txt: guest 8, bare host 20); the BIOS strings
cannot tell, VmLoader rewrites the guest's firmware view.
"""
import datetime
import hashlib
import os
import sys
import winreg

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENV = 'NOVMP_CAPTURE_ROOT'
ENVS = ('vmware_guest', 'bare_host')
PRODUCT = {'winnt': 1, 'lanmannt': 2, 'servernt': 3}   # NT_PRODUCT_TYPE (Windows SDK shared\ntdef.h: NtProductWinNt = 1, ...)


def fail(msg):
    sys.exit('[capture_sysdlls] FAIL: ' + msg)


def say(msg):
    print('[capture_sysdlls] ' + msg)


def reg(path, name):
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path) as k:
        return winreg.QueryValueEx(k, name)[0]


def capture_root(explicit):
    if explicit:
        return os.path.abspath(explicit)
    r = os.environ.get(ENV, '').strip()
    if r:
        return os.path.abspath(r)
    p = ROOT
    while True:
        c = os.path.join(p, 'capture')
        if os.path.isdir(c):
            return c
        q = os.path.dirname(p)
        if q == p:
            fail('no capture root: set %s or create a folder "capture" above %s' % (ENV, ROOT))
        p = q


def environment_ok(env):
    # The firmware view of the guest is rewritten by VmLoader (BIOS strings name no VMware there), so the environment is told
    # by a fact of its own platform profile: the logical processor count (views\environment.txt dwNumberOfProcessors: the
    # VMware guest has 8 vCPUs, the bare host 20 logical processors) must equal this machine's.
    prof = os.path.join(ROOT, 'Emulator', 'data', 'platform', env, 'views', 'environment.txt')
    want = None
    for line in open(prof, encoding='utf-8', errors='replace'):
        if 'dwNumberOfProcessors ' in line:
            want = int(line.split('dwNumberOfProcessors ', 1)[1].split(',', 1)[0])
            break
    if want is None:
        fail('%s has no dwNumberOfProcessors line' % prof)
    if os.cpu_count() != want:
        fail('--env %s, but this machine\'s logical processor count differs from that environment\'s platform profile (%s): '
             'run the capture in the environment it names' % (env, os.path.relpath(prof, ROOT)))


def facts():
    cv = r'SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    major = int(reg(cv, 'CurrentMajorVersionNumber'))
    minor = int(reg(cv, 'CurrentMinorVersionNumber'))
    build = int(str(reg(cv, 'CurrentBuildNumber')))
    root = str(reg(cv, 'SystemRoot'))
    pt = str(reg(r'SYSTEM\CurrentControlSet\Control\ProductOptions', 'ProductType')).lower()
    if pt not in PRODUCT:
        fail('ProductOptions\\ProductType is none of WinNT / LanmanNt / ServerNT')
    if not (0 < major <= 0xFFFF and 0 <= minor <= 0xFFFF and 0 < build <= 0xFFFF) or len(root) > 128 or not root.isascii():
        fail('unexpected version facts (major / minor / build / SystemRoot)')
    return major, minor, build, PRODUCT[pt], root


def live_path(dll):
    sr = os.environ.get('SystemRoot', r'C:\Windows')
    return os.path.join(sr, 'System32', dll)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def main(argv):
    if '--env' not in argv or argv.index('--env') + 1 >= len(argv) or argv[argv.index('--env') + 1] not in ENVS:
        print(__doc__.strip().split('usage:')[1].strip())
        return 2
    env = argv[argv.index('--env') + 1]
    explicit = argv[argv.index('--root') + 1] if '--root' in argv else ''
    dlls = ['ntdll.dll'] + [argv[i + 1] for i, a in enumerate(argv) if a == '--dll' and i + 1 < len(argv)]
    if dlls[1:]:
        fail('--dll: DLLs beyond ntdll.dll are milestone M5')
    environment_ok(env)
    dst = os.path.join(capture_root(explicit), 'sysdlls', env)
    major, minor, build, ptype, root = facts()
    data = open(live_path('ntdll.dll'), 'rb').read()
    man_path = os.path.join(dst, 'manifest.txt')
    if '--check' in argv:
        if not os.path.isfile(man_path):
            fail('no capture at %s' % dst)
        have = open(os.path.join(dst, 'ntdll.dll'), 'rb').read()
        same = sha(have) == sha(data)
        say('capture %s: ntdll.dll %s the live file (%d / %d bytes)' % (dst, 'EQUALS' if same else 'DIFFERS FROM', len(have), len(data)))
        return 0 if same else 1
    if os.path.isfile(man_path) and '--replace' not in argv:
        old = open(os.path.join(dst, 'ntdll.dll'), 'rb').read() if os.path.isfile(os.path.join(dst, 'ntdll.dll')) else b''
        if sha(old) == sha(data):
            say('capture %s already holds this ntdll.dll (%d bytes): nothing to do' % (dst, len(data)))
            return 0
        fail('a different capture exists at %s (pass --replace after reviewing why the environment changed)' % dst)
    os.makedirs(dst, exist_ok=True)
    with open(os.path.join(dst, 'ntdll.dll'), 'wb') as f:
        f.write(data)
    if sha(open(os.path.join(dst, 'ntdll.dll'), 'rb').read()) != sha(data):
        fail('the copy does not hash like the source')
    lines = [
        '# system-DLL capture of one emulated environment (Emulator/tools/capture_sysdlls.py, plan 3.M4, decision 3.0.8):',
        '# LOCAL ONLY, never committed; read by the Emulator lib (emu::win::load_manifest) and the harness (win_env_t1.py)',
        'format 1',
        'environment ' + env,
        'captured ' + datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S'),
        'os_major %d' % major,
        'os_minor %d' % minor,
        'os_build %d' % build,
        'product_type %d' % ptype,
        'system_root ' + root,
        'ntdll_size %d' % len(data),
        'ntdll_sha256 ' + sha(data),
    ]
    with open(man_path, 'w', encoding='ascii', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    say('captured %s: ntdll.dll %d bytes, manifest %s (OS version %d.%d build %d, product type %d)' % (dst, len(data), man_path, major, minor, build, ptype))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
