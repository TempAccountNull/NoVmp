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

U720-U727 (ISE 319433-062 ch. 3, 1.7; SDM Vol1 13.11-13.14, Vol2D XSAVES/XRSTORS): AMX-MOVRS
(TILELOADDRS[T1], VEX and the APX-promoted EVEX form), AMX-FP8 (TDP[B,H,BH,HB]F8PS: the fixed-
point helpers convert_bf8/hf8_to_int64 and convert_int128_to_fp32 literally, the INF / NaN
treatment of the pseudocode, "DAZ==0 ... FTZ==1" for the accumulation), AMX-AVX512 (TCVTROWD2PS,
TCVTROWPS2BF16H/L, TCVTROWPS2PHH/L, TILEMOVROW; AMX-E7/E8-EVEX), XSAVES / XRSTORS of the AMX
components and IA32_XSS. Choices the documents leave open (made by the emulator too): INF * INF
= INF (IEEE); TILEMOVROW r32 is AMX-E8-EVEX and imm8 AMX-E7-EVEX (ISE Table lists them swapped,
as XED does not); E7/E8 order: XCR0 #UD, CR0.TS #NM, XFD #NM, TILECFG #UD.

Usage:
  python ref_amx.py --selftest   hand-derived checks of the model, exit 0 on pass
  python ref_amx.py --cinc       unicorn/tests/unit/x86_amx_vectors.inc (stdout)
  python ref_amx.py --cases      Emulator/data/cases_amx.txt (stdout)
  python ref_amx.py --cinc2      unicorn/tests/unit/x86_amx2_vectors.inc (stdout)
  python ref_amx.py --cases2     Emulator/data/cases_amx2.txt (stdout)

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

