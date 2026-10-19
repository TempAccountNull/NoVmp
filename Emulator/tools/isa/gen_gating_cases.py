#!/usr/bin/env python3
"""gen_gating_cases.py - the "GATING ONLY" tests of the reduced Gate 1 checklist (plan item 4, your decision
2026-10-17 (G10), notes/eread/G10_rescope_pe_only.md section 1; ledger U1630-U1632; notes/eread/G13_gating_only.md).

A GATING ONLY feature is one that neither real profile enumerates (bare i5-13600K host = --host, VMware guest =
--guest / --vm). It closes as "CPUID + #UD verified" once a test shows, on every profile, that CPUID reports the
feature absent (the bit is cited) and that every instruction form of the feature raises #UD (or the documented
absent-feature behaviour: the MPX BND* forms are NOPs; CR4.LA57 = 1 is #GP(0)).

Generates (Emulator/data, CRLF, ASCII; deterministic: the same inputs give the same bytes):
  cases_gating.txt         expected-value file for EVERY profile (the absent bits are 0 on all three CPUID profiles):
                             emu-alltest --cases Emulator\\data\\cases_gating.txt --host --expect-only
                             (also --host --host-core E, --guest, --vm; suites gating_host_p / _host_e / _guest / _vm)
  cases_gating_host.txt    per-profile expected values (1.J.37.c CET: CET_SS + CET_U / CET_S, IBT = 1 on the host)
  cases_gating_guest.txt   the same for the VMware guest (IBT = 0)
  cases_gating_hw.txt      hardware cases (this machine = the VMware guest): the CPUID bits and the first encoding of
                           every user-mode-executable form, at CPL3 on both engines (suite cases_gating_hw)

Every form = every (mnemonic, encoding) row of Emulator/data/isa_manual_forms.tsv (the instruction table's rows)
whose ISA families are all gating families. Encodings, all self-written (never sample bytes):
  - EVEX (AVX-512, AVX10, AVX512_FP16, Xeon Phi, AMX-AVX512, ACE): every row of Emulator/data/evex_forms.tsv
    (map / pp / opcode / ModRM / W / VL), each VL and each register / memory form; the VCMP pseudo-op rows as their base
    opcode with the predicate imm8
  - APX (EVEX map 4 / promoted, REX2): the first encoding of each form in the APX case files, found by apx_cov.py's
    independent APX decoder (Emulator/data/cases_apx_*.txt); WRMSRNS imm32 (EVEX.MAP7) from the MSR-IMM table
  - VEX / XOP / legacy: the HAND table below (SDM / ISE / AMD APM opcode lines)
Each encoding is decoded back with Capstone when the capstone module is importable (a check, printed only; the
output never depends on it).

usage: python gen_gating_cases.py [--write | --check]   (default --check: regenerate in memory, compare, exit 1 if
       a file differs; --write rewrites the four files). Verbose: every group, every CPUID check, coverage counts.
"""
import os
import re
import sys
from collections import OrderedDict, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, '..', '..', 'data'))
sys.path.insert(0, HERE)
import apx_cov  # noqa: E402  (independent APX decoder, read only)

PROFILES = OrderedDict([
    ('host_p', 'cpuid_i5-13600k_bare_host.txt'),
    ('host_e', 'cpuid_i5-13600k_bare_host_ecore.txt'),
    ('guest', 'cpuid_i5-13600k.txt'),
])
REGS = ['EAX', 'EBX', 'ECX', 'EDX']
GPR = {'EAX': 'rax', 'EBX': 'rbx', 'ECX': 'rcx', 'EDX': 'rdx'}


def load_cpuid(path):
    regs = {}
    for line in open(path, encoding='utf-8'):
        t = line.split()
        if len(t) == 6 and len(t[0]) == 8 and all(len(x) == 8 for x in t[2:6]):
            regs[(int(t[0], 16), int(t[1], 16))] = [int(x, 16) for x in t[2:6]]
    return regs


