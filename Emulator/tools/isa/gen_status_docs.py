"""Generate the repository status documents from the instruction table data.

Writes (relative to the NoVmp root):
  docs/instructions.md                       index + what is being added right now
  docs/Intel_instruction_sets_supported.md   every Intel instruction-set family, per-instruction status
  docs/AMD_instruction_sets_supported.md     AMD / VIA families (deferred until Intel is complete)
  README.md                                  the block between <!-- NOVMP-STATUS:BEGIN --> and <!-- NOVMP-STATUS:END -->

Inputs: build/instruction_table.json (written by gen_instruction_table.py), CHANGES_LEDGER.md,
../PLAN_emulation.md (the plan lives next to the repository), git HEAD.

usage: python Emulator/tools/isa/gen_status_docs.py   (run from the NoVmp root)
"""
import datetime
import json
import os
import re
import subprocess

ROOT = os.getcwd()
PLAN = os.path.normpath(os.path.join(ROOT, '..', 'PLAN_emulation.md'))
DATA = os.path.join(ROOT, 'build', 'instruction_table.json')
LEDGER = os.path.join(ROOT, 'CHANGES_LEDGER.md')
DOCS = os.path.join(ROOT, 'docs')
BEGIN, END = '<!-- NOVMP-STATUS:BEGIN -->', '<!-- NOVMP-STATUS:END -->'

LEGEND = ('**How to read the tables.** Two separate questions per instruction: **"your i5-13600K"** = can '
          'your CPU execute it at all (✅ runs / ❌ **cannot run** — CPUID bit clear, AMD/VIA-only, or disabled by '
          'Windows; these can never be checked against your hardware) and **"emulator"** = what the emulator does '
          '(✅ identical to the CPU, or implemented per the manual and verified against SDM-pseudocode vectors when '
          'the CPU cannot run it · ⏳ open item · ⬜ not implemented yet).')


def git(*args):
    try:
        return subprocess.check_output(['git', '-c', 'safe.directory=*'] + list(args), cwd=ROOT,
                                       stderr=subprocess.DEVNULL).decode('utf-8', 'replace').strip()
    except Exception:
        return ''


def short(note, n=170):
    note = re.sub(r'\*\*', '', note).replace('|', '/')
    return note if len(note) <= n else note[:n - 1] + '…'


def impl_kind(row):
    n = row['note']
    if row['status'] != '❌':
        return row['status']
    if 'implemented per the manual (SDM-vector verified)' in n:
        return '❌ (implemented per manual)'
    if 'implemented per the manual, open item' in n:
        return '❌ (implemented, open item)'
    return '❌ (not implemented yet)'


def cpu_cell(row):
    """'✅ runs' or '❌ cannot run (reason)' for our i5-13600K"""
    if row['status'] != '❌':
        return '✅ runs'
    n = re.sub(r'\*\*', '', row['note'])
    m = re.search(r'NOT SUPPORTED on our i5-13600K \((.*?)\) —', n)
    why = m.group(1) if m else 'not reported by this CPU'
    return '❌ **cannot run** (%s)' % why.replace('|', '/')


def emu_cell(row):
    """emulator status without the CPU part"""
    n = re.sub(r'\*\*', '', row['note'])
    if row['status'] != '❌':
        return '%s %s' % (row['status'], short(n))
    rest = n.split(' — ', 1)[1] if ' — ' in n else n
    k = impl_kind(row)
    tag = {'❌ (implemented per manual)': '✅ implemented per the manual (SDM-vector verified)',
           '❌ (implemented, open item)': '⏳ implemented per the manual, open item',
           '❌ (not implemented yet)': '⬜ not implemented yet'}[k]
    detail = rest.split(': ', 1)[1] if ': ' in rest else ''
    return tag + (' — ' + short(detail, 140) if detail else '')


def family_table(rows):
    fams = {}
    for r in rows:
        f = fams.setdefault(r['family'], {'n': 0, 'cpu': 0, '✅': 0, '⏳': 0, '⬜': 0, 'xi': 0, 'xo': 0, 'xn': 0})
        f['n'] += 1
        if r['status'] != '❌':
            f['cpu'] += 1
        k = impl_kind(r)
        if k == '❌ (implemented per manual)':
            f['xi'] += 1
        elif k == '❌ (implemented, open item)':
            f['xo'] += 1
        elif k.startswith('❌'):
            f['xn'] += 1
        else:
            f[k] += 1
    out = ['| family | forms | **runs on your i5-13600K?** | ✅ identical to the CPU | ⏳ open item | ⬜ not done | '
           '❌→ implemented per manual | ❌→ open item | ❌→ not implemented yet |',
           '|---|---|---|---|---|---|---|---|---|']
    for name in sorted(fams, key=lambda x: (x.startswith(('AVX512', 'AVX10', 'APX', 'AMX')), x)):
        f = fams[name]
        if f['cpu'] == f['n']:
            runs = '✅ yes'
        elif f['cpu'] == 0:
            runs = '❌ **no — cannot be supported on this CPU**'
        else:
            runs = '⚠️ partly (%d of %d forms)' % (f['cpu'], f['n'])
        out.append('| %s | %d | %s | %d | %d | %d | %d | %d | %d |' % (
            name, f['n'], runs, f['✅'], f['⏳'], f['⬜'], f['xi'], f['xo'], f['xn']))
    return out


