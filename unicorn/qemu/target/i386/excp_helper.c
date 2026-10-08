/*
 *  x86 exception helpers
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
#include "qemu/log.h"
#include "exec/helper-proto.h"
#include "sysemu/sysemu.h"

#include "uc_priv.h"

void helper_raise_interrupt(CPUX86State *env, int intno, int next_eip_addend)
{
    raise_interrupt(env, intno, 1, 0, next_eip_addend);
}

void helper_raise_exception(CPUX86State *env, int exception_index)
{
    raise_exception(env, exception_index);
}

/*
 * Check nested exceptions and change to double or triple fault if
 * needed. It should only be called, if this is not an interrupt.
 * Returns the new exception number.
 */
static int check_exception(CPUX86State *env, int intno, int *error_code,
                           uintptr_t retaddr)
{
#if __Use_Original_Qemu == 1 /* original QEMU (U114) */
    int first_contributory = env->old_exception == 0 ||
                              (env->old_exception >= 10 &&
                               env->old_exception <= 13);
    int second_contributory = intno == 0 ||
                               (intno >= 10 && intno <= 13);
#else /* ours (U114) */
    /* #CP (21) is a contributory exception too (SDM Vol3 Table 7-3) */
    int first_contributory = env->old_exception == 0 ||
                              (env->old_exception >= 10 &&
                               env->old_exception <= 13) ||
                              env->old_exception == EXCP15_CP;
    int second_contributory = intno == 0 ||
                               (intno >= 10 && intno <= 13) ||
                               intno == EXCP15_CP;
#endif /* __Use_Original_Qemu (U114) */

    qemu_log_mask(CPU_LOG_INT, "check_exception old: 0x%x new 0x%x\n",
                env->old_exception, intno);

    if (env->old_exception == EXCP08_DBLE) {
        if (env->hflags & HF_GUEST_MASK) {
            cpu_vmexit(env, SVM_EXIT_SHUTDOWN, 0, retaddr); /* does not return */
        }

        qemu_log_mask(CPU_LOG_RESET, "Triple fault\n");

        qemu_system_reset_request(env->uc);
        return EXCP_HLT;
    }

    if ((first_contributory && second_contributory)
        || (env->old_exception == EXCP0E_PAGE &&
            (second_contributory || (intno == EXCP0E_PAGE)))) {
        intno = EXCP08_DBLE;
        *error_code = 0;
    }

    if (second_contributory || (intno == EXCP0E_PAGE) ||
        (intno == EXCP08_DBLE)) {
        env->old_exception = intno;
    }

    return intno;
}

/*
 * Signal an interruption. It is executed in the main CPU loop.
 * is_int is TRUE if coming from the int instruction. next_eip is the
 * env->eip value AFTER the interrupt instruction. It is only relevant if
 * is_int is TRUE.
 */
static void QEMU_NORETURN raise_interrupt2(CPUX86State *env, int intno,
                                           int is_int, int error_code,
                                           int next_eip_addend,
                                           uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);

    if (!is_int) {
        cpu_svm_check_intercept_param(env, SVM_EXIT_EXCP_BASE + intno,
                                      error_code, retaddr);
        intno = check_exception(env, intno, &error_code, retaddr);
    } else {
        cpu_svm_check_intercept_param(env, SVM_EXIT_SWINT, 0, retaddr);
    }

    cs->exception_index = intno;
    env->error_code = error_code;
    env->exception_is_int = is_int;
    env->exception_next_eip = env->eip + next_eip_addend;
    cpu_loop_exit_restore(cs, retaddr);
}

/* shortcuts to generate exceptions */

void QEMU_NORETURN raise_interrupt(CPUX86State *env, int intno, int is_int,
                                   int error_code, int next_eip_addend)
{
    raise_interrupt2(env, intno, is_int, error_code, next_eip_addend, 0);
}

void raise_exception_err(CPUX86State *env, int exception_index,
                         int error_code)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, 0);
}

void raise_exception_err_ra(CPUX86State *env, int exception_index,
                            int error_code, uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, retaddr);
}

void raise_exception(CPUX86State *env, int exception_index)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, 0);
}

void raise_exception_ra(CPUX86State *env, int exception_index, uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, retaddr);
}

/*
 * INT1 aka ICEBP generates a trap-like #DB: it is delivered with RIP pointing after the
 * instruction (backport of QEMU 73fb7b3c49; the translator stores the next EIP first).
 */
