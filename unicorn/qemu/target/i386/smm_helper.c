/*
 *  x86 SMM helpers
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
#if __Use_Original_Qemu != 1 /* ours (U1461) */
#include "exec/exec-all.h"
#include "uc_priv.h"
#endif /* __Use_Original_Qemu (U1461) */

/* SMM support */

/*
 * backport of upstream QEMU cdba90ac1b (DR6 / DR7 through helper_set_dr on SMM entry and
 * RSM, so the debug-register breakpoints follow), 5d08f36983 (the 64-bit and 32-bit state-save
 * code in separate functions) and e932132963 (the map chosen at run time from CPUID LM instead
 * of TARGET_X86_64) - U1460
 */
static void sm_state_init_64(X86CPU *cpu)
{
#ifdef TARGET_X86_64
    CPUX86State *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    SegmentCache *dt;
    int i, offset;
    target_ulong sm_state = env->smbase + 0x8000;

    for (i = 0; i < 6; i++) {
        dt = &env->segs[i];
        offset = 0x7e00 + i * 16;
        x86_stw_phys(cs, sm_state + offset, dt->selector);
        x86_stw_phys(cs, sm_state + offset + 2, (dt->flags >> 8) & 0xf0ff);
        x86_stl_phys(cs, sm_state + offset + 4, dt->limit);
        x86_stq_phys(cs, sm_state + offset + 8, dt->base);
    }

    x86_stq_phys(cs, sm_state + 0x7e68, env->gdt.base);
    x86_stl_phys(cs, sm_state + 0x7e64, env->gdt.limit);

    x86_stw_phys(cs, sm_state + 0x7e70, env->ldt.selector);
    x86_stq_phys(cs, sm_state + 0x7e78, env->ldt.base);
    x86_stl_phys(cs, sm_state + 0x7e74, env->ldt.limit);
    x86_stw_phys(cs, sm_state + 0x7e72, (env->ldt.flags >> 8) & 0xf0ff);

    x86_stq_phys(cs, sm_state + 0x7e88, env->idt.base);
    x86_stl_phys(cs, sm_state + 0x7e84, env->idt.limit);

    x86_stw_phys(cs, sm_state + 0x7e90, env->tr.selector);
    x86_stq_phys(cs, sm_state + 0x7e98, env->tr.base);
    x86_stl_phys(cs, sm_state + 0x7e94, env->tr.limit);
    x86_stw_phys(cs, sm_state + 0x7e92, (env->tr.flags >> 8) & 0xf0ff);

    /* ??? Vol 1, 16.5.6 Intel MPX and SMM says that IA32_BNDCFGS
       is saved at offset 7ED0.  Vol 3, 34.4.1.1, Table 32-2, has
       7EA0-7ED7 as "reserved".  What's this, and what's really
       supposed to happen?  */
    x86_stq_phys(cs, sm_state + 0x7ed0, env->efer);

    x86_stq_phys(cs, sm_state + 0x7ff8, env->regs[R_EAX]);
    x86_stq_phys(cs, sm_state + 0x7ff0, env->regs[R_ECX]);
    x86_stq_phys(cs, sm_state + 0x7fe8, env->regs[R_EDX]);
    x86_stq_phys(cs, sm_state + 0x7fe0, env->regs[R_EBX]);
    x86_stq_phys(cs, sm_state + 0x7fd8, env->regs[R_ESP]);
    x86_stq_phys(cs, sm_state + 0x7fd0, env->regs[R_EBP]);
    x86_stq_phys(cs, sm_state + 0x7fc8, env->regs[R_ESI]);
    x86_stq_phys(cs, sm_state + 0x7fc0, env->regs[R_EDI]);
    for (i = 8; i < 16; i++) {
        x86_stq_phys(cs, sm_state + 0x7ff8 - i * 8, env->regs[i]);
    }
    x86_stq_phys(cs, sm_state + 0x7f78, env->eip);
    x86_stl_phys(cs, sm_state + 0x7f70, cpu_compute_eflags(env));
    x86_stl_phys(cs, sm_state + 0x7f68, env->dr[6]);
    x86_stl_phys(cs, sm_state + 0x7f60, env->dr[7]);

    x86_stl_phys(cs, sm_state + 0x7f48, env->cr[4]);
    x86_stq_phys(cs, sm_state + 0x7f50, env->cr[3]);
    x86_stl_phys(cs, sm_state + 0x7f58, env->cr[0]);

    x86_stl_phys(cs, sm_state + 0x7efc, 0x00020064);    /* SMM revision ID */
    x86_stl_phys(cs, sm_state + 0x7f00, env->smbase);
#else
    g_assert_not_reached();
#endif
}

