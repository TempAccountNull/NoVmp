"""bare_host_cpuid.py -- the bare-host CPUID profiles of the i5-13600K, derived from captures (U1492).

User requirement 2026-10-15: the emulator emulates EITHER the VMware guest (emu-alltest --guest, profile
Emulator\\data\\cpuid_i5-13600k.txt) OR the real bare host (emu-alltest --host [--host-core P|E]), each with
every value taken from a capture of that environment. This tool writes the two bare-host profiles from the
bare-host captures ONLY (never from the VMware-guest capture):

    Emulator\\data\\cpuid_i5-13600k_bare_host.txt         P-core (CPUID.1AH core type 40H), --host / --host-core P
    Emulator\\data\\cpuid_i5-13600k_bare_host_ecore.txt   E-core (CPUID.1AH core type 20H), --host --host-core E

  python -I bare_host_cpuid.py --write     regenerate both files (after a new capture; review the diff)
  python -I bare_host_cpuid.py --check     re-derive both files from the captures and compare them byte for byte
                                           (line endings normalised: git autocrlf); run all evidence checks
  python -I bare_host_cpuid.py --cli EXE   command-line checks of emu-alltest EXE (--host / --guest / --cpuid /
                                           --host-core: selection, log lines, exclusions; exit status)
  python -I bare_host_cpuid.py --diff      print the per-core-type differences (P vs E, and per-LP fields)

Captures (outside the repository) are found under the capture root: $NOVMP_CAPTURE_ROOT if set, else the
first ancestor folder of the repository root that holds capture\\host\\output\\<the capture below>
(found by the rest of its name after the machine name, shown as <bare-host>; U1610). A missing
capture is an error (no fallback, no stub result).

Evidence checks (--check / --write), each a hard failure:
  * every LP of one core type (CPUID.1AH:EAX[31:24]) reports the same rows and values, except the
    per-LP fields 01H EBX[31:24] (initial APIC ID), 06H EDX[31:16] (HFI row index), 0BH / 1FH EDX (x2APIC ID);
  * the P-core file's data rows = the capture tool's own profile file (LP 0) data rows;
  * every row of the raw capture of the representative LP (arch, reserved and probe rows, the hypervisor
    range) equals what the emulator returns from the profile (x86_cpuid_profile semantics, U68: an unlisted
    (leaf, sub-leaf) reads 0, 0BH / 1FH echo ECX[7:0] with the x2APIC ID, a leaf above the maximum returns the
    highest basic leaf), so leaving the probe rows out changes no CPUID result;
  * every row of the smmid raw dump (LP 0, sub-leaves 0..63) equals what the emulator returns from the
    P-core file; the smmid per-LP table (01H EBX, 1AH EAX, x2APIC ID of all 20 LPs) agrees with the capture;
  * the microcode revision 129H: the capture's registry record on every LP and the smmid record.
"""
import os
import subprocess
import sys

