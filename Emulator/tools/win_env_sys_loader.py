#!/usr/bin/env python3
r"""win_env_sys_loader.py - suite win_env_sys_loader (plan 3.M5 part 2, ledger U2039; P39 U2060; P40 U2067; P41 U2086): the opt-in --Sys
--loader start through the VMware-guest environment's real ntdll!LdrInitializeThunk - since P41 to the END of the test PEs: T2 / T3 PASS
under --Sys (the plan 3.M5 done criterion).

P41 (USER DECISION 2026-10-26 "started without a console"): the emulated process is the model of a process started DETACHED (process
parameters ConsoleHandle -1 = RTL_USER_PROC_DETACHED_PROCESS, no standard handles); every system service the real ntdll / kernelbase /
kernel32 then call is answered from phnt prototypes + native measurements of our own test PE tm_nt (Emulator/data/
nt_measured_vmware_guest.txt; P39 / P40 / P41), never forwarded to the host. Checked (the test PEs are read as data by emu-oep, never run
by it; the system DLLs are mapped as data and run only in emulation):
  - the capture holds the loader files and the registry subset with the P39 / P40 / P41 items (capture_sysdlls.py --dll / --reg / --reg-value)
  - T2 --Sys without --loader: refused (exit 3) as in milestone M5
  - T2 and T3 (its service number patched from the capture, as win_env_m5; a copy of its own in test_pe\sysloader\) under --Sys
    --loader: the kernel-side start (NLS / API set views, the CONTEXT, IRETQ to LdrInitializeThunk); EVERY system service answered, in the
    recorded order (EXPECTED below: T2 210, T3 202 services); the guest's exit status 2BH / 2EH (emu-oep exit 0); nothing forwarded
  - the same test PEs under --HLE: the same exit status (the observable result of both modes)
  - the NATIVE REFERENCE (labelled, decision 3.0.7), only when the live ntdll equals the capture: T2 / T3 started natively DETACHED
    (tm_nt run.detached: CreateProcessW( DETACHED_PROCESS )) exit with the same codes as their emulated runs
A changed service sequence fails this suite (a newly answered or changed service must update EXPECTED and the notes). Exit 0 = PASS,
1 = FAIL, 2 = usage.
usage: python -I win_env_sys_loader.py REPO_ROOT TESTS_DIR
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


T1H = load('win_env_t1')
EXPECTED = {
    'T2': ('NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtCreateEvent NtQuerySystemInformation NtQueryInformationProcess '
    'NtQuerySystemInformation NtQueryVirtualMemory NtQueryVirtualMemory NtProtectVirtualMemory NtOpenKey NtQueryValueKey NtClose NtOpenKey NtOpenKey '
    'NtOpenKey NtQueryInformationProcess NtQueryInformationProcess NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken NtOpenKey '
    'NtQueryValueKey NtClose NtQueryInformationProcess NtQuerySystemInformation NtQuerySystemInformation NtAllocateVirtualMemoryEx '
    'NtAllocateVirtualMemoryEx NtAllocateVirtualMemoryEx NtQuerySystemInformation NtAllocateVirtualMemory NtFreeVirtualMemory NtAllocateVirtualMemory '
    'NtQuerySystemInformation NtQuerySystemInformation NtCreateEvent NtCreateWaitCompletionPacket NtQuerySystemInformationEx NtCreateIoCompletion '
    'NtCreateWorkerFactory NtCreateTimer2 NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtCreateTimer2 NtCreateWaitCompletionPacket '
    'NtAssociateWaitCompletionPacket NtSetInformationWorkerFactory NtSetInformationWorkerFactory NtSetInformationWorkerFactory '
    'NtAssociateWaitCompletionPacket NtTraceControl NtTraceControl NtTraceControl NtTraceControl NtFreeVirtualMemory NtProtectVirtualMemory '
    'NtOpenDirectoryObject NtProtectVirtualMemory NtOpenSymbolicLinkObject NtQuerySymbolicLinkObject NtClose NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtCreateEvent NtCreateEvent NtProtectVirtualMemory NtProtectVirtualMemory NtOpenFile NtQueryVolumeInformationFile '
    'NtSetEvent NtAllocateVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtClose NtProtectVirtualMemory NtClose NtProtectVirtualMemory '
    'NtSetInformationProcess NtApphelpCacheControl NtApphelpCacheControl NtSetInformationVirtualMemory NtQuerySystemInformation '
    'NtQuerySystemInformation NtOpenSection NtCreateSection NtConnectPort NtClose NtMapViewOfSection NtClose NtAllocateVirtualMemory '
    'NtInitializeNlsFiles NtSetInformationProcess NtTraceControl NtTraceControl NtQueryInformationToken NtAccessCheck NtAccessCheck NtOpenProcessToken '
    'NtQueryInformationToken NtQueryInformationToken NtQueryInformationToken NtOpenDirectoryObject NtQueryInformationToken NtClose NtCreateMutant '
    'NtWaitForSingleObject NtOpenSemaphore NtCreateSemaphore NtCreateSemaphore NtReleaseMutant NtQueryWnfStateData NtCreateEvent '
    'NtCreateWaitCompletionPacket NtSetWnfProcessNotificationEvent NtAssociateWaitCompletionPacket NtSubscribeWnfStateChange NtQueryWnfStateData '
    'NtSubscribeWnfStateChange NtQueryWnfStateData NtSubscribeWnfStateChange NtQueryWnfStateData NtAllocateVirtualMemory NtSetTimer2 NtOpenKey '
    'NtTraceControl NtTraceControl NtTraceControl NtTraceControl NtSetInformationVirtualMemory NtOpenKey NtTraceControl NtTraceControl '
    'NtQuerySystemInformation NtSetEvent NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtOpenKey NtOpenKey NtOpenKey NtQueryValueKey NtClose '
    'NtQueryInformationToken NtOpenKey NtOpenKey NtQueryValueKey NtClose NtOpenKey NtQueryValueKey NtClose NtQuerySystemInformationEx '
    'NtCreateIoCompletion NtCreateWorkerFactory NtSetInformationWorkerFactory NtCreateTimer2 NtCreateWaitCompletionPacket '
    'NtAssociateWaitCompletionPacket NtCreateTimer2 NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtSetInformationWorkerFactory '
    'NtSetInformationWorkerFactory NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtApphelpCacheControl NtSetInformationVirtualMemory NtSetEvent NtSetEvent NtSetInformationWorkerFactory NtSetEvent NtTestAlert NtContinue '
    'NtTerminateProcess NtClose NtClose NtClose NtClose NtQueryWnfStateData NtTerminateProcess ').split(),
    'T3': ('NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtCreateEvent NtQuerySystemInformation NtQueryInformationProcess '
    'NtQuerySystemInformation NtQueryVirtualMemory NtQueryVirtualMemory NtProtectVirtualMemory NtOpenKey NtQueryValueKey NtClose NtOpenKey NtOpenKey '
    'NtOpenKey NtQueryInformationProcess NtQueryInformationProcess NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken NtOpenKey '
    'NtQueryValueKey NtClose NtQueryInformationProcess NtQuerySystemInformation NtQuerySystemInformation NtAllocateVirtualMemoryEx '
    'NtAllocateVirtualMemoryEx NtAllocateVirtualMemoryEx NtQuerySystemInformation NtAllocateVirtualMemory NtFreeVirtualMemory NtAllocateVirtualMemory '
    'NtQuerySystemInformation NtQuerySystemInformation NtCreateEvent NtCreateWaitCompletionPacket NtQuerySystemInformationEx NtCreateIoCompletion '
    'NtCreateWorkerFactory NtCreateTimer2 NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtCreateTimer2 NtCreateWaitCompletionPacket '
    'NtAssociateWaitCompletionPacket NtSetInformationWorkerFactory NtSetInformationWorkerFactory NtSetInformationWorkerFactory '
    'NtAssociateWaitCompletionPacket NtTraceControl NtTraceControl NtTraceControl NtTraceControl NtFreeVirtualMemory NtProtectVirtualMemory '
    'NtOpenDirectoryObject NtProtectVirtualMemory NtOpenSymbolicLinkObject NtQuerySymbolicLinkObject NtClose NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtCreateEvent NtCreateEvent NtProtectVirtualMemory NtProtectVirtualMemory NtOpenFile NtQueryVolumeInformationFile '
    'NtSetEvent NtAllocateVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtClose NtProtectVirtualMemory NtClose NtProtectVirtualMemory '
    'NtSetInformationProcess NtApphelpCacheControl NtApphelpCacheControl NtSetInformationVirtualMemory NtQuerySystemInformation '
    'NtQuerySystemInformation NtOpenSection NtCreateSection NtConnectPort NtClose NtMapViewOfSection NtClose NtAllocateVirtualMemory '
    'NtInitializeNlsFiles NtSetInformationProcess NtTraceControl NtTraceControl NtQueryInformationToken NtAccessCheck NtAccessCheck NtOpenProcessToken '
    'NtQueryInformationToken NtQueryInformationToken NtQueryInformationToken NtOpenDirectoryObject NtQueryInformationToken NtClose NtCreateMutant '
    'NtWaitForSingleObject NtOpenSemaphore NtCreateSemaphore NtCreateSemaphore NtReleaseMutant NtQueryWnfStateData NtCreateEvent '
    'NtCreateWaitCompletionPacket NtSetWnfProcessNotificationEvent NtAssociateWaitCompletionPacket NtSubscribeWnfStateChange NtQueryWnfStateData '
    'NtSubscribeWnfStateChange NtQueryWnfStateData NtSubscribeWnfStateChange NtAllocateVirtualMemory NtQueryWnfStateData NtSetTimer2 NtOpenKey '
    'NtTraceControl NtTraceControl NtTraceControl NtTraceControl NtSetInformationVirtualMemory NtOpenKey NtTraceControl NtTraceControl '
    'NtQuerySystemInformation NtSetEvent NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtOpenKey NtOpenKey NtOpenKey NtQueryValueKey NtClose '
    'NtQueryInformationToken NtOpenKey NtOpenKey NtQueryValueKey NtClose NtOpenKey NtQueryValueKey NtClose NtQuerySystemInformationEx '
    'NtCreateIoCompletion NtCreateWorkerFactory NtSetInformationWorkerFactory NtCreateTimer2 NtCreateWaitCompletionPacket '
    'NtAssociateWaitCompletionPacket NtCreateTimer2 NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtSetInformationWorkerFactory '
    'NtSetInformationWorkerFactory NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken NtApphelpCacheControl NtSetInformationVirtualMemory '
    'NtSetEvent NtSetEvent NtSetInformationWorkerFactory NtSetEvent NtTestAlert NtContinue NtTerminateProcess ').split(),
}
STATUS = {'T2': 0x2B, 'T3': 0x2E}
# P41: the registry items the start of a process without a console asks (capture_sysdlls.py --reg / --reg-value; local, never committed)
P41_REGISTRY = [
    r'key	absent	\registry\machine\system\currentcontrolset\control\stateseparation\redirectionmap\keys',
    r'key	absent	\registry\machine\system\currentcontrolset\control\safeboot\option',
    r'key	absent	\registry\machine\system\currentcontrolset\control\srp\gp\dll',
    r'key	present	\registry\machine\software\policies\microsoft\windows\safer\codeidentifiers',
    r'key	present	\registry\machine\system\currentcontrolset\control\filesystem',
]


def say(msg):
    print('[win_env_sys_loader] ' + msg)


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    oep = os.path.join(tests, 'emu-oep.exe')
    pe_dir = os.path.join(tests, 'test_pe')
    tm = os.path.join(pe_dir, 'tm_nt.exe')
    t2, t3 = (os.path.join(pe_dir, n) for n in ('t2_import.exe', 't3_tls.exe'))
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in (oep, t2, t3, tm):
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    cap = T1H.capture_dir(root, 'vmware_guest')
    man = T1H.manifest(cap) if cap else {}
    reg = open(os.path.join(cap, 'registry.txt'), encoding='ascii').read().lower().splitlines() if cap and os.path.isfile(os.path.join(cap, 'registry.txt')) else []
    check(cap and all('file_' + n in man for n in ('kernel32.dll', 'kernelbase.dll', 'apisetschema.dll', 'locale.nls')) and 'nls_case' in man and
          all(x in reg for x in P41_REGISTRY), 'the capture holds the loader files (+ locale.nls, P40) and the registry subset (U2024 / U2034; + the P41 items)')
    # T3's copy with the service number of the capture's ntdll (as win_env_m5.py; a folder of its own: suites run in parallel)
    nums = T1H.stub_numbers(open(os.path.join(cap, 'ntdll.dll'), 'rb').read()) if cap else {}
    img = bytearray(open(t3, 'rb').read())
    at = img.find(b'T3SYSNUM')
    if not check('NtTerminateProcess' in nums and img.count(b'T3SYSNUM') == 1 and struct.unpack_from('<I', img, at + 8)[0] == 0xFFFFFFFF,
                 'T3: the marker once, unpatched; NtTerminateProcess in the capture\'s ntdll'):
        return 1
    struct.pack_into('<I', img, at + 8, nums['NtTerminateProcess'])
    sub = os.path.join(pe_dir, 'sysloader')
    os.makedirs(sub, exist_ok=True)
    t3p = os.path.join(sub, 't3_tls.vmware_guest.exe')
    open(t3p, 'wb').write(bytes(img))
    rc, out = T1H.run([oep, '--guest', '--Sys', t2])
    check(rc == 3 and any(l.startswith('[emu-oep] error: --Sys:') and 'LdrInitializeThunk' in l for l in out), 'T2 --Sys without --loader: refused, exit 3')
    status = {}
    for tag, path in (('T2', t2), ('T3', t3p)):
        rc, out = T1H.run([oep, '--guest', '--Sys', '--loader', '--counters', path])
        obs = T1H.observable(out)
        check(any('env: NLS view at ' in l for l in obs) and any('env: API set schema view at ' in l and 'schema version 6' in l for l in obs) and
              any('env: --Sys loader start: CONTEXT at ' in l and 'RDX = the ntdll base' in l for l in obs) and
              any(l.startswith('[emu-oep] run: IRETQ to ntdll!LdrInitializeThunk') for l in out),
              '%s --Sys --loader: the kernel-side start (NLS / API set views, the CONTEXT, IRETQ to LdrInitializeThunk with RCX / RDX)' % tag)
        sc = [l for l in obs if re.match(r'\[emu-oep\] observable: syscall \d+: ', l) and ' number ' in l]
        names = [re.match(r'\[emu-oep\] observable: syscall \d+: (\S+) ', l).group(1) for l in sc]
        answered = [(' -> status ' in l) or ('process terminated' in l) or ('-> continue at the CONTEXT' in l) for l in sc]
        first_bad = next((i for i, (a, b) in enumerate(zip(names, EXPECTED[tag])) if a != b), None)
        check(names == EXPECTED[tag] and all(answered), '%s: the %d services in the recorded order, every one answered (got %d; first difference at %s)' %
              (tag, len(EXPECTED[tag]), len(names), first_bad))
        st = [l for l in obs if l.startswith('[emu-oep] observable: exit status: ')]
        status[tag] = int(st[0].split(': ', 2)[2].split()[0], 16) if len(st) == 1 else None
        check(rc == 0 and status[tag] == STATUS[tag], '%s --Sys --loader: PASS - the guest exit status %02XH (got %s), emu-oep exit 0 (got %d)' % (tag, STATUS[tag], status[tag], rc))
        check(any('forwarded to the host: 0 (no such path exists)' in l for l in obs), '%s: nothing forwarded to the host' % tag)
        reads = [l for l in out if l.startswith('[emu-oep] probe: read ')]
        check(any('PEB.AnsiCodePageData' in l for l in reads) and any('PEB.UnicodeCaseTableData' in l for l in reads) and any('PARAMS.ImagePathName' in l for l in reads),
              '%s: the real loader read the NLS pointers and the process parameters the kernel side wrote' % tag)

    # --HLE: the same observable result
    for tag, path in (('T2', t2), ('T3', t3p)):
        rc, out = T1H.run([oep, '--guest', '--HLE', path])
        st = [l for l in out if l.startswith('[emu-oep] observable: exit status: ')]
        h = int(st[0].split(': ', 2)[2].split()[0], 16) if len(st) == 1 else None
        check(rc == 0 and h == STATUS[tag] and h == status.get(tag), '%s --HLE: the guest exit status %02XH = the --Sys result (got %s)' % (tag, STATUS[tag], h))
    # the native reference: started DETACHED (our own test PEs only)
    live = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', 'ntdll.dll')
    cap_sha = hashlib.sha256(open(os.path.join(cap, 'ntdll.dll'), 'rb').read()).hexdigest()
    if os.path.isfile(live) and hashlib.sha256(open(live, 'rb').read()).hexdigest() == cap_sha:
        for tag, path in (('T2', t2), ('T3', t3p)):
            r = subprocess.run([tm, 'run.detached', path], capture_output=True, text=True, timeout=120)
            m = [re.search(r'exit code ([0-9A-F]+)H', l) for l in r.stdout.splitlines() if l.startswith('tm_nt run.detached: ')]
            code = int(m[0].group(1), 16) if len(m) == 1 and m[0] else None
            check(r.returncode == 0 and code == STATUS[tag] and code == status.get(tag),
                  'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest, started DETACHED by tm_nt run.detached): %s exit code %s = the emulated %s'
                  % (tag, hex(code) if code is not None else None, hex(STATUS[tag])))
    else:
        say('native reference: not applicable here (the live ntdll.dll is not the VMware-guest capture)')
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: T2 / T3 run to their end under --Sys --loader (the real LdrInitializeThunk, %d / %d answered services, the model of a process '
        'started without a console) with the exit status 2BH / 2EH = --HLE = the detached native reference' % (len(EXPECTED['T2']), len(EXPECTED['T3'])))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
