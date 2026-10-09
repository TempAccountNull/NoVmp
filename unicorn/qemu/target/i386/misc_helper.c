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

void helper_outb(CPUX86State *env, uint32_t port, uint32_t data)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     glue(address_space_stb, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port, data & 0xff,
// #else
//     address_space_stb(env->uc, &env->uc->address_space_io, port, data & 0xff,
// #endif
//                       cpu_get_mem_attrs(env), NULL);
    return cpu_outb(env->uc, port, data, GETPC());
}

target_ulong helper_inb(CPUX86State *env, uint32_t port)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     return glue(address_space_ldub, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port,
// #else
//     return address_space_ldub(env->uc, &env->uc->address_space_io, port,
// #endif
//                               cpu_get_mem_attrs(env), NULL);
    return cpu_inb(env->uc, port, GETPC());
}

void helper_outw(CPUX86State *env, uint32_t port, uint32_t data)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     glue(address_space_stw, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port, data & 0xffff,
// #else
//     address_space_stw(env->uc, &env->uc->address_space_io, port, data & 0xffff,
// #endif
//                       cpu_get_mem_attrs(env), NULL);
    return cpu_outw(env->uc, port, data, GETPC());
}

target_ulong helper_inw(CPUX86State *env, uint32_t port)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     return glue(address_space_lduw, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port,
// #else
//     return address_space_lduw(env->uc, &env->uc->address_space_io, port,
// #endif
//                               cpu_get_mem_attrs(env), NULL);
    return cpu_inw(env->uc, port, GETPC());
}

void helper_outl(CPUX86State *env, uint32_t port, uint32_t data)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     glue(address_space_stl, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port, data,
// #else
//     address_space_stl(env->uc, &env->uc->address_space_io, port, data,
// #endif
//                       cpu_get_mem_attrs(env), NULL);
    return cpu_outl(env->uc, port, data, GETPC());
}

target_ulong helper_inl(CPUX86State *env, uint32_t port)
{
// #ifdef UNICORN_ARCH_POSTFIX
//     return glue(address_space_ldl, UNICORN_ARCH_POSTFIX)(env->uc, &env->uc->address_space_io, port,
// #else
//     return address_space_ldl(env->uc, &env->uc->address_space_io, port,
// #endif
//                              cpu_get_mem_attrs(env), NULL);
    return cpu_inl(env->uc, port, GETPC());
}

void helper_into(CPUX86State *env, int next_eip_addend)
{
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);
    if (eflags & CC_O) {
        raise_interrupt(env, EXCP04_INTO, 1, 0, next_eip_addend);
    }
}

void helper_cpuid(CPUX86State *env)
{
    uint32_t eax, ebx, ecx, edx;
    uc_engine *uc = env->uc;
    struct hook *hook;
    int skip_cpuid = 0;
    bool synced = false;
    cpu_svm_check_intercept_param(env, SVM_EXIT_CPUID, 0, GETPC());

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
            // val = cpu_get_apic_tpr(env_archcpu(env)->apic_state);
            val = 0;
        } else {
            val = env->v_tpr;
        }
        break;
    }
    return val;
}

