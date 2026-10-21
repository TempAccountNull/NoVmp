r"""U1813 (notes\eread\P17_gate1_docs_5_6_15.md "LEFT TO IMPLEMENT" 2): emu-alltest report wording check.

usage: python check_alltest_report_wording.py REPORT.md [REPORT.md ...]

emu-alltest (quick run and --full, Emulator\tests\alltest\at_main.cpp) writes one "Known deviations:" line into
alltest_report.md. The forms it counts (Emulator\data\alltest_known_deviations.tsv) are i5-13600K deviations and the
VMware-guest artifact "PTWRITE hidden by the guest CPUID" (U1710), so the line uses neutral wording: the hardware
result differs from the SDM-modelled emulator result (documented in docs\quirks.md), and no longer says that "the
i5-13600K deviates from the SDM". Every REPORT.md given (test.cmd: the quick run's build\x64\<config>\tests\alltest\
alltest_report.md and the stored --full baseline Emulator\data\alltest_baseline\alltest_report.md) must hold
  * exactly one line starting with "Known deviations: ", and it equals NEW_LINE (N = any count);
  * no "deviates from the SDM" anywhere.
Two in-memory negative controls (the old line; no "Known deviations:" line) must be rejected, one positive control
(the neutral line) accepted.
Exit status 0 when every check holds, 1 otherwise, 2 on a usage error. Reads the reports only; writes nothing.
"""
import re
import sys

TSV = "`Emulator\\data\\alltest_known_deviations.tsv`"
NEW_TAIL = "(hardware result differs from the SDM-modelled emulator result, documented in docs/quirks.md)."
NEW_LINE = re.compile("^Known deviations: (0|[1-9][0-9]*) forms listed in " + re.escape(TSV + " " + NEW_TAIL) + "$")
OLD = "deviates from the SDM"


def problems(text):
    out = []
    lines = text.splitlines()
    kd = [(i + 1, l) for i, l in enumerate(lines) if l.startswith("Known deviations: ")]
    if len(kd) != 1:
        out.append('%d lines start with "Known deviations: " (expected 1)' % len(kd))
    for n, l in kd:
        if not NEW_LINE.match(l):
            out.append('line %d is not the neutral "Known deviations:" line: %s' % (n, l))
        else:
            print("  line %d ok: %s" % (n, l))
    for i, l in enumerate(lines):
        if OLD in l:
            out.append('line %d still says "%s"' % (i + 1, OLD))
    return out


def main(argv):
    if len(argv) < 2:
        print("usage: python check_alltest_report_wording.py REPORT.md [REPORT.md ...]")
        return 2
    bad = 0
    # negative controls: the checker must reject the old sentence and a report without the line
    old_line = ("Known deviations: 6 forms listed in " + TSV + " (docs/quirks.md: the "
                "i5-13600K deviates from the SDM, the emulator implements the SDM).")
    # positive control: the line at_main.cpp writes must be accepted
    good = "Known deviations: 6 forms listed in " + TSV + " " + NEW_TAIL
    p = problems("# emu-alltest report\n\n" + good + "\n")
    print("positive control (neutral line): %s" % ("accepted" if not p else "NOT ACCEPTED: " + "; ".join(p)))
    if p:
        bad += 1
    for name, text in (("old sentence", "# emu-alltest report\n\n" + old_line + "\n"), ("no line", "# emu-alltest report\n")):
        p = problems(text)
        print("negative control (%s): %s" % (name, "rejected (%d problem(s))" % len(p) if p else "NOT REJECTED"))
        if not p:
            bad += 1
    for path in argv[1:]:
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except OSError as e:
            print("FAIL %s: cannot read (%s)" % (path, e))
            bad += 1
            continue
        print("report %s (%d lines)" % (path, len(text.splitlines())))
        p = problems(text)
        for x in p:
            print("  FAIL " + x)
        print("  %s" % ("ok" if not p else "FAILED (%d problem(s))" % len(p)))
        bad += 1 if p else 0
    print("check_alltest_report_wording: %s" % ("OK" if not bad else "FAILED (%d)" % bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
