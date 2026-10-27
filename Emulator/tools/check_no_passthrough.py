#!/usr/bin/env python3
"""check_no_passthrough.py - suite no_host_passthrough (plan 3.M4, ledger U1988; AGENT_RULES "Host safety": "No emulator path
forwards a syscall, an I/O request or a memory access to the real host kernel ... everything a guest observes comes from the
emulated environment and the captured profiles").

The NEW "no host pass-through" scan of milestone 3.M4, beside no_native_check (which keeps its own rules):
  P  static, over every emulator code file (the four Emulator projects + Emulator/include + Emulator/src: the file list of
     check_no_native.py S3), comments and string / character literals blanked first:
       pt_nt_symbol       any Nt* / Zw* identifier (a call, its address, a typedef'd pointer): the emulated kernel's handlers are
                          never named so; the guest's service names appear only as strings
       pt_host_syscall    a host system-call path: syscall( / __syscall / NtCurrentTeb / NtCurrentPeb / __readgs* / __readfs* /
                          __writegs* / __writefs* / _read|_write(fs|gs)base* calls
       pt_syscall_names   host system-call / shared-data names: KiFastSystemCall*, Wow64Transition, SharedUserData,
                          USER_SHARED_DATA, KI_USER_SHARED_DATA, MM_SHARED_USER_DATA_VA, KiUserSharedData
       pt_process_memory  another process's memory or context: Read/WriteProcessMemory, Toolhelp32ReadProcessMemory,
                          Virtual(Query|Alloc|Protect|Free)Ex, OpenProcess, OpenThread, Get/SetThreadContext, DuplicateHandle,
                          MiniDumpWriteDump, CreateToolhelp32Snapshot
       pt_host_kuser      the host KUSER_SHARED_DATA address 7FFE0xxxH in any spelling (hex with suffixes, decimal 2147352576) -
                          except the ONE definition of the guest's constant (a name starting guest_, = 0x7FFE0000) in
                          Emulator/include/emu/win_env.hpp
       pt_host_query      host facts a guest could be shown: GetSystemInfo, GetNativeSystemInfo, GlobalMemoryStatus*,
                          GetVersion*, RtlGetVersion, VerifyVersionInfo*, IsProcessorFeaturePresent, GetComputerName*,
                          firmware tables, GetModuleHandle*, Get(System|Windows)Directory*, environment variables (getenv ...),
                          processor counts, time zone, locale, GetProductInfo - exemptions with an exact count below (EXEMPT)
       pt_host_clock      (U2098, plan 3.M6: "never the host clock at runtime") host time and sleep: QueryPerformanceCounter /
                          Frequency, GetSystemTime* / GetLocalTime / GetTickCount* / timeGetTime / timeGetSystemTime /
                          QueryInterruptTime* / QueryUnbiasedInterruptTime* / GetProcessTimes / GetThreadTimes / GetSystemTimes,
                          Sleep / SleepEx, RDTSC / RDTSCP executed natively (__rdtsc / __rdtscp / _rdtsc / __builtin_ia32_rdtsc*),
                          the C / C++ clocks (time( / clock( / _time32 / _time64 / _ftime* / gettimeofday / clock_gettime,
                          std::chrono::*_clock) - every guest-visible time comes from emu/vclock.hpp
     and, on the code with comments blanked but strings kept:
       pt_live_system_dir a path into the running machine's system directory (System32, SysWOW64, %SystemRoot%, %windir%,
                          \\Windows\\): the emulator reads only the environment's capture (decision 3.0.8)
  T  (U2098) the time path: unicorn/qemu/hw/i386/x86.c cpu_get_tsc returns uc_vclock_base( ... ) for UC_X86_VCLOCK_VIRTUAL before
     the stock cpu_get_ticks() (the host TSC), and Emulator/src/engine.cpp opens every engine with uc_ctl_set_x86_vclock(
     UC_X86_VCLOCK_VIRTUAL ) - no emulator engine reads the host TSC for a guest value (runtime: emu-selftest vclock_consistency,
     deterministic RDTSC run to run)
  R  runtime: (1) emu-selftest win_env_no_passthrough_runtime; (2) emu-oep runs T1 with the REAL NtTerminateProcess number of
     the guest environment (the same number the host kernel of this VMware guest uses) and ProcessHandle NtCurrentProcess():
     forwarded to the host it would end emu-oep itself with exit code 2AH - instead emu-oep must report the guest's exit
     status, print its result line after it and exit 0; the guest's one SYSCALL handled by the emulated kernel layer.
Negative controls (in memory, every run): each rule fires on its sample; comments, strings, the guest constant and member names
do not. Reads only (the runtime part writes one patched T1 copy into the build output). Exit 0 PASS, 1 FAIL, 2 usage.
usage: python -I check_no_passthrough.py REPO_ROOT --build TESTS_DIR
"""
import importlib.util
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True   # python -I ignores PYTHONDONTWRITEBYTECODE: the sibling modules loaded below leave no __pycache__
HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


