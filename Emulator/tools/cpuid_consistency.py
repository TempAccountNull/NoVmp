"""CPUID self-consistency per profile, each against its own environment (plan 1.K.8 = P12; ledger U2000-U2003).

Decision 3.0.10: after milestone M2, before any sample run. The checks are rules the Intel SDM states (or two SDM fields
that describe the same quantity); each rule names its source (single-volume 093 PDFs, the AGENT_RULES citation rule; the
phrases are quoted from the project's PyMuPDF extracts intel_docs_text\\sdm_vol1_093.md / sdm_vol3a_093.md).

Views (what the rules are applied to):
  profile:<name>      a committed CPUID profile file, as captured (one logical processor; only the rows it lists):
                      --host P-core  Emulator\\data\\cpuid_i5-13600k_bare_host.txt
                      --host E-core  Emulator\\data\\cpuid_i5-13600k_bare_host_ecore.txt
                      --guest        Emulator\\data\\cpuid_i5-13600k.txt
  env:<environment>   the raw capture of every logical processor of that environment + the OS topology view taken with
                      it (cpuid_capture 1.6, outside the repository under the capture root, never committed):
                      bare_host     capture\\host\\output\\<bare-host>_20261010_133354 (the source of both --host files)
                      vmware_guest  capture\\host\\output\\<vmware-guest>_20261015_230223 (stock guest, no VmLoader)
                      The per-LP rules run on every LP; the environment rules compare the LPs with each other and with the
                      OS topology (packages, cores, caches), and the profile with the LP it was taken from.
  emu:<choice>        what the EMULATED CPU returns (emu-snippet runs CPUID through the engine, read back from guest
                      memory): --host, --guest and the built-in --max model - every leaf 0..2FH, every sub-leaf 0..31 of
                      the sub-leaf leaves, 80000000H..8000000FH and the out-of-range leaves 40000000H / 4FFFFFFFH /
                      C0000000H. The emulated view must show exactly the inconsistencies of its profile (never "fixed",
                      never new ones) and must answer the profile rows unchanged except the runtime-mutable fields
                      (SDM Table 21-1).

Captured data is NEVER edited: an inconsistency of a captured profile or capture is recorded as such in
Emulator\\data\\cpuid_consistency_expected.tsv ("inconsistent" + the reason) and reported; the suite fails when a result
differs from that file in either direction (a new inconsistency, or a recorded one that disappeared).

usage:
  python -I cpuid_consistency.py --build <tests dir>     run every view, compare with the expectations (the suite)
  python -I cpuid_consistency.py --build <dir> --list    print every result line (no comparison)
  python -I cpuid_consistency.py --selftest              negative controls on in-memory views (each must be detected)
Exit 0 = every result as expected. Values printed are topology / cache / feature fields only (no identifier).
"""
import json
import os
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "Emulator", "data")
EXPECTED = os.path.join(DATA, "cpuid_consistency_expected.tsv")
CAPTURE_ENV = "NOVMP_CAPTURE_ROOT"
PROFILES = [  # view name, file, the environment it describes, the LP it was taken from (by x2APIC ID; None = from its rows)
    ("profile:host_p", "cpuid_i5-13600k_bare_host.txt", "bare_host"),
    ("profile:host_e", "cpuid_i5-13600k_bare_host_ecore.txt", "bare_host"),
    ("profile:guest", "cpuid_i5-13600k.txt", "vmware_guest"),
]
# capture folders: located by their parent and the unique end of the name (the machine name is never printed)
CAPTURES = {
    "bare_host": ("<bare-host>", "_20261010_133354", "cpuid_GenuineIntel_06-B7-1_bare_host"),
    "vmware_guest": ("<vmware-guest>", "_20261015_230223", "cpuid_GenuineIntel_06-B7-1_vmware_guest"),
}
EMULATED = [("emu:host", "--host", "profile:host_p"), ("emu:guest", "--guest", "profile:guest"), ("emu:max", "--max", None)]
# U68 x86_cpuid_leaf_has_subleaves (+ 1EH, U721) - the leaves the emulator reads ECX for
SUBLEAF_LEAVES = {0x4, 0x7, 0xB, 0xD, 0xF, 0x10, 0x12, 0x14, 0x17, 0x18, 0x1B, 0x1D, 0x1E, 0x1F, 0x20, 0x23, 0x24, 0x29}
# SDM 253665-093 Vol1 Table 21-1 (p. 21-3) "Runtime Mutable CPUID Fields Expected to Change During Normal Operation" and
# Table 21-2 01H EDX[9] APIC ("reflects IA32_APIC_BASE[11]"): (leaf, sub or None) -> masks EAX, EBX, ECX, EDX
MUTABLE = {(0x01, None): (0, 0, 1 << 27, 1 << 9), (0x07, 0): (0, 0, 1 << 4, 0), (0x0D, 0): (0, 0xFFFFFFFF, 0, 0),
           (0x0D, 1): (0, 0xFFFFFFFF, 0, 0), (0x19, 0): (0, 1, 0, 0), (0x80000001, None): (0, 0, 0, 1 << 20)}
# the per-logical-processor fields a profile models for ONE LP (bare_host_cpuid.py PER_LP): excluded when a profile is
# compared with the capture LP it was taken from - none: the LP is chosen by its x2APIC ID, so they must match too
OUT_OF_RANGE = [0x40000000, 0x40000001, 0x4FFFFFFF, 0xC0000000]

