#!/usr/bin/env python3
"""apx_cov.py -- map every APX case line (raw .byte encodings) of the APX case files to the
instruction form (mnemonic, encoding) of Emulator/data/isa_manual_forms.tsv it executes.

Independent decoder written from the APX spec 355828-009 (3.1.2.x encodings, 3.1.5 map 4 table,
chapter 6-9 opcode lines); names follow XED (isa_manual_forms.tsv), CMOVcc/SETcc use the
cmova/seta spellings of the table, CMPccXADD covers the XED + alias spellings.

usage: python apx_cov.py <Emulator/data dir> [--dump]
prints per form: value cases (expected values, no fault), loose (=>!), hw pairs, fault cases
(#GP/#PF/#DE...), #UD cases, per file.
"""
import os
import re
import sys
from collections import defaultdict

CC = ["o", "no", "b", "nb", "z", "nz", "be", "nbe", "s", "ns", "p", "np", "l", "nl", "le", "nle"]
# isa_manual_forms spellings
CMOV_NAME = ["o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g"]
SCC = ["o", "no", "b", "nb", "z", "nz", "be", "nbe", "s", "ns", "t", "f", "l", "nl", "le", "nle"]
# CMPccXADD: XED name + extra spellings in isa_manual_forms.tsv for the same cc
CMPX_ALIAS = {3: ["ae"], 7: ["a"], 4: ["e"], 5: ["ne"], 13: ["ge"], 15: ["g"]}

LEGACY_PFX = {0x66, 0x67, 0xF0, 0xF2, 0xF3, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65}
ALU = ["add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"]
SHIFT = ["rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"]


class Dec(Exception):
    pass


def modrm_len(b, i, asz32=False):
    """length of ModRM + SIB + disp starting at b[i] (64-bit addressing)"""
    m = b[i]
    mod, rm = m >> 6, m & 7
    n = 1
    if mod == 3:
        return n
    if rm == 4:
        sib = b[i + 1]
        n += 1
        if mod == 0 and (sib & 7) == 5:
            n += 4
    elif mod == 0 and rm == 5:
        n += 4
    if mod == 1:
        n += 1
    elif mod == 2:
        n += 4
    return n


