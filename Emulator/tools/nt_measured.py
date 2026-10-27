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
  4. P44 (U2113, USER DECISION 2026-10-27 "measured-service drift"): fields Windows legitimately changes are compared by SHAPE / RANGE,
     every other byte exactly - VOLATILE below lists each such field with its justification and evidence (P44 STEP 3: tm_u32 prints
     CLIENTINFO +60H, a desktop-heap block - four measurements E3430H / CE640H / DB2D0H / 138B90H - as its shape); a field is loosened only with
     evidence (two measurements that differ, or documentation). The rules are checked on every run by a built-in RED / GREEN self-check:
     every listed mutation that breaks a shape (a wrong size class / type / value range, a missing field, a changed stable byte) must be
     rejected, every listed legitimate variation accepted. The native reference runs tm_nt from a COPY on the system volume (a fresh
     folder below %TEMP%): case sp's NtQueryAttributesFile miss writes nothing when the image's directory lies on the system volume and
     bytes 0..23H zero on the guest's other volume (E:) - the measured value belongs to the location, the location is fixed (as U2062's
     current directory) and the emulated image directory lies on the system volume.
The emulator side (the answers implemented from these lines) is checked by emu-selftest nt_measured_services / nt_measured_pool /
nt_measured_loader / nt_measured_csr (P40) / nt_measured_console (P41) and by suite win_env_sys_loader. Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I nt_measured.py REPO_ROOT TESTS_DIR
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
CASES = ['sat', 'qsi', 'hvp', 'qip', 'wcp', 'qsix', 'iocp', 'wkf', 'tmr2', 'awcp', 'swf', 'etw', 'kd', 'of', 'tls', 'ahc', 'vmi', 'csr', 'sec', 'csrc', 'nls',   # P40 (U2064): + csrc, nls
         'con', 'etwt', 'tok', 'acc', 'bno', 'mut', 'wil', 'wnf', 'wnfe', 'wnfs', 't2s', 'hku', 'alert', 'term',   # P41 (U2068): a process started without a console
         'time',   # P42 (U2102, plan 3.M6): the time sources of a process
         'sp', 'mit',   # P43 (U2106 / U2109, sample run 8): the DLL path search, ProcessMitigationPolicy
         'u32', 'u32k']   # P44 (U2115 / U2116, STEP 3): what an ordinary GUI program gets from user32's start (our GUI test PE tm_u32), its kernel side
# P41 (U2068): case term ends the process with exit code 2BH (its NtTerminateProcess( NULL, 2BH ) fixes the process' exit status)
EXIT_CODE = {'term': 0x2B}
# hex constants (9 .. 16 digits) that may appear: system constants and the pre-set fill pattern - never an address of the run
ALLOWED_HEX = {'FFFF800000000000', 'FFFFFFFFFF676980', 'FFFFFFFFDC3CBA00', 'FFFFFFFFFF676980H', 'CCCCCCCCCCCCCCCC',
               # P41 (U2068): WNF state names of the system DLLs, a worker-factory idle timeout - constants
               '0280032EA3BC0875', '418A073AA3BC7C75', '418A073AA3BC88F5', '418A073AA3BC1C75', 'FFFFFFFFEE1E5D00'}