# UC_CTL_X86_AMX bits (unicorn.h, U170; U720: FP8 / AVX512 / MOVRS)
AMX_TILE, AMX_INT8, AMX_BF16, AMX_FP16, AMX_COMPLEX = 1, 2, 4, 8, 16
AMX_FP8, AMX_AVX512, AMX_MOVRS = 32, 64, 128
AMX_ALL = 255
AMX_V1 = 31        # the U170-U180 families: the mask of x86_amx_vectors.inc / cases_amx.txt

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

    def add(name, code, mask=AMX_V1, xcr0=None, osxsave=True, xfd=0, mode64=True,
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
            add(n + "_no_feature", c, mask=AMX_V1 & ~f[3])
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


# ==========================================================================
# U720-U727: AMX-FP8, AMX-AVX512, AMX-MOVRS (ISE 319433-062 ch. 3), XSAVES / XRSTORS
# (SDM Vol1 13.11 / 13.12, Vol2D). Transcribed from the documents, not from the C code.
# ==========================================================================
BF8, HF8 = "bf8", "hf8"      # E5M2 / E4M3 (ISE 1.7: "denoted by BF8 ... and HF8")


def fp8_class(x, t):
    """ISE Table 1-12: E5M2 infinity S.11111.00, NaN S.11111.{01,10,11}; E4M3 NaN S.1111.111,
    no infinity; everything else is a number (zero, denormal, normal)"""
    if t == BF8:
        if (x & 0x7C) == 0x7C:
            return "inf" if (x & 3) == 0 else "nan"
        return "num"
    return "nan" if (x & 0x7F) == 0x7F else "num"


def convert_bf8_to_int64(x):
    """ISE 3.4 convert_bf8_to_int64: value is 2^16 * in"""
    sign = (x & 0x80) >> 7
    exp = (x & 0x7C) >> 2
    frac = x & 0x03
    mant = frac if exp == 0 else (frac | 0x4)
    e_count = 0 if exp == 0 else exp - 1
    mag = mant << e_count
    return -mag if sign else mag


def convert_hf8_to_int64(x):
    """ISE 3.4 convert_hf8_to_int64: value is 2^9 * in"""
    sign = (x & 0x80) >> 7
    exp = (x & 0x78) >> 3
    frac = x & 0x07
    mant = frac if exp == 0 else (frac | 0x8)
    e_count = 0 if exp == 0 else exp - 1
    mag = mant << e_count
    return -mag if sign else mag


def convert_fp8_to_int64(x, t):
    return convert_bf8_to_int64(x) if t == BF8 else convert_hf8_to_int64(x)


def convert_int128_to_fp32(v, type1, type2):
    """ISE 3.4 convert_int128_to_fp32, literally (v: a Python int, |v| < 2^127)"""
    if v == 0:
        return 0
    m128 = (1 << 128) - 1
    vin = v & m128
    sign = vin >> 127
    magnitude = ((-vin) & m128) if sign else vin
    jbit_position = 126
    while (magnitude >> 126) & 1 == 0:
        jbit_position -= 1
        magnitude = (magnitude << 1) & m128
    sticky = 1 if magnitude & ((1 << 102) - 1) else 0
    gbit = (magnitude >> 102) & 1
    lbit = (magnitude >> 103) & 1
    rnd_add1 = gbit & (lbit | sticky)
    mantissa = (magnitude >> 103) & ((1 << 25) - 1)
    rnd_mantissa = mantissa + rnd_add1
    ovf = rnd_mantissa >> 24
    if (type1, type2) == (BF8, BF8):
        factor = 32
    elif (type1, type2) == (HF8, HF8):
        factor = 18
    else:
        factor = 25
    exp = 127 + jbit_position - factor + ovf
    frac = rnd_mantissa & 0x7FFFFF
    return (sign << 31) | (exp << 23) | frac


def add32_nodaz(a, b):
    """tsrcdest.fp32[n] + tmpf32 of TDP*F8PS: "For the inputs, DAZ==0 is assumed, for the output
    FTZ==1 is assumed", RNE (FTZ as round_f32_ftz, U596). NaN operands are excluded by the caller."""
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


def cvt_fp32_to_bfloat16(x):
    """ISE 3.4 convert_fp32_to_bfloat16, literally"""
    e, f = f32_exp(x), f32_frac(x)
    if e == 0:                                   # zero or denormal
        return ((x >> 31) & 1) << 15
    if e == 0xFF and f == 0:                     # infinity
        return (x >> 16) & 0xFFFF
    if e == 0xFF:                                # NaN: truncate, force QNaN (dest[6] := 1)
        return ((x >> 16) & 0xFFFF) | 0x40
    lsb = (x >> 16) & 1
    rounding_bias = 0x00007FFF + lsb
    temp = (x + rounding_bias) & 0xFFFFFFFF
    return (temp >> 16) & 0xFFFF


def round_f16(v):
    """RNE of the exact rational v != 0 to binary16, gradual underflow, overflow to infinity"""
    sign = 1 if v < 0 else 0
    a = -v if v < 0 else v
    n, d = a.numerator, a.denominator
    e = n.bit_length() - d.bit_length()
    while n * (1 << max(-e, 0)) < d * (1 << max(e, 0)):
        e -= 1
    while n * (1 << max(-(e + 1), 0)) >= d * (1 << max(e + 1, 0)):
        e += 1
    q_exp = -24 if e < -14 else e - 10
    num = n * (1 << max(-q_exp, 0))
    den = d * (1 << max(q_exp, 0))
    q, r = divmod(num, den)
    if 2 * r > den or (2 * r == den and (q & 1)):
        q += 1
    if e < -14:
        return (sign << 15) | q                  # q = 2^10: the smallest normal
    if q == (1 << 11):
        q >>= 1
        e += 1
    if e > 15:
        return (sign << 15) | 0x7C00
    return (sign << 15) | ((e + 15) << 10) | (q - (1 << 10))


def vcvt_s2h_rne(x):
    """vCvt_s2h with RNE (TCVTROWPS2PH*): "Input FP32 denormals become FP16 zeros on outputs. This
    instruction can produce FP16 denormal outputs"; NaN: SDM Vol1 Table 14-9 (sign kept, exponent
    1FH, the significand truncated by its 13 low fraction bits, SNaN made quiet)"""
    s, e, f = (x >> 31) & 1, f32_exp(x), f32_frac(x)
    if e == 0:
        return s << 15
    if e == 0xFF:
        if f == 0:
            return (s << 15) | 0x7C00
        return (s << 15) | 0x7C00 | 0x200 | (f >> 13)
    return round_f16(f32_value(x))


def cvt_int32_to_fp32(x):
    """Convert_Integer_To_Single_Precision_Floating_Point, RNE"""
    v = x - (1 << 32) if x & 0x80000000 else x
    return 0 if v == 0 else round_f32(Fraction(v))


ROWOPS = ["TCVTROWD2PS", "TCVTROWPS2BF16H", "TCVTROWPS2BF16L", "TCVTROWPS2PHH", "TCVTROWPS2PHL",
          "TILEMOVROW"]


class Amx2(Amx):
    """Amx + the ISE 319433-062 instructions; zmm: dict of 64-byte values; cr0_ts: CR0.TS"""

    def __init__(self, mask=AMX_ALL, xcr0=0x600E7, cr0_ts=False, **kw):
        Amx.__init__(self, mask=mask, xcr0=xcr0, **kw)
        self.cr0_ts = cr0_ts
        self.zmm = {}

    # AMX-MOVRS: TILELOADDRS[T1] - the Operation of TILELOADD (start_row, zero_upper_rows,
    # write_row_and_zero, zero_tilecfg_start); the read-shared hint has no architectural effect
    def TILELOADDRS(self, tdest, base_disp, stride):
        Amx.TILELOADD(self, tdest, base_disp, stride)

    TILELOADDRST1 = TILELOADDRS

    # AMX-FP8: TDP[B,H,BH,HB]F8PS (AMX-E4)
    def TDPF8(self, kind, d, s1, s2):
        """kind: 'BB' TDPBF8PS, 'BH' TDPBHF8PS, 'HB' TDPHBF8PS, 'HH' TDPHF8PS (tsrc1, tsrc2 types)"""
        self.check_e4(d, s1, s2)
        type1 = BF8 if kind[0] == "B" else HF8
        type2 = BF8 if kind[1] == "B" else HF8
        T = self.tiles
        elements_src1 = self.colsb[s1] // 4
        elements_dest = self.colsb[d] // 4
        for m in range(self.rows[d]):
            temp1 = [0] * elements_dest
            nan = [False] * elements_dest
            pinf = [False] * elements_dest
            ninf = [False] * elements_dest
            inval = [False] * elements_dest
            for k in range(elements_src1):
                for n in range(elements_dest):
                    temp = 0
                    for i in range(4):
                        x = T[s1][64 * m + 4 * k + i]
                        y = T[s2][64 * k + 4 * n + i]
                        cx, cy = fp8_class(x, type1), fp8_class(y, type2)
                        if cx == "nan" or cy == "nan":
                            nan[n] = True              # NaN treatment
                        elif cx == "inf" or cy == "inf":
                            zx = cx == "num" and convert_fp8_to_int64(x, type1) == 0
                            zy = cy == "num" and convert_fp8_to_int64(y, type2) == 0
                            if zx or zy:
                                inval[n] = True        # mult(INF, ZERO) = QNaN_indefinite
                            elif (x >> 7) != (y >> 7):
                                ninf[n] = True         # mult(INF, x): sign of INF.sign ^ x.sign
                            else:
                                pinf[n] = True
                        else:
                            temp += convert_fp8_to_int64(x, type1) * convert_fp8_to_int64(y, type2)
                    temp1[n] += temp
            data = bytearray(T[d][64 * m: 64 * m + 64])
            for n in range(elements_dest):
                acc = u32(data, 4 * n)
                if nan[n] or f32_is_nan(acc) or inval[n] or (pinf[n] and ninf[n]):
                    res = QNAN_INDEFINITE              # add(+INF, -INF) = QNaN_indefinite
                else:
                    if pinf[n]:
                        tmpf32 = 0x7F800000
                    elif ninf[n]:
                        tmpf32 = 0xFF800000
                    else:
                        tmpf32 = convert_int128_to_fp32(temp1[n], type1, type2)
                    res = add32_nodaz(acc, tmpf32)
                put32(data, 4 * n, res)
            self.write_row_and_zero(d, m, data, self.colsb[d])
        self.zero_upper_rows(d, self.rows[d])
        self.zero_tilecfg_start()

    # AMX-AVX512 (AMX-E7-EVEX imm8 / AMX-E8-EVEX r32), the run-time part
    def check_e78(self, t):
        if not self.osxsave or (self.xcr0 & 0x60000) != 0x60000:
            raise Fault(UD)
        if (self.xcr0 & 0xE0) != 0xE0 or (self.xcr0 & 0x6) != 0x6:
            raise Fault(UD)                            # XCR0[7:5] != 111b, XCR0[2:1] != 11b
        if self.cr0_ts:
            raise Fault(NM)                            # CR0.TS (IA32_XFD_ERR unchanged)
        self.check_xfd()
        if not self.tiles_configured():
            raise Fault(UD)
        if t >= PALETTE[self.palette_id]["max_names"] or not self.valid(t):
            raise Fault(UD)
        if self.colsb[t] % 4 != 0:
            raise Fault(UD)

    def ROWOP(self, op, zdest, tsrc, row):
        """TCVTROWD2PS / TCVTROWPS2BF16H/L / TCVTROWPS2PHH/L / TILEMOVROW zdest, tsrc, r32|imm8"""
        self.check_e78(tsrc)
        out = bytearray(64)                            # VL = 512
        row_index = row & 0xF
        if row_index < self.rows[tsrc]:
            src = self.tiles[tsrc][64 * row_index: 64 * row_index + 64]
            if op == "TILEMOVROW":
                for i in range(64):
                    out[i] = src[i] if i < self.colsb[tsrc] else 0
            else:
                for i in range(16):
                    if i >= self.colsb[tsrc] // 4:
                        put32(out, 4 * i, 0)
                        continue
                    x = u32(src, 4 * i)
                    if op == "TCVTROWD2PS":
                        put32(out, 4 * i, cvt_int32_to_fp32(x))
                        continue
                    h = cvt_fp32_to_bfloat16(x) if "BF16" in op else vcvt_s2h_rne(x)
                    pos = 1 if op.endswith("H") else 0
                    w = [0, 0]
                    w[pos] = h                         # zdest.word[2i + zeropos] := 0
                    put32(out, 4 * i, w[0] | (w[1] << 16))
        self.zmm[zdest] = bytes(out)
        self.zero_tilecfg_start()


# ---- encodings of the new forms ----
def vex_map5(pp, reg, rm, vvvv, W=0, L=0):
    """VEX.128.pp.MAP5.W0 FD 11:rrr:bbb (TDP*F8PS)"""
    return vex3(pp, (reg >> 3) & 1, 0, (rm >> 3) & 1, W, vvvv, L, 5) + [
        0xFD, 0xC0 | ((reg & 7) << 3) | (rm & 7)]


F8_PP = {"BB": PP["NP"], "BH": PP["F2"], "HB": PP["F3"], "HH": PP["66"]}
F8_NAME = {"BB": "TDPBF8PS", "BH": "TDPBHF8PS", "HB": "TDPHBF8PS", "HH": "TDPHF8PS"}


def enc_f8(kind, d, s1, s2, **kw):
    return vex_map5(F8_PP[kind], d, s1, s2, **kw)


def evex(mmm, pp, R=0, X=0, B=0, R2=0, W=0, vvvv=0, V2=0, z=0, LL=2, b=0, aaa=0, U=1, B4=0):
    """62 P0 P1 P2 with the logical register bits (inverted where the encoding inverts them):
    P0 = ~R ~X ~B ~R' B4 m m m, P1 = W ~vvvv U pp, P2 = z L'L b ~V' aaa"""
    p0 = ((0 if R else 0x80) | (0 if X else 0x40) | (0 if B else 0x20) | (0 if R2 else 0x10) |
          (B4 << 3) | mmm)
    p1 = (W << 7) | ((~vvvv & 15) << 3) | (U << 2) | pp
    p2 = (z << 7) | (LL << 5) | (b << 4) | ((0 if V2 else 1) << 3) | aaa
    return [0x62, p0, p1, p2]


# name: (pp, 0F38 opcode of the r32 form, (pp, 0F3A opcode) of the imm8 form)
ROW_FORMS = {
    "TCVTROWD2PS":     (PP["F3"], 0x4A, PP["F3"], 0x07),
    "TCVTROWPS2BF16H": (PP["F2"], 0x6D, PP["F2"], 0x07),
    "TCVTROWPS2BF16L": (PP["F3"], 0x6D, PP["F3"], 0x77),
    "TCVTROWPS2PHH":   (PP["NP"], 0x6D, PP["NP"], 0x07),
    "TCVTROWPS2PHL":   (PP["66"], 0x6D, PP["F2"], 0x77),
    "TILEMOVROW":      (PP["66"], 0x4A, PP["66"], 0x07),
}


def enc_row(op, zdst, tsrc, gpr=None, imm=None, **kw):
    """EVEX.512.pp.0F38.W0 op 11:rrr:bbb (r32 = gpr in vvvv) or EVEX.512.pp.0F3A.W0 op ib"""
    ppr, opr, ppi, opi = ROW_FORMS[op]
    modrm = kw.pop("modrm", 0xC0 | ((zdst & 7) << 3) | (tsrc & 7))
    if imm is None:
        e = dict(R=(zdst >> 3) & 1, R2=(zdst >> 4) & 1, B=(tsrc >> 3) & 1, X=(tsrc >> 4) & 1,
                 vvvv=gpr & 15)
        e.update(kw)
        return evex(2, ppr, **e) + [opr, modrm]
    e = dict(R=(zdst >> 3) & 1, R2=(zdst >> 4) & 1, B=(tsrc >> 3) & 1, X=(tsrc >> 4) & 1)
    e.update(kw)
    return evex(3, ppi, **e) + [opi, modrm, imm & 0xFF]


def enc_loaddrs(t1, reg, base="rsi", index="rcx", disp=0, evex_form=False, **kw):
    """TILELOADDRS[T1] tmm, [base+index+disp]: VEX.128.F2/66.0F38.W0 4A !(11):rrr:100, or the
    APX-promoted EVEX.128 form (P2 = 0 0 L 0 ~V4 NF 0 0 with V4 = 0: 08h)"""
    pp = PP["66"] if t1 else PP["F2"]
    b, ix = REG[base], REG[index]
    if disp == 0 and (b & 7) != 5:
        mod, d = 0, []
    elif -128 <= disp <= 127:
        mod, d = 1, [disp & 0xFF]
    else:
        mod, d = 2, [(disp >> (8 * i)) & 0xFF for i in range(4)]
    tail = [0x4A, (mod << 6) | ((reg & 7) << 3) | 4, ((ix & 7) << 3) | (b & 7)] + d
    if evex_form:
        e = dict(R=(reg >> 3) & 1, X=(ix >> 3) & 1, B=(b >> 3) & 1, LL=0)
        e.update(kw)
        return evex(2, pp, **e) + tail
    return vex3(pp, (reg >> 3) & 1, (ix >> 3) & 1, (b >> 3) & 1, kw.get("W", 0), kw.get("vvvv", 0),
                kw.get("L", 0), 2) + tail


# ---- self test of the ISE 319433-062 part (hand-derived values) ----
def selftest2():
    ok = True

    def chk(cond, what):
        nonlocal ok
        if not cond:
            print("FAIL:", what)
            ok = False

    # FP8 decoding: E5M2 1.0 = 3C (2^16), E4M3 1.0 = 38 (2^9), denormals, maxima
    chk(convert_bf8_to_int64(0x3C) == 1 << 16, "bf8 1.0")
    chk(convert_hf8_to_int64(0x38) == 1 << 9, "hf8 1.0")
    chk(convert_bf8_to_int64(0x01) == 1, "bf8 min denormal = 2^-16")
    chk(convert_hf8_to_int64(0x01) == 1, "hf8 min denormal = 2^-9")
    chk(convert_bf8_to_int64(0x7B) == 57344 << 16, "bf8 max 57344")
    chk(convert_hf8_to_int64(0x7E) == 448 << 9, "hf8 max 448")
    chk(convert_bf8_to_int64(0xBC) == -(1 << 16), "bf8 -1.0")
    chk(fp8_class(0x7C, BF8) == "inf" and fp8_class(0xFD, BF8) == "nan", "bf8 specials")
    chk(fp8_class(0x7F, HF8) == "nan" and fp8_class(0x7E, HF8) == "num", "hf8 specials")
    # convert_int128_to_fp32 against exact rounding
    import random
    rnd = random.Random(7)
    for _ in range(3000):
        v = rnd.randrange(-(1 << 72), 1 << 72) >> rnd.randrange(0, 72)
        for (t1, t2, fac) in ((BF8, BF8, 32), (HF8, HF8, 18), (BF8, HF8, 25)):
            got = convert_int128_to_fp32(v, t1, t2)
            want = 0 if v == 0 else round_f32(Fraction(v, 1 << fac))
            if got != want:
                chk(False, "int128 -> fp32 %d / 2^%d: %08x vs %08x" % (v, fac, got, want))
                break
    # TDPBF8PS by hand: 1x1x1, a = (1, 2, -1, 0.5), b = (1, 1, 1, 4) -> 1 + 2 - 1 + 2 = 4
    a = Amx2()
    a.set_cfg((1, 0, [4, 4, 4, 0, 0, 0, 0, 0], [1, 1, 1, 0, 0, 0, 0, 0]))
    a.tiles[1][0:4] = bytes([0x3C, 0x40, 0xBC, 0x38])
    a.tiles[2][0:4] = bytes([0x3C, 0x3C, 0x3C, 0x44])
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0x40800000, "TDPBF8PS 4.0 (got %08x)" % u32(a.tiles[0], 0))
    # NaN anywhere in the tuple, INF * 0, INF - INF, DAZ = 0 on the accumulator, FTZ on the sum
    a.tiles[0][0:4] = bytes(4)
    a.tiles[1][0:4] = bytes([0x7D, 0, 0, 0])            # bf8 NaN times 0
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == QNAN_INDEFINITE, "NaN -> indefinite")
    a.tiles[0][0:4] = bytes(4)
    a.tiles[1][0:4] = bytes([0x7C, 0, 0, 0])
    a.tiles[2][0:4] = bytes([0x00, 0, 0, 0])            # INF * +0
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == QNAN_INDEFINITE, "INF * 0 -> indefinite")
    a.tiles[0][0:4] = bytes(4)
    a.tiles[1][0:4] = bytes([0x7C, 0xFC, 0, 0])
    a.tiles[2][0:4] = bytes([0x3C, 0x3C, 0, 0])         # +INF + -INF
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == QNAN_INDEFINITE, "+INF - INF -> indefinite")
    a.tiles[0][0:4] = bytes(4)
    a.tiles[1][0:4] = bytes([0x7C, 0x01, 0, 0])
    a.tiles[2][0:4] = bytes([0x81, 0x3C, 0, 0])         # +INF * -denormal: -INF
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0xFF800000, "INF * -denormal = -INF")
    a.tiles[0][0:4] = bytes([1, 0, 0, 0])               # accumulator 2^-149 (denormal)
    a.tiles[1][0:4] = bytes(4)
    a.tiles[2][0:4] = bytes(4)
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0, "denormal accumulator + 0: FTZ of the denormal sum -> +0")
    a.tiles[0][0:4] = bytes([1, 0, 0, 0x80])            # -2^-149 + 0: DAZ = 0 keeps the sign
    a.tiles[1][0:4] = bytes(4)
    a.tiles[2][0:4] = bytes(4)
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0x80000000, "-denormal + 0 -> -0 (DAZ = 0, FTZ)")
    a.tiles[0][0:4] = bytes(4)                          # 1 + 2^-24: tie, to even
    a.tiles[1][0:4] = bytes([0x3C, 0x0C, 0, 0])
    a.tiles[2][0:4] = bytes([0x3C, 0x0C, 0, 0])
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0x3F800000, "1 + 2^-24 -> 1")
    a.tiles[0][0:4] = bytes(4)                          # 1 + 3 * 2^-24: tie, up to even
    a.tiles[1][0:4] = bytes([0x3C, 0x0C, 0x10, 0])
    a.tiles[2][0:4] = bytes([0x3C, 0x0C, 0x0C, 0])
    a.TDPF8("BB", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0x3F800002, "1 + 3 * 2^-24 -> 1 + 2^-22")
    # E4M3 x E4M3: 448 * 448 = 200704 exactly
    a.tiles[0][0:4] = bytes(4)
    a.tiles[1][0:4] = bytes([0x7E, 0, 0, 0])
    a.tiles[2][0:4] = bytes([0x7E, 0, 0, 0])
    a.TDPF8("HH", 0, 1, 2)
    chk(u32(a.tiles[0], 0) == 0x48440000, "448 * 448 (got %08x)" % u32(a.tiles[0], 0))
    # conversions of the row instructions
    chk(cvt_fp32_to_bfloat16(0x3F808000) == 0x3F80, "bf16 tie to even down")
    chk(cvt_fp32_to_bfloat16(0x3F818000) == 0x3F82, "bf16 tie to even up")
    chk(cvt_fp32_to_bfloat16(0x7F800001) == 0x7FC0, "bf16 SNaN quieted")
    chk(cvt_fp32_to_bfloat16(0x80000001) == 0x8000, "bf16 denormal -> -0")
    chk(cvt_fp32_to_bfloat16(0x7F7FFFFF) == 0x7F80, "bf16 max rounds to inf")
    chk(vcvt_s2h_rne(0x3F800000) == 0x3C00, "fp16 1.0")
    chk(vcvt_s2h_rne(0x33800000) == 0x0001, "fp16 2^-24 (denormal output)")
    chk(vcvt_s2h_rne(0x33000000) == 0x0000, "fp16 2^-25 tie to even -> 0")
    chk(vcvt_s2h_rne(0x477FF000) == 0x7C00, "fp16 65520 -> inf")
    chk(vcvt_s2h_rne(0x7F800001) == 0x7E00, "fp16 SNaN quieted")
    chk(vcvt_s2h_rne(0x00000001) == 0x0000, "fp16 FP32 denormal -> 0")
    chk(cvt_int32_to_fp32(0x01000001) == 0x4B800000, "int32 2^24+1 -> 2^24 (RNE)")
    chk(cvt_int32_to_fp32(0x80000000) == 0xCF000000, "int32 -2^31")
    # TILEMOVROW / TCVTROW* by hand
    a = Amx2()
    a.set_cfg((1, 0, [8, 0, 0, 0, 0, 0, 0, 0], [2, 0, 0, 0, 0, 0, 0, 0]))
    a.tiles[0][64:72] = bytes([0, 0, 0x80, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF])
    a.ROWOP("TILEMOVROW", 3, 0, 0x11)
    chk(a.zmm[3] == bytes([0, 0, 0x80, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF]) + bytes(56), "TILEMOVROW")
    a.ROWOP("TCVTROWD2PS", 3, 0, 1)
    chk(u32(a.zmm[3], 0) == 0x4E7E0000 and u32(a.zmm[3], 4) == 0xBF800000, "TCVTROWD2PS")
    a.ROWOP("TCVTROWPS2BF16H", 3, 0, 1)
    chk(u32(a.zmm[3], 0) == 0x3F800000, "TCVTROWPS2BF16H")
    a.ROWOP("TCVTROWPS2PHL", 3, 0, 1)
    chk(u32(a.zmm[3], 0) == 0x00003C00, "TCVTROWPS2PHL")
    a.ROWOP("TILEMOVROW", 3, 0, 2)
    chk(a.zmm[3] == bytes(64), "row >= rows -> zero")
    a.cr0_ts = True
    try:
        a.ROWOP("TILEMOVROW", 3, 0, 0)
        chk(False, "CR0.TS #NM")
    except Fault as f:
        chk(f.vector == NM and a.xfd_err == 0, "CR0.TS #NM without XFD_ERR")
    # encodings (XED amx-dmr-isa patterns)
    chk(enc_row("TCVTROWD2PS", 1, 2, gpr=0) == [0x62, 0xF2, 0x7E, 0x48, 0x4A, 0xCA], "evex r32")
    chk(enc_row("TILEMOVROW", 1, 2, imm=5) == [0x62, 0xF3, 0x7D, 0x48, 0x07, 0xCA, 5], "evex imm")
    chk(enc_f8("BB", 1, 2, 3) == [0xC4, 0xE5, 0x60, 0xFD, 0xCA], "vex map5")
    print("selftest2:", "PASS" if ok else "FAIL")
    return ok


