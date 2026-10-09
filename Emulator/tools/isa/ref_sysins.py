r"""Independent reference model + expected-value case generator (ledger U800-U829, agent "sysins"):

  * AVX512DQ VPMOVD2M / VPMOVQ2M / VPMOVM2D / VPMOVM2Q (U800)       -> Emulator\data\cases_sysins_dq.txt

Written from the Intel manuals only (the emulator's C sources were not read for the expected values;
no CPU measurements - the i5-13600K has no AVX-512):
  * SDM Vol2C 325462-092 VPMOVB2M/VPMOVW2M/VPMOVD2M/VPMOVQ2M (opcode table, Operation:
    "(KL, VL) = (4, 128), (8, 256), (16, 512) ... IF SRC[i+31] THEN DEST[j] := 1 ... DEST[MAX_KL-1:KL]
    := 0") and VPMOVM2B/VPMOVM2W/VPMOVM2D/VPMOVM2Q ("DEST[i+31:i] := -1 ... DEST[MAXVL-1:VL] := 0").
  * SDM Vol2A 2.7 (EVEX P0/P1/P2 layout, Table 2-40 opcode-independent fields, Table 2-41 operand
    encoding: EVEX.R / EVEX.R' must be 1 when ModRM.reg encodes a k-reg, EVEX.X / EVEX.B ignored
    when ModRM.r/m encodes a k-reg; Table 2-42: aaa != 000b and z != 0 #UD for VPMOVM2x / VPMOVx2M;
    Table 2-43: EVEX.b = 1 #UD for "other instruction classes"), 2.8.7 Table 2-57 (E7NM: vvvv !=
    1111b, V' = 0, L'L = 11b #UD), and the "RM" operand encoding (ModRM.r/m register only).

Modelling decisions:
  a. The opcode table lists only register forms ("k1, xmm1" / "xmm1, k1"): ModRM.mod != 11b #UD.
  b. NP / F2 forms of EVEX.0F38 38/39 do not exist (#UD); the 66 forms are VPMINSB / VPMINSD/Q
     (other instructions, not generated here).

Usage:
  python -I ref_sysins.py --selftest   hand-derived checks of the model (exit 0 on pass)
  python -I ref_sysins.py --write      regenerate the case files listed above (CRLF)
"""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, '..', '..', 'data'))


def hexbytes(b):
    return bytes(b).hex().upper()


def bytelist(b):
    return ', '.join('0x%02x' % x for x in b)


# ------------------------------------------------------------------------------------------------
# EVEX encoding (SDM Vol2A 2.7.1, Figure 2-11): P0 = R X B R' 0 0 m m (R X B R' stored inverted),
# P1 = W vvvv 1 pp (vvvv inverted), P2 = z L'L b V' aaa (V' inverted). Logical register-number bits
# are passed; raw_* override a stored bit directly (for the #UD cases).
# ------------------------------------------------------------------------------------------------
def evex(op, mod, reg, rm, w, ll, pp, mm=2, vvvv=0, vhi=0, z=0, b=0, aaa=0, raw_r=None, raw_rp=None,
         raw_x=None, raw_b=None, tail=b''):
    r_bit = (~reg >> 3) & 1 if raw_r is None else raw_r
    rp_bit = (~reg >> 4) & 1 if raw_rp is None else raw_rp
    x_bit = (~rm >> 4) & 1 if raw_x is None else raw_x
    b_bit = (~rm >> 3) & 1 if raw_b is None else raw_b
    p0 = (r_bit << 7) | (x_bit << 6) | (b_bit << 5) | (rp_bit << 4) | mm
    p1 = (w << 7) | (((~vvvv) & 15) << 3) | 4 | pp
    p2 = (z << 7) | (ll << 5) | (b << 4) | ((~vhi & 1) << 3) | aaa
    modrm = (mod << 6) | ((reg & 7) << 3) | (rm & 7)
    return bytes([0x62, p0, p1, p2, op, modrm]) + tail


