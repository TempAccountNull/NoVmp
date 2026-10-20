/*
 *  i386 breakpoint helpers
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"


static inline bool hw_local_breakpoint_enabled(unsigned long dr7, int index)
{
    return (dr7 >> (index * 2)) & 1;
}

static inline bool hw_global_breakpoint_enabled(unsigned long dr7, int index)
{
    return (dr7 >> (index * 2)) & 2;

}
static inline bool hw_breakpoint_enabled(unsigned long dr7, int index)
{
    return hw_global_breakpoint_enabled(dr7, index) ||
           hw_local_breakpoint_enabled(dr7, index);
}

static inline int hw_breakpoint_type(unsigned long dr7, int index)
{
    return (dr7 >> (DR7_TYPE_SHIFT + (index * 4))) & 3;
}

static inline int hw_breakpoint_len(unsigned long dr7, int index)
{
    int len = ((dr7 >> (DR7_LEN_SHIFT + (index * 4))) & 3);
    return (len == 2) ? 8 : len + 1;
}
#if __Use_Original_Qemu != 1 /* ours (U1760) */
/*
 * NoVmp (ledger U1760): the interrupt shadow of a MOV SS / POP SS - HF_INHIBIT_IRQ with env->fred_sti = 0 (STI
 * writes 1, a MOV / POP SS outside a shadow 0, on every model since U1752); it ends with the next instruction
 * (do_gen_eob_worker) or with the delivery of an event (x86_movss_event)
 */
static bool x86_movss_shadow(CPUX86State *env)
{
    return (env->hflags & HF_INHIBIT_IRQ_MASK) && !env->fred_sti;
}

#endif /* __Use_Original_Qemu (U1760) */
#if __Use_Original_Qemu != 1 /* ours (U1263) */
/*
 * NoVmp (ledger U1263): Intel SGX breakpoint suppression (SDM 092 Vol3D EENTER p. 41-94, ERESUME
 * p. 41-126: "Suppress_all_code_breakpoints_that_are_outside_ELRANGE; IF (CR_DBGOPTIN = 0) THEN
 * Suppress_all_code_breakpoints_that_overlap_with_ELRANGE"; 43.3.1: after an opt-out entry "Data
 * breakpoints are not triggered on accesses to the address range defined by ELRANGE", they are on
 * accesses outside it; "Following an opt-in entry instruction and data breakpoints are not
 * suppressed"; "The processor does not report any matches on debug breakpoints that are suppressed
 * on enclave entry"). EEXIT and AEX unsuppress them (x86_sgx_bp_update). A data breakpoint is
 * suppressed when its range overlaps ELRANGE (the model does not split an access that straddles
 * the ELRANGE boundary, 38.5.2). I/O breakpoints: IN / OUT are #UD inside an enclave.
 */
static bool hw_breakpoint_sgx_suppressed(CPUX86State *env, int index)
{
    target_ulong a = env->dr[index];
    target_ulong len;

    if (!env->sgx_enclave_mode) {
        return false;
    }
    switch (hw_breakpoint_type(env->dr[7], index)) {
    case DR7_TYPE_BP_INST:
        return !env->sgx_dbgoptin || a - env->sgx_elbase >= env->sgx_elsize;
    case DR7_TYPE_DATA_WR:
    case DR7_TYPE_DATA_RW:
        len = hw_breakpoint_len(env->dr[7], index);
        return !env->sgx_dbgoptin && a < env->sgx_elbase + env->sgx_elsize &&
               a + len > env->sgx_elbase;
    default:
        return false;
    }
}
#endif /* __Use_Original_Qemu (U1263) */