# P44 (U2113): VOLATILE fields - compared by shape, never by value. Each entry: the items it applies to, the field, why Windows changes
# it, the evidence. Fields tm_nt already prints as a shape (handles / addresses as <handle> / <va>, stamps and ids as zero / non-zero,
# time as relations, the WIL feature-usage WNF state wnf4 as size range + header with its count as zero / non-zero - U2104, measured
# 44CH / 450H on 2026-10-26 and 1EAH after the guest's restart) are compared exactly as printed: their shape is in the line itself.
VOLATILE = [
    {'items': r'etw\.reg\d*(\.fields)?',
     'field': 'the provider ENABLE STATE written into the registration reply [+70H, +A0H) (phnt ntwmi.h:6439-6457 rev names '
              'EnableCallbackContext .. HasPayload): IsEnabled / Level / keywords / the provider traits flags of the enabling session',
     'why': 'set by the trace sessions that are running: a session that enables the provider makes the reply carry its enable state, '
            'a provider no session enables gets zeros; sessions start and stop with the use of the system',
     'evidence': 'documentation: Windows SDK 10.0.26100.0 shared/evntprov.h:446-460 (IsEnabled 0 = "No sessions are listening", 1 = "At least '
                 'one session is listening", 2 = capture state; "the ETW runtime will adjust the Level, MatchAnyKeyword, and MatchAllKeyword '
                 'parameters to the least-restrictive values set by any subscribed session"); measured contrast in this guest: provider 5 '
                 '(0A27EFF1, etw.reg5) enabled - changed bytes +70 +74 +78 +87 +90 +98, IsEnabled 1, Level FFH, keywords A0..H - while '
                 'providers 1-4 / 6 / 7 are not (zeros)',
     'shape': 'outside [+70H, +A0H) exact (status, ReturnLength, changed bytes +18 +2C, RegType, LoggerId, Reserved0, ReplyCallback, '
              'ReplyBufferSize, Reserved1, Reserved2, the handle shape); inside: the seven fields present in order, each a hex value '
              '(< 10000000H) or its 8 bytes, IsEnabled (the low ULONG at +70H) 0 / 1 / 2, IsEnabled 0 = every enable field zero, '
              'changed bytes inside the region only'},
]
_V_REG = re.compile(r'^(tm_nt etw\.reg\d*: NtTraceControl\(0FH, .*?\) -> status [0-9A-F]{8}H, ReturnLength [0-9A-F]+H; changed bytes:)((?: \+[0-9A-F]{2})*)(; ReplyHandle \(\+18H\) .*)$')
_V_FIELDS = re.compile(r'^(tm_nt etw\.reg\d*\.fields: RegType \d+, LoggerId \d+, Reserved0 \d+, ReplyCallback (?:unchanged|changed), ReplyBufferSize [0-9A-F]+H, '
                       r'Reserved1 [0-9A-F]+H, )EnableCallbackContext (?P<c>[^,]+), EnableFilterDesc (?P<f>[^,]+), EnableCallback (?P<e>[^,]+), '
                       r'EnableCallbackReserved (?P<r>[^,]+), ProviderTraits (?P<t>[^,]+), TraitsFlags (?P<tf>[0-9A-F]{1,8})H, HasPayload (?P<hp>[0-9A-F]{1,8})H$')
_V_Q = re.compile(r'^(?:([0-9A-F]{1,7})H|bytes((?: [0-9A-F]{2}){8}))$')


def _q(s):
    """a 64-bit enable field as tm_nt prints it (hex below 10000000H, else its 8 bytes little-endian) -> its value, or None"""
    m = _V_Q.match(s)
    if not m:
        return None
    if m.group(1) is not None:
        return int(m.group(1), 16)
    v = int.from_bytes(bytes(int(x, 16) for x in m.group(2).split()), 'little')
    return v if v >= 0x10000000 else None


def volatile_shape(line):
    """the shape of a VOLATILE line (a string two lines must share), None when the line breaks the shape, '' when no rule applies"""
    key = line.split(':', 1)[0]
    if not re.fullmatch(r'tm_nt ' + VOLATILE[0]['items'], key):
        return ''
    m = _V_REG.match(line)
    if m and key.count('.') == 1:
        offs = [int(x, 16) for x in m.group(2).split(' +')[1:]] if m.group(2) else []
        outside = [o for o in offs if not 0x70 <= o < 0xA0]
        return '%s %s <the enable state [+70H, +A0H)>%s' % (m.group(1), ' '.join('+%02X' % o for o in outside), m.group(3))
    m = _V_FIELDS.match(line)
    if m:
        v = [_q(m.group(g)) for g in ('c', 'f', 'e', 'r', 't')]
        if None in v:
            return None
        en = v[0] & 0xFFFFFFFF
        if en not in (0, 1, 2):
            return None
        if en == 0 and (any(v) or int(m.group('tf'), 16) or int(m.group('hp'), 16)):
            return None
        return m.group(1) + '<the enable state of the running sessions>'
    return None


def same_line(got, want):
    """exact, or both lines of a VOLATILE item with the same valid shape"""
    if got == want:
        return True
    if got.split(':', 1)[0] != want.split(':', 1)[0]:
        return False
    a, b = volatile_shape(got), volatile_shape(want)
    return bool(a) and a == b