G_NORETURN void helper_icebp(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    /* end the instruction as gen_eob() would: no IRQ shadow, RF cleared */
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    env->eflags &= ~RF_MASK;

    cs->exception_index = EXCP01_DB;
    env->error_code = 0;
    env->exception_is_int = 0;
    env->exception_next_eip = env->eip;
    cpu_loop_exit(cs);
}

G_NORETURN void handle_unaligned_access(CPUX86State *env, vaddr vaddr,
                                        MMUAccessType access_type,
                                        uintptr_t retaddr)
{
    /*
     * Unaligned accesses are currently only triggered by SSE/AVX
     * instructions that impose alignment requirements on memory
     * operands. These instructions raise #GP(0) upon accessing an
     * unaligned address.
     */
    raise_exception_ra(env, EXCP0D_GPF, retaddr);
}

static hwaddr get_hphys(CPUState *cs, hwaddr gphys, MMUAccessType access_type,
                        int *prot)
{
    CPUX86State *env = &X86_CPU(cs)->env;
    uint64_t rsvd_mask = PG_HI_RSVD_MASK;
    uint64_t ptep, pte;
    uint64_t exit_info_1 = 0;
    target_ulong pde_addr, pte_addr;
    uint32_t page_offset;
    int page_size;

    if (likely(!(env->hflags2 & HF2_NPT_MASK))) {
        return gphys;
    }

    if (!(env->nested_pg_mode & SVM_NPT_NXE)) {
        rsvd_mask |= PG_NX_MASK;
    }

    if (env->nested_pg_mode & SVM_NPT_PAE) {
        uint64_t pde, pdpe;
        target_ulong pdpe_addr;

#ifdef TARGET_X86_64
        if (env->nested_pg_mode & SVM_NPT_LMA) {
            uint64_t pml5e;
            uint64_t pml4e_addr, pml4e;

            pml5e = env->nested_cr3;
            ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;

            pml4e_addr = (pml5e & PG_ADDRESS_MASK) +
                    (((gphys >> 39) & 0x1ff) << 3);
            pml4e = x86_ldq_phys(cs, pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pml4e & (rsvd_mask | PG_PSE_MASK)) {
                goto do_fault_rsvd;
            }
            if (!(pml4e & PG_ACCESSED_MASK)) {
                pml4e |= PG_ACCESSED_MASK;
                x86_stl_phys_notdirty(cs, pml4e_addr, pml4e);
            }
            ptep &= pml4e ^ PG_NX_MASK;
            pdpe_addr = (pml4e & PG_ADDRESS_MASK) +
                    (((gphys >> 30) & 0x1ff) << 3);
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pdpe & rsvd_mask) {
                goto do_fault_rsvd;
            }
            ptep &= pdpe ^ PG_NX_MASK;
            if (!(pdpe & PG_ACCESSED_MASK)) {
                pdpe |= PG_ACCESSED_MASK;
                x86_stl_phys_notdirty(cs, pdpe_addr, pdpe);
            }
            if (pdpe & PG_PSE_MASK) {
                /* 1 GB page */
                page_size = 1024 * 1024 * 1024;
                pte_addr = pdpe_addr;
                pte = pdpe;
                goto do_check_protect;
            }
        } else
#endif
        {
            pdpe_addr = (env->nested_cr3 & ~0x1f) + ((gphys >> 27) & 0x18);
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            rsvd_mask |= PG_HI_USER_MASK;
            if (pdpe & (rsvd_mask | PG_NX_MASK)) {
                goto do_fault_rsvd;
            }
            ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
        }

        pde_addr = (pdpe & PG_ADDRESS_MASK) + (((gphys >> 21) & 0x1ff) << 3);
        pde = x86_ldq_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pde & rsvd_mask) {
            goto do_fault_rsvd;
        }
        ptep &= pde ^ PG_NX_MASK;
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte_addr = pde_addr;
            pte = pde;
            goto do_check_protect;
        }
        /* 4 KB page */
        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }
        pte_addr = (pde & PG_ADDRESS_MASK) + (((gphys >> 12) & 0x1ff) << 3);
        pte = x86_ldq_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pte & rsvd_mask) {
            goto do_fault_rsvd;
        }
        /* combine pde and pte nx, user and rw protections */
        ptep &= pte ^ PG_NX_MASK;
        page_size = 4096;
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = (env->nested_cr3 & ~0xfff) + ((gphys >> 20) & 0xffc);
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        ptep = pde | PG_NX_MASK;

        /* if PSE bit is set, then we use a 4MB page */
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            page_size = 4096 * 1024;
            pte_addr = pde_addr;

            /* Bits 20-13 provide bits 39-32 of the address, bit 21 is reserved.
             * Leave bits 20-13 in place for setting accessed/dirty bits below.
             */
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            rsvd_mask = 0x200000;
            goto do_check_protect_pse36;
        }

        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }

        /* page directory entry */
        pte_addr = (pde & ~0xfff) + ((gphys >> 10) & 0xffc);
        pte = x86_ldl_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        /* combine pde and pte user and rw protections */
        ptep &= pte | PG_NX_MASK;
        page_size = 4096;
        rsvd_mask = 0;
    }

 do_check_protect:
    rsvd_mask |= (page_size - 1) & PG_ADDRESS_MASK & ~PG_PSE_PAT_MASK;
 do_check_protect_pse36:
    if (pte & rsvd_mask) {
        goto do_fault_rsvd;
    }
    ptep ^= PG_NX_MASK;

    if (!(ptep & PG_USER_MASK)) {
        goto do_fault_protect;
    }
    if (ptep & PG_NX_MASK) {
        if (access_type == MMU_INST_FETCH) {
            goto do_fault_protect;
        }
        *prot &= ~PAGE_EXEC;
    }
    if (!(ptep & PG_RW_MASK)) {
        if (access_type == MMU_DATA_STORE) {
            goto do_fault_protect;
        }
        *prot &= ~PAGE_WRITE;
    }

    pte &= PG_ADDRESS_MASK & ~(page_size - 1);
    page_offset = gphys & (page_size - 1);
    return pte + page_offset;

 do_fault_rsvd:
    exit_info_1 |= SVM_NPTEXIT_RSVD;
 do_fault_protect:
    exit_info_1 |= SVM_NPTEXIT_P;
 do_fault:
    x86_stq_phys(cs, env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2),
                 gphys);
    exit_info_1 |= SVM_NPTEXIT_US;
    if (access_type == MMU_DATA_STORE) {
        exit_info_1 |= SVM_NPTEXIT_RW;
    } else if (access_type == MMU_INST_FETCH) {
        exit_info_1 |= SVM_NPTEXIT_ID;
    }
    if (prot) {
        exit_info_1 |= SVM_NPTEXIT_GPA;
    } else { /* page table access */
        exit_info_1 |= SVM_NPTEXIT_GPT;
    }
    cpu_vmexit(env, SVM_EXIT_NPF, exit_info_1, env->retaddr);
}