# ------------------------------------------------------------------------------------------------ the groups
# A CPUID check: (leaf, subleaf, register, bit, name, expected value on every profile). Expected values = the clean
# captures (bare host 2026-10-16 Host_clean_20261016, VMware guest 2026-10-15 Run3_clean_guest_probes_20261015; G10
# section 0); the generator also checks them against the three profile files (which are those captures).
GROUPS = [
    {
        'id': 'AVX512',
        'title': 'AVX-512 / AVX10 (EVEX), AVX512_FP16, Xeon Phi, opmask (VEX K*)',
        'plan': ['1.J.37.f.1', 'regs leftovers (VCOMPRESS/VPCOMPRESS)', '1.15d', 'M2 leftovers', 'VRCP14/VRSQRT14',
                 'M3 leftovers', 'M4 leftovers (incl. AVX512_4VNNIW)', 'fp16 leftovers', 'Fixes leftovers (KMOV)',
                 '5.2 sub 6 (AVX-512 subsets, AVX10)'],
        'cpuid': [(7, 0, 'EBX', 16, 'AVX512F'), (7, 0, 'EBX', 17, 'AVX512DQ'), (7, 0, 'EBX', 21, 'AVX512_IFMA'),
                  (7, 0, 'EBX', 26, 'AVX512PF'), (7, 0, 'EBX', 27, 'AVX512ER'), (7, 0, 'EBX', 28, 'AVX512CD'),
                  (7, 0, 'EBX', 30, 'AVX512BW'), (7, 0, 'EBX', 31, 'AVX512VL'), (7, 0, 'ECX', 1, 'AVX512_VBMI'),
                  (7, 0, 'ECX', 6, 'AVX512_VBMI2'), (7, 0, 'ECX', 11, 'AVX512_VNNI'), (7, 0, 'ECX', 12, 'AVX512_BITALG'),
                  (7, 0, 'ECX', 14, 'AVX512_VPOPCNTDQ'), (7, 0, 'EDX', 2, 'AVX512_4VNNIW'),
                  (7, 0, 'EDX', 3, 'AVX512_4FMAPS'), (7, 0, 'EDX', 8, 'AVX512_VP2INTERSECT'),
                  (7, 0, 'EDX', 23, 'AVX512_FP16'), (7, 1, 'EAX', 5, 'AVX512_BF16'), (7, 1, 'EDX', 19, 'AVX10'),
                  (0xD, 0, 'EAX', 5, 'XCR0 opmask state supported'), (0xD, 0, 'EAX', 6, 'XCR0 ZMM_Hi256 supported'),
                  (0xD, 0, 'EAX', 7, 'XCR0 Hi16_ZMM supported')],
        'extra': [('XSETBV XCR0 = E7H (x87 | SSE | AVX | opmask | ZMM_Hi256 | Hi16_ZMM): unsupported bits #GP(0)',
                   '.byte 0x0f, 0x01, 0xd1 | rax=0xE7 rcx=0 rdx=0 => #GP(0)')],
    },
    {
        'id': 'AMX',
        'title': 'AMX (TILE / BF16 / INT8 / FP16 / COMPLEX / FP8 / TF32 / MOVRS / AVX512), ACE tile instructions',
        'plan': ['1.J.37.f.2 (AMX part)', 'AMX leftovers 2', '1.F.13.a (TILELOADDT1)'],
        'cpuid': [(7, 0, 'EDX', 24, 'AMX_TILE'), (7, 0, 'EDX', 22, 'AMX_BF16'), (7, 0, 'EDX', 25, 'AMX_INT8'),
                  (7, 1, 'EAX', 21, 'AMX_FP16'), (7, 1, 'EDX', 8, 'AMX_COMPLEX'), (7, 1, 'ECX', 11, 'ACE'),
                  (0xD, 0, 'EAX', 17, 'XCR0 XTILECFG supported'), (0xD, 0, 'EAX', 18, 'XCR0 XTILEDATA supported'),
                  (0xD, 1, 'ECX', 10, 'IA32_XSS PASID supported'), (0xD, 1, 'ECX', 13, 'IA32_XSS HDC supported')],
        'extra': [('XSETBV XCR0 = 60007H (x87 | SSE | AVX | XTILECFG | XTILEDATA): #GP(0)',
                   '.byte 0x0f, 0x01, 0xd1 | rax=0x60007 rcx=0 rdx=0 => #GP(0)'),
                  ('WRMSR IA32_XSS (DA0H) PASID [10]: not in CPUID.(0DH,1):ECX, #GP(0)',
                   '.byte 0x0f, 0x30 | rcx=0xda0 rax=0x400 rdx=0 => #GP(0)'),
                  ('WRMSR IA32_XSS (DA0H) HDC [13]: not in CPUID.(0DH,1):ECX, #GP(0)',
                   '.byte 0x0f, 0x30 | rcx=0xda0 rax=0x2000 rdx=0 => #GP(0)')],
    },
    {
        'id': 'APX',
        'title': 'APX (REX2, EVEX map 4 and the promoted EVEX forms, incl. APX INVEPT / INVVPID)',
        'plan': ['1.J.12.c (APX EVEX INVEPT / INVVPID)', 'APX leftovers', '1.F.13.a (APX parts)'],
        'cpuid': [(7, 1, 'EDX', 21, 'APX_F'), (0xD, 0, 'EAX', 19, 'XCR0 APX extended GPR state supported')],
        'extra': [('XSETBV XCR0 = 80007H (x87 | SSE | AVX | APX): #GP(0)',
                   '.byte 0x0f, 0x01, 0xd1 | rax=0x80007 rcx=0 rdx=0 => #GP(0)')],
    },
    {
        'id': 'SGX',
        'title': 'SGX (ENCLS / ENCLU / ENCLV)',
        'plan': ['1.J.37.f.2 (SGX part)', 'SGX part (ENCLS/ENCLU)'],
        'cpuid': [(7, 0, 'EBX', 2, 'SGX'), (7, 0, 'ECX', 30, 'SGX_LC'), (0x12, 0, 'EAX', 0, 'SGX1'),
                  (0x12, 0, 'EAX', 1, 'SGX2'), (0x12, 0, 'EAX', 5, 'OVERSUB (ENCLV)')],
        'extra': [],
    },
    {
        'id': 'TDX',
        'title': 'TDX / SEAM (SEAMCALL / SEAMRET / SEAMOPS / TDCALL)',
        'plan': ['1.J.13'],
        'cpuid': [],
        'extra': [('IA32_MTRRCAP (FEH) bit 15 SEAMRR = 0: "Processors that support SEAM architecture return '
                   'IA32_MTRRCAP bit 15 as 1" (no SEAM)',
                   'mov ecx, 0xfe; rdmsr; mov r8d, eax; shr r8d, 15; and r8d, 1 =>! r8=0'),
                  ('IA32_SEAMRR_BASE (1400H) does not exist without SEAMRR: RDMSR #GP(0)',
                   '.byte 0x0f, 0x32 | rcx=0x1400 => #GP(0)'),
                  ('CPUID.0:EAX = 20H, the maximum basic leaf: the TDX leaf 21H is not enumerated',
                   'xor eax, eax; xor ecx, ecx; cpuid; mov r8, rax =>! r8=0x20')],
    },
    {
        'id': 'UINTR',
        'title': 'UINTR (CLUI / STUI / TESTUI / UIRET / SENDUIPI)',
        'plan': ['1.J.37.a', 'AMX leftovers 2 (UINTR component)', 'UINTR leftovers'],
        'cpuid': [(7, 0, 'EDX', 5, 'UINTR'), (0xD, 1, 'ECX', 14, 'IA32_XSS UINTR state supported')],
        'extra': [('WRMSR IA32_XSS (DA0H) UINTR [14]: not in CPUID.(0DH,1):ECX, #GP(0)',
                   '.byte 0x0f, 0x30 | rcx=0xda0 rax=0x4000 rdx=0 => #GP(0)')],
    },
    {
        'id': 'ENQCMD',
        'title': 'ENQCMD / ENQCMDS (legacy map; the APX EVEX forms are in APX)',
        'plan': ['1.J.37.b', '1.F.13.a (ENQCMD)'],
        'cpuid': [(7, 0, 'ECX', 29, 'ENQCMD'), (0xD, 1, 'ECX', 10, 'IA32_XSS PASID supported')],
        'extra': [],
    },
    {
        'id': 'MPX',
        'title': 'MPX (BND* forms: NOP when MPX is absent, as on the CPU)',
        'plan': ['regs leftovers (MPX BND* forms)'],
        'cpuid': [(7, 0, 'EBX', 14, 'MPX'), (0xD, 0, 'EAX', 3, 'XCR0 BNDREGS supported'),
                  (0xD, 0, 'EAX', 4, 'XCR0 BNDCSR supported')],
        'extra': [('XSETBV XCR0 = 1FH (x87 | SSE | AVX | BNDREGS | BNDCSR): #GP(0)',
                   '.byte 0x0f, 0x01, 0xd1 | rax=0x1F rcx=0 rdx=0 => #GP(0)')],
    },
    {
        'id': 'LA57',
        'title': 'LA57 (5-level paging): CPUID absent, MOV CR4 with LA57 [12] = 1 #GP(0)',
        'plan': ['1.J.40.f.1'],
        'cpuid': [(7, 0, 'ECX', 16, 'LA57')],
        'extra': [('MOV CR4 with CR4.LA57 [12] = 1: reserved bit (LA57 not enumerated), #GP(0)',
                   'mov rax, cr4; or rax, 0x1000; mov cr4, rax =>! #GP(0)'),
                  ('MOV CR4 with CR4.LA57 [12] = 1 from a register: #GP(0), CR4 unchanged (bit 12 reads 0 afterwards)',
                   'mov rax, cr4; bts rax, 12; mov rcx, rax; mov cr4, rcx =>! #GP(0)')],
    },
    {
        'id': 'FRED',
        'title': 'FRED / LKGS (extra: not one of the 24 GATING ONLY lines; evidence for the ERETS / ERETU table rows, '
                 'G13 section 3)',
        'plan': ['Gate 1 item 2 (ERETS / ERETU rows, stopped for your decision)', 'G10 section 3 (FRED / LKGS gating only)'],
        'cpuid': [(7, 1, 'EAX', 17, 'FRED'), (7, 1, 'EAX', 18, 'LKGS')],
        'extra': [],
        # ERETS / ERETU only at CPL3: without FRED the emulator executes F2 / F3 0F 01 CA at CPL0 as CLAC
        # (docs/quirks.md, U1172 / U1182), which SDM 093 Vol2A ERETS / ERETU ("#UD If CR4.FRED = 0") does not say
        'cpl3': {'erets', 'eretu'},
    },
    {
        'id': 'AMD',
        'title': 'AMD / VIA-only (SVM, SSE4A, 3DNow!, XOP, FMA4, TBM, LWP, CLZERO, MCOMMIT, MONITORX, RDPRU, '
                 'INVLPGB, SEV-SNP, VIA PadLock)',
        'plan': ['2.0', '1.J.45.b (SVM)'],
        'cpuid': [(0x80000001, 0, 'ECX', 2, 'SVM'), (0x80000001, 0, 'ECX', 6, 'SSE4A'),
                  (0x80000001, 0, 'ECX', 11, 'XOP'), (0x80000001, 0, 'ECX', 15, 'LWP'),
                  (0x80000001, 0, 'ECX', 16, 'FMA4'), (0x80000001, 0, 'ECX', 21, 'TBM'),
                  (0x80000001, 0, 'ECX', 29, 'MONITORX'), (0x80000001, 0, 'EDX', 30, '3DNowExt'),
                  (0x80000001, 0, 'EDX', 31, '3DNow!'), (0x80000008, 0, 'EBX', 0, 'CLZERO'),
                  (0x80000008, 0, 'EBX', 3, 'INVLPGB'), (0x80000008, 0, 'EBX', 4, 'RDPRU'),
                  (0x80000008, 0, 'EBX', 8, 'MCOMMIT')],
        'extra': [('CPUID.0 vendor: EBX "Genu" 756E6547H', 'xor eax, eax; xor ecx, ecx; cpuid; mov r8, rbx =>! r8=0x756E6547'),
                  ('CPUID.0 vendor: EDX "ineI" 49656E69H', 'xor eax, eax; xor ecx, ecx; cpuid; mov r8, rdx =>! r8=0x49656E69'),
                  ('CPUID.0 vendor: ECX "ntel" 6C65746EH', 'xor eax, eax; xor ecx, ecx; cpuid; mov r8, rcx =>! r8=0x6C65746E')],
    },
]

