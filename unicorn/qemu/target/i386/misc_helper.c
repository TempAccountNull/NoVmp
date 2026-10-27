/*
 *  x86 misc helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/ioport.h"

#include "uc_priv.h"
#include "tcg/tcg-apple-jit.h"

#if __Use_Original_Qemu != 1 /* ours (U1461) */
/*
 * NoVmp (ledger U1461): the I/O instruction the IN / OUT hooks run for, so an SMI they signal
 * (UC_CTL_X86_SMI) is synchronous to it (smm_helper.c x86_smm_signal)
 */
static inline void smm_io_begin(CPUX86State *env, uint32_t port, int size, int in, uintptr_t ra)
{
    env->smm_io_port = (uint16_t)port;
    env->smm_io_size = (uint8_t)size;
    env->smm_io_in = (uint8_t)in;
    env->smm_io_ra = ra;
}

#endif /* __Use_Original_Qemu (U1461) */
void helper_outb(CPUX86State *env, uint32_t port, uint32_t data)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     glue(address_space_stb, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port, data & 0xff,
// #else
//     address_space_stb(env->uc, &env->uc->address_space_io, port, data & 0xff,
// #endif
//                       cpu_get_mem_attrs(env), NULL);
#if __Use_Original_Qemu == 1 /* original QEMU (U1461) */
    return cpu_outb(env->uc, port, data, GETPC());
#else /* ours (U1461) */
    smm_io_begin(env, port, 1, 0, GETPC());
    cpu_outb(env->uc, port, data, GETPC());
    env->smm_io_size = 0;
#endif /* __Use_Original_Qemu (U1461) */
}

target_ulong helper_inb(CPUX86State *env, uint32_t port)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     return glue(address_space_ldub, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port,
// #else
//     return address_space_ldub(env->uc, &env->uc->address_space_io, port,
// #endif
//                               cpu_get_mem_attrs(env), NULL);
#if __Use_Original_Qemu == 1 /* original QEMU (U1461) */
    return cpu_inb(env->uc, port, GETPC());
#else /* ours (U1461) */
    target_ulong v;

    smm_io_begin(env, port, 1, 1, GETPC());
    v = cpu_inb(env->uc, port, GETPC());
    env->smm_io_size = 0;
    return v;
#endif /* __Use_Original_Qemu (U1461) */
}

void helper_outw(CPUX86State *env, uint32_t port, uint32_t data)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     glue(address_space_stw, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port, data & 0xffff,
// #else
//     address_space_stw(env->uc, &env->uc->address_space_io, port, data & 0xffff,
// #endif
//                       cpu_get_mem_attrs(env), NULL);
#if __Use_Original_Qemu == 1 /* original QEMU (U1461) */
    return cpu_outw(env->uc, port, data, GETPC());
#else /* ours (U1461) */
    smm_io_begin(env, port, 2, 0, GETPC());
    cpu_outw(env->uc, port, data, GETPC());
    env->smm_io_size = 0;
#endif /* __Use_Original_Qemu (U1461) */
}

target_ulong helper_inw(CPUX86State *env, uint32_t port)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     return glue(address_space_lduw, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port,
// #else
//     return address_space_lduw(env->uc, &env->uc->address_space_io, port,
// #endif
//                               cpu_get_mem_attrs(env), NULL);
#if __Use_Original_Qemu == 1 /* original QEMU (U1461) */
    return cpu_inw(env->uc, port, GETPC());
#else /* ours (U1461) */
    target_ulong v;

    smm_io_begin(env, port, 2, 1, GETPC());
    v = cpu_inw(env->uc, port, GETPC());
    env->smm_io_size = 0;
    return v;
#endif /* __Use_Original_Qemu (U1461) */
}

void helper_outl(CPUX86State *env, uint32_t port, uint32_t data)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     glue(address_space_stl, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port, data,
// #else
//     address_space_stl(env->uc, &env->uc->address_space_io, port, data,
// #endif
//                       cpu_get_mem_attrs(env), NULL);
#if __Use_Original_Qemu == 1 /* original QEMU (U1461) */
    return cpu_outl(env->uc, port, data, GETPC());
#else /* ours (U1461) */
    smm_io_begin(env, port, 4, 0, GETPC());
    cpu_outl(env->uc, port, data, GETPC());
    env->smm_io_size = 0;
#endif /* __Use_Original_Qemu (U1461) */
}

target_ulong helper_inl(CPUX86State *env, uint32_t port)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     return glue(address_space_ldl, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port,
// #else
//     return address_space_ldl(env->uc, &env->uc->address_space_io, port,
// #endif
//                              cpu_get_mem_attrs(env), NULL);
#if __Use_Original_Qemu == 1 /* original QEMU (U1461) */
    return cpu_inl(env->uc, port, GETPC());
#else /* ours (U1461) */
    target_ulong v;

    smm_io_begin(env, port, 4, 1, GETPC());
    v = cpu_inl(env->uc, port, GETPC());
    env->smm_io_size = 0;
    return v;
#endif /* __Use_Original_Qemu (U1461) */
}

void helper_into(CPUX86State *env, int next_eip_addend)
{
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    if (eflags & CC_O) {
#if __Use_Original_Qemu != 1 /* ours (U1171) */
        env->fred_swexc = 1;    /* FRED event type 6, software exception (SDM 092 Vol3A 8.3.2.1) */
#endif /* __Use_Original_Qemu (U1171) */
        raise_interrupt(env, EXCP04_INTO, 1, 0, next_eip_addend);
    }
}

#if __Use_Original_Qemu != 1 /* ours (U1171) */
/* NoVmp (ledger U1171): SWAPGS is #UD with CR4.FRED = 1 (SDM 092 Vol3A 8.5.1, Vol2B SWAPGS) */
void helper_fred_ud_check(CPUX86State *env)
{
    if (x86_fred_enabled(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }
}

#endif /* __Use_Original_Qemu (U1171) */
void helper_cpuid(CPUX86State *env)
{
    uint32_t eax, ebx, ecx, edx;
    uc_engine *uc = env->uc;
    struct hook *hook;
    int skip_cpuid = 0;
    bool synced = false;
    cpu_svm_check_intercept_param(env, SVM_EXIT_CPUID, 0, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1303) */
    /* VMX non-root operation: CPUID exits unconditionally (SDM 092 Vol3C 28.1.2), before the hooks */
    x86_vmx_uncond_exit(env, VMX_EXIT_CPUID, GETPC());
#endif /* __Use_Original_Qemu (U1303) */

    // Unicorn: call registered CPUID hooks
    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN) {
        if (hook->to_delete)
            continue;
        if (!HOOK_BOUND_CHECK(hook, env->eip))
            continue;
        
        // Multiple cpuid callbacks returning different values is undefined.
        // true -> skip the cpuid instruction
        if (hook->insn == UC_X86_INS_CPUID) {
            uintptr_t pc = GETPC();
            if (!synced && !uc->skip_sync_pc_on_exit && pc) {
                cpu_restore_state(uc->cpu, pc, false);
                synced = true;
            }
            JIT_CALLBACK_GUARD_VAR(skip_cpuid, ((uc_cb_insn_cpuid_t)hook->callback)(env->uc, hook->user_data));
        }

        // the last callback may already asked to stop emulation
        if (env->uc->stop_request)
            break;
    }

    if (!skip_cpuid) {
        cpu_x86_cpuid(env, (uint32_t)env->regs[R_EAX], (uint32_t)env->regs[R_ECX],
                    &eax, &ebx, &ecx, &edx);
        env->regs[R_EAX] = eax;
        env->regs[R_EBX] = ebx;
        env->regs[R_ECX] = ecx;
        env->regs[R_EDX] = edx;
#if __Use_Original_Qemu != 1 /* ours (U1471) */
        /*
         * NoVmp (ledger U1471): SDM Vol2A CPUID Operation "IA32_BIOS_SIGN_ID MSR := Update with
         * installed microcode revision number" (every leaf); Vol3A 12.11.7 "If no update is
         * present in the processor, the value in the MSR remains unmodified". The revision is
         * the CPU profile's (UC_CTL_X86_MICROCODE); bits 31:0 (reserved) read 0. A CPUID that a
         * UC_X86_INS_CPUID hook skipped did not execute and deposits nothing.
         */
        if (IS_INTEL_CPU(env) && (env_archcpu(env)->ucode_rev >> 32)) {
            env->msr_bios_sign_id = env_archcpu(env)->ucode_rev & 0xffffffff00000000ULL;
        }
#endif /* __Use_Original_Qemu (U1471) */
    }

}

target_ulong helper_read_crN(CPUX86State *env, int reg)
{
    target_ulong val;

    cpu_svm_check_intercept_param(env, SVM_EXIT_READ_CR0 + reg, 0, GETPC());
    switch (reg) {
    default:
        val = env->cr[reg];
        break;
    case 8:
        if (!(env->hflags2 & HF2_VINTR_MASK)) {
#if __Use_Original_Qemu == 1 /* original QEMU (U960) */
            // val = cpu_get_apic_tpr(env_archcpu(env)->apic_state);
            val = 0;
#else /* ours (U960) */
            /* SDM Vol3A 13.8.6.1: CR8[3:0] = TPR[7:4] of the local APIC */
            val = x86_apic_get_cr8(env);
#endif /* __Use_Original_Qemu (U960) */
        } else {
            val = env->v_tpr;
        }
        break;
    }
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* VMX non-root operation: CR0 / CR4 bits of the guest/host mask read from the read shadow
       (SDM 092 Vol3C 28.3 MOV from CR0 / CR4) */
    val = x86_vmx_cr_read(env, reg, val);
#endif /* __Use_Original_Qemu (U1304) */
    return val;
}

#if __Use_Original_Qemu != 1 /* ours (U1965) */
/*
 * NoVmp (ledger U1965, plan 1.K.15 follow-up, user decision 2026-10-24: the MOV to CR0 / CR4 instruction applies the same
 * SDM #GP(0) checks as uc_reg_write (U1962), with ONE shared check function): the checks of MOV to CR0 / CR4 on the value
 * v about to be loaded - called by helper_write_crN (the instruction, U1966; LMSW goes through it) and by unicorn.c
 * x86_crw_write (uc_reg_write / uc_context_reg_write, U1962). No side effect; false = #GP(0). Rules (numbered as in the
 * kk_ unit tests; SDM 253667-093 Vol2B MOV-Move to/from Control Registers pp. 4-32 to 4-34, 253668-093 Vol3A 2.5
 * pp. 2-13 to 2-21, 5.1.2 p. 5-3, 5.10.1 p. 5-41, 326019-093 Vol3C 26.8 p. 26-3):
 *  (1) CR0[63:32] reserved: #GP(0) (Vol2B p. 4-32 "Attempting to set any reserved bits in CR0[63:32] results in a
 *      general-protection exception, #GP(0)."; Vol3A 2.5 p. 2-13).
 *  (2) reserved CR0[31:0] bits are ignored ("Attempting to set any reserved bits in CR0[31:0] is ignored.") and CR0.ET
 *      stays 1 ("CR0.ET remains set after any load of CR0"), Vol2B p. 4-32: *eff without them / with ET.
 *  (3) CR0.PG = 1 with CR0.PE = 0: #GP (Vol3A 2.5 p. 2-16 CR0.PG; Vol2B pp. 4-33 / 4-34 "invalid bit combinations").
 *  (4) CR0.NW = 1 with CR0.CD = 0: #GP (Vol2B pp. 4-33 / 4-34 "setting the CD flag to 0 when the NW flag is set to 1").
 *  (5) CR0.WP cleared while CR4.CET = 1: #GP (Vol3A 2.5 p. 2-16 CR0.WP, p. 2-19 CR4.CET "it must be clear before
 *      CR0.WP can be cleared"; was U114 in helper_write_crN).
 *  (6) CR0.PG cleared in 64-bit mode: #GP (Vol2B p. 4-34 "If an attempt is made to clear CR0.PG[bit 31]."; Vol3A 5.1.2
 *      p. 5-3 footnote).
 *  (7) CR0.PG cleared while CR4.PCIDE = 1: #GP (Vol3A 5.10.1 p. 5-41; Vol2B p. 4-33 compatibility mode).
 *  (8) CR0.PG set while IA32_EFER.LME = 1 and CR4.PAE = 0: #GP (Vol3A 5.1.2 p. 5-3 "Paging cannot be enabled ...";
 *      cpu_x86_update_cr0 used to return early - "XXX: generate an exception").
 *  (9) CR0.PG set with IA32_EFER.LME = 1 activates IA-32e mode: #GP if CS.L = 1 or TR references a 16-bit TSS (Vol2B
 *      p. 4-33 "If an attempt is made to activate IA-32e mode and either the current CS has the L-bit set or the TR
 *      references a 16-bit TSS.").
 * (10) / (15) VMX operation: CR0 / CR4 outside IA32_VMX_CR0_FIXED0/1 / CR4_FIXED0/1: #GP(0) (x86_vmx_cr_write_ok, U1302;
 *      Vol3C 26.8 p. 26-3; was U1302 in helper_write_crN).
 * (11) a reserved CR4 bit: #GP(0) (Vol2B p. 4-32 / p. 4-34; Vol3A 2.5 p. 2-13; Vol3A 2.5.1 p. 2-21: CPUID-qualified
 *      bits) - cr4_reserved_bits: the bits the CPU model lacks and the bits a strict CPUID profile hides (U1680).
 * (12) CR4.CET set while CR0.WP = 0: #GP (Vol3A 2.5 p. 2-19 "This flag can be set only if CR0.WP is set"; was U114).
 * (13) CR4.PCIDE 0 -> 1 while IA32_EFER.LMA = 0 or CR3[11:0] != 0: #GP (Vol3A 5.10.1 p. 5-41; Vol2B pp. 4-33 / 4-34).
 * (14) CR4.PAE or CR4.LA57 changed while CR0.PG = 1 and IA32_EFER.LME = 1: #GP (Vol3A 5.1.2 p. 5-3; Vol3A 2.5 p. 2-20
 *      CR4.LA57; Vol2B pp. 4-33 / 4-34 "leave IA-32e mode by clearing CR4.PAE"). The paging condition, not "64-bit mode"
 *      (upstream's LA57 check, CS.L = 1): the same in every state a processor reaches; Unicorn's UC_MODE_64 start state
 *      (IA-32e mode without paging, U1962) may change PAE / LA57 before it turns paging on.
 */
#define X86_MOVCR_CR0_NW (1U << 29)
#define X86_MOVCR_CR0_CD (1U << 30)
#define X86_MOVCR_CR0_DEFINED (CR0_PE_MASK | CR0_MP_MASK | CR0_EM_MASK | CR0_TS_MASK | CR0_ET_MASK | CR0_NE_MASK | \
                               CR0_WP_MASK | CR0_AM_MASK | X86_MOVCR_CR0_NW | X86_MOVCR_CR0_CD | CR0_PG_MASK)

bool x86_mov_cr0_check(CPUX86State *env, uint64_t v, uint64_t *eff)
{
    uint64_t old = env->cr[0];
    unsigned tss = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;

    if (v >> 32) {
        return false;                                                       /* (1) */
    }
    v = (v & X86_MOVCR_CR0_DEFINED) | CR0_ET_MASK;                          /* (2) */
    if ((v & CR0_PG_MASK) && !(v & CR0_PE_MASK)) {
        return false;                                                       /* (3) */
    }
    if ((v & X86_MOVCR_CR0_NW) && !(v & X86_MOVCR_CR0_CD)) {
        return false;                                                       /* (4) */
    }
    if (!(v & CR0_WP_MASK) && (env->cr[4] & CR4_CET_MASK)) {
        return false;                                                       /* (5) */
    }
    if ((old & CR0_PG_MASK) && !(v & CR0_PG_MASK) &&
        ((env->hflags & HF_CS64_MASK) || (env->cr[4] & CR4_PCIDE_MASK))) {
        return false;                                                       /* (6) / (7) */
    }
    if (!(old & CR0_PG_MASK) && (v & CR0_PG_MASK) && (env->efer & MSR_EFER_LME)) {
        if (!(env->cr[4] & CR4_PAE_MASK)) {
            return false;                                                   /* (8) */
        }
        if (!(env->efer & MSR_EFER_LMA) &&
            ((env->segs[R_CS].flags & DESC_L_MASK) ||
             (!(env->tr.flags & DESC_S_MASK) && (tss == 1 || tss == 3)))) {
            return false;                                                   /* (9) */
        }
    }
    if (!x86_vmx_cr_write_ok(env, 0, v)) {
        return false;                                                       /* (10) */
    }
    *eff = v;
    return true;
}

bool x86_mov_cr4_check(CPUX86State *env, uint64_t v)
{
    uint64_t old = env->cr[4];

    if (v & cr4_reserved_bits(env)) {
        return false;                                                       /* (11) */
    }
    if ((v & CR4_CET_MASK) && !(env->cr[0] & CR0_WP_MASK)) {
        return false;                                                       /* (12) */
    }
    if (!(old & CR4_PCIDE_MASK) && (v & CR4_PCIDE_MASK) &&
        (!(env->efer & MSR_EFER_LMA) || (env->cr[3] & 0xfff))) {
        return false;                                                       /* (13) */
    }
    if ((env->cr[0] & CR0_PG_MASK) && (env->efer & MSR_EFER_LME) &&
        ((old ^ v) & (CR4_PAE_MASK | CR4_LA57_MASK))) {
        return false;                                                       /* (14) */
    }
    if (!x86_vmx_cr_write_ok(env, 4, v)) {
        return false;                                                       /* (15) */
    }
    return true;
}