# ---- unicorn/tests/unit/x86_amx2_vectors.inc: the exception matrix of the new forms ----
EXC2_CFG = cfg_bytes({0: (16, 64), 1: (16, 64), 2: (16, 64), 3: (4, 8), 4: (16, 6),
                      5: (4, 16), 6: (2, 64), 7: (16, 64)})


def decode2(code, mode64=True, mask=AMX_ALL, apx=False):
    """-> ('UD', None) or (name, fields): the static #UD rules of the ISE 319433-062 forms
    (AMX-E3/E4 VEX, AMX-E3-EVEX promoted, AMX-E7/E8-EVEX)"""
    i = 0
    pre = set()
    while code[i] in (0x66, 0xF2, 0xF3, 0xF0, 0x67, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65) or \
            (mode64 and 0x40 <= code[i] <= 0x4F):
        pre.add(code[i] if not (mode64 and 0x40 <= code[i] <= 0x4F) else "REX")
        i += 1
    if pre & {0x66, 0xF2, 0xF3, 0xF0, "REX"}:
        return "UD", None
    if code[i] == 0xC4:
        b1, b2 = code[i + 1], code[i + 2]
        R, X, B = (~b1 >> 7) & 1, (~b1 >> 6) & 1, (~b1 >> 5) & 1
        mmmmm, W, vvvv, L, pp = b1 & 0x1F, b2 >> 7, (~b2 >> 3) & 15, (b2 >> 2) & 1, b2 & 3
        op, modrm = code[i + 3], code[i + 4]
        mod, rg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        if mmmmm == 5 and op == 0xFD and mod == 3:
            kind = {v: k for k, v in F8_PP.items()}[pp]
            if not (mask & AMX_FP8) or not mode64 or L or W:
                return "UD", None
            return F8_NAME[kind], dict(kind=kind, reg=rg | (R << 3), rm=rm | (B << 3), vvvv=vvvv)
        if mmmmm == 2 and op == 0x4A and mod != 3 and pp in (PP["F2"], PP["66"]):
            if not (mask & AMX_MOVRS) or not mode64 or L or W or vvvv or rm != 4:
                return "UD", None
            return ("TILELOADDRST1" if pp == PP["66"] else "TILELOADDRS"), dict(reg=rg | (R << 3))
        return "UD", None
    if code[i] == 0x62 and mode64:
        p0, p1, p2 = code[i + 1], code[i + 2], code[i + 3]
        if not apx and ((p0 & 0x08) or not (p1 & 0x04)):
            return "UD", None                      # EVEX.B4 / U reserved without APX
        mmm, pp, W = p0 & 7, p1 & 3, p1 >> 7
        R, X, B, R2 = (~p0 >> 7) & 1, (~p0 >> 6) & 1, (~p0 >> 5) & 1, (~p0 >> 4) & 1
        vvvv, U = (~p1 >> 3) & 15, (p1 >> 2) & 1
        z, LL, bb, V2, aaa = p2 >> 7, (p2 >> 5) & 3, (p2 >> 4) & 1, (~p2 >> 3) & 1, p2 & 7
        op, modrm = code[i + 4], code[i + 5]
        mod, rg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        if mmm == 2 and op == 0x4A and mod != 3 and apx and pp in (PP["F2"], PP["66"]):
            # APX-promoted (AMX-E3-EVEX): z, L'L, b, aaa 0, vvvv 1111b, V' 1, NF 0, W0
            if not (mask & AMX_MOVRS) or z or LL or bb or aaa or vvvv or V2 or W or rm != 4 \
                    or (p2 & 0x04):
                return "UD", None
            return ("TILELOADDRST1" if pp == PP["66"] else "TILELOADDRS"), dict(reg=rg | (R << 3))
        form = None
        for name, (ppr, opr, ppi, opi) in ROW_FORMS.items():
            if mmm == 2 and op == opr and pp == ppr:
                form = (name, False)
            if mmm == 3 and op == opi and pp == ppi:
                form = (name, True)
        if form is None or mod != 3:
            return "UD", None
        name, imm = form
        if not (mask & AMX_AVX512):
            return "UD", None
        if z or LL != 2 or bb or aaa or not U or V2 or W:
            return "UD", None
        if imm and vvvv:
            return "UD", None                      # AMX-E7-EVEX: EVEX.VVVV != 1111b
        return name, dict(zdst=rg | (R << 3) | (R2 << 4), tsrc=rm | (B << 3) | (X << 4),
                          gpr=None if imm else vvvv, imm=code[i + 6] if imm else None)
    return "UD", None


