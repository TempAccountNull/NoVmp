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

#if __Use_Original_Qemu == 1 /* original QEMU (U1462) */
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

    env->msr_smi_count++;
    env->hflags |= HF_SMM_MASK;
    if (env->hflags2 & HF2_NMI_MASK) {
        env->hflags2 |= HF2_SMM_INSIDE_NMI_MASK;
    } else {
        env->hflags2 |= HF2_NMI_MASK;
    }

    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        sm_state_init_64(cpu);
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
#else /* ours (U1462) */
/*
 * NoVmp (ledger U1462 Intel maps, U1463 AMD map; the SMI signal U1461): SMI delivery and RSM with
 * the state-save map of the CPU's vendor (docs/smm.md).
 *  - AuthenticAMD: AMD64 APM Vol2 24593 r3.45 10.2.3 Table 10-1 ("Implementations of the AMD64
 *    architecture do not use" the legacy format of Table 10-2), SMM-revision identifier
 *    0003_0064h (10.2.4: 0_xx64h with xx = 00, bits 16 and 17 "always set to 1").
 *  - every other vendor (GenuineIntel): SDM 093 Vol3C Table 34-3 when IA-32e mode is supported
 *    (CPUID.80000001H:EDX[29], 34.4.1.1) or the signature is one of Table 34-2, else Table 34-1
 *    (Vol2B RSM Operation: "IF (IA-32e mode supported) ... IA-32e SMM STATE MAP"). The SMM
 *    revision identifier is the platform profile's (U1465, below): I/O instruction restart (bit
 *    16), SMBASE relocation (bit 17, 34.9) and the I/O state information of 34.7 ("revision ID of
 *    30004H or higher") always hold.
 * The vendor is the one CPUID leaf 0 returns (a UC_CTL_X86_CPUID profile's, else the CPU
 * model's), never the build. What a map has no field for (Intel: the hidden part of the segment
 * registers, 34.4.1 "saved (but not readable) and restored"; the GDTR / IDTR limits; in Table
 * 34-1 also CR4, LDTR, GDTR and IDTR) is kept in the CPU (env->smm_*). Reserved bytes of a map
 * are never written.
 */
#define SMM_MAP_INTEL64 1
#define SMM_MAP_INTEL32 2
#define SMM_MAP_AMD64 3

/*
 * SMM revision identifiers (U1465, docs/smm.md "Revision identifier: evidence"):
 *  - Intel 64 map default 0003_0101h (platform profile raptor_lake_s, the default; also used for
 *    the --host profile): inferred from indirect evidence - the value coreboot 26.06 pairs with the
 *    Table 34-3 layout it uses on Alder Lake / Raptor Lake (soc/intel/common/block/smm/smihandler.c
 *    em64t101 ops, cpu/smmrelocate.c); never read from the i5-13600K;
 *  - the --guest profile (VMware guest) writes 0003_0004h through UC_CTL_X86_SMM_REVISION: the
 *    documented minimum (34.7), the default approximation (no VMware-specific source);
 *  - Intel Table 34-1 map 0003_0004h: the smallest value with the I/O state fields (34.7), a
 *    compatibility approximation (no processor-specific source); not a profile datum;
 *  - UC_CTL_X86_SMM_REVISION replaces the Table 34-3 value only (a platform profile's datum);
 *  - AMD 0003_0064h (APM Figure 10-3: 0_xx64h, xx = 00), not configurable.
 */
#define SMM_REVID_INTEL64 0x00030101u
#define SMM_REVID_INTEL32 0x00030004u
#define SMM_REVID_AMD 0x00030064u

/* hflags that belong to the hidden CS / SS state (the Unicorn API sets CS64 without CS.L) */
#define SMM_HFLAGS_SEG (HF_CPL_MASK | HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK | HF_ADDSEG_MASK)

extern void x86_store_dr(CPUX86State *env, int reg, target_ulong t0);

static bool smm_vendor_amd(CPUX86State *env)
{
    uint32_t a, b, c, d;

    cpu_x86_cpuid(env, 0, 0, &a, &b, &c, &d);
    return b == CPUID_VENDOR_AMD_1 && d == CPUID_VENDOR_AMD_2 && c == CPUID_VENDOR_AMD_3;
}

