"""docroot.py -- where the tools find the external reference sources (U1410, user request 2026-10-13).

The reference material (Intel XED, Intel SDE, the Pin kits, asmjit, the Intel approximation
reference C) lives outside the repository under ONE sources root:

    NOVMP_SOURCES_ROOT   environment variable; default E:\\sources

Expected layout below the root:
    xed-main\\datafiles\\...                              Intel XED datafiles (apx-f, avx10-2-aux, ...)
    sde-external-10.13.1-2026-07-28-win\\sde.exe          Intel SDE 10.13.1 (labelled reference only)
    pin-sources\\...                                      Pin kits
    asmjit\\db\\isa_x86.json                               asmjit instruction database
    Reference Implementations for IA Approximation Instructions\\

Every helper checks that the path exists and otherwise stops the tool with a message that names
the path, what was expected there and the environment variable to set. There is no fallback to
an older location and no stub result. PowerShell twin: docroot.ps1 (same variable, same default).
"""
import os
import sys

ENV = "NOVMP_SOURCES_ROOT"
DEFAULT_ROOT = "E:\\sources"
SDE_KIT = "sde-external-10.13.1-2026-07-28-win"


def sources_root():
    """the sources root: $NOVMP_SOURCES_ROOT if set (and not empty), else E:\\sources"""
    r = os.environ.get(ENV, "").strip()
    return os.path.normpath(r if r else DEFAULT_ROOT)


def _fail(path, what):
    r = os.environ.get(ENV, "").strip()
    src = "%s=%s" % (ENV, r) if r else "%s not set, default %s" % (ENV, DEFAULT_ROOT)
    sys.exit("[docroot] ERROR: missing %s\n"
             "[docroot]   expected: %s\n"
             "[docroot]   sources root: %s (%s)\n"
             "[docroot]   fix: set %s to the folder that holds xed-main\\, %s\\, pin-sources\\, asmjit\\ ...\n"
             "[docroot]        or pass the tool's own path option where it has one"
             % (path, what, sources_root(), src, ENV, SDE_KIT))


def require(rel, what, kind="dir"):
    """<root>\\rel, which must exist (kind 'dir' or 'file'); else stop with a clear message"""
    p = os.path.join(sources_root(), rel)
    ok = os.path.isdir(p) if kind == "dir" else os.path.isfile(p)
    if not ok:
        _fail(p, what)
    return p


def xed_datafiles(sub=None):
    """<root>\\xed-main\\datafiles[\\sub]"""
    rel = os.path.join("xed-main", "datafiles", sub) if sub else os.path.join("xed-main", "datafiles")
    return require(rel, "Intel XED datafiles folder (xed-main\\datafiles%s)" % ("\\" + sub if sub else ""))


def sde_exe():
    """<root>\\sde-external-10.13.1-2026-07-28-win\\sde.exe"""
    return require(os.path.join(SDE_KIT, "sde.exe"), "Intel SDE 10.13.1 launcher (%s\\sde.exe)" % SDE_KIT, "file")


def pin_sources():
    """<root>\\pin-sources"""
    return require("pin-sources", "the Pin kits folder (pin-sources\\pin-external-*)")


def asmjit_db():
    """<root>\\asmjit\\db\\isa_x86.json"""
    return require(os.path.join("asmjit", "db", "isa_x86.json"), "asmjit instruction database (asmjit\\db\\isa_x86.json)",
                   "file")


def approx_ref():
    """<root>\\Reference Implementations for IA Approximation Instructions"""
    return require("Reference Implementations for IA Approximation Instructions",
                   "Intel's approximation-instruction reference C (recip14.c, RECIP28EXP2.c)")


def sde_launch_path(p):
    """SDE 10.13.1's launcher starts pind.exe with an unquoted path: a kit path with a space fails with
    "Create pind process failed with code 5". Returns p unchanged when it has no space, else its 8.3
    short path; stops with a clear message when the short path still has a space (8.3 names disabled)."""
    p = os.path.abspath(p)
    if " " not in p:
        return p
    short = p
    if os.name == "nt":
        import ctypes
        buf = ctypes.create_unicode_buffer(32768)
        n = ctypes.windll.kernel32.GetShortPathNameW(p, buf, len(buf))
        if 0 < n < len(buf):
            short = buf.value
    if " " in short:
        sys.exit("[docroot] ERROR: the SDE path has a space and no 8.3 short name: %s\n"
                 "[docroot]   SDE 10.13.1 cannot start pind.exe from such a path (\"Create pind process failed with "
                 "code 5\").\n"
                 "[docroot]   fix: set %s (or the tool's SDE option) to a folder without spaces" % (p, ENV))
    return short
