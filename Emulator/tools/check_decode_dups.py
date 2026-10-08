"""Duplicate designated initializers in the x86 decoder's opcode tables (test.cmd suite).

MSVC (and C99) accept a designated initializer that names an element twice ([0x08] = A,
... [0x08] = B): the later one silently wins, so two changes that both take an opcode slot
merge without a compile error and one of them is lost. This check fails on any such pair.

Scanned: unicorn/qemu/target/i386/decode-new.c.inc with every '#include "*.c.inc"' of that
directory expanded in place (decode_fp16.c.inc, decode-avx10.c.inc, ...). Every
'static const X86OpEntry NAME[..]... = {' table - file-level opcode maps and the local
per-prefix tables of group decoders - is walked; designators '[k] =' and '[i][j] =' are
tracked per brace nesting path. A function-like macro whose body starts with its parameter
as a designator ('#define FP16_FP(b_) [b_] = ...') counts as one at each use.

The source is preprocessed twice, for __Use_Original_Qemu = 0 (ours, the default build) and
= 1 (original QEMU): '#if __Use_Original_Qemu == 1' / '!= 1' blocks are resolved, every
other conditional keeps all of its branches (a slot taken in both arms of an unrelated
#if/#else would be reported; there is none).

usage: check_decode_dups.py [DIR]     DIR = target/i386 directory (default: from this script)
exit status 0 = no duplicate in either mode, 1 = duplicates (listed), 2 = input error.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DIR = os.path.normpath(os.path.join(HERE, '..', '..', 'unicorn', 'qemu', 'target', 'i386'))


def read_lines(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read().split('\n')


def expand(dirname, fname, depth=0):
    """lines of fname with local .c.inc includes expanded: list of (file, lineno, text)"""
    out = []
    for n, l in enumerate(read_lines(os.path.join(dirname, fname)), 1):
        m = re.match(r'\s*#\s*include\s+"([^"]+\.c\.inc)"', l)
        if m and depth < 8 and os.path.exists(os.path.join(dirname, m.group(1))):
            out.append((fname, n, '/* include */'))
            out.extend(expand(dirname, m.group(1), depth + 1))
            continue
        out.append((fname, n, l))
    return out


def preprocess(lines, orig):
    """keep the lines active for __Use_Original_Qemu = orig (other #if: both branches)"""
    out, stack = [], []
    active = True
    for f, n, l in lines:
        m = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', l)
        if m:
            kw, cond = m.group(1), m.group(2)
            if kw in ('if', 'ifdef', 'ifndef'):
                val = None
                c = re.match(r'\s*__Use_Original_Qemu\s*(==|!=)\s*1\b', cond)
                if c:
                    val = (orig == 1) if c.group(1) == '==' else (orig != 1)
                stack.append([active, val])
                active = active and (val is not False)
            elif kw == 'elif':
                active = stack[-1][0]
            elif kw == 'else':
                val = stack[-1][1]
                active = stack[-1][0] and (val is None or not val)
            elif kw == 'endif':
                if not stack:
                    raise ValueError('%s:%d: #endif without #if' % (f, n))
                active = stack.pop()[0]
            continue
        if active:
            out.append((f, n, l))
    if stack:
        raise ValueError('unterminated #if')
    return out


def designator_macros(lines):
    """names of '#define NAME(p) [p] = ...' macros"""
    names = set()
    for f, n, l in lines:
        m = re.match(r'\s*#\s*define\s+(\w+)\((\w+)\)\s*\[\s*(\w+)\s*\]\s*=', l)
        if m and m.group(2) == m.group(3):
            names.add(m.group(1))
    return names


def strip_comments(lines):
    """remove /* */ (also multi-line) and // comments, keep line structure"""
    out = []
    in_c = False
    for f, n, l in lines:
        s, i, buf = l, 0, []
        while i < len(s):
            if in_c:
                j = s.find('*/', i)
                if j < 0:
                    i = len(s)
                else:
                    in_c = False
                    i = j + 2
            else:
                j = s.find('/*', i)
                k = s.find('//', i)
                if k >= 0 and (j < 0 or k < j):
                    buf.append(s[i:k])
                    i = len(s)
                elif j >= 0:
                    buf.append(s[i:j])
                    in_c = True
                    i = j + 2
                else:
                    buf.append(s[i:])
                    i = len(s)
        out.append((f, n, ''.join(buf)))
    return out


TABLE = re.compile(r'\bstatic\s+const\s+X86OpEntry\s+(\w+)\s*((?:\[[^\]]*\])+)\s*=\s*\{')
TOKEN = re.compile(r'\[\s*(0x[0-9a-fA-F]+|\d+)\s*\](?:\s*\[\s*(0x[0-9a-fA-F]+|\d+)\s*\])?\s*=(?!=)|\{|\}')


def scan(lines, tag, verbose):
    macros = designator_macros(lines)
    mac_re = re.compile(r'\b(%s)\(\s*(0x[0-9a-fA-F]+|\d+)\s*\)' % '|'.join(sorted(macros))) if macros else None
    lines = strip_comments(lines)
    tables = dups = 0
    report = []
    i = 0
    while i < len(lines):
        f, n, l = lines[i]
        m = TABLE.search(l)
        if not m:
            i += 1
            continue
        tables += 1
        name = m.group(1)
        seen = {}
        path = []
        pending = None
        depth = 0
        first = True
        while i < len(lines):
            f, n, l = lines[i]
            s = l[m.end() - 1:] if first else l
            first = False
            if mac_re:
                s = mac_re.sub(lambda mm: '[%s] =' % mm.group(2), s)
            for tok in TOKEN.finditer(s):
                t = tok.group(0)
                if t == '{':
                    depth += 1
                    path.append(pending)
                    pending = None
                elif t == '}':
                    depth -= 1
                    if path:
                        path.pop()
                    if depth == 0:
                        break
                else:
                    a = int(tok.group(1), 0)
                    b = int(tok.group(2), 0) if tok.group(2) else None
                    key = (tuple(path), a, b)
                    where = '%s:%d' % (f, n)
                    if key in seen:
                        dups += 1
                        report.append('  [%s] DUPLICATE %s%s: %s and %s' % (
                            tag, name, ''.join('[%#x]' % x for x in (a, b) if x is not None),
                            seen[key], where))
                    else:
                        seen[key] = where
                    pending = (a, b)
            i += 1
            if depth == 0:
                break
        if verbose:
            print('  [%s] %s: %d designated initializers' % (tag, name, len(seen)))
    for r in report:
        print(r)
    print('[%s] X86OpEntry tables scanned: %d, duplicate designated initializers: %d' % (tag, tables, dups))
    return tables, dups


def main(argv):
    verbose = '-v' in argv
    args = [a for a in argv[1:] if a != '-v']
    d = args[0] if args else DEFAULT_DIR
    if not os.path.exists(os.path.join(d, 'decode-new.c.inc')):
        print('check_decode_dups: %s has no decode-new.c.inc' % d)
        return 2
    try:
        src = expand(d, 'decode-new.c.inc')
        total = 0
        for orig, tag in ((0, 'ours'), (1, 'orig')):
            tables, dups = scan(preprocess(src, orig), tag, verbose)
            if tables == 0:
                print('check_decode_dups: no tables found (%s)' % tag)
                return 2
            total += dups
    except (ValueError, OSError) as e:
        print('check_decode_dups: %s' % e)
        return 2
    print('check_decode_dups: %s' % ('OK' if total == 0 else 'FAILED, %d duplicate(s)' % total))
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