static int smm_map_of(CPUX86State *env)
{
    uint32_t v = env->cpuid_version;
    uint32_t fam = (v >> 8) & 0xf, model = (v >> 4) & 0xf;

    if (smm_vendor_amd(env)) {
        return SMM_MAP_AMD64;
    }
    if (fam == 6 || fam == 0xf) {
        model |= (v >> 12) & 0xf0;
    }
    if ((env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) ||
        (fam == 6 && (model == 0x17 || model == 0x0f || model == 0x1c))) {
        return SMM_MAP_INTEL64;
    }
    return SMM_MAP_INTEL32;
}

/* CET shadow stacks supported (SDM 34.5.3 "On processors that support CET shadow stacks") */
static bool smm_cet_ss(CPUX86State *env)
{
    return (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK) &&
           (x86_cpuid_profile_mask(env, 7, 0, 2) & CPUID_7_0_ECX_CET_SHSTK);
}

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

/* SDM 093 Vol3C Table 34-3 (Intel 64): offsets relative to SMBASE + 8000H */
static void smm_save_intel64(CPUX86State *env, target_ulong sm, bool halted, bool sgx_enc)
{
    CPUState *cs = env_cpu(env);
    const X86SmmIo *io = env->smm_io_valid ? &env->smm_io : NULL;
    int i;

    x86_stq_phys(cs, sm + 0x7ff8, env->cr[0]);
    x86_stq_phys(cs, sm + 0x7ff0, env->cr[3]);
    x86_stq_phys(cs, sm + 0x7fe8, cpu_compute_eflags(env));
    x86_stq_phys(cs, sm + 0x7fe0, env->efer);
    x86_stq_phys(cs, sm + 0x7fd8, env->eip);
    x86_stq_phys(cs, sm + 0x7fd0, env->dr[6]);
    x86_stq_phys(cs, sm + 0x7fc8, env->dr[7]);
    /* selectors, dwords (note 1: the two most significant bytes are reserved) */
    x86_stl_phys(cs, sm + 0x7fc4, env->tr.selector & 0xffff);
    x86_stl_phys(cs, sm + 0x7fc0, env->ldt.selector & 0xffff);
    for (i = 0; i < 6; i++) {               /* ES 7FA8H, CS, SS, DS, FS, GS 7FBCH */
        x86_stl_phys(cs, sm + 0x7fa8 + i * 4, env->segs[i].selector & 0xffff);
    }
    x86_stl_phys(cs, sm + 0x7fa4, io ? io->intel_misc : 0);         /* IO_MISC */
    x86_stq_phys(cs, sm + 0x7f9c, io ? io->mem : 0);                /* IO_MEM_ADDR */
    x86_stq_phys(cs, sm + 0x7f94, env->regs[R_EDI]);
    x86_stq_phys(cs, sm + 0x7f8c, env->regs[R_ESI]);
    x86_stq_phys(cs, sm + 0x7f84, env->regs[R_EBP]);
    x86_stq_phys(cs, sm + 0x7f7c, env->regs[R_ESP]);
    x86_stq_phys(cs, sm + 0x7f74, env->regs[R_EBX]);
    x86_stq_phys(cs, sm + 0x7f6c, env->regs[R_EDX]);
    x86_stq_phys(cs, sm + 0x7f64, env->regs[R_ECX]);
    x86_stq_phys(cs, sm + 0x7f5c, env->regs[R_EAX]);
    for (i = 8; i < 16; i++) {              /* R8 7F54H ... R15 7F1CH */
        x86_stq_phys(cs, sm + 0x7f54 - (i - 8) * 8, env->regs[i]);
    }
    x86_stw_phys(cs, sm + 0x7f02, halted ? 1 : 0);                  /* auto HALT restart */
    x86_stw_phys(cs, sm + 0x7f00, 0);                               /* I/O instruction restart */
    x86_stl_phys(cs, sm + 0x7efc, env->smm_revid ? env->smm_revid : SMM_REVID_INTEL64);
    x86_stl_phys(cs, sm + 0x7ef8, env->smbase);
    /*
     * 7EE0H: bit 0 EPT (not in VMX non-root operation: 0, so 7ED8H EPTP is not saved, 34.14.1),
     * bit 1 incident to enclave mode (Vol3D Table 42-1, U1263)
     */
    x86_stl_phys(cs, sm + 0x7ee0, sgx_enc ? 2 : 0);
    if (smm_cet_ss(env)) {
        x86_stq_phys(cs, sm + 0x7ec8, env->ssp);                    /* 34.5.3 */
    }
    x86_stl_phys(cs, sm + 0x7e9c, (uint32_t)env->ldt.base);
    x86_stl_phys(cs, sm + 0x7e94, (uint32_t)env->idt.base);
    x86_stl_phys(cs, sm + 0x7e8c, (uint32_t)env->gdt.base);
    x86_stq_phys(cs, sm + 0x7e40, env->cr[4]);                      /* all 64 bits (FRED 12) */
    x86_stq_phys(cs, sm + 0x7de8, io ? io->rip : 0);                /* IO_RIP */
    x86_stl_phys(cs, sm + 0x7dd8, (uint32_t)((uint64_t)env->idt.base >> 32));
    x86_stl_phys(cs, sm + 0x7dd4, (uint32_t)((uint64_t)env->ldt.base >> 32));
    x86_stl_phys(cs, sm + 0x7dd0, (uint32_t)((uint64_t)env->gdt.base >> 32));
}

