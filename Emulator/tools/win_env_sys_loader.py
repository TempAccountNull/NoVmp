#!/usr/bin/env python3
r"""win_env_sys_loader.py - suite win_env_sys_loader (plan 3.M5 part 2, ledger U2039; P39 U2060; P40 U2067; P41 U2086): the opt-in --Sys
--loader start through the VMware-guest environment's real ntdll!LdrInitializeThunk - since P41 to the END of the test PEs: T2 / T3 / T5
PASS under --Sys (the plan 3.M5 done criterion for T2 / T3).

P41 (USER DECISION 2026-10-26 "started without a console"): the emulated process is the model of a process started DETACHED (process
parameters ConsoleHandle -1 = RTL_USER_PROC_DETACHED_PROCESS, no standard handles); every system service the real ntdll / kernelbase /
kernel32 then call is answered from phnt prototypes + native measurements of our own test PE tm_nt (Emulator/data/
nt_measured_vmware_guest.txt; P39 / P40 / P41), never forwarded to the host. Checked (the test PEs are read as data by emu-oep, never run
by it; the system DLLs are mapped as data and run only in emulation):
  - the capture holds the loader files and the registry subset with the P39 / P40 / P41 items (capture_sysdlls.py --dll / --reg / --reg-value)
  - T2 --Sys without --loader: refused (exit 3) as in milestone M5
  - T2, T3 (its service number patched from the capture, as win_env_m5; a copy of its own in test_pe\sysloader\) and T5 under --Sys
    --loader: the kernel-side start (NLS / API set views, the CONTEXT, IRETQ to LdrInitializeThunk); EVERY system service answered, in the
    recorded order (EXPECTED below: T2 210, T3 202, T5 211 services); the guest's exit status 2BH / 2EH / 2FH (emu-oep exit 0); nothing
    forwarded; the process parameters' console fields at the stop = what a natively detached process reads (ConsoleHandle 0 after its
    kernelbase start, no standard handles: tm_nt con.pp)
  - the same test PEs under --HLE: the same exit status (the observable result of both modes)
  - the CONSOLE CAPTURE (P41, U2084): T5 prints through WriteFile and WriteConsoleW to its (absent) standard output; under --Sys and
    --HLE the capture file <out-dir>\console_capture.txt holds both attempts with their text and results (FALSE, LastError 6), while T5 got
    the measured "no console" results (its exit status 2FH says so)
  - the NATIVE REFERENCE (labelled, decision 3.0.7), only when the live ntdll equals the capture: T2 / T3 / T5 started natively DETACHED
    (tm_nt run.detached: CreateProcessW( DETACHED_PROCESS )) exit with the same codes as their emulated runs
  - P44 (U2124): T6 (t6_user32, GUI subsystem, links user32: USER DECISION 2026-10-27 STEP 3) under --Sys --loader - user32's CSR USERSRV
    connection (the model measured from inside our own GUI test PE tm_u32), the GUI conversion at NtGdiInit with user32's ClientThreadSetup
    callback, CharUpperW, the DLL detach: the recorded 369 services, exit 36H; --HLE (the CharUpperW stub) 36H; the native reference started
    NORMALLY (tm_nt run.normal) 36H; no console output attempt
  - P45 (U2133, USER DECISION 2026-10-27 "virtual FS, measured misses"): T7 (t7_vfs, a console program started detached) opens a missing
    file next to itself and a missing <parent>\SystemResources\<its name>.mun (the sample's .mun search, run 9) and queries the missing
    file's attributes; the exit status encodes the three statuses and what was written: 1F343A34H natively (DETACHED), under --Sys
    (213 services; the emulated file system's measured misses, tm_nt fs.*) and under --HLE (ntdll's file services = the kernel layer);
    <out dir>\fs_access.txt of both modes holds the accesses with their answers
A changed service sequence fails this suite (a newly answered or changed service must update EXPECTED and the notes). Exit 0 = PASS,
1 = FAIL, 2 = usage.
usage: python -I win_env_sys_loader.py REPO_ROOT TESTS_DIR
"""
import hashlib
import importlib.util
import os
import re
import struct
import shutil
import subprocess
import sys
import tempfile
import time

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
    'T5': ('NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtCreateEvent NtQuerySystemInformation NtQueryInformationProcess '
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
    'NtWriteFile NtTerminateProcess NtClose NtClose NtClose NtClose NtQueryWnfStateData NtTerminateProcess ').split(),
    # P44 (U2124): T6 - a GUI program that links user32 (its USERSRV connection, the GUI conversion + ClientThreadSetup callback, CharUpperW,
    # the DLL detach); the win32k services carry their win32u names (the second table, U2117)
    'T6': ('NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtCreateEvent NtQuerySystemInformation NtQueryInformationProcess '
    'NtQuerySystemInformation NtQueryVirtualMemory NtQueryVirtualMemory NtProtectVirtualMemory NtOpenKey NtQueryValueKey NtClose NtOpenKey '
    'NtOpenKey NtOpenKey NtQueryInformationProcess NtQueryInformationProcess NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken '
    'NtOpenKey NtQueryValueKey NtClose NtQueryInformationProcess NtQuerySystemInformation NtQuerySystemInformation NtAllocateVirtualMemoryEx '
    'NtAllocateVirtualMemoryEx NtAllocateVirtualMemoryEx NtQuerySystemInformation NtAllocateVirtualMemory NtFreeVirtualMemory '
    'NtAllocateVirtualMemory NtQuerySystemInformation NtQuerySystemInformation NtCreateEvent NtCreateWaitCompletionPacket '
    'NtQuerySystemInformationEx NtCreateIoCompletion NtCreateWorkerFactory NtCreateTimer2 NtCreateWaitCompletionPacket '
    'NtAssociateWaitCompletionPacket NtCreateTimer2 NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtSetInformationWorkerFactory '
    'NtSetInformationWorkerFactory NtSetInformationWorkerFactory NtAssociateWaitCompletionPacket NtTraceControl NtTraceControl NtTraceControl '
    'NtTraceControl NtFreeVirtualMemory NtProtectVirtualMemory NtOpenDirectoryObject NtProtectVirtualMemory NtOpenSymbolicLinkObject '
    'NtQuerySymbolicLinkObject NtClose NtProtectVirtualMemory NtProtectVirtualMemory NtCreateEvent NtCreateEvent NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtOpenFile NtQueryVolumeInformationFile NtSetEvent NtAllocateVirtualMemory NtOpenSection NtMapViewOfSection '
    'NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtOpenSection '
    'NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtClose NtProtectVirtualMemory NtClose NtProtectVirtualMemory NtSetInformationProcess NtApphelpCacheControl NtApphelpCacheControl '
    'NtSetInformationVirtualMemory NtQuerySystemInformation NtQuerySystemInformation NtOpenSection NtCreateSection NtConnectPort NtClose '
    'NtMapViewOfSection NtClose NtAllocateVirtualMemory NtInitializeNlsFiles NtTraceControl NtTraceControl NtQueryInformationToken NtAccessCheck '
    'NtAccessCheck NtOpenProcessToken NtQueryInformationToken NtQueryInformationToken NtQueryInformationToken NtOpenDirectoryObject '
    'NtQueryInformationToken NtClose NtCreateMutant NtWaitForSingleObject NtOpenSemaphore NtCreateSemaphore NtCreateSemaphore NtReleaseMutant '
    'NtQueryWnfStateData NtCreateEvent NtCreateWaitCompletionPacket NtSetWnfProcessNotificationEvent NtAssociateWaitCompletionPacket '
    'NtSubscribeWnfStateChange NtQueryWnfStateData NtSubscribeWnfStateChange NtQueryWnfStateData NtSubscribeWnfStateChange NtQueryWnfStateData '
    'NtAllocateVirtualMemory NtSetTimer2 NtOpenKey NtTraceControl NtTraceControl NtTraceControl NtTraceControl NtSetInformationVirtualMemory '
    'NtOpenKey NtTraceControl NtTraceControl NtQuerySystemInformation NtSetEvent NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtOpenKey NtOpenKey NtOpenKey NtQueryValueKey NtClose NtQueryInformationToken NtOpenKey NtOpenKey NtQueryValueKey NtClose NtOpenKey '
    'NtQueryValueKey NtClose NtQuerySystemInformationEx NtCreateIoCompletion NtCreateWorkerFactory NtSetInformationWorkerFactory NtCreateTimer2 '
    'NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtCreateTimer2 NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket '
    'NtSetInformationWorkerFactory NtSetInformationWorkerFactory NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken '
    'NtProtectVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtClose NtAllocateVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtOpenSection NtMapViewOfSection '
    'NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtOpenSection '
    'NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtAllocateVirtualMemory '
    'NtAllocateVirtualMemory NtOpenKey NtQueryValueKey NtClose NtAllocateVirtualMemory NtAllocateVirtualMemory NtClose NtClose NtClose NtClose '
    'NtClose NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtApphelpCacheControl NtApphelpCacheControl NtApphelpCacheControl NtApphelpCacheControl NtApphelpCacheControl '
    'NtApphelpCacheControl NtApphelpCacheControl NtSetInformationVirtualMemory NtSetEvent NtSetInformationVirtualMemory '
    'NtSetInformationVirtualMemory NtAllocateVirtualMemory NtAllocateVirtualMemory NtOpenKey NtQueryValueKey NtQueryValueKey '
    'NtQueryPerformanceCounter NtSetInformationVirtualMemory NtAllocateVirtualMemory NtAllocateVirtualMemory NtAllocateVirtualMemory '
    'NtSetInformationVirtualMemory NtSetInformationVirtualMemory NtQueryInformationProcess NtSetInformationVirtualMemory '
    'NtQuerySystemInformation NtProtectVirtualMemory NtProtectVirtualMemory NtAlpcSendWaitReceivePort NtQueryAttributesFile NtOpenFile '
    'NtCreateSection NtMapViewOfSection NtUnmapViewOfSection NtClose NtClose NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken '
    'NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtProtectVirtualMemory NtClose NtApphelpCacheControl NtSetInformationVirtualMemory NtQuerySystemInformation '
    'NtQueryInformationProcess NtQueryInformationProcess NtOpenKey NtTraceControl NtTraceControl NtTraceControl NtTraceControl NtTraceControl '
    'NtTraceControl NtOpenKey NtOpenKey NtOpenKey NtOpenKey NtOpenKey NtOpenKey NtOpenKey NtQueryValueKey NtClose NtOpenKey NtQueryValueKey '
    'NtClose NtGdiInit NtAllocateVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtUserCallNoParam NtProtectVirtualMemory '
    'NtProtectVirtualMemory NtUserGetThreadDesktop NtUserCallOneParam NtUserGetProcessUIContextInformation NtOpenProcessToken NtDuplicateToken '
    'NtAccessCheck NtClose NtClose NtQueryInformationProcess NtOpenKeyEx NtQueryKey NtOpenKeyEx NtQueryValueKey NtClose NtCallbackReturn '
    'NtOpenKey NtSetEvent NtSetInformationWorkerFactory NtSetEvent NtTestAlert NtContinue NtTerminateProcess NtClose NtClose NtClose NtOpenKey '
    'NtQueryValueKey NtClose NtOpenKey NtQueryValueKey NtClose NtSetInformationThread NtSetInformationThread NtClose NtClose NtClose NtClose '
    'NtQueryWnfStateData NtTerminateProcess ').split(),
    # P45 (U2133): T7 - a console program (started detached) that opens a missing file next to itself and a missing
    # <parent>\\SystemResources\\<its name>.mun and queries the missing file's attributes: the emulated file system's measured misses
    'T7': ('NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtCreateEvent NtQuerySystemInformation NtQueryInformationProcess '
           'NtQuerySystemInformation NtQueryVirtualMemory NtQueryVirtualMemory NtProtectVirtualMemory NtOpenKey NtQueryValueKey NtClose NtOpenKey NtOpenKey '
           'NtOpenKey NtQueryInformationProcess NtQueryInformationProcess NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken NtOpenKey NtQueryValueKey '
           'NtClose NtQueryInformationProcess NtQuerySystemInformation NtQuerySystemInformation NtAllocateVirtualMemoryEx NtAllocateVirtualMemoryEx '
           'NtAllocateVirtualMemoryEx NtQuerySystemInformation NtAllocateVirtualMemory NtFreeVirtualMemory NtAllocateVirtualMemory NtQuerySystemInformation '
           'NtQuerySystemInformation NtCreateEvent NtCreateWaitCompletionPacket NtQuerySystemInformationEx NtCreateIoCompletion NtCreateWorkerFactory '
           'NtCreateTimer2 NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtCreateTimer2 NtCreateWaitCompletionPacket '
           'NtAssociateWaitCompletionPacket NtSetInformationWorkerFactory NtSetInformationWorkerFactory NtSetInformationWorkerFactory '
           'NtAssociateWaitCompletionPacket NtTraceControl NtTraceControl NtTraceControl NtTraceControl NtFreeVirtualMemory NtProtectVirtualMemory '
           'NtOpenDirectoryObject NtProtectVirtualMemory NtOpenSymbolicLinkObject NtQuerySymbolicLinkObject NtClose NtProtectVirtualMemory NtProtectVirtualMemory '
           'NtCreateEvent NtCreateEvent NtProtectVirtualMemory NtProtectVirtualMemory NtOpenFile NtQueryVolumeInformationFile NtSetEvent NtAllocateVirtualMemory '
           'NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
           'NtProtectVirtualMemory NtOpenSection NtMapViewOfSection NtQueryPerformanceCounter NtProtectVirtualMemory NtProtectVirtualMemory '
           'NtProtectVirtualMemory NtProtectVirtualMemory NtClose NtProtectVirtualMemory NtClose NtProtectVirtualMemory NtSetInformationProcess '
           'NtApphelpCacheControl NtApphelpCacheControl NtSetInformationVirtualMemory NtQuerySystemInformation NtQuerySystemInformation NtOpenSection '
           'NtCreateSection NtConnectPort NtClose NtMapViewOfSection NtClose NtAllocateVirtualMemory NtInitializeNlsFiles NtSetInformationProcess NtTraceControl '
           'NtTraceControl NtQueryInformationToken NtAccessCheck NtAccessCheck NtOpenProcessToken NtQueryInformationToken NtQueryInformationToken '
           'NtQueryInformationToken NtOpenDirectoryObject NtQueryInformationToken NtClose NtCreateMutant NtWaitForSingleObject NtOpenSemaphore NtCreateSemaphore '
           'NtCreateSemaphore NtReleaseMutant NtQueryWnfStateData NtCreateEvent NtCreateWaitCompletionPacket NtSetWnfProcessNotificationEvent '
           'NtAssociateWaitCompletionPacket NtSubscribeWnfStateChange NtQueryWnfStateData NtSubscribeWnfStateChange NtQueryWnfStateData NtSubscribeWnfStateChange '
           'NtQueryWnfStateData NtAllocateVirtualMemory NtSetTimer2 NtOpenKey NtTraceControl NtTraceControl NtTraceControl NtTraceControl '
           'NtSetInformationVirtualMemory NtOpenKey NtTraceControl NtTraceControl NtQuerySystemInformation NtSetEvent NtProtectVirtualMemory '
           'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
           'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
           'NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory NtProtectVirtualMemory '
           'NtProtectVirtualMemory NtOpenKey NtOpenKey NtOpenKey NtQueryValueKey NtClose NtQueryInformationToken NtOpenKey NtOpenKey NtQueryValueKey NtClose '
           'NtOpenKey NtQueryValueKey NtClose NtQuerySystemInformationEx NtCreateIoCompletion NtCreateWorkerFactory NtSetInformationWorkerFactory NtCreateTimer2 '
           'NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtCreateTimer2 NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket '
           'NtSetInformationWorkerFactory NtSetInformationWorkerFactory NtQuerySecurityAttributesToken NtQuerySecurityAttributesToken NtProtectVirtualMemory '
           'NtProtectVirtualMemory NtApphelpCacheControl NtSetInformationVirtualMemory NtSetEvent NtSetEvent NtSetInformationWorkerFactory NtSetEvent NtTestAlert '
           'NtContinue NtOpenFile NtQueryAttributesFile NtOpenFile NtTerminateProcess NtClose NtClose NtClose NtClose NtQueryWnfStateData NtTerminateProcess ').split(),
}
STATUS = {'T2': 0x2B, 'T3': 0x2E, 'T5': 0x2F, 'T6': 0x36, 'T7': 0x1F343A34}
# P41: the registry items the start of a process without a console asks (capture_sysdlls.py --reg / --reg-value; local, never committed)
P41_REGISTRY = [
    r'key	absent	\registry\machine\system\currentcontrolset\control\stateseparation\redirectionmap\keys',
    r'key	absent	\registry\machine\system\currentcontrolset\control\safeboot\option',
    r'key	absent	\registry\machine\system\currentcontrolset\control\srp\gp\dll',
    r'key	present	\registry\machine\software\policies\microsoft\windows\safer\codeidentifiers',
    r'key	present	\registry\machine\system\currentcontrolset\control\filesystem',
    r'key	absent	\registry\machine\software\microsoft\windows nt\currentversion\image file execution options\t5_stdout.exe',
    # P44 (U2124): user32's start in T6 (capture_sysdlls.py --reg / --reg-value; local)
    r'key	absent	\registry\machine\software\microsoft\windows nt\currentversion\image file execution options\t6_user32.exe',
    r'key	absent	\registry\machine\system\currentcontrolset\control\error message instrument',
    r'key	absent	\registry\machine\software\policies\microsoft\windows\display',
    r'key	present	\registry\machine\software\microsoft\windows nt\currentversion\gre_initialize',
    r'value	\registry\machine\software\microsoft\windows nt\currentversion\gre_initialize	disablemetafiles	absent',
    r'value	\registry\machine\software\microsoft\windows nt\currentversion\gre_initialize	disableumpdbuffersizecheck	absent',
    r'key	present	\registry\machine\software\microsoft\windows nt\currentversion\windows',
    r'value	\registry\machine\software\microsoft\windows nt\currentversion\windows	loadappinit_dlls	4	00000000',
    # P45 (U2133): T7's start
    r'key	absent	\registry\machine\software\microsoft\windows nt\currentversion\image file execution options\t7_vfs.exe',
]
T5_TEXTS = ('t5_stdout: hello from WriteFile' + chr(92) + 'r' + chr(92) + 'n', 't5_stdout: hello from WriteConsoleW' + chr(92) + 'r' + chr(92) + 'n')


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
    t2, t3, t5, t6, t7 = (os.path.join(pe_dir, n) for n in ('t2_import.exe', 't3_tls.exe', 't5_stdout.exe', 't6_user32.exe', 't7_vfs.exe'))   # P45: + T7
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in (oep, t2, t3, t5, t6, t7, tm):
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    cap = T1H.capture_dir(root, 'vmware_guest')
    man = T1H.manifest(cap) if cap else {}
    reg = open(os.path.join(cap, 'registry.txt'), encoding='ascii').read().lower().splitlines() if cap and os.path.isfile(os.path.join(cap, 'registry.txt')) else []
    check(cap and all('file_' + n in man for n in ('kernel32.dll', 'kernelbase.dll', 'apisetschema.dll', 'locale.nls')) and 'nls_case' in man and
          all(x in reg for x in P41_REGISTRY) and 'file_user32.dll' in man and 'file_win32u.dll' in man and 'file_imm32.dll' in man,
          'the capture holds the loader files (+ locale.nls, P40), user32 / win32u / imm32 (P44) and the registry subset (U2024 / U2034; + the P41 / P44 items)')
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
    outdir = os.path.join(tests, 'sysloader_out')
    status = {}
    for tag, path in (('T2', t2), ('T3', t3p), ('T5', t5), ('T6', t6), ('T7', t7)):
        od = os.path.join(outdir, tag.lower() + '_sys')
        os.makedirs(od, exist_ok=True)
        cf = os.path.join(od, 'console_capture.txt')
        if os.path.isfile(cf):
            os.remove(cf)   # this suite's own output file of an earlier run (one file)
        ff = os.path.join(od, 'fs_access.txt')   # P45 (U2128)
        if os.path.isfile(ff):
            os.remove(ff)   # this suite's own output file of an earlier run
        rc, out = T1H.run([oep, '--guest', '--Sys', '--loader', '--counters', '--out-dir', od, path])
        obs = T1H.observable(out)
        check(any('env: NLS view at ' in l for l in obs) and any('env: API set schema view at ' in l and 'schema version 6' in l for l in obs) and
              any('env: --Sys loader start: CONTEXT at ' in l and 'RDX = the ntdll base' in l for l in obs) and
              any(l.startswith('[emu-oep] run: IRETQ to ntdll!LdrInitializeThunk') for l in out),
              '%s --Sys --loader: the kernel-side start (NLS / API set views, the CONTEXT, IRETQ to LdrInitializeThunk with RCX / RDX)' % tag)
        sc = [l for l in obs if re.match(r'\[emu-oep\] observable: syscall \d+: ', l) and ' number ' in l]
        names = [re.match(r'\[emu-oep\] observable: syscall \d+: (\S+) ', l).group(1) for l in sc]
        answered = [(' -> status ' in l) or ('process terminated' in l) or ('-> continue at the CONTEXT' in l) or ('-> callback into user mode: ' in l) for l in sc]   # P44: + a callback
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
        check(any(l.startswith('[emu-oep] console fields at the stop: ') and "(the process' copy): ConsoleHandle 0x0, ConsoleFlags 0x0, StandardInput 0x0, StandardOutput 0x0, "
                  'StandardError 0x0' in l for l in out),
              '%s: the process parameters after the start = a natively detached process (ConsoleHandle 0, no standard handles: tm_nt con.pp)' % tag)
        if tag == 'T7':
            # P45 (U2133): the emulated file system's answers, recorded in <out dir>\fs_access.txt (observation only)
            body = open(ff, encoding='ascii').read() if os.path.isfile(ff) else ''
            want = ['NtOpenFile "\\??\\<image dir>\\" access 100020H, attributes 40H, share 3H, options 21H -> status 00000000H',
                    'NtOpenFile "\\??\\<image dir>\\t7_vfs_absent_5d2c.dat" access 100001H, attributes 40H, share 5H, options 60H -> status C0000034H',
                    'NtQueryAttributesFile "\\??\\<image dir>\\t7_vfs_absent_5d2c.dat" attributes 40H -> status C0000034H',
                    'NtOpenFile "\\??\\<user profile>\\SystemResources\\t7_vfs.exe.mun" access 100001H, attributes 40H, share 5H, options 60H -> status C000003AH']
            check(all(w in body for w in want) and body.count(chr(10)) == 6 and any('file-system accesses: 5; file ' in l for l in out),
                  'T7 --Sys: fs_access.txt holds the 5 file-system accesses (the loader\'s directory open + its volume query, T7\'s two misses and the .mun miss) with the measured answers')
        if tag == 'T5':
            cap_lines = [l for l in out if l.startswith('[emu-oep] console capture #')]
            body = open(cf, encoding='ascii').read() if os.path.isfile(cf) else ''
            check(len(cap_lines) == 2 and 'kernelbase.dll!WriteFile handle 0x0 length 33 text "%s" -> returned 0x0, LastError 6, written 0' % T5_TEXTS[0] in cap_lines[0] and
                  'kernelbase.dll!WriteConsoleW handle 0x0 length 37 text "%s" -> returned 0x0, LastError 6' % T5_TEXTS[1] in cap_lines[1] and
                  all(t in body for t in T5_TEXTS) and body.count(chr(10)) == 3,
                  'T5 --Sys: the console capture holds both output attempts (kernelbase WriteFile / WriteConsoleW on the standard handle 0, their text, FALSE / LastError 6) in %s' % cf)
        else:
            check(not os.path.isfile(cf) and any('console capture: 0 output attempt(s)' in l for l in out), '%s --Sys: no output attempt, no capture file' % tag)
    # --HLE: the same observable result
    for tag, path in (('T2', t2), ('T3', t3p), ('T5', t5), ('T6', t6), ('T7', t7)):
        od = os.path.join(outdir, tag.lower() + '_hle')
        os.makedirs(od, exist_ok=True)
        cf = os.path.join(od, 'console_capture.txt')
        if os.path.isfile(cf):
            os.remove(cf)
        rc, out = T1H.run([oep, '--guest', '--HLE', '--out-dir', od, path])
        st = [l for l in out if l.startswith('[emu-oep] observable: exit status: ')]
        h = int(st[0].split(': ', 2)[2].split()[0], 16) if len(st) == 1 else None
        check(rc == 0 and h == STATUS[tag] and h == status.get(tag), '%s --HLE: the guest exit status %02XH = the --Sys result (got %s)' % (tag, STATUS[tag], h))
        if tag == 'T7':
            body = open(os.path.join(od, 'fs_access.txt'), encoding='ascii').read() if os.path.isfile(os.path.join(od, 'fs_access.txt')) else ''
            check(body.count(chr(10)) == 4 and 'C000003AH' in body and body.count('C0000034H') == 2, 'T7 --HLE: fs_access.txt holds T7\'s 3 file-system calls (the ntdll stubs = the kernel layer) with the same answers')
        if tag == 'T5':
            body = open(cf, encoding='ascii').read() if os.path.isfile(cf) else ''
            check('KERNEL32.dll!WriteFile handle 0x0 length 33 text "%s" -> FALSE, LastError 6 (ERROR_INVALID_HANDLE), *lpNumberOfBytesWritten 0' % T5_TEXTS[0] in body and
                  'KERNEL32.dll!WriteConsoleW handle 0x0 length 37 text "%s" -> FALSE, LastError 6 (ERROR_INVALID_HANDLE), the written count not written' % T5_TEXTS[1] in body,
                  'T5 --HLE: the console capture holds both output attempts (the API stubs, their text, FALSE / LastError 6) in %s' % cf)
    # the native reference: started DETACHED (our own test PEs only)
    live = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', 'ntdll.dll')
    cap_sha = hashlib.sha256(open(os.path.join(cap, 'ntdll.dll'), 'rb').read()).hexdigest()
    if os.path.isfile(live) and hashlib.sha256(open(live, 'rb').read()).hexdigest() == cap_sha:
        for tag, path in (('T2', t2), ('T3', t3p), ('T5', t5), ('T6', t6), ('T7', t7)):
            # P44 (U2124): T6 (a GUI program) is started NORMALLY (tm_nt run.normal, no creation flags), the console programs detached
            how = 'run.normal' if tag == 'T6' else 'run.detached'
            folder = None
            if tag == 'T7':
                # P45 (U2133): what a file-system miss WRITES depends on the volume of the image's folder (P44 U2111: the system volume writes
                # nothing, the guest's other volume E: zeros) and the emulated image lies on the system volume - T7 runs from a COPY in
                # <a fresh folder below %TEMP%>\img (its .mun search then looks in that fresh folder: SystemResources is surely missing)
                folder = tempfile.mkdtemp(prefix='win_env_t7_')
                os.mkdir(os.path.join(folder, 'img'))
                path = os.path.join(folder, 'img', 't7_vfs.exe')
                shutil.copyfile(t7, path)
                check(os.path.splitdrive(folder)[0].upper() == os.path.splitdrive(os.environ.get('SystemRoot', r'C:\Windows'))[0].upper(),
                      'T7 native copy on the system volume (%%TEMP%% %s)' % os.path.splitdrive(folder)[0])
            r = subprocess.run([tm, how, path], capture_output=True, text=True, timeout=120)
            if folder:
                for k in range(30):   # our one copy, then the two folders (retried while the image is still held)
                    try:
                        if os.path.exists(path):
                            os.remove(path)
                        os.rmdir(os.path.join(folder, 'img'))
                        os.rmdir(folder)
                        break
                    except OSError:
                        time.sleep(0.5)
            m = [re.search(r'exit code ([0-9A-F]+)H', l) for l in r.stdout.splitlines() if l.startswith('tm_nt %s: ' % how)]
            code = int(m[0].group(1), 16) if len(m) == 1 and m[0] else None
            check(r.returncode == 0 and code == STATUS[tag] and code == status.get(tag),
                  'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest, started %s by tm_nt %s): %s exit code %s = the emulated %s'
                  % ('NORMALLY' if tag == 'T6' else 'DETACHED', how, tag, hex(code) if code is not None else None, hex(STATUS[tag])))
    else:
        say('native reference: not applicable here (the live ntdll.dll is not the VMware-guest capture)')
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: T2 / T3 / T5 / T6 / T7 run to their end under --Sys --loader (the real LdrInitializeThunk, %d / %d / %d / %d answered services, the model of a process '
        'started without a console; T6: user32\'s USERSRV connection and GUI start, P44) with the exit status 2BH / 2EH / 2FH / 36H = --HLE = the native '
        'reference; T5\'s output attempts captured in both modes' % (len(EXPECTED['T2']), len(EXPECTED['T3']), len(EXPECTED['T5']), len(EXPECTED['T6'])))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