static void sm_state_init_32(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    SegmentCache *dt;
    int i, offset;
    target_ulong sm_state = env->smbase + 0x8000;

    x86_stl_phys(cs, sm_state + 0x7ffc, env->cr[0]);
    x86_stl_phys(cs, sm_state + 0x7ff8, env->cr[3]);
    x86_stl_phys(cs, sm_state + 0x7ff4, cpu_compute_eflags(env));
    x86_stl_phys(cs, sm_state + 0x7ff0, env->eip);
    x86_stl_phys(cs, sm_state + 0x7fec, env->regs[R_EDI]);
    x86_stl_phys(cs, sm_state + 0x7fe8, env->regs[R_ESI]);
    x86_stl_phys(cs, sm_state + 0x7fe4, env->regs[R_EBP]);
    x86_stl_phys(cs, sm_state + 0x7fe0, env->regs[R_ESP]);
    x86_stl_phys(cs, sm_state + 0x7fdc, env->regs[R_EBX]);
    x86_stl_phys(cs, sm_state + 0x7fd8, env->regs[R_EDX]);
    x86_stl_phys(cs, sm_state + 0x7fd4, env->regs[R_ECX]);
    x86_stl_phys(cs, sm_state + 0x7fd0, env->regs[R_EAX]);
    x86_stl_phys(cs, sm_state + 0x7fcc, env->dr[6]);
    x86_stl_phys(cs, sm_state + 0x7fc8, env->dr[7]);

    x86_stl_phys(cs, sm_state + 0x7fc4, env->tr.selector);
    x86_stl_phys(cs, sm_state + 0x7f64, env->tr.base);
    x86_stl_phys(cs, sm_state + 0x7f60, env->tr.limit);
    x86_stl_phys(cs, sm_state + 0x7f5c, (env->tr.flags >> 8) & 0xf0ff);

    x86_stl_phys(cs, sm_state + 0x7fc0, env->ldt.selector);
    x86_stl_phys(cs, sm_state + 0x7f80, env->ldt.base);
    x86_stl_phys(cs, sm_state + 0x7f7c, env->ldt.limit);
    x86_stl_phys(cs, sm_state + 0x7f78, (env->ldt.flags >> 8) & 0xf0ff);

    x86_stl_phys(cs, sm_state + 0x7f74, env->gdt.base);
    x86_stl_phys(cs, sm_state + 0x7f70, env->gdt.limit);

    x86_stl_phys(cs, sm_state + 0x7f58, env->idt.base);
    x86_stl_phys(cs, sm_state + 0x7f54, env->idt.limit);

    for (i = 0; i < 6; i++) {
        dt = &env->segs[i];
        if (i < 3) {
            offset = 0x7f84 + i * 12;
        } else {
            offset = 0x7f2c + (i - 3) * 12;
        }
        x86_stl_phys(cs, sm_state + 0x7fa8 + i * 4, dt->selector);
        x86_stl_phys(cs, sm_state + offset + 8, dt->base);
        x86_stl_phys(cs, sm_state + offset + 4, dt->limit);
        x86_stl_phys(cs, sm_state + offset, (dt->flags >> 8) & 0xf0ff);
    }
    x86_stl_phys(cs, sm_state + 0x7f14, env->cr[4]);

    x86_stl_phys(cs, sm_state + 0x7efc, 0x00020000);   /* SMM revision ID */
    x86_stl_phys(cs, sm_state + 0x7ef8, env->smbase);
}

