#!/usr/bin/env python3
"""check_setjmp_slots.py - suite setjmp_slots (ledger U2046, notes/eread/P38_setjmp_audit.md; the bug class of U2043).

Why: under MSVC the fork's sigsetjmp is the asm routine _setjmp_wrapper (unicorn/qemu/util/setjmp-wrapper-win32.asm:
`xor rdx, rdx; jmp _setjmp`, so that longjmp does no unwinding through generated code). The compiler therefore sees an
ORDINARY call without returns-twice semantics. After siglongjmp the code after the call runs again with the nonvolatile
registers (rbx rbp rdi rsi r12-r15) restored to their values at the setjmp call (they are in the jmp_buf), but with stack
memory as the first ("zero") return left it. A frame slot that the resumed code reads may meanwhile hold ANOTHER variable:
MSVC packs variables with disjoint live ranges (in its single-return view) into one slot. That is how U2043's
orig_cc_op / orig_pc_save were lost (also when declared volatile). C11 7.13.2.1 only covers locals modified between
setjmp and longjmp, and only when the implementation knows setjmp; here neither guarantee holds, so the binary decides.

What is checked (Release objects of unicorn.vcxproj, read only; dumpbin /disasm:nobytes, nothing executed):
  1. every .obj in OBJDIR that names _setjmp_wrapper is disassembled; every function with `call _setjmp_wrapper` is a site;
     the set of (object, function) sites must equal EXPECTED (a new or vanished site fails: review it, P38, update EXPECTED);
  2. per site, the RESUMED paths are walked from the instruction after the call with the return register holding each value
     the site's throwers pass (EXPECTED: cpu_loop_exit 1, advance_pc 1 / 2, tcg_raise_tb_overflow -2); `test` / `cmp` + jcc on
     that register (and copies made by `mov r32, eax`) are evaluated while its value is known; calls of NORETURN functions end a
     path; an indirect jmp on a resumed path makes the site unanalysable (fail);
  3. a forward must-write dataflow over the resumed paths gives, for every stack slot ([rsp+X] / [rbp+-X]) read there, whether
     the resumed path itself wrote it first on every path; a slot read WITHOUT that is a setjmp-time read;
  4. RISK = a setjmp-time read of a slot that is written anywhere after the setjmp call (reachable from the call, or later in
     the layout - a jump table hides its targets from the walk). Such a slot can hold the zero path's value, not the setjmp-
     time one. The suite passes with 0 RISK (state that must survive the jump lives in a structure that outlives the frame,
     the U2043 / U2045 pattern) and every site analysed.

usage: python -I check_setjmp_slots.py OBJDIR [--dumpbin EXE] [--expect-risk N] [--any-sites] [-v]
       python -I check_setjmp_slots.py --selftest
  --expect-risk N   pass when exactly N RISK reads are found (negative control: the original-QEMU objects of the U2044 tree
                    give 3 - disas_insn's orig_cc_op / orig_pc_save and tb_gen_code's max_insns before the U2045 backport)
  --any-sites       do not require the EXPECTED site set (for other builds, e.g. build\\orig)
dumpbin: --dumpbin, else NOVMP_DUMPBIN, else vswhere (Visual Studio 2022, VC\\Tools\\MSVC\\*\\bin\\Hostx64\\x64\\dumpbin.exe)."""
import os, re, subprocess, sys
sys.dont_write_bytecode = True

# (object file, function) -> the values a longjmp to that site passes (the setjmp return values of the resumed paths)
EXPECTED = {
    ('unicorn_qemu_accel_tcg_cpu-exec.obj', 'cpu_exec_x86_64'): (1,),        # cpu_loop_exit (cpu-exec-common.c)
    ('unicorn_qemu_accel_tcg_translate-all.obj', 'uc_invalidate_tb'): (1,),   # uc_invalidate_tb_page inlined (U1594 loop)
    ('unicorn_qemu_accel_tcg_translate-all.obj', 'setjmp_gen_code'): (-2,),     # tcg_raise_tb_overflow (tcg.c); U2045 backport, never inlined
    ('unicorn_qemu_target_i386_translate.obj', 'disas_insn'): (1, 2),         # advance_pc (translate.c)
}
DEFAULT_VALUES = (1, 2, -1, -2)  # --any-sites: an unknown site is walked with every value a fork thrower uses
NORETURN = {'g_assertion_message_expr', 'abort', 'tcg_abort', 'cpu_abort', 'cpu_loop_exit_x86_64', 'cpu_loop_exit_restore_x86_64',
            'cpu_loop_exit_noexc_x86_64', 'cpu_loop_exit_atomic_x86_64', '__report_rangecheckfailure', '__report_gsfailure',
            'exit', '_exit', 'longjmp', 'siglongjmp'}
