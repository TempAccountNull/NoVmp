/*
 *  x86 memory access helpers
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
#include "qemu/int128.h"
#include "qemu/atomic128.h"
#include "tcg/tcg.h"
#if __Use_Original_Qemu != 1 /* ours (U592) */
#include "uc_priv.h"
#endif /* __Use_Original_Qemu (U592) */

#if __Use_Original_Qemu != 1 /* ours (U592) */
/*
 * NoVmp (ledger U592): a store that the emulator splits (the 8-byte parts of a 16/32-byte
 * SSE/AVX store) or performs inside a helper (x87 FST/FIST, which then pops) must fault
 * before anything changes, as the CPU does (SDM Vol3A 6.5: the state is that before the
 * faulting instruction; emu-alltest cases_fixes2). Every page of [a0, a0 + len) is translated
 * for a store first (#PF with paging); a page that Unicorn has not mapped, or maps read-only,
 * gets the first operand byte on it stored now - the UC_HOOK_MEM_WRITE_UNMAPPED / _PROT event
 * the plain store would raise. If no hook maps the page the instruction stops here with
 * nothing written (and nothing popped); if one does, the byte is stored again by the access.
 * 'src' is the little-endian image of the operand that is about to be stored.
 */
void x86_probe_store(CPUX86State *env, target_ulong a0, uint32_t len, const uint8_t *src,
                     uintptr_t ra)
{
    struct uc_struct *uc = env->uc;
    int mmu_idx = cpu_mmu_index(env, false);
    uint32_t off, n;
    target_ulong p;

    if (len == 0) {
        return;
    }
    if (!(env->hflags & HF_CS64_MASK) && a0 + len - 1 > 0xffffffffULL) {
        return;     /* wraps at 4 GiB outside 64-bit mode: left to the stores (U490) */
    }
    for (off = 0, p = a0; off < len; off += n, p += n) {
        n = (uint32_t)(TARGET_PAGE_SIZE - (p & ~TARGET_PAGE_MASK));
        if (n > len - off) {
            n = len - off;
        }
        probe_access(env, p, (int)n, MMU_DATA_STORE, mmu_idx, ra);
    }
    for (off = 0, p = a0; off < len; off += n, p += n) {
        target_ulong paddr;
        MemoryRegion *mr;

        n = (uint32_t)(TARGET_PAGE_SIZE - (p & ~TARGET_PAGE_MASK));
        if (n > len - off) {
            n = len - off;
        }
        if (!tlb_vaddr_to_paddr(env, p, MMU_DATA_STORE, mmu_idx, &paddr)) {
            continue;
        }
        mr = uc->memory_mapping(uc, paddr);
        if (mr == NULL || !(mr->perms & UC_PROT_WRITE)) {
            int old_size = uc->size_recur_mem;

            uc->size_recur_mem = (int)len;      /* no UC_HOOK_MEM_WRITE for the probe */
            cpu_stb_mmuidx_ra(env, p, src[off], mmu_idx, ra);
            uc->size_recur_mem = old_size;
            if (uc->invalid_error != UC_ERR_OK && uc->nested_level > 0 && !uc->cpu->stopped) {
                cpu_loop_exit_restore(uc->cpu, ra);
            }
        }
    }
}

/*
 * NoVmp (ledger U592): a 16/32-byte SSE/AVX store (gen_sto_env_A0 / gen_sty_env_A0) is a
 * sequence of 8-byte stores, so one that crosses into a page that faults stored its first
 * part (upstream 46c684c862 makes the 128-bit case one i128 access that translates both
 * pages first - not available with our TCG; its 256-bit store is still two halves).
 * Called (inline test in gen_vec_store_probe) only when the operand crosses a page.
 */
void helper_probe_vec_store(CPUX86State *env, target_ulong a0, void *src, uint32_t len)
{
    x86_probe_store(env, a0, len, (const uint8_t *)src, GETPC());
}
#endif /* __Use_Original_Qemu (U592) */