def details(rows):
    out = []
    by = {}
    for r in rows:
        by.setdefault(r['family'], []).append(r)
    for name in sorted(by, key=lambda x: (x.startswith(('AVX512', 'AVX10', 'APX', 'AMX')), x)):
        rs = by[name]
        out.append('<details><summary><b>%s</b> (%d forms)</summary>\n' % (name, len(rs)))
        out.append('| instruction | encoding | vector bits | **your i5-13600K** | emulator |')
        out.append('|---|---|---|---|---|')
        for r in rs:
            out.append('| %s | %s | %s | %s | %s |' % (r['mnemonic'].upper(), r['encoding'], r['vl'],
                                                      cpu_cell(r), emu_cell(r)))
        out.append('\n</details>\n')
    return out


def plan_lines():
    try:
        lines = open(PLAN, encoding='utf-8').read().split('\n')
    except OSError:
        return [], []
    status, adding = [], []
    skip = False
    for l in lines[2:80]:
        if l.startswith('- Done items') or l.startswith('## '):
            break
        if l.startswith('- '):
            skip = l.startswith('- ⏳ **1.15') or l.startswith('- ✅ **1.15')
        if not skip and (l.startswith('- ') or l.startswith('  - ')):
            status.append(l)
    in115 = False
    for l in lines:
        if l.startswith('- ⏳ **1.15') or l.startswith('- ✅ **1.15'):
            in115 = True
            continue
        if in115 and not l.startswith('  '):
            break
        if in115 and re.match(r'\s+- (⏳|⬜)', l):
            adding.append(l)
    return status, adding


def ledger_summary():
    try:
        rows = [l for l in open(LEDGER, encoding='utf-8').read().split('\n') if re.match(r'\| U\d+ \|', l)]
    except OSError:
        return 0, []
    last = []
    for l in rows[-12:]:
        c = [x.strip() for x in l.split('|')]
        last.append('- %s — %s' % (c[1], short(c[2], 140)))
    return len(rows), last