static int hw_breakpoint_insert(CPUX86State *env, int index)
{
    CPUState *cs = env_cpu(env);
    target_ulong dr7 = env->dr[7];
    target_ulong drN = env->dr[index];
    int err = 0;

    switch (hw_breakpoint_type(dr7, index)) {
    case DR7_TYPE_BP_INST:
#if __Use_Original_Qemu != 1 /* ours (U1263) */
        if (hw_breakpoint_sgx_suppressed(env, index)) {
            break;      /* not inserted while suppressed inside an enclave (U1263) */
        }
#endif /* __Use_Original_Qemu (U1263) */
        if (hw_breakpoint_enabled(dr7, index)) {
            err = cpu_breakpoint_insert(cs, drN, BP_CPU,
                                        &env->cpu_breakpoint[index]);
        }
        break;

    case DR7_TYPE_IO_RW:
        /* Notice when we should enable calls to bpt_io.  */
        return hw_breakpoint_enabled(env->dr[7], index)
               ? HF_IOBPT_MASK : 0;

    case DR7_TYPE_DATA_WR:
        if (hw_breakpoint_enabled(dr7, index)) {
            err = cpu_watchpoint_insert(cs, drN,
                                        hw_breakpoint_len(dr7, index),
                                        BP_CPU | BP_MEM_WRITE,
                                        &env->cpu_watchpoint[index]);
        }
        break;

    case DR7_TYPE_DATA_RW:
        if (hw_breakpoint_enabled(dr7, index)) {
            err = cpu_watchpoint_insert(cs, drN,
                                        hw_breakpoint_len(dr7, index),
                                        BP_CPU | BP_MEM_ACCESS,
                                        &env->cpu_watchpoint[index]);
        }
        break;
    }
    if (err) {
        env->cpu_breakpoint[index] = NULL;
    }
    return 0;
}

static void hw_breakpoint_remove(CPUX86State *env, int index)
{
    CPUState *cs = env_cpu(env);

    switch (hw_breakpoint_type(env->dr[7], index)) {
    case DR7_TYPE_BP_INST:
        if (env->cpu_breakpoint[index]) {
            cpu_breakpoint_remove_by_ref(cs, env->cpu_breakpoint[index]);
            env->cpu_breakpoint[index] = NULL;
        }
        break;

    case DR7_TYPE_DATA_WR:
    case DR7_TYPE_DATA_RW:
        if (env->cpu_breakpoint[index]) {
            cpu_watchpoint_remove_by_ref(cs, env->cpu_watchpoint[index]);
            env->cpu_breakpoint[index] = NULL;
        }
        break;

    case DR7_TYPE_IO_RW:
        /* HF_IOBPT_MASK cleared elsewhere.  */
        break;
    }
}

#if __Use_Original_Qemu != 1 /* ours (U1263) */
/*
 * NoVmp (ledger U1263): re-evaluate the instruction breakpoints after an enclave entry or exit
 * (suppressed ones are not inserted, so the translator does not stop on them; removing and
 * inserting a breakpoint invalidates the translations of its address).
 */
void x86_sgx_bp_update(CPUX86State *env)
{
    int i;

    for (i = 0; i < DR7_MAX_BP; i++) {
        if (hw_breakpoint_type(env->dr[7], i) == DR7_TYPE_BP_INST) {
            hw_breakpoint_remove(env, i);
            hw_breakpoint_insert(env, i);
        }
    }
}
#endif /* __Use_Original_Qemu (U1263) */

void cpu_x86_update_dr7(CPUX86State *env, uint32_t new_dr7)
{
    target_ulong old_dr7 = env->dr[7];
    int iobpt = 0;
    int i;

    new_dr7 |= DR7_FIXED_1;

    /* If nothing is changing except the global/local enable bits,
       then we can make the change more efficient.  */
    if (((old_dr7 ^ new_dr7) & ~0xff) == 0) {
        /* Fold the global and local enable bits together into the
           global fields, then xor to show which registers have
           changed collective enable state.  */
        int mod = ((old_dr7 | old_dr7 * 2) ^ (new_dr7 | new_dr7 * 2)) & 0xff;

        for (i = 0; i < DR7_MAX_BP; i++) {
            if ((mod & (2 << i * 2)) && !hw_breakpoint_enabled(new_dr7, i)) {
                hw_breakpoint_remove(env, i);
            }
        }
        env->dr[7] = new_dr7;
        for (i = 0; i < DR7_MAX_BP; i++) {
            if (mod & (2 << i * 2) && hw_breakpoint_enabled(new_dr7, i)) {
                iobpt |= hw_breakpoint_insert(env, i);
            } else if (hw_breakpoint_type(new_dr7, i) == DR7_TYPE_IO_RW
                       && hw_breakpoint_enabled(new_dr7, i)) {
                iobpt |= HF_IOBPT_MASK;
            }
        }
    } else {
        for (i = 0; i < DR7_MAX_BP; i++) {
            hw_breakpoint_remove(env, i);
        }
        env->dr[7] = new_dr7;
        for (i = 0; i < DR7_MAX_BP; i++) {
            iobpt |= hw_breakpoint_insert(env, i);
        }
    }

    env->hflags = (env->hflags & ~HF_IOBPT_MASK) | iobpt;
}