/* return value:
 * -1 = cannot handle fault
 * 0  = nothing more to do
 * 1  = generate PF fault
 */
static int handle_mmu_fault(CPUState *cs, vaddr addr, int size,
                            int is_write1, int mmu_idx)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint64_t ptep, pte;
    int32_t a20_mask;
    target_ulong pde_addr, pte_addr;
    int error_code = 0;
    int is_dirty, prot, page_size, is_write, is_user;
    hwaddr paddr;
    uint64_t rsvd_mask = PG_HI_RSVD_MASK;
    uint32_t page_offset;
    target_ulong vaddr;
#if __Use_Original_Qemu != 1 /* ours (U117) */
    /* R/W of every paging-structure entry above the one that maps the page */
    uint64_t ss_upper_rw = PG_RW_MASK;
#endif /* __Use_Original_Qemu (U117) */

#if __Use_Original_Qemu == 1 /* original QEMU (U117) */
    is_user = mmu_idx == MMU_USER_IDX;
#else /* ours (U117) */
    is_user = mmu_idx == MMU_USER_IDX || mmu_idx == MMU_SS_USER_IDX;
#endif /* __Use_Original_Qemu (U117) */
#if defined(DEBUG_MMU)
    printf("MMU fault: addr=%" VADDR_PRIx " w=%d u=%d eip=" TARGET_FMT_lx "\n",
           addr, is_write1, is_user, env->eip);
#endif
    is_write = is_write1 & 1;

    a20_mask = x86_get_a20_mask(env);
    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr;
#ifdef TARGET_X86_64
        if (!(env->hflags & HF_LMA_MASK)) {
            /* Without long mode we can only address 32bits in real mode */
            pte = (uint32_t)pte;
#if __Use_Original_Qemu != 1 /* ours (U42) */
        } else {
            /*
             * NoVmp (ledger U42): Unicorn runs IA-32e mode with paging off,
             * which hardware cannot do, so the canonical check of the paging
             * path below was skipped and the high bits were dropped later,
             * aliasing non-canonical addresses into mapped memory. The SDM
             * makes a non-canonical reference #GP(0) in 64-bit mode
             * regardless of paging (Vol1 3.3.7.1, Vol3 6.15 "Interrupt 13").
             */
            bool la57 = env->cr[4] & CR4_LA57_MASK;
            int32_t sext = la57 ? (int64_t)addr >> 56 : (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                env->error_code = 0;
                cs->exception_index = EXCP0D_GPF;
                return 1;
            }
#endif /* __Use_Original_Qemu (U42) */
        }