#endif /* __Use_Original_Qemu (U1965) */
void helper_write_crN(CPUX86State *env, int reg, target_ulong t0)
{
#if __Use_Original_Qemu != 1 /* ours (U1966) */
    uint64_t eff;

#endif /* __Use_Original_Qemu (U1966) */
    cpu_svm_check_intercept_param(env, SVM_EXIT_WRITE_CR0 + reg, 0, GETPC());
    switch (reg) {
    case 0:
#if __Use_Original_Qemu != 1 /* ours (U1966) */
        /*
         * NoVmp (ledger U1966): "The operand size for these instructions is always 32 bits in non-64-bit modes" (SDM
         * 253667-093 Vol2B MOV CR p. 4-32): outside 64-bit mode RAX[63:32] (kept from 64-bit mode) is not part of the
         * source, so rule (1) cannot see it (as U1170 for CR4). LMSW passes a 32-bit value.
         */
        if (!(env->hflags & HF_CS64_MASK)) {
            t0 = (uint32_t)t0;
        }
#endif /* __Use_Original_Qemu (U1966) */
#if __Use_Original_Qemu != 1 /* ours (U1304) */
        /* VMX non-root operation: bits of the CR0 guest/host mask are not modified (Vol3C 28.3) */
        t0 = x86_vmx_cr_write(env, 0, t0);
#endif /* __Use_Original_Qemu (U1304) */
#if __Use_Original_Qemu != 1 /* ours (U1966) */
        /*
         * NoVmp (ledger U1966, user decision 2026-10-24): the SDM #GP(0) checks of MOV to CR0, the ONE function shared with
         * uc_reg_write (x86_mov_cr0_check, U1965: rules (1)-(10); (5) and (10) were U114 / U1302 here); t0 = the value
         * loaded (ignored CR0[31:0] bits dropped, ET set). LMSW and the VMX non-root masking above feed it as well.
         */
        if (!x86_mov_cr0_check(env, t0, &eff)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        t0 = eff;
#endif /* __Use_Original_Qemu (U1966) */
#if __Use_Original_Qemu != 1 /* ours (U1306) */
        x86_vmx_pdpte_load(env, 0, (uint32_t)t0, GETPC());    /* EPT: PDPTEs (Vol3A 5.4.1) */
#endif /* __Use_Original_Qemu (U1306) */
        cpu_x86_update_cr0(env, (uint32_t)t0);
        break;
    case 3:
#if __Use_Original_Qemu != 1 /* ours (U1967) */
        /*
         * NoVmp (ledger U1967): outside 64-bit mode the source is 32 bits ("The operand size for these instructions is
         * always 32 bits in non-64-bit modes", "an execution of MOV to CR outside of 64-bit mode zeros the upper 32 bits
         * of the control register", SDM 253667-093 Vol2B MOV CR p. 4-32). In compatibility mode (IA32_EFER.LMA = 1) the
         * translator passed the whole register, so RAX[63:32] kept from 64-bit mode reached the CR3[63:MAXPHYADDR] #GP
         * below - a 64-Bit Mode exception only (Vol2B p. 4-34; the Compatibility Mode list on p. 4-33 has none) - or CR3.
         */
        if (!(env->hflags & HF_CS64_MASK)) {
            t0 = (uint32_t)t0;
        }
#endif /* __Use_Original_Qemu (U1967) */
#if __Use_Original_Qemu == 1 /* original QEMU (U478) */
        /* backport 3407259b20 + 24d84c7e48 (as upstream: SVM_EXIT_ERR) */
        if ((env->efer & MSR_EFER_LMA) &&
                (t0 & ((~0ULL) << env_archcpu(env)->phys_bits))) {
            cpu_vmexit(env, SVM_EXIT_ERR, 0, GETPC());
        }
#else /* ours (U478) */
        /*
         * NoVmp (ledger U478, upstream 3407259b20 + 24d84c7e48 with the SDM
         * exception): SDM Vol2B MOV CR, 64-Bit Mode Exceptions: "#GP(0) If an
         * attempt is made to write a 1 to any reserved bit in CR3[63:MAXPHYADDR]"
         * - a #GP, not an SVM exit (we are not an SVM guest). With CR4.PCIDE = 1
         * bit 63 is the no-invalidate flag and is not written ("The instruction
         * does not modify bit 63 of CR3"). MAXPHYADDR = the width the page walk
         * uses for its reserved-bit checks.
         * U1430: MAXPHYADDR = x86_maxphyaddr - CPUID.80000008H:EAX[7:0] less
         * IA32_TME_ACTIVATE[39:36] once TME is locked outside SEAM (SDM 093 Vol3A 2.5
         * footnote p. 2-13, Vol2B MOV CR p. 4-32). Was: phys_bits.
         * U1434: in VMX non-root operation with EPT the CPUID width (x86_paging_maxphyaddr;
         * Vol3C 28.3 p. 28-10: "Unlike other cases, the value M is not reduced outside SEAM").
         */
        if (env->efer & MSR_EFER_LMA) {
            if (env->cr[4] & CR4_PCIDE_MASK) {
                t0 &= ~(1ULL << 63);
            }
            /* U1180: with LAM, CR3[62:61] are LAM_U48 / LAM_U57, not reserved (Vol3A 4.4.1) */
            if (t0 & ((~0ULL) << x86_paging_maxphyaddr(env, x86_vmx_ept_active(env))) &
                ~(x86_lam_cpuid(env) ? (CR3_LAM_U57_MASK | CR3_LAM_U48_MASK) : 0)) {
                raise_exception_ra(env, EXCP0D_GPF, GETPC());
            }
        }
#endif /* __Use_Original_Qemu (U478) */
        /* backport 3407259b20: without IA-32e mode CR3 is a 32-bit register */
        if (!(env->efer & MSR_EFER_LMA)) {
            t0 &= 0xffffffffUL;
        }
#if __Use_Original_Qemu != 1 /* ours (U1306) */
        x86_vmx_pdpte_load(env, 3, t0, GETPC());              /* EPT: PDPTEs (Vol3A 5.4.1) */
#endif /* __Use_Original_Qemu (U1306) */
        cpu_x86_update_cr3(env, t0);
        break;
    case 4:
#if __Use_Original_Qemu != 1 /* ours (U1170) */
        /*
         * NoVmp (ledger U1170): "Execution of the MOV to CR4 instruction outside 64-bit mode clears
         * CR4[63:32]" (SDM 092 Vol3A 8.2.2): the source is 32 bits there (the translator passes the
         * whole register, whose upper half compatibility mode may have kept from 64-bit mode)
         */
        if (!(env->hflags & HF_CS64_MASK)) {
            t0 = (uint32_t)t0;
        }
#endif /* __Use_Original_Qemu (U1170) */
#if __Use_Original_Qemu != 1 /* ours (U1304) */
        /* VMX non-root operation: bits of the CR4 guest/host mask are not modified (Vol3C 28.3) */
        t0 = x86_vmx_cr_write(env, 4, t0);
#endif /* __Use_Original_Qemu (U1304) */
#if __Use_Original_Qemu == 1 /* original QEMU (U1966) */
        if (t0 & cr4_reserved_bits(env)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        if (((t0 ^ env->cr[4]) & CR4_LA57_MASK) &&
            (env->hflags & HF_CS64_MASK)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#else /* ours (U1966) */
        /*
         * NoVmp (ledger U1966, user decision 2026-10-24): the SDM #GP(0) checks of MOV to CR4, the ONE function shared with
         * uc_reg_write (x86_mov_cr4_check, U1965): (11) reserved bits (upstream's check), (12) CET without CR0.WP (was
         * U114 here), (13) PCIDE 0 -> 1 with IA32_EFER.LMA = 0 or CR3[11:0] != 0 (new), (14) PAE / LA57 changed with
         * CR0.PG = 1 and IA32_EFER.LME = 1 (replaces upstream's "LA57 changed in 64-bit mode"; clearing PAE is new),
         * (15) VMX fixed bits (was U1302 here)
         */
        if (!x86_mov_cr4_check(env, t0)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#endif /* __Use_Original_Qemu (U1966) */
#if __Use_Original_Qemu == 1 /* original QEMU (U1170) */
        cpu_x86_update_cr4(env, (uint32_t)t0);
#else /* ours (U1170) */
        /*
         * NoVmp (ledger U1170): all 64 bits (CR4.FRED = bit 32; the reserved-bit check above has
         * refused it without FRED). Outside 64-bit mode the operand is 32 bits wide, so MOV to CR4
         * there clears CR4[63:32] (SDM 092 Vol3A 8.2.2).
         */
        x86_vmx_pdpte_load(env, 4, t0, GETPC());              /* U1306: EPT PDPTEs (5.4.1) */
        cpu_x86_update_cr4(env, t0);
#endif /* __Use_Original_Qemu (U1170) */
        break;
    case 8:
#if __Use_Original_Qemu == 1 /* original QEMU (U960) */
#if 0
        if (!(env->hflags2 & HF2_VINTR_MASK)) {
            cpu_set_apic_tpr(env_archcpu(env)->apic_state, t0);
        }
#endif
        env->v_tpr = t0 & 0x0f;
#else /* ours (U960) */
        /*
         * SDM Vol3A 13.8.6.1 / Vol2B MOV CR: bits 63:4 of CR8 are reserved (#GP(0) if set);
         * the write loads TPR[7:4] = CR8[3:0], TPR[3:0] = 0 of the local APIC.
         */
        if (t0 & ~(target_ulong)0xf) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#if __Use_Original_Qemu != 1 /* ours (U1307) */
        /* VMX non-root operation with "use TPR shadow": VTPR and TPR virtualization (Vol3C 32.3) */
        if (x86_vmx_cr8_write(env, t0)) {
            break;
        }
#endif /* __Use_Original_Qemu (U1307) */
        if (env->hflags2 & HF2_VINTR_MASK) {
            env->v_tpr = t0 & 0x0f;
        } else {
            x86_apic_set_cr8(env, t0);
        }
#endif /* __Use_Original_Qemu (U960) */
        break;
    default:
        env->cr[reg] = t0;
        break;
    }
}

void helper_lmsw(CPUX86State *env, target_ulong t0)
{
    /* only 4 lower bits of CR0 are modified. PE cannot be set to zero
       if already set to one. */
    t0 = (env->cr[0] & ~0xe) | (t0 & 0xf);
    helper_write_crN(env, 0, t0);
}

void helper_invlpg(CPUX86State *env, target_ulong addr)
{
    X86CPU *cpu = env_archcpu(env);

    cpu_svm_check_intercept_param(env, SVM_EXIT_INVLPG, 0, GETPC());
    tlb_flush_page(CPU(cpu), addr);
}

void helper_flush_page(CPUX86State *env, target_ulong addr)
{
#if __Use_Original_Qemu != 1 /* ours (U1180) */
    /*
     * NoVmp (ledger U1180): INVLPG's operand is not LAM-masked (SDM 092 Vol3A 4.4.3) but the TLB
     * holds the entries of masked accesses under their unmasked addresses: with LAM active every
     * entry that may translate this page goes (a full flush - invalidating more is allowed)
     */
    if (x86_lam_active(env)) {
        tlb_flush(env_cpu(env));
        return;
    }
#endif /* __Use_Original_Qemu (U1180) */
    tlb_flush_page(env_cpu(env), addr);
}

#if __Use_Original_Qemu != 1 /* ours (U905) */
/*
 * NoVmp (ledger U905): the time-stamp counter RDTSC, RDTSCP and RDMSR 10H read: the host-time
 * counter plus IA32_TSC_ADJUST, which a WRMSR to IA32_TIME_STAMP_COUNTER moves by the delta
 * written (SDM Vol3B 18.17.3: a write to the TSC changes IA32_TSC_ADJUST by the same amount)
 */
static uint64_t msr_tsc_now(CPUX86State *env)
{
    uint64_t tsc = cpu_get_tsc(env) + env->tsc_offset + env->tsc_adjust;

#if __Use_Original_Qemu != 1 /* ours (U1620) */
    /* NoVmp (ledger U1620): plus the ticks TPAUSE / UMWAIT waited (helper_waitpkg) */
    tsc += env->tsc_wait;
#endif /* __Use_Original_Qemu (U1620) */
    return tsc;
}

#if __Use_Original_Qemu != 1 /* ours (U1671) */
/*
 * NoVmp (ledger U1671, plan 1.J.60): a WRMSR that moves the TSC by delta (SDM 093 Vol3B 20.17.3 p. 20-44): to
 * IA32_TIME_STAMP_COUNTER (delta = value - TSC: IA32_TSC_ADJUST moves by the same amount) or to IA32_TSC_ADJUST (delta =
 * value - IA32_TSC_ADJUST: the TSC moves by it). The move is no TSC increment: the VMX-preemption timer keeps its value
 * (x86_vmx_tsc_moved, Vol3C 28.5.1). The TPAUSE / UMWAIT wait (tsc_wait, U1620) is time passing, not a write.
 */
static void msr_tsc_move(CPUX86State *env, uint64_t delta)
{
    x86_vmx_tsc_moved(env, delta);
    env->tsc_adjust += delta;
}

#endif /* __Use_Original_Qemu (U1671) */
#endif /* __Use_Original_Qemu (U905) */
void helper_rdtsc(CPUX86State *env)
{
    uint64_t val;
    uc_engine *uc = env->uc;
    struct hook *hook;
    int skip_rdtsc = 0;
    bool synced = false;

    if ((env->cr[4] & CR4_TSD_MASK) && ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    cpu_svm_check_intercept_param(env, SVM_EXIT_RDTSC, 0, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* VMX non-root operation: "RDTSC exiting" (Vol3C 28.1.3), after the CR4.TSD #GP */
    x86_vmx_insn_check(env, VMX_EXIT_RDTSC, GETPC());
#endif /* __Use_Original_Qemu (U1304) */

    // Unicorn: call registered RDTSC hooks
    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN) {
        if (hook->to_delete)
            continue;
        if (!HOOK_BOUND_CHECK(hook, env->eip))
            continue;

        // Multiple rdtsc callbacks returning different values is undefined.
        // true -> skip the rdtsc instruction
        if (hook->insn == UC_X86_INS_RDTSC) {
            uintptr_t pc = GETPC();
            if (!synced && !uc->skip_sync_pc_on_exit && pc) {
                cpu_restore_state(uc->cpu, pc, false);
                synced = true;
            }
            JIT_CALLBACK_GUARD_VAR(skip_rdtsc, ((uc_cb_insn_cpuid_t)hook->callback)(env->uc, hook->user_data));
        }

        // the last callback may already asked to stop emulation
        if (env->uc->stop_request)
            break;
    }

    if (!skip_rdtsc) {
#if __Use_Original_Qemu == 1 /* original QEMU (U905) */
        val = cpu_get_tsc(env) + env->tsc_offset;
#else /* ours (U905) */
        val = msr_tsc_now(env);         /* IA32_TSC_ADJUST included (U905) */
#endif /* __Use_Original_Qemu (U905) */
#if __Use_Original_Qemu != 1 /* ours (U1304) */
        val = x86_vmx_guest_tsc(env, val);  /* TSC offsetting / scaling (Vol3C 28.3) */
#endif /* __Use_Original_Qemu (U1304) */
        env->regs[R_EAX] = (uint32_t)(val);
        env->regs[R_EDX] = (uint32_t)(val >> 32);
    }
}

void helper_rdtscp(CPUX86State *env)
{
    uint64_t val;
    uc_engine *uc = env->uc;
    struct hook *hook;
    int skip_rdtscp = 0;
    bool synced = false;

#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* VMX non-root operation: "enable RDTSCP" 0 is #UD before any other exception (Vol3C 28.3) */
    x86_vmx_rdtscp_check(env, GETPC());
#endif /* __Use_Original_Qemu (U1304) */
    if ((env->cr[4] & CR4_TSD_MASK) && ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    cpu_svm_check_intercept_param(env, SVM_EXIT_RDTSC, 0, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_insn_check(env, 51, GETPC());      /* RDTSCP: "RDTSC exiting" (exit reason 51) */
#endif /* __Use_Original_Qemu (U1304) */

    // Unicorn: call registered RDTSCP hooks
    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN) {
        if (hook->to_delete)
            continue;
        if (!HOOK_BOUND_CHECK(hook, env->eip))
            continue;

        // Multiple rdtscp callbacks returning different values is undefined.
        // true -> skip the rdtscp instruction
        if (hook->insn == UC_X86_INS_RDTSCP) {
            uintptr_t pc = GETPC();
            if (!synced && !uc->skip_sync_pc_on_exit && pc) {
                cpu_restore_state(uc->cpu, pc, false);
                synced = true;
            }
            JIT_CALLBACK_GUARD_VAR(skip_rdtscp, ((uc_cb_insn_cpuid_t)hook->callback)(env->uc, hook->user_data));
        }

        // the last callback may already asked to stop emulation
        if (env->uc->stop_request)
            break;
    }

    if (!skip_rdtscp) {
#if __Use_Original_Qemu == 1 /* original QEMU (U905) */
        val = cpu_get_tsc(env) + env->tsc_offset;
#else /* ours (U905) */
        val = msr_tsc_now(env);         /* IA32_TSC_ADJUST included (U905) */
#endif /* __Use_Original_Qemu (U905) */
#if __Use_Original_Qemu != 1 /* ours (U1304) */
        val = x86_vmx_guest_tsc(env, val);  /* TSC offsetting / scaling (Vol3C 28.3) */
#endif /* __Use_Original_Qemu (U1304) */
        env->regs[R_EAX] = (uint32_t)(val);
        env->regs[R_EDX] = (uint32_t)(val >> 32);

        env->regs[R_ECX] = (uint32_t)(env->tsc_aux);
    }
}

#if __Use_Original_Qemu != 1 /* ours (U907) */
/*
 * NoVmp (ledger U907, decision A17): the architectural performance-monitoring MSRs (SDM Vol4 Table
 * 2-2, Vol3B 21.2 "Architectural Performance Monitoring") as storage, enumerated by CPUID leaf 0AH
 * as the guest sees it (the built-in models report version 0: no PMU; a UC_CTL_X86_CPUID profile can
 * report one, as for RDPMC, U595):
 *   IA32_PMCx (C1H+x) and IA32_PERFEVTSELx (186H+x) for x < CPUID.0AH:EAX[15:8] or
 *   CPUID.23H.01H:EAX[x] (x <= 9: Table 2-2 lists PMC0-9); IA32_A_PMCx (4C1H+x) only with
 *   CPUID.23H.01H:EAX[x] (IA32_PERF_CAPABILITIES.FW_WRITE reads 0); IA32_FIXED_CTRm (309H+m, m <=
 *   6) for m < CPUID.0AH:EDX[4:0], CPUID.0AH:ECX[m] or CPUID.23H.01H:EBX[m]; IA32_FIXED_CTR_CTRL
 *   (38DH) with version > 1; IA32_PERF_GLOBAL_STATUS (38EH, R/O) and IA32_PERF_GLOBAL_CTRL (38FH)
 *   with version > 0; 390H (IA32_PERF_GLOBAL_OVF_CTRL, versions 2-3, IA32_PERF_GLOBAL_STATUS_RESET
 *   from version 4) with version > 1; IA32_PERF_GLOBAL_STATUS_SET (391H) and
 *   IA32_PERF_GLOBAL_INUSE (392H, R/O) with version > 3; IA32_PERF_CAPABILITIES (345H, R/O, reads
 *   0) with CPUID.01H:ECX.PDCM.
 * Writes: IA32_PMCx takes EAX sign-extended from bit 31 (Vol3B 21.2.1: EDX is ignored), kept to the
 * counter width CPUID.0AH:EAX[23:16]; IA32_A_PMCx / IA32_FIXED_CTRm: bits at or above the width
 * (CPUID.0AH:EAX[23:16] / EDX[12:5]) are reserved (#GP(0)); IA32_PERFEVTSELx: 63:32 reserved, AnyThread
 * (21) only with version > 2 and CPUID.0AH:EDX[15] = 0 (Vol3B: "a non-zero write of a field that is
 * introduced in a later ... version results in #GP"); IA32_FIXED_CTR_CTRL: EN_OS / EN_USR / PMI (and
 * AnyThread as above) of the enumerated counters 0-3 (Table 2-2 defines no fields for 4-6);
 * IA32_PERF_GLOBAL_CTRL: the enumerated counters; 390H clears and 391H sets bits of
 * IA32_PERF_GLOBAL_STATUS (overflow bits of the enumerated counters, 58-59 from version 4, 61 from
 * version 3, 62, and 63 for 390H only); both read 0 (our choice: their bits act on write).
 * Nothing counts (no event model): the counters keep the values written, no overflow or PMI ever
 * happens, IA32_PERF_GLOBAL_INUSE is computed from the event selects / FIXED_CTR_CTRL. RDPMC reads
 * the stored counter (U907, was 0).
 */
#define PMU_GP_MAX      10
#define PMU_FIX_MAX     MAX_FIXED_COUNTERS

typedef struct X86Pmu {
    int version;
    uint32_t gp, fix, a_pmc;        /* counters present (bit x) */
    int gp_width, fix_width;
    bool anythread;
} X86Pmu;

static void x86_pmu(CPUX86State *env, X86Pmu *p)
{
    uint32_t a, b, c, d, max, a23 = 0, b23 = 0, x;
    int i;

    memset(p, 0, sizeof(*p));
    cpu_x86_cpuid(env, 0, 0, &max, &x, &x, &x);
    if (max < 0xa) {
        return;
    }
    cpu_x86_cpuid(env, 0xa, 0, &a, &b, &c, &d);
    p->version = a & 0xff;
    if (!p->version) {
        return;
    }
    if (max >= 0x23) {
        cpu_x86_cpuid(env, 0x23, 1, &a23, &b23, &x, &x);
    }
    for (i = 0; i < PMU_GP_MAX; i++) {
        if (i < (int)((a >> 8) & 0xff) || ((a23 >> i) & 1)) {
            p->gp |= 1u << i;
        }
        if ((a23 >> i) & 1) {
            p->a_pmc |= 1u << i;
        }
    }
    for (i = 0; i < PMU_FIX_MAX; i++) {
        if (i < (int)(d & 0x1f) || ((c >> i) & 1) || ((b23 >> i) & 1)) {
            p->fix |= 1u << i;
        }
    }
    p->gp_width = (a >> 16) & 0xff;
    p->fix_width = (d >> 5) & 0xff;
    p->anythread = p->version > 2 && !(d & (1u << 15));
}

static uint64_t pmu_width_mask(int w)
{
    return w >= 64 ? ~0ULL : (1ULL << w) - 1;
}

/* IA32_PERF_GLOBAL_STATUS bits that exist (390H / 391H act on them) */
static uint64_t pmu_status_bits(const X86Pmu *p, bool set)
{
    return p->gp | ((uint64_t)p->fix << 32) | (p->version > 3 ? (3ULL << 58) : 0) |
           (p->version > 2 ? (1ULL << 61) : 0) | (1ULL << 62) | (set ? 0 : (1ULL << 63));
}

/* 0: not a PMU MSR of this model; 1: present */
static bool pmu_msr(CPUX86State *env, uint32_t msr, const X86Pmu *p)
{
    if (msr == 0x345) {
        return (env->features[FEAT_1_ECX] & CPUID_EXT_PDCM) &&
               (x86_cpuid_profile_mask(env, 1, 0, 2) & CPUID_EXT_PDCM);
    }
    if (msr >= MSR_P6_PERFCTR0 && msr < MSR_P6_PERFCTR0 + PMU_GP_MAX) {
        return (p->gp >> (msr - MSR_P6_PERFCTR0)) & 1;
    }
    if (msr >= 0x4c1 && msr < 0x4c1 + PMU_GP_MAX) {
        return (p->a_pmc >> (msr - 0x4c1)) & 1;
    }
    if (msr >= MSR_P6_EVNTSEL0 && msr < MSR_P6_EVNTSEL0 + PMU_GP_MAX) {
        return (p->gp >> (msr - MSR_P6_EVNTSEL0)) & 1;
    }
    if (msr >= MSR_CORE_PERF_FIXED_CTR0 && msr < MSR_CORE_PERF_FIXED_CTR0 + PMU_FIX_MAX) {
        return (p->fix >> (msr - MSR_CORE_PERF_FIXED_CTR0)) & 1;
    }
    switch (msr) {
    case MSR_CORE_PERF_FIXED_CTR_CTRL:
    case MSR_CORE_PERF_GLOBAL_OVF_CTRL:     /* 390H */
        return p->version > 1;
    case MSR_CORE_PERF_GLOBAL_STATUS:
    case MSR_CORE_PERF_GLOBAL_CTRL:
        return p->version > 0;
    case 0x391:
    case 0x392:
        return p->version > 3;
    default:
        return false;
    }
}

static uint64_t pmu_fixed_ctrl_valid(const X86Pmu *p)
{
    uint64_t v = 0;
    int m;

    for (m = 0; m < 4; m++) {
        if ((p->fix >> m) & 1) {
            v |= (uint64_t)(0xb | (p->anythread ? 4 : 0)) << (4 * m);
        }
    }
    return v;
}

/* the value check of a PMU MSR (false: #GP(0)) */
static bool pmu_write_ok(CPUX86State *env, uint32_t msr, uint64_t val, const X86Pmu *p)
{
    if (msr == 0x345 || msr == MSR_CORE_PERF_GLOBAL_STATUS || msr == 0x392) {
        return false;                                   /* R/O */
    }
    if (msr >= MSR_P6_PERFCTR0 && msr < MSR_P6_PERFCTR0 + PMU_GP_MAX) {
        return true;                                    /* EAX sign-extended, EDX ignored */
    }
    if (msr >= 0x4c1 && msr < 0x4c1 + PMU_GP_MAX) {
        return !(val & ~pmu_width_mask(p->gp_width));
    }
    if (msr >= MSR_P6_EVNTSEL0 && msr < MSR_P6_EVNTSEL0 + PMU_GP_MAX) {
        return !(val & ~(0xffffffffULL & ~(p->anythread ? 0 : (1ULL << 21))));
    }
    if (msr >= MSR_CORE_PERF_FIXED_CTR0 && msr < MSR_CORE_PERF_FIXED_CTR0 + PMU_FIX_MAX) {
        return !(val & ~pmu_width_mask(p->fix_width));
    }
    switch (msr) {
    case MSR_CORE_PERF_FIXED_CTR_CTRL:
        return !(val & ~pmu_fixed_ctrl_valid(p));
    case MSR_CORE_PERF_GLOBAL_CTRL:
        return !(val & ~(p->gp | ((uint64_t)p->fix << 32)));
    case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
        return !(val & ~pmu_status_bits(p, false));
    case 0x391:
        return !(val & ~pmu_status_bits(p, true));
    default:
        return true;
    }
}

static void pmu_write(CPUX86State *env, uint32_t msr, uint64_t val, const X86Pmu *p)
{
    if (msr >= MSR_P6_PERFCTR0 && msr < MSR_P6_PERFCTR0 + PMU_GP_MAX) {
        env->msr_gp_counters[msr - MSR_P6_PERFCTR0] =
            (uint64_t)(int64_t)(int32_t)val & pmu_width_mask(p->gp_width);
    } else if (msr >= 0x4c1 && msr < 0x4c1 + PMU_GP_MAX) {
        env->msr_gp_counters[msr - 0x4c1] = val;
    } else if (msr >= MSR_P6_EVNTSEL0 && msr < MSR_P6_EVNTSEL0 + PMU_GP_MAX) {
        env->msr_gp_evtsel[msr - MSR_P6_EVNTSEL0] = val;
    } else if (msr >= MSR_CORE_PERF_FIXED_CTR0 && msr < MSR_CORE_PERF_FIXED_CTR0 + PMU_FIX_MAX) {
        env->msr_fixed_counters[msr - MSR_CORE_PERF_FIXED_CTR0] = val;
    } else if (msr == MSR_CORE_PERF_FIXED_CTR_CTRL) {
        env->msr_fixed_ctr_ctrl = val;
    } else if (msr == MSR_CORE_PERF_GLOBAL_CTRL) {
        env->msr_global_ctrl = val;
    } else if (msr == MSR_CORE_PERF_GLOBAL_OVF_CTRL) {
        env->msr_global_status &= ~val;
    } else if (msr == 0x391) {
        env->msr_global_status |= val;
    }
}

static uint64_t pmu_read(CPUX86State *env, uint32_t msr, const X86Pmu *p)
{
    if (msr >= MSR_P6_PERFCTR0 && msr < MSR_P6_PERFCTR0 + PMU_GP_MAX) {
        return env->msr_gp_counters[msr - MSR_P6_PERFCTR0];
    }
    if (msr >= 0x4c1 && msr < 0x4c1 + PMU_GP_MAX) {
        return env->msr_gp_counters[msr - 0x4c1];
    }
    if (msr >= MSR_P6_EVNTSEL0 && msr < MSR_P6_EVNTSEL0 + PMU_GP_MAX) {
        return env->msr_gp_evtsel[msr - MSR_P6_EVNTSEL0];
    }
    if (msr >= MSR_CORE_PERF_FIXED_CTR0 && msr < MSR_CORE_PERF_FIXED_CTR0 + PMU_FIX_MAX) {
        return env->msr_fixed_counters[msr - MSR_CORE_PERF_FIXED_CTR0];
    }
    switch (msr) {
    case MSR_CORE_PERF_FIXED_CTR_CTRL:
        return env->msr_fixed_ctr_ctrl;
    case MSR_CORE_PERF_GLOBAL_CTRL:
        return env->msr_global_ctrl;
    case MSR_CORE_PERF_GLOBAL_STATUS:
        return env->msr_global_status;
    case 0x392: {
        /* Vol3B 21.2.6: PERFEVTSELn[7:0] != 0; the fixed counter's enable bits != 0; PMI in use */
        uint64_t v = 0;
        bool pmi = false;
        int i;

        for (i = 0; i < PMU_GP_MAX; i++) {
            if ((p->gp >> i) & 1) {
                v |= (uint64_t)((env->msr_gp_evtsel[i] & 0xff) != 0) << i;
                pmi |= (env->msr_gp_evtsel[i] >> 20) & 1;
            }
        }
        for (i = 0; i < 4; i++) {
            v |= (uint64_t)(((env->msr_fixed_ctr_ctrl >> (4 * i)) & 3) != 0) << (32 + i);
            pmi |= (env->msr_fixed_ctr_ctrl >> (4 * i + 3)) & 1;
        }
        return v | ((uint64_t)pmi << 63);
    }
    default:
        return 0;                                       /* 345H, 390H, 391H */
    }
}

#endif /* __Use_Original_Qemu (U907) */
void helper_rdpmc(CPUX86State *env)
{
    /* backport of QEMU c45b426acd: #GP(0) if CPL > 0 and CR4.PCE = 0 (SDM Vol2 RDPMC) */
    if (((env->cr[4] & CR4_PCE_MASK) == 0 ) &&
        ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    cpu_svm_check_intercept_param(env, SVM_EXIT_RDPMC, 0, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_insn_check(env, VMX_EXIT_RDPMC, GETPC());     /* "RDPMC exiting" (Vol3C 28.1.3) */
#endif /* __Use_Original_Qemu (U1304) */

#if __Use_Original_Qemu == 1 /* original QEMU (U595) */
    /* currently unimplemented */
    qemu_log_mask(LOG_UNIMP, "x86: unimplemented rdpmc\n");
    raise_exception_err(env, EXCP06_ILLOP, 0);
#else /* ours (U595) */
    /*
     * NoVmp (ledger U595): SDM Vol2B RDPMC reads the counter ECX selects into EDX:EAX (upper
     * halves of RAX/RDX cleared, RCX[63:32] ignored) and is #GP(0) for "an unsupported PMC
     * encoding". Which counters exist comes from CPUID (the profile when one is installed):
     *  - CPUID.0AH:EAX[7:0] = 0 (the built-in models: QEMU's TCG has no PMU): no counter is
     *    enumerated, every ECX is unsupported -> #GP(0);
     *  - else ECX[31:16] = type, ECX[15:0] = index: type 0 general-purpose, index <
     *    CPUID.0AH:EAX[15:8] or CPUID.23H.01H:EAX[index] = 1; type 4000H fixed-function,
     *    index < CPUID.0AH:EDX[4:0] or CPUID.0AH:ECX[index] = 1 or CPUID.23H.01H:EBX[index]
     *    = 1 (index <= 31); type 2000H (performance metrics) needs
     *    IA32_PERF_CAPABILITIES.PERF_METRICS_AVAILABLE, which the emulator does not report;
     *    any other type -> #GP(0).
     * Counter model: the emulator counts no events. A counter only counts while it is
     * enabled (IA32_PERFEVTSELx.EN / IA32_FIXED_CTR_CTRL with IA32_PERF_GLOBAL_CTRL, SDM
     * Vol3B 22.2), the counters and their controls are 0 after reset (SDM Vol3A Table 12-1)
     * and the emulator does not implement those MSRs (WRMSR to them is ignored, RDMSR reads
     * 0), so no counter can be enabled and every supported counter reads 0 - the same value
     * RDMSR returns for IA32_PMCx / IA32_FIXED_CTRx. Deterministic; documented in
     * docs/quirks.md ("SDM undefined, our choice"). U907: the MSRs are storage now and RDPMC
     * reads the value last written (still nothing counts).
     */
    {
        uint32_t ecx = (uint32_t)env->regs[R_ECX];
        uint32_t type = ecx >> 16, idx = ecx & 0xffff;
        uint32_t a, b, c, d, max, a23 = 0, b23 = 0, x;
        bool ok = false;

        cpu_x86_cpuid(env, 0xa, 0, &a, &b, &c, &d);
        if (a & 0xff) {
            cpu_x86_cpuid(env, 0, 0, &max, &b, &x, &x);
            cpu_x86_cpuid(env, 0xa, 0, &a, &b, &c, &d);
            if (max >= 0x23) {
                cpu_x86_cpuid(env, 0x23, 1, &a23, &b23, &x, &x);
            }
            if (type == 0) {
                ok = idx < ((a >> 8) & 0xff) || (idx <= 31 && ((a23 >> idx) & 1));
            } else if (type == 0x4000) {
                ok = idx < (d & 0x1f) ||
                     (idx <= 31 && (((c >> idx) & 1) || ((b23 >> idx) & 1)));
            }
        }
        if (!ok) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#if __Use_Original_Qemu != 1 /* ours (U907) */
        /* NoVmp (ledger U907): the stored counter (the PMU MSRs are storage, nothing counts) */
        {
            X86Pmu p;
            uint64_t v = 0;

            x86_pmu(env, &p);
            if (type == 0 && idx < PMU_GP_MAX) {
                v = env->msr_gp_counters[idx] & pmu_width_mask(p.gp_width);
            } else if (type == 0x4000 && idx < PMU_FIX_MAX) {
                v = env->msr_fixed_counters[idx] & pmu_width_mask(p.fix_width);
            }
            env->regs[R_EAX] = (uint32_t)v;
            env->regs[R_EDX] = (uint32_t)(v >> 32);
        }
#endif /* __Use_Original_Qemu (U907) */
    }
#endif /* __Use_Original_Qemu (U595) */
}

#if __Use_Original_Qemu != 1 /* ours (U103) */
/* canonical relative to the maximum linear-address width (CPUID.80000008H:EAX[15:8]) */
static bool novmp_canonical(CPUX86State *env, uint64_t addr)
{
    int shift = (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57) ? 64 - 57 : 64 - 48;

    return (uint64_t)((int64_t)(addr << shift) >> shift) == addr;
}

#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U1063) */
/*
 * NoVmp (ledger U1063): the X86CPU of the model for an MSR access. A uc_context image
 * (msr_api == 2, U878) has none of its own (env_archcpu would point in front of the image): the
 * engine's CPU, as helper_rdmsr does since U878.
 */
static X86CPU *msr_cpu(CPUX86State *env)
{
    return env->msr_api == 2 ? X86_CPU(env->uc->cpu) : env_archcpu(env);
}

#endif /* __Use_Original_Qemu (U1063) */
#if __Use_Original_Qemu != 1 /* ours (U1430) */
/*
 * NoVmp (ledger U1430): the effective MAXPHYADDR. SDM 093 Vol3A 3.3.1 (p. 3-6): "If
 * IA32_TME_ACTIVATE[0] = 1 (indicating that TME has been configured), MAXPHYADDR is reduced by the
 * value of IA32_TME_ACTIVATE[39:36] when a logical processor is outside secure arbitration mode
 * (SEAM ...); the value is not reduced in SEAM"; the same footnote at every use of MAXPHYADDR (2.5
 * p. 2-13 CR3, 5.1.4 p. 5-6 and 6.13 p. 6-33 paging entries, 14.11.2.3 p. 14-24 MTRRs, Vol4 2.1). Only
 * the TDX-reserved KeyID bits (39:36) are subtracted, the shared KeyID bits (35:32 - 39:36) stay
 * address bits. The width is a function of the lock bit, not of the enable bit (bit 1). Used by MOV
 * to CR3 (U1430), the IA-32 page walk without EPT (excp_helper.c, U1430) and the MTRRs (U1431).
 * Kept at CPUID.80000008H:EAX[7:0] (cpu->phys_bits): CPUID itself (MKTME 336907-007 4.1.4),
 * guest-physical addresses under EPT (Vol3A 5.1.4 "without any regard to TME or SEAM";
 * x86_paging_maxphyaddr, U1434), IA32_RTIT_OUTPUT_BASE (Vol3C 35.4.2 / 36.2.8.7: checked against
 * the CPUID width at WRMSR; Intel PT is not modelled), IA32_TME_EXCLUDE_MASK / _BASE (writable only
 * before the lock).
 * The model is always outside SEAM (x86_in_seam), so a lock with TDX_RESERVED_KEYID_BITS > 0 reduces
 * the width (emulator policy, docs/quirks.md). Review: emulator\notes\eread\T1_tmekeyid_review.md.
 */
int x86_maxphyaddr(CPUX86State *env)
{
    int m = (int)msr_cpu(env)->phys_bits;      /* a uc_context image: the engine's CPU (U1063) */

    if ((env->tme_activate & 1) && !x86_in_seam(env)) {
        m -= (int)((env->tme_activate >> 36) & 0xf);
    }
    return m;
}

#endif /* __Use_Original_Qemu (U1430) */
#if __Use_Original_Qemu != 1 /* ours (U1434) */
/*
 * NoVmp (ledger U1434): MAXPHYADDR of CR3 / paging-structure addresses (cpu.h). Users: MOV to CR3
 * and the IA-32 page walk (with x86_vmx_ept_active), the VM-entry checks of the guest CR3 field and
 * the guest PDPTEs (ept = "enable EPT" of the VMCS) and the PDPTE loads through EPT (vmx_helper.c).
 */
int x86_paging_maxphyaddr(CPUX86State *env, bool ept)
{
    return ept ? (int)msr_cpu(env)->phys_bits : x86_maxphyaddr(env);
}

#endif /* __Use_Original_Qemu (U1434) */
#if __Use_Original_Qemu != 1 /* ours (U1432) */
/*
 * NoVmp (ledger U1432): MAXAPICADDR of IA32_APIC_BASE. SDM 093 Vol3A 13.4.4 footnote 1 (p. 13-8) and
 * Vol4 Table 2-2 1BH (p. 2-5): normally CPUID.80000008H:EAX[7:0]; "If IA32_TME_ACTIVATE[39:36] > 0
 * ..., MAXAPICADDR is reduced by the value of IA32_TME_ACTIVATE[35:32] (the KeyID bits in the address
 * must be 0)" - every KeyID bit, not only the TDX-reserved ones, and no lock / SEAM condition in the
 * text (39:36 is non-zero only after a locking write in this model). Vol3C 35.4.2 (p. 35-5): "a
 * non-zero KeyID results in #GP(0)".
 */
int x86_maxapicaddr(CPUX86State *env)
{
    int m = (int)msr_cpu(env)->phys_bits;

    if ((env->tme_activate >> 36) & 0xf) {
        m -= (int)((env->tme_activate >> 32) & 0xf);
    }
    return m;
}

#endif /* __Use_Original_Qemu (U1432) */
#if __Use_Original_Qemu != 1 /* ours (U1020) */
/*
 * NoVmp (ledger U1020): the TME / TME-MK MSRs, present with CPUID.(07H,0):ECX.TME_EN[13] (SDM
 * Vol1 Table 21-22, Vol4 Table 2-2 981H-984H and 87H; Intel Architecture Memory Encryption
 * Technologies Specification 336907-007 rev. 1.7, 4.1-4.2). U1022: without TME_EN, RDMSR and
 * WRMSR of these MSRs raise #GP(0) (SDM Vol4 2.1: an MSR the processor does not support "will
 * generate an exception"; MKTME Table 4-3 "WRMSR when not enumerated. #GP(0)"); an API access
 * is dropped / reads 0. U1060: the MSRs are in the U905 MSR list (msr_present); an API access
 * that would #GP(0) returns UC_ERR_EXCEPTION and changes nothing.
 *   IA32_TME_CAPABILITY (981H): NOVMP_TME_CAPABILITY, read-only (WRMSR #GP(0)).
 *   IA32_TME_ACTIVATE (982H): U1022: a write while locked (bit 0) is ignored - SDM Vol4 Table 2-2
 *     982H: "Any write to the following MSRs will be ignored after they are locked"; the MKTME
 *     spec's Table 4-3 says #GP(0) instead (docs/quirks.md "Specification conflicts": the SDM
 *     wins). Table 4-3 otherwise: #GP(0) for reserved bits 30:8,
 *     47:40, 63:52 (and 31 without bypass support, 63:32 without TME-MK), for a policy (7:4)
 *     whose IA32_TME_CAPABILITY bit is 0 or that selects an integrity algorithm (capability bits
 *     1 and 3; rev. 1.6 / SDM Vol4: "not allowed to be used for TME ... will result in #GP"),
 *     MK_TME_KEYID_BITS (35:32) > MK_TME_MAX_KEYID_BITS, MK_TME_KEYID_BITS > 0 with Hardware
 *     Encryption Enable (bit 1) = 0, TDX_RESERVED_KEYID_BITS (39:36) > MK_TME_KEYID_BITS.
 *     Otherwise: enable = 0 -> locked, TME disabled (RDMSR x..x01b); enable = 1, key select (bit
 *     2) = 0 -> a new TME key, locked, enabled (x..x011b) - the key is internal to the hardware
 *     (not software-visible, nothing is drawn from the RDRAND source), the RNG never fails here;
 *     enable = 1, key select = 1 -> "restore the TME key from storage": the model has no
 *     standby/resume storage (bit 3 "save key" is kept in the MSR and has no other effect), so
 *     this is the "zero key restored" row: not enabled, not locked, x..x100b with
 *     MK_TME_KEYID_BITS = 0, "write not committed" (MSR unchanged) with MK_TME_KEYID_BITS > 0.
 *     The lock is cleared only by a CPU reset (reset area of CPUX86State).
 *   IA32_TME_EXCLUDE_MASK / _BASE (983H / 984H): #GP(0) while IA32_TME_ACTIVATE is locked, for
 *     reserved bits (MASK 10:0, BASE 11:0, both 63:MAXPHYADDR) and for a TMEEMASK that is not a
 *     contiguous region (its set bits must run from MAXPHYADDR-1 down without a gap; 0 = whole
 *     space). MAXPHYADDR = CPUID.80000008H:EAX[7:0] (cpu->phys_bits): writes are possible only
 *     before activation, when no KeyID / TDX reduction applies yet.
 *   IA32_MKTME_KEYID_PARTITIONING (87H, R/O, WRMSR #GP(0)): 0 while unlocked; locked: NUM_MKTME_
 *     KEYIDS = 2^(k-p) - 1 (KeyIDs 1 .. 2^(k-p)-1: the TDX bits are taken from the most significant
 *     KeyID bit downward), at most MK_TME_MAX_KEYS; NUM_TDX_PRIV_KEYIDS = 0 - the field is
 *     "supported on all parts that enumerate support for SEAM mode" (SDM Vol4) and the model has
 *     no SEAM. U1333: with UC_X86_FEATURE_SEAM it is 2^k - 2^(k-p) (x86_seam_tdx_priv_keyids).
 * U1433: TDX_RESERVED_KEYID_BITS (39:36) without SEAM - EMULATOR POLICY, not a normative rule:
 *   SDM 093 Vol4 Table 2-2 982H (p. 2-66) and MKTME 336907-007 Tables 4-2 / 4-3 (pp. 15-16) give
 *   only "> MK_TME_KEYID_BITS -> #GP(0)" (and "= 35:32 if IA32_TME_CAPABILITY[28] = 1", 0 here); no
 *   row ties 39:36 to SEAM enumeration, while 35:32 is "Reserved if TME-MK is not enumerated". The
 *   architecture leaves p > 0 on a processor without SEAM unspecified; tme_activate_write accepts it
 *   exactly as the documented error rows (docs/quirks.md).
 * U1430-U1432: the consequences the SDM defines for a lock with p > 0 apply, the model being always
 *   outside SEAM (x86_in_seam, cpu.h): MAXPHYADDR = CPUID width - p for MOV to CR3, the IA-32 page
 *   walk and the MTRRs (x86_maxphyaddr); MAXAPICADDR = CPUID width - k for IA32_APIC_BASE
 *   (x86_maxapicaddr); PCONFIG rejects SEAM-private KeyIDs (helper_pconfig, U1021). CPUID.80000008H,
 *   IA32_TME_EXCLUDE_MASK / _BASE and the SVM nested-paging walk keep the full width. WRMSR 982H
 *   flushes no TLB entry (the SDM is silent; translations cached before the lock stay until
 *   software invalidates them).
 * Not modelled: memory encryption itself (a KeyID selects no key; data is plain text) and the
 * removal of the KeyID bits from a physical address - an address that keeps a KeyID bit reaches
 * the host mapping at that full address, so KeyIDs alias the same memory only where the host maps
 * the aliases (docs/quirks.md); the exclusion range's effect; MK_TME_CORE_ACTIVATE (9FFH,
 * model-specific "BIOS only" MSR) and IA32_TME_CLEAR_SAVED_KEY (9FBH: capability bit 30 = 0).
 */
static bool tme_enumerated(CPUX86State *env)
{
    return (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_TME) != 0;
}

/* bits MAXPHYADDR-1:12 */
static uint64_t tme_pa_field(CPUX86State *env)
{
    int m = msr_cpu(env)->phys_bits;                /* U1063: an image: the engine's CPU */

    return MAKE_64BIT_MASK(12, m - 12);
}

/* false = #GP(0) */
static bool tme_activate_write(CPUX86State *env, uint64_t val)
{
    const uint64_t cap = NOVMP_TME_CAPABILITY;
    unsigned maxk = (unsigned)(cap >> 32) & 0xf;
    unsigned policy = (unsigned)(val >> 4) & 0xf;
    unsigned k = (unsigned)(val >> 32) & 0xf, p = (unsigned)(val >> 36) & 0xf;
    uint64_t rsvd = MAKE_64BIT_MASK(8, 23) | MAKE_64BIT_MASK(40, 8);

    if (!(cap & (1ULL << 31))) {
        rsvd |= 1ULL << 31;                 /* TME encryption bypass not supported */
    }
    rsvd |= maxk ? MAKE_64BIT_MASK(52, 12) : MAKE_64BIT_MASK(32, 32);
    if (env->tme_activate & 1) {
        return true;                        /* locked: ignored (SDM Vol4, U1022) */
    }
    if (val & rsvd) {
        return false;
    }
    if (policy > 3 || !((cap >> policy) & 1) || policy == 1 || policy == 3) {
        return false;
    }
    if (k > maxk || (k && !(val & 2)) || p > k) {
        return false;
    }
    if (!(val & 2)) {
        env->tme_activate = (val & ~3ULL) | 1;          /* TME disabled, locked: x..x01b */
    } else if (!(val & 4)) {
        env->tme_activate = val | 3;                    /* new TME key, locked: x..x011b */
#if __Use_Original_Qemu != 1 /* ours (U1021) */
        /* "All KeyIDs default to TME behavior on activation of TME-MK" (SDM Vol2B PCONFIG) */
        memset(env->mktme_keys, 0, sizeof(env->mktme_keys));
#endif /* __Use_Original_Qemu (U1021) */
    } else if (k == 0) {
        env->tme_activate = val & ~3ULL;                /* no saved key: x..x100b */
    }                                                   /* else: write not committed */
    return true;
}

/* false = #GP(0) */
static bool tme_wrmsr(CPUX86State *env, uint32_t msr, uint64_t val)
{
    uint64_t field = tme_pa_field(env), f;

    switch (msr) {
    case MSR_IA32_TME_ACTIVATE:
        return tme_activate_write(env, val);
    case MSR_IA32_TME_EXCLUDE_MASK:
        f = val & field;
        if ((env->tme_activate & 1) || (val & ~(field | (1ULL << 11))) ||
            (f && f != (field & ~((f & -f) - 1)))) {
            return false;
        }
        env->tme_exclude_mask = val;
        return true;
    case MSR_IA32_TME_EXCLUDE_BASE:
        if ((env->tme_activate & 1) || (val & ~field)) {
            return false;
        }
        env->tme_exclude_base = val;
        return true;
    default:                                /* IA32_TME_CAPABILITY, PARTITIONING: R/O */
        return false;
    }
}

static uint64_t tme_rdmsr(CPUX86State *env, uint32_t msr)
{
    uint64_t act = env->tme_activate;
    unsigned k = (unsigned)(act >> 32) & 0xf, p = (unsigned)(act >> 36) & 0xf;
    uint64_t n;

    if (!tme_enumerated(env)) {
        return 0;
    }
    switch (msr) {
    case MSR_IA32_TME_CAPABILITY:
        return NOVMP_TME_CAPABILITY;
    case MSR_IA32_TME_ACTIVATE:
        return act;
    case MSR_IA32_TME_EXCLUDE_MASK:
        return env->tme_exclude_mask;
    case MSR_IA32_TME_EXCLUDE_BASE:
        return env->tme_exclude_base;
    default:                                /* IA32_MKTME_KEYID_PARTITIONING */
        if (!(act & 1) || k == 0) {
            return 0;
        }
        n = (1ULL << (k - p)) - 1;
        n = MIN(n, (uint64_t)NOVMP_MKTME_MAX_KEYS);
#if __Use_Original_Qemu != 1 /* ours (U1333) */
        /* U1333: NUM_TDX_PRIV_KEYIDS (63:32) when the CPU enumerates SEAM (seam_model.c) */
        n |= x86_seam_tdx_priv_keyids(env) << 32;
#endif /* __Use_Original_Qemu (U1333) */
        return n;
    }
}

#endif /* __Use_Original_Qemu (U1020) */
#if __Use_Original_Qemu != 1 /* ours (U114) */
static bool cet_canonical(CPUX86State *env, uint64_t v)
{
    int64_t sext = (int64_t)v >> ((env->cr[4] & CR4_LA57_MASK) ? 56 : 47);

    return sext == 0 || sext == -1;
}

#if __Use_Original_Qemu != 1 /* ours (U760) */
/*
 * NoVmp (ledger U760): the canonical check of a CET MSR value (IA32_U_CET/S_CET
 * EB_LEG_BITMAP_BASE, IA32_PLx_SSP, IA32_INTERRUPT_SSP_TABLE_ADDR) on WRMSR and XRSTORS
 * is CPU canonicality (SDM Vol3A 4.5.3): relative to the maximum linear-address width
 * the CPU reports (57 with CPUID.(7,0):ECX.LA57, else 48), not to the current paging
 * mode - the check novmp_canonical does for the other MSRs. cet_canonical (CR4.LA57)
 * stays for shadow-stack linear addresses.
 */
static bool cet_msr_canonical(CPUX86State *env, uint64_t v)
{
    return novmp_canonical(env, v);
}

#endif /* __Use_Original_Qemu (U760) */
/*
 * NoVmp (ledger U114): WRMSR to a CET MSR (SDM Vol4 Table 2-2). The MSRs exist
 * with CET_SS or CET_IBT (without either they stay unknown MSRs: ignored, as
 * before). IA32_U_CET/IA32_S_CET: bits 1:0 need CET_SS, bits 5:2 and 63:10
 * need CET_IBT, 9:6 reserved, EB_LEG_BITMAP_BASE canonical, SUPPRESS = 1 only
 * with TRACKER = IDLE. IA32_PLx_SSP (CET_SS): bits 1:0 zero, canonical.
 * IA32_INTERRUPT_SSP_TABLE_ADDR (CET_SS): canonical. Returns false when WRMSR
 * must raise #GP(0) (the MSR is then unchanged).
 */
static bool cet_wrmsr(CPUX86State *env, uint32_t msr, uint64_t val)
{
    bool ss = env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK;
    bool ibt = env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_CET_IBT;

    if (!ss && !ibt) {
        return true;
    }
    switch (msr) {
    case MSR_IA32_U_CET:
    case MSR_IA32_S_CET: {
        uint64_t valid = (ss ? 0x3ull : 0) | (ibt ? (0x3cull | ~0x3ffull) : 0);

        if ((val & ~valid) || !cet_msr_canonical(env, val) ||   /* U760 */
            ((val & CET_SUPPRESS) && (val & CET_TRACKER))) {
            return false;
        }
        if (msr == MSR_IA32_U_CET) {
            env->u_cet = val;
        } else {
            env->s_cet = val;
        }
        cpu_sync_cet_hflags(env);
        return true;
    }
    case MSR_IA32_INT_SSP_TAB:
        if (ss) {
            if (!cet_msr_canonical(env, val)) {                    /* U760 */
                return false;
            }
            env->int_ssp_table = val;
        }
        return true;
    default: /* IA32_PL0_SSP .. IA32_PL3_SSP */
        if (ss) {
            if ((val & 3) || !cet_msr_canonical(env, val)) {       /* U760 */
                return false;
            }
            env->pl_ssp[msr - MSR_IA32_PL0_SSP] = val;
        }
        return true;
    }
}

#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U756) */
/*
 * NoVmp (ledger U756): XRSTORS loads the CET_U / CET_S MSRs (IA32_U_CET, IA32_PLi_SSP) as
 * WRMSR does (SDM Vol1 13.12: #GP "if it would load any element of that component with
 * an unsupported value"; 18.2.3: "The WRMSR and XRSTORS instructions require the address
 * specified in the IA32_PLx_SSP MSR ... to be 4 byte aligned"). x86_cet_msr_ok applies
 * cet_wrmsr's checks without writing (XRSTORS checks every value before loading any);
 * x86_cet_msr_load is the write.
 */
bool x86_cet_msr_ok(CPUX86State *env, uint32_t msr, uint64_t val)
{
    bool ss = env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK;
    bool ibt = env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_CET_IBT;

    if (msr == MSR_IA32_U_CET || msr == MSR_IA32_S_CET) {
        uint64_t valid = (ss ? 0x3ull : 0) | (ibt ? (0x3cull | ~0x3ffull) : 0);

        return !(val & ~valid) && cet_msr_canonical(env, val) &&
               !((val & CET_SUPPRESS) && (val & CET_TRACKER));
    }
    return !ss || (!(val & 3) && cet_msr_canonical(env, val));
}

void x86_cet_msr_load(CPUX86State *env, uint32_t msr, uint64_t val)
{
    cet_wrmsr(env, msr, val);
}

#endif /* __Use_Original_Qemu (U756) */
#if __Use_Original_Qemu != 1 /* ours (U905) */
/*
 * NoVmp (ledger U905, decision A2): the MSR list of the CPU model. SDM Vol2B RDMSR / Vol2D WRMSR:
 * "#GP(0) If the value in ECX specifies a reserved or unimplemented MSR address" - before, every
 * MSR the emulator does not model read 0 and ignored writes. An MSR exists when SDM Vol4 Table 2-2
 * enumerates it for the model: the CPUID condition printed there (the model's feature word, hidden
 * by a strict UC_CTL_X86_CPUID profile exactly as the translator hides instructions,
 * x86_cpuid_profile_mask) or, for the MSRs Table 2-2 gives only a DisplayFamily_DisplayModel, the
 * vendor and family of the model (Intel, family 6 or 0FH); the model-specific MSR_SMI_COUNT (34H,
 * Tables 2-6/2-20 and later: Nehalem/Silvermont and newer) on Intel family 6 from model 1AH;
 * IA32_VM_HSAVE_PA (C0010117H, AMD APM) with CPUID.80000001H:ECX.SVM. Any other address is
 * #GP(0). A UC_X86_INS_RDMSR / UC_X86_INS_WRMSR hook that returns 1 handles the access itself
 * (no #GP), as before. uc_reg_read / uc_reg_write (UC_X86_REG_MSR) never raise a guest fault:
 * the access is not made and the API returns UC_ERR_EXCEPTION (msr_api_err).
 */
#define MSR_IA32_PLATFORM_ID_NV     0x17
#if __Use_Original_Qemu != 1 /* ours (U1472) */
#define MSR_IA32_BIOS_UPDT_TRIG_NV  0x79
#endif /* __Use_Original_Qemu (U1472) */
#define MSR_IA32_PERF_CTL_NV        0x199
#define MSR_IA32_DEBUGCTL_NV        0x1d9
#define MSR_IA32_FLUSH_CMD_NV       0x10b
#define CPUID_7_0_EDX_L1D_FLUSH_NV  (1U << 28)
#define CPUID_7_0_EBX_TSC_ADJUST_NV (1U << 1)
#define CPUID_7_0_EBX_SGX_NV        (1U << 2)
#define CPUID_7_0_ECX_SGX_LC_NV     (1U << 30)

/* a CPUID feature of the model, hidden by a strict profile (as the translator) */
static bool msr_feat(CPUX86State *env, FeatureWord w, uint32_t bit, uint32_t leaf,
                     uint32_t sub, int reg)
{
    return (env->features[w] & bit) && (x86_cpuid_profile_mask(env, leaf, sub, reg) & bit);
}

#define MF_1EDX(b)  msr_feat(env, FEAT_1_EDX, (b), 1, 0, 3)
#define MF_1ECX(b)  msr_feat(env, FEAT_1_ECX, (b), 1, 0, 2)
#define MF_7EBX(b)  msr_feat(env, FEAT_7_0_EBX, (b), 7, 0, 1)
#define MF_7ECX(b)  msr_feat(env, FEAT_7_0_ECX, (b), 7, 0, 2)
#define MF_7EDX(b)  msr_feat(env, FEAT_7_0_EDX, (b), 7, 0, 3)
#define MF_71EAX(b) msr_feat(env, FEAT_7_1_EAX, (b), 7, 1, 0)
#define MF_71EBX(b) msr_feat(env, FEAT_7_1_EBX, (b), 7, 1, 1)
#define MF_71EDX(b) msr_feat(env, FEAT_7_1_EDX, (b), 7, 1, 3)
#define MF_X1EDX(b) msr_feat(env, FEAT_8000_0001_EDX, (b), 0x80000001, 0, 3)
#define MF_X1ECX(b) msr_feat(env, FEAT_8000_0001_ECX, (b), 0x80000001, 0, 2)
#define MF_D1EAX(b) msr_feat(env, FEAT_XSAVE, (b), 0xd, 1, 0)
#define MF_6EAX(b)  msr_feat(env, FEAT_6_EAX, (b), 6, 0, 0)       /* U1128 */

/*
 * CPUID.80000001H:EDX.LM[29] of the model: a strict profile does not hide it - the engine runs in
 * IA-32e mode whatever the profile says (Unicorn's 64-bit mode sets it in the model, U594)
 */
static bool msr_lm(CPUX86State *env)
{
    return (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) || (env->hflags & HF_LMA_MASK);
}

/* DisplayFamily / DisplayModel of the model (SDM Vol2A CPUID, Figure 3-6) */
static int msr_family(CPUX86State *env)
{
    int f = (env->cpuid_version >> 8) & 0xf;

    return f == 0xf ? f + ((env->cpuid_version >> 20) & 0xff) : f;
}

static int msr_model(CPUX86State *env)
{
    int f = (env->cpuid_version >> 8) & 0xf, m = (env->cpuid_version >> 4) & 0xf;

    return (f == 6 || f == 0xf) ? m | ((env->cpuid_version >> 12) & 0xf0) : m;
}

/* Table 2-2 "06_xxH" / "0F_xxH" MSRs: an Intel P6-family or later model */
static bool msr_intel_p6(CPUX86State *env)
{
    return IS_INTEL_CPU(env) && msr_family(env) >= 6;
}

static bool msr_present(CPUX86State *env, uint32_t msr)
{
    uint32_t nbank = env->mcg_cap & 0xff;

#if __Use_Original_Qemu != 1 /* ours (U1123) */
    /*
     * NoVmp (ledger U1123): the x2APIC MSRs 800H-8FFH exist with CPUID.01H:ECX.x2APIC[21] (SDM Vol3A
     * 13.12.1, Vol4 Table 2-2 "If CPUID.01H:ECX.[21] = 1"), as the CPU reports it (a strict profile
     * that hides x2APIC removes them); which of them are registers is the APIC model's business
     */
    if (msr >= 0x800 && msr <= 0x8ff) {
        return MF_1ECX(CPUID_EXT_X2APIC);
    }
#endif /* __Use_Original_Qemu (U1123) */
#if __Use_Original_Qemu != 1 /* ours (U1261) */
    /* NoVmp (ledger U1261): IA32_SGXLEPUBKEYHASH0-3, IA32_SGX_SVN_STATUS (sgx_model.c) */
    {
        int sgx = x86_sgx_msr_present(env, msr);

        if (sgx >= 0) {
            return sgx != 0;
        }
    }
#endif /* __Use_Original_Qemu (U1261) */
#if __Use_Original_Qemu != 1 /* ours (U1301) */
    /* NoVmp (ledger U1301): the VMX capability MSRs 480H-493H (Vol4 Table 2-2 pp. 2-46 to 2-48) */
    if (msr >= MSR_IA32_VMX_BASIC && msr <= MSR_IA32_VMX_EXIT_CTLS2_NV) {
        return x86_vmx_msr_exists(env, msr);
    }
#endif /* __Use_Original_Qemu (U1301) */
#if __Use_Original_Qemu != 1 /* ours (U1332) */
    /* NoVmp (ledger U1332): IA32_SEAMRR_BASE / IA32_SEAMRR_MASK with IA32_MTRRCAP[15] (seam_model.c) */
    {
        int seam = x86_seam_msr_present(env, msr);

        if (seam >= 0) {
            return seam != 0;
        }
    }
#endif /* __Use_Original_Qemu (U1332) */
    switch (msr) {
    case MSR_IA32_TSC:                      /* 05_01H; Vol3B 18.17: CPUID.01H:EDX.TSC */
        return MF_1EDX(CPUID_TSC);
    case MSR_IA32_PLATFORM_ID_NV:           /* 06_01H */
    case MSR_IA32_PERF_STATUS:              /* 0F_03H */
    case MSR_IA32_PERF_CTL_NV:              /* 0F_03H */
    case MSR_IA32_MISC_ENABLE:              /* no MSR-level condition (Intel) */
    case MSR_IA32_DEBUGCTL_NV:              /* 06_0EH */
        return msr_intel_p6(env);
    case MSR_IA32_UCODE_REV:                /* IA32_BIOS_SIGN_ID, 06_01H (AMD: patch level) */
        return true;
#if __Use_Original_Qemu != 1 /* ours (U1472) */
    case MSR_IA32_BIOS_UPDT_TRIG_NV:        /* IA32_BIOS_UPDT_TRIG (W), 06_01H */
        return msr_intel_p6(env);
#endif /* __Use_Original_Qemu (U1472) */
    case MSR_IA32_APICBASE:                 /* 06_01H, with the local APIC */
        return MF_1EDX(CPUID_APIC);
    case MSR_SMI_COUNT:                     /* model-specific: Nehalem / Silvermont and later */
        return IS_INTEL_CPU(env) && msr_family(env) == 6 && msr_model(env) >= 0x1a;
    case MSR_IA32_FEATURE_CONTROL:          /* "if any one enumeration condition ... holds" */
        return MF_1ECX(CPUID_EXT_VMX) || MF_1ECX(CPUID_EXT_SMX) ||
               MF_7EBX(CPUID_7_0_EBX_SGX_NV) || MF_7ECX(CPUID_7_0_ECX_SGX_LC_NV) ||
               (env->mcg_cap & MCG_LMCE_P);
    case MSR_TSC_ADJUST:                    /* U1670: the MAX model enumerates it (cpu.c) */
        return MF_7EBX(CPUID_7_0_EBX_TSC_ADJUST_NV);
    case MSR_IA32_SPEC_CTRL:                /* IBRS, STIBP, SSBD (leaf 7.2 is not modelled) */
        return MF_7EDX(CPUID_7_0_EDX_SPEC_CTRL) || MF_7EDX(CPUID_7_0_EDX_STIBP) ||
               MF_7EDX(CPUID_7_0_EDX_SPEC_CTRL_SSBD);
    case MSR_IA32_PRED_CMD:
        return MF_7EDX(CPUID_7_0_EDX_SPEC_CTRL);
    case MSR_IA32_FLUSH_CMD_NV:
        return MF_7EDX(CPUID_7_0_EDX_L1D_FLUSH_NV);
    case MSR_IA32_ARCH_CAPABILITIES:
        return MF_7EDX(CPUID_7_0_EDX_ARCH_CAPABILITIES);
    case MSR_IA32_CORE_CAPABILITY:
        return MF_7EDX(CPUID_7_0_EDX_CORE_CAPABILITY);
    case MSR_IA32_TSX_CTRL:
        return MF_7EDX(CPUID_7_0_EDX_ARCH_CAPABILITIES) &&
               (env->features[FEAT_ARCH_CAPABILITIES] & ARCH_CAP_TSX_CTRL_MSR);
    case MSR_IA32_USER_MSR_CTL:
    case MSR_IA32_UARCH_MISC_CTL:           /* modelled with USER_MSR (U103) */
        return MF_71EDX(CPUID_7_1_EDX_USER_MSR);
    case MSR_IA32_BARRIER:
        return MF_71EAX(CPUID_7_1_EAX_MSRLIST);
    case MSR_IA32_UMWAIT_CONTROL:
        return MF_7ECX(CPUID_7_0_ECX_WAITPKG);
    case MSR_MTRRcap:
    case MSR_MTRRdefType:
    case MSR_MTRRfix64K_00000:
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        return MF_1EDX(CPUID_MTRR);
    case MSR_IA32_SYSENTER_CS:
    case MSR_IA32_SYSENTER_ESP:
    case MSR_IA32_SYSENTER_EIP:
        return MF_1EDX(CPUID_SEP);
    case MSR_MCG_CAP:
    case MSR_MCG_STATUS:
        return MF_1EDX(CPUID_MCA);
    case MSR_MCG_CTL:
        return MF_1EDX(CPUID_MCA) && (env->mcg_cap & MCG_CTL_P);
    case MSR_IA32_XFD:
    case MSR_IA32_XFD_ERR:
        return MF_D1EAX(CPUID_D_1_EAX_XFD);
    case MSR_PAT:
        return MF_1EDX(CPUID_PAT);
    case MSR_IA32_U_CET:
    case MSR_IA32_S_CET:
        return MF_7ECX(CPUID_7_0_ECX_CET_SHSTK) || MF_7EDX(CPUID_7_0_EDX_CET_IBT);
    case MSR_IA32_PL0_SSP:
#if __Use_Original_Qemu != 1 /* ours (U1170) */
        /* also IA32_FRED_SSP0: "supported by any processor that enumerates FRED" (Vol3A 8.2.3) */
        if (MF_71EAX(CPUID_7_1_EAX_FRED)) {
            return true;
        }
#endif /* __Use_Original_Qemu (U1170) */
        /* fall through */
    case MSR_IA32_PL1_SSP:
    case MSR_IA32_PL2_SSP:
    case MSR_IA32_PL3_SSP:
    case MSR_IA32_INT_SSP_TAB:
        return MF_7ECX(CPUID_7_0_ECX_CET_SHSTK);
#if __Use_Original_Qemu != 1 /* ours (U1170) */
    /* NoVmp (ledger U1170): the FRED MSRs 1CCH-1D4H with CPUID.(07H,1):EAX.FRED[17] (Vol3A 8.2.1) */
    case MSR_IA32_FRED_RSP0:
    case MSR_IA32_FRED_RSP0 + 1:
    case MSR_IA32_FRED_RSP0 + 2:
    case MSR_IA32_FRED_RSP3:
    case MSR_IA32_FRED_STKLVLS:
    case MSR_IA32_FRED_SSP1:
    case MSR_IA32_FRED_SSP1 + 1:
    case MSR_IA32_FRED_SSP3:
    case MSR_IA32_FRED_CONFIG:
        return MF_71EAX(CPUID_7_1_EAX_FRED);
#endif /* __Use_Original_Qemu (U1170) */
    case MSR_IA32_TSCDEADLINE:
        return MF_1ECX(CPUID_EXT_TSC_DEADLINE_TIMER);
    case MSR_IA32_PKRS:
        return MF_7ECX(CPUID_7_0_ECX_PKS);
    case MSR_IA32_BNDCFGS:
        return MF_7EBX(CPUID_7_0_EBX_MPX);
    case MSR_IA32_PASID:
        return MF_7ECX(CPUID_7_0_ECX_ENQCMD);
    case MSR_IA32_XSS:
        return MF_D1EAX(CPUID_XSAVE_XSAVES);
    case MSR_ARCH_LBR_CTL:
    case MSR_ARCH_LBR_DEPTH:
        return MF_7EDX(CPUID_7_0_EDX_ARCH_LBR);
    case MSR_IA32_HRESET_ENABLE:
        return MF_71EAX(CPUID_7_1_EAX_HRESET);
#if __Use_Original_Qemu != 1 /* ours (U1128) */
    case MSR_IA32_THREAD_FEEDBACK_CHAR:      /* Vol1 Table 21-16: with CPUID.06H:EAX[23] */
    case MSR_IA32_HW_FEEDBACK_THREAD_CONFIG:
        return MF_6EAX(CPUID_6_EAX_THREAD_DIRECTOR);
#endif /* __Use_Original_Qemu (U1128) */
    case MSR_IA32_TSE_CAPABILITY:
        return MF_71EBX(CPUID_7_1_EBX_PBNDKB);
    case MSR_IA32_UINTR_RR:
    case MSR_IA32_UINTR_HANDLER:
    case MSR_IA32_UINTR_STACKADJUST:
    case MSR_IA32_UINTR_MISC:
    case MSR_IA32_UINTR_PD:
    case MSR_IA32_UINTR_TT:
        return MF_7EDX(CPUID_7_0_EDX_UINTR);
    case MSR_EFER:                          /* CPUID.80000001H:EDX[20] || EDX[29] */
        return MF_X1EDX(CPUID_EXT2_NX) || msr_lm(env);
#ifdef TARGET_X86_64
    case MSR_STAR:
    case MSR_LSTAR:
    case MSR_CSTAR:
    case MSR_FMASK:
    case MSR_FSBASE:
    case MSR_GSBASE:
    case MSR_KERNELGSBASE:
        return msr_lm(env);
#endif
    case MSR_TSC_AUX:
        return MF_X1EDX(CPUID_EXT2_RDTSCP) || MF_7ECX(CPUID_7_0_ECX_RDPID);
    case MSR_VM_HSAVE_PA:
        return MF_X1ECX(CPUID_EXT3_SVM);
#if __Use_Original_Qemu != 1 /* ours (U1060) */
    /*
     * NoVmp (ledger U1060): the TME / TME-MK MSRs of U1020. SDM Vol4 Table 2-2 981H-984H: "If
     * CPUID.07H.00H:ECX[13] = 1"; 87H has no comment-column condition there (its fields are
     * "supported on all parts that enumerate support for Intel TME-MK"), MKTME spec 336907-007
     * 4.1.1: "CPUID.TME ... enumerates the existence of these five architectural MSRs" (981H-984H
     * and 87H). The model's IA32_TME_CAPABILITY enumerates TME-MK (MK_TME_MAX_KEYID_BITS = 6),
     * so all five follow TME_EN, as U1020 / U1022 gate them.
     */
    case MSR_IA32_MKTME_KEYID_PARTITIONING:
    case MSR_IA32_TME_CAPABILITY:
    case MSR_IA32_TME_ACTIVATE:
    case MSR_IA32_TME_EXCLUDE_MASK:
    case MSR_IA32_TME_EXCLUDE_BASE:
        return MF_7ECX(CPUID_7_0_ECX_TME);
#endif /* __Use_Original_Qemu (U1060) */
    default:
        break;
    }
    if (msr >= MSR_MTRRphysBase(0) && msr <= MSR_MTRRphysMask(MSR_MTRRcap_VCNT - 1)) {
        return MF_1EDX(CPUID_MTRR);
    }
    if (msr >= MSR_MTRRfix4K_C0000 && msr <= MSR_MTRRfix4K_F8000) {
        return MF_1EDX(CPUID_MTRR);
    }
    if (msr >= MSR_MC0_CTL && msr < MSR_MC0_CTL + 4 * nbank) {
        return MF_1EDX(CPUID_MCA);           /* "If IA32_MCG_CAP.CNT > i" */
    }
    if ((msr >= MSR_ARCH_LBR_FROM_0 && msr < MSR_ARCH_LBR_FROM_0 + ARCH_LBR_NR_ENTRIES) ||
        (msr >= MSR_ARCH_LBR_TO_0 && msr < MSR_ARCH_LBR_TO_0 + ARCH_LBR_NR_ENTRIES) ||
        (msr >= MSR_ARCH_LBR_INFO_0 && msr < MSR_ARCH_LBR_INFO_0 + ARCH_LBR_NR_ENTRIES)) {
        return MF_7EDX(CPUID_7_0_EDX_ARCH_LBR);
    }
    return false;
}

#endif /* __Use_Original_Qemu (U905) */
#if __Use_Original_Qemu != 1 /* ours (U906) */
/*
 * NoVmp (ledger U906, decision A2): the values WRMSR refuses (SDM Vol2D WRMSR: "#GP(0) If the value
 * in EDX:EAX sets bits that are reserved in the MSR specified by ECX", "If the source register
 * contains a non-canonical address and ECX specifies one of the following MSRs: IA32_DS_AREA,
 * IA32_FS_BASE, IA32_GS_BASE, IA32_KERNEL_GS_BASE, IA32_LSTAR, IA32_SYSENTER_EIP,
 * IA32_SYSENTER_ESP"; canonical = CPU canonical, Vol3A 4.5.3), checked before anything changes:
 *  - read-only MSRs (Table 2-2 "R/O"): IA32_PLATFORM_ID, MSR_SMI_COUNT, IA32_MTRRCAP (Vol3A
 *    14.11.1 "#GP"), IA32_PERF_STATUS, IA32_ARCH_CAPABILITIES, IA32_CORE_CAPABILITIES and
 *    IA32_MCG_CAP (Vol3B: "the effect of writing ... is undefined" - our choice: #GP like the
 *    other R/O MSRs); the write-only IA32_PRED_CMD / IA32_FLUSH_CMD are #GP for RDMSR (not
 *    stated by the SDM; our choice);
 *  - IA32_BIOS_SIGN_ID 31:0, IA32_APIC_BASE 7:0, 9, 10 (without x2APIC) and 63:MAXPHYADDR,
 *    IA32_FEATURE_CONTROL (bits of absent features; any write once Lock = 1), IA32_SPEC_CTRL
 *    (IBRS/STIBP/SSBD as enumerated; the leaf 7.2 bits are not modelled), IA32_PRED_CMD /
 *    IA32_FLUSH_CMD 63:1, IA32_TSX_CTRL 63:2, IA32_PERF_CTL 63:16 (bit 32 is mobile-only),
 *    IA32_DEBUGCTL (5:3, 63:16 and the bits of absent features), IA32_MCG_STATUS (11:4, 63:13,
 *    LMCE_S / SEAM_NR without MCG_CAP[27] / [12]), IA32_MCi_STATUS / ADDR / MISC (Vol3B 16.3.2:
 *    software may write only zeros), the MTRRs and IA32_PAT (Vol3A 14.11: memory types 0, 1, 4,
 *    5, 6 (+ 7 = UC- in the PAT), reserved bits, bits at or above MAXPHYADDR), IA32_EFER (7:1,
 *    9, 63:12 and the bits of absent features; LMA is read-only and ignored; LME cannot change
 *    while CR0.PG = 1, Vol3A 5.8.5), IA32_FMASK 63:32 and IA32_TSC_AUX 63:32 (Vol3A Figure 6-14
 *    / Vol3B 18.17.2: 32-bit fields);
 *  - canonical: IA32_SYSENTER_ESP/EIP, IA32_LSTAR, IA32_FS_BASE, IA32_GS_BASE,
 *    IA32_KERNEL_GS_BASE.
 * Not checked (the SDM states no #GP): IA32_STAR (Figure 6-14 prints 31:0 "Reserved", but the
 * field is AMD's legacy SYSCALL EIP and no #GP is stated), IA32_CSTAR, IA32_SYSENTER_CS (31:16
 * "can be read and written", 63:32 "writes ignored"), IA32_MISC_ENABLE (per-bit, model-specific
 * conditions), IA32_MCG_CTL / IA32_MCi_CTL (implementation-specific values). The MSRs with checks
 * of their own (IA32_XSS, IA32_XFD, IA32_PKRS, CET, UINTR, IA32_BNDCFGS, ...) keep them; an API
 * access now gets UC_ERR_EXCEPTION from them too.
 */
static bool msr_memtype_ok(uint64_t v, bool pat)
{
    int i;

    for (i = 0; i < 8; i++) {
        uint8_t t = v >> (8 * i);
        if (t == 2 || t == 3 || t > (pat ? 7 : 6)) {
            return false;
        }
    }
    return true;
}

#if __Use_Original_Qemu != 1 /* ours (U1780) */
/*
 * NoVmp (ledger U1780, plan 1.J.63): the IA32_EFER bits WRMSR may set, from CPUID as the active strict
 * UC_CTL_X86_CPUID profile reports it (msr_feat: the model's feature AND the profile's bit, as the MSR list). WRMSR
 * #GP(0) "If the value in EDX:EAX sets bits that are reserved in the MSR specified by ECX" (SDM 334569-093 Vol2D
 * WRMSR p. 6-9); IA32_EFER (Vol4 335592-093 Table 2-2 C000_0080H p. 2-93; Vol3A 253668-093 2.2.1 Table 2-1 p. 2-9):
 * 0 SCE (R/W), 7:1 reserved, 8 LME (R/W), 9 reserved, 10 LMA (R), 11 NXE (R/W), 63:12 reserved; CPUID (ISE
 * 319433-062, 80000001H): EDX.SYSCALL_SYSRET_64[11] -> SCE, EDX.EXECUTE_DIS[20] -> NXE, EDX.INTEL64[29] -> LME
 * (msr_lm: the engine runs in IA-32e mode whatever the profile says, U594). The AMD bits (APM Vol2 24593 r3.45
 * 3.1.7, secondary reference): SVME (12) with CPUID 8000_0001H ECX.SVM[2], FFXSR (14) with EDX.FFXSR[25]; LMSLE,
 * TCE and AutoIBRS are not modelled (reserved). Before: SCE / NXE / SVME / FFXSR from the model's feature words
 * alone, so with UC_CTL_X86_APX or a strict profile that hides VMX (no VMX model: the MAX model keeps SVM, U1301 /
 * U1660) or an AMD CPU model under Intel rows, WRMSR set EFER.SVME (VMRUN & co. no longer #UD) although the
 * profile reports CPUID.80000001H:ECX.SVM = 0. LMA is not part of it (read-only: msr_write_ok allows any LMA value
 * and helper_wrmsr keeps the current one, as before).
 */
uint64_t x86_efer_writable(CPUX86State *env)
{
    return (MF_X1EDX(CPUID_EXT2_SYSCALL) ? MSR_EFER_SCE : 0) |
           (msr_lm(env) ? MSR_EFER_LME : 0) |
           (MF_X1EDX(CPUID_EXT2_NX) ? MSR_EFER_NXE : 0) |
           (MF_X1ECX(CPUID_EXT3_SVM) ? MSR_EFER_SVME : 0) |
           (MF_X1EDX(CPUID_EXT2_FFXSR) ? MSR_EFER_FFXSR : 0);
}

/*
 * The CPUID-qualified IA32_EFER bits (SCE, NXE, SVME, FFXSR) WRMSR may not set under the active strict profile; 0
 * without one or with a non-strict one (the model decides, unchanged). unicorn.c drops them when such a profile is
 * installed (x86_cpuid_changed) and leaves SCE out of the 64-bit reset value (reg_reset, U594).
 */
uint64_t x86_efer_profile_hidden(CPUX86State *env)
{
    if (!env->uc || !env->uc->x86_cpuid_strict || !env->uc->x86_cpuid_count) {
        return 0;
    }
    return (MSR_EFER_SCE | MSR_EFER_NXE | MSR_EFER_SVME | MSR_EFER_FFXSR) & ~x86_efer_writable(env);
}

#endif /* __Use_Original_Qemu (U1780) */
static bool msr_write_ok(CPUX86State *env, uint32_t msr, uint64_t val)
{
    /*
     * U1431: bits 63:MAXPHYADDR of the MTRR PhysBase / PhysMask fields with the effective
     * MAXPHYADDR (x86_maxphyaddr: reduced by IA32_TME_ACTIVATE[39:36] once TME is locked outside
     * SEAM; SDM 093 Vol3A 14.11.2.3 p. 14-24, fields MAXPHYADDR-1:12 and footnote 1). U1432:
     * IA32_APIC_BASE 63:MAXAPICADDR (x86_maxapicaddr; WRMSR reaches apic_write_base first, this is
     * the same rule for the paths that do not). Was: phys_bits for both (U1063).
     */
    uint64_t phys_mask = ~((1ULL << x86_maxphyaddr(env)) - 1);
    uint64_t apic_mask = ~((1ULL << x86_maxapicaddr(env)) - 1);
    uint64_t valid;

#if __Use_Original_Qemu != 1 /* ours (U1261) */
    /* NoVmp (ledger U1261): the SGX MSRs' write permission (sgx_model.c, Vol4 8CH-8FH / 500H) */
    {
        int sgx = x86_sgx_msr_write_ok(env, msr, val);

        if (sgx >= 0) {
            return sgx != 0;
        }
    }
#endif /* __Use_Original_Qemu (U1261) */
#if __Use_Original_Qemu != 1 /* ours (U1332) */
    /* NoVmp (ledger U1332): reserved bits, KeyID bits, VALID, LOCK of the SEAM range registers */
    {
        int seam = x86_seam_msr_write_ok(env, msr, val);

        if (seam >= 0) {
            return seam != 0;
        }
    }
#endif /* __Use_Original_Qemu (U1332) */
#if __Use_Original_Qemu != 1 /* ours (U1301) */
    /* the VMX capability MSRs are read-only: WRMSR #GP(0) (Vol3D Appendix A p. A-1) */
    if (msr >= MSR_IA32_VMX_BASIC && msr <= MSR_IA32_VMX_EXIT_CTLS2_NV) {
        return false;
    }
#endif /* __Use_Original_Qemu (U1301) */
    switch (msr) {
    case MSR_IA32_PLATFORM_ID_NV:
    case MSR_SMI_COUNT:
    case MSR_MTRRcap:
    case MSR_IA32_PERF_STATUS:
    case MSR_IA32_ARCH_CAPABILITIES:
    case MSR_IA32_CORE_CAPABILITY:
    case MSR_MCG_CAP:
        return false;
    case MSR_IA32_UCODE_REV:
        return !(val & 0xffffffffULL);
    case MSR_IA32_APICBASE:
        valid = MSR_IA32_APICBASE_BSP | MSR_IA32_APICBASE_ENABLE |
                (MF_1ECX(CPUID_EXT_X2APIC) ? MSR_IA32_APICBASE_EXTD : 0) |
                (~apic_mask & ~0xfffULL);
        return !(val & ~valid);
    case MSR_IA32_FEATURE_CONTROL:
        valid = FEATURE_CONTROL_LOCKED |
                ((MF_1ECX(CPUID_EXT_VMX) && MF_1ECX(CPUID_EXT_SMX)) ? 2 : 0) |
                (MF_1ECX(CPUID_EXT_VMX) ? FEATURE_CONTROL_VMXON_ENABLED_OUTSIDE_SMX : 0) |
                (MF_1ECX(CPUID_EXT_SMX) ? 0xff00 : 0) |
                (MF_7ECX(CPUID_7_0_ECX_SGX_LC_NV) ? (1ULL << 17) : 0) |
                (MF_7EBX(CPUID_7_0_EBX_SGX_NV) ? (1ULL << 18) : 0) |
                ((env->mcg_cap & MCG_LMCE_P) ? FEATURE_CONTROL_LMCE : 0);
        return !(env->msr_ia32_feature_control & FEATURE_CONTROL_LOCKED) && !(val & ~valid);
    case MSR_IA32_SPEC_CTRL:
        valid = (MF_7EDX(CPUID_7_0_EDX_SPEC_CTRL) ? 1 : 0) |
                (MF_7EDX(CPUID_7_0_EDX_STIBP) ? 2 : 0) |
                (MF_7EDX(CPUID_7_0_EDX_SPEC_CTRL_SSBD) ? 4 : 0);
        return !(val & ~valid);
    case MSR_IA32_PRED_CMD:
    case MSR_IA32_FLUSH_CMD_NV:
        return !(val & ~1ULL);
    case MSR_IA32_TSX_CTRL:
        return !(val & ~3ULL);
    case MSR_IA32_PERF_CTL_NV:
        return !(val & ~0xffffULL);
    case MSR_IA32_DEBUGCTL_NV:
        valid = 0x1 | 0x2 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x2000 |
                (MF_7ECX(1U << 24) ? 0x4 : 0) |                    /* bus-lock detection */
                (MF_7EBX(CPUID_7_0_EBX_RTM) ? 0x8000 : 0);         /* RTM_DEBUG */
        {
            uint32_t a, b, c, d;
            cpu_x86_cpuid(env, 0xa, 0, &a, &b, &c, &d);
            if (MF_1ECX(CPUID_EXT_PDCM) && (a & 0xff) > 1) {
                valid |= 0x1800;                                    /* FREEZE_*_ON_PMI */
            }
        }
        return !(val & ~valid);
    case MSR_MCG_STATUS:
        valid = 0x7 | ((env->mcg_cap & MCG_LMCE_P) ? 0x8 : 0) |
                ((env->mcg_cap & (1ULL << 12)) ? 0x1000 : 0);
        return !(val & ~valid);
    case MSR_MTRRdefType:
        return !(val & ~0xcffULL) && msr_memtype_ok(val & 0xff, false);
    case MSR_MTRRfix64K_00000:
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        return msr_memtype_ok(val, false);
    case MSR_PAT:
        return msr_memtype_ok(val, true);
    case MSR_EFER:
        /* U1780 (plan 1.J.63): the bits the strict profile enumerates (was: the model's feature words) */
        valid = MSR_EFER_LMA | x86_efer_writable(env);
        if (val & ~valid) {
            return false;
        }
        return !((val ^ env->efer) & MSR_EFER_LME) || !(env->cr[0] & CR0_PG_MASK);
#ifdef TARGET_X86_64
    case MSR_LSTAR:
    case MSR_FSBASE:
    case MSR_GSBASE:
    case MSR_KERNELGSBASE:
        return novmp_canonical(env, val);
    case MSR_FMASK:
        return !(val >> 32);
#endif
    case MSR_IA32_SYSENTER_ESP:
    case MSR_IA32_SYSENTER_EIP:
        return novmp_canonical(env, val);
    case MSR_TSC_AUX:
        return !(val >> 32);
    /*
     * the MSRs whose own checks live in helper_wrmsr's switch (U103, U104, U111, U112, U114, U173,
     * U727, U783, U802, U804, U807): the same rules here, so that an API access gets
     * UC_ERR_EXCEPTION and never reaches a guest #GP raised outside translated code
     */
    case MSR_IA32_XSS:
        /* U1581: the model's bits, or a strict profile's CPUID.(0DH,1):EDX:ECX of the engine's components (cpu.c) */
        return !(val & ~x86_cpu_xss_valid(env));
    case MSR_IA32_XFD:
    case MSR_IA32_XFD_ERR:
        return !(val & ~x86_cpu_xfd_supported(env));
    case MSR_IA32_PKRS:
        return !(val >> 32);
    case MSR_IA32_USER_MSR_CTL:
        return !(val & 0xffe) && novmp_canonical(env, val);
    case MSR_IA32_UARCH_MISC_CTL:
        return !(val & ~1ULL);
    case MSR_IA32_BARRIER:
    case MSR_IA32_TSE_CAPABILITY:
        return false;
    case MSR_IA32_HRESET_ENABLE:
        return !(val & ~(uint64_t)CPUID_20_0_EBX_THREAD_DIRECTOR_HRESET);
#if __Use_Original_Qemu != 1 /* ours (U1128) */
    case MSR_IA32_THREAD_FEEDBACK_CHAR:      /* R/O (Vol3B 17.6.10, Vol4) */
        return false;
    case MSR_IA32_HW_FEEDBACK_THREAD_CONFIG: /* 63:1 reserved */
        return !(val & ~1ULL);
#endif /* __Use_Original_Qemu (U1128) */
    case MSR_IA32_UMWAIT_CONTROL:
        return !(val & ~0xfffffffdull);
    case MSR_IA32_PASID:
        return !(val & ~0x800fffffull);
    case MSR_IA32_BNDCFGS:
        return !(val & 0xffc) && novmp_canonical(env, val);
    case MSR_IA32_U_CET:
    case MSR_IA32_S_CET:
    case MSR_IA32_PL0_SSP:
    case MSR_IA32_PL1_SSP:
    case MSR_IA32_PL2_SSP:
    case MSR_IA32_PL3_SSP:
#if __Use_Original_Qemu != 1 /* ours (U1170) */
        /* IA32_FRED_SSP0 without CET: bits 1:0 reserved, CPU canonical (Vol3A 8.2.3) */
        if (msr == MSR_IA32_PL0_SSP && !(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK)) {
            return !(val & 3) && novmp_canonical(env, val);
        }
#endif /* __Use_Original_Qemu (U1170) */
        return x86_cet_msr_ok(env, msr, val);
    case MSR_IA32_INT_SSP_TAB:
        return novmp_canonical(env, val);
#if __Use_Original_Qemu != 1 /* ours (U1170) */
    /*
     * NoVmp (ledger U1170): SDM 092 Vol3A 8.2.3 - "A write to this MSR ... causes a #GP if its operand
     * sets any reserved bits or if it is not CPU canonical": IA32_FRED_CONFIG (reserved 2, 5:4, 11),
     * IA32_FRED_RSPi (bits 5:0 must be 0: 64-byte aligned), IA32_FRED_SSPi, i = 1..3 (bits 2:0
     * reserved). IA32_FRED_STKLVLS has no reserved bit (32 2-bit fields).
     */
    case MSR_IA32_FRED_CONFIG:
        return !(val & FRED_CONFIG_RESERVED) && novmp_canonical(env, val);
    case MSR_IA32_FRED_RSP0:
    case MSR_IA32_FRED_RSP0 + 1:
    case MSR_IA32_FRED_RSP0 + 2:
    case MSR_IA32_FRED_RSP3:
        return !(val & 0x3f) && novmp_canonical(env, val);
    case MSR_IA32_FRED_SSP1:
    case MSR_IA32_FRED_SSP1 + 1:
    case MSR_IA32_FRED_SSP3:
        return !(val & 7) && novmp_canonical(env, val);
    case MSR_IA32_FRED_STKLVLS:
        return true;
#endif /* __Use_Original_Qemu (U1170) */
    case MSR_IA32_UINTR_HANDLER:
    case MSR_IA32_UINTR_STACKADJUST:
        return novmp_canonical(env, val);
    case MSR_IA32_UINTR_MISC:
        return !(val >> 40);
    case MSR_IA32_UINTR_PD:
        return novmp_canonical(env, val) && !(val & 0x3f);
    case MSR_IA32_UINTR_TT:
        return novmp_canonical(env, val & ~0xfULL) && !(val & 0xe);
    default:
        break;
    }
    if (msr >= MSR_MTRRphysBase(0) && msr <= MSR_MTRRphysMask(MSR_MTRRcap_VCNT - 1)) {
        if (msr & 1) {
            return !(val & (0x7ffULL | phys_mask));                     /* PHYSMASK */
        }
        return !(val & (0xf00ULL | phys_mask)) && msr_memtype_ok(val & 0xff, false);
    }
    if (msr >= MSR_MTRRfix4K_C0000 && msr <= MSR_MTRRfix4K_F8000) {
        return msr_memtype_ok(val, false);
    }
    if (msr >= MSR_MC0_CTL && msr < MSR_MC0_CTL + 4 * (env->mcg_cap & 0xff) && (msr & 3)) {
        return val == 0;                                                /* STATUS / ADDR / MISC */
    }
    return true;
}

#endif /* __Use_Original_Qemu (U906) */
#if __Use_Original_Qemu != 1 /* ours (U905) */
/*
 * The access check of RDMSR / WRMSR (after the UC_HOOK_INSN hooks): false = no access. A guest
 * access raises #GP(0); an API access (msr_api) sets msr_api_err instead.
 */
static bool msr_access_ok(CPUX86State *env, uint32_t msr, bool write, uint64_t val,
                          uintptr_t ra)
{
    /* U906: the value WRMSR refuses; RDMSR of a write-only MSR (U1472: + IA32_BIOS_UPDT_TRIG,
       Vol4 79H "(W)" - our choice, as for IA32_PRED_CMD / IA32_FLUSH_CMD) */
    if (msr_present(env, msr) &&
        (write ? msr_write_ok(env, msr, val)
               : (msr != MSR_IA32_PRED_CMD && msr != MSR_IA32_FLUSH_CMD_NV
#if __Use_Original_Qemu != 1 /* ours (U1472) */
                  && msr != MSR_IA32_BIOS_UPDT_TRIG_NV
#endif /* __Use_Original_Qemu (U1472) */
                  ))) {
        return true;
    }
#if __Use_Original_Qemu != 1 /* ours (U907) */
    {
        X86Pmu p;

        x86_pmu(env, &p);
        if (pmu_msr(env, msr, &p) && (!write || pmu_write_ok(env, msr, val, &p))) {
            return true;
        }
    }
#endif /* __Use_Original_Qemu (U907) */
    if (env->msr_api) {
        env->msr_api_err = 1;
        return false;
    }
    raise_exception_ra(env, EXCP0D_GPF, ra);
    return false;
}

#endif /* __Use_Original_Qemu (U905) */
#if __Use_Original_Qemu != 1 /* ours (U1303) */
/*
 * NoVmp (ledger U1303, docs/vmx.md): MSR access for the VMX transitions (vmx_helper.c).
 * x86_efer_value_ok: the reserved bits of IA32_EFER only (VM-entry / host-state checks, SDM 092
 * Vol3C 29.2.2 / 29.3.1.1: "bits reserved in the IA32_EFER MSR must be 0"; not the LME-with-paging
 * rule of WRMSR, which concerns the current state). x86_msr_value_ok: "one that could be written by
 * WRMSR without fault at CPL 0" for the current state. x86_msr_vmx_access: RDMSR / WRMSR as the
 * MSR-load / -store areas perform them (29.4, 30.4, 30.6) - no UC_HOOK_INSN hook, a #GP(0) becomes
 * false (msr_api = 3: the live CPU, like the host API, without its hooks).
 */
bool x86_efer_value_ok(CPUX86State *env, uint64_t efer)
{
    return msr_write_ok(env, MSR_EFER, (efer & ~MSR_EFER_LME) | (env->efer & MSR_EFER_LME)) ||
           (!(env->cr[0] & CR0_PG_MASK) && msr_write_ok(env, MSR_EFER, efer));
}

bool x86_msr_value_ok(CPUX86State *env, uint32_t msr, uint64_t val)
{
    return msr_present(env, msr) && msr_write_ok(env, msr, val);
}

bool x86_msr_vmx_access(CPUX86State *env, uint32_t msr, uint64_t *val, bool write)
{
    uint64_t ecx = env->regs[R_ECX], eax = env->regs[R_EAX], edx = env->regs[R_EDX];
    uint8_t api = env->msr_api, err = env->msr_api_err;
    bool ok;

    env->regs[R_ECX] = msr;
    env->regs[R_EAX] = (uint32_t)*val;
    env->regs[R_EDX] = (uint32_t)(*val >> 32);
    env->msr_api = 3;
    env->msr_api_err = 0;
    if (write) {
        helper_wrmsr(env);
    } else {
        helper_rdmsr(env);
        *val = (uint32_t)env->regs[R_EAX] | ((uint64_t)(uint32_t)env->regs[R_EDX] << 32);
    }
    ok = !env->msr_api_err;
    env->msr_api = api;
    env->msr_api_err = err;
    env->regs[R_ECX] = ecx;
    env->regs[R_EAX] = eax;
    env->regs[R_EDX] = edx;
    return ok;
}

#endif /* __Use_Original_Qemu (U1303) */
#if __Use_Original_Qemu != 1 /* ours (U1120) */
/*
 * NoVmp (ledger U1120): IA32_TSE_CAPABILITY as the CPU has it: NOVMP_TSE_CAPABILITY with the
 * UC_X86_FEATURE_TSE opt-in, else 0 (U807: no TSE engine). The PCONFIG TSE leaves read it too;
 * when the MSR does not exist (PBNDKB not in the model or hidden by a strict profile, SDM Vol4
 * 9F1H "If CPUID.07H.01H:EBX.PBNDKB[1] = 1") they see 0 - our choice: the SDM does not tie the
 * TSE target of CPUID.1BH to PBNDKB (tse audit 2026-10-10 D5).
 */
/*
 * U1129: the opt-in value keeps the architectural layout (ISE 319433-060 6.2.2 Table 6-1 p. 6-2,
 * SDM 092/093 Vol4 9F1H p. 2-69): reserved bits 35:24 and 63:51 zero, exactly one algorithm bit in
 * 15:0, no key source in 23:16 (no encoding is defined), TSE_MAX_KEYS within 50:36.
 */
typedef char novmp_tse_cap_rsvd_35_24[((NOVMP_TSE_CAPABILITY >> 24) & 0xfff) == 0 ? 1 : -1];
typedef char novmp_tse_cap_rsvd_63_51[(NOVMP_TSE_CAPABILITY >> 51) == 0 ? 1 : -1];
typedef char novmp_tse_cap_keysrc[((NOVMP_TSE_CAPABILITY >> 16) & 0xff) == 0 ? 1 : -1];
typedef char novmp_tse_cap_alg[(NOVMP_TSE_CAPABILITY & 0xffff) == 1 ? 1 : -1];
typedef char novmp_tse_cap_max[NOVMP_TSE_MAX_KEYS <= 0x7fff ? 1 : -1];

static uint64_t x86_tse_capability(CPUX86State *env)
{
    if (!env->uc || !(env->uc->x86_features & UC_X86_FEATURE_TSE) ||
        !MF_71EBX(CPUID_7_1_EBX_PBNDKB)) {
        return 0;
    }
    return NOVMP_TSE_CAPABILITY;
}

#endif /* __Use_Original_Qemu (U1120) */
void helper_wrmsr(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    uint64_t val;
    uc_engine *uc = env->uc;
    struct hook *hook;
    int skip_wrmsr = 0;
    bool synced = false;
#if __Use_Original_Qemu != 1 /* ours (U905) */
    uintptr_t wrmsr_ra = GETPC();
#endif /* __Use_Original_Qemu (U905) */

    cpu_svm_check_intercept_param(env, SVM_EXIT_MSR, 1, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* VMX non-root operation: the MSR bitmaps (Vol3C 28.1.3); an instruction's own access only */
    if (env->msr_api == 0 && !env->msr_swap) {
        x86_vmx_msr_check(env, (uint32_t)env->regs[R_ECX], true, VMX_EXIT_WRMSR, 0, GETPC());
    }
#endif /* __Use_Original_Qemu (U1304) */

    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN)
    {
#if __Use_Original_Qemu != 1 /* ours (U878) */
        if (env->msr_api == 2 || env->msr_api == 3) {
            break;      /* a write into a uc_context image runs no WRMSR hook (U878); U1303: nor
                           the MSR-load area of a VMX transition */
        }
#endif /* __Use_Original_Qemu (U878) */
        if (hook->to_delete)
            continue;
        if (!HOOK_BOUND_CHECK(hook, env->eip))
            continue;
        if (hook->insn == UC_X86_INS_WRMSR) {
            uintptr_t pc = GETPC();
            if (!synced && !uc->skip_sync_pc_on_exit && pc) {
                cpu_restore_state(uc->cpu, pc, false);
                synced = true;
            }
            JIT_CALLBACK_GUARD_VAR(
                skip_wrmsr,
                ((uc_cb_insn_cpuid_t)hook->callback)(env->uc, hook->user_data));
        }
        if (env->uc->stop_request)
            break;
    }

    if (skip_wrmsr)
        return;
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* VMX non-root operation: WRMSR 79H loads no microcode update; no fault (Vol3C 28.3) */
    if (env->vmx.nonroot && env->msr_api == 0 && (uint32_t)env->regs[R_ECX] == 0x79) {
        return;
    }
#endif /* __Use_Original_Qemu (U1304) */

    val = ((uint32_t)env->regs[R_EAX]) |
        ((uint64_t)((uint32_t)env->regs[R_EDX]) << 32);

#if __Use_Original_Qemu != 1 /* ours (U1307) */
    /* VMX non-root operation: "virtualize x2APIC mode" (Vol3C 32.5), also without x2APIC mode */
    if (env->msr_api == 0 &&
        x86_vmx_x2apic_wrmsr(env, (uint32_t)env->regs[R_ECX], val, wrmsr_ra)) {
        return;
    }
#endif /* __Use_Original_Qemu (U1307) */
#if __Use_Original_Qemu != 1 /* ours (U960) */
    /* IA32_APIC_BASE and the x2APIC MSRs 800H-8FFH: the local APIC (apic_model.c). U1123: only
       when the MSR exists for the CPU as reported (msr_present: 1BH with CPUID.01H:EDX.APIC,
       800H-8FFH with CPUID.01H:ECX.x2APIC, a strict profile counts), else the U905 refusal below
       (#GP(0), UC_ERR_EXCEPTION for the API); was: the APIC handler ran first and ignored a
       profile that hides APIC / x2APIC */
    if (msr_present(env, (uint32_t)env->regs[R_ECX]) &&
        x86_apic_msr_write(env, (uint32_t)env->regs[R_ECX], val, GETPC())) {
        return;
    }
#endif /* __Use_Original_Qemu (U960) */
#if __Use_Original_Qemu != 1 /* ours (U905) */
    if (!msr_access_ok(env, (uint32_t)env->regs[R_ECX], true, val, wrmsr_ra)) {
        return;
    }
#endif /* __Use_Original_Qemu (U905) */
    switch ((uint32_t)env->regs[R_ECX]) {
#if __Use_Original_Qemu != 1 /* ours (U905) */
    /* NoVmp (ledger U905): MSRs of Table 2-2 the emulator did not model (stored, no side effect) */
    case MSR_IA32_TSC:
#if __Use_Original_Qemu != 1 /* ours (U1671) */
        msr_tsc_move(env, val - msr_tsc_now(env));
#endif /* __Use_Original_Qemu (U1671) */
        break;
    case MSR_TSC_ADJUST:
#if __Use_Original_Qemu != 1 /* ours (U1671) */
        msr_tsc_move(env, val - env->tsc_adjust);
#endif /* __Use_Original_Qemu (U1671) */
        break;
    case MSR_IA32_FEATURE_CONTROL:
        env->msr_ia32_feature_control = val;
        break;
    case MSR_IA32_SPEC_CTRL:
        env->spec_ctrl = val;
        break;
    case MSR_IA32_TSX_CTRL:
        env->tsx_ctrl = (uint32_t)val;
        break;
    case MSR_IA32_PERF_CTL_NV:
        env->msr_perf_ctl = val;
        break;
    case MSR_IA32_DEBUGCTL_NV:
        env->msr_debugctl = val;
        break;
    case MSR_IA32_PLATFORM_ID_NV:
    case MSR_IA32_PRED_CMD:             /* IBPB: no prediction state is kept */
    case MSR_IA32_FLUSH_CMD_NV:         /* L1D_FLUSH: no cache is modelled */
    case MSR_IA32_TSCDEADLINE:          /* no local APIC: never in TSC-deadline mode */
        break;
    case MSR_IA32_UCODE_REV:
#if __Use_Original_Qemu != 1 /* ours (U1471) */
        /*
         * NoVmp (ledger U1471): IA32_BIOS_SIGN_ID is R/W (SDM Vol4 8BH); software preloads it
         * (normally with 0) and CPUID deposits the update signature (Vol2A CPUID Operation,
         * Vol3A 12.11.7). Bits 31:0 are reserved (a non-zero value is #GP, U906). AMD's 8BH
         * (patch level) stays as before: the write has no effect.
         */
        if (IS_INTEL_CPU(env)) {
            env->msr_bios_sign_id = val;
        }
#endif /* __Use_Original_Qemu (U1471) */
        break;
#if __Use_Original_Qemu != 1 /* ours (U1472) */
    case MSR_IA32_BIOS_UPDT_TRIG_NV:
        /*
         * NoVmp (ledger U1472), emulator policy (docs/quirks.md "Microcode update trigger"):
         * SDM Vol4 79H "Executing a WRMSR instruction to this MSR causes a microcode update to be
         * loaded into the processor" (Vol3A 12.11.6). The emulator has no microcode and does not
         * model update loading: a privileged WRMSR 79H is accepted and has no effect - no update
         * data is read (no memory access, no #PF), nothing is authenticated or loaded, the active
         * revision (IA32_BIOS_SIGN_ID reporting, UC_CTL_X86_MICROCODE) and the CPU profile do not
         * change; to software this looks like an update the processor did not accept (Vol3A
         * Example 12-10: the signature after the load is compared, a failed load is no fault).
         * EDX:EAX is the update's linear address; the SDM defines no reserved bits and no #GP for
         * this MSR's value (Vol2D WRMSR's canonical check names other MSRs). CPL > 0 stays #GP(0)
         * (translator), CPUs that do not have 79H (not Intel family 6+) stay #GP(0) (msr_present),
         * a UC_X86_INS_WRMSR hook that returns 1 still handles the write itself.
         */
        break;
#endif /* __Use_Original_Qemu (U1472) */
#endif /* __Use_Original_Qemu (U905) */
    case MSR_IA32_SYSENTER_CS:
#if __Use_Original_Qemu == 1 /* original QEMU (U906) */
        env->sysenter_cs = val & 0xffff;
#else /* ours (U906) */
        /* SDM Vol4: 15:0 selector, 31:16 "can be read and written", 63:32 writes ignored */
        env->sysenter_cs = (uint32_t)val;
#endif /* __Use_Original_Qemu (U906) */
        break;
    case MSR_IA32_SYSENTER_ESP:
        env->sysenter_esp = val;
        break;
    case MSR_IA32_SYSENTER_EIP:
        env->sysenter_eip = val;
        break;
    case MSR_IA32_APICBASE:
        // cpu_set_apic_base(env_archcpu(env)->apic_state, val);
        break;
    case MSR_EFER:
        {
            uint64_t update_mask;

            update_mask = 0;
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_SYSCALL) {
                update_mask |= MSR_EFER_SCE;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
                update_mask |= MSR_EFER_LME;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_FFXSR) {
                update_mask |= MSR_EFER_FFXSR;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_NX) {
                update_mask |= MSR_EFER_NXE;
            }
            if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
                update_mask |= MSR_EFER_SVME;
            }
            if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_FFXSR) {
                update_mask |= MSR_EFER_FFXSR;
            }
            cpu_load_efer(env, (env->efer & ~update_mask) |
                          (val & update_mask));
        }
        break;
    case MSR_STAR:
        env->star = val;
        break;
    case MSR_PAT:
        env->pat = val;
        break;
    case MSR_VM_HSAVE_PA:
        env->vm_hsave = val;
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        env->lstar = val;
        break;
    case MSR_CSTAR:
        env->cstar = val;
        break;
    case MSR_FMASK:
        env->fmask = val;
        break;
    case MSR_FSBASE:
        env->segs[R_FS].base = val;
        break;
    case MSR_GSBASE:
        env->segs[R_GS].base = val;
        break;
    case MSR_KERNELGSBASE:
        env->kernelgsbase = val;
        break;
#endif
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                       MSR_MTRRphysBase(0)) / 2].base = val;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                       MSR_MTRRphysMask(0)) / 2].mask = val;
        break;
    case MSR_MTRRfix64K_00000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix64K_00000] = val;
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix16K_80000 + 1] = val;
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                        MSR_MTRRfix4K_C0000 + 3] = val;
        break;
    case MSR_MTRRdefType:
        env->mtrr_deftype = val;
        break;
    case MSR_MCG_STATUS:
        env->mcg_status = val;
        break;
    case MSR_MCG_CTL:
        if ((env->mcg_cap & MCG_CTL_P)
            && (val == 0 || val == ~(uint64_t)0)) {
            env->mcg_ctl = val;
        }
        break;
    case MSR_TSC_AUX:
        env->tsc_aux = val;
        break;
    case MSR_IA32_XSS: {
        uint64_t valid;

        valid = ((uint64_t)env->features[FEAT_XSAVE_XSS_HI] << 32) |
                env->features[FEAT_XSAVE_XSS_LO];
#if __Use_Original_Qemu == 1 /* original QEMU (U727) */
        env->xss = val & valid;
#else /* ours (U727) */
        /*
         * NoVmp (ledger U727): SDM Vol1 13.2/13.3 - IA32_XSS exists only with CPUID.(EAX=0DH,
         * ECX=1):EAX.XSAVES[3] ("an attempt to access the IA32_XSS MSR using RDMSR or WRMSR
         * causes a #GP"), and "a bit can be set in the IA32_XSS MSR if and only if the
         * corresponding bit is set in" CPUID.(0DH,1):EDX:ECX - WRMSR with any other bit #GP(0)
         * (was: silently dropped). An API write (UC_X86_REG_MSR) is dropped instead, as U173.
         * U1122: and returns UC_ERR_EXCEPTION (msr_api_err), the U905 contract (was: UC_ERR_OK).
         * U1581 (plan 1.J.56): "that bitmap" is what CPUID reports - with a strict UC_CTL_X86_CPUID profile its
         * 0DH.1 EDX:ECX (i5-13600K --host 19900H incl. PT / LBR / HWP, U1582; --guest 1800H), else the model's
         * (x86_cpu_xss_valid); before, the model's CET_U | CET_S only, so --host WRMSR of PT / LBR / HWP was #GP(0).
         */
        valid = x86_cpu_xss_valid(env);
        if (!(env->features[FEAT_XSAVE] & CPUID_XSAVE_XSAVES) || (val & ~valid)) {
            if (env->msr_api) {
                env->msr_api_err = 1;   /* U1122 */
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        env->xss = val;
#endif /* __Use_Original_Qemu (U727) */
        break;
    }
#if __Use_Original_Qemu == 1 /* original QEMU (U173) */
    case MSR_IA32_XFD:
        env->msr_xfd = val;
        break;
    case MSR_IA32_XFD_ERR:
        env->msr_xfd_err = val;
        break;
#else /* ours (U173) */
    case MSR_IA32_XFD:
    case MSR_IA32_XFD_ERR:
        /*
         * NoVmp (ledger U173): SDM Vol1 13.14 - with CPUID.(EAX=0DH,ECX=1):EAX[4] (XFD,
         * U170) "Bit i of either MSR can be set to 1 only if CPUID.0DH.i:ECX[2] is
         * enumerated as 1": other bits #GP(0) (an API write with them is dropped).
         * Without XFD the value is kept as QEMU did; only supported bits ever take
         * effect (x86_cpu_xfd_armed).
         */
        if ((env->features[FEAT_XSAVE] & CPUID_D_1_EAX_XFD) &&
            (val & ~x86_cpu_xfd_supported(env))) {
            if (env->msr_api) {
                env->msr_api_err = 1;   /* U1122: UC_ERR_EXCEPTION (U905) */
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        if ((uint32_t)env->regs[R_ECX] == MSR_IA32_XFD) {
            env->msr_xfd = val;
        } else {
            env->msr_xfd_err = val;
        }
        break;
#endif /* __Use_Original_Qemu (U173) */
    case MSR_IA32_PKRS:
        if (val & 0xffffffff00000000ull) {
#if __Use_Original_Qemu != 1 /* ours (U878) */
            if (env->msr_api) {
                /* an API write of a reserved value is dropped, no #GP longjmp (U878); since
                   U906 msr_write_ok refuses it first (UC_ERR_EXCEPTION, U1063) */
                env->msr_api_err = 1;   /* U1122 */
                break;
            }
#endif /* __Use_Original_Qemu (U878) */
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        env->pkrs = val;
#if __Use_Original_Qemu == 1 /* original QEMU (U878) */
        tlb_flush(cs);
#else /* ours (U878) */
        if (env->msr_api != 2) {
            tlb_flush(cs);  /* an image has no TLB: the restore flushes (U832/U878) */
        }
#endif /* __Use_Original_Qemu (U878) */
        break;
    case MSR_ARCH_LBR_CTL:
        env->msr_lbr_ctl = val;
        break;
    case MSR_ARCH_LBR_DEPTH:
        env->msr_lbr_depth = val;
        break;
    case MSR_IA32_MISC_ENABLE:
        env->msr_ia32_misc_enable = val;
        break;
#if __Use_Original_Qemu != 1 /* ours (U103) */
    case MSR_IA32_USER_MSR_CTL:
        /* SDM Vol4: bit 0 enable, 11:1 reserved, 63:12 canonical bitmap address */
        if (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_USER_MSR) {
            if ((val & 0xffe) || !novmp_canonical(env, val)) {
                if (env->msr_api) {
                    env->msr_api_err = 1;   /* U1122: UC_ERR_EXCEPTION (U905) */
                    break;      /* API write of a reserved value: dropped (U878) */
                }
                raise_exception_ra(env, EXCP0D_GPF, GETPC());
            }
            env->msr_user_msr_ctl = val;
        }
        break;
    case MSR_IA32_UARCH_MISC_CTL:
        /* SDM Vol4: bit 0 DOITM, 63:1 reserved (modelled with USER_MSR, U103) */
        if (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_USER_MSR) {
            if (val & ~1ULL) {
                if (env->msr_api) {
                    env->msr_api_err = 1;   /* U1122: UC_ERR_EXCEPTION (U905) */
                    break;      /* API write of a reserved value: dropped (U878) */
                }
                raise_exception_ra(env, EXCP0D_GPF, GETPC());
            }
            env->msr_uarch_misc_ctl = val;
        }
        break;
#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U802) */
    case MSR_IA32_BARRIER:
        /*
         * NoVmp (ledger U802): IA32_BARRIER (2FH) exists with CPUID.(07H,1):EAX.MSRLIST[27] and
         * is read-only (SDM Vol4): WRMSR #GP(0) (an API write is dropped). Without MSRLIST it
         * stays an unknown MSR (ignored, as before). U1122: an API write returns UC_ERR_EXCEPTION
         * (msr_api_err, U905; msr_write_ok refuses it first since U906).
         */
        if (env->features[FEAT_7_1_EAX] & CPUID_7_1_EAX_MSRLIST) {
            if (env->msr_api) {
                env->msr_api_err = 1;
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        break;
#endif /* __Use_Original_Qemu (U802) */
#if __Use_Original_Qemu != 1 /* ours (U804) */
    case MSR_IA32_HRESET_ENABLE:
        /*
         * NoVmp (ledger U804): IA32_HRESET_ENABLE (17DAH) exists with CPUID.(07H,1):EAX.HRESET;
         * "only the bits enumerated by CPUID.20H.00H:EBX can be set" (SDM Vol2A HRESET, Vol4:
         * 31:1 reserved for other capabilities, 63:32 reserved): other bits #GP(0) (an API
         * write with them is dropped). Without HRESET it stays an unknown MSR (ignored).
         */
        if (env->features[FEAT_7_1_EAX] & CPUID_7_1_EAX_HRESET) {
            if (val & ~(uint64_t)CPUID_20_0_EBX_THREAD_DIRECTOR_HRESET) {
                if (env->msr_api) {
                    env->msr_api_err = 1;   /* U1122: UC_ERR_EXCEPTION (U905) */
                    break;
                }
                raise_exception_ra(env, EXCP0D_GPF, GETPC());
            }
            env->msr_hreset_enable = val;
        }
        break;
#endif /* __Use_Original_Qemu (U804) */
#if __Use_Original_Qemu != 1 /* ours (U1128) */
    case MSR_IA32_HW_FEEDBACK_THREAD_CONFIG:
        /*
         * NoVmp (ledger U1128): bit 0 enables ITD on this logical processor (msr_access_ok checked
         * presence and the reserved bits). Vol3B 17.6.8: when the OS clears it, "the hardware
         * also clears the history of the logical processor".
         */
        env->itd_thread_config = val;
        if (!(val & 1)) {
            env->itd_valid = 0;
            env->itd_class = 0;
        }
        break;
#endif /* __Use_Original_Qemu (U1128) */
#if __Use_Original_Qemu != 1 /* ours (U1332) */
    case MSR_IA32_SEAMRR_BASE:
    case MSR_IA32_SEAMRR_MASK:
        /* NoVmp (ledger U1332): present and writable as msr_present / msr_write_ok allowed it */
        x86_seam_wrmsr(env, (uint32_t)env->regs[R_ECX], val);
        break;
#endif /* __Use_Original_Qemu (U1332) */
#if __Use_Original_Qemu != 1 /* ours (U1261) */
    case MSR_IA32_SGXLEPUBKEYHASH0:
    case MSR_IA32_SGXLEPUBKEYHASH0 + 1:
    case MSR_IA32_SGXLEPUBKEYHASH0 + 2:
    case MSR_IA32_SGXLEPUBKEYHASH3:
        /* NoVmp (ledger U1261): writable when msr_write_ok allowed it (FEATURE_CONTROL[17] + lock) */
        x86_sgx_wrmsr(env, (uint32_t)env->regs[R_ECX], val);
        break;
#endif /* __Use_Original_Qemu (U1261) */
#if __Use_Original_Qemu != 1 /* ours (U807) */
    case MSR_IA32_TSE_CAPABILITY:
        /* NoVmp (ledger U807): read-only with PBNDKB: WRMSR #GP(0) (an API write is dropped;
           U1122: with UC_ERR_EXCEPTION, msr_api_err - msr_write_ok refuses it first since U906) */
        if (env->features[FEAT_7_1_EBX] & CPUID_7_1_EBX_PBNDKB) {
            if (env->msr_api) {
                env->msr_api_err = 1;
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        break;
#endif /* __Use_Original_Qemu (U807) */
#if __Use_Original_Qemu != 1 /* ours (U1020) */
    case MSR_IA32_TME_CAPABILITY:
    case MSR_IA32_TME_ACTIVATE:
    case MSR_IA32_TME_EXCLUDE_MASK:
    case MSR_IA32_TME_EXCLUDE_BASE:
    case MSR_IA32_MKTME_KEYID_PARTITIONING:
        /*
         * NoVmp (ledger U1020, U1022): see tme_wrmsr. U1060: presence is checked by the U905
         * MSR list (msr_present); an API write that would #GP(0) writes nothing and returns
         * UC_ERR_EXCEPTION (msr_api_err), as every access U905 / U906 refuse (was: dropped, OK)
         */
        if (!tme_enumerated(env) || !tme_wrmsr(env, (uint32_t)env->regs[R_ECX], val)) {
            if (env->msr_api) {
                env->msr_api_err = 1;
            } else {
                raise_exception_ra(env, EXCP0D_GPF, GETPC());
            }
        }
        break;
#endif /* __Use_Original_Qemu (U1020) */
#if __Use_Original_Qemu != 1 /* ours (U104) */
    /* user-interrupt MSRs (SDM Vol3A 9.3.2), present with CPUID.(07H,0):EDX.UINTR */
    case MSR_IA32_UINTR_RR:
        if (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_UINTR) {
            env->uintr_rr = val;
            if (env->msr_api != 2) {
                x86_uintr_update_request(env);  /* an image: on restore (U878) */
            }
        }
        break;
    case MSR_IA32_UINTR_HANDLER:
    case MSR_IA32_UINTR_STACKADJUST:
    case MSR_IA32_UINTR_MISC:
    case MSR_IA32_UINTR_PD:
    case MSR_IA32_UINTR_TT:
        if (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_UINTR) {
            uint32_t msr = (uint32_t)env->regs[R_ECX];
            bool gp = false;

            switch (msr) {
            case MSR_IA32_UINTR_HANDLER:
                gp = !novmp_canonical(env, val);
                env->uintr_handler = gp ? env->uintr_handler : val;
                break;
            case MSR_IA32_UINTR_STACKADJUST:
                gp = !novmp_canonical(env, val);
                env->uintr_stackadjust = gp ? env->uintr_stackadjust : val;
                break;
            case MSR_IA32_UINTR_MISC:
                gp = (val >> 40) != 0;
                env->uintr_misc = gp ? env->uintr_misc : val;
                break;
            case MSR_IA32_UINTR_PD:
                gp = !novmp_canonical(env, val) || (val & 0x3f);
                env->uintr_pd = gp ? env->uintr_pd : val;
                break;
            default:
                gp = !novmp_canonical(env, val & ~0xfULL) || (val & 0xe);
                env->uintr_tt = gp ? env->uintr_tt : val;
                break;
            }
            if (gp && env->msr_api) {
                env->msr_api_err = 1;   /* U1122: UC_ERR_EXCEPTION (U905) */
                break;          /* API write of a reserved value: dropped (U878) */
            }
            if (gp) {
                raise_exception_ra(env, EXCP0D_GPF, GETPC());
            }
        }
        break;
#endif /* __Use_Original_Qemu (U104) */
#if __Use_Original_Qemu != 1 /* ours (U111) */
    case MSR_IA32_UMWAIT_CONTROL:
        /*
         * NoVmp (ledger U111): IA32_UMWAIT_CONTROL (E1H, WAITPKG): bit 0
         * C0.2 disable, bit 1 reserved, 31:2 maximum wait in TSC quanta,
         * 63:32 reserved (SDM Vol4 Table 2-2): #GP(0) on reserved bits.
         * Without WAITPKG it is an unknown MSR (ignored, as before).
         */
        if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_WAITPKG)) {
            break;
        }
        if (val & ~0xfffffffdull) {
            if (env->msr_api) {
                env->msr_api_err = 1;   /* U1122: UC_ERR_EXCEPTION (U905) */
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        env->umwait = (uint32_t)val;
        break;
#endif /* __Use_Original_Qemu (U111) */
#if __Use_Original_Qemu != 1 /* ours (U114) */
    case MSR_IA32_U_CET:
    case MSR_IA32_S_CET:
    case MSR_IA32_PL0_SSP:
    case MSR_IA32_PL1_SSP:
    case MSR_IA32_PL2_SSP:
    case MSR_IA32_PL3_SSP:
    case MSR_IA32_INT_SSP_TAB:
        /* NoVmp (ledger U114): CET MSRs, see cet_wrmsr; #GP(0) on an invalid value (U1122: an
           API write UC_ERR_EXCEPTION, msr_api_err - msr_write_ok refuses it first since U906) */
        if (!cet_wrmsr(env, (uint32_t)env->regs[R_ECX], val)) {
            if (env->msr_api) {
                env->msr_api_err = 1;
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#if __Use_Original_Qemu != 1 /* ours (U1170) */
        /* IA32_FRED_SSP0 on a CPU with FRED but without CET shadow stacks (msr_write_ok checked it) */
        if ((uint32_t)env->regs[R_ECX] == MSR_IA32_PL0_SSP &&
            !(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK)) {
            env->pl_ssp[0] = val;
        }
#endif /* __Use_Original_Qemu (U1170) */
        break;
#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U1170) */
    /* NoVmp (ledger U1170): the FRED MSRs (values checked by msr_write_ok, Vol3A 8.2.3) */
    case MSR_IA32_FRED_RSP0:
    case MSR_IA32_FRED_RSP0 + 1:
    case MSR_IA32_FRED_RSP0 + 2:
    case MSR_IA32_FRED_RSP3:
        env->fred_rsp[(uint32_t)env->regs[R_ECX] - MSR_IA32_FRED_RSP0] = val;
        break;
    case MSR_IA32_FRED_SSP1:
    case MSR_IA32_FRED_SSP1 + 1:
    case MSR_IA32_FRED_SSP3:
        env->fred_ssp[(uint32_t)env->regs[R_ECX] - MSR_IA32_FRED_SSP1 + 1] = val;
        break;
    case MSR_IA32_FRED_STKLVLS:
        env->fred_stklvls = val;
        break;
    case MSR_IA32_FRED_CONFIG:
        env->fred_config = val;
        break;
#endif /* __Use_Original_Qemu (U1170) */
#if __Use_Original_Qemu != 1 /* ours (U112) */
    case MSR_IA32_PASID:
        /*
         * NoVmp (ledger U112): IA32_PASID (D93H, ENQCMD): 19:0 PASID, 31
         * valid, 30:20 and 63:32 reserved (#GP(0)). Without ENQCMD it is an
         * unknown MSR (ignored, as before).
         */
        if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_ENQCMD)) {
            break;
        }
        if (val & ~0x800fffffull) {
            if (env->msr_api) {
                env->msr_api_err = 1;   /* U1122: UC_ERR_EXCEPTION (U905) */
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        env->pasid = val;
        break;
#endif /* __Use_Original_Qemu (U112) */
    case MSR_IA32_BNDCFGS:
#if __Use_Original_Qemu == 1 /* original QEMU (U783) */
        /* FIXME: #GP if reserved bits are set.  */
        /* FIXME: Extend highest implemented bit of linear address.  */
#else /* ours (U783) */
        /*
         * NoVmp (ledger U783): SDM Vol1 E.3.3 "WRMSR to BNDCFGS will #GP if any of the
         * reserved bits of BNDCFGS is not zero or if the base address of the bound directory
         * is not canonical" (bits 11:2 reserved; canonical to the CPU's linear-address width).
         * An API write with such a value is dropped, as for the other MSRs.
         */
        if ((val & 0xffc) || !novmp_canonical(env, val)) {
            if (env->msr_api) {
                env->msr_api_err = 1;   /* U1122: UC_ERR_EXCEPTION (U905) */
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#endif /* __Use_Original_Qemu (U783) */
        env->msr_bndcfgs = val;
        cpu_sync_bndcs_hflags(env);
        break;
    default:
#if __Use_Original_Qemu != 1 /* ours (U907) */
        {   /* the architectural PMU MSRs (storage) */
            X86Pmu p;

            x86_pmu(env, &p);
            if (pmu_msr(env, (uint32_t)env->regs[R_ECX], &p)) {
                pmu_write(env, (uint32_t)env->regs[R_ECX], val, &p);
                break;
            }
        }
#endif /* __Use_Original_Qemu (U907) */
        if ((uint32_t)env->regs[R_ECX] >= MSR_ARCH_LBR_FROM_0 &&
            (uint32_t)env->regs[R_ECX] <
            MSR_ARCH_LBR_FROM_0 + ARCH_LBR_NR_ENTRIES) {
            env->lbr_records[(uint32_t)env->regs[R_ECX] -
                             MSR_ARCH_LBR_FROM_0].from = val;
            break;
        }
        if ((uint32_t)env->regs[R_ECX] >= MSR_ARCH_LBR_TO_0 &&
            (uint32_t)env->regs[R_ECX] <
            MSR_ARCH_LBR_TO_0 + ARCH_LBR_NR_ENTRIES) {
            env->lbr_records[(uint32_t)env->regs[R_ECX] -
                             MSR_ARCH_LBR_TO_0].to = val;
            break;
        }
        if ((uint32_t)env->regs[R_ECX] >= MSR_ARCH_LBR_INFO_0 &&
            (uint32_t)env->regs[R_ECX] <
            MSR_ARCH_LBR_INFO_0 + ARCH_LBR_NR_ENTRIES) {
            env->lbr_records[(uint32_t)env->regs[R_ECX] -
                             MSR_ARCH_LBR_INFO_0].info = val;
            break;
        }
        if ((uint32_t)env->regs[R_ECX] >= MSR_MC0_CTL
            && (uint32_t)env->regs[R_ECX] < MSR_MC0_CTL +
            (4 * env->mcg_cap & 0xff)) {
            uint32_t offset = (uint32_t)env->regs[R_ECX] - MSR_MC0_CTL;
            if ((offset & 0x3) != 0
                || (val == 0 || val == ~(uint64_t)0)) {
                env->mce_banks[offset] = val;
            }
            break;
        }
        /* XXX: exception? */
        break;
    }
}

void helper_rdmsr(CPUX86State *env)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U878) */
    X86CPU *x86_cpu = env_archcpu(env);
#else /* ours (U878) */
    /* a uc_context image (msr_api == 2) has no X86CPU of its own: the engine's model (U878) */
    X86CPU *x86_cpu = env->msr_api == 2 ? X86_CPU(env->uc->cpu) : env_archcpu(env);
#endif /* __Use_Original_Qemu (U878) */
    uint64_t val;
    uc_engine *uc = env->uc;
    struct hook *hook;
    int skip_rdmsr = 0;
    bool synced = false;
#if __Use_Original_Qemu != 1 /* ours (U905) */
    uintptr_t rdmsr_ra = GETPC();
#endif /* __Use_Original_Qemu (U905) */

    cpu_svm_check_intercept_param(env, SVM_EXIT_MSR, 0, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* VMX non-root operation: the MSR bitmaps (Vol3C 28.1.3); an instruction's own access only */
    if (env->msr_api == 0 && !env->msr_swap) {
        x86_vmx_msr_check(env, (uint32_t)env->regs[R_ECX], false, VMX_EXIT_RDMSR, 0, GETPC());
    }
#endif /* __Use_Original_Qemu (U1304) */

    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN)
    {
#if __Use_Original_Qemu != 1 /* ours (U878) */
        if (env->msr_api == 2 || env->msr_api == 3) {
            break;      /* a read of a uc_context image runs no RDMSR hook (U878); U1303: nor the
                           MSR-store area of a VM exit */
        }
#endif /* __Use_Original_Qemu (U878) */
        if (hook->to_delete)
            continue;
        if (!HOOK_BOUND_CHECK(hook, env->eip))
            continue;
        if (hook->insn == UC_X86_INS_RDMSR) {
            uintptr_t pc = GETPC();
            if (!synced && !uc->skip_sync_pc_on_exit && pc) {
                cpu_restore_state(uc->cpu, pc, false);
                synced = true;
            }
            JIT_CALLBACK_GUARD_VAR(
                skip_rdmsr,
                ((uc_cb_insn_cpuid_t)hook->callback)(env->uc, hook->user_data));
        }
        if (env->uc->stop_request)
            break;
    }

    if (skip_rdmsr)
        return;
#if __Use_Original_Qemu != 1 /* ours (U1307) */
    /* VMX non-root operation: "virtualize x2APIC mode" reads (Vol3C 32.5) */
    if (env->msr_api == 0 && x86_vmx_x2apic_rdmsr(env, (uint32_t)env->regs[R_ECX], &val)) {
        env->regs[R_EAX] = (uint32_t)(val);
        env->regs[R_EDX] = (uint32_t)(val >> 32);
        return;
    }
#endif /* __Use_Original_Qemu (U1307) */

#if __Use_Original_Qemu != 1 /* ours (U960) */
    /* IA32_APIC_BASE and the x2APIC MSRs 800H-8FFH: the local APIC (apic_model.c); U1123: only
       when the MSR exists for the CPU as reported (msr_present), else the U905 refusal below */
    if (msr_present(env, (uint32_t)env->regs[R_ECX]) &&
        x86_apic_msr_read(env, (uint32_t)env->regs[R_ECX], &val, GETPC())) {
        env->regs[R_EAX] = (uint32_t)(val);
        env->regs[R_EDX] = (uint32_t)(val >> 32);
        return;
    }
#endif /* __Use_Original_Qemu (U960) */
#if __Use_Original_Qemu != 1 /* ours (U905) */
    if (!msr_access_ok(env, (uint32_t)env->regs[R_ECX], false, 0, rdmsr_ra)) {
        return;
    }
#endif /* __Use_Original_Qemu (U905) */
#if __Use_Original_Qemu != 1 /* ours (U1301) */
    /* NoVmp (ledger U1301): the VMX capability MSRs (vmx_helper.c, Vol3D Appendix A) */
    if (x86_vmx_msr_read(env, (uint32_t)env->regs[R_ECX], &val)) {
        env->regs[R_EAX] = (uint32_t)(val);
        env->regs[R_EDX] = (uint32_t)(val >> 32);
        return;
    }
#endif /* __Use_Original_Qemu (U1301) */
    switch ((uint32_t)env->regs[R_ECX]) {
#if __Use_Original_Qemu != 1 /* ours (U905) */
    /* NoVmp (ledger U905): MSRs of Table 2-2 the emulator did not model */
    case MSR_IA32_TSC:
        val = msr_tsc_now(env);
#if __Use_Original_Qemu != 1 /* ours (U1304) */
        if (env->msr_api == 0) {
            val = x86_vmx_guest_tsc(env, val);  /* RDMSR 10H: TSC offsetting / scaling (28.3) */
        }
#endif /* __Use_Original_Qemu (U1304) */
        break;
    case MSR_TSC_ADJUST:
        val = env->tsc_adjust;
        break;
    case MSR_IA32_FEATURE_CONTROL:
        val = env->msr_ia32_feature_control;
        break;
    case MSR_IA32_SPEC_CTRL:
        val = env->spec_ctrl;
        break;
    case MSR_IA32_TSX_CTRL:
        val = env->tsx_ctrl;
        break;
    case MSR_IA32_PERF_CTL_NV:
        val = env->msr_perf_ctl;
        break;
    case MSR_IA32_DEBUGCTL_NV:
        val = env->msr_debugctl;
        break;
    case MSR_IA32_ARCH_CAPABILITIES:
        val = env->features[FEAT_ARCH_CAPABILITIES];
        break;
    case MSR_IA32_CORE_CAPABILITY:
        val = env->features[FEAT_CORE_CAPABILITY];
        break;
    case MSR_IA32_PLATFORM_ID_NV:
        /* U1470: Platform ID (52:50) of the CPU profile (UC_CTL_X86_MICROCODE, default 0); the
           other bits are reserved (SDM Vol4 17H) and read 0 */
        val = (uint64_t)(x86_cpu->platform_id & 7) << 50;
        break;
    case MSR_IA32_PRED_CMD:
    case MSR_IA32_FLUSH_CMD_NV:
    case MSR_IA32_TSCDEADLINE:          /* not in TSC-deadline mode: reads 0 (Vol3A 13.5.4.1) */
        val = 0;
        break;
#endif /* __Use_Original_Qemu (U905) */
    case MSR_IA32_SYSENTER_CS:
        val = env->sysenter_cs;
        break;
    case MSR_IA32_SYSENTER_ESP:
        val = env->sysenter_esp;
        break;
    case MSR_IA32_SYSENTER_EIP:
        val = env->sysenter_eip;
        break;
    case MSR_IA32_APICBASE:
        val = 0; // cpu_get_apic_base(env_archcpu(env)->apic_state);
        break;
    case MSR_EFER:
        val = env->efer;
        break;
    case MSR_STAR:
        val = env->star;
        break;
    case MSR_PAT:
        val = env->pat;
        break;
    case MSR_VM_HSAVE_PA:
        val = env->vm_hsave;
        break;
    case MSR_IA32_PERF_STATUS:
        /* tsc_increment_by_tick */
        val = 1000ULL;
        /* CPU multiplier */
        val |= (((uint64_t)4ULL) << 40);
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        val = env->lstar;
        break;
    case MSR_CSTAR:
        val = env->cstar;
        break;
    case MSR_FMASK:
        val = env->fmask;
        break;
    case MSR_FSBASE:
        val = env->segs[R_FS].base;
        break;
    case MSR_GSBASE:
        val = env->segs[R_GS].base;
        break;
    case MSR_KERNELGSBASE:
        val = env->kernelgsbase;
        break;
    case MSR_TSC_AUX:
        val = env->tsc_aux;
        break;
#endif
    case MSR_SMI_COUNT:
        val = env->msr_smi_count;
        break;
    case MSR_MTRRphysBase(0):
    case MSR_MTRRphysBase(1):
    case MSR_MTRRphysBase(2):
    case MSR_MTRRphysBase(3):
    case MSR_MTRRphysBase(4):
    case MSR_MTRRphysBase(5):
    case MSR_MTRRphysBase(6):
    case MSR_MTRRphysBase(7):
        val = env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                             MSR_MTRRphysBase(0)) / 2].base;
        break;
    case MSR_MTRRphysMask(0):
    case MSR_MTRRphysMask(1):
    case MSR_MTRRphysMask(2):
    case MSR_MTRRphysMask(3):
    case MSR_MTRRphysMask(4):
    case MSR_MTRRphysMask(5):
    case MSR_MTRRphysMask(6):
    case MSR_MTRRphysMask(7):
        val = env->mtrr_var[((uint32_t)env->regs[R_ECX] -
                             MSR_MTRRphysMask(0)) / 2].mask;
        break;
    case MSR_MTRRfix64K_00000:
        val = env->mtrr_fixed[0];
        break;
    case MSR_MTRRfix16K_80000:
    case MSR_MTRRfix16K_A0000:
        val = env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                              MSR_MTRRfix16K_80000 + 1];
        break;
    case MSR_MTRRfix4K_C0000:
    case MSR_MTRRfix4K_C8000:
    case MSR_MTRRfix4K_D0000:
    case MSR_MTRRfix4K_D8000:
    case MSR_MTRRfix4K_E0000:
    case MSR_MTRRfix4K_E8000:
    case MSR_MTRRfix4K_F0000:
    case MSR_MTRRfix4K_F8000:
        val = env->mtrr_fixed[(uint32_t)env->regs[R_ECX] -
                              MSR_MTRRfix4K_C0000 + 3];
        break;
    case MSR_MTRRdefType:
        val = env->mtrr_deftype;
        break;
    case MSR_MTRRcap:
        if (env->features[FEAT_1_EDX] & CPUID_MTRR) {
            val = MSR_MTRRcap_VCNT | MSR_MTRRcap_FIXRANGE_SUPPORT |
                MSR_MTRRcap_WC_SUPPORTED;
#if __Use_Original_Qemu != 1 /* ours (U1332) */
            /* "Processors that support SEAM architecture return IA32_MTRRCAP bit 15 as 1" (Vol3C
               35.4.1 p. 35-5; 343754 4.1 p. 4-1): SEAMRR supported (Vol4 FEH) */
            if (x86_seam_enumerated(env)) {
                val |= MSR_MTRRcap_SEAMRR;
            }
#endif /* __Use_Original_Qemu (U1332) */
        } else {
            /* XXX: exception? */
            val = 0;
        }
        break;
    case MSR_MCG_CAP:
        val = env->mcg_cap;
        break;
    case MSR_MCG_CTL:
        if (env->mcg_cap & MCG_CTL_P) {
            val = env->mcg_ctl;
        } else {
            val = 0;
        }
        break;
    case MSR_MCG_STATUS:
        val = env->mcg_status;
        break;
    case MSR_IA32_MISC_ENABLE:
        val = env->msr_ia32_misc_enable;
        break;
    case MSR_IA32_BNDCFGS:
        val = env->msr_bndcfgs;
        break;
#if __Use_Original_Qemu != 1 /* ours (U111) */
    case MSR_IA32_UMWAIT_CONTROL:
        val = (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_WAITPKG) ? env->umwait : 0;
        break;
#endif /* __Use_Original_Qemu (U111) */
#if __Use_Original_Qemu != 1 /* ours (U112) */
    case MSR_IA32_PASID:
        val = (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_ENQCMD) ? env->pasid : 0;
        break;
#endif /* __Use_Original_Qemu (U112) */
#if __Use_Original_Qemu != 1 /* ours (U114) */
    case MSR_IA32_U_CET:
        val = env->u_cet;
        break;
    case MSR_IA32_S_CET:
        val = env->s_cet;
        break;
    case MSR_IA32_PL0_SSP:
    case MSR_IA32_PL1_SSP:
    case MSR_IA32_PL2_SSP:
    case MSR_IA32_PL3_SSP:
        val = env->pl_ssp[(uint32_t)env->regs[R_ECX] - MSR_IA32_PL0_SSP];
        break;
    case MSR_IA32_INT_SSP_TAB:
        val = env->int_ssp_table;
        break;
#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U1170) */
    case MSR_IA32_FRED_RSP0:
    case MSR_IA32_FRED_RSP0 + 1:
    case MSR_IA32_FRED_RSP0 + 2:
    case MSR_IA32_FRED_RSP3:
        val = env->fred_rsp[(uint32_t)env->regs[R_ECX] - MSR_IA32_FRED_RSP0];
        break;
    case MSR_IA32_FRED_SSP1:
    case MSR_IA32_FRED_SSP1 + 1:
    case MSR_IA32_FRED_SSP3:
        val = env->fred_ssp[(uint32_t)env->regs[R_ECX] - MSR_IA32_FRED_SSP1 + 1];
        break;
    case MSR_IA32_FRED_STKLVLS:
        val = env->fred_stklvls;
        break;
    case MSR_IA32_FRED_CONFIG:
        val = env->fred_config;
        break;
#endif /* __Use_Original_Qemu (U1170) */
    case MSR_IA32_XSS:
#if __Use_Original_Qemu != 1 /* ours (U727) */
        /* NoVmp (ledger U727): no IA32_XSS without CPUID.(0DH,1):EAX.XSAVES (#GP, SDM 13.2) */
        if (!(env->features[FEAT_XSAVE] & CPUID_XSAVE_XSAVES) && !env->msr_api) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#endif /* __Use_Original_Qemu (U727) */
        val = env->xss;
        break;
    case MSR_IA32_XFD:
        val = env->msr_xfd;
        break;
    case MSR_IA32_XFD_ERR:
        val = env->msr_xfd_err;
        break;
    case MSR_IA32_PKRS:
        val = env->pkrs;
        break;
    case MSR_ARCH_LBR_CTL:
        val = env->msr_lbr_ctl;
        break;
    case MSR_ARCH_LBR_DEPTH:
        val = env->msr_lbr_depth;
        break;
    case MSR_IA32_UCODE_REV:
#if __Use_Original_Qemu == 1 /* original QEMU (U1471) */
        val = x86_cpu->ucode_rev;
#else /* ours (U1471) */
        /* Intel: the IA32_BIOS_SIGN_ID register (written value / CPUID deposit); AMD: the patch level */
        val = IS_INTEL_CPU(env) ? env->msr_bios_sign_id : x86_cpu->ucode_rev;
#endif /* __Use_Original_Qemu (U1471) */
        break;
#if __Use_Original_Qemu != 1 /* ours (U103) */
    case MSR_IA32_USER_MSR_CTL:
        val = env->msr_user_msr_ctl;
        break;
    case MSR_IA32_UARCH_MISC_CTL:
        val = env->msr_uarch_misc_ctl;
        break;
#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U802) */
    case MSR_IA32_BARRIER:
        /* NoVmp (ledger U802): SDM Vol4 IA32_BARRIER (R/O, bits 63:0 always 0) */
        val = 0;
        break;
#endif /* __Use_Original_Qemu (U802) */
#if __Use_Original_Qemu != 1 /* ours (U804) */
    case MSR_IA32_HRESET_ENABLE:
        val = env->msr_hreset_enable;
        break;
#endif /* __Use_Original_Qemu (U804) */
#if __Use_Original_Qemu != 1 /* ours (U1128) */
    case MSR_IA32_HW_FEEDBACK_THREAD_CONFIG:
        val = env->itd_thread_config;
        break;
    case MSR_IA32_THREAD_FEEDBACK_CHAR:
        /*
         * NoVmp (ledger U1128): Vol3B 17.6.10: the class ID is produced while THREAD_CONFIG[0] = 1;
         * Valid (63) = 1 only with an ITD history the (host-set) classification created; with
         * Valid = 0 the class ID is "to be ignored" - the model reads 0 then.
         */
        val = ((env->itd_thread_config & 1) && env->itd_valid) ?
              ((1ULL << 63) | env->itd_class) : 0;
        break;
#endif /* __Use_Original_Qemu (U1128) */
#if __Use_Original_Qemu != 1 /* ours (U1332) */
    case MSR_IA32_SEAMRR_BASE:
    case MSR_IA32_SEAMRR_MASK:
        /* NoVmp (ledger U1332): seam_model.c (present only with SEAM, msr_present) */
        if (!x86_seam_rdmsr(env, (uint32_t)env->regs[R_ECX], &val)) {
            val = 0;
        }
        break;
#endif /* __Use_Original_Qemu (U1332) */
#if __Use_Original_Qemu != 1 /* ours (U1261) */
    case MSR_IA32_SGXLEPUBKEYHASH0:
    case MSR_IA32_SGXLEPUBKEYHASH0 + 1:
    case MSR_IA32_SGXLEPUBKEYHASH0 + 2:
    case MSR_IA32_SGXLEPUBKEYHASH3:
    case MSR_IA32_SGX_SVN_STATUS:
        /* NoVmp (ledger U1261): sgx_model.c (present only with SGX, msr_present) */
        if (!x86_sgx_rdmsr(env, (uint32_t)env->regs[R_ECX], &val)) {
            val = 0;
        }
        break;
#endif /* __Use_Original_Qemu (U1261) */
#if __Use_Original_Qemu != 1 /* ours (U807) */
    case MSR_IA32_TSE_CAPABILITY:
        /*
         * NoVmp (ledger U807): SDM Vol4 IA32_TSE_CAPABILITY (R/O, with PBNDKB): no encryption
         * algorithm, no key source, TSE_MAX_KEYS 0 - the model has no TSE engine. U1120: with the
         * UC_X86_FEATURE_TSE opt-in NOVMP_TSE_CAPABILITY (x86_tse_capability).
         */
        val = x86_tse_capability(env);
        break;
#endif /* __Use_Original_Qemu (U807) */
#if __Use_Original_Qemu != 1 /* ours (U1020) */
    case MSR_IA32_TME_CAPABILITY:
    case MSR_IA32_TME_ACTIVATE:
    case MSR_IA32_TME_EXCLUDE_MASK:
    case MSR_IA32_TME_EXCLUDE_BASE:
    case MSR_IA32_MKTME_KEYID_PARTITIONING:
        /* NoVmp (ledger U1020): tme_rdmsr; U1022: #GP(0) without TME_EN (U1060: msr_present) */
        if (!tme_enumerated(env) && !env->msr_api) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        val = tme_rdmsr(env, (uint32_t)env->regs[R_ECX]);
        break;
#endif /* __Use_Original_Qemu (U1020) */
#if __Use_Original_Qemu != 1 /* ours (U104) */
    case MSR_IA32_UINTR_RR:
        val = env->uintr_rr;
        break;
    case MSR_IA32_UINTR_HANDLER:
        val = env->uintr_handler;
        break;
    case MSR_IA32_UINTR_STACKADJUST:
        val = env->uintr_stackadjust;
        break;
    case MSR_IA32_UINTR_MISC:
        val = env->uintr_misc;
        break;
    case MSR_IA32_UINTR_PD:
        val = env->uintr_pd;
        break;
    case MSR_IA32_UINTR_TT:
        val = env->uintr_tt;
        break;
#endif /* __Use_Original_Qemu (U104) */
    default:
#if __Use_Original_Qemu != 1 /* ours (U907) */
        {   /* the architectural PMU MSRs (storage) */
            X86Pmu p;

            x86_pmu(env, &p);
            if (pmu_msr(env, (uint32_t)env->regs[R_ECX], &p)) {
                val = pmu_read(env, (uint32_t)env->regs[R_ECX], &p);
                break;
            }
        }
#endif /* __Use_Original_Qemu (U907) */
        if ((uint32_t)env->regs[R_ECX] >= MSR_ARCH_LBR_FROM_0 &&
            (uint32_t)env->regs[R_ECX] <
            MSR_ARCH_LBR_FROM_0 + ARCH_LBR_NR_ENTRIES) {
            val = env->lbr_records[(uint32_t)env->regs[R_ECX] -
                                   MSR_ARCH_LBR_FROM_0].from;
            break;
        }
        if ((uint32_t)env->regs[R_ECX] >= MSR_ARCH_LBR_TO_0 &&
            (uint32_t)env->regs[R_ECX] <
            MSR_ARCH_LBR_TO_0 + ARCH_LBR_NR_ENTRIES) {
            val = env->lbr_records[(uint32_t)env->regs[R_ECX] -
                                   MSR_ARCH_LBR_TO_0].to;
            break;
        }
        if ((uint32_t)env->regs[R_ECX] >= MSR_ARCH_LBR_INFO_0 &&
            (uint32_t)env->regs[R_ECX] <
            MSR_ARCH_LBR_INFO_0 + ARCH_LBR_NR_ENTRIES) {
            val = env->lbr_records[(uint32_t)env->regs[R_ECX] -
                                   MSR_ARCH_LBR_INFO_0].info;
            break;
        }
        if ((uint32_t)env->regs[R_ECX] >= MSR_MC0_CTL
            && (uint32_t)env->regs[R_ECX] < MSR_MC0_CTL +
            (4 * env->mcg_cap & 0xff)) {
            uint32_t offset = (uint32_t)env->regs[R_ECX] - MSR_MC0_CTL;
            val = env->mce_banks[offset];
            break;
        }
        /* XXX: exception? */
        val = 0;
        break;
    }
    env->regs[R_EAX] = (uint32_t)(val);
    env->regs[R_EDX] = (uint32_t)(val >> 32);
}


static void do_pause(X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;

    /*
     * backport of QEMU 3718523d01: do the gen_eob() tasks before going back to the
     * main loop - no interrupt shadow, RF cleared, and the single-step trap after
     * PAUSE when TF = 1 (SDM Vol3B 20.3.1.4)
     */
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    env->eflags &= ~RF_MASK;
    helper_rechecking_single_step(env);

    /* Just let another CPU run.  */
    cs->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit(cs);
}

static void do_hlt(X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;

    env->hflags &= ~HF_INHIBIT_IRQ_MASK; /* needed if sti is just before */
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

void helper_hlt(CPUX86State *env, int next_eip_addend)
{
    X86CPU *cpu = env_archcpu(env);

    cpu_svm_check_intercept_param(env, SVM_EXIT_HLT, 0, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_insn_check(env, VMX_EXIT_HLT, GETPC());       /* "HLT exiting" (Vol3C 28.1.3) */
    env->vmx.mwait_wait = 0;
#endif /* __Use_Original_Qemu (U1304) */
    env->eip += next_eip_addend;
#if __Use_Original_Qemu != 1 /* ours (U1305) */
    x86_vmx_hlt(env);   /* VMX events that end HLT at once: VM exit, activity state HLT */
#endif /* __Use_Original_Qemu (U1305) */
#if __Use_Original_Qemu != 1 /* ours (U1461) */
    env->smm_hlt = 1;   /* the HALT state an SMI records (auto HALT restart, smm_helper.c) */
#endif /* __Use_Original_Qemu (U1461) */

    do_hlt(cpu);
}

void helper_monitor(CPUX86State *env, target_ulong ptr)
{
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* "MONITOR exiting" (Vol3C 28.1.3): before the ECX #GP (28.1.1) */
    x86_vmx_insn_check(env, VMX_EXIT_MONITOR, GETPC());
#endif /* __Use_Original_Qemu (U1304) */
    if ((uint32_t)env->regs[R_ECX] != 0) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    env->vmx.monitor_armed = 1;     /* address-range monitoring armed (the MWAIT exit qualification) */
#endif /* __Use_Original_Qemu (U1304) */
#if __Use_Original_Qemu != 1 /* ours (U1621) */
    /* MONITOR after UMONITOR: UMWAIT will not wait (SDM 093 Vol2B UMWAIT p. 4-743) */
    x86_umonitor_disarm(env);
#endif /* __Use_Original_Qemu (U1621) */
    /* XXX: store address? */
    cpu_svm_check_intercept_param(env, SVM_EXIT_MONITOR, 0, GETPC());
}

void helper_mwait(CPUX86State *env, int next_eip_addend)
{
    CPUState *cs = env_cpu(env);
    X86CPU *cpu = env_archcpu(env);

#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* "MWAIT exiting" (Vol3C 28.1.3): before the ECX #GP (28.1.1) */
    x86_vmx_mwait_check(env, GETPC());
#endif /* __Use_Original_Qemu (U1304) */
    if ((uint32_t)env->regs[R_ECX] != 0) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }

    cpu_svm_check_intercept_param(env, SVM_EXIT_MWAIT, 0, GETPC());
    env->eip += next_eip_addend;
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    env->vmx.monitor_armed = 0;
    env->vmx.mwait_wait = 1;        /* the wait is not the HLT activity state (Vol3C 30.1) */
#endif /* __Use_Original_Qemu (U1304) */

    /* XXX: not complete but not completely erroneous */
    // if (cs->cpu_index != 0 || CPU_NEXT(cs) != NULL) { // TODO
    if (cs->cpu_index != 0) {
        // do_pause(cpu);
    } else {
        do_hlt(cpu);
    }
}

void helper_pause(CPUX86State *env, int next_eip_addend)
{
    X86CPU *cpu = env_archcpu(env);

    cpu_svm_check_intercept_param(env, SVM_EXIT_PAUSE, 0, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_insn_check(env, VMX_EXIT_PAUSE, GETPC());     /* "PAUSE exiting" (Vol3C 28.1.3) */
#endif /* __Use_Original_Qemu (U1304) */
    env->eip += next_eip_addend;

    do_pause(cpu);
}

void helper_debug(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

#if __Use_Original_Qemu != 1 /* ours (U110) */
/*
 * NoVmp (ledger U110): XBEGIN outside 64-bit mode, #GP(0) if the fallback EIP
 * is beyond the CS limit; in real-address and virtual-8086 mode if it is outside
 * 0000H-FFFFH (SDM Vol2 XBEGIN exceptions). Raised before any state changes.
 * Raw Unicorn starts UC_MODE_32 with an all-zero CS cache (no descriptor ever
 * loaded, P = 0, which a real CS cannot be); then there is no limit to check.
 */
void helper_xbegin_check(CPUX86State *env, target_ulong fallback_eip)
{
    uint32_t limit = env->segs[R_CS].limit;

    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        limit = 0xffff;
    } else if (!(env->segs[R_CS].flags & DESC_P_MASK)) {
        return;
    }
    if (fallback_eip > limit) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
}

#endif /* __Use_Original_Qemu (U110) */
#if __Use_Original_Qemu != 1 /* ours (U111) */
/*
 * NoVmp (ledger U111): UMWAIT r32 / TPAUSE r32 (SDM Vol2 UMWAIT, TPAUSE).
 * #GP(0) if src[31:1] != 0 or CR4.TSD = 1 at CPL > 0. The pseudocode is
 * evaluated with the TSC RDTSC would return (without hooks, IA32_TSC_ADJUST
 * included since U1600): os_deadline = TSC + IA32_UMWAIT_CONTROL[31:2] quanta
 * (0 = no maximum time, per the MSR description); AF/PF/SF/ZF/OF = 0.
 * Timing (U1620, below): the wait advances the emulated TSC, no real sleep.
 */
#if __Use_Original_Qemu != 1 /* ours (U1621) */
/*
 * NoVmp (ledger U1621, plan 1.J.55.a): the UMONITOR address-range monitor (SDM 093 Vol2B UMONITOR pp. 4-740 /
 * 4-741, UMWAIT pp. 4-742 / 4-743). UMONITOR arms the line of the linear address, CPUID.05H:EBX[15:0] bytes
 * ("the address range that the monitoring hardware checks for store operations can be determined by using the
 * CPUID.05H"; 64 when that field is not a power of two of at least 16), as a write watch of the CPU
 * (BP_MONITOR, exec.c): "A store to an address within the specified address range triggers the monitoring
 * hardware" - every store of the CPU that overlaps the line (cpu_check_watchpoint) and every uc_mem_write
 * overlapping its physical line (a store by a non-processor agent, x86_umonitor_store) set BP_MONITOR_HIT.
 * UMWAIT waits only while the range is armed and not triggered ("WHILE monitor hardware armed AND TSC <
 * deadline"). The arm ends with a new UMONITOR, MONITOR, IRET, SYSRET, SYSEXIT, ERETU (Vol3A 8.4.2.4), a far
 * RET that changes CPL, a uc_context_restore (a thread switch returns to user mode through IRET / SYSRET), the
 * reset and uc_close. U1852: also every UMWAIT when it returns (the bare host disarms after an ended UMWAIT;
 * before, our reading kept the range armed, the SDM lists no such event).
 */
static uint32_t umon_line_size(CPUX86State *env)
{
    uint32_t eax, ebx, ecx, edx, size;

    cpu_x86_cpuid(env, 5, 0, &eax, &ebx, &ecx, &edx);
    size = ebx & 0xffff;    /* "Largest monitor-line size in bytes" */
    if (size < 16 || (size & (size - 1))) {
        size = 64;
    }
    return size;
}

void x86_umonitor_disarm(CPUX86State *env)
{
    if (env->umon_wp) {
        cpu_watchpoint_remove_by_ref(env_cpu(env), env->umon_wp);
        env->umon_wp = NULL;
    }
}

void helper_umonitor(CPUX86State *env, target_ulong addr)
{
    CPUState *cs = env_cpu(env);
    uint32_t size = umon_line_size(env);
    target_ulong line = addr & ~(target_ulong)(size - 1);
    MemTxAttrs attrs;
    hwaddr pa;

    x86_umonitor_disarm(env);
    if (cpu_watchpoint_insert(cs, line, size, BP_MEM_WRITE | BP_MONITOR, &env->umon_wp) != 0) {
        env->umon_wp = NULL;
        return;
    }
    pa = cpu_get_phys_page_attrs_debug(cs, line & TARGET_PAGE_MASK, &attrs);
    env->umon_pa = (pa == (hwaddr)-1) ? (uint64_t)-1 : (uint64_t)pa + (line & ~TARGET_PAGE_MASK);
}

/* a uc_mem_write of [pa, pa + len) (unicorn.c): a store by a non-processor agent triggers an armed line */
void x86_umonitor_store(CPUX86State *env, uint64_t pa, uint64_t len)
{
    uint64_t size;

    if (!env->umon_wp || env->umon_pa == (uint64_t)-1 || len == 0) {
        return;
    }
    size = env->umon_wp->len;
    if (pa <= env->umon_pa + size - 1 && env->umon_pa <= pa + len - 1) {
        env->umon_wp->flags |= BP_MONITOR_HIT;
    }
}

static bool umon_armed(CPUX86State *env)
{
    return env->umon_wp != NULL && !(env->umon_wp->flags & BP_MONITOR_HIT);
}

#endif /* __Use_Original_Qemu (U1621) */
#if __Use_Original_Qemu != 1 /* ours (U1620) */
/*
 * NoVmp (ledger U1620, plan 1.J.55.a, user decision 2026-10-18 "Advance TSC to deadline"): the wait advances
 * the emulated TSC. "The instruction execution wakes up when the time-stamp counter reaches or exceeds the
 * implicit EDX:EAX 64-bit input value" / the OS limit ends it with CF = 1 (SDM 093 Vol2B TPAUSE pp. 4-729 /
 * 4-730, UMWAIT pp. 4-742 / 4-743): with deadline = min(EDX:EAX, TSC + (IA32_UMWAIT_CONTROL[31:2] << 2)) and
 * TSC < deadline (UMWAIT: only while the UMONITOR range is armed, U1621) the counter RDTSC reads jumps to the
 * deadline - env->tsc_wait grows by the delay (in VMX non-root operation the physical delay of Vol3C 28.3,
 * x86_vmx_wait_delay). No real sleep, no host-time dependence of the result. CF = using_os_deadline AND TSC >=
 * deadline, so CF = 1 exactly when the OS limit ended the wait (also when os_deadline wrapped below the TSC).
 * src[0] (C0.1 / C0.2) and IA32_UMWAIT_CONTROL[0] choose a power state only: the same wait. One wake event is
 * modelled (U1622): the environment's timer interrupt, W ticks after the start (CF = 0). Other wake events
 * (NMI, SMI, debug exceptions, stores by other agents during the wait) are not: nothing else runs while the
 * instruction waits. umwait = 1: UMWAIT, 0: TPAUSE.
 */
#endif /* __Use_Original_Qemu (U1620) */
void helper_waitpkg(CPUX86State *env, uint32_t src, uint32_t umwait)
{
    uint64_t tsc, instr_deadline, deadline, limit;
    bool using_os_deadline = false;

    if ((src & ~1u) || ((env->cr[4] & CR4_TSD_MASK)
                        && (env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
#if __Use_Original_Qemu != 1 /* ours (U1600) */
    /*
     * NoVmp (ledger U1600, plan 1.J.55): the Operation's "TSC" is the counter RDTSC reads, IA32_TSC_ADJUST included
     * (SDM 093 Vol3B 20.17.3 p. 20-44: a WRMSR to IA32_TIME_STAMP_COUNTER moves IA32_TSC_ADJUST by the same amount;
     * U905); before, the sample left IA32_TSC_ADJUST out. 64-bit arithmetic: an os_deadline past 2^64 wraps below
     * the instruction deadline, and the single sample is then already past it (CF = 1, docs/quirks.md).
     */
    tsc = msr_tsc_now(env);
#endif /* __Use_Original_Qemu (U1600) */
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    tsc = x86_vmx_guest_tsc(env, tsc);  /* the deadline is in guest TSC units (Vol3C 28.3) */
#endif /* __Use_Original_Qemu (U1304) */
    instr_deadline = ((uint64_t)(uint32_t)env->regs[R_EDX] << 32)
                     | (uint32_t)env->regs[R_EAX];
    deadline = instr_deadline;
    limit = env->umwait & ~3u;
    if (limit != 0 && tsc + limit < instr_deadline) {
        deadline = tsc + limit;
        using_os_deadline = true;
    }
#if __Use_Original_Qemu != 1 /* ours (U1620) */
    if (tsc < deadline && (!umwait || umon_armed(env))) {
        uint64_t end = deadline;
#if __Use_Original_Qemu != 1 /* ours (U1622) */
        /*
         * NoVmp (ledger U1622, user decision 2026-10-18 "Wake after ~1 ms"): the environment's wake event
         * (Windows on the bare host: the timer interrupt; measured 2026-10-18, docs/quirks.md) ends the
         * wait at TSC + W when that comes before the deadline: "an external interrupt causes the processor
         * to exit the implementation-dependent optimized state regardless of whether maskable-interrupts are
         * inhibited" (SDM 093 Vol2B TPAUSE p. 4-730). Not the OS time limit, so CF = 0 (TSC < deadline
         * below). W = UC_CTL_X86_WAIT_WAKE (default 2^21 ticks; 0 = none, the wait runs to the deadline).
         */
        if (env->wait_wake != 0 && end - tsc > env->wait_wake) {
            end = tsc + env->wait_wake;
        }
#endif /* __Use_Original_Qemu (U1622) */
        env->tsc_wait += x86_vmx_wait_delay(env, end - tsc);
        tsc = end;          /* the wait ended at the deadline (or the wake event) */
    }
#endif /* __Use_Original_Qemu (U1620) */
#if __Use_Original_Qemu != 1 /* ours (U1852) */
    /*
     * NoVmp (ledger U1852, H22 item 5, H4 item 1): a UMWAIT that ended disarms the UMONITOR range - a second UMWAIT
     * without a new UMONITOR does not wait. The i5-13600K on the bare host, P- and E-cores (capture
     * 20261022_041918_native-host, waitpkg_rearm_hostP.log / _hostE.log, cases_waitpkg_rearm_probe_hw lines A and C: the
     * second wait returns at once, also with a far deadline; line B with a fresh UMONITOR waits). SDM 253667-093 Vol2B
     * UMWAIT pp. 4-742 / 4-743 names IRET, MONITOR, SYSEXIT, SYSRET and a far RET changing CPL as the events that end
     * the arm and says nothing about an ended UMWAIT: hardware-decided (docs/quirks.md). Our reading where nothing was
     * measured: every UMWAIT that passed its #GP checks disarms when it returns, also one that did not wait (deadline in
     * the past, the line already triggered, no range armed). TPAUSE leaves the range alone.
     */
    if (umwait) {
        x86_umonitor_disarm(env);
    }
#endif /* __Use_Original_Qemu (U1852) */
    CC_SRC = (using_os_deadline && tsc >= deadline) ? CC_C : 0;
}

#endif /* __Use_Original_Qemu (U111) */
#if __Use_Original_Qemu != 1 /* ours (U113) */
/*
 * NoVmp (ledger U113): GETSEC (SDM Vol2 chapter 7) on a platform without an
 * Intel TXT-capable chipset. #UD if CR4.SMXE = 0. GETSEC[CAPABILITIES] (EAX = 0)
 * may run at any CPL: EAX = 0 for every EBX index (bit 0 chipset not present,
 * bits 1-30 no GETSEC leaf available, bit 31 no extended leaves). Every other
 * leaf is "not reported as supported by GETSEC[CAPABILITIES]": #UD, before any
 * CPL/mode #GP condition of that leaf.
 */
#if __Use_Original_Qemu != 1 /* ours (U1261) */
/*
 * NoVmp (ledger U1261): INVD with the SGX model - "Once processor reserved memory protections are
 * activated (see Section 39.5), any execution of INVD will result in a #GP(0)" (SDM 092 Vol3D
 * 39.6.5; Vol2A INVD). When they are activated is our choice (x86_sgx_prm_active, docs/sgx.md 3).
 * The translator calls this only when the model enumerates SGX, after the CPL check.
 */
void helper_sgx_invd(CPUX86State *env)
{
    if (x86_sgx_prm_active(env)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
}

#endif /* __Use_Original_Qemu (U1261) */
#if __Use_Original_Qemu != 1 /* ours (U1267) */
/*
 * NoVmp (ledger U1267): GETSEC with UC_X86_FEATURE_SMX - an SMX processor on a platform without an
 * Intel TXT-capable chipset (SDM 092 Vol2D 7.2, 7.3, the leaf Operations p. 7-8..7-43; docs/sgx.md
 * 1.7). Order of every Operation: #UD for CR4.SMXE = 0; (VMX non-root operation: VM exit - plan
 * 1.J.12, pending); #UD for a leaf CAPABILITIES does not report (1, 9-FFFFFFFFH).
 * CAPABILITIES (any CPL): EBX = 0 -> leaves ENTERACCS .. WAKEUP (bits 8:2) available, Chipset
 * Present (bit 0) = 0, no extended leaves (bit 31); EBX != 0 -> 0. PARAMETERS (any CPL): the
 * default values of Table 7-10 as the model's parameters - type 1 (AC module HeaderVersion 0.0
 * only: EAX = 1, EBX = FFFFFFFFH, ECX = 0), type 2 (32 KB authenticated code area: 8002H), type 3
 * (UC only: 103H), then NULL (EAX = 0, EBX / ECX unmodified). The other leaves end in their #GP(0)
 * group, which always holds here: ENTERACCS / SENTER / SEXIT / WAKEUP "TXT chipset not present",
 * EXITAC "ACMODEFLAG = 0", SMCTRL "SENTERFLAG = 0" (no measured environment can be launched
 * without a chipset). The chipset, the AC module format, TPM and the measured environment need
 * the Intel TXT MLE Developer's Guide / chipset documentation (docs/sgx.md 4).
 */
static void smx_getsec(CPUX86State *env, uintptr_t ra)
{
    uint32_t leaf = (uint32_t)env->regs[R_EAX];

    if (!(env->cr[4] & CR4_SMXE_MASK) || leaf == 1 || leaf > 8) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    switch (leaf) {
    case 0:     /* CAPABILITIES */
        env->regs[R_EAX] = (uint32_t)env->regs[R_EBX] == 0 ? 0x1fc : 0;
        break;
    case 6:     /* PARAMETERS */
        switch ((uint32_t)env->regs[R_EBX]) {
        case 0:
            env->regs[R_EAX] = 1;
            env->regs[R_EBX] = 0xffffffffu;
            env->regs[R_ECX] = 0;
            break;
        case 1:
            env->regs[R_EAX] = 0x8002;
            break;
        case 2:
            env->regs[R_EAX] = 0x103;
            break;
        default:
            env->regs[R_EAX] = 0;
            break;
        }
        break;
    default:    /* ENTERACCS, EXITAC, SENTER, SEXIT, SMCTRL, WAKEUP */
        raise_exception_err_ra(env, EXCP0D_GPF, 0, ra);
    }
}

#endif /* __Use_Original_Qemu (U1267) */
void helper_getsec(CPUX86State *env)
{
#if __Use_Original_Qemu != 1 /* ours (U1303) */
    /* VMX non-root operation: a VM exit if CR4.SMXE = 1, whatever CPL and EAX (Vol3C 28.1.2
       footnote 1); #UD with CR4.SMXE = 0 */
    if (env->cr[4] & CR4_SMXE_MASK) {
        x86_vmx_uncond_exit(env, VMX_EXIT_GETSEC, GETPC());
    }
#endif /* __Use_Original_Qemu (U1303) */
#if __Use_Original_Qemu != 1 /* ours (U1267) */
    if (env->uc->x86_features & UC_X86_FEATURE_SMX) {
        smx_getsec(env, GETPC());
        return;
    }
#endif /* __Use_Original_Qemu (U1267) */
    if (!(env->cr[4] & CR4_SMXE_MASK) || (uint32_t)env->regs[R_EAX] != 0) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }
    env->regs[R_EAX] = 0;
}

/*
 * NoVmp (ledger U113, replaced by U1021): helper_pconfig is now the PCONFIG Operation at the end
 * of this file (U113 raised #UD for the TSE leaves in real-address mode; the SDM 092 Operation
 * checks the target first and raises #GP(0)).
 */

#endif /* __Use_Original_Qemu (U113) */
#if __Use_Original_Qemu != 1 /* ours (U114) */
/*
 * NoVmp (ledger U114): CET shadow-stack management instructions (SDM Vol1
 * 18.2, Vol2 INCSSP, SAVEPREVSSP, RSTORSSP, WRSS, WRUSS, SETSSBSY, CLRSSBSY).
 * The translator has already raised #UD for LOCK, real-address/virtual-8086
 * mode and - for INCSSP/SAVEPREVSSP/RSTORSSP/WRSS - when HF_CET_SS
 * (ShadowStackEnabled(CPL)) is clear; RDSSP is done inline.
 *
 * Shadow-stack accesses: a user access at CPL 3, a supervisor access otherwise
 * (WRUSS: always user). Outside 64-bit mode SSP and the shadow-stack addresses
 * are 32 bits wide (Vol1 18.2.1). They use their own MMU modes, which with
 * paging allow only shadow-stack pages (Vol3 5.6, U117); without paging there
 * are no page types and every linear address may be accessed.
 */
static bool cet_lm(CPUX86State *env)
{
    return env->hflags & HF_CS64_MASK;      /* IA32_EFER.LMA AND CS.L */
}

static target_ulong cet_la(CPUX86State *env, uint64_t a)
{
    return cet_lm(env) ? a : (uint32_t)a;
}

static int cet_ss_idx(CPUX86State *env, bool user)
{
    /* the shadow-stack MMU modes apply the page-type rules (U117) */
    return (user || (env->hflags & HF_CPL_MASK) == 3) ? MMU_SS_USER_IDX : MMU_SS_KSMAP_IDX;
}

/*
 * The linear address of a shadow-stack access. It is not an SS-segment (stack)
 * reference, so a non-canonical one is #GP(0), never #SS (U51 classifies the
 * faulting instruction, e.g. a CALL, as a stack user).
 */
static target_ulong ss_addr(CPUX86State *env, uint64_t a, uintptr_t ra)
{
    if (cet_lm(env) && !cet_canonical(env, a)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    return cet_la(env, a);
}

static uint64_t ss_ld8(CPUX86State *env, uint64_t a, bool user, uintptr_t ra)
{
    return cpu_ldq_mmuidx_ra(env, ss_addr(env, a, ra), cet_ss_idx(env, user), ra);
}

static uint32_t ss_ld4(CPUX86State *env, uint64_t a, bool user, uintptr_t ra)
{
    return cpu_ldl_mmuidx_ra(env, ss_addr(env, a, ra), cet_ss_idx(env, user), ra);
}

static void ss_st8(CPUX86State *env, uint64_t a, uint64_t v, bool user, uintptr_t ra)
{
    cpu_stq_mmuidx_ra(env, ss_addr(env, a, ra), v, cet_ss_idx(env, user), ra);
}

static void ss_st4(CPUX86State *env, uint64_t a, uint32_t v, bool user, uintptr_t ra)
{
    cpu_stl_mmuidx_ra(env, ss_addr(env, a, ra), v, cet_ss_idx(env, user), ra);
}

static void cet_set_ssp(CPUX86State *env, uint64_t v)
{
    env->ssp = cet_lm(env) ? v : (uint32_t)v;
}

/* INCSSPD r32 / INCSSPQ r64: pop and discard the first and last of r[7:0] elements */
void helper_incssp(CPUX86State *env, target_ulong src, uint32_t q)
{
    uintptr_t ra = GETPC();
    uint64_t n = src & 0xff, sz = q ? 8 : 4, ssp = env->ssp;

    if (q) {
        ss_ld8(env, ssp, false, ra);
        if (n) {
            ss_ld8(env, ssp + sz * (n - 1), false, ra);
        }
    } else {
        ss_ld4(env, ssp, false, ra);
        if (n) {
            ss_ld4(env, ssp + sz * (n - 1), false, ra);
        }
    }
    cet_set_ssp(env, ssp + n * sz);
}

/* SAVEPREVSSP: restore-shadow-stack token on the previous shadow stack */
void helper_saveprevssp(CPUX86State *env)
{
    uintptr_t ra = GETPC();
    bool lm = cet_lm(env);
    uint64_t ssp = cet_la(env, env->ssp), prev, old;
    bool cf = cpu_cc_compute_all(env, CC_OP) & CC_C;

    if (ssp & 7) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    prev = ss_ld8(env, ssp, false, ra);
    ssp += 8;
    if (cf && lm) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (cf) {
        if (ss_ld4(env, ssp, false, ra) != 0) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        ssp += 4;
    }
    if (!(prev & 2) || (!lm && (prev >> 32))) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    old = prev & ~3ull;
    ss_st4(env, old - 4, 0, false, ra);
    ss_st8(env, (old & ~7ull) - 8, old | lm, false, ra);
    cet_set_ssp(env, ssp);
}

/* RSTORSSP m64: switch to the shadow stack whose restore token is at m64 */
void helper_rstorssp(CPUX86State *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    uint64_t lm = cet_lm(env), tok, prev_tok;
    bool fault = false;

    if (addr & 7) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    prev_tok = cet_la(env, env->ssp) | lm | 2;
    tok = ss_ld8(env, addr, false, ra);
    if ((tok & 3) != lm || (!lm && (tok >> 32)) ||
        (((tok & ~1ull) - 8) & ~7ull) != addr) {
        fault = true;
    }
    ss_st8(env, addr, fault ? tok : prev_tok, false, ra);
    if (fault) {
        raise_exception_err_ra(env, EXCP15_CP, CP_RSTORSSP, ra);
    }
    cet_set_ssp(env, addr);
    CC_SRC = (tok & 4) ? CC_C : 0;
}

/* WRSSD/WRSSQ (user = 0) and WRUSSD/WRUSSQ (user = 1) m, r */
void helper_wrss(CPUX86State *env, target_ulong addr, target_ulong val,
                 uint32_t q, uint32_t user)
{
    uintptr_t ra = GETPC();
    bool cpl3 = (env->hflags & HF_CPL_MASK) == 3;

    if (user) {
        if (!(env->cr[4] & CR4_CET_MASK)) {
            raise_exception_ra(env, EXCP06_ILLOP, ra);
        }
        if (env->hflags & HF_CPL_MASK) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
    } else if (!((cpl3 ? env->u_cet : env->s_cet) & CET_WR_SHSTK_EN)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (addr & (q ? 7 : 3)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (q) {
        ss_st8(env, addr, val, user, ra);
    } else {
        ss_st4(env, addr, (uint32_t)val, user, ra);
    }
}

/* SETSSBSY / CLRSSBSY: common #UD / #GP checks (there is no CR4.FRED here) */
static void cet_busy_check(CPUX86State *env, uintptr_t ra)
{
#if __Use_Original_Qemu != 1 /* ours (U1171) */
    /* SDM 092 Vol2B SETSSBSY / Vol2A CLRSSBSY: "IF CR4.CET = 0 OR CR4.FRED = 1 THEN #UD" */
    if (x86_fred_enabled(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
#endif /* __Use_Original_Qemu (U1171) */
    if (!(env->cr[4] & CR4_CET_MASK) || !(env->s_cet & CET_SH_STK_EN)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (env->hflags & HF_CPL_MASK) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
}

/* SETSSBSY: mark the supervisor shadow-stack token at IA32_PL0_SSP busy; SSP = it */
void helper_setssbsy(CPUX86State *env)
{
    uintptr_t ra = GETPC();
    uint64_t la, old;

    cet_busy_check(env, ra);
    la = cet_la(env, env->pl_ssp[0]);
    if (la & 7) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    old = ss_ld8(env, la, false, ra);
    ss_st8(env, la, old == la ? (la | 1) : old, false, ra);
    if (old != la) {
        raise_exception_err_ra(env, EXCP15_CP, CP_SETSSBSY, ra);
    }
    cet_set_ssp(env, la);
}

/* CLRSSBSY m64: clear the busy flag of the token at m64; CF = invalid token; SSP = 0 */
void helper_clrssbsy(CPUX86State *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    uint64_t old;

    cet_busy_check(env, ra);
    if (addr & 7) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    old = ss_ld8(env, addr, false, ra);
    ss_st8(env, addr, old == (addr | 1) ? addr : old, false, ra);
    CC_SRC = old == (addr | 1) ? 0 : CC_C;
    env->ssp = 0;
}

#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U115) */
/*
 * NoVmp (ledger U115): near CALL / near RET with shadow stacks enabled at the
 * CPL (SDM Vol2 CALL, RET). The translator calls these only when HF_CET_SS is
 * set. CALL: after the data-stack store and before RSP moves, ShadowStackPush8B
 * (RIP) in 64-bit mode, else ShadowStackPush4B(EIP, or IP zero-extended); a
 * fault leaves RSP and SSP unchanged. RET: after the data-stack load, the
 * return address is popped from the shadow stack and compared (8 bytes in
 * 64-bit mode, else 4); a mismatch is #CP(NEAR-RET) with RSP and SSP unchanged.
 */
void helper_ss_call(CPUX86State *env, target_ulong ret_ip)
{
    uintptr_t ra = GETPC();
    uint64_t ssp = env->ssp;

    if (cet_lm(env)) {
        ss_st8(env, ssp - 8, ret_ip, false, ra);
        cet_set_ssp(env, ssp - 8);
    } else {
        ss_st4(env, ssp - 4, (uint32_t)ret_ip, false, ra);
        cet_set_ssp(env, ssp - 4);
    }
}

void helper_ss_ret(CPUX86State *env, target_ulong ret_ip)
{
    uintptr_t ra = GETPC();
    uint64_t ssp = env->ssp;

    if (cet_lm(env)) {
        if (ss_ld8(env, ssp, false, ra) != (uint64_t)ret_ip) {
            raise_exception_err_ra(env, EXCP15_CP, CP_NEAR_RET, ra);
        }
        cet_set_ssp(env, ssp + 8);
    } else {
        if (ss_ld4(env, ssp, false, ra) != (uint32_t)ret_ip) {
            raise_exception_err_ra(env, EXCP15_CP, CP_NEAR_RET, ra);
        }
        cet_set_ssp(env, ssp + 4);
    }
}

#endif /* __Use_Original_Qemu (U115) */
#if __Use_Original_Qemu != 1 /* ours (U780) */
/*
 * NoVmp (ledger U780): a near CALL with shadow stacks stores the return address on the data
 * stack, then pushes it on the shadow stack (helper_ss_call). A fault on the shadow-stack
 * push (#PF with the SS bit, #GP(0) for a non-canonical SSP, memory Unicorn has not mapped
 * or maps read-only) left the data-stack slot written although RSP, SSP and RIP were
 * unchanged; SDM Vol3A 6.15: the faulting instruction writes nothing. Both slots are now
 * checked first, the data-stack slot before the shadow-stack one (the CALL pseudocode order,
 * so a data-stack fault still comes first), without writing anything (x86_probe_write).
 */
void helper_ss_call_probe(CPUX86State *env, target_ulong data_la, uint32_t size)
{
    uintptr_t ra = GETPC();
    uint32_t sz = cet_lm(env) ? 8 : 4;

    x86_probe_write(env, data_la, size, ra);
    x86_probe_write_la(env, ss_addr(env, env->ssp - sz, ra), sz, cet_ss_idx(env, false), ra);
}

#endif /* __Use_Original_Qemu (U780) */
#if __Use_Original_Qemu != 1 /* ours (U116) */
/*
 * NoVmp (ledger U116): CET indirect branch tracking (SDM Vol1 18.3; Vol2 CALL,
 * JMP, ENDBR32/64). The tracker (TRACKER/SUPPRESS) lives in IA32_U_CET (CPL 3)
 * or IA32_S_CET (CPL < 3). The translator calls these only with HF_CET_IBT
 * (EndbranchEnabled(CPL)) set.
 */
static uint64_t *cet_ibt_msr(CPUX86State *env)
{
    return (env->hflags & HF_CPL_MASK) == 3 ? &env->u_cet : &env->s_cet;
}

static bool cet_ibt_enabled(CPUX86State *env)
{
    return (env->cr[4] & CR4_CET_MASK) && (env->cr[0] & CR0_PE_MASK) &&
           !(env->eflags & VM_MASK) && (*cet_ibt_msr(env) & CET_ENDBR_EN);
}

/* near indirect CALL/JMP: WAIT_FOR_ENDBRANCH unless suppressed or NOTRACK honoured */
void helper_ibt_branch(CPUX86State *env, uint32_t notrack)
{
    uint64_t *cet = cet_ibt_msr(env);

    if (cet_ibt_enabled(env) && !(*cet & CET_SUPPRESS) &&
        !(notrack && (*cet & CET_NO_TRACK_EN))) {
        *cet |= CET_TRACKER;
    }
}

/* far CALL/JMP (protected mode, at the new CPL): WAIT_FOR_ENDBRANCH, unsuppressed */
void helper_ibt_far(CPUX86State *env)
{
    uint64_t *cet = cet_ibt_msr(env);

    if (cet_ibt_enabled(env)) {
        *cet = (*cet & ~CET_SUPPRESS) | CET_TRACKER;
    }
}

/* ENDBR32 / ENDBR64 in their mode, or an RTM abort: TRACKER = IDLE, SUPPRESS = 0 */
void helper_ibt_idle(CPUX86State *env)
{
    if (cet_ibt_enabled(env)) {
        *cet_ibt_msr(env) &= ~(CET_TRACKER | CET_SUPPRESS);
    }
}

/*
 * The first instruction of a TB (the only place a branch target can be): in
 * WAIT_FOR_ENDBRANCH it must be the ENDBR of the mode (kind 1), which makes the
 * tracker IDLE; INT3/INT1 (kind 2) deliver their trap first and keep the state
 * (18.3.5). Anything else is the CET state-machine violation: with LEG_IW_EN
 * the legacy code page bitmap (18.3.6) may allow it (TRACKER = IDLE, SUPPRESS =
 * !SUPPRESS_DIS, the instruction runs), else #CP(ENDBRANCH) at the target,
 * with the tracker still waiting. la = linear address of the instruction.
 */
void helper_ibt_check(CPUX86State *env, target_ulong la, uint32_t kind)
{
    uintptr_t ra = GETPC();
    uint64_t *cet = cet_ibt_msr(env);

    if (!(*cet & CET_TRACKER) || kind == 2) {
        return;
    }
#if __Use_Original_Qemu != 1 /* ours (U1263) */
    /*
     * NoVmp (ledger U1263): ENCLU (kind 4), SDM 092 Vol3D p. 41-10: "IF EndbranchEnabledAndNot-
     * Suppressed(CPL) and TRACKER = WAIT_FOR_ENDBRANCH and (EAX != ERESUME or CR0.TS or (in SMM)
     * or (CPUID.12H.00H:EAX.SGX1 = 0) or (CPL < 3)) THEN Handle CET State machine violation";
     * ERESUME otherwise runs with the tracker still waiting (the state it saves for the exit).
     */
    if (kind == 4) {
        if ((uint32_t)env->regs[R_EAX] == 3 && !(env->cr[0] & CR0_TS_MASK) &&
            !(env->hflags & HF_SMM_MASK) && x86_sgx_enumerated(env) &&
            (env->hflags & HF_CPL_MASK) == 3) {
            return;
        }
        kind = 0;
    }
#endif /* __Use_Original_Qemu (U1263) */
#if __Use_Original_Qemu != 1 /* ours (U758) */
    /* F3 REX2 1E FA (kind 3, U758): ENDBR64 when REX2 is usable (CR4.OSXSAVE, XCR0[APX_F]) */
    if (kind == 3) {
        kind = ((env->cr[4] & CR4_OSXSAVE_MASK) && (env->xcr0 & XSTATE_APX_MASK)) ? 1 : 0;
    }
#endif /* __Use_Original_Qemu (U758) */
    if (kind == 1) {
        *cet &= ~(CET_TRACKER | CET_SUPPRESS);
        return;
    }
    if (*cet & CET_LEG_IW_EN) {
        uint64_t base = *cet & CET_EB_LEG_BITMAP_BASE, idx, a;
        uint8_t byte;

        if (!(env->hflags & HF_CS64_MASK)) {
            idx = (uint32_t)la >> 15;
        } else if (!(env->cr[4] & CR4_LA57_MASK)) {
            idx = (la & ((1ull << 48) - 1)) >> 15;
        } else {
            idx = (la & ((1ull << 57) - 1)) >> 15;
        }
        a = base + idx;
        if (!(env->hflags & HF_LMA_MASK)) {
            a = (uint32_t)a;
        }
        byte = cpu_ldub_data_ra(env, a, ra);
        if (byte & (1u << ((la >> 12) & 7))) {
            *cet = (*cet & ~(CET_TRACKER | CET_SUPPRESS)) |
                   ((*cet & CET_SUPPRESS_DIS) ? 0 : CET_SUPPRESS);
            return;
        }
    }
    raise_exception_err_ra(env, EXCP15_CP, CP_ENDBRANCH, ra);
}

#endif /* __Use_Original_Qemu (U116) */
uint64_t helper_rdpkru(CPUX86State *env, uint32_t ecx)
{
    if ((env->cr[4] & CR4_PKE_MASK) == 0) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
    if (ecx != 0) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }

    return env->pkru;
}

void helper_wrpkru(CPUX86State *env, uint32_t ecx, uint64_t val)
{
    CPUState *cs = env_cpu(env);

    if ((env->cr[4] & CR4_PKE_MASK) == 0) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
    if (ecx != 0 || (val & 0xFFFFFFFF00000000ull)) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }

    env->pkru = val;
    tlb_flush(cs);
}

/*
 * RDPID: DEST := IA32_TSC_AUX (SDM Vol.2B RDPID).  Unicorn is always
 * system emulation, so the user-mode getcpu()/sched_getcpu() variants of
 * upstream 6750485bf4 do not apply.
 */
target_ulong HELPER(rdpid)(CPUX86State *env)
{
    return env->tsc_aux;
}

#if __Use_Original_Qemu != 1 /* ours (U100) */
/*
 * NoVmp (ledger U100): Key Locker (SDM Vol2 LOADIWKEY, ENCODEKEY128/256,
 * AESENC/DEC128KL/256KL, AESENC/DECWIDE128KL/256KL; Intel Key Locker
 * Specification 343965: handle format 1.4, A.3 functions, A.5.2 AES-GCM-SIV
 * C code, which this follows step by step). IWKey is CPUX86State.kl_iwkey.
 * The CPUID.19H of this model is NOVMP_CPUID_19_* (cpu.h); KeySource 1 is
 * not enumerated, so LOADIWKEY never uses randomness.
 */
#include "crypto/aes.h"

typedef struct KLBlock {
    uint64_t lo, hi;    /* bits 63:0, 127:64 (byte 0 = bits 7:0) */
} KLBlock;

static KLBlock kl_ld(const uint8_t *p)
{
    KLBlock r;

    r.lo = ldq_le_p(p);
    r.hi = ldq_le_p(p + 8);
    return r;
}

static void kl_st(uint8_t *p, KLBlock v)
{
    stq_le_p(p, v.lo);
    stq_le_p(p + 8, v.hi);
}

static KLBlock kl_xmm(CPUX86State *env, int n)
{
    KLBlock r;

    r.lo = env->xmm_regs[n].ZMM_Q(0);
    r.hi = env->xmm_regs[n].ZMM_Q(1);
    return r;
}

static void kl_set_xmm(CPUX86State *env, int n, KLBlock v)
{
    /* legacy-SSE write: bits MAXVL-1:128 are unchanged */
    env->xmm_regs[n].ZMM_Q(0) = v.lo;
    env->xmm_regs[n].ZMM_Q(1) = v.hi;
}

/* PCLMULQDQ of two quadwords */
static KLBlock kl_clmul(uint64_t a, uint64_t b)
{
    KLBlock r = { 0, 0 };
    int i;

    for (i = 0; i < 64; i++) {
        if ((b >> i) & 1) {
            r.lo ^= a << i;
            if (i) {
                r.hi ^= a >> (64 - i);
            }
        }
    }
    return r;
}

/*
 * Horner_Step (A.5.2): (A ^ B) * H in the POLYVAL field. POLY =
 * setr_epi32(1, 0, 0, 0xc2000000); the code only uses its high quadword.
 */
static KLBlock kl_horner(KLBlock a, KLBlock b, KLBlock h)
{
    const uint64_t poly_hi = 0xc200000000000000ULL;
    KLBlock t1, t2, t3, t4, sw;
    int i;

    a.lo ^= b.lo;
    a.hi ^= b.hi;
    t1 = kl_clmul(a.lo, h.lo);              /* imm 0x00 */
    t4 = kl_clmul(a.hi, h.hi);              /* imm 0x11 */
    t2 = kl_clmul(a.lo, h.hi);              /* imm 0x10 */
    t3 = kl_clmul(a.hi, h.lo);              /* imm 0x01 */
    t2.lo ^= t3.lo;
    t2.hi ^= t3.hi;
    t1.hi ^= t2.lo;                         /* TMP1 ^= TMP2 << 64 */
    t4.lo ^= t2.hi;                         /* TMP4 ^= TMP2 >> 64 */
    for (i = 0; i < 2; i++) {
        t2 = kl_clmul(t1.lo, poly_hi);      /* clmul(TMP1, POLY, 0x10) */
        sw.lo = t1.hi;                      /* shuffle_epi32(TMP1, 78) */
        sw.hi = t1.lo;
        t1.lo = sw.lo ^ t2.lo;
        t1.hi = sw.hi ^ t2.hi;
    }
    t4.lo ^= t1.lo;
    t4.hi ^= t1.hi;
    return t4;
}

static KLBlock kl_aes(const AES_KEY *k, KLBlock in)
{
    uint8_t b[16];

    kl_st(b, in);
    AES_encrypt(b, b, k);
    return kl_ld(b);
}

/* POLYVAL over AAD, the n plaintext blocks and the length block, bit 127 cleared */
static KLBlock kl_polyval(CPUX86State *env, KLBlock aad, const KLBlock *pt, int n)
{
    KLBlock k1 = kl_ld(env->kl_iwkey), zero = { 0, 0 }, len, s;
    int i;

    s = kl_horner(aad, zero, k1);
    for (i = 0; i < n; i++) {
        s = kl_horner(pt[i], s, k1);
    }
    len.lo = 16 * 8;                        /* LENBLK = setr_epi32(128, 0, n*128, 0) */
    len.hi = (uint64_t)n * 16 * 8;
    s = kl_horner(s, len, k1);
    s.hi &= 0x7fffffffffffffffULL;          /* AND_MASK */
    return s;
}

/* XOR x[0..n) with AES(TAG | TOP_ONE), counter incremented by _mm_add_epi32 on dword 0 */
static void kl_ctr(const AES_KEY *k, KLBlock tag, KLBlock *x, int n)
{
    KLBlock c = tag;
    int i;

    c.hi |= 0x8000000000000000ULL;
    for (i = 0; i < n; i++) {
        KLBlock ks = kl_aes(k, c);
        x[i].lo ^= ks.lo;
        x[i].hi ^= ks.hi;
        c.lo = (c.lo & 0xffffffff00000000ULL) | (uint32_t)(c.lo + 1);
    }
}

/* WrapKey128 / WrapKey256 (A.5.2.5/6): handle = AAD | tag | ciphertext */
static void kl_wrap(CPUX86State *env, KLBlock aad, const KLBlock *key, int n, KLBlock *handle)
{
    AES_KEY k;
    KLBlock tag;
    int i;

    AES_set_encrypt_key(env->kl_iwkey + 16, 256, &k);
    tag = kl_aes(&k, kl_polyval(env, aad, key, n));
    handle[0] = aad;
    handle[1] = tag;
    for (i = 0; i < n; i++) {
        handle[2 + i] = key[i];
    }
    kl_ctr(&k, tag, handle + 2, n);
}

/* UnwrapKeyAndAuthenticate384/512 (A.5.2.7/8): true if authentic */
static bool kl_unwrap(CPUX86State *env, const KLBlock *handle, int n, KLBlock *key)
{
    AES_KEY k;
    KLBlock t;
    int i;

    AES_set_encrypt_key(env->kl_iwkey + 16, 256, &k);
    for (i = 0; i < n; i++) {
        key[i] = handle[2 + i];
    }
    kl_ctr(&k, handle[1], key, n);
    t = kl_aes(&k, kl_polyval(env, handle[0], key, n));
    return t.lo == handle[1].lo && t.hi == handle[1].hi;
}

/* every Key Locker instruction: #UD if CR4.KL = 0 (Key Locker spec 1.3) */
static void kl_check_cr4(CPUX86State *env, uintptr_t ra)
{
    if (!(env->cr[4] & CR4_KL_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
}

/* LOADIWKEY xmm1, xmm2, <EAX>, <XMM0>: F3 0F 38 DC 11:rrr:bbb */
void helper_loadiwkey(CPUX86State *env, uint32_t src1, uint32_t src2)
{
    uintptr_t ra = GETPC();
    uint32_t eax = (uint32_t)env->regs[R_EAX];
    uint32_t keysource = (eax >> 1) & 0xf;

    kl_check_cr4(env, ra);
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    /* "LOADIWKEY exiting" (Vol3C 28.1.3): after the CPL #GP, before the other #GP checks */
    if ((env->hflags & HF_CPL_MASK) != 0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    x86_vmx_loadiwkey_check(env, src1, src2, ra);
#endif /* __Use_Original_Qemu (U1304) */
    if ((env->hflags & HF_CPL_MASK) != 0 ||
        keysource > 1 ||
        (eax >> 5) != 0 ||
        ((eax & 1) && !(NOVMP_CPUID_19_ECX & CPUID_19_ECX_NOBACKUP)) ||
        (keysource == 1 && !(NOVMP_CPUID_19_ECX & CPUID_19_ECX_KEYSOURCE_RANDOM))) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    /* KeySource 0: IntegrityKey = XMM0, EncryptionKey[127:0] = SRC2, [255:128] = SRC1 */
    kl_st(env->kl_iwkey, kl_xmm(env, 0));
    kl_st(env->kl_iwkey + 16, kl_xmm(env, src2));
    kl_st(env->kl_iwkey + 32, kl_xmm(env, src1));
    env->kl_iwkey_nobackup = eax & 1;
    env->kl_iwkey_keysource = keysource;
    CC_SRC = 0;                             /* ZF, OF, SF, AF, PF, CF := 0 */
}

/*
 * ENCODEKEY128 r32, r32 (F3 0F 38 FA 11:rrr:bbb) / ENCODEKEY256 (FB): the
 * key in XMM0 (XMM1:XMM0) becomes the handle in XMM0-2 (XMM0-3); XMM4-6 := 0.
 * Returns DEST (the 32-bit destination register).
 */
target_ulong helper_encodekey(CPUX86State *env, target_ulong src, uint32_t bits)
{
    uintptr_t ra = GETPC();
    uint32_t reserved = 0xfffffff8u | (~NOVMP_CPUID_19_EAX & 7);
    int n = bits == 256 ? 2 : 1;
    KLBlock key[2], handle[4], aad, zero = { 0, 0 };
    int i;

    kl_check_cr4(env, ra);
    if ((uint32_t)src & reserved) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    /* KeyMetadata: [2:0] restrictions, [27:24] KeyType (0 AES-128, 1 AES-256) */
    aad.lo = (src & 7) | ((uint64_t)(n - 1) << 24);
    aad.hi = 0;
    key[0] = kl_xmm(env, 0);
    key[1] = kl_xmm(env, 1);
    kl_wrap(env, aad, key, n, handle);
    for (i = 0; i < 2 + n; i++) {
        kl_set_xmm(env, i, handle[i]);
    }
    for (i = 4; i <= 6; i++) {
        kl_set_xmm(env, i, zero);
    }
    CC_SRC = 0;                             /* OF, SF, ZF, AF, PF, CF := 0 */
    return env->kl_iwkey_nobackup | ((uint32_t)env->kl_iwkey_keysource << 1);
}

/*
 * AESENC128KL / AESDEC128KL / AESENC256KL / AESDEC256KL xmm, m384/m512
 * (F3 0F 38 DC/DD/DE/DF) and AESENCWIDE128KL / AESDECWIDE128KL /
 * AESENCWIDE256KL / AESDECWIDE256KL m384/m512 (F3 0F 38 D8 /0-/3, XMM0-7).
 * op: bit 0 decrypt, bit 1 AES-256, bit 2 wide. ZF := 1 on a handle
 * violation (destination unchanged); the other arithmetic flags := 0.
 */
void helper_aeskl(CPUX86State *env, target_ulong a0, uint32_t reg, uint32_t op)
{
    uintptr_t ra = GETPC();
    bool dec = op & 1, wide = op & 4;
    int n = (op & 2) ? 2 : 1;
    uint32_t cpl = env->hflags & HF_CPL_MASK;
    int first = wide ? 0 : reg, last = wide ? 7 : reg;
    KLBlock handle[4], key[2];
    uint8_t keybytes[32];
    AES_KEY k;
    uint64_t aad;
    bool illegal;
    int i;

    kl_check_cr4(env, ra);
    /* Handle := UnalignedLoad of 384/512 bits (not guaranteed atomic) */
    for (i = 0; i < 2 + n; i++) {
        handle[i].lo = cpu_ldq_data_ra(env, a0 + 16 * i, ra);
        handle[i].hi = cpu_ldq_data_ra(env, a0 + 16 * i + 8, ra);
    }
    aad = handle[0].lo;
    illegal = handle[0].hi != 0 ||                      /* AAD[127:64] reserved */
              (aad & 0xfffffffff0000000ULL) != 0 ||     /* AAD[63:28] reserved */
              (aad & 0x0000000000fffff8ULL) != 0 ||     /* AAD[23:3] reserved */
              ((aad & 1) && cpl > 0) ||                 /* CPL0-only handle */
              (aad & (dec ? 4 : 2)) != 0 ||             /* no-decrypt / no-encrypt */
              ((aad >> 24) & 0xf) != (uint64_t)(n - 1); /* HandleKeyType */
    if (illegal || !kl_unwrap(env, handle, n, key)) {
        CC_SRC = CC_Z;
        return;
    }
    kl_st(keybytes, key[0]);
    kl_st(keybytes + 16, key[1]);
    if (dec) {
        AES_set_decrypt_key(keybytes, 128 * n, &k);
    } else {
        AES_set_encrypt_key(keybytes, 128 * n, &k);
    }
    for (i = first; i <= last; i++) {
        uint8_t b[16];

        kl_st(b, kl_xmm(env, i));
        if (dec) {
            AES_decrypt(b, b, &k);
        } else {
            AES_encrypt(b, b, &k);
        }
        kl_set_xmm(env, i, kl_ld(b));
    }
    memset(keybytes, 0, sizeof(keybytes));
    memset(&k, 0, sizeof(k));
    CC_SRC = 0;
}
#endif /* __Use_Original_Qemu (U100) */

#if __Use_Original_Qemu != 1 /* ours (U103) */
/*
 * NoVmp (ledger U103): URDMSR / UWRMSR (SDM Vol2; Vol4 IA32_USER_MSR_CTL).
 * #UD while IA32_USER_MSR_CTL.ENABLE = 0; #GP if MSR address[63:14] != 0 or
 * its bit in the user-MSR bitmap (low 2 KB: URDMSR, high 2 KB: UWRMSR, at
 * IA32_USER_MSR_CTL[63:12], implicit supervisor-mode reads) is 0. (The SDM
 * Vol2 exception list prints the URDMSR bitmap case under #UD with the range
 * 0-3FFH; its Description says #GP and 0H-3FFFH, which is modelled.) The
 * access itself is RDMSR / WRMSR (same semantics, same hooks); UWRMSR
 * accepts only IA32_UARCH_MISC_CTL (1B01H) and #GPs on its reserved bits.
 */
static void user_msr_check(CPUX86State *env, target_ulong msr, bool write, uintptr_t ra)
{
    target_ulong byte;

    if (!(env->msr_user_msr_ctl & 1)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }
    if (msr >> 14) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    byte = (env->msr_user_msr_ctl & ~(target_ulong)0xfff) + (write ? 2048 : 0) + (msr >> 3);
    if (!((cpu_ldub_mmuidx_ra(env, byte, cpu_mmu_index_kernel(env), ra) >> (msr & 7)) & 1)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
}

/*
 * NoVmp (ledger U908): the access goes through x86_msr_access (U802): RAX, RCX and RDX are put back
 * when RDMSR / WRMSR faults (an MSR the model lacks is #GP(0) since U905), so a faulting URDMSR /
 * UWRMSR leaves them unchanged (before, the fault kept ECX = the MSR number and, for UWRMSR,
 * EDX:EAX = the value).
 */
target_ulong helper_urdmsr(CPUX86State *env, target_ulong msr)
{
    user_msr_check(env, msr, false, GETPC());
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_user_msr_check(env, (uint32_t)msr, false, GETPC());     /* Vol3C 28.1.3 */
#endif /* __Use_Original_Qemu (U1304) */
    return x86_msr_access(env, (uint32_t)msr, 0, false);
}

void helper_uwrmsr(CPUX86State *env, target_ulong msr, target_ulong val)
{
    uintptr_t ra = GETPC();

    user_msr_check(env, msr, true, ra);
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_user_msr_check(env, (uint32_t)msr, true, ra);          /* Vol3C 28.1.3 */
#endif /* __Use_Original_Qemu (U1304) */
    if (msr != MSR_IA32_UARCH_MISC_CTL || (val & ~(target_ulong)1)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    x86_msr_access(env, (uint32_t)msr, val, true);
}
#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U802) */

/*
 * NoVmp (ledger U802): RDMSR / WRMSR of an MSR an instruction names itself (RDMSRLIST /
 * WRMSRLIST table entries, the MSR-IMM immediate forms): helper_rdmsr / helper_wrmsr do the
 * access with their checks, #GP(0) cases and UC_X86_INS_RDMSR / UC_X86_INS_WRMSR hooks, and
 * take the MSR in ECX and the value in EDX:EAX. RAX, RCX and RDX are swapped in for the call
 * and put back afterwards; if the access faults, raise_interrupt2 puts them back
 * (x86_msr_swap_restore), so the faulting instruction leaves them unchanged. The caller has
 * synced EIP (hooks, faults) before calling its helper.
 */
void x86_msr_swap_restore(CPUX86State *env)
{
    if (env->msr_swap) {
        env->regs[R_EAX] = env->msr_swap_regs[0];
        env->regs[R_ECX] = env->msr_swap_regs[1];
        env->regs[R_EDX] = env->msr_swap_regs[2];
        env->msr_swap = false;
    }
}

uint64_t x86_msr_access(CPUX86State *env, uint32_t msr, uint64_t val, bool write)
{
    env->msr_swap_regs[0] = env->regs[R_EAX];
    env->msr_swap_regs[1] = env->regs[R_ECX];
    env->msr_swap_regs[2] = env->regs[R_EDX];
    env->msr_swap = true;
    env->regs[R_ECX] = msr;
    if (write) {
        env->regs[R_EAX] = (uint32_t)val;
        env->regs[R_EDX] = (uint32_t)(val >> 32);
        helper_wrmsr(env);
    } else {
        helper_rdmsr(env);
        val = (uint32_t)env->regs[R_EAX] | ((uint64_t)(uint32_t)env->regs[R_EDX] << 32);
    }
    x86_msr_swap_restore(env);
    return val;
}

/*
 * NoVmp (ledger U802): RDMSRLIST (F2 0F 01 C6) / WRMSRLIST (F3 0F 01 C6), SDM Vol2B/2D, 64-bit
 * mode, CPL0 (checked by the translator). #GP(0) if RSI[2:0] or RDI[2:0] != 0. Then, while RCX
 * != 0, for the lowest set bit n: the MSR address = 8 bytes at linear address RSI + 8n (bits
 * 63:32 != 0: #GP(0)); RDMSRLIST stores the MSR value to RDI + 8n, WRMSRLIST loads it from
 * RDI + 8n and writes the MSR (any RDMSR / WRMSR #GP is #GP(0)); then RCX[n] := 0. A fault
 * leaves RCX with the bits of the completed entries cleared and RIP at the instruction (partial
 * completion, like REP string instructions). The table reads/writes are CPL0 data accesses
 * (#PF, SMAP, non-canonical #GP from the MMU). Not modelled: pending interrupts between entries
 * (Unicorn has none), "load ahead", IA32_BARRIER ordering (the model is sequential).
 */
/* a store to memory Unicorn has not mapped: the instruction stops there, like a #PF (U480 style) */
static void msrlist_unicorn_stop(CPUX86State *env, uintptr_t ra)
{
    struct uc_struct *uc = env->uc;

    if (uc->invalid_error != UC_ERR_OK && uc->nested_level > 0 && !uc->cpu->stopped) {
        cpu_loop_exit_restore(uc->cpu, ra);
    }
}

static void msrlist_entry_check(CPUX86State *env, uintptr_t ra)
{
    if ((env->regs[R_ESI] & 7) || (env->regs[R_EDI] & 7)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
}

void helper_rdmsrlist(CPUX86State *env)
{
    uintptr_t ra = GETPC();

#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_msrlist_entry(env, 0, -1, false, 0, ra);  /* no MSR bitmaps: VM exit (Vol3C 28.1.3) */
#endif /* __Use_Original_Qemu (U1304) */
    msrlist_entry_check(env, ra);
    while (env->regs[R_ECX]) {
        int n = ctz64(env->regs[R_ECX]);
        uint64_t a = cpu_ldq_data_ra(env, env->regs[R_ESI] + 8 * (target_ulong)n, ra);
        uint64_t v;

        if (a >> 32) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
#if __Use_Original_Qemu != 1 /* ours (U1304) */
        x86_vmx_msrlist_entry(env, (uint32_t)a, n, false, 0, ra);
#endif /* __Use_Original_Qemu (U1304) */
        v = x86_msr_access(env, (uint32_t)a, 0, false);
        cpu_stq_data_ra(env, env->regs[R_EDI] + 8 * (target_ulong)n, v, ra);
        msrlist_unicorn_stop(env, ra);
        env->regs[R_ECX] &= ~(1ULL << n);
    }
}

void helper_wrmsrlist(CPUX86State *env)
{
    uintptr_t ra = GETPC();

#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_msrlist_entry(env, 0, -1, true, 0, ra);   /* no MSR bitmaps: VM exit (Vol3C 28.1.3) */
#endif /* __Use_Original_Qemu (U1304) */
    msrlist_entry_check(env, ra);
    while (env->regs[R_ECX]) {
        int n = ctz64(env->regs[R_ECX]);
        uint64_t a = cpu_ldq_data_ra(env, env->regs[R_ESI] + 8 * (target_ulong)n, ra);
        uint64_t v;

        if (a >> 32) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        v = cpu_ldq_data_ra(env, env->regs[R_EDI] + 8 * (target_ulong)n, ra);
#if __Use_Original_Qemu != 1 /* ours (U1304) */
        x86_vmx_msrlist_entry(env, (uint32_t)a, n, true, v, ra);   /* after the data read */
#endif /* __Use_Original_Qemu (U1304) */
        x86_msr_access(env, (uint32_t)a, v, true);
        env->regs[R_ECX] &= ~(1ULL << n);
    }
}
#endif /* __Use_Original_Qemu (U802) */
#if __Use_Original_Qemu != 1 /* ours (U803) */
/* NoVmp (ledger U803): RDMSR r64, imm32 / WRMSRNS imm32, r64 (CPL checked by the translator) */
target_ulong helper_rdmsr_imm(CPUX86State *env, uint32_t msr)
{
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_msr_check(env, msr, false, VMX_EXIT_RDMSR_IMM, 0, GETPC());     /* Vol3C 28.1.3 */
#endif /* __Use_Original_Qemu (U1304) */
    return x86_msr_access(env, msr, 0, false);
}

void helper_wrmsr_imm(CPUX86State *env, uint32_t msr, target_ulong val)
{
#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_msr_check(env, msr, true, VMX_EXIT_WRMSRNS_IMM, 0, GETPC());    /* Vol3C 28.1.3 */
#endif /* __Use_Original_Qemu (U1304) */
    x86_msr_access(env, msr, val, true);
}
#endif /* __Use_Original_Qemu (U803) */
#if __Use_Original_Qemu != 1 /* ours (U804) */
/*
 * NoVmp (ledger U804): HRESET (SDM Vol2A): #GP(0) if (EAX AND NOT IA32_HRESET_ENABLE) != 0 (the
 * CPL check is the translator's); otherwise it resets the selected prediction history, which
 * the emulator does not keep: nothing else happens (EAX = 0 is a NOP anyway). U1128: with
 * UC_X86_FEATURE_ITD the Intel Thread Director history (EAX[0]) exists and is cleared.
 */
void helper_hreset(CPUX86State *env)
{
    if ((uint32_t)env->regs[R_EAX] & ~env->msr_hreset_enable) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
#if __Use_Original_Qemu != 1 /* ours (U1128) */
    /*
     * NoVmp (ledger U1128): EAX[0] resets the Intel Thread Director history of this logical
     * processor (CPUID.20H.0:EBX[0]; Vol3B 17.6.10/17.6.11: IA32_THREAD_FEEDBACK_CHAR.Valid is
     * cleared). The state exists with UC_X86_FEATURE_ITD; without it the fields stay 0.
     */
    if (env->regs[R_EAX] & CPUID_20_0_EBX_THREAD_DIRECTOR_HRESET) {
        env->itd_valid = 0;
        env->itd_class = 0;
    }
#endif /* __Use_Original_Qemu (U1128) */
}
#endif /* __Use_Original_Qemu (U804) */
#if __Use_Original_Qemu != 1 /* ours (U806) */
/*
 * NoVmp (ledger U806): INVPCID (SDM Vol2A; CPL checked by the translator). #GP(0) if the type
 * > 3, if descriptor bits 63:12 are not 0, if the type is 0 or 1 with a PCID != 0 while
 * CR4.PCIDE = 0 (outside IA-32e mode PCIDE is always 0), or if the type is 0 and the linear
 * address (descriptor bits 127:64) is not canonical for the current paging mode (48 bits, 57
 * with CR4.LA57). QEMU's TLB holds only the current PCID's translations (every CR3 load flushes
 * it) and no paging-structure caches: type 0 for the current PCID flushes that page, type 1
 * for the current PCID flushes the whole TLB (global translations too, which the SDM allows),
 * types 2 and 3 flush everything; a PCID other than the current one has nothing cached.
 */
void helper_invpcid(CPUX86State *env, target_ulong type, target_ulong lo, target_ulong hi)
{
    uintptr_t ra = GETPC();
    uint64_t pcid = lo & 0xfff;
    uint64_t cur = (env->cr[4] & CR4_PCIDE_MASK) ? (env->cr[3] & 0xfff) : 0;
    int bits = (env->cr[4] & CR4_LA57_MASK) ? 57 : 48;
    int64_t sext = (int64_t)((uint64_t)hi << (64 - bits)) >> (64 - bits);

    if (type > 3 || (lo >> 12) != 0 ||
        (type <= 1 && pcid != 0 && !(env->cr[4] & CR4_PCIDE_MASK)) ||
        (type == 0 && (uint64_t)sext != (uint64_t)hi)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    switch (type) {
    case 0:
        if (pcid == cur) {
#if __Use_Original_Qemu != 1 /* ours (U1180) */
            /* LAM active: the entries of masked accesses are under unmasked addresses (U1180) */
            if (x86_lam_active(env)) {
                tlb_flush(env_cpu(env));
                break;
            }
#endif /* __Use_Original_Qemu (U1180) */
            tlb_flush_page(env_cpu(env), hi);
        }
        break;
    case 1:
        if (pcid == cur) {
            tlb_flush(env_cpu(env));
        }
        break;
    default:
        tlb_flush(env_cpu(env));
        break;
    }
}
#endif /* __Use_Original_Qemu (U806) */
#if __Use_Original_Qemu != 1 /* ours (U807) */
#include "qemu/guest-random.h"

/*
 * NoVmp (ledger U807): PBNDKB (SDM Vol2B), 64-bit mode, CPL 0 (checked by the translator).
 * Crypto: HMAC-SHA256 (FIPS 198-1 / 180-4) and AES-256-GCM (SP 800-38D, 96-bit IV, 128-bit tag)
 * written here; AES itself is QEMU's crypto/aes.c (as Key Locker, U100).
 * Not modelled (platform state Unicorn does not have): the 256-bit platform-specific key that
 * PBNDKB derives the wrapping key from is a fixed model constant (32 zero bytes), so the bind
 * structures are not platform-bound; the random values (key randomization, IV) come from the
 * same source as RDRAND (since U2091 x86_rdrand_bytes: the seeded CTR_DRBG or the host DRNG; U807 read
 * qemu_guest_getrandom), whose failure is the ENTROPY_ERROR path.
 */
static const uint32_t pb_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t pb_ror(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void pb_sha256_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, k, t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ldl_be_p(p + 4 * i);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = pb_ror(w[i - 15], 7) ^ pb_ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = pb_ror(w[i - 2], 17) ^ pb_ror(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; k = h[7];
    for (i = 0; i < 64; i++) {
        t1 = k + (pb_ror(e, 6) ^ pb_ror(e, 11) ^ pb_ror(e, 25)) + ((e & f) ^ (~e & g)) +
             pb_sha256_k[i] + w[i];
        t2 = (pb_ror(a, 2) ^ pb_ror(a, 13) ^ pb_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

/* SHA-256 of the concatenation p1 || p2 (FIPS 180-4) */
static void pb_sha256(const uint8_t *p1, size_t n1, const uint8_t *p2, size_t n2, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t blk[64];
    size_t total = n1 + n2, used = 0, i;

    for (i = 0; i < total; i++) {
        blk[used++] = i < n1 ? p1[i] : p2[i - n1];
        if (used == 64) {
            pb_sha256_block(h, blk);
            used = 0;
        }
    }
    blk[used++] = 0x80;
    if (used > 56) {
        memset(blk + used, 0, 64 - used);
        pb_sha256_block(h, blk);
        used = 0;
    }
    memset(blk + used, 0, 56 - used);
    stq_be_p(blk + 56, (uint64_t)total * 8);
    pb_sha256_block(h, blk);
    for (i = 0; i < 8; i++) {
        stl_be_p(out + 4 * i, h[i]);
    }
}

/* HMAC-SHA256 with a 32-byte key (FIPS 198-1: the key is padded to the 64-byte block) */
static void pb_hmac_sha256(const uint8_t key[32], const uint8_t *msg, size_t n, uint8_t out[32])
{
    uint8_t pad[64], inner[32];
    int i;

    for (i = 0; i < 64; i++) {
        pad[i] = (i < 32 ? key[i] : 0) ^ 0x36;
    }
    pb_sha256(pad, 64, msg, n, inner);
    for (i = 0; i < 64; i++) {
        pad[i] = (i < 32 ? key[i] : 0) ^ 0x5c;
    }
    pb_sha256(pad, 64, inner, 32, out);
}

/* GF(2^128) multiplication of SP 800-38D 6.3 (Algorithm 1); x, y big-endian 16-byte blocks */
static void pb_gf_mul(uint8_t x[16], const uint8_t y[16])
{
    uint64_t zh = 0, zl = 0, vh = ldq_be_p(y), vl = ldq_be_p(y + 8);
    int i;

    for (i = 0; i < 128; i++) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1) {
            zh ^= vh;
            zl ^= vl;
        }
        if (vl & 1) {
            vl = (vl >> 1) | (vh << 63);
            vh = (vh >> 1) ^ 0xe100000000000000ULL;
        } else {
            vl = (vl >> 1) | (vh << 63);
            vh >>= 1;
        }
    }
    stq_be_p(x, zh);
    stq_be_p(x + 8, zl);
}

static void pb_ghash_blocks(uint8_t x[16], const uint8_t h[16], const uint8_t *p, size_t n)
{
    size_t i, j;

    for (i = 0; i < n; i += 16) {
        for (j = 0; j < 16 && i + j < n; j++) {
            x[j] ^= p[i + j];
        }
        pb_gf_mul(x, h);
    }
}

/* AES-256-GCM encryption, 96-bit IV, 128-bit tag (SP 800-38D 7.1) */
static void pb_aes256_gcm(const uint8_t key[32], const uint8_t iv[12], const uint8_t *aad,
                          size_t na, const uint8_t *pt, size_t np, uint8_t *ct, uint8_t tag[16])
{
    AES_KEY k;
    uint8_t h[16] = {0}, j0[16], cb[16], ks[16], x[16] = {0}, lens[16];
    uint32_t ctr;
    size_t i, j;

    AES_set_encrypt_key(key, 256, &k);
    AES_encrypt(h, h, &k);
    memcpy(j0, iv, 12);
    stl_be_p(j0 + 12, 1);
    memcpy(cb, j0, 16);
    for (i = 0; i < np; i += 16) {
        ctr = ldl_be_p(cb + 12) + 1;
        stl_be_p(cb + 12, ctr);
        AES_encrypt(cb, ks, &k);
        for (j = 0; j < 16 && i + j < np; j++) {
            ct[i + j] = pt[i + j] ^ ks[j];
        }
    }
    pb_ghash_blocks(x, h, aad, na);
    pb_ghash_blocks(x, h, ct, np);
    stq_be_p(lens, (uint64_t)na * 8);
    stq_be_p(lens + 8, (uint64_t)np * 8);
    pb_ghash_blocks(x, h, lens, 16);
    AES_encrypt(j0, ks, &k);
    for (i = 0; i < 16; i++) {
        tag[i] = ks[i] ^ x[i];
    }
}

/*
 * NoVmp (ledger U1120): the model's 256-bit platform-specific key (SDM 092 Vol2B PBNDKB 4-240,
 * PCONFIG 4-279), one constant for PBNDKB and PCONFIG TSE_KEY_PROGRAM_WRAPPED so that a bind
 * structure PBNDKB made unwraps on the same model: 32 zero bytes (U807; not modelled - a real
 * platform's key is a platform secret, so bind structures are not platform-bound here).
 */
static const uint8_t pb_platform_key[32];

/*
 * NoVmp (ledger U1120): AES-256-GCM authenticated decryption (SP 800-38D 7.2), 96-bit IV, the
 * inverse of pb_aes256_gcm: the plaintext 'pt' of the ciphertext 'ct' and the 128-bit tag computed
 * over 'aad' and 'ct' (the caller compares it with the stored MAC before using 'pt').
 */
static void pb_aes256_gcm_dec(const uint8_t key[32], const uint8_t iv[12], const uint8_t *aad,
                              size_t na, const uint8_t *ct, size_t nc, uint8_t *pt,
                              uint8_t tag[16])
{
    AES_KEY k;
    uint8_t h[16] = {0}, j0[16], cb[16], ks[16], x[16] = {0}, lens[16];
    uint32_t ctr;
    size_t i, j;

    AES_set_encrypt_key(key, 256, &k);
    AES_encrypt(h, h, &k);
    memcpy(j0, iv, 12);
    stl_be_p(j0 + 12, 1);
    pb_ghash_blocks(x, h, aad, na);
    pb_ghash_blocks(x, h, ct, nc);
    stq_be_p(lens, (uint64_t)na * 8);
    stq_be_p(lens + 8, (uint64_t)nc * 8);
    pb_ghash_blocks(x, h, lens, 16);
    AES_encrypt(j0, ks, &k);
    for (i = 0; i < 16; i++) {
        tag[i] = ks[i] ^ x[i];
    }
    memcpy(cb, j0, 16);
    for (i = 0; i < nc; i += 16) {
        ctr = ldl_be_p(cb + 12) + 1;
        stl_be_p(cb + 12, ctr);
        AES_encrypt(cb, ks, &k);
        for (j = 0; j < 16 && i + j < nc; j++) {
            pt[i + j] = ct[i + j] ^ ks[j];
        }
    }
}

static bool pb_zero(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i]) {
            return false;
        }
    }
    return true;
}

static bool pb_canonical(CPUX86State *env, uint64_t a)
{
    int bits = (env->cr[4] & CR4_LA57_MASK) ? 57 : 48;

    return (uint64_t)((int64_t)(a << (64 - bits)) >> (64 - bits)) == a;
}

/*
 * PBNDKB (SDM Vol2B Operation): #GP(0) if RBX or RCX is not canonical (current paging mode),
 * not 256-byte aligned, or RBX = RCX; the 256-byte input bind structure at RBX is read; #GP(0)
 * if bytes 23:16 or 63:36 are not 0, KEY_GENERATION_CTRL (byte 160) > 1, or BTDATA bytes 127:33
 * (structure bytes 255:161) are not 0. KEY_GENERATION_CTRL = 1 XORs 64 random bytes into
 * BTENCDATA (data key, tweak key). WRAPPING_KEY = HMAC_SHA256(PLATFORM_KEY, USER_SUPP_CHALLENGE
 * (BTDATA bytes 31:0)); a random 96-bit IV; AAD = 8 zero bytes || IV || 28 zero bytes || the
 * input BTDATA (176 bytes); AES-256-GCM of BTENCDATA. The output structure (MAC, zero, IV,
 * zero, ciphertext, BTDATA with USER_SUPP_CHALLENGE zeroed and bytes 127:33 zero) is stored at
 * RCX; RAX = 0, ZF = 0. No entropy: RAX = 1 (ENTROPY_ERROR), ZF = 1, nothing stored. CF, PF,
 * AF, OF, SF := 0 in both cases.
 */
void helper_pbndkb(CPUX86State *env)
{
    uintptr_t ra = GETPC();
    uint64_t in = env->regs[R_EBX], out = env->regs[R_ECX];
    uint8_t s[256], o[256], rnd[64], iv[12], ivb[16], wkey[32], aad[176], tag[16];   /* ivb: U2091 */
    int i;

#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_pbndkb_check(env, ra);  /* "enable PBNDKB" 0: #UD before any other exception (28.3) */
#endif /* __Use_Original_Qemu (U1304) */
    if (!pb_canonical(env, in) || !pb_canonical(env, out) || (in & 0xff) || (out & 0xff) ||
        in == out) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    for (i = 0; i < 256; i += 8) {
        stq_le_p(s + i, cpu_ldq_data_ra(env, in + i, ra));
    }
    if (!pb_zero(s + 16, 8) || !pb_zero(s + 36, 28) || s[160] > 1 || !pb_zero(s + 161, 95)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (s[160] == 1) {
        /* U2091 (plan 3.M6: every emulated random source from the RDRAND source - the seeded CTR_DRBG or, with
         * UC_X86_RDRAND_HOST, the host DRNG; U807 read the host OS generator here) */
        if (!x86_rdrand_bytes(env, rnd, sizeof(rnd))) {
            goto entropy_error;
        }
        for (i = 0; i < 64; i++) {
            s[64 + i] ^= rnd[i];         /* the copy only: the input structure is not modified */
        }
    }
    pb_hmac_sha256(pb_platform_key, s + 128, 32, wkey);     /* U1120: shared with PCONFIG */
    if (!x86_rdrand_bytes(env, ivb, sizeof(ivb))) {     /* U2091: two 64-bit values, the IV = their first 12 bytes */
        goto entropy_error;
    }
    memcpy(iv, ivb, sizeof(iv));
    memset(aad, 0, sizeof(aad));
    memcpy(aad + 8, iv, 12);
    memcpy(aad + 48, s + 128, 128);
    memset(o, 0, sizeof(o));
    pb_aes256_gcm(wkey, iv, aad, sizeof(aad), s + 64, 64, o + 64, tag);
    memcpy(o, tag, 16);
    memcpy(o + 24, iv, 12);
    o[160] = s[160];
    for (i = 0; i < 256; i += 8) {
        cpu_stq_data_ra(env, out + i, ldq_le_p(o + i), ra);
        msrlist_unicorn_stop(env, ra);   /* unmapped: stops like a #PF (U802) */
    }
    env->regs[R_EAX] = 0;
    CC_SRC = 0;
    return;

entropy_error:
    env->regs[R_EAX] = 1;               /* ENTROPY_ERROR */
    CC_SRC = CC_Z;
}
#endif /* __Use_Original_Qemu (U807) */
#if __Use_Original_Qemu != 1 /* ours (U1021) */
/*
 * NoVmp (ledger U1021): PCONFIG (NP 0F 01 C5), SDM 325462-092 Vol2B PCONFIG Operation; CPL 0, the
 * CPUID bit, LOCK / 66 / F2 / F3 are checked by the translator (#UD). Then:
 *   EAX > 2: #GP(0). Each leaf needs its target in CPUID.1BH (the leaf as CPUID reports it, so a
 *   CPUID profile counts): the model enumerates target 1 (TME-MK, with TME_EN) and target 2 (TSE,
 *   with PBNDKB) - U1120: the earlier "target 1 only" here was stale.
 *   Leaf 0 MKTME_KEY_PROGRAM: #GP(0) unless IA32_TME_ACTIVATE is locked [0], enabled [1] with
 *   MK_TME_KEYID_BITS [35:32] != 0; #GP(0) if the linear address DS:RBX (64-bit mode: RBX,
 *   non-canonical #GP(0); otherwise DS.base + EBX, 32 bits, with the DS null-selector / limit
 *   checks of a data read) is not 256-byte aligned; the 192-byte MKTME_KEY_PROGRAM_STRUCT is read
 *   (#PF); #GP(0) if KEYID_CTRL[31:24] != 0, COMMAND > 3, KEYID = 0 or > MK_TME_MAX_KEYS,
 *   KEYID[15:k] != 0, KEYID[k-1:k-p] != 0 (p = TDX_RESERVED_KEYID_BITS; the model has no SEAM),
 *   ENC_ALG does not set exactly one bit or not one of IA32_TME_ACTIVATE[63:48]. Bytes 63:6 and
 *   the key-field bytes beyond the algorithm's key size (16 for AES-XTS-128, 32 for AES-XTS-256)
 *   are ignored. The key table lock is always acquired (one logical processor: DEVICE_BUSY (5)
 *   cannot occur). KEYID_SET_KEY_DIRECT: the keys from KEY_FIELD_1 / KEY_FIELD_2;
 *   KEYID_SET_KEY_RANDOM: data key then tweak key from the RDRAND source (U835, x86_rdrand_bytes),
 *   each XOR the software entropy - ENTROPY_ERROR (2) only when the host DRNG fails in
 *   UC_X86_RDRAND_HOST mode; KEYID_CLEAR_KEY: the KeyID uses the TME behaviour again;
 *   KEYID_NO_ENCRYPT: no encryption. Success: RAX := 0, ZF := 0; failure: RAX := reason, ZF := 1;
 *   CF, PF, AF, OF, SF := 0 in both cases.
 *   Leaf 1 TSE_KEY_PROGRAM / leaf 2 TSE_KEY_PROGRAM_WRAPPED (SDM 092 Vol2B; target identifier 2,
 *   which the model reports in CPUID.1BH with PBNDKB, U807): #GP(0) without the TSE target and
 *   outside 64-bit mode. Leaf 1: #GP(0) for RBX not 256-byte aligned (or not canonical), the
 *   192-byte TSE_KEY_PROGRAM_STRUCT is read (#PF), #GP(0) for KEYID_CTRL[31:24] != 0, COMMAND > 1,
 *   KEYID > TSE_MAX_KEYS, ENC_ALG not exactly one bit of IA32_TSE_CAPABILITY[15:0]. Leaf 2: #GP(0)
 *   for RBX[23:16] != 0 or RCX not 256-byte aligned, RBX[15:0] > TSE_MAX_KEYS, RBX[39:24] not
 *   exactly one bit of IA32_TSE_CAPABILITY[15:0]. Without UC_X86_FEATURE_TSE the model's
 *   IA32_TSE_CAPABILITY is 0 (U807: no algorithm, TSE_MAX_KEYS 0, no TSE engine), so the ENC_ALG
 *   check always fails: every TSE leaf ends in #GP(0) at the latest there (leaf 2 never reaches
 *   its 256-byte read). U1120, with the opt-in (IA32_TSE_CAPABILITY = NOVMP_TSE_CAPABILITY): see
 *   pconfig_tse below.
 * The key table (CPUX86State.mktme_keys, reset area) is not software-visible; UC_CTL_X86_MKTME_KEY
 * reads it. Not modelled: the memory encryption itself (see U1020), VMX non-root operation ("enable
 * PCONFIG", PCONFIG-exiting bitmap: the emulator has no VMX), SEAM.
 */

/* CPUID.1BH (as CPUID reports it, a profile included) lists target identifier 'id' */
static bool pconfig_target(CPUX86State *env, uint32_t id)
{
    uint32_t a, b, c, d, sub;

    cpu_x86_cpuid(env, 0, 0, &a, &b, &c, &d);
    if (a < 0x1b) {
        return false;
    }
    for (sub = 0; sub < 256; sub++) {
        cpu_x86_cpuid(env, 0x1b, sub, &a, &b, &c, &d);
        if ((a & 0xfff) == 0) {
            return false;                   /* invalid, and so is every later sub-leaf */
        }
        if ((a & 0xfff) == 1 && (b == id || c == id || d == id)) {
            return true;
        }
    }
    return false;
}

static bool pconfig_canonical(CPUX86State *env, uint64_t a)
{
    int bits = (env->cr[4] & CR4_LA57_MASK) ? 57 : 48;

    return (uint64_t)((int64_t)(a << (64 - bits)) >> (64 - bits)) == a;
}

/*
 * The linear address of the structure: 64-bit mode RBX (#GP(0) if not canonical); otherwise
 * DS.base + EBX (32 bits), #GP(0) for a null DS in protected mode or when the 'len' bytes at EBX
 * are outside the DS limit (expand-up: last byte <= limit; expand-down data: first byte > limit,
 * last byte <= FFFFh / FFFFFFFFh by DS.B).
 */
static uint64_t pconfig_linear(CPUX86State *env, uint64_t reg, uint32_t len, uintptr_t ra)
{
    SegmentCache *ds = &env->segs[R_DS];
    uint32_t off = (uint32_t)reg, last = off + len - 1;

    if (env->hflags & HF_CS64_MASK) {
        if (!pconfig_canonical(env, reg)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        return reg;
    }
    if ((env->cr[0] & CR0_PE_MASK) && (ds->selector & 0xfffc) == 0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (last < off) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if ((env->cr[0] & CR0_PE_MASK) && !(ds->flags & DESC_CS_MASK) && (ds->flags & DESC_E_MASK)) {
        uint32_t top = (ds->flags & DESC_B_MASK) ? 0xffffffffu : 0xffffu;

        if (off <= ds->limit || last > top) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
    } else if (last > ds->limit) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    return (uint32_t)(ds->base + off);
}

static void pconfig_load(CPUX86State *env, uint64_t lin, uint8_t *s, int n, uintptr_t ra)
{
    int i;

    for (i = 0; i < n; i += 8) {
        stq_le_p(s + i, cpu_ldq_data_ra(env, lin + i, ra));
        msrlist_unicorn_stop(env, ra);      /* unmapped: stops like a #PF (U802) */
    }
}

static int pconfig_popcount16(uint32_t v)
{
    int n = 0;

    for (v &= 0xffff; v; v &= v - 1) {
        n++;
    }
    return n;
}

/*
 * NoVmp (ledger U1120, U1129): PCONFIG leaves 1 TSE_KEY_PROGRAM and 2 TSE_KEY_PROGRAM_WRAPPED,
 * SDM 092 / 093 Vol2B 4-273..4-281 (Tables 4-17 / 4-18, Operation 4-278 / 4-279; 093 unchanged,
 * U1130) and ISE
 * 319433-060 chapter 2 PCONFIG (Table 2-3 "TSE_KEY_PROGRAM_STRUCT Format" p. 2-20, Table 2-4
 * "TSE_KEY_PROGRAM_WRAPPED Control Input" p. 2-21, Operation pp. 2-25..2-27; the same rules), in
 * the pseudocode's order:
 *   both: #GP(0) without the TSE target (2) in CPUID.1BH or outside 64-bit mode.
 *   Leaf 1: #GP(0) if RBX is not 256-byte aligned (or not canonical: "If a memory operand is
 *   non-canonical form"); the 192-byte TSE_KEY_PROGRAM_STRUCT is read (#PF); #GP(0) for
 *   KEYID_CTRL[31:24] != 0, COMMAND > 1, KEYID > TSE_MAX_KEYS (KeyID 0 is accepted - the
 *   pseudocode has no zero check, unlike MKTME_KEY_PROGRAM), ENC_ALG not exactly one bit or not
 *   in IA32_TSE_CAPABILITY[15:0]. TSE_SET_KEY_DIRECT: data key = KEY_FIELD_1, tweak key =
 *   KEY_FIELD_2, NOVMP_TSE_KEY_BYTES of each ("the number of bytes used ... depends on selected
 *   encryption algorithm": 32 for bit 0 = 256-bit AES-XTS, ISE -060 6.2.2 p. 6-2; SDM 092 Vol2B
 *   4-274 NOTES "32 bytes for AES-XTS 256");
 *   TSE_NO_ENCRYPT: no encryption, keys and algorithm not used.
 *   Leaf 2: #GP(0) if RBX[23:16] != 0 or RCX is not 256-byte aligned, RBX[15:0] > TSE_MAX_KEYS,
 *   RBX[39:24] not exactly one bit of IA32_TSE_CAPABILITY[15:0] (RBX[63:40] ignored), RCX not
 *   canonical; the 256-byte TSE_BIND_STRUCT is read (#PF); #GP(0) if bytes 23:16 or 63:36 are not
 *   0, KEY_GENERATION_CTRL (byte 160) > 1, or BTDATA bytes 127:33 (structure bytes 255:161) are
 *   not 0 (the SDM prints "bytes 128:33"; BTDATA has 128 bytes and the prose says "the remaining
 *   95 bytes"). WRAPPING_KEY := HMAC-SHA256(PLATFORM_KEY, USER_SUPP_CHALLENGE = BTDATA bytes
 *   31:0); AAD := structure bytes 63:16 || 255:128 (176 bytes); AES-256-GCM decryption of
 *   BTENCDATA (64 bytes) with the 96-bit IV (bytes 35:24). A computed MAC other than bytes 15:0:
 *   RAX := 7 (UNWRAP_FAILURE), ZF := 1, the table unchanged. Otherwise the KeyID gets data key =
 *   decrypted bytes 31:0 and tweak key = bytes 63:32 (our inference: SDM PBNDKB / PCONFIG name
 *   "a data key and a tweak key" in that order without offsets; not software-visible).
 * The key table lock (a platform lock) is always acquired: one logical processor, DEVICE_BUSY (5)
 * cannot occur. Success: RAX := 0, ZF := 0; CF, PF, AF, OF, SF := 0 in every case that does not
 * fault. The platform key is pb_platform_key (U807's model constant), so the PBNDKB -> restore
 * USER_SUPP_CHALLENGE -> leaf 2 round trip works on any engine of this model; no hardware TSE
 * platform is claimed or matched. Not modelled: the storage encryption engine itself.
 */
static void pconfig_tse(CPUX86State *env, uint32_t leaf, uintptr_t ra)
{
    uint64_t cap = x86_tse_capability(env), rbx = env->regs[R_EBX], rcx = env->regs[R_ECX];
    unsigned max = (unsigned)(cap >> 36) & 0x7fff, keyid, alg, cmd;
    uint8_t s[256], wkey[32], aad[176], dec[64], tag[16];
    uint32_t ctrl;
    X86MktmeKey *e;

    if (!pconfig_target(env, 2) || !(env->hflags & HF_CS64_MASK)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (leaf == 1) {
        if ((rbx & 0xff) || !pconfig_canonical(env, rbx)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        pconfig_load(env, rbx, s, 192, ra);
        keyid = lduw_le_p(s);
        ctrl = ldl_le_p(s + 2);
        cmd = ctrl & 0xff;
        alg = (ctrl >> 8) & 0xffff;
        if ((ctrl >> 24) || cmd > 1 || keyid > max) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        if (pconfig_popcount16(alg) != 1 || !(alg & (unsigned)cap)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        /* the key table lock: always acquired (one logical processor) */
        e = &env->tse_keys[keyid];
        memset(e, 0, sizeof(*e));
        if (cmd == 0) {                     /* TSE_SET_KEY_DIRECT */
            e->mode = 1;
            e->enc_alg = (uint16_t)alg;
            memcpy(e->data_key, s + 64, NOVMP_TSE_KEY_BYTES);
            memcpy(e->tweak_key, s + 128, NOVMP_TSE_KEY_BYTES);
        } else {                            /* TSE_NO_ENCRYPT */
            e->mode = 2;
        }
    } else {
        if (((rbx >> 16) & 0xff) || (rcx & 0xff)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        keyid = (unsigned)rbx & 0xffff;
        alg = (unsigned)(rbx >> 24) & 0xffff;
        if (keyid > max) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        if (pconfig_popcount16(alg) != 1 || !(alg & (unsigned)cap)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        if (!pconfig_canonical(env, rcx)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        pconfig_load(env, rcx, s, 256, ra);
        if (!pb_zero(s + 16, 8) || !pb_zero(s + 36, 28) || s[160] > 1 || !pb_zero(s + 161, 95)) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        pb_hmac_sha256(pb_platform_key, s + 128, 32, wkey);
        memcpy(aad, s + 16, 48);
        memcpy(aad + 48, s + 128, 128);
        pb_aes256_gcm_dec(wkey, s + 24, aad, sizeof(aad), s + 64, 64, dec, tag);
        if (memcmp(tag, s, 16) != 0) {
            env->regs[R_EAX] = 7;           /* UNWRAP_FAILURE; the table is not changed */
            CC_SRC = CC_Z;
            return;
        }
        /* the key table lock: always acquired (one logical processor) */
        e = &env->tse_keys[keyid];
        memset(e, 0, sizeof(*e));
        e->mode = 1;
        e->enc_alg = (uint16_t)alg;
        memcpy(e->data_key, dec, NOVMP_TSE_KEY_BYTES);
        memcpy(e->tweak_key, dec + 32, NOVMP_TSE_KEY_BYTES);
    }
    env->regs[R_EAX] = 0;
    CC_SRC = 0;
}

void helper_pconfig(CPUX86State *env)
{
    uintptr_t ra = GETPC();
    uint32_t leaf = (uint32_t)env->regs[R_EAX];
    uint64_t act = env->tme_activate, lin;
    uint8_t s[192], dk[32], tk[32];
    unsigned k, p, cmd, keyid, alg, n, i;
    uint32_t ctrl;
    X86MktmeKey *e;

#if __Use_Original_Qemu != 1 /* ours (U1304) */
    x86_vmx_pconfig_check(env, ra);     /* "enable PCONFIG" #UD, PCONFIG-exiting bitmap (28.1.3, 28.3) */
#endif /* __Use_Original_Qemu (U1304) */
    if (leaf > 2) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (leaf != 0) {
        pconfig_tse(env, leaf, ra);     /* TSE_KEY_PROGRAM / TSE_KEY_PROGRAM_WRAPPED (U1120) */
        return;
    }
    if (!pconfig_target(env, 1)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    k = (unsigned)(act >> 32) & 0xf;
    p = (unsigned)(act >> 36) & 0xf;
    if (!(act & 1) || !(act & 2) || k == 0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    lin = pconfig_linear(env, env->regs[R_EBX], 192, ra);
    if (lin & 0xff) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    pconfig_load(env, lin, s, 192, ra);
    keyid = lduw_le_p(s);
    ctrl = ldl_le_p(s + 2);
    cmd = ctrl & 0xff;
    alg = (ctrl >> 8) & 0xffff;
    if ((ctrl >> 24) || cmd > 3) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (keyid == 0 || keyid > NOVMP_MKTME_MAX_KEYS || (keyid >> k)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (p && ((keyid >> (k - p)) & ((1u << p) - 1))) {      /* not in SEAM */
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (pconfig_popcount16(alg) != 1 || !(alg & (unsigned)(act >> 48))) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    /* the key table lock: always acquired (one logical processor; DEVICE_BUSY cannot occur) */
    n = (alg & 3) ? 16 : 32;                /* AES-XTS-128 (+integrity): 16, -256: 32 bytes */
    e = &env->mktme_keys[keyid];
    switch (cmd) {
    case 0:                                 /* KEYID_SET_KEY_DIRECT */
    case 1:                                 /* KEYID_SET_KEY_RANDOM */
        memset(dk, 0, sizeof(dk));
        memset(tk, 0, sizeof(tk));
        if (cmd == 1) {
            if (!x86_rdrand_bytes(env, dk, n) || !x86_rdrand_bytes(env, tk, n)) {
                env->regs[R_EAX] = 2;       /* ENTROPY_ERROR; the table is not changed */
                CC_SRC = CC_Z;
                return;
            }
        }
        for (i = 0; i < n; i++) {
            dk[i] ^= s[64 + i];
            tk[i] ^= s[128 + i];
        }
        e->mode = 1;
        e->random = (uint8_t)cmd;
        e->enc_alg = (uint16_t)alg;
        memcpy(e->data_key, dk, sizeof(dk));
        memcpy(e->tweak_key, tk, sizeof(tk));
        break;
    default:                                /* KEYID_CLEAR_KEY (2) / KEYID_NO_ENCRYPT (3) */
        memset(e, 0, sizeof(*e));
        e->mode = cmd == 2 ? 0 : 2;
        break;
    }
    env->regs[R_EAX] = 0;
    CC_SRC = 0;
}
#endif /* __Use_Original_Qemu (U1021) */