/* SDM 093 Vol3C Table 34-1 (no IA-32e mode): offsets relative to SMBASE + 8000H */
static void smm_save_intel32(CPUX86State *env, target_ulong sm, bool halted)
{
    CPUState *cs = env_cpu(env);
    const X86SmmIo *io = env->smm_io_valid ? &env->smm_io : NULL;
    int i;

    x86_stl_phys(cs, sm + 0x7ffc, env->cr[0]);
    x86_stl_phys(cs, sm + 0x7ff8, env->cr[3]);
    x86_stl_phys(cs, sm + 0x7ff4, cpu_compute_eflags(env));
    x86_stl_phys(cs, sm + 0x7ff0, env->eip);
    x86_stl_phys(cs, sm + 0x7fec, env->regs[R_EDI]);
    x86_stl_phys(cs, sm + 0x7fe8, env->regs[R_ESI]);
    x86_stl_phys(cs, sm + 0x7fe4, env->regs[R_EBP]);
    x86_stl_phys(cs, sm + 0x7fe0, env->regs[R_ESP]);
    x86_stl_phys(cs, sm + 0x7fdc, env->regs[R_EBX]);
    x86_stl_phys(cs, sm + 0x7fd8, env->regs[R_EDX]);
    x86_stl_phys(cs, sm + 0x7fd4, env->regs[R_ECX]);
    x86_stl_phys(cs, sm + 0x7fd0, env->regs[R_EAX]);
    x86_stl_phys(cs, sm + 0x7fcc, env->dr[6]);
    x86_stl_phys(cs, sm + 0x7fc8, env->dr[7]);
    x86_stl_phys(cs, sm + 0x7fc4, env->tr.selector & 0xffff);      /* 7FC0H is reserved */
    for (i = 0; i < 6; i++) {               /* ES 7FA8H ... GS 7FBCH */
        x86_stl_phys(cs, sm + 0x7fa8 + i * 4, env->segs[i].selector & 0xffff);
    }
    x86_stl_phys(cs, sm + 0x7fa4, io ? io->intel_misc : 0);         /* I/O state field */
    x86_stl_phys(cs, sm + 0x7fa0, io ? (uint32_t)io->mem : 0);      /* I/O memory address */
    x86_stw_phys(cs, sm + 0x7f02, halted ? 1 : 0);
    x86_stw_phys(cs, sm + 0x7f00, 0);
    x86_stl_phys(cs, sm + 0x7efc, SMM_REVID_INTEL32);              /* not a profile datum */
    x86_stl_phys(cs, sm + 0x7ef8, env->smbase);
}

