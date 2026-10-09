#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_apx_map4_sweep.py -- decode sweep of Intel APX EVEX map 4 (ledger U640-U646): every opcode
byte x pp x W x ND x NF x ModRM.mod (register / memory) (x ModRM.reg for the opcodes whose forms
fix it), each either #UD or not, written as Emulator/data/cases_apx_map4_sweep.txt:
  "<bytes> | <state> => #UD"     the combination encodes no instruction
  "<bytes> | <state> =>!"        it does: it must run without any fault (loose: no fields)
The valid set comes from the APX spec 355828-009 table of 3.1.5 (the per-opcode rules of
apx_spec_table() below) and is cross-checked against the Intel XED APX datafiles (the patterns
of datafiles/apx-f/*.xed.txt with MAP4: opcode, VNP/V66/VF2/VF3, MOD, REG, ND, NF / SCC, W0/W1);
the generator stops on any disagreement, so the file holds only combinations both agree on.
Combinations whose execution depends on run-time state (WRSS/WRUSS: CR4.CET; URDMSR/UWRMSR:
IA32_USER_MSR_CTL; ENQCMD/ENQCMDS: PASID / destination) and the instructions this CPU model
does not implement (INVEPT, INVVPID, INVPCID: #UD) are left out of the "=>!" lines.

The state makes every valid form fault-free: RAX = 1, RDX = 0 (DIV/IDIV: 1 / divisor), RSI =
0101H (the register r/m operand), RBX = MEM+8000H (ModRM.reg of the non-group opcodes and the
memory operand [RBX], which holds 1), RCX = 0 (shift counts), RSP 16-byte aligned (PUSH2/POP2);
V (NDD / PUSH2 / POP2) is 0 (RAX) or 9 (R9). An opcode without any map 4 form gets 12
combinations (each pp: W0 ND0 NF0 register and memory, W1 ND1 NF1 register).

Usage: python gen_apx_map4_sweep.py [XED_APX_DIR] > Emulator/data/cases_apx_map4_sweep.txt
"""
import glob
import os
import re
import sys

MEM = 0x30020000
MEM_PTR = MEM + 0x8000
XED_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "..",
                           "Intel docs", "xed-main", "datafiles", "apx-f")


# --------------------------------------------------------------------------------------------
# the spec table (3.1.5) as rules: opc -> list of (pp set, mod: 'r'/'m'/'*', reg (None = any),
# nd set, nf set, w set)
# --------------------------------------------------------------------------------------------
NP, P66, F3, F2 = 0, 1, 2, 3
ANYW = (0, 1)


def apx_spec_table():
    T = {}

    def add(opc, pps, mod, reg, nds, nfs, ws=ANYW, vr="nd"):
        """vr: 'nd' = V must be 0 unless ND = 1 (the NDD), 'zero' = V must be 0 (ZU forms),
        'any' = V is not a register id (CCMP/CTEST DFV) or any GPR but RSP (PUSH2/POP2)"""
        T.setdefault(opc, []).append((set(pps), mod, reg, set(nds), set(nfs), set(ws), vr))
    # ADD OR ADC SBB AND SUB XOR, CMP (CCMP: ND 0, NF = SC2: both)
    for k in range(8):
        nf = (0, 1) if k in (0, 1, 4, 5, 6, 7) else (0,)
        nd = (0,) if k == 7 else (0, 1)
        for low in range(4):
            opc = 8 * k + low
            pps = (NP,) if low % 2 == 0 else (NP, P66)
            add(opc, pps, "*", None, nd, nf, vr="any" if k == 7 else "nd")
    for opc, pps in ((0x80, (NP,)), (0x81, (NP, P66)), (0x83, (NP, P66))):
        for reg in range(8):
            nf = (0, 1) if reg in (0, 1, 4, 5, 6, 7) else (0,)
            add(opc, pps, "*", reg, (0,) if reg == 7 else (0, 1), nf, vr="any" if reg == 7 else "nd")
    for opc, pps in ((0x84, (NP,)), (0x85, (NP, P66))):
        add(opc, pps, "*", None, (0,), (0, 1), vr="any")          # CTEST
    for opc, pps in ((0xF6, (NP,)), (0xF7, (NP, P66))):
        add(opc, pps, "*", 0, (0,), (0, 1), vr="any")             # CTEST imm
        add(opc, pps, "*", 1, (0,), (0, 1), vr="any")
        add(opc, pps, "*", 2, (0, 1), (0,))                       # NOT
        add(opc, pps, "*", 3, (0, 1), (0, 1))                     # NEG
        for reg in (4, 5, 6, 7):
            add(opc, pps, "*", reg, (0,), (0, 1))                 # MUL IMUL DIV IDIV
    for opc, pps in ((0xFE, (NP,)), (0xFF, (NP, P66))):
        add(opc, pps, "*", 0, (0, 1), (0, 1))
        add(opc, pps, "*", 1, (0, 1), (0, 1))
    add(0xFF, (NP,), "r", 6, (1,), (0,), vr="any")                # PUSH2
    add(0x8F, (NP,), "r", 0, (1,), (0,), vr="any")                # POP2
    for opc, pps in ((0xC0, (NP,)), (0xC1, (NP, P66)), (0xD0, (NP,)), (0xD1, (NP, P66)),
                     (0xD2, (NP,)), (0xD3, (NP, P66))):
        for reg in range(8):
            add(opc, pps, "*", reg, (0, 1), (0,) if reg in (2, 3) else (0, 1))
    for opc in (0x69, 0x6B):
        add(opc, (NP, P66), "*", None, (0, 1), (0, 1), vr="zero") # IMUL (ZU)
    for opc in (0x24, 0x2C, 0xA5, 0xAD, 0xAF):
        add(opc, (NP, P66), "*", None, (0, 1), (0, 1))            # SHLD SHRD IMUL
    for opc in range(0x40, 0x50):
        add(opc, (NP, P66), "*", None, (0, 1), (0, 1))            # CMOVcc / CFCMOVcc
        add(opc, (F2,), "*", None, (0, 1), (0,), vr="zero")       # SETcc (ZU)
    for opc in (0x88, 0xF4, 0xF5):
        add(opc, (NP, P66), "*", None, (0,), (0, 1))              # POPCNT TZCNT LZCNT
    add(0x60, (NP, P66), "*", None, (0,), (0,))                   # MOVBE
    add(0x61, (NP, P66), "*", None, (0,), (0,))
    add(0x65, (P66,), "m", None, (0,), (0,))                      # WRUSS
    add(0x66, (NP,), "m", None, (0,), (0,))                       # WRSS
    add(0x66, (P66, F3), "*", None, (0, 1), (0,))                 # ADCX ADOX
    add(0x8A, (NP,), "m", None, (0,), (0,), (0,))                 # MOVRS r8 (W0)
    add(0x8B, (NP, P66), "m", None, (0,), (0,))                   # MOVRS rv
    add(0xF0, (NP,), "*", None, (0,), (0,))                       # CRC32 r/m8
    add(0xF1, (NP, P66), "*", None, (0,), (0,))                   # CRC32 rv/mv
    add(0xF0, (F3,), "m", None, (0,), (0,))                       # INVEPT
    add(0xF1, (F3,), "m", None, (0,), (0,))                       # INVVPID
    add(0xF2, (F3,), "m", None, (0,), (0,))                       # INVPCID
    add(0xF8, (P66,), "m", None, (0,), (0,))                      # MOVDIR64B
    add(0xF8, (F2, F3), "m", None, (0,), (0,))                    # ENQCMD ENQCMDS
    add(0xF8, (F2, F3), "r", None, (0,), (0,), (0,))              # URDMSR UWRMSR (W0)
    add(0xF9, (NP,), "m", None, (0,), (0,))                       # MOVDIRI
    add(0xFC, (NP, P66, F2, F3), "m", None, (0,), (0,))           # AADD AAND AOR AXOR
    return T


RUNTIME = {(0x65, P66), (0x66, NP), (0xF8, F2), (0xF8, F3), (0xF0, F3), (0xF1, F3), (0xF2, F3)}


def spec_valid(T, opc, pp, w, nd, nf, mod3, reg, v):
    for pps, mod, r, nds, nfs, ws, vr in T.get(opc, []):
        if pp not in pps or (mod != "*" and (mod == "r") != mod3) or (r is not None and r != reg):
            continue
        if nd not in nds or nf not in nfs or w not in ws:
            continue
        if vr == "any" or v == 0 or (vr == "nd" and nd == 1):
            return True
    return False


# --------------------------------------------------------------------------------------------
# XED patterns (MAP4)
# --------------------------------------------------------------------------------------------
def xed_patterns(xdir):
    pats = []
    for f in sorted(glob.glob(os.path.join(xdir, "*.xed.txt"))):
        for line in open(f, encoding="utf-8", errors="replace"):
            if not line.startswith("PATTERN:") or " MAP4 " not in line:
                continue
            tok = line.split()[1:]
            p = {"opc": None, "pp": None, "mod": "*", "reg": None, "nd": None, "nf": None, "w": None, "scc": False,
                 "noevsr": False}
            for t in tok:
                if re.fullmatch(r"0x[0-9A-Fa-f]{2}", t):
                    p["opc"] = int(t, 16)
                elif t in ("VNP", "V66", "VF3", "VF2"):
                    p["pp"] = {"VNP": NP, "V66": P66, "VF3": F3, "VF2": F2}[t]
                elif t == "MOD=3":
                    p["mod"] = "r"
                elif t == "MOD!=3":
                    p["mod"] = "m"
                elif t.startswith("REG[0b"):
                    p["reg"] = int(t[6:9], 2)
                elif t in ("ND=0", "ND=1"):
                    p["nd"] = int(t[3])
                elif t in ("NF=0", "NF=1", "NO_SCC_NF0", "NO_SCC_NF1"):
                    p["nf"] = int(t[-1])
                elif t in ("W0", "W1"):
                    p["w"] = int(t[1])
                elif t == "EVAPX_SCC()":
                    p["scc"] = True
                elif t == "NOEVSR":
                    p["noevsr"] = True
            pats.append(p)
    return pats


def xed_valid(pats, opc, pp, w, nd, nf, mod3, reg, v):
    for p in pats:
        if p["opc"] != opc or p["pp"] != pp:
            continue
        if p["mod"] != "*" and (p["mod"] == "r") != mod3:
            continue
        if p["reg"] is not None and p["reg"] != reg:
            continue
        if p["w"] is not None and p["w"] != w:
            continue
        if p["scc"]:
            if nd == 0:              # SCC forms: ND = 0, NF is SC2 (any)
                return True
            continue
        if p["nd"] is not None and p["nd"] != nd:
            continue
        if p["nf"] is not None and p["nf"] != nf:
            continue
        if p["noevsr"] and v != 0:
            continue
        return True
    return False


# --------------------------------------------------------------------------------------------
def enc(opc, pp, w, nd, nf, mod3, reg, v):
    """EVEX map 4: ModRM.reg = reg (R3 = R4 = 0), r/m = RSI (register) or [RBX] (memory),
    V = v (0 = RAX or 9 = R9)"""
    p0 = 0xF4                                       # R3 X3 B3 R4 = 0, B4 = 0, map 4
    p1 = (w << 7) | (((~v) & 15) << 3) | 0x04 | pp  # U = 1
    p2 = (nd << 4) | (((~v >> 4) & 1) << 3) | (nf << 2)
    modrm = (0xC0 | (reg << 3) | 6) if mod3 else ((reg << 3) | 3)
    tail = bytes([modrm])
    # immediates by opcode (map 4 keeps the legacy ones)
    imm = b""
    if opc in (0x80, 0x83, 0xC0, 0xC1, 0x6B, 0x24, 0x2C):
        imm = b"\x01"
    elif opc in (0x81, 0x69):
        imm = b"\x01\x00" if (pp == P66 and not w) else b"\x01\x00\x00\x00"
    elif opc in (0xF6, 0xF7) and reg in (0, 1):
        imm = b"\x01" if opc == 0xF6 else (b"\x01\x00" if (pp == P66 and not w) else b"\x01\x00\x00\x00")
    return bytes([0x62, p0, p1, p2, opc]) + tail + imm


GROUPS = {0x80, 0x81, 0x83, 0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3, 0xF6, 0xF7, 0xFE, 0xFF, 0x8F}


def main():
    xdir = sys.argv[1] if len(sys.argv) > 1 else XED_DEFAULT
    T = apx_spec_table()
    pats = xed_patterns(xdir)
    if not pats:
        print("no XED MAP4 patterns under %s" % xdir, file=sys.stderr)
        sys.exit(2)
    state = "rax=0x1 rsi=0x101 rbx=0x%X m+0x8000=0100000000000000" % MEM_PTR
    out = sys.stdout
    out.write("# Intel APX EVEX map 4 decode sweep (ledger U640-U646): every opcode x pp x W x ND x NF x\n")
    out.write("# register/memory (x ModRM.reg for group opcodes); \"=> #UD\" = no instruction, \"=>!\" = runs\n")
    out.write("# without a fault. Valid set: APX spec 355828-009 3.1.5, cross-checked against Intel XED.\n")
    out.write("# Generated by Emulator/tools/isa/gen_apx_map4_sweep.py (do not edit); run with --apx:\n")
    out.write("#   emu-alltest --cases Emulator\\data\\cases_apx_map4_sweep.txt --apx --expect-only\n")
    n_ud = n_ok = 0
    bad = []
    for opc in range(256):
        regs = range(8) if opc in GROUPS else (3,)
        # an opcode with no form in either source: 12 combinations per opcode are enough
        full = opc in T or any(p["opc"] == opc for p in pats)
        for pp in range(4):
            for w in (0, 1):
                for nd in (0, 1):
                    for nf in (0, 1):
                        if not full and (w, nd, nf) not in ((0, 0, 0), (1, 1, 1)):
                            continue
                        for mod3 in (True, False):
                            if not full and nd and not mod3:
                                continue
                            for reg, v in [(r, v) for r in regs for v in ((0, 9) if nd else (0,))]:
                                if not full and v == 0 and nd:
                                    continue
                                sv = spec_valid(T, opc, pp, w, nd, nf, mod3, reg, v)
                                xv = xed_valid(pats, opc, pp, w, nd, nf, mod3, reg, v)
                                if sv != xv:
                                    bad.append((opc, pp, w, nd, nf, mod3, reg, sv, xv))
                                    continue
                                b = enc(opc, pp, w, nd, nf, mod3, reg, v)
                                line = ".byte " + ", ".join("0x%02x" % x for x in b)
                                if not sv:
                                    out.write("%s | %s => #UD\n" % (line, state))
                                    n_ud += 1
                                elif (opc, pp) not in RUNTIME:
                                    out.write("%s | %s =>!\n" % (line, state))
                                    n_ok += 1
    for x in bad[:40]:
        print("spec/XED disagree: opc %02X pp %d W%d ND%d NF%d %s reg %d: spec %s XED %s" % (
            x[0], x[1], x[2], x[3], x[4], "reg" if x[5] else "mem", x[6], x[7], x[8]), file=sys.stderr)
    print("sweep: %d #UD lines, %d run lines, %d disagreements" % (n_ud, n_ok, len(bad)), file=sys.stderr)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
