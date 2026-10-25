#!/usr/bin/env python3
"""check_win_layout.py - suite win_env_layout (plan 3.M4, ledger U1987): every PEB / TEB / KUSER_SHARED_DATA offset, GDT
selector and the KUSER address of the emulated Windows environment (Emulator/include/emu/win_env.hpp, namespace
emu::win::layout + guest_kuser_va) against the documented layouts it cites - read from the headers themselves, not copied:
  ksamd64.inc  Windows SDK / WDK 10.0.26100.0 Include\\shared\\ksamd64.inc "NAME equ VALUEH" lines: Te* (TEB), Pe* (PEB),
               Us* (KUSER), KGDT64_* (GDT selectors), RPL_MASK, MM_SHARED_USER_DATA_VA
  ntddk.h      WDK 10.0.26100.0 Include\\km\\ntddk.h "C_ASSERT(FIELD_OFFSET(KUSER_SHARED_DATA, F) == V);" lines
  ntdll.h      the Intel Pin kit's intel64 pinrt\\include\\adaptor\\windows\\ntdll.h "typedef struct _PEB { ... } PEB;"
               (an independent layout, notes\\sdeida\\06_pinrt_runtime.md section 3) - its field offsets COMPUTED here with
               the x64 natural-alignment rules (BOOLEAN / BYTE 1, WORD 2, ULONG 4, pointers / HANDLE / SIZE_T / LARGE_INTEGER 8,
               arrays of them), for the PEB fields the SDK headers do not name (ImageBaseAddress, ProcessHeap, OS version)
  (um\\winnt.h NT_TIB and um\\winternl.h PEB / TEB are checked at compile time: static_asserts in Emulator/src/win_env.cpp)
The SDK is found under the registry's KitsRoot10 (else C:\\Program Files (x86)\\Windows Kits\\10); the Pin kit under the
sources root (NOVMP_SOURCES_ROOT, default E:\\sources: Emulator\\tools\\docroot.py). A missing header is a FAIL (no fallback).
In-memory negative controls: a wrong offset, a missing source symbol and a wrong computed Pin offset must be reported.
Reads only. Exit 0 PASS, 1 FAIL, 2 usage.
usage: python -I check_win_layout.py REPO_ROOT
"""
import importlib.util
import os
import re
import sys
import winreg

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
SDK_VERSION = '10.0.26100.0'
PIN_NTDLL = os.path.join('pin-sources', 'pin-external-4.4-99977-g54fbb8814-clang-windows', 'pin-external-4.4-99977-g54fbb8814-clang-windows',
                         'intel64', 'pinrt', 'include', 'adaptor', 'windows', 'ntdll.h')