ENV = "NOVMP_CAPTURE_ROOT"
# The capture folders are named <machine>_<date>_<time>[_<environment>]. They are located by their parent
# folder and the unique rest of the name (U1610, privacy: the machine name is not written into the
# repository); HOST_DIR / SMMID_DIR are the display forms with the label <bare-host>.
HOST_LABEL = "<bare-host>"
HOST_PARENT = os.path.join("host", "output")
HOST_SUFFIX = "_20261010_133354"
HOST_DIR = os.path.join(HOST_PARENT, HOST_LABEL + HOST_SUFFIX)
HOST_STEM = "cpuid_GenuineIntel_06-B7-1_bare_host"
SMMID_PARENT = os.path.join("smmid", "output")
SMMID_SUFFIX = "_20261015_002318_native-host"
SMMID_DIR = os.path.join(SMMID_PARENT, HOST_LABEL + SMMID_SUFFIX)
SMMID_STEM = "smmid_native-host_host"

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "Emulator", "data")
VARIANTS = [  # key, core type 1AH:EAX[31:24], file, label
    ("P", 0x40, "cpuid_i5-13600k_bare_host.txt", "P-core (CPUID.1AH:EAX[31:24] = 40H, Core)"),
    ("E", 0x20, "cpuid_i5-13600k_bare_host_ecore.txt", "E-core (CPUID.1AH:EAX[31:24] = 20H, Atom)"),
]
UCODE_REV = 0x129
PLATFORM_ID = 1
# U68 x86_cpuid_leaf_has_subleaves (+ 1EH, U721)
SUBLEAF_LEAVES = {0x4, 0x7, 0xB, 0xD, 0xF, 0x10, 0x12, 0x14, 0x17, 0x18, 0x1B, 0x1D, 0x1E, 0x1F, 0x20, 0x23, 0x24, 0x29}
# per-logical-processor fields: (leaf, sub or None = every sub-leaf) -> [mask EAX, EBX, ECX, EDX]
PER_LP = {(0x1, 0): (0, 0xFF000000, 0, 0), (0x6, 0): (0, 0, 0, 0xFFFF0000),
          (0xB, None): (0, 0, 0, 0xFFFFFFFF), (0x1F, None): (0, 0, 0, 0xFFFFFFFF)}
REGS = ("EAX", "EBX", "ECX", "EDX")


def fail(msg):
    sys.exit("[bare_host_cpuid] FAIL: " + msg)


def say(msg):
    print("[bare_host_cpuid] " + msg)


RESOLVED = {}  # real capture folder -> display form (label instead of the machine name)


def find_dir(root, parent, suffix, display):
    """The one folder under root\\parent whose name ends with suffix; None if there is none."""
    d = os.path.join(root, parent)
    if not os.path.isdir(d):
        return None
    hits = sorted(x for x in os.listdir(d) if x.endswith(suffix) and os.path.isdir(os.path.join(d, x)))
    if len(hits) > 1:
        fail("%d folders under %s end with %s (expected exactly one)" % (len(hits), d, suffix))
    if not hits:
        return None
    real = os.path.join(d, hits[0])
    RESOLVED[real] = os.path.join(root, display)
    return real


def shown(path):
    """path for output: a resolved capture folder is shown with the label, not the machine name."""
    for real, disp in RESOLVED.items():
        if path.startswith(real):
            return disp + path[len(real):]
    return path


def capture_root():
    r = os.environ.get(ENV, "").strip()
    if r:
        if find_dir(r, HOST_PARENT, HOST_SUFFIX, HOST_DIR) is None:
            fail("%s=%s has no %s" % (ENV, r, HOST_DIR))
        return os.path.normpath(r)
    p = ROOT
    while True:
        c = os.path.join(p, "capture")
        if find_dir(c, HOST_PARENT, HOST_SUFFIX, HOST_DIR) is not None:
            return c
        q = os.path.dirname(p)
        if q == p:
            fail("no capture root: set %s to the folder that holds %s and %s (searched the ancestors of %s for capture\\%s)"
                 % (ENV, HOST_DIR, SMMID_DIR, ROOT, HOST_DIR))
        p = q


def need(path):
    if not os.path.isfile(path):
        fail("missing capture file " + shown(path))
    return path


def per_lp_mask(leaf, sub):
    m = PER_LP.get((leaf, sub)) or PER_LP.get((leaf, None))
    return m or (0, 0, 0, 0)


# ------------------------------------------------------------------------------------------- inputs
def read_raw(path):
    """cpuid_capture 1.6 raw TSV -> {lp: {(leaf, sub): (regs, kind)}} (call 1 only; 02H runs once here)"""
    lps = {}
    header = []
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\r\n")
        if line.startswith("#"):
            header.append(line)
            continue
        if not line or line.startswith("lp\t"):
            continue
        f = line.split("\t")
        if len(f) < 14:
            fail("raw row with %d fields: %s" % (len(f), line))
        lp, pinned, off = int(f[0]), int(f[3]), int(f[4])
        if pinned != 1 or off != 0:
            fail("LP %d not pinned or off-processor samples: %s" % (lp, line))
        if f[7] != "1":
            fail("more than one call of a leaf: %s" % line)
        key = (int(f[5], 16), int(f[6], 16))
        lps.setdefault(lp, {})[key] = (tuple(int(x, 16) for x in f[8:12]), f[12])
    return lps, header