#endif
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        page_size = 4096;
        goto do_mapping;
    }

    if (!(env->efer & MSR_EFER_NXE)) {
        rsvd_mask |= PG_NX_MASK;
    }

    if (env->cr[4] & CR4_PAE_MASK) {
        uint64_t pde, pdpe;
        target_ulong pdpe_addr;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            bool la57 = env->cr[4] & CR4_LA57_MASK;
            uint64_t pml5e_addr, pml5e;
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = la57 ? (int64_t)addr >> 56 : (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                env->error_code = 0;
                cs->exception_index = EXCP0D_GPF;
                return 1;
            }

            if (la57) {
                pml5e_addr = ((env->cr[3] & ~0xfff) +
                        (((addr >> 48) & 0x1ff) << 3)) & a20_mask;
                pml5e_addr = get_hphys(cs, pml5e_addr, MMU_DATA_STORE, NULL);
                pml5e = x86_ldq_phys(cs, pml5e_addr);
                if (!(pml5e & PG_PRESENT_MASK)) {
                    goto do_fault;
                }
                if (pml5e & (rsvd_mask | PG_PSE_MASK)) {
                    goto do_fault_rsvd;
                }
                if (!(pml5e & PG_ACCESSED_MASK)) {
                    pml5e |= PG_ACCESSED_MASK;
                    x86_stl_phys_notdirty(cs, pml5e_addr, pml5e);
                }
                ptep = pml5e ^ PG_NX_MASK;
#if __Use_Original_Qemu != 1 /* ours (U117) */
                ss_upper_rw &= pml5e;
#endif /* __Use_Original_Qemu (U117) */
            } else {
                pml5e = env->cr[3];
                ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
            }

            pml4e_addr = ((pml5e & PG_ADDRESS_MASK) +
                    (((addr >> 39) & 0x1ff) << 3)) & a20_mask;
            pml4e_addr = get_hphys(cs, pml4e_addr, MMU_DATA_STORE, false);
            pml4e = x86_ldq_phys(cs, pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pml4e & (rsvd_mask | PG_PSE_MASK)) {
                goto do_fault_rsvd;
            }
            if (!(pml4e & PG_ACCESSED_MASK)) {
                pml4e |= PG_ACCESSED_MASK;
                x86_stl_phys_notdirty(cs, pml4e_addr, pml4e);
            }
            ptep &= pml4e ^ PG_NX_MASK;
#if __Use_Original_Qemu != 1 /* ours (U117) */
            ss_upper_rw &= pml4e;
#endif /* __Use_Original_Qemu (U117) */
            pdpe_addr = ((pml4e & PG_ADDRESS_MASK) + (((addr >> 30) & 0x1ff) << 3)) &
                a20_mask;
            pdpe_addr = get_hphys(cs, pdpe_addr, MMU_DATA_STORE, NULL);
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pdpe & rsvd_mask) {
                goto do_fault_rsvd;
            }
            ptep &= pdpe ^ PG_NX_MASK;
            if (!(pdpe & PG_ACCESSED_MASK)) {
                pdpe |= PG_ACCESSED_MASK;
                x86_stl_phys_notdirty(cs, pdpe_addr, pdpe);
            }
            if (pdpe & PG_PSE_MASK) {
                /* 1 GB page */
                page_size = 1024 * 1024 * 1024;
                pte_addr = pdpe_addr;
                pte = pdpe;
                goto do_check_protect;
            }
#if __Use_Original_Qemu != 1 /* ours (U117) */
            ss_upper_rw &= pdpe;
#endif /* __Use_Original_Qemu (U117) */
        } else
#endif
        {
            /* XXX: load them when cr3 is loaded ? */
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                a20_mask;
            pdpe_addr = get_hphys(cs, pdpe_addr, MMU_DATA_STORE, false);
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            rsvd_mask |= PG_HI_USER_MASK;
            if (pdpe & (rsvd_mask | PG_NX_MASK)) {
                goto do_fault_rsvd;
            }
            ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
        }

        pde_addr = ((pdpe & PG_ADDRESS_MASK) + (((addr >> 21) & 0x1ff) << 3)) &
            a20_mask;
        pde_addr = get_hphys(cs, pde_addr, MMU_DATA_STORE, NULL);
        pde = x86_ldq_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pde & rsvd_mask) {
            goto do_fault_rsvd;
        }
        ptep &= pde ^ PG_NX_MASK;
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte_addr = pde_addr;
            pte = pde;
            goto do_check_protect;
        }
