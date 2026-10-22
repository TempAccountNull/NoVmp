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
 * NoVmp (ledger U1761, Gate 1 step 23 D-c; U1854): 'inst' selects the instruction breakpoints at RIP that are
 * reported: HX_BP_INST_NONE none (the data-breakpoint paths), HX_BP_INST_ALL every one, enabled or not (QEMU), and
 * HX_BP_INST_ENABLED (U1854) only the enabled ones (L / G bit in DR7) - the single-step #DB (helper_single_step).
 * U1761 left them out of the single-step #DB (BS only), fitted to the VMware guest, which reports FFFF4FF0H. U1854:
 * the i5-13600K on the bare host reports the enabled breakpoint at the next RIP with BS, FFFF4FF1H, on P- and E-cores,
 * and BS only (FFFF4FF0H) for a disabled one (H22 item 8, cases_probe_p2dr_hw lines 14 / 16, 20 runs each); the guest
 * drops B0 when it reports BS (docs/quirks.md "VMware drops DR6.B0 when BS is set"). SDM 253669-093 Vol3B 20.2.3
 * p. 20-4: B0-B3 "They may or may not be set if the breakpoint is not enabled", BS "When the BS flag is set, any of
 * the other debug status bits also may be set" - hardware-decided. QEMU 11.1 sets Bn for every type-0 DRn == EIP.
 */
#if __Use_Original_Qemu != 1 /* ours (U1854) */
#define HX_BP_INST_NONE     0
#define HX_BP_INST_ALL      1
#define HX_BP_INST_ENABLED  2
#endif /* __Use_Original_Qemu (U1854) */
static bool check_hw_breakpoints_inst(CPUX86State *env, bool force_dr6_update, int inst)
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
            if (inst != HX_BP_INST_NONE && env->dr[reg] == env->eip &&
                (inst == HX_BP_INST_ALL || hw_breakpoint_enabled(env->dr[7], reg))) {    /* U1854 */
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
    return check_hw_breakpoints_inst(env, force_dr6_update, HX_BP_INST_ALL);
}
#endif /* __Use_Original_Qemu (U1761) */
#if __Use_Original_Qemu != 1 /* ours (U1762) */
/*
 * NoVmp (ledger U1762, Gate 1 step 23 D-b): a data breakpoint that a MOV SS / POP SS hits (its memory operand) is
 * held until the instruction boundary after the next instruction. SDM 093 Vol3A 7.8.3 p. 7-9: "Any data
 * breakpoint on the MOV to SS instruction or POP to SS instruction is inhibited until the instruction boundary
 * following the next instruction."; Vol3B 20.3.1.2 p. 20-9: "If an execution of the MOV or POP instruction loads
 * the SS register and encounters a data breakpoint, the resulting debug exception is delivered after completion
 * of the next instruction (the one after the MOV or POP)." / "Any pending data or I/O breakpoints are lost upon
 * delivery of an exception. ... the data breakpoint is lost if the next instruction causes a fault." /
 * "Delivery of events due to INT n, INT3, or INTO does not cause a loss of data breakpoints" (the #DB "will be
 * delivered after the transition to the software-interrupt handler", Vol3A 7.15 Event 1). The i5-13600K holds it
 * over the next instruction (MOV SS m16: trap after the INC; POP SS in compatibility mode: after MOV ESP;
 * cases_dbgfix_hw.txt). QEMU 11.1 raises the #DB at once ("FIXME: #DB should be delayed by one instruction").
 * A MOV / POP SS in a shadow opens none ("only the first is guaranteed", U1752), so its breakpoint is reported at
 * once.
 */

/*
 * raise the held #DB: B0-B3 of the held breakpoints, those of a data breakpoint the next instruction hit itself
 * (QEMU re-ran it with watchpoint_hit set and requested CPU_INTERRUPT_DEBUG for after it: one #DB reports both)
 * and BS when the single-step trap of the next instruction is due as well (Vol3B 20.2.3: "When the BS flag is set,
 * any of the other debug status bits also may be set")
 */
static void x86_movss_held_deliver(CPUX86State *env, bool bs)
{
    CPUState *cs = env_cpu(env);
    uint32_t bits = env->movss_held_db;

    env->movss_held_db = 0;
    if (cs->watchpoint_hit && (cs->watchpoint_hit->flags & BP_CPU)) {
        target_ulong old = env->dr[6];

        cs->watchpoint_hit = NULL;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_DEBUG);
        if (check_hw_breakpoints_inst(env, false, HX_BP_INST_NONE)) {
            bits |= env->dr[6] & 0xf;
        }
        env->dr[6] = old;
    }
    if (bs) {
        x86_vmx_single_step(env);       /* VMX: a pending MTF VM exit comes before this #DB (U1321) */
    }
    x86_vmx_db_snapshot(env);           /* VMX: DR6 / DR7 before this #DB (U1305) */
    env->dr[6] = (env->dr[6] & ~(target_ulong)0xf) | bits | (bs ? DR6_BS : 0);
    env->fred_evdata_next = bits | (bs ? DR6_BS : 0);  /* FRED #DB event data (U1171) */
    raise_exception(env, EXCP01_DB);
}