#if __Use_Original_Qemu == 1 /* original QEMU (U1761) */
static bool check_hw_breakpoints(CPUX86State *env, bool force_dr6_update)
#else /* ours (U1761) */
/*
 * NoVmp (ledger U1761, Gate 1 step 23 D-c): 'inst' = false leaves the instruction breakpoints out. A trap-class
 * #DB taken at the boundary before the instruction at RIP (single step) must not report the instruction
 * breakpoint of that instruction: its condition has not been met yet (SDM 093 Vol3B 20.2.3 p. 20-4: B0-B3 "are
 * set if the condition described for each breakpoint by the LENn, and R/Wn flags in debug control register DR7
 * is true"; Table 20-2 p. 20-8: single-step trap BS = 1). The i5-13600K reports DR6 = FFFF4FF0H (BS only) for a
 * single step onto an enabled or a disabled instruction breakpoint (cases_dbgfix_hw.txt); QEMU 11.1 sets Bn.
 */
static bool check_hw_breakpoints_inst(CPUX86State *env, bool force_dr6_update, bool inst)
#endif /* __Use_Original_Qemu (U1761) */
{
    target_ulong dr6;
    int reg;
    bool hit_enabled = false;

    dr6 = env->dr[6] & ~0xf;
    for (reg = 0; reg < DR7_MAX_BP; reg++) {
        bool bp_match = false;
        bool wp_match = false;

        switch (hw_breakpoint_type(env->dr[7], reg)) {
        case DR7_TYPE_BP_INST:
#if __Use_Original_Qemu == 1 /* original QEMU (U1761) */
            if (env->dr[reg] == env->eip) {
#else /* ours (U1761) */
            if (inst && env->dr[reg] == env->eip) {
#endif /* __Use_Original_Qemu (U1761) */
                bp_match = true;
            }
            break;
        case DR7_TYPE_DATA_WR:
        case DR7_TYPE_DATA_RW:
            if (env->cpu_watchpoint[reg] &&
                env->cpu_watchpoint[reg]->flags & BP_WATCHPOINT_HIT) {
                wp_match = true;
            }
            break;
        case DR7_TYPE_IO_RW:
            break;
        }
#if __Use_Original_Qemu != 1 /* ours (U1263) */
        if (hw_breakpoint_sgx_suppressed(env, reg)) {
            bp_match = wp_match = false;    /* no match is reported (Vol3D 43.3.1, U1263) */
        }
#endif /* __Use_Original_Qemu (U1263) */
        if (bp_match || wp_match) {
            dr6 |= 1ULL << reg;
            if (hw_breakpoint_enabled(env->dr[7], reg)) {
                hit_enabled = true;
            }
        }
    }

    if (hit_enabled || force_dr6_update) {
        env->dr[6] = dr6;
    }

    return hit_enabled;
}

#if __Use_Original_Qemu != 1 /* ours (U1761) */
static bool check_hw_breakpoints(CPUX86State *env, bool force_dr6_update)
{
    return check_hw_breakpoints_inst(env, force_dr6_update, true);
}
#endif /* __Use_Original_Qemu (U1761) */
#if __Use_Original_Qemu != 1 /* ours (U1760) */

/*
 * cpu-exec.c: an exception or software interrupt is about to be delivered (through the IDT or to UC_HOOK_INTR).
 * U1760: it ends a MOV SS / POP SS shadow (Vol3A 7.8.3: "The suppression and inhibition ends after delivery of an
 * exception or the execution of the next instruction"): the instruction in the shadow raised it, so the shadow's
 * TB never reached its end (do_gen_eob_worker), and the handler's first instruction must not count as shadowed.
 * U1762: a held data breakpoint survives INT n / INT3 / INTO and its #DB follows the transition to the handler
 * (CPU_INTERRUPT_DEBUG -> breakpoint_handler); any other event loses it.
 */
void x86_movss_event(CPUX86State *env)
{
    if (x86_movss_shadow(env)) {
        env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    }
}
#endif /* __Use_Original_Qemu (U1760) */

#if __Use_Original_Qemu != 1 /* ours (U709) */
/*
 * NoVmp (ledger U709): a gather / scatter element is about to fault while a data breakpoint
 * hit by an earlier element of the same execution is pending (QEMU re-runs the instruction
 * after a watchpoint hit with cs->watchpoint_hit set and requests CPU_INTERRUPT_DEBUG for
 * after it; the fault came first and the #DB after it). SDM Vol2C VPGATHERDD/VSCATTERDPS, Vol2B
 * VPGATHERDD (AVX2): "If any traps or interrupts are pending from already gathered elements,
 * they will be delivered in lieu of the exception; in this case, EFLAG.RF is set to one so an
 * instruction breakpoint is not re-triggered when the instruction is continued." Raises that
 * #DB (DR6 as breakpoint_handler, RF = 1, RIP at the instruction: the completed elements stay
 * done) and does not return; returns when no data breakpoint is pending.
 */
void x86_deliver_pending_data_bp(CPUX86State *env, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    if (!cs->watchpoint_hit || !(cs->watchpoint_hit->flags & BP_CPU)) {
        return;
    }
    cs->watchpoint_hit = NULL;
    cpu_reset_interrupt(cs, CPU_INTERRUPT_DEBUG);
#if __Use_Original_Qemu != 1 /* ours (U1305) */
    x86_vmx_db_snapshot(env);       /* VMX: DR6 / DR7 before this #DB (30.1) */
#endif /* __Use_Original_Qemu (U1305) */
    if (check_hw_breakpoints(env, false)) {
        env->eflags |= RF_MASK;
        #if __Use_Original_Qemu != 1 /* ours (U1171) */
        env->fred_evdata_next = env->dr[6] & 0xf;   /* FRED #DB event data (U1171) */
        #endif /* __Use_Original_Qemu (U1171) */
        raise_exception_ra(env, EXCP01_DB, ra);
    }
}
#endif /* __Use_Original_Qemu (U709) */

void breakpoint_handler(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    CPUBreakpoint *bp;

    if (cs->watchpoint_hit) {
        if (cs->watchpoint_hit->flags & BP_CPU) {
            cs->watchpoint_hit = NULL;
#if __Use_Original_Qemu != 1 /* ours (U1305) */
            x86_vmx_db_snapshot(env);   /* VMX: DR6 / DR7 before this #DB */
#endif /* __Use_Original_Qemu (U1305) */
            if (check_hw_breakpoints(env, false)) {
                #if __Use_Original_Qemu != 1 /* ours (U1171) */
                env->fred_evdata_next = env->dr[6] & 0xf;   /* FRED #DB event data (U1171) */
                #endif /* __Use_Original_Qemu (U1171) */
                raise_exception(env, EXCP01_DB);
            } else {
                cpu_loop_exit_noexc(cs);
            }
        }
    } else {
        QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
            if (bp->pc == env->eip) {
                if (bp->flags & BP_CPU) {
#if __Use_Original_Qemu != 1 /* ours (U1305) */
                    x86_vmx_db_snapshot(env);   /* VMX: DR6 / DR7 before this #DB */
#endif /* __Use_Original_Qemu (U1305) */
                    check_hw_breakpoints(env, true);
                    #if __Use_Original_Qemu != 1 /* ours (U1171) */
                    env->fred_evdata_next = env->dr[6] & 0xf;   /* FRED #DB event data (U1171) */
                    #endif /* __Use_Original_Qemu (U1171) */
                    raise_exception(env, EXCP01_DB);
                }
                break;
            }
        }
    }
}