def read_tool_profile(path):
    rows = []
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#"):
            rows.append(line)
    return rows


def read_smmid(path):
    rows = {}
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\r\n")
        if not line or line.startswith("#") or line.startswith("leaf\t"):
            continue
        f = line.split("\t")
        rows[(int(f[0], 16), int(f[1], 16))] = tuple(int(x, 16) for x in f[2:6])
    return rows


def read_tsv(path):
    lines = [l.rstrip("\r\n") for l in open(path, encoding="utf-8") if l.strip() and not l.startswith("#")]
    cols = lines[0].split("\t")
    return [dict(zip(cols, l.split("\t"))) for l in lines[1:]]


# ------------------------------------------------------------------------------------------- model
def emu_lookup(prof, leaf, sub):
    """what the emulator returns for CPUID(leaf, sub) from profile 'prof' (cpu.c x86_cpuid_profile, U68);
    CR4-dependent bits (OSXSAVE, OSPKE), 0DH.0 EBX (U125) and the APIC overrides (U960) not applied"""
    max_basic = prof[(0, 0)][0]
    max_ext = prof.get((0x80000000, 0), (0,))[0]
    if (leaf > max_ext) if leaf >= 0x80000000 else (leaf > max_basic):
        leaf = max_basic
    if leaf not in SUBLEAF_LEAVES:
        sub = 0
    e = prof.get((leaf, sub))
    if e:
        return e
    if leaf in (0xB, 0x1F):
        s0 = prof.get((leaf, 0))
        return (0, 0, sub & 0xFF, s0[3] if s0 else 0)
    return (0, 0, 0, 0)


def h(v):
    return "%08X" % v


def key_str(k):
    return "%sH.%X" % (("%02X" % k[0]) if k[0] < 0x100 else ("%08X" % k[0]), k[1])


# ------------------------------------------------------------------------------------------- derivation
class Evidence:
    pass


def gather():
    cr = capture_root()
    hd = find_dir(cr, HOST_PARENT, HOST_SUFFIX, HOST_DIR)
    sd = find_dir(cr, SMMID_PARENT, SMMID_SUFFIX, SMMID_DIR)
    if hd is None or sd is None:
        fail("capture root %s lacks %s" % (cr, HOST_DIR if hd is None else SMMID_DIR))
    ev = Evidence()
    ev.capture_root = cr
    ev.raw_path = need(os.path.join(hd, HOST_STEM + "_raw.tsv"))
    ev.tool_profile = need(os.path.join(hd, HOST_STEM + ".txt"))
    ev.system = need(os.path.join(hd, HOST_STEM + "_system.txt"))
    ev.smmid_raw = need(os.path.join(sd, SMMID_STEM + "_cpuid_raw.tsv"))
    ev.smmid_lps = need(os.path.join(sd, SMMID_STEM + "_lps.tsv"))
    ev.smmid_fields = need(os.path.join(sd, SMMID_STEM + "_fields.tsv"))
    ev.lps, ev.raw_header = read_raw(ev.raw_path)
    hdr = "\n".join(ev.raw_header)
    for want in ("# tool: cpuid_capture 1.6", "(--env): bare_host", "hypervisor-present bit CPUID.1:ECX[31] = 0",
                 "40000000H range not valid"):
        if want not in hdr:
            fail("raw capture header lacks %r" % want)
    if sorted(ev.lps) != list(range(20)):
        fail("raw capture: LPs %s (expected 0..19)" % sorted(ev.lps))
    # core type per LP (1AH valid only with 07H.0:EDX[15] hybrid, SDM 092 Vol2A CPUID)
    ev.core_type = {}
    for lp, rows in ev.lps.items():
        if not (rows[(7, 0)][0][3] >> 15) & 1:
            fail("LP %d: 07H.0:EDX[15] hybrid = 0" % lp)
        ev.core_type[lp] = rows[(0x1A, 0)][0][0] >> 24
    ev.rep = {}
    for key, ct, _, _ in VARIANTS:
        members = sorted(lp for lp, c in ev.core_type.items() if c == ct)
        if not members:
            fail("no LP with core type %02XH" % ct)
        ev.rep[key] = (members[0], members)
    if sum(len(m) for _, m in ev.rep.values()) != 20:
        fail("LPs of an unknown core type: %s" % ev.core_type)
    # microcode revision: the capture's registry record, every LP
    ucode_lines = [l for l in open(ev.system, encoding="utf-8") if l.startswith("CentralProcessor\\")]
    if len(ucode_lines) != 20 or any("(Intel bytes 7:4: 0x%x)" % UCODE_REV not in l for l in ucode_lines):
        fail("_system.txt: not 20 CentralProcessor lines with revision 0x%x" % UCODE_REV)
    fields = {r["field"]: r["value"] for r in read_tsv(ev.smmid_fields)}
    if fields.get("microcode.revision") != "%08X" % UCODE_REV or fields.get("environment.declared") != "native-host":
        fail("smmid fields: microcode.revision %s, environment.declared %s" % (fields.get("microcode.revision"),
                                                                              fields.get("environment.declared")))
    ev.fields = fields
    return ev


