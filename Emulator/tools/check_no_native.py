#!/usr/bin/env python3
"""check_no_native.py - suite no_native_check (plan 3.M1, ledger U1942-U1943; AGENT_RULES "Host safety", your decision
2026-10-24 3.0.9; start plan R1 / R2).

Rule: guest code (the sample, a snippet, anything target-derived) never runs on the host CPU, and no emulator path forwards a
syscall, an I/O request or a memory access to the real host kernel. emu-alltest is OUTSIDE this check by rule (AGENT_RULES
"Safety": it runs our own self-generated snippets natively; never the sample). What is checked:

  S1 emulator source scan - every C / C++ file of the Emulator projects (Emulator.vcxproj, emu-oep, emu-snippet,
     emu-selftest: their ClCompile / ClInclude items, plus every file under Emulator/include and Emulator/src, which must
     all be listed). Comments, string and character literals are blanked first (a comment may NAME an API). Rules (RULES):
       exec      executable host memory: PAGE_EXECUTE* (except as the operand of == / != / case: classifying a protection
                 that VirtualQuery reported is no request for one), HEAP_CREATE_ENABLE_EXECUTE, FILE_MAP_EXECUTE / SEC_IMAGE /
                 PROT_EXEC, calls of VirtualAlloc* / VirtualProtect* / MapViewOfFile* / CreateFileMapping* / mmap / mprotect
       fnptr     a cast to a function-pointer type (abstract declarator "(*)(" or "(__stdcall*)(") = calling into a buffer;
                 inline assembly (__asm)
       loader    host code loading: LoadLibrary*, GetProcAddress, LdrLoadDll / LdrGetProcedureAddress*; #pragma comment(lib,
                 "ntdll...")
       process   CreateProcess*, WinExec, ShellExecute*, CreateRemoteThread*, OpenProcess, Read/WriteProcessMemory,
                 DebugActiveProcess*, WaitForDebugEvent*, system / popen / spawn* / exec*
       kernel    host-kernel forwarding: any call Nt*( / Zw*( (an emulated handler is never named Nt* / Zw*: use
                 emu::nt::... names), DeviceIoControl, host TEB / PEB reads (NtCurrentTeb, __readgs*, __readfs*), a host
                 pointer to KUSER_SHARED_DATA (a cast of 0x7FFE0000 to a pointer)
       hosttime  host time / sleep (host-safety rule: time comes only from the one virtual time base, M6): Sleep*,
                 QueryPerformanceCounter / Frequency, GetTickCount*, GetSystemTime*, GetLocalTime, timeGetTime,
                 QueryUnbiasedInterruptTime, __rdtsc / __rdtscp, std::chrono, std::this_thread, time( NULL ), clock()
       hostcpu   host CPU queries (decision 3.0.3: runtime CPUID only as the explicit option --cpuid-runtime; D8 --HostSeed):
                 __cpuid / __cpuidex / _xgetbv / __readmsr / __rdpmc / _rdrand*_step / _rdseed*_step - each use needs an
                 allow-list row naming the explicit option
       fileio    (U1979, plan 3.M3; replaces the M1-only group "opens") file access: fopen* / _wfopen* / CreateFile* /
                 OpenFile / _open / _wopen / _sopen*, std::(i|o)fstream, std::filesystem create / remove / rename / copy are
                 findings everywhere except in the ONE audited reader Emulator/src/input_file.cpp (emu::read_input_file /
                 read_input_lines: read-only, bounded), where exactly std::ifstream is allowed and every other match is a
                 finding (a write, a create, another open) - the lib and its tools only ever READ their inputs; U2020 (plan 3.M5
                 part 2): plus the ONE audited writer Emulator/src/output_file.cpp (emu::output_file: create / append text,
                 bounded - emu-oep --trace FILE), where exactly std::ofstream is allowed and every other match is a finding
  S2 NoVmp native path - the NoVmp project's own sources (NoVmp/NoVmp.vcxproj ClCompile / ClInclude) through the rule groups
     exec, fnptr, loader, kernel (not process: main.cpp's interactive system( "pause" ) runs no guest code); plus "compiled_native_executor": emulator\\emulator.cpp / emulator\\rwx_allocator.cpp
     as ClCompile items of NoVmp.vcxproj. Known findings are listed in Emulator/data/no_native_allowlist.tsv (count per file
     and rule, each with the milestone that removes it); the findings must equal the allow-list EXACTLY (a new finding fails,
     and so does an allow-listed one that is gone - the list must shrink with the code). Today the native executor is
     compiled into NoVmp behind the N6 guard: PENDING until M8 (3.C), when the allow-list becomes empty.
     --no-allowlist = the M8 target state (every finding fails).
  S3 project graph - the four Emulator projects are in NoVmp.sln under the solution folder "Emulator"; none lists a file
     outside Emulator/ (never NoVmp/emulator/*), none has a MASM / .asm item (no host syscall stub), none references
     NoVmp.vcxproj or links ntdll.lib; Emulator.vcxproj is a static library referencing unicorn / capstone / keystone; the
     solution flags (v143, /MT in Release, C++latest, /permissive-).
  S5 (U1989, plan 3.M4) test PEs = GUEST code (Emulator/tests/pe/*: T1 ...): every such project in NoVmp.sln references no
     project, writes its executable to build\\x64\\<cfg>\\tests\\test_pe\\ (data for emu-oep), and no Emulator project references
     it - guest code is never linked into the emulator; the emulator reads the built file as data only.
  S4 imports of the built emu-oep / emu-snippet / emu-selftest (--build DIR): nothing from ntdll.dll, no Nt* / Zw* import,
     none of DENY_IMPORTS (process creation / injection / debugging, DeviceIoControl, LoadLibraryA / W / ExA; the static
     CRT's own LoadLibraryExW is the one loader import allowed).
  R  runtime - DIR\\emu-selftest.exe no_native_runtime (own test code): no executable memory outside mapped images in the
     process (no RWX heap / shellcode / JIT at M1) and a guest-code marker is found in readable memory (positive control)
     but never in executable memory.
Negative controls (in memory, every run): each rule must fire on its sample, comment / string text must not, a
synthetic project item into NoVmp/emulator and a synthetic ntdll import must be refused. Reads only; starts only
emu-selftest. Exit 0 = PASS (pending allow-listed findings named), 1 = FAIL, 2 = usage.
usage: python -I check_no_native.py REPO_ROOT [--build TESTS_DIR] [--no-allowlist]
"""
import os
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