def exc2_vectors():
    """the AMX2 exception matrix: state via the Unicorn API, then one instruction; RSI = DATA +
    1000h (tile rows there), RCX = 64, RDX = row register value 3; RDI = DATA + 4000h"""
    out = []

    def add(name, code, mask=AMX_ALL, avx512=True, apx=False, xcr0=None, osxsave=True, xfd=0,
            cr0_ts=False, cfg=EXC2_CFG, rdx=3):
        code = bytes(code)
        base_x = 0x60007 | (0xE0 if avx512 else 0)
        x = base_x if xcr0 is None else xcr0
        a = Amx2(mask=mask, xcr0=x, osxsave=osxsave, xfd=xfd, cr0_ts=cr0_ts)
        if not mask:
            a.xcr0 &= ~0x60000
        a.load_cfg_like_xrstor(cfg)
        name_, f = decode2(code, mask=mask, apx=apx)
        vec = -1
        try:
            if name_ == "UD":
                raise Fault(UD)
            if name_ in ("TILELOADDRS", "TILELOADDRST1"):
                a.TILELOADDRS(f["reg"], 0x201000, 64)
            elif name_ in F8_NAME.values():
                a.TDPF8(f["kind"], f["reg"], f["rm"], f["vvvv"])
            else:
                row = (rdx & 0xFFFFFFFF) if f["gpr"] is not None else f["imm"]
                if f["gpr"] is not None and f["gpr"] != 2:
                    row = 0                            # every other GPR is 0 here
                a.ROWOP(name_, f["zdst"], f["tsrc"], row)
        except Fault as e:
            vec = e.vector
        out.append(dict(name=name, code=code, mask=mask, avx512=1 if avx512 else 0,
                        apx=1 if apx else 0, xcr0=xcr0 or 0, osxsave=osxsave, xfd=xfd,
                        cr0_ts=1 if cr0_ts else 0, cfg=cfg, rdx=rdx, vec=vec,
                        xfd_err=a.xfd_err, post_cfg=a.cfg_image()))

    loads = {"TILELOADDRS": enc_loaddrs(False, 1), "TILELOADDRST1": enc_loaddrs(True, 2)}
    f8 = {F8_NAME[k]: enc_f8(k, 0, 1, 2) for k in F8_PP}
    rows = {}
    for op in ROWOPS:
        rows[op + "_r32"] = enc_row(op, 9, 1, gpr=2)
        rows[op + "_imm"] = enc_row(op, 17, 1, imm=3)
    for n, c in list(loads.items()) + list(f8.items()) + list(rows.items()):
        feat = AMX_MOVRS if n in loads else AMX_FP8 if n in f8 else AMX_AVX512
        add(n + "_ok", c)
        add(n + "_amx_off", c, mask=0)
        add(n + "_no_feature", c, mask=AMX_ALL & ~feat)
        add(n + "_tile_only", c, mask=AMX_TILE)
        add(n + "_xcr0_no_amx", c, xcr0=0xE7)
        add(n + "_no_osxsave", c, osxsave=False)
        for p in (0x66, 0xF2, 0xF3, 0xF0, 0x41):
            add(n + "_prefix_%02x" % p, [p] + c)
        add(n + "_xfd", c, xfd=1 << 18)
        add(n + "_unconfigured", c, cfg=bytes(64))
        add(n + "_xfd_unconfigured", c, xfd=1 << 18, cfg=bytes(64))
        add(n + "_cr0_ts", c, cr0_ts=True)
        add(n + "_cr0_ts_xfd", c, cr0_ts=True, xfd=1 << 18)
    # AMX-MOVRS (AMX-E3) and the APX-promoted form (AMX-E3-EVEX)
    add("tileloaddrs_vexL1", _patch_vex(loads["TILELOADDRS"], L=1))
    add("tileloaddrs_vexW1", _patch_vex(loads["TILELOADDRS"], W=1))
    add("tileloaddrs_vvvv", _patch_vex(loads["TILELOADDRS"], vvvv=3))
    add("tileloaddrs_nosib", loads["TILELOADDRS"][:4] + [0x0E])
    add("tileloaddrs_mod3", loads["TILELOADDRS"][:4] + [0xCC])
    add("tileloaddrs_np", _patch_pp(loads["TILELOADDRS"], PP["NP"]))
    add("tileloaddrs_f3", _patch_pp(loads["TILELOADDRS"], PP["F3"]))
    add("tileloaddrs_tmm9", enc_loaddrs(False, 9))
    add("tileloaddrs_colsb6", enc_loaddrs(False, 4))
    add("tileloaddrs_startrow_eq", enc_loaddrs(False, 3), cfg=_with_start(EXC2_CFG, 4))
    add("tileloaddrs_32bit_map", _patch_map(loads["TILELOADDRS"], 3))
    ev = enc_loaddrs(False, 1, evex_form=True)
    add("evex_tileloaddrs_apx", ev, apx=True)
    add("evex_tileloaddrst1_apx", enc_loaddrs(True, 1, evex_form=True), apx=True)
    add("evex_tileloaddrs_no_apx", ev)
    add("evex_tileloaddrs_apx_movrs_off", ev, apx=True, mask=AMX_ALL & ~AMX_MOVRS)
    add("evex_tileloaddrs_apx_LL1", enc_loaddrs(False, 1, evex_form=True, LL=1), apx=True)
    add("evex_tileloaddrs_apx_W1", enc_loaddrs(False, 1, evex_form=True, W=1), apx=True)
    add("evex_tileloaddrs_apx_vvvv", enc_loaddrs(False, 1, evex_form=True, vvvv=2), apx=True)
    add("evex_tileloaddrs_apx_aaa", enc_loaddrs(False, 1, evex_form=True, aaa=1), apx=True)
    add("evex_tileloaddrs_apx_z", enc_loaddrs(False, 1, evex_form=True, z=1), apx=True)
    add("evex_tileloaddrs_apx_b", enc_loaddrs(False, 1, evex_form=True, b=1), apx=True)
    add("evex_tileloaddrs_apx_V2", enc_loaddrs(False, 1, evex_form=True, V2=1), apx=True)
    # AMX-FP8 (AMX-E4)
    add("tdpbf8ps_vexL1", _patch_vex(f8["TDPBF8PS"], L=1))
    add("tdpbf8ps_vexW1", _patch_vex(f8["TDPBF8PS"], W=1))
    add("tdpbf8ps_mem", f8["TDPBF8PS"][:4] + [0x06])
    add("tdpbf8ps_op_fc", f8["TDPBF8PS"][:3] + [0xFC, f8["TDPBF8PS"][4]])
    add("tdpbf8ps_map6", _patch_map(f8["TDPBF8PS"], 6))
    add("tdpbf8ps_dst_eq_src1", enc_f8("BB", 1, 1, 2))
    add("tdpbf8ps_src1_eq_src2", enc_f8("BB", 0, 2, 2))
    add("tdpbf8ps_dst_eq_src2", enc_f8("BB", 2, 1, 2))
    add("tdpbf8ps_dst8", enc_f8("BB", 8, 1, 2))
    add("tdphf8ps_src2_15", enc_f8("HH", 0, 1, 15))
    add("tdpbf8ps_colsb6", enc_f8("BB", 4, 1, 2))
    add("tdpbf8ps_dst_eq_src1_xfd", enc_f8("BB", 1, 1, 2), xfd=1 << 18)
    for (nm, sh) in [("dst_colsb_ne_src2", {0: (4, 16), 1: (4, 16), 2: (4, 32)}),
                     ("dst_rows_ne_src1", {0: (4, 16), 1: (5, 16), 2: (4, 16)}),
                     ("src1_k_ne_src2_rows", {0: (4, 16), 1: (4, 12), 2: (4, 16)}),
                     ("shape_ok", {0: (4, 16), 1: (4, 12), 2: (3, 16)})]:
        for k in ("BB", "HB"):
            add("%s_%s" % (F8_NAME[k].lower(), nm), enc_f8(k, 0, 1, 2), cfg=cfg_bytes(sh))
    # AMX-AVX512 (AMX-E7/E8-EVEX)
    r32 = rows["TILEMOVROW_r32"]
    im = rows["TCVTROWD2PS_imm"]
    for nm, c in (("r32", r32), ("imm", im)):
        op = "TILEMOVROW" if nm == "r32" else "TCVTROWD2PS"
        g = dict(gpr=2) if nm == "r32" else dict(imm=3)
        add("row_%s_no_avx512_state" % nm, c, avx512=False)
        add("row_%s_z" % nm, enc_row(op, 9, 1, z=1, **g))
        add("row_%s_LL0" % nm, enc_row(op, 9, 1, LL=0, **g))
        add("row_%s_LL1" % nm, enc_row(op, 9, 1, LL=1, **g))
        add("row_%s_LL3" % nm, enc_row(op, 9, 1, LL=3, **g))
        add("row_%s_b" % nm, enc_row(op, 9, 1, b=1, **g))
        add("row_%s_aaa" % nm, enc_row(op, 9, 1, aaa=1, **g))
        add("row_%s_V2" % nm, enc_row(op, 9, 1, V2=1, **g))
        add("row_%s_W1" % nm, enc_row(op, 9, 1, W=1, **g))
        add("row_%s_U0" % nm, enc_row(op, 9, 1, U=0, **g))
        add("row_%s_U0_apx" % nm, enc_row(op, 9, 1, U=0, **g), apx=True)
        add("row_%s_mem" % nm, c[:5] + [0x0E] + c[6:])
        add("row_%s_tmm8" % nm, enc_row(op, 9, 8, **g))
        add("row_%s_tmm16_X" % nm, enc_row(op, 9, 16, **g))
        add("row_%s_tmm4_colsb6" % nm, enc_row(op, 9, 4, **g))
        add("row_%s_invalid_tile" % nm, c, cfg=cfg_bytes({0: (16, 64)}))
        add("row_%s_zmm31" % nm, enc_row(op, 31, 1, **g))
    add("row_imm_vvvv", enc_row("TCVTROWD2PS", 9, 1, imm=3, vvvv=4))
    add("row_r32_rdx_hi", rows["TILEMOVROW_r32"], rdx=0xFFFFFFF0 | 15)
    add("row_r32_rdx_row_ge_rows", rows["TCVTROWPS2PHH_r32"], cfg=cfg_bytes({1: (2, 64)}))
    add("row_r32_gpr_r15", enc_row("TCVTROWD2PS", 9, 1, gpr=15))
    for pp in range(4):
        if all(f[0] != pp for f in ROW_FORMS.values() if f[1] == 0x4A):
            add("op0f38_4a_pp%d_reg" % pp, _patch_evex_pp(rows["TILEMOVROW_r32"], pp))
        if all(f[2] != pp for f in ROW_FORMS.values() if f[3] == 0x77):
            add("op0f3a_77_pp%d" % pp, _patch_evex_pp(rows["TCVTROWPS2BF16L_imm"], pp))
    add("legacy_0f38_4a", [0x0F, 0x38, 0x4A, 0xC1])
    return out