/*
 * the end of a TB that started in an interrupt shadow (one instruction; translate.c do_gen_eob_worker): deliver a
 * held data breakpoint. tf: 0 no single-step trap follows, 1 one does (TF), 2 one does if EFLAGS.TF (SYSCALL /
 * SYSRET recheck), 3 one does unless the shadow came from STI (a chained MOV / POP SS, U1752)
 */
void helper_movss_held_db(CPUX86State *env, uint32_t tf)
{
    bool bs;

    if (!env->movss_held_db) {
        return;
    }
    bs = tf == 1 || (tf == 2 && (env->eflags & TF_MASK)) || (tf == 3 && !env->fred_sti);
    x86_movss_held_deliver(env, bs);
}
#endif /* __Use_Original_Qemu (U1762) */
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
#if __Use_Original_Qemu != 1 /* ours (U1762) */
    if (env->movss_held_db) {
        if (env->exception_is_int) {
            cpu_interrupt(env_cpu(env), CPU_INTERRUPT_DEBUG);
        } else {
            env->movss_held_db = 0;
        }
    }
#endif /* __Use_Original_Qemu (U1762) */
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
#if __Use_Original_Qemu != 1 /* ours (U1762) */
            /* the MOV / POP SS that just ran opened a shadow: hold the breakpoint (see x86_movss_shadow) */
            if (x86_movss_shadow(env)) {
                target_ulong old = env->dr[6];
                int i;

                if (check_hw_breakpoints_inst(env, false, HX_BP_INST_NONE)) {
                    env->movss_held_db |= env->dr[6] & 0xf;
                }
                env->dr[6] = old;
                for (i = 0; i < DR7_MAX_BP; i++) {
                    int type = hw_breakpoint_type(env->dr[7], i);

                    if ((type == DR7_TYPE_DATA_WR || type == DR7_TYPE_DATA_RW) && env->cpu_watchpoint[i]) {
                        env->cpu_watchpoint[i]->flags &= ~BP_WATCHPOINT_HIT;   /* held, not hit again */
                    }
                }
                cpu_loop_exit_noexc(cs);
            }
#endif /* __Use_Original_Qemu (U1762) */
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
#if __Use_Original_Qemu != 1 /* ours (U1762) */
        /* CPU_INTERRUPT_DEBUG of x86_movss_event: an INT n / INT3 / INTO in the shadow was delivered */
        if (env->movss_held_db) {
            x86_movss_held_deliver(env, false);
        }
#endif /* __Use_Original_Qemu (U1762) */
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
#if __Use_Original_Qemu != 1 /* ours (U1770) */
    /*
     * NoVmp (ledger U1770, D-f): the single-stepped instruction hit a data breakpoint. QEMU re-ran it with
     * cs->watchpoint_hit set and requested CPU_INTERRUPT_DEBUG for after it (exec.c cpu_check_watchpoint), which
     * raised a second #DB once this one had been delivered. Both are trap-class debug conditions of the same
     * instruction boundary and one #DB reports them: SDM 093 Vol3B 20.3.1 p. 20-7 "The debug exception may report
     * one or both at one time."; 20.2.3 p. 20-4 "When the BS flag is set, any of the other debug status bits also may
     * be set."; Vol3A 7.9 Table 7-2 p. 7-9 (class 4: #DB due to TF or a data / I/O breakpoint). The i5-13600K takes
     * one #DB (cases_dbgfix_hw.txt, D-f). The pending request is consumed here; DR6 is unchanged: B0-B3 of the data
     * breakpoint (its BP_WATCHPOINT_HIT, check_hw_breakpoints_inst below) and BS, as before (the bare host reports B0
     * with BS, P- and E-cores, H22 item 1; the VMware guest drops B0, docs/quirks.md).
     */
    if (env_cpu(env)->watchpoint_hit && (env_cpu(env)->watchpoint_hit->flags & BP_CPU)) {
        env_cpu(env)->watchpoint_hit = NULL;
        cpu_reset_interrupt(env_cpu(env), CPU_INTERRUPT_DEBUG);
    }
#endif /* __Use_Original_Qemu (U1770) */
#if __Use_Original_Qemu == 1 /* original QEMU (U1761) */
    check_hw_breakpoints(env, true);
#else /* ours (U1761) */
    /* U1854: the instruction breakpoint at the next RIP only when it is enabled (bare host: B0 | BS) */
    check_hw_breakpoints_inst(env, true, HX_BP_INST_ENABLED);
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
