"""U1481 (agent xinusemodel, docs/xinuse.md): emu-alltest XINUSE-model selection check.

usage: python xinuse_cli_check.py --cli PATH\\emu-alltest.exe

Runs emu-alltest on the expected-value files Emulator\\data\\cases_xu_{pcore,ecore,always}.txt (one per XINUSE
model) with every platform switch / override combination and checks
  * the selected model: exactly the file of that model passes (differing 0), the two others differ (negative
    controls - a wrong model is noticed);
  * the log: "xinuse model: <name> (UC_CTL_X86_XINUSE_MODEL n) - <source>" and "xinuse evidence: ..." with the
    expected source (default / override / fallback) and evidence;
  * the usage errors (--xinuse-model with another value, two platform switches) exit with status 2.
Exit status 0 when every check holds. Only Unicorn runs (--expect-only), except the --host fallback run, which
reads this machine's CPUID.1AH / EfficiencyClass for the evidence line (no snippet runs natively).
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, "..", "data"))
ECORE = os.path.join(DATA, "cpuid_xu_hybrid_ecore.txt")
PCORE = os.path.join(DATA, "cpuid_xu_hybrid_pcore.txt")
GUEST = os.path.join(DATA, "cpuid_i5-13600k.txt")        # U1482: the VMware-guest profile (07H.0:EDX[15] = 0)

# (switches, model file that must pass, model name, source substring, evidence substring)
COMBOS = [
    ([], "pcore", "engine default", "no platform switch: not written", "no CPUID profile"),
    (["--guest"], "pcore", "P-core", "--guest / --vm default: P-core model", "host CPUs not consulted"),
    (["--vm"], "pcore", "P-core", "--guest / --vm default: P-core model", "host CPUs not consulted"),
    # U1482: since U1491 --host loads the bare-host profile (P-core, cpuid_i5-13600k_bare_host.txt; E-core with
    # --host-core E): the source is the profile's core type (choose_xinuse, at_cases.hpp)
    (["--host"], "pcore", "P-core", "--host: the core type of the CPUID profile", "1AH:EAX[31:24] = 40H"),
    (["--host", "--host-core", "E"], "ecore", "E-core", "--host: the core type of the CPUID profile", "1AH:EAX[31:24] = 20H"),
    # U1482: a --cpuid override without hybrid enumeration (the VMware-guest profile): no profile core type and
    # no native runs -> the logged P-core FALLBACK
    (["--host", "--cpuid", GUEST], "pcore", "P-core", "--host: FALLBACK to the P-core model - no verified core-type identification",
     "07H.0:EDX[15] hybrid = 0"),
    (["--host", "--cpuid", PCORE], "pcore", "P-core", "--host: the core type of the CPUID profile", "1AH:EAX[31:24] = 40H"),
    (["--host", "--cpuid", ECORE], "ecore", "E-core", "--host: the core type of the CPUID profile", "1AH:EAX[31:24] = 20H"),
    (["--guest", "--cpuid", ECORE], "pcore", "P-core", "--guest / --vm default", "1AH:EAX[31:24] = 20H"),
    (["--AMD"], "always", "always in use", "--AMD default: AMD behaviour unchanged", "host CPUs not consulted"),
    (["--guest", "--xinuse-model", "e-core"], "ecore", "E-core", "override --xinuse-model e-core", ""),
    (["--host", "--cpuid", ECORE, "--xinuse-model", "p-core"], "pcore", "P-core", "override --xinuse-model p-core", "= 20H"),
    (["--xinuse-model", "e-core"], "ecore", "E-core", "override --xinuse-model e-core", ""),
    (["--AMD", "--xinuse-model", "p-core"], "pcore", "P-core", "override --xinuse-model p-core (applied to --AMD on explicit request)", ""),
]
FILES = ("pcore", "ecore", "always")
SUM = re.compile(r"^expected-value cases: (\d+), differing: (\d+), errors: (\d+)", re.M)


def run(exe, args):
    p = subprocess.run([exe] + args, capture_output=True, text=True, errors="replace")
    return p.returncode, p.stdout + p.stderr


def main():
    if len(sys.argv) != 3 or sys.argv[1] != "--cli":
        print(__doc__)
        return 2
    exe = os.path.abspath(sys.argv[2])
    bad = 0
    checks = 0

    def check(ok, what):
        nonlocal bad, checks
        checks += 1
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            bad += 1

    for sw, want, name, src, ev in COMBOS:
        for f in FILES:
            code, out = run(exe, ["--cases", os.path.join(DATA, "cases_xu_%s.txt" % f), "--expect-only", "--xcr0", "0x7"] + sw)
            m = SUM.search(out)
            if not m:
                check(False, "%s on %s: no summary line (exit %d): %s" % (" ".join(sw) or "(none)", f, code, out[-300:]))
                continue
            n, diff, err = (int(x) for x in m.groups())
            if f == want:
                check(n == 5 and diff == 0 and err == 0 and code == 0,
                      "%-60s %-6s passes (cases %d, differing %d, errors %d, exit %d)" % (" ".join(os.path.basename(a) for a in sw) or "(none)", f, n, diff, err, code))
                lm = [l for l in out.splitlines() if l.startswith("xinuse model: ")]
                le = [l for l in out.splitlines() if l.startswith("xinuse evidence: ")]
                check(len(lm) == 1 and lm[0].startswith("xinuse model: %s (" % name) and src in lm[0],
                      "  log model/source: %s" % (lm[0] if lm else "(missing)"))
                check(len(le) == 1 and ev in le[0], "  log evidence: %s" % (le[0] if le else "(missing)"))
            else:
                check(n == 5 and diff > 0 and err == 0,
                      "%-60s %-6s differs as it must (differing %d of %d, errors %d)" % (" ".join(os.path.basename(a) for a in sw) or "(none)", f, diff, n, err))

    # --host with native runs allowed (no --expect-only): the host CPUs are consulted; in a VMware guest / on a
    # machine without verified hybrid identification the model is the logged P-core fallback - or, since U1491 loads
    # the bare-host profile with --host, the profile's core type (U1482)
    code, out = run(exe, ["--cases", os.path.join(DATA, "cases_xu_pcore.txt"), "--xcr0", "0x7", "--host"])
    le = [l for l in out.splitlines() if l.startswith("xinuse evidence: ")]
    lm = [l for l in out.splitlines() if l.startswith("xinuse model: ")]
    check(len(le) == 1 and "host 07H.0:EDX[15] hybrid = " in le[0] and "EfficiencyClass" in le[0], "--host (native runs allowed): %s" % (le[0] if le else "(missing)"))
    verified = bool(le) and "(verified)" in le[0]
    check(len(lm) == 1 and (verified or "FALLBACK" in lm[0] or "--host: the core type of the CPUID profile" in lm[0]),
          "  %s" % (lm[0] if lm else "(missing)"))

    # usage errors
    for args, msg in ((["--xinuse-model", "x"], "neither"), (["--xinuse-model", "P"], "neither"),
                      (["--guest", "--AMD"], "exclude each other"), (["--vm", "--host"], "exclude each other")):
        code, out = run(exe, ["--cases", os.path.join(DATA, "cases_xu_pcore.txt")] + args)
        check(code == 2 and msg in out, "usage error %s: exit %d" % (" ".join(args), code))

    print("xinuse_cli_check: %d checks, %d failed" % (checks, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