#if __Use_Original_Qemu != 1 /* ours (U701) */
static void x86_probe_write_part(CPUX86State *env, target_ulong a0, uint32_t len, int mmu_idx,
                                 uintptr_t ra)
{
    struct uc_struct *uc = env->uc;
    uint32_t off, n;
    target_ulong p;

    for (off = 0, p = a0; off < len; off += n, p += n) {
        n = (uint32_t)(TARGET_PAGE_SIZE - (p & ~TARGET_PAGE_MASK));
        if (n > len - off) {
            n = len - off;
        }
        probe_access(env, p, (int)n, MMU_DATA_STORE, mmu_idx, ra);
    }
    for (off = 0, p = a0; off < len; off += n, p += n) {
        target_ulong paddr;
        MemoryRegion *mr;

        n = (uint32_t)(TARGET_PAGE_SIZE - (p & ~TARGET_PAGE_MASK));
        if (n > len - off) {
            n = len - off;
        }
        if (!tlb_vaddr_to_paddr(env, p, MMU_DATA_STORE, mmu_idx, &paddr)) {
            continue;
        }
        mr = uc->memory_mapping(uc, paddr);
        if (mr == NULL) {
            /* UC_HOOK_MEM_READ_UNMAPPED; no hook: the instruction stops in load_helper */
            (void)cpu_ldub_mmuidx_ra(env, p, mmu_idx, ra);
            if (uc->invalid_error != UC_ERR_OK && uc->nested_level > 0 && !uc->cpu->stopped) {
                cpu_loop_exit_restore(uc->cpu, ra);
            }
            if (!tlb_vaddr_to_paddr(env, p, MMU_DATA_STORE, mmu_idx, &paddr)) {
                continue;
            }
            mr = uc->memory_mapping(uc, paddr);     /* a hook mapped it */
            if (mr == NULL) {
                continue;
            }
        }
        if (!(mr->perms & UC_PROT_WRITE)) {
            /* store the byte that is there (read without hooks): UC_HOOK_MEM_WRITE_PROT */
            int old_size = uc->size_recur_mem;
            uint8_t b = 0;

            uc->read_mem(&uc->address_space_memory, paddr, &b, 1);
            uc->size_recur_mem = (int)len;      /* no UC_HOOK_MEM_WRITE for the probe */
            cpu_stb_mmuidx_ra(env, p, b, mmu_idx, ra);
            uc->size_recur_mem = old_size;
            if (uc->invalid_error != UC_ERR_OK && uc->nested_level > 0 && !uc->cpu->stopped) {
                cpu_loop_exit_restore(uc->cpu, ra);
            }
        }
    }
}

/*
 * NoVmp (ledger U701): fault if [a0, a0 + len) cannot be written, without writing anything
 * (the bytes keep their values). For instructions that store several parts (ENTER, far CALL,
 * ...): a page fault is reported before the first part is stored, "a program-state change does
 * not normally accompany a page-fault exception, because the instruction that causes the
 * exception to be generated is not executed" (SDM Vol3A 6.15, Interrupt 14), and for ENTER's
 * check of its final stack pointer (SDM Vol2A ENTER). Every page is translated for a store
 * (#PF with paging); a page Unicorn has not mapped is reported through a one-byte read of it
 * (UC_HOOK_MEM_READ_UNMAPPED, as U480 does), a page mapped without UC_PROT_WRITE through a
 * store of its own byte (UC_HOOK_MEM_WRITE_PROT); if no hook makes it accessible, the
 * instruction stops here. Outside 64-bit mode the range wraps at 4 GiB (U490).
 */
void x86_probe_write(CPUX86State *env, target_ulong a0, uint32_t len, uintptr_t ra)
{
    x86_probe_write_mmu(env, a0, len, cpu_mmu_index(env, false), ra);
}