CNN = load('check_no_native')

PT_RULES = [
    ('pt_nt_symbol', r'(?<![\w$])(?:Nt|Zw)[A-Z][A-Za-z0-9_]*'),
    ('pt_host_syscall', r'(?<![\w.>:])(?:syscall|__syscall|_syscall[0-9]?|NtCurrentTeb|NtCurrentPeb|__readgs\w*|__readfs\w*|__writegs\w*|__writefs\w*|'
                        r'_readgsbase\w*|_readfsbase\w*|_writegsbase\w*|_writefsbase\w*)\s*\('),
    ('pt_syscall_names', r'\b(?:KiFastSystemCall\w*|Wow64Transition|SharedUserData|USER_SHARED_DATA|KI_USER_SHARED_DATA|MM_SHARED_USER_DATA_VA|KiUserSharedData)\b'),
    ('pt_process_memory', r'\b(?:ReadProcessMemory|WriteProcessMemory|Toolhelp32ReadProcessMemory|VirtualQueryEx|VirtualAllocEx\w*|VirtualProtectEx|VirtualFreeEx|'
                          r'OpenProcess|OpenThread|GetThreadContext|SetThreadContext|Wow64GetThreadContext|DuplicateHandle|MiniDumpWriteDump|CreateToolhelp32Snapshot)\b'),
    ('pt_host_kuser', r'(?i)\b0x0*7ffe0[0-9a-f]{3}(?:ui64|ull|ul|u|l)*\b|\b2147352576\b'),
    ('pt_host_query', r'\b(?:GetSystemInfo|GetNativeSystemInfo|GlobalMemoryStatus\w*|GetVersion\w*|RtlGetVersion|RtlGetNtVersionNumbers|VerifyVersionInfo\w*|'
                      r'IsProcessorFeaturePresent|GetComputerName\w*|GetSystemFirmwareTable|EnumSystemFirmwareTables|GetModuleHandle\w*|GetSystemDirectory\w*|'
                      r'GetWindowsDirectory\w*|GetSystemWindowsDirectory\w*|GetEnvironmentStrings\w*|GetEnvironmentVariable\w*|getenv|_wgetenv|_dupenv_s|_wdupenv_s|'
                      r'GetLogicalProcessorInformation\w*|GetActiveProcessorCount|GetMaximumProcessorCount|GetTimeZoneInformation|GetDynamicTimeZoneInformation|'
                      r'GetSystemDefault\w*|GetUserDefault\w*|GetProductInfo)\b'),
    # U2098 (plan 3.M6): the host clock
    ('pt_host_clock', r'\b(?:QueryPerformanceCounter|QueryPerformanceFrequency|GetSystemTime\w*|GetLocalTime|GetTickCount\w*|timeGetTime|timeGetSystemTime|'
                      r'QueryInterruptTime\w*|QueryUnbiasedInterruptTime\w*|GetProcessTimes|GetThreadTimes|GetSystemTimes|SleepEx|Sleep|__rdtscp?|_rdtscp?|'
                      r'__builtin_ia32_rdtscp?|_time32|_time64|_ftime\w*|gettimeofday|clock_gettime|steady_clock|system_clock|high_resolution_clock|'
                      r'utc_clock|tai_clock|gps_clock|file_clock)\b|(?<![\w.>:])(?:time|clock)\s*\(|\bstd::(?:time|clock)\s*\('),
]
STRING_RULES = [
    ('pt_live_system_dir', r'(?i)system32|syswow64|%systemroot%|%windir%|\\\\windows\\\\'),
]
GUEST_KUSER_DEF = re.compile(r'(?i)\bguest_\w+\s*=\s*0x0*7ffe0000\b')
GUEST_KUSER_FILE = 'Emulator/include/emu/win_env.hpp'
EXEMPT = {
    ('Emulator/src/host_guard.cpp', 'pt_host_query'): (1, 'GetSystemInfo: the address bounds of the VirtualQuery walk over the OWN process (U1941), never guest-visible'),
    # U2042 (P37, worktrees on E:\novmp_wt): the host folder of the local capture, as capture_sysdlls.py / win_env_t1.py / emu-profilegen
    ('Emulator/src/sysdll.cpp', 'pt_host_query'): (1, '_dupenv_s("NOVMP_CAPTURE_ROOT"): the host path of the local system-DLL capture (locate_capture, U2042), '
                                                     'read as a file location only, never written into guest memory'),
    # U2025-U2026 (plan 3.M5 part 2): guest-visible paths of the EMULATED environment, built from the capture's system root and
    # written into guest memory only (never opened on the host): the system images' FullDllName, ComSpec / Path of the synthesized
    # environment block; and the selftest's expectation of one of them
    ('Emulator/src/win_process.cpp', 'pt_live_system_dir'): (4, 'guest-visible paths of the emulated environment (FullDllName <system root>\\System32\\..., '
                                                                'ComSpec, Path) written into guest memory, never opened on the host (U2025-U2026)'),
    ('Emulator/tests/selftest/st_main.cpp', 'pt_live_system_dir'): (22, 'win_env_process_state expects the guest-visible FullDllName C:\\Windows\\System32\\ntdll.dll '
                                                                       '(a string compared in guest memory, U2027); P39 (U2061) nt_measured_loader: the guest-visible '
                                                                       'KnownDllPath target <system root>\\System32 (x2), the guest paths \\??\\C:\\Windows\\Temp\\ (a path '
                                                                       'outside the emulated file system, expected to stop) and \\Sessions\\1\\Windows\\SharedSection / '
                                                                       'ApiPort (the emulated namespace) written into guest memory (x3); P40 (U2067) nt_measured_csr: the '
                                                                       '\\Sessions\\<1|2>\\Windows\\ApiPort / SharedSection names and \\??\\C:\\Windows\\win.ini (an NtCreateFile that must stop) '
                                                                       'written into guest memory, the expected guest-visible directories of the CSR shared view (x6); P41 (U2087) nt_measured_console: '
                                                                       'the user-hive key \\REGISTRY\\USER\\<the placeholder SID>\\Software\\Policies\\Microsoft\\Windows\\Safer\\... '
                                                                       'written into guest memory (x1); P43 (U2110) nt_measured_dllsearch: the guest paths '
                                                                       '\\??\\C:\\Windows\\SYSTEM32\\<name> of the emulated system directory written into guest memory '
                                                                       '(4 lines, 2 matches each: x8)'),
    # P41 (U2073): the guest-visible key name of the USER's hive the loader opens (compared with a guest OBJECT_ATTRIBUTES name; the
    # user hive is never read on the host - each key is measured from inside tm_nt, hku.*)
    ('Emulator/src/nt_kernel_sys.cpp', 'pt_live_system_dir'): (1, 'the measured user-hive key \\Software\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers '
                                                                 'compared with a guest name under the placeholder user SID (tm_nt hku.safer) - never opened on the host (P41)'),
    # P39 (U2057 / U2060): the EMULATED object namespace - the \KnownDlls\KnownDllPath link target built from the capture's system root,
    # its text in the reply, the session's CSR shared-section name compared with a guest OBJECT_ATTRIBUTES name, the frontier text naming it
    ('Emulator/src/nt_kernel_measured.cpp', 'pt_live_system_dir'): (6, 'the emulated namespace: the KnownDllPath target <system root>\\System32 (written into '
                                                                       'guest memory), its reply text, the guest name \\Sessions\\<id>\\Windows\\SharedSection compared, '
                                                                       'the frontier stop text - never opened on the host (P39); P40 (U2065): the guest name \\Sessions\\<id>\\Windows\\ApiPort compared and '
                                                                       'the system directory of the CSR shared view <system root>\\system32 written into guest memory (the P39 stop text removed); '
                                                                       'P43 (U2107): the prefix \\??\\<system root>\\system32\\ a guest OBJECT_ATTRIBUTES name is compared '
                                                                       'with (the emulated system directory = the capture\'s files, never opened on the host; x1)'),
}


