#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_cases_f16c.py -- F16C hardware cases (ledger U440-U444), Python 3 stdlib only.

  python gen_cases_f16c.py > Emulator/data/cases_f16c.txt
      hardware cases (no "=>") for VEX VCVTPS2PH (xmm/m64 <- xmm, xmm/m128 <- ymm) and
      VCVTPH2PS (xmm <- xmm/m64, ymm <- xmm/m128). Run against the host CPU:
        emu-alltest --cases Emulator\\data\\cases_f16c.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt
                    --strict

SDM rules exercised:
  VCVTPS2PH (Vol2C 5-68, Vol1 14.4 Tables 14-8..14-14):
    "Underflow results (i.e., tiny results) are converted to denormals. MXCSR.FTZ is ignored."
    Rounding: imm8[2] = 0 -> imm8[1:0], imm8[2] = 1 -> MXCSR.RC; imm8[7:3] ignored.
    Exceptions: Invalid, Underflow, Overflow, Precision, Denormal (if MXCSR.DAZ = 0), i.e. DAZ
    applies to the FP32 source. Denormal source with DM masked and PM or UM unmasked: #XM with
    DE, UE and PE set. NaN: sign kept, significand truncated to 10 bits, SNaN quieted (#I).
  VCVTPH2PS (Vol2C 5-54, Table 14-11): "If case of a denormal operand, the correct normal result
    is returned. MXCSR.DAZ is ignored and is treated as if it 0. No denormal exception is
    reported on MXCSR." Exceptions: Invalid only (SNaN source).
Every single-value case puts the value in lane 0 (lanes 1..3 = 1.0, exact) so MXCSR names the
flags of that value alone; the packed cases (8 lanes, both VL, register and memory forms) check
lane order and the zeroing of the destination's upper bits (destination preloaded with junk).
"""

import sys

OUT = []


def w(line=""):
    OUT.append(line)


def le(v, n):
    return "".join("%02X" % b for b in (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little"))


def lanes32(vals):
    return "".join(le(v, 4) for v in vals)


def lanes16(vals):
    return "".join(le(v, 2) for v in vals)


ONE32 = 0x3F800000
ONE16 = 0x3C00
JUNK_X = "A5" * 16
JUNK_H = "5A" * 16

# FP32 sources for VCVTPS2PH (name, bits)
PS = [
    ("+1.0 exact", 0x3F800000),
    ("1+2^-23 inexact", 0x3F800001),
    ("1+half ulp16 tie even", 0x3F801000),
    ("1+3 half ulp16 tie odd", 0x3F803000),
    ("-1-ulp32", 0xBF800001),
    ("65504 max fp16", 0x477FE000),
    ("below 65520", 0x477FEFFF),
    ("65520 overflow RNE", 0x477FF000),
    ("-65520", 0xC77FF000),
    ("65536", 0x47800000),
    ("FLT_MAX", 0x7F7FFFFF),
    ("-FLT_MAX", 0xFF7FFFFF),
    ("+inf", 0x7F800000),
    ("-inf", 0xFF800000),
    ("QNaN", 0x7FC00000),
    ("QNaN payload", 0x7FC12345),
    ("-QNaN payload", 0xFFE5A5A5),
    ("SNaN payload", 0x7F812345),
    ("-SNaN payload", 0xFF9FFFFF),
    ("SNaN low payload only", 0x7F800001),
    ("+0", 0x00000000),
    ("-0", 0x80000000),
    ("fp32 min denormal", 0x00000001),
    ("fp32 max denormal", 0x007FFFFF),
    ("-fp32 denormal", 0x80400000),
    ("fp32 min normal", 0x00800000),
    ("fp16 min normal 2^-14", 0x38800000),
    ("just below 2^-14", 0x387FFFFF),
    ("2^-14 - half ulp tie", 0x387FF000),
    ("-just below 2^-14", 0xB87FFFFF),
    ("fp16 max denormal exact", 0x387FC000),
    ("fp16 denormal 2^-20 exact", 0x35800000),
    ("-fp16 denormal 2^-20 exact", 0xB5800000),
    ("fp16 min denormal 2^-24 exact", 0x33800000),
    ("2^-20 + small inexact", 0x35801000),
    ("-(2^-20 + small)", 0xB5801000),
    ("2^-25 tie to 0", 0x33000000),
    ("2^-25 + ulp", 0x33000001),
    ("-(2^-25 + ulp)", 0xB3000001),
    ("1.5*2^-24 tie", 0x33C00000),
    ("2^-27", 0x32000000),
    ("-2^-27", 0xB2000000),
]

# rounding configurations: (imm8, MXCSR.RC)
ROUND = [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (4, 1), (4, 2), (4, 3)]
# imm8[7:3] ignored
ROUND_IGN = [(0xF8, 3), (0xFB, 0), (0xFC, 2), (0x7D, 1)]

FTZDAZ = [(0, 0), (1, 0), (0, 1), (1, 1)]
MASKS = 0x1F80
FTZ = 0x8000
DAZ = 0x0040


def mxcsr(rc=0, ftz=0, daz=0, masks=MASKS):
    return masks | (rc << 13) | (FTZ if ftz else 0) | (DAZ if daz else 0)


# FP16 sources for VCVTPH2PS
PH = [
    ("+1.0", 0x3C00),
    ("fp16 min denormal", 0x0001),
    ("fp16 max denormal", 0x03FF),
    ("-fp16 denormal", 0x8200),
    ("fp16 min normal", 0x0400),
    ("fp16 max", 0x7BFF),
    ("+inf", 0x7C00),
    ("-inf", 0xFC00),
    ("QNaN", 0x7E00),
    ("QNaN payload", 0x7E5A),
    ("SNaN", 0x7D00),
    ("-SNaN payload", 0xFD01),
    ("SNaN min payload", 0x7C01),
    ("+0", 0x0000),
    ("-0", 0x8000),
]


def gen_ps2ph():
    w("# ===== VCVTPS2PH xmm, xmm, imm8: one value in lane 0, lanes 1..3 = 1.0; every rounding")
    w("# configuration (imm8[1:0] with imm8[2] = 0, MXCSR.RC with imm8[2] = 1) x FTZ/DAZ, all masked")
    for name, v in PS:
        w("# %s (%08X)" % (name, v))
        for imm, rc in ROUND:
            for ftz, daz in FTZDAZ:
                w("vcvtps2ph xmm0, xmm1, %d | mxcsr=0x%04X xmm1=%s xmm0=%s ymmh0=%s"
                  % (imm, mxcsr(rc, ftz, daz), lanes32([v, ONE32, ONE32, ONE32]), JUNK_X, JUNK_H))
    w("# ===== imm8[7:3] ignored (SDM Table 5-3)")
    for name, v in PS:
        if name not in ("1+2^-23 inexact", "-(2^-20 + small)", "2^-25 + ulp", "65520 overflow RNE",
                        "-just below 2^-14"):
            continue
        for imm, rc in ROUND_IGN:
            for ftz, daz in FTZDAZ:
                w("vcvtps2ph xmm0, xmm1, 0x%02X | mxcsr=0x%04X xmm1=%s xmm0=%s ymmh0=%s"
                  % (imm, mxcsr(rc, ftz, daz), lanes32([v, ONE32, ONE32, ONE32]), JUNK_X, JUNK_H))

    w("# ===== unmasked exceptions (#XM, destination unchanged, MXCSR flags): IM, DM, ZM, OM, UM, PM")
    w("# unmasked one at a time, UM+PM, all unmasked; imm8 = 0 (RNE) and 1 (down); FTZ/DAZ")
    unm = [("IM", 0x1F80 & ~0x0080), ("DM", 0x1F80 & ~0x0100), ("OM", 0x1F80 & ~0x0400),
           ("UM", 0x1F80 & ~0x0800), ("PM", 0x1F80 & ~0x1000), ("UM+PM", 0x1F80 & ~0x1800),
           ("all", 0x0000), ("ZM", 0x1F80 & ~0x0200)]
    for name, v in PS:
        w("# %s (%08X)" % (name, v))
        for mname, m in unm:
            for imm in (0, 1):
                for ftz, daz in FTZDAZ:
                    w("vcvtps2ph xmm0, xmm1, %d | mxcsr=0x%04X xmm1=%s xmm0=%s ymmh0=%s"
                      % (imm, mxcsr(0, ftz, daz, m), lanes32([v, ONE32, ONE32, ONE32]),
                         JUNK_X, JUNK_H))

    # packed: 8 lanes per group, every value appears somewhere
    vals = [v for _, v in PS]
    groups = [vals[i:i + 8] for i in range(0, len(vals), 8)]
    if len(groups[-1]) < 8:
        groups[-1] += [ONE32] * (8 - len(groups[-1]))
    w("# ===== packed: VEX.128 (lanes 0..3) and VEX.256 (lanes 0..7), register and memory destination")
    for g in groups:
        w("# lanes %s" % " ".join("%08X" % v for v in g))
        for imm, rc in ROUND:
            for ftz, daz in FTZDAZ:
                mx = mxcsr(rc, ftz, daz)
                x, h = lanes32(g[:4]), lanes32(g[4:])
                w("vcvtps2ph xmm0, xmm1, %d | mxcsr=0x%04X xmm1=%s ymmh1=%s xmm0=%s ymmh0=%s"
                  % (imm, mx, x, h, JUNK_X, JUNK_H))
                w("vcvtps2ph xmm0, ymm1, %d | mxcsr=0x%04X xmm1=%s ymmh1=%s xmm0=%s ymmh0=%s"
                  % (imm, mx, x, h, JUNK_X, JUNK_H))
                w("vcvtps2ph qword ptr [rsi+0x100], xmm1, %d | mxcsr=0x%04X xmm1=%s ymmh1=%s m+0x8100=%s"
                  % (imm, mx, x, h, "C3" * 24))
                w("vcvtps2ph xmmword ptr [rsi+0x100], ymm1, %d | mxcsr=0x%04X xmm1=%s ymmh1=%s m+0x8100=%s"
                  % (imm, mx, x, h, "C3" * 24))
    w("# ===== packed with unmasked exceptions: register and memory destination (no store on #XM)")
    for g in groups:
        for m in (0x1F80 & ~0x0800, 0x1F80 & ~0x1000, 0x1F80 & ~0x0080, 0x1F80 & ~0x0100, 0):
            for ftz, daz in FTZDAZ:
                mx = mxcsr(0, ftz, daz, m)
                x, h = lanes32(g[:4]), lanes32(g[4:])
                w("vcvtps2ph xmm0, ymm1, 0 | mxcsr=0x%04X xmm1=%s ymmh1=%s xmm0=%s ymmh0=%s"
                  % (mx, x, h, JUNK_X, JUNK_H))
                w("vcvtps2ph xmmword ptr [rsi+0x100], ymm1, 0 | mxcsr=0x%04X xmm1=%s ymmh1=%s m+0x8100=%s"
                  % (mx, x, h, "C3" * 24))


def gen_ph2ps():
    w("# ===== VCVTPH2PS xmm, xmm: one value in lane 0 (lanes 1..3 = 1.0); FTZ/DAZ x exception masks")
    masks = [("all masked", 0x1F80), ("DM unmasked", 0x1F80 & ~0x0100),
             ("IM unmasked", 0x1F80 & ~0x0080), ("UM+PM unmasked", 0x1F80 & ~0x1800),
             ("all unmasked", 0x0000)]
    for name, v in PH:
        w("# %s (%04X)" % (name, v))
        for mname, m in masks:
            for rc in (0, 1):
                for ftz, daz in FTZDAZ:
                    w("vcvtph2ps xmm0, xmm1 | mxcsr=0x%04X xmm1=%s xmm0=%s ymmh0=%s"
                      % (mxcsr(rc, ftz, daz, m), lanes16([v, ONE16, ONE16, ONE16]) + "77" * 8,
                         JUNK_X, JUNK_H))
    vals = [v for _, v in PH] + [0x3555]
    groups = [vals[i:i + 8] for i in range(0, len(vals), 8)]
    w("# ===== packed VCVTPH2PS: VEX.128 / VEX.256, register and memory source")
    for g in groups:
        for m in (0x1F80, 0x1F80 & ~0x0100, 0x1F80 & ~0x0080, 0):
            for ftz, daz in FTZDAZ:
                mx = mxcsr(0, ftz, daz, m)
                w("vcvtph2ps xmm0, xmm1 | mxcsr=0x%04X xmm1=%s xmm0=%s ymmh0=%s"
                  % (mx, lanes16(g), JUNK_X, JUNK_H))
                w("vcvtph2ps ymm0, xmm1 | mxcsr=0x%04X xmm1=%s xmm0=%s ymmh0=%s"
                  % (mx, lanes16(g), JUNK_X, JUNK_H))
                w("vcvtph2ps xmm0, qword ptr [rsi+0x100] | mxcsr=0x%04X m+0x8100=%s xmm0=%s ymmh0=%s"
                  % (mx, lanes16(g), JUNK_X, JUNK_H))
                w("vcvtph2ps ymm0, xmmword ptr [rsi+0x100] | mxcsr=0x%04X m+0x8100=%s xmm0=%s ymmh0=%s"
                  % (mx, lanes16(g), JUNK_X, JUNK_H))


def main():
    w("# F16C (ledger U440-U444): hardware cases, generated by Emulator/tools/isa/gen_cases_f16c.py")
    w("# (regenerate, do not edit). Only these self-generated snippets run natively:")
    w("#   emu-alltest --cases Emulator\\data\\cases_f16c.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt --strict")
    w("# SDM VCVTPS2PH: tiny results -> denormals, MXCSR.FTZ ignored; DE only if MXCSR.DAZ = 0 (DAZ applies")
    w("# to the FP32 source). SDM VCVTPH2PS: DAZ ignored, no DE, Invalid only.")
    gen_ps2ph()
    gen_ph2ps()
    sys.stdout.write("\n".join(OUT) + "\n")


if __name__ == "__main__":
    main()
