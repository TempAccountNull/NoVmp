#!/usr/bin/env python3
"""capture_sysdlls.py - the system-DLL capture of ONE emulated Windows environment (plan 3.M4, ledger U1981; decision 3.0.8:
"--Sys DLLs per emulated environment (guest now; a bare-host DLL capture via a host package later), local only, never
committed; syscall numbers from that environment's ntdll").

Run it IN the environment it captures (the VMware guest; the bare host through a host package). It copies
%SystemRoot%\\System32\\ntdll.dll (as bytes) into <capture root>\\sysdlls\\<env>\\ and writes manifest.txt there (format 1): the
OS version facts the emulated KUSER_SHARED_DATA / PEB need (from the registry of that environment: CurrentMajorVersionNumber,
CurrentMinorVersionNumber, CurrentBuildNumber, SystemRoot, ProductOptions\\ProductType) and the size / SHA-256 of the copy. The
capture root is OUTSIDE the repository: $NOVMP_CAPTURE_ROOT, else the first ancestor folder of the repository that holds a
folder "capture" (as bare_host_cpuid.py). Nothing here goes into Git; no value of the manifest is an identifier of the machine
(versions, sizes, hashes of a system file, a path, the environment evidence line).

The emulator never reads the running machine's System32: it reads this capture only (emu::win::locate_capture /
load_manifest / parse_syscall_table). U2024 (plan 3.M5 part 2): further System32 files of the same environment with --dll NAME
(repeatable; e.g. kernel32.dll, KernelBase.dll, apisetschema.dll) and --nls (the ANSI / OEM code-page files the registry
names and l_intl.nls, the NLS tables the kernel maps into a process); each is copied as bytes and listed in the manifest as
"file_<lower-case name> <size> <sha256>" (+ "nls_acp / nls_oemcp <code page> <file>", "nls_case <file>"). An existing capture
with the same ntdll.dll is extended (the files already captured are kept and re-checked); --check compares every file.

The environment (U1990, user decision 2026-10-25): decided ONLY by the project's evidence-based environment detection of
U1893 - emu-alltest's "tag environment:" line (at_cases.hpp tag_environment(): the native CPUID equal to a captured profile
on the 14 identifying registers of tag_sig() -> guest / bare host P / E / either, else unknown). This script does not
re-implement that register list: it runs emu-alltest with an empty case file (--cases NUL: no case line, no native snippet)
and reads the one line. A label is written only on positive evidence ("the native CPUID equals the captured ... profile");
"unknown", an explicit --tag-env override ("(explicit)": not evidence), a missing / doubled / malformed line or an
emu-alltest failure = refused, nothing written. Not the processor count (the version of U1981 used it; a VmLoader-rewritten
firmware view or another machine with 8 / 20 logical processors would have been labelled wrongly).

usage:
  python -I capture_sysdlls.py [--env vmware_guest|bare_host] [--root DIR] [--alltest EXE] [--dll NAME ...] [--nls] [--replace]
          --env (optional) must equal the detected environment; the label written is always the detected one
  python -I capture_sysdlls.py [--env ...] --check        the capture of the detected environment equals the live files (exit 0 / 1)
  python -I capture_sysdlls.py --detect                   print the detected environment and the evidence (exit 0 / 1 refused)
  python -I capture_sysdlls.py --selftest [--alltest EXE] RED / GREEN self-test (suite capture_sysdlls_selftest, U1991): forced /
          mocked wrong or unknown environments must refuse without writing; the live detection must be positive
--alltest default: <repo>\\build\\x64\\Release\\tests\\emu-alltest.exe (it reads the captured CPUID profiles next to itself or
from <repo>\\Emulator\\data, as in the hardware gates).
"""
import datetime
import hashlib
import os
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True   # python -I ignores PYTHONDONTWRITEBYTECODE

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENV = 'NOVMP_CAPTURE_ROOT'
ENVS = ('vmware_guest', 'bare_host')
PRODUCT = {'winnt': 1, 'lanmannt': 2, 'servernt': 3}   # NT_PRODUCT_TYPE (Windows SDK shared\ntdef.h: NtProductWinNt = 1, ...)
ALLTEST = os.path.join(ROOT, 'build', 'x64', 'Release', 'tests', 'emu-alltest.exe')
# emu-alltest tag_env_text() labels (at_cases.hpp, U1875 / U1893) -> the capture environment
LABELS = {'guest': 'vmware_guest', 'hostP': 'bare_host', 'hostE': 'bare_host', 'host (hostP | hostE)': 'bare_host'}
# the positive-evidence wording of tag_environment() for each platform (at_cases.hpp e.why)
EVIDENCE = {'vmware_guest': 'the native CPUID equals the captured VMware-guest profile ',
            'bare_host': 'the native CPUID equals the captured bare-host profile '}