# families of each group (instruction-table family names, gen_instruction_table.family())
AMD_FAMS = {'SVM', 'SSE4a', '3DNOW', 'XOP', 'FMA4', 'TBM', 'LWP', 'CLZERO', 'MCOMMIT', 'MONITORX', 'RDPRU',
            'AMD_INVLPGB', 'SNP', 'VIA_PADLOCK_AES', 'VIA_PADLOCK_SHA', 'VIA_PADLOCK_RNG', 'VIA_PADLOCK_MONTMUL'}


def family(isa):
    """gen_instruction_table.family() of one ISA component"""
    f = re.sub(r'_(128N?|256|512|SCALAR|KOP[BWDQ])$', '', isa)
    return f or '?'


def group_of(mn, enc, isas):
    fams = set(family(f) for isa in isas for f in isa.split('+'))
    if any(f.startswith('APX') for f in fams):
        return 'APX'
    if fams and all(f.startswith('AMX') or f == 'ACE_1' for f in fams):
        return 'AMX'
    if fams and all(f.startswith(('AVX512', 'AVX10', 'SM4')) for f in fams) and enc in ('evex', 'vex'):
        if enc == 'vex' and not all(f.startswith('AVX512') for f in fams):
            return None
        return 'AVX512'
    if enc == 'evex':                       # EVEX forms of GFNI / VAES / VPCLMULQDQ / MOVRS (AVX512_*_128 ISA sets)
        return 'AVX512'
    for g, fs in (('SGX', {'SGX', 'SGX_ENCLV'}), ('TDX', {'TDX'}), ('UINTR', {'UINTR'}), ('ENQCMD', {'ENQCMD'}),
                  ('MPX', {'MPX'}), ('FRED', {'FRED', 'LKGS'}), ('AMD', AMD_FAMS)):
        if fams and fams <= fs:
            return g
    return None


# ------------------------------------------------------------------------------------------------ encoders
MAPS = {'0F': 1, '0F38': 2, '0F3A': 3, 'MAP5': 5, 'MAP6': 6}
PPS = {'NP': 0, '66': 1, 'F3': 2, 'F2': 3}
LL = {'128': 0, '256': 1, '512': 2, 'LIG': 0}


def evex_bytes(f, vl, mod, imm=None):
    """62 P0 P1 P2 opcode ModRM [SIB] [imm8] with registers 0/1 (zmm0, zmm1 / [rsi], vsib [rsi+xmm1]), vvvv = 1111,
    V' = 1, aaa = 0 (k1 where the form requires a mask), z = b = 0, U = 1"""
    p0 = 0xF0 | MAPS[f['map']]                                    # ~R ~X ~B ~R' = 1111, bit 3 = 0, mmm
    w = 1 if f['W'] == '1' else 0
    p1 = (w << 7) | (0xF << 3) | 0x04 | PPS[f['pp']]
    p2 = (LL[vl] << 5) | 0x08 | (1 if f['masking'] == 'k-required' else 0)
    regf = int(f['modrm'][1:]) if re.fullmatch(r'/[0-7]', f['modrm']) else 0
    if f['modrm'] == 'vsib':
        tail = [0x04 | (regf << 3), 0x0E]                          # [rsi + xmm1*1]
    elif mod == 'reg':
        tail = [0xC0 | (regf << 3) | 1]
    else:
        tail = [0x06 | (regf << 3)]                                # [rsi]
    if f['imm'] == 'ib':
        tail.append(0 if imm is None else imm)
    return [0x62, p0, p1, p2, int(f['opcode'], 16)] + tail


def vex3(mp, pp, w, l, opc, modrm, v=0, tail=()):
    """C4 ~R~X~B.mmmmm W.~vvvv.L.pp opcode ModRM (registers 0..7)"""
    return [0xC4, 0xE0 | mp, (w << 7) | ((~v & 15) << 3) | (l << 2) | pp, opc, modrm] + list(tail)


def hx(s):
    return [int(s[i:i + 2], 16) for i in range(0, len(s), 2)]


# KMOV / K* opmask forms (SDM 093 Vol2 "Opcode/Instruction" lines). (pp, W) per size suffix:
K_SIZE = {'b': (1, 0), 'w': (0, 0), 'd': (1, 1), 'q': (0, 1)}


def kop_forms():
    out = OrderedDict()
    for op, opc in (('kadd', 0x4A), ('kand', 0x41), ('kandn', 0x42), ('kor', 0x45), ('kxnor', 0x46), ('kxor', 0x47)):
        for s, (pp, w) in K_SIZE.items():                         # VEX.L1 k1, k2, k3
            out[op + s] = [('%s k1, k2, k3' % (op + s), vex3(1, pp, w, 1, opc, 0xCB, v=2))]
    for op, opc in (('knot', 0x44), ('kortest', 0x98), ('ktest', 0x99)):
        for s, (pp, w) in K_SIZE.items():                         # VEX.L0 k1, k2
            out[op + s] = [('%s k1, k2' % (op + s), vex3(1, pp, w, 0, opc, 0xCA))]
    for mn, pp, w in (('kunpckbw', 1, 0), ('kunpckwd', 0, 0), ('kunpckdq', 0, 1)):
        out[mn] = [('%s k1, k2, k3' % mn, vex3(1, pp, w, 1, 0x4B, 0xCB, v=2))]
    gpr_pw = {'b': (1, 0), 'w': (0, 0), 'd': (3, 0), 'q': (3, 1)}  # 92 / 93: KMOVD / KMOVQ use F2
    for s, (pp, w) in K_SIZE.items():
        gp, gw = gpr_pw[s]
        out['kmov' + s] = [('kmov%s k1, k2' % s, vex3(1, pp, w, 0, 0x90, 0xCA)),
                           ('kmov%s k1, [rsi]' % s, vex3(1, pp, w, 0, 0x90, 0x0E)),
                           ('kmov%s [rsi], k1' % s, vex3(1, pp, w, 0, 0x91, 0x0E)),
                           ('kmov%s k1, %s' % (s, 'rax' if s == 'q' else 'eax'), vex3(1, gp, gw, 0, 0x92, 0xC8)),
                           ('kmov%s %s, k1' % (s, 'rax' if s == 'q' else 'eax'), vex3(1, gp, gw, 0, 0x93, 0xC1))]
    for mn, opc, w in (('kshiftrb', 0x30, 0), ('kshiftrw', 0x30, 1), ('kshiftrd', 0x31, 0), ('kshiftrq', 0x31, 1),
                       ('kshiftlb', 0x32, 0), ('kshiftlw', 0x32, 1), ('kshiftld', 0x33, 0), ('kshiftlq', 0x33, 1)):
        out[mn] = [('%s k1, k2, 3' % mn, vex3(3, 1, w, 0, opc, 0xCA, tail=[3]))]
    return out


