#!/usr/bin/env python3
"""mkrows.py <Emulator/data> -> prints the APX verified_forms.tsv rows (from apx_cov.py coverage)
and a per-family summary on stderr"""
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apx_cov  # noqa: E402

D = sys.argv[1]

NOT_IMPL = {
    "invept": "VMX (INVEPT)", "invvpid": "VMX (INVVPID)", "invpcid": "INVPCID",
    "tileloaddrs": "AMX-MOVRS (TILELOADDRS)", "tileloaddrst1": "AMX-MOVRS (TILELOADDRST1)",
    "rdmsr": "MSR-IMM (RDMSR imm32)", "wrmsrns": "MSR-IMM (WRMSRNS imm32)",
}
NOT_IMPL_WHERE = {
    "invept": "EVEX map 4 F3 F0", "invvpid": "EVEX map 4 F3 F1", "invpcid": "EVEX map 4 F3 F2",
    "tileloaddrs": "EVEX map 2 F2 4A", "tileloaddrst1": "EVEX map 2 66 4A",
    "rdmsr": "EVEX map 7 F2 F6 /0", "wrmsrns": "EVEX map 7 F3 F6 /0",
}
NOT_IMPL_U = {"invept": "U645", "invvpid": "U645", "invpcid": "U645", "tileloaddrs": "U646",
              "tileloaddrst1": "U646", "rdmsr": "U646", "wrmsrns": "U646"}

U640 = set("add or adc sbb and sub xor inc dec not neg mul imul div idiv rol ror rcl rcr shl sal shr sar "
           "shld shrd popcnt tzcnt lzcnt".split())
U645 = set("movbe adcx adox movrs crc32 movdir64b movdiri aadd aand aor axor enqcmd enqcmds urdmsr uwrmsr "
           "wrssd wrssq wrussd wrussq".split())
BMI = set("andn bextr blsi blsmsk blsr bzhi pdep pext mulx shlx sarx shrx rorx".split())
AMX = set("ldtilecfg sttilecfg tileloadd tileloaddt1 tilestored".split())
CMPX_SAME = {"cmpaexadd": "CMPNBXADD (E3)", "cmpaxadd": "CMPNBEXADD (E7)", "cmpexadd": "CMPZXADD (E4)",
             "cmpnexadd": "CMPNZXADD (E5)", "cmpgexadd": "CMPNLXADD (ED)", "cmpgxadd": "CMPNLEXADD (EF)"}


def group(mn):
    if mn in ("pushp", "popp"):
        return "U614", "Intel APX PUSHP/POPP (REX2 50+r / 58+r with REX2.W = 1 = PPX hint; R16-R31)", "ref_apx_core.py"
    if mn == "jmpabs":
        return "U644", "Intel APX JMPABS (REX2 M0 = 0 W = 0 A1 imm64)", "ref_apx_map4.py"
    if mn in U640 or re.fullmatch(r"set[a-z]+", mn):
        extra = ""
        if mn == "sal":
            extra = "; SAL = SHL: /4 and /6 both encode it (APX spec 3.1.2.3.1 note 2), /6 counted here"
        if mn == "shl":
            extra = "; /4 counted here (/6 = SAL row)"
        if mn.startswith("set"):
            return "U640", "Intel APX EVEX map 4 SETcc.ZU (F2 40+cc, ND = ZU)", "ref_apx_map4.py"
        return "U640", "Intel APX EVEX map 4 promoted legacy instruction (NDD / NF / ZU forms" + extra + ")", "ref_apx_map4.py"
    if mn.startswith("ccmp") or mn.startswith("ctest"):
        return "U641", "Intel APX %s (EVEX map 4, DFV in P1[6:3], SCC in P2[3:0])" % ("CCMPscc" if mn.startswith("ccmp") else "CTESTscc"), "ref_apx_map4.py"
    if mn.startswith("cfcmov"):
        return "U642", "Intel APX CFCMOVcc (EVEX map 4 40+cc, ND/NF = 00/01/11: no memory access when false)", "ref_apx_map4.py"
    if mn.startswith("cmov"):
        return "U642", "Intel APX CMOVcc NDD (EVEX map 4 40+cc, ND = 1 NF = 0)", "ref_apx_map4.py"
    if mn in ("push2", "push2p", "pop2", "pop2p"):
        return "U643", "Intel APX %s (EVEX map 4 %s, ND = 1, W = %d PPX)" % (
            mn.upper(), "FF /6" if mn.startswith("push") else "8F /0", 1 if mn.endswith("p") else 0), "ref_apx_map4.py"
    if mn in U645:
        return "U640/U645", "Intel APX EVEX map 4 promotion of a legacy map 2/3 instruction", "ref_apx_map4.py"
    if mn.startswith("kmov"):
        return "U646", "Intel APX-promoted KMOV (EVEX map 1 90-93, GPRs R16-R31 / EGPR addresses)", "ref_apx_map4.py --ext"
    if mn in BMI:
        return "U646", "Intel APX VEX-promoted BMI1/BMI2 instruction (EVEX map 2/3, EGPRs%s)" % (
            ", NF" if mn in ("andn", "bextr", "blsi", "blsmsk", "blsr", "bzhi") else ""), "ref_apx_map4.py"
    if re.fullmatch(r"cmpn?[a-z]{1,2}xadd", mn):
        same = CMPX_SAME.get(mn)
        return "U646", "Intel APX VEX-promoted CMPccXADD (EVEX map 2 66 E0+cc, EGPRs%s)" % (
            "; same opcode as " + same if same else ""), "ref_apx_map4.py"
    if mn in AMX:
        return "U646", "Intel APX-promoted AMX memory form (EVEX map 2 49/4B, EGPR addresses)", "ref_apx_map4.py --ext"
    return "?", "?", "?"


