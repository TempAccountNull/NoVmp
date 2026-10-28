#!/usr/bin/env python3
r"""win_env_exceptions.py - suite win_env_exceptions (plan 3.M7 = Gate 6 / G3.B exception set, P49, ledger U2260): the exception test PEs
TX1 - TX6 (the plan line's "T5-T10", renumbered: T5 - T10 are other tests) under --Sys --loader (the environment's real ntdll:
KiUserExceptionDispatcher, RtlDispatchException, RtlVirtualUnwind, NtContinue) and --HLE (win_exception.cpp: our documented dispatcher),
against the native reference of decision 3.0.7 (our own test PEs, run natively in the VMware guest, never under a debugger).

Each TX PE prints every observation as text (WriteFile to its standard output: natively a pipe here; emulated - the model of a process
started without a console (P41) - the console capture <out-dir>\console_capture.txt records the text) and exits with marker << 24 | a hash
of every observation (tx_common.inc). Checked:
  - the capture holds the registry items of the TX images' starts (IFEO\txN.exe absent; capture_sysdlls.py --reg, local only)
  - --Sys --loader: the guest's exit status = EXPECTED (emu-oep exit 0), nothing forwarded, the CPU events the run reports
    ("observable: exception N") = EXCEPTIONS (dispatched / silent)
  - --HLE: the same exit status, the same events (vector, error code, pushed RIP / RFLAGS / RSP, code, address, CONTEXT.Rip, parameters,
    how), the same text as --Sys
  - the NATIVE REFERENCE (labelled, only when the live ntdll equals the capture): the TX PE started DETACHED (tm_nt run.detached) exits
    with EXPECTED, and started with its standard output on a pipe prints exactly the emulated text and exits with EXPECTED
What the six PEs cover (P49): TX1 SEH through the x64 unwind data and 35 CPU events (INT3, INT 3, UD2, #DE zero / overflow, CPL 3 #GP
privileged / non-canonical / INT n, #PF read / write / execute, INT 4, ICEBP, INT 2CH, INT 2DH services, ...); TX2 vectored handlers, their
order against the frame handler and the continue handlers; TX3 single-step over REP string instructions (a #DB per iteration, RF);
TX4 CONTEXT / CONTEXT_EX / XSTATE (AVX), the frame layout, KUSER XState, a changed YMM state restored; TX5 the anti-debug surface and
PEB / TEB relations; TX6 the alignment check. Exit 0 = PASS, 1 = FAIL, 2 = usage.
usage: python -I win_env_exceptions.py REPO_ROOT TESTS_DIR
"""
import hashlib
import importlib.util
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
B = chr(92)


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


T1H = load('win_env_t1')
# the exit statuses (= the native reference, P49) and the CPU events (all, silent) of each TX PE
TX = [('TX1', 'tx1_seh', 0x61EDBFE4, 35, 4), ('TX2', 'tx2_veh', 0x62E99E3A, 4, 0), ('TX3', 'tx3_step', 0x637C7537, 29, 0),
      ('TX4', 'tx4_xstate', 0x64D39790, 10, 0), ('TX5', 'tx5_peb', 0x65F9C36D, 0, 0), ('TX6', 'tx6_align', 0x66901B83, 1, 0)]
IFEO = B + 'registry' + B + 'machine' + B + 'software' + B + 'microsoft' + B + 'windows nt' + B + 'currentversion' + B + 'image file execution options' + B


def say(msg):
    print('[win_env_exceptions] ' + msg)


def unesc(t):
    out, i = [], 0
    while i < len(t):
        if t[i] == B and i + 1 < len(t):
            out.append({'r': chr(13), 'n': chr(10), 't': chr(9), B: B, '"': '"'}.get(t[i + 1], t[i + 1]))
            i += 2
        else:
            out.append(t[i])
            i += 1
    return ''.join(out)