LAB = re.compile(r'^([A-Za-z_$?@][A-Za-z0-9_@?$]*):$')
INS = re.compile(r'^\s+([0-9A-F]+): (\S+)\s*(.*?)\s*$')
SLOT = re.compile(r'\[(rsp|rbp)([+-][0-9A-F]+h)?\]')
HEX = re.compile(r'^([0-9A-F]{4,16})$')
READ_ONLY_DST = {'cmp', 'test', 'bt', 'push', 'ucomiss', 'ucomisd', 'comiss', 'comisd'}
PURE_WRITE = {'mov', 'movaps', 'movups', 'movdqa', 'movdqu', 'movsd', 'movss', 'movq', 'movd', 'movzx', 'movsx', 'movsxd',
              'movapd', 'movupd', 'vmovaps', 'vmovups', 'vmovdqa', 'vmovdqu', 'vmovsd', 'vmovss', 'vmovq', 'vmovd', 'setb',
              'setae', 'sete', 'setne', 'setbe', 'seta', 'sets', 'setns', 'setl', 'setge', 'setle', 'setg', 'setp', 'setnp',
              'seto', 'setno', 'pop', 'stosb', 'stosd', 'stosq'}
GPR32 = {'eax': 'rax', 'ebx': 'rbx', 'ecx': 'rcx', 'edx': 'rdx', 'esi': 'rsi', 'edi': 'rdi', 'ebp': 'rbp', 'esp': 'rsp',
         **{'r%dd' % i: 'r%d' % i for i in range(8, 16)}}
GPR64 = set(GPR32.values())
CALL_CLOBBER = {'rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11'}

def full(r):
    r = r.strip().lower()
    if r in GPR32: return GPR32[r]
    if r in GPR64: return r
    return None

def operands(op):
    parts, depth, cur = [], 0, ''
    for ch in op:
        if ch == '[': depth += 1
        elif ch == ']': depth -= 1
        if ch == ',' and depth == 0:
            parts.append(cur); cur = ''; continue
        cur += ch
    parts.append(cur)
    return [p.strip() for p in parts if p.strip()]

def norm(s, fp=True):
    m = SLOT.search(s)
    if not m or (m.group(1) == 'rbp' and not fp): return None   # rbp is a frame slot base only with a frame pointer
    return '[%s%s]' % (m.group(1), m.group(2) or '')

def has_fp(body):
    """the prologue makes rbp a frame pointer (lea rbp, [rsp...] / mov rbp, rsp); otherwise [rbp+X] is ordinary memory"""
    return any((mn == 'lea' and op.replace(' ', '').startswith('rbp,[rsp')) or (mn == 'mov' and op.replace(' ', '') == 'rbp,rsp')
               for a, mn, op in body[:32])

def access(mn, op, fp=True):
    """(written slot or None, [read slots]) of one instruction (lea / nop: address only)"""
    if mn in ('lea', 'nop', 'call', 'jmp') or mn.startswith('j'): return None, []
    ops = operands(op)
    if not ops: return None, []
    if mn in READ_ONLY_DST: return None, [x for x in (norm(o, fp) for o in ops) if x]
    dst = norm(ops[0], fp); rd = [x for x in (norm(o, fp) for o in ops[1:]) if x]
    if dst and mn not in PURE_WRITE: rd.append(dst)
    if mn == 'xchg' and dst: rd.append(dst)
    return dst, rd

def parse(text):
    """{function: (body [(addr, mnemonic, operands)], labels {name: addr})}"""
    out, cur, body, labels, pend = {}, None, [], {}, []
    for l in text.split('\n'):
        l = l.rstrip('\r')
        m = LAB.match(l)
        if m:
            if not m.group(1).startswith('$'):
                if cur: out[cur] = (body, labels)
                cur, body, labels, pend = m.group(1), [], {}, []
            else:
                pend.append(m.group(1))
            continue
        mi = INS.match(l)
        if mi and cur:
            a = int(mi.group(1), 16)
            for p in pend: labels[p] = a
            pend = []
            body.append((a, mi.group(2), mi.group(3)))
    if cur: out[cur] = (body, labels)
    return out