def strip_comments(text):
    """Blank comments only (string literals kept), newlines kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        c, nx = text[i], text[i + 1] if i + 1 < n else ''
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"' and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1]); i = j + 1; continue
        if c == '/' and nx == '/':
            j = text.find('\n', i); j = n if j < 0 else j
            out.append(' ' * (j - i)); i = j; continue
        if c == '/' and nx == '*':
            j = text.find('*/', i + 2); j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j])); i = j; continue
        out.append(c); i += 1
    return ''.join(out)


def scan(text, rel=''):
    hits = []
    code = CNN.strip_code(text)
    lines = text.split('\n')
    # the ONE exempt spelling: the value of the first guest_* = 0x7FFE0000 definition in win_env.hpp (exactly that literal)
    exempt_span = None
    if rel == GUEST_KUSER_FILE:
        d = GUEST_KUSER_DEF.search(code)
        if d:
            exempt_span = (d.start(), d.end())
    for rid, rx in PT_RULES:
        for m in re.finditer(rx, code):
            ln = code.count('\n', 0, m.start()) + 1
            if rid == 'pt_host_kuser' and exempt_span and exempt_span[0] <= m.start() and m.end() <= exempt_span[1]:
                continue
            hits.append((rid, ln, lines[ln - 1].strip()[:110]))
    kept = strip_comments(text)
    for rid, rx in STRING_RULES:
        for m in re.finditer(rx, kept):
            ln = kept.count('\n', 0, m.start()) + 1
            hits.append((rid, ln, lines[ln - 1].strip()[:110]))
    return hits


MUST_HIT = [
    ('pt_nt_symbol', 'status = NtQuerySystemInformation( 5, buf, n, &r );'),
    ('pt_nt_symbol', 'auto fn = &ZwTerminateProcess;'),
    ('pt_nt_symbol', 'using query_t = decltype( NtQueryInformationProcess );'),
    ('pt_host_syscall', 'long r = syscall( 60, 0 );'),
    ('pt_host_syscall', 'auto teb = NtCurrentTeb();'),
    ('pt_host_syscall', 'uint64_t p = __readgsqword( 0x60 );'),
    ('pt_syscall_names', 'auto k = SharedUserData->TickCountMultiplier;'),
    ('pt_syscall_names', 'const uint64_t a = MM_SHARED_USER_DATA_VA;'),
    ('pt_process_memory', 'ReadProcessMemory( h, a, b, n, &got );'),
    ('pt_process_memory', 'GetThreadContext( th, &ctx );'),
    ('pt_host_kuser', 'memcpy( page, ( const void* )0x7FFE0000, 0x1000 );'),
    ('pt_host_kuser', 'uint32_t sc = *( uint32_t* )0x7ffe0308ull;'),
    ('pt_host_kuser', 'uint64_t kuser = 2147352576;'),
    ('pt_host_query', 'SYSTEM_INFO si; GetNativeSystemInfo( &si );'),
    ('pt_host_query', 'const char* r = getenv( "SystemRoot" );'),
    ('pt_host_query', 'auto b = (uint64_t)GetModuleHandleA( "ntdll.dll" );'),
    ('pt_host_clock', 'LARGE_INTEGER q; QueryPerformanceCounter( &q );'),
    ('pt_host_clock', 'FILETIME ft; GetSystemTimeAsFileTime( &ft );'),
    ('pt_host_clock', 'const uint64_t t = __rdtsc();'),
    ('pt_host_clock', 'unsigned a; auto t = __rdtscp( &a );'),
    ('pt_host_clock', 'auto ms = GetTickCount64();'),
    ('pt_host_clock', 'DWORD t = timeGetTime();'),
    ('pt_host_clock', 'auto n = std::chrono::steady_clock::now();'),
    ('pt_host_clock', 'Sleep( 10 );'),
    ('pt_host_clock', 'time_t now = time( nullptr );'),
    ('pt_host_clock', 'auto c = std::clock();'),
    ('pt_live_system_dir', 'std::string p = "C:\\\\Windows\\\\System32\\\\ntdll.dll";'),
    ('pt_live_system_dir', 'auto d = std::string( "%SystemRoot%" ) + "\\\\x";'),
]
MUST_PASS = [
    '// NtQuerySystemInformation( ... ) and SharedUserData in a comment; syscall( 1 ); GetSystemInfo( &si );',
    'std::cout << "NtTerminateProcess( NtCurrentProcess() ) ended the guest";',
    'void on_syscall( uc_engine* uc, void* user ); emu::nt::query_system_information( c ); x.syscall( 1 );',
    'uint64_t kuser_nt_major_version = 0x26C; const char* n = "NT_TIB"; auto t = offsetof( NT_TIB, Self );',
    'constexpr uint64_t page = 0x7FFE1000; uint64_t big = 21473525760;',
    'const char* root = "C:\\\\Windows"; /* System32 */',
    'vclock::read( uc, hz, rd, why ); r = kern.time_now( q, st, why ); // QueryPerformanceCounter, __rdtsc in a comment',
    'std::string n = "NtQuerySystemTime"; const char* q = "QueryPerformanceCounter"; x.time( 1 ); p->clock( 2 );',
]


def run_controls():
    fails = []
    for rid, text in MUST_HIT:
        if rid not in [h[0] for h in scan(text)]:
            fails.append('rule %s did not fire on: %s' % (rid, text))
    for text in MUST_PASS:
        got = scan(text)
        if got:
            fails.append('false hit %s on: %s' % ([g[0] for g in got], text))
    if scan('inline constexpr uint64_t guest_kuser_va = 0x7FFE0000;', GUEST_KUSER_FILE):
        fails.append('the guest constant definition in win_env.hpp was not exempt')
    if not scan('inline constexpr uint64_t guest_kuser_va = 0x7FFE0000;', 'Emulator/src/other.cpp'):
        fails.append('a guest_ constant outside win_env.hpp was not refused')
    if not scan('uint64_t guest_alias = 0x7FFE0000; auto p = (void*)0x7FFE0000;', GUEST_KUSER_FILE):
        fails.append('a second use of the address in win_env.hpp was not refused')
    return fails


def runtime(root, build, report):
    fails = []
    st = os.path.join(build, 'emu-selftest.exe')
    r = subprocess.run([st, 'win_env_no_passthrough_runtime'], capture_output=True, text=True, timeout=300)
    out = r.stdout.splitlines()
    report += ['R1 | ' + l for l in out]
    if r.returncode != 0 or not any(re.match(r'^Test win_env_no_passthrough_runtime\.\.\. +\[ OK \]$', l) for l in out):
        fails.append('R1 emu-selftest win_env_no_passthrough_runtime failed (exit %d)' % r.returncode)
    t1m = load('win_env_t1')
    cap = t1m.capture_dir(root, 'vmware_guest')
    t1 = os.path.join(build, 'test_pe', 't1_exit.exe')
    oep = os.path.join(build, 'emu-oep.exe')
    if not cap or not os.path.isfile(t1) or not os.path.isfile(oep):
        return fails + ['R2 needs the VMware-guest capture (capture_sysdlls.py), test_pe\\t1_exit.exe and emu-oep.exe']
    num = t1m.stub_numbers(open(os.path.join(cap, 'ntdll.dll'), 'rb').read()).get('NtTerminateProcess')
    img = bytearray(open(t1, 'rb').read())
    at = img.find(b'T1SYSNUM')
    if num is None or at < 0:
        return fails + ['R2: no NtTerminateProcess stub / no T1 marker']
    img[at + 8:at + 12] = num.to_bytes(4, 'little')
    p = os.path.join(build, 'test_pe', 't1_exit.passthrough_check.exe')
    open(p, 'wb').write(bytes(img))
    r = subprocess.run([oep, '--guest', p], capture_output=True, text=True, timeout=300)
    out = r.stdout.splitlines()
    report.append('R2 | emu-oep --guest T1 (NtTerminateProcess %s = the number this guest\'s own kernel uses): exit %d; %s' %
                  (hex(num), r.returncode, out[-1] if out else '(no output)'))
    if r.returncode == 0x2A:
        fails.append('R2 emu-oep exited with the GUEST\'S status 2AH: the system call reached the host kernel')
    elif r.returncode != 0 or not out or not out[-1].startswith('[emu-oep] result: guest exit status 0x0000002a; the emu-oep process continues'):
        fails.append('R2 emu-oep did not report the emulated termination and continue (exit %d)' % r.returncode)
    if not any('syscall instructions executed by the guest: 1; handled by the emulated kernel layer: 1; forwarded to the host: 0' in l for l in out):
        fails.append('R2 the guest\'s SYSCALL was not handled by the emulated kernel layer')
    return fails


def time_path(root):
    """U2098 check T: the fork's cpu_get_tsc takes the virtual time base before the host ticks; engine.cpp sets the virtual mode."""
    fails = []
    x86 = open(os.path.join(root, 'unicorn', 'qemu', 'hw', 'i386', 'x86.c'), encoding='utf-8', errors='replace').read()
    body = x86[x86.find('uint64_t cpu_get_tsc(CPUX86State *env)'):]
    v = body.find('if (env->uc->vclock_mode == UC_X86_VCLOCK_VIRTUAL)')
    h = body.find('return cpu_get_ticks();')
    if v < 0 or h < 0 or v > h or 'return uc_vclock_base(env->uc);' not in body[v:h]:
        fails.append('T  unicorn/qemu/hw/i386/x86.c cpu_get_tsc: no virtual-time-base branch before the host ticks (U2090)')
    eng = CNN.strip_code(open(os.path.join(root, 'Emulator', 'src', 'engine.cpp'), encoding='utf-8', errors='replace').read())
    if not re.search(r'uc_ctl_set_x86_vclock\s*\(\s*e->uc_\s*,\s*UC_X86_VCLOCK_VIRTUAL\s*,', eng):
        fails.append('T  Emulator/src/engine.cpp does not open the engine with uc_ctl_set_x86_vclock( UC_X86_VCLOCK_VIRTUAL )')
    return fails