# our constant -> (source, symbol); the expected value comes from that source
MAP = {
    'teb_stack_base': ('ksamd64', 'TeStackBase'), 'teb_stack_limit': ('ksamd64', 'TeStackLimit'), 'teb_self': ('ksamd64', 'TeSelf'),
    'teb_client_id': ('ksamd64', 'TeClientId'), 'teb_tls_pointer': ('ksamd64', 'TeThreadLocalStoragePointer'), 'teb_peb': ('ksamd64', 'TePeb'),
    'teb_last_error': ('ksamd64', 'TeLastErrorValue'), 'teb_deallocation_stack': ('ksamd64', 'TeDeallocationStack'), 'teb_tls_slots': ('ksamd64', 'TeTlsSlots'),
    'teb_exception_list': ('ksamd64', 'TeCmTeb'),   # the TEB's first field (NT_TIB.ExceptionList; also static_asserted vs winnt.h)
    'peb_being_debugged': ('ksamd64', 'PeBeingDebugged'), 'peb_process_parameters': ('ksamd64', 'PeProcessParameters'),
    'peb_ldr': ('pin', 'Ldr'), 'peb_image_base_address': ('pin', 'ImageBaseAddress'), 'peb_process_heap': ('pin', 'ProcessHeap'),
    'peb_os_major_version': ('pin', 'OSMajorVersion'), 'peb_os_minor_version': ('pin', 'OSMinorVersion'), 'peb_os_build_number': ('pin', 'OSBuildNumber'),
    'peb_os_csd_version': ('pin', 'OSCSDVersion'), 'peb_os_platform_id': ('pin', 'OSPlatformId'),
    'kuser_tick_count_multiplier': ('ntddk', 'TickCountMultiplier'), 'kuser_interrupt_time': ('ntddk', 'InterruptTime'), 'kuser_system_time': ('ntddk', 'SystemTime'),
    'kuser_time_zone_bias': ('ntddk', 'TimeZoneBias'), 'kuser_image_number_low': ('ntddk', 'ImageNumberLow'), 'kuser_image_number_high': ('ntddk', 'ImageNumberHigh'),
    'kuser_system_root': ('ntddk', 'NtSystemRoot'), 'kuser_build_number': ('ntddk', 'NtBuildNumber'), 'kuser_product_type': ('ntddk', 'NtProductType'),
    'kuser_product_type_is_valid': ('ntddk', 'ProductTypeIsValid'), 'kuser_native_processor_architecture': ('ntddk', 'NativeProcessorArchitecture'),
    'kuser_major_version': ('ntddk', 'NtMajorVersion'), 'kuser_minor_version': ('ntddk', 'NtMinorVersion'), 'kuser_processor_features': ('ntddk', 'ProcessorFeatures'),
    'kuser_kd_debugger_enabled': ('ntddk', 'KdDebuggerEnabled'), 'kuser_number_of_physical_pages': ('ntddk', 'NumberOfPhysicalPages'),
    'kuser_safe_boot_mode': ('ntddk', 'SafeBootMode'), 'kuser_system_call': ('ntddk', 'SystemCall'), 'kuser_active_processor_count': ('ntddk', 'ActiveProcessorCount'),
    'kuser_active_group_count': ('ntddk', 'ActiveGroupCount'),
    'gdt_r0_code': ('ksamd64', 'KGDT64_R0_CODE'), 'gdt_r0_data': ('ksamd64', 'KGDT64_R0_DATA'), 'gdt_r3_cmcode': ('ksamd64', 'KGDT64_R3_CMCODE'),
    'gdt_r3_data': ('ksamd64', 'KGDT64_R3_DATA'), 'gdt_r3_code': ('ksamd64', 'KGDT64_R3_CODE'), 'gdt_sys_tss': ('ksamd64', 'KGDT64_SYS_TSS'),
    'gdt_r3_cmteb': ('ksamd64', 'KGDT64_R3_CMTEB'), 'rpl3': ('ksamd64', 'RPL_MASK'),
    'guest_kuser_va': ('ksamd64', 'MM_SHARED_USER_DATA_VA'),
}
# also cross-checked: the KUSER offsets that ksamd64.inc names too (Us*), so the two WDK files agree with each other
KSAMD64_KUSER = {'kuser_tick_count_multiplier': 'UsTickCountMultiplier', 'kuser_interrupt_time': 'UsInterruptTime', 'kuser_system_time': 'UsSystemTime',
                 'kuser_time_zone_bias': 'UsTimeZoneBias', 'kuser_image_number_low': 'UsImageNumberLow', 'kuser_image_number_high': 'UsImageNumberHigh',
                 'kuser_system_root': 'UsNtSystemRoot', 'kuser_build_number': 'UsNtBuildNumber', 'kuser_product_type': 'UsNtProductType',
                 'kuser_product_type_is_valid': 'UsProductTypeIsValid', 'kuser_major_version': 'UsNtMajorVersion', 'kuser_minor_version': 'UsNtMinorVersion',
                 'kuser_processor_features': 'UsProcessorFeatures', 'kuser_kd_debugger_enabled': 'UsKdDebuggerEnabled',
                 'kuser_number_of_physical_pages': 'UsNumberOfPhysicalPages', 'kuser_safe_boot_mode': 'UsSafeBootMode'}

TYPE_SIZE = {'BOOLEAN': 1, 'BYTE': 1, 'UCHAR': 1, 'CHAR': 1, 'WORD': 2, 'USHORT': 2, 'ULONG': 4, 'LONG': 4, 'DWORD': 4,
             'LARGE_INTEGER': 8, 'SIZE_T': 8, 'HANDLE': 8, 'PVOID': 8}


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def ours(root):
    t = open(os.path.join(root, 'Emulator', 'include', 'emu', 'win_env.hpp'), encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'\b([a-z][a-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)\b', t):
        if m.group(1) in MAP or m.group(1) == 'kuser_system_root_chars':
            out[m.group(1)] = int(m.group(2), 0)
    return out


def sdk_include():
    roots = []
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots', 0, winreg.KEY_READ | winreg.KEY_WOW64_32KEY) as k:
            roots.append(winreg.QueryValueEx(k, 'KitsRoot10')[0])
    except OSError:
        pass
    roots.append(r'C:\Program Files (x86)\Windows Kits\10')
    for r in roots:
        p = os.path.join(r, 'Include', SDK_VERSION)
        if os.path.isdir(p):
            return p
    return None


def ksamd64(path):
    out = {}
    for line in open(path, encoding='latin-1'):
        m = re.match(r'^(\w+) equ 0*([0-9A-F]+)H\s*$', line.strip())
        if m:
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def ntddk(path):
    out = {}
    for line in open(path, encoding='latin-1'):
        m = re.match(r'^\s*C_ASSERT\(FIELD_OFFSET\(KUSER_SHARED_DATA,\s*(\w+)\)\s*==\s*(0x[0-9A-Fa-f]+)\);', line)
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def pin_peb(path):
    """field -> offset of the intel64 Pin kit's PEB, computed with x64 natural alignment"""
    t = open(path, encoding='latin-1').read()
    m = re.search(r'typedef struct _PEB\s*\{(.*?)\}\s*PEB;', t, re.S)
    if not m:
        raise ValueError('no "typedef struct _PEB { ... } PEB;" in ' + path)
    off, out = 0, {}
    for raw in m.group(1).split(';'):
        decl = re.sub(r'//.*', '', raw).strip()
        if not decl:
            continue
        d = re.match(r'^(?:struct\s+)?(\w+)\s*(\*?)\s*(\w+)\s*(?:\[\s*(\d+)\s*\])?$', ' '.join(decl.split()))
        if not d:
            raise ValueError('PEB field not understood: ' + decl)
        typ, star, name, count = d.group(1), d.group(2), d.group(3), int(d.group(4) or 1)
        size = 8 if star or typ.startswith('P') else TYPE_SIZE.get(typ)
        if size is None:
            raise ValueError('PEB field type %s has no size rule' % typ)
        off = (off + size - 1) // size * size
        out[name] = off
        off += size * count
    return out