def decode(b):
    """returns (forms list [(mn, enc)], length, desc) of the first instruction in b
    (only the APX forms of the table are named; others -> [])"""
    i = 0
    pfx = []
    while i < len(b) and b[i] in LEGACY_PFX:
        pfx.append(b[i])
        i += 1
    rex = None
    if i < len(b) and 0x40 <= b[i] <= 0x4F:
        rex = b[i]
        i += 1
    if i >= len(b):
        raise Dec("empty")
    if b[i] == 0xD5:
        p = b[i + 1]
        m0, w = p >> 7, (p >> 3) & 1
        opc = b[i + 2]
        j = i + 3
        if m0 == 0 and 0x50 <= opc <= 0x5F:
            return ([("pushp" if opc < 0x58 else "popp", "legacy")] if w else [], j, "rex2 %s W%d" % ("push" if opc < 0x58 else "pop", w))
        if m0 == 0 and opc == 0xA1:
            return ([("jmpabs", "legacy")] if w == 0 else [], j + 8, "rex2 A1 W%d" % w)
        return [], None, "rex2 other"
    if b[i] in (0xC4, 0xC5):
        return [], None, "vex"
    if b[i] != 0x62:
        return [], None, "legacy"
    p0, p1, p2 = b[i + 1], b[i + 2], b[i + 3]
    mmm = p0 & 7
    w = p1 >> 7
    vvvv = (~p1 >> 3) & 15
    pp = p1 & 3          # 0 NP 1 66 2 F3 3 F2
    ll = (p2 >> 5) & 3
    nd = (p2 >> 4) & 1
    v4 = ((~p2) >> 3) & 1
    nf = (p2 >> 2) & 1
    v = vvvv | (v4 << 4)
    opc = b[i + 4]
    j = i + 5
    modrm = b[j] if j < len(b) else None
    mod = modrm >> 6 if modrm is not None else None
    reg = (modrm >> 3) & 7 if modrm is not None else None
    mlen = modrm_len(b, j) if modrm is not None else 0
    desc = "evex map%d pp%d W%d ND%d NF%d L%d opc %02X mod%s reg%s V%d" % (mmm, pp, w, nd, nf, ll, opc, mod, reg, v)
    PPN = {0: "NP", 1: "66", 2: "F3", 3: "F2"}[pp]
    F = []
    imm = 0
    if mmm == 4:
        opsz16 = (pp == 1 and w == 0)
        iz = 2 if opsz16 else 4
        if opc < 0x40 and (opc & 7) < 4:
            k = opc >> 3
            if k == 7:
                F = [("ccmp" + SCC[p2 & 15], "evex")]
            else:
                F = [(ALU[k], "evex")]
        elif opc in (0x80, 0x81, 0x83):
            imm = 1 if opc != 0x81 else iz
            if reg == 7:
                F = [("ccmp" + SCC[p2 & 15], "evex")]
            else:
                F = [(ALU[reg], "evex")]
        elif opc in (0x84, 0x85):
            F = [("ctest" + SCC[p2 & 15], "evex")]
        elif opc in (0xF6, 0xF7):
            if reg in (0, 1):
                imm = 1 if opc == 0xF6 else iz
                F = [("ctest" + SCC[p2 & 15], "evex")]
            else:
                F = [(["", "", "not", "neg", "mul", "imul", "div", "idiv"][reg], "evex")]
        elif opc in (0xFE, 0xFF) and reg in (0, 1):
            F = [(["inc", "dec"][reg], "evex")]
        elif opc == 0xFF and reg == 6:
            F = [("push2p" if w else "push2", "evex")]
        elif opc == 0x8F and reg == 0:
            F = [("pop2p" if w else "pop2", "evex")]
        elif opc in (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
            imm = 1 if opc in (0xC0, 0xC1) else 0
            F = [(SHIFT[reg], "evex")]
        elif opc in (0x69, 0x6B, 0xAF):
            imm = {0x69: iz, 0x6B: 1, 0xAF: 0}[opc]
            F = [("imul", "evex")]
        elif opc in (0x24, 0xA5):
            imm = 1 if opc == 0x24 else 0
            F = [("shld", "evex")]
        elif opc in (0x2C, 0xAD):
            imm = 1 if opc == 0x2C else 0
            F = [("shrd", "evex")]
        elif 0x40 <= opc <= 0x4F:
            c = opc & 15
            if pp == 3:
                F = [("set" + CMOV_NAME[c], "evex")]
            elif nd == 1 and nf == 0:
                F = [("cmov" + CMOV_NAME[c], "evex")]
            else:
                F = [("cfcmov" + CC[c], "evex")]
        elif opc == 0x88:
            F = [("popcnt", "evex")]
        elif opc == 0xF4:
            F = [("tzcnt", "evex")]
        elif opc == 0xF5:
            F = [("lzcnt", "evex")]
        elif opc in (0x60, 0x61):
            F = [("movbe", "evex")]
        elif opc == 0x65:
            F = [("wrussq" if w else "wrussd", "evex")]
        elif opc == 0x66:
            F = [({0: "wrssq" if w else "wrssd", 1: "adcx", 2: "adox", 3: "?66F2"}[pp], "evex")]
        elif opc in (0x8A, 0x8B):
            F = [("movrs", "evex")]
        elif opc in (0xF0, 0xF1, 0xF2) and pp == 2:
            F = [({0xF0: "invept", 0xF1: "invvpid", 0xF2: "invpcid"}[opc], "evex")]
        elif opc in (0xF0, 0xF1):
            F = [("crc32", "evex")]
        elif opc == 0xF8:
            if pp == 1:
                F = [("movdir64b", "evex")]
            elif pp in (2, 3):
                if mod == 3:
                    F = [({3: "urdmsr", 2: "uwrmsr"}[pp], "evex")]
                else:
                    F = [({3: "enqcmd", 2: "enqcmds"}[pp], "evex")]
        elif opc == 0xF9:
            F = [("movdiri", "evex")]
        elif opc == 0xFC:
            F = [({0: "aadd", 1: "aand", 3: "aor", 2: "axor"}[pp], "evex")]
        else:
            F = []
    elif mmm == 1:
        if opc in (0x90, 0x91, 0x92, 0x93):
            if opc in (0x92, 0x93) and pp == 3:
                F = [("kmovq" if w else "kmovd", "evex")]
            elif pp == 0:
                F = [("kmovq" if w else "kmovw", "evex")]
            elif pp == 1:
                F = [("kmovd" if w else "kmovb", "evex")]
    elif mmm == 2:
        if opc == 0xF2 and pp == 0:
            F = [("andn", "evex")]
        elif opc == 0xF3 and pp == 0 and reg in (1, 2, 3):
            F = [({1: "blsr", 2: "blsmsk", 3: "blsi"}[reg], "evex")]
        elif opc == 0xF5:
            F = [({0: "bzhi", 3: "pdep", 2: "pext"}.get(pp, "?F5"), "evex")]
        elif opc == 0xF6 and pp == 3:
            F = [("mulx", "evex")]
        elif opc == 0xF7:
            F = [({0: "bextr", 1: "shlx", 2: "sarx", 3: "shrx"}[pp], "evex")]
        elif 0xE0 <= opc <= 0xEF and pp == 1:
            c = opc & 15
            F = [("cmp%sxadd" % CC[c], "evex")] + [("cmp%sxadd" % a, "evex") for a in CMPX_ALIAS.get(c, [])]
        elif opc == 0x49:
            F = [({0: "ldtilecfg", 1: "sttilecfg"}.get(pp, "?49"), "evex")]
        elif opc == 0x4B:
            F = [({3: "tileloadd", 1: "tileloaddt1", 2: "tilestored"}.get(pp, "?4B"), "evex")]
        elif opc == 0x4A:
            F = [({3: "tileloaddrs", 1: "tileloaddrst1"}.get(pp, "?4A"), "evex")]
    elif mmm == 3:
        if opc == 0xF0 and pp == 3:
            F = [("rorx", "evex")]
            imm = 1
    elif mmm == 7:
        if opc == 0xF8 and reg == 0:
            F = [({3: "urdmsr", 2: "uwrmsr"}.get(pp, "?M7F8"), "evex")]
            imm = 4
        elif opc == 0xF6 and reg == 0:
            F = [({3: "rdmsr", 2: "wrmsrns"}.get(pp, "?M7F6"), "evex")]
            imm = 4
    if not F:
        return [], None, desc + " (non-APX-table)"
    ln = j + mlen + imm
    return F, ln, desc


def segs(code):
    """the .byte segments of a case's code text"""
    out = []
    for part in code.split(";"):
        part = part.strip()
        if part.startswith(".byte"):
            out.append(bytes(int(x, 16) for x in part[5:].split(",")))
    return out


def classify(rest):
    rest = rest.split("#  ")[0]
    if "=>!" in rest:
        return "loose"
    m = re.search(r"=>\s*(#[A-Z]+)", rest)
    if m:
        return "ud" if m.group(1) == "#UD" else "fault"
    if "=>" in rest:
        return "value"
    return "?"


FILES = ["cases_apx_core.txt", "cases_apx_core_hw.txt", "cases_apx_evex.txt", "cases_apx_map4.txt",
         "cases_apx_map4_hw.txt", "cases_apx_map4_ext.txt", "cases_apx_map4_sweep.txt"]


def main():
    ddir = sys.argv[1]
    dump = "--dump" in sys.argv
    cov = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))   # form -> file -> kind -> n
    leftovers = defaultdict(int)
    examples = {}
    for fn in FILES:
        for ln in open(os.path.join(ddir, fn), encoding="utf-8"):
            ln = ln.rstrip("\n")
            if not ln.strip() or ln.startswith("#"):
                continue
            hw = "~~" in ln
            if hw:
                left, right = ln.split("~~", 1)
                code = right.split("|")[0]
                kind = "hw"
                if "=>" in right:
                    kind = "hw-" + classify(right)
            else:
                code = re.split(r"\s\|\s|\s=>", ln)[0]
                kind = classify(ln[len(code):])
            for s in segs(code):
                pos = 0
                while pos < len(s):
                    try:
                        F, L, desc = decode(s[pos:])
                    except (Dec, IndexError) as e:
                        leftovers["decode-error " + fn] += 1
                        break
                    if not F:
                        if pos:
                            leftovers["%s trailing non-APX-table bytes after an APX form: %s" % (fn, desc)] += 1
                            if dump:
                                print("TRAIL?", fn, ln[:200])
                        break
                    for f in F:
                        cov[f][fn][kind] += 1
                        examples.setdefault((f, kind), ln[:200])
                    if pos:
                        leftovers["%s second APX instruction in one .byte run: %s" % (fn, F[0][0])] += 1
                    pos += L
    forms = []
    for line in open(os.path.join(ddir, "isa_manual_forms.tsv"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        t = line.rstrip("\n").split("\t")
        if "APX_F" in t[3]:
            forms.append((t[0], t[1], t[3], t[4]))
    names = set((f[0], f[1]) for f in forms)
    for f in sorted(cov):
        if f not in names:
            print("DECODED BUT NOT IN TABLE:", f, dict((k, dict(v)) for k, v in cov[f].items()))
    for k, v in sorted(leftovers.items()):
        print("NOTE", v, k)
    for mn, enc, isa, fl in forms:
        c = cov.get((mn, enc), {})
        parts = []
        for fn in FILES:
            if fn in c:
                parts.append("%s{%s}" % (fn, ",".join("%s:%d" % kv for kv in sorted(c[fn].items()))))
        print("FORM\t%s\t%s\t%s\t%s\t%s" % (mn, enc, isa, fl, " ".join(parts) or "-"))
    if dump:
        for (f, kind), ex in sorted(examples.items()):
            print("EX", f, kind, ex)


if __name__ == "__main__":
    main()