void helper_write_crN(CPUX86State *env, int reg, target_ulong t0)
{
    cpu_svm_check_intercept_param(env, SVM_EXIT_WRITE_CR0 + reg, 0, GETPC());
    switch (reg) {
    case 0:
#if __Use_Original_Qemu != 1 /* ours (U114) */
        /* CR0.WP cannot be cleared while CR4.CET = 1 (SDM Vol3 2.5, CR4.CET) */
        if (!(t0 & CR0_WP_MASK) && (env->cr[4] & CR4_CET_MASK)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#endif /* __Use_Original_Qemu (U114) */
        cpu_x86_update_cr0(env, (uint32_t)t0);
        break;
    case 3:
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
         * does not modify bit 63 of CR3"). MAXPHYADDR = phys_bits, the width the
         * page walk uses for its reserved-bit checks.
         */
        if (env->efer & MSR_EFER_LMA) {
            if (env->cr[4] & CR4_PCIDE_MASK) {
                t0 &= ~(1ULL << 63);
            }
            if (t0 & ((~0ULL) << env_archcpu(env)->phys_bits)) {
                raise_exception_ra(env, EXCP0D_GPF, GETPC());
            }
        }
#endif /* __Use_Original_Qemu (U478) */
        /* backport 3407259b20: without IA-32e mode CR3 is a 32-bit register */
        if (!(env->efer & MSR_EFER_LMA)) {
            t0 &= 0xffffffffUL;
        }
        cpu_x86_update_cr3(env, t0);
        break;
    case 4:
        if (t0 & cr4_reserved_bits(env)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#if __Use_Original_Qemu != 1 /* ours (U114) */
        /* CR4.CET can be set only if CR0.WP = 1 */
        if ((t0 & CR4_CET_MASK) && !(env->cr[0] & CR0_WP_MASK)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#endif /* __Use_Original_Qemu (U114) */
        if (((t0 ^ env->cr[4]) & CR4_LA57_MASK) &&
            (env->hflags & HF_CS64_MASK)) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        cpu_x86_update_cr4(env, (uint32_t)t0);
        break;
    case 8:
#if 0
        if (!(env->hflags2 & HF2_VINTR_MASK)) {
            cpu_set_apic_tpr(env_archcpu(env)->apic_state, t0);
        }
#endif
        env->v_tpr = t0 & 0x0f;
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
    tlb_flush_page(env_cpu(env), addr);
}

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
        val = cpu_get_tsc(env) + env->tsc_offset;
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

    if ((env->cr[4] & CR4_TSD_MASK) && ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    cpu_svm_check_intercept_param(env, SVM_EXIT_RDTSC, 0, GETPC());

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
        val = cpu_get_tsc(env) + env->tsc_offset;
        env->regs[R_EAX] = (uint32_t)(val);
        env->regs[R_EDX] = (uint32_t)(val >> 32);

        env->regs[R_ECX] = (uint32_t)(env->tsc_aux);
    }
}

void helper_rdpmc(CPUX86State *env)
{
    /* backport of QEMU c45b426acd: #GP(0) if CPL > 0 and CR4.PCE = 0 (SDM Vol2 RDPMC) */
    if (((env->cr[4] & CR4_PCE_MASK) == 0 ) &&
        ((env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    cpu_svm_check_intercept_param(env, SVM_EXIT_RDPMC, 0, GETPC());

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
     * docs/quirks.md ("SDM undefined, our choice").
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
        env->regs[R_EAX] = 0;
        env->regs[R_EDX] = 0;
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
void helper_wrmsr(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    uint64_t val;
    uc_engine *uc = env->uc;
    struct hook *hook;
    int skip_wrmsr = 0;
    bool synced = false;

    cpu_svm_check_intercept_param(env, SVM_EXIT_MSR, 1, GETPC());

    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN)
    {
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

    val = ((uint32_t)env->regs[R_EAX]) |
        ((uint64_t)((uint32_t)env->regs[R_EDX]) << 32);

    switch ((uint32_t)env->regs[R_ECX]) {
    case MSR_IA32_SYSENTER_CS:
        env->sysenter_cs = val & 0xffff;
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
         */
        if (!(env->features[FEAT_XSAVE] & CPUID_XSAVE_XSAVES) || (val & ~valid)) {
            if (env->msr_api) {
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
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        env->pkrs = val;
        tlb_flush(cs);
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
                raise_exception_ra(env, EXCP0D_GPF, GETPC());
            }
            env->msr_user_msr_ctl = val;
        }
        break;
    case MSR_IA32_UARCH_MISC_CTL:
        /* SDM Vol4: bit 0 DOITM, 63:1 reserved (modelled with USER_MSR, U103) */
        if (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_USER_MSR) {
            if (val & ~1ULL) {
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
         * stays an unknown MSR (ignored, as before).
         */
        if ((env->features[FEAT_7_1_EAX] & CPUID_7_1_EAX_MSRLIST) && !env->msr_api) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        break;
#endif /* __Use_Original_Qemu (U802) */
#if __Use_Original_Qemu != 1 /* ours (U104) */
    /* user-interrupt MSRs (SDM Vol3A 9.3.2), present with CPUID.(07H,0):EDX.UINTR */
    case MSR_IA32_UINTR_RR:
        if (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_UINTR) {
            env->uintr_rr = val;
            x86_uintr_update_request(env);
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
        /* NoVmp (ledger U114): CET MSRs, see cet_wrmsr; #GP(0) on an invalid value */
        if (!cet_wrmsr(env, (uint32_t)env->regs[R_ECX], val) && !env->msr_api) {
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
        break;
#endif /* __Use_Original_Qemu (U114) */
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
                break;
            }
            raise_exception_ra(env, EXCP0D_GPF, GETPC());
        }
#endif /* __Use_Original_Qemu (U783) */
        env->msr_bndcfgs = val;
        cpu_sync_bndcs_hflags(env);
        break;
    default:
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
    X86CPU *x86_cpu = env_archcpu(env);
    uint64_t val;
    uc_engine *uc = env->uc;
    struct hook *hook;
    int skip_rdmsr = 0;
    bool synced = false;

    cpu_svm_check_intercept_param(env, SVM_EXIT_MSR, 0, GETPC());

    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(env->uc, hook, UC_HOOK_INSN)
    {
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

    switch ((uint32_t)env->regs[R_ECX]) {
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
        val = x86_cpu->ucode_rev;
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
    env->eip += next_eip_addend;

    do_hlt(cpu);
}

void helper_monitor(CPUX86State *env, target_ulong ptr)
{
    if ((uint32_t)env->regs[R_ECX] != 0) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    /* XXX: store address? */
    cpu_svm_check_intercept_param(env, SVM_EXIT_MONITOR, 0, GETPC());
}

void helper_mwait(CPUX86State *env, int next_eip_addend)
{
    CPUState *cs = env_cpu(env);
    X86CPU *cpu = env_archcpu(env);

    if ((uint32_t)env->regs[R_ECX] != 0) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }

    cpu_svm_check_intercept_param(env, SVM_EXIT_MWAIT, 0, GETPC());
    env->eip += next_eip_addend;

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
 * #GP(0) if src[31:1] != 0 or CR4.TSD = 1 at CPL > 0. Timing model (fully
 * deterministic): the implementation-dependent optimized state is left at once,
 * as the SDM allows ("Other implementation-dependent events may cause the
 * processor to exit the implementation-dependent optimized state"), so no TSC
 * time passes inside the instruction. The pseudocode is evaluated with the TSC
 * sampled once (the RDTSC value without hooks): os_deadline = TSC +
 * IA32_UMWAIT_CONTROL[31:2] quanta (0 = no maximum time, per the MSR
 * description); CF = using_os_deadline AND TSC >= deadline, which therefore
 * stays 0 (the OS limit is never reached); AF/PF/SF/ZF/OF = 0. UMONITOR state
 * does not change the result: an unarmed monitor only means no wait at all.
 */
void helper_waitpkg(CPUX86State *env, uint32_t src)
{
    uint64_t tsc, instr_deadline, deadline, limit;
    bool using_os_deadline = false;

    if ((src & ~1u) || ((env->cr[4] & CR4_TSD_MASK)
                        && (env->hflags & HF_CPL_MASK) != 0)) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    tsc = cpu_get_tsc(env) + env->tsc_offset;
    instr_deadline = ((uint64_t)(uint32_t)env->regs[R_EDX] << 32)
                     | (uint32_t)env->regs[R_EAX];
    deadline = instr_deadline;
    limit = env->umwait & ~3u;
    if (limit != 0 && tsc + limit < instr_deadline) {
        deadline = tsc + limit;
        using_os_deadline = true;
    }
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
void helper_getsec(CPUX86State *env)
{
    if (!(env->cr[4] & CR4_SMXE_MASK) || (uint32_t)env->regs[R_EAX] != 0) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }
    env->regs[R_EAX] = 0;
}

/*
 * NoVmp (ledger U113): PCONFIG at CPL0 (the translator raised #UD before).
 * CPUID leaf 1BH enumerates no PCONFIG target, so every leaf faults: in
 * real-address mode the TSE leaves (EAX = 1, 2) are #UD ("not supported in
 * real-address mode"); otherwise #GP(0) - MKTME_KEY_PROGRAM (0) without the
 * TME-MK target, TSE_KEY_PROGRAM(_WRAPPED) (1, 2) without the TSE target, any
 * other EAX an unsupported leaf (SDM Vol2 PCONFIG exceptions).
 */
void helper_pconfig(CPUX86State *env)
{
    uint32_t leaf = (uint32_t)env->regs[R_EAX];

    if (!(env->cr[0] & CR0_PE_MASK) && (leaf == 1 || leaf == 2)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }
    raise_exception_ra(env, EXCP0D_GPF, GETPC());
}

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

target_ulong helper_urdmsr(CPUX86State *env, target_ulong msr)
{
    target_ulong rax = env->regs[R_EAX], rcx = env->regs[R_ECX], rdx = env->regs[R_EDX];
    uint64_t val;

    user_msr_check(env, msr, false, GETPC());
    env->regs[R_ECX] = (uint32_t)msr;
    helper_rdmsr(env);
    val = (uint32_t)env->regs[R_EAX] | ((uint64_t)(uint32_t)env->regs[R_EDX] << 32);
    env->regs[R_EAX] = rax;
    env->regs[R_ECX] = rcx;
    env->regs[R_EDX] = rdx;
    return val;
}

void helper_uwrmsr(CPUX86State *env, target_ulong msr, target_ulong val)
{
    uintptr_t ra = GETPC();
    target_ulong rax = env->regs[R_EAX], rcx = env->regs[R_ECX], rdx = env->regs[R_EDX];

    user_msr_check(env, msr, true, ra);
    if (msr != MSR_IA32_UARCH_MISC_CTL || (val & ~(target_ulong)1)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    env->regs[R_ECX] = (uint32_t)msr;
    env->regs[R_EAX] = (uint32_t)val;
    env->regs[R_EDX] = (uint32_t)((uint64_t)val >> 32);
    helper_wrmsr(env);
    env->regs[R_EAX] = rax;
    env->regs[R_ECX] = rcx;
    env->regs[R_EDX] = rdx;
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

    msrlist_entry_check(env, ra);
    while (env->regs[R_ECX]) {
        int n = ctz64(env->regs[R_ECX]);
        uint64_t a = cpu_ldq_data_ra(env, env->regs[R_ESI] + 8 * (target_ulong)n, ra);
        uint64_t v;

        if (a >> 32) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        v = x86_msr_access(env, (uint32_t)a, 0, false);
        cpu_stq_data_ra(env, env->regs[R_EDI] + 8 * (target_ulong)n, v, ra);
        msrlist_unicorn_stop(env, ra);
        env->regs[R_ECX] &= ~(1ULL << n);
    }
}

void helper_wrmsrlist(CPUX86State *env)
{
    uintptr_t ra = GETPC();

    msrlist_entry_check(env, ra);
    while (env->regs[R_ECX]) {
        int n = ctz64(env->regs[R_ECX]);
        uint64_t a = cpu_ldq_data_ra(env, env->regs[R_ESI] + 8 * (target_ulong)n, ra);
        uint64_t v;

        if (a >> 32) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
        v = cpu_ldq_data_ra(env, env->regs[R_EDI] + 8 * (target_ulong)n, ra);
        x86_msr_access(env, (uint32_t)a, v, true);
        env->regs[R_ECX] &= ~(1ULL << n);
    }
}
#endif /* __Use_Original_Qemu (U802) */