def amx_forms():
    """AMX VEX forms (ISE 319433-062 / SDM 093 Vol2 opcode lines; the FP8 / TF32 / MOVRS lines of the ISE;
    BSRINIT: VEX.128.F2.0F38.W1 49 C0, ACE ISA set ACE_1). Tiles tmm0 (reg), tmm1 (r/m), tmm2 (vvvv);
    memory [rsi] or the SIB form [rsi + rcx*1]."""
    m, sib = 0x06, (0x04, [0x0E])
    rrr = lambda mp, pp, opc, w=0: vex3(mp, pp, w, 0, opc, 0xC1, v=2)
    return OrderedDict([
        ('ldtilecfg', [('ldtilecfg [rsi]', vex3(2, 0, 0, 0, 0x49, m))]),
        ('sttilecfg', [('sttilecfg [rsi]', vex3(2, 1, 0, 0, 0x49, m))]),
        ('tilerelease', [('tilerelease', vex3(2, 0, 0, 0, 0x49, 0xC0))]),
        ('tilezero', [('tilezero tmm0', vex3(2, 3, 0, 0, 0x49, 0xC0))]),
        ('tileloadd', [('tileloadd tmm0, [rsi+rcx]', vex3(2, 3, 0, 0, 0x4B, sib[0], tail=sib[1]))]),
        ('tileloaddt1', [('tileloaddt1 tmm0, [rsi+rcx]', vex3(2, 1, 0, 0, 0x4B, sib[0], tail=sib[1]))]),
        ('tilestored', [('tilestored [rsi+rcx], tmm0', vex3(2, 2, 0, 0, 0x4B, sib[0], tail=sib[1]))]),
        ('tileloaddrs', [('tileloaddrs tmm0, [rsi+rcx]', vex3(2, 3, 0, 0, 0x4A, sib[0], tail=sib[1]))]),
        ('tileloaddrst1', [('tileloaddrst1 tmm0, [rsi+rcx]', vex3(2, 1, 0, 0, 0x4A, sib[0], tail=sib[1]))]),
        ('tdpbf16ps', [('tdpbf16ps tmm0, tmm1, tmm2', rrr(2, 2, 0x5C))]),
        ('tdpfp16ps', [('tdpfp16ps tmm0, tmm1, tmm2', rrr(2, 3, 0x5C))]),
        ('tdpbssd', [('tdpbssd tmm0, tmm1, tmm2', rrr(2, 3, 0x5E))]),
        ('tdpbsud', [('tdpbsud tmm0, tmm1, tmm2', rrr(2, 2, 0x5E))]),
        ('tdpbusd', [('tdpbusd tmm0, tmm1, tmm2', rrr(2, 1, 0x5E))]),
        ('tdpbuud', [('tdpbuud tmm0, tmm1, tmm2', rrr(2, 0, 0x5E))]),
        ('tcmmimfp16ps', [('tcmmimfp16ps tmm0, tmm1, tmm2', rrr(2, 1, 0x6C))]),
        ('tcmmrlfp16ps', [('tcmmrlfp16ps tmm0, tmm1, tmm2', rrr(2, 0, 0x6C))]),
        ('tdpbf8ps', [('tdpbf8ps tmm0, tmm1, tmm2 (VEX.128.NP.MAP5.W0 FD)', rrr(5, 0, 0xFD))]),
        ('tdpbhf8ps', [('tdpbhf8ps tmm0, tmm1, tmm2 (VEX.128.F2.MAP5.W0 FD)', rrr(5, 3, 0xFD))]),
        ('tdphbf8ps', [('tdphbf8ps tmm0, tmm1, tmm2 (VEX.128.F3.MAP5.W0 FD)', rrr(5, 2, 0xFD))]),
        ('tdphf8ps', [('tdphf8ps tmm0, tmm1, tmm2 (VEX.128.66.MAP5.W0 FD)', rrr(5, 1, 0xFD))]),
        ('tmmultf32ps', [('tmmultf32ps tmm0, tmm1, tmm2 (VEX.128.66.0F38.W0 48)', rrr(2, 1, 0x48))]),
        ('bsrinit', [('bsrinit (VEX.128.F2.0F38.W1 49 C0)', vex3(2, 3, 1, 0, 0x49, 0xC0))]),
    ])