def profile_rows(ev, key):
    """the profile of core type 'key': the representative LP's arch + reserved rows, after the checks"""
    rep, members = ev.rep[key]
    rows = ev.lps[rep]
    # 1. every LP of the core type: same rows, same values outside the per-LP fields
    for lp in members:
        other = ev.lps[lp]
        if set(other) != set(rows):
            fail("core type %s: LP %d rows %s differ from LP %d" % (key, lp, sorted(set(other) ^ set(rows)), rep))
        for k, (regs, kind) in rows.items():
            m = per_lp_mask(*k)
            o = other[k][0]
            for r in range(4):
                if (regs[r] & ~m[r]) != (o[r] & ~m[r]):
                    fail("core type %s: LP %d %s %s = %s, LP %d %s (not a per-LP field)"
                         % (key, lp, key_str(k), REGS[r], h(o[r]), rep, h(regs[r])))
    prof = {k: regs for k, (regs, kind) in rows.items() if kind in ("arch", "reserved")}
    kinds = {kind for _, kind in rows.values()}
    if not kinds <= {"arch", "reserved", "probe"}:
        fail("unknown row kinds %s" % kinds)
    # 2. the rows left out (probes, hypervisor range) read the same through the emulator's lookup
    for k, (regs, kind) in rows.items():
        got = emu_lookup(prof, *k)
        if got != regs:
            fail("LP %d %s (%s): capture %s, emulator from the profile %s" % (rep, key_str(k), kind,
                                                                            " ".join(map(h, regs)), " ".join(map(h, got))))
    return prof


def cross_check_smmid(ev, pprof):
    smm = read_smmid(ev.smmid_raw)
    for k, regs in sorted(smm.items()):
        got = emu_lookup(pprof, *k)
        if got != regs:
            fail("smmid %s: %s, emulator from the P-core profile %s" % (key_str(k), " ".join(map(h, regs)), " ".join(map(h, got))))
    lps = read_tsv(ev.smmid_lps)
    if len(lps) != 20:
        fail("smmid lps.tsv: %d LPs" % len(lps))
    for r in lps:
        lp = int(r["lp"])
        cap = ev.lps[lp]
        x2 = cap[(0x1F, 0)][0][3]
        if (int(r["cpuid01_ebx"], 16), int(r["cpuid1a_eax"], 16), int(r["x2apic"], 16)) != (cap[(1, 0)][0][1], cap[(0x1A, 0)][0][0], x2):
            fail("smmid lps.tsv LP %d disagrees with the capture" % lp)
        if r["type"] != ("P" if ev.core_type[lp] == 0x40 else "E"):
            fail("smmid lps.tsv LP %d type %s" % (lp, r["type"]))
    return len(smm)