def captured_text(path):
    """the text of every output attempt in a console capture file, as lines"""
    lines = []
    if not os.path.isfile(path):
        return lines
    for l in open(path, encoding='ascii', errors='replace'):
        m = re.search(r' text "(.*)" -> ', l)
        if m:
            lines.extend(x for x in unesc(m.group(1)).split(chr(13) + chr(10)) if x)
    return lines


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    root, tests = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    oep = os.path.join(tests, 'emu-oep.exe')
    pe_dir = os.path.join(tests, 'test_pe')
    tm = os.path.join(pe_dir, 'tm_nt.exe')
    fails = []

    def check(ok, what):
        say(('ok    ' if ok else 'FAIL  ') + what)
        if not ok:
            fails.append(what)
        return ok

    for p in [oep, tm] + [os.path.join(pe_dir, n + '.exe') for _, n, _, _, _ in TX]:
        if not os.path.isfile(p):
            say('FAIL %s missing (build.cmd first)' % p)
            return 1
    cap = T1H.capture_dir(root, 'vmware_guest')
    reg = open(os.path.join(cap, 'registry.txt'), encoding='ascii').read().lower().splitlines() if cap and os.path.isfile(os.path.join(cap, 'registry.txt')) else []
    check(cap and all(('key' + chr(9) + 'absent' + chr(9) + IFEO + n + '.exe') in reg for _, n, _, _, _ in TX),
          'the capture holds the registry items of the TX images\' starts (IFEO' + B + 'txN.exe absent; capture_sysdlls.py --reg)')
    outdir = os.path.join(tests, 'exceptions_out')
    text = {}
    for tag, name, status, n_exc, n_silent in TX:
        path = os.path.join(pe_dir, name + '.exe')
        res = {}
        for mode, args in (('sys', ['--Sys', '--loader']), ('hle', ['--HLE'])):
            od = os.path.join(outdir, name + '_' + mode)
            os.makedirs(od, exist_ok=True)
            for f in ('console_capture.txt', 'fs_access.txt'):
                if os.path.isfile(os.path.join(od, f)):
                    os.remove(os.path.join(od, f))   # this suite's own output of an earlier run (one file each)
            rc, out = T1H.run([oep, '--guest'] + args + ['--out-dir', od, path])
            obs = T1H.observable(out)
            st = [l for l in obs if l.startswith('[emu-oep] observable: exit status: ')]
            got = int(st[0].split(': ', 2)[2].split()[0], 16) if len(st) == 1 else None
            # the events without the instruction count and with the stack addresses as one distance (the two modes start the thread
            # at different stack depths: --Sys through ntdll!RtlUserThreadStart): RSP - CONTEXT less RSP's low 6 bits (the frame is
            # 64-byte aligned below RSP, P49 TX3 / TX4)
            ev = []
            for l in obs:
                if not re.match(r'\[emu-oep\] observable: exception \d+ \(insn \d+\): ', l):
                    continue
                e = re.sub(r' \(insn \d+\)', '', l.split('observable: ', 1)[1])
                mr, mc = re.search(r' rsp (0x[0-9a-f]+) ', e), re.search(r'the CONTEXT at (0x[0-9a-f]+)', e)
                if mr and mc:
                    rsp_v = int(mr.group(1), 16)
                    e = e.replace(mr.group(0), ' rsp-CONTEXT-(rsp&3F) %x ' % (rsp_v - int(mc.group(1), 16) - (rsp_v & 0x3F))).replace(mc.group(0), 'the CONTEXT')
                elif mr:
                    e = e.replace(mr.group(0), ' rsp ')
                ev.append(e)
            res[mode] = (rc, got, ev, captured_text(os.path.join(od, 'console_capture.txt')), obs)
            check(rc == 0 and got == status, '%s %s: the guest exit status %08XH (got %s), emu-oep exit 0 (got %d)' % (tag, mode, status, '%08XH' % got if got is not None else None, rc))
            check(any('forwarded to the host: 0 (no such path exists)' in l for l in obs), '%s %s: nothing forwarded to the host' % (tag, mode))
            check(len(ev) == n_exc and sum('-> silent:' in e for e in ev) == n_silent,
                  '%s %s: %d CPU events (%d silent INT 2DH services) delivered (got %d / %d)' % (tag, mode, n_exc, n_silent, len(ev), sum('-> silent:' in e for e in ev)))
        s, h = res['sys'], res['hle']
        check(s[2] == h[2], '%s: --Sys and --HLE deliver the same CPU events (vector, error code, pushed RIP / RFLAGS / RSP, code, address, CONTEXT.Rip, '
                            'parameters; first difference: %s)' % (tag, next((i for i, (a, b) in enumerate(zip(s[2], h[2])) if a != b), None)))
        check(s[3] == h[3] and len(s[3]) > 0, '%s: --Sys and --HLE print the same %d lines (the observations of the test PE)' % (tag, len(s[3])))
        text[tag] = s[3]
    # the native reference: started detached (tm_nt run.detached) and with the standard output on a pipe
    live = os.path.join(os.environ.get('SystemRoot', r'C:' + B + 'Windows'), 'System32', 'ntdll.dll')
    cap_sha = hashlib.sha256(open(os.path.join(cap, 'ntdll.dll'), 'rb').read()).hexdigest() if cap else ''
    if os.path.isfile(live) and hashlib.sha256(open(live, 'rb').read()).hexdigest() == cap_sha:
        for tag, name, status, _, _ in TX:
            path = os.path.join(pe_dir, name + '.exe')
            r = subprocess.run([tm, 'run.detached', path], capture_output=True, text=True, timeout=120)
            m = [re.search(r'exit code ([0-9A-F]+)H', l) for l in r.stdout.splitlines() if l.startswith('tm_nt run.detached: ')]
            code = int(m[0].group(1), 16) if len(m) == 1 and m[0] else None
            check(r.returncode == 0 and code == status, 'NATIVE REFERENCE (labelled, decision 3.0.7: our own test PE in the VMware guest, started DETACHED by tm_nt '
                                                        'run.detached): %s exit code %s = the emulated %08XH' % (tag, '%08XH' % code if code is not None else None, status))
            r = subprocess.run([path], capture_output=True, timeout=120)
            nat = [l for l in r.stdout.decode('ascii', 'replace').split(chr(13) + chr(10)) if l]
            first = next((i for i, (a, b) in enumerate(zip(nat, text.get(tag, []))) if a != b), None)
            check((r.returncode & 0xFFFFFFFF) == status and nat == text.get(tag),
                  'NATIVE REFERENCE (labelled): %s with its standard output on a pipe exits %08XH and prints the %d emulated lines exactly (got exit %08XH, %d lines, first '
                  'difference at line %s)' % (tag, status, len(text.get(tag, [])), r.returncode & 0xFFFFFFFF, len(nat), first))
    else:
        say('native reference: not applicable here (the live ntdll.dll is not the VMware-guest capture)')
    if fails:
        say('FAIL (%d problem(s))' % len(fails))
        return 1
    say('PASS: TX1 - TX6 under --Sys --loader (the real ntdll exception dispatch) and --HLE (win_exception.cpp) - the same exit statuses, CPU events and '
        'printed observations as the native reference (Gate 6 exception set, plan 3.M7)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