def target(op, labels):
    t = op.strip()
    if HEX.match(t): return int(t, 16)
    if t in labels: return labels[t]
    return None

def callee(op):
    t = op.strip()
    return t.split()[-1] if t else t

def analyse(body, labels, call, values):
    """walk the resumed paths; returns dict(nodes, reads {slot: [addr]}, risk [(slot, first read, writes after)], indirect)"""
    idx = {a: i for i, (a, mn, op) in enumerate(body)}
    ci = idx[call]
    fp = has_fp(body)
    # writes after the call: reachable from the call (unconstrained), plus every write later in the layout
    reach, work = set(), [ci + 1]
    while work:
        i = work.pop()
        if i >= len(body) or i in reach: continue
        reach.add(i)
        a, mn, op = body[i]
        if mn in ('ret', 'int') or (mn == 'call' and callee(op) in NORETURN): continue
        if mn.startswith('j'):
            t = target(op, labels)
            if t is not None and t in idx: work.append(idx[t])
            if mn == 'jmp': continue
        work.append(i + 1)
    wafter = {}
    for i, (a, mn, op) in enumerate(body):
        if i > ci or i in reach:
            d, _ = access(mn, op, fp)
            if d: wafter.setdefault(d, []).append(a)
    # resumed paths: nodes (index, frozen known-register state); state = {reg: value} of the setjmp return value + flags
    nodes, succ, indirect = {}, {}, []
    start = [(ci + 1, (('rax', v),), None) for v in values]
    work = list(start)
    def key(n): return (n[0], n[1], n[2])
    while work:
        n = work.pop()
        k = key(n)
        if k in nodes: continue
        i, regs, flags = n
        nodes[k] = n
        succ[k] = []
        if i >= len(body): continue
        a, mn, op = body[i]
        R = dict(regs); F = flags
        ops = operands(op)
        nxt = []
        def kill(r):
            if r in R: del R[r]
        if mn in ('ret', 'int'): pass
        elif mn == 'call':
            if callee(op) not in NORETURN:
                for r in CALL_CLOBBER: kill(r)
                F = None
                nxt = [i + 1]
        elif mn.startswith('j'):
            t = target(op, labels)
            if t is None or t not in idx:
                indirect.append((a, op))
            if mn == 'jmp':
                if t is not None and t in idx: nxt = [idx[t]]
            else:
                taken = None
                if F is not None:
                    kind, x, y = F   # ('test', v) or ('cmp', v, imm) on 32-bit values
                    z = (x - y) & 0xFFFFFFFF if kind == 'cmp' else x & 0xFFFFFFFF
                    sx = lambda u: u - (1 << 32) if u & 0x80000000 else u
                    if mn in ('je', 'jz'): taken = z == 0
                    elif mn in ('jne', 'jnz'): taken = z != 0
                    elif kind == 'cmp' and mn in ('jl', 'jnge'): taken = sx(x & 0xFFFFFFFF) < sx(y & 0xFFFFFFFF)
                    elif kind == 'cmp' and mn in ('jge', 'jnl'): taken = sx(x & 0xFFFFFFFF) >= sx(y & 0xFFFFFFFF)
                    elif kind == 'cmp' and mn in ('jle', 'jng'): taken = sx(x & 0xFFFFFFFF) <= sx(y & 0xFFFFFFFF)
                    elif kind == 'cmp' and mn in ('jg', 'jnle'): taken = sx(x & 0xFFFFFFFF) > sx(y & 0xFFFFFFFF)
                    elif kind == 'test' and mn == 'js': taken = bool(z & 0x80000000)
                    elif kind == 'test' and mn == 'jns': taken = not (z & 0x80000000)
                tgt = [idx[t]] if (t is not None and t in idx) else []
                nxt = tgt if taken is True else [i + 1] if taken is False else tgt + [i + 1]
        else:
            # value tracking of the setjmp return: mov r32, r32 / test r, r / cmp r, imm; anything else writing a tracked
            # register or the flags makes it unknown
            if mn == 'mov' and len(ops) == 2 and full(ops[0]) and full(ops[1]) and full(ops[1]) in R:
                R[full(ops[0])] = R[full(ops[1])]
            elif mn == 'test' and len(ops) == 2 and full(ops[0]) and full(ops[0]) == full(ops[1]) and full(ops[0]) in R:
                F = ('test', R[full(ops[0])] & 0xFFFFFFFF, 0)
            elif mn == 'cmp' and len(ops) == 2 and full(ops[0]) in R and re.fullmatch(r'0?[0-9A-F]+h|\d+', ops[1].strip()):
                imm = ops[1].strip(); v = int(imm[:-1], 16) if imm.endswith('h') else int(imm)
                F = ('cmp', R[full(ops[0])] & 0xFFFFFFFF, v & 0xFFFFFFFF)
            else:
                if ops and full(ops[0]): kill(full(ops[0]))
                if mn not in ('mov', 'lea', 'nop', 'push', 'pop', 'movzx', 'movsx', 'movsxd') and not mn.startswith('mov'): F = None
                if mn in ('xchg',) and len(ops) == 2 and full(ops[1]): kill(full(ops[1]))
            nxt = [i + 1]
        st = tuple(sorted(R.items()))
        for j in nxt:
            kk = (j, st, F)
            succ[k].append(kk)
            work.append((j, st, F))
    # must-write dataflow over the resumed nodes
    preds = {k: [] for k in nodes}
    for k, ss in succ.items():
        for s in ss:
            if s in preds: preds[s].append(k)
    ALL = None  # top
    IN = {k: ALL for k in nodes}
    entries = [key(s) for s in start]
    for e in entries: IN[e] = frozenset()
    def out_of(k):
        i = k[0]
        if IN[k] is ALL or i >= len(body): return IN[k]
        d, _ = access(body[i][1], body[i][2], fp)
        return IN[k] | {d} if d else IN[k]
    changed = True
    order = list(nodes)
    while changed:
        changed = False
        for k in order:
            if k in entries: continue
            ps = [out_of(p) for p in preds[k]]
            ps = [p for p in ps if p is not ALL]
            if not ps: continue
            v = frozenset.intersection(*ps)
            if IN[k] is ALL or v != IN[k]:
                IN[k] = v; changed = True
    reads = {}
    for k in nodes:
        i = k[0]
        if i >= len(body) or IN[k] is ALL: continue
        _, r = access(body[i][1], body[i][2], fp)
        for s in r:
            if s not in IN[k]: reads.setdefault(s, set()).add(body[i][0])
    risk = [(s, min(reads[s]), sorted(wafter[s])) for s in sorted(reads) if s in wafter]
    return {'fp': fp, 'nodes': len({k[0] for k in nodes}), 'reads': reads, 'risk': risk, 'indirect': sorted(set(indirect)), 'wafter': wafter}