def main(argv):
    if len(argv) != 3 or argv[1] != '--build':
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, build = os.path.abspath(argv[0]), os.path.abspath(argv[2])
    report, fails = [], []
    cf = run_controls()
    print('negative controls: %d rule samples, %d clean samples, 3 guest-constant controls: %d failed' % (len(MUST_HIT), len(MUST_PASS), len(cf)))
    fails += ['control: ' + f for f in cf]
    files, _f3 = CNN.check_projects(root, [])   # S3's problems are no_native_check's verdict; the file list is reused here
    if not files:
        fails.append('P no emulator code found - the scan cannot pass vacuously')
    found = {}
    for ap in files:
        rel = CNN.relp(root, ap)
        for rid, ln, snip in scan(open(ap, encoding='utf-8', errors='replace').read(), rel):
            found.setdefault((rel, rid), []).append((ln, snip))
    report.append('P  scanned %d emulator files, rules %s' % (len(files), ' '.join(r for r, _ in PT_RULES + STRING_RULES)))
    for (rel, rid), hits in sorted(found.items()):
        ex = EXEMPT.get((rel, rid))
        if ex and ex[0] == len(hits):
            report.append('P  exempt %s %s x%d: %s' % (rel, rid, len(hits), ex[1]))
            continue
        fails.append('P  %s %s x%d: %s' % (rel, rid, len(hits), '; '.join('line %d: %s' % h for h in hits[:4])))
    for (rel, rid), ex in sorted(EXEMPT.items()):
        if (rel, rid) not in found:
            fails.append('exemption %s %s no longer found - remove it' % (rel, rid))
    tf = time_path(root)
    report.append('T  the time path: fork cpu_get_tsc virtual branch before the host ticks, engine.cpp UC_X86_VCLOCK_VIRTUAL: %s' % ('ok' if not tf else 'FAIL'))
    fails += tf
    fails += runtime(root, build, report)
    for l in report:
        print(l)
    for f in fails:
        print('FAIL ' + f)
    if fails:
        print('no_host_passthrough: FAIL (%d problem(s))' % len(fails))
        return 1
    print('no_host_passthrough: PASS (static: no Nt* / syscall / process-memory / host-KUSER / host-query / host-clock / live System32 path in %d emulator files; '
          'the TSC of every emulator engine on the virtual time base; '
          'runtime: the emulated system call never reached the host)' % len(files))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
