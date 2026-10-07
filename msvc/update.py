"""NoVmp update checker (plan step 0.3). Run through update.cmd.

Reads and reports; never overwrites our files.
  * every repo: git fetch, branch, behind/ahead vs upstream, upstream log
  * our forks (root, VTIL-Core, linux-pe): upstream changes are only shown, never merged
  * pristine dependencies (unicorn, capstone, keystone): fast-forwarded only with --apply,
    and only when we have no local commits on them
  * project drift: every .vcxproj in NoVmp.sln is checked against the disk - listed files
    that are missing, and source files next to listed ones that the project doesn't list
    (compared with msvc\\project_drift_baseline.json, so only new drift is reported)

usage: update.cmd [--no-fetch] [--apply] [--save-baseline] [--max-log N]
"""
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BASELINE = os.path.join(HERE, "project_drift_baseline.json")
MSB = "{http://schemas.microsoft.com/developer/msbuild/2003}"
SRC_EXTS = (".c", ".cc", ".cpp", ".cxx", ".asm")

# path (relative to ROOT), kind, upstream branch, parent repo (relative to ROOT) or None
REPOS = [
    (".", "fork", "master", None),
    ("VTIL-Core", "fork", "master", "."),
    ("linux-pe", "fork", "master", "."),
    ("unicorn", "dep", "dev", "."),
    ("VTIL-Core/Dependencies/capstone", "dep", "next", "VTIL-Core"),
    ("VTIL-Core/Dependencies/keystone", "dep", "master", "VTIL-Core"),
]


def log(msg=""):
    print(msg, flush=True)


def git(repo, *args, check=True):
    cmd = ["git", "-c", "safe.directory=*", "-C", os.path.join(ROOT, repo)] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        raise RuntimeError("git %s failed in %s:\n%s" % (" ".join(args), repo, r.stderr.strip()))
    return r.stdout.strip()


def report_repos(fetch, apply, max_log):
    log("=" * 100)
    log("REPOSITORIES  (root: %s)" % ROOT)
    log("=" * 100)
    pending = []
    for path, kind, branch, parent in REPOS:
        if fetch:
            git(path, "fetch", "-q", "origin")
        cur = git(path, "branch", "--show-current") or "(detached)"
        head = git(path, "log", "-1", "--format=%h %ad %s", "--date=short")
        up = "origin/" + branch
        uphead = git(path, "log", "-1", "--format=%h %ad", "--date=short", up)
        behind = int(git(path, "rev-list", "--count", "HEAD.." + up))
        ahead = int(git(path, "rev-list", "--count", up + "..HEAD"))
        dirty = len([l for l in git(path, "status", "--porcelain").splitlines() if l.strip()])
        log("")
        log("[%s] %s  kind=%s  branch=%s" % ("UP-TO-DATE" if behind == 0 else "BEHIND %d" % behind, path, kind, cur))
        log("    HEAD     : %s" % head)
        log("    upstream : %s %s   ahead=%d behind=%d dirty=%d" % (up, uphead, ahead, behind, dirty))
        if behind:
            log("    upstream commits not in HEAD (newest first, max %d):" % max_log)
            for line in git(path, "log", "--format=%h %ad %s", "--date=short", "-n", str(max_log), "HEAD.." + up).splitlines():
                log("      " + line)
            if kind == "fork":
                base = git(path, "merge-base", "HEAD", up)
                log("    upstream diffstat since our base %s:" % base[:9])
                for line in git(path, "diff", "--stat=120", base, up).splitlines()[-15:]:
                    log("      " + line)
                log("    -> fork: NOT merged. Review, then take changes deliberately as commits on '%s'." % cur)
            elif ahead:
                log("    -> dependency has local commits: NOT fast-forwarded (needs a deliberate rebase).")
            else:
                pending.append((path, branch, parent))
                log("    -> pristine dependency: fast-forward with --apply.")
    if apply:
        for path, branch, parent in pending:
            if git(path, "status", "--porcelain"):
                log("[apply] %s is dirty - skipped" % path)
                continue
            old = git(path, "rev-parse", "--short", "HEAD")
            git(path, "merge", "--ff-only", "-q", "origin/" + branch)
            new = git(path, "rev-parse", "--short", "HEAD")
            rel = os.path.relpath(os.path.join(ROOT, path), os.path.join(ROOT, parent)).replace("\\", "/")
            git(parent, "add", rel)
            log("[apply] %s fast-forwarded %s -> %s; pointer staged in '%s' (commit it after build + tests)" % (path, old, new, parent))
    elif pending:
        log("")
        log("%d pristine dependenc%s can be fast-forwarded: rerun with --apply." % (len(pending), "y" if len(pending) == 1 else "ies"))
    return pending