def tool_profile_check(ev, pprof):
    want = read_tool_profile(ev.tool_profile)
    got = [row_text(k, v) for k, v in sorted(pprof.items())]
    if want != got:
        fail("P-core rows differ from the capture tool's own profile %s" % shown(ev.tool_profile))


def row_text(k, v):
    return "%08X %02X %08X %08X %08X %08X" % (k[0], k[1], v[0], v[1], v[2], v[3])


def core_diffs(pp, ep):
    out = []
    for k in sorted(set(pp) | set(ep)):
        if k not in ep:
            out.append("%s only P (%s)" % (key_str(k), " ".join(map(h, pp[k]))))
        elif k not in pp:
            out.append("%s only E (%s)" % (key_str(k), " ".join(map(h, ep[k]))))
        else:
            for r in range(4):
                if pp[k][r] != ep[k][r]:
                    out.append("%s %s P %s E %s" % (key_str(k), REGS[r], h(pp[k][r]), h(ep[k][r])))
    return out


def x2apic_list(ev, members):
    return " ".join("%d:%02X" % (lp, ev.lps[lp][(0x1F, 0)][0][3]) for lp in members)


def render(ev, key, prof, smm_n, diffs):
    _, ct, fname, label = [v for v in VARIANTS if v[0] == key][0]
    rep, members = ev.rep[key]
    other = "E" if key == "P" else "P"
    rows = ev.lps[rep]
    x2 = rows[(0x1F, 0)][0][3]
    f = ev.fields
    L = []
    a = L.append
    a("# ENVIRONMENT: BARE HOST - the physical Intel Core i5-13600K (no hypervisor), %s." % label)
    a("# NOT the VMware guest (that is cpuid_i5-13600k.txt, --guest); the VMware-guest capture is not a source of this file.")
    a("# GENERATED by Emulator/tools/bare_host_cpuid.py --write (U1492) - do not edit by hand; the suite bp2_profile_check")
    a("# (bare_host_cpuid.py --check) re-derives this file from the captures and compares it byte for byte (line endings normalised).")
    a("# Selected by emu-alltest --host%s; --cpuid FILE overrides it." % ("" if key == "P" else " --host-core E"))
    a("# Machine %s: %s %s, BIOS %s %s; %s." % (HOST_LABEL,
        f.get("firmware.registry.BaseBoardManufacturer", "?"), f.get("firmware.registry.BaseBoardProduct", "?"),
        f.get("firmware.registry.BIOSVersion", "?"), f.get("firmware.registry.BIOSReleaseDate", "?"), f.get("os.version", "?")))
    a("# Source: capture\\%s\\%s_raw.tsv - cpuid_capture 1.6 (standalone, 2026-10-10), captured 2026-10-10T18:33:54Z," % (HOST_DIR, HOST_STEM))
    a("#   environment declared bare_host; hypervisor-present bit CPUID.1:ECX[31] = 0, 40000000H range not valid; all 20 logical")
    a("#   processors pinned, 0 off-processor samples. Data rows = logical processor %d (x2APIC ID %02XH), the lowest-numbered LP of" % (rep, x2))
    a("#   core type %02XH; the %d LPs of this core type (%s) report identical rows except the per-LP fields below." % (ct, len(members), "%d-%d" % (members[0], members[-1])))
    a("# Cross-check: capture\\%s\\%s_cpuid_raw.tsv - smmid_probe 1.0 (2026-10-14), 2026-10-15T05:23:18Z, environment" % (SMMID_DIR, SMMID_STEM))
    if key == "P":
        a("#   declared native-host, pinned to LP 0: all %d rows (sub-leaves 0..63) equal what the emulator returns from this file;" % smm_n)
    else:
        a("#   declared native-host, pinned to LP 0 (a P-core): it checks the P-core file only (%d rows, all equal);" % smm_n)
    a("#   %s_lps.tsv: 01H EBX, 1AH EAX and the x2APIC ID of all 20 LPs equal the capture." % SMMID_STEM)
    a("# Row selection, no value changed: the capture's architecturally enumerated rows (kind arch) and the raw sub-leaf 0 of the")
    a("#   reserved leaves (kind reserved) of LP %d%s. The raw probes of leaves" % (rep, " - the same rows as the capture tool's own profile file %s.txt" % HOST_STEM if key == "P" else ""))
    a("#   the processor does not enumerate (0FH 10H 12H 17H 1DH 1EH) and the hypervisor range are left out: the emulator returns")
    a("#   0 for an unlisted (leaf, sub-leaf), echoes ECX[7:0] with the x2APIC ID for 0BH / 1FH and the highest basic leaf above")
    a("#   the maximum (U68), which reproduces every one of those captured rows (checked).")
    a("# Per-LP fields (the format models one logical processor): 01H EBX[31:24] initial APIC ID, 0BH / 1FH EDX x2APIC ID,")
    a("#   06H EDX[31:16] (HFI table row index): here LP %d's values (x2APIC ID %02XH, 06H EDX[31:16] = %XH). x2APIC IDs from 1FH" % (rep, x2, rows[(6, 0)][0][3] >> 16))
    a("#   (SMT shift 1, package shift 7): P-core k = 0..5, thread t: 8k + t; E-core j = 0..7: 30H + 2j; initial APIC ID =")
    a("#   x2APIC ID[7:0]. Measured %s: %s." % ("P-cores (LP:x2APIC)" if key == "P" else "E-cores (LP:x2APIC)", x2apic_list(ev, members)))
    a("#   Another LP: UC_CTL_X86_APIC_ID (U961) replaces 01H EBX[31:24] and 0BH / 1FH EDX.")
    a("# P-core vs E-core differences (leaf H.sub-leaf register; the %s-core file has the other value):" % other)
    for i in range(0, len(diffs), 3):
        a("#   " + "; ".join(diffs[i:i + 3]))
    a("# Recomputed by the emulator whatever the file says: 01H ECX[27] OSXSAVE and 07H.0 ECX[4] OSPKE (CR4), 0DH.0 EBX (XCR0, U125).")
    a("# Microcode identity (\"#@ microcode REV platform-id N\" -> UC_CTL_X86_MICROCODE in emu-alltest, U1473; other readers skip it):")
    a("#   revision 129H = the capture's %s_system.txt \"Update Revision ... (Intel bytes 7:4: 0x129)\" on all 20" % HOST_STEM)
    a("#   processors (BIOS H.D0) and smmid microcode.revision 00000129 (registry); CPU-Z 3.01 host report E:\\CPU Info\\cpuz-host\\CPU-info.txt")
    a("#   \"Microcode Revision 0x129\". Platform ID 1: the same CPU-Z report, \"MSR 0x00000017 0x00040000 0x00000000\" (IA32_PLATFORM_ID")
    a("#   EDX 00040000H, bits 52:50 = 001b) on every processor and \"Package (platform ID) ... (0x1)\". Signature 000B0671 (01H EAX below).")
    a("# Format: leaf subleaf eax ebx ecx edx (hex), one row per leaf/sub-leaf; lines starting with # are ignored.")
    a("#@ microcode %08X platform-id %d" % (UCODE_REV, PLATFORM_ID))
    for k in sorted(prof):
        a(row_text(k, prof[k]))
    return "\r\n".join(L) + "\r\n"