/* U708: the same with an explicit MMU index (stack pushes at another privilege level) */
void x86_probe_write_mmu(CPUX86State *env, target_ulong a0, uint32_t len, int mmu_idx,
                         uintptr_t ra)
{
    if (len == 0) {
        return;
    }
    if (!(env->hflags & HF_CS64_MASK)) {
        a0 = (uint32_t)a0;
        if (a0 + len - 1 > 0xffffffffULL) {
            uint32_t n = (uint32_t)(0x100000000ULL - a0);

            x86_probe_write_part(env, a0, n, mmu_idx, ra);
            x86_probe_write_part(env, 0, len - n, mmu_idx, ra);
            return;
        }
    }
    x86_probe_write_part(env, a0, len, mmu_idx, ra);
}

void helper_probe_write(CPUX86State *env, target_ulong a0, uint32_t len)
{
    x86_probe_write(env, a0, len, GETPC());
}
#endif /* __Use_Original_Qemu (U701) */
#if __Use_Original_Qemu != 1 /* ours (U750) */
/*
 * NoVmp (ledger U750): x86_probe_write_mmu for a shadow-stack slot whose linear address the
 * caller has already formed for the mode of that shadow stack (32-bit outside 64-bit mode,
 * canonical in it), which can differ from the current mode on a far transfer: no 4-GiB
 * truncation here.
 */
void x86_probe_write_la(CPUX86State *env, target_ulong a0, uint32_t len, int mmu_idx,
                        uintptr_t ra)
{
    if (len != 0) {
        x86_probe_write_part(env, a0, len, mmu_idx, ra);
    }
}
#endif /* __Use_Original_Qemu (U750) */


void helper_cmpxchg8b_unlocked(CPUX86State *env, target_ulong a0)
{
    uintptr_t ra = GETPC();
    uint64_t oldv, cmpv, newv;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);

    cmpv = deposit64(env->regs[R_EAX], 32, 32, env->regs[R_EDX]);
    newv = deposit64(env->regs[R_EBX], 32, 32, env->regs[R_ECX]);

    oldv = cpu_ldq_data_ra(env, a0, ra);
    newv = (cmpv == oldv ? newv : oldv);
#if __Use_Original_Qemu != 1 /* ours (U708) */
    /*
     * U708: the destination is written either way; a store fault leaves EDX:EAX and ZF
     * unchanged (SDM Vol3A 6.15). Unicorn's store to a page it has not mapped or maps
     * read-only only requests an exit, so the helper went on and updated EDX:EAX / ZF.
     */
    x86_probe_write(env, a0, 8, ra);
#endif /* __Use_Original_Qemu (U708) */
    /* always do the store */
    cpu_stq_data_ra(env, a0, newv, ra);

    if (oldv == cmpv) {
        eflags |= CC_Z;
    } else {
        env->regs[R_EAX] = (uint32_t)oldv;
        env->regs[R_EDX] = (uint32_t)(oldv >> 32);
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}

