"""Generate the EVEX (AVX-512 / AVX10) instruction form table for the M1 decoder (EVEX_DESIGN.md 2.1-2.9, 4).

Sources, in lookup order (the first one that has a value wins; every asmjit disagreement is recorded):
  1. Intel SDM Vol. 2 rev 092 (325383-092, June 2026), PyMuPDF text per volume (intel_docs_text\\sdm_vol2{a,b,c,d}_092.md)
     - opcode-table rows (EVEX.<L>.<pp>.<map>.<W> <opcode> ...) + the page's "Instruction Operand Encoding" table
       (Op/En -> Tuple Type, operand roles) + the "Other Exceptions" EVEX class (E1..E12[NF|NP|NM]).
     Also Intel AVX10.2 Architecture Specification 361050-007 and the ISE 319433-062 (forms the SDM does not carry yet).
     The -layout text sdm2.txt (pdftotext of the same 325383-092 PDF) is only used to cross-check that every EVEX
     opcode string of the SDM was seen (its table columns are interleaved and cannot give Op/En reliably).
  2. asmjit db\\isa_x86.json EVEX rows (tt, {k|z}, /bNN, {er}, {sae}, vl) - the primary *structured* source.
  3. XED datafiles (Intel docs\\xed-main\\datafiles) - only to fill forms neither Intel text nor asmjit has, and as a
     third opinion printed next to each asmjit-vs-Intel conflict.
  (QEMU 11.1 has no EVEX decoder, so it contributes nothing here.)

Outputs (Emulator\\data):
  evex_forms.tsv            one row per EVEX form (VLs folded into one row), disp8*N per VL
  evex_forms_conflicts.txt  every asmjit-vs-Intel disagreement and how it was resolved (Intel wins)
  evex_forms_apx.txt        APX "EVEX-promoted legacy/VEX" forms, kept out of the AVX-512 table
  evex_tables_draft.inc     X86_EVEX_ENTRY(...) lines grouped by map/opcode/pp (draft, not compiled)
  evex_forms_summary.txt    counts per family / feature / tuple, conflicts, missing forms, decoder notes

usage: python gen_evex_tables.py [--garbage DIR] [--out DIR] [--sdm-layout sdm2.txt] [-q]
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
VERBOSE = True


try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def log(*a):
    if VERBOSE:
        print(*a, flush=True)


def find_garbage():
    d = HERE
    for _ in range(10):
        if os.path.isfile(os.path.join(d, "asmjit", "db", "isa_x86.json")):
            return d
        d = os.path.dirname(d)
    return None


# ====================================================================================== common vocabulary
VL_ALL = (128, 256, 512)
VLB = {128: 16, 256: 32, 512: 64, "LIG": 16}

TUPLES = ["Full", "Half", "Quarter", "FullMem", "HalfMem", "QuarterMem", "EighthMem", "Tuple1Scalar", "T1F",
          "T2", "T4", "T8", "T1_4X", "Mem128", "MOVDDUP", "N/A", "?"]
TUPLE_NORM = {
    "full": "Full", "fullvector": "Full", "fv": "Full",
    "half": "Half", "halfvector": "Half", "hv": "Half",
    "quarter": "Quarter", "quartervector": "Quarter", "qv": "Quarter",
    "fullmem": "FullMem", "fvm": "FullMem", "fm": "FullMem", "fullmemory": "FullMem",
    "halfmem": "HalfMem", "hvm": "HalfMem", "halfmemory": "HalfMem",
    "quartermem": "QuarterMem", "qvm": "QuarterMem",
    "eighthmem": "EighthMem", "ovm": "EighthMem", "oneeighthmem": "EighthMem",
    "tuple1scalar": "Tuple1Scalar", "t1s": "Tuple1Scalar", "scalar": "Tuple1Scalar", "t1": "Tuple1Scalar",
    "tuple1": "Tuple1Scalar",
    "tuple1fixed": "T1F", "t1f": "T1F",
    "tuple2": "T2", "t2": "T2", "tuple4": "T4", "t4": "T4", "tuple8": "T8", "t8": "T8",
    "tuple14x": "T1_4X", "t14x": "T1_4X",
    "mem128": "Mem128", "m128": "Mem128", "movddup": "MOVDDUP",
    "n/a": "N/A", "na": "N/A", "none": "N/A",
}


def norm_tuple(s):
    k = re.sub(r"[\s_\-]", "", s.strip().lower())
    return TUPLE_NORM.get(k)


EXC_RE = r"E\d{1,2}(?:NF|NP|NM)?(?:\.[A-Za-z0-9]+)?"
MAPS = {"0F": "0F", "0F38": "0F38", "0F3A": "0F3A", "MAP5": "MAP5", "MAP6": "MAP6", "M5": "MAP5", "M6": "MAP6",
        "MAP4": "MAP4", "MAP7": "MAP7"}
PPS = {"NP": "NP", "66": "66", "F2": "F2", "F3": "F3"}
WS = {"W0": "W0", "W1": "W1", "WIG": "WIG"}


def vl_key(v):
    return {128: 0, 256: 1, 512: 2, "LIG": 3}[v]


def vl_str(vls):
    vls = sorted(set(vls), key=vl_key)
    return ",".join(str(v) for v in vls)


# ====================================================================================== Intel text parsing
OPC_RE = re.compile(r"^(EVEX|VEX)\.((?:[0-9A-Za-z]+\.)*(?:0F|0F38|0F3A|MAP[4-7]|M5|M6)(?:\.[0-9A-Za-z]+)*)\s+"
                    r"([0-9A-F]{2}(?:[\s/.].*)?)$")
MODE_RE = re.compile(r"^(V|I|N\.?E\.?|N\.?S\.?|NE|N\.?P\.?)\s*/\s*(V|I|N\.?E\.?|N\.?S\.?|NE|N\.?P\.?)\s*\*?\d?$")
OPEN_RE = re.compile(r"^[A-Z](?:[A-Z0-9]|-(?=[A-Z0-9])){0,9}$")
HEAD_DASH_RE = re.compile(r"^([A-Z][A-Za-z0-9/\[\],:x()\- ]{0,90}?[A-Z0-9\])])\s*[—–]\s*\S")
HEAD_NUM_RE = re.compile(r"^(\d+\.\d+)\.\s+(\S.*)$")
PAGE_RE = re.compile(r"^--- PAGE (\d+) ---$")
PRINTED_RE = re.compile(r"(?:Vol\.\s*(2[ABCD])\s+(\d+-\d+))|(?:(\d+-\d+)\s+Vol\.\s*(2[ABCD]))")
ROW_STOP_RE = re.compile(
    r"^(NOTES?:?|Op\s*/\s*En|Opcode\s*/?|Opcode/Instruction|Instruction Operand Encoding|INSTRUCTION OPERAND ENCODING|"
    r"Encoding / Instruction|Description|Operation|Continued on next page.*|Table continued.*|Document Number.*|"
    r"\d+|\d+(\.\d+)+\.?|Instruction|64/32[- ]bit.*|CPUID.*Feature.*|Flag)$")
CPUID_TOK_RE = re.compile(r"^(AND|OR|and|or|[A-Z][A-Z0-9_.\-]*)$")
INSTR_START_RE = re.compile(r"^[A-Z][A-Z0-9]{1,}(\s|$)")


def is_cpuid_line(s):
    if not s or re.search(r"[a-z]{2,}", s.replace("and", "").replace("or", "")):
        return False
    toks = [t for t in re.split(r"[\s()]+", s) if t]
    return bool(toks) and all(CPUID_TOK_RE.match(t) for t in toks)


def norm_instr(s):
    s = re.sub(r"\s+", " ", s).strip()
    s = re.sub(r"\s*/\s*", "/", s)
    s = re.sub(r"\s*\{\s*", "{", s)
    s = re.sub(r"\s*\}", "}", s)
    s = re.sub(r"\s*,\s*", ", ", s)
    s = re.sub(r"\}(?=[A-Za-z0-9])", "} ", s)
    return s


def parse_header(hdr, notes):
    """'512.66.0F38.W0' -> dict(vl, pp, map, W); unknown tokens noted."""
    out = {"vl": None, "pp": None, "map": None, "W": None}
    for t in hdr.split("."):
        if not t:
            continue
        if t in ("128", "256", "512"):
            out["vl"] = int(t)
        elif t in ("LIG", "LLIG"):
            out["vl"] = "LIG"
        elif t in ("L0", "LZ", "L128"):
            out["vl"] = 128
        elif t in ("L1", "L256"):
            out["vl"] = 256
        elif t in PPS:
            out["pp"] = PPS[t]
        elif t in MAPS:
            out["map"] = MAPS[t]
        elif t in WS:
            out["W"] = WS[t]
        elif t in ("NDS", "NDD", "DDS"):
            pass
        else:
            notes.append(f"unknown header token '{t}'")
    return out


def parse_opcode_rest(rest, notes):
    """'FE /r', '72 /2 ib', '92 /vsib', '6F !(11):rrr:bbb', '07 11:rrr:bbb /ib' -> dict."""
    r = {"opc": None, "digit": "r", "mod": None, "vsib": False, "imm": False}
    rest = rest.strip()
    m = re.match(r"^([0-9A-F]{2})\.(W0|W1|WIG)\s*(.*)$", rest)          # SDM typo 'EVEX.128.66.0F38 30.WIG /r'
    if m:
        notes.append(f"W '{m.group(2)}' written after the opcode byte")
        r["W_typo"] = m.group(2)
        rest = m.group(1) + " " + m.group(3)
    rest = re.sub(r"^([0-9A-F]{2})/", r"\1 /", rest)
    toks = rest.split()
    r["opc"] = toks[0]
    i = 1
    if i < len(toks) and re.fullmatch(r"[0-9A-F]{2}", toks[i]):
        notes.append(f"stray byte '{toks[i]}' after the opcode")
        i += 1
    seen_modrm = False
    for t in toks[i:]:
        if t == "/r":
            seen_modrm = True
        elif re.fullmatch(r"/[0-7]", t):
            r["digit"] = t[1]
            seen_modrm = True
        elif t in ("/vsib", "vsib"):
            r["vsib"] = True
            r["mod"] = "mem"
            seen_modrm = True
        elif t in ("ib", "/ib", "/is4", "is4"):
            r["imm"] = True
        elif t.startswith("!(11)"):
            r["mod"] = "mem"
            seen_modrm = True
            m2 = re.match(r"!\(11\):(\w+):", t)
            if m2 and re.fullmatch(r"[01]{3}", m2.group(1)):
                r["digit"] = str(int(m2.group(1), 2))
        elif t.startswith("11:"):
            r["mod"] = "reg"
            seen_modrm = True
            m2 = re.match(r"11:(\w+):", t)
            if m2 and re.fullmatch(r"[01]{3}", m2.group(1)):
                r["digit"] = str(int(m2.group(1), 2))
        else:
            notes.append(f"unknown opcode token '{t}'")
    if not seen_modrm:
        notes.append("no ModRM spec (/r, /digit) in opcode column")
    return r


OPND_ROLE_RE = re.compile(r"(ModRM:reg|MODRM\.REG|ModRM:r/m|MODRM\.R/M|EVEX\.vvvv|VEX\.vvvv|VVVV|imm8|IMM8|Imm8|"
                          r"BaseReg|VSIB)", re.I)


def openc_letters(opnd_text):
    out = ""
    for m in OPND_ROLE_RE.finditer(opnd_text):
        t = m.group(1).lower()
        if "vvvv" in t and out.endswith("v"):
            continue        # 'VEX.vvvv (r) / EVEX.vvvv (r)' is one cell
        if "reg" in t and "modrm" in t:
            out += "r"
        elif "r/m" in t or t in ("basereg", "vsib"):
            if not out.endswith("m"):
                out += "m"
        elif "vvvv" in t:
            out += "v"
        elif "imm8" in t:
            out += "i"
    return out


class Section:
    def __init__(self, doc, heading):
        self.doc = doc
        self.heading = heading
        self.rows = []
        self.openc = {}          # op/en letter -> (tuple, operand text)
        self.openc_clash = []
        self.exc = collections.defaultdict(set)   # mnemonic or '*' -> {class}
        self.last_instr_mn = None
        self.in_exc = False


def expand_slash_mnemonics(tok, known=()):
    """'VPADDD/Q' -> VPADDD, VPADDQ; 'VPERMI2D/Q/PS/PD' -> ..., VPERMI2PS, VPERMI2PD (a stem that yields a
    mnemonic of the page's opcode table wins over the plain same-length suffix replacement)."""
    parts = tok.split("/")
    first = parts[0]
    out = [first]
    for p in parts[1:]:
        if not p:
            continue
        if len(p) >= len(first) - 1 or p.startswith("V"):
            out.append(p)
            continue
        hit = None
        for k in range(1, len(first)):
            cand = first[:len(first) - k] + p
            if cand in known:
                hit = cand
                break
        out.append(hit or (first[:len(first) - len(p)] + p))
    return out


def parse_intel_doc(path, docname, stats, is_sdm):
    lines = [l.rstrip("\n") for l in open(path, encoding="utf-8", errors="replace")]
    sections = []
    cur = Section(docname, "(front matter)")
    sections.append(cur)
    by_key = {}
    page = 0
    printed = ""
    nrows = 0
    i = 0
    n = len(lines)
    while i < n:
        s = lines[i].strip()
        pm = PAGE_RE.match(s)
        if pm:
            page = int(pm.group(1))
            printed = ""
            for k in range(i + 1, min(i + 5, n)):
                mm = PRINTED_RE.search(lines[k])
                if mm:
                    printed = (mm.group(1) or mm.group(4)) + " " + (mm.group(2) or mm.group(3))
                    break
            i += 1
            continue
        # ---- headings
        if ". . ." not in s and len(s) < 140:
            hm = HEAD_DASH_RE.match(s)
            if hm and re.search(r"[a-z]{2,}", hm.group(1).replace("cc", "")):
                hm = None       # prose with an en dash ('...bits MAXVL-1:128'), not an instruction heading
            hn = HEAD_NUM_RE.match(s) if docname.startswith("AVX10.2") else None
            if hm or hn:
                # The section identity is the mnemonic list before the dash (or the AVX10.2 section number): the
                # first page of an SDM section wraps the long title, later pages repeat it in full.
                key = hm.group(1).strip() if hm else hn.group(1)
                sec = by_key.get(key)
                if sec is None:
                    sec = Section(docname, s)
                    by_key[key] = sec
                    sections.append(sec)
                elif len(s) > len(sec.heading):
                    sec.heading = s
                cur = sec
                i += 1
                continue
        if s in ("EXCEPTIONS", "Exception Type", "Other Exceptions"):
            cur.in_exc = True
        # ---- opcode-table row
        om = OPC_RE.match(s)
        if om:
            row, i = parse_row(lines, i, om, docname, page, printed, cur.heading)
            if row:
                cur.rows.append(row)
                nrows += 1
            continue
        # ---- operand-encoding table
        if re.fullmatch(r"Op\s*/\s*En", s):
            look = [lines[k].strip() for k in range(i + 1, min(i + 4, n))]
            if any(re.fullmatch(r"Tuple( Type)?|TupleType", x, re.I) for x in look):
                i = parse_openc_table(lines, i, cur, stats, True)
                continue
            if look and re.fullmatch(r"Operand\s*1", look[0]):
                i = parse_openc_table(lines, i, cur, stats, False)
                continue
        # ---- exceptions
        if "Type E" in s or re.fullmatch(r"(Type\s+)?" + EXC_RE, s):
            collect_exc(lines, i, cur)
        im = re.match(r"^(V[A-Z0-9]{2,}|K[A-Z]{3,}[BWDQ]?)\s+(?:[a-z{]|k\d|xmm|ymm|zmm|r\d|m\d)", s)
        if not im and cur.in_exc:
            im = re.match(r"^(V[A-Z0-9]{2,}|T[A-Z0-9]{3,})$", s)   # exception table: mnemonic cell wrapped alone
        if im:
            cur.last_instr_mn = im.group(1)
        i += 1
    stats[f"{docname}: rows"] = nrows
    log(f"[intel] {docname}: {len(lines)} lines, {len(sections)} sections, {nrows} EVEX/VEX opcode rows")
    return sections


def parse_row(lines, i, om, docname, page, printed, heading):
    kind, hdr, rest = om.group(1), om.group(2), om.group(3)
    notes = []
    j = i + 1
    block = []
    n = len(lines)
    skipping_notes = 0
    while j < n and len(block) < 30:
        s = lines[j].strip()
        if not s:
            j += 1
            continue
        if skipping_notes:
            # a NOTES block printed between the mode cell and the CPUID cell (e.g. VCVTTSD2USI r64 'V/N.E.1')
            if is_cpuid_line(s) or skipping_notes > 8:
                skipping_notes = 0
            else:
                skipping_notes += 1
                j += 1
                continue
        if re.fullmatch(r"NOTES?:?", s) and any(MODE_RE.match(x) for x in block[-2:]):
            skipping_notes = 1
            j += 1
            continue
        if OPC_RE.match(s) or PAGE_RE.match(s) or ROW_STOP_RE.match(s):
            break
        if re.match(r"^(NP|66|F2|F3|REX|NFx)\b.*\b[0-9A-F]{2}\b", s) and not re.search(r"[a-z]{3,}", s):
            break       # legacy opcode row
        if ". . ." in s or HEAD_DASH_RE.match(s):
            break
        block.append(s)
        j += 1
    # opcode cell wrapped onto the next line ('EVEX.512.66.0F3A.W0 04 /r' + 'ib')
    while block and re.fullmatch(r"(/r|ib|/ib|/is4|/vsib|/[0-7])(\s+(ib|/ib))?", block[0]):
        rest = rest + " " + block.pop(0)
        notes.append("opcode cell continued on the next line")
    if block and re.match(r"^ib(V[A-Z0-9]+\s)", block[0]):     # 'ibVPERMILPS zmm1 ...' (cells glued together)
        rest = rest + " ib"
        block[0] = block[0][2:]
        notes.append("'ib' of the opcode cell glued to the instruction cell")
    # Op/En + mode pair
    k = None
    for q in range(len(block) - 1):
        if OPEN_RE.match(block[q]) and MODE_RE.match(block[q + 1]):
            k = q
            break
    if k is None:
        return ({"bad": True, "kind": kind, "opcode": lines[i].strip(), "page": page, "doc": docname,
                 "heading": heading, "block": block[:6]}, j)
    op_en = block[k]
    mode = block[k + 1]
    q = k + 2
    cpuid = []
    while q < len(block) and is_cpuid_line(block[q]):
        cpuid.append(block[q])
        q += 1
    if k > 0:
        instr = " ".join(block[:k])
    else:
        ins = []
        for x in block[q:]:
            if not ins and not INSTR_START_RE.match(x):
                break
            if ins and re.match(r"^[A-Z][a-z]", x):
                break
            ins.append(x)
        instr = " ".join(ins)
    hd = parse_header(hdr, notes)
    rr = parse_opcode_rest(rest, notes)
    if rr.get("W_typo"):
        hd["W"] = rr["W_typo"]
    row = {"kind": kind, "opcode": lines[i].strip(), "instr": norm_instr(instr), "op_en": op_en, "mode": mode,
           "cpuid": " ".join(cpuid), "page": page, "printed": printed, "doc": docname, "heading": heading,
           "notes": notes}
    row.update(hd)
    row.update({k2: v for k2, v in rr.items() if k2 != "W_typo"})
    return row, j


def parse_openc_table(lines, i, sec, stats, has_tuple=True):
    n = len(lines)
    j = i + 1
    # skip header cells
    while j < n and j < i + 9:
        s = lines[j].strip()
        if not s or re.fullmatch(r"Op\s*/\s*En|Tuple( Type)?|TupleType|Operand\s*\d", s, re.I):
            j += 1
            continue
        break
    body = []
    while j < n:
        s = lines[j].strip()
        if not s:
            j += 1
            continue
        if PAGE_RE.match(s) or re.match(r"^\d\.\s", s) or re.match(r"^(Description|Opcode\s*/?|Opcode/Instruction|NOTES?:?|Operation|"
                                        r"\d+(\.\d+)+\.?|DESCRIPTION|Encoding / Instruction|EXCEPTIONS)$", s) \
                or OPC_RE.match(s) or HEAD_DASH_RE.match(s):
            break
        body.append(s)
        j += 1
    rows = parse_openc_body(body, has_tuple, stats)
    if not rows and has_tuple:
        # PyMuPDF sometimes emits the table body *before* its header (table split over a column break, e.g.
        # PMINSB/PMINSW): take the contiguous block of table-cell lines that ends right above the header.
        k = i - 1
        frag = []
        while k > 0 and len(frag) < 60:
            t = lines[k].strip()
            if not t:
                k -= 1
                continue
            if OPEN_RE.match(t) or norm_tuple(t) or CELL_RE.search(t):
                frag.insert(0, t)
                k -= 1
                continue
            break
        rows = parse_openc_body(frag, has_tuple, stats)
        if rows:
            stats.setdefault("Op/En tables recovered from a fragment above the header", []).append(
                f"{sec.doc} :: {sec.heading} :: {[r[0] for r in rows]}")
    for letter, tup, ops in rows:
        txt = " | ".join(ops)
        if letter in sec.openc and sec.openc[letter][0] != tup:
            sec.openc_clash.append((letter, sec.openc[letter][0], tup))
            if sec.openc[letter][0] != "N/A":
                continue
        sec.openc[letter] = (tup, txt)
    return j


CELL_RE = re.compile(r"ModRM|vvvv|VVVV|imm8|IMM8|^N/A$|Implicit|BaseReg|VSIB|MODRM|Opmask")


def parse_openc_body(body, has_tuple, stats):
    rows = []
    q = 0
    while q < len(body):
        s = body[q]
        if not has_tuple:
            # no Tuple Type column: register-only or legacy table; every Op/En maps to N/A
            if OPEN_RE.match(s) and q + 1 < len(body) and re.search(r"ModRM|vvvv|VVVV|imm|N/A|Implicit|BaseReg|"
                                                                     r"MODRM|Opmask|k\d", body[q + 1]):
                rows.append([s, "N/A", []])
                q += 1
                continue
            if rows:
                rows[-1][2].append(s)
            q += 1
            continue
        if OPEN_RE.match(s) and q + 1 < len(body):
            t2 = norm_tuple(body[q + 1] + (" " + body[q + 2] if q + 2 < len(body) else ""))
            t1 = norm_tuple(body[q + 1])
            if t2 and q + 2 < len(body) and not t1:
                rows.append([s, t2, []])
                q += 3
                continue
            if t1:
                rows.append([s, t1, []])
                q += 2
                continue
            if re.fullmatch(r"[A-Za-z0-9 _\-]{2,20}", body[q + 1]) and not re.search(r"ModRM|vvvv|imm|N/A", body[q + 1]) \
                    and len(rows) < 30 and not OPEN_RE.match(body[q + 1]):
                stats.setdefault("unknown tuple words", collections.Counter())[body[q + 1]] += 1
        if rows:
            rows[-1][2].append(s)
        q += 1
    return rows


def collect_exc(lines, i, sec):
    s = lines[i].strip()
    m = re.fullmatch(r"(?:Type\s+)?(" + EXC_RE + r")", s)
    if m:       # table style (AVX10.2): class cell after the instruction cell
        if not sec.in_exc or not sec.doc.startswith("AVX10.2"):
            return      # a bare 'E0'.. in an SDM page is table data (e.g. the GF2P8AFFINEINVQB inverse table)
        if sec.last_instr_mn:
            sec.exc[sec.last_instr_mn].add(m.group(1))
        else:
            sec.exc["*"].add(m.group(1))
        return
    ctx = s
    prev = lines[i - 1].strip() if i > 0 else ""
    if "EVEX" not in ctx and "EVEX" in prev:
        ctx = prev + " " + ctx
    # 'Type E<n>' classes exist only for EVEX encodings, so an EVEX-only page may just say
    # 'See Table 2-51, "Type E4 Class Exception Conditions."' without naming EVEX
    if "EVEX" not in ctx and not re.search(r"Type\s+E\d", ctx):
        return
    seen_cls = []
    for mm in re.finditer(r"Type\s+(" + EXC_RE + r")", ctx):
        cls = mm.group(1)
        # 'see Exceptions Type E4.nb in Table 2-51, "Type E4 Class Exception Conditions."': the second hit is
        # only the title of the table the specific class lives in
        if any(p.split(".")[0] == cls and p != cls for p in seen_cls):
            continue
        seen_cls.append(cls)
        before = ctx[:mm.start()]
        before = before.split("see")[0] if "see" in before else before
        mns = []
        known = {r["instr"].split(" ")[0].upper() for r in sec.rows if not r.get("bad")}
        for tok in re.findall(r"\b(V[A-Z0-9]{2,}(?:/[A-Z0-9]+)*)", before):
            mns += expand_slash_mnemonics(tok, known)
        if mns:
            for x in mns:
                sec.exc[x].add(cls)
        else:
            sec.exc["*"].add(cls)


# --------------------------------------------------------------------- Intel operand analysis
def split_operands(instr):
    mn, _, ops = instr.partition(" ")
    out = []
    depth = 0
    curo = ""
    for ch in ops:
        if ch == "," and depth == 0:
            out.append(curo.strip())
            curo = ""
            continue
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        curo += ch
    if curo.strip():
        out.append(curo.strip())
    return mn, out


MEM_TOK_RE = re.compile(r"^m(8|16|32|64|128|256|512)$")


def analyze_sdm_operands(instr):
    """-> dict(mn, ops, mask, bcst, rc, mod, memsize, vsib, kdest)"""
    mn, ops = split_operands(instr)
    res = {"mn": mn.lower(), "ops": ops, "bcst": 0, "rc": "", "memsize": 0, "vsib": False}
    has_mem_alt = has_reg_alt = pure_mem = False
    first_dec = ""
    for idx, o in enumerate(ops):
        decs = re.findall(r"\{([^}]*)\}", o)
        base = re.sub(r"\{[^}]*\}", "", o).strip()
        if idx == 0:
            first_dec = ",".join(decs)
        for d in decs:
            if d.lower() == "er":
                res["rc"] = "er"
            elif d.lower() == "sae":
                res["rc"] = "sae"
        alts = [a for a in base.split("/") if a]
        if base.startswith("r/m"):
            alts = ["r" + base[3:], "m" + base[3:]]
        mems = []
        regs = []
        for a in alts:
            a2 = a.strip()
            if re.fullmatch(r"mm(8|16|32|64|128|256|512)", a2):
                res.setdefault("typos", []).append(f"'{a2}' read as '{a2[1:]}'")
                a2 = a2[1:]
            mb = re.fullmatch(r"m(16|32|64)bcst", a2)
            if mb:
                res["bcst"] = int(mb.group(1))
                continue
            if MEM_TOK_RE.match(a2) or a2 in ("mem", "m"):
                mems.append(a2)
                mm = MEM_TOK_RE.match(a2)
                if mm:
                    res["memsize"] = max(res["memsize"], int(mm.group(1)) // 8)
                continue
            mv = re.fullmatch(r"vm(32|64)([xyz])", a2)
            if mv:
                mems.append(a2)
                res["vsib"] = True
                res["memsize"] = int(mv.group(1)) // 8
                continue
            regs.append(a2)
        if mems and regs:
            has_mem_alt = has_reg_alt = True
        elif mems:
            pure_mem = True
    res["mod"] = "any" if has_mem_alt else ("mem" if pure_mem else "reg")
    first = re.sub(r"\{[^}]*\}", "", ops[0]).strip() if ops else ""
    if re.fullmatch(r"k\d", first):
        res["mask"] = "kdest" if "k" in first_dec else "none"
    elif "z" in first_dec.split(","):
        res["mask"] = "mz"
    elif re.search(r"\bk\d\b", first_dec):
        res["mask"] = "kreq" if res["vsib"] else "m"
    else:
        # mask written on a later operand (rare) ?
        other = [d for o in ops[1:] for d in re.findall(r"\{(k\d)\}", o)]
        res["mask"] = ("kreq" if res["vsib"] else "m") if other else "none"
    return res


def clean_cpuid_text(text):
    t = re.sub(r"_\s+", "_", text)                     # 'AVX10_V1_ AUX' (cell wrapped after '_')
    t = re.sub(r"\)\s+R\s", ") OR ", t)                # '(AVX512VL AND AVX512F) R AVX10.11' (lost 'O')
    t = re.sub(r"\bAVX10\.(\d)\d\b", r"AVX10.\1", t)    # 'AVX10.11' = AVX10.1 + footnote 1
    t = re.sub(r"\b(AVX512[A-Z]{1,2})\d\b", r"\1", t)   # 'AVX512F1' footnote
    return re.sub(r"\s+", " ", t).strip()


def norm_cpuid(text):
    text = clean_cpuid_text(text)
    toks = [t for t in re.split(r"[\s()]+", text) if t and t not in ("AND", "OR", "and", "or")]
    vl = "AVX512VL" in toks
    avx10 = sorted({t for t in toks if t.startswith("AVX10")})
    fam = [t for t in toks if t != "AVX512VL" and not t.startswith("AVX10")]
    return fam, vl, avx10


FAMILY_ALIASES = {"AVX512_F": "AVX512F", "AVX512_BW": "AVX512BW", "AVX512_DQ": "AVX512DQ", "AVX512_CD": "AVX512CD",
                  "AVX512_ER": "AVX512ER", "AVX512_PF": "AVX512PF", "AVX512_VL": "AVX512VL"}


def family_of(fams, avx10):
    f = [FAMILY_ALIASES.get(x, x) for x in fams]
    f = [x for x in f if x not in ("AVX512VL",)]
    f2 = [x for x in f if x != "AVX512F"]
    if f2:
        return "+".join(sorted(set(f2)))
    if f:
        return "AVX512F"
    if avx10:
        # 'AVX10.2 OR AVX10_V1_AUX': the family is AVX10.2 (the AUX bit is an alternative enumeration)
        for pref in ("AVX10.2", "AVX10.1"):
            if pref in avx10:
                return pref
        return avx10[-1]
    return "?"


# ====================================================================================== asmjit
AJ_EXT = {"AVX512_F": "AVX512F", "AVX512_BW": "AVX512BW", "AVX512_DQ": "AVX512DQ", "AVX512_CD": "AVX512CD",
          "AVX512_FP16": "AVX512_FP16", "AVX512_BF16": "AVX512_BF16", "AVX512_VBMI": "AVX512_VBMI",
          "AVX512_VBMI2": "AVX512_VBMI2", "AVX512_VNNI": "AVX512_VNNI", "AVX512_BITALG": "AVX512_BITALG",
          "AVX512_VPOPCNTDQ": "AVX512_VPOPCNTDQ", "AVX512_IFMA": "AVX512_IFMA",
          "AVX512_VP2INTERSECT": "AVX512_VP2INTERSECT", "AVX10_2": "AVX10.2", "VAES": "VAES", "GFNI": "GFNI",
          "VPCLMULQDQ": "VPCLMULQDQ", "SM4": "SM4", "MOVRS": "MOVRS", "AMX_AVX512": "AMX-AVX512"}
AJ_SIZE = {128: ("xmm", "m128"), 256: ("ymm", "m256"), 512: ("zmm", "m512")}
AJ_HALF = {128: ("xmm", "m64"), 256: ("xmm", "m128"), 512: ("ymm", "m256")}
AJ_QUART = {128: ("xmm", "m32"), 256: ("xmm", "m64"), 512: ("xmm", "m128")}


def aj_operands_for_vl(ops, vl, wbits):
    out = []
    for o in ops:
        o = re.sub(r"^[WRXwrx]:", "", o.strip())
        o = o.replace("~", "")
        decs = " ".join(re.findall(r"\{[^}]*\}", o))
        base = re.sub(r"\{[^}]*\}", "", o).strip()
        alts = []
        for a in base.split("/"):
            a = a.strip()
            a = re.sub(r"@\d+|\[\d+:\d+\]", "", a)
            v = vl if vl != "LIG" else 128
            if a == "xyz":
                a = AJ_SIZE[v][0]
            elif a == "mxyz":
                a = AJ_SIZE[v][1]
            elif a == "xxy":
                a = AJ_HALF[v][0]
            elif a == "mxxy":
                a = AJ_HALF[v][1]
            elif a == "xxx":
                a = AJ_QUART[v][0]
            elif a == "mxxx":
                a = AJ_QUART[v][1]
            elif a == "ry":
                a = "r64" if wbits == 64 else "r32"
            elif a == "my":
                a = "m64" if wbits == 64 else "m32"
            elif re.fullmatch(r"b(16|32|64)", a):
                a = "m" + a[1:] + "bcst"
            alts.append(a)
        out.append("/".join(alts) + ((" " + decs) if decs else ""))
    return out


def parse_asmjit(path, stats):
    d = json.load(open(path, encoding="utf-8"))
    recs = []
    apx = []
    bad = []
    nrows = 0
    for g in d["instructions"]:
        gext = g.get("ext", "")
        for r in g["instructions"]:
            for field in ("any", "x86", "x64", "apx"):
                enc = r.get(field)
                if not enc or "EVEX" not in enc:
                    continue
                nrows += 1
                inst = r["inst"]
                if field == "apx" or g.get("category") == "GP" or ".MAP4." in enc:
                    apx.append({"inst": inst, "enc": enc, "ext": r.get("ext", gext), "field": field})
                    continue
                m = re.match(r"^\s*(\w+)\s*:\s*EVEX\.(\S+)\s+(.*)$", enc)
                if not m:
                    bad.append((inst, enc, "cannot split encoding"))
                    continue
                openc, hdr, rest = m.group(1), m.group(2), m.group(3).strip()
                notes = []
                toks = hdr.split(".")
                vlt = toks[0]
                hd = parse_header(".".join(toks[1:]), notes)
                hd_notes = []
                if hd["pp"] is None:
                    hd_notes.append("pp missing in asmjit encoding (taken as NP)")
                    hd["pp"] = "NP"
                if hd["map"] is None:
                    hd_notes.append("map missing in asmjit encoding")
                    hd["map"] = "?"
                if "Wy" in toks:
                    wlist = [("W0", 32), ("W1", 64)]
                elif hd["W"] is None:
                    hd_notes.append("W missing in asmjit encoding")
                    wlist = [("W?", 32)]
                else:
                    wlist = [(hd["W"], 64 if hd["W"] == "W1" else 32)]
                rr = parse_opcode_rest(rest, notes)
                if vlt == "xyz":
                    vls = list(VL_ALL)
                elif vlt in ("128", "256", "512"):
                    vls = [int(vlt)]
                elif vlt in ("LIG", "LLIG"):
                    vls = ["LIG"]
                else:
                    vls = []
                    bad.append((inst, enc, f"vl token {vlt}"))
                mn, _, opstr = inst.partition(" ")
                ops = [o for o in re.split(r",\s*(?![^{]*\})", opstr) if o.strip()]
                # masking
                o0 = ops[0] if ops else ""
                if "k_zeroing" in o0:
                    mask = "kdest"
                elif "{k|z}" in o0 or "{k|k_blending|z}" in o0:
                    mask = "mz"
                elif "{k}" in o0:
                    mask = "m"
                else:
                    mask = "none"
                rc = "er" if "{er}" in opstr else ("sae" if "{sae}" in opstr else "")
                mb = re.search(r"/b(16|32|64)\b", opstr)
                bcst = int(mb.group(1)) if mb else 0
                vsib = bool(re.search(r"\bvm(32|64)[xyz]\b", opstr))
                if vsib and mask == "m":
                    mask = "kreq"
                tt = r.get("tt")
                tup = norm_tuple(tt) if tt else "?"
                if tt and not tup:
                    bad.append((inst, enc, f"unknown tt {tt}"))
                    tup = "?"
                gex = [AJ_EXT.get(x, x) for x in gext.split()]
                fam = family_of(gex, ["AVX10.2"] if "AVX10.2" in gex else [])
                vlfeat = r.get("vl") == "xy"
                for (W, wbits) in wlist:
                    for vl in vls:
                        vops = aj_operands_for_vl(ops, vl, wbits)
                        mod = rr["mod"]
                        memsize = 0
                        has_m = has_r = pure_m = False
                        for o in vops:
                            alts = re.sub(r"\{[^}]*\}", "", o).strip().split("/")
                            ms = [a for a in alts if MEM_TOK_RE.match(a) or re.fullmatch(r"vm(32|64)[xyz]", a)]
                            rs = [a for a in alts if a and a not in ms and not a.endswith("bcst")]
                            for a in ms:
                                mm2 = MEM_TOK_RE.match(a)
                                memsize = max(memsize, int(mm2.group(1)) // 8 if mm2 else int(a[2:4]) // 8)
                            if ms and rs:
                                has_m = has_r = True
                            elif ms:
                                pure_m = True
                        if mod is None:
                            mod = "any" if has_m else ("mem" if pure_m else "reg")
                        recs.append({
                            "src": "asmjit", "mn": mn.lower(), "map": hd["map"], "pp": hd["pp"], "opc": rr["opc"],
                            "digit": rr["digit"], "mod": mod, "vsib": vsib or rr["vsib"], "W": W, "vl": vl,
                            "imm": rr["imm"], "openc": openc.replace("i", ""), "operands": f"{mn} " + ", ".join(vops),
                            "tuple": tup, "bcst": bcst, "mask": mask, "rc": rc, "family": fam, "vlfeat": vlfeat,
                            "avx10": "AVX10.2" if fam == "AVX10.2" else "", "cpuid": gext, "exc": "",
                            "memsize": memsize, "ref": f"asmjit isa_x86.json: {inst} | {enc}",
                            "notes": notes + hd_notes, "aj_inst": inst, "aj_enc": enc,
                        })
    stats["asmjit EVEX rows"] = nrows
    stats["asmjit APX rows"] = len(apx)
    log(f"[asmjit] {nrows} EVEX encodings: {len(apx)} APX/MAP4-promoted (separate list), "
        f"{len(recs)} per-VL records from the rest, {len(bad)} unparsed")
    for b in bad:
        log(f"[asmjit]   unparsed: {b}")
    return recs, apx, bad


def old_avx102_name(mn):
    """asmjit still spells several AVX10.2 instructions with the names of the first AVX10.2 spec revisions."""
    m = mn
    m = re.sub(r"nepbf16$", "bf16", m)
    m = re.sub(r"pbf16$", "bf16", m)
    m = m.replace("cvtne2ph2", "cvt2ph2").replace("cvtneph2", "cvtph2").replace("cvttnebf162", "cvttbf162")
    m = m.replace("cvtnebf162", "cvtbf162").replace("tcvtrowps2pbf16", "tcvtrowps2bf16")
    if m == "vcomsbf16":
        m = "vcomisbf16"
    return m


# ====================================================================================== XED
XED_NELEM = {"NELEM_FULL()": "Full", "NELEM_HALF()": "Half", "NELEM_QUARTER()": "Quarter",
             "NELEM_FULLMEM()": "FullMem", "NELEM_HALFMEM()": "HalfMem", "NELEM_QUARTERMEM()": "QuarterMem",
             "NELEM_EIGHTHMEM()": "EighthMem", "NELEM_ONE()": "Tuple1Scalar", "NELEM_SCALAR()": "Tuple1Scalar",
             "NELEM_GSCAT()": "Tuple1Scalar", "NELEM_GPR_READER()": "T1F", "NELEM_GPR_WRITER()": "T1F",
             "NELEM_GPR_READER_BYTE()": "Tuple1Scalar", "NELEM_GPR_READER_WORD()": "Tuple1Scalar",
             "NELEM_GPR_WRITER_STORE()": "T1F", "NELEM_GPR_WRITER_STORE_BYTE()": "Tuple1Scalar",
             "NELEM_GPR_WRITER_STORE_WORD()": "Tuple1Scalar", "NELEM_GPR_WRITER_LDOP_D()": "T1F",
             "NELEM_GPR_WRITER_LDOP_Q()": "T1F", "NELEM_GPR_READER_SUBDWORD()": "Tuple1Scalar",
             "NELEM_TUPLE1()": "Tuple1Scalar", "NELEM_TUPLE1_BYTE()": "Tuple1Scalar",
             "NELEM_TUPLE1_WORD()": "Tuple1Scalar", "NELEM_TUPLE1_SUBDWORD()": "Tuple1Scalar",
             "NELEM_TUPLE2()": "T2", "NELEM_TUPLE4()": "T4", "NELEM_TUPLE8()": "T8", "NELEM_TUPLE1_4X()": "T1_4X",
             "NELEM_MEM128()": "Mem128", "NELEM_MOVDDUP()": "MOVDDUP"}


def parse_xed(xdir, stats):
    if not xdir or not os.path.isdir(xdir):
        log(f"[xed] datafiles not found ({xdir}); XED fill-in disabled")
        return []
    files = []
    for p in glob.glob(os.path.join(xdir, "**", "*.txt"), recursive=True):
        rel = os.path.relpath(p, xdir)
        if "tests" in rel.split(os.sep) or re.search(r"nop-remove|nop-mod", os.path.basename(p)):
            continue
        txt = open(p, encoding="utf-8", errors="replace").read()
        if "EVV" not in txt or not re.search(r"^\s*ICLASS\s*:", txt, re.M):
            continue
        files.append((rel, txt))
    FIELD = re.compile(r"^\s*([A-Z_0-9]+)\s*:\s*(.*?)\s*$")
    recs = []
    for rel, txt in sorted(files):
        cur = None
        for ln in txt.split("\n"):
            s = ln.split("#", 1)[0].rstrip()
            st = s.strip()
            if st == "{":
                cur = {"file": rel, "variants": []}
                continue
            if st == "}":
                if cur and "ICLASS" in cur:
                    recs.append(cur)
                cur = None
                continue
            if cur is None:
                continue
            m = FIELD.match(s)
            if not m:
                continue
            k, v = m.group(1), m.group(2)
            if k == "PATTERN":
                cur["variants"].append({"PATTERN": v})
            elif k in ("OPERANDS", "IFORM") and cur["variants"]:
                cur["variants"][-1][k] = v
            else:
                cur[k] = v
    out = []
    for r in recs:
        for var in r["variants"]:
            t = var["PATTERN"].split()
            if not t or t[0] != "EVV":
                continue
            mp = next((x for x in t if x in ("V0F", "V0F38", "V0F3A", "MAP4", "MAP5", "MAP6", "MAP7")), "?")
            mp = {"V0F": "0F", "V0F38": "0F38", "V0F3A": "0F3A"}.get(mp, mp)
            if mp in ("MAP4", "MAP7") or "EVAPX()" in t or "EVAPX_SCC()" in t or                     r.get("ISA_SET", "").startswith("APX"):
                continue
            opc = next((x[2:].upper() for x in t if re.fullmatch(r"0x[0-9A-Fa-f]{2}", x)), "?")
            pp = next(({"VNP": "NP", "V66": "66", "VF2": "F2", "VF3": "F3"}[x] for x in t
                       if x in ("VNP", "V66", "VF2", "VF3")), "NP")
            W = "W0" if "W0" in t else ("W1" if "W1" in t else "WIG")
            digit = "r"
            for x in t:
                m = re.fullmatch(r"REG\[0b([01]{3})\]", x)
                if m:
                    digit = str(int(m.group(1), 2))
            mod = "reg" if "MOD=3" in t else ("mem" if "MOD!=3" in t else "any")
            if "VL128" in t:
                vl = 128
            elif "VL256" in t:
                vl = 256
            elif "VL512" in t or "FIX_ROUND_LEN512()" in t:
                vl = 512
            else:
                vl = "LIG"
            ops = var.get("OPERANDS", "")
            if re.search(r"REG0=MASK_R\(\)", ops):
                mask = "kdest" if "MASK1()" in ops else "none"
            elif "MASKNOT0()" in ops:
                mask = "kreq"
            elif "MASK1()" in ops:
                mask = "mz" if ("ZEROSTR" in ops and "ZEROING=0" not in t) else "m"
            else:
                mask = "none"
            tup = next((XED_NELEM[x] for x in t if x in XED_NELEM), "N/A" if mod == "reg" else "?")
            es = next((int(re.match(r"ESIZE_(\d+)_BITS", x).group(1)) for x in t if x.startswith("ESIZE_")), 0)
            bcst = es if ("BCRC=1" in t and mod == "mem") else 0
            rc = "er" if "AVX512_ROUND()" in t else ("sae" if "SAE()" in t else "")
            exc = r.get("EXCEPTIONS", "")
            exc = re.sub(r"^AVX512-", "", exc)
            out.append({"src": "xed", "mn": re.sub(r"_[0-9a-f]{2}$", "", r["ICLASS"].lower()), "map": mp, "pp": pp, "opc": opc, "digit": digit,
                        "mod": mod, "W": W, "vl": vl, "tuple": tup, "bcst": bcst, "mask": mask, "rc": rc,
                        "isa_set": r.get("ISA_SET", ""), "exc": exc, "imm": "UIMM8()" in t or "SIMM8()" in t,
                        "vsib": "VMODRM" in var["PATTERN"] or "VSIB" in ops.upper(), "file": r["file"],
                        "iform": var.get("IFORM", ""), "esize": es})
    stats["xed EVEX variants"] = len(out)
    log(f"[xed] {len(files)} datafiles with EVV patterns, {len(out)} non-APX EVEX pattern variants")
    return out


def xed_index(xrecs):
    idx = collections.defaultdict(list)
    for x in xrecs:
        idx[(x["mn"], x["map"], x["pp"], x["opc"], x["digit"])].append(x)
    return idx


def xed_view(xidx, mn, mp, pp, opc, digit, vl, W):
    cands = [x for x in xidx.get((mn, mp, pp, opc, digit), []) if (x["vl"] == vl or x["vl"] == "LIG" or vl == "LIG")]
    if not cands:
        return None
    if W in ("W0", "W1"):
        c2 = [x for x in cands if x["W"] in (W, "WIG")]
        cands = c2 or cands
    tup = sorted({x["tuple"] for x in cands if x["mod"] != "reg" and x["tuple"] not in ("?", "N/A")})
    return {"tuple": "/".join(tup) or "N/A", "W": "/".join(sorted({x["W"] for x in cands})),
            "bcst": max([x["bcst"] for x in cands] + [0]),
            "mask": "/".join(sorted({x["mask"] for x in cands})),
            "rc": "/".join(sorted({x["rc"] for x in cands if x["rc"]})),
            "exc": "/".join(sorted({x["exc"] for x in cands if x["exc"]})),
            "isa": "/".join(sorted({x["isa_set"] for x in cands}))}


# ====================================================================================== disp8*N
def disp8_n(tup, vl, W, bcst, memsize):
    """SDM Vol.2A Tables 2-36 / 2-37 (and the AVX512-FP16 'Quarter' tuple). -> (N for b=0 or None, N for b=1 or None,
    error string or '')."""
    v = VLB.get(vl, 16)
    if tup in ("Full", "Half", "Quarter"):
        div = {"Full": 1, "Half": 2, "Quarter": 4}[tup]
        nb = bcst // 8 if bcst else None
        return v // div, nb, ""
    if tup == "FullMem":
        return v, None, ""
    if tup == "HalfMem":
        return v // 2, None, ""
    if tup == "QuarterMem":
        return v // 4, None, ""
    if tup == "EighthMem":
        return v // 8, None, ""
    if tup == "Mem128":
        return 16, None, ""
    if tup == "MOVDDUP":
        return {128: 8, 256: 32, 512: 64}.get(vl, 8), None, ""
    if tup == "Tuple1Scalar":
        # Table 2-37: N = InputSize (8/16/32/64 bit); the EVEX.W column only says which W usually selects 32/64.
        # The memory operand (one element) gives the input size directly; compress/expand/gather/scatter
        # (element-granular full-vector or VSIB memory) take the element size from W.
        wsz = 4 if W == "W0" else (8 if W == "W1" else None)
        if memsize and memsize <= 8:
            return memsize, None, ""
        if wsz:
            return wsz, None, ""
        return None, None, "T1S with WIG and no element-sized memory operand"
    if tup == "T1F":
        return memsize or None, None, ("" if memsize in (4, 8) else f"T1F with memory size {memsize}")
    if tup == "T2":
        if W == "W1":
            return (16, None, "") if vl in (256, 512) else (16, None, f"Tuple2 W1 at VL{vl} is NA in Table 2-37")
        return 8, None, ""
    if tup == "T4":
        if W == "W1":
            return (32, None, "") if vl == 512 else (32, None, f"Tuple4 W1 at VL{vl} is NA in Table 2-37")
        return (16, None, "") if vl in (256, 512) else (16, None, f"Tuple4 W0 at VL{vl} is NA in Table 2-37")
    if tup == "T8":
        return (32, None, "") if vl == 512 else (32, None, f"Tuple8 at VL{vl} is NA in Table 2-37")
    if tup == "T1_4X":
        return 16, None, ""
    return None, None, ""


# ====================================================================================== main
def main():
    global VERBOSE
    ap = argparse.ArgumentParser()
    ap.add_argument("--garbage", default=None, help="folder that holds asmjit\\ and emulator\\ (auto-detected)")
    ap.add_argument("--out", default=os.path.normpath(os.path.join(HERE, "..", "..", "data")))
    ap.add_argument("--sdm-layout", default=None, help="pdftotext -layout text of 325383-092 (cross-check only)")
    ap.add_argument("-q", action="store_true")
    a = ap.parse_args()
    VERBOSE = not a.q
    g = a.garbage or find_garbage()
    if not g:
        sys.exit("cannot find the garbage folder (asmjit\\db\\isa_x86.json); pass --garbage")
    docs_dir = os.path.join(g, "emulator", "intel_docs_text")
    aj_path = os.path.join(g, "asmjit", "db", "isa_x86.json")
    xed_dir = os.path.join(g, "emulator", "Intel docs", "xed-main", "datafiles")
    manual_path = os.path.join(a.out, "isa_manual_forms.tsv")
    log(f"[cfg] garbage={g}")
    log(f"[cfg] intel docs text={docs_dir}")
    log(f"[cfg] asmjit={aj_path}")
    log(f"[cfg] xed={xed_dir}")
    log(f"[cfg] manual forms={manual_path}")
    log(f"[cfg] out={a.out}")
    stats = {}

    # ------------------------------------------------------------------ 1. Intel documents
    intel_docs = [("SDM-092 Vol.2A", "sdm_vol2a_092.md", True), ("SDM-092 Vol.2B", "sdm_vol2b_092.md", True),
                  ("SDM-092 Vol.2C", "sdm_vol2c_092.md", True), ("SDM-092 Vol.2D", "sdm_vol2d_092.md", True),
                  ("AVX10.2 spec 361050-007", "avx10_2_spec_361050_007.md", False),
                  ("ISE 319433-062", "ise_319433_062.md", False)]
    sections = []
    for name, fn, is_sdm in intel_docs:
        p = os.path.join(docs_dir, fn)
        if not os.path.isfile(p):
            log(f"[intel] MISSING {p}")
            continue
        sections += parse_intel_doc(p, name, stats, is_sdm)

    intel = []          # per-VL EVEX records
    vex_rows = []
    bad_rows = []
    parse_notes = []
    no_openc = collections.Counter()
    for sec in sections:
        for row in sec.rows:
            if row.get("bad"):
                bad_rows.append(row)
                continue
            if row["kind"] == "VEX":
                mn = row["instr"].split(" ")[0].lower()
                vex_rows.append({"mn": mn, "map": row["map"], "pp": row["pp"] or "NP", "opc": row["opc"],
                                 "digit": row["digit"], "W": row["W"] or "WIG", "vl": row["vl"], "doc": row["doc"]})
                continue
            an = analyze_sdm_operands(row["instr"])
            tup_ops = sec.openc.get(row["op_en"])
            if tup_ops is None:
                no_openc[(row["doc"], sec.heading, row["op_en"])] += 1
                tup, opnd = "?", ""
            else:
                tup, opnd = tup_ops
            fams, vlfeat, avx10 = norm_cpuid(row["cpuid"])
            fam = family_of(fams, avx10)
            ex = set()
            for key in (an["mn"].upper(),):
                ex |= sec.exc.get(key, set())
            if not ex:
                ex = set(sec.exc.get("*", set()))
            pp = row["pp"]
            notes = list(row["notes"])
            if pp is None:
                pp = "NP"
                notes.append("pp omitted in the opcode column (NP)")
            W = row["W"]
            if W is None:
                W = "W?"
                notes.append("W omitted in the opcode column")
            mod = row["mod"] or an["mod"]
            for ty in an.get("typos", []):
                notes.append("SDM operand typo " + ty)
            if row["mod"] and an["mod"] != row["mod"] and an["mod"] != "any":
                pass
            ref = f"{row['doc']} p{row['page']}" + (f" ({row['printed']})" if row["printed"] else "") + \
                  f" {sec.heading}"
            rec = {"src": "intel", "doc": row["doc"], "mn": an["mn"], "map": row["map"] or "?", "pp": pp,
                   "opc": row["opc"], "digit": row["digit"], "mod": mod, "vsib": an["vsib"] or row["vsib"], "W": W,
                   "vl": row["vl"], "imm": row["imm"] or bool(re.search(r"\bimm8\b", row["instr"])),
                   "openc": openc_letters(opnd).replace("i", ""), "operands": row["instr"], "tuple": tup,
                   "bcst": an["bcst"], "mask": an["mask"], "rc": an["rc"], "family": fam, "vlfeat": vlfeat,
                   "avx10": ",".join(avx10), "cpuid": clean_cpuid_text(row["cpuid"]), "exc": "/".join(sorted(ex)),
                   "memsize": an["memsize"], "ref": ref, "notes": notes, "op_en": row["op_en"],
                   "opnd_roles": opnd, "opcode_text": row["opcode"]}
            if rec["vl"] is None:
                rec["notes"].append("no vector length in opcode column")
                rec["vl"] = "LIG"
            if row["notes"]:
                parse_notes.append((ref, row["opcode"], "; ".join(row["notes"])))
            intel.append(rec)
    log(f"[intel] EVEX per-VL rows: {len(intel)}; VEX rows: {len(vex_rows)}; unparsed rows: {len(bad_rows)}; "
        f"rows whose Op/En has no tuple entry: {sum(no_openc.values())}")
    for b in bad_rows:
        if b["kind"] == "EVEX":
            log(f"[intel]   unparsed EVEX row {b['doc']} p{b['page']}: {b['opcode']} | {b['block']}")
    for (doc, hd, oe), c in sorted(no_openc.items()):
        log(f"[intel]   no tuple for Op/En '{oe}' ({c} rows) in {doc} :: {hd}")
    if stats.get("unknown tuple words"):
        log(f"[intel]   unknown tuple words: {dict(stats['unknown tuple words'])}")

    # dedupe Intel records across docs (SDM > AVX10.2 spec > ISE)
    doc_rank = {n: i for i, (n, _, _) in enumerate(intel_docs)}
    doc_rank_key = lambda r: min(v for k, v in doc_rank.items() if r["doc"] == k)
    by_key = collections.OrderedDict()
    dup_drop = 0
    for r in sorted(intel, key=doc_rank_key):
        k = (r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["mod"], r["W"], r["vl"])
        if k in by_key:
            if by_key[k]["doc"] != r["doc"]:
                dup_drop += 1
                continue
            # same doc, same key: keep the first (duplicate printing, e.g. a row repeated on a page break)
            dup_drop += 1
            continue
        by_key[k] = r
    intel = list(by_key.values())
    log(f"[intel] after de-duplication across documents: {len(intel)} per-VL records ({dup_drop} duplicates dropped)")

    # ------------------------------------------------------------------ sdm2.txt cross-check
    layout = a.sdm_layout
    if layout is None:
        cand = glob.glob(os.path.join(os.environ.get("TEMP", ""), "claude", "*", "*", "scratchpad", "sdm2.txt"))
        layout = cand[0] if cand else None
    layout_report = []
    if layout and os.path.isfile(layout):
        txt = open(layout, encoding="utf-8", errors="replace").read()
        lay = collections.Counter()
        # opcode-table cells start a line in the -layout text; prose mentions ('VMOVSD (EVEX.LLIG...') do not
        for m in re.finditer(r"^\s*(EVEX\.[0-9A-Z.]+?)\s+([0-9A-F]{2})\b", txt, re.M):
            lay[m.group(1).replace(".NDS", "") + " " + m.group(2)] += 1
        md = collections.Counter()
        for sec in sections:
            if not sec.doc.startswith("SDM"):
                continue
            for row in sec.rows:
                if row.get("kind") != "EVEX":
                    continue
                mm = re.match(r"^(EVEX\.[0-9A-Z.]+?)\s+([0-9A-F]{2})", row["opcode"])
                if mm:
                    md[mm.group(1).replace(".NDS", "") + " " + mm.group(2)] += 1
        only_lay = sorted(set(lay) - set(md))
        only_md = sorted(set(md) - set(lay))
        layout_report.append(f"sdm2.txt (pdftotext -layout of 325383-092): {sum(lay.values())} EVEX opcode strings, {len(lay)} distinct; "
                             f"SDM md parse: {sum(md.values())} rows, {len(md)} distinct")
        layout_report.append(f"  only in sdm2.txt: {len(only_lay)}  {only_lay[:40]}")
        layout_report.append(f"  only in md parse: {len(only_md)}  {only_md[:40]}")
        for l in layout_report:
            log("[layout] " + l)
    else:
        layout_report.append("sdm2.txt not available - layout cross-check skipped")

    # ------------------------------------------------------------------ 2. asmjit
    aj, aj_apx, aj_bad = parse_asmjit(aj_path, stats)
    # ------------------------------------------------------------------ 3. XED
    xrecs = parse_xed(xed_dir, stats)
    xidx = xed_index(xrecs)

    # ------------------------------------------------------------------ 4. match asmjit <-> Intel per VL
    ikey = collections.defaultdict(list)
    for r in intel:
        ikey[(r["mn"], r["opc"], r["digit"])].append(r)
    ifull = {(r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["mod"], r["W"], r["vl"]): r for r in intel}
    rc_notation = [0]
    conflicts = []          # (kind, mn, enc, vl, asmjit, intel, xed, resolution)
    matched_intel = set()
    aj_only = []

    def enc_of(r):
        return f"EVEX.{r['pp']}.{r['map']}.{r['W']} {r['opc']} /{r['digit']}" + \
               (" ib" if r["imm"] else "") + f" mod={r['mod']}"

    def xv(r):
        v = xed_view(xidx, r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["vl"], r["W"])
        if not v:
            return "-"
        return f"tt={v['tuple']} W={v['W']} b={v['bcst']} mask={v['mask']} rc={v['rc'] or '-'} exc={v['exc'] or '-'}"

    def pick(r, cands):
        c1 = [c for c in cands if c["vl"] == r["vl"]]
        c2 = [c for c in c1 if c["map"] == r["map"] and c["pp"] == r["pp"]] or c1
        c3 = [c for c in c2 if c["mod"] == r["mod"]] or [c for c in c2 if "any" in (c["mod"], r["mod"])] or c2
        c4 = [c for c in c3 if c["W"] == r["W"]] or c3
        return c4

    ienc = collections.defaultdict(list)
    for c in intel:
        ienc[(c["map"], c["pp"], c["opc"], c["digit"])].append(c)
    pairs = []
    unmatched = []
    for r in aj:
        c4 = pick(r, ikey.get((r["mn"], r["opc"], r["digit"]), []))
        if not c4:
            unmatched.append(r)
            continue
        pairs.append((r, c4[0]))
    renamed = collections.OrderedDict()
    for r in unmatched:
        # second pass: the same encoding under another name (asmjit still uses the pre-revision AVX10.2 names such
        # as VADDNEPBF16 = VADDBF16, VCVTNE2PH2BF8 = VCVT2PH2BF8; the ISE renamed TCVTROWPS2PBF16H -> TCVTROWPS2BF16H)
        cands = [c for c in ienc.get((r["map"], r["pp"], r["opc"], r["digit"]), [])
                 if c["vl"] == r["vl"] and (c["W"] == r["W"] or "WIG" in (c["W"], r["W"]))
                 and (c["mod"] == r["mod"] or "any" in (c["mod"], r["mod"]))]
        if cands and len({c["mn"] for c in cands}) == 1:
            cc = [c for c in cands if c["mn"] == old_avx102_name(r["mn"])] or \
                [c for c in cands if c["mod"] == r["mod"]] or cands
            renamed.setdefault((r["mn"], cc[0]["mn"]), []).append(r["vl"])
            pairs.append((r, cc[0]))
            continue
        # third pass: the pre-revision AVX10.2 name maps to a current one, but asmjit's map/pp differ
        cc = pick(r, ikey.get((old_avx102_name(r["mn"]), r["opc"], r["digit"]), []))
        if old_avx102_name(r["mn"]) != r["mn"] and cc:
            renamed.setdefault((r["mn"], cc[0]["mn"]), []).append(r["vl"])
            pairs.append((r, cc[0]))
        else:
            aj_only.append(r)
    for (old, new), vls in renamed.items():
        for v in vls:
            conflicts.append(("mnemonic", new, "-", v, old, new, "-", "Intel (current) mnemonic used"))
    log(f"[match] {len(renamed)} asmjit mnemonics matched by encoding under a different (current Intel) name")
    for r, c in pairs:
        matched_intel.add(id(c))
        r["_intel"] = c
        e = enc_of(c)
        for fld in ("map", "pp", "W", "mod", "tuple", "bcst", "mask", "rc", "openc", "imm"):
            av, iv = r[fld], c[fld]
            if fld == "tuple":
                if iv == "?" or av == "?":
                    if av != iv:
                        conflicts.append(("tuple-missing", r["mn"], e, r["vl"], av, iv, xv(c),
                                          "Intel tuple used" if iv != "?" else "asmjit tuple used (Intel Op/En table not parsed)"))
                    continue
                if c["mod"] == "reg" and (av in ("N/A",) or iv == "N/A"):
                    continue
                if av == "N/A" and iv != "N/A":
                    pass
            if fld == "openc":
                if not iv or not av:
                    continue
                # asmjit writes 'rm'/'rvm' etc.; Intel letters come from the Op/En operand cells
                if av == iv:
                    continue
            if fld == "rc" and av and not iv and r["vl"] in (128, 256):
                # asmjit folds 128/256/512 into one 'xyz' row and writes {er}/{sae} on it; the SDM puts the
                # decorator only on the 512-bit row (static rounding / SAE need L'L = 512 or scalar, Table 2-38)
                c512 = ifull.get((c["mn"], c["map"], c["pp"], c["opc"], c["digit"], c["mod"], c["W"], 512))
                if c512 is not None and c512["rc"] == av:
                    rc_notation[0] += 1
                    continue
            if fld == "W" and iv == "W?" and av in ("W0", "W1", "WIG"):
                # the SDM row has no W at all (e.g. 'EVEX.512.66.0F DA /r' VPMINUB): nothing to win with
                conflicts.append(("W-omitted-in-SDM", r["mn"], e, r["vl"], av, iv, xv(c),
                                  f"SDM opcode column has no W; asmjit {av} used (XED agrees if shown)"))
                c["W"] = av
                c["notes"].append(f"W not printed in the SDM opcode column; {av} from asmjit/XED")
                continue
            if av != iv:
                conflicts.append((fld, r["mn"], e, r["vl"], av, iv, xv(c), "Intel value used"))
        fa, fi = r["family"], c["family"]
        if fa != fi and not (fa == "AVX512F" and fi.startswith("AVX512F")):
            conflicts.append(("family", r["mn"], e, r["vl"], fa, fi, xv(c), "Intel CPUID column used"))
        if r["vl"] != "LIG" and r["vl"] != 512 and r["vlfeat"] != c["vlfeat"] and not c["avx10"].startswith("AVX10.2"):
            conflicts.append(("AVX512VL", r["mn"], e, r["vl"], r["vlfeat"], c["vlfeat"], xv(c), "Intel CPUID column used"))
        for nt in r["notes"]:
            if "missing in asmjit" in nt:
                conflicts.append(("asmjit-encoding-defect", r["mn"], e, r["vl"], r["aj_enc"] + f"  [{nt}]",
                                  re.sub(r"^EVEX\.(128|256|512)\.", "EVEX.<L>.", c["opcode_text"]), "-",
                                  "Intel encoding used"))
    intel_only = [r for r in intel if id(r) not in matched_intel]
    log(f"[match] asmjit per-VL records matched to Intel: {len(aj) - len(aj_only)}; asmjit-only: {len(aj_only)}; "
        f"Intel-only: {len(intel_only)}; raw conflicts: {len(conflicts)}")

    # tuple fill-in for Intel records whose Op/En table did not parse
    for r in aj:
        c = r.get("_intel")
        if c and c["tuple"] == "?" and r["tuple"] not in ("?",):
            c["tuple"] = r["tuple"]
            c["notes"].append("tuple from asmjit (Intel Op/En table not parsed)")
    for r in intel:
        if r["W"] == "W?":
            v = xed_view(xidx, r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["vl"], "W?")
            if v and "/" not in v["W"]:
                conflicts.append(("W-omitted-in-SDM", r["mn"], enc_of(r), r["vl"], "W?", "W?", xv(r),
                                  f"SDM and asmjit print no W; XED {v['W']} used"))
                r["W"] = v["W"]
                r["notes"].append(f"W not printed in the SDM opcode column (nor by asmjit); {v['W']} from XED")
        if r["tuple"] == "?":
            v = xed_view(xidx, r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["vl"], r["W"])
            if v and v["tuple"] and "/" not in v["tuple"]:
                r["tuple"] = v["tuple"]
                r["notes"].append("tuple from XED (Intel Op/En table not parsed, asmjit has none)")
        if not r["exc"]:
            v = xed_view(xidx, r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["vl"], r["W"])
            if v and v["exc"]:
                r["exc"] = v["exc"] + "(xed)"

    # ------------------------------------------------------------------ 5. XED-only fill-in
    have = {(r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["vl"]) for r in intel}
    have |= {(r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["vl"]) for r in aj_only}
    have_mn = {r["mn"] for r in intel} | {r["mn"] for r in aj}
    have_enc = {(r["map"], r["pp"], r["opc"], r["digit"], r["vl"], r["W"]) for r in intel + aj_only}
    xed_fill = collections.OrderedDict()
    for x in xrecs:
        k = (x["mn"], x["map"], x["pp"], x["opc"], x["digit"], x["vl"])
        if k in have or x["mn"] in have_mn or \
                any((x["map"], x["pp"], x["opc"], x["digit"], x["vl"], w) in have_enc
                    for w in ((x["W"], "WIG") if x["W"] != "WIG" else ("W0", "W1", "WIG"))):
            continue
        kk = k + (x["W"],)
        e = xed_fill.get(kk)
        if e is None:
            e = {"src": "xed", "mn": x["mn"], "map": x["map"], "pp": x["pp"], "opc": x["opc"], "digit": x["digit"],
                 "mod": x["mod"], "vsib": x["vsib"], "W": x["W"], "vl": x["vl"], "imm": x["imm"], "openc": "",
                 "operands": x["iform"], "tuple": "N/A", "bcst": 0, "mask": x["mask"], "rc": "",
                 "family": re.sub(r"_(128N?|256|512|SCALAR)$", "", x["isa_set"]), "vlfeat": False, "avx10": "",
                 "cpuid": x["isa_set"], "exc": x["exc"], "memsize": 0, "ref": f"XED {x['file']} {x['iform']}",
                 "notes": [], "isa_sets": set()}
            xed_fill[kk] = e
        e["isa_sets"].add(x["isa_set"])
        if x["mod"] != e["mod"]:
            e["mod"] = "any"
        if x["tuple"] not in ("N/A", "?"):
            e["tuple"] = x["tuple"]
            e["memsize"] = e["memsize"] or 0
        if x["bcst"]:
            e["bcst"] = x["bcst"]
        if x["rc"]:
            e["rc"] = x["rc"]
        if x["mask"] != "none":
            e["mask"] = x["mask"] if e["mask"] in ("none", x["mask"]) else e["mask"]
    xed_only = list(xed_fill.values())
    log(f"[xed] forms present only in XED (no Intel text, no asmjit): {len(xed_only)} per-VL records, "
        f"{len({r['mn'] for r in xed_only})} mnemonics")

    # ------------------------------------------------------------------ 6. fold per-VL records into forms
    allrecs = intel + aj_only + xed_only
    for r in allrecs:
        n0, nb, err = disp8_n(r["tuple"], r["vl"], r["W"], r["bcst"], r["memsize"])
        r["n0"], r["nb"], r["nerr"] = n0, nb, err
        # cross-check: N (b=0) must equal the size of the memory operand, except for element-granular T1S forms
        # (compress/expand: full-vector memory, N = element) and VSIB (memsize there is the index size)
        if r["tuple"] not in ("N/A", "?", "Mem128", "MOVDDUP", "T1_4X") and r["memsize"] and n0 and r["mod"] != "reg" \
                and n0 != r["memsize"] and not r["vsib"] and not (r["tuple"] == "Tuple1Scalar" and r["memsize"] > 8):
            r["nerr"] = (err + "; " if err else "") + f"N={n0} but the memory operand is {r['memsize']} bytes"
        if r["tuple"] == "Tuple1Scalar" and r["memsize"] and r["memsize"] <= 8 and r["W"] in ("W0", "W1") \
                and r["memsize"] in (4, 8) and r["memsize"] != (4 if r["W"] == "W0" else 8):
            r["notes"].append(f"T1S input size {r['memsize'] * 8} bit although EVEX.{r['W']} (W selects the GPR, "
                              f"not the memory element): N={r['memsize']}")

    def srcname(r):
        return r["doc"] if r["src"] == "intel" else r["src"]

    forms = collections.OrderedDict()
    for r in allrecs:
        k = (srcname(r), r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["mod"], r["vsib"], r["W"], r["imm"],
             r["tuple"], r["bcst"], r["mask"], r["family"], r["openc"])
        # {er}/{sae} is printed only on the 512-bit (or scalar) row: it is legal only there (Table 2-38), so it
        # does not split a form; the rc column carries the decorator of the 512/LIG member
        f = forms.get(k)
        if f is None:
            f = {"recs": [], "key": k}
            forms[k] = f
        f["recs"].append(r)
    formlist = []
    for k, f in forms.items():
        rs = sorted(f["recs"], key=lambda r: vl_key(r["vl"]))
        top = rs[-1]
        vls = [r["vl"] for r in rs]
        vlfeat = any(r["vlfeat"] for r in rs if r["vl"] in (128, 256))
        exc = "/".join(sorted({r["exc"] for r in rs if r["exc"]})) or "-"
        nper = {}
        for r in rs:
            nper[r["vl"]] = r["n0"] if r["mod"] != "reg" else None
        nb = sorted({r["nb"] for r in rs if r["nb"]})
        nerr = sorted({f"VL{r['vl']}: {r['nerr']}" for r in rs if r["nerr"]})
        notes = sorted({n for r in rs for n in r["notes"]})
        avx10 = sorted({r["avx10"] for r in rs if r["avx10"]})
        elem = "-"
        if top["bcst"]:
            elem = str(top["bcst"])
        elif top["tuple"] in ("Tuple1Scalar", "T1F") and top["memsize"]:
            elem = str(top["memsize"] * 8)
        elif top["tuple"] in ("T2", "T4", "T8") and top["memsize"]:
            elem = str(top["memsize"] * 8 // int(top["tuple"][1]))
        elif top["tuple"] in ("Full", "Half", "Quarter") and top["W"] in ("W0", "W1"):
            elem = "32" if top["W"] == "W0" else "64"
        else:
            m = re.search(r"(8|16|32|64)$", top["mn"])
            if m and re.match(r"v(movdq[au]|extract|insert|broadcast|pmov|perm|pexpand|pcompress)", top["mn"]):
                elem = m.group(1)
        formlist.append({
            "src": k[0], "mn": top["mn"], "map": top["map"], "pp": top["pp"], "opc": top["opc"], "digit": top["digit"],
            "mod": top["mod"], "vsib": top["vsib"], "W": top["W"], "vls": vls, "imm": top["imm"],
            "openc": top["openc"], "operands": top["operands"], "opnd_roles": top.get("opnd_roles", ""),
            "elem": elem, "tuple": top["tuple"], "bcst": top["bcst"], "mask": top["mask"],
            "rc": "/".join(sorted({r["rc"] for r in rs if r["rc"]})),
            "family": top["family"], "vlfeat": vlfeat, "avx10": ",".join(avx10), "cpuid": top["cpuid"], "exc": exc,
            "nper": nper, "nb": nb, "nerr": nerr, "ref": top["ref"], "notes": notes,
            "rows": len(rs)})
    formlist.sort(key=lambda f: ({"0F": 0, "0F38": 1, "0F3A": 2, "MAP5": 3, "MAP6": 4}.get(f["map"], 9), f["opc"],
                                 f["pp"], f["digit"], f["W"], f["mod"], f["mn"]))
    log(f"[forms] {len(formlist)} forms from {len(allrecs)} per-VL records "
        f"(intel {len(intel)}, asmjit-only {len(aj_only)}, xed-only {len(xed_only)})")

    # ------------------------------------------------------------------ 7. manual-form coverage
    man = []
    for ln in open(manual_path, encoding="utf-8"):
        if ln.startswith("#") or not ln.strip():
            continue
        p = ln.rstrip("\n").split("\t")
        if len(p) >= 4 and p[1] == "evex":
            man.append((p[0], int(p[2]), p[3], p[4] if len(p) > 4 else ""))
    man_apx = [m for m in man if m[2].startswith("APX")]
    man_evex = [m for m in man if not m[2].startswith("APX")]
    log(f"[manual] EVEX rows: {len(man)} ({len(man_apx)} APX EVEX-promoted, {len(man_evex)} vector)")
    by_mn = collections.defaultdict(list)
    for f in formlist:
        by_mn[f["mn"]].append(f)
    XED_ALIAS = {"vpextrw_c5": "vpextrw"}
    CMP_ALIAS = re.compile(r"^(vcmp)(?:eq|lt|le|unord|neq|nlt|nle|ord|nge|ngt|false|ge|gt|true)(?:_[a-z]{1,2})?"
                           r"(ps|pd|ph|ss|sd|sh)$")
    missing = []
    covered = 0
    covered_alias = 0
    for (mn, vb, feat, flags) in man_evex:
        fs = by_mn.get(mn) or by_mn.get(XED_ALIAS.get(mn, ""), [])
        if not fs:
            ma = CMP_ALIAS.match(mn)
            if ma:      # Capstone-style compare-predicate pseudo-op = VCMPPS/PD/PH/SS/SD/SH with that imm8
                fs = by_mn.get(ma.group(1) + ma.group(2), [])
                if fs:
                    covered_alias += 1
        ok = False
        for f in fs:
            if vb == 0 or vb in f["vls"] or (vb == 128 and "LIG" in f["vls"]) or \
                    (feat.endswith("SCALAR") and "LIG" in f["vls"]):
                ok = True
                break
        if ok:
            covered += 1
        else:
            why = "mnemonic absent from every source" if not fs else \
                f"mnemonic present but not at VL {vb} (have {sorted({str(v) for f in fs for v in f['vls']})})"
            missing.append((mn, vb, feat, why))
    man_mn = {m[0] for m in man_evex}
    extra = sorted({f["mn"] for f in formlist} - man_mn)
    log(f"[manual] covered {covered}/{len(man_evex)} ({covered_alias} of them VCMP predicate pseudo-ops); "
        f"missing {len(missing)}; TSV mnemonics not in manual: {len(extra)}")

    # ------------------------------------------------------------------ 8. decoder notes
    # VEX vs EVEX slot meaning
    vex_slot = collections.defaultdict(set)
    for v in vex_rows:
        if v["map"] in ("0F", "0F38", "0F3A"):
            vex_slot[(v["map"], v["pp"], v["opc"], v["digit"], v["W"])].add(v["mn"])
    evex_slot = collections.defaultdict(set)
    for f in formlist:
        evex_slot[(f["map"], f["pp"], f["opc"], f["digit"], f["W"])].add(f["mn"])

    def vex_mns(mp, pp, opc, digit, W):
        out = set()
        for (m2, p2, o2, d2, w2), s in vex_slot.items():
            if (m2, p2, o2, d2) == (mp, pp, opc, digit) and (W == "WIG" or w2 == "WIG" or w2 == W):
                out |= s
        return out

    diff_slots = []
    for (mp, pp, opc, digit, W), es in sorted(evex_slot.items()):
        if mp not in ("0F", "0F38", "0F3A"):
            continue
        vs = vex_mns(mp, pp, opc, digit, W)
        if not vs:
            continue
        if not (es & vs):
            diff_slots.append((mp, pp, opc, digit, W, sorted(es), sorted(vs)))
    vex_only_slots = sorted({(k[0], k[1], k[2], k[3]) for k in vex_slot} -
                            {(k[0], k[1], k[2], k[3]) for k in evex_slot})
    # W-split and pp-split
    by_slot_w = collections.defaultdict(lambda: collections.defaultdict(set))
    by_slot_pp = collections.defaultdict(lambda: collections.defaultdict(set))
    by_slot_mod = collections.defaultdict(lambda: collections.defaultdict(set))
    for f in formlist:
        by_slot_w[(f["map"], f["pp"], f["opc"], f["digit"])][f["W"]].add(f["mn"])
        by_slot_pp[(f["map"], f["opc"], f["digit"])][f["pp"]].add(f["mn"])
        by_slot_mod[(f["map"], f["pp"], f["opc"], f["digit"], f["W"])][f["mod"]].add(f["mn"])
    w_split = [(k, {w: sorted(s) for w, s in v.items()}) for k, v in sorted(by_slot_w.items())
               if len(v) > 1 and len({frozenset(s) for s in v.values()}) > 1]
    pp_split = [(k, {p: sorted(s) for p, s in v.items()}) for k, v in sorted(by_slot_pp.items())
                if len(v) > 1 and len({frozenset(s) for s in v.values()}) > 1]
    mod_split = [(k, {m: sorted(s) for m, s in v.items()}) for k, v in sorted(by_slot_mod.items())
                 if len(v) > 1 and len({frozenset(s) for s in v.values()}) > 1]
    vl_restricted = [f for f in formlist if f["vls"] != ["LIG"] and set(f["vls"]) != set(VL_ALL)]

    # ------------------------------------------------------------------ 9. write TSV
    os.makedirs(a.out, exist_ok=True)
    tsv = os.path.join(a.out, "evex_forms.tsv")
    cols = ["mnemonic", "map", "pp", "opcode", "modrm", "mod", "W", "vl", "imm", "op_enc", "operands",
            "operand_roles", "elem_bits", "tuple", "bcst", "masking", "rc_sae", "feature", "vl_feature", "avx10",
            "cpuid_text", "exc_class", "N128", "N256", "N512", "N_bcst", "disp8_check", "source", "sdm_ref", "notes"]
    MASK_TXT = {"mz": "merge+zero", "m": "merge", "kreq": "k-required", "kdest": "k-dest", "none": "none"}
    with open(tsv, "w", encoding="utf-8", newline="\n") as fo:
        fo.write("# EVEX instruction forms (generated by Emulator\\tools\\isa\\gen_evex_tables.py - do not edit).\n")
        fo.write("# One row per EVEX form; VLs that share every other attribute are folded into one row.\n")
        fo.write("# map: 0F/0F38/0F3A/MAP5/MAP6. modrm: /r or /digit (vsib = VSIB memory). mod: any|reg (mod=11 only)|"
                 "mem (mod!=11 only). W: W0|W1|WIG. vl: 128,256,512 or LIG.\n")
        fo.write("# rc_sae: er = static rounding, sae = suppress-all-exceptions; both only reg-reg with VL512 (L'L = "
                 "RC) or scalar (SDM 2.7.8/2.7.9, Table 2-38); EVEX.b on a 128/256 reg-reg form is #UD (AVX10.2 "
                 "rev 5+ removed 256-bit rounding).\n")
        fo.write("# masking: merge+zero ({k1}{z}), merge ({k1}, z #UD), k-required (aaa!=0, gathers/scatters), "
                 "k-dest (k1 {k2}: AND with k2, z #UD), none (aaa must be 0).\n")
        fo.write("# N128/N256/N512: disp8*N with EVEX.b=0 (SDM Tables 2-36/2-37); N_bcst: N with EVEX.b=1 (memory "
                 "broadcast). 'NA' = vector length not allowed.\n")
        fo.write("# source: the Intel document the row comes from (SDM wins over AVX10.2 spec over ISE), 'asmjit' "
                 "(no Intel text) or 'xed' (neither Intel text nor asmjit).\n")
        fo.write("\t".join(cols) + "\n")
        for f in formlist:
            nv = []
            for v in (128, 256, 512):
                if "LIG" in f["vls"]:
                    nv.append(str(f["nper"].get("LIG") or "-") if v == 128 else "-")
                elif v in f["vls"]:
                    nv.append(str(f["nper"].get(v) or "-"))
                else:
                    nv.append("NA")
            fo.write("\t".join([
                f["mn"], f["map"], f["pp"], f["opc"], ("vsib" if f["vsib"] else "/" + f["digit"]), f["mod"],
                f["W"].replace("W0", "0").replace("W1", "1").replace("WIG", "ig"), vl_str(f["vls"]),
                "ib" if f["imm"] else "-", f["openc"] or "-", f["operands"],
                f["opnd_roles"] or "-", f["elem"], f["tuple"], (f"b{f['bcst']}" if f["bcst"] else "-"),
                MASK_TXT[f["mask"]], f["rc"] or "-", f["family"], "AVX512VL" if f["vlfeat"] else "-",
                f["avx10"] or "-", f["cpuid"] or "-", f["exc"], nv[0], nv[1], nv[2],
                ",".join(str(x) for x in f["nb"]) or "-", "; ".join(f["nerr"]) or "ok",
                f["src"], f["ref"], "; ".join(f["notes"]) or "-"]) + "\n")
    log(f"[out] {tsv}: {len(formlist)} rows")

    # ------------------------------------------------------------------ 10. conflicts file
    agg = collections.OrderedDict()
    for (kind, mn, enc, vl, av, iv, xvw, res) in conflicts:
        k = (kind, mn, enc, str(av), str(iv), res)
        e = agg.setdefault(k, {"vls": [], "xed": set()})
        e["vls"].append(vl)
        e["xed"].add(xvw)
    cpath = os.path.join(a.out, "evex_forms_conflicts.txt")
    kinds = collections.Counter(k[0] for k in agg)
    with open(cpath, "w", encoding="utf-8", newline="\n") as fo:
        fo.write("# asmjit isa_x86.json vs Intel documents (SDM 325383-092 first, then AVX10.2 spec 361050-007, ISE "
                 "319433-062): every disagreement.\n")
        fo.write("# The Intel document wins; the TSV carries the Intel value. XED's view is shown as a third opinion.\n")
        fo.write("# kind | mnemonic | Intel encoding | VLs | asmjit | Intel | XED | resolution\n#\n")
        fo.write("# counts per kind: " + ", ".join(f"{k}={v}" for k, v in kinds.most_common()) + "\n")
        fo.write(f"# total distinct conflicts: {len(agg)} (from {len(conflicts)} per-VL disagreements)\n\n")
        for k in sorted(agg, key=lambda k: (k[0], k[1], k[2])):
            e = agg[k]
            fo.write(f"{k[0]} | {k[1]} | {k[2]} | {vl_str(e['vls'])} | asmjit={k[3]} | intel={k[4]} | "
                     f"xed: {'; '.join(sorted(e['xed']))} | {k[5]}\n")
        fo.write("\n# ---- asmjit EVEX forms with no Intel text (kept from asmjit; XED view shown)\n")
        seen = set()
        for r in aj_only:
            kk = (r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["W"], r["mod"])
            if kk in seen:
                continue
            seen.add(kk)
            vls = [x["vl"] for x in aj_only if (x["mn"], x["map"], x["pp"], x["opc"], x["digit"], x["W"], x["mod"]) == kk]
            fo.write(f"asmjit-only | {r['mn']} | {enc_of(r)} | {vl_str(vls)} | {r['aj_inst']} | {r['aj_enc']} | "
                     f"xed: {xv(r)}\n")
        fo.write("\n# ---- Intel EVEX forms that asmjit does not have (TSV source = Intel)\n")
        seen = set()
        for r in intel_only:
            kk = (r["mn"], r["map"], r["pp"], r["opc"], r["digit"], r["W"], r["mod"])
            if kk in seen:
                continue
            seen.add(kk)
            vls = [x["vl"] for x in intel_only if (x["mn"], x["map"], x["pp"], x["opc"], x["digit"], x["W"], x["mod"]) == kk]
            fo.write(f"intel-only | {r['mn']} | {enc_of(r)} | {vl_str(vls)} | {r['operands']} | {r['ref']}\n")
        fo.write("\n# ---- Intel text normalisations (typos in the opcode column, resolved as noted)\n")
        for ref, opc, nt in parse_notes:
            fo.write(f"intel-text | {opc} | {nt} | {ref}\n")
        fo.write("\n# ---- asmjit rows that could not be parsed\n")
        for b in aj_bad:
            fo.write(f"asmjit-unparsed | {b[0]} | {b[1]} | {b[2]}\n")
    log(f"[out] {cpath}: {len(agg)} distinct conflicts ({len(conflicts)} per-VL)")

    # ------------------------------------------------------------------ 11. APX list
    apath = os.path.join(a.out, "evex_forms_apx.txt")
    with open(apath, "w", encoding="utf-8", newline="\n") as fo:
        fo.write("# APX 'EVEX-promoted' legacy / VEX forms (MAP4 and promoted BMI/AMX/KMOV/CMPCCXADD/...), excluded from "
                 "evex_forms.tsv.\n# Section 1: isa_manual_forms.tsv EVEX rows with an APX_F* feature. Section 2: asmjit "
                 "'apx' encodings.\n\n")
        fo.write(f"## manual rows ({len(man_apx)})\n")
        for m in man_apx:
            fo.write(f"{m[0]}\t{m[1]}\t{m[2]}\n")
        fo.write(f"\n## asmjit apx encodings ({len(aj_apx)})\n")
        for x in aj_apx:
            fo.write(f"{x['inst']}\t{x['enc']}\t{x['ext']}\n")
    log(f"[out] {apath}")

    # ------------------------------------------------------------------ 12. draft .inc
    ipath = os.path.join(a.out, "evex_tables_draft.inc")
    TT_C = {"Full": "FULL", "Half": "HALF", "Quarter": "QUARTER", "FullMem": "FULL_MEM", "HalfMem": "HALF_MEM",
            "QuarterMem": "QUARTER_MEM", "EighthMem": "EIGHTH_MEM", "Tuple1Scalar": "T1S", "T1F": "T1F", "T2": "T2",
            "T4": "T4", "T8": "T8", "T1_4X": "T1_4X", "Mem128": "MEM128", "MOVDDUP": "MOVDDUP", "N/A": "NONE",
            "?": "UNKNOWN"}
    with open(ipath, "w", encoding="utf-8", newline="\n") as fo:
        fo.write("""/*
 * evex_tables_draft.inc - DRAFT EVEX form table (generated by Emulator\\tools\\isa\\gen_evex_tables.py;
 * not compiled anywhere yet). One X86_EVEX_ENTRY per form of evex_forms.tsv, grouped by map / opcode / pp.
 *
 * X86_EVEX_ENTRY(map, opcode, pp, w, modrm, mod, vl, tuple, bcst, mask, rc, imm, openc, feature, vl_feature,
 *                exc, n128, n256, n512, nbcst, mnemonic, operands)
 *   map        X86_EVEX_MAP_0F | _0F38 | _0F3A | _MAP5 | _MAP6          (EVEX.mmm)
 *   opcode     opcode byte
 *   pp         X86_EVEX_PP_NP | _66 | _F3 | _F2                        (EVEX.pp)
 *   w          X86_EVEX_W0 | X86_EVEX_W1 | X86_EVEX_WIG                (EVEX.W; WIG = ignored)
 *   modrm      X86_EVEX_RM_R (/r) | X86_EVEX_RM_0.._7 (/digit in ModRM.reg) | X86_EVEX_RM_VSIB (/r, VSIB memory)
 *   mod        X86_EVEX_MOD_ANY | _REG (mod=11 only) | _MEM (mod!=11 only)
 *   vl         bit mask: X86_EVEX_VL128 | X86_EVEX_VL256 | X86_EVEX_VL512, or X86_EVEX_LIG (L'L ignored, scalar)
 *   tuple      X86_EVEX_TT_<tuple> (SDM Tables 2-36/2-37; NONE = register-only form)
 *   bcst       broadcast element bits when EVEX.b=1 on a memory operand (0 = EVEX.b must be 0 for memory forms)
 *   mask       X86_EVEX_MASK_MZ ({k1}{z}) | _M ({k1}, z #UD) | _KREQ (aaa!=0 required) | _KDEST (k1 {k2}, z #UD)
 *              | _NONE (aaa must be 0)
 *   rc         X86_EVEX_RC_NONE | _ER (static rounding, reg-reg) | _SAE (suppress all exceptions, reg-reg)
 *   imm        1 if an imm8 follows
 *   openc      operand order string: r = ModRM.reg, v = EVEX.vvvv, m = ModRM.r/m (Op/En table; "" if unknown)
 *   feature    X86_FEAT_<CPUID feature> (family; AVX512F if nothing else), vl_feature 1 = AVX512VL for VL<512
 *   exc        EVEX exception class string from the SDM page ("" if not parseable)
 *   n128..n512 disp8*N with EVEX.b=0 (0 = VL not allowed / no memory operand; LIG forms repeat the scalar N
 *              in all three); nbcst = N with EVEX.b=1
 *   mnemonic / operands  for diagnostics only
 */
""")
        cur = None
        for f in formlist:
            if f["map"] not in ("0F", "0F38", "0F3A", "MAP5", "MAP6"):
                continue
            g2 = (f["map"], f["opc"], f["pp"])
            if g2 != cur:
                fo.write(f"\n/* ---- map {f['map']} opcode 0x{f['opc']} pp {f['pp']} ---- */\n")
                cur = g2
            vlm = "X86_EVEX_LIG" if "LIG" in f["vls"] else \
                "|".join(f"X86_EVEX_VL{v}" for v in sorted(f["vls"], key=vl_key))
            nv = [str(f["nper"].get(v) or 0) if (v in f["vls"]) else "0" for v in VL_ALL]
            if "LIG" in f["vls"]:
                nv = [str(f["nper"].get("LIG") or 0)] * 3
            feat = re.sub(r"[^A-Za-z0-9]+", "_", f["family"]).strip("_")
            modrm = "X86_EVEX_RM_VSIB" if f["vsib"] else ("X86_EVEX_RM_R" if f["digit"] == "r" else f"X86_EVEX_RM_{f['digit']}")
            fo.write(f"X86_EVEX_ENTRY(X86_EVEX_MAP_{f['map']}, 0x{f['opc']}, X86_EVEX_PP_{f['pp']}, "
                     f"X86_EVEX_{f['W'].replace('?', 'UNK')}, {modrm}, X86_EVEX_MOD_{f['mod'].upper()}, {vlm}, "
                     f"X86_EVEX_TT_{TT_C[f['tuple']]}, {f['bcst']}, X86_EVEX_MASK_{f['mask'].upper()}, "
                     f"X86_EVEX_RC_{(f['rc'] or 'none').upper()}, {1 if f['imm'] else 0}, \"{f['openc']}\", "
                     f"X86_FEAT_{feat}, {1 if f['vlfeat'] else 0}, \"{f['exc'] if f['exc'] != '-' else ''}\", "
                     f"{nv[0]}, {nv[1]}, {nv[2]}, {f['nb'][0] if f['nb'] else 0}, \"{f['mn']}\", "
                     f"\"{f['operands'].replace(chr(34), '')}\")\n")
    log(f"[out] {ipath}")

    # ------------------------------------------------------------------ 13. summary
    spath = os.path.join(a.out, "evex_forms_summary.txt")
    S = []
    S.append("EVEX form table summary (gen_evex_tables.py)")
    S.append("=" * 72)
    S.append(f"Intel text: SDM 325383-092 Vol.2A-D, AVX10.2 spec 361050-007, ISE 319433-062 (PyMuPDF text)")
    S.append(f"asmjit: {os.path.relpath(aj_path, g)}   XED: {os.path.relpath(xed_dir, g)}   (relative to the 'garbage' folder)")
    S.append(r"Intel text: emulator\intel_docs_text\{sdm_vol2a..d_092, avx10_2_spec_361050_007, ise_319433_062}.md")
    S.append("")
    S.append(f"forms (TSV rows): {len(formlist)}   per-VL records: {len(allrecs)}")
    S.append(f"  from Intel text: {sum(1 for f in formlist if f['src'] not in ('asmjit', 'xed'))} forms "
             f"({len(intel)} per-VL)")
    for name, _, _ in intel_docs:
        S.append(f"    {name}: {sum(1 for f in formlist if f['src'] == name)} forms")
    S.append(f"  asmjit only: {sum(1 for f in formlist if f['src'] == 'asmjit')} forms; "
             f"XED only: {sum(1 for f in formlist if f['src'] == 'xed')} forms")
    S.append(f"  Intel opcode rows that did not parse: {sum(1 for b in bad_rows if b['kind'] == 'EVEX')} EVEX")
    S.append(f"  Intel rows whose Op/En letter had no tuple entry: {sum(no_openc.values())} "
             f"(tuple then taken from asmjit, else XED)")
    S.append("")
    S.append("Layout cross-check (sdm2.txt):")
    S += ["  " + l for l in layout_report]
    S.append("")
    S.append("Per family (forms / per-VL records):")
    fam_c = collections.Counter(f["family"] for f in formlist)
    fam_r = collections.Counter()
    for f in formlist:
        fam_r[f["family"]] += len(f["vls"])
    for k, v in fam_c.most_common():
        S.append(f"  {k:28s} {v:5d} {fam_r[k]:6d}")
    S.append("")
    S.append("Per CPUID feature combination (forms): family [+AVX512VL for 128/256] [| AVX10.x alternative]")
    feat_c = collections.Counter(f"{f['family']}{'+VL' if f['vlfeat'] else ''}{(' | ' + f['avx10']) if f['avx10'] else ''}"
                                 for f in formlist)
    for k, v in feat_c.most_common():
        S.append(f"  {k:44s} {v}")
    S.append("")
    S.append("Per map:")
    for k, v in collections.Counter(f["map"] for f in formlist).most_common():
        S.append(f"  {k:6s} {v}")
    S.append("")
    S.append("Per tuple type (forms):")
    for k, v in collections.Counter(f["tuple"] for f in formlist).most_common():
        S.append(f"  {k:14s} {v}")
    S.append("")
    S.append("Masking: " + ", ".join(f"{MASK_TXT[k]}={v}" for k, v in
                                     collections.Counter(f["mask"] for f in formlist).most_common()))
    S.append("Rounding: " + ", ".join(f"{k or 'none'}={v}" for k, v in
                                      collections.Counter(f["rc"] for f in formlist).most_common()))
    S.append("Broadcast: " + ", ".join(f"{('b' + str(k)) if k else 'none'}={v}" for k, v in
                                       collections.Counter(f["bcst"] for f in formlist).most_common()))
    S.append("Exception class: " + ", ".join(f"{k}={v}" for k, v in
                                             collections.Counter(f["exc"] for f in formlist).most_common()))
    S.append("")
    S.append(f"asmjit vs Intel conflicts: {len(agg)} distinct ({len(conflicts)} per-VL); all resolved to the Intel value")
    S.append(f"  (not counted: {rc_notation[0]} per-VL cases where asmjit writes {{er}}/{{sae}} on a folded xyz row and the "
             f"SDM only on the 512-bit row - same meaning, rounding/SAE needs VL512 or scalar)")
    for k, v in kinds.most_common():
        S.append(f"  {k:24s} {v}")
    S.append(f"asmjit-only per-VL records: {len(aj_only)}; Intel-only per-VL records: {len(intel_only)}")
    S.append("")
    S.append(f"disp8*N check: {sum(1 for f in formlist if f['nerr'])} forms flagged")
    for f in formlist:
        if f["nerr"]:
            S.append(f"  {f['mn']} {f['map']}.{f['pp']}.{f['W']} {f['opc']} [{f['tuple']}] {'; '.join(f['nerr'])}")
    S.append("")
    S.append(f"isa_manual_forms.tsv coverage: {len(man)} EVEX rows = {len(man_evex)} vector + {len(man_apx)} APX "
             f"(APX listed in evex_forms_apx.txt)")
    S.append(f"  covered {covered}/{len(man_evex)} ({covered_alias} are Capstone VCMP<pred>xx pseudo-ops = VCMPxx imm8), "
             f"missing {len(missing)}")
    mfeat = collections.Counter(m[2] for m in missing)
    for k, v in mfeat.most_common():
        S.append(f"    {k:36s} {v}")
    for m in missing:
        S.append(f"  MISSING {m[0]:20s} {m[1]:4d} {m[2]:34s} {m[3]}")
    S.append(f"  TSV mnemonics not in isa_manual_forms.tsv: {len(extra)}: {' '.join(extra)}")
    S.append("")
    S.append("Decoder notes")
    S.append("-------------")
    def same_op(es, vs):
        # VPAND -> VPANDD/VPANDQ, VMOVDQA -> VMOVDQA32/64: same operation with an element-size suffix
        return all(any(e.startswith(v) and e[len(v):] in ("d", "q", "32", "64", "8", "16") for v in vs) for e in es)
    renamed_slots = [d for d in diff_slots if same_op(d[5], d[6])]
    real_slots = [d for d in diff_slots if not same_op(d[5], d[6])]
    S.append(f"EVEX slots (map 0F/0F38/0F3A, pp, opcode, /digit, W) shared with VEX but with a DIFFERENT instruction: "
             f"{len(real_slots)} (the decoder must not reuse the VEX entry; W often selects it)")
    for d in real_slots:
        S.append(f"  {d[0]}.{d[1]} {d[2]} /{d[3]} {d[4]}: EVEX {','.join(d[5])}  vs  VEX {','.join(d[6])}")
    S.append(f"EVEX slots where only the name gains an element-size suffix (same operation, EVEX adds masking/W "
             f"element width): {len(renamed_slots)}")
    for d in renamed_slots:
        S.append(f"  {d[0]}.{d[1]} {d[2]} /{d[3]} {d[4]}: EVEX {','.join(d[5])}  vs  VEX {','.join(d[6])}")
    S.append(f"VEX-only slots (no EVEX form; EVEX encoding there is #UD): {len(vex_only_slots)}")
    S.append("  " + " ".join(f"{m}.{p}:{o}{'' if d == 'r' else '/' + d}" for (m, p, o, d) in vex_only_slots))
    S.append(f"W-split opcodes (same map/pp/opcode/digit, different mnemonic for W0 and W1): {len(w_split)}")
    for k, v in w_split:
        S.append(f"  {k[0]}.{k[1]} {k[2]} /{k[3]}: " + "  ".join(f"{w}={','.join(s)}" for w, s in sorted(v.items())))
    S.append(f"pp-split opcodes (same map/opcode/digit, different mnemonic per pp): {len(pp_split)}")
    for k, v in pp_split:
        S.append(f"  {k[0]} {k[1]} /{k[2]}: " + "  ".join(f"{p}={','.join(s)}" for p, s in sorted(v.items())))
    S.append(f"mod-split opcodes (register and memory forms decode to different instructions): {len(mod_split)}")
    for k, v in mod_split:
        S.append(f"  {k[0]}.{k[1]} {k[2]} /{k[3]} {k[4]}: " + "  ".join(f"{m}={','.join(s)}" for m, s in sorted(v.items())))
    # same mnemonic, but the register and the memory form are separate SDM rows with different operands/masking
    same_mn_mod = []
    for k, v in sorted(by_slot_mod.items()):
        if "reg" in v and "mem" in v and v["reg"] & v["mem"]:
            fr = [f for f in formlist if (f["map"], f["pp"], f["opc"], f["digit"], f["W"]) == k]
            d = {f["mod"]: f for f in fr if f["mod"] in ("reg", "mem")}
            diffs = [fld for fld in ("openc", "mask", "tuple") if d["reg"][fld] != d["mem"][fld]]
            same_mn_mod.append((k, sorted(v["reg"] & v["mem"]), diffs,
                                d["reg"]["operands"], d["mem"]["operands"]))
    S.append(f"same mnemonic, separate register/memory rows (operand count, masking or tuple differ by ModRM.mod): "
             f"{len(same_mn_mod)}")
    for k, mns, diffs, ro, mo in same_mn_mod:
        S.append(f"  {k[0]}.{k[1]} {k[2]} /{k[3]} {k[4]} {','.join(mns)}: differs in {','.join(diffs) or '-'}  "
                 f"reg: {ro}  |  mem: {mo}")
    zstore = [f for f in formlist if f["openc"].startswith("m") and f["mod"] in ("any", "mem") and f["mask"] == "mz"]
    S.append(f"store-direction forms written with {{k1}}{{z}} in the SDM (EVEX.z=1 with a memory destination is #UD, "
             f"SDM 2.7.11 / Table 2-42; only the register destination may zero): {len(zstore)}")
    S.append("  " + " ".join(sorted({f["mn"] for f in zstore})))
    S.append(f"forms with a restricted VL set (not all of 128/256/512): {len(vl_restricted)}")
    for f in vl_restricted:
        S.append(f"  {f['mn']:20s} {f['map']}.{f['pp']}.{f['W']} {f['opc']} vl={vl_str(f['vls'])}")
    with open(spath, "w", encoding="utf-8", newline="\n") as fo:
        fo.write("\n".join(S) + "\n")
    log(f"[out] {spath}")
    # console summary (head)
    for l in S[:80]:
        print(l)


if __name__ == "__main__":
    main()