FILE_DESC = {
    "cases_apx_core.txt": ('cases_apx_core.txt %d "=>"', "(--apx)"),
    "cases_apx_core_hw.txt": ("cases_apx_core_hw.txt %d hardware pair(s)", ""),
    "cases_apx_map4.txt": ('cases_apx_map4.txt %d "=>"', "(--apx)"),
    "cases_apx_map4_hw.txt": ("cases_apx_map4_hw.txt %d hardware pair(s)", ""),
    "cases_apx_map4_ext.txt": ('cases_apx_map4_ext.txt %d "=>"', "(--apx --avx512 --amx)"),
    "cases_apx_map4_sweep.txt": ('cases_apx_map4_sweep.txt %d "=>!" decode-sweep runs', "(gen_apx_map4_sweep.py, spec == XED)"),
}


def main():
    import io
    import contextlib
    buf = io.StringIO()
    old = sys.argv
    sys.argv = ["apx_cov.py", D]
    with contextlib.redirect_stdout(buf):
        apx_cov.main()
    sys.argv = old
    forms = []
    for ln in buf.getvalue().splitlines():
        if ln.startswith("FORM\t"):
            _, mn, enc, isa, fl, cov = ln.split("\t")
            c = defaultdict(lambda: defaultdict(int))
            if cov != "-":
                for m in re.finditer(r"(\S+?)\{([^}]*)\}", cov):
                    for kv in m.group(2).split(","):
                        k, v = kv.split(":")
                        c[m.group(1)][k] += int(v)
            forms.append((mn, enc, isa, int(fl), c))
    rows = []
    summ = defaultdict(lambda: defaultdict(int))
    lists = defaultdict(list)
    for mn, enc, isa, fl, c in forms:
        fam = isa.split("+")[0]
        if mn in NOT_IMPL and enc == "evex":
            ud = sum(v.get("ud", 0) for v in c.values())
            where = ", ".join("%s %d" % (f, v["ud"]) for f, v in c.items() if v.get("ud"))
            rows.append("%s\t%s\tud\tbase instruction not implemented in this CPU model: %s is #UD with and "
                        "without the APX opt-in (%s, %s; #UD lines: %s)%s" % (
                            mn, enc, NOT_IMPL[mn], NOT_IMPL_WHERE[mn], NOT_IMPL_U[mn], where or "none",
                            "; CPL0 form: gen_instruction_table.py keeps its CPL0 status and ignores this row" if (fl & 1) and mn != "invpcid" else ""))
            summ[fam]["not implemented"] += 1
            lists["not implemented"].append("%s %s" % (mn, enc))
            continue
        val = sum(v.get("value", 0) for f, v in c.items() if f != "cases_apx_map4_sweep.txt")
        hw = sum(v.get("hw", 0) for v in c.values())
        if val == 0 and hw == 0:
            summ[fam]["implemented, not covered"] += 1
            lists["implemented, not covered"].append("%s %s: %s" % (mn, enc, " ".join(
                "%s{%s}" % (f, ",".join("%s:%d" % kv for kv in sorted(v.items()))) for f, v in c.items()) or "no case line"))
            continue
        u, desc, model = group(mn)
        parts = []
        for f in apx_cov.FILES:
            if f not in c:
                continue
            v = c[f]
            n = v.get("hw", 0) if f.endswith("_hw.txt") else (v.get("loose", 0) if "sweep" in f else v.get("value", 0))
            if n:
                fmt, opt = FILE_DESC[f]
                parts.append((fmt % n) + ((" " + opt) if opt else ""))
        fault = sum(v.get("fault", 0) for v in c.values())
        udn = sum(v.get("ud", 0) for v in c.values())
        hwtxt = ("; the hardware pairs run the legacy encoding of the same operation on the i5-13600K and this form on "
                 "Unicorn, whole state equal (CPU == model)") if hw else ""
        extra = []
        if fault:
            extra.append("%d expected-fault case(s) (#GP/#PF/#DE)" % fault)
        if udn:
            extra.append("%d #UD-rule case(s)" % udn)
        note = "%s %s: independent spec model (%s) + %s%s%s; all 0 differing in test.cmd apx; without the UC_CTL_X86_APX opt-in #UD, as on the i5-13600K (no APX; unit tests apx_gating / ax4_gating)" % (
            u, desc, model, ", ".join(parts), (" + " + " + ".join(extra)) if extra else "", hwtxt)
        rows.append("%s\t%s\tsdm\t%s" % (mn, enc, note))
        summ[fam]["sdm"] += 1
    for r in rows:
        print(r)
    tot = defaultdict(int)
    for fam in sorted(summ):
        print("FAM %-20s %s" % (fam, dict(summ[fam])), file=sys.stderr)
        for k, v in summ[fam].items():
            tot[k] += v
    print("TOTAL", dict(tot), sum(tot.values()), file=sys.stderr)
    for k, L in lists.items():
        print("LIST", k, len(L), file=sys.stderr)
        for x in L:
            print("   ", x, file=sys.stderr)


main()