#if __Use_Original_Qemu != 1 /* ours (U117) */
        ss_upper_rw &= pde;
#endif /* __Use_Original_Qemu (U117) */
        /* 4 KB page */
        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }
        pte_addr = ((pde & PG_ADDRESS_MASK) + (((addr >> 12) & 0x1ff) << 3)) &
            a20_mask;
        pte_addr = get_hphys(cs, pte_addr, MMU_DATA_STORE, NULL);
        pte = x86_ldq_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pte & rsvd_mask) {
            goto do_fault_rsvd;
        }
        /* combine pde and pte nx, user and rw protections */
        ptep &= pte ^ PG_NX_MASK;
        page_size = 4096;
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) &
            a20_mask;
        pde_addr = get_hphys(cs, pde_addr, MMU_DATA_STORE, NULL);
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        ptep = pde | PG_NX_MASK;

        /* if PSE bit is set, then we use a 4MB page */
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            page_size = 4096 * 1024;
            pte_addr = pde_addr;

            /* Bits 20-13 provide bits 39-32 of the address, bit 21 is reserved.
             * Leave bits 20-13 in place for setting accessed/dirty bits below.
             */
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            rsvd_mask = 0x200000;
            goto do_check_protect_pse36;
        }
#if __Use_Original_Qemu != 1 /* ours (U117) */
        ss_upper_rw &= pde;
#endif /* __Use_Original_Qemu (U117) */

        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }

        /* page directory entry */
        pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) &
            a20_mask;
        pte_addr = get_hphys(cs, pte_addr, MMU_DATA_STORE, NULL);
        pte = x86_ldl_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        /* combine pde and pte user and rw protections */
        ptep &= pte | PG_NX_MASK;
        page_size = 4096;
        rsvd_mask = 0;
    }

do_check_protect:
    rsvd_mask |= (page_size - 1) & PG_ADDRESS_MASK & ~PG_PSE_PAT_MASK;
do_check_protect_pse36:
    if (pte & rsvd_mask) {
        goto do_fault_rsvd;
    }
    ptep ^= PG_NX_MASK;

    /* can the page can be put in the TLB?  prot will tell us */
    if (is_user && !(ptep & PG_USER_MASK)) {
        goto do_fault_protect;
    }

    prot = 0;
#if __Use_Original_Qemu != 1 /* ours (U117) */
    /*
     * NoVmp (ledger U117): a shadow-stack access (SDM Vol3 5.6.1) is allowed
     * only to a shadow-stack address - R/W = 0 and D = 1 in the entry that maps
     * the page, R/W = 1 in every other paging-structure entry - of its own mode
     * (user accesses to user addresses, supervisor accesses to supervisor
     * addresses), and then both reads and writes are allowed. Protection keys
     * apply as to ordinary data accesses (below).
     */
    if (mmu_idx == MMU_SS_KSMAP_IDX || mmu_idx == MMU_SS_USER_IDX) {
        if ((pte & PG_RW_MASK) || !(pte & PG_DIRTY_MASK) ||
            !(ss_upper_rw & PG_RW_MASK) || !(ptep & PG_USER_MASK) != !is_user) {
            goto do_fault_protect;
        }
        prot = PAGE_READ | PAGE_WRITE;
    } else
#endif /* __Use_Original_Qemu (U117) */
    if (mmu_idx != MMU_KSMAP_IDX || !(ptep & PG_USER_MASK)) {
        prot |= PAGE_READ;
        if ((ptep & PG_RW_MASK) || (!is_user && !(env->cr[0] & CR0_WP_MASK))) {
            prot |= PAGE_WRITE;
        }
    }
#if __Use_Original_Qemu == 1 /* original QEMU (U117) */
    if (!(ptep & PG_NX_MASK) &&
        (mmu_idx == MMU_USER_IDX ||
         !((env->cr[4] & CR4_SMEP_MASK) && (ptep & PG_USER_MASK)))) {
        prot |= PAGE_EXEC;
    }
#else /* ours (U117) */
    if (!(ptep & PG_NX_MASK) && mmu_idx != MMU_SS_KSMAP_IDX && mmu_idx != MMU_SS_USER_IDX &&
        (mmu_idx == MMU_USER_IDX ||
         !((env->cr[4] & CR4_SMEP_MASK) && (ptep & PG_USER_MASK)))) {
        prot |= PAGE_EXEC;
    }