void do_smm_enter(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    // qemu_log_mask(CPU_LOG_INT, "SMM: enter\n");
    // log_cpu_state_mask(CPU_LOG_INT, CPU(cpu), CPU_DUMP_CCOP);

#if __Use_Original_Qemu != 1 /* ours (U1263) */
    /* an SMI in enclave mode is an enclave-exiting event (SDM 092 Vol3D 40.4, 42.8.2, sgx_model.c) */
    bool sgx_enc = env->sgx_enclave_mode != 0;

    if (sgx_enc) {
        x86_sgx_aex(env, -1, 0, 1, 0, 0);
        env->sgx_enc_event = 0;
    }
#endif /* __Use_Original_Qemu (U1263) */
    env->msr_smi_count++;
    env->hflags |= HF_SMM_MASK;
    if (env->hflags2 & HF2_NMI_MASK) {
        env->hflags2 |= HF2_SMM_INSIDE_NMI_MASK;
    } else {
        env->hflags2 |= HF2_NMI_MASK;
    }

    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        sm_state_init_64(cpu);
#if __Use_Original_Qemu != 1 /* ours (U1263) */
        if (env->sgx) {
            /* Vol3D Table 42-1: SMRAM 7EE0H bit 1 ENCLAVE_INTERRUPTION (U1263) */
            CPUState *cs = CPU(cpu);
            target_ulong sm_state = env->smbase + 0x8000;
            uint32_t f = x86_ldl_phys(cs, sm_state + 0x7ee0);

            x86_stl_phys(cs, sm_state + 0x7ee0, (f & ~2u) | (sgx_enc ? 2u : 0));
        }
#endif /* __Use_Original_Qemu (U1263) */
        cpu_load_efer(env, 0);
    } else {
        sm_state_init_32(cpu);
    }

    /* init SMM cpu state */

    cpu_load_eflags(env, 0, ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C |
                              DF_MASK));
    env->eip = 0x00008000;
    cpu_x86_update_cr0(env,
                       env->cr[0] & ~(CR0_PE_MASK | CR0_EM_MASK | CR0_TS_MASK |
                                      CR0_PG_MASK));
    cpu_x86_update_cr4(env, 0);
    helper_set_dr(env, 7, 0x00000400);

    cpu_x86_load_seg_cache(env, R_CS, (env->smbase >> 4) & 0xffff, env->smbase,
                           0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffffffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_G_MASK | DESC_A_MASK);
}

