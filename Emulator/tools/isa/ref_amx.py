#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_amx.py -- independent reference model of the VEX-encoded Intel AMX instructions
(Python 3 stdlib only; ledger U170-U180)

Literal transcription of
  * SDM Vol1 ch. 19 (palette_table, helper functions write_row_and_zero, zero_upper_rows,
    zero_tilecfg_start, zero_all_tile_data, xcr0_supports_palette), 13.3 (XCR0[18:17]),
    13.14 (XFD);
  * SDM Vol2A 2.10 "Intel AMX Instruction Exception Classes" (AMX-E1..E6) and the
    "Operation" sections of LDTILECFG, STTILECFG, TILELOADD/TILELOADDT1, TILESTORED,
    TILEZERO, TILERELEASE, TDPBSSD/TDPBSUD/TDPBUSD/TDPBUUD, TDPBF16PS, TDPFP16PS,
    TCMMIMFP16PS/TCMMRLFP16PS (Vol2A/2B, 092);
  * ISE 319433-062 3.2 (RNE, exceptions masked, MXCSR untouched), Table 3-1 (denormals),
    3.4 fma32(acc, x, y, daz, ftz, ...): "traditional infinite precision fma".

The floating-point results are computed with exact rational arithmetic
(fractions.Fraction) and one explicit round-to-nearest-even to binary32 (gradual
underflow); FTZ (U596) as MXCSR.FTZ defines it (SDM Vol1 10.2.3.3, 11.5.2.5): a result that
is tiny after rounding with an unbounded exponent becomes a zero of its sign (the ISE's
"if ftz and denormal(v)"); DAZ turns denormal inputs into zeros. Choices the SDM leaves open (also made by the
emulator, documented in fpu_helper.c U178):
  * a flushed / DAZ-ed zero keeps the sign of the value it replaces (x86 FTZ/DAZ);
  * NaN operands: the result is the first NaN in the order x, y, acc (fma32: v = x*y+acc)
    or a, b (a + b), with the quiet bit set; invalid operations (inf*0, inf-inf) give the
    QNaN indefinite FFC00000h;
  * cvt_fp16_to_fp32 is exact; a NaN keeps sign and payload (frac << 13);
  * exception ordering: the encoding/CPUID/mode #UDs, then CR4.OSXSAVE/XCR0 #UD, then #NM
    (XFD, classes E3/E4/E5 only), then the #UDs that depend on TILECFG.

Usage:
  python ref_amx.py --selftest   hand-derived checks of the model, exit 0 on pass
  python ref_amx.py --cinc       unicorn/tests/unit/x86_amx_vectors.inc (stdout)
  python ref_amx.py --cases      Emulator/data/cases_amx.txt (stdout)