def compare(our, src, label_of):
    rows, bad = [], []
    for name, (where, sym) in sorted(MAP.items()):
        if name not in our:
            bad.append('%s: not defined in win_env.hpp' % name)
            continue
        want = src[where].get(sym)
        if want is None:
            bad.append('%s: %s has no %s' % (name, label_of[where], sym))
            continue
        ok = our[name] == want
        rows.append('%-5s %-38s %#7x  %s %s = %#x' % ('ok' if ok else 'DIFF', name, our[name], label_of[where], sym, want))
        if not ok:
            bad.append('%s = %#x, %s %s = %#x' % (name, our[name], label_of[where], sym, want))
    for name, sym in sorted(KSAMD64_KUSER.items()):
        a, b = src['ksamd64'].get(sym), src['ntddk'].get(MAP[name][1])
        if a is None or a != b:
            bad.append('%s: ksamd64.inc %s = %s, ntddk.h %s = %s (the two WDK files disagree)' % (name, sym, a, MAP[name][1], b))
    nsr, mst = src['ntddk'].get('NtSystemRoot'), src['ntddk'].get('MaxStackTraceDepth')
    if nsr is None or mst is None or our.get('kuser_system_root_chars') != (mst - nsr) // 2:
        bad.append('kuser_system_root_chars %s, ntddk.h (MaxStackTraceDepth - NtSystemRoot) / 2 = %s' % (our.get('kuser_system_root_chars'),
                                                                                                    (mst - nsr) // 2 if nsr is not None and mst is not None else '?'))
    return rows, bad


def main(argv):
    if len(argv) != 1:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root = os.path.abspath(argv[0])
    inc = sdk_include()
    if not inc:
        print('check_win_layout: FAIL - Windows SDK / WDK %s include folder not found (KitsRoot10)' % SDK_VERSION)
        return 1
    ks_path = os.path.join(inc, 'shared', 'ksamd64.inc')
    nd_path = os.path.join(inc, 'km', 'ntddk.h')
    pin_path = os.path.join(load('docroot').sources_root(), PIN_NTDLL)
    missing = [p for p in (ks_path, nd_path, pin_path) if not os.path.isfile(p)]
    if missing:
        print('check_win_layout: FAIL - missing %s' % ', '.join(missing))
        return 1
    src = {'ksamd64': ksamd64(ks_path), 'ntddk': ntddk(nd_path), 'pin': pin_peb(pin_path)}
    label = {'ksamd64': 'ksamd64.inc', 'ntddk': 'ntddk.h', 'pin': 'Pin ntdll.h PEB'}
    print('sources: %s (%d equ), %s (%d KUSER C_ASSERTs), %s (%d PEB fields computed)' % (ks_path, len(src['ksamd64']), nd_path, len(src['ntddk']), pin_path, len(src['pin'])))
    our = ours(root)
    rows, bad = compare(our, src, label)
    # negative controls (in memory): a wrong offset, a missing symbol, a wrong computed Pin offset
    ctl = []
    o2 = dict(our); o2['teb_self'] = 0x38
    if not any('teb_self' in b for b in compare(o2, src, label)[1]):
        ctl.append('a wrong TEB offset was not reported')
    s2 = {k: dict(v) for k, v in src.items()}; del s2['ntddk']['KdDebuggerEnabled']
    if not any('KdDebuggerEnabled' in b for b in compare(our, s2, label)[1]):
        ctl.append('a missing KUSER symbol was not reported')
    s3 = {k: dict(v) for k, v in src.items()}; s3['pin']['OSBuildNumber'] += 2
    if not any('peb_os_build_number' in b for b in compare(our, s3, label)[1]):
        ctl.append('a wrong computed Pin offset was not reported')
    for r in rows:
        print(r)
    print('negative controls: 3, failed %d' % len(ctl))
    for b in bad + ['control: ' + c for c in ctl]:
        print('FAIL ' + b)
    if bad or ctl:
        print('check_win_layout: FAIL (%d problem(s))' % (len(bad) + len(ctl)))
        return 1
    print('check_win_layout: PASS (%d offsets / selectors equal their sources; ksamd64.inc and ntddk.h agree on %d KUSER offsets; NtSystemRoot 260 chars)'
          % (len(rows), len(KSAMD64_KUSER)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