TAG = 'tag environment: '


class Refused(Exception):
    pass


def fail(msg):
    sys.exit('[capture_sysdlls] FAIL: ' + msg)


def refuse(msg):
    raise Refused(msg)


def say(msg):
    print('[capture_sysdlls] ' + msg)


def reg(path, name):
    import winreg
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


def run_alltest(exe, extra=()):
    """emu-alltest --cases NUL [extra]: no case line runs; only the run header (incl. "tag environment:") is printed.
    Returns (exit status, stdout text)."""
    if not os.path.isfile(exe):
        refuse('emu-alltest not found at %s (build the Release configuration first, or pass --alltest EXE)' % exe)
    r = subprocess.run([exe, '--cases', os.devnull] + list(extra), capture_output=True, timeout=300)
    return r.returncode, r.stdout.decode('utf-8', errors='replace')


def decide(status, text):
    """The capture environment from one emu-alltest run, positive evidence only: (env, label, evidence) or Refused."""
    if status != 0:
        refuse('emu-alltest --cases NUL exited %d: no environment evidence' % status)
    lines = [l.strip() for l in text.splitlines() if l.startswith(TAG)]
    if len(lines) != 1:
        refuse('emu-alltest printed %d "%s" lines (want exactly 1): no environment evidence' % (len(lines), TAG.strip()))
    rest = lines[0][len(TAG):]
    if ' - ' not in rest:
        refuse('malformed "%s" line' % TAG.strip())
    label, why = rest.split(' - ', 1)
    if label == 'unknown':
        refuse('environment unknown - emu-alltest found no positive evidence (the native CPUID equals none of the captured '
               'profiles on the identifying registers, U1893): refusing to label a capture')
    if why.startswith('--tag-env') or '(explicit)' in why:
        refuse('the environment "%s" was forced (--tag-env, explicit): an override is not evidence' % label)
    env = LABELS.get(label)
    if env is None:
        refuse('unrecognised environment label "%s"' % label)
    if not why.startswith(EVIDENCE[env]):
        refuse('environment "%s" without the positive-evidence statement for %s' % (label, env))
    return env, label, why


def detect(exe, extra=()):
    st, text = run_alltest(exe, extra)
    return decide(st, text)


def environment(asked, detector):
    """Detect, then compare with --env (if given). Returns (env, evidence line) or Refused."""
    env, label, why = detector()
    if asked and asked != env:
        refuse('--env %s, but the detected environment is %s (emu-alltest "tag environment: %s"): run the capture in the '
               'environment it names' % (asked, env, label))
    return env, '%s - %s' % (label, why.split(';', 1)[0])


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


def arg(argv, name, default=''):
    if name not in argv:
        return default
    i = argv.index(name)
    if i + 1 >= len(argv):
        fail('%s needs a value' % name)
    return argv[i + 1]


def nls_files():
    """U2024 (plan 3.M5 part 2): the three NLS tables the kernel maps into every process (PEB AnsiCodePageData /
    OemCodePageData / UnicodeCaseTableData): the ANSI and OEM code-page files the registry names (Control\\Nls\\CodePage: the
    values ACP / OEMCP give the code page, the value of that number the file name) and the case table l_intl.nls - the
    file set of the boot loader as ReactOS 0.4.16 boot\\freeldr\\freeldr\\ntldr\\wlregistry.c:283-393 reads it (licensed OSS,
    reference only). Returns [(role, code page or '', file name)]."""
    key = r'SYSTEM\CurrentControlSet\Control\Nls\CodePage'
    out = []
    for role, val in (('acp', 'ACP'), ('oemcp', 'OEMCP')):
        cp = str(reg(key, val)).strip()
        if not cp.isdigit() or len(cp) > 5:
            fail('Nls\\CodePage %s is not a code-page number' % val)
        name = str(reg(key, cp)).strip()
        if not name or '\\' in name or '/' in name or not name.isascii() or len(name) > 64:
            fail('Nls\\CodePage %s names no plain file' % cp)
        out.append((role, cp, name))
    out.append(('case', '', 'l_intl.nls'))
    return out


def plain_name(n):
    return bool(n) and n.isascii() and len(n) <= 64 and not any(c in n for c in '\\/:*?"<>|') and n not in ('.', '..')