/* AMD64 APM Vol2 r3.45 Table 10-1: offsets relative to SMBASE + 8000H (FE00H = 7E00H) (U1463) */
static void smm_save_amd64(CPUX86State *env, target_ulong sm, bool halted)
{
    CPUState *cs = env_cpu(env);
    const X86SmmIo *io = env->smm_io_valid ? &env->smm_io : NULL;
    SegmentCache *dt;
    int i;

    for (i = 0; i < 6; i++) {               /* ES FE00H, CS, SS, DS, FS, GS FE50H */
        dt = &env->segs[i];
        x86_stw_phys(cs, sm + 0x7e00 + i * 16, dt->selector);
        x86_stw_phys(cs, sm + 0x7e02 + i * 16, (dt->flags >> 8) & 0xf0ff);
        x86_stl_phys(cs, sm + 0x7e04 + i * 16, dt->limit);
        x86_stq_phys(cs, sm + 0x7e08 + i * 16, dt->base);
    }
    x86_stw_phys(cs, sm + 0x7e64, env->gdt.limit);                  /* GDTR limit: a word */
    x86_stq_phys(cs, sm + 0x7e68, env->gdt.base);
    x86_stw_phys(cs, sm + 0x7e70, env->ldt.selector);
    x86_stw_phys(cs, sm + 0x7e72, (env->ldt.flags >> 8) & 0xf0ff);
    x86_stl_phys(cs, sm + 0x7e74, env->ldt.limit);
    x86_stq_phys(cs, sm + 0x7e78, env->ldt.base);
    x86_stw_phys(cs, sm + 0x7e84, env->idt.limit);                  /* IDTR limit: a word */
    x86_stq_phys(cs, sm + 0x7e88, env->idt.base);
    x86_stw_phys(cs, sm + 0x7e90, env->tr.selector);
    x86_stw_phys(cs, sm + 0x7e92, (env->tr.flags >> 8) & 0xf0ff);
    x86_stl_phys(cs, sm + 0x7e94, env->tr.limit);
    x86_stq_phys(cs, sm + 0x7e98, env->tr.base);
    /* 10.3.8: I/O Instruction Restart RIP / RCX / RSI / RDI / Dword (VAL = 0: no I/O SMI) */
    x86_stq_phys(cs, sm + 0x7ea0, io ? io->rip : 0);
    x86_stq_phys(cs, sm + 0x7ea8, io ? io->rcx : 0);
    x86_stq_phys(cs, sm + 0x7eb0, io ? io->rsi : 0);
    x86_stq_phys(cs, sm + 0x7eb8, io ? io->rdi : 0);
    x86_stl_phys(cs, sm + 0x7ec0, io ? io->amd_dword : 0);
    x86_stb_phys(cs, sm + 0x7ec8, 0);                               /* I/O instruction restart */
    x86_stb_phys(cs, sm + 0x7ec9, halted ? 1 : 0);                  /* auto-halt restart */
    x86_stq_phys(cs, sm + 0x7ed0, env->efer);
    if (!(env->hflags & HF_GUEST_MASK)) {
        /* SVM Guest, Guest VMCB Physical Address, Guest Virtual Interrupt: not in a guest */
        x86_stq_phys(cs, sm + 0x7ed8, 0);
        x86_stq_phys(cs, sm + 0x7ee0, 0);
        x86_stq_phys(cs, sm + 0x7ee8, 0);
    }
    x86_stl_phys(cs, sm + 0x7efc, SMM_REVID_AMD);
    x86_stl_phys(cs, sm + 0x7f00, env->smbase);
    if (smm_cet_ss(env)) {
        x86_stq_phys(cs, sm + 0x7f18, env->ssp);
    }
    x86_stq_phys(cs, sm + 0x7f48, env->cr[4]);
    x86_stq_phys(cs, sm + 0x7f50, env->cr[3]);
    x86_stq_phys(cs, sm + 0x7f58, env->cr[0]);
    x86_stq_phys(cs, sm + 0x7f60, env->dr[7]);
    x86_stq_phys(cs, sm + 0x7f68, env->dr[6]);
    x86_stq_phys(cs, sm + 0x7f70, cpu_compute_eflags(env));
    x86_stq_phys(cs, sm + 0x7f78, env->eip);
    for (i = 8; i < 16; i++) {              /* R15 FF80H ... R8 FFB8H */
        x86_stq_phys(cs, sm + 0x7ff8 - i * 8, env->regs[i]);
    }
    x86_stq_phys(cs, sm + 0x7fc0, env->regs[R_EDI]);
    x86_stq_phys(cs, sm + 0x7fc8, env->regs[R_ESI]);
    x86_stq_phys(cs, sm + 0x7fd0, env->regs[R_EBP]);
    x86_stq_phys(cs, sm + 0x7fd8, env->regs[R_ESP]);
    x86_stq_phys(cs, sm + 0x7fe0, env->regs[R_EBX]);
    x86_stq_phys(cs, sm + 0x7fe8, env->regs[R_EDX]);
    x86_stq_phys(cs, sm + 0x7ff0, env->regs[R_ECX]);
    x86_stq_phys(cs, sm + 0x7ff8, env->regs[R_EAX]);
}