def build():
    ev = gather()
    profs = {key: profile_rows(ev, key) for key, _, _, _ in VARIANTS}
    smm_n = cross_check_smmid(ev, profs["P"])
    tool_profile_check(ev, profs["P"])
    diffs = core_diffs(profs["P"], profs["E"])
    out = {}
    for key, _, fname, _ in VARIANTS:
        out[fname] = render(ev, key, profs[key], smm_n, diffs)
    return ev, profs, diffs, out, smm_n


def norm(b):
    return b.replace(b"\r\n", b"\n")


def cmd_write():
    ev, profs, diffs, out, smm_n = build()
    for fname, text in out.items():
        p = os.path.join(DATA, fname)
        with open(p, "wb") as fh:
            fh.write(text.encode("ascii"))
        say("wrote %s (%d rows)" % (p, sum(1 for l in text.splitlines() if l and not l.startswith("#"))))
    return 0


def cmd_check():
    ev, profs, diffs, out, smm_n = build()
    say("capture root %s" % ev.capture_root)
    for key, _, _, _ in VARIANTS:
        rep, members = ev.rep[key]
        say("core type %s: LPs %s, representative LP %d, %d rows; per-LP fields only differ: ok" % (key, members, rep, len(profs[key])))
    say("captured probe / hypervisor rows reproduced by the emulator lookup: ok")
    say("P-core rows = capture tool profile %s: ok" % shown(ev.tool_profile))
    say("smmid raw dump: %d rows equal the emulator lookup of the P-core profile; smmid lps.tsv (20 LPs) agrees: ok" % smm_n)
    say("microcode 129H: capture registry (20 LPs) and smmid: ok")
    bad = 0
    for fname, text in out.items():
        p = os.path.join(DATA, fname)
        if not os.path.isfile(p):
            say("MISSING %s" % p)
            bad += 1
            continue
        have = norm(open(p, "rb").read())
        want = norm(text.encode("ascii"))
        if have != want:
            hl, wl = have.split(b"\n"), want.split(b"\n")
            i = next((i for i in range(min(len(hl), len(wl))) if hl[i] != wl[i]), min(len(hl), len(wl)))
            say("DIFFERS %s at line %d:\n  file:    %r\n  derived: %r" % (p, i + 1, hl[i] if i < len(hl) else b"<eof>", wl[i] if i < len(wl) else b"<eof>"))
            bad += 1
        else:
            say("%s: byte-identical to the derivation (%d bytes, EOL normalised)" % (fname, len(have)))
    if bad:
        fail("%d profile file(s) not reproducible from the captures (bare_host_cpuid.py --write, then review)" % bad)
    say("OK")
    return 0