# ------------------------------------------------------------------------------------------------- the SDM sources
SRC = {
    "range": "253665-093 Vol1 21.1.3 p. 21-2: \"If a value entered for CPUID.EAX is higher than the maximum input value for basic "
             "or extended function for that processor then the data for the highest basic information leaf is returned.\"; "
             "\"The range CPUID.40000000H to CPUID.4FFFFFFFH do not return feature information for the processor.\"",
    "domain": "253665-093 Vol1 21.1.4 p. 21-2: \"On a properly configured platform, all logical processors within a CPUID domain "
              "return a consistent output value for fields belonging to that domain.\" (Platform / Package / Logical "
              "Processor; the Domain column of each leaf table)",
    "mutable": "253665-093 Vol1 21.1.5 Table 21-1 p. 21-3 (OSXSAVE, OSPKE, 0DH.0/.1 EBX, 19H EBX[0], 80000001H EDX[20]) and "
               "Table 21-2 (01H EDX[9] APIC reflects IA32_APIC_BASE[11])",
    "vendor": "253665-093 Vol1 Table 21-4 p. 21-7: \"For Intel processors, the string is \"GenuineIntel\"\"",
    "version": "253665-093 Vol1 Table 21-6 p. 21-8 (Leaf 01H Version and Features Returned in EAX): EAX[15:14] and EAX[31:28] Reserved; p. 21-9 DisplayFamily / "
               "DisplayModel rule (\"The Extended Family ID needs to be examined only when the Family ID is 0FH.\")",
    "apicid": "253665-093 Vol1 Table 21-8 pp. 21-9 / 21-10 (01H EBX[31:24] INITIAL_APIC_ID): \"The 8-bit initial APIC ID in EBX[31:24] "
              "is replaced by the 32-bit x2APIC ID, available in Leaf 0BH and Leaf 1FH.\"; 253668-093 Vol3A 13.12.5.1 p. 13-42 "
              "(x2APIC state at reset): \"The lowest 8 bits of the x2APIC ID are the legacy local xAPIC ID\"",
    "topo": "253665-093 Vol1 CPUID.0BH (Table 21-31) pp. 21-39 / 21-40 and CPUID.1FH (Table 21-70) pp. 21-71 / 21-72: ECX[7:0] \"The input ECX sub-leaf "
            "index.\" (21-39, 21-72); \"If sub-leaf index \"N\" returns an invalid domain type in ECX[15:08] (00H), then all sub-leaves with "
            "an index greater than \"N\" also return an invalid domain type. A sub-leaf returning an invalid domain always "
            "returns 0 in EAX and EBX.\" (21-40, 21-72); EDX: 0BH \"The X2APIC ID of this logical processor.\" (21-40), 1FH \"The x2APIC "
            "ID of the current logical processor is always valid and does not vary with the sub-leaf index in ECX.\" (21-72); detection: "
            "253668-093 Vol3A 11.6 p. 11-27 (\"Check CPUID.0BH.00H:EBX is non-zero.\")",
    "leaf4": "253665-093 Vol1 CPUID.04H p. 21-21 (Table 21-14): \"The sub-leaves are enumerated until sub-leaf n returns 0 in "
             "EAX[4:0].\"; \"Sub-leaf index n+1 is invalid if sub-leaf n returns EAX[4:0] as 0.\"; EAX[31:26] \"The returned "
             "value is constant for valid initial values in ECX.\"; EAX[25:14] \"The nearest power-of-2 integer that is not "
             "smaller than (1 + EAX[25:14]) is the number of unique initial APIC IDs reserved for addressing different logical "
             "processors sharing this cache.\"",
    "leaf2": "253665-093 Vol1 CPUID.02H (Table 21-11) p. 21-15: \"The least-significant byte in register EAX (register AL) will always "
             "return 01H.\"; \"The most significant bit (bit 31) of each register indicates whether the register contains "
             "valid information (set to 0) or is reserved (set to 1).\"; Table 21-12 p. 21-19: FEH \"CPUID leaf 2 does not report "
             "TLB descriptor information; use CPUID leaf 18H to query TLB and other address translation parameters.\", FFH "
             "\"CPUID leaf 2 does not report cache descriptor information, use CPUID leaf 4 to query cache parameters.\"",
    "leaf18": "253665-093 Vol1 CPUID.18H p. 21-63: \"This leaf is valid if CPUID.18H.00H:EAX[31:0] <> 0 and MAX_LEAF >= 18H.\"; "
              "\"A sub-leaf index is also invalid if EDX[4:0] returns 0.\"",
    "leaf1a": "253665-093 Vol1 CPUID.1AH p. 21-66 (Table 21-63): \"This leaf exists on all logical processors in a hybrid "
              "package\"; CORE_TYPE 20H Intel Atom, 40H Intel Core, 10H / 30H Reserved; 07H.0 EDX[15] HYBRID p. 21-29",
    "brand": "253665-093 Vol1 21.2.1 p. 21-4 (\"returns an ASCII brand identification string\"), Figure 21-1 p. 21-5 (brand "
             "string supported when CPUID.80000000H:EAX >= 80000004H), Tables 21-91..21-93 pp. 21-89..21-91 (Platform domain)",
    "xsave": "253665-093 Vol1 CPUID.0DH (Tables 21-34..21-36, pp. 21-42..21-44): 0DH.0 EAX / EDX = the XCR0 bits supported, "
             "ECX = the maximum size of the XSAVE area for all of them; 0DH.n (n > 1) EAX size, EBX offset (0 for a supervisor "
             "component), ECX[0] set for an IA32_XSS component; 253665-093 Vol1 13.4 (the legacy region 512 bytes + the XSAVE "
             "header 64 bytes precede every extended component in the standard format)",
    "l2": "253665-093 Vol1 CPUID.80000006H p. 21-93 (Tables 21-95 / 21-96): ECX[7:0] L2 line size, ECX[15:12] associativity "
          "encoding (07H \"See CPUID leaf 4 sub-leaf 2\"), ECX[31:16] L2 size in 1K units; \"The preferred method to enumerate "
          "caching information description is to use CPUID.04H\"",
    "tscaux": "253667-093 Vol2B RDTSCP (\"ECX := IA32_TSC_AUX[31:0];\") and RDPID (\"DEST := IA32_TSC_AUX\")",
    "os": "the OS topology of the same capture (GetLogicalProcessorInformationEx RelationAll, taken by cpuid_capture 1.6 on the "
          "same machine): packages, cores and caches with their logical-processor masks",
}

# ------------------------------------------------------------------------------------------------- views
class View:
    """one logical processor's CPUID answers. complete = every (leaf, sub-leaf) can be asked (emulated); otherwise only the
    listed rows exist and a rule that needs a missing row is not applicable."""

    def __init__(self, name, rows, complete=False, kind="profile"):
        self.name, self.rows, self.complete, self.kind = name, dict(rows), complete, kind

    def q(self, leaf, sub=0):
        return self.rows.get((leaf, sub))

    def max_basic(self):
        r = self.q(0)
        return r[0] if r else None

    def max_ext(self):
        r = self.q(0x80000000)
        return r[0] if r else None


def bits(v, hi, lo):
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)