/*
 * UC_CTL_X86_SMM_REVISION (U1465): a value must state what the model implements - bit 16 (I/O
 * instruction restart) and bit 17 (SMBASE relocation) set, the reserved bits 31:18 clear (Figure
 * 34-2) and a revision with the I/O state information (34.7: 30004H or higher). 0 = the default.
 */
bool x86_smm_revision_valid(uint32_t rev)
{
    return rev == 0 || ((rev & 0xfffc0000u) == 0 && (rev & 0x00030000u) == 0x00030000u &&
                        rev >= 0x00030004u);
}

/* the SMM revision identifier the next SMI writes (U1465) */
uint32_t x86_smm_revision(CPUX86State *env)
{
    switch (smm_map_of(env)) {
    case SMM_MAP_AMD64:
        return SMM_REVID_AMD;
    case SMM_MAP_INTEL64:
        return env->smm_revid ? env->smm_revid : SMM_REVID_INTEL64;
    default:
        return SMM_REVID_INTEL32;
    }
}

void do_smm_enter(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    target_ulong sm;
    bool halted = env->smm_pend_hlt != 0;
    /* an SMI in enclave mode is an enclave-exiting event (SDM 092 Vol3D 40.4, 42.8.2, sgx_model.c) */
    bool sgx_enc = env->sgx_enclave_mode != 0;
    int i, map;

    if (sgx_enc) {
        x86_sgx_aex(env, -1, 0, 1, 0, 0);
        env->sgx_enc_event = 0;
    }
    env->msr_smi_count++;
    env->hflags |= HF_SMM_MASK;
    if (env->hflags2 & HF2_NMI_MASK) {
        env->hflags2 |= HF2_SMM_INSIDE_NMI_MASK;
    } else {
        env->hflags2 |= HF2_NMI_MASK;
    }

    map = smm_map_of(env);
    env->smm_map = map;
    env->smm_io_valid = env->smm_pend_io;
    if (env->smm_io_valid) {
        env->smm_io = env->smm_io_pend;
    } else {
        memset(&env->smm_io, 0, sizeof(env->smm_io));
    }
    env->smm_pend_hlt = 0;
    env->smm_pend_io = 0;
    env->smm_hlt = 0;
    /* the state no field of the map holds (34.4.1: saved, not readable, restored by RSM) */
    for (i = 0; i < 6; i++) {
        env->smm_seg[i] = env->segs[i];
    }
    env->smm_ldt = env->ldt;
    env->smm_tr = env->tr;
    env->smm_gdt = env->gdt;
    env->smm_idt = env->idt;
    env->smm_cr4 = env->cr[4];
    env->smm_hflags = env->hflags & SMM_HFLAGS_SEG;

    sm = env->smbase + 0x8000;
    switch (map) {
    case SMM_MAP_AMD64:
        smm_save_amd64(env, sm, halted);
        break;
    case SMM_MAP_INTEL64:
        smm_save_intel64(env, sm, halted, sgx_enc);
        break;
    default:
        smm_save_intel32(env, sm, halted);
        break;
    }

    /* SDM Table 34-4 / APM Table 10-3 */
    if (map != SMM_MAP_INTEL32) {
        cpu_load_efer(env, 0);
    }
    cpu_load_eflags(env, 0, ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C |
                              DF_MASK));
    env->eip = 0x00008000;
    cpu_x86_update_cr0(env,
                       env->cr[0] & ~(CR0_PE_MASK | CR0_EM_MASK | CR0_TS_MASK |
                                      CR0_PG_MASK));
    cpu_x86_update_cr4(env, 0);             /* CR4.CET = 0 (34.5.3) */
    x86_store_dr(env, 7, 0x00000400);       /* removes the breakpoints too (upstream cdba90ac1b) */

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

/*
 * The hidden CS / SS state of the SMI comes back with the segments; Unicorn's API sets the
 * IA-32e mode bits (HF_CS64 etc.) without a CS.L descriptor bit, so the hflags of that state
 * are restored too while CS and SS carry the attributes they had at the SMI.
 */
static void smm_restore_seg_hflags(CPUX86State *env)
{
    if (env->segs[R_CS].flags == env->smm_seg[R_CS].flags &&
        env->segs[R_SS].flags == env->smm_seg[R_SS].flags &&
        ((env->hflags ^ env->smm_hflags) & SMM_HFLAGS_SEG)) {
        env->hflags = (env->hflags & ~SMM_HFLAGS_SEG) | env->smm_hflags;
        cpu_sync_cet_hflags(env);
    }
}