def _patch_pp(code, pp):
    c = list(code)
    i = c.index(0xC4)
    c[i + 2] = (c[i + 2] & ~3) | pp
    return c


def _patch_evex_pp(code, pp):
    c = list(code)
    i = c.index(0x62)
    c[i + 2] = (c[i + 2] & ~3) | pp
    return c


def emit_cinc2():
    w = sys.stdout.write
    w("/*\n * Generated by Emulator/tools/isa/ref_amx.py --cinc2 (independent model of ISE\n"
      " * 319433-062 ch. 3); regenerate instead of editing. Used by test_x86.c (test_x86_amx2_*).\n"
      " * x86_amx2_evecs: the exception matrix of AMX-MOVRS, AMX-FP8 and AMX-AVX512. State set with\n"
      " * the API: mask = UC_CTL_X86_AMX, avx512 = UC_CTL_X86_AVX512 (all), apx = UC_CTL_X86_APX,\n"
      " * xcr0 (0 = reset value), osxsave 0 = CR4.OSXSAVE cleared, xfd = IA32_XFD, cr0_ts = CR0.TS,\n"
      " * cfg = TILECFG; RSI = DATA + 1000h, RCX = 64, RDX = rdx (the r32 operand when vvvv = 2),\n"
      " * every other GPR 0. vec = expected vector (-1 none, 6 #UD, 7 #NM), xfd_err = IA32_XFD_ERR\n"
      " * afterwards, post_cfg = the TILECFG image afterwards.\n */\n\n")
    w("struct x86_amx2_evec {\n    const char *name;\n    uint8_t code[16];\n    int code_len;\n"
      "    int mask;\n    int avx512;\n    int apx;\n    uint64_t xcr0;\n    int osxsave;\n"
      "    uint64_t xfd;\n    int cr0_ts;\n    const char *cfg;\n    uint64_t rdx;\n    int vec;\n"
      "    uint64_t xfd_err;\n    const char *post_cfg;\n};\n\n")
    w("static const struct x86_amx2_evec x86_amx2_evecs[] = {\n")
    for v in exc2_vectors():
        w("    { \"%s\", %s, %d, %d, %d, %d, 0x%xULL, %d, 0x%xULL, %d,\n      %s,\n"
          "      0x%xULL, %d, 0x%xULL,\n      %s },\n"
          % (v["name"], cbytes(v["code"]), len(v["code"]), v["mask"], v["avx512"], v["apx"],
             v["xcr0"], 1 if v["osxsave"] else 0, v["xfd"], v["cr0_ts"], chex(v["cfg"]),
             v["rdx"], v["vec"], v["xfd_err"], chex(v["post_cfg"])))
    w("};\n")


# ---- Emulator/data/cases_amx2.txt (emu-alltest expected-value programs) ----
FP8_CLASSES = ["FINITE", "SMALL", "INFS", "ANY", "NANRARE"]


def gen_fp8(cls, t, r):
    """one FP8 byte of class cls for type t from the 64-bit random r"""
    sign = (r >> 63) & 1
    c = r & 0xFF
    body = (r >> 8) & 0x7F
    if cls == "ANY":
        return (sign << 7) | body
    if cls == "FINITE":                              # no NaN / INF
        while fp8_class(body, t) != "num":
            body = (body - 1) & 0x7F
        return (sign << 7) | body
    if cls == "SMALL":                               # zeros, denormals, the lowest binades
        lim = 0x0F if t == BF8 else 0x17
        return (sign << 7) | (body % (lim + 1))
    if cls == "INFS":                                # BF8 infinities among finite values
        if t == BF8 and c < 12:
            return (sign << 7) | 0x7C
        if c < 16:
            return sign << 7                         # zeros (INF * 0)
        while fp8_class(body, t) != "num":
            body = (body - 1) & 0x7F
        return (sign << 7) | body
    if cls == "NANRARE":                             # one NaN in about 200 elements
        if c == 0:
            return (sign << 7) | (0x7D if t == BF8 else 0x7F)
        while fp8_class(body, t) != "num":
            body = (body - 1) & 0x7F
        return (sign << 7) | body
    raise ValueError(cls)


def fp8_bytes(seed, n, cls, t):
    st = seed
    out = bytearray(n)
    for i in range(n):
        st, r = splitmix64(st)
        out[i] = gen_fp8(cls, t, r)
    return bytes(out)


def f32_words(seed, n, cls):
    st = seed
    out = bytearray(4 * n)
    for i in range(n):
        st, r = splitmix64(st)
        put32(out, 4 * i, gen_elem(F32, cls, r))
    return bytes(out)