def capture(argv, detector):
    asked = arg(argv, '--env')
    if asked and asked not in ENVS:
        fail('--env must be one of %s' % ' / '.join(ENVS))
    extra = [argv[i + 1] for i, a in enumerate(argv) if a == '--dll' and i + 1 < len(argv)]
    for n in extra:   # U2024 (plan 3.M5 part 2): DLLs beyond ntdll.dll, by plain file name (System32)
        if not plain_name(n) or n.lower() == 'ntdll.dll':
            fail('--dll %s: a plain System32 file name other than ntdll.dll' % n)
    env, evidence = environment(asked, detector)   # before anything is read or written
    say('environment %s (emu-alltest: tag environment: %s)' % (env, evidence))
    dst = os.path.join(capture_root(arg(argv, '--root')), 'sysdlls', env)
    major, minor, build, ptype, root = facts()
    data = open(live_path('ntdll.dll'), 'rb').read()
    man_path = os.path.join(dst, 'manifest.txt')
    old_kv = {}
    if os.path.isfile(man_path):
        for line in open(man_path, encoding='ascii'):
            line = line.rstrip('\r\n')
            if line and not line.startswith('#') and ' ' in line:
                k, v = line.split(' ', 1)
                old_kv[k] = v
    if '--check' in argv:
        if not os.path.isfile(man_path):
            fail('no capture at %s' % dst)
        have = open(os.path.join(dst, 'ntdll.dll'), 'rb').read()
        same = sha(have) == sha(data)
        say('capture %s: ntdll.dll %s the live file (%d / %d bytes)' % (dst, 'EQUALS' if same else 'DIFFERS FROM', len(have), len(data)))
        for k, v in sorted(old_kv.items()):   # U2024: every further captured file
            if k.startswith('file_'):
                name = k[5:]
                live = open(live_path(name), 'rb').read()
                cap = open(os.path.join(dst, name), 'rb').read()
                ok = sha(live) == sha(cap) == v.split(' ')[1]
                say('capture %s: %s %s the live file' % (dst, name, 'EQUALS' if ok else 'DIFFERS FROM'))
                same = same and ok
        return 0 if same else 1
    files = {}   # U2024: lower-case name -> bytes (the files beyond ntdll.dll: --dll, --nls, and those already captured)
    nls = []
    if '--nls' in argv:
        nls = nls_files()
    wanted = [n.lower() for n in extra] + [n.lower() for _, _, n in nls]
    if os.path.isfile(man_path) and '--replace' not in argv:
        old = open(os.path.join(dst, 'ntdll.dll'), 'rb').read() if os.path.isfile(os.path.join(dst, 'ntdll.dll')) else b''
        if sha(old) != sha(data):
            fail('a different capture exists at %s (pass --replace after reviewing why the environment changed)' % dst)
        for k, v in old_kv.items():
            if k.startswith('file_'):
                files[k[5:]] = None   # kept (checked below if asked again)
        if all(n in files for n in wanted):
            for n in wanted:
                if sha(open(live_path(n), 'rb').read()) != old_kv['file_' + n].split(' ')[1]:
                    fail('%s differs from the captured copy (pass --replace after reviewing why)' % n)
            say('capture %s already holds this ntdll.dll (%d bytes)%s: nothing to do' % (dst, len(data), ' and the files asked for' if wanted else ''))
            return 0
    os.makedirs(dst, exist_ok=True)
    with open(os.path.join(dst, 'ntdll.dll'), 'wb') as f:
        f.write(data)
    if sha(open(os.path.join(dst, 'ntdll.dll'), 'rb').read()) != sha(data):
        fail('the copy does not hash like the source')
    for n in wanted:
        b = open(live_path(n), 'rb').read()
        if not b or len(b) > 64 << 20:
            fail('%s: size %d outside 1..64 MiB' % (n, len(b)))
        with open(os.path.join(dst, n), 'wb') as f:
            f.write(b)
        if sha(open(os.path.join(dst, n), 'rb').read()) != sha(b):
            fail('the copy of %s does not hash like the source' % n)
        files[n] = b
    lines = [
        '# system-DLL capture of one emulated environment (Emulator/tools/capture_sysdlls.py, plan 3.M4, decision 3.0.8):',
        '# LOCAL ONLY, never committed; read by the Emulator lib (emu::win::load_manifest) and the harness (win_env_t1.py)',
        '# environment evidence (U1990; emu-alltest "tag environment:", U1893): ' + evidence,
        'format 1',
        'environment ' + env,
        'captured ' + (old_kv.get('captured') if old_kv.get('captured') and '--replace' not in argv else datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S')),
        'os_major %d' % major,
        'os_minor %d' % minor,
        'os_build %d' % build,
        'product_type %d' % ptype,
        'system_root ' + root,
        'ntdll_size %d' % len(data),
        'ntdll_sha256 ' + sha(data),
    ]
    # U2024 (plan 3.M5 part 2): the further files ("file_<name> <size> <sha256>", lower-case names) and the NLS roles
    if files:
        lines.insert(0, '# U2024: file_<name> <size> <sha256> = a further System32 file of the same environment (--dll / --nls); nls_* = '
                        'the NLS tables the kernel maps (code page, file)')
        lines.append('files_captured ' + datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S'))
    for n in sorted(files):
        b = files[n] if files[n] is not None else open(os.path.join(dst, n), 'rb').read()
        if files[n] is None and 'file_' + n in old_kv and old_kv['file_' + n].split(' ')[1] != sha(b):
            fail('the captured %s no longer hashes like its manifest line' % n)
        lines.append('file_%s %d %s' % (n, len(b), sha(b)))
    for role, cp, n in nls:
        lines.append('nls_%s %s%s' % (role, (cp + ' ') if cp else '', n.lower()))
    for k in ('nls_acp', 'nls_oemcp', 'nls_case'):
        if not nls and k in old_kv:
            lines.append('%s %s' % (k, old_kv[k]))
    with open(man_path, 'w', encoding='ascii', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    say('captured %s: ntdll.dll %d bytes, manifest %s (OS version %d.%d build %d, product type %d)' % (dst, len(data), man_path, major, minor, build, ptype))
    for n in sorted(files):
        say('  + %s' % n)
    return 0


# ------------------------------------------------------------------------------------------------ self-test (U1991)
GUEST_OK = (TAG + 'guest - the native CPUID equals the captured VMware-guest profile cpuid_i5-13600k.txt on the identifying CPUID '
            'registers (14: leaf 0, 01H, 07H.0, 07H.1, 80000001H, 80000008H.EAX); CPUID.1:ECX[31] = 0, CPUID.07H.0:EDX[15] Hybrid = 0')
HOSTP_OK = (TAG + 'hostP - the native CPUID equals the captured bare-host profile cpuid_i5-13600k_bare_host.txt on the identifying '
            'CPUID registers (14: ...); CPUID.1:ECX[31] = 0, CPUID.07H.0:EDX[15] Hybrid = 1, native runs pinned to P-cores (--hw-cpu, CPUID.1AH)')
HOST_EITHER_OK = (TAG + 'host (hostP | hostE) - the native CPUID equals the captured bare-host profile cpuid_i5-13600k_bare_host_ecore.txt '
                  'on the identifying CPUID registers (14: ...); core type: either')
UNKNOWN = (TAG + 'unknown - no positive evidence of either environment - VMware-guest profile: differs in 01H.EAX; bare-host P-core '
           'profile: differs in 07H.0.EDX; bare-host E-core profile: differs in 07H.0.EDX: an environment-specific tag statement is '
           'a harness error (U1893; --tag-env overrides)')


def selftest(argv):
    exe = arg(argv, '--alltest', ALLTEST)
    checks = [0, 0]

    def check(name, ok, detail=''):
        checks[0] += 1
        if not ok:
            checks[1] += 1
        print('  %-4s %s%s' % ('ok' if ok else 'FAIL', name, (' - ' + detail) if detail else ''))

    def decided(st, text):
        try:
            return decide(st, text)[0]
        except Refused as e:
            return 'REFUSED: ' + str(e)

    print('[capture_sysdlls] selftest 1: the decision on emu-alltest output (synthetic lines)')
    for name, st, text, want in (
            ('guest, positive evidence', 0, 'x\n' + GUEST_OK + '\ny\n', 'vmware_guest'),
            ('hostP, positive evidence', 0, HOSTP_OK, 'bare_host'),
            ('host either core, positive evidence', 0, HOST_EITHER_OK, 'bare_host'),
            ('unknown', 0, UNKNOWN, None),
            ('forced guest (--tag-env, explicit)', 0, TAG + 'guest - --tag-env guest (explicit)', None),
            ('forced hostE (--tag-env, explicit)', 0, TAG + 'hostE - --tag-env hostE (explicit)', None),
            ('guest label with bare-host evidence (inconsistent)', 0, TAG + 'guest - ' + EVIDENCE['bare_host'] + 'x.txt', None),
            ('hostP label with guest evidence (inconsistent)', 0, TAG + 'hostP - ' + EVIDENCE['vmware_guest'] + 'x.txt', None),
            ('unrecognised label', 0, TAG + 'vm - ' + EVIDENCE['vmware_guest'] + 'x.txt', None),
            ('no line', 0, 'cases: 0, differing: 0\n', None),
            ('two lines', 0, GUEST_OK + '\n' + GUEST_OK + '\n', None),
            ('malformed line', 0, TAG + 'guest', None),
            ('emu-alltest failed', 2, GUEST_OK, None)):
        got = decided(st, text)
        check(name, got == want if want else got.startswith('REFUSED'), got)

    print('[capture_sysdlls] selftest 2: a capture with a mocked wrong / unknown / forced environment refuses and writes nothing')
    nowhere = os.path.join(tempfile.gettempdir(), 'capture_sysdlls_selftest_%d_must_not_exist' % os.getpid())

    def mocked(text):
        return lambda: decide(0, text)

    for name, asked, text in (
            ('mocked bare host (positive), --env vmware_guest', 'vmware_guest', HOSTP_OK),
            ('mocked VMware guest (positive), --env bare_host', 'bare_host', GUEST_OK),
            ('mocked unknown, --env vmware_guest', 'vmware_guest', UNKNOWN),
            ('mocked unknown, no --env', '', UNKNOWN),
            ('mocked forced guest (explicit), --env vmware_guest', 'vmware_guest', TAG + 'guest - --tag-env guest (explicit)')):
        a = ['--root', nowhere] + (['--env', asked] if asked else [])
        try:
            capture(a, mocked(text))
            check(name, False, 'NOT refused')
        except Refused as e:
            check(name, not os.path.exists(nowhere), 'refused: ' + str(e))
        except SystemExit as e:
            check(name, False, 'other stop: %s' % e)

    print('[capture_sysdlls] selftest 3: the live detection through emu-alltest (%s)' % os.path.relpath(exe, ROOT))
    if not os.path.isfile(exe):
        check('emu-alltest present', False, exe)
    else:
        for name, extra, asked in (
                ('emu-alltest --tag-env hostP (forced wrong environment), --env vmware_guest', ['--tag-env', 'hostP'], 'vmware_guest'),
                ('emu-alltest --tag-env guest (forced, not evidence), --env vmware_guest', ['--tag-env', 'guest'], 'vmware_guest'),
                ('emu-alltest --tag-env host (forced), no --env', ['--tag-env', 'host'], ''),
                ('emu-alltest --tag-profiles none (unknown), --env vmware_guest', ['--tag-profiles', 'none'], 'vmware_guest'),
                ('emu-alltest --tag-profiles none (unknown), no --env', ['--tag-profiles', 'none'], '')):
            a = ['--root', nowhere] + (['--env', asked] if asked else [])
            try:
                capture(a, lambda extra=extra: detect(exe, extra))
                check(name, False, 'NOT refused')
            except Refused as e:
                check(name, not os.path.exists(nowhere), 'refused: ' + str(e))
            except SystemExit as e:
                check(name, False, 'other stop: %s' % e)
        try:
            env, label, why = detect(exe)
            check('live detection: positive evidence', True, '%s (tag environment: %s)' % (env, label))
            other = 'bare_host' if env == 'vmware_guest' else 'vmware_guest'
            try:
                capture(['--root', nowhere, '--env', other], lambda: detect(exe))
                check('live detection, --env %s (the other environment)' % other, False, 'NOT refused')
            except Refused as e:
                check('live detection, --env %s (the other environment)' % other, not os.path.exists(nowhere), 'refused: ' + str(e))
            print('[capture_sysdlls] detected environment: %s' % env)
        except Refused as e:
            check('live detection: positive evidence', False, 'refused: ' + str(e))
    print('[capture_sysdlls] selftest: %d checks, %d failed -> %s' % (checks[0], checks[1], 'PASS' if checks[1] == 0 else 'FAIL'))
    return 0 if checks[1] == 0 else 1


def main(argv):
    if '--selftest' in argv:
        return selftest(argv)
    exe = arg(argv, '--alltest', ALLTEST)
    try:
        if '--detect' in argv:
            env, evidence = environment(arg(argv, '--env'), lambda: detect(exe))
            say('detected environment: %s (tag environment: %s)' % (env, evidence))
            return 0
        return capture(argv, lambda: detect(exe))
    except Refused as e:
        print('[capture_sysdlls] REFUSED: %s' % e)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