static void smm_load_regs(CPUX86State *env, uint64_t rflags, uint64_t rip, uint64_t dr6,
                          uint64_t dr7)
{
    env->eip = rip;
    cpu_load_eflags(env, (uint32_t)rflags,
                    ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    x86_store_dr(env, 6, (uint32_t)dr6);
    x86_store_dr(env, 7, (uint32_t)dr7);
}

static void smm_load_intel64(CPUX86State *env, target_ulong sm, bool *halt)
{
    CPUState *cs = env_cpu(env);
    uint64_t v;
    int i;

    cpu_load_efer(env, x86_ldq_phys(cs, sm + 0x7fe0));
    env->gdt = env->smm_gdt;
    env->gdt.base = x86_ldl_phys(cs, sm + 0x7e8c) | ((uint64_t)x86_ldl_phys(cs, sm + 0x7dd0) << 32);
    env->idt = env->smm_idt;
    env->idt.base = x86_ldl_phys(cs, sm + 0x7e94) | ((uint64_t)x86_ldl_phys(cs, sm + 0x7dd8) << 32);
    env->ldt = env->smm_ldt;
    env->ldt.selector = x86_lduw_phys(cs, sm + 0x7fc0);
    env->ldt.base = x86_ldl_phys(cs, sm + 0x7e9c) | ((uint64_t)x86_ldl_phys(cs, sm + 0x7dd4) << 32);
    env->tr = env->smm_tr;
    env->tr.selector = x86_lduw_phys(cs, sm + 0x7fc4);

    env->regs[R_EDI] = x86_ldq_phys(cs, sm + 0x7f94);
    env->regs[R_ESI] = x86_ldq_phys(cs, sm + 0x7f8c);
    env->regs[R_EBP] = x86_ldq_phys(cs, sm + 0x7f84);
    env->regs[R_ESP] = x86_ldq_phys(cs, sm + 0x7f7c);
    env->regs[R_EBX] = x86_ldq_phys(cs, sm + 0x7f74);
    env->regs[R_EDX] = x86_ldq_phys(cs, sm + 0x7f6c);
    env->regs[R_ECX] = x86_ldq_phys(cs, sm + 0x7f64);
    env->regs[R_EAX] = x86_ldq_phys(cs, sm + 0x7f5c);
    for (i = 8; i < 16; i++) {
        env->regs[i] = x86_ldq_phys(cs, sm + 0x7f54 - (i - 8) * 8);
    }
    smm_load_regs(env, x86_ldq_phys(cs, sm + 0x7fe8), x86_ldq_phys(cs, sm + 0x7fd8),
                  x86_ldq_phys(cs, sm + 0x7fd0), x86_ldq_phys(cs, sm + 0x7fc8));

    cpu_x86_update_cr4(env, x86_ldq_phys(cs, sm + 0x7e40));
    cpu_x86_update_cr3(env, x86_ldq_phys(cs, sm + 0x7ff0));
    cpu_x86_update_cr0(env, x86_ldq_phys(cs, sm + 0x7ff8));

    for (i = 0; i < 6; i++) {
        cpu_x86_load_seg_cache(env, i, x86_lduw_phys(cs, sm + 0x7fa8 + i * 4),
                               env->smm_seg[i].base, env->smm_seg[i].limit,
                               env->smm_seg[i].flags);
    }
    smm_restore_seg_hflags(env);
    if (smm_cet_ss(env)) {
        /* 34.3.2: "made canonical by sign-extension before loading it into SSP" */
        int top = (env->cr[4] & CR4_LA57_MASK) ? 56 : 47;

        v = x86_ldq_phys(cs, sm + 0x7ec8);
        env->ssp = (uint64_t)((int64_t)(v << (63 - top)) >> (63 - top));
    }
    *halt = (x86_lduw_phys(cs, sm + 0x7f02) & 1) != 0;
    if (x86_lduw_phys(cs, sm + 0x7f00) == 0xff) {
        /* 34.12: EIP to the I/O instruction (IO_RIP, writable), a string one re-executes its iteration */
        env->eip = x86_ldq_phys(cs, sm + 0x7de8);
        if (env->smm_io_valid && env->smm_io.str) {
            env->regs[R_ECX] = env->smm_io.rcx;
            env->regs[R_ESI] = env->smm_io.rsi;
            env->regs[R_EDI] = env->smm_io.rdi;
        }
    }
    env->smbase = x86_ldl_phys(cs, sm + 0x7ef8);    /* 34.11 */
}

static void smm_load_intel32(CPUX86State *env, target_ulong sm, bool *halt)
{
    CPUState *cs = env_cpu(env);
    int i;

    env->gdt = env->smm_gdt;
    env->idt = env->smm_idt;
    env->ldt = env->smm_ldt;
    env->tr = env->smm_tr;
    env->tr.selector = x86_lduw_phys(cs, sm + 0x7fc4);

    env->regs[R_EDI] = x86_ldl_phys(cs, sm + 0x7fec);
    env->regs[R_ESI] = x86_ldl_phys(cs, sm + 0x7fe8);
    env->regs[R_EBP] = x86_ldl_phys(cs, sm + 0x7fe4);
    env->regs[R_ESP] = x86_ldl_phys(cs, sm + 0x7fe0);
    env->regs[R_EBX] = x86_ldl_phys(cs, sm + 0x7fdc);
    env->regs[R_EDX] = x86_ldl_phys(cs, sm + 0x7fd8);
    env->regs[R_ECX] = x86_ldl_phys(cs, sm + 0x7fd4);
    env->regs[R_EAX] = x86_ldl_phys(cs, sm + 0x7fd0);
    smm_load_regs(env, x86_ldl_phys(cs, sm + 0x7ff4), x86_ldl_phys(cs, sm + 0x7ff0),
                  x86_ldl_phys(cs, sm + 0x7fcc), x86_ldl_phys(cs, sm + 0x7fc8));

    cpu_x86_update_cr4(env, env->smm_cr4);
    cpu_x86_update_cr3(env, x86_ldl_phys(cs, sm + 0x7ff8));
    cpu_x86_update_cr0(env, x86_ldl_phys(cs, sm + 0x7ffc));

    for (i = 0; i < 6; i++) {
        cpu_x86_load_seg_cache(env, i, x86_lduw_phys(cs, sm + 0x7fa8 + i * 4),
                               env->smm_seg[i].base, env->smm_seg[i].limit,
                               env->smm_seg[i].flags);
    }
    smm_restore_seg_hflags(env);
    *halt = (x86_lduw_phys(cs, sm + 0x7f02) & 1) != 0;
    if (x86_lduw_phys(cs, sm + 0x7f00) == 0xff) {
        /* Table 34-1 has no IO_RIP: the I/O instruction recorded at the SMI */
        env->eip = env->smm_io.rip;
        if (env->smm_io_valid && env->smm_io.str) {
            env->regs[R_ECX] = (uint32_t)env->smm_io.rcx;
            env->regs[R_ESI] = (uint32_t)env->smm_io.rsi;
            env->regs[R_EDI] = (uint32_t)env->smm_io.rdi;
        }
    }
    env->smbase = x86_ldl_phys(cs, sm + 0x7ef8);
}

/* RSM with Table 10-1 (APM 10.4) (U1463) */
static void smm_load_amd64(CPUX86State *env, target_ulong sm, bool *halt)
{
    CPUState *cs = env_cpu(env);
    int i;

    cpu_load_efer(env, x86_ldq_phys(cs, sm + 0x7ed0));
    env->gdt.limit = x86_lduw_phys(cs, sm + 0x7e64);
    env->gdt.base = x86_ldq_phys(cs, sm + 0x7e68);
    env->ldt.selector = x86_lduw_phys(cs, sm + 0x7e70);
    env->ldt.flags = (x86_lduw_phys(cs, sm + 0x7e72) & 0xf0ff) << 8;
    env->ldt.limit = x86_ldl_phys(cs, sm + 0x7e74);
    env->ldt.base = x86_ldq_phys(cs, sm + 0x7e78);
    env->idt.limit = x86_lduw_phys(cs, sm + 0x7e84);
    env->idt.base = x86_ldq_phys(cs, sm + 0x7e88);
    env->tr.selector = x86_lduw_phys(cs, sm + 0x7e90);
    env->tr.flags = (x86_lduw_phys(cs, sm + 0x7e92) & 0xf0ff) << 8;
    env->tr.limit = x86_ldl_phys(cs, sm + 0x7e94);
    env->tr.base = x86_ldq_phys(cs, sm + 0x7e98);

    for (i = 8; i < 16; i++) {
        env->regs[i] = x86_ldq_phys(cs, sm + 0x7ff8 - i * 8);
    }
    env->regs[R_EDI] = x86_ldq_phys(cs, sm + 0x7fc0);
    env->regs[R_ESI] = x86_ldq_phys(cs, sm + 0x7fc8);
    env->regs[R_EBP] = x86_ldq_phys(cs, sm + 0x7fd0);
    env->regs[R_ESP] = x86_ldq_phys(cs, sm + 0x7fd8);
    env->regs[R_EBX] = x86_ldq_phys(cs, sm + 0x7fe0);
    env->regs[R_EDX] = x86_ldq_phys(cs, sm + 0x7fe8);
    env->regs[R_ECX] = x86_ldq_phys(cs, sm + 0x7ff0);
    env->regs[R_EAX] = x86_ldq_phys(cs, sm + 0x7ff8);
    smm_load_regs(env, x86_ldq_phys(cs, sm + 0x7f70), x86_ldq_phys(cs, sm + 0x7f78),
                  x86_ldq_phys(cs, sm + 0x7f68), x86_ldq_phys(cs, sm + 0x7f60));

    cpu_x86_update_cr4(env, x86_ldq_phys(cs, sm + 0x7f48));
    cpu_x86_update_cr3(env, x86_ldq_phys(cs, sm + 0x7f50));
    cpu_x86_update_cr0(env, x86_ldq_phys(cs, sm + 0x7f58));

    for (i = 0; i < 6; i++) {
        cpu_x86_load_seg_cache(env, i, x86_lduw_phys(cs, sm + 0x7e00 + i * 16),
                               x86_ldq_phys(cs, sm + 0x7e08 + i * 16),
                               x86_ldl_phys(cs, sm + 0x7e04 + i * 16),
                               (x86_lduw_phys(cs, sm + 0x7e02 + i * 16) & 0xf0ff) << 8);
    }
    smm_restore_seg_hflags(env);
    if (smm_cet_ss(env)) {
        env->ssp = x86_ldq_phys(cs, sm + 0x7f18);
    }
    *halt = x86_ldub_phys(cs, sm + 0x7ec9) != 0;
    if (x86_ldub_phys(cs, sm + 0x7ec8) != 0) {
        /* 10.3.8: "Set to any non-zero value - The I/O instruction is restarted" */
        env->eip = x86_ldq_phys(cs, sm + 0x7ea0);
        env->regs[R_ECX] = x86_ldq_phys(cs, sm + 0x7ea8);
        env->regs[R_ESI] = x86_ldq_phys(cs, sm + 0x7eb0);
        env->regs[R_EDI] = x86_ldq_phys(cs, sm + 0x7eb8);
    }
    env->smbase = x86_ldl_phys(cs, sm + 0x7f00);    /* 10.2.2 */
}

void helper_rsm(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    target_ulong sm = env->smbase + 0x8000;
    int map = env->smm_map ? env->smm_map : smm_map_of(env);
    bool halt = false;

    switch (map) {
    case SMM_MAP_AMD64:
        smm_load_amd64(env, sm, &halt);
        break;
    case SMM_MAP_INTEL64:
        smm_load_intel64(env, sm, &halt);
        break;
    default:
        smm_load_intel32(env, sm, &halt);
        break;
    }
    env->smm_map = 0;
    env->smm_io_valid = 0;

    if ((env->hflags2 & HF2_SMM_INSIDE_NMI_MASK) == 0) {
        env->hflags2 &= ~HF2_NMI_MASK;
    }
    env->hflags2 &= ~HF2_SMM_INSIDE_NMI_MASK;
    env->hflags &= ~HF_SMM_MASK;

    if (halt) {
        /* back to the HALT state (34.10, APM 10.3.7); an SMI latched in SMM is taken in it */
        if (cs->interrupt_request & CPU_INTERRUPT_SMI) {
            env->smm_pend_hlt = 1;
        } else {
            env->smm_hlt = 1;
            cs->halted = 1;
            cs->exception_index = EXCP_HLT;
            cpu_loop_exit(cs);
        }
    }
}
#endif /* __Use_Original_Qemu (U1462) */
