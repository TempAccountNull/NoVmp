#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sde_rcp.py -- SDE reference run for the approximate reciprocal instructions (decision A9, U940-U959).

Runs OUR OWN probe (sde_rcp_probe.exe, built from sde_rcp_probe.c) under Intel SDE 10.13.1 and
compares SDE's results with
  (a) the documented architectural requirements (error bound, special-case tables, DAZ / FTZ, the
      NaN rules), and
  (b) the emulator's documented stand-in (ref_rcp.py: correctly rounded).
The SDE results are a LABELLED REFERENCE ("SDE-validated"), never "hardware-validated", and never
the definition: the SDM wins any disagreement. A match with SDE does not prove that silicon gives
the same bits; a mismatch with the stand-in is expected (the stand-in is not Intel's algorithm).

Must run OUTSIDE a job object that forbids breakaway: SDE's launcher creates pind.exe with
CREATE_BREAKAWAY_FROM_JOB and fails with "Create pind process failed with code 5" otherwise (the
agents' shells run in such a job; run this from an ordinary user shell).

usage: python sde_rcp.py --probe sde_rcp_probe.exe --out DIR [--sde PATH\\sde.exe] [--quick]
       python sde_rcp.py --compare DIR          (re-analyse the dumps of an earlier run)
Exit status 0 when every SDE result meets every documented requirement (the stand-in agreement is
reported, not judged).
"""
import os
import random
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ref_rcp import (F16, BF16, F32, F64, rcp14, rsqrt14, rcp_ph, rsqrt_ph, rcp_bf16, rsqrt_bf16,  # noqa: E402
                     value, is_nan, is_inf, is_normal, specials)
from fractions import Fraction  # noqa: E402

BS = chr(92)
SDE_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..",
                           "Intel docs", "sde-external-10.13.1-2026-07-28-win", "sde.exe")

# name, probe feature, SDE CPU, encoding (zmm0 <- zmm1), format, model(x, mxcsr), rsqrt?, bound, extra SDE knobs
FORMS = [
    ("VRCP14PS", "avx512f", "-spr", "62F27D484CC1", F32, lambda x, m: rcp14(F32, x, m), False, Fraction(1, 2 ** 14), []),
    ("VRSQRT14PS", "avx512f", "-spr", "62F27D484EC1", F32, lambda x, m: rsqrt14(F32, x, m), True, Fraction(1, 2 ** 14), []),
    ("VRCP14PD", "avx512f", "-spr", "62F2FD484CC1", F64, lambda x, m: rcp14(F64, x, m), False, Fraction(1, 2 ** 14), []),
    ("VRSQRT14PD", "avx512f", "-spr", "62F2FD484EC1", F64, lambda x, m: rsqrt14(F64, x, m), True, Fraction(1, 2 ** 14), []),
    ("VRCPPH", "fp16", "-spr", "62F67D484CC1", F16, lambda x, m: rcp_ph(x), False, Fraction(1, 2 ** 11) + Fraction(1, 2 ** 14), ["-fp16_fast", "0"]),
    ("VRSQRTPH", "fp16", "-spr", "62F67D484EC1", F16, lambda x, m: rsqrt_ph(x), True, Fraction(1, 2 ** 11) + Fraction(1, 2 ** 14), ["-fp16_fast", "0"]),
    ("VRCPBF16", "avx10.2", "-dmr", "62F67C484CC1", BF16, lambda x, m: rcp_bf16(x), False, Fraction(1, 2 ** 8) + Fraction(1, 2 ** 14), []),
    ("VRSQRTBF16", "avx10.2", "-dmr", "62F67C484EC1", BF16, lambda x, m: rsqrt_bf16(x), True, Fraction(1, 2 ** 8) + Fraction(1, 2 ** 14), []),
]
MXCSRS = [0x1F80, 0x1FC0, 0x9F80]


def inputs_for(f, quick, rng):
    """the input list per format: exhaustive for 16-bit, structured + random for FP32 / FP64"""
    if f.bits == 16:
        return list(range(1 << 16))
    xs = list(specials(f))
    n = 1 << (12 if quick else 16)
    if f.bits == 32:
        # every mantissa's top 16 bits in [1, 4) (both exponent parities), the denormal and underflow
        # regions, every exponent
        xs += [0x3F800000 + (i << 8) + rng.getrandbits(8) for i in range(1 << 16 if not quick else 1 << 10)]
        xs += [0x40000000 + (i << 8) + rng.getrandbits(8) for i in range(1 << 16 if not quick else 1 << 10)]
    xs += [rng.getrandbits(f.p - 1) for _ in range(n)]                                  # denormals
    xs += [((2 * f.bias - 1) << (f.p - 1)) | rng.getrandbits(f.p - 1) for _ in range(n)]  # X > 2^(emax-1)
    xs += [(rng.randrange(1, (1 << f.ebits) - 1) << (f.p - 1)) | rng.getrandbits(f.p - 1) for _ in range(n)]
    return xs


def write_records(path, xs, f):
    lanes = 512 // f.bits
    fmt = {16: "<H", 32: "<I", 64: "<Q"}[f.bits]
    with open(path, "wb") as fh:
        for i in range(0, len(xs), lanes):
            ch = xs[i:i + lanes] + [0] * (lanes - len(xs[i:i + lanes]))
            src = b"".join(struct.pack(fmt, v) for v in ch)
            fh.write(b"\0" * 64 + src + b"\0" * 64 + b"\0" * 64)


def read_results(path, n, f):
    lanes = 512 // f.bits
    fmt = {16: "<H", 32: "<I", 64: "<Q"}[f.bits]
    data = open(path, "rb").read()
    out = []
    for r in range(0, len(data), 64):
        out += [struct.unpack_from(fmt, data, r + i * (f.bits // 8))[0] for i in range(lanes)]
    return out[:n]


def requirement_failures(name, f, rsq, bound, x, r, mx):
    """documented-requirement check of one SDE result; returns a reason or None"""
    daz = (mx & 0x40) and f in (F32, F64) or f is BF16
    ftz = (mx & 0x8000) and f in (F32, F64) or f is BF16
    sg = x & f.sign
    if is_nan(f, x):
        return None if r == x | f.qbit else "NaN rule"
    zero_like = (x & f.emask) == 0 and ((x & f.fmask) == 0 or daz)
    if zero_like:
        return None if r == sg | f.inf else "0 -> INF (sign kept)"
    if is_inf(f, x):
        if rsq:
            return None if r == (f.indef if sg else 0) else "INF row"
        return None if r == sg else "INF -> 0"
    if rsq and sg:
        return None if r == f.indef else "X < 0 -> QNaN indefinite"
    tiny_in = Fraction(1, 2 ** 16) if f is F16 else Fraction(1, 2 ** (f.bias + 1))
    if not rsq and abs(value(f, x)) <= tiny_in:         # "0 <= X <= 2^-128 / 2^-1024 / 2^-16 -> INF"
        return None if r == sg | f.inf else "very small denormal -> INF"
    if is_nan(f, r) or is_inf(f, r):
        return "unexpected NaN / INF"
    if not is_normal(f, r):
        if rsq:
            return "RSQRT result not normal"
        if ftz and (r & ~f.sign) != 0:
            return "FTZ: tiny result not flushed"
        return None if (r & f.sign) == sg else "tiny result sign"
    xv = value(f, x)
    rv = value(f, r)
    if rsq:
        q = rv * rv * xv
        return None if (1 - bound) ** 2 < q < (1 + bound) ** 2 else "error bound"
    return None if abs(rv * xv - 1) < bound else "error bound"


def analyse(out_dir, quick):
    ok = True
    lines = []
    for name, feat, cpu, enc, f, model, rsq, bound, knobs in FORMS:
        xs = inputs_for(f, quick, random.Random(sum(name.encode())))
        for mx in (MXCSRS if f in (F32, F64) else MXCSRS[:1] + [0x9FC0]):
            res = os.path.join(out_dir, "%s_%04X.bin" % (name, mx))
            if not os.path.exists(res):
                lines.append("%-10s MXCSR %04X: no SDE result (%s)" % (name, mx, res))
                ok = False
                continue
            rs = read_results(res, len(xs), f)
            agree = fails = 0
            first = []
            for x, r in zip(xs, rs):
                if r == model(x, mx):
                    agree += 1
                why = requirement_failures(name, f, rsq, bound, x, r, mx)
                if why:
                    fails += 1
                    if len(first) < 5:
                        first.append("x=%0*X r=%0*X (%s)" % (f.bits // 4, x, f.bits // 4, r, why))
            ok &= fails == 0
            lines.append("%-10s MXCSR %04X: %d inputs, SDE meets the documented requirements: %s; identical to the "
                         "emulator stand-in: %d (%.2f %%)" % (name, mx, len(xs), "yes" if not fails else "NO (%d)" % fails,
                                                              agree, 100.0 * agree / max(1, len(xs))))
            lines += ["    " + s for s in first]
    print("\n".join(lines))
    print("SDE-validated documented-requirement check: %s" % ("PASS" if ok else "FAIL"))
    return ok


def run(sde, probe, out_dir, quick):
    os.makedirs(out_dir, exist_ok=True)
    for name, feat, cpu, enc, f, model, rsq, bound, knobs in FORMS:
        xs = inputs_for(f, quick, random.Random(sum(name.encode())))
        rec = os.path.join(out_dir, "%s_in.bin" % name)
        write_records(rec, xs, f)
        for mx in (MXCSRS if f in (F32, F64) else MXCSRS[:1] + [0x9FC0]):
            res = os.path.join(out_dir, "%s_%04X.bin" % (name, mx))
            cmd = [sde, cpu] + knobs + ["--", probe, feat, enc, "%X" % mx, "file", rec, res]
            print(" ".join(cmd))
            sys.stdout.flush()
            subprocess.run(cmd, check=True)


def main():
    a = sys.argv
    quick = "--quick" in a
    if "--compare" in a:
        sys.exit(0 if analyse(a[a.index("--compare") + 1], quick) else 1)
    if "--probe" not in a or "--out" not in a:
        print(__doc__)
        sys.exit(2)
    sde = a[a.index("--sde") + 1] if "--sde" in a else os.path.normpath(SDE_DEFAULT)
    out = a[a.index("--out") + 1]
    run(sde, a[a.index("--probe") + 1], out, quick)
    sys.exit(0 if analyse(out, quick) else 1)


if __name__ == "__main__":
    main()