def red_green_selfcheck(by_case):
    """P44 (U2113): the built-in RED / GREEN check of the VOLATILE rules on the recorded lines. RED: every mutation that breaks the
    shape must be rejected; GREEN: every legitimate variation (another session state) accepted. Returns (problems, red n, green n)"""
    rec = {l.split(':', 1)[0]: l for c in by_case for l in by_case[c]}
    r5, f5 = rec['tm_nt etw.reg5'], rec['tm_nt etw.reg5.fields']
    r4, f4 = rec['tm_nt etw.reg4'], rec['tm_nt etw.reg4.fields']
    w4 = rec['tm_nt wnf4.1000']
    red = [
        ('changed byte in Reserved2 (+40)', r5, r5.replace('changed bytes: +18 +2C', 'changed bytes: +18 +2C +40')),
        ('a stable changed byte missing (+2C)', r5, r5.replace('changed bytes: +18 +2C', 'changed bytes: +18')),
        ('status changed', r5, r5.replace('status 00000000H', 'status C0000001H')),
        ('ReturnLength changed', r5, r5.replace('ReturnLength A0H', 'ReturnLength 98H')),
        ('the handle shape changed', r5, r5.replace('(non-zero, multiple of 4)', '(zero)')),
        ('LoggerId changed', f5, f5.replace('LoggerId 5,', 'LoggerId 6,')),
        ('ReplyBufferSize changed', f5, f5.replace('ReplyBufferSize 0H', 'ReplyBufferSize 8H')),
        ('a field missing (HasPayload)', f5, f5.replace(', HasPayload 0H', '')),
        ('IsEnabled outside the documented codes (5)', f5, f5.replace('EnableCallbackContext bytes 01 00', 'EnableCallbackContext bytes 05 00')),
        ('wrong type (a field not hex)', f5, f5.replace('EnableFilterDesc 41H', 'EnableFilterDesc enabled')),
        ('wrong size (7 bytes)', f5, f5.replace('EnableCallback bytes 00 00 00 00 00 00 00 A0', 'EnableCallback bytes 00 00 00 00 00 00 A0')),
        ('not enabled but an enable field set', f4, f4.replace('EnableCallback 0H', 'EnableCallback 8H')),
        ('Reserved2 non-zero (a stable line of the item)', rec['tm_nt etw.reg5.reserved2'], rec['tm_nt etw.reg5.reserved2'].replace('[+30H, +70H): 0', '[+30H, +70H): 4')),
        ('WNF size class broken (wnf4: "1..1000H no")', w4, w4.replace('BufferLength 1..1000H yes', 'BufferLength 1..1000H no')),
        ('WNF header changed (wnf4)', w4, w4.replace('header 00 00 00 00 04 00', 'header 00 00 00 00 05 00')),
        ('a stable line (qsi.c0 bytes)', rec['tm_nt qsi.c0'], rec['tm_nt qsi.c0'].replace('bytes 07 00', 'bytes 08 00')),
        ('case sp: the system-volume miss writing bytes', rec['tm_nt sp.0.app'], rec['tm_nt sp.0.app'].replace('bytes written 0', 'bytes written 36 (offsets 0H..23H, non-zero 0)')),
    ]
    green = [
        ('provider 5 not enabled (its session stopped)', r5, r4.replace('etw.reg4:', 'etw.reg5:').replace('provider 4, LoggerId 4', 'provider 5, LoggerId 5')),
        ('provider 5 fields not enabled', f5, f4.replace('etw.reg4.fields', 'etw.reg5.fields').replace('LoggerId 4,', 'LoggerId 5,')),
        ('provider 4 enabled by a session (fields)', f4,
         f4.replace('EnableCallbackContext 0H', 'EnableCallbackContext 1H').replace('EnableCallback 0H', 'EnableCallback bytes 00 00 00 00 00 00 00 80')),
        ('provider 4 enabled by a session (changed bytes)', r4, r4.replace('changed bytes: +18 +2C', 'changed bytes: +18 +2C +70 +87')),
        ('provider 5 at another level', f5, f5.replace('bytes 01 00 00 00 FF 00 00 00', 'bytes 01 00 00 00 04 00 00 00')),
    ]
    bad = []
    for what, want, got in red:
        if got == want:
            bad.append('RED mutation not applied: ' + what)
        elif same_line(got, want):
            bad.append('RED mutation accepted: ' + what)
    for what, want, got in green:
        if got == want:
            bad.append('GREEN variation not applied: ' + what)
        elif not same_line(got, want):
            bad.append('GREEN variation rejected: ' + what)
    return bad, len(red), len(green)