void helper_single_step(CPUX86State *env)
{
#if __Use_Original_Qemu != 1 /* ours (U1321) */
    x86_vmx_single_step(env);       /* VMX: a pending MTF VM exit comes before this #DB (28.5.2) */
#endif /* __Use_Original_Qemu (U1321) */
#if __Use_Original_Qemu != 1 /* ours (U1305) */
    x86_vmx_db_snapshot(env);       /* VMX: DR6 / DR7 before this #DB (30.1) */
#endif /* __Use_Original_Qemu (U1305) */
#if __Use_Original_Qemu == 1 /* original QEMU (U1761) */
    check_hw_breakpoints(env, true);
#else /* ours (U1761) */
    check_hw_breakpoints_inst(env, true, false);   /* not the instruction breakpoint at the next RIP */
#endif /* __Use_Original_Qemu (U1761) */
    env->dr[6] |= DR6_BS;
    #if __Use_Original_Qemu != 1 /* ours (U1171) */
    env->fred_evdata_next = (env->dr[6] & 0xf) | DR6_BS;   /* FRED #DB event data (U1171) */
    #endif /* __Use_Original_Qemu (U1171) */
    raise_exception(env, EXCP01_DB);
}

void helper_rechecking_single_step(CPUX86State *env)
{
    if ((env->eflags & TF_MASK) != 0) {
        helper_single_step(env);
    }
}