def cmd_diff():
    ev, profs, diffs, out, smm_n = build()
    for key, _, _, _ in VARIANTS:
        rep, members = ev.rep[key]
        say("core type %s: LPs %s (representative %d); x2APIC %s" % (key, members, rep, x2apic_list(ev, members)))
        say("  06H EDX[31:16] per LP: %s" % " ".join("%d:%X" % (lp, ev.lps[lp][(6, 0)][0][3] >> 16) for lp in members))
    for d in diffs:
        say("  " + d)
    return 0


# ------------------------------------------------------------------------------------------- CLI checks
def cmd_cli(exe):
    if not os.path.isfile(exe):
        fail("no emu-alltest at " + exe)
    guest = os.path.join(DATA, "cpuid_i5-13600k.txt")
    pfile = os.path.join(DATA, VARIANTS[0][2])
    efile = os.path.join(DATA, VARIANTS[1][2])
    cases = {k: os.path.join(DATA, "cases_bp2_%s.txt" % k) for k in ("host_p", "host_e", "guest")}
    for p in list(cases.values()) + [guest, pfile, efile]:
        if not os.path.isfile(p):
            fail("missing " + p)
    n = 0

    def run(args, rc, must=(), mustnot=()):
        nonlocal n
        r = subprocess.run([exe] + args, capture_output=True, text=True, timeout=600)
        out = r.stdout + r.stderr
        shown = " ".join(os.path.basename(a) if os.path.isabs(a) else a for a in args)
        if r.returncode != rc:
            fail("emu-alltest %s: exit %d, expected %d\n%s" % (shown, r.returncode, rc, out))
        for m in must:
            if m not in out:
                fail("emu-alltest %s: output lacks %r\n%s" % (shown, m, out))
        for m in mustnot:
            if m in out:
                fail("emu-alltest %s: output has %r\n%s" % (shown, m, out))
        n += 1
        say("ok: emu-alltest %s -> exit %d" % (shown, rc))
        return out

    c = ["--cases", cases["host_p"], "--expect-only"]
    excl = "--host, --guest, --vm and --AMD exclude each other"
    # mutually exclusive platform switches, --host-core only with --host, bad core letters
    run(c + ["--host", "--guest"], 2, [excl])
    run(c + ["--guest", "--host"], 2, [excl])
    run(c + ["--host", "--vm"], 2, [excl])
    run(c + ["--host", "--AMD"], 2, [excl])
    run(c + ["--host-core", "E"], 2, ["--host-core needs --host"])
    run(c + ["--guest", "--host-core", "P"], 2, ["--host-core needs --host"])
    run(c + ["--host", "--host-core", "X"], 2, ["--host-core P|E"])
    run(c + ["--host", "--host-core", "P", "--host-core", "E"], 2, ["--host-core given twice"])
    # selection and log lines (the values themselves: the expected-value suites bp2_host_p / _e / bp2_guest)
    pl = "cpuid profile: %s (--host default: bare host, P-core)" % pfile
    el = "cpuid profile: %s (--host --host-core E default: bare host, E-core)" % efile
    gl = "cpuid profile: %s (--guest default: VMware guest)" % guest
    run(c + ["--host"], 0, [pl, "cpuid core type: P (CPUID.1AH:EAX[31:24] = 40H Core", "microcode: revision 129h, platform ID 1 (profile)"])
    run(c + ["--host", "--host-core", "p"], 0, [pl])
    run(["--cases", cases["host_e"], "--expect-only", "--host-core", "e", "--host"], 0,
        [el, "cpuid core type: E (CPUID.1AH:EAX[31:24] = 20H Atom"])
    run(["--cases", cases["guest"], "--expect-only", "--guest"], 0, [gl, "cpuid core type: none (07H.0:EDX[15] hybrid = 0"])
    run(["--cases", cases["guest"], "--expect-only", "--vm"], 0, ["cpuid profile: %s (--vm default: VMware guest)" % guest])
    # --cpuid overrides the platform default (either order) and says so
    ov = "cpuid profile: %s (--cpuid, overrides the --host default %s)" % (guest, pfile)
    run(["--cases", cases["guest"], "--expect-only", "--host", "--cpuid", guest], 0, [ov])
    run(["--cases", cases["guest"], "--expect-only", "--cpuid", guest, "--host"], 0, [ov])
    # negative controls: the wrong environment's expectations fail (the case files tell the profiles apart)
    run(["--cases", cases["guest"], "--expect-only", "--host"], 1, ["differing:"])
    run(["--cases", cases["host_p"], "--expect-only", "--guest"], 1, ["differing:"])
    run(["--cases", cases["host_e"], "--expect-only", "--host"], 1, ["differing:"])
    run(["--cases", cases["host_p"], "--expect-only", "--host", "--host-core", "E"], 1, ["differing:"])
    # no switch: no profile, as before
    run(["--cases", cases["guest"], "--expect-only"], 1, ["cpuid profile: none (no --host/--guest/--vm/--cpuid: Unicorn's CPU model)"])
    say("OK: %d command lines" % n)
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--write":
        return cmd_write()
    if len(argv) == 2 and argv[1] in ("--check", "--selftest"):
        return cmd_check()
    if len(argv) == 2 and argv[1] == "--diff":
        return cmd_diff()
    if len(argv) == 3 and argv[1] == "--cli":
        return cmd_cli(argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