static void rsm_load_regs_64(CPUX86State *env)
{
#ifdef TARGET_X86_64
    CPUState *cs = env_cpu(env);
    target_ulong sm_state;
    int i, offset;
    uint32_t val;

    sm_state = env->smbase + 0x8000;

    cpu_load_efer(env, x86_ldq_phys(cs, sm_state + 0x7ed0));

    env->gdt.base = x86_ldq_phys(cs, sm_state + 0x7e68);
    env->gdt.limit = x86_ldl_phys(cs, sm_state + 0x7e64);

    env->ldt.selector = x86_lduw_phys(cs, sm_state + 0x7e70);
    env->ldt.base = x86_ldq_phys(cs, sm_state + 0x7e78);
    env->ldt.limit = x86_ldl_phys(cs, sm_state + 0x7e74);
    env->ldt.flags = (x86_lduw_phys(cs, sm_state + 0x7e72) & 0xf0ff) << 8;

    env->idt.base = x86_ldq_phys(cs, sm_state + 0x7e88);
    env->idt.limit = x86_ldl_phys(cs, sm_state + 0x7e84);

    env->tr.selector = x86_lduw_phys(cs, sm_state + 0x7e90);
    env->tr.base = x86_ldq_phys(cs, sm_state + 0x7e98);
    env->tr.limit = x86_ldl_phys(cs, sm_state + 0x7e94);
    env->tr.flags = (x86_lduw_phys(cs, sm_state + 0x7e92) & 0xf0ff) << 8;

    env->regs[R_EAX] = x86_ldq_phys(cs, sm_state + 0x7ff8);
    env->regs[R_ECX] = x86_ldq_phys(cs, sm_state + 0x7ff0);
    env->regs[R_EDX] = x86_ldq_phys(cs, sm_state + 0x7fe8);
    env->regs[R_EBX] = x86_ldq_phys(cs, sm_state + 0x7fe0);
    env->regs[R_ESP] = x86_ldq_phys(cs, sm_state + 0x7fd8);
    env->regs[R_EBP] = x86_ldq_phys(cs, sm_state + 0x7fd0);
    env->regs[R_ESI] = x86_ldq_phys(cs, sm_state + 0x7fc8);
    env->regs[R_EDI] = x86_ldq_phys(cs, sm_state + 0x7fc0);
    for (i = 8; i < 16; i++) {
        env->regs[i] = x86_ldq_phys(cs, sm_state + 0x7ff8 - i * 8);
    }
    env->eip = x86_ldq_phys(cs, sm_state + 0x7f78);
    cpu_load_eflags(env, x86_ldl_phys(cs, sm_state + 0x7f70),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    helper_set_dr(env, 6, x86_ldl_phys(cs, sm_state + 0x7f68));
    helper_set_dr(env, 7, x86_ldl_phys(cs, sm_state + 0x7f60));

    cpu_x86_update_cr4(env, x86_ldl_phys(cs, sm_state + 0x7f48));
    cpu_x86_update_cr3(env, x86_ldq_phys(cs, sm_state + 0x7f50));
    cpu_x86_update_cr0(env, x86_ldl_phys(cs, sm_state + 0x7f58));

    for (i = 0; i < 6; i++) {
        offset = 0x7e00 + i * 16;
        cpu_x86_load_seg_cache(env, i,
                               x86_lduw_phys(cs, sm_state + offset),
                               x86_ldq_phys(cs, sm_state + offset + 8),
                               x86_ldl_phys(cs, sm_state + offset + 4),
                               (x86_lduw_phys(cs, sm_state + offset + 2) &
                                0xf0ff) << 8);
    }

    val = x86_ldl_phys(cs, sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = x86_ldl_phys(cs, sm_state + 0x7f00);
    }
#else
    g_assert_not_reached();
#endif
}

static void rsm_load_regs_32(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    target_ulong sm_state;
    int i, offset;
    uint32_t val;

    sm_state = env->smbase + 0x8000;

    cpu_x86_update_cr0(env, x86_ldl_phys(cs, sm_state + 0x7ffc));
    cpu_x86_update_cr3(env, x86_ldl_phys(cs, sm_state + 0x7ff8));
    cpu_load_eflags(env, x86_ldl_phys(cs, sm_state + 0x7ff4),
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->eip = x86_ldl_phys(cs, sm_state + 0x7ff0);
    env->regs[R_EDI] = x86_ldl_phys(cs, sm_state + 0x7fec);
    env->regs[R_ESI] = x86_ldl_phys(cs, sm_state + 0x7fe8);
    env->regs[R_EBP] = x86_ldl_phys(cs, sm_state + 0x7fe4);
    env->regs[R_ESP] = x86_ldl_phys(cs, sm_state + 0x7fe0);
    env->regs[R_EBX] = x86_ldl_phys(cs, sm_state + 0x7fdc);
    env->regs[R_EDX] = x86_ldl_phys(cs, sm_state + 0x7fd8);
    env->regs[R_ECX] = x86_ldl_phys(cs, sm_state + 0x7fd4);
    env->regs[R_EAX] = x86_ldl_phys(cs, sm_state + 0x7fd0);
    helper_set_dr(env, 6, x86_ldl_phys(cs, sm_state + 0x7fcc));
    helper_set_dr(env, 7, x86_ldl_phys(cs, sm_state + 0x7fc8));

    env->tr.selector = x86_ldl_phys(cs, sm_state + 0x7fc4) & 0xffff;
    env->tr.base = x86_ldl_phys(cs, sm_state + 0x7f64);
    env->tr.limit = x86_ldl_phys(cs, sm_state + 0x7f60);
    env->tr.flags = (x86_ldl_phys(cs, sm_state + 0x7f5c) & 0xf0ff) << 8;

    env->ldt.selector = x86_ldl_phys(cs, sm_state + 0x7fc0) & 0xffff;
    env->ldt.base = x86_ldl_phys(cs, sm_state + 0x7f80);
    env->ldt.limit = x86_ldl_phys(cs, sm_state + 0x7f7c);
    env->ldt.flags = (x86_ldl_phys(cs, sm_state + 0x7f78) & 0xf0ff) << 8;

    env->gdt.base = x86_ldl_phys(cs, sm_state + 0x7f74);
    env->gdt.limit = x86_ldl_phys(cs, sm_state + 0x7f70);

    env->idt.base = x86_ldl_phys(cs, sm_state + 0x7f58);
    env->idt.limit = x86_ldl_phys(cs, sm_state + 0x7f54);

    for (i = 0; i < 6; i++) {
        if (i < 3) {
            offset = 0x7f84 + i * 12;
        } else {
            offset = 0x7f2c + (i - 3) * 12;
        }
        cpu_x86_load_seg_cache(env, i,
                               x86_ldl_phys(cs,
                                        sm_state + 0x7fa8 + i * 4) & 0xffff,
                               x86_ldl_phys(cs, sm_state + offset + 8),
                               x86_ldl_phys(cs, sm_state + offset + 4),
                               (x86_ldl_phys(cs,
                                         sm_state + offset) & 0xf0ff) << 8);
    }
    cpu_x86_update_cr4(env, x86_ldl_phys(cs, sm_state + 0x7f14));

    val = x86_ldl_phys(cs, sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = x86_ldl_phys(cs, sm_state + 0x7ef8);
    }
}

void helper_rsm(CPUX86State *env)
{
    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        rsm_load_regs_64(env);
    } else {
        rsm_load_regs_32(env);
    }

    if ((env->hflags2 & HF2_SMM_INSIDE_NMI_MASK) == 0) {
        env->hflags2 &= ~HF2_NMI_MASK;
    }
    env->hflags2 &= ~HF2_SMM_INSIDE_NMI_MASK;
    env->hflags &= ~HF_SMM_MASK;

    // qemu_log_mask(CPU_LOG_INT, "SMM: after RSM\n");
    // log_cpu_state_mask(CPU_LOG_INT, CPU(cpu), CPU_DUMP_CCOP);
}

#if __Use_Original_Qemu != 1 /* ours (U1461) */
/*
 * The I/O instruction an IN / OUT hook signalled the SMI in (helper_in* / helper_out* set
 * env->smm_io_size / _in / _port / _ra around the hooks): EIP is put back to the instruction
 * (cpu_restore_state, as the hooks do), its prefixes and opcode are read and the Intel I/O
 * information (Tables 34-5 / 34-6) and the AMD I/O Instruction Restart Dword (Figure 10-6) are
 * built from them. False if the bytes are not an I/O instruction of that size and direction.
 * (U1461)
 */
static bool smm_io_decode(CPUX86State *env, X86SmmIo *io)
{
    CPUState *cs = env_cpu(env);
    bool code64 = (env->hflags & HF_CS64_MASK) != 0;
    int asize = code64 ? 64 : (env->hflags & HF_CS32_MASK) ? 32 : 16;
    int size = env->smm_io_size, in = 0, str = 0, rep = 0, type = 0, seg = R_DS, i, op = -1;
    bool aov = false;
    uint8_t b;
    target_ulong pc, base, addr;
    uint64_t amask;

    if (env->smm_io_ra) {
        cpu_restore_state(cs, env->smm_io_ra, false);
    }
    pc = env->segs[R_CS].base + env->eip;
    if (!code64) {
        pc = (uint32_t)pc;
    }
    for (i = 0; i < 15 && op < 0; i++) {
        if (cpu_memory_rw_debug(cs, code64 ? pc + i : (uint32_t)(pc + i), &b, 1, 0) < 0) {
            return false;
        }
        switch (b) {
        case 0x66: case 0xf0:
            break;
        case 0x67:
            aov = true;
            break;
        case 0xf2: case 0xf3:
            rep = 1;
            break;
        case 0x26: seg = R_ES; break;
        case 0x2e: seg = R_CS; break;
        case 0x36: seg = R_SS; break;
        case 0x3e: seg = R_DS; break;
        case 0x64: seg = R_FS; break;
        case 0x65: seg = R_GS; break;
        default:
            if (!(code64 && (b & 0xf0) == 0x40)) {       /* REX */
                op = b;
            }
            break;
        }
    }
    if (aov) {
        asize = code64 ? 32 : asize == 32 ? 16 : 32;
    }
    switch (op & 0xfe) {
    case 0x6c: in = 1; str = 1; type = rep ? 7 : 3; break;      /* (REP) INS */
    case 0x6e: in = 0; str = 1; type = rep ? 6 : 2; break;      /* (REP) OUTS */
    case 0xe4: in = 1; type = 9; break;                         /* IN imm8 */
    case 0xe6: in = 0; type = 8; break;                         /* OUT imm8 */
    case 0xec: in = 1; type = 1; break;                         /* IN DX */
    case 0xee: in = 0; type = 0; break;                         /* OUT DX */
    default:
        return false;
    }
    if (!str) {
        rep = 0;
    }
    if (in != env->smm_io_in || (size != 1 && size != 2 && size != 4)) {
        return false;
    }
    memset(io, 0, sizeof(*io));
    io->rip = env->eip;
    io->rcx = env->regs[R_ECX];
    io->rsi = env->regs[R_ESI];
    io->rdi = env->regs[R_EDI];
    io->str = str;
    if (str) {
        amask = asize == 64 ? ~0ULL : asize == 32 ? 0xffffffffULL : 0xffffULL;
        if (in) {
            seg = R_ES;     /* INS: ES:(E/R)DI, no override */
        }
        base = (code64 && seg != R_FS && seg != R_GS) ? 0 : env->segs[seg].base;
        addr = base + ((in ? env->regs[R_EDI] : env->regs[R_ESI]) & amask);
        io->mem = code64 ? addr : (uint32_t)addr;
    }
    /* Table 34-5: 31:16 port, 7:4 type (Table 34-6), 3:1 length (001b/010b/100b), 0 IO_SMI */
    io->intel_misc = ((uint32_t)env->smm_io_port << 16) | (type << 4) | (size << 1) | 1;
    /*
     * Figure 10-6: 31:16 PORT, 11 TF, 9 A64, 8 A32, 7 A16, 6 SZ32, 5 SZ16, 4 SZ8, 3 REP, 2 STR,
     * 1 VAL, 0 TYPE (1 = IN). B3-B0 (15:12, the DR6 status) stay 0 (I/O breakpoints are not
     * reported here); bit 10, also labelled PORT, has no definition in the text and stays 0.
     */
    io->amd_dword = ((uint32_t)env->smm_io_port << 16) |
                    ((env->eflags & TF_MASK) ? 1u << 11 : 0) |
                    (asize == 64 ? 1u << 9 : asize == 32 ? 1u << 8 : 1u << 7) |
                    (size == 4 ? 1u << 6 : size == 2 ? 1u << 5 : 1u << 4) |
                    (rep << 3) | (str << 2) | (1u << 1) | in;
    return true;
}

/*
 * SMI# (UC_CTL_X86_SMI): latch one SMI (34.3.1 / APM 10.3.3: one SMI is latched in SMM and
 * serviced on RSM). Recorded with it: the HALT state (auto HALT restart, 34.10 / APM 10.3.7) and,
 * from an IN / OUT hook outside SMM, the I/O instruction (34.7 IO_SMI: "taken immediately after a
 * successful I/O instruction or ... after a successful iteration of a REP I/O instruction").
 * The translator ends the TB after every I/O instruction, so the SMI is taken at the very next
 * instruction boundary. (U1461)
 */
void x86_smm_signal(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    if (cs->interrupt_request & CPU_INTERRUPT_SMI) {
        return;
    }
    env->smm_pend_hlt = env->smm_hlt;
    env->smm_pend_io = 0;
    if (env->smm_io_size && !(env->hflags & HF_SMM_MASK) && env->uc->nested_level > 0 &&
        smm_io_decode(env, &env->smm_io_pend)) {
        env->smm_pend_io = 1;
    }
    cs->interrupt_request |= CPU_INTERRUPT_SMI;
    if (!(env->hflags & HF_SMM_MASK)) {
        /*
         * leave the TB chain at the next TB start, as cpu_exit does; in SMM the SMI waits for RSM,
         * whose end of block returns to the main loop (a kick there would only make a code hook
         * run again for the instruction it was called for)
         */
        *(volatile int16_t *)&cs->icount_decr_ptr->u16.high = -1;
    }
}
#endif /* __Use_Original_Qemu (U1461) */