def solution_projects():
    sln = open(os.path.join(ROOT, "NoVmp.sln"), encoding="utf-8-sig").read()
    out = []
    for line in sln.splitlines():
        if line.startswith("Project(") and ".vcxproj" in line:
            rel = line.split(",")[1].strip().strip('"')
            out.append(os.path.normpath(os.path.join(ROOT, rel)))
    return out


def project_files(vcxproj):
    tree = ET.parse(vcxproj)
    base = os.path.dirname(vcxproj)
    listed = []
    for tag in ("ClCompile", "ClInclude", "MASM", "None", "Text"):
        for el in tree.iter(MSB + tag):
            inc = el.get("Include")
            if inc and "$(" not in inc and "*" not in inc:
                listed.append(os.path.normpath(os.path.join(base, inc)))
    return listed


def check_projects(save_baseline):
    log("")
    log("=" * 100)
    log("PROJECT DRIFT  (.vcxproj vs disk)")
    log("=" * 100)
    baseline = {}
    if os.path.exists(BASELINE) and not save_baseline:
        baseline = json.load(open(BASELINE, encoding="utf-8"))
    current = {}
    problems = 0
    for proj in solution_projects():
        name = os.path.splitext(os.path.basename(proj))[0]
        listed = project_files(proj)
        listed_set = set(p.lower() for p in listed)
        missing = [p for p in listed if not os.path.exists(p)]
        dirs = sorted(set(os.path.dirname(p) for p in listed if p.lower().endswith(SRC_EXTS)))
        unlisted = []
        for d in dirs:
            if not os.path.isdir(d):
                continue
            for f in os.listdir(d):
                full = os.path.join(d, f)
                if f.lower().endswith(SRC_EXTS) and os.path.isfile(full) and full.lower() not in listed_set:
                    unlisted.append(os.path.relpath(full, ROOT).replace("\\", "/"))
        unlisted.sort()
        current[name] = unlisted
        new_unlisted = [u for u in unlisted if u not in set(baseline.get(name, []))] if baseline else []
        gone_unlisted = [u for u in baseline.get(name, []) if u not in set(unlisted)] if baseline else []
        status = "OK" if not missing and not new_unlisted else "CHECK"
        problems += len(missing) + len(new_unlisted)
        log("[%-5s] %-18s listed=%-5d missing=%-3d unlisted-in-source-dirs=%-4d new-since-baseline=%d" %
            (status, name, len(listed), len(missing), len(unlisted), len(new_unlisted)))
        for p in missing:
            log("          MISSING (listed, not on disk): %s" % os.path.relpath(p, ROOT))
        for u in new_unlisted:
            log("          NEW upstream source not in project: %s" % u)
        for u in gone_unlisted:
            log("          (baseline file no longer on disk: %s)" % u)
    if save_baseline or not os.path.exists(BASELINE):
        with open(BASELINE, "w", encoding="utf-8", newline="\n") as f:
            json.dump(current, f, indent=1, sort_keys=True)
            f.write("\n")
        log("")
        log("baseline written: %s (%d projects)" % (os.path.relpath(BASELINE, ROOT), len(current)))
    if problems:
        log("")
        log("%d project item(s) need a hand edit of the .vcxproj (nothing was changed)." % problems)
    return problems


def main():
    args = sys.argv[1:]
    known = {"--no-fetch", "--apply", "--save-baseline", "--max-log"}
    max_log = 30
    if "--max-log" in args:
        max_log = int(args[args.index("--max-log") + 1])
    for a in args:
        if a.startswith("--") and a not in known:
            print("[update] unknown option %s\n%s" % (a, __doc__.strip().splitlines()[-1]))
            sys.exit(2)  # same convention as build.cmd
    report_repos(fetch="--no-fetch" not in args, apply="--apply" in args, max_log=max_log)
    problems = check_projects(save_baseline="--save-baseline" in args)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