#endif /* __Use_Original_Qemu (U117) */
    if ((env->cr[4] & CR4_PKE_MASK) && (env->hflags & HF_LMA_MASK) &&
        (ptep & PG_USER_MASK) && env->pkru) {
        uint32_t pk = (pte & PG_PKRU_MASK) >> PG_PKRU_BIT;
        uint32_t pkru_ad = (env->pkru >> pk * 2) & 1;
        uint32_t pkru_wd = (env->pkru >> pk * 2) & 2;
        uint32_t pkru_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

        if (pkru_ad) {
            pkru_prot &= ~(PAGE_READ | PAGE_WRITE);
        } else if (pkru_wd && (is_user || env->cr[0] & CR0_WP_MASK)) {
            pkru_prot &= ~PAGE_WRITE;
        }

        prot &= pkru_prot;
        if ((pkru_prot & (1 << is_write1)) == 0) {
            assert(is_write1 != 2);
            error_code |= PG_ERROR_PK_MASK;
            goto do_fault_protect;
        }
    }

    if ((prot & (1 << is_write1)) == 0) {
        goto do_fault_protect;
    }

    /* yes, it can! */
    is_dirty = is_write && !(pte & PG_DIRTY_MASK);
    if (!(pte & PG_ACCESSED_MASK) || is_dirty) {
        pte |= PG_ACCESSED_MASK;
        if (is_dirty) {
            pte |= PG_DIRTY_MASK;
        }
        x86_stl_phys_notdirty(cs, pte_addr, pte);
    }

    if (!(pte & PG_DIRTY_MASK)) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
        assert(!is_write);
        prot &= ~PAGE_WRITE;
    }

 do_mapping:

    pte = pte & a20_mask;

    /* align to page_size */
    pte &= PG_ADDRESS_MASK & ~(page_size - 1);
    page_offset = addr & (page_size - 1);
    paddr = get_hphys(cs, pte + page_offset, is_write1, &prot);

    /* Even if 4MB pages, we map only one 4KB page in the cache to
       avoid filling it too fast */
    vaddr = addr & TARGET_PAGE_MASK;
    paddr &= TARGET_PAGE_MASK;
    assert(prot & (1 << is_write1));

    tlb_set_page_with_attrs(cs, vaddr, paddr, cpu_get_mem_attrs(env),
                            prot, mmu_idx, page_size);
    return 0;
 do_fault_rsvd:
    error_code |= PG_ERROR_RSVD_MASK;
 do_fault_protect:
    error_code |= PG_ERROR_P_MASK;
 do_fault:
    error_code |= (is_write << PG_ERROR_W_BIT);
    if (is_user)
        error_code |= PG_ERROR_U_MASK;
#if __Use_Original_Qemu != 1 /* ours (U117) */
    if (mmu_idx == MMU_SS_KSMAP_IDX || mmu_idx == MMU_SS_USER_IDX) {
        error_code |= PG_ERROR_SSTK_MASK;
    }
#endif /* __Use_Original_Qemu (U117) */
    if (is_write1 == 2 &&
        (((env->efer & MSR_EFER_NXE) &&
          (env->cr[4] & CR4_PAE_MASK)) ||
         (env->cr[4] & CR4_SMEP_MASK)))
        error_code |= PG_ERROR_I_D_MASK;
    if (env->intercept_exceptions & (1 << EXCP0E_PAGE)) {
        /* cr2 is not modified in case of exceptions */
        x86_stq_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2),
                 addr);
    } else {
        env->cr[2] = addr;
    }
    env->error_code = error_code;
    cs->exception_index = EXCP0E_PAGE;
    return 1;
}

#if __Use_Original_Qemu != 1 /* ours (U51/U52) */
/*
 * NoVmp (ledger U51): segment of a non-canonical data reference in 64-bit
 * mode. SDM Vol1 3.3.7.1: "in the case of explicit or implied stack
 * references, a stack fault (#SS) is generated" - PUSH/POP-related
 * instructions and RSP/RBP-based operands; an FS/GS override on an RSP/RBP
 * operand makes it #GP, and CS/DS/ES/SS overrides are ignored (so ss:[rcx]
 * is #GP). The MMU does not know the segment, so the faulting instruction
 * is decoded here; this only runs on the canonical-fault path.
 */
static uint8_t ss_code(CPUX86State *env, target_ulong pc, int *len)
{
    return cpu_ldub_code(env, pc + (*len)++);
}