#if __Use_Original_Qemu != 1 /* ours (U1760) */
/*
 * NoVmp (ledger U1760, Gate 1 step 23 D-a): an instruction breakpoint (DR7) on the instruction after MOV SS /
 * POP SS is suppressed. SDM 093 Vol3A 7.8.3 p. 7-8: "Any instruction breakpoint on the next instruction is
 * suppressed (as if EFLAGS.RF were 1)."; 7.8.2 p. 7-8; Vol3B 20.3.1.1 p. 20-8: "if an instruction breakpoint is
 * placed on an instruction located immediately after a POP SS/MOV SS instruction, the breakpoint will be
 * suppressed as if EFLAGS.RF were 1". The i5-13600K: no #DB for MOV SS r32 / m16 and POP SS (compatibility
 * mode), one #DB with BS after the next instruction when TF is set too (cases_dbgfix_hw.txt). The translator
 * calls this instead of helper_debug for a DR7 breakpoint on the first instruction of a TB that starts in an
 * interrupt shadow (HF_INHIBIT_IRQ in the TB flags): the shadow's cause is known at run time only (env->fred_sti:
 * 1 STI, which does not suppress it; 0 MOV / POP SS). A MOV / POP SS in a shadow opens none (U1752), so the
 * instruction after MOV SS; MOV SS is not shadowed and its breakpoint fires (as measured). QEMU 11.1 ignores the
 * shadow (only RF suppresses).
 */
void helper_debug_shadow(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    if (x86_movss_shadow(env)) {
        return;
    }
    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}
#endif /* __Use_Original_Qemu (U1760) */

/*
 * Unicorn: store a debug register without the MOV DR checks (uc_reg_write must
 * not raise a guest exception). reg is 0-3, 6 or 7.
 */
void x86_store_dr(CPUX86State *env, int reg, target_ulong t0)
{
    if (reg < 4) {
        if (hw_breakpoint_enabled(env->dr[7], reg)
            && hw_breakpoint_type(env->dr[7], reg) != DR7_TYPE_IO_RW) {
            hw_breakpoint_remove(env, reg);
            env->dr[reg] = t0;
            hw_breakpoint_insert(env, reg);
        } else {
            env->dr[reg] = t0;
        }
    } else if (reg == 6) {
        env->dr[6] = t0 | DR6_FIXED_1;
    } else {
        cpu_x86_update_dr7(env, t0);
    }
}

