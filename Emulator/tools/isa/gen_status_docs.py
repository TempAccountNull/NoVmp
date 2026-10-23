"""Generate the repository status documents from the instruction table data.

Writes (relative to the NoVmp root):
  docs/instructions.md                       index: legend, totals per privilege class, what is being added, latest ledger rows
  docs/Intel_instruction_sets_supported.md   THE instruction page: every form (Intel, AMD, VIA) exactly once, split into
                                             "Usermode (no emu driver required)" / "Kernel mode required" / "Needs review"
  docs/AMD_instruction_sets_supported.md     3-line stub pointing to the page above (P26: one table for both vendors)
  README.md                                  the block between <!-- NOVMP-STATUS:BEGIN --> and <!-- NOVMP-STATUS:END -->

Inputs: build/instruction_table.json (written by gen_instruction_table.py), Emulator/data/isa_privilege.tsv (privilege
class, P26), CHANGES_LEDGER.md, ../PLAN_emulation.md (the plan lives next to the repository; --plan overrides), git HEAD.

usage: python Emulator/tools/isa/gen_status_docs.py [--plan PATH] [--data PATH]   (run from the NoVmp root)
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import textwrap

ROOT = os.getcwd()
_ap = argparse.ArgumentParser()
_ap.add_argument('--plan', default=os.path.normpath(os.path.join(ROOT, '..', 'PLAN_emulation.md')))
_ap.add_argument('--data', default=os.path.join(ROOT, 'build', 'instruction_table.json'))
ARGS = _ap.parse_args()
PLAN = ARGS.plan
DATA = ARGS.data
PRIV = os.path.join(ROOT, 'Emulator', 'data', 'isa_privilege.tsv')
FORMS_TSV = os.path.join(ROOT, 'Emulator', 'data', 'isa_manual_forms.tsv')
LEDGER = os.path.join(ROOT, 'CHANGES_LEDGER.md')
DOCS = os.path.join(ROOT, 'docs')
BEGIN, END = '<!-- NOVMP-STATUS:BEGIN -->', '<!-- NOVMP-STATUS:END -->'
MAIN_PAGE = 'Intel_instruction_sets_supported.md'
MAIN_TITLE = 'Instruction sets supported (Intel + AMD/VIA)'
WIDTH = 160                     # prose lines are wrapped here (P26: no prose line above ~200 characters)

# P26: one legend for docs/instructions.md and the main page (replaces the "How the page is split" paragraph)
LEGEND = [
    '| Mark | Meaning |',
    '|---|---|',
    '| ✅ **Done** | Implemented and identical to your i5-13600K (hardware sweep / hardware cases); for a form the CPU '
    'cannot run: implemented per the manual and verified against SDM-pseudocode vectors (or the independent model '
    'named in "verified by"). |',
    '| ⏳ **Implementing** | Implemented with an open item (named in "verified by"), or being implemented now (agent named). |',
    '| ⬜ **Queued** | Not implemented yet (not started). |',
    '| ❌ **Cannot run** | Your i5-13600K cannot run the form: CPUID bit clear, AMD/VIA-only, or disabled by Windows; '
    'the reason is in the **i5-13600K** column. Never compared with your hardware. The Status cell adds the emulator '
    'state: `❌ · ✅ per manual`, `❌ · ✅ gating only`, `❌ · ⏳ open item`, `❌ · ⬜ not implemented yet`. |',
    '| **Per profile** `--guest` / `--host` | The i5-13600K column follows the VMware guest (`--guest` / `--vm`, where '
    'the hardware cases run); each reason says whether it also holds on the bare host (`--host`); forms the bare host '
    'enumerates say "--host: enumerated" (U1632, plan 1.J.57). |',
    '| **Gating only** | Not enumerated on either profile; CPUID + #UD verified on both (`Emulator/data/cases_gating*.txt`; '
    'your decision 2026-10-17, G10). |',
    '| **Usermode** / **Kernel mode** | Privilege class (`Emulator/data/isa_privilege.tsv`): kernel mode = the form '
    'cannot execute at CPL3 under Windows (CPL0, VMX / SEAM, IOPL, CR4.PCE) and needs the emulator driver; the reason '
    'is in the **Why kernel** column. |',
]

# families an agent is working on right now: (family prefix, who). P26: emptied - plan 1.15 is done (✅) and its
# "Currently being added" list is empty; the old entries named agents that finished long ago (m2/m3/fp16/avx10).
# Add an entry while an agent works on a family (prefix match on the family name).
IN_PROGRESS = []

# P26: vendor of a row (gen_instruction_table.py CPU_NOT families); AMD-originated forms the i5-13600K runs = both
VIA_FAMS = ('VIA_PADLOCK_AES', 'VIA_PADLOCK_SHA', 'VIA_PADLOCK_RNG', 'VIA_PADLOCK_MONTMUL')
# P26: XED lists the legacy / REX prefixes as forms with no ISA set (family "?"): their own family; FWAIT
# aliases (FCLEX = 9B + FNCLEX, ...) take the family of the FN* form they wrap
PREFIX_FORMS = {'addr32', 'data16', 'rex64', 'lock', 'rep', 'repe', 'repne', 'repnz', 'repz', 'bnd', 'notrack'}
FWAIT_ALIAS = {'fclex': 'fnclex', 'finit': 'fninit', 'fstcw': 'fnstcw', 'fstsw': 'fnstsw', 'fsave': 'fnsave',
               'fstenv': 'fnstenv'}
ENC_ORDER = {'legacy': 0, 'vex': 1, 'xop': 2, 'evex': 3}


def in_progress(fam):
    best = None
    for k, who in IN_PROGRESS:
        if (fam == k or fam.startswith(k)) and (best is None or len(k) > len(best[0])):
            best = (k, who)
    return best[1] if best else None


def git(*args):
    try:
        return subprocess.check_output(['git', '-c', 'safe.directory=*'] + list(args), cwd=ROOT,
                                       stderr=subprocess.DEVNULL).decode('utf-8', 'replace').strip()
    except Exception:
        return ''


def short(note, n=170):
    """cut at a word boundary (P26: never mid-word) and mark the cut with an ellipsis"""
    note = re.sub(r'\*\*', '', note).replace('|', '/').strip()
    if len(note) <= n:
        return note
    cut = note[:n - 1]
    sp = cut.rfind(' ')
    if sp > n // 2:
        cut = cut[:sp]
    return cut.rstrip(' ,;:—-') + ' …'


def wrap(line):
    """wrap a prose line / list item at WIDTH (tables, headings and short lines untouched)"""
    if len(line) <= WIDTH or line.startswith(('|', '#', '<')):
        return [line]
    m = re.match(r'(\s*)([-*] |\d+\. )?', line)
    lead = m.group(1) + (m.group(2) or '')
    return textwrap.wrap(line, WIDTH, subsequent_indent=' ' * len(lead), break_long_words=False,
                         break_on_hyphens=False)


def prose(lines):
    out = []
    for l in lines:
        out += wrap(l)
    return out


# ---- row classification ----------------------------------------------------------------------------------------
def impl_kind(row):
    n = row['note']
    if row['status'] != '❌':
        return row['status']
    if 'gating only: ' in n:                                # U1631
        return '❌ (gating only)'
    # U1094: rows checked against an independent model of a non-SDM spec, or only in the absent-feature form
    if ('implemented per the manual (SDM-vector verified)' in n or 'spec-model verified' in n or
            'only the absent-feature form is hardware-checked' in n or 'documented compliance' in n):
        return '❌ (implemented per manual)'
    if 'implemented per the manual, open item' in n:
        return '❌ (implemented, open item)'
    return '❌ (not implemented yet)'


def parts(row):
    """(emulator state ✅/⏳/⬜, detail text, why the i5-13600K cannot run it or '')"""
    n = re.sub(r'\*\*', '', row['note'])
    if row['status'] != '❌':
        return row['status'], n, ''
    m = re.search(r'NOT SUPPORTED on our i5-13600K \((.*?)\) — (.*)', n)
    why = m.group(1) if m else 'not reported by this CPU'
    rest = m.group(2) if m else n
    st = {'❌ (implemented per manual)': '✅', '❌ (gating only)': '✅', '❌ (implemented, open item)': '⏳',
          '❌ (not implemented yet)': '⬜'}[impl_kind(row)]
    detail = rest.split(': ', 1)[1] if ': ' in rest else rest
    return st, detail, why


def status_cell(row):
    st, detail, why = parts(row)
    who = in_progress(row['family'])
    if why:
        k = impl_kind(row)
        tail = {'❌ (implemented per manual)': '✅ per manual', '❌ (gating only)': '✅ gating only',
                '❌ (implemented, open item)': '⏳ open item', '❌ (not implemented yet)': '⬜ not implemented yet'}[k]
        if st == '⬜' and who:
            tail = '⏳ being implemented (%s)' % who
        return '❌ · ' + tail
    if st == '✅':
        return '✅'
    if st == '⏳':
        return '⏳ open item'
    return '⏳ being implemented (%s)' % who if who else '⬜ queued'


# "verified by": the evidence kind first, then the row's own detail (shortened)
VERIFIED = [
    (r'^identical to the i5-13600K \((\d+) forms(; [^)]*)?\)', r'hardware sweep (\1 forms\2)'),
    (r'^identical to the i5-13600K \(cases_reach\): ', 'hardware (cases_reach): '),
    (r'^#UD in 64-bit mode, identical to the CPU \(one-byte-map sweep\); ', 'hardware sweep: #UD in 64-bit mode; '),
    (r'^implemented; the i5-13600K lacks it: verified against SDM-pseudocode vectors(: )?', 'SDM vectors: '),
    (r'^implemented; the VMware guest hides it \(the bare i5-13600K host enumerates it\): verified against '
     r'SDM-pseudocode vectors(: )?', 'SDM vectors (the guest hides it): '),
    (r'^implemented; not run natively by the sweep \(host state\): verified against SDM-pseudocode vectors(: )?',
     'SDM vectors (host state): '),
    (r'^implemented; the i5-13600K lacks it: specification-validated against an independent model(: )?', 'spec model: '),
    (r'^implemented; documented compliance \(the documented error bound and special cases, independent model; NOT '
     r'bit-exact\)(: )?', 'documented bound, not bit-exact: '),
    (r'^implemented; CPL0 only \(user-mode code cannot execute it, so it is never compared with the i5-13600K\): '
     r'verified against SDM-pseudocode vectors(: )?', 'SDM vectors (CPL0 only): '),
    (r'^#UD: not enumerated on the i5-13600K profiles \(--host and --guest\), CPUID \+ #UD verified '
     r'\(cases_gating\.txt\)(: )?', 'gating (CPUID + #UD, --host and --guest): '),
    (r'^the i5-13600K lacks the feature: the CPU and the emulator execute the encoding with the absent-feature '
     r'behaviour \(NOP / hint\), identical(: )?', 'hardware, absent-feature behaviour (NOP / hint): '),
    (r'^the i5-13600K lacks the feature: only the absent-feature behaviour \(NOP\) is hardware-checked; '
     r'feature-enabled semantics open(: )?', 'hardware, absent-feature only; feature semantics open: '),
    (r'^CPL0 instruction \((\d+) forms\): CPL3 fault check in Phase 3 \(D6\)', r'open: CPL3 fault check, Phase 3 (D6)'),
    (r'^out of scope: ', 'not implemented (out of scope until plan 1.15f): '),
    (r' — CPL0 form: its CPL3 fault \(vs the i5-13600K\) is a Phase 3 item \(D6\)', '; CPL3 fault: Phase 3 (D6)'),
    (r' — evidence label: ', '; '),
]


def verified_cell(row):
    st, detail, why = parts(row)
    d = detail
    for rx, rep in VERIFIED:
        d = re.sub(rx, rep, d)
    return short(d.rstrip(': '), 150)


def vendor(row):
    if not row.get('vendor'):
        return 'Intel'
    v = 'VIA' if row['family'] in VIA_FAMS else 'AMD'
    return 'both' if row['status'] != '❌' else v      # AMD-originated forms the i5-13600K runs (LZCNT, SYSCALL, SYSRET)


def fix_family(rows):
    """P26: the 17 rows of family '?' -> X87 (FWAIT aliases, family of the wrapped FN* form) or PREFIX"""
    fam = {(r['mnemonic'], r['encoding']): r['family'] for r in rows}
    moved = {}
    for r in rows:
        if r['family'] != '?':
            continue
        if r['mnemonic'] in FWAIT_ALIAS and (FWAIT_ALIAS[r['mnemonic']], r['encoding']) in fam:
            r['family'] = fam[(FWAIT_ALIAS[r['mnemonic']], r['encoding'])]
        elif r['mnemonic'] in PREFIX_FORMS:
            r['family'] = 'PREFIX'
        moved[r['family']] = moved.get(r['family'], 0) + 1
    return moved


def load_priv():
    pv = {}
    for line in open(PRIV, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        c = line.rstrip('\r\n').split('\t')
        c += [''] * (8 - len(c))
        mn, enc, cls, why, sdm, emu, hw, note = c[:8]
        if cls not in ('kernel', 'user', 'review'):
            raise SystemExit('isa_privilege.tsv: unknown class %r for %s/%s' % (cls, mn, enc))
        if (mn, enc) in pv:
            raise SystemExit('isa_privilege.tsv: duplicate row %s/%s' % (mn, enc))
        pv[(mn, enc)] = {'class': cls, 'why': why, 'sdm': sdm, 'emu': emu, 'hw': hw, 'note': note}
    return pv


def famkey(name):
    return (name.startswith(('AVX512', 'AVX10', 'APX', 'AMX')), name)


def sdm_short(s):
    """'SDM 253667-093 Vol2B RDMSR p. 4-545, 64-Bit Mode Exceptions' -> 'SDM Vol2B 4-545'"""
    out = []
    for m in re.finditer(r'SDM (\S+) (Vol\w+) .*?p\. (\d+-\d+)', s):
        out.append('SDM %s %s%s' % (m.group(2), m.group(3), '' if m.group(1).endswith('-093') else
                                    ' (%s)' % m.group(1).split('-')[1]))
    if out:
        return ', '.join(out)
    if s.startswith('AMD APM'):
        return 'XED CPL 0' if 'XED' in s else 'emulator SVM check'
    return ''


def why_cell(p):
    bits = [p['why']]
    s = sdm_short(p['sdm'])
    if s:
        bits.append(s)
    if p['hw'].startswith('hardware') or 'cases_' in p['hw']:
        m = re.search(r'(cases_\w+\.txt)', p['hw'])
        bits.append('hw: ' + ('CLI ' if p['hw'].startswith('hardware: CLI') else '') + (m.group(1) if m else 'case files'))
    return short(' · '.join(bits), 190)


# ---- tables -----------------------------------------------------------------------------------------------------
def counts(rs):
    c = {'✅': 0, '⏳': 0, '⬜': 0, '❌': 0, 'pm': 0, 'gate': 0, 'open': 0, 'ni': 0}
    for r in rs:
        c[r['status']] += 1
        k = impl_kind(r)
        c['pm'] += k == '❌ (implemented per manual)'
        c['gate'] += k == '❌ (gating only)'
        c['open'] += k == '❌ (implemented, open item)'
        c['ni'] += k == '❌ (not implemented yet)'
    return c


def family_table(rows):
    fams = {}
    for r in rows:
        f = fams.setdefault(r['family'], {'rows': [], 'vendor': [], 'why': []})
        f['rows'].append(r)
        v = vendor(r)
        if v not in f['vendor']:
            f['vendor'].append(v)
        w = parts(r)[2]
        if w and w not in f['why']:
            f['why'].append(w)
    out = ['| family | vendor | forms | ✅ | ⏳ | ⬜ | ❌ | i5-13600K cannot run it because |',
           '|---|---|---|---|---|---|---|---|']
    for name in sorted(fams, key=famkey):
        f = fams[name]
        c = counts(f['rows'])
        why = short(f['why'][0], 110) + (' (+%d more)' % (len(f['why']) - 1) if len(f['why']) > 1 else '') if f['why'] else ''
        out.append('| %s | %s | %d | %s | %s | %s | %s | %s |' % (
            name, ' / '.join(f['vendor']), len(f['rows']), c['✅'] or '', c['⏳'] or '', c['⬜'] or '', c['❌'] or '', why))
    return out


def form_table(rows, pv, kind):
    """kind: 'user' / 'kernel' / 'review'"""
    head = '| instruction | encoding | vector bits | vendor | family |'
    sep = '|---|---|---|---|---|'
    if kind != 'user':
        head += ' %s |' % ('Why kernel' if kind == 'kernel' else 'Why not classified')
        sep += '---|'
    head += ' i5-13600K | Status | verified by |'
    sep += '---|---|---|'
    out = [head, sep]
    for r in rows:
        st, detail, why = parts(r)
        if why:
            cpu = 'cannot: ' + why.replace('|', '/')
        else:
            cpu = 'runs' if kind == 'user' else 'has it'
        cells = [r['mnemonic'].upper(), r['encoding'], r['vl'], vendor(r), r['family']]
        if kind != 'user':
            cells.append(why_cell(pv[(r['mnemonic'], r['encoding'])]))
        cells += [cpu, status_cell(r), verified_cell(r)]
        out.append('| ' + ' | '.join(cells) + ' |')
    return out


def totals_table(classes, label):
    out = ['| privilege class | forms | ✅ | ⏳ | ⬜ | ❌ | ❌ split: per manual / gating only / open item / not implemented yet |',
           '|---|---|---|---|---|---|---|']
    allr = []
    for name, rs in classes:
        c = counts(rs)
        allr += rs
        out.append('| %s | %d | %d | %d | %d | %d | %d / %d / %d / %d |' % (
            label(name), len(rs), c['✅'], c['⏳'], c['⬜'], c['❌'], c['pm'], c['gate'], c['open'], c['ni']))
    c = counts(allr)
    out.append('| **Total** | **%d** | **%d** | **%d** | **%d** | **%d** | %d / %d / %d / %d |' % (
        len(allr), c['✅'], c['⏳'], c['⬜'], c['❌'], c['pm'], c['gate'], c['open'], c['ni']))
    return out


# ---- plan / ledger ----------------------------------------------------------------------------------------------
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


def ledger_summary(n=10):
    """P26: the n highest U numbers (numeric, newest first), one line each, cut at a word"""
    try:
        rows = [l for l in open(LEDGER, encoding='utf-8').read().split('\n') if re.match(r'\| U\d+ \|', l)]
    except OSError:
        return 0, []
    num = []
    for l in rows:
        c = [x.strip() for x in re.split(r'(?<!\\)\|', l)]       # cells may hold escaped pipes (\|)
        num.append((int(c[1][1:]), c[1], c[2].replace('\\|', '/')))
    num.sort(reverse=True)
    return len(rows), ['- **%s** — %s' % (u, short(t, 150)) for _, u, t in num[:n]]


# ---- main -------------------------------------------------------------------------------------------------------
def main():
    data = json.load(open(DATA, encoding='utf-8'))
    rows = data['rows']
    moved = fix_family(rows)
    rows.sort(key=lambda r: (famkey(r['family']), r['mnemonic'], ENC_ORDER.get(r['encoding'], 9)))
    pv = load_priv()
    keys = {(r['mnemonic'], r['encoding']) for r in rows}
    stale = sorted('%s/%s' % k for k in pv if k not in keys)
    if stale:
        raise SystemExit('isa_privilege.tsv rows without a form in the instruction table: %s' % ', '.join(stale))
    cls = lambda r: pv.get((r['mnemonic'], r['encoding']), {'class': 'user'})['class']
    user = [r for r in rows if cls(r) == 'user']
    kern = [r for r in rows if cls(r) == 'kernel']
    review = [r for r in rows if cls(r) == 'review']
    sub = [r for r in user if pv.get((r['mnemonic'], r['encoding']), {}).get('note') == 'kernel sub-forms']
    classes = [('user', user), ('kernel', kern), ('review', review)]
    label = {'user': 'Usermode (no emu driver required)', 'kernel': 'Kernel mode required',
             'review': 'Needs review'}.get
    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    head = git('log', '-1', '--format=%h %s')
    status, adding = plan_lines()
    nledger, last = ledger_summary()
    t = counts(rows)
    nhost = sum(1 for r in rows if r.get('host_enumerates'))
    os.makedirs(DOCS, exist_ok=True)
    gen = '_Generated %s from `Emulator/tools/isa/gen_status_docs.py` (HEAD `%s`). Do not edit by hand._' % (now, head)

    # ---- the instruction page
    o = ['# ' + MAIN_TITLE, ''] + prose([gen]) + ['']
    o += prose(['Every instruction form (mnemonic + encoding) of the Intel SDM / XED list `Emulator/data/isa_manual_forms.tsv`, '
                'AMD and VIA forms included: **%d forms, each listed exactly once**, in one of three tables by privilege '
                'class (`Emulator/data/isa_privilege.tsv`). Checked against an Intel i5-13600K (Raptor Lake) with '
                '`emu-alltest` (hardware sweep and `--cases` files) and, for forms this CPU cannot run, against expected '
                'values derived from the SDM pseudocode.' % len(rows)])
    o += ['', '## Legend', ''] + LEGEND + ['', '## Totals', ''] + totals_table(classes, label) + ['']
    o += prose(['Per profile (U1632): %d of the ❌ forms are enumerated by the bare host (`--host`) and hidden only by the '
                'VMware guest (`--guest` / `--vm`); every other reason holds on both.' % nhost])
    o += [''] + prose(['AMD / VIA forms (Vendor column; "both" = AMD-originated forms the i5-13600K also runs): implemented '
                       'after every Intel instruction (decision 2026-10-07, plan 1.15f) from the vendor manuals; a switch '
                       '`__use_AMD_instruction_set__` (default 0 = #UD as on Intel) will gate them. 3DNow!, SSE4a and FEMMS '
                       'already exist in QEMU and run under the non-strict MAX model; under the i5-13600K profile with '
                       '`--strict` they are #UD like the CPU.'])
    o += ['', '## Currently being added', ''] + prose(adding or ['- (nothing in progress)']) + ['']
    o += ['## Usermode (no emu driver required)', '']
    o += prose(['%d forms whose privilege rules allow CPL3 under Windows: no CPL / IOPL / CR4 privilege condition on their '
                'SDM page, or one Windows does not trigger (CR4.TSD = 0, CR4.UMIP = 0, CR4.FSGSBASE = 1; IOPL only in '
                'virtual-8086 mode), as listed in `Emulator/data/isa_privilege.tsv`. Whether the i5-13600K itself can run '
                'a form is the i5-13600K column.' % len(user)])
    o += ['', '### By family (usermode)', ''] + family_table(user)
    o += ['', '### Usermode forms (%d)' % len(user), ''] + form_table(user, pv, 'user')
    o += ['', '## Kernel mode required', '']
    o += prose(['%d forms that cannot execute at CPL3 under Windows (Windows runs user code at CPL3 with IOPL = 0 and '
                'CR4.PCE = 0); **Why kernel** gives the reason and its evidence: the SDM exception list (093 '
                'single-volume PDFs, printed page), the hardware cases that fault at CPL3 on the i5-13600K '
                '(`Emulator/data/cases_lockpriv_hw.txt`: 13 CPL0-only instructions -> #GP, G26) and the emulator\'s own '
                'check (`isa_privilege.tsv` columns 6-7).' % len(kern)])
    o += ['', '### By family (kernel mode)', ''] + family_table(kern)
    o += ['', '### Kernel-mode forms (%d)' % len(kern), ''] + form_table(kern, pv, 'kernel')
    if sub:
        o += ['', '### Kernel-only sub-forms of usermode rows', '']
        for r in sub:
            p = pv[(r['mnemonic'], r['encoding'])]
            o += prose(['- %s (%s): %s — %s; %s' % (r['mnemonic'].upper(), r['encoding'], p['why'], sdm_short(p['sdm']),
                                                     short(p['hw'], 160))])
    o += ['', '## Needs review', '']
    o += prose(['%d forms without a privilege class backed by evidence (not guessed); they are not in the two tables '
                'above.' % len(review)])
    o += [''] + form_table(review, pv, 'review') + ['']
    if moved:
        o += prose(['Family fix (P26): the forms XED lists without an ISA set (family "?") are shown as %s.' % ', '.join(
            '%s (%d)' % (k, v) for k, v in sorted(moved.items()))]) + ['']
    open(os.path.join(DOCS, MAIN_PAGE), 'w', encoding='utf-8').write('\n'.join(o) + '\n')

    # ---- AMD / VIA: stub (P26: the forms are in the main page, Vendor column AMD / VIA / both)
    o = ['# AMD (and VIA) instruction sets', '',
         'Merged into [%s](%s): one table for both vendors (Vendor column: Intel / AMD / VIA / both).' % (MAIN_TITLE, MAIN_PAGE)]
    open(os.path.join(DOCS, 'AMD_instruction_sets_supported.md'), 'w', encoding='utf-8').write('\n'.join(o) + '\n')

    # ---- index
    o = ['# Instruction support — index', ''] + prose(['_Generated %s (HEAD `%s`) by `Emulator/tools/isa/gen_status_docs.py`._' % (now, head)]) + ['']
    o += prose(['Status of every instruction form the NoVmp emulator knows (Intel SDM / XED list, AMD and VIA forms '
                'included), checked against an Intel i5-13600K or, where the CPU cannot run a form, against the manual.'])
    o += ['', '- [%s](%s) — every form once, split into usermode / kernel mode / needs review' % (MAIN_TITLE, MAIN_PAGE),
          '', '## Legend', ''] + LEGEND + ['', '## Totals', ''] + totals_table(classes, label) + ['']
    o += prose(['Your i5-13600K runs %d forms and cannot run %d (❌; the reason is in the i5-13600K column of the page above).'
                % (len(rows) - t['❌'], t['❌'])])
    o += ['', '## Currently being added', ''] + prose(adding or ['- (nothing in progress)'])
    o += ['', '## Latest ledger entries (`CHANGES_LEDGER.md`, %d rows; the 10 highest U numbers)' % nledger, ''] + last
    open(os.path.join(DOCS, 'instructions.md'), 'w', encoding='utf-8').write('\n'.join(o) + '\n')

    # ---- README status block
    block = [BEGIN, '## Status of this fork (NoVmp emulator)', '']
    block += prose(['_Updated %s — branch `novmp-emu`, HEAD `%s`. Generated by `Emulator/tools/isa/gen_status_docs.py`._'
                    % (now, head), '',
                    'This fork turns NoVmp into a **pure-emulation** devirtualizer: the packed sample is never run natively; '
                    'a Unicorn/QEMU 7.2 fork (in-tree, `unicorn/`) is being extended until every Intel-manual instruction is '
                    'emulated exactly (verified against an Intel i5-13600K, or against SDM-pseudocode vectors where the CPU '
                    'lacks the instruction). Every change to QEMU code sits under one switch `__Use_Original_Qemu` (0 = ours, '
                    '1 = stock QEMU) and is recorded in [`CHANGES_LEDGER.md`](CHANGES_LEDGER.md) (%d entries).' % nledger])
    block += ['', '### Instruction support', '']
    block += prose(['- %d forms: ✅ %d · ⏳ %d · ⬜ %d · **❌ %d your i5-13600K cannot run** (implemented per the manual %d, '
                    'gating only %d, open item %d, not implemented yet %d)' % (
                        len(rows), t['✅'], t['⏳'], t['⬜'], t['❌'], t['pm'], t['gate'], t['open'], t['ni']),
                    '- By privilege: usermode %d · kernel mode required %d · needs review %d' % (len(user), len(kern), len(review)),
                    '- Details: [docs/instructions.md](docs/instructions.md) · [%s](docs/%s)' % (MAIN_TITLE, MAIN_PAGE)])
    block += ['', '### Plan status', ''] + prose(status) + ['', '### Currently being added', ''] + \
        prose(adding or ['- (nothing in progress)']) + [
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
    print('docs written: %d forms (usermode %d, kernel %d, needs review %d), totals %s, family fix %s, %d in-progress lines'
          % (len(rows), len(user), len(kern), len(review), {k: t[k] for k in ('✅', '⏳', '⬜', '❌')}, moved, len(adding)))


main()