def report(name, call, res, verbose):
    print('site %s: setjmp call at %Xh; resumed paths: %d instructions; stack slots read at setjmp-time value: %d; RISK %d; '
          'indirect jumps on resumed paths: %d' % (name, call, res['nodes'], len(res['reads']), len(res['risk']), len(res['indirect'])))
    for s in sorted(res['reads']):
        w = res['wafter'].get(s, [])
        tag = 'RISK' if w else 'safe'
        if verbose or w:
            print('   %-4s read %-12s at %-24s writes after the call: %d%s' % (tag, s, ','.join('%Xh' % a for a in sorted(res['reads'][s])[:4]),
                  len(w), (' (' + ','.join('%Xh' % a for a in w[:6]) + (',...' if len(w) > 6 else '') + ')') if w else ''))
    for a, op in res['indirect'][:5]:
        print('   UNANALYSABLE indirect jump at %Xh: jmp %s' % (a, op))

def find_dumpbin(explicit):
    if explicit: return explicit
    if os.environ.get('NOVMP_DUMPBIN'): return os.environ['NOVMP_DUMPBIN']
    vsw = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    if not os.path.isfile(vsw): return None
    r = subprocess.run([vsw, '-version', '[17.0,18.0)', '-latest', '-products', '*', '-find', r'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe'],
                       capture_output=True, text=True)
    c = [l.strip() for l in r.stdout.splitlines() if l.strip().lower().endswith('dumpbin.exe') and os.path.isfile(l.strip())]
    return sorted(c)[-1] if c else None