def hexs(b):
    return "".join("%02X" % x for x in b)


def bstr(code):
    return ", ".join("0x%02x" % x for x in code)


def emit_cases2():
    """complete programs on the 64 KiB operand memory (RSI = MEM+8000h, RDI = MEM+9000h); the
    results reach memory (TILESTORED, XSAVES) or ZMM registers (the AMX-AVX512 rows)"""
    w = sys.stdout.write
    w("# Intel AMX ISE 319433-062 expected-value cases (ledger U720-U727): AMX-MOVRS, AMX-FP8,\n"
      "# AMX-AVX512, XSAVES / XRSTORS, IA32_XSS. Generated by Emulator/tools/isa/ref_amx.py --cases2\n"
      "# (independent model of ISE 319433-062 / SDM Vol1 13.11-13.14); regenerate, do not edit. The\n"
      "# i5-13600K has none of the AMX families: expected-value cases only, Unicorn at CPL0 with\n"
      "#   emu-alltest --cases Emulator\\data\\cases_amx2.txt --amx --avx512 --apx --expect-only\n"
      "# (AMX-AVX512 needs XCR0[7:5]; the EVEX TILELOADDRS forms need APX).\n")
    MEM = 0x10000000
    cases = []

    def case(name, code, ins, exp, loose=False):
        cases.append("# %s\n.byte %s | %s =>%s %s\n" % (name, bstr(code), " ".join(ins),
                                                         "!" if loose else "", exp))

    # ---- AMX-MOVRS: TILELOADDRS[T1] (VEX and EVEX) -> TILESTORED ----
    seed = 0xA2A20000
    for (t1, ev) in [(False, False), (True, False), (False, True), (True, True)]:
        for (M, C, stride, start) in [(4, 16, 64, 0), (16, 64, 64, 0), (3, 20, 24, 0), (6, 8, 40, 4)]:
            seed += 1
            cfg = cfg_bytes({1: (M, C)}, start_row=start)
            st = seed
            src = bytearray()
            for i in range(M * stride):
                st, r = splitmix64(st)
                src.append(r & 0xFF)
            pre = bytearray(b"\xEE" * (M * stride))
            a = Amx2()
            for i, b_ in enumerate(cfg):
                a.mem[MEM + 0x8000 + i] = b_
            for i, b_ in enumerate(src):
                a.mem[MEM + 0x8100 + i] = b_
            for i, b_ in enumerate(pre):
                a.mem[MEM + 0x9000 + i] = b_
            a.LDTILECFG(MEM + 0x8000)
            a.start_row = start          # LDTILECFG loads start_row too
            ld = enc_loaddrs(t1, 1, base="rsi", index="rcx", disp=0x100, evex_form=ev)
            a.TILELOADDRS(1, MEM + 0x8100, stride)
            a.TILESTORED(MEM + 0x9000, stride, 1)
            a.TILERELEASE()
            code = enc("LDTILECFG", base="rsi") + ld + \
                enc("TILESTORED", reg=1, base="rdi", index="rcx") + enc("TILERELEASE")
            res = bytes(a.mem.get(MEM + 0x9000 + i, 0) for i in range(M * stride))
            case("tileloaddrs%s%s_%dx%d_stride%d_start%d" % ("t1" if t1 else "",
                                                           "_evex" if ev else "", M, C, stride,
                                                           start),
                 code, ["rcx=%d" % stride, "m+0x8000=" + hexs(cfg), "m+0x8100=" + hexs(src),
                        "m+0x9000=" + hexs(pre)], "m+0x9000=" + hexs(res))
    # static #UDs of TILELOADDRS
    ld = enc_loaddrs(False, 1, disp=0x100)
    case("tileloaddrs_vexL1_ud", _patch_vex(ld, L=1), ["rcx=64"], "#UD")
    case("tileloaddrs_vexW1_ud", _patch_vex(ld, W=1), ["rcx=64"], "#UD")
    case("tileloaddrs_vvvv_ud", _patch_vex(ld, vvvv=1), ["rcx=64"], "#UD")
    case("tileloaddrs_mod3_ud", ld[:4] + [0xC1], [], "#UD")
    case("tileloaddrs_unconfigured_ud", ld, ["rcx=64"], "#UD")
    # ---- AMX-FP8 ----
    seed = 0xF8F80000
    for kind in ("BB", "BH", "HB", "HH"):
        t1 = BF8 if kind[0] == "B" else HF8
        t2 = BF8 if kind[1] == "B" else HF8
        for (cls, acc) in [("FINITE", "NORMAL"), ("SMALL", "TINY"), ("INFS", "WIDE"),
                           ("ANY", "NORMAL"), ("NANRARE", "SPECIAL"), ("FINITE", "HUGE")]:
            for (M, K, N, stride) in [(4, 4, 4, 64), (3, 2, 5, 24), (16, 16, 16, 64)]:
                if cls in ("ANY", "NANRARE") and M == 16:
                    continue
                seed += 1
                cfg = cfg_bytes({0: (M, 4 * N), 1: (M, 4 * K), 2: (K, 4 * N)})
                A = bytearray(1024)
                B = bytearray(1024)
                C = bytearray(1024)
                av = fp8_bytes(seed, 1024, cls, t1)
                bv = fp8_bytes(seed ^ 0x5555, 1024, cls, t2)
                cv = f32_words(seed ^ 0xAAAA, 256, acc)
                A[:] = av
                B[:] = bv
                C[:] = cv
                mem = {}

                def put(off, data):
                    for i, b_ in enumerate(data):
                        mem[off + i] = b_
                put(0x8000, cfg)
                for (off, t, rows, colsb) in [(0x8100, A, M, 4 * K), (0x8500, B, K, 4 * N),
                                              (0x9000, C, M, 4 * N)]:
                    for r in range(rows):
                        put(off + r * stride, t[64 * r: 64 * r + colsb])
                a = Amx2()
                for k_, v_ in mem.items():
                    a.mem[MEM + k_] = v_
                rsi, rdi = MEM + 0x8000, MEM + 0x9000
                a.LDTILECFG(rsi)
                a.TILELOADD(1, rsi + 0x100, stride)
                a.TILELOADD(2, rsi + 0x500, stride)
                a.TILELOADD(0, rdi, stride)
                a.TDPF8(kind, 0, 1, 2)
                a.TILESTORED(rdi, stride, 0)
                a.TILERELEASE()
                code = (enc("LDTILECFG", base="rsi") +
                        enc("TILELOADD", reg=1, base="rsi", index="rcx", disp=0x100) +
                        enc("TILELOADD", reg=2, base="rsi", index="rcx", disp=0x500) +
                        enc("TILELOADD", reg=0, base="rdi", index="rcx") +
                        enc_f8(kind, 0, 1, 2) +
                        enc("TILESTORED", reg=0, base="rdi", index="rcx") + enc("TILERELEASE"))
                spans = [(0x8000, 0x8040), (0x8100, 0x8100 + (M - 1) * stride + 4 * K),
                         (0x8500, 0x8500 + (K - 1) * stride + 4 * N),
                         (0x9000, 0x9000 + (M - 1) * stride + 4 * N)]
                ins = ["rcx=%d" % stride] + ["m+0x%X=%s" % (lo, hexs(bytes(mem.get(i, 0)
                                                                        for i in range(lo, hi))))
                                             for lo, hi in spans]
                lo, hi = spans[3]
                res = bytes(a.mem.get(MEM + i, 0) for i in range(lo, hi))
                case("%s_%s_%s_%dx%dx%d_stride%d" % (F8_NAME[kind].lower(), cls.lower(),
                                                     acc.lower(), M, K, N, stride),
                     code, ins, "m+0x%X=%s" % (lo, hexs(res)))
    # FP8 specials, one element (M = K = N = 1), hand-picked: (a tuple, b tuple, accumulator)
    specials = [
        ("nan_src1", "BB", [0x7D, 0, 0, 0], [0x3C] * 4, 0x3F800000),
        ("nan_src2_hf8", "HH", [0x38] * 4, [0, 0, 0xFF, 0], 0),
        ("nan_acc", "BB", [0x3C] * 4, [0x3C] * 4, 0x7FC00001),
        ("snan_acc", "BB", [0x3C] * 4, [0x3C] * 4, 0x7F800001),
        ("inf_times_zero", "BB", [0x7C, 0, 0, 0], [0x80, 0, 0, 0], 0),
        ("inf_minus_inf", "BB", [0x7C, 0xFC, 0, 0], [0x3C, 0x3C, 0, 0], 0),
        ("inf_times_denormal", "BH", [0xFC, 0, 0, 0], [0x01, 0, 0, 0], 0x3F800000),
        ("inf_plus_acc_neg_inf", "BB", [0x7C, 0, 0, 0], [0x3C, 0, 0, 0], 0xFF800000),
        ("inf_inf_same", "BB", [0x7C, 0xFC, 0, 0], [0x7C, 0xFC, 0, 0], 0x41200000),
        ("acc_denormal_daz0", "HH", [0x08, 0, 0, 0], [0x08, 0, 0, 0], 0x00400000),
        ("acc_denormal_plus_zero", "BB", [0, 0, 0, 0], [0, 0, 0, 0], 0x80000001),
        ("minus_zero_acc", "BB", [0x80, 0, 0, 0], [0x3C, 0, 0, 0], 0x80000000),
        ("hf8_max_square", "HH", [0x7E, 0x7E, 0x7E, 0x7E], [0x7E, 0xFE, 0x7E, 0x7E], 0),
        ("bf8_max_square", "BB", [0x7B] * 4, [0x7B] * 4, 0x7F7FFFFF),
        ("round_tie_even", "BB", [0x3C, 0x0C, 0, 0], [0x3C, 0x0C, 0, 0], 0),
        ("round_tie_up", "BB", [0x3C, 0x0C, 0x10, 0], [0x3C, 0x0C, 0x0C, 0], 0),
        ("round_above_tie", "BH", [0x3C, 0x0C, 0, 0], [0x38, 0x01, 0, 0], 0x80000000),
        ("mixed_hb", "HB", [0x38, 0x01, 0xB8, 0x7E], [0x3C, 0x3C, 0x01, 0x7B], 0x3F000000),
    ]
    for (nm, kind, av, bv, acc) in specials:
        cfg = cfg_bytes({0: (1, 4), 1: (1, 4), 2: (1, 4)})
        a = Amx2()
        a.set_cfg((1, 0, [4, 4, 4, 0, 0, 0, 0, 0], [1, 1, 1, 0, 0, 0, 0, 0]))
        a.tiles[1][0:4] = bytes(av)
        a.tiles[2][0:4] = bytes(bv)
        put32(a.tiles[0], 0, acc)
        a.TDPF8(kind, 0, 1, 2)
        res = bytes(a.tiles[0][0:4])
        accb = bytearray(4)
        put32(accb, 0, acc)
        code = (enc("LDTILECFG", base="rsi") +
                enc("TILELOADD", reg=1, base="rsi", index="rcx", disp=0x100) +
                enc("TILELOADD", reg=2, base="rsi", index="rcx", disp=0x500) +
                enc("TILELOADD", reg=0, base="rdi", index="rcx") + enc_f8(kind, 0, 1, 2) +
                enc("TILESTORED", reg=0, base="rdi", index="rcx") + enc("TILERELEASE"))
        case("%s_special_%s" % (F8_NAME[kind].lower(), nm), code,
             ["rcx=4", "m+0x8000=" + hexs(cfg), "m+0x8100=" + hexs(bytes(av)),
              "m+0x8500=" + hexs(bytes(bv)), "m+0x9000=" + hexs(accb)], "m+0x9000=" + hexs(res))
    # FP8 static #UDs and shape #UD after LDTILECFG
    case("tdpbf8ps_vexL1_ud", _patch_vex(enc_f8("BB", 0, 1, 2), L=1), [], "#UD")
    case("tdpbf8ps_vexW1_ud", _patch_vex(enc_f8("BB", 0, 1, 2), W=1), [], "#UD")
    case("tdpbf8ps_mem_ud", enc_f8("BB", 0, 1, 2)[:4] + [0x06], [], "#UD")
    case("tdpbf8ps_same_tiles_ud", enc_f8("BB", 1, 1, 2), [], "#UD")
    c = enc("LDTILECFG", base="rsi") + enc_f8("HH", 0, 1, 2)
    case("tdphf8ps_rows_mismatch_ud", c,
         ["m+0x8000=" + hexs(cfg_bytes({0: (4, 16), 1: (5, 16), 2: (4, 16)}))], "#UD")
    # ---- AMX-AVX512: LDTILECFG; TILELOADD tmm1..; row op -> zmm; TILERELEASE ----
    seed = 0xA5120000
    data_kinds = [("bytes", None), ("f32normal", "NORMAL"), ("f32wide", "WIDE"),
                  ("f32special", "SPECIAL"), ("f32tiny", "TINY"), ("f32huge", "HUGE"),
                  ("f32int", "INT")]
    zi = 0
    for op in ROWOPS:
        for (dname, dcls) in data_kinds:
            if op == "TILEMOVROW" and dcls is not None and dcls != "WIDE":
                continue
            if op == "TCVTROWD2PS" and dcls is not None:
                continue
            for (rows_, colsb, row, form) in [(16, 64, 5, "imm"), (7, 40, 6, "r32"),
                                              (4, 12, 0x13, "imm"), (3, 64, 3, "r32"),
                                              (2, 4, 0xFFFFFFF1, "r32"), (16, 60, 15, "imm")]:
                seed += 1
                zi = (zi + 7) % 32
                cfg = cfg_bytes({1: (rows_, colsb)})
                if dcls is None:
                    st = seed
                    tile = bytearray()
                    for i in range(1024):
                        st, r = splitmix64(st)
                        tile.append(r & 0xFF)
                else:
                    tile = bytearray(f32_words(seed, 256, dcls))
                a = Amx2()
                a.set_cfg((1, 0, [0, colsb, 0, 0, 0, 0, 0, 0], [0, rows_, 0, 0, 0, 0, 0, 0]))
                for r in range(rows_):
                    a.tiles[1][64 * r: 64 * r + colsb] = tile[64 * r: 64 * r + colsb]
                if form == "imm":
                    rc = enc_row(op, zi, 1, imm=row)
                    rowv = row & 0xFF
                else:
                    rc = enc_row(op, zi, 1, gpr=2)
                    rowv = row
                a.ROWOP(op, zi, 1, rowv)
                old = bytes((0xC3 + 5 * i) & 0xFF for i in range(64))
                code = (enc("LDTILECFG", base="rsi") +
                        enc("TILELOADD", reg=1, base="rsi", index="rcx", disp=0x100) + rc +
                        enc("TILERELEASE"))
                mem_rows = b"".join(bytes(tile[64 * r: 64 * r + colsb]) + bytes(64 - colsb)
                                    for r in range(rows_))
                ins = ["rcx=64", "m+0x8000=" + hexs(cfg), "m+0x8100=" + hexs(mem_rows),
                       "zmm%d=%s" % (zi, hexs(old))]
                if form == "r32":
                    ins.insert(0, "rdx=0x%X" % row)
                case("%s_%s_%s_%dx%d_row%x" % (op.lower(), form, dname, rows_, colsb, row),
                     code, ins, "zmm%d=%s" % (zi, hexs(a.zmm[zi])))
    # AMX-AVX512 faults: CR0.TS (#NM), XFD (#NM), unconfigured, colsb % 4, invalid tile
    ldc = enc("LDTILECFG", base="rsi")
    rowc = enc_row("TILEMOVROW", 2, 1, imm=0)
    cfgok = cfg_bytes({1: (2, 64), 4: (2, 6)})
    mov_cr0_ts = [0x0F, 0x20, 0xC0, 0x48, 0x83, 0xC8, 0x08, 0x0F, 0x22, 0xC0]   # mov rax,cr0; or rax,8; mov cr0,rax
    case("tilemovrow_cr0_ts_nm", ldc + mov_cr0_ts + rowc, ["m+0x8000=" + hexs(cfgok)],
         "#NM", loose=True)
    case("tdpbf8ps_cr0_ts_no_nm", mov_cr0_ts + enc_f8("BB", 0, 1, 2), [], "#UD", loose=True)
    wr_xfd = [0xB9, 0xC4, 0x01, 0, 0, 0xB8, 0, 0, 4, 0, 0xBA, 0, 0, 0, 0, 0x0F, 0x30]  # mov ecx,1C4h; mov eax,40000h; mov edx,0; wrmsr
    case("tilemovrow_xfd_nm", ldc + wr_xfd + rowc, ["m+0x8000=" + hexs(cfgok)], "#NM", loose=True)
    case("tilemovrow_unconfigured_ud", rowc, [], "#UD")
    case("tilemovrow_colsb6_ud", ldc + enc_row("TILEMOVROW", 2, 4, imm=0),
         ["m+0x8000=" + hexs(cfgok)], "#UD", loose=True)
    case("tilemovrow_invalid_tile_ud", ldc + enc_row("TILEMOVROW", 2, 3, imm=0),
         ["m+0x8000=" + hexs(cfgok)], "#UD", loose=True)
    case("tilemovrow_tmm8_ud", ldc + enc_row("TILEMOVROW", 2, 9, imm=0),
         ["m+0x8000=" + hexs(cfgok)], "#UD", loose=True)
    for (nm, kw) in [("z", dict(z=1)), ("ll0", dict(LL=0)), ("b", dict(b=1)), ("aaa", dict(aaa=2)),
                     ("v2", dict(V2=1)), ("w1", dict(W=1)), ("vvvv", dict(vvvv=1))]:
        case("tilemovrow_imm_%s_ud" % nm, enc_row("TILEMOVROW", 2, 1, imm=0, **kw), [], "#UD")
    case("tcvtrowd2ps_r32_vvvv_ok_unconfigured_ud", enc_row("TCVTROWD2PS", 2, 1, gpr=5), [], "#UD")
    # ---- XSAVES / XRSTORS / IA32_XSS (CPL0) ----
    cases.extend(xsaves_cases())
    for s in cases:
        w(s)