Written from the Intel documents only (not from any C implementation).
"""

import sys
from fractions import Fraction

M64 = (1 << 64) - 1

# --------------------------------------------------------------------------
# palette_table (CPUID leaf 1DH, ISE Table 1-3) and TMUL limits (leaf 1EH)
# --------------------------------------------------------------------------
PALETTE = {1: dict(total_tile_bytes=8192, bytes_per_tile=1024, bytes_per_row=64,
                   max_names=8, max_rows=16)}
MAX_PALETTE = 1
TMUL_MAXK = 16
TMUL_MAXN = 64

XCR0_TILECFG = 1 << 17
XCR0_TILEDATA = 1 << 18

# UC_CTL_X86_AMX bits (unicorn.h, U170)
AMX_TILE, AMX_INT8, AMX_BF16, AMX_FP16, AMX_COMPLEX = 1, 2, 4, 8, 16
AMX_ALL = 31

UD, NM, GP = 6, 7, 13


class Fault(Exception):
    def __init__(self, vector):
        Exception.__init__(self, vector)
        self.vector = vector


# --------------------------------------------------------------------------
# binary32 arithmetic: fma32 / add32 with DAZ = FTZ = 1, RNE (ISE 3.4)
# --------------------------------------------------------------------------
QNAN_INDEFINITE = 0xFFC00000


def f32_sign(b):
    return (b >> 31) & 1


def f32_exp(b):
    return (b >> 23) & 0xFF


def f32_frac(b):
    return b & 0x7FFFFF


def f32_is_nan(b):
    return f32_exp(b) == 0xFF and f32_frac(b) != 0


def f32_is_inf(b):
    return f32_exp(b) == 0xFF and f32_frac(b) == 0


def f32_is_zero(b):
    return (b & 0x7FFFFFFF) == 0


def f32_is_denormal(b):
    return f32_exp(b) == 0 and f32_frac(b) != 0


def f32_value(b):
    """exact value of a finite binary32"""
    e, f = f32_exp(b), f32_frac(b)
    if e == 0:
        v = Fraction(f, 1 << 149)
    else:
        v = Fraction((1 << 23) | f) * (Fraction(2) ** (e - 150))
    return -v if f32_sign(b) else v


def round_f32(v):
    """round the exact rational v to binary32, round-to-nearest-even, gradual underflow,
    overflow to infinity (RNE). A zero result keeps the sign of v (v != 0 here)."""
    assert v != 0
    sign = 1 if v < 0 else 0
    a = -v if v < 0 else v
    n, d = a.numerator, a.denominator

    def ge_pow2(e):          # a >= 2**e
        return n * (1 << max(-e, 0)) >= d * (1 << max(e, 0))

    e = n.bit_length() - d.bit_length()
    while not ge_pow2(e):
        e -= 1
    while ge_pow2(e + 1):
        e += 1
    q_exp = -149 if e < -126 else e - 23
    num = n * (1 << max(-q_exp, 0))
    den = d * (1 << max(q_exp, 0))
    q, r = divmod(num, den)
    if 2 * r > den or (2 * r == den and (q & 1)):
        q += 1
    if e < -126:
        if q == 0:
            return sign << 31
        return (sign << 31) | q          # q = 2^23 is the smallest normal (exponent 1)
    if q == (1 << 24):
        q >>= 1
        e += 1
    if e > 127:
        return (sign << 31) | 0x7F800000
    return (sign << 31) | ((e + 127) << 23) | (q - (1 << 23))


def daz(b):
    """DAZ: a denormal input is treated as a zero (of the same sign)"""
    return b & 0x80000000 if f32_is_denormal(b) else b


def ftz(b):
    """flush a denormal binary32 to a zero of the same sign"""
    return b & 0x80000000 if f32_is_denormal(b) else b


def tiny_unbounded(v):
    """U596: SDM Vol1 11.5.2.5 underflow condition: the magnitude of v rounded to 24 bits
    (RNE) with an unbounded exponent is less than the smallest normal 2^-126"""
    a = -v if v < 0 else v
    n, d = a.numerator, a.denominator
    e = n.bit_length() - d.bit_length()
    while n * (1 << max(-e, 0)) < d * (1 << max(e, 0)):
        e -= 1
    while n * (1 << max(-(e + 1), 0)) >= d * (1 << max(e + 1, 0)):
        e += 1
    q_exp = e - 23
    num = n * (1 << max(-q_exp, 0))
    den = d * (1 << max(q_exp, 0))
    q, r = divmod(num, den)
    if 2 * r > den or (2 * r == den and (q & 1)):
        q += 1
    return Fraction(q) * (Fraction(2) ** q_exp) < Fraction(2) ** -126


def round_f32_ftz(v):
    """U596: RNE to binary32 with FTZ = 1 as MXCSR.FTZ defines it (SDM Vol1 10.2.3.3): an
    underflow condition (tiny after rounding with an unbounded exponent, 11.5.2.5) returns a
    zero with the sign of the true result; otherwise the ordinary rounding. (Rounding to
    denormal precision first and flushing a denormal kept 2^-126 - 2^-150 as 2^-126.)"""
    if tiny_unbounded(v):
        return (1 << 31) if v < 0 else 0
    return round_f32(v)


def quiet(b):
    return b | 0x00400000


def fma32(acc, x, y):
    """ISE 3.4 fma32(acc, x, y, daz=1, ftz=1, sae=1, rc=RNE): v = (x*y) + acc"""
    x, y, acc = daz(x), daz(y), daz(acc)
    for o in (x, y, acc):
        if f32_is_nan(o):
            return quiet(o)
    ps = f32_sign(x) ^ f32_sign(y)
    if (f32_is_inf(x) and f32_is_zero(y)) or (f32_is_inf(y) and f32_is_zero(x)):
        return QNAN_INDEFINITE
    if f32_is_inf(x) or f32_is_inf(y):
        if f32_is_inf(acc) and f32_sign(acc) != ps:
            return QNAN_INDEFINITE
        return (ps << 31) | 0x7F800000
    if f32_is_inf(acc):
        return acc
    p = f32_value(x) * f32_value(y)
    s = p + f32_value(acc)
    if s == 0:
        if p == 0 and f32_is_zero(acc):
            return (ps & f32_sign(acc)) << 31      # (+-0) + (+-0): -0 only if both -0
        return 0                                   # exact cancellation: +0 under RNE
    return round_f32_ftz(s)


def add32(a, b):
    """a + b with DAZ = FTZ = 1, RNE (the horizontal and accumulating adds)"""
    a, b = daz(a), daz(b)
    for o in (a, b):
        if f32_is_nan(o):
            return quiet(o)
    if f32_is_inf(a) and f32_is_inf(b):
        return a if f32_sign(a) == f32_sign(b) else QNAN_INDEFINITE
    if f32_is_inf(a):
        return a
    if f32_is_inf(b):
        return b
    s = f32_value(a) + f32_value(b)
    if s == 0:
        if f32_is_zero(a) and f32_is_zero(b):
            return (f32_sign(a) & f32_sign(b)) << 31
        return 0
    return round_f32_ftz(s)


def make_fp32(bf16):
    """TDPBF16PS make_fp32: the bfloat16 bit pattern in bits 31:16 of a dword"""
    return (bf16 & 0xFFFF) << 16


def cvt_fp16_to_fp32(h):
    """exact FP16 -> FP32; FP16 denormals are handled (not treated as zero)"""
    s, e, f = (h >> 15) & 1, (h >> 10) & 0x1F, h & 0x3FF
    if e == 0x1F:
        return (s << 31) | 0x7F800000 | (f << 13)
    if e == 0:
        if f == 0:
            return s << 31
        v = Fraction(f, 1 << 24)
    else:
        v = Fraction((1 << 10) | f) * (Fraction(2) ** (e - 25))
    return round_f32(-v if s else v)


# --------------------------------------------------------------------------
# tile state
# --------------------------------------------------------------------------

def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def u32(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def put32(b, o, v):
    for i in range(4):
        b[o + i] = (v >> (8 * i)) & 0xFF


class Amx:
    """TILECFG (palette_id, start_row, t[n].colsb/rows/valid), TILEDATA, the controls"""

    def __init__(self, mask=AMX_ALL, xcr0=0x60007, osxsave=True, xfd=0, mode64=True):
        self.mask = mask
        self.xcr0 = xcr0
        self.osxsave = osxsave
        self.xfd = xfd
        self.xfd_err = 0
        self.mode64 = mode64
        self.palette_id = 0
        self.start_row = 0
        self.colsb = [0] * 8
        self.rows = [0] * 8
        self.tiles = [bytearray(1024) for _ in range(8)]
        self.mem = {}

    # ---- TILECFG image (the STTILECFG format) ----
    def tiles_configured(self):
        return self.palette_id != 0

    def valid(self, t):
        return self.palette_id != 0 and t < 8 and self.rows[t] != 0 and self.colsb[t] != 0

    def cfg_image(self):
        buf = bytearray(64)
        if not self.tiles_configured():
            return buf
        buf[0] = self.palette_id
        buf[1] = self.start_row
        p = 16
        for n in range(PALETTE[self.palette_id]["max_names"]):
            buf[p] = self.colsb[n] & 0xFF
            buf[p + 1] = self.colsb[n] >> 8
            p += 2
        p = 48
        for n in range(PALETTE[self.palette_id]["max_names"]):
            buf[p] = self.rows[n]
            p += 1
        return bytes(buf)

    def xcr0_supports_palette(self, palette_id):
        if palette_id == 0:
            return True
        if palette_id == 1:
            return (self.xcr0 & XCR0_TILECFG) and (self.xcr0 & XCR0_TILEDATA)
        return False

    def tilecfg_check(self, buf):
        """LDTILECFG's 'error' computation (SDM Vol2A LDTILECFG Operation)"""
        error = False
        pid = buf[0]
        if pid > MAX_PALETTE:
            error = True
        if not self.xcr0_supports_palette(pid):
            error = True
        cfg = None
        if pid != 0 and not error:
            pt = PALETTE[pid]
            start_row = buf[1]
            if any(buf[2:16]):
                error = True
            p = 16
            colsb = []
            for n in range(pt["max_names"]):
                c = u16(buf, p)
                p += 2
                if c > pt["bytes_per_row"]:
                    error = True
                colsb.append(c)
            if any(buf[p:48]):
                error = True
            p = 48
            rows = []
            for n in range(pt["max_names"]):
                r = buf[p]
                if r > pt["max_rows"]:
                    error = True
                rows.append(r)
                p += 1
            if any(buf[p:64]):
                error = True
            for n in range(pt["max_names"]):
                if (rows[n] != 0) != (colsb[n] != 0):
                    error = True        # one of rows or colsb was 0 but not both
            cfg = (pid, start_row, colsb, rows)
        return error, cfg

    def set_cfg(self, cfg):
        if cfg is None:
            self.palette_id, self.start_row = 0, 0
            self.colsb, self.rows = [0] * 8, [0] * 8
        else:
            self.palette_id, self.start_row = cfg[0], cfg[1]
            self.colsb, self.rows = list(cfg[2]), list(cfg[3])

    def load_cfg_like_xrstor(self, buf):
        """UC_X86_REG_TILECFG write / XRSTOR: an image LDTILECFG refuses -> INIT"""
        error, cfg = self.tilecfg_check(bytes(buf))
        self.set_cfg(None if error or buf[0] == 0 else cfg)

    # ---- helpers of SDM Vol1 19.4 ----
    def write_row_and_zero(self, t, r, data, nbytes):
        row = self.tiles[t]
        for j in range(nbytes):
            row[64 * r + j] = data[j]
        for j in range(nbytes, PALETTE[self.palette_id]["bytes_per_row"]):
            row[64 * r + j] = 0

    def zero_upper_rows(self, t, r):
        pt = PALETTE[self.palette_id]
        for i in range(r, pt["max_rows"]):
            for j in range(pt["bytes_per_row"]):
                self.tiles[t][64 * i + j] = 0

    def zero_tilecfg_start(self):
        self.start_row = 0

    def zero_all_tile_data(self):
        if self.xcr0 & XCR0_TILEDATA:
            for t in range(8):
                self.tiles[t][:] = bytes(1024)

    # ---- exception classes (SDM Vol2A 2.10), the run-time part ----
    def check_enabled(self):
        if not self.osxsave or (self.xcr0 & 0x60000) != 0x60000:
            raise Fault(UD)

    def check_xfd(self):
        supported = (1 << 18) if self.mask else 0
        if self.xfd & self.xcr0 & supported & (1 << 18):
            self.xfd_err = self.xfd & (1 << 18)
            raise Fault(NM)

    def check_e3(self, t):
        self.check_enabled()
        self.check_xfd()
        if not self.tiles_configured():
            raise Fault(UD)
        if not self.valid(t) or t >= PALETTE[self.palette_id]["max_names"]:
            raise Fault(UD)
        if self.colsb[t] % 4 != 0:
            raise Fault(UD)
        if self.start_row >= self.rows[t]:
            raise Fault(UD)

    def check_e4(self, d, s1, s2):
        self.check_enabled()
        if d == s1 or s1 == s2 or d == s2:
            raise Fault(UD)
        self.check_xfd()
        if not self.tiles_configured():
            raise Fault(UD)
        mx = PALETTE[self.palette_id]["max_names"]
        for t in (d, s1, s2):
            if t >= mx or not self.valid(t):
                raise Fault(UD)
            if self.colsb[t] % 4 != 0:
                raise Fault(UD)
        if self.colsb[d] != self.colsb[s2]:
            raise Fault(UD)
        if self.rows[d] != self.rows[s1]:
            raise Fault(UD)
        if self.colsb[s1] // 4 != self.rows[s2]:
            raise Fault(UD)
        if self.colsb[d] > TMUL_MAXN or self.colsb[s2] > TMUL_MAXN:
            raise Fault(UD)
        if self.colsb[s1] // 4 > TMUL_MAXK or self.rows[s2] > TMUL_MAXK:
            raise Fault(UD)

    def check_e5(self, t):
        self.check_enabled()
        self.check_xfd()
        if not self.tiles_configured():
            raise Fault(UD)
        if not self.valid(t) or t >= PALETTE[self.palette_id]["max_names"]:
            raise Fault(UD)

    # ---- memory (flat, byte granular) ----
    def rd(self, a):
        return self.mem.get(a & M64, 0)

    def wr(self, a, v):
        self.mem[a & M64] = v & 0xFF

    # ---- instructions ----
    def LDTILECFG(self, mem):
        self.check_enabled()
        buf = bytes(self.rd(mem + i) for i in range(64))
        error, cfg = self.tilecfg_check(buf)
        if error:
            raise Fault(GP)
        if buf[0] == 0:
            self.set_cfg(None)               # TILES_CONFIGURED := 0, tilecfg := 0
            self.zero_all_tile_data()
        else:
            self.set_cfg(cfg)
            self.zero_all_tile_data()

    def STTILECFG(self, mem):
        self.check_enabled()
        buf = self.cfg_image()
        for i in range(64):
            self.wr(mem + i, buf[i])

    def TILERELEASE(self):
        self.check_enabled()
        self.zero_all_tile_data()
        self.set_cfg(None)

    def TILELOADD(self, tdest, base_disp, stride):
        self.check_e3(tdest)
        start = self.start_row
        self.zero_upper_rows(tdest, start)
        nbytes = self.colsb[tdest]
        while start < self.rows[tdest]:
            memptr = base_disp + start * stride
            self.write_row_and_zero(tdest, start, [self.rd(memptr + j) for j in range(nbytes)],
                                    nbytes)
            start += 1
        self.zero_tilecfg_start()

    TILELOADDT1 = TILELOADD

    def TILESTORED(self, base_disp, stride, tsrc):
        self.check_e3(tsrc)
        start = self.start_row
        while start < self.rows[tsrc]:
            memptr = base_disp + start * stride
            for j in range(self.colsb[tsrc]):
                self.wr(memptr + j, self.tiles[tsrc][64 * start + j])
            start += 1
        self.zero_tilecfg_start()

    def TILEZERO(self, tdest):
        self.check_e5(tdest)
        pt = PALETTE[self.palette_id]
        for i in range(pt["max_rows"]):
            for j in range(pt["bytes_per_row"]):
                self.tiles[tdest][64 * i + j] = 0
        self.zero_tilecfg_start()

    def TDPB(self, kind, d, s1, s2):
        """TDPBSSD / TDPBSUD / TDPBUSD / TDPBUUD; kind = 'SS', 'SU', 'US', 'UU'"""
        self.check_e4(d, s1, s2)
        sx = kind[0] == "S"
        sy = kind[1] == "S"

        def ext(b, signed):
            return b - 256 if signed and b >= 128 else b

        T = self.tiles
        for m in range(self.rows[d]):
            tmp = [u32(T[d], 64 * m + 4 * n) for n in range(16)]
            for k in range(self.colsb[s1] // 4):
                for n in range(self.colsb[d] // 4):
                    x = T[s1][64 * m + 4 * k: 64 * m + 4 * k + 4]
                    y = T[s2][64 * k + 4 * n: 64 * k + 4 * n + 4]
                    c = tmp[n]
                    for i in range(4):
                        c += ext(x[i], sx) * ext(y[i], sy)
                    tmp[n] = c & 0xFFFFFFFF
            data = bytearray(64)
            for n in range(16):
                put32(data, 4 * n, tmp[n])
            self.write_row_and_zero(d, m, data, self.colsb[d])
        self.zero_upper_rows(d, self.rows[d])
        self.zero_tilecfg_start()

    def _fp_dot(self, d, s1, s2, products):
        """the common TDPBF16PS / TDPFP16PS / TCMM*FP16PS body; products(a, b) gives the
        (x, y) FP32 pairs for temp1.fp32[2n+0] and temp1.fp32[2n+1] from the dwords
        a = tsrc1.row[m].dword[k] and b = tsrc2.row[k].dword[n]"""
        T = self.tiles
        for m in range(self.rows[d]):
            temp1 = [0] * (self.colsb[d] // 2)
            for k in range(self.colsb[s1] // 4):
                for n in range(self.colsb[d] // 4):
                    a = u32(T[s1], 64 * m + 4 * k)
                    b = u32(T[s2], 64 * k + 4 * n)
                    (x0, y0), (x1, y1) = products(a, b)
                    temp1[2 * n + 0] = fma32(temp1[2 * n + 0], x0, y0)
                    temp1[2 * n + 1] = fma32(temp1[2 * n + 1], x1, y1)
            data = bytearray(T[d][64 * m: 64 * m + 64])
            for n in range(self.colsb[d] // 4):
                tmpf32 = add32(temp1[2 * n], temp1[2 * n + 1])
                put32(data, 4 * n, add32(u32(data, 4 * n), tmpf32))
            self.write_row_and_zero(d, m, data, self.colsb[d])
        self.zero_upper_rows(d, self.rows[d])
        self.zero_tilecfg_start()

    def TDPBF16PS(self, d, s1, s2):
        self.check_e4(d, s1, s2)
        self._fp_dot(d, s1, s2, lambda a, b: (
            (make_fp32(a & 0xFFFF), make_fp32(b & 0xFFFF)),
            (make_fp32(a >> 16), make_fp32(b >> 16))))

    def TDPFP16PS(self, d, s1, s2):
        self.check_e4(d, s1, s2)
        self._fp_dot(d, s1, s2, lambda a, b: (
            (cvt_fp16_to_fp32(a & 0xFFFF), cvt_fp16_to_fp32(b & 0xFFFF)),
            (cvt_fp16_to_fp32(a >> 16), cvt_fp16_to_fp32(b >> 16))))

    def TCMMIMFP16PS(self, d, s1, s2):
        # temp1[2n+0] = fma32(., s1o, s2e); temp1[2n+1] = fma32(., s1e, s2o)
        self.check_e4(d, s1, s2)
        self._fp_dot(d, s1, s2, lambda a, b: (
            (cvt_fp16_to_fp32(a >> 16), cvt_fp16_to_fp32(b & 0xFFFF)),
            (cvt_fp16_to_fp32(a & 0xFFFF), cvt_fp16_to_fp32(b >> 16))))

    def TCMMRLFP16PS(self, d, s1, s2):
        # s1o = cvt_fp16_to_fp32(-tsrc1.fp16[2k+1]); temp1[2n+0] = fma32(., s1e, s2e);
        # temp1[2n+1] = fma32(., s1o, s2o)
        self.check_e4(d, s1, s2)
        self._fp_dot(d, s1, s2, lambda a, b: (
            (cvt_fp16_to_fp32(a & 0xFFFF), cvt_fp16_to_fp32(b & 0xFFFF)),
            (cvt_fp16_to_fp32((a >> 16) ^ 0x8000), cvt_fp16_to_fp32(b >> 16))))


# --------------------------------------------------------------------------
# encodings (SDM Vol2A/2B "Opcode/Instruction"), VEX.128.pp.0F38.W0
# --------------------------------------------------------------------------
PP = {"NP": 0, "66": 1, "F3": 2, "F2": 3}

# name: (pp, opcode, form, feature)
#   form: 'm' memory !(11):000:bbb, 'rel' 49 C0, 'z' 11:rrr:000, 'sib' !(11):rrr:100,
#         'rrr' 11:rrr:bbb + vvvv
FORMS = {
    "LDTILECFG":    ("NP", 0x49, "m", AMX_TILE, "E1"),
    "STTILECFG":    ("66", 0x49, "m", AMX_TILE, "E2"),
    "TILERELEASE":  ("NP", 0x49, "rel", AMX_TILE, "E6"),
    "TILEZERO":     ("F2", 0x49, "z", AMX_TILE, "E5"),
    "TILELOADD":    ("F2", 0x4B, "sib", AMX_TILE, "E3"),
    "TILELOADDT1":  ("66", 0x4B, "sib", AMX_TILE, "E3"),
    "TILESTORED":   ("F3", 0x4B, "sib", AMX_TILE, "E3"),
    "TDPBF16PS":    ("F3", 0x5C, "rrr", AMX_BF16, "E4"),
    "TDPFP16PS":    ("F2", 0x5C, "rrr", AMX_FP16, "E4"),
    "TDPBSSD":      ("F2", 0x5E, "rrr", AMX_INT8, "E4"),
    "TDPBSUD":      ("F3", 0x5E, "rrr", AMX_INT8, "E4"),
    "TDPBUSD":      ("66", 0x5E, "rrr", AMX_INT8, "E4"),
    "TDPBUUD":      ("NP", 0x5E, "rrr", AMX_INT8, "E4"),
    "TCMMIMFP16PS": ("66", 0x6C, "rrr", AMX_COMPLEX, "E4"),
    "TCMMRLFP16PS": ("NP", 0x6C, "rrr", AMX_COMPLEX, "E4"),
}

REG = {"rax": 0, "rcx": 1, "rdx": 2, "rbx": 3, "rsp": 4, "rbp": 5, "rsi": 6, "rdi": 7,
       "r8": 8, "r9": 9, "r10": 10, "r11": 11, "r12": 12, "r13": 13, "r14": 14, "r15": 15}


def vex3(pp, R, X, B, W, vvvv, L, map_=2):
    """C4 RXBmmmmm WvvvvLpp (R/X/B/vvvv given as the logical register bits)"""
    b1 = ((0 if R else 0x80) | (0 if X else 0x40) | (0 if B else 0x20) | map_)
    b2 = (W << 7) | ((~vvvv & 15) << 3) | (L << 2) | pp
    return [0xC4, b1, b2]


def enc(name, reg=0, rm=0, vvvv=0, base="rsi", index=None, scale=0, disp=0,
        W=0, L=0, pp=None, prefix=(), modrm=None, nosib=False, map_=2):
    """encode one AMX form; reg/rm/vvvv are tile numbers (0..15) or for memory forms
    reg is ModRM.reg. Returns the byte list."""
    fpp, op, form, _, _ = FORMS[name]
    ppv = PP[fpp] if pp is None else pp
    out = list(prefix)
    if form in ("rrr", "z", "rel"):
        rmv = rm if form == "rrr" else 0
        out += vex3(ppv, (reg >> 3) & 1, 0, (rmv >> 3) & 1, W, vvvv, L, map_)
        out += [op, modrm if modrm is not None else (0xC0 | ((reg & 7) << 3) | (rmv & 7))]
        return out
    # memory forms
    b = REG[base]
    X = 0
    tail = []
    if form == "sib" and not nosib or form == "m" and index is not None:
        ix = 4 if index is None else REG[index]
        X = (ix >> 3) & 1
        rmf, sib = 4, [(scale << 6) | ((ix & 7) << 3) | (b & 7)]
    else:
        rmf, sib = b & 7, []
        assert (b & 7) not in (4, 5)
    if disp == 0 and (b & 7) != 5:
        mod, d = 0, []
    elif -128 <= disp <= 127:
        mod, d = 1, [disp & 0xFF]
    else:
        mod, d = 2, [(disp >> (8 * i)) & 0xFF for i in range(4)]
    out += vex3(ppv, (reg >> 3) & 1, X, (b >> 3) & 1, W, vvvv, L, map_)
    out += [op, modrm if modrm is not None else ((mod << 6) | ((reg & 7) << 3) | rmf)]
    out += sib + d
    return out


# --------------------------------------------------------------------------
# decoder + static exception checks (Vol2A 2.10 / 2.3 / the opcode tables)
# --------------------------------------------------------------------------

def decode(code, mode64=True, mask=AMX_ALL):
    """-> ('UD', None) or (name, fields) for the first instruction of code"""
    i = 0
    pre = set()
    while code[i] in (0x66, 0xF2, 0xF3, 0xF0, 0x67, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65) or \
            (mode64 and 0x40 <= code[i] <= 0x4F):
        pre.add(code[i] if not (mode64 and 0x40 <= code[i] <= 0x4F) else "REX")
        i += 1
    if code[i] != 0xC4:
        return "UD", None                      # only the 3-byte VEX reaches map 0F38
    b1, b2 = code[i + 1], code[i + 2]
    if not mode64 and (b1 & 0xC0) != 0xC0:
        return "LES", None
    if pre & {0x66, 0xF2, 0xF3, 0xF0, "REX"}:
        return "UD", None                      # #UD if preceded by LOCK, 66H, F2H, F3H or REX
    R, X, B = (~b1 >> 7) & 1, (~b1 >> 6) & 1, (~b1 >> 5) & 1
    mmmmm = b1 & 0x1F
    W, vvvv, L, pp = b2 >> 7, (~b2 >> 3) & 15, (b2 >> 2) & 1, b2 & 3
    op, modrm = code[i + 3], code[i + 4]
    mod, rg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
    if mmmmm != 2:
        return "UD", None
    found = None
    for name, (fpp, fop, form, feat, cls) in FORMS.items():
        if fop != op or PP[fpp] != pp:
            continue
        if form in ("m", "sib") and mod == 3:
            continue
        if form in ("rel", "z", "rrr") and mod != 3:
            continue
        found = (name, form, feat, cls)
    if found is None:
        return "UD", None
    name, form, feat, cls = found
    if not (mask & feat) or not (mask & AMX_TILE):
        return "UD", None                      # CPUID feature flag
    if not mode64:
        return "UD", None                      # IA32_EFER.LMA != 1 OR CS.L != 1
    if L != 0 or W != 0:
        return "UD", None                      # VEX.128, W0
    if cls != "E4" and vvvv != 0:
        return "UD", None                      # VVVV != 1111b
    if form == "m" and rg != 0:
        return "UD", None                      # !(11):000:bbb
    if form == "rel" and modrm != 0xC0:
        return "UD", None                      # 49 C0
    if form == "z" and rm != 0:
        return "UD", None                      # 11:rrr:000
    if form == "sib" and rm != 4:
        return "UD", None                      # "#UD if not using SIB addressing"
    return name, dict(reg=rg | (R << 3), rm=rm | (B << 3), vvvv=vvvv, X=X, B=B, mod=mod,
                      a32=0x67 in pre)


# --------------------------------------------------------------------------
# deterministic tile contents shared with test_x86.c (splitmix64)
# --------------------------------------------------------------------------
BF16, FP16, F32 = 0, 1, 2
K_ZERO, K_BYTES = 0, 1
CLASSES = ["NORMAL", "INT", "WIDE", "TINY", "HUGE", "SPECIAL"]
# kind numbers: 2 + 6*fmt + class
KIND_NAMES = ["ZERO", "BYTES"] + ["%s_%s" % (f, c) for f in ("BF16", "FP16", "F32")
                                  for c in CLASSES]


def kind(fmt, cls):
    return 2 + 6 * fmt + CLASSES.index(cls)


def splitmix64(state):
    state = (state + 0x9E3779B97F4A7C15) & M64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return state, z ^ (z >> 31)


def gen_elem(fmt, cls, r):
    EB, FB = {BF16: (8, 7), FP16: (5, 10), F32: (8, 23)}[fmt]
    W = 1 + EB + FB
    BIAS = (1 << (EB - 1)) - 1
    EMAX = (1 << EB) - 1
    sign = (r >> 63) & 1
    c = r & 0xFF
    fr = (r >> 8) & ((1 << FB) - 1)
    ex = (r >> 40) & 0xFFFF
    sb = sign << (W - 1)

    def norm(lo, hi):
        return sb | ((lo + ex % (hi - lo + 1)) << FB) | fr

    def zero():
        return sb

    def den():
        return sb | (fr if fr else 1)

    def inf():
        return sb | (EMAX << FB)

    def nan():
        f = fr & ((1 << (FB - 1)) - 1)
        if (r >> 56) & 1:
            f |= 1 << (FB - 1)
        elif f == 0:
            f = 1
        return sb | (EMAX << FB) | f

    if cls == "NORMAL":
        return norm(BIAS - 6, BIAS + 6)
    if cls == "INT":
        v = ex % 9
        a = abs(v - 4)
        s = 1 if v < 4 else (sign if v == 4 else 0)
        mag = {0: 0, 1: BIAS << FB, 2: (BIAS + 1) << FB,
               3: ((BIAS + 1) << FB) | (1 << (FB - 1)), 4: (BIAS + 2) << FB}[a]
        return (s << (W - 1)) | mag
    if cls == "WIDE":
        if c < 8:
            return zero()
        if c < 16:
            return den()
        if c < 18:
            return inf()
        if c < 20:
            return nan()
        return norm(1, EMAX - 1)
    if cls == "TINY":
        if fmt == FP16:
            return den() if c < 128 else norm(1, 3)
        if fmt == BF16:
            return den() if c < 64 else norm(BIAS - 70, BIAS - 56)
        return den() if c < 128 else norm(1, 8)
    if cls == "HUGE":
        if fmt == BF16:
            return norm(BIAS + 60, BIAS + 66)
        if fmt == FP16:
            return norm(EMAX - 3, EMAX - 1)
        return norm(EMAX - 8, EMAX - 1)
    if cls == "SPECIAL":
        if c < 40:
            return zero()
        if c < 80:
            return den()
        if c < 120:
            return inf()
        if c < 160:
            return nan()
        return norm(BIAS - 2, BIAS + 2)
    raise ValueError(cls)


def fill_tile(seed, t, k):
    state = (seed + (t + 1) * 0xD1B54A32D192ED03) & M64
    out = bytearray(1024)
    if k == K_ZERO:
        return out
    if k == K_BYTES:
        for i in range(128):
            state, r = splitmix64(state)
            for b in range(8):
                out[8 * i + b] = (r >> (8 * b)) & 0xFF
        return out
    fmt, cls = divmod(k - 2, 6)
    cls = CLASSES[cls]
    w = 4 if fmt == F32 else 2
    for i in range(1024 // w):
        state, r = splitmix64(state)
        v = gen_elem(fmt, cls, r)
        for b in range(w):
            out[w * i + b] = (v >> (8 * b)) & 0xFF
    return out


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & M64
    return h


def cfg_bytes(shapes, palette=1, start_row=0):
    """shapes: {tile: (rows, colsb)} -> 64-byte TILECFG image"""
    buf = bytearray(64)
    buf[0] = palette
    buf[1] = start_row
    for t, (rows, colsb) in shapes.items():
        buf[16 + 2 * t] = colsb & 0xFF
        buf[17 + 2 * t] = colsb >> 8
        buf[48 + t] = rows
    return bytes(buf)


# --------------------------------------------------------------------------
# self test (hand-derived values)
# --------------------------------------------------------------------------

def selftest():
    ok = True

    def chk(cond, what):
        nonlocal ok
        if not cond:
            print("FAIL:", what)
            ok = False

    one, two, half = 0x3F800000, 0x40000000, 0x3F000000
    chk(fma32(0, one, two) == two, "0 + 1*2 = 2")
    chk(fma32(one, two, two) == 0x40A00000, "1 + 2*2 = 5")
    chk(add32(one, 0xBF800000) == 0, "1 + -1 = +0")
    chk(add32(0x80000000, 0x80000000) == 0x80000000, "-0 + -0 = -0")
    chk(add32(0x80000000, 0) == 0, "-0 + +0 = +0")
    chk(fma32(0x80000000, 0x80000000, one) == 0x80000000, "-0 + (-0*1) = -0")
    # DAZ: denormal inputs are zeros
    chk(fma32(0, 0x00000001, 0x7F000000) == 0, "denormal * 2^127 = 0 (DAZ)")
    chk(add32(0x80000001, 0x80000000) == 0x80000000, "-denormal + -0 = -0")
    # FTZ: 2^-126 * 0.5 = 2^-127 (denormal) -> 0; 2^-126 stays
    chk(fma32(0, 0x00800000, half) == 0, "2^-127 flushed")
    chk(fma32(0, 0x80800000, half) == 0x80000000, "-2^-127 flushed to -0")
    chk(fma32(0, 0x00800000, one) == 0x00800000, "2^-126 kept")
    # rounding at denormal precision: (2^-126 - 2^-150) rounds up to 2^-126 (gradual underflow)
    v = round_f32(Fraction(2) ** -126 - Fraction(2) ** -150)
    chk(v == 0x00800000, "RNE to the smallest normal (got %08x)" % v)
    # U596, FTZ per SDM Vol1 10.2.3.3 / 11.5.2.5: that value is tiny with an unbounded exponent
    # (1.11..1b * 2^-127, exact at 24 bits) -> +0; the i5-13600K's VFMADD231SS with FTZ agrees
    chk(fma32(0x00800000, 0x1A000000, 0x9A000000) == 0, "2^-126 - 2^-150 flushed (U596)")
    chk(fma32(0x80800000, 0x9A000000, 0x9A000000) == 0x80000000, "-(2^-126 - 2^-150) -> -0")
    # 2^-126 - 2^-152 rounds to 2^-126 with an unbounded exponent: not tiny, kept
    chk(fma32(0x00800000, 0x19800000, 0x99800000) == 0x00800000, "2^-126 - 2^-152 kept")
    chk(add32(0x00800000, 0x80800000) == 0, "2^-126 - 2^-126 = +0")
    # RNE ties to even: 1 + 2^-24 -> 1; 1 + 3*2^-24 -> 1 + 2^-22
    chk(round_f32(1 + Fraction(1, 1 << 24)) == one, "tie to even down")
    chk(round_f32(1 + Fraction(3, 1 << 24)) == 0x3F800002, "tie to even up")
    # overflow
    chk(fma32(0, 0x7F000000, 0x40000000) == 0x7F800000, "2^127 * 2 = +inf")
    # NaN rules
    chk(fma32(0x7FC00001, 0x7F800001, one) == 0x7FC00001, "first NaN is x (SNaN quieted)")
    chk(fma32(0x7FC00001, one, 0xFF800002) == 0xFFC00002, "y NaN before acc")
    chk(fma32(0, 0x7F800000, 0) == QNAN_INDEFINITE, "inf * 0 = indefinite")
    chk(fma32(0, 0x7F800000, 0x00000001) == QNAN_INDEFINITE, "inf * denormal (DAZ) = indef")
    chk(fma32(0xFF800000, 0x7F800000, one) == QNAN_INDEFINITE, "inf - inf = indefinite")
    chk(add32(0x7F800000, 0xFF800000) == QNAN_INDEFINITE, "add inf - inf")
    # conversions
    chk(make_fp32(0x3F80) == one, "bf16 1.0")
    chk(cvt_fp16_to_fp32(0x3C00) == one, "fp16 1.0")
    chk(cvt_fp16_to_fp32(0x0001) == 0x33800000, "fp16 min denormal = 2^-24")
    chk(cvt_fp16_to_fp32(0x8400) == 0xB8800000, "fp16 -2^-14")
    chk(cvt_fp16_to_fp32(0x7BFF) == 0x477FE000, "fp16 65504")
    chk(cvt_fp16_to_fp32(0x7E01) == 0x7FC02000, "fp16 QNaN payload")
    chk(cvt_fp16_to_fp32(0xFC00) == 0xFF800000, "fp16 -inf")
    # FP16 denormal products are not flushed on input: 2^-24 * 2^-24 = 2^-48 (normal fp32)
    chk(fma32(0, cvt_fp16_to_fp32(1), cvt_fp16_to_fp32(1)) == 0x27800000, "2^-48")
    # LDTILECFG checks
    a = Amx()
    good = cfg_bytes({0: (16, 64), 1: (2, 4)})
    chk(not a.tilecfg_check(good)[0], "valid config")
    chk(a.tilecfg_check(cfg_bytes({0: (16, 65)}))[0], "colsb 65")
    chk(a.tilecfg_check(cfg_bytes({0: (17, 64)}))[0], "rows 17")
    chk(a.tilecfg_check(cfg_bytes({0: (0, 4)}))[0], "rows 0, colsb 4")
    chk(a.tilecfg_check(cfg_bytes({0: (1, 0)}))[0], "rows 1, colsb 0")
    chk(a.tilecfg_check(bytes([2]) + bytes(63))[0], "palette 2")
    chk(not a.tilecfg_check(bytes([0, 5, 1]) + bytes([0xFF] * 61))[0], "palette 0: rest ignored")
    bad = bytearray(good)
    bad[2] = 1
    chk(a.tilecfg_check(bytes(bad))[0], "reserved byte 2")
    bad = bytearray(good)
    bad[40] = 1
    chk(a.tilecfg_check(bytes(bad))[0], "reserved byte 40")
    bad = bytearray(good)
    bad[63] = 1
    chk(a.tilecfg_check(bytes(bad))[0], "reserved byte 63")
    a.xcr0 = 7
    chk(a.tilecfg_check(good)[0], "palette 1 without XCR0[18:17]")
    # INT8 dot product by hand: 1 row, K = 1, N = 1
    a = Amx()
    a.set_cfg((1, 0, [4, 4, 4, 0, 0, 0, 0, 0], [1, 1, 1, 0, 0, 0, 0, 0]))
    a.tiles[0][0:4] = bytes([1, 0, 0, 0])
    a.tiles[1][0:4] = bytes([0xFF, 2, 3, 4])            # -1, 2, 3, 4 (signed)
    a.tiles[2][0:4] = bytes([0xFF, 0xFF, 1, 2])         # -1, -1, 1, 2 (signed)
    a.TDPB("SS", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 1 + 1 - 2 + 3 + 8, "TDPBSSD by hand")
    a.tiles[0][0:4] = bytes(4)
    a.TDPB("UU", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 255 * 255 + 2 * 255 + 3 + 8, "TDPBUUD by hand")
    # complex by hand: (1 + 2i) * (3 + 4i) = -5 + 10i
    a = Amx()
    a.set_cfg((1, 0, [4, 4, 4, 0, 0, 0, 0, 0], [1, 1, 1, 0, 0, 0, 0, 0]))
    a.tiles[1][0:4] = bytes([0x00, 0x3C, 0x00, 0x40])   # re 1.0, im 2.0
    a.tiles[2][0:4] = bytes([0x00, 0x42, 0x00, 0x44])   # re 3.0, im 4.0
    a.TCMMRLFP16PS(0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0xC0A00000, "real part -5")
    a.tiles[0][0:4] = bytes(4)
    a.TCMMIMFP16PS(0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0x41200000, "imaginary part 10")
    # exception ordering: XFD #NM before TILES_CONFIGURED #UD; LDTILECFG never #NM
    a = Amx(xfd=1 << 18)
    try:
        a.TILEZERO(0)
        chk(False, "TILEZERO with XFD")
    except Fault as f:
        chk(f.vector == NM and a.xfd_err == 1 << 18, "XFD #NM, XFD_ERR")
    a.mem = {i: b for i, b in enumerate(good)}
    a.LDTILECFG(0)
    chk(a.tiles_configured(), "LDTILECFG with XFD armed")
    # decoder
    chk(decode(bytes(enc("TILELOADD", reg=3, base="rsi", index="rcx")))[0] == "TILELOADD",
        "decode TILELOADD")
    chk(decode(bytes([0x66] + enc("TILERELEASE")))[0] == "UD", "66 before VEX")
    chk(decode(bytes(enc("TDPBSSD", 1, 2, 3)))[1]["vvvv"] == 3, "vvvv tsrc2")
    print("selftest:", "PASS" if ok else "FAIL")
    return ok


# --------------------------------------------------------------------------
# vectors
# --------------------------------------------------------------------------
TMUL_OPS = ["TDPBSSD", "TDPBSUD", "TDPBUSD", "TDPBUUD", "TDPBF16PS", "TDPFP16PS",
            "TCMMIMFP16PS", "TCMMRLFP16PS"]
OP_MASK = {"TDPBSSD": AMX_INT8, "TDPBSUD": AMX_INT8, "TDPBUSD": AMX_INT8,
           "TDPBUUD": AMX_INT8, "TDPBF16PS": AMX_BF16, "TDPFP16PS": AMX_FP16,
           "TCMMIMFP16PS": AMX_COMPLEX, "TCMMRLFP16PS": AMX_COMPLEX}

# (M, K, N): tsrcdest M x 4N bytes, tsrc1 M x 4K, tsrc2 K x 4N
SHAPES = {"full": (16, 16, 16), "part": (5, 9, 6), "one": (1, 1, 1), "tall": (16, 3, 16),
          "k1": (8, 1, 16), "wide": (2, 16, 1)}


def run_tmul(a, op, d, s1, s2):
    if op.startswith("TDPB") and op != "TDPBF16PS":
        a.TDPB(op[4:6], d, s1, s2)
    else:
        getattr(a, op)(d, s1, s2)


def tmul_vectors():
    vecs = []
    seed = 0x5EED0000
    regsets = [(0, 1, 2), (7, 5, 3), (4, 6, 0), (2, 0, 1)]
    for op in TMUL_OPS:
        if op.startswith("TDPB") and op != "TDPBF16PS":
            combos = [("rand", K_BYTES, K_BYTES, K_BYTES)]
            shapes = ["full", "part", "one", "tall", "wide"]
        else:
            f = BF16 if op == "TDPBF16PS" else FP16
            combos = [("normal", kind(f, "NORMAL"), kind(f, "NORMAL"), kind(F32, "NORMAL")),
                      ("int", kind(f, "INT"), kind(f, "INT"), kind(F32, "INT")),
                      ("wide", kind(f, "WIDE"), kind(f, "WIDE"), kind(F32, "WIDE")),
                      ("tiny", kind(f, "TINY"), kind(f, "TINY"), kind(F32, "TINY")),
                      ("huge", kind(f, "HUGE"), kind(f, "HUGE"), kind(F32, "HUGE")),
                      ("special", kind(f, "SPECIAL"), kind(f, "SPECIAL"), kind(F32, "SPECIAL")),
                      ("dstspecial", kind(f, "NORMAL"), kind(f, "NORMAL"),
                       kind(F32, "SPECIAL"))]
            shapes = ["full", "part"]
        vi = 0
        for cname, ka, kb, kc in combos:
            for sname in shapes:
                if cname == "special":
                    sname = "k1" if sname == "full" else "one"
                M, K, N = SHAPES[sname]
                d, s1, s2 = regsets[vi % len(regsets)]
                vi += 1
                seed += 1
                shapes_cfg = {d: (M, 4 * N), s1: (M, 4 * K), s2: (K, 4 * N)}
                # the other tiles get valid shapes too (they must not matter)
                for t in range(8):
                    if t not in shapes_cfg:
                        shapes_cfg[t] = (1 + (t * 5 + vi) % 16, 4 * (1 + (t * 3 + vi) % 16))
                cfg = cfg_bytes(shapes_cfg, start_row=(vi * 3) % 16)
                kinds = [K_BYTES] * 8
                kinds[d], kinds[s1], kinds[s2] = kc, ka, kb
                tiles = [fill_tile(seed, t, kinds[t]) for t in range(8)]
                a = Amx()
                a.load_cfg_like_xrstor(cfg)
                for t in range(8):
                    a.tiles[t][:] = tiles[t]
                run_tmul(a, op, d, s1, s2)
                assert a.start_row == 0
                for t in range(8):
                    assert t == d or a.tiles[t] == tiles[t]
                vecs.append(dict(name="%s_%s_%s" % (op.lower(), cname, sname), op=op,
                                 code=enc(op, d, s1, s2), mask=AMX_TILE | OP_MASK[op],
                                 cfg=cfg, seed=seed, kinds=kinds,
                                 in_hash=fnv1a64(b"".join(tiles)), dst=d,
                                 exp=bytes(a.tiles[d])))
    return vecs


# exception vectors: the state is set with the Unicorn API, then one instruction runs
# with RSI = DATA + 0x1000 (a valid palette-1 config image there), RCX = 64,
# RDI = DATA + 0x4000.
EXC_CFG = cfg_bytes({0: (16, 64), 1: (16, 64), 2: (16, 64), 3: (4, 8), 4: (16, 6),
                     5: (4, 16), 6: (2, 64), 7: (16, 64)})


def exc_vectors():
    out = []

    def add(name, code, mask=AMX_ALL, xcr0=None, osxsave=True, xfd=0, mode64=True,
            cfg=EXC_CFG, mem_cfg=EXC_CFG):
        code = bytes(code)
        a = Amx(mask=mask, xcr0=0x60007 if xcr0 is None else xcr0, osxsave=osxsave, xfd=xfd,
                mode64=mode64)
        if not mask:
            a.xcr0 &= ~0x60000            # no AMX components in the model
        a.load_cfg_like_xrstor(cfg)
        base = 0x200000 + 0x1000
        for i, b in enumerate(mem_cfg):
            a.mem[base + i] = b
        name_, f = decode(code, mode64=mode64, mask=mask)
        vec = -1
        try:
            if name_ in ("UD", "LES"):
                raise Fault(UD)
            if name_ == "LDTILECFG":
                a.LDTILECFG(base)
            elif name_ == "STTILECFG":
                a.STTILECFG(0x200000 + 0x4000)
            elif name_ == "TILERELEASE":
                a.TILERELEASE()
            elif name_ == "TILEZERO":
                a.TILEZERO(f["reg"])
            elif name_ in ("TILELOADD", "TILELOADDT1"):
                a.TILELOADD(f["reg"], base, 64)
            elif name_ == "TILESTORED":
                a.TILESTORED(base, 64, f["reg"])
            else:
                run_tmul(a, name_, f["reg"], f["rm"], f["vvvv"])
        except Fault as e:
            vec = e.vector
        out.append(dict(name=name, code=code, mask=mask, xcr0=xcr0 or 0, osxsave=osxsave,
                        xfd=xfd, mode64=mode64, cfg=cfg, mem_cfg=mem_cfg, vec=vec,
                        xfd_err=a.xfd_err, post_cfg=a.cfg_image()))

    one = {"LDTILECFG": enc("LDTILECFG", base="rsi"),
           "STTILECFG": enc("STTILECFG", base="rdi"),
           "TILERELEASE": enc("TILERELEASE"),
           "TILEZERO": enc("TILEZERO", reg=7),
           "TILELOADD": enc("TILELOADD", reg=1, base="rsi", index="rcx"),
           "TILELOADDT1": enc("TILELOADDT1", reg=2, base="rsi", index="rcx"),
           "TILESTORED": enc("TILESTORED", reg=0, base="rsi", index="rcx"),
           "TDPBSSD": enc("TDPBSSD", 0, 1, 2), "TDPBSUD": enc("TDPBSUD", 0, 1, 2),
           "TDPBUSD": enc("TDPBUSD", 0, 1, 2), "TDPBUUD": enc("TDPBUUD", 0, 1, 2),
           "TDPBF16PS": enc("TDPBF16PS", 0, 1, 2), "TDPFP16PS": enc("TDPFP16PS", 0, 1, 2),
           "TCMMIMFP16PS": enc("TCMMIMFP16PS", 0, 1, 2),
           "TCMMRLFP16PS": enc("TCMMRLFP16PS", 0, 1, 2)}
    for n, c in one.items():
        f = FORMS[n]
        add(n + "_ok", c)
        add(n + "_amx_off", c, mask=0)
        add(n + "_xcr0_no_amx", c, xcr0=0x7)
        add(n + "_no_osxsave", c, osxsave=False)
        add(n + "_32bit", c, mode64=False)
        for p in (0x66, 0xF2, 0xF3, 0xF0, 0x41):
            add(n + "_prefix_%02x" % p, [p] + c)
        add(n + "_67", [0x67] + c)                # address size: allowed
        add(n + "_vexL1", _patch_vex(c, L=1))
        add(n + "_vexW1", _patch_vex(c, W=1))
        if f[2] != "rrr":
            add(n + "_vvvv", _patch_vex(c, vvvv=5))
        add(n + "_xfd", c, xfd=1 << 18)
        add(n + "_unconfigured", c, cfg=bytes(64))
        add(n + "_xfd_unconfigured", c, xfd=1 << 18, cfg=bytes(64))
        if f[3] != AMX_TILE:
            add(n + "_no_feature", c, mask=AMX_ALL & ~f[3])
        if f[2] == "rrr":
            add(n + "_tile_only", c, mask=AMX_TILE)
    # fixed ModRM fields and the other pp/mod combinations
    add("ldtilecfg_reg1", enc("LDTILECFG", reg=1, base="rsi"))
    add("sttilecfg_reg7", enc("STTILECFG", reg=7, base="rdi"))
    add("tilerelease_c1", enc("TILERELEASE", modrm=0xC1))
    add("tilerelease_c8", enc("TILERELEASE", modrm=0xC8))
    add("tilerelease_vexR", _patch_vex(enc("TILERELEASE"), R=1))
    add("tilezero_rm1", enc("TILEZERO", reg=2, modrm=0xD1))
    add("tilezero_tmm8", enc("TILEZERO", reg=8))
    add("tilezero_tmm15", enc("TILEZERO", reg=15))
    add("tilezero_invalid", enc("TILEZERO", reg=1), cfg=cfg_bytes({0: (16, 64)}))
    add("tileloadd_nosib", enc("TILELOADD", reg=1, base="rsi", nosib=True))
    add("tileloadd_noindex", enc("TILELOADD", reg=1, base="rsi"))
    add("tileloadd_index_r12", enc("TILELOADD", reg=1, base="rsi", index="r12"))
    add("tileloadd_mod3", enc("TILELOADD", reg=1, base="rsi", index="rcx")[:4] + [0xCC])
    add("tileloadd_tmm9", enc("TILELOADD", reg=9, base="rsi", index="rcx"))
    add("tileloadd_colsb6", enc("TILELOADD", reg=4, base="rsi", index="rcx"))
    add("tileloadd_invalid", enc("TILELOADD", reg=3, base="rsi", index="rcx"),
        cfg=cfg_bytes({0: (16, 64)}))
    add("tileloadd_startrow_lt", enc("TILELOADD", reg=3, base="rsi", index="rcx"),
        cfg=_with_start(EXC_CFG, 3))
    add("tileloadd_startrow_eq", enc("TILELOADD", reg=3, base="rsi", index="rcx"),
        cfg=_with_start(EXC_CFG, 4))
    add("tilestored_startrow_eq", enc("TILESTORED", reg=6, base="rsi", index="rcx"),
        cfg=_with_start(EXC_CFG, 2))
    add("tilestored_colsb6", enc("TILESTORED", reg=4, base="rsi", index="rcx"))
    add("tdpbssd_startrow_any", enc("TDPBSSD", 0, 1, 2), cfg=_with_start(EXC_CFG, 15))
    add("op49_f3_mem", enc("LDTILECFG", base="rsi", pp=PP["F3"]))
    add("op49_66_reg", enc("TILERELEASE", pp=PP["66"]))
    add("op49_f3_reg", enc("TILERELEASE", pp=PP["F3"]))
    add("op4b_np", enc("TILELOADD", reg=1, base="rsi", index="rcx", pp=PP["NP"]))
    add("op5c_np", enc("TDPBF16PS", 0, 1, 2, pp=PP["NP"]))
    add("op5c_66", enc("TDPBF16PS", 0, 1, 2, pp=PP["66"]))
    add("op6c_f3", enc("TCMMIMFP16PS", 0, 1, 2, pp=PP["F3"]))
    add("op6c_f2", enc("TCMMIMFP16PS", 0, 1, 2, pp=PP["F2"]))
    add("op5e_mem", enc("TILELOADD", reg=0, base="rsi", index="rcx", pp=PP["F2"])[:3] +
        [0x5E] + enc("TILELOADD", reg=0, base="rsi", index="rcx")[4:])
    add("map_0f3a_49", _patch_map(enc("TILERELEASE"), 3))
    add("legacy_0f38_49", [0x0F, 0x38, 0x49, 0xC0])
    # TMUL register and shape rules (AMX-E4)
    add("tdp_dst_eq_src1", enc("TDPBSSD", 1, 1, 2))
    add("tdp_src1_eq_src2", enc("TDPBSSD", 0, 2, 2))
    add("tdp_dst_eq_src2", enc("TDPBSSD", 2, 1, 2))
    add("tdp_dst8", enc("TDPBSSD", 8, 1, 2))
    add("tdp_src1_9", enc("TDPBSSD", 0, 9, 2))
    add("tdp_src2_10", enc("TDPBSSD", 0, 1, 10))
    add("tdp_dst_eq_src1_xfd", enc("TDPBSSD", 1, 1, 2), xfd=1 << 18)
    add("tdp_colsb6", enc("TDPBF16PS", 4, 1, 2))
    add("tdp_invalid_src2", enc("TDPBF16PS", 0, 1, 2),
        cfg=cfg_bytes({0: (16, 64), 1: (16, 64)}))
    for (nm, sh) in [("dst_colsb_ne_src2", {0: (4, 16), 1: (4, 16), 2: (4, 32)}),
                     ("dst_rows_ne_src1", {0: (4, 16), 1: (5, 16), 2: (4, 16)}),
                     ("src1_k_ne_src2_rows", {0: (4, 16), 1: (4, 12), 2: (4, 16)}),
                     ("shape_ok_small", {0: (4, 16), 1: (4, 12), 2: (3, 16)}),
                     ("shape_ok_k16", {0: (2, 4), 1: (2, 64), 2: (16, 4)}),
                     ("shape_ok_m16", {0: (16, 64), 1: (16, 4), 2: (1, 64)})]:
        for op in ("TDPBUUD", "TDPBF16PS", "TDPFP16PS", "TCMMRLFP16PS"):
            add("%s_%s" % (op.lower(), nm), enc(op, 0, 1, 2), cfg=cfg_bytes(sh))
    # LDTILECFG configuration matrix (#GP / loaded)
    for (nm, img) in ldtilecfg_images():
        add("ldtilecfg_" + nm, enc("LDTILECFG", base="rsi"), mem_cfg=img)
    return out


def _with_start(cfg, start_row):
    b = bytearray(cfg)
    b[1] = start_row
    return bytes(b)


def _patch_vex(code, L=None, W=None, vvvv=None, R=None):
    c = list(code)
    i = c.index(0xC4)
    if L is not None:
        c[i + 2] = (c[i + 2] & ~4) | (L << 2)
    if W is not None:
        c[i + 2] = (c[i + 2] & 0x7F) | (W << 7)
    if vvvv is not None:
        c[i + 2] = (c[i + 2] & 0x87) | ((~vvvv & 15) << 3)
    if R is not None:
        c[i + 1] = (c[i + 1] & 0x7F) | (0 if R else 0x80)
    return c


def _patch_map(code, m):
    c = list(code)
    i = c.index(0xC4)
    c[i + 1] = (c[i + 1] & 0xE0) | m
    return c


def ldtilecfg_images():
    good = cfg_bytes({0: (16, 64), 1: (1, 4), 7: (3, 12)})
    imgs = [("good", good), ("good_start5", _with_start(good, 5)),
            ("good_start255", _with_start(good, 255)),
            ("palette0", bytes(64)),
            ("palette0_garbage", bytes([0, 9, 7]) + bytes([0xFF] * 61)),
            ("palette2", bytes([2]) + good[1:]),
            ("palette255", bytes([255]) + good[1:]),
            ("colsb65", cfg_bytes({0: (16, 65)})),
            ("colsb_hi", cfg_bytes({3: (1, 0x104)})),
            ("rows17", cfg_bytes({5: (17, 4)})),
            ("rows0_colsb4", cfg_bytes({2: (0, 4)})),
            ("rows1_colsb0", cfg_bytes({2: (1, 0)})),
            ("colsb_odd", cfg_bytes({2: (2, 3)})),          # not a LDTILECFG check
            ("all_unused", cfg_bytes({}))]
    for off in (2, 15, 32, 47, 56, 63):
        b = bytearray(good)
        b[off] = 0x80
        imgs.append(("reserved_%d" % off, bytes(b)))
    return imgs


# --------------------------------------------------------------------------
# output
# --------------------------------------------------------------------------

def cbytes(b):
    return "{" + ",".join("0x%02x" % x for x in b) + "}"


def chex(b):
    return '"' + "".join("%02x" % x for x in b) + '"'


def emit_cinc():
    w = sys.stdout.write
    w("/*\n * Generated by Emulator/tools/isa/ref_amx.py --cinc (independent SDM reference model);\n"
      " * regenerate instead of editing. Used by test_x86.c (test_x86_amx_*).\n *\n"
      " * x86_amx_tvecs: one TMUL instruction on tiles filled by amx_fill_tile(seed, t, kind)\n"
      " * (splitmix64, identical in ref_amx.py); in_hash = FNV-1a 64 of the 8 KB filled; cfg is\n"
      " * written with UC_X86_REG_TILECFG; exp = the destination tile afterwards (1024 bytes),\n"
      " * every other tile unchanged, start_row 0.\n"
      " * x86_amx_evecs: the exception matrix; state set with the API (mask = UC_CTL_X86_AMX,\n"
      " * xcr0 0 = reset value, osxsave 0 = CR4.OSXSAVE cleared, xfd = IA32_XFD, cfg = TILECFG),\n"
      " * mem_cfg at DATA + 1000h (RSI), RCX = 64, RDI = DATA + 4000h; vec = expected vector\n"
      " * (-1 none, 6 #UD, 7 #NM, 13 #GP), xfd_err = IA32_XFD_ERR afterwards, post_cfg =\n"
      " * the TILECFG image (UC_X86_REG_TILECFG) afterwards.\n */\n\n")
    w("#define AMX_K_ZERO 0\n#define AMX_K_BYTES 1\n")
    w("/* kinds 2..19: 2 + 6 * format (0 BF16, 1 FP16, 2 FP32) + class "
      "(NORMAL INT WIDE TINY HUGE SPECIAL) */\n\n")
    w("struct x86_amx_tvec {\n    const char *name;\n    uint8_t code[8];\n    int code_len;\n"
      "    int mask;\n    const char *cfg;\n    uint64_t seed;\n    uint8_t kind[8];\n"
      "    uint64_t in_hash;\n    int dst;\n    const char *exp;\n};\n\n")
    w("static const struct x86_amx_tvec x86_amx_tvecs[] = {\n")
    for v in tmul_vectors():
        w("    { \"%s\", %s, %d, %d,\n      %s,\n      0x%016xULL, %s, 0x%016xULL, %d,\n      %s },\n"
          % (v["name"], cbytes(v["code"]), len(v["code"]), v["mask"], chex(v["cfg"]),
             v["seed"],cbytes(v["kinds"]), v["in_hash"], v["dst"], chex(v["exp"])))
    w("};\n\n")
    w("struct x86_amx_evec {\n    const char *name;\n    uint8_t code[16];\n    int code_len;\n"
      "    int mask;\n    uint64_t xcr0;\n    int osxsave;\n    uint64_t xfd;\n    int mode64;\n"
      "    const char *cfg;\n    const char *mem_cfg;\n    int vec;\n    uint64_t xfd_err;\n"
      "    const char *post_cfg;\n};\n\n")
    w("static const struct x86_amx_evec x86_amx_evecs[] = {\n")
    for v in exc_vectors():
        w("    { \"%s\", %s, %d, %d, 0x%xULL, %d, 0x%xULL, %d,\n      %s,\n      %s,\n"
          "      %d, 0x%xULL,\n      %s },\n"
          % (v["name"], cbytes(v["code"]), len(v["code"]), v["mask"], v["xcr0"],
             1 if v["osxsave"] else 0, v["xfd"], 1 if v["mode64"] else 0, chex(v["cfg"]),
             chex(v["mem_cfg"]), v["vec"], v["xfd_err"], chex(v["post_cfg"])))
    w("};\n")


def emit_cases():
    """emu-alltest expected-value cases: complete tile programs on the 64 KiB operand
    memory (RSI = MEM+8000h: TILECFG at +0, tsrc1 at +100h, tsrc2 at +500h; RDI = MEM+9000h:
    tsrcdest), stride RCX; the result is stored back over tsrcdest and the tiles released."""
    w = sys.stdout.write
    w("# Intel AMX (VEX) expected-value cases (ledger U170-U180)\n"
      "# Generated by Emulator/tools/isa/ref_amx.py --cases (independent SDM model); regenerate, do\n"
      "# not edit. The i5-13600K has no AMX: expected-value cases only, run with AMX enabled:\n"
      "#   emu-alltest --cases Emulator\\data\\cases_amx.txt --amx --expect-only\n"
      "# Each program: LDTILECFG [rsi]; TILELOADD tmm1, [rsi+rcx+100h]; TILELOADD tmm2,\n"
      "# [rsi+rcx+500h]; TILELOADD tmm0, [rdi+rcx]; <op> tmm0, tmm1, tmm2; TILESTORED [rdi+rcx],\n"
      "# tmm0; TILERELEASE (RSI = MEM+0x8000, RDI = MEM+0x9000, RCX = stride).\n")
    seed = 0xCA5E0000
    MEM = 0x10000000          # any base: only offsets matter

    def prog(op):
        c = []
        c += enc("LDTILECFG", base="rsi")
        c += enc("TILELOADD", reg=1, base="rsi", index="rcx", disp=0x100)
        c += enc("TILELOADD", reg=2, base="rsi", index="rcx", disp=0x500)
        c += enc("TILELOADD", reg=0, base="rdi", index="rcx")
        c += enc(op, 0, 1, 2)
        c += enc("TILESTORED", reg=0, base="rdi", index="rcx")
        c += enc("TILERELEASE")
        return c

    cases = []
    for op in TMUL_OPS:
        if op.startswith("TDPB") and op != "TDPBF16PS":
            sets = [("rand", K_BYTES, K_BYTES, K_BYTES)]
        else:
            f = BF16 if op == "TDPBF16PS" else FP16
            sets = [("normal", kind(f, "NORMAL"), kind(f, "NORMAL"), kind(F32, "NORMAL")),
                    ("int", kind(f, "INT"), kind(f, "INT"), kind(F32, "INT")),
                    ("wide", kind(f, "WIDE"), kind(f, "WIDE"), kind(F32, "WIDE"))]
        for (cname, ka, kb, kc) in sets:
            for (M, K, N, stride) in [(4, 4, 4, 64), (3, 2, 5, 24), (16, 16, 16, 64)]:
                seed += 1
                cfg = cfg_bytes({0: (M, 4 * N), 1: (M, 4 * K), 2: (K, 4 * N)})
                A = fill_tile(seed, 1, ka)
                B = fill_tile(seed, 2, kb)
                C = fill_tile(seed, 0, kc)
                mem = {}

                def put(off, data):
                    for i, b in enumerate(data):
                        mem[off + i] = b
                put(0x8000, cfg)
                # memory images: row r of a tile at base + r*stride, colsb bytes
                for (off, t, rows, colsb) in [(0x8100, A, M, 4 * K), (0x8500, B, K, 4 * N),
                                              (0x9000, C, M, 4 * N)]:
                    for r in range(rows):
                        put(off + r * stride, t[64 * r: 64 * r + colsb])
                a = Amx()
                for k_, v_ in mem.items():
                    a.mem[MEM + k_] = v_
                rsi, rdi = MEM + 0x8000, MEM + 0x9000
                a.LDTILECFG(rsi)
                a.TILELOADD(1, rsi + 0x100, stride)
                a.TILELOADD(2, rsi + 0x500, stride)
                a.TILELOADD(0, rdi, stride)
                run_tmul(a, op, 0, 1, 2)
                a.TILESTORED(rdi, stride, 0)
                a.TILERELEASE()
                code = prog(op)
                ins = ["rcx=%d" % stride]
                spans =[(0x8000, 0x8040), (0x8100, 0x8100 + (M - 1) * stride + 4 * K),
                         (0x8500, 0x8500 + (K - 1) * stride + 4 * N),
                         (0x9000, 0x9000 + (M - 1) * stride + 4 * N)]
                for lo, hi in spans:
                    ins.append("m+0x%X=%s" % (lo, "".join("%02X" % mem.get(i, 0)
                                                           for i in range(lo, hi))))
                lo, hi = spans[3]
                res = "".join("%02X" % a.mem.get(MEM + i, 0) for i in range(lo, hi))
                cases.append("# %s_%s_%dx%dx%d_stride%d\n.byte %s | %s => m+0x%X=%s\n" % (
                    op.lower(), cname, M, K, N, stride,
                    ", ".join("0x%02x" % b for b in code), " ".join(ins), lo, res))
    # U596: FTZ at the smallest normal. M = N = 1, K = 2, stride 8: temp1.fp32[1] (the odd
    # products) = fma32(fma32(0, 2^-63, 2^-63), x, y) = fma32(2^-126, x, y) with x*y = -+2^-150
    # (BF16 1A00h = 2^-75) or -+2^-152 (1980h = 2^-76); the even products are 0 * 0. 2^-126 -
    # 2^-150 is tiny after rounding with an unbounded exponent (SDM Vol1 11.5.2.5) -> a zero of
    # its sign; 2^-126 - 2^-152 rounds to 2^-126 and is kept. The dot product is then added to
    # the accumulator 0 (add32). FP16 products are multiples of 2^-48 with magnitude >= 2^-48,
    # so TDPFP16PS / TCMM*FP16PS / VDPPHPS cannot reach this boundary.
    for (cname, x0, xo, yo) in [("ftz_tiny_pos", 0x2000, 0x1A00, 0x9A00),
                                ("ftz_tiny_neg", 0xA000, 0x9A00, 0x9A00),
                                ("ftz_kept", 0x2000, 0x1980, 0x9980),
                                ("ftz_kept_neg", 0xA000, 0x9980, 0x9980)]:
        op, stride = "TDPBF16PS", 8
        cfg = cfg_bytes({0: (1, 4), 1: (1, 8), 2: (2, 4)})
        A = bytes([0, 0, x0 & 0xFF, x0 >> 8, 0, 0, xo & 0xFF, xo >> 8])     # k = 0, 1
        B = bytes([0, 0, 0x00, 0x20] + [0] * (stride - 4) + [0, 0, yo & 0xFF, yo >> 8])
        C = bytes(4)
        mem = {}
        for (off, data) in [(0x8000, cfg), (0x8100, A), (0x8500, B), (0x9000, C)]:
            for i, b_ in enumerate(data):
                mem[off + i] = b_
        a_ = Amx()
        for k_, v_ in mem.items():
            a_.mem[MEM + k_] = v_
        rsi, rdi = MEM + 0x8000, MEM + 0x9000
        a_.LDTILECFG(rsi)
        a_.TILELOADD(1, rsi + 0x100, stride)
        a_.TILELOADD(2, rsi + 0x500, stride)
        a_.TILELOADD(0, rdi, stride)
        run_tmul(a_, op, 0, 1, 2)
        a_.TILESTORED(rdi, stride, 0)
        a_.TILERELEASE()
        ins = ["rcx=%d" % stride]
        for lo, hi in [(0x8000, 0x8040), (0x8100, 0x8108), (0x8500, 0x8500 + stride + 4),
                       (0x9000, 0x9004)]:
            ins.append("m+0x%X=%s" % (lo, "".join("%02X" % mem.get(i, 0) for i in range(lo, hi))))
        res = "".join("%02X" % a_.mem.get(MEM + i, 0) for i in range(0x9000, 0x9004))
        cases.append("# %s_%s_1x2x1_stride8 (U596)\n.byte %s | %s => m+0x9000=%s\n" % (
            op.lower(), cname, ", ".join("0x%02x" % b_ for b_ in prog(op)), " ".join(ins), res))
    # faults: an LDTILECFG #GP, a TMUL shape #UD (state saved at the faulting instruction)
    bad = cfg_bytes({0: (17, 64)})
    cases.append("# ldtilecfg_rows17_gp\n.byte %s | m+0x8000=%s => #GP\n" % (
        ", ".join("0x%02x" % b for b in enc("LDTILECFG", base="rsi")),
        "".join("%02X" % b for b in bad)))
    c = enc("LDTILECFG", base="rsi") + enc("TDPBSSD", 0, 1, 2)
    cfg = cfg_bytes({0: (4, 16), 1: (5, 16), 2: (4, 16)})
    cases.append("# tdpbssd_rows_mismatch_ud (after LDTILECFG)\n.byte %s | m+0x8000=%s => #UD\n"
                 % (", ".join("0x%02x" % b for b in c), "".join("%02X" % b for b in cfg)))
    c = enc("STTILECFG", base="rdi")
    cases.append("# sttilecfg_unconfigured_zeros\n.byte %s | m+0x9000=%s => m+0x9000=%s\n"
                 % (", ".join("0x%02x" % b for b in c), "AB" * 64, "00" * 64))
    cfg = cfg_bytes({0: (16, 64), 3: (2, 8)}, start_row=0)
    c = enc("LDTILECFG", base="rsi") + enc("STTILECFG", base="rdi") + enc("TILERELEASE")
    cases.append("# ldtilecfg_sttilecfg_roundtrip\n.byte %s | m+0x8000=%s => m+0x9000=%s\n"
                 % (", ".join("0x%02x" % b for b in c), "".join("%02X" % b for b in cfg),
                    "".join("%02X" % b for b in cfg)))
    for s in cases:
        w(s)


def main():
    if "--selftest" in sys.argv:
        sys.exit(0 if selftest() else 1)
    if "--cinc" in sys.argv:
        emit_cinc()
        return
    if "--cases" in sys.argv:
        emit_cases()
        return
    print(__doc__)


if __name__ == "__main__":
    main()