def system_volume_copy(tm, sysroot):
    """P44 (U2113): a copy of tm_nt.exe in a fresh folder below %TEMP% on the system volume -> (folder, image) or (None, why)"""
    drive = os.path.splitdrive(sysroot)[0].upper()
    tmp = tempfile.mkdtemp(prefix='nt_measured_')
    if os.path.splitdrive(tmp)[0].upper() != drive:
        os.rmdir(tmp)
        return None, '%TEMP% is not on the system volume ' + drive
    dst = os.path.join(tmp, 'tm_nt.exe')
    shutil.copyfile(tm, dst)
    # P44 (U2115): case u32 starts our own GUI test PE tm_u32.exe beside tm_nt.exe
    u32 = os.path.join(os.path.dirname(tm), 'tm_u32.exe')
    if os.path.isfile(u32):
        shutil.copyfile(u32, os.path.join(tmp, 'tm_u32.exe'))
    return tmp, dst


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
    # P44 (U2113): the VOLATILE rules - every recorded line of a volatile item has a valid shape, and the RED / GREEN self-check
    vol = [l for l in body if volatile_shape(l) != '']
    check(all(volatile_shape(l) for l in vol), 'VOLATILE items: %d recorded lines (%s), each of a valid shape' % (len(vol), ' '.join(l.split(':', 1)[0][6:] for l in vol)))
    rg, nred, ngreen = red_green_selfcheck(by_case)
    check(not rg, 'VOLATILE rules RED / GREEN self-check: %d shape-breaking mutations rejected, %d legitimate variations accepted%s' % (nred - sum('RED' in x for x in rg), ngreen - sum('GREEN' in x for x in rg), ('; ' + '; '.join(rg)) if rg else ''))
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
        # P44 (U2113): tm_nt runs from a copy on the system volume (case sp: the image's directory's volume decides what a miss writes)
        folder, image = system_volume_copy(tm, sysroot)
        if folder is None:
            check(False, 'a copy of tm_nt.exe on the system volume (%s)' % image)
            image = None
        loose = []
        for c in CASES if image else []:
            r = subprocess.run([image, c], capture_output=True, timeout=300, cwd=sysroot)
            got = [l for l in r.stdout.decode('ascii', 'replace').replace('\r\n', '\n').split('\n') if l.startswith('tm_nt ')]
            ok = r.returncode == EXIT_CODE.get(c, 0) and len(got) == len(by_case[c]) and all(same_line(g, w) for g, w in zip(got, by_case[c]))
            loose += [g.split(':', 1)[0][6:] for g, w in zip(got, by_case[c]) if g != w and same_line(g, w)]
            if not ok:
                diffs.append('%s (exit %d, %d vs %d lines%s)' % (c, r.returncode, len(got), len(by_case[c]),
                                                                ', first difference ' + next((g.split(':', 1)[0] for g, w in zip(got, by_case[c]) if not same_line(g, w)), '-') if r.returncode == EXIT_CODE.get(c, 0) else ''))
        if image:
            # the image can stay held for a moment after its last process ended (a child of case con, a scanner): retry for up to 15 s
            for k in range(30):
                try:
                    for name in ('tm_nt.exe', 'tm_u32.exe'):   # our two copies (single files), then the folder
                        if os.path.exists(os.path.join(folder, name)):
                            os.remove(os.path.join(folder, name))
                    os.rmdir(folder)
                    break
                except OSError as e:
                    if k == 29:
                        say('note: the copy could not be removed (%s)' % e.__class__.__name__)
                    time.sleep(0.5)
        say('volatile items equal by shape only (the value differs from the recording): %s' % (', '.join(loose) if loose else 'none'))
        check(not diffs, 'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest): tm_nt, one process per case, equals the recorded lines '
              '(%d cases%s)' % (len(CASES), ('; differing: ' + ', '.join(diffs)) if diffs else ''))
    else:
        say('native reference: not applicable here (the live ntdll.dll is not the VMware-guest capture) - the recorded lines are those of the guest')
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: %d measured lines of %d cases, identifier screen clean, %d VOLATILE rule(s) (RED / GREEN self-check ok)%s' % (len(body), len(CASES), len(VOLATILE), ', native reference equal (from the system volume)' if cap_sha and live_sha == cap_sha else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