# legacy / XOP / FMA4 encodings (SDM 093 Vol2 / Vol3, AMD APM Vol3 / Vol4, VIA PadLock; XOP / FMA4 / TBM / 3DNow! /
# SSE4A bytes from Keystone, then decoded back with Capstone; registers 0..3, memory [rsi])
HAND = OrderedDict([
    # SGX (SDM 093 Vol3D chapter 40 / 41: ENCLS NP 0F 01 CF, ENCLU NP 0F 01 D7, ENCLV NP 0F 01 C0)
    ('encls', [('encls', '0f01cf')]), ('enclu', [('enclu', '0f01d7')]), ('enclv', [('enclv', '0f01c0')]),
    # TDX / SEAM (Vol3C 35.5, TDCAE 343754 ch. 5: 66 0F 01 CC TDCALL, CD SEAMRET, CE SEAMOPS, CF SEAMCALL)
    ('tdcall', [('tdcall', '660f01cc')]), ('seamret', [('seamret', '660f01cd')]),
    ('seamops', [('seamops', '660f01ce')]), ('seamcall', [('seamcall', '660f01cf')]),
    # UINTR (Vol2: F3 0F 01 EC UIRET, ED TESTUI, EE CLUI, EF STUI; F3 0F C7 /6 SENDUIPI r64)
    ('uiret', [('uiret', 'f30f01ec')]), ('testui', [('testui', 'f30f01ed')]), ('clui', [('clui', 'f30f01ee')]),
    ('stui', [('stui', 'f30f01ef')]), ('senduipi', [('senduipi rax', 'f30fc7f0')]),
    # ENQCMD (Vol2: F2 0F 38 F8 !(11):rrr:bbb ENQCMD r64, m512; F3 ... ENQCMDS)
    ('enqcmd', [('enqcmd rax, [rsi]', 'f20f38f806')]), ('enqcmds', [('enqcmds rax, [rsi]', 'f30f38f806')]),
    # MPX (0F 1A / 0F 1B; NOP space without MPX)
    ('bndcl', [('bndcl bnd0, [rsi]', 'f30f1a06'), ('bndcl bnd0, rax', 'f30f1ac0')]),
    ('bndcu', [('bndcu bnd0, [rsi]', 'f20f1a06'), ('bndcu bnd0, rax', 'f20f1ac0')]),
    ('bndcn', [('bndcn bnd0, [rsi]', 'f20f1b06'), ('bndcn bnd0, rax', 'f20f1bc0')]),
    ('bndmk', [('bndmk bnd0, [rsi]', 'f30f1b06')]),
    ('bndmov', [('bndmov bnd0, bnd1', '660f1ac1'), ('bndmov bnd0, [rsi]', '660f1a06'),
                ('bndmov [rsi], bnd0', '660f1b06')]),
    ('bndldx', [('bndldx bnd0, [rsi+rax]', '0f1a0406')]), ('bndstx', [('bndstx [rsi+rax], bnd0', '0f1b0406')]),
    # FRED / LKGS (SDM 093 Vol2A: F2 0F 01 CA ERETS, F3 0F 01 CA ERETU; LKGS F2 0F 00 /6, here LKGS AX)
    ('erets', [('erets', 'f20f01ca')]), ('eretu', [('eretu', 'f30f01ca')]), ('lkgs', [('lkgs ax', 'f20f00f0')]),
    # SVM (AMD APM Vol3: 0F 01 D8-DF)
    ('vmrun', [('vmrun', '0f01d8')]), ('vmmcall', [('vmmcall', '0f01d9')]), ('vmload', [('vmload', '0f01da')]),
    ('vmsave', [('vmsave', '0f01db')]), ('stgi', [('stgi', '0f01dc')]), ('clgi', [('clgi', '0f01dd')]),
    ('skinit', [('skinit', '0f01de')]), ('invlpga', [('invlpga', '0f01df')]),
    # AMD system / SEV-SNP (APM Vol3)
    ('monitorx', [('monitorx', '0f01fa')]), ('mwaitx', [('mwaitx', '0f01fb')]), ('clzero', [('clzero', '0f01fc')]),
    ('rdpru', [('rdpru', '0f01fd')]), ('invlpgb', [('invlpgb', '0f01fe')]), ('tlbsync', [('tlbsync', '0f01ff')]),
    ('mcommit', [('mcommit', 'f30f01fa')]), ('rmpadjust', [('rmpadjust', 'f30f01fe')]),
    ('psmash', [('psmash', 'f30f01ff')]), ('rmpupdate', [('rmpupdate', 'f20f01fe')]),
    ('pvalidate', [('pvalidate', 'f20f01ff')]),
    # LWP (AMD LWP spec: XOP.map9 12 /0 LLWPCB, /1 SLWPCB; XOP.mapA 12 /0 LWPINS, /1 LWPVAL, imm32)
    ('llwpcb', [('llwpcb eax', '8fe97812c0')]), ('slwpcb', [('slwpcb eax', '8fe97812c8')]),
    ('lwpins', [('lwpins eax, ecx, 0', '8fea7812c100000000')]),
    ('lwpval', [('lwpval eax, ecx, 0', '8fea7812c900000000')]),
    # VIA PadLock (0F A7 C0 XSTORE, F3 0F A7 C8-E8 XCRYPT*, F3 0F A6 C0 MONTMUL, C8 XSHA1, D0 XSHA256)
    ('xstore', [('xstore', '0fa7c0'), ('rep xstore', 'f30fa7c0')]), ('xcryptecb', [('rep xcryptecb', 'f30fa7c8')]),
    ('xcryptcbc', [('rep xcryptcbc', 'f30fa7d0')]), ('xcryptctr', [('rep xcryptctr', 'f30fa7d8')]),
    ('xcryptcfb', [('rep xcryptcfb', 'f30fa7e0')]), ('xcryptofb', [('rep xcryptofb', 'f30fa7e8')]),
    ('montmul', [('rep montmul', 'f30fa6c0')]), ('xsha1', [('rep xsha1', 'f30fa6c8')]),
    ('xsha256', [('rep xsha256', 'f30fa6d0')]),
    # SSE4A
    ('extrq', [('extrq xmm0, 4, 8', '660f78c00408'), ('extrq xmm0, xmm1', '660f79c1')]),
    ('insertq', [('insertq xmm0, xmm1, 4, 8', 'f20f78c10408'), ('insertq xmm0, xmm1', 'f20f79c1')]),
    ('movntsd', [('movntsd [rsi], xmm0', 'f20f2b06')]), ('movntss', [('movntss [rsi], xmm0', 'f30f2b06')]),
    # 3DNow! (0F 0F /r imm8 suffix; FEMMS 0F 0E)
    ('femms', [('femms', '0f0e')]),
])
for _mn, _sfx in (('pavgusb', 'bf'), ('pf2id', '1d'), ('pf2iw', '1c'), ('pfacc', 'ae'), ('pfadd', '9e'),
                  ('pfcmpeq', 'b0'), ('pfcmpge', '90'), ('pfcmpgt', 'a0'), ('pfmax', 'a4'), ('pfmin', '94'),
                  ('pfmul', 'b4'), ('pfnacc', '8a'), ('pfpnacc', '8e'), ('pfrcp', '96'), ('pfrcpit1', 'a6'),
                  ('pfrcpit2', 'b6'), ('pfrsqit1', 'a7'), ('pfrsqrt', '97'), ('pfsub', '9a'), ('pfsubr', 'aa'),
                  ('pi2fd', '0d'), ('pi2fw', '0c'), ('pmulhrw', 'b7'), ('pswapd', 'bb')):
    HAND[_mn] = [('%s mm0, mm1' % _mn, '0f0fc1' + _sfx), ('%s mm0, [rsi]' % _mn, '0f0f06' + _sfx)]
# XOP (AMD APM Vol4): 8F E8 (map 8, W0, vvvv = xmm1, L0) / 8F E9 (map 9)
for _mn, _op in (('vfrczpd', '81'), ('vfrczps', '80'), ('vfrczsd', '83'), ('vfrczss', '82'), ('vphaddbd', 'c2'),
                 ('vphaddbq', 'c3'), ('vphaddbw', 'c1'), ('vphadddq', 'cb'), ('vphaddubd', 'd2'), ('vphaddubq', 'd3'),
                 ('vphaddubw', 'd1'), ('vphaddudq', 'db'), ('vphadduwd', 'd6'), ('vphadduwq', 'd7'),
                 ('vphaddwd', 'c6'), ('vphaddwq', 'c7'), ('vphsubbw', 'e1'), ('vphsubdq', 'e3'), ('vphsubwd', 'e2')):
    HAND[_mn] = [('%s xmm0, xmm1' % _mn, '8fe978' + _op + 'c1')]
for _mn, _op in (('vpcmov', 'a2'), ('vpperm', 'a3'), ('vpmacsdd', '9e'), ('vpmacsdqh', '9f'), ('vpmacsdql', '97'),
                 ('vpmacssdd', '8e'), ('vpmacssdqh', '8f'), ('vpmacssdql', '87'), ('vpmacsswd', '86'),
                 ('vpmacssww', '85'), ('vpmacswd', '96'), ('vpmacsww', '95'), ('vpmadcsswd', 'a6'),
                 ('vpmadcswd', 'b6')):
    HAND[_mn] = [('%s xmm0, xmm1, xmm2, xmm3' % _mn, '8fe870' + _op + 'c230')]
for _mn, _op in (('vprotb', '90'), ('vprotw', '91'), ('vprotd', '92'), ('vprotq', '93'), ('vpshab', '98'),
                 ('vpshaw', '99'), ('vpshad', '9a'), ('vpshaq', '9b'), ('vpshlb', '94'), ('vpshlw', '95'),
                 ('vpshld', '96'), ('vpshlq', '97')):
    HAND[_mn] = [('%s xmm0, xmm1, xmm2' % _mn, '8fe968' + _op + 'c1')]
VPCOM_OP = OrderedDict([('b', 'cc'), ('w', 'cd'), ('d', 'ce'), ('q', 'cf'), ('ub', 'ec'), ('uw', 'ed'), ('ud', 'ee'),
                        ('uq', 'ef')])
VPCOM_PRED = ['lt', 'le', 'gt', 'ge', 'eq', 'neq', 'false', 'true']
for _s, _op in VPCOM_OP.items():
    HAND['vpcom' + _s] = [('vpcom%s xmm0, xmm1, xmm2, 0' % _s, '8fe870' + _op + 'c200')]
    for _i, _p in enumerate(VPCOM_PRED):
        HAND['vpcom%s%s' % (_p, _s)] = [('vpcom%s%s xmm0, xmm1, xmm2 (imm8 %d)' % (_p, _s, _i),
                                         '8fe870' + _op + 'c2%02x' % _i)]