MSB = '{http://schemas.microsoft.com/developer/msbuild/2003}'
C_EXT = ('.c', '.h', '.cc', '.cpp', '.cxx', '.hpp', '.hh', '.inc', '.inl')
CALL = r'(?<![\w.>:])'   # not a member / qualified name: obj.system( / ns::Sleep( are someone else's functions

RULES = [
    # (rule id, group, regex)
    ('exec_page_protection', 'exec', r'(?<![=!]=)(?<![=!]=\s)(?<!case\s)\bPAGE_EXECUTE\w*\b(?!\s*[=!]=)'),
    ('exec_heap', 'exec', r'\bHEAP_CREATE_ENABLE_EXECUTE\b'),
    ('exec_mapping', 'exec', r'\b(?:FILE_MAP_EXECUTE|SEC_IMAGE\w*|PROT_EXEC)\b'),
    ('host_page_api', 'exec', CALL + r'(?:VirtualAlloc\w*|VirtualProtect\w*|MapViewOfFile\w*|CreateFileMapping\w*|mmap|mprotect)\s*\('),
    ('fnptr_cast', 'fnptr', r'\(\s*(?:__(?:stdcall|cdecl|fastcall|vectorcall|thiscall)\s*)?\*\s*\)\s*\('),
    ('inline_asm', 'fnptr', r'\b(?:__asm|_asm)\b'),
    ('host_loader', 'loader', CALL + r'(?:LoadLibrary\w*|GetProcAddress|LdrLoadDll|LdrGetProcedureAddress\w*)\s*\('),
    ('host_process', 'process', CALL + r'(?:CreateProcess\w*|WinExec|ShellExecute\w*|CreateRemoteThread\w*|OpenProcess|WriteProcessMemory|'
                                       r'ReadProcessMemory|DebugActiveProcess\w*|WaitForDebugEvent\w*|system|_?popen|_?spawn\w*|_?exec[lv]p?e?)\s*\('),
    ('nt_call', 'kernel', CALL + r'(?:Nt|Zw)[A-Z]\w*\s*\('),
    ('device_io', 'kernel', CALL + r'DeviceIoControl\s*\('),
    ('host_teb_peb', 'kernel', r'\b(?:NtCurrentTeb|NtCurrentPeb|__readgs\w*|__readfs\w*|_readgsbase\w*|_readfsbase\w*)\b'),
    ('host_kuser', 'kernel', r'(?i)(?:\*\s*\)\s*0x0*7ffe0000|reinterpret_cast\s*<[^>]*\*\s*>\s*\(\s*0x0*7ffe0000)'),
    ('host_time', 'hosttime', CALL + r'(?:Sleep(?:Ex)?|SleepConditionVariable\w*|QueryPerformanceCounter|QueryPerformanceFrequency|'
                                     r'GetTickCount\w*|GetSystemTime\w*|GetLocalTime|timeGetTime|QueryUnbiasedInterruptTime\w*|__rdtscp?|_rdtsc)\s*\('),
    ('host_time_std', 'hosttime', r'\bstd::(?:chrono|this_thread)\b|' + CALL + r'time\s*\(\s*(?:NULL|nullptr|0)\s*\)|' + CALL + r'clock\s*\(\s*\)'),
    ('host_cpuid', 'hostcpu', CALL + r'(?:__cpuid(?:ex)?|_xgetbv|__readmsr|__rdpmc)\s*\('),
    ('host_rng', 'hostcpu', CALL + r'(?:_rdrand\d+_step|_rdseed\d+_step)\s*\('),
    ('opens_file', 'fileio', CALL + r'(?:fopen\w*|_wfopen\w*|CreateFile\w*|OpenFile|_open|_wopen|_sopen\w*)\s*\(|\bstd::(?:i|o)?fstream\b|'
                             r'\bstd::filesystem::(?:create_director\w*|remove\w*|rename|copy\w*)\b'),
]
RAW_RULES = [
    ('ntdll_pragma', 'loader', r'(?i)#\s*pragma\s+comment\s*\(\s*lib\s*,\s*"[^"]*ntdll'),
]
EMU_GROUPS = ('exec', 'fnptr', 'loader', 'process', 'kernel', 'hosttime', 'hostcpu', 'fileio')
AUDITED_READER = 'Emulator/src/input_file.cpp'   # U1979: the one place a file is opened (read-only std::ifstream)
READER_ALLOWED = re.compile(r'^std::ifstream$')
AUDITED_WRITER = 'Emulator/src/output_file.cpp'   # U2020: the one place a file is created (write-only std::ofstream: emu-oep --trace)
WRITER_ALLOWED = re.compile(r'^std::ofstream$')
NOVMP_GROUPS = ('exec', 'fnptr', 'loader', 'kernel')   # not 'process': NoVmp's CLI system( "pause" ) is no guest code