def xsaves_cases():
    """XSAVES / XRSTORS of the AMX components and the header, SDM Vol1 13.4.3 (compacted format:
    the extended region from offset 576, a component with CPUID.(0DH,i):ECX[1] = 1 64-byte
    aligned: TILECFG at 576, TILEDATA at 640), 13.11, 13.12, 13.14; Vol2D XSAVES / XRSTORS."""
    out = []
    MEM = 0x10000000

    def case(name, code, ins, exp, loose=False):
        out.append("# %s\n.byte %s | %s =>%s %s\n" % (name, bstr(code), " ".join(ins),
                                                       "!" if loose else "", exp))

    def eaxedx(v):
        return ([0xB8] + list((v & 0xFFFFFFFF).to_bytes(4, "little")) + [0xBA] +
                list((v >> 32).to_bytes(4, "little")))

    XSAVES = [0x48, 0x0F, 0xC7, 0x2E]          # xsaves64 [rsi]
    XRSTORS = [0x48, 0x0F, 0xC7, 0x1E]         # xrstors64 [rsi]
    cfg = cfg_bytes({1: (3, 16), 2: (16, 64)})
    st = 0x5A5A
    rows1, rows2 = bytearray(), bytearray()
    for i in range(3 * 16):
        st, r = splitmix64(st)
        rows1.append(r & 0xFF)
    for i in range(16 * 64):
        st, r = splitmix64(st)
        rows2.append(r & 0xFF)
    # 1. XSAVES of TILECFG + TILEDATA after LDTILECFG / TILELOADD: XSTATE_BV = 60000h,
    #    XCOMP_BV = 8000000000060000h, TILECFG image at 576, all 8 KB of TILEDATA at 640
    tiles = [bytearray(1024) for _ in range(8)]
    for r in range(3):
        tiles[1][64 * r: 64 * r + 16] = rows1[16 * r: 16 * r + 16]
    for r in range(16):
        tiles[2][64 * r: 64 * r + 64] = rows2[64 * r: 64 * r + 64]
    code = (enc("LDTILECFG", base="rdi") +
            enc("TILELOADD", reg=1, base="rdi", index="rcx", disp=0x100) +
            enc("TILELOADD", reg=2, base="rdi", index="rcx", disp=0x400) + eaxedx(0x60000) + XSAVES)
    hdr = (0x60000).to_bytes(8, "little") + (0x8000000000060000).to_bytes(8, "little")
    ins = ["rcx=64", "m+0x9000=" + hexs(cfg),
           "m+0x9100=" + hexs(b"".join(bytes(rows1[16 * r:16 * r + 16]) + bytes(48) for r in range(3))),
           "m+0x9400=" + hexs(bytes(rows2))]
    # the XSAVE area (RSI = MEM+8000h) overlaps RDI = MEM+9000h only from offset 1000h (TILEDATA)
    img = bytearray(64 * 1024)
    for s_ in ins[1:]:
        k, v = s_.split("=")
        off = int(k[2:], 16)
        img[off:off + len(v) // 2] = bytes.fromhex(v)
    expect = bytearray(img)
    expect[0x8200:0x8210] = hdr
    expect[0x8240:0x8280] = cfg
    expect[0x8280:0x8280 + 8192] = b"".join(bytes(t) for t in tiles)
    lo, hi = 0x8200, 0x8280 + 8192
    case("xsaves_amx_rfbm_60000", code, ins, "rax=0x60000 rdx=0 m+0x%X=%s" % (lo, hexs(expect[lo:hi])))
    # 2. XSAVES with TILECFG in INIT (TILERELEASE): XINUSE = 0 for both -> XSTATE_BV = 0, the
    #    components are not written (init optimisation); XCOMP_BV = 8000000000060000h
    pre = bytes([0xCC]) * 64
    case("xsaves_amx_init", ax_list(enc("TILERELEASE")) + eaxedx(0x60000) + XSAVES,
         ["m+0x8200=" + hexs(pre[:16]) + "00" * 48, "m+0x8240=" + hexs(pre)],
         "rax=0x60000 rdx=0 m+0x8200=" + hexs((0).to_bytes(8, "little") +
                                             (0x8000000000060000).to_bytes(8, "little")))
    # 3. XSAVES with XFD armed for TILEDATA (IA32_XFD = 40000h): TILEDATA as if XINUSE[18] = 0
    #    (XSTATE_BV[18] = 0, not written), TILECFG saved; no #NM
    wr_xfd = [0xB9, 0xC4, 0x01, 0, 0, 0xB8, 0, 0, 4, 0, 0xBA, 0, 0, 0, 0, 0x0F, 0x30]   # IA32_XFD := 40000h
    code = (enc("LDTILECFG", base="rdi") +
            enc("TILELOADD", reg=2, base="rdi", index="rcx", disp=0x400) + wr_xfd +
            eaxedx(0x60000) + XSAVES)
    exp = (0x20000).to_bytes(8, "little") + (0x8000000000060000).to_bytes(8, "little")
    case("xsaves_amx_xfd_armed", code, ["rcx=64", "m+0x9000=" + hexs(cfg),
                                        "m+0x9400=" + hexs(bytes(rows2)),
                                        "m+0x8280=" + "DD" * 64],
         "rax=0x60000 rcx=0x1C4 rdx=0 m+0x8200=%s m+0x8240=%s m+0x8280=%s" % (
             hexs(exp), hexs(cfg), "DD" * 64))
    # 4. XRSTORS of TILECFG + TILEDATA, then STTILECFG / TILESTORED show the loaded state
    area = bytearray(576 + 64 + 8192)
    area[512:528] = (0x60000).to_bytes(8, "little") + (0x8000000000060000).to_bytes(8, "little")
    cfg4 = cfg_bytes({3: (2, 8), 5: (16, 64)}, start_row=0)
    area[576:640] = cfg4
    st = 0x7777
    for i in range(8192):
        st, r = splitmix64(st)
        area[640 + i] = r & 0xFF
    # (the area spans MEM+8000h..A27Fh: STTILECFG / TILESTORED write at RDI + 2000h = MEM+B000h)
    code = (eaxedx(0x60000) + XRSTORS + enc("STTILECFG", base="rdi", disp=0x2000) +
            enc("TILESTORED", reg=3, base="rdi", index="rcx", disp=0x2100) + enc("TILERELEASE"))
    t3 = area[640 + 3 * 1024: 640 + 3 * 1024 + 1024]
    case("xrstors_amx_then_store", code,
         ["rcx=64", "m+0x8200=" + hexs(bytes(area[512:]))],
         "rax=0x60000 rdx=0 m+0xB000=%s m+0xB100=%s m+0xB140=%s" % (
             hexs(cfg4), hexs(bytes(t3[0:8])), hexs(bytes(t3[64:72]))))
    # 5. XRSTORS #GP conditions (header) and #NM (XFD with XSTATE_BV[18] = 1)
    def hdr_case(nm, xs, xc, rest=b"", rfbm=0x60000, pre_code=(), exp="#GP"):
        h = xs.to_bytes(8, "little") + xc.to_bytes(8, "little") + rest
        case("xrstors_" + nm, list(pre_code) + eaxedx(rfbm) + XRSTORS,
             ["m+0x8200=" + hexs(h)], exp, loose=True)
    hdr_case("xcomp_bit63_clear_gp", 0, 0x60000)
    hdr_case("xcomp_not_enabled_pt_gp", 0, 0x8000000000000100)
    hdr_case("xcomp_bit_63_only_ok", 0, 0x8000000000000000, exp="")
    hdr_case("xstate_not_in_xcomp_gp", 0x40000, 0x8000000000020000)
    hdr_case("xstate_bit63_gp", 0x8000000000000000, 0x8000000000000000)
    hdr_case("header_byte16_gp", 0, 0x8000000000000000, b"\x01")
    hdr_case("header_byte63_gp", 0, 0x8000000000000000, bytes(47) + b"\x01")
    hdr_case("xfd_tiledata_nm", 0x40000, 0x8000000000060000, pre_code=wr_xfd, exp="#NM")
    hdr_case("xfd_tiledata_init_ok", 0x20000, 0x8000000000060000, pre_code=wr_xfd, exp="")
    # misaligned area, CPL3, prefixes, register form
    case("xsaves_misaligned_gp", [0x48, 0x0F, 0xC7, 0x6E, 0x08], [], "#GP", loose=True)
    case("xrstors_misaligned_gp", [0x48, 0x0F, 0xC7, 0x5E, 0x10], [], "#GP", loose=True)
    case("xsaves_cpl3_gp", XSAVES, ["cpl=3"], "#GP", loose=True)
    case("xrstors_cpl3_gp", XRSTORS, ["cpl=3"], "#GP", loose=True)
    case("xsaves_66_ud", [0x66] + XSAVES, [], "#UD", loose=True)
    case("xsaves_f3_ud", [0xF3] + XSAVES, [], "#UD", loose=True)
    case("xrstors_f2_ud", [0xF2] + XRSTORS, [], "#UD", loose=True)
    case("xsaves_lock_ud", [0xF0] + XSAVES, [], "#UD", loose=True)
    case("xsaves_mod3_ud", [0x48, 0x0F, 0xC7, 0xEE], [], "#UD", loose=True)
    case("xsaves_cr0_ts_nm", [0x0F, 0x20, 0xC0, 0x48, 0x83, 0xC8, 0x08, 0x0F, 0x22, 0xC0] + XSAVES,
         [], "#NM", loose=True)
    # 6. IA32_XSS (DA0H): this CPU model supports no supervisor state component (CPUID.(0DH,1):
    #    EDX:ECX = 0): WRMSR of 0 works, any set bit #GP (bit 8 PT, bit 0 reserved, bit 63);
    #    RDMSR reads 0
    for (nm, v, e) in [("zero_ok", 0, ""), ("pt_gp", 0x100, "#GP"), ("bit0_gp", 1, "#GP"),
                       ("bit63_gp", 1 << 63, "#GP")]:
        code = [0xB9, 0xA0, 0x0D, 0, 0] + eaxedx(v) + [0x0F, 0x30]
        case("wrmsr_xss_" + nm, code, [], (e or "rcx=0xDA0 rax=0x%X rdx=0x%X" % (v & 0xFFFFFFFF, v >> 32)),
             loose=bool(e))
    case("rdmsr_xss_zero", [0xB9, 0xA0, 0x0D, 0, 0, 0x0F, 0x32], ["rax=0x1234", "rdx=0x5678"],
         "rcx=0xDA0 rax=0 rdx=0")
    return out


def ax_list(c):
    return list(c)


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
        ok1 = selftest()
        ok2 = selftest2()
        sys.exit(0 if ok1 and ok2 else 1)
    if "--cinc2" in sys.argv:
        emit_cinc2()
        return
    if "--cases2" in sys.argv:
        emit_cases2()
        return
    if "--cinc" in sys.argv:
        emit_cinc()
        return
    if "--cases" in sys.argv:
        emit_cases()
        return
    print(__doc__)


if __name__ == "__main__":
    main()
