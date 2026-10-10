r"""Independent reference model + expected-value case generator (ledger U900-U929, agent "sysmsr"):

  * SYSCALL (U901), SYSENTER (U902), SYSRET (U903), SYSEXIT (U904): the SDM transitions, MAX model,
    64-bit mode, CPL0 / CPL3 snippets                                -> Emulator\data\cases_sysmsr.txt

Written from the Intel manuals only (the emulator's C sources were not read for the expected values;
no CPU measurements - SYSCALL / SYSENTER would enter the Windows kernel natively and SYSRET / SYSEXIT /
WRMSR are CPL0 instructions, so the i5-13600K can only show #GP at CPL3):
  * SDM Vol2B 325462-092 SYSCALL (Operation, CR4.FRED = 0: "IF IA32_EFER.SCE = 0 THEN #UD; RCX :=
    RIP; RIP := IA32_LSTAR; R11 := RFLAGS; RFLAGS := RFLAGS AND NOT(IA32_FMASK); CS.Selector :=
    IA32_STAR[47:32] AND FFFCH ... CS.L := 1 ... IF ShadowStackEnabled(CPL) IA32_PL3_SSP :=
    LA_adjust(SSP); CPL := 0; IF ShadowStackEnabled(CPL) SSP := 0; IF EndbranchEnabled(CPL)
    IA32_S_CET.TRACKER = WAIT_FOR_ENDBRANCH, IA32_S_CET.SUPPRESS = 0; SS.Selector := IA32_STAR[47:32]
    + 8"; 64-Bit Mode Exceptions: #UD if IA32_EFER.SCE = 0).
  * SDM Vol3A 6.8.8 (SYSCALL saves RFLAGS into R11 and the next RIP into RCX; RFLAGS AND NOT
    IA32_FMASK), Figure 6-14 (IA32_STAR 47:32 SYSCALL CS/SS, 63:48 SYSRET CS/SS).
  * SDM Vol2B POPF (64-bit mode, CPL 0: every non-reserved flag but RF, VIP, VIF and VM is loaded; RF
    is cleared), PUSHF ("VM and RF ... cleared in the image"), MOV r32, Sreg (selector zero-extended).
  * SDM Vol2B SYSRET (the round trip of the CPL3 cases: "RFLAGS := (R11 & 3C7FD7H) | 2; CS.Selector
    := IA32_STAR[63:48] + 16 OR 3 (64-bit operand size); SS.Selector := (IA32_STAR[63:48] + 8) OR 3;
    IF ShadowStackEnabled(CPL) SSP := IA32_PL3_SSP").
  * SDM Vol1 18.2.2 / 18.3.2 (ShadowStackEnabled / EndbranchEnabled: CR4.CET, IA32_U_CET (CPL 3) /
    IA32_S_CET (CPL < 3) SH_STK_EN[0] / ENDBR_EN[2]), 18.3 (ENDBR64 = F3 0F 1E FA makes the tracker
    IDLE; any other instruction in WAIT_FOR_ENDBRANCH is #CP(ENDBRANCH), error code 3, at that
    instruction), Vol2A SETSSBSY (SSP := IA32_PL0_SSP after the token at it is marked busy), RDSSPQ
    (a NOP unless shadow stacks are enabled at the current CPL), Vol4 IA32_S_CET (6A2H),
    IA32_PL0_SSP (6A4H), IA32_PL3_SSP (6A7H), IA32_U_CET (6A0H).

Modelling decisions:
  a. The harness runs the snippet in 64-bit mode at CPL0 without a GDT; SYSCALL / SYSRET load fixed
     descriptor caches, so no descriptor table is needed. The code address is not part of the
     model: the handler address (IA32_LSTAR) is a label, and RCX is checked as RCX - label = 0.
  b. RFLAGS inputs are loaded with POPFQ, which cannot set TF here (a single-step trap would follow;
     not modelled), RF, VIP, VIF or VM; the RFLAGS image the snippet observes is PUSHFQ's (RF and VM
     clear). R11 is the RFLAGS image as the SDM prints it ("R11 := RFLAGS").
  c. IA32_FMASK bits 63:32 are reserved (Figure 6-14): only 32-bit masks are written.
  d. The CET cases use the shadow-stack token at IA32_PL0_SSP = MEM + 9FF8h (the case memory) for
     SETSSBSY and an SSP below 2^47, so LA_adjust is the identity (its sign extension is covered by
     the unit test test_x86_sm_syscall_cet).
  e. #CP(ENDBRANCH) is reported at the handler's first instruction; the snippet stops there.

Usage:
  python -I ref_sysmsr.py --selftest   hand-derived checks of the model (exit 0 on pass)
  python -I ref_sysmsr.py --write      regenerate the case files listed above (CRLF)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, '..', '..', 'data'))

M64 = (1 << 64) - 1
MEM = 0x30020000            # the harness's case memory (m+OFF), MEM + 8000h = RSI, MEM + 9000h = RDI
LA_BITS = 57                # MAX model: CPUID.(07H,0):ECX.LA57 = 1 -> CPU linear-address width 57

# RFLAGS bits
CF, PF, AF, ZF, SF, TF, IF, DF, OF = 1, 4, 0x10, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800
IOPL, NT, RF, VM, AC, VIF, VIP, ID = 0x3000, 0x4000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000
FLAGS_DEFINED = CF | PF | AF | ZF | SF | TF | IF | DF | OF | IOPL | NT | RF | VM | AC | VIF | VIP | ID

MSR_EFER, MSR_STAR, MSR_LSTAR, MSR_FMASK = 0xC0000080, 0xC0000081, 0xC0000082, 0xC0000084
MSR_U_CET, MSR_S_CET, MSR_PL0_SSP, MSR_PL3_SSP = 0x6A0, 0x6A2, 0x6A4, 0x6A7
CET_SH_STK_EN, CET_ENDBR_EN, CET_TRACKER = 1, 4, 1 << 11

ENDBR64 = '.byte 0xf3, 0x0f, 0x1e, 0xfa'
SYSRETQ = '.byte 0x48, 0x0f, 0x07'
SETSSBSY = '.byte 0xf3, 0x0f, 0x01, 0xe8'
RDSSPQ_RBP = '.byte 0xf3, 0x48, 0x0f, 0x1e, 0xcd'


def hx(v):
    return '0x%X' % v


class Fault(Exception):
    def __init__(self, name, code=None):
        Exception.__init__(self, name)
        self.name = name
        self.code = code

    def token(self):
        return self.name if self.code is None else '%s(%d)' % (self.name, self.code)


def la_adjust(v, bits=LA_BITS):
    """bits 63:N := bit N-1 (SYSCALL / SYSENTER IA32_PL3_SSP)"""
    v &= M64
    if v & (1 << (bits - 1)):
        return v | (M64 & ~((1 << bits) - 1))
    return v & ((1 << bits) - 1)


def popfq_image(v):
    """RFLAGS after POPFQ at CPL0 in 64-bit mode from a reset RFLAGS (VIP/VIF/VM 0): RF cleared"""
    return (v & (CF | PF | AF | ZF | SF | TF | IF | DF | OF | IOPL | NT | AC | ID)) | 2


def pushfq_image(fl):
    return (fl & ~(RF | VM)) | 2


# ---- SDM Vol2B SYSCALL (64-bit mode, CR4.FRED = 0) ---------------------------------------------
def syscall(st):
    """st: dict with rip_next, rflags, efer, star, lstar, fmask, cpl, ssp, msr (dict), cr4_cet.
    Returns the new state (a copy) or raises Fault."""
    if not (st['efer'] & 1):
        raise Fault('#UD')
    n = dict(st)
    n['msr'] = dict(st['msr'])
    n['rcx'] = st['rip_next']
    n['rip'] = st['lstar']
    n['r11'] = st['rflags']
    n['rflags'] = (st['rflags'] & ~(st['fmask'] & 0xFFFFFFFF) & FLAGS_DEFINED) | 2
    sel = (st['star'] >> 32) & 0xFFFF
    n['cs'] = sel & 0xFFFC
    if ss_enabled(st, st['cpl']):
        n['msr'][MSR_PL3_SSP] = la_adjust(st['ssp'])
    n['cpl'] = 0
    if ss_enabled(n, 0):
        n['ssp'] = 0
    if ibt_enabled(n, 0):
        n['msr'][MSR_S_CET] = (n['msr'].get(MSR_S_CET, 0) & ~(1 << 10)) | CET_TRACKER
    n['ss'] = (sel + 8) & 0xFFFF
    return n


def ss_enabled(st, cpl):
    m = st['msr'].get(MSR_U_CET if cpl == 3 else MSR_S_CET, 0)
    return bool(st.get('cr4_cet')) and bool(m & CET_SH_STK_EN)


def ibt_enabled(st, cpl):
    m = st['msr'].get(MSR_U_CET if cpl == 3 else MSR_S_CET, 0)
    return bool(st.get('cr4_cet')) and bool(m & CET_ENDBR_EN)


def base_state(**kw):
    st = dict(rip_next=0, rflags=0x202, efer=0xD01, star=0, lstar=0, fmask=0, cpl=0, ssp=0,
              msr={}, cr4_cet=False)
    st.update(kw)
    return st


# ---- snippet pieces (Keystone syntax) --------------------------------------------------------------
def wrmsr_imm(msr, val):
    return 'mov ecx, %s; mov eax, %s; mov edx, %s; wrmsr' % (hx(msr), hx(val & 0xFFFFFFFF), hx(val >> 32))


def wrmsr_label(msr, label):
    return 'mov ecx, %s; lea rax, [rip + %s]; mov rdx, rax; shr rdx, 32; wrmsr' % (hx(msr), label)


def rdmsr_to(msr, reg):
    """RDMSR into reg (64-bit: EDX:EAX combined)"""
    return 'mov ecx, %s; rdmsr; shl rdx, 32; or rax, rdx; mov %s, rax' % (hx(msr), reg)


CET_ON = 'mov rax, cr0; bts rax, 16; mov cr0, rax; mov rax, cr4; bts rax, 23; mov cr4, rax'


# ---- cases -----------------------------------------------------------------------------------------
def cases_syscall(a):
    a('# --- SYSCALL (U901): the SDM transition. IA32_LSTAR = label k (the handler); after it: R15 = PUSHFQ')
    a('# image, R12/R13 = CS/SS selectors, RCX - k = 0, R11 = the RFLAGS image at SYSCALL (POPFQ input)')
    combos = [
        # (STAR[63:32], FMASK, RFLAGS input)
        (0x00230010, 0x00047700, 0x00000ED7),
        (0x00230010, 0x00004700, 0x00240ED7),
        (0x00230010, 0x00000000, 0x00243AD7),
        (0x00230010, 0xFFFFFFFF, 0x00247ED7),
        (0x00230010, 0x000008D5, 0x000008D7),
        (0x00230013, 0x00040701, 0x00240ED7),   # SYSCALL CS base with RPL 3: CS 10h, SS 1Bh
        (0x00000000, 0x00000000, 0x00000202),   # CS 0, SS 8
        (0xFFFFFFFB, 0x00000200, 0x00000246),   # CS FFF8h, SS (FFFBh + 8) AND FFFFh = 3
        (0x12345678, 0x00200000, 0x00200002),
    ]
    for star_hi, fmask, fl_in in combos:
        star = star_hi << 32
        img = popfq_image(fl_in)
        st = base_state(rflags=img, star=star, fmask=fmask, lstar='k', rip_next='k')
        n = syscall(st)
        asm = '; '.join([
            wrmsr_imm(MSR_STAR, star), wrmsr_imm(MSR_FMASK, fmask), wrmsr_label(MSR_LSTAR, 'k'),
            'mov rax, %s; push rax; popfq' % hx(fl_in), 'syscall',
            'k: pushfq; pop r15; mov r12d, cs; mov r13d, ss; lea r14, [rip + k]; sub rcx, r14'])
        a('%s =>! rcx=0 r11=%s r15=%s r12=%s r13=%s' % (asm, hx(n['r11']), hx(pushfq_image(n['rflags'])),
                                                         hx(n['cs']), hx(n['ss'])))
    a('# IA32_EFER.SCE = 0: #UD, nothing changes (RCX keeps the MSR number, R11 its input)')
    try:
        syscall(base_state(efer=0xD00))
        raise AssertionError('SCE = 0 must #UD')
    except Fault as f:
        a('mov ecx, 0xC0000080; rdmsr; and eax, 0xFFFFFFFE; wrmsr; syscall | r11=0x5555 =>! %s rcx=0xC0000080 '
          'r11=0x5555' % f.token())
    a('# CPL3 -> CPL0: SYSRETQ (IA32_STAR[63:48] = 23h: CS 33h, SS 2Bh) to label u at CPL3, SYSCALL there')
    a('# (IA32_STAR[47:32] = 10h); RBX = CS at CPL3, R12/R13 = CS/SS at the handler, R11 = RFLAGS at CPL3')
    st = base_state(cpl=3, rflags=popfq_image(0x8D7), star=0x0023001000000000, lstar='k', rip_next='k')
    n = syscall(st)
    asm = '; '.join([
        wrmsr_imm(MSR_STAR, 0x0023001000000000), wrmsr_label(MSR_LSTAR, 'k'),
        'lea rcx, [rip + u]; mov r11d, 0x8D7', SYSRETQ,
        'u: mov ebx, cs; syscall',
        'k: pushfq; pop r15; mov r12d, cs; mov r13d, ss; lea r14, [rip + k]; sub rcx, r14'])
    a('%s =>! rbx=0x33 rcx=0 r11=%s r15=%s r12=%s r13=%s' % (asm, hx(n['r11']), hx(pushfq_image(n['rflags'])),
                                                             hx(n['cs']), hx(n['ss'])))

    a('# --- SYSCALL CET (U901), CR4.CET = 1 (CR0.WP = 1). Supervisor shadow stack: IA32_PL0_SSP = T = MEM + 9FF8h,')
    a('# token at T, SETSSBSY (SSP := T); IA32_PL3_SSP = P = 7FFF12345670h before SYSCALL; after it RBP = RDSSPQ')
    a('# (input 5555h: a NOP when shadow stacks are off at CPL 0), R8 = IA32_PL3_SSP, R9 = IA32_S_CET')
    T = MEM + 0x9FF8
    P = 0x7FFF12345670
    for s_cet in (CET_SH_STK_EN, CET_SH_STK_EN | CET_ENDBR_EN, 0, CET_ENDBR_EN):
        msr = {MSR_S_CET: s_cet, MSR_PL0_SSP: T, MSR_PL3_SSP: P}
        ssp = T if s_cet & CET_SH_STK_EN else 0
        st = base_state(cr4_cet=True, msr=msr, ssp=ssp, lstar='k', rip_next='k')
        n = syscall(st)
        setss = [SETSSBSY] if s_cet & CET_SH_STK_EN else []
        endbr = s_cet & CET_ENDBR_EN
        asm = '; '.join([CET_ON, wrmsr_imm(MSR_S_CET, s_cet), wrmsr_imm(MSR_PL0_SSP, T), wrmsr_imm(MSR_PL3_SSP, P)]
                        + setss + [wrmsr_label(MSR_LSTAR, 'k'), 'syscall',
                                   'k: ' + ENDBR64, RDSSPQ_RBP, rdmsr_to(MSR_PL3_SSP, 'r8'),
                                   rdmsr_to(MSR_S_CET, 'r9')])
        tok = T | 1 if s_cet & CET_SH_STK_EN else T
        rbp = n['ssp'] if ss_enabled(n, 0) else 0x5555
        # ENDBR64 makes the tracker IDLE again (SUPPRESS 0)
        s_after = n['msr'][MSR_S_CET] & ~CET_TRACKER
        a('%s | rbp=0x5555 m+0x9FF8=%s =>! rbp=%s r8=%s r9=%s m+0x9FF8=%s' % (
            asm, le64(T), hx(rbp), hx(n['msr'][MSR_PL3_SSP]), hx(s_after), le64(tok)))
        if endbr:
            # the same without ENDBR64 at the handler: #CP(ENDBRANCH) there
            asm2 = '; '.join([CET_ON, wrmsr_imm(MSR_S_CET, s_cet), wrmsr_label(MSR_LSTAR, 'k'), 'syscall',
                              'k: nop'])
            a('%s =>! #CP(3)' % asm2)
    a('# CPL3 -> CPL0 with IA32_U_CET = SH_STK_EN | ENDBR_EN only: SYSRETQ loads SSP := IA32_PL3_SSP = P; SYSCALL')
    a('# saves IA32_PL3_SSP := LA_adjust(SSP) = P; no shadow stack and no tracker at CPL 0 (no ENDBR64 needed)')
    msr = {MSR_U_CET: CET_SH_STK_EN | CET_ENDBR_EN, MSR_PL3_SSP: P}
    st = base_state(cpl=3, cr4_cet=True, msr=msr, ssp=P, star=0x0023001000000000, lstar='k', rip_next='k')
    n = syscall(st)
    asm = '; '.join([CET_ON, wrmsr_imm(MSR_U_CET, CET_SH_STK_EN | CET_ENDBR_EN), wrmsr_imm(MSR_PL3_SSP, P),
                     wrmsr_imm(MSR_STAR, 0x0023001000000000), wrmsr_label(MSR_LSTAR, 'k'),
                     'lea rcx, [rip + u]; mov r11d, 0x202', SYSRETQ,
                     'u: ' + ENDBR64, 'syscall', 'k: nop', rdmsr_to(MSR_PL3_SSP, 'r8'),
                     rdmsr_to(MSR_U_CET, 'r9')])
    a('%s =>! r8=%s r9=%s' % (asm, hx(n['msr'][MSR_PL3_SSP]), hx(n['msr'][MSR_U_CET])))


# ---- SDM Vol2B SYSENTER (CR4.FRED = 0) ----------------------------------------------------------
def sysenter(st):
    """st: dict with sysenter_cs/esp/eip, rflags, cpl, lma, ssp, msr, cr4_cet, pe"""
    if not st.get('pe', True) or ((st['sysenter_cs'] >> 2) & 0x3FFF) == 0:
        raise Fault('#GP', 0)
    n = dict(st)
    n['msr'] = dict(st['msr'])
    n['rflags'] = st['rflags'] & ~(VM | IF | RF)
    if st['lma']:
        n['rsp'], n['rip'] = st['sysenter_esp'], st['sysenter_eip']
    else:
        n['rsp'], n['rip'] = st['sysenter_esp'] & 0xFFFFFFFF, st['sysenter_eip'] & 0xFFFFFFFF
    n['cs'] = st['sysenter_cs'] & 0xFFFC
    n['cs_l'] = 1 if st['lma'] else 0
    if ss_enabled(n, st['cpl']):          # RFLAGS.VM is already 0 here (pseudocode order)
        n['msr'][MSR_PL3_SSP] = la_adjust(st['ssp']) if st['lma'] else st['ssp']
    n['cpl'] = 0
    if ss_enabled(n, 0):
        n['ssp'] = 0
    if ibt_enabled(n, 0):
        n['msr'][MSR_S_CET] = (n['msr'].get(MSR_S_CET, 0) & ~(1 << 10)) | CET_TRACKER
    n['ss'] = (n['cs'] + 8) & 0xFFFF
    return n


def sysenter_state(**kw):
    st = dict(sysenter_cs=0x10, sysenter_esp=0, sysenter_eip=0, rflags=0x202, cpl=0, lma=True, ssp=0,
              msr={}, cr4_cet=False, pe=True)
    st.update(kw)
    return st


SYSEXIT32 = '.byte 0x0f, 0x35'


def cases_sysenter(a):
    a('# --- SYSENTER (U902): the SDM transition (64-bit mode). IA32_SYSENTER_EIP = label k, IA32_SYSENTER_ESP =')
    a('# RSP - 100h (RBP = the old RSP); after it R15 = PUSHFQ image, R12/R13 = CS/SS, R14 = RBP - RSP = 100h')
    for cs, fl_in in ((0x10, 0x00000ED7), (0x13, 0x00243AD7), (0x08, 0x00000202), (0xFFFF, 0x00040246),
                      (0x23, 0x00200ED6)):
        n = sysenter(sysenter_state(sysenter_cs=cs, rflags=popfq_image(fl_in)))
        asm = '; '.join([
            wrmsr_imm(0x174, cs), wrmsr_label(0x176, 'k'),
            'mov rbp, rsp; lea rax, [rsp - 0x100]; mov rdx, rax; shr rdx, 32; mov ecx, 0x175; wrmsr',
            'mov rax, %s; push rax; popfq' % hx(fl_in), 'sysenter',
            'k: pushfq; pop r15; mov r12d, cs; mov r13d, ss; mov r14, rbp; sub r14, rsp'])
        a('%s =>! r15=%s r12=%s r13=%s r14=0x100' % (asm, hx(pushfq_image(n['rflags'])), hx(n['cs']), hx(n['ss'])))
    a('# IA32_SYSENTER_ESP/EIP bits 63:32 are used in IA-32e mode: RSP = FFFF8000_12345678h (no stack use after it)')
    n = sysenter(sysenter_state(sysenter_esp=0xFFFF800012345678, sysenter_eip='k'))
    asm = '; '.join([wrmsr_imm(0x175, 0xFFFF800012345678), wrmsr_label(0x176, 'k'), 'mov rbp, rsp', 'sysenter',
                     'k: mov rbx, rsp; mov rsp, rbp'])
    a('%s =>! rbx=%s' % (asm, hx(n['rsp'])))
    a('# CPL3 (compatibility mode, entered with SYSEXIT, IA32_SYSENTER_CS = 10h: CS 23h, SS 2Bh) -> 64-bit mode CPL0:')
    a('# RBX = CS in compatibility mode, R12/R13 = CS/SS after SYSENTER, R14 = RSP - (RBP - 100h) = 0')
    n = sysenter(sysenter_state(sysenter_cs=0x10, cpl=3))
    asm = '; '.join([
        wrmsr_imm(0x174, 0x10), wrmsr_label(0x176, 'k'),
        'mov rbp, rsp; lea rax, [rsp - 0x100]; mov rdx, rax; shr rdx, 32; mov ecx, 0x175; wrmsr',
        'lea rdx, [rip + u]; mov rcx, rsp', SYSEXIT32,
        'u: mov ebx, cs; sysenter',
        'k: mov r12d, cs; mov r13d, ss; lea r14, [rbp - 0x100]; sub r14, rsp; neg r14'])
    a('%s =>! rbx=0x23 r12=%s r13=%s r14=0' % (asm, hx(n['cs']), hx(n['ss'])))
    a('# the same from 64-bit mode CPL3 (SYSEXIT REX.W: CS 33h)')
    asm = asm.replace(SYSEXIT32, '.byte 0x48, 0x0f, 0x35')
    a('%s =>! rbx=0x33 r12=%s r13=%s r14=0' % (asm, hx(n['cs']), hx(n['ss'])))
    a('# IA32_SYSENTER_CS[15:2] = 0: #GP(0), nothing changes')
    for cs in (0, 3, 0x10000):
        try:
            sysenter(sysenter_state(sysenter_cs=cs & 0xFFFF))
            raise AssertionError('sysenter must fault')
        except Fault as f:
            a('%s; sysenter =>! %s rcx=0x174 rax=%s rdx=%s' % (wrmsr_imm(0x174, cs), f.token(), hx(cs & 0xFFFFFFFF),
                                                             hx(cs >> 32)))

    a('# --- SYSENTER CET (U902), as for SYSCALL above: T = MEM + 9FF8h (SETSSBSY), P = IA32_PL3_SSP')
    T = MEM + 0x9FF8
    P = 0x7FFF12345670
    for s_cet in (CET_SH_STK_EN | CET_ENDBR_EN, 0):
        msr = {MSR_S_CET: s_cet, MSR_PL0_SSP: T, MSR_PL3_SSP: P}
        ssp = T if s_cet & CET_SH_STK_EN else 0
        n = sysenter(sysenter_state(cr4_cet=True, msr=msr, ssp=ssp))
        setss = [SETSSBSY] if s_cet & CET_SH_STK_EN else []
        asm = '; '.join([CET_ON, wrmsr_imm(MSR_S_CET, s_cet), wrmsr_imm(MSR_PL0_SSP, T), wrmsr_imm(MSR_PL3_SSP, P)]
                        + setss + [wrmsr_label(0x176, 'k'), 'mov rbp, rsp; mov rax, rsp; mov rdx, rax; shr rdx, 32; '
                                   'mov ecx, 0x175; wrmsr', 'mov ebp, 0x5555', 'sysenter',
                                   'k: ' + ENDBR64, RDSSPQ_RBP, rdmsr_to(MSR_PL3_SSP, 'r8'), rdmsr_to(MSR_S_CET, 'r9')])
        tok = T | 1 if s_cet & CET_SH_STK_EN else T
        rbp = n['ssp'] if ss_enabled(n, 0) else 0x5555
        s_after = n['msr'][MSR_S_CET] & ~CET_TRACKER
        a('%s | m+0x9FF8=%s =>! rbp=%s r8=%s r9=%s m+0x9FF8=%s' % (
            asm, le64(T), hx(rbp), hx(n['msr'][MSR_PL3_SSP]), hx(s_after), le64(tok)))
    asm2 = '; '.join([CET_ON, wrmsr_imm(MSR_S_CET, CET_ENDBR_EN), wrmsr_label(0x176, 'k'),
                      'mov rax, rsp; mov rdx, rax; shr rdx, 32; mov ecx, 0x175; wrmsr', 'sysenter', 'k: nop'])
    a('%s =>! #CP(3)' % asm2)


# ---- SDM Vol2B SYSRET / SYSEXIT (the parts U903 / U904 change) ------------------------------------
def sysret_rflags(r11):
    """RFLAGS := (R11 & 3C7FD7H) | 2"""
    return (r11 & 0x3C7FD7) | 2


def canonical(v, bits=48):
    top = (v & M64) >> (bits - 1)
    return top == 0 or top == (1 << (65 - bits)) - 1


def sysexit(sysenter_cs, rcx, rdx, opsize, cpl=0, paging_bits=48):
    """returns (rsp, rip, cs, ss) or raises Fault"""
    if ((sysenter_cs >> 2) & 0x3FFF) == 0 or cpl != 0:
        raise Fault('#GP', 0)
    if opsize == 64:
        if not canonical(rcx, paging_bits) or not canonical(rdx, paging_bits):
            raise Fault('#GP', 0)
        cs = ((sysenter_cs & 0xFFFF) + 32) & 0xFFFF
        rsp, rip = rcx & M64, rdx & M64
    else:
        cs = ((sysenter_cs & 0xFFFF) + 16) & 0xFFFF
        rsp, rip = rcx & 0xFFFFFFFF, rdx & 0xFFFFFFFF
    cs |= 3
    return rsp, rip, cs, (cs + 8) & 0xFFFF


def cases_sysret(a):
    a('# --- SYSRET RFLAGS (U903): (R11 AND 3C7FD7H) OR 2 - RF, VM and the reserved bits 0, VIF/VIP from R11.')
    a('# SYSRETQ to label u at CPL3; R15 = PUSHFQ image there (RF / VM clear in any PUSHFQ image), RBX = CS')
    for r11 in (0xFFFFFFFFFFFFFEFF, 0x0000000000030202, 0x00000000001A0ED7, 0x00000000FFC08AD5, 0x0000000000240246):
        fl = sysret_rflags(r11)
        asm = '; '.join([wrmsr_imm(MSR_STAR, 0x0023001000000000), 'lea rcx, [rip + u]; mov r11, %s' % hx(r11),
                         SYSRETQ, 'u: pushfq; pop r15; mov ebx, cs'])
        a('%s =>! rbx=0x33 r15=%s' % (asm, hx(pushfq_image(fl))))


def cases_sysexit(a):
    a('# --- SYSEXIT (U904): #GP(0) if IA32_SYSENTER_CS[15:2] = 0 (RPL-only selectors too); REX.W with RCX or RDX')
    a('# not canonical (4-level paging: 48 bits) #GP(0); nothing changes')
    for cs, rcx, rdx, w in ((1, 0x1000, 0x2000, 64), (3, 0x1000, 0x2000, 32), (0x10000, 0x1000, 0x2000, 64),
                            (0x10, 0x0000800000000000, 0x2000, 64), (0x10, 0x1000, 0xFFFF7FFFFFFFF000, 64)):
        try:
            sysexit(cs, rcx, rdx, w)
            raise AssertionError('sysexit must fault')
        except Fault as f:
            ins = '.byte 0x48, 0x0f, 0x35' if w == 64 else SYSEXIT32
            a('%s; mov rcx, %s; mov rdx, %s; %s =>! %s rcx=%s rdx=%s' % (
                wrmsr_imm(0x174, cs), hx(rcx), hx(rdx), ins, f.token(), hx(rcx), hx(rdx)))
    a('# 32-bit operand size: RSP := ECX, RIP := EDX (bits 63:32 ignored) -> compatibility mode CS 23h at label u;')
    a('# there PUSH CS (0Eh) writes the selector at ECX - 4 = MEM + 90FCh (only its 2 low bytes are checked: the SDM')
    a('# allows a 16-bit store), then SYSENTER back to 64-bit mode CPL0 at k (IA32_SYSENTER_ESP = the old RSP)')
    rsp, rip, cs, ss = sysexit(0x10, 0xAAAAAAAA00000000 | (MEM + 0x9100), 0x5555555500000000 | 0x30000000, 32)
    asm = '; '.join([wrmsr_imm(0x174, 0x10), wrmsr_label(0x176, 'k'),
                     'mov rax, rsp; mov rdx, rax; shr rdx, 32; mov ecx, 0x175; wrmsr',
                     'lea rdx, [rip + u]; mov rax, 0x5555555500000000; or rdx, rax',
                     'mov rcx, 0xAAAAAAAA%08X' % (MEM + 0x9100), SYSEXIT32,
                     'u: .byte 0x0e', 'sysenter', 'k: nop'])
    a('%s =>! m+0x90FC=%s' % (asm, (cs).to_bytes(2, 'little').hex().upper()))


def le64(v):
    return v.to_bytes(8, 'little').hex().upper()


def cases_all():
    lines = []
    a = lines.append
    a('# SYSCALL / SYSENTER / SYSRET / SYSEXIT and the MSR model (ledger U900-U929): expected values from the')
    a('# independent model Emulator/tools/isa/ref_sysmsr.py (regenerate with --write, do not edit). Unicorn')
    a('# only, MAX model, 64-bit mode; without cpl=3 the snippet starts at CPL0:')
    a('#   emu-alltest --cases Emulator\\data\\cases_sysmsr.txt --expect-only')
    cases_syscall(a)
    cases_sysenter(a)
    cases_sysret(a)
    cases_sysexit(a)
    return lines


def write_file(name, lines):
    path = os.path.join(DATA, name)
    with open(path, 'w', newline='\r\n') as f:
        for l in lines:
            f.write(l + '\n')
    print('wrote %s (%d lines)' % (path, len(lines)))


def selftest():
    fails = []

    def check(c, what):
        if not c:
            fails.append(what)

    # SYSCALL: R11 = RFLAGS, RFLAGS AND NOT FMASK, selectors (Vol2B pseudocode)
    n = syscall(base_state(rflags=0x240ED7, star=0x0023001300000000, fmask=0x40701, lstar=0x5000,
                           rip_next=0x1002))
    check(n['rcx'] == 0x1002 and n['rip'] == 0x5000 and n['r11'] == 0x240ED7, 'syscall rcx/rip/r11')
    check(n['rflags'] == 0x2008D6, 'syscall fmask (hand: 240ED7h AND NOT 40701h = 2008D6h)')
    check(n['cs'] == 0x10 and n['ss'] == 0x1B and n['cpl'] == 0, 'syscall selectors (SS not masked)')
    n = syscall(base_state(star=0xFFFB << 32))
    check(n['cs'] == 0xFFF8 and n['ss'] == 0x3, 'syscall selector wrap')
    n = syscall(base_state(rflags=0x202, fmask=0xFFFFFFFF))
    check(n['rflags'] == 2, 'fmask all: bit 1 stays')
    try:
        syscall(base_state(efer=0xD00))
        check(False, 'SCE = 0 #UD')
    except Fault as f:
        check(f.name == '#UD', 'SCE #UD name')
    # CET
    msr = {MSR_S_CET: 5, MSR_PL3_SSP: 0x1111}
    n = syscall(base_state(cr4_cet=True, msr=msr, ssp=0x0100000000001000))
    check(n['msr'][MSR_PL3_SSP] == 0xFF00000000001000, 'LA_adjust 57 (bit 56 -> 63:57)')
    check(n['ssp'] == 0 and n['msr'][MSR_S_CET] == 5 | CET_TRACKER, 'SSP := 0, tracker WAIT')
    check(la_adjust(0x0000800000000000) == 0x0000800000000000 and la_adjust(0x8000, 16) == M64 & ~0x7FFF,
          'la_adjust')
    n = syscall(base_state(cr4_cet=True, msr={MSR_U_CET: 1, MSR_PL3_SSP: 9}, ssp=0x2000, cpl=3))
    check(n['msr'][MSR_PL3_SSP] == 0x2000 and n['ssp'] == 0x2000, 'CPL3 save, no CPL0 shadow stack')
    n = syscall(base_state(cr4_cet=False, msr={MSR_S_CET: 5, MSR_PL3_SSP: 9}, ssp=0x2000))
    check(n['msr'][MSR_PL3_SSP] == 9 and n['ssp'] == 0x2000, 'CR4.CET = 0: no CET effect')
    # SYSENTER
    n = sysenter(sysenter_state(sysenter_cs=0x13, sysenter_esp=0x1234567890, sysenter_eip=0xFFFFFFFF80001000,
                                rflags=0x30A02 | VM, lma=False))
    check(n['cs'] == 0x10 and n['ss'] == 0x18 and n['rsp'] == 0x34567890 and n['rip'] == 0x80001000,
          'sysenter legacy: selectors, 32-bit ESP/EIP')
    check(n['rflags'] == 0x802 and n['cpl'] == 0, 'sysenter clears VM IF RF')
    n = sysenter(sysenter_state(sysenter_cs=0xFFFF, sysenter_esp=0xFFFF800000000000, lma=True))
    check(n['ss'] == 4 and n['rsp'] == 0xFFFF800000000000, 'sysenter SS wrap, 64-bit RSP')
    for cs in (0, 1, 2, 3):
        try:
            sysenter(sysenter_state(sysenter_cs=cs))
            check(False, 'sysenter #GP %d' % cs)
        except Fault as f:
            check(f.token() == '#GP(0)', 'sysenter #GP(0)')
    n = sysenter(sysenter_state(cr4_cet=True, msr={MSR_U_CET: 1, MSR_S_CET: 4}, ssp=0x0100000000000008, cpl=3,
                                lma=False))
    check(n['msr'][MSR_PL3_SSP] == 0x0100000000000008 and n['msr'][MSR_S_CET] == 4 | CET_TRACKER,
          'sysenter legacy: PL3_SSP := SSP (no LA_adjust), tracker')
    check(popfq_image(0xFFFFFFFF) == 0x247FD7 and pushfq_image(0x30202) == 0x202, 'popfq / pushfq images')
    # the generator runs and every case has an expectation
    lines = cases_all()
    check(all(l.startswith('#') or '=>' in l for l in lines), 'every case line has =>')
    if fails:
        for f in fails:
            print('FAIL: ' + f)
        return 1
    print('ref_sysmsr selftest: OK (%d case lines)' % sum(1 for l in lines if not l.startswith('#')))
    return 0


def main():
    if '--selftest' in sys.argv:
        sys.exit(selftest())
    if '--write' in sys.argv:
        write_file('cases_sysmsr.txt', cases_all())
        return
    print(__doc__)
    sys.exit(2)


if __name__ == '__main__':
    main()