EMU_PROJECTS = [
    ('Emulator', 'Emulator/Emulator.vcxproj'),
    ('emu-oep', 'Emulator/tools/oep/emu-oep.vcxproj'),
    ('emu-snippet', 'Emulator/tools/snippet/emu-snippet.vcxproj'),
    ('emu-selftest', 'Emulator/tests/selftest/emu-selftest.vcxproj'),
]
EMU_EXES = ('emu-oep', 'emu-snippet', 'emu-selftest')
NATIVE_EXECUTOR = ('emulator/emulator.cpp', 'emulator/rwx_allocator.cpp')
DEP_GUIDS = {'unicorn': '{5F160114-1B96-585D-9629-47B21D8F1CC0}', 'capstone': '{873022FC-CC65-5981-9086-4F9AB107BA4F}',
             'keystone': '{86818791-A27D-5702-86E3-EFE9C61E7EC8}'}
DENY_IMPORTS = {
    'createprocessa', 'createprocessw', 'createprocessasusera', 'createprocessasuserw', 'createprocesswithlogonw',
    'createprocesswithtokenw', 'winexec', 'shellexecutea', 'shellexecutew', 'shellexecuteexa', 'shellexecuteexw',
    'createremotethread', 'createremotethreadex', 'openprocess', 'writeprocessmemory', 'readprocessmemory', 'virtualallocex',
    'virtualallocexnuma', 'virtualprotectex', 'debugactiveprocess', 'debugactiveprocessstop', 'waitfordebugevent',
    'waitfordebugeventex', 'continuedebugevent', 'deviceiocontrol', 'loadlibrarya', 'loadlibraryw', 'loadlibraryexa',
}
ALLOWLIST = 'Emulator/data/no_native_allowlist.tsv'

# ----------------------------------------------------------------------------------------------------------- helpers

