#!/usr/bin/env python3
"""check_sdm_citations.py - suite sdm_citation_check (plan step 16 / Gate 1 fix G30-1, ledger U1922).

AGENT_RULES "Citation rule" (user decision 2026-10-19, P15) and docs/quirks.md line 7: SDM 093 pages are cited from the
single-volume PDFs - Vol2A 253666-093, Vol2B 253667-093, Vol2C 326018-093, Vol2D 334569-093, Vol3A 253668-093, Vol3B
253669-093, Vol3C 326019-093, Vol3D 332831-093 (U1915). 325383-093 is the Vol. 2 SET (Vol. 2A-2D in one PDF) and
325384-093 the Vol. 3 set; naming a set is fine ("the volume sets 325383-093 (Vol2)"), but a set number with a
single-volume label (the set number followed by "Vol2A", "Vol. 3B", ...) is a wrong document number. U1915 removed every such label;
2c67d396 (isa_privilege.tsv) brought 32 back (G30-1). This check refuses the label in:
  - every file under Emulator/data with the extension .tsv or .txt (data tables, case files);
  - every C / C++ source and header of the repository (.c .h .cc .cpp .cxx .hpp .hh .inc; comments included);
  - Emulator/tools (.py .ps1: the generators that write citations into the data files).
Not scanned: CHANGES_LEDGER.md and docs/instructions.md (history: ledger rows may quote what they fixed), build/, .git/,
bundled third-party trees are scanned too (they never use the label). The 092 revision (325383-092 ...) is outside the
093 rule and not refused.
Negative controls (in memory, every run): label forms that must be refused and set / single-volume forms that must pass;
the scan must have read Emulator/data/isa_privilege.tsv and unicorn/qemu/target/i386/translate.c, so it cannot pass
vacuously. Exit 0 = no label found; 1 = labels found (file:line listed) or a control failed. Reads only.
usage: python -I check_sdm_citations.py [REPO_ROOT]
"""
import os
import re
import sys

LABEL = re.compile(r'(?<![0-9])(325383|325384)-093\W{0,3}Vol(?:ume)?\.?\s?[0-9][A-D](?![A-Za-z])', re.I)
C_EXT = ('.c', '.h', '.cc', '.cpp', '.cxx', '.hpp', '.hh', '.inc')
SKIP_DIRS = {'.git', 'build', '__pycache__', '.vs'}

S2, S3 = '325383-093', '325384-093'   # the two set numbers (kept apart from the labels so this file passes its own scan)
MUST_REFUSE = [
    'SDM %s Vol2A CLAC p. 3-139, 64-Bit Mode Exceptions' % S2,
    '/* SDM %s Vol2B UMWAIT p. 4-742 */' % S2,
    '%s Vol3A 7.9 p. 7-10' % S3,
    '%s Vol. 2D p. 6-17' % S2,
    '%s, Vol 2C' % S2,
    '(%s Volume 3B)' % S3,
]
MUST_PASS = [
    'SDM 253666-093 Vol2A CLAC p. 3-139, 64-Bit Mode Exceptions',
    'the volume sets %s (Vol2) / %s (Vol3)' % (S2, S3),
    'SDM 253666-093 Vol2A (the same pages in the set %s) LDS/LES' % S2,
    '325383-092 Vol2A (another revision)',
    '3%s Vol2A' % S2,
]

def scanned(root):
    data = os.path.join(root, 'Emulator', 'data')
    tools = os.path.join(root, 'Emulator', 'tools')
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d not in SKIP_DIRS]
        for f in fn:
            p = os.path.join(dp, f)
            low = f.lower()
            if low.endswith(C_EXT):
                yield p, 'C/C++'
            elif (dp == data or dp.startswith(data + os.sep)) and low.endswith(('.tsv', '.txt')):
                yield p, 'Emulator/data'
            elif (dp == tools or dp.startswith(tools + os.sep)) and low.endswith(('.py', '.ps1')):
                yield p, 'Emulator/tools'


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    fail = 0
    for s in MUST_REFUSE:
        if not LABEL.search(s):
            print('CONTROL FAILED: not refused: %s' % s); fail += 1
    for s in MUST_PASS:
        if LABEL.search(s):
            print('CONTROL FAILED: refused: %s' % s); fail += 1
    print('negative controls: %d refused forms, %d accepted forms, %d failed' % (len(MUST_REFUSE), len(MUST_PASS), fail))
    counts = {}
    seen = set()
    hits = []
    for p, cls in scanned(root):
        counts[cls] = counts.get(cls, 0) + 1
        rel = os.path.relpath(p, root).replace(os.sep, '/')
        seen.add(rel)
        with open(p, 'rb') as fh:
            text = fh.read().decode('latin-1')
        if '-093' not in text:
            continue
        for n, line in enumerate(text.split('\n'), 1):
            for m in LABEL.finditer(line):
                hits.append('%s:%d: %s' % (rel, n, m.group(0)))
    print('scanned: ' + ', '.join('%s %d files' % (k, v) for k, v in sorted(counts.items())))
    for anchor in ('Emulator/data/isa_privilege.tsv', 'unicorn/qemu/target/i386/translate.c'):
        if anchor not in seen:
            print('CONTROL FAILED: %s was not scanned (wrong root?)' % anchor); fail += 1
    for h in hits:
        print('REFUSED (set number with a single-volume label; cite 253666 / 253667 / 326018 / 334569-093 Vol2A-2D, '
              '253668 / 253669 / 326019 / 332831-093 Vol3A-3D): ' + h)
    print('combined-volume labels found: %d' % len(hits))
    if hits or fail:
        print('FAILED')
        return 1
    print('OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