def run(objdir, dumpbin, expect_risk, any_sites, verbose):
    dumpbin = find_dumpbin(dumpbin)
    if not dumpbin:
        print('FAIL: dumpbin.exe not found (--dumpbin / NOVMP_DUMPBIN / vswhere)'); return 1
    if not os.path.isdir(objdir):
        print('FAIL: object folder not found: %s' % objdir); return 1
    objs = sorted(f for f in os.listdir(objdir) if f.lower().endswith('.obj'))
    cand = [f for f in objs if b'_setjmp_wrapper' in open(os.path.join(objdir, f), 'rb').read() and not f.lower().startswith('setjmp-wrapper')]
    print('objects: %d in %s; naming _setjmp_wrapper: %d (%s)' % (len(objs), objdir, len(cand), ', '.join(cand)))
    sites, total_risk, bad = [], 0, []
    for f in cand:
        r = subprocess.run([dumpbin, '/nologo', '/disasm:nobytes', os.path.join(objdir, f)], capture_output=True)
        if r.returncode != 0:
            print('FAIL: dumpbin exit %d on %s' % (r.returncode, f)); return 1
        fn = parse(r.stdout.decode('utf-8', 'replace'))
        for name, (body, labels) in sorted(fn.items()):
            calls = [a for a, mn, op in body if mn == 'call' and callee(op) == '_setjmp_wrapper']
            for c in calls:
                vals = EXPECTED.get((f, name), DEFAULT_VALUES)
                res = analyse(body, labels, c, vals)
                sites.append((f, name))
                report('%s :: %s (longjmp values %s)' % (f, name, ','.join(map(str, vals))), c, res, verbose)
                total_risk += len(res['risk'])
                if res['indirect']: bad.append('%s :: %s unanalysable' % (f, name))
    got = set(sites)
    if not any_sites:
        if got != set(EXPECTED) or len(sites) != len(EXPECTED):
            bad.append('site set differs from EXPECTED: missing %s, new %s, %d calls' % (sorted(set(EXPECTED) - got), sorted(got - set(EXPECTED)), len(sites)))
    want = 0 if expect_risk is None else expect_risk
    if total_risk != want: bad.append('RISK %d, expected %d' % (total_risk, want))
    print('setjmp sites: %d, RISK reads: %d (expected %d)' % (len(sites), total_risk, want))
    if bad:
        print('RESULT: FAIL (' + '; '.join(bad) + ')'); return 1
    print('RESULT: OK'); return 0