def strip_code(text):
    """Blank comments, string and character literals (newlines kept, so line numbers stay)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nx = text[i + 1] if i + 1 < n else ''
        if c == '/' and nx == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i)); i = j; continue
        if c == '/' and nx == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j])); i = j; continue
        if c == 'R' and nx == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_') or text[i - 1] in 'uU8L'):
            m = re.match(r'R"([^()\\ \n]{0,16})\(', text[i:])
            if m:
                end = text.find(')' + m.group(1) + '"', i + m.end())
                end = n if end < 0 else end + len(m.group(1)) + 2
                out.append('R""' + re.sub(r'[^\n]', ' ', text[i + 3:end])[:max(0, end - i - 3)]); i = end; continue
        if c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum() and nx.isalnum())):
            q = c
            j = i + 1
            while j < n and text[j] != q and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(q + ' ' * max(0, j - i - 2) + (q if j - i >= 2 else '')); i = j; continue
        out.append(c); i += 1
    return ''.join(out)


def scan_text(text, groups, reader=False, writer=False):
    """[(rule, line, snippet)] for the rule groups given; reader = the audited file reader (only std::ifstream exempt);
    writer = the audited file writer (U2020: only std::ofstream exempt)."""
    hits = []
    code = strip_code(text)
    for rid, grp, rx in RULES:
        if grp not in groups:
            continue
        for m in re.finditer(rx, code):
            if reader and rid == 'opens_file' and READER_ALLOWED.match(m.group(0).strip()):
                continue
            if writer and rid == 'opens_file' and WRITER_ALLOWED.match(m.group(0).strip()):
                continue
            line = code.count('\n', 0, m.start()) + 1
            hits.append((rid, line, text.split('\n')[line - 1].strip()[:110]))
    for rid, grp, rx in RAW_RULES:
        if grp not in groups:
            continue
        for m in re.finditer(rx, text):
            line = text.count('\n', 0, m.start()) + 1
            hits.append((rid, line, text.split('\n')[line - 1].strip()[:110]))
    return hits


def project_items(vcxproj_path, xml_text=None):
    root = ET.fromstring(xml_text) if xml_text is not None else ET.parse(vcxproj_path).getroot()
    items = {}
    for tag in ('ClCompile', 'ClInclude', 'MASM', 'None', 'Text', 'CustomBuild'):
        for el in root.iter(MSB + tag):
            inc = el.get('Include')
            if inc:
                items.setdefault(tag, []).append(inc)
    return root, items


def norm(root, base_dir, rel):
    return os.path.normpath(os.path.join(base_dir, rel.replace('\\', os.sep)))


def relp(root, p):
    return os.path.relpath(p, root).replace('\\', '/')


def pe_imports(data):
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE')
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    optsz = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from('<H', data, opt)[0] != 0x20B:
        raise ValueError('not PE32+')
    imp_rva = struct.unpack_from('<I', data, opt + 112 + 8)[0]
    secs = []
    so = opt + optsz
    for _ in range(nsec):
        vs, va, rs, rp = struct.unpack_from('<IIII', data, so + 8)
        secs.append((va, max(vs, rs), rp))
        so += 40

    def off(rva):
        for va, sz, rp in secs:
            if va <= rva < va + sz:
                return rva - va + rp
        raise ValueError('rva %x outside the sections' % rva)

    def cstr(o):
        return data[o:data.index(b'\0', o)].decode('ascii', 'replace')
    out = {}
    if imp_rva == 0:
        return out
    d = off(imp_rva)
    while True:
        oft, _ts, _fc, name, ft = struct.unpack_from('<IIIII', data, d)
        if name == 0:
            break
        fns = []
        t = off(oft or ft)
        while True:
            v = struct.unpack_from('<Q', data, t)[0]
            if v == 0:
                break
            fns.append('#%d' % (v & 0xFFFF) if v >> 63 else cstr(off(v & 0x7FFFFFFF) + 2))
            t += 8
        out[cstr(off(name))] = fns
        d += 20
    return out


def import_violations(imports):
    bad = []
    for dll, fns in imports.items():
        if dll.lower() == 'ntdll.dll':
            bad.append('%s (any ntdll import: host-kernel pass-through)' % dll)
        for f in fns:
            if f.lower() in DENY_IMPORTS:
                bad.append('%s!%s' % (dll, f))
            elif re.match(r'(Nt|Zw)[A-Z]', f):
                bad.append('%s!%s (Nt* / Zw* import)' % (dll, f))
    return bad

# ----------------------------------------------------------------------------------------------------------- controls

MUST_HIT = [
    ('exec_page_protection', 'void* p = VirtualQueryish( PAGE_EXECUTE_READWRITE );'),
    ('exec_page_protection', 'DWORD prot = PAGE_EXECUTE_READ; flags |= PAGE_EXECUTE;'),
    ('exec_heap', 'HeapCreate( HEAP_CREATE_ENABLE_EXECUTE, 0, 0 );'),
    ('exec_mapping', 'mmap( 0, n, PROT_READ | PROT_EXEC, f, -1, 0 );'),
    ('host_page_api', 'auto b = VirtualAlloc( nullptr, 4096, MEM_COMMIT, flags );'),
    ('host_page_api', 'VirtualProtect( buf, n, old, &old );'),
    ('fnptr_cast', '( ( void( __stdcall* )( emulator* ) )shellcode.data() )( this );'),
    ('fnptr_cast', 'reinterpret_cast<int(*)(int)>( guest_buffer )( 1 );'),
    ('inline_asm', '__asm { int 3 }'),
    ('host_loader', 'auto h = LoadLibraryA( name );'),
    ('host_loader', 'auto f = GetProcAddress( GetModuleHandleA( name ), sym );'),
    ('host_process', 'CreateProcessW( nullptr, cmd, nullptr, nullptr, 0, 0, nullptr, nullptr, &si, &pi );'),
    ('host_process', 'system( cmd );'),
    ('nt_call', 'status = NtQuerySystemInformation( cls, buf, len, &ret );'),
    ('nt_call', 'ZwOpenSection( &h, access, &oa );'),
    ('device_io', 'DeviceIoControl( h, code, in, n, out, m, &r, nullptr );'),
    ('host_teb_peb', 'auto teb = NtCurrentTeb();'),
    ('host_teb_peb', 'auto peb = __readgsqword( 0x60 );'),
    ('host_kuser', 'memcpy( page, ( const void* )0x7FFE0000, 0x1000 );'),
    ('host_kuser', 'auto k = reinterpret_cast<const uint8_t*>( 0x7ffe0000 );'),
    ('host_time', 'Sleep( 10 );'),
    ('host_time', 'QueryPerformanceCounter( &t );'),
    ('host_time', 'auto t = __rdtsc();'),
    ('host_time_std', 'auto t0 = std::chrono::steady_clock::now();'),
    ('host_time_std', 'srand( time( NULL ) );'),
    ('host_cpuid', '__cpuidex( regs, 7, 0 );'),
    ('host_rng', '_rdrand64_step( &v );'),
    ('opens_file', 'FILE* f = fopen( path, "rb" );'),
    ('opens_file', 'std::ifstream in( path );'),
    ('opens_file', 'HANDLE h = CreateFileW( p, 0, 0, 0, 0, 0, 0 );'),
    ('ntdll_pragma', '#pragma comment( lib, "ntdll.lib" )'),
]
MUST_PASS = [
    '// VirtualAlloc( PAGE_EXECUTE_READWRITE ) and NtQuerySystemInformation( ... ) named in a comment',
    '/* LoadLibraryA( "x" ); CreateProcessW( ... ); __cpuid( r, 0 ); */',
    'std::cout << "never call NtQuerySystemInformation( host ) or Sleep( 1 )";',
    'const char c = \'(\'; cfg.system( 1 ); emu::nt::query_system_information( ctx );',
    'struct test_case { const char* name; void ( *fn )(); };',
    'uint64_t kuser_guest_va = 0x7FFE0000; uint64_t n = 1\'000\'000;',
    'auto r = R"(VirtualAlloc( PAGE_EXECUTE_READ ))";',
    'auto regs = emu::host_guard::committed_regions(); VirtualQuery( a, &mbi, sizeof( mbi ) );',
    'return base == PAGE_EXECUTE || base==PAGE_EXECUTE_READ || PAGE_EXECUTE_WRITECOPY != base; switch ( p ) { case PAGE_EXECUTE: break; }',
]
BAD_PROJECT = ('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemGroup>'
               '<ClCompile Include="..\\..\\NoVmp\\emulator\\emulator.cpp" /><MASM Include="stub.asm" /></ItemGroup></Project>')


def run_controls():
    fails = []
    for rid, text in MUST_HIT:
        got = [h[0] for h in scan_text(text, EMU_GROUPS)]
        if rid not in got:
            fails.append('rule %s did not fire on: %s' % (rid, text))
    for text in MUST_PASS:
        got = scan_text(text, EMU_GROUPS)
        if got:
            fails.append('false hit %s on: %s' % ([g[0] for g in got], text))
    # U1979: the audited reader may hold std::ifstream only; a write / another open there still fires
    if scan_text('std::ifstream in( p, std::ios::binary );', EMU_GROUPS, reader=True):
        fails.append('the audited reader\'s std::ifstream was not exempt')
    for bad in ('std::ofstream out( p );', 'std::fstream io( p );', 'FILE* f = fopen( p, "rb" );', 'std::filesystem::remove( p );'):
        if 'opens_file' not in [h[0] for h in scan_text(bad, EMU_GROUPS, reader=True)]:
            fails.append('file access in the audited reader not refused: ' + bad)
    # U2020: the audited writer may hold std::ofstream only; a read / another open there still fires, and elsewhere std::ofstream fires
    if scan_text('std::ofstream out;', EMU_GROUPS, writer=True):
        fails.append('the audited writer\'s std::ofstream was not exempt')
    if 'opens_file' not in [h[0] for h in scan_text('std::ofstream out;', EMU_GROUPS)]:
        fails.append('std::ofstream outside the audited writer not refused')
    for bad in ('std::ifstream in( p );', 'std::fstream io( p );', 'FILE* f = fopen( p, "wb" );', 'std::filesystem::remove( p );'):
        if 'opens_file' not in [h[0] for h in scan_text(bad, EMU_GROUPS, writer=True)]:
            fails.append('file access in the audited writer not refused: ' + bad)
    _root, items = project_items(None, BAD_PROJECT)
    if not any('novmp' in i.lower() for i in items.get('ClCompile', [])) or 'MASM' not in items:
        fails.append('synthetic bad project items not seen')
    if not import_violations({'ntdll.dll': ['NtQuerySystemInformation'], 'KERNEL32.dll': ['CreateProcessW', 'GetLastError']}):
        fails.append('synthetic ntdll / CreateProcessW import not refused')
    if import_violations({'KERNEL32.dll': ['VirtualQuery', 'LoadLibraryExW', 'GetLastError']}):
        fails.append('a clean import set was refused')
    # U1989 (S5): a test PE that references a project / writes elsewhere, and an Emulator project that references a test PE
    bad_pe = ('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><OutDir>..\\bin\\</OutDir></PropertyGroup>'
              '<ItemGroup><ProjectReference Include="..\\..\\..\\Emulator.vcxproj" /></ItemGroup></Project>')
    if len(test_pe_problems('t9_bad', bad_pe, ['..\\..\\tests\\pe\\t9_bad\\t9_bad.vcxproj'])) != 3:
        fails.append('S5 synthetic bad test-PE project not refused three ways')
    good_pe = ('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><OutDir>$(X)..\\build\\tests\\test_pe\\</OutDir>'
               '</PropertyGroup></Project>')
    if test_pe_problems('t9_good', good_pe, ['..\\msvc\\unicorn.vcxproj']):
        fails.append('S5 a clean test-PE project was refused')
    return fails

# ----------------------------------------------------------------------------------------------------------- checks

def check_projects(root, report):
    """S3; returns the emulator code files (absolute)."""
    fails = []
    sln_path = os.path.join(root, 'NoVmp.sln')
    sln = open(sln_path, encoding='utf-8-sig').read()
    projs = {}
    folders = {}
    for m in re.finditer(r'Project\("\{([0-9A-F-]+)\}"\) = "([^"]+)", "([^"]+)", "(\{[0-9A-F-]+\})"', sln, re.I):
        kind, name, path, guid = m.groups()
        if kind.upper() == '2150E333-8FDC-42A3-9474-1A3956D46DE8':
            folders[guid.upper()] = name
        else:
            projs[path.replace('\\', '/').lower()] = (name, guid.upper())
    nested = dict((a.upper(), b.upper()) for a, b in re.findall(r'(\{[0-9A-F-]+\}) = (\{[0-9A-F-]+\})', sln.split('NestedProjects', 1)[-1], re.I))
    files = []
    for name, rel in EMU_PROJECTS:
        p = os.path.join(root, rel.replace('/', os.sep))
        ent = projs.get(rel.lower())
        if not ent:
            fails.append('S3 %s (%s) is not in NoVmp.sln' % (name, rel)); continue
        if folders.get(nested.get(ent[1], ''), '') != 'Emulator':
            fails.append('S3 %s is not nested in the solution folder "Emulator"' % name)
        if not os.path.isfile(p):
            fails.append('S3 %s: %s missing on disk' % (name, rel)); continue
        xml = open(p, encoding='utf-8-sig').read()
        base = os.path.dirname(p)
        prj_root, items = project_items(p)
        emu_dir = os.path.join(root, 'Emulator') + os.sep
        for tag, incs in items.items():
            for inc in incs:
                ap = norm(root, base, inc)
                if not ap.startswith(emu_dir):
                    fails.append('S3 %s lists %s outside Emulator/: %s' % (name, tag, relp(root, ap)))
                if tag in ('MASM', 'CustomBuild') or inc.lower().endswith('.asm'):
                    fails.append('S3 %s has a %s item (no host syscall stubs): %s' % (name, tag, inc))
                if tag in ('ClCompile', 'ClInclude') and inc.lower().endswith(C_EXT):
                    files.append(ap)
        for el in prj_root.iter(MSB + 'ProjectReference'):
            if 'novmp.vcxproj' in el.get('Include', '').lower():
                fails.append('S3 %s references NoVmp.vcxproj' % name)
        if re.search(r'ntdll(\.lib)?', ' '.join(e.text or '' for e in prj_root.iter(MSB + 'AdditionalDependencies')), re.I):
            fails.append('S3 %s links ntdll' % name)
        rel_cfg = [g for g in prj_root.iter(MSB + 'ItemDefinitionGroup') if "'Release'" in (g.get('Condition') or '')]
        rt = [e.text for g in rel_cfg for e in g.iter(MSB + 'RuntimeLibrary')]
        if rt != ['MultiThreaded']:
            fails.append('S3 %s: Release RuntimeLibrary %s (want MultiThreaded = /MT)' % (name, rt))
        for tag, want in (('PlatformToolset', 'v143'), ('LanguageStandard', 'stdcpplatest'), ('ConformanceMode', 'true')):
            vals = set(e.text for e in prj_root.iter(MSB + tag))
            if vals != {want}:
                fails.append('S3 %s: %s %s (want %s)' % (name, tag, sorted(vals), want))
        ctype = set(e.text for e in prj_root.iter(MSB + 'ConfigurationType'))
        if name == 'Emulator':
            if ctype != {'StaticLibrary'}:
                fails.append('S3 Emulator.vcxproj ConfigurationType %s (want StaticLibrary)' % sorted(ctype))
            refs = ' '.join(el.findtext(MSB + 'Project') or '' for el in prj_root.iter(MSB + 'ProjectReference')).upper()
            for dep, guid in DEP_GUIDS.items():
                if guid not in refs:
                    fails.append('S3 Emulator.vcxproj does not reference %s' % dep)
            if 'linux-pe' not in xml:
                fails.append('S3 Emulator.vcxproj does not include linux-pe')
        elif ctype != {'Application'}:
            fails.append('S3 %s ConfigurationType %s (want Application)' % (name, sorted(ctype)))
        report.append('S3 %-13s %s: %d C/C++ items, in solution folder "%s"' % (name, rel, sum(1 for t, i in items.items() for _ in i if t in ('ClCompile', 'ClInclude')),
                                                                         folders.get(nested.get(ent[1], ''), '-')))
    listed = set(f.lower() for f in files)
    for sub in ('include', 'src'):
        d = os.path.join(root, 'Emulator', sub)
        for dp, _dn, fn in os.walk(d):
            for f in fn:
                ap = os.path.join(dp, f)
                if f.lower().endswith(C_EXT) and ap.lower() not in listed:
                    fails.append('S3 %s is under Emulator/%s but in no Emulator project' % (relp(root, ap), sub))
                    files.append(ap)
    return sorted(set(files)), fails


TEST_PE_DIR = 'emulator/tests/pe/'


def test_pe_problems(name, xml_text, emu_refs):
    """S5 for one test-PE project (its XML text); emu_refs = the ProjectReference includes of the Emulator projects"""
    fails = []
    prj, _items = project_items(None, xml_text)
    refs = [el.get('Include', '') for el in prj.iter(MSB + 'ProjectReference')]
    if refs:
        fails.append('S5 test PE %s references projects %s (guest code links nothing of the emulator)' % (name, refs))
    outs = [e.text or '' for e in prj.iter(MSB + 'OutDir')]
    if not outs or any('tests\\test_pe\\' not in o.replace('/', '\\') for o in outs):
        fails.append('S5 test PE %s: OutDir %s is not build\\...\\tests\\test_pe\\' % (name, outs))
    for r in emu_refs:
        if TEST_PE_DIR.replace('/', '\\') in r.replace('/', '\\').lower() or 'tests\\pe\\' in r.replace('/', '\\').lower() or r.lower().endswith(name.lower() + '.vcxproj'):
            fails.append('S5 an Emulator project references the test PE %s (%s)' % (name, r))
    return fails


def check_test_pes(root, report):
    sln = open(os.path.join(root, 'NoVmp.sln'), encoding='utf-8-sig').read()
    pes = [(n, p) for n, p in re.findall(r'Project\("\{[0-9A-F-]+\}"\) = "([^"]+)", "([^"]+\.vcxproj)"', sln, re.I) if p.replace('\\', '/').lower().startswith(TEST_PE_DIR)]
    emu_refs = []
    for _n, rel in EMU_PROJECTS:
        p = os.path.join(root, rel.replace('/', os.sep))
        if os.path.isfile(p):
            emu_refs += [el.get('Include', '') for el in project_items(p)[0].iter(MSB + 'ProjectReference')]
    fails = []
    for n, rel in pes:
        p = os.path.join(root, rel.replace('\\', os.sep))
        if not os.path.isfile(p):
            fails.append('S5 test PE %s: %s missing' % (n, rel)); continue
        fails += test_pe_problems(n, open(p, encoding='utf-8-sig').read(), emu_refs)
    report.append('S5 %d test-PE project(s) (guest code): %s' % (len(pes), ', '.join(n for n, _ in pes) or '-'))
    return fails


def check_emulator_sources(root, files, report):
    fails, found = [], {}
    if not files:
        return ['S1 no emulator code found (the Emulator projects of milestone M1 are missing) - the check cannot pass vacuously'], found
    for ap in files:
        if not os.path.isfile(ap):
            fails.append('S1 listed file missing: %s' % relp(root, ap)); continue
        text = open(ap, encoding='utf-8', errors='replace').read()
        for rid, line, snip in scan_text(text, EMU_GROUPS, reader=relp(root, ap) == AUDITED_READER, writer=relp(root, ap) == AUDITED_WRITER):
            found.setdefault((relp(root, ap), rid), []).append((line, snip))
    report.append('S1 scanned %d emulator files (%s), rule groups %s' % (len(files), ', '.join(relp(root, f) for f in files), ' '.join(EMU_GROUPS)))
    return fails, found


def check_novmp(root, report):
    found = {}
    p = os.path.join(root, 'NoVmp', 'NoVmp.vcxproj')
    _r, items = project_items(p)
    base = os.path.dirname(p)
    srcs = [norm(root, base, i) for t in ('ClCompile', 'ClInclude') for i in items.get(t, []) if i.lower().endswith(C_EXT)]
    for ap in srcs:
        text = open(ap, encoding='utf-8', errors='replace').read()
        for rid, line, snip in scan_text(text, NOVMP_GROUPS):
            found.setdefault((relp(root, ap), rid), []).append((line, snip))
    compiled = [i for i in items.get('ClCompile', []) if i.replace('\\', '/').lower() in NATIVE_EXECUTOR]
    for c in compiled:
        found.setdefault(('NoVmp/NoVmp.vcxproj', 'compiled_native_executor'), []).append((0, 'ClCompile ' + c))
    report.append('S2 scanned %d NoVmp project files, rule groups %s; ClCompile native executor items: %d' % (len(srcs), ' '.join(NOVMP_GROUPS), len(compiled)))
    return found


def load_allowlist(root):
    p = os.path.join(root, ALLOWLIST.replace('/', os.sep))
    rows = {}
    if not os.path.isfile(p):
        return None
    for n, l in enumerate(open(p, encoding='utf-8'), 1):
        l = l.rstrip('\r\n')
        if not l or l.startswith('#'):
            continue
        c = l.split('\t')
        if len(c) != 6 or not c[3].isdigit() or not c[4].strip() or not c[5].strip():
            raise ValueError('%s:%d: want 6 tab-separated columns scope / file / rule / count / until / reason' % (ALLOWLIST, n))
        rows[(c[1], c[2])] = (int(c[3]), c[0], c[4], c[5])
    return rows


def check_imports(build, report):
    fails = []
    for exe in EMU_EXES:
        p = os.path.join(build, exe + '.exe')
        if not os.path.isfile(p):
            fails.append('S4 %s missing (build.cmd first)' % p); continue
        imps = pe_imports(open(p, 'rb').read())
        bad = import_violations(imps)
        report.append('S4 %s.exe imports: %s; refused: %d' % (exe, ', '.join('%s %d' % (d, len(f)) for d, f in sorted(imps.items())), len(bad)))
        fails += ['S4 %s.exe imports %s' % (exe, b) for b in bad]
    return fails


def check_runtime(build, report):
    exe = os.path.join(build, 'emu-selftest.exe')
    if not os.path.isfile(exe):
        return ['R  %s missing (build.cmd first)' % exe]
    r = subprocess.run([exe, 'no_native_runtime'], capture_output=True, text=True, timeout=300)
    out = r.stdout.splitlines()
    for l in out:
        report.append('R  | ' + l)
    if r.returncode != 0 or not any(re.match(r'^Test no_native_runtime\.\.\. +\[ OK \]$', l) for l in out) or \
            'SUCCESS: All unit tests have passed.' not in out:
        return ['R  emu-selftest no_native_runtime failed (exit %d)' % r.returncode]
    return []


def main(argv):
    args = [a for a in argv if not a.startswith('--')]
    if '--build' in argv:
        i = argv.index('--build')
        if i + 1 >= len(argv):
            print(__doc__.strip().splitlines()[-1]); return 2
        build = argv[i + 1]
        args = [a for a in args if a != build]
    else:
        build = None
    if len(args) != 1 or any(a.startswith('--') and a not in ('--build', '--no-allowlist') for a in argv):
        print(__doc__.strip().splitlines()[-1]); return 2
    root = os.path.abspath(args[0])
    no_allow = '--no-allowlist' in argv
    report, fails = [], []

    cf = run_controls()
    print('negative controls: %d rule samples, %d clean samples, project + import controls: %d failed' % (len(MUST_HIT), len(MUST_PASS), len(cf)))
    fails += ['control: ' + f for f in cf]

    files, f3 = check_projects(root, report)
    f1, emu_found = check_emulator_sources(root, files, report)
    nv_found = check_novmp(root, report)
    fails += f3 + f1 + check_test_pes(root, report)

    allow = None if no_allow else load_allowlist(root)
    if not no_allow and allow is None:
        report.append('S2 allow-list %s missing' % ALLOWLIST)
    allow = allow or {}
    pending = []
    all_found = dict(emu_found)
    for k, v in nv_found.items():
        all_found[k] = v
    for (f, rid), hits in sorted(all_found.items()):
        want = allow.get((f, rid))
        detail = '; '.join('%s%s' % ('line %d: ' % ln if ln else '', s) for ln, s in hits[:4])
        if want and want[0] == len(hits):
            pending.append('%s %s x%d - allow-listed until %s (%s)' % (f, rid, len(hits), want[2], want[3]))
        elif want:
            fails.append('%s %s: %d finding(s), allow-list says %d: %s' % (f, rid, len(hits), want[0], detail))
        else:
            scope = 'S2 NoVmp' if (f, rid) in nv_found else 'S1 emulator'
            fails.append('%s %s %s x%d: %s' % (scope, f, rid, len(hits), detail))
    for (f, rid), want in sorted(allow.items()):
        if (f, rid) not in all_found:
            fails.append('allow-list row %s %s (until %s) no longer found - remove it' % (f, rid, want[2]))

    if build:
        fails += check_imports(build, report)
        fails += check_runtime(build, report)
    else:
        report.append('S4 / R skipped (no --build): the suite always passes --build')

    for l in report:
        print(l)
    for p in pending:
        print('PENDING ' + p)
    for f in fails:
        print('FAIL ' + f)
    if fails:
        print('no_native_check: FAIL (%d problem(s)%s)' % (len(fails), ', allow-list ignored (--no-allowlist)' if no_allow else ''))
        return 1
    print('no_native_check: PASS (%d pending allow-listed finding(s): the NoVmp native executor stays compiled in behind N6 until M8 / 3.C)' % len(pending)
          if pending else 'no_native_check: PASS (no pending findings)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