HAND['vpermil2pd'] = [('vpermil2pd xmm0, xmm1, xmm2, xmm3, 0', 'c4e37149c230')]
HAND['vpermil2ps'] = [('vpermil2ps xmm0, xmm1, xmm2, xmm3, 0', 'c4e37148c230')]
# FMA4 (VEX.0F3A W1 = the register / memory operand order of the 4th operand form; xmm0, xmm1, xmm2, xmm3)
for _mn, _op in (('vfmaddpd', '69'), ('vfmaddps', '68'), ('vfmaddsd', '6b'), ('vfmaddss', '6a'), ('vfmaddsubpd', '5d'),
                 ('vfmaddsubps', '5c'), ('vfmsubaddpd', '5f'), ('vfmsubaddps', '5e'), ('vfmsubpd', '6d'),
                 ('vfmsubps', '6c'), ('vfmsubsd', '6f'), ('vfmsubss', '6e'), ('vfnmaddpd', '79'), ('vfnmaddps', '78'),
                 ('vfnmaddsd', '7b'), ('vfnmaddss', '7a'), ('vfnmsubpd', '7d'), ('vfnmsubps', '7c'),
                 ('vfnmsubsd', '7f'), ('vfnmsubss', '7e')):
    HAND[_mn] = [('%s xmm0, xmm1, xmm2, xmm3' % _mn, 'c4e3f1' + _op + 'c320')]
# TBM (XOP.map9 01 / 02 /digit, XOP.mapA 10 BEXTR imm32)
HAND['bextr'] = [('bextr eax, ecx, 0x1234 (XOP.mapA 10)', '8fea7810c134120000')]
for _mn, _b in (('blcfill', '01c9'), ('blci', '02f1'), ('blcic', '01e9'), ('blcmsk', '02c9'), ('blcs', '01d9'),
                ('blsfill', '01d1'), ('blsic', '01f1'), ('t1mskc', '01f9'), ('tzmsk', '01e1')):
    HAND[_mn] = [('%s eax, ecx' % _mn, '8fe978' + _b)]

# VCMP pseudo-op predicates (SDM 093 Vol2A CMPPS Table 3-2 / VCMPPS imm8)
VCMP_PRED = ['eq', 'lt', 'le', 'unord', 'neq', 'nlt', 'nle', 'ord', 'eq_uq', 'nge', 'ngt', 'false', 'neq_oq', 'ge',
             'gt', 'true', 'eq_os', 'lt_oq', 'le_oq', 'unord_s', 'neq_us', 'nlt_uq', 'nle_uq', 'ord_s', 'eq_us',
             'nge_uq', 'ngt_uq', 'false_os', 'neq_os', 'ge_oq', 'gt_oq', 'true_us']
VCMP_RE = re.compile(r'vcmp(%s)(ps|pd|ss|sd|ph|sh)' % '|'.join(sorted(VCMP_PRED, key=len, reverse=True)))


# ------------------------------------------------------------------------------------------------ inputs
def load_rows():
    rows = OrderedDict()
    for line in open(os.path.join(DATA, 'isa_manual_forms.tsv'), encoding='utf-8'):
        if not line.strip() or line.startswith('#'):
            continue
        t = line.rstrip('\n').split('\t')
        rows.setdefault((t[0].lower(), t[1]), []).append(t[3])
    return rows