#if __Use_Original_Qemu != 1 /* ours (U73) */
/* NoVmp (ledger U73): MOVDIR64B, see gen_MOVDIR64B; all 64 bytes are read first */
void helper_movdir64b(CPUX86State *env, target_ulong dst, target_ulong src)
{
    uintptr_t ra = GETPC();
    uint64_t buf[8];
    int i;

    if (dst & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    for (i = 0; i < 8; i++) {
        buf[i] = cpu_ldq_data_ra(env, src + 8 * i, ra);
    }
    for (i = 0; i < 8; i++) {
        cpu_stq_data_ra(env, dst + 8 * i, buf[i], ra);
    }
}

#endif /* __Use_Original_Qemu (U73) */
#if __Use_Original_Qemu != 1 /* ours (U101) */
/*
 * NoVmp (ledger U101): RAO-INT AADD / AAND / AOR / AXOR m32/m64, r (ISE
 * 319433-062): dest := dest op src, no flags. #GP(0) unless the operand is
 * naturally aligned. desc = op << 4 | MemOp size (op: 0 add, 1 and, 2 or,
 * 3 xor). Unicorn runs one vCPU, so load + store is atomic; all memory is
 * write-back, so the "not WB" #GP cannot occur.
 */
void helper_rao(CPUX86State *env, target_ulong a0, target_ulong src, uint32_t desc)
{
    uintptr_t ra = GETPC();
    bool q = (desc & 0xf) == MO_64;
    uint64_t v;

    if (a0 & (q ? 7 : 3)) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    v = q ? cpu_ldq_data_ra(env, a0, ra) : cpu_ldl_data_ra(env, a0, ra);
    switch (desc >> 4) {
    case 0:
        v += src;
        break;
    case 1:
        v &= src;
        break;
    case 2:
        v |= src;
        break;
    default:
        v ^= src;
        break;
    }
    if (q) {
        cpu_stq_data_ra(env, a0, v, ra);
    } else {
        cpu_stl_data_ra(env, a0, (uint32_t)v, ra);
    }
}

#endif /* __Use_Original_Qemu (U101) */
#if __Use_Original_Qemu != 1 /* ours (U112) */
/*
 * NoVmp (ledger U112): ENQCMD / ENQCMDS m512 to ES:[r] (SDM Vol2 ENQCMD,
 * ENQCMDS). ENQCMD: #GP(0) if IA32_PASID[31] (valid) = 0; ENQCMDS: #GP(0) if
 * CPL > 0. #GP(0) if the destination is not 64-byte aligned. The 64 source
 * bytes are read with ordinary loads (any alignment); #GP(0) if source bits
 * 31:0 (ENQCMD) / 30:20 (ENQCMDS) are not zero. The command would be
 * (ENQCMD) source[511:32] : 0 (bit 31, user) : 0 (30:20) : IA32_PASID[19:0],
 * (ENQCMDS) source with bits 30:20 = 0. No device implements an enqueue
 * register in this emulator, so every destination - memory or nothing - is
 * "not an enqueue register": the store is dropped (written neither to MMIO nor
 * to memory) and the retry status is returned, ZF = 1; CF/PF/AF/SF/OF = 0.
 * With paging enabled the destination is still translated for a write
 * (#PF); without paging there is nothing to translate.
 */
void helper_enqcmd(CPUX86State *env, target_ulong dst, target_ulong src,
                   uint32_t supervisor)
{
    uintptr_t ra = GETPC();
    uint64_t buf[8];
    int i;

    if (supervisor) {
        if ((env->hflags & HF_CPL_MASK) != 0) {
            raise_exception_ra(env, EXCP0D_GPF, ra);
        }
    } else if (!(env->pasid & (1ull << 31))) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (dst & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    for (i = 0; i < 8; i++) {
        buf[i] = cpu_ldq_data_ra(env, src + 8 * i, ra);
    }
    if (supervisor ? (buf[0] & 0x7ff00000u) : (uint32_t)buf[0]) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }
    if (env->cr[0] & CR0_PG_MASK) {
        probe_write(env, dst, 64, cpu_mmu_index(env, false), ra);
    }
    CC_SRC = CC_Z;
}

#endif /* __Use_Original_Qemu (U112) */
void helper_cmpxchg8b(CPUX86State *env, target_ulong a0)
{
#ifdef CONFIG_ATOMIC64
    uint64_t oldv, cmpv, newv;
    int eflags;

    eflags = cpu_cc_compute_all(env, CC_OP);

    cmpv = deposit64(env->regs[R_EAX], 32, 32, env->regs[R_EDX]);
    newv = deposit64(env->regs[R_EBX], 32, 32, env->regs[R_ECX]);

    {
        uintptr_t ra = GETPC();
        int mem_idx = cpu_mmu_index(env, false);
        TCGMemOpIdx oi = make_memop_idx(MO_TEQ, mem_idx);
        oldv = helper_atomic_cmpxchgq_le_mmu(env, a0, cmpv, newv, oi, ra);
    }

    if (oldv == cmpv) {
        eflags |= CC_Z;
    } else {
        env->regs[R_EAX] = (uint32_t)oldv;
        env->regs[R_EDX] = (uint32_t)(oldv >> 32);
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
#else
    cpu_loop_exit_atomic(env_cpu(env), GETPC());
#endif /* CONFIG_ATOMIC64 */
}

#ifdef TARGET_X86_64
void helper_cmpxchg16b_unlocked(CPUX86State *env, target_ulong a0)
{
    uintptr_t ra = GETPC();
    Int128 oldv, cmpv, newv;
    uint64_t o0, o1;
    int eflags;
    bool success;

    if ((a0 & 0xf) != 0) {
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    eflags = cpu_cc_compute_all(env, CC_OP);

    cmpv = int128_make128(env->regs[R_EAX], env->regs[R_EDX]);
    newv = int128_make128(env->regs[R_EBX], env->regs[R_ECX]);

    o0 = cpu_ldq_data_ra(env, a0 + 0, ra);
    o1 = cpu_ldq_data_ra(env, a0 + 8, ra);

    oldv = int128_make128(o0, o1);
    success = int128_eq(oldv, cmpv);
    if (!success) {
        newv = oldv;
    }

#if __Use_Original_Qemu != 1 /* ours (U708) */
    x86_probe_write(env, a0, 16, ra);       /* as CMPXCHG8B (U708) */
#endif /* __Use_Original_Qemu (U708) */
    cpu_stq_data_ra(env, a0 + 0, int128_getlo(newv), ra);
    cpu_stq_data_ra(env, a0 + 8, int128_gethi(newv), ra);

    if (success) {
        eflags |= CC_Z;
    } else {
        env->regs[R_EAX] = int128_getlo(oldv);
        env->regs[R_EDX] = int128_gethi(oldv);
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}

void helper_cmpxchg16b(CPUX86State *env, target_ulong a0)
{
    uintptr_t ra = GETPC();

    if ((a0 & 0xf) != 0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    } else {
#if HAVE_CMPXCHG128 == 1
        int eflags = cpu_cc_compute_all(env, CC_OP);

        Int128 cmpv = int128_make128(env->regs[R_EAX], env->regs[R_EDX]);
        Int128 newv = int128_make128(env->regs[R_EBX], env->regs[R_ECX]);

        int mem_idx = cpu_mmu_index(env, false);
        TCGMemOpIdx oi = make_memop_idx(MO_TEQ | MO_ALIGN_16, mem_idx);
        Int128 oldv = helper_atomic_cmpxchgo_le_mmu(env, a0, cmpv,
                                                    newv, oi, ra);

        if (int128_eq(oldv, cmpv)) {
            eflags |= CC_Z;
        } else {
            env->regs[R_EAX] = int128_getlo(oldv);
            env->regs[R_EDX] = int128_gethi(oldv);
            eflags &= ~CC_Z;
        }
        CC_SRC = eflags;
#else
        cpu_loop_exit_atomic(env_cpu(env), ra);
#endif
    }
}
#endif

void helper_boundw(CPUX86State *env, target_ulong a0, int v)
{
    int low, high;

    low = cpu_ldsw_data_ra(env, a0, GETPC());
    high = cpu_ldsw_data_ra(env, a0 + 2, GETPC());
    v = (int16_t)v;
    if (v < low || v > high) {
        if (env->hflags & HF_MPX_EN_MASK) {
            env->bndcs_regs.sts = 0;
        }
        raise_exception_ra(env, EXCP05_BOUND, GETPC());
    }
}

void helper_boundl(CPUX86State *env, target_ulong a0, int v)
{
    int low, high;

    low = cpu_ldl_data_ra(env, a0, GETPC());
    high = cpu_ldl_data_ra(env, a0 + 4, GETPC());
    if (v < low || v > high) {
        if (env->hflags & HF_MPX_EN_MASK) {
            env->bndcs_regs.sts = 0;
        }
        raise_exception_ra(env, EXCP05_BOUND, GETPC());
    }
}