def main():
    data = json.load(open(DATA, encoding='utf-8'))
    rows = data['rows']
    intel = [r for r in rows if not r.get('vendor')]
    amd = [r for r in rows if r.get('vendor')]
    t = data['totals']
    u = data.get('unsupported_impl', {})
    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    head = git('log', '-1', '--format=%h %s')
    status, adding = plan_lines()
    nledger, last = ledger_summary()

    def cnt(rs, k):
        return sum(1 for r in rs if impl_kind(r) == k or (k == '❌' and r['status'] == '❌'))

    os.makedirs(DOCS, exist_ok=True)

    # ---- Intel
    o = ['# Intel instruction sets supported by the NoVmp emulator', '',
         '_Generated %s from `Emulator/tools/isa/gen_status_docs.py` (HEAD `%s`). Do not edit by hand._' % (now, head), '',
         LEGEND, '',
         'Source of truth: every instruction form of the Intel SDM / XED list (`Emulator/data/isa_manual_forms.tsv`), '
         'checked against an Intel i5-13600K (Raptor Lake) with `emu-alltest` (hardware sweeps, `--cases` files) and, '
         'for instructions this CPU lacks, against expected values derived from the SDM pseudocode.', '',
         '**Totals (Intel families):** ✅ %d · ⏳ %d · ⬜ %d · ❌ %d (of which implemented per the manual %d, open item %d, not implemented yet %d) — %d forms' % (
             cnt(intel, '✅'), cnt(intel, '⏳'), cnt(intel, '⬜'), cnt(intel, '❌'),
             cnt(intel, '❌ (implemented per manual)'), cnt(intel, '❌ (implemented, open item)'),
             cnt(intel, '❌ (not implemented yet)'), len(intel)), '',
         '## Currently being added', ''] + (adding or ['- (nothing in progress)']) + ['', '## By family', ''] + \
        family_table(intel) + ['', '## Per instruction', ''] + details(intel)
    open(os.path.join(DOCS, 'Intel_instruction_sets_supported.md'), 'w', encoding='utf-8').write('\n'.join(o) + '\n')

    # ---- AMD / VIA
    o = ['# AMD (and VIA) instruction sets', '',
         '_Generated %s from `Emulator/tools/isa/gen_status_docs.py` (HEAD `%s`). Do not edit by hand._' % (now, head), '',
         'These instruction sets are **not in the Intel manuals** and our CPU (Intel i5-13600K) **cannot run them**, '
         'so every row is ❌ NOT SUPPORTED on our CPU.', '',
         '- Decision (2026-10-07): implement them **after every Intel instruction is done** (plan 1.15f), from the vendor manuals '
         '(AMD APM Vol 2–5, AMD LWP spec, VIA PadLock guide → `emulator/Amd Handbooks/`).',
         '- A switch `__use_AMD_instruction_set__` (default 0 = Intel instruction set) will gate them: with 0 they are #UD as on Intel.',
         '- Some (3DNow!, SSE4a, FEMMS) already exist in QEMU and run under the non-strict MAX model; under the i5-13600K '
         'profile with `--strict` they are #UD like the CPU.', '',
         '**Totals:** %d forms, implemented per the manual %d, not implemented yet %d' % (
             len(amd), cnt(amd, '❌ (implemented per manual)') + cnt(amd, '❌ (implemented, open item)'),
             cnt(amd, '❌ (not implemented yet)')), '',
         '## By family', ''] + family_table(amd) + ['', '## Per instruction', ''] + details(amd)
    open(os.path.join(DOCS, 'AMD_instruction_sets_supported.md'), 'w', encoding='utf-8').write('\n'.join(o) + '\n')

    # ---- index
    o = ['# Instruction support — index', '',
         '_Generated %s (HEAD `%s`); refreshed every 30 minutes while work is in progress._' % (now, head), '',
         '- [Intel instruction sets supported](Intel_instruction_sets_supported.md)',
         '- [AMD / VIA instruction sets](AMD_instruction_sets_supported.md)', '', LEGEND, '',
         '**All forms:** ✅ %d · ⏳ %d · ⬜ %d · ❌ %d (implemented per the manual %d, open item %d, not implemented yet %d)' % (
             t.get('✅', 0), t.get('⏳', 0), t.get('⬜', 0), t.get('❌', 0), u.get('✅', 0), u.get('⏳', 0), u.get('⬜', 0)), '',
         '## Currently being added', ''] + (adding or ['- (nothing in progress)']) + \
        ['', '## Latest ledger entries (`CHANGES_LEDGER.md`, %d rows)' % nledger, ''] + last
    open(os.path.join(DOCS, 'instructions.md'), 'w', encoding='utf-8').write('\n'.join(o) + '\n')

    # ---- README status block
    block = [BEGIN, '## Status of this fork (NoVmp emulator)', '',
             '_Updated %s — branch `novmp-emu`, HEAD `%s`. Generated by `Emulator/tools/isa/gen_status_docs.py`; '
             'refreshed every 30 minutes while work is in progress._' % (now, head), '',
             'This fork turns NoVmp into a **pure-emulation** devirtualizer: the packed sample is never run natively; '
             'a Unicorn/QEMU 7.2 fork (in-tree, `unicorn/`) is being extended until every Intel-manual instruction is emulated '
             'exactly (verified against an Intel i5-13600K, or against SDM-pseudocode vectors where the CPU lacks the instruction). '
             'Every change to QEMU code sits under one switch `__Use_Original_Qemu` (0 = ours, 1 = stock QEMU) and is recorded in '
             '[`CHANGES_LEDGER.md`](CHANGES_LEDGER.md) (%d entries).' % nledger, '',
             '### Instruction support', '',
             '- ✅ %d · ⏳ %d · ⬜ %d · **❌ %d not supported on our CPU** (implemented per the manual %d, open item %d, not implemented yet %d)' % (
                 t.get('✅', 0), t.get('⏳', 0), t.get('⬜', 0), t.get('❌', 0), u.get('✅', 0), u.get('⏳', 0), u.get('⬜', 0)),
             '- Details: [docs/instructions.md](docs/instructions.md) · [Intel](docs/Intel_instruction_sets_supported.md) · '
             '[AMD / VIA](docs/AMD_instruction_sets_supported.md)', '',
             '### Plan status', ''] + status + ['', '### Currently being added', ''] + (adding or ['- (nothing in progress)']) + [
             '', '### Build and test', '',
             '```bash', 'build.cmd --release', '```', '```bash', 'test.cmd', '```',
             'Release x64 only (Visual Studio 2022, hand-maintained `.vcxproj`/`.sln`, no CMake).', '', END]
    rp = os.path.join(ROOT, 'README.md')
    raw = open(rp, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    s = raw.replace('\r\n', '\n')
    blk = '\n'.join(block)
    if BEGIN in s:
        s = s[:s.index(BEGIN)] + blk + s[s.index(END) + len(END):]
    else:
        anchor = '### VMProtect? Nope.'
        i = s.find(anchor)
        s = (s[:i] + blk + '\n\n' + s[i:]) if i >= 0 else (blk + '\n\n' + s)
    open(rp, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
    print('docs written: Intel %d forms, AMD/VIA %d forms, %d in-progress lines' % (len(intel), len(amd), len(adding)))


main()