def load_evex_forms():
    by = defaultdict(list)
    hdr = None
    for line in open(os.path.join(DATA, 'evex_forms.tsv'), encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        t = line.rstrip('\n').split('\t')
        if hdr is None:
            hdr = t
            continue
        d = dict(zip(hdr, t))
        by[d['mnemonic'].lower()].append(d)
    return by


def evex_lines(forms, imm=None, label=None):
    out = []
    for f in forms:
        vls = ['LIG'] if f['vl'] == 'LIG' else f['vl'].split(',')
        mods = {'any': ['reg', 'mem'], 'reg': ['reg'], 'mem': ['mem']}[f['mod']] if f['modrm'] != 'vsib' else ['mem']
        for vl in vls:
            for mod in mods:
                b = evex_bytes(f, vl, mod, imm)
                desc = '%s EVEX.%s.%s.%s.W%s %s %s%s, %s' % (
                    (label or f['mnemonic']).upper(), vl, f['pp'], f['map'], f['W'].upper() if f['W'] != 'ig' else 'IG',
                    f['opcode'], f['modrm'], ' ib' if f['imm'] == 'ib' else '', mod if f['modrm'] != 'vsib' else 'vsib')
                out.append((desc, b))
    return out


def apx_harvest():
    """first encoding of every APX form in the APX case files (apx_cov.decode, apx_cov.FILES order)"""
    found = OrderedDict()
    for fn in apx_cov.FILES:
        for no, ln in enumerate(open(os.path.join(DATA, fn), encoding='utf-8'), 1):
            ln = ln.rstrip('\n')
            if not ln.strip() or ln.startswith('#'):
                continue
            code = ln.split('~~', 1)[1].split('|')[0] if '~~' in ln else re.split(r'\s\|\s|\s=>', ln)[0]
            for s in apx_cov.segs(code):
                pos = 0                                       # as apx_cov.main: successive APX forms of one run
                while pos < len(s):
                    try:
                        F, L, desc = apx_cov.decode(s[pos:])
                    except (apx_cov.Dec, IndexError):
                        break
                    if not F or L is None:
                        break
                    for f in F:
                        if f not in found:
                            found[f] = (list(s[pos:pos + L]), '%s line %d (%s)' % (fn, no, desc))
                    pos += L
    return found


# ------------------------------------------------------------------------------------------------ capstone check
# Capstone 5 does not know these forms and decodes the opcode without its mandatory prefix (F3 0F 01 EE / EF are
# CLUI / STUI, not RDPKRU / WRPKRU; 66 0F 01 CF is SEAMCALL, not ENCLS; F3 0F 01 FA is MCOMMIT, not MONITORX): the
# encoding is the SDM / APM opcode line, Capstone's name is wrong for it
CAPSTONE_PREFIX_IGNORED = {('clui', 'rdpkru'), ('stui', 'wrpkru'), ('seamcall', 'encls'), ('mcommit', 'monitorx'),
                           ('erets', 'clac'), ('eretu', 'clac')}


def make_checker():
    try:
        import capstone
    except ImportError:
        return None
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    def check(mn, b):
        d = list(cs.disasm(bytes(b), 0))
        if not d:
            return 'capstone-unknown', ''
        got = d[0].mnemonic.split()[-1]
        txt = d[0].mnemonic + ' ' + d[0].op_str
        if d[0].size != len(b):
            return 'LENGTH', txt
        want = mn.split()[-1]
        if got == want:
            return 'ok', txt
        # base rows encoded with predicate imm8 0: Capstone prints the pseudo-op (VCMPEQPS, VPCOMLTB, VPCMPEQB)
        if (re.sub(r'^vcmp(%s)' % '|'.join(VCMP_PRED), 'vcmp', got) == want or
                re.sub(r'^vpcom(%s)' % '|'.join(VPCOM_PRED), 'vpcom', got) == want or
                re.sub(r'^vpcmp(eq|lt|le|false|neq|nlt|nle|true)', 'vpcmp', got) == want):
            return 'ok', txt
        if (want, got) in CAPSTONE_PREFIX_IGNORED:
            return 'capstone-ignores-mandatory-prefix', txt
        return 'MISMATCH', txt
    return check


# ------------------------------------------------------------------------------------------------ generation
def cpuid_line(leaf, sub, reg, bit):
    return ('mov eax, 0x%x; mov ecx, %d; cpuid; mov r8, %s; shr r8, %d; and r8, 1' % (leaf, sub, GPR[reg], bit))


def byte_line(b):
    return '.byte ' + ', '.join('0x%02x' % x for x in b)


def main():
    mode = '--check'
    for a in sys.argv[1:]:
        if a in ('--write', '--check'):
            mode = a
        else:
            print('usage: gen_gating_cases.py [--write | --check]')
            return 2
    print('[gating] data dir: %s' % DATA)
    print('[gating] mode: %s' % mode)
    prof = OrderedDict((k, load_cpuid(os.path.join(DATA, v))) for k, v in PROFILES.items())
    for k, v in PROFILES.items():
        print('[gating] profile %-6s %s: %d CPUID rows' % (k, v, len(prof[k])))
    bad = 0
    # every cited bit must be 0 on all three profiles (absent rows read as 0: a missing leaf / subleaf row)
    for g in GROUPS:
        for leaf, sub, reg, bit, name in g['cpuid']:
            vals = []
            for k in PROFILES:
                r = prof[k].get((leaf, sub), [0, 0, 0, 0])
                vals.append('%s=%d' % (k, (r[REGS.index(reg)] >> bit) & 1))
                if (r[REGS.index(reg)] >> bit) & 1:
                    bad += 1
            print('[gating] %-7s CPUID.(%XH,%d):%s[%d] %-36s %s' % (g['id'], leaf, sub, reg, bit, name, ' '.join(vals)))
    if bad:
        print('[gating] ERROR: %d cited bits are set in a profile (the feature is not absent)' % bad)
        return 1

    rows = load_rows()
    evf = load_evex_forms()
    kop = kop_forms()
    amx = amx_forms()
    apx = apx_harvest()
    print('[gating] isa_manual_forms rows: %d, evex_forms mnemonics: %d, APX forms harvested: %d' % (
        len(rows), len(evf), len(apx)))
    check = make_checker()
    print('[gating] capstone decode check: %s' % ('on' if check else 'capstone not importable, skipped'))
    verdicts = defaultdict(int)
    mism = []
    unknown = defaultdict(set)

    per_group = OrderedDict((g['id'], []) for g in GROUPS)      # (mn, enc, isas, [(desc, bytes)], source)
    uncovered = []
    for (mn, enc), isas in rows.items():
        gid = group_of(mn, enc, isas)
        if gid is None:
            continue
        enc_list, src = [], ''
        if gid == 'APX':
            if (mn, enc) in apx:
                b, where = apx[(mn, enc)]
                enc_list, src = [(mn, b)], 'APX case file ' + where
            elif (mn, enc) == ('wrmsrns', 'evex'):
                # MSR-IMM / APX: EVEX.128.F3.MAP7.W0 F6 /0 id (EVV 0xF6 VF3 MAP7 MOD=3 REG=0, UIMM32)
                enc_list, src = [('wrmsrns rax, imm32 0 (EVEX.MAP7.F3 F6 /0 id)',
                                  [0x62, 0xF7, 0x7E, 0x08, 0xF6, 0xC0, 0, 0, 0, 0])], 'MSR-IMM opcode line'
        elif enc == 'evex':
            m = VCMP_RE.fullmatch(mn)
            if mn in evf:
                enc_list, src = evex_lines(evf[mn]), 'evex_forms.tsv'
            elif m and ('vcmp' + m.group(2)) in evf:
                enc_list = evex_lines(evf['vcmp' + m.group(2)], imm=VCMP_PRED.index(m.group(1)), label=mn)
                src = 'evex_forms.tsv %s, predicate imm8 %02XH' % ('VCMP' + m.group(2).upper(), VCMP_PRED.index(m.group(1)))
        elif mn in kop and enc == 'vex':
            enc_list, src = kop[mn], 'opmask VEX opcode lines'
        elif mn in amx and enc == 'vex':
            enc_list, src = amx[mn], 'AMX / ACE VEX opcode lines'
        elif mn in HAND:
            enc_list, src = [(d, hx(h)) for d, h in HAND[mn]], 'opcode lines (HAND)'
        if not enc_list:
            uncovered.append((gid, mn, enc, '+'.join(isas)))
            continue
        per_group[gid].append((mn, enc, isas, enc_list, src))
        if check:
            for d, b in enc_list:
                v, txt = check(mn, b)
                verdicts[v] += 1
                if v == 'capstone-unknown':
                    unknown[gid].add(mn)
                elif v == 'capstone-ignores-mandatory-prefix':
                    print('[gating] capstone note %s %s: %s -> %s (Capstone 5 ignores the mandatory prefix)' % (
                        mn, enc, byte_line(b), txt))
                elif v != 'ok':
                    mism.append('%s %s: %s -> %s (%s)' % (mn, enc, byte_line(b), txt, v))

    for gid, lst in per_group.items():
        print('[gating] group %-7s %4d table rows, %5d case lines' % (gid, len(lst), sum(len(x[3]) for x in lst)))
    if check:
        print('[gating] capstone: %s' % ', '.join('%s %d' % kv for kv in sorted(verdicts.items())))
        for m in mism:
            print('[gating] capstone %s' % m)
        for gid in per_group:
            if unknown[gid]:
                print('[gating] capstone does not decode (%s, %d mnemonics): %s' % (
                    gid, len(unknown[gid]), ' '.join(sorted(unknown[gid]))))
    for u in uncovered:
        print('[gating] UNCOVERED %s %s %s %s' % u)
    if uncovered or mism:
        print('[gating] ERROR: %d table rows without an encoding, %d encodings Capstone decodes as another form' % (
            len(uncovered), len(mism)))
        return 1

    files = OrderedDict()
    files['cases_gating.txt'] = gen_expect(per_group)
    files['cases_gating_host.txt'] = gen_cet('host')
    files['cases_gating_guest.txt'] = gen_cet('guest')
    files['cases_gating_hw.txt'] = gen_hw(per_group)
    rc = 0
    for fn, lines in files.items():
        text = '\r\n'.join(lines) + '\r\n'
        p = os.path.join(DATA, fn)
        ncase = sum(1 for l in lines if l and not l.startswith('#'))
        if mode == '--write':
            with open(p, 'w', encoding='ascii', newline='') as fh:
                fh.write(text)
            print('[gating] wrote %s: %d lines, %d cases' % (fn, len(lines), ncase))
        else:
            try:
                old = open(p, encoding='ascii', newline='').read().replace('\r\n', '\n')
            except OSError:
                old = None
            same = old == text.replace('\r\n', '\n')
            print('[gating] check %s: %s (%d lines, %d cases)' % (fn, 'identical' if same else 'DIFFERS', len(lines), ncase))
            if not same:
                rc = 1
    print('[gating] %s' % ('OK' if rc == 0 else 'FAILED: run gen_gating_cases.py --write and review the diff'))
    return rc


HEADER = [
    '# GATING ONLY features (reduced Gate 1 checklist item 4; your decision 2026-10-17 (G10), notes\\eread\\G10_rescope_pe_only.md',
    '# section 1; ledger U1630-U1632, notes\\eread\\G13_gating_only.md). GENERATED by Emulator\\tools\\isa\\gen_gating_cases.py',
    '# --write - do not edit by hand (suite gating_cases_check runs --check). Expected-value cases for EVERY profile: the',
    '# features below are absent on the bare i5-13600K host and in the VMware guest (captures Host_clean_20261016 /',
    '# Run3_clean_guest_probes_20261015 = the three CPUID profiles), so the same lines hold for all of them:',
    '#   emu-alltest --cases Emulator\\data\\cases_gating.txt --host --expect-only                (suite gating_host_p)',
    '#   emu-alltest --cases Emulator\\data\\cases_gating.txt --host --host-core E --expect-only  (suite gating_host_e)',
    '#   emu-alltest --cases Emulator\\data\\cases_gating.txt --guest --expect-only               (suite gating_guest; --vm: gating_vm)',
    '# Per group: one line per cited CPUID bit (bit -> r8, expected 0), the absent XSAVE components / MSRs / CR4 bit where',
    '# the item names them, then one line per instruction form (every (mnemonic, encoding) row of isa_manual_forms.tsv of',
    '# the group; EVEX rows: every evex_forms.tsv opcode line, each VL, register and memory form) -> #UD with nothing',
    '# changed ("=> #UD", strict). MPX: the BND* forms are NOPs without MPX ("=>": no fault, nothing changed).',
    '# Raw Unicorn runs these at CPL0, so CR4, RDMSR / WRMSR and XSETBV are reachable; only the FRED group ERETS /',
    '# ERETU lines run at CPL3 (cpl=3; G13 section 3).',
]


def gen_expect(per_group):
    out = list(HEADER)
    for g in GROUPS:
        lst = per_group[g['id']]
        out.append('#')
        out.append('# ===== %s: %s' % (g['id'], g['title']))
        out.append('# plan lines: %s' % '; '.join(g['plan']))
        out.append('# %d instruction-table rows, %d form lines' % (len(lst), sum(len(x[3]) for x in lst)))
        for leaf, sub, reg, bit, name in g['cpuid']:
            out.append('# --- CPUID.(%XH,%d):%s[%d] %s = 0' % (leaf, sub, reg, bit, name))
            out.append(cpuid_line(leaf, sub, reg, bit) + ' =>! r8=0')
        for c, line in g['extra']:
            out.append('# --- ' + c)
            out.append(line)
        exp = '=>' if g['id'] == 'MPX' else '=> #UD'
        for mn, enc, isas, enc_list, src in lst:
            out.append('# %s %s [%s] (%s)' % (mn.upper(), enc, '+'.join(isas), src))
            cpl = ' | cpl=3' if mn in g.get('cpl3', ()) else ''
            if cpl:
                out.append('#   at CPL3 only (G13 section 3: CPL0 executes CLAC without FRED, decision pending)')
            for d, b in enc_list:
                if d != mn:
                    out.append('#   ' + d)                # (a case line takes no trailing comment)
                out.append('%s%s %s' % (byte_line(b), cpl, exp))
    return out


# 1.J.37.c: CET_SS + CET_U / CET_S on both profiles (IBT alone never occurs on a real profile); IBT host 1, guest 0
CET = [
    ((7, 0, 'ECX', 7, 'CET_SS'), {'host': 1, 'guest': 1}),
    ((7, 0, 'EDX', 20, 'CET_IBT'), {'host': 1, 'guest': 0}),
    ((0xD, 1, 'ECX', 11, 'IA32_XSS CET_U supported'), {'host': 1, 'guest': 1}),
    ((0xD, 1, 'ECX', 12, 'IA32_XSS CET_S supported'), {'host': 1, 'guest': 1}),
    ((0xD, 0xB, 'ECX', 0, 'component 11 (CET_U) is supervisor state (IA32_XSS)'), {'host': 1, 'guest': 1}),
    ((0xD, 0xC, 'ECX', 0, 'component 12 (CET_S) is supervisor state (IA32_XSS)'), {'host': 1, 'guest': 1}),
]


def gen_cet(p):
    pf = {'host': 'cpuid_i5-13600k_bare_host.txt (and _ecore.txt)', 'guest': 'cpuid_i5-13600k.txt'}[p]
    sw, suites = {'host': ('--host', 'gating_cet_host_p; --host --host-core E: gating_cet_host_e'),
                  'guest': ('--guest', 'gating_cet_guest; --vm: gating_cet_vm')}[p]
    out = ['# 1.J.37.c GATING ONLY (CET_U / CET_S with IBT alone): "CPUID verified" on the %s profile %s.' % (
        {'host': 'bare-host', 'guest': 'VMware-guest'}[p], pf),
        '# GENERATED by Emulator\\tools\\isa\\gen_gating_cases.py --write (ledger U1630; notes\\eread\\G13_gating_only.md).',
        '# Both real profiles enumerate CET_SS with the CET_U / CET_S XSAVE components (IA32_XSS bits 11 / 12), so IBT alone',
        '# (CET_U / CET_S offered with IBT but without shadow stacks) only occurs on a synthetic profile.',
        '#   emu-alltest --cases Emulator\\data\\cases_gating_%s.txt %s --expect-only   (suite %s)' % (p, sw, suites)]
    for (leaf, sub, reg, bit, name), v in CET:
        out.append('# --- CPUID.(%XH,%XH):%s[%d] %s = %d' % (leaf, sub, reg, bit, name, v[p]))
        out.append(cpuid_line(leaf, sub, reg, bit) + ' =>! r8=%d' % v[p])
    out.append('# --- CPUID.(0DH,0BH):EAX = 10H (CET_U size), CPUID.(0DH,0CH):EAX = 18H (CET_S size)')
    out.append('mov eax, 0xd; mov ecx, 0xb; cpuid; mov r8, rax =>! r8=0x10')
    out.append('mov eax, 0xd; mov ecx, 0xc; cpuid; mov r8, rax =>! r8=0x18')
    out.append('# --- WRMSR IA32_XSS (DA0H) = 1800H (CET_U | CET_S) is accepted and reads back')
    out.append('mov ecx, 0xda0; mov eax, 0x1800; xor edx, edx; wrmsr; rdmsr =>! rax=0x1800 rdx=0')
    return out


def gen_hw(per_group):
    out = ['# GATING ONLY features as HARDWARE cases (ledger U1630; notes\\eread\\G13_gating_only.md). GENERATED by',
           '# Emulator\\tools\\isa\\gen_gating_cases.py --write - do not edit by hand. This machine is the VMware guest: the host',
           '# CPU (CPL3 under Windows) and Unicorn (cpl=3, the Windows GDT, the gate profile cpuid_i5-13600k.txt) run the same',
           '# self-written snippets; test.cmd requires differing: 0 (suite cases_gating_hw):',
           '#   emu-alltest --cases Emulator\\data\\cases_gating_hw.txt --cpuid Emulator\\data\\cpuid_i5-13600k.txt',
           '# CPUID lines: the cited bit -> r8, EAX..EDX cleared (only the bit is compared). Then the first encoding of every',
           '# instruction-table row of cases_gating.txt: #UD on both (the MPX BND* forms: NOP on both). The bare host is',
           '# covered by the expected-value suites (gating_host_p / _host_e) and the host package; no new package.']
    for g in GROUPS:
        lst = per_group[g['id']]
        if not g['cpuid'] and not lst:
            continue
        out.append('#')
        out.append('# ===== %s: %s' % (g['id'], g['title']))
        for leaf, sub, reg, bit, name in g['cpuid']:
            out.append('# --- CPUID.(%XH,%d):%s[%d] %s' % (leaf, sub, reg, bit, name))
            out.append(cpuid_line(leaf, sub, reg, bit) + '; xor eax, eax; xor ebx, ebx; xor ecx, ecx; xor edx, edx | cpl=3')
        for mn, enc, isas, enc_list, src in lst:
            d, b = enc_list[0]
            out.append('# %s %s%s' % (mn.upper(), enc, '' if d == mn else ': ' + d))
            out.append('%s | cpl=3' % byte_line(b))    # (a trailing '#' would be read as a tag)
    return out


if __name__ == '__main__':
    sys.exit(main())