static bool x86_canonical_fault_is_ss(CPUX86State *env, vaddr addr)
{
    /* 0F-map opcodes without a ModRM byte */
    static const uint8_t no_modrm_0f[] = {
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0b, 0x0e, 0x30, 0x31, 0x32, 0x33,
        0x34, 0x35, 0x37, 0x77, 0xa0, 0xa1, 0xa2, 0xa8, 0xa9, 0xaa,
    };
    target_ulong pc = env->segs[R_CS].base + env->eip;
    int len = 0, rex = 0, map = 0, fsgs = -1, i;
    bool a32 = false, modrm_present, implicit = false, vex = false;
    uint8_t b, modrm, sib = 0;
    int mod, rm, base = -1, index = -1, scale = 0;
    int64_t disp = 0;
    target_ulong ea;

    /* legacy prefixes and REX */
    for (;;) {
        if (len >= 15) {
            return false;
        }
        b = ss_code(env, pc, &len);
        if (b == 0x64 || b == 0x65) {
            fsgs = b == 0x64 ? R_FS : R_GS;
        } else if (b == 0x67) {
            a32 = true;
        } else if (b == 0x26 || b == 0x2e || b == 0x36 || b == 0x3e ||
                   b == 0x66 || b == 0xf0 || b == 0xf2 || b == 0xf3) {
            /* CS/DS/ES/SS overrides are ignored in 64-bit mode */
        } else if ((b & 0xf0) == 0x40) {
            rex = b;
            continue;
        } else {
            break;
        }
        rex = 0;        /* REX must immediately precede the opcode */
    }

    /* opcode */
    if (b == 0xc4 || b == 0xc5 || b == 0x62) {        /* VEX / EVEX */
        uint8_t p0 = ss_code(env, pc, &len);
        vex = true;
        if (b == 0xc5) {
            map = 1;
            rex = 0x40 | (p0 & 0x80 ? 0 : 4);
        } else {
            map = p0 & 7;
            rex = 0x40 | ((~p0 >> 5) & 7);              /* R X B */
            ss_code(env, pc, &len);
            if (b == 0x62) {
                ss_code(env, pc, &len);
            }
        }
        b = ss_code(env, pc, &len);
        modrm_present = !(map == 1 && b == 0x77);       /* VZEROUPPER/ALL */
    } else if (b == 0x0f) {
        b = ss_code(env, pc, &len);
        if (b == 0x38 || b == 0x3a) {
            map = b == 0x38 ? 2 : 3;
            b = ss_code(env, pc, &len);
            modrm_present = true;
        } else {
            map = 1;
            modrm_present = !((b >= 0x80 && b <= 0x8f) || (b >= 0xc8 && b <= 0xcf));
            for (i = 0; i < (int)ARRAY_SIZE(no_modrm_0f); i++) {
                if (b == no_modrm_0f[i]) {
                    modrm_present = false;
                }
            }
            /* push/pop fs, gs */
            implicit = b == 0xa0 || b == 0xa1 || b == 0xa8 || b == 0xa9;
        }
    } else {
        /* one-byte map: opcodes with a ModRM byte */
        modrm_present = (b < 0x40 && (b & 7) < 4) || b == 0x63 || b == 0x69 ||
                        b == 0x6b || (b >= 0x80 && b <= 0x8f) ||
                        b == 0xc0 || b == 0xc1 || b == 0xc6 || b == 0xc7 ||
                        (b >= 0xd0 && b <= 0xd3) || (b >= 0xd8 && b <= 0xdf) ||
                        b == 0xf6 || b == 0xf7 || b == 0xfe || b == 0xff;
        /* implied stack references */
        implicit = (b >= 0x50 && b <= 0x5f) || b == 0x68 || b == 0x6a ||
                   b == 0x9c || b == 0x9d || b == 0xc2 || b == 0xc3 ||
                   b == 0xc8 || b == 0xc9 || b == 0xca || b == 0xcb ||
                   b == 0xcf || b == 0xe8;
    }
    if (!modrm_present) {
        return implicit;
    }

    /* ModRM / SIB / displacement */
    modrm = ss_code(env, pc, &len);
    mod = modrm >> 6;
    rm = modrm & 7;
    if (!vex && map == 0) {
        int reg = (modrm >> 3) & 7;
        if ((b == 0x8f && reg == 0) ||                   /* pop r/m */
            (b == 0xff && (reg == 2 || reg == 3 || reg == 6))) {  /* call, push r/m */
            implicit = true;
        }
    }
    if (mod == 3) {
        return implicit;
    }
    if (rm == 4) {
        sib = ss_code(env, pc, &len);
        scale = sib >> 6;
        index = ((sib >> 3) & 7) | (rex & 2 ? 8 : 0);
        if (index == 4) {
            index = -1;                                  /* no index */
        }
        base = (sib & 7) | (rex & 1 ? 8 : 0);
        if ((sib & 7) == 5 && mod == 0) {
            base = -1;                                   /* disp32, no base */
            mod = 2;
        }
    } else if (rm == 5 && mod == 0) {
        base = -2;                                       /* RIP-relative */
        mod = 2;
    } else {
        base = rm | (rex & 1 ? 8 : 0);
    }

    if (!implicit) {
        /* only the explicit operand: SS iff RSP/RBP based, no FS/GS */
        return fsgs < 0 && (base == R_ESP || base == R_EBP);
    }
    /*
     * PUSH/POP/CALL r/m: explicit operand and implied stack reference. The
     * explicit operand is the one that faulted iff the fault lies within it.
     */
    if (mod == 1) {
        disp = (int8_t)ss_code(env, pc, &len);
    } else if (mod == 2) {
        uint32_t d32 = 0;
        for (i = 0; i < 4; i++) {           /* bytes in order: one call each */
            d32 |= (uint32_t)ss_code(env, pc, &len) << (8 * i);
        }
        disp = (int32_t)d32;
    }
    ea = disp;
    if (base == -2) {
        ea += env->segs[R_CS].base + env->eip + len;
    } else if (base >= 0) {
        ea += env->regs[base];
    }
    if (index >= 0) {
        ea += env->regs[index] << scale;
    }
    if (a32) {
        ea = (uint32_t)ea;
    }
    if (fsgs >= 0) {
        ea += env->segs[fsgs].base;
    }
    if (addr - ea < 0x10) {
        return fsgs < 0 && (base == R_ESP || base == R_EBP);
    }
    return true;
}