def pow2ceil(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def log2(p):
    return p.bit_length() - 1


def load_profile(path):
    rows = {}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = line.split()
        if len(f) != 6:
            raise SystemExit("[cpuid_consistency] FAIL: %s line %d: not 6 fields" % (path, n))
        rows[(int(f[0], 16), int(f[1], 16))] = tuple(int(x, 16) for x in f[2:])
    return rows


RESOLVED = {}


def capture_root():
    r = os.environ.get(CAPTURE_ENV, "").strip()
    if r:
        return os.path.normpath(r)
    p = ROOT
    while True:
        c = os.path.join(p, "capture")
        if os.path.isdir(os.path.join(c, "host", "output")):
            return c
        q = os.path.dirname(p)
        if q == p:
            return None
        p = q


def find_capture(env):
    label, suffix, stem = CAPTURES[env]
    root = capture_root()
    if not root:
        return None, "no capture root (set %s or keep capture\\host\\output beside the repositories)" % CAPTURE_ENV
    d = os.path.join(root, "host", "output")
    hits = sorted(x for x in os.listdir(d) if x.endswith(suffix) and os.path.isdir(os.path.join(d, x)))
    if len(hits) != 1:
        return None, "%d capture folders under capture\\host\\output end with %s (expected exactly one)" % (len(hits), suffix)
    real = os.path.join(d, hits[0])
    RESOLVED[real] = os.path.join("capture", "host", "output", label + suffix)
    return (real, stem), None


def shown(path):
    for real, disp in RESOLVED.items():
        if path.startswith(real):
            return disp + path[len(real):]
    return path


def load_capture(env):
    """raw TSV -> {os_lp: {(leaf, sub): regs}} (call 1), the header lines, the OS topology"""
    found, err = find_capture(env)
    if err:
        return None, err
    d, stem = found
    raw = os.path.join(d, stem + "_raw.tsv")
    topo = os.path.join(d, stem + "_topology.txt")
    for p in (raw, topo):
        if not os.path.isfile(p):
            return None, "missing capture file " + shown(p)
    lps, header = {}, []
    for line in open(raw, encoding="utf-8"):
        line = line.rstrip("\r\n")
        if line.startswith("#"):
            header.append(line)
            continue
        if not line or line.startswith("lp\t"):
            continue
        f = line.split("\t")
        if len(f) < 14 or f[7] != "1":
            continue
        if f[3] != "1" or f[4] != "0":
            return None, "%s: LP %s not pinned or off-processor samples" % (shown(raw), f[0])
        lps.setdefault(int(f[0]), {})[(int(f[5], 16), int(f[6], 16))] = tuple(int(x, 16) for x in f[8:12])
    # OS topology: "package ... group0_mask=M", "core ... efficiency_class=E ... group0_mask=M", "cache level=L type=T ... group0_mask=M"
    osv = {"package": [], "core": [], "cache": []}
    in_raw = False
    for line in open(topo, encoding="utf-8"):
        line = line.strip()
        if line.startswith("## "):
            in_raw = line.startswith("## RAW GetLogicalProcessorInformationEx")
            continue
        if not in_raw:
            continue
        m = re.match(r"^(package|core|cache)\b(.*)group0_mask=([0-9A-Fa-f]+)", line)
        if not m:
            continue
        mask = int(m.group(3), 16)
        lpset = frozenset(i for i in range(64) if mask >> i & 1)
        kv = dict(re.findall(r"(\w+)=(\S+)", m.group(2)))
        if m.group(1) == "cache":
            osv["cache"].append((int(kv["level"]), int(kv["type"]), lpset))
        elif m.group(1) == "core":
            osv["core"].append((int(kv.get("efficiency_class", "0")), lpset))
        else:
            osv["package"].append(lpset)
    if not lps:
        return None, shown(raw) + ": no rows"
    return {"lps": lps, "header": header, "os": osv, "raw": shown(raw), "topo": shown(topo)}, None


def emulated_view(build, flag, name):
    """CPUID through the engine: one emu-snippet run that stores each answer in guest memory (dumped back)"""
    exe = os.path.join(build, "emu-snippet.exe")
    if not os.path.isfile(exe):
        return None, "missing " + exe
    queries = []
    for leaf in range(0, 0x30):
        for sub in (range(0, 32) if leaf in SUBLEAF_LEAVES else (0, 1)):
            queries.append((leaf, sub))
    for leaf in (0xB, 0x1F):
        queries += [(leaf, 0xFF), (leaf, 0x107)]
    for leaf in range(0x80000000, 0x80000010):
        queries += [(leaf, 0), (leaf, 1)]
    for leaf in OUT_OF_RANGE:
        queries.append((leaf, 0))
    code = bytearray()
    for leaf, sub in queries:
        code += b"\xB8" + leaf.to_bytes(4, "little") + b"\xB9" + sub.to_bytes(4, "little") + b"\x0F\xA2"
        code += b"\x89\x07\x89\x5F\x04\x89\x4F\x08\x89\x57\x0C\x48\x83\xC7\x10"   # mov [rdi]..[rdi+12]; add rdi, 16
    # TSC_AUX (rule tsc_aux): rdtscp -> [rdi]; rdtscp -> [rdi+4]; rdpid rax -> [rdi+8]; rdmsr C0000103H -> [rdi+12]
    tail = bytearray(b"\x0F\x01\xF9\x89\x0F\x0F\x01\xF9\x89\x4F\x04\xF3\x0F\xC7\xF8\x89\x47\x08\xB9\x03\x01\x00\xC0\x0F\x32\x89\x47\x0C")
    data = 0x200000
    size = (len(queries) + 1) * 16
    region = (size + 0xFFF) & ~0xFFF
    base_spec = {"spec": 1, "name": "cpuid_consistency " + name, "regions": [{"address": hex(data), "size": hex(region), "prot": "rw", "name": "answers"}],
                 "state": {"rdi": hex(data)}, "stop": {"max_insns": 200000}}
    out = {}
    for part, blob, length in (("cpuid", code, len(queries) * 16), ("tsc_aux", tail, 16)):
        spec = dict(base_spec)
        spec["code"] = {"hex": blob.hex()}
        spec["dump_mem"] = [{"address": hex(data), "size": hex(length)}]
        fd, path = tempfile.mkstemp(prefix="cpuidcons_", suffix=".json")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                json.dump(spec, f)
            p = subprocess.run([exe, "--state", path, flag], capture_output=True, text=True, timeout=600)
        finally:
            os.remove(path)
        dump = [l for l in p.stdout.splitlines() if "memory [" in l]
        if not dump:
            if part == "tsc_aux":
                out["tsc_aux"] = ("no result", "\n".join(p.stdout.splitlines()[-6:]))
                continue
            return None, "%s %s: no memory dump (exit %d): %s" % (name, part, p.returncode, " | ".join(p.stdout.splitlines()[-5:]))
        hx = dump[-1].split("=", 1)[1].strip()
        raw = bytes.fromhex(hx)
        if part == "cpuid":
            rows = {}
            for i, k in enumerate(queries):
                rows[k] = tuple(int.from_bytes(raw[16 * i + 4 * r:16 * i + 4 * r + 4], "little") for r in range(4))
            out["rows"] = rows
        else:
            out["tsc_aux"] = tuple(int.from_bytes(raw[4 * r:4 * r + 4], "little") for r in range(4))
    v = View(name, out["rows"], complete=True, kind="emulated")
    v.tsc_aux = out.get("tsc_aux")
    v.queries = queries
    return v, None


# ------------------------------------------------------------------------------------------------- rules (one LP)
PASS, INCONS, NA = "pass", "inconsistent", "n/a"


def r_vendor(v):
    r = v.q(0)
    if not r:
        return NA, "leaf 0 not listed"
    s = b"".join(x.to_bytes(4, "little") for x in (r[1], r[3], r[2])).decode("latin-1")
    if s != "GenuineIntel":
        return INCONS, "vendor %r" % s
    return PASS, "GenuineIntel, MAX_LEAF %02XH" % r[0]


def r_version(v):
    r = v.q(1)
    if not r:
        return NA, "leaf 01H not listed"
    a = r[0]
    fam, model, ext_m, ext_f = bits(a, 11, 8), bits(a, 7, 4), bits(a, 19, 16), bits(a, 27, 20)
    dfam = fam if fam != 0xF else fam + ext_f
    dmod = (ext_m << 4) + model if fam in (6, 0xF) else model
    bad = []
    if bits(a, 15, 14):
        bad.append("EAX[15:14] reserved = %d" % bits(a, 15, 14))
    if bits(a, 31, 28):
        bad.append("EAX[31:28] reserved = %d" % bits(a, 31, 28))
    txt = "DisplayFamily %02XH DisplayModel %02XH stepping %d" % (dfam, dmod, bits(a, 3, 0))
    return (INCONS, txt + "; " + "; ".join(bad)) if bad else (PASS, txt)


def topo_levels(v, leaf):
    """the listed / answered sub-leaves of 0BH / 1FH: [(sub, eax, ebx, ecx, edx)] in order"""
    out = []
    for sub in range(0, 32):
        r = v.q(leaf, sub)
        if r is None:
            if v.complete:
                break
            continue
        out.append((sub,) + r)
    return out


def supported(v, leaf):
    mb = v.max_basic()
    r = v.q(leaf, 0)
    return mb is not None and mb >= leaf and r is not None and bits(r[1], 15, 0) != 0


def r_apicid(v):
    r1 = v.q(1)
    if not r1:
        return NA, "leaf 01H not listed"
    init = bits(r1[1], 31, 24)
    have_b, have_f = supported(v, 0xB), supported(v, 0x1F)
    if not have_b and not have_f:
        return NA, "neither 0BH nor 1FH enumerated (0BH.0 EBX[15:0] = 0 or MAX_LEAF below): the 8-bit initial APIC ID %d is the only ID" % init
    bad, txt = [], ["01H EBX[31:24] = %d" % init]
    if have_b:
        x = v.q(0xB, 0)[3]
        txt.append("0BH EDX = %d" % x)
        if x & 0xFF != init:
            bad.append("01H EBX[31:24] %d != 0BH EDX[7:0] %d" % (init, x & 0xFF))
    if have_f:
        y = v.q(0x1F, 0)[3]
        txt.append("1FH EDX = %d" % y)
        if y & 0xFF != init:
            bad.append("01H EBX[31:24] %d != 1FH EDX[7:0] %d" % (init, y & 0xFF))
        if have_b and v.q(0xB, 0)[3] != y:
            bad.append("0BH EDX %d != 1FH EDX %d (both are the x2APIC ID of the current logical processor)" % (v.q(0xB, 0)[3], y))
    return (INCONS, "; ".join(bad)) if bad else (PASS, ", ".join(txt))


def r_topo(v, leaf):
    if v.max_basic() is None or v.max_basic() < leaf or v.q(leaf, 0) is None:
        return NA, "%02XH not within MAX_LEAF / not listed" % leaf
    lv = topo_levels(v, leaf)
    if not supported(v, leaf):
        if v.complete and any(s[1] or s[2] or bits(s[3], 15, 8) for s in lv):
            return INCONS, "%02XH.0 EBX[15:0] = 0 (not supported) but a sub-leaf reports a domain" % leaf
        return PASS, "%02XH not supported (sub-leaf 0 EBX[15:0] = 0, %d sub-leaves checked)" % (leaf, len(lv))
    bad = []
    edx0 = lv[0][4]
    invalid_seen, last_shift, shifts = False, -1, []
    for sub, a, b, c, d in lv:
        if d != edx0:
            bad.append("sub-leaf %d EDX %d != sub-leaf 0 EDX %d" % (sub, d, edx0))
        if bits(c, 7, 0) != sub & 0xFF:
            bad.append("sub-leaf %d ECX[7:0] = %d" % (sub, bits(c, 7, 0)))
        dt = bits(c, 15, 8)
        if dt == 0:
            invalid_seen = True
            if a or b:
                bad.append("sub-leaf %d: invalid domain with EAX %X / EBX %X (must be 0)" % (sub, a, b))
        else:
            if invalid_seen:
                bad.append("sub-leaf %d: domain type %d after an invalid sub-leaf" % (sub, dt))
            sh = bits(a, 4, 0)
            if sh < last_shift:
                bad.append("sub-leaf %d: shift %d below the previous %d" % (sub, sh, last_shift))
            last_shift = sh
            shifts.append("%d:%d/%d" % (dt, sh, bits(b, 15, 0)))
    if v.complete:
        for sub in (0xFF, 0x107):
            r = v.q(leaf, sub)
            if r is None:
                continue
            if r[3] != edx0 or bits(r[2], 7, 0) != sub & 0xFF or bits(r[2], 15, 8) or r[0] or r[1]:
                bad.append("sub-leaf %XH: EAX %X EBX %X ECX %X EDX %d (expected 0 0 %02X x2APIC ID)" % (sub, r[0], r[1], r[2], r[3], sub & 0xFF))
    if not invalid_seen and v.complete:
        bad.append("no invalid (terminating) sub-leaf among 0..31")
    txt = "domains (type:shift/count) " + " ".join(shifts) + ", x2APIC ID %d, %d sub-leaves" % (edx0, len(lv))
    return (INCONS, "; ".join(bad) + " | " + txt) if bad else (PASS, txt)


def cache_list(v):
    """leaf 4: [(sub, type, level, share_ids, cores_ids, ways, parts, line, sets)] for the valid sub-leaves, + problems"""
    out, bad = [], []
    if v.max_basic() is None or v.max_basic() < 4 or v.q(4, 0) is None:
        return None, ["04H not within MAX_LEAF / not listed"]
    null_seen = None
    for sub in range(0, 32):
        r = v.q(4, sub)
        if r is None:
            if v.complete:
                break
            continue
        a, b, c, d = r
        t = bits(a, 4, 0)
        if null_seen is not None:
            if any(r):
                bad.append("sub-leaf %d after the null sub-leaf %d is not all 0" % (sub, null_seen))
            continue
        if t == 0:
            null_seen = sub
            continue
        out.append((sub, t, bits(a, 7, 5), bits(a, 25, 14) + 1, bits(a, 31, 26) + 1, bits(b, 31, 22) + 1, bits(b, 21, 12) + 1,
                    bits(b, 11, 0) + 1, c + 1))
    if null_seen is None:
        bad.append("no null (EAX[4:0] = 0) sub-leaf %s" % ("among 0..31" if v.complete else "listed"))
    return out, bad


def r_leaf4(v):
    cl, bad = cache_list(v)
    if cl is None:
        return NA, bad[0]
    if not cl:
        return INCONS, "sub-leaf 0 is null although MAX_LEAF >= 4"
    cores = set(x[4] for x in cl)
    if len(cores) != 1:
        bad.append("EAX[31:26] differs between valid sub-leaves: %s" % sorted(cores))
    for x in cl:
        if x[1] > 3 or x[2] < 1:
            bad.append("sub-leaf %d: type %d level %d" % (x[0], x[1], x[2]))
    txt = ", ".join("L%d%s %dK share %d" % (x[2], "?DIU"[x[1]], x[5] * x[6] * x[7] * x[8] // 1024, x[3]) for x in cl)
    return (INCONS, "; ".join(bad) + " | " + txt) if bad else (PASS, txt + ", cores field %d" % cl[0][4])


# SDM 253665-093 Vol1 Table 21-12 "Encoding of CPUID Leaf 2 Descriptors" (pp. 21-16..21-19): the cache descriptors this
# tool decodes -> (level, leaf-4 type 1 data / 2 instruction / 3 unified, KBytes, ways, line bytes); others are listed only
DESCRIPTORS = {
    0x2C: (1, 1, 32, 8, 64),      # "1st-level data cache: 32 KBytes, 8-way set associative, 64 byte line size."
    0x30: (1, 2, 32, 8, 64),      # "1st-level instruction cache: 32 KBytes, 8-way set associative, 64 byte line size."
    0x7D: (2, 3, 2048, 8, 64),    # "2nd-level cache: 2 MByte, 8-way set associative, 64byte line size."
    0x80: (2, 3, 512, 8, 64),     # "2nd-level cache: 512 KByte, 8-way set associative, 64-byte line size."
    0x4D: (3, 3, 16384, 16, 64),  # "3rd-level cache: 16MByte, 16-way set associative, 64 byte line size."
}


def r_leaf2(v):
    r = v.q(2)
    if not r:
        return NA, "leaf 02H not listed"
    bad, desc = [], []
    if r[0] & 0xFF != 1:
        bad.append("AL = %02XH (always 01H)" % (r[0] & 0xFF))
    for i, x in enumerate(r):
        if x >> 31:
            continue
        for k in range(4):
            if i == 0 and k == 0:
                continue
            d = x >> (8 * k) & 0xFF
            if d:
                desc.append(d)
    txt = "descriptors " + " ".join("%02X" % d for d in desc)
    # cache descriptors (SDM Table 21-12) vs the deterministic cache parameters of leaf 4
    cl, _ = cache_list(v)
    for d in desc:
        if d in DESCRIPTORS and cl:
            lvl, typ, kb, ways, line = DESCRIPTORS[d]
            hit = [x for x in cl if x[2] == lvl and x[1] == typ]
            if not hit:
                bad.append("descriptor %02XH (L%d type %d) but leaf 4 has no such cache" % (d, lvl, typ))
            elif not any(x[5] * x[6] * x[7] * x[8] // 1024 == kb and x[5] == ways and x[7] == line for x in hit):
                x = hit[0]
                bad.append("descriptor %02XH = L%d %dK %d-way %dB line, leaf 4 L%d %dK %d-way %dB line" % (d, lvl, kb, ways, line, lvl, x[5] * x[6] * x[7] * x[8] // 1024, x[5], x[7]))
    if 0xFF in desc:
        cl, _ = cache_list(v)
        if not cl:
            bad.append("FFH (use leaf 4) but leaf 4 enumerates no cache")
    if 0xFE in desc:
        mb = v.max_basic()
        r18 = v.q(0x18, 0)
        if mb is None or mb < 0x18 or r18 is None or r18[0] == 0:
            bad.append("FEH (use leaf 18H) but leaf 18H is %s" % ("above MAX_LEAF" if mb is not None and mb < 0x18 else "not listed" if r18 is None else "empty (18H.0 EAX = 0)"))
        else:
            valid = [s for s in range(1, min(r18[0], 31) + 1) if v.q(0x18, s) and bits(v.q(0x18, s)[3], 4, 0)]
            if not valid:
                bad.append("FEH (use leaf 18H) but no sub-leaf 1..%d of 18H has a TLB type (EDX[4:0])" % r18[0])
    return (INCONS, "; ".join(bad) + " | " + txt) if bad else (PASS, txt)


def r_leaf1a(v):
    r7 = v.q(7, 0)
    mb = v.max_basic()
    if r7 is None or mb is None:
        return NA, "07H / 00H not listed"
    hybrid = bits(r7[3], 15, 15)
    r = v.q(0x1A) if mb >= 0x1A else None
    ct = bits(r[0], 31, 24) if r else 0
    if hybrid and (not r or r[0] == 0):
        return INCONS, "07H.0 EDX[15] HYBRID = 1 but leaf 1AH is %s" % ("above MAX_LEAF" if mb < 0x1A else "0 / not listed")
    if r and r[0] and ct not in (0x20, 0x40):
        return INCONS, "core type %02XH is reserved" % ct
    return PASS, "hybrid %d, core type %s" % (hybrid, "%02XH" % ct if r and r[0] else "none")


def brand(v):
    mx = v.max_ext()
    if mx is None or mx < 0x80000004:
        return None
    bs = b""
    for leaf in (0x80000002, 0x80000003, 0x80000004):
        r = v.q(leaf)
        if r is None:
            return None
        bs += b"".join(x.to_bytes(4, "little") for x in r)
    return bs


def r_brand(v):
    bs = brand(v)
    if bs is None:
        return NA, "brand string leaves not listed / MAX_EXTENDED_LEAF below 80000004H"
    bad = []
    if any(not (0x20 <= c < 0x7F or c == 0) for c in bs):
        bad.append("non-ASCII byte")
    z = bs.find(b"\0")
    if z < 0:
        bad.append("no NUL in the 48 bytes")
    elif any(bs[z:]):
        bad.append("text after the first NUL")
    s = bs.split(b"\0")[0].decode("latin-1").strip()
    return (INCONS, "; ".join(bad) + " | %r" % s) if bad else (PASS, repr(s))


def r_xsave(v):
    r1 = v.q(1)
    if not r1 or not bits(r1[2], 26, 26):
        return NA, "no XSAVE (01H ECX[26] = 0) or 01H not listed"
    r0, rs = v.q(0xD, 0), v.q(0xD, 1)
    if r0 is None:
        return NA, "0DH.0 not listed"
    user = r0[0] | r0[3] << 32
    sup = (rs[2] | rs[3] << 32) if rs else 0
    bad = []
    if user & 3 != 3:
        bad.append("0DH.0 EAX[1:0] = %d (x87 and SSE state are always supported)" % (user & 3))
    end = 576
    for i in range(2, 63):
        if not (user >> i & 1 or sup >> i & 1):
            continue
        r = v.q(0xD, i)
        if r is None:
            if v.complete:
                bad.append("component %d enumerated, sub-leaf %d all 0" % (i, i))
            continue
        if not r[0]:
            bad.append("component %d: size 0" % i)
        if user >> i & 1:
            if r[1] < 576:
                bad.append("component %d: offset %d inside the legacy region + header (576 bytes)" % (i, r[1]))
            if r[2] & 1:
                bad.append("component %d: ECX[0] = 1 (IA32_XSS) for an XCR0 component" % i)
            end = max(end, r[1] + r[0])
        else:
            if r[1] != 0 or not r[2] & 1:
                bad.append("component %d (IA32_XSS): EBX %d ECX[0] %d" % (i, r[1], r[2] & 1))
    if end > r0[2]:
        bad.append("0DH.0 ECX %d below the end of the last component %d" % (r0[2], end))
    txt = "XCR0 bits %X, IA32_XSS bits %X, max size %d" % (user, sup, r0[2])
    return (INCONS, "; ".join(bad) + " | " + txt) if bad else (PASS, txt)


def r_l2(v):
    mx = v.max_ext()
    r = v.q(0x80000006) if mx is not None and mx >= 0x80000006 else None
    if r is None:
        return NA, "80000006H not listed / above MAX_EXTENDED_LEAF"
    cl, _ = cache_list(v)
    l2 = [x for x in (cl or []) if x[2] == 2 and x[1] == 3]
    if not l2:
        return NA, "no unified level-2 cache in leaf 4"
    x = l2[0]
    ways_tab = {1: 1, 2: 2, 4: 4, 6: 8, 8: 16, 0xA: 32, 0xB: 48, 0xC: 64, 0xD: 96, 0xE: 128}
    c = r[2]
    size_k, assoc, line = bits(c, 31, 16), bits(c, 15, 12), bits(c, 7, 0)
    l4_k = x[5] * x[6] * x[7] * x[8] // 1024
    bad = []
    if size_k != l4_k:
        bad.append("80000006H L2 %dK != leaf 4 L2 %dK" % (size_k, l4_k))
    if line != x[7]:
        bad.append("line %d != leaf 4 line %d" % (line, x[7]))
    if assoc in ways_tab and ways_tab[assoc] != x[5]:
        bad.append("associativity code %XH (%d ways) != leaf 4 %d ways" % (assoc, ways_tab[assoc], x[5]))
    if assoc in (3, 5, 9):
        bad.append("associativity code %XH reserved" % assoc)
    txt = "L2 %dK line %d assoc code %XH%s" % (size_k, line, assoc, " (see leaf 4: %d ways)" % x[5] if assoc == 7 else "")
    return (INCONS, "; ".join(bad) + " | " + txt) if bad else (PASS, txt)


def r_range(v):
    """emulated only: the out-of-range leaves return the highest basic leaf (unless 01H ECX[31] announces a hypervisor)"""
    if not v.complete:
        return NA, "needs an answering CPU"
    mb, mx = v.max_basic(), v.max_ext()
    top = v.q(mb, 0)
    bad = []
    hv = bits(v.q(1)[2], 31, 31)
    probes = [mb + 1] + [x for x in range(mb + 1, 0x30) if (x, 0) in v.rows] + [mx + 1] + OUT_OF_RANGE
    seen = set()
    for leaf in probes:
        if leaf in seen or (leaf, 0) not in v.rows:
            continue
        seen.add(leaf)
        if 0x40000000 <= leaf <= 0x4FFFFFFF and hv:
            continue
        if v.q(leaf, 0) != top:
            bad.append("%XH answers %s, not the highest basic leaf %02XH" % (leaf, "/".join("%X" % x for x in v.q(leaf, 0)), mb))
    txt = "%d out-of-range leaves return leaf %02XH%s" % (len(seen), mb, " (hypervisor bit set: 4000xxxxH skipped)" if hv else "")
    return (INCONS, "; ".join(bad[:4]) + (" (+%d)" % (len(bad) - 4) if len(bad) > 4 else "") + " | " + txt) if bad else (PASS, txt)


def r_ecx_ignored(v):
    """emulated only: a leaf without sub-leaves answers the same for ECX 0 and 1 (Vol1: "provides the same information
    regardless of the value of ECX")"""
    if not v.complete:
        return NA, "needs an answering CPU"
    mb, mx = v.max_basic(), v.max_ext()
    inr = [leaf for (leaf, sub) in v.rows if sub == 1 and leaf not in SUBLEAF_LEAVES and (leaf <= mb or 0x80000000 <= leaf <= mx)]
    bad = [leaf for leaf in inr if v.q(leaf, 0) != v.q(leaf, 1)]
    return (INCONS, "leaves answering differently for ECX 0 / 1: " + " ".join("%XH" % x for x in sorted(bad))) if bad else (PASS, "%d leaves within MAX_LEAF / MAX_EXTENDED_LEAF (an out-of-range leaf answers as the highest basic leaf, ECX included: K8.12)" % len(inr))


def r_tsc_aux(v):
    t = getattr(v, "tsc_aux", None)
    if t is None:
        return NA, "needs an answering CPU"
    if isinstance(t[0], str):
        return NA, "RDTSCP / RDPID / RDMSR not executable here: " + t[1].replace("\n", " | ")[:200]
    if len(set(t)) != 1:
        return INCONS, "RDTSCP ECX %X / %X, RDPID %X, IA32_TSC_AUX %X" % t
    return PASS, "RDTSCP (twice) = RDPID = IA32_TSC_AUX = %X" % t[0]


RULES = [  # id, function, source key, scope
    ("K8.01 vendor", r_vendor, "vendor"),
    ("K8.02 version", r_version, "version"),
    ("K8.03 apic_id", r_apicid, "apicid"),
    ("K8.04 leaf0B", lambda v: r_topo(v, 0xB), "topo"),
    ("K8.05 leaf1F", lambda v: r_topo(v, 0x1F), "topo"),
    ("K8.06 leaf04", r_leaf4, "leaf4"),
    ("K8.07 leaf02", r_leaf2, "leaf2"),
    ("K8.08 leaf1A", r_leaf1a, "leaf1a"),
    ("K8.09 brand", r_brand, "brand"),
    ("K8.10 xsave", r_xsave, "xsave"),
    ("K8.11 l2_80000006", r_l2, "l2"),
    ("K8.12 out_of_range", r_range, "range"),
    ("K8.13 ecx_ignored", r_ecx_ignored, "range"),
    ("K8.14 tsc_aux", r_tsc_aux, "tscaux"),
]


def run_rules(v):
    res = []
    for rid, fn, src in RULES:
        try:
            st, txt = fn(v)
        except Exception as e:   # a rule must never crash silently: report it as a tool failure
            st, txt = "error", "%s: %s" % (type(e).__name__, e)
        res.append((v.name, rid, st, txt))
    return res


# ------------------------------------------------------------------------------------------------- environment rules
def x2apic(rows):
    for leaf in (0x1F, 0xB):
        r = rows.get((leaf, 0))
        if r and bits(r[1], 15, 0):
            return r[3]
    return bits(rows[(1, 0)][1], 31, 24)


def shifts(rows):
    """(SMT shift, package shift) from 1FH (else 0BH)"""
    for leaf in (0x1F, 0xB):
        r0 = rows.get((leaf, 0))
        if not (r0 and bits(r0[1], 15, 0)):
            continue
        lv = [rows[(leaf, s)] for s in range(32) if (leaf, s) in rows and bits(rows[(leaf, s)][2], 15, 8)]
        return bits(lv[0][0], 4, 0), bits(lv[-1][0], 4, 0)
    return None


DOMAIN_LP = {  # fields of the Logical Processor domain (253665-093 Vol1 leaf tables, Domain column), by (leaf, sub or None, reg) -> mask
    (0x01, None, 1): 0xFF000000,   # Table 21-8 EBX[31:24] INITIAL_APIC_ID
    (0x04, None, 0): 0x03FFC3FF,   # Table 21-14 p. 21-21 EAX[9:0], [25:14]
    (0x04, None, 1): 0xFFFFF000,   # EBX[21:12] partitions, [31:22] ways
    (0x04, None, 2): 0xFFFFFFFF,   # ECX sets
    (0x04, None, 3): 0x00000007,   # EDX[2:0]
    (0x06, None, 3): 0xFFFF0000,   # Table 21-16 p. 21-25 EDX[31:16] (HFI / Thread Director row index)
    (0x0B, None, 1): 0x0000FFFF,   # Table 21-31 / 21-70 EBX[15:0] logical processors at this domain
    (0x0B, None, 3): 0xFFFFFFFF,   # EDX x2APIC ID
    (0x14, 0, 2): 0x80000000,      # Table 21-51 p. 21-57 14H.0 ECX[31] LIP
    (0x16, None, 0): 0xFFFF, (0x16, None, 1): 0xFFFF, (0x16, None, 2): 0xFFFF,   # Table 21-54 p. 21-60 frequencies
    (0x18, None, 0): 0xFFFFFFFF, (0x18, None, 1): 0xFFFFFFFF, (0x18, None, 2): 0xFFFFFFFF, (0x18, None, 3): 0xFFFFFFFF,   # Tables 21-60 / 21-61
    (0x1A, None, 0): 0xFFFFFFFF,   # Table 21-63 p. 21-66
    (0x1C, None, 0): 0x80000000,   # Table 21-66 EAX[31] IP_VALUES_CONTAIN_LIP
    (0x1F, None, 1): 0x0000FFFF, (0x1F, None, 3): 0xFFFFFFFF,
    (0x80000006, None, 2): 0xFFFFF0FF,   # Table 21-95 ECX[7:0], [31:12]
}


def lp_mask(leaf, sub, reg):
    return DOMAIN_LP.get((leaf, sub, reg), DOMAIN_LP.get((leaf, None, reg), 0))


def env_rules(env, cap, profiles):
    """the rules that compare the LPs of one environment with each other, with the OS topology and with its profiles"""
    name = "env:" + env
    lps, osv = cap["lps"], cap["os"]
    res = []
    ids = {lp: x2apic(rows) for lp, rows in lps.items()}
    # E.01 unique x2APIC IDs
    dup = len(set(ids.values())) != len(ids)
    res.append((name, "K8.20 x2apic_unique", INCONS if dup else PASS, "%d LPs, %d distinct x2APIC IDs" % (len(ids), len(set(ids.values())))))
    # E.02 domains: every field that differs between LPs is in the Logical Processor domain
    keys = sorted(set().union(*[set(r) for r in lps.values()]))
    bad = []
    nfields = 0
    for k in keys:
        if 0x40000000 <= k[0] <= 0x4FFFFFFF:
            continue
        for reg in range(4):
            vals = [rows[k][reg] for rows in lps.values() if k in rows]
            m = 0
            for x in vals:
                m |= x ^ vals[0]
            if not m:
                continue
            nfields += 1
            rest = m & ~lp_mask(k[0], k[1], reg)
            if rest:
                bad.append("%XH.%d %s bits %08X differ between LPs outside the Logical Processor domain" % (k[0], k[1], "ABCD"[reg], rest))
    res.append((name, "K8.21 domains", INCONS if bad else PASS, "; ".join(bad) if bad else "%d (leaf, register) fields differ between LPs, all in the Logical Processor domain" % nfields))
    # E.03 packages / cores from the x2APIC ID = the OS packages / cores
    sh = {lp: shifts(rows) for lp, rows in lps.items()}
    if any(s is None for s in sh.values()):
        res.append((name, "K8.22 os_topology", NA, "an LP enumerates neither 1FH nor 0BH"))
    else:
        bad = []
        def groups(fn):
            g = {}
            for lp in lps:
                g.setdefault(fn(lp), set()).add(lp)
            return sorted(frozenset(x) for x in g.values())
        pk = groups(lambda lp: ids[lp] >> sh[lp][1])
        co = groups(lambda lp: ids[lp] >> sh[lp][0])
        if sorted(osv["package"]) != pk:
            bad.append("packages: CPUID %s, OS %s" % ([sorted(x) for x in pk], [sorted(x) for x in sorted(osv["package"])]))
        if sorted(c[1] for c in osv["core"]) != co:
            bad.append("cores: CPUID %d groups, OS %d groups (differ)" % (len(co), len(osv["core"])))
        res.append((name, "K8.22 os_topology", INCONS if bad else PASS, "; ".join(bad) if bad else "%d package(s), %d core(s): CPUID (x2APIC ID >> package / SMT shift) = OS" % (len(pk), len(co))))
        # E.04 cache sharing from leaf 4 = the OS cache masks
        bad, ncache = [], 0
        for lp, rows in lps.items():
            v = View("lp", rows)
            cl, _ = cache_list(v)
            for x in cl or []:
                w = log2(pow2ceil(x[3]))
                share = frozenset(o for o in lps if ids[o] >> w == ids[lp] >> w and any(y[1] == x[1] and y[2] == x[2] for y in (cache_list(View("o", lps[o]))[0] or [])))
                ostype = {1: 1, 2: 2, 3: 0}[x[1]]   # leaf 4 type (data / instruction / unified) -> PROCESSOR_CACHE_TYPE (CacheData 1, CacheInstruction 2, CacheUnified 0)
                osm = [c[2] for c in osv["cache"] if c[0] == x[2] and c[1] == ostype and lp in c[2]]
                ncache += 1
                if len(osm) != 1 or osm[0] != share:
                    bad.append("LP %d L%d type %d: CPUID shares with %s, OS %s" % (lp, x[2], x[1], sorted(share), [sorted(m) for m in osm]))
        res.append((name, "K8.23 cache_sharing", INCONS if bad else PASS, "; ".join(bad[:3]) + (" (+%d)" % (len(bad) - 3) if len(bad) > 3 else "") if bad else "%d (LP, cache) pairs: leaf 4 sharing (x2APIC ID >> log2 pow2ceil(1 + EAX[25:14])) = the OS cache masks" % ncache))
        # E.05 core types (hybrid): every OS core holds one core type; one efficiency class per core type
        cts = {lp: bits(lps[lp].get((0x1A, 0), (0, 0, 0, 0))[0], 31, 24) for lp in lps}
        bad = []
        cls = {}
        for ec, s in osv["core"]:
            t = set(cts[lp] for lp in s if lp in cts)
            if len(t) != 1:
                bad.append("an OS core holds core types %s" % sorted(t))
            for x in t:
                cls.setdefault(x, set()).add(ec)
        for t, e in cls.items():
            if len(e) != 1:
                bad.append("core type %02XH in efficiency classes %s" % (t, sorted(e)))
        res.append((name, "K8.24 core_types", INCONS if bad else PASS, "; ".join(bad) if bad else "core types " + ", ".join("%02XH -> efficiency class %s" % (t, sorted(e)[0]) for t, e in sorted(cls.items()))))
    # E.06 each profile = the capture LP with the same x2APIC ID (rows the capture holds; arch / reserved / probe)
    for pname, prow in profiles:
        pid = x2apic(prow)
        lp = [o for o in lps if ids[o] == pid]
        if not lp:
            res.append((name, "K8.25 profile_rows " + pname, INCONS, "no LP of this capture has the profile's x2APIC ID %d" % pid))
            continue
        crow = lps[lp[0]]
        diff = []
        common = [k for k in prow if k in crow and not (0x40000000 <= k[0] <= 0x4FFFFFFF)]
        for k in common:
            for reg in range(4):
                mm = 0
                for (ml, ms), masks in MUTABLE.items():
                    if ml == k[0] and (ms is None or ms == k[1]):
                        mm |= masks[reg]
                d = (prow[k][reg] ^ crow[k][reg]) & ~mm
                if d:
                    diff.append("%XH.%d %s bits %08X" % (k[0], k[1], "ABCD"[reg], d))
        # rows of the profile with the per-LP fields of ANOTHER LP (a profile mixed from several LPs)
        ids_in_profile = set()
        for leaf in (0xB, 0x1F):
            if (leaf, 0) in prow:
                ids_in_profile.add(prow[(leaf, 0)][3])
        if (1, 0) in prow:
            ids_in_profile.add(bits(prow[(1, 0)][1], 31, 24) | (pid & ~0xFF))
        mixed = len(ids_in_profile) > 1
        st = INCONS if diff or mixed else PASS
        txt = "LP %d (x2APIC ID %d): %d common rows, %d differing fields%s%s" % (lp[0], pid, len(common), len(diff), (": " + "; ".join(diff[:6]) + (" (+%d)" % (len(diff) - 6) if len(diff) > 6 else "")) if diff else "",
                                                                          "; the profile's per-LP fields name x2APIC IDs %s (rows taken on different LPs)" % sorted(ids_in_profile) if mixed else "")
        res.append((name, "K8.25 profile_rows " + pname, st, txt))
    return res


def emu_vs_profile(ev, prow, pname):
    """the emulated CPU answers every listed profile row unchanged except the runtime-mutable fields"""
    diff = []
    n = 0
    for k, pr in sorted(prow.items()):
        er = ev.q(*k)
        if er is None:
            continue
        n += 1
        for reg in range(4):
            mm = 0
            for (ml, ms), masks in MUTABLE.items():
                if ml == k[0] and (ms is None or ms == k[1]):
                    mm |= masks[reg]
            d = (pr[reg] ^ er[reg]) & ~mm
            if d:
                diff.append("%XH.%d %s bits %08X" % (k[0], k[1], "ABCD"[reg], d))
    return (ev.name, "K8.26 rows_as_profile", INCONS if diff else PASS,
            ("; ".join(diff[:6]) + (" (+%d)" % (len(diff) - 6) if len(diff) > 6 else "")) if diff else "%d %s rows answered unchanged (mutable fields of Table 21-1 / 21-2 excluded)" % (n, pname))


# ------------------------------------------------------------------------------------------------- driver
def collect(build, want_emu=True):
    results, notes = [], []
    prof_rows = {}
    for vname, fname, env in PROFILES:
        rows = load_profile(os.path.join(DATA, fname))
        prof_rows[vname] = rows
        results += run_rules(View(vname, rows))
    for env in CAPTURES:
        cap, err = load_capture(env)
        if err:
            results.append(("env:" + env, "K8.2x capture", "error", err))
            continue
        notes.append("env:%s = %s + %s (%d LPs)" % (env, cap["raw"], cap["topo"], len(cap["lps"])))
        for lp in sorted(cap["lps"]):
            for (vn, rid, st, txt) in run_rules(View("env:%s/lp%d" % (env, lp), cap["lps"][lp])):
                results.append((vn, rid, st, txt))
        results += env_rules(env, cap, [(vn, prof_rows[vn]) for vn, fn, e in PROFILES if e == env])
    if want_emu:
        for vname, flag, pname in EMULATED:
            ev, err = emulated_view(build, flag, vname)
            if err:
                results.append((vname, "K8.1x emulate", "error", err))
                continue
            results += run_rules(ev)
            if pname:
                results.append(emu_vs_profile(ev, prof_rows[pname], pname))
    return results, notes


def fold(results):
    """per-LP lines of an environment folded into one line per (environment, rule) when every LP has the same status"""
    out, groups = [], {}
    for vn, rid, st, txt in results:
        m = re.match(r"^(env:[a-z_]+)/lp(\d+)$", vn)
        if m:
            groups.setdefault((m.group(1), rid), []).append((int(m.group(2)), st, txt))
        else:
            out.append((vn, rid, st, txt))
    for (env, rid), items in groups.items():
        sts = sorted(set(s for _, s, _ in items))
        if len(sts) == 1:
            out.append((env + "/each_lp", rid, sts[0], "%d LPs; LP %d: %s" % (len(items), items[0][0], items[0][2])))
        else:
            for lp, st, txt in items:
                out.append(("%s/lp%d" % (env, lp), rid, st, txt))
    return out


def load_expected():
    exp = {}
    for n, line in enumerate(open(EXPECTED, encoding="utf-8"), 1):
        line = line.rstrip("\r\n")
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) < 3 or f[2] not in (PASS, INCONS, NA):
            raise SystemExit("[cpuid_consistency] FAIL: %s line %d: <view> TAB <rule> TAB pass|inconsistent|n/a [TAB note]" % (EXPECTED, n))
        exp[(f[0], f[1])] = (f[2], f[3] if len(f) > 3 else "")
    return exp


def selftest():
    """negative controls: a mutated view must turn the rule's result to inconsistent"""
    base = load_profile(os.path.join(DATA, "cpuid_i5-13600k_bare_host.txt"))
    def mut(fn):
        rows = dict(base)
        fn(rows)
        return View("selftest", rows)
    def setreg(rows, k, reg, val):
        r = list(rows[k]); r[reg] = val; rows[k] = tuple(r)
    cases = [
        ("K8.01 vendor", lambda r: setreg(r, (0, 0), 1, 0x41414141)),
        ("K8.02 version", lambda r: setreg(r, (1, 0), 0, r[(1, 0)][0] | 0x10000000)),
        ("K8.03 apic_id", lambda r: setreg(r, (1, 0), 1, (r[(1, 0)][1] & 0x00FFFFFF) | 0x05000000)),
        ("K8.03 apic_id", lambda r: setreg(r, (0x1F, 0), 3, r[(0x1F, 0)][3] + 1)),
        ("K8.04 leaf0B", lambda r: setreg(r, (0xB, 1), 3, r[(0xB, 1)][3] + 2)),
        ("K8.05 leaf1F", lambda r: setreg(r, (0x1F, 2), 0, 1)),
        ("K8.06 leaf04", lambda r: setreg(r, (4, 1), 0, r[(4, 1)][0] ^ (1 << 26))),
        ("K8.07 leaf02", lambda r: setreg(r, (2, 0), 0, (r[(2, 0)][0] & ~0xFF) | 0x02)),
        ("K8.07 leaf02", lambda r: [r.pop(k) for k in list(r) if k[0] == 0x18 and k[1] > 0] and setreg(r, (2, 0), 3, 0xFE) or setreg(r, (2, 0), 3, 0xFE)),
        ("K8.08 leaf1A", lambda r: setreg(r, (0x1A, 0), 0, 0)),
        ("K8.09 brand", lambda r: setreg(r, (0x80000004, 0), 3, 0x41414141)),
        ("K8.10 xsave", lambda r: setreg(r, (0xD, 0), 2, 0x200)),
        ("K8.11 l2_80000006", lambda r: setreg(r, (0x80000006, 0), 2, r[(0x80000006, 0)][2] ^ 0x04000000)),
    ]
    bad = 0
    for rid, fn in cases:
        v = mut(fn)
        st = [s for (vn, r, s, t) in run_rules(v) if r == rid][0]
        ok = st == INCONS
        bad += not ok
        print("[cpuid_consistency] selftest %-20s mutated -> %s %s" % (rid, st, "ok" if ok else "NOT DETECTED"))
    # the unmutated profile passes those rules
    clean = {r: s for (vn, r, s, t) in run_rules(View("clean", base))}
    for rid in sorted(set(c[0] for c in cases)):
        ok = clean[rid] == PASS
        bad += not ok
        print("[cpuid_consistency] selftest %-20s unmutated -> %s %s" % (rid, clean[rid], "ok" if ok else "WRONG"))
    # emulated-only rules on a synthetic complete view: an out-of-range leaf that does not echo the top leaf
    rows = {(0, 0): (2, 0x756E6547, 0x6C65746E, 0x49656E69), (1, 0): (0x906A0, 0, 0, 0), (2, 0): (1, 0, 0, 0), (2, 1): (1, 0, 0, 0),
            (3, 0): (5, 6, 7, 8), (0x80000000, 0): (0x80000000, 0, 0, 0), (0x80000001, 0): (2, 0, 0, 0), (0x40000000, 0): (1, 0, 0, 0)}
    v = View("selftest_emu", rows, complete=True)
    st = [s for (vn, r, s, t) in run_rules(v) if r == "K8.12 out_of_range"][0]
    bad += st != INCONS
    print("[cpuid_consistency] selftest K8.12 out_of_range  synthetic -> %s %s" % (st, "ok" if st == INCONS else "NOT DETECTED"))
    v.tsc_aux = (1, 1, 2, 1)
    st = [s for (vn, r, s, t) in run_rules(v) if r == "K8.14 tsc_aux"][0]
    bad += st != INCONS
    print("[cpuid_consistency] selftest K8.14 tsc_aux       synthetic -> %s %s" % (st, "ok" if st == INCONS else "NOT DETECTED"))
    print("[cpuid_consistency] selftest: %d problem(s)" % bad)
    return bad


def main():
    a = sys.argv[1:]
    if "--selftest" in a:
        return 1 if selftest() else 0
    build = None
    if "--build" in a:
        build = a[a.index("--build") + 1]
    if build is None:
        print(__doc__)
        return 2
    results, notes = collect(build)
    results = fold(results)
    for n in notes:
        print("[cpuid_consistency] " + n)
    if "--list" in a:
        for vn, rid, st, txt in results:
            print("%s\t%s\t%s\t%s" % (vn, rid, st, txt))
        return 0
    exp = load_expected()
    bad = 0
    seen = set()
    counts = {}
    for vn, rid, st, txt in results:
        key = (vn, rid)
        seen.add(key)
        counts[st] = counts.get(st, 0) + 1
        e = exp.get(key)
        if st == "error":
            bad += 1
            print("[cpuid_consistency] ERROR  %s %s: %s" % (vn, rid, txt))
        elif e is None:
            bad += 1
            print("[cpuid_consistency] NEW    %s %s: %s - %s (not in %s)" % (vn, rid, st, txt, os.path.basename(EXPECTED)))
        elif e[0] != st:
            bad += 1
            print("[cpuid_consistency] DIFF   %s %s: %s, expected %s - %s" % (vn, rid, st, e[0], txt))
        else:
            print("[cpuid_consistency] %-6s %s %s: %s%s" % ("ok", vn, rid, st, (" (recorded: " + e[1] + ")") if st == INCONS else "") + " - " + txt)
    for key in exp:
        if key not in seen:
            bad += 1
            print("[cpuid_consistency] MISSING %s %s (expected %s, not produced)" % (key[0], key[1], exp[key][0]))
    print("[cpuid_consistency] results: %d (%s); expectation lines %d; problems %d" % (len(results), ", ".join("%s %d" % kv for kv in sorted(counts.items())), len(exp), bad))
    if bad:
        print("[cpuid_consistency] FAIL")
        return 1
    print("[cpuid_consistency] OK: every result as expected (captured inconsistencies recorded, never edited)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