# ---------------------------------------------------------------------------------------------------------------- selftest
ST = {
 # (a) the U2043 shape: case 2 reads two slots the zero path reuses -> 2 RISK
 'a_reuse': ('''f:
  00F0: lea         rbp,[rsp-100h]
  0000: mov         dword ptr [rsp+50h],ecx
  0004: mov         qword ptr [rbp-80h],rcx
  0008: call        _setjmp_wrapper
  000D: test        eax,eax
  000F: je          0030
  0011: mov         eax,dword ptr [rsp+50h]
  0015: mov         dword ptr [rbx+64h],eax
  0018: mov         rax,qword ptr [rbp-80h]
  001C: ret
  0030: mov         dword ptr [rsp+50h],edx
  0034: mov         qword ptr [rbp-80h],rax
  0038: call        advance_pc
  003D: ret
''', (2,), 2, 0),
 # (b) only nonvolatile registers / memory through them on the resumed path (U2043 fixed shape) -> 0
 'b_regs': ('''f:
  0000: mov         dword ptr [rsp+50h],ecx
  0004: call        _setjmp_wrapper
  0009: test        eax,eax
  000B: je          0020
  000D: mov         eax,dword ptr [rbx+24Ch]
  0013: mov         dword ptr [rbx+90h],eax
  0019: ret
  0020: mov         dword ptr [rsp+50h],edx
  0024: ret
''', (2,), 0, 0),
 # (c) the value constraint: jne taken for -2 only; the -1 branch (normal return only) is not walked -> 1 RISK, not 2
 'c_value': ('''f:
  0000: call        _setjmp_wrapper
  0005: mov         edi,eax
  0007: test        eax,eax
  0009: jne         0020
  000B: mov         dword ptr [rsp+90h],eax
  0012: mov         dword ptr [rsp+88h],eax
  0019: ret
  0020: cmp         edi,0FFFFFFFEh
  0023: jne         0030
  0025: mov         eax,dword ptr [rsp+90h]
  002C: ret
  0030: mov         eax,dword ptr [rsp+88h]
  0037: ret
''', (-2,), 1, 0),
 # (d) the U2045 shape: the resumed path writes the slot first (from the context) -> 0
 'd_mustwrite': ('''f:
  0000: call        _setjmp_wrapper
  0005: mov         edi,eax
  0007: test        eax,eax
  0009: jne         0020
  000B: mov         dword ptr [rsp+90h],eax
  0012: ret
  0020: mov         eax,dword ptr [rsi+0AA80h]
  0026: mov         dword ptr [rsp+90h],eax
  002D: mov         eax,dword ptr [rsp+90h]
  0034: ret
''', (-2,), 0, 0),
 # (e) a write on only one of two paths before the join -> RISK 1 (must, not may)
 'e_join': ('''f:
  0000: call        _setjmp_wrapper
  0005: mov         dword ptr [rsp+40h],eax
  0009: call        g
  000E: test        al,al
  0010: je          0018
  0012: mov         qword ptr [rsp+70h],r15
  0017: nop
  0018: mov         r15,qword ptr [rsp+70h]
  001D: ret
''', (1,), 1, 0),
 # (f) shrink-wrapped saves: written before every read on the resumed path (cpu_exec shape) -> 0
 'f_shrinkwrap': ('''f:
  0000: call        _setjmp_wrapper
  0005: call        g
  000A: test        al,al
  000C: jne         0030
  000E: mov         qword ptr [rsp+70h],r15
  0013: call        h
  0018: test        al,al
  001A: je          0013
  001C: mov         r15,qword ptr [rsp+70h]
  0030: ret
''', (1,), 0, 0),
 # (g) a noreturn call ends the path (g_assert_not_reached) -> the zero-path read after it is not walked -> 0
 'g_noreturn': ('''f:
  0000: call        _setjmp_wrapper
  0005: test        eax,eax
  0007: je          0020
  0009: call        g_assertion_message_expr
  0020: mov         eax,dword ptr [rsp+44h]
  0024: mov         dword ptr [rsp+44h],ecx
  0028: ret
''', (1,), 0, 0),
 # (h) an indirect jump on a resumed path -> unanalysable
 'h_indirect': ('''f:
  0000: call        _setjmp_wrapper
  0005: test        eax,eax
  0007: je          0010
  0009: jmp         rcx
  0010: ret
''', (1,), 0, 1),
 # (i) a write placed later in the layout but only reachable through a jump table still counts as "after the call" -> RISK 1
 'i_layout': ('''f:
  0000: mov         dword ptr [rsp+58h],ecx
  0004: call        _setjmp_wrapper
  0009: test        eax,eax
  000B: je          0014
  000D: mov         eax,dword ptr [rsp+58h]
  0011: ret
  0014: jmp         rdx
  0016: mov         dword ptr [rsp+58h],eax
  001A: ret
''', (1,), 1, 0),
}

def selftest():
    bad = 0
    for name, (text, vals, want_risk, want_ind) in sorted(ST.items()):
        fn = parse(text)
        body, labels = fn['f']
        call = [a for a, mn, op in body if mn == 'call' and callee(op) == '_setjmp_wrapper'][0]
        res = analyse(body, labels, call, vals)
        ok = len(res['risk']) == want_risk and len(res['indirect']) == want_ind
        print('selftest %-14s RISK %d (want %d), indirect %d (want %d): %s' % (name, len(res['risk']), want_risk, len(res['indirect']), want_ind, 'ok' if ok else 'MISMATCH'))
        bad += 0 if ok else 1
    print('selftest: %d cases, %d mismatches' % (len(ST), bad))
    return 1 if bad else 0

def main(argv):
    if '--selftest' in argv: return selftest()
    args, dumpbin, er, anys, verbose = [], None, None, False, False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--dumpbin': dumpbin = argv[i + 1]; i += 2; continue
        if a == '--expect-risk': er = int(argv[i + 1]); i += 2; continue
        if a == '--any-sites': anys = True; i += 1; continue
        if a == '-v': verbose = True; i += 1; continue
        args.append(a); i += 1
    if len(args) != 1:
        print(__doc__); return 2
    rc = selftest()
    if rc: print('RESULT: FAIL (selftest)'); return 1
    return run(args[0], dumpbin, er, anys, verbose)

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