/*
 * backport of QEMU 533883fd7e: MOV DR4/DR5 with CR4.DE = 1 is #UD without an
 * error code; MOV to DR6/DR7 with any of bits 63:32 set is #GP(0) (SDM Vol2B
 * MOV-Move to/from Debug Registers).
 */
target_ulong helper_get_dr(CPUX86State *env, int reg)
{
    if (reg >= 4 && reg < 6) {
        if (env->cr[4] & CR4_DE_MASK) {
            raise_exception_ra(env, EXCP06_ILLOP, GETPC());
        } else {
            reg += 2;
        }
    }

    /*
     * backport of QEMU 57f8dbdbe9: DR7.GD = 1 makes any MOV DR a fault-class #DB
     * with DR6.BD = 1; GD is cleared so the handler can use the debug registers
     * (SDM Vol3B 20.2.4, 20.3.1.3)
     */
    if (env->dr[7] & DR7_GD) {
#if __Use_Original_Qemu != 1 /* ours (U1305) */
        x86_vmx_db_snapshot(env);   /* VMX: DR6 / DR7 before this #DB (30.1) */
#endif /* __Use_Original_Qemu (U1305) */
        env->dr[7] &= ~DR7_GD;
        env->dr[6] |= DR6_BD;
        #if __Use_Original_Qemu != 1 /* ours (U1171) */
        env->fred_evdata_next = DR6_BD;   /* FRED #DB event data (U1171) */
        #endif /* __Use_Original_Qemu (U1171) */
        raise_exception_ra(env, EXCP01_DB, GETPC());
    }

    return env->dr[reg];
}

void helper_set_dr(CPUX86State *env, int reg, target_ulong t0)
{
    if (reg >= 4 && reg < 6) {
        if (env->cr[4] & CR4_DE_MASK) {
            raise_exception_ra(env, EXCP06_ILLOP, GETPC());
        } else {
            reg += 2;
        }
    }

    if (env->dr[7] & DR7_GD) {
#if __Use_Original_Qemu != 1 /* ours (U1305) */
        x86_vmx_db_snapshot(env);   /* VMX: DR6 / DR7 before this #DB (30.1) */
#endif /* __Use_Original_Qemu (U1305) */
        env->dr[7] &= ~DR7_GD;
        env->dr[6] |= DR6_BD;
        #if __Use_Original_Qemu != 1 /* ours (U1171) */
        env->fred_evdata_next = DR6_BD;   /* FRED #DB event data (U1171) */
        #endif /* __Use_Original_Qemu (U1171) */
        raise_exception_ra(env, EXCP01_DB, GETPC());
    }

    if (reg >= 4 && (t0 & DR_RESERVED_MASK)) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
    x86_store_dr(env, reg, t0);
}

/* Check if Port I/O is trapped by a breakpoint.  */
void helper_bpt_io(CPUX86State *env, uint32_t port,
                   uint32_t size, target_ulong next_eip)
{
    target_ulong dr7 = env->dr[7];
    int i, hit = 0;

    for (i = 0; i < DR7_MAX_BP; ++i) {
        if (hw_breakpoint_type(dr7, i) == DR7_TYPE_IO_RW
            && hw_breakpoint_enabled(dr7, i)) {
            int bpt_len = hw_breakpoint_len(dr7, i);
            if (port + size - 1 >= env->dr[i]
                && port <= env->dr[i] + bpt_len - 1) {
                hit |= 1 << i;
            }
        }
    }

    if (hit) {
#if __Use_Original_Qemu != 1 /* ours (U1305) */
        x86_vmx_db_snapshot(env);   /* VMX: DR6 / DR7 before this #DB (30.1) */
#endif /* __Use_Original_Qemu (U1305) */
        env->dr[6] = (env->dr[6] & ~0xf) | hit;
        env->eip = next_eip;
        #if __Use_Original_Qemu != 1 /* ours (U1171) */
        env->fred_evdata_next = hit;   /* FRED #DB event data (U1171) */
        #endif /* __Use_Original_Qemu (U1171) */
        raise_exception(env, EXCP01_DB);
    }
}