/*
 * NoVmp (ledger U52): a near branch to a non-canonical target raises #GP(0)
 * on the branch itself - RIP stays at the branch, nothing is pushed or
 * popped (SDM Vol2 JMP/CALL/RET: "IF tempRIP is not canonical THEN #GP(0)"
 * before the push / RIP update). Unicorn loaded the target into RIP and
 * faulted on the fetch there. Called before any side effect.
 */
bool x86_ip_is_canonical(CPUX86State *env, target_ulong ip)
{
    int shift = env->cr[4] & CR4_LA57_MASK ? 56 : 47;
    int64_t sext = (int64_t)ip >> shift;

    /* UC_TLB_VIRTUAL bypasses the x86 MMU (a flat 64-bit space without the
       canonical rule, as for data, U42): only the CPU MMU mode checks */
    if (env->uc->cpu->cc->tlb_fill != env->uc->cpu->cc->tlb_fill_cpu) {
        return true;
    }
    return sext == 0 || sext == -1;
}

void helper_check_canonical_ip(CPUX86State *env, target_ulong ip)
{
    if (!x86_ip_is_canonical(env, ip)) {
        raise_exception_err_ra(env, EXCP0D_GPF, 0, GETPC());
    }
}

#endif /* __Use_Original_Qemu (U51/U52) */
bool x86_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->retaddr = retaddr;
    if (handle_mmu_fault(cs, addr, size, access_type, mmu_idx)) {
        if (probe)
	    return false;
#if __Use_Original_Qemu != 1 /* ours (U51) */
        /* the MMU raises #GP only for non-canonical addresses (U51) */
        if (cs->exception_index == EXCP0D_GPF &&
            access_type != MMU_INST_FETCH && (env->hflags & HF_CS64_MASK) &&
            retaddr && cpu_restore_state(cs, retaddr, true)) {
            if (x86_canonical_fault_is_ss(env, addr)) {
                cs->exception_index = EXCP0C_STACK;
            }
            raise_exception_err(env, cs->exception_index, 0);
        }
#endif /* __Use_Original_Qemu (U51) */
        /* FIXME: On error in get_hphys we have already jumped out.  */
        raise_exception_err_ra(env, cs->exception_index,
                               env->error_code, retaddr);
    }
    return true;
}

G_NORETURN void x86_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                            MMUAccessType access_type,
                                            int mmu_idx, uintptr_t retaddr)
{
    X86CPU *cpu = X86_CPU(cs);

    handle_unaligned_access(&cpu->env, vaddr, access_type, retaddr);
}