# ------------------------------------------------------------------------------------------------
# AVX512DQ VPMOVD2M / VPMOVQ2M / VPMOVM2D / VPMOVM2Q (SDM Vol2C Operation, transcribed)
# ------------------------------------------------------------------------------------------------
def vpmov2m(src, esz, vl):
    """VPMOVD2M (esz 32) / VPMOVQ2M (esz 64): bit j of the 64-bit k = SRC[j*esz + esz-1]."""
    kl = vl // esz
    k = 0
    for j in range(kl):
        i = j * esz
        bit = (src[(i + esz - 1) // 8] >> ((i + esz - 1) % 8)) & 1
        k |= bit << j
    return k                                    # DEST[MAX_KL-1:KL] := 0


def vpmovm2(k, esz, vl):
    """VPMOVM2D / VPMOVM2Q: element j = all ones if k[j], else 0; DEST[MAXVL-1:VL] := 0."""
    kl = vl // esz
    out = bytearray(64)
    for j in range(kl):
        if (k >> j) & 1:
            for x in range(j * esz // 8, (j + 1) * esz // 8):
                out[x] = 0xFF
    return out


def cases_dq():
    rnd = random.Random(0x5150800)
    lines = []
    a = lines.append
    a('# AVX512DQ VPMOVD2M / VPMOVQ2M / VPMOVM2D / VPMOVM2Q (ledger U800): expected values from the')
    a('# independent SDM model Emulator/tools/isa/ref_sysins.py (regenerate with --write, do not edit).')
    a('# The i5-13600K has no AVX-512: expected-value cases only, run with AVX-512 or AVX10.1:')
    a('#   emu-alltest --cases Emulator\\data\\cases_sysins_dq.txt --avx512 --expect-only')
    a('#   emu-alltest --cases Emulator\\data\\cases_sysins_dq.txt --avx10 1 --expect-only')
    names = {(0x38, 0): 'VPMOVM2D', (0x38, 1): 'VPMOVM2Q', (0x39, 0): 'VPMOVD2M', (0x39, 1): 'VPMOVQ2M'}
    vls = {0: 128, 1: 256, 2: 512}

    def rzmm():
        # mix of sign bits: random bytes, then force some element tops
        return bytearray(rnd.getrandbits(8) for _ in range(64))

    # register choices: VPMOVM2x (dest zmm, source k), VPMOVx2M (dest k, source zmm)
    m2_regs = [(0, 2), (9, 7), (17, 1), (31, 0), (12, 5)]
    tom_regs = [(0, 2), (7, 9), (1, 17), (3, 31), (5, 26)]
    for op in (0x38, 0x39):
        for w in (0, 1):
            esz = 64 if w else 32
            for ll in (0, 1, 2):
                vl = vls[ll]
                a('# %s VL%d' % (names[(op, w)], vl))
                regs = m2_regs if op == 0x38 else tom_regs
                for reg, rm in regs:
                    kin = rnd.getrandbits(64)
                    z = rzmm()
                    if op == 0x38:
                        enc = evex(op, 3, reg, rm, w, ll, 2)
                        exp = vpmovm2(kin, esz, vl)
                        a('.byte %s | zmm%d=%s k%d=0x%X => zmm%d=%s' %
                          (bytelist(enc), reg, hexbytes(z), rm, kin, reg, hexbytes(exp)))
                    else:
                        enc = evex(op, 3, reg, rm, w, ll, 2)
                        exp = vpmov2m(z, esz, vl)
                        a('.byte %s | zmm%d=%s k%d=0x%X => k%d=0x%X' %
                          (bytelist(enc), rm, hexbytes(z), reg, kin, reg, exp))
                # all-ones / all-zero / alternating patterns
                for pat in (0xFFFFFFFFFFFFFFFF, 0, 0x5555555555555555):
                    if op == 0x38:
                        z = rzmm()
                        enc = evex(op, 3, 3, 4, w, ll, 2)
                        a('.byte %s | zmm3=%s k4=0x%X => zmm3=%s' %
                          (bytelist(enc), hexbytes(z), pat, hexbytes(vpmovm2(pat, esz, vl))))
                    else:
                        z = bytearray(pat.to_bytes(8, 'little') * 8)
                        enc = evex(op, 3, 6, 4, w, ll, 2)
                        a('.byte %s | zmm4=%s k6=0xFFFFFFFFFFFFFFFF => k6=0x%X' %
                          (bytelist(enc), hexbytes(z), vpmov2m(z, esz, vl)))
    # Table 2-41: X / B are ignored when ModRM.r/m encodes a k-reg (VPMOVM2x)
    a('# VPMOVM2D/Q: EVEX.X / EVEX.B ignored for the k-reg in ModRM.r/m (Table 2-41)')
    for w in (0, 1):
        esz = 64 if w else 32
        for rx, rb in ((0, 1), (1, 0), (0, 0)):
            kin = rnd.getrandbits(64)
            z = rzmm()
            enc = evex(0x38, 3, 5, 3, w, 2, 2, raw_x=rx, raw_b=rb)
            a('.byte %s | zmm5=%s k3=0x%X => zmm5=%s' %
              (bytelist(enc), hexbytes(z), kin, hexbytes(vpmovm2(kin, esz, 512))))
    # #UD conditions
    a('# #UD: memory form, aaa != 0, z = 1, b = 1, L\'L = 11b, vvvv != 1111b, V\' = 0, k-reg in ModRM.reg')
    a('# with EVEX.R = 0 or EVEX.R\' = 0 (stored bits), NP / F2 forms of 38 and 39')
    for op in (0x38, 0x39):
        for w in (0, 1):
            ud = [
                evex(op, 0, 1, 6, w, 2, 2),                   # [rsi]
                evex(op, 1, 1, 6, w, 2, 2, tail=b'\x40'),     # [rsi+disp8]
                evex(op, 3, 1, 2, w, 2, 2, aaa=1),
                evex(op, 3, 1, 2, w, 2, 2, aaa=7),
                evex(op, 3, 1, 2, w, 2, 2, z=1),
                evex(op, 3, 1, 2, w, 2, 2, b=1),
                evex(op, 3, 1, 2, w, 3, 2),
                evex(op, 3, 1, 2, w, 2, 2, vvvv=1),
                evex(op, 3, 1, 2, w, 2, 2, vvvv=15),
                evex(op, 3, 1, 2, w, 2, 2, vhi=1),
                evex(op, 3, 1, 2, w, 2, 0),                   # NP
                evex(op, 3, 1, 2, w, 2, 3),                   # F2
            ]
            if op == 0x39:
                ud.append(evex(op, 3, 1, 2, w, 2, 2, raw_r=0))
                ud.append(evex(op, 3, 1, 2, w, 2, 2, raw_rp=0))
            for enc in ud:
                a('.byte %s => #UD' % bytelist(enc))
    return lines


# ------------------------------------------------------------------------------------------------
def write_file(name, lines):
    path = os.path.join(DATA, name)
    with open(path, 'w', newline='\r\n') as f:
        for l in lines:
            f.write(l + '\n')
    n = sum(1 for l in lines if l and not l.startswith('#'))
    print('%d cases written to %s' % (n, path))


def selftest():
    ok = True

    def chk(name, got, exp):
        nonlocal ok
        if got != exp:
            ok = False
            print('FAIL %s: got %r expected %r' % (name, got, exp))

    # EVEX bytes against the encodings of the emu-alltest sweep (alltest.csv) / asmjit db:
    # vpmovm2d zmm0, k2 = 62 F2 7E 48 38 C2; vpmovq2m k0, xmm2 = 62 F2 FE 08 39 C2
    chk('evex vpmovm2d', evex(0x38, 3, 0, 2, 0, 2, 2), bytes.fromhex('62F27E4838C2'))
    chk('evex vpmovq2m', evex(0x39, 3, 0, 2, 1, 0, 2), bytes.fromhex('62F2FE0839C2'))
    chk('evex zmm31', evex(0x38, 3, 31, 0, 0, 2, 2)[1], 0x62)      # R = R' = 1 stored as 0
    # VPMOVD2M on 4 doublewords 80000000h, 7FFFFFFFh, FFFFFFFFh, 0 -> k = 0101b
    src = bytearray(64)
    for j, v in enumerate((0x80000000, 0x7FFFFFFF, 0xFFFFFFFF, 0)):
        src[4 * j:4 * j + 4] = v.to_bytes(4, 'little')
    src[16:] = b'\xff' * 48                     # beyond VL128: ignored
    chk('d2m 128', vpmov2m(src, 32, 128), 0b0101)
    chk('d2m 512', vpmov2m(src, 32, 512), 0xFFF5)
    chk('q2m 128', vpmov2m(src, 64, 128), 0)       # qwords 7FFFFFFF80000000h, FFFFFFFFh
    chk('q2m 256', vpmov2m(src, 64, 256), 0b1100)
    chk('m2d 128', vpmovm2(0b1001, 32, 128)[:16], bytes.fromhex('FFFFFFFF' '00000000' '00000000' 'FFFFFFFF'))
    chk('m2d 128 upper', vpmovm2(0xFFFFFFFF, 32, 128)[16:], bytes(48))
    chk('m2q 512', vpmovm2(0x81, 64, 512), bytearray(b'\xff' * 8 + bytes(48) + b'\xff' * 8))
    try:
        cases_dq()
    except Exception as e:                      # pragma: no cover
        ok = False
        print('FAIL case generation: %s' % e)
    return ok


def main():
    if '--selftest' in sys.argv:
        ok = selftest()
        print('selftest %s' % ('passed' if ok else 'FAILED'))
        sys.exit(0 if ok else 1)
    if '--write' in sys.argv:
        write_file('cases_sysins_dq.txt', cases_dq())
        return
    print(__doc__)
    sys.exit(2)


if __name__ == '__main__':
    main()
